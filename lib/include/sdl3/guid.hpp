#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstring>

#include "../core/core.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// Guid — wrap de SDL_GUID (identifiant 128 bits de périphérique : joystick,
// gamepad, capteur...)
// ============================================================================

struct Guid {
    std::array<uint8_t, 16> data{};

    constexpr Guid() = default;
    explicit Guid(const SDL_GUID &g) noexcept { std::memcpy(data.data(), g.data, 16); }

    [[nodiscard]] operator SDL_GUID() const noexcept {
        SDL_GUID g{};
        std::memcpy(g.data, data.data(), 16);
        return g;
    }

    [[nodiscard]] String ToString() const;

    [[nodiscard]] static Guid FromString(const String &s) noexcept { return Guid(SDL_StringToGUID(s.c_str())); }

    [[nodiscard]] bool operator==(const Guid &o) const noexcept { return data == o.data; }
};

} // namespace sdl3
