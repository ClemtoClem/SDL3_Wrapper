// Définitions de voxelia/studio/studio.hpp
#include <voxelia/studio/studio.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <voxel/components.hpp>
#include <voxel/physics.hpp>
#include <voxelia/session.hpp>
#include <voxelia/shell.hpp>

namespace voxelia::studio {

namespace {

constexpr float OUTLINER_PERIOD = 0.2f; ///< 5 Hz
constexpr float PITCH_LIMIT = 1.5f;
constexpr float LOOK_SPEED = 0.004f;
constexpr float FLY_SPEED = 12.f;

/// Feuille du studio : propre à SA fenêtre (la fenêtre de jeu garde la sienne).
constexpr const char *STUDIO_CSS = R"css(
#studio { color: var(--text); }
.studio-viewbar { padding: 3px 6px; gap: 4px; align-items: center; height: 34px; }
.studio-tool { height: 26px; font-size: 12px; padding: 0 8px; }
.studio-status { color: var(--muted); font-size: 12px; }
.studio-group { color: #e0b45a; font-size: 12px; padding-top: 6px; }
.studio-row { padding: 2px 6px; font-size: 12px; border-radius: 3px; }
.studio-row:hover { background-color: var(--hover); }
.studio-row.selected { background-color: var(--ed-selection); }
.studio-dead { color: #c96565; }
.studio-section { color: #e0b45a; font-size: 12px; font-weight: bold; padding-top: 8px; }
.studio-key { color: var(--muted); font-size: 12px; width: 130px; }
.studio-value { font-size: 12px; }
.studio-edit { font-weight: bold; }
.studio-element { padding: 4px 6px; gap: 8px; align-items: center; border-radius: 4px; background-color: transparent; }
.studio-element:hover { background-color: var(--hover); }
.studio-wizard { width: 420px; padding: 14px; gap: 6px; background-color: var(--panel); border-radius: 6px; }
.studio-wizard-title { font-size: 16px; font-weight: bold; padding-bottom: 4px; }
.studio-console-filters { padding: 0 0 2px 0; align-items: center; }
.studio-filter { height: 22px; font-size: 11px; padding: 0 6px; }
.studio-log-ok { color: #8fd16a; font-size: 12px; }
.studio-log-error { color: #ff6b6b; font-size: 12px; }
.studio-log-warning { color: #f0c14b; font-size: 12px; }
.studio-log-confirm { color: #f0c14b; font-size: 12px; }
.studio-log-pending { color: #7fb6ff; font-size: 12px; }
.studio-log-input { color: var(--text); font-size: 12px; }
.studio-log-info { color: var(--muted); font-size: 12px; }
.studio-log-bookmark { background-color: var(--ed-selection); }
)css";

math::FVector3 Add(const math::FVector3 &a, const math::FVector3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
math::FVector3 Scale(const math::FVector3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }

Option<float> ParseFloat(const String &s) {
	char *end = nullptr;
	const float v = std::strtof(s.CStr(), &end);
	if (!end || end == s.CStr() || *end != '\0')
		return NONE;
	return Some(v);
}

} // namespace

// ── Caméra ───────────────────────────────────────────────────────────────────

math::FVector3 EditorCamera::Forward() const noexcept {
	const float c = std::cos(pitch);
	return {std::sin(yaw) * c, std::sin(pitch), std::cos(yaw) * c};
}

render3d::Camera EditorCamera::ToCamera(float aspect, float farPlane) const {
	render3d::Camera camera;
	camera.position = position;
	camera.target = Add(position, Forward());
	camera.aspect = aspect > 0.f ? aspect : 16.f / 9.f;
	camera.farPlane = farPlane;
	return camera;
}

void EditorCamera::Look(float dyaw, float dpitch) noexcept {
	yaw += dyaw;
	pitch = std::clamp(pitch + dpitch, -PITCH_LIMIT, PITCH_LIMIT);
}

void EditorCamera::LookAt(const math::FVector3 &target) noexcept {
	const float dx = target.x - position.x, dy = target.y - position.y, dz = target.z - position.z;
	const float flat = std::sqrt(dx * dx + dz * dz);
	if (flat < 1e-4f && std::fabs(dy) < 1e-4f)
		return;
	yaw = std::atan2(dx, dz);
	pitch = std::clamp(std::atan2(dy, flat), -PITCH_LIMIT, PITCH_LIMIT);
}

// ── Studio ───────────────────────────────────────────────────────────────────

Studio::Studio(Shell &shell, ui::WindowContext &window, StudioReport *report, String viewportTransfer)
	: m_shell(shell), m_window(window), m_report(report), m_transfer(std::move(viewportTransfer)), m_leftDock(window.Gui()),
	  m_centerDock(window.Gui()), m_catalogDock(window.Gui()), m_consoleDock(window.Gui()), m_rightDock(window.Gui()) {
	m_blocks = std::make_unique<BlockEditor>(m_shell, m_report, [this] {
		// Le bloc visé : 4 blocs devant la caméra de l'éditeur.
		return voxel::BlockPos::FromWorld(Add(m_camera.position, Scale(m_camera.Forward(), 4.f)));
	});
	m_models = std::make_unique<ModelEditor>(m_shell, m_report);
	m_structures = std::make_unique<StructureEditor>(m_shell, m_report, [this] {
		return voxel::BlockPos::FromWorld(Add(m_camera.position, Scale(m_camera.Forward(), 4.f)));
	});
	m_textures = std::make_unique<TextureEditor>(m_shell, m_report);
	m_worldgen = std::make_unique<WorldgenEditor>(m_shell, m_report);
	m_entities = std::make_unique<EntityEditor>(m_shell, m_report, [this] {
		// Arène : 12 blocs devant la caméra (à plat), son sol 2 blocs plus bas.
		const math::FVector3 f = m_camera.Forward();
		const float flat = std::max(1e-3f, std::sqrt(f.x * f.x + f.z * f.z));
		const math::FVector3 at = Add(m_camera.position, {f.x / flat * 12.f, -2.f, f.z / flat * 12.f});
		return voxel::BlockPos::FromWorld(at);
	});
	RegisterCommands();
	m_models->RegisterCommands(m_console);
	m_structures->RegisterCommands(m_console);
	m_textures->RegisterCommands(m_console);
	m_entities->RegisterCommands(m_console);
	m_worldgen->RegisterCommands(m_console);
	Build();
	m_console.Print("Voxelia Studio — « help » liste les commandes ; toute autre ligne est une commande du jeu.");
	Visit("studio.open", FeatureStatus::Ok,
		  String(m_window.SharesGpuDevice() ? "device GPU partagé avec la fenêtre de jeu" : "device GPU NON partagé"));
}

Studio::~Studio() {
	if (m_flying && m_window.Window())
		(void)m_window.Window()->SetRelativeMouseMode(false);
	m_window.Root().Clear();
}

voxel::Game *Studio::Game() {
	Session *session = m_shell.Game();
	return session && session->Ready() ? &session->Game() : nullptr;
}

void Studio::Visit(const char *feature, FeatureStatus status, String detail) {
	if (m_report)
		m_report->Visit(String(feature), status, std::move(detail));
}

void Studio::Build() {
	ui::Ui &gui = m_window.Gui();
	gui.AddStyleSheet(editor::EditorCss(gui.CurrentPalette()).View());
	gui.AddStyleSheet(StringView(STUDIO_CSS));
	// Cadre : barre de titre et barre d'état personnalisées (fenêtre sans
	// décoration) ; sans fenêtre SDL (tests), une simple colonne.
	if (sdl3::Window *window = m_window.Window()) {
		ui::WindowFrameOptions options;
		options.title = "Voxelia Studio";
		options.appIcon = Some(ui::MaterialIcons::VIEW_IN_AR);
		options.status = "prêt";
		options.onClose = [this] { m_window.RequestClose(); };
		m_frame.Build(gui, *window, std::move(options));
		m_root = m_frame.Content().Append(ui::Column().Id("studio").Class("editor-base").Gap(0.f).Padding(0.f).Grow());
	} else {
		m_root = gui.Mount(ui::Column().Id("studio").Class("editor-base").Gap(0.f).Padding(0.f).Fill());
	}
	BuildMenus(m_root);
	const ui::Palette &theme = gui.CurrentPalette();

	ui::Widget body = m_root.Append(ui::Row().Gap(0.f).Padding(math::Sides{4.f, 2.f, 4.f, 2.f}).GrowWidth().GrowHeight());
	ui::Widget main = body.Append(ui::Column().Gap(0.f).Padding(0.f).GrowWidth().GrowHeight());
	ui::Widget top = main.Append(ui::Row().Gap(0.f).Padding(0.f).GrowWidth().GrowHeight());

	// Gauche : World Outliner, Éléments.
	m_leftDock.Build(top, [](ui::WidgetBuilder &b) { b.Width(270.f).GrowHeight(); });
	BuildOutliner(m_leftDock.AddPage(String("Outliner"), false));
	BuildElements(m_leftDock.AddPage(String("Éléments"), true));
	m_leftDock.SetActive(0);
	(void)editor::ResizeHandle(theme, top, ui::Orientation::Horizontal, m_leftDock.Frame(), true, 180.f, 600.f);

	// Centre : la vue 3D.
	m_centerDock.Build(top, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });
	BuildViewport(m_centerDock.AddPage(String("Vue 3D"), false, false, 0.f));

	// Bas : catalogues | console.
	ui::Widget bottom = main.Append(ui::Row().Gap(0.f).Padding(0.f).GrowWidth().Height(230.f));
	(void)editor::ResizeHandle(theme, main, ui::Orientation::Vertical, bottom, false, 90.f, 700.f);
	m_catalogDock.Build(bottom, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });
	ui::Widget catalogs[6];
	const char *catalogTitles[6] = {"Blocs", "Modèles", "Structures", "Textures", "Entités", "Monde"};
	const char *catalogIds[6] = {"studio-block-catalog", "studio-model-catalog", "studio-structure-catalog", "studio-texture-catalog",
								 "studio-entity-catalog", "studio-world-catalog"};
	for (int i = 0; i < 6; ++i)
		catalogs[i] = m_catalogDock.AddPage(String(catalogTitles[i]), false, false, 4.f).Append(ui::Column().Id(catalogIds[i]).Fill());
	m_catalogDock.SetActive(0);
	ui::Widget consoleSlot = bottom.Append(ui::Column().Gap(0.f).Padding(0.f).Width(470.f).GrowHeight());
	(void)editor::ResizeHandle(theme, bottom, ui::Orientation::Horizontal, consoleSlot, false, 220.f, 1000.f);
	m_consoleDock.Build(consoleSlot, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });
	BuildConsole(m_consoleDock.AddPage(String("Console"), false, false, 4.f));

	// Droite : inspecteur et éditeurs.
	ui::Widget right = body.Append(ui::Column().Gap(0.f).Padding(0.f).Width(400.f).GrowHeight());
	(void)editor::ResizeHandle(theme, body, ui::Orientation::Horizontal, right, false, 260.f, 900.f);
	m_rightDock.Build(right, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });
	BuildInspector(m_rightDock.AddPage(String("Inspecteur"), false));
	ui::Widget editors[6];
	const char *editorTitles[6] = {"Bloc", "Modèle", "Structure", "Texture", "Entité", "Monde"};
	const char *editorIds[6] = {"studio-block-editor", "studio-model-editor", "studio-structure-editor", "studio-texture-editor",
								"studio-entity-editor", "studio-world-editor"};
	for (int i = 0; i < 6; ++i)
		editors[i] = m_rightDock.AddPage(String(editorTitles[i]), false).Append(ui::Column().Id(editorIds[i]).Fill());
	m_rightDock.SetActive(0);

	if (!m_frame.Root().Valid()) // sans cadre : la ligne d'état en bas
		m_stats = m_root.Append(ui::Label("").Id("studio-stats").Class("studio-status"));
	else
		m_stats = m_root.Append(ui::Label("").Id("studio-stats").Class("studio-status").Padding(math::Sides{2.f, 8.f}));
	m_stats.Hide();

	m_blocks->BuildCatalog(catalogs[0]);
	m_blocks->Build(editors[0]);
	m_blocks->onDocumentOpened = [this] { ShowRight(1); };
	m_models->BuildCatalog(catalogs[1]);
	m_models->Build(editors[1]);
	m_models->onDocumentOpened = [this] { ShowRight(2); };
	m_structures->BuildCatalog(catalogs[2]);
	m_structures->Build(editors[2]);
	m_structures->onDocumentOpened = [this] { ShowRight(3); };
	m_textures->BuildCatalog(catalogs[3]);
	m_textures->Build(editors[3]);
	m_textures->onDocumentOpened = [this] { ShowRight(4); };
	m_entities->BuildCatalog(catalogs[4]);
	m_entities->Build(editors[4]);
	m_entities->onDocumentOpened = [this] { ShowRight(5); };
	m_worldgen->BuildCatalog(catalogs[5]);
	m_worldgen->Build(editors[5]);
	m_worldgen->onDocumentOpened = [this] { ShowRight(6); };
	m_entities->onPreviewSpawned = [this](ecs::Entity e) {
		// Inspection en direct (outliner / inspecteur) et caméra qui la suit.
		Select(e);
		FocusOn(e);
		m_follow = true;
	};
	if (auto vp = m_viewport.Get<ui::Viewport3DState>(); vp.IsSome()) {
		vp.Unwrap()->transfer = m_transfer == "readback"	? ui::ViewportTransfer::READBACK
								: m_transfer == "pipelined" ? ui::ViewportTransfer::PIPELINED
															: ui::ViewportTransfer::DIRECT;
		// Vue secondaire : 60 images 3D par seconde suffisent ; la fenêtre de
		// jeu garde toute la cadence de l'écran.
		vp.Unwrap()->maxFps = 60.f;
	}
	BuildPalette();
	RebuildOutliner();
	RefreshElements();
}

void Studio::ShowRight(int index) {
	m_rightDock.SetActive(index);
	if (index >= 1 && index <= 6)
		m_catalogDock.SetActive(index - 1);
}

void Studio::BuildMenus(ui::Widget parent) {
	auto item = [this](const char *text, const char *shortcut, String command) {
		ui::WidgetBuilder b = ui::MenuItem(String(text), String(shortcut));
		b.OnClick([this, command] { (void)Execute(command); });
		return b;
	};
	auto action = [](const char *text, const char *shortcut, std::function<void()> fn) {
		ui::WidgetBuilder b = ui::MenuItem(String(text), String(shortcut));
		b.OnClick(std::move(fn));
		return b;
	};
	ui::Widget bar = parent.Append(ui::MenuBar().GrowWidth().Padding(math::Sides{6.f, 2.f}).Class("editor-header").Id("studio-menubar"));
	bar.Append(ui::Menu(String("Fichier")).Children({
		action("Nouveau bloc…", "", [this] { OpenNewElement(0); }),
		action("Nouveau modèle…", "", [this] { OpenNewElement(1); }),
		action("Nouvelle structure…", "", [this] { OpenNewElement(2); }),
		action("Nouvelle texture…", "", [this] { OpenNewElement(3); }),
		action("Nouvelle entité…", "", [this] { OpenNewElement(4); }),
		action("Nouveau biome…", "", [this] { OpenNewElement(5); }),
		action("Nouveau générateur…", "", [this] { OpenNewElement(6); }),
		action("Nouvelle dimension…", "", [this] { OpenNewElement(7); }),
		ui::MenuSeparator(),
		item("Enregistrer le document", "Ctrl+S", String("studio.save")),
		item("Appliquer à chaud", "Ctrl+Maj+R", String("studio.apply")),
		ui::MenuSeparator(),
		action("Fermer le studio", "", [this] { m_window.RequestClose(); }),
	}));
	bar.Append(ui::Menu(String("Édition")).Children({
		item("Annuler", "Ctrl+Z", String("studio.undo")),
		item("Rétablir", "Ctrl+Y", String("studio.redo")),
		ui::MenuSeparator(),
		item("Désélectionner", "Échap", String("studio.deselect")),
		item("Déplacer (gizmo)", "W", String("studio.gizmo translate")),
		item("Tourner (gizmo)", "E", String("studio.gizmo rotate")),
		item("Mettre à l'échelle (gizmo)", "R", String("studio.gizmo scale")),
	}));
	bar.Append(ui::Menu(String("Affichage")).Children({
		item("Vue de face", "", String("studio.view front")),
		item("Vue de droite", "", String("studio.view right")),
		item("Vue de dessus", "", String("studio.view top")),
		item("Vue isométrique", "", String("studio.view iso")),
		item("Cadrer la sélection", "F", String("studio.focus")),
		item("Caméra au joueur", "", String("studio.camera player")),
		ui::MenuSeparator(),
		action("World Outliner", "", [this] { m_leftDock.SetActive(0); }),
		action("Éléments", "", [this] { m_leftDock.SetActive(1); }),
		action("Inspecteur", "", [this] { m_rightDock.SetActive(0); }),
		item("Statistiques détaillées", "F3", String("studio.stats")),
	}));
	bar.Append(ui::Menu(String("Simulation")).Children({
		item("Suspendre", "", String("studio.pause")),
		item("Reprendre", "", String("studio.play")),
		item("Avancer d'un pas", "", String("studio.step")),
		item("Suivre la sélection", "", String("studio.follow")),
	}));
	bar.Append(ui::Menu(String("Outils")).Children({
		action("Palette de commandes…", "F1", [this] { m_paletteView->Open(); }),
		item("Démarrer le diagnostic", "", String("studio.diag.start")),
		item("Arrêter le diagnostic et exporter (.vcap)", "", String("studio.diag.stop")),
		ui::MenuSeparator(),
		item("Recharger le bloc ouvert", "", String("reload")),
		item("Reconstruire l'atlas", "", String("studio.texture.atlas")),
		item("Aperçu du monde", "", String("studio.world.preview")),
		item("Chaîne des dimensions", "", String("studio.world.graph")),
	}));
	bar.Append(ui::Menu(String("Aide")).Children({
		item("Commandes de la console", "", String("help")),
	}));
}

void Studio::BuildViewport(ui::Widget page) {
	const ui::Palette &theme = m_window.Gui().CurrentPalette();
	ui::Widget bar = page.Append(ui::Row().Class("editor-toolbar").Class("studio-viewbar").GrowWidth());
	auto button = [&](ui::MaterialIcons icon, const char *tooltip, const char *command, ui::Widget *out = nullptr) {
		const String line(command);
		ui::Widget b = editor::IconButton(theme, bar, icon, String(tooltip), [this, line] { (void)Execute(line); }, 26.f);
		if (out)
			*out = b;
		return b;
	};
	m_pauseButton = bar.Append(ui::Button("Pause").Id("studio-pause").Class("studio-tool").Tooltip("Suspendre / reprendre la simulation").OnClick([this] {
		(void)Execute(m_shell.SimulationHeld() ? String("studio.play") : String("studio.pause"));
	}));
	bar.Append(ui::Button("Pas").Id("studio-step").Class("studio-tool").Tooltip("Avancer d'un pas de simulation").OnClick([this] {
		(void)Execute(String("studio.step"));
	}));
	m_followButton = bar.Append(ui::Button("Suivre").Id("studio-follow").Class("studio-tool").Tooltip("Caméra poursuite de la sélection").OnClick([this] {
		(void)Execute(String("studio.follow"));
	}));
	bar.Append(ui::Button("Au joueur").Id("studio-to-player").Class("studio-tool").OnClick([this] { (void)Execute(String("studio.camera player")); }));
	m_diagnosticButton = bar.Append(ui::Button("● Diag.")
										 .Id("studio-diagnostic")
										 .Class("studio-tool")
										 .Tooltip("Démarrer / arrêter la capture de performances (exportée en .vcap pour le profiler)")
										 .OnClick([this] { (void)Execute(String("studio.diag")); }));
	bar.Append(ui::Separator(ui::Orientation::Vertical).Height(20.f));
	button(ui::MaterialIcons::OPEN_WITH, "Déplacer (W)", "studio.gizmo translate", &m_gizmoButtons[0]);
	button(ui::MaterialIcons::ROTATE_RIGHT, "Tourner (E)", "studio.gizmo rotate", &m_gizmoButtons[1]);
	button(ui::MaterialIcons::OPEN_IN_FULL, "Mettre à l'échelle (R)", "studio.gizmo scale", &m_gizmoButtons[2]);
	button(ui::MaterialIcons::GRID_ON, "Accroche à la grille (1 bloc, 15°)", "studio.gizmo snap");
	bar.Append(ui::Separator(ui::Orientation::Vertical).Height(20.f));
	button(ui::MaterialIcons::CROP_FREE, "Cadrer la sélection (F)", "studio.focus");
	bar.Append(ui::Button("Face").Class("studio-tool").OnClick([this] { (void)Execute(String("studio.view front")); }));
	bar.Append(ui::Button("Droite").Class("studio-tool").OnClick([this] { (void)Execute(String("studio.view right")); }));
	bar.Append(ui::Button("Dessus").Class("studio-tool").OnClick([this] { (void)Execute(String("studio.view top")); }));
	bar.Append(ui::Button("Iso").Class("studio-tool").OnClick([this] { (void)Execute(String("studio.view iso")); }));
	bar.Append(ui::Spacer());
	button(ui::MaterialIcons::SEARCH, "Palette de commandes (F1)", "studio.palette");

	// La vue est une surface de l'application : clics, glissés et molette
	// vont au studio (sélection, gizmo, navigation), pas à l'interface.
	m_viewport = page.Append(ui::Viewport3D(m_emptyScene).Id("studio-viewport").AppSurface().Grow());
	m_viewport.Append(ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &r) { DrawOverlay(painter, r); })
						  .Absolute()
						  .Anchor(ui::Anchor::TopLeft)
						  .Width(ui::Length::Pct(100.f))
						  .Height(ui::Length::Pct(100.f))
						  .PointerThrough());
}

void Studio::BuildOutliner(ui::Widget page) {
	ui::Widget filter;
	page.Append(ui::Input("Filtrer (nom, id)…").Id("outliner-filter").Ref(filter).GrowWidth().Height(26.f).OnTextChange([this](const String &text) {
		m_filter.text = text;
		m_outlinerTimer = OUTLINER_PERIOD; // rafraîchi à la prochaine image
		Visit("outliner.filter");
	}));
	std::vector<String> kinds = {String("Tous les genres")};
	for (EntityCategory c : {EntityCategory::Player, EntityCategory::Creature, EntityCategory::Item, EntityCategory::FallingBlock,
							 EntityCategory::Carrier, EntityCategory::Other})
		kinds.push_back(String(EntityCategoryLabel(c)));
	page.Append(ui::Row().Gap(4.f).GrowWidth().Children({
		ui::Combo(kinds, 0).Id("outliner-category").Grow().OnSelect([this](int index) {
			m_filter.category = index <= 0 ? Option<EntityCategory>(NONE) : Some(EntityCategory(index - 1));
			m_outlinerTimer = OUTLINER_PERIOD;
			Visit("outliner.filter_category");
		}),
		ui::Checkbox("Vivants", false).Id("outliner-alive").OnToggle([this](bool on) {
			m_filter.aliveOnly = on;
			m_outlinerTimer = OUTLINER_PERIOD;
		}),
	}));
	page.Append(ui::Row().Gap(4.f).GrowWidth().Children({
		ui::Label("Distance max").Class("prop-label").Width(90.f),
		ui::Slider(0.f, 256.f, 0.f, 8.f).Id("outliner-distance").Grow().Tooltip("0 : sans limite").OnChange([this](float v) {
			m_filter.maxDistance = v;
			m_outlinerTimer = OUTLINER_PERIOD;
		}),
	}));
	m_outlinerCount = page.Append(ui::Label("").Class("studio-status"));
	m_outlinerList = page.Append(ui::ScrollView().Id("outliner-list").GrowWidth().Height(10.f).Grow());
}

void Studio::BuildElements(ui::Widget page) {
	page.Append(ui::Label("Contenu de la partie, par genre (comme les « mod elements » de MCreator) : "
						  "ouvrir le catalogue, ou créer un élément.")
					.Class("caption")
					.TextWrap()
					.GrowWidth());
	m_elementList = page.Append(ui::Column().Id("studio-elements").Gap(2.f).GrowWidth());
	m_wizard = m_root.Append(ui::Modal(ui::Column().Class("studio-wizard").Children({
		ui::Label("Nouvel élément").Class("studio-wizard-title").Ref(m_wizardStatus),
		ui::Label("Identifiant").Class("prop-label"),
		ui::Input("mod:nom").Id("wizard-id").GrowWidth().Ref(m_wizardId),
		ui::Label("Mod").Class("prop-label"),
		ui::Input("core").Id("wizard-mod").GrowWidth().Ref(m_wizardMod),
		ui::Row().Gap(6.f).Children({
			ui::Spacer(),
			ui::Button("Annuler").OnClick([this] { m_wizard.Close(); }),
			ui::Button("Créer").Id("wizard-create").OnClick([this] { (void)Execute(String::Format("studio.element.new %d \"%s\" \"%s\"", m_wizardKind,
																									m_wizardId.Text().CStr(), m_wizardMod.Text().CStr())); }),
		}),
	})).Id("studio-wizard"));
}

void Studio::BuildConsole(ui::Widget page) {
	ui::Widget filters = page.Append(ui::Row().Gap(2.f).GrowWidth().Class("studio-console-filters"));
	const std::pair<const char *, std::vector<String>> choices[] = {
		{"Tout", {}},
		{"Erreurs", {String("error")}},
		{"Avertissements", {String("warning"), String("confirm")}},
		{"Résultats", {String("ok"), String("pending")}},
		{"Saisies", {String("input")}},
	};
	for (const auto &[label, kinds] : choices) {
		const std::vector<String> k = kinds;
		filters.Append(ui::Button(String(label)).Class("studio-filter").OnClick([this, k] {
			m_consoleKinds = k;
			m_consoleDirty = true;
			Visit("console.filter");
		}));
	}
	filters.Append(ui::Spacer());
	filters.Append(ui::Label("clic : marque-page").Class("caption"));
	m_consoleLog = page.Append(ui::ScrollView().Id("console-log").GrowWidth().Height(10.f).Grow());
	m_consoleBar = page.Append(ui::Row().GrowWidth().Children({
		ui::Input("commande (help) — Tab : compléter").Id("console-input").Grow().Ref(m_consoleInput)
			.OnSubmit([this](const String &line) {
				(void)Execute(line);
				m_consoleInput.SetText(String());
			})
			.OnKey(sdl3::keyboard::Key::UP, [this] { m_consoleInput.SetText(m_console.HistoryStep(-1)); })
			.OnKey(sdl3::keyboard::Key::DOWN, [this] { m_consoleInput.SetText(m_console.HistoryStep(1)); })
			.OnKey(sdl3::keyboard::Key::TAB, [this] {
				// Complétion du dernier mot : préfixe commun des candidats.
				const String text = m_consoleInput.Text();
				const std::vector<String> found = m_console.Complete(text);
				if (found.empty())
					return;
				String common = found[0];
				for (const String &f : found)
					while (!f.StartsWith(common))
						common = common.Substr(0, common.Length() - 1);
				const int space = text.LastIndexOf(' ');
				const String head = space < 0 ? String() : text.Substr(0, size_t(space) + 1);
				m_consoleInput.SetText(head + (found.size() == 1 ? found[0] + String(" ") : common));
				if (found.size() > 1) {
					String all;
					for (size_t i = 0; i < found.size() && i < 12; ++i)
						all += (i ? String("  ") : String()) + found[i];
					m_console.Print(all, String("info"));
				}
			}),
	}));
}

void Studio::BuildInspector(ui::Widget page) {
	page.Append(ui::Row().Gap(4.f).Children({
		ui::Button("Soigner").Id("inspector-heal").OnClick([this] { (void)Execute(String("studio.heal")); }),
		ui::Button("Tuer").Id("inspector-kill").OnClick([this] { (void)Execute(String("studio.kill")); }),
		ui::Button("Voir").Id("inspector-focus").OnClick([this] { (void)Execute(String("studio.focus")); }),
		ui::Button("Utiliser").Id("inspector-use").Tooltip("Clic droit sur le bloc, comme en jeu").OnClick([this] {
			(void)Execute(String("studio.inspect.block use"));
		}),
		ui::Button("Basculer").Id("inspector-toggle").OnClick([this] { (void)Execute(String("studio.inspect.block toggle")); }),
	}));
	page.Append(ui::Label("Valeurs en gras modifiables : Entrée applique.").Class("caption"));
	m_inspector = page.Append(ui::ScrollView().Id("inspector").GrowWidth().Height(10.f).Grow());
}

void Studio::BuildPalette() {
	m_paletteView = std::make_unique<editor::CommandPaletteView>(m_window.Gui(), m_palette);
	m_paletteView->Build(m_root);
	m_palette.Clear();
	for (const auto &[name, help] : m_console.Commands()) {
		// La commande nue s'exécute ; avec arguments, elle part dans la console.
		const bool bare = help.Find(String("<")) == String::NPOS;
		const String command = name;
		m_palette.Add({name, name, help, String(), [this, command, bare] {
						   if (bare) {
							   (void)Execute(command);
						   } else {
							   m_consoleInput.SetText(command + String(" "));
							   m_consoleInput.Focus();
						   }
					   }});
	}
}

void Studio::RefreshElements() {
	voxel::Game *game = Game();
	struct Kind {
		const char *label;
		ui::MaterialIcons icon;
		int catalog;
		size_t count;
	};
	std::vector<Kind> kinds = {
		{"Blocs", ui::MaterialIcons::VIEW_IN_AR, 0, game ? game->Content().BlockCount() : 0},
		{"Modèles articulés", ui::MaterialIcons::ACCESSIBILITY, 1, game ? game->Content().Models().size() : 0},
		{"Structures", ui::MaterialIcons::DOMAIN, 2, 0},
		{"Textures", ui::MaterialIcons::BRUSH, 3, game ? game->Content().Textures().size() : 0},
		{"Entités", ui::MaterialIcons::PETS, 4, game ? game->Content().Entities().size() : 0},
		{"Biomes", ui::MaterialIcons::FOREST, 5, game ? game->Content().Biomes().size() : 0},
		{"Générateurs", ui::MaterialIcons::LANDSCAPE, 5, game ? game->Content().Generators().size() : 0},
		{"Dimensions", ui::MaterialIcons::PUBLIC, 5, game ? game->Content().Dimensions().size() : 0},
	};
	String signature;
	for (const Kind &k : kinds)
		signature += String::Format("%zu;", k.count);
	if (signature == m_elementSignature || !m_elementList.Valid())
		return;
	m_elementSignature = signature;
	m_elementList.Clear();
	const ui::Palette &theme = m_window.Gui().CurrentPalette();
	for (size_t i = 0; i < kinds.size(); ++i) {
		const Kind k = kinds[i];
		const int kind = int(i);
		ui::Widget row = m_elementList.Append(ui::Row().Class("studio-element").GrowWidth().Pressable().OnClick([this, k] {
			m_catalogDock.SetActive(k.catalog);
			Visit("elements.open");
		}));
		row.Append(ui::Icon(k.icon, 18.f).Color(theme.accent).Size(22.f, 22.f).PointerThrough());
		row.Append(ui::Label(String(k.label)).Grow().PointerThrough());
		row.Append(ui::Label(k.count ? String::Format("%zu", k.count) : String("—")).Class("caption").PointerThrough());
		(void)editor::IconButton(theme, row, ui::MaterialIcons::ADD, String("Nouveau…"), [this, kind] { OpenNewElement(kind); }, 22.f);
	}
}

void Studio::OpenNewElement(int kind) {
	static const char *titles[8] = {"Nouveau bloc", "Nouveau modèle articulé", "Nouvelle structure", "Nouvelle texture",
									"Nouvelle entité", "Nouveau biome", "Nouveau générateur", "Nouvelle dimension"};
	m_wizardKind = std::clamp(kind, 0, 7);
	m_wizardStatus.SetText(String(titles[m_wizardKind]));
	m_wizardId.SetText(String());
	m_wizard.Open();
	m_wizardId.Focus();
	Visit("elements.wizard");
}

void Studio::RegisterCommands() {
	m_console.Register("studio.select", "studio.select player | <entité|nom> — sélectionne une entité", [this](const auto &args) {
		if (args.size() < 2)
			return CommandResult::Error(String("usage : studio.select <entité|nom>"));
		voxel::Game *game = Game();
		if (!game)
			return CommandResult::Error(String("aucune partie en cours"));
		if (args[1] == "player") {
			Select(m_shell.Game()->Player());
			return CommandResult::Ok(String("sélection : joueur"));
		}
		const std::vector<OutlinerEntry> all = CollectEntities(*game, {}, m_camera.position);
		const Option<float> number = ParseFloat(args[1]);
		for (const OutlinerEntry &e : all)
			if ((number.IsSome() && e.entity.id == uint32_t(number.Value())) || e.name == args[1] || e.id == args[1]) {
				Select(e.entity);
				return CommandResult::Ok(String("sélection : ") + e.name);
			}
		return CommandResult::Error(String("entité introuvable : ") + args[1]);
	});
	m_console.Register("studio.deselect", "studio.deselect — vide la sélection", [this](const auto &) {
		Select(ecs::Entity{});
		return CommandResult::Ok();
	});
	m_console.Register("studio.focus", "studio.focus — caméra sur la sélection (entité ou bloc)", [this](const auto &) {
		if (m_selected.Valid()) {
			FocusOn(m_selected);
			return CommandResult::Ok();
		}
		if (m_selectedBlock.IsNone())
			return CommandResult::Error(String("aucune sélection"));
		const voxel::BlockPos b = m_selectedBlock.Value();
		const math::FVector3 at{float(b.x) + 0.5f, float(b.y) + 0.5f, float(b.z) + 0.5f};
		m_follow = false;
		m_camera.position = Add(at, {-3.f, 2.5f, -3.f});
		m_camera.LookAt(at);
		m_cameraPlaced = true;
		Visit("camera.focus");
		return CommandResult::Ok();
	});
	m_console.Register("studio.follow", "studio.follow — poursuite de la sélection (bascule)", [this](const auto &) {
		if (!m_follow && !m_selected.Valid())
			return CommandResult::Error(String("aucune sélection à suivre"));
		m_follow = !m_follow;
		Visit("camera.follow");
		return CommandResult::Ok(String(m_follow ? "poursuite" : "caméra libre"));
	});
	m_console.Register("studio.camera", "studio.camera player | <x> <y> <z> [<vers x> <y> <z>] — place la caméra", [this](const auto &args) {
		voxel::Game *game = Game();
		if (args.size() == 2 && args[1] == "player") {
			if (!game)
				return CommandResult::Error(String("aucune partie en cours"));
			m_follow = false;
			FocusOn(m_shell.Game()->Player());
			return CommandResult::Ok();
		}
		if (args.size() != 4 && args.size() != 7)
			return CommandResult::Error(String("usage : studio.camera player | <x> <y> <z> [<vers x> <y> <z>]"));
		std::vector<float> v;
		for (size_t i = 1; i < args.size(); ++i) {
			const Option<float> f = ParseFloat(args[i]);
			if (f.IsNone())
				return CommandResult::Error(String("coordonnées invalides"));
			v.push_back(f.Value());
		}
		m_follow = false;
		m_camera.position = {v[0], v[1], v[2]};
		if (v.size() == 6)
			m_camera.LookAt({v[3], v[4], v[5]});
		m_cameraPlaced = true;
		Visit("camera.move");
		return CommandResult::Ok();
	});
	m_console.Register("studio.pause", "studio.pause — suspend la simulation", [this](const auto &) {
		m_shell.HoldSimulation(true);
		Visit("simulation.pause");
		return CommandResult::Ok(String("simulation suspendue"));
	});
	m_console.Register("studio.play", "studio.play — reprend la simulation", [this](const auto &) {
		m_shell.HoldSimulation(false);
		Visit("simulation.play");
		return CommandResult::Ok(String("simulation reprise"));
	});
	m_console.Register("studio.step", "studio.step [n] — n pas de simulation (pause)", [this](const auto &args) {
		int steps = 1;
		if (args.size() > 1) {
			const Option<float> n = ParseFloat(args[1]);
			if (n.IsNone() || n.Value() < 1.f)
				return CommandResult::Error(String("nombre de pas invalide"));
			steps = int(n.Value());
		}
		m_shell.HoldSimulation(true);
		m_shell.StepSimulation(steps);
		Visit("simulation.step");
		return CommandResult::Ok(String::Format("%d pas", steps));
	});
	m_console.Register("studio.heal", "studio.heal — rend ses PV à la sélection", [this](const auto &) {
		voxel::Game *game = Game();
		if (!game || !m_selected.Valid())
			return CommandResult::Error(String("aucune sélection"));
		auto health = game->Registry().GetComponent<voxel::Health>(m_selected);
		if (health.IsNone())
			return CommandResult::Warning(String("cette entité n'a pas de santé"));
		health.Unwrap()->current = health.Unwrap()->max;
		Visit("inspector.edit");
		return CommandResult::Ok(String("PV restaurés"));
	});
	m_console.Register("studio.kill", "studio.kill — tue la sélection (confirmation)", [this](const auto &) {
		if (!Game() || !m_selected.Valid())
			return CommandResult::Error(String("aucune sélection"));
		const ecs::Entity target = m_selected;
		return CommandResult::Confirm(String("Tuer l'entité sélectionnée ?"), [this, target] {
			voxel::Game *game = Game();
			if (!game || !game->Registry().IsAlive(target))
				return CommandResult::Error(String("l'entité n'existe plus"));
			game->Kill(target);
			Visit("inspector.kill");
			return CommandResult::Ok(String("entité tuée"));
		});
	});
	m_console.Register("studio.stats", "studio.stats — statistiques détaillées (F3)", [this](const auto &) {
		m_showStats = !m_showStats;
		m_showStats ? m_stats.Show() : m_stats.Hide();
		Visit("stats.toggle");
		return CommandResult::Ok();
	});
	m_console.Register("reload", "reload [block <id> | mod <nom> | dimension <id> | biome <id>] — rechargement à chaud", [this](const auto &args) {
		if (args.size() == 1) // le bloc ouvert dans l'éditeur
			return m_blocks->HasDocument() ? m_blocks->Apply() : CommandResult::Error(String("usage : reload block <id> | reload mod <nom>"));
		if (args.size() == 3 && args[1] == "block")
			return m_blocks->ReloadFromDisk(args[2]);
		if (args.size() == 3 && args[1] == "mod")
			return m_blocks->ReloadMod(args[2]);
		if (args.size() == 3 && (args[1] == "dimension" || args[1] == "biome"))
			return CommandResult::Warning(String("reload ") + args[1] +
										  String(" : le terrain déjà généré ne change pas — régénération dans une dimension temporaire : étape 7 du studio"));
		return CommandResult::Error(String("usage : reload [block <id> | mod <nom>]"));
	});
	m_console.Register("studio.tab", "studio.tab left|right|catalog <index> — onglet d'un dock (0 : le premier)", [this](const auto &args) {
		if (args.size() != 3 || (args[1] != "left" && args[1] != "right" && args[1] != "catalog"))
			return CommandResult::Error(String("usage : studio.tab left|right|catalog <index>"));
		const int index = std::atoi(args[2].CStr());
		if (index < 0)
			return CommandResult::Error(String("onglet invalide"));
		if (args[1] == "right") {
			if (index >= m_rightDock.Count())
				return CommandResult::Error(String("onglet invalide"));
			m_rightDock.SetActive(index);
		} else if (args[1] == "catalog" || index >= 1) {
			// « left n » (n ≥ 1) : l'ancien dock de gauche portait les catalogues.
			const int page = args[1] == "catalog" ? index : index - 1;
			if (page >= m_catalogDock.Count())
				return CommandResult::Error(String("onglet invalide"));
			m_catalogDock.SetActive(page);
		} else {
			m_leftDock.SetActive(0);
		}
		return CommandResult::Ok(args[1] + String::Format(" %d", index));
	});
	auto startDiagnostic = [this]() -> CommandResult {
		String error;
		if (!m_shell.StartDiagnostic(error))
			return CommandResult::Error(error);
		Visit("diagnostic.start");
		return CommandResult::Ok(String("diagnostic démarré — « studio.diag.stop » l'arrête et l'exporte"));
	};
	auto stopDiagnostic = [this]() -> CommandResult {
		const double seconds = m_shell.DiagnosticSeconds();
		Result<String, String> written = m_shell.StopDiagnostic();
		if (written.IsError())
			return CommandResult::Error(written.Error());
		Visit("diagnostic.export", FeatureStatus::Ok, written.Value());
		return CommandResult::Ok(String::Format("%.1f s capturées : %s (profiler report / view)", seconds, written.Value().CStr()));
	};
	m_console.Register("studio.diag.start", "studio.diag.start — démarre le diagnostic de performances (capture)",
					   [startDiagnostic](const auto &) { return startDiagnostic(); });
	m_console.Register("studio.diag.stop", "studio.diag.stop — arrête le diagnostic et l'exporte en .vcap",
					   [stopDiagnostic](const auto &) { return stopDiagnostic(); });
	m_console.Register("studio.diag", "studio.diag — démarre ou arrête le diagnostic",
					   [this, startDiagnostic, stopDiagnostic](const auto &) {
						   return m_shell.DiagnosticRunning() ? stopDiagnostic() : startDiagnostic();
					   });
	m_console.Register("studio.palette", "studio.palette — palette de commandes (F1, Ctrl+Maj+P)", [this](const auto &) {
		if (!m_paletteView)
			return CommandResult::Error(String("palette indisponible"));
		BuildPalette(); // commandes enregistrées depuis (éditeurs)
		m_paletteView->Open();
		Visit("palette.open");
		return CommandResult::Ok();
	});
	m_console.Register("studio.gizmo", "studio.gizmo translate|rotate|scale | snap [pas] — mode et accroche du gizmo", [this](const auto &args) {
		if (args.size() < 2)
			return CommandResult::Error(String("usage : studio.gizmo translate|rotate|scale | snap [pas]"));
		if (args[1] == "snap") {
			const Option<float> step = args.size() > 2 ? ParseFloat(args[2]) : Option<float>(NONE);
			const bool on = step.IsSome() ? step.Value() > 0.f : m_gizmo.translateSnap <= 0.f;
			m_gizmo.translateSnap = on ? (step.IsSome() ? step.Value() : 1.f) : 0.f;
			m_gizmo.rotateSnap = on ? 3.14159265f / 12.f : 0.f;
			m_gizmo.scaleSnap = on ? 0.25f : 0.f;
			return CommandResult::Ok(on ? String::Format("accroche %.2f bloc, 15°", double(m_gizmo.translateSnap)) : String("accroche libre"));
		}
		const Option<editor::GizmoMode> mode = editor::ParseGizmoMode(args[1].View());
		if (mode.IsNone())
			return CommandResult::Error(String("mode inconnu : ") + args[1]);
		m_gizmo.mode = mode.Value();
		Visit("gizmo.mode", FeatureStatus::Ok, args[1]);
		return CommandResult::Ok(String("gizmo : ") + args[1]);
	});
	m_console.Register("studio.view", "studio.view front|back|left|right|top|bottom|iso — vue normalisée autour de la sélection", [this](const auto &args) {
		const Option<editor::ViewPreset> preset = args.size() == 2 ? editor::ParseViewPreset(args[1].View()) : Option<editor::ViewPreset>(NONE);
		if (preset.IsNone())
			return CommandResult::Error(String("usage : studio.view front|back|left|right|top|bottom|iso"));
		// Pivot : la sélection, sinon le point visé au centre de la vue.
		math::FVector3 pivot = PointUnder(m_viewport.Bounds().w * 0.5f, m_viewport.Bounds().h * 0.5f);
		voxel::Game *game = Game();
		if (game && m_selected.Valid()) {
			if (auto t = game->Registry().GetComponent<voxel::Transform>(m_selected); t.IsSome())
				pivot = Add(t.Unwrap()->position, {0.f, 1.f, 0.f});
		} else if (game && m_selectedBlock.IsSome())
			pivot = {float(m_selectedBlock.Value().x) + 0.5f, float(m_selectedBlock.Value().y) + 0.5f, float(m_selectedBlock.Value().z) + 0.5f};
		const float distance = std::clamp((pivot - m_camera.position).Length(), 6.f, 80.f);
		m_nav.pivot = pivot;
		m_nav.distance = distance;
		m_nav.SetView(preset.Value());
		m_camera.position = m_nav.Eye();
		m_camera.yaw = m_nav.yaw;
		m_camera.pitch = std::clamp(m_nav.pitch, -PITCH_LIMIT, PITCH_LIMIT);
		m_cameraPlaced = true;
		m_follow = false;
		Visit("camera.view", FeatureStatus::Ok, args[1]);
		return CommandResult::Ok(String("vue ") + args[1]);
	});
	m_console.Register("studio.inspect.set", "studio.inspect.set <champ> <valeur…> — modifie la sélection (entité ou bloc)", [this](const auto &args) {
		voxel::Game *game = Game();
		if (!game)
			return CommandResult::Error(String("aucune partie en cours"));
		if (args.size() < 3)
			return CommandResult::Error(String("usage : studio.inspect.set <champ> <valeur…>"));
		String value = args[2];
		for (size_t i = 3; i < args.size(); ++i)
			value += String(" ") + args[i];
		Result<String, String> r = m_selected.Valid()		   ? ApplyEntityEdit(*game, m_selected, args[1], value)
								   : m_selectedBlock.IsSome() ? ApplyBlockEdit(*game, m_selectedBlock.Value(), args[1], value)
															  : Result<String, String>(Err(String("aucune sélection")));
		m_inspectorSignature = String();
		Visit("inspector.edit", r.IsOk() ? FeatureStatus::Ok : FeatureStatus::Warning, args[1]);
		return r.IsOk() ? CommandResult::Ok(r.Value()) : CommandResult::Error(r.Error());
	});
	m_console.Register("studio.inspect.block", "studio.inspect.block <x> <y> <z> | use | toggle | state <état> — bloc sélectionné", [this](const auto &args) {
		voxel::Game *game = Game();
		if (!game)
			return CommandResult::Error(String("aucune partie en cours"));
		if (args.size() == 4) {
			std::vector<int> v;
			for (size_t i = 1; i < 4; ++i) {
				const Option<float> f = ParseFloat(args[i]);
				if (f.IsNone())
					return CommandResult::Error(String("coordonnées invalides"));
				v.push_back(int(std::floor(f.Value())));
			}
			Select(ecs::Entity{});
			SelectBlock(Some(voxel::BlockPos{v[0], v[1], v[2]}));
			return CommandResult::Ok(game->GetWorld().BlockAt(m_selectedBlock.Value()).id);
		}
		if (m_selectedBlock.IsNone())
			return CommandResult::Error(String("aucun bloc sélectionné"));
		if (args.size() >= 2 && (args[1] == "use" || args[1] == "toggle" || (args[1] == "state" && args.size() == 3))) {
			Result<String, String> r = ApplyBlockEdit(*game, m_selectedBlock.Value(), args[1], args.size() == 3 ? args[2] : String());
			m_inspectorSignature = String();
			Visit("inspector.block_edit", r.IsOk() ? FeatureStatus::Ok : FeatureStatus::Warning, args[1]);
			return r.IsOk() ? CommandResult::Ok(r.Value()) : CommandResult::Error(r.Error());
		}
		return CommandResult::Error(String("usage : studio.inspect.block <x> <y> <z> | use | toggle | state <état>"));
	});
	m_console.Register("studio.element.new", "studio.element.new <genre 0..7> <id> [mod] — bloc, modèle, structure, texture, entité, biome, générateur, dimension", [this](const auto &args) {
		if (args.size() < 3)
			return CommandResult::Error(String("usage : studio.element.new <genre 0..7> <id> [mod]"));
		const int kind = std::atoi(args[1].CStr());
		const String id = args[2].Trim();
		const String mod = args.size() > 3 && !args[3].Trim().IsEmpty() ? args[3].Trim() : String("core");
		if (id.IsEmpty())
			return CommandResult::Error(String("identifiant vide"));
		CommandResult r;
		switch (kind) {
		case 0: r = m_blocks->New(id, mod); break;
		case 1: r = m_models->New(id, mod); break;
		case 2: r = m_structures->New(voxel::BlockPos{8, 8, 8}, id); break;
		case 3: r = m_textures->New(16, 16, id.Contains(':') ? id : mod + String(":") + id); break;
		case 4: r = m_entities->New(id, mod); break;
		case 5: r = m_worldgen->New(WorldgenKind::Biome, id, mod); break;
		case 6: r = m_worldgen->New(WorldgenKind::Generator, id, mod); break;
		case 7: r = m_worldgen->New(WorldgenKind::Dimension, id, mod); break;
		default: return CommandResult::Error(String("genre inconnu (0..7)"));
		}
		if (r.status != CommandStatus::Error && m_wizard.Valid() && m_wizard.IsOpen())
			m_wizard.Close();
		static const int pages[8] = {1, 2, 3, 4, 5, 6, 6, 6};
		if (r.status != CommandStatus::Error)
			ShowRight(pages[kind]);
		Visit("elements.new", r.status == CommandStatus::Error ? FeatureStatus::Warning : FeatureStatus::Ok);
		return r;
	});
	// Actions du document ouvert : celui de la page active du dock de droite.
	auto onActive = [this](const char *what) -> CommandResult {
		const String action(what);
		const int page = m_rightDock.Active();
		auto unsupported = [&] { return CommandResult::Warning(action + String(" : sans objet pour cette page")); };
		switch (page) {
		case 1: return action == "undo" ? m_blocks->Undo() : action == "redo" ? m_blocks->Redo() : action == "save" ? m_blocks->Save() : m_blocks->Apply();
		case 2: return action == "undo" ? m_models->Undo() : action == "redo" ? m_models->Redo() : action == "save" ? m_models->Save() : m_models->Apply();
		case 3: return action == "undo" ? m_structures->Undo() : action == "redo" ? m_structures->Redo() : action == "save" ? m_structures->Save(String()) : unsupported();
		case 4: return action == "undo" ? m_textures->Undo() : action == "redo" ? m_textures->Redo() : m_textures->Save();
		case 5: return action == "undo" ? m_entities->Undo() : action == "redo" ? m_entities->Redo() : action == "save" ? m_entities->Save() : m_entities->Apply();
		case 6: return action == "undo" ? m_worldgen->Undo() : action == "redo" ? m_worldgen->Redo() : action == "save" ? m_worldgen->Save() : m_worldgen->Apply();
		default: return CommandResult::Warning(String("ouvrez un document (dock de droite) pour : ") + action);
		}
	};
	m_console.Register("studio.undo", "studio.undo — annule dans le document actif (Ctrl+Z)", [onActive](const auto &) { return onActive("undo"); });
	m_console.Register("studio.redo", "studio.redo — rétablit dans le document actif (Ctrl+Y)", [onActive](const auto &) { return onActive("redo"); });
	m_console.Register("studio.save", "studio.save — enregistre le document actif (Ctrl+S)", [onActive](const auto &) { return onActive("save"); });
	m_console.Register("studio.apply", "studio.apply — applique à chaud le document actif (Ctrl+Maj+R)", [onActive](const auto &) { return onActive("apply"); });
	m_console.Register("studio.block.filter", "studio.block.filter [texte] — filtre du catalogue des blocs", [this](const auto &args) {
		String text;
		for (size_t i = 1; i < args.size(); ++i)
			text += (i > 1 ? String(" ") : String()) + args[i];
		return m_blocks->SetCatalogFilter(text);
	});
	m_console.Register("studio.block.open", "studio.block.open <id> — ouvre un bloc dans l'éditeur", [this](const auto &args) {
		return args.size() == 2 ? m_blocks->Open(args[1]) : CommandResult::Error(String("usage : studio.block.open <id>"));
	});
	m_console.Register("studio.block.new", "studio.block.new <id> [mod] — nouveau bloc", [this](const auto &args) {
		return args.size() >= 2 ? m_blocks->New(args[1], args.size() > 2 ? args[2] : String("core"))
								: CommandResult::Error(String("usage : studio.block.new <id> [mod]"));
	});
	m_console.Register("studio.block.set", "studio.block.set <champ> <json> — écrit un champ (null : le retire)", [this](const auto &args) {
		if (args.size() < 3)
			return CommandResult::Error(String("usage : studio.block.set <champ> <json>"));
		String value = args[2];
		for (size_t i = 3; i < args.size(); ++i)
			value += String(" ") + args[i];
		return m_blocks->SetField(args[1], value);
	});
	m_console.Register("studio.block.scope", "studio.block.scope [état] — portée d'édition", [this](const auto &args) {
		return m_blocks->SetScope(args.size() > 1 ? args[1] : String());
	});
	m_console.Register("studio.block.apply", "studio.block.apply — rechargement à chaud (Ctrl+Maj+R)", [this](const auto &) { return m_blocks->Apply(); });
	m_console.Register("studio.block.save", "studio.block.save — enregistre dans le mod", [this](const auto &) { return m_blocks->Save(); });
	m_console.Register("studio.block.place", "studio.block.place — pose devant la caméra", [this](const auto &) { return m_blocks->Place(); });
	m_console.Register("studio.block.box.add", "studio.block.box.add x1 y1 z1 x2 y2 z2 — boîte nodebox (pixels 0 à 16)", [this](const auto &args) {
		if (args.size() != 7)
			return CommandResult::Error(String("usage : studio.block.box.add x1 y1 z1 x2 y2 z2"));
		BlockDocument::NodeBox box{};
		for (size_t k = 0; k < 6; ++k) {
			const Option<float> v = ParseFloat(args[k + 1]);
			if (v.IsNone())
				return CommandResult::Error(String("coordonnée invalide : ") + args[k + 1]);
			box[k] = v.Value();
		}
		return m_blocks->ShapeAddBox(box);
	});
	m_console.Register("studio.block.box.mirror", "studio.block.box.mirror <index> x|y|z — copie miroir", [this](const auto &args) {
		const Option<float> i = args.size() > 2 ? ParseFloat(args[1]) : Option<float>(NONE);
		if (i.IsNone() || (args[2] != "x" && args[2] != "y" && args[2] != "z"))
			return CommandResult::Error(String("usage : studio.block.box.mirror <index> x|y|z"));
		return m_blocks->ShapeMirror(size_t(i.Value()), args[2] == "x" ? 0 : args[2] == "y" ? 1 : 2);
	});
	m_console.Register("studio.block.box.rotate", "studio.block.box.rotate — quart de tour (verticale)", [this](const auto &) { return m_blocks->ShapeRotate(); });
	m_console.Register("studio.block.box.remove", "studio.block.box.remove <index>", [this](const auto &args) {
		const Option<float> i = args.size() > 1 ? ParseFloat(args[1]) : Option<float>(NONE);
		return i.IsSome() ? m_blocks->ShapeRemove(size_t(i.Value())) : CommandResult::Error(String("usage : studio.block.box.remove <index>"));
	});
	m_console.Register("studio.block.undo", "studio.block.undo — annule (Ctrl+Z)", [this](const auto &) { return m_blocks->Undo(); });
	m_console.Register("studio.block.redo", "studio.block.redo — rétablit (Ctrl+Y)", [this](const auto &) { return m_blocks->Redo(); });
	m_console.SetFallback([this](const String &line) {
		Session *session = m_shell.Game();
		if (!session || !session->Ready())
			return CommandResult::Error(String("aucune partie en cours : commande du jeu impossible"));
		Result<String, String> r = session->Command(line);
		Visit("console.game_command", r.IsOk() ? FeatureStatus::Ok : FeatureStatus::Warning);
		return r.IsOk() ? CommandResult::Ok(r.Value()) : CommandResult::Error(r.Error());
	});
}

CommandResult Studio::Execute(const String &line) {
	CommandResult result = m_console.Submit(line);
	m_statusTimer = 0.f; // boutons (pause, suivre, gizmo) à jour dès l'image suivante
	if (m_report)
		m_report->Count(String("console.") + String(CommandStatusId(result.status)));
	Visit("console.command", FeatureStatus::Ok, String(CommandStatusId(result.status)));
	return result;
}

void Studio::Select(ecs::Entity entity) {
	if (entity == m_selected)
		return;
	m_selected = entity;
	if (!entity.Valid())
		m_follow = false;
	else
		m_selectedBlock = NONE;
	m_gizmo.End();
	m_inspectorSignature = String();
	m_outlinerSignature = String(); // surlignage de la ligne
	RefreshOutliner();
	if (entity.Valid())
		Visit("outliner.select");
}

void Studio::FocusOn(ecs::Entity entity) {
	voxel::Game *game = Game();
	if (!game || !game->Registry().IsAlive(entity))
		return;
	auto t = game->Registry().GetComponent<voxel::Transform>(entity);
	if (t.IsNone())
		return;
	const math::FVector3 at = Add(t.Unwrap()->position, {0.f, 1.f, 0.f});
	m_camera.position = Add(at, {-4.f, 3.f, -4.f});
	m_camera.LookAt(at);
	m_cameraPlaced = true;
	Visit("camera.focus");
}

void Studio::HandleEvent(const sdl3::Event &ev, bool consumed) {
	if (m_textures->HandleEvent(ev)) // peinture sur la toile de l'atelier de textures
		return;
	if (HandleViewportEvent(ev, consumed))
		return;
	const uint32_t type = ev.raw.type;
	if (consumed || type != SDL_EVENT_KEY_DOWN)
		return;
	const bool ctrl = (ev.raw.key.mod & SDL_KMOD_CTRL) != 0, shift = (ev.raw.key.mod & SDL_KMOD_SHIFT) != 0;
	// La palette répond même depuis un champ de texte.
	if (ev.raw.key.key == SDLK_F1 || (ctrl && shift && ev.raw.key.key == SDLK_P)) {
		(void)Execute(String("studio.palette"));
		return;
	}
	if (m_window.TextFieldHasFocus())
		return;
	if (ctrl && shift && ev.raw.key.key == SDLK_R) {
		(void)Execute(String("studio.apply"));
		return;
	}
	if (ctrl && !shift && ev.raw.key.key == SDLK_Z) {
		(void)Execute(String("studio.undo"));
		return;
	}
	if (ctrl && (ev.raw.key.key == SDLK_Y || (shift && ev.raw.key.key == SDLK_Z))) {
		(void)Execute(String("studio.redo"));
		return;
	}
	if (ctrl && ev.raw.key.key == SDLK_S) {
		(void)Execute(String("studio.save"));
		return;
	}
	if (ctrl)
		return;
	switch (ev.raw.key.key) {
	case SDLK_ESCAPE:
		// Échap : poursuite, puis sélection — jamais une sortie.
		if (m_follow)
			m_follow = false;
		else {
			Select(ecs::Entity{});
			SelectBlock(NONE);
		}
		break;
	case SDLK_F3:
		(void)Execute(String("studio.stats"));
		break;
	case SDLK_F:
		(void)Execute(String("studio.focus"));
		break;
	case SDLK_W:
		(void)Execute(String("studio.gizmo translate"));
		break;
	case SDLK_E:
		(void)Execute(String("studio.gizmo rotate"));
		break;
	case SDLK_R:
		(void)Execute(String("studio.gizmo scale"));
		break;
	default:
		break;
	}
}

Option<editor::GizmoView> Studio::ViewportView() {
	auto vp = m_viewport.Get<ui::Viewport3DState>();
	const sdl3::FRect box = m_viewport.Bounds();
	if (vp.IsNone() || box.w < 2.f || box.h < 2.f)
		return NONE;
	editor::GizmoView view{vp.Unwrap()->camera, box.w, box.h};
	view.camera.aspect = box.w / box.h;
	return Some(view);
}

math::FVector3 Studio::PointUnder(float x, float y) {
	const Option<editor::GizmoView> view = ViewportView();
	voxel::Game *game = Game();
	if (view.IsNone() || !game)
		return Add(m_camera.position, Scale(m_camera.Forward(), 12.f));
	const math::FRay ray = view.Value().camera.ScreenPointToRay(x, y, view.Value().width, view.Value().height);
	if (const Option<voxel::BlockHit> hit = voxel::RaycastBlocks(game->GetWorld(), ray.origin, ray.direction, 256.f); hit.IsSome())
		return hit.Value().point;
	return ray.origin + ray.direction * 12.f;
}

void Studio::Navigate(const std::function<void(editor::OrbitNavigator &)> &op, const math::FVector3 &pivot) {
	// La caméra libre (position, cap, tangage) devient une caméra orbitale
	// dont le pivot est à la distance du point visé, l'opération s'applique,
	// puis on relit position et angles.
	m_nav.yaw = m_camera.yaw;
	m_nav.pitch = m_camera.pitch;
	m_nav.distance = std::max(0.5f, (pivot - m_camera.position).Length());
	m_nav.pivot = m_camera.position + m_camera.Forward() * m_nav.distance;
	op(m_nav);
	m_camera.position = m_nav.Eye();
	m_camera.yaw = m_nav.yaw;
	m_camera.pitch = std::clamp(m_nav.pitch, -PITCH_LIMIT, PITCH_LIMIT);
	m_cameraPlaced = true;
	m_follow = false;
}

bool Studio::HandleViewportEvent(const sdl3::Event &ev, bool consumed) {
	const uint32_t type = ev.raw.type;
	const sdl3::FRect box = m_viewport.Bounds();
	auto inside = [&](float x, float y) { return x >= box.x && y >= box.y && x < box.x + box.w && y < box.y + box.h; };
	voxel::Game *game = Game();

	// ── Vol (clic droit maintenu) ───────────────────────────────────────────
	if (type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.raw.button.button == SDL_BUTTON_RIGHT && inside(ev.raw.button.x, ev.raw.button.y)) {
		m_flying = true;
		m_follow = false;
		m_window.SetKeyboardCapture(true);
		if (m_window.Window())
			(void)m_window.Window()->SetRelativeMouseMode(true);
		Visit("camera.fly");
		return true;
	}
	if (type == SDL_EVENT_MOUSE_BUTTON_UP && ev.raw.button.button == SDL_BUTTON_RIGHT && m_flying) {
		m_flying = false;
		m_window.SetKeyboardCapture(false);
		if (m_window.Window())
			(void)m_window.Window()->SetRelativeMouseMode(false);
		return true;
	}
	if (type == SDL_EVENT_MOUSE_MOTION && m_flying) {
		m_camera.Look(-ev.raw.motion.xrel * LOOK_SPEED, -ev.raw.motion.yrel * LOOK_SPEED);
		m_cameraPlaced = true;
		return true;
	}

	// ── Orbite / panoramique (clic milieu, Maj) ─────────────────────────────
	if (type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.raw.button.button == SDL_BUTTON_MIDDLE && !consumed &&
		inside(ev.raw.button.x, ev.raw.button.y)) {
		m_orbitPivot = PointUnder(ev.raw.button.x - box.x, ev.raw.button.y - box.y);
		const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
		m_panning = shift;
		m_orbiting = !shift;
		Visit(shift ? "camera.pan" : "camera.orbit");
		return true;
	}
	if (type == SDL_EVENT_MOUSE_BUTTON_UP && ev.raw.button.button == SDL_BUTTON_MIDDLE && (m_orbiting || m_panning)) {
		m_orbiting = m_panning = false;
		return true;
	}
	if (type == SDL_EVENT_MOUSE_MOTION && (m_orbiting || m_panning)) {
		const float dx = ev.raw.motion.xrel, dy = ev.raw.motion.yrel;
		if (m_orbiting)
			Navigate([&](editor::OrbitNavigator &nav) { nav.OrbitAbout(m_orbitPivot, dx, dy); }, m_orbitPivot);
		else
			Navigate([&](editor::OrbitNavigator &nav) { nav.Pan(dx, dy, box.h); }, m_orbitPivot);
		return true;
	}

	// ── Molette : zoom vers le curseur ──────────────────────────────────────
	if (type == SDL_EVENT_MOUSE_WHEEL && !consumed && inside(ev.raw.wheel.mouse_x, ev.raw.wheel.mouse_y)) {
		const math::FVector3 toward = PointUnder(ev.raw.wheel.mouse_x - box.x, ev.raw.wheel.mouse_y - box.y);
		const float steps = ev.raw.wheel.y;
		Navigate([&](editor::OrbitNavigator &nav) { nav.Dolly(steps, Some(toward)); }, toward);
		Visit("camera.dolly");
		return true;
	}

	// ── Gizmo et sélection (clic gauche) ────────────────────────────────────
	const Option<editor::GizmoView> view = ViewportView();
	if (type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.raw.button.button == SDL_BUTTON_LEFT && !consumed && game && view.IsSome() &&
		inside(ev.raw.button.x, ev.raw.button.y)) {
		const float x = ev.raw.button.x - box.x, y = ev.raw.button.y - box.y;
		UpdateGizmo();
		if (m_gizmo.visible && m_gizmo.Begin(view.Value(), x, y)) {
			ecs::ArchetypeRegistry &reg = game->Registry();
			if (auto t = reg.GetComponent<voxel::Transform>(m_selected); t.IsSome())
				m_gizmoStart = {t.Unwrap()->position, t.Unwrap()->yaw, t.Unwrap()->pitch, 1.f};
			if (auto v = reg.GetComponent<voxel::Visual>(m_selected); v.IsSome())
				m_gizmoStart.scale = v.Unwrap()->scale;
			m_follow = false;
			return true;
		}
		// Sélection : l'entité la plus proche, sinon le bloc.
		const math::FRay ray = view.Value().camera.ScreenPointToRay(x, y, view.Value().width, view.Value().height);
		const Option<voxel::EntityHit> entity = voxel::RaycastEntities(game->Registry(), ray.origin, ray.direction, 256.f);
		const Option<voxel::BlockHit> block = voxel::RaycastBlocks(game->GetWorld(), ray.origin, ray.direction, 256.f);
		if (entity.IsSome() && (block.IsNone() || entity.Value().distance <= block.Value().distance)) {
			Select(entity.Value().entity);
			if (ev.raw.button.clicks >= 2)
				FocusOn(entity.Value().entity);
			Visit("viewport.pick_entity");
		} else if (block.IsSome()) {
			Select(ecs::Entity{});
			SelectBlock(Some(block.Value().block));
			Visit("viewport.pick_block");
		} else {
			Select(ecs::Entity{});
			SelectBlock(NONE);
		}
		return true;
	}
	if (type == SDL_EVENT_MOUSE_MOTION && game && view.IsSome()) {
		const float x = ev.raw.motion.x - box.x, y = ev.raw.motion.y - box.y;
		if (!m_gizmo.Active()) {
			if (!consumed)
				(void)m_gizmo.Hover(view.Value(), x, y);
			return false;
		}
		const editor::GizmoDelta d = m_gizmo.Drag(view.Value(), x, y);
		ecs::ArchetypeRegistry &reg = game->Registry();
		auto t = reg.GetComponent<voxel::Transform>(m_selected);
		if (t.IsNone()) {
			m_gizmo.End();
			return true;
		}
		switch (m_gizmo.mode) {
		case editor::GizmoMode::Translate:
			t.Unwrap()->position = t.Unwrap()->previous = m_gizmoStart.position + d.translation;
			if (auto v = reg.GetComponent<voxel::Velocity>(m_selected); v.IsSome())
				v.Unwrap()->linear = {0.f, 0.f, 0.f};
			break;
		case editor::GizmoMode::Rotate:
			// Cap autour de Y, tangage autour d'un axe horizontal.
			if (d.rotationAxis.y > 0.5f)
				t.Unwrap()->yaw = m_gizmoStart.yaw + d.angle;
			else
				t.Unwrap()->pitch = std::clamp(m_gizmoStart.pitch + d.angle, -1.5f, 1.5f);
			break;
		case editor::GizmoMode::Scale:
			if (auto v = reg.GetComponent<voxel::Visual>(m_selected); v.IsSome()) {
				const float f = std::fabs(d.scale.x - 1.f) > std::fabs(d.scale.y - 1.f)
									? (std::fabs(d.scale.x - 1.f) > std::fabs(d.scale.z - 1.f) ? d.scale.x : d.scale.z)
									: (std::fabs(d.scale.y - 1.f) > std::fabs(d.scale.z - 1.f) ? d.scale.y : d.scale.z);
				v.Unwrap()->scale = std::clamp(m_gizmoStart.scale * f, 0.05f, 50.f);
			}
			break;
		}
		return true;
	}
	if (type == SDL_EVENT_MOUSE_BUTTON_UP && ev.raw.button.button == SDL_BUTTON_LEFT && m_gizmo.Active()) {
		m_gizmo.End();
		Visit("gizmo.drag", FeatureStatus::Ok, String(editor::GizmoModeId(m_gizmo.mode)));
		return true;
	}
	return false;
}

void Studio::UpdateGizmo() {
	voxel::Game *game = Game();
	m_gizmo.visible = false;
	if (!game || !m_selected.Valid() || !game->Registry().IsAlive(m_selected))
		return;
	auto t = game->Registry().GetComponent<voxel::Transform>(m_selected);
	if (t.IsNone())
		return;
	m_gizmo.visible = true;
	if (!m_gizmo.Active())
		m_gizmo.position = t.Unwrap()->position;
}

void Studio::DrawOverlay(ui::Painter &painter, const sdl3::FRect &rect) {
	// Repère des axes (coin haut-droit), puis le gizmo de la sélection.
	auto vp = m_viewport.Get<ui::Viewport3DState>();
	if (vp.IsNone() || rect.w < 4.f || rect.h < 4.f)
		return;
	const render3d::Camera &camera = vp.Unwrap()->camera;
	const math::FVector3 forward = (camera.target - camera.position).Normalize();
	const math::FVector3 right = forward.Cross(camera.up).Normalize();
	const math::FVector3 up = right.Cross(forward);
	const sdl3::FPoint center{rect.x + rect.w - 44.f, rect.y + 44.f};
	painter.FillCircle(center, 34.f, sdl3::FColor{0.f, 0.f, 0.f, 0.35f});
	const std::pair<math::FVector3, sdl3::FColor> axes[3] = {
		{{1.f, 0.f, 0.f}, editor::AXIS_X}, {{0.f, 1.f, 0.f}, editor::AXIS_Y}, {{0.f, 0.f, 1.f}, editor::AXIS_Z}};
	for (const auto &[axis, color] : axes) {
		const sdl3::FPoint tip{center.x + axis.Dot(right) * 26.f, center.y - axis.Dot(up) * 26.f};
		painter.Line(center, tip, 2.5f, color);
		painter.FillCircle(tip, 4.f, color);
	}
	const Option<editor::GizmoView> view = ViewportView();
	if (view.IsNone() || !m_gizmo.visible)
		return;
	for (const editor::GizmoStroke &s : m_gizmo.Strokes(view.Value()))
		painter.Line(sdl3::FPoint{rect.x + s.a.x, rect.y + s.a.y}, sdl3::FPoint{rect.x + s.b.x, rect.y + s.b.y}, s.width, s.color);
}

void Studio::UpdateCamera(float dt) {
	Session *session = m_shell.Game();
	voxel::Game *game = Game();
	if (!game)
		return;
	if (!m_cameraPlaced) {
		// Premier affichage : la caméra du joueur.
		const render3d::Camera &c = session->Camera();
		m_camera.position = c.position;
		m_camera.LookAt(c.target);
		m_cameraPlaced = true;
	}
	if (m_follow && m_selected.Valid() && game->Registry().IsAlive(m_selected)) {
		auto t = game->Registry().GetComponent<voxel::Transform>(m_selected);
		if (t.IsSome()) {
			// Cap pris sur la VITESSE (pas sur l'orientation du modèle, qui
			// tourne sur place) ; à l'arrêt, le dernier cap est gardé.
			if (auto v = game->Registry().GetComponent<voxel::Velocity>(m_selected); v.IsSome()) {
				const math::FVector3 vel = v.Unwrap()->linear;
				const float speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
				if (speed > 0.5f)
					m_followHeading = {vel.x / speed, 0.f, vel.z / speed};
			}
			const math::FVector3 at = Add(t.Unwrap()->position, {0.f, 1.2f, 0.f});
			const math::FVector3 wanted = Add(at, Add(Scale(m_followHeading, -6.f), {0.f, 2.5f, 0.f}));
			const float k = std::min(1.f, dt * 6.f);
			const math::FVector3 eased = Add(m_camera.position, Scale(Add(wanted, Scale(m_camera.position, -1.f)), k));
			// Jamais dans le terrain : de la cible vers la caméra, arrêt avant
			// le premier bloc plein (une colline derrière la cible).
			const math::FVector3 ray = Add(eased, Scale(at, -1.f));
			const float length = std::sqrt(ray.x * ray.x + ray.y * ray.y + ray.z * ray.z);
			math::FVector3 safe = eased;
			if (length > 1e-3f) {
				const voxel::World &world = game->GetWorld();
				for (float d = 0.25f; d <= length; d += 0.25f) {
					const math::FVector3 p = Add(at, Scale(ray, d / length));
					if (world.BlockAt(voxel::BlockPos::FromWorld(p)).solid) {
						safe = Add(at, Scale(ray, std::max(0.f, d - 0.4f) / length));
						break;
					}
				}
			}
			m_camera.position = safe;
			m_camera.LookAt(at);
		}
	} else if (m_follow) {
		m_follow = false;
	}
	if (m_flying) {
		const bool *keys = SDL_GetKeyboardState(nullptr);
		const math::FVector3 forward = m_camera.Forward();
		const math::FVector3 right{std::cos(m_camera.yaw), 0.f, -std::sin(m_camera.yaw)};
		math::FVector3 move{0.f, 0.f, 0.f};
		if (keys[SDL_SCANCODE_W])
			move = Add(move, forward);
		if (keys[SDL_SCANCODE_S])
			move = Add(move, Scale(forward, -1.f));
		if (keys[SDL_SCANCODE_D])
			move = Add(move, Scale(right, -1.f));
		if (keys[SDL_SCANCODE_A])
			move = Add(move, right);
		if (keys[SDL_SCANCODE_E])
			move = Add(move, {0.f, 1.f, 0.f});
		if (keys[SDL_SCANCODE_Q])
			move = Add(move, {0.f, -1.f, 0.f});
		const float speed = FLY_SPEED * (keys[SDL_SCANCODE_LSHIFT] ? 3.f : 1.f);
		m_camera.position = Add(m_camera.position, Scale(move, speed * dt));
	}
}

void Studio::Update(float dt) {
	m_console.Update(dt);
	Session *session = m_shell.Game();
	voxel::Game *game = Game();
	if (game && m_selected.Valid() && !game->Registry().IsAlive(m_selected))
		Select(ecs::Entity{});
	UpdateCamera(dt);
	if (auto vp = m_viewport.Get<ui::Viewport3DState>(); vp.IsSome()) {
		const float aspect = vp.Unwrap()->textureHeight > 0
								 ? float(vp.Unwrap()->textureWidth) / float(vp.Unwrap()->textureHeight)
								 : 16.f / 9.f;
		if (game) {
			vp.Unwrap()->root = &session->Renderer().Root();
			vp.Unwrap()->camera = m_camera.ToCamera(aspect, session->Camera().farPlane);
		} else {
			vp.Unwrap()->root = &m_emptyScene;
		}
	}
	m_outlinerTimer += dt;
	if (m_outlinerTimer >= OUTLINER_PERIOD) {
		m_outlinerTimer = 0.f;
		RefreshOutliner();
	}
	RefreshInspector();
	m_blocks->Update(dt);
	m_models->Update(dt);
	m_structures->Update(dt);
	m_textures->Update(dt);
	m_entities->Update(dt);
	m_worldgen->Update(dt);
	RefreshConsole();
	m_statusTimer -= dt;
	if (m_statusTimer <= 0.f) {
		m_statusTimer = 0.25f;
		RefreshToolbar();
	}
	UpdateGizmo();
	m_elementTimer += dt;
	if (m_elementTimer >= 1.f) { // nombres d'éléments : 1 Hz suffit
		m_elementTimer = 0.f;
		RefreshElements();
	}
	if (m_frame.Root().Valid())
		m_frame.Update();
}

void Studio::RefreshOutliner() {
	voxel::Game *game = Game();
	m_entries = game ? CollectEntities(*game, m_filter, m_camera.position) : std::vector<OutlinerEntry>{};
	if (m_report && game)
		m_report->Visit(String("outliner.refresh"));
	// Signature : l'ensemble affiché (pas les distances, qui bougent toujours).
	String signature = String::Format("%u|", unsigned(m_selected.id));
	for (const OutlinerEntry &e : m_entries)
		signature += String::Format("%u:%u:%d:%d,%d,%d;", unsigned(e.entity.id), unsigned(e.category), int(e.dead),
									e.chunk.x, e.chunk.y, e.chunk.z) +
					 e.name;
	if (signature == m_outlinerSignature)
		return;
	m_outlinerSignature = signature;
	RebuildOutliner();
}

void Studio::RebuildOutliner() {
	if (!m_outlinerList.Valid())
		return;
	m_outlinerList.Clear();
	if (!Game()) {
		m_outlinerCount.SetText(String("aucune partie en cours"));
		return;
	}
	m_outlinerCount.SetText(String::Format("%zu entité(s) — %s", m_entries.size(), Game()->CurrentDimension().CStr()));
	Option<EntityCategory> group = NONE;
	Option<voxel::ChunkPos> chunk = NONE;
	for (const OutlinerEntry &e : m_entries) {
		if (group.IsNone() || group.Value() != e.category) {
			group = Some(e.category);
			chunk = NONE;
			m_outlinerList.Append(ui::Label(String(EntityCategoryLabel(e.category))).Class("studio-group"));
		}
		if (chunk.IsNone() || !(chunk.Value() == e.chunk)) {
			chunk = Some(e.chunk);
			m_outlinerList.Append(ui::Label(String("  chunk ") + e.chunk.ToString()).Class("studio-status"));
		}
		const ecs::Entity entity = e.entity;
		ui::WidgetBuilder row = ui::Label(String("    ") + e.name + (e.dead ? String(" (mort)") : String()))
									.Class("studio-row")
									.Tooltip(e.id)
									.OnClick([this, entity] { Select(entity); })
									.OnDoubleClick([this, entity] {
										Select(entity);
										FocusOn(entity);
									});
		if (entity == m_selected)
			row.Class("selected");
		if (e.dead)
			row.Class("studio-dead");
		m_outlinerList.Append(row);
	}
}

void Studio::SelectBlock(Option<voxel::BlockPos> pos) {
	const bool same = pos.IsSome() == m_selectedBlock.IsSome() && (pos.IsNone() || pos.Value() == m_selectedBlock.Value());
	if (same)
		return;
	m_selectedBlock = pos;
	m_inspectorSignature = String();
	if (pos.IsSome()) {
		m_rightDock.SetActive(0);
		Visit("inspector.block");
	}
}

void Studio::RefreshInspector() {
	voxel::Game *game = Game();
	std::vector<InspectorSection> sections;
	if (game && m_selected.Valid())
		sections = InspectEntity(*game, m_selected);
	else if (game && m_selectedBlock.IsSome())
		sections = InspectBlock(*game, m_selectedBlock.Value());
	String signature = m_selected.Valid() ? String::Format("e%u", unsigned(m_selected.id)) : String("b");
	for (const InspectorSection &s : sections) {
		signature += s.title + String("{");
		for (const InspectorRow &r : s.rows)
			signature += r.label + String(";");
		signature += String("}");
	}
	if (signature != m_inspectorSignature) {
		// Structure changée (autre entité, composant ajouté) : reconstruction.
		m_inspectorSignature = signature;
		m_inspector.Clear();
		m_inspectorRows.clear();
		if (sections.empty())
			m_inspector.Append(ui::Label(String("Sélectionnez une entité ou un bloc (outliner, clic dans la vue).")).Class("studio-status"));
		for (const InspectorSection &s : sections) {
			m_inspector.Append(ui::Label(s.title).Class("studio-section"));
			for (const InspectorRow &r : s.rows) {
				ui::Widget value;
				if (r.field.IsEmpty()) {
					m_inspector.Append(ui::Row().Class("prop-row").Children({
						ui::Label(r.label).Class("studio-key"),
						ui::Label(r.value).Class("studio-value").Ref(value),
					}));
				} else {
					// Valeur modifiable : Entrée applique (commande, donc
					// journal et rapport comme toute action).
					const String field = r.field;
					m_inspector.Append(ui::Row().Class("prop-row").Children({
						ui::Label(r.label).Class("studio-key"),
						ui::Input(String(), r.value).Class("studio-edit").Class("prop-field").Id(String("inspect-") + field).Grow().Ref(value)
							.OnSubmit([this, field](const String &text) {
								(void)Execute(String("studio.inspect.set ") + field + String(" \"") + text + String("\""));
								m_inspectorSignature = String(); // relu tel que le jeu l'a pris
							}),
					}));
				}
				m_inspectorRows.push_back({r.label, value, !r.field.IsEmpty()});
			}
		}
		if (!sections.empty())
			Visit("inspector.show");
		return;
	}
	// Même structure : seules les valeurs changent (60 Hz, sans reconstruire) ;
	// un champ en cours de saisie n'est pas écrasé.
	size_t i = 0;
	for (const InspectorSection &s : sections)
		for (const InspectorRow &r : s.rows) {
			if (i < m_inspectorRows.size()) {
				RowView &row = m_inspectorRows[i];
				if (!(row.editable && row.value.IsFocused()) && row.value.Text() != r.value)
					row.value.SetText(r.value);
			}
			++i;
		}
}

void Studio::RebuildConsole() {
	m_consoleLog.Clear();
	const auto &lines = m_console.Lines();
	for (size_t index : m_console.Filtered(m_consoleKinds)) {
		const bool marked = m_console.IsBookmarked(index);
		ui::WidgetBuilder b = ui::Label(marked ? String("★ ") + lines[index].text : lines[index].text)
								  .Class(String("studio-log-") + lines[index].kind)
								  .OnClick([this, index] {
									  m_console.Bookmark(index);
									  m_consoleDirty = true;
									  Visit("console.bookmark");
								  });
		if (marked)
			b.Class("studio-log-bookmark");
		m_consoleLog.Append(b);
	}
	m_consoleShown = lines.size();
	m_consoleDirty = false;
}

void Studio::RefreshConsole() {
	const auto &lines = m_console.Lines();
	if (m_consoleDirty || !m_consoleKinds.empty() || m_consoleShown > lines.size()) {
		if (m_consoleDirty || m_consoleShown != lines.size())
			RebuildConsole();
	} else {
		for (size_t i = m_consoleShown; i < lines.size(); ++i) {
			const size_t index = i;
			m_consoleLog.Append(ui::Label(lines[i].text).Class(String("studio-log-") + lines[i].kind).OnClick([this, index] {
				m_console.Bookmark(index);
				m_consoleDirty = true;
			}));
		}
		if (lines.size() >= 500 && m_consoleShown == lines.size()) {
			// Console pleine : le compte ne bouge plus, la dernière ligne change.
			if (!lines.empty() && m_consoleLog.Children().size() && m_consoleLog.Children().back().Text() != lines.back().text)
				RebuildConsole();
		}
		m_consoleShown = lines.size();
	}
	// Code d'état → visibilité : fermée = seule la ligne de saisie reste.
	if (m_console.Visible())
		m_consoleLog.Show();
	else
		m_consoleLog.Hide();
}

void Studio::RefreshToolbar() {
	Session *session = m_shell.Game();
	m_pauseButton.SetText(String(m_shell.SimulationHeld() ? "Reprendre" : "Pause"));
	m_diagnosticButton.SetText(m_shell.DiagnosticRunning() ? String::Format("■ %.0f s", m_shell.DiagnosticSeconds()) : String("● Diag."));
	m_followButton.SetText(String(m_follow ? "Libérer" : "Suivre"));
	for (int i = 0; i < 3; ++i)
		if (m_gizmoButtons[i].Valid()) {
			const bool active = int(m_gizmo.mode) == i;
			if (active != m_gizmoButtons[i].HasClass(StringView("active"))) {
				if (active)
					m_gizmoButtons[i].AddClass(String("active"));
				else
					m_gizmoButtons[i].RemoveClass("active");
			}
		}
	String status;
	if (!session || !session->Ready()) {
		status = String("menu — aucune partie en cours");
	} else {
		WorldRenderer &world = session->Renderer();
		const int threads = 1 + (world.Threaded() ? m_shell.Opts().meshThreads : 0) + (m_shell.Opts().threaded ? 1 : 0);
		if (m_report)
			m_report->Threads(threads);
		status = String::Format("%s · %.0f img/s · %zu chunks en attente · simulation %.2f ms · maillage %.2f ms · %d fils%s",
								Game()->CurrentDimension().CStr(), double(session->Fps()), world.PendingChunks(),
								double(session->TickMilliseconds()), double(world.MeshMilliseconds()), threads,
								m_shell.SimulationHeld() ? " · EN PAUSE" : "");
		if (m_showStats) {
			const ui::FrameTimings &t = m_window.Gui().Timings();
			m_stats.SetText(String::Format("interface du studio : styles %.2f ms, mise en page %.2f ms, vue 3D %.2f ms, dessin %.2f ms · "
										   "%zu chunks maillés, %zu triangles, %zu en cours de maillage",
										   t.styleMs, t.layoutMs, t.viewport3dMs, t.drawMs, world.ChunkMeshCount(),
										   world.TriangleCount(), world.MeshingChunks()));
		}
	}
	if (m_shell.DiagnosticRunning())
		status += String::Format(" · ● diagnostic %.0f s (Outils › Arrêter le diagnostic)", m_shell.DiagnosticSeconds());
	if (m_frame.Root().Valid())
		m_frame.SetStatus(status);
	else if (m_stats.Valid() && !m_showStats)
		m_stats.SetText(status);
}



} // namespace voxelia::studio
