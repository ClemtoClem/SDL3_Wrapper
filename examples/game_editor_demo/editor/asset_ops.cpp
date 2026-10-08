// Définitions de asset_ops.hpp
#include "asset_ops.hpp"

#include "../document/project_files.hpp"
#include "../engine/runtime.hpp"

namespace game_editor {

namespace {

bool Exists(const String& path) {
	return sdl3::filesystem::PathInfo(path).IsSome();
}

bool IsDirectory(const String& path) {
	Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(path);
	return info.IsSome() && info.Unwrap().type == sdl3::PathType::DIRECTORY;
}

String ParentOf(const String& path) {
	size_t slash = String::NPOS;
	for (size_t i = 0; i < path.size(); ++i)
		if (path[i] == '/')
			slash = i;
	return slash == String::NPOS ? String() : path.Substr(0, slash);
}

String NameOf(const String& path) {
	size_t slash = String::NPOS;
	for (size_t i = 0; i < path.size(); ++i)
		if (path[i] == '/')
			slash = i;
	return slash == String::NPOS ? path : path.Substr(slash + 1);
}

std::vector<String> ChildrenOf(const String& directory) {
	std::vector<String> names;
	(void)sdl3::filesystem::EnumerateDirectory(directory, [&names](const char*, const char* file) {
		names.emplace_back(file);
		return true;
	});
	return names;
}

/// Suppression récursive (SDL ne retire qu'un dossier VIDE).
bool RemoveTree(const String& path) {
	if (IsDirectory(path))
		for (const String& child : ChildrenOf(path))
			if (!RemoveTree(path + String("/") + child))
				return false;
	return sdl3::filesystem::Remove(path);
}

/// Copie récursive d'un fichier ou d'un dossier.
bool CopyTree(const String& from, const String& to) {
	if (!IsDirectory(from))
		return sdl3::filesystem::CopyFile(from, to);
	if (!sdl3::filesystem::CreateDirectory(to))
		return false;
	for (const String& child : ChildrenOf(from))
		if (!CopyTree(from + String("/") + child, to + String("/") + child))
			return false;
	return true;
}

String Prefixed(const String& location, const char* prefix) {
	const String head = String(prefix) + String("/");
	return location.StartsWith(head.View()) ? location.Substr(head.size()) : String();
}

} // namespace

// ── AssetOpReport ────────────────────────────────────────────────────────────

String AssetOpReport::Summary(const char* verb) const {
	String text = String::Format("%d élément%s %s", done, done > 1 ? "s" : "", verb);
	if (!errors.empty())
		text.Concat(String::Format(" — %d refusé%s : %s", int(errors.size()),
								   errors.size() > 1 ? "s" : "", errors.front().CStr()));
	return text;
}

// ── AssetOperations ──────────────────────────────────────────────────────────

String AssetOperations::ExtensionOf(const String& name) {
	if (name.ToLower().EndsWith(files::GAMEPLAY_SUFFIX))
		return name.Substr(name.size() - String(files::GAMEPLAY_SUFFIX).size());
	size_t dot = String::NPOS;
	for (size_t i = 1; i < name.size(); ++i)
		if (name[i] == '.')
			dot = i;
	return dot == String::NPOS ? String() : name.Substr(dot);
}

Option<String> AssetOperations::InvalidName(const String& name) {
	const String clean = name.Trim();
	if (clean.IsEmpty())
		return Some(String("nom vide"));
	if (clean == "." || clean == "..")
		return Some(String("nom réservé"));
	if (clean.StartsWith("."))
		return Some(String("un nom ne commence pas par un point (fichier caché)"));
	for (size_t i = 0; i < clean.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(clean[i]);
		if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
			c == '<' || c == '>' || c == '|')
			return Some(String::Format("caractère interdit « %c »", c < 0x20 ? '?' : char(c)));
	}
	return NONE;
}

String AssetOperations::FreeCopyName(const String& folder, const String& name) {
	const String ext = IsDirectory(folder + String("/") + name) ? String() : ExtensionOf(name);
	const String stem = name.Substr(0, name.size() - ext.size());
	for (int n = 1; n < 10000; ++n) {
		const String candidate =
			n == 1 ? String::Format("%s (copie)%s", stem.CStr(), ext.CStr())
				   : String::Format("%s (copie %d)%s", stem.CStr(), n, ext.CStr());
		if (!Exists(folder + String("/") + candidate))
			return candidate;
	}
	return name + String(" (copie)");
}

bool AssetOperations::InsideProject(const String& path) const {
	const String& root = m_model.ProjectDirectory();
	return !root.IsEmpty() && (path == root || path.StartsWith(root + String("/")));
}

