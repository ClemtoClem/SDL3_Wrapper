// Définitions de diagnostics/log.hpp
#include <diagnostics/log.hpp>

#include <cstdio>
#include <ctime>
#include <fstream>

namespace diagnostics {

std::string_view LevelName(Level level) noexcept {
	switch (level) {
	case Level::Trace: return "Trace";
	case Level::Debug: return "Debug";
	case Level::Info: return "Info";
	case Level::Warn: return "Warn";
	case Level::Error: return "Error";
	case Level::Critical: return "Critical";
	case Level::Off: return "Off";
	}
	return "?";
}

std::string FormatRecord(const Record &record) {
	using namespace std::chrono;
	const std::time_t tt = system_clock::to_time_t(record.time);
	std::tm tmv{};
	localtime_r(&tt, &tmv);
	char stamp[32];
	std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tmv);
	const auto ms = duration_cast<milliseconds>(record.time.time_since_epoch()).count() % 1000;
	return std::format("{}.{:03} [{:<8}] {}: {}", stamp, ms, LevelName(record.level), record.category,
	                   record.message);
}

// ── Logger ───────────────────────────────────────────────────────────────────

Logger &Logger::Global() {
	static Logger logger;
	return logger;
}

size_t Logger::AddSink(Sink sink) {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!sink)
		return 0;
	const size_t id = m_nextId++;
	m_sinks.emplace_back(id, std::move(sink));
	return id;
}

void Logger::RemoveSink(size_t id) {
	std::lock_guard<std::mutex> lock(m_mutex);
	std::erase_if(m_sinks, [id](const auto &s) { return s.first == id; });
}

void Logger::ClearSinks() {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_sinks.clear();
}

void Logger::SetLevel(Level level) noexcept {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_level = level;
}

Level Logger::GetLevel() const noexcept {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_level;
}

void Logger::SetCategoryLevel(std::string_view category, Level level) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_categoryLevels[std::string(category)] = level;
}

void Logger::ClearCategoryLevels() {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_categoryLevels.clear();
}

bool Logger::IsEnabled(Level level, std::string_view category) const {
	if (level == Level::Off)
		return false;
	std::lock_guard<std::mutex> lock(m_mutex);
	Level threshold = m_level;
	if (!m_categoryLevels.empty())
		if (auto it = m_categoryLevels.find(std::string(category)); it != m_categoryLevels.end())
			threshold = it->second;
	return level >= threshold;
}

void Logger::Write(Level level, std::string_view category, std::string_view message, const char *file, int line) {
	if (!IsEnabled(level, category))
		return;
	Record record{level, category, message, std::chrono::system_clock::now(), std::this_thread::get_id(), file, line};
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto &[id, sink] : m_sinks)
		sink(record);
}

// ── Sinks ────────────────────────────────────────────────────────────────────

Sink ConsoleSink() {
	return [](const Record &r) { std::fprintf(stderr, "%s\n", FormatRecord(r).c_str()); };
}

Sink FileSink(const std::string &path) {
	auto file = std::make_shared<std::ofstream>(path, std::ios::app);
	if (!file->is_open())
		return {};
	return [file](const Record &r) {
		*file << FormatRecord(r) << '\n';
		if (r.level >= Level::Error)
			file->flush();
	};
}

Sink MemorySink::AsSink() const {
	return [state = m_state](const Record &r) {
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->capacity == 0)
			return;
		if (state->entries.size() == state->capacity)
			state->entries.pop_front();
		state->entries.push_back({r.level, std::string(r.category), std::string(r.message)});
	};
}

std::vector<MemorySink::Entry> MemorySink::Entries() const {
	std::lock_guard<std::mutex> lock(m_state->mutex);
	return {m_state->entries.begin(), m_state->entries.end()};
}

size_t MemorySink::Count(Level atLeast) const {
	std::lock_guard<std::mutex> lock(m_state->mutex);
	size_t n = 0;
	for (const Entry &e : m_state->entries)
		n += e.level >= atLeast ? 1 : 0;
	return n;
}

void MemorySink::Clear() {
	std::lock_guard<std::mutex> lock(m_state->mutex);
	m_state->entries.clear();
}

} // namespace diagnostics
