#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <core/core.hpp>
#include "asset.hpp"

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
	/// Identifiant STABLE (cf. asset.hpp) : ce que les composants et les
	/// fichiers écrivent pour désigner cette ressource. Invalide pour une
	/// ressource qui n'est pas cataloguée (chargée seulement par chemin).
	[[nodiscard]] const AssetId &Id() const noexcept { return m_id; }
	/// Nature de la ressource (maillage, texture…), UNKNOWN si non précisée.
	[[nodiscard]] AssetType Type() const noexcept { return m_type; }
	/// Nom affiché (le chemin à défaut).
	[[nodiscard]] String DisplayName() const { return m_name.IsEmpty() ? String(Path()) : m_name; }
	/// Message du dernier échec de Load() (vide sinon). Lisible dès que
	/// HasFailed() est vrai, depuis n'importe quel fil.
	[[nodiscard]] const String &LastError() const noexcept { return m_error; }

	// --- Load / Unload ---
	/// `DoLoad()` peut s'exécuter sur un thread de fond (cf. AsyncLoader,
	/// cache.hpp) — l'erreur est une `String` POSSÉDÉE (pas `StringView`) :
	/// si `DoLoad()` s'appuie sur `sdl3::GetError()` (buffer thread-local),
	/// il faut la copier AVANT de retourner, sinon un appel SDL ultérieur sur
	/// CE MÊME thread pourrait écraser le message avant que l'appelant (sur
	/// un autre thread) ne le lise.
	/// Charge (DoLoad) si la ressource est Unloaded ou Failed. Déjà prête :
	/// Ok(true) sans rien refaire ; en cours sur un autre fil : Ok(false)
	/// (attendre IsLoading()/OnReady) — un seul DoLoad à la fois.
	Result<bool, String> Load();

	void Unload();

	// --- Callbacks ---
	void OnReady(ReadyCallback cb);

protected:
	Resource() = default;
	Resource(AssetId id, AssetType type, String name = String())
		: m_id(id), m_type(type), m_name(std::move(name)) {}
	/// Identité posée après construction (ressources créées puis cataloguées).
	void SetIdentity(AssetId id, AssetType type, String name = String()) {
		m_id = id;
		m_type = type;
		m_name = std::move(name);
	}
	friend class ResourceCache;

	virtual Result<bool, String> DoLoad() = 0;
	virtual void DoUnload() = 0;

	void MarkReadyInternal();

private:
	void FireCallbacks();

	AssetId m_id;
	AssetType m_type = AssetType::UNKNOWN;
	String m_name;
	String m_error; ///< écrit AVANT la publication de l'état (release) : lisible après HasFailed()
	std::atomic<ResourceState> state{ResourceState::Unloaded};
	std::mutex cbMutex;
	std::vector<ReadyCallback> callbacks;
};

} // namespace resources
