#pragma once
#include "../math/math.hpp"
#include "../sdl3/structs.hpp"

namespace render3d {

// DirectionalLight/AmbientLight correspondent à ce que LightUBO (mesh_phong.frag,
// le pipeline précompilé de Canvas) consomme. PointLight/SpotLight sont
// fonctionnels via render3d::ShaderBuilder (shader_chunks::LIGHTING_FUNC_PHONG/PBR,
// tableaux uPointLights/uSpotLights) et Canvas::SetLights — Area/Environment/Sky
// restent hors périmètre (voir le plan).
struct DirectionalLight {
	math::FVector3 direction{0.f, -1.f, 0.f};
	sdl3::Color color = sdl3::Color::WHITE();
	float intensity = 1.f;

	// Shadow mapping (voir le plan, M7-M8) : un unique volume ortho fixe,
	// centré sur `shadowTarget` — pas d'ajustement automatique aux limites de
	// la scène (comme three.js par défaut, DirectionalLightShadow.camera).
	bool castShadow = false;
	math::FVector3 shadowTarget{0.f, 0.f, 0.f};
	float shadowOrthoSize = 20.f;
	float shadowNear = 0.1f;
	float shadowFar = 100.f;

	[[nodiscard]] math::FMatrix4 ShadowViewProjection() const noexcept {
		math::FVector3 dir = direction.Normalize();
		math::FVector3 eye = shadowTarget - dir * (shadowFar * 0.5f);
		math::FVector3 up = (sdl3::Abs(dir.y) > 0.99f) ? math::FVector3{0.f, 0.f, 1.f} : math::FVector3{0.f, 1.f, 0.f};
		math::FMatrix4 view = math::FMatrix4::LookAt(eye, shadowTarget, up);
		math::FMatrix4 proj = math::FMatrix4::Ortho(-shadowOrthoSize, shadowOrthoSize, -shadowOrthoSize,
													 shadowOrthoSize, shadowNear, shadowFar);
		return proj * view;
	}
};

struct AmbientLight {
	sdl3::Color color{40, 40, 40};
	float intensity = 1.f;
};

/// Lumière ponctuelle omnidirectionnelle (three.js PointLight). `distance`
/// est la portée de coupure de l'atténuation (0 = illimitée, désactive la
/// coupure et ne laisse que la chute en carré inverse via `decay`) ;
/// `decay` est l'exposant de chute (2 = physiquement correct).
struct PointLight {
	math::FVector3 position{0.f, 0.f, 0.f};
	sdl3::Color color = sdl3::Color::WHITE();
	float intensity = 1.f;
	float distance = 0.f;
	float decay = 2.f;

	// Shadow mapping (voir le plan, M7/M9) : cubemap de distance (6 faces),
	// pas de sampler de comparaison — voir la note dans le plan sur le choix
	// "distance stockée + comparaison manuelle" plutôt qu'une depth cubemap.
	bool castShadow = false;
	float shadowNear = 0.1f;
	float shadowFar = 50.f;

	/// Matrice vue-projection pour la face `faceIndex` (0-5, ordre standard
	/// SDL_GPU_CUBEMAPFACE_*: +X,-X,+Y,-Y,+Z,-Z) — FOV fixe à 90° (couvre
	/// exactement une face de cube depuis le centre).
	[[nodiscard]] math::FMatrix4 ShadowViewProjection(int faceIndex) const noexcept {
		static constexpr math::FVector3 DIRS[6] = {{1.f, 0.f, 0.f},  {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
													{0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},  {0.f, 0.f, -1.f}};
		static constexpr math::FVector3 UPS[6] = {{0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},
												   {0.f, 0.f, -1.f}, {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}};
		math::FVector3 target = position + DIRS[faceIndex];
		math::FMatrix4 view = math::FMatrix4::LookAt(position, target, UPS[faceIndex]);
		math::FMatrix4 proj = math::FMatrix4::Perspective(1.57079632679f, 1.f, shadowNear, shadowFar);
		return proj * view;
	}
};

/// Lumière conique (three.js SpotLight). `angle` est le demi-angle du cône en
/// radians, `penumbra` (0-1) adoucit la transition entre le cœur pleinement
/// éclairé et le bord du cône.
struct SpotLight {
	math::FVector3 position{0.f, 0.f, 0.f};
	math::FVector3 direction{0.f, -1.f, 0.f};
	sdl3::Color color = sdl3::Color::WHITE();
	float intensity = 1.f;
	float distance = 0.f;
	float angle = 3.14159265f / 3.f; // π/3, comme three.js
	float penumbra = 0.f;
	float decay = 2.f;

	// Shadow mapping (voir le plan, M7-M8) : une depth map perspective,
	// réutilise `angle` comme FOV (avec une marge, voir ShadowViewProjection).
	bool castShadow = false;
	float shadowNear = 0.1f;
	float shadowFar = 50.f;

	[[nodiscard]] math::FMatrix4 ShadowViewProjection() const noexcept {
		math::FVector3 dir = direction.Normalize();
		math::FVector3 target = position + dir;
		math::FVector3 up = (sdl3::Abs(dir.y) > 0.99f) ? math::FVector3{0.f, 0.f, 1.f} : math::FVector3{0.f, 1.f, 0.f};
		math::FMatrix4 view = math::FMatrix4::LookAt(position, target, up);
		// Marge sur le FOV réel du cône pour couvrir la pénombre jusqu'au bord.
		float fov = sdl3::Clamp(angle * 2.2f, 0.1f, 3.0f);
		math::FMatrix4 proj = math::FMatrix4::Perspective(fov, 1.f, shadowNear, shadowFar);
		return proj * view;
	}
};

} // namespace render3d
