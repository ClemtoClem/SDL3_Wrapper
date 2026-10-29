// Définitions de panels.hpp
#include <sandbox/editor/panels.hpp>

namespace sandbox {

// ── EditorUi ─────────────────────────────────────────────────────────────────

EditorUi::EditorUi(Runtime &runtime, ui::Ui &gui)
	: m_ctx{runtime, gui, gui.World()}, m_tree(m_ctx),
	  m_inspector(m_ctx, MakeInspectorActions()), m_library(m_ctx, MakeInspectorActions()),
	  m_assets(m_ctx, MakeAssetActions()), m_documents(m_ctx), m_treeDock(m_ctx), m_inspectorDock(m_ctx),
	  m_libraryDock(m_ctx), m_assetDock(m_ctx), m_consoleDock(m_ctx) {
	m_ctx.modeling = &m_modeling;
	Rt().onSceneStructureChanged = [this] {
		m_tree.MarkDirty();
		m_inspector.MarkDirty();
		m_scenesDirty = true;
		m_documentChoicesDirty = true;
	};
	Rt().onSelectionChanged = [this] {
		m_inspector.MarkDirty();
		m_tree.RevealSelection();
		FollowSelectionWorkspace();
	};
	Rt().onNodeChanged = [this] {
		m_inspector.OnNodeChanged();
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
	gameUi->root = [this] { return m_gameOverlay.Entity(); };
	Rt().onGameUiReset = [this] { ResetGameOverlay(); };
}

EditorUi::~EditorUi() {
	// Le runtime survit à l'interface : on débranche ce qui la cite.
	const auto &gameUi = Rt().GameUi();
	gameUi->root = nullptr;
	gameUi->registry = nullptr;
	if (m_editorSheet)
		m_ctx.gui.RemoveStyleSheet(m_editorSheet);
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

	m_root = m_ctx.gui.Mount(ui::Column().Id("editor").Class("editor-base").Gap(0.f).Padding(0.f));

	BuildWindowTitleBar();
	BuildMenuBar(m_root);

	ui::WidgetBuilder body = ui::Row();
	body.Gap(0.f).Padding(math::Sides{4.f, 2.f, 4.f, 2.f}).GrowWidth().GrowHeight();
	m_body = m_root.Append(body);

	// Colonne principale : (arbre | documents) au-dessus de (ressources | console).
	ui::WidgetBuilder main = ui::Column();
	main.Padding(0.f);
	main.Gap(0.f).GrowWidth().GrowHeight();
	ui::Widget mainEntity = m_body.Append(main);

	ui::WidgetBuilder top = ui::Row();
	top.Padding(0.f);
	top.Gap(0.f).GrowWidth().GrowHeight();
	ui::Widget topEntity = mainEntity.Append(top);

	m_treeDock.Build(topEntity, [](ui::WidgetBuilder &b) { b.Width(LEFT_WIDTH).GrowHeight(); });
	m_leftHandle = ResizeHandle(topEntity, ui::Orientation::Horizontal, m_treeDock.Frame(), true, 160.f, 700.f);
	m_documents.Build(topEntity, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });

	ui::WidgetBuilder bottom = ui::Row();
	bottom.Padding(0.f);
	bottom.Gap(0.f).GrowWidth().Height(BOTTOM_HEIGHT);
	m_bottomRow = mainEntity.Append(bottom);
	(void)ResizeHandle(mainEntity, ui::Orientation::Vertical, m_bottomRow, false, 90.f, 700.f);

	m_assetDock.Build(m_bottomRow, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });
	ui::WidgetBuilder consoleSlot = ui::Column();
	consoleSlot.Padding(0.f);
	consoleSlot.Gap(0.f).Width(CONSOLE_WIDTH).GrowHeight();
	ui::Widget consoleSlotEntity = m_bottomRow.Append(consoleSlot);
	(void)ResizeHandle(m_bottomRow, ui::Orientation::Horizontal, consoleSlotEntity, false, 200.f, 900.f);
	m_consoleDock.Build(consoleSlotEntity, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });

	// Colonne de droite : inspecteur au-dessus de la bibliothèque.
	ui::WidgetBuilder right = ui::Column();
	right.Padding(0.f);
	right.Gap(0.f).Width(RIGHT_WIDTH).GrowHeight();
	m_rightColumn = m_body.Append(right);
	(void)ResizeHandle(m_body, ui::Orientation::Horizontal, m_rightColumn, false, 240.f, 700.f);
	m_inspectorDock.Build(m_rightColumn, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });
	ui::WidgetBuilder librarySlot = ui::Column();
	librarySlot.Padding(0.f);
	librarySlot.Gap(0.f).GrowWidth().Height(LIBRARY_HEIGHT);
	ui::Widget librarySlotEntity = m_rightColumn.Append(librarySlot);
	(void)ResizeHandle(m_rightColumn, ui::Orientation::Vertical, librarySlotEntity, false, 80.f, 600.f);
	m_libraryDock.Build(librarySlotEntity, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });

	BuildStatusBar(m_root);
	AttachWindowChrome();

	// ── Contenu des panneaux ─────────────────────────────────────────
	m_tree.Build(m_treeDock.AddPage(String("Arbre de scène"), false, false, 4.f));
	m_sceneListPage = m_treeDock.AddPage(String("Scènes"), true);
	m_objectListPage = m_treeDock.AddPage(String("Objets"), true);
	m_treeDock.SetActive(0);
	m_treeDock.onActivate = [this](int page) {
		if (page == 1 || page == 2)
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
	m_sceneName = ActiveDocumentName();
	RefreshConsole();
	RefreshSceneList();
	RefreshObjectList();
	m_editorBuilt = true;
}

void EditorUi::Rebuild() {
	Teardown();
	Build();
}

void EditorUi::Teardown() {
	m_chrome.Detach();
	CloseDialog();
	ExitRunMode();
	for (ui::Widget popup : m_menuPopups)
		if (popup.Valid())
			ui::Widget(popup).Destroy();
	m_menuPopups.clear();
	m_tree.Teardown();
	m_inspector.Teardown();
	m_library.Teardown();
	m_assets.Teardown();
	m_documents.Teardown();
	if (m_root.Valid()) {
		ui::Widget(m_root).Destroy();
		m_root = ui::Widget();
	}
	m_maximized = false;
	m_editorBuilt = false;
}

bool EditorUi::SetTheme(const String &name) {
	Option<ui::Palette> theme = ThemeByName(name);
	if (theme.IsNone())
		return false;
	m_themeName = name.ToLower();
	m_ctx.gui.SetTheme(theme.Unwrap());
	// Feuille de l'éditeur : couleurs du cadre dérivées de la nouvelle palette.
	if (m_editorSheet)
		m_ctx.gui.RemoveStyleSheet(m_editorSheet);
	m_editorSheet = ui::StyleSheet::Parse(kit::EditorCss(theme.Unwrap()).View(), String(), String("<editor>"));
	m_ctx.gui.AddStyleSheet(m_editorSheet);
	kit::syntax::Colors() = kit::PaletteOf(m_ctx).light ? kit::syntax::Scheme::Light() : kit::syntax::Scheme::Dark();
	if (m_root.Valid())
		Rebuild();
	return true;
}

Option<ui::Palette> EditorUi::ThemeByName(const String &name) {
	String key = name.ToLower();
	if (key == "studio")
		return Some(ui::Palette::Of(ui::Theme::Studio));
	if (key == "dark")
		return Some(ui::Palette::Of(ui::Theme::Dark));
	if (key == "light")
		return Some(ui::Palette::Of(ui::Theme::Light));
	if (key == "aero")
		return Some(ui::Palette::Of(ui::Theme::Aero));
	return NONE;
}

sdl3::FRect EditorUi::ViewportRect() const {
	ui::Widget viewport = m_runMode ? m_runViewport : m_viewport;
	return viewport ? viewport.Bounds() : sdl3::FRect{};
}

