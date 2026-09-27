// Définitions de physics/rigidbody.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "physics/rigidbody.hpp"

namespace physics {

math::FVector3 ComputeInertiaLocal(const Shape &shape, float mass) noexcept {
	return std::visit(
		[mass](const auto &s) -> math::FVector3 {
			using T = std::decay_t<decltype(s)>;
			if constexpr (std::is_same_v<T, Sphere>) {
				float i = 0.4f * mass * s.radius * s.radius; // solid sphere: I = 2/5 m r^2
				return {i, i, i};
			} else if constexpr (std::is_same_v<T, Box>) {
				const math::FVector3 &h = s.halfExtents; // full extents are 2h; I = m/12*(b^2+c^2) with b=2h
				return {
					(mass / 3.f) * (h.y * h.y + h.z * h.z),
					(mass / 3.f) * (h.x * h.x + h.z * h.z),
					(mass / 3.f) * (h.x * h.x + h.y * h.y),
				};
			} else {
				static_assert(std::is_same_v<T, Capsule>, "ComputeInertiaLocal: unhandled Shape alternative");
				float length = 2.f * s.halfHeight + 2.f * s.radius; // equivalent-cylinder approximation
				float axisI = 0.5f * mass * s.radius * s.radius;
				float transI = mass * (3.f * s.radius * s.radius + length * length) / 12.f;
				return {transI, axisI, transI};
			}
		},
		shape);
}

// ── RigidBody ────────────────────────────────────────────────────────────────

math::FMatrix4 RigidBody::WorldInverseInertia() const noexcept {
	math::FMatrix4 r = orientation.ToMat4();
	math::FMatrix4 invILocal = math::FMatrix4::Scale(invInertiaLocal);
	return r * invILocal * r.Transpose();
}

RigidBody RigidBody::MakeDynamic(Shape shape, const math::FVector3 &position, float mass,
		float restitution, float friction) noexcept {
	RigidBody rb;
	rb.shape = shape;
	rb.position = position;
	SyncShapeTransform(rb.shape, rb.position, rb.orientation);
	rb.mass = mass;
	rb.invMass = mass > 1e-8f ? 1.f / mass : 0.f;

	math::FVector3 inertia = ComputeInertiaLocal(rb.shape, mass);
	rb.invInertiaLocal = {
		inertia.x > 1e-8f ? 1.f / inertia.x : 0.f,
		inertia.y > 1e-8f ? 1.f / inertia.y : 0.f,
		inertia.z > 1e-8f ? 1.f / inertia.z : 0.f,
	};
	rb.restitution = restitution;
	rb.friction = friction;
	return rb;
}

RigidBody RigidBody::MakeStatic(Shape shape, const math::FVector3 &position,
		const math::FQuaternion &orientation, float restitution,
		float friction) noexcept {
	RigidBody rb;
	rb.shape = shape;
	rb.position = position;
	rb.orientation = orientation;
	SyncShapeTransform(rb.shape, rb.position, rb.orientation);
	rb.mass = 0.f;
	rb.invMass = 0.f;
	rb.invInertiaLocal = {0.f, 0.f, 0.f};
	rb.restitution = restitution;
	rb.friction = friction;
	return rb;
}

} // namespace physics
