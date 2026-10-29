#pragma once
/**
 * platform::FixedStepClock — boucle de simulation à pas fixe découplée de la
 * cadence d'affichage (« Fix Your Timestep »).
 *
 * Chaque image, Advance(dt réel) rend le nombre de pas fixes à simuler ;
 * Alpha() donne la fraction de pas restante pour interpoler le rendu entre
 * l'avant-dernier et le dernier état simulé. Un dt aberrant (débogueur, fenêtre
 * déplacée) est borné par maxFrameTime, et le nombre de pas par image par
 * maxStepsPerFrame, pour éviter la « spirale de la mort ».
 *
 * Logique pure, sans SDL : entièrement testable avec des durées synthétiques.
 */
#include <cstdint>

namespace platform {

class FixedStepClock {
public:
	struct Config {
		double stepSeconds = 1.0 / 60.0;
		double maxFrameTime = 0.25;
		int maxStepsPerFrame = 8;
	};

	FixedStepClock() = default;
	explicit FixedStepClock(Config config) noexcept : m_config(config) {}

	/// Ajoute `frameSeconds` de temps réel ; rend le nombre de pas fixes à exécuter.
	int Advance(double frameSeconds) noexcept;

	/// Fraction [0, 1) du pas suivant déjà écoulée (interpolation du rendu).
	[[nodiscard]] double Alpha() const noexcept { return m_accumulator / m_config.stepSeconds; }
	[[nodiscard]] double StepSeconds() const noexcept { return m_config.stepSeconds; }
	/// Temps simulé total (pas exécutés × durée d'un pas).
	[[nodiscard]] double SimulatedSeconds() const noexcept { return double(m_steps) * m_config.stepSeconds; }
	[[nodiscard]] uint64_t TotalSteps() const noexcept { return m_steps; }
	/// Temps réel ignoré parce qu'au-delà des bornes (retard non rattrapable).
	[[nodiscard]] double DroppedSeconds() const noexcept { return m_dropped; }

	void Reset() noexcept;

private:
	Config m_config;
	double m_accumulator = 0.0;
	double m_dropped = 0.0;
	uint64_t m_steps = 0;
};

} // namespace platform
