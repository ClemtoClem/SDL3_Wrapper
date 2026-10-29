#pragma once
#include "pool.hpp"
#include "resource.hpp"
#include <algorithm>
#include <chrono>
#include <concepts>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <span>
#include <thread> // std::thread::hardware_concurrency() uniquement (pas std::jthread/mutex/condition_variable)
#include <unordered_map>
#include <vector>

#include <core/core.hpp>
#include "asset.hpp"
#include "sdl3/stdinc.hpp"
#include "sdl3/thread.hpp"

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

	explicit AsyncLoader(size_t threadCount = 0);

	/// SDL_Thread n'a pas d'équivalent `std::jthread`+`stop_token` (réveil
	/// automatique d'un `wait()` bloqué à la destruction) — le flag d'arrêt
	/// est posé MANUELLEMENT, suivi d'un `broadcast()` explicite pour réveiller
	/// tout worker en attente AVANT de les joindre. C'est le correctif du bug
	/// latent de la version `std::jthread`/`condition_variable` PLAINE
	/// d'origine : celle-ci attendait sur un prédicat `stop_requested()` sans
	/// jamais être notifiée de l'arrêt (seule `condition_variable_any` a
	/// cette intégration), pouvant bloquer indéfiniment un worker inactif à
	/// la fermeture — corrigé ici en migrant, pas simplement porté tel quel.
	~AsyncLoader();

	AsyncLoader(const AsyncLoader &) = delete;
	AsyncLoader &operator=(const AsyncLoader &) = delete;

	[[nodiscard]] std::future<bool> Enqueue(std::shared_ptr<Resource> res, Priority prio = Priority::NORMAL);

	void EnqueueAll(std::span<std::shared_ptr<Resource>> resources, Priority prio = Priority::NORMAL);

	[[nodiscard]] size_t PendingCount() const noexcept { return m_pending.load(std::memory_order_relaxed); }
	[[nodiscard]] bool IsIdle() const noexcept { return PendingCount() == 0; }

	bool WaitIdle(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

	static bool LoadSync(Resource &res) { return res.Load().IsOk(); }

private:
	void WorkerLoop();

	sdl3::Mutex m_mutex;
	sdl3::Condition m_cv;
	sdl3::AtomicInt m_stop{0};
	std::queue<LoadRequest> m_high, m_normal;
	std::atomic<size_t> m_pending{0}; // pas d'équivalent SDL pour un compteur size_t
	std::vector<sdl3::Thread> m_workers;
};

/// Où se fait un chargement demandé par identifiant.
enum class LoadPolicy : uint8_t {
	ASYNC, ///< en arrière-plan, sur l'AsyncLoader (par défaut)
	SYNC,  ///< sur place, avant de rendre la main
};

// ============================================================================
// ResourceCache — combines Registry + AsyncLoader.
//
// Deux façons d'y désigner une ressource :
//  - par CHEMIN (LoadAsync/LoadSync/Get<T>(clé)) : le cache historique ;
//  - par IDENTIFIANT STABLE (AssetId / AssetRef<T>, cf. asset.hpp) : c'est
//    ce qu'écrivent les composants ECS et les fichiers. Le cache tient alors
//    un CATALOGUE (id → chemin ou clé + paramètres) et, pour chaque nature de
//    ressource, une FABRIQUE enregistrée par le module qui la définit
//    (render3d : maillages, textures, matériaux, shaders ; audio : sons).
//    `Get(ref)` rend la ressource en mémoire, ou la recrée depuis le
//    catalogue et lance son chargement (asynchrone par défaut) ; une
//    référence inconnue ou un chargement en échec est RECENSÉ
//    (`Missing()`) au lieu de casser l'appelant.
// ============================================================================
class ResourceCache {
public:
	explicit ResourceCache(String basePath = String(), size_t threadCount = 0)
		: m_base(std::move(basePath)), m_loader(threadCount) {}

	ResourceCache(const ResourceCache &) = delete;
	ResourceCache &operator=(const ResourceCache &) = delete;

	[[nodiscard]] const String &BasePath() const noexcept { return m_base; }

	[[nodiscard]] String Resolve(StringView rel) const;

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

	// --- Identifiants stables (AssetId) ---

	/// Fabrique d'une nature de ressource : crée la ressource (NON chargée)
	/// décrite par une entrée du catalogue. Enregistrée par le module qui
	/// définit le type (cf. render3d::RegisterResourceFactories).
	using Factory = std::function<std::shared_ptr<Resource>(const AssetRecord &record, const ResourceCache &cache)>;
	void RegisterFactory(AssetType type, Factory factory);
	[[nodiscard]] bool HasFactory(AssetType type) const;

