// Définitions de resources/cache.hpp
#include "resources/cache.hpp"
#include "sdl3/time.hpp"

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

// ── ResourceCache : identifiants stables ─────────────────────────────────────

void ResourceCache::RegisterFactory(AssetType type, Factory factory) {
	std::scoped_lock lock(m_idMutex);
	m_factories[type] = std::move(factory);
}

bool ResourceCache::HasFactory(AssetType type) const {
	std::scoped_lock lock(m_idMutex);
	return m_factories.count(type) != 0;
}

AssetId ResourceCache::Add(std::shared_ptr<Resource> resource, AssetRecord record, LoadPolicy policy) {
	if (!record.id.IsValid())
		record.id = AssetId::Generate();
	const AssetId id = record.id;
	if (resource) {
		resource->SetIdentity(id, record.type, record.name);
		std::scoped_lock lock(m_idMutex);
		(void)m_catalog.Put(std::move(record));
		m_resident[id] = resource;
	} else {
		std::scoped_lock lock(m_idMutex);
		(void)m_catalog.Put(std::move(record));
	}
	if (resource)
		Start(resource, policy);
	return id;
}

AssetId ResourceCache::Declare(AssetRecord record) {
	if (!record.id.IsValid())
		record.id = AssetId::Generate();
	const AssetId id = record.id;
	std::scoped_lock lock(m_idMutex);
	(void)m_catalog.Put(std::move(record));
	return id;
}

void ResourceCache::ReportLocked(AssetId id, AssetType type, StringView context, String reason) {
	for (const MissingAsset &missing : m_missing)
		if (missing.id == id)
			return;
	m_missing.push_back(MissingAsset{id, type, String(context), std::move(reason)});
}

std::shared_ptr<Resource> ResourceCache::Acquire(AssetId id, AssetType type, StringView context) {
	if (!id.IsValid())
		return nullptr;
	AssetRecord record;
	Factory factory;
	{
		std::scoped_lock lock(m_idMutex);
		if (auto it = m_resident.find(id); it != m_resident.end()) {
			const std::shared_ptr<Resource> &resident = it->second;
			if (type != AssetType::UNKNOWN && resident->Type() != type) {
				ReportLocked(id, type, context,
							 String::Format("ressource de nature « %s », « %s » attendue", AssetTypeName(resident->Type()),
											AssetTypeName(type)));
				return nullptr;
			}
			if (resident->HasFailed())
				ReportLocked(id, resident->Type(), context, resident->LastError());
			return resident;
		}
		const AssetRecord *found = m_catalog.Find(id);
		if (!found) {
			ReportLocked(id, type, context, String("référence inconnue du catalogue"));
			return nullptr;
		}
		if (type != AssetType::UNKNOWN && found->type != type) {
			ReportLocked(id, type, context,
						 String::Format("ressource de nature « %s », « %s » attendue", AssetTypeName(found->type),
										AssetTypeName(type)));
			return nullptr;
		}
		auto factoryIt = m_factories.find(found->type);
		if (factoryIt == m_factories.end()) {
			ReportLocked(id, found->type, context,
						 String::Format("aucune fabrique pour les ressources « %s »", AssetTypeName(found->type)));
			return nullptr;
		}
		record = *found;
		factory = factoryIt->second;
	}
	// La fabrique tourne hors du verrou (elle peut lire le cache).
	std::shared_ptr<Resource> created = factory(record, *this);
	std::scoped_lock lock(m_idMutex);
	if (!created) {
		ReportLocked(id, record.type, context, String("la fabrique n'a rien créé"));
		return nullptr;
	}
	if (auto it = m_resident.find(id); it != m_resident.end())
		return it->second; // créée entre-temps par un autre appel
	created->SetIdentity(id, record.type, record.name);
	m_resident[id] = created;
	if (created->IsUnloaded())
		(void)m_loader.Enqueue(created);
	return created;
}

