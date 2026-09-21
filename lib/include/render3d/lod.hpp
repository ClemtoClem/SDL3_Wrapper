#pragma once
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "canvas.hpp"
#include "object3d.hpp"

namespace render3d {

/// Niveau de détail par distance caméra (three.js LOD, M18 du plan) — chaque
/// niveau est un Object3D normal possédé via Object3D::Add() (comme
/// n'importe quel enfant) ; OnDraw() ne dessine RIEN lui-même, il bascule la
/// visibilité des niveaux (Object3D::SetVisible()) avant que
/// Object3D::Traverse() (voir DrawObject/Canvas) ne recurse dans les
/// enfants — celui-ci saute déjà les sous-arbres invisibles, donc aucun
/// changement de Canvas/pipeline n'est nécessaire ici (voir le plan).
/// Distance simple caméra->nœud (pas de bounding-sphere/frustum culling —
/// même simplification que three.js LOD par défaut).
class LOD : public Object3D {
	std::vector<std::pair<float, Object3D *>> m_levels; // (distance seuil, possédé via Add())

public:
	LOD() = default;

	/// Ajoute un niveau visible à partir de `distance` (voir OnDraw ci-dessous
	/// pour la règle de sélection) — retriés par distance croissante à
	/// chaque appel, l'ordre d'appel n'a donc pas d'importance.
	Object3D &AddLevel(float distance, std::unique_ptr<Object3D> child) {
		Object3D &ref = Add(std::move(child));
		m_levels.push_back({distance, &ref});
		std::sort(m_levels.begin(), m_levels.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		return ref;
	}

	[[nodiscard]] const std::vector<std::pair<float, Object3D *>> &Levels() const noexcept { return m_levels; }

	void OnDraw(Canvas &canvas) override {
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
};

} // namespace render3d
