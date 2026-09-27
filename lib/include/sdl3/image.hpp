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
[[nodiscard]] Result<Surface, StringView> ImgLoad(const String &path);

/// Load an image directly as a Texture in the given Renderer.
[[nodiscard]] Result<Texture, StringView> ImgLoadTexture(Renderer &ren, const String &path);

/// Load an image from a memory buffer as a Surface.
[[nodiscard]] Result<Surface, StringView> ImgLoadMemory(const void *data, size_t size);

/// Load a texture from a memory buffer.
[[nodiscard]] Result<Texture, StringView> ImgLoadTextureMemory(Renderer &ren, const void *data, size_t size);

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
[[nodiscard]] Result<bool, StringView> ImgSavePng(const Surface &surface, const String &path);

/// Écrit `surface` en JPEG. `quality` va de 0 (minimale) à 100 (maximale).
[[nodiscard]] Result<bool, StringView> ImgSaveJpg(const Surface &surface, const String &path,
                                                         int quality = 90);

} // namespace sdl3
