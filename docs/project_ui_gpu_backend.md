---
name: project-ui-gpu-backend
description: "Engine expansion Phase 1 (M20): ui::IUiRenderBackend abstraction (render_backend.hpp), SdlRendererBackend, ui::Ui Initialize/Update/Draw additions. Part of /home/clement/.claude/plans/optimized-humming-toast.md."
metadata:
  type: project
---

`ui::` (`lib/include/ui/`) rendered exclusively via `sdl3::Renderer` (SDL_Renderer) before this phase. M20 introduces a seam — `ui::IUiRenderBackend` — so a future GPU backend (`render3d::Canvas`-based, M21-M22) can render the same widget tree without touching any draw code. Part of a much larger plan (`/home/clement/.claude/plans/optimized-humming-toast.md`, Phases 1-9): `ui::` GPU backend + `Viewport3D` widget + shader effects + `physics::` + `render3d::` animation + ECS/scene bridge + `sql::` + a level-editor capstone example.

## M20 done — `ui::IUiRenderBackend` + `SdlRendererBackend` + `ui::Ui::Initialize`/`Update`/`Draw`

**New file** `lib/include/ui/render_backend.hpp`: `IUiRenderBackend` is a pure-virtual interface whose method set is EXACTLY the primitive surface `RenderSystem`/`NodeGraphSystem`/`plot.hpp`'s free functions already called on `sdl3::Renderer&` (grepped, deduped: `SetDrawColor`/`SetBlendMode`/`SetClipRect`/`ClearClipRect`/`GetDrawColorFloat`/`DrawLine`(2 overloads)/`DrawRect`/`FillRect`/`DrawRoundedRect`/`FillRoundedRect`/`DrawCircle`/`FillCircle`/`DrawArc`/`DrawPie`/`DrawPolygon`/`FillPolygon`/`RenderGeometry`/`Render`(2 overloads, textured quad)). Same names, same signatures as `sdl3::Renderer`'s own methods — the point being that threading `IUiRenderBackend&` through every draw function only changes the PARAMETER TYPE, never the function body (every `ren.FillRect(...)` call site compiles unchanged against either type). `SdlRendererBackend : IUiRenderBackend` wraps a `sdl3::Renderer&` and forwards 1:1 — today's behavior, unchanged, viewed through the new interface.

**Threaded through** `lib/include/ui/systems.hpp` (`RenderSystem::Run`/`DrawTree`/`DrawWidget` + ~15 private draw-primitive helpers), `lib/include/ui/nodegraph.hpp` (`NodeGraphSystem::RenderGrid`/`RenderConnections`/`RenderHeaders`/`RenderPins`/`RenderMarquee` + private helpers), `lib/include/ui/plot.hpp` (9 free `DrawPlot*`/`DrawHeatmap`/`DrawCandleChart`/etc. functions) — every `sdl3::Renderer &ren` parameter became `IUiRenderBackend &ren` via a scoped `sed` (verified no `std::function<...>` field declarations were caught in the substitution).

**Callback escape hatch** (`UiCanvas::onDraw`, `PlotSeries::onCustomDraw`, `UiGraphPin`/`GraphConnection::onCustomDraw`): these three application-supplied `std::function<void(sdl3::Renderer&, ...)>` fields were deliberately **kept unchanged** (not migrated to `IUiRenderBackend&`) to avoid churning every example that sets one. `IUiRenderBackend` gained `virtual sdl3::Renderer *NativeRenderer() noexcept { return nullptr; }` (overridden by `SdlRendererBackend` to return the wrapped renderer); each of the 3 call sites now does `if (auto *nr = ren.NativeRenderer()) callback(*nr, ...)` — the callback still fires normally under `SdlRendererBackend` (verified: `ui_smoke_test_3`'s "canvas callback: ok", `ui_smoke_test_8`'s "onCustomDraw (appelé au rendu...)" both pass), and will simply no-op (or, for `GraphConnection::onCustomDraw`, fall back to the default-connection draw) under a future non-SDL backend until that's addressed. **Deliberate scope cut, not an oversight** — matches this codebase's established best-effort-degrade philosophy.

