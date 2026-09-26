#pragma once
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <cmath>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "../core/core.hpp"
#include "../core/wrapper.hpp"
#include "error.hpp"
#include "stdinc.hpp"
#include "structs.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

class Surface;
class Window;
class Renderer;
// Déclaration avancée seulement : Renderer::GetGPUDevice() n'a besoin que
// d'un type de retour dans sa DÉCLARATION ici — le corps (qui a besoin du
// type complet) vit dans gpu.hpp, qui inclut déjà render.hpp (l'inverse
// créerait un cycle, même patron que Canvas::RenderObjectOffscreen/
// offscreen.hpp dans render3d/canvas.hpp).
class GpuDevice;

using SurfaceRaw = SDL_Surface;
using WindowRaw = SDL_Window;
using RendererRaw = SDL_Renderer;
using SurfaceRef = Ref<Surface>;
using WindowRef = Ref<Window>;
using RendererRef = Ref<Renderer>;

// ============================================================================
// Surface
// ============================================================================

enum class PixelFormat {
	UNKNOWN = SDL_PIXELFORMAT_UNKNOWN,
	INDEX1_LSB = SDL_PIXELFORMAT_INDEX1LSB,
	INDEX1_MSB = SDL_PIXELFORMAT_INDEX1MSB,
	INDEX2_LSB = SDL_PIXELFORMAT_INDEX2LSB,
	INDEX2_MSB = SDL_PIXELFORMAT_INDEX2MSB,
	INDEX4_LSB = SDL_PIXELFORMAT_INDEX4LSB,
	INDEX4_MSB = SDL_PIXELFORMAT_INDEX4MSB,
	INDEX8 = SDL_PIXELFORMAT_INDEX8,
	RGB332 = SDL_PIXELFORMAT_RGB332,
	XRGB4444 = SDL_PIXELFORMAT_XRGB4444,
	XBGR4444 = SDL_PIXELFORMAT_XBGR4444,
	XRGB1555 = SDL_PIXELFORMAT_XRGB1555,
	XBGR1555 = SDL_PIXELFORMAT_XBGR1555,
	ARGB4444 = SDL_PIXELFORMAT_ARGB4444,
	RGBA4444 = SDL_PIXELFORMAT_RGBA4444,
	ABGR4444 = SDL_PIXELFORMAT_ABGR4444,
	BGRA4444 = SDL_PIXELFORMAT_BGRA4444,
	ARGB1555 = SDL_PIXELFORMAT_ARGB1555,
	RGBA5551 = SDL_PIXELFORMAT_RGBA5551,
	ABGR1555 = SDL_PIXELFORMAT_ABGR1555,
	BGRA5551 = SDL_PIXELFORMAT_BGRA5551,
	RGB565 = SDL_PIXELFORMAT_RGB565,
	BGR565 = SDL_PIXELFORMAT_BGR565,
	RGB24 = SDL_PIXELFORMAT_RGB24,
	BGR24 = SDL_PIXELFORMAT_BGR24,
	XRGB8888 = SDL_PIXELFORMAT_XRGB8888,
	RGBX8888 = SDL_PIXELFORMAT_RGBX8888,
	XBGR8888 = SDL_PIXELFORMAT_XBGR8888,
	BGRX8888 = SDL_PIXELFORMAT_BGRX8888,
	ARGB8888 = SDL_PIXELFORMAT_ARGB8888,
	RGBA8888 = SDL_PIXELFORMAT_RGBA8888,
	ABGR8888 = SDL_PIXELFORMAT_ABGR8888,
	BGRA8888 = SDL_PIXELFORMAT_BGRA8888,
	XRGB2101010 = SDL_PIXELFORMAT_XRGB2101010,
	XBGR2101010 = SDL_PIXELFORMAT_XBGR2101010,
	ARGB2101010 = SDL_PIXELFORMAT_ARGB2101010,
	ABGR2101010 = SDL_PIXELFORMAT_ABGR2101010,
	RGB48 = SDL_PIXELFORMAT_RGB48,
	BGR48 = SDL_PIXELFORMAT_BGR48,
	RGBA64 = SDL_PIXELFORMAT_RGBA64,
	ARGB64 = SDL_PIXELFORMAT_ARGB64,
	BGRA64 = SDL_PIXELFORMAT_BGRA64,
	ABGR64 = SDL_PIXELFORMAT_ABGR64,
	RGB48_FLOAT = SDL_PIXELFORMAT_RGB48_FLOAT,
	BGR48_FLOAT = SDL_PIXELFORMAT_BGR48_FLOAT,
	RGBA64_FLOAT = SDL_PIXELFORMAT_RGBA64_FLOAT,
	ARGB64_FLOAT = SDL_PIXELFORMAT_ARGB64_FLOAT,
	BGRA64_FLOAT = SDL_PIXELFORMAT_BGRA64_FLOAT,
	ABGR64_FLOAT = SDL_PIXELFORMAT_ABGR64_FLOAT,
	RGB96_FLOAT = SDL_PIXELFORMAT_RGB96_FLOAT,
	BGR96_FLOAT = SDL_PIXELFORMAT_BGR96_FLOAT,
	RGBA128_FLOAT = SDL_PIXELFORMAT_RGBA128_FLOAT,
	ARGB128_FLOAT = SDL_PIXELFORMAT_ARGB128_FLOAT,
	BGRA128_FLOAT = SDL_PIXELFORMAT_BGRA128_FLOAT,
	ABGR128_FLOAT = SDL_PIXELFORMAT_ABGR128_FLOAT,
	YV12 = SDL_PIXELFORMAT_YV12,
	IYUV = SDL_PIXELFORMAT_IYUV,
	YUY2 = SDL_PIXELFORMAT_YUY2,
	UYVY = SDL_PIXELFORMAT_UYVY,
	YVYU = SDL_PIXELFORMAT_YVYU,
	NV12 = SDL_PIXELFORMAT_NV12,
	NV21 = SDL_PIXELFORMAT_NV21,
	P010 = SDL_PIXELFORMAT_P010,
	EXTERNAL_OES = SDL_PIXELFORMAT_EXTERNAL_OES,
	MJPG = SDL_PIXELFORMAT_MJPG,
	RGBA32 = SDL_PIXELFORMAT_RGBA32,
	ARGB32 = SDL_PIXELFORMAT_ARGB32,
	BGRA32 = SDL_PIXELFORMAT_BGRA32,
	ABGR32 = SDL_PIXELFORMAT_ABGR32,
	RGBX32 = SDL_PIXELFORMAT_RGBX32,
	XRGB32 = SDL_PIXELFORMAT_XRGB32,
	BGRX32 = SDL_PIXELFORMAT_BGRX32,
	XBGR32 = SDL_PIXELFORMAT_XBGR32,
};

namespace detail {
	constexpr SDL_PixelFormat ToSDL(PixelFormat e) {
		return static_cast<SDL_PixelFormat>(e);
	}
}

