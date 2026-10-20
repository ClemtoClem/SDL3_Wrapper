// Définitions de catalog.hpp — mondes enregistrés et mods.
#include <voxelgame/catalog.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include <voxel/json.hpp>
#include <voxel/save.hpp>

namespace voxelgame {

namespace fs = std::filesystem;
namespace json = voxel::json;
using dataio::NodePtr;

namespace {

String Str(const fs::path &p) { return String(p.string().c_str()); }

uint64_t FolderSize(const fs::path &dir) {
	uint64_t total = 0;
	std::error_code ec;
	for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
		if (!ec && it->is_regular_file(ec))
			total += it->file_size(ec);
	return total;
}

String NowIso() {
	const std::time_t t = std::time(nullptr);
	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
	return String(buffer);
}

/// « 1.10.2 » > « 1.9 » : comparaison numérique champ par champ.
int CompareVersions(const String &a, const String &b) {
	auto parts = [](const String &v) {
		std::vector<int> out;
		int n = 0;
		bool any = false;
		for (size_t i = 0; i < v.GetSize(); ++i) {
			const char c = v.CharAt(i);
			if (c >= '0' && c <= '9') {
				n = n * 10 + (c - '0');
				any = true;
			} else if (c == '.') {
				out.push_back(n);
				n = 0;
				any = false;
			}
		}
		if (any || out.empty())
			out.push_back(n);
		return out;
	};
	const std::vector<int> pa = parts(a), pb = parts(b);
	for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
		const int x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
		if (x != y)
			return x < y ? -1 : 1;
	}
	return 0;
}

} // namespace

// ── Mondes ───────────────────────────────────────────────────────────────────

Option<WorldEntry> WorldCatalog::Read(const String &folder, const std::vector<voxel::ModInfo> &installed) const {
	const fs::path dir = fs::path(m_dir.CStr()) / folder.CStr();
	Result<NodePtr, String> read = json::ReadFile(Str(dir / "world.json"));
	if (read.IsError())
		return NONE;
	const NodePtr w = read.Unwrap();
	WorldEntry e;
	e.folder = folder;
	e.path = Str(dir);
	e.name = json::Str(w, "name", folder);
	e.created = json::Str(w, "created");
	e.lastPlayed = json::Str(w, "lastPlayed", e.created);
	e.playTime = json::Num(w, "playTime", 0.0);
	e.generator = json::Str(w, "generator");
	e.seed = uint32_t(json::Num(w, "seed", 0.0));
	e.version = json::Int(w, "version", 1);
	e.settings = json::At(w, "settings");
	if (!e.settings || !e.settings->IsObject())
		e.settings = dataio::Node::MakeObject();
	e.description = json::Str(e.settings, "description");
	e.gameMode = json::Str(e.settings, "gameMode", "survival");
	e.difficulty = json::Str(e.settings, "difficulty", "normal");
	e.enabledMods = json::StrList(e.settings, "mods.enabled");
	if (NodePtr mods = json::At(w, "mods"); mods && mods->IsArray())
		for (size_t i = 0; i < mods->GetSize(); ++i)
			e.mods.push_back({json::Str(mods->At(i), "id"), json::Str(mods->At(i), "version")});
	if (fs::exists(dir / "thumbnail.png"))
		e.thumbnail = Str(dir / "thumbnail.png");
	e.sizeBytes = FolderSize(dir);
	// Compatibilité : mods attendus présents, versions, format de sauvegarde.
	if (e.version > voxel::SAVE_VERSION)
		e.issues.push_back({WorldIssue::Kind::NEWER_SAVE, String(), String::Format("sauvegarde v%d, jeu v%d", e.version,
																				   voxel::SAVE_VERSION)});
	auto required = e.enabledMods;
	for (const auto &[id, version] : e.mods)
		if (std::find(required.begin(), required.end(), id) == required.end() && e.enabledMods.empty())
			required.push_back(id);
	for (const String &id : required) {
		auto it = std::find_if(installed.begin(), installed.end(), [&](const voxel::ModInfo &m) { return m.id == id; });
		if (it == installed.end()) {
			e.issues.push_back({WorldIssue::Kind::MISSING_MOD, id, String("mod absent")});
			continue;
		}
		for (const auto &[mid, version] : e.mods)
			if (mid == id && !version.IsEmpty() && CompareVersions(version, it->version) != 0)
				e.issues.push_back({WorldIssue::Kind::VERSION_CHANGED, id, String::Format("%s → %s", version.CStr(), it->version.CStr())});
	}
	return Some(std::move(e));
}

std::vector<WorldEntry> WorldCatalog::List(const std::vector<voxel::ModInfo> &installed) const {
	std::vector<WorldEntry> out;
	for (const String &folder : voxel::WorldSave::ListWorlds(m_dir))
		if (Option<WorldEntry> e = Read(folder, installed); e.IsSome())
			out.push_back(std::move(e).Unwrap());
	std::sort(out.begin(), out.end(), [](const WorldEntry &a, const WorldEntry &b) { return a.lastPlayed > b.lastPlayed; });
	return out;
}

