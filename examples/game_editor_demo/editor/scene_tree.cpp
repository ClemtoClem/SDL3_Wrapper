// Définitions de scene_tree.hpp
#include "scene_tree.hpp"

#include "../document/objects.hpp"

namespace game_editor {

// ── SceneTreePanel ───────────────────────────────────────────────────────────

void SceneTreePanel::Build(ecs::Entity page) {
	// ── Barre : recherche · + · ≡ ──────────────────────────────────────
	ui::WidgetBuilder bar = m_ctx.factory.Row();
	bar.Pad(0.f);
	bar.Gap(2.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(page);
	ecs::Entity barEntity = bar.Spawn();
	m_search = kit::SearchField(m_ctx, barEntity, String("Rechercher un nœud"), [this](const String &text) {
		m_filter = text.Trim();
		MarkDirty();
	});
	m_addButton = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::ADD, String("Créer un nœud enfant"), [this] {
		m_contextTarget = m_ctx.runtime.SelectedId();
		if (auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(m_addButton); computed.IsSome()) {
			const sdl3::FRect r = computed.Unwrap()->screen;
			m_ctx.gui.OpenPopupAt(m_createMenu, sdl3::FPoint{r.x, r.y + r.h});
		}
	});
	m_optionsButton = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::MENU, String("Options de l'arbre"), [this] {
		if (auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(m_optionsButton); computed.IsSome()) {
			const sdl3::FRect r = computed.Unwrap()->screen;
			m_ctx.gui.OpenPopupAt(m_optionsMenu, sdl3::FPoint{r.x, r.y + r.h});
		}
	});

	// ── Scènes du projet : un clic ouvre la scène ──────────────────────
	ui::WidgetBuilder strip = m_ctx.factory.Column();
	strip.Gap(0.f).Pad(math::Sides{0.f, 2.f}).GrowW().HAuto().Parent(page);
	m_sceneStrip = strip.Spawn();

	// ── Liste ──────────────────────────────────────────────────────────
	ui::WidgetBuilder list = m_ctx.factory.Column();
	list.Gap(0.f).Pad(math::Sides{0.f, 2.f}).GrowW().GrowH().Scrollable().Clip().Parent(page);
	// Clic droit dans le VIDE de la liste : créer à la racine.
	list.OnContextMenu([this](float x, float y) {
		const SceneDesc *scene = m_ctx.runtime.ActiveScene();
		if (scene)
			OpenContextMenu(scene->tree.Root(), x, y);
	});
	m_list = list.Spawn();

	BuildMenus();
	BuildRenameDialog();
	BuildDragFeedback();
	MarkDirty();
}

void SceneTreePanel::Teardown() {
	for (ecs::Entity popup : m_popups)
		if (popup.Valid())
			ui::DespawnTree(m_ctx.registry, popup);
	m_popups.clear();
	m_list = m_search = m_sceneStrip = m_sceneRows = ecs::Entity{};
	m_ghost = m_ghostIcon = m_ghostLabel = m_ghostAction = m_dropLine = m_dropBox = m_dropBoxInvalid = ecs::Entity{};
	m_dragSource = m_dropTarget = scene::NodeId{};
	m_dropZone = DropZone::NONE;
}

void SceneTreePanel::Tick() {
	if (m_dirty) {
		m_dirty = false;
		Refresh();
	}
	if (m_scrollToSelection)
		ScrollToSelection();
	UpdateDragFeedback();
}

void SceneTreePanel::RevealSelection() {
	const SceneDesc *scene = m_ctx.runtime.ActiveScene();
	const scene::NodeId selected = m_ctx.runtime.SelectedId();
	if (!scene || !selected.Valid())
		return;
	for (scene::NodeId cur = scene->tree.ParentOf(selected); cur.Valid(); cur = scene->tree.ParentOf(cur))
		m_expanded[cur] = true;
	m_scrollToSelection = true;
	MarkDirty();
}

void SceneTreePanel::SetExpanded(scene::NodeId id, bool expanded) {
	m_expanded[id] = expanded;
	MarkDirty();
}

void SceneTreePanel::ExpandAll(bool expanded) {
	const SceneDesc *scene = m_ctx.runtime.ActiveScene();
	if (!scene)
		return;
	scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &) { m_expanded[id] = expanded; });
	MarkDirty();
}

void SceneTreePanel::ResetExpansion() {
	m_expanded.clear();
	MarkDirty();
}

void SceneTreePanel::OpenContextMenu(scene::NodeId target, float x, float y) {
	m_contextTarget = target;
	const SceneDesc *scene = m_ctx.runtime.ActiveScene();
	const bool isRoot = scene && target == scene->tree.Root();
	// Sur la racine, seules les créations ont un sens.
	for (ecs::Entity item : m_nodeOnlyItems)
		kit::SetHidden(m_ctx, item, isRoot);
	m_ctx.gui.OpenPopupAt(m_contextMenu, sdl3::FPoint{x, y});
}

