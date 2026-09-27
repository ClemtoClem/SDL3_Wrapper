// Définitions de panels.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "panels.hpp"

namespace game_editor {

// ── EditorUi ─────────────────────────────────────────────────────────────────

EditorUi::EditorUi(Runtime &runtime, ui::Ui &gui)
	: m_ctx{runtime, gui, gui.World(), gui.Factory()}, m_tree(m_ctx),
	  m_inspector(m_ctx, MakeInspectorActions()), m_library(m_ctx, MakeInspectorActions()),
	  m_assets(m_ctx, MakeAssetActions()), m_documents(m_ctx), m_treeDock(m_ctx), m_inspectorDock(m_ctx),
	  m_libraryDock(m_ctx), m_assetDock(m_ctx), m_consoleDock(m_ctx) {
	Rt().onSceneStructureChanged = [this] {
		m_tree.MarkDirty();
		m_inspector.MarkDirty();
		m_scenesDirty = true;
	};
	Rt().onSelectionChanged = [this] {
		m_inspector.MarkDirty();
		m_tree.RevealSelection();
		FollowSelectionWorkspace();
	};
	Rt().onObjectChanged = [this] {
		m_inspector.OnObjectChanged();
		m_tree.MarkDirty(); // nom, visibilité, verrou : l'arbre les montre
	};
	Rt().onLog = [this](const LogEntry &) { m_consoleDirty = true; };
	m_tree.onStatus = [this](const String &text) { SetStatus(text); };
	m_tree.spawnPoint = [this] { return SpawnPointInFrontOfCamera(1.f); };
	m_tree.spawnPoint2D = [this] { return m_edit2dCenter; };
	m_renderer2d.SetResolver([this](const String &path) { return ResolveAsset(path); });
	Rt().SetTextMeasure(m_renderer2d.Measure());
	m_documents.onStatus = [this](const String &text) { SetStatus(text); };
	m_documents.onSaveScriptAs = [this](const String &key) { OnSaveScriptAs(key); };

	// Interface des scripts de jeu : un calque sur la vue du jeu.
	const auto &gameUi = Rt().GameUi();
	gameUi->registry = &m_ctx.registry;
	gameUi->factory = &m_ctx.factory;
	gameUi->layout = &m_ctx.gui.Layout();
	gameUi->root = [this] { return m_gameOverlay; };
	Rt().onGameUiReset = [this] { ResetGameOverlay(); };
}

EditorUi::~EditorUi() {
	// Le runtime survit à l'interface : on débranche ce qui la cite.
	const auto &gameUi = Rt().GameUi();
	gameUi->root = nullptr;
	gameUi->registry = nullptr;
	gameUi->factory = nullptr;
	gameUi->layout = nullptr;
	Rt().onGameUiReset = nullptr;
	Rt().SetTextMeasure(nullptr);
}

void EditorUi::SetDirectories(const String &assets, const String &saves) {
	m_assetsDir = assets;
	m_savesDir = saves;
	m_assets.SetRoots(assets, saves);
	m_renderer2d.SetFontPath(assets + String("/fonts/DejaVuSans.ttf"));
}

void EditorUi::Build() {
	m_editorBuilt = false;
	if (!Rt().HasProject()) {
		BuildStartPage();
		return;
	}
	// Le navigateur de ressources montre le dossier du projet (assets,
	// scenes, scripts) à côté des ressources partagées.
	const String projectDir = Rt().ProjectDirectory();
	if (projectDir.IsEmpty())
		m_assets.SetRoots(m_assetsDir, m_savesDir);
	else
		m_assets.SetRoots(m_assetsDir, projectDir, String("Dossier du projet"));

	ui::WidgetBuilder root = m_ctx.factory.Column();
	root.Gap(0.f).Pad(0.f).Fixed().Anchor(ui::Anchor::TopLeft).Bg(kit::PaletteOf(m_ctx).base);
	root.W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Rpct(100.f));
	m_root = root.Spawn();

	BuildMenuBar(m_root);

	ui::WidgetBuilder body = m_ctx.factory.Row();
	body.Gap(0.f).Pad(math::Sides{4.f, 2.f, 4.f, 2.f}).GrowW().GrowH().Parent(m_root);
	m_body = body.Spawn();

	// Colonne principale : (arbre | documents) au-dessus de (ressources | console).
	ui::WidgetBuilder main = m_ctx.factory.Column();
	main.Pad(0.f);
	main.Gap(0.f).GrowW().GrowH().Parent(m_body);
	ecs::Entity mainEntity = main.Spawn();

	ui::WidgetBuilder top = m_ctx.factory.Row();
	top.Pad(0.f);
	top.Gap(0.f).GrowW().GrowH().Parent(mainEntity);
	ecs::Entity topEntity = top.Spawn();

	m_treeDock.Build(topEntity, [](ui::WidgetBuilder &b) { b.W(ui::Dimension::Px(LEFT_WIDTH)).GrowH(); });
	m_leftHandle = ResizeHandle(topEntity, ui::Orientation::Horizontal, m_treeDock.Frame(), true, 160.f, 700.f);
	m_documents.Build(topEntity, [](ui::WidgetBuilder &b) { b.GrowW().GrowH(); });

	ui::WidgetBuilder bottom = m_ctx.factory.Row();
	bottom.Pad(0.f);
	bottom.Gap(0.f).GrowW().H(ui::Dimension::Px(BOTTOM_HEIGHT)).Parent(mainEntity);
	m_bottomRow = bottom.Spawn();
	// La poignée PRÉCÈDE la rangée du bas : elle la redimensionne « après ».
	(void)ResizeHandle(mainEntity, ui::Orientation::Vertical, m_bottomRow, false, 90.f, 700.f);
	MoveBefore(mainEntity, m_bottomRow);

	m_assetDock.Build(m_bottomRow, [](ui::WidgetBuilder &b) { b.GrowW().GrowH(); });
	ui::WidgetBuilder consoleSlot = m_ctx.factory.Column();
	consoleSlot.Pad(0.f);
	consoleSlot.Gap(0.f).W(ui::Dimension::Px(CONSOLE_WIDTH)).GrowH().Parent(m_bottomRow);
	ecs::Entity consoleSlotEntity = consoleSlot.Spawn();
	(void)ResizeHandle(m_bottomRow, ui::Orientation::Horizontal, consoleSlotEntity, false, 200.f, 900.f);
	MoveBefore(m_bottomRow, consoleSlotEntity);
	m_consoleDock.Build(consoleSlotEntity, [](ui::WidgetBuilder &b) { b.GrowW().GrowH(); });

	// Colonne de droite : inspecteur au-dessus de la bibliothèque.
	ui::WidgetBuilder right = m_ctx.factory.Column();
	right.Pad(0.f);
	right.Gap(0.f).W(ui::Dimension::Px(RIGHT_WIDTH)).GrowH().Parent(m_body);
	m_rightColumn = right.Spawn();
	(void)ResizeHandle(m_body, ui::Orientation::Horizontal, m_rightColumn, false, 240.f, 700.f);
	MoveBefore(m_body, m_rightColumn);
	m_inspectorDock.Build(m_rightColumn, [](ui::WidgetBuilder &b) { b.GrowW().GrowH(); });
	ui::WidgetBuilder librarySlot = m_ctx.factory.Column();
	librarySlot.Pad(0.f);
	librarySlot.Gap(0.f).GrowW().H(ui::Dimension::Px(LIBRARY_HEIGHT)).Parent(m_rightColumn);
	ecs::Entity librarySlotEntity = librarySlot.Spawn();
	(void)ResizeHandle(m_rightColumn, ui::Orientation::Vertical, librarySlotEntity, false, 80.f, 600.f);
	MoveBefore(m_rightColumn, librarySlotEntity);
	m_libraryDock.Build(librarySlotEntity, [](ui::WidgetBuilder &b) { b.GrowW().GrowH(); });

	BuildStatusBar(m_root);

	// ── Contenu des panneaux ─────────────────────────────────────────
	m_tree.Build(m_treeDock.AddPage(String("Arbre de scène"), false, false, 4.f));
	m_sceneListPage = m_treeDock.AddPage(String("Scènes"), true);
	m_treeDock.SetActive(0);
	m_treeDock.onActivate = [this](int page) {
		if (page == 1)
			m_scenesDirty = true;
	};
	m_treeDock.onMenu = [this](float x, float y) { OpenMenu(m_treeMenu, x, y); };

	BuildViewportPage(m_documents.AddViewportPage(ViewportTitle()));
	m_documents.Dock().onMenu = [this](float x, float y) { OpenMenu(m_documentsMenu, x, y); };

	m_inspector.Build(m_inspectorDock.AddPage(String("Inspecteur"), true, false, 8.f));
	m_inspectorDock.onMenu = [this](float x, float y) { OpenMenu(m_inspectorMenu, x, y); };

	m_library.BuildMaterials(m_libraryDock.AddPage(String("Matériaux"), true));
	m_library.BuildScripts(m_libraryDock.AddPage(String("Scripts"), true));
	m_library.BuildWorld(m_libraryDock.AddPage(String("Monde"), true));
	m_libraryDock.SetActive(0);
	m_libraryDock.onActivate = [this](int page) {
		if (page == 1)
			m_library.RefreshScripts();
		if (page == 2)
			m_library.RefreshWorld();
	};

	m_assets.Build(m_assetDock.AddPage(String("Ressources"), false, false, 4.f));
	m_assetDock.onMenu = [this](float x, float y) { OpenMenu(m_assetsMenu, x, y); };

	BuildConsolePage(m_consoleDock.AddPage(String("Console"), false, false, 0.f));
	m_profilerPage = m_consoleDock.AddPage(String("Profil"), true);
	m_consoleDock.SetActive(0);
	m_consoleDock.onActivate = [this](int page) {
		if (page == 0)
			m_consoleDirty = true;
		if (page == 1)
			RefreshProfiler();
	};
	(void)kit::IconButton(m_ctx, m_consoleDock.HeaderExtra(), ui::MaterialIcons::DELETE, String("Effacer la console"),
						  [this] { OnClearConsole(); }, 22.f);

	BuildDockMenus();
	m_sceneName = ActiveSceneName();
	RefreshConsole();
	RefreshSceneList();
	m_editorBuilt = true;
}

void EditorUi::Rebuild() {
	Teardown();
	Build();
}

void EditorUi::Teardown() {
	CloseDialog();
	ExitRunMode();
	for (ecs::Entity popup : m_menuPopups)
		if (popup.Valid())
			ui::DespawnTree(m_ctx.registry, popup);
	m_menuPopups.clear();
	m_tree.Teardown();
	m_inspector.Teardown();
	m_library.Teardown();
	m_assets.Teardown();
	m_documents.Teardown();
	if (m_root.Valid()) {
		ui::DespawnTree(m_ctx.registry, m_root);
		m_root = ecs::Entity{};
	}
	m_maximized = false;
	m_editorBuilt = false;
}

bool EditorUi::SetTheme(const String &name) {
	Option<ui::UiTheme> theme = ThemeByName(name);
	if (theme.IsNone())
		return false;
	m_themeName = name.ToLower();
	m_ctx.gui.SetTheme(theme.Unwrap());
	kit::syntax::Colors() = kit::PaletteOf(m_ctx).light ? kit::syntax::Scheme::Light() : kit::syntax::Scheme::Dark();
	if (m_root.Valid())
		Rebuild();
	return true;
}

Option<ui::UiTheme> EditorUi::ThemeByName(const String &name) {
	String key = name.ToLower();
	if (key == "studio")
		return Some(ui::UiTheme::Studio());
	if (key == "dark")
		return Some(ui::UiTheme::Dark());
	if (key == "light")
		return Some(ui::UiTheme::Light());
	if (key == "aero")
		return Some(ui::UiTheme::Aero());
	return NONE;
}

sdl3::FRect EditorUi::ViewportRect() const {
	const ecs::Entity viewport = m_runMode ? m_runViewport : m_viewport;
	auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(viewport);
	return computed.IsSome() ? computed.Unwrap()->screen : sdl3::FRect{};
}

bool EditorUi::PointerOverViewport(float x, float y) const {
	const sdl3::FRect rect = ViewportRect();
	if (rect.w <= 0.f || rect.h <= 0.f)
		return false;
	if (x < rect.x || y < rect.y || x >= rect.x + rect.w || y >= rect.y + rect.h)
		return false;
	ecs::Entity front = m_ctx.gui.Input().FrontMostAt(m_ctx.registry, m_ctx.gui.Layout(), sdl3::FPoint{x, y});
	const ecs::Entity viewport = m_runMode ? m_runViewport : m_viewport;
	if (!front.Valid())
		return true;
	if (!ui::IsDescendantOrSelf(m_ctx.registry, front, viewport))
		return false;
	// Un widget de l'interface du jeu (bouton, curseur…) reçoit le clic
	// lui-même : la vue ne doit ni sélectionner l'objet derrière, ni bouger.
	const bool gameWidget = m_gameOverlay.Valid() && front != m_gameOverlay &&
							ui::IsDescendantOrSelf(m_ctx.registry, front, m_gameOverlay);
	return !gameWidget;
}

bool EditorUi::PointerInViewport() const {
	float x = 0.f, y = 0.f;
	SDL_GetMouseState(&x, &y);
	return PointerOverViewport(x, y);
}

Option<math::FRay> EditorUi::RayAt(float screenX, float screenY) const {
	const sdl3::FRect rect = ViewportRect();
	if (!PointerOverViewport(screenX, screenY))
		return NONE;
	return Some(Rt().ViewportRay(screenX - rect.x, screenY - rect.y, rect.w, rect.h));
}

