---
name: project-ui-ecs-module
description: "The rewritten src/ui/ module — ECS-backed retained UI with scenes, factory/builder, dirty-cached layout; naming/API decisions made by the user mid-session."
metadata: 
  node_type: memory
  type: project
  originSessionId: 4007b853-281f-4ca2-a318-41aaecee4cae
---

**2026-07-05 session.** `src/ui/` fully rewritten as an ECS-backed
retained-mode UI (the old never-compiled UINode tree — node/layout/renderer/
widgets/style/types.hpp — was deleted after the user explicitly asked to
remove useless code; an earlier attempt without that explicit ask was
correctly denied). Sources of inspiration: `voxel/crates/engine_ui`
(components/systems/factory/builder split, Val units, flow layout,
per-widget-per-state WidgetColors themes) and `SDL3pp_ui_v2` (pipeline
Measure→Place→Input→Render, builder DSL).

**Files:** `components.hpp` (Dimension/Sides/Anchor/UiRect/UiFlow/UiItem +
widget components Panel/Label/Button/Toggle/Checkbox/Slider/Progress/
Separator/Input/Image + UiCallbacks + hierarchy helpers), `systems.hpp`
(LayoutSystem/InputSystem/RenderSystem), `factory.hpp` (UiTheme dark/light,
WidgetBuilder, UiFactory), `scene.hpp` (Scene/SceneManager, lazy build,
hide-preserves-state), `ui.hpp` umbrella. Tests: `tests/ui_smoke_test_1/2/3.cpp`
(the user adopted my two debug programs as tests 1–2 and renamed my
smoke_test_13 to ui_smoke_test_3).

**Perf model (the "GTK4-like" ask):** LayoutSystem is dirty-flag cached —
`runIfNeeded()` does nothing unless a builder spawned/scene toggled/scroll
changed/window resized. Measured: 1200 widgets → build ~37ms, one layout
pass ~19ms, **1000 idle frames ~0.007ms total**. Hierarchy is an explicit
`UiChildren` ordered list maintained by `setParent` (no per-frame tree
rebuild, unlike the Rust reference). Scene switching hides/shows retained
entities (widget state survives) — no rebuild. RenderSystem caches
`sdl3::Text` objects per entity keyed on string+color.

**User naming/API decisions made mid-session (respect these):**
- `ui::Val` renamed to `ui::Dimension`, `ui::Unit` to `ui::DimUnit` (aligns
  with the old style.hpp vocabulary), `ui::World` alias renamed to
  `ecs::ArchetypeRegistry`.

**Typographic units (2026-07-05, user request):** `DimUnit::Em` (× the
widget's own effective font size), `Pem` (× parent's effective font),
`Rem` (× `LayoutSystem::rootFontSize`, which `UiFactory`'s ctor aligns to
`theme.fontSize`). "Effective font" = own fontSize (Label/Button/Input)
else inherited from parent chain, root inherits rootFontSize — precomputed
per entity in one DFS during `Pass::snapshot()` (`computeFonts()`), which
keeps `measure()` memoization valid and Em/Pem resolution O(1).
`Dimension::resolve()` gained an `EmBases {em, pem, rem}` defaulted third
parameter. Covered by asserts in ui_smoke_test_3 (§8bis).
- Widget text fields use core `String`, not std::string.
- `Option<T>::Unwrap()` returns a COPY — mutation must go through
  `operator*`/`operator->` (this silently broke builder chainers once;
  documented in factory.hpp).

