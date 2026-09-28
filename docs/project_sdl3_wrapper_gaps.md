---
name: project-sdl3-wrapper-gaps
description: "Status of the sdl3:: C++ wrapper vs SDL3pp coverage, and the deferred plan for migrating ui/ to ECS."
metadata: 
  node_type: memory
  type: project
  originSessionId: 4007b853-281f-4ca2-a318-41aaecee4cae
---

This project (`SDL3_gui11`) is building a hand-rolled modern-C++ SDL3
wrapper (`src/sdl3/*.hpp`, namespace `sdl3::`) plus a separate CSS/flexbox-like
UI layer (`src/ui/*.hpp`) and an archetype-based ECS (`src/ecs/ecs.hpp`).
Two reference projects are used as inspiration/prior art (not dependencies):
`~/Documents/Programs/Programs_C/SDL/Video_Projects/SDL3pp` (a much larger
C++ SDL3 binding with its own ECS + `SDL3pp_ui_v1`/`SDL3pp_ui_v2` entity-based
UI system, ~18k lines) and `~/Documents/Programs/Projects_Rust/voxel/vendor/sdl3`
(Rust bindings, less directly applicable).

**2026-07-03 session — decisions made via AskUserQuestion:**
- UI rendering backend: render via `sdl3::Renderer`/`Font`/`TextEngine`
  (the existing SDL_Renderer-based 2D wrapper), NOT a GPU pipeline.
- UI architecture: `ui/` nodes as ECS entities (component-per-concern +
  systems), closer to `SDL3pp_ui_v2`, rather than a plain `unique_ptr` tree.
- Session scope chosen: extend the `sdl3::` wrapper first, UI/ECS migration
  deferred to a later session.

**Why:** the user wants the wrapper's SDL3 coverage more complete before
investing in the bigger UI/ECS rearchitecture — avoids building UI on top of
a moving-target wrapper.

**UPDATE 2026-07-05: the UI/ECS migration is DONE** — both decisions above
were implemented; the old broken UINode files were deleted. See
[[project-ui-ecs-module]] for the current `src/ui/` architecture, naming
decisions (Dimension, ArchetypeRegistry), and its remaining limitations.

**Wrapper modules added this session** (all under `src/sdl3/`, wired into the
umbrella `sdl3.hpp`), following [[feedback-wrap-c-pointers]]:
`dialog.hpp`, `camera.hpp`, `sensor.hpp`, `process.hpp`, `storage.hpp`.
Verified via a standalone smoke test (camera enumeration, process spawn+read,
storage read/write/glob all exercised at runtime, not just compiled).

