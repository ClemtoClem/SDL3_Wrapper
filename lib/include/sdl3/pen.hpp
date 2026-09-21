#pragma once
#include <SDL3/SDL.h>

#include "../core/core.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// PenDeviceType / PenAxis / PenInputFlags
//
// La majorité des données de stylet (position, pression, inclinaison...)
// arrive via les événements SDL_EVENT_PEN_* déjà exposés dans events.hpp
// (Event::penMotion(), Event::penAxis(), Event::isPenDown(), ...).
// Ce module ne couvre que la requête statique restante : le type de device.
// ============================================================================

enum class PenDeviceType {
    INVALID = SDL_PEN_DEVICE_TYPE_INVALID,
    UNKNOWN = SDL_PEN_DEVICE_TYPE_UNKNOWN,
    DIRECT = SDL_PEN_DEVICE_TYPE_DIRECT,
    INDIRECT = SDL_PEN_DEVICE_TYPE_INDIRECT,
};

enum class PenAxis {
    PRESSURE = SDL_PEN_AXIS_PRESSURE,
    X_TILT = SDL_PEN_AXIS_XTILT,
    Y_TILT = SDL_PEN_AXIS_YTILT,
    DISTANCE = SDL_PEN_AXIS_DISTANCE,
    ROTATION = SDL_PEN_AXIS_ROTATION,
    SLIDER = SDL_PEN_AXIS_SLIDER,
    TANGENTIAL_PRESSURE = SDL_PEN_AXIS_TANGENTIAL_PRESSURE,
};

namespace pen_input {
inline constexpr SDL_PenInputFlags DOWN = SDL_PEN_INPUT_DOWN;
inline constexpr SDL_PenInputFlags BUTTON1 = SDL_PEN_INPUT_BUTTON_1;
inline constexpr SDL_PenInputFlags BUTTON2 = SDL_PEN_INPUT_BUTTON_2;
inline constexpr SDL_PenInputFlags BUTTON3 = SDL_PEN_INPUT_BUTTON_3;
inline constexpr SDL_PenInputFlags BUTTON4 = SDL_PEN_INPUT_BUTTON_4;
inline constexpr SDL_PenInputFlags BUTTON5 = SDL_PEN_INPUT_BUTTON_5;
inline constexpr SDL_PenInputFlags ERASER_TIP = SDL_PEN_INPUT_ERASER_TIP;
inline constexpr SDL_PenInputFlags IN_PROXIMITY = SDL_PEN_INPUT_IN_PROXIMITY;
} // namespace pen_input

namespace pen {

[[nodiscard]] inline PenDeviceType DeviceType(SDL_PenID id) noexcept { return PenDeviceType(SDL_GetPenDeviceType(id)); }

} // namespace pen

} // namespace sdl3
