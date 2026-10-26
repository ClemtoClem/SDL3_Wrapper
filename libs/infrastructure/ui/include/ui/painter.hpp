#pragma once
/**
 * ui::Painter — primitives de dessin 2D de l'interface au-dessus d'un
 * `sdl3::Renderer` (pilote « gpu » = SDL_GPU / Vulkan pour les fenêtres, ou
 * rendu logiciel sur surface pour l'encadrement d'une scène render3d, cf.
 * window_frame.hpp).
 *
 * Tient une pile de clips (intersectés), un facteur d'opacité, un cache de
 * textes mis en forme (`sdl3::Text`, évincés après quelques images sans
 * usage) et un cache de textures (images par clé ou par chemin).
 *
 * Sans renderer (tests sans fenêtre), toutes les opérations sont sans effet :
 * la mise en page et l'interaction restent testables.
 */
#include <span>
#include <unordered_map>
#include <vector>

#include <sdl3/render.hpp>
#include <sdl3/ttf.hpp>

#include "fonts.hpp"
#include "style.hpp"

namespace ui {

/// Textures de l'interface (images, fonds `url()`), par clé ou par chemin.
class Textures {
public:
	void SetRenderer(sdl3::Renderer *renderer) noexcept { m_renderer = renderer; }
	/// Ajoute (ou remplace) une texture sous une clé.
	void Add(const String &key, sdl3::Texture texture);
	void Remove(const String &key) { m_textures.erase(key); }
	[[nodiscard]] bool Has(const String &key) const { return m_textures.count(key) > 0; }
	/// Texture d'une clé ; si la clé est un chemin de fichier inconnu, il est
	/// chargé une fois (échec mémorisé : pas de nouvel essai à chaque image).
	[[nodiscard]] sdl3::Texture *Get(const String &key);
	/// Taille d'une texture (0 si absente).
	[[nodiscard]] sdl3::FPoint SizeOf(const String &key);
	void Clear() {
		m_textures.clear();
		m_failed.clear();
	}

private:
	sdl3::Renderer *m_renderer = nullptr;
	std::unordered_map<String, sdl3::Texture> m_textures;
	std::unordered_map<String, bool> m_failed;
};

class Painter {
public:
	Painter() = default;
	Painter(const Painter &) = delete;
	Painter &operator=(const Painter &) = delete;

	/// Branche le renderer (et crée le moteur de texte). nullptr : sans dessin.
	void SetRenderer(sdl3::Renderer *renderer);
	[[nodiscard]] sdl3::Renderer *Renderer() noexcept { return m_renderer; }
	[[nodiscard]] bool CanDraw() const noexcept { return m_renderer != nullptr; }
	void SetFonts(Fonts *fonts) noexcept { m_fonts = fonts; }
	void SetTextures(Textures *textures) noexcept { m_textures = textures; }
	[[nodiscard]] Fonts *GetFonts() noexcept { return m_fonts; }
	[[nodiscard]] Textures *GetTextures() noexcept { return m_textures; }

	/// Début / fin d'une image : réinitialise clips et opacité, évince les
	/// textes inutilisés.
	void BeginFrame(sdl3::FRect viewport);
	void EndFrame();

	// ── Clip ─────────────────────────────────────────────────────────────────
	void PushClip(const sdl3::FRect &r);
	void PopClip();
	/// Remplace la pile par un seul clip (couches Fixed : la fenêtre entière).
	void ResetClip(const sdl3::FRect &r);
	[[nodiscard]] const sdl3::FRect &Clip() const noexcept { return m_clip; }
	[[nodiscard]] bool IsClippedOut(const sdl3::FRect &r) const noexcept;

	// ── Opacité ──────────────────────────────────────────────────────────────
	void SetOpacity(float opacity) noexcept { m_opacity = opacity; }
	[[nodiscard]] float Opacity() const noexcept { return m_opacity; }

