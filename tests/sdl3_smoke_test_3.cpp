// Smoke test : events, keyboard/mouse/cursor, gamepad, joystick, haptic, touch, pen.
#include "sdl3/sdl3.hpp"
#include <cstdlib>
#include <iostream>

int main() {
	setenv("SDL_VIDEODRIVER", "dummy", 0);

	using namespace sdl3;

	auto sdl = sdl3::SdlContext::Create(init_flags::VIDEO | init_flags::EVENTS | init_flags::JOYSTICK |
										init_flags::GAMEPAD | init_flags::HAPTIC);
	if (!sdl) {
		std::cerr << "sdl init failed: " << sdl3::GetError().CStr() << "\n";
		return 1;
	}

	// --- keyboard / mouse / cursor ---
	auto kb = keyboard::State();
	std::cout << "keyboard scancode count=" << kb.size() << "\n";
	auto ms = mouse::State();
	std::cout << "mouse pos=" << ms.x << "," << ms.y << "\n";

	auto cursorRes = Cursor::FromSystem(SystemCursor::MOVE);
	if (cursorRes)
		std::cout << "system cursor created ok\n";
	else
		std::cerr << "cursor failed: " << cursorRes.Error().CStr() << "\n";

	// --- gamepad / joystick ---
	std::cout << "has gamepad=" << Gamepad::Any() << "\n";
	for (auto id : Gamepad::Enumerated()) {
		auto g = Gamepad::Open(id);
		if (g)
			std::cout << "  gamepad: " << g.Value().Name() << "\n";
	}

	std::cout << "has joystick=" << joystick::Any() << "\n";
	for (auto id : joystick::Enumerated()) {
		std::cout << "  joystick: " << joystick::NameFor(id) << " type=" << int(joystick::TypeFor(id)) << "\n";
		auto j = Joystick::Open(id);
		if (j) {
			auto &jj = j.Value();
			std::cout << "    axes=" << jj.GetNumAxes() << " buttons=" << jj.GetNumButtons() << "\n";
		}
	}

	// --- haptic ---
	std::cout << "mouse haptic=" << haptic::MouseHaptic() << "\n";
	for (auto id : haptic::Enumerated()) {
		std::cout << "  haptic: " << haptic::NameFor(id) << "\n";
		auto h = Haptic::Open(id);
		if (h)
			std::cout << "    rumble supported=" << h.Value().RumbleSupported() << "\n";
	}

	// --- touch ---
	auto touchDevices = touch::Devices();
	std::cout << "touch devices: " << touchDevices.size() << "\n";
	for (auto id : touchDevices) {
		std::cout << "  " << touch::DeviceName(id) << " type=" << int(touch::DeviceType(id)) << "\n";
		auto fingers = touch::Fingers(id);
		std::cout << "    fingers down: " << fingers.size() << "\n";
	}

	// --- pen (aucun stylet réel attendu ici : vérifie juste que l'appel ne plante pas) ---
	std::cout << "pen device type (id=0): " << int(pen::DeviceType(0)) << "\n";

	// --- events ---
	int watchCount = 0;
	EventWatch watch([&](const Event &) {
		watchCount++;
		return true;
	});

	uint32_t custom = RegisterEvents(1);
	std::cout << "custom event type=" << custom << "\n";

	keyboard::Pump();
	int polled = 0;
	while (auto ev = PollEvent()) {
		(void)ev;
		polled++;
	}
	std::cout << "polled=" << polled << " watchCount=" << watchCount << "\n";

	std::cout << "smoke test 3 done\n";
	return 0;
}