Option<scene::NodeId> SceneTreePanel::CreateNode(const String &key, scene::NodeId parent) {
	Option<NodeDesc> desc = MakeNodeFromTemplate(key);
	SceneDesc *scene = m_ctx.runtime.ActiveScene();
	if (desc.IsNone() || !scene)
		return NONE;
	if (!parent.Valid())
		parent = scene->tree.Root();
	// À la racine, on pose le nouveau nœud devant la caméra (sinon il
	// naîtrait à l'origine, souvent hors de vue) ; sous un parent, au
	// pivot du parent — c'est le sens d'« enfant ».
	const scene::Node *parentNode = scene->tree.Get(parent);
	const bool flat = Is2DNode(desc.Value().ToNode());
	if (flat && spawnPoint2D && (parent == scene->tree.Root() || !parentNode || !Is2DNode(*parentNode)))
		desc.Value().transform.position = spawnPoint2D();
	else if (!flat && parent == scene->tree.Root() && spawnPoint)
		desc.Value().transform.position = spawnPoint();
	Option<scene::NodeId> created = m_ctx.runtime.SpawnNode(std::move(desc).Unwrap(), parent);
	if (created.IsSome()) {
		m_expanded[parent] = true;
		(void)m_ctx.runtime.Select(created.Unwrap());
		RevealSelection();
		const scene::Node *node = scene->tree.Get(created.Unwrap());
		Status(String::Format("Nœud créé : %s", node ? node->name.CStr() : "?"));
	}
	return created;
}

void SceneTreePanel::BeginRename(scene::NodeId id) {
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	if (!node)
		return;
	m_renameTarget = id;
	if (auto input = m_ctx.registry.GetComponent<ui::UiInput>(m_renameInput); input.IsSome()) {
		input.Unwrap()->text = node->name;
		input.Unwrap()->cursor = node->name.size();
		input.Unwrap()->selectionAnchor = 0;
		input.Unwrap()->focused = true;
	}
	m_ctx.gui.OpenModal(m_renameModal);
}

Option<sdl3::FRect> SceneTreePanel::RowRect(scene::NodeId id) const {
	for (const RowRef &row : m_rows) {
		if (row.node != id)
			continue;
		auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(row.entity);
		if (computed.IsSome() && computed.Unwrap()->screen.h > 0.f)
			return Some(computed.Unwrap()->screen);
	}
	return NONE;
}

bool SceneTreePanel::OpenCreateSubmenu() {
	auto trigger = m_ctx.registry.GetComponent<ui::UiComputed>(m_createTrigger);
	if (trigger.IsNone() || trigger.Unwrap()->screen.w <= 0.f)
		return false;
	const sdl3::FRect r = trigger.Unwrap()->screen;
	m_ctx.gui.OpenPopupAt(m_createSubmenu, sdl3::FPoint{r.x + r.w, r.y});
	if (auto state = m_ctx.registry.GetComponent<ui::UiPopupState>(m_createSubmenu); state.IsSome())
		state.Unwrap()->trigger = m_createTrigger;
	return true;
}

bool SceneTreePanel::IsExpanded(scene::NodeId id) const {
	auto it = m_expanded.find(id);
	return it != m_expanded.end() && it->second;
}

void SceneTreePanel::Status(const String &text) {
	if (onStatus)
		onStatus(text);
}

bool SceneTreePanel::ExpandedByDefault(scene::NodeId id, const scene::Node &node) const {
	if (auto it = m_expanded.find(id); it != m_expanded.end())
		return it->second;
	return node.type == node_kind::FOLDER;
}

void SceneTreePanel::Refresh() {
	if (!m_list.Valid())
		return;
	RefreshSceneStrip();
	kit::ClearChildren(m_ctx, m_list);
	m_rows.clear();
	m_scriptClasses.clear();
	const SceneDesc *scene = m_ctx.runtime.ActiveScene();
	if (!scene)
		return;
	for (const ScriptObjectInfo &info : m_ctx.runtime.LiveScriptObjects()) {
		if (!info.node.Valid() || info.destroyed)
			continue;
		String &classes = m_scriptClasses[info.node];
		classes = classes.IsEmpty() ? info.className : classes + String(", ") + info.className;
	}

	int index = 0;
	if (!m_filter.IsEmpty()) {
		// Recherche : liste à plat, chemin du parent en gris.
		const String wanted = m_filter.ToLower();
		size_t shown = 0;
		scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
			if (id == scene->tree.Root() || shown >= MAX_SEARCH_RESULTS || !node.name.ToLower().Contains(wanted))
				return;
			AddRow(*scene, id, 0, index++, true);
			++shown;
		});
		if (shown == 0)
			(void)kit::Caption(m_ctx, m_list, String("Aucun nœud ne correspond"), 13.f);
		return;
	}

	// La racine (la scène) en tête, comme « DungeonLevel » dans la maquette.
	AddRow(*scene, scene->tree.Root(), 0, index++, false);
	for (scene::NodeId child : scene->tree.ChildrenOf(scene->tree.Root()))
		AddSubtree(*scene, child, 1, index);
}

void SceneTreePanel::AddSubtree(const SceneDesc &scene, scene::NodeId id, int depth, int &index) {
	const scene::Node *node = scene.tree.Get(id);
	if (!node)
		return;
	AddRow(scene, id, depth, index++, false);
	if (node->children.empty() || !ExpandedByDefault(id, *node))
		return;
	for (scene::NodeId child : node->children)
		AddSubtree(scene, child, depth + 1, index);
}

