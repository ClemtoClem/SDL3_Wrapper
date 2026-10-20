// Définitions de voxel/mods.hpp
#include <voxel/json.hpp>
#include <voxel/mods.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace voxel {

namespace fs = std::filesystem;

namespace {

String Str(const fs::path &p) { return String(p.string().c_str()); }

/// Fichiers d'un dossier (récursif), triés : chargement déterministe.
std::vector<fs::path> Files(const fs::path &root, const std::vector<std::string> &extensions) {
	std::vector<fs::path> out;
	std::error_code ec;
	if (!fs::is_directory(root, ec))
		return out;
	for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
		if (ec)
			break;
		if (!it->is_regular_file())
			continue;
		std::string ext = it->path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
			out.push_back(it->path());
	}
	std::sort(out.begin(), out.end());
	return out;
}

String Stem(const fs::path &p) { return String(p.stem().string().c_str()); }

} // namespace

std::vector<ModInfo> ModLoader::Discover(const std::vector<String> &directories) {
	std::vector<ModInfo> mods;
	for (const String &dir : directories) {
		std::error_code ec;
		std::vector<fs::path> entries;
		for (const auto &entry : fs::directory_iterator(dir.CStr(), ec))
			if (entry.is_directory())
				entries.push_back(entry.path());
		std::sort(entries.begin(), entries.end());
		for (const fs::path &path : entries) {
			ModInfo mod;
			mod.path = Str(path);
			mod.id = String(path.filename().string().c_str());
			const fs::path manifest = path / "mod.json";
			if (fs::exists(manifest)) {
				Result<NodePtr, String> m = json::ReadFile(Str(manifest));
				if (m.IsOk()) {
					mod.id = json::Str(m.Value(), "id", mod.id);
					mod.name = json::Str(m.Value(), "name", mod.id);
					mod.version = json::Str(m.Value(), "version", "1.0");
					mod.description = json::Str(m.Value(), "description");
					mod.dependencies = json::StrList(m.Value(), "dependencies");
					mod.optionalDependencies = json::StrList(m.Value(), "optionalDependencies");
					mod.incompatibilities = json::StrList(m.Value(), "incompatibilities");
					mod.author = json::Str(m.Value(), "author");
					mod.category = json::Str(m.Value(), "category");
					mod.homepage = json::Str(m.Value(), "homepage");
					mod.gameVersion = json::Str(m.Value(), "gameVersion");
					mod.tags = json::StrList(m.Value(), "tags");
					if (const String icon = json::Str(m.Value(), "icon"); !icon.IsEmpty())
						mod.icon = Str(fs::absolute(path / icon.CStr()));
					for (const String &shot : json::StrList(m.Value(), "screenshots"))
						mod.screenshots.push_back(Str(fs::absolute(path / shot.CStr())));
					mod.manifest = m.Value();
				} else {
					mod.description = m.Error(); // signalé au chargement
					mod.enabled = false;
				}
			}
			if (mod.name.IsEmpty())
				mod.name = mod.id;
			if (mod.version.IsEmpty())
				mod.version = "1.0";
			// Scripts : init.script d'abord, puis scripts/ (trié).
			if (fs::exists(path / "init.script"))
				mod.scripts.push_back(Str(path / "init.script"));
			for (const fs::path &s : Files(path / "scripts", {".script"}))
				mod.scripts.push_back(Str(s));
			mods.push_back(std::move(mod));
		}
	}
	return mods;
}

