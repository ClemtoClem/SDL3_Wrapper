#pragma once
#include "pool.hpp"
#include "resource.hpp"
#include <algorithm>
#include <chrono>
#include <concepts>
#include <future>
#include <memory>
#include <queue>
#include <span>
#include <thread> // std::thread::hardware_concurrency() uniquement (pas std::jthread/mutex/condition_variable)
#include <vector>

#include "../core/core.hpp"
#include "../sdl3/stdinc.hpp"
#include "../sdl3/thread.hpp"

namespace resources {

struct LoadRequest {
	std::shared_ptr<Resource> resource;
	std::promise<bool> promise;
};

// ============================================================================
// AsyncLoader — thread pool for background resource loading, sur sdl3::Thread
// (cf. lib/sdl3/thread.hpp, Phase 1 du même chantier) plutôt que std::jthread.
// ============================================================================
class AsyncLoader {
public:
	enum class Priority : uint8_t { NORMAL = 0, HIGH = 1 };

	explicit AsyncLoader(size_t threadCount = 0)
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

	/// SDL_Thread n'a pas d'équivalent `std::jthread`+`stop_token` (réveil
	/// automatique d'un `wait()` bloqué à la destruction) — le flag d'arrêt
	/// est posé MANUELLEMENT, suivi d'un `broadcast()` explicite pour réveiller
	/// tout worker en attente AVANT de les joindre. C'est le correctif du bug
	/// latent de la version `std::jthread`/`condition_variable` PLAINE
	/// d'origine : celle-ci attendait sur un prédicat `stop_requested()` sans
	/// jamais être notifiée de l'arrêt (seule `condition_variable_any` a
	/// cette intégration), pouvant bloquer indéfiniment un worker inactif à
	/// la fermeture — corrigé ici en migrant, pas simplement porté tel quel.
	~AsyncLoader() {
		m_stop.Store(1);
		m_cv.Broadcast();
		for (auto &w : m_workers)
			w.Wait();
	}

	AsyncLoader(const AsyncLoader &) = delete;
	AsyncLoader &operator=(const AsyncLoader &) = delete;

	[[nodiscard]] std::future<bool> Enqueue(std::shared_ptr<Resource> res, Priority prio = Priority::NORMAL) {
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

	void EnqueueAll(std::span<std::shared_ptr<Resource>> resources, Priority prio = Priority::NORMAL) {
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

	[[nodiscard]] size_t PendingCount() const noexcept { return m_pending.load(std::memory_order_relaxed); }
	[[nodiscard]] bool IsIdle() const noexcept { return PendingCount() == 0; }

	bool WaitIdle(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
		auto deadline = std::chrono::steady_clock::now() + timeout;
		while (!IsIdle()) {
			if (std::chrono::steady_clock::now() >= deadline)
				return false;
			SDL_Delay(1);
		}
		return true;
	}

	static bool LoadSync(Resource &res) { return res.Load().IsOk(); }

private:
	void WorkerLoop() {
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

	sdl3::Mutex m_mutex;
	sdl3::Condition m_cv;
	sdl3::AtomicInt m_stop{0};
	std::queue<LoadRequest> m_high, m_normal;
	std::atomic<size_t> m_pending{0}; // pas d'équivalent SDL pour un compteur size_t
	std::vector<sdl3::Thread> m_workers;
};

// ============================================================================
// ResourceCache — combines Registry + AsyncLoader.
// ============================================================================
class ResourceCache {
public:
	explicit ResourceCache(String basePath = String(), size_t threadCount = 0)
		: m_base(std::move(basePath)), m_loader(threadCount) {}

	ResourceCache(const ResourceCache &) = delete;
	ResourceCache &operator=(const ResourceCache &) = delete;

	[[nodiscard]] const String &BasePath() const noexcept { return m_base; }

	[[nodiscard]] String Resolve(StringView rel) const {
		if (rel.IsEmpty())
			return String();
		if (!m_base.IsEmpty() && rel[0] != '/' && rel[0] != '.')
			return m_base + String(rel);
		return String(rel);
	}

	// --- Async load ---
	template <std::derived_from<Resource> T>
	[[nodiscard]] std::future<bool> LoadAsync(std::shared_ptr<T> res, AsyncLoader::Priority prio = AsyncLoader::Priority::NORMAL) {
		String key = String(res->Path());
		m_registry.Insert(key, res);
		return m_loader.Enqueue(std::move(res), prio);
	}

	template <std::derived_from<Resource> T, typename... Args> [[nodiscard]] std::future<bool> EmplaceAsync(Args &&...args) {
		return LoadAsync(std::make_shared<T>(std::forward<Args>(args)...));
	}

	// --- Sync load ---
	template <std::derived_from<Resource> T> [[nodiscard]] std::shared_ptr<T> LoadSync(std::shared_ptr<T> res) {
		String key = String(res->Path());
		if (auto existing = m_registry.Get<T>(key); existing.IsSome() && existing.Unwrap()->IsReady())
			return existing.Unwrap();
		m_registry.Insert(key, res);
		res->Load();
		return res;
	}

	template <std::derived_from<Resource> T, typename... Args> [[nodiscard]] std::shared_ptr<T> EmplaceSync(Args &&...args) {
		return LoadSync(std::make_shared<T>(std::forward<Args>(args)...));
	}

	// --- Registry ---
	template <std::derived_from<Resource> T> [[nodiscard]] Option<std::shared_ptr<T>> Get(const String &key) {
		return m_registry.Get<T>(key);
	}
	template <std::derived_from<Resource> T> [[nodiscard]] bool Contains(const String &key) { return m_registry.Contains<T>(key); }
	template <std::derived_from<Resource> T> bool Remove(const String &key) { return m_registry.Remove<T>(key); }
	template <std::derived_from<Resource> T> void Clear() { m_registry.Clear<T>(); }
	void ClearAll() { m_registry.ClearAll(); }

	// --- Loader ---
	[[nodiscard]] size_t PendingCount() const noexcept { return m_loader.PendingCount(); }
	[[nodiscard]] bool IsIdle() const noexcept { return m_loader.IsIdle(); }
	bool WaitIdle(std::chrono::milliseconds t = std::chrono::milliseconds(5000)) { return m_loader.WaitIdle(t); }

	[[nodiscard]] Registry &GetRegistry() noexcept { return m_registry; }
	[[nodiscard]] const Registry &GetRegistry() const noexcept { return m_registry; }
	[[nodiscard]] AsyncLoader &GetLoader() noexcept { return m_loader; }

private:
	String m_base;
	Registry m_registry;
	AsyncLoader m_loader;
};

} // namespace resources
