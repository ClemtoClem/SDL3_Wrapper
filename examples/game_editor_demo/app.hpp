#pragma once
/**
 * game_editor::App — la coquille applicative : ligne de commande, fenêtre,
 * boucle d'images, captures d'écran, rapport d'exécution.
 *
 * Deux modes, UN SEUL chemin de logique :
 *
 *  - **fenêtré** : SDL + `render3d::Canvas` + interface `ui::`. Sous
 *    `xvfb-run`, ce mode tourne sans écran physique tout en produisant de
 *    VRAIES captures d'écran — c'est ainsi que la démo se vérifie.
 *  - **`--headless`** : ni fenêtre, ni GPU. Le document, la physique, les
 *    scripts, le rapport et le scénario tournent à l'identique ; seules les
 *    captures d'écran sont impossibles (et signalées comme telles dans le
 *    rapport plutôt que tues). Utile là où aucun serveur d'affichage n'est
 *    disponible du tout.
 *
 * Le SCÉNARIO est le même dans les deux cas : c'est ce qui garantit qu'une
 * vérification sans écran teste bien le même enchaînement que ce qu'un humain
 * verrait à l'écran.
 *
 * ── Écran de chargement asynchrone ──────────────────────────────────────
 * Chaque matériau distinct fait compiler son pipeline GPU au PREMIER rendu.
 * Sans précaution, la fenêtre reste figée plusieurs secondes à l'ouverture.
 * Un fil secondaire effectue donc un rendu hors écran jetable de la scène
 * entière (`RenderObjectToTexture` — tampons de commandes locaux, jamais la
 * chaîne d'échange de la fenêtre, donc licite hors du fil principal d'après
 * SDL_gpu.h) pendant que le fil principal continue de pomper les évènements
 * et d'afficher une barre de progression. Même raisonnement et même structure
 * que examples/ui_viewport3d_demo.cpp, dont ce fichier reprend le motif.
 */
#include <cstdio>
#include <vector>

#include "core/core.hpp"
#include "data/script.hpp"
#include "ecs/ecs.hpp"
#include "render3d/canvas.hpp"
#include "render3d/offscreen.hpp"
#include "sdl3/filesystem.hpp"
#include "sdl3/image.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include "cli.hpp"
#include "content.hpp"
#include "panels.hpp"
#include "report.hpp"
#include "runtime.hpp"
#include "scenarios.hpp"

namespace game_editor {

class App {
public:
	App(CommandLine options, String commandLine)
		: m_options(std::move(options)), m_commandLine(std::move(commandLine)) {}

	App(const App &) = delete;
	App &operator=(const App &) = delete;