bool EditorUi::PointerOverViewport(float x, float y) const {
	const sdl3::FRect rect = ViewportRect();
	if (rect.w <= 0.f || rect.h <= 0.f)
		return false;
	if (x < rect.x || y < rect.y || x >= rect.x + rect.w || y >= rect.y + rect.h)
		return false;
	const ui::Widget front(m_ctx.registry, ui::HitTest(m_ctx.registry, sdl3::FPoint{x, y}));
	const ui::Widget viewport = m_runMode ? m_runViewport : m_viewport;
	if (!front.Valid())
		return true;
	if (!viewport.Contains(front))
		return false;
	// Un widget de l'interface du jeu (bouton, curseur…) reçoit le clic
	// lui-même : la vue ne doit ni sélectionner l'objet derrière, ni bouger.
	const bool gameWidget = m_gameOverlay.Valid() && !(front == m_gameOverlay) && m_gameOverlay.Contains(front);
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

	ui::WidgetBuilder root = ui::Column();
	root.Fixed().Anchor(ui::Anchor::TopLeft).Width(ui::Length::Pct(100.f)).Height(ui::Length::Pct(100.f));
	root.Gap(0.f).Padding(0.f).Background(kit::Rgb(0, 0, 0));
	m_runRoot = m_ctx.gui.Mount(root);

	ui::WidgetBuilder viewport = ui::Viewport3D(Rt().SceneRoot(), Rt().ActiveCamera());
	viewport.GrowWidth().GrowHeight();
	m_runViewport = m_runRoot.Append(viewport);
	Spawn2DLayer(m_runViewport, true);

	// Réticule au centre, seulement quand la souris est capturée (vue
	// subjective) : libre, c'est le curseur qui vise.
	Runtime *runtime = &Rt();
	ui::WidgetBuilder crosshair = ui::Canvas([runtime](ui::Painter &painter, const sdl3::FRect &r) {
		if (!painter.Renderer())
			return;
		sdl3::Renderer &ren = *painter.Renderer();
		if (runtime->EffectiveMouseMode() != Runtime::MouseMode::CAPTURED)
			return;
		const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
		ren.SetDrawColor(sdl3::FColor{1.f, 1.f, 1.f, 0.85f});
		ren.FillRect({cx - 11.f, cy - 1.f, 8.f, 2.f});
		ren.FillRect({cx + 3.f, cy - 1.f, 8.f, 2.f});
		ren.FillRect({cx - 1.f, cy - 11.f, 2.f, 8.f});
		ren.FillRect({cx - 1.f, cy + 3.f, 2.f, 8.f});
	});
	crosshair.Absolute().Anchor(ui::Anchor::TopLeft).Width(ui::Length::Pct(100.f)).Height(ui::Length::Pct(100.f));
	crosshair.PointerThrough();
	m_runViewport.Append(crosshair);

	ui::WidgetBuilder hint = ui::Label(String("Échap : revenir à l'éditeur · ZQSD/WASD + souris : se déplacer · "
									  "F7 : libérer / capturer la souris · F9 : encart des scripts"));
	hint.Absolute().Anchor(ui::Anchor::TopLeft).Offset(14.f, 12.f).FontSize(12.f);
	hint.Color(sdl3::FColor{1.f, 1.f, 1.f, 0.6f}).PointerThrough();
	m_runViewport.Append(hint);

	// Encart « MODE TEST » : les scripts qui tournent. Déplaçable par son
	// en-tête (souris libre), masquable par ✕ ou F9.
	ui::WidgetBuilder panel = ui::Column();
	// Fixed : dessiné (et touché) dans la passe des superpositions, au-dessus
	// de l'interface du jeu qu'il peut désormais survoler. La racine du mode
	// plein écran est elle aussi une superposition (ordre 0) : l'encart passe
	// explicitement APRÈS elle, sinon leur ordre relatif dépendrait de celui
	// de l'ECS — dessin et clics pourraient même ne pas s'accorder.
	panel.Fixed().ZIndex(10);
	panel.Anchor(ui::Anchor::BottomRight).Offset(m_runPanelOffset.x, m_runPanelOffset.y);
	panel.Gap(4.f).Padding(math::Sides{12.f, 8.f}).BorderRadius(6.f).Background(sdl3::FColor{0.08f, 0.08f, 0.09f, 0.82f});
	panel.BorderColor(kit::PaletteOf(m_ctx).separator).PointerThrough();
	m_runPanel = m_runViewport.Append(panel);

	ui::WidgetBuilder header = ui::Row();
	header.Padding(0.f);
	header.Gap(8.f).AlignItems(ui::Align::Center);
	header.Tooltip(String("Glisser pour déplacer (souris libre : F7)"));
	m_runPanelHeader = m_runPanel.Append(header);
	(void)kit::Glyph(m_ctx, m_runPanelHeader, ui::MaterialIcons::DRAG_INDICATOR, sdl3::FColor{0.7f, 0.7f, 0.74f, 1.f},
					 16.f);
	ui::WidgetBuilder title = ui::Label(String("MODE TEST (Run Mode)"));
	title.FontSize(15.f).Color(sdl3::FColor{0.92f, 0.92f, 0.94f, 1.f}).PointerThrough();
	m_runPanelHeader.Append(title);
	m_runPanelClose = kit::IconButton(m_ctx, m_runPanelHeader, ui::MaterialIcons::CLOSE,
									  String("Masquer l'encart (F9 pour le réafficher)"),
									  [this] { SetRunPanelHidden(true); }, 20.f);

	ui::WidgetBuilder body = ui::Column();
	body.Padding(0.f);
	body.Gap(4.f).PointerThrough();
	m_runPanelBody = m_runPanel.Append(body);
	m_draggingRunPanel = false;
	EnsureGameOverlay(); // l'interface du jeu suit dans la vue plein écran
	RefreshRunPanel();
	SetStatus(String("Mode Jeu plein écran — Échap pour revenir"));
}

void EditorUi::ExitRunMode() {
	if (!m_runMode)
		return;
	m_runMode = false;
	Rt().SetHostFullscreen(false);
	EnsureGameOverlay(); // ramenée dans la vue de l'éditeur avant de détruire l'autre
	if (m_runRoot.Valid())
		ui::Widget(m_runRoot).Destroy();
	m_runRoot = m_runViewport = m_runPanel = ui::Widget();
	m_runPanelHeader = m_runPanelClose = m_runPanelBody = ui::Widget();
	m_draggingRunPanel = false;
	kit::SetHidden(m_ctx, m_root, false);
	if (Rt().IsPlaying())
		Rt().Stop();
}

void EditorUi::EnsureGameOverlay() {
	ui::Widget viewport = GameViewport();
	if (!Rt().IsPlaying() || !viewport.Valid())
		return;
	if (m_gameOverlay.Valid()) {
		const Option<ui::Widget> parent = m_gameOverlay.Parent();
		if (parent.IsNone() || !(parent.Value() == viewport))
			(void)m_gameOverlay.MoveTo(viewport);
		return;
	}
	ui::WidgetBuilder overlay = ui::Column();
	// Pct (du PARENT, la vue) et non Rpct (de la fenêtre) : dans l'éditeur, la
	// vue ne remplit pas la fenêtre.
	overlay.Absolute().Anchor(ui::Anchor::TopLeft).Width(ui::Length::Pct(100.f)).Height(ui::Length::Pct(100.f));
	overlay.Gap(0.f).Padding(0.f).PointerThrough();
	m_gameOverlay = viewport.Append(overlay);
}

void EditorUi::ResetGameOverlay() {
	if (m_gameOverlay.Valid())
		ui::Widget(m_gameOverlay).Destroy();
	m_gameOverlay = ui::Widget();
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
	// Pointeur sur le navigateur de ressources : Suppr, F2, Ctrl+D… visent
	// ses fichiers, pas le nœud sélectionné de la scène.
	if (m_assets.HandleKey(event))
		return;
	if (HandleGameMouse(event))
		return;
	if (!Is2DEditing() && HandleModelingEvent(event))
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
		// Outils de SketchUp : Espace sélection, T mètre, O orbite, H
		// panoramique, Maj+Z zoom étendu.
		if (event.IsKeyDown(SDLK_SPACE))
			SetSceneTool(SceneTool::SELECT);
		if (event.IsKeyDown(SDLK_T))
			SetSceneTool(SceneTool::TAPE);
		if (event.IsKeyDown(SDLK_O))
			SetNavTool(NavTool::ORBIT);
		if (event.IsKeyDown(SDLK_H))
			SetNavTool(NavTool::PAN);
		if (event.IsKeyDown(SDLK_Z) && (SDL_GetModState() & SDL_KMOD_SHIFT) != 0)
			ZoomExtents();
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
	if (event.IsKeyDown(SDLK_ESCAPE) && !m_ctx.gui.HasOpenModal()) {
		if (m_navTool != NavTool::NONE)
			SetNavTool(NavTool::NONE);
		else if (m_tapeFirst.Valid() || m_tapeA.IsSome())
			ResetTape();
		else
			Rt().ClearSelection();
	}
}

