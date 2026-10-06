// Exemple : démonstration bout en bout de tout ce qui a été ajouté après
// render3d_cube.cpp — Object3D (hiérarchie parent/enfants), les nouvelles
// géométries procédurales (Box/Sphere/Torus/TorusKnot), un matériau PBR
// metallic-roughness compilé via ShaderBuilder (Material::Pbr, voir
// canvas.hpp/shader_builder.hpp), et deux lumières ponctuelles + une lumière
// spot (Canvas::SetLights).
#include <iostream>
#include <memory>

#include "render3d/canvas.hpp"
#include "render3d/shape.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/canvas_window_frame.hpp"

static constexpr int WIN_W = 1000;
static constexpr int WIN_H = 600;

int main() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}

	auto windowResult =
		sdl3::Window::Create(u8"render3d:: - showcase", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
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

		render3d::Camera camera;
		camera.position = {0.f, 2.5f, -9.f};
		camera.target = {0.f, 0.f, 0.f};
		camera.aspect = float(WIN_W) / float(WIN_H);
		canvas.SetCamera(camera);

		render3d::DirectionalLight sun;
		sun.direction = {0.2f, -0.4f, 0.6f};
		sun.intensity = 0.5f; // discret : les lumières ponctuelles/spot doivent porter la scène
		render3d::AmbientLight ambient;
		ambient.color = sdl3::Color{15, 15, 20};
		canvas.SetLighting(sun, ambient);
		canvas.SetBackgroundColor(sdl3::Color{10, 10, 14});

		render3d::PointLight redLight;
		redLight.position = {-4.f, 2.f, -2.f};
		redLight.color = sdl3::Color::RED();
		redLight.intensity = 3.f;
		redLight.decay = 1.5f;

		render3d::PointLight blueLight;
		blueLight.position = {4.f, 2.f, -2.f};
		blueLight.color = sdl3::Color::BLUE();
		blueLight.intensity = 3.f;
		blueLight.decay = 1.5f;

		render3d::SpotLight spot;
		spot.position = {0.f, 6.f, -4.f};
		spot.direction = {0.f, -1.f, 0.3f};
		spot.color = sdl3::Color::WHITE();
		spot.intensity = 4.f;
		spot.angle = 0.4f;
		spot.penumbra = 0.4f;
		spot.decay = 1.2f;

		render3d::PointLight pointLights[2] = {redLight, blueLight};
		render3d::SpotLight spotLights[1] = {spot};
		canvas.SetLights(pointLights, spotLights);

		// Racine tournant lentement sur Y : fait pivoter toute la rangée
		// d'objets ensemble (démonstration de la composition Object3D).
		auto root = std::make_unique<render3d::Object3D>();
		root->SetName(String("Showcase root"));

		auto &box = static_cast<render3d::Shape &>(root->Add(
			std::make_unique<render3d::Shape>(render3d::Mesh::Cube(1.4f), render3d::Material::Plastic(sdl3::Color{80, 140, 220}))));
		box.SetPosition({-4.5f, 0.f, 0.f});

		auto &sphere = static_cast<render3d::Shape &>(root->Add(std::make_unique<render3d::Shape>(
			render3d::Mesh::Sphere(0.9f, 32, 24), render3d::Material::Metal(sdl3::Color{220, 220, 230}))));
		sphere.SetPosition({-1.5f, 0.f, 0.f});

		auto &torus = static_cast<render3d::Shape &>(root->Add(std::make_unique<render3d::Shape>(
			render3d::Mesh::Torus(0.8f, 0.3f, 16, 32), render3d::Material::Pbr(sdl3::Color{230, 180, 60}, 0.9f, 0.25f))));
		torus.SetPosition({1.5f, 0.f, 0.f});

		auto &knot = static_cast<render3d::Shape &>(root->Add(std::make_unique<render3d::Shape>(
			render3d::Mesh::TorusKnot(0.6f, 0.22f, 96, 12, 2, 3),
			render3d::Material::Pbr(sdl3::Color{200, 60, 200}, 0.2f, 0.4f))));
		knot.SetPosition({4.5f, 0.f, 0.f});

		// Encadrement de fenêtre (barre de titre, barre d'état) : rendu en
		// logiciel puis copié par-dessus la scène 3D (cf. ui::CanvasWindowFrame).
		auto frameResult = ui::CanvasWindowFrame::Create(
			window, {.title = "render3d:: - géométries, PBR et lumières", .appIcon = Some(ui::MaterialIcons::CATEGORY), .status = "Box, Sphere, Torus, TorusKnot · deux lumières ponctuelles et un spot."});
		if (!frameResult) {
			std::cerr << "CanvasWindowFrame: " << frameResult.Error().CStr() << "\n";
			return 1;
		}
		ui::CanvasWindowFrame &frame = *frameResult.Value();

		sdl3::FrameTimestep timestep(60.f);
		float rootAngle = 0.f, spinAngle = 0.f;
		bool running = true;

		while (running) {
			timestep.Begin();

			while (auto ev = sdl3::PollEvent()) {
				auto &e = ev.Value();
				if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE))
					running = false;
				frame.HandleEvent(e);
			}
			frame.Update(timestep.GetDelta());
			if (frame.CloseRequested())
				running = false;

			float dt = timestep.GetDelta();
			rootAngle += dt * 0.25f;
			spinAngle += dt * 1.2f;

			root->SetRotation(math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, rootAngle));
			box.SetRotation(math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, spinAngle));
			sphere.SetRotation(math::FQuaternion::FromAxisAngle({1.f, 0.f, 0.f}, spinAngle));
			torus.SetRotation(math::FQuaternion::FromAxisAngle({1.f, 1.f, 0.f}, spinAngle));
			knot.SetRotation(math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, -spinAngle));

			if (canvas.Begin()) {
				canvas.DrawObject(*root);
				frame.Draw(canvas);
				canvas.End();
			}

			timestep.End();
		}
	}

	return 0;
}
