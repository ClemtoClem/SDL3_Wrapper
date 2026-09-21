#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <vector>

#include "../core/core.hpp"

namespace sdl3 {

// ============================================================================
// SensorType / SensorID
// ============================================================================

using SensorID = SDL_SensorID;

enum class SensorType {
	INVALID		= SDL_SENSOR_INVALID,
	UNKNOWN		= SDL_SENSOR_UNKNOWN,
	ACCEL		= SDL_SENSOR_ACCEL,
	GYRO		= SDL_SENSOR_GYRO,
	ACCEL_L		= SDL_SENSOR_ACCEL_L,
	GYRO_L		= SDL_SENSOR_GYRO_L,
	ACCEL_R		= SDL_SENSOR_ACCEL_R,
	GYRO_R		= SDL_SENSOR_GYRO_R,
};

namespace detail {
	constexpr SDL_SensorType ToSDL(SensorType e) {
		return static_cast<SDL_SensorType>(e);
	}
}

// ============================================================================
// Énumération des capteurs connectés
// ============================================================================

namespace sensor {

[[nodiscard]] inline std::vector<SensorID> Enumerated() {
	int count = 0;
	SensorID *ids = SDL_GetSensors(&count);
	if (!ids)
		return {};
	std::vector<SensorID> v(ids, ids + count);
	SDL_free(ids);
	return v;
}

[[nodiscard]] inline const char *NameFor(SensorID id) noexcept { return SDL_GetSensorNameForID(id); }
[[nodiscard]] inline SensorType TypeFor(SensorID id) noexcept { return SensorType(SDL_GetSensorTypeForID(id)); }
[[nodiscard]] inline int NonPortableTypeFor(SensorID id) noexcept { return SDL_GetSensorNonPortableTypeForID(id); }

/// Force la mise à jour de tous les capteurs ouverts (appelé automatiquement
/// par la pompe d'événements si SDL_INIT_SENSOR est actif).
inline void Update() noexcept { SDL_UpdateSensors(); }

} // namespace sensor

namespace detail {

// Requêtes communes à Sensor (possédant) et SensorView (emprunté) — évite la
// duplication entre les deux classes tout en gardant SDL_Sensor* privé aux .cpp/.hpp internes.
[[nodiscard]] inline const char *GetSensorName(SDL_Sensor *h) noexcept { return h ? SDL_GetSensorName(h) : ""; }
[[nodiscard]] inline SensorType GetSensorType(SDL_Sensor *h) noexcept {
	return h ? SensorType(SDL_GetSensorType(h)) : SensorType::INVALID;
}
[[nodiscard]] inline int SensorNonPortableType(SDL_Sensor *h) noexcept {
	return h ? SDL_GetSensorNonPortableType(h) : -1;
}
[[nodiscard]] inline SensorID SensorId(SDL_Sensor *h) noexcept { return h ? SDL_GetSensorID(h) : 0; }
[[nodiscard]] inline SDL_PropertiesID SensorProperties(SDL_Sensor *h) noexcept {
	return h ? SDL_GetSensorProperties(h) : 0;
}

template <size_t N> [[nodiscard]] Option<std::array<float, N>> SensorData(SDL_Sensor *h) {
	std::array<float, N> out{};
	if (!h || !SDL_GetSensorData(h, out.data(), int(N)))
		return NONE;
	return Some(out);
}

} // namespace detail

// ============================================================================
// SensorView — vue non-possédante sur un capteur déjà ouvert (ex: retrouvé à
// partir d'un SDL_EVENT_SENSOR_UPDATE). Ne ferme jamais le capteur sous-jacent.
// ============================================================================

class SensorView : public Borrowed<SDL_Sensor> {
public:
	using Borrowed::Borrowed;

	[[nodiscard]] SDL_PropertiesID Properties() const noexcept { return detail::SensorProperties(m_handle); }
	[[nodiscard]] const char *Name() const noexcept { return detail::GetSensorName(m_handle); }
	[[nodiscard]] SensorType Type() const noexcept { return detail::GetSensorType(m_handle); }
	[[nodiscard]] int NonPortableType() const noexcept { return detail::SensorNonPortableType(m_handle); }
	[[nodiscard]] SensorID GetId() const noexcept { return detail::SensorId(m_handle); }

	template <size_t N = 3> [[nodiscard]] Option<std::array<float, N>> GetData() const {
		return detail::SensorData<N>(m_handle);
	}
};

// ============================================================================
// Sensor — RAII SDL_Sensor
// ============================================================================

class Sensor : public Wrapper<SDL_Sensor, SDL_CloseSensor> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Sensor, StringView> Open(SensorID id) {
		auto *s = SDL_OpenSensor(id);
		if (!s)
			return Err(GetError());
		return Ok(Sensor(s));
	}

	/// Retrouve la vue d'un capteur déjà ouvert ailleurs, à partir de son id
	/// (utile en réponse à un SDL_EVENT_SENSOR_UPDATE).
	[[nodiscard]] static Option<SensorView> FromId(SensorID id) noexcept {
		auto *s = SDL_GetSensorFromID(id);
		if (!s)
			return NONE;
		return Some(SensorView(s));
	}

	[[nodiscard]] SDL_PropertiesID Properties() const noexcept { return detail::SensorProperties(m_handle); }
	[[nodiscard]] const char *Name() const noexcept { return detail::GetSensorName(m_handle); }
	[[nodiscard]] SensorType Type() const noexcept { return detail::GetSensorType(m_handle); }
	[[nodiscard]] int NonPortableType() const noexcept { return detail::SensorNonPortableType(m_handle); }
	[[nodiscard]] SensorID GetId() const noexcept { return detail::SensorId(m_handle); }

	/// Lit jusqu'à `N` valeurs float dans le capteur (le nombre de canaux
	/// dépend du type : 3 pour Accel/Gyro).
	template <size_t N = 3> [[nodiscard]] Option<std::array<float, N>> GetData() const {
		return detail::SensorData<N>(m_handle);
	}
};

} // namespace sdl3
