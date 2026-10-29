/**
 * ui_viewport3d_demo — ui::Viewport3D : deux widgets 3D à des rapports d'aspect TRÈS différents (un
 * panneau haut-étroit et un panneau large-bas), montrant la MÊME scène
 * render3d:: depuis deux caméras distinctes, intégrés dans une appli ui::
 * par ailleurs tout à fait normale (panneaux/labels 2D, sdl3::Renderer,
 * thème sombre).
 *
 * Coexistence sdl3::Renderer + render3d::Canvas sur la MÊME fenêtre :
 * confirmée empiriquement sans conflit pendant ce jalon (cf. rapport de
 * tâche M22 — Canvas::Create() réclame le swapchain de la fenêtre pour
 * SDL_GPU, mais aucun code de ce fichier n'appelle canvas.Begin()/End() ni
 * ne présente quoi que ce soit VIA le Canvas : l'extension Viewport3D (appelée
 * par gui.Render(), cf. viewport3d.hpp) utilise en interne RenderObjectToTexture,
 * qui rend dans ses propres command buffers locaux — jamais dans le
 * swapchain de la fenêtre, cf. offscreen.hpp) — seul le sdl3::Renderer
 * présente réellement à l'écran ici, comme dans n'importe quel autre
 * exemple ui::.
 *
 * ── Chargement asynchrone (évite le gel de la fenêtre au lancement) ────────
 * Le premier rendu d'un matériau donné compile paresseusement son pipeline
 * (GetOrCreatePipeline, canvas.hpp) — pour un matériau PBR (ShaderBuilder),
 * cela inclut une compilation GLSL->SPIR-V->MSL à l'exécution (shaderc +
 * spirv-cross), un coût réel de plusieurs centaines de ms. Sans précaution,
 * ce coût tombe en entier sur le tout premier appel à gui.Render() (donc
 * AVANT le tout premier ren.Present()) : la fenêtre SDL3 apparaît figée
 * pendant ce délai, avant même d'afficher quoi que ce soit.
 *
 * Solution : un thread SECONDAIRE (sdl3::Thread) fait deux rendus de
 * "préchauffage" (RenderObjectToTexture, M21, sur une OffscreenTarget
 * jetable) AVANT que la vraie UI ne soit construite — cela force la
 * compilation de tous les pipelines dont la scène aura besoin. Le thread
 * PRINCIPAL reste seul à pomper les évènements/présenter une frame de
 * chargement (label + barre de progression 2D, sans widget Viewport3D —
 * donc sans toucher le Canvas) pendant ce temps, avec une progression
 * relue depuis une petite structure protégée par un sdl3::Mutex.
 *
 * Répartition thread-safe (cf. doc SDL_gpu.h) :
 *   - SDL_ClaimWindowForGPUDevice (donc render3d::Canvas::Create) DOIT
 *     s'exécuter sur le thread qui a créé la fenêtre -> reste sur le thread
 *     principal (rapide de toute façon : charge des shaders précompilés
 *     depuis assets/shaders/bin/, ne compile rien à l'exécution).
 *   - RenderObjectToTexture (M21) n'acquiert/soumet JAMAIS que ses propres
 *     command buffers LOCAUX et ne touche jamais la swapchain -> sûr sur un
 *     thread secondaire (cf. doc SDL_AcquireGPUCommandBuffer : un command
 *     buffer est lié au thread qui l'a acquis, jamais partagé ici).
 *   - `canvas`/`sceneRoot`/les deux Camera ne sont JAMAIS touchés par les
 *     deux threads EN MÊME TEMPS : le thread principal ne les touche pas
 *     pendant le chargement (aucun widget Viewport3D encore spawné, donc
 *     l'extension ne rend rien), et le thread secondaire est
 *     joint avant que le thread principal ne recommence à y toucher —
 *     remise de propriété séquentielle, pas d'accès concurrent réel, donc
 *     aucun verrou nécessaire sur ces objets-là (seule la petite structure
 *     de progression, réellement lue/écrite des deux côtés, est protégée).
 *
 *   cpp-maker run ui_viewport3d_demo
 */
#include <iostream>

#include <core/core.hpp>
#include <render3d/shape.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 1100;
static constexpr int WIN_H = 700;

