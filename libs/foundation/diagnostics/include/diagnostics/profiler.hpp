#pragma once
/**
 * Instrumentation de diagnostics:: : durées mesurées par nom et compteurs.
 *
 * @code{.cpp}
 * void Physics::Step() {
 *     DIAG_PROFILE_SCOPE("physics.step");   // chronomètre la portée
 *     ...
 * }
 * diagnostics::Profiler::Global().Increment("draw_calls", n);
 * for (auto &[name, stats] : diagnostics::Profiler::Global().Timings())
 *     DIAG_INFO("perf", "{} : {:.3f} ms en moyenne", name, stats.MeanMs());
 * @endcode
 *
 * Sûr depuis plusieurs fils. Horloge : std::chrono::steady_clock.
 */
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace diagnostics {

struct TimingStats {
	uint64_t count = 0;
	std::chrono::nanoseconds total{0};
	std::chrono::nanoseconds min{std::numeric_limits<std::chrono::nanoseconds::rep>::max()};
	std::chrono::nanoseconds max{0};

	[[nodiscard]] double TotalMs() const noexcept { return total.count() / 1e6; }
	[[nodiscard]] double MeanMs() const noexcept { return count ? TotalMs() / double(count) : 0.0; }
	[[nodiscard]] double MinMs() const noexcept { return count ? min.count() / 1e6 : 0.0; }
	[[nodiscard]] double MaxMs() const noexcept { return max.count() / 1e6; }
};

class Profiler {
public:
	Profiler() = default;
	Profiler(const Profiler &) = delete;
	Profiler &operator=(const Profiler &) = delete;

	[[nodiscard]] static Profiler &Global();

	void Record(std::string_view name, std::chrono::nanoseconds duration);
	[[nodiscard]] TimingStats Timing(std::string_view name) const;
	/// Toutes les mesures, triées par nom.
	[[nodiscard]] std::vector<std::pair<std::string, TimingStats>> Timings() const;

	void Increment(std::string_view counter, int64_t delta = 1);
	[[nodiscard]] int64_t Counter(std::string_view counter) const;
	[[nodiscard]] std::vector<std::pair<std::string, int64_t>> Counters() const;

	void Reset();

private:
	mutable std::mutex m_mutex;
	std::map<std::string, TimingStats, std::less<>> m_timings;
	std::map<std::string, int64_t, std::less<>> m_counters;
};

/// Mesure la durée de sa propre vie et l'enregistre dans un Profiler.
class ScopedTimer {
public:
	explicit ScopedTimer(std::string_view name, Profiler &profiler = Profiler::Global())
		: m_name(name), m_profiler(profiler), m_start(std::chrono::steady_clock::now()) {}
	~ScopedTimer() { m_profiler.Record(m_name, std::chrono::steady_clock::now() - m_start); }

	ScopedTimer(const ScopedTimer &) = delete;
	ScopedTimer &operator=(const ScopedTimer &) = delete;

	[[nodiscard]] std::chrono::nanoseconds Elapsed() const noexcept {
		return std::chrono::steady_clock::now() - m_start;
	}

private:
	std::string m_name;
	Profiler &m_profiler;
	std::chrono::steady_clock::time_point m_start;
};

} // namespace diagnostics

#define DIAG_CAT_(a, b) a##b
#define DIAG_CAT(a, b) DIAG_CAT_(a, b)
#define DIAG_PROFILE_SCOPE(name) ::diagnostics::ScopedTimer DIAG_CAT(diag_scope_timer_, __LINE__)(name)
