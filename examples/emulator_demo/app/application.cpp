#include "application.hpp"

#include <algorithm>

#include "sdl3/filesystem.hpp"
#include "sdl3/image.hpp"

#include "../emulator/settings.hpp"
#include "modals.hpp"
#include "rom_metadata.hpp"

namespace emulator_demo::app {

namespace {

constexpr float TITLE_BAR_HEIGHT = 32.f;
constexpr float SIDEBAR_WIDTH = 250.f;
constexpr float LOG_HEIGHT = 130.f;
constexpr float FONT_POINT_SIZE = 13.f;
constexpr uint64_t TARGET_FRAME_MS = 1000 / 60;

/// `--frames` compte des images ÉMULÉES : si l'émulation n'avance plus (cœur
/// bloqué), la boucle s'arrête quand même après cette marge d'images de
/// l'interface, en le signalant, plutôt que de tourner indéfiniment.
[[nodiscard]] long StallBudget(long frames) { return frames * 4 + 900; }

} // namespace

Application::Application(const CommandLine &options, RunReport &report, ThreadTracker &threads, LogConsole &log)
	: m_options(options), m_report(report), m_threads(threads), m_log(log), m_net(&threads) {
	m_hudVisible = options.showHud;
	m_screenshotDone.assign(options.screenshots.size(), false);
}

Application::~Application() {
	if (m_fileSinkId != 0)
		sdl3::LogRouter::Instance().RemoveSink(m_fileSinkId);
	DestroySession();
	for (ecs::Entity popup : m_menuPopups)
		if (popup.Valid())
			ui::DespawnTree(m_world, popup);
}

// ── Initialisation ───────────────────────────────────────────────────────────

Result<bool, String> Application::Initialize() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS |
										sdl3::init_flags::GAMEPAD |
										(m_options.audio ? sdl3::init_flags::AUDIO : sdl3::InitFlags(0)));
	if (!sdl)
		return Err(String::Format("initialisation SDL : %s", sdl.Error().CStr()));
	m_sdl = Some(std::move(sdl).Unwrap());
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);

	auto window = sdl3::Window::Create("EmulOS - emulator_demo", m_options.windowWidth, m_options.windowHeight,
									   sdl3::window_flags::RESIZABLE | sdl3::window_flags::BORDERLESS);
	if (!window)
		return Err(String::Format("création de la fenêtre : %s", window.Error().CStr()));
	m_window = Some(std::move(window).Unwrap());
	m_window.Value().SetPosition(SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

	// Pilote choisi par SDL : aucune fonctionnalité ne dépend d'un pilote
	// précis. Sur une machine sans GPU (xvfb), SDL retombe sur un rendu
	// logiciel — c'est ce qui permet de prendre de vraies captures sans écran.
	auto renderer = sdl3::Renderer::Create(m_window.Value(), nullptr);
	if (!renderer)
		return Err(String::Format("création du renderer : %s", renderer.Error().CStr()));
	m_renderer = Some(std::move(renderer).Unwrap());

	if (m_options.audio) {
		auto mixerContext = sdl3::MixerContext::Create();
		if (mixerContext.IsOk()) {
			m_mixerContext = Some(std::move(mixerContext).Unwrap());
			auto mixer = sdl3::Mixer::CreateDevice();
			if (mixer.IsOk())
				m_mixer = Some(std::move(mixer).Unwrap());
			else
				m_report.warnings.push_back(String::Format("audio indisponible : %s", mixer.Error().CStr()));
		} else {
			m_report.warnings.push_back(String::Format("SDL_mixer indisponible : %s", mixerContext.Error().CStr()));
		}
	}

	auto ttf = sdl3::TtfContext::Create();
	if (!ttf)
		return Err(String::Format("initialisation SDL_ttf : %s", ttf.Error().CStr()));
	m_ttf = Some(std::move(ttf).Unwrap());
	auto font = sdl3::Font::FindLocal({"DejaVuSans", "LiberationSans-Regular", "NotoSans-Regular", "FreeSans", "Arial"},
									  FONT_POINT_SIZE);
	if (!font)
		return Err(String::Format("aucune police système trouvée : %s", font.Error().CStr()));
	m_font = Some(std::move(font).Unwrap());
	auto engine = sdl3::TextEngine::Create(m_renderer.Value());
	if (!engine)
		return Err(String::Format("moteur de texte : %s", engine.Error().CStr()));
	m_textEngine = Some(std::move(engine).Unwrap());

	m_ui = std::make_unique<ui::Ui>(m_world, m_window.Value(), m_renderer.Value(), ui::UiTheme::Dark());
	// Enregistrée depuis son emplacement DÉFINITIF (m_font) : RenderSystem
	// mémorise un pointeur.
	m_ui->SetTextEngine(m_textEngine.Value(), m_font.Value());
	sdl3::Font *fontPtr = &m_font.Value();
	// Mesure ligne par ligne : les libellés multi-lignes (carte de session,
	// titres de bannière NDS) étaient mesurés comme une seule ligne et se
	// chevauchaient.
	m_ui->Layout().measureText = [fontPtr](const String &text, float fontSize) -> sdl3::FPoint {
		const float scale = fontSize / FONT_POINT_SIZE;
		float width = 0.f;
		int lines = 0;
		for (const String &line : text.Split('\n')) {
			++lines;
			float lineWidth = float(line.GetSize()) * fontSize * 0.55f;
			if (auto size = fontPtr->Measure(line.IsEmpty() ? String(" ") : line); size.IsSome())
				lineWidth = float(size.Unwrap().x) * scale;
			width = std::max(width, lineWidth);
		}
		return {width, fontSize * 1.35f * float(std::max(lines, 1))};
	};

	m_gamepads.OpenConnectedPads();
	SetLogFilePath(Settings::getLogFilePath());
	BuildShell();
	return Ok(true);
}

