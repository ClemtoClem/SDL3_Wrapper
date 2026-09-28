---
name: project-render3d-animation
description: "Engine expansion Phase 6 (M27): render3d:: generic Object3D-level keyframe animation — KeyframeTrack<FVector3/FQuaternion>/AnimationClip/AnimationMixer, plus Object3D::FindByName(). Drives SkinnedMesh/Bone for free since Bone : public Object3D. Part of /home/clement/.claude/plans/optimized-humming-toast.md."
metadata:
  type: project
---

Phase 6 of the engine-expansion plan (see [[project-physics-module]] for Phase 5) adds keyframe animation to `render3d::` at the generic `Object3D` level rather than a skeletal-only system — the scoping decision locked in at plan time. Because `Bone : public Object3D` (`skinned_mesh.hpp`), this same mixer drives skeletal animation with zero separate code path.

## Files

- `lib/include/render3d/animation.hpp` (new) — `KeyframeTrack<T>` (a single template covering both `math::FVector3` and `math::FQuaternion` instantiations, `Sample(time)` dispatching to `.Lerp()`/`.Slerp()` via `if constexpr`, clamping at the track's endpoints); `AnimationClip` (named position/rotation/scale track lists targeting nodes by name, `Duration()` = max over all tracks' last keyframe time); `AnimationMixer` (holds the scene root non-owning, exactly how `Bone *m_rootBone` is held in `skinned_mesh.hpp` — `Object3D` is non-copyable/non-movable so nothing else is possible; supports multiple simultaneously-playing clips with independent time cursors via `Play()`/`Stop()` handles; `Update(dt)` advances each active clip and applies sampled values via `SetPosition`/`SetRotation`/`SetScale`).
- `lib/include/render3d/object3d.hpp` — new `Object3D::FindByName(StringView) -> Object3D*`. Deliberately NOT implemented via the existing `Traverse()`, which skips invisible subtrees (correct for its own draw-order use, wrong for a structural name lookup — an animated bone toggled invisible for unrelated reasons must still resolve). Own small recursive walk instead.
- `tests/animation_smoke_test.cpp` (new) — CPU-only, 3 tests: exact `Lerp` sampling at track endpoints/midpoint; `Slerp` sampling verified independently via plain trigonometry on a rotated probe vector (not by re-deriving from `FromAxisAngle`/`Slerp` again, which would be circular); full `AnimationClip`+`AnimationMixer` end-to-end wiring check (name resolution + dispatch) across several `Update()` calls summing to a known cumulative time.
- `tests/render3d_smoke_test.cpp` — retrofitted the M17 `SkinnedMeshBonePoseDeformsOnlyWeightedHalf` GPU pixel-readback test: the manual `childBone->SetRotation(...)` pose is now driven through a 2-keyframe `AnimationClip` + `AnimationMixer::Update(1.f)` targeting the bone by name, with identical before/after pixel assertions confirming the generic animation system produces the exact same pose as the old manual call.

## Design point: correctness-by-recomputation

`AnimationMixer::Update()` resolves each track's `targetName` to an `Object3D*` via `FindByName()` fresh every call — never cached. Matches this codebase's explicit, repeated policy (`Object3D::WorldMatrix()`, `physics::RigidBody::WorldInverseInertia()`, both undocumented-cache-free by design) and avoids a dangling-pointer hazard if the hierarchy changes between frames. A name that doesn't resolve is skipped silently (best-effort, matching `Canvas::EnsureShadowResources`'s style elsewhere in this codebase).

## Real (pre-existing, unrelated) bug found and fixed incidentally

`tests/render3d_smoke_test.cpp` had ~8 calls to `sdl3::GetError().Empty()` — `Empty()` never existed on `String`/`StringView` (the real method is `IsEmpty()`), so the file would not compile at all before this phase touched it. Not related to animation; fixed as a prerequisite to getting a real green build (this file apparently went stale after an earlier `String`/`StringView` API rename elsewhere in this session's history).

## Verification

`animation_smoke_test`: 3/3 pass. `render3d_smoke_test` (under `xvfb-run`, ASan/UBSan): 25/25 pass, including the retrofitted skeletal test — exit code 0, no leaks beyond known pre-existing X11/SDL3_ttf library noise (only appears when run without a display; clean under `xvfb-run`). `physics_smoke_test` (Phase 5) still 5/5 — no cross-phase regression. Scoped clang-tidy on `animation.hpp`/`object3d.hpp`: zero anchored warnings.

## Status

Phase 6 (M27) DONE as of 2026-08-24. Next per the plan: Phase 7 (ECS ↔ `render3d::Object3D` bridge, M28).