	/// Point d'entrée unique. Rend le code de sortie du processus : 0 si tout
	/// s'est bien passé, 1 si une étape a échoué OU si un script a signalé une
	/// erreur (un scénario qui échoue doit faire échouer la commande, sinon
	/// il ne vérifie rien).
	[[nodiscard]] int Run() {
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

private:
	// ── Sorties d'information ────────────────────────────────────────────────

	void PrintScenarios() const {
		std::fputs("Scénarios intégrés :\n", stdout);
		for (const Scenario &scenario : BuiltinScenarios())
			std::printf("  %-10s %s (≈%ld images)\n", scenario.name, scenario.description, scenario.suggestedFrames);
		std::fputs("\nUsage : --scenario=NOM [--frames=N] [--report=CHEMIN]\n", stdout);
	}

	void PrintScenes() {
		Project project = LoadProjectOrDefault();
		std::printf("Projet « %s » — %d scènes :\n", project.name.CStr(), int(project.scenes.size()));
		for (const SceneDesc &scene : project.scenes)
			std::printf("  %-24s %4d objets  %s\n", scene.name.CStr(), int(scene.ObjectCount()),
						scene.description.CStr());
	}

	/// Charge `--project` s'il est donné, sinon le projet de démonstration
	/// intégré. Un projet illisible est signalé mais NE fait pas échouer :
	/// mieux vaut ouvrir l'éditeur sur la démo que refuser de démarrer.
	[[nodiscard]] Project LoadProjectOrDefault() {
		if (m_options.projectPath.IsEmpty())
			return MakeDemoProject();
		auto text = data::script::LoadScriptFile(m_options.projectPath);
		if (text.IsError()) {
			m_report.warnings.push_back(String::Format("projet illisible (%s) — projet de démonstration utilisé",
													   text.Error().CStr()));
			return MakeDemoProject();
		}
		auto project = Project::DecodeJson(text.Value());
		if (project.IsError()) {
			m_report.warnings.push_back(String::Format("projet invalide (%s) — projet de démonstration utilisé",
													   project.Error().CStr()));
			return MakeDemoProject();
		}
		return std::move(project).Unwrap();
	}

	/// Nombre d'images à jouer : `--frames` prime, sinon la durée conseillée
	/// du scénario, sinon 0 (boucle jusqu'à fermeture par l'utilisateur).
	[[nodiscard]] long FrameBudget() const {
		if (m_options.frames > 0)
			return m_options.frames;
		if (Option<Scenario> scenario = FindScenario(m_options.scenario); scenario.IsSome())
			return scenario.Unwrap().suggestedFrames;
		return 0;
	}

	// ── Scripts et scénarios ─────────────────────────────────────────────────

	/// Installe le scénario (intégré ou fichier) dans l'interpréteur outil.
	/// `false` si le nom/chemin demandé n'existe pas ou ne compile pas — le
	/// processus s'arrête alors, car poursuivre exécuterait un scénario vide
	/// en prétendant avoir testé quelque chose.
	[[nodiscard]] bool InstallScenario(Runtime &runtime) {
		runtime.ToolVm().SetGlobal(String("shot_dir"), data::script::Value::Str(m_options.screenshotDir));
		runtime.ToolVm().SetGlobal(String("assets_dir"), data::script::Value::Str(m_options.assetsDir));
		runtime.ToolVm().SetGlobal(String("saves_dir"), data::script::Value::Str(m_options.savesDir));

		if (!m_options.scenario.IsEmpty()) {
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

	/// Branche les fonctions que les scripts appellent sur l'hôte. En mode
	/// sans écran, `onScreenshot`/`onThemeChange`/`onPanelFocus` restent non
	/// installés : les fonctions de script rendent alors `false` (cf.
	/// runtime.hpp), ce qui laisse un même scénario s'exécuter dans les deux
	/// modes sans branche conditionnelle dans le script.
	void InstallCommonHooks(Runtime &runtime) {
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

	/// Suit les entrées/sorties du mode Jeu pour le rapport, sans que le
	/// runtime ait à connaître le rapport.
	void TrackPlayState(Runtime &runtime) {
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

	/// Relève les compteurs déclarés par le script de gameplay, pour que le
	/// rapport puisse affirmer « la voiture a bouclé N tours » — c'est ce qui
	/// fait d'un essai de jouabilité une VÉRIFICATION et pas une démo.
	void CollectGameplayCounters(Runtime &runtime) {
		static constexpr const char *COUNTERS[] = {"total_laps", "best_lap", "elapsed", "dropped", "shots"};
		for (const char *name : COUNTERS) {
			Option<data::script::Value> value = runtime.GameplayGlobal(String(name));
			if (value.IsSome() && !value.Unwrap().IsNil())
				m_report.gameplayCounters.emplace_back(String(name), value.Unwrap().ToDisplayString());
		}
	}

	void FinishReport(Runtime &runtime, uint64_t startTicks) {
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

	/// Écrit le rapport si `--report` a été donné, et l'affiche toujours en
	/// mode bavard.
	void EmitReport(const Project &project) {
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

	/// Code de sortie : un scénario dont un `assert` a échoué, ou un script
	/// en erreur, doit faire échouer la commande.
	[[nodiscard]] int ExitCode() const {
		if (!m_report.failure.IsEmpty())
			return 1;
		return m_report.scriptErrors > 0 ? 1 : 0;
	}

	// ── Mode sans écran ──────────────────────────────────────────────────────

	/// Boucle « simulation pure » à pas fixe. Le temps est SIMULÉ (1/60 s par
	/// image) et non mesuré : deux exécutions produisent alors exactement la
	/// même partie, ce qui est le seul moyen d'écrire une assertion stable
	/// sur un nombre de tours de circuit. La cadence mesurée reste, elle,
	/// bien réelle (c'est le temps de calcul de chaque pas).
	[[nodiscard]] int RunHeadless() {
		auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::EVENTS);
		if (!sdl) {
			m_report.failure = String::Format("initialisation SDL : %s", sdl.Error().CStr());
			Project empty = MakeDemoProject();
			EmitReport(empty);
			return 1;
		}

		ecs::ArchetypeRegistry registry;
		Runtime runtime(registry, m_options.jobWorkers);
		InstallCommonHooks(runtime);
		runtime.OpenProject(LoadProjectOrDefault());
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

	// ── Mode fenêtré ─────────────────────────────────────────────────────────

	[[nodiscard]] int RunWindowed() {
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
												 sdl3::window_flags::RESIZABLE);
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
		auto monoFont = sdl3::Font::FindLocal({"DejaVuSansMono", "LiberationMono-Regular", "NotoSansMono-Regular",
												"FreeMono", "Consolas"},
											   FONT_POINT_SIZE);
		if (monoFont)
			gui.RegisterMonospaceFont(monoFont.Value());

		Runtime runtime(registry, m_options.jobWorkers);
		InstallCommonHooks(runtime);
		runtime.AttachCanvas(canvas);
		runtime.OpenProject(LoadProjectOrDefault());
		if (!m_options.scene.IsEmpty() && !runtime.SwitchScene(m_options.scene))
			m_report.warnings.push_back(String::Format("scène inconnue : %s", m_options.scene.CStr()));

		// Précompilation des pipelines pendant l'écran de chargement.
		if (!RunLoadingScreen(gui, registry, renderer, canvas, runtime))
			return ExitCode(); // fermeture demandée pendant le chargement : sortie propre

		EditorUi editorUi(runtime, gui);
		editorUi.SetRenderer(renderer);
		editorUi.SetDirectories(m_options.assetsDir, m_options.savesDir);
		if (!m_options.projectPath.IsEmpty())
			editorUi.projectPath = m_options.projectPath;
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
			// Mode Jeu plein écran : souris capturée (vue subjective).
			if (window.RelativeMouseMode() != editorUi.IsRunMode())
				(void)window.SetRelativeMouseMode(editorUi.IsRunMode());

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

	void InstallWindowedHooks(Runtime &runtime, EditorUi &editorUi) {
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

	/// Ouvre la police d'icônes. Elle est RENDUE à l'appelant sans être
	/// enregistrée, pour deux raisons distinctes qui se sont chacune payées
	/// d'un plantage :
	///
	///  - `RenderSystem::RegisterFont` mémorise un POINTEUR vers la police ;
	///    l'enregistrer ici viserait la variable locale de cette fonction,
	///    qui meurt dès le `return` (le déplacement vers l'appelant change
	///    l'adresse) — use-after-return à la première icône dessinée ;
	///  - une `sdl3::Font` doit être détruite AVANT le `TtfContext` qui l'a
	///    vue naître ; une variable statique (détruite à la toute fin du
	///    processus) ne l'assure pas, une locale de l'appelant déclarée après
	///    son `TtfContext`, si.
	///
	/// Chargement « au mieux » : sans police d'icônes, l'interface reste
	/// entièrement utilisable (les boutons d'outils perdent leur glyphe),
	/// donc un échec ne doit pas empêcher l'application de démarrer.
	[[nodiscard]] static Option<sdl3::Font> OpenIconFont() {
		auto loaded = sdl3::Font::Open(String("assets/fonts/") + ui::MATERIALICONS_FILENAME, 22.f);
		if (!loaded)
			return NONE;
		return Some(std::move(loaded.Value()));
	}

	// ── Captures d'écran ─────────────────────────────────────────────────────

	/// Les demandes de `--screenshot=N:CHEMIN` dont l'image est arrivée.
	void QueueCommandLineScreenshots() {
		for (const ScreenshotRequest &request : m_options.screenshots)
			if (request.frame == m_frame)
				m_pendingScreenshots.push_back(request.path);
	}

	void FlushScreenshots(sdl3::Renderer &renderer) {
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

	void EnsureScreenshotDirectory() {
		if (m_screenshotDirectoryReady || m_options.screenshotDir.IsEmpty())
			return;
		// Sans effet si le dossier existe déjà ; on n'insiste pas en cas
		// d'échec, l'écriture du PNG dira elle-même ce qui ne va pas.
		(void)sdl3::filesystem::CreateDirectory(m_options.screenshotDir);
		m_screenshotDirectoryReady = true;
	}

	// ── Écran de chargement ──────────────────────────────────────────────────

	/// Affiche une barre de progression pendant qu'un fil secondaire force la
	/// compilation des pipelines GPU. Rend `false` si l'utilisateur a fermé la
	/// fenêtre entre-temps (sortie propre, sans construire l'interface).
	[[nodiscard]] bool RunLoadingScreen(ui::Ui &gui, ecs::ArchetypeRegistry &registry, sdl3::Renderer &renderer,
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

	[[nodiscard]] int Fail(String message) {
		std::fprintf(stderr, "%s\n", message.CStr());
		m_report.failure = std::move(message);
		Project project = MakeDemoProject();
		EmitReport(project);
		return 1;
	}

	// ── Membres ──────────────────────────────────────────────────────────────

	static constexpr float FONT_POINT_SIZE = 14.f;
	static constexpr double MAX_STEP_SECONDS = 1.0 / 20.0;

	CommandLine m_options;
	String m_commandLine;
	RunReport m_report;
	ThreadTracker m_threads;
	EditorUi *m_ui = nullptr;
	bool m_quitRequested = false;

	long m_frame = 0;
	bool m_wasPlaying = false;
	bool m_screenshotDirectoryReady = false;
	std::vector<String> m_pendingScreenshots;
};

} // namespace game_editor
