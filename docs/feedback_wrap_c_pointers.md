---
name: feedback-wrap-c-pointers
description: "Never expose raw SDL/C pointers or raw C structs in sdl3:: wrapper function signatures — always wrap them in RAII/value classes first."
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 4007b853-281f-4ca2-a318-41aaecee4cae
---

In the `sdl3::` wrapper layer, avoid raw C pointers appearing as function
parameters or return values, and avoid passing/returning plain SDL C structs
directly — wrap them in a proper C++ class/struct first.

**Why:** user explicitly corrected this mid-session while I was adding
`dialog.hpp`/`camera.hpp`/`sensor.hpp`/`process.hpp`/`storage.hpp` — my first
drafts returned raw `SDL_Surface*`, `SDL_IOStream*`, took `const SDL_CameraSpec*`,
etc. directly at API boundaries.

**How to apply:**
- Owning resources: use the existing `Wrapper<T, Deleter>` RAII base
  ([core/wrapper.hpp](../../../../../Documents/Programs/Programs_C/SDL/Video_Projects/UI/SDL3_gui11/src/core/wrapper.hpp)).
- Borrowed/non-owning resources (a stream/m_handle you don't own, e.g. a
  process's stdio pipe, a sensor looked up from an event): use the new
  `Borrowed<T>` base added alongside `Wrapper<T,Deleter>` in the same file —
  it exposes `get()`/`operator T*()`/`operator bool()` but never destroys.
  Concrete examples: `sdl3::IOStreamView` (iostream.hpp), `sdl3::SensorView`
  (sensor.hpp), `sdl3::CameraFrame` (camera.hpp, RAII release instead of
  plain borrow since it must call `SDL_ReleaseCameraFrame` on drop).
- Plain C value structs (e.g. `SDL_CameraSpec`, `SDL_PathInfo`): wrap in a
  C++ struct with named fields, a `constexpr explicit` ctor from the SDL
  struct, and an implicit `operator SDL_Whatever()` back — mirrors the
  existing `Point`/`FPoint`/`Rect`/`Color` pattern in `sdl3/sdl.hpp`. See
  `sdl3::CameraSpec` and `sdl3::PathInfo` for the pattern applied to new code.
- Out-params (`int* exitCode`, `uint64_t* timestamp`) should become part of
  the return type instead (a small result struct, or bundled into the
  RAII/view type) — see `sdl3::ProcessOutput` (process.hpp) and
  `CameraFrame::timestampNs()`.
- Exception: plain read-only C strings (`const char*` for names like
  `Camera::name()`, `SDL_GetKeyName`) are fine and already used throughout
  the pre-existing wrapper (input.hpp, misc.hpp) — the concern is opaque
  struct pointers and C aggregate structs, not simple C-string getters.

Related: [[project-sdl3-wrapper-gaps]] (the module this rule was established
while working on).
