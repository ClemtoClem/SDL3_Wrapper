// Définitions de assets.hpp
#include <sandbox/editor/assets.hpp>

#include <sandbox/editor/asset_ops.hpp>
#include <sandbox/document/objects.hpp>
#include <sandbox/document/project_files.hpp>

namespace sandbox {

const char * AssetKindLabel(AssetKind kind) noexcept {
	switch (kind) {
		case AssetKind::FOLDER:
			return "Dossier";
		case AssetKind::SCENE:
			return "Scène";
		case AssetKind::OBJECT:
			return "Objet";
		case AssetKind::SCRIPT:
			return "Script";
		case AssetKind::NODE:
			return "Nœud";
		case AssetKind::MODEL:
			return "Modèle 3D";
		case AssetKind::TEXTURE:
			return "Texture";
		case AssetKind::SOUND:
			return "Son";
		case AssetKind::FONT:
			return "Police";
		case AssetKind::SHADER:
			return "Shader";
		case AssetKind::DATA:
			return "Données";
		case AssetKind::OTHER:
			break;
	}
	return "Fichier";
}

// ── AssetBrowserModel ────────────────────────────────────────────────────────

Option<ProjectPath> AssetBrowserModel::ProjectLocation(const String &location) {
	if (!location.StartsWith("/"))
		return NONE;
	auto path = ProjectPath::Parse(location.View());
	if (path.IsNone())
		return NONE; // chemin disque absolu (`/home/…`)
	switch (path.Unwrap().kind) {
		case ProjectPath::Kind::ASSETS_FOLDER:
		case ProjectPath::Kind::ASSET:
		case ProjectPath::Kind::INVALID:
			return NONE; // les ressources se parcourent par leur chemin disque
		default:
			return path;
	}
}

ProjectPath AssetBrowserModel::DiskPathOf(const String &location) const {
	if (m_diskRoot.IsEmpty())
		return ProjectPath{};
	if (location == m_diskRoot)
		return ProjectPath::AssetsFolder();
	if (location.StartsWith(m_diskRoot + String("/")))
		return ProjectPath::Asset(location.Substr(m_diskRoot.size() + 1));
	return ProjectPath{};
}

void AssetBrowserModel::SetProjectDirectory(String directory) {
	while (directory.EndsWith("/"))
		directory = directory.Substring(0, directory.GetSize() - 1);
	m_projectDir = std::move(directory);
	m_location = Canonical(m_location);
}

void AssetBrowserModel::SetOwnedFiles(std::vector<String> paths) {
	for (String &path : paths)
		path = files::NormalizePath(path);
	m_ownedFiles = std::move(paths);
}

String AssetBrowserModel::SceneFolder() const {
	return m_projectDir.IsEmpty() ? String() : m_projectDir + String("/") + files::SCENES_DIR;
}

String AssetBrowserModel::ScriptFolder() const {
	return m_projectDir.IsEmpty() ? String() : m_projectDir + String("/") + files::SCRIPTS_DIR;
}

String AssetBrowserModel::ObjectFolder() const {
	return m_projectDir.IsEmpty() ? String() : m_projectDir + String("/") + files::OBJECTS_DIR;
}

String AssetBrowserModel::Canonical(const String &location) const {
	if (m_projectDir.IsEmpty())
		return location;
	if (location == SceneFolder())
		return String(SCENES);
	if (location == ScriptFolder())
		return String(SCRIPTS);
	if (location == ObjectFolder())
		return String(OBJECTS);
	return location;
}

String AssetBrowserModel::DiskFolderOf(const String &location) const {
	if (location == SCENES)
		return SceneFolder();
	if (location == SCRIPTS)
		return ScriptFolder();
	if (location == OBJECTS)
		return ObjectFolder();
	if (ProjectLocation(location).IsSome())
		return String(); // scène, objet, nœud, script : pas un dossier disque
	return location;
}

bool AssetBrowserModel::Navigate(const String &where) {
	const String location = Canonical(where);
	if (!IsFolder(location) || location == m_location)
		return false;
	m_back.push_back(m_location);
	m_forward.clear();
	m_location = location;
	return true;
}

bool AssetBrowserModel::Back() {
	if (m_back.empty())
		return false;
	m_forward.push_back(m_location);
	m_location = m_back.back();
	m_back.pop_back();
	return true;
}

bool AssetBrowserModel::Forward() {
	if (m_forward.empty())
		return false;
	m_back.push_back(m_location);
	m_location = m_forward.back();
	m_forward.pop_back();
	return true;
}

bool AssetBrowserModel::Up() {
	if (m_location == ROOT)
		return false;
	return Navigate(Parent(m_location));
}

String AssetBrowserModel::Parent(const String &location) const {
	if (location == ROOT || location == SCENES || location == SCRIPTS || location == OBJECTS)
		return String(ROOT);
	// Chemin du projet : son parent typé (nœud → nœud parent → document →
	// dossier → racine).
	if (Option<ProjectPath> path = ProjectLocation(location); path.IsSome()) {
		const ProjectPath parent = path.Unwrap().Parent();
		return parent.IsValid() ? parent.ToString() : String(ROOT);
	}
	// Sous-dossier de `scenes/` ou `scripts/` : son parent, canonique (le
	// dossier lui-même remonte à « Scènes » / « Scripts »).
	for (const String &folder : {SceneFolder(), ScriptFolder()})
		if (!folder.IsEmpty() && location.StartsWith(folder + String("/"))) {
			const size_t slash = LastSlash(location);
			return Canonical(location.Substr(0, slash));
		}
	if (location == m_savesRoot)
		return String(ROOT);
	const size_t slash = LastSlash(location);
	if (slash == String::NPOS)
		return String(ROOT);
	const String parent = location.Substr(0, slash);
	return parent == m_diskRoot ? String(ROOT) : parent;
}

std::vector<std::pair<String, String>> AssetBrowserModel::Breadcrumb() const {
	std::vector<std::pair<String, String>> crumbs;
	crumbs.emplace_back(String("Projet"), String(ROOT));
	if (m_location == ROOT)
		return crumbs;
	if (m_location == SCENES) {
		crumbs.emplace_back(String("Scènes"), String(SCENES));
		return crumbs;
	}
	if (m_location == SCRIPTS) {
		crumbs.emplace_back(String("Scripts"), String(SCRIPTS));
		return crumbs;
	}
	if (m_location == OBJECTS) {
		crumbs.emplace_back(String("Objets"), String(OBJECTS));
		return crumbs;
	}
	// Chemin du projet (scène, objet, nœud) : un segment par niveau typé.
	if (Option<ProjectPath> path = ProjectLocation(m_location); path.IsSome()) {
		std::vector<ProjectPath> chain;
		for (ProjectPath at = path.Unwrap(); at.IsValid() && at.kind != ProjectPath::Kind::ROOT; at = at.Parent())
			chain.push_back(at);
		for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
			String label;
			switch (it->kind) {
				case ProjectPath::Kind::SCENES_FOLDER:
					label = String("Scènes");
					break;
				case ProjectPath::Kind::OBJECTS_FOLDER:
					label = String("Objets");
					break;
				case ProjectPath::Kind::SCRIPTS_FOLDER:
					label = String("Scripts");
					break;
				case ProjectPath::Kind::SCENE_NODE:
				case ProjectPath::Kind::OBJECT_NODE: {
					const size_t slash = it->nodePath.Rfind('/');
					label = slash == String::NPOS ? it->nodePath : it->nodePath.Substr(slash + 1);
					break;
				}
				default:
					label = it->document;
					break;
			}
			crumbs.emplace_back(label, it->ToString());
		}
		return crumbs;
	}
	// Sous-dossiers de « Scènes » / « Scripts » : sous leur libellé, pas sous
	// « Dossier du projet › scenes ».
	for (const auto &[folder, label, virtualLocation] :
		 {std::tuple<String, const char *, const char *>{SceneFolder(), "Scènes", SCENES},
		  std::tuple<String, const char *, const char *>{ScriptFolder(), "Scripts", SCRIPTS}}) {
		if (folder.IsEmpty() || !m_location.StartsWith(folder + String("/")))
			continue;
		crumbs.emplace_back(String(label), String(virtualLocation));
		const String relative = m_location.Substr(folder.size() + 1);
		String accumulated = folder;
		size_t start = 0;
		while (start <= relative.size()) {
			size_t end = relative.Find('/', start);
			if (end == String::NPOS)
				end = relative.size();
			const String segment = relative.Substr(start, end - start);
			if (!segment.IsEmpty()) {
				accumulated = accumulated + String("/") + segment;
				crumbs.emplace_back(segment, accumulated);
			}
			start = end + 1;
		}
		return crumbs;
	}
	// Chemin disque : un segment par dossier sous sa racine (ressources ou
	// sauvegardes).
	const bool inSaves = !m_savesRoot.IsEmpty() &&
						 (m_location == m_savesRoot || m_location.StartsWith(m_savesRoot + String("/")));
	const String &base = inSaves ? m_savesRoot : m_diskRoot;
	if (inSaves)
		crumbs.emplace_back(m_savesLabel, m_savesRoot);
	if (m_location == base)
		return crumbs;
	const String relative = m_location.StartsWith(base + String("/")) ? m_location.Substr(base.size() + 1)
																		 : m_location;
	String accumulated = base;
	size_t start = 0;
	while (start <= relative.size()) {
		size_t end = relative.Find('/', start);
		if (end == String::NPOS)
			end = relative.size();
		const String segment = relative.Substr(start, end - start);
		if (!segment.IsEmpty()) {
			accumulated = accumulated + String("/") + segment;
			crumbs.emplace_back(segment, accumulated);
		}
		start = end + 1;
	}
	return crumbs;
}

