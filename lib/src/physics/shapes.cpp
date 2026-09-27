// Définitions de physics/shapes.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "physics/shapes.hpp"

namespace physics {

// ── Sphere ───────────────────────────────────────────────────────────────────

math::FAABB Sphere::WorldAABB() const noexcept {
	math::FVector3 r{radius, radius, radius};
	return {center - r, center + r};
}

// ── Box ──────────────────────────────────────────────────────────────────────

math::FAABB Box::WorldAABB() const noexcept {
	math::FAABB local{-halfExtents, halfExtents};
	return local.Transformed(math::ComposeTRS(center, orientation, math::FVector3{1.f, 1.f, 1.f}));
}

// ── Capsule ──────────────────────────────────────────────────────────────────

math::FAABB Capsule::WorldAABB() const noexcept {
	math::FVector3 r{radius, radius, radius};
	math::FAABB box{PointA() - r, PointA() + r};
	box.Expand(PointB() - r);
	box.Expand(PointB() + r);
	return box;
}

math::FAABB ShapeWorldAABB(const Shape &shape) noexcept {
	return std::visit([](const auto &s) { return s.WorldAABB(); }, shape);
}

void SyncShapeTransform(Shape &shape, const math::FVector3 &position, const math::FQuaternion &orientation) noexcept {
	std::visit(
		[&](auto &s) {
			s.center = position;
			using T = std::decay_t<decltype(s)>;
			if constexpr (!std::is_same_v<T, Sphere>)
				s.orientation = orientation;
		},
		shape);
}

} // namespace physics
