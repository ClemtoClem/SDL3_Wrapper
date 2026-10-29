// Définitions de resources/resource.hpp
#include "resources/resource.hpp"

namespace resources {

// ── Resource ─────────────────────────────────────────────────────────────────

Result<bool, String> Resource::Load() {
	// UN seul chargement à la fois : un fil de l'AsyncLoader et un appel sur
	// place (ResourceCache::LoadNow) peuvent viser la même ressource. Seul
	// celui qui fait passer l'état de Unloaded/Failed à Loading charge ; les
	// autres voient « déjà prête » (Ok(true)) ou « en cours ailleurs »
	// (Ok(false)) — jamais un second DoLoad concurrent qui repasserait une
	// ressource prête à l'état Loading.
	ResourceState expected = state.load(std::memory_order_acquire);
	do {
		if (expected == ResourceState::Ready)
			return Ok(true);
		if (expected == ResourceState::Loading)
			return Ok(false);
	} while (!state.compare_exchange_weak(expected, ResourceState::Loading, std::memory_order_acq_rel));
	auto result = DoLoad();
	m_error = result.IsOk() ? String() : result.Error();
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
