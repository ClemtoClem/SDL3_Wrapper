---
name: project-game-editor
description: "Engine expansion Phase 9, milestones M31+M32 (final milestones of the whole 9-phase plan): the game-editor capstone example (examples/game_editor_demo.cpp + examples/game_editor/editor.hpp) — outliner/inspector/toolbar UI + Play/Stop mode wiring together every prior phase (ui::, ECS scene bridge, portals, animation, physics, JSON project save/load). Part of /home/clement/.claude/plans/optimized-humming-toast.md."
metadata:
  type: project
---

M31 (editor UI) + M32 (play/test mode) — the final milestones of the entire engine-expansion plan (see [[project-portal-rendering]] for M30, the plan's other Phase 9 sub-milestone). Pure composition on top of everything built in Phases 1-9: no new `ui::`/`render3d::`/`physics::`/`sql::` public API, per the plan's own explicit framing — this milestone only wires existing, independently-verified pieces together.

## What was built

- `examples/game_editor_demo.cpp` — completed a pre-existing WIP scaffold (async GPU-warmup loading screen, mirroring `ui_viewport3d_demo.cpp`'s established pattern) rather than starting fresh. Fixed its two small bugs (unused `ui::UiFactory &f`, missing `return 0`) as part of finishing it. Has a `LEVEL_EDITOR_DEMO_AUTOQUIT_FRAMES` env-var hook for automated smoke-testing (runs N frames then exits cleanly) — the established verification technique for a UI example that can't be driven interactively in this environment.
- `examples/game_editor/editor.hpp` (887 lines, was empty) — the `game_editor::Editor` class and free functions: scene setup (obstacles, a linked portal pair reusing M30, one `AnimationClip`-driven object), outliner (`TreeNode` per `SceneNode`-bearing entity), inspector (`DragValue`s bound to the selected entity's `SceneTransform`), toolbar (New/Save/Load/placement-mode/Play-Stop), and the play-mode state machine.

## Two legitimately new, editor-domain ECS components

- `Selected` (marker) — single-selection only (explicit scope cut). `SelectEntity()` collects existing holders via `EntitiesWith<Selected>()` into a vector FIRST, then removes in a separate loop — avoiding this session's now-familiar iterator-invalidation hazard by never mutating during the query itself.
- `EditorEuler` (cached pitch/yaw/roll in degrees) — a deliberate design choice worth remembering: `math::FQuaternion` has `FromEuler()` but no `ToEuler()`, so decomposing an arbitrary `SceneTransform::rotation` back to Euler angles for the inspector's `DragValue`s would need a new, easy-to-get-subtly-wrong conversion. Instead, `EditorEuler` is the inspector's own single source of truth for what the DragValues display, converted one-way (Euler → quaternion) into `SceneTransform::rotation` on every edit — the decomposition problem is sidestepped entirely rather than solved riskily.

## Play/Stop mechanism

The whole of "Play" is a `std::vector<TransformSnapshot>` captured via `SnapshotSceneTransforms()` before switching camera/activating simulation; "Stop" calls `RestoreSceneTransforms()` from that snapshot — deliberately not a general undo/redo system (explicit scope cut). During Play: `physics::World::Step`, `physics::TeleportBodiesThroughPortals` (M30), and `AnimationMixer::Update` all run each frame, followed by `SceneSyncSystem::Sync` (Phase 7 bridge) as normal so everything reaches the real `Object3D` graph the `Viewport3D` widget renders — no separate rendering path for play vs. edit mode.

## Real bugs found and fixed while building this

1. **`data::JsonDocument`'s FLOAT encoder writes a whole-number double (e.g. `5.0`) as `5`** (a plain `ostringstream`, no forced decimal point) — indistinguishable from an INT node on reload. Fixed defensively on the READ side only (the JSON loader accepts both `FLOAT` and `INT` node types for a value that should be treated as float) rather than touching the shared `data::` module for a narrow, local need — found via this milestone's own Save/Load round-trip test, which is exactly why that test exists.
2. **Two pre-existing compile breaks in `lib/include/ui/` were blocking the entire `ui::` module** (left over from an earlier, uncommitted, in-progress repo-wide naming-convention rename, unrelated to this milestone): a missing `math/math.hpp` include, and a raw-`SDL_TextureAccess`-vs-wrapped-`sdl3::TextureAccess` type mismatch in `shader_effect.hpp` — the same `shader_effect.hpp` bug flagged (but not yet fixed) during Phase 8's cross-check. Fixed as a prerequisite for `ui::` to compile at all; confirmed genuinely correct afterward by re-running `ui_shader_effects_smoke_test` (previously blocked, now 3/3 passing).

## Verification

`tests/game_editor_smoke_test.cpp` (4 CPU-only tests, no window/GPU): Play→Stop snapshot/restore exactness, single-selection replace-on-reselect, Save→Load JSON round-trip (including the FLOAT/INT bug above), and a malformed-root error path. The real example binary: clean build under `-Wall -Wextra -Werror`, a real 180-frame run under `xvfb-run` + ASan/UBSan with `LSAN_OPTIONS=suppressions=tests/lsan_suppressions.txt` (the suppression file gained one new, precisely-documented entry for a `SDL_X11_SetWindowTitle`-internal libX11 leak triggered by any UTF-8 window title — third-party X11 driver internals, not this repo's code) — exit code 0, zero unexpected stderr.

**Full-repo regression sweep after this milestone**: every test binary from every phase of this entire plan (physics, animation, ECS bridge, all four `sql::` binaries, both portal binaries, picking, render3d, game editor, plus spot-checked `ui_smoke_test_*` binaries) passes — 84+ tests total, zero regressions anywhere. Zero clang-tidy warnings anchored in any new/touched file.

## Remplacé (2026-09-12)

`examples/game_editor/editor.hpp` a été SUPPRIMÉ et toute la démo réécrite —
voir [[project-game-editor-app]], qui est désormais le document de référence
pour l'éditeur de niveau. Ce document-ci reste utile pour deux choses :
l'historique du jalon M31/M32, et le contournement du bug FLOAT/INT de
l'encodeur JSON qu'il décrit — bug depuis corrigé À LA SOURCE dans
`data/json.hpp`, le contournement côté lecture n'étant plus qu'une tolérance
pour les documents écrits à la main.

L'astuce `EditorEuler` décrite plus haut n'a également plus lieu d'être :
`math::FQuaternion::ToEuler` existe maintenant.

## Status

M31+M32 DONE as of 2026-08-25. **This closes out the entire 9-phase engine-expansion plan** (`~/.claude/plans/optimized-humming-toast.md`) — `ui::` GPU backend (Phases 1-4), `physics::` (Phase 5), `render3d::` animation (Phase 6), ECS↔render3d bridge (Phase 7), `sql::` at its user-expanded scope (Phase 8), and the game-editor capstone with portal rendering (Phase 9) are all done, independently verified, and documented.
