#pragma once

#include "../core/core.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

inline bool SetError(StringView msg) { return SDL_SetError("%.*s", int(msg.GetSize()), msg.GetData()) == 0; }

template <class... Args> inline bool SetError(StringView fmt, Args &&...args) {
    return SDL_SetError(fmt.GetData(), std::forward<Args>(args)...) == 0;
}

inline StringView GetError() { return StringView(SDL_GetError()); }

inline bool ClearError() { return SDL_ClearError() == 0; }

inline bool ErrorIsOutOfMemory() { return SDL_OutOfMemory(); }

inline Result<bool, Error> CheckError(bool result) {
    if (result) return Ok(result);
    return Err(GetError());
}

template <class T> inline Result<T, Error> CheckError(T result) {
    if (result) return Ok(result);
    return Err(GetError());
}

template <class T> inline Result<T, Error> CheckError(T result, T invalidValue) {
    if (result == invalidValue) return Ok(result);
    return Err(GetError());
}

template <class T> inline Result<T, Error> CheckErrorIfNot(T result, T validValue) {
    if (result != validValue) return Ok(result);
    return Err(GetError());
}

} /* namespace sdl3 */