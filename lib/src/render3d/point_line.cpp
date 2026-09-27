// Définitions de render3d/point_line.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/point_line.hpp"

namespace render3d {

// ── Points ───────────────────────────────────────────────────────────────────

void Points::OnDraw(Canvas &canvas) {
	const auto &materials = Materials();
	canvas.DrawMesh(Geometry(), WorldMatrix(), materials.empty() ? Material::Default() : materials[0],
					PrimitiveTopology::POINTS);
}

// ── LineSegments ─────────────────────────────────────────────────────────────

void LineSegments::OnDraw(Canvas &canvas) {
	const auto &materials = Materials();
	canvas.DrawMesh(Geometry(), WorldMatrix(), materials.empty() ? Material::Default() : materials[0],
					PrimitiveTopology::LINES);
}

} // namespace render3d
