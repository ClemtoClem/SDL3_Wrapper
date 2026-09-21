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

#include "../core/core.hpp"
#include "error.hpp"

namespace sdl3 {

using LogPriority = SDL_LogPriority;
using LogCategory = int; // SDL_LogCategory n'est pas scellé — catégories perso possibles

namespace log_priority {
constexpr LogPriority INVALID = SDL_LOG_PRIORITY_INVALID;
constexpr LogPriority TRACE = SDL_LOG_PRIORITY_TRACE;
constexpr LogPriority VERBOSE = SDL_LOG_PRIORITY_VERBOSE;
constexpr LogPriority DEBUG = SDL_LOG_PRIORITY_DEBUG;
constexpr LogPriority INFO = SDL_LOG_PRIORITY_INFO;
constexpr LogPriority WARN = SDL_LOG_PRIORITY_WARN;
constexpr LogPriority ERROR = SDL_LOG_PRIORITY_ERROR;
constexpr LogPriority CRITICAL = SDL_LOG_PRIORITY_CRITICAL;
constexpr LogPriority COUNT = SDL_LOG_PRIORITY_COUNT;
} // namespace log_priority

namespace log_category {
constexpr LogCategory APPLICATION = SDL_LOG_CATEGORY_APPLICATION;
constexpr LogCategory ERROR = SDL_LOG_CATEGORY_ERROR;
constexpr LogCategory ASSERT = SDL_LOG_CATEGORY_ASSERT;
constexpr LogCategory SYSTEM = SDL_LOG_CATEGORY_SYSTEM;
constexpr LogCategory AUDIO = SDL_LOG_CATEGORY_AUDIO;
constexpr LogCategory VIDEO = SDL_LOG_CATEGORY_VIDEO;
constexpr LogCategory RENDER = SDL_LOG_CATEGORY_RENDER;
constexpr LogCategory INPUT = SDL_LOG_CATEGORY_INPUT;
constexpr LogCategory TEST = SDL_LOG_CATEGORY_TEST;
constexpr LogCategory GPU = SDL_LOG_CATEGORY_GPU;
constexpr LogCategory CUSTOM = SDL_LOG_CATEGORY_CUSTOM;
} // namespace log_category

[[nodiscard]] inline StringView LogPriorityToString(LogPriority p) noexcept {
    switch (p) {
    case SDL_LOG_PRIORITY_TRACE:
        return "TRACE";
    case SDL_LOG_PRIORITY_VERBOSE:
        return "VERBOSE";
    case SDL_LOG_PRIORITY_DEBUG:
        return "DEBUG";
    case SDL_LOG_PRIORITY_INFO:
        return "INFO";
    case SDL_LOG_PRIORITY_WARN:
        return "WARN";
    case SDL_LOG_PRIORITY_ERROR:
        return "ERROR";
    case SDL_LOG_PRIORITY_CRITICAL:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}

[[nodiscard]] inline String LogTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_r(&tt, &tmv);
    char buf[32];
    size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return String(buf, n);
}

// ── Priorités / catégories (fines enveloppes de SDL_Log*) ──────────────────

inline void SetLogPriorities(LogPriority p) noexcept { SDL_SetLogPriorities(p); }
inline void SetLogPriority(LogCategory c, LogPriority p) noexcept { SDL_SetLogPriority(c, p); }
[[nodiscard]] inline LogPriority GetLogPriority(LogCategory c) noexcept { return SDL_GetLogPriority(c); }
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
    [[nodiscard]] static LogRouter &Instance() {
        static LogRouter router;
        return router;
    }

    /// Enregistre un sink ; retourne un identifiant à repasser à removeSink().
    size_t AddSink(LogSink sink) {
        std::lock_guard<std::mutex> lock(mutex);
        size_t id = nextId++;
        sinks[id] = std::move(sink);
        return id;
    }

