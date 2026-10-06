// Définitions de sdl3/sensor.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/sensor.hpp"

namespace sdl3 {

namespace sensor {

std::vector<SensorID> Enumerated() {
	int count = 0;
	SensorID *ids = SDL_GetSensors(&count);
	if (!ids)
		return {};
	std::vector<SensorID> v(ids, ids + count);
	SDL_free(ids);
	return v;
}

} // namespace sensor

namespace detail {

SensorType GetSensorType(SDL_Sensor *h) noexcept {
	return h ? SensorType(SDL_GetSensorType(h)) : SensorType::INVALID;
}

int SensorNonPortableType(SDL_Sensor *h) noexcept {
	return h ? SDL_GetSensorNonPortableType(h) : -1;
}

SDL_PropertiesID SensorProperties(SDL_Sensor *h) noexcept {
	return h ? SDL_GetSensorProperties(h) : 0;
}

} // namespace detail

// ── Sensor ───────────────────────────────────────────────────────────────────

Result<Sensor, StringView> Sensor::Open(SensorID id) {
	auto *s = SDL_OpenSensor(id);
	if (!s)
		return Err(GetError());
	return Ok(Sensor(s));
}

Option<SensorView> Sensor::FromId(SensorID id) noexcept {
	auto *s = SDL_GetSensorFromID(id);
	if (!s)
		return NONE;
	return Some(SensorView(s));
}

} // namespace sdl3