std::vector<ModInfo> ModLoader::Order(std::vector<ModInfo> mods, std::vector<String> &errors, const std::vector<String> &preferred) {
	std::map<std::string, size_t> byId;
	for (size_t i = 0; i < mods.size(); ++i) {
		if (!mods[i].enabled) {
			errors.push_back(String::Format("mod « %s » : mod.json illisible (%s)", mods[i].id.CStr(), mods[i].description.CStr()));
			continue;
		}
		if (byId.contains(mods[i].id.CStr())) {
			errors.push_back(String::Format("mod « %s » présent deux fois : %s ignoré", mods[i].id.CStr(), mods[i].path.CStr()));
			mods[i].enabled = false;
			continue;
		}
		byId[mods[i].id.CStr()] = i;
	}
	// Tri topologique (DFS) ; un cycle ou une dépendance absente écarte le mod.
	std::vector<ModInfo> ordered;
	std::map<std::string, int> state; // 0 : à faire, 1 : en cours, 2 : fait, 3 : écarté
	std::function<bool(const std::string &, std::vector<std::string> &)> visit = [&](const std::string &id,
																					 std::vector<std::string> &chain) {
		if (state[id] == 2)
			return true;
		if (state[id] == 3)
			return false;
		ModInfo &mod = mods[byId[id]];
		if (state[id] == 1) {
			std::string cycle;
			for (const std::string &c : chain)
				cycle += c + " → ";
			errors.push_back(String::Format("dépendance circulaire : %s%s", cycle.c_str(), id.c_str()));
			return false;
		}
		state[id] = 1;
		chain.push_back(id);
		bool ok = true;
		for (const String &dep : mod.dependencies) {
			if (!byId.contains(dep.CStr())) {
				errors.push_back(String::Format("mod « %s » : dépendance manquante « %s »", id.c_str(), dep.CStr()));
				ok = false;
			} else if (!visit(dep.CStr(), chain)) {
				ok = false;
			}
		}
		for (const String &dep : mod.optionalDependencies)
			if (byId.contains(dep.CStr()))
				(void)visit(dep.CStr(), chain);
		chain.pop_back();
		state[id] = ok ? 2 : 3;
		if (ok)
			ordered.push_back(mod);
		return ok;
	};
	std::vector<std::string> ids;
	for (const String &id : preferred)
		if (byId.contains(id.CStr()))
			ids.push_back(id.CStr());
	for (const auto &[id, index] : byId)
		if (std::find(ids.begin(), ids.end(), id) == ids.end())
			ids.push_back(id);
	for (const std::string &id : ids) {
		std::vector<std::string> chain;
		(void)visit(id, chain);
	}
	return ordered;
}

