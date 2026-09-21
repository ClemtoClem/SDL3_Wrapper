/**
 * ui_viewport3d_demo — ui::Viewport3D (M22, Phase 3 du plan d'expansion
 * moteur) : deux widgets 3D à des rapports d'aspect TRÈS différents (un
 * panneau haut-étroit et un panneau large-bas), montrant la MÊME scène
 * render3d:: depuis deux caméras distinctes, intégrés dans une appli ui::
 * par ailleurs tout à fait normale (panneaux/labels 2D, sdl3::Renderer,
 * UiTheme::Dark()).
 *
 * Coexistence sdl3::Renderer + render3d::Canvas sur la MÊME fenêtre :
 * confirmée empiriquement sans conflit pendant ce jalon (cf. rapport de
 * tâche M22 — Canvas::Create() réclame le swapchain de la fenêtre pour
 * SDL_GPU, mais aucun code de ce fichier n'appelle canvas.Begin()/End() ni
 * ne présente quoi que ce soit VIA le Canvas : ui::Viewport3DSystem (appelé
 * par gui.Render(), cf. ui.hpp) utilise en interne RenderObjectToTexture,
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
 *     Viewport3DSystem::Update est un no-op), et le thread secondaire est
 *     joint avant que le thread principal ne recommence à y toucher —
 *     remise de propriété séquentielle, pas d'accès concurrent réel, donc
 *     aucun verrou nécessaire sur ces objets-là (seule la petite structure
 *     de progression, réellement lue/écrite des deux côtés, est protégée).
 *
 *   make build/bin/ui_viewport3d_demo && ./build/bin/ui_viewport3d_demo
 */
#include <iostream>

