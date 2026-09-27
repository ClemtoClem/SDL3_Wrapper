// Définitions de jobs/job_system.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "jobs/job_system.hpp"

namespace jobs {

// ── JobSystem ────────────────────────────────────────────────────────────────

size_t JobSystem::RecommendedWorkerCount() noexcept {
	int cores = sdl3::system::CpuCount();
	return cores > 1 ? size_t(cores - 1) : 0;
}

JobSystem::JobSystem(size_t workerCount) {
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

JobSystem::~JobSystem() {
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

void JobSystem::ParallelFor(size_t count, size_t minimumPerBatch, const std::function<void(size_t)> &body) {
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

void JobSystem::WorkerLoop() {
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

} // namespace jobs
