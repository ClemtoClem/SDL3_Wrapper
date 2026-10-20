#include "test.hpp"
#include <math/polygon.hpp>

#include <cmath>

using math::FVector2;

namespace {

/// Somme des aires des triangles (doit égaler l'aire du polygone troué).
float TriangulatedArea(const std::vector<FVector2> &pts, const std::vector<uint32_t> &idx) {
	float total = 0.f;
	for (size_t i = 0; i + 2 < idx.size(); i += 3) {
		const float a = math::SignedArea({pts[idx[i]], pts[idx[i + 1]], pts[idx[i + 2]]});
		CHECK(a >= -1e-6f); // sens trigonométrique
		total += a;
	}
	return total;
}

std::vector<FVector2> Concat(const std::vector<FVector2> &outer, const std::vector<std::vector<FVector2>> &holes) {
	std::vector<FVector2> all = outer;
	for (const auto &h : holes)
		all.insert(all.end(), h.begin(), h.end());
	return all;
}

} // namespace

TEST(Polygon_SignedAreaAndContainment) {
	const std::vector<FVector2> square{{0, 0}, {2, 0}, {2, 2}, {0, 2}};
	CHECK_NEAR(math::SignedArea(square), 4.f, 1e-6f);
	CHECK_NEAR(math::SignedArea({{0, 0}, {0, 2}, {2, 2}, {2, 0}}), -4.f, 1e-6f);
	CHECK(math::PointInPolygon(square, {1, 1}));
	CHECK(!math::PointInPolygon(square, {3, 1}));
	CHECK(math::OnPolygonBorder(square, {2, 1}, 1e-4f));
}

TEST(Polygon_ConcaveOutlineTriangulates) {
	// Un « L » (concave) donné dans le sens horaire.
	const std::vector<FVector2> l{{0, 0}, {0, 3}, {1, 3}, {1, 1}, {3, 1}, {3, 0}};
	std::vector<uint32_t> idx;
	REQUIRE(math::TriangulatePolygon(l, {}, idx));
	CHECK_EQ(idx.size(), size_t(3 * 4));
	CHECK_NEAR(TriangulatedArea(l, idx), 5.f, 1e-4f);
}

TEST(Polygon_HolesIncludingOneTouchingTheContour) {
	const std::vector<FVector2> outer{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
	const std::vector<std::vector<FVector2>> holes{
		{{1, 1}, {3, 1}, {3, 3}, {1, 3}},             // trou ordinaire
		{{8.5f, 4}, {9.99f, 4}, {9.99f, 6}, {8.5f, 6}}, // presque collé au bord droit
		{{4, 7}, {6, 7}, {5, 9}}};                    // triangle
	std::vector<uint32_t> idx;
	REQUIRE(math::TriangulatePolygon(outer, holes, idx));
	const float expected = 100.f - 4.f - 1.49f * 2.f - 2.f;
	CHECK_NEAR(TriangulatedArea(Concat(outer, holes), idx), expected, 1e-2f);
}

TEST(Polygon_DegenerateInputIsRejected) {
	std::vector<uint32_t> idx;
	CHECK(!math::TriangulatePolygon({{0, 0}, {1, 1}}, {}, idx));
	CHECK(idx.empty());
}