void SceneTreePanel::AddRow(const SceneDesc &scene, scene::NodeId id, int depth, int index, bool flat) {
	const scene::Node *node = scene.tree.Get(id);
	if (!node)
		return;
	const ui::UiTheme &theme = m_ctx.Theme();
	const bool isRoot = id == scene.tree.Root();
	// Instance d'objet : référence (icône d'objet) ; nœud généré : contenu
	// de l'objet, grisé, ni déplaçable ni renommable ici.
	const bool instance = !isRoot && objects::IsInstance(*node);
	const bool generated = !isRoot && objects::IsGenerated(*node);
	kit::NodeLook look = isRoot ? kit::NodeLook{scene.IsObject() ? ui::MaterialIcons::CATEGORY : ui::MaterialIcons::VIEW_IN_AR,
											   scene.IsObject() ? kit::Rgb(232, 176, 92) : kit::Rgb(126, 172, 232),
											   scene.IsObject() ? "Objet" : "Scène"}
								: kit::LookOf(*node);
	if (instance)
		look = kit::NodeLook{ui::MaterialIcons::CATEGORY, kit::Rgb(232, 176, 92), "Instance"};
	const bool dimmed = !isRoot && (!scene.tree.IsVisibleInTree(id) || node->locked || generated);

	ui::WidgetBuilder row = m_ctx.factory.Selectable(String(), index);
	row.GrowW().HAuto().Gap(4.f).Pad(math::Sides{4.f + float(depth) * 14.f, 1.f, 6.f, 1.f}).Parent(m_list);
	row.OnClick([this, id, isRoot] {
		if (!isRoot)
			(void)m_ctx.runtime.Select(id);
	});
	row.OnContextMenu([this, id, isRoot](float x, float y) {
		if (!isRoot)
			(void)m_ctx.runtime.Select(id);
		OpenContextMenu(id, x, y);
	});
	if (!generated) {
		row.DropTarget(String("node"), false); // indicateur propre (avant / après / dans)
		row.OnDrop([this, id](int64_t payload) { OnDrop(uint32_t(payload), id); });
	}
	if (!isRoot && !generated)
		row.DragPayload(String("node"), int64_t(id.index));
	String tip = scene.tree.PathOf(id);
	if (instance)
		tip += String::Format("\nInstance de l'objet « %s » : son contenu suit l'objet", objects::SourceOf(*node).CStr());
	if (generated)
		tip += String("\nContenu d'une instance d'objet : modifiez l'objet pour le changer");
	row.Tooltip(tip);
	ecs::Entity rowEntity = row.Spawn();
	if (auto selectable = m_ctx.registry.GetComponent<ui::UiSelectable>(rowEntity); selectable.IsSome())
		selectable.Unwrap()->selected = m_ctx.runtime.SelectedId() == id;

	// Flèche (ou retrait de même largeur, pour garder les icônes alignées).
	if (!flat && !node->children.empty() && !isRoot) {
		const bool expanded = ExpandedByDefault(id, *node);
		(void)kit::IconButton(m_ctx, rowEntity, expanded ? ui::MaterialIcons::ARROW_DROP_DOWN : ui::MaterialIcons::ARROW_RIGHT,
							  String(expanded ? "Replier" : "Déplier"),
							  [this, id, expanded] { SetExpanded(id, !expanded); }, 18.f, nullptr, theme.muted);
	} else {
		ui::WidgetBuilder gap = m_ctx.factory.Row();
		gap.Pad(0.f);
		gap.Size(18.f, 18.f).PointerThrough().Parent(rowEntity);
		(void)gap.Spawn();
	}

	(void)kit::Glyph(m_ctx, rowEntity, look.icon, dimmed ? theme.muted : look.color, 16.f);

	ui::WidgetBuilder name = m_ctx.factory.Label(node->name);
	name.WAuto().HAuto().FontSize(13.f).TextColor(dimmed ? theme.muted : theme.text).PointerThrough();
	name.Parent(rowEntity);
	(void)name.Spawn();

	String suffix = instance ? String::Format("(Objet : %s)", objects::SourceOf(*node).CStr())
							 : String::Format("(%s)", look.label);
	if (flat && !isRoot) {
		const scene::Node *parent = scene.tree.Get(node->parent);
		if (parent && parent->id != scene.tree.Root())
			suffix = String::Format("(%s) — %s", look.label, parent->name.CStr());
	}
	// L'étiquette de type prend la place RESTANTE (et s'y abrège) : en
	// largeur naturelle, une ligne longue débordait et la rangée affichait
	// une barre de défilement horizontale.
	// Objet de script vivant qui porte ce nœud : sa classe à la suite.
	const auto scripted = m_scriptClasses.find(id);
	if (scripted != m_scriptClasses.end())
		suffix = String::Format("%s · %s", suffix.CStr(), scripted->second.CStr());
	ui::WidgetBuilder type = m_ctx.factory.Label(suffix);
	type.GrowW().HAuto().FontSize(12.f).TextColor(theme.muted).TextEllipsis().PointerThrough().Parent(rowEntity);
	(void)type.Spawn();

	if (!isRoot && !node->visible)
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::VISIBILITY_OFF, theme.muted, 13.f);
	if (!isRoot && node->locked)
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::LOCK, theme.muted, 13.f);
	if (!isRoot && ScriptRef::Has(*node))
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::DESCRIPTION, kit::syntax::Colors().nameSpace, 13.f);
	if (scripted != m_scriptClasses.end())
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::SMART_TOY, kit::syntax::Colors().type, 13.f);

	m_rows.push_back(RowRef{rowEntity, id, flat ? 0 : depth});
}