void EditorUi::EnterRunMode() {
	if (m_runMode || !m_root.Valid() || !m_editorBuilt)
		return;
	if (!Rt().IsPlaying())
		Rt().Play();
	m_runMode = true;
	Rt().SetHostFullscreen(true);
	kit::SetHidden(m_ctx, m_root, true);

	ui::WidgetBuilder root = m_ctx.factory.Column();
	root.Fixed().Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Rpct(100.f));
	root.Gap(0.f).Pad(0.f).Bg(kit::Rgb(0, 0, 0));
	m_runRoot = root.Spawn();

	ui::WidgetBuilder viewport = m_ctx.factory.Viewport3D(Rt().SceneRoot(), Rt().ActiveCamera());
	viewport.GrowW().GrowH().Parent(m_runRoot);
	m_runViewport = viewport.Spawn();
	Spawn2DLayer(m_runViewport, true);

	// Réticule au centre, seulement quand la souris est capturée (vue
	// subjective) : libre, c'est le curseur qui vise.
	Runtime *runtime = &Rt();
	ui::WidgetBuilder crosshair = m_ctx.factory.Canvas([runtime](sdl3::Renderer &ren, sdl3::FRect r) {
		if (runtime->EffectiveMouseMode() != Runtime::MouseMode::CAPTURED)
			return;
		const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
		ren.SetDrawColor(sdl3::FColor{1.f, 1.f, 1.f, 0.85f});
		ren.FillRect({cx - 11.f, cy - 1.f, 8.f, 2.f});
		ren.FillRect({cx + 3.f, cy - 1.f, 8.f, 2.f});
		ren.FillRect({cx - 1.f, cy - 11.f, 2.f, 8.f});
		ren.FillRect({cx - 1.f, cy + 3.f, 2.f, 8.f});
	});
	crosshair.Absolute().Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Rpct(100.f));
	crosshair.PointerThrough().Parent(m_runViewport);
	(void)crosshair.Spawn();

	ui::WidgetBuilder hint = m_ctx.factory.Label(String("Échap : revenir à l'éditeur · ZQSD/WASD + souris : se déplacer · "
									  "F7 : libérer / capturer la souris · F9 : encart des scripts"));
	hint.Absolute().Anchor(ui::Anchor::TopLeft).Offset(14.f, 12.f).WAuto().HAuto().FontSize(12.f);
	hint.TextColor(sdl3::FColor{1.f, 1.f, 1.f, 0.6f}).PointerThrough().Parent(m_runViewport);
	(void)hint.Spawn();

	// Encart « MODE TEST » : les scripts qui tournent. Déplaçable par son
	// en-tête (souris libre), masquable par ✕ ou F9.
	ui::WidgetBuilder panel = m_ctx.factory.Column();
	// Fixed : dessiné (et touché) dans la passe des superpositions, au-dessus
	// de l'interface du jeu qu'il peut désormais survoler. La racine du mode
	// plein écran est elle aussi une superposition (ordre 0) : l'encart passe
	// explicitement APRÈS elle, sinon leur ordre relatif dépendrait de celui
	// de l'ECS — dessin et clics pourraient même ne pas s'accorder.
	panel.Fixed().OverlayOrder(10);
	panel.Anchor(ui::Anchor::BottomRight).Offset(m_runPanelOffset.x, m_runPanelOffset.y).WAuto().HAuto();
	panel.Gap(4.f).Pad(math::Sides{12.f, 8.f}).Radius(6.f).Bg(sdl3::FColor{0.08f, 0.08f, 0.09f, 0.82f});
	panel.BorderColor(kit::PaletteOf(m_ctx).separator).PointerThrough().Parent(m_runViewport);
	m_runPanel = panel.Spawn();

	ui::WidgetBuilder header = m_ctx.factory.Row();
	header.Pad(0.f);
	header.Gap(8.f).WAuto().HAuto().Align(ui::CrossAlign::Center).Parent(m_runPanel);
	header.Tooltip(String("Glisser pour déplacer (souris libre : F7)"));
	m_runPanelHeader = header.Spawn();
	(void)kit::Glyph(m_ctx, m_runPanelHeader, ui::MaterialIcons::DRAG_INDICATOR, sdl3::FColor{0.7f, 0.7f, 0.74f, 1.f},
					 16.f);
	ui::WidgetBuilder title = m_ctx.factory.Label(String("MODE TEST (Run Mode)"));
	title.FontSize(15.f).WAuto().HAuto().TextColor(sdl3::FColor{0.92f, 0.92f, 0.94f, 1.f}).PointerThrough();
	title.Parent(m_runPanelHeader);
	(void)title.Spawn();
	m_runPanelClose = kit::IconButton(m_ctx, m_runPanelHeader, ui::MaterialIcons::CLOSE,
									  String("Masquer l'encart (F9 pour le réafficher)"),
									  [this] { SetRunPanelHidden(true); }, 20.f);

	ui::WidgetBuilder body = m_ctx.factory.Column();
	body.Pad(0.f);
	body.Gap(4.f).WAuto().HAuto().PointerThrough().Parent(m_runPanel);
	m_runPanelBody = body.Spawn();
	m_draggingRunPanel = false;
	EnsureGameOverlay(); // l'interface du jeu suit dans la vue plein écran
	RefreshRunPanel();
	m_ctx.gui.Layout().MarkDirty();
	SetStatus(String("Mode Jeu plein écran — Échap pour revenir"));
}

void EditorUi::ExitRunMode() {
	if (!m_runMode)
		return;
	m_runMode = false;
	Rt().SetHostFullscreen(false);
	EnsureGameOverlay(); // ramenée dans la vue de l'éditeur avant de détruire l'autre
	if (m_runRoot.Valid())
		ui::DespawnTree(m_ctx.registry, m_runRoot);
	m_runRoot = m_runViewport = m_runPanel = ecs::Entity{};
	m_runPanelHeader = m_runPanelClose = m_runPanelBody = ecs::Entity{};
	m_draggingRunPanel = false;
	kit::SetHidden(m_ctx, m_root, false);
	if (Rt().IsPlaying())
		Rt().Stop();
}

void EditorUi::EnsureGameOverlay() {
	const ecs::Entity viewport = GameViewport();
	if (!Rt().IsPlaying() || !viewport.Valid() || !m_ctx.registry.IsAlive(viewport))
		return;
	if (m_gameOverlay.Valid() && m_ctx.registry.IsAlive(m_gameOverlay)) {
		auto parent = m_ctx.registry.GetComponent<ui::UiParent>(m_gameOverlay);
		if (parent.IsSome() && parent.Unwrap()->parent != viewport) {
			ui::SetParent(m_ctx.registry, m_gameOverlay, viewport);
			m_ctx.gui.Layout().MarkDirty();
		}
		return;
	}
	ui::WidgetBuilder overlay = m_ctx.factory.Column();
	// Pct (du PARENT, la vue) et non Rpct (de la fenêtre) : dans l'éditeur, la
	// vue ne remplit pas la fenêtre.
	overlay.Absolute().Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Pct(100.f)).H(ui::Dimension::Pct(100.f));
	overlay.Gap(0.f).Pad(0.f).PointerThrough().Parent(viewport);
	m_gameOverlay = overlay.Spawn();
	m_ctx.gui.Layout().MarkDirty();
}

void EditorUi::ResetGameOverlay() {
	if (m_gameOverlay.Valid() && m_ctx.registry.IsAlive(m_gameOverlay))
		ui::DespawnTree(m_ctx.registry, m_gameOverlay);
	m_gameOverlay = ecs::Entity{};
	m_ctx.gui.Layout().MarkDirty();
	EnsureGameOverlay();
}

void EditorUi::HandleEvent(const sdl3::Event &event) {
	// Avant le filtre « consommé » : l'interface peut avoir pris le clic sur
	// l'en-tête de l'encart, c'est pourtant bien lui qu'on tire.
	if (m_runMode && HandleRunPanelDrag(event))
		return;
	if (event.IsConsumed())
		return;
	const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
	if (ctrl && event.IsKeyDown(SDLK_N)) {
		OnNewProject();
		return;
	}
	if (ctrl && event.IsKeyDown(SDLK_O)) {
		OnOpenProject();
		return;
	}
	if (!m_editorBuilt)
		return; // page « Aucun projet ouvert » : rien d'autre à piloter
	if (m_runMode) {
		HandleRunModeEvent(event);
		return;
	}
	if (HandleGameMouse(event))
		return;
	if (Is2DEditing())
		Handle2DEvent(event);
	else
		Handle3DMouse(event);

	const bool control = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
	// Ctrl+S et Ctrl+Entrée valent AUSSI dans un champ (c'est là qu'on
	// écrit un script) : la règle de transmission de l'interface les
	// laisse passer, comme les touches de fonction.
	if (control && event.IsKeyDown(SDLK_S)) {
		if (!m_documents.SaveActive())
			OnSaveProject();
		return;
	}
	if (control && event.IsKeyDown(SDLK_RETURN)) {
		m_documents.RunActive();
		return;
	}

	if (control && event.IsKeyDown(SDLK_Z)) {
		if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0)
			OnRedo();
		else
			OnUndo();
	}
	if (control && event.IsKeyDown(SDLK_Y))
		OnRedo();
	if (control && event.IsKeyDown(SDLK_D))
		OnDuplicate();
	if (!control && !m_rightMouseDown) {
		if (event.IsKeyDown(SDLK_W))
			SetGizmoMode(Runtime::GizmoMode::TRANSLATE);
		if (event.IsKeyDown(SDLK_E))
			SetGizmoMode(Runtime::GizmoMode::ROTATE);
		if (event.IsKeyDown(SDLK_R))
			SetGizmoMode(Runtime::GizmoMode::SCALE);
		if (event.IsKeyDown(SDLK_X))
			ToggleSnap();
	}
	if (event.IsKeyDown(SDLK_F5))
		Rt().TogglePlay();
	if (event.IsKeyDown(SDLK_F6))
		EnterRunMode();
	if (event.IsKeyDown(SDLK_F2))
		m_tree.BeginRename(Rt().SelectedId());
	if (event.IsKeyDown(SDLK_DELETE))
		OnDeleteSelection();
	if (event.IsKeyDown(SDLK_F))
		OnFocusSelection();
	if (event.IsKeyDown(SDLK_ESCAPE) && !m_ctx.gui.HasOpenModal())
		Rt().ClearSelection();
}

void EditorUi::Tick(float dt, double fps) {
	if (m_rebuildRequested) {
		m_rebuildRequested = false;
		Rebuild();
	}
	ApplyBrowseResult();
	if (!m_editorBuilt)
		return;
	m_fps = fps;
	EnsureGameOverlay();
	PollCameraKeys(dt);
	const float aspect = UpdateViewportCamera(m_runMode ? m_runViewport : m_viewport);
	if (aspect > 0.f)
		Rt().SetViewportAspect(aspect);

	// Changement de scène : les identifiants de nœud ne désignent plus
	// les mêmes objets, l'état de dépli de l'arbre repart de zéro.
	const String sceneName = ActiveSceneName();
	if (sceneName != m_sceneName) {
		m_sceneName = sceneName;
		m_tree.ResetExpansion();
		m_documents.Dock().SetTitle(0, ViewportTitle());
		m_library.RefreshWorld();
		m_scenesDirty = true;
	}

	if (m_runMode) {
		if (++m_tickCount % RUN_PANEL_REFRESH_FRAMES == 0)
			RefreshRunPanel();
		if (!Rt().IsPlaying())
			ExitRunMode(); // la partie s'est arrêtée (script, F5)
		return;
	}

	if (m_treeDock.Active() == 0)
		m_tree.Tick();
	if (m_scenesDirty && m_treeDock.Active() == 1) {
		m_scenesDirty = false;
		RefreshSceneList();
	}
	m_inspector.Tick();
	m_assets.Tick(dt);
	m_documents.Tick();
	if (m_consoleDirty && m_consoleDock.Active() == 0) {
		m_consoleDirty = false;
		RefreshConsole();
	}
	if (++m_tickCount % PROFILER_REFRESH_FRAMES == 0 && m_consoleDock.Active() == 1)
		RefreshProfiler();
	RefreshStatusBar();
}

void EditorUi::Handle3DMouse(const sdl3::Event &event) {
	if (event.IsMouseDown(SDL_BUTTON_RIGHT))
		m_rightMouseDown = PointerInViewport();
	if (event.IsMouseUp(SDL_BUTTON_RIGHT))
		m_rightMouseDown = false;
	if (event.IsMouseDown(SDL_BUTTON_MIDDLE))
		m_middleMouseDown = PointerInViewport();
	if (event.IsMouseUp(SDL_BUTTON_MIDDLE))
		m_middleMouseDown = false;
	if (event.IsMouseUp(SDL_BUTTON_LEFT))
		m_orbiting = false;

	if (event.IsMouseMotion() && m_rightMouseDown) {
		constexpr float SENSITIVITY = 0.0035f;
		Rt().ApplyLookDelta(float(event.MouseMotion().xrel) * SENSITIVITY,
							-float(event.MouseMotion().yrel) * SENSITIVITY);
	}
	if (event.IsMouseMotion() && m_middleMouseDown) {
		const float scale = Rt().PivotDistance() * 0.0016f;
		Rt().PanCamera(-float(event.MouseMotion().xrel) * scale, float(event.MouseMotion().yrel) * scale);
	}
	if (event.IsMouseMotion() && m_orbiting) {
		constexpr float SENSITIVITY = 0.006f;
		Rt().OrbitCamera(float(event.MouseMotion().xrel) * SENSITIVITY,
						 -float(event.MouseMotion().yrel) * SENSITIVITY, m_orbitPivot);
	}
	if (event.IsMouseWheel() && PointerInViewport())
		Rt().DollyCamera(float(event.MouseWheel().y) * Rt().PivotDistance() * 0.18f);

	if (event.IsMouseDown(SDL_BUTTON_LEFT) && (SDL_GetModState() & SDL_KMOD_ALT) != 0) {
		if (RayAt(event.MouseButton().x, event.MouseButton().y).IsSome()) {
			m_orbitPivot = Rt().CameraPivot();
			m_orbiting = true;
		}
	} else if (event.IsMouseDown(SDL_BUTTON_LEFT)) {
		if (Option<math::FRay> ray = RayAt(event.MouseButton().x, event.MouseButton().y); ray.IsSome()) {
			const Runtime::GizmoAxis axis = Rt().PickGizmoAxis(ray.Unwrap());
			if (axis != Runtime::GizmoAxis::NONE)
				(void)Rt().BeginGizmoDrag(axis, ray.Unwrap());
			else
				(void)Rt().SelectAt(ray.Unwrap());
		}
	}
	if (event.IsMouseUp(SDL_BUTTON_LEFT))
		Rt().EndGizmoDrag();
	if (event.IsMouseMotion() && !m_rightMouseDown && !m_middleMouseDown && !m_orbiting) {
		if (Option<math::FRay> ray = RayAt(event.MouseMotion().x, event.MouseMotion().y); ray.IsSome()) {
			if (Rt().IsGizmoDragging())
				(void)Rt().UpdateGizmoDrag(ray.Unwrap());
			else
				Rt().SetHoveredAxis(Rt().PickGizmoAxis(ray.Unwrap()));
		} else if (!Rt().IsGizmoDragging()) {
			Rt().SetHoveredAxis(Runtime::GizmoAxis::NONE);
		}
	}
}

void EditorUi::Set2DView(bool enabled) {
	if (m_view2d == enabled)
		return;
	m_view2d = enabled;
	m_drag2d = NONE;
	m_panning2d = false;
	Show2DWorkspace();
	SetStatus(String(enabled ? "Espace de travail 2D" : "Espace de travail 3D"));
}

