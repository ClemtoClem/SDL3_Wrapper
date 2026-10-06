// Définitions de sdl3/guid.hpp
#include "sdl3/guid.hpp"

namespace sdl3 {

// ── Guid ─────────────────────────────────────────────────────────────────────

String Guid::ToString() const {
    char buf[33] = {};
    SDL_GUIDToString(*this, buf, sizeof(buf));
    return String(buf);
}

} // namespace sdl3
