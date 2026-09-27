// Définitions de sdl3/gl.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/gl.hpp"

namespace sdl3 {

// ── GlContext ────────────────────────────────────────────────────────────────

GlContext::~GlContext() {
    if (m_handle)
        SDL_GL_DestroyContext(m_handle);
}

Result<GlContext, Error> GlContext::Create(SDL_Window *win) {
    auto *ctx = SDL_GL_CreateContext(win);
    if (!ctx)
        return Err(GetError());
    return Ok(GlContext(ctx));
}

namespace gl {

SDL_FunctionPointer EglProcAddress(const char *proc) noexcept {
    return SDL_EGL_GetProcAddress(proc);
}

Option<int> GetAttribute(SDL_GLAttr attr) noexcept {
    int v = 0;
    if (!SDL_GL_GetAttribute(attr, &v))
        return NONE;
    return Some(v);
}

Option<int> GetSwapInterval() noexcept {
    int v = 0;
    if (!SDL_GL_GetSwapInterval(&v))
        return NONE;
    return Some(v);
}

} // namespace gl

} // namespace sdl3