void EditorUi::Show2DWorkspace() {
	kit::SetHidden(m_ctx, m_axesGizmo, m_view2d);
	kit::SetLabelText(m_ctx, m_projectionLabel, String(m_view2d ? "" : "‹ Persp"));
}

void EditorUi::FollowSelectionWorkspace() {
	const scene::Node *node = Rt().SelectedObject();
	if (!node || !m_editorBuilt || Rt().IsPlaying())
		return;
	if (Is2DNode(*node))
		Set2DView(true);
	else if (VisualDesc::Has(*node))
		Set2DView(false);
}

String EditorUi::ResolveAsset(const String &path) const {
	if (path.IsEmpty() || path.CStr()[0] == '/')
		return path;
	const String project = Rt().ProjectDirectory();
	if (!project.IsEmpty() && files::IsFile(files::Join(project, path)))
		return files::Join(project, path);
	if (files::IsFile(files::Join(m_assetsDir, path)))
		return files::Join(m_assetsDir, path);
	return path;
}

void EditorUi::Spawn2DLayer(ecs::Entity viewport, bool runView) {
	ui::WidgetBuilder layer = m_ctx.factory.Canvas(
		[this, runView](sdl3::Renderer &ren, sdl3::FRect r) { Draw2DLayer(ren, r, runView); });
	layer.Absolute().Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Pct(100.f)).H(ui::Dimension::Pct(100.f));
	layer.PointerThrough().Parent(viewport);
	(void)layer.Spawn();
}

void EditorUi::Draw2DLayer(sdl3::Renderer &ren, sdl3::FRect r, bool runView) {
	SceneDesc *scene = Rt().ActiveScene();
	if (!scene || r.w < 2.f || r.h < 2.f)
		return;
	if (Rt().IsPlaying()) {
		if (runView != m_runMode)
			return; // seule la vue qui montre le jeu le dessine
		Rt().SetView2DRect(r);
		if (!scene->canvas.render3d)
			Canvas2DRenderer::FillRect(ren, r, scene->canvas.background);
		const Canvas2DFrame frame = Rt().Frame2D();
		m_renderer2d.Draw(ren, Rt().GameView2D(r, frame), frame.items);
		return;
	}
	if (runView || !m_view2d)
		return;
	const math::FVector2 reference = scene->canvas.Size();
	if (m_edit2dZoom <= 0.f || m_edit2dScene != scene->name) {
		m_edit2dScene = scene->name;
		m_edit2dZoom = View2D::FitZoom(r, reference);
		m_edit2dCenter = {reference.x * 0.5f, reference.y * 0.5f, 0.f};
	}
	m_edit2dView = View2D::Editor(r, reference, {m_edit2dCenter.x, m_edit2dCenter.y}, m_edit2dZoom);
	Canvas2DRenderer::FillRect(ren, r, scene->canvas.render3d ? sdl3::Color{34, 36, 42, 255}
																: scene->canvas.background);
	DrawGrid2D(ren, r);
	const Canvas2DFrame frame = Rt().SceneFrame2D();
	m_renderer2d.Draw(ren, m_edit2dView, frame.items);
	DrawEditorOverlays2D(ren, frame, reference);
}

void EditorUi::DrawGrid2D(sdl3::Renderer &ren, sdl3::FRect r) {
	Option<Transform2D> inverse = m_edit2dView.world.Inverse();
	if (inverse.IsNone())
		return;
	const math::FVector2 a = inverse.Value().Apply({r.x, r.y});
	const math::FVector2 b = inverse.Value().Apply({r.x + r.w, r.y + r.h});
	float step = 32.f;
	while (step * m_edit2dZoom < 12.f)
		step *= 2.f;
	while (step * m_edit2dZoom > 64.f && step > 1.f)
		step *= 0.5f;
	const Transform2D &w = m_edit2dView.world;
	for (int axis = 0; axis < 2; ++axis) {
		const float from = axis == 0 ? a.x : a.y, to = axis == 0 ? b.x : b.y;
		for (float v = std::floor(from / step) * step; v <= to; v += step) {
			const bool major = std::fmod(std::fabs(v), step * 4.f) < 0.5f;
			const bool origin = std::fabs(v) < 0.5f;
			ren.SetDrawColor(origin ? (axis == 0 ? kit::AXIS_Y : kit::AXIS_X)
									: sdl3::FColor{1.f, 1.f, 1.f, major ? 0.09f : 0.035f});
			if (axis == 0) {
				const float x = w.Apply({v, 0.f}).x;
				ren.DrawLine(x, r.y, x, r.y + r.h);
			} else {
				const float y = w.Apply({0.f, v}).y;
				ren.DrawLine(r.x, y, r.x + r.w, y);
			}
		}
	}
}

