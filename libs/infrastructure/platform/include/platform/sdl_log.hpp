#pragma once
/**
 * Pont SDL_Log → diagnostics::Logger : les messages émis par SDL (et par le
 * code qui utilise sdl3::Log*) arrivent dans le journal du processus, avec
 * la catégorie "sdl.<catégorie SDL>" et le niveau correspondant.
 *
 * C'est la couche plate-forme qui fait le lien : diagnostics:: (Foundation)
 * ignore tout de SDL, et sdl3:: ignore tout de diagnostics::.
 */
#include <cstddef>

#include <diagnostics/log.hpp>
#include <sdl3/log.hpp>

namespace platform {

[[nodiscard]] diagnostics::Level ToDiagnosticsLevel(sdl3::LogPriority priority) noexcept;

/// Installe le pont (idempotent) ; rend l'identifiant du sink côté sdl3::LogRouter.
size_t RouteSdlLog(diagnostics::Logger &logger = diagnostics::Logger::Global());

} // namespace platform
