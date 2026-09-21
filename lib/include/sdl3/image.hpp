#pragma once
#include <SDL3_image/SDL_image.h>

#include "../core/core.hpp"
#include "render.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// SDL3_image 3.x does not require initialization — just call the load functions directly.

// ============================================================================
// Loading functions
// ============================================================================

/// Load an image file as a Surface (any format SDL3_image supports).
[[nodiscard]] inline Result<Surface, StringView> ImgLoad(const String &path) {
    auto *s = IMG_Load(path.c_str());
    if (!s)
        return Err(GetError());
    return Ok(Surface(s));
}

/// Load an image directly as a Texture in the given Renderer.
[[nodiscard]] inline Result<Texture, StringView> ImgLoadTexture(Renderer &ren, const String &path) {
    auto *t = IMG_LoadTexture(ren.Get(), path.c_str());
    if (!t)
        return Err(GetError());
    return Ok(Texture(t));
}

/// Load an image from a memory buffer as a Surface.
[[nodiscard]] inline Result<Surface, StringView> ImgLoadMemory(const void *data, size_t size) {
    auto *io = SDL_IOFromConstMem(data, size);
    if (!io)
        return Err(GetError());
    auto *s = IMG_Load_IO(io, true);
    if (!s)
        return Err(GetError());
    return Ok(Surface(s));
}

/// Load a texture from a memory buffer.
[[nodiscard]] inline Result<Texture, StringView> ImgLoadTextureMemory(Renderer &ren, const void *data, size_t size) {
    auto *io = SDL_IOFromConstMem(data, size);
    if (!io)
        return Err(GetError());
    auto *t = IMG_LoadTexture_IO(ren.Get(), io, true);
    if (!t)
        return Err(GetError());
    return Ok(Texture(t));
}

// ============================================================================
// Saving functions
// ============================================================================

// Lacune trouvée en implémentant les captures d'écran de l'éditeur de niveau :
// le wrapper savait CHARGER une image (ci-dessus) mais pas en ÉCRIRE une, alors
// que SDL3_image expose IMG_SavePNG/IMG_SaveJPG depuis toujours. Toute capture
// d'écran passe par Renderer::ReadPixels() -> Surface, puis par ces fonctions.
//
// `Surface` est pris par référence const (IMG_SavePNG ne modifie pas la
// surface) et le SDL_Surface* brut ne sort jamais de l'API — il n'est
// déréférencé qu'ici, en interne (cf. memory/feedback_wrap_c_pointers.md).

/// Écrit `surface` en PNG. Err porte le message d'erreur SDL d'origine.
[[nodiscard]] inline Result<bool, StringView> ImgSavePng(const Surface &surface, const String &path) {
    if (!surface.Get())
        return Err(StringView("ImgSavePng: surface nulle"));
    if (!IMG_SavePNG(surface.Get(), path.c_str()))
        return Err(GetError());
    return Ok(true);
}

/// Écrit `surface` en JPEG. `quality` va de 0 (minimale) à 100 (maximale).
[[nodiscard]] inline Result<bool, StringView> ImgSaveJpg(const Surface &surface, const String &path,
                                                         int quality = 90) {
    if (!surface.Get())
        return Err(StringView("ImgSaveJpg: surface nulle"));
    if (!IMG_SaveJPG(surface.Get(), path.c_str(), quality))
        return Err(GetError());
    return Ok(true);
}

} // namespace sdl3