void Application::SetLogFilePath(const String &path) {
	if (m_fileSinkId != 0) {
		sdl3::LogRouter::Instance().RemoveSink(m_fileSinkId);
		m_fileSinkId = 0;
	}
	m_fileLogSink.reset(); // vide et joint le fil d'écriture précédent
	Settings::setLogFilePath(path);
	if (path.IsEmpty())
		return;
	// Le dossier par défaut (<exe>/logs) n'existe pas au premier lancement :
	// l'ancienne version échouait alors à chaque démarrage.
	size_t slash = path.Rfind('/');
	if (slash != String::NPOS && slash > 0)
		(void)sdl3::filesystem::CreateDirectory(path.Substr(0, slash));
	auto sink = sdl3::FileLogSink::Create(path);
	if (sink.IsError()) {
		m_report.warnings.push_back(String::Format("journal fichier désactivé : %s", sink.Error().CStr()));
		return;
	}
	m_fileLogSink = std::move(sink).Unwrap();
	sdl3::FileLogSink *raw = m_fileLogSink.get();
	m_fileSinkId = sdl3::LogRouter::Instance().AddSink([raw](const sdl3::LogMessage &message) { raw->Write(message); });
}

// ── Construction de la coquille ─────────────────────────────────────────────

void Application::BuildShell() {
	ui::UiFactory &f = Factory();
	// Pas de fond ici : Run() efface déjà la fenêtre de cette couleur.
	ui::WidgetBuilder root = f.Column();
	root.GrowW().GrowH();
	m_shellRoot = root.Spawn();

	BuildTitleBar(m_shellRoot);
	BuildMenuBar(m_shellRoot);

	ui::WidgetBuilder body = f.Row();
	body.GrowW().GrowH().Parent(m_shellRoot);
	ecs::Entity bodyEntity = body.Spawn();
	BuildSidebar(bodyEntity);
	BuildViewport(bodyEntity);
	BuildLogPanel(m_shellRoot);
	BuildStatusBar(m_shellRoot);
}

void Application::BuildTitleBar(ecs::Entity parent) {
	// Barre de titre du module ui : icône, titre, poignée de déplacement,
	// réduire / agrandir-restaurer / fermer en icônes MaterialIcons (repli
	// texte sans la police).
	ui::RenderSystem &render = Gui().RenderSystem();
	if (!render.HasFont(ui::Glyphs::FontFamily<ui::MaterialIcons>())) {
		m_iconFont = ui::OpenMaterialIconFont();
		if (m_iconFont.IsSome())
			render.RegisterFont(ui::Glyphs::FontFamily<ui::MaterialIcons>(), m_iconFont.Value());
	}
	ui::TitleBarOptions options;
	options.title = "EmulOS";
	options.appIcon = Some(ui::MaterialIcons::VIDEOGAME_ASSET);
	options.icons = render.HasFont(ui::Glyphs::FontFamily<ui::MaterialIcons>());
	options.height = TITLE_BAR_HEIGHT;
	options.titleSize = 14.f;
	options.background = BG_DARK();
	options.onClose = [this] { RequestQuit(); };
	m_titleBar = ui::TitleBar(Factory(), Window(), std::move(options), parent);
	m_titleLabel = m_titleBar.title;
}

