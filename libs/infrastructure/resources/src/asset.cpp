// Définitions de resources/asset.hpp
#include "resources/asset.hpp"

namespace resources {

namespace {

/// Espaces de noms des identifiants déterministes (deux Uuid fixes, tirés une
/// fois pour toutes) : un chemin et une clé de même texte ne se confondent pas.
constexpr Uuid PATH_SPACE{0x6a1d2c3e4f5b4a7cull, 0x8e9f0a1b2c3d4e5full};
constexpr Uuid KEY_SPACE{0x1b2c3d4e5f604a71ull, 0x9283a4b5c6d7e8f9ull};

String StrOf(const dataio::NodePtr &node) {
	return node && node->IsString() ? node->stringValue : String();
}

} // namespace

const char *AssetTypeName(AssetType type) noexcept {
	switch (type) {
		case AssetType::MESH:
			return "mesh";
		case AssetType::TEXTURE:
			return "texture";
		case AssetType::MATERIAL:
			return "material";
		case AssetType::SHADER:
			return "shader";
		case AssetType::AUDIO_CLIP:
			return "audio";
		case AssetType::OBJECT_DEFINITION:
			return "object";
		case AssetType::BYTES:
			return "bytes";
		case AssetType::UNKNOWN:
			break;
	}
	return "unknown";
}

AssetType AssetTypeFromName(StringView name) noexcept {
	for (AssetType type : {AssetType::MESH, AssetType::TEXTURE, AssetType::MATERIAL, AssetType::SHADER,
						   AssetType::AUDIO_CLIP, AssetType::OBJECT_DEFINITION, AssetType::BYTES})
		if (name == StringView(AssetTypeName(type)))
			return type;
	return AssetType::UNKNOWN;
}

AssetId AssetId::FromPath(StringView normalizedPath) noexcept {
	return AssetId{Uuid::FromName(PATH_SPACE, normalizedPath)};
}

AssetId AssetId::FromKey(StringView key) noexcept {
	return AssetId{Uuid::FromName(KEY_SPACE, key)};
}

Option<AssetId> AssetId::Parse(StringView text) noexcept {
	auto uuid = Uuid::Parse(text);
	if (uuid.IsNone() || uuid.Unwrap().IsNil())
		return NONE;
	return Some(AssetId{uuid.Unwrap()});
}

// ── AssetCatalog ─────────────────────────────────────────────────────────────

const AssetRecord &AssetCatalog::Put(AssetRecord record) {
	for (AssetRecord &existing : m_records)
		if (existing.id == record.id) {
			existing = std::move(record);
			return existing;
		}
	m_records.push_back(std::move(record));
	return m_records.back();
}

bool AssetCatalog::Remove(AssetId id) {
	for (auto it = m_records.begin(); it != m_records.end(); ++it)
		if (it->id == id) {
			m_records.erase(it);
			return true;
		}
	return false;
}

const AssetRecord *AssetCatalog::Find(AssetId id) const noexcept {
	for (const AssetRecord &record : m_records)
		if (record.id == id)
			return &record;
	return nullptr;
}

const AssetRecord *AssetCatalog::FindByPath(StringView path) const noexcept {
	if (path.IsEmpty())
		return nullptr;
	for (const AssetRecord &record : m_records)
		if (record.path.View() == path)
			return &record;
	return nullptr;
}

const AssetRecord *AssetCatalog::FindByKey(StringView key) const noexcept {
	if (key.IsEmpty())
		return nullptr;
	for (const AssetRecord &record : m_records)
		if (record.key.View() == key)
			return &record;
	return nullptr;
}

std::vector<const AssetRecord *> AssetCatalog::OfType(AssetType type) const {
	std::vector<const AssetRecord *> out;
	for (const AssetRecord &record : m_records)
		if (record.type == type)
			out.push_back(&record);
	return out;
}

dataio::NodePtr AssetCatalog::ToJson() const {
	auto root = dataio::Node::MakeObject();
	root->Set("format", dataio::Node::MakeString(String("assets.catalog")));
	root->Set("version", dataio::Node::MakeInt(FORMAT_VERSION));
	auto array = dataio::Node::MakeArray();
	for (const AssetRecord &record : m_records) {
		auto entry = dataio::Node::MakeObject();
		entry->Set("id", dataio::Node::MakeString(record.id.ToString()));
		entry->Set("type", dataio::Node::MakeString(String(AssetTypeName(record.type))));
		if (!record.name.IsEmpty())
			entry->Set("name", dataio::Node::MakeString(record.name));
		if (!record.path.IsEmpty())
			entry->Set("path", dataio::Node::MakeString(record.path));
		if (!record.key.IsEmpty())
			entry->Set("key", dataio::Node::MakeString(record.key));
		if (record.params)
			entry->Set("params", record.params->Clone());
		array->Push(entry);
	}
	root->Set("assets", array);
	return root;
}

Result<AssetCatalog, String> AssetCatalog::FromJson(const dataio::NodePtr &json) {
	if (!json || !json->IsObject())
		return Err(String("catalogue d'assets : objet JSON attendu"));
	const dataio::NodePtr version = json->Get("version");
	if (version && version->IsNumber() && version->intValue > FORMAT_VERSION)
		return Err(String::Format("catalogue d'assets : version %d plus récente que ce programme (%d)",
								  int(version->intValue), FORMAT_VERSION));
	AssetCatalog catalog;
	const dataio::NodePtr assets = json->Get("assets");
	if (!assets || !assets->IsArray())
		return Ok(std::move(catalog));
	for (size_t i = 0; i < assets->GetSize(); ++i) {
		const dataio::NodePtr entry = assets->At(i);
		if (!entry || !entry->IsObject())
			continue;
		auto id = AssetId::Parse(StrOf(entry->Get("id")).View());
		if (id.IsNone())
			return Err(String::Format("catalogue d'assets : entrée %d sans identifiant valide", int(i)));
		AssetRecord record;
		record.id = id.Unwrap();
		record.type = AssetTypeFromName(StrOf(entry->Get("type")).View());
		record.name = StrOf(entry->Get("name"));
		record.path = StrOf(entry->Get("path"));
		record.key = StrOf(entry->Get("key"));
		if (const dataio::NodePtr params = entry->Get("params"); params && !params->IsNone())
			record.params = params->Clone();
		(void)catalog.Put(std::move(record));
	}
	return Ok(std::move(catalog));
}

} // namespace resources
