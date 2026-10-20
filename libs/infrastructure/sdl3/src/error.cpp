// Définitions de sdl3/error.hpp
#include "sdl3/error.hpp"

namespace sdl3 {

Result<bool, Error> CheckError(bool result) {
    if (result) return Ok(result);
    return Err(GetError());
}

} // namespace sdl3