class Surface : public Wrapper<SDL_Surface, SDL_DestroySurface> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Surface, Error> Create(int w, int h, PixelFormat fmt = PixelFormat::RGBA8888) {
		auto *s = SDL_CreateSurface(w, h, detail::ToSDL(fmt));
		if (!s)
			return Err(GetError());
		return Ok(Surface(s));
	}

	/// Surface POSSÉDANT ses pixels, remplie par copie de `pixels` (`pitch`
	/// octets par ligne, disposés selon `fmt`). Copie plutôt que
	/// SDL_CreateSurfaceFrom (qui emprunte le tampon) : la surface peut alors
	/// survivre au tampon source — typiquement un framebuffer d'émulateur ou
	/// de rendu logiciel écrit ensuite en PNG (cf. ImgSavePng). Ajouté pour
	/// examples/emulator_demo (captures du framebuffer émulé en mode sans
	/// écran) : le wrapper savait charger et écrire une surface, mais pas en
	/// construire une depuis des octets bruts.
	[[nodiscard]] static Result<Surface, StringView> CreateFromPixels(int w, int h, PixelFormat fmt, const void *pixels,
																	  int pitch) {
		if (w <= 0 || h <= 0 || !pixels || pitch <= 0)
			return Err(StringView("Surface::CreateFromPixels: dimensions, pitch ou tampon invalides"));
		auto *s = SDL_CreateSurface(w, h, detail::ToSDL(fmt));
		if (!s)
			return Err(GetError());
		Surface surface(s);
		if (!SDL_LockSurface(s))
			return Err(GetError());
		const size_t rowBytes = size_t(pitch < s->pitch ? pitch : s->pitch);
		for (int y = 0; y < h; ++y)
			SDL_memcpy(static_cast<uint8_t *>(s->pixels) + size_t(y) * size_t(s->pitch),
					   static_cast<const uint8_t *>(pixels) + size_t(y) * size_t(pitch), rowBytes);
		SDL_UnlockSurface(s);
		return Ok(std::move(surface));
	}

	[[nodiscard]] static Result<Surface, StringView> CreateFromFile(const String &path) {
		auto *s = IMG_Load(path.c_str());
		if (!s)
			return Err(GetError());
		return Ok(Surface(s));
	}

	[[nodiscard]] Result<Surface, StringView> Convert(PixelFormat fmt) const {
		auto *s = SDL_ConvertSurface(m_handle, detail::ToSDL(fmt));
		if (!s)
			return Err(GetError());
		return Ok(Surface(s));
	}

	[[nodiscard]] int GetWidth() const { return m_handle ? m_handle->w : 0; }
	[[nodiscard]] int GetHeight() const { return m_handle ? m_handle->h : 0; }
	[[nodiscard]] Point GetSize() const { return {GetWidth(), GetHeight()}; }

	bool Fill(Color c) {
		if (!m_handle)
			return false;
		uint32_t mapped = SDL_MapSurfaceRGBA(m_handle, c.r, c.g, c.b, c.a);
		return SDL_FillSurfaceRect(m_handle, nullptr, mapped);
	}

	bool Blit(const Surface &src, Point dest) {
		if (!m_handle || !src.m_handle)
			return false;
		SDL_Rect dstR{dest.x, dest.y, src.GetWidth(), src.GetHeight()};
		return SDL_BlitSurface(src.m_handle, nullptr, m_handle, &dstR);
	}

	bool SetColorKey(bool enable, Color c) {
		if (!m_handle)
			return false;
		uint32_t key = SDL_MapSurfaceRGBA(m_handle, c.r, c.g, c.b, c.a);
		return SDL_SetSurfaceColorKey(m_handle, enable, key);
	}

	bool SetAlphaMod(uint8_t alpha) { return m_handle && SDL_SetSurfaceAlphaMod(m_handle, alpha); }
	bool SetBlendMode(BlendMode mode) {
		return m_handle && SDL_SetSurfaceBlendMode(m_handle, SDL_BlendMode(int(mode)));
	}
};

// ============================================================================
// Window
// ============================================================================

using WindowFlags = SDL_WindowFlags;

namespace window_flags {
constexpr WindowFlags FULLSCREEN = SDL_WINDOW_FULLSCREEN;
constexpr WindowFlags OPEN_GL = SDL_WINDOW_OPENGL;
constexpr WindowFlags OCCULED = SDL_WINDOW_OCCLUDED;
constexpr WindowFlags HIDDEN = SDL_WINDOW_HIDDEN;
constexpr WindowFlags BORDERLESS = SDL_WINDOW_BORDERLESS;
constexpr WindowFlags RESIZABLE = SDL_WINDOW_RESIZABLE;
constexpr WindowFlags MINIMIZED = SDL_WINDOW_MINIMIZED;
constexpr WindowFlags MAXIMIZED = SDL_WINDOW_MAXIMIZED;
constexpr WindowFlags MOUSE_GRABBED = SDL_WINDOW_MOUSE_GRABBED;
constexpr WindowFlags INPUT_FOCUS = SDL_WINDOW_INPUT_FOCUS;
constexpr WindowFlags MOUSE_FOCUS = SDL_WINDOW_MOUSE_FOCUS;
constexpr WindowFlags EXTERNAL = SDL_WINDOW_EXTERNAL;
constexpr WindowFlags MODAL = SDL_WINDOW_MODAL;
constexpr WindowFlags HIGH_PIXEL_DENSITY = SDL_WINDOW_HIGH_PIXEL_DENSITY;
constexpr WindowFlags MOUSE_CAPTURE = SDL_WINDOW_MOUSE_CAPTURE;
constexpr WindowFlags MOUSE_RELATIVE_MODE = SDL_WINDOW_MOUSE_RELATIVE_MODE;
constexpr WindowFlags ALWAYS_ON_TOP = SDL_WINDOW_ALWAYS_ON_TOP;
constexpr WindowFlags UTILITY = SDL_WINDOW_UTILITY;
constexpr WindowFlags TOOLTIP = SDL_WINDOW_TOOLTIP;
constexpr WindowFlags POPUP_MENU = SDL_WINDOW_POPUP_MENU;
constexpr WindowFlags KEYBOARD_GRABBED = SDL_WINDOW_KEYBOARD_GRABBED;
constexpr WindowFlags FILL_DOCUMENT = SDL_WINDOW_FILL_DOCUMENT;
constexpr WindowFlags VULKAN = SDL_WINDOW_VULKAN;
constexpr WindowFlags METAL = SDL_WINDOW_METAL;
constexpr WindowFlags TRANSPARENT = SDL_WINDOW_TRANSPARENT;
constexpr WindowFlags NOT_FOCUSABLE = SDL_WINDOW_NOT_FOCUSABLE;
} // namespace window_flags

