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

	void OnDraw(Canvas &canvas) override;

	/// Calcule la rotation qui fait face à `camera` depuis `worldPosition` —
	/// exposée séparément d'OnDraw() pour rester testable sans device GPU
	/// (voir tests/render3d_smoke_test.cpp).
	[[nodiscard]] static math::FQuaternion ComputeBillboardRotation(const math::FVector3 &worldPosition,
																	 const Camera &camera) noexcept;

private:
	[[nodiscard]] static Mesh MakeQuad(float width, float height) noexcept;
};

} // namespace render3d