std::vector<AssetEntry> AssetBrowserModel::Entries(const String &filter) const {
	return List(m_location, filter);
}

std::vector<AssetEntry> AssetBrowserModel::List(const String &location, const String &filter) const {
	std::vector<AssetEntry> entries;
	if (location == ROOT) {
		entries.push_back(AssetEntry{String("Scènes"), String(SCENES), AssetKind::FOLDER,
									 String::Format("%d scène(s) du projet", m_project ? int(m_project->Fs().ListScenes().size()) : 0)});
		entries.push_back(AssetEntry{String("Objets"), String(OBJECTS), AssetKind::FOLDER,
									 String::Format("%d objet(s) réutilisable(s)",
													m_project ? int(m_project->Fs().ListObjects().size()) : 0)});
		entries.push_back(AssetEntry{String("Scripts"), String(SCRIPTS), AssetKind::FOLDER,
									 String("Bibliothèque de scripts du projet")});
		if (!m_savesRoot.IsEmpty() && IsFolder(m_savesRoot))
			entries.push_back(AssetEntry{m_savesLabel, m_savesRoot, AssetKind::FOLDER,
										 String("Fichiers du projet : assets, scènes, scripts")});
		for (AssetEntry &entry : ListDisk(m_diskRoot))
			if (entry.kind == AssetKind::FOLDER)
				entries.push_back(std::move(entry));
	} else if (location == SCENES) {
		if (m_project) {
			const ProjectFs fs = m_project->Fs();
			for (const String &name : fs.ListScenes()) {
				const SceneDesc &scene = *fs.FindScene(SceneRef{name});
				const ProjectPath path = ProjectPath::Scene(name);
				AssetEntry entry{name, path.ToString(), AssetKind::SCENE,
								 String::Format("%d nœud(s)%s%s", int(scene.NodeCount()),
												scene.description.IsEmpty() ? "" : " — ", scene.description.CStr())};
				entry.path = path;
				entry.browsable = scene.NodeCount() > 0;
				entry.managed = true;
				entries.push_back(std::move(entry));
			}
		}
		AppendDiskExtras(entries, SceneFolder(), true);
	} else if (location == OBJECTS) {
		if (m_project) {
			const ProjectFs fs = m_project->Fs();
			for (const String &name : fs.ListObjects()) {
				const SceneDesc &object = *fs.FindObject(ObjectRef{name});
				const ProjectPath path = ProjectPath::Object(name);
				AssetEntry entry{name, path.ToString(), AssetKind::OBJECT,
								 String::Format("%d nœud(s)%s%s", int(object.NodeCount()),
												object.description.IsEmpty() ? "" : " — ",
												object.description.CStr())};
				entry.path = path;
				entry.browsable = object.NodeCount() > 0;
				entry.managed = true;
				entries.push_back(std::move(entry));
			}
		}
		if (const String folder = ObjectFolder(); !folder.IsEmpty())
			for (AssetEntry &entry : ListDisk(folder))
				if (entry.kind == AssetKind::FOLDER ||
					std::find(m_ownedFiles.begin(), m_ownedFiles.end(), files::NormalizePath(entry.location)) ==
						m_ownedFiles.end())
					if (!(m_project && entry.kind == AssetKind::OBJECT &&
						  m_project->Fs().FindObject(ObjectRef{files::Stem(entry.name)})))
						entries.push_back(std::move(entry));
	} else if (location == SCRIPTS) {
		if (m_project) {
			// Analyse statique (cf. ScriptOutline) : rôle et refus du moteur
			// visibles sans lancer le mode Jeu ; imports lus dans la bibliothèque.
			const Project *project = m_project;
			const ModuleSource modules = [project](const String &specifier) -> Option<String> {
				if (const ScriptAsset *asset = project->FindScript(specifier))
					return Some(asset->source);
				return NONE;
			};
			auto describe = [](const ScriptOutline &outline, const String &description) {
				const String summary = outline.Summary();
				return description.IsEmpty() ? summary : String::Format("%s — %s", summary.CStr(), description.CStr());
			};
			for (const ScriptAsset &script : m_project->scripts) {
				const ScriptOutline outline = OutlineScript(script.source, ScriptUse::LIBRARY, modules);
				AssetEntry entry{script.name + String(".script"), ProjectPath::Script(script.name).ToString(),
								 AssetKind::SCRIPT, describe(outline, script.description)};
				entry.path = ProjectPath::Script(script.name);
				entry.broken = outline.error.IsSome() || outline.role == ScriptRole::INVALID;
				entry.managed = true;
				entries.push_back(std::move(entry));
			}
			// Le script de JEU de chaque scène, adressé par `/scripts/@scène`.
			for (const String &sceneName : m_project->Fs().ListGameplayScripts()) {
				const SceneDesc &scene = *m_project->Fs().FindScene(SceneRef{sceneName});
				const ScriptOutline outline = OutlineScript(scene.gameplayScript, ScriptUse::SCENE, modules);
				AssetEntry entry{scene.name + String(".main.script"), ProjectPath::GameplayScript(scene.name).ToString(),
								 AssetKind::SCRIPT,
								 describe(outline, String::Format("script de la scène « %s »", scene.name.CStr()))};
				entry.path = ProjectPath::GameplayScript(scene.name);
				entry.broken = outline.error.IsSome() || outline.role == ScriptRole::INVALID;
				entry.managed = true;
				entries.push_back(std::move(entry));
			}
		}
		AppendDiskExtras(entries, ScriptFolder(), false);
	} else if (Option<ProjectPath> path = ProjectLocation(location); path.IsSome()) {
		entries = ListNodes(path.Unwrap()); // une scène, un objet ou un nœud
	} else {
		entries = ListDisk(location);
	}

	if (!filter.IsEmpty()) {
		const String wanted = filter.ToLower();
		std::erase_if(entries, [&wanted](const AssetEntry &entry) { return !entry.name.ToLower().Contains(wanted); });
	}
	std::stable_sort(entries.begin(), entries.end(), [](const AssetEntry &a, const AssetEntry &b) {
		const bool fa = a.kind == AssetKind::FOLDER, fb = b.kind == AssetKind::FOLDER;
		if (fa != fb)
			return fa;
		return a.name.ToLower() < b.name.ToLower();
	});
	return entries;
}

void AssetBrowserModel::AppendDiskExtras(std::vector<AssetEntry> &entries, const String &folder, bool scenes) const {
	if (folder.IsEmpty())
		return;
	// Les fichiers que le projet écrit lui-même (cf. files::SaveProject) sont
	// déjà là, comme éléments du projet : ne pas les montrer deux fois.
	std::vector<String> owned;
	if (m_project) {
		if (scenes) {
			for (const String &scene : m_project->Fs().ListScenes())
				owned.push_back(files::SafeFileName(scene) + String(".scene"));
		} else {
			for (const ScriptAsset &script : m_project->scripts)
				owned.push_back(files::SafeFileName(script.name) + String(".script"));
			for (const String &scene : m_project->Fs().ListGameplayScripts())
				owned.push_back(files::SafeFileName(scene) + String(files::GAMEPLAY_SUFFIX));
		}
	}
	for (AssetEntry &entry : ListDisk(folder)) {
		if (entry.kind != AssetKind::FOLDER &&
			(std::find(owned.begin(), owned.end(), entry.name) != owned.end() ||
			 std::find(m_ownedFiles.begin(), m_ownedFiles.end(), files::NormalizePath(entry.location)) !=
				 m_ownedFiles.end()))
			continue;
		entries.push_back(std::move(entry));
	}
}

bool AssetBrowserModel::IsFolder(const String &location) const {
	if (location == ROOT || location == SCENES || location == SCRIPTS || location == OBJECTS)
		return true;
	if (Option<ProjectPath> path = ProjectLocation(location); path.IsSome()) {
		// Une scène, un objet ou un nœud se parcourent ; un script, non.
		const ProjectPath &p = path.Unwrap();
		return m_project && (p.IsScene() || p.IsObject()) && m_project->Fs().Exists(p);
	}
	Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(location);
	return info.IsSome() && info.Unwrap().type == sdl3::PathType::DIRECTORY;
}

