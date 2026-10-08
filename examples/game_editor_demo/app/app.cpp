#include "app.hpp"

namespace game_editor {

// ── App ──────────────────────────────────────────────────────────────────────

int App::Run() {
	if (m_options.showHelp) {
		std::fputs(CommandLine::HelpText(), stdout);
		return 0;
	}
	if (m_options.listScenarios) {
		PrintScenarios();
		return 0;
	}

	m_report.commandLine = m_commandLine;
	m_report.theme = m_options.theme;
	m_report.scenario = m_options.scenario;
	m_report.randomSeed = m_options.randomSeed;
	m_report.mode = m_options.headless ? String("sans écran") : String("fenêtré");

	if (m_options.listScenes) {
		PrintScenes();
		return 0;
	}
	return m_options.headless ? RunHeadless() : RunWindowed();
}

void App::PrintScenarios() const {
	std::fputs("Scénarios intégrés :\n", stdout);
	for (const Scenario &scenario : BuiltinScenarios())
		std::printf("  %-10s %s (≈%ld images)\n", scenario.name, scenario.description, scenario.suggestedFrames);
	std::fputs("\nUsage : --scenario=NOM [--frames=N] [--report=CHEMIN]\n", stdout);
}

void App::PrintScenes() {
	if (m_options.projectPath.IsEmpty()) {
		std::fputs("--list-scenes : indiquez le projet avec --project=CHEMIN\n", stderr);
		return;
	}
	const String manifest = files::ResolveManifest(ResolveProjectPath(m_options.projectPath));
	auto project = files::LoadProject(manifest);
	if (project.IsError()) {
		std::fprintf(stderr, "%s\n", project.Error().CStr());
		return;
	}
	std::printf("Projet « %s » — %d scènes :\n", project.Value().name.CStr(), int(project.Value().scenes.size()));
	for (const SceneDesc &scene : project.Value().scenes)
		std::printf("  %-24s %4d objets  %s\n", scene.name.CStr(), int(scene.NodeCount()),
					scene.description.CStr());
}

String App::ResolveProjectPath(const String &path) const {
	if (sdl3::filesystem::PathInfo(path).IsSome())
		return path;
	for (const String &base : {files::Join(m_options.savesDir, String("projects")), m_options.savesDir})
		if (const String candidate = files::Join(base, path); sdl3::filesystem::PathInfo(candidate).IsSome())
			return candidate;
	return path;
}

void App::OpenInitialProject(Runtime &runtime) {
	runtime.projectsRoot = files::Join(m_options.savesDir, String("projects"));
	if (m_options.projectPath.IsEmpty())
		return;
	auto loaded = runtime.LoadProjectFile(ResolveProjectPath(m_options.projectPath));
	if (loaded.IsError())
		m_report.warnings.push_back(String::Format("projet non ouvert : %s", loaded.Error().CStr()));
}

long App::FrameBudget() const {
	if (m_options.frames > 0)
		return m_options.frames;
	if (Option<Scenario> scenario = FindScenario(m_options.scenario); scenario.IsSome())
		return scenario.Unwrap().suggestedFrames;
	return 0;
}

bool App::InstallScenario(Runtime &runtime) {
	runtime.ToolVm().SetGlobal(String("shot_dir"), data::script::Value::Str(m_options.screenshotDir));
	runtime.ToolVm().SetGlobal(String("assets_dir"), data::script::Value::Str(m_options.assetsDir));
	runtime.ToolVm().SetGlobal(String("saves_dir"), data::script::Value::Str(m_options.savesDir));

	if (!m_options.scenario.IsEmpty()) {
		// Les scénarios parcourent les scènes d'un projet : sans projet
		// ouvert, ils ne vérifieraient rien.
		if (!runtime.HasProject()) {
			m_report.failure = String::Format("le scénario « %s » nécessite un projet (--project=CHEMIN)",
											  m_options.scenario.CStr());
			return false;
		}
		Option<Scenario> scenario = FindScenario(m_options.scenario);
		if (scenario.IsNone()) {
			m_report.failure = String::Format("scénario inconnu : %s (voir --list-scenarios)",
											  m_options.scenario.CStr());
			return false;
		}
		auto result = runtime.RunToolScript(String(scenario.Unwrap().source));
		if (result.IsError()) {
			m_report.failure = String::Format("scénario `%s` : %s", m_options.scenario.CStr(),
											  result.Error().Format().CStr());
			return false;
		}
		m_report.scriptsExecuted.push_back(String::Format("scénario:%s", m_options.scenario.CStr()));
	}

	if (!m_options.scriptPath.IsEmpty()) {
		auto source = data::script::LoadScriptFile(m_options.scriptPath);
		if (source.IsError()) {
			m_report.failure = source.Error();
			return false;
		}
		auto result = runtime.RunToolScript(source.Value());
		if (result.IsError()) {
			m_report.failure = String::Format("%s : %s", m_options.scriptPath.CStr(),
											  result.Error().Format().CStr());
			return false;
		}
		m_report.scriptsExecuted.push_back(m_options.scriptPath);
	}
	return true;
}

void App::InstallCommonHooks(Runtime &runtime) {
	runtime.SetRandomSeed(m_options.randomSeed);
	runtime.onQueryFps = [this] { return m_report.frames.RecentFps(); };
	runtime.onStatusMessage = [this](const String &text) {
		if (m_ui)
			m_ui->SetStatus(text);
		if (m_options.verbose)
			std::printf("[statut] %s\n", text.CStr());
	};
	if (m_options.verbose) {
		runtime.onLog = [](const LogEntry &entry) {
			std::printf("[%-6s] %s\n", entry.LevelName(), entry.text.CStr());
		};
	}
}

void App::TrackPlayState(Runtime &runtime) {
	bool playing = runtime.IsPlaying();
	if (playing && !m_wasPlaying) {
		PlaySessionRecord session;
		session.scene = runtime.ActiveScene() ? runtime.ActiveScene()->name : String();
		session.startFrame = m_frame;
		m_report.playSessions.push_back(session);
	} else if (!playing && m_wasPlaying && !m_report.playSessions.empty()) {
		PlaySessionRecord &session = m_report.playSessions.back();
		session.endFrame = m_frame;
		session.duration = double(m_frame - session.startFrame) / 60.0;
	}
	m_wasPlaying = playing;
}

void App::CollectGameplayCounters(Runtime &runtime) {
	static constexpr const char *COUNTERS[] = {"total_laps", "best_lap", "elapsed", "dropped", "shots"};
	for (const char *name : COUNTERS) {
		Option<data::script::Value> value = runtime.GameplayGlobal(String(name));
		if (value.IsSome() && !value.Unwrap().IsNil())
			m_report.gameplayCounters.emplace_back(String(name), value.Unwrap().ToDisplayString());
	}
}

void App::FinishReport(Runtime &runtime, uint64_t startTicks) {
	m_report.wallClockSeconds = double(sdl3::GetTicksMS() - startTicks) / 1000.0;
	m_report.peakThreads = m_threads.Peak();
	m_report.totalThreads = m_threads.TotalStarted();
	m_report.jobWorkers = int(runtime.Jobs().WorkerCount());
	m_report.jobPeakConcurrent = runtime.Jobs().PeakConcurrentWorkers();
	// Le vivier tourne EN PLUS du fil principal : le maximum simultané
	// global est donc le pic du suivi de fils, augmenté des tâches
	// réellement exécutées en parallèle par le vivier.
	m_report.peakThreads += m_report.jobPeakConcurrent;
	m_report.totalThreads += m_report.jobWorkers;
	m_report.scriptRuns = runtime.ScriptRunCount();
	m_report.scriptCalls = runtime.ScriptCallCount();
	m_report.scriptErrors = runtime.ScriptErrorCount();
	for (const String &script : runtime.LoadedScripts())
		m_report.scriptsExecuted.push_back(script);
	CollectGameplayCounters(runtime);
	m_report.CollectDiagnostics(runtime);
	m_report.completed = m_report.failure.IsEmpty();
}

void App::EmitReport(const Project &project) {
	if (m_options.verbose)
		std::fputs(m_report.ToText(project).CStr(), stdout);
	if (m_options.reportPath.IsEmpty())
		return;
	auto written = m_report.Write(project, m_options.reportPath, m_options.reportFormat);
	if (written.IsError())
		std::fprintf(stderr, "%s\n", written.Error().CStr());
	else
		std::printf("Rapport écrit : %s\n", m_options.reportPath.CStr());
}

int App::ExitCode() const {
	if (!m_report.failure.IsEmpty())
		return 1;
	return m_report.scriptErrors > 0 ? 1 : 0;
}

int App::RunHeadless() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::EVENTS);
	if (!sdl) {
		m_report.failure = String::Format("initialisation SDL : %s", sdl.Error().CStr());
		EmitReport(Project{});
		return 1;
	}

	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry, m_options.jobWorkers);
	InstallCommonHooks(runtime);
	OpenInitialProject(runtime);
	if (!m_options.scene.IsEmpty() && !runtime.SwitchScene(m_options.scene))
		m_report.warnings.push_back(String::Format("scène inconnue : %s", m_options.scene.CStr()));

	runtime.onScreenshot = [this](const String &path) {
		// Aucune image à capturer sans GPU : la demande est TRACÉE comme
		// non satisfaite plutôt qu'ignorée en silence.
		ScreenshotRecord record;
		record.frame = m_frame;
		record.path = path;
		record.error = String("mode sans écran : aucune image à capturer");
		m_report.screenshots.push_back(std::move(record));
		return false;
	};

	if (!InstallScenario(runtime)) {
		EmitReport(runtime.GetProject());
		return 1;
	}

	const uint64_t startTicks = sdl3::GetTicksMS();
	const long budget = FrameBudget() > 0 ? FrameBudget() : 600;
	constexpr float FIXED_DT = 1.f / 60.f;

	for (m_frame = 1; m_frame <= budget; ++m_frame) {
		uint64_t frameStart = sdl3::GetPerformanceCounter();
		runtime.CallToolHook(String("on_frame"), {data::script::Value::Number(double(m_frame))});
		uint64_t afterScenario = sdl3::GetPerformanceCounter();
		runtime.Update(FIXED_DT);
		TrackPlayState(runtime);

		const double tickRate = double(sdl3::GetPerformanceFrequency());
		m_report.phases.Add(PhaseStats::SCENARIO, double(afterScenario - frameStart) * 1000.0 / tickRate);
		m_report.phases.Add(PhaseStats::SIMULATION, runtime.SimulationMs());
		m_report.phases.Add(PhaseStats::PORTALS, runtime.PortalMs());
		m_report.phases.EndFrame();
		m_report.frames.Push(double(sdl3::GetPerformanceCounter() - frameStart) / tickRate);
	}
	if (runtime.IsPlaying())
		runtime.Stop();
	TrackPlayState(runtime);

	FinishReport(runtime, startTicks);
	EmitReport(runtime.GetProject());
	return ExitCode();
}

