#pragma once
#include "canvas.hpp"
#include "shape.hpp"

namespace render3d {

/// Billboard toujours face à la caméra (three.js Sprite, M16 du plan) — un
/// Shape possédant un quad unité dans le plan XY (normale locale +Z) dont
/// OnDraw() recalcule la rotation à chaque frame pour faire face à la
/// caméra avant de déléguer à Shape::OnDraw() ; approche C++/CPU pure (pas
/// de changement de shader/pipeline), voir le plan.
///
/// Technique : deux FQuaternion::FromTo() composés — le premier aligne l'axe
/// local +Z sur la direction caméra, le second corrige le "roulis" autour de
/// cet axe pour aligner l'up local sur l'up caméra ORTHOGONALISÉ (pas
/// `camera.up` brut, qui n'a aucune raison d'être perpendiculaire à la
/// direction caméra->sprite — seul un up déjà orthogonalisé contre l'axe
/// déjà aligné garantit que la 2e rotation ne perturbe pas la 1re).
///
/// Limite connue : la rotation billboard est calculée en espace monde mais
/// appliquée via SetRotation() (espace local, voir Object3D) — correct sauf
/// si un ancêtre du Sprite a lui-même une rotation non-identité.
class Sprite : public Shape {
public:
	explicit Sprite(Material material, float width = 1.f, float height = 1.f)
		: Shape(MakeQuad(width, height), std::move(material)) {}

	void OnDraw(Canvas &canvas) override {
		const Camera &camera = canvas.GetCamera();
		math::FMatrix4 world = WorldMatrix();
		math::FVector3 worldPosition{world.m[12], world.m[13], world.m[14]};
		SetRotation(ComputeBillboardRotation(worldPosition, camera));
		Shape::OnDraw(canvas);
	}

	/// Calcule la rotation qui fait face à `camera` depuis `worldPosition` —
	/// exposée séparément d'OnDraw() pour rester testable sans device GPU
	/// (voir tests/render3d_smoke_test.cpp).
	[[nodiscard]] static math::FQuaternion ComputeBillboardRotation(const math::FVector3 &worldPosition,
																	 const Camera &camera) noexcept {
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

private:
	[[nodiscard]] static Mesh MakeQuad(float width, float height) noexcept {
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
};

} // namespace render3d
