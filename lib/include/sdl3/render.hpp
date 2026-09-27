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

	[[nodiscard]] static Result<Surface, Error> Create(int w, int h, PixelFormat fmt = PixelFormat::RGBA8888);

	/// Surface POSSÉDANT ses pixels, remplie par copie de `pixels` (`pitch`
	/// octets par ligne, disposés selon `fmt`). Copie plutôt que
	/// SDL_CreateSurfaceFrom (qui emprunte le tampon) : la surface peut alors
	/// survivre au tampon source — typiquement un framebuffer d'émulateur ou
	/// de rendu logiciel écrit ensuite en PNG (cf. ImgSavePng). Ajouté pour
	/// examples/emulator_demo (captures du framebuffer émulé en mode sans
	/// écran) : le wrapper savait charger et écrire une surface, mais pas en
	/// construire une depuis des octets bruts.
	[[nodiscard]] static Result<Surface, StringView> CreateFromPixels(int w, int h, PixelFormat fmt, const void *pixels,
																	  int pitch);

	[[nodiscard]] static Result<Surface, StringView> CreateFromFile(const String &path);

	[[nodiscard]] Result<Surface, StringView> Convert(PixelFormat fmt) const;

	[[nodiscard]] int GetWidth() const { return m_handle ? m_handle->w : 0; }
	[[nodiscard]] int GetHeight() const { return m_handle ? m_handle->h : 0; }
	[[nodiscard]] Point GetSize() const { return {GetWidth(), GetHeight()}; }

	bool Fill(Color c);

	bool Blit(const Surface &src, Point dest);

	bool SetColorKey(bool enable, Color c);

	bool SetAlphaMod(uint8_t alpha) { return m_handle && SDL_SetSurfaceAlphaMod(m_handle, alpha); }
	bool SetBlendMode(BlendMode mode);
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
														   SDL_WindowFlags flags = 0);

	[[nodiscard]] static Result<Window, Error> Create(const String &title, std::span<const int> size,
														   SDL_WindowFlags flags = 0);

	[[nodiscard]] static Result<Window, Error> Create(const String &title, std::span<const int> pos,
														   std::span<const int> size, SDL_WindowFlags flags = 0);

	// ── Getters ──────────────────────────────────────────────────────────────

	[[nodiscard]] int GetWidth() const;
	[[nodiscard]] int GetHeight() const;
	[[nodiscard]] Point GetSize() const;
	[[nodiscard]] int GetX() const;
	[[nodiscard]] int GetY() const;
	[[nodiscard]] Point GetPosition() const;
	[[nodiscard]] bool IsFullscreen() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_FULLSCREEN; }
	[[nodiscard]] bool IsResizable() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_RESIZABLE; }
	[[nodiscard]] bool IsBordered() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_BORDERLESS; }
	[[nodiscard]] bool IsVisible() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_HIDDEN; }
	[[nodiscard]] bool HasInputFocus() const;
	[[nodiscard]] bool HasMouseFocus() const;
	[[nodiscard]] bool IsMinimized() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_MINIMIZED; }
	[[nodiscard]] bool IsMaximized() const { return m_handle && SDL_GetWindowFlags(m_handle) & SDL_WINDOW_MAXIMIZED; }
	[[nodiscard]] SDL_WindowID GetId() const { return m_handle ? SDL_GetWindowID(m_handle) : 0; }
	[[nodiscard]] String Title() const;

	// ── Setters ──────────────────────────────────────────────────────────────

	void SetTitle(const String &t);
	void SetSize(int w, int h);
	void SetPosition(int x, int y);
	void SetFullscreen(bool on);
	void SetResizable(bool on);
	void SetBordered(bool on);
	void SetOpacity(float opacity);
	/// Active la saisie de texte (événements TEXT_INPUT ; affiche le clavier
	/// virtuel sur les plateformes qui en ont un).
	void StartTextInput();
	void StopTextInput();
	[[nodiscard]] bool TextInputActive() const { return m_handle && SDL_TextInputActive(m_handle); }

	void Show();
	void Hide();
	void Raise();
	void Maximize();
	void Minimize();
	void Restore();

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
	/// Souris « confinée » : curseur visible mais retenu dans la fenêtre.
	bool SetMouseGrab(bool enabled) { return m_handle && SDL_SetWindowMouseGrab(m_handle, enabled); }
	[[nodiscard]] bool MouseGrab() const { return m_handle && SDL_GetWindowMouseGrab(m_handle); }

	bool SetHitTest(std::function<SDL_HitTestResult(const SDL_Point &)> fn);

