#pragma once
#include <SDL3/SDL.h>
#include <vector>

#include "../core/core.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// TouchDeviceType
// ============================================================================

enum class TouchDeviceType {
	INVALID = SDL_TOUCH_DEVICE_INVALID,
	DIRECT = SDL_TOUCH_DEVICE_DIRECT,
	INDIRECT_ABSOLUTE = SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE,
	INDIRECT_RELATIVE = SDL_TOUCH_DEVICE_INDIRECT_RELATIVE,
};

namespace detail {
	constexpr SDL_TouchDeviceType ToSDL(TouchDeviceType e) {
		return static_cast<SDL_TouchDeviceType>(e);
	}
}

// ============================================================================
// Finger — wrap de SDL_Finger
// ============================================================================

struct Finger {
	SDL_FingerID id = 0;
	float x = 0.f, y = 0.f; // normalisé [0, 1]
	float pressure = 0.f;   // normalisé [0, 1]

	Finger() = default;
	constexpr explicit Finger(const SDL_Finger &f) noexcept : id(f.id), x(f.x), y(f.y), pressure(f.pressure) {}
};

// ============================================================================
// touch — énumération des périphériques tactiles et de leurs doigts actifs
// ============================================================================

namespace touch {

[[nodiscard]] std::vector<SDL_TouchID> Devices();

[[nodiscard]] inline const char *DeviceName(SDL_TouchID id) noexcept { return SDL_GetTouchDeviceName(id); }
[[nodiscard]] TouchDeviceType DeviceType(SDL_TouchID id) noexcept;

// Doigts actuellement posés sur le périphérique `id`.
[[nodiscard]] std::vector<Finger> Fingers(SDL_TouchID id);

} // namespace touch

} // namespace sdl3
