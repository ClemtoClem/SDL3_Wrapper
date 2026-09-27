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

[[nodiscard]] math::FVector3 PointVelocity(const RigidBody &rb, const math::FVector3 &r) noexcept;

void ApplyImpulse(RigidBody &rb, const math::FVector3 &impulse, const math::FVector3 &r) noexcept;

/// Effective (inverse) mass for a scalar velocity constraint along `dir` at
/// contact offsets `rA`/`rB` — the standard `1 / (invMassA + invMassB +
/// n.(I_A^-1(rA x n) x rA) + n.(I_B^-1(rB x n) x rB))` formula, generalised to 3D.
[[nodiscard]] float EffectiveMass(const RigidBody &a, const RigidBody &b, const math::FVector3 &rA,
										  const math::FVector3 &rB, const math::FVector3 &dir) noexcept;

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
void BuildContactConstraints(ecs::ArchetypeRegistry &registry, const std::vector<Manifold> &manifolds,
									 const WarmStartCache &cache, std::vector<ContactConstraint> &out,
									 const SolverConfig &config, float dt);

/**
 * One Gauss-Seidel sweep over every contact constraint: a clamped
 * (>= 0) normal impulse using the pre-computed bias, then two friction
 * impulses along the tangent basis clamped to the Coulomb cone
 * `[-mu*Pn, +mu*Pn]` from the just-updated normal impulse (the standard
 * two-tangent box approximation of the friction cone). Call this
 * `SolverConfig::iterations` times per step.
 */
void SolveContactConstraints(std::vector<ContactConstraint> &constraints);

/**
 * Snapshots the accumulated impulses from this step's constraints into a
 * fresh cache for next frame's warm start. Contacts that no longer exist are
 * simply not carried over — replacing the cache wholesale each step IS the
 * pruning, no separate staleness pass needed.
 */
[[nodiscard]] WarmStartCache BuildWarmStartCache(const std::vector<ContactConstraint> &constraints);

} // namespace physics
