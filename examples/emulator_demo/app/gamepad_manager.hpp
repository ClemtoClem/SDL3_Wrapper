#pragma once
/**
 * GamepadManager — table d'affectation clavier/manette des 12 boutons de la
 * console, persistée via `Settings::add()` (fichier de réglages).
 *
 * Encodage d'une case : une valeur < GAMEPAD_BUTTON_ENCODING_OFFSET est un
 * `SDL_Keycode` brut ; au-delà, `OFFSET + SDL_GamepadButton`. Toujours
 * positive, car `Settings::load()` n'analyse que les valeurs commençant par
 * un chiffre.
 */
#include <array>
#include <vector>

#include "sdl3/sdl3.hpp"

#include "../emulator/settings.hpp"
#include "cli.hpp"

namespace emulator_demo::app {

class GamepadManager {
public:
	GamepadManager() {
		static constexpr const char *SETTING_NAMES[BTN_COUNT] = {"bindA",	  "bindB",	  "bindSelect", "bindStart",
																 "bindRight", "bindLeft", "bindUp",		"bindDown",
																 "bindR",	  "bindL",	  "bindX",		"bindY"};
		static constexpr SDL_Keycode DEFAULT_KEYS[BTN_COUNT] = {SDLK_D,		SDLK_S,	   SDLK_SPACE, SDLK_RETURN,
																SDLK_RIGHT, SDLK_LEFT, SDLK_UP,	   SDLK_DOWN,
																SDLK_E,		SDLK_A,	   SDLK_Z,	   SDLK_Q};
		std::vector<Setting> toRegister;
		for (int i = 0; i < BTN_COUNT; ++i) {
			m_bindings[size_t(i)] = int(DEFAULT_KEYS[i]);
			toRegister.emplace_back(SETTING_NAMES[i], &m_bindings[size_t(i)], false);
		}
		Settings::add(toRegister);
	}

	GamepadManager(const GamepadManager &) = delete;
	GamepadManager &operator=(const GamepadManager &) = delete;

	/// Ouvre les manettes déjà branchées — à appeler une fois le sous-système
	/// GAMEPAD initialisé (pas depuis le constructeur : il précède SDL_Init).
	void OpenConnectedPads() {
		for (auto id : sdl3::Gamepad::Enumerated()) {
			auto pad = sdl3::Gamepad::Open(id);
			if (pad.IsOk())
				m_pads.push_back(std::move(pad).Unwrap());
		}
	}

	void HandleEvent(const sdl3::Event &event) {
		if (event.IsGamepadAdded()) {
			auto pad = sdl3::Gamepad::Open(event.GamepadDevice().which);
			if (pad.IsOk())
				m_pads.push_back(std::move(pad).Unwrap());
		} else if (event.IsGamepadRemoved()) {
			SDL_JoystickID id = event.GamepadDevice().which;
			std::erase_if(m_pads, [id](const sdl3::Gamepad &pad) { return pad.GetId() == id; });
		}
	}

	[[nodiscard]] bool IsPressed(int button) const {
		if (button < 0 || button >= BTN_COUNT)
			return false;
		int encoded = m_bindings[size_t(button)];
		if (IsGamepadBinding(encoded)) {
			for (const sdl3::Gamepad &pad : m_pads)
				if (pad.Button(ToGamepadButton(encoded)))
					return true;
			return false;
		}
		return sdl3::keyboard::IsPressed(SDL_Keycode(encoded));
	}

	/// Masque des boutons tenus (bit i = bouton i).
	[[nodiscard]] uint32_t PressedMask() const {
		uint32_t mask = 0;
		for (int button = 0; button < BTN_COUNT; ++button)
			if (IsPressed(button))
				mask |= 1u << button;
		return mask;
	}

	void RebindKeyboard(int button, SDL_Keycode key) {
		if (button >= 0 && button < BTN_COUNT)
			m_bindings[size_t(button)] = int(key);
	}
	void RebindGamepad(int button, sdl3::GamepadButton padButton) {
		if (button >= 0 && button < BTN_COUNT)
			m_bindings[size_t(button)] = GAMEPAD_BUTTON_ENCODING_OFFSET + int(padButton);
	}

	[[nodiscard]] bool IsGamepadBound(int button) const {
		return button >= 0 && button < BTN_COUNT && IsGamepadBinding(m_bindings[size_t(button)]);
	}
	[[nodiscard]] SDL_Keycode BoundKey(int button) const {
		return (button >= 0 && button < BTN_COUNT) ? SDL_Keycode(m_bindings[size_t(button)]) : SDLK_UNKNOWN;
	}
	[[nodiscard]] sdl3::GamepadButton BoundGamepadButton(int button) const {
		return (button >= 0 && button < BTN_COUNT) ? ToGamepadButton(m_bindings[size_t(button)])
												   : sdl3::GamepadButton::INVALID;
	}
	[[nodiscard]] int ConnectedPads() const { return int(m_pads.size()); }

private:
	static constexpr int GAMEPAD_BUTTON_ENCODING_OFFSET = 0x60000000;

	[[nodiscard]] static bool IsGamepadBinding(int encoded) noexcept { return encoded >= GAMEPAD_BUTTON_ENCODING_OFFSET; }
	[[nodiscard]] static sdl3::GamepadButton ToGamepadButton(int encoded) noexcept {
		return sdl3::GamepadButton(encoded - GAMEPAD_BUTTON_ENCODING_OFFSET);
	}

	std::array<int, BTN_COUNT> m_bindings{};
	std::vector<sdl3::Gamepad> m_pads;
};

} // namespace emulator_demo::app