void SceneTreePanel::OnDrop(uint32_t draggedIndex, scene::NodeId target) {
	scene::NodeId dragged;
	for (const RowRef &row : m_rows)
		if (row.node.index == draggedIndex)
			dragged = row.node;
	// La zone est recalculée au point de relâchement : c'est celle que
	// l'indicateur montrait à cet instant.
	const DropZone zone = DropZoneAt(dragged, target, Pointer().y);
	HideDragFeedback();
	if (!dragged.Valid() || zone == DropZone::NONE)
		return;
	if (zone == DropZone::INVALID) {
		Status(String("Déplacement refusé : un nœud ne peut pas descendre dans son propre sous-arbre"));
		return;
	}
	(void)ApplyDrop(dragged, target, zone);
}

SceneTreePanel::DropZone SceneTreePanel::DropZoneAt(scene::NodeId dragged, scene::NodeId target, float pointerY) const {
	const SceneDesc *scene = m_ctx.runtime.ActiveScene();
	if (!scene || !dragged.Valid() || !target.Valid() || !scene->tree.Contains(dragged) || !scene->tree.Contains(target))
		return DropZone::NONE;
	if (dragged == target || scene->tree.IsAncestorOf(dragged, target))
		return DropZone::INVALID;
	// Le contenu d'une instance n'appartient pas à la scène : ni source ni
	// cible (on modifie l'objet lui-même).
	if (objects::IsGenerated(*scene->tree.Get(dragged)) || objects::IsGenerated(*scene->tree.Get(target)))
		return DropZone::INVALID;
	if (target == scene->tree.Root())
		return DropZone::INSIDE; // la ligne de la scène : à la racine
	Option<sdl3::FRect> rect = RowRect(target);
	if (rect.IsNone() || rect.Value().h <= 0.f)
		return DropZone::INSIDE;
	const float t = (pointerY - rect.Value().y) / rect.Value().h;
	if (t < 0.28f)
		return DropZone::BEFORE;
	if (t > 0.72f)
		return DropZone::AFTER;
	return DropZone::INSIDE;
}

bool SceneTreePanel::ApplyDrop(scene::NodeId dragged, scene::NodeId target, DropZone zone) {
	const SceneDesc *scene = m_ctx.runtime.ActiveScene();
	if (!scene || zone == DropZone::NONE || zone == DropZone::INVALID)
		return false;
	const scene::Node *node = scene->tree.Get(dragged);
	const scene::Node *targetNode = scene->tree.Get(target);
	if (!node || !targetNode)
		return false;
	const String name = node->name, targetName = targetNode->name;
	if (zone == DropZone::INSIDE) {
		if (!m_ctx.runtime.ReparentNode(dragged, target)) {
			Status(String("Déplacement refusé"));
			return false;
		}
		m_expanded[target] = true;
		Status(String::Format("%s placé dans %s", name.CStr(), targetName.CStr()));
	} else {
		// Rang parmi les frères de la cible, compté SANS le nœud déplacé
		// (NodeTree retire avant d'insérer).
		const scene::NodeId parent = scene->tree.ParentOf(target);
		size_t index = 0;
		for (scene::NodeId sibling : scene->tree.ChildrenOf(parent)) {
			if (sibling == target)
				break;
			if (sibling != dragged)
				++index;
		}
		if (zone == DropZone::AFTER)
			++index;
		const bool sameParent = scene->tree.ParentOf(dragged) == parent;
		const bool moved = sameParent ? m_ctx.runtime.MoveNodeInParent(dragged, index)
									  : m_ctx.runtime.ReparentNode(dragged, parent, scene::ReparentMode::KEEP_GLOBAL, index);
		if (!moved) {
			Status(String("Déplacement refusé"));
			return false;
		}
		Status(String::Format("%s placé %s %s", name.CStr(), zone == DropZone::BEFORE ? "avant" : "après",
							  targetName.CStr()));
	}
	(void)m_ctx.runtime.Select(dragged);
	RevealSelection();
	MarkDirty();
	return true;
}

const SceneTreePanel::RowRef *SceneTreePanel::RowOf(scene::NodeId id) const {
	for (const RowRef &row : m_rows)
		if (row.node == id)
			return &row;
	return nullptr;
}

sdl3::FPoint SceneTreePanel::Pointer() {
	float x = 0.f, y = 0.f;
	(void)SDL_GetMouseState(&x, &y);
	return sdl3::FPoint{x, y};
}

// ── Scènes du projet ─────────────────────────────────────────────────────────