void ModLoader::LoadDefinitions(ContentRegistry &content, ModInfo &mod, ModReport &report) {
	const fs::path root(mod.path.CStr());
	ContentRegistry::LoadContext ctx{mod.id, mod.path, String()};
	for (const fs::path &file : Files(root, {".json"})) {
		if (file.filename() == "mod.json")
			continue;
		Result<std::vector<String>, String> ids = content.LoadFile(Str(file), ctx);
		if (ids.IsError())
			report.errors.push_back(String::Format("[%s] %s", mod.id.CStr(), ids.Error().CStr()));
		else
			mod.definitions += int(ids.Value().size());
	}
	// Ressources déclarées par leur seule présence (si aucune définition JSON
	// ne porte déjà cet id).
	auto declare = [&](const String &type, const fs::path &file, const char *field, const String &name = String()) {
		const String id = mod.id + String(":") + (name.IsEmpty() ? Stem(file) : name);
		NodePtr def = dataio::Node::MakeObject();
		def->Set("type", json::MakeStr(type));
		def->Set("id", json::MakeStr(id));
		def->Set(field, json::MakeStr(Str(fs::absolute(file)))); // déjà résolu : pas relatif au mod
		ContentRegistry::LoadContext fileCtx{mod.id, mod.path, Str(file)};
		if (Result<String, String> r = content.Load(def, fileCtx); r.IsOk())
			++mod.definitions;
		else
			report.errors.push_back(r.Error());
	};
	// Textures et sons rangés en sous-dossiers : id complet « mod:dossier/nom »
	// ET, si le nom est libre, raccourci « mod:nom » (les blocs l'emportent
	// sur les objets, eux-mêmes sur le reste).
	auto relative = [](const fs::path &file, const fs::path &base, bool lower) {
		std::string rel = file.lexically_relative(base).replace_extension().generic_string();
		if (lower)
			std::transform(rel.begin(), rel.end(), rel.begin(), ::tolower);
		return String(rel.c_str());
	};
	auto priority = [](const fs::path &file, const fs::path &base) {
		const std::string top = file.lexically_relative(base).begin()->string();
		return top == file.filename().string() ? 0 : top == "blocks" ? 1 : top == "items" ? 2 : top == "entities" ? 3 : 4;
	};
	auto ordered = [&](std::vector<fs::path> files, const fs::path &base) {
		std::stable_sort(files.begin(), files.end(),
						 [&](const fs::path &a, const fs::path &b) { return priority(a, base) < priority(b, base); });
		return files;
	};
	const fs::path textureDir = root / "textures";
	for (const fs::path &png : ordered(Files(textureDir, {".png", ".bmp", ".jpg", ".tga"}), textureDir)) {
		const String rel = relative(png, textureDir, false);
		for (const String &name : {rel, Stem(png)})
			if (!content.FindTexture(mod.id + String(":") + name))
				declare("texture", png, "path", name);
	}
	const fs::path soundDir = root / "sounds";
	std::map<std::string, std::vector<fs::path>> variants; // « pas_1 », « pas_2 »… → « pas »
	for (const fs::path &snd : ordered(Files(soundDir, {".wav", ".ogg", ".mp3"}), soundDir)) {
		const String rel = relative(snd, soundDir, true);
		for (const String &name : {rel, Stem(snd).ToLower()})
			if (!content.FindSound(mod.id + String(":") + name))
				declare("sound", snd, "path", name);
		std::string base(rel.CStr());
		size_t cut = base.size();
		while (cut > 0 && std::isdigit(uint8_t(base[cut - 1])))
			--cut;
		if (cut < base.size() && cut > 1 && (base[cut - 1] == '_' || base[cut - 1] == '.'))
			variants[base.substr(0, cut - 1)].push_back(snd);
	}
	for (const auto &[base, files] : variants) {
		if (files.size() < 2)
			continue;
		const String rel(base.c_str());
		const String stem(fs::path(base).filename().string().c_str());
		for (const String &name : {rel, stem}) {
			const String id = mod.id + String(":") + name;
			if (content.FindSound(id))
				continue;
			NodePtr def = dataio::Node::MakeObject();
			def->Set("type", json::MakeStr("sound"));
			def->Set("id", json::MakeStr(id));
			NodePtr paths = dataio::Node::MakeArray();
			for (const fs::path &f : files)
				paths->Push(json::MakeStr(Str(fs::absolute(f))));
			def->Set("paths", paths);
			def->Set("pitchVariation", json::MakeNum(0.06));
			if (Result<String, String> r = content.Load(def, ContentRegistry::LoadContext{mod.id, mod.path, Str(files[0])}); r.IsOk())
				++mod.definitions;
			else
				report.errors.push_back(r.Error());
		}
	}
	for (const fs::path &model : Files(root / "models", {".obj", ".b3d", ".b3b"}))
		if (!content.FindModel(mod.id + String(":") + Stem(model)))
			declare("model", model, "path");
	for (const fs::path &vert : Files(root / "shaders", {".vert"})) {
		fs::path frag = vert;
		frag.replace_extension(".frag");
		const String id = mod.id + String(":") + Stem(vert);
		if (!fs::exists(frag) || content.FindShader(id))
			continue;
		NodePtr def = dataio::Node::MakeObject();
		def->Set("type", json::MakeStr("shader"));
		def->Set("id", json::MakeStr(id));
		def->Set("vertex", json::MakeStr(Str(fs::absolute(vert))));
		def->Set("fragment", json::MakeStr(Str(fs::absolute(frag))));
		if (content.Load(def, ctx).IsOk())
			++mod.definitions;
	}
}

std::vector<std::pair<String, String>> ModLoader::Conflicts(const std::vector<ModInfo> &mods) {
	std::vector<std::pair<String, String>> out;
	for (const ModInfo &a : mods)
		for (const String &other : a.incompatibilities)
			for (const ModInfo &b : mods)
				if (b.id == other && std::none_of(out.begin(), out.end(), [&](const auto &p) { return p.first == b.id && p.second == a.id; }))
					out.push_back({a.id, b.id});
	return out;
}

ModReport ModLoader::LoadAll(ContentRegistry &content, const std::vector<String> &directories, const std::vector<String> &disabled,
							 const std::vector<String> &preferred) {
	ModReport report;
	std::vector<ModInfo> found = Discover(directories);
	std::erase_if(found, [&](const ModInfo &m) { return std::find(disabled.begin(), disabled.end(), m.id) != disabled.end(); });
	for (const auto &[a, b] : Conflicts(found))
		report.warnings.push_back(String::Format("mods incompatibles : « %s » et « %s »", a.CStr(), b.CStr()));
	report.mods = Order(std::move(found), report.errors, preferred);
	for (ModInfo &mod : report.mods)
		LoadDefinitions(content, mod, report);
	return report;
}

} // namespace voxel
