// Définitions de physics/solver.hpp
#include "physics/solver.hpp"

namespace physics {

namespace solver_detail {

math::FVector3 PointVelocity(const RigidBody &rb, const math::FVector3 &r) noexcept {
	return rb.linearVelocity + rb.angularVelocity.Cross(r);
}

void ApplyImpulse(RigidBody &rb, const math::FVector3 &impulse, const math::FVector3 &r) noexcept {
	if (rb.invMass <= 0.f)
		return;
	rb.linearVelocity += impulse * rb.invMass;
	rb.angularVelocity += rb.WorldInverseInertia().TransformDir(r.Cross(impulse));
}

float EffectiveMass(const RigidBody &a, const RigidBody &b, const math::FVector3 &rA,
		const math::FVector3 &rB, const math::FVector3 &dir) noexcept {
	math::FVector3 rAxD = rA.Cross(dir);
	math::FVector3 rBxD = rB.Cross(dir);
	math::FVector3 angA = a.WorldInverseInertia().TransformDir(rAxD).Cross(rA);
	math::FVector3 angB = b.WorldInverseInertia().TransformDir(rBxD).Cross(rB);
	float k = a.invMass + b.invMass + dir.Dot(angA) + dir.Dot(angB);
	return k > 1e-8f ? 1.f / k : 0.f;
}

} // namespace solver_detail

void BuildContactConstraints(ecs::ArchetypeRegistry &registry, const std::vector<Manifold> &manifolds,
		const WarmStartCache &cache, std::vector<ContactConstraint> &out,
		const SolverConfig &config, float dt) {
	out.clear();
	out.reserve(manifolds.size() * 4);

	for (const Manifold &m : manifolds) {
		Option<RefMut<RigidBody>> oa = registry.GetComponent<RigidBody>(m.a);
		Option<RefMut<RigidBody>> ob = registry.GetComponent<RigidBody>(m.b);
		if (!oa.IsSome() || !ob.IsSome())
			continue;

		RigidBody &a = oa.Value();
		RigidBody &b = ob.Value();

		ecs::Entity lo = (m.a.id < m.b.id) ? m.a : m.b;
		ecs::Entity hi = (m.a.id < m.b.id) ? m.b : m.a;

		for (int i = 0; i < m.pointCount; ++i) {
			const ContactPoint &cp = m.points[i];

			ContactConstraint c(m.a, m.b, MakeRefMut(a), MakeRefMut(b));
			c.rA = cp.worldPoint - a.position;
			c.rB = cp.worldPoint - b.position;
			c.normal = m.normal;
			c.penetration = cp.penetration;

			math::FVector3 t1 =
				sdl3::Abs(c.normal.x) > 0.9f ? math::FVector3{0.f, 1.f, 0.f} : math::FVector3{1.f, 0.f, 0.f};
			t1 = (t1 - c.normal * c.normal.Dot(t1)).Normalize();
			math::FVector3 t2 = c.normal.Cross(t1);
			c.tangent[0] = t1;
			c.tangent[1] = t2;

			c.normalMass = solver_detail::EffectiveMass(a, b, c.rA, c.rB, c.normal);
			c.tangentMass[0] = solver_detail::EffectiveMass(a, b, c.rA, c.rB, t1);
			c.tangentMass[1] = solver_detail::EffectiveMass(a, b, c.rA, c.rB, t2);

			math::FVector3 relVel = solver_detail::PointVelocity(b, c.rB) - solver_detail::PointVelocity(a, c.rA);
			float vn = relVel.Dot(c.normal);
			float restitutionBias = (vn < -config.restitutionVelocityThreshold)
										 ? -sdl3::Min(a.restitution, b.restitution) * vn
										 : 0.f;
			float baumgarteBias = dt > 0.f ? (config.baumgarte / dt) * sdl3::Max(0.f, c.penetration - config.slop)
											: 0.f;
			c.bias = sdl3::Max(restitutionBias, baumgarteBias);

			c.key = {lo.id, lo.generation, hi.id, hi.generation, i};

			auto it = cache.find(c.key);
			if (it != cache.end()) {
				c.normalImpulse = it->second.normal;
				c.tangentImpulse[0] = it->second.tangent[0];
				c.tangentImpulse[1] = it->second.tangent[1];

				math::FVector3 warmImpulse =
					c.normal * c.normalImpulse + t1 * c.tangentImpulse[0] + t2 * c.tangentImpulse[1];
				solver_detail::ApplyImpulse(a, -warmImpulse, c.rA);
				solver_detail::ApplyImpulse(b, warmImpulse, c.rB);
			}

			out.push_back(c);
		}
	}
}

void SolveContactConstraints(std::vector<ContactConstraint> &constraints) {
	for (ContactConstraint &c : constraints) {
		RigidBody &a = *c.a;
		RigidBody &b = *c.b;

		{
			math::FVector3 relVel = solver_detail::PointVelocity(b, c.rB) - solver_detail::PointVelocity(a, c.rA);
			float vn = relVel.Dot(c.normal);
			float lambda = c.normalMass * (-vn + c.bias);
			float newImpulse = sdl3::Max(c.normalImpulse + lambda, 0.f);
			float delta = newImpulse - c.normalImpulse;
			c.normalImpulse = newImpulse;

			math::FVector3 impulse = c.normal * delta;
			solver_detail::ApplyImpulse(a, -impulse, c.rA);
			solver_detail::ApplyImpulse(b, impulse, c.rB);
		}

		float maxFriction = sdl3::Min(a.friction, b.friction) * c.normalImpulse;
		for (int i = 0; i < 2; ++i) {
			math::FVector3 relVel = solver_detail::PointVelocity(b, c.rB) - solver_detail::PointVelocity(a, c.rA);
			float vt = relVel.Dot(c.tangent[i]);
			float lambda = -c.tangentMass[i] * vt;
			float newImpulse = sdl3::Clamp(c.tangentImpulse[i] + lambda, -maxFriction, maxFriction);
			float delta = newImpulse - c.tangentImpulse[i];
			c.tangentImpulse[i] = newImpulse;

			math::FVector3 impulse = c.tangent[i] * delta;
			solver_detail::ApplyImpulse(a, -impulse, c.rA);
			solver_detail::ApplyImpulse(b, impulse, c.rB);
		}
	}
}

WarmStartCache BuildWarmStartCache(const std::vector<ContactConstraint> &constraints) {
	WarmStartCache cache;
	cache.reserve(constraints.size());
	for (const ContactConstraint &c : constraints)
		cache[c.key] = {c.normalImpulse, {c.tangentImpulse[0], c.tangentImpulse[1]}};
	return cache;
}

} // namespace physics