void SceneTreePanel::RefreshSceneStrip() {
	if (!m_sceneStrip.Valid())
		return;
	kit::ClearChildren(m_ctx, m_sceneStrip);
	const ui::UiTheme &theme = m_ctx.Theme();
	const Project &project = m_ctx.runtime.GetProject();
	const SceneDesc *active = m_ctx.runtime.ActiveScene();

	ui::WidgetBuilder head = m_ctx.factory.Row();
	head.Pad(math::Sides{2.f, 1.f}).Gap(2.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(m_sceneStrip);
	ecs::Entity headEntity = head.Spawn();
	(void)kit::IconButton(m_ctx, headEntity,
						  m_scenesExpanded ? ui::MaterialIcons::ARROW_DROP_DOWN : ui::MaterialIcons::ARROW_RIGHT,
						  String(m_scenesExpanded ? "Masquer les scènes" : "Afficher les scènes"),
						  [this] {
							  m_scenesExpanded = !m_scenesExpanded;
							  MarkDirty();
						  },
						  18.f, nullptr, theme.muted);
	// Les objets réutilisables ont leur propre onglet (« Objets ») : ici, les scènes.
	ui::WidgetBuilder title = m_ctx.factory.Label(String::Format("Scènes du projet (%d)", int(project.SceneCount())));
	title.GrowW().HAuto().FontSize(12.f).TextColor(theme.muted).TextEllipsis().PointerThrough().Parent(headEntity);
	(void)title.Spawn();
	if (!m_scenesExpanded)
		return;

	int index = 0;
	for (const SceneDesc &scene : project.scenes) {
		if (scene.IsObject())
			continue;
		const bool current = active && active->name == scene.name;
		ui::WidgetBuilder row = m_ctx.factory.Selectable(String(), index++);
		row.GrowW().HAuto().Gap(6.f).Pad(math::Sides{22.f, 2.f, 6.f, 2.f}).Parent(m_sceneStrip);
		row.Tooltip(scene.description.IsEmpty() ? scene.name : scene.description);
		row.OnClick([this, name = scene.name] {
			const SceneDesc *now = m_ctx.runtime.ActiveScene();
			if (now && now->name == name)
				return;
			if (m_ctx.runtime.SwitchScene(name))
				Status(String::Format("Scène ouverte : %s", name.CStr()));
		});
		ecs::Entity rowEntity = row.Spawn();
		if (auto selectable = m_ctx.registry.GetComponent<ui::UiSelectable>(rowEntity); selectable.IsSome())
			selectable.Unwrap()->selected = current;
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::LAYERS, kit::Rgb(126, 172, 232), 15.f);
		ui::WidgetBuilder name = m_ctx.factory.Label(scene.name);
		name.WAuto().HAuto().FontSize(13.f).PointerThrough().Parent(rowEntity);
		(void)name.Spawn();
		ui::WidgetBuilder count = m_ctx.factory.Label(String::Format("(%d)", int(scene.NodeCount())));
		count.GrowW().HAuto().FontSize(12.f).TextColor(theme.muted).TextEllipsis().PointerThrough().Parent(rowEntity);
		(void)count.Spawn();
	}
	// Filet entre les scènes et l'arbre de la scène active.
	ui::WidgetBuilder rule = m_ctx.factory.Row();
	rule.Pad(0.f);
	rule.GrowW().H(ui::Dimension::Px(1.f)).Bg(kit::PaletteOf(m_ctx).separator).PointerThrough().Parent(m_sceneStrip);
	(void)rule.Spawn();
}

// ── Retour visuel du glisser-déposer ─────────────────────────────────────────

void SceneTreePanel::BuildDragFeedback() {
	const kit::Palette palette = kit::PaletteOf(m_ctx);
	// Fantôme : une étiquette flottante qui suit la souris.
	ui::WidgetBuilder ghost = m_ctx.factory.Row();
	ghost.Fixed().Hidden().OverlayOrder(900).PointerThrough();
	ghost.Pad(math::Sides{8.f, 4.f}).Gap(6.f).WAuto().HAuto().Align(ui::CrossAlign::Center).Radius(5.f);
	ghost.Bg(sdl3::FColor{palette.selection.r, palette.selection.g, palette.selection.b, 0.88f});
	ghost.BorderColor(sdl3::FColor{1.f, 1.f, 1.f, 0.35f});
	m_ghost = ghost.Spawn();
	m_popups.push_back(m_ghost);
	// Trait d'insertion (avant / après) et cadre (enfant de).
	ui::WidgetBuilder line = m_ctx.factory.Row();
	line.Fixed().Hidden().OverlayOrder(899).PointerThrough().Pad(0.f).Size(10.f, 3.f).Radius(1.5f);
	line.Bg(sdl3::FColor{0.45f, 0.72f, 1.f, 1.f}); // plus clair que la sélection : lisible sur elle aussi
	m_dropLine = line.Spawn();
	m_popups.push_back(m_dropLine);
	// Deux cadres aux couleurs fixes : « dans ce nœud » et « impossible ici ».
	auto makeBox = [&](sdl3::FColor color) {
		ui::WidgetBuilder box = m_ctx.factory.Row();
		box.Fixed().Hidden().OverlayOrder(899).PointerThrough().Pad(0.f).Size(10.f, 10.f).Radius(3.f);
		box.Bg(sdl3::FColor{color.r, color.g, color.b, 0.22f});
		box.BorderColor(color, 2.f);
		ecs::Entity e = box.Spawn();
		m_popups.push_back(e);
		return e;
	};
	m_dropBox = makeBox(sdl3::FColor{0.45f, 0.72f, 1.f, 1.f});
	m_dropBoxInvalid = makeBox(palette.error);
}

void SceneTreePanel::HideDragFeedback() {
	for (ecs::Entity e : {m_ghost, m_dropLine, m_dropBox, m_dropBoxInvalid})
		if (e.Valid() && !m_ctx.registry.HasComponent<ui::UiHidden>(e))
			kit::SetHidden(m_ctx, e, true);
	m_dragSource = m_dropTarget = scene::NodeId{};
	m_dropZone = DropZone::NONE;
}