int App::RunWindowed() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS);
	if (!sdl)
		return Fail(String::Format("initialisation SDL : %s", sdl.Error().CStr()));
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf)
		return Fail(String::Format("initialisation TTF : %s", ttf.Error().CStr()));

	auto fontResult = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_POINT_SIZE);
	if (!fontResult)
		return Fail(String::Format("police introuvable : %s", fontResult.Error().CStr()));
	sdl3::Font &font = fontResult.Value();

	auto windowResult = sdl3::Window::Create(u8"Éditeur de jeux 2D/3D",
											 m_options.windowWidth, m_options.windowHeight,
											 ui::WindowFrame::WINDOW_FLAGS);
	if (!windowResult)
		return Fail(String::Format("création de la fenêtre : %s", windowResult.Error().CStr()));
	sdl3::Window &window = windowResult.Value();

	auto rendererResult = sdl3::Renderer::Create(window, "gpu");
	if (!rendererResult)
		return Fail(String::Format("création du renderer : %s", rendererResult.Error().CStr()));
	sdl3::Renderer &renderer = rendererResult.Value();

	auto canvasResult = render3d::Canvas::Create(renderer);
	if (!canvasResult)
		return Fail(String::Format("création du canvas 3D : %s", canvasResult.Error().CStr()));
	render3d::Canvas canvas = std::move(canvasResult.Value());

	auto engineResult = sdl3::TextEngine::Create(renderer);
	if (!engineResult)
		return Fail(String::Format("moteur de texte : %s", engineResult.Error().CStr()));
	sdl3::TextEngine &textEngine = engineResult.Value();

	ecs::ArchetypeRegistry registry;
	ui::UiTheme theme = EditorUi::ThemeByName(m_options.theme).UnwrapOr(ui::UiTheme::Studio());
	ui::Ui gui(registry, window, renderer, theme);
	gui.Initialize(canvas);
	gui.SetTextEngine(textEngine, font);
	gui.Layout().measureText = [&font](const String &text, float fontSize) -> sdl3::FPoint {
		if (auto size = font.Measure(text); size.IsSome())
			return {float(size.Unwrap().x) * (fontSize / FONT_POINT_SIZE), fontSize * 1.35f};
		return {float(text.GetSize()) * fontSize * 0.55f, fontSize * 1.3f};
	};
	// Déclarée APRÈS `ttf` (détruite avant lui) et enregistrée DEPUIS SON
	// EMPLACEMENT DÉFINITIF (cf. OpenIconFont).
	Option<sdl3::Font> iconFont = OpenIconFont();
	if (iconFont.IsSome())
		gui.RegisterFont(ui::Glyphs::FontFamily<ui::MaterialIcons>(), iconFont.Value());
	// Chasse fixe de l'éditeur de code et de la console (même règle de
	// durée de vie que la police d'icônes). Facultative : sans elle, le
	// code s'affiche en police proportionnelle.
	auto monoFont = sdl3::Font::FindLocal({"DejaVuSansMono", "LiberationMono-Regular", "NotoSansMono-Regular", "FreeMono", "Consolas"}, FONT_POINT_SIZE);
	if (monoFont) gui.RegisterMonospaceFont(monoFont.Value());

	Runtime runtime(registry, m_options.jobWorkers);
	InstallCommonHooks(runtime);
	runtime.AttachCanvas(canvas);
	OpenInitialProject(runtime);
	if (!m_options.scene.IsEmpty() && !runtime.SwitchScene(m_options.scene))
		m_report.warnings.push_back(String::Format("scène inconnue : %s", m_options.scene.CStr()));

	// Précompilation des pipelines pendant l'écran de chargement.
	if (!RunLoadingScreen(gui, registry, renderer, canvas, runtime))
		return ExitCode(); // fermeture demandée pendant le chargement : sortie propre

	EditorUi editorUi(runtime, gui);
	editorUi.SetRenderer(renderer);
	editorUi.SetWindow(window);
	editorUi.SetDirectories(m_options.assetsDir, m_options.savesDir);
	m_ui = &editorUi;
	(void)editorUi.SetTheme(m_options.theme);
	editorUi.Build();
	InstallWindowedHooks(runtime, editorUi);

	if (!InstallScenario(runtime)) {
		EmitReport(runtime.GetProject());
		return 1;
	}

	const long budget = FrameBudget();
	const uint64_t startTicks = sdl3::GetTicksMS();
	uint64_t previousTicks = sdl3::GetPerformanceCounter();
	bool running = true;

	while (running) {
		uint64_t now = sdl3::GetPerformanceCounter();
		double elapsed = double(now - previousTicks) / double(sdl3::GetPerformanceFrequency());
		previousTicks = now;
		// Un pas borné : après une pause du système (ou le tout premier
		// pas, qui inclut la construction de l'interface), un dt brut
		// ferait traverser les murs à tout corps en mouvement.
		float dt = float(elapsed > MAX_STEP_SECONDS ? MAX_STEP_SECONDS : elapsed);
		++m_frame;

		while (auto event = sdl3::PollEvent()) {
			sdl3::Event &e = event.Value();
			// Échap ne quitte plus : il sert à sortir du mode Jeu plein
			// écran, fermer une boîte de dialogue, désélectionner — comme
			// dans tout éditeur. On quitte par la fenêtre ou Ctrl+Q.
			const bool control = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
			if (e.IsQuit() || m_quitRequested || (control && e.IsKeyDown(SDLK_Q))) {
				running = false;
				break;
			}
			// L'interface d'abord : le widget qui a le focus y traite la
			// frappe et la consomme ; l'éditeur ne voit ensuite que ce
			// qu'elle a laissé passer (raccourcis F1–F12, Ctrl+S…).
			gui.HandleEvent(e);
			editorUi.HandleEvent(e);
		}
		if (!running || m_quitRequested)
			break;
		// Souris du jeu : capturée (relative, vue subjective), confinée à la
		// fenêtre ou libre — choisie par les scripts, la touche de bascule, ou
		// par défaut capturée en plein écran (cf. Runtime::EffectiveMouseMode).
		const Runtime::MouseMode mouseMode = runtime.EffectiveMouseMode();
		const bool relative = mouseMode == Runtime::MouseMode::CAPTURED;
		const bool grab = mouseMode == Runtime::MouseMode::CONFINED;
		if (window.RelativeMouseMode() != relative || window.MouseGrab() != grab) {
			(void)window.SetRelativeMouseMode(relative);
			(void)window.SetMouseGrab(grab);
			if (runtime.IsPlaying())
				runtime.LogInfo(String::Format("Souris du jeu : %s", mouseMode == Runtime::MouseMode::CAPTURED ? "capturée"
																	   : grab ? "confinée à la fenêtre"
																			  : "libre"));
		}

		// Chronomètre de phase : une lecture d'horloge entre chaque étage,
		// pour que le rapport dise OÙ part le temps (cf. PhaseStats).
		const double tickRate = double(sdl3::GetPerformanceFrequency());
		uint64_t mark = now;
		auto phase = [&mark, tickRate](PhaseStats &stats, int which) {
			uint64_t at = sdl3::GetPerformanceCounter();
			stats.Add(which, double(at - mark) * 1000.0 / tickRate);
			mark = at;
		};

		runtime.CallToolHook(String("on_frame"), {data::script::Value::Number(double(m_frame))});
		QueueCommandLineScreenshots();
		phase(m_report.phases, PhaseStats::SCENARIO);

		runtime.Update(dt);
		TrackPlayState(runtime);
		editorUi.Tick(dt, m_report.frames.RecentFps());
		gui.Tick(dt);
		// `Runtime::Update` mesure lui-même ses deux étages ; on les
		// reventile ici, et le reste de ce bloc (interface) passe dans
		// la simulation — il est du même ordre que le bruit de mesure.
		m_report.phases.Add(PhaseStats::PORTALS, runtime.PortalMs());
		mark = sdl3::GetPerformanceCounter();
		m_report.phases.Add(PhaseStats::SIMULATION, runtime.SimulationMs());

		renderer.SetDrawColor(sdl3::FColor::UI_APP_BG());
		renderer.Clear();
		gui.Render();
		const ui::UiFrameTimings &uiTimings = gui.LastFrameTimings();
		m_report.phases.Add(PhaseStats::UI_STYLE, uiTimings.styleMs);
		m_report.phases.Add(PhaseStats::UI_LAYOUT, uiTimings.layoutMs);
		m_report.phases.Add(PhaseStats::UI_VIEWPORT, uiTimings.viewport3dMs);
		m_report.phases.Add(PhaseStats::UI_EFFECTS, uiTimings.shaderEffectMs);
		m_report.phases.Add(PhaseStats::UI_DRAW, uiTimings.drawMs);
		mark = sdl3::GetPerformanceCounter();

		// Les captures sont prises APRÈS le dessin et AVANT la
		// présentation : c'est le seul instant où le tampon de rendu
		// contient bien l'image de CETTE frame (SDL n'en garantit plus
		// le contenu après Present).
		FlushScreenshots(renderer);
		renderer.Present();
		phase(m_report.phases, PhaseStats::PRESENT);
		m_report.phases.EndFrame();

		m_report.frames.Push(double(sdl3::GetPerformanceCounter() - now) /
							 double(sdl3::GetPerformanceFrequency()));

		if (budget > 0 && m_frame >= budget)
			running = false;
	}

	if (runtime.IsPlaying())
		runtime.Stop();
	TrackPlayState(runtime);
	m_ui = nullptr;

	FinishReport(runtime, startTicks);
	EmitReport(runtime.GetProject());
	return ExitCode();
}

