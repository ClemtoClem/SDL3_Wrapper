---
name: project-ecs-render3d-bridge
description: "Engine expansion Phase 7 (M28): one-way ECS -> render3d::Object3D sync bridge (SceneTransform/SceneNode/SceneParent/SceneChildren + SceneSyncSystem::Sync). ECS-authoritative: entities are the stable identity, Object3D nodes are driven from them each frame, never the reverse. Part of /home/clement/.claude/plans/optimized-humming-toast.md."
metadata:
  type: project
---

Phase 7 of the engine-expansion plan (see [[project-render3d-animation]] for Phase 6) bridges `ecs::` to `render3d::Object3D`, per the locked-in scoping decision: ECS entities are the source of truth (stable identity for a future editor's selection/undo model), `Object3D` nodes are synced FROM ECS components each frame — one-way, never the reverse. `ecs::` itself needed zero changes (confirmed generic/adequate); this is new integration code only.

## Files

- `lib/include/render3d/ecs_bridge.hpp` (new) — `SceneTransform` (position/rotation/scale, ECS-authoritative); `SceneNode` (binds an entity to the non-owning `Object3D*` it drives — this module never constructs/destroys `Object3D` nodes, only transforms/re-parents ones already owned elsewhere in the app's scene graph); `SceneParent`/`SceneChildren` + `SetSceneParent()` — a deliberate mirror of the exact same problem this repo already solved for `ui::`'s own ECS tree (`UiParent`/`UiChildren`/`SetParent()`, `ui/components.hpp:230-238,1022-1038`); `SceneSyncSystem::Sync(registry, sceneRoot)` — two independent passes (transform copy, then re-parent), safe under the ECS's documented iterator-invalidation rule (mutating the `Object3D` a `SceneNode` points to is not an ECS structural change; `GetComponent<T>(otherEntity)` mid-`Query` is a plain archetype-row read, same pattern already used in `physics::World::Step`). Any tracked node with no resolved `SceneParent` attaches directly under `sceneRoot` — guarantees no tracked node ever ends up orphaned.
- `lib/include/render3d/object3d.hpp` — new `Object3D::RemoveChild(Object3D*) -> std::unique_ptr<Object3D>`, the missing symmetric counterpart to `Add()`. A real gap found during design (not by the implementing agent — planned for up front): `Add()` is a one-way ownership transfer with nothing to detach a child without destroying it, but the plan's "reparenting to match ECS hierarchy changes" requirement needs exactly that (move an already-owned node to a new parent, no allocation/destruction). Finds by pointer identity, moves the owning `unique_ptr` out, erases the slot, clears the removed node's `m_parent`. French doc comment, matching this file's existing convention.
- `tests/ecs_render3d_bridge_smoke_test.cpp` (new) — CPU-only, 3 tests: transform sync verified via a known point through `WorldMatrix()` (not raw field peeking), re-checked live after mutating the ECS component to prove it's driven every call, not just at spawn; a null `SceneNode::node` is skipped without crashing; a real reparent test that checks ownership was genuinely *moved* (old parent's `Children()` shrinks, new parent's grows, no duplication) and that the child's `WorldMatrix()` correctly composes through the new parent's transform after re-`Sync()`.

## Verification

3/3 new tests pass. Zero clang-tidy warnings anchored in `ecs_bridge.hpp` or the new lines in `object3d.hpp`. `physics_smoke_test` (5/5) and `animation_smoke_test` (3/3) both still green — no cross-phase regression from the `object3d.hpp` addition.

## Status

Phase 7 (M28) DONE as of 2026-08-24. Next per the plan: Phase 8 (`sql::` module, M29) — `data::`-based project persistence + a small hand-rolled metadata table store, no new external dependency.
