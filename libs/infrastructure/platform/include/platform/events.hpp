#pragma once
/**
 * Événements de plate-forme publiés sur un events::EventBus, et leur
 * traduction depuis les événements SDL.
 *
 * Les modules de plus haut niveau (UI, gameplay, éditeur) s'abonnent à ces
 * types simples plutôt qu'à SDL_Event : ils restent testables en publiant
 * eux-mêmes un platform::KeyChanged sur un bus, sans fenêtre ni SDL_Init.
 */
#include <cstdint>
#include <string>

#include <events/event_bus.hpp>
#include <sdl3/events.hpp>

#include "input_state.hpp"

namespace platform {

/// L'utilisateur ou le système demande la fermeture de l'application.
struct QuitRequested {};

/// Taille de la zone de rendu de la fenêtre, en pixels.
struct WindowResized {
	int width = 0, height = 0;
};

struct FocusChanged {
	bool focused = false;
};

struct KeyChanged {
	int scancode = 0;
	int keycode = 0;
	bool down = false;
	bool repeat = false;
};

struct MouseButtonChanged {
	MouseButton button = MouseButton::Left;
	bool down = false;
	float x = 0.f, y = 0.f;
	int clicks = 1;
};

struct MouseMoved {
	float x = 0.f, y = 0.f, dx = 0.f, dy = 0.f;
};

struct MouseWheelMoved {
	float dx = 0.f, dy = 0.f;
};

struct TextEntered {
	std::string text;
};

/// Met `input` à jour et publie l'événement de plate-forme correspondant sur
/// `bus` (Publish immédiat). Rend faux pour un type d'événement non traduit.
/// Fonction pure vis-à-vis de SDL : n'appelle aucune fonction de SDL, et peut
/// donc être testée avec un sdl3::Event construit à la main.
bool TranslateEvent(const sdl3::Event &event, InputState &input, events::EventBus &bus);

} // namespace platform