AssetKind AssetBrowserModel::KindOf(const String &fileName) {
	const String lower = fileName.ToLower();
	auto any = [&lower](std::initializer_list<const char *> suffixes) {
		for (const char *suffix : suffixes)
			if (lower.EndsWith(suffix))
				return true;
		return false;
	};
	if (any({".gltf", ".glb", ".obj", ".fbx"}))
		return AssetKind::MODEL;
	if (any({".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".webp"}))
		return AssetKind::TEXTURE;
	if (any({".ogg", ".wav", ".mp3", ".flac"}))
		return AssetKind::SOUND;
	if (any({".ttf", ".otf"}))
		return AssetKind::FONT;
	if (any({".vert", ".frag", ".comp", ".glsl", ".hlsl", ".spv", ".msl"}))
		return AssetKind::SHADER;
	if (any({".script", ".lua", ".py"}))
		return AssetKind::SCRIPT;
	if (any({".scene"}))
		return AssetKind::SCENE;
	if (any({".object"}))
		return AssetKind::OBJECT;
	if (any({".json", ".yaml", ".yml", ".xml", ".csv", ".txt", ".md", ".ini"}))
		return AssetKind::DATA;
	return AssetKind::OTHER;
}

bool AssetBrowserModel::IsHiddenFolder(const String &name) {
	return name.StartsWith(".") || name == "bios-firmware" || name == "roms";
}

size_t AssetBrowserModel::LastSlash(const String &path) {
	size_t found = String::NPOS;
	for (size_t i = 0; i < path.size(); ++i)
		if (path[i] == '/')
			found = i;
	return found;
}

std::vector<AssetEntry> AssetBrowserModel::ListDisk(const String &directory) const {
	std::vector<AssetEntry> entries;
	std::vector<String> names;
	(void)sdl3::filesystem::EnumerateDirectory(directory, [&names](const char *, const char *file) {
		names.emplace_back(file);
		return true;
	});
	const bool atRoot = directory == m_diskRoot;
	for (const String &name : names) {
		if (name.StartsWith(".") || (atRoot && IsHiddenFolder(name)))
			continue;
		// Tampons binaires d'un glTF (`.bin`) : des dépendances du modèle,
		// pas des ressources qu'on manipule — le moteur les masque.
		if (name.ToLower().EndsWith(".bin"))
			continue;
		const String path = directory + String("/") + name;
		Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(path);
		if (info.IsNone())
			continue;
		AssetEntry entry;
		entry.name = name;
		entry.location = path;
		entry.path = DiskPathOf(path);
		if (info.Unwrap().type == sdl3::PathType::DIRECTORY) {
			entry.kind = AssetKind::FOLDER;
		} else {
			entry.kind = KindOf(name);
			const uint64_t size = info.Unwrap().size;
			entry.detail = size >= 1024 * 1024 ? String::Format("%.1f Mo", double(size) / (1024.0 * 1024.0))
											   : String::Format("%d Ko", int((size + 1023) / 1024));
		}
		entries.push_back(std::move(entry));
	}
	return entries;
}

std::vector<AssetEntry> AssetBrowserModel::ListNodes(const ProjectPath &parent) const {
	std::vector<AssetEntry> entries;
	if (!m_project)
		return entries;
	const ProjectFs fs = m_project->Fs();
	const SceneDesc *document = fs.FindDocument(parent);
	if (!document)
		return entries;
	const NodeRef at = fs.FindNode(parent.Document(), parent.nodePath.View());
	if (!at.IsValid())
		return entries;
	for (scene::NodeId id : document->tree.ChildrenOf(at.id)) {
		const scene::Node *node = document->tree.Get(id);
		if (!node)
			continue;
		const String nodePath = at.path.IsEmpty() ? node->name : at.path + String("/") + node->name;
		const ProjectPath path = parent.IsObject() ? ProjectPath::ObjectNode(document->name, nodePath)
												   : ProjectPath::SceneNode(document->name, nodePath);
		const size_t children = document->tree.ChildrenOf(id).size();
		String detail = node->type;
		if (objects::IsInstance(*node))
			detail = String::Format("instance de « %s »", objects::SourceOf(*node).CStr());
		else if (objects::IsGenerated(*node))
			detail = detail + String(" (contenu d'instance)");
		if (children > 0)
			detail = detail + String::Format(" · %d enfant%s", int(children), children > 1 ? "s" : "");
		AssetEntry entry{node->name, path.ToString(), AssetKind::NODE, detail};
		entry.path = path;
		entry.browsable = children > 0;
		entry.managed = true;
		entries.push_back(std::move(entry));
	}
	return entries;
}

// ── AssetBrowserPanel ────────────────────────────────────────────────────────

AssetBrowserPanel::AssetBrowserPanel(UiContext &ctx, AssetActions actions)
	: m_ctx(ctx), m_actions(std::move(actions)), m_ops(std::make_unique<AssetOperations>(ctx.runtime, m_model)) {}

AssetBrowserPanel::~AssetBrowserPanel() {
	ClearThumbnails();
}

void AssetBrowserPanel::Build(ui::Widget page) {
	m_model.SetProject(&m_ctx.runtime.GetProject());
	m_model.SetProjectDirectory(m_ctx.runtime.ProjectDirectory());
	m_page = page;
	const ui::Palette &theme = m_ctx.Theme();

	// ── Barre : ← → ↑ · fil d'Ariane · recherche · + ≡ ────────────────────
	ui::WidgetBuilder bar = ui::Row();
	bar.Padding(0.f);
	bar.Gap(2.f).GrowWidth().AlignItems(ui::Align::Center);
	ui::Widget barEntity = page.Append(bar);
	m_back = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::ARROW_BACK, String("Précédent"), [this] {
		if (m_model.Back())
			MarkDirty();
	});
	m_forward = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::ARROW_FORWARD, String("Suivant"), [this] {
		if (m_model.Forward())
			MarkDirty();
	});
	m_up = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::ARROW_UPWARD, String("Dossier parent"), [this] {
		if (m_model.Up())
			MarkDirty();
	});
	ui::WidgetBuilder crumbs = ui::Row();
	crumbs.Padding(0.f);
	crumbs.Gap(0.f).GrowWidth().AlignItems(ui::Align::Center).Clip();
	m_crumbs = barEntity.Append(crumbs);
	m_search = kit::SearchField(m_ctx, barEntity, String("Filtrer…"), [this](const String &text) {
		m_filter = text;
		MarkDirty();
	}, 170.f);
	(void)kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::ADD, String("Nouveau script"), [this] {
		const String name = m_ctx.runtime.AddScript(String("nouveau_script"), String(NEW_SCRIPT_TEMPLATE),
													String("Script créé depuis le navigateur"));
		(void)m_model.Navigate(String(AssetBrowserModel::SCRIPTS));
		MarkDirty();
		if (m_actions.openScript)
			m_actions.openScript(name);
	});
	// Opérations sur les fichiers (activées selon la sélection, cf. Refresh).
	m_newFolderButton = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::CREATE_NEW_FOLDER,
										String("Nouveau dossier"), [this] { BeginNewFolder(); });
	m_renameButton = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::DRIVE_FILE_RENAME_OUTLINE,
									 String("Renommer (F2)"), [this] { BeginRename(); });
	m_duplicateButton = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::CONTENT_COPY,
										String("Dupliquer (Ctrl+D)"), [this] { (void)DuplicateSelection(); });
	m_deleteButton = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::DELETE, String("Supprimer (Suppr)"),
									 [this] { BeginDelete(); });

	// ── Corps : arbre des dossiers | grille ─────────────────────────────
	ui::WidgetBuilder body = ui::Row();
	body.Padding(0.f);
	body.Gap(0.f).GrowWidth().GrowHeight();
	ui::Widget bodyEntity = page.Append(body);

	ui::WidgetBuilder tree = ui::Column();
	tree.Gap(1.f).Padding(math::Sides{2.f, 4.f}).Width(170.f).GrowHeight().Scrollable();
	tree.Background(kit::PaletteOf(m_ctx).toolbar);
	m_tree = bodyEntity.Append(tree);

	ui::WidgetBuilder right = ui::Column();
	right.Gap(0.f).Padding(0.f).GrowWidth().GrowHeight();
	ui::Widget rightEntity = bodyEntity.Append(right);

	ui::WidgetBuilder grid = ui::Column();
	grid.Gap(TILE_GAP).Padding(math::Sides{8.f, 6.f}).GrowWidth().GrowHeight().Scrollable();
	// Clic droit dans le vide de la grille : menu du dossier courant.
	grid.OnContextMenu([this](sdl3::FPoint p) { OpenMenu(p.x, p.y, false); });
	m_grid = rightEntity.Append(grid);

	ui::WidgetBuilder footer = ui::Row();
	footer.Gap(8.f).Padding(math::Sides{8.f, 2.f}).GrowWidth().AlignItems(ui::Align::Center);
	ui::Widget footerEntity = rightEntity.Append(footer);
	ui::WidgetBuilder info = ui::Label(String());
	info.GrowWidth().FontSize(12.f).Color(theme.muted).Ellipsis();
	m_info = footerEntity.Append(info);
	(void)kit::Glyph(m_ctx, footerEntity, ui::MaterialIcons::VIEW_QUILT, theme.muted, 14.f);
	ui::WidgetBuilder zoom = ui::Slider(MIN_TILE, MAX_TILE, m_tileSize);
	zoom.Size(110.f, 14.f).Tooltip(String("Taille des vignettes"));
	zoom.OnChange([this](float value) {
		if (sdl3::Abs(value - m_tileSize) >= 4.f) {
			m_tileSize = value;
			MarkDirty();
		}
	});
	footerEntity.Append(zoom);

	BuildMenus();
	BuildDialogs();
	MarkDirty();
}

void AssetBrowserPanel::Teardown() {
	ClearThumbnails();
	for (ui::Widget popup : m_popups)
		if (popup.Valid())
			ui::Widget(popup).Destroy();
	m_popups.clear();
	m_tiles.clear();
	m_page = m_tree = m_grid = m_crumbs = m_info = m_search = ui::Widget();
	m_newFolderButton = m_renameButton = m_duplicateButton = m_deleteButton = ui::Widget();
	m_menu = m_nameModal = m_nameInput = m_nameTitle = m_confirmModal = m_confirmText = m_confirmTitle = ui::Widget();
}

