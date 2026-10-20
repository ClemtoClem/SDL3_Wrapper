// Définitions de platform/events.hpp
#include <platform/events.hpp>

namespace platform {

bool TranslateEvent(const sdl3::Event &event, InputState &input, events::EventBus &bus) {
	switch (event.Type()) {
	case SDL_EVENT_QUIT:
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		bus.Publish(QuitRequested{});
		return true;
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		bus.Publish(WindowResized{event.Window().data1, event.Window().data2});
		return true;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		bus.Publish(FocusChanged{true});
		return true;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		input.ReleaseAll();
		bus.Publish(FocusChanged{false});
		return true;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP: {
		const SDL_KeyboardEvent &k = event.Key();
		input.OnKey(int(k.scancode), k.down);
		bus.Publish(KeyChanged{int(k.scancode), int(k.key), k.down, k.repeat});
		return true;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP: {
		const SDL_MouseButtonEvent &b = event.MouseButton();
		const auto button = MouseButton(b.button);
		input.OnMouseButton(button, b.down);
		input.OnMouseMove(b.x, b.y);
		bus.Publish(MouseButtonChanged{button, b.down, b.x, b.y, int(b.clicks)});
		return true;
	}
	case SDL_EVENT_MOUSE_MOTION: {
		const SDL_MouseMotionEvent &m = event.MouseMotion();
		input.OnMouseMove(m.x, m.y);
		bus.Publish(MouseMoved{m.x, m.y, m.xrel, m.yrel});
		return true;
	}
	case SDL_EVENT_MOUSE_WHEEL: {
		const SDL_MouseWheelEvent &w = event.MouseWheel();
		input.OnMouseWheel(w.x, w.y);
		bus.Publish(MouseWheelMoved{w.x, w.y});
		return true;
	}
	case SDL_EVENT_TEXT_INPUT:
		bus.Publish(TextEntered{event.TextInput().text ? event.TextInput().text : ""});
		return true;
	default:
		return false;
	}
}

} // namespace platform
