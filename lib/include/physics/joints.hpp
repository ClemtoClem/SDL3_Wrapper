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
											const math::FQuaternion &orientB0) noexcept {
		FixedJoint j;
		j.a = a;
		j.b = b;
		j.anchorLocalA = anchorLocalA;
		j.anchorLocalB = anchorLocalB;
		j.relativeOrientation = orientA0.Conjugate() * orientB0;
		return j;
	}
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

[[nodiscard]] inline math::FVector3 PointVelocity(const RigidBody &rb, const math::FVector3 &r) noexcept {
	return rb.linearVelocity + rb.angularVelocity.Cross(r);
}

[[nodiscard]] inline float LinearEffectiveMass(const RigidBody &a, const RigidBody &b, const math::FVector3 &rA,
												const math::FVector3 &rB, const math::FVector3 &dir) noexcept {
	math::FVector3 rAxD = rA.Cross(dir);
	math::FVector3 rBxD = rB.Cross(dir);
	math::FVector3 angA = a.WorldInverseInertia().TransformDir(rAxD).Cross(rA);
	math::FVector3 angB = b.WorldInverseInertia().TransformDir(rBxD).Cross(rB);
	float k = a.invMass + b.invMass + dir.Dot(angA) + dir.Dot(angB);
	return k > 1e-8f ? 1.f / k : 0.f;
}

[[nodiscard]] inline float AngularEffectiveMass(const RigidBody &a, const RigidBody &b,
												 const math::FVector3 &dir) noexcept {
	float k = dir.Dot(a.WorldInverseInertia().TransformDir(dir)) + dir.Dot(b.WorldInverseInertia().TransformDir(dir));
	return k > 1e-8f ? 1.f / k : 0.f;
}

inline void ApplyLinearImpulse(RigidBody &rb, const math::FVector3 &impulse, const math::FVector3 &r) noexcept {
	if (rb.invMass <= 0.f)
		return;
	rb.linearVelocity += impulse * rb.invMass;
	rb.angularVelocity += rb.WorldInverseInertia().TransformDir(r.Cross(impulse));
}

inline void ApplyAngularImpulse(RigidBody &rb, const math::FVector3 &impulse) noexcept {
	if (rb.invMass <= 0.f)
		return;
	rb.angularVelocity += rb.WorldInverseInertia().TransformDir(impulse);
}

inline constexpr std::array<math::FVector3, 3> WORLD_AXES{
	math::FVector3{1.f, 0.f, 0.f}, math::FVector3{0.f, 1.f, 0.f}, math::FVector3{0.f, 0.f, 1.f}};

} // namespace joint_detail

/// One Gauss-Seidel sweep: the point (ball-socket) part of the hinge, plus the
/// 2 angular DOF that keep the hinge axis aligned between the two bodies —
/// leaving only rotation about the shared axis free.
inline void SolveHingeJoint(HingeJoint &joint, RigidBody &a, RigidBody &b, float dt) {
	if (dt <= 0.f)
		return;

	math::FVector3 rA = a.orientation.Rotate(joint.anchorLocalA);
	math::FVector3 rB = b.orientation.Rotate(joint.anchorLocalB);
	math::FVector3 posError = (b.position + rB) - (a.position + rA);

	for (const math::FVector3 &axis : joint_detail::WORLD_AXES) {
		float mass = joint_detail::LinearEffectiveMass(a, b, rA, rB, axis);
		if (mass <= 0.f)
			continue;
		math::FVector3 relVel = joint_detail::PointVelocity(b, rB) - joint_detail::PointVelocity(a, rA);
		float bias = (joint.biasFactor / dt) * posError.Dot(axis);
		float lambda = -mass * (relVel.Dot(axis) + bias);
		math::FVector3 impulse = axis * lambda;
		joint_detail::ApplyLinearImpulse(a, -impulse, rA);
		joint_detail::ApplyLinearImpulse(b, impulse, rB);
	}

	math::FVector3 axisA = a.orientation.Rotate(joint.axisLocalA).Normalize();
	math::FVector3 axisB = b.orientation.Rotate(joint.axisLocalB).Normalize();

	math::FVector3 perp1 =
		sdl3::Abs(axisA.x) > 0.9f ? math::FVector3{0.f, 1.f, 0.f} : math::FVector3{1.f, 0.f, 0.f};
	perp1 = (perp1 - axisA * axisA.Dot(perp1)).Normalize();
	math::FVector3 perp2 = axisA.Cross(perp1);

	// Small-angle misalignment error between the two axes (~0 when aligned).
	math::FVector3 axisError = axisA.Cross(axisB);

	for (const math::FVector3 &perp : {perp1, perp2}) {
		float mass = joint_detail::AngularEffectiveMass(a, b, perp);
		if (mass <= 0.f)
			continue;
		math::FVector3 relAngVel = b.angularVelocity - a.angularVelocity;
		float bias = (joint.biasFactor / dt) * axisError.Dot(perp);
		float lambda = -mass * (relAngVel.Dot(perp) + bias);
		math::FVector3 impulse = perp * lambda;
		joint_detail::ApplyAngularImpulse(a, -impulse);
		joint_detail::ApplyAngularImpulse(b, impulse);
	}
}

