#pragma once
/**
 * @file solver.hpp
 * @brief physics:: sequential-impulse contact solver, with warm starting and
 * Baumgarte position correction.
 */
#include "../core/core.hpp"
#include "../ecs/ecs.hpp"
#include "narrowphase.hpp"
#include "rigidbody.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace physics {

struct SolverConfig {
	int iterations = 8;
	float baumgarte = 0.2f;                  // Baumgarte stabilisation factor
	float slop = 0.01f;                       // allowed penetration before position correction kicks in
	float restitutionVelocityThreshold = 1.f; // below this closing speed, restitution is skipped (kills jitter)
};

/**
 * Stable identity for one contact point across frames, used to persist its
 * accumulated impulse for warm-starting. `(entA, entB)` are expected pre-
 * canonicalised (lower entity id first — see broadphase.hpp's `BroadPhasePair`)
 * so the same physical contact always hashes to the same key regardless of
 * which order narrow-phase happened to receive the pair in; `pointIndex` is the
 * point's index within that pair's manifold (a plain `(entityA, entityB,
 * contactIndex)` key, not a full point-persistence/matching scheme).
 */
struct ContactKey {
	uint32_t idA = 0, genA = 0, idB = 0, genB = 0;
	int pointIndex = 0;

	[[nodiscard]] bool operator==(const ContactKey &o) const noexcept {
		return idA == o.idA && genA == o.genA && idB == o.idB && genB == o.genB && pointIndex == o.pointIndex;
	}
};

struct ContactKeyHash {
	size_t operator()(const ContactKey &k) const noexcept {
		size_t h = k.idA;
		h ^= static_cast<size_t>(k.genA) + 0x9e3779b9ull + (h << 6) + (h >> 2);
		h ^= static_cast<size_t>(k.idB) + 0x9e3779b9ull + (h << 6) + (h >> 2);
		h ^= static_cast<size_t>(k.genB) + 0x9e3779b9ull + (h << 6) + (h >> 2);
		h ^= static_cast<size_t>(k.pointIndex) + 0x9e3779b9ull + (h << 6) + (h >> 2);
		return h;
	}
};

struct CachedImpulse {
	float normal = 0.f;
	float tangent[2] = {0.f, 0.f};
};

using WarmStartCache = std::unordered_map<ContactKey, CachedImpulse, ContactKeyHash>;

struct ContactConstraint {
	ecs::Entity entA, entB;
	RefMut<RigidBody> a;
	RefMut<RigidBody> b;

	ContactConstraint(ecs::Entity entA, ecs::Entity entB, RefMut<RigidBody> a, RefMut<RigidBody> b) noexcept
		: entA(entA), entB(entB), a(a), b(b) {}

	math::FVector3 rA, rB; // contact point relative to each body's centre of mass
	math::FVector3 normal; // A -> B
	math::FVector3 tangent[2];
	float penetration = 0.f;

	float normalMass = 0.f;
	float tangentMass[2] = {0.f, 0.f};
	float bias = 0.f; // combined Baumgarte + restitution target for -vn

	float normalImpulse = 0.f; // accumulated across this step's solver iterations
	float tangentImpulse[2] = {0.f, 0.f};

	ContactKey key;
};

namespace solver_detail {

[[nodiscard]] inline math::FVector3 PointVelocity(const RigidBody &rb, const math::FVector3 &r) noexcept {
	return rb.linearVelocity + rb.angularVelocity.Cross(r);
}

inline void ApplyImpulse(RigidBody &rb, const math::FVector3 &impulse, const math::FVector3 &r) noexcept {
	if (rb.invMass <= 0.f)
		return;
	rb.linearVelocity += impulse * rb.invMass;
	rb.angularVelocity += rb.WorldInverseInertia().TransformDir(r.Cross(impulse));
}

/// Effective (inverse) mass for a scalar velocity constraint along `dir` at
/// contact offsets `rA`/`rB` — the standard `1 / (invMassA + invMassB +
/// n.(I_A^-1(rA x n) x rA) + n.(I_B^-1(rB x n) x rB))` formula, generalised to 3D.
[[nodiscard]] inline float EffectiveMass(const RigidBody &a, const RigidBody &b, const math::FVector3 &rA,
										  const math::FVector3 &rB, const math::FVector3 &dir) noexcept {
	math::FVector3 rAxD = rA.Cross(dir);
	math::FVector3 rBxD = rB.Cross(dir);
	math::FVector3 angA = a.WorldInverseInertia().TransformDir(rAxD).Cross(rA);
	math::FVector3 angB = b.WorldInverseInertia().TransformDir(rBxD).Cross(rB);
	float k = a.invMass + b.invMass + dir.Dot(angA) + dir.Dot(angB);
	return k > 1e-8f ? 1.f / k : 0.f;
}

} // namespace solver_detail

/**
 * Builds one `ContactConstraint` per manifold point: effective masses, a
 * tangent basis, and the combined Baumgarte/restitution velocity bias.
 *
 * This is also where warm starting actually happens: the previous frame's
 * cached impulse (if any, found via `ContactKey`) is applied to both bodies
 * immediately, right here, BEFORE the iterative solve — the solve loop below
 * then only ever computes the incremental delta on top of that seeded value,
 * which is what makes it a real warm start rather than a no-op.
 */
inline void BuildContactConstraints(ecs::ArchetypeRegistry &registry, const std::vector<Manifold> &manifolds,
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

/**
 * One Gauss-Seidel sweep over every contact constraint: a clamped
 * (>= 0) normal impulse using the pre-computed bias, then two friction
 * impulses along the tangent basis clamped to the Coulomb cone
 * `[-mu*Pn, +mu*Pn]` from the just-updated normal impulse (the standard
 * two-tangent box approximation of the friction cone). Call this
 * `SolverConfig::iterations` times per step.
 */
inline void SolveContactConstraints(std::vector<ContactConstraint> &constraints) {
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

/**
 * Snapshots the accumulated impulses from this step's constraints into a
 * fresh cache for next frame's warm start. Contacts that no longer exist are
 * simply not carried over — replacing the cache wholesale each step IS the
 * pruning, no separate staleness pass needed.
 */
[[nodiscard]] inline WarmStartCache BuildWarmStartCache(const std::vector<ContactConstraint> &constraints) {
	WarmStartCache cache;
	cache.reserve(constraints.size());
	for (const ContactConstraint &c : constraints)
		cache[c.key] = {c.normalImpulse, {c.tangentImpulse[0], c.tangentImpulse[1]}};
	return cache;
}

} // namespace physics
