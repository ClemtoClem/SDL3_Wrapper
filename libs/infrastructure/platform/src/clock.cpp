// Définitions de platform/clock.hpp
#include <platform/clock.hpp>

#include <cmath>

namespace platform {

int FixedStepClock::Advance(double frameSeconds) noexcept {
	if (frameSeconds < 0.0)
		frameSeconds = 0.0;
	if (frameSeconds > m_config.maxFrameTime) {
		m_dropped += frameSeconds - m_config.maxFrameTime;
		frameSeconds = m_config.maxFrameTime;
	}
	m_accumulator += frameSeconds;
	int steps = 0;
	while (m_accumulator >= m_config.stepSeconds && steps < m_config.maxStepsPerFrame) {
		m_accumulator -= m_config.stepSeconds;
		++steps;
	}
	// Retard au-delà de maxStepsPerFrame : abandonné plutôt qu'accumulé.
	if (m_accumulator >= m_config.stepSeconds) {
		const double remainder = std::fmod(m_accumulator, m_config.stepSeconds);
		m_dropped += m_accumulator - remainder;
		m_accumulator = remainder;
	}
	m_steps += uint64_t(steps);
	return steps;
}

void FixedStepClock::Reset() noexcept {
	m_accumulator = 0.0;
	m_dropped = 0.0;
	m_steps = 0;
}

} // namespace platform
