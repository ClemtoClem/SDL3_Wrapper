#pragma once
/**
 * @file shapes.hpp
 * @brief physics:: collision shapes — Sphere, Box (OBB), Capsule.
 *
 * Each shape carries its OWN world-space center/orientation (sphere has no
 * orientation — it's rotationally invariant), rather than taking a transform as
 * a parameter everywhere. This keeps every narrow-phase function signature small
 * (e.g. `CollideSphereSphere(const Sphere&, const Sphere&, Manifold&)`) and lets
 * shapes be unit-tested directly without an ecs::ArchetypeRegistry or a
 * RigidBody. A RigidBody (see rigidbody.hpp) still owns the authoritative
 * position/orientation; `SyncShapeTransform()` below copies it into the shape
 * once per step (see broadphase.hpp) before broad/narrow-phase run.
 *
 * `Shape` is a `std::variant<Sphere, Box, Capsule>` — a small tagged union, not
 * an OOP hierarchy — matching this repo's established preference (see
 * render3d::/ui:: prior work) for deliberate duplication + a narrow switch/visit
 * over deep class hierarchies when there are only a handful of cases.
 */
#include "../math/math.hpp"
#include <type_traits>
#include <variant>

namespace physics {

// ── Sphere ──────────────────────────────────────────────────────────────────

struct Sphere {
	math::FVector3 center;
	float radius = 0.5f;

	[[nodiscard]] math::FAABB WorldAABB() const noexcept;
};

// ── Box (oriented bounding box) ────────────────────────────────────────────

struct Box {
	math::FVector3 center;
	math::FVector3 halfExtents{0.5f, 0.5f, 0.5f};
	math::FQuaternion orientation = math::FQuaternion::Identity();

	/// World-space local X/Y/Z axes (unit length).
	[[nodiscard]] constexpr math::FVector3 AxisX() const noexcept { return orientation.Rotate({1.f, 0.f, 0.f}); }
	[[nodiscard]] constexpr math::FVector3 AxisY() const noexcept { return orientation.Rotate({0.f, 1.f, 0.f}); }
	[[nodiscard]] constexpr math::FVector3 AxisZ() const noexcept { return orientation.Rotate({0.f, 0.f, 1.f}); }

	[[nodiscard]] math::FAABB WorldAABB() const noexcept;
};

// ── Capsule ─────────────────────────────────────────────────────────────────

/**
 * A cylinder of `radius` capped by two hemispheres, centred on `center`, whose
 * long axis is the body-local +Y direction rotated by `orientation`.
 * `halfHeight` is half the length of the CYLINDRICAL segment only (excludes the
 * hemispherical caps) — the two segment endpoints (hemisphere centres) are at
 * `center ± Axis() * halfHeight`.
 */
struct Capsule {
	math::FVector3 center;
	float halfHeight = 0.5f;
	float radius = 0.5f;
	math::FQuaternion orientation = math::FQuaternion::Identity();

	[[nodiscard]] constexpr math::FVector3 Axis() const noexcept { return orientation.Rotate({0.f, 1.f, 0.f}); }
	[[nodiscard]] constexpr math::FVector3 PointA() const noexcept { return center - Axis() * halfHeight; }
	[[nodiscard]] constexpr math::FVector3 PointB() const noexcept { return center + Axis() * halfHeight; }

	[[nodiscard]] math::FAABB WorldAABB() const noexcept;
};

// ── Shape (tagged union) ───────────────────────────────────────────────────

using Shape = std::variant<Sphere, Box, Capsule>;

[[nodiscard]] math::FAABB ShapeWorldAABB(const Shape &shape) noexcept;

/**
 * Copies `position`/`orientation` (a RigidBody's authoritative transform) into
 * the shape's own `center`/`orientation` fields (Sphere has no orientation to
 * sync — it doesn't need one). Call once per step, before broad/narrow-phase,
 * so the shape reflects the latest integrated pose — see broadphase.hpp.
 */
void SyncShapeTransform(Shape &shape, const math::FVector3 &position,
								const math::FQuaternion &orientation) noexcept;

} // namespace physics
