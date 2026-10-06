// Définitions de physics/joints.hpp
#include "physics/joints.hpp"

namespace physics {

// ── FixedJoint ───────────────────────────────────────────────────────────────

FixedJoint FixedJoint::Create(ecs::Entity a, ecs::Entity b, const math::FVector3 &anchorLocalA,
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

namespace joint_detail {

math::FVector3 PointVelocity(const RigidBody &rb, const math::FVector3 &r) noexcept {
	return rb.linearVelocity + rb.angularVelocity.Cross(r);
}

float LinearEffectiveMass(const RigidBody &a, const RigidBody &b, const math::FVector3 &rA,
		const math::FVector3 &rB, const math::FVector3 &dir) noexcept {
	math::FVector3 rAxD = rA.Cross(dir);
	math::FVector3 rBxD = rB.Cross(dir);
	math::FVector3 angA = a.WorldInverseInertia().TransformDir(rAxD).Cross(rA);
	math::FVector3 angB = b.WorldInverseInertia().TransformDir(rBxD).Cross(rB);
	float k = a.invMass + b.invMass + dir.Dot(angA) + dir.Dot(angB);
	return k > 1e-8f ? 1.f / k : 0.f;
}

float AngularEffectiveMass(const RigidBody &a, const RigidBody &b, const math::FVector3 &dir) noexcept {
	float k = dir.Dot(a.WorldInverseInertia().TransformDir(dir)) + dir.Dot(b.WorldInverseInertia().TransformDir(dir));
	return k > 1e-8f ? 1.f / k : 0.f;
}

void ApplyLinearImpulse(RigidBody &rb, const math::FVector3 &impulse, const math::FVector3 &r) noexcept {
	if (rb.invMass <= 0.f)
		return;
	rb.linearVelocity += impulse * rb.invMass;
	rb.angularVelocity += rb.WorldInverseInertia().TransformDir(r.Cross(impulse));
}

void ApplyAngularImpulse(RigidBody &rb, const math::FVector3 &impulse) noexcept {
	if (rb.invMass <= 0.f)
		return;
	rb.angularVelocity += rb.WorldInverseInertia().TransformDir(impulse);
}

} // namespace joint_detail

void SolveHingeJoint(HingeJoint &joint, RigidBody &a, RigidBody &b, float dt) {
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

void SolveFixedJoint(FixedJoint &joint, RigidBody &a, RigidBody &b, float dt) {
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

void ApplySpringForce(const SpringJoint &joint, RigidBody &a, RigidBody &b, float dt) {
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