void Application::BuildStatusBar(ecs::Entity parent) {
	ui::StatusBarOptions options;
	options.text = "F5 : sauvegarde rapide · F9 : chargement rapide · F12 : capture d'écran · P : pause";
	options.icons = Gui().RenderSystem().HasFont(ui::Glyphs::FontFamily<ui::MaterialIcons>());
	options.background = BG_DARK();
	m_statusBar = ui::StatusBar(Factory(), std::move(options), parent);
	// Fenêtre sans bordure : la barre de titre la déplace, les bords et la
	// poignée de la barre d'état la redimensionnent.
	m_chrome.Attach(Window(), World(), Gui().Layout(), m_titleBar, &m_statusBar);
}

void Application::BuildMenuBar(ecs::Entity parent) {
	ui::UiFactory &f = Factory();
	ui::WidgetBuilder bar = f.MenuBar();
	bar.Parent(parent).GrowW().HAuto().Bg(BG_DARK());
	ecs::Entity barEntity = bar.Spawn();

	auto item = [&f](const char *text, const char *shortcut, std::function<void()> action) {
		ui::WidgetBuilder builder = f.MenuItem(String(text), String(shortcut));
		builder.OnClick(std::move(action));
		return builder;
	};

	m_menuPopups.push_back(f.Menu(
		barEntity, "Fichier", item("Charger une ROM…", "Ctrl+O", [this] { OpenModal("roms"); }),
		item("Ouvrir un fichier…", "", [this] { OpenRomFileDialog(*this); }),
		item("États sauvegardés…", "", [this] { OpenModal("saves"); }),
		item("Sauvegarde rapide", "F5", [this] { QuickSave(); }),
		item("Chargement rapide", "F9", [this] { QuickLoad(); }), item("Quitter", "Échap", [this] { RequestQuit(); })));
	m_menuPopups.push_back(f.Menu(barEntity, "Émulation", item("Pause / reprise", "P", [this] { TogglePause(); }),
								  item("Réinitialiser", "Ctrl+R", [this] { ResetGame(); }),
								  item("Arrêter", "", [this] { StopGame(); })));
	m_menuPopups.push_back(f.Menu(barEntity, "Configuration",
								  item("Contrôles…", "", [this] { OpenModal("config"); }),
								  item("Vidéo et système…", "", [this] { OpenModal("config"); })));
	m_menuPopups.push_back(f.Menu(barEntity, "Outils", item("Réseau…", "", [this] { OpenModal("network"); }),
								  item("Codes de triche…", "", [this] { OpenModal("cheats"); }),
								  item("Capture d'écran", "F12", [this] {
									  RequestScreenshot(String::Format("%s/capture-%06ld.png",
																	   m_options.screenshotDir.CStr(), DisplayedFrame()));
								  })));
	m_menuPopups.push_back(
		f.Menu(barEntity, "Affichage", item("Bandeau de performances", "", [this] { SetHudVisible(!m_hudVisible); }),
			   item("Écrans NDS : côte à côte / empilés", "R",
					[] { Settings::setScreenLayout(Settings::getScreenLayout() == 0 ? 1 : 0); }),
			   item("Journal", "", [this] { SetHidden(m_logBody, !World().HasComponent<ui::UiHidden>(m_logBody)); })));
	m_menuPopups.push_back(f.Menu(barEntity, "Aide", item("À propos d'EmulOS…", "", [this] { OpenModal("about"); })));
}

void Application::BuildSidebar(ecs::Entity parent) {
	ui::UiFactory &f = Factory();
	ui::WidgetBuilder sidebar = f.Column();
	sidebar.W(ui::Dimension::Px(SIDEBAR_WIDTH)).GrowH().Bg(SIDEBAR_BG()).Parent(parent);
	ecs::Entity sidebarEntity = sidebar.Spawn();

	ui::WidgetBuilder header = f.Row();
	header.H(ui::Dimension::Px(28.f)).GrowW().Bg(HEADER_BLUE()).Pad(sdl3::Sides(10.f, 4.f)).Parent(sidebarEntity);
	header.Children(f.Label("État du système").FontSize(14.f).TextColor(TEXT_PRIMARY()));
	(void)header.Spawn();

	ui::WidgetBuilder rows = f.Column();
	rows.GrowW().Pad(10.f).Gap(8.f).Parent(sidebarEntity);
	ecs::Entity rowsEntity = rows.Spawn();

	for (RomSystem system : {RomSystem::NDS, RomSystem::GBA, RomSystem::GBC}) {
		ui::WidgetBuilder row = f.Column();
		row.GrowW().Bg(ROW_BG()).Radius(4.f).Pad(8.f).Gap(2.f).Parent(rowsEntity);
		ecs::Entity rowEntity = row.Spawn();
		ui::WidgetBuilder name = f.Label(String(RomSystemName(system)));
		name.FontSize(14.f).TextColor(TEXT_PRIMARY()).Parent(rowEntity);
		(void)name.Spawn();
		ui::WidgetBuilder status = f.Label("Aucune ROM chargée");
		status.FontSize(12.f).TextColor(TEXT_MUTED()).WAuto().HAuto().Parent(rowEntity);
		m_systemStatus.emplace_back(system, status.Spawn());
	}

	ui::WidgetBuilder info = f.Column();
	info.GrowW().Bg(ROW_BG()).Radius(4.f).Pad(8.f).Gap(4.f).Parent(rowsEntity);
	ecs::Entity infoEntity = info.Spawn();
	ui::WidgetBuilder title = f.Label("Session");
	title.FontSize(14.f).Bold().TextColor(TEXT_PRIMARY()).WAuto().HAuto().Parent(infoEntity);
	m_romTitle = title.Spawn();
	ui::WidgetBuilder details = f.Label("Glissez une ROM sur la fenêtre\nou Fichier > Charger une ROM…");
	details.FontSize(12.f).TextColor(TEXT_MUTED()).WAuto().HAuto().Parent(infoEntity);
	m_romDetails = details.Spawn();
}