/// One Gauss-Seidel sweep: the point (ball-socket) part, plus all 3 angular
/// DOF driven back toward the orientation captured at `FixedJoint::Create()`.
inline void SolveFixedJoint(FixedJoint &joint, RigidBody &a, RigidBody &b, float dt) {
	if (dt <= 0.f)
		return;

	math::FVector3 rA = a.orientation.Rotate(joint.anchorLocalA);
	math::FVector3 rB = b.orientation.Rotate(joint.anchorLocalB);
	math::FVector3 posError = (b.position + rB) - (a.position + rA);

	for (const math::FVector3 &axis : joint_detail::WORLD_AXES) {
		float mass = joint_detail::LinearEffectiveMass(a, b, rA, rB, axis);
		if (mass <= 0.f)
			continue;
		math::FVector3 relVel = joint_detail::PointVelocity(b, rB) - joint_detail::PointVelocity(a, rA);
		float bias = (joint.biasFactor / dt) * posError.Dot(axis);
		float lambda = -mass * (relVel.Dot(axis) + bias);
		math::FVector3 impulse = axis * lambda;
		joint_detail::ApplyLinearImpulse(a, -impulse, rA);
		joint_detail::ApplyLinearImpulse(b, impulse, rB);
	}

	// Orientation lock: the vector part of the error quaternion (current
	// relative orientation vs. the one captured at creation) is a standard
	// small-angle approximation of the angular position error.
	math::FQuaternion currentRel = a.orientation.Conjugate() * b.orientation;
	math::FQuaternion errorQuat = currentRel * joint.relativeOrientation.Conjugate();
	math::FVector3 angError{2.f * errorQuat.x, 2.f * errorQuat.y, 2.f * errorQuat.z};
	if (errorQuat.w < 0.f)
		angError = -angError; // shortest-path correction

	for (const math::FVector3 &axis : joint_detail::WORLD_AXES) {
		float mass = joint_detail::AngularEffectiveMass(a, b, axis);
		if (mass <= 0.f)
			continue;
		math::FVector3 relAngVel = b.angularVelocity - a.angularVelocity;
		float bias = (joint.biasFactor / dt) * angError.Dot(axis);
		float lambda = -mass * (relAngVel.Dot(axis) + bias);
		math::FVector3 impulse = axis * lambda;
		joint_detail::ApplyAngularImpulse(a, -impulse);
		joint_detail::ApplyAngularImpulse(b, impulse);
	}
}

/// Applies the spring's Hookean restoring force (+ damping) directly to both
/// bodies' velocities for this step — not an impulse constraint, just a force.
inline void ApplySpringForce(const SpringJoint &joint, RigidBody &a, RigidBody &b, float dt) {
	math::FVector3 rA = a.orientation.Rotate(joint.anchorLocalA);
	math::FVector3 rB = b.orientation.Rotate(joint.anchorLocalB);
	math::FVector3 pA = a.position + rA;
	math::FVector3 pB = b.position + rB;

	math::FVector3 delta = pB - pA;
	float dist = delta.Length();
	if (dist < 1e-6f)
		return;
	math::FVector3 dir = delta / dist; // A -> B

	float stretch = dist - joint.restLength;
	float relSpeed = (joint_detail::PointVelocity(b, rB) - joint_detail::PointVelocity(a, rA)).Dot(dir);
	float forceMag = -joint.stiffness * stretch - joint.damping * relSpeed; // Hooke's law + damping
	math::FVector3 force = dir * forceMag;                                 // force on B; A gets -force

	if (b.invMass > 0.f) {
		b.linearVelocity += force * b.invMass * dt;
		b.angularVelocity += b.WorldInverseInertia().TransformDir(rB.Cross(force)) * dt;
	}
	if (a.invMass > 0.f) {
		a.linearVelocity += -force * a.invMass * dt;
		a.angularVelocity += a.WorldInverseInertia().TransformDir(rA.Cross(-force)) * dt;
	}
}

} // namespace physics