**Dedup decisions (this session's audit):**
- `math::Corners`/`math::Sides` moved from render.hpp to sdl.hpp (generic
  geometry); `Sides` gained `h()`/`v()`; `math::Sides` is now
  `using Sides = math::Sides` (was a duplicate struct with l/t/r/b fields).
- `FRect` gained `intersects()`/`intersection()`; ui's local
  `intersect()`/`rectContains()` helpers deleted in favor of FRect methods.
  While converting, caught a user-edit regression in InputSystem's `hitOk`
  (checked mouse-in-clip but no longer mouse-in-widget-rect — any point in
  a panel would "hover" every widget in it); fixed to
  `screen.contains(p) && clip.contains(p)`.
- `sdl.hpp` now includes `stdinc.hpp` (base layer, SDL3pp-style) — the
  duplicated float math helpers previously in sdl.hpp are gone.
- `UiPanel::BordersWidth` was unused by RenderSystem → implemented
  (concentric rounded outlines) rather than removed.

**ECS additions for the UI** (in `src/ecs/ecs.hpp`): `get_or_add_component`,
`entities_with<T>()`, `clear()`, and `ecs::CommandBuffer` (deferred
add/remove/despawn — REQUIRED for structural changes from inside
`Query()` callbacks; UI callbacks must use it too, or defer). Also fixed a
latent never-compiled bug there (`Option::hasValue()` → `IsSome()` in
`remove_component`) — ecs.hpp had never been included by anything the
Makefile compiled until the UI module pulled it in (same lesson as math.hpp,
see [[project-renderer-shapes-and-stdinc]]).

**Makefile:** test binaries now regenerate when headers change
(`-MMD -MP` + `-include *.d`) — previously `make tests` silently reused
stale binaries after header-only edits, which cost a debugging detour.

**Widget set (2026-07-05, complete vs SDL3pp_ui.h's list):** Container
(column/row/panel), Label, Button, Toggle, Checkbox, Radio (group-exclusive,
two-pass input so no structural change during query), Slider H/V,
ScrollBar H/V (standalone: contentSize/viewSize/offset, thumb-follows-mouse
drag, onScroll callback), Progress, Separator, Input, Knob (normalized [0,1],
vertical-drag ±0.005/px, 135°→405° dial sweep), Image, Canvas (custom
`std::function<void(sdl3::Renderer&, FRect)>` draw callback — needed a
`namespace sdl3 { class Renderer; }` forward decl in components.hpp since
it only includes sdl.hpp). Theme gained radio/scrollbar/knob WidgetColors
tables. That covers every widget SDL3pp_ui.h lists.

**Extended widget batch (2026-07-05, second wave):** UiComboBox (dropdown
drawn as a clip-free OVERLAY pass in RenderSystem::run; open combo is modal —
InputSystem processes combos FIRST and sets `clickConsumed`, all later
handlers use the gated `pressed` local), ListBox (leaf widget, internal
scroll + inline thumb, consumes wheel), UiExpander (header click toggles
UiHidden on children — structural, so collected during query and applied
after), UiTabView (equal-width tab bar in the flow padding-top, same deferred
pattern), UiSpinner (animated in InputSystem — it has dt), UiBadge, UiTooltip
(InputSystem tracks smallest hovered tooltip'd widget + hover timer, exposes
`input.tooltip`; pass `&input.tooltip` as 3rd arg to RenderSystem::run).
Complements: UiDisabled marker (recursive; hitOk excludes, render dims via
translucent overlay after subtree), UiSlider.step + wheel-adjust, UiKnob real
min/max/step range (default [0,1] keeps old semantics), ImageFit
Fill/Contain/Cover/None on UiImage (`.fit()` builder chainer),
Window::startTextInput()/stopTextInput() added to render.hpp.
RenderSystem gained a string-keyed text cache (strCache) for per-item text
(list items, tabs, tooltip) alongside the entity-keyed one. Covered by
tests/ui_smoke_test_4.cpp (16 tests total, all green).

**Examples (./examples, user-requested location):** `ui_showcase.cpp` —
full interactive showcase modeled on SDL3pp 13_ui_v1.cpp (header nav +
5 SceneManager pages: Base/Contrôles/Saisie/Listes/Image, tooltips
everywhere); `example_ui.cpp` — minimal UI example (the user had left a
14-line stub without main() there — completed it rather than deleting);
`example_renderer.cpp` — the user MOVED old src/main.cpp there. src/ is now
headers-only, so Makefile `all` builds `examples` when SOURCES is empty
(ifeq guard). `make examples` → build/examples/*; the user added the
examples/run-examples rules themselves (EXAMPLESDIR naming), I only added
the missing EXAMPLES_SOURCES/BINS vars and .PHONY entries.

**Layout gotcha (bit in test 4):** a flow container with NO UiItem freezes
its Auto size after the first pass — writeBack() stores the computed size in
UiRect.size, and sizeVals() then returns px(previous size). Any widget whose
auto-size must change at runtime (expander collapse, tab switch) needs an
explicit UiItem (factory expander()/tabview() now call wAuto().hAuto()).
Badge defaults alignSelf(Start) so the pill hugs content despite Stretch.

**Known limitations / next steps if asked:** no Wrap layout dir, no grid
(SDL3pp has InGrid + GridCell spans — the biggest remaining gap), no inline
auto-scrollbars rendering on containers (wheel scroll works, bars not drawn;
ListBox draws its own), Input widget has end-cursor editing only (no
mid-string cursor/selection), callbacks must not mutate ECS structurally
(use CommandBuffer). Combo dropdown always opens downward (no flip-up).
Splitter/Tree/ColorPicker/Popup/Graph/TextArea from 13_ui_v1.cpp unported.

**Test-writing gotcha (hit twice):** widgets positioned outside the
800×600 test window silently fail hit-testing (clip excludes them) — when
adding UI test blocks, keep the whole widget stack inside the window bounds.

**2026-07-05, critical bug found in ui_showcase.cpp (real crash, user-reported
"plante quand je change d'onglet" + garbled widget rendering).** Root cause:
`ecs::ArchetypeRegistry::Query<>()` iterates
`for (const auto& arch_ptr : archetypes)` over a `std::vector<unique_ptr
<Archetype>>`. The OLD `InputSystem::run()` fired `UiCallbacks::onClick/
onChange/onToggle/...` **directly inside** the per-widget-type query lambdas.
The showcase's header nav buttons' `onClick` called `SceneManager::switchTo()`,
which does `add_component<UiHidden>` on many entities — a structural mutation
that can call `archetypes.push_back(...)` (via `get_or_create_archetype`) and
**reallocate the vector**, invalidating the `arch_ptr` reference the OUTER
query loop was still iterating over → UB (observed as real SIGSEGVs, confirmed
via `journalctl -k` kernel segfault lines matching the user's screenshot
timestamps, `ui_showcase[pid]: segfault at 10 ...`). This is the same class of
hazard [[project-ui-ecs-module]] already flagged for CommandBuffer, but it
had NOT been applied to callback firing sites, only to Expander/TabView's own
internal UiHidden toggling.

**Fix — deferred callback pattern, applied to every callback site:** every
`if (cb.IsSome() && cb.Unwrap()->onX) cb.Unwrap()->onX(args)` inside a query
lambda now instead copies the `std::function` and pushes a closure into a
local `std::vector<std::function<void()>> pending` declared at the top of
`InputSystem::dispatch()`; `pending` is drained in a plain loop **after**
every `world.Query<>()` call in that function has returned (i.e., not inside
any active archetype iteration) — mirrors why `CommandBuffer::Flush()` is
safe. Covered by `tests/ui_smoke_test_5.cpp`: 3 scenes of ~75 widgets each,
3 nav buttons whose `onClick` calls `scenes.switchTo()`, clicked 60 times via
synthetic `sdl3::Event`s — passes cleanly (would previously corrupt/crash).

**Second request in the same turn — remove the InputState aggregation
pattern entirely** ("évite la structure substitute InputState, elle corrompt
les évènements" — batching multiple SDL events into one struct per frame
overwrote mouse positions and could set pressed+released simultaneously).
`ui::InputState` (public struct) is gone; `InputSystem` now exposes:
- `handleEvent(world, const sdl3::Event&, layout)` — one discrete SDL event,
  hit-tests/dispatches with THAT event's own coordinates. Persists
  `mouseX/mouseY/down` as private members across calls (continuous drags
  and hover survive across separate motion/wheel events).
- `tick(world, layout, dt)` — continuous, event-independent updates: toggle
  `animT` easing, spinner rotation, input-focus cursor blink, tooltip hover
  timer (needs real dt, driven every frame regardless of events — mirrors
  SDL3pp's real `ui.ProcessEvent(ev)` / `ui.Iterate(dt)` split).
Internally a private `InputSystem::Frame` struct replaces the old public
`InputState` for passing one event's flags into `dispatch()`.
`src/ui/systems.hpp` now includes `sdl3/events.hpp` for `sdl3::Event`.

**Third request — `ui::Ui` facade (src/ui/ui.hpp, ~60 lines)** bundling
LayoutSystem+InputSystem+RenderSystem+UiFactory for the common case; does
**not** own `ArchetypeRegistry` (kept external, passed by ref, so apps can add
their own components) nor Window/Renderer (kept as pointers). Ctor
`Ui(world, window, renderer, theme=dark())` calls `window.startTextInput()`
automatically. API: `handleEvent(ev)`, `tick(dt)`, `render()` (does
`layout.runIfNeeded(world, window.size())` + `renderSystem.run(..., &tooltip)`
in one call — app just calls this once per frame right before
`renderer.present()`), plus `layout()/input()/renderSystem()/factory()/
world()/theme()/setTextEngine(engine,font)` accessors. `examples/ui_minimal.cpp`
migrated to it — **local variable was named `gui`, not `ui`**: naming it `ui`
shadows the `ui::` namespace for every subsequent `ui::Xxx` qualified name in
the same scope (e.g. `ui::Anchor::Center`) → hard compile error. Don't reuse
the namespace name for the instance.

**Examples were renamed by the user mid-session** (not by me):
`example_renderer.cpp` → `renderer.cpp`, `example_ui.cpp` → `ui_minimal.cpp`.
`ui_showcase.cpp` also had cosmetic touch-ups applied externally (window
title fixed to a `u8"..."` literal — the earlier em-dash was mojibake;
`Font::find` → `Font::findLocal` in that file specifically). Always re-read
these three files before editing — they get hand-edited between turns.

**GUI verification lesson:** manual xdotool/xwd verification in this
snap-confined X11+XWayland desktop session is flaky in a way unrelated to
app bugs — windows intermittently vanish with **no** kernel segfault line, no
coredump (core_pattern is apport, which silently drops unpackaged binaries),
and no stderr output, after repeated rapid launches. Contrast: the ORIGINAL
pre-fix bug reliably left `kernel: ui_showcase[pid]: segfault at 10 ...` lines
in `journalctl -k`. Absence of that signature post-fix, on runs that DID
survive (one run took 30 rapid clicks across all 5 nav tabs over 3 minutes,
clean screenshot, no more overlapping-widget rendering glitches on
Contrôles/Listes pages either — that corruption was downstream of the same
UB), is the real evidence; don't over-trust a single flaky manual repro when
a targeted headless regression test (ui_smoke_test_5) proves the mechanism
directly and reproducibly.

---

## 2026-08-10 session — ImGui-level widget coverage + Frutiger Aero theme + borderless chrome

New multi-phase initiative, plan file `/home/clement/.claude/plans/mossy-forging-fog.md`
(Phases 0–10, updated as each lands): bring `ui::` towards Dear ImGui's widget breadth
("Maximale" tier, user-confirmed), a Windows-7-Aero-glass theme, and 3+ example apps each in
an SDL3 **borderless** window whose title bar is drawn by `ui::` widgets. `ui_minimal.cpp`/
`ui_showcase.cpp` stay as the dark-theme reference but may be adapted if underlying `ui::` APIs
change; all new work lives in new files (`interaction.hpp`, later `chrome.hpp`, `examples/ui_aero_*.cpp`).

**Style Engine v2 — the biggest architectural change this session (unplanned, emerged mid-session).**
The user required ALL widget aesthetics to depend exclusively on a CSS-like style system —
widget components (`UiLabel`/`UiButton`/`UiSlider`/...) no longer store `fontSize`/colors/
border/radius, only structural/behavioral fields (text, value, min/max, checked, items...).
Design converged through three rounds of user correction:
1. First ask ("glass effect") was building it as `styles.hpp` static `UiStyle` factory
   functions, not a bespoke `UiGlass` component — this revived the pre-existing but
   never-wired `StyleSystem`/`UiStyleSheet`.
2. Second ask (full style-driven widgets) — I proposed a flag-based priority model
   (inherit-from-parent/inherit-from-root/private/heritable); **user rejected it** in favor of
   named CSS classes with explicit priority = list order (last wins), a universal `"root"`
   fallback every widget carries, and a dynamic string-keyed API: `gui.createStyleClass(name,
   style)` / `gui.setStyleProperty("target.property", value)`.
3. Third ask pointed at a concrete reference implementation on another local project,
   `SDL3_ui/src/UI/Style.hpp` — type-erased property bag (`unordered_map<type_index, {any,
   name}>`) with tiny per-property structs generated by macros
   (`UI_PROP_COLOR/FLOAT/SIDES/CORNERS/ALIGN(Name, "css-name")`), adapted to the ECS: scope
   narrowed to **visual-only** properties, structural/behavioral widget data stays on ECS
   components as before. `prop::BordersWidth`/`BordersRadius` are `Sides`/`Corners`-typed (not
   float) per an explicit later user requirement — every edge/corner independently configurable.
   **Extension pattern for future style properties**: one `UI_PROP_*` macro line in `styles.hpp`'s
   `prop::` namespace + a fluent `UiStyle` setter one-liner + a `ResolvedStyle` accessor pair +
   an entry in the relevant `detail::*PropRegistry()` table if reachable via `setStyleProperty`.
   `UiFactory` registers a `"root"` class (generic defaults) + one `"root-<kind>"` class per
   widget type (from `UiTheme`'s `WidgetColors` tables via `styleFromColors()`) — always
   lowest-priority in `WidgetBuilder::Spawn()`'s `UiClassList`, so explicit classes/inline style
   always win (standard CSS cascade).
   **Known scope limit**: `RenderSystem`'s border-drawing (concentric-outline loop) honors
   per-corner `BordersRadius` fully but still uses one representative value (`.top`) for
   per-edge WIDTH — `sdl3::Renderer::drawRoundedBorderedRect` (draws true per-side annulus, but
   OUTSET of the rect vs. the current INSET convention) is the documented alternative if a
   future widget (e.g. Phase 9's title bar) needs real per-edge width.
   **Real bug found**: `StyleSystem::resolve()`'s dirty-clearing pass did
   `world.Query<UiStyleDirty>([&](Entity e, UiStyleDirty&){ world.remove_component<UiStyleDirty>
   (e); })` — removing a component INSIDE the query iterating that same component corrupts the
   archetype iterator (same hazard class as the 2026-07-05 callback bug above, now hit in a new
   spot). Fixed by collecting dirty entities into a `std::vector<Entity>` first, mutating in a
   second loop. **Grep for this pattern before adding any new `Query<T>` callback that also
   calls `add_component<T>`/`remove_component<T>` on that same T.**

**Phase 0 — Aero glass rendering foundation.** `RenderSystem::drawGlossHighlight`/`drawGlowRing`
(private, same pattern as `drawVGradient`), `UiTheme::aero()`, `WidgetBuilder::glass(gloss, glow)`.

**Phase 1 — Popup/Modal overlay system.** Reuses the existing `AttachLayout::Fixed` overlay
pass in `RenderSystem::run()` (no second overlay mechanism). `UiOverlayLayer{int order}` sorts
that pass (`std::stable_sort`) so nested popups/modals stack correctly. `UiPopupState{bool
open, modal; Entity trigger}`; open/close toggles `UiHidden` (never spawn/despawn — preserves
state). `InputSystem` gained `modalStack` (blocks the rest of the tree via `isDescendantOrSelf`
in `hitOk`), Escape pops the top modal. Free functions `ui::openPopup/closePopup`; modal
variants are `InputSystem` methods (`openModal/closeModal/hasOpenModal`) since they touch the
private stack. `UiFactory::popup()` (Fixed+hidden+themed) and `::modal(content)` (wraps content
with a full-screen scrim + centers via `.absolute().anchor(Center)`). Not yet covered by an
automated test — deferred to Phase 10's `ui_smoke_test_6` alongside Phase 2's primitives.
**Gotcha for future Aero examples**: existing examples treat Escape as "quit" before forwarding
to `ui::` — new examples must check `gui.hasOpenModal()` first.

**Phase 2 — `src/ui/interaction.hpp` (new file)**, shared primitives with no widget of their
own: `SelectionState{unordered_set<int> selected; int anchor,lastClicked}` + `UiSelection` +
`applySelectionClick(state, index, ctrl, shift, multiSelect)` (Explorer/Finder semantics —
plain=replace, ctrl=toggle, shift=range-from-anchor), for Table (Phase 6)/TreeNode+Selectable
(Phase 4). `UiResizeHandle{Orientation; function<float()> getBefore/getAfter;
function<void(float)> setBefore/setAfter; min/max Before/After; dragging/hovered state}` +
`resolveResizeDrag()`, for Splitter (Phase 8)/Table column resize (Phase 6) — the drag
press/move/release lifecycle is already wired into `InputSystem::dispatch()` this phase (new
`resizeDrag` member), so later widgets just attach the component + accessors, no per-widget
drag loop duplication. Unrelated bug fixed while verifying: a concurrently-added
`boolPropRegistry()` (new `prop::Enable`/`prop::Visible` style props) was missing its
`return table;` statement — genuine typo, not a design issue.

**Phase 3 — value-editing widgets.** `UiDragValue` (leaf, `components.hpp`): ImGui::DragFloat-
style — horizontal drag changes `value` (`speed` units/px), a **double-click** (via SDL's
native per-event `clicks` counter on `SDL_MouseButtonEvent`, exposed as `Frame::doubleClick` in
`InputSystem`, no manual timer needed) enters minimal text-edit mode (digits/point/sign only,
backspace, Enter commits, Escape cancels — cursor always at end, no mid-string
navigation/selection, deliberately simpler than `UiInput`). `UiFactory::inputNumber()` is a
**composite**, not a new leaf: a row of `[- UiButton][UiInput][+ UiButton]`. `UiColorSwatch`
(leaf, button-like hover/press/click) + `UiColorPicker` built as a Phase-1 popup from three new
leaf widgets — `UiSVSquare` (2D drag, 4-corner gradient via `renderGeometry`), `UiHueSlider`
(1D drag, 6-segment rainbow bar), `UiAlphaSlider` (1D drag, checkerboard + gradient) — plus a
hex `UiInput` and a preview `UiColorSwatch`. New free functions in `components.hpp`:
`hsvToColor`/`colorToHsv`/`colorToHex`/`hexToColor`. New `RenderSystem` private helpers:
`drawHGradient`/`drawQuadGradient`/`drawHueBar`/`drawCheckerboard`.
**Cross-widget sync pattern (new, worth reusing in later composites)**: `WidgetBuilder`'s
fluent chain builds an entity tree that doesn't exist yet — sibling sub-widgets can't capture
each other's `Entity` at construction time. `inputNumber()`/`colorPicker()` solve this by giving
internal sub-widgets an auto-generated, collision-proof `UiName` (e.g.
`"__ui_colorPicker_3_sv"`) and having each other's callbacks do `findByName(world, name)` at
*click/drag time* (when the entity definitely exists) rather than at construction time. The 3
color-picker sub-widgets' `onChange` callbacks are a "ping" (`fn(0.f)`, argument unused) that
triggers a full `resync()` re-reading all 3 widgets' current state — simpler than trying to
thread partial (h)/(s,v)/(a) updates through the generic `onChange(float)` signature.
Verified via a standalone `/tmp` harness that spawns one of each new widget + resolves style +
runs layout (catches template/lambda-capture issues a bare `-fsyntax-only` header check can
miss), then the full `ui_smoke_test_1..5` + headless `ui_showcase`/`ui_minimal` regression run,
all green after every phase so far.

**Phase 4 — Selectable & TreeNode, with drag-reorder.** `UiSelectable` (leaf-ish flow container,
`components.hpp`): PERSISTENT highlight (unlike `UiButton.pressed`, momentary) — deliberately
owns no selection state itself; it walks up `UiParent` via a new `nearestSelectionAncestor()`
(`interaction.hpp`) to find the nearest ancestor carrying a `UiSelection` (attached to a list's
root container via `WidgetBuilder::selectionRoot()`) and reads/writes selection there. Click
handling mirrors the pre-existing `UiRadio` group-exclusivity code exactly: pass 1 detects the
click and calls `applySelectionClick` (ctrl=toggle, shift=range, Explorer/Finder semantics, from
Phase 2) once on the container's `SelectionState`; pass 2 walks every `UiSelectable`/`UiTreeNode`
in that same container and reflects `.selected` — same two-pass shape as the Radio group, for the
same reason (avoid touching sibling state while a structural-ish decision is still being made).
`UiTreeNode` is `UiExpander`'s header-toggle pattern fused with `UiSelectable`: a fixed-width
arrow hit-rect at the header's left edge toggles `expanded` (structural — deferred/collected then
applied, identical to `UiExpander`'s own toggle code) and hides/shows children via `UiHidden`;
a header click OUTSIDE the arrow instead runs the same selection path as `UiSelectable`. Nested
tree levels are just `UiChildren` — a `treeNode()` node's own children are typically more
`treeNode()`/`selectable()` calls, so the tree IS the ECS hierarchy, no separate tree-model
structure. **Indentation realization**: I first wrote `depth * 16px` baked into each node's own
left-padding at construction — wrong, because nesting through `UiChildren` already compounds each
ancestor's own left-padding automatically. Switched to a FIXED per-level padding
(`Sides{24,...}`) applied uniformly regardless of `depth`; `depth` ended up a purely informational
field on `UiTreeNode` (kept for the caller's own bookkeeping, e.g. alternating row styling),
unused by the widget's own layout/hit-testing.

`UiReorderable` (`interaction.hpp`, alongside `UiResizeHandle` — same "transverse drag primitive"
category, reusable by Table rows in Phase 6): opt-in via `.reorderable()` on any row-like builder.
Its press/drag/release lifecycle is wired into `InputSystem::dispatch()` the same way
`UiResizeHandle`'s was in Phase 2 (latch-on-press, follow-while-down, commit-on-release), but
resolves the DROP TARGET by scanning siblings (same `UiParent`) for the nearest one whose vertical
center is closest to the cursor, then calls a new `reorderChild(world, parent, child, target)`
that mutates `UiChildren` DIRECTLY (erase + reinsert-before-target) rather than maintaining any
separate "logical order" state — the ECS child list order already IS the render order IS the
truth, so there's nothing else to keep in sync. A `dragMoved` threshold (4px) on `UiReorderable`
disambiguates a real drag from the plain click that `UiSelectable`/`UiTreeNode` (often on the SAME
entity) already handles unconditionally on press — without it, every selection click would also
fire a spurious zero-distance reorder. The container gets notified via a new
`UiCallbacks::onReorder(int from, int to)` field, posed on the PARENT (via `.onReorder()`), not
the dragged row itself.

**New dependency this phase**: `SDL_MouseButtonEvent` carries no modifier-key field (unlike
`SDL_KeyboardEvent.mod`, already used for arrow-key selection extension) — ctrl/shift for a mouse
CLICK has to be read live via `sdl3::keyboard::mods()` (`src/sdl3/input.hpp`, wraps
`SDL_GetModState()`), so `systems.hpp` gained `#include "../sdl3/input.hpp"`.

Verified the same way as Phase 3: a standalone harness (spawn a selection-root list containing 2
`selectable()`s + a `treeNode()` with a reorderable child, resolve style, run layout, synthesize
a click via `sdl3::Event`, assert the resulting `UiSelection::state.selected` set) plus the full
`ui_smoke_test_1..5` + headless `ui_showcase`/`ui_minimal` run — all green.

**Phase 5 — MenuBar/Menu/MenuItem, one level of submenu.** `UiMenuBarItem`/`UiMenuItem`
(`components.hpp`) store `text` directly on the component (like `UiButton`), not as a child
label — keeps them true leaves, simpler `intrinsic()` sizing, no child-label indirection.

**Key architectural realization**: dropdown/submenu popups must be spawned as ORPHAN ROOT
entities (no `UiParent` at all) — a Fixed-attached widget's position still resolves against its
own PARENT's content-box origin (`place()`'s `absKids` path, same as `Absolute` — only the CLIP
gets reset to full-window for `Fixed`, not the position), so a nested popup would need offset
math relative to its parent's content origin. An orphan root, by contrast, resolves its
anchor+offset directly against the window's (0,0) via `LayoutSystem::run()`'s root-entity
handling — which is what makes the new `positionPopupBelow`/`positionPopupRightOf` helpers'
simple window-absolute-coordinate math (`{trigger.x, trigger.y + trigger.h}` / `{trigger.x +
trigger.w, trigger.y}`) actually correct. Position is computed once, at open time, from the
trigger's current `UiComputed.screen` — menus don't track a live-following position while open
(standard menu behavior; a resize mid-interaction would leave it stale, acceptable).

**API shape**: `UiFactory::menu(Entity bar, String text, Builders&&... items) -> Entity` and
`subMenu(Entity parentPopup, String text, Builders&&... items) -> Entity` are EAGER — they spawn
immediately and return the popup's `Entity`, rather than returning an unspawned `WidgetBuilder`
like every other factory method. Reason: they need to cross-link an already-spawned MENU ITEM's
`menuPopup`/`submenuPopup` field to an already-spawned POPUP's `Entity` — the same chicken-egg
problem as Phase 3's `inputNumber()`/`colorPicker()` (an entity doesn't exist until `.Spawn()`
runs, so sibling builders can't capture each other's `Entity` while still being composed), but
solved differently here: Phase 3 used `findByName()` lookups deferred to click-time, because
NEITHER sibling existed yet at construction time; here the CALLER already has a real `Entity` for
`bar`/`parentPopup` (it was returned by an earlier `menuBar().Spawn()` or `menu()`/`subMenu()`
call), so eager two-step spawn-then-cross-link works directly, no name-based indirection needed.
Consequence: `subMenu()` items always land AFTER a `menu()` call's own `items...` in the popup's
child order (can't interleave a submenu trigger between two plain items in one call) — acceptable
given the plan only requires one level of submenu, noted as a real but minor ordering limitation.

`closeMenuChain()` (a leaf `UiMenuItem` click, no submenu, executes + closes everything) walks
`UiPopupState.trigger` + a new `nearestPopupAncestor()` (climbs `UiParent` to the nearest
`UiPopupState`-bearing ancestor — same shape as Phase 4's `nearestSelectionAncestor`) to cascade
through however many popup levels are actually open. Only one level (item → submenu) is exercised
by the plan, but the walk is depth-general for free. Outside-click closing of the WHOLE chain
needed **no new code at all** — Phase 1's existing per-popup outside-click loop already iterates
every open `UiPopupState` independently each click, so a submenu and its parent dropdown each
close themselves correctly without any chain-awareness.

**Iterator-invalidation hazard, hit a second time this session**: `openPopup`/`closePopup`
internally call `add_component`/`remove_component` (for `UiHidden`), which can migrate an
archetype and reallocate the archetypes vector — calling them directly inside the
`world.Query<UiMenuBarItem>`/`Query<UiMenuItem>` lambdas that were ALSO deciding whether to open/
close would have corrupted those same iterations. Same fix as the `StyleSystem`/Popup-outside-
click bugs from earlier phases: collect the decision (`popup`, `trigger`, `triggerScreen`, `open`
bool) into a `menuToggles` vector during the query, apply `openPopup`/`closePopup` in a plain loop
strictly after. **This pattern has now recurred often enough** (StyleSystem::resolve, Popup
outside-click, this) that any FUTURE widget whose click handler needs to open/close a popup should
default to this collect-then-mutate shape from the start rather than being found via a crash.

**Distinct, NEW kind of bug this phase (not the same hazard, a plain C++ rule)**: a free function
called from the BODY of a member function defined inline inside a class only compiles if that
free function was declared BEFORE the class — the "complete-class context" rule that lets an
inline member call another member declared LATER in the SAME class does NOT extend to free
functions declared later in the enclosing namespace. `openPopup`/`closePopup` (pre-existing, Phase
1) had always been declared AFTER `class InputSystem`, which happened to compile fine only because
nothing inside `InputSystem`'s own methods had ever called them (the Phase 1 dispatch() code
manipulates `UiPopupState`/`UiHidden` directly inline instead). The moment Phase 5's
`dispatch()` needed to call them (plus the 4 new helpers), this surfaced immediately as a
compile error — fixed by moving `openPopup`/`closePopup`/`nearestPopupAncestor`/
`positionPopupBelow`/`positionPopupRightOf`/`closeMenuChain` to appear BEFORE `class InputSystem`
in `systems.hpp` (right after the `// InputSystem` banner comment, before the class itself).

Verified via a standalone harness: `menuBar().Spawn()` → `menu(bar, "Fichier", menuItem("Nouveau")
.onClick(...), menuItem("Quitter"))` → `subMenu(fileMenuPopup, "Recents", menuItem(...), menuItem
(...))`, then synthetic clicks on the bar item (asserts the popup opens) and on "Nouveau" (asserts
`onClick` fired AND the popup closed) — plus the full `ui_smoke_test_1..5` + headless
`ui_showcase`/`ui_minimal` run, all green.

**Phase 6 — Table (resizable/sortable columns, multi-select, row drag-reorder).** Deliberate
architectural departure from every widget in Phases 1–5: `UiTable` is a SELF-CONTAINED,
self-drawing leaf (`components.hpp`) — like the pre-existing `ListBox`/`UiComboBox` — NOT a
composition of one ECS entity per cell. A 100-row × 5-column grid would need 500+ entities for a
widget whose cells need zero individual interactivity beyond displaying text; the ECS-per-widget
idiom used everywhere else in this module stops making sense at that granularity. It still
genuinely REUSES two Phase 2/4 primitives as real shared components/calls: `UiSelection` (attached
as an actual separate ECS component on the table entity, same type, same `applySelectionClick()`
call as `Selectable`/`TreeNode`) and `UiCallbacks::onReorder`'s exact `(int,int)` signature. What
it does NOT reuse is the ENTITY form of `UiResizeHandle`/`UiReorderable` — both assume independent
entities with accessor functors to move/resize; `UiTable`'s column-resize and row-reorder drags
reimplement the same ALGORITHM (latch-on-press, threshold-then-follow, commit-on-release) directly
against its own internal `columns`/`rows` vectors, since there's no per-column/per-row entity to
attach a component to. Row rendering reuses `ListBox`'s existing virtualization pattern verbatim
(compute `first`/`last` visible row index from `scroll`/`rowHeight`, tighten the clip rect to the
body area, restore the outer clip afterward) — same shape, copied rather than factored into a
shared helper (small enough that a helper would add more indirection than it saves).

**Known, deliberate limitations** (both documented in the component's own doc comment):
- Sort is LEXICOGRAPHIC on the cell's raw `String` value, not numeric/typed — sorting cells
  `"10"`/`"20"`/`"5"` ascending leaves them in that exact order, since `'1' < '2' < '5'` as
  characters. Caught during verification: a test first asserted NUMERIC-sort behavior (`"5"`
  first), failed, and had to be corrected once the actual (correct, as-designed) lexicographic
  output was observed — a genuine "test was wrong, not the code" case, worth remembering before
  assuming a red assertion means a source bug.
- Sort and row-reorder both operate on raw row INDICES with no stable per-row identity. Sorting
  therefore deliberately CLEARS the table's current `UiSelection` — after a sort, the old selected
  indices would silently point at different (wrong) rows rather than following the data they used
  to mark. A future "stable row ID" model (e.g. rows carrying an opaque id, selection keyed on that
  instead of position) would remove this limitation if ever needed — not attempted here, out of
  scope for this tier.

Verified via a standalone harness: a 2-column/3-row table, click the sortable header (asserts
`sortColumn`/`sortOrder`/actual lexicographic row order), click a body row (asserts
`UiSelection::state.selected`), drag row 0 down past row 2 (asserts `onReorder(0,2)` fired and the
`rows` vector actually reordered) — plus the full `ui_smoke_test_1..5` + headless
`ui_showcase`/`ui_minimal` run, all green.

**Phase 7 — PlotLines/PlotHistogram.** `Plot` (`components.hpp`) is another self-drawing leaf in
the `UiTable`/`ListBox`/`UiComboBox` family — no entity per data point. One unified component
with a `PlotKind{Lines,Histogram}` enum rather than two separate component types, since the
hover/render logic is ~95% shared (only the actual draw loop branches on `kind`).

**Retained-vs-immediate tension, worth remembering for any future ImGui-ported widget**: the
original `ImGui::PlotLines(label, values, count, ...)` takes a caller-owned pointer+count valid
only for that one immediate-mode draw call. `UiPlot::values` instead OWNS a `std::vector<float>`,
populated via `setValues(std::span<const float>)` (copies in) — a retained-mode ECS component has
to survive across frames on its own, so storing a `std::span` would risk pointing at a buffer the
caller already destroyed or reused by the next frame. This is the exact same "retained vs
immediate" gap that originally forced the Style Engine v2 rewrite (ImGui-style immediate styling
vs. a persistent per-entity style state) — a useful lens to re-apply whenever porting another
ImGui widget whose signature takes a raw buffer/span.

`autoScale` (bool, default `true`) recomputes `min`/`max` from `values` every render frame, rather
than the original API's NaN-sentinel-in-scale_min/scale_max convention — deliberately avoided to
sidestep NaN-comparison footguns in the auto-scale-or-not branch.

Verified via a standalone harness: spawn a `plotLines()` + a `plotHistogram()` from the same float
array, synthesize a mouse-motion event over the plot (asserts `hovered`/`hoveredIndex`), call
`setValues()` with a different array and assert `values.size()` updated — plus the full
`ui_smoke_test_1..5` + headless `ui_showcase`/`ui_minimal` run, all green.

**Phase 8 — Splitter + DatePicker.** `UiFactory::splitter()` is the FIRST widget to genuinely reuse
`UiResizeHandle` as a real spawned entity+component — Phases 2 and 6 only ever reused its DRAG
ALGORITHM (Table reimplements the math against its own internal column array; the resize-m_handle
entity+component form had never actually been exercised end-to-end). That surfaced a gap: despite
Phase 2 adding the `UiResizeHandle` component type and wiring its full press/drag/release lifecycle
into `InputSystem::dispatch()`, `WidgetBuilder` had never been wired for it at all — no
`resizeHandle` field, no `Spawn()` wiring, no way to attach one via the fluent builder. That
plumbing had to be added now, as part of Phase 8, not before — a reminder that "wired into
`InputSystem`" and "reachable from the builder DSL" are two separate completion bars, and a
primitive can sit half-finished between them for several phases without anything breaking (nothing
needed it yet).

`splitter()` eagerly spawns 4 entities in sequence — container, before-panel, m_handle, after-panel —
so the m_handle's `getBefore`/`setBefore` closures can capture the ALREADY-KNOWN before-panel
`Entity` directly. This is the simplest variant yet of the eager-spawn cross-reference pattern
(Phase 3's `inputNumber()`/`colorPicker()` needed `findByName` since NEITHER side existed yet at
construction time; Phase 5's `menu()`/`subMenu()` needed to return `Entity` instead of
`WidgetBuilder` because two SEPARATE popups needed cross-linking) — here there's exactly one
cross-reference, and the referenced entity is already spawned by the time the m_handle is built, so
straight closure-capture is enough, no lookup or entity-return needed.

`UiResizeHandle` previously had NO `RenderSystem` draw block at all — its own Phase 2 doc comment
explicitly says the primitive draws nothing (a pure interaction primitive, composed by whoever
needs a visible affordance). Splitter's m_handle needs to actually be visible and give hover/drag
feedback, so a small dedicated render block was added (tinted bar, brighter on hover/drag) — scoped
narrowly enough that it has zero effect on Table's column resizing (Phase 6), which doesn't use
this component at all.

`UiCalendar` continues the `UiTable`/`Plot` self-drawing-leaf philosophy for the day grid (no
entity per cell). Date math — `isLeapYear`/`daysInMonth`/`firstWeekdayOfMonth` (Zeller's
congruence)/`monthNameFr` — are free `constexpr` functions in `components.hpp`, general enough to
be reused outside the widget if ever needed elsewhere in the codebase. The (year, month, day)
selection callback is a direct `std::function` field on `UiCalendar` itself
(`UiCalendar::onDaySelected`) rather than routed through `UiCallbacks::onChange` — the same
"component-local callback" idiom already established by `UiCanvas::onDraw` back in the original
2026-07-05 widget batch, used whenever the generic single-`float` `onChange` signature can't carry
what the widget actually needs to report (here, three integers).

Verified via a standalone harness: build a splitter with two panels, locate the resize-m_handle
entity via `world.query`, assert its initial screen X position, synthetically drag it +60px and
assert the before-panel's `UiItem.width` actually grew (200px → 260px); separately, build and open
a `datePicker()` popup, compute the exact screen coordinates of a known date (10 August 2026) via
`firstWeekdayOfMonth`, click there, and assert `onDaySelected` fired with exactly `(2026, 8, 10)` —
plus the full `ui_smoke_test_1..5` + headless `ui_showcase`/`ui_minimal` run, all green.

**Phase 9 — `src/ui/chrome.hpp` (new file) — WindowChrome + PanelChrome.** First genuinely NEW file
added this session (every prior phase extended `components.hpp`/`systems.hpp`/`factory.hpp`).
Contains two chrome strategies that share only a name, not an implementation:

- `WindowChrome` drives the real OS-level `sdl3::Window::setHitTest` — a POD `HitTestCache` (title
  bar rect, excluded-button rects, resize border thickness, window size) rebuilt only when
  `LayoutSystem::passCount()` has actually changed since the last check (same dirty-flag-style
  guard as the layout system itself, no second mechanism, per the plan's own architecture
  decision). The hit-test callback registered with SDL never touches the ECS — it's a pure
  point-in-rect test against that cache, captured by pointer. Corners are tested before edges,
  excluded buttons (minimize/maximize/close) before both, so the title bar's buttons stay normally
  clickable despite sitting inside the draggable band.
- `PanelChrome` is pure ECS-level dragging, for a simulated-desktop use case (several floating
  panels inside ONE real SDL3 window, no OS involvement at all). Built on a brand-new primitive,
  `UiDraggable` (`interaction.hpp`) — same latch-then-follow drag lifecycle shape as
  `UiResizeHandle`/`UiReorderable` from Phases 2/4, but generalized with a `resizeMode` bool so ONE
  component type and ONE `InputSystem::dispatch()` block m_handle both "drag to move" (writes
  `UiRect.offset`) and "drag to resize" (writes `UiItem.width`/`height`) depending only on which
  entity it's attached to (a title bar vs. a corner grip) and that one flag.

`titleBar()`'s window-control buttons use plain Unicode text glyphs — em dash U+2014 (minimize),
white square U+25A1 (maximize), multiplication sign U+00D7 (close) — instead of the plan's literal
wording ("boutons ... en MaterialIcons"). Deliberate scope reduction, not an oversight: real icon
glyphs need `RenderSystem::registerFont` called first with the MaterialIcons font loaded, a
prerequisite `chrome.hpp` can't assume or enforce at title-bar-construction time, whereas plain
Unicode punctuation renders through whatever font is already loaded for every other label. The
buttons are ordinary `UiButton` entities, so an app that HAS registered MaterialIcons can freely
swap them for real icon buttons afterward — nothing about the chrome mechanism depends on the glyph
choice.

Verified via a standalone harness using a REAL `sdl3::Window` (`SDL_VIDEODRIVER=dummy`,
`Window::Create(..., window_flags::Borderless)`): built a `titleBar()`, attached `WindowChrome`,
confirmed the title bar's resolved screen rect covers a known drag point, clicked the close button
and asserted the `onClose` callback fired; separately built a bare floating panel + header entity
pair, called `PanelChrome::attach`, synthesized a header drag, and asserted the panel's
`UiComputed.screen.x` actually moved — plus the full `ui_smoke_test_1..5` + headless
`ui_showcase`/`ui_minimal` run, all green.

**Phase 10 (final) — example apps + `ui_smoke_test_6`.** Three example apps, matching the user's
plan-approval note ("3+ exemples thématiques", not one big gallery): `examples/ui_aero_basics.cpp`
(controls + value-editing + color picker — DragValue/inputNumber/ColorSwatch+ColorPicker from
Phase 3), `examples/ui_aero_lists.cpp` (Selectable/TreeNode/Table/PlotLines from Phases 4/6/7),
`examples/ui_aero_windows.cpp` (simulated desktop: `PanelChrome`-driven floating panels, a menu bar
with a real popup + a modal "About" dialog, a splitter, a date-picker popup — Phases 1/5/8/9 tied
together). All three: real borderless `sdl3::Window`, `UiTheme::aero()`, `WindowChrome` for the OS
title bar. The dashboard bonus app was skipped — three solid apps already exceed the plan's "3+"
floor and cover every phase at least once; a fourth would have been mostly repetition of widgets
already demonstrated.

**`UiTheme::aero()` didn't actually exist before this phase** — despite being referenced
throughout this file's own Phase 0 notes and several doc comments across `styles.hpp`/`factory.hpp`
as if already implemented, a `grep` turned up nothing: only the `glassDefault` bool field and the
`UiStyle::glass()/glassPanel()/glassButton()` presets it gates were real. Added now, following the
`dark()`/`light()` pattern (blue-tinted translucent palette, `glassDefault=true`).

**Two real bugs found and fixed while writing the FIRST example app** (`ui_aero_basics.cpp`,
before any of the others):
1. **A second, previously-undetected instance of the `StyleSystem::resolve()` iterator-
   invalidation hazard** (see the "Bug found and fixed" section near the top of this file for the
   first instance, back in Style Engine v2) — `resolve()`'s SECOND query (`world.Query<UiComputed>`,
   which finds root entities to kick off `resolveEntity()`) called `resolveEntity()` directly
   inside the query lambda, and `resolveEntity()` internally calls
   `get_or_add_component<UiComputedStyle>` for every entity in the resolved subtree — a structural
   ECS mutation that can reallocate the archetypes vector, run from INSIDE the very iteration over
   that vector. This crashed with a real, reproducible SIGSEGV (`Archetype::contains` on a null
   `this`) the first time `ui_aero_basics.cpp` ran — and never once before, across 9 phases and 5
   prior smoke tests, purely because none of those programs had spawned enough DISTINCT widget-type
   archetypes to force a reallocation at the unlucky moment. Fixed with the same collect-then-
   mutate shape used everywhere else this session: collect root entities into a `std::vector`
   during the query, call `resolveEntity()` on each in a plain loop afterward. **Lesson worth
   repeating one more time**: this hazard class doesn't announce itself with a failing test in a
   small scene — it's latent until a big enough example app happens to trip the exact archetype-
   count threshold, so "no crash in the smoke tests" was never actually proof this bug didn't
   exist.
2. **My own mistake, not a codebase bug**: spawned a `colorSwatch()` builder once via `.Spawn()` to
   get its `Entity` (to wire an `onClick` that opens the color-picker popup), then ALSO passed the
   same already-spawned builder into `.children(...)` — which spawns it a SECOND time, since
   `WidgetBuilder::Spawn()` has no "already spawned" guard. Fixed by giving the swatch a `.name()`
   instead and wiring its `onClick` via `findByName()` + direct `UiCallbacks` mutation AFTER the
   single real spawn — the same pattern Phase 3's `inputNumber()`/`colorPicker()` already use
   internally, applied here at the call-site level instead of inside a factory method.

`tests/ui_smoke_test_6.cpp` covers exactly the plan's own Vérification checklist: popup open +
outside-click close, modal open + Escape-close + background-blocking (own `InputSystem` instance
per sub-test, to keep each modal stack isolated), `applySelectionClick` ctrl/shift semantics
in isolation, splitter drag-resize, and table column drag-resize. Two assertion mistakes surfaced
and were fixed DURING writing, both genuinely instructive:
- The multi-select assertion first assumed a ctrl-click leaves the selection ANCHOR unchanged —
  wrong: `applySelectionClick` moves the anchor to the ctrl-clicked index too (Explorer-style), so
  a subsequent shift-click ranges from THAT index, not the original one. Corrected the test to
  match the real (correct, already-documented-since-Phase-2) semantics rather than an assumption.
- The table-column-resize click landed EXACTLY on a column boundary (`x = 100` for a 100px-wide
  column) — but the header hover-detection uses a half-open interval (`relX >= x && relX < x + w`),
  so `x = 100` falls into the NEXT column, not the one whose right edge is being dragged. Moved the
  test's click 3px inside the resize tolerance band instead of exactly on the mathematical boundary.

Final verification: all 6 `ui_smoke_test_N` binaries pass (`exit 0`), all 5 example binaries
(`ui_showcase`, `ui_minimal`, `ui_aero_basics`, `ui_aero_lists`, `ui_aero_windows`) run headless
under `SDL_VIDEODRIVER=dummy` for their full timeout with zero stderr output and no crash
(`exit 124` = killed by timeout while still running, the expected "healthy" outcome for a GUI
event loop with no real events to quit on).

---

## Status at end of the 2026-08-10 session — ImGui-coverage initiative complete

All 10 phases of `/home/clement/.claude/plans/mossy-forging-fog.md` plus the unplanned Style Engine
v2 rewrite are done and green. Summary of what exists now that didn't at the start of this session:
- **Style system**: full CSS-like cascade (named classes, inline overrides, 4 heritable properties,
  type-erased extensible property bag) replacing the old fixed-field/enum-mask style struct.
- **New widgets**: popups/modals, `UiDragValue`, `inputNumber()`, `ColorSwatch`/`ColorPicker`,
  `Selectable`/`TreeNode` (with drag-reorder), `MenuBar`/`Menu`/`MenuItem` (with submenus),
  `Table` (sortable/resizable/multi-select/row-reorder), `PlotLines`/`PlotHistogram`, `Splitter`,
  `DatePicker`, and two window-chrome strategies (`WindowChrome` for real OS borderless windows,
  `PanelChrome` for ECS-level floating panels).
- **New shared infrastructure**: `interaction.hpp` (`SelectionState`/`applySelectionClick`,
  `UiResizeHandle`, `UiReorderable`, `UiDraggable`, `nearestSelectionAncestor`), `chrome.hpp`.
- **6 headless smoke tests**, 5 example apps (2 pre-existing dark-theme references left
  intentionally unchanged in spirit, 3 new Aero-themed borderless apps).
- **Real bugs found and fixed along the way** (all documented above/in the relevant phase
  sections): the original `StyleSystem::resolve()` dirty-flag removal hazard, a SECOND instance of
  the same hazard class in the same function's root-resolution query, a `boolPropRegistry()`
  missing-return typo, and the free-function-must-be-declared-before-the-class-that-calls-it
  compile rule in Phase 5.

Nothing from the plan remains outstanding. Any further work on `ui::` (more widgets, real
MaterialIcons wiring in the title bar, a 4th "dashboard" example) is new scope, not a gap in this
plan's delivery.

---

## Post-plan addition (same day, 2026-08-10) — text formatting + ui_showcase "Dynamique" page

Follow-up request, outside the original plan: improve `ui_showcase.cpp` to exercise ALL the new
Phase 1–9 widgets together and specifically stress-test UI dynamism (live property changes, auto
show/hide scrollbars, resizing) plus add a fixed-width/auto-height multi-paragraph italic Lorem
Ipsum text block. The user explicitly redirected the text-block approach mid-task from a Canvas-
drawn escape hatch to "a widget container holding span widgets" — i.e. real ECS `UiLabel` entities,
one per word, not raw SDL drawing — which meant the style system needed genuine per-widget text-
formatting support that didn't exist yet.

**New style properties** (`styles.hpp`): `prop::Italic`, `prop::Bold`, `prop::Underline`,
`prop::Strikethrough`, `prop::Highlight` (all bool) plus finally WIRING `prop::HighlightTextColor`
— which had been declared since the original Style Engine v2 rewrite but never given a setter,
accessor, or registry entry, another "referenced everywhere, implemented nowhere" gap in the same
family as `UiTheme::aero()` from Phase 10. `RenderSystem` gained `registerItalicFont()`/
`registerBoldFont()`/`registerBoldItalicFont()` (three separate `sdl3::Font` object slots — SDL_ttf
only exposes `TTF_SetFontStyle` per-Font, not per-Text, so achieving per-WIDGET bold/italic without
making ALL text bold/italic requires distinct Font objects, not a style flag on the shared one) and
a `pickTextFont(bold, italic)` cascade (prefers the combined variant, falls back to bold-only, then
italic-only, then normal — never silently drops the text). Underline/strikethrough are drawn
manually (a filled rect under/through the measured text position) rather than via another Font
variant, specifically so they compose with ANY bold/italic combination without needing 2× more
registered Font objects. `sdl3::Text` gained `setWrapWidth()` (`ttf.hpp`) — a one-line addition
wrapping an existing untapped SDL_ttf entry point — though ultimately NOT used for the final
approach (see below); still useful groundwork if a future widget wants engine-level paragraph wrap.

**Word-wrapping without engine support**: `UiFlow` still has no "wrap" layout direction (a known
gap called out since the very first `project_ui_ecs_module.md` notes from 2026-07-05). Rather than
add wrap-in-flow support to `LayoutSystem` (real layout-engine surgery, out of scope for an example
improvement), `ui_showcase.cpp` computes the wrap ONCE at construction time — a local `wrapWords()`
helper measures each word via real `Font::measure()` and greedily fills lines up to a fixed pixel
width, producing a `.column()` of `.row()`s, each row holding one `UiLabel` "span" per word
(`.italic()` set via the new style property). The outer column is `Px` width / `Auto` height, so it
auto-sizes to however many lines the wrap produced — genuinely "extensible verticalement, fixe
horizontalement" as asked, just computed once rather than continuously re-flowed (acceptable since
the text never changes after construction).

**The "Dynamique" page** (6th scene in `ui_showcase.cpp`) ties three columns together: (1) a
`DragValue` live-editing a sample label's `fontSize` via `editInlineStyle`, a second `DragValue`
toggling `UiHidden` on a 20-item pool to directly exercise the auto-scrollbar show/hide mechanism,
and a `Splitter`; (2) a small `menuBar()`/`menu()`, `inputNumber()`, `colorSwatch()`+`colorPicker()`
popup, `Selectable`+`TreeNode`, a `Table`, and a `PlotLines` fed live data every frame (a sine
composite pushed into a rolling buffer via `setValues()` each tick — doubles as an animation-
dynamism demo); (3) the bold-title + italic-paragraph Lorem Ipsum span block plus one line
demonstrating underline/strikethrough/highlight directly. All popups/modal spawned for this page
are collected into the scene's own returned root vector (not just the page panel) so
`SceneManager::hide()` correctly hides them too when the user switches to a different tab —
otherwise an opened popup could be left floating over an unrelated page.

Verified via a modified headless copy of `ui_showcase.cpp` (switching straight to the new page
instead of page 0 at startup) with temporary diagnostic asserts: every by-name lookup resolved to a
valid entity (sample label, plot, all 20 pool items, both popups, the modal, the menu bar), and a
synthetic click on the color swatch correctly opened the color-picker popup — confirming the whole
wiring chain (hit-test → dispatch → `openPopup`) works in the actual page layout, not just in
isolation. Plus the full `ui_smoke_test_1..6` + all 5 example binaries (`ui_showcase`, `ui_minimal`,
`ui_aero_basics/lists/windows`) headless run, all green, zero stderr output.

---

## Post-plan fix (same day, 2026-08-10) — core ECS query performance (user-reported low FPS)

User reported low FPS running `ui_showcase`. Root cause found via measurement, not guessing:
`ecs::ArchetypeRegistry::Query<Comps...>()` (both overloads) did a full linear scan over EVERY
existing archetype on EVERY call, checking each one's type list for a match before rejecting it.
`InputSystem::dispatch()` — which fires for every discrete SDL event, including every single mouse-
motion event — now makes roughly 25-30 separate `Query<T,...>()` calls (one or more per widget type:
Button, Toggle, Slider, ComboBox, ..., plus everything added across Phases 1-9: Selectable,
TreeNode, MenuBarItem, MenuItem, Table, Calendar, Plot, DragValue, ColorSwatch, SVSquare,
HueSlider, AlphaSlider, ResizeHandle, Reorderable, Draggable). A quick instrumented count on the
new "Dynamique" page (added earlier the same day) found **251 distinct archetypes for only 265
total entities** — i.e. almost every entity has a UNIQUE component-type combination (expected,
given how many widget types now exist, each combined with a different subset of optional
components like `UiName`/`UiCallbacks`/`UiTooltip`), meaning the old linear scan was paying almost
the SAME cost as if it applied no filtering by type at all.

**Fix**: added `type_to_archetypes` (`std::unordered_map<std::type_index, std::vector<size_t>>`) to
`ArchetypeRegistry`, populated inside `get_or_create_archetype()` alongside the existing
`archetype_index`. Both `Query<Comps...>()` overloads (mutable/const) now pick the SMALLEST
candidate archetype list among the query's REAL (non-filter) component types and iterate only
those IDs — the existing per-archetype `contains()`/`Comps::matches()` checks are left completely
unchanged afterward, so the candidate list only needs to be a safe SUPERSET of the true
intersection (never a subset) for correctness to hold; it just means far fewer archetypes get
rejected for nothing. `entities_with<T>()` (single-type) now does a direct index lookup instead of
any scan at all. `With`/`Without`/`And`/`Or`/`All` query filters exist in the ECS but are UNUSED
anywhere in this repo currently (confirmed via grep) — the fallback path for an all-filter query
(no real component type to index by) still does a full scan, since there's nothing to narrow by,
but this path is never actually exercised today.

**Measured impact**: a synthetic burst of 2000 mouse-motion events run through
`InputSystem::handleEvent()` on the Dynamique page took **1122 µs/event before** the fix and
**22 µs/event after** — a **~51x speedup** — measured by temporarily reverting `ecs.hpp` to the old
linear-scan implementation in an isolated copy, rebuilding the SAME test harness against it, and
comparing. This lines up exactly with the reported symptom: active mouse movement generates many
motion events per frame, each one previously costing over 1ms just in `dispatch()`'s query
overhead alone, easily enough to tank FPS well below 30 on any page with a few dozen widgets.

**Two small permanent diagnostic additions** (`ecs.hpp`): `ArchetypeRegistry::archetypeCount()`/
`entityCount()` — used to produce the 251/265 measurement above, kept as a lightweight, permanent
profiling hook for any future performance investigation (this codebase apparently doesn't have a
`perf`/profiler-based tuning discipline yet, so exposing "how fragmented are the archetypes right
now" cheaply and directly is likely to keep being useful — worth reaching for FIRST the next time
something feels slow, before hypothesizing a fix).

`ecs::ArchetypeRegistry` has exactly ONE consumer in this repo (confirmed via grep of
`ecs::ArchetypeRegistry`/`#include "ecs/ecs.hpp"` across the whole tree): `src/ui/components.hpp`
(and transitively everything under `src/ui/`). So the existing `ui::` regression suite — all 6
`ui_smoke_test_N` + all 5 example binaries — is the COMPLETE correctness check for this change; no
other subsystem could have been affected. All green after the fix, byte-identical stdout (screen
coordinates, entity counts, test assertions) to before, confirming the index-based query visits the
exact same entities as the old full scan, just faster.

---

## New initiative (2026-08-10) — Node-Graph Editor

User requested a full, reusable node-graph editor component (nodes/pins/connections/waypoints,
pan/zoom, selection, an independent "inner-zoom" content scale, theming, serialization — modeled
on [thedmd/imgui-node-editor]) as a precursor to porting SDL3pp's `07_audio_patchbay.cpp`
(1399 lines, multithreaded audio DSP graph) into `examples/ui_aero_pathbay.cpp`. Given the combined
scope rivals the entire Phase 0-10 initiative above, the user chose (via explicit question) to
sequence node-graph first as its own fully-verified deliverable, with the audio patchbay port
planned SEPARATELY afterward — this file's job right now is ONLY the node-graph component.
Full architecture and phase breakdown: `/home/clement/.claude/plans/mossy-forging-fog.md` (plan
file reused/overwritten for this new chantier — the completed Aero-widgets plan is preserved
further down in that same file under "[Historique]").

**Phase 0 — layout scale foundation (`EmBases::scale` / `Dimension::resolve()` / `placeLinear()`).**
The trickiest, highest-risk piece of the whole node-graph plan, done FIRST and in isolation before
any node-graph component exists, specifically so it could be verified against the untouched
existing test suite (zero new components = zero confound if something broke). Added
`EmBases::scale` (4th field alongside em/pem/rem, default `1.f`), threaded through
`LayoutSystem::computeFonts()`'s existing per-entity DFS — composed MULTIPLICATIVELY down the tree
from a new heritable style property `prop::InnerZoom` (mirrors exactly how `FontSize` is threaded:
same DFS, same "own inline style overrides, else inherit parent's ALREADY-computed value" pattern,
same known limitation that `computeFonts()` only reads INLINE `UiStyle`, not the full class-based
cascade via `UiComputedStyle` — a class-based `InnerZoom` would resolve fine for `ResolvedStyle`
consumers but NOT feed into layout, exactly as already true for `FontSize`).

**Two categories of dimension needed genuinely different treatment, discovered by reasoning through
what "parent"/"root"/`fonts.em` already represent at each call site, not by trial and error:**
- `Dimension`-typed values (`Px`, `Em`/`Pem`/`Rem`) resolve through `Dimension::resolve()`, which
  now multiplies `(base + off)` by `fonts.scale` — but ONLY for the `Px` case and the `off` term.
  `Pct`/`Rpct` and `Em`/`Pem`/`Rem` are deliberately left UNMULTIPLIED: `Pct`'s `parent` argument
  and `Em`'s `fonts.em` are references into values that are ALREADY pre-scaled upstream (the
  parent's own resolved width; the pre-scaled effective font size from `computeFonts()`) — scaling
  them AGAIN here would double-count. `Rpct` resolves against `root` (the true window size,
  untouched by any ancestor's scale) and is INTENTIONALLY nesting-independent by its own existing
  semantics ("% of window"), so it stays unscaled on principle, not just to avoid double-counting.
- Raw `Sides`/`float` values that never go through `Dimension::resolve()` at all — `UiFlow.gap`
  (float) and `UiFlow.padding`/`UiItem.margin` (`Sides`) — needed EXPLICIT scaling at each of their
  THREE independent use sites: `LayoutSystem::Pass::place()` (padding, when computing a container's
  content box), `placeLinear()` (gap between siblings + each child's margin, when distributing main-
  axis space), and `autoSize()` (the SAME three, again, when computing a flow container's OWN
  intrinsic size from its children — a separate code path that duplicates the gap/padding math for
  the "how big should I auto-size to" question rather than the "where do I place my children"
  question). Missing any ONE of these three would have produced a subtly wrong composed size
  under nested inner-zoom (right position, wrong intrinsic size, or vice versa) — worth remembering
  as a checklist if this scale mechanism needs a 4th touch point later (e.g. if Phase 1's node
  header padding needs the same treatment).

**Deliberately scoped OUT of Phase 0, documented as a known limitation**: `LayoutSystem::intrinsic()`
— the ~20-branch function returning hardcoded pixel constants for leaf widgets with no explicit
`Dimension` sizing (`UiToggle{44,24}`, `UiCheckbox{20,20}`, `UiSlider{160,20}`, etc.) — was left
completely untouched. Widgets that measure TEXT (`UiLabel`/`UiButton`/`UiRadio`/`UiBadge`, via
`sys.measureText(text, fs)`) get correctly-scaled sizing for FREE since `fs` (from `fontsOf(e).em`)
is already pre-scaled by `computeFonts()` — but the FIXED PADDING CONSTANTS added alongside that
measurement (`{t.x + 24.f, t.y + 14.f}` for `UiButton`, etc.) do NOT scale, nor do the fully
hardcoded non-text intrinsic sizes. Rewriting all 20 branches correctly (distinguishing the
text-derived part, already scaled, from the literal-constant part, not) was judged too risky to do
blind in the SAME phase as the core mechanism — the practical mitigation is that node-graph content
is expected to use EXPLICIT `.size()`/`.w()`/`.h()` (which DOES scale correctly, being
`Dimension`-typed) rather than relying on intrinsic auto-sizing, matching how every example this
session already builds widget layouts. Revisit only if a real node-graph demo shows visibly
inconsistent chrome-vs-text scaling under inner-zoom.

Verified via a standalone 3-case harness BEFORE running anything node-graph-specific: (1) baseline
at the default scale (1.0 everywhere, since nothing sets `prop::InnerZoom` yet) reproduces the
EXACT pre-existing numbers (`child at (8,8) 50×30` with an 8px pad — matches `ui_smoke_test_1`'s own
long-standing baseline coordinates); (2) `innerZoom(2.0)` on a container doubles BOTH its padding
AND its child's own explicit `.size(50,30)` box (`(16,116) 100×60`); (3) a 0.5-scaled container
nested inside a 2.0-scaled one composes to exactly 1.0 (`(24,274) 50×30`, matching the
hand-computed padding chain `16 (outer, scaled) + 8 (inner, unscaled since composed=1.0) = 24`).
Then the full `ui_smoke_test_1..6` + all 5 example binaries, byte-identical stdout to the pre-Phase-0
baseline (same `e1 screen=66,66 268x18.2` etc. as printed by `ui_smoke_test_1`/`2` before ANY of
this session's node-graph work began) — genuine zero-regression confirmation, not just "didn't
crash."

**Phase 1 — Canvas + nodes: pan/zoom/drag/resize (`src/ui/nodegraph.hpp`, new file).** First
genuinely new file since `chrome.hpp` (Phase 9 of the prior initiative). Deliberately a fully
STANDALONE class (`NodeGraphSystem`) — own `handleEvent()`/`prepass()`/`RenderGrid()`/
`RenderHeaders()`, own small string-keyed `sdl3::Text` cache for titles (mirrors
`RenderSystem::strCache` but can't reuse it directly — no access to `RenderSystem`'s private
members from an unrelated class) — called EXPLICITLY by the application ALONGSIDE `gui.handleEvent()`/
`gui.render()`, not hooked into `InputSystem`/`RenderSystem` internals. The file's own header
comment documents the exact integration snippet (`prepass()` before `layout.runIfNeeded()`,
`handleEvent()` per SDL event alongside `gui.handleEvent()`, `RenderGrid()` before `gui.render()`,
`RenderHeaders()` after).

Nodes are real ECS entities (`.panel()`-like, `UiChildren` intact — can hold arbitrary user
widgets), positioned `.absolute()` in canvas space via `UiGraphNode.canvasPos/size`. `prepass()`
rewrites each node's `UiRect.offset`/`UiItem.width/height` to screen space
(`(canvasPos - pan) * zoom`, `size * zoom`) BEFORE each `layout.runIfNeeded()` — `LayoutSystem`
itself needed zero changes, it just sees a normal `.absolute()` child every frame. Drag-move
(header band) writes `canvasPos`; drag-resize (corner grip) writes `size`, both in canvas units
(divides screen-space mouse delta by `zoom`). Pan (middle-drag) writes `UiNodeGraphCanvas.pan`.
Wheel zoom multiplies by 1.1/notch, clamped to `[minZoom,maxZoom]`, and recomputes `pan` so the
point under the cursor stays fixed (`mouseCanvas = pan + (mouseScreen-origin)/oldZoom; newPan =
mouseCanvas - (mouseScreen-origin)/newZoom`).

**Real bug found via the functional harness, not by inspection**: `canvasBuilder()` used
`f.column()`, which carries `UiFlow{}`'s default 8px padding — since nodes are `.absolute()`
children positioned relative to the parent's CONTENT box (post-padding), every node's screen
position was silently offset by `(8,8)` from its intended `canvasPos`. Diagnosed by seeing
`initial node screen: 58,48` instead of the expected `50,40` in the test harness output, recognizing
`58-50=8`/`48-40=8` matched the default `Sides(8.f)` padding exactly. Fixed with an explicit
`canvas.pad(0.f)` in `canvasBuilder()`, commented — **any future `.absolute()`-child-positioning
helper built on `f.column()`/`f.row()` needs the same treatment** (zero the inherited padding
explicitly, or document that it's intentionally part of the coordinate origin).

Node headers reuse the established "reserve space via padding-top" idiom (`UiExpander`/`UiTabView`/
`UiTreeNode`): `addGraphNode()` pads the node's content box by `headerHeight+6` on top, so user
content never has to account for the header itself.

**Known, documented Phase-1 scoping limitation**: `RenderHeaders()` draws in one flat pass over
ALL nodes, separate from and after `gui.render()`'s own node-body pass — so an overlapping
background node's header can currently appear visually above a foreground node's body content.
Not fixed in Phase 1 (no z-order/selection concept yet — Phase 5 owns that); revisit once
selection/z-order lands.

`tests/ui_smoke_test_7.cpp` is now the real, canonical, auto-globbed test file for this initiative
(covers initial transform identity, header drag, corner-grip resize, middle-drag pan, wheel zoom
toward cursor) — confirmed building and passing via `make build/tests/ui_smoke_test_7`, matching
the plan's stated intent that it "grandit au fil des phases (2 à 9)" rather than being written in
one block at the end. Full regression (`ui_smoke_test_1..7` all exit 0, all 5 examples headless
exit 124, `ui_smoke_test_1`'s printed screen coordinates byte-identical to the pre-Phase-0/1
baseline) confirmed green.

**Phase 2 — Pins (`UiGraphPin`, still `src/ui/nodegraph.hpp`).** `PinShape{Circle,Triangle,Square,
Star}` + `PinSide{Left,Right,Top,Bottom}` enums; `UiGraphPin{node, shape, color, size, side,
sideOffset, connectable, connected, selected, hovered, onCustomDraw}`.

**Key design departure from nodes (deliberate, not an oversight)**: pins are ECS entities but carry
NO `UiParent`/`UiRect`/`UiItem` at all — they never enter `LayoutSystem`'s tree (which wouldn't know
what to do with them anyway, lacking `UiRect`). `addGraphPin()` instead `SpawnBundle(pin,
UiComputed{})` directly, and a new `NodeGraphSystem::updatePins(world)` (called AFTER
`layout.runIfNeeded()`, not before like `prepass()` — needs the node's ALREADY-resolved
`UiComputed.screen`) recomputes each pin's `UiComputed.screen` every frame straight from its owning
node's screen rect + `side`/`sideOffset` (normalized [0,1] along that edge, so it stays correct
across node resizes) + the owning canvas's `zoom` (pin `size` is canvas-space, like node size, scaled
at read time — visually consistent with the rest of the canvas rather than a fixed on-screen pixel
size). This is a DIFFERENT flavor of the same "editor computes position outside LayoutSystem" idiom
`prepass()`/`RenderHeaders()` already established in Phase 1, one step further: not just skipping
layout's PLACEMENT algorithm (nodes still do, via `.absolute()`), but skipping the layout TREE
entirely.

**Deliberately pre-empted a repeat of the session's most common bug class**: baking `UiComputed{}`
into `addGraphPin()`'s `SpawnBundle()` at construction time — rather than having `updatePins()`
lazily `add_component` it on first sight — means `updatePins()` only ever MUTATES fields on an
already-matching `Query<UiGraphPin, UiComputed>()`, never triggers an archetype migration from
inside that same query's iteration. This is the exact iterator-invalidation hazard that has bitten
this module in `StyleSystem::resolve()` (twice), the Popup/menu toggle code, and elsewhere — caught
here by construction instead of by a crash, simply by choosing to spawn both components together up
front.

**Hit-testing**: `hitTestPin(world, x, y) -> Entity` is a small standalone query, reused three ways
in Phase 2 itself — hover tracking on every mouse-motion event (skipped while a node-drag or pan is
already in progress, mirroring how hover conventionally freezes mid-gesture elsewhere in this
module), a `connectable`-gate check (non-connectable pins are invisible to hit-testing, forward
compatibility for Phase 3's connection rules), and a guard placed FIRST in the mouse-down handler
so clicking a pin can never accidentally start a node header-drag even where a pin's box visually
overlaps the header band — the guard just early-returns for now (real pin-drag-to-connect logic is
Phase 3's job), but the collision is prevented starting now rather than retrofitted later.

**Default rendering**: circle/square/triangle/star, hollow outline when `!connected` vs. filled when
`connected` (the standard node-editor convention for "nothing plugged in here yet"), hover
brightens the color (+45 per channel, same `lighten` lambda idiom as `styles.hpp`'s existing glass
gradient code), `selected` adds a gold ring (same `{255,200,60,255}` used for node selection in
Phase 1, for visual consistency). Star points computed by a small local `detail::starPoints()`
helper (alternating outer/inner radius around a center, 5 points by default) — no existing star
primitive in `render.hpp` to reuse, unlike circle (`fillCircle`/`drawCircle`) and triangle/square
(`fillPolygon`/`drawPolygon`/`FillRect`/`drawRect`, all pre-existing). `onCustomDraw` (same idiom as
`UiCanvas::onDraw`/`UiCalendar::onDaySelected`) fully replaces default rendering when set.
`RenderPins()` is called AFTER `RenderHeaders()` (deliberately, per the integration snippet in the
file's header comment) and does NOT clip to the node's box — pins are meant to visually straddle the
node's edge, unlike header/body content which stay clipped inside it.

Extended `tests/ui_smoke_test_7.cpp` in place (per the plan's "grandit au fil des phases" intent,
not a new file): spawns two pins on the node left over from Phase 1's tests (whose canvas is
mid-pan/zoom by this point — `pan=(50,30)`, `zoom=1.1`), and checks pin screen position against an
INDEPENDENTLY re-derived expected box (computed in the test from the node's own `UiComputed.screen`
read back, not by calling into `NodeGraphSystem` a second time) rather than a hand-computed absolute
number — deliberately avoids a tautological test while still exercising the real side/offset/zoom
formula. Also covers hover on/off, the `connectable=false` hit-test exclusion, and the
"clicking a pin doesn't start a node drag" guard. Full regression (`ui_smoke_test_1..7` all exit 0,
all 5 examples headless exit 124, zero stderr) confirmed green.

**Phase 3 — Connections (`GraphConnection`, still `src/ui/nodegraph.hpp`).** `ConnectionStyle{Bezier,
Straight,Orthogonal}` + `ConnectionEndCap{None,Arrow}` enums; `GraphConnection{fromPin, toPin, color,
thickness, opacity, style, startCap, endCap, arrowSize, bezierControlOffset, waypoints,
onCustomDraw}` stored as `std::vector<GraphConnection>` directly on `UiNodeGraphCanvas::connections`
— PURE data, no entity per connection (per Décision #1: a few-hundred-connection graph shouldn't
need a few hundred more entities, same reasoning already applied to `UiTable.rows`/`Plot.values`).
`waypoints` exists as a field now (empty, unused) purely so the struct's SHAPE doesn't change again
in Phase 4 — the routing/rendering code stays 2-endpoint-only until Phase 4 explicitly generalizes
it, matching the plan's own phase split (deliberately not solving waypoint routing early).

**`GraphConnection`/`ConnectionStyle`/enums had to be declared BEFORE `UiNodeGraphCanvas`**, one
level higher than originally organized — because `UiNodeGraphCanvas::connections` is a
`std::vector<GraphConnection>` MEMBER (not a pointer/reference), C++ requires the complete type
already visible at that point. Caught immediately while writing the edit (moved the definitions up
front) rather than by a compile error — but same family of ordering constraint as the "free function
declared before the class that calls it" C++ rule that bit Phase 5's `openPopup`/`closePopup`; worth
scanning for on any future struct-embeds-another-new-type addition in this file.

**Routing (`detail::connectionPath(a, b, style, controlOffsetCanvas, zoom) -> vector<FPoint>`,
screen-space)**: `Straight` = the 2 endpoints; `Orthogonal` = a fixed 3-segment "elbow" through the
horizontal midpoint (`{a, {midX,a.y}, {midX,b.y}, b}` — the simplest standard node-editor routing,
not obstacle-aware); `Bezier` reuses the existing horizontal-tangent 2-control-point shape (control
offset auto-proportional to `|dx|`, clamped `[30,150]*zoom`, or an explicit
`bezierControlOffset*zoom` override), but does NOT reuse `sdl3::Renderer::drawBezier` — that helper
only DRAWS a thin 1px polyline, it doesn't hand back the sampled points, and a THICK ribbon needs the
actual point list to build quads from. Wrote a small `detail::sampleCubicBezier()` (24 fixed
segments, de Casteljau, degree-3-specialized rather than `drawBezier`'s arbitrary-degree loop) purely
to get points out.

**Thickness — Décision #6 ("un petit quad, pas plusieurs lignes parallèles") implemented as a real
thick-ribbon renderer**, new this phase since nothing in `render.hpp` already did this: `appendThickSegment()`
builds one quad (2 triangles) per path segment, offset perpendicular to the local segment direction
by half the on-screen thickness (`conn.thickness * zoom / 2`); consecutive segments (Bezier's 24
samples, Orthogonal's 3 segments) get a small filled circle at each internal joint to hide the seams
a plain quad chain leaves at any bend — same "why not just N `drawLine` calls" reasoning as Décision
#6 already anticipated. Arrow caps (`drawArrowHead()`) are a filled triangle oriented off the LOCAL
tangent at that end of the path (last two points for `endCap`, first two for `startCap`), sized in
canvas units and scaled by zoom like everything else connection-related.

**Drag-to-connect + rubber band**: a THIRD mutually-exclusive drag mode alongside Phase 1's
`dragNode`/`panCanvas`, added as `Entity connectFromPin`. The pin-hit guard already added in
Phase 2 (which used to just `return` to block an accidental node-drag) now actually DOES something —
sets `connectFromPin` and starts the gesture. On release, `hitTestPin()` at the cursor decides
create-or-cancel via `connectPins()`; a miss (release over empty canvas) silently cancels, no error
path needed since `connectPins()` is already a validated no-op on a bad target. The temporary
"elastic" line during the drag needed NO new state to track the moving endpoint — `mouseX/mouseY`
(already updated every event since Phase 1) IS the live cursor position, so `RenderConnections()`
just reads those two members directly when `connectFromPin.valid()`, drawn with the same
`drawDefaultConnection()` path used for real connections (translucent white, no fixed style —
always rendered as Bezier for a consistent "not yet committed" look regardless of what style newly
created connections will default to).

**`connectPins()`/`disconnectPins()`/`canvasOfPin()` are free functions, not `NodeGraphSystem`
methods** — deliberate, matching how `addGraphNode()`/`addGraphPin()` are already free functions:
they only mutate plain data (`UiNodeGraphCanvas::connections`, `UiGraphPin::connected`), no
interaction/drag STATE involved, so there's no reason to route them through the stateful system
object. `connectPins()` validates in one pass — invalid/self entities, non-`connectable` pins,
pins belonging to DIFFERENT canvases (rejected, not silently cross-wired), and duplicate connections
(either direction) — and returns a `GraphConnection*` into the just-grown vector so the caller can
immediately customize style/color/thickness/caps without a second lookup. `disconnectPins()` only
clears a pin's `connected` flag if NO connection references it anymore (a pin can have several).

Extended `tests/ui_smoke_test_7.cpp` again in place: a second node (`nodeB`) + third pin (`pinC`) so
connections span two distinct nodes; covers `connectPins()` success + both duplicate-rejection
directions + self-connection rejection, `disconnectPins()` clearing both the vector entry and both
pins' `connected` flags, all 3 routing styles checked against hand-computed geometry via DIRECT calls
to `ui::detail::connectionPath()` (exposed since `detail` is just a nested namespace, not hidden —
convenient for testing the math independently of any rendering), a full drag-to-connect gesture via
synthetic mouse down/move/up ending on the target pin (asserts the resulting connection's
`fromPin`/`toPin` and both pins' `connected` state), and a drag-to-empty-space gesture asserting NO
connection is created. Full regression (`ui_smoke_test_1..7` all exit 0 with byte-identical
baseline numbers, all 5 examples headless exit 124, zero stderr) confirmed green.

**Phase 4 — Waypoints (still `src/ui/nodegraph.hpp`).** Generalized `detail::connectionPath()`
(Phase 3's 2-point version renamed to `detail::segmentPath()`) to route through an ordered
`std::span<const FPoint>` of already-screen-space through-points: builds `[a, through..., b]`,
calls `segmentPath()` on each consecutive pair, and concatenates while popping the duplicate
joint point between chained segments. A no-through-points overload (`connectionPath(a, b, style,
offset, zoom)`) is kept as a thin forward so Phase 3's call sites and tests didn't need touching.
`GraphConnection::waypoints` (declared in Phase 3, unused until now) is CANVAS-space, converted to
screen at render/hit-test time via two new small free functions, `canvasToScreen()`/`screenToCanvas()`
(exact inverses) — same `(pt - pan) * zoom` formula `prepass()` already uses for nodes, but applied
manually here since waypoints never touch `LayoutSystem` at all (no `UiRect`, no entity).

**`insertWaypoint(conn, index, canvasPoint)`/`removeWaypoint(conn, index)` are trivial free functions
taking a `GraphConnection&` directly** — deliberately NOT threaded through `NodeGraphSystem`, matching
the "connections are pure data, mutated via free functions" precedent from Phase 3's
`connectPins()`/`disconnectPins()`. This is also the full "API programmatique" surface the spec
asks for (section 6) — the mouse-driven paths below are just two more callers of the same two
functions, not a separate code path.

**Waypoint HIT-TESTING deliberately uses the CONTROL POLYGON, not the rendered curve**: to decide
"which segment did a double-click land near" (for insertion) or "is the cursor on an existing
waypoint m_handle", the code tests proximity against straight lines between `[pin, waypoints...,
pin]` directly — exact for Straight/Orthogonal, an approximation for Bezier (good enough for
reasonable control-offset values, avoids re-deriving segment boundaries from `connectionPath()`'s
already-sampled/concatenated output, which has no boundary markers between conceptual segments once
flattened into one point list). New `detail::distanceToSegment()` (perpendicular distance, clamped
to the segment) is the one new geometry primitive this needed.

**A third mutually-exclusive drag mode, `dragWaypointCanvas`/`dragWaypointConn`/
`dragWaypointIndex`** (index-based, not `Entity`-based like node/pin drags — waypoints have no
entity to reference). Documented as valid only within one continuous gesture, since nothing else in
this synchronous single-threaded event model can mutate the connections/waypoints vectors mid-drag
to invalidate the indices. Unlike node dragging, the waypoint-drag motion handler does NOT call
`layout.markDirty()` — waypoints never feed `LayoutSystem`, so there's nothing to invalidate;
"real-time recompute" (the phase's explicit requirement) falls out for free because
`RenderConnections()` already recomputes the full path from `conn.waypoints` fresh every single
frame, no caching to invalidate in the first place.

**Mouse-down priority order, extended**: pin hit (unchanged, starts connect-drag) → existing
waypoint HANDLE hit (double-click removes, single-click starts the drag) → double-click on a
connection's PATH with no m_handle underneath (inserts a new waypoint at the click point, converted
back to canvas-space via `screenToCanvas()`) → the pre-existing resize-grip/header checks. Reused
SDL's native per-event `.clicks` field on `SDL_MouseButtonEvent` for double-click detection — the
exact same technique the ORIGINAL (Part A) session's `UiDragValue` already established for its
tap-to-edit gesture, so no new double-click-timing machinery was needed here either.

Visual handles (`drawWaypointHandle()`): small filled+outlined squares, larger and gold-tinted while
actively being dragged (looked up during `RenderConnections()`'s per-connection loop via a
canvas+index match against the drag-state members, same "compare against system state" idiom
`RenderHeaders()` already uses for `draggingResize`'s grip-brightening).

Extended `tests/ui_smoke_test_7.cpp` a third time in place: `connectionPath()` with one through-point
(Straight and Orthogonal, hand-verified point counts/positions), a double-click on the pinLeft→pinC
connection's midpoint (asserts a waypoint was inserted at very close to the clicked screen position,
round-tripped through `canvasToScreen()`), a drag of that waypoint's m_handle (asserts the new position
matches the mouse delta), a double-click on the m_handle (asserts removal), and direct
`insertWaypoint()`/`removeWaypoint()` calls including an out-of-bounds `removeWaypoint()` (asserts
it's a safe no-op, not a crash). Full regression (`ui_smoke_test_1..7` all exit 0 with byte-identical
baseline numbers, all 5 examples headless exit 124, zero stderr) confirmed green — first try, no
debugging detour this phase.

**Phase 5 — Selection & group move (still `src/ui/nodegraph.hpp`).** `NodeSelectionState{
unordered_set<Entity> selected; Entity anchor; Entity lastClicked; }` + `applyNodeSelectionClick()` —
the Entity-keyed sibling of `interaction.hpp`'s original `SelectionState`/`applySelectionClick`,
per the plan's own Décision #4 (nodes are identified by `Entity`, not list position). Same
Explorer/Finder semantics (plain=replace, ctrl=toggle-and-move-anchor, shift=range-from-anchor) —
but "range" needed a stand-in for the `int` index the original relies on, since a node has no
natural position on a free 2D canvas. Resolved by ranging over the CANVAS's own `UiChildren` order
(creation order, passed in by the caller as `order`) rather than spatial position — deterministic,
testable, and already-available data (no new bookkeeping needed). `selection` lives as a plain
field on `UiNodeGraphCanvas` (not a separate optional component like `interaction.hpp`'s
`UiSelection`) — matches the plan's own listing of "sélection" alongside zoom/pan/connections as
canvas-level state (spec section 10, persistence), and skips a `GetComponent`/`add_component` step
on every selection touch since the canvas is already looked up for pan/zoom anyway.

**The UX subtlety that actually mattered this phase: clicking an ALREADY-selected node (no
modifier) must NOT collapse a multi-selection before a drag starts** — otherwise every group-move
gesture would begin by silently discarding the group down to one node. Implemented as a guard in the
header mouse-down handler: `applyNodeSelectionClick()` only runs when `ctrl || shift ||
!alreadySelected`; a plain click on a node that's part of an existing selection leaves the selection
untouched and goes straight to starting the drag. Ctrl/shift clicks, conversely, are PURE
selection-toggle gestures — they update `NodeSelectionState` but deliberately do NOT start a drag
(`if (!ctrlDown && !shiftDown) { start drag }`), matching standard node-editor / file-manager
convention (Ctrl-click to multi-pick, plain-click-and-drag to move).

**Group move implementation**: a new `groupDragStart` (`vector<pair<Entity,FPoint>>`) captured at
drag-start from the FULL current selection (not just the grabbed "leader" node) — the motion handler
then applies the SAME screen-delta/zoom to each entry from ITS OWN captured start position, rather
than deriving followers from the leader's delta (equivalent result, but avoids any relative-offset
math). A single selected node is just a group of one — no special-cased single-node path, the old
Phase-1 per-node update was fully replaced rather than kept as a fast path. Resize deliberately stays
single-node-only (`groupDragStart` is only populated/consulted in `DragMode::Move`) — group-resize
was never asked for and would need a much less obvious semantic (resize proportionally? from which
corner?).

**Marquee** (Décision #7): a fourth mutually-exclusive drag mode, `marqueeCanvas`/`marqueeStart`
— starts on a Left-press that lands on canvas empty space (hits nothing above it in mouse-down
priority: pin, waypoint m_handle, connection double-click-add, resize grip, header). No ECS mutation
during the drag itself (the rectangle is derived live from `marqueeStart` + current mouse for
`RenderMarquee()`); the actual selection is computed ONCE at release via `FRect::intersects()`
against every node's `UiComputed.screen` in that canvas. Ctrl-held-at-press (`marqueeAdditive`)
extends the existing selection instead of replacing it — captured at PRESS time, not release, so
releasing over a stray Ctrl-up doesn't change the gesture's own semantics mid-drag.
**Gotcha hit while writing the test**: the marquee's Start point must itself land inside the
canvas's own screen rect (that's how `marqueeCanvas` gets set at all) — a naive "bounding box of
the two target nodes minus a margin" can compute a start corner that's OUTSIDE the canvas viewport
if a node sits near canvas edge 0, producing a silent no-op (no assertion failure at the point of
misuse, just a later assertion failing on the resulting empty selection). Fixed in the TEST (not
the library) by clamping the press corner into the canvas rect; documented as a real trap for any
future marquee-driving code, not just this test.

**`UiGraphNode::selected` (existed since Phase 1, previously never driven by anything real) is now
actually wired**: a single private `SyncSelectionVisuals()` is the ONE place that copies
`NodeSelectionState::selected` membership onto every node's own `.selected` bool (feeding
`RenderHeaders()`'s gold selection ring) AND fires `NodeGraphCallbacks::onSelectionChanged` — called
from every selection-mutating path (header click, marquee release, and all of the new public API
methods) so the two side effects can never drift out of sync between callers.

**Public programmatic API** (spec section 6/9: "sélectionner/désélectionner par Entity, créer/
détruire un nœud"): `selectNode()`/`deselectNode()`/`clearSelection()`/`selectAllNodes()`/
`isNodeSelected()` on `NodeGraphSystem` (need `this` for nothing but symmetry with the mouse-driven
paths — kept as methods rather than free functions, unlike Phase 3/4's connection/waypoint helpers,
specifically so they can all funnel through the same private `SyncSelectionVisuals()`). Node
CREATION was already `addGraphNode()` (Phase 1); node DESTRUCTION is new this phase —
`destroyGraphNode()` (free function, no interaction state needed): despawns the node's own widget
subtree via `despawnTree()` (existing `components.hpp` helper), finds and despawns its pins (no
`UiParent` link for pins, so a collect-then-despawn `Query<UiGraphPin>` filtered by `.node ==
target`, same collect-before-structural-mutation discipline as everywhere else in this file), removes
any connection referencing those pins, and clears the destroyed node from the canvas's selection.

**Real bug found while writing the destroyGraphNode test, not caught by inspection**: the first
version erased matching `GraphConnection` entries but never re-checked the SURVIVING pin at the
other end of each removed connection — so destroying a node left its neighbor's pin permanently
stuck with `connected = true` even though its only connection was gone. Exactly the same class of
omission `disconnectPins()` (Phase 3) already had to guard against (a pin can have several
connections, so `connected` can only drop to false once NONE remain) — `destroyGraphNode()` just
hadn't reused that check. Fixed by collecting the "other side" pin of every connection being removed
BEFORE erasing, then re-scanning the (now-shorter) connections list per affected pin afterward — same
two-phase shape as everywhere else, applied to a case that had been missed the first time round.

`NodeGraphCallbacks` (optional component, attached only if the app wants hooks — mirrors the
project's existing `UiCallbacks` convention of a separate component rather than fields embedded in
the "main" component so the no-callback path pays zero cost): `onSelectionChanged` (fires from
`SyncSelectionVisuals()`), `onNodeMoved`/`onNodeResized` (fire once per affected node at drag-release,
looked up via the SAME `canvasOfNode()` used elsewhere — a new tiny sibling to the pre-existing
`canvasOfPin()`).

**Live keyboard-modifier state, reused from the ORIGINAL 2026-08-10 session (not new machinery)**:
ctrl/shift for node selection clicks reads `sdl3::keyboard::mods()` (`SDL_GetModState()`), the exact
same technique `UiSelectable`/`UiTreeNode` already established back in that session's Phase 4 (a
`SDL_MouseButtonEvent` carries no modifier field of its own). New for THIS phase: the test needed to
actually simulate Ctrl being held for a synthetic click, which isn't expressible on the synthetic
`sdl3::Event` itself — `sdl3::keyboard::setMods()` (a thin `SDL_SetModState()` wrapper that already
existed in `input.hpp` but had never been exercised by a test before) does work correctly even under
`SDL_VIDEODRIVER=dummy` (confirmed via a standalone one-off check before trusting it in the real
test) — worth remembering as the way to drive modifier-dependent code from a headless test.

Extended `tests/ui_smoke_test_7.cpp` a fourth time in place, reusing `nodeE`/`nodeB` (and their
existing pinLeft/pinC connection) from earlier phases: `applyNodeSelectionClick()` exercised directly
(plain/ctrl/shift, including the "ctrl-click moves the anchor too" semantic that had already tripped
up a Phase-10-era test once before — this time the TEST was written correctly the first time by
deliberately checking `state.anchor` after the ctrl step before writing the shift-range assertion);
the full public API; `NodeGraphCallbacks` firing; a real click-driven ctrl-select via `handleEvent()`
+ `setMods()`; a full group-move drag asserting BOTH nodes move by the identical canvas delta; a
marquee covering both nodes and a second marquee over empty space (asserts selection clears); and
`destroyGraphNode()`'s cascade (a disposable third node/pin connected to the previously-idle
`pinRight`, destroyed, asserting the node/pin are gone, the connection count dropped, and
`pinRight.connected` correctly reverted to false). Full regression (`ui_smoke_test_1..7` all exit 0
with byte-identical baseline numbers, all 5 examples headless exit 124, zero stderr) confirmed green
after two real bugs fixed (the marquee test's out-of-canvas start corner, and destroyGraphNode's
missing other-side-pin recheck) — both caught by the functional harness, not by inspection, again
underscoring why this module keeps a real headless test per phase rather than trusting review alone.

**Phase 6 — Auto-scroll, context menus, cut/copy/paste/delete (still `src/ui/nodegraph.hpp`).**
Three loosely-related interaction features, all scoped tightly per Décision #7 ("réutilisation
maximale de l'existant").

**Auto-scroll needed a genuinely NEW kind of update this file didn't have yet**: every prior
interaction (drag, pan, zoom, connect) is purely event-driven, but auto-scroll must keep scrolling
even while the mouse sits PERFECTLY STILL at the canvas edge — event-driven alone can't do that.
Added `NodeGraphSystem::tick(world, layout, dt)`, mirroring `InputSystem::tick(dt)`'s existing
"continuous, event-independent" role from the very first 2026-07-05 session, called once per frame
regardless of events. Only active while `dragNode` is in `DragMode::Move`; checks the CURRENT
mouse position (already tracked via `mouseX/Y`) against the dragged node's canvas screen-rect
edges (24px threshold, 480 screen-px/sec).

**The actual math took real reasoning to get right, not just "nudge pan"**: shifting `cv.pan` alone
while leaving the dragged node's `canvasPos` untouched would make the node visually SLIDE BACKWARD
relative to the (stationary) cursor as the view scrolled under it — wrong feel. The correct
behavior (verified against how auto-scroll drags feel in every mainstream editor) is for the
dragged node to stay visually PINNED near the cursor while both the view and the node's true
canvas-space position advance together — which falls out of `screenPos = (canvasPos − pan) × zoom`
by adding the IDENTICAL `canvasDelta` to both `pan` and every dragged node's position each tick.
Because the Phase-1 motion handler recomputes `canvasPos` fresh from `startPos + (mouse −
dragStartMouse)/zoom` on every actual mouse-motion event (absolute, not incremental), a naive
one-shot nudge to `canvasPos` alone would just get overwritten by the next real motion event —
so `tick()` bumps the STORED `groupDragStart` start-position entries (not just the live
`canvasPos`), keeping the reference frame itself consistent for whichever update (this tick, or the
next motion event) touches it next. `dragStartMouse` (the real captured OS cursor position) is
deliberately never touched — only the canvas-space side of the equation moves.

**Context menus: the editor detects, the user builds.** Given the plan's own editor/user split
(spec section 9) and that `NodeGraphSystem` never holds a `UiFactory&` (it only ever receives
`ArchetypeRegistry&`, by design, so it can't spawn widget-content), building an actual popup here
was never going to be in scope for the EDITOR side. Implemented as a new `NodeGraphCallbacks::
onContextMenu(Entity node, Entity pin, FPoint screenPos)` hook — `handleEvent()` detects a right
mouse-down, hit-tests which node/pin (if any) sits under the cursor, and just forwards the context;
the actual `UiFactory::popup()/menu()` construction is entirely the application's job (Phase 9's
demo will exercise this for real). A small, deliberate scope boundary, not a placeholder.

**Cut/copy/paste/delete: an internal clipboard, NOT yet built on `data::Properties`.** The plan's
Décision #7 says clipboard and full persistence should share ONE serialization mechanism — but
persistence is explicitly Phase 8's job, later in the plan's own ordering, so building the REAL
`data::Properties` plumbing now would mean redoing it (or worse, half-duplicating it) once Phase 8
lands. Resolved by introducing the shape now, reusable later: `ClipboardNode`/`ClipboardPin` (plain
structs, C++-native, no `data::` dependency yet) capture everything the EDITOR owns about a node —
chrome (title/size/header color/resize-move-constraints) and pins — explicitly EXCLUDING the node's
CONTENT (arbitrary user-supplied child widgets), since the editor has no generic way to clone
widgets it didn't create. A pasted node therefore gets an empty body — an honest, documented
limitation flowing directly from the same editor/user boundary as the context-menu decision above,
not an oversight. `copySelection()`/`cutSelection()`/`deleteSelection()` (free of any `UiFactory`
dependency, since they only read/destroy) are wired into `handleEvent()`'s new keyboard branch
(Delete/Backspace, Ctrl+C, Ctrl+X, checked across every canvas with a non-empty selection — apps
only ever run one canvas at a time in practice, so no "focused canvas" tracking was needed).
`pasteClipboard(UiFactory&, canvasEntity, atCanvasPos)`, by contrast, genuinely needs a `UiFactory&`
to spawn real entities — so Ctrl+V is deliberately NOT auto-wired inside `handleEvent()` (which only
gets an `ArchetypeRegistry&`); the file's own integration-comment now shows the one-line pattern the
application adds itself (`if (ctrl+V) nodeGraph.pasteClipboard(f, canvasE, cursorCanvasPos)`) — the
same "handleEvent can't do everything, expose the rest as a method" shape already used for
`selectNode()` etc. in Phase 5. Pasted nodes preserve RELATIVE positions between each other (offset
from the first clipboard node), translated so the first node lands at the caller-supplied paste
point.

Extended `tests/ui_smoke_test_7.cpp` a fifth time in place: `onContextMenu` fired via a synthetic
right-click on a node header (asserts the hit node) and on empty canvas (asserts an invalid node);
auto-scroll — `tick()` confirmed to be a true no-op with no active drag, then a real header-drag held
near the canvas's left screen edge, ticked 5×0.1s, asserting `pan.x` decreased AND the dragged node's
`canvasPos.x` moved by the EXACT same delta as `pan.x`; `Ctrl+C`/paste/`Delete`-key/`Ctrl+X` exercised
end-to-end through `handleEvent()`'s new keyboard branch (paste checked for correct title/size/pin
count/position, delete checked via the synthetic `SDLK_DELETE` key event, cut checked to both destroy
the node AND correctly clear its pin's connection back on `pinLeft`, reusing `disconnectPins()`'s
own "only clear `connected` once truly disconnected" rule that Phase 3 established). A new
`keyDownEv()` test helper (`SDL_EVENT_KEY_DOWN` + `.mod`) was needed since none of the prior 6 phases
had exercised keyboard input through `NodeGraphSystem::handleEvent()` before. Full regression
(`ui_smoke_test_1..7` all exit 0 with byte-identical baseline numbers, all 5 examples headless exit
124, zero stderr) confirmed green — first try this phase, no debugging detour.

**Phase 7 — inner-zoom wired into the node-graph API + Blueprint-inspired default theme (still
`src/ui/nodegraph.hpp`).** Two independent pieces, both intentionally small — Phase 0 (the ORIGINAL
2026-08-10-era work on this initiative) already built the generic `EmBases::scale`/`prop::InnerZoom`
mechanism; this phase's job was exposing it through the node-graph's own vocabulary and giving the
editor sensible-looking defaults, not re-deriving the mechanism itself.

**Inner-zoom realization, worth remembering**: it took actually reasoning through
`Dimension::resolve()`/`computeFonts()` again to confirm inner-zoom is DELIBERATELY independent of
canvas zoom, not compounded with it — `EmBases::scale` for a node's content subtree is driven
PURELY by `prop::InnerZoom`'s own cascade, with zero awareness of `UiNodeGraphCanvas::zoom` (a
completely separate transform applied via `prepass()`'s literal pixel rewrite). This matches the
spec's own wording ("échelle du contenu... INDÉPENDANTE du zoom du canvas") but is easy to
mis-assume the other way (that content should visually track camera zoom AND get an extra
inner-zoom multiplier on top) — worth flagging for whoever builds Phase 9's demo, since visually a
node's content will NOT shrink/grow just because the user zoomed the canvas out/in unless that
content also uses Grow/Auto sizing that naturally follows the node's own (canvas-zoom-scaled) box;
explicit-pixel-sized content stays visually fixed under canvas zoom, only responding to
`prop::InnerZoom` itself. `addGraphNode()` gained a trailing `float innerZoom = 1.f` parameter
(applies `content.innerZoom(value)` — the Phase-0 `WidgetBuilder` chain method — before wrapping);
a new free function `setNodeInnerZoom(world, layout, node, value)` covers the RUNTIME-change
acceptance criterion (spec: "changer indépendamment l'inner-zoom d'un nœud"), locating the node's
sole content child via `UiChildren` and calling the pre-existing `editInlineStyle()`/`setInlineStyle()`
helpers from `styles.hpp` (unmodified, first real reuse of them outside the original style-engine
work) — it takes a `LayoutSystem&` explicitly (unlike `editInlineStyle()` itself) purely to call
`layout.markDirty()`, since style mutation and layout invalidation are DELIBERATELY decoupled
mechanisms in this codebase (documented Phase-0 limitation: `computeFonts()` only re-reads inline
style on a real layout pass, not on `StyleSystem::resolve()`).

**Default theme — tuned existing struct member-initializers, not a new theme-object abstraction.**
Considered (and rejected) building a `NodeGraphTheme` struct with named presets/apply-helpers —
the acceptance criteria never ask for a themeable-preset API, only for the DEFAULTS to look
Blueprint-inspired, so introducing a new configuration surface would have been unrequested scope.
Instead: retinted the existing default member-initializers directly — `UiNodeGraphCanvas` (dark
slate background/grid, `{25,26,30}`/`{38,39,45}`/`{55,57,66}`), `UiGraphNode::headerColor`
(`{52,86,150}`, a neutral blue — Blueprint's real category-color system, where function/event/pure
nodes get different header colors, is explicitly a per-user-node choice this editor already
supports via the existing `headerColor` field/parameter, not something to hardcode), `UiGraphPin`/
`GraphConnection` (near-white `{235,235,235}`/light grey-white `{225,225,225,235}`, mirroring
Blueprint's white exec pins and light wires — pin/wire "type colors" are, again, already a
per-instance override the user controls). **Real bug found by the FIRST assertion in this phase's
test, not by later debugging**: `addGraphPin()`'s own DEFAULT PARAMETER value (a second, separate
copy of the old palette baked into the function signature, independent of `UiGraphPin`'s struct
member-initializer) had been left un-retinted — same latent duplication risk as `ClipboardPin`/
`ClipboardNode`'s own separate default-value copies, all four now kept in sync by hand since none of
them derive from a single shared constant (a `constexpr` palette would remove this class of
duplication if a future phase touches these colors again).

**Two new node-level visual states, genuinely missing before this phase**: `UiGraphNode::hovered`
(pins already had this since Phase 2; nodes never did) and `UiGraphNode::disabled` (spec section 8
explicitly lists "actif/désactivé" among the required visual states, and nothing modeled it).
Hover: same per-motion-event hit-test-then-broadcast idiom as pin hover (Phase 2), tested against
the node's FULL screen rect (not just the header) so hovering the body still lights up the header.
Disabled: gates BOTH hit-test paths that could select/manipulate a node (resize-grip AND header —
added `|| node.disabled` alongside the pre-existing `!resizable`/`!movable` guards) — mirrors the
generic `UiDisabled` marker's "ignore l'input" contract used everywhere else in this module, just
implemented as a plain bool instead of a marker component (no subtree-wide disable propagation is
meaningful here, since a node's content isn't recursively "disabled" by this flag — deliberately
narrower in scope than `UiDisabled`). "Active" (also listed in spec section 8) was deliberately NOT
given its own new field — the pre-existing `draggingMove`/`draggingResize` bools already ARE the
node's "currently active" signal (a node being manipulated is definitionally active), so
`RenderHeaders()` treats them as such rather than duplicating the concept.

**Real bug caught by the test, not by inspection, again**: the marquee-selection finalize path
(Phase 5) iterated every node's `UiComputed.screen` for intersection WITHOUT checking `disabled` —
so a disabled node could still be marquee-selected even though its own direct header-click path
correctly excluded it. Exposed by the very test written to confirm "clicking a disabled node's
header selects nothing": the click fell through the (correctly-guarded) header hit-test, past the
resize-grip check, all the way to the marquee-start fallback (any left-click that hits nothing above
it starts a marquee, cf. Phase 5) — and the marquee's OWN release-time selection logic then picked
up the disabled node anyway, since ITS guard hadn't been added. Fixed with the same `|| node.disabled`
skip. Lesson: a new per-node gate has to be threaded through EVERY selection entry point (direct
click, marquee, and — from Phase 6 — anything that iterates nodes for keyboard-shortcut actions),
not just the most obvious one; the disabled state doesn't yet block Ctrl+A (`selectAllNodes()`) or
copy/cut, which is an intentionally narrower interpretation for this phase (disabled blocks
mouse-driven manipulation specifically, not the programmatic API) but worth flagging if a future
phase's acceptance testing expects disabled nodes to be fully invisible to every selection path.

Extended `tests/ui_smoke_test_7.cpp` a sixth time in place: default-color spot-checks on the
already-live `canvasE`/`nodeE` plus a freshly-added pin; a dedicated throwaway node with an
explicit-pixel-sized child, `innerZoom=2.0` at construction (asserts the child's resolved screen
size doubles) then `setNodeInnerZoom(0.5)` at runtime (asserts it changes again, independent of
whatever the canvas's OWN zoom happened to be at that point in the test sequence — deliberately
NOT asserting any interaction with canvas zoom, per the independence reasoning above); node
hover on/off via synthetic mouse motion; and the disabled-node click test that surfaced the marquee
bug above (selection unchanged, position unchanged, fixed then re-verified). Full regression
(`ui_smoke_test_1..7` all exit 0 with byte-identical baseline numbers, all 5 examples headless exit
124, zero stderr) confirmed green after the one marquee-disabled fix — caught immediately by the
test, not a later debugging session.

**Phase 8 — Persistence via `data::` (still `src/ui/nodegraph.hpp`).** `serializeGraph(world,
canvasEntity) -> data::NodePtr` / `deserializeGraph(f, nodeGraph, layout, canvasEntity, doc) ->
unordered_map<string,Entity>`, built on the project's PRE-EXISTING `data::Node`/`NodePtr` tree
(`src/data/node.hpp` — a `sdl3::Properties`-backed Object/Array/scalar tree already shared by every
format codec in `data::`, per Décision #8) — no new serialization format invented, exactly as
planned. These two functions produce/consume only the intermediate `data::Node` tree; converting
that to actual JSON/XML/YAML/TOML text is the CALLER's job via the pre-existing `data::JsonDocument`
etc. (`data.hpp`) — `nodegraph.hpp` itself only `#include`s the lightweight `data/node.hpp`, not the
full `data.hpp` umbrella with every codec, to avoid pulling parser code into every translation unit
that touches the node-graph.

**Entity identity is never persisted** — `Entity` (id+generation) is an ECS-internal, ephemeral
value with no meaning outside a single process run; `serializeGraph()` instead assigns fresh
sequential string keys ("n0", "n1", ..., "p0", "p1", ...) purely to cross-reference nodes/pins/
connections/selection WITHIN one document, rebuilding the id↔key maps from scratch on every call
(no state carried between serialize calls, no assumption the keys are stable across saves).
`deserializeGraph()` never tries to honor the OLD keys as new Entity values either — it just builds
its OWN fresh id↔key map for THIS deserialization pass, exactly mirroring how `pasteClipboard()`
(Phase 6) already handles the same "recreate identity-bearing things without reusing dead
identities" problem.

**Content is not persisted, same boundary as Phase 6's clipboard, same reasoning**: the editor has
no generic way to serialize widgets it didn't create, so a deserialized node gets an empty body
(`f.column()`) — an app that wants full round-trip content needs to store that separately (e.g., a
"node type" string this file deliberately does NOT model, left to the application, matching the
plan's own "generic enough for logic graphs/workflows/behavior trees/material editors" framing —
inventing a node-type/factory-registry system here would be scope creep no acceptance criterion
asks for).

**Deliberate deviation from the plan's literal Décision #7 wording, made and documented rather than
silently followed or silently ignored**: the plan says clipboard (Phase 6) and full persistence
(this phase) should share ONE mechanism ("pas de code dupliqué"). Given Phase 6 shipped and was
fully verified BEFORE this phase (persistence necessarily has to come after, in this file's own
build order) with its own lightweight `ClipboardNode`/`ClipboardPin` structs — refactoring already-
green Phase 6 code to route through `data::Node` now would touch tested, working code purely for
architectural purity with no user-visible behavior change, for real regression risk. Left as two
separate, small, independently-correct mechanisms rather than unifying them after the fact — noted
here explicitly so it isn't mistaken for an oversight if revisited later (e.g., if a future need
arises for the clipboard to survive an app restart, which `data::Node`-backed persistence would
support and the current clipboard structs don't).

**Round-trip coverage confirmed exhaustively**, matching spec section 10's own list: canvas pan/
zoom; node position/size/header color/resizable/movable/disabled/min-size/header-height/inner-zoom;
every declarative pin field (shape/side/side-offset/size/color/connectable); every declarative
connection field (color/thickness/opacity/style/both end-caps/arrow-size/bezier-control-offset/
waypoints); and node selection. `onCustomDraw` callbacks (pins and connections) are, unavoidably,
NOT persisted — a `std::function` has no serializable representation; an app relying on custom
rendering needs to re-attach those callbacks itself after `deserializeGraph()` returns (same
limitation any callback-based extensibility mechanism in this codebase already has, not new here).

Extended `tests/ui_smoke_test_7.cpp` a seventh time — this one deliberately on a FRESH, isolated
pair of canvases (source + destination) rather than reusing the 7-phases-of-mutation `canvasE`,
specifically so every asserted value is a clean, hand-chosen literal rather than something requiring
re-derivation from prior test state (a lesson from how fragile that got in earlier phases' marquee/
auto-scroll tests). Builds a 2-node, 1-connection, 1-waypoint graph exercising every non-default
field at once (deliberately NOT using any default value, so a field silently failing to round-trip
would show up as a wrong-value assertion rather than an accidentally-correct default matching),
serializes, deserializes into a brand-new canvas, and re-finds the deserialized node by TITLE (since
document keys aren't exposed to the caller) before asserting every field, the pin count via a live
`UiGraphPin` query, and the connection's full field set directly off `UiNodeGraphCanvas::connections
[0]`. Full regression (`ui_smoke_test_1..7` all exit 0 with byte-identical baseline numbers, all 5
examples headless exit 124, zero stderr) confirmed green — first try, no debugging detour, largest
single addition of the whole node-graph initiative so far and it compiled/passed clean.

**Phase 9 (final) — `examples/ui_node_graph_demo.cpp` + closing out `tests/ui_smoke_test_7.cpp`.**
A real, interactive "visual calculator" demo — Add/Multiply nodes (two input pins each, a
Triangle-shaped output pin colored to match the node's category-style header) feeding an Output
node (two input pins) — matching the plan's own "façon calculatrice visuelle simple" framing (no
actual arithmetic evaluation; the graph LOOKS like a calculator, wiring/evaluation logic is
explicitly out of scope, same as the original spec's own wording). Demonstrates, for real, in one
running app: the default Blueprint-style theme (Phase 7, applied automatically, no extra code
needed), a Bézier connection with an inserted waypoint and an Orthogonal connection with custom
color/thickness on the other, `innerZoom=1.3` on the Output node's content, a real right-click
context menu (Delete/Duplicate) built entirely in application code against `NodeGraphCallbacks::
onContextMenu` (confirming the editor/user split actually works end-to-end, not just in isolation),
and Ctrl+S/Ctrl+O wired to `serializeGraph()`/`deserializeGraph()` through `data::JsonDocument` +
`sdl3::IOStream::fromFile()` — real file save/load, not a stub.

**Doesn't use the `ui::Ui` facade** (unlike `ui_minimal.cpp`) — deliberately, since `Ui::render()`
bundles style-resolve/layout/render into one opaque call, but `NodeGraphSystem` needs to inject
`RenderGrid()`/`RenderConnections()` BETWEEN the grid and the node bodies, and `RenderHeaders()`/
`RenderPins()`/`RenderMarquee()` AFTER them — exactly the ordering the file's own header comment has
documented since Phase 1, now finally exercised in a real app rather than only in the smoke test's
raw-systems style. Two small real bugs caught immediately by the compiler, not left for review:
(1) `WidgetBuilder` is deliberately non-copyable (established since the original 2026-07-05 rewrite)
— chaining `.textColor(...).fontSize(...)` directly into an `auto x = f.label(...)....;`
initializer tries to copy-construct from the returned `WidgetBuilder&`, which doesn't compile; fixed
by splitting into the established two-statement idiom (`auto x = f.label(...); x.textColor(...)
.fontSize(...);`) already used everywhere else in this codebase — a reminder that fluent-chain
results must never be the thing an `auto` variable is initialized FROM, only ever built on a
previously-named lvalue. (2) `data::Document::encode()` is `[[nodiscard]]`; the save handler now
checks and reports the result instead of silently discarding it.

Final `tests/ui_smoke_test_7.cpp` addition covers the two acceptance-checklist items no earlier
phase had actually exercised: the `PinShape::Triangle` shape (Circle/Square landed in Phase 2, Star
in Phase 8's round-trip test, Triangle had never been spawned in any test until now) and the
`onCustomDraw` escape hatch on both a pin AND a connection (criterion #15, "rendu 100%
personnalisable") — verified for real, not just by code inspection, via a genuine headless
`sdl3::Window`/`sdl3::Renderer` (same `setenv("SDL_VIDEODRIVER","dummy",0)` + `Window::Create(...,
Hidden)` + `Renderer::Create()` pattern already established by `ui_smoke_test_4`/`ui_smoke_test_3`,
newly added to `ui_smoke_test_7` since none of Phases 1-8 had ever needed a real renderer — pure
interaction-logic testing until now), running NodeGraphSystem's actual FULL render pipeline
(`RenderGrid → RenderConnections → RenderHeaders → RenderPins → RenderMarquee`, the exact
documented order) once, asserting both custom-draw lambdas fired. A closing comment block maps all
15 of the spec's acceptance criteria (section 11) to the exact phase/test that covers each one, for
future traceability without needing to re-derive it from scratch.

Final verification, the last of the whole initiative: `ui_smoke_test_1..7` all exit 0 with
byte-identical baseline numbers all the way back to Phase 0's original pre-node-graph values, all 6
example binaries (`ui_showcase`, `ui_minimal`, `ui_aero_basics/lists/windows`, and the new
`ui_node_graph_demo`) run headless under `SDL_VIDEODRIVER=dummy` for their full timeout with zero
stderr and no crash (`exit 124`, the expected "healthy" GUI-event-loop outcome, same as every prior
phase's check).

---

## Status at end of the Node-Graph Editor initiative (2026-08-14)

All 10 phases (0-9) of the plan's "Node-Graph Editor" section are complete and green. What exists
now that didn't at the start of this initiative: a full, reusable, generic node-graph editor
(`src/ui/nodegraph.hpp`) — canvas pan/zoom/grid, ECS-real nodes with arbitrary user content, small
hit-testable pin entities (4 shapes, declarative styling, `onCustomDraw` escape hatch),
data-only connections (3 routing styles, arrows, waypoints, `onCustomDraw` escape hatch),
Entity-keyed multi-selection with marquee and true group-move, auto-scroll, a
detect-only/user-builds-the-menu context-menu hook, an internal cut/copy/paste/delete clipboard,
per-node independent content scaling (`inner-zoom`) built on the existing style-cascade machinery,
a Blueprint-inspired default theme, full `data::Node`-backed state serialization, and a real
interactive demo app — plus the Phase 0 layout-engine extension (`EmBases::scale`) that made
inner-zoom possible without touching `LayoutSystem`'s placement algorithm at all. `tests/
ui_smoke_test_7.cpp` grew phase-by-phase into the project's largest single smoke test, covering
every phase's mechanics with real assertions rather than "didn't crash" checks. Zero regressions
introduced to the pre-existing `ui::` module or the ImGui-coverage/Aero-theme initiative that
preceded this one — every phase's full regression pass (all 7 smoke tests + all 6 examples) stayed
green throughout, most phases on the first try. Explicitly out of scope for this initiative, deferred
to a separate future planning session per the user's own earlier sequencing decision: porting
SDL3pp's `07_audio_patchbay.cpp` into `examples/ui_aero_pathbay.cpp` and the associated audio-DSP
additions to `src/sdl3/audio.hpp`.

---

## New initiative (2026-08-14) — 2D plotting widget system (`src/ui/plot.hpp`), ImPlot-inspired

User requested a configurable widget covering all common 2D plot/chart types, modeled on
[epezent/implot], adapted to this module's ECS/factory architecture. Full plan:
`/home/clement/.claude/plans/mossy-forging-fog.md` (reused/overwritten a third time — the completed
node-graph and Aero-widgets plans are preserved further down in that same file under
`[Historique]` headings).

**Architecture decision made during planning, worth remembering**: a plot does NOT need a
node-graph-style standalone system (no `PlotSystem`, no `prepass()`, nothing extra to call in the
app's main loop). Unlike node-graph nodes (a real ECS subtree per node, hence
`NodeGraphSystem`), a plot has no per-point/per-series entities at all — it's a single
self-contained, self-drawing leaf, the SAME category as `UiTable`/`UiCalendar`/the widget it
replaces. It plugs into the EXISTING `RenderSystem::drawWidget`/`InputSystem::dispatch` dispatch
points, exactly like those already do — `f.plot()...Spawn()` and it just works, no extra call
sites for the application to wire up. This was reconsidered mid-planning: my first mental draft
mirrored the node-graph's standalone-system shape by inertia; re-deriving from the actual
data-shape question ("does this need per-element entities?") gave the opposite, simpler answer
before any code was written — worth the same "does this thing actually need per-element identity"
question test for any FUTURE big self-drawing-leaf widget too.

**Immediate full replacement, not a parallel second widget**: the project already had a minimal
`Plot` (2026-07-05, `PlotKind::Lines/Histogram`, one implicit-X Y-only series, no axes/legend/
zoom) used by `ui_showcase.cpp` (live sine-wave demo) and `ui_aero_lists.cpp` (static demo), with NO
dedicated test. Rather than adding a second, bigger plot widget alongside it (this codebase
consistently avoids duplicate/overlapping abstractions), the old component was deleted from
`components.hpp` and both call sites migrated in the SAME phase that introduced its replacement —
Phase 0, not deferred to a cleanup phase at the end (unlike the node-graph's clipboard-vs-persistence
deferral, this migration was low-risk enough — no prepass/system entanglement — to just do
immediately).

**`src/ui/plot.hpp` is a DATA + FREE-FUNCTIONS file, not a class-based system** — deliberately
different shape from `nodegraph.hpp`. Included from `components.hpp`... actually corrected during
implementation: mirrors `interaction.hpp`'s REAL inclusion point (`factory.hpp`/`systems.hpp`/
`ui.hpp` each include it directly, `#include "components.hpp"` at plot.hpp's own top) — the
original plan text said "included from components.hpp" but a quick check showed `interaction.hpp`
itself isn't actually included there either (a minor plan-vs-reality correction made silently
during Phase 0, no behavioral difference, just matching the file's REAL existing convention rather
than a slightly-misremembered one).

**Text drawing stays OUT of plot.hpp — a real constraint discovered while implementing, not
anticipated in the plan**: `RenderSystem::drawTextRaw`/`measureCached`/`drawTextCentered` are
PRIVATE MEMBER functions of `RenderSystem` (systems.hpp), and plot.hpp is included BEFORE
`RenderSystem` is even declared — so plot.hpp CANNOT draw any text itself. Split cleanly: plot.hpp
provides pure geometry/math (`drawPlotGrid`, `drawPlotSeries`, `generateTicks`, transform functions)
using free `sdl3::Renderer` methods only; `RenderSystem::drawWidget`'s `Plot` block (which DOES
have access to its own private text helpers) draws the title/hover-value text itself, calling into
plot.hpp only for the geometry. Tick VALUE labels (not just gridlines) were deliberately deferred
past Phase 0 — `generateGetTicks()` exists and is exercised via the grid, but no numeric axis labels
are drawn yet; noted as a small later addition (Phase 4, alongside secondary-axis work) rather than
a Phase 0 requirement.

**Component shape**: `PlotSeries{kind, x[], y[], color, lineWidth, marker, markerSize, label,
visible, onCustomDraw}` — pure data in `UiPlot::series` (`std::vector`, no entity per series/point,
same reasoning as `GraphConnection`/`UiTable::rows`: a plot can have thousands of points).
`PlotAxis{min, max, autoFit, showGrid, label}` — X + Y only this phase (secondary Y is Phase 4).
Interaction state (`hovered`, `hoveredSeries`, `hoveredIndex`) lives directly ON `Plot`, matching
`UiKnob`/`UiSlider`/`UiResizeHandle`'s existing convention (a widget's own interaction state lives
on ITS OWN component, not in a side-table) — NOT the `NodeGraphSystem`-style private-members-on-a-
system-object pattern, which only made sense there because that system spans many entities.

**Auto-fit is a PURE, non-caching computation — deliberately never written back into
`PlotAxis::min/max`**, discovered to matter while implementing hover: both `RenderSystem::
drawWidget` (every frame) and `InputSystem::dispatch`'s hover block (every discrete event) need
"the currently effective axis range," and if auto-fit were computed once and cached, one of the two
could read a stale value depending on frame timing. New `resolveAxis(axis, series, forX) ->
PlotAxis` (a COPY with min/max resolved fresh if `autoFit`, passed through unchanged otherwise) is
called independently at BOTH sites — cheap enough (linear scan) to not cache, and guarantees they
can never disagree. This also pre-answers a Phase 3 design question: pan/zoom will need to flip
`autoFit = false` on first user interaction (matching how virtually every plotting library behaves)
specifically because leaving it `true` would mean the NEXT frame's fresh auto-fit recompute
immediately overwrites whatever the user just panned/zoomed to.

**Palette**: `constexpr std::array<Color,10>` "tab10" categorical palette (the same well-known
qualitative palette matplotlib/D3/Vega ship by default — reused as plain RGB constants, not copied
code) lives locally in `plot.hpp`, NOT added to `UiTheme` (confirmed via research: `UiTheme` has
no categorical-sequence concept today, just a single `accent` color — adding one there for a single
consumer wasn't worth the wider surface change). `plotPaletteColor(index)` round-robins it;
`WidgetBuilder::addSeries()` calls it automatically whenever the caller doesn't pass an explicit
color.

**Thick-line rendering duplicates `appendThickSegment`/`FColor` handling from `nodegraph.hpp`
rather than sharing it** — same quad-per-segment technique (no native thick-line primitive in
`sdl3::Renderer`, confirmed once again), copy-pasted into `plot.hpp`'s own `detail::` namespace
instead of factored into a shared helper. Deliberate, not an oversight: `nodegraph.hpp` and
`plot.hpp` have no natural shared-dependency file between them (`nodegraph.hpp` sits much later in
the include chain, `plot.hpp` right after `components.hpp`), so sharing would mean inventing a new
tiny "geometry helpers" file for two call sites — judged not worth it yet; worth revisiting if a
THIRD consumer shows up.

**One real bug caught immediately by the compiler, not left for review**: `WidgetBuilder::
addSeries()` first wrote `plot.Unwrap()->series.push_back(...)` — but `plot` is
`Option<Plot>` (a VALUE-holding Option, not `Option<Plot*>`), and `.Unwrap()` on that returns a
COPY per this project's own established `Option<T>` contract (documented back in the very first
2026-07-05 session note in this file, and it bit builder chaining then too) — so the `push_back`
would have silently mutated a throwaway temporary, never actually growing the real stored series
list. Fixed by using `plot->series` (the `operator->` overload, which DOES give real mutable
access) instead of `.Unwrap()->`. Exactly the documented gotcha, still very much alive as a trap —
worth grepping for `\.Unwrap()->` on any future `Option<T>`-by-value (not `Option<T*>` from
`GetComponent`) field before trusting it compiles-and-does-the-right-thing.

**Verified**: standalone `-fsyntax-only` on `plot.hpp` alone, then the full `ui.hpp` umbrella;
`ui_showcase.cpp`/`ui_aero_lists.cpp` needed ZERO changes to their plot CONSTRUCTION call sites
(the new `plotLines(values, title)` convenience wrapper was deliberately kept, on the new
foundation, with the exact old signature shape — a small, deliberate deviation from the plan's
literal "remove plotLines()" wording, judged worth it since it avoids all migration churn at both
call sites for zero loss of capability) — only `ui_showcase.cpp`'s runtime `setValues()` call
needed updating to `series[0].setY()` (`Plot` itself no longer owns values directly). New
`tests/ui_smoke_test_8.cpp`: transform round-trip, `computeAutoFit` (visible-only, 5% padding,
empty-input fallback), `generateTicks` (rounds to a "nice" 1/2/5×10ⁿ step), construction via both
the `plotLines()` convenience and the full `.plot().addLineSeries()/.addScatterSeries()` fluent
API (including palette-vs-explicit-color resolution), `PlotSeries::setY()`'s implicit-X
regeneration, hover/nearest-point via a real synthetic `InputSystem::handleEvent`, and a full real
`sdl3::Window`/`Renderer` render pass (grid+series+tooltip) with no crash — following the exact
same "real headless window, not just logic assertions" discipline `ui_smoke_test_7`'s Phase 9
established for testing anything that touches actual drawing. Full regression (`ui_smoke_test_1..8`
all exit 0, `ui_smoke_test_1`'s numbers byte-identical to the pre-plot baseline, all 6 examples
headless exit 124, zero stderr) confirmed green — first try, no debugging detour.

**Phase 1 — Bar/BarH/Histogram/Area/Stem/Step series kinds (still `src/ui/plot.hpp`).** `PlotSeriesKind`
grew from `{Line,Scatter}` to add all six; `drawPlotSeries()` became a `switch` instead of an
`if(Line)`. Two small new fields on `PlotSeries` (`barWidth=0.67` — DATA-space width, same
convention as `ImPlot::PlotBars`'s own width parameter, not pixels; `fillOpacity=0.35`, Area only).

**`BarH` swaps which array means what** — `x[i]` is the bar's LENGTH (extends from the baseline),
`y[i]` is its POSITION/category — matching `ImPlot::PlotBarsH`'s own established convention exactly,
rather than inventing a different one. Documented prominently on the enum itself since it's the one
easy-to-get-backwards spot in the whole kind list.

**`Histogram` stayed a pure rendering variant of `Bar`, not real frequency binning** — a deliberate,
scope-conscious choice, not a shortcut taken without noticing: true ImPlot `PlotHistogram` takes RAW
unbinned samples and computes bin counts internally, but re-checking the OLD widget being replaced
confirmed it never did real binning either (its "histogram" mode was already just "one bar per
array index," identical math to today's `Bar`) — so keeping that established behavior and only
changing the VISUAL distinction (bars touching, no gap between them, vs `Bar`'s 80%-width-with-gaps)
preserves continuity without silently promising a statistical feature nobody asked for. `x[]`/`y[]`
are documented as "already binned" (bin centers / counts) — the caller does any real binning
upstream if they have raw samples.

**Area fill needed real geometry work, not a shortcut**: the OBVIOUS approach — build one big
polygon (curve points + reversed baseline points) and call the existing `fillPolygon()`
(triangle-fan-from-centroid, already used for Pie-shaped things) — was rejected mid-implementation
because a fan from one centroid only renders correctly for convex/star-shaped polygons, and a
wiggly line's area-under-curve is neither in general (a fan would produce visible wrong-color
triangles cutting across the shape on any non-monotonic data). Built `appendFillQuad()` instead:
one quad (2 triangles) per consecutive point-pair PLUS its two baseline projections, assembled via
`renderGeometry` exactly like the existing thick-line technique — correct for ANY curve shape,
because each quad is independently convex by construction (a trapezoid between two vertical
projections), never relying on global shape convexity.

**`drawThickPolyline()`/`stepPoints()` were factored out of the old inline Line-only block** —
`Step` turned out to need ZERO new drawing code, only a different INPUT point sequence (insert an
intermediate corner point between each pair, held-then-jump / "post" convention, matching how a
piecewise-constant / digital signal is usually drawn) — `detail::stepPoints()` transforms the point
list, then hands off to the exact same `drawThickPolyline()` Line already uses. A clean example of
"new series kind, zero new rendering code" once the right seam existed.

**Marker-drawing default rule extended, not duplicated**: Stem's tip markers reuse the EXACT same
generic trailing marker block every other kind already shares (`pts[i]` already IS the tip position
for a Stem series — no special case needed), just added to the "draw markers by default even
without an explicit `.marker`" condition alongside `Scatter` (matching the classic "lollipop plot"
look, where an unmarked stem doesn't really read as data). Bar/BarH/Histogram/Area deliberately do
NOT get default markers — only if the caller explicitly sets `.marker`, which still works uniformly
since the trailing block doesn't gate on `kind` for the explicit-marker case.

**Builder API**: six new `WidgetBuilder` chain methods (`addBarSeries`/`addBarHSeries`/
`addHistogramSeries`/`addAreaSeries`/`addStemSeries`/`addStepSeries`), all thin wrappers around the
existing `addSeries()` plus a trailing kind-specific parameter (`barWidth` for the three bar
variants — `Histogram`'s own default is `1.0` for touching bars vs `Bar`'s `0.67`, exercised in the
test as `.barWidth` field values, not just visually; `fillOpacity` for Area) — no new
`Option<Plot>`-mutation gotchas resurfaced (each helper calls the already-fixed `addSeries()`
internally then mutates `plot->series.back()` directly via the correct `operator->`, not
`.Unwrap()->`).

Extended `tests/ui_smoke_test_8.cpp` a second time in place: `detail::stepPoints()` tested directly
against a known 3-point input (confirms the exact "hold then jump" point sequence, not just "some
staircase shape"); construction of all six new kinds via their dedicated builder methods on one
plot (asserts each series' `kind` AND its `barWidth`/`fillOpacity` — both the DEFAULT value picked
per-kind and one EXPLICIT override); and a full real-renderer pass with all six kinds present in
the SAME plot simultaneously (catches any cross-kind interaction bug a one-kind-at-a-time test
would miss — none found). Full regression (`ui_smoke_test_1..8` all exit 0 with byte-identical
baseline numbers, all 6 examples headless exit 124, zero stderr) confirmed green — first try.

## Phase 2 — Legend + full per-series style wiring (scope expanded mid-phase by the user)

Started as "legend list + wire up color/width/marker/fill that were already stored but not fully
used by rendering." Mid-implementation, the user exited Auto Mode (system-triggered — from then on
I ask clarifying questions instead of assuming on ambiguous points) and I asked, via
`AskUserQuestion`, how legend overlay-vs-reserved-space should work. The user's answer expanded
scope well beyond the original plan text (verbatim, French):

> "Ajouter des options pour choisir le positionnenement des graduations (abscisses: left, right,
> ordonnée: top, bottom) et de la légende (left, right, top, bottom, top-left, top-right,
> bottom-left, bottom-right) et si la elle est superposée par dessus ou si elle à un espace
> réservée et la couleur RGBA de la boite de légende et de graduation si superposées au dessus."

**Interpreting "abscisses: left, right / ordonnée: top, bottom"**: taken literally this reads
X-axis positioned left/right and Y-axis positioned top/bottom, which is backwards for a Cartesian
plot (X ticks run along a horizontal edge — Top or Bottom; Y ticks run along a vertical edge — Left
or Right). Read as a likely slip in French math-class terminology under time pressure rather than a
literal spec, and implemented the sensible mapping instead: X-axis ticks choose `PlotEdge::Top` or
`PlotEdge::Bottom`, Y-axis ticks choose `PlotEdge::Left` or `PlotEdge::Right`. `PlotEdge` is one
shared 4-way enum (`Left/Right/Top/Bottom`) rather than two separate 2-way enums, since it's also
reused for margin-side bookkeeping (`detail::legendMarginSide`).

**Design: a single margin-composition system serves both axis-ticks and legend, in both overlay and
reserved modes.** `computePlotMargins(const Plot&)` inspects `xAxis.tickPosition/tickOverlay`,
`yAxis.tickPosition/tickOverlay`, and `legendPosition/legendOverlay/showLegend` together and returns
one `PlotMargins{left,right,top,bottom}` — each non-overlaid element (ticks or legend) contributes
its reserved thickness (`kPlotTickMargin` per tick edge, `kPlotLegendWidth`/`kPlotLegendRowH`-derived
for the legend) to the correct side; overlaid elements contribute zero margin and are instead drawn
directly on top of the plot rect at render time. `computePlotRect()` (renamed from the old
`plotContentRect()`, now margin-aware) shrinks the widget rect by these margins once, and everything
downstream (`drawPlotGrid`, series rendering, tick label loops, legend) works off that single rect —
no separate code path for "ticks reserved but legend overlaid" vs "both reserved" vs "both overlaid"
combinations; margin composition handles all 2×2×(4+8) combinations uniformly.

**One 8-position anchor function reused for two different containers.** `anchorRectIn(LegendPosition,
area, w, h, pad)` places a `w×h` box at one of 8 anchors (4 edges centered + 4 corners) inside an
arbitrary `area` rect. It's called with TWO different `area` values depending on `legendOverlay`:
when overlaid, `area = plotRect` itself (legend floats on top of the data); when reserved,
`area = legendReservedStrip(...)` (the thin margin strip carved out on the correct side by
`computePlotMargins`). Same anchor math, same 8 positions, no duplicated positioning logic between
overlay and reserved modes — the only difference is which rect gets passed in.

**Legend box color is genuinely conditional on overlay mode**, per the user's explicit request:
`legendBoxColor`/`tickBoxColor` (RGBA) are only drawn (as a `fillRoundedRect` behind the
swatches/labels) when `legendOverlay`/`tickOverlay` is true — reserved-space mode has no
translucent box to draw since the strip is already visually separated from plot data by its own
empty margin, matching the user's framing ("la couleur RGBA de la boite ... si superposées",
conditioning the box's existence on the overlay case specifically).

**`dimFactor` legend-hover-highlight mechanism**: `drawPlotSeries()` gained a `float dimFactor = 1.f`
trailing parameter. `RenderSystem::drawWidget`'s per-series loop computes
`dim = (p.legendHover < 0 || i == (int)p.legendHover) ? 1.f : 0.25f` and passes it through — when a
legend row is hovered, every OTHER series fades to 25% alpha (multiplied into the draw color) rather
than the hovered series being redrawn brighter, cheaper and reversible without a second pass.

**Bug caught mid-refactor, not by a failing test**: switching `drawPlotSeries` to a `switch(s.kind)`
structure while threading `dimFactor` through, noticed the `Scatter` case fell through to
`default: break;` before reaching the shared trailing marker-drawing block — which never called
`ren.SetDrawColor(...)` for that path, so Scatter markers would silently render in whatever color a
PREVIOUS draw call (e.g. the grid line color) had last left the renderer in. Fixed by adding one
unconditional `ren.SetDrawColor(drawColor);` immediately before the shared marker block, which fixes
every kind reaching that block at once rather than patching Scatter specifically — the kind of bug
that would have been invisible in a test asserting series *data* (`kind`/`color` fields) without a
real-renderer pass, reinforcing why every phase's test suite ends with an actual `Window`/`Renderer`
render call, not just state assertions.

**Builder API**: `xTicks(PlotEdge, bool overlay=false, Option<Color>)`, `yTicks(PlotEdge, bool
overlay=false, Option<Color>)`, `legend(LegendPosition, bool overlay=true, Option<Color>)`,
`showLegend(bool)` — four new chain methods on `WidgetBuilder`, default overlay values chosen so
that calling NONE of them reproduces the pre-Phase-2 layout exactly (ticks reserved outside the
plot rect as before, legend overlaid top-right as before) — a deliberate non-regression guarantee
for every plot built before this phase existed, not just an arbitrary default choice.

Extended `tests/ui_smoke_test_8.cpp` a third time in place: `anchorRectIn` direct test (all 8
positions against a known rect); `computePlotMargins` test (default all-reserved case has all four
margins > 0; all-overlay case collapses all four to 0; legend-reserved-right case has `right > 0`
specifically); construction via `xTicks`/`yTicks`/`legend` builder methods (asserts the resulting
`PlotEdge`/`LegendPosition`/overlay bool/box color fields, entity `plotD`); legend interaction test
via real `InputSystem::dispatch` (hover sets `legendHover` to the row index under the mouse; a
`pressed` click on a row toggles that series' `.visible`; a second click re-toggles it back; moving
the mouse off the widget entirely clears `legendHover` back to -1); full real-renderer pass with
deliberately MIXED settings (X ticks reserved-bottom, Y ticks overlaid-left, legend reserved-right)
to exercise the margin-composition system's general case, not just the all-default or all-overlay
corners.

Full regression (`touch src/ui/*.hpp` + full `make` of all 6 examples + all 8 `ui_smoke_test_N`):
clean rebuild, zero warnings from `plot.hpp`/`systems.hpp`/`factory.hpp`, all 8 smoke tests exit 0
with `ui_smoke_test_1`'s coordinates byte-identical to the long-established baseline, all 6 examples
headless exit 124 (healthy timeout, no crash/stderr) — confirmed green, first try, despite the
significantly larger-than-planned scope.

## Phase 3 — Navigation : pan/zoom/zoom-rectangle/reset + échelle log

Pan (glisser gauche), zoom molette (par axe ou combiné selon la position du curseur), zoom-
rectangle (clic-droit glissé), retour à l'ajustement automatique (double-clic), et `PlotAxis::logScale`
(échelle log10, transform + graduations adaptées) — exactement le périmètre prévu par le plan, sans
extension de portée cette fois (contraste avec la Phase 2).

**Nouveau champ jamais exercé avant ce chantier : le bouton DROIT de la souris.** En auditant
`InputSystem::handleEvent`/`Frame` pour câbler le zoom-rectangle, découverte que le pipeline
d'entrée entier ne trackait QUE `SDL_BUTTON_LEFT` depuis le tout début du module — un `MOUSE_BUTTON_
DOWN`/`UP` avec le bouton droit tombait silencieusement dans aucune branche de `handleEvent` (ni
`in.down`, ni rien ne changeait). Ajouté `Frame::rightDown/rightPressed/rightReleased` + un membre
privé `rightDown` sur `InputSystem`, en miroir exact du trio gauche existant — seul consommateur
actuel : `UiPlot::boxZooming`. Aucun autre widget n'est affecté (les nouveaux champs restent à
`false` partout ailleurs).

**Design retenu pour l'échelle log — `toAxisSpace()`/`fromAxisSpace()` comme SEUL point de
variation.** Plutôt que de dupliquer la logique linéaire/log dans `dataToScreen`, `screenToData`,
`computeAutoFit`, le pan et le zoom séparément, ces deux petites fonctions (identité si linéaire,
`log10`/`pow10` bornés à un epsilon positif si `axis.logScale`) sont le SEUL endroit qui connaît la
différence ; tout le reste raisonne dans un « espace d'axe » uniformément linéaire. Concrètement : le
pan et le zoom-molette convertissent `axis.min`/`axis.max` en espace d'axe, font leur arithmétique
(décalage pour le pan, mise à l'échelle autour d'un pivot pour le zoom) comme si c'était toujours
linéaire, puis reconvertissent. Résultat : pan/zoom/zoom-rectangle fonctionnent correctement sur un
axe log SANS AUCUN cas particulier dans leur propre code — vérifié explicitement par un test dédié
(`generateLogTicks`, `computeAutoFit` en mode log qui ignore les valeurs <= 0 et pad dans l'espace
log, un plot log-log construit et rendu réellement).

**Deux bugs réels de signe trouvés et corrigés PENDANT ce chantier (pas hérités tels quels, mais la
même erreur commise deux fois puis unifiée)** : `dataToScreen`/`screenToData` prennent `screenMin`/
`screenMax` qui, pour l'axe Y de ce fichier, sont INVERSÉS par convention (`screenMin` = bas de
l'écran = `axis.min`, `screenMax` = haut = `axis.max` — établi dès la Phase 0). Le clamp de garde
anti-division-par-zéro écrit à la Phase 0 pour `screenToData` (`sdl3::Max(1e-6f, screenMax -
screenMin)`) suppose implicitement un span POSITIF — pour l'axe Y, `screenMax - screenMin` est
NÉGATIF, donc ce clamp l'écrasait silencieusement à `+1e-6f`, explosant le résultat vers des valeurs
énormes. **Jamais détecté avant cette phase** car `screenToData` n'avait jamais été appelé avec la
convention Y inversée jusqu'à `applyPlotBoxZoom` — le test de round-trip de la Phase 0 n'utilisait
que la convention X (non-inversée). Corrigé une première fois dans `screenToData` (test dédié : le
zoom-rectangle sur `Plot` réel produisait `yAxis.min == yAxis.max` avec des valeurs aberrantes,
`assert` a crashé immédiatement — bug attrapé par le test, pas découvert en production). Puis
EXACTEMENT LA MÊME erreur retrouvée, dupliquée inline, dans `applyPlotAxisZoom` (son propre calcul de
`t` réimplémentait le même clamp au lieu d'appeler `screenToData`) — repérée seulement parce que le
test d'intégration molette-sur-`Plot`-réel (Y) a produit des valeurs `-3.74e7` degénérées deux
tests plus tard (le test PUR de `applyPlotAxisZoom` avait échappé au bug car il ne testait QUE la
convention X non-inversée). Les deux occurrences unifiées en un seul helper `safeScreenSpan(span)`
(préserve le SIGNE du span écran au lieu de l'écraser à une constante positive), et un test dédié
« convention inversée, axe Y » ajouté à `applyPlotAxisZoom` pour que cette classe de bug ne puisse
plus jamais régresser en silence. Leçon retenue : un test « la plage a rétréci » (`post < pre`) est
un piège — `0 < grand-nombre-positif` passe trivialement même quand les DEUX valeurs sont des
degénérescences ; les tests suivants ont depuis vérifié aussi `range > 0` explicitement, pas
seulement la décroissance.

**Bug de conception de test (pas du code de prod) trouvé en cours de route** : `plotA`/`plotB`/
`plotC`/`plotD` (Phases 0-2) n'avaient jamais reçu de `.offset(...)` explicite — chacun, en tant
qu'entité racine sans parent, se retrouvait positionné à l'origine (0,0) par défaut, donc TOUS
superposés exactement au même endroit à l'écran. Invisible dans les Phases 0-2 car chaque test ne
vérifiait QUE l'état du plot qui l'intéressait, jamais l'absence d'effet de bord sur les autres.
Le nouveau `wheelConsumed` (exclusif par design — un seul widget doit réagir à la molette à un point
donné, comportement VOULU) a rendu ce chevauchement visible : le plot itéré en premier « gagnait »
systématiquement la molette, laissant les autres plots superposés inertes. Corrigé en donnant à
`plotB`/`plotC`/`plotD`/`plotE` des `.offset(...)` distincts (grille 2×2 puis une ligne de plus pour
`plotE`) — non-régression : aucune assertion des Phases 0-2 ne dépendait de positions absolues.

**Zoom molette par-axe vs combiné** : survoler la bande de graduation RÉSERVÉE (non superposée) d'un
axe zoome CET axe seul ; survoler ailleurs sur le widget (zone de tracé, ou une bande de graduation
SUPERPOSÉE — pas de bande dédiée détectable) zoome les deux axes ensemble, pivot centré sur le
curseur dans les deux cas. Couvre exactement la formulation du plan (« par axe ou combiné »).

**Seuil de 4px sur le zoom-rectangle** : un clic-droit sans glisser (relâché au même endroit,
< 4px de mouvement sur les deux axes) n'applique aucun zoom — évite qu'un simple clic-droit
« accidentel » réduise le plot à un point ; testé explicitement (axes inchangés après un aller-retour
sur place).

**Verification discipline étendue** : suite à la découverte du bug de span inversé, la fonction pure
`applyPlotAxisZoom` a désormais DEUX tests directs (convention X normale ET Y inversée) plutôt qu'un
seul — précédent pour les futures fonctions de transform de ce fichier : tout nouveau helper prenant
un couple `screenMin`/`screenMax` doit être testé dans les DEUX orientations, pas seulement celle qui
vient naturellement à l'esprit en écrivant le test.

Extended `tests/ui_smoke_test_8.cpp` une quatrième fois en place : `toAxisSpace`/`fromAxisSpace`
(linéaire + log), `dataToScreen`/`screenToData` en échelle log, `computeAutoFit(...,logScale=true)`
(ignore <= 0, marge en espace log), `generateLogTicks` (multiples {1,2,5}×10^n par décade présents),
`applyPlotPan`/`applyPlotAxisZoom` (x2, cf. ci-dessus)/`applyPlotBoxZoom`/`resetPlotToAutoFit` en
fonctions pures, puis intégration complète via `InputSystem::handleEvent` réel : pan, zoom molette
combiné, zoom molette par-axe, zoom-rectangle, clic-droit sans glisser (no-op), double-clic (reset),
et un rendu réel d'un plot en double échelle log AVEC un zoom-rectangle actif d'un autre plot
simultanément affiché (exerce `drawPlotBoxZoomRect`, jamais couvert par un rendu réel avant cette
phase). Full regression (`ui_smoke_test_1..8` tous exit 0, coordonnées de `ui_smoke_test_1` inchangées
octet pour octet, 6 exemples headless exit 124, zéro stderr) confirmée verte après correction des deux
bugs de span ci-dessus.

**Incident de session, sans rapport avec le code** : le rebuild complet en arrière-plan a été
interrompu (processus tué) après avoir compilé seulement `ui_smoke_test_1..5` sur les 8, suite à une
coupure de la session parente entre deux tours de conversation — la notification de complétion a été
perdue. Détecté en comparant les timestamps de mtime des binaires (`ui_smoke_test_6/7/8` nettement
plus anciens que le reste du lot) plutôt qu'en faisant confiance à un signal de complétion silencieux
; le rebuild des 3 binaires manquants a suffi, aucune perte de travail.

## Phase 4 — Curseur/tooltip (déjà couvert Phase 0) + axe Y secondaire

Le survol/tooltip existait déjà en forme correcte depuis la Phase 0 (point le plus proche parmi les
séries visibles, croix verticale + valeur) — vérifié qu'il continue de fonctionner sans changement
pour les séries de l'axe primaire ; le vrai travail de cette phase est l'axe Y secondaire (`yAxis2`)
et sa composition avec TOUT ce que les Phases 2-3 avaient déjà construit (légende, pan, zoom,
zoom-rectangle, reset).

**Décision de conception : `yAxis2` toujours présent, activé implicitement par les séries, pas de
flag séparé à synchroniser.** Plutôt qu'un `Option<PlotAxis> yAxis2` avec un booléen d'activation
distinct, `UiPlot::yAxis2` est un `PlotAxis` simple TOUJOURS présent ; son activation est dérivée
PUREMENT de l'état des séries via `plotHasSecondaryY(plot)` (scan de `PlotSeries::useSecondaryY`) —
aucun risque de désynchronisation entre "l'axe est actif" et "une série l'utilise réellement", contrairement
à un flag redondant. Toutes les fonctions qui en ont besoin (marges, rendu, navigation) appellent ce
helper plutôt que de lire un champ d'état séparé.

**`computeAutoFit`/`resolveAxis` gagnent un paramètre `forSecondaryY` (Phase 4) qui suit le même
patron que `logScale` (Phase 3) : un booléen additionnel à la fin, défaut `false`, aucun changement
de signature nécessaire côté appelants existants.** Point important documenté explicitement dans le
code : `forSecondaryY` n'a de sens QUE quand `forX=false` — l'axe X est PARTAGÉ par toutes les séries
quel que soit leur axe Y, donc filtrer par `useSecondaryY` n'aurait aucun sens pour le calcul de la
plage X (et n'est délibérément PAS appliqué dans ce cas, testé explicitement : une série primaire et
une série secondaire contribuent TOUTES LES DEUX à l'auto-fit de X).

**`applyPlotPan`/`applyPlotBoxZoom` étendus pour muter `yAxis2` INCONDITIONNELLEMENT, en plus de
`yAxis`** (nouveaux paramètres `startYAxis2`/toujours transmis) — décision délibérée plutôt qu'un
paramètre optionnel : la mutation est un pur calcul sans effet visible si `yAxis2` n'est jamais
dessiné/résolu ailleurs (cas où aucune série ne l'utilise), donc pas besoin de la garder conditionnelle
— plus simple à appeler (`Plot` a maintenant un `dragStartYAxis2` snapshot systématiquement pris en
plus de `dragStartXAxis`/`dragStartYAxis`, même remarque). Résultat : pan et zoom-rectangle affectent
les DEUX axes Y ensemble quand un axe secondaire existe — décision UX délibérée (les deux axes
partagent la même fenêtre X visible, il serait incohérent qu'un pan horizontal+vertical ne bouge que
l'un des deux).

**Zoom molette : zoom PAR AXE étendu à trois cibles possibles (X, Y primaire, Y secondaire), zoom
COMBINÉ étend désormais aux DEUX axes Y simultanément si un axe secondaire existe.** Survoler la
bande de graduation réservée de `yAxis2` (typiquement à droite, côté opposé à `yAxis` par défaut)
zoome CET axe seul — même mécanisme que `yAxis`/`xAxis` déjà en place, juste un troisième
`bool overY2Ticks` gaté par `plotHasSecondaryY(p)`. Survoler la zone de tracé (aucune bande de
graduation) zoome X + yAxis + yAxis2 (si présent) tous ensemble.

**Pas de grille dédiée pour `yAxis2`** — décision délibérée, documentée en commentaire à l'endroit
précis où `drawPlotGrid` est appelé (uniquement avec `xr`/`yr`, jamais `yr2`) : une seconde grille de
fond superposée à la première (souvent à une échelle très différente) brouillerait la lecture plus
qu'elle n'aiderait — convention déjà standard chez ImPlot et la plupart des bibliothèques de graphes
à double axe Y (l'axe secondaire garde ses graduations propres, pas ses propres lignes).

**`plotNearestPoint` change de signature** (`+const PlotAxis &yAxis2`) et choisit `yAxis` ou `yAxis2`
PAR SÉRIE via `s.useSecondaryY` à l'intérieur de sa propre boucle — un seul point d'appel existant
(`InputSystem::dispatch`) à migrer, plus le nouveau test direct de la Phase 4 qui vérifie explicitement
qu'un point appartenant à la série secondaire est bien retrouvé (et pas confondu avec un point de la
série primaire à une position écran proche mais une valeur de données très différente, du fait des
échelles différentes des deux axes).

**Builder API** : `useSecondaryY(bool=true)` — bascule la DERNIÈRE série ajoutée (pattern déjà établi
par Phase 1 pour `barWidth`/`fillOpacity`, mais ici comme méthode fluide séparée plutôt qu'un
paramètre des `addXSeries()`, car applicable à N'IMPORTE QUEL type de série existant sans dupliquer le
paramètre dans les 9 méthodes `addXSeries`) ; `yTicks2(PlotEdge, overlay, boxColor)` (miroir de
`yGetTicks()`) ; `logScaleY2(bool)` (miroir de `logScaleY()`). `UiFactory::plot()` initialise
`yAxis2.tickPosition = PlotEdge::Right` (bord opposé au défaut de `yAxis`, Left) — cohérent avec la
convention ImPlot/matplotlib d'un axe secondaire sur le bord droit.

Extended `tests/ui_smoke_test_8.cpp` une cinquième fois en place : `plotHasSecondaryY` (dérivation
pure depuis l'état des séries, pas un flag) ; `computeAutoFit(forSecondaryY=...)` (filtre Y
indépendamment primaire/secondaire, X toujours partagé par les deux) ; `computePlotMargins` (marge
droite supplémentaire seulement une fois `plotHasSecondaryY` devenu vrai) ; construction via
`addLineSeriesXY(...).useSecondaryY()` (nouvelle entité `plotF`, deux séries de magnitudes très
différentes — [1,2.5] vs [100,500] — pour rendre toute contamination croisée immédiatement visible
dans les assertions) ; auto-fit indépendant vérifié numériquement (`yr.max < 10`, `yr2.min > 50`) ;
`plotNearestPoint` direct PUIS intégration réelle via `InputSystem::handleEvent` sur un point de la
série secondaire (les deux doivent s'accorder) ; rendu réel avec graduations sur les deux bords.
Toutes les fonctions de navigation (Phase 3) déjà testées avec `yAxis2` dans leurs propres tests purs
(`applyPlotPan`/`applyPlotBoxZoom` — mise à jour de ces tests EXISTANTS avec une plage `yAxis2` de
magnitude 10-20x différente de `yAxis`, pas de nouveaux tests séparés, pour vérifier que les deux
échelles sont bien traitées indépendamment par le MÊME geste). Full regression (`ui_smoke_test_1..8`
tous exit 0, coordonnées de `ui_smoke_test_1` inchangées octet pour octet, 6 exemples headless exit
124, zéro stderr) confirmée verte, première tentative — aucun bug de signe/span cette fois (les deux
bugs de la Phase 3 avaient déjà forcé l'unification dans `safeScreenSpan()`/`toAxisSpace()`, dont
`yAxis2` hérite gratuitement puisqu'il n'est qu'un `PlotAxis` de plus passé aux mêmes fonctions).

## Phase 5 — Barres d'erreur (Line/Scatter) + Camembert (PlotMode::Pie)

Deux ajouts indépendants du plan original : `PlotSeries::yError` (surcouche géométrique simple sur
`drawPlotSeries()` existant, aucune nouvelle infrastructure) et `PlotMode::Pie` (première forme de
données VRAIMENT distincte du moule x[]/y[], cf. Décision #5 du plan — le morceau structurellement le
plus significatif de cette phase).

**Barres d'erreur : ~15 lignes ajoutées à `drawPlotSeries()`, aucun nouveau fichier/fonction
séparée.** `PlotSeries::yError` (vide par défaut = pas de barres, même idiome que `x`/`y` eux-mêmes)
dessine une ligne verticale ± l'écart autour de chaque point PLUS deux petites moustaches
horizontales, AVANT les marqueurs (pour que le marqueur du point reste visible par-dessus sa propre
barre) — décision de simplicité délibérée : pas de garde technique limitant `yError` à Line/Scatter
(la doc le RECOMMANDE pour ces deux types sans l'imposer), cohérent avec le principe déjà établi de
ne pas valider ce qui n'a pas besoin de l'être.

**Camembert : décision de conception centrale — dévie de la lettre de la Décision #5 du plan
(`Option<...>` séparé sur Plot) en faveur de la cohérence avec `PlotSeries`/`series` déjà en place.**
Le plan proposait `Option<vector<PieSlice>>` ; implémenté comme `std::vector<PieSlice> pieSlices`
simple (vide = pas de données), exactement le même idiome que `UiPlot::series` — un vecteur vide
communique déjà "pas de données" sans avoir besoin d'un `Option` en plus, et introduire l'`Option`
aurait été le SEUL endroit de tout le fichier à utiliser ce pattern pour une collection. Activation du
mode purement via `UiPlot::mode == PlotMode::Pie` (un `PlotMode` enum étendu `{XY, Pie}`, pas un
`Option<PieChart>` séparé) — cohérent avec le principe "pas de flag redondant à synchroniser" déjà
appliqué à `plotHasSecondaryY()` en Phase 4.

**`plotLegendItemCount(plot)` : nouvelle indirection minimale pour généraliser TOUTE l'infrastructure
de légende (déjà substantielle : marges, boîte, fond+pastilles, texte, interaction survol/bascule)
entre `series` (XY) et `pieSlices` (Pie) sans dupliquer 5 sites d'appel.** Seule la fonction de COMPTE
est généralisée via ce helper (`computePlotMargins`, `legendBoxRect`) ; les boucles qui accèdent
réellement aux champs (`drawPlotLegendBackground`, la boucle de texte de légende dans
`RenderSystem::drawWidget`, l'interaction légende dans `InputSystem::dispatch`) restent des
branchements `if (plot.mode == PlotMode::Pie) {...} else {...}` explicites et dupliqués — décision
délibérée cohérente avec le style déjà établi du fichier (cf. `appendThickSegment` dupliqué entre
plot.hpp et nodegraph.hpp) : pas d'abstraction/interface partagée pour 2 sites d'usage seulement, la
duplication reste plus lisible qu'une indirection.

**Géométrie du camembert : éventail de triangles EXACT depuis le centre RÉEL, PAS l'approximation par
centroïde de `fillPolygon()`/`renderGeometryFromPoints()` existant.** Repéré AVANT d'écrire le code
(pas un bug découvert après coup, cette fois) : ces fonctions génériques calculent leur éventail
depuis le CENTROÏDE des points passés — correct pour un polygone convexe fermé ordinaire, mais si on
leur passe seulement les points d'ARC d'une part de camembert (sans le centre explicite), le centroïde
ne coïncide PAS avec le vrai centre du cercle (surtout pour une part étroite), et le résultat visuel
serait un triangle/segment déformé au lieu d'un vrai coin de tarte. Nouveau
`detail::appendPieSliceFan()` construit l'éventail directement depuis le centre RÉEL passé en
paramètre (même esprit que le rejet de `fillPolygon()` pour le remplissage Area en Phase 1, motivé
cette fois-ci par une analyse a priori plutôt qu'un artefact visuel constaté).

**Angles en convention horaire partagée avec `sdl3::Renderer::drawPie()`/`generateArcPoints()`
(0°=droite, sens horaire croissant) — départ à -90° (haut), pas 0° (droite).** `computePieAngles()`
recalcule TOUJOURS depuis les valeurs courantes (même non-mise-en-cache que `resolveAxis()`, pour la
même raison : survol et rendu doivent voir exactement la même géométrie à chaque frame). Parts
masquées (bascule de légende, `PieSlice::visible`) EXCLUES du total ET de la sortie (angle {0,0},
`endDeg <= startDeg`) plutôt que rendues à angle inchangé mais invisibles — les parts restantes se
redistribuent pour occuper tout le cercle, comportement standard des bibliothèques de camemberts
interactifs. Valeurs négatives silencieusement traitées comme 0 (pas d'aire négative, documenté sur le
champ `PieSlice::value` lui-même).

**`pieHitTest()` : distance au centre puis angle via `atan2(dy,dx)`, sans conversion supplémentaire —
vérifié par le raisonnement (pas juste testé empiriquement) que la convention `atan2` en espace écran
(y vers le bas) coïncide EXACTEMENT avec la convention 0°=droite/sens-horaire déjà utilisée par
`generateArcPoints()`, donc aucune inversion de signe à gérer (contrairement aux bugs de span
écran/données de la Phase 3) — normalisation `rel` dans `[0,360)` par rapport à `startDeg` de chaque
part pour gérer le wrap-around proprement.**

**Pas de pan/zoom/box-zoom/reset en `PlotMode::Pie`** — décision explicite et directe (pas une
omission) : ces gestes n'ont de sens que pour une plage de données continue sur un axe, qu'un
camembert n'a pas. `InputSystem::dispatch` et `RenderSystem::drawWidget` branchent TÔT sur `p.mode`
(avant même le code de continuation de pan/box-zoom) et retournent après avoir traité légende +
survol de part — jamais d'entrée dans le code de navigation XY pour un plot Pie, pas de garde
supplémentaire nécessaire ailleurs.

**Survol de part + priorité légende-sur-tracé pour la surbrillance, réutilise `UiPlot::hoveredSeries`
existant (pas de nouveau champ) plutôt qu'un champ dédié `hoveredSlice`.** `hoveredIndex` n'a pas de
sens pour Pie (pas de "point dans une série") et reste à -1. Dans le rendu, `int hoveredSlice =
p.legendHover >= 0 ? p.legendHover : p.hoveredSeries;` — même hiérarchie implicite que XY (où
`legendHover` pilote déjà `dimFactor` indépendamment de `hoveredSeries`), rendue explicite ici parce
que les DEUX mécanismes convergent vers la MÊME visualisation (assombrir les autres parts) alors qu'en
XY ils pilotaient deux effets visuels séparés (dim des séries vs. curseur/tooltip).

**Builder API** : `pie(title)` (nouvelle factory, `PlotMode::Pie`, taille par défaut 260×220 — plus
carrée que le 300×200 de `plot()`, plus adaptée à un cercle) ; `addPieSlice(value, label, color)`
(couleur palette round-robin si non précisée, même mécanisme que `addSeries`) ; `setYError(span)`
(bascule la DERNIÈRE série ajoutée, même pattern que `useSecondaryY()` de la Phase 4).

**`drawWidget()` retourne tôt après le bloc Pie plutôt que de l'envelopper dans un `if/else` englobant
tout le reste du bloc XY (~150 lignes).** Vérifié explicitement que c'est sans risque : chaque entité
ne porte qu'UN SEUL type de widget dans cette architecture (jamais `Plot` + un autre composant de
rendu sur la même entité), donc les blocs `if (auto x = GetComponent<AutreType>(e); ...)` qui suivent
échoueraient de toute façon silencieusement — le `return` anticipé est strictement équivalent en
sortie, juste plus direct à lire que d'indenter tout le bloc XY existant dans un `else`.

Extended `tests/ui_smoke_test_8.cpp` une sixième fois en place : `computePieAngles` (proportions
exactes, parts masquées exclues du total ET de la sortie, valeurs négatives traitées comme 0) ;
`computePieCircleRect` (carré inscrit centré) ; `pieHitTest` (angle/distance, hors rayon) ;
construction via `pie().addPieSlice(...)` (nouvelle entité `plotG`) et `addScatterSeries(...)
.setYError(...)` (nouvelle entité `plotH`) ; interaction légende Pie via `InputSystem` réel
(survol/bascule, même mécanisme observable que XY) ; survol direct d'une part (point choisi
exactement au début angulaire de la part 0, `-90°`, pour éviter toute ambiguïté de frontière plutôt
que de recourir à la trigonométrie dans le test) ; rendu réel avec camembert + légende + étiquette de
part survolée + barres d'erreur simultanément. Full regression (`ui_smoke_test_1..8` tous exit 0,
coordonnées de `ui_smoke_test_1` inchangées, 6 exemples headless exit 124, zéro stderr) confirmée
verte, première tentative — aucun bug de signe cette fois, la géométrie du camembert ayant été conçue
dès le départ avec la leçon de la Phase 3 en tête (conventions d'angle/écran vérifiées PAR
RAISONNEMENT avant d'écrire le code, pas découvertes après un crash).

## Phase 6 — Heatmap (PlotMode::Heatmap) + Chandelier (PlotMode::Candle)

Deux dernières formes de données distinctes de la Décision #5 du plan. Décision d'architecture
CENTRALE de cette phase, arbitrée AVANT d'écrire le code (pas ajustée après coup) : **Heatmap reste
sans axes comme Pie, mais Candle RÉUTILISE `xAxis`/`yAxis` de `Plot`** — un chandelier sans
graduations de prix/temps visibles serait quasiment inutilisable, contrairement à un camembert ou une
grille de couleur qui se suffisent à eux-mêmes visuellement.

**Candle : axes toujours résolus depuis `candleBars`, JAMAIS depuis `PlotAxis::autoFit`/navigation —
nouveau couple `computeCandleAutoFit()`/`resolveCandleAxis()`, symétrique de
`computeAutoFit()`/`resolveAxis()` mais qui IGNORE délibérément `axis.autoFit`** (toujours recalculé,
comme si `autoFit` était figé à `true` en permanence) : décision de portée explicite, documentée dans
les deux fichiers — pas de pan/zoom/box-zoom/reset en `PlotMode::Candle` (même limitation que Pie/
Heatmap), donc aucune plage manuelle à préserver d'une frame à l'autre qui justifierait de respecter
un `autoFit=false`. Testé explicitement : poser `axis.autoFit=false` avec des `min`/`max` arbitraires
(999/1000) puis appeler `resolveCandleAxis()` renvoie quand même la plage calculée depuis les bougies,
pas les 999/1000 — vérifie que le champ est bien totalement ignoré, pas juste "généralement recalculé".

**`RenderSystem::drawWidget`'s bloc Candle DUPLIQUE (ne partage pas) les ~40 lignes de dessin de
graduations X/Y déjà écrites pour XY**, plutôt que de restructurer le code XY existant pour les
partager. Décision délibérée, pas un raccourci par paresse : les deux blocs sont structurellement
proches (même fonctions `generateTicks`/`dataToScreen`/`drawTextRaw`) mais PAS identiques (Candle n'a
ni axe Y secondaire ni légende, ses axes viennent de `resolveCandleAxis` pas `resolveAxis`) —
entremêler les deux au sein d'un même bloc géant avec des branches conditionnelles partout aurait
été plus risqué à modifier/vérifier que deux blocs séparés et complets, chacun lisible de bout en
bout. Même raisonnement déjà appliqué à `detail::appendThickSegment` (dupliqué entre `plot.hpp` et
`nodegraph.hpp`) — précédent explicitement cité dans le commentaire du code.

**Heatmap : dégradés de couleur (`Colormap::Viridis/Grayscale/CoolWarm`) implémentés comme des
interpolations RGB linéaires par morceaux entre 2-3 couleurs ancrées** (PAS de véritable carte
perceptuellement uniforme — hors de portée pour ce module, et non nécessaire pour une lecture
visuelle qualitative plutôt qu'une reproduction scientifique exacte). `resolveHeatmapRange()` suit
exactement le même non-cache que `resolveAxis()`/`resolveCandleAxis()` (recalculé depuis `values`
chaque frame si `autoRange`, jamais mis en cache) — troisième occurrence de ce pattern dans le
fichier, maintenant clairement établi comme LA convention du module pour toute plage
dérivée-des-données.

**`drawHeatmap()` a une garde explicite contre un `values` trop court** (`rows*cols` éléments
attendus) plutôt que de risquer un accès hors bornes — comparé en `int64_t` pour éviter un
débordement silencieux de `int` si `rows*cols` dépassait `INT_MAX` (improbable en pratique pour un
widget UI, mais le coût de la précaution est nul). Testé explicitement avec un `values` délibérément
trop court : la fonction ne dessine simplement rien, ne plante pas — vérifié avec un VRAI renderer
(pas juste "ne lève pas d'exception", cf. discipline déjà établie de toujours passer par un rendu réel
pour ce genre de garde).

**Survol réutilise `UiPlot::hoveredSeries` une TROISIÈME fois pour deux sens différents** (index de
cellule `r*cols+c` pour Heatmap, index de bougie pour Candle) — même choix que Pie en Phase 5 (pas de
nouveau champ dédié par mode). `heatmapHitTest()` fait un test de rectangle simple (division entière
de la position relative par la taille de cellule) ; `candleNearestBar()` teste la distance
HORIZONTALE seule (pas la distance au point comme `plotNearestPoint()`) — une bougie occupe toute la
hauteur de sa mèche, donc seule la proximité en X a un sens pour "quelle bougie est-ce que je
survole".

**`plotLegendItemCount()` étendu à un `switch` explicite sur les 4 modes** (au lieu du ternaire à 2
branches de la Phase 5) — Heatmap et Candle renvoient tous deux 0 explicitement (pas juste "tombent
dans le cas par défaut XY et lisent un `series` vide qui donne 0 par coïncidence") : plus robuste si
`PlotMode` gagne un cinquième mode un jour, et plus lisible que de compter sur un vecteur vide comme
mécanisme implicite de "pas de légende".

**Builder API** : `heatmap(title)` (`showLegend=false` explicite dès la construction, pas seulement
un `plotLegendItemCount` qui donnerait 0 — évite tout artefact visuel si quelqu'un réactivait
`showLegend` sans réfléchir) + `setHeatmapData(rows, cols, values, colormap)` ; `candlestick(title)`
(même `showLegend=false` explicite, `yAxis.tickPosition=Left` posé comme `plot()` le fait) +
`addOhlcBar(x, open, high, low, close)`.

Extended `tests/ui_smoke_test_8.cpp` une septième fois en place : `heatmapColor` (valeurs exactes aux
points d'ancrage — pas d'approximation floue, les couleurs ancrées sont des CONSTANTES connues) ;
`resolveHeatmapRange` (auto vs manuel) ; `drawHeatmap` avec un VRAI renderer et des données
insuffisantes (garde, pas de crash) ; `heatmapHitTest` (4 cellules + hors grille) ; construction via
`heatmap().setHeatmapData(...)` (nouvelle entité `plotI`) ; `computeCandleAutoFit`/`resolveCandleAxis`
(y compris le test `autoFit=false` ignoré, cf. ci-dessus) ; `computeCandleBodyRect`/`candleNearestBar`
(trois bougies, deux positions de survol + une hors-portée) ; construction via
`candlestick().addOhlcBar(...)` (nouvelle entité `plotJ`) ; intégration réelle via `InputSystem` pour
les deux nouveaux modes (survol de cellule, survol de bougie, effacement hors-widget) ; rendu réel
avec heatmap + chandelier simultanément affichés. Full regression (`ui_smoke_test_1..8` tous exit 0,
coordonnées de `ui_smoke_test_1` inchangées, 6 exemples headless exit 124, zéro stderr) confirmée
verte, première tentative — les 4 modes de `PlotMode` sont maintenant tous livrés (XY, Pie, Heatmap,
Candle), Phase 7 (dernière du chantier) ne touche plus de nouveau mode, seulement `onCustomDraw` +
l'exemple applicatif + la clôture des tests.

## Phase 7 (finale) — onCustomDraw + exemple applicatif + clôture

Dernière phase du chantier plots. `PlotSeries::onCustomDraw` existait comme champ depuis la Phase 0
(`drawPlotSeries()` le respecte déjà : `if (s.onCustomDraw) { s.onCustomDraw(...); return; }`) mais
n'avait JAMAIS été exercé par un test ni exposé via le builder — même motif « déclaré tôt, testé à la
fin » déjà suivi pour l'initiative Node-Graph précédente (pins/connexions `onCustomDraw` déclarés en
Phase 0 de ce chantier-là, testés en Phase 9).

**`setOnCustomDraw()` ajouté au builder**, même pattern « modifie la DERNIÈRE série ajoutée » que
`useSecondaryY()`/`setYError()` des phases précédentes — cohérence maintenue jusqu'au bout plutôt que
d'improviser une convention différente pour ce dernier champ.

**Test dédié avec compteur d'appels + capture des arguments reçus**, plutôt qu'un simple "ça ne
plante pas" : un callback statique incrémente un compteur et enregistre `plotRect`/`xAxis`/`yAxis` à
chaque appel, vérifié à `0` avant tout rendu, `1` après un premier `render.run()`, `2` après un
second — confirme non seulement que l'échappatoire est appelée, mais qu'elle l'est EXACTEMENT une
fois par rendu (pas zéro, pas deux fois par accident d'un double parcours), et que les arguments reçus
sont plausibles (rectangle de taille positive, axes avec `max > min`) — assez pour qu'un appelant réel
puisse s'en servir pour dessiner quelque chose de correctement positionné, ce que l'exemple applicatif
démontre ensuite concrètement.

**`examples/ui_plot_demo.cpp`** : galerie de 7 plots (2 rangées) couvrant CHAQUE famille livrée par le
chantier — Ligne+Aire+barres d'erreur, Bar+Stem, Nuage+axe Y secondaire, Camembert, Heatmap,
Chandelier, `onCustomDraw` (dégradé de couleur par segment, sinusoïde dessinée point par point avec
une couleur interpolée bleu→rouge selon la position X — démontre concrètement que l'échappatoire
« widget configurable » de la demande initiale de l'utilisateur permet un rendu que les types de
série intégrés ne permettent pas). Construit sur le pattern `ui::Ui` façade (comme `ui_minimal.cpp`,
pas les systèmes bruts de `ui_node_graph_demo.cpp` — aucune interleaving particulière requise ici).

**Deux pièges de mise en page découverts en écrivant l'exemple (aucun n'est un bug du widget, les
deux corrigés côté exemple) :**
1. **Axes proportionnellement identiques par coïncidence de données.** Premier jeu de données pour
   "Nuage + axe Y secondaire" avait `sy2 = sy1_extrêmes × 100` — l'auto-fit des deux axes (même
   marge de 5%) donnait alors des plages EXACTEMENT proportionnelles entre elles, rendant le rendu
   (pourtant correct, vérifié via capture d'écran zoomée) visuellement indiscernable d'un rendu qui
   utiliserait par erreur l'axe primaire pour les deux séries. Corrigé en choisissant des données
   DIVERGENTES (primaire monte, secondaire descend, non proportionnelles) — la capture d'écran
   confirme alors sans ambiguïté que les deux séries suivent des échelles indépendantes. Leçon
   générale : choisir des données de démonstration délibérément "moches"/asymétriques pour un axe
   secondaire, jamais des multiples simples les uns des autres, sous peine de rendre la fonctionnalité
   invisible même quand elle marche.
2. **Légende superposée (TopRight, comportement par défaut depuis la Phase 2) chevauchant le titre
   centré** sur le camembert, qui est presque carré (peu de place en haut). Corrigé dans l'exemple par
   `.legend(LegendPosition::Right, false)` (espace réservé plutôt que superposé) — pas un correctif au
   widget, juste un choix de configuration plus adapté à CE plot précis (un plot plus large qu'haut
   n'aurait pas eu ce problème avec le défaut).

**Vérification visuelle RÉELLE effectuée, pas seulement "exit 124 sans crash"** — nouveauté de
discipline pour ce chantier : lancé l'exemple sous un VRAI serveur X (Xvfb, disponible dans
l'environnement) plutôt que le pilote vidéo `dummy` habituel des tests headless, capturé une capture
d'écran (`xwd` + `ffmpeg` vers PNG, aucun outil dédié existant dans le projet), et inspectée
visuellement (y compris un zoom Python/PIL sur un panneau spécifique pour trancher le piège n°1
ci-dessus) — a permis de détecter les deux pièges de mise en page qu'aucune assertion numérique
n'aurait révélés (les DEUX rendus étaient géométriquement corrects du point de vue des tests). Pas
d'infrastructure de capture d'écran pérenne ajoutée au projet (usage ponctuel, fichiers temporaires
nettoyés après coup) — à refaire à la main si un futur chantier UI a besoin de la même vérification.

Extended `tests/ui_smoke_test_8.cpp` une huitième et dernière fois en place (Phase 7) : construction
via `addLineSeries(...).setOnCustomDraw(...)` (vérifie qu'une série SANS `onCustomDraw` et une AVEC
coexistent sur le même plot sans interférence) puis compteur d'appels + arguments capturés comme
décrit ci-dessus. Full regression (`ui_smoke_test_1..8` tous exit 0, coordonnées de `ui_smoke_test_1`
inchangées, les 7 exemples — les 6 précédents + le nouveau `ui_plot_demo` — headless exit 124, zéro
stderr) confirmée verte.

## Statut à la fin du chantier « Widgets de plots/graphiques 2D façon ImPlot »

**Chantier TERMINÉ (Phases 0 à 7, 8 phases au total)** — `src/ui/plot.hpp` (widget + fonctions libres,
~1050 lignes), intégré dans `systems.hpp`/`factory.hpp`, testé dans `tests/ui_smoke_test_8.cpp`
(~950 lignes, grandi phase par phase), démontré dans `examples/ui_plot_demo.cpp`. Livré : 8 types de
séries à axes partagés (Line/Scatter/Bar/BarH/Histogram/Area/Stem/Step) + barres d'erreur Y, axe Y
secondaire indépendant, échelle logarithmique par axe, pan/zoom/zoom-rectangle/reset, légende
interactive (survol/bascule) à 8 positions × overlay-ou-réservé, positionnement des graduations
configurable par bord × overlay-ou-réservé (extension demandée explicitement par l'utilisateur en
cours de Phase 2), 4 modes de plot (XY/Pie/Heatmap/Candle), et l'échappatoire `onCustomDraw` pour un
rendu 100% personnalisé par série. Architecture décidée dès la planification et tenue jusqu'au bout :
AUCUN sous-système dédié (contrairement au Node-Graph) — un unique widget auto-dessiné intégré aux
points d'extension existants de `RenderSystem`/`InputSystem`, cohérent avec `UiTable`/`UiCalendar`.

Bugs réels trouvés et corrigés PENDANT le chantier (pas après coup) : le clamp de span écran non
préservant le signe (`screenToData`/`applyPlotAxisZoom`, Phase 3, unifié dans `safeScreenSpan()`) —
seul bug ayant nécessité un crash de test pour être détecté ; tous les autres pièges (approximation par
centroïde de `fillPolygon()` pour le camembert, Phase 5 ; pièges de mise en page de l'exemple, Phase 7)
ont été anticipés PAR RAISONNEMENT avant que le code ne les manifeste. Aucune régression sur les 7
autres exemples ni les 7 smoke tests préexistants à aucun moment du chantier.

## Blocage des évènements par ordre d'affichage (z-order) — 2026-09-21

**Demande :** « bloquer la propagation des évènements au widget le plus en avant lorsque plusieurs
widgets se superposent » — l'exemple donné étant un `UiMenuItem` survolé qui doit être le SEUL à
traiter l'évènement, les widgets derrière lui restant inertes.

**Le défaut :** `InputSystem::Dispatch` enchaîne ~25 passes `world.Query<Widget, UiComputed>`
indépendantes, chacune décidant du survol avec `hitOk(e, c)` = rect écran + clip + non caché + non
désactivé + pile modale. Aucune de ces conditions ne regarde ce qui est dessiné PAR-DESSUS : deux
widgets superposés devenaient tous les deux `hovered`, et le gagnant d'un clic dépendait de l'ordre
des TYPES dans Dispatch (`clickConsumed` / `wheelConsumed` sont posés dans cet ordre-là), pas de la
profondeur d'affichage. Un menu déroulé par-dessus une barre d'outils surlignait donc le bouton
caché derrière lui, et le clic pouvait partir au mauvais widget.

**Le mécanisme ajouté** — `HitTestIndex` (+ le raccourci `HitTestTopMost(world, point)`), dans
`components.hpp` (à côté de `IsHiddenRecursive`/`IsDescendantOrSelf`, et PAS dans `systems.hpp` :
`nodegraph.hpp` en a besoin aussi et n'inclut que `factory.hpp`). Il rejoue **exactement**
l'ordre de dessin de `RenderSystem::Run` et répond au premier candidat, du plus en avant au plus en
arrière :
listes déroulantes des combos ouverts (leur rect est `UiComboBox::DropdownRect`, pas celui du
widget) → overlays `AttachLayout::FIXED` triés par `UiOverlayLayer::order` décroissant → arbres
normaux du dernier au premier, enfants avant parents. Les mêmes sorties anticipées que `DrawTree`
sont reproduites (UiHidden, pas de `UiComputed`, clip dégénéré, sous-arbre FIXED sauté pendant la
descente normale) : « ce qui est cliquable » colle à « ce qui est visible ».

`InputSystem` calcule cette cible UNE fois par évènement (`Dispatch`) et par frame (`Tick`, pour que
l'infobulle affichée soit celle du widget vu), la mémorise dans `frontMost`, et `hitOk` exige en
plus `IsFrontMost(e)`. Les ~7 hit-tests écrits à la main hors de `hitOk` (en-tête d'un `UiTreeNode`,
`UiTable`, `UiCalendar`, en-tête d'`UiExpander`, barre d'onglets d'`UiTabView`, liste d'un combo,
pouce d'auto-scrollbar) ont reçu le même test.

**Décision centrale : la cible ET SES ANCÊTRES passent.** Un widget composite n'est presque jamais
la feuille dessinée en dernier — un `UiButton` est recouvert par son propre libellé, un conteneur
scrollable par tout son contenu. N'autoriser que la feuille exacte rendrait la moitié des widgets
inertes (et ferait perdre la molette à tout conteneur scrollable dès que le curseur est sur un
enfant). En revanche un FRÈRE resté derrière ne passe plus : c'est précisément le blocage demandé.
Corollaire utile pour les menus : le `UiMenuBarItem` reste surligné pendant que son popup est
survolé, puisque le popup est son enfant dans l'arbre.

**Trois choix de compatibilité, délibérés :**
- cible invalide (pointeur au-dessus du vide, ou entité montée à la main hors de tout arbre dessiné
  — cas de certains tests) → aucun blocage, comportement historique ;
- un widget DÉSACTIVÉ au premier plan reste inerte mais continue de bloquer ce qui est derrière lui
  (il est visible, donc il capte le pointeur) ;
- `UiPointerThrough` (nouveau marqueur, `.PointerThrough()` côté builder) est l'échappatoire façon
  `pointer-events: none` : le widget est dessiné mais jamais retenu comme cible, ses enfants si.
  Volontairement NON récursif, contrairement à `UiHidden`/`UiDisabled`.

**Deux systèmes qui hit-testent hors d'`InputSystem`, mis au même niveau :**
- `NodeGraphSystem::HandleEvent` (autonome par conception) : calcule `pointerBlocked` = la cible de
  premier plan n'appartient à AUCUN `UiNodeGraphCanvas`, et ignore alors survol, menu contextuel et
  démarrage d'interaction — mais JAMAIS les gestes déjà en cours (connexion, drag, pan, marquee),
  qui ont capté la souris au départ et doivent pouvoir sortir du canvas.
- `examples/level_editor_demo/panels.hpp` : `PointerOverViewport(x, y)` remplace le simple test de
  rectangle, et `RayAt` passe par lui — sans quoi un clic dans un menu ouvert AU-DESSUS de la vue 3D
  sélectionnait aussi l'objet situé derrière le menu.

`InputSystem::FrontMostWidget()` / `PointerOverUi()` exposent la cible aux applications (c'est ce
dont un viewport 3D a besoin pour ne pas traiter un clic capté par l'UI).

**Tests :** `tests/ui_zorder_smoke_test.cpp` (9 tests) — item de menu au-dessus d'un bouton (le cas
de la demande, survol ET clic), bouton toujours réactif là où rien ne le couvre, pointeur au-dessus
du vide, ancêtre qui garde la molette, `UiPointerThrough` avec et sans le marqueur, `UiOverlayLayer`
qui tranche entre deux overlays, popup fermé qui ne bloque rien, widget désactivé qui bloque quand
même, liste déroulante d'un combo ouvert. Vérifié utile : en neutralisant `IsFrontMost`, 5 des 9
tests échouent (dont celui de la demande) — les 4 autres gardent leur valeur de non-régression.

**Seule régression trouvée dans la suite existante — et elle est instructive :** `ui_smoke_test_5`
posait son bouton de contrôle final à `Offset(0, 500)`, c'est-à-dire PILE sur un bouton de la page
de scène active (rect mesuré : page à `8,493.4 160x32.2`, bouton de test à `0,500 80x32`). Le clic
partait désormais au bouton de la page, qui est dessiné APRÈS. Le chevauchement était accidentel —
ce test vérifie l'intégrité de l'ECS après réentrance, pas le z-order — donc le bouton a été déplacé
à `Offset(600, 500)`, hors de la page, avec un commentaire expliquant pourquoi sa position compte
maintenant. **Rien d'autre n'a bougé :** sorties de `ui_smoke_test_1/2/4/6/7/8` et
`ui_shader_effects_smoke_test` identiques octet pour octet avant/après (seul `ui_smoke_test_3`
diffère, sur ses chiffres de performance).

**Limite connue, héritée du rendu :** entre deux ARBRES RACINES qui se recouvrent (aucun des deux en
`Fixed()`), celui « au-dessus » est celui que `RenderSystem::Run` dessine en dernier, c'est-à-dire
l'ordre d'itération des archétypes — pas l'ordre de spawn. Le hit-test reproduit fidèlement ce choix
(c'est tout l'intérêt), mais il ne le rend pas plus prévisible : pour un empilement VOULU, il faut
`.Fixed()` + `.OverlayOrder(n)`, qui est trié explicitement. C'est exactement ce qui rendait le
chevauchement de `ui_smoke_test_5` inoffensif jusqu'ici et visible maintenant.

## Optimisation du hit-test de z-order — 2026-09-21 (même jour, régression signalée par l'utilisateur)

**Symptôme :** `build/bin/level_editor_demo` est passé de 60 à 25 images/s dès l'ajout du blocage
par ordre d'affichage.

**Cause, mesurée et non devinée** (banc d'essai sur 1200 widgets, build de debug `-O0` + ASan, comme
le binaire de l'utilisateur) : la première version reconstruisait l'ordre de dessin **à chaque
appel** — deux requêtes `Query<>` balayant tout le registre, puis une descente d'arbre faisant
plusieurs recherches de composants ET une allocation de vecteur par conteneur. Coût : **5 à 7 ms par
appel**. Or l'éditeur en fait DEUX par mouvement de souris (`InputSystem::Dispatch` + `RayAt` du
manipulateur, panels.hpp:283) plus un par image dans `Tick` : quelques évènements suffisaient à
manger un budget d'image entier.

Deuxième cause, plus discrète : `IsFrontMost` appelait `IsDescendantOrSelf` — donc remontait la
chaîne des parents, une recherche de composant par niveau — pour CHAQUE widget de CHACUNE des ~25
passes `Query<>` de `Dispatch`. Et il avait été placé AVANT le test géométrique dans `hitOk`,
c'est-à-dire avant le seul test qui écarte 99 % des widgets pour trois comparaisons de flottants.

**Corrections :**
1. `HitTestIndex` : l'ordre de dessin est mis en cache sous forme de `vector<{entité, screen, clip}>`
   et reconstruit seulement quand la géométrie change — `LayoutSystem::PassCount()` (qui existait
   déjà) sert de version, complété par `EntityCount()` pour les créations/destructions hors passe de
   layout, et `Invalidate()` pour le reste. Un test n'est plus qu'un parcours ARRIÈRE d'un vecteur
   contigu : aucune allocation, aucune recherche de composant tant qu'aucun candidat ne contient le
   point. Les états DYNAMIQUES (`UiHidden`, `UiPointerThrough`, combo ouvert) restent lus en direct
   sur les rares candidats retenus — afficher/masquer ne coûte donc aucune reconstruction.
2. `IsFrontMost(e)` compare à une **chaîne pré-calculée** (la cible + ses ancêtres, quelques entrées,
   construite une fois par évènement) au lieu de remonter les parents par widget.
3. Dans `hitOk` et les 7 hit-tests en ligne, le test de rectangle passe EN PREMIER, avant tout ce qui
   remonte une chaîne de parents.
4. `InputSystem::FrontMostAt(world, layout, point)` partage l'index avec l'application — l'éditeur de
   niveau l'utilise au lieu de `ui::HitTestTopMost`, qui reconstruit à chaque appel et n'est plus
   destiné qu'aux appels isolés et aux tests. `NodeGraphSystem` garde son propre index membre.

**Résultat mesuré sur les mêmes 1200 widgets** — `HandleEvent`(mouvement souris) : 0,233 ms avant la
fonctionnalité, 7,40 ms après, **0,304 ms** maintenant ; `Tick` : 0,029 / 4,96 / **0,085 ms**. Il
reste ~0,07 ms par évènement au-dessus de l'état d'origine, soit le coût du parcours de l'index
lui-même. À noter : l'index ne contient que ce qui est réellement dessiné (271 entrées pour 1201
entités dans le banc d'essai — les rangées qui débordent sous la fenêtre ont un clip vide et sont
écartées, exactement comme `DrawTree` les écarte).

**Deux tests de non-régression ajoutés** dans `ui_zorder_smoke_test.cpp` : un test STRUCTUREL (le
compteur `HitTestIndex::Revision()` ne bouge pas sur 50 évènements + ticks, puis bouge après une
passe de layout, puis après une création d'entité) — déterministe, donc pas de mesure de temps
fragile — et un test de temps à borne RELATIVE (un évènement doit coûter moins d'un dixième d'une
passe de layout sur la même UI : rapport ~0,4 avant, ~0,01 maintenant).

## Mesure de texte et codes d'échappement — 2026-09-21

**Le défaut**, mesuré et non supposé (DejaVuSans 16 pt, cf.
`tests/ui_text_metrics_smoke_test.cpp`) : SDL_ttf ne traite PAS les codes
d'échappement de la même façon selon qu'on mesure ou qu'on dessine.

| texte | `TTF_GetStringSize` (mesure) | `TTF_Text` (dessin) |
|---|---|---|
| `"abc"` | 29 × 19 | 29 × 19 |
| `"abc\nabc"` | **68 × 19** | **29 × 38** |
| `"a\tb"` | 30 × 19 | 30 × 19 (boîte « glyphe manquant ») |
| `"a\r\nb"` | **40 × 19** | 10 × 38 |

Autrement dit la mesure comptait chaque `\n`, `\r`, `\t` comme un glyphe
MANQUANT d'environ 10 px et restait sur UNE ligne, alors que le rendu cassait
bien la ligne sur `\n`. Le layout réservait donc une boîte d'une seule ligne,
trop large, dans laquelle le texte débordait par le bas. L'heuristique sans
police avait le même défaut (elle comptait `\n` comme un caractère) et, en
prime, comptait des OCTETS : « Réglages » (8 caractères, 10 octets) était
surestimé de 25 %.

**La correction, en deux temps** (`lib/include/ui/systems.hpp`) :
1. `NormalizeDisplayText()` met la chaîne sous une forme où mesure et dessin ne
   peuvent plus diverger — `\r\n` et `\r` → `\n`, tabulations développées
   jusqu'au taquet suivant, autres caractères de contrôle retirés. Chaîne sans
   contrôle : rendue telle quelle, sans allocation.
2. `MeasureTextBlock()` mesure LIGNE PAR LIGNE : largeur = la plus large,
   hauteur = nombre de lignes × hauteur de ligne.

`LayoutSystem::measureText` **reste le point de branchement des applications**
et ne mesure qu'UNE ligne : le découpage est fait une fois, dans
`LayoutSystem::MeasureText()`, donc les dix exemples qui branchent une mesure
TTF en profitent sans changer une ligne. `RenderSystem` normalise avant
`sdl3::Text::Create` (et met la forme normalisée en cache), sans quoi ce qui
est dessiné ne correspondrait plus à ce qui a été réservé.

**Décision : taquets comptés en CARACTÈRES, pas en pixels.** Un libellé est un
seul objet `TTF_Text` ; aligner sur des taquets en pixels imposerait de
dessiner segment par segment. En développant les tabulations en espaces une
fois pour toutes, mesure et dessin voient exactement la même chaîne —
l'alignement est approximatif en police proportionnelle, mais *la place
réservée est la place occupée*, qui est la propriété dont dépend tout le
layout.

**Piège trouvé en écrivant le code** : la hauteur de ligne ne doit PAS être le
maximum entre celle rapportée par la mesure branchée et l'heuristique
(`1,3 × taille`). L'heuristique dépasse la hauteur réelle d'une police donnée
(20,8 contre 19 pour DejaVuSans 16), et la propriété « mesuré = dessiné »
tombait. L'heuristique ne sert que si la mesure ne rapporte AUCUNE hauteur.

**Conséquence sur les champs éditables** : ils placent curseur, sélection et
clics sur une grille de cellules fixes indexée par OCTETS. Depuis que le dessin
développe les tabulations, il a fallu passer cette grille en colonnes
D'AFFICHAGE (`DisplayColumn`, `DisplayColumns`, `ColumnToByteOffset`) — ce qui
corrige du même coup le placement du curseur dans un texte accentué (un « é »
occupait deux cellules pour deux octets). Un clic AU MILIEU d'une tabulation
retombe sur le bord le plus proche, comme dans un éditeur.

`SplitLines` avale par ailleurs un `\r` de fin de ligne : un texte collé depuis
un fichier CRLF affichait sinon une boîte au bout de chaque ligne.

**Trois taquets à garder égaux** : `LayoutSystem::tabStop` (ce qui est
réservé), `RenderSystem::tabStop` (ce qui est dessiné), `InputSystem::tabStop`
(où tombe le clic).

## Clip : un widget ne peint plus hors de sa propre boîte — 2026-09-22

**Symptôme** (capture de l'utilisateur, champ de script de l'éditeur) : la
dernière ligne du texte était dessinée SOUS la bordure du champ, sur le fond du
panneau, et la première ligne passait PAR-DESSUS la bordure haute.

**Cause.** `UiComputed::clip` est la région héritée du PARENT ; `childClip`
(= `clip ∩ screen`) est la partie visible du widget lui-même. `RenderSystem::
DrawTree` posait `clip` avant de dessiner le widget, et ne réservait `childClip`
qu'à ses ENFANTS. Un widget qui peint son propre contenu — champ de texte,
tableau, liste, tracé — pouvait donc déborder de lui-même tant qu'il restait
dans son parent. `UiTable` et `ListBox` resserraient déjà le clip à la main ;
les champs de texte, non.

**Correction** (`lib/include/ui/systems.hpp`) :
1. `DrawTree` pose désormais `childClip` pour le dessin du widget, ses
   auto-scrollbars et son voile de désactivation. Les widgets qui doivent
   déborder (liste d'un combo, infobulle, overlays `Fixed`) passent par des
   passes séparées de `Run()`, après `ClearClipRect()` : ils ne sont pas
   affectés.
2. `UiInput`/`UiInputArea` resserrent en plus leur contenu (sélection, lignes,
   curseur) à l'INTÉRIEUR de leur bordure (`InsetRect(s, FIELD_BORDER)`) — une
   ligne partiellement visible se coupe net sur le bord au lieu de chevaucher
   le trait.
3. `UiPlot` intersecte son clip au lieu de l'écraser (`ToClipRect(s)` seul
   ignorait la région héritée : un tracé dans un panneau défilant peignait
   par-dessus).
4. `ToClipRect()` remplace `{int(x), int(y), int(w) + 1, int(h) + 1}` : cette
   forme tronquait l'origine PUIS ajoutait un pixel plein, soit jusqu'à deux
   pixels de débordement en bas et à droite. Arrondi vers l'extérieur
   (floor/ceil) : au plus un pixel, et sans accumulation.

**Ce que le clip strict a révélé** — un second défaut, invisible jusque-là :
`DrawTextCentered` décalait le texte de 8 px vers l'intérieur de sa boîte. Cette
marge n'est RÉSERVÉE que par les widgets dont la mesure l'ajoute (un bouton
ajoute 24 px) ; la boîte d'un `UiLabel`, elle, fait exactement la largeur du
texte, qui débordait donc de 8 px à droite. Une fois le clip resserré, tous les
libellés de l'outliner perdaient leur dernière lettre. `TextOriginX()` rabote
maintenant la marge quand il n'y a pas la place (et, pour un texte plus large
que sa boîte, le cale à GAUCHE : on préfère couper la fin, un libellé restant
identifiable par son début).

**Une exception, explicite.** Le halo de lueur « verre » (`DrawGlowRing`,
thème Aero) est dessiné AUTOUR de la boîte par conception et comptait sur le
clip du parent. Les widgets qui en portent un reçoivent donc exactement
`GLOW_MAX_OUTSET` de marge — constante à garder égale au plus grand `outset`
des anneaux. C'est le SEUL dessin autorisé hors de la boîte.

**Piège rencontré en corrigeant** : lire `c.childClip` dans le rendu cassait
`ui_shader_effects_smoke_test`, qui construit un `UiComputed` À LA MAIN et ne
pose que `screen` et `clip` (le champ `childClip` restait vide, donc le widget
n'était plus dessiné du tout). La région est donc RECALCULÉE (`clip ∩ screen`)
plutôt que lue : même valeur après une passe de layout, mais robuste pour tout
code qui remplit un `UiComputed` lui-même.

**Tests** : `tests/ui_clip_smoke_test.cpp` (12). Trois échouent si l'on
neutralise les correctifs. Le rendu lui-même est testé SANS GPU grâce à un
backend d'enregistrement (`IUiRenderBackend` qui retient le clip courant et les
rectangles peints) : on vérifie que le clip actif pendant qu'un widget peint est
bien sa propre boîte visible, et qu'un contenu plus grand que son conteneur
reste borné à celui-ci. Le cas discriminant est un PETIT widget dans un GRAND
panneau : c'est là que région héritée et boîte propre diffèrent.

## Modes de débordement du texte — 2026-09-22

Cinq façons d'afficher une chaîne plus large que son widget, choisies au
`WidgetBuilder` (`TextClip`, `TextEllipsis`, `TextScroll`,
`TextWrap(align)`, `TextMarquee(speed, pause)`), portées par un composant
`UiTextOverflow`. Règle commune : **aucun mode ne contraint la largeur** — un
widget libre s'élargit toujours jusqu'à son texte, le débordement n'existe
qu'une fois la largeur bornée (`W`, `MaxSize`, ou étirement du parent).

**Ce qui a été réutilisé plutôt que réécrit** — c'est l'essentiel du travail :
- `SCROLL` ne dessine aucune barre : le layout déclare `UiRect::content.x` =
  largeur du texte, et l'auto-scrollbar existante (plus la molette et le pouce
  déjà gérés par `InputSystem`) fait le reste. Idem pour `WRAP` en vertical
  quand la hauteur est bornée.
- La normalisation des échappements et la mesure par lignes (chantier
  précédent) servent de base à la découpe.

**Ce qu'il a fallu ajouter :**
- `WrapTextToWidth()` : découpe par MOTS mesurée pour de vrai, là où
  `WrapText` (champs de saisie) coupe au caractère sur une grille fixe. Un mot
  plus long qu'une ligne est coupé au caractère, sur une frontière UTF-8.
- `TextAlign::Justify` : l'espace restant est réparti entre les mots, **sauf
  sur la dernière ligne d'un paragraphe** (l'étirer rendrait la phrase
  illisible) — d'où le drapeau `WrappedLine::lastOfParagraph`.
- `RenderSystem::TruncateWithEllipsis()` (publique : c'est une mesure, pas un
  dessin) : recherche DICHOTOMIQUE du plus long préfixe qui tienne avec « … ».
  Une recherche linéaire mesurerait autant de préfixes qu'il y a de lettres, à
  chaque image et pour chaque libellé tronqué.

**La hauteur d'un texte replié ne peut pas être mesurée par `Measure()`** :
elle dépend de la largeur que le widget REÇOIT. Elle est donc calculée dans
`PlaceLinear`, à deux endroits distincts — pour une COLONNE dès la première
boucle (la largeur est l'axe transverse, déjà connue), pour une RANGÉE après
la répartition des `Grow` (la largeur est l'axe principal). Dans les deux cas
le calcul n'invalide rien de déjà décidé.

**Piège de conception évité** : `InputSystem::Tick` anime le défilement
automatique mais n'a aucune police sous la main ; mesurer le texte là aurait
donné une amplitude fausse dès que la mesure réelle s'écarte de l'heuristique.
La largeur est donc écrite par le LAYOUT dans `UiTextOverflow::textWidth`.

**Limite connue, révélée par la démonstration** : le rendu dessine toujours à
la taille de la police CHARGÉE, alors que la mesure branchée par les
applications met à l'échelle selon `fontSize` (`font.Measure(s) * fs/FONT_PT`).
Un widget dont le `fontSize` diffère de la police chargée voit donc sa boîte
sous-estimée — et, depuis que le clip est strict, son texte tronqué. Corriger
demande que `RenderSystem` honore réellement `fontSize` (fontes en cache par
taille), ce qui dépasse ce chantier.

**Tests** : `tests/ui_text_overflow_smoke_test.cpp` (15) — découpe par mots,
mot imprenable, sauts explicites, hauteur repliée, ce que le layout réserve
dans chacun des cinq modes, barres de défilement attendues, aller-retour du
défilement automatique borné au débordement, pause aux extrémités, texte qui
tient qui ne bouge pas, et troncature aux frontières de caractères. Contrôle
visuel des cinq modes par capture (`/tmp` — non versionnée).

## Ajouts pour l'éditeur de jeu — 2026-09-26

Trouvés ou demandés en refaisant l'interface de `game_editor_demo` :

- **Menus contextuels** : `UiCallbacks::onContextMenu(x, y)` / `.OnContextMenu`
  (le plus PROCHE porteur de la chaîne de premier plan est notifié ; rien dans
  un popup ouvert ni derrière une modale), `UiFactory::ContextMenu(items…)`,
  `ui::OpenPopupAt` / `Ui::OpenPopupAt`. Un popup racine est ramené dans la
  fenêtre (`LayoutSystem::Run`). Tests : `ui_context_menu_smoke_test` (7).
- **Éditeur de code** dans `UiInputArea` : gouttière de numéros, chasse fixe
  (`Ui::RegisterMonospaceFont`), ligne du curseur, coloration par morceaux
  (`UiSyntaxHighlighter`, lignes VISIBLES seulement, tabulations développées),
  largeur de cellule MESURÉE sur la police dessinée et publiée dans le
  composant (clic et curseur tombent sous le caractère), défilement
  horizontal qui suit le curseur, `followTail` (un journal suit sa fin, un
  document de code reste en haut). `.CodeEditor(highlighter)`. Tests :
  `ui_code_area_smoke_test` (8).
- **Texte à la taille du widget** : le rendu dessinait TOUT texte et toute
  icône à la taille de chargement de la police. `RenderSystem::FontAt(base,
  taille)` garde une copie redimensionnée par taille (`sdl3::Font::Copy`,
  `PointSize`) ; la taille du widget en cours de dessin s'applique partout ;
  une icône prend `UiIcon::size`. Origine du texte arrondie au pixel (une
  demi-coordonnée rendait le texte flou, comme dédoublé).
- `UiTheme::Studio()`, `WidgetBuilder::OnTextChange`, `UiViewport3D`
  `continuous = false` (rendu à la demande — vignettes) et `lighting` propre,
  viewport masqué non rendu.
- **Bugs corrigés** : `ColorPicker` divisait encore l'alpha par 255 (vestige
  de `sdl3::Color`) — toute retouche rendait la couleur transparente (tests :
  `ui_color_picker_smoke_test`, 3) ; `Renderer::Fill/DrawRoundedRect` sans
  borne sur le rayon (un carré de 8 px au rayon 8 sortait en « × »,
  `ClampCorners`).

Hors `ui::`, dans la même session : `physics::CollideCapsuleBox` implémenté
(c'était un « pas de contact » documenté : une capsule traversait tout sol en
boîte) + deux erreurs de signe de `CollideSphereBox` (point de contact du
mauvais côté de la sphère, normale inversée quand le centre est dans la
boîte) — 8 tests dans `physics_smoke_test` ; `data::JsonDocument` écrit les
flottants sous leur forme la plus COURTE exacte (`1.65`, plus
`1.6499999999999999`) ; `scene::NodeTree::StructureStamp()` ;
`sdl3::Window::SetRelativeMouseMode`.

## Bordures, barres de défilement, focus clavier, thème — 2026-09-26

Quatre corrections demandées ensemble par l'utilisateur, toutes testées par
`tests/ui_focus_scroll_smoke_test.cpp` (17 tests).

- **Bordures (border-box).** `LayoutSystem::Pass` relève dans `Snapshot()` la
  bordure DESSINÉE de chaque `UiPanel` (couleur de bordure résolue présente →
  `BorderSides(rs)`, épaisseur `.top` uniforme, la même que `RenderSystem`).
  `AutoSize` l'ajoute à la taille, `Place` place le contenu et découpe les
  enfants À L'INTÉRIEUR. Piège : la classe `root-panel` donne une bordure à
  TOUT `UiPanel`, donc aussi à toute colonne passée par `.Bg()` — 1 px de
  retrait partout, voulu. Le style est lu au `Snapshot` : une bordure ajoutée
  à chaud ne décale le contenu qu'au prochain layout dirty.
  `sdl3::Renderer::DrawRoundedRect` trace désormais ses bords droit/bas en
  `x+w-1`/`y+h-1` (comme `SDL_RenderRect`) : avant, ils tombaient en `x+w`,
  hors de la boîte, et le clip les rognait.
- **Barres de défilement.** `UiRect::gutter` = bande réservée (x : barre
  verticale, y : horizontale), `ViewSize()` = boîte moins les barres,
  `MaxScroll()` ignore tout débordement ≤ `OVERFLOW_EPSILON` (1 px),
  `GutterFor()` gère l'interaction (la barre verticale peut rendre
  l'horizontale nécessaire). `Place` part du gutter de l'image précédente et
  replace (≤ 3 essais) tant qu'il change — un seul passage en régime stable.
  Pistes : `VScrollbarTrackRect`/`HScrollbarTrackRect` (ne se chevauchent pas).
  `UiInput` d'une seule ligne : contenu 0 → jamais de barre (avant, ligne +
  marges > hauteur du champ → barre fantôme dans chaque champ de recherche).
  `UiInput`/`UiInputArea` appellent `r.UpdateGutter()` après avoir posé leur
  contenu. `Ui::SetTheme` colore les barres depuis `theme.scrollbar`.
- **Focus clavier.** `sdl3::Event` porte `consumed` (`Consume()`,
  `Release()`, `IsConsumed()`). `InputSystem::HandleEvent(world, Event&, …)`
  (et `Ui::HandleEvent(Event&)`) marque l'évènement consommé quand le widget
  qui a le focus (`KeyboardFocus()` : `UiInput`/`UiInputArea` focused,
  `UiDragValue` editing) le réclame, ou quand Échap ferme une modale. Règle
  `keyPassThrough` (vide = `DefaultKeyPassThrough`) : F1–F24 et Ctrl/Alt
  gauche/Cmd + touche hors édition (Ctrl+S, Ctrl+Q…) passent ; AltGr n'est pas
  un raccourci ; champ en lecture seule = navigation + Ctrl+A/C seulement.
  Échap sur un champ lui retire le focus (consommé) avant de fermer quoi que ce
  soit. Ctrl+A sélectionne tout (`Frame::selectAll`). Un évènement déjà
  consommé n'est pas traité. La surcharge `const Event&` rend juste l'avis.

## Menus façon combo, navigation au survol, infobulles, largeur Unicode — 2026-09-26

Testé par `tests/ui_menu_navigation_smoke_test.cpp` (9) et 4 tests
`DisplayWidth` ajoutés à `ui_text_metrics_smoke_test.cpp` ; vérifié à
l'écran sous Xvfb avec `xdotool` (vrai pointeur : infobulle, sous-menu au
survol, bascule de la barre de menus).

- **Apparence.** `UiFactory::MenuPopup(order)` (utilisé par `Menu`,
  `ContextMenu`, `SubMenu`) : fond `theme.combo.bgNormal`, liseré
  `theme.combo.borderFocus`, rayon 4, `Gap(0)`, marge verticale
  `MENU_POPUP_PAD_Y` (systems.hpp) — la liste d'un combo ouvert. Une
  `UiMenuItem` n'a PLUS de fond au repos (c'était un `FColor::BLACK()` opaque
  par entrée : pile de boutons séparés, texte illisible en thème clair) ;
  survolée ou sous-menu ouvert : `FillRect` pleine largeur en `BgChecked`
  (classe `root-menuitem` = `StyleFromColors(theme.combo)`). Raccourci en
  couleur du texte à 55 %.
- **Navigation.** `InputSystem::NavigateMenus` (appelé par `Tick`) : entrée
  à sous-menu survolée `submenuDelay` (0,2 s) → déploie (à droite, première
  ligne alignée : `PositionPopupRightOf` remonte de marge + bordure) et replie
  la voisine (`CloseSubmenusOf`, en cascade) ; entrée simple survolée → replie
  les sous-menus de son popup ; barre de menus : un menu ouvert + survol d'une
  autre entrée de la même barre → bascule immédiate. Le clic sur une entrée à
  sous-menu OUVRE toujours (ne bascule plus : le survol l'a déjà déployé). Le
  compteur de survol démarre à l'image suivant l'arrivée du pointeur (les tests
  font `Tick(0)` puis `Tick(durée)`).
- **Infobulle.** Mesurée à 13 px mais dessinée à la taille de chargement de la
  police (`m_currentFontSize = 0`) → texte plus large que sa boîte. Corrigé :
  `TOOLTIP_FONT_SIZE` pour les deux ; boîte gardée dans la fenêtre
  (`NativeRenderer()->OutputSize()`) ; couleurs posées par `Ui::SetTheme`.
- **Largeur Unicode.** `CodepointCells(char32_t)` (0 combinants/liants/
  sélecteurs, 2 CJK/Hangul/pleine chasse/émojis, 1 sinon), `DisplayCells`
  pour UTF-8/`u16string_view`/`u32string_view`, `CharCount` UTF-16 (paires de
  substitution = 1, moitié isolée = U+FFFD) et UTF-32, `NextUtf16Codepoint`,
  `CellsAt`. L'heuristique de mesure sans police, `DisplayColumn` et
  `ColumnToByteOffset` (grille d'édition) comptent désormais en cellules.
  `LayoutSystem::MeasureText` accepte aussi `std::u16string_view` et
  `std::u32string_view`.