Option<WorldEntry> WorldCatalog::Find(const String &folder, const std::vector<voxel::ModInfo> &installed) const {
	return Read(folder, installed);
}

String WorldCatalog::FreeFolder(const String &name) const {
	std::string base;
	for (size_t i = 0; i < name.GetSize(); ++i) {
		const unsigned char c = uint8_t(name.CharAt(i));
		if (std::isalnum(c))
			base += char(std::tolower(c));
		else if ((c == ' ' || c == '-' || c == '_') && !base.empty() && base.back() != '_')
			base += '_';
		else if (c >= 0x80 && !base.empty() && base.back() != '_')
			base += '_'; // accents : remplacés (noms de dossiers portables)
	}
	while (!base.empty() && base.back() == '_')
		base.pop_back();
	if (base.empty())
		base = "monde";
	std::string candidate = base;
	for (int n = 2; fs::exists(fs::path(m_dir.CStr()) / candidate); ++n)
		candidate = base + "_" + std::to_string(n);
	return String(candidate.c_str());
}

Result<String, String> WorldCatalog::Create(const String &name, uint32_t seed, const String &generator, const NodePtr &settings) const {
	const String folder = FreeFolder(name);
	const fs::path dir = fs::path(m_dir.CStr()) / folder.CStr();
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (ec)
		return Err(String::Format("dossier %s : %s", Str(dir).CStr(), ec.message().c_str()));
	NodePtr world = dataio::Node::MakeObject();
	world->Set("format", json::MakeStr(voxel::SAVE_FORMAT));
	world->Set("version", json::MakeInt(voxel::SAVE_VERSION));
	world->Set("name", json::MakeStr(name));
	world->Set("seed", json::MakeInt(int64_t(seed)));
	world->Set("generator", json::MakeStr(generator));
	world->Set("created", json::MakeStr(NowIso()));
	world->Set("lastPlayed", json::MakeStr(NowIso()));
	world->Set("playTime", json::MakeNum(0.0));
	world->Set("gameTime", json::MakeNum(-1.0)); // < 0 : heure de départ des mods
	world->Set("mods", dataio::Node::MakeArray());
	world->Set("settings", settings ? settings->Clone() : dataio::Node::MakeObject());
	if (Result<bool, String> r = json::WriteFile(Str(dir / "world.json"), world); r.IsError())
		return Err(r.Error());
	return Ok(folder);
}

Result<bool, String> WorldCatalog::Update(const String &folder, const Option<String> &name, const NodePtr &settings) const {
	const String path = Str(fs::path(m_dir.CStr()) / folder.CStr() / "world.json");
	Result<NodePtr, String> read = json::ReadFile(path);
	if (read.IsError())
		return Err(read.Error());
	NodePtr world = read.Unwrap();
	if (name.IsSome())
		world->Set("name", json::MakeStr(name.Value()));
	if (settings)
		world->Set("settings", settings->Clone());
	return json::WriteFile(path, world);
}

Result<bool, String> WorldCatalog::Remove(const String &folder) const {
	std::error_code ec;
	fs::remove_all(fs::path(m_dir.CStr()) / folder.CStr(), ec);
	if (ec)
		return Err(String(ec.message().c_str()));
	return Ok(true);
}

Result<String, String> WorldCatalog::Backup(const String &folder) const {
	const fs::path src = fs::path(m_dir.CStr()) / folder.CStr();
	std::string stamp(NowIso().CStr());
	std::replace(stamp.begin(), stamp.end(), ':', '-');
	const fs::path dst = fs::path(m_dir.CStr()).parent_path() / "backups" / (std::string(folder.CStr()) + "-" + stamp);
	std::error_code ec;
	fs::create_directories(dst.parent_path(), ec);
	fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
	if (ec)
		return Err(String(ec.message().c_str()));
	return Ok(Str(dst));
}

Result<String, String> WorldCatalog::Duplicate(const String &folder) const {
	Option<WorldEntry> e = Read(folder, {});
	if (e.IsNone())
		return Err(String("monde introuvable"));
	const String copyName = e.Value().name + String(" (copie)");
	const String target = FreeFolder(copyName);
	std::error_code ec;
	fs::copy(fs::path(m_dir.CStr()) / folder.CStr(), fs::path(m_dir.CStr()) / target.CStr(), fs::copy_options::recursive, ec);
	if (ec)
		return Err(String(ec.message().c_str()));
	(void)Update(target, Some(copyName), NodePtr());
	return Ok(target);
}

// ── Mods ─────────────────────────────────────────────────────────────────────

bool ModEntry::UpdateAvailable() const {
	return installed && available.IsSome() && CompareVersions(available.Value().version, info.version) > 0;
}

String ModEntry::Category() const { return info.category.IsEmpty() ? String("gameplay") : info.category; }