void AssetBrowserPanel::Tick(float dt) {
	m_clock += dt;
	if (!m_grid.Valid())
		return;
	{
		const float width = m_grid.Bounds().w;
		if (width > 0.f && ColumnsFor(width) != ColumnsFor(m_gridWidth)) {
			m_gridWidth = width;
			m_dirty = true;
		}
	}
	if (m_dirty) {
		m_dirty = false;
		Refresh();
	}
}

void AssetBrowserPanel::SetRoots(const String &assets, const String &saves, const String &savesLabel) {
	// Mêmes racines (reconstruction de l'interface après un changement du
	// projet) : on garde l'emplacement, l'historique et la sélection.
	const bool same = m_model.DiskRoot() == assets && m_model.SavesRoot() == saves;
	if (!same) {
		m_model = AssetBrowserModel(&m_ctx.runtime.GetProject(), assets, saves, savesLabel);
		m_selection.clear();
		m_anchor = String();
		m_treeOpen.clear();
	}
	m_model.SetProject(&m_ctx.runtime.GetProject());
	m_model.SetProjectDirectory(m_ctx.runtime.ProjectDirectory());
	if (!m_model.IsFolder(m_model.Location()))
		(void)m_model.Navigate(String(AssetBrowserModel::ROOT));
	MarkDirty();
}

bool AssetBrowserPanel::SelectByName(const String &name) {
	return SelectNames({name}) == 1;
}

size_t AssetBrowserPanel::SelectNames(const std::vector<String> &names) {
	m_entries = m_model.Entries(m_filter);
	std::vector<String> chosen;
	for (const AssetEntry &entry : m_entries)
		if (std::find(names.begin(), names.end(), entry.name) != names.end())
			chosen.push_back(entry.location);
	if (chosen.empty())
		return 0;
	SelectLocations(chosen);
	return chosen.size();
}

void AssetBrowserPanel::SelectLocations(const std::vector<String> &locations) {
	m_selection = locations;
	m_anchor = locations.empty() ? String() : locations.front();
	MarkDirty();
}

void AssetBrowserPanel::SelectAll() {
	std::vector<String> all;
	for (const AssetEntry &entry : m_entries)
		all.push_back(entry.location);
	SelectLocations(all);
}

void AssetBrowserPanel::ClearSelection() {
	SelectLocations({});
}

bool AssetBrowserPanel::IsSelected(const String &location) const {
	return std::find(m_selection.begin(), m_selection.end(), location) != m_selection.end();
}

std::vector<AssetEntry> AssetBrowserPanel::SelectedEntries() const {
	std::vector<AssetEntry> out;
	for (const AssetEntry &entry : m_entries)
		if (IsSelected(entry.location))
			out.push_back(entry);
	return out;
}

void AssetBrowserPanel::Status(const String &text) {
	if (m_actions.status)
		m_actions.status(text);
}

void AssetBrowserPanel::Report(const AssetOpReport &report, const char *verb) {
	Status(report.Summary(verb));
	for (const String &error : report.errors)
		m_ctx.runtime.LogWarning(error);
	if (!report.created.empty())
		SelectLocations(report.created);
	MarkDirty();
}

// ── Opérations ───────────────────────────────────────────────────────────────

bool AssetBrowserPanel::CreateFolder(const String &name) {
	const String parent = m_menuFolder.IsSome() ? m_menuFolder.Value().location : m_model.Location();
	auto created = m_ops->CreateFolder(parent, name);
	if (created.IsError()) {
		Status(String::Format("Nouveau dossier : %s", created.Error().CStr()));
		return false;
	}
	m_treeOpen[m_model.Canonical(parent)] = true;
	if (parent == m_model.Location())
		SelectLocations({created.Value()});
	Status(String::Format("Dossier créé : %s", name.Trim().CStr()));
	MarkDirty();
	return true;
}

bool AssetBrowserPanel::RenameSelection(const String &wanted) {
	std::vector<AssetEntry> targets =
		m_menuFolder.IsSome() ? std::vector<AssetEntry>{m_menuFolder.Value()} : SelectedEntries();
	if (targets.empty())
		return false;
	const String base = wanted.Trim();
	std::vector<String> renamed;
	int failures = 0;
	for (size_t i = 0; i < targets.size(); ++i) {
		// Plusieurs éléments : numérotés à partir du deuxième (« mur »,
		// « mur 2 »…) ; l'extension de chaque fichier est gardée.
		const String name = i == 0 ? base : String::Format("%s %d", base.CStr(), int(i + 1));
		auto result = m_ops->Rename(targets[i], name);
		if (result.IsError()) {
			++failures;
			m_ctx.runtime.LogWarning(String::Format("%s : %s", targets[i].name.CStr(), result.Error().CStr()));
			Status(String::Format("Renommer « %s » : %s", targets[i].name.CStr(), result.Error().CStr()));
			continue;
		}
		renamed.push_back(result.Value());
	}
	if (m_menuFolder.IsNone() && !renamed.empty())
		SelectLocations(renamed);
	if (failures == 0)
		Status(String::Format("%d élément%s renommé%s", int(renamed.size()), renamed.size() > 1 ? "s" : "",
							  renamed.size() > 1 ? "s" : ""));
	MarkDirty();
	return failures == 0;
}

bool AssetBrowserPanel::DeleteSelection() {
	std::vector<AssetEntry> targets =
		m_menuFolder.IsSome() ? std::vector<AssetEntry>{m_menuFolder.Value()} : SelectedEntries();
	if (targets.empty())
		return false;
	const AssetOpReport report = m_ops->Delete(targets);
	if (m_menuFolder.IsNone())
		ClearSelection();
	Report(report, report.done > 1 ? "supprimés" : "supprimé");
	return report.Ok();
}

bool AssetBrowserPanel::DuplicateSelection() {
	const std::vector<AssetEntry> targets = SelectedEntries();
	if (targets.empty())
		return false;
	const AssetOpReport report = m_ops->Duplicate(targets);
	Report(report, report.done > 1 ? "dupliqués" : "dupliqué");
	return report.Ok();
}

void AssetBrowserPanel::CutSelection() {
	m_clipboard.clear();
	for (const AssetEntry &entry : SelectedEntries())
		if (m_ops->WhyLocked(entry, "move").IsNone())
			m_clipboard.push_back(entry);
	Status(m_clipboard.empty() ? String("Rien à couper (ces éléments ne se déplacent pas)")
							   : String::Format("%d élément%s coupé%s — Ctrl+V dans un autre dossier pour les y "
												"déplacer",
												int(m_clipboard.size()), m_clipboard.size() > 1 ? "s" : "",
												m_clipboard.size() > 1 ? "s" : ""));
}

bool AssetBrowserPanel::Paste() {
	if (m_clipboard.empty())
		return false;
	const String target = m_menuFolder.IsSome() ? m_menuFolder.Value().location : m_model.Location();
	const bool moved = MoveEntries(m_clipboard, target);
	m_clipboard.clear();
	return moved;
}

bool AssetBrowserPanel::MoveEntries(const std::vector<AssetEntry> &entries, const String &location) {
	const AssetOpReport report = m_ops->Move(entries, location);
	m_treeOpen[m_model.Canonical(location)] = true;
	Report(report, report.done > 1 ? "déplacés" : "déplacé");
	// Les éléments partis ne sont plus dans ce dossier : la sélection les suit
	// seulement si l'on est dans la destination.
	if (m_model.Location() != m_model.Canonical(location))
		ClearSelection();
	return report.Ok();
}

void AssetBrowserPanel::OnDrop(const String &folder, int64_t draggedIndex) {
	if (draggedIndex < 0 || size_t(draggedIndex) >= m_entries.size())
		return;
	const AssetEntry &dragged = m_entries[size_t(draggedIndex)];
	// Glisser une vignette sélectionnée emporte TOUTE la sélection.
	const std::vector<AssetEntry> items =
		IsSelected(dragged.location) ? SelectedEntries() : std::vector<AssetEntry>{dragged};
	(void)MoveEntries(items, folder);
}

// ── Boîtes de dialogue ───────────────────────────────────────────────────────