void EditorUi::Tick(float dt, double fps) {
	if (m_rebuildRequested) {
		m_rebuildRequested = false;
		Rebuild();
	}
	ApplyBrowseResult();
	RefreshModelingUi();
	if (m_window) {
		m_chrome.Update();
		m_titleBar.Update(*m_window);
	}
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
	const String sceneName = ActiveDocumentName();
	if (sceneName != m_sceneName) {
		m_sceneName = sceneName;
		m_tree.ResetExpansion();
		m_documents.Dock().SetTitle(0, ViewportTitle());
		m_library.RefreshWorld();
		m_scenesDirty = true;
		m_documentChoicesDirty = true;
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
	if (m_scenesDirty && (m_treeDock.Active() == 1 || m_treeDock.Active() == 2)) {
		m_scenesDirty = false;
		RefreshSceneList();
		RefreshObjectList();
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
	const SDL_Keymod mods = SDL_GetModState();
	
	// ── Gestion du clic droit ─────────────────────────────────────────────
	if (event.IsMouseDown(SDL_BUTTON_RIGHT))
		m_rightMouseDown = PointerInViewport();
	if (event.IsMouseUp(SDL_BUTTON_RIGHT))
		m_rightMouseDown = false;

	// ── Navigation à la SketchUp ─────────────────────────────────────────
	const bool leftNav = event.IsMouseDown(SDL_BUTTON_LEFT) &&
						 (m_navTool != NavTool::NONE || (mods & SDL_KMOD_ALT) != 0);

	// Début de la navigation (Milieu ou Gauche avec outil/Alt)
	if ((event.IsMouseDown(SDL_BUTTON_MIDDLE) || leftNav) && 
		PointerOverViewport(event.MouseButton().x, event.MouseButton().y)) {
		
		const bool isMiddle = event.IsMouseDown(SDL_BUTTON_MIDDLE);
		const bool pan = (isMiddle && (mods & SDL_KMOD_SHIFT) != 0) ||
						 (leftNav && m_navTool == NavTool::PAN);
		
		m_middleMouseDown = isMiddle;
		m_navPanning = pan;
		m_navOrbiting = !pan;
		
		if (m_navOrbiting)
			m_navPivot = PointUnderCursor(event.MouseButton().x, event.MouseButton().y);
		return;
	}

	// Fin de la navigation (Relâchement du bouton correspondant)
	if (event.IsMouseUp(SDL_BUTTON_MIDDLE) || (event.IsMouseUp(SDL_BUTTON_LEFT) && (m_navOrbiting || m_navPanning))) {
		// On vérifie qu'on relâche bien le bon type de bouton
		if (event.IsMouseUp(SDL_BUTTON_MIDDLE) || !m_modeling.IsActive()) {
			m_navOrbiting = false;
			m_navPanning = false;
			m_middleMouseDown = false;
			// Ne pas retourner immédiatement pour laisser le clic gauche finir son traitement si besoin
		}
	}

	// ── Mouvements de la souris (Drag & Drop pour la caméra) ──────────────
	if (event.IsMouseMotion()) {
		// Ctrl + Milieu glissé : Dolly (avancer / reculer)
		if (m_middleMouseDown && (mods & SDL_KMOD_CTRL) != 0) {
			Rt().DollyCamera(-float(event.MouseMotion().yrel) * Rt().PivotDistance() * 0.01f);
		} 
		// Panoramique
		else if (m_navPanning) {
			const float scale = Rt().PivotDistance() * 0.0016f;
			Rt().PanCamera(-float(event.MouseMotion().xrel) * scale, float(event.MouseMotion().yrel) * scale);
		} 
		// Orbite
		else if (m_navOrbiting) {
			constexpr float SENSITIVITY = 0.006f;
			Rt().OrbitAround(float(event.MouseMotion().xrel) * SENSITIVITY, -float(event.MouseMotion().yrel) * SENSITIVITY, m_navPivot);
		}
		// Rotation libre avec clic droit (si implémentée via LookDelta)
		else if (m_rightMouseDown) {
			constexpr float SENSITIVITY = 0.0035f;
			Rt().ApplyLookDelta(float(event.MouseMotion().xrel) * SENSITIVITY,
								-float(event.MouseMotion().yrel) * SENSITIVITY);
		}
	}

	// ── Molette : Zoom vers le curseur ───────────────────────────────────
	if (event.IsMouseWheel() && PointerInViewport()) {
		float x = 0.f, y = 0.f;
		(void)SDL_GetMouseState(&x, &y);
		if (Option<math::FRay> ray = RayAt(x, y); ray.IsSome())
			Rt().ZoomTowards(ray.Unwrap(), float(event.MouseWheel().y) * 0.15f);
		else
			Rt().DollyCamera(float(event.MouseWheel().y) * Rt().PivotDistance() * 0.18f);
	}

	// Si un outil de modélisation est actif, on stoppe la propagation des clics de sélection
	if (m_modeling.IsActive())
		return;

	// ── Sélection et Outils de Scène (Clic Gauche standard) ───────────────
	if (event.IsMouseDown(SDL_BUTTON_LEFT) && !leftNav) {
		Option<math::FRay> ray = RayAt(event.MouseButton().x, event.MouseButton().y);
		if (ray.IsSome()) {
			// Double-clic sur un nœud (entrée dans un groupe/objet)
			if (event.MouseButton().clicks >= 2 && m_sceneTool != SceneTool::TAPE) {
				if (Option<Runtime::PickHit> hit = Rt().PickAt(ray.Unwrap()); hit.IsSome()) {
					BeginModeling(hit.Value().node);
					return;
				}
			}
			switch (m_sceneTool) {
			case SceneTool::TAPE: TapeClick(ray.Unwrap()); break;
			case SceneTool::SELECT:
			case SceneTool::GIZMO: {
				const Runtime::GizmoAxis axis = Rt().PickGizmoAxis(ray.Unwrap());
				if (axis != Runtime::GizmoAxis::NONE)
					(void)Rt().BeginGizmoDrag(axis, ray.Unwrap());
				else
					(void)Rt().SelectAt(ray.Unwrap());
				break;
			}
			}
		}
	}

	if (event.IsMouseUp(SDL_BUTTON_LEFT) && !leftNav)
		Rt().EndGizmoDrag();

	// ── Survol (Hover) ───────────────────────────────────────────────────
	if (event.IsMouseMotion() && !m_rightMouseDown && !m_navOrbiting && !m_navPanning) {
		Option<math::FRay> ray = RayAt(event.MouseMotion().x, event.MouseMotion().y);
		if (m_sceneTool == SceneTool::TAPE) {
			if (ray.IsSome())
				TapeHover(ray.Unwrap());
		} else if (ray.IsSome()) {
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
	const scene::Node *node = Rt().SelectedNode();
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

void EditorUi::Spawn2DLayer(ui::Widget viewport, bool runView) {
	ui::WidgetBuilder layer = ui::Canvas(
		[this, runView](ui::Painter &painter, const sdl3::FRect &r) {
			if (painter.Renderer())
				Draw2DLayer(*painter.Renderer(), r, runView);
		});
	layer.Absolute().Anchor(ui::Anchor::TopLeft).Width(ui::Length::Pct(100.f)).Height(ui::Length::Pct(100.f));
	layer.PointerThrough();
	viewport.Append(layer);
}

void EditorUi::Draw2DLayer(sdl3::Renderer &ren, sdl3::FRect r, bool runView) {
	SceneDesc *scene = Rt().ActiveDocument();
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
	const SceneDesc *scene = Rt().ActiveDocument();
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
	const SceneDesc *scene = Rt().ActiveDocument();
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
		const scene::Node *node = Rt().FindNode(id);
		const SceneDesc *scene = Rt().ActiveDocument();
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
	const scene::Node *selected = Rt().SelectedNode();
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
	if (key == "objects") {
		m_treeDock.SetActive(2);
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
		else if (argument == "save_object_as" && m_editorBuilt)
			OnSaveObjectAs();
		else if (argument == "import_object" && m_editorBuilt)
			OnImportObject();
		else if (argument == "save_scene_as" && m_editorBuilt)
			OnSaveSceneAs();
		else if (argument == "import_scene" && m_editorBuilt)
			OnImportScene();
		else if (argument == "save_script_as" && m_editorBuilt)
			OnSaveScriptAs(ActiveScriptKey());
		else if (argument == "import_script" && m_editorBuilt)
			OnImportScript();
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
		m_ctx.gui.CloseAllPopups();
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
	// Opérations du navigateur, sans boîte de dialogue : `select_assets`
	// prend des noms séparés par « | ».
	if (cmd == "select_assets") {
		std::vector<String> names;
		for (const String &name : argument.Split(StringView("|")))
			if (!name.Trim().IsEmpty())
				names.push_back(name.Trim());
		return m_assets.SelectNames(names) == names.size();
	}
	if (cmd == "asset_new_folder")
		return m_assets.CreateFolder(argument);
	if (cmd == "asset_rename")
		return m_assets.RenameSelection(argument);
	if (cmd == "asset_duplicate")
		return m_assets.DuplicateSelection();
	if (cmd == "asset_delete")
		return m_assets.DeleteSelection();
	if (cmd == "asset_move")
		return m_assets.MoveEntries(m_assets.SelectedEntries(), argument);
	if (cmd == "open_script") {
		m_documents.OpenLibraryScript(argument);
		return m_documents.Active() != nullptr;
	}
	if (cmd == "open_scene_script") {
		m_documents.OpenSceneScript(argument.IsEmpty() ? ActiveDocumentName() : argument);
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
	if (cmd.StartsWith("model"))
		return ModelingCommand(cmd, argument);
	// Outils de la vue (cf. BuildViewportTools).
	if (cmd == "tool") {
		if (argument == "select")
			SetSceneTool(SceneTool::SELECT);
		else if (argument == "tape")
			SetSceneTool(SceneTool::TAPE);
		else if (argument == "move" || argument == "rotate" || argument == "scale")
			SetGizmoMode(argument == "move"	   ? Runtime::GizmoMode::TRANSLATE
						 : argument == "rotate" ? Runtime::GizmoMode::ROTATE
												: Runtime::GizmoMode::SCALE);
		else if (argument == "orbit" || argument == "pan" || argument == "none")
			SetNavTool(argument == "orbit" ? NavTool::ORBIT : argument == "pan" ? NavTool::PAN : NavTool::NONE);
		else
			return false;
		return true;
	}
	if (cmd == "tape_node") { // mètre : clic sur ce nœud (son origine)
		const scene::NodeId id = Rt().ResolveId(argument);
		if (!Rt().FindObject3D(id))
			return false;
		TapeNode(id);
		return true;
	}
	if (cmd == "sketch") {
		if (argument == "ground" || argument == "front" || argument == "side" || argument == "face")
			BeginSketch(argument == "ground" ? 0 : argument == "front" ? 1 : argument == "side" ? 2 : 3);
		else
			EndSketch();
		return m_modeling.IsActive();
	}
	if (cmd == "zoom_extents") {
		ZoomExtents();
		return true;
	}
	if (cmd == "workshop_light") {
		SetModelingLight(argument != "off");
		return true;
	}
	return false;
}

InspectorActions EditorUi::MakeInspectorActions() {
	InspectorActions actions;
	actions.openScript = [this](const String &name) { m_documents.OpenLibraryScript(name); };
	actions.editJson = [this](scene::NodeId id, const String &type) { m_documents.OpenComponentJson(id, type); };
	actions.status = [this](const String &text) { SetStatus(text); };
	actions.beginModeling = [this](scene::NodeId id) { BeginModeling(id); };
	actions.endModeling = [this] { EndModeling(); };
	actions.exportMesh = [this](const char *extension) { ExportModeledMesh(extension); };
	return actions;
}

AssetActions EditorUi::MakeAssetActions() {
	AssetActions actions;
	actions.openScript = [this](const String &name) { m_documents.OpenLibraryScript(name); };
	actions.openSceneScript = [this](const String &scene) { m_documents.OpenSceneScript(scene); };
	actions.openFile = [this](const String &path) { m_documents.OpenFile(path); };
	actions.importModel = [this](const String &path) { ImportModel(path); };
	actions.previewSound = [this](const String &path) {
		if (Rt().IsPreviewing()) {
			Rt().StopPreview();
			SetStatus(String("Écoute arrêtée"));
		} else if (Rt().PreviewSound(path) != AudioEngine::NO_VOICE) {
			SetStatus(String::Format("Écoute : %s — double-clic pour arrêter", Runtime::FileStem(path).CStr()));
		}
	};
	actions.openScene = [this](const String &scene) { (void)Rt().OpenScene(SceneRef{scene}); };
	actions.openPath = [this](const ProjectPath &path) {
		if (Rt().OpenDocument(path))
			SetStatus(String::Format("%s ouvert : %s", ProjectPathKindName(path.kind), path.ToString().CStr()));
		else
			SetStatus(String::Format("Introuvable : %s", path.ToString().CStr()));
	};
	actions.instantiateScene = [this](const String &path) {
		auto instance = Rt().InstantiateSceneFile(path);
		if (instance.IsError()) {
			Rt().LogError(String::Format("Instanciation : %s", instance.Error().CStr()));
			return;
		}
		(void)Rt().Select(instance.Value());
		SetStatus(String::Format("Scène instanciée : %s", path.CStr()));
	};
	actions.instantiateObject = [this](const String &object) { PlaceObject(object); };
	actions.status = [this](const String &text) { SetStatus(text); };
	return actions;
}

String EditorUi::ActiveDocumentName() const {
	const SceneDesc *scene = Rt().ActiveDocument();
	return scene ? scene->name : String();
}

math::FVector3 EditorUi::SpawnPointInFrontOfCamera(float lift) const {
	const render3d::Camera &camera = Rt().ActiveCamera();
	const math::FVector3 forward = (camera.target - camera.position).Normalize();
	math::FVector3 at = camera.position + forward * 7.f;
	at.y = sdl3::Max(at.y, lift * 0.5f);
	return at;
}

ui::Widget EditorUi::ResizeHandle(ui::Widget parent, ui::Orientation orient, ui::Widget target, bool before, float minSize,
		float maxSize) {
	const bool horizontal = orient == ui::Orientation::Horizontal;
	ui::WidgetBuilder handle = ui::Row();
	handle.Padding(0.f);
	if (horizontal)
		handle.Width(5.f).GrowHeight();
	else
		handle.GrowWidth().Height(5.f);
	handle.Background(kit::PaletteOf(m_ctx).base);
	ui::ResizeHandle h;
	h.axis = orient;
	h.after = !before;
	(before ? h.minBefore : h.minAfter) = minSize;
	h.maxSize = maxSize;
	handle.With(h);
	if (before)
		return parent.Append(handle);
	// La poignée PRÉCÈDE le panneau qu'elle redimensionne (ancré à droite / en bas).
	const std::vector<ui::Widget> children = parent.Children();
	const auto at = std::find(children.begin(), children.end(), target);
	return parent.Insert(size_t(at - children.begin()), handle);
}

void EditorUi::PlaceObject(const String &objectName) {
	// Sous la sélection (l'instance qui porte un nœud généré), à défaut à la
	// racine du document ouvert.
	auto placed = Rt().InstantiateObject(ObjectRef{objectName}, Rt().SelectedId());
	if (placed.IsError()) {
		SetStatus(String::Format("Instance impossible : %s", placed.Error().CStr()));
		return;
	}
	(void)Rt().Select(placed.Value());
	SetStatus(String::Format("Instance de « %s » posée", objectName.CStr()));
}

ui::WidgetBuilder EditorUi::MenuAction(const char *text, const char *shortcut, std::function<void()> action) {
	ui::WidgetBuilder item = ui::MenuItem(String(text), String(shortcut));
	item.OnClick(std::move(action));
	return item;
}

void EditorUi::AddItem(ui::Widget menu, const char *text, const char *shortcut, std::function<void()> action) {
	ui::WidgetBuilder item = MenuAction(text, shortcut, std::move(action));
	menu.Append(item);
}

void EditorUi::BuildMenuBar(ui::Widget parent) {
	ui::WidgetBuilder bar = ui::MenuBar();
	bar.GrowWidth().Padding(math::Sides{6.f, 2.f}).Class("editor-header");
	ui::Widget barEntity = parent.Append(bar);

	ui::Widget file = barEntity.Append(ui::Menu(String("Fichier")).Children({MenuAction("Nouveau projet…", "Ctrl+N", [this] { OnNewProject(); }),
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
		})}));
	m_menuPopups.push_back(file);
	ui::Widget scenes = file.Append(ui::SubMenu(String("Ouvrir une scène")));
	m_menuPopups.push_back(scenes);
	for (const String &name : Rt().GetProject().Fs().ListScenes())
		AddItem(scenes, name.CStr(), "", [this, name] { (void)Rt().OpenScene(SceneRef{name}); });

	m_menuPopups.push_back(barEntity.Append(ui::Menu(String("Édition")).Children({MenuAction("Annuler", "Ctrl+Z", [this] { OnUndo(); }),
MenuAction("Rétablir", "Ctrl+Y", [this] { OnRedo(); }),
MenuAction("Dupliquer", "Ctrl+D", [this] { OnDuplicate(); }),
MenuAction("Supprimer", "Suppr", [this] { OnDeleteSelection(); }),
MenuAction("Renommer…", "F2", [this] { m_tree.BeginRename(Rt().SelectedId()); }),
MenuAction("Tout désélectionner", "Échap", [this] { Rt().ClearSelection(); }),
MenuAction("Déplacer", "W", [this] { SetGizmoMode(Runtime::GizmoMode::TRANSLATE); }),
MenuAction("Tourner", "E", [this] { SetGizmoMode(Runtime::GizmoMode::ROTATE); }),
MenuAction("Redimensionner", "R", [this] { SetGizmoMode(Runtime::GizmoMode::SCALE); }),
MenuAction("Magnétisme", "X", [this] { ToggleSnap(); })})));

	// ── Objets réutilisables : une arborescence éditée une fois, posée
	// partout ; ses instances suivent ses modifications.
	ui::Widget objectsMenu = barEntity.Append(ui::Menu(String("Objets")).Children({MenuAction("Nouvel objet", "", [this] {
			const ObjectRef object = Rt().AddEmptyObject();
			SetStatus(String::Format("Objet « %s » ouvert : ajoutez-y des nœuds", object.name.CStr()));
		}),
MenuAction("Créer un objet à partir de la sélection", "", [this] {
			auto object = Rt().CreateObjectFromNode(Rt().SelectedId());
			SetStatus(object.IsOk() ? String::Format("Objet « %s » créé ; la sélection en est une instance",
													  object.Value().name.CStr())
									: String::Format("Objet impossible : %s", object.Error().CStr()));
		}),
MenuAction("Rendre l'instance indépendante", "", [this] {
			auto done = Rt().DetachInstance(Rt().SelectedId());
			SetStatus(done.IsOk() ? String("Instance rendue indépendante")
								  : String::Format("Impossible : %s", done.Error().CStr()));
		}),
MenuAction("Actualiser les instances", "", [this] {
			Rt().RefreshObjectInstances();
			Rt().RebuildRuntime();
			SetStatus(String("Instances régénérées à partir des objets"));
		})}));
	m_menuPopups.push_back(objectsMenu);
	BuildModelingMenu(barEntity);
	ui::Widget place = objectsMenu.Append(ui::SubMenu(String("Poser une instance")));
	m_menuPopups.push_back(place);
	ui::Widget openObject = objectsMenu.Append(ui::SubMenu(String("Ouvrir un objet")));
	m_menuPopups.push_back(openObject);
	for (const String &name : Rt().GetProject().Fs().ListObjects()) {
		AddItem(place, name.CStr(), "", [this, name] { PlaceObject(name); });
		AddItem(openObject, name.CStr(), "", [this, name] { (void)Rt().OpenObject(ObjectRef{name}); });
	}

	ui::Widget tools = barEntity.Append(ui::Menu(String("Outils")).Children({MenuAction("Console de script", "", [this] { m_documents.OpenToolConsole(); }),
MenuAction("Script de la scène", "", [this] { m_documents.OpenSceneScript(ActiveDocumentName()); }),
MenuAction("Vérifier tous les scripts", "", [this] { OnCheckAllScripts(); }),
MenuAction("Cadrer la sélection", "F", [this] { OnFocusSelection(); }),
MenuAction("Parcourir les modèles", "", [this] {
			m_assetDock.SetActive(0);
			if (m_assets.Model().Navigate(m_assetsDir + String("/models")))
				m_assets.MarkDirty();
		})}));
	m_menuPopups.push_back(tools);
	ui::Widget create = tools.Append(ui::SubMenu(String("Créer un nœud")));
	m_menuPopups.push_back(create);
	for (const NodeTemplate &entry : NODE_TEMPLATES) {
		const String key(entry.key);
		AddItem(create, entry.label, "", [this, key] {
			const SceneDesc *scene = Rt().ActiveDocument();
			(void)m_tree.CreateNode(key, scene ? scene->tree.Root() : scene::NodeId{});
		});
	}

	ui::Widget window = barEntity.Append(ui::Menu(String("Fenêtre")).Children({MenuAction("Arbre de scène", "", [this] { (void)FocusPanel("tree", 0); }),
MenuAction("Inspecteur", "", [this] { (void)FocusPanel("inspector", 0); }),
MenuAction("Ressources", "", [this] { (void)FocusPanel("assets", 0); }),
MenuAction("Console", "", [this] { (void)FocusPanel("console", 0); }),
MenuAction("Profileur", "", [this] { (void)FocusPanel("profiler", 0); }),
MenuAction("Vue 3D seule", "", [this] { ToggleMaximized(); })}));
	m_menuPopups.push_back(window);
	
	ui::Widget themes = window.Append(ui::SubMenu(String("Thème")));
	m_menuPopups.push_back(themes);
	AddItem(themes, "Atelier (par défaut)", "", [this] { (void)SetTheme("studio"); });
	AddItem(themes, "Sombre", "", [this] { (void)SetTheme("dark"); });
	AddItem(themes, "Clair", "", [this] { (void)SetTheme("light"); });
	AddItem(themes, "Verre", "", [this] { (void)SetTheme("aero"); });

	m_menuPopups.push_back(barEntity.Append(ui::Menu(String("Compiler")).Children({MenuAction("Lancer dans la vue", "F5", [this] { Rt().TogglePlay(); }),
MenuAction("Lancer en plein écran", "F6", [this] { EnterRunMode(); }),
MenuAction("Arrêter", "", [this] { Rt().Stop(); }),
MenuAction("Vérifier les scripts", "", [this] { OnCheckAllScripts(); }),
MenuAction("Exporter le rapport", "", [this] { OnExportReport(); })})));

	m_menuPopups.push_back(barEntity.Append(ui::Menu(String("Aide")).Children({MenuAction("Raccourcis", "", [this] { OnShowShortcuts(); }),
MenuAction("À propos", "", [this] { OnAbout(); })})));
}

void EditorUi::BuildDockMenus() {
	m_treeMenu = m_ctx.gui.Mount(ui::ContextMenu());
	m_menuPopups.push_back(m_treeMenu);
	AddItem(m_treeMenu, "Tout déplier", "", [this] { m_tree.ExpandAll(true); });
	AddItem(m_treeMenu, "Tout replier", "", [this] { m_tree.ExpandAll(false); });
	AddItem(m_treeMenu, "Montrer la sélection", "", [this] { m_tree.RevealSelection(); });

	m_documentsMenu = m_ctx.gui.Mount(ui::ContextMenu());
	m_menuPopups.push_back(m_documentsMenu);
	AddItem(m_documentsMenu, "Console de script", "", [this] { m_documents.OpenToolConsole(); });
	AddItem(m_documentsMenu, "Script de la scène", "", [this] { m_documents.OpenSceneScript(ActiveDocumentName()); });
	AddItem(m_documentsMenu, "Fermer tous les documents", "", [this] {
		while (m_documents.Dock().Count() > 1)
			m_documents.Close(m_documents.Dock().Count() - 1);
	});
	AddItem(m_documentsMenu, "Vue 3D seule", "", [this] { ToggleMaximized(); });

	m_inspectorMenu = m_ctx.gui.Mount(ui::ContextMenu());
	m_menuPopups.push_back(m_inspectorMenu);
	AddItem(m_inspectorMenu, "Modifier les propriétés en JSON…", "", [this] {
		if (Rt().SelectedId().Valid())
			m_documents.OpenComponentJson(Rt().SelectedId(), String());
	});
	AddItem(m_inspectorMenu, "Cadrer la sélection", "F", [this] { OnFocusSelection(); });

	m_assetsMenu = m_ctx.gui.Mount(ui::ContextMenu());
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

void EditorUi::BuildViewportPage(ui::Widget page) {
	const ui::Palette &theme = m_ctx.Theme();
	ui::WidgetBuilder bar = ui::Row();
	bar.Gap(2.f).Padding(math::Sides{6.f, 3.f}).GrowWidth().AlignItems(ui::Align::Center);
	bar.Background(kit::PaletteOf(m_ctx).toolbar);
	ui::Widget barEntity = page.Append(bar);

	BuildViewportTools(barEntity);
	(void)kit::Spacer(m_ctx, barEntity);

	// Sélecteur de document : deux sous-menus, les scènes puis les objets
	// (un objet s'édite comme une scène) — tenu à jour par RefreshStatusBar.
	ui::WidgetBuilder comboSlot = ui::Row();
	comboSlot.Padding(0.f).Gap(0.f).AlignItems(ui::Align::Center);
	m_sceneComboSlot = barEntity.Append(comboSlot);
	m_sceneCombo = m_sceneComboSlot.Append(DocumentCombo());
	m_documentChoicesDirty = true;

	ui::WidgetBuilder searchRow = ui::Row();
	searchRow.Padding(0.f);
	searchRow.Gap(4.f).Width(120.f).AlignItems(ui::Align::Center);
	ui::Widget searchRowEntity = barEntity.Append(searchRow);
	// Pendant la modélisation, la place va aux outils.
	m_sceneOnlyButtons.push_back(m_sceneComboSlot);
	m_sceneOnlyButtons.push_back(searchRowEntity);
	(void)kit::Glyph(m_ctx, searchRowEntity, ui::MaterialIcons::SEARCH, theme.muted, 15.f);
	ui::WidgetBuilder search = ui::Input(String("Aller à un nœud…"));
	search.GrowWidth().Height(24.f).FontSize(13.f);
	search.OnSubmit([this](const String &text) { GoToNode(text); });
	searchRowEntity.Append(search);
	(void)kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::FULLSCREEN, String("Vue 3D seule (masquer les panneaux)"),
						  [this] { ToggleMaximized(); }, 26.f);

	// La vue elle-même, avec les superpositions (axes, projection, badge).
	ui::WidgetBuilder viewport = ui::Viewport3D(Rt().SceneRoot(), Rt().ActiveCamera());
	viewport.GrowWidth().GrowHeight();
	// Glisser une vignette de modèle du navigateur dans la vue : import.
	viewport.DropTarget(String("asset"));
	viewport.OnDrop([this](int64_t index) {
		// Un OBJET déposé dans la vue y pose une instance ; le reste
		// (modèle glTF, scène emballée…) suit l'ouverture habituelle.
		const std::vector<AssetEntry> &entries = m_assets.Entries();
		const size_t i = size_t(index);
		if (i < entries.size() && entries[i].kind == AssetKind::OBJECT && entries[i].managed)
			PlaceObject(entries[i].name);
		else if (i < entries.size() && entries[i].kind == AssetKind::SOUND)
			AddSoundSource(entries[i].location);
		else
			m_assets.Activate(i);
	});
	m_viewport = page.Append(viewport);
	// Le calque 2D d'abord : tout le reste (repères, badge, interface du
	// jeu) se dessine par-dessus.
	Spawn2DLayer(m_viewport, false);

	Runtime *runtime = &Rt();
	ui::WidgetBuilder axes = ui::Canvas([runtime](ui::Painter &painter, const sdl3::FRect &r) {
		if (!painter.Renderer())
			return;
		sdl3::Renderer &ren = *painter.Renderer();
		DrawAxisGizmo(ren, r, runtime->ActiveCamera());
	});
	axes.Absolute().Anchor(ui::Anchor::TopRight).Offset(-10.f, 10.f).Size(92.f, 92.f).PointerThrough();
	m_axesGizmo = m_viewport.Append(axes);
	ui::WidgetBuilder projection = ui::Label(String("‹ Persp"));
	projection.Absolute().Anchor(ui::Anchor::TopRight).Offset(-30.f, 104.f).FontSize(12.f);
	projection.Color(sdl3::FColor{1.f, 1.f, 1.f, 0.65f}).PointerThrough();
	m_projectionLabel = m_viewport.Append(projection);
	Show2DWorkspace();

	ui::WidgetBuilder badge = ui::Label(String("▶  MODE JEU — F5 pour arrêter"));
	badge.Absolute().Anchor(ui::Anchor::TopLeft).Offset(12.f, 10.f).FontSize(13.f).Bold();
	badge.Padding(math::Sides{10.f, 4.f}).BorderRadius(4.f).Background(sdl3::FColor{0.12f, 0.45f, 0.18f, 0.85f});
	badge.Color(sdl3::FColor::WHITE()).PointerThrough();
	m_playBadge = m_viewport.Append(badge);
	kit::SetHidden(m_ctx, m_playBadge, true);
	BuildModelingOverlay(m_viewport);
}

void EditorUi::AddToolbarSeparator(ui::Widget parent) {
	ui::WidgetBuilder separator = ui::Row();
	separator.Padding(0.f);
	separator.Size(1.f, 20.f).Background(kit::PaletteOf(m_ctx).separator).Margin(math::Sides{4.f, 0.f});
	parent.Append(separator);
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
	for (ui::Widget entity : {m_treeDock.Frame(), m_leftHandle, m_bottomRow, Previous(m_bottomRow), m_rightColumn,
							   Previous(m_rightColumn)})
		kit::SetHidden(m_ctx, entity, m_maximized);
	SetStatus(String(m_maximized ? "Vue 3D seule — même bouton pour revenir" : "Panneaux restaurés"));
}

ui::Widget EditorUi::Previous(ui::Widget entity) const {
	const Option<ui::Widget> parent = entity.Parent();
	if (parent.IsNone())
		return ui::Widget();
	const std::vector<ui::Widget> list = parent.Value().Children();
	for (size_t i = 1; i < list.size(); ++i)
		if (list[i] == entity)
			return list[i - 1];
	return ui::Widget();
}

void EditorUi::GoToNode(const String &text) {
	const SceneDesc *scene = Rt().ActiveDocument();
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

float EditorUi::UpdateViewportCamera(ui::Widget entity) {
	auto viewport = entity.Get<ui::Viewport3DState>();
	if (viewport.IsNone())
		return 0.f;
	viewport.Unwrap()->camera = Rt().ActiveCamera();
	const ui::Viewport3DState &vp = *viewport.Unwrap();
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
	if (event.IsMouseDown(SDL_BUTTON_LEFT) && m_runPanel.IsVisible() &&
		Rt().EffectiveMouseMode() != Runtime::MouseMode::CAPTURED) {
		const sdl3::FPoint at{event.MouseButton().x, event.MouseButton().y};
		const ui::Widget front(m_ctx.registry, ui::HitTest(m_ctx.registry, at));
		if (front.Valid() && m_runPanelHeader.Contains(front) && !m_runPanelClose.Contains(front)) {
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
	if (!m_runPanel.Valid() || !m_runViewport.Valid())
		return;
	// Ancré en bas à droite : le décalage va de 0 (coin) à −(vue − encart).
	const sdl3::FRect p = m_runPanel.Bounds(), v = m_runViewport.Bounds();
	sdl3::FPoint offset = m_runPanelOffset;
	offset.x = sdl3::Clamp(offset.x + dx, -sdl3::Max(0.f, v.w - p.w), 0.f);
	offset.y = sdl3::Clamp(offset.y + dy, -sdl3::Max(0.f, v.h - p.h), 0.f);
	m_runPanel.Offset(offset.x, offset.y);
	m_runPanelOffset = offset;
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
		SetRunPanelHidden(m_runPanel.IsVisible());
	(void)HandleGameMouse(event);
	if (event.IsKeyDown(SDLK_ESCAPE) || event.IsKeyDown(SDLK_F6) || event.IsKeyDown(SDLK_F5))
		ExitRunMode();
}

void EditorUi::RefreshRunPanel() {
	if (!m_runPanelBody.Valid())
		return;
	// Seul le corps est reconstruit (l'en-tête et son ✕ restent en place).
	kit::ClearChildren(m_ctx, m_runPanelBody);
	ui::WidgetBuilder rule = ui::Row();
	rule.Padding(0.f);
	rule.GrowWidth().Height(1.f).Background(kit::PaletteOf(m_ctx).separator).PointerThrough();
	m_runPanelBody.Append(rule);
	for (const ScriptStatus &status : Rt().ScriptStatuses()) {
		ui::WidgetBuilder row = ui::Row();
		row.Padding(0.f);
		row.Gap(6.f).AlignItems(ui::Align::Center).PointerThrough();
		ui::Widget rowEntity = m_runPanelBody.Append(row);
		const bool failed = !status.error.IsEmpty();
		const sdl3::FColor dot = failed ? kit::PaletteOf(m_ctx).error : status.running ? kit::PaletteOf(m_ctx).ok : kit::PaletteOf(m_ctx).warning;
		ui::WidgetBuilder square = ui::Row();
		square.Padding(0.f);
		square.Size(8.f, 8.f).Background(dot).PointerThrough();
		rowEntity.Append(square);
		const bool sceneScript = status.role == "scène";
		const char *state = failed ? "Erreur" : !status.running ? "Arrêté" : sceneScript ? "Actif" : "Animé";
		const String text = sceneScript ? String::Format("%s.script (%s)", status.name.CStr(), state)
										: String::Format("%s.script (%s, %d nœud%s)", status.name.CStr(), state,
														 int(status.nodes), status.nodes > 1 ? "s" : "");
		ui::WidgetBuilder label = ui::Label(text);
		label.FontSize(13.f).Color(sdl3::FColor{0.86f, 0.86f, 0.88f, 1.f}).PointerThrough();
		rowEntity.Append(label);
	}

	// Objets de script vivants, regroupés par classe (registres C++ : aucune
	// exécution de script, donc rien qu'un script bogué puisse figer).
	struct Group {
		String signature;
		int count = 0;
	};
	std::vector<Group> groups;
	for (const ScriptObjectInfo &info : Rt().LiveScriptObjects()) {
		if (info.destroyed)
			continue;
		const String signature = info.Signature();
		auto it = std::find_if(groups.begin(), groups.end(), [&](const Group &g) { return g.signature == signature; });
		if (it == groups.end())
			groups.push_back(Group{signature, 1});
		else
			++it->count;
	}
	if (groups.empty())
		return;
	ui::WidgetBuilder heading = ui::Label(String("Objets vivants"));
	heading.FontSize(12.f).Color(sdl3::FColor{0.62f, 0.64f, 0.70f, 1.f}).PointerThrough();
	m_runPanelBody.Append(heading);
	constexpr size_t MAX_GROUPS = 8;
	for (size_t i = 0; i < groups.size() && i < MAX_GROUPS; ++i) {
		const String text = groups[i].count > 1 ? String::Format("%s ×%d", groups[i].signature.CStr(), groups[i].count)
												: groups[i].signature;
		ui::WidgetBuilder label = ui::Label(text);
		label.FontSize(12.f).Color(kit::syntax::Colors().type).PointerThrough();
		m_runPanelBody.Append(label);
	}
	if (groups.size() > MAX_GROUPS) {
		ui::WidgetBuilder more = ui::Label(String::Format("… %d autres classes", int(groups.size() - MAX_GROUPS)));
		more.FontSize(12.f).Color(sdl3::FColor{0.62f, 0.64f, 0.70f, 1.f}).PointerThrough();
		m_runPanelBody.Append(more);
	}
}

void EditorUi::BuildConsolePage(ui::Widget page) {
	ui::WidgetBuilder console = ui::LogView(kit::syntax::HighlightLog);
	console.GrowWidth().GrowHeight().FontSize(12.f);
	m_console = page.Append(console);
}

void EditorUi::RefreshConsole() {
	if (!m_console.Valid())
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
	m_console.SetText(std::move(text));
}

void EditorUi::AddProfilerRow(const char *label, const String &value) {
	kit::PropertyRows rows(m_ctx, 150.f);
	(void)rows.ReadOnly(m_profilerPage, label, value);
}

void EditorUi::RefreshProfiler() {
	if (!m_profilerPage.Valid())
		return;
	kit::ClearChildren(m_ctx, m_profilerPage);
	const SceneDesc *scene = Rt().ActiveDocument();
	AddProfilerRow("Cadence", String::Format("%.1f img/s", m_fps));
	AddProfilerRow("Simulation", String::Format("%.2f ms", Rt().SimulationMs()));
	AddProfilerRow("Objets (scène)", String::From(int(scene ? scene->NodeCount() : 0)));
	AddProfilerRow("Nœuds 3D", String::From(int(Rt().RuntimeNodeCount())));
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
		ui::PlotBuilder plot = ui::PlotLines(m_fpsHistory, String("images / seconde"));
		plot.GrowWidth().Height(120.f);
		m_profilerPage.Append(plot);
	}
}

void EditorUi::RefreshSceneList() {
	if (!m_sceneListPage.Valid())
		return;
	kit::ClearChildren(m_ctx, m_sceneListPage);
	const Project &project = Rt().GetProject();
	int index = 0;
	for (const String &sceneName : project.Fs().ListScenes()) { // jamais d'objets (onglet « Objets »)
		const SceneDesc &scene = *project.Fs().FindScene(SceneRef{sceneName});
		const bool active = Rt().ActivePath() == ProjectPath::Scene(scene.name);
		ui::WidgetBuilder row = ui::Selectable(String(), index++);
		row.GrowWidth().Gap(6.f).Padding(math::Sides{6.f, 3.f}).Tooltip(scene.description);
		const String name = scene.name;
		row.OnClick([this, name] { (void)Rt().OpenScene(SceneRef{name}); });
		ui::Widget rowEntity = m_sceneListPage.Append(row);
		if (active)
			kit::MarkSelected(m_sceneListPage, index - 1);
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::LAYERS, kit::Rgb(126, 172, 232), 16.f);
		ui::WidgetBuilder label = ui::Label(scene.name);
		label.FontSize(13.f).PointerThrough();
		rowEntity.Append(label);
		ui::WidgetBuilder detail = ui::Label(String::Format("(%d objets)", int(scene.NodeCount())));
		detail.GrowWidth().Ellipsis().FontSize(12.f).Color(m_ctx.Theme().muted).PointerThrough();
		rowEntity.Append(detail);
	}
}

ui::WidgetBuilder EditorUi::DocumentCombo() {
	const std::vector<ui::ComboCategory> choices = DocumentChoices();
	ui::WidgetBuilder combo = ui::Combo(choices, -1);
	combo.Size(140.f, 24.f).FontSize(13.f).Tooltip(String("Document ouvert : scène ou objet"));
	combo.OnChange([this, choices](float index) {
		// Premier sous-menu : les scènes ; second : les objets.
		int i = int(index);
		for (size_t group = 0; group < choices.size(); ++group) {
			if (i >= 0 && size_t(i) < choices[group].items.size()) {
				const String name = choices[group].items[size_t(i)];
				(void)(group == 0 ? Rt().OpenScene(SceneRef{name}) : Rt().OpenObject(ObjectRef{name}));
				return;
			}
			i -= int(choices[group].items.size());
		}
	});
	return combo;
}

std::vector<ui::ComboCategory> EditorUi::DocumentChoices() const {
	const std::vector<String> scenes = Rt().GetProject().Fs().ListScenes();
	const std::vector<String> objects = Rt().GetProject().Fs().ListObjects();
	std::vector<ui::ComboCategory> categories;
	categories.push_back({String::Format("Scènes (%d)", int(scenes.size())), scenes});
	if (!objects.empty())
		categories.push_back({String::Format("Objets (%d)", int(objects.size())), objects});
	return categories;
}

void EditorUi::RefreshObjectList() {
	if (!m_objectListPage.Valid())
		return;
	kit::ClearChildren(m_ctx, m_objectListPage);
	const Project &project = Rt().GetProject();
	const ui::Palette &theme = m_ctx.Theme();

	// En-tête : créer un objet, vide ou à partir de la sélection.
	ui::WidgetBuilder head = ui::Row();
	head.Padding(math::Sides{4.f, 2.f}).Gap(4.f).GrowWidth().AlignItems(ui::Align::Center);
	ui::Widget headEntity = m_objectListPage.Append(head);
	ui::WidgetBuilder title =
		ui::Label(String::Format("Objets réutilisables (%d)", int(project.Fs().ListObjects().size())));
	title.GrowWidth().FontSize(12.f).Color(theme.muted).Ellipsis().PointerThrough();
	headEntity.Append(title);
	(void)kit::IconButton(m_ctx, headEntity, ui::MaterialIcons::ADD_BOX, String("Nouvel objet"),
						  [this] {
							  const ObjectRef object = Rt().AddEmptyObject();
							  SetStatus(String::Format("Objet « %s » ouvert : ajoutez-y des nœuds", object.name.CStr()));
						  },
						  18.f, nullptr, theme.muted);
	(void)kit::IconButton(m_ctx, headEntity, ui::MaterialIcons::CATEGORY,
						  String("Créer un objet à partir de la sélection (elle en devient une instance)"),
						  [this] {
							  auto object = Rt().CreateObjectFromNode(Rt().SelectedId());
							  SetStatus(object.IsOk() ? String::Format("Objet « %s » créé ; la sélection en est une instance",
																	   object.Value().name.CStr())
													  : String::Format("Objet impossible : %s", object.Error().CStr()));
						  },
						  18.f, nullptr, theme.muted);

	// Une ligne par objet : clic = l'ouvrir ; bouton = en poser une instance
	// dans le document ouvert (sous la sélection).
	int index = 0;
	for (const String &objectName : project.Fs().ListObjects()) {
		const SceneDesc &scene = *project.Fs().FindObject(ObjectRef{objectName});
		const bool active = Rt().ActivePath() == ProjectPath::Object(scene.name);
		ui::WidgetBuilder row = ui::Selectable(String(), index++);
		row.GrowWidth().Gap(6.f).Padding(math::Sides{6.f, 3.f});
		row.Tooltip((scene.description.IsEmpty() ? scene.name : scene.description) +
					String("\nObjet réutilisable : ses instances suivent ses modifications"));
		const String name = scene.name;
		row.OnClick([this, name] { (void)Rt().OpenObject(ObjectRef{name}); });
		ui::Widget rowEntity = m_objectListPage.Append(row);
		if (active)
			kit::MarkSelected(m_objectListPage, index - 1);
		(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::CATEGORY, kit::Rgb(232, 176, 92), 16.f);
		ui::WidgetBuilder label = ui::Label(scene.name);
		label.FontSize(13.f).PointerThrough();
		rowEntity.Append(label);
		ui::WidgetBuilder detail = ui::Label(String::Format("(%d nœuds)", int(scene.NodeCount())));
		detail.GrowWidth().Ellipsis().FontSize(12.f).Color(theme.muted).PointerThrough();
		rowEntity.Append(detail);
		if (!active)
			(void)kit::IconButton(m_ctx, rowEntity, ui::MaterialIcons::ADD_LOCATION_ALT,
								  String("Poser une instance dans le document ouvert (sous la sélection)"),
								  [this, name] { PlaceObject(name); }, 16.f, nullptr, theme.muted);
	}
}

void EditorUi::BuildStatusBar(ui::Widget parent) {
	ui::WidgetBuilder bar = ui::Row();
	bar.Gap(16.f).Padding(math::Sides{10.f, 3.f}).GrowWidth().AlignItems(ui::Align::Center);
	bar.Class("editor-header");
	ui::Widget barEntity = parent.Append(bar);
	m_statusBar = barEntity;
	m_statusFps = StatusLabel(barEntity, "— img/s", 70.f);
	m_statusNodes = StatusLabel(barEntity, "— objets", 90.f);
	m_statusBodies = StatusLabel(barEntity, "— corps", 80.f);
	m_statusScene = StatusLabel(barEntity, "—", 260.f); // « Projet · /objects/Torche »
	m_statusMode = StatusLabel(barEntity, "■ Édition", 80.f);
	m_statusGizmo = StatusLabel(barEntity, "déplacer", 170.f);
	ui::WidgetBuilder message = ui::Label(m_statusMessage);
	message.GrowWidth().FontSize(12.f).Color(m_ctx.Theme().muted).Ellipsis().TextAlign(ui::TextAlign::Right);
	m_statusMessageLabel = barEntity.Append(message);
	if (m_window) {
		// Poignée de redimensionnement (coin bas-droit, cf. ui::WindowChrome).
		const bool icons = m_ctx.gui.GetFonts().HasFamily(ui::Glyphs::FontFamily<ui::MaterialIcons>());
		ui::WidgetBuilder grip = icons ? ui::Icon(ui::MaterialIcons::SOUTH_EAST, 16.f)
									   : ui::Label(String("\xe2\x87\xb2"));
		grip.Color(m_ctx.Theme().muted).Tooltip(String("Glisser pour redimensionner la fenêtre"));
		m_statusGrip = barEntity.Append(grip);
	} else {
		m_statusGrip = ui::Widget();
	}
}

ui::Widget EditorUi::StatusLabel(ui::Widget parent, const char *text, float width) {
	ui::WidgetBuilder label = ui::Label(String(text));
	label.Size(width, 0.f).FontSize(12.f).Ellipsis();
	return parent.Append(label);
}

void EditorUi::RefreshStatusBar() {
	const SceneDesc *scene = Rt().ActiveDocument();
	kit::SetLabelText(m_ctx, m_statusFps, String::Format("%.0f img/s", m_fps));
	kit::SetLabelText(m_ctx, m_statusNodes, String::Format("%d objets", int(scene ? scene->NodeCount() : 0)));
	kit::SetLabelText(m_ctx, m_statusBodies, String::Format("%d corps", int(Rt().RigidBodyCount())));
	kit::SetLabelText(m_ctx, m_statusScene,
					  scene ? String::Format("%s · %s", Rt().GetProject().name.CStr(), Rt().ActivePath().ToString().CStr())
							: String("(aucune scène)"));
	kit::SetLabelText(m_ctx, m_statusMode, String(Rt().IsPlaying() ? "▶ Jeu" : "■ Édition"));
	kit::SetLabelText(m_ctx, m_statusGizmo, String::Format("%s%s", GizmoModeName(Rt().GetGizmoMode()),
														  Rt().SnapEnabled() ? " · magnétisme" : ""));

	// Outil actif et magnétisme surlignés dans la barre d'outils de la vue —
	// seulement quand ils CHANGENT (re-styler à chaque image relancerait la
	// cascade de styles pour rien).
	// En modélisation, « 2D » est allumé pendant une esquisse.
	const bool shown2d = m_view2d || (m_modeling.IsActive() && m_modeling.IsSketching());
	if (shown2d != m_shownView2d) {
		m_shownView2d = shown2d;
		for (int i = 0; i < 2; ++i)
			kit::SetBackground(m_ctx, m_workspaceButtons[i],
							   (i == 0) == shown2d ? kit::PaletteOf(m_ctx).selection : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	}
	const int mode = int(Rt().GetGizmoMode());
	const bool snap = Rt().SnapEnabled();
	if (mode != m_shownGizmoMode || snap != m_shownSnap) {
		m_shownGizmoMode = mode;
		m_shownSnap = snap;
		kit::SetBackground(m_ctx, m_snapButton, snap ? kit::PaletteOf(m_ctx).selection : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	}
	if (Rt().IsPlaying() != m_shownPlaying) {
		m_shownPlaying = Rt().IsPlaying();
		kit::SetHidden(m_ctx, m_playBadge, !m_shownPlaying);
		if (auto glyph = m_playIcon.Get<ui::IconContent>(); glyph.IsSome())
			glyph.Unwrap()->glyph = ui::Glyphs::ToUtf8(m_shownPlaying ? ui::MaterialIcons::STOP : ui::MaterialIcons::PLAY_ARROW);
	}
	// Le sélecteur de document suit les changements venus d'ailleurs :
	// scènes ou objets ajoutés, renommés, supprimés, document ouvert.
	if (m_sceneComboSlot.Valid() && scene) {
		if (m_documentChoicesDirty && !m_sceneCombo.IsOpen()) {
			m_documentChoicesDirty = false;
			m_sceneComboSlot.Clear();
			m_sceneCombo = m_sceneComboSlot.Append(DocumentCombo());
		}
		int index = 0, current = -1;
		for (const ui::ComboCategory &category : DocumentChoices())
			for (const String &item : category.items) {
				if (item == scene->name)
					current = index;
				++index;
			}
		if (current >= 0 && m_sceneCombo.Selected() != current)
			m_sceneCombo.SetSelected(current);
	}
	m_fpsHistory.push_back(float(m_fps));
	if (m_fpsHistory.size() > FPS_HISTORY_SIZE)
		m_fpsHistory.erase(m_fpsHistory.begin());
}

void EditorUi::SetGizmoMode(Runtime::GizmoMode mode) {
	Rt().SetGizmoMode(mode);
	m_sceneTool = SceneTool::GIZMO;
	m_navTool = NavTool::NONE;
	ResetTape();
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

void EditorUi::AddSoundSource(const String &path) {
	auto created = Rt().AddAudioSource(path);
	if (created.IsError()) {
		Rt().LogError(String::Format("Source audio : %s", created.Error().CStr()));
		return;
	}
	SetStatus(String::Format("Source audio ajoutée : %s", Runtime::FileStem(path).CStr()));
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
	if (Rt().ManifestPath().IsEmpty()) {
		OnSaveProjectAs();
		return;
	}
	auto saved = Rt().SaveProject();
	if (saved.IsError())
		Rt().LogError(String::Format("Enregistrement impossible : %s", saved.Error().CStr()));
	else
		Rt().LogSuccess(String::Format("Projet enregistré : %s", Rt().ManifestPath().CStr()));
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
	const String name = Rt().AddEmptyScene().name;
	Rt().LogSuccess(String::Format("Scène « %s » ajoutée (enregistrez le projet pour l'écrire)", name.CStr()));
	RequestRebuild();
}

String EditorUi::SuggestedPath(const char *sub, const String &fileName) const {
	const String directory = Rt().ProjectDirectory();
	return files::Join(files::Join(directory.IsEmpty() ? m_savesDir : directory, String(sub)), fileName);
}

void EditorUi::OnSaveObjectAs() {
	const SceneDesc *object = Rt().ActiveDocument();
	if (!object || !object->IsObject()) {
		SetStatus(String("Ouvrez d'abord l'objet à enregistrer (onglet Objets)"));
		return;
	}
	m_dialogKey = object->name;
	OpenDialog(DialogKind::SAVE_OBJECT_AS, String("Enregistrer l'objet sous"),
			   String("L'objet ouvert, dans un seul fichier .object."),
			   SuggestedPath(files::OBJECTS_DIR, files::SafeFileName(object->name) + String(".object")),
			   String("Chemin du fichier .object"), String("Enregistrer"));
}

String EditorUi::ActiveScriptKey() {
	// Le script au premier plan dans la zone de documents, sinon le script
	// de jeu de la scène ouverte.
	if (const CodeDocument *doc = m_documents.Active()) {
		if (doc->kind == DocumentKind::LIBRARY_SCRIPT)
			return doc->target;
		if (doc->kind == DocumentKind::SCENE_SCRIPT)
			return String("@") + doc->target;
	}
	return String("@") + ActiveDocumentName();
}

void EditorUi::OnImportObject() {
	OpenDialog(DialogKind::IMPORT_OBJECT, String("Importer un objet"),
			   String("Ajoute au projet l'objet d'un fichier .object."),
			   SuggestedPath(files::OBJECTS_DIR, String()), String("Chemin du fichier .object"),
			   String("Importer"));
}

void EditorUi::OnSaveSceneAs() {
	const SceneDesc *scene = Rt().ActiveDocument();
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
	const scene::Node *node = Rt().SelectedNode();
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

void EditorUi::BuildWindowTitleBar() {
	m_titleBar = ui::TitleBarWidgets{};
	if (!m_window)
		return;
	ui::TitleBarOptions title;
	title.title = u8"Éditeur de jeux 2D/3D";
	title.appIcon = Some(ui::MaterialIcons::VIDEOGAME_ASSET);
	title.icons = m_ctx.gui.GetFonts().HasFamily(ui::Glyphs::FontFamily<ui::MaterialIcons>());
	title.onClose = [this] {
		if (onQuit)
			onQuit();
	};
	m_root.Append(ui::TitleBar(*m_window, std::move(title)).Class("editor-header").Height(32.f));
	m_titleBar = ui::TitleBarWidgets::From(m_root);
}

void EditorUi::AttachWindowChrome() {
	if (!m_window)
		return;
	// Déplacer par la barre de titre, redimensionner par les bords et la
	// poignée de la barre d'état (rebranché à chaque reconstruction).
	ui::StatusBarWidgets status;
	status.root = m_statusBar;
	status.label = m_statusMessageLabel;
	status.grip = m_statusGrip;
	m_chrome.Attach(*m_window, m_titleBar, &status);
}

void EditorUi::BuildStartPage() {
	const ui::Palette &theme = m_ctx.Theme();
	m_root = m_ctx.gui.Mount(ui::Column().Id("editor").Class("editor-base").Gap(0.f).Padding(0.f));
	BuildWindowTitleBar();

	ui::WidgetBuilder bar = ui::MenuBar();
	bar.GrowWidth().Padding(math::Sides{6.f, 2.f}).Class("editor-header");
	ui::Widget barEntity = m_root.Append(bar);
	m_menuPopups.push_back(barEntity.Append(ui::Menu(String("Fichier")).Children({MenuAction("Nouveau projet…", "Ctrl+N", [this] { OnNewProject(); }),
MenuAction("Ouvrir un projet…", "Ctrl+O", [this] { OnOpenProject(); }),
MenuAction("Quitter", "Ctrl+Q", [this] {
			if (onQuit)
				onQuit();
		})})));
	ui::Widget window = barEntity.Append(ui::Menu(String("Fenêtre")));
	m_menuPopups.push_back(window);
	ui::Widget themes = window.Append(ui::SubMenu(String("Thème")));
	m_menuPopups.push_back(themes);
	AddItem(themes, "Atelier (par défaut)", "", [this] { (void)SetTheme("studio"); });
	AddItem(themes, "Sombre", "", [this] { (void)SetTheme("dark"); });
	AddItem(themes, "Clair", "", [this] { (void)SetTheme("light"); });
	AddItem(themes, "Verre", "", [this] { (void)SetTheme("aero"); });

	ui::WidgetBuilder center = ui::Column();
	center.GrowWidth().GrowHeight().Padding(24.f).JustifyContent(ui::Justify::Center).AlignItems(ui::Align::Center);
	ui::Widget centerEntity = m_root.Append(center);

	ui::WidgetBuilder card = ui::Panel();
	card.Width(640.f).Padding(24.f).Gap(12.f);
	ui::Widget cardEntity = centerEntity.Append(card);
	(void)kit::Glyph(m_ctx, cardEntity, ui::MaterialIcons::FOLDER_OPEN, theme.muted, 44.f);
	ui::WidgetBuilder title = ui::Label(String("Aucun projet ouvert"));
	title.FontSize(22.f);
	cardEntity.Append(title);
	ui::WidgetBuilder hint = ui::Label(String::Format(
		"Créez un projet vierge ou ouvrez un projet existant. Chaque projet est un dossier de %s : "
		"son fichier .json, et les sous-dossiers assets, scenes (.scene) et scripts (.script).",
		Rt().projectsRoot.CStr()));
	hint.FontSize(13.f).Color(theme.muted).GrowWidth().TextWrap();
	cardEntity.Append(hint);

	ui::WidgetBuilder actions = ui::Row();
	actions.Gap(8.f).Padding(0.f).GrowWidth();
	ui::Widget actionsEntity = cardEntity.Append(actions);
	for (auto [label, action] : {std::pair<const char *, void (EditorUi::*)()>{"Nouveau projet…", &EditorUi::OnNewProject},
								 std::pair<const char *, void (EditorUi::*)()>{"Ouvrir un projet…", &EditorUi::OnOpenProject}}) {
		ui::WidgetBuilder button = ui::Button(String(label));
		button.Height(32.f);
		button.OnClick([this, action] { (this->*action)(); });
		actionsEntity.Append(button);
	}

	const std::vector<files::ProjectEntry> projects = files::ListProjects(Rt().projectsRoot);
	ui::WidgetBuilder listTitle = ui::Label(
		projects.empty() ? String("Aucun projet dans ce dossier pour l'instant.") : String("Projets existants"));
	listTitle.FontSize(14.f);
	cardEntity.Append(listTitle);
	for (const files::ProjectEntry &project : projects)
		SpawnProjectButton(cardEntity, project);

	ui::WidgetBuilder status = ui::Label(String());
	status.FontSize(12.f).Color(theme.muted).GrowWidth().TextWrap();
	m_statusMessageLabel = cardEntity.Append(status);

	// Barre d'état minimale : la poignée de redimensionnement de la fenêtre.
	m_statusBar = m_statusGrip = ui::Widget();
	if (m_window) {
		ui::StatusBarOptions options;
		options.icons = m_ctx.gui.GetFonts().HasFamily(ui::Glyphs::FontFamily<ui::MaterialIcons>());
		m_root.Append(ui::StatusBar(std::move(options)).Class("editor-header"));
		ui::StatusBarWidgets statusBar = ui::StatusBarWidgets::From(m_root);
		m_statusBar = statusBar.root;
		m_statusGrip = statusBar.grip;
	}
	AttachWindowChrome();
}

void EditorUi::SpawnProjectButton(ui::Widget parent, const files::ProjectEntry &project) {
	ui::WidgetBuilder row = ui::Button(String::Format("%s    —    %s", project.name.CStr(),
																files::RelativeTo(Rt().projectsRoot, project.manifest).CStr()));
	row.GrowWidth().Height(30.f).FontSize(13.f).Tooltip(project.manifest);
	const String manifest = project.manifest;
	row.OnClick([this, manifest] { OpenProjectAt(manifest); });
	parent.Append(row);
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
	const ui::Palette &theme = m_ctx.Theme();

	ui::WidgetBuilder card = ui::Panel();
	ui::Widget cardEntity{};
	card.Width(600.f).Padding(18.f).Gap(10.f).Ref(cardEntity);
	m_dialog = m_ctx.gui.Mount(ui::Modal(std::move(card)));

	ui::WidgetBuilder heading = ui::Label(title);
	heading.FontSize(17.f);
	cardEntity.Append(heading);
	ui::WidgetBuilder note = ui::Label(message);
	note.FontSize(13.f).Color(theme.muted).GrowWidth().TextWrap();
	cardEntity.Append(note);

	if (kind == DialogKind::OPEN_PROJECT)
		for (const files::ProjectEntry &project : files::ListProjects(Rt().projectsRoot))
			SpawnProjectButton(cardEntity, project);

	ui::WidgetBuilder input = ui::Input(placeholder);
	input.GrowWidth().Height(30.f);
	input.OnSubmit([this](const String &) { ConfirmDialog(); });
	m_dialogInput = cardEntity.Append(input);
	SetDialogText(text);

	ui::WidgetBuilder error = ui::Label(String());
	error.FontSize(12.f).Color(kit::PaletteOf(m_ctx).error).GrowWidth().TextWrap();
	m_dialogError = cardEntity.Append(error);

	ui::WidgetBuilder buttons = ui::Row();
	buttons.Gap(8.f).Padding(0.f).GrowWidth();
	ui::Widget buttonsEntity = cardEntity.Append(buttons);
	if (kind != DialogKind::NEW_PROJECT && kind != DialogKind::SAVE_PROJECT_AS) {
		ui::WidgetBuilder browse = ui::Button(String("Parcourir…"));
		browse.Height(30.f).OnClick([this] { BrowseForDialog(); });
		buttonsEntity.Append(browse);
	}
	(void)kit::Spacer(m_ctx, buttonsEntity);
	ui::WidgetBuilder cancel = ui::Button(String("Annuler"));
	cancel.Height(30.f).OnClick([this] { CloseDialog(); });
	buttonsEntity.Append(cancel);
	ui::WidgetBuilder ok = ui::Button(confirm);
	ok.Height(30.f).OnClick([this] { ConfirmDialog(); });
	buttonsEntity.Append(ok);

	m_dialog.Open();
	m_dialogInput.Focus();
}

void EditorUi::CloseDialog() {
	if (!m_dialog.Valid())
		return;
	m_dialog.Close();
	m_dialog.Destroy();
	m_dialog = m_dialogInput = m_dialogError = ui::Widget();
}

void EditorUi::SetDialogText(const String &text) {
	m_dialogInput.SetText(text);
}

String EditorUi::DialogText() const {
	return m_dialogInput.Valid() ? m_dialogInput.Text().Trim() : String();
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
			Rt().LogSuccess(String::Format("Scène « %s » importée", imported.Value().name.CStr()));
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
		case DialogKind::SAVE_OBJECT_AS: {
			const String path = WithExtension(text, ".object");
			auto saved = Rt().SaveObjectAs(ObjectRef{m_dialogKey}, path);
			if (saved.IsError()) {
				ShowDialogError(saved.Error());
				return;
			}
			Rt().LogSuccess(String::Format("Objet enregistré : %s", path.CStr()));
			CloseDialog();
			return;
		}
		case DialogKind::IMPORT_OBJECT: {
			auto imported = Rt().ImportObjectFile(text);
			if (imported.IsError()) {
				ShowDialogError(imported.Error());
				return;
			}
			Rt().LogSuccess(String::Format("Objet « %s » importé", imported.Value().name.CStr()));
			CloseDialog();
			RequestRebuild();
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
		case DialogKind::SAVE_OBJECT_AS:
			sdl3::dialog::ShowSaveFile(deliver, {sdl3::DialogFilter{"Objets", "object"}}, current);
			break;
		case DialogKind::IMPORT_OBJECT:
			sdl3::dialog::ShowOpenFile(deliver, {sdl3::DialogFilter{"Objets", "object"}}, start);
			break;
		case DialogKind::SAVE_SCENE_AS:
			sdl3::dialog::ShowSaveFile(deliver, {sdl3::DialogFilter{"Scènes", "scene"}}, current);
			break;
		case DialogKind::IMPORT_SCENE:
			sdl3::dialog::ShowOpenFile(deliver, {sdl3::DialogFilter{"Scènes", "scene"}}, start);
			break;
		case DialogKind::IMPORT_SCRIPT:
			sdl3::dialog::ShowOpenFile(deliver, {sdl3::DialogFilter{"Scripts Script", "script"}}, start);
			break;
		case DialogKind::SAVE_SCRIPT_AS:
			sdl3::dialog::ShowSaveFile(deliver, {sdl3::DialogFilter{"Scripts Script", "script"}}, current);
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
	SceneDesc *scene = Rt().ActiveDocument();
	scene::Node *object = Rt().SelectedNode();
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
	const scene::Node *node = Rt().FindNode(id);
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
	// Syntaxe ET règles du moteur (une classe Scene par script de scène, au
	// plus une Behaviour par script de la bibliothèque) : tout ce qui
	// échouerait au lancement du mode Jeu est signalé ici.
	auto check = [&](const String &label, const String &source, ScriptUse use) {
		const ScriptOutline outline = Rt().OutlineScript(source, use);
		if (outline.error.IsSome()) {
			++broken;
			Rt().LogError(String::Format("%s : %s", label.CStr(), outline.error.Value().Format().CStr()));
		} else if (outline.role == ScriptRole::INVALID) {
			++broken;
			Rt().LogError(String::Format("%s : %s", label.CStr(), outline.problem.CStr()));
		} else {
			++ok;
			Rt().LogInfo(String::Format("%s : %s", label.CStr(), outline.Summary().CStr()));
		}
	};
	for (const ScriptAsset &script : project.scripts)
		check(script.name + String(".script"), script.source, ScriptUse::LIBRARY);
	for (const String &sceneName : project.Fs().ListGameplayScripts())
		if (const SceneDesc *scene = project.Fs().FindScene(SceneRef{sceneName}); scene)
			check(scene->name + String(".main.script"), scene->gameplayScript, ScriptUse::SCENE);
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
	Rt().LogInfo(String("sandbox — éditeur de jeu 3D bâti sur le wrapper SDL3/C++23 "
						"(ui::, scene::, ecs::, render3d::, physics::, script)"));
	(void)FocusPanel("console", 0);
}

bool EditorUi::TextFieldHasFocus() const {
	ui::Widget focus = m_ctx.gui.Focused();
	if (!focus.Valid())
		return false;
	auto editor = focus.Get<ui::TextEditor>();
	return editor.IsSome() && editor.Unwrap()->mode != ui::IOMode::ReadOnly && editor.Unwrap()->mode != ui::IOMode::ReadAndCopy;
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

} // namespace sandbox