const std::vector<std::pair<String, String>> &ModCatalog::Categories() {
	static const std::vector<std::pair<String, String>> list{
		{"blocks", "Nouveaux blocs"},		  {"items", "Objets et équipements"},		{"creatures", "Créatures"},
		{"worldgen", "Biomes et génération"}, {"building", "Construction"},			{"tech", "Technologie et automatisation"},
		{"interface", "Interface et ergonomie"}, {"graphics", "Graphismes et effets visuels"}, {"audio", "Audio"},
		{"gameplay", "Gameplay"},			  {"admin", "Outils d'administration"},
	};
	return list;
}

String ModCatalog::CategoryName(const String &id) {
	for (const auto &[key, name] : Categories())
		if (key == id)
			return name;
	return id;
}

void ModCatalog::Refresh() {
	m_entries.clear();
	for (const voxel::ModInfo &m : voxel::ModLoader::Discover(m_modDirs)) {
		ModEntry e;
		e.info = m;
		e.installed = true;
		e.builtin = m.id == "core";
		m_entries.push_back(std::move(e));
	}
	for (const voxel::ModInfo &m : voxel::ModLoader::Discover({m_repository})) {
		auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const ModEntry &e) { return e.info.id == m.id; });
		if (it != m_entries.end()) {
			it->available = Some(m);
			continue;
		}
		ModEntry e;
		e.info = m;
		e.available = Some(m);
		m_entries.push_back(std::move(e));
	}
	std::sort(m_entries.begin(), m_entries.end(), [](const ModEntry &a, const ModEntry &b) {
		if (a.builtin != b.builtin)
			return a.builtin;
		return a.info.name < b.info.name;
	});
}

const ModEntry *ModCatalog::Find(const String &id) const {
	for (const ModEntry &e : m_entries)
		if (e.info.id == id)
			return &e;
	return nullptr;
}

std::vector<voxel::ModInfo> ModCatalog::Installed() const {
	std::vector<voxel::ModInfo> out;
	for (const ModEntry &e : m_entries)
		if (e.installed)
			out.push_back(e.info);
	return out;
}

Result<bool, String> ModCatalog::Install(const String &id) {
	const ModEntry *e = Find(id);
	if (!e || e->available.IsNone())
		return Err(String::Format("« %s » n'est pas dans le dépôt", id.CStr()));
	if (m_modDirs.empty())
		return Err(String("aucun dossier de mods"));
	const fs::path src(e->available.Value().path.CStr());
	const fs::path dst = e->installed ? fs::path(e->info.path.CStr()) : fs::path(m_modDirs.front().CStr()) / src.filename();
	std::error_code ec;
	if (e->installed)
		fs::remove_all(dst, ec);
	fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
	if (ec)
		return Err(String(ec.message().c_str()));
	Refresh();
	return Ok(true);
}

Result<bool, String> ModCatalog::Uninstall(const String &id) {
	const ModEntry *e = Find(id);
	if (!e || !e->installed)
		return Err(String::Format("« %s » n'est pas installé", id.CStr()));
	if (e->builtin)
		return Err(String("le contenu de base ne peut pas être désinstallé"));
	if (e->available.IsNone())
		return Err(String("absent du dépôt : il ne pourrait pas être réinstallé"));
	std::error_code ec;
	fs::remove_all(fs::path(e->info.path.CStr()), ec);
	if (ec)
		return Err(String(ec.message().c_str()));
	Refresh();
	return Ok(true);
}

// ── Formats ──────────────────────────────────────────────────────────────────

String FormatDate(const String &iso) {
	if (iso.GetSize() < 16)
		return iso.IsEmpty() ? String("—") : iso;
	// AAAA-MM-JJTHH:MM
	return iso.Substr(8, 2) + String("/") + iso.Substr(5, 2) + String("/") + iso.Substr(0, 4) + String(" ") + iso.Substr(11, 5);
}

String FormatDuration(double seconds) {
	const long s = long(seconds);
	if (s < 60)
		return String::Format("%ld s", s);
	if (s < 3600)
		return String::Format("%ld min", s / 60);
	return String::Format("%ld h %02ld min", s / 3600, (s / 60) % 60);
}

String FormatBytes(uint64_t bytes) {
	if (bytes < 1024)
		return String::Format("%d o", int(bytes));
	if (bytes < 1024 * 1024)
		return String::Format("%.1f Ko", double(bytes) / 1024.0);
	return String::Format("%.1f Mo", double(bytes) / (1024.0 * 1024.0));
}

String GameModeName(const String &id) {
	if (id == "creative")
		return String("Créatif");
	if (id == "adventure")
		return String("Aventure");
	if (id == "spectator")
		return String("Spectateur");
	return String("Survie");
}

String DifficultyName(const String &id) {
	if (id == "peaceful")
		return String("Paisible");
	if (id == "easy")
		return String("Facile");
	if (id == "hard")
		return String("Difficile");
	return String("Normale");
}

} // namespace voxelgame
