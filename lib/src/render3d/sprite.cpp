// Définitions de render3d/sprite.hpp
#include "render3d/sprite.hpp"

namespace render3d {

// ── Sprite ───────────────────────────────────────────────────────────────────

void Sprite::OnDraw(Canvas &canvas) {
	const Camera &camera = canvas.GetCamera();
	math::FMatrix4 world = WorldMatrix();
	math::FVector3 worldPosition{world.m[12], world.m[13], world.m[14]};
	SetRotation(ComputeBillboardRotation(worldPosition, camera));
	Shape::OnDraw(canvas);
}

math::FQuaternion Sprite::ComputeBillboardRotation(const math::FVector3 &worldPosition, const Camera &camera) noexcept {
	math::FVector3 toCamera = (camera.position - worldPosition).Normalize();
	// Orthogonalise camera.up contre toCamera (Gram-Schmidt) : sans ça,
	// FromTo(rotatedUp, camera.up) ci-dessous perturbe l'alignement déjà
	// établi par faceCamera dès que camera.up n'est pas exactement
	// perpendiculaire à toCamera (le cas général).
	math::FVector3 right = camera.up.Cross(toCamera).Normalize();
	math::FVector3 orthogonalUp = toCamera.Cross(right);

	math::FQuaternion faceCamera = math::FQuaternion::FromTo({0.f, 0.f, 1.f}, toCamera);
	math::FVector3 rotatedUp = faceCamera.Rotate({0.f, 1.f, 0.f});
	math::FQuaternion fixRoll = math::FQuaternion::FromTo(rotatedUp, orthogonalUp);
	return fixRoll * faceCamera;
}

Mesh Sprite::MakeQuad(float width, float height) noexcept {
	float hw = width * 0.5f, hh = height * 0.5f;
	std::vector<Vertex3D> vertices = {
		{{-hw, -hh, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}, sdl3::Color::WHITE()},
		{{hw, -hh, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}, sdl3::Color::WHITE()},
		{{hw, hh, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}, sdl3::Color::WHITE()},
		{{-hw, hh, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}, sdl3::Color::WHITE()},
	};
	std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3}; // CCW vu de +Z (face avant), voir canvas.hpp
	return Mesh(std::move(vertices), std::move(indices));
}

} // namespace render3d