**2026-07-03 session, part 2:** added `joystick.hpp` (raw `SDL_Joystick`,
distinct from the existing `Gamepad` wrapper), `haptic.hpp` (force feedback,
including the simple rumble API and the raw-effect API which still takes
`const SDL_HapticEffect&` by reference since fully wrapping that big tagged
union was out of scope), `touch.hpp` (touch devices/fingers, wraps
`SDL_Finger`), `pen.hpp` (just the one static query left uncovered by
events.hpp's existing pen event predicates). Added `init_flags::HAPTIC` to
`sdl.hpp` (was missing). All wired into `sdl3.hpp`.

Also built out `./tests/` as a proper smoke-test suite (`make tests` /
`make run-tests` targets added to the Makefile — each `tests/*.cpp` is its
own standalone executable with its own `main()`, compiled+linked
independently, not merged into the `GUI` binary). Tests force `SDL_VIDEODRIVER=dummy`
(and `SDL_AUDIODRIVER=dummy` where relevant) via `setenv(..., 0)` so they
never flash a real window or touch the user's real clipboard/audio device —
important since this machine has a real DISPLAY/Wayland session, not a
headless CI box. `smoke_test_1.cpp` (camera/sensor/process/storage) already
existed when this part of the session started; added `smoke_test_2`
(window/renderer/surface/texture/properties/system), `_3`
(events/keyboard/mouse/cursor/gamepad/joystick/haptic/touch/pen), `_4`
(audio/mixer, plays real assets from `assets/sounds/`), `_5`
(ttf/image/clipboard, loads real assets from `assets/textures/`), `_6`
(net TCP+UDP loopback, GPU device creation best-effort, dialog compile-check
only — dialogs need a real display portal so they're never invoked at
runtime in a test). All 6 currently pass on this machine.

**2026-07-03 session, part 3:** added `power.hpp` (`PowerStatus`, reuses the
`PowerState` enum class from `joystick.hpp`), `locale.hpp` (`Locale` wraps
`SDL_Locale`), `guid.hpp` (`Guid` wraps `SDL_GUID`, string round-trip),
`filesystem.hpp` (unsandboxed base/pref/user-folder paths, directory
create/enumerate/remove/rename/copy/glob — reuses `PathInfo`/`PathType` from
`storage.hpp`). Added `smoke_test_7.cpp` covering all four; all 7 smoke
tests pass.

**Note on parallel editing:** partway through this session the user started
hand-editing `input.hpp`/`joystick.hpp` directly (in the IDE, concurrently)
to wrap more raw SDL enums into `enum class` types — e.g. `SystemCursor`,
`GamepadType`/`GamepadButton`/`GamepadAxis`/`GamepadBindingType`, `PowerState`
— applying [[feedback-wrap-c-pointers]] themselves. `Cursor::fromSurface`
was also changed from a raw `SDL_Surface*` param to `Ref<Surface>`. Expect
more of this pattern going forward — when adding new modules, check whether
sibling files already got a hand-pass before assuming their raw-pointer
signatures are stable.

**2026-07-03 session, part 4:** added `tray.hpp` (`Tray` owning RAII root,
`TrayMenu`/`TrayEntry` as `Borrowed<T>` non-owning views since the whole
menu tree is destroyed with the `Tray`). Deliberately did NOT wrap
thread/mutex/atomic — C++23 already has `std::jthread`/`std::mutex`/
`std::atomic`, which is what a user of this wrapper would reach for anyway;
SDL's versions add no value here vs. the standard library, unlike the other
modules (camera, sensor, dialog, etc.) which have no std:: equivalent.

Important runtime finding: `SDL_CreateTray()` cannot be safely invoked in
this session's shell — it `dlopen`s a platform tray backend (appindicator/
dbus/gio on Linux) which pulls in a mismatched `libpthread` from a snap
runtime mount (this shell runs under a snap-confined VSCode) and crashes the
process with a hard dynamic-linker symbol-lookup error *before* any SDL or
C++ exception/error-handling can run — not a bug in the wrapper, just an
environment where the tray backend can't load cleanly. `smoke_test_8.cpp`
therefore only compile/link-checks the `Tray`/`TrayMenu`/`TrayEntry` API
shapes (flags, callback signature, null-pointer-safe borrowed views) and
never calls `SDL_CreateTray`/`Tray::create` — mirrors how `dialog.hpp` is
handled (real invocation needs a display portal too). If a future session
runs outside this snap-confined shell, actually creating a tray icon could
be tried, but expect it may still be undesirable (pops a real, possibly
lingering icon in the user's desktop panel).

8 smoke tests total now, all passing.

**2026-07-03 session, part 5:** added `hidapi.hpp` (`HidContext`,
`HidDevice`, `HidDeviceInfo`, `HidBusType` — wchar_t manufacturer/product/
serial strings converted to UTF-8 `String` via a `sizeof(wchar_t)`-aware
helper, never exposed raw). Added `smoke_test_9.cpp`.

Audited `gpu.hpp` (880 lines) against `SDL3pp_gpu.h` (8047 lines) and the
voxel Rust bindings (`voxel/vendor/sdl3/src/sdl3/gpu/*.rs`): class-for-class
coverage already matches (GpuDevice/Buffer/Texture/Sampler/Shader/Pipeline/
CommandBuffer/RenderPass/ComputePass/CopyPass/Fence/TransferBuffer/
MappedBuffer all present), and method-level coverage is also essentially
complete — uniforms push, blit, swapchain acquire/wait, debug labels/groups,
storage buffer/texture bindings (vertex/fragment/compute stages), format
predicates and conversions, fence wait/query, mipmap generation, copy/
download — all already there. Only gap found: `SDL_GPUSupportsProperties`
(pre-flight check before `createWithProperties()`), now added as
`gpuSupportsProperties()`. The voxel Rust bindings use a builder-pattern API
(`ShaderBuilder`, `GraphicsPipelineBuilder`, etc.) instead of a flat 1:1 C
wrap — that's a design upgrade (more ergonomic for huge create-info structs),
not a coverage gap; flagged to the user as a possible future direction but
not implemented (would be a larger, separate ergonomics project, not a gap-fill).

**MIDI/DMX extension (user-requested, via AskUserQuestion to scope first):**
added `midi.hpp` (transport-agnostic `MidiMessage` + `MidiParser` — handles
running status, System Real-Time interleaving mid-message, System Common,
SysEx accumulation; deliberately has NO dependency on hidapi.hpp, since most
real USB-MIDI hardware is USB-MIDI-class, not HID, and SDL3 has no native
MIDI API at all — the parser just consumes `std::span<const uint8_t>` from
whatever transport the caller has) and `dmx.hpp` (`DmxUniverse` 512-channel
buffer + `dmx::sendToHid()` generic transport helper, since USB-DMX widgets
split into serial-protocol ones like Enttec, which are out of scope for a
HID module, and a few cheap ones that take the raw universe over an HID
output report — the exact report framing is vendor-specific and documented
as needing per-device adjustment, since guessing wrong would silently not
work on real hardware). Both are covered by `smoke_test_10.cpp` with real
assertions (round-trip encode/parse, running status, interleaved real-time,
SysEx, DMX channel indexing) — no hardware needed since both modules are
pure logic/buffers.

Caught and fixed one thing while doing this: the user had sketched
`HidBusType::DMX`/`MIDI` directly into the enum mirroring `SDL_hid_bus_type`
— confirmed via grep across every vendored SDL3 header on this machine that
no SDL version defines those (real values are only Unknown/USB/Bluetooth/
I2C/SPI, the physical transport bus, not an application protocol) — removed
them from that enum and built the actual protocol support as separate
modules instead.

10 smoke tests total now, all passing.

**Still missing vs SDL3pp** (not done): none identified as clear gaps after
the gpu.hpp audit above. Possible future ergonomics upgrade: builder-pattern
API for GPU pipeline/shader/buffer creation (see voxel Rust bindings),
optional, bigger scope than a gap-fill.

**2026-07-03 session, part 6 — midi.hpp improved from LMMS
(`~/Documents/Programs/Programs_C/lmms`, esp. `include/Midi.h`,
`include/MidiEvent.h`, `src/core/midi/MidiEventToByteSeq.cpp`,
`include/MidiPort.h`):** the user had started sketching
`MidiMetaEventTypes`/`MidiStandardControllers`/
`MidiControllerRegisteredParameterNumbers` enums directly into `midi.hpp`
themselves (mirroring LMMS's `Midi.h`, but as `enum class` to match this
project's convention rather than LMMS's plain C enums) — kept and completed
those. Added: named scalar constants in `namespace midi` (`MaxKey`,
`ChannelCount`, `MaxPitchBend`, etc., plus `ccToNormalized`/`ccToBipolar`
helpers, ported from LMMS's `Midi.h` constants); a `metaEvent` field on
`MidiMessage` (mirrors `MidiEvent::metaEvent`, only meaningful for a
Standard MIDI File reader we don't have — documented that `MetaEvent` and
`SystemReset` legitimately share the byte value 0xFF by MIDI-protocol design,
same as LMMS's own `Midi.h` does); the LV2/LMMS convention that a Note On
with velocity 0 serializes as a real Note Off (`MidiEventToByteSeq.cpp`);
and a new `MidiPortFilter` class (channel remap + fixed velocity/note +
base-velocity scaling) — a dependency-free, no-raw-pointer distillation of
`MidiPort`'s routing logic, without any of LMMS's Qt/Model/serialization
machinery (that's DAW-app plumbing, out of scope for a protocol library).

Deliberately NOT ported: `MidiClient`/`MidiAlsaRaw`/`MidiAlsaSeq`/
`MidiApple`/`MidiJack`/`MidiOss`/`MidiSndio`/`MidiWinMM` (real OS MIDI I/O
backends — huge, per-platform, needs external dev libs like ALSA headers,
a genuinely separate project from "improve the message/parser layer");
`MidiController`/`MidiClip`/`MidiClipView`/`MidiCCRackView`/
`MidiSetupWidget`/`MidiPortMenu` (LMMS's own Qt-based DAW automation/UI
layer, not relevant to a transport-agnostic C++ library). If real MIDI I/O
(actually talking to a device, not just parsing bytes) is wanted later,
that's the next logical ask — it would need platform backends note wrapped
by SDL3 at all (SDL has no MIDI API), so likely ALSA rawmidi/seq on Linux
as a first target.

`smoke_test_10.cpp` extended with assertions for the Note-On/velocity-0
convention and `MidiPortFilter` (channel filtering, fixed velocity, output
routing). 10 smoke tests still all passing.
