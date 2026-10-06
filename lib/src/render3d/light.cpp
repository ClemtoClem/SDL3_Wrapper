// Définitions de render3d/light.hpp
#include "render3d/light.hpp"

namespace render3d {

// ── DirectionalLight ─────────────────────────────────────────────────────────

math::FMatrix4 DirectionalLight::ShadowViewProjection() const noexcept {
	math::FVector3 dir = direction.Normalize();
	math::FVector3 eye = shadowTarget - dir * (shadowFar * 0.5f);
	math::FVector3 up = (sdl3::Abs(dir.y) > 0.99f) ? math::FVector3{0.f, 0.f, 1.f} : math::FVector3{0.f, 1.f, 0.f};
	math::FMatrix4 view = math::FMatrix4::LookAt(eye, shadowTarget, up);
	math::FMatrix4 proj = math::FMatrix4::Ortho(-shadowOrthoSize, shadowOrthoSize, -shadowOrthoSize,
												 shadowOrthoSize, shadowNear, shadowFar);
	return proj * view;
}

// ── PointLight ───────────────────────────────────────────────────────────────

math::FMatrix4 PointLight::ShadowViewProjection(int faceIndex) const noexcept {
	static constexpr math::FVector3 DIRS[6] = {{1.f, 0.f, 0.f},  {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
												{0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},  {0.f, 0.f, -1.f}};
	static constexpr math::FVector3 UPS[6] = {{0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},
											   {0.f, 0.f, -1.f}, {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}};
	math::FVector3 target = position + DIRS[faceIndex];
	math::FMatrix4 view = math::FMatrix4::LookAt(position, target, UPS[faceIndex]);
	math::FMatrix4 proj = math::FMatrix4::Perspective(1.57079632679f, 1.f, shadowNear, shadowFar);
	return proj * view;
}

// ── SpotLight ────────────────────────────────────────────────────────────────

math::FMatrix4 SpotLight::ShadowViewProjection() const noexcept {
	math::FVector3 dir = direction.Normalize();
	math::FVector3 target = position + dir;
	math::FVector3 up = (sdl3::Abs(dir.y) > 0.99f) ? math::FVector3{0.f, 0.f, 1.f} : math::FVector3{0.f, 1.f, 0.f};
	math::FMatrix4 view = math::FMatrix4::LookAt(position, target, up);
	// Marge sur le FOV réel du cône pour couvrir la pénombre jusqu'au bord.
	float fov = sdl3::Clamp(angle * 2.2f, 0.1f, 3.0f);
	math::FMatrix4 proj = math::FMatrix4::Perspective(fov, 1.f, shadowNear, shadowFar);
	return proj * view;
}

} // namespace render3d