    void RemoveSink(size_t id) {
        std::lock_guard<std::mutex> lock(mutex);
        sinks.erase(id);
    }

    /// Le SDL_LogOutputFunction installé avant que LogRouter ne prenne la
    /// main (généralement le handler par défaut de SDL, qui écrit sur la
    /// console) — exposé pour les sinks qui veulent y chaîner explicitement
    /// (cf. Application::buildLogging, activé seulement si l'utilisateur
    /// active l'écho console dans les réglages : l'écriture console
    /// synchrone est justement ce qui ralentit l'exécution par défaut).
    [[nodiscard]] SDL_LogOutputFunction PreviousHandler() const noexcept { return prevFn; }
    [[nodiscard]] void *PreviousHandlerData() const noexcept { return prevData; }

private:
    LogRouter() {
        SDL_GetLogOutputFunction(&prevFn, &prevData);
        SDL_SetLogOutputFunction(&LogRouter::Trampoline, this);
    }
    // Volontairement jamais désinstallé : LogRouter vit pour la durée du
    // process (singleton), donc restaurer le handler précédent au dtor
    // n'aurait jamais l'occasion de s'exécuter avant la sortie du process.
    ~LogRouter() = default;

    LogRouter(const LogRouter &) = delete;
    LogRouter &operator=(const LogRouter &) = delete;

    static void SDLCALL Trampoline(void *userdata, int category, SDL_LogPriority priority, const char *message) {
        auto *self = static_cast<LogRouter *>(userdata);
        LogMessage msg{category, priority, StringView(message ? message : "")};
        std::lock_guard<std::mutex> lock(self->mutex);
        for (auto &[id, sink] : self->sinks)
            sink(msg);
    }

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
    [[nodiscard]] static Result<std::unique_ptr<FileLogSink>, Error> Create(const String &path) {
        std::ofstream file(path.c_str(), std::ios::app);
        if (!file.is_open()) {
            SetError("Could not open log file: %s", path.c_str());
            return Err(GetError());
        }
        auto sink = std::unique_ptr<FileLogSink>(new FileLogSink(path, std::move(file)));
        return Ok(std::move(sink));
    }

    ~FileLogSink() {
        thread.request_stop();
        cv.notify_all();
        // Le jthread membre joint automatiquement à sa destruction (après ce
        // destructeur, par ordre inverse de déclaration) — mais on le fait
        // explicitement ici pour garantir que file reste vivant tant que
        // le thread peut encore y écrire.
        if (thread.joinable())
            thread.join();
    }

    FileLogSink(const FileLogSink &) = delete;
    FileLogSink &operator=(const FileLogSink &) = delete;
    FileLogSink(FileLogSink &&) = delete;
    FileLogSink &operator=(FileLogSink &&) = delete;

    /// Non-bloquant : copie le message formaté dans la file et revient.
    void Write(const LogMessage &msg) {
        String line;
        line.Reserve(msg.text.GetSize() + 64);
        line += LogTimestamp();
        line += " [";
        line += LogPriorityToString(msg.priority);
        line += "] ";
        line += msg.text;
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push_back(std::move(line));
        }
        cv.notify_one();
    }

    [[nodiscard]] const String &Path() const noexcept { return path; }

private:
    FileLogSink(String path, std::ofstream file)
        : path(std::move(path)), file(std::move(file)), thread([this](std::stop_token st) { ThreadLoop(st); }) {}

    void ThreadLoop(std::stop_token st) {
        while (true) {
            std::deque<String> batch;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] { return st.stop_requested() || !queue.empty(); });
                batch.swap(queue);
            }
            for (const String &line : batch) {
                file << line.c_str() << '\n';
            }
            if (!batch.empty())
                file.flush();
            if (st.stop_requested() && batch.empty())
                break;
        }
    }

    String path;
    std::ofstream file;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<String> queue;
    std::jthread thread; // déclaré en dernier : démarre après le reste, s'arrête/joint avant
};

} // namespace sdl3
