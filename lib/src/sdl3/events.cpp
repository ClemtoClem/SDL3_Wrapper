// Définitions de sdl3/events.hpp
#include "sdl3/events.hpp"

namespace sdl3 {

// ── Event ────────────────────────────────────────────────────────────────────

bool Event::IsApp() const noexcept {
    return raw.type >= uint32_t(EventType::TERMINATING) && raw.type <= uint32_t(EventType::SYSTEM_THEME_CHANGED);
}

bool Event::IsDisplay() const noexcept {
    return raw.type >= uint32_t(EventType::DISPLAY_FIRST) && raw.type <= uint32_t(EventType::DISPLAY_LAST);
}

bool Event::IsWindow() const noexcept {
    return raw.type >= uint32_t(EventType::WINDOW_FIRST) && raw.type <= uint32_t(EventType::WINDOW_LAST);
}

bool Event::IsKeyboard() const noexcept {
    return raw.type >= uint32_t(EventType::KEY_DOWN) && raw.type <= uint32_t(EventType::SCREEN_KEYBOARD_HIDDEN);
}

bool Event::IsMouse() const noexcept {
    return raw.type >= uint32_t(EventType::MOUSE_MOTION) && raw.type <= uint32_t(EventType::MOUSE_REMOVED);
}

bool Event::IsJoystick() const noexcept {
    return raw.type >= uint32_t(EventType::JOYSTICK_AXIS_MOTION) &&
           raw.type <= uint32_t(EventType::JOYSTICK_UPDATE_COMPLETE);
}

bool Event::IsGamepad() const noexcept {
    return raw.type >= uint32_t(EventType::GAMEPAD_AXIS_MOTION) &&
           raw.type <= uint32_t(EventType::GAMEPAD_STEAM_HANDLE_UPDATED);
}

bool Event::IsFinger() const noexcept {
    return raw.type >= uint32_t(EventType::FINGER_DOWN) && raw.type <= uint32_t(EventType::FINGER_CANCELED);
}

bool Event::IsPinch() const noexcept {
    return raw.type >= uint32_t(EventType::PINCH_BEGIN) && raw.type <= uint32_t(EventType::PINCH_END);
}

bool Event::IsDrop() const noexcept {
    return raw.type >= uint32_t(EventType::DROP_FILE) && raw.type <= uint32_t(EventType::DROP_POSITION);
}

bool Event::IsAudio() const noexcept {
    return raw.type >= uint32_t(EventType::AUDIO_DEVICE_ADDED) &&
           raw.type <= uint32_t(EventType::AUDIO_DEVICE_FORMAT_CHANGED);
}

bool Event::IsPen() const noexcept {
    return raw.type >= uint32_t(EventType::PEN_PROXIMITY_IN) && raw.type <= uint32_t(EventType::PEN_AXIS);
}

bool Event::IsCamera() const noexcept {
    return raw.type >= uint32_t(EventType::CAMERA_DEVICE_ADDED) &&
           raw.type <= uint32_t(EventType::CAMERA_DEVICE_DENIED);
}

bool Event::IsRender() const noexcept {
    return raw.type >= uint32_t(EventType::RENDER_TARGETS_RESET) &&
           raw.type <= uint32_t(EventType::RENDER_DEVICE_LOST);
}

bool Event::IsUser() const noexcept {
    return raw.type >= uint32_t(EventType::USER) && raw.type < uint32_t(EventType::LAST);
}

bool Event::IsKey() const noexcept {
    return raw.type == uint32_t(EventType::KEY_DOWN) || raw.type == uint32_t(EventType::KEY_UP);
}

bool Event::IsKeyboardDevice() const noexcept {
    return raw.type == uint32_t(EventType::KEYBOARD_ADDED) || raw.type == uint32_t(EventType::KEYBOARD_REMOVED);
}

bool Event::IsMouseDevice() const noexcept {
    return raw.type == uint32_t(EventType::MOUSE_ADDED) || raw.type == uint32_t(EventType::MOUSE_REMOVED);
}

bool Event::IsWindowMouseEnter() const noexcept {
    return raw.type == uint32_t(EventType::WINDOW_MOUSE_ENTER);
}

bool Event::IsWindowMouseLeave() const noexcept {
    return raw.type == uint32_t(EventType::WINDOW_MOUSE_LEAVE);
}

bool Event::IsWindowFocusGained() const noexcept {
    return raw.type == uint32_t(EventType::WINDOW_FOCUS_GAINED);
}

bool Event::IsWindowClosed() const noexcept {
    return raw.type == uint32_t(EventType::WINDOW_CLOSE_REQUESTED);
}

bool Event::IsWindowFullscreen() const noexcept {
    return raw.type == uint32_t(EventType::WINDOW_ENTER_FULLSCREEN);
}

bool Event::IsJoyButtonDown() const noexcept {
    return raw.type == uint32_t(EventType::JOYSTICK_BUTTON_DOWN);
}

bool Event::IsJoyDevice() const noexcept {
    return raw.type == uint32_t(EventType::JOYSTICK_ADDED) || raw.type == uint32_t(EventType::JOYSTICK_REMOVED);
}

bool Event::IsJoyBattery() const noexcept {
    return raw.type == uint32_t(EventType::JOYSTICK_BATTERY_UPDATED);
}

bool Event::IsGamepadButtonDown() const noexcept {
    return raw.type == uint32_t(EventType::GAMEPAD_BUTTON_DOWN);
}

bool Event::IsGamepadTouchpad() const noexcept {
    return raw.type >= uint32_t(EventType::GAMEPAD_TOUCHPAD_DOWN) &&
           raw.type <= uint32_t(EventType::GAMEPAD_TOUCHPAD_UP);
}

bool Event::IsGamepadSensor() const noexcept {
    return raw.type == uint32_t(EventType::GAMEPAD_SENSOR_UPDATE);
}

uint32_t Event::WindowId() const noexcept {
    const auto T = raw.type;
    if (T >= uint32_t(EventType::WINDOW_FIRST) && T <= uint32_t(EventType::WINDOW_LAST))
        return raw.window.windowID;
    if (T == uint32_t(EventType::KEY_DOWN) || T == uint32_t(EventType::KEY_UP) ||
        T == uint32_t(EventType::KEYBOARD_ADDED) || T == uint32_t(EventType::KEYBOARD_REMOVED))
        return raw.key.windowID;
    if (T == uint32_t(EventType::TEXT_EDITING) || T == uint32_t(EventType::TEXT_EDITING_CANDIDATES))
        return raw.edit.windowID;
    if (T == uint32_t(EventType::TEXT_INPUT))
        return raw.text.windowID;
    if (T == uint32_t(EventType::MOUSE_MOTION))
        return raw.motion.windowID;
    if (T == uint32_t(EventType::MOUSE_BUTTON_DOWN) || T == uint32_t(EventType::MOUSE_BUTTON_UP))
        return raw.button.windowID;
    if (T == uint32_t(EventType::MOUSE_WHEEL))
        return raw.wheel.windowID;
    if (T >= uint32_t(EventType::FINGER_DOWN) && T <= uint32_t(EventType::FINGER_CANCELED))
        return raw.tfinger.windowID;
    if (T >= uint32_t(EventType::DROP_FILE) && T <= uint32_t(EventType::DROP_POSITION))
        return raw.drop.windowID;
    if (T >= uint32_t(EventType::RENDER_TARGETS_RESET) && T <= uint32_t(EventType::RENDER_DEVICE_LOST))
        return raw.render.windowID;
    if (T >= uint32_t(EventType::PEN_PROXIMITY_IN) && T <= uint32_t(EventType::PEN_AXIS)) {
        // All pen events share windowID at the same offset
        return raw.pproximity.windowID;
    }
    return 0;
}

String Event::Describe() const {
    char buf[512] = {};
    SDL_GetEventDescription(&raw, buf, int(sizeof(buf)));
    return String(buf);
}

// ── EventWatch ───────────────────────────────────────────────────────────────

bool SDLCALL EventWatch::Trampoline(void *ud, SDL_Event *ev) noexcept {
    Event e;
    e.raw = *ev;
    return static_cast<Ctx *>(ud)->fn(e);
}

EventWatch::~EventWatch() {
    if (ctx) {
        SDL_RemoveEventWatch(Trampoline, ctx);
        delete ctx;
    }
}

Option<Event> PollEvent() noexcept {
    Event e;
    if (SDL_PollEvent(&e.raw))
        return Some(e);
    return NONE;
}

Option<Event> WaitEvent(int timeoutMs) noexcept {
    Event e;
    if (SDL_WaitEventTimeout(&e.raw, timeoutMs))
        return Some(e);
    return NONE;
}

bool HasEvents(EventType minType, EventType maxType) noexcept {
    return SDL_HasEvents(uint32_t(minType), uint32_t(maxType));
}

std::vector<Event> PeekEvents(int max, EventType minType, EventType maxType) {
    std::vector<SDL_Event> buf(max);
    int n = SDL_PeepEvents(buf.data(), max, SDL_PEEKEVENT, uint32_t(minType), uint32_t(maxType));
    if (n <= 0)
        return {};
    std::vector<Event> out(n);
    for (int i = 0; i < n; ++i)
        out[i].raw = buf[i];
    return out;
}

std::vector<Event> GetEvents(int max, EventType minType, EventType maxType) {
    std::vector<SDL_Event> buf(max);
    int n = SDL_PeepEvents(buf.data(), max, SDL_GETEVENT, uint32_t(minType), uint32_t(maxType));
    if (n <= 0)
        return {};
    std::vector<Event> out(n);
    for (int i = 0; i < n; ++i)
        out[i].raw = buf[i];
    return out;
}

bool PushUserEvent(uint32_t type, int32_t code, void *data1, void *data2) noexcept {
    SDL_Event e{};
    e.user.type = type;
    e.user.code = code;
    e.user.data1 = data1;
    e.user.data2 = data2;
    return SDL_PushEvent(&e);
}

void FlushEvents(EventType minType, EventType maxType) noexcept {
    SDL_FlushEvents(uint32_t(minType), uint32_t(maxType));
}

void FilterEvents(std::function<bool(const Event &)> fn) noexcept {
    struct Ctx {
        std::function<bool(const Event &)> &fn;
    };
    Ctx ctx{fn};
    SDL_FilterEvents(
        [](void *ud, SDL_Event *ev) -> bool {
            Ctx &c = *static_cast<Ctx *>(ud);
            Event e;
            e.raw = *ev;
            return c.fn(e);
        },
        &ctx);
}

} // namespace sdl3
