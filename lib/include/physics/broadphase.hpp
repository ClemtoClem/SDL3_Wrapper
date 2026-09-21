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
[[nodiscard]] inline std::vector<BroadPhasePair> BroadPhase(ecs::ArchetypeRegistry &registry,
														   jobs::JobSystem *jobSystem = nullptr) {
	std::vector<ecs::Entity> entities;
	std::vector<math::FAABB> aabbs;
	std::vector<bool> isStatic;

	registry.Query<RigidBody>([&](ecs::Entity e, RigidBody &rb) {
		SyncShapeTransform(rb.shape, rb.position, rb.orientation);
		rb.worldAABB = ShapeWorldAABB(rb.shape);
		entities.push_back(e);
		aabbs.push_back(rb.worldAABB);
		isStatic.push_back(rb.invMass <= 0.f);
	});

	const size_t count = entities.size();
	if (count < 2)
		return {};

	// Chaque LIGNE `i` du triangle supérieur est indépendante des autres :
	// elle ne lit que `aabbs`/`isStatic` et n'écrit que dans SA propre case de
	// `rows`. C'est ce qui autorise la répartition sur le vivier.
	//
	// La concaténation finale se fait dans l'ordre croissant des lignes, donc
	// la liste de paires produite est RIGOUREUSEMENT LA MÊME qu'en série. Ça
	// n'est pas cosmétique : le solveur à impulsions séquentielles traite les
	// contacts dans l'ordre, un ordre différent donnerait un résultat
	// numériquement différent, et la démo promet des exécutions rejouables.
	std::vector<std::vector<BroadPhasePair>> rows(count);
	auto buildRow = [&](size_t i) {
		std::vector<BroadPhasePair> &row = rows[i];
		for (size_t j = i + 1; j < count; ++j) {
			if (isStatic[i] && isStatic[j])
				continue; // two immovable bodies never need solving
			if (!aabbs[i].Intersects(aabbs[j]))
				continue;

			ecs::Entity ea = entities[i], eb = entities[j];
			if (eb.id < ea.id)
				std::swap(ea, eb);
			row.push_back({ea, eb});
		}
	};

	// Seuil : en dessous, la synchronisation d'un lot coûte plus que le
	// travail lui-même (quelques dizaines de corps = quelques microsecondes).
	constexpr size_t MINIMUM_ROWS_PER_BATCH = 32;
	if (jobSystem)
		jobSystem->ParallelFor(count, MINIMUM_ROWS_PER_BATCH, buildRow);
	else
		for (size_t i = 0; i < count; ++i)
			buildRow(i);

	size_t total = 0;
	for (const auto &row : rows)
		total += row.size();
	std::vector<BroadPhasePair> pairs;
	pairs.reserve(total);
	for (const auto &row : rows)
		pairs.insert(pairs.end(), row.begin(), row.end());
	return pairs;
}

} // namespace physics
