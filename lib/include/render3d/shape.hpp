#pragma once
#include <vector>

#include "canvas.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "object3d.hpp"

namespace render3d {

/// Object3D concret combinant une géométrie (Mesh) et un ou plusieurs
/// matériaux — le "classe Shape avec une matrice de transformation, des
/// matériaux pour chaque face" demandé : la matrice vient d'Object3D, le
/// matériau par face de Mesh::Group (Mesh::Cube() en pose un par face,
/// index 0..5) combiné à `materials[group.materialIndex]`.
class Shape : public Object3D {
	Mesh m_geometry;
	std::vector<Material> m_materials;

public:
	Shape(Mesh geometry, Material material) : m_geometry(std::move(geometry)), m_materials{std::move(material)} {}
	Shape(Mesh geometry, std::vector<Material> materials)
		: m_geometry(std::move(geometry)), m_materials(std::move(materials)) {}

	[[nodiscard]] Mesh &Geometry() noexcept { return m_geometry; }
	[[nodiscard]] const Mesh &Geometry() const noexcept { return m_geometry; }
	[[nodiscard]] std::vector<Material> &Materials() noexcept { return m_materials; }
	[[nodiscard]] const std::vector<Material> &Materials() const noexcept { return m_materials; }

	void OnDraw(Canvas &canvas) override;

private:
	/// `index` hors bornes -> premier matériau (ou Material::Default() si
	/// aucun n'a été fourni) plutôt qu'un crash — un Shape sans matériau pour
	/// une face donnée doit rester dessinable.
	[[nodiscard]] Material MaterialFor(size_t index) const noexcept;
};

} // namespace render3d