void Application::BuildViewport(ecs::Entity parent) {
	ui::UiFactory &f = Factory();
	ui::WidgetBuilder viewport = f.Column();
	viewport.GrowW().GrowH().Bg(VIEWPORT_BG()).Clip().Parent(parent);
	ecs::Entity viewportEntity = viewport.Spawn();

	// Le jeu est dessiné PAR l'arbre d'interface : tout ce qui est créé après
	// ce canevas (bandeau, message de pause, boîtes) passe au-dessus.
	ui::WidgetBuilder canvas = f.Canvas([this](sdl3::Renderer &renderer, sdl3::FRect rect) {
		if (m_gameView)
			m_gameView->Render(renderer, rect);
	});
	canvas.GrowW().GrowH().Parent(viewportEntity);
	(void)canvas.Spawn();

	ui::WidgetBuilder placeholder = f.Label("Glissez-déposez une ROM ou choisissez Fichier > Charger une ROM…");
	placeholder.FontSize(18.f).TextColor(TEXT_MUTED()).TextAlign(ui::TextAlign::Center);
	placeholder.Absolute().Anchor(ui::Anchor::Center).WAuto().HAuto().Parent(viewportEntity);
	m_placeholder = placeholder.Spawn();

	ui::WidgetBuilder hud = f.Row();
	hud.Absolute().Anchor(ui::Anchor::TopLeft).Offset(8.f, 8.f).WAuto().HAuto().Bg(HUD_BG());
	hud.Pad(sdl3::Sides(10.f, 4.f)).Radius(4.f).Hidden().Parent(viewportEntity);
	m_hud = hud.Spawn();
	ui::WidgetBuilder hudLabel = f.Label("—");
	hudLabel.FontSize(13.f).TextColor(sdl3::FColor(sdl3::Color{120, 255, 160, 255})).WAuto().HAuto().Parent(m_hud);
	m_hudLabel = hudLabel.Spawn();

	ui::WidgetBuilder pause = f.Label("PAUSE");
	pause.FontSize(28.f).Bold().TextColor(TEXT_PRIMARY()).Absolute().Anchor(ui::Anchor::Center).WAuto().HAuto();
	pause.Hidden().Parent(viewportEntity);
	m_pauseLabel = pause.Spawn();
}

void Application::BuildLogPanel(ecs::Entity parent) {
	ui::UiFactory &f = Factory();
	ui::WidgetBuilder header = f.Row();
	header.H(ui::Dimension::Px(24.f)).GrowW().Bg(HEADER_BLUE()).Pad(sdl3::Sides(8.f, 2.f)).Gap(6.f);
	header.Align(ui::CrossAlign::Center).Parent(parent);
	ecs::Entity headerEntity = header.Spawn();

	ui::WidgetBuilder title = f.Label("Journal");
	title.FontSize(13.f).TextColor(TEXT_PRIMARY()).GrowW().Parent(headerEntity);
	(void)title.Spawn();
	ui::WidgetBuilder copy = f.Button("Copier");
	copy.WAuto().H(ui::Dimension::Px(20.f)).FontSize(11.f).Tooltip("Copie tout le journal dans le presse-papiers");
	copy.Parent(headerEntity).OnClick([this] { (void)sdl3::clipboard::SetText(m_log.AllText()); });
	(void)copy.Spawn();
	ui::WidgetBuilder collapse = f.Button("Replier");
	collapse.WAuto().H(ui::Dimension::Px(20.f)).FontSize(11.f).Parent(headerEntity);
	collapse.OnClick([this] { SetHidden(m_logBody, !World().HasComponent<ui::UiHidden>(m_logBody)); });
	(void)collapse.Spawn();

	ui::WidgetBuilder body = f.Column();
	body.H(ui::Dimension::Px(LOG_HEIGHT)).GrowW().Bg(sdl3::FColor(sdl3::Color{10, 12, 15, 255})).Parent(parent);
	m_logBody = body.Spawn();
	ui::WidgetBuilder view = f.InputArea("(journal vide)");
	view.IoMode(ui::IOMode::READ_AND_COPY_ONLY).FontSize(12.f).GrowW().GrowH().Parent(m_logBody);
	m_logView = view.Spawn();
}

