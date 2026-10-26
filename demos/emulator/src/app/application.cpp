#include <emulator/app/application.hpp>

#include <algorithm>

#include <sdl3/filesystem.hpp>
#include <sdl3/image.hpp>

#include <emulator/core/settings.hpp>
#include <emulator/app/modals.hpp>
#include <emulator/app/rom_metadata.hpp>

namespace emulator::app {

namespace {

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
}

const char *Application::ShellCss() {
	return R"css(
/* Palette « EmulOS » (maquettes d'origine) */
:root { font-size: 13px; }
window-frame { background-color: #0d1117; }
#titlebar, statusbar, menubar { background-color: #0d1117; }
.titlebar-title { font-size: 14px; }
#sidebar { width: 250px; background-color: #1e2128; }
.blue-header { height: 28px; background-color: #2563eb; padding: 4px 10px; gap: 6px; }
.blue-header > label { font-size: 14px; color: #e6e6eb; }
#system-rows { padding: 10px; gap: 8px; }
.card { background-color: #2a2e36; border-radius: 4px; padding: 8px; gap: 2px; }
.card-title { font-size: 14px; color: #e6e6eb; }
.muted { font-size: 12px; color: #969ba5; }
.primary { color: #e6e6eb; }
#viewport { background-color: #3a3d42; overflow: hidden; }
#placeholder { font-size: 18px; color: #969ba5; text-align: center; }
#hud { background-color: #0a0c0fc8; padding: 4px 10px; border-radius: 4px; }
#hud-label { font-size: 13px; color: #78ffa0; }
#pause { font-size: 28px; font-weight: bold; color: #e6e6eb; }
#log-header { height: 24px; padding: 2px 8px; }
#log-header > label { font-size: 13px; flex-grow: 1; }
.small-button { height: 20px; font-size: 11px; padding: 0 8px; }
#log-body { height: 130px; background-color: #0a0c0f; }
#log-view { font-size: 12px; }
.dialog { background-color: #181b21; padding: 0; gap: 0; border-radius: 6px; }
.dialog-header { height: 32px; background-color: #2563eb; padding: 4px 12px; }
.dialog-header > label { font-size: 14px; font-weight: bold; flex-grow: 1; }
.dialog-header > button { height: 24px; font-size: 12px; }
.dialog-body { flex-grow: 1; padding: 12px; gap: 10px; }
.dialog-body button, .dialog-row button { height: 28px; font-size: 13px; }
.dialog-row { gap: 8px; width: 100%; }
.note { font-size: 12px; color: #969ba5; text-overflow: wrap; width: 100%; }
.heading { font-size: 14px; }
.field { width: 100%; height: 28px; }
.config-page { padding: 8px; gap: 10px; overflow-y: auto; }
)css";
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

	auto window = sdl3::Window::Create("EmulOS - emulator", m_options.windowWidth, m_options.windowHeight,
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

	// Polices (DejaVu, icônes) chargées par l'interface elle-même.
	m_ui = std::make_unique<ui::Ui>(m_world, m_window.Value(), m_renderer.Value(), ui::Theme::Dark);
	if (m_ui->GetFonts().DefaultFamily().IsEmpty() || !m_ui->GetFonts().HasFamily(m_ui->GetFonts().DefaultFamily().View()))
		return Err(String("aucune police système trouvée"));
	m_ui->AddStyleSheet(ShellCss());

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
	// Fenêtre sans bordure : barre de titre et barre d'état du module ui
	// (déplacer, réduire / agrandir / fermer, redimensionner).
	m_frame.Build(*m_ui, Window(),
				  {.title = "EmulOS",
				   .appIcon = Some(ui::MaterialIcons::VIDEOGAME_ASSET),
				   .status = "F5 : sauvegarde rapide · F9 : chargement rapide · F12 : capture d'écran · P : pause",
				   .onClose = [this] { RequestQuit(); },
				   .titleHeight = Some(32.f)});
	ui::Widget content = m_frame.Content();
	content.Append(MenuBar());
	content.Append(ui::Row().Grow().FillWidth().Gap(0.f).AlignItems(ui::Align::Stretch).Children({Sidebar(), Viewport()}));
	content.Append(LogPanel());
}

ui::AnyWidget Application::MenuBar() {
	auto item = [](const char *text, const char *shortcut, std::function<void()> action) -> ui::AnyWidget {
		return ui::MenuItem(text, shortcut).OnClick(std::move(action));
	};
	return ui::MenuBar().FillWidth().Children({
		ui::Menu("Fichier").Children({
			item("Charger une ROM…", "Ctrl+O", [this] { OpenModal("roms"); }),
			item("Ouvrir un fichier…", "", [this] { OpenRomFileDialog(*this); }),
			item("États sauvegardés…", "", [this] { OpenModal("saves"); }),
			item("Sauvegarde rapide", "F5", [this] { QuickSave(); }),
			item("Chargement rapide", "F9", [this] { QuickLoad(); }),
			ui::MenuSeparator(),
			item("Quitter", "Échap", [this] { RequestQuit(); }),
		}),
		ui::Menu("Émulation").Children({
			item("Pause / reprise", "P", [this] { TogglePause(); }),
			item("Réinitialiser", "Ctrl+R", [this] { ResetGame(); }),
			item("Arrêter", "", [this] { StopGame(); }),
		}),
		ui::Menu("Configuration").Children({
			item("Contrôles…", "", [this] { OpenModal("config"); }),
			item("Vidéo et système…", "", [this] { OpenModal("config"); }),
		}),
		ui::Menu("Outils").Children({
			item("Réseau…", "", [this] { OpenModal("network"); }),
			item("Codes de triche…", "", [this] { OpenModal("cheats"); }),
			item("Capture d'écran", "F12",
				 [this] {
					 RequestScreenshot(
						 String::Format("%s/capture-%06ld.png", m_options.screenshotDir.CStr(), DisplayedFrame()));
				 }),
		}),
		ui::Menu("Affichage").Children({
			item("Bandeau de performances", "", [this] { SetHudVisible(!m_hudVisible); }),
			item("Écrans NDS : côte à côte / empilés", "R",
				 [] { Settings::setScreenLayout(Settings::getScreenLayout() == 0 ? 1 : 0); }),
			item("Journal", "", [this] { SetHidden(m_logBody, m_logBody.IsVisible()); }),
		}),
		ui::Menu("Aide").Children({item("À propos d'EmulOS…", "", [this] { OpenModal("about"); })}),
	});
}

ui::AnyWidget Application::Sidebar() {
	std::vector<ui::AnyWidget> cards;
	m_systemStatus.clear();
	m_systemStatus.reserve(3);
	for (RomSystem system : {RomSystem::NDS, RomSystem::GBA, RomSystem::GBC}) {
		m_systemStatus.emplace_back(system, ui::Widget());
		cards.push_back(ui::Column().Class("card").FillWidth().Children({
			ui::Label(RomSystemName(system)).Class("card-title"),
			ui::Label("Aucune ROM chargée").Class("muted").Ref(m_systemStatus.back().second),
		}));
	}
	cards.push_back(ui::Column().Class("card").FillWidth().Gap(4.f).Children({
		ui::Label("Session").Class("card-title").Bold().Ref(m_romTitle),
		ui::Label("Glissez une ROM sur la fenêtre\nou Fichier > Charger une ROM…").Class("muted").Ref(m_romDetails),
	}));
	return ui::Column().Id("sidebar").Children({
		ui::Row().Class("blue-header").FillWidth().Children({ui::Label("État du système")}),
		ui::Column().Id("system-rows").FillWidth().Children(std::move(cards)),
	});
}

ui::AnyWidget Application::Viewport() {
	// Le jeu est dessiné PAR l'arbre d'interface : tout ce qui vient après ce
	// canevas (bandeau, message de pause, boîtes) passe au-dessus.
	return ui::Stack().Id("viewport").Grow().Children({
		ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) {
			if (m_gameView && painter.Renderer())
				m_gameView->Render(*painter.Renderer(), rect);
		}).Fill(),
		ui::Label("Glissez-déposez une ROM ou choisissez Fichier > Charger une ROM…")
			.Id("placeholder")
			.Absolute()
			.Anchor(ui::Anchor::Center)
			.Ref(m_placeholder),
		ui::Row()
			.Id("hud")
			.Absolute()
			.Anchor(ui::Anchor::TopLeft)
			.Offset(8.f, 8.f)
			.Hidden()
			.Ref(m_hud)
			.Children({ui::Label("—").Id("hud-label").Ref(m_hudLabel)}),
		ui::Label("PAUSE").Id("pause").Absolute().Anchor(ui::Anchor::Center).Hidden().Ref(m_pauseLabel),
	});
}

ui::AnyWidget Application::LogPanel() {
	return ui::Column().FillWidth().Gap(0.f).Children({
		ui::Row().Id("log-header").Class("blue-header").FillWidth().Children({
			ui::Label("Journal"),
			ui::Button("Copier")
				.Class("small-button")
				.Tooltip("Copie tout le journal dans le presse-papiers")
				.OnClick([this] { (void)sdl3::clipboard::SetText(m_log.AllText()); }),
			ui::Button("Replier").Class("small-button").OnClick([this] { SetHidden(m_logBody, m_logBody.IsVisible()); }),
		}),
		ui::Column().Id("log-body").FillWidth().Ref(m_logBody).Children({
			ui::LogView().Id("log-view").Fill().Ref(m_logView),
		}),
	});
}

// ── Petites aides ────────────────────────────────────────────────────────────

void Application::SetLabel(ui::Widget widget, const String &text) {
	if (widget && widget.Text() != text)
		widget.SetText(text);
}

void Application::SetHidden(ui::Widget widget, bool hidden) {
	if (widget && widget.IsVisible() == hidden)
		widget.SetVisible(!hidden);
}

void Application::SetHudVisible(bool visible) { m_hudVisible = visible; }

Application::ModalFrame Application::BeginModal(const char *title, float width, float height) {
	ModalFrame frame;
	ui::Widget dialog;
	frame.root = m_ui->Mount(ui::Modal(ui::Panel().Class("dialog").Size(width, height).Ref(dialog).Children({
		ui::Row().Class("dialog-header").FillWidth().Children({
			ui::Label(title),
			ui::Button("Fermer").OnClick([this] { CloseModal(); }),
		}),
		ui::Column().Class("dialog-body").FillWidth(),
	})));
	frame.body = dialog.Find(".dialog-body").UnwrapOr(ui::Widget());
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
		if (!m_modalRoot) {
			m_modalName.Clear();
			return;
		}
		m_modalRoot.Open();
		m_report.modalsOpened.push_back(name);
	});
}

void Application::CloseModal() {
	m_deferred.push_back([this] { DoCloseModal(); });
}

void Application::DoCloseModal() {
	if (!m_modalRoot)
		return;
	m_modalRoot.Close();
	m_modalRoot.Destroy();
	m_modalRoot = ui::Widget();
	m_modalName.Clear();
	m_modalTicks.clear();
	m_modalEventHandlers.clear();
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
		m_ui->Update(float(period));
		m_frame.Update();
		if (m_frame.CloseRequested())
			m_running = false;

		Renderer().SetDrawColor(sdl3::Color{13, 17, 23, 255});
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
	if (m_modalRoot && !m_ui->HasOpenModal())
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
		if (const String full = String::Format("EmulOS — %s", title.Replace('\n', ' ').CStr());
			m_frame.TitleBar().title.Text() != full)
			m_frame.SetTitle(full);
	} else {
		SetLabel(m_romTitle, "Session");
		SetLabel(m_romDetails, "Glissez une ROM sur la fenêtre\nou Fichier > Charger une ROM…");
		if (m_frame.TitleBar().title.Text() != "EmulOS")
			m_frame.SetTitle("EmulOS");
	}

	uint64_t revision = m_log.Revision();
	if (revision != m_logRevision) {
		m_logRevision = revision;
		m_logView.SetText(m_log.AllText());
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

} // namespace emulator::app
