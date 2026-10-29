---
name: project-ui-fcolor-migration
description: "ui:: module migrated from sdl3::Color (8-bit 0-255) to sdl3::FColor (float 0-1) throughout, per explicit user request. Found and fixed a partially-done, partially-buggy prior migration already in the tree, including a severe UiTheme bug (every themed widget was rendering transparent/black). 31 new named colors added to LIST_COLORS. Done as a detour before Phase 3 of the engine-expansion plan (~/.claude/plans/optimized-humming-toast.md)."
metadata:
  type: project
---

The user explicitly asked (French): "modifie toutes les couleurs utilisées dans l'ui en utilisant en priorité les couleurs prédéfinies de FColor. Si la couleur que tu veux utiliser n'est pas définie dans FColor ajoute-la. N'utilise que des couleurs flottantes." — convert every color in `ui::` (and its examples) from `sdl3::Color` (8-bit 0-255) to `sdl3::FColor` (float 0-1), preferring predefined named colors, adding new named entries where needed, floats only. Done as a detour immediately before Phase 3 (M22) of the engine-expansion plan.

## Critical discovery: this was NOT a from-scratch task

On investigation, a large fraction of `ui::` had **already been partially migrated** to `sdl3::FColor` — the `using Color = sdl3::Color;` alias that used to exist in `components.hpp` had already been removed, and many struct fields/signatures already said `sdl3::FColor`. But the migration was **incomplete and had introduced real, silent bugs**: `FColor`'s constructor clamps components to `[0,1]` (`Clamp(r_, 0.f, 1.f)`), so a leftover literal like `sdl3::FColor{220, 222, 232}` does **not** error — it silently clamps to `{1,1,1}` (white). This is fundamentally different from a normal type-migration bug (which the compiler catches): most of these sites compiled fine and were simply wrong.

## Real bugs found and fixed (not just literal-scaling cleanup)

**The big one**: `UiTheme::Dark()`/`Light()`/`Aero()` (`factory.hpp`) had their entire per-widget-state color tables (`t.button`, `t.toggle`, `t.input`, `t.combo`, `t.tabs`, etc. — everything `StyleFromColors()` reads) **commented out** from a prior commit — meaning **every themed widget in every app using `ui::` was rendering transparent/black**, not a cosmetic issue. (This exact commented-out block is what caused the spurious `-Wunused-but-set-variable` warning on `Light()`'s `focus` variable that got fixed cosmetically during M20 — at the time it looked like ordinary Style-Engine-v2-in-progress dead code per prior memory notes; it was actually a severe live bug, not a documented scope cut. Lesson: an unused-variable warning pointing at dead code is worth asking *why* the code is dead, not just silencing the warning.) Fixed by rewriting all three theme functions with a new `MakeWidgetColors()` helper (plain field assignment — this codebase's `-Werror -Wextra` rejects partial designated-init of the 14-field `WidgetColors` aggregate).