void App::InstallWindowedHooks(Runtime &runtime, EditorUi &editorUi) {
	// Projet ouvert, créé ou fermé depuis un script : l'interface suit.
	runtime.onProjectChanged = [&editorUi] { editorUi.RequestRebuild(); };
	runtime.onScreenshot = [this](const String &path) {
		m_pendingScreenshots.push_back(path);
		return true;
	};
	runtime.onThemeChange = [&editorUi](const String &name) { return editorUi.SetTheme(name); };
	runtime.onPanelFocus = [&editorUi](const String &panel, int tab) { return editorUi.FocusPanel(panel, tab); };
	editorUi.onQuit = [this] { m_quitRequested = true; };
	runtime.onUiCommand = [&editorUi](const String &command, const String &argument) {
		return editorUi.UiCommand(command, argument);
	};
	editorUi.onExportReport = [this, &runtime] {
		if (m_options.reportPath.IsEmpty()) {
			runtime.LogWarning(String("Aucun chemin de rapport (--report=CHEMIN)"));
			return;
		}
		auto written = m_report.Write(runtime.GetProject(), m_options.reportPath, m_options.reportFormat);
		if (written.IsError())
			runtime.LogError(written.Error());
		else
			runtime.LogSuccess(String::Format("Rapport écrit : %s", m_options.reportPath.CStr()));
	};
}

