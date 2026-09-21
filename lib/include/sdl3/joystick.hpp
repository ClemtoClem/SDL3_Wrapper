#pragma once
#include <SDL3/SDL.h>
#include <vector>

#include "../core/core.hpp"

namespace sdl3 {

// ============================================================================
// JoystickType / JoystickConnectionState
// ============================================================================

enum class JoystickType : int {
    UNKNOWN = SDL_JOYSTICK_TYPE_UNKNOWN,
    GAMEPAD = SDL_JOYSTICK_TYPE_GAMEPAD,
    WHEEL = SDL_JOYSTICK_TYPE_WHEEL,
    ARCADE_STICK = SDL_JOYSTICK_TYPE_ARCADE_STICK,
    FLIGHT_STICK = SDL_JOYSTICK_TYPE_FLIGHT_STICK,
    DANCE_PAD = SDL_JOYSTICK_TYPE_DANCE_PAD,
    GUITAR = SDL_JOYSTICK_TYPE_GUITAR,
    DRUM_KIT = SDL_JOYSTICK_TYPE_DRUM_KIT,
    ARCADE_PAD = SDL_JOYSTICK_TYPE_ARCADE_PAD,
    THROTTLE = SDL_JOYSTICK_TYPE_THROTTLE,
};

enum class JoystickConnection : int {
    INVALID = SDL_JOYSTICK_CONNECTION_INVALID,
    UNKNOWN = SDL_JOYSTICK_CONNECTION_UNKNOWN,
    WIRED = SDL_JOYSTICK_CONNECTION_WIRED,
    WIRELESS = SDL_JOYSTICK_CONNECTION_WIRELESS,
};

// ============================================================================
// Ball delta — wrap du couple (dx, dy) retourné par SDL_GetJoystickBall
// ============================================================================

struct BallDelta {
    int dx = 0, dy = 0;
};

// ============================================================================
// PowerInfo — wrap du couple (SDL_PowerState, pourcentage) retourné par
// SDL_GetJoystickPowerInfo
// ============================================================================

enum class PowerState : int {
    ERROR = SDL_POWERSTATE_ERROR,          /**< error determining power status */
    UNKNOWN = SDL_POWERSTATE_UNKNOWN,      /**< cannot determine power status */
    ON_BATTERY = SDL_POWERSTATE_ON_BATTERY, /**< Not plugged in, running on the battery */
    NO_BATTERY = SDL_POWERSTATE_NO_BATTERY, /**< Plugged in, no battery available */
    CHARGING = SDL_POWERSTATE_CHARGING,    /**< Plugged in, charging battery */
    CHARGED = SDL_POWERSTATE_CHARGED       /**< Plugged in, battery charged */
};

struct PowerInfo {
    PowerState state = PowerState::UNKNOWN;
    Option<int> percent = NONE;
};

// ============================================================================
// Énumération des manettes brutes connectées
// ============================================================================

namespace joystick {

[[nodiscard]] inline bool Any() noexcept { return SDL_HasJoystick(); }

[[nodiscard]] inline std::vector<JoystickID> Enumerated() {
    int count = 0;
    JoystickID *ids = SDL_GetJoysticks(&count);
    if (!ids)
        return {};
    std::vector<JoystickID> v(ids, ids + count);
    SDL_free(ids);
    return v;
}

[[nodiscard]] inline const char *NameFor(JoystickID id) noexcept { return SDL_GetJoystickNameForID(id); }
[[nodiscard]] inline const char *PathFor(JoystickID id) noexcept { return SDL_GetJoystickPathForID(id); }
[[nodiscard]] inline int PlayerIndexFor(JoystickID id) noexcept { return SDL_GetJoystickPlayerIndexForID(id); }
[[nodiscard]] inline uint16_t VendorFor(JoystickID id) noexcept { return SDL_GetJoystickVendorForID(id); }
[[nodiscard]] inline uint16_t ProductFor(JoystickID id) noexcept { return SDL_GetJoystickProductForID(id); }
[[nodiscard]] inline JoystickType TypeFor(JoystickID id) noexcept { return JoystickType(SDL_GetJoystickTypeForID(id)); }

inline void SetEventsEnabled(bool enabled) noexcept { SDL_SetJoystickEventsEnabled(enabled); }
[[nodiscard]] inline bool EventsEnabled() noexcept { return SDL_JoystickEventsEnabled(); }
inline void Update() noexcept { SDL_UpdateJoysticks(); }

} // namespace joystick

// ============================================================================
// Joystick — RAII SDL_Joystick (accès bas niveau, cf. `Gamepad` pour l'API
// haut niveau normalisée)
// ============================================================================

class Joystick : public Wrapper<SDL_Joystick, SDL_CloseJoystick> {
public:
    using Wrapper::Wrapper;