class Window : public Wrapper<SDL_Window, SDL_DestroyWindow> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Window, Error> Create(const String &title, int w, int h,
														   SDL_WindowFlags flags = 0) {
		auto *win = SDL_CreateWindow(title.c_str(), w, h, flags);
		if (!win)
			return Err(GetError());
		return Ok(Window(win));
	}

	[[nodiscard]] static Result<Window, Error> Create(const String &title, std::span<const int> size,
														   SDL_WindowFlags flags = 0) {
		auto *win = SDL_CreateWindow(title.c_str(), size[0], size[1], flags);
		if (!win)
			return Err(GetError());
		return Ok(Window(win));
	}

	[[nodiscard]] static Result<Window, Error> Create(const String &title, std::span<const int> pos,
														   std::span<const int> size, SDL_WindowFlags flags = 0) {
		auto *win = SDL_CreateWindow(title.c_str(), size[0], size[1], flags);
		if (!win)
			return Err(GetError());
		SDL_SetWindowPosition(win, pos[0], pos[1]);
		return Ok(Window(win));
	}

	// ── Getters ──────────────────────────────────────────────────────────────

	[[nodiscard]] int GetWidth() const {
		int w = 0;
		if (m_handle)
			SDL_GetWindowSize(m_handle, &w, nullptr);
		return w;
	}
	[[nodiscard]] int GetHeight() const {
		int h = 0;
		if (m_handle)
			SDL_GetWindowSize(m_handle, nullptr, &h);
		return h;
	}
	[[nodiscard]] Point GetSize() const {
		int w = 0, h = 0;
		if (m_handle)
			SDL_GetWindowSize(m_handle, &w, &h);
		return {w, h};
	}
	[[nodiscard]] int GetX() const {
		int x = 0;
		if (m_handle)
			SDL_GetWindowPosition(m_handle, &x, nullptr);
		return x;
	}
	[[nodiscard]] int GetY() const {
		int y = 0;
		if (m_handle)
			SDL_GetWindowPosition(m_handle, nullptr, &y);
		return y;
	}
	[[nodiscard]] Point GetPosition() const {
		int x = 0, y = 0;
		if (m_handle)
			SDL_GetWindowPosition(m_handle, &x, &y);
		return {x, y};
	}
	[[nodiscard]] bool IsFullscreen() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_FULLSCREEN; }
	[[nodiscard]] bool IsResizable() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_RESIZABLE; }
	[[nodiscard]] bool IsBordered() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_BORDERLESS; }
	[[nodiscard]] bool IsVisible() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_HIDDEN; }
	[[nodiscard]] bool HasInputFocus() const {
		return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_INPUT_FOCUS;
	}
	[[nodiscard]] bool HasMouseFocus() const {
		return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_MOUSE_FOCUS;
	}
	[[nodiscard]] bool IsMinimized() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_MINIMIZED; }
	[[nodiscard]] bool IsMaximized() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_MAXIMIZED; }
	[[nodiscard]] SDL_WindowID GetId() const { return m_handle ? SDL_GetWindowID(m_handle) : 0; }
	[[nodiscard]] String Title() const {
		const char *t = m_handle ? SDL_GetWindowTitle(m_handle) : "";
		return String(t ? t : "");
	}

	// ── Setters ──────────────────────────────────────────────────────────────

	void SetTitle(const String &t) {
		if (m_handle)
			SDL_SetWindowTitle(m_handle, t.CStr());
	}
	void SetSize(int w, int h) {
		if (m_handle)
			SDL_SetWindowSize(m_handle, w, h);
	}
	void SetPosition(int x, int y) {
		if (m_handle)
			SDL_SetWindowPosition(m_handle, x, y);
	}
	void SetFullscreen(bool on) {
		if (m_handle)
			SDL_SetWindowFullscreen(m_handle, on);
	}
	void SetResizable(bool on) {
		if (m_handle)
			SDL_SetWindowResizable(m_handle, on);
	}
	void SetBordered(bool on) {
		if (m_handle)
			SDL_SetWindowBordered(m_handle, on);
	}
	void SetOpacity(float opacity) {
		if (m_handle)
			SDL_SetWindowOpacity(m_handle, opacity);
	}
	/// Active la saisie de texte (événements TEXT_INPUT ; affiche le clavier
	/// virtuel sur les plateformes qui en ont un).
	void StartTextInput() {
		if (m_handle)
			SDL_StartTextInput(m_handle);
	}
	void StopTextInput() {
		if (m_handle)
			SDL_StopTextInput(m_handle);
	}
	[[nodiscard]] bool TextInputActive() const { return m_handle && SDL_TextInputActive(m_handle); }

	void Show() {
		if (m_handle)
			SDL_ShowWindow(m_handle);
	}
	void Hide() {
		if (m_handle)
			SDL_HideWindow(m_handle);
	}
	void Raise() {
		if (m_handle)
			SDL_RaiseWindow(m_handle);
	}
	void Maximize() {
		if (m_handle)
			SDL_MaximizeWindow(m_handle);
	}
	void Minimize() {
		if (m_handle)
			SDL_MinimizeWindow(m_handle);
	}
	void Restore() {
		if (m_handle)
			SDL_RestoreWindow(m_handle);
	}

	[[nodiscard]] SDL_Surface *Surface() const { return m_handle ? SDL_GetWindowSurface(m_handle) : nullptr; }
	bool UpdateSurface() { return m_handle && SDL_UpdateWindowSurface(m_handle); }

	/// Installs a hit-test callback (SDL_SetWindowHitTest) so parts of a
	/// borderless window can report themselves as draggable/resizable, e.g. a
	/// hand-drawn title bar strip returning SDL_HITTEST_DRAGGABLE. Pass an
	/// empty function to clear a previously-installed callback.
	/// Souris « relative » : curseur caché et capturé, seuls les déplacements
	/// (xrel/yrel) arrivent — la vue à la première personne d'un jeu.
	bool SetRelativeMouseMode(bool enabled) { return m_handle && SDL_SetWindowRelativeMouseMode(m_handle, enabled); }
	[[nodiscard]] bool RelativeMouseMode() const { return m_handle && SDL_GetWindowRelativeMouseMode(m_handle); }

	bool SetHitTest(std::function<SDL_HitTestResult(const SDL_Point &)> fn) {
		if (!m_handle)
			return false;
		if (!fn) {
			hitTest.reset();
			return SDL_SetWindowHitTest(m_handle, nullptr, nullptr);
		}
		hitTest = std::make_unique<std::function<SDL_HitTestResult(const SDL_Point &)>>(std::move(fn));
		return SDL_SetWindowHitTest(m_handle, &Window::HitTestTrampoline, hitTest.get());
	}

private:
	std::unique_ptr<std::function<SDL_HitTestResult(const SDL_Point &)>> hitTest;

	static SDL_HitTestResult SDLCALL HitTestTrampoline(SDL_Window *, const SDL_Point *area, void *data) {
		auto *fn = static_cast<std::function<SDL_HitTestResult(const SDL_Point &)> *>(data);
		return (*fn)(*area);
	}
};

// ============================================================================
// Texture
// ============================================================================

enum class TextureAccess {
	STATIC    = SDL_TEXTUREACCESS_STATIC,    /**< Changes rarely, not lockable */
    STREAMING = SDL_TEXTUREACCESS_STREAMING, /**< Changes frequently, lockable */
    TARGET    = SDL_TEXTUREACCESS_TARGET     /**< Texture can be used as a render target */
};
namespace detail {
	constexpr SDL_TextureAccess ToSDL(TextureAccess e) {
		return static_cast<SDL_TextureAccess>(e);
	}
}