	// ── Formes ───────────────────────────────────────────────────────────────
	void FillRect(const sdl3::FRect &r, Color c);
	void FillRoundedRect(const sdl3::FRect &r, const math::Corners &radius, Color c);
	/// Contour de `width` pixels, À L'INTÉRIEUR de `r`.
	void StrokeRoundedRect(const sdl3::FRect &r, const math::Corners &radius, const math::Sides &width, Color c);
	void FillPaint(const sdl3::FRect &r, const Paint &paint, const math::Corners &radius, ObjectFit fit);
	void DrawShadow(const sdl3::FRect &r, const math::Corners &radius, const Shadow &shadow);
	void DrawGloss(const sdl3::FRect &r, float radius, float strength);
	void DrawGlow(const sdl3::FRect &r, float radius, float strength, Color c);
	void VGradient(const sdl3::FRect &r, Color top, Color bottom);
	void HGradient(const sdl3::FRect &r, Color left, Color right);
	void QuadGradient(const sdl3::FRect &r, Color tl, Color tr, Color bl, Color br);
	void HueBar(const sdl3::FRect &r, bool vertical = false);
	void Checkerboard(const sdl3::FRect &r, float cell = 6.f);

	void Line(sdl3::FPoint a, sdl3::FPoint b, float width, Color c);
	void Polyline(std::span<const sdl3::FPoint> pts, float width, Color c, bool closed = false);
	void FillPolygon(std::span<const sdl3::FPoint> pts, Color c);
	void FillCircle(sdl3::FPoint center, float radius, Color c);
	void StrokeCircle(sdl3::FPoint center, float radius, float width, Color c);
	/// Arc épais : angles en degrés, 0 = droite, sens horaire (y vers le bas).
	void Arc(sdl3::FPoint center, float radius, float startDeg, float sweepDeg, float width, Color c);
	void FillPie(sdl3::FPoint center, float radius, float startDeg, float sweepDeg, Color c);
	void Triangles(std::span<const sdl3::Vertex> vertices);

	// ── Texte ────────────────────────────────────────────────────────────────
	/// Dessine une ligne (déjà normalisée) à (x, y) = coin haut-gauche, origine
	/// arrondie au pixel (un texte à mi-pixel paraît flou).
	void Text(const String &line, sdl3::Font *font, Color c, float x, float y);
	/// Taille d'une ligne telle que dessinée (repli : heuristique).
	[[nodiscard]] sdl3::FPoint MeasureText(const String &line, sdl3::Font *font, float size);

	// ── Images ───────────────────────────────────────────────────────────────
	void Image(const sdl3::Texture &tex, const sdl3::FRect &dst, ObjectFit fit = ObjectFit::Fill,
			   Option<sdl3::FRect> src = NONE);
	void Image(const String &key, const sdl3::FRect &dst, ObjectFit fit = ObjectFit::Fill);

private:
	void ApplyClip();
	[[nodiscard]] Color Fade(Color c) const noexcept { return Color{c.r, c.g, c.b, c.a * m_opacity}; }

	sdl3::Renderer *m_renderer = nullptr;
	Option<sdl3::TextEngine> m_engine = NONE;
	Fonts *m_fonts = nullptr;
	Textures *m_textures = nullptr;
	sdl3::FRect m_viewport{};
	sdl3::FRect m_clip{};
	std::vector<sdl3::FRect> m_clips;
	float m_opacity = 1.f;
	uint64_t m_frame = 0;

	struct TextKey {
		const sdl3::Font *font;
		String text;
		bool operator==(const TextKey &o) const { return font == o.font && text == o.text; }
	};
	struct TextHash {
		size_t operator()(const TextKey &k) const noexcept {
			return std::hash<String>{}(k.text) ^ (std::hash<const void *>{}(k.font) << 1);
		}
	};
	struct CachedText {
		sdl3::Text text;
		Color color;
		uint64_t lastUse = 0;
		sdl3::FPoint size{};
	};
	std::unordered_map<TextKey, CachedText, TextHash> m_texts;
};

/// Rect de clip entier, bords arrondis VERS L'EXTÉRIEUR.
[[nodiscard]] sdl3::Rect ToClipRect(const sdl3::FRect &r) noexcept;
/// Rayons bornés à la moitié du plus petit côté.
[[nodiscard]] math::Corners ClampRadius(const sdl3::FRect &r, const math::Corners &c) noexcept;
/// Rect de destination d'une image de taille `size` dans `box` selon `fit`.
[[nodiscard]] sdl3::FRect FitRect(sdl3::FPoint size, const sdl3::FRect &box, ObjectFit fit) noexcept;

} // namespace ui
