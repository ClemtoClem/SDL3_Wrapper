// Définitions de app.hpp — options, mode sans fenêtre, boucle de la fenêtre.
#include <voxelia/app.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include <SDL3_image/SDL_image.h>
#include <render3d/canvas.hpp>
#include <render3d/shader_compiler.hpp>
#include <sdl3/image.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>
#include <voxel/json.hpp>
#include <voxel/scripting.hpp>
#include <voxelia/session.hpp>
#include <voxelia/shell.hpp>
#include <voxelia/studio/studio.hpp>
#include <ui/window_manager.hpp>

namespace voxelia {

using namespace voxel;

namespace {

/// Mesures de la boucle (images de jeu) : durées en ms.
struct FrameStats {
	std::vector<double> frames;
	double wait = 0.0, update = 0.0, draw = 0.0, present = 0.0;
	double pending = 0.0; ///< chunks à mailler (somme sur les images)
};

} // namespace

// ── Options ──────────────────────────────────────────────────────────────────

const char *Usage() {
	return "voxelia — jeu voxel piloté par les données (mods JSON + scripts)\n"
		   "\n"
		   "  --world=DOSSIER        joue directement ce monde (créé s'il n'existe pas)\n"
		   "  --seed=N               graine d'un nouveau monde\n"
		   "  --generator=ID         générateur d'un nouveau monde (core:overworld, core:flat, core:islands…)\n"
		   "  --mods=DIR             dossier de mods (répétable ; défaut : demos/voxelia/mods)\n"
		   "  --repository=DIR       mods disponibles à l'installation (défaut : demos/voxelia/repository)\n"
		   "  --saves=DIR            dossier des sauvegardes (défaut : data/voxelia/saves)\n"
		   "  --structures=DIR       structures à importer / exporter entre mondes (défaut : à côté des sauvegardes)\n"
		   "  --settings=FICHIER     préférences (défaut : data/voxelia/settings.json)\n"
		   "  --view=N               distance d'affichage en chunks (sans fenêtre)\n"
		   "  --creative             mode créatif (nouveau monde)\n"
		   "  --frames=N             quitte après N images\n"
		   "  --screenshot=N:FICHIER capture l'image N (répétable)\n"
		   "  --exec=N:/commande     commande de jeu à l'image N (répétable) ;\n"
		   "  --exec=N:!commande     commande de l'application : !screen NOM, !play DOSSIER, !back,\n"
		   "                         !home, !look LACET TANGAGE, !notify TON TEXTE, !confirm, !set CLÉ VALEUR…\n"
		   "  --headless             sans fenêtre : simulation, commandes, rapport\n"
		   "  --threads=on|off       simulation et maillage sur leurs fils (on, défaut) ou sur le fil principal\n"
		   "  --mesh-threads=N       fils de rendu pour le maillage des chunks (défaut : 2)\n"
		   "  --viewport=MODE        vue 3D → interface : direct (défaut, sans copie), pipelined, readback\n"
		   "  --width=W --height=H   taille de la fenêtre\n"
		   "\n"
		   "Voxelia Studio (éditeur, dans sa propre fenêtre, sur la même partie) :\n"
		   "  --studio               ouvre le studio au démarrage (F10 l'ouvre / le ferme)\n"
		   "  --studio-display=N     écran du studio (0 : principal)\n"
		   "  --studio-screenshot=N:FICHIER  capture la fenêtre du studio à l'image N\n"
		   "  --screenshot-desktop=N:FICHIER capture toutes les fenêtres, à leur place, à l'image N\n"
		   "  --exec=N:!studio LIGNE commande de la console du studio (studio.select, studio.pause…)\n"
		   "  --report=FICHIER       rapport d'exécution (texte, et FICHIER.json)\n"
		   "\n"
		   "Touches par défaut (Réglages > Commandes) : ZQSD/WASD, souris, Espace, Maj, Ctrl, F vol,\n"
		   "E inventaire, C fabrication, T discussion, / terminal, Tab informations, Échap pause,\n"
		   "F1 masquer l'interface, F2 capture, F3 informations techniques, F5 vue, F6 enregistrer,\n"
		   "F10 Voxelia Studio, Ctrl+Q quitter (toutes les fenêtres).\n";
}

Option<String> ParseOptions(int argc, char **argv, Options &o) {
	for (int i = 1; i < argc; ++i) {
		const String arg(argv[i]);
		auto value = [&](const char *name) -> Option<String> {
			const String prefix = String(name) + String("=");
			return arg.StartsWith(prefix.CStr()) ? Some(arg.Substring(prefix.GetSize())) : Option<String>(NONE);
		};
		auto framed = [&](const String &text, std::vector<std::pair<long, String>> &out) -> bool {
			const size_t colon = text.Find(":");
			if (colon == String::NPOS)
				return false;
			out.push_back({std::atol(text.Substring(0, colon).CStr()), text.Substring(colon + 1)});
			return true;
		};
		if (arg == "--help" || arg == "-h")
			o.help = true;
		else if (arg == "--creative")
			o.creative = true;
		else if (auto threadsArg = value("--threads"); threadsArg.IsSome()) {
			if (threadsArg.Value() != "on" && threadsArg.Value() != "off")
				return Some(String("--threads=on|off attendu"));
			o.threaded = threadsArg.Value() == "on";
		} else if (auto meshArg = value("--mesh-threads"); meshArg.IsSome()) {
			o.meshThreads = std::clamp(std::atoi(meshArg.Value().CStr()), 1, 16);
		} else if (auto viewportArg = value("--viewport"); viewportArg.IsSome()) {
			if (viewportArg.Value() != "direct" && viewportArg.Value() != "pipelined" && viewportArg.Value() != "readback")
				return Some(String("--viewport=direct|pipelined|readback attendu"));
			o.viewport = viewportArg.Value();
		}
		else if (arg == "--headless")
			o.headless = true;
		else if (auto worldArg = value("--world"); worldArg.IsSome())
			o.world = worldArg.Unwrap();
		else if (auto seedArg = value("--seed"); seedArg.IsSome())
			o.seed = Some(uint32_t(std::strtoul(seedArg.Unwrap().CStr(), nullptr, 10)));
		else if (auto generatorArg = value("--generator"); generatorArg.IsSome())
			o.generator = generatorArg.Unwrap();
		else if (auto modsArg = value("--mods"); modsArg.IsSome())
			o.mods.push_back(modsArg.Unwrap());
		else if (auto repositoryArg = value("--repository"); repositoryArg.IsSome())
			o.repository = repositoryArg.Unwrap();
		else if (auto savesArg = value("--saves"); savesArg.IsSome())
			o.saves = savesArg.Unwrap();
		else if (auto structuresArg = value("--structures"); structuresArg.IsSome())
			o.structures = structuresArg.Unwrap();
		else if (auto settingsArg = value("--settings"); settingsArg.IsSome())
			o.settingsPath = settingsArg.Unwrap();
		else if (auto viewArg = value("--view"); viewArg.IsSome())
			o.view = std::clamp(std::atoi(viewArg.Unwrap().CStr()), 2, 16);
		else if (auto framesArg = value("--frames"); framesArg.IsSome())
			o.frames = std::atol(framesArg.Unwrap().CStr());
		else if (auto widthArg = value("--width"); widthArg.IsSome())
			o.width = std::max(640, std::atoi(widthArg.Unwrap().CStr()));
		else if (auto heightArg = value("--height"); heightArg.IsSome())
			o.height = std::max(400, std::atoi(heightArg.Unwrap().CStr()));
		else if (auto screenshotArg = value("--screenshot"); screenshotArg.IsSome()) {
			if (!framed(screenshotArg.Unwrap(), o.screenshotsAt))
				return Some(String("--screenshot=N:FICHIER attendu"));
		} else if (arg == "--studio") {
			o.studio = true;
		} else if (auto displayArg = value("--studio-display"); displayArg.IsSome()) {
			o.studioDisplay = Some(size_t(std::max(0, std::atoi(displayArg.Unwrap().CStr()))));
			o.studio = true;
		} else if (auto studioShot = value("--studio-screenshot"); studioShot.IsSome()) {
			if (!framed(studioShot.Unwrap(), o.studioShots))
				return Some(String("--studio-screenshot=N:FICHIER attendu"));
		} else if (auto desktopShot = value("--screenshot-desktop"); desktopShot.IsSome()) {
			if (!framed(desktopShot.Unwrap(), o.desktopShots))
				return Some(String("--screenshot-desktop=N:FICHIER attendu"));
		} else if (auto reportArg = value("--report"); reportArg.IsSome()) {
			o.report = reportArg.Unwrap();
		} else if (auto execArg = value("--exec"); execArg.IsSome()) {
			if (!framed(execArg.Unwrap(), o.commands))
				return Some(String("--exec=N:/commande attendu"));
		} else
			return Some(String::Format("option inconnue « %s »", arg.CStr()));
	}
	if (o.mods.empty())
		o.mods.push_back(String("demos/voxelia/mods"));
	if (o.structures.IsEmpty()) { // <saves>/../structures, comme les copies de sécurité
		std::string saves(o.saves.CStr());
		while (!saves.empty() && saves.back() == '/')
			saves.pop_back();
		const size_t slash = saves.rfind('/');
		o.structures = String(slash == std::string::npos ? "structures" : (saves.substr(0, slash) + "/structures").c_str());
	}
	return NONE;
}

App::App(Options options) : m_options(std::move(options)) {}
App::~App() = default;

int App::Run() {
	if (m_options.help) {
		std::printf("%s", Usage());
		return 0;
	}
	return m_options.headless ? RunHeadless() : RunWindowed();
}

// ── Sans fenêtre ─────────────────────────────────────────────────────────────

int App::RunHeadless() {
	Game game;
	GameOptions options;
	options.modDirectories = m_options.mods;
	options.savesDirectory = m_options.saves;
	options.structureDirectory = m_options.structures;
	options.viewRadius = m_options.view;
	options.verticalRadius = std::max(2, m_options.view / 2);
	options.creative = m_options.creative;
	const ModReport report = game.LoadMods(options);
	for (const ModInfo &mod : report.mods)
		std::printf("mod %s %s : %d définition(s), %d script(s)\n", mod.id.CStr(), mod.version.CStr(), mod.definitions,
					int(mod.scripts.size()));
	for (const String &e : report.errors)
		std::printf("[erreur] %s\n", e.CStr());
	for (const String &w : report.warnings)
		std::printf("[avertissement] %s\n", w.CStr());
	if (!game.Mods().errors.empty() && game.Content().BlockCount() <= 1) {
		std::printf("aucun contenu chargé : vérifiez --mods\n");
		return 1;
	}
	// Shaders des mods : chargés et compilés (GLSL → SPIR-V) pour les valider.
	for (const ShaderDef *shader : game.Content().Shaders())
		for (const auto &[path, stage] : {std::pair{shader->vertex, sdl3::gpu_shader_stage::VERTEX},
										  std::pair{shader->fragment, sdl3::gpu_shader_stage::FRAGMENT}}) {
			Result<String, String> source = script::LoadScriptFile(path);
			if (source.IsError())
				continue;
			auto spirv = render3d::CompileGlslToSpirv(source.Value().View(), stage, path.View());
			if (spirv.IsError())
				std::printf("[shader] %s : %s\n", shader->id.CStr(), spirv.Error().CStr());
		}
	const String world = m_options.world.IsEmpty() ? String("Monde") : m_options.world;
	if (Result<bool, String> opened = game.OpenWorld(world, m_options.seed, m_options.generator); opened.IsError()) {
		std::printf("monde « %s » : %s\n", world.CStr(), opened.Error().CStr());
		return 1;
	}
	const ecs::Entity player = game.JoinPlayer("Joueur");
	const long frames = m_options.frames > 0 ? m_options.frames : 600;
	uint64_t messages = 0;
	for (long frame = 1; frame <= frames; ++frame) {
		for (const auto &[at, command] : m_options.commands)
			if (at == frame && !command.StartsWith("!")) {
				Result<String, String> r = game.Execute(command, player);
				std::printf("%s → %s\n", command.CStr(), r.IsOk() ? r.Value().CStr() : r.Error().CStr());
			}
		game.Tick(1.f / 60.f);
		ecs::EventsOf<GameMessage>(game.Registry()).Read(messages, [](const GameMessage &m) { std::printf("[%s] %s\n", m.channel.CStr(), m.text.CStr()); });
	}
	auto t = game.Registry().GetComponent<Transform>(player);
	std::printf("%ld image(s) simulées — %d chunk(s) chargé(s), %d entité(s), joueur en %.1f %.1f %.1f\n", frames,
				int(game.GetWorld().LoadedChunkCount()), int(game.Registry().EntityCount()), double(t.Unwrap()->position.x),
				double(t.Unwrap()->position.y), double(t.Unwrap()->position.z));
	for (const String &line : game.Log())
		if (line.StartsWith("[script") || line.StartsWith("[erreur"))
			std::printf("%s\n", line.CStr());
	(void)game.Save();
	game.CloseWorld();
	return 0;
}

// ── Fenêtre ──────────────────────────────────────────────────────────────────

namespace {

/// Vignette du monde : la vue courante réduite à 320×180.
void SaveThumbnail(sdl3::Renderer &renderer, const String &path) {
	auto surface = renderer.ReadPixels();
	if (!surface)
		return;
	SDL_Surface *src = surface.Value().Get();
	const float aspect = 16.f / 9.f;
	int w = src->w, h = int(float(src->w) / aspect);
	if (h > src->h) {
		h = src->h;
		w = int(float(h) * aspect);
	}
	const SDL_Rect crop{(src->w - w) / 2, (src->h - h) / 2, w, h};
	SDL_Surface *cut = SDL_CreateSurface(w, h, src->format);
	if (!cut)
		return;
	(void)SDL_BlitSurface(src, &crop, cut, nullptr);
	if (SDL_Surface *small = SDL_ScaleSurface(cut, 320, 180, SDL_SCALEMODE_LINEAR)) {
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(path.CStr()).parent_path(), ec);
		(void)IMG_SavePNG(small, path.CStr());
		SDL_DestroySurface(small);
	}
	SDL_DestroySurface(cut);
}

} // namespace