void EditorUi::DrawEditorOverlays2D(sdl3::Renderer &ren, const Canvas2DFrame &frame, math::FVector2 reference) {
	const sdl3::FRect f = m_edit2dView.frame;
	auto rect = [](float x, float y, float w, float h) {
		return std::vector<sdl3::FPoint>{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
	};
	Canvas2DRenderer::Stroke(ren, rect(f.x, f.y, f.w, f.h), true, 1.5f, sdl3::FColor{0.45f, 0.62f, 1.f, 0.9f});
	CanvasItemDesc caption;
	caption.kind = CanvasItemKind::TEXT;
	caption.text = String::Format("Écran %d × %d", int(reference.x), int(reference.y));
	caption.fontSize = 12.f;
	caption.align = TextAlign2D::LEFT;
	caption.centered = false;
	caption.color = sdl3::Color{130, 160, 255, 230};
	m_renderer2d.DrawItem(ren, Transform2D::Translate(f.x + 2.f, f.y - 17.f), caption);

	for (const Camera2DState &camera : frame.cameras) {
		const math::FVector2 half{reference.x * 0.5f / camera.zoom, reference.y * 0.5f / camera.zoom};
		const math::FVector2 p0 = m_edit2dView.world.Apply({camera.position.x - half.x, camera.position.y - half.y});
		const math::FVector2 p1 = m_edit2dView.world.Apply({camera.position.x + half.x, camera.position.y + half.y});
		const bool current = frame.camera.IsSome() && frame.camera.Value().id == camera.id;
		Canvas2DRenderer::Stroke(ren, rect(p0.x, p0.y, p1.x - p0.x, p1.y - p0.y), true, current ? 2.f : 1.f,
								 sdl3::FColor{0.78f, 0.45f, 1.f, current ? 0.95f : 0.5f});
	}

	const scene::NodeId selected = Rt().SelectedId();
	const SceneDesc *scene = Rt().ActiveScene();
	if (!selected.Valid() || !scene)
		return;
	const sdl3::FColor orange{1.f, 0.62f, 0.2f, 1.f};
	for (const DrawItem2D &item : frame.items)
		if (item.id == selected) {
			const auto corners = ScreenCorners(item, m_edit2dView, Rt().TextMeasurer());
			std::vector<sdl3::FPoint> outline;
			for (const math::FVector2 &c : corners)
				outline.push_back({c.x, c.y});
			Canvas2DRenderer::Stroke(ren, outline, true, 1.5f, orange);
			for (const sdl3::FPoint &c : outline)
				Canvas2DRenderer::FillDisc(ren, c, 3.5f, orange);
			return;
		}
	// Nœud sans apparence (pivot, caméra) : une croix à sa position.
	if (Option<NodePlacement2D> placement = PlaceNode2D(*scene, selected); placement.IsSome()) {
		const math::FVector2 o = m_edit2dView.For(placement.Value().space).Apply(placement.Value().transform.Origin());
		Canvas2DRenderer::Stroke(ren, {{o.x - 9.f, o.y}, {o.x + 9.f, o.y}}, false, 2.f, orange);
		Canvas2DRenderer::Stroke(ren, {{o.x, o.y - 9.f}, {o.x, o.y + 9.f}}, false, 2.f, orange);
	}
}

bool EditorUi::Focus2D(scene::NodeId id) {
	const SceneDesc *scene = Rt().ActiveScene();
	if (!scene)
		return false;
	Option<NodePlacement2D> placement = PlaceNode2D(*scene, id);
	if (placement.IsNone())
		return false;
	const math::FVector2 o = placement.Value().transform.Origin();
	m_edit2dCenter = {o.x, o.y, 0.f};
	return true;
}

void EditorUi::Handle2DEvent(const sdl3::Event &event) {
	const View2D &view = m_edit2dView;
	if (event.IsMouseWheel() && PointerInViewport()) {
		float mx = 0.f, my = 0.f;
		SDL_GetMouseState(&mx, &my);
		Option<Transform2D> inverse = view.world.Inverse();
		if (inverse.IsNone())
			return;
		const math::FVector2 anchor = inverse.Value().Apply({mx, my}); // point fixe sous la souris
		const float zoom = std::clamp(m_edit2dZoom * std::pow(1.15f, float(event.MouseWheel().y)), 0.02f, 40.f);
		const sdl3::FRect r = ViewportRect();
		m_edit2dCenter = {anchor.x - (mx - (r.x + r.w * 0.5f)) / zoom, anchor.y - (my - (r.y + r.h * 0.5f)) / zoom,
						  0.f};
		m_edit2dZoom = zoom;
		return;
	}
	if ((event.IsMouseDown(SDL_BUTTON_MIDDLE) || event.IsMouseDown(SDL_BUTTON_RIGHT)) && PointerInViewport())
		m_panning2d = true;
	if (event.IsMouseUp(SDL_BUTTON_MIDDLE) || event.IsMouseUp(SDL_BUTTON_RIGHT))
		m_panning2d = false;
	if (event.IsMouseMotion() && m_panning2d) {
		m_edit2dCenter.x -= float(event.MouseMotion().xrel) / m_edit2dZoom;
		m_edit2dCenter.y -= float(event.MouseMotion().yrel) / m_edit2dZoom;
		return;
	}
	if (event.IsMouseDown(SDL_BUTTON_LEFT)) {
		const float x = event.MouseButton().x, y = event.MouseButton().y;
		if (!PointerOverViewport(x, y))
			return;
		const Canvas2DFrame frame = Rt().SceneFrame2D();
		Option<size_t> hit = PickItem(frame.items, view, {x, y}, 4.f, Rt().TextMeasurer());
		if (hit.IsNone()) {
			Rt().ClearSelection();
			return;
		}
		const scene::NodeId id = frame.items[hit.Unwrap()].id;
		(void)Rt().Select(id);
		const scene::Node *node = Rt().FindObject(id);
		const SceneDesc *scene = Rt().ActiveScene();
		if (!node || node->locked || !scene)
			return;
		Option<NodePlacement2D> placement = PlaceNode2D(*scene, id);
		if (placement.IsNone())
			return;
		Rt().BeginEdit(String::Format("Déplacer %s", node->name.CStr()));
		m_drag2d = Some(Drag2D{id, {x, y}, node->transform.position,
							   view.For(placement.Value().space) * placement.Value().parent});
		return;
	}
	if (event.IsMouseUp(SDL_BUTTON_LEFT) && m_drag2d.IsSome()) {
		m_drag2d = NONE;
		Rt().EndEdit();
		return;
	}
	if (event.IsMouseMotion() && m_drag2d.IsSome()) {
		const Drag2D &drag = m_drag2d.Value();
		Option<Transform2D> inverse = drag.parentToPixels.Inverse();
		if (inverse.IsNone())
			return;
		const math::FVector2 delta = inverse.Value().ApplyVector(
			{event.MouseMotion().x - drag.startPixel.x, event.MouseMotion().y - drag.startPixel.y});
		math::FVector3 position = drag.startPosition;
		position.x += delta.x;
		position.y += delta.y;
		if (Rt().SnapEnabled()) { // magnétisme : grille de 8 px
			position.x = std::round(position.x / 8.f) * 8.f;
			position.y = std::round(position.y / 8.f) * 8.f;
		}
		(void)Rt().SetPosition(drag.id, position);
		return;
	}
	// Flèches : décale la sélection d'un pixel (dix avec Maj).
	const scene::Node *selected = Rt().SelectedObject();
	if (event.Type() == SDL_EVENT_KEY_DOWN && selected && Is2DNode(*selected) && !TextFieldHasFocus()) {
		const float step = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 10.f : 1.f;
		math::FVector3 position = selected->transform.position;
		const SDL_Keycode key = event.Key().key;
		if (key == SDLK_LEFT)
			position.x -= step;
		else if (key == SDLK_RIGHT)
			position.x += step;
		else if (key == SDLK_UP)
			position.y -= step;
		else if (key == SDLK_DOWN)
			position.y += step;
		else
			return;
		(void)Rt().SetPosition(selected->id, position);
	}
}

bool EditorUi::FocusPanel(const String &panel, int tab) {
	if (!m_editorBuilt)
		return false;
	const String key = panel.ToLower();
	if (key == "outliner" || key == "tree") {
		m_treeDock.SetActive(0);
		return true;
	}
	if (key == "scenes") {
		m_treeDock.SetActive(1);
		return true;
	}
	if (key == "assets") {
		m_assetDock.SetActive(0);
		return true;
	}
	if (key == "inspector") {
		if (tab == int(InspectorTab::WORLD)) {
			m_libraryDock.SetActive(2);
			return true;
		}
		m_inspectorDock.SetActive(0);
		m_inspector.FocusSection(String(InspectorPanel::SectionForTab(tab)));
		return true;
	}
	if (key == "materials" || key == "library") {
		m_libraryDock.SetActive(0);
		return true;
	}
	if (key == "scripts") {
		m_libraryDock.SetActive(1);
		return true;
	}
	if (key == "world") {
		m_libraryDock.SetActive(2);
		return true;
	}
	if (key == "console") {
		m_consoleDock.SetActive(0);
		return true;
	}
	if (key == "profiler") {
		m_consoleDock.SetActive(1);
		return true;
	}
	if (key == "script") {
		m_documents.OpenToolConsole();
		return true;
	}
	if (key == "viewport") {
		m_documents.Dock().SetActive(0);
		return true;
	}
	return false;
}

void EditorUi::SetStatus(String message) {
	m_statusMessage = std::move(message);
	kit::SetLabelText(m_ctx, m_statusMessageLabel, m_statusMessage);
}

bool EditorUi::UiCommand(const String &command, const String &argument) {
	const String cmd = command.ToLower();
	if (cmd == "view" && m_editorBuilt && (argument == "2d" || argument == "3d")) {
		Set2DView(argument == "2d");
		return true;
	}
	// Boîtes de dialogue de fichiers (valent aussi sur la page d'accueil).
	if (cmd == "dialog") {
		if (argument == "new_project")
			OnNewProject();
		else if (argument == "open_project")
			OnOpenProject();
		else if (argument == "save_scene_as" && m_editorBuilt)
			OnSaveSceneAs();
		else if (argument == "import_scene" && m_editorBuilt)
			OnImportScene();
		else if (argument == "close")
			CloseDialog();
		else
			return false;
		return true;
	}
	if (cmd == "dialog_text") {
		SetDialogText(argument);
		return m_dialog.Valid();
	}
	if (cmd == "dialog_confirm") {
		ConfirmDialog();
		return true;
	}
	if (!m_editorBuilt)
		return false;
	auto nodeId = [this](const String &name) { return Rt().ResolveId(name); };
	if (cmd == "select") {
		const scene::NodeId id = nodeId(argument);
		return id.Valid() && Rt().Select(id);
	}
	if (cmd == "context_menu") {
		const scene::NodeId id = nodeId(argument);
		if (!id.Valid())
			return false;
		(void)Rt().Select(id);
		m_tree.RevealSelection();
		m_tree.Tick(); // lignes à jour ; leur position vient au prochain rendu
		Option<sdl3::FRect> row = m_tree.RowRect(id);
		const sdl3::FRect r = row.IsSome() ? row.Unwrap() : sdl3::FRect{40.f, 200.f, 200.f, 22.f};
		m_tree.OpenContextMenu(id, r.x + r.w * 0.55f, r.y + r.h * 0.7f);
		return true;
	}
	if (cmd == "section") {
		m_inspectorDock.SetActive(0);
		m_inspector.FocusSection(argument);
		return true;
	}
	if (cmd == "context_submenu")
		return m_tree.OpenCreateSubmenu();
	if (cmd == "create")
		return m_tree.CreateNode(argument, Rt().SelectedId()).IsSome();
	if (cmd == "rename") {
		const scene::NodeId id = nodeId(argument);
		if (!id.Valid())
			return false;
		m_tree.BeginRename(id);
		return true;
	}
	if (cmd == "close_menus") {
		std::vector<ecs::Entity> open;
		m_ctx.registry.Query<ui::UiPopupState>([&open](ecs::Entity e, ui::UiPopupState &state) {
			if (state.open)
				open.push_back(e);
		});
		for (ecs::Entity e : open) {
			if (m_ctx.registry.GetComponent<ui::UiPopupState>(e).Unwrap()->modal)
				m_ctx.gui.CloseModal(e);
			else
				m_ctx.gui.ClosePopup(e);
		}
		return true;
	}
	if (cmd == "browse") {
		m_assetDock.SetActive(0);
		const String where = argument.StartsWith(AssetBrowserModel::ROOT) ? argument
							 : argument.IsEmpty()                     ? String(AssetBrowserModel::ROOT)
																	  : m_assetsDir + String("/") + argument;
		const bool moved = m_assets.Model().Navigate(where) || m_assets.Model().Location() == where;
		m_assets.MarkDirty();
		return moved;
	}
	if (cmd == "select_asset")
		return m_assets.SelectByName(argument);
	if (cmd == "open_script") {
		m_documents.OpenLibraryScript(argument);
		return m_documents.Active() != nullptr;
	}
	if (cmd == "open_scene_script") {
		m_documents.OpenSceneScript(argument.IsEmpty() ? ActiveSceneName() : argument);
		return m_documents.Active() != nullptr;
	}
	if (cmd == "open_console") {
		m_documents.OpenToolConsole();
		return true;
	}
	if (cmd == "open_json") {
		const size_t bar = argument.Find('|');
		const String name = bar == String::NPOS ? argument : argument.Substr(0, bar);
		const String component = bar == String::NPOS ? String() : argument.Substr(bar + 1);
		const scene::NodeId id = nodeId(name);
		if (!id.Valid())
			return false;
		m_documents.OpenComponentJson(id, component);
		return m_documents.Active() != nullptr;
	}
	if (cmd == "document") {
		m_documents.Dock().SetActive(int(std::strtol(argument.CStr(), nullptr, 10)));
		return true;
	}
	if (cmd == "library") {
		m_libraryDock.SetActive(int(std::strtol(argument.CStr(), nullptr, 10)));
		return true;
	}
	if (cmd == "run_mode") {
		if (argument == "off")
			ExitRunMode();
		else
			EnterRunMode();
		return true;
	}
	if (cmd == "maximize") {
		ToggleMaximized();
		return true;
	}
	return false;
}

InspectorActions EditorUi::MakeInspectorActions() {
	InspectorActions actions;
	actions.openScript = [this](const String &name) { m_documents.OpenLibraryScript(name); };
	actions.editJson = [this](scene::NodeId id, const String &type) { m_documents.OpenComponentJson(id, type); };
	actions.status = [this](const String &text) { SetStatus(text); };
	return actions;
}

AssetActions EditorUi::MakeAssetActions() {
	AssetActions actions;
	actions.openScript = [this](const String &name) { m_documents.OpenLibraryScript(name); };
	actions.openSceneScript = [this](const String &scene) { m_documents.OpenSceneScript(scene); };
	actions.openFile = [this](const String &path) { m_documents.OpenFile(path); };
	actions.importModel = [this](const String &path) { ImportModel(path); };
	actions.openScene = [this](const String &scene) { (void)Rt().SwitchScene(scene); };
	actions.instantiateScene = [this](const String &path) {
		auto instance = Rt().InstantiateSceneFile(path);
		if (instance.IsError()) {
			Rt().LogError(String::Format("Instanciation : %s", instance.Error().CStr()));
			return;
		}
		(void)Rt().Select(instance.Value());
		SetStatus(String::Format("Scène instanciée : %s", path.CStr()));
	};
	actions.status = [this](const String &text) { SetStatus(text); };
	return actions;
}

String EditorUi::ActiveSceneName() const {
	const SceneDesc *scene = Rt().ActiveScene();
	return scene ? scene->name : String();
}

math::FVector3 EditorUi::SpawnPointInFrontOfCamera(float lift) const {
	const render3d::Camera &camera = Rt().ActiveCamera();
	const math::FVector3 forward = (camera.target - camera.position).Normalize();
	math::FVector3 at = camera.position + forward * 7.f;
	at.y = sdl3::Max(at.y, lift * 0.5f);
	return at;
}

ecs::Entity EditorUi::ResizeHandle(ecs::Entity parent, ui::Orientation orient, ecs::Entity target, bool before, float minSize,
		float maxSize) {
	const bool horizontal = orient == ui::Orientation::Horizontal;
	ui::WidgetBuilder handle = m_ctx.factory.Row();
	handle.Pad(0.f);
	if (horizontal)
		handle.W(ui::Dimension::Px(5.f)).GrowH();
	else
		handle.GrowW().H(ui::Dimension::Px(5.f));
	handle.Bg(kit::PaletteOf(m_ctx).base).Parent(parent);
	ecs::Entity entity = handle.Spawn();

	ui::UiResizeHandle h;
	h.orient = orient;
	ecs::ArchetypeRegistry *world = &m_ctx.registry;
	auto get = [world, target, horizontal]() -> float {
		if (auto item = world->GetComponent<ui::UiItem>(target); item.IsSome())
			return horizontal ? item.Unwrap()->width.value : item.Unwrap()->height.value;
		return 0.f;
	};
	auto set = [world, target, horizontal](float v) {
		if (auto item = world->GetComponent<ui::UiItem>(target); item.IsSome()) {
			if (horizontal)
				item.Unwrap()->width = ui::Dimension::Px(v);
			else
				item.Unwrap()->height = ui::Dimension::Px(v);
		}
	};
	if (before) {
		h.getBefore = get;
		h.setBefore = set;
		h.minBefore = minSize;
		h.maxBefore = maxSize;
	} else {
		h.getAfter = get;
		h.setAfter = set;
		h.minAfter = minSize;
		h.maxAfter = maxSize;
	}
	m_ctx.registry.AddComponent(entity, std::move(h));
	return entity;
}

void EditorUi::MoveBefore(ecs::Entity parent, ecs::Entity anchor) {
	auto children = m_ctx.registry.GetComponent<ui::UiChildren>(parent);
	if (children.IsNone() || children.Unwrap()->list.size() < 2)
		return;
	std::vector<ecs::Entity> &list = children.Unwrap()->list;
	const ecs::Entity moved = list.back();
	list.pop_back();
	list.insert(std::find(list.begin(), list.end(), anchor), moved);
	m_ctx.gui.Layout().MarkDirty();
}

ui::WidgetBuilder EditorUi::MenuAction(const char *text, const char *shortcut, std::function<void()> action) {
	ui::WidgetBuilder item = m_ctx.factory.MenuItem(String(text), String(shortcut));
	item.OnClick(std::move(action));
	return item;
}

void EditorUi::AddItem(ecs::Entity menu, const char *text, const char *shortcut, std::function<void()> action) {
	ui::WidgetBuilder item = MenuAction(text, shortcut, std::move(action));
	item.Parent(menu);
	(void)item.Spawn();
}

void EditorUi::BuildMenuBar(ecs::Entity parent) {
	ui::WidgetBuilder bar = m_ctx.factory.MenuBar();
	bar.Parent(parent).GrowW().HAuto().Pad(math::Sides{6.f, 2.f}).Bg(kit::PaletteOf(m_ctx).header);
	ecs::Entity barEntity = bar.Spawn();

	ecs::Entity file = m_ctx.factory.Menu(
		barEntity, String("Fichier"), MenuAction("Nouveau projet…", "Ctrl+N", [this] { OnNewProject(); }),
		MenuAction("Ouvrir un projet…", "Ctrl+O", [this] { OnOpenProject(); }),
		MenuAction("Enregistrer le projet", "Ctrl+S", [this] { OnSaveProject(); }),
		MenuAction("Enregistrer le projet sous…", "", [this] { OnSaveProjectAs(); }),
		MenuAction("Nouvelle scène", "", [this] { OnNewScene(); }),
		MenuAction("Enregistrer la scène sous…", "", [this] { OnSaveSceneAs(); }),
		MenuAction("Importer une scène…", "", [this] { OnImportScene(); }),
		MenuAction("Importer un script…", "", [this] { OnImportScript(); }),
		MenuAction("Enregistrer la sélection comme scène", "", [this] { OnSaveSelectionAsScene(); }),
		MenuAction("Exporter le rapport", "", [this] { OnExportReport(); }),
		MenuAction("Fermer le projet", "", [this] { OnCloseProject(); }),
		MenuAction("Quitter", "Ctrl+Q", [this] {
			if (onQuit)
				onQuit();
		}));
	m_menuPopups.push_back(file);
	ecs::Entity scenes = m_ctx.factory.SubMenu(file, String("Ouvrir une scène"));
	m_menuPopups.push_back(scenes);
	for (const String &name : Rt().GetProject().SceneNames())
		AddItem(scenes, name.CStr(), "", [this, name] { (void)Rt().SwitchScene(name); });

	m_menuPopups.push_back(m_ctx.factory.Menu(
		barEntity, String("Édition"), MenuAction("Annuler", "Ctrl+Z", [this] { OnUndo(); }),
		MenuAction("Rétablir", "Ctrl+Y", [this] { OnRedo(); }),
		MenuAction("Dupliquer", "Ctrl+D", [this] { OnDuplicate(); }),
		MenuAction("Supprimer", "Suppr", [this] { OnDeleteSelection(); }),
		MenuAction("Renommer…", "F2", [this] { m_tree.BeginRename(Rt().SelectedId()); }),
		MenuAction("Tout désélectionner", "Échap", [this] { Rt().ClearSelection(); }),
		MenuAction("Déplacer", "W", [this] { SetGizmoMode(Runtime::GizmoMode::TRANSLATE); }),
		MenuAction("Tourner", "E", [this] { SetGizmoMode(Runtime::GizmoMode::ROTATE); }),
		MenuAction("Redimensionner", "R", [this] { SetGizmoMode(Runtime::GizmoMode::SCALE); }),
		MenuAction("Magnétisme", "X", [this] { ToggleSnap(); })));

	ecs::Entity tools = m_ctx.factory.Menu(
		barEntity, String("Outils"), MenuAction("Console de script", "", [this] { m_documents.OpenToolConsole(); }),
		MenuAction("Script de la scène", "", [this] { m_documents.OpenSceneScript(ActiveSceneName()); }),
		MenuAction("Vérifier tous les scripts", "", [this] { OnCheckAllScripts(); }),
		MenuAction("Cadrer la sélection", "F", [this] { OnFocusSelection(); }),
		MenuAction("Parcourir les modèles", "", [this] {
			m_assetDock.SetActive(0);
			if (m_assets.Model().Navigate(m_assetsDir + String("/models")))
				m_assets.MarkDirty();
		}));
	m_menuPopups.push_back(tools);
	ecs::Entity create = m_ctx.factory.SubMenu(tools, String("Créer un nœud"));
	m_menuPopups.push_back(create);
	for (const NodeTemplate &entry : NODE_TEMPLATES) {
		const String key(entry.key);
		AddItem(create, entry.label, "", [this, key] {
			const SceneDesc *scene = Rt().ActiveScene();
			(void)m_tree.CreateNode(key, scene ? scene->tree.Root() : scene::NodeId{});
		});
	}

	ecs::Entity window = m_ctx.factory.Menu(
		barEntity, String("Fenêtre"), MenuAction("Arbre de scène", "", [this] { (void)FocusPanel("tree", 0); }),
		MenuAction("Inspecteur", "", [this] { (void)FocusPanel("inspector", 0); }),
		MenuAction("Ressources", "", [this] { (void)FocusPanel("assets", 0); }),
		MenuAction("Console", "", [this] { (void)FocusPanel("console", 0); }),
		MenuAction("Profileur", "", [this] { (void)FocusPanel("profiler", 0); }),
		MenuAction("Vue 3D seule", "", [this] { ToggleMaximized(); }));
	m_menuPopups.push_back(window);
	ecs::Entity themes = m_ctx.factory.SubMenu(window, String("Thème"));
	m_menuPopups.push_back(themes);
	AddItem(themes, "Atelier (par défaut)", "", [this] { (void)SetTheme("studio"); });
	AddItem(themes, "Sombre", "", [this] { (void)SetTheme("dark"); });
	AddItem(themes, "Clair", "", [this] { (void)SetTheme("light"); });
	AddItem(themes, "Verre", "", [this] { (void)SetTheme("aero"); });

	m_menuPopups.push_back(m_ctx.factory.Menu(
		barEntity, String("Compiler"), MenuAction("Lancer dans la vue", "F5", [this] { Rt().TogglePlay(); }),
		MenuAction("Lancer en plein écran", "F6", [this] { EnterRunMode(); }),
		MenuAction("Arrêter", "", [this] { Rt().Stop(); }),
		MenuAction("Vérifier les scripts", "", [this] { OnCheckAllScripts(); }),
		MenuAction("Exporter le rapport", "", [this] { OnExportReport(); })));

	m_menuPopups.push_back(m_ctx.factory.Menu(
		barEntity, String("Aide"), MenuAction("Raccourcis", "", [this] { OnShowShortcuts(); }),
		MenuAction("À propos", "", [this] { OnAbout(); })));
}

void EditorUi::BuildDockMenus() {
	m_treeMenu = m_ctx.factory.ContextMenu();
	m_menuPopups.push_back(m_treeMenu);
	AddItem(m_treeMenu, "Tout déplier", "", [this] { m_tree.ExpandAll(true); });
	AddItem(m_treeMenu, "Tout replier", "", [this] { m_tree.ExpandAll(false); });
	AddItem(m_treeMenu, "Montrer la sélection", "", [this] { m_tree.RevealSelection(); });

	m_documentsMenu = m_ctx.factory.ContextMenu();
	m_menuPopups.push_back(m_documentsMenu);
	AddItem(m_documentsMenu, "Console de script", "", [this] { m_documents.OpenToolConsole(); });
	AddItem(m_documentsMenu, "Script de la scène", "", [this] { m_documents.OpenSceneScript(ActiveSceneName()); });
	AddItem(m_documentsMenu, "Fermer tous les documents", "", [this] {
		while (m_documents.Dock().Count() > 1)
			m_documents.Close(m_documents.Dock().Count() - 1);
	});
	AddItem(m_documentsMenu, "Vue 3D seule", "", [this] { ToggleMaximized(); });

	m_inspectorMenu = m_ctx.factory.ContextMenu();
	m_menuPopups.push_back(m_inspectorMenu);
	AddItem(m_inspectorMenu, "Modifier les propriétés en JSON…", "", [this] {
		if (Rt().SelectedId().Valid())
			m_documents.OpenComponentJson(Rt().SelectedId(), String());
	});
	AddItem(m_inspectorMenu, "Cadrer la sélection", "F", [this] { OnFocusSelection(); });

	m_assetsMenu = m_ctx.factory.ContextMenu();
	m_menuPopups.push_back(m_assetsMenu);
	const std::pair<const char *, const char *> places[] = {
		{"Projet", AssetBrowserModel::ROOT},        {"Scènes", AssetBrowserModel::SCENES},
		{"Scripts", AssetBrowserModel::SCRIPTS},    {"Modèles", "/models"},
		{"Textures", "/textures"},                  {"Sons", "/sounds"},
	};
	for (const auto &[label, location] : places) {
		// Les dossiers du disque sont relatifs à la racine des ressources.
		const String where = location[0] == '/' ? m_assetsDir + String(location) : String(location);
		AddItem(m_assetsMenu, label, "", [this, where] {
			if (m_assets.Model().Navigate(where))
				m_assets.MarkDirty();
		});
	}
}

void EditorUi::BuildViewportPage(ecs::Entity page) {
	const ui::UiTheme &theme = m_ctx.Theme();
	ui::WidgetBuilder bar = m_ctx.factory.Row();
	bar.Gap(2.f).Pad(math::Sides{6.f, 3.f}).GrowW().HAuto().Align(ui::CrossAlign::Center);
	bar.Bg(kit::PaletteOf(m_ctx).toolbar).Parent(page);
	ecs::Entity barEntity = bar.Spawn();

	// Espace de travail : 2D ou 3D (comme les onglets de Godot).
	for (int i = 0; i < 2; ++i) {
		ui::WidgetBuilder button = m_ctx.factory.Button(String(i == 0 ? "2D" : "3D"));
		button.Size(34.f, 24.f).FontSize(12.f).Bold().Radius(4.f);
		button.Tooltip(String(i == 0 ? "Espace de travail 2D : calque d'interface et jeu 2D"
									 : "Espace de travail 3D : le monde"));
		button.OnClick([this, i] { Set2DView(i == 0); }).Parent(barEntity);
		m_workspaceButtons[i] = button.Spawn();
	}
	AddToolbarSeparator(barEntity);

	m_gizmoButtons[0] = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::OPEN_WITH, String("Déplacer (W)"),
										[this] { SetGizmoMode(Runtime::GizmoMode::TRANSLATE); }, 26.f);
	m_gizmoButtons[1] = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::ROTATE_RIGHT, String("Tourner (E)"),
										[this] { SetGizmoMode(Runtime::GizmoMode::ROTATE); }, 26.f);
	m_gizmoButtons[2] = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::ZOOM_OUT_MAP, String("Redimensionner (R)"),
										[this] { SetGizmoMode(Runtime::GizmoMode::SCALE); }, 26.f);
	m_snapButton = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::GRID_ON, String("Magnétisme (X)"),
								   [this] { ToggleSnap(); }, 26.f);
	AddToolbarSeparator(barEntity);
	(void)kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::PLAY_ARROW, String("Lancer / arrêter dans la vue (F5)"),
						  [this] { Rt().TogglePlay(); }, 26.f, &m_playIcon, kit::PaletteOf(m_ctx).ok);
	(void)kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::SPORTS_ESPORTS, String("Lancer en plein écran (F6)"),
						  [this] { EnterRunMode(); }, 26.f);
	(void)kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::CENTER_FOCUS_STRONG, String("Cadrer la sélection (F)"),
						  [this] { OnFocusSelection(); }, 26.f);
	(void)kit::Spacer(m_ctx, barEntity);

	std::vector<String> sceneNames = Rt().GetProject().SceneNames();
	int active = 0;
	for (size_t i = 0; i < sceneNames.size(); ++i)
		if (sceneNames[i] == ActiveSceneName())
			active = int(i);
	ui::WidgetBuilder combo = m_ctx.factory.Combo(sceneNames, active);
	combo.Size(170.f, 24.f).FontSize(13.f).Tooltip(String("Scène active")).Parent(barEntity);
	combo.OnChange([this, sceneNames](float index) {
		const size_t i = size_t(index < 0.f ? 0.f : index);
		if (i < sceneNames.size())
			(void)Rt().SwitchScene(sceneNames[i]);
	});
	m_sceneCombo = combo.Spawn();

	ui::WidgetBuilder searchRow = m_ctx.factory.Row();
	searchRow.Pad(0.f);
	searchRow.Gap(4.f).W(ui::Dimension::Px(200.f)).HAuto().Align(ui::CrossAlign::Center).Parent(barEntity);
	ecs::Entity searchRowEntity = searchRow.Spawn();
	(void)kit::Glyph(m_ctx, searchRowEntity, ui::MaterialIcons::SEARCH, theme.muted, 15.f);
	ui::WidgetBuilder search = m_ctx.factory.Input(String("Aller à un nœud…"));
	search.GrowW().H(ui::Dimension::Px(24.f)).FontSize(13.f).Parent(searchRowEntity);
	search.OnSubmit([this](const String &text) { GoToNode(text); });
	(void)search.Spawn();
	(void)kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::FULLSCREEN, String("Vue 3D seule (masquer les panneaux)"),
						  [this] { ToggleMaximized(); }, 26.f);

	// La vue elle-même, avec les superpositions (axes, projection, badge).
	ui::WidgetBuilder viewport = m_ctx.factory.Viewport3D(Rt().SceneRoot(), Rt().ActiveCamera());
	viewport.GrowW().GrowH().Parent(page);
	// Glisser une vignette de modèle du navigateur dans la vue : import.
	viewport.DropTarget(String("asset"));
	viewport.OnDrop([this](int64_t index) { m_assets.Activate(size_t(index)); });
	m_viewport = viewport.Spawn();
	// Le calque 2D d'abord : tout le reste (repères, badge, interface du
	// jeu) se dessine par-dessus.
	Spawn2DLayer(m_viewport, false);

	Runtime *runtime = &Rt();
	ui::WidgetBuilder axes = m_ctx.factory.Canvas([runtime](sdl3::Renderer &ren, sdl3::FRect r) {
		DrawAxisGizmo(ren, r, runtime->ActiveCamera());
	});
	axes.Absolute().Anchor(ui::Anchor::TopRight).Offset(-10.f, 10.f).Size(92.f, 92.f).PointerThrough();
	axes.Parent(m_viewport);
	m_axesGizmo = axes.Spawn();
	ui::WidgetBuilder projection = m_ctx.factory.Label(String("‹ Persp"));
	projection.Absolute().Anchor(ui::Anchor::TopRight).Offset(-30.f, 104.f).WAuto().HAuto().FontSize(12.f);
	projection.TextColor(sdl3::FColor{1.f, 1.f, 1.f, 0.65f}).PointerThrough().Parent(m_viewport);
	m_projectionLabel = projection.Spawn();
	Show2DWorkspace();

	ui::WidgetBuilder badge = m_ctx.factory.Label(String("▶  MODE JEU — F5 pour arrêter"));
	badge.Absolute().Anchor(ui::Anchor::TopLeft).Offset(12.f, 10.f).WAuto().HAuto().FontSize(13.f).Bold();
	badge.Pad(math::Sides{10.f, 4.f}).Radius(4.f).Bg(sdl3::FColor{0.12f, 0.45f, 0.18f, 0.85f});
	badge.TextColor(sdl3::FColor::WHITE()).PointerThrough().Parent(m_viewport);
	m_playBadge = badge.Spawn();
	kit::SetHidden(m_ctx, m_playBadge, true);
}

