#pragma once
#include <SDL3/SDL.h>
#include <vector>

#include "../core/core.hpp"
#include "render.hpp" // PixelFormat

namespace sdl3 {

// ============================================================================
// CameraSpec — wrap de SDL_CameraSpec
// ============================================================================

struct CameraSpec {
    PixelFormat format = PixelFormat::UNKNOWN;
    SDL_Colorspace colorspace = SDL_COLORSPACE_UNKNOWN;
    int width = 0, height = 0;
    int framerateNumerator = 0, framerateDenominator = 0;

    constexpr CameraSpec() = default;
    constexpr CameraSpec(PixelFormat fmt, int w, int h, int fpsNum = 30, int fpsDen = 1,
                         SDL_Colorspace cs = SDL_COLORSPACE_UNKNOWN) noexcept
        : format(fmt), colorspace(cs), width(w), height(h), framerateNumerator(fpsNum), framerateDenominator(fpsDen) {}
    constexpr explicit CameraSpec(const SDL_CameraSpec &s) noexcept
        : format(PixelFormat(s.format)), colorspace(s.colorspace), width(s.width), height(s.height),
          framerateNumerator(s.framerate_numerator), framerateDenominator(s.framerate_denominator) {}

    [[nodiscard]] constexpr operator SDL_CameraSpec() const noexcept {
        return {detail::ToSDL(format), colorspace, width, height, framerateNumerator, framerateDenominator};
    }
};

// ============================================================================
// CameraID / enums
// ============================================================================

using CameraID = SDL_CameraID;

enum class CameraPosition {
    UNKNOWN = SDL_CAMERA_POSITION_UNKNOWN,
    FRONT_FACING = SDL_CAMERA_POSITION_FRONT_FACING,
    BACK_FACING = SDL_CAMERA_POSITION_BACK_FACING,
};

enum class CameraPermission {
    DENIED = SDL_CAMERA_PERMISSION_STATE_DENIED,
    PENDING = SDL_CAMERA_PERMISSION_STATE_PENDING,
    APPROVED = SDL_CAMERA_PERMISSION_STATE_APPROVED,
};

// ============================================================================
// Camera drivers / device enumeration
// ============================================================================

namespace camera {

[[nodiscard]] inline int NumDrivers() noexcept { return SDL_GetNumCameraDrivers(); }
[[nodiscard]] inline const char *Driver(int index) noexcept { return SDL_GetCameraDriver(index); }
[[nodiscard]] inline const char *CurrentDriver() noexcept { return SDL_GetCurrentCameraDriver(); }

// Liste des identifiants de caméras actuellement connectées.
[[nodiscard]] inline std::vector<CameraID> Enumerated() {
    int count = 0;
    CameraID *ids = SDL_GetCameras(&count);
    if (!ids)
        return {};
    std::vector<CameraID> v(ids, ids + count);
    SDL_free(ids);
    return v;
}

[[nodiscard]] inline const char *Name(CameraID id) noexcept { return SDL_GetCameraName(id); }
[[nodiscard]] inline CameraPosition Position(CameraID id) noexcept { return CameraPosition(SDL_GetCameraPosition(id)); }

// Formats supportés par la caméra `id`.
[[nodiscard]] inline std::vector<CameraSpec> SupportedFormats(CameraID id) {
    int count = 0;
    SDL_CameraSpec **specs = SDL_GetCameraSupportedFormats(id, &count);
    if (!specs)
        return {};
    std::vector<CameraSpec> out;
    out.reserve(size_t(count));
    for (int i = 0; i < count; ++i)
        out.emplace_back(*specs[i]);
    SDL_free(specs);
    return out;
}

} // namespace camera

// ============================================================================
// CameraFrame — vue RAII sur une frame acquise (libérée automatiquement)
// ============================================================================

class CameraFrame {
    SDL_Camera *m_camera = nullptr;
    SDL_Surface *m_surface = nullptr;
    uint64_t m_timestampNs = 0;

