// Définitions de voxel/save.hpp
#include <voxel/entities.hpp>
#include <voxel/json.hpp>
#include <voxel/save.hpp>

#include <sql/binary_storage.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <cstdio>
#include <fstream>

namespace voxel {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t CHUNK_MAGIC = 0x4B435856; // « VXCK »
/// 1 : params bruts (CHUNK_VOLUME octets) ; 2 : params en plages.
constexpr uint16_t CHUNK_FORMAT = 2;
constexpr uint8_t PARAMS_NONE = 0, PARAMS_RAW = 1, PARAMS_RUNS = 2;

constexpr const char *DB_FORMAT = "voxel.world.db";
constexpr int DB_VERSION = 1;

using dataio::Node;

String LegacyChunkKey(const ChunkPos &p) { return String::Format("%d,%d,%d", p.x, p.y, p.z); }

String Bytes(const std::vector<uint8_t> &bytes) { return String(reinterpret_cast<const char *>(bytes.data()), bytes.size()); }

std::vector<uint8_t> Bytes(const String &text) {
	const auto *data = reinterpret_cast<const uint8_t *>(text.CStr());
	return std::vector<uint8_t>(data, data + text.GetSize());
}

String NowIso() {
	const std::time_t now = std::time(nullptr);
	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
	return String(buffer);
}

void PutU16(std::vector<uint8_t> &out, uint16_t v) {
	out.push_back(uint8_t(v));
	out.push_back(uint8_t(v >> 8));
}
void PutU32(std::vector<uint8_t> &out, uint32_t v) {
	for (int i = 0; i < 4; ++i)
		out.push_back(uint8_t(v >> (8 * i)));
}

struct Reader {
	const std::vector<uint8_t> &data;
	size_t at = 0;
	bool ok = true;
	uint8_t U8() {
		if (at >= data.size()) {
			ok = false;
			return 0;
		}
		return data[at++];
	}
	uint16_t U16() { return uint16_t(U8() | (U8() << 8)); }
	uint32_t U32() {
		uint32_t v = 0;
		for (int i = 0; i < 4; ++i)
			v |= uint32_t(U8()) << (8 * i);
		return v;
	}
};

NodePtr Vec(const math::FVector3 &v) {
	NodePtr n = dataio::Node::MakeArray();
	n->Push(json::MakeNum(v.x));
	n->Push(json::MakeNum(v.y));
	n->Push(json::MakeNum(v.z));
	return n;
}

std::vector<uint8_t> ReadBytes(const String &path) {
	std::ifstream file(path.CStr(), std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

} // namespace

WorldSave::WorldSave(const ContentRegistry &content, String directory) : m_content(content), m_dir(std::move(directory)) {
	// Version 1 → 2 : le point d'apparition était un tableau « spawn »
	// [x, y, z] et l'horloge s'appelait « time » ; les joueurs vivaient dans
	// un seul fichier players.json.
	RegisterMigration(1, [](NodePtr &world, const String &dir) -> Option<String> {
		if (NodePtr spawn = json::At(world, "spawn"); spawn && spawn->IsArray()) {
			NodePtr obj = dataio::Node::MakeObject();
			obj->Set("x", spawn->At(0) ? spawn->At(0)->Clone() : json::MakeNum(0));
			obj->Set("y", spawn->At(1) ? spawn->At(1)->Clone() : json::MakeNum(64));
			obj->Set("z", spawn->At(2) ? spawn->At(2)->Clone() : json::MakeNum(0));
			world->Set("spawn", obj);
		}
		if (json::Has(world, "time") && !json::Has(world, "gameTime")) {
			world->Set("gameTime", json::At(world, "time")->Clone());
			world->Remove("time");
		}
		const String legacy = dir + String("/players.json");
		if (fs::exists(legacy.CStr())) {
			Result<NodePtr, String> players = json::ReadFile(legacy);
			if (players.IsError())
				return Some(players.Error());
			NodePtr list = players.Value();
			for (size_t i = 0; list && list->IsArray() && i < list->GetSize(); ++i) {
				NodePtr p = list->At(i);
				const String name = json::Str(p, "name", "Joueur");
				if (auto written = json::WriteFile(dir + String("/players/") + name + String(".json"), p); written.IsError())
					return Some(written.Error());
			}
			std::error_code ec;
			fs::remove(legacy.CStr(), ec);
		}
		return NONE;
	});
	// Version 2 → 3 : un fichier par chunk, entities.json et players/*.json
	// rejoignent la base world.db.
	RegisterMigration(2, [this](NodePtr &, const String &) -> Option<String> { return ImportLegacyFiles(); });
}

WorldSave::~WorldSave() {
	if (m_dirty)
		(void)Flush();
}

void WorldSave::RegisterMigration(int fromVersion, Migration migration) { m_migrations[fromVersion] = std::move(migration); }

// ── Base world.db ────────────────────────────────────────────────────────────

String WorldSave::DatabasePath() const { return m_dir + String("/world.db"); }

void WorldSave::CreateSchema(sql::Database &db) const {
	(void)db.Execute("CREATE TABLE info (key STRING PRIMARY KEY, value STRING)");
	(void)db.Execute("CREATE TABLE chunks (pos STRING PRIMARY KEY, x INT, y INT, z INT, data STRING)");
	(void)db.Execute("CREATE TABLE entities (uuid STRING PRIMARY KEY, kind STRING, x FLOAT, y FLOAT, z FLOAT, data STRING)");
	(void)db.Execute("CREATE TABLE players (name STRING PRIMARY KEY, data STRING, saved STRING)");
	(void)db.Upsert("info", {Node::MakeString(String("format")), Node::MakeString(String(DB_FORMAT))});
	(void)db.Upsert("info", {Node::MakeString(String("version")), Node::MakeString(String::Format("%d", DB_VERSION))});
}

Result<bool, String> WorldSave::OpenDatabase() {
	if (m_dbOpen)
		return Ok(true);
	const String path = DatabasePath(), backup = path + String(".bak");
	auto valid = [](sql::Database &db) {
		for (const char *table : {"info", "chunks", "entities", "players"})
			if (db.GetSchema(String(table)).IsNone())
				return false;
		auto format = db.FindByKey(String("info"), Node::MakeString(String("format")));
		return format.IsOk() && format.Value().IsSome() && format.Value().Unwrap()[1]->stringValue == DB_FORMAT;
	};
	String failure;
	for (const String &candidate : {path, backup}) {
		if (!fs::exists(candidate.CStr()))
			continue;
		Result<sql::Database, String> loaded = sql::LoadDatabaseBinary(candidate);
		if (loaded.IsOk() && valid(loaded.Value())) {
			m_db = std::move(loaded.Value());
			if (candidate == backup) {
				m_warnings.push_back(String::Format("%s illisible (%s) : sauvegarde précédente restaurée", path.CStr(), failure.CStr()));
				m_dirty = true; // réécrit au propre à la prochaine sauvegarde
			}
			m_dbOpen = true;
			return Ok(true);
		}
		failure = loaded.IsError() ? loaded.Error() : String("tables manquantes");
	}
	if (!failure.IsEmpty()) {
		// Rien de lisible : on écarte les fichiers abîmés (jamais écrasés)
		// et on repart d'une base vide.
		const String stamp = NowIso();
		std::error_code ec;
		for (const String &candidate : {path, backup})
			if (fs::exists(candidate.CStr()))
				fs::rename(candidate.CStr(), (candidate + String(".corrupt-") + stamp).CStr(), ec);
		m_warnings.push_back(String::Format("base du monde illisible (%s) : mise de côté, monde régénéré", failure.CStr()));
	}
	m_db = sql::Database();
	CreateSchema(m_db);
	m_dbOpen = true;
	return Ok(true);
}

sql::Database &WorldSave::Database() {
	(void)OpenDatabase();
	return m_db;
}

size_t WorldSave::StoredChunkCount() {
	(void)OpenDatabase();
	return m_db.RowCount(String("chunks"));
}

Result<bool, String> WorldSave::Flush() {
	if (!m_dirty)
		return Ok(true);
	if (Result<bool, String> r = OpenDatabase(); r.IsError())
		return r;
	std::error_code ec;
	fs::create_directories(m_dir.CStr(), ec);
	// La base actuelle devient la sauvegarde de repli, puis la nouvelle la
	// remplace atomiquement (cf. sql::SaveDatabaseBinary).
	const String path = DatabasePath();
	if (fs::exists(path.CStr()))
		fs::copy_file(path.CStr(), (path + String(".bak")).CStr(), fs::copy_options::overwrite_existing, ec);
	if (!sql::SaveDatabaseBinary(path, m_db))
		return Err(String::Format("écriture de %s impossible", path.CStr()));
	m_dirty = false;
	return Ok(true);
}

Option<String> WorldSave::ImportLegacyFiles() {
	if (Result<bool, String> r = OpenDatabase(); r.IsError())
		return Some(r.Error());
	const fs::path dir(m_dir.CStr());
	std::error_code ec;
	// Chunks : un fichier « x_y_z.chunk » chacun, réencodé au format courant
	// (repris tel quel s'il est illisible : le jeu le signalera au chargement).
	size_t chunks = 0;
	if (fs::is_directory(dir / "chunks", ec))
		for (const auto &entry : fs::directory_iterator(dir / "chunks", ec)) {
			int x = 0, y = 0, z = 0;
			if (entry.path().extension() != ".chunk" ||
				std::sscanf(entry.path().stem().string().c_str(), "%d_%d_%d", &x, &y, &z) != 3)
				continue;
			const std::vector<uint8_t> bytes = ReadBytes(String(entry.path().string().c_str()));
			if (bytes.empty())
				continue;
			const ChunkPos p{x, y, z};
			Chunk chunk(p);
			const std::vector<uint8_t> stored = DecodeChunk(bytes, chunk) ? EncodeChunk(chunk) : bytes;
			(void)m_db.Upsert(String("chunks"), {Node::MakeString(LegacyChunkKey(p)), Node::MakeInt(x), Node::MakeInt(y), Node::MakeInt(z),
												  Node::MakeString(Bytes(stored))});
			++chunks;
		}
	if (fs::exists(dir / "entities.json")) {
		Result<NodePtr, String> read = json::ReadFile(String((dir / "entities.json").string().c_str()));
		if (read.IsError())
			return Some(read.Error());
		NodePtr list = json::At(read.Value(), "entities");
		for (size_t i = 0; list && i < list->GetSize(); ++i) {
			NodePtr e = list->At(i);
			String uuid = json::Str(e, "uuid");
			if (uuid.IsEmpty())
				uuid = Uuid::Generate().ToString();
			const math::FVector3 pos = json::Vec3(e, "position");
			(void)m_db.Upsert(String("entities"), {Node::MakeString(uuid), Node::MakeString(json::Str(e, "kind")), Node::MakeFloat(pos.x),
												   Node::MakeFloat(pos.y), Node::MakeFloat(pos.z), Node::MakeString(json::Encode(e))});
		}
	}
	if (fs::is_directory(dir / "players", ec))
		for (const auto &entry : fs::directory_iterator(dir / "players", ec)) {
			if (entry.path().extension() != ".json")
				continue;
			Result<NodePtr, String> read = json::ReadFile(String(entry.path().string().c_str()));
			if (read.IsError())
				return Some(read.Error());
			const String name = json::Str(read.Value(), "name", String(entry.path().stem().string().c_str()));
			(void)m_db.Upsert(String("players"),
							  {Node::MakeString(name), Node::MakeString(json::Encode(read.Value())), Node::MakeString(NowIso())});
		}
	// La base doit être écrite AVANT de supprimer les anciens fichiers.
	m_dirty = true;
	if (Result<bool, String> r = Flush(); r.IsError())
		return Some(r.Error());
	// Relue et comparée : les anciens fichiers ne partent que si la base
	// écrite contient bien tout.
	Result<sql::Database, String> check = sql::LoadDatabaseBinary(DatabasePath());
	if (check.IsError())
		return Some(String("relecture de world.db : ") + check.Error());
	for (const char *table : {"chunks", "entities", "players"})
		if (check.Value().RowCount(String(table)) != m_db.RowCount(String(table)))
			return Some(String::Format("relecture de world.db : table %s incomplète", table));
	fs::remove_all(dir / "chunks", ec);
	fs::remove_all(dir / "players", ec);
	fs::remove(dir / "entities.json", ec);
	if (chunks > 0)
		m_warnings.push_back(String::Format("ancienne sauvegarde importée dans world.db (%d chunk(s))", int(chunks)));
	return NONE;
}

bool WorldSave::Exists() const { return fs::exists((m_dir + String("/world.json")).CStr()); }

Result<Option<WorldMetadata>, String> WorldSave::LoadMetadata() {
	if (!Exists())
		return Ok(Option<WorldMetadata>(NONE));
	Result<NodePtr, String> read = json::ReadFile(m_dir + String("/world.json"));
	if (read.IsError())
		return Err(read.Error());
	NodePtr world = read.Unwrap();
	if (json::Str(world, "format", SAVE_FORMAT) != SAVE_FORMAT)
		return Err(String::Format("%s : ce n'est pas une sauvegarde de monde voxel", m_dir.CStr()));
	int version = json::Int(world, "version", 1);
	if (version > SAVE_VERSION)
		return Err(String::Format("sauvegarde de version %d, plus récente que le jeu (%d)", version, SAVE_VERSION));
	const bool migrated = version < SAVE_VERSION;
	while (version < SAVE_VERSION) {
		if (auto it = m_migrations.find(version); it != m_migrations.end())
			if (Option<String> err = it->second(world, m_dir); err.IsSome())
				return Err(String::Format("migration %d → %d : %s", version, version + 1, err.Unwrap().CStr()));
		++version;
		world->Set("version", json::MakeInt(version));
	}
	if (migrated)
		(void)json::WriteFile(m_dir + String("/world.json"), world);
	if (Result<bool, String> r = OpenDatabase(); r.IsError())
		return Err(r.Error());
	WorldMetadata m;
	m.name = json::Str(world, "name", m.name);
	m.seed = uint32_t(json::Num(world, "seed", m.seed));
	m.generator = json::Str(world, "generator");
	m.version = version;
	m.created = json::Str(world, "created");
	m.lastPlayed = json::Str(world, "lastPlayed");
	m.playTime = json::Num(world, "playTime", 0.0);
	m.gameTime = json::Num(world, "gameTime", 0.0);
	m.spawnKnown = json::Has(world, "spawn");
	m.spawn = json::Vec3(world, "spawn", m.spawn);
	if (NodePtr mods = json::At(world, "mods"); mods && mods->IsArray())
		for (size_t i = 0; i < mods->GetSize(); ++i)
			m.mods.push_back({json::Str(mods->At(i), "id"), json::Str(mods->At(i), "version")});
	m.settings = json::At(world, "settings");
	return Ok(Option<WorldMetadata>(Some(std::move(m))));
}

Result<bool, String> WorldSave::SaveMetadata(const WorldMetadata &m) {
	NodePtr world = dataio::Node::MakeObject();
	world->Set("format", json::MakeStr(SAVE_FORMAT));
	world->Set("version", json::MakeInt(SAVE_VERSION));
	world->Set("name", json::MakeStr(m.name));
	world->Set("seed", json::MakeInt(m.seed));
	world->Set("generator", json::MakeStr(m.generator));
	world->Set("created", json::MakeStr(m.created.IsEmpty() ? NowIso() : m.created));
	world->Set("lastPlayed", json::MakeStr(NowIso()));
	world->Set("playTime", json::MakeNum(m.playTime));
	world->Set("gameTime", json::MakeNum(m.gameTime));
	if (m.spawnKnown)
		world->Set("spawn", json::MakeVec3(m.spawn));
	NodePtr mods = dataio::Node::MakeArray();
	for (const auto &[id, version] : m.mods) {
		NodePtr mod = dataio::Node::MakeObject();
		mod->Set("id", json::MakeStr(id));
		mod->Set("version", json::MakeStr(version));
		mods->Push(mod);
	}
	world->Set("mods", mods);
	if (m.settings)
		world->Set("settings", m.settings->Clone());
	return json::WriteFile(m_dir + String("/world.json"), world);
}

std::vector<uint8_t> WorldSave::EncodeChunk(const Chunk &chunk) const {
	const std::vector<BlockId> blocks = chunk.Blocks();
	// Palette locale : indices compacts, identifiants textuels.
	std::vector<BlockId> palette;
	std::map<BlockId, uint16_t> index;
	for (BlockId b : blocks)
		if (!index.contains(b)) {
			index[b] = uint16_t(palette.size());
			palette.push_back(b);
		}
	std::vector<uint8_t> out;
	PutU32(out, CHUNK_MAGIC);
	PutU16(out, CHUNK_FORMAT);
	PutU16(out, uint16_t(palette.size()));
	for (BlockId b : palette) {
		const String &id = m_content.Block(b).id;
		PutU16(out, uint16_t(id.GetSize()));
		out.insert(out.end(), id.CStr(), id.CStr() + id.GetSize());
	}
	// Plages (indice, longueur).
	size_t runs = 0;
	std::vector<uint8_t> body;
	for (size_t i = 0; i < blocks.size();) {
		size_t j = i;
		while (j < blocks.size() && blocks[j] == blocks[i] && j - i < 0xFFFF)
			++j;
		PutU16(body, index[blocks[i]]);
		PutU16(body, uint16_t(j - i));
		++runs;
		i = j;
	}
	PutU32(out, uint32_t(runs));
	out.insert(out.end(), body.begin(), body.end());
	// Params : absents, ou en plages (valeur, longueur) — surtout des zéros.
	const std::vector<uint8_t> &params = chunk.Params();
	if (params.empty()) {
		out.push_back(PARAMS_NONE);
		return out;
	}
	out.push_back(PARAMS_RUNS);
	std::vector<uint8_t> paramRuns;
	uint32_t paramRunCount = 0;
	for (size_t i = 0; i < params.size();) {
		size_t j = i;
		while (j < params.size() && params[j] == params[i] && j - i < 0xFFFF)
			++j;
		paramRuns.push_back(params[i]);
		PutU16(paramRuns, uint16_t(j - i));
		++paramRunCount;
		i = j;
	}
	PutU32(out, paramRunCount);
	out.insert(out.end(), paramRuns.begin(), paramRuns.end());
	return out;
}

bool WorldSave::DecodeChunk(const std::vector<uint8_t> &bytes, Chunk &chunk) {
	Reader r{bytes};
	if (r.U32() != CHUNK_MAGIC)
		return false;
	const uint16_t format = r.U16();
	if (format > CHUNK_FORMAT)
		return false;
	const uint16_t paletteSize = r.U16();
	std::vector<BlockId> palette;
	for (uint16_t i = 0; i < paletteSize && r.ok; ++i) {
		const uint16_t length = r.U16();
		std::string id;
		for (uint16_t k = 0; k < length; ++k)
			id.push_back(char(r.U8()));
		const String key(id.c_str(), id.size());
		const BlockDef *def = m_content.FindBlock(key);
		if (!def && !m_reportedUnknown[id]) {
			m_reportedUnknown[id] = true;
			m_warnings.push_back(String::Format("bloc inconnu « %s » (mod retiré ?) : remplacé par de l'air", id.c_str()));
		}
		palette.push_back(def ? def->numeric : AIR);
	}
	const uint32_t runs = r.U32();
	std::vector<BlockId> blocks;
	blocks.reserve(CHUNK_VOLUME);
	for (uint32_t i = 0; i < runs && r.ok; ++i) {
		const uint16_t idx = r.U16(), count = r.U16();
		const BlockId id = idx < palette.size() ? palette[idx] : AIR;
		blocks.insert(blocks.end(), count, id);
	}
	if (!r.ok || blocks.size() != size_t(CHUNK_VOLUME))
		return false;
	std::vector<uint8_t> params;
	const uint8_t paramsMode = r.U8();
	if (paramsMode == PARAMS_RAW && r.ok) {
		params.reserve(CHUNK_VOLUME);
		for (int i = 0; i < CHUNK_VOLUME && r.ok; ++i)
			params.push_back(r.U8());
	} else if (paramsMode == PARAMS_RUNS && r.ok) {
		params.reserve(CHUNK_VOLUME);
		const uint32_t paramRuns = r.U32();
		for (uint32_t i = 0; i < paramRuns && r.ok && params.size() <= size_t(CHUNK_VOLUME); ++i) {
			const uint8_t value = r.U8();
			const uint16_t count = r.U16();
			params.insert(params.end(), count, value);
		}
		if (params.size() != size_t(CHUNK_VOLUME))
			return false;
	}
	chunk.Assign(std::move(blocks), std::move(params));
	return r.ok;
}

String WorldSave::ChunkKey(const ChunkPos &p) const {
	return m_dimension.IsEmpty() ? LegacyChunkKey(p) : m_dimension + String("|") + LegacyChunkKey(p);
}

bool WorldSave::LoadChunk(Chunk &chunk) {
	if (OpenDatabase().IsError())
		return false;
	Result<Option<sql::Row>, String> row = m_db.FindByKey(String("chunks"), Node::MakeString(ChunkKey(chunk.Pos())));
	if (row.IsError() || row.Value().IsNone())
		return false;
	if (!DecodeChunk(Bytes(row.Value().Unwrap()[4]->stringValue), chunk)) {
		m_warnings.push_back(String::Format("chunk illisible %s : régénéré", ChunkKey(chunk.Pos()).CStr()));
		return false;
	}
	return true;
}

void WorldSave::SaveChunk(const Chunk &chunk) {
	if (OpenDatabase().IsError())
		return;
	const ChunkPos &p = chunk.Pos();
	(void)m_db.Upsert(String("chunks"), {Node::MakeString(ChunkKey(p)), Node::MakeInt(p.x), Node::MakeInt(p.y), Node::MakeInt(p.z),
										  Node::MakeString(Bytes(EncodeChunk(chunk)))});
	m_dirty = true;
}

NodePtr EntityToJson(ecs::ArchetypeRegistry &registry, ecs::Entity e) {
	NodePtr n = dataio::Node::MakeObject();
	if (auto kind = registry.GetComponent<EntityKind>(e); kind.IsSome())
		n->Set("kind", json::MakeStr(kind.Unwrap()->id));
	if (auto p = registry.GetComponent<Persistent>(e); p.IsSome())
		n->Set("uuid", json::MakeStr(p.Unwrap()->uuid.ToString()));
	if (auto t = registry.GetComponent<Transform>(e); t.IsSome()) {
		n->Set("position", Vec(t.Unwrap()->position));
		n->Set("yaw", json::MakeNum(t.Unwrap()->yaw));
		n->Set("pitch", json::MakeNum(t.Unwrap()->pitch));
	}
	if (auto v = registry.GetComponent<Velocity>(e); v.IsSome())
		n->Set("velocity", Vec(v.Unwrap()->linear));
	if (auto h = registry.GetComponent<Health>(e); h.IsSome()) {
		n->Set("health", json::MakeNum(h.Unwrap()->current));
		n->Set("maxHealth", json::MakeNum(h.Unwrap()->max));
	}
	if (auto props = registry.GetComponent<Properties>(e); props.IsSome() && props.Unwrap()->values) {
		// Seulement l'état propre des scripts (« state »), pas la définition.
		if (NodePtr state = json::At(props.Unwrap()->values, "state"))
			n->Set("state", state->Clone());
	}
	if (auto inv = registry.GetComponent<InventoryComponent>(e); inv.IsSome())
		n->Set("inventory", inv.Unwrap()->inventory.ToJson());
	if (auto item = registry.GetComponent<DroppedItem>(e); item.IsSome()) {
		n->Set("item", json::MakeStr(item.Unwrap()->stack.item));
		n->Set("count", json::MakeInt(item.Unwrap()->stack.count));
		n->Set("age", json::MakeNum(item.Unwrap()->age));
	}
	if (auto player = registry.GetComponent<Player>(e); player.IsSome()) {
		n->Set("name", json::MakeStr(player.Unwrap()->name));
		n->Set("selectedSlot", json::MakeInt(player.Unwrap()->selectedSlot));
		if (!player.Unwrap()->dimension.IsEmpty())
			n->Set("dimension", json::MakeStr(player.Unwrap()->dimension));
		if (!player.Unwrap()->returnDimension.IsEmpty()) {
			n->Set("returnDimension", json::MakeStr(player.Unwrap()->returnDimension));
			n->Set("returnPosition", Vec(player.Unwrap()->returnPosition));
		}
	}
	if (auto body = registry.GetComponent<PhysicsBody>(e); body.IsSome() && body.Unwrap()->gravityDirection < 0.f)
		n->Set("gravityInverted", json::MakeBool(true));
	if (auto body = registry.GetComponent<PhysicsBody>(e); body.IsSome() && body.Unwrap()->flies)
		n->Set("flying", json::MakeBool(true));
	if (auto sv = registry.GetComponent<Survival>(e); sv.IsSome()) {
		NodePtr s = dataio::Node::MakeObject();
		s->Set("hunger", json::MakeNum(sv.Unwrap()->hunger));
		s->Set("saturation", json::MakeNum(sv.Unwrap()->saturation));
		s->Set("stamina", json::MakeNum(sv.Unwrap()->stamina));
		s->Set("air", json::MakeNum(sv.Unwrap()->air));
		s->Set("xp", json::MakeInt(sv.Unwrap()->xp));
		n->Set("survival", s);
	}
	if (auto fx = registry.GetComponent<StatusEffects>(e); fx.IsSome() && !fx.Unwrap()->list.empty()) {
		NodePtr list = dataio::Node::MakeArray();
		for (const StatusEffect &effect : fx.Unwrap()->list) {
			NodePtr o = dataio::Node::MakeObject();
			o->Set("id", json::MakeStr(effect.id));
			o->Set("seconds", json::MakeNum(effect.remaining));
			o->Set("level", json::MakeInt(effect.level));
			list->Push(o);
		}
		n->Set("effects", list);
	}
	if (auto eq = registry.GetComponent<Equipment>(e); eq.IsSome()) {
		Inventory armor(ARMOR_SLOTS);
		for (int i = 0; i < ARMOR_SLOTS; ++i)
			armor.Slot(i) = eq.Unwrap()->armor[size_t(i)];
		n->Set("armor", armor.ToJson());
	}
	if (auto visual = registry.GetComponent<Visual>(e); visual.IsSome() && registry.HasComponent<Player>(e))
		n->Set("skin", json::MakeStr(visual.Unwrap()->skin));
	return n;
}

ecs::Entity EntityFromJson(ecs::ArchetypeRegistry &registry, const ContentRegistry &content, const NodePtr &n) {
	const String kind = json::Str(n, "kind");
	const math::FVector3 position = json::Vec3(n, "position");
	ecs::Entity e;
	if (kind == ITEM_KIND) {
		e = SpawnDroppedItem(registry, content, ItemStack{json::Str(n, "item"), json::Int(n, "count", 1), 0, nullptr}, position,
							 json::Vec3(n, "velocity"));
		if (auto item = registry.GetComponent<DroppedItem>(e); item.IsSome())
			item.Unwrap()->age = float(json::Num(n, "age", 0.0));
	} else if (const EntityDef *def = content.FindEntity(kind)) {
		e = SpawnFromDef(registry, *def, position);
	} else {
		return ecs::Entity{};
	}
	if (auto t = registry.GetComponent<Transform>(e); t.IsSome()) {
		t.Unwrap()->yaw = float(json::Num(n, "yaw", 0.0));
		t.Unwrap()->pitch = float(json::Num(n, "pitch", 0.0));
	}
	if (auto v = registry.GetComponent<Velocity>(e); v.IsSome())
		v.Unwrap()->linear = json::Vec3(n, "velocity");
	if (auto h = registry.GetComponent<Health>(e); h.IsSome() && json::Has(n, "health"))
		h.Unwrap()->current = float(json::Num(n, "health", h.Unwrap()->current));
	if (auto p = registry.GetComponent<Persistent>(e); p.IsSome())
		if (Option<Uuid> id = Uuid::Parse(json::Str(n, "uuid").View()); id.IsSome())
			p.Unwrap()->uuid = id.Unwrap();
	if (NodePtr state = json::At(n, "state"))
		if (auto props = registry.GetComponent<Properties>(e); props.IsSome() && props.Unwrap()->values)
			props.Unwrap()->values->Set("state", state->Clone());
	if (NodePtr inv = json::At(n, "inventory"))
		registry.AddComponent(e, InventoryComponent{Inventory::FromJson(inv)});
	return e;
}

Result<bool, String> WorldSave::SaveEntities(ecs::ArchetypeRegistry &registry) {
	if (Result<bool, String> r = OpenDatabase(); r.IsError())
		return r;
	// Instantané complet : les entités disparues depuis quittent la table.
	std::vector<ecs::Entity> entities;
	registry.Query<Persistent, EntityKind>([&](ecs::Entity e, Persistent &, EntityKind &) { entities.push_back(e); });
	// Les entités des AUTRES dimensions restent : on les réécrit telles quelles.
	std::vector<sql::Row> others;
	(void)m_db.ForEachRow(String("entities"), [&](const sql::Row &row) {
		if (Result<NodePtr, String> n = json::Parse(row[5]->stringValue); n.IsOk() && json::Str(n.Value(), "dimension") != m_dimension)
			others.push_back(row);
	});
	if (Result<sql::QueryResult, String> r = m_db.Execute(String("DELETE FROM entities")); r.IsError())
		return Err(r.Error());
	for (sql::Row &row : others)
		if (Result<sql::QueryResult, String> r = m_db.Upsert(String("entities"), std::move(row)); r.IsError())
			return Err(r.Error());
	for (ecs::Entity e : entities) {
		if (registry.HasComponent<Player>(e))
			continue;
		const NodePtr n = EntityToJson(registry, e);
		if (!m_dimension.IsEmpty())
			n->Set("dimension", json::MakeStr(m_dimension));
		const math::FVector3 pos = json::Vec3(n, "position");
		if (Result<sql::QueryResult, String> r =
				m_db.Upsert(String("entities"), {Node::MakeString(json::Str(n, "uuid")), Node::MakeString(json::Str(n, "kind")),
												 Node::MakeFloat(pos.x), Node::MakeFloat(pos.y), Node::MakeFloat(pos.z),
												 Node::MakeString(json::Encode(n))});
			r.IsError())
			return Err(r.Error());
	}
	m_dirty = true;
	return Ok(true);
}

Result<size_t, String> WorldSave::LoadEntities(ecs::ArchetypeRegistry &registry) {
	if (Result<bool, String> r = OpenDatabase(); r.IsError())
		return Err(r.Error());
	std::vector<NodePtr> stored;
	(void)m_db.ForEachRow(String("entities"), [&](const sql::Row &row) {
		if (Result<NodePtr, String> n = json::Parse(row[5]->stringValue); n.IsOk()) {
			if (json::Str(n.Value(), "dimension") == m_dimension)
				stored.push_back(n.Value());
		}
		else
			m_warnings.push_back(String::Format("entité %s illisible : ignorée", row[0]->stringValue.CStr()));
	});
	size_t count = 0;
	for (const NodePtr &n : stored) {
		if (EntityFromJson(registry, m_content, n).Valid())
			++count;
		else
			m_warnings.push_back(String::Format("entité « %s » inconnue : ignorée", json::Str(n, "kind").CStr()));
	}
	return Ok(count);
}

Result<bool, String> WorldSave::SavePlayer(ecs::ArchetypeRegistry &registry, ecs::Entity player) {
	auto p = registry.GetComponent<Player>(player);
	if (p.IsNone())
		return Err(String("pas un joueur"));
	if (Result<bool, String> r = OpenDatabase(); r.IsError())
		return r;
	if (Result<sql::QueryResult, String> r =
			m_db.Upsert(String("players"), {Node::MakeString(p.Unwrap()->name), Node::MakeString(json::Encode(EntityToJson(registry, player))),
										   Node::MakeString(NowIso())});
		r.IsError())
		return Err(r.Error());
	m_dirty = true;
	return Ok(true);
}

bool WorldSave::LoadPlayer(ecs::ArchetypeRegistry &registry, ecs::Entity player) {
	auto p = registry.GetComponent<Player>(player);
	if (p.IsNone())
		return false;
	if (OpenDatabase().IsError())
		return false;
	Result<Option<sql::Row>, String> row = m_db.FindByKey(String("players"), Node::MakeString(p.Unwrap()->name));
	if (row.IsError() || row.Value().IsNone())
		return false;
	Result<NodePtr, String> read = json::Parse(row.Value().Unwrap()[1]->stringValue);
	if (read.IsError())
		return false;
	NodePtr n = read.Unwrap();
	if (auto t = registry.GetComponent<Transform>(player); t.IsSome()) {
		t.Unwrap()->position = t.Unwrap()->previous = json::Vec3(n, "position", t.Unwrap()->position);
		t.Unwrap()->yaw = float(json::Num(n, "yaw", 0.0));
		t.Unwrap()->pitch = float(json::Num(n, "pitch", 0.0));
	}
	if (auto h = registry.GetComponent<Health>(player); h.IsSome())
		h.Unwrap()->current = float(json::Num(n, "health", h.Unwrap()->current));
	if (auto inv = registry.GetComponent<InventoryComponent>(player); inv.IsSome() && json::Has(n, "inventory"))
		inv.Unwrap()->inventory = Inventory::FromJson(json::At(n, "inventory"));
	if (auto pl = registry.GetComponent<Player>(player); pl.IsSome()) {
		pl.Unwrap()->selectedSlot = json::Int(n, "selectedSlot", 0);
		pl.Unwrap()->dimension = json::Str(n, "dimension");
		pl.Unwrap()->returnDimension = json::Str(n, "returnDimension");
		pl.Unwrap()->returnPosition = json::Vec3(n, "returnPosition");
	}
	if (auto body = registry.GetComponent<PhysicsBody>(player); body.IsSome())
		body.Unwrap()->gravityDirection = json::Bool(n, "gravityInverted", false) ? -1.f : 1.f;
	if (auto body = registry.GetComponent<PhysicsBody>(player); body.IsSome())
		body.Unwrap()->flies = json::Bool(n, "flying", false);
	if (auto sv = registry.GetComponent<Survival>(player); sv.IsSome() && json::Has(n, "survival")) {
		sv.Unwrap()->hunger = float(json::Num(n, "survival.hunger", 20.0));
		sv.Unwrap()->saturation = float(json::Num(n, "survival.saturation", 5.0));
		sv.Unwrap()->stamina = float(json::Num(n, "survival.stamina", 1.0));
		sv.Unwrap()->air = float(json::Num(n, "survival.air", 10.0));
		sv.Unwrap()->xp = json::Int(n, "survival.xp", 0);
	}
	if (auto fx = registry.GetComponent<StatusEffects>(player); fx.IsSome())
		if (NodePtr list = json::At(n, "effects"); list && list->IsArray())
			for (size_t i = 0; i < list->GetSize(); ++i)
				fx.Unwrap()->list.push_back(StatusEffect{json::Str(list->At(i), "id"), float(json::Num(list->At(i), "seconds", 1.0)),
														 json::Int(list->At(i), "level", 1), 0.f});
	if (auto eq = registry.GetComponent<Equipment>(player); eq.IsSome() && json::Has(n, "armor")) {
		const Inventory armor = Inventory::FromJson(json::At(n, "armor"));
		for (int i = 0; i < ARMOR_SLOTS && i < armor.Size(); ++i)
			eq.Unwrap()->armor[size_t(i)] = armor.Slot(i);
	}
	if (auto visual = registry.GetComponent<Visual>(player); visual.IsSome())
		if (const String skin = json::Str(n, "skin"); !skin.IsEmpty())
			visual.Unwrap()->skin = skin;
	return true;
}

std::vector<String> WorldSave::ListWorlds(const String &savesDirectory) {
	std::vector<String> worlds;
	std::error_code ec;
	for (const auto &entry : fs::directory_iterator(savesDirectory.CStr(), ec))
		if (entry.is_directory() && fs::exists(entry.path() / "world.json"))
			worlds.push_back(String(entry.path().filename().string().c_str()));
	std::sort(worlds.begin(), worlds.end(), [](const String &a, const String &b) { return std::strcmp(a.CStr(), b.CStr()) < 0; });
	return worlds;
}

} // namespace voxel