namespace {

void PlaceFixed(UiContext &ctx, ecs::Entity e, float x, float y, Option<sdl3::FPoint> size) {
	if (auto rect = ctx.registry.GetComponent<ui::UiRect>(e); rect.IsSome()) {
		rect.Unwrap()->anchor = ui::Anchor::TopLeft;
		rect.Unwrap()->offset = {x, y};
	}
	if (size.IsSome())
		if (auto item = ctx.registry.GetComponent<ui::UiItem>(e); item.IsSome()) {
			item.Unwrap()->width = ui::Dimension::Px(size.Value().x);
			item.Unwrap()->height = ui::Dimension::Px(size.Value().y);
		}
	if (ctx.registry.HasComponent<ui::UiHidden>(e))
		kit::SetHidden(ctx, e, false);
}

} // namespace

void SceneTreePanel::UpdateDragFeedback() {
	if (!m_ghost.Valid())
		return;
	// Le glissé est celui de la bibliothèque ui:: (UiDragPayload sur la
	// ligne source, UiDropTarget survolé) : on ne fait que le MONTRER.
	scene::NodeId source, target;
	for (const RowRef &row : m_rows) {
		if (auto payload = m_ctx.registry.GetComponent<ui::UiDragPayload>(row.entity);
			payload.IsSome() && payload.Unwrap()->dragging)
			source = row.node;
		if (auto drop = m_ctx.registry.GetComponent<ui::UiDropTarget>(row.entity); drop.IsSome() && drop.Unwrap()->hovered)
			target = row.node;
	}
	if (!source.Valid()) {
		if (m_dragSource.Valid() || !m_ctx.registry.HasComponent<ui::UiHidden>(m_ghost))
			HideDragFeedback();
		return;
	}
	const SceneDesc *scene = m_ctx.runtime.ActiveScene();
	const scene::Node *node = scene ? scene->tree.Get(source) : nullptr;
	if (!node)
		return;
	const sdl3::FPoint pointer = Pointer();

	// Contenu du fantôme : construit au début du glissé seulement.
	if (source != m_dragSource) {
		m_dragSource = source;
		kit::ClearChildren(m_ctx, m_ghost);
		const kit::NodeLook look = kit::LookOf(*node);
		(void)kit::Glyph(m_ctx, m_ghost, look.icon, look.color, 15.f);
		ui::WidgetBuilder label = m_ctx.factory.Label(node->name);
		label.WAuto().HAuto().FontSize(13.f).Bold().PointerThrough().Parent(m_ghost);
		m_ghostLabel = label.Spawn();
		ui::WidgetBuilder action = m_ctx.factory.Label(String());
		action.WAuto().HAuto().FontSize(12.f).TextColor(sdl3::FColor{1.f, 1.f, 1.f, 0.75f}).PointerThrough();
		action.Parent(m_ghost);
		m_ghostAction = action.Spawn();
	}

	m_dropTarget = target;
	m_dropZone = DropZoneAt(source, target, pointer.y);
	const scene::Node *targetNode = scene->tree.Get(target);
	const String targetName = targetNode ? targetNode->name : String();
	String action;
	switch (m_dropZone) {
		case DropZone::BEFORE:
			action = String::Format("↑ avant %s", targetName.CStr());
			break;
		case DropZone::AFTER:
			action = String::Format("↓ après %s", targetName.CStr());
			break;
		case DropZone::INSIDE:
			action = target == scene->tree.Root() ? String("→ à la racine")
												  : String::Format("→ dans %s", targetName.CStr());
			break;
		case DropZone::INVALID:
			action = String("⊘ impossible ici");
			break;
		case DropZone::NONE:
			break;
	}
	kit::SetLabelText(m_ctx, m_ghostAction, action);
	PlaceFixed(m_ctx, m_ghost, pointer.x + 16.f, pointer.y + 8.f, NONE);

	// Indicateur sur la ligne cible.
	const RowRef *row = RowOf(target);
	Option<sdl3::FRect> rect = row ? RowRect(target) : NONE;
	const bool hasRect = rect.IsSome() && rect.Value().h > 0.f;
	if (hasRect && (m_dropZone == DropZone::BEFORE || m_dropZone == DropZone::AFTER)) {
		const sdl3::FRect r = rect.Value();
		const float indent = 22.f + float(row->depth) * 14.f; // alignée sur l'icône : on voit le niveau
		const float y = m_dropZone == DropZone::BEFORE ? r.y - 1.5f : r.y + r.h - 1.5f;
		PlaceFixed(m_ctx, m_dropLine, r.x + indent, y, Some(sdl3::FPoint{sdl3::Max(20.f, r.w - indent - 4.f), 3.f}));
		kit::SetHidden(m_ctx, m_dropBox, true);
		kit::SetHidden(m_ctx, m_dropBoxInvalid, true);
	} else if (hasRect && (m_dropZone == DropZone::INSIDE || m_dropZone == DropZone::INVALID)) {
		const sdl3::FRect r = rect.Value();
		const bool invalid = m_dropZone == DropZone::INVALID;
		PlaceFixed(m_ctx, invalid ? m_dropBoxInvalid : m_dropBox, r.x + 1.f, r.y, Some(sdl3::FPoint{r.w - 2.f, r.h}));
		kit::SetHidden(m_ctx, invalid ? m_dropBox : m_dropBoxInvalid, true);
		kit::SetHidden(m_ctx, m_dropLine, true);
	} else {
		kit::SetHidden(m_ctx, m_dropLine, true);
		kit::SetHidden(m_ctx, m_dropBox, true);
		kit::SetHidden(m_ctx, m_dropBoxInvalid, true);
	}

	// Défilement automatique près des bords de la liste.
	auto list = m_ctx.registry.GetComponent<ui::UiComputed>(m_list);
	auto listRect = m_ctx.registry.GetComponent<ui::UiRect>(m_list);
	if (list.IsSome() && listRect.IsSome()) {
		const sdl3::FRect l = list.Unwrap()->screen;
		constexpr float EDGE = 28.f, STEP = 9.f;
		float delta = 0.f;
		if (pointer.y < l.y + EDGE && pointer.y >= l.y - EDGE)
			delta = -STEP;
		else if (pointer.y > l.y + l.h - EDGE && pointer.y <= l.y + l.h + EDGE)
			delta = STEP;
		if (delta != 0.f) {
			ui::UiRect &rr = *listRect.Unwrap();
			rr.scroll.y = sdl3::Clamp(rr.scroll.y + delta, 0.f, rr.MaxScroll().y);
		}
	}
	m_ctx.gui.Layout().MarkDirty();
}

