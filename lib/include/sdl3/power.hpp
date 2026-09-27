#pragma once
#include <SDL3/SDL.h>

#include "../core/core.hpp"
#include "joystick.hpp" // PowerState
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// PowerStatus — wrap du triplet (SDL_PowerState, secondes restantes, %
// restant) retourné par SDL_GetPowerInfo. Réutilise `PowerState` (déjà défini
// dans joystick.hpp pour SDL_GetJoystickPowerInfo — même énum SDL).
// ============================================================================

struct PowerStatus {
    PowerState state = PowerState::UNKNOWN;
    Option<int> secondsLeft = NONE; // NONE si inconnu (ex: alimentation secteur sans batterie)
    Option<int> percent = NONE;     // NONE si inconnu
};

namespace power {

// Ne jamais prendre ces valeurs pour une vérité absolue : ce sont des
// estimations remontées par le matériel (cf. doc SDL_GetPowerInfo).
[[nodiscard]] PowerStatus Info() noexcept;

} // namespace power

} // namespace sdl3