class Texture : public Wrapper<SDL_Texture, SDL_DestroyTexture> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Texture, Error> Create(Renderer &ren, int w, int h,
															PixelFormat fmt = PixelFormat::RGBA8888);
	[[nodiscard]] static Result<Texture, StringView> CreateFromSurface(Renderer &ren, const Surface &surface);
	[[nodiscard]] static Result<Texture, StringView> CreateFromFile(Renderer &ren, const String &path);

	[[nodiscard]] FPoint GetSize() const {
		float w = 0, h = 0;
		if (m_handle)
			SDL_GetTextureSize(m_handle, &w, &h);
		return {w, h};
	}
	[[nodiscard]] int GetWidth() const { return int(GetSize().x); }
	[[nodiscard]] int GetHeight() const { return int(GetSize().y); }

	bool SetColorMod(uint8_t r, uint8_t g, uint8_t b) { return m_handle && SDL_SetTextureColorMod(m_handle, r, g, b); }
	bool SetColorMod(Color c) { return SetColorMod(c.r, c.g, c.b); }
	bool SetAlphaMod(uint8_t a) { return m_handle && SDL_SetTextureAlphaMod(m_handle, a); }
	bool SetBlendMode(BlendMode mode) {
		return m_handle && SDL_SetTextureBlendMode(m_handle, SDL_BlendMode(int(mode)));
	}
	bool SetScaleMode(SDL_ScaleMode mode) { return m_handle && SDL_SetTextureScaleMode(m_handle, mode); }

	/// Replaces the full texture contents (or just `rect` if given) from a raw
	/// pixel buffer laid out per the texture's own format, `pitch` bytes per row.
	bool Update(const void *pixels, int pitch, Option<Rect> rect = NONE) {
		if (rect.IsSome()) {
			SDL_Rect r = SDL_Rect(rect.Unwrap());
			return m_handle && SDL_UpdateTexture(m_handle, &r, pixels, pitch);
		}
		return m_handle && SDL_UpdateTexture(m_handle, nullptr, pixels, pitch);
	}
};

// Corners / Sides (rayons de coins, épaisseurs de bords) vivent dans structs.hpp
// avec les autres types géométriques (Rect/FRect/Point...).

// ============================================================================
// Génération de points de contour — utilisés en interne par Renderer pour les
// formes complexes (cercle, ellipse, arc) ; exposés en namespace `shapes` au
// cas où l'appelant veut les points bruts sans passer par un Renderer.
// ============================================================================

namespace shapes {

// Cercle par points via l'algorithme de Bresenham (style SDL3pp::GenerateCirclePoints).
[[nodiscard]] inline std::vector<FPoint> GenerateCirclePoints(FPoint center, float radius) {
	std::vector<FPoint> points;
	int x = 0, y = int(radius);
	int d = 3 - 2 * int(radius);
	while (x <= y) {
		float fx = float(x), fy = float(y);
		points.push_back({center.x + fx, center.y + fy});
		points.push_back({center.x + fx, center.y - fy});
		points.push_back({center.x - fx, center.y + fy});
		points.push_back({center.x - fx, center.y - fy});
		points.push_back({center.x + fy, center.y + fx});
		points.push_back({center.x + fy, center.y - fx});
		points.push_back({center.x - fy, center.y + fx});
		points.push_back({center.x - fy, center.y - fx});
		d += (d < 0) ? 4 * x + 6 : 4 * (x - y--) + 10;
		++x;
	}
	return points;
}

// Points d'ellipse (ou de cercle si radiusX==radiusY) ordonnés angulairement —
// requis pour l'éventail de triangles utilisé par les versions "fill".
// `rotationDeg` tourne l'ellipse autour de son centre (sens antihoraire).
[[nodiscard]] inline std::vector<FPoint> GenerateEllipsePoints(FPoint center, float radiusX, float radiusY,
															   float rotationDeg = 0.f) {
	std::vector<FPoint> points;
	if (radiusX <= 0.f || radiusY <= 0.f) {
		points.push_back(center);
		return points;
	}

	float maxRadius = sdl3::Max(radiusX, radiusY);
	int n = sdl3::Max(8, int(std::ceil(2.f * PI_F * maxRadius)));

	float rad = rotationDeg * (PI_F / 180.f);
	float cosA = std::cos(rad), sinA = std::sin(rad);
	float dTheta = 2.f * PI_F / float(n);

	points.reserve(size_t(n));
	for (int i = 0; i < n; ++i) {
		float theta = float(i) * dTheta;
		float cosT = std::cos(theta), sinT = std::sin(theta);
		points.push_back({center.x + radiusX * cosT * cosA - radiusY * sinT * sinA,
						  center.y + radiusX * cosT * sinA + radiusY * sinT * cosA});
	}
	return points;
}

// Points d'arc ordonnés angulairement (angles en degrés, convention SDL :
// 0° = droite, sens horaire croissant) — adaptés au triangle-fan des "fill".
[[nodiscard]] inline std::vector<FPoint> GenerateArcPoints(FPoint center, float radius, float startAngleDeg,
														   float endAngleDeg) {
	std::vector<FPoint> points;
	if (radius <= 0.f) {
		points.push_back(center);
		return points;
	}

	float span = endAngleDeg - startAngleDeg;
	while (span < 0.f)
		span += 360.f;
	while (span > 360.f)
		span -= 360.f;
	if (span == 0.f)
		span = 360.f;

	int n = sdl3::Max(4, int(std::ceil(PI_F * radius * span / 180.f)));

	float startRad = startAngleDeg * (PI_F / 180.f);
	float spanRad = span * (PI_F / 180.f);

	points.reserve(size_t(n) + 1);
	for (int i = 0; i <= n; ++i) {
		float t = float(i) / float(n);
		float ang = startRad + t * spanRad;
		points.push_back({center.x + radius * std::cos(ang), center.y + radius * std::sin(ang)});
	}
	return points;
}

} // namespace shapes

// ============================================================================
// Renderer
// ============================================================================

class Renderer : public Wrapper<SDL_Renderer, SDL_DestroyRenderer> {
public:
	using Wrapper::Wrapper;

	Renderer(WindowRef win, Option<StringView> name)
		: Renderer(SDL_CreateRenderer(win->Get(), name.IsSome() ? name->CStr() : nullptr)) {}

	[[nodiscard]] static Result<Renderer, Error> Create(Window &window, const char *driver = nullptr) {
		auto *r = SDL_CreateRenderer(window, driver);
		if (!r)
			return Err(GetError());
		return Ok(Renderer(r));
	}

	// ── Synchronisation verticale ────────────────────────────────────────────

	/// Mode de synchronisation verticale d'un `Renderer`.
	enum class VSync : int {
		DISABLED = SDL_RENDERER_VSYNC_DISABLED, ///< présente sans attendre le balayage
		EVERY_REFRESH = 1,                      ///< une image par rafraîchissement (défaut de SDL)
		EVERY_SECOND_REFRESH = 2,               ///< une image sur deux
		ADAPTIVE = SDL_RENDERER_VSYNC_ADAPTIVE, ///< adaptatif, si le pilote le gère
	};

	/// Règle la synchronisation verticale. AUCUN moyen de le faire n'existait
	/// dans ce wrapper : `Present()` bloquait donc systématiquement jusqu'au
	/// prochain balayage, ce qui plafonne la cadence à celle de l'écran et
	/// rend impossible toute mesure de performance au-delà (constaté en
	/// profilant examples/game_editor — la présentation pesait à elle seule
	/// plus de la moitié du temps d'image).
	///
	/// `false` si le pilote ne gère pas le mode demandé (certains n'acceptent
	/// que DISABLED et EVERY_REFRESH) — l'appelant peut alors se rabattre.
	bool SetVSync(VSync mode) { return m_handle && SDL_SetRenderVSync(m_handle, int(mode)); }