bool AssetOperations::IsProjectStructure(const String& path) const {
	const String& root = m_model.ProjectDirectory();
	if (root.IsEmpty())
		return false;
	return path == root || path == m_model.SceneFolder() || path == m_model.ScriptFolder() ||
		   path == root + String("/") + files::ASSETS_DIR || path == m_runtime.ProjectPath() ||
		   (path.StartsWith(root + String("/")) && path.ToLower().EndsWith(".json") &&
			ParentOf(path) == root);
}

bool AssetOperations::IsWritableFolder(const String& location) const {
	const String folder = m_model.DiskFolderOf(location);
	return !folder.IsEmpty() && InsideProject(folder) && IsDirectory(folder);
}

Option<String> AssetOperations::WhyLocked(const AssetEntry& entry, const char* operation) const {
	const String op(operation);
	if (m_runtime.IsPlaying())
		return Some(String("impossible pendant une partie"));
	if (entry.managed) {
		if (op == "move")
			return Some(String("élément du projet : le projet le range lui-même dans son dossier"));
		const bool gameplay =
			entry.location.StartsWith(String(AssetBrowserModel::SCRIPTS) + String("/@"));
		if (gameplay && op == "rename")
			return Some(String("le script de jeu porte le nom de sa scène — renommez la scène"));
		if (gameplay && op == "duplicate")
			return Some(String("dupliquez la scène : son script de jeu suit"));
		return NONE;
	}
	if (entry.location.StartsWith(AssetBrowserModel::ROOT))
		return Some(String("dossier du projet"));
	if (!InsideProject(entry.location))
		return Some(
			String("ressource partagée : lecture seule (seul le dossier du projet se modifie)"));
	if (IsProjectStructure(entry.location))
		return Some(String("structure du projet (manifeste, scenes/, scripts/, assets/)"));
	return NONE;
}

Result<String, String> AssetOperations::CreateFolder(const String& parent, const String& name) {
	if (!IsWritableFolder(parent))
		return Err(String("ce dossier ne se modifie pas (seul le dossier du projet le peut)"));
	if (Option<String> invalid = InvalidName(name); invalid.IsSome())
		return Err(invalid.Unwrap());
	const String path = m_model.DiskFolderOf(parent) + String("/") + name.Trim();
	if (Exists(path))
		return Err(String::Format("« %s » existe déjà", name.Trim().CStr()));
	if (!sdl3::filesystem::CreateDirectory(path))
		return Err(String::Format("création refusée par le système (%s)", path.CStr()));
	return Ok(m_model.Canonical(path));
}

Result<String, String> AssetOperations::Rename(const AssetEntry& entry, const String& wanted) {
	if (Option<String> locked = WhyLocked(entry, "rename"); locked.IsSome())
		return Err(locked.Unwrap());
	if (Option<String> invalid = InvalidName(wanted); invalid.IsSome())
		return Err(invalid.Unwrap());
	String name = wanted.Trim();
	if (entry.managed) {
		if (entry.kind == AssetKind::SCENE || entry.kind == AssetKind::OBJECT) {
			const char *folder = entry.kind == AssetKind::OBJECT ? AssetBrowserModel::OBJECTS : AssetBrowserModel::SCENES;
			if (name.ToLower().EndsWith(".scene"))
				name = name.Substr(0, name.size() - 6);
			auto renamed =
				m_runtime.RenameScene(Prefixed(entry.location, folder), name);
			if (renamed.IsError())
				return Err(renamed.Error());
			return Ok(String(folder) + String("/") + renamed.Value());
		}
		auto renamed =
			m_runtime.RenameScript(Prefixed(entry.location, AssetBrowserModel::SCRIPTS), name);
		if (renamed.IsError())
			return Err(renamed.Error());
		return Ok(String(AssetBrowserModel::SCRIPTS) + String("/") + renamed.Value());
	}
	// Fichier : l'extension reste si le nouveau nom n'en donne pas.
	if (entry.kind != AssetKind::FOLDER && ExtensionOf(name).IsEmpty())
		name.Concat(ExtensionOf(entry.name));
	if (name == entry.name)
		return Ok(entry.location);
	const String folder = ParentOf(entry.location);
	const String target = folder + String("/") + name;
	if (Exists(target))
		return Err(String::Format("« %s » existe déjà", name.CStr()));
	if (!sdl3::filesystem::Rename(entry.location, target))
		return Err(String("renommage refusé par le système"));
	return Ok(m_model.Canonical(target));
}