// ── Petites aides ────────────────────────────────────────────────────────────

void Application::SetLabel(ecs::Entity entity, const String &text) {
	if (auto label = m_world.GetComponent<ui::UiLabel>(entity); label.IsSome() && label.Unwrap()->text != text) {
		label.Unwrap()->text = text;
		m_ui->Layout().MarkDirty();
	}
}

void Application::SetHidden(ecs::Entity entity, bool hidden) {
	if (!entity.Valid())
		return;
	bool isHidden = m_world.HasComponent<ui::UiHidden>(entity);
	if (hidden == isHidden)
		return;
	if (hidden)
		m_world.AddComponent(entity, ui::UiHidden{});
	else
		m_world.RemoveComponent<ui::UiHidden>(entity);
	m_ui->Layout().MarkDirty();
}

void Application::SetHudVisible(bool visible) { m_hudVisible = visible; }

Application::ModalFrame Application::BeginModal(const char *title, float width, float height) {
	ui::UiFactory &f = Factory();
	ui::WidgetBuilder panel = f.Panel();
	panel.Size(width, height).Gap(0.f).Pad(0.f).Bg(DIALOG_BG());
	ui::WidgetBuilder modal = f.Modal(std::move(panel));
	ModalFrame frame;
	frame.root = modal.Spawn();

	// Modal() enveloppe le panneau : [voile, panneau].
	ecs::Entity dialog{};
	if (auto children = m_world.GetComponent<ui::UiChildren>(frame.root);
		children.IsSome() && children.Unwrap()->list.size() == 2)
		dialog = children.Unwrap()->list[1];

	ui::WidgetBuilder header = f.Row();
	header.H(ui::Dimension::Px(32.f)).GrowW().Bg(HEADER_BLUE()).Pad(sdl3::Sides(12.f, 4.f)).Align(ui::CrossAlign::Center);
	header.Parent(dialog);
	ecs::Entity headerEntity = header.Spawn();
	ui::WidgetBuilder titleLabel = f.Label(String(title));
	titleLabel.FontSize(14.f).Bold().TextColor(TEXT_PRIMARY()).GrowW().Parent(headerEntity);
	(void)titleLabel.Spawn();
	ui::WidgetBuilder close = f.Button("Fermer");
	close.WAuto().H(ui::Dimension::Px(24.f)).FontSize(12.f).Parent(headerEntity).OnClick([this] { CloseModal(); });
	(void)close.Spawn();

	ui::WidgetBuilder body = f.Column();
	body.GrowW().GrowH().Pad(12.f).Gap(10.f).Parent(dialog);
	frame.body = body.Spawn();
	return frame;
}

// ── Actions ─────────────────────────────────────────────────────────────────

void Application::OpenModal(const String &name) {
	// Différé : l'appel vient souvent d'un rappel de widget, pendant que le
	// système d'entrée parcourt l'ECS — détruire/créer des entités à ce
	// moment-là est précisément ce qui plantait ui:: autrefois.
	m_deferred.push_back([this, name] {
		DoCloseModal();
		m_modalName = name;
		m_modalRoot = BuildModal(*this, name);
		if (!m_modalRoot.Valid()) {
			m_modalName.Clear();
			return;
		}
		m_ui->OpenModal(m_modalRoot);
		m_report.modalsOpened.push_back(name);
	});
}

void Application::CloseModal() {
	m_deferred.push_back([this] { DoCloseModal(); });
}

void Application::DoCloseModal() {
	if (!m_modalRoot.Valid())
		return;
	m_ui->CloseModal(m_modalRoot);
	ui::DespawnTree(m_world, m_modalRoot);
	m_modalRoot = ecs::Entity{};
	m_modalName.Clear();
	m_modalTicks.clear();
	m_modalEventHandlers.clear();
	m_ui->Layout().MarkDirty();
}

