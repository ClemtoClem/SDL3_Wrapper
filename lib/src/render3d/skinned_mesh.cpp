// Définitions de render3d/skinned_mesh.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/skinned_mesh.hpp"

namespace render3d {

// ── SkinnedMesh ──────────────────────────────────────────────────────────────

SkinnedMesh::SkinnedMesh(SkinnedGeometry geometry, Material material, std::unique_ptr<Bone> rootBone,
		std::vector<Bone *> bones)
	: m_geometry(std::move(geometry)), m_material(std::move(material)),
	  m_rootBone(static_cast<Bone *>(&Add(std::move(rootBone)))), m_bones(std::move(bones)) {
	m_inverseBindMatrices.reserve(m_bones.size());
	for (Bone *bone : m_bones)
		m_inverseBindMatrices.push_back(bone->WorldMatrix().Inverse());
}

std::vector<math::FMatrix4> SkinnedMesh::ComputeSkinMatrices() const {
	std::vector<math::FMatrix4> result;
	result.reserve(m_bones.size());
	for (size_t i = 0; i < m_bones.size(); ++i)
		result.push_back(m_bones[i]->WorldMatrix() * m_inverseBindMatrices[i]);
	return result;
}

void SkinnedMesh::OnDraw(Canvas &canvas) {
	std::vector<math::FMatrix4> skinMatrices = ComputeSkinMatrices();
	canvas.DrawSkinnedMesh(m_geometry, skinMatrices, m_material);
}

} // namespace render3d