void EditorUi::AddToolbarSeparator(ecs::Entity parent) {
	ui::WidgetBuilder separator = m_ctx.factory.Row();
	separator.Pad(0.f);
	separator.Size(1.f, 20.f).Bg(kit::PaletteOf(m_ctx).separator).Margin(math::Sides{4.f, 0.f}).Parent(parent);
	(void)separator.Spawn();
}

void EditorUi::DrawAxisGizmo(sdl3::Renderer &ren, sdl3::FRect r, const render3d::Camera &camera) {
	const math::FVector3 forward = (camera.target - camera.position).Normalize();
	math::FVector3 right = forward.Cross(math::FVector3{0.f, 1.f, 0.f});
	if (right.LengthSq() < 1e-6f)
		right = math::FVector3{1.f, 0.f, 0.f};
	right = right.Normalize();
	const math::FVector3 up = right.Cross(forward).Normalize();
	const sdl3::FPoint center{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
	const float radius = r.w * 0.36f;

	struct Axis {
		sdl3::FColor color;
		char letter;
		bool positive;
		float depth;
		sdl3::FPoint tip;
	};
	std::vector<Axis> axes;
	const math::FVector3 dirs[3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
	const sdl3::FColor colors[3] = {kit::AXIS_X, kit::AXIS_Y, kit::AXIS_Z};
	const char letters[3] = {'X', 'Y', 'Z'};
	for (int i = 0; i < 3; ++i) {
		for (float sign : {1.f, -1.f}) {
			const math::FVector3 d = dirs[i] * sign;
			axes.push_back(Axis{colors[i], letters[i], sign > 0.f, d.Dot(forward),
								sdl3::FPoint{center.x + d.Dot(right) * radius, center.y - d.Dot(up) * radius}});
		}
	}
	std::sort(axes.begin(), axes.end(), [](const Axis &a, const Axis &b) { return a.depth > b.depth; });

	ren.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 0.22f});
	ren.FillCircle(center, r.w * 0.48f);
	for (const Axis &axis : axes) {
		if (axis.positive) {
			ren.SetDrawColor(axis.color);
			for (float offset : {-1.f, 0.f, 1.f}) {
				ren.DrawLine(center.x + offset, center.y, axis.tip.x + offset, axis.tip.y);
				ren.DrawLine(center.x, center.y + offset, axis.tip.x, axis.tip.y + offset);
			}
			ren.FillCircle(axis.tip, 7.5f);
			ren.SetDrawColor(sdl3::FColor{0.07f, 0.07f, 0.08f, 1.f});
			DrawLetter(ren, axis.letter, axis.tip, 3.2f);
		} else {
			ren.SetDrawColor(sdl3::FColor{axis.color.r, axis.color.g, axis.color.b, 0.45f});
			ren.FillCircle(axis.tip, 5.f);
		}
	}
	ren.SetDrawColor(sdl3::FColor{0.85f, 0.85f, 0.88f, 1.f});
	ren.FillCircle(center, 3.5f);
}

void EditorUi::DrawLetter(sdl3::Renderer &ren, char letter, sdl3::FPoint c, float s) {
	auto line = [&](float x0, float y0, float x1, float y1) {
		ren.DrawLine(c.x + x0 * s, c.y + y0 * s, c.x + x1 * s, c.y + y1 * s);
		ren.DrawLine(c.x + x0 * s + 0.6f, c.y + y0 * s, c.x + x1 * s + 0.6f, c.y + y1 * s);
	};
	switch (letter) {
		case 'X':
			line(-1.f, -1.f, 1.f, 1.f);
			line(-1.f, 1.f, 1.f, -1.f);
			break;
		case 'Y':
			line(-1.f, -1.f, 0.f, 0.f);
			line(1.f, -1.f, 0.f, 0.f);
			line(0.f, 0.f, 0.f, 1.f);
			break;
		default:
			line(-1.f, -1.f, 1.f, -1.f);
			line(1.f, -1.f, -1.f, 1.f);
			line(-1.f, 1.f, 1.f, 1.f);
			break;
	}
}

void EditorUi::ToggleMaximized() {
	m_maximized = !m_maximized;
	for (ecs::Entity entity : {m_treeDock.Frame(), m_leftHandle, m_bottomRow, Previous(m_bottomRow), m_rightColumn,
							   Previous(m_rightColumn)})
		kit::SetHidden(m_ctx, entity, m_maximized);
	SetStatus(String(m_maximized ? "Vue 3D seule — même bouton pour revenir" : "Panneaux restaurés"));
}

ecs::Entity EditorUi::Previous(ecs::Entity entity) const {
	auto parent = m_ctx.registry.GetComponent<ui::UiParent>(entity);
	if (parent.IsNone())
		return ecs::Entity{};
	auto children = m_ctx.registry.GetComponent<ui::UiChildren>(parent.Unwrap()->parent);
	if (children.IsNone())
		return ecs::Entity{};
	const std::vector<ecs::Entity> &list = children.Unwrap()->list;
	for (size_t i = 1; i < list.size(); ++i)
		if (list[i] == entity)
			return list[i - 1];
	return ecs::Entity{};
}

void EditorUi::GoToNode(const String &text) {
	const SceneDesc *scene = Rt().ActiveScene();
	const String wanted = text.Trim().ToLower();
	if (!scene || wanted.IsEmpty())
		return;
	scene::NodeId found;
	scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
		if (!found.Valid() && id != scene->tree.Root() && node.name.ToLower().Contains(wanted))
			found = id;
	});
	if (!found.Valid()) {
		SetStatus(String::Format("Aucun nœud ne contient « %s »", text.CStr()));
		return;
	}
	(void)Rt().Select(found);
	(void)Rt().FocusOn(found);
}

