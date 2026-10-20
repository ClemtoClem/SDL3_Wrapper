// Définitions de render3d/shape.hpp
#include <render3d/shape.hpp>

namespace render3d {

// ── Shape ────────────────────────────────────────────────────────────────────

void Shape::OnDraw(Canvas &canvas) {
	if (!m_geometry)
		return;
	math::FMatrix4 world = WorldMatrix();
	Mesh &geometry = *m_geometry;
	const auto &groups = geometry.Groups();

	if (groups.empty()) {
		canvas.DrawMesh(geometry, world, MaterialFor(0));
		return;
	}
	for (const auto &group : groups)
		canvas.DrawMeshGroup(geometry, group.start, group.count, world, MaterialFor(group.materialIndex));
}

Material Shape::MaterialFor(size_t index) const noexcept {
	if (index < m_materials.size())
		return m_materials[index];
	if (!m_materials.empty())
		return m_materials[0];
	return Material::Default();
}

} // namespace render3d
