// Définitions de jobs/job_system.hpp
#include "jobs/job_system.hpp"

namespace jobs {

// ── JobSystem ────────────────────────────────────────────────────────────────

size_t JobSystem::RecommendedWorkerCount() noexcept {
	unsigned cores = std::thread::hardware_concurrency();
	return cores > 1 ? size_t(cores - 1) : 0;
}

JobSystem::JobSystem(size_t workerCount) {
	m_workers.reserve(workerCount);
	for (size_t i = 0; i < workerCount; ++i)
		m_workers.emplace_back([this] { WorkerLoop(); });
}

JobSystem::~JobSystem() {
	if (m_workers.empty())
		return;
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_stopping = true;
	}
	m_wakeUp.notify_all();
	for (std::thread &worker : m_workers)
		worker.join();
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
		std::lock_guard<std::mutex> guard(m_mutex);
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
	m_wakeUp.notify_all();

	size_t firstEnd = batchSize < count ? batchSize : count;
	for (size_t i = 0; i < firstEnd; ++i)
		body(i);

	// Attente des lots confiés au vivier. `body` est capturé par
	// RÉFÉRENCE ci-dessus : c'est licite précisément parce qu'on ne sort
	// pas d'ici avant que toutes les tâches l'aient fini d'utiliser.
	std::unique_lock<std::mutex> lock(m_mutex);
	m_allDone.wait(lock, [this] { return m_pending == 0; });
}

void JobSystem::WorkerLoop() {
	for (;;) {
		std::function<void()> job;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_wakeUp.wait(lock, [this] { return !m_queue.empty() || m_stopping; });
			if (m_queue.empty())
				return; // arrêt demandé et file vide
			job = std::move(m_queue.front());
			m_queue.pop_front();
		}

		int active = m_active.fetch_add(1) + 1;
		// Maximum simultané, par compare-échange : deux fils qui démarrent
		// ensemble ne doivent pas s'écraser mutuellement.
		int peak = m_peakActive.load();
		while (active > peak && !m_peakActive.compare_exchange_weak(peak, active)) {
		}
		job();
		m_active.fetch_sub(1);

		{
			std::lock_guard<std::mutex> guard(m_mutex);
			if (m_pending > 0)
				--m_pending;
		}
		m_allDone.notify_all();
	}
}

} // namespace jobs