Other real bugs found, all silent-clamp or double-scale variants of the same root cause:
- `ColorToHsv`/`ColorToHex`/`HexToColor` (`components.hpp`) — double-divided an already-normalized float, formatted floats with `%02X` (garbage hex output), and wrote raw 0-255 bytes into normalized `[0,1]` fields — broke the `ColorPicker`'s hex-input sync.
- `UiStyle::Glass()` (`styles.hpp`) — its `lighten` lambda truncated `FColor`'s `[0,1]` floats through `uint8_t`, so every Aero glass panel/button rendered pure white instead of a lightened gradient.
- 8 separate `alpha = uint8_t(float(alpha) * factor)` dimming sites (`plot.hpp` ×4, `nodegraph.hpp` ×2, `systems.hpp` ×2) — truncated fractional alpha to 0, making "dimmed" legend items/disabled nodes/faded connections fully invisible instead of partially transparent.
- `nodegraph.hpp::PackColor` (graph serialize/deserialize) — packed normalized floats directly as integers without scaling to byte range, corrupting persisted/copy-pasted node header colors.
- `systems.hpp::DrawGlowRing` — an intentionally byte-scaled alpha ramp got clamped to full opacity, flattening the glow gradient.
- `HsvToColor`'s `a` parameter was `uint8_t a = 255` returning into an `FColor` — fixed to `float a = 1.f`; the one caller (`factory.hpp`) was doing a wasteful/wrong `uint8_t(a * 255.f)` round-trip, simplified to pass the float straight through.
- A dead, mathematically wrong `detail::LerpColor` in `plot.hpp` (computed `a + b - a*t` instead of `a + (b-a)*t` — coincidentally correct only at `t=1`) — confirmed zero callers (superseded by `sdl3::FColor::Lerp`), deleted rather than fixed.
- `Rect::Rect(FRect)` (new converting constructor added to `structs.hpp` as part of this work) had a copy-paste bug: `w`/`h` were both initialized from `r.x` instead of `r.w`/`r.h` — would have silently corrupted any `Rect` built from an `FRect`. Fixed, and made consistent with the sibling `FPoint::operator SDL_Point()`/`FRect::operator SDL_Rect()` conversions (added alongside it) by using `Round()` + `int` cast rather than truncating.
- Several test-file sites in `tests/ui_smoke_test_7.cpp`/`_8.cpp` (out of the main migration's scope, since `tests/` was deliberately excluded from the bulk pass) still constructed colors with bare 0-255 literals (`sdl3::FColor{9, 8, 7, 6}`, `sdl3::FColor{255, 0, 0, 255}`, etc.) *and* asserted against the old byte-range values (`.r == 255`) — found by actually running the tests (build-only verification doesn't catch this class of bug, since it doesn't error). Fixed both the constructions and the assertions to consistent `x / 255.f` form or the matching named color (`sdl3::FColor::RED()`).

## New named colors (`LIST_COLORS`, `structs.hpp`)

31 new entries, additive-only (existing ~108 entries untouched), each auto-generating both `Color::NAME()` and `FColor::NAME()`: `UI_TEXT_PRIMARY/BRIGHT/MUTED`, `UI_BORDER_SLATE/MUTED/MUTED2`, `UI_PANEL_DARK/DARK2/DARKER`, `UI_BG_DEEP`, `UI_WINDOW_BG`, `UI_APP_BG`, `UI_ACCENT_BLUE/_PRIMARY/_LIGHT/_BRIGHT/_HOVER/_DEEP/_GLOW/_SKY`, `UI_ACCENT_SKY_BRIGHT`, `UI_ACCENT_GREEN`, `UI_NODE_HEADER_BLUE/NEUTRAL`, `UI_AERO_TITLE_DARK/LIGHT`, `UI_AERO_GLASS_BLUE`, `UI_OVERLAY_DARK`, `UI_HIGHLIGHT_FAINT`, `UI_WHITE_SOFT/STRONG` — each named from the actual recurring RGBA combination found across the codebase, not arbitrarily.

## Scope

`lib/include/ui/*.hpp`, all `examples/ui_*.cpp`/`examples/audio_*.cpp`, plus the small set of `structs.hpp` additions above (additive `LIST_COLORS` entries + the `Point`/`Rect`/`FRect` converting constructors needed for the migration + `FColor::Lerp` gaining an optional `alpha` override parameter). `render3d::`/`data::`/`core::` deliberately untouched (already used `FColor` correctly, per explicit instruction). `tests/` deliberately excluded from the bulk pass, with the exception of fixing the handful of sites in `ui_smoke_test_7.cpp`/`_8.cpp` that broke as a direct, discoverable-only-by-running consequence.

## A note on process

This work was done via a dispatched agent given a precise, scoped brief, with direct verification (real builds + real runs, not just `-fsyntax-only`) done independently afterward rather than trusting the agent's own report at face value. That independent verification caught two real problems the agent's own summary didn't surface: (1) two assertions in `ui_smoke_test_7.cpp` had reverted to the old broken byte-range form between rounds of concurrent editing (both sessions were touching the same file near-simultaneously) — re-fixed after being caught by an independent re-run, not by re-trusting the agent; (2) the `Rect(FRect)` w/h-swap bug, found by directly reading the diff rather than relying on "it compiles" as proof of correctness. **Lesson reinforced**: for a task whose defining risk is "produces a value that's syntactically valid but semantically wrong" (as opposed to "fails to compile"), a clean build is necessary but nowhere near sufficient — real runs against real assertions, and a manual read of any diff touching shared/foundational code, are the only levels of verification that actually catch this class of bug.

## Verification

Full project rebuild clean, zero warnings (`-Wall -Wextra -Werror -std=c++23`). All 8 `ui_smoke_test_*` binaries now build (7 and 6 were previously blocked by an unrelated, concurrent naming-convention rename that resolved itself mid-session); **7 of 8 pass completely** (`ui_smoke_test_1` through `_5`, `_7`, `_8`, all exit 0, all assertions green) — `ui_smoke_test_6` fails on one pre-existing, color-unrelated splitter-drag-resize assertion, confirmed out of scope (not touched by, and unrelated to, this work). `render3d_smoke_test` (25/25) and `offscreen_smoke_test` (3/3) re-verified clean, confirming the shared `structs.hpp` changes caused zero regression outside `ui::`. Several examples (`ui_showcase`, `ui_aero_basics`, `ui_node_graph_demo`, `ui_plot_demo`, `audio_patchbay`) built and ran clean for several seconds under ASan/UBSan.

## Status

Done as of 2026-08-23. Resuming Phase 3 (M22 — `ui::Viewport3D`) of the engine-expansion plan next.
