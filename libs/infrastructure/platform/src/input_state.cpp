// Définitions de platform/input_state.hpp
#include <platform/input_state.hpp>

namespace platform {

void InputState::BeginFrame() noexcept {
	m_keysPressed.reset();
	m_keysReleased.reset();
	m_buttonsPressed.reset();
	m_buttonsReleased.reset();
	m_frameMouseX = m_mouseX;
	m_frameMouseY = m_mouseY;
	m_wheelX = m_wheelY = 0.f;
}

void InputState::OnKey(int scancode, bool down) noexcept {
	if (!ValidKey(scancode))
		return;
	if (down && !m_keys[size_t(scancode)])
		m_keysPressed.set(size_t(scancode));
	if (!down && m_keys[size_t(scancode)])
		m_keysReleased.set(size_t(scancode));
	m_keys.set(size_t(scancode), down);
}

void InputState::OnMouseButton(MouseButton button, bool down) noexcept {
	if (!ValidButton(button))
		return;
	const size_t i = size_t(button);
	if (down && !m_buttons[i])
		m_buttonsPressed.set(i);
	if (!down && m_buttons[i])
		m_buttonsReleased.set(i);
	m_buttons.set(i, down);
}

void InputState::OnMouseMove(float x, float y) noexcept {
	m_mouseX = x;
	m_mouseY = y;
}

void InputState::OnMouseWheel(float dx, float dy) noexcept {
	m_wheelX += dx;
	m_wheelY += dy;
}

void InputState::ReleaseAll() noexcept {
	m_keysReleased |= m_keys;
	m_buttonsReleased |= m_buttons;
	m_keys.reset();
	m_buttons.reset();
}

bool InputState::IsDown(int s) const noexcept { return ValidKey(s) && m_keys[size_t(s)]; }
bool InputState::WasPressed(int s) const noexcept { return ValidKey(s) && m_keysPressed[size_t(s)]; }
bool InputState::WasReleased(int s) const noexcept { return ValidKey(s) && m_keysReleased[size_t(s)]; }

bool InputState::IsDown(MouseButton b) const noexcept { return ValidButton(b) && m_buttons[size_t(b)]; }
bool InputState::WasPressed(MouseButton b) const noexcept { return ValidButton(b) && m_buttonsPressed[size_t(b)]; }
bool InputState::WasReleased(MouseButton b) const noexcept { return ValidButton(b) && m_buttonsReleased[size_t(b)]; }

} // namespace platform