**`ui::Ui`** (`ui.hpp`): forward-declares `namespace render3d { class Canvas; }` (avoids pulling the huge `render3d/canvas.hpp` into every `ui::` consumer before a GPU backend actually needs it). Constructor now delegates to a new `Initialize(sdl3::Renderer&)` (constructs `Option<SdlRendererBackend>` in place, points `backend` at it) instead of storing a raw `sdl3::Renderer*` directly. Added `Initialize(render3d::Canvas&)` — a **stub** for now (just records the reference in a new `canvasForViewports` member; does NOT change the active 2D backend) — real body lands in M21-M22 when `Viewport3D` exists. Added `Update(dt)`/`Draw()` as pure aliases for the existing `Tick(dt)`/`Render()` (both old and new names work — additive, non-breaking, matches this codebase's established API-evolution habit).

## Real friction hit during the mechanical thread-through

`NodeGraphSystem`'s `Render*` methods are **not** called internally by `RenderSystem` — they're called directly by application code (`examples/ui_node_graph_demo.cpp`, `examples/audio_patchbay.cpp`, `tests/ui_smoke_test_7.cpp`), confirming the prior research finding that NodeGraphSystem is a deliberately separate, app-orchestrated subsystem (not integrated into the generic `RenderSystem::DrawWidget` dispatch). This meant the `sdl3::Renderer&` → `IUiRenderBackend&` signature change on those methods **did** require touching those 3 call sites (wrap the app's `sdl3::Renderer&` in a local `ui::SdlRendererBackend renBackend(ren);`, pass `renBackend` everywhere) — real, minimal, expected churn, not an oversight; the "no churn" guarantee only applies to code going through the `ui::Ui` facade, which fully absorbed the backend swap internally.

## Pre-existing, unrelated compile errors found (NOT introduced by M20, NOT fixed — out of scope)

Confirmed via `git log`/`git status` that these files are tracked and unmodified by this work; both fail to compile independently of any render-backend change (verified the errors reference symbols/members with zero relation to `sdl3::Renderer`/`IUiRenderBackend`):
- `tests/ui_smoke_test_6.cpp:204-205` — `t.Unwrap()->ColumnX[0].width` resolves to `<unresolved overloaded function type>[int]` (a member/overload-resolution mismatch, likely fallout from the concurrent repo-wide clang-tidy naming-convention rename mentioned in `memory/project_render3d_threejs_port.md`'s Makefile history — see commit "Baseline before applying naming-convention rename").
- `tests/ui_smoke_test_7.cpp:857` — `st2.Unwrap()->get<ui::prop::InnerZoom>()` — `UiStyle` has `Get`, not `get` (same naming-rename class of issue).

Both block their respective binaries from building at all (not a regression: they never built during this session either, before or after M20). `ui_smoke_test_7.cpp` also received the same mechanical `sdl3::Renderer&`→backend fix as the other files (it's blocked on the unrelated `get`/`Get` bug at line 857, upstream of the `RenderGrid`/etc. calls at line 951+, so the backend fix in that file is unverified by a build — logged here so the fix isn't lost/re-discovered once the naming-rename bug is separately resolved).

Two more one-line pre-existing issues fixed only because they blocked *verifying no regression* in files this phase's Verify step explicitly named: `tests/ui_smoke_test_2.cpp:28` (unused lambda param `r`) and `examples/ui_showcase.cpp:253` (unused `const ui::UiTheme &th`, dead declaration removed). Neither related to rendering.

## Verification

Real builds + real runs (not `-fsyntax-only`), matching this session's established bar. Built and ran clean: `ui_smoke_test_1/2/3/4/5/8` (all exit 0, all prior assertions still pass — this is a pure-refactor phase, "identical before/after" is the test itself), `ui_minimal`/`ui_showcase`/`ui_aero_basics`/`ui_aero_lists`/`ui_aero_windows`/`ui_node_graph_demo`/`audio_patchbay`/`ui_plot_demo` all compile; `ui_minimal`/`ui_showcase`/`ui_aero_basics`/`ui_node_graph_demo` ran for several seconds under ASan/UBSan (`SdlContext` + real window) with zero stderr output. `ui_smoke_test_6`/`ui_smoke_test_7` blocked by the pre-existing unrelated bugs above (not run).

## Status

M20 (Phase 1 of the engine-expansion plan) DONE as of 2026-08-22. Next: M21 (`render3d::OffscreenSurface`, the shared GPU↔2D compositor primitive both `Viewport3D` (M22) and widget shader effects (M23) will build on).
