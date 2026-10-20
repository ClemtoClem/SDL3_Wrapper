#pragma once
#include <algorithm>
#include <vector>

#include <core/core.hpp>
#include <math/math.hpp>
#include <math/polygon.hpp>
#include <sdl3/sdl3.hpp>

namespace render3d {

/// Contour 2D avec trous optionnels (three.js Shape) — triangulé par
/// math::TriangulatePolygon pour Mesh::Extrude() et les formes 2D. Convention : le contour extérieur doit être fourni
/// dans un sens quelconque, Triangulate() le normalise en CCW (aire signée
/// positive, vu depuis +Z) — la même convention "CCW vu depuis l'extérieur"
/// que Mesh::Cube()/Sphere()/etc.
class Shape2D {
	std::vector<math::FVector2> m_contour;
	std::vector<std::vector<math::FVector2>> m_holes;

public:
	explicit Shape2D(std::vector<math::FVector2> contour) noexcept : m_contour(std::move(contour)) {}

	void AddHole(std::vector<math::FVector2> hole) { m_holes.push_back(std::move(hole)); }

	[[nodiscard]] const std::vector<math::FVector2> &Contour() const noexcept { return m_contour; }
	[[nodiscard]] const std::vector<std::vector<math::FVector2>> &Holes() const noexcept { return m_holes; }

	/// Triangule le contour et ses trous (math::TriangulatePolygon : ponts
	/// visibles d'Eberly, robuste aux trous proches du contour). Retourne
	/// les points [contour, trous…] et les indices de triangles (sens
	/// trigonométrique vu depuis +Z) dans ce tableau.
	[[nodiscard]] std::pair<std::vector<math::FVector2>, std::vector<uint32_t>> Triangulate() const;
};

} // namespace render3d
