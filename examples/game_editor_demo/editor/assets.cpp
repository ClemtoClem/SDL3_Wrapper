// Définitions de assets.hpp
#include "assets.hpp"

#include "asset_ops.hpp"
#include "../document/project_files.hpp"

namespace game_editor {

const char * AssetKindLabel(AssetKind kind) noexcept {
	switch (kind) {
		case AssetKind::FOLDER:
			return "Dossier";
		case AssetKind::SCENE:
			return "Scène";
		case AssetKind::SCRIPT:
			return "Script";
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

String AssetBrowserModel::Canonical(const String &location) const {
	if (m_projectDir.IsEmpty())
		return location;
	if (location == SceneFolder())
		return String(SCENES);
	if (location == ScriptFolder())
		return String(SCRIPTS);
	return location;
}

String AssetBrowserModel::DiskFolderOf(const String &location) const {
	if (location == SCENES)
		return SceneFolder();
	if (location == SCRIPTS)
		return ScriptFolder();
	if (location.StartsWith(ROOT))
		return String();
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
	if (location == ROOT || location == SCENES || location == SCRIPTS)
		return String(ROOT);
	// Sous-dossier de `scenes/` ou `scripts/` : son parent, canonique (le
	// dossier lui-même remonte à « Scènes » / « Scripts »).
	for (const String &folder : {SceneFolder(), ScriptFolder()})
		if (!folder.IsEmpty() && location.StartsWith(folder + String("/"))) {
			const size_t slash = LastSlash(location);
			return Canonical(location.Substr(0, slash));
		}
	if (location.StartsWith(ROOT) || location == m_savesRoot)
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
									 String::Format("%d scène(s) du projet", m_project ? int(m_project->scenes.size()) : 0)});
		entries.push_back(AssetEntry{String("Scripts"), String(SCRIPTS), AssetKind::FOLDER,
									 String("Bibliothèque de scripts du projet")});
		if (!m_savesRoot.IsEmpty() && IsFolder(m_savesRoot))
			entries.push_back(AssetEntry{m_savesLabel, m_savesRoot, AssetKind::FOLDER,
										 String("Fichiers du projet : assets, scènes, scripts")});
		for (AssetEntry &entry : ListDisk(m_diskRoot))
			if (entry.kind == AssetKind::FOLDER)
				entries.push_back(std::move(entry));
	} else if (location == SCENES) {
		if (m_project)
			for (const SceneDesc &scene : m_project->scenes) {
				AssetEntry entry{scene.name, String(SCENES) + String("/") + scene.name, AssetKind::SCENE,
								 String::Format("%d objets", int(scene.ObjectCount()))};
				entry.managed = true;
				entries.push_back(std::move(entry));
			}
		AppendDiskExtras(entries, SceneFolder(), true);
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
				AssetEntry entry{script.name + String(".script"), String(SCRIPTS) + String("/") + script.name,
								 AssetKind::SCRIPT, describe(outline, script.description)};
				entry.broken = outline.error.IsSome() || outline.role == ScriptRole::INVALID;
				entry.managed = true;
				entries.push_back(std::move(entry));
			}
			// Le script de JEU de chaque scène, adressé par `@scène`.
			for (const SceneDesc &scene : m_project->scenes) {
				if (scene.gameplayScript.IsEmpty())
					continue;
				const ScriptOutline outline = OutlineScript(scene.gameplayScript, ScriptUse::SCENE, modules);
				AssetEntry entry{scene.name + String(".main.script"),
								 String(SCRIPTS) + String("/@") + scene.name, AssetKind::SCRIPT,
								 describe(outline, String::Format("script de la scène « %s »", scene.name.CStr()))};
				entry.broken = outline.error.IsSome() || outline.role == ScriptRole::INVALID;
				entry.managed = true;
				entries.push_back(std::move(entry));
			}
		}
		AppendDiskExtras(entries, ScriptFolder(), false);
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
			for (const SceneDesc &scene : m_project->scenes)
				owned.push_back(files::SafeFileName(scene.name) + String(".scene"));
		} else {
			for (const ScriptAsset &script : m_project->scripts)
				owned.push_back(files::SafeFileName(script.name) + String(".script"));
			for (const SceneDesc &scene : m_project->scenes)
				if (!scene.gameplayScript.Trim().IsEmpty())
					owned.push_back(files::SafeFileName(scene.name) + String(files::GAMEPLAY_SUFFIX));
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
	if (location == ROOT || location == SCENES || location == SCRIPTS)
		return true;
	if (location.StartsWith(ROOT))
		return false;
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

// ── AssetBrowserPanel ────────────────────────────────────────────────────────

AssetBrowserPanel::AssetBrowserPanel(UiContext &ctx, AssetActions actions)
	: m_ctx(ctx), m_actions(std::move(actions)), m_ops(std::make_unique<AssetOperations>(ctx.runtime, m_model)) {}

AssetBrowserPanel::~AssetBrowserPanel() {
	ClearThumbnails();
}

void AssetBrowserPanel::Build(ecs::Entity page) {
	m_model.SetProject(&m_ctx.runtime.GetProject());
	m_model.SetProjectDirectory(m_ctx.runtime.ProjectDirectory());
	m_page = page;
	const ui::UiTheme &theme = m_ctx.Theme();

	// ── Barre : ← → ↑ · fil d'Ariane · recherche · + ≡ ────────────────────
	ui::WidgetBuilder bar = m_ctx.factory.Row();
	bar.Pad(0.f);
	bar.Gap(2.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(page);
	ecs::Entity barEntity = bar.Spawn();
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
	ui::WidgetBuilder crumbs = m_ctx.factory.Row();
	crumbs.Pad(0.f);
	crumbs.Gap(0.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Clip().Parent(barEntity);
	m_crumbs = crumbs.Spawn();
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
	ui::WidgetBuilder body = m_ctx.factory.Row();
	body.Pad(0.f);
	body.Gap(0.f).GrowW().GrowH().Parent(page);
	ecs::Entity bodyEntity = body.Spawn();

	ui::WidgetBuilder tree = m_ctx.factory.Column();
	tree.Gap(1.f).Pad(math::Sides{2.f, 4.f}).W(ui::Dimension::Px(170.f)).GrowH().Scrollable().Clip();
	tree.Bg(kit::PaletteOf(m_ctx).toolbar).Parent(bodyEntity);
	m_tree = tree.Spawn();

	ui::WidgetBuilder right = m_ctx.factory.Column();
	right.Gap(0.f).Pad(0.f).GrowW().GrowH().Parent(bodyEntity);
	ecs::Entity rightEntity = right.Spawn();

	ui::WidgetBuilder grid = m_ctx.factory.Column();
	grid.Gap(TILE_GAP).Pad(math::Sides{8.f, 6.f}).GrowW().GrowH().Scrollable().Clip().Parent(rightEntity);
	// Clic droit dans le vide de la grille : menu du dossier courant.
	grid.OnContextMenu([this](float x, float y) { OpenMenu(x, y, false); });
	m_grid = grid.Spawn();

	ui::WidgetBuilder footer = m_ctx.factory.Row();
	footer.Gap(8.f).Pad(math::Sides{8.f, 2.f}).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(rightEntity);
	ecs::Entity footerEntity = footer.Spawn();
	ui::WidgetBuilder info = m_ctx.factory.Label(String());
	info.GrowW().HAuto().FontSize(12.f).TextColor(theme.muted).TextEllipsis().Parent(footerEntity);
	m_info = info.Spawn();
	(void)kit::Glyph(m_ctx, footerEntity, ui::MaterialIcons::VIEW_QUILT, theme.muted, 14.f);
	ui::WidgetBuilder zoom = m_ctx.factory.Slider(MIN_TILE, MAX_TILE, m_tileSize);
	zoom.Size(110.f, 14.f).Tooltip(String("Taille des vignettes")).Parent(footerEntity);
	zoom.OnChange([this](float value) {
		if (sdl3::Abs(value - m_tileSize) >= 4.f) {
			m_tileSize = value;
			MarkDirty();
		}
	});
	(void)zoom.Spawn();

	BuildMenus();
	BuildDialogs();
	MarkDirty();
}

void AssetBrowserPanel::Teardown() {
	ClearThumbnails();
	for (ecs::Entity popup : m_popups)
		if (popup.Valid())
			ui::DespawnTree(m_ctx.registry, popup);
	m_popups.clear();
	m_tiles.clear();
	m_page = m_tree = m_grid = m_crumbs = m_info = m_search = ecs::Entity{};
	m_newFolderButton = m_renameButton = m_duplicateButton = m_deleteButton = ecs::Entity{};
	m_menu = m_nameModal = m_nameInput = m_nameTitle = m_confirmModal = m_confirmText = m_confirmTitle = ecs::Entity{};
}

void AssetBrowserPanel::Tick(float dt) {
	m_clock += dt;
	if (!m_grid.Valid())
		return;
	if (auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(m_grid); computed.IsSome()) {
		const float width = computed.Unwrap()->screen.w;
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
	ui::WidgetBuilder title = m_ctx.factory.Label(String("Nom"));
	title.FontSize(15.f).Bold().GrowW().HAuto();
	ui::WidgetBuilder input = m_ctx.factory.Input();
	input.GrowW().H(ui::Dimension::Px(28.f)).OnSubmit([this](const String &text) { ApplyNameDialog(text); });
	ui::WidgetBuilder ok = m_ctx.factory.Button(String("Valider"));
	ok.WAuto().HAuto().OnClick([this] {
		if (auto field = m_ctx.registry.GetComponent<ui::UiInput>(m_nameInput); field.IsSome())
			ApplyNameDialog(field.Unwrap()->text);
	});
	ui::WidgetBuilder cancel = m_ctx.factory.Button(String("Annuler"));
	cancel.WAuto().HAuto().OnClick([this] {
		m_ctx.gui.CloseModal(m_nameModal);
		m_ctx.gui.ClearKeyboardFocus(); // le champ caché ne doit pas garder le clavier
		m_menuFolder = NONE;
	});
	ui::WidgetBuilder buttons = m_ctx.factory.Row();
	buttons.Pad(0.f);
	buttons.Gap(8.f).GrowW().HAuto().Justify(ui::Justify::End).Children(std::move(cancel), std::move(ok));
	ui::WidgetBuilder panel = m_ctx.factory.Panel();
	panel.Size(420.f, 150.f).Pad(16.f).Gap(12.f).Children(std::move(title), std::move(input), std::move(buttons));
	m_nameModal = m_ctx.factory.Modal(std::move(panel)).Spawn();
	m_popups.push_back(m_nameModal);

	// Confirmation de suppression.
	ui::WidgetBuilder heading = m_ctx.factory.Label(String("Supprimer ?"));
	heading.FontSize(15.f).Bold().GrowW().HAuto();
	ui::WidgetBuilder text = m_ctx.factory.Label(String());
	text.GrowW().H(ui::Dimension::Px(92.f)).FontSize(13.f).TextWrap();
	ui::WidgetBuilder yes = m_ctx.factory.Button(String("Supprimer"));
	yes.WAuto().HAuto().Bg(kit::PaletteOf(m_ctx).error).OnClick([this] {
		m_ctx.gui.CloseModal(m_confirmModal);
		m_ctx.gui.ClearKeyboardFocus();
		(void)DeleteSelection();
		m_menuFolder = NONE;
	});
	ui::WidgetBuilder no = m_ctx.factory.Button(String("Annuler"));
	no.WAuto().HAuto().OnClick([this] {
		m_ctx.gui.CloseModal(m_confirmModal);
		m_ctx.gui.ClearKeyboardFocus();
		m_menuFolder = NONE;
	});
	ui::WidgetBuilder confirmButtons = m_ctx.factory.Row();
	confirmButtons.Pad(0.f);
	confirmButtons.Gap(8.f).GrowW().HAuto().Justify(ui::Justify::End).Children(std::move(no), std::move(yes));
	ui::WidgetBuilder confirm = m_ctx.factory.Panel();
	confirm.Size(480.f, 220.f).Pad(16.f).Gap(10.f).Children(std::move(heading), std::move(text),
															  std::move(confirmButtons));
	m_confirmModal = m_ctx.factory.Modal(std::move(confirm)).Spawn();
	m_popups.push_back(m_confirmModal);

	m_ctx.registry.Query<ui::UiInput, ui::UiParent>([this](ecs::Entity e, ui::UiInput &, ui::UiParent &) {
		if (ui::IsDescendantOrSelf(m_ctx.registry, e, m_nameModal))
			m_nameInput = e;
	});
	// Les libellés à mettre à jour : le titre de la boîte de nom, le texte
	// de la confirmation (premiers libellés de chaque panneau).
	m_ctx.registry.Query<ui::UiLabel, ui::UiParent>([this](ecs::Entity e, ui::UiLabel &label, ui::UiParent &) {
		if (!m_nameTitle.Valid() && label.text == "Nom" && ui::IsDescendantOrSelf(m_ctx.registry, e, m_nameModal))
			m_nameTitle = e;
		if (!m_confirmText.Valid() && label.text.IsEmpty() && ui::IsDescendantOrSelf(m_ctx.registry, e, m_confirmModal))
			m_confirmText = e;
		if (!m_confirmTitle.Valid() && label.text == "Supprimer ?" &&
			ui::IsDescendantOrSelf(m_ctx.registry, e, m_confirmModal))
			m_confirmTitle = e;
	});
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
	if (auto field = m_ctx.registry.GetComponent<ui::UiInput>(m_nameInput); field.IsSome()) {
		field.Unwrap()->text = String("Nouveau dossier");
		field.Unwrap()->cursor = field.Unwrap()->text.size();
		field.Unwrap()->selectionAnchor = 0;
		field.Unwrap()->focused = true;
	}
	m_ctx.gui.OpenModal(m_nameModal);
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
	if (auto field = m_ctx.registry.GetComponent<ui::UiInput>(m_nameInput); field.IsSome()) {
		field.Unwrap()->text = current;
		field.Unwrap()->cursor = current.size();
		field.Unwrap()->selectionAnchor = 0;
		field.Unwrap()->focused = true;
	}
	m_ctx.gui.OpenModal(m_nameModal);
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
		managed += targets[i].managed ? 1 : 0;
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
	m_ctx.gui.OpenModal(m_confirmModal);
}

void AssetBrowserPanel::ApplyNameDialog(const String &text) {
	m_ctx.gui.CloseModal(m_nameModal);
	m_ctx.gui.ClearKeyboardFocus(); // le champ caché ne doit pas garder le clavier
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

ecs::Entity AssetBrowserPanel::MenuItem(const char *text, const char *shortcut, std::function<void()> action) {
	ui::WidgetBuilder item = m_ctx.factory.MenuItem(String(text), String(shortcut));
	item.OnClick(std::move(action)).Parent(m_menu);
	return item.Spawn();
}

void AssetBrowserPanel::BuildMenus() {
	m_menu = m_ctx.factory.ContextMenu();
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
	SetEnabled(m_menuNewFolder, m_ops->IsWritableFolder(folder));
	SetEnabled(m_menuRename, allowed("rename"));
	SetEnabled(m_menuDuplicate, m_menuFolder.IsNone() && allowed("duplicate"));
	SetEnabled(m_menuCut, m_menuFolder.IsNone() && allowed("move"));
	SetEnabled(m_menuPaste, !m_clipboard.empty() && m_ops->IsWritableFolder(folder));
	SetEnabled(m_menuDelete, allowed("delete"));
	SetEnabled(m_menuSelectAll, m_menuFolder.IsNone() && !m_entries.empty());
	m_ctx.gui.OpenPopupAt(m_menu, sdl3::FPoint{x, y});
}

// ── Clavier ──────────────────────────────────────────────────────────────────

bool AssetBrowserPanel::PointerOver() const {
	if (!m_page.Valid() || m_ctx.gui.HasOpenModal())
		return false;
	auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(m_page);
	if (computed.IsNone() || computed.Unwrap()->screen.w <= 0.f)
		return false;
	float x = 0.f, y = 0.f;
	(void)SDL_GetMouseState(&x, &y);
	return computed.Unwrap()->screen.Contains(sdl3::FPoint{x, y});
}

bool AssetBrowserPanel::HandleKey(const sdl3::Event &event) {
	if (!event.IsKeyDown() || !PointerOver())
		return false;
	// Saisie en cours (filtre) : les touches sont les siennes. Un bouton qui
	// a gardé le focus après un clic (une vignette) ne compte pas.
	const ecs::Entity focus = m_ctx.gui.KeyboardFocus();
	if (focus.Valid() && (m_ctx.registry.HasComponent<ui::UiInput>(focus) ||
						  m_ctx.registry.HasComponent<ui::UiInputArea>(focus)))
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
			if (entry.location.StartsWith(AssetBrowserModel::ROOT)) {
				if (m_actions.openScene)
					m_actions.openScene(entry.name);
			} else if (m_actions.instantiateScene) {
				m_actions.instantiateScene(entry.location); // scène emballée (.scene)
			}
			return;
		case AssetKind::SCRIPT:
			if (entry.location.StartsWith(String(AssetBrowserModel::SCRIPTS) + String("/@"))) {
				if (m_actions.openSceneScript)
					m_actions.openSceneScript(entry.location.Substr(String(AssetBrowserModel::SCRIPTS).size() + 2));
			} else if (entry.location.StartsWith(AssetBrowserModel::SCRIPTS)) {
				if (m_actions.openScript)
					m_actions.openScript(entry.location.Substr(String(AssetBrowserModel::SCRIPTS).size() + 1));
			} else if (m_actions.openFile) {
				m_actions.openFile(entry.location);
			}
			return;
		case AssetKind::MODEL:
			if (m_actions.importModel)
				m_actions.importModel(entry.location);
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

void AssetBrowserPanel::SetEnabled(ecs::Entity button, bool enabled) {
	if (!button.Valid())
		return;
	if (enabled)
		m_ctx.registry.RemoveComponent<ui::UiDisabled>(button);
	else if (!m_ctx.registry.HasComponent<ui::UiDisabled>(button))
		m_ctx.registry.AddComponent(button, ui::UiDisabled{});
}

void AssetBrowserPanel::RefreshCrumbs() {
	kit::ClearChildren(m_ctx, m_crumbs);
	const auto crumbs = m_model.Breadcrumb();
	for (size_t i = 0; i < crumbs.size(); ++i) {
		if (i > 0)
			(void)kit::Glyph(m_ctx, m_crumbs, ui::MaterialIcons::CHEVRON_RIGHT, m_ctx.Theme().muted, 14.f);
		const bool last = i + 1 == crumbs.size();
		ui::WidgetBuilder crumb = m_ctx.factory.Button(crumbs[i].first);
		crumb.WAuto().H(ui::Dimension::Px(24.f)).FontSize(13.f).Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f});
		crumb.TextColor(last ? m_ctx.Theme().text : m_ctx.Theme().muted).Parent(m_crumbs);
		const String location = crumbs[i].second;
		crumb.OnClick([this, location] {
			if (m_model.Navigate(location))
				MarkDirty();
		});
		(void)crumb.Spawn();
	}
}

void AssetBrowserPanel::RefreshTree() {
	kit::ClearChildren(m_ctx, m_tree);
	m_treeRowIndex = 0;
	AddTreeRow(AssetEntry{String("Projet"), String(AssetBrowserModel::ROOT), AssetKind::FOLDER, String()}, 0, false,
			   true);
	for (const AssetEntry &entry : m_model.List(String(AssetBrowserModel::ROOT))) {
		// « Scènes » et « Scripts » : tous leurs sous-dossiers, d'emblée.
		const bool project = entry.location == AssetBrowserModel::SCENES || entry.location == AssetBrowserModel::SCRIPTS;
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
		if (child.kind == AssetKind::FOLDER)
			children.push_back(std::move(child));
	const bool open = !children.empty() && IsTreeOpen(location, openByDefault);
	AddTreeRow(folder, depth, !children.empty(), open);
	if (!open)
		return;
	for (const AssetEntry &child : children)
		AddTreeBranch(child, depth + 1, openByDefault);
}

bool AssetBrowserPanel::IsOnPath(const String &folder, const String &current) {
	return current == folder || current.StartsWith(folder + String("/"));
}

void AssetBrowserPanel::AddTreeRow(const AssetEntry &folder, int depth, bool hasChildren, bool open) {
	const String location = m_model.Canonical(folder.location);
	const bool current = location == m_model.Location();
	const ui::UiTheme &theme = m_ctx.Theme();
	ui::WidgetBuilder row = m_ctx.factory.Selectable(String(), m_treeRowIndex++);
	row.Gap(2.f).Pad(math::Sides{2.f + float(depth) * 12.f, 1.f, 4.f, 1.f}).GrowW().HAuto().Parent(m_tree);
	row.OnClick([this, location] {
		if (m_model.Navigate(location))
			MarkDirty();
	});
	row.OnContextMenu([this, folder](float x, float y) {
		m_menuFolder = Some(folder);
		OpenMenu(x, y, true);
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
	else if (tip.StartsWith(AssetBrowserModel::ROOT) || tip == root)
		tip = folder.name;
	row.Tooltip(tip);
	ecs::Entity rowEntity = row.Spawn();
	if (auto selectable = m_ctx.registry.GetComponent<ui::UiSelectable>(rowEntity); selectable.IsSome())
		selectable.Unwrap()->selected = current;
	if (hasChildren && depth > 0) {
		(void)kit::IconButton(m_ctx, rowEntity, open ? ui::MaterialIcons::ARROW_DROP_DOWN : ui::MaterialIcons::ARROW_RIGHT,
							  String(open ? "Replier" : "Déplier"),
							  [this, location, open] {
								  m_treeOpen[location] = !open;
								  MarkDirty();
							  },
							  16.f, nullptr, theme.muted);
	} else {
		ui::WidgetBuilder gap = m_ctx.factory.Row();
		gap.Pad(0.f);
		gap.Size(16.f, 16.f).PointerThrough().Parent(rowEntity);
		(void)gap.Spawn();
	}
	(void)kit::Glyph(m_ctx, rowEntity, open || current ? ui::MaterialIcons::FOLDER_OPEN : ui::MaterialIcons::FOLDER,
					 kit::Rgb(176, 176, 180), 15.f);
	ui::WidgetBuilder label = m_ctx.factory.Label(folder.name);
	label.GrowW().HAuto().FontSize(13.f).TextEllipsis().PointerThrough().Parent(rowEntity);
	(void)label.Spawn();
}

void AssetBrowserPanel::RefreshGrid() {
	kit::ClearChildren(m_ctx, m_grid);
	ClearThumbnails();
	m_tiles.assign(m_entries.size(), ecs::Entity{});
	const int columns = ColumnsFor(m_gridWidth > 0.f ? m_gridWidth : 600.f);
	ecs::Entity row{};
	for (size_t i = 0; i < m_entries.size(); ++i) {
		if (i % size_t(columns) == 0) {
			ui::WidgetBuilder rowBuilder = m_ctx.factory.Row();
			rowBuilder.Pad(0.f);
			rowBuilder.Gap(TILE_GAP).WAuto().HAuto().Parent(m_grid);
			row = rowBuilder.Spawn();
		}
		AddTile(row, i);
	}
	if (m_entries.empty())
		(void)kit::Caption(m_ctx, m_grid, m_filter.IsEmpty() ? String("Dossier vide") : String("Aucun résultat"), 13.f);
}

void AssetBrowserPanel::AddTile(ecs::Entity row, size_t index) {
	const AssetEntry &entry = m_entries[index];
	const ui::UiTheme &theme = m_ctx.Theme();
	const bool selected = IsSelected(entry.location);
	const float thumb = m_tileSize;

	ui::WidgetBuilder tile = m_ctx.factory.Button(String());
	tile.Size(thumb + 8.f, thumb + 26.f).Radius(4.f).Tooltip(TooltipOf(entry)).Parent(row);
	tile.Bg(selected ? kit::PaletteOf(m_ctx).selection : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	tile.OnClick([this, index] { OnTileClick(index); });
	tile.OnDoubleClick([this, index] { Activate(index); });
	tile.OnContextMenu([this, index](float x, float y) {
		// Clic droit hors sélection : il la remplace (comme tout explorateur).
		if (index < m_entries.size() && !IsSelected(m_entries[index].location))
			SelectLocations({m_entries[index].location});
		OpenMenu(x, y, false);
	});
	// Glisser : la vignette, ou toute la sélection si elle en fait partie ;
	// le fantôme dit combien d'éléments partent.
	tile.DragPayload(String("asset"), int64_t(index), entry.name);
	tile.OnDragStart([this, index] {
		if (index >= m_entries.size() || index >= m_tiles.size())
			return;
		const bool many = IsSelected(m_entries[index].location) && m_selection.size() > 1;
		if (auto payload = m_ctx.registry.GetComponent<ui::UiDragPayload>(m_tiles[index]); payload.IsSome()) {
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
	ecs::Entity tileEntity = tile.Spawn();
	m_tiles[index] = tileEntity;

	// Vignette : aperçu réel pour les modèles et les textures, grande
	// icône sinon.
	ui::WidgetBuilder frame = m_ctx.factory.Column();
	frame.Pad(0.f);
	frame.Size(thumb, thumb).Absolute().Anchor(ui::Anchor::TopLeft).Offset(4.f, 4.f).Radius(3.f).Clip();
	frame.Bg(entry.kind == AssetKind::MODEL || entry.kind == AssetKind::TEXTURE ? kit::PaletteOf(m_ctx).well
																				  : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	frame.PointerThrough().Parent(tileEntity);
	ecs::Entity frameEntity = frame.Spawn();
	if (!AddPreview(frameEntity, entry, thumb)) {
		ui::WidgetBuilder icon = m_ctx.factory.Icon(IconOf(entry.kind), thumb * 0.62f);
		icon.Absolute().Anchor(ui::Anchor::Center).Justify(ui::Justify::Center).PointerThrough();
		icon.TextColor(kit::Readable(m_ctx, ColorOf(entry.kind))).Parent(frameEntity);
		(void)icon.Spawn();
	}
	if (entry.kind == AssetKind::SCRIPT) {
		// Pastille de compilation, comme les coches de la maquette.
		ui::WidgetBuilder mark = m_ctx.factory.Icon(entry.broken ? ui::MaterialIcons::ERROR : ui::MaterialIcons::CHECK_CIRCLE, 18.f);
		mark.Absolute().Anchor(ui::Anchor::TopRight).Offset(-2.f, 2.f).PointerThrough();
		mark.TextColor(entry.broken ? kit::PaletteOf(m_ctx).error : kit::PaletteOf(m_ctx).ok).Parent(frameEntity);
		(void)mark.Spawn();
	}

	ui::WidgetBuilder name = m_ctx.factory.Label(entry.name);
	name.Absolute().Anchor(ui::Anchor::BottomLeft).Offset(2.f, -3.f).W(ui::Dimension::Px(thumb + 4.f)).HAuto();
	name.FontSize(12.f).TextAlign(ui::TextAlign::Center).TextEllipsis().PointerThrough();
	name.TextColor(theme.text).Parent(tileEntity);
	(void)name.Spawn();
}

bool AssetBrowserPanel::AddPreview(ecs::Entity frame, const AssetEntry &entry, float size) {
	if (entry.kind == AssetKind::TEXTURE && m_ctx.renderer) {
		const String key = String("asset:") + entry.location;
		auto &pool = m_ctx.gui.RenderSystem().textures;
		if (pool.find(key) == pool.end()) {
			auto texture = sdl3::ImgLoadTexture(*m_ctx.renderer, entry.location);
			if (texture.IsError())
				return false;
			pool.insert_or_assign(key, std::move(texture.Value()));
		}
		ui::WidgetBuilder image = m_ctx.factory.Image(key, size, size);
		image.PointerThrough().Parent(frame);
		const ecs::Entity img = image.Spawn();
		if (auto component = m_ctx.registry.GetComponent<ui::UiImage>(img); component.IsSome())
			component.Unwrap()->fit = ui::ImageFit::CONTAIN;
		return true;
	}
	if (entry.kind == AssetKind::MODEL && m_ctx.renderer && entry.location.ToLower().EndsWith(".gltf")) {
		render3d::Object3D *root = ThumbnailFor(entry.location);
		if (!root)
			return false;
		ui::WidgetBuilder view = m_ctx.factory.Viewport3D(*root, ThumbnailCamera(entry.location));
		view.Size(size, size).PointerThrough().Parent(frame);
		ecs::Entity viewEntity = view.Spawn();
		if (auto viewport = m_ctx.registry.GetComponent<ui::UiViewport3D>(viewEntity); viewport.IsSome()) {
			ui::UiViewport3D &v = *viewport.Unwrap();
			v.continuous = false;
			ui::UiViewport3D::Lighting lighting;
			lighting.sun.direction = math::FVector3{-0.5f, -0.7f, -0.6f};
			lighting.sun.intensity = 1.1f;
			lighting.ambient.color = sdl3::Color{64, 64, 70, 255};
			lighting.background = sdl3::Color{20, 20, 23, 255};
			v.lighting = Some(lighting);
		}
		return true;
	}
	return false;
}

render3d::Object3D * AssetBrowserPanel::ThumbnailFor(const String &path) {
	if (auto it = m_thumbnailRoots.find(path); it != m_thumbnailRoots.end())
		return it->second.get();
	auto mesh = Runtime::LoadGltfMesh(path);
	if (mesh.IsError())
		return nullptr;
	auto root = std::make_unique<render3d::Object3D>();
	const math::FAABB bounds = mesh.Value().LocalBounds();
	render3d::Material material = render3d::Material::Plastic(sdl3::Color{196, 184, 166, 255});
	auto &shape = root->Add(std::make_unique<render3d::Shape>(std::move(mesh).Unwrap(), std::move(material)));
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
	kit::SetLabelText(m_ctx, m_info, String::Format("%s · %s%s%s — double-clic pour ouvrir", entry.name.CStr(),
													AssetKindLabel(entry.kind), entry.detail.IsEmpty() ? "" : " · ",
													entry.detail.CStr()));
}

String AssetBrowserPanel::TooltipOf(const AssetEntry &entry) {
	if (entry.kind == AssetKind::SCRIPT && entry.broken)
		return String::Format("%s — ne compile pas", entry.name.CStr());
	return entry.detail.IsEmpty() ? entry.name : String::Format("%s — %s", entry.name.CStr(), entry.detail.CStr());
}

ui::MaterialIcons AssetBrowserPanel::IconOf(AssetKind kind) noexcept {
	switch (kind) {
		case AssetKind::FOLDER:
			return ui::MaterialIcons::FOLDER;
		case AssetKind::SCENE:
			return ui::MaterialIcons::LAYERS;
		case AssetKind::SCRIPT:
			return ui::MaterialIcons::DESCRIPTION;
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
		case AssetKind::SCRIPT:
			return kit::Rgb(210, 210, 214);
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

} // namespace game_editor
