#pragma once
/**
 * jobs::JobSystem — petit ordonnanceur de tâches sur un vivier de fils
 * persistants, bâti sur `sdl3::Thread`/`Mutex`/`Condition`/`AtomicInt`.
 *
 * Raison d'être : plusieurs boucles de ce dépôt sont ouvertement
 * parallélisables (les paires de la phase large de `physics::`, la génération
 * de manifolds, la préparation par objet d'une grande scène), mais rien ne
 * permettait de les répartir — créer un `sdl3::Thread` par lot coûterait plus
 * cher que le travail lui-même. Un vivier créé une fois, réveillé par
 * condition, règle ça.
 *
 * ── Ce que ce système N'EST PAS ─────────────────────────────────────────
 * Pas de vol de tâches, pas de dépendances entre tâches, pas de priorités.
 * `ParallelFor` est BLOQUANT et le fil appelant participe au travail — c'est
 * le seul motif dont ce dépôt a besoin, et c'est celui qui se raisonne le
 * plus facilement (aucune tâche ne survit à l'appel qui l'a créée, donc
 * aucune question de durée de vie sur ce qu'elle capture).
 *
 * ── Zéro fil = exécution en ligne ───────────────────────────────────────
 * `JobSystem(0)` n'ouvre aucun fil et exécute tout sur l'appelant. C'est le
 * mode de repli sur une machine monocœur, et surtout le moyen de vérifier en
 * test qu'un calcul donne EXACTEMENT le même résultat avec et sans
 * parallélisme.
 *
 * ── Aucune exception ────────────────────────────────────────────────────
 * Conformément au reste du dépôt (cf. memory/feedback_no_exceptions.md) : une
 * tâche ne doit pas lever. Elle ne le peut pas davantage ici que partout
 * ailleurs — il n'y a simplement aucun mécanisme pour rattraper quoi que ce
 * soit à travers une frontière de fil.
 */
#include <functional>
#include <utility>
#include <vector>

#include "../core/core.hpp"
#include "../sdl3/misc.hpp"
#include "../sdl3/thread.hpp"

namespace jobs {

class JobSystem {
public:
	/// Nombre de fils conseillé pour un vivier : tous les cœurs logiques sauf
	/// un, celui-là restant au fil appelant (qui participe au travail dans
	/// `ParallelFor`). Au moins 0, jamais négatif.
	[[nodiscard]] static size_t RecommendedWorkerCount() noexcept {
		int cores = sdl3::system::CpuCount();
		return cores > 1 ? size_t(cores - 1) : 0;
	}

	/// `workerCount` fils secondaires. 0 = tout s'exécute en ligne sur
	/// l'appelant (cf. en-tête).
	explicit JobSystem(size_t workerCount = RecommendedWorkerCount()) {
		if (workerCount == 0)
			return;

		auto mutexResult = sdl3::Mutex::Create();
		auto conditionResult = sdl3::Condition::Create();
		auto doneResult = sdl3::Condition::Create();
		// Sans primitive de synchronisation, on reste en mode « en ligne » :
		// dégradation silencieuse mais parfaitement correcte, plutôt qu'un
		// vivier à moitié monté.
		if (!mutexResult || !conditionResult || !doneResult)
			return;
		m_mutex = Some(std::move(mutexResult.Value()));
		m_wakeUp = Some(std::move(conditionResult.Value()));
		m_allDone = Some(std::move(doneResult.Value()));

		m_workers.reserve(workerCount);
		for (size_t i = 0; i < workerCount; ++i) {
			auto thread = sdl3::Thread::Create([this]() -> int {
				WorkerLoop();
				return 0;
			}, String::Format("job-worker-%d", int(i)));
			if (!thread)
				break; // on tourne avec les fils effectivement obtenus
			m_workers.push_back(std::move(thread.Value()));
		}
	}

	JobSystem(const JobSystem &) = delete;
	JobSystem &operator=(const JobSystem &) = delete;

	/// Arrête et joint tous les fils. Les tâches encore en file sont
	/// TERMINÉES avant l'arrêt (un `ParallelFor` bloquant ne peut de toute
	/// façon pas être en vol ici : il aurait bloqué son appelant).
	~JobSystem() {
		if (m_workers.empty())
			return;
		{
			sdl3::MutexGuard guard(m_mutex.Value());
			m_stopping = true;
		}
		m_wakeUp.Value().Broadcast();
		for (sdl3::Thread &worker : m_workers)
			(void)worker.Wait();
		m_workers.clear();
	}

