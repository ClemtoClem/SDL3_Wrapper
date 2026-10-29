#pragma once
#include <vector>

#include "canvas.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "object3d.hpp"

namespace render3d {

/// Rendu instancié GPU réel (three.js InstancedMesh, M16 du plan) : un seul
/// Mesh + Material dessiné une fois par transform de `Instances()`, via un
/// seul appel de dessin (voir Canvas::DrawInstancedMesh,
/// shader_chunks::INSTANCE_ATTRIBUTES). Contrairement à Shape, n'hérite pas
/// de la transform Object3D pour composer avec les instances — chaque
/// matrice d'`Instances()` EST la transform monde complète de cette
/// instance (comme three.js InstancedMesh.setMatrixAt) ; la propre position/
/// rotation/scale d'InstancedMesh (héritées d'Object3D) ne sont pas
/// utilisées par OnDraw() — limite assumée, voir le plan (pas d'ombres/IBL
/// pour les draws instanciés non plus, cette phase).
class InstancedMesh : public Object3D {
	Mesh m_geometry;
	Material m_material;
	std::vector<math::FMatrix4> m_instances;

public:
	InstancedMesh(Mesh geometry, Material material) : m_geometry(std::move(geometry)), m_material(std::move(material)) {}

	[[nodiscard]] Mesh &Geometry() noexcept { return m_geometry; }
	[[nodiscard]] const Mesh &Geometry() const noexcept { return m_geometry; }
	[[nodiscard]] Material &GetMaterial() noexcept { return m_material; }
	[[nodiscard]] const Material &GetMaterial() const noexcept { return m_material; }
	[[nodiscard]] std::vector<math::FMatrix4> &Instances() noexcept { return m_instances; }
	[[nodiscard]] const std::vector<math::FMatrix4> &Instances() const noexcept { return m_instances; }

	void OnDraw(Canvas &canvas) override { canvas.DrawInstancedMesh(m_geometry, m_instances, m_material); }
};

} // namespace render3d