bool Application::SwitchToGame(const String &romPath, const String &romEntry, bool fromCommandLine) {
	DestroySession();

	SessionOptions sessionOptions;
	sessionOptions.threaded = true;
	sessionOptions.unlimitedSpeed = m_options.unlimitedSpeed;
	sessionOptions.mixer = m_mixer.IsSome() ? &m_mixer.Value() : nullptr;
	sessionOptions.netBridge = &m_net;
	sessionOptions.threads = &m_threads;
	sessionOptions.romEntry = romEntry;
	sessionOptions.extractDirectory = m_options.extractDir;
	if (fromCommandLine) {
		sessionOptions.statePath = m_options.statePath;
		sessionOptions.presses = m_options.presses;
		sessionOptions.stateActions = m_options.stateActions;
	}

	m_report.romPath = romPath;
	auto session = EmulatorSession::Create(romPath, std::move(sessionOptions));
	if (session.IsError()) {
		m_report.booted = false;
		m_report.bootError = session.Error();
		if (auto rom = LoadRom(romPath, romEntry); rom.IsOk())
			if (Option<RomMetadata> metadata = ReadRomMetadataFromBytes(rom.Value().bytes); metadata.IsSome())
				DescribeRomInReport(m_report, metadata.Value());
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Démarrage impossible (%s) : %s", romPath.CStr(),
					 session.Error().CStr());
		if (fromCommandLine)
			m_report.failure = String::Format("démarrage de la ROM impossible : %s", session.Error().CStr());
		return false;
	}
	m_session = std::move(session).Unwrap();
	m_report.booted = true;
	m_report.bootError.Clear();
	DescribeSessionInReport(m_report, *m_session);
	m_report.audioEnabled = m_session->HasAudio();
	m_gameView = std::make_unique<GameView>(Renderer(), *m_session);
	m_session->Start();
	m_sessionStartTicks = sdl3::GetTicksMS();
	m_lastHashedFrame = 0;
	m_distinctFrames = 0;
	SDL_Log("%s : partie lancée — %s", RomSystemName(m_session->System()), romPath.CStr());
	return true;
}

void Application::DestroySession() {
	if (!m_session)
		return;
	CollectReport();
	m_gameView.reset();
	m_session.reset(); // arrête le fil d'émulation et la pompe audio
}

void Application::StopGame() {
	m_deferred.push_back([this] { DestroySession(); });
}

void Application::ResetGame() {
	if (!m_session)
		return;
	String rom = m_session->RomPath();
	String entry = m_session->RomEntry();
	m_deferred.push_back([this, rom, entry] { (void)SwitchToGame(rom, entry); });
}

void Application::TogglePause() {
	if (m_session)
		m_session->SetPaused(!m_session->IsPaused());
}

void Application::QuickSave() {
	if (m_session)
		m_session->RequestQuickSave();
}

void Application::QuickLoad() {
	if (m_session)
		m_session->RequestQuickLoad();
}

void Application::RequestScreenshot(String path) { m_pendingScreenshots.push_back(std::move(path)); }

// ── Boucle ──────────────────────────────────────────────────────────────────

long Application::DisplayedFrame() const {
	return m_gameView ? m_gameView->Snapshot().frame : m_uiFrame;
}

void Application::Run() {
	if (!m_options.romPath.IsEmpty())
		(void)SwitchToGame(m_options.romPath, m_options.romEntry, true);

	const uint64_t tickRate = sdl3::GetPerformanceFrequency();
	uint64_t previous = sdl3::GetPerformanceCounter();
	bool firstFrame = true;
	while (m_running) {
		const uint64_t frameStart = sdl3::GetTicksMS();
		++m_uiFrame;

		while (auto event = sdl3::PollEvent()) {
			HandleEvent(event.Value());
			if (!m_running)
				break;
		}
		if (!m_running)
			break;

		if (m_uiFrame == 3 && !m_options.openModal.IsEmpty())
			OpenModal(m_options.openModal);

		// Pas de boutons de jeu pendant qu'une boîte a le focus (saisie d'un
		// code de triche, affectation d'une touche…).
		if (m_session)
			m_session->SetHostButtons(m_ui->HasOpenModal() ? 0u : m_gamepads.PressedMask());
		if (m_gameView)
			m_gameView->Update();

		UpdateWidgets();
		// Période réelle de la boucle (attente de cadence comprise) : c'est la
		// cadence d'affichage vue par l'utilisateur.
		const uint64_t now = sdl3::GetPerformanceCounter();
		const double period = double(now - previous) / double(tickRate);
		previous = now;
		if (!firstFrame)
			m_report.frames.Push(period);
		firstFrame = false;
		m_ui->Tick(float(period));
		m_chrome.Update();
		m_titleBar.Update(World(), Window());

		Renderer().SetDrawColor(BG_DARK());
		Renderer().Clear();
		m_ui->Render();
		const long displayed = DisplayedFrame();
		// Après le dessin et AVANT la présentation : seul instant où le tampon
		// contient l'image de cette boucle.
		CaptureScreenshots(displayed);
		Renderer().Present();
		m_net.Pump();

		if (Option<int> threads = ReadProcessThreadCount(); threads.IsSome()) {
			int peak = m_report.processThreadsPeak.IsSome() ? m_report.processThreadsPeak.Unwrap() : 0;
			if (threads.Unwrap() > peak)
				m_report.processThreadsPeak = threads;
		}

		if (m_options.frames > 0) {
			if (displayed >= m_options.frames) {
				m_running = false;
			} else if (m_session && m_uiFrame > StallBudget(m_options.frames)) {
				m_report.failedChecks.push_back(String::Format(
					"l'émulation n'a atteint que %ld image(s) sur %ld demandées", displayed, m_options.frames));
				m_running = false;
			}
		}

		const uint64_t elapsed = sdl3::GetTicksMS() - frameStart;
		if (elapsed < TARGET_FRAME_MS)
			SDL_Delay(uint32_t(TARGET_FRAME_MS - elapsed));
	}
	CollectReport();
}