private:
	std::unique_ptr<std::function<SDL_HitTestResult(const SDL_Point &)>> hitTest;

	static SDL_HitTestResult SDLCALL HitTestTrampoline(SDL_Window *, const SDL_Point *area, void *data);
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

	[[nodiscard]] FPoint GetSize() const;
	[[nodiscard]] int GetWidth() const { return int(GetSize().x); }
	[[nodiscard]] int GetHeight() const { return int(GetSize().y); }

	bool SetColorMod(uint8_t r, uint8_t g, uint8_t b) { return m_handle && SDL_SetTextureColorMod(m_handle, r, g, b); }
	bool SetColorMod(Color c) { return SetColorMod(c.r, c.g, c.b); }
	bool SetAlphaMod(uint8_t a) { return m_handle && SDL_SetTextureAlphaMod(m_handle, a); }
	bool SetBlendMode(BlendMode mode);
	bool SetScaleMode(SDL_ScaleMode mode) { return m_handle && SDL_SetTextureScaleMode(m_handle, mode); }

	/// Replaces the full texture contents (or just `rect` if given) from a raw
	/// pixel buffer laid out per the texture's own format, `pitch` bytes per row.
	bool Update(const void *pixels, int pitch, Option<Rect> rect = NONE);
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
[[nodiscard]] std::vector<FPoint> GenerateCirclePoints(FPoint center, float radius);

// Points d'ellipse (ou de cercle si radiusX==radiusY) ordonnés angulairement —
// requis pour l'éventail de triangles utilisé par les versions "fill".
// `rotationDeg` tourne l'ellipse autour de son centre (sens antihoraire).
[[nodiscard]] std::vector<FPoint> GenerateEllipsePoints(FPoint center, float radiusX, float radiusY,
															   float rotationDeg = 0.f);

// Points d'arc ordonnés angulairement (angles en degrés, convention SDL :
// 0° = droite, sens horaire croissant) — adaptés au triangle-fan des "fill".
[[nodiscard]] std::vector<FPoint> GenerateArcPoints(FPoint center, float radius, float startAngleDeg,
														   float endAngleDeg);

} // namespace shapes

// ============================================================================
// Renderer
// ============================================================================

class Renderer : public Wrapper<SDL_Renderer, SDL_DestroyRenderer> {
public:
	using Wrapper::Wrapper;

	Renderer(WindowRef win, Option<StringView> name)
		: Renderer(SDL_CreateRenderer(win->Get(), name.IsSome() ? name->CStr() : nullptr)) {}

	[[nodiscard]] static Result<Renderer, Error> Create(Window &window, const char *driver = nullptr);

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
	[[nodiscard]] Option<VSync> GetVSync() const;

	[[nodiscard]] static Result<Renderer, StringView> CreateSoftware(Surface &surface);

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

	Point GetOutputSize() const;

	int GetOutputWidth() const;

	int GetOutputHeight() const;

	// ── Draw state ───────────────────────────────────────────────────────────
	bool SetDrawColor(Color c) { return (!m_handle) ? false : SDL_SetRenderDrawColor(m_handle, c.r, c.g, c.b, c.a); }
	bool SetDrawColor(FColor c);

	[[nodiscard]] Color GetDrawColor() const;
	[[nodiscard]] FColor GetDrawColorFloat() const;

	bool SetBlendMode(BlendMode mode);
	bool SetClipRect(const Rect &r);
	bool SetClipRect(const FRect &r);
	bool ClearClipRect() { return m_handle && SDL_SetRenderClipRect(m_handle, nullptr); }
	bool SetScale(float sx, float sy) { return m_handle && SDL_SetRenderScale(m_handle, sx, sy); }
	bool SetViewport(const Rect &r);
	bool SetViewport(const FRect &r);
	bool ClearViewport() { return m_handle && SDL_SetRenderViewport(m_handle, nullptr); }

	// ── Clear & present ──────────────────────────────────────────────────────

	bool Clear() { return (!m_handle) ? false : SDL_RenderClear(m_handle); }
	bool Present() { return (!m_handle) ? false : SDL_RenderPresent(m_handle); }

	// ── Primitives ───────────────────────────────────────────────────────────

	bool DrawPoint(float x, float y) { return (!m_handle) ? false : SDL_RenderPoint(m_handle, x, y); }
	bool DrawPoint(FPoint p) { return DrawPoint(p.x, p.y); }

	bool DrawPoints(const FPoint *pts, int count) { return m_handle && SDL_RenderPoints(m_handle, (SDL_FPoint*)pts, count); }
	bool DrawPoints(std::span<const FPoint> pts);

	bool DrawLine(float x1, float y1, float x2, float y2);
	bool DrawLine(FPoint a, FPoint b) { return DrawLine(a.x, a.y, b.x, b.y); }