#include "core/core.hpp"
#include "render3d/shape.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 1100;
static constexpr int WIN_H = 640;
static constexpr float FONT_PT = 14.f;

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
	auto fontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
	if (!fontRes) {
		std::cerr << "Font: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto &font = fontRes.Value();

	auto winRes = sdl3::Window::Create(u8"ui:: - Viewport3D demo", WIN_W, WIN_H, sdl3::window_flags::RESIZABLE);
	if (!winRes) {
		std::cerr << "Window: " << winRes.Error().CStr() << "\n";
		return 1;
	}
	auto &window = winRes.Value();

	// Renderer (backend 2D de ui::) ET Canvas (device GPU pour les widgets
	// Viewport3D) sur la MÊME fenêtre — cf. en-tête du fichier. Les deux
	// DOIVENT être créés sur le thread principal (ClaimWindowForGPUDevice).
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

	auto engRes = sdl3::TextEngine::Create(ren);
	if (!engRes) {
		std::cerr << "TextEngine: " << engRes.Error().CStr() << "\n";
		return 1;
	}
	auto &eng = engRes.Value();

	// ── Scène 3D partagée par les deux Viewport3D (mêmes objets, deux
	// caméras différentes — cf. en-tête du fichier) ─────────────────────────
	render3d::DirectionalLight sun;
	sun.direction = {0.3f, -0.5f, 0.5f};
	sun.intensity = 0.9f;
	render3d::AmbientLight ambient;
	ambient.color = sdl3::Color{25, 25, 32};
	canvas.SetLighting(sun, ambient);
	canvas.SetBackgroundColor(sdl3::Color{18, 20, 28});

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

	// Caméra A : vue de face, légèrement en hauteur — pour le panneau
	// HAUT-ÉTROIT (aspect << 1) : recule assez pour que toute la rangée
	// d'objets tienne dans un cadrage vertical serré.
	render3d::Camera cameraFront;
	cameraFront.position = {0.f, 1.2f, -9.f};
	cameraFront.target = {0.f, 0.f, 0.f};

	// Caméra B : vue de 3/4 depuis le côté, plus proche — pour le panneau
	// LARGE-BAS (aspect >> 1) : angle et distance différents de la caméra A,
	// donc contenu visiblement différent (pas juste un recadrage de la même
	// image), cf. exigence "deux caméras génuinement différentes" du plan.
	render3d::Camera cameraSide;
	cameraSide.position = {5.5f, 2.5f, -4.f};
	cameraSide.target = {0.f, 0.f, 0.f};

	// ── Pipeline UI ──────────────────────────────────────────────────────────
	ecs::ArchetypeRegistry ar;
	ui::Ui gui(ar, window, ren);
	gui.Initialize(canvas); // enregistre le Canvas pour les widgets Viewport3D (M22) — ne touche PAS le backend 2D
	ui::UiFactory &f = gui.Factory();

	gui.SetTextEngine(eng, font);
	gui.Layout().measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};

	// ── Phase de chargement asynchrone (cf. en-tête du fichier) ─────────────
	// Frame de chargement : juste un label + une barre de progression, tous
	// les deux en 2D pur (aucun widget Viewport3D ici) — le thread principal
	// peut donc la dessiner/présenter en boucle sans jamais toucher `canvas`.
	auto loadingRootBuilder = f.Column();
	loadingRootBuilder.Anchor(ui::Anchor::Center).Gap(12.f).Pad(24.f).WAuto().HAuto().Children(
		f.Label("Chargement de la scène 3D...").FontSize(16.f).Name("loadStage"),
		f.Progress(0.f, 1.f, 0.f).Size(320.f, 14.f).Name("loadBar"));
	ecs::Entity loadingRoot = loadingRootBuilder.Spawn();

	// Progression partagée entre le thread principal (lecteur) et le thread
	// de préchauffage (écrivain) — seule donnée réellement touchée par les
	// deux threads en même temps, donc la seule qui a besoin d'un verrou
	// (cf. raisonnement complet en en-tête du fichier).
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

	// Deux rendus de préchauffage "jetables" (mêmes formats couleur/
	// profondeur QUE Viewport3DSystem — cf. ui::VIEWPORT3D_COLOR_FORMAT/
	// DEPTH_FORMAT, sinon le cache de pipelines ne serait pas réellement
	// réchauffé pour les vrais widgets) suffisent à compiler tous les
	// pipelines dont la scène a besoin, une fois pour toutes : les deux
	// Viewport3D finaux partagent la MÊME sceneRoot (mêmes matériaux), donc
	// le premier rendu compile déjà tout ; le second n'existe que pour
	// vérifier/documenter que la caméra B fonctionne aussi (quasi instantané,
	// cache de pipeline déjà chaud).
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
			auto r1 = render3d::RenderObjectToTexture(canvas, sceneRoot, cameraFront, warmTarget, warmPixels);
			if (!r1) {
				setProgress(0.f, "Échec", true, true, r1.Error());
				return 1;
			}

			setProgress(0.8f, "Préchauffage de la seconde caméra...");
			auto r2 = render3d::RenderObjectToTexture(canvas, sceneRoot, cameraSide, warmTarget, warmPixels);
			if (!r2) {
				setProgress(0.f, "Échec", true, true, r2.Error());
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

		float frac;
		String stage;
		bool done;
		{
			sdl3::MutexGuard guard(progressMutex);
			frac = progressFraction;
			stage = progressStage;
			done = progressDone;
		}
		if (auto e = ui::FindByName(ar, "loadBar"); e.IsSome())
			if (auto p = ar.GetComponent<ui::UiProgress>(e.Unwrap()); p.IsSome())
				p.Unwrap()->value = frac;
		if (auto e = ui::FindByName(ar, "loadStage"); e.IsSome())
			if (auto l = ar.GetComponent<ui::UiLabel>(e.Unwrap()); l.IsSome())
				l.Unwrap()->text = stage;

		gui.Tick(0.f);
		ren.SetDrawColor(sdl3::FColor::UI_APP_BG());
		ren.Clear();
		gui.Render(); // aucun widget Viewport3D encore vivant : ne touche pas `canvas`, cf. en-tête du fichier
		ren.Present();

		if (done)
			break;
	}

	// Joint le thread AVANT toute décision de sortie — jamais de thread GPU
	// encore actif pendant la destruction de `canvas`/`window` (fermeture
	// demandée pendant le chargement comme succès normal).
	warmupThread.Wait();

	if (!running)
		return 0;

	bool failed;
	String error;
	{
		sdl3::MutexGuard guard(progressMutex);
		failed = progressFailed;
		error = progressError;
	}
	if (failed) {
		std::cerr << "Préchauffage 3D: " << error.c_str() << "\n";
		return 1;
	}

	ui::DespawnTree(ar, loadingRoot);

	// ── Vraie UI (deux Viewport3D, cf. en-tête du fichier) — les pipelines
	// sont déjà compilés, ce premier vrai rendu sera donc immédiat. ─────────
	auto root = f.Row();
	root.Anchor(ui::Anchor::Center).Gap(16.f).Pad(20.f).WAuto().HAuto().Children(
		f.Panel().Gap(8.f).Pad(12.f).Children(
			f.Label("Panneau haut-étroit").FontSize(16.f),
			f.Label("Caméra de face (aspect ~0.52)").TextColor({150, 156, 178}),
			f.Viewport3D(sceneRoot, cameraFront).Size(220.f, 420.f)),
		f.Panel().Gap(8.f).Pad(12.f).Children(
			f.Label("Panneau large-bas").FontSize(16.f),
			f.Label("Caméra 3/4 côté (aspect ~1.9)").TextColor({150, 156, 178}),
			f.Viewport3D(sceneRoot, cameraSide).Size(420.f, 220.f)));
	root.Spawn();

	uint64_t lastTick = sdl3::GetTicksMS();
	float sceneAngle = 0.f;

	while (running) {
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}
			gui.HandleEvent(e);
		}
		gui.Tick(dt);

		// Fait tourner lentement la scène : les deux Viewport3D (même racine,
		// caméras différentes) se mettent à jour indépendamment chaque frame
		// via Viewport3DSystem::Update (appelé depuis gui.Render() ci-dessous).
		sceneAngle += dt * 0.35f;
		sceneRoot.SetRotation(math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, sceneAngle));

		ren.SetDrawColor(sdl3::FColor::UI_APP_BG());
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	return 0;
}