void SceneTreePanel::ScrollToSelection() {
	const scene::NodeId selected = m_ctx.runtime.SelectedId();
	auto list = m_ctx.registry.GetComponent<ui::UiComputed>(m_list);
	auto rect = m_ctx.registry.GetComponent<ui::UiRect>(m_list);
	if (!selected.Valid() || list.IsNone() || rect.IsNone()) {
		m_scrollToSelection = false;
		return;
	}
	for (const RowRef &row : m_rows) {
		if (row.node != selected)
			continue;
		auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(row.entity);
		if (computed.IsNone() || computed.Unwrap()->screen.h <= 0.f)
			return; // pas encore mise en page : on réessaiera à l'image suivante
		const sdl3::FRect view = list.Unwrap()->screen;
		const sdl3::FRect line = computed.Unwrap()->screen;
		ui::UiRect &r = *rect.Unwrap();
		if (line.y < view.y)
			r.scroll.y -= view.y - line.y + 4.f;
		else if (line.y + line.h > view.y + view.h)
			r.scroll.y += line.y + line.h - (view.y + view.h) + 4.f;
		r.ClampScroll();
		m_ctx.gui.Layout().MarkDirty();
		break;
	}
	m_scrollToSelection = false;
}

ecs::Entity SceneTreePanel::Item(ecs::Entity menu, const char *text, const char *shortcut, std::function<void()> action) {
	ui::WidgetBuilder item = m_ctx.factory.MenuItem(String(text), String(shortcut));
	item.OnClick(std::move(action)).Parent(menu);
	return item.Spawn();
}

void SceneTreePanel::AddCreateEntries(ecs::Entity menu) {
	for (const char *category : {"3D", "Lumière", "Logique"}) {
		ecs::Entity sub = m_ctx.factory.SubMenu(menu, String(category));
		m_popups.push_back(sub);
		for (const NodeTemplate &entry : NODE_TEMPLATES) {
			if (String(entry.category) != category)
				continue;
			const String key(entry.key);
			(void)Item(sub, entry.label, "", [this, key] { (void)CreateNode(key, m_contextTarget); });
		}
	}
	for (const NodeTemplate &entry : NODE_TEMPLATES) {
		if (String(entry.category) != "Nœud")
			continue;
		const String key(entry.key);
		(void)Item(menu, entry.label, "", [this, key] { (void)CreateNode(key, m_contextTarget); });
	}
}

