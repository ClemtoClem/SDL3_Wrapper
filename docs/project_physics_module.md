---
name: project-physics-module
description: "Engine expansion Phase 5 (M24-M26): new physics:: module — Sphere/Box/Capsule shapes, brute-force AABB broad-phase, SAT box-box narrow-phase with manifold generation, a real sequential-impulse solver with warm starting, and hinge/fixed/spring joints. Part of /home/clement/.claude/plans/optimized-humming-toast.md."
metadata:
  type: project
---

Phase 5 of the engine-expansion plan (see [[project-ui-shader-effects]] for Phase 4) adds a new, self-contained `physics::` module under `lib/include/physics/` — the full constraint-solver system the user chose over the minimal option (see the plan's locked-in scoping decisions). Zero dependency on `ui::`/`render3d::`; only `math::`/`ecs::`.

## Files

- `shapes.hpp` — `Sphere`/`Box` (OBB)/`Capsule`, `Shape = std::variant<...>` (tagged union, matching the repo's established small-switch/visit preference over class hierarchies). Each shape carries its own world-space center/orientation; `SyncShapeTransform()` copies a `RigidBody`'s authoritative transform in once per step.
- `broadphase.hpp` — brute-force O(n²) pairwise AABB overlap, canonicalized `a.id < b.id` pair ordering (everything downstream — manifold normal direction, warm-start cache key — depends on this staying stable frame-to-frame). Static-static pairs skipped. Deliberately no BVH/spatial-hash — fine at editor/game-object scale, per plan scope.
- `narrowphase.hpp` — sphere-sphere, sphere-box, capsule-sphere/capsule (closed-form), and a full 15-axis SAT box-box test with face-bias (`FACE_BIAS = 0.95`, avoids a numerically-fragile edge axis winning over a real face contact by a hair) producing genuine multi-point manifolds via Sutherland-Hodgman reference/incident face clipping — this is what gives a resting box angular stability across many solver steps instead of rocking on one contact point. Edge-edge case falls back to single closest-point-between-segments. Capsule-box explicitly deferred (always reports no contact, documented, not silently wrong).
- `rigidbody.hpp` — `RigidBody` ECS component; closed-form solid-body inertia tensors for sphere (`2/5 m r²`) and box (`m/12*(b²+c²)` in terms of full side lengths); capsule approximated as an equivalent solid cylinder (documented simplification, not exercised by required tests). `invMass == 0` marks static bodies (infinite-mass trick, checked throughout instead of a separate flag).
- `solver.hpp` — sequential-impulse contact solver. `ContactKey{entityId+generation ×2, pointIndex}` on canonicalized lo/hi entities for a stable warm-start cache key. `BuildContactConstraints` computes effective masses (proper `1/(invMassA+invMassB+angular terms)`), a Gram-Schmidt tangent basis, `bias = max(restitutionBias, baumgarteBias)`, and — this is the real warm start, not a stub — applies the previous frame's cached impulse to both bodies immediately, before the iterative solve begins. `SolveContactConstraints` does clamped Gauss-Seidel (`newImpulse = max(normalImpulse+lambda, 0)`) then Coulomb-cone-clamped friction (`maxFriction = min(frictionA,frictionB) * normalImpulse`, using the just-updated normal impulse — standard technique).
- `joints.hpp` — `HingeJoint` (3 linear DOF removed + 2 of 3 angular, axis stays free), `FixedJoint` (all 6 DOF locked, quaternion-error small-angle correction against the relative orientation captured at creation), `SpringJoint` (plain Hookean force + damping, not an impulse constraint — applied every step alongside gravity). Same Gauss-Seidel philosophy as the contact solver.
- `world.hpp` — `physics::World`, holding a non-owning `ecs::ArchetypeRegistry&` (mirrors `ui::Ui`'s pattern) + the one piece of cross-step state, the warm-start cache. `Step(dt)`: gravity+springs → broad-phase → narrow-phase → build constraints (warm-start applied here) → N solver iterations → M joint iterations → snapshot new warm-start cache → semi-implicit Euler integration (including proper quaternion integration via `orientation += 0.5*dt*(angularVelocityQuat * orientation)`, normalized).

## Verification

`tests/physics_smoke_test.cpp`, 5 CPU-only tests, all independently-derived closed-form/invariant checks (not solver-internal comparisons): two spheres settle at combined-radii separation under gravity; a box rests stably on ground over 400 steps with no tipping/sinking; an equal-mass elastic sphere-sphere collision matches textbook momentum+energy conservation; a hinge joint holds its axis direction (invariant: spin about the hinge axis doesn't change the axis itself) under an off-axis angular-velocity perturbation; the warm-start cache is confirmed genuinely populated with non-trivial impulses, not just structurally present. All 5 pass.

Beyond running the tests, did a full manual code-correctness read of every file (not just trusting green tests) — confirmed `EffectiveMass`, warm-start application order, Gauss-Seidel clamping, the SAT face-bias + Sutherland-Hodgman clipping, the inertia tensor formulas, and the joint bias/error-quaternion math are all genuine, standard, correct derivations — not coincidentally-passing placeholders.

## Status

Phase 5 (M24-M26) DONE as of 2026-08-24. Next per the plan: Phase 6 (`render3d::` keyframe animation, M27).