void AssetBrowserPanel::BuildDialogs() {
	// Nom (nouveau dossier, renommer).
	ui::WidgetBuilder title = ui::Label(String("Nom"));
	title.FontSize(15.f).Bold().GrowWidth().Ref(m_nameTitle);
	ui::WidgetBuilder input = ui::Input();
	input.GrowWidth().Height(28.f).Ref(m_nameInput).OnSubmit([this](const String &text) { ApplyNameDialog(text); });
	ui::WidgetBuilder ok = ui::Button(String("Valider"));
	ok.OnClick([this] { ApplyNameDialog(m_nameInput.Text()); });
	ui::WidgetBuilder cancel = ui::Button(String("Annuler"));
	cancel.OnClick([this] {
		m_nameModal.Close();
		m_ctx.gui.ClearFocus(); // le champ caché ne doit pas garder le clavier
		m_menuFolder = NONE;
	});
	ui::WidgetBuilder buttons = ui::Row();
	buttons.Padding(0.f);
	buttons.Gap(8.f).GrowWidth().JustifyContent(ui::Justify::End).Children({std::move(cancel), std::move(ok)});
	ui::WidgetBuilder panel = ui::Panel();
	panel.Size(420.f, 150.f).Padding(16.f).Gap(12.f).Children({std::move(title), std::move(input), std::move(buttons)});
	m_nameModal = m_ctx.gui.Mount(ui::Modal(std::move(panel)));
	m_popups.push_back(m_nameModal);

	// Confirmation de suppression.
	ui::WidgetBuilder heading = ui::Label(String("Supprimer ?"));
	heading.FontSize(15.f).Bold().GrowWidth().Ref(m_confirmTitle);
	ui::WidgetBuilder text = ui::Label(String());
	text.GrowWidth().Height(92.f).FontSize(13.f).TextWrap().Ref(m_confirmText);
	ui::WidgetBuilder yes = ui::Button(String("Supprimer"));
	yes.Background(kit::PaletteOf(m_ctx).error).OnClick([this] {
		m_confirmModal.Close();
		m_ctx.gui.ClearFocus();
		(void)DeleteSelection();
		m_menuFolder = NONE;
	});
	ui::WidgetBuilder no = ui::Button(String("Annuler"));
	no.OnClick([this] {
		m_confirmModal.Close();
		m_ctx.gui.ClearFocus();
		m_menuFolder = NONE;
	});
	ui::WidgetBuilder confirmButtons = ui::Row();
	confirmButtons.Padding(0.f);
	confirmButtons.Gap(8.f).GrowWidth().JustifyContent(ui::Justify::End).Children({std::move(no), std::move(yes)});
	ui::WidgetBuilder confirm = ui::Panel();
	confirm.Size(480.f, 220.f).Padding(16.f).Gap(10.f).Children({std::move(heading), std::move(text), std::move(confirmButtons)});
	m_confirmModal = m_ctx.gui.Mount(ui::Modal(std::move(confirm)));
	m_popups.push_back(m_confirmModal);

}

void AssetBrowserPanel::BeginNewFolder() {
	const String parent = m_menuFolder.IsSome() ? m_menuFolder.Value().location : m_model.Location();
	if (!m_ops->IsWritableFolder(parent)) {
		Status(String("Ce dossier ne se modifie pas : seul le dossier du projet le peut"));
		m_menuFolder = NONE;
		return;
	}
	m_nameDialog = NameDialog::NEW_FOLDER;
	kit::SetLabelText(m_ctx, m_nameTitle, String("Nouveau dossier"));
	m_nameModal.Open();
	m_nameInput.SetText(String("Nouveau dossier")).Focus();
}

void AssetBrowserPanel::BeginRename() {
	std::vector<AssetEntry> targets =
		m_menuFolder.IsSome() ? std::vector<AssetEntry>{m_menuFolder.Value()} : SelectedEntries();
	if (targets.empty()) {
		Status(String("Rien à renommer : sélectionnez un ou plusieurs éléments"));
		return;
	}
	for (const AssetEntry &entry : targets)
		if (Option<String> locked = m_ops->WhyLocked(entry, "rename"); locked.IsSome()) {
			Status(String::Format("« %s » ne se renomme pas : %s", entry.name.CStr(), locked.Unwrap().CStr()));
			m_menuFolder = NONE;
			return;
		}
	m_nameDialog = NameDialog::RENAME;
	kit::SetLabelText(m_ctx, m_nameTitle,
					  targets.size() == 1 ? String::Format("Renommer « %s »", targets.front().name.CStr())
										  : String::Format("Renommer %d éléments (numérotés : nom, nom 2…)",
														   int(targets.size())));
	// Proposer le nom actuel SANS son extension (elle est gardée).
	const AssetEntry &first = targets.front();
	String current = first.name;
	if (first.kind != AssetKind::FOLDER) {
		const String ext = AssetOperations::ExtensionOf(current);
		if (!ext.IsEmpty() && (!first.managed || first.kind == AssetKind::SCRIPT))
			current = current.Substr(0, current.size() - ext.size());
	}
	m_nameModal.Open();
	m_nameInput.SetText(current).Focus();
}

void AssetBrowserPanel::BeginDelete() {
	std::vector<AssetEntry> targets =
		m_menuFolder.IsSome() ? std::vector<AssetEntry>{m_menuFolder.Value()} : SelectedEntries();
	if (targets.empty()) {
		Status(String("Rien à supprimer : sélectionnez un ou plusieurs éléments"));
		return;
	}
	String names;
	size_t managed = 0;
	for (size_t i = 0; i < targets.size(); ++i) {
		managed += targets[i].managed ? 1 : static_cast<size_t>(0);
		if (i < 4)
			names.Concat(String::Format("%s« %s »", i ? ", " : "", targets[i].name.CStr()));
	}
	if (targets.size() > 4)
		names.Concat(String::Format(" et %d autre(s)", int(targets.size() - 4)));
	String text = names;
	if (managed > 0)
		text.Concat("\nScènes et scripts du projet : retirés du projet (fichiers effacés à l'enregistrement).");
	if (managed < targets.size())
		text.Concat("\nFichiers et dossiers : effacés du disque, sans corbeille.");
	kit::SetLabelText(m_ctx, m_confirmText, text);
	kit::SetLabelText(m_ctx, m_confirmTitle,
					  String::Format("Supprimer %d élément%s ?", int(targets.size()), targets.size() > 1 ? "s" : ""));
	m_confirmModal.Open();
}

void AssetBrowserPanel::ApplyNameDialog(const String &text) {
	m_nameModal.Close();
	m_ctx.gui.ClearFocus(); // le champ caché ne doit pas garder le clavier
	if (text.Trim().IsEmpty()) {
		m_menuFolder = NONE;
		return;
	}
	if (m_nameDialog == NameDialog::NEW_FOLDER)
		(void)CreateFolder(text);
	else
		(void)RenameSelection(text);
	m_menuFolder = NONE;
}

// ── Menu contextuel ──────────────────────────────────────────────────────────

ui::Widget AssetBrowserPanel::MenuItem(const char *text, const char *shortcut, std::function<void()> action) {
	ui::WidgetBuilder item = ui::MenuItem(String(text), String(shortcut));
	item.OnClick(std::move(action));
	return m_menu.Append(item);
}

void AssetBrowserPanel::BuildMenus() {
	m_menu = m_ctx.gui.Mount(ui::ContextMenu());
	m_popups.push_back(m_menu);
	m_menuOpen = MenuItem("Ouvrir", "Entrée", [this] {
		const std::vector<AssetEntry> selected = SelectedEntries();
		if (m_menuFolder.IsSome()) {
			if (m_model.Navigate(m_menuFolder.Value().location))
				MarkDirty();
		} else if (selected.size() == 1) {
			for (size_t i = 0; i < m_entries.size(); ++i)
				if (m_entries[i].location == selected.front().location)
					Activate(i);
		}
		m_menuFolder = NONE;
	});
	m_menuBrowse = MenuItem("Parcourir les nœuds", "", [this] {
		const std::vector<AssetEntry> selected = SelectedEntries();
		if (selected.size() == 1 && m_model.Navigate(selected.front().location))
			MarkDirty();
	});
	m_menuInstance = MenuItem("Poser une instance", "", [this] {
		const std::vector<AssetEntry> selected = SelectedEntries();
		for (const AssetEntry &entry : selected)
			if (entry.kind == AssetKind::OBJECT && entry.managed && m_actions.instantiateObject)
				m_actions.instantiateObject(entry.name);
	});
	m_menuNewFolder = MenuItem("Nouveau dossier…", "", [this] { BeginNewFolder(); });
	m_menuRename = MenuItem("Renommer…", "F2", [this] { BeginRename(); });
	m_menuDuplicate = MenuItem("Dupliquer", "Ctrl+D", [this] { (void)DuplicateSelection(); });
	m_menuCut = MenuItem("Couper", "Ctrl+X", [this] { CutSelection(); });
	m_menuPaste = MenuItem("Coller ici", "Ctrl+V", [this] {
		(void)Paste();
		m_menuFolder = NONE;
	});
	m_menuDelete = MenuItem("Supprimer…", "Suppr", [this] { BeginDelete(); });
	m_menuSelectAll = MenuItem("Tout sélectionner", "Ctrl+A", [this] { SelectAll(); });
}

