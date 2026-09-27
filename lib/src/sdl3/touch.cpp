// Définitions de sdl3/touch.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/touch.hpp"

namespace sdl3 {

namespace touch {

std::vector<SDL_TouchID> Devices() {
	int count = 0;
	SDL_TouchID *ids = SDL_GetTouchDevices(&count);
	if (!ids)
		return {};
	std::vector<SDL_TouchID> v(ids, ids + count);
	SDL_free(ids);
	return v;
}

TouchDeviceType DeviceType(SDL_TouchID id) noexcept {
	return TouchDeviceType(SDL_GetTouchDeviceType(id));
}

std::vector<Finger> Fingers(SDL_TouchID id) {
	int count = 0;
	SDL_Finger **raw = SDL_GetTouchFingers(id, &count);
	if (!raw)
		return {};
	std::vector<Finger> out;
	out.reserve(size_t(count));
	for (int i = 0; i < count; ++i)
		out.emplace_back(*raw[i]);
	SDL_free(raw);
	return out;
}

} // namespace touch

} // namespace sdl3
