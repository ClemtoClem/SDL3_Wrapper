// Définitions de resources/cache.hpp
#include "resources/cache.hpp"

namespace resources {

// ── AsyncLoader ──────────────────────────────────────────────────────────────

AsyncLoader::AsyncLoader(size_t threadCount)
	: m_mutex(sdl3::Mutex::Create().Unwrap()), m_cv(sdl3::Condition::Create().Unwrap()) {
	const size_t N = threadCount > 0 ? threadCount : sdl3::Max(size_t(1), size_t(std::thread::hardware_concurrency()));
	m_workers.reserve(N);
	for (size_t i = 0; i < N; ++i) {
		auto tRes = sdl3::Thread::Create([this]() -> int {
			WorkerLoop();
			return 0;
		});
		m_workers.push_back(std::move(tRes).Unwrap());
	}
}

AsyncLoader::~AsyncLoader() {
	m_stop.Store(1);
	m_cv.Broadcast();
	for (auto &w : m_workers)
		w.Wait();
}

std::future<bool> AsyncLoader::Enqueue(std::shared_ptr<Resource> res, Priority prio) {
	if (!res) {
		std::promise<bool> p;
		p.set_value(false);
		return p.get_future();
	}
	LoadRequest req{std::move(res), {}};
	auto future = req.promise.get_future();
	{
		sdl3::MutexGuard lock(m_mutex);
		(prio == Priority::HIGH ? m_high : m_normal).push(std::move(req));
		++m_pending;
	}
	m_cv.Signal();
	return future;
}

void AsyncLoader::EnqueueAll(std::span<std::shared_ptr<Resource>> resources, Priority prio) {
	if (resources.empty())
		return;
	{
		sdl3::MutexGuard lock(m_mutex);
		for (auto &res : resources) {
			if (!res)
				continue;
			LoadRequest r{res, {}};
			(prio == Priority::HIGH ? m_high : m_normal).push(std::move(r));
			++m_pending;
		}
	}
	m_cv.Broadcast();
}

bool AsyncLoader::WaitIdle(std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (!IsIdle()) {
		if (std::chrono::steady_clock::now() >= deadline)
			return false;
		SDL_Delay(1);
	}
	return true;
}

void AsyncLoader::WorkerLoop() {
	while (true) {
		LoadRequest req;
		{
			sdl3::MutexGuard lock(m_mutex);
			while (m_stop.Load() == 0 && m_high.empty() && m_normal.empty())
				m_cv.Wait(m_mutex);
			if (m_stop.Load() != 0 && m_high.empty() && m_normal.empty())
				return;
			if (!m_high.empty()) {
				req = std::move(m_high.front());
				m_high.pop();
			} else {
				req = std::move(m_normal.front());
				m_normal.pop();
			}
		}
		bool ok = false;
		try {
			ok = req.resource->Load().IsOk();
		} catch (...) {
		}
		req.promise.set_value(ok);
		--m_pending;
	}
}

// ── ResourceCache ────────────────────────────────────────────────────────────

String ResourceCache::Resolve(StringView rel) const {
	if (rel.IsEmpty())
		return String();
	if (!m_base.IsEmpty() && rel[0] != '/' && rel[0] != '.')
		return m_base + String(rel);
	return String(rel);
}

} // namespace resources