void SceneTreePanel::BuildMenus() {
	// Menu contextuel d'une ligne.
	m_contextMenu = m_ctx.factory.ContextMenu();
	m_popups.push_back(m_contextMenu);
	ecs::Entity create = m_ctx.factory.SubMenu(m_contextMenu, String("Créer un nœud enfant"));
	m_popups.push_back(create);
	m_createSubmenu = create;
	if (auto children = m_ctx.registry.GetComponent<ui::UiChildren>(m_contextMenu);
		children.IsSome() && !children.Unwrap()->list.empty())
		m_createTrigger = children.Unwrap()->list.front();
	AddCreateEntries(create);
	m_nodeOnlyItems.clear();
	m_nodeOnlyItems.push_back(Item(m_contextMenu, "Dupliquer", "Ctrl+D", [this] {
		Option<scene::NodeId> copy = m_ctx.runtime.DuplicateNode(m_contextTarget);
		if (copy.IsSome()) {
			(void)m_ctx.runtime.Select(copy.Unwrap());
			RevealSelection();
		}
	}));
	m_nodeOnlyItems.push_back(Item(m_contextMenu, "Supprimer", "Suppr", [this] {
		const scene::Node *node = m_ctx.runtime.FindNode(m_contextTarget);
		const String name = node ? node->name : String();
		if (m_ctx.runtime.RemoveNode(m_contextTarget))
			Status(String::Format("Supprimé : %s (et son sous-arbre)", name.CStr()));
	}));
	m_nodeOnlyItems.push_back(Item(m_contextMenu, "Renommer…", "F2", [this] { BeginRename(m_contextTarget); }));
	m_nodeOnlyItems.push_back(Item(m_contextMenu, "Créer un objet à partir du nœud", "", [this] {
		auto object = m_ctx.runtime.CreateObjectFromNode(m_contextTarget);
		if (object.IsError())
			Status(String::Format("Objet impossible : %s", object.Error().CStr()));
		else
			Status(String::Format("Objet « %s » créé : le nœud en est maintenant une instance", object.Value().CStr()));
		MarkDirty();
	}));
	m_nodeOnlyItems.push_back(Item(m_contextMenu, "Ouvrir l'objet", "", [this] {
		const scene::Node *node = m_ctx.runtime.FindNode(m_contextTarget);
		if (!node || !objects::IsInstance(*node)) {
			Status(String("Ce nœud n'est pas une instance d'objet"));
			return;
		}
		const String source = objects::SourceOf(*node);
		if (m_ctx.runtime.GetProject().Fs().FindObject(ObjectRef{source}) && m_ctx.runtime.SwitchScene(source))
			Status(String::Format("Objet ouvert : %s — ses instances suivront vos modifications", source.CStr()));
		else
			Status(String::Format("Objet « %s » introuvable", source.CStr()));
	}));
	m_nodeOnlyItems.push_back(Item(m_contextMenu, "Rendre indépendant", "", [this] {
		auto detached = m_ctx.runtime.DetachInstance(m_contextTarget);
		Status(detached.IsOk() ? String("L'instance est devenue des nœuds propres à la scène")
							   : String::Format("Impossible : %s", detached.Error().CStr()));
		MarkDirty();
	}));
	m_nodeOnlyItems.push_back(Item(m_contextMenu, "Cadrer dans la vue", "F", [this] {
		(void)m_ctx.runtime.FocusOn(m_contextTarget);
	}));
	m_nodeOnlyItems.push_back(Item(m_contextMenu, "Copier le chemin", "", [this] {
		const SceneDesc *scene = m_ctx.runtime.ActiveScene();
		if (!scene)
			return;
		const String path = scene->tree.PathOf(m_contextTarget);
		(void)SDL_SetClipboardText(path.CStr());
		Status(String::Format("Chemin copié : %s", path.CStr()));
	}));

	// Menu du bouton + (création seule).
	m_createMenu = m_ctx.factory.ContextMenu();
	m_popups.push_back(m_createMenu);
	AddCreateEntries(m_createMenu);

	// Menu ≡ de l'arbre.
	m_optionsMenu = m_ctx.factory.ContextMenu();
	m_popups.push_back(m_optionsMenu);
	(void)Item(m_optionsMenu, "Tout déplier", "", [this] { ExpandAll(true); });
	(void)Item(m_optionsMenu, "Tout replier", "", [this] { ExpandAll(false); });
	(void)Item(m_optionsMenu, "Replier sauf les dossiers", "", [this] { ResetExpansion(); });
	(void)Item(m_optionsMenu, "Montrer la sélection", "", [this] { RevealSelection(); });
}

void SceneTreePanel::BuildRenameDialog() {
	ui::WidgetBuilder title = m_ctx.factory.Label(String("Renommer le nœud"));
	title.FontSize(15.f).Bold().WAuto().HAuto();
	ui::WidgetBuilder input = m_ctx.factory.Input();
	input.GrowW().H(ui::Dimension::Px(28.f)).OnSubmit([this](const String &text) { ApplyRename(text); });
	ui::WidgetBuilder ok = m_ctx.factory.Button(String("Renommer"));
	ok.WAuto().HAuto().OnClick([this] {
		if (auto field = m_ctx.registry.GetComponent<ui::UiInput>(m_renameInput); field.IsSome())
			ApplyRename(field.Unwrap()->text);
	});
	ui::WidgetBuilder cancel = m_ctx.factory.Button(String("Annuler"));
	cancel.WAuto().HAuto().OnClick([this] { m_ctx.gui.CloseModal(m_renameModal); });
	ui::WidgetBuilder buttons = m_ctx.factory.Row();
	buttons.Pad(0.f);
	buttons.Gap(8.f).GrowW().HAuto().Justify(ui::Justify::End).Children(std::move(cancel), std::move(ok));
	ui::WidgetBuilder panel = m_ctx.factory.Panel();
	panel.Size(380.f, 150.f).Pad(16.f).Gap(12.f).Children(std::move(title), std::move(input), std::move(buttons));
	m_renameModal = m_ctx.factory.Modal(std::move(panel)).Spawn();
	m_popups.push_back(m_renameModal);
	m_ctx.registry.Query<ui::UiInput, ui::UiParent>([this](ecs::Entity e, ui::UiInput &, ui::UiParent &) {
		if (ui::IsDescendantOrSelf(m_ctx.registry, e, m_renameModal))
			m_renameInput = e;
	});
}

void SceneTreePanel::ApplyRename(const String &text) {
	const String name = text.Trim();
	m_ctx.gui.CloseModal(m_renameModal);
	if (name.IsEmpty() || !m_renameTarget.Valid())
		return;
	if (m_ctx.runtime.RenameNode(m_renameTarget, name)) {
		const scene::Node *node = m_ctx.runtime.FindNode(m_renameTarget);
		Status(String::Format("Renommé : %s", node ? node->name.CStr() : name.CStr()));
	}
}

} // namespace game_editor