	bool DrawLines(const FPoint *pts, int count) { return m_handle && SDL_RenderLines(m_handle, (SDL_FPoint*)pts, count); }
	bool DrawLines(std::span<const FPoint> pts);

	bool DrawRect(const FRect &r);
	bool FillRect(const FRect &r);

	bool DrawRects(const FRect *rects, int count) { return m_handle && SDL_RenderRects(m_handle, (SDL_FRect*)rects, count); }
	bool DrawRects(std::span<const FRect> rects);
	bool FillRects(const FRect *rects, int count);
	bool FillRects(std::span<const FRect> rects);

	// ── Texture rendering ────────────────────────────────────────────────────

	bool Render(const Texture &tex, float x, float y);
	bool Render(const Texture &tex, const FRect &dst);
	bool Render(const Texture &tex, const FRect &src, const FRect &dst);
	bool RenderRotated(const Texture &tex, const FRect &dst, double angleDeg, FPoint center = {0.f, 0.f},
					   FlipMode flip = FlipMode::NONE);

	// ── Geometry (triangles) ─────────────────────────────────────────────────

	bool RenderGeometry(std::span<const Vertex> vertices, std::span<const int> indices = {});
	bool RenderGeometry(const Texture &tex, std::span<const Vertex> vertices, std::span<const int> indices = {});

	// Remplit un polygone (convexe, ou concave "raisonnable") défini par des
	// points de contour ordonnés angulairement, via un éventail de triangles
	// centré sur le centroïde — teinté avec la couleur de dessin courante.
	bool RenderGeometryFromPoints(std::span<const FPoint> points);

	// ── Formes complexes ─────────────────────────────────────────────────────

	bool DrawCircle(FPoint center, float radius) { return DrawPoints(shapes::GenerateCirclePoints(center, radius)); }
	bool FillCircle(FPoint center, float radius);

	bool DrawEllipse(FPoint center, float radiusX, float radiusY, float rotationDeg = 0.f);
	bool FillEllipse(FPoint center, float radiusX, float radiusY, float rotationDeg = 0.f);

	bool DrawArc(FPoint center, float radius, float startAngleDeg, float endAngleDeg);

	// Contour d'une part de camembert : arc + deux rayons vers le centre.
	bool DrawPie(FPoint center, float radius, float startAngleDeg, float endAngleDeg);

	// Contour d'un rectangle à coins arrondis (rayons indépendants par coin).
	/// Rayons bornés à la moitié du plus petit côté : au-delà, les quarts de
	/// disque d'angle se chevauchent et la bande centrale prend une largeur
	/// négative — un carré de 8 px au rayon 8 se dessinait en « × ».
	[[nodiscard]] static Corners ClampCorners(const FRect &rect, const Corners &c) noexcept;

	/// Comme SDL_RenderRect, le trait reste À L'INTÉRIEUR du rectangle : les
	/// bords droit/bas passent par les pixels x+w-1 et y+h-1 (et non x+w/y+h,
	/// hors de la boîte — donc rognés dès qu'un clip la borne).
	bool DrawRoundedRect(const FRect &outer, const Corners &corners);

	// Rectangle à coins arrondis rempli (4 secteurs de coin + 3 bandes rectangulaires).
	bool FillRoundedRect(const FRect &rect, const Corners &corners);

	// Bordure (anneau) d'un rectangle à coins arrondis, remplie entre le
	// contour `rect` (intérieur) et `rect` étendu de `border` (extérieur).
	bool DrawRoundedBorderedRect(const FRect &rect, const Sides &border, const Corners &c);

	// Contour d'un polygone fermé (renvoie au premier point).
	bool DrawPolygon(std::span<const FPoint> points);

	// Polygone rempli (convexe, ou concave "raisonnable") via éventail de triangles.
	bool FillPolygon(std::span<const FPoint> points);

	// Courbe de Bézier (degré arbitraire, via de Casteljau) approximée par des
	// segments — `step` dans (0, 1], plus petit = plus de segments = plus lisse.
	bool DrawBezier(std::span<const FPoint> controlPoints, float step = 0.05f);

	// ── Texture creation ─────────────────────────────────────────────────────

	[[nodiscard]] Result<Texture, StringView> CreateTexture(PixelFormat fmt, TextureAccess access, int w, int h);

	[[nodiscard]] Result<Texture, StringView> CreateTextureFromSurface(const Surface &surf);

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
	[[nodiscard]] Result<Surface, StringView> ReadPixels(Option<Rect> rect = NONE) const;

	// ── Output info ──────────────────────────────────────────────────────────

	[[nodiscard]] Point OutputSize() const;
};

// Texture factory methods that need complete Renderer type




} // namespace sdl3
