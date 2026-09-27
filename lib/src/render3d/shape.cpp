// Définitions de render3d/shape.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/shape.hpp"

namespace render3d {

// ── Shape ────────────────────────────────────────────────────────────────────

void Shape::OnDraw(Canvas &canvas) {
	math::FMatrix4 world = WorldMatrix();
	const auto &groups = m_geometry.Groups();

	if (groups.empty()) {
		canvas.DrawMesh(m_geometry, world, MaterialFor(0));
		return;
	}
	for (const auto &group : groups)
		canvas.DrawMeshGroup(m_geometry, group.start, group.count, world, MaterialFor(group.materialIndex));
}

Material Shape::MaterialFor(size_t index) const noexcept {
	if (index < m_materials.size())
		return m_materials[index];
	if (!m_materials.empty())
		return m_materials[0];
	return Material::Default();
}

} // namespace render3d