    void Release() noexcept {
        if (m_camera && m_surface)
            SDL_ReleaseCameraFrame(m_camera, m_surface);
        m_camera = nullptr;
        m_surface = nullptr;
    }

public:
    CameraFrame() = default;
    CameraFrame(SDL_Camera *cam, SDL_Surface *surf, uint64_t ts) noexcept
        : m_camera(cam), m_surface(surf), m_timestampNs(ts) {}
    ~CameraFrame() { Release(); }

    CameraFrame(const CameraFrame &) = delete;
    CameraFrame &operator=(const CameraFrame &) = delete;

    CameraFrame(CameraFrame &&o) noexcept
        : m_camera(o.m_camera), m_surface(o.m_surface), m_timestampNs(o.m_timestampNs) {
        o.m_camera = nullptr;
        o.m_surface = nullptr;
    }
    CameraFrame &operator=(CameraFrame &&o) noexcept {
        if (this != &o) {
            Release();
            m_camera = o.m_camera;
            m_surface = o.m_surface;
            m_timestampNs = o.m_timestampNs;
            o.m_camera = nullptr;
            o.m_surface = nullptr;
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return m_surface != nullptr; }

    [[nodiscard]] int GetWidth() const noexcept { return m_surface ? m_surface->w : 0; }
    [[nodiscard]] int GetHeight() const noexcept { return m_surface ? m_surface->h : 0; }
    [[nodiscard]] Point GetSize() const noexcept { return {GetWidth(), GetHeight()}; }
    [[nodiscard]] int Pitch() const noexcept { return m_surface ? m_surface->pitch : 0; }
    [[nodiscard]] PixelFormat Format() const noexcept { return m_surface ? PixelFormat(m_surface->format) : PixelFormat::UNKNOWN; }
    [[nodiscard]] const void *Pixels() const noexcept { return m_surface ? m_surface->pixels : nullptr; }
    [[nodiscard]] uint64_t TimestampNs() const noexcept { return m_timestampNs; }
};

// ============================================================================
// Camera — RAII SDL_Camera
// ============================================================================

class Camera : public Wrapper<SDL_Camera, SDL_CloseCamera> {
public:
    using Wrapper::Wrapper;

    // `spec` peut être NONE pour laisser SDL choisir un format par défaut.
    [[nodiscard]] static Result<Camera, StringView> Open(CameraID id, Option<CameraSpec> spec = NONE) {
        SDL_CameraSpec sdlSpec{};
        if (spec.IsSome())
            sdlSpec = spec.Unwrap();

        auto *c = SDL_OpenCamera(id, spec.IsSome() ? &sdlSpec : nullptr);
        if (!c)
            return Err(GetError());
        return Ok(Camera(c));
    }

    [[nodiscard]] CameraPermission Permission() const noexcept {
        return m_handle ? CameraPermission(SDL_GetCameraPermissionState(m_handle)) : CameraPermission::DENIED;
    }

    [[nodiscard]] CameraID GetId() const noexcept { return m_handle ? SDL_GetCameraID(m_handle) : 0; }

    [[nodiscard]] SDL_PropertiesID Properties() const noexcept {
        return m_handle ? SDL_GetCameraProperties(m_handle) : 0;
    }

    // Renvoie le format négocié (valide une fois la permission accordée).
    [[nodiscard]] Option<CameraSpec> Format() const {
        SDL_CameraSpec spec{};
        if (!m_handle || !SDL_GetCameraFormat(m_handle, &spec))
            return NONE;
        return Some(CameraSpec(spec));
    }

    // Récupère la dernière frame disponible (non bloquant). La frame retournée
    // est auto-libérée (RAII) — aucun appel de "release" manuel nécessaire.
    [[nodiscard]] Option<CameraFrame> AcquireFrame() noexcept {
        if (!m_handle)
            return NONE;
        uint64_t ts = 0;
        SDL_Surface *frame = SDL_AcquireCameraFrame(m_handle, &ts);
        if (!frame)
            return NONE;
        return Some(CameraFrame(m_handle, frame, ts));
    }
};

} // namespace sdl3
