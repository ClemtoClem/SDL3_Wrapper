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
[[nodiscard]] std::vector<Locale> Preferred();

} // namespace locale

} // namespace sdl3
