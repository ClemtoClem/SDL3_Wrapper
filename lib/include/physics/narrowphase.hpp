#pragma once
/**
 * @file narrowphase.hpp
 * @brief physics:: narrow-phase — contact-manifold generation per shape pair.
 *
 * Every `Collide*` function below takes shapes directly (each shape already
 * carries its own world-space center/orientation, see shapes.hpp) and produces
 * a `Manifold` whose `normal` points from the FIRST shape argument ("A") to the
 * SECOND ("B") by convention — documented once, here, and followed throughout.
 *
 * `GenerateManifold()` is the ECS-facing entry point: it double-dispatches over
 * the `Shape` variant (`std::visit` nested twice — the "small switch/visitor"
 * this repo prefers over a shape class hierarchy for a handful of cases) and
 * flips the normal when the canonical per-pair implementation had to be called
 * with its arguments swapped.
 */
#include "../ecs/ecs.hpp"
#include "shapes.hpp"
#include <type_traits>
#include <variant>

namespace physics {

inline constexpr int MAX_MANIFOLD_POINTS = 8; // box-box face-clip of a quad against 4 half-planes: at most 4+4

struct ContactPoint {
	math::FVector3 worldPoint;
	float penetration = 0.f;
};

struct Manifold {
	ecs::Entity a, b;
	math::FVector3 normal{0.f, 1.f, 0.f}; // unit, points from A to B (see file header)
	ContactPoint points[MAX_MANIFOLD_POINTS];
	int pointCount = 0;

	void AddPoint(const math::FVector3 &worldPoint, float penetration) noexcept;
};

// ── Shared geometric helpers (also reused by capsule collisions) ──────────

/// Closest point on the finite segment `[a, b]` to `p`.
[[nodiscard]] math::FVector3 ClosestPointOnSegment(const math::FVector3 &p, const math::FVector3 &a,
														   const math::FVector3 &b) noexcept;

/// Closest points between two finite segments `[p1,q1]` and `[p2,q2]` (Ericson,
/// "Real-Time Collision Detection", ClosestPtSegmentSegment) — also the
/// building block for capsule-capsule and box-box edge-edge contacts below.
void ClosestPointsSegmentSegment(const math::FVector3 &p1, const math::FVector3 &q1, const math::FVector3 &p2,
										 const math::FVector3 &q2, math::FVector3 &c1,
										 math::FVector3 &c2) noexcept;

// ── Sphere - Sphere ─────────────────────────────────────────────────────────

[[nodiscard]] bool CollideSphereSphere(const Sphere &a, const Sphere &b, Manifold &out) noexcept;

// ── Sphere - Box ────────────────────────────────────────────────────────────

[[nodiscard]] math::FVector3 ClosestPointOnBox(const math::FVector3 &point, const Box &box) noexcept;

/// Normal points sphere (A) -> box (B).
[[nodiscard]] bool CollideSphereBox(const Sphere &sph, const Box &box, Manifold &out) noexcept;

// ── Box - Box (SAT + reference/incident face clipping) ─────────────────────

/**
 * Full 15-axis Separating Axis Theorem test (3 face normals of A, 3 of B, 9
 * edge-cross-products), tracking the axis of minimum penetration with a bias
 * toward face axes — a well-known SAT box-box pitfall is a numerically-
 * fragile edge-edge axis winning by a hair over what's really a face contact,
 * so an edge axis only wins here when it is CLEARLY the smaller penetration.
 *
 * Face contacts are resolved via reference/incident face clipping (Sutherland-
 * Hodgman against the reference face's 4 side planes) to produce a real
 * multi-point manifold — this is what gives a resting box angular stability
 * across many solver steps instead of rocking on a single contact point.
 * Edge-edge contacts fall back to a single closest-point-between-edges point.
 */
[[nodiscard]] bool CollideBoxBox(const Box &boxA, const Box &boxB, Manifold &out) noexcept;

// ── Capsule - Sphere / Capsule - Capsule ───────────────────────────────────

/// Normal points capsule (A) -> sphere (B).
[[nodiscard]] bool CollideCapsuleSphere(const Capsule &cap, const Sphere &sph, Manifold &out) noexcept;

[[nodiscard]] bool CollideCapsuleCapsule(const Capsule &a, const Capsule &b, Manifold &out) noexcept;

/// Signed distance from `worldPoint` to `box` (negative inside) — the SDF of
/// an oriented box. Convex, which is what CollideCapsuleBox relies on.
[[nodiscard]] float BoxSignedDistance(const Box &box, const math::FVector3 &worldPoint) noexcept;

/**
 * Normal points capsule (A) -> box (B).
 *
 * Reduced to the sphere-box case: the capsule is the set of spheres of
 * radius `radius` centred on its segment, so the contact is carried by the
 * segment point where the box's SIGNED distance is smallest — the closest
 * point when separated, the deepest one when penetrating. That distance is a
 * convex function along the segment (the SDF of a convex set is convex), so a
 * ternary search finds its minimum with no special case for parallel faces,
 * edges or corners.
 *
 * A capsule lying on a face touches it along a whole line, and a single
 * contact point would let it rock around that point. The two segment ends are
 * therefore added as extra points when they touch with (nearly) the same
 * normal — the same idea as the face clipping of the box-box case.
 */
[[nodiscard]] bool CollideCapsuleBox(const Capsule &cap, const Box &box, Manifold &out) noexcept;

// ── Dispatch ────────────────────────────────────────────────────────────────

/// Generates a contact manifold for the pair `(entA, shapeA)` / `(entB, shapeB)`
/// if they overlap. `out.normal` always ends up pointing A -> B regardless of
/// which underlying `Collide*` function's native argument order was used.
[[nodiscard]] bool GenerateManifold(ecs::Entity entA, const Shape &shapeA, ecs::Entity entB,
											const Shape &shapeB, Manifold &out) noexcept;

} // namespace physics
