// Définitions de diagnostics/profiler.hpp
#include <diagnostics/profiler.hpp>

namespace diagnostics {

Profiler &Profiler::Global() {
	static Profiler profiler;
	return profiler;
}

void Profiler::Record(std::string_view name, std::chrono::nanoseconds duration) {
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_timings.find(name);
	if (it == m_timings.end())
		it = m_timings.emplace(std::string(name), TimingStats{}).first;
	TimingStats &s = it->second;
	++s.count;
	s.total += duration;
	if (duration < s.min)
		s.min = duration;
	if (duration > s.max)
		s.max = duration;
}

TimingStats Profiler::Timing(std::string_view name) const {
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_timings.find(name);
	return it == m_timings.end() ? TimingStats{} : it->second;
}

std::vector<std::pair<std::string, TimingStats>> Profiler::Timings() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return {m_timings.begin(), m_timings.end()};
}

void Profiler::Increment(std::string_view counter, int64_t delta) {
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_counters.find(counter);
	if (it == m_counters.end())
		it = m_counters.emplace(std::string(counter), 0).first;
	it->second += delta;
}

int64_t Profiler::Counter(std::string_view counter) const {
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_counters.find(counter);
	return it == m_counters.end() ? 0 : it->second;
}

std::vector<std::pair<std::string, int64_t>> Profiler::Counters() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return {m_counters.begin(), m_counters.end()};
}

void Profiler::Reset() {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_timings.clear();
	m_counters.clear();
}

} // namespace diagnostics
