// Définitions de ui/events.hpp
#include <ui/events.hpp>

#include <ui/widget.hpp>

namespace ui {

namespace {

/// Groupes de modificateurs (gauche/droite indifférents).
bool ModsSatisfied(uint16_t required, uint16_t have) noexcept {
	const uint16_t groups[] = {uint16_t(sdl3::keyboard::Keymod::CTRL), uint16_t(sdl3::keyboard::Keymod::SHIFT), uint16_t(sdl3::keyboard::Keymod::ALT),
							   uint16_t(sdl3::keyboard::Keymod::GUI)};
	for (uint16_t g : groups)
		if ((required & g) && !(have & g))
			return false;
	return true;
}

} // namespace

bool Trigger::Matches(const Trigger &occurred) const noexcept {
	if (source != occurred.source)
		return false;
	if (code != ANY && occurred.code != ANY && code != occurred.code)
		return false;
	if (!ModsSatisfied(mods, occurred.mods))
		return false;
	switch (source) {
	case InputSource::MouseButton:
	case InputSource::Key:
	case InputSource::GamepadButton:
	case InputSource::JoystickButton:
	case InputSource::Finger:
	case InputSource::PenButton:
		if (gesture != occurred.gesture) {
			// Une touche maintenue répète son appui : une liaison Press la suit.
			if (!(gesture == sdl3::ButtonEvent::Press && occurred.gesture == sdl3::ButtonEvent::Repeat &&
				  source == InputSource::Key))
				return false;
		}
		if (gesture == sdl3::ButtonEvent::MultiClicks)
			return param <= 0 ? occurred.param >= 2 : occurred.param == param;
		if (gesture == sdl3::ButtonEvent::Hold)
			return occurred.param >= (param > 0 ? param : 500);
		return true;
	case InputSource::GamepadAxis:
	case InputSource::JoystickAxis:
		// Seuil signé : positif = au-delà vers le haut, négatif = vers le bas.
		if (param >= 0)
			return occurred.param >= param;
		return occurred.param <= param;
	default:
		return true;
	}
}

uint32_t Bindings::Add(std::vector<Trigger> triggers, EventCallback cb, bool internal) {
	Binding b;
	b.id = nextId++;
	b.triggers = std::move(triggers);
	b.callback = std::move(cb);
	b.internal = internal;
	list.push_back(std::move(b));
	return list.back().id;
}

size_t Bindings::Remove(const Trigger &t) {
	size_t touched = 0;
	for (Binding &b : list) {
		if (b.internal)
			continue;
		const size_t before = b.triggers.size();
		std::erase(b.triggers, t);
		if (b.triggers.size() != before)
			++touched;
	}
	std::erase_if(list, [](const Binding &b) { return !b.internal && b.triggers.empty(); });
	return touched;
}

bool Bindings::RemoveId(uint32_t id) {
	return std::erase_if(list, [id](const Binding &b) { return b.id == id; }) > 0;
}

void Bindings::RemoveAll(bool includeInternal) {
	std::erase_if(list, [includeInternal](const Binding &b) { return includeInternal || !b.internal; });
}

bool Bindings::Has(const Trigger &t) const {
	for (const Binding &b : list)
		if (!b.internal)
			for (const Trigger &x : b.triggers)
				if (x == t)
					return true;
	return false;
}

Option<BindPair> Bindings::Get(const Trigger &t) const {
	for (const Binding &b : list)
		if (!b.internal)
			for (const Trigger &x : b.triggers)
				if (x == t)
					return Some(BindPair{b.triggers, b.callback});
	return NONE;
}

std::vector<BindPair> Bindings::All() const {
	std::vector<BindPair> out;
	for (const Binding &b : list)
		if (!b.internal)
			out.emplace_back(b.triggers, b.callback);
	return out;
}

bool Bindings::Listens(const Trigger &occurred) const {
	for (const Binding &b : list)
		if (b.enabled)
			for (const Trigger &t : b.triggers)
				if (t.Matches(occurred))
					return true;
	return false;
}

Widget WidgetEvent::Target() const { return world ? Widget(*world, target) : Widget(); }
Widget WidgetEvent::Current() const { return world ? Widget(*world, currentTarget) : Widget(); }

String DescribeTrigger(const Trigger &t) {
	auto gesture = [](sdl3::ButtonEvent g, int p) -> String {
		switch (g) {
		case sdl3::ButtonEvent::Press:
			return "appui";
		case sdl3::ButtonEvent::Release:
			return "relâchement";
		case sdl3::ButtonEvent::Clic:
			return "clic";
		case sdl3::ButtonEvent::DoubleClic:
			return "double clic";
		case sdl3::ButtonEvent::MultiClicks:
			return String::From(p) + " clics";
		case sdl3::ButtonEvent::Hold:
			return String("appui long ") + String::From(p > 0 ? p : 500) + " ms";
		case sdl3::ButtonEvent::Repeat:
			return "répétition";
		}
		return String();
	};
	String mods;
	if (t.mods & uint16_t(sdl3::keyboard::Keymod::CTRL))
		mods += "Ctrl+";
	if (t.mods & uint16_t(sdl3::keyboard::Keymod::SHIFT))
		mods += "Maj+";
	if (t.mods & uint16_t(sdl3::keyboard::Keymod::ALT))
		mods += "Alt+";
	switch (t.source) {
	case InputSource::MouseButton:
		return mods + "souris " + String::From(t.code) + " " + gesture(t.gesture, t.param);
	case InputSource::Key:
		return mods + String(SDL_GetKeyName(SDL_Keycode(t.code))) + " " + gesture(t.gesture, t.param);
	case InputSource::GamepadButton:
		return String("manette ") + String(SDL_GetGamepadStringForButton(SDL_GamepadButton(t.code))) + " " +
			   gesture(t.gesture, t.param);
	case InputSource::JoystickButton:
		return String("joystick ") + String::From(t.code) + " " + gesture(t.gesture, t.param);
	case InputSource::Finger:
		return String("doigt ") + gesture(t.gesture, t.param);
	case InputSource::PenButton:
		return String("stylet ") + gesture(t.gesture, t.param);
	default:
		break;
	}
	return String("évènement ") + String::From(int(t.source));
}

} // namespace ui
