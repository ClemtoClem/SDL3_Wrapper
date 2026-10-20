#pragma once
/**
 * diagnostics::Logger — journal à niveaux et catégories, distribué à des
 * sinks interchangeables (console, mémoire, fichier, ou tout std::function).
 *
 * @code{.cpp}
 * auto &log = diagnostics::Logger::Global();
 * log.AddSink(diagnostics::ConsoleSink());
 * DIAG_INFO("render", "{} objets dessinés en {:.2f} ms", count, ms);
 * @endcode
 *
 * Indépendant de SDL : une couche supérieure peut y raccorder SDL_Log (cf.
 * platform::RouteSdlLog), ou l'inverse, sans que ce module n'en sache rien.
 * Toutes les méthodes sont sûres depuis plusieurs fils ; les sinks sont
 * appelés sous un verrou, l'un après l'autre (un sink ne doit donc pas
 * rappeler le Logger).
 */
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace diagnostics {

enum class Level : uint8_t { Trace, Debug, Info, Warn, Error, Critical, Off };

[[nodiscard]] std::string_view LevelName(Level level) noexcept;

/// Un message tel que délivré aux sinks. Les vues ne sont valides que
/// pendant l'appel du sink : un sink qui conserve le message le copie.
struct Record {
	Level level = Level::Info;
	std::string_view category;
	std::string_view message;
	std::chrono::system_clock::time_point time;
	std::thread::id thread;
	const char *file = nullptr;
	int line = 0;
};

using Sink = std::function<void(const Record &)>;

/// Mise en forme standard : "2026-10-17 13:45:12.345 [Info ] render: message".
[[nodiscard]] std::string FormatRecord(const Record &record);

class Logger {
public:
	Logger() = default;
	Logger(const Logger &) = delete;
	Logger &operator=(const Logger &) = delete;

	/// Journal du processus (les macros DIAG_* l'utilisent).
	[[nodiscard]] static Logger &Global();

	/// Ajoute un sink ; l'identifiant rendu sert à RemoveSink().
	size_t AddSink(Sink sink);
	void RemoveSink(size_t id);
	void ClearSinks();

	/// Niveau minimal global (défaut : Info).
	void SetLevel(Level level) noexcept;
	[[nodiscard]] Level GetLevel() const noexcept;
	/// Niveau minimal propre à une catégorie (prioritaire sur le niveau global).
	void SetCategoryLevel(std::string_view category, Level level);
	void ClearCategoryLevels();

	[[nodiscard]] bool IsEnabled(Level level, std::string_view category) const;

	/// Délivre le message aux sinks s'il passe le filtre de niveau.
	void Write(Level level, std::string_view category, std::string_view message, const char *file = nullptr,
	           int line = 0);

	template <class... Args>
	void Log(Level level, std::string_view category, std::format_string<Args...> fmt, Args &&...args) {
		if (IsEnabled(level, category))
			Write(level, category, std::format(fmt, std::forward<Args>(args)...));
	}

private:
	mutable std::mutex m_mutex;
	std::vector<std::pair<size_t, Sink>> m_sinks;
	size_t m_nextId = 1;
	Level m_level = Level::Info;
	std::unordered_map<std::string, Level> m_categoryLevels;
};

// ── Sinks fournis ───────────────────────────────────────────────────────────

/// Écrit FormatRecord() sur stderr.
[[nodiscard]] Sink ConsoleSink();

/// Écrit FormatRecord() dans un fichier (ajout) ; vidé à chaque message de
/// niveau Error ou plus. Sink vide si le fichier ne peut pas être ouvert.
[[nodiscard]] Sink FileSink(const std::string &path);

/// Conserve les `capacity` derniers messages (copiés) : console intégrée
/// d'une application, ou vérification des messages dans un test.
class MemorySink {
public:
	struct Entry {
		Level level;
		std::string category;
		std::string message;
	};

	explicit MemorySink(size_t capacity = 1024) : m_state(std::make_shared<State>(capacity)) {}

	/// Le sink à passer à Logger::AddSink (partage l'état de ce MemorySink).
	[[nodiscard]] Sink AsSink() const;
	[[nodiscard]] std::vector<Entry> Entries() const;
	[[nodiscard]] size_t Count(Level atLeast = Level::Trace) const;
	void Clear();

private:
	struct State {
		explicit State(size_t c) : capacity(c) {}
		mutable std::mutex mutex;
		size_t capacity;
		std::deque<Entry> entries;
	};
	std::shared_ptr<State> m_state;
};

} // namespace diagnostics

// ── Macros (journal global, catégorie explicite) ────────────────────────────

#define DIAG_LOG(level, category, ...)                                                                                 \
	do {                                                                                                               \
		auto &diag_logger_ = ::diagnostics::Logger::Global();                                                         \
		if (diag_logger_.IsEnabled(level, category))                                                                   \
			diag_logger_.Write(level, category, std::format(__VA_ARGS__), __FILE__, __LINE__);                         \
	} while (0)

#define DIAG_TRACE(category, ...) DIAG_LOG(::diagnostics::Level::Trace, category, __VA_ARGS__)
#define DIAG_DEBUG(category, ...) DIAG_LOG(::diagnostics::Level::Debug, category, __VA_ARGS__)
#define DIAG_INFO(category, ...) DIAG_LOG(::diagnostics::Level::Info, category, __VA_ARGS__)
#define DIAG_WARN(category, ...) DIAG_LOG(::diagnostics::Level::Warn, category, __VA_ARGS__)
#define DIAG_ERROR(category, ...) DIAG_LOG(::diagnostics::Level::Error, category, __VA_ARGS__)
#define DIAG_CRITICAL(category, ...) DIAG_LOG(::diagnostics::Level::Critical, category, __VA_ARGS__)
