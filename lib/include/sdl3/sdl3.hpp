#pragma once

// Stdinc: math/memory/ctype/environment wrappers over SDL_stdinc.h
#include "stdinc.hpp"

// Core SDL3 error
#include "error.hpp"

// Log: LogRouter (fan-out SDL_LogOutputFunction) + threaded FileLogSink
#include "log.hpp"

// Thread: SDL_Thread/SDL_Mutex/SDL_RWLock/SDL_Semaphore/SDL_Condition/atomics
#include "thread.hpp"

// SDL3: SDL_Init / SDL_Quit RAII guard
#include "sdl_context.hpp"

// Core SDL3 colors, geometry
#include "structs.hpp"

// Core SDL3 time
#include "time.hpp"

// Rendering: Window, Surface, Texture, Renderer
#include "render.hpp"

// Events
#include "events.hpp"

// Input: Keyboard, Mouse, Cursor, Gamepad
#include "input.hpp"

// Audio: AudioStream, WavData
#include "audio.hpp"

// File I/O: IOStream, readFile, writeFile
#include "iostream.hpp"

// Misc: Properties, MessageBox, Clipboard, system info
#include "misc.hpp"

// Dialog: native open/save file & folder dialogs (async)
#include "dialog.hpp"

// Camera: device enumeration, frame acquisition
#include "camera.hpp"

// Sensor: accelerometer/gyroscope enumeration & reading
#include "sensor.hpp"

// Process: subprocess creation, stdio piping
#include "process.hpp"

// Storage: sandboxed title/user/file storage abstraction
#include "storage.hpp"

// Joystick: raw device enumeration & queries (see input.hpp for Gamepad)
#include "joystick.hpp"

// Haptic: force-feedback device enumeration, rumble & effects
#include "haptic.hpp"

// Touch: touch device enumeration & active fingers
#include "touch.hpp"

// Pen: stylus device type queries (event data lives in events.hpp)
#include "pen.hpp"

// Power: battery/AC status query
#include "power.hpp"

// Locale: user preferred language/country
#include "locale.hpp"

// Guid: 128-bit device identifiers (joystick/gamepad/sensor)
#include "guid.hpp"

// Filesystem: unsandboxed base/pref/user paths, directory ops, glob
#include "filesystem.hpp"

// Tray: system tray icon + menu
#include "tray.hpp"

// HID: raw USB/Bluetooth HID device access
#include "hidapi.hpp"

// MIDI: transport-agnostic message (de)serialization + streaming parser
#include "midi.hpp"

// DMX512: universe buffer + generic HID-widget transport helper
#include "dmx.hpp"

// SDL3_ttf: Font, TextEngine, Text
#include "ttf.hpp"

// SDL3_image: ImgContext, imgLoad, imgLoadTexture
#include "image.hpp"

// SDL3_mixer: MixerContext, Mixer, MixAudio, MixTrack
#include "mixer.hpp"

// SDL3_net: NetContext, IpAddress, TcpSocket, TcpServer, UdpSocket
#include "net.hpp"

// GPU: GpuDevice, GpuBuffer, GpuTexture, GpuShader, GpuGraphicsPipeline, ...
#include "gpu.hpp"

// OpenGL: GlContext + gl:: helpers
#include "gl.hpp"

// Vulkan: VkLibrary + vulkan:: helpers
#include "vulkan.hpp"
