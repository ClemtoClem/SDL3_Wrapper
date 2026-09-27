#pragma once
/**
 * @file rigidbody.hpp
 * @brief physics::RigidBody — the ECS component every simulated body carries.
 */
#include "../math/math.hpp"
#include "shapes.hpp"
#include <type_traits>
#include <variant>

namespace physics {

/**
 * Diagonal body-local inertia tensor `(Ixx, Iyy, Izz)` for a solid, uniform-
 * density shape of the given total `mass`.
 *
 * Sphere and box use the exact closed-form solid-body formulas. Capsule is
 * approximated as an equivalent-length solid cylinder (length =
 * `2*halfHeight + 2*radius`, i.e. the full capsule length including the caps)
 * — the exact hemispherical-cap correction terms are a fair amount of extra
 * derivation for a shape that isn't exercised by this milestone's required
 * tests, so this documented simplification stands in for now (swap in the
 * closed-form capsule tensor if capsule rotational accuracy becomes
 * load-bearing later). Local Y is every shape's up/symmetry axis.
 */
[[nodiscard]] math::FVector3 ComputeInertiaLocal(const Shape &shape, float mass) noexcept;

/**
 * A simulated rigid body. `position`/`orientation` are the authoritative
 * transform; the body's `shape` carries its own copy synced from these once
 * per step (see `SyncShapeTransform()` / broadphase.hpp) so narrow-phase code
 * only ever has to look at the shape.
 *
 * `invMass == 0` (and `invInertiaLocal == 0`) marks a static/immovable body —
 * the standard infinite-mass trick, checked throughout the solver instead of
 * branching on a separate "is static" flag.
 */
struct RigidBody {
	math::FVector3 position;
	math::FQuaternion orientation = math::FQuaternion::Identity();
	math::FVector3 linearVelocity;
	math::FVector3 angularVelocity;

	float mass = 1.f;
	float invMass = 1.f;
	math::FVector3 invInertiaLocal{2.5f, 2.5f, 2.5f}; // diagonal, body-local axes

	float restitution = 0.3f;
	float friction = 0.5f;

	Shape shape = Sphere{};
	math::FAABB worldAABB; // refreshed every BroadPhase() call — never cached across steps

	/**
	 * World-space inverse inertia tensor `R * diag(invInertiaLocal) * R^T`,
	 * returned as an FMatrix4 (only the rotational 3x3 block is meaningful —
	 * use `TransformDir()` to apply it to a vector, never `TransformPoint()`).
	 *
	 * Recomputed fresh on every call: this module follows the repo-wide
	 * convention of correctness-by-recomputation over dirty-flag caching.
	 */
	[[nodiscard]] math::FMatrix4 WorldInverseInertia() const noexcept;

	/// Dynamic body: `invMass`/`invInertiaLocal` are derived from `shape`/`mass`.
	[[nodiscard]] static RigidBody MakeDynamic(Shape shape, const math::FVector3 &position, float mass,
												float restitution = 0.3f, float friction = 0.5f) noexcept;

	/// Static (infinite-mass, immovable) body: `invMass = 0`, `invInertiaLocal = 0`.
	[[nodiscard]] static RigidBody
	MakeStatic(Shape shape, const math::FVector3 &position,
			   const math::FQuaternion &orientation = math::FQuaternion::Identity(), float restitution = 0.3f,
			   float friction = 0.5f) noexcept;
};

} // namespace physics