float EditorUi::UpdateViewportCamera(ecs::Entity entity) {
	auto viewport = m_ctx.registry.GetComponent<ui::UiViewport3D>(entity);
	if (viewport.IsNone())
		return 0.f;
	viewport.Unwrap()->camera = Rt().ActiveCamera();
	const ui::UiViewport3D &vp = *viewport.Unwrap();
	return vp.textureHeight > 0 ? float(vp.textureWidth) / float(vp.textureHeight) : 0.f;
}

/// Souris et touche de bascule pendant une partie (vue de l'éditeur ou plein
/// écran). Vrai si l'évènement est pour le jeu (l'éditeur ne doit pas s'en
/// servir : souris capturée, touche de bascule).
bool EditorUi::HandleGameMouse(const sdl3::Event &event) {
	if (!Rt().IsPlaying())
		return false;
	Option<SDL_Keycode> toggle = Rt().MouseToggleKey();
	if (toggle.IsSome() && event.IsKeyDown(toggle.Unwrap()) && !TextFieldHasFocus()) {
		const Runtime::MouseMode mode = Rt().ToggleMouseCapture();
		SetStatus(String(mode == Runtime::MouseMode::CAPTURED ? "Souris capturée (vue subjective)"
															  : "Souris libre : l'interface du jeu se clique"));
		return true;
	}
	if (Rt().EffectiveMouseMode() != Runtime::MouseMode::CAPTURED)
		return false;
	if (event.IsMouseMotion()) {
		Rt().AddMouseDelta(float(event.MouseMotion().xrel), float(event.MouseMotion().yrel));
		return true;
	}
	// Capturée : les clics et la molette vont au jeu, pas à la caméra de l'éditeur.
	return event.Type() == SDL_EVENT_MOUSE_BUTTON_DOWN || event.Type() == SDL_EVENT_MOUSE_BUTTON_UP || event.IsMouseWheel();
}

bool EditorUi::HandleRunPanelDrag(const sdl3::Event &event) {
	if (!m_runPanel.Valid())
		return false;
	if (event.IsMouseDown(SDL_BUTTON_LEFT) && !m_ctx.registry.HasComponent<ui::UiHidden>(m_runPanel) &&
		Rt().EffectiveMouseMode() != Runtime::MouseMode::CAPTURED) {
		const sdl3::FPoint at{event.MouseButton().x, event.MouseButton().y};
		const ecs::Entity front = m_ctx.gui.Input().FrontMostAt(m_ctx.registry, m_ctx.gui.Layout(), at);
		if (front.Valid() && ui::IsDescendantOrSelf(m_ctx.registry, front, m_runPanelHeader) &&
			!ui::IsDescendantOrSelf(m_ctx.registry, front, m_runPanelClose)) {
			m_draggingRunPanel = true;
			return true;
		}
		return false;
	}
	if (!m_draggingRunPanel)
		return false;
	if (event.IsMouseUp(SDL_BUTTON_LEFT)) {
		m_draggingRunPanel = false;
		return true;
	}
	if (event.IsMouseMotion()) {
		MoveRunPanel(float(event.MouseMotion().xrel), float(event.MouseMotion().yrel));
		return true;
	}
	return false;
}

void EditorUi::MoveRunPanel(float dx, float dy) {
	auto rect = m_ctx.registry.GetComponent<ui::UiRect>(m_runPanel);
	auto panel = m_ctx.registry.GetComponent<ui::UiComputed>(m_runPanel);
	auto view = m_ctx.registry.GetComponent<ui::UiComputed>(m_runViewport);
	if (rect.IsNone() || panel.IsNone() || view.IsNone())
		return;
	// Ancré en bas à droite : le décalage va de 0 (coin) à −(vue − encart).
	const sdl3::FRect p = panel.Unwrap()->screen, v = view.Unwrap()->screen;
	sdl3::FPoint offset = rect.Unwrap()->offset;
	offset.x = sdl3::Clamp(offset.x + dx, -sdl3::Max(0.f, v.w - p.w), 0.f);
	offset.y = sdl3::Clamp(offset.y + dy, -sdl3::Max(0.f, v.h - p.h), 0.f);
	rect.Unwrap()->offset = offset;
	m_runPanelOffset = offset;
	m_ctx.gui.Layout().MarkDirty();
}

void EditorUi::SetRunPanelHidden(bool hidden) {
	if (!m_runPanel.Valid())
		return;
	m_draggingRunPanel = false;
	kit::SetHidden(m_ctx, m_runPanel, hidden);
	SetStatus(String(hidden ? "Encart des scripts masqué — F9 pour le réafficher" : "Encart des scripts affiché"));
}

void EditorUi::HandleRunModeEvent(const sdl3::Event &event) {
	if (event.IsKeyDown(SDLK_F9))
		SetRunPanelHidden(!m_ctx.registry.HasComponent<ui::UiHidden>(m_runPanel));
	(void)HandleGameMouse(event);
	if (event.IsKeyDown(SDLK_ESCAPE) || event.IsKeyDown(SDLK_F6) || event.IsKeyDown(SDLK_F5))
		ExitRunMode();
}

void EditorUi::RefreshRunPanel() {
	if (!m_runPanelBody.Valid())
		return;
	// Seul le corps est reconstruit (l'en-tête et son ✕ restent en place).
	kit::ClearChildren(m_ctx, m_runPanelBody);
	ui::WidgetBuilder rule = m_ctx.factory.Row();
	rule.Pad(0.f);
	rule.GrowW().H(ui::Dimension::Px(1.f)).Bg(kit::PaletteOf(m_ctx).separator).PointerThrough().Parent(m_runPanelBody);
	(void)rule.Spawn();
	for (const ScriptStatus &status : Rt().ScriptStatuses()) {
		ui::WidgetBuilder row = m_ctx.factory.Row();
		row.Pad(0.f);
		row.Gap(6.f).WAuto().HAuto().Align(ui::CrossAlign::Center).PointerThrough().Parent(m_runPanelBody);
		ecs::Entity rowEntity = row.Spawn();
		const bool failed = !status.error.IsEmpty();
		const sdl3::FColor dot = failed ? kit::PaletteOf(m_ctx).error : status.running ? kit::PaletteOf(m_ctx).ok : kit::PaletteOf(m_ctx).warning;
		ui::WidgetBuilder square = m_ctx.factory.Row();
		square.Pad(0.f);
		square.Size(8.f, 8.f).Bg(dot).PointerThrough().Parent(rowEntity);
		(void)square.Spawn();
		const bool sceneScript = status.role == "scène";
		const char *state = failed ? "Erreur" : !status.running ? "Arrêté" : sceneScript ? "Actif" : "Animé";
		const String text = sceneScript ? String::Format("%s.script (%s)", status.name.CStr(), state)
										: String::Format("%s.script (%s, %d nœud%s)", status.name.CStr(), state,
														 int(status.nodes), status.nodes > 1 ? "s" : "");
		ui::WidgetBuilder label = m_ctx.factory.Label(text);
		label.FontSize(13.f).WAuto().HAuto().TextColor(sdl3::FColor{0.86f, 0.86f, 0.88f, 1.f}).PointerThrough();
		label.Parent(rowEntity);
		(void)label.Spawn();
	}
}

void EditorUi::BuildConsolePage(ecs::Entity page) {
	ui::WidgetBuilder console = m_ctx.factory.InputArea(String("(journal vide)"));
	console.GrowW().GrowH().FontSize(12.f).IoMode(ui::IOMode::READ_AND_COPY_ONLY).Monospace();
	console.Highlighter(kit::syntax::HighlightLog).Parent(page);
	m_console = console.Spawn();
}

void EditorUi::RefreshConsole() {
	auto area = m_ctx.registry.GetComponent<ui::UiInputArea>(m_console);
	if (area.IsNone())
		return;
	// Les dernières lignes seulement : recopier 500 entrées à chaque ligne
	// nouvelle coûterait plus que tout le reste de l'interface.
	const std::vector<LogEntry> &log = Rt().Log();
	const size_t start = log.size() > CONSOLE_LINES ? log.size() - CONSOLE_LINES : 0;
	String text;
	for (size_t i = start; i < log.size(); ++i) {
		text.Concat(String::Format("[%-6s] %s", log[i].LevelName(), log[i].text.CStr()));
		text.Concat("\n");
	}
	area.Unwrap()->text = std::move(text);
}

void EditorUi::AddProfilerRow(const char *label, const String &value) {
	kit::PropertyRows rows(m_ctx, 150.f);
	(void)rows.ReadOnly(m_profilerPage, label, value);
}

void EditorUi::RefreshProfiler() {
	if (!m_profilerPage.Valid())
		return;
	kit::ClearChildren(m_ctx, m_profilerPage);
	const SceneDesc *scene = Rt().ActiveScene();
	AddProfilerRow("Cadence", String::Format("%.1f img/s", m_fps));
	AddProfilerRow("Simulation", String::Format("%.2f ms", Rt().SimulationMs()));
	AddProfilerRow("Objets (scène)", String::From(int(scene ? scene->ObjectCount() : 0)));
	AddProfilerRow("Nœuds 3D", String::From(int(Rt().RuntimeObjectCount())));
	AddProfilerRow("Lumières actives", String::Format("%d ponctuelles, %d projecteurs", int(Rt().PointLights().size()),
													  int(Rt().SpotLights().size())));
	AddProfilerRow("Corps physiques", String::From(int(Rt().RigidBodyCount())));
	AddProfilerRow("Entités ECS", String::From(int(m_ctx.registry.AliveCount())));
	AddProfilerRow("Scripts exécutés", String::From(int(Rt().ScriptRunCount())));
	AddProfilerRow("Rappels de script", String::From(int(Rt().ScriptCallCount())));
	AddProfilerRow("Erreurs de script", String::From(int(Rt().ScriptErrorCount())));
	AddProfilerRow("Déclenchements", String::From(int(Rt().TriggerCount())));
	AddProfilerRow("Fils du vivier", String::Format("%d (pic %d en parallèle)", int(Rt().Jobs().WorkerCount()),
													 Rt().Jobs().PeakConcurrentWorkers()));
	if (m_fpsHistory.size() >= 2) {
		ui::WidgetBuilder plot = m_ctx.factory.PlotLines(m_fpsHistory, String("images / seconde"));
		plot.GrowW().H(ui::Dimension::Px(120.f)).Parent(m_profilerPage);
		(void)plot.Spawn();
	}
}

void EditorUi::RefreshSceneList() {
	if (!m_sceneListPage.Valid())
		return;
	kit::ClearChildren(m_ctx, m_sceneListPage);
	const Project &project = Rt().GetProject();
	int index = 0;
	for (const SceneDesc &scene : project.scenes) {
		const bool active = scene.name == ActiveSceneName();
		ui::WidgetBuilder row = m_ctx.factory.Selectable(String(), index++);
		row.GrowW().HAuto().Gap(6.f).Pad(math::Sides{6.f, 3.f}).Tooltip(scene.description).Parent(m_sceneListPage);
		const String name = scene.name;
		row.OnClick([this, name] { (void)Rt().SwitchScene(name); });
		ecs::Entity rowEntity = row.Spawn();
		if (auto selectable = m_ctx.registry.GetComponent<ui::UiSelectable>(rowEntity); selectable.IsSome())
			selectable.Unwrap()->selected = active;
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::LAYERS, kit::Rgb(126, 172, 232), 16.f);
		ui::WidgetBuilder label = m_ctx.factory.Label(scene.name);
		label.WAuto().HAuto().FontSize(13.f).PointerThrough().Parent(rowEntity);
		(void)label.Spawn();
		ui::WidgetBuilder detail = m_ctx.factory.Label(String::Format("(%d objets)", int(scene.ObjectCount())));
		detail.GrowW().HAuto().TextEllipsis().FontSize(12.f).TextColor(m_ctx.Theme().muted).PointerThrough().Parent(rowEntity);
		(void)detail.Spawn();
	}
}

void EditorUi::BuildStatusBar(ecs::Entity parent) {
	ui::WidgetBuilder bar = m_ctx.factory.Row();
	bar.Gap(16.f).Pad(math::Sides{10.f, 3.f}).GrowW().HAuto().Align(ui::CrossAlign::Center);
	bar.Bg(kit::PaletteOf(m_ctx).header).Parent(parent);
	ecs::Entity barEntity = bar.Spawn();
	m_statusFps = StatusLabel(barEntity, "— img/s", 70.f);
	m_statusObjects = StatusLabel(barEntity, "— objets", 90.f);
	m_statusBodies = StatusLabel(barEntity, "— corps", 80.f);
	m_statusScene = StatusLabel(barEntity, "—", 140.f);
	m_statusMode = StatusLabel(barEntity, "■ Édition", 80.f);
	m_statusGizmo = StatusLabel(barEntity, "déplacer", 170.f);
	ui::WidgetBuilder message = m_ctx.factory.Label(m_statusMessage);
	message.GrowW().HAuto().FontSize(12.f).TextColor(m_ctx.Theme().muted).TextEllipsis().TextAlign(ui::TextAlign::Right);
	message.Parent(barEntity);
	m_statusMessageLabel = message.Spawn();
}

ecs::Entity EditorUi::StatusLabel(ecs::Entity parent, const char *text, float width) {
	ui::WidgetBuilder label = m_ctx.factory.Label(String(text));
	label.Size(width, 0.f).HAuto().FontSize(12.f).TextEllipsis().Parent(parent);
	return label.Spawn();
}

