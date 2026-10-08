// Définitions de project_files.hpp
#include "project_files.hpp"
#include "objects.hpp"

namespace game_editor::files {

String Join(const String &directory, const String &name) {
	if (directory.IsEmpty() || name.StartsWith("/"))
		return name;
	return directory.EndsWith("/") ? directory + name : directory + String("/") + name;
}

String DirectoryOf(const String &path) {
	const size_t slash = path.Rfind('/');
	return slash == String::NPOS ? String(".") : (slash == 0 ? String("/") : path.Substr(0, slash));
}

String NormalizePath(const String &path) {
	const bool absolute = path.StartsWith("/");
	std::vector<String> parts;
	for (const String &part : path.Split('/')) {
		if (part.IsEmpty() || part == ".")
			continue;
		if (part == ".." && !parts.empty() && parts.back() != "..")
			parts.pop_back();
		else if (part != ".." || !absolute)
			parts.push_back(part);
	}
	String out = absolute ? String("/") : String();
	for (size_t i = 0; i < parts.size(); ++i)
		out += i == 0 ? parts[i] : String("/") + parts[i];
	return out.IsEmpty() ? String(".") : out;
}

String BaseName(const String &path) {
	const size_t slash = path.Rfind('/');
	return slash == String::NPOS ? path : path.Substr(slash + 1);
}

String Stem(const String &path) {
	const String base = BaseName(path);
	const size_t dot = base.Rfind('.');
	return dot == String::NPOS || dot == 0 ? base : base.Substr(0, dot);
}

bool IsDirectory(const String &path) {
	Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(path);
	return info.IsSome() && info.Value().type == sdl3::PathType::DIRECTORY;
}

bool IsFile(const String &path) {
	Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(path);
	return info.IsSome() && info.Value().type == sdl3::PathType::FILE;
}

String SafeFileName(const String &name) {
	String out;
	for (size_t i = 0; i < name.GetSize(); ++i) {
		const unsigned char c = static_cast<unsigned char>(name[i]);
		const bool reserved = c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
							  c == '<' || c == '>' || c == '|';
		out.PushBack(reserved ? '_' : char(c));
	}
	out = out.Trim();
	while (out.StartsWith("."))
		out = out.Substr(1);
	return out.IsEmpty() ? String("sans_nom") : out;
}

String RelativeTo(const String &directory, const String &path) {
	const String prefix = directory.EndsWith("/") ? directory : directory + String("/");
	return path.StartsWith(prefix) ? path.Substr(prefix.GetSize()) : path;
}

Result<bool, String> WriteText(const String &path, const String &text) {
	(void)sdl3::filesystem::CreateDirectory(DirectoryOf(path));
	if (!sdl3::WriteFile(path, text.CStr(), text.GetSize()))
		return Err(String::Format("écriture impossible : %s", path.CStr()));
	return Ok(true);
}

Result<data::NodePtr, String> ReadJson(const String &path) {
	auto text = ReadText(path);
	if (text.IsError())
		return Err(text.Error());
	data::JsonDocument document;
	if (auto error = document.DecodeStr(text.Value()); error.IsSome())
		return Err(String::Format("%s : %s", path.CStr(), error.Unwrap().Format().CStr()));
	return Ok(document.GetRoot());
}

Result<bool, String> WriteJson(const String &path, const data::NodePtr &root) {
	data::JsonDocument document;
	document.SetRoot(root);
	return WriteText(path, document.EncodeStr());
}

String FormatOf(const String &path) {
	auto root = ReadJson(path);
	return root.IsOk() ? json::Str(root.Value()->Get("format")) : String();
}

data::NodePtr SceneFileJson(const SceneDesc &scene, const String &gameplayFile) {
	auto root = scene.ToJson();
	root->Set("format", data::Node::MakeString(SCENE_FORMAT));
	root->Set("version", data::Node::MakeInt(SCENE_VERSION));
	if (!gameplayFile.IsEmpty()) {
		root->Set("gameplay_script", data::Node::MakeString(String()));
		root->Set("gameplay_script_file", data::Node::MakeString(gameplayFile));
	}
	return root;
}

Result<bool, String> SaveSceneFile(const SceneDesc &scene, const String &path) {
	return WriteJson(path, SceneFileJson(scene, String()));
}

Result<SceneDesc, String> LoadSceneFile(const String &path) {
	auto root = ReadJson(path);
	if (root.IsError())
		return Err(root.Error());
	const data::NodePtr &node = root.Value();
	const String format = json::Str(node->Get("format"));
	if (format == PACKED_FORMAT)
		return Err(String::Format("%s : sous-arbre réutilisable (à instancier dans une scène, pas à ouvrir "
								  "comme scène)",
								  path.CStr()));
	if (format != SCENE_FORMAT)
		return Err(String::Format("%s : fichier de scène attendu (format « %s »)", path.CStr(), SCENE_FORMAT));
	if (json::Int(node->Get("version"), SCENE_VERSION) > SCENE_VERSION)
		return Err(String::Format("%s : version de scène trop récente", path.CStr()));
	auto scene = SceneDesc::FromJson(node);
	if (scene.IsError())
		return Err(String::Format("%s : %s", path.CStr(), scene.Error().CStr()));
	SceneDesc result = std::move(scene).Unwrap();
	if (const String file = json::Str(node->Get("gameplay_script_file")); !file.IsEmpty()) {
		auto source = ReadText(Join(DirectoryOf(path), file));
		if (source.IsError())
			return Err(String::Format("%s : script de jeu illisible (%s)", path.CStr(), source.Error().CStr()));
		result.gameplayScript = source.Value();
	}
	return Ok(std::move(result));
}

data::NodePtr ObjectFileJson(const SceneDesc &object) {
	auto root = data::Node::MakeObject();
	root->Set("format", data::Node::MakeString(OBJECT_FORMAT));
	root->Set("version", data::Node::MakeInt(OBJECT_VERSION));
	root->Set("name", data::Node::MakeString(object.name));
	root->Set("description", data::Node::MakeString(object.description));
	root->Set("tree", objects::StripGenerated(object.tree).ToJson());
	return root;
}

Result<bool, String> SaveObjectFile(const SceneDesc &object, const String &path) {
	return WriteJson(path, ObjectFileJson(object));
}

Result<SceneDesc, String> LoadObjectFile(const String &path) {
	auto root = ReadJson(path);
	if (root.IsError())
		return Err(root.Error());
	const data::NodePtr &node = root.Value();
	if (json::Str(node->Get("format")) != OBJECT_FORMAT)
		return Err(String::Format("%s : fichier d'objet attendu (format « %s »)", path.CStr(), OBJECT_FORMAT));
	if (json::Int(node->Get("version"), OBJECT_VERSION) > OBJECT_VERSION)
		return Err(String::Format("%s : version d'objet trop récente", path.CStr()));
	SceneDesc object;
	object.kind = SceneKind::OBJECT;
	object.SetName(json::Str(node->Get("name"), Stem(path).CStr()));
	object.description = json::Str(node->Get("description"));
	if (auto treeJson = node->Get("tree"); treeJson && treeJson->IsObject()) {
		auto tree = scene::NodeTree::FromJson(treeJson);
		if (tree.IsError())
			return Err(String::Format("%s : %s", path.CStr(), tree.Error().CStr()));
		object.tree = std::move(tree).Unwrap();
		object.SetName(object.name); // la racine suit le nom de l'objet
	}
	return Ok(std::move(object));
}

Result<bool, String> SaveScriptFile(const String &source, const String &path) {
	return WriteText(path, source);
}

Result<ScriptAsset, String> LoadScriptFile(const String &path) {
	auto source = ReadText(path);
	if (source.IsError())
		return Err(source.Error());
	ScriptAsset asset;
	asset.name = Stem(path);
	asset.source = source.Value();
	return Ok(std::move(asset));
}

String ResolveManifest(const String &path) {
	String clean = path;
	while (clean.GetSize() > 1 && clean.EndsWith("/"))
		clean = clean.Substr(0, clean.GetSize() - 1);
	if (IsFile(clean))
		return clean;
	if (!IsDirectory(clean))
		return String();
	const String named = Join(clean, BaseName(clean) + String(".json"));
	if (IsFile(named))
		return named;
	std::vector<String> manifests = sdl3::filesystem::Glob(clean, "*.json", true);
	return manifests.size() == 1 ? Join(clean, manifests.front()) : String();
}

std::vector<ProjectEntry> ListProjects(const String &root) {
	std::vector<ProjectEntry> projects;
	if (!IsDirectory(root))
		return projects;
	(void)sdl3::filesystem::EnumerateDirectory(root, [&](const char *, const char *name) {
		const String directory = Join(root, String(name));
		if (!IsDirectory(directory))
			return true;
		const String manifest = ResolveManifest(directory);
		if (manifest.IsEmpty())
			return true;
		auto json = ReadJson(manifest);
		if (json.IsError())
			return true;
		// `level_editor.project` : manifestes d'avant le renommage de l'éditeur.
		const String format = json::Str(json.Value()->Get("format"));
		if (format != PROJECT_FORMAT && format != "level_editor.project")
			return true;
		projects.push_back({json::Str(json.Value()->Get("name"), name), manifest});
		return true;
	});
	std::sort(projects.begin(), projects.end(),
			  [](const ProjectEntry &a, const ProjectEntry &b) { return a.name < b.name; });
	return projects;
}

Project MakeBlankProject(const String &name) {
	Project project;
	project.name = name;
	SceneDesc scene;
	scene.SetName(String("Scène principale"));
	project.scenes.push_back(std::move(scene));
	project.activeScene = project.scenes.front().name;
	return project;
}

Result<String, String> CreateProjectDirectory(const String &root, const String &name) {
	const String folder = SafeFileName(name);
	const String directory = Join(root, folder);
	if (sdl3::filesystem::PathInfo(directory).IsSome())
		return Err(String::Format("le dossier %s existe déjà", directory.CStr()));
	for (const char *sub : {ASSETS_DIR, SCENES_DIR, SCRIPTS_DIR})
		if (!sdl3::filesystem::CreateDirectory(Join(directory, String(sub))))
			return Err(String::Format("création impossible : %s", Join(directory, String(sub)).CStr()));
	return Ok(Join(directory, folder + String(".json")));
}

Result<Project, String> LoadProject(const String &manifestPath, FileList *files) {
	auto root = ReadJson(manifestPath);
	if (root.IsError())
		return Err(root.Error());
	const data::NodePtr &manifest = root.Value();
	auto scenes = manifest->Get("scenes");
	const bool legacy = scenes && scenes->IsArray() && scenes->GetSize() > 0 && scenes->At(0)->IsObject();
	if (legacy || json::Int(manifest->Get("version"), PROJECT_VERSION) < PROJECT_VERSION)
		return Project::FromJson(manifest);
	if (json::Int(manifest->Get("version"), PROJECT_VERSION) > PROJECT_VERSION)
		return Err(String::Format("%s : version de projet trop récente", manifestPath.CStr()));

	const String directory = DirectoryOf(manifestPath);
	Project project;
	project.name = json::Str(manifest->Get("name"), "Projet sans titre");
	project.activeScene = json::Str(manifest->Get("active_scene"));
	if (scenes && scenes->IsArray()) {
		for (size_t i = 0; i < scenes->GetSize(); ++i) {
			const String path = Join(directory, json::Str(scenes->At(i)));
			auto scene = LoadSceneFile(path);
			if (scene.IsError())
				return Err(scene.Error());
			if (files) {
				files->push_back(path);
				if (auto file = ReadJson(path); file.IsOk())
					if (const String gameplay = json::Str(file.Value()->Get("gameplay_script_file")); !gameplay.IsEmpty())
						files->push_back(NormalizePath(Join(DirectoryOf(path), gameplay)));
			}
			project.scenes.push_back(std::move(scene).Unwrap());
		}
	}
	if (auto objectList = manifest->Get("objects"); objectList && objectList->IsArray()) {
		for (size_t i = 0; i < objectList->GetSize(); ++i) {
			const String path = Join(directory, json::Str(objectList->At(i)));
			auto object = LoadObjectFile(path);
			if (object.IsError())
				return Err(object.Error());
			if (files)
				files->push_back(path);
			project.scenes.push_back(std::move(object).Unwrap());
		}
	}
	if (auto scripts = manifest->Get("scripts"); scripts && scripts->IsArray()) {
		for (size_t i = 0; i < scripts->GetSize(); ++i) {
			const data::NodePtr &entry = scripts->At(i);
			const String path = Join(directory, json::Str(entry->Get("file")));
			auto script = LoadScriptFile(path);
			if (script.IsError())
				return Err(String::Format("script « %s » : %s", json::Str(entry->Get("name")).CStr(),
										  script.Error().CStr()));
			ScriptAsset asset = std::move(script).Unwrap();
			if (const String name = json::Str(entry->Get("name")); !name.IsEmpty())
				asset.name = name;
			asset.description = json::Str(entry->Get("description"));
			project.scripts.push_back(std::move(asset));
			if (files)
				files->push_back(path);
		}
	}
	if (project.SceneCount() == 0)
		return Err(String::Format("%s : le projet ne contient aucune scène", manifestPath.CStr()));
	if (!project.FindScene(project.activeScene))
		project.activeScene = project.SceneNames().front();
	// Contenu des instances d'objets : généré, jamais lu du disque.
	(void)objects::ExpandProject(project);
	return Ok(std::move(project));
}

Result<FileList, String> SaveProject(const Project &project, const String &manifestPath, const FileList &previous) {
	const String directory = DirectoryOf(manifestPath);
	(void)sdl3::filesystem::CreateDirectory(Join(directory, String(ASSETS_DIR)));
	FileList written;

	auto sceneList = data::Node::MakeArray();
	auto objectList = data::Node::MakeArray();
	for (const SceneDesc &scene : project.scenes) {
		if (scene.IsObject()) {
			const String path = Join(directory, String(OBJECTS_DIR) + String("/") + SafeFileName(scene.name) +
														OBJECT_EXTENSION);
			auto saved = SaveObjectFile(scene, path);
			if (saved.IsError())
				return Err(saved.Error());
			written.push_back(path);
			objectList->Push(data::Node::MakeString(RelativeTo(directory, path)));
			continue;
		}
		const String base = SafeFileName(scene.name);
		const String scenePath = Join(directory, String(SCENES_DIR) + String("/") + base + String(".scene"));
		String gameplayRef;
		if (!scene.gameplayScript.Trim().IsEmpty()) {
			gameplayRef = String("../") + SCRIPTS_DIR + String("/") + base + GAMEPLAY_SUFFIX;
			const String gameplayPath = Join(directory, String(SCRIPTS_DIR) + String("/") + base + GAMEPLAY_SUFFIX);
			auto saved = SaveScriptFile(scene.gameplayScript, gameplayPath);
			if (saved.IsError())
				return Err(saved.Error());
			written.push_back(gameplayPath);
		}
		auto saved = WriteJson(scenePath, SceneFileJson(scene, gameplayRef));
		if (saved.IsError())
			return Err(saved.Error());
		written.push_back(scenePath);
		sceneList->Push(data::Node::MakeString(RelativeTo(directory, scenePath)));
	}

	auto scriptList = data::Node::MakeArray();
	for (const ScriptAsset &script : project.scripts) {
		const String path = Join(directory, String(SCRIPTS_DIR) + String("/") + SafeFileName(script.name) + ".script");
		auto saved = SaveScriptFile(script.source, path);
		if (saved.IsError())
			return Err(saved.Error());
		written.push_back(path);
		auto entry = data::Node::MakeObject();
		entry->Set("name", data::Node::MakeString(script.name));
		entry->Set("description", data::Node::MakeString(script.description));
		entry->Set("file", data::Node::MakeString(RelativeTo(directory, path)));
		scriptList->Push(entry);
	}

	auto manifest = data::Node::MakeObject();
	manifest->Set("format", data::Node::MakeString(PROJECT_FORMAT));
	manifest->Set("version", data::Node::MakeInt(PROJECT_VERSION));
	manifest->Set("name", data::Node::MakeString(project.name));
	manifest->Set("active_scene", data::Node::MakeString(project.activeScene));
	manifest->Set("scenes", sceneList);
	manifest->Set("objects", objectList);
	manifest->Set("scripts", scriptList);
	auto saved = WriteJson(manifestPath, manifest);
	if (saved.IsError())
		return Err(saved.Error());

	// Fichiers orphelins : seulement ceux que CE projet avait écrits ou lus.
	// Comparaison sur les chemins NORMALISÉS : un script de jeu lu sous
	// `scenes/../scripts/x` est le même fichier que `scripts/x` réécrit ici.
	std::vector<String> kept;
	for (const String &path : written)
		kept.push_back(NormalizePath(path));
	const String root = NormalizePath(directory) + String("/");
	for (const String &old : previous) {
		const String normal = NormalizePath(old);
		if (std::find(kept.begin(), kept.end(), normal) == kept.end() && normal.StartsWith(root))
			(void)sdl3::filesystem::Remove(normal);
	}
	return Ok(std::move(written));
}

} // namespace game_editor::files