static constexpr const char *VIEWPORT_CSS = R"css(
#loading { gap: 12px; padding: 24px; align-items: center; }
#views { gap: 16px; padding: 20px; }
#views > panel { gap: 8px; padding: 12px; }
.title { font-size: 16px; }
.muted { color: var(--muted); }
viewport3d { border-radius: 6px; }
)css";

int main() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::cerr << "TTF init: " << ttf.Error().CStr() << "\n";
		return 1;
	}
	auto winRes = sdl3::Window::Create(u8"ui:: - Viewport3D demo", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
	if (!winRes) {
		std::cerr << "Window: " << winRes.Error().CStr() << "\n";
		return 1;
	}
	auto &window = winRes.Value();
	auto renRes = sdl3::Renderer::Create(window, "gpu");
	if (!renRes) {
		std::cerr << "Renderer: " << renRes.Error().CStr() << "\n";
		return 1;
	}
	auto &ren = renRes.Value();
	auto canvasRes = render3d::Canvas::Create(ren);
	if (!canvasRes) {
		std::cerr << "Canvas::Create: " << canvasRes.Error().CStr() << "\n";
		return 1;
	}
	render3d::Canvas canvas = std::move(canvasRes.Value());

	render3d::DirectionalLight sun;
	sun.direction = {0.3f, -0.5f, 0.5f};
	sun.intensity = 0.9f;
	render3d::AmbientLight ambient;
	ambient.color = sdl3::Color{25, 25, 32};
	canvas.SetLighting(sun, ambient);
	canvas.SetBackgroundColor(sdl3::Color{18, 20, 28});

	// ── Scène partagée par les deux vues ────────────────────────────────────
	render3d::Object3D sceneRoot;
	sceneRoot.SetName(String("Viewport3D demo scene"));
	auto &box = static_cast<render3d::Shape &>(sceneRoot.Add(std::make_unique<render3d::Shape>(
		render3d::Mesh::Cube(1.4f), render3d::Material::Plastic(sdl3::Color{80, 140, 220}))));
	box.SetPosition({-1.8f, 0.f, 0.f});
	auto &sphere = static_cast<render3d::Shape &>(sceneRoot.Add(std::make_unique<render3d::Shape>(
		render3d::Mesh::Sphere(0.9f, 32, 24), render3d::Material::Metal(sdl3::Color{220, 220, 230}))));
	sphere.SetPosition({0.f, 0.f, 0.f});
	auto &torus = static_cast<render3d::Shape &>(sceneRoot.Add(std::make_unique<render3d::Shape>(
		render3d::Mesh::Torus(0.8f, 0.3f, 16, 32), render3d::Material::Pbr(sdl3::Color{230, 180, 60}, 0.9f, 0.25f))));
	torus.SetPosition({1.8f, 0.f, 0.f});

	render3d::Camera cameraFront;
	cameraFront.position = {0.f, 1.2f, -9.f};
	cameraFront.target = {0.f, 0.f, 0.f};
	render3d::Camera cameraSide;
	cameraSide.position = {5.5f, 2.5f, -4.f};
	cameraSide.target = {0.f, 0.f, 0.f};

	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, window, ren);
	gui.AddStyleSheet(VIEWPORT_CSS);
	gui.Initialize(canvas); // device GPU des Viewport3D — le rendu 2D reste au sdl3::Renderer

	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "ui:: - Viewport3D", .appIcon = Some(ui::MaterialIcons::VIEW_IN_AR),
				 .status = "Préchauffage des pipelines GPU…"});

	// ── Écran de chargement (aucun Viewport3D : le Canvas reste libre) ──────
	ui::Widget loadStage, loadBar;
	ui::Widget loading = frame.Content().Append(ui::Column().Id("loading").Absolute().Anchor(ui::Anchor::Center).Children({
		ui::Label("Chargement de la scène 3D...").Class("title").Ref(loadStage),
		ui::Progress(0.f, 1.f, 0.f).Size(320.f, 14.f).Ref(loadBar),
	}));

	auto progressMutexRes = sdl3::Mutex::Create();
	if (!progressMutexRes) {
		std::cerr << "Mutex::Create: " << progressMutexRes.Error().CStr() << "\n";
		return 1;
	}
	sdl3::Mutex progressMutex = std::move(progressMutexRes.Value());
	float progressFraction = 0.f;
	String progressStage("Démarrage...");
	bool progressDone = false;
	bool progressFailed = false;
	String progressError;
	auto setProgress = [&](float frac, const char *stage, bool done = false, bool failed = false,
						   StringView err = StringView()) {
		sdl3::MutexGuard guard(progressMutex);
		progressFraction = frac;
		progressStage = String(stage);
		progressDone = done;
		progressFailed = failed;
		if (!err.IsEmpty())
			progressError = String(err);
	};

	// Thread secondaire : deux rendus de préchauffage compilent les pipelines.
	auto threadRes = sdl3::Thread::Create(
		[&]() -> int {
			setProgress(0.05f, "Préparation du GPU...");
			render3d::OffscreenTarget warmTarget;
			std::vector<uint8_t> warmPixels;
			auto sized = warmTarget.EnsureSize(canvas.Device(), 64, 64, ui::VIEWPORT3D_COLOR_FORMAT,
											   ui::VIEWPORT3D_DEPTH_FORMAT);
			if (!sized) {
				setProgress(0.f, "Échec", true, true, sized.Error());
				return 1;
			}
			setProgress(0.15f, "Compilation des pipelines (Plastic/Metal/PBR)...");
			if (auto r = render3d::RenderObjectToTexture(canvas, sceneRoot, cameraFront, warmTarget, warmPixels); !r) {
				setProgress(0.f, "Échec", true, true, r.Error());
				return 1;
			}
			setProgress(0.8f, "Préchauffage de la seconde caméra...");
			if (auto r = render3d::RenderObjectToTexture(canvas, sceneRoot, cameraSide, warmTarget, warmPixels); !r) {
				setProgress(0.f, "Échec", true, true, r.Error());
				return 1;
			}
			setProgress(1.f, "Terminé", true, false);
			return 0;
		},
		String("viewport3d-warmup"));
	if (!threadRes) {
		std::cerr << "Thread::Create: " << threadRes.Error().CStr() << "\n";
		return 1;
	}
	sdl3::Thread warmupThread = std::move(threadRes.Value());

	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}
			gui.HandleEvent(e);
		}
		if (!running)
			break;
		bool done;
		{
			sdl3::MutexGuard guard(progressMutex);
			loadBar.SetValue(progressFraction);
			loadStage.SetText(progressStage);
			done = progressDone;
		}
		const uint64_t now = sdl3::GetTicksMS();
		gui.Update(float(now - lastTick) / 1000.f);
		lastTick = now;
		frame.Update();
		if (frame.CloseRequested()) {
			running = false;
			break;
		}
		ren.SetDrawColor(gui.CurrentPalette().appBg);
		ren.Clear();
		gui.Render();
		ren.Present();
		if (done)
			break;
	}
	warmupThread.Wait();
	if (!running)
		return 0;
	{
		sdl3::MutexGuard guard(progressMutex);
		if (progressFailed) {
			std::cerr << "Préchauffage 3D: " << progressError.c_str() << "\n";
			return 1;
		}
	}

	// ── Les deux vues : même scène, deux caméras, deux rapports d'aspect ────
	loading.Destroy();
	frame.SetStatus("Même scène, deux caméras.");
	frame.Content().Append(ui::Row().Id("views").Absolute().Anchor(ui::Anchor::Center).Children({
		ui::Panel().Children({
			ui::Label("Panneau haut-étroit").Class("title"),
			ui::Label("Caméra de face (aspect ~0.52)").Class("muted"),
			ui::Viewport3D(sceneRoot, cameraFront).Size(220.f, 420.f),
		}),
		ui::Panel().Children({
			ui::Label("Panneau large-bas").Class("title"),
			ui::Label("Caméra 3/4 côté (aspect ~1.9)").Class("muted"),
			ui::Viewport3D(sceneRoot, cameraSide).Size(420.f, 220.f),
		}),
	}));

	float sceneAngle = 0.f;
	while (running) {
		const uint64_t now = sdl3::GetTicksMS();
		const float dt = float(now - lastTick) / 1000.f;
		lastTick = now;
		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}
			gui.HandleEvent(e);
		}
		gui.Update(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		// Rotation lente : les deux vues (continues) se mettent à jour à chaque image.
		sceneAngle += dt * 0.35f;
		sceneRoot.SetRotation(math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, sceneAngle));

		ren.SetDrawColor(gui.CurrentPalette().appBg);
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	return 0;
}
