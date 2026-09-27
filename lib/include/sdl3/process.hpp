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
    [[nodiscard]] static Result<Process, Error> Create(const std::vector<String> &args, bool pipeStdio = false);

    // Contrôle fin (répertoire de travail, redirections individuelles, env...)
    // via un SDL_PropertiesID préparé par l'appelant (SDL_PROP_PROCESS_CREATE_*).
    [[nodiscard]] static Result<Process, StringView> CreateWithProperties(SDL_PropertiesID props);

    [[nodiscard]] SDL_PropertiesID Properties() const noexcept;

    // Flux prêtés (valides tant que le Process existe) — non nul seulement si
    // le process a été créé avec pipeStdio=true ou une redirection App.
    [[nodiscard]] Option<IOStreamView> StdinStream() const noexcept;
    [[nodiscard]] Option<IOStreamView> StdoutStream() const noexcept;

    // Lit la sortie complète du process (bloquant jusqu'à la fin de l'exécution).
    // Nécessite que le process ait été créé avec pipeStdio=true.
    [[nodiscard]] Result<ProcessOutput, StringView> Read();

    [[nodiscard]] bool Kill(bool force) noexcept { return m_handle && SDL_KillProcess(m_handle, force); }

    // block=true : attend la fin du process (retourne le code de sortie).
    // block=false : sonde sans bloquer (NONE si le process tourne toujours).
    [[nodiscard]] Option<int> Wait(bool block) noexcept;
};

} // namespace sdl3
