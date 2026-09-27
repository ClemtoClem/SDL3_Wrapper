#pragma once
/**
 * @file joints.hpp
 * @brief physics:: joints — hinge (revolute), fixed, and spring constraints.
 *
 * Hinge and fixed are solved with the same sequential-impulse philosophy as
 * the contact solver (see solver.hpp): a Gauss-Seidel sweep per call, meant to
 * be invoked several times per step. Both simplify the point (ball-socket)
 * part of the constraint to 3 independent scalar constraints along world X/Y/Z
 * rather than a full 3x3 block solve — this converges to the same answer over
 * enough iterations and keeps the code close to the contact solver's shape.
 *
 * Spring is not an impulse constraint at all — it's a plain Hookean force
 * (+ damping) applied every step, alongside gravity, before the velocity
 * solve runs (see world.hpp), matching "no hard DOF removal" from the spec.
 */
#include "../ecs/ecs.hpp"
#include "../math/math.hpp"
#include "rigidbody.hpp"
#include <array>

namespace physics {

// ── Hinge (revolute) joint ──────────────────────────────────────────────────

/// Removes all 3 positional DOF (anchor points coincide) and 2 of the 3
/// rotational DOF — only rotation about the shared axis stays free.
struct HingeJoint {
	ecs::Entity a, b;
	math::FVector3 anchorLocalA;               // anchor point, in A's local frame
	math::FVector3 anchorLocalB;               // anchor point, in B's local frame
	math::FVector3 axisLocalA{0.f, 1.f, 0.f};  // hinge axis, in A's local frame
	math::FVector3 axisLocalB{0.f, 1.f, 0.f};  // hinge axis, in B's local frame (kept aligned to A's)
	float biasFactor = 0.2f;                   // Baumgarte-style correction factor
};

// ── Fixed joint ──────────────────────────────────────────────────────────────

/// Locks all 6 relative DOF: anchor points coincide AND relative orientation
/// is held at whatever it was when the joint was created.
struct FixedJoint {
	ecs::Entity a, b;
	math::FVector3 anchorLocalA;
	math::FVector3 anchorLocalB;
	math::FQuaternion relativeOrientation = math::FQuaternion::Identity(); // conjugate(qA0) * qB0, at creation
	float biasFactor = 0.2f;

	[[nodiscard]] static FixedJoint Create(ecs::Entity a, ecs::Entity b, const math::FVector3 &anchorLocalA,
											const math::FVector3 &anchorLocalB, const math::FQuaternion &orientA0,
											const math::FQuaternion &orientB0) noexcept;
};

// ── Spring joint ────────────────────────────────────────────────────────────

/// A soft positional constraint (Hookean force) between two anchor points —
/// no hard DOF removal, just a restoring force + damping applied every step.
struct SpringJoint {
	ecs::Entity a, b;
	math::FVector3 anchorLocalA;
	math::FVector3 anchorLocalB;
	float restLength = 1.f;
	float stiffness = 50.f;
	float damping = 0.5f;
};

namespace joint_detail {

[[nodiscard]] math::FVector3 PointVelocity(const RigidBody &rb, const math::FVector3 &r) noexcept;

[[nodiscard]] float LinearEffectiveMass(const RigidBody &a, const RigidBody &b, const math::FVector3 &rA,
												const math::FVector3 &rB, const math::FVector3 &dir) noexcept;

[[nodiscard]] float AngularEffectiveMass(const RigidBody &a, const RigidBody &b,
												 const math::FVector3 &dir) noexcept;

void ApplyLinearImpulse(RigidBody &rb, const math::FVector3 &impulse, const math::FVector3 &r) noexcept;

void ApplyAngularImpulse(RigidBody &rb, const math::FVector3 &impulse) noexcept;

inline constexpr std::array<math::FVector3, 3> WORLD_AXES{
	math::FVector3{1.f, 0.f, 0.f}, math::FVector3{0.f, 1.f, 0.f}, math::FVector3{0.f, 0.f, 1.f}};

} // namespace joint_detail

/// One Gauss-Seidel sweep: the point (ball-socket) part of the hinge, plus the
/// 2 angular DOF that keep the hinge axis aligned between the two bodies —
/// leaving only rotation about the shared axis free.
void SolveHingeJoint(HingeJoint &joint, RigidBody &a, RigidBody &b, float dt);

/// One Gauss-Seidel sweep: the point (ball-socket) part, plus all 3 angular
/// DOF driven back toward the orientation captured at `FixedJoint::Create()`.
void SolveFixedJoint(FixedJoint &joint, RigidBody &a, RigidBody &b, float dt);

/// Applies the spring's Hookean restoring force (+ damping) directly to both
/// bodies' velocities for this step — not an impulse constraint, just a force.
void ApplySpringForce(const SpringJoint &joint, RigidBody &a, RigidBody &b, float dt);

} // namespace physics