void Application::HandleEvent(const sdl3::Event &event) {
	m_gamepads.HandleEvent(event);
	if (event.IsQuit()) {
		m_running = false;
		return;
	}
	if (event.IsDropFile() && event.Drop().data) {
		String path(event.Drop().data);
		m_deferred.push_back([this, path] {
			DoCloseModal();
			(void)SwitchToGame(path);
		});
		return;
	}
	// Une boîte de dialogue peut capturer l'évènement (affectation de touche).
	for (auto &handler : m_modalEventHandlers)
		if (handler(event))
			return;
	if (!m_ui->HasOpenModal()) {
		if (HandleShortcut(event))
			return;
		if (m_gameView && m_gameView->HandleMouse(event))
			return;
	}
	m_ui->HandleEvent(event);
	if (!m_deferred.empty()) {
		std::vector<std::function<void()>> actions;
		actions.swap(m_deferred);
		for (auto &action : actions)
			action();
	}
}

bool Application::HandleShortcut(const sdl3::Event &event) {
	if (!event.IsKeyDown())
		return false;
	const bool ctrl = (sdl3::keyboard::Mods() & SDL_KMOD_CTRL) != 0;
	switch (event.Keycode()) {
	case SDLK_ESCAPE:
		RequestQuit();
		return true;
	case SDLK_F5:
		QuickSave();
		return true;
	case SDLK_F9:
		QuickLoad();
		return true;
	case SDLK_F12:
		RequestScreenshot(
			String::Format("%s/capture-%06ld.png", m_options.screenshotDir.CStr(), DisplayedFrame()));
		return true;
	case SDLK_P:
		TogglePause();
		return true;
	case SDLK_O:
		if (ctrl) {
			OpenModal("roms");
			return true;
		}
		return false;
	case SDLK_R:
		if (ctrl) {
			ResetGame();
		} else {
			Settings::setScreenLayout(Settings::getScreenLayout() == 0 ? 1 : 0);
		}
		return true;
	default:
		return false;
	}
}

