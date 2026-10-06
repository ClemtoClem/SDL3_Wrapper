// Définitions de sdl3/joystick.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/joystick.hpp"

namespace sdl3 {

namespace joystick {

std::vector<JoystickID> Enumerated() {
    int count = 0;
    JoystickID *ids = SDL_GetJoysticks(&count);
    if (!ids)
        return {};
    std::vector<JoystickID> v(ids, ids + count);
    SDL_free(ids);
    return v;
}

} // namespace joystick

// ── Joystick ─────────────────────────────────────────────────────────────────

Result<Joystick, StringView> Joystick::Open(JoystickID id) {
    auto *j = SDL_OpenJoystick(id);
    if (!j)
        return Err(GetError());
    return Ok(Joystick(j));
}

SDL_PropertiesID Joystick::Properties() const noexcept {
    return m_handle ? SDL_GetJoystickProperties(m_handle) : 0;
}

uint16_t Joystick::GetProductVersion() const noexcept {
    return m_handle ? SDL_GetJoystickProductVersion(m_handle) : 0;
}

uint16_t Joystick::FirmwareVersion() const noexcept {
    return m_handle ? SDL_GetJoystickFirmwareVersion(m_handle) : 0;
}

JoystickType Joystick::GetType() const noexcept {
    return m_handle ? JoystickType(SDL_GetJoystickType(m_handle)) : JoystickType::UNKNOWN;
}

JoystickConnection Joystick::ConnectionState() const noexcept {
    return m_handle ? JoystickConnection(SDL_GetJoystickConnectionState(m_handle)) : JoystickConnection::INVALID;
}

Option<Sint16> Joystick::AxisInitialState(int index) const {
    Sint16 state = 0;
    if (!m_handle || !SDL_GetJoystickAxisInitialState(m_handle, index, &state))
        return NONE;
    return Some(state);
}

Option<BallDelta> Joystick::Ball(int index) const {
    BallDelta d;
    if (!m_handle || !SDL_GetJoystickBall(m_handle, index, &d.dx, &d.dy))
        return NONE;
    return Some(d);
}

bool Joystick::Rumble(uint16_t lowHz, uint16_t highHz, uint32_t durationMs) noexcept {
    return m_handle && SDL_RumbleJoystick(m_handle, lowHz, highHz, durationMs);
}

bool Joystick::SendEffect(const void *data, int size) noexcept {
    return m_handle && SDL_SendJoystickEffect(m_handle, data, size);
}

sdl3::PowerInfo Joystick::PowerInfo() const noexcept {
    int p = -1;
    PowerState st = m_handle ? PowerState(SDL_GetJoystickPowerInfo(m_handle, &p)) : PowerState::UNKNOWN;
    return {st, p >= 0 ? Some(p) : Option<int>(NONE)};
}

} // namespace sdl3