	/// Mode de synchronisation courant, `NONE` s'il n'est pas interrogeable.
	[[nodiscard]] Option<VSync> GetVSync() const {
		int mode = 0;
		if (!m_handle || !SDL_GetRenderVSync(m_handle, &mode))
			return NONE;
		return Some(VSync(mode));
	}

	[[nodiscard]] static Result<Renderer, StringView> CreateSoftware(Surface &surface) {
		auto *r = SDL_CreateSoftwareRenderer(surface);
		if (!r)
			return Err(GetError());
		return Ok(Renderer(r));
	}

	// ── GPU Device ───────────────────────────────────────────────────────────
	/// Le SDL_GPUDevice utilisé par ce Renderer, si (et seulement si) il a été
	/// créé avec le backend "gpu" de SDL3 (`SDL_CreateGPURenderer`/
	/// `SDL_GetGPURendererDevice`) — `Err` sinon (renderer classique
	/// opengl/vulkan/metal/direct3d, cas par défaut de `Renderer::Create`
	/// dans ce dépôt). Corps défini dans gpu.hpp (a besoin du type complet de
	/// GpuDevice, cf. la déclaration avancée en tête de ce fichier).
	///
	/// @warning Le GpuDevice retourné est EMPRUNTÉ (ce Renderer reste
	/// propriétaire du device SDL_GPU sous-jacent) alors que `sdl3::GpuDevice`
	/// est normalement un wrapper POSSESSEUR (son destructeur appelle
	/// SDL_DestroyGPUDevice) — ne JAMAIS laisser la valeur retournée ici
	/// atteindre son propre destructeur pendant que ce Renderer est encore en
	/// vie (double-free sinon). `render3d::Canvas::Create(sdl3::Renderer&, ...)`
	/// (canvas.hpp) est le seul consommateur prévu : il neutralise
	/// explicitement la destruction du device dans son propre destructeur
	/// (cf. `Canvas::m_ownsDevice`) précisément pour ce cas.
	[[nodiscard]] Result<GpuDevice, StringView> GetGPUDevice() const;

	// ── Getters ──────────────────────────────────────────────────────────────

	Point GetOutputSize() const {
		int x, y;
		SDL_GetRenderOutputSize(m_handle, &x, &y);
		return Point{x, y};
	}

	int GetOutputWidth() const {
		return GetOutputSize().x;
	}

	int GetOutputHeight() const {
		return GetOutputSize().y;
	}

	// ── Draw state ───────────────────────────────────────────────────────────
	bool SetDrawColor(Color c) { return (!m_handle) ? false : SDL_SetRenderDrawColor(m_handle, c.r, c.g, c.b, c.a); }
	bool SetDrawColor(FColor c) {
		return (!m_handle) ? false : SDL_SetRenderDrawColorFloat(m_handle, c.r, c.g, c.b, c.a);
	}

	[[nodiscard]] Color GetDrawColor() const {
		Color c{};
		if (m_handle)
			SDL_GetRenderDrawColor(m_handle, &c.r, &c.g, &c.b, &c.a);
		return c;
	}
	[[nodiscard]] FColor GetDrawColorFloat() const {
		FColor c{};
		if (m_handle)
			SDL_GetRenderDrawColorFloat(m_handle, &c.r, &c.g, &c.b, &c.a);
		return c;
	}

	bool SetBlendMode(BlendMode mode) {
		return m_handle && SDL_SetRenderDrawBlendMode(m_handle, SDL_BlendMode(int(mode)));
	}
	bool SetClipRect(const Rect &r) {
		SDL_Rect sr = r;
		return m_handle && SDL_SetRenderClipRect(m_handle, &sr);
	}
	bool SetClipRect(const FRect &r) {
		SDL_Rect sr = SDL_Rect(r);
		return m_handle && SDL_SetRenderClipRect(m_handle, &sr);
	}
	bool ClearClipRect() { return m_handle && SDL_SetRenderClipRect(m_handle, nullptr); }
	bool SetScale(float sx, float sy) { return m_handle && SDL_SetRenderScale(m_handle, sx, sy); }
	bool SetViewport(const Rect &r) {
		SDL_Rect sr = r;
		return m_handle && SDL_SetRenderViewport(m_handle, &sr);
	}
	bool SetViewport(const FRect &r) {
		SDL_Rect sr = SDL_Rect(r);
		return m_handle && SDL_SetRenderViewport(m_handle, &sr);
	}
	bool ClearViewport() { return m_handle && SDL_SetRenderViewport(m_handle, nullptr); }

	// ── Clear & present ──────────────────────────────────────────────────────

	bool Clear() { return (!m_handle) ? false : SDL_RenderClear(m_handle); }
	bool Present() { return (!m_handle) ? false : SDL_RenderPresent(m_handle); }

	// ── Primitives ───────────────────────────────────────────────────────────

	bool DrawPoint(float x, float y) { return (!m_handle) ? false : SDL_RenderPoint(m_handle, x, y); }
	bool DrawPoint(FPoint p) { return DrawPoint(p.x, p.y); }

	bool DrawPoints(const FPoint *pts, int count) { return m_handle && SDL_RenderPoints(m_handle, (SDL_FPoint*)pts, count); }
	bool DrawPoints(std::span<const FPoint> pts) {
		return m_handle && SDL_RenderPoints(m_handle, (SDL_FPoint*)(pts.data()), int(pts.size()));
	}

	bool DrawLine(float x1, float y1, float x2, float y2) {
		return m_handle && SDL_RenderLine(m_handle, x1, y1, x2, y2);
	}
	bool DrawLine(FPoint a, FPoint b) { return DrawLine(a.x, a.y, b.x, b.y); }

	bool DrawLines(const FPoint *pts, int count) { return m_handle && SDL_RenderLines(m_handle, (SDL_FPoint*)pts, count); }
	bool DrawLines(std::span<const FPoint> pts) {
		return m_handle && SDL_RenderLines(m_handle, (SDL_FPoint*)(pts.data()), int(pts.size()));
	}

	bool DrawRect(const FRect &r) {
		SDL_FRect sr = r;
		return m_handle && SDL_RenderRect(m_handle, &sr);
	}
	bool FillRect(const FRect &r) {
		SDL_FRect sr = r;
		return m_handle && SDL_RenderFillRect(m_handle, &sr);
	}

	bool DrawRects(const FRect *rects, int count) { return m_handle && SDL_RenderRects(m_handle, (SDL_FRect*)rects, count); }
	bool DrawRects(std::span<const FRect> rects) {
		return m_handle && SDL_RenderRects(m_handle, (SDL_FRect*)(rects.data()), int(rects.size()));
	}
	bool FillRects(const FRect *rects, int count) {
		return m_handle && SDL_RenderFillRects(m_handle, (SDL_FRect*)rects, count);
	}
	bool FillRects(std::span<const FRect> rects) {
		return m_handle && SDL_RenderFillRects(m_handle, (SDL_FRect*)(rects.data()), int(rects.size()));
	}

	// ── Texture rendering ────────────────────────────────────────────────────