void Application::UpdateWidgets() {
	{
		std::lock_guard<std::mutex> lock(m_postMutex);
		for (auto &action : m_posted)
			m_deferred.push_back(std::move(action));
		m_posted.clear();
	}
	if (!m_deferred.empty()) {
		std::vector<std::function<void()>> actions;
		actions.swap(m_deferred);
		for (auto &action : actions)
			action();
	}
	// Échap ferme la boîte côté ui:: sans passer par CloseModal() : on range.
	if (m_modalRoot.Valid() && !m_ui->HasOpenModal())
		DoCloseModal();
	for (auto &tick : m_modalTicks)
		tick();

	const bool running = m_session != nullptr;
	SetHidden(m_placeholder, running);
	SetHidden(m_hud, !(running && m_hudVisible));
	SetHidden(m_pauseLabel, !(running && m_session->IsPaused()));

	for (auto &[system, label] : m_systemStatus) {
		String status = (running && m_session->System() == system)
							? String(m_session->IsPaused() ? "En pause" : "En cours d'exécution")
							: String("Aucune ROM chargée");
		SetLabel(label, status);
	}

	if (running) {
		const long frame = m_gameView->Snapshot().frame;
		if (m_uiFrame - m_lastCoreFpsSample >= 60 && frame > 120) {
			m_lastCoreFpsSample = m_uiFrame;
			int fps = m_session->CoreFps();
			if (fps > 0) {
				m_report.coreFpsMin = m_report.coreFpsMin == 0 ? fps : std::min(m_report.coreFpsMin, fps);
				m_report.coreFpsMax = std::max(m_report.coreFpsMax, fps);
			}
		}
		if (frame != m_lastHashedFrame && !m_gameView->Snapshot().pixels.empty()) {
			uint64_t hash = HashPixels(m_gameView->Snapshot().pixels);
			if (m_lastHashedFrame == 0 || hash != m_lastFrameHash)
				++m_distinctFrames;
			m_lastFrameHash = hash;
			m_lastHashedFrame = frame;
		}
		if (m_hudVisible)
			SetLabel(m_hudLabel,
					 String::Format("Fenêtre %.0f img/s · émulation %d img/s · image %ld%s", m_report.frames.RecentFps(),
									m_session->CoreFps(), frame, m_session->HasAudio() ? "" : " · muet"));
		String title = m_report.romTitle.IsEmpty() ? m_session->DisplayName() : m_report.romTitle;
		SetLabel(m_romTitle, title);
		SetLabel(m_romDetails, String::Format("%s · %s\nImage %ld · %d img/s\nÉtat : %s",
											  RomSystemName(m_session->System()),
											  m_report.romPublisher.IsEmpty() ? "éditeur inconnu"
																			  : m_report.romPublisher.CStr(),
											  frame, m_session->CoreFps(), m_session->StatePath().CStr()));
		SetLabel(m_titleLabel, String::Format("EmulOS — %s", title.Replace('\n', ' ').CStr()));
	} else {
		SetLabel(m_romTitle, "Session");
		SetLabel(m_romDetails, "Glissez une ROM sur la fenêtre\nou Fichier > Charger une ROM…");
		SetLabel(m_titleLabel, "EmulOS");
	}

	uint64_t revision = m_log.Revision();
	if (revision != m_logRevision) {
		m_logRevision = revision;
		if (auto area = m_world.GetComponent<ui::UiInputArea>(m_logView); area.IsSome())
			area.Unwrap()->text = m_log.AllText();
	}
}

void Application::CaptureScreenshots(long displayedFrame) {
	for (size_t i = 0; i < m_options.screenshots.size(); ++i) {
		if (!m_screenshotDone[i] && displayedFrame >= m_options.screenshots[i].frame) {
			m_screenshotDone[i] = true;
			m_pendingScreenshots.push_back(m_options.screenshots[i].path);
		}
	}
	if (m_options.screenshotEvery > 0 && displayedFrame >= m_lastAutoScreenshot + m_options.screenshotEvery) {
		m_lastAutoScreenshot = displayedFrame - displayedFrame % m_options.screenshotEvery;
		m_pendingScreenshots.push_back(
			String::Format("%s/emulator-%06ld.png", m_options.screenshotDir.CStr(), m_lastAutoScreenshot));
	}
	if (m_pendingScreenshots.empty())
		return;

	(void)sdl3::filesystem::CreateDirectory(m_options.screenshotDir);
	for (const String &path : m_pendingScreenshots) {
		ScreenshotRecord record;
		record.frame = displayedFrame;
		record.path = path;
		record.source = "fenêtre";
		size_t slash = path.Rfind('/');
		if (slash != String::NPOS && slash > 0)
			(void)sdl3::filesystem::CreateDirectory(path.Substr(0, slash));
		auto surface = Renderer().ReadPixels();
		if (!surface) {
			record.error = String(surface.Error());
		} else {
			auto saved = sdl3::ImgSavePng(surface.Value(), path);
			if (!saved)
				record.error = String(saved.Error());
			else
				record.written = true;
		}
		m_report.screenshots.push_back(std::move(record));
	}
	m_pendingScreenshots.clear();
}

void Application::CollectReport() {
	if (!m_session)
		return;
	m_report.emulatedFrames = m_session->EmulatedFrames();
	m_report.emulatedSeconds = double(m_report.emulatedFrames) / m_session->NativeRefreshRate();
	m_report.emulationWallSeconds = double(sdl3::GetTicksMS() - m_sessionStartTicks) / 1000.0;
	m_report.audioChunks = m_session->AudioChunks();
	m_report.stateOperations = m_session->StateOperations();
	m_report.inputEdges = m_session->InputEdges();
	FrameSnapshot last;
	(void)m_session->CopyLatestFrame(last);
	m_report.video = FingerprintFrame(last.pixels, last.width, last.height, last.frame);
	m_report.video.distinctFrameCount = m_distinctFrames;
}

} // namespace emulator_demo::app
