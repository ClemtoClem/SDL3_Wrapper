// Définitions de sdl3/stdinc.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/stdinc.hpp"

namespace sdl3 {

namespace env {

bool Set(const char *name, const char *value, bool overwrite) noexcept {
	return SDL_setenv_unsafe(name, value, overwrite) == 0;
}

} // namespace env

} // namespace sdl3
