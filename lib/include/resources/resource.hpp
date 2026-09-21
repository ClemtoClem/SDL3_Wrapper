#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "../core/core.hpp"

namespace resources {

enum class ResourceState : uint8_t { Unloaded, Loading, Ready, Failed };

// ============================================================================
// Resource — abstract base for any loadable asset (C++20, RAII).
// ============================================================================
class Resource {
public:
	using ReadyCallback = std::function<void(Resource &)>;

	virtual ~Resource() = default;

	Resource(const Resource &) = delete;
	Resource &operator=(const Resource &) = delete;

	// --- State ---
	[[nodiscard]] ResourceState GetState() const noexcept { return state.load(std::memory_order_acquire); }
	[[nodiscard]] bool IsReady() const noexcept { return GetState() == ResourceState::Ready; }
	[[nodiscard]] bool HasFailed() const noexcept { return GetState() == ResourceState::Failed; }
	[[nodiscard]] bool IsLoading() const noexcept { return GetState() == ResourceState::Loading; }
	[[nodiscard]] bool IsUnloaded() const noexcept { return GetState() == ResourceState::Unloaded; }

	// --- Identity ---
	[[nodiscard]] virtual StringView Path() const noexcept = 0;

	// --- Load / Unload ---
	/// `DoLoad()` peut s'exécuter sur un thread de fond (cf. AsyncLoader,
	/// cache.hpp) — l'erreur est une `String` POSSÉDÉE (pas `StringView`) :
	/// si `DoLoad()` s'appuie sur `sdl3::GetError()` (buffer thread-local),
	/// il faut la copier AVANT de retourner, sinon un appel SDL ultérieur sur
	/// CE MÊME thread pourrait écraser le message avant que l'appelant (sur
	/// un autre thread) ne le lise.
	Result<bool, String> Load() {
		state.store(ResourceState::Loading, std::memory_order_release);
		auto result = DoLoad();
		state.store(result.IsOk() ? ResourceState::Ready : ResourceState::Failed, std::memory_order_release);
		if (result.IsOk())
			FireCallbacks();
		return result;
	}

	void Unload() {
		DoUnload();
		state.store(ResourceState::Unloaded, std::memory_order_release);
	}

	// --- Callbacks ---
	void OnReady(ReadyCallback cb) {
		if (IsReady()) {
			cb(*this);
			return;
		}
		std::scoped_lock lock(cbMutex);
		callbacks.push_back(std::move(cb));
	}

protected:
	Resource() = default;

	virtual Result<bool, String> DoLoad() = 0;
	virtual void DoUnload() = 0;

	void MarkReadyInternal() {
		state.store(ResourceState::Ready, std::memory_order_release);
		FireCallbacks();
	}

private:
	void FireCallbacks() {
		std::vector<ReadyCallback> cbs;
		{
			std::scoped_lock lock(cbMutex);
			cbs.swap(callbacks);
		}
		for (auto &cb : cbs)
			cb(*this);
	}

	std::atomic<ResourceState> state{ResourceState::Unloaded};
	std::mutex cbMutex;
	std::vector<ReadyCallback> callbacks;
};

} // namespace resources