AssetOpReport AssetOperations::Delete(const std::vector<AssetEntry>& entries) {
	AssetOpReport report;
	for (const AssetEntry& entry : entries) {
		if (Option<String> locked = WhyLocked(entry, "delete"); locked.IsSome()) {
			report.errors.push_back(
				String::Format("%s : %s", entry.name.CStr(), locked.Unwrap().CStr()));
			continue;
		}
		if (entry.managed) {
			const String scripts = String(AssetBrowserModel::SCRIPTS) + String("/@");
			Result<bool, String> removed = Ok(true);
			if (entry.kind == AssetKind::SCENE || entry.kind == AssetKind::OBJECT) {
				const char *folder = entry.kind == AssetKind::OBJECT ? AssetBrowserModel::OBJECTS : AssetBrowserModel::SCENES;
				removed =
					m_runtime.RemoveScene(Prefixed(entry.location, folder));
			} else if (entry.location.StartsWith(scripts.View())) {
				// Script de jeu : la scène reste, sans script.
				(void)m_runtime.SetGameplayScript(entry.location.Substr(scripts.size()), String());
			} else {
				auto gone =
					m_runtime.RemoveScript(Prefixed(entry.location, AssetBrowserModel::SCRIPTS));
				if (gone.IsError())
					removed = Err(gone.Error());
			}
			if (removed.IsError()) {
				report.errors.push_back(
					String::Format("%s : %s", entry.name.CStr(), removed.Error().CStr()));
				continue;
			}
			++report.done;
			continue;
		}
		if (!RemoveTree(entry.location)) {
			report.errors.push_back(
				String::Format("%s : suppression refusée par le système", entry.name.CStr()));
			continue;
		}
		++report.done;
	}
	return report;
}

AssetOpReport AssetOperations::Duplicate(const std::vector<AssetEntry>& entries) {
	AssetOpReport report;
	for (const AssetEntry& entry : entries) {
		if (Option<String> locked = WhyLocked(entry, "duplicate"); locked.IsSome()) {
			report.errors.push_back(
				String::Format("%s : %s", entry.name.CStr(), locked.Unwrap().CStr()));
			continue;
		}
		if (entry.managed) {
			if (entry.kind == AssetKind::SCENE || entry.kind == AssetKind::OBJECT) {
				const char *folder = entry.kind == AssetKind::OBJECT ? AssetBrowserModel::OBJECTS : AssetBrowserModel::SCENES;
				auto copy =
					m_runtime.DuplicateScene(Prefixed(entry.location, folder));
				if (copy.IsError()) {
					report.errors.push_back(
						String::Format("%s : %s", entry.name.CStr(), copy.Error().CStr()));
					continue;
				}
				report.created.push_back(String(folder) + String("/") +
										 copy.Value());
			} else {
				auto copy =
					m_runtime.DuplicateScript(Prefixed(entry.location, AssetBrowserModel::SCRIPTS));
				if (copy.IsError()) {
					report.errors.push_back(
						String::Format("%s : %s", entry.name.CStr(), copy.Error().CStr()));
					continue;
				}
				report.created.push_back(String(AssetBrowserModel::SCRIPTS) + String("/") +
										 copy.Value());
			}
			++report.done;
			continue;
		}
		const String folder = ParentOf(entry.location);
		const String target = folder + String("/") + FreeCopyName(folder, entry.name);
		if (!CopyTree(entry.location, target)) {
			(void)RemoveTree(target); // pas de copie à moitié faite
			report.errors.push_back(
				String::Format("%s : copie refusée par le système", entry.name.CStr()));
			continue;
		}
		report.created.push_back(m_model.Canonical(target));
		++report.done;
	}
	return report;
}

AssetOpReport AssetOperations::Move(const std::vector<AssetEntry>& entries,
									const String& destination) {
	AssetOpReport report;
	const String folder = m_model.DiskFolderOf(destination);
	if (!IsWritableFolder(destination)) {
		report.errors.push_back(
			String("destination en lecture seule (seul le dossier du projet se modifie)"));
		return report;
	}
	for (const AssetEntry& entry : entries) {
		if (Option<String> locked = WhyLocked(entry, "move"); locked.IsSome()) {
			report.errors.push_back(
				String::Format("%s : %s", entry.name.CStr(), locked.Unwrap().CStr()));
			continue;
		}
		if (ParentOf(entry.location) == folder)
			continue; // déjà là : rien à faire, rien à signaler
		if (folder == entry.location || folder.StartsWith(entry.location + String("/"))) {
			report.errors.push_back(
				String::Format("%s : un dossier ne se range pas en lui-même", entry.name.CStr()));
			continue;
		}
		const String target = folder + String("/") + NameOf(entry.location);
		if (Exists(target)) {
			report.errors.push_back(
				String::Format("%s : existe déjà dans la destination", entry.name.CStr()));
			continue;
		}
		if (!sdl3::filesystem::Rename(entry.location, target)) {
			report.errors.push_back(
				String::Format("%s : déplacement refusé par le système", entry.name.CStr()));
			continue;
		}
		report.created.push_back(m_model.Canonical(target));
		++report.done;
	}
	return report;
}

} // namespace game_editor
