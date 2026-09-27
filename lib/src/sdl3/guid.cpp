// Définitions de sdl3/guid.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/guid.hpp"

namespace sdl3 {

// ── Guid ─────────────────────────────────────────────────────────────────────

String Guid::ToString() const {
    char buf[33] = {};
    SDL_GUIDToString(*this, buf, sizeof(buf));
    return String(buf);
}

} // namespace sdl3
