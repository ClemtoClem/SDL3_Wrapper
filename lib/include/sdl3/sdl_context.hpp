#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <span>

#include "../core/core.hpp"
#include "error.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// SdlContext — RAII SDL_Init / SDL_Quit context
// ============================================================================

using InitFlags = SDL_InitFlags;

namespace init_flags {
inline constexpr InitFlags VIDEO = SDL_INIT_VIDEO;
inline constexpr InitFlags AUDIO = SDL_INIT_AUDIO;
inline constexpr InitFlags JOYSTICK = SDL_INIT_JOYSTICK;
inline constexpr InitFlags GAMEPAD = SDL_INIT_GAMEPAD;
inline constexpr InitFlags EVENTS = SDL_INIT_EVENTS;
inline constexpr InitFlags SENSOR = SDL_INIT_SENSOR;
inline constexpr InitFlags CAMERA = SDL_INIT_CAMERA;
inline constexpr InitFlags HAPTIC = SDL_INIT_HAPTIC;
} // namespace init_flags

class SdlContext {
	bool owns = false;

public:
	SdlContext() = default;
	explicit SdlContext(InitFlags flags) noexcept : owns(SDL_Init(flags)) {}

	~SdlContext();

	SdlContext(const SdlContext &) = delete;
	SdlContext &operator=(const SdlContext &) = delete;

	SdlContext(SdlContext &&o) noexcept : owns(o.owns) { o.owns = false; }
	SdlContext &operator=(SdlContext &&o) noexcept {
		if (this != &o) {
			if (owns)
				SDL_Quit();
			owns = o.owns;
			o.owns = false;
		}
		return *this;
	}

	[[nodiscard]] explicit operator bool() const noexcept { return owns; }

	bool InitSubsystem(InitFlags flags) noexcept { return SDL_InitSubSystem(flags); }
	void QuitSubsystem(InitFlags flags) noexcept { SDL_QuitSubSystem(flags); }

	[[nodiscard]] static Result<SdlContext, Error> Create(InitFlags flags);
};

} // namespace sdl3