// Exemple : premier bout en bout de render3d:: — Canvas + Mesh::Cube +
// Material Phong + Camera + DirectionalLight/AmbientLight, un cube texturé
// (couleur unie) qui tourne. Démontre la pile complète GPU native construite
// sur sdl3::Gpu* (voir lib/render3d/canvas.hpp).
#include <iostream>

#include "render3d/canvas.hpp"
#include "sdl3/sdl3.hpp"

static constexpr int WIN_W = 900;
static constexpr int WIN_H = 600;

int main() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}

	auto windowResult = sdl3::Window::Create(u8"render3d:: - rotating cube", WIN_W, WIN_H, sdl3::window_flags::RESIZABLE);
	if (!windowResult) {
		std::cerr << "Window: " << windowResult.Error().CStr() << "\n";
		return 1;
	}
	sdl3::Window &window = windowResult.Value();

	// Le Canvas (device GPU, shaders intégrés, cache de pipelines) doit être
	// détruit avant SDL_Quit() : scope imbriqué (cf. plan, leçon ECS_2).
	{
		auto canvasResult = render3d::Canvas::Create(window, WIN_W, WIN_H);
		if (!canvasResult) {
			std::cerr << "Canvas::Create: " << canvasResult.Error().CStr() << "\n";
			return 1;
		}
		render3d::Canvas canvas = std::move(canvasResult.Value());

		render3d::Mesh cube = render3d::Mesh::Cube();

		render3d::Camera camera;
		camera.position = {0.f, 1.5f, -4.f};
		camera.aspect = float(WIN_W) / float(WIN_H);

		render3d::Material material = render3d::Material::Plastic(sdl3::Color{80, 140, 220});

		// Éclairage type "lampe frontale" : direction proche de l'axe caméra->cible,
		// pour qu'une face du cube reste bien éclairée quel que soit l'angle de
		// rotation (une direction fixe plus "de haut" laisserait la plupart des
		// faces visibles quasi uniquement en lumière ambiante la majeure partie
		// du temps — correct optiquement, mais peu lisible comme démonstration).
		render3d::DirectionalLight sun;
		sun.direction = {0.f, -0.3f, 0.9f};
		sun.intensity = 1.3f;
		render3d::AmbientLight ambient;
		ambient.color = sdl3::Color{70, 70, 85};

		canvas.SetCamera(camera);
		canvas.SetLighting(sun, ambient);
		canvas.SetBackgroundColor(sdl3::Color{20, 22, 30});

		sdl3::FrameTimestep timestep(60.f);
		float angle = 0.f;
		bool running = true;

		while (running) {
			timestep.Begin();

			while (auto ev = sdl3::PollEvent()) {
				auto &e = ev.Value();
				if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE))
					running = false;
			}

			angle += timestep.GetDelta() * 0.8f;
			math::FMatrix4 transform = math::FMatrix4::RotateY(angle) * math::FMatrix4::RotateX(angle * 0.6f);

			if (canvas.Begin()) {
				canvas.DrawMesh(cube, transform, material);
				canvas.End();
			}

			timestep.End();
		}
	}

	return 0;
}