	/// Catalogue `record` et garde `resource` en mémoire sous son id (posé sur
	/// la ressource), puis la charge selon `policy` si elle ne l'est pas.
	/// Rend l'id. Une ressource déjà connue sous cet id est REMPLACÉE.
	AssetId Add(std::shared_ptr<Resource> resource, AssetRecord record, LoadPolicy policy = LoadPolicy::ASYNC);

	/// Catalogue seulement (la ressource sera créée par sa fabrique au
	/// premier Get). Rend l'id.
	AssetId Declare(AssetRecord record);

	/// La ressource, dans l'état où elle est (nullptr si l'id est inconnu ou
	/// d'une autre nature) ; recréée et mise en chargement si elle n'est plus
	/// en mémoire.
	template <std::derived_from<Resource> T>
	[[nodiscard]] std::shared_ptr<T> Get(AssetRef<T> ref, StringView context = StringView()) {
		return std::dynamic_pointer_cast<T>(Acquire(ref.id, AssetRef<T>::Type(), context));
	}
	/// La ressource si elle est PRÊTE, sinon nullptr — ce qu'un système
	/// utilise à chaque image (en chargement : rien à signaler ; en échec :
	/// recensée dans Missing()).
	template <std::derived_from<Resource> T>
	[[nodiscard]] std::shared_ptr<T> GetReady(AssetRef<T> ref, StringView context = StringView()) {
		std::shared_ptr<T> resource = Get(ref, context);
		return resource && resource->IsReady() ? resource : nullptr;
	}
	/// Sans type statique (éditeur, sérialiseurs).
	[[nodiscard]] std::shared_ptr<Resource> Find(AssetId id, StringView context = StringView()) {
		return Acquire(id, AssetType::UNKNOWN, context);
	}

	/// Charge sur place une ressource cataloguée ; vrai si elle est prête.
	bool LoadNow(AssetId id);

	[[nodiscard]] bool Knows(AssetId id) const;
	[[nodiscard]] bool IsResident(AssetId id) const;
	[[nodiscard]] Option<AssetRecord> Record(AssetId id) const;
	[[nodiscard]] Option<AssetRecord> RecordByPath(StringView path) const;
	[[nodiscard]] Option<AssetRecord> RecordByKey(StringView key) const;
	[[nodiscard]] AssetCatalog CatalogSnapshot() const;

	/// Modifie l'entrée de catalogue d'une ressource (nom, paramètres) ; la
	/// ressource en mémoire n'est pas touchée.
	bool UpdateRecord(const AssetRecord &record);

	/// Références non résolues depuis le dernier ClearMissing (une par id) ;
	/// les ressources en mémoire passées en échec y sont ajoutées.
	[[nodiscard]] std::vector<MissingAsset> Missing();
	void ClearMissing();

	/// Ressources désignées par id actuellement en mémoire (d'une nature, ou
	/// toutes avec UNKNOWN).
	[[nodiscard]] size_t ResidentCount(AssetType type = AssetType::UNKNOWN) const;
	/// Libère les ressources par id que plus personne ne tient (le catalogue
	/// garde leur trace : elles se rechargeront). Rend leur nombre.
	size_t UnloadUnused();

	[[nodiscard]] dataio::NodePtr CatalogToJson() const;
	/// Remplace le catalogue (les ressources par id en mémoire sont oubliées).
	[[nodiscard]] Option<String> LoadCatalog(const dataio::NodePtr &json);

	[[nodiscard]] Registry &GetRegistry() noexcept { return m_registry; }
	[[nodiscard]] const Registry &GetRegistry() const noexcept { return m_registry; }
	[[nodiscard]] AsyncLoader &GetLoader() noexcept { return m_loader; }

private:
	[[nodiscard]] std::shared_ptr<Resource> Acquire(AssetId id, AssetType type, StringView context);
	void Start(const std::shared_ptr<Resource> &resource, LoadPolicy policy);
	void ReportLocked(AssetId id, AssetType type, StringView context, String reason);

	String m_base;
	Registry m_registry;
	AsyncLoader m_loader;

	mutable std::mutex m_idMutex; ///< tout ce qui suit (les chargements finissent sur d'autres fils)
	AssetCatalog m_catalog;
	std::unordered_map<AssetType, Factory> m_factories;
	std::unordered_map<AssetId, std::shared_ptr<Resource>> m_resident;
	std::vector<MissingAsset> m_missing;
};

/// Poignée NON propriétaire vers un ResourceCache, à ranger dans les
/// ressources d'un monde ECS (`registry.resources.Insert(CacheRef{&cache})`) :
/// les systèmes y résolvent les AssetRef de leurs composants. Le cache
/// lui-même n'est ni copiable ni déplaçable (fils de chargement).
struct CacheRef {
	ResourceCache *cache = nullptr;
};

} // namespace resources
