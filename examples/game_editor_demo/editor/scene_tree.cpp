// Définitions de scene_tree.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "scene_tree.hpp"

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
	MarkDirty();
}

void SceneTreePanel::Teardown() {
	for (ecs::Entity popup : m_popups)
		if (popup.Valid())
			ui::DespawnTree(m_ctx.registry, popup);
	m_popups.clear();
	m_list = m_search = ecs::Entity{};
}

void SceneTreePanel::Tick() {
	if (m_dirty) {
		m_dirty = false;
		Refresh();
	}
	if (m_scrollToSelection)
		ScrollToSelection();
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
	Option<ObjectDesc> desc = MakeNodeFromTemplate(key);
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
	const scene::Node *node = m_ctx.runtime.FindObject(id);
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
	kit::ClearChildren(m_ctx, m_list);
	m_rows.clear();
	const SceneDesc *scene = m_ctx.runtime.ActiveScene();
	if (!scene)
		return;

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
	const kit::NodeLook look = isRoot ? kit::NodeLook{ui::MaterialIcons::VIEW_IN_AR, kit::Rgb(126, 172, 232), "Scène"}
									  : kit::LookOf(*node);
	const bool dimmed = !isRoot && (!scene.tree.IsVisibleInTree(id) || node->locked);

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
	row.DropTarget(String("node"));
	row.OnDrop([this, id](int64_t payload) { OnDrop(uint32_t(payload), id); });
	if (!isRoot)
		row.DragPayload(String("node"), int64_t(id.index));
	row.Tooltip(scene.tree.PathOf(id));
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

	String suffix = String::Format("(%s)", look.label);
	if (flat && !isRoot) {
		const scene::Node *parent = scene.tree.Get(node->parent);
		if (parent && parent->id != scene.tree.Root())
			suffix = String::Format("(%s) — %s", look.label, parent->name.CStr());
	}
	// L'étiquette de type prend la place RESTANTE (et s'y abrège) : en
	// largeur naturelle, une ligne longue débordait et la rangée affichait
	// une barre de défilement horizontale.
	ui::WidgetBuilder type = m_ctx.factory.Label(suffix);
	type.GrowW().HAuto().FontSize(12.f).TextColor(theme.muted).TextEllipsis().PointerThrough().Parent(rowEntity);
	(void)type.Spawn();

	if (!isRoot && !node->visible)
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::VISIBILITY_OFF, theme.muted, 13.f);
	if (!isRoot && node->locked)
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::LOCK, theme.muted, 13.f);
	if (!isRoot && ScriptRef::Has(*node))
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::DESCRIPTION, kit::syntax::Colors().nameSpace, 13.f);

	m_rows.push_back(RowRef{rowEntity, id});
}

void SceneTreePanel::OnDrop(uint32_t draggedIndex, scene::NodeId target) {
	const SceneDesc *scene = m_ctx.runtime.ActiveScene();
	if (!scene)
		return;
	scene::NodeId dragged;
	for (const RowRef &row : m_rows)
		if (row.node.index == draggedIndex)
			dragged = row.node;
	if (!dragged.Valid() || dragged == target)
		return;
	if (m_ctx.runtime.ReparentNode(dragged, target)) {
		m_expanded[target] = true;
		MarkDirty();
		const scene::Node *node = scene->tree.Get(dragged);
		const scene::Node *parent = scene->tree.Get(target);
		Status(String::Format("%s placé sous %s", node ? node->name.CStr() : "?", parent ? parent->name.CStr() : "?"));
	} else {
		Status(String("Reparentage refusé (un nœud ne peut pas descendre dans son propre sous-arbre)"));
	}
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
		const scene::Node *node = m_ctx.runtime.FindObject(m_contextTarget);
		const String name = node ? node->name : String();
		if (m_ctx.runtime.RemoveNode(m_contextTarget))
			Status(String::Format("Supprimé : %s (et son sous-arbre)", name.CStr()));
	}));
	m_nodeOnlyItems.push_back(Item(m_contextMenu, "Renommer…", "F2", [this] { BeginRename(m_contextTarget); }));
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
	if (m_ctx.runtime.RenameObject(m_renameTarget, name)) {
		const scene::Node *node = m_ctx.runtime.FindObject(m_renameTarget);
		Status(String::Format("Renommé : %s", node ? node->name.CStr() : name.CStr()));
	}
}

} // namespace game_editor