    [[nodiscard]] static Result<Joystick, StringView> Open(JoystickID id) {
        auto *j = SDL_OpenJoystick(id);
        if (!j)
            return Err(GetError());
        return Ok(Joystick(j));
    }

    [[nodiscard]] SDL_PropertiesID Properties() const noexcept {
        return m_handle ? SDL_GetJoystickProperties(m_handle) : 0;
    }
    [[nodiscard]] const char *GetName() const noexcept { return m_handle ? SDL_GetJoystickName(m_handle) : ""; }
    [[nodiscard]] const char *GetPath() const noexcept { return m_handle ? SDL_GetJoystickPath(m_handle) : ""; }

    [[nodiscard]] int PlayerIndex() const noexcept { return m_handle ? SDL_GetJoystickPlayerIndex(m_handle) : -1; }
    bool SetPlayerIndex(int index) noexcept { return m_handle && SDL_SetJoystickPlayerIndex(m_handle, index); }

    [[nodiscard]] uint16_t GetVendor() const noexcept { return m_handle ? SDL_GetJoystickVendor(m_handle) : 0; }
    [[nodiscard]] uint16_t GetProduct() const noexcept { return m_handle ? SDL_GetJoystickProduct(m_handle) : 0; }
    [[nodiscard]] uint16_t GetProductVersion() const noexcept {
        return m_handle ? SDL_GetJoystickProductVersion(m_handle) : 0;
    }
    [[nodiscard]] uint16_t FirmwareVersion() const noexcept {
        return m_handle ? SDL_GetJoystickFirmwareVersion(m_handle) : 0;
    }
    [[nodiscard]] const char *Serial() const noexcept { return m_handle ? SDL_GetJoystickSerial(m_handle) : ""; }
    [[nodiscard]] JoystickType GetType() const noexcept {
        return m_handle ? JoystickType(SDL_GetJoystickType(m_handle)) : JoystickType::UNKNOWN;
    }

    [[nodiscard]] bool Connected() const noexcept { return m_handle && SDL_JoystickConnected(m_handle); }
    [[nodiscard]] JoystickID GetId() const noexcept { return m_handle ? SDL_GetJoystickID(m_handle) : 0; }
    [[nodiscard]] JoystickConnection ConnectionState() const noexcept {
        return m_handle ? JoystickConnection(SDL_GetJoystickConnectionState(m_handle)) : JoystickConnection::INVALID;
    }

    [[nodiscard]] int GetNumAxes() const noexcept { return m_handle ? SDL_GetNumJoystickAxes(m_handle) : 0; }
    [[nodiscard]] int GetNumBalls() const noexcept { return m_handle ? SDL_GetNumJoystickBalls(m_handle) : 0; }
    [[nodiscard]] int GetNumHats() const noexcept { return m_handle ? SDL_GetNumJoystickHats(m_handle) : 0; }
    [[nodiscard]] int GetNumButtons() const noexcept { return m_handle ? SDL_GetNumJoystickButtons(m_handle) : 0; }

    [[nodiscard]] Sint16 GetAxis(int index) const noexcept { return m_handle ? SDL_GetJoystickAxis(m_handle, index) : 0; }

    [[nodiscard]] Option<Sint16> AxisInitialState(int index) const {
        Sint16 state = 0;
        if (!m_handle || !SDL_GetJoystickAxisInitialState(m_handle, index, &state))
            return NONE;
        return Some(state);
    }

    [[nodiscard]] Option<BallDelta> Ball(int index) const {
        BallDelta d;
        if (!m_handle || !SDL_GetJoystickBall(m_handle, index, &d.dx, &d.dy))
            return NONE;
        return Some(d);
    }

    [[nodiscard]] uint8_t Hat(int index) const noexcept { return m_handle ? SDL_GetJoystickHat(m_handle, index) : 0; }
    [[nodiscard]] bool Button(int index) const noexcept { return m_handle && SDL_GetJoystickButton(m_handle, index); }

    bool Rumble(uint16_t lowHz, uint16_t highHz, uint32_t durationMs) noexcept {
        return m_handle && SDL_RumbleJoystick(m_handle, lowHz, highHz, durationMs);
    }

    // Envoie des données d'effet spécifiques au pilote (usage avancé).
    bool SendEffect(const void *data, int size) noexcept {
        return m_handle && SDL_SendJoystickEffect(m_handle, data, size);
    }

    // `percent` (charge restante, [0,100]) est NONE si inconnu.
    [[nodiscard]] sdl3::PowerInfo PowerInfo() const noexcept {
        int p = -1;
        PowerState st = m_handle ? PowerState(SDL_GetJoystickPowerInfo(m_handle, &p)) : PowerState::UNKNOWN;
        return {st, p >= 0 ? Some(p) : Option<int>(NONE)};
    }
};

} // namespace sdl3
