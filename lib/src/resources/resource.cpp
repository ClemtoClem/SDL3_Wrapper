// Définitions de resources/resource.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "resources/resource.hpp"

namespace resources {

// ── Resource ─────────────────────────────────────────────────────────────────

Result<bool, String> Resource::Load() {
	state.store(ResourceState::Loading, std::memory_order_release);
	auto result = DoLoad();
	state.store(result.IsOk() ? ResourceState::Ready : ResourceState::Failed, std::memory_order_release);
	if (result.IsOk())
		FireCallbacks();
	return result;
}

void Resource::Unload() {
	DoUnload();
	state.store(ResourceState::Unloaded, std::memory_order_release);
}

void Resource::OnReady(ReadyCallback cb) {
	if (IsReady()) {
		cb(*this);
		return;
	}
	std::scoped_lock lock(cbMutex);
	callbacks.push_back(std::move(cb));
}

void Resource::MarkReadyInternal() {
	state.store(ResourceState::Ready, std::memory_order_release);
	FireCallbacks();
}

void Resource::FireCallbacks() {
	std::vector<ReadyCallback> cbs;
	{
		std::scoped_lock lock(cbMutex);
		cbs.swap(callbacks);
	}
	for (auto &cb : cbs)
		cb(*this);
}

} // namespace resources
