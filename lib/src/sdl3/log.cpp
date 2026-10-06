// Définitions de sdl3/log.hpp
#include "sdl3/log.hpp"

namespace sdl3 {

StringView LogPriorityToString(LogPriority p) noexcept {
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

String LogTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_r(&tt, &tmv);
    char buf[32];
    size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return String(buf, n);
}

// ── LogRouter ────────────────────────────────────────────────────────────────

LogRouter & LogRouter::Instance() {
    static LogRouter router;
    return router;
}

size_t LogRouter::AddSink(LogSink sink) {
    std::lock_guard<std::mutex> lock(mutex);
    size_t id = nextId++;
    sinks[id] = std::move(sink);
    return id;
}

void LogRouter::RemoveSink(size_t id) {
    std::lock_guard<std::mutex> lock(mutex);
    sinks.erase(id);
}

LogRouter::LogRouter() {
    SDL_GetLogOutputFunction(&prevFn, &prevData);
    SDL_SetLogOutputFunction(&LogRouter::Trampoline, this);
}

void SDLCALL LogRouter::Trampoline(void *userdata, int category, SDL_LogPriority priority, const char *message) {
    auto *self = static_cast<LogRouter *>(userdata);
    LogMessage msg{category, priority, StringView(message ? message : "")};
    std::lock_guard<std::mutex> lock(self->mutex);
    for (auto &[id, sink] : self->sinks)
        sink(msg);
}

// ── FileLogSink ──────────────────────────────────────────────────────────────

Result<std::unique_ptr<FileLogSink>, Error> FileLogSink::Create(const String &path) {
    std::ofstream file(path.c_str(), std::ios::app);
    if (!file.is_open()) {
        SetError("Could not open log file: %s", path.c_str());
        return Err(GetError());
    }
    auto sink = std::unique_ptr<FileLogSink>(new FileLogSink(path, std::move(file)));
    return Ok(std::move(sink));
}

FileLogSink::~FileLogSink() {
    thread.request_stop();
    cv.notify_all();
    // Le jthread membre joint automatiquement à sa destruction (après ce
    // destructeur, par ordre inverse de déclaration) — mais on le fait
    // explicitement ici pour garantir que file reste vivant tant que
    // le thread peut encore y écrire.
    if (thread.joinable())
        thread.join();
}

void FileLogSink::Write(const LogMessage &msg) {
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

void FileLogSink::ThreadLoop(std::stop_token st) {
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

} // namespace sdl3
