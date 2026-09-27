// Définitions de sdl3/error.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/error.hpp"

namespace sdl3 {

Result<bool, Error> CheckError(bool result) {
    if (result) return Ok(result);
    return Err(GetError());
}

} // namespace sdl3
