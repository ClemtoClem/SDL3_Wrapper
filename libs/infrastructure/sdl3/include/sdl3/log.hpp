#pragma once
/**
 * Wrapper C++ de SDL3/SDL_log.h — namespace les constantes de
 * SDL_LogPriority/SDL_LogCategory (cf. PixelFormat::), et ajoute deux
 * briques absentes de la SDL brute :
 *
 *  - LogRouter : un unique SDL_LogOutputFunction process-wide qui fan-out
 *    vers N sink C++ (std::function), pour que plusieurs consommateurs
 *    (console UI, export fichier...) puissent coexister sans se marcher
 *    dessus en réinstallant chacun leur propre callback SDL.
 *  - FileLogSink : écrit les messages dans un fichier depuis un thread dédié
 *    (std::jthread + file d'attente), pour que l'appelant (SDL_Log* — donc
 *    potentiellement le thread audio/rendu/réseau) ne bloque jamais sur de
 *    l'I/O disque.
 *
 * Inspiré de core::Logger/FileSink (SDL3_ui/src/Logger), adapté pour
 * s'appuyer sur le système de logging de SDL3 lui-même (catégories/priorités/
 * SDL_Log*) plutôt que de le dupliquer.
 */
#include <SDL3/SDL.h>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <core/core.hpp>
#include "error.hpp"

namespace sdl3 {

enum class LogPriority: int {
    Invalid = SDL_LOG_PRIORITY_INVALID,
    Trace   = SDL_LOG_PRIORITY_TRACE,
    Verbose = SDL_LOG_PRIORITY_VERBOSE,
    Debug   = SDL_LOG_PRIORITY_DEBUG,
    Info    = SDL_LOG_PRIORITY_INFO,
    Warn    = SDL_LOG_PRIORITY_WARN,
    Error   = SDL_LOG_PRIORITY_ERROR,
    Critical = SDL_LOG_PRIORITY_CRITICAL,
    Count   = SDL_LOG_PRIORITY_COUNT
};

enum class LogCategory: int {
    App     = SDL_LOG_CATEGORY_APPLICATION,
    Error   = SDL_LOG_CATEGORY_ERROR,
    Assert  = SDL_LOG_CATEGORY_ASSERT,
    System  = SDL_LOG_CATEGORY_SYSTEM,
    Audio   = SDL_LOG_CATEGORY_AUDIO,
    Video   = SDL_LOG_CATEGORY_VIDEO,
    Render  = SDL_LOG_CATEGORY_RENDER,
    Input   = SDL_LOG_CATEGORY_INPUT,
    Test    = SDL_LOG_CATEGORY_TEST,
    Gpu     = SDL_LOG_CATEGORY_GPU,
    Custom  = SDL_LOG_CATEGORY_CUSTOM
};

[[nodiscard]] StringView LogPriorityToString(LogPriority p) noexcept;

[[nodiscard]] String LogTimestamp();

// ── Priorités / catégories (fines enveloppes de SDL_Log*) ──────────────────

inline void SetLogPriorities(LogPriority p) noexcept { SDL_SetLogPriorities(static_cast<SDL_LogPriority>(p)); }
inline void SetLogPriority(LogCategory c, LogPriority p) noexcept { SDL_SetLogPriority(static_cast<int>(c), static_cast<SDL_LogPriority>(p)); }
[[nodiscard]] inline LogPriority GetLogPriority(LogCategory c) noexcept { return static_cast<LogPriority>(SDL_GetLogPriority(static_cast<int>(c))); }
inline void ResetLogPriorities() noexcept { SDL_ResetLogPriorities(); }

/// Un message tel que délivré aux sinks de LogRouter. `text` n'est valide que
/// pour la durée de l'appel du sink (vue sur le buffer interne de SDL) — un
/// sink qui doit le conserver au-delà (ex: FileLogSink, qui le transmet à un
/// thread) doit le copier en String immédiatement.
struct LogMessage {
    LogCategory category;
    LogPriority priority;
    StringView text;
};

/// Un consommateur de messages de log, enregistré auprès de LogRouter.
using LogSink = std::function<void(const LogMessage &)>;

// ============================================================================
// LogRouter — un seul SDL_LogOutputFunction process-wide, fan-out vers N sinks
// ============================================================================

class LogRouter {
public:
    [[nodiscard]] static LogRouter &Instance();

    /// Enregistre un sink ; retourne un identifiant à repasser à removeSink().
    size_t AddSink(LogSink sink);

    void RemoveSink(size_t id);

    /// Le SDL_LogOutputFunction installé avant que LogRouter ne prenne la
    /// main (généralement le handler par défaut de SDL, qui écrit sur la
    /// console) — exposé pour les sinks qui veulent y chaîner explicitement
    /// (cf. Application::buildLogging, activé seulement si l'utilisateur
    /// active l'écho console dans les réglages : l'écriture console
    /// synchrone est justement ce qui ralentit l'exécution par défaut).
    [[nodiscard]] SDL_LogOutputFunction PreviousHandler() const noexcept { return prevFn; }
    [[nodiscard]] void *PreviousHandlerData() const noexcept { return prevData; }

private:
    LogRouter();
    // Volontairement jamais désinstallé : LogRouter vit pour la durée du
    // process (singleton), donc restaurer le handler précédent au dtor
    // n'aurait jamais l'occasion de s'exécuter avant la sortie du process.
    ~LogRouter() = default;

    LogRouter(const LogRouter &) = delete;
    LogRouter &operator=(const LogRouter &) = delete;

    static void SDLCALL Trampoline(void *userdata, int category, SDL_LogPriority priority, const char *message);

    std::mutex mutex;
    std::unordered_map<size_t, LogSink> sinks;
    size_t nextId = 1;
    SDL_LogOutputFunction prevFn = nullptr;
    void *prevData = nullptr;
};

// ============================================================================
// FileLogSink — écrit les messages dans un fichier depuis un thread dédié
// ============================================================================

/**
 * @brief Sink de log fichier asynchrone : write() copie/enfile le message et
 * revient immédiatement (jamais d'I/O sur le thread appelant) ; un unique
 * thread dédié dépile et écrit sur disque.
 *
 * Non copiable ni déplaçable : le thread de fond capture `this` par
 * référence. Détruire l'objet arrête le thread (std::jthread::request_stop),
 * réveille la file d'attente et joint — les messages déjà enfilés sont
 * garantis écrits avant le retour du destructeur.
 */
class FileLogSink {
public:
    [[nodiscard]] static Result<std::unique_ptr<FileLogSink>, Error> Create(const String &path);

    ~FileLogSink();

    FileLogSink(const FileLogSink &) = delete;
    FileLogSink &operator=(const FileLogSink &) = delete;
    FileLogSink(FileLogSink &&) = delete;
    FileLogSink &operator=(FileLogSink &&) = delete;

    /// Non-bloquant : copie le message formaté dans la file et revient.
    void Write(const LogMessage &msg);

    [[nodiscard]] const String &Path() const noexcept { return path; }

private:
    FileLogSink(String path, std::ofstream file)
        : path(std::move(path)), file(std::move(file)), thread([this](std::stop_token st) { ThreadLoop(st); }) {}

    void ThreadLoop(std::stop_token st);

    String path;
    std::ofstream file;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<String> queue;
    std::jthread thread; // déclaré en dernier : démarre après le reste, s'arrête/joint avant
};

} // namespace sdl3
