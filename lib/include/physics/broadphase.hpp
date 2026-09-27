#pragma once
/**
 * @file broadphase.hpp
 * @brief physics:: broad-phase — brute-force pairwise AABB overlap.
 */
#include "../ecs/ecs.hpp"
#include "../jobs/job_system.hpp"
#include "rigidbody.hpp"
#include <utility>
#include <vector>

namespace physics {

/// A candidate overlapping pair from broad-phase, canonicalised `a.id < b.id`
/// so the pair (and everything downstream that keys off entity order — the
/// manifold's A->B normal direction, the solver's warm-start cache key) stays
/// stable frame-to-frame regardless of ECS/query iteration order.
struct BroadPhasePair {
	ecs::Entity a, b;
};

/**
 * Refreshes every `RigidBody`'s collision shape (position/orientation synced
 * from the body) and cached world AABB, then finds candidate overlapping pairs
 * via brute-force O(n^2) pairwise `FAABB::Intersects` tests.
 *
 * Brute-force is genuinely fine at editor/game-object scale (dozens of
 * bodies) — no BVH/spatial-hash is built here, deliberately, per the module's
 * scope (see the physics module's task notes).
 *
 * Mutates each `RigidBody`'s `shape`/`worldAABB` fields in place through the
 * `Query<>()` reference — that's plain field mutation, not a structural ECS
 * change (no AddComponent/RemoveComponent/Despawn happens here), so it's safe
 * during iteration.
 */
[[nodiscard]] std::vector<BroadPhasePair> BroadPhase(ecs::ArchetypeRegistry &registry,
														   jobs::JobSystem *jobSystem = nullptr);

} // namespace physics
