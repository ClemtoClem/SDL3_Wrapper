#pragma once
#include "material.hpp"
#include "shape.hpp"

namespace render3d {

/// Nuage de points (three.js Points) — un Shape dont la topologie de dessin
/// est POINT_LIST au lieu de TRIANGLE_LIST (voir Mesh::FromPoints() pour
/// construire la géométrie, PrimitiveTopology dans material.hpp pour le
/// câblage pipeline). N'utilise pas la logique par-groupe de Shape::OnDraw()
/// (un nuage de points n'a pas de face) — un seul DrawMesh, premier matériau.
class Points : public Shape {
public:
	Points(Mesh geometry, Material material) : Shape(std::move(geometry), std::move(material)) {}

	void OnDraw(Canvas &canvas) override;
};

/// Segments de droite indépendants (three.js LineSegments, pas LineStrip —
/// voir Mesh::FromLineSegments()) — même principe que Points ci-dessus avec
/// LINE_LIST.
class LineSegments : public Shape {
public:
	LineSegments(Mesh geometry, Material material) : Shape(std::move(geometry), std::move(material)) {}

	void OnDraw(Canvas &canvas) override;
};

} // namespace render3d