void AssetBrowserPanel::OpenMenu(float x, float y, bool onFolderRow) {
	if (!onFolderRow)
		m_menuFolder = NONE;
	// Ce que chaque entrée peut faire, d'après la cible : la ligne d'arbre,
	// ou la sélection de la grille.
	const std::vector<AssetEntry> targets =
		m_menuFolder.IsSome() ? std::vector<AssetEntry>{m_menuFolder.Value()} : SelectedEntries();
	auto allowed = [&](const char *operation) {
		if (targets.empty())
			return false;
		for (const AssetEntry &entry : targets)
			if (m_ops->WhyLocked(entry, operation).IsSome())
				return false;
		return true;
	};
	const String folder = m_menuFolder.IsSome() ? m_menuFolder.Value().location : m_model.Location();
	SetEnabled(m_menuOpen, m_menuFolder.IsSome() || targets.size() == 1);
	bool objectsOnly = m_menuFolder.IsNone() && !targets.empty();
	for (const AssetEntry &entry : targets)
		objectsOnly = objectsOnly && entry.kind == AssetKind::OBJECT && entry.managed;
	SetEnabled(m_menuInstance, objectsOnly);
	SetEnabled(m_menuBrowse, m_menuFolder.IsNone() && targets.size() == 1 && targets.front().browsable);
	SetEnabled(m_menuNewFolder, m_ops->IsWritableFolder(folder));
	SetEnabled(m_menuRename, allowed("rename"));
	SetEnabled(m_menuDuplicate, m_menuFolder.IsNone() && allowed("duplicate"));
	SetEnabled(m_menuCut, m_menuFolder.IsNone() && allowed("move"));
	SetEnabled(m_menuPaste, !m_clipboard.empty() && m_ops->IsWritableFolder(folder));
	SetEnabled(m_menuDelete, allowed("delete"));
	SetEnabled(m_menuSelectAll, m_menuFolder.IsNone() && !m_entries.empty());
	m_menu.OpenAt(sdl3::FPoint{x, y});
}

// ── Clavier ──────────────────────────────────────────────────────────────────

bool AssetBrowserPanel::PointerOver() const {
	if (!m_page.Valid() || m_ctx.gui.HasOpenModal())
		return false;
	const sdl3::FRect bounds = m_page.Bounds();
	if (bounds.w <= 0.f)
		return false;
	float x = 0.f, y = 0.f;
	(void)SDL_GetMouseState(&x, &y);
	return bounds.Contains(sdl3::FPoint{x, y});
}

bool AssetBrowserPanel::HandleKey(const sdl3::Event &event) {
	if (!event.IsKeyDown() || !PointerOver())
		return false;
	// Saisie en cours (filtre) : les touches sont les siennes. Un bouton qui
	// a gardé le focus après un clic (une vignette) ne compte pas.
	ui::Widget focus = m_ctx.gui.Focused();
	if (focus.Valid() && focus.Get<ui::TextEditor>().IsSome())
		return false;
	// Modificateurs portés par l'ÉVÈNEMENT (pas l'état courant du clavier,
	// qui peut déjà avoir changé quand l'évènement est traité).
	const bool ctrl = (event.raw.key.mod & SDL_KMOD_CTRL) != 0;
	m_menuFolder = NONE;
	if (event.IsKeyDown(SDLK_DELETE)) {
		BeginDelete();
		return true;
	}
	if (event.IsKeyDown(SDLK_F2)) {
		BeginRename();
		return true;
	}
	if (ctrl && event.IsKeyDown(SDLK_D)) {
		(void)DuplicateSelection();
		return true;
	}
	if (ctrl && event.IsKeyDown(SDLK_X)) {
		CutSelection();
		return true;
	}
	if (ctrl && event.IsKeyDown(SDLK_V)) {
		(void)Paste();
		return true;
	}
	if (ctrl && event.IsKeyDown(SDLK_A)) {
		SelectAll();
		return true;
	}
	if (event.IsKeyDown(SDLK_ESCAPE)) {
		ClearSelection();
		return true;
	}
	if (event.IsKeyDown(SDLK_BACKSPACE)) {
		if (m_model.Up())
			MarkDirty();
		return true;
	}
	if (event.IsKeyDown(SDLK_RETURN)) {
		const std::vector<AssetEntry> selected = SelectedEntries();
		if (selected.size() == 1)
			for (size_t i = 0; i < m_entries.size(); ++i)
				if (m_entries[i].location == selected.front().location)
					Activate(i);
		return true;
	}
	return false;
}

void AssetBrowserPanel::Activate(size_t index) {
	if (index >= m_entries.size())
		return;
	const AssetEntry entry = m_entries[index];
	switch (entry.kind) {
		case AssetKind::FOLDER:
			if (m_model.Navigate(entry.location))
				MarkDirty();
			return;
		case AssetKind::SCENE:
		case AssetKind::OBJECT:
		case AssetKind::NODE:
			// Scène, objet du projet ou nœud : ouvrir le document (et
			// sélectionner le nœud) ; hors du projet, un fichier.
			if (entry.managed && (entry.path.IsScene() || entry.path.IsObject())) {
				if (m_actions.openPath)
					m_actions.openPath(entry.path);
			} else if (entry.kind == AssetKind::SCENE && m_actions.instantiateScene) {
				m_actions.instantiateScene(entry.location); // scène emballée (.scene)
			} else if (m_actions.status) {
				m_actions.status(String("Fichier d'objet hors du projet : ajoutez-le au manifeste pour l'utiliser"));
			}
			return;
		case AssetKind::SCRIPT:
			if (entry.path.IsGameplayScript()) {
				if (m_actions.openSceneScript)
					m_actions.openSceneScript(entry.path.document);
			} else if (entry.path.IsScript()) {
				if (m_actions.openScript)
					m_actions.openScript(entry.path.document);
			} else if (m_actions.openFile) {
				m_actions.openFile(entry.location);
			}
			return;
		case AssetKind::MODEL:
			if (m_actions.importModel)
				m_actions.importModel(entry.location);
			return;
		case AssetKind::SOUND:
			if (m_actions.previewSound)
				m_actions.previewSound(entry.location);
			return;
		case AssetKind::DATA:
		case AssetKind::SHADER:
			if (m_actions.openFile)
				m_actions.openFile(entry.location);
			return;
		default:
			if (m_actions.status)
				m_actions.status(String::Format("%s — %s (%s)", entry.name.CStr(), AssetKindLabel(entry.kind),
												entry.detail.CStr()));
			return;
	}
}

int AssetBrowserPanel::ColumnsFor(float width) const noexcept {
	const float tile = m_tileSize + 8.f;
	return sdl3::Max(1, int((width - 16.f - 14.f + TILE_GAP) / (tile + TILE_GAP)));
}

void AssetBrowserPanel::Refresh() {
	m_model.SetOwnedFiles(m_ctx.runtime.ProjectFiles());
	m_entries = m_model.Entries(m_filter);
	// La sélection ne garde que ce qui est encore là.
	std::erase_if(m_selection, [this](const String &location) {
		return std::none_of(m_entries.begin(), m_entries.end(),
							[&](const AssetEntry &entry) { return entry.location == location; });
	});
	const std::vector<AssetEntry> selected = SelectedEntries();
	auto allowed = [&](const char *operation) {
		if (selected.empty())
			return false;
		for (const AssetEntry &entry : selected)
			if (m_ops->WhyLocked(entry, operation).IsSome())
				return false;
		return true;
	};
	SetEnabled(m_newFolderButton, m_ops->IsWritableFolder(m_model.Location()));
	SetEnabled(m_renameButton, allowed("rename"));
	SetEnabled(m_duplicateButton, allowed("duplicate"));
	SetEnabled(m_deleteButton, allowed("delete"));
	RefreshCrumbs();
	RefreshTree();
	RefreshGrid();
	RefreshInfo();
	SetEnabled(m_back, m_model.CanBack());
	SetEnabled(m_forward, m_model.CanForward());
	SetEnabled(m_up, m_model.CanUp());
}

void AssetBrowserPanel::SetEnabled(ui::Widget button, bool enabled) {
	if (!button.Valid())
		return;
	button.SetEnabled(enabled);
}

void AssetBrowserPanel::RefreshCrumbs() {
	kit::ClearChildren(m_ctx, m_crumbs);
	const auto crumbs = m_model.Breadcrumb();
	for (size_t i = 0; i < crumbs.size(); ++i) {
		if (i > 0)
			(void)kit::Glyph(m_ctx, m_crumbs, ui::MaterialIcons::CHEVRON_RIGHT, m_ctx.Theme().muted, 14.f);
		const bool last = i + 1 == crumbs.size();
		ui::WidgetBuilder crumb = ui::Button(crumbs[i].first);
		crumb.Height(24.f).FontSize(13.f).Background(sdl3::FColor{0.f, 0.f, 0.f, 0.f});
		crumb.Color(last ? m_ctx.Theme().text : m_ctx.Theme().muted);
		const String location = crumbs[i].second;
		crumb.OnClick([this, location] {
			if (m_model.Navigate(location))
				MarkDirty();
		});
		m_crumbs.Append(crumb);
	}
}

void AssetBrowserPanel::RefreshTree() {
	kit::ClearChildren(m_ctx, m_tree);
	m_treeRowIndex = 0;
	AddTreeRow(AssetEntry{String("Projet"), String(AssetBrowserModel::ROOT), AssetKind::FOLDER, String()}, 0, false,
			   true);
	for (const AssetEntry &entry : m_model.List(String(AssetBrowserModel::ROOT))) {
		// « Scènes » et « Scripts » : tous leurs sous-dossiers, d'emblée.
		const bool project = entry.location == AssetBrowserModel::SCENES || entry.location == AssetBrowserModel::SCRIPTS ||
							 entry.location == AssetBrowserModel::OBJECTS;
		AddTreeBranch(entry, 1, project);
	}
}

