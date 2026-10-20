#pragma once
#include <memory>
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
///
/// La géométrie est PARTAGÉE (`std::shared_ptr<Mesh>`) : un maillage chargé
/// une fois (render3d::MeshResource, cf. resources.hpp) sert à tous les
/// Shape qui le montrent, tampons GPU compris — le transform reste propre à
/// chaque Shape. Construire un Shape à partir d'un Mesh par valeur lui en
/// donne un exemplaire à lui seul (comportement historique).
class Shape : public Object3D {
	std::shared_ptr<Mesh> m_geometry;
	std::vector<Material> m_materials;

public:
	Shape(Mesh geometry, Material material)
		: m_geometry(std::make_shared<Mesh>(std::move(geometry))), m_materials{std::move(material)} {}
	Shape(Mesh geometry, std::vector<Material> materials)
		: m_geometry(std::make_shared<Mesh>(std::move(geometry))), m_materials(std::move(materials)) {}
	/// Géométrie partagée (nullptr : rien à dessiner).
	Shape(std::shared_ptr<Mesh> geometry, Material material)
		: m_geometry(std::move(geometry)), m_materials{std::move(material)} {}

	/// La géométrie. ATTENTION : partagée, la modifier la modifie pour tous
	/// les Shape qui la montrent — pour en changer, SetGeometry.
	[[nodiscard]] Mesh &Geometry() noexcept { return *m_geometry; }
	[[nodiscard]] const Mesh &Geometry() const noexcept { return *m_geometry; }
	[[nodiscard]] const std::shared_ptr<Mesh> &SharedGeometry() const noexcept { return m_geometry; }
	void SetGeometry(std::shared_ptr<Mesh> geometry) noexcept { m_geometry = std::move(geometry); }
	void SetGeometry(Mesh geometry) { m_geometry = std::make_shared<Mesh>(std::move(geometry)); }
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