	[[nodiscard]] size_t WorkerCount() const noexcept { return m_workers.size(); }
	/// Vrai si le travail s'exécute sur le fil appelant (aucun fil secondaire).
	[[nodiscard]] bool IsInline() const noexcept { return m_workers.empty(); }
	/// Nombre maximal de fils ayant exécuté une tâche EN MÊME TEMPS depuis la
	/// création — ce que publie le rapport d'exécution de l'application.
	[[nodiscard]] int PeakConcurrentWorkers() const noexcept { return m_peakActive.Load(); }

	/// Exécute `body(index)` pour chaque `index` de [0, count), réparti sur le
	/// vivier ET sur le fil appelant. BLOQUANT : au retour, tous les appels
	/// sont terminés.
	///
	/// `minimumPerBatch` est le garde-fou qui évite de paralléliser ce qui
	/// n'en vaut pas la peine : en dessous, tout s'exécute en ligne. La
	/// synchronisation d'un lot coûte quelques microsecondes — répartir dix
	/// additions les perdrait.
	///
	/// `body` est appelé depuis PLUSIEURS fils : il ne doit écrire que dans
	/// des emplacements distincts par `index` (ou se synchroniser lui-même).
	void ParallelFor(size_t count, size_t minimumPerBatch, const std::function<void(size_t)> &body) {
		if (count == 0)
			return;
		if (m_workers.empty() || count < minimumPerBatch * 2) {
			for (size_t i = 0; i < count; ++i)
				body(i);
			return;
		}

		// Un lot par fil (vivier + appelant), borné par `minimumPerBatch`.
		size_t participants = m_workers.size() + 1;
		size_t batchSize = (count + participants - 1) / participants;
		if (batchSize < minimumPerBatch)
			batchSize = minimumPerBatch;
		size_t batchCount = (count + batchSize - 1) / batchSize;

		{
			sdl3::MutexGuard guard(m_mutex.Value());
			// Les lots 1..n-1 vont au vivier ; le lot 0 reste pour l'appelant,
			// qui serait sinon à ne rien faire pendant tout l'appel.
			for (size_t batch = 1; batch < batchCount; ++batch) {
				size_t begin = batch * batchSize;
				size_t end = begin + batchSize < count ? begin + batchSize : count;
				m_queue.push_back([&body, begin, end]() {
					for (size_t i = begin; i < end; ++i)
						body(i);
				});
			}
			m_pending += batchCount - 1;
		}
		m_wakeUp.Value().Broadcast();

		size_t firstEnd = batchSize < count ? batchSize : count;
		for (size_t i = 0; i < firstEnd; ++i)
			body(i);

		// Attente des lots confiés au vivier. `body` est capturé par
		// RÉFÉRENCE ci-dessus : c'est licite précisément parce qu'on ne sort
		// pas d'ici avant que toutes les tâches l'aient fini d'utiliser.
		sdl3::MutexGuard guard(m_mutex.Value());
		while (m_pending > 0)
			m_allDone.Value().Wait(m_mutex.Value());
	}

private:
	void WorkerLoop() {
		for (;;) {
			std::function<void()> job;
			{
				sdl3::MutexGuard guard(m_mutex.Value());
				while (m_queue.empty() && !m_stopping)
					m_wakeUp.Value().Wait(m_mutex.Value());
				if (m_queue.empty() && m_stopping)
					return;
				job = std::move(m_queue.front());
				m_queue.erase(m_queue.begin());
			}

			int active = m_active.FetchAdd(1) + 1;
			// Maximum simultané, par compare-échange : deux fils qui démarrent
			// ensemble ne doivent pas s'écraser mutuellement.
			for (;;) {
				int peak = m_peakActive.Load();
				if (active <= peak || m_peakActive.CompareExchange(peak, active))
					break;
			}
			job();
			m_active.FetchAdd(-1);

			{
				sdl3::MutexGuard guard(m_mutex.Value());
				if (m_pending > 0)
					--m_pending;
			}
			m_allDone.Value().Broadcast();
		}
	}

	Option<sdl3::Mutex> m_mutex = NONE;
	Option<sdl3::Condition> m_wakeUp = NONE;
	Option<sdl3::Condition> m_allDone = NONE;
	std::vector<sdl3::Thread> m_workers;
	std::vector<std::function<void()>> m_queue;
	size_t m_pending = 0;
	bool m_stopping = false;
	sdl3::AtomicInt m_active;
	sdl3::AtomicInt m_peakActive;
};

} // namespace jobs