bool AssetBrowserPanel::IsTreeOpen(const String &location, bool openByDefault) const {
	if (auto it = m_treeOpen.find(location); it != m_treeOpen.end())
		return it->second;
	const String &current = m_model.Location();
	// Un sous-dossier de `scenes/` ou `scripts/` se montre sous « Scènes » /
	// « Scripts », pas une seconde fois sous « Dossier du projet ».
	const bool underProjectItems =
		(!m_model.SceneFolder().IsEmpty() && IsOnPath(m_model.SceneFolder(), current)) ||
		(!m_model.ScriptFolder().IsEmpty() && IsOnPath(m_model.ScriptFolder(), current));
	if (location == m_model.ProjectDirectory() && underProjectItems)
		return openByDefault;
	return openByDefault || IsOnPath(location, current) ||
		   (location == AssetBrowserModel::SCENES && IsOnPath(m_model.SceneFolder(), m_model.Location())) ||
		   (location == AssetBrowserModel::SCRIPTS && IsOnPath(m_model.ScriptFolder(), m_model.Location()));
}

void AssetBrowserPanel::AddTreeBranch(const AssetEntry &folder, int depth, bool openByDefault) {
	if (depth > 12)
		return;
	const String location = m_model.Canonical(folder.location);
	std::vector<AssetEntry> children;
	for (AssetEntry &child : m_model.List(location))
		if (child.kind == AssetKind::FOLDER || child.browsable)
			children.push_back(std::move(child));
	const bool open = !children.empty() && IsTreeOpen(location, openByDefault);
	AddTreeRow(folder, depth, !children.empty(), open);
	if (!open)
		return;
	// Les DOSSIERS de « Scènes »/« Scripts » se déplient d'emblée ; une scène,
	// un objet ou un nœud seulement à la demande (ou s'ils mènent à l'endroit
	// courant).
	for (const AssetEntry &child : children)
		AddTreeBranch(child, depth + 1, openByDefault && child.kind == AssetKind::FOLDER);
}

bool AssetBrowserPanel::IsOnPath(const String &folder, const String &current) {
	return current == folder || current.StartsWith(folder + String("/"));
}

void AssetBrowserPanel::AddTreeRow(const AssetEntry &folder, int depth, bool hasChildren, bool open) {
	const String location = m_model.Canonical(folder.location);
	const bool current = location == m_model.Location();
	const ui::Palette &theme = m_ctx.Theme();
	const int rowIndex = m_treeRowIndex++;
	ui::WidgetBuilder row = ui::Selectable(String(), rowIndex);
	row.Gap(2.f).Padding(math::Sides{2.f + float(depth) * 12.f, 1.f, 4.f, 1.f}).GrowWidth();
	row.OnClick([this, location] {
		if (m_model.Navigate(location))
			MarkDirty();
	});
	row.OnContextMenu([this, folder](sdl3::FPoint p) {
		m_menuFolder = Some(folder);
		OpenMenu(p.x, p.y, true);
	});
	// Déposer des éléments sur un dossier du projet : les y déplacer.
	if (m_ops->IsWritableFolder(location)) {
		row.DropTarget(String("asset"));
		row.OnDrop([this, location](int64_t index) { OnDrop(location, index); });
	}
	// Infobulle : le chemin RELATIF au projet (ou aux ressources partagées).
	String tip = folder.location;
	if (const String &root = m_model.ProjectDirectory(); !root.IsEmpty() && tip.StartsWith(root + String("/")))
		tip = String("projet/") + tip.Substr(root.size() + 1);
	else if (AssetBrowserModel::ProjectLocation(tip).IsSome() || tip == root)
		tip = folder.path.IsValid() ? String::Format("%s — %s", ProjectPathKindName(folder.path.kind), tip.CStr())
									: folder.name;
	row.Tooltip(tip);
	ui::Widget rowEntity = m_tree.Append(row);
	if (current)
		kit::MarkSelected(m_tree, rowIndex);
	if (hasChildren && depth > 0) {
		(void)kit::IconButton(m_ctx, rowEntity, open ? ui::MaterialIcons::ARROW_DROP_DOWN : ui::MaterialIcons::ARROW_RIGHT,
							  String(open ? "Replier" : "Déplier"),
							  [this, location, open] {
								  m_treeOpen[location] = !open;
								  MarkDirty();
							  },
							  16.f, nullptr, theme.muted);
	} else {
		ui::WidgetBuilder gap = ui::Row();
		gap.Padding(0.f);
		gap.Size(16.f, 16.f).PointerThrough();
		rowEntity.Append(gap);
	}
	if (folder.kind == AssetKind::FOLDER)
		(void)kit::Glyph(m_ctx, rowEntity, open || current ? ui::MaterialIcons::FOLDER_OPEN : ui::MaterialIcons::FOLDER,
						 kit::Rgb(176, 176, 180), 15.f);
	else // scène, objet ou nœud parcourus comme des dossiers : leur icône
		(void)kit::Glyph(m_ctx, rowEntity, IconOf(folder.kind), ColorOf(folder.kind), 15.f);
	ui::WidgetBuilder label = ui::Label(folder.name);
	label.GrowWidth().FontSize(13.f).Ellipsis().PointerThrough();
	rowEntity.Append(label);
}

void AssetBrowserPanel::RefreshGrid() {
	kit::ClearChildren(m_ctx, m_grid);
	ClearThumbnails();
	m_tiles.assign(m_entries.size(), ui::Widget());
	const int columns = ColumnsFor(m_gridWidth > 0.f ? m_gridWidth : 600.f);
	ui::Widget row{};
	for (size_t i = 0; i < m_entries.size(); ++i) {
		if (i % size_t(columns) == 0) {
			ui::WidgetBuilder rowBuilder = ui::Row();
			rowBuilder.Padding(0.f);
			rowBuilder.Gap(TILE_GAP);
			row = m_grid.Append(rowBuilder);
		}
		AddTile(row, i);
	}
	if (m_entries.empty())
		(void)kit::Caption(m_ctx, m_grid, m_filter.IsEmpty() ? String("Dossier vide") : String("Aucun résultat"), 13.f);
}

