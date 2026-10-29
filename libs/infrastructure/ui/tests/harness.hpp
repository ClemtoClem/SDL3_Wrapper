#pragma once
// Banc d'essai sans fenêtre : interface sur un monde ECS, évènements SDL
// synthétiques (souris, clavier, manette, texte) horodatés.
#include <ui/ui.hpp>

struct Harness {
	ecs::ArchetypeRegistry world;
	ui::Ui gui;
	uint64_t timeNs = 1'000'000'000ull;

	/// Scène de test : remplit la fenêtre mais n'étire pas ses enfants (la
	/// racine, elle, étire une page montée directement sous elle).
	ui::Widget stage;

	explicit Harness(sdl3::FPoint viewport = {800.f, 600.f}) : gui(world, viewport) {
		stage = gui.Mount(ui::Column().Id("stage").AlignItems(ui::Align::Start).Gap(0.f));
	}

	ui::Widget Mount(const ui::AnyWidget &w) {
		ui::Widget out = stage.Append(w);
		Frame();
		return out;
	}
	void Frame() { gui.Layout(); }
	void Advance(float seconds) {
		timeNs += uint64_t(double(seconds) * 1e9);
		gui.Update(seconds);
		Frame();
	}

	sdl3::Event Base(SDL_EventType type) {
		sdl3::Event ev;
		ev.raw.type = type;
		timeNs += 20'000'000ull; // 20 ms entre deux évènements
		ev.raw.common.timestamp = timeNs;
		return ev;
	}
	bool Send(sdl3::Event ev) {
		const bool consumed = gui.HandleEvent(ev);
		Frame();
		return consumed;
	}
	bool Move(float x, float y) {
		sdl3::Event ev = Base(SDL_EVENT_MOUSE_MOTION);
		ev.raw.motion.x = x;
		ev.raw.motion.y = y;
		return Send(ev);
	}
	bool Down(float x, float y, uint8_t button = SDL_BUTTON_LEFT) {
		Move(x, y);
		sdl3::Event ev = Base(SDL_EVENT_MOUSE_BUTTON_DOWN);
		ev.raw.button.button = button;
		ev.raw.button.down = true;
		ev.raw.button.x = x;
		ev.raw.button.y = y;
		ev.raw.button.clicks = 1;
		return Send(ev);
	}
	bool Up(float x, float y, uint8_t button = SDL_BUTTON_LEFT) {
		sdl3::Event ev = Base(SDL_EVENT_MOUSE_BUTTON_UP);
		ev.raw.button.button = button;
		ev.raw.button.down = false;
		ev.raw.button.x = x;
		ev.raw.button.y = y;
		return Send(ev);
	}
	bool Click(float x, float y, uint8_t button = SDL_BUTTON_LEFT) {
		const bool a = Down(x, y, button);
		const bool b = Up(x, y, button);
		return a || b;
	}
	bool Click(const ui::Widget &w, uint8_t button = SDL_BUTTON_LEFT) {
		const sdl3::FPoint c = Center(w);
		return Click(c.x, c.y, button);
	}
	bool Drag(float x0, float y0, float x1, float y1, int steps = 4) {
		Down(x0, y0);
		for (int i = 1; i <= steps; ++i)
			Move(x0 + (x1 - x0) * float(i) / float(steps), y0 + (y1 - y0) * float(i) / float(steps));
		return Up(x1, y1);
	}
	bool Wheel(float x, float y, float dy) {
		Move(x, y);
		sdl3::Event ev = Base(SDL_EVENT_MOUSE_WHEEL);
		ev.raw.wheel.x = 0.f;
		ev.raw.wheel.y = dy;
		ev.raw.wheel.mouse_x = x;
		ev.raw.wheel.mouse_y = y;
		ev.raw.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
		return Send(ev);
	}
	bool Key(SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE, bool down = true) {
		sdl3::Event ev = Base(down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP);
		ev.raw.key.key = key;
		ev.raw.key.mod = mod;
		ev.raw.key.down = down;
		return Send(ev);
	}
	bool Press(SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE) {
		const bool a = Key(key, mod, true);
		const bool b = Key(key, mod, false);
		return a || b;
	}
	bool Type(const char *text) {
		sdl3::Event ev = Base(SDL_EVENT_TEXT_INPUT);
		ev.raw.text.text = text;
		return Send(ev);
	}
	bool Pad(sdl3::GamepadButton b, bool down) {
		sdl3::Event ev = Base(down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP);
		ev.raw.gbutton.button = uint8_t(int(b));
		ev.raw.gbutton.down = down;
		return Send(ev);
	}
	bool PadClick(sdl3::GamepadButton b) {
		const bool a = Pad(b, true);
		const bool c = Pad(b, false);
		return a || c;
	}

	static sdl3::FPoint Center(const ui::Widget &w) {
		const sdl3::FRect r = w.Bounds();
		return {r.x + r.w * 0.5f, r.y + r.h * 0.5f};
	}
};

inline bool Near(float a, float b, float eps = 0.51f) { return std::fabs(a - b) <= eps; }
