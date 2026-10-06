// Définitions de sdl3/locale.hpp
#include "sdl3/locale.hpp"

namespace sdl3 {

namespace locale {

std::vector<Locale> Preferred() {
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
