// Exemple final de la Phase 3 (M19 du plan) : démonstration bout en bout de
// tout ce qui a été ajouté après render3d_showcase.cpp — shadow mapping réel
// (directionnelle + spot + point, M7-M9), IBL split-sum complet chargé
// depuis un .hdr équirectangulaire (M10-M14, voir
// assets/textures/equirectangularmaps/procedural_sky.hdr — ciel procédural
// généré une fois, aucun vrai fichier .hdr disponible dans cet
// environnement, voir le plan), et les 6 sous-classes Object3D restantes
// (Points/LineSegments/Sprite/InstancedMesh/SkinnedMesh+Bone/LOD, M15-M18).
#include <cmath>
#include <iostream>
#include <memory>

#include "render3d/canvas.hpp"
#include "render3d/environment.hpp"
#include "render3d/instanced_mesh.hpp"
#include "render3d/lod.hpp"
#include "render3d/point_line.hpp"
#include "render3d/shape.hpp"
#include "render3d/skinned_mesh.hpp"
#include "render3d/sprite.hpp"
#include "sdl3/sdl3.hpp"

static constexpr int WIN_W = 1000;
static constexpr int WIN_H = 600;

int main() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}

	auto windowResult = sdl3::Window::Create(u8"render3d:: - shadows + IBL + object3d showcase", WIN_W, WIN_H,
											 sdl3::window_flags::RESIZABLE);
	if (!windowResult) {
		std::cerr << "Window: " << windowResult.Error().CStr() << "\n";
		return 1;
	}
	sdl3::Window &window = windowResult.Value();

	{
		auto canvasResult = render3d::Canvas::Create(window, WIN_W, WIN_H);
		if (!canvasResult) {
			std::cerr << "Canvas::Create: " << canvasResult.Error().CStr() << "\n";
			return 1;
		}
		render3d::Canvas canvas = std::move(canvasResult.Value());

		render3d::Camera camera;
		camera.position = {0.f, 4.f, -11.f};
		camera.target = {0.f, 1.5f, 0.f};
		camera.aspect = float(WIN_W) / float(WIN_H);
		canvas.SetCamera(camera);

		// ── Environnement (M11-M14) : skybox + IBL split-sum complet ────────
		auto envResult = render3d::LoadEnvironmentFromHdr(
			canvas.Device(), String("assets/textures/equirectangularmaps/procedural_sky.hdr"));
		if (!envResult) {
			std::cerr << "LoadEnvironmentFromHdr: " << envResult.Error().CStr() << "\n";
			return 1;
		}
		render3d::Environment env = std::move(envResult.Value());
		canvas.SetEnvironment(env);

		// ── Lumières, toutes projetant une ombre réelle (M7-M9) ──────────────
		render3d::DirectionalLight sun;
		sun.direction = {0.3f, -0.7f, 0.4f};
		sun.intensity = 0.6f;
		sun.castShadow = true;
		sun.shadowTarget = {0.f, 0.f, 0.f};
		sun.shadowOrthoSize = 10.f;
		render3d::AmbientLight ambient;
		ambient.color = sdl3::Color{10, 10, 15};
		canvas.SetLighting(sun, ambient);
		canvas.SetBackgroundColor(sdl3::Color{5, 5, 8});

		render3d::PointLight point;
		point.position = {-3.f, 3.f, -2.f};
		point.color = sdl3::Color::RED();
		point.intensity = 4.f;
		point.decay = 1.5f;
		point.castShadow = true;

		render3d::SpotLight spot;
		spot.position = {3.f, 5.f, -3.f};
		spot.direction = {-0.3f, -1.f, 0.4f};
		spot.color = sdl3::Color::WHITE();
		spot.intensity = 6.f;
		spot.angle = 0.5f;
		spot.penumbra = 0.3f;
		spot.castShadow = true;

		render3d::PointLight pointLights[1] = {point};
		render3d::SpotLight spotLights[1] = {spot};
		canvas.SetLights(pointLights, spotLights);

		// ── Racine de la scène ────────────────────────────────────────────────
		auto root = std::make_unique<render3d::Object3D>();
		root->SetName(String("Showcase M19"));

		// Sol (receveur d'ombre) + cube et sphère PBR (projettent une ombre,
		// dessinés via Shape -> passent par la pré-passe d'ombre de Canvas::End()).
		auto &ground = static_cast<render3d::Shape &>(root->Add(std::make_unique<render3d::Shape>(
			render3d::Mesh::Plane(20.f, 20.f), render3d::Material::Plastic(sdl3::Color{90, 90, 100}))));
		ground.SetRotation(math::FQuaternion::FromAxisAngle({1.f, 0.f, 0.f}, -1.5707963f));

		auto &cube = static_cast<render3d::Shape &>(root->Add(std::make_unique<render3d::Shape>(
			render3d::Mesh::Cube(1.5f), render3d::Material::Pbr(sdl3::Color{200, 60, 60}, 0.1f, 0.4f))));
		cube.SetPosition({-2.f, 1.2f, -1.f});

		auto &sphere = static_cast<render3d::Shape &>(root->Add(std::make_unique<render3d::Shape>(
			render3d::Mesh::Sphere(0.9f, 32, 24), render3d::Material::Pbr(sdl3::Color{220, 220, 230}, 1.f, 0.15f))));
		sphere.SetPosition({1.5f, 1.5f, -1.5f});

		// ── Points (M15) : petit nuage de particules ────────────────────────
		std::vector<math::FVector3> pointPositions;
		for (int i = 0; i < 30; ++i) {
			float a = float(i) * 0.6f;
			pointPositions.push_back({math::FVector3{4.5f + 0.4f * sdl3::Cos(a), 1.f + 0.05f * float(i),
													  -3.f + 0.4f * sdl3::Sin(a)}});
		}
		auto &points = static_cast<render3d::Points &>(root->Add(std::make_unique<render3d::Points>(
			render3d::Mesh::FromPoints(pointPositions, sdl3::Color::YELLOW()),
			render3d::Material::Unlit(sdl3::Color::YELLOW()))));
		(void)points;

		// ── LineSegments (M15) : axes de repère au sol ──────────────────────
		std::vector<math::FVector3> linePositions = {
			{-5.f, 0.01f, 4.f}, {5.f, 0.01f, 4.f}, {-5.f, 0.01f, -4.f}, {5.f, 0.01f, -4.f},
			{-5.f, 0.01f, 4.f}, {-5.f, 0.01f, -4.f}, {5.f, 0.01f, 4.f}, {5.f, 0.01f, -4.f},
		};
		root->Add(std::make_unique<render3d::LineSegments>(
			render3d::Mesh::FromLineSegments(linePositions, sdl3::Color::CYAN()),
			render3d::Material::Unlit(sdl3::Color::CYAN())));

		// ── Sprite (M16) : billboard toujours face caméra ───────────────────
		auto &sprite = static_cast<render3d::Sprite &>(
			root->Add(std::make_unique<render3d::Sprite>(render3d::Material::Unlit(sdl3::Color{255, 200, 80}), 0.8f, 0.8f)));
		sprite.SetPosition({0.f, 3.2f, -2.f});

		// ── InstancedMesh (M16) : petite grille de cubes en un seul draw ────
		auto &instanced = static_cast<render3d::InstancedMesh &>(root->Add(std::make_unique<render3d::InstancedMesh>(
			render3d::Mesh::Cube(0.35f), render3d::Material::Plastic(sdl3::Color{80, 200, 120}))));
		for (int ix = 0; ix < 4; ++ix)
			for (int iz = 0; iz < 3; ++iz)
				instanced.Instances().push_back(
					math::FMatrix4::Translate(3.f + float(ix) * 0.6f, 0.3f, 1.f + float(iz) * 0.6f));

		// ── SkinnedMesh + Bone (M17) : 2 os, animé (pose manuelle par frame,
		// pas de clip d'animation — voir le plan) ───────────────────────────
		std::vector<render3d::SkinnedVertex3D> skinnedVerts;
		std::vector<uint32_t> skinnedIndices;
		{
			auto addQuad = [&](math::FVector3 a, math::FVector3 b, math::FVector3 c, math::FVector3 d,
							   float boneIndex, sdl3::Color color) {
				uint32_t base = uint32_t(skinnedVerts.size());
				for (auto &p : {a, b, c, d})
					skinnedVerts.push_back(render3d::SkinnedVertex3D{
						p, {0.f, 0.f, 1.f}, {0.f, 0.f}, color, {boneIndex, 0.f, 0.f, 0.f}, {1.f, 0.f, 0.f, 0.f}});
				for (uint32_t idx : {0u, 1u, 2u, 0u, 2u, 3u})
					skinnedIndices.push_back(base + idx);
			};
			// Segment bas (os racine, y in [0,0.8]) + segment haut (os enfant,
			// pivot (0,0.8,0), y in [0.8,1.6]) — un petit "bras" articulé.
			addQuad({-0.25f, 0.f, 0.f}, {0.25f, 0.f, 0.f}, {0.25f, 0.8f, 0.f}, {-0.25f, 0.8f, 0.f}, 0.f,
					sdl3::Color{200, 140, 90});
			addQuad({-0.2f, 0.8f, 0.f}, {0.2f, 0.8f, 0.f}, {0.2f, 1.6f, 0.f}, {-0.2f, 1.6f, 0.f}, 1.f,
					sdl3::Color{160, 100, 60});
		}
		auto rootBoneOwned = std::make_unique<render3d::Bone>();
		auto childBoneOwned = std::make_unique<render3d::Bone>();
		childBoneOwned->SetPosition({0.f, 0.8f, 0.f});
		auto &childBoneRef = static_cast<render3d::Bone &>(rootBoneOwned->Add(std::move(childBoneOwned)));
		render3d::Bone *rootBonePtr = rootBoneOwned.get();
		render3d::Bone *childBonePtr = &childBoneRef;
		std::vector<render3d::Bone *> skeletonBones = {rootBonePtr, childBonePtr};

		auto &skinned = static_cast<render3d::SkinnedMesh &>(root->Add(std::make_unique<render3d::SkinnedMesh>(
			render3d::SkinnedGeometry(skinnedVerts, skinnedIndices), render3d::Material::Unlit(sdl3::Color::WHITE()),
			std::move(rootBoneOwned), skeletonBones)));
		skinned.SetPosition({-4.5f, 0.f, -2.f});

		// ── LOD (M18) : 3 niveaux de détail (haute/moyenne/basse résolution
		// de sphère), sélectionné selon la distance réelle à la caméra ──────
		auto lodPtr = std::make_unique<render3d::LOD>();
		render3d::LOD &lod = *lodPtr;
		lod.AddLevel(0.f, std::make_unique<render3d::Shape>(render3d::Mesh::Sphere(0.7f, 32, 24),
															 render3d::Material::Metal(sdl3::Color{180, 180, 200})));
		lod.AddLevel(8.f, std::make_unique<render3d::Shape>(render3d::Mesh::Sphere(0.7f, 12, 8),
															 render3d::Material::Metal(sdl3::Color{180, 180, 200})));
		lod.AddLevel(20.f, std::make_unique<render3d::Shape>(render3d::Mesh::Sphere(0.7f, 6, 4),
															  render3d::Material::Metal(sdl3::Color{180, 180, 200})));
		root->Add(std::move(lodPtr));
		lod.SetPosition({0.f, 1.f, 3.f}); // ~9 unités de la caméra -> niveau moyen attendu

		sdl3::FrameTimestep timestep(60.f);
		float animTime = 0.f;
		bool running = true;

		while (running) {
			timestep.Begin();

			while (auto ev = sdl3::PollEvent()) {
				auto &e = ev.Value();
				if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE))
					running = false;
			}

			float dt = timestep.GetDelta();
			animTime += dt;
			cube.SetRotation(math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, animTime * 0.6f));
			// Pose manuelle du bras skinné (pas de clip d'animation, voir plus haut).
			childBonePtr->SetRotation(math::FQuaternion::FromAxisAngle({0.f, 0.f, 1.f}, sdl3::Sin(animTime) * 0.8f));

			if (canvas.Begin()) {
				canvas.DrawObject(*root);
				canvas.End();
			}

			timestep.End();
		}
	}

	return 0;
}
