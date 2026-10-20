#pragma once
/**
 * platform::InputState — état instantané du clavier et de la souris, avec
 * les transitions de l'image courante (appuyé / relâché CETTE image).
 *
 * Alimenté par TranslateEvent (events.hpp) ou directement par les méthodes
 * On*(), ce qui permet de tester la logique de jeu sans SDL : un test simule
 * une frappe par OnKey(scancode, true) puis vérifie WasPressed(scancode).
 *
 * Cycle : BeginFrame() efface les transitions de l'image précédente, puis
 * les événements de l'image s'accumulent, puis la logique lit l'état.
 */
#include <array>
#include <bitset>
#include <cstdint>

namespace platform {

enum class MouseButton : uint8_t { Left = 1, Middle = 2, Right = 3, X1 = 4, X2 = 5 };

class InputState {
public:
	static constexpr int KEY_COUNT = 512; // SDL_SCANCODE_COUNT
	static constexpr int BUTTON_COUNT = 6;

	void BeginFrame() noexcept;

	// ── Alimentation ─────────────────────────────────────────────────────
	void OnKey(int scancode, bool down) noexcept;
	void OnMouseButton(MouseButton button, bool down) noexcept;
	void OnMouseMove(float x, float y) noexcept;
	void OnMouseWheel(float dx, float dy) noexcept;
	/// Perte du focus : tout est relâché (évite les touches « collées »).
	void ReleaseAll() noexcept;

	// ── Lecture ──────────────────────────────────────────────────────────
	[[nodiscard]] bool IsDown(int scancode) const noexcept;
	[[nodiscard]] bool WasPressed(int scancode) const noexcept;
	[[nodiscard]] bool WasReleased(int scancode) const noexcept;

	[[nodiscard]] bool IsDown(MouseButton b) const noexcept;
	[[nodiscard]] bool WasPressed(MouseButton b) const noexcept;
	[[nodiscard]] bool WasReleased(MouseButton b) const noexcept;

	[[nodiscard]] float MouseX() const noexcept { return m_mouseX; }
	[[nodiscard]] float MouseY() const noexcept { return m_mouseY; }
	/// Déplacement de la souris depuis BeginFrame().
	[[nodiscard]] float MouseDeltaX() const noexcept { return m_mouseX - m_frameMouseX; }
	[[nodiscard]] float MouseDeltaY() const noexcept { return m_mouseY - m_frameMouseY; }
	/// Molette cumulée depuis BeginFrame().
	[[nodiscard]] float WheelX() const noexcept { return m_wheelX; }
	[[nodiscard]] float WheelY() const noexcept { return m_wheelY; }

private:
	static bool ValidKey(int s) noexcept { return s >= 0 && s < KEY_COUNT; }
	static bool ValidButton(MouseButton b) noexcept { return int(b) > 0 && int(b) < BUTTON_COUNT; }

	std::bitset<KEY_COUNT> m_keys, m_keysPressed, m_keysReleased;
	std::bitset<BUTTON_COUNT> m_buttons, m_buttonsPressed, m_buttonsReleased;
	float m_mouseX = 0.f, m_mouseY = 0.f, m_frameMouseX = 0.f, m_frameMouseY = 0.f;
	float m_wheelX = 0.f, m_wheelY = 0.f;
};

} // namespace platform
