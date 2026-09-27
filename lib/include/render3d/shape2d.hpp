#pragma once
#include <algorithm>
#include <vector>

#include "../math/math.hpp"

namespace render3d {

/// Contour 2D avec trous optionnels (three.js Shape) — triangulé par ear
/// clipping (three.js ShapeUtils) pour Mesh::Extrude() et une future
/// ShapeGeometry plate. Convention : le contour extérieur doit être fourni
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

	/// Triangule le contour (trous fusionnés par un pont vers le sommet le
	/// plus proche — suffisant pour des trous convexes n'entrant pas en
	/// contact avec le contour extérieur ni entre eux ; pas une
	/// implémentation "visibility graph" complète). Retourne les points
	/// aplatis (contour fusionné) et les indices de triangles dans ce tableau.
	[[nodiscard]] std::pair<std::vector<math::FVector2>, std::vector<uint32_t>> Triangulate() const;

private:
	static void MergeHole(std::vector<math::FVector2> &contour, const std::vector<math::FVector2> &hole);

	[[nodiscard]] static bool PointInTriangle(const math::FVector2 &p, const math::FVector2 &a,
											  const math::FVector2 &b, const math::FVector2 &c) noexcept;

	[[nodiscard]] static std::vector<uint32_t> EarClip(const std::vector<math::FVector2> &poly);
};

} // namespace render3d