void ResourceCache::Start(const std::shared_ptr<Resource> &resource, LoadPolicy policy) {
	if (!resource || !resource->IsUnloaded())
		return;
	if (policy == LoadPolicy::SYNC)
		(void)resource->Load();
	else
		(void)m_loader.Enqueue(resource);
}

bool ResourceCache::LoadNow(AssetId id) {
	std::shared_ptr<Resource> resource = Find(id);
	if (!resource)
		return false;
	if (resource->IsUnloaded())
		(void)resource->Load();
	// En cours sur un fil de chargement : l'attendre (borné).
	for (int i = 0; i < 2000 && resource->IsLoading(); ++i)
		sdl3::DelayMS(1);
	return resource->IsReady();
}

bool ResourceCache::Knows(AssetId id) const {
	std::scoped_lock lock(m_idMutex);
	return m_catalog.Find(id) != nullptr;
}

bool ResourceCache::IsResident(AssetId id) const {
	std::scoped_lock lock(m_idMutex);
	return m_resident.count(id) != 0;
}

Option<AssetRecord> ResourceCache::Record(AssetId id) const {
	std::scoped_lock lock(m_idMutex);
	const AssetRecord *record = m_catalog.Find(id);
	return record ? Option<AssetRecord>(Some(*record)) : Option<AssetRecord>(NONE);
}

Option<AssetRecord> ResourceCache::RecordByPath(StringView path) const {
	std::scoped_lock lock(m_idMutex);
	const AssetRecord *record = m_catalog.FindByPath(path);
	return record ? Option<AssetRecord>(Some(*record)) : Option<AssetRecord>(NONE);
}

Option<AssetRecord> ResourceCache::RecordByKey(StringView key) const {
	std::scoped_lock lock(m_idMutex);
	const AssetRecord *record = m_catalog.FindByKey(key);
	return record ? Option<AssetRecord>(Some(*record)) : Option<AssetRecord>(NONE);
}

AssetCatalog ResourceCache::CatalogSnapshot() const {
	std::scoped_lock lock(m_idMutex);
	return m_catalog;
}

bool ResourceCache::UpdateRecord(const AssetRecord &record) {
	std::scoped_lock lock(m_idMutex);
	if (!m_catalog.Find(record.id))
		return false;
	(void)m_catalog.Put(record);
	return true;
}

std::vector<MissingAsset> ResourceCache::Missing() {
	std::scoped_lock lock(m_idMutex);
	for (const auto &[id, resource] : m_resident)
		if (resource->HasFailed())
			ReportLocked(id, resource->Type(), StringView(), resource->LastError());
	return m_missing;
}

void ResourceCache::ClearMissing() {
	std::scoped_lock lock(m_idMutex);
	m_missing.clear();
}

size_t ResourceCache::ResidentCount(AssetType type) const {
	std::scoped_lock lock(m_idMutex);
	if (type == AssetType::UNKNOWN)
		return m_resident.size();
	size_t count = 0;
	for (const auto &[id, resource] : m_resident)
		count += resource->Type() == type ? 1 : 0;
	return count;
}

size_t ResourceCache::UnloadUnused() {
	std::scoped_lock lock(m_idMutex);
	size_t freed = 0;
	for (auto it = m_resident.begin(); it != m_resident.end();) {
		// Tenue seulement par le cache, et pas en plein chargement (un fil de
		// l'AsyncLoader la tiendrait alors lui aussi).
		if (it->second.use_count() == 1 && !it->second->IsLoading()) {
			it = m_resident.erase(it);
			++freed;
		} else {
			++it;
		}
	}
	return freed;
}

dataio::NodePtr ResourceCache::CatalogToJson() const {
	std::scoped_lock lock(m_idMutex);
	return m_catalog.ToJson();
}

Option<String> ResourceCache::LoadCatalog(const dataio::NodePtr &json) {
	auto catalog = AssetCatalog::FromJson(json);
	if (catalog.IsError())
		return Some(catalog.Error());
	std::scoped_lock lock(m_idMutex);
	m_catalog = std::move(catalog).Unwrap();
	m_resident.clear();
	m_missing.clear();
	return NONE;
}

} // namespace resources
