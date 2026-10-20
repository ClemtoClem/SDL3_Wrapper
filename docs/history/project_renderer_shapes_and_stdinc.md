---
name: project-renderer-shapes-and-stdinc
description: "Renderer complex-shape methods (circle/ellipse/arc/rounded-rect/polygon/bezier), new sdl3/stdinc.hpp, and math.hpp fixes — all from one session, tightly coupled."
metadata: 
  node_type: memory
  type: project
  originSessionId: 4007b853-281f-4ca2-a318-41aaecee4cae
---

**2026-07-03 session.** Ported complex-shape rendering into `sdl3::Renderer`
(`src/sdl3/render.hpp`), based on `SDL3pp_render.h`
(`~/Documents/Programs/Programs_C/SDL/Video_Projects/SDL3pp`) — same
algorithms (Bresenham circle, parametric ellipse/arc point generation,
triangle-fan/annulus fills for rounded rects, de Casteljau Bézier), adapted
to this project's naming (`drawCircle`/`fillCircle`/`drawEllipse`/
`fillEllipse`/`drawArc`/`drawPie`/`drawRoundedRect`/`fillRoundedRect`/
`drawRoundedBorderedRect`/`drawPolygon`/`fillPolygon`/`drawBezier`,
`renderGeometry`/`renderGeometryFromPoints`, new `Corners`/`Sides` value
structs, `Renderer::getDrawColor()`/`getDrawColorFloat()`). Point generators
(`generateCirclePoints`/`generateEllipsePoints`/`generateArcPoints`) live in
a new `sdl3::shapes` namespace, usable standalone without a `Renderer`.
Added `drawPoints`/`drawLines` overloads taking `std::span<const FPoint>`
(previously only `std::span<const SDL_FPoint>` existed) since the shape
generators return the wrapper's own `FPoint`, not the raw SDL struct.
Covered by `smoke_test_12.cpp`.

**This surfaced that `src/math/math.hpp` had never actually been compiled.**
Nothing in the real build (`main.cpp` + tests) had included it before —
`render.hpp` including it (for `PI`/`lerp`/`clamp`) was the first time. It
was full of calls to nonexistent `sdl3::Sqrt`/`Cos`/`Sin`/`Tan`/`Abs`/`Max`/
`Min`/`Acos` (capitalized, SDL3pp-style) and had a circular include
(`math.hpp` pulled in the *entire* `sdl3.hpp` umbrella, which now includes
`render.hpp`, which needed `math.hpp` — `#pragma once` prevented an infinite
loop but meant symbols were used out of order).

**Fixed by, in order:**
1. User created `src/sdl3/stdinc.hpp` mid-session and asked for a proper
   C++ wrapper of `SDL3/SDL_stdinc.h` (based on `SDL3pp_stdinc.h`, 6353
   lines — NOT fully ported; scoped down to what's useful here). It now
   provides, all under `sdl3::` in lowerCamelCase: math (`sqrt/sin/cos/tan/
   asin/acos/atan/atan2/ceil/floor/round/trunc/exp/log/log10/pow/fmod/
   copysign/abs`, float+double overloads, wrapping SDL's libc-independent
   `SDL_*f`/`SDL_*` functions — consistent with this wrapper's "always go
   through SDL, not <cmath>" philosophy), `PI_F`/`PI_D`, `degToRad`/
   `radToDeg`, `lerp`, generic `min`/`max`/`clamp` templates; `sdl3::memory::`
   (malloc/calloc/realloc/free); `sdl3::ctype::` (isAlpha/isDigit/toUpper/...);
   `sdl3::env::` (get/set/unset). Deliberately NOT ported: qsort/bsearch
   (std::sort/std::binary_search are better), string/wide-string functions
   (redundant with `core/string.hpp`'s `String`), the `Time`/`Environment`/
   `IConv`/`Random` RAII classes SDL3pp has (out of scope / std:: alternatives
   suffice — same reasoning as skipping thread/mutex in
   [[project-sdl3-wrapper-gaps]]).
2. Changed `math.hpp`'s include from `"../sdl3/sdl3.hpp"` (full umbrella,
   circular) to `"../sdl3/sdl.hpp"` + `"../sdl3/stdinc.hpp"` (only what it
   actually uses: `sdl3::FPoint` as `FVector2`'s base, and the new stdinc
   math functions).
3. The user had also been mid-rename (PascalCase -> lowerCamelCase
   throughout `math.hpp`, matching this project's convention, moving away
   from the SDL3pp-style names they'd first copied) and had already deleted
   `math::PI`/`DEG2RAD`/`lerp`/`clamp` (now redundant with the new
   `sdl3::` stdinc versions) — updated `render.hpp` to call the unqualified
   `sdl3::` versions (`PI_F`, `lerp`, `clamp`, `PI_F/180.f` for degToRad)
   instead of the removed `math::` ones, since `render.hpp` is itself inside
   `namespace sdl3`.
4. Fixed one leftover `Expand`/`expand` capitalization mismatch from the
   in-progress rename (`FAABB::expand(const FAABB&)` called `Expand(...)`
   on itself, and `FMatrix4`-transform code called `result.Expand(...)`) —
   both now call the actual (lowercase) method.

**Lesson for future sessions:** when a header in this repo has never been
transitively included by anything the Makefile actually compiles, don't
assume it works just because it exists — `math.hpp` sat broken for
presumably a long time because nothing pulled it in. First time something
does, expect latent bugs.

12 smoke tests total now, all passing; main build clean.
