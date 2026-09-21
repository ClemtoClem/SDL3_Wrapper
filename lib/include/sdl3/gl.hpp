#pragma once
#include <SDL3/SDL.h>

#include "../core/core.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// GlContext — RAII SDL_GLContext (OpenGL context tied to a window)
// ============================================================================

class GlContext {
    SDL_GLContext m_handle = nullptr;

public:
    constexpr GlContext() noexcept = default;
    explicit GlContext(SDL_GLContext h) noexcept : m_handle(h) {}
    ~GlContext() {
        if (m_handle)
            SDL_GL_DestroyContext(m_handle);
    }

    GlContext(const GlContext &) = delete;
    GlContext &operator=(const GlContext &) = delete;
    GlContext(GlContext &&o) noexcept : m_handle(o.m_handle) { o.m_handle = nullptr; }
    GlContext &operator=(GlContext &&o) noexcept {
        if (this != &o) {
            if (m_handle)
                SDL_GL_DestroyContext(m_handle);
            m_handle = o.m_handle;
            o.m_handle = nullptr;
        }
        return *this;
    }

    [[nodiscard]] SDL_GLContext Get() const noexcept { return m_handle; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }

    // ── Factory ──────────────────────────────────────────────────────────────

    [[nodiscard]] static Result<GlContext, Error> Create(SDL_Window *win) {
        auto *ctx = SDL_GL_CreateContext(win);
        if (!ctx)
            return Err(GetError());
        return Ok(GlContext(ctx));
    }

    // ── Context management ───────────────────────────────────────────────────

    bool MakeCurrent(SDL_Window *win) noexcept { return m_handle && SDL_GL_MakeCurrent(win, m_handle); }

    // Return the current OpenGL window (not owned).
    [[nodiscard]] static SDL_Window *CurrentWindow() noexcept { return SDL_GL_GetCurrentWindow(); }
    // Return the current OpenGL context m_handle (not owned).
    [[nodiscard]] static SDL_GLContext CurrentContext() noexcept { return SDL_GL_GetCurrentContext(); }
};

// ============================================================================
// OpenGL free functions — wrappers for SDL_GL_* and SDL_EGL_* helpers
// ============================================================================

namespace gl {

// ── Library loading ───────────────────────────────────────────────────────────

inline bool LoadLibrary(const char *path = nullptr) noexcept { return SDL_GL_LoadLibrary(path); }
inline void UnloadLibrary() noexcept { SDL_GL_UnloadLibrary(); }

// ── Proc address ──────────────────────────────────────────────────────────────

[[nodiscard]] inline SDL_FunctionPointer ProcAddress(const char *proc) noexcept { return SDL_GL_GetProcAddress(proc); }
[[nodiscard]] inline SDL_FunctionPointer EglProcAddress(const char *proc) noexcept {
    return SDL_EGL_GetProcAddress(proc);
}

// ── Extension query ───────────────────────────────────────────────────────────

[[nodiscard]] inline bool ExtensionSupported(const char *ext) noexcept { return SDL_GL_ExtensionSupported(ext); }

// ── Attributes ───────────────────────────────────────────────────────────────

inline bool SetAttribute(SDL_GLAttr attr, int value) noexcept { return SDL_GL_SetAttribute(attr, value); }
inline Option<int> GetAttribute(SDL_GLAttr attr) noexcept {
    int v = 0;
    if (!SDL_GL_GetAttribute(attr, &v))
        return NONE;
    return Some(v);
}
inline void ResetAttributes() noexcept { SDL_GL_ResetAttributes(); }

// ── Swap interval ─────────────────────────────────────────────────────────────

inline bool SetSwapInterval(int interval) noexcept { return SDL_GL_SetSwapInterval(interval); }
inline Option<int> GetSwapInterval() noexcept {
    int v = 0;
    if (!SDL_GL_GetSwapInterval(&v))
        return NONE;
    return Some(v);
}

// ── Swap ──────────────────────────────────────────────────────────────────────

inline bool SwapWindow(SDL_Window *win) noexcept { return SDL_GL_SwapWindow(win); }

// ── EGL helpers ───────────────────────────────────────────────────────────────

[[nodiscard]] inline SDL_EGLDisplay EglDisplay() noexcept { return SDL_EGL_GetCurrentDisplay(); }
[[nodiscard]] inline SDL_EGLConfig EglConfig() noexcept { return SDL_EGL_GetCurrentConfig(); }
[[nodiscard]] inline SDL_EGLSurface EglWindowSurface(SDL_Window *win) noexcept { return SDL_EGL_GetWindowSurface(win); }

} // namespace gl
} // namespace sdl3
