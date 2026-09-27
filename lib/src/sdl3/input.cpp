// Définitions de sdl3/input.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/input.hpp"

namespace sdl3 {

namespace keyboard {

std::span<const bool> State() noexcept {
    int n = 0;
    const bool *s = SDL_GetKeyboardState(&n);
    return {s, size_t(n)};
}

bool IsPressed(SDL_Scancode sc) noexcept {
    std::span<const bool> state = State();
    size_t index = size_t(sc);
    return state.data() != nullptr && index < state.size() && state[index];
}

} // namespace keyboard

namespace mouse {

MouseState State() noexcept {
    MouseState ms;
    ms.buttons = SDL_GetMouseState(&ms.x, &ms.y);
    return ms;
}

MouseState RelativeState() noexcept {
    MouseState ms;
    ms.buttons = SDL_GetRelativeMouseState(&ms.x, &ms.y);
    return ms;
}

} // namespace mouse

// ── Cursor ───────────────────────────────────────────────────────────────────

Result<Cursor, StringView> Cursor::FromSystem(SystemCursor id) {
    auto *c = SDL_CreateSystemCursor(SDL_SystemCursor(id));
    if (!c)
        return Err(GetError());
    return Ok(Cursor(c));
}

Result<Cursor, StringView> Cursor::FromSurface(Ref<Surface> surf, int hotX, int hotY) {
    auto *c = SDL_CreateColorCursor(surf->Get(), hotX, hotY);
    if (!c)
        return Err(GetError());
    return Ok(Cursor(c));
}

// ── Gamepad ──────────────────────────────────────────────────────────────────

Result<Gamepad, StringView> Gamepad::Open(JoystickID id) {
    auto *g = SDL_OpenGamepad(id);
    if (!g)
        return Err(GetError());
    return Ok(Gamepad(g));
}

std::vector<JoystickID> Gamepad::Enumerated() {
    int count = 0;
    JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids)
        return {};
    std::vector<JoystickID> v(ids, ids + count);
    SDL_free(ids);
    return v;
}

Sint16 Gamepad::Axis(GamepadAxis a) const noexcept {
    return m_handle ? SDL_GetGamepadAxis(m_handle, SDL_GamepadAxis(a)) : 0;
}

bool Gamepad::Button(GamepadButton b) const noexcept {
    return m_handle && SDL_GetGamepadButton(m_handle, SDL_GamepadButton(b));
}

GamepadType Gamepad::Type() const noexcept {
    return m_handle ? GamepadType(SDL_GetGamepadType(m_handle)) : GamepadType::UNKNOWN;
}

bool Gamepad::Rumble(uint16_t lowHz, uint16_t highHz, uint32_t durationMs) noexcept {
    return m_handle && SDL_RumbleGamepad(m_handle, lowHz, highHz, durationMs);
}

bool Gamepad::RumbleTriggers(uint16_t left, uint16_t right, uint32_t durationMs) noexcept {
    return m_handle && SDL_RumbleGamepadTriggers(m_handle, left, right, durationMs);
}

} // namespace sdl3