	bool Render(const Texture &tex, float x, float y) {
		float w, h;
		SDL_GetTextureSize(tex.Get(), &w, &h);
		SDL_FRect d{x, y, w, h};
		return m_handle && SDL_RenderTexture(m_handle, tex.Get(), nullptr, &d);
	}
	bool Render(const Texture &tex, const FRect &dst) {
		SDL_FRect d = dst;
		return m_handle && SDL_RenderTexture(m_handle, tex.Get(), nullptr, &d);
	}
	bool Render(const Texture &tex, const FRect &src, const FRect &dst) {
		SDL_FRect s = src, d = dst;
		return m_handle && SDL_RenderTexture(m_handle, tex.Get(), &s, &d);
	}
	bool RenderRotated(const Texture &tex, const FRect &dst, double angleDeg, FPoint center = {0.f, 0.f},
					   FlipMode flip = FlipMode::NONE) {
		SDL_FRect d = dst;
		SDL_FPoint c = center;
		return m_handle &&
			   SDL_RenderTextureRotated(m_handle, tex.Get(), nullptr, &d, angleDeg, &c, SDL_FlipMode(int(flip)));
	}

	// ── Geometry (triangles) ─────────────────────────────────────────────────

	bool RenderGeometry(std::span<const Vertex> vertices, std::span<const int> indices = {}) {
		return m_handle && SDL_RenderGeometry(m_handle, nullptr, (SDL_Vertex*)(vertices.data()), int(vertices.size()),
											  indices.empty() ? nullptr : indices.data(), int(indices.size()));
	}
	bool RenderGeometry(const Texture &tex, std::span<const Vertex> vertices, std::span<const int> indices = {}) {
		return m_handle && SDL_RenderGeometry(m_handle, tex.Get(), (SDL_Vertex*)(vertices.data()), int(vertices.size()),
											  indices.empty() ? nullptr : indices.data(), int(indices.size()));
	}

	// Remplit un polygone (convexe, ou concave "raisonnable") défini par des
	// points de contour ordonnés angulairement, via un éventail de triangles
	// centré sur le centroïde — teinté avec la couleur de dessin courante.
	bool RenderGeometryFromPoints(std::span<const FPoint> points) {
		if (points.size() < 3)
			return false;
		FColor color = GetDrawColorFloat();

		float cx = 0.f, cy = 0.f;
		for (auto &p : points) {
			cx += p.x;
			cy += p.y;
		}
		cx /= float(points.size());
		cy /= float(points.size());

		std::vector<Vertex> verts;
		verts.reserve(points.size() * 3);
		Vertex centerV{{cx, cy}, color, {0.f, 0.f}};
		for (size_t i = 0; i < points.size(); ++i) {
			size_t j = (i + 1) % points.size();
			verts.push_back(centerV);
			verts.push_back({{points[i].x, points[i].y}, color, {0.f, 0.f}});
			verts.push_back({{points[j].x, points[j].y}, color, {0.f, 0.f}});
		}
		return RenderGeometry(verts);
	}

	// ── Formes complexes ─────────────────────────────────────────────────────

	bool DrawCircle(FPoint center, float radius) { return DrawPoints(shapes::GenerateCirclePoints(center, radius)); }
	bool FillCircle(FPoint center, float radius) {
		return RenderGeometryFromPoints(shapes::GenerateEllipsePoints(center, radius, radius, 0.f));
	}

	bool DrawEllipse(FPoint center, float radiusX, float radiusY, float rotationDeg = 0.f) {
		return DrawPoints(shapes::GenerateEllipsePoints(center, radiusX, radiusY, rotationDeg));
	}
	bool FillEllipse(FPoint center, float radiusX, float radiusY, float rotationDeg = 0.f) {
		return RenderGeometryFromPoints(shapes::GenerateEllipsePoints(center, radiusX, radiusY, rotationDeg));
	}

	bool DrawArc(FPoint center, float radius, float startAngleDeg, float endAngleDeg) {
		return DrawLines(shapes::GenerateArcPoints(center, radius, startAngleDeg, endAngleDeg));
	}

	// Contour d'une part de camembert : arc + deux rayons vers le centre.
	bool DrawPie(FPoint center, float radius, float startAngleDeg, float endAngleDeg) {
		auto arc = shapes::GenerateArcPoints(center, radius, startAngleDeg, endAngleDeg);
		bool ok = DrawLines(arc);
		if (!arc.empty()) {
			ok = DrawLine(center, arc.front()) && ok;
			ok = DrawLine(center, arc.back()) && ok;
		}
		return ok;
	}

	// Contour d'un rectangle à coins arrondis (rayons indépendants par coin).
	/// Rayons bornés à la moitié du plus petit côté : au-delà, les quarts de
	/// disque d'angle se chevauchent et la bande centrale prend une largeur
	/// négative — un carré de 8 px au rayon 8 se dessinait en « × ».
	[[nodiscard]] static Corners ClampCorners(const FRect &rect, const Corners &c) noexcept {
		const float limit = sdl3::Max(0.f, sdl3::Min(rect.w, rect.h) * 0.5f);
		return Corners{sdl3::Clamp(c.tl, 0.f, limit), sdl3::Clamp(c.tr, 0.f, limit), sdl3::Clamp(c.bl, 0.f, limit),
					   sdl3::Clamp(c.br, 0.f, limit)};
	}

	/// Comme SDL_RenderRect, le trait reste À L'INTÉRIEUR du rectangle : les
	/// bords droit/bas passent par les pixels x+w-1 et y+h-1 (et non x+w/y+h,
	/// hors de la boîte — donc rognés dès qu'un clip la borne).
	bool DrawRoundedRect(const FRect &outer, const Corners &corners) {
		if (outer.w <= 0.f || outer.h <= 0.f)
			return true;
		const FRect rect{outer.x, outer.y, Max(0.f, outer.w - 1.f), Max(0.f, outer.h - 1.f)};
		const Corners c = ClampCorners(rect, corners);
		bool ok = true;
		ok = DrawArc({rect.x + c.tl, rect.y + c.tl}, c.tl, 180.f, 270.f) && ok;
		ok = DrawArc({rect.x + rect.w - c.tr, rect.y + c.tr}, c.tr, 270.f, 360.f) && ok;
		ok = DrawArc({rect.x + rect.w - c.br, rect.y + rect.h - c.br}, c.br, 0.f, 90.f) && ok;
		ok = DrawArc({rect.x + c.bl, rect.y + rect.h - c.bl}, c.bl, 90.f, 180.f) && ok;
		ok = DrawLine({rect.x + c.tl, rect.y}, {rect.x + rect.w - c.tr, rect.y}) && ok;
		ok = DrawLine({rect.x + rect.w, rect.y + c.tr}, {rect.x + rect.w, rect.y + rect.h - c.br}) && ok;
		ok = DrawLine({rect.x + rect.w - c.br, rect.y + rect.h}, {rect.x + c.bl, rect.y + rect.h}) && ok;
		ok = DrawLine({rect.x, rect.y + c.bl}, {rect.x, rect.y + rect.h - c.bl}) && ok;
		return ok;
	}

