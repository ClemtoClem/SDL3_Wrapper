#pragma once
#include <SDL3/SDL.h>
#include <vector>

#include "../core/core.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// Locale — wrap de SDL_Locale
// ============================================================================

struct Locale {
    String language;               // ex: "en"
    Option<String> country = NONE; // ex: "US" — NONE si non précisé par l'OS

    Locale() = default;
    explicit Locale(const SDL_Locale &l)
        : language(l.language ? l.language : ""), country(l.country ? Option<String>(Some(String(l.country))) : NONE) {}
};

namespace locale {

// Liste des locales préférées de l'utilisateur, par ordre de préférence.
[[nodiscard]] inline std::vector<Locale> Preferred() {
    int count = 0;
    SDL_Locale **locales = SDL_GetPreferredLocales(&count);
    if (!locales)
        return {};
    std::vector<Locale> out;
    out.reserve(size_t(count));
    for (int i = 0; i < count; ++i)
        out.emplace_back(*locales[i]);
    SDL_free(locales);
    return out;
}

} // namespace locale

} // namespace sdl3
