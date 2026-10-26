// Définitions de sdl3/input.hpp
#include "sdl3/input.hpp"
#include "sdl3/events.hpp"

#include <cmath>

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

// ── Boutons normalisés ───────────────────────────────────────────────────────

Option<ButtonInput> ReadButton(const Event &ev) noexcept {
	ButtonInput in;
	in.timestampMs = ev.Timestamp() / 1000000u;
	in.mods = uint16_t(SDL_GetModState());
	switch (ev.Type()) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		in.source = ButtonSource::Mouse;
		in.code = ev.raw.button.button;
		in.down = ev.raw.button.down;
		in.position = {ev.raw.button.x, ev.raw.button.y};
		in.device = ev.raw.button.which;
		return Some(in);
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		in.source = ButtonSource::Key;
		in.code = int(ev.raw.key.key);
		in.down = ev.raw.key.down;
		in.repeat = ev.raw.key.repeat;
		in.mods = uint16_t(ev.raw.key.mod);
		in.device = ev.raw.key.which;
		return Some(in);
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		in.source = ButtonSource::Gamepad;
		in.code = ev.raw.gbutton.button;
		in.down = ev.raw.gbutton.down;
		in.device = ev.raw.gbutton.which;
		return Some(in);
	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
	case SDL_EVENT_JOYSTICK_BUTTON_UP:
		in.source = ButtonSource::Joystick;
		in.code = ev.raw.jbutton.button;
		in.down = ev.raw.jbutton.down;
		in.device = ev.raw.jbutton.which;
		return Some(in);
	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_UP: {
		in.source = ButtonSource::Finger;
		in.code = 0;
		in.down = ev.Type() == SDL_EVENT_FINGER_DOWN;
		in.device = ev.raw.tfinger.fingerID;
		// Coordonnées normalisées [0,1] : ramenées en pixels de fenêtre.
		int w = 0, h = 0;
		if (SDL_Window *win = SDL_GetWindowFromID(ev.raw.tfinger.windowID))
			SDL_GetWindowSize(win, &w, &h);
		in.position = {ev.raw.tfinger.x * float(w), ev.raw.tfinger.y * float(h)};
		return Some(in);
	}
	case SDL_EVENT_PEN_DOWN:
	case SDL_EVENT_PEN_UP:
		in.source = ButtonSource::Pen;
		in.code = 0; // pointe
		in.down = ev.Type() == SDL_EVENT_PEN_DOWN;
		in.position = {ev.raw.ptouch.x, ev.raw.ptouch.y};
		in.device = ev.raw.ptouch.which;
		return Some(in);
	case SDL_EVENT_PEN_BUTTON_DOWN:
	case SDL_EVENT_PEN_BUTTON_UP:
		in.source = ButtonSource::Pen;
		in.code = ev.raw.pbutton.button;
		in.down = ev.Type() == SDL_EVENT_PEN_BUTTON_DOWN;
		in.position = {ev.raw.pbutton.x, ev.raw.pbutton.y};
		in.device = ev.raw.pbutton.which;
		return Some(in);
	default:
		return NONE;
	}
}

// ── ButtonGestureTracker ─────────────────────────────────────────────────────

ButtonGestureTracker::State &ButtonGestureTracker::StateOf(ButtonSource source, int code) {
	for (State &s : m_states)
		if (s.source == source && s.code == code)
			return s;
	m_states.push_back(State{source, code});
	return m_states.back();
}

std::vector<ButtonGesture> ButtonGestureTracker::Feed(const ButtonInput &in, bool sameTarget) {
	std::vector<ButtonGesture> out;
	State &s = StateOf(in.source, in.code);
	if (in.down) {
		if (in.repeat && s.down) {
			out.push_back({ButtonEvent::Repeat, s.clicks});
			return out;
		}
		const float dx = in.position.x - s.lastClickPos.x, dy = in.position.y - s.lastClickPos.y;
		const bool chained = sameTarget && s.clicks > 0 && in.timestampMs - s.lastClickMs <= multiClickMs &&
							 std::sqrt(dx * dx + dy * dy) <= slop;
		if (!chained)
			s.clicks = 0;
		s.down = true;
		s.moved = false;
		s.pressMs = in.timestampMs;
		s.pressPos = in.position;
		out.push_back({ButtonEvent::Press, s.clicks + 1});
		return out;
	}
	const bool wasDown = s.down;
	s.down = false;
	out.push_back({ButtonEvent::Release, s.clicks});
	if (!wasDown || !sameTarget || s.moved) {
		s.clicks = 0;
		return out;
	}
	++s.clicks;
	s.lastClickMs = in.timestampMs;
	s.lastClickPos = in.position;
	out.push_back({ButtonEvent::Clic, s.clicks});
	if (s.clicks == 2)
		out.push_back({ButtonEvent::DoubleClic, 2});
	if (s.clicks >= 2)
		out.push_back({ButtonEvent::MultiClicks, s.clicks});
	return out;
}

void ButtonGestureTracker::Move(ButtonSource source, int code, FPoint position) noexcept {
	for (State &s : m_states)
		if (s.source == source && s.code == code && s.down) {
			const float dx = position.x - s.pressPos.x, dy = position.y - s.pressPos.y;
			if (std::sqrt(dx * dx + dy * dy) > slop)
				s.moved = true;
		}
}

std::vector<ButtonGestureTracker::Held> ButtonGestureTracker::Update(uint64_t nowMs) const {
	std::vector<Held> out;
	for (const State &s : m_states)
		if (s.down)
			out.push_back({s.source, s.code, nowMs >= s.pressMs ? nowMs - s.pressMs : 0, s.moved});
	return out;
}

int ButtonGestureTracker::ClickCount(ButtonSource source, int code) const noexcept {
	for (const State &s : m_states)
		if (s.source == source && s.code == code)
			return s.clicks;
	return 0;
}

bool ButtonGestureTracker::IsDown(ButtonSource source, int code) const noexcept {
	for (const State &s : m_states)
		if (s.source == source && s.code == code)
			return s.down;
	return false;
}

} // namespace sdl3