	// Rectangle à coins arrondis rempli (4 secteurs de coin + 3 bandes rectangulaires).
	bool FillRoundedRect(const FRect &rect, const Corners &corners) {
		const Corners c = ClampCorners(rect, corners);
		FColor color = GetDrawColorFloat();
		bool ok = true;

		auto fillSector = [&](FPoint arcCenter, float r, float startDeg, float endDeg) {
			if (r <= 0.f)
				return true;
			int n = sdl3::Max(4, int(std::ceil(PI_F * r * 0.5f)));
			float startRad = startDeg * (PI_F / 180.f), endRad = endDeg * (PI_F / 180.f);
			Vertex cv{{arcCenter.x, arcCenter.y}, color, {0.f, 0.f}};
			std::vector<Vertex> verts;
			verts.reserve(size_t(n) * 3);
			for (int i = 0; i < n; ++i) {
				float t0 = float(i) / float(n), t1 = float(i + 1) / float(n);
				float a0 = Lerp(startRad, endRad, t0), a1 = Lerp(startRad, endRad, t1);
				verts.push_back(cv);
				verts.push_back({{arcCenter.x + r * std::cos(a0), arcCenter.y + r * std::sin(a0)}, color, {0.f, 0.f}});
				verts.push_back({{arcCenter.x + r * std::cos(a1), arcCenter.y + r * std::sin(a1)}, color, {0.f, 0.f}});
			}
			return RenderGeometry(verts);
		};

		if (c.tl > 0.f)
			ok = fillSector({rect.x + c.tl, rect.y + c.tl}, c.tl, 180.f, 270.f) && ok;
		if (c.tr > 0.f)
			ok = fillSector({rect.x + rect.w - c.tr, rect.y + c.tr}, c.tr, 270.f, 360.f) && ok;
		if (c.br > 0.f)
			ok = fillSector({rect.x + rect.w - c.br, rect.y + rect.h - c.br}, c.br, 0.f, 90.f) && ok;
		if (c.bl > 0.f)
			ok = fillSector({rect.x + c.bl, rect.y + rect.h - c.bl}, c.bl, 90.f, 180.f) && ok;

		float innerLeft = rect.x + sdl3::Max(c.tl, c.bl);
		float innerRight = rect.x + rect.w - sdl3::Max(c.tr, c.br);
		if (innerRight > innerLeft)
			ok = FillRect({innerLeft, rect.y, innerRight - innerLeft, rect.h}) && ok;

		float leftW = sdl3::Max(c.tl, c.bl), leftH = rect.h - c.tl - c.bl;
		if (leftW > 0.f && leftH > 0.f)
			ok = FillRect({rect.x, rect.y + c.tl, leftW, leftH}) && ok;

		float rightW = sdl3::Max(c.tr, c.br), rightH = rect.h - c.tr - c.br;
		if (rightW > 0.f && rightH > 0.f)
			ok = FillRect({rect.x + rect.w - rightW, rect.y + c.tr, rightW, rightH}) && ok;

		return ok;
	}

	// Bordure (anneau) d'un rectangle à coins arrondis, remplie entre le
	// contour `rect` (intérieur) et `rect` étendu de `border` (extérieur).
	bool DrawRoundedBorderedRect(const FRect &rect, const Sides &border, const Corners &c) {
		FColor color = GetDrawColorFloat();
		bool ok = true;

		auto quad = [&](FPoint a0, FPoint a1, FPoint b0, FPoint b1) {
			Vertex va0{{a0.x, a0.y}, color, {}}, va1{{a1.x, a1.y}, color, {}};
			Vertex vb0{{b0.x, b0.y}, color, {}}, vb1{{b1.x, b1.y}, color, {}};
			std::array<Vertex, 6> v{va0, va1, vb0, va1, vb1, vb0};
			return RenderGeometry(v);
		};

		auto arcAnnulus = [&](FPoint center, float innerR, float outerR, float startDeg, float endDeg) {
			if (outerR <= 0.f)
				return true;
			float clampedInner = sdl3::Max(0.f, innerR);
			int n = sdl3::Max(4, int(std::ceil(PI_F * outerR * 0.5f)));
			float sRad = startDeg * (PI_F / 180.f), eRad = endDeg * (PI_F / 180.f);
			std::vector<Vertex> verts;
			verts.reserve(size_t(n) * 6);
			for (int i = 0; i < n; ++i) {
				float t0 = float(i) / float(n), t1 = float(i + 1) / float(n);
				float a0 = Lerp(sRad, eRad, t0), a1 = Lerp(sRad, eRad, t1);
				float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
				Vertex i0{{center.x + clampedInner * c0, center.y + clampedInner * s0}, color, {}};
				Vertex i1{{center.x + clampedInner * c1, center.y + clampedInner * s1}, color, {}};
				Vertex o0{{center.x + outerR * c0, center.y + outerR * s0}, color, {}};
				Vertex o1{{center.x + outerR * c1, center.y + outerR * s1}, color, {}};
				verts.insert(verts.end(), {i0, o0, i1, i1, o0, o1});
			}
			return RenderGeometry(verts);
		};

		if (float x0 = rect.x + c.tl, x1 = rect.x + rect.w - c.tr; x1 > x0)
			ok = quad({x0, rect.y}, {x1, rect.y}, {x0, rect.y - border.top}, {x1, rect.y - border.top}) && ok;
		if (float x0 = rect.x + c.bl, x1 = rect.x + rect.w - c.br; x1 > x0)
			ok = quad({x0, rect.y + rect.h}, {x1, rect.y + rect.h}, {x0, rect.y + rect.h + border.bottom},
					  {x1, rect.y + rect.h + border.bottom}) &&
				 ok;
		if (float y0 = rect.y + c.tl, y1 = rect.y + rect.h - c.bl; y1 > y0)
			ok = quad({rect.x, y0}, {rect.x, y1}, {rect.x - border.left, y0}, {rect.x - border.left, y1}) && ok;
		if (float y0 = rect.y + c.tr, y1 = rect.y + rect.h - c.br; y1 > y0)
			ok = quad({rect.x + rect.w, y0}, {rect.x + rect.w, y1}, {rect.x + rect.w + border.right, y0},
					  {rect.x + rect.w + border.right, y1}) &&
				 ok;

		ok = arcAnnulus({rect.x + c.tl, rect.y + c.tl}, c.tl, c.tl + sdl3::Max(border.left, border.top), 180.f, 270.f) &&
			 ok;
		ok = arcAnnulus({rect.x + rect.w - c.tr, rect.y + c.tr}, c.tr, c.tr + sdl3::Max(border.right, border.top), 270.f,
						360.f) &&
			 ok;
		ok = arcAnnulus({rect.x + rect.w - c.br, rect.y + rect.h - c.br}, c.br,
						c.br + sdl3::Max(border.right, border.bottom), 0.f, 90.f) &&
			 ok;
		ok = arcAnnulus({rect.x + c.bl, rect.y + rect.h - c.bl}, c.bl, c.bl + sdl3::Max(border.left, border.bottom),
						90.f, 180.f) &&
			 ok;
		return ok;
	}

	// Contour d'un polygone fermé (renvoie au premier point).
	bool DrawPolygon(std::span<const FPoint> points) {
		if (points.size() < 2)
			return false;
		std::vector<FPoint> closed(points.begin(), points.end());
		closed.push_back(closed.front());
		return DrawLines(closed);
	}

	// Polygone rempli (convexe, ou concave "raisonnable") via éventail de triangles.
	bool FillPolygon(std::span<const FPoint> points) {
		if (points.size() < 3)
			return false;
		return RenderGeometryFromPoints(points);
	}