Option<sdl3::Font> App::OpenIconFont() {
	auto loaded = sdl3::Font::Open(String("assets/fonts/") + ui::MATERIALICONS_FILENAME, 22.f);
	if (!loaded)
		return NONE;
	return Some(std::move(loaded.Value()));
}

void App::QueueCommandLineScreenshots() {
	for (const ScreenshotRequest &request : m_options.screenshots)
		if (request.frame == m_frame)
			m_pendingScreenshots.push_back(request.path);
}

void App::FlushScreenshots(sdl3::Renderer &renderer) {
	if (m_pendingScreenshots.empty())
		return;
	EnsureScreenshotDirectory();

	for (const String &path : m_pendingScreenshots) {
		ScreenshotRecord record;
		record.frame = m_frame;
		record.path = path;

		auto surface = renderer.ReadPixels();
		if (!surface) {
			record.error = String(surface.Error());
		} else {
			// IMG_SavePNG convertit lui-même le format de la surface : pas
			// de conversion manuelle ici, donc pas de risque de se tromper
			// d'ordre d'octets (cf. l'avertissement de Renderer::ReadPixels).
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

void App::EnsureScreenshotDirectory() {
	if (m_screenshotDirectoryReady || m_options.screenshotDir.IsEmpty())
		return;
	// Sans effet si le dossier existe déjà ; on n'insiste pas en cas
	// d'échec, l'écriture du PNG dira elle-même ce qui ne va pas.
	(void)sdl3::filesystem::CreateDirectory(m_options.screenshotDir);
	m_screenshotDirectoryReady = true;
}

bool App::RunLoadingScreen(ui::Ui &gui, ecs::ArchetypeRegistry &registry, sdl3::Renderer &renderer,
		render3d::Canvas &canvas, Runtime &runtime) {
	ui::UiFactory &factory = gui.Factory();
	ui::WidgetBuilder root = factory.Column();
	root.Anchor(ui::Anchor::Center).Gap(14.f).Pad(28.f).WAuto().HAuto().Children(
		factory.Label(String("Éditeur de niveau")).FontSize(22.f).Bold(),
		factory.Label(String("Compilation des pipelines de rendu…")).FontSize(14.f).Name("loadStage"),
		factory.Progress(0.f, 1.f, 0.f).Size(360.f, 12.f).Name("loadBar"));
	ecs::Entity loadingRoot = root.Spawn();

	auto mutexResult = sdl3::Mutex::Create();
	if (!mutexResult) {
		m_report.warnings.push_back(String("mutex indisponible : préchauffage GPU ignoré"));
		ui::DespawnTree(registry, loadingRoot);
		return true;
	}
	sdl3::Mutex mutex = std::move(mutexResult.Value());

	struct Progress {
		float fraction = 0.f;
		String stage;
		bool done = false;
		bool failed = false;
		String error;
	} progress;

	auto publish = [&mutex, &progress](float fraction, const char *stage, bool done = false, bool failed = false,
									   StringView error = StringView()) {
		sdl3::MutexGuard guard(mutex);
		progress.fraction = fraction;
		progress.stage = String(stage);
		progress.done = done;
		progress.failed = failed;
		if (!error.IsEmpty())
			progress.error = String(error);
	};

	render3d::Camera warmupCamera;
	warmupCamera.position = {0.f, 6.f, -16.f};
	warmupCamera.target = {0.f, 1.f, 0.f};

	auto threadResult = sdl3::Thread::Create(
		[&]() -> int {
			// Compté dans le suivi de parallélisme du rapport (cf.
			// ThreadTracker) : c'est CE fil qui fait passer le maximum
			// simultané de 1 à 2.
			ThreadTracker::Scope tracked(m_threads);
			publish(0.1f, "Préparation du GPU…");

			render3d::OffscreenTarget target;
			std::vector<uint8_t> pixels;
			auto sized = target.EnsureSize(canvas.Device(), 64, 64, ui::VIEWPORT3D_COLOR_FORMAT,
										   ui::VIEWPORT3D_DEPTH_FORMAT);
			if (!sized) {
				publish(0.f, "Échec", true, true, sized.Error());
				return 1;
			}
			publish(0.35f, "Compilation des pipelines (plastique / métal / PBR / non éclairé)…");
			auto rendered =
				render3d::RenderObjectToTexture(canvas, runtime.SceneRoot(), warmupCamera, target, pixels);
			if (!rendered) {
				publish(0.f, "Échec", true, true, rendered.Error());
				return 1;
			}
			publish(1.f, "Prêt", true, false);
			return 0;
		},
		String("game-editor-warmup"));

	if (!threadResult) {
		m_report.warnings.push_back(String("fil de préchauffage indisponible : démarrage direct"));
		ui::DespawnTree(registry, loadingRoot);
		return true;
	}
	sdl3::Thread warmupThread = std::move(threadResult.Value());

	bool running = true;
	while (running) {
		while (auto event = sdl3::PollEvent()) {
			const sdl3::Event &e = event.Value();
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}
			gui.HandleEvent(e);
		}

		float fraction;
		String stage;
		bool done;
		{
			sdl3::MutexGuard guard(mutex);
			fraction = progress.fraction;
			stage = progress.stage;
			done = progress.done;
		}
		if (auto entity = ui::FindByName(registry, "loadBar"); entity.IsSome())
			if (auto bar = registry.GetComponent<ui::UiProgress>(entity.Unwrap()); bar.IsSome())
				bar.Unwrap()->value = fraction;
		if (auto entity = ui::FindByName(registry, "loadStage"); entity.IsSome())
			if (auto label = registry.GetComponent<ui::UiLabel>(entity.Unwrap()); label.IsSome())
				label.Unwrap()->text = stage;

		gui.Tick(0.f);
		renderer.SetDrawColor(sdl3::FColor::UI_APP_BG());
		renderer.Clear();
		gui.Render(); // aucun widget Viewport3D vivant : ne touche pas `canvas`
		renderer.Present();

		if (done)
			break;
	}

	// Toujours joindre AVANT toute décision de sortie : ne jamais laisser
	// un fil GPU vivant pendant la destruction du canvas/de la fenêtre.
	warmupThread.Wait();
	ui::DespawnTree(registry, loadingRoot);

	bool failed;
	String error;
	{
		sdl3::MutexGuard guard(mutex);
		failed = progress.failed;
		error = progress.error;
	}
	if (failed)
		m_report.warnings.push_back(String::Format("préchauffage GPU : %s", error.CStr()));
	return running;
}

int App::Fail(String message) {
	std::fprintf(stderr, "%s\n", message.CStr());
	m_report.failure = std::move(message);
	EmitReport(Project{});
	return 1;
}

} // namespace game_editor
