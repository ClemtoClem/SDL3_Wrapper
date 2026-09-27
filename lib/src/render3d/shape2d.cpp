// Définitions de render3d/shape2d.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/shape2d.hpp"

namespace render3d {

// ── Shape2D ──────────────────────────────────────────────────────────────────

std::pair<std::vector<math::FVector2>, std::vector<uint32_t>> Shape2D::Triangulate() const {
	std::vector<math::FVector2> merged = m_contour;
	for (const auto &hole : m_holes)
		MergeHole(merged, hole);
	return {merged, EarClip(merged)};
}

void Shape2D::MergeHole(std::vector<math::FVector2> &contour, const std::vector<math::FVector2> &hole) {
	if (hole.empty())
		return;
	size_t bestOuter = 0, bestHole = 0;
	float bestDistSq = 1e30f;
	for (size_t i = 0; i < contour.size(); ++i) {
		for (size_t j = 0; j < hole.size(); ++j) {
			float dx = contour[i].x - hole[j].x, dy = contour[i].y - hole[j].y;
			float distSq = dx * dx + dy * dy;
			if (distSq < bestDistSq) {
				bestDistSq = distSq;
				bestOuter = i;
				bestHole = j;
			}
		}
	}
	std::vector<math::FVector2> result;
	result.reserve(contour.size() + hole.size() + 2);
	for (size_t i = 0; i <= bestOuter; ++i)
		result.push_back(contour[i]);
	for (size_t k = 0; k <= hole.size(); ++k)
		result.push_back(hole[(bestHole + k) % hole.size()]);
	for (size_t i = bestOuter; i < contour.size(); ++i)
		result.push_back(contour[i]);
	contour = std::move(result);
}

bool Shape2D::PointInTriangle(const math::FVector2 &p, const math::FVector2 &a,
		const math::FVector2 &b, const math::FVector2 &c) noexcept {
	auto sign = [](const math::FVector2 &p1, const math::FVector2 &p2, const math::FVector2 &p3) {
		return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
	};
	float d1 = sign(p, a, b), d2 = sign(p, b, c), d3 = sign(p, c, a);
	bool hasNeg = (d1 < 0.f) || (d2 < 0.f) || (d3 < 0.f);
	bool hasPos = (d1 > 0.f) || (d2 > 0.f) || (d3 > 0.f);
	return !(hasNeg && hasPos);
}

std::vector<uint32_t> Shape2D::EarClip(const std::vector<math::FVector2> &poly) {
	std::vector<uint32_t> indices;
	size_t n = poly.size();
	if (n < 3)
		return indices;

	std::vector<uint32_t> remaining(n);
	for (size_t i = 0; i < n; ++i)
		remaining[i] = uint32_t(i);

	float signedArea = 0.f;
	for (size_t i = 0; i < n; ++i) {
		const auto &p0 = poly[remaining[i]];
		const auto &p1 = poly[remaining[(i + 1) % n]];
		signedArea += p0.x * p1.y - p1.x * p0.y;
	}
	if (signedArea < 0.f)
		std::reverse(remaining.begin(), remaining.end());

	size_t guard = 0;
	while (remaining.size() > 2 && guard < n * n + 8) {
		++guard;
		bool earFound = false;
		for (size_t i = 0; i < remaining.size(); ++i) {
			size_t iPrev = (i + remaining.size() - 1) % remaining.size();
			size_t iNext = (i + 1) % remaining.size();
			const auto &a = poly[remaining[iPrev]];
			const auto &b = poly[remaining[i]];
			const auto &c = poly[remaining[iNext]];

			float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
			// Tolérance légèrement négative (pas une stricte inégalité à
			// 0) : les sommets dupliqués introduits par MergeHole() au
			// point de jonction du pont produisent des triplets
			// quasi-colinéaires (cross ≈ 0) qui doivent rester des
			// oreilles valides (triangle dégénéré, sans conséquence),
			// sous peine de bloquer l'algorithme pile à la jonction.
			if (cross < -1e-6f)
				continue; // sommet réflexe : ne peut pas être une oreille

			bool anyInside = false;
			for (size_t k = 0; k < remaining.size(); ++k) {
				if (k == iPrev || k == i || k == iNext)
					continue;
				const auto &p = poly[remaining[k]];
				// Un point quasi confondu avec un sommet du triangle
				// candidat (cas des sommets dupliqués au pont d'un trou
				// fusionné, voir MergeHole()) n'est pas un point "à
				// l'intérieur" qui invaliderait l'oreille — sans cette
				// exclusion, la jonction bloque l'algorithme en
				// permanence (chaque triangle testé près du pont "voit"
				// sa propre copie dupliquée comme point intérieur).
				constexpr float EPS_SQ = 1e-10f;
				auto nearVertex = [&](const math::FVector2 &v) {
					float dx = p.x - v.x, dy = p.y - v.y;
					return dx * dx + dy * dy < EPS_SQ;
				};
				if (nearVertex(a) || nearVertex(b) || nearVertex(c))
					continue;
				if (PointInTriangle(p, a, b, c)) {
					anyInside = true;
					break;
				}
			}
			if (anyInside)
				continue;

			indices.push_back(remaining[iPrev]);
			indices.push_back(remaining[i]);
			indices.push_back(remaining[iNext]);
			remaining.erase(remaining.begin() + long(i));
			earFound = true;
			break;
		}
		if (!earFound)
			break; // contour dégénéré/auto-intersectant : abandon propre plutôt qu'une boucle infinie
	}
	return indices;
}

} // namespace render3d