	// Courbe de Bézier (degré arbitraire, via de Casteljau) approximée par des
	// segments — `step` dans (0, 1], plus petit = plus de segments = plus lisse.
	bool DrawBezier(std::span<const FPoint> controlPoints, float step = 0.05f) {
		if (controlPoints.size() < 2)
			return false;
		step = Clamp(step, 0.001f, 1.0f);

		size_t n = controlPoints.size();
		std::vector<float> wx(n), wy(n);
		int numSamples = int(std::ceil(1.f / step)) + 1;

		std::vector<FPoint> curve;
		curve.reserve(size_t(numSamples));
		for (int s = 0; s < numSamples; ++s) {
			float t = (s == numSamples - 1) ? 1.f : float(s) * step;
			for (size_t i = 0; i < n; ++i) {
				wx[i] = controlPoints[i].x;
				wy[i] = controlPoints[i].y;
			}
			for (size_t r = 1; r < n; ++r) {
				for (size_t i = 0; i + r < n; ++i) {
					wx[i] = Lerp(wx[i], wx[i + 1], t);
					wy[i] = Lerp(wy[i], wy[i + 1], t);
				}
			}
			curve.push_back({wx[0], wy[0]});
		}
		return DrawLines(curve);
	}

	// ── Texture creation ─────────────────────────────────────────────────────

	[[nodiscard]] Result<Texture, StringView> CreateTexture(PixelFormat fmt, TextureAccess access, int w, int h) {
		auto *t = SDL_CreateTexture(m_handle, detail::ToSDL(fmt), detail::ToSDL(access), w, h);
		if (!t) return Err(GetError());
		return Ok(Texture(t));
	}

	[[nodiscard]] Result<Texture, StringView> CreateTextureFromSurface(const Surface &surf) {
		auto *t = SDL_CreateTextureFromSurface(m_handle, surf.Get());
		if (!t) return Err(GetError());
		return Ok(Texture(t));
	}

	// ── Render target ────────────────────────────────────────────────────────

	bool SetTarget(Texture &tex) { return m_handle && SDL_SetRenderTarget(m_handle, tex.Get()); }
	bool ResetTarget() { return m_handle && SDL_SetRenderTarget(m_handle, nullptr); }

	// ── Pixel readback ───────────────────────────────────────────────────────

	/// Lit les pixels de la cible de rendu COURANTE (texture posée via
	/// SetTarget, ou le backbuffer sinon) dans une nouvelle Surface CPU —
	/// SDL_RenderReadPixels. `rect` NONE = tout le viewport courant (attention
	/// si un viewport décalé/agrandi est actif via SetViewport : la lecture
	/// suit CE viewport, pas forcément [0,largeur)x[0,hauteur) de la cible —
	/// ClearViewport() avant l'appel si un rectangle pleine-cible exact est
	/// voulu, cf. usage dans ui/shader_effect.hpp).
	///
	/// Format des pixels retournés NON garanti par SDL (dépend du backend/de
	/// la cible) : appeler Surface::Convert(PixelFormat::RGBA32) — PAS
	/// RGBA8888 — avant tout accès aux octets si un ordre d'octets précis
	/// [R,G,B,A] tightly-packed est nécessaire.
	///
	/// @warning Piège réel rencontré (cf. rapport de tâche M23) :
	/// `PixelFormat::RGBA8888` (SDL_PIXELFORMAT_RGBA8888) est un format
	/// PACKED — son ordre en MÉMOIRE sur little-endian est l'INVERSE de son
	/// nom ([A,B,G,R], pas [R,G,B,A]), un pixel converti puis lu octet-par-
	/// octet "à la main" en RGBA8888 apparaît donc CHANNEL-SWAPPED. SEUL
	/// `PixelFormat::RGBA32` garantit l'ordre littéral [R,G,B,A] en mémoire
	/// quelle que soit l'endianness (cf. doc SDL_pixels.h : RGBA32 est un
	/// alias PLATEFORME-DÉPENDANT — ABGR8888 sur little-endian, RGBA8888 sur
	/// big-endian — précisément pour donner cette garantie). Confirmé
	/// empiriquement round-trip complet (upload littéral -> sdl3::Texture ->
	/// composite -> ReadPixels -> Convert) : RGBA8888 aux DEUX bouts corrompt
	/// silencieusement les couleurs (une texture taguée RGBA8888 puis nourrie
	/// d'octets déjà en ordre littéral [R,G,B,A] s'affiche comme totalement
	/// transparente, le premier octet — R — étant relu comme alpha) ; RGBA32
	/// aux deux bouts round-trippe exactement. Même discipline que le reste
	/// du round-trip GPU<->CPU de ce dépôt (offscreen.hpp) : ne jamais deviner
	/// un layout, toujours le vérifier par pixel-readback réel — cf. ui/
	/// shader_effect.hpp::ShaderEffectSystem pour le consommateur qui a
	/// débusqué ce piège. Primitive ReadPixels elle-même AJOUTÉE pour M23
	/// (ui::ShaderEffectSystem) : aucun wrapper de SDL_RenderReadPixels
	/// n'existait auparavant dans ce dépôt.
	///
	/// @warning Lente (lecture GPU->CPU synchrone, avertissement officiel
	/// SDL) — pas destinée à un usage par frame sur une grande cible en
	/// dehors des tests/outils et des effets shader de widget (déjà coûteux
	/// par nature, cf. shader_effect.hpp).
	[[nodiscard]] Result<Surface, StringView> ReadPixels(Option<Rect> rect = NONE) const {
		if (!m_handle)
			return Err(StringView("Renderer::ReadPixels: null renderer"));
		SDL_Rect sr{};
		const SDL_Rect *rectPtr = nullptr;
		if (rect.IsSome()) {
			sr = SDL_Rect(rect.Unwrap());
			rectPtr = &sr;
		}
		auto *s = SDL_RenderReadPixels(m_handle, rectPtr);
		if (!s)
			return Err(GetError());
		return Ok(Surface(s));
	}

	// ── Output info ──────────────────────────────────────────────────────────

	[[nodiscard]] Point OutputSize() const {
		int w = 0, h = 0;
		if (m_handle)
			SDL_GetRenderOutputSize(m_handle, &w, &h);
		return {w, h};
	}
};

// Texture factory methods that need complete Renderer type
inline Result<Texture, StringView> Texture::Create(Renderer &ren, int w, int h, PixelFormat fmt) {
	auto *t = SDL_CreateTexture(ren.Get(), detail::ToSDL(fmt), SDL_TEXTUREACCESS_STATIC, w, h);
	if (!t)
		return Err(GetError());
	return Ok(Texture(t));
}
inline Result<Texture, StringView> Texture::CreateFromSurface(Renderer &ren, const Surface &surf) {
	auto *t = SDL_CreateTextureFromSurface(ren.Get(), surf.Get());
	if (!t)
		return Err(GetError());
	return Ok(Texture(t));
}
inline Result<Texture, StringView> Texture::CreateFromFile(Renderer &ren, const String &path) {
	auto *t = IMG_LoadTexture(ren.Get(), path.c_str());
	if (!t)
		return Err(GetError());
	return Ok(Texture(t));
}

} // namespace sdl3
