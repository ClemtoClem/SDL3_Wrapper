#pragma once
#include <SDL3/SDL.h>
#include <vector>

#include "../core/core.hpp"
#include "iostream.hpp"

namespace sdl3 {

// ============================================================================
// ProcessIO
// ============================================================================

enum class ProcessIO: Uint8 {
    INHERITED   = SDL_PROCESS_STDIO_INHERITED,
    Null        = SDL_PROCESS_STDIO_NULL,
    APP         = SDL_PROCESS_STDIO_APP,
    REDIRECT    = SDL_PROCESS_STDIO_REDIRECT,
};

namespace detail {
    constexpr SDL_ProcessIO ToSDL(ProcessIO e) {
        return static_cast<SDL_ProcessIO>(e);
    }
}

// ============================================================================
// ProcessOutput — résultat de Process::read()
// ============================================================================

struct ProcessOutput {
    std::vector<uint8_t> data;
    int exitCode = 0;
};

// ============================================================================
// Process — RAII SDL_Process (lancement de sous-processus)
// ============================================================================

class Process : public Wrapper<SDL_Process, SDL_DestroyProcess> {
public:
    using Wrapper::Wrapper;

    // `args[0]` est le chemin de l'exécutable. Si `pipeStdio` est vrai, stdin/
    // stdout/stderr du sous-processus sont redirigés vers des SDL_IOStream
    // accessibles via stdinStream()/stdoutStream().
    [[nodiscard]] static Result<Process, Error> Create(const std::vector<String> &args, bool pipeStdio = false) {
        std::vector<const char *> argv;
        argv.reserve(args.size() + 1);
        for (auto &a : args)
            argv.push_back(a.c_str());
        argv.push_back(nullptr);

        auto *p = SDL_CreateProcess(argv.data(), pipeStdio);
        if (!p)
            return Err(GetError());
        return Ok(Process(p));
    }

    // Contrôle fin (répertoire de travail, redirections individuelles, env...)
    // via un SDL_PropertiesID préparé par l'appelant (SDL_PROP_PROCESS_CREATE_*).
    [[nodiscard]] static Result<Process, StringView> CreateWithProperties(SDL_PropertiesID props) {
        auto *p = SDL_CreateProcessWithProperties(props);
        if (!p)
            return Err(GetError());
        return Ok(Process(p));
    }

    [[nodiscard]] SDL_PropertiesID Properties() const noexcept {
        return m_handle ? SDL_GetProcessProperties(m_handle) : 0;
    }

    // Flux prêtés (valides tant que le Process existe) — non nul seulement si
    // le process a été créé avec pipeStdio=true ou une redirection App.
    [[nodiscard]] Option<IOStreamView> StdinStream() const noexcept {
        auto *io = m_handle ? SDL_GetProcessInput(m_handle) : nullptr;
        if (!io)
            return NONE;
        return Some(IOStreamView(io));
    }
    [[nodiscard]] Option<IOStreamView> StdoutStream() const noexcept {
        auto *io = m_handle ? SDL_GetProcessOutput(m_handle) : nullptr;
        if (!io)
            return NONE;
        return Some(IOStreamView(io));
    }

    // Lit la sortie complète du process (bloquant jusqu'à la fin de l'exécution).
    // Nécessite que le process ait été créé avec pipeStdio=true.
    [[nodiscard]] Result<ProcessOutput, StringView> Read() {
        if (!m_handle)
            return Err(StringView("process invalide"));
        size_t size = 0;
        int exitCode = 0;
        void *data = SDL_ReadProcess(m_handle, &size, &exitCode);
        if (!data)
            return Err(GetError());

        ProcessOutput out;
        out.data.assign(static_cast<uint8_t *>(data), static_cast<uint8_t *>(data) + size);
        out.exitCode = exitCode;
        SDL_free(data);
        return Ok(std::move(out));
    }

    [[nodiscard]] bool Kill(bool force) noexcept { return m_handle && SDL_KillProcess(m_handle, force); }

    // block=true : attend la fin du process (retourne le code de sortie).
    // block=false : sonde sans bloquer (NONE si le process tourne toujours).
    [[nodiscard]] Option<int> Wait(bool block) noexcept {
        if (!m_handle)
            return NONE;
        int exitCode = 0;
        if (!SDL_WaitProcess(m_handle, block, &exitCode))
            return NONE;
        return Some(exitCode);
    }
};

} // namespace sdl3
