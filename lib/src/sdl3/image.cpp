// Définitions de sdl3/image.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/image.hpp"

namespace sdl3 {

Result<Surface, StringView> ImgLoad(const String &path) {
    auto *s = IMG_Load(path.c_str());
    if (!s)
        return Err(GetError());
    return Ok(Surface(s));
}

Result<Texture, StringView> ImgLoadTexture(Renderer &ren, const String &path) {
    auto *t = IMG_LoadTexture(ren.Get(), path.c_str());
    if (!t)
        return Err(GetError());
    return Ok(Texture(t));
}

Result<Surface, StringView> ImgLoadMemory(const void *data, size_t size) {
    auto *io = SDL_IOFromConstMem(data, size);
    if (!io)
        return Err(GetError());
    auto *s = IMG_Load_IO(io, true);
    if (!s)
        return Err(GetError());
    return Ok(Surface(s));
}

Result<Texture, StringView> ImgLoadTextureMemory(Renderer &ren, const void *data, size_t size) {
    auto *io = SDL_IOFromConstMem(data, size);
    if (!io)
        return Err(GetError());
    auto *t = IMG_LoadTexture_IO(ren.Get(), io, true);
    if (!t)
        return Err(GetError());
    return Ok(Texture(t));
}

Result<bool, StringView> ImgSavePng(const Surface &surface, const String &path) {
    if (!surface.Get())
        return Err(StringView("ImgSavePng: surface nulle"));
    if (!IMG_SavePNG(surface.Get(), path.c_str()))
        return Err(GetError());
    return Ok(true);
}

Result<bool, StringView> ImgSaveJpg(const Surface &surface, const String &path, int quality) {
    if (!surface.Get())
        return Err(StringView("ImgSaveJpg: surface nulle"));
    if (!IMG_SaveJPG(surface.Get(), path.c_str(), quality))
        return Err(GetError());
    return Ok(true);
}

} // namespace sdl3
