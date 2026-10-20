// Définitions de render3d/shape2d.hpp
#include "render3d/shape2d.hpp"

namespace render3d {

std::pair<std::vector<math::FVector2>, std::vector<uint32_t>> Shape2D::Triangulate() const {
	std::vector<math::FVector2> points = m_contour;
	for (const auto &hole : m_holes)
		points.insert(points.end(), hole.begin(), hole.end());
	std::vector<uint32_t> indices;
	(void)math::TriangulatePolygon(m_contour, m_holes, indices);
	return {std::move(points), std::move(indices)};
}

} // namespace render3d
