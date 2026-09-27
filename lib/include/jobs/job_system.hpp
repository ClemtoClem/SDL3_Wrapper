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
	[[nodiscard]] static size_t RecommendedWorkerCount() noexcept;

	/// `workerCount` fils secondaires. 0 = tout s'exécute en ligne sur
	/// l'appelant (cf. en-tête).
	explicit JobSystem(size_t workerCount = RecommendedWorkerCount());

	JobSystem(const JobSystem &) = delete;
	JobSystem &operator=(const JobSystem &) = delete;

	/// Arrête et joint tous les fils. Les tâches encore en file sont
	/// TERMINÉES avant l'arrêt (un `ParallelFor` bloquant ne peut de toute
	/// façon pas être en vol ici : il aurait bloqué son appelant).
	~JobSystem();

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
	void ParallelFor(size_t count, size_t minimumPerBatch, const std::function<void(size_t)> &body);

private:
	void WorkerLoop();

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