int App::RunWindowed() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS | sdl3::init_flags::AUDIO);
	if (!sdl) {
		std::printf("SDL : %s\n", String(sdl.Error()).CStr());
		return 1;
	}
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::printf("TTF : %s\n", String(ttf.Error()).CStr());
		return 1;
	}
	// Fenêtres : la partie (principale) et, à la demande, Voxelia Studio. Un
	// seul device GPU (ui::WindowManager), une swapchain par fenêtre : la vue 3D
	// du studio affiche la scène de la partie sans copie (DIRECT).
	ui::WindowManager windows;
	ui::WindowSpec gameSpec;
	gameSpec.title = "Voxelia";
	gameSpec.width = m_options.width;
	gameSpec.height = m_options.height;
	gameSpec.role = "game";
	gameSpec.primary = true;
	gameSpec.followDisplayScale = false; // le jeu a son échelle d'interface (réglages)
	auto gameWindow = windows.Create(gameSpec);
	if (!gameWindow) {
		std::printf("fenêtre : %s\n", gameWindow.Error().CStr());
		return 1;
	}
	ui::WindowContext &gameContext = *gameWindow.Value();
	sdl3::Window &window = *gameContext.Window();
	sdl3::Renderer &renderer = *gameContext.Renderer();
	// Détruit avant `windows` (qui possède le device GPU qu'il emprunte).
	auto canvasResult = render3d::Canvas::Create(renderer);
	if (!canvasResult) {
		std::printf("canvas 3D : %s\n", String(canvasResult.Error()).CStr());
		return 1;
	}
	render3d::Canvas canvas = std::move(canvasResult.Value());
	// Polices (DejaVu Sans et ses variantes, icônes) chargées par l'interface.
	ui::Ui &gui = gameContext.Gui();
	if (gui.GetFonts().DefaultFamily().IsEmpty()) {
		std::printf("police introuvable\n");
		return 1;
	}
	gui.Initialize(canvas);

	Shell shell(m_options, window, renderer, canvas, gui);
	if (Result<bool, String> r = shell.Initialize(); r.IsError()) {
		std::printf("initialisation : %s\n", r.Error().CStr());
		return 1;
	}
	if (!m_options.world.IsEmpty()) {
		// Accès direct à un monde (créé au besoin) : tests, captures pilotées.
		const std::filesystem::path dir = std::filesystem::path(m_options.saves.CStr()) / m_options.world.CStr();
		if (!std::filesystem::exists(dir / "world.json")) {
			dataio::NodePtr settings = dataio::Node::MakeObject();
			settings->Set("gameMode", json::MakeStr(m_options.creative ? "creative" : "survival"));
			dataio::NodePtr world = dataio::Node::MakeObject();
			world->Set("format", json::MakeStr(SAVE_FORMAT));
			world->Set("version", json::MakeInt(SAVE_VERSION));
			world->Set("name", json::MakeStr(m_options.world));
			world->Set("seed", json::MakeInt(int64_t(m_options.seed.IsSome() ? m_options.seed.Value() : 1337u)));
			world->Set("generator", json::MakeStr(m_options.generator));
			world->Set("gameTime", json::MakeNum(-1.0));
			world->Set("settings", settings);
			(void)json::WriteFile(String(dir.string().c_str()) + String("/world.json"), world);
		}
		shell.PlayWorld(m_options.world);
	}
	// ── Voxelia Studio ──────────────────────────────────────────────────────
	studio::StudioReport report;
	report.Set(String("viewport"), m_options.viewport);
	report.Set(String("threads"), String(m_options.threaded ? "on" : "off"));
	report.Set(String("meshThreads"), String::Format("%d", m_options.meshThreads));
	report.Set(String("gpuShared"), String(gameContext.SharesGpuDevice() ? "oui" : "non"));
	std::unique_ptr<studio::Studio> studioApp; // détruit avant le Shell qu'il référence
	ui::WindowId studioId = 0;
	auto openStudio = [&]() -> bool {
		if (studioApp)
			return true;
		ui::WindowSpec spec;
		spec.title = "Voxelia Studio";
		spec.width = 1360;
		spec.height = 820;
		spec.role = "studio";
		spec.vsync = false; // la fenêtre de jeu attend le balayage pour les deux
		if (m_options.studioDisplay.IsSome())
			spec.display = m_options.studioDisplay;
		else
			spec.position = Some(sdl3::Point{gameContext.DesktopPosition().x + window.GetWidth() + 8, gameContext.DesktopPosition().y});
		auto created = windows.Create(spec);
		if (!created) {
			shell.Log(String("[erreur] studio : ") + created.Error());
			report.Visit(String("studio.open"), studio::FeatureStatus::Error, created.Error());
			return false;
		}
		ui::WindowContext &context = *created.Value();
		context.Gui().Initialize(canvas);
		studioApp = std::make_unique<studio::Studio>(shell, context, &report, m_options.viewport);
		studioId = context.Id();
		report.Visit(String("windows.studio"), studio::FeatureStatus::Ok,
					 String::Format("%zu fenêtre(s), device GPU %s", windows.Count(),
									context.SharesGpuDevice() ? "partagé" : "distinct"));
		context.onClose = [&](ui::WindowContext &) {
			studioApp.reset();
			studioId = 0;
			report.Visit(String("studio.close"));
		};
		return true;
	};
	bool toggleStudio = false, quitAll = false;
	windows.AddGlobalShortcut({SDLK_Q, SDL_KMOD_CTRL, "quit", [&](ui::WindowContext &) { quitAll = true; }});
	windows.AddGlobalShortcut({SDLK_F10, SDL_KMOD_NONE, "studio", [&](ui::WindowContext &) { toggleStudio = true; }});
	if (m_options.studio)
		(void)openStudio();


	//  1. il attend la fin du pas de simulation lancé à l'image précédente
	//     (fil « voxel-update ») : à partir d'ici, le jeu est à lui seul ;
	//  2. événements, commandes, scène (WorldRenderer : il dépose les chunks
	//     à mailler sur le fil « voxel-render » et pose ceux qui en reviennent),
	//     interface, enregistrement des commandes GPU ;
	//  3. il lance le pas de simulation suivant sur le fil de mise à jour,
	//  4. et présente l'image (soumission GPU, attente de la synchronisation
	//     verticale) pendant que la simulation avance.
	uint64_t previous = sdl3::GetPerformanceCounter();
	long frame = 0;
	double totalSeconds = 0.0;
	FrameStats stats;
	long playing = 0; // images de jeu consécutives (partie prête)
	const double frequency = double(sdl3::GetPerformanceFrequency());
	auto ms = [frequency](uint64_t from, uint64_t to) { return double(to - from) * 1000.0 / frequency; };
	while (shell.Running()) {
		const uint64_t now = sdl3::GetPerformanceCounter();
		shell.EndSimulation();
		const uint64_t joined = sdl3::GetPerformanceCounter();
		const float dt = std::min(0.1f, float(double(now - previous) / frequency));
		const double frameMs = ms(previous, now);
		previous = now;
		++frame;
		totalSeconds += double(dt);
		// Statistiques : images de jeu seulement, après 60 images de mise en route.
		Session *session = shell.Game();
		playing = session && session->Ready() ? playing + 1 : 0;
		const bool counted = playing > 60;
		if (counted) {
			stats.frames.push_back(frameMs);
			stats.pending += double(session->Renderer().PendingChunks());
		}
		while (auto event = sdl3::PollEvent()) {
			sdl3::Event &ev = event.Value();
			if (studioApp && windows.Target(ev) == windows.Find(studioId)) {
				// Fenêtre du studio : raccourcis globaux, interface, caméra.
				const ui::DispatchResult routed = windows.Dispatch(ev);
				if (routed.shortcut.IsEmpty())
					studioApp->HandleEvent(ev, routed.consumed);
				continue;
			}
			// Fenêtre de jeu (et évènements sans fenêtre) : le Shell, comme avant.
			ui::WindowContext *target = windows.Observe(ev);
			if (target && windows.TriggerShortcut(ev, *target))
				continue;
			if (ev.Type() == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
				continue; // HandleCloseRequests : la principale arrête l'application
			shell.HandleEvent(ev);
		}
		if (toggleStudio) {
			toggleStudio = false;
			if (studioApp)
				(void)windows.Close(studioId);
			else
				(void)openStudio();
		}
		windows.HandleCloseRequests();
		if (quitAll || windows.QuitRequested())
			shell.Quit();
		for (const auto &[at, command] : m_options.commands)
			if (at == frame) {
				if (command.StartsWith("!studio")) {
					const String line = command.Substring(7).Trim();
					if (line == "close") {
						if (studioApp)
							(void)windows.Close(studioId);
					} else if (openStudio() && !line.IsEmpty() && line != "open") {
						const studio::CommandResult r = studioApp->Execute(line);
						if (r.status == studio::CommandStatus::Error)
							report.Warning(String("!studio ") + line + String(" : ") + r.message);
					}
				} else if (command.StartsWith("!"))
					shell.AppCommand(command);
				else
					shell.GameCommand(command);
			}
		const uint64_t t0 = sdl3::GetPerformanceCounter();
		shell.Update(dt, frame);
		// Le studio lit et modifie la partie ICI : le pas de simulation de
		// l'image précédente est terminé, le suivant n'est pas lancé.
		if (studioApp)
			studioApp->Update(dt);
		const uint64_t t1 = sdl3::GetPerformanceCounter();
		windows.Update(dt);
		for (const auto &[at, path] : m_options.desktopShots)
			if (at == frame) {
				windows.RequestDesktopCapture(path);
				report.Capture(path);
				report.Visit(String("capture.desktop"), studio::FeatureStatus::Ok, String::Format("%zu fenêtre(s)", windows.Count()));
			}
		for (const auto &[at, path] : m_options.studioShots)
			if (at == frame) {
				if (studioApp) {
					windows.RequestCapture(studioId, path);
					report.Capture(path);
					report.Visit(String("capture.studio"));
				} else {
					report.Error(String("capture du studio ") + path + String(" : studio fermé"));
				}
			}
		windows.Draw({0.f, 0.f, 0.f, 1.f});
		for (const String &error : windows.CaptureErrors())
			report.Error(error);
		const uint64_t t2 = sdl3::GetPerformanceCounter();
		if (!shell.pendingThumbnail.IsEmpty()) {
			SaveThumbnail(renderer, shell.pendingThumbnail);
			shell.pendingThumbnail = String();
		}
		for (const auto &[at, path] : m_options.screenshotsAt)
			if (at == frame)
				if (auto surface = renderer.ReadPixels(); surface)
					(void)sdl3::ImgSavePng(surface.Value(), path);
		const uint64_t built = sdl3::GetPerformanceCounter();
		shell.BeginSimulation();
		windows.Present();
		if (frame > 60)
			report.Frame(frameMs);
		if (counted) {
			stats.wait += ms(now, joined);
			stats.update += ms(t0, t1);
			stats.draw += ms(t1, t2);
			stats.present += ms(built, sdl3::GetPerformanceCounter());
		}
		// Limite d'images (réglages graphiques).
		if (const int limit = shell.Prefs().fpsLimit; limit > 0) {
			const double target = 1.0 / double(limit);
			const double spent = double(sdl3::GetPerformanceCounter() - now) / double(sdl3::GetPerformanceFrequency());
			if (spent < target)
				std::this_thread::sleep_for(std::chrono::duration<double>(target - spent));
		}
		if (m_options.frames > 0 && frame >= m_options.frames)
			break;
	}
	shell.EndSimulation();
	std::printf("%ld image(s), %.1f img/s en moyenne\n", frame, totalSeconds > 0.0 ? double(frame) / totalSeconds : 0.0);
	if (!stats.frames.empty()) {
		const double n = double(stats.frames.size());
		std::vector<double> sorted = stats.frames;
		std::sort(sorted.begin(), sorted.end());
		double sum = 0.0;
		for (double f : sorted)
			sum += f;
		const double p99 = sorted[std::min(sorted.size() - 1, size_t(n * 0.99))];
		std::printf("en jeu (%zu images ; fils %s, maillage %d, vue %s) : %.1f img/s, image %.2f ms (99 %% : %.2f ms), "
					"%.1f chunks en attente\n",
					sorted.size(), m_options.threaded ? "on" : "off", m_options.threaded ? m_options.meshThreads : 0,
					m_options.viewport.CStr(), 1000.0 * n / sum, sum / n, p99, stats.pending / n);
		std::printf("  fil principal : attente de la simulation %.2f ms, jeu et scène %.2f ms, interface et rendu %.2f ms, "
					"présentation %.2f ms\n",
					stats.wait / n, stats.update / n, stats.draw / n, stats.present / n);
		if (Session *game = shell.Game(); game && game->Ready())
			std::printf("  simulation %.2f ms par pas, maillage %.2f ms par chunk\n", double(game->TickMilliseconds()),
						double(game->Renderer().MeshMilliseconds()));
	}
	for (const String &line : shell.RecentLog())
		if (line.StartsWith("[erreur") || line.StartsWith("[script") || line.StartsWith("[son"))
			std::printf("%s\n", line.CStr());
	// Le studio se détache avant le Shell et le Canvas qu'il emprunte.
	if (studioApp)
		(void)windows.Close(studioId);
	if (!m_options.report.IsEmpty()) {
		report.Set(String("frames"), String::Format("%ld", frame));
		if (Result<bool, String> written = report.Write(m_options.report); written.IsError()) {
			std::printf("%s\n", written.Error().CStr());
			return 1;
		}
		std::printf("rapport : %s (%s)\n", m_options.report.CStr(), report.Passed() ? "succès" : "ÉCHEC");
		if (!report.Passed())
			return 3;
	}
	return 0;
}

} // namespace voxelia
