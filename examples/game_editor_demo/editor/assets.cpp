// Définitions de assets.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "assets.hpp"

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

bool AssetBrowserModel::Navigate(const String &location) {
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
			for (const SceneDesc &scene : m_project->scenes)
				entries.push_back(AssetEntry{scene.name, String(SCENES) + String("/") + scene.name,
											 AssetKind::SCENE,
											 String::Format("%d objets", int(scene.ObjectCount()))});
	} else if (location == SCRIPTS) {
		if (m_project) {
			for (const ScriptAsset &script : m_project->scripts) {
				AssetEntry entry{script.name + String(".script"), String(SCRIPTS) + String("/") + script.name,
								 AssetKind::SCRIPT, script.description};
				entry.broken = Runtime::CheckScript(script.source).IsSome();
				entries.push_back(std::move(entry));
			}
			// Le script de JEU de chaque scène, adressé par `@scène`.
			for (const SceneDesc &scene : m_project->scenes) {
				if (scene.gameplayScript.IsEmpty())
					continue;
				AssetEntry entry{scene.name + String(".main.script"),
								 String(SCRIPTS) + String("/@") + scene.name, AssetKind::SCRIPT,
								 String::Format("Script de la scène « %s »", scene.name.CStr())};
				entry.broken = Runtime::CheckScript(scene.gameplayScript).IsSome();
				entries.push_back(std::move(entry));
			}
		}
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

void AssetBrowserPanel::Build(ecs::Entity page) {
	m_model.SetProject(&m_ctx.runtime.GetProject());
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

	MarkDirty();
}

void AssetBrowserPanel::Teardown() {
	ClearThumbnails();
	m_tree = m_grid = m_crumbs = m_info = m_search = ecs::Entity{};
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
	m_model = AssetBrowserModel(&m_ctx.runtime.GetProject(), assets, saves, savesLabel);
	MarkDirty();
}

bool AssetBrowserPanel::SelectByName(const String &name) {
	m_entries = m_model.Entries(m_filter);
	for (size_t i = 0; i < m_entries.size(); ++i) {
		if (m_entries[i].name == name) {
			m_selected = int(i);
			MarkDirty();
			return true;
		}
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
	m_entries = m_model.Entries(m_filter);
	m_selected = sdl3::Min(m_selected, int(m_entries.size()) - 1);
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
	const String current = m_model.Location();
	AddTreeRow(String("Projet"), String(AssetBrowserModel::ROOT), 0, true);
	for (const AssetEntry &entry : m_model.List(String(AssetBrowserModel::ROOT))) {
		AddTreeRow(entry.name, entry.location, 1, IsOnPath(entry.location, current));
		if (!IsOnPath(entry.location, current) || entry.location.StartsWith(AssetBrowserModel::ROOT))
			continue;
		AddSubTree(entry.location, current, 2);
	}
}

void AssetBrowserPanel::AddSubTree(const String &folder, const String &current, int depth) {
	if (depth > 8)
		return;
	for (const AssetEntry &child : m_model.List(folder)) {
		if (child.kind != AssetKind::FOLDER)
			continue;
		const bool onPath = IsOnPath(child.location, current);
		AddTreeRow(child.name, child.location, depth, onPath);
		if (onPath)
			AddSubTree(child.location, current, depth + 1);
	}
}

bool AssetBrowserPanel::IsOnPath(const String &folder, const String &current) {
	return current == folder || current.StartsWith(folder + String("/"));
}

void AssetBrowserPanel::AddTreeRow(const String &name, const String &location, int depth, bool open) {
	const bool current = location == m_model.Location();
	ui::WidgetBuilder row = m_ctx.factory.Selectable(String(), m_treeRowIndex++);
	row.Gap(4.f).Pad(math::Sides{4.f + float(depth) * 12.f, 2.f, 4.f, 2.f}).GrowW().HAuto().Parent(m_tree);
	row.OnClick([this, location] {
		if (m_model.Navigate(location))
			MarkDirty();
	});
	ecs::Entity rowEntity = row.Spawn();
	if (auto selectable = m_ctx.registry.GetComponent<ui::UiSelectable>(rowEntity); selectable.IsSome())
		selectable.Unwrap()->selected = current;
	(void)kit::Glyph(m_ctx, rowEntity, open ? ui::MaterialIcons::FOLDER_OPEN : ui::MaterialIcons::FOLDER,
					 kit::Rgb(176, 176, 180), 15.f);
	ui::WidgetBuilder label = m_ctx.factory.Label(name);
	label.GrowW().HAuto().FontSize(13.f).TextEllipsis().PointerThrough().Parent(rowEntity);
	(void)label.Spawn();
}

void AssetBrowserPanel::RefreshGrid() {
	kit::ClearChildren(m_ctx, m_grid);
	ClearThumbnails();
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
	const bool selected = int(index) == m_selected;
	const float thumb = m_tileSize;

	ui::WidgetBuilder tile = m_ctx.factory.Button(String());
	tile.Size(thumb + 8.f, thumb + 26.f).Radius(4.f).Tooltip(TooltipOf(entry)).Parent(row);
	tile.Bg(selected ? kit::PaletteOf(m_ctx).selection : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	tile.OnClick([this, index] { OnTileClick(index); });
	tile.DragPayload(String("asset"), int64_t(index));
	ecs::Entity tileEntity = tile.Spawn();

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
	// Double clic « maison » : deux clics sur la même vignette en moins
	// de 0,45 s ouvrent l'entrée ; un clic seul la sélectionne.
	const bool second = int(index) == m_selected && m_clock - m_lastClick < 0.45f;
	m_lastClick = m_clock;
	if (second) {
		Activate(index);
		return;
	}
	m_selected = int(index);
	RefreshGrid();
	RefreshInfo();
}

void AssetBrowserPanel::RefreshInfo() {
	if (!m_info.Valid())
		return;
	if (m_selected < 0 || m_selected >= int(m_entries.size())) {
		kit::SetLabelText(m_ctx, m_info, String::Format("%d élément(s)", int(m_entries.size())));
		return;
	}
	const AssetEntry &entry = m_entries[size_t(m_selected)];
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
