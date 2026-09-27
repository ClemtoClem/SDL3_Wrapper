// Définitions de render3d/lod.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/lod.hpp"

namespace render3d {

// ── LOD ──────────────────────────────────────────────────────────────────────

Object3D & LOD::AddLevel(float distance, std::unique_ptr<Object3D> child) {
	Object3D &ref = Add(std::move(child));
	m_levels.push_back({distance, &ref});
	std::sort(m_levels.begin(), m_levels.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	return ref;
}

void LOD::OnDraw(Canvas &canvas) {
	if (m_levels.empty())
		return;

	math::FMatrix4 world = WorldMatrix();
	math::FVector3 worldPosition{world.m[12], world.m[13], world.m[14]};
	float distance = worldPosition.Distance(canvas.GetCamera().position);

	// Niveau le plus éloigné (donc le moins détaillé) dont le seuil est
	// atteint — three.js LOD.getCurrentLevel() : le dernier niveau dont
	// `distance` est dépassée l'emporte (un niveau ajouté à distance=0
	// reste donc affiché tant qu'aucun seuil supérieur n'est atteint).
	size_t selected = 0;
	for (size_t i = 0; i < m_levels.size(); ++i)
		if (distance >= m_levels[i].first)
			selected = i;

	for (size_t i = 0; i < m_levels.size(); ++i)
		m_levels[i].second->SetVisible(i == selected);
}

} // namespace render3d