void EditorUi::RefreshStatusBar() {
	const SceneDesc *scene = Rt().ActiveScene();
	kit::SetLabelText(m_ctx, m_statusFps, String::Format("%.0f img/s", m_fps));
	kit::SetLabelText(m_ctx, m_statusObjects, String::Format("%d objets", int(scene ? scene->ObjectCount() : 0)));
	kit::SetLabelText(m_ctx, m_statusBodies, String::Format("%d corps", int(Rt().RigidBodyCount())));
	kit::SetLabelText(m_ctx, m_statusScene,
					  scene ? String::Format("%s › %s", Rt().GetProject().name.CStr(), scene->name.CStr())
							: String("(aucune scène)"));
	kit::SetLabelText(m_ctx, m_statusMode, String(Rt().IsPlaying() ? "▶ Jeu" : "■ Édition"));
	kit::SetLabelText(m_ctx, m_statusGizmo, String::Format("%s%s", GizmoModeName(Rt().GetGizmoMode()),
														  Rt().SnapEnabled() ? " · magnétisme" : ""));

	// Outil actif et magnétisme surlignés dans la barre d'outils de la vue —
	// seulement quand ils CHANGENT (re-styler à chaque image relancerait la
	// cascade de styles pour rien).
	if (m_view2d != m_shownView2d) {
		m_shownView2d = m_view2d;
		for (int i = 0; i < 2; ++i)
			kit::SetBackground(m_ctx, m_workspaceButtons[i],
							   (i == 0) == m_view2d ? kit::PaletteOf(m_ctx).selection : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	}
	const int mode = int(Rt().GetGizmoMode());
	const bool snap = Rt().SnapEnabled();
	if (mode != m_shownGizmoMode || snap != m_shownSnap) {
		m_shownGizmoMode = mode;
		m_shownSnap = snap;
		for (int i = 0; i < 3; ++i)
			kit::SetBackground(m_ctx, m_gizmoButtons[i], i == mode ? kit::PaletteOf(m_ctx).selection : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
		kit::SetBackground(m_ctx, m_snapButton, snap ? kit::PaletteOf(m_ctx).selection : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	}
	if (Rt().IsPlaying() != m_shownPlaying) {
		m_shownPlaying = Rt().IsPlaying();
		kit::SetHidden(m_ctx, m_playBadge, !m_shownPlaying);
		if (auto glyph = m_ctx.registry.GetComponent<ui::UiIcon>(m_playIcon); glyph.IsSome())
			glyph.Unwrap()->glyph = ui::Glyphs::ToUtf8(m_shownPlaying ? ui::MaterialIcons::STOP : ui::MaterialIcons::PLAY_ARROW);
	}
	// Le sélecteur de scène suit les changements venus d'ailleurs.
	if (auto combo = m_ctx.registry.GetComponent<ui::UiComboBox>(m_sceneCombo); combo.IsSome() && scene) {
		const std::vector<String> &items = combo.Unwrap()->items;
		for (size_t i = 0; i < items.size(); ++i)
			if (items[i] == scene->name)
				combo.Unwrap()->selected = int(i);
	}
	m_fpsHistory.push_back(float(m_fps));
	if (m_fpsHistory.size() > FPS_HISTORY_SIZE)
		m_fpsHistory.erase(m_fpsHistory.begin());
}

void EditorUi::SetGizmoMode(Runtime::GizmoMode mode) {
	Rt().SetGizmoMode(mode);
	SetStatus(String::Format("Manipulateur : %s", GizmoModeName(mode)));
}

void EditorUi::ToggleSnap() {
	Rt().SetSnapEnabled(!Rt().SnapEnabled());
	SetStatus(Rt().SnapEnabled() ? String::Format("Magnétisme : %.2f u / %.0f° / %.2f", Rt().TranslateSnap(),
												  Rt().RotateSnapDegrees(), Rt().ScaleSnap())
								 : String("Magnétisme désactivé"));
}

const char * EditorUi::GizmoModeName(Runtime::GizmoMode mode) noexcept {
	switch (mode) {
		case Runtime::GizmoMode::TRANSLATE:
			return "déplacer";
		case Runtime::GizmoMode::ROTATE:
			return "tourner";
		case Runtime::GizmoMode::SCALE:
			return "redimensionner";
	}
	return "?";
}

void EditorUi::ImportModel(const String &path) {
	auto imported = Rt().ImportGltf(path);
	if (imported.IsError()) {
		Rt().LogError(String::Format("Import : %s", imported.Error().CStr()));
		return;
	}
	(void)Rt().Select(imported.Value());
	SetStatus(String::Format("Modèle importé : %s", imported.Value().CStr()));
}

void EditorUi::OnNewProject() {
	OpenDialog(DialogKind::NEW_PROJECT, String("Nouveau projet"),
			   String::Format("Un dossier du nom du projet est créé dans %s, avec son fichier .json et les "
							  "sous-dossiers assets, scenes et scripts.",
							  Rt().projectsRoot.CStr()),
			   String(), String("Nom du projet"), String("Créer"));
}

void EditorUi::OnOpenProject() {
	OpenDialog(DialogKind::OPEN_PROJECT, String("Ouvrir un projet"),
			   String("Choisissez un projet, ou indiquez le chemin de son dossier ou de son fichier .json."),
			   String(), String("Dossier ou fichier .json du projet"), String("Ouvrir"));
}

void EditorUi::OnSaveProject() {
	if (!Rt().HasProject())
		return;
	if (Rt().ProjectPath().IsEmpty()) {
		OnSaveProjectAs();
		return;
	}
	auto saved = Rt().SaveProject();
	if (saved.IsError())
		Rt().LogError(String::Format("Enregistrement impossible : %s", saved.Error().CStr()));
	else
		Rt().LogSuccess(String::Format("Projet enregistré : %s", Rt().ProjectPath().CStr()));
}

void EditorUi::OnSaveProjectAs() {
	if (!Rt().HasProject())
		return;
	OpenDialog(DialogKind::SAVE_PROJECT_AS, String("Enregistrer le projet sous"),
			   String::Format("Le projet est copié dans un nouveau dossier de %s.", Rt().projectsRoot.CStr()),
			   Rt().GetProject().name, String("Nom du nouveau projet"), String("Enregistrer"));
}

void EditorUi::OnCloseProject() {
	if (!Rt().HasProject())
		return;
	Rt().CloseProject();
	SetStatus(String("Projet fermé"));
	RequestRebuild();
}

void EditorUi::OnNewScene() {
	if (!Rt().HasProject())
		return;
	const String name = Rt().AddEmptyScene();
	Rt().LogSuccess(String::Format("Scène « %s » ajoutée (enregistrez le projet pour l'écrire)", name.CStr()));
	RequestRebuild();
}

String EditorUi::SuggestedPath(const char *sub, const String &fileName) const {
	const String directory = Rt().ProjectDirectory();
	return files::Join(files::Join(directory.IsEmpty() ? m_savesDir : directory, String(sub)), fileName);
}

void EditorUi::OnSaveSceneAs() {
	const SceneDesc *scene = Rt().ActiveScene();
	if (!scene)
		return;
	OpenDialog(DialogKind::SAVE_SCENE_AS, String("Enregistrer la scène sous"),
			   String("La scène active, avec son ambiance, ses caméras et son script de jeu, dans un seul "
					  "fichier .scene."),
			   SuggestedPath(files::SCENES_DIR, files::SafeFileName(scene->name) + String(".scene")),
			   String("Chemin du fichier .scene"), String("Enregistrer"));
}

void EditorUi::OnImportScene() {
	OpenDialog(DialogKind::IMPORT_SCENE, String("Importer une scène"),
			   String("Ajoute au projet la scène d'un fichier .scene et l'active."),
			   SuggestedPath(files::SCENES_DIR, String()), String("Chemin du fichier .scene"),
			   String("Importer"));
}

void EditorUi::OnImportScript() {
	OpenDialog(DialogKind::IMPORT_SCRIPT, String("Importer un script"),
			   String("Ajoute à la bibliothèque le script d'un fichier .script (son nom est celui du fichier)."),
			   SuggestedPath(files::SCRIPTS_DIR, String()), String("Chemin du fichier .script"),
			   String("Importer"));
}

void EditorUi::OnSaveScriptAs(const String &key) {
	const String base = key.StartsWith("@") ? files::SafeFileName(key.Substr(1)) + String(".gameplay")
											: files::SafeFileName(key);
	m_dialogKey = key;
	OpenDialog(DialogKind::SAVE_SCRIPT_AS, String("Enregistrer le script sous"),
			   String("Écrit le script dans un fichier .script."),
			   SuggestedPath(files::SCRIPTS_DIR, base + String(".script")), String("Chemin du fichier .script"),
			   String("Enregistrer"));
}

void EditorUi::EnsureParentDirectory(const String &path) {
	(void)sdl3::filesystem::CreateDirectory(files::DirectoryOf(path));
}

void EditorUi::OnSaveSelectionAsScene() {
	const scene::Node *node = Rt().SelectedObject();
	if (!node) {
		SetStatus(String("Sélectionnez le nœud à enregistrer comme scène"));
		return;
	}
	const String path = SuggestedPath(files::ASSETS_DIR, files::SafeFileName(node->name) + String(".scene"));
	auto saved = Rt().SavePackedScene(node->id, path);
	if (saved.IsError())
		Rt().LogError(saved.Error());
	else
		Rt().LogSuccess(String::Format("Scène enregistrée : %s", path.CStr()));
}

void EditorUi::BuildStartPage() {
	const ui::UiTheme &theme = m_ctx.Theme();
	ui::WidgetBuilder root = m_ctx.factory.Column();
	root.Gap(0.f).Pad(0.f).Fixed().Anchor(ui::Anchor::TopLeft).Bg(kit::PaletteOf(m_ctx).base);
	root.W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Rpct(100.f));
	m_root = root.Spawn();

	ui::WidgetBuilder bar = m_ctx.factory.MenuBar();
	bar.Parent(m_root).GrowW().HAuto().Pad(math::Sides{6.f, 2.f}).Bg(kit::PaletteOf(m_ctx).header);
	ecs::Entity barEntity = bar.Spawn();
	m_menuPopups.push_back(m_ctx.factory.Menu(
		barEntity, String("Fichier"), MenuAction("Nouveau projet…", "Ctrl+N", [this] { OnNewProject(); }),
		MenuAction("Ouvrir un projet…", "Ctrl+O", [this] { OnOpenProject(); }),
		MenuAction("Quitter", "Ctrl+Q", [this] {
			if (onQuit)
				onQuit();
		})));
	ecs::Entity window = m_ctx.factory.Menu(barEntity, String("Fenêtre"));
	m_menuPopups.push_back(window);
	ecs::Entity themes = m_ctx.factory.SubMenu(window, String("Thème"));
	m_menuPopups.push_back(themes);
	AddItem(themes, "Atelier (par défaut)", "", [this] { (void)SetTheme("studio"); });
	AddItem(themes, "Sombre", "", [this] { (void)SetTheme("dark"); });
	AddItem(themes, "Clair", "", [this] { (void)SetTheme("light"); });
	AddItem(themes, "Verre", "", [this] { (void)SetTheme("aero"); });

	ui::WidgetBuilder center = m_ctx.factory.Column();
	center.GrowW().GrowH().Pad(24.f).Justify(ui::Justify::Center).Align(ui::CrossAlign::Center).Parent(m_root);
	ecs::Entity centerEntity = center.Spawn();

	ui::WidgetBuilder card = m_ctx.factory.Panel();
	card.W(ui::Dimension::Px(640.f)).HAuto().Pad(24.f).Gap(12.f).Parent(centerEntity);
	ecs::Entity cardEntity = card.Spawn();
	(void)kit::Glyph(m_ctx, cardEntity, ui::MaterialIcons::FOLDER_OPEN, theme.muted, 44.f);
	ui::WidgetBuilder title = m_ctx.factory.Label(String("Aucun projet ouvert"));
	title.FontSize(22.f).WAuto().HAuto().Parent(cardEntity);
	(void)title.Spawn();
	ui::WidgetBuilder hint = m_ctx.factory.Label(String::Format(
		"Créez un projet vierge ou ouvrez un projet existant. Chaque projet est un dossier de %s : "
		"son fichier .json, et les sous-dossiers assets, scenes (.scene) et scripts (.script).",
		Rt().projectsRoot.CStr()));
	hint.FontSize(13.f).TextColor(theme.muted).GrowW().HAuto().TextWrap().Parent(cardEntity);
	(void)hint.Spawn();

	ui::WidgetBuilder actions = m_ctx.factory.Row();
	actions.Gap(8.f).Pad(0.f).GrowW().HAuto().Parent(cardEntity);
	ecs::Entity actionsEntity = actions.Spawn();
	for (auto [label, action] : {std::pair<const char *, void (EditorUi::*)()>{"Nouveau projet…", &EditorUi::OnNewProject},
								 std::pair<const char *, void (EditorUi::*)()>{"Ouvrir un projet…", &EditorUi::OnOpenProject}}) {
		ui::WidgetBuilder button = m_ctx.factory.Button(String(label));
		button.WAuto().H(ui::Dimension::Px(32.f)).Parent(actionsEntity);
		button.OnClick([this, action] { (this->*action)(); });
		(void)button.Spawn();
	}

	const std::vector<files::ProjectEntry> projects = files::ListProjects(Rt().projectsRoot);
	ui::WidgetBuilder listTitle = m_ctx.factory.Label(
		projects.empty() ? String("Aucun projet dans ce dossier pour l'instant.") : String("Projets existants"));
	listTitle.FontSize(14.f).WAuto().HAuto().Parent(cardEntity);
	(void)listTitle.Spawn();
	for (const files::ProjectEntry &project : projects)
		SpawnProjectButton(cardEntity, project);

	ui::WidgetBuilder status = m_ctx.factory.Label(String());
	status.FontSize(12.f).TextColor(theme.muted).GrowW().HAuto().TextWrap().Parent(cardEntity);
	m_statusMessageLabel = status.Spawn();
}

void EditorUi::SpawnProjectButton(ecs::Entity parent, const files::ProjectEntry &project) {
	ui::WidgetBuilder row = m_ctx.factory.Button(String::Format("%s    —    %s", project.name.CStr(),
																files::RelativeTo(Rt().projectsRoot, project.manifest).CStr()));
	row.GrowW().H(ui::Dimension::Px(30.f)).FontSize(13.f).Tooltip(project.manifest).Parent(parent);
	const String manifest = project.manifest;
	row.OnClick([this, manifest] { OpenProjectAt(manifest); });
	(void)row.Spawn();
}

bool EditorUi::OpenProjectAt(const String &path) {
	String target = path.Trim();
	if (!target.IsEmpty() && sdl3::filesystem::PathInfo(target).IsNone())
		target = files::Join(Rt().projectsRoot, target); // nom d'un dossier du dossier des projets
	auto loaded = Rt().LoadProjectFile(target);
	if (loaded.IsError()) {
		Rt().LogError(String::Format("Ouverture impossible : %s", loaded.Error().CStr()));
		SetStatus(String::Format("Ouverture impossible : %s", loaded.Error().CStr()));
		ShowDialogError(loaded.Error());
		return false;
	}
	CloseDialog();
	RequestRebuild();
	return true;
}

void EditorUi::OpenDialog(DialogKind kind, const String &title, const String &message, const String &text,
		const String &placeholder, const String &confirm) {
	CloseDialog();
	m_dialogKind = kind;
	const ui::UiTheme &theme = m_ctx.Theme();

	ui::WidgetBuilder card = m_ctx.factory.Panel();
	card.W(ui::Dimension::Px(600.f)).HAuto().Pad(18.f).Gap(10.f);
	ui::WidgetBuilder modal = m_ctx.factory.Modal(std::move(card));
	m_dialog = modal.Spawn();
	ecs::Entity cardEntity{};
	if (auto kids = m_ctx.registry.GetComponent<ui::UiChildren>(m_dialog); kids.IsSome() && kids.Unwrap()->list.size() > 1)
		cardEntity = kids.Unwrap()->list[1]; // [0] = voile, [1] = le contenu (cf. UiFactory::Modal)

	ui::WidgetBuilder heading = m_ctx.factory.Label(title);
	heading.FontSize(17.f).WAuto().HAuto().Parent(cardEntity);
	(void)heading.Spawn();
	ui::WidgetBuilder note = m_ctx.factory.Label(message);
	note.FontSize(13.f).TextColor(theme.muted).GrowW().HAuto().TextWrap().Parent(cardEntity);
	(void)note.Spawn();

	if (kind == DialogKind::OPEN_PROJECT)
		for (const files::ProjectEntry &project : files::ListProjects(Rt().projectsRoot))
			SpawnProjectButton(cardEntity, project);

	ui::WidgetBuilder input = m_ctx.factory.Input(placeholder);
	input.GrowW().H(ui::Dimension::Px(30.f)).Parent(cardEntity);
	input.OnSubmit([this](const String &) { ConfirmDialog(); });
	m_dialogInput = input.Spawn();
	SetDialogText(text);

	ui::WidgetBuilder error = m_ctx.factory.Label(String());
	error.FontSize(12.f).TextColor(kit::PaletteOf(m_ctx).error).GrowW().HAuto().TextWrap().Parent(cardEntity);
	m_dialogError = error.Spawn();

	ui::WidgetBuilder buttons = m_ctx.factory.Row();
	buttons.Gap(8.f).Pad(0.f).GrowW().HAuto().Parent(cardEntity);
	ecs::Entity buttonsEntity = buttons.Spawn();
	if (kind != DialogKind::NEW_PROJECT && kind != DialogKind::SAVE_PROJECT_AS) {
		ui::WidgetBuilder browse = m_ctx.factory.Button(String("Parcourir…"));
		browse.WAuto().H(ui::Dimension::Px(30.f)).Parent(buttonsEntity).OnClick([this] { BrowseForDialog(); });
		(void)browse.Spawn();
	}
	(void)kit::Spacer(m_ctx, buttonsEntity);
	ui::WidgetBuilder cancel = m_ctx.factory.Button(String("Annuler"));
	cancel.WAuto().H(ui::Dimension::Px(30.f)).Parent(buttonsEntity).OnClick([this] { CloseDialog(); });
	(void)cancel.Spawn();
	ui::WidgetBuilder ok = m_ctx.factory.Button(confirm);
	ok.WAuto().H(ui::Dimension::Px(30.f)).Parent(buttonsEntity).OnClick([this] { ConfirmDialog(); });
	(void)ok.Spawn();

	m_ctx.gui.OpenModal(m_dialog);
}

void EditorUi::CloseDialog() {
	if (!m_dialog.Valid())
		return;
	m_ctx.gui.CloseModal(m_dialog);
	ui::DespawnTree(m_ctx.registry, m_dialog);
	m_dialog = m_dialogInput = m_dialogError = ecs::Entity{};
}

void EditorUi::SetDialogText(const String &text) {
	if (auto field = m_ctx.registry.GetComponent<ui::UiInput>(m_dialogInput); field.IsSome()) {
		field.Unwrap()->text = text;
		field.Unwrap()->cursor = field.Unwrap()->selectionAnchor = text.GetSize();
	}
}

String EditorUi::DialogText() const {
	auto field = m_ctx.registry.GetComponent<ui::UiInput>(m_dialogInput);
	return field.IsSome() ? field.Unwrap()->text.Trim() : String();
}

void EditorUi::ShowDialogError(const String &message) {
	if (m_dialogError.Valid())
		kit::SetLabelText(m_ctx, m_dialogError, message);
}

String EditorUi::WithExtension(const String &path, const char *extension) {
	return path.ToLower().EndsWith(extension) ? path : path + String(extension);
}

void EditorUi::ConfirmDialog() {
	if (!m_dialog.Valid())
		return;
	const String text = DialogText();
	if (text.IsEmpty()) {
		ShowDialogError(String("Champ vide"));
		return;
	}
	switch (m_dialogKind) {
		case DialogKind::NEW_PROJECT: {
			auto created = Rt().CreateProject(Rt().projectsRoot, text);
			if (created.IsError()) {
				ShowDialogError(created.Error());
				return;
			}
			CloseDialog();
			RequestRebuild();
			return;
		}
		case DialogKind::OPEN_PROJECT:
			(void)OpenProjectAt(text);
			return;
		case DialogKind::SAVE_PROJECT_AS: {
			auto manifest = files::CreateProjectDirectory(Rt().projectsRoot, text);
			if (manifest.IsError()) {
				ShowDialogError(manifest.Error());
				return;
			}
			Rt().GetProject().name = text;
			auto saved = Rt().SaveProjectFile(manifest.Value());
			if (saved.IsError()) {
				ShowDialogError(saved.Error());
				return;
			}
			Rt().LogSuccess(String::Format("Projet enregistré : %s", manifest.Value().CStr()));
			CloseDialog();
			RequestRebuild(); // le navigateur de ressources suit le nouveau dossier
			return;
		}
		case DialogKind::SAVE_SCENE_AS: {
			const String path = WithExtension(text, ".scene");
			auto saved = Rt().SaveSceneAs(path);
			if (saved.IsError()) {
				ShowDialogError(saved.Error());
				return;
			}
			Rt().LogSuccess(String::Format("Scène enregistrée : %s", path.CStr()));
			CloseDialog();
			return;
		}
		case DialogKind::IMPORT_SCENE: {
			auto imported = Rt().ImportSceneFile(text);
			if (imported.IsError()) {
				ShowDialogError(imported.Error());
				return;
			}
			Rt().LogSuccess(String::Format("Scène « %s » importée", imported.Value().CStr()));
			CloseDialog();
			RequestRebuild();
			return;
		}
		case DialogKind::IMPORT_SCRIPT: {
			auto imported = Rt().ImportScriptFile(text);
			if (imported.IsError()) {
				ShowDialogError(imported.Error());
				return;
			}
			Rt().LogSuccess(String::Format("Script « %s » importé", imported.Value().CStr()));
			m_library.RefreshScripts();
			CloseDialog();
			return;
		}
		case DialogKind::SAVE_SCRIPT_AS: {
			const String path = WithExtension(text, ".script");
			auto saved = Rt().SaveScriptAs(m_dialogKey, path);
			if (saved.IsError()) {
				ShowDialogError(saved.Error());
				return;
			}
			Rt().LogSuccess(String::Format("Script enregistré : %s", path.CStr()));
			CloseDialog();
			return;
		}
	}
}

void EditorUi::BrowseForDialog() {
	auto deliver = [this](const sdl3::DialogResult &result, int) {
		if (!result.Ok())
			return;
		std::lock_guard<std::mutex> lock(m_browseMutex);
		m_browseResult = Some(result.First());
	};
	const String current = DialogText();
	const String start = current.IsEmpty() ? Rt().projectsRoot : files::DirectoryOf(current);
	switch (m_dialogKind) {
		case DialogKind::OPEN_PROJECT:
			sdl3::dialog::ShowOpenFolder(deliver, Rt().projectsRoot);
			break;
		case DialogKind::SAVE_SCENE_AS:
			sdl3::dialog::ShowSaveFile(deliver, {sdl3::DialogFilter{"Scènes", "tscene"}}, current);
			break;
		case DialogKind::IMPORT_SCENE:
			sdl3::dialog::ShowOpenFile(deliver, {sdl3::DialogFilter{"Scènes", "tscene"}}, start);
			break;
		case DialogKind::IMPORT_SCRIPT:
			sdl3::dialog::ShowOpenFile(deliver, {sdl3::DialogFilter{"Scripts Sled", "sled"}}, start);
			break;
		case DialogKind::SAVE_SCRIPT_AS:
			sdl3::dialog::ShowSaveFile(deliver, {sdl3::DialogFilter{"Scripts Sled", "sled"}}, current);
			break;
		case DialogKind::NEW_PROJECT:
		case DialogKind::SAVE_PROJECT_AS:
			break;
	}
}

void EditorUi::ApplyBrowseResult() {
	Option<String> chosen = NONE;
	{
		std::lock_guard<std::mutex> lock(m_browseMutex);
		std::swap(chosen, m_browseResult);
	}
	if (chosen.IsSome())
		SetDialogText(chosen.Unwrap());
}

void EditorUi::OnExportReport() {
	if (onExportReport)
		onExportReport();
	else
		Rt().LogWarning(String("Export du rapport indisponible dans ce mode"));
}

void EditorUi::OnUndo() {
	if (!Rt().Undo())
		SetStatus(String("Rien à annuler"));
}

void EditorUi::OnRedo() {
	if (!Rt().Redo())
		SetStatus(String("Rien à rétablir"));
}

void EditorUi::OnDuplicate() {
	SceneDesc *scene = Rt().ActiveScene();
	scene::Node *object = Rt().SelectedObject();
	if (!scene || !object)
		return;
	const float offset = VisualDesc::Has(*object) ? VisualDesc::Read(*object).dimensions.x + 0.5f : 1.f;
	Option<scene::NodeId> copy = Rt().DuplicateNode(object->id);
	if (copy.IsNone())
		return;
	math::FVector3 position = scene->tree.Get(copy.Unwrap())->transform.position;
	position.x += offset;
	(void)Rt().SetPosition(copy.Unwrap(), position);
}

void EditorUi::OnDeleteSelection() {
	const scene::NodeId id = Rt().SelectedId();
	const scene::Node *node = Rt().FindObject(id);
	if (!node)
		return;
	const String name = node->name;
	if (Rt().RemoveNode(id))
		SetStatus(String::Format("Supprimé : %s", name.CStr()));
}

void EditorUi::OnFocusSelection() {
	if (!Rt().SelectedId().Valid())
		return;
	if (m_view2d)
		(void)Focus2D(Rt().SelectedId());
	else
		(void)Rt().FocusOn(Rt().SelectedId());
}

void EditorUi::OnClearConsole() {
	Rt().ClearLog();
	RefreshConsole();
}

void EditorUi::OnCheckAllScripts() {
	int ok = 0, broken = 0;
	const Project &project = Rt().GetProject();
	auto check = [&](const String &label, const String &source) {
		if (Option<data::script::ScriptError> error = Runtime::CheckScript(source); error.IsSome()) {
			++broken;
			Rt().LogError(String::Format("%s : %s", label.CStr(), error.Unwrap().Format().CStr()));
		} else {
			++ok;
		}
	};
	for (const ScriptAsset &script : project.scripts)
		check(script.name + String(".script"), script.source);
	for (const SceneDesc &scene : project.scenes)
		if (!scene.gameplayScript.IsEmpty())
			check(scene.name + String(".main.script"), scene.gameplayScript);
	if (broken == 0)
		Rt().LogSuccess(String::Format("%d script(s) vérifié(s), aucune erreur", ok));
	else
		Rt().LogWarning(String::Format("%d script(s) en erreur sur %d", broken, ok + broken));
	(void)FocusPanel("console", 0);
}

void EditorUi::OnShowShortcuts() {
	Rt().LogInfo(String("Vue : clic droit orienter + ZQSD/WASD voler · clic milieu panoramique · molette "
						"avancer · Alt+clic orbiter · F cadrer"));
	Rt().LogInfo(String("Édition : W déplacer · E tourner · R redimensionner · X magnétisme · F2 renommer · "
						"Suppr supprimer · Ctrl+D dupliquer · Ctrl+Z / Ctrl+Y"));
	Rt().LogInfo(String("Arbre : clic droit pour le menu contextuel · glisser une ligne pour reparenter"));
	Rt().LogInfo(String("Code : Ctrl+S enregistrer · Ctrl+Entrée exécuter la console · F5 jouer · F6 plein écran"));
	(void)FocusPanel("console", 0);
}

void EditorUi::OnAbout() {
	Rt().LogInfo(String("game_editor_demo — éditeur de jeu 3D bâti sur le wrapper SDL3/C++23 "
						"(ui::, scene::, ecs::, render3d::, physics::, data::script)"));
	(void)FocusPanel("console", 0);
}

bool EditorUi::TextFieldHasFocus() const {
	bool focused = false;
	m_ctx.registry.Query<ui::UiInputArea>([&focused](ecs::Entity, const ui::UiInputArea &area) {
		if (area.focused && area.ioMode != ui::IOMode::READ_AND_COPY_ONLY)
			focused = true;
	});
	if (focused)
		return true;
	m_ctx.registry.Query<ui::UiInput>([&focused](ecs::Entity, const ui::UiInput &input) {
		if (input.focused)
			focused = true;
	});
	return focused;
}

void EditorUi::PollCameraKeys(float dt) {
	if (TextFieldHasFocus() || m_runMode || Is2DEditing())
		return;
	const float speed = sdl3::keyboard::IsPressed(SDLK_LSHIFT) ? 22.f : 8.f;
	math::FVector3 delta{};
	if (m_rightMouseDown) {
		if (sdl3::keyboard::IsPressed(SDLK_W))
			delta.z += 1.f;
		if (sdl3::keyboard::IsPressed(SDLK_S))
			delta.z -= 1.f;
		if (sdl3::keyboard::IsPressed(SDLK_D))
			delta.x += 1.f;
		if (sdl3::keyboard::IsPressed(SDLK_A))
			delta.x -= 1.f;
		if (sdl3::keyboard::IsPressed(SDLK_E))
			delta.y += 1.f;
		if (sdl3::keyboard::IsPressed(SDLK_Q))
			delta.y -= 1.f;
	}
	if (sdl3::keyboard::IsPressed(SDLK_UP))
		delta.z += 1.f;
	if (sdl3::keyboard::IsPressed(SDLK_DOWN))
		delta.z -= 1.f;
	if (sdl3::keyboard::IsPressed(SDLK_RIGHT))
		delta.x += 1.f;
	if (sdl3::keyboard::IsPressed(SDLK_LEFT))
		delta.x -= 1.f;
	if (sdl3::keyboard::IsPressed(SDLK_PAGEUP))
		delta.y += 1.f;
	if (sdl3::keyboard::IsPressed(SDLK_PAGEDOWN))
		delta.y -= 1.f;
	if (delta.x != 0.f || delta.y != 0.f || delta.z != 0.f)
		Rt().MoveCamera(delta * (speed * dt));
}

} // namespace game_editor