void AssetBrowserPanel::AddTile(ui::Widget row, size_t index) {
	const AssetEntry &entry = m_entries[index];
	const ui::Palette &theme = m_ctx.Theme();
	const bool selected = IsSelected(entry.location);
	const float thumb = m_tileSize;

	ui::WidgetBuilder tile = ui::Button(String());
	tile.Size(thumb + 8.f, thumb + 26.f).BorderRadius(4.f).Tooltip(TooltipOf(entry));
	tile.Background(selected ? kit::PaletteOf(m_ctx).selection : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	tile.OnClick([this, index] { OnTileClick(index); });
	tile.OnDoubleClick([this, index] { Activate(index); });
	tile.OnContextMenu(ui::EventCallback([this, index](const ui::WidgetEvent &event) {
		event.StopPropagation(); // la grille ouvrirait sinon le menu du dossier
		// Clic droit hors sélection : il la remplace (comme tout explorateur).
		if (index < m_entries.size() && !IsSelected(m_entries[index].location))
			SelectLocations({m_entries[index].location});
		OpenMenu(event.pointer.x, event.pointer.y, false);
	}));
	// Glisser : la vignette, ou toute la sélection si elle en fait partie ;
	// le fantôme dit combien d'éléments partent.
	tile.DragSource(String("asset"), int64_t(index), entry.name);
	tile.OnDragStart([this, index] {
		if (index >= m_entries.size() || index >= m_tiles.size())
			return;
		const bool many = IsSelected(m_entries[index].location) && m_selection.size() > 1;
		if (auto payload = m_tiles[index].Get<ui::DragSource>(); payload.IsSome()) {
			payload.Unwrap()->count = many ? int(m_selection.size()) : 1;
			payload.Unwrap()->label = many ? String::Format("%d éléments", int(m_selection.size())) : m_entries[index].name;
		}
	});
	// Un dossier du projet reçoit les éléments qu'on y dépose.
	if (entry.kind == AssetKind::FOLDER && m_ops->IsWritableFolder(entry.location)) {
		const String location = m_model.Canonical(entry.location);
		tile.DropTarget(String("asset"));
		tile.OnDrop([this, location](int64_t dragged) { OnDrop(location, dragged); });
	}
	ui::Widget tileEntity = row.Append(tile);
	m_tiles[index] = tileEntity;

	// Vignette : aperçu réel pour les modèles et les textures, grande
	// icône sinon.
	ui::WidgetBuilder frame = ui::Column();
	frame.Padding(0.f);
	frame.Size(thumb, thumb).Absolute().Anchor(ui::Anchor::TopLeft).Offset(4.f, 4.f).BorderRadius(3.f).Clip();
	frame.Background(entry.kind == AssetKind::MODEL || entry.kind == AssetKind::TEXTURE ? kit::PaletteOf(m_ctx).well
																				  : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	frame.PointerThrough();
	ui::Widget frameEntity = tileEntity.Append(frame);
	if (!AddPreview(frameEntity, entry, thumb)) {
		ui::WidgetBuilder icon = ui::Icon(IconOf(entry.kind), thumb * 0.62f);
		icon.Absolute().Anchor(ui::Anchor::Center).JustifyContent(ui::Justify::Center).PointerThrough();
		icon.Color(kit::Readable(m_ctx, ColorOf(entry.kind)));
		frameEntity.Append(icon);
	}
	if (entry.kind == AssetKind::SCRIPT) {
		// Pastille de compilation, comme les coches de la maquette.
		ui::WidgetBuilder mark = ui::Icon(entry.broken ? ui::MaterialIcons::ERROR : ui::MaterialIcons::CHECK_CIRCLE, 18.f);
		mark.Absolute().Anchor(ui::Anchor::TopRight).Offset(-2.f, 2.f).PointerThrough();
		mark.Color(entry.broken ? kit::PaletteOf(m_ctx).error : kit::PaletteOf(m_ctx).ok);
		frameEntity.Append(mark);
	}

	ui::WidgetBuilder name = ui::Label(entry.name);
	name.Absolute().Anchor(ui::Anchor::BottomLeft).Offset(2.f, -3.f).Width(thumb + 4.f);
	name.FontSize(12.f).TextAlign(ui::TextAlign::Center).Ellipsis().PointerThrough();
	name.Color(theme.text);
	tileEntity.Append(name);
}

bool AssetBrowserPanel::AddPreview(ui::Widget frame, const AssetEntry &entry, float size) {
	if (entry.kind == AssetKind::TEXTURE && m_ctx.renderer) {
		ui::WidgetBuilder image = ui::Image(entry.location);
		image.Size(size, size).ObjectFit(ui::ObjectFit::Contain).PointerThrough();
		frame.Append(image);
		return true;
	}
	if (entry.kind == AssetKind::MODEL && m_ctx.renderer && entry.location.ToLower().EndsWith(".gltf")) {
		render3d::Object3D *root = ThumbnailFor(entry.location);
		if (!root)
			return false;
		ui::ViewportLighting lighting;
		lighting.sun.direction = math::FVector3{-0.5f, -0.7f, -0.6f};
		lighting.sun.intensity = 1.1f;
		lighting.ambient.color = sdl3::Color{64, 64, 70, 255};
		lighting.background = sdl3::Color{20, 20, 23, 255};
		ui::WidgetBuilder view = ui::Viewport3D(*root, ThumbnailCamera(entry.location), false, Some(lighting));
		view.Size(size, size).PointerThrough();
		frame.Append(view);
		return true;
	}
	return false;
}

render3d::Object3D * AssetBrowserPanel::ThumbnailFor(const String &path) {
	if (auto it = m_thumbnailRoots.find(path); it != m_thumbnailRoots.end())
		return it->second.get();
	// Même ressource que les nœuds qui montrent ce modèle : la vignette ne
	// relit pas le fichier et partage sa géométrie.
	resources::ResourceCache &cache = m_ctx.runtime.Resources();
	auto resource = cache.GetReady(render3d::ImportMesh(cache, path, resources::LoadPolicy::SYNC));
	if (!resource)
		return nullptr;
	std::shared_ptr<render3d::Mesh> mesh = resource->SharedGeometry();
	auto root = std::make_unique<render3d::Object3D>();
	const math::FAABB bounds = mesh->LocalBounds();
	render3d::Material material = render3d::Material::Plastic(sdl3::Color{196, 184, 166, 255});
	auto &shape = root->Add(std::make_unique<render3d::Shape>(std::move(mesh), std::move(material)));
	if (bounds.IsValid())
		shape.SetPosition(-bounds.Center());
	m_thumbnailBounds[path] = bounds;
	render3d::Object3D *raw = root.get();
	m_thumbnailRoots[path] = std::move(root);
	return raw;
}

render3d::Camera AssetBrowserPanel::ThumbnailCamera(const String &path) const {
	render3d::Camera camera;
	float radius = 1.f;
	if (auto it = m_thumbnailBounds.find(path); it != m_thumbnailBounds.end() && it->second.IsValid())
		radius = sdl3::Max(0.01f, (it->second.max - it->second.min).Length() * 0.5f);
	const math::FVector3 direction = math::FVector3{0.8f, 0.55f, 1.f}.Normalize();
	camera.position = direction * (radius * 2.3f);
	camera.target = math::FVector3{0.f, 0.f, 0.f};
	camera.nearPlane = radius * 0.02f;
	camera.farPlane = radius * 10.f;
	return camera;
}

void AssetBrowserPanel::ClearThumbnails() {
	// Les widgets qui référencent ces racines ont été détruits avec la
	// grille ; les racines elles-mêmes restent en cache (cf. ThumbnailFor).
}

void AssetBrowserPanel::OnTileClick(size_t index) {
	if (index >= m_entries.size())
		return;
	// Ctrl : ajouter / retirer ; Maj : plage depuis l'ancre ; sinon : seule.
	const SDL_Keymod mods = SDL_GetModState();
	const bool ctrl = (mods & SDL_KMOD_CTRL) != 0, shift = (mods & SDL_KMOD_SHIFT) != 0;
	const String &location = m_entries[index].location;
	if (shift && !m_anchor.IsEmpty()) {
		size_t from = index;
		for (size_t i = 0; i < m_entries.size(); ++i)
			if (m_entries[i].location == m_anchor)
				from = i;
		std::vector<String> range = ctrl ? m_selection : std::vector<String>{};
		for (size_t i = sdl3::Min(from, index); i <= sdl3::Max(from, index); ++i)
			if (std::find(range.begin(), range.end(), m_entries[i].location) == range.end())
				range.push_back(m_entries[i].location);
		m_selection = std::move(range); // l'ancre reste : Maj+clic suivant repart d'elle
	} else if (ctrl) {
		if (IsSelected(location))
			std::erase(m_selection, location);
		else
			m_selection.push_back(location);
		m_anchor = location;
	} else {
		m_selection = {location};
		m_anchor = location;
	}
	MarkDirty();
}

void AssetBrowserPanel::RefreshInfo() {
	if (!m_info.Valid())
		return;
	const std::vector<AssetEntry> selected = SelectedEntries();
	if (selected.empty()) {
		kit::SetLabelText(m_ctx, m_info, String::Format("%d élément(s)", int(m_entries.size())));
		return;
	}
	if (selected.size() > 1) {
		kit::SetLabelText(m_ctx, m_info, String::Format("%d éléments sélectionnés sur %d — glisser sur un dossier pour "
														"les déplacer",
														int(selected.size()), int(m_entries.size())));
		return;
	}
	const AssetEntry &entry = selected.front();
	// Le chemin typé dit ce qu'est l'élément sans ambiguïté (« Objet
	// /objects/Torche », « Nœud (scène) /scenes/Donjon/Joueur »).
	const String what = entry.path.IsValid() ? String::Format("%s %s", ProjectPathKindName(entry.path.kind),
															   entry.path.ToString().CStr())
											 : String::Format("%s · %s", entry.name.CStr(), AssetKindLabel(entry.kind));
	kit::SetLabelText(m_ctx, m_info, String::Format("%s%s%s — double-clic pour ouvrir", what.CStr(),
													entry.detail.IsEmpty() ? "" : " · ", entry.detail.CStr()));
}

String AssetBrowserPanel::TooltipOf(const AssetEntry &entry) {
	String tip = entry.kind == AssetKind::SCRIPT && entry.broken ? String::Format("%s — ne compile pas", entry.name.CStr())
				 : entry.detail.IsEmpty()						  ? entry.name
														  : String::Format("%s — %s", entry.name.CStr(), entry.detail.CStr());
	if (entry.path.IsValid())
		tip = tip + String::Format("\n%s : %s", ProjectPathKindName(entry.path.kind), entry.path.ToString().CStr());
	return tip;
}

ui::MaterialIcons AssetBrowserPanel::IconOf(AssetKind kind) noexcept {
	switch (kind) {
		case AssetKind::FOLDER:
			return ui::MaterialIcons::FOLDER;
		case AssetKind::SCENE:
			return ui::MaterialIcons::LAYERS;
		case AssetKind::OBJECT:
			return ui::MaterialIcons::CATEGORY;
		case AssetKind::SCRIPT:
			return ui::MaterialIcons::DESCRIPTION;
		case AssetKind::NODE:
			return ui::MaterialIcons::ACCOUNT_TREE;
		case AssetKind::MODEL:
			return ui::MaterialIcons::VIEW_IN_AR;
		case AssetKind::TEXTURE:
			return ui::MaterialIcons::IMAGE;
		case AssetKind::SOUND:
			return ui::MaterialIcons::AUDIOTRACK;
		case AssetKind::FONT:
			return ui::MaterialIcons::TEXTURE;
		case AssetKind::SHADER:
			return ui::MaterialIcons::CODE;
		case AssetKind::DATA:
			return ui::MaterialIcons::DATA_OBJECT;
		case AssetKind::OTHER:
			break;
	}
	return ui::MaterialIcons::DESCRIPTION;
}

sdl3::FColor AssetBrowserPanel::ColorOf(AssetKind kind) noexcept {
	switch (kind) {
		case AssetKind::FOLDER:
			return kit::Rgb(198, 198, 202);
		case AssetKind::SCENE:
			return kit::Rgb(126, 172, 232);
		case AssetKind::OBJECT:
			return kit::Rgb(232, 176, 92);
		case AssetKind::SCRIPT:
			return kit::Rgb(210, 210, 214);
		case AssetKind::NODE:
			return kit::Rgb(150, 200, 160);
		case AssetKind::SOUND:
			return kit::Rgb(206, 150, 226);
		case AssetKind::SHADER:
			return kit::Rgb(120, 206, 190);
		case AssetKind::DATA:
			return kit::Rgb(236, 196, 96);
		default:
			break;
	}
	return kit::Rgb(170, 176, 190);
}

} // namespace sandbox
