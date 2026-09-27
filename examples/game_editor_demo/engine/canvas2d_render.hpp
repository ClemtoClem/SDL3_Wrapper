#pragma once
/**
 * game_editor — le moteur 2D, partie DESSIN : transforme une liste d'éléments
 * (canvas2d.hpp) en triangles SDL (`Renderer::RenderGeometry`), la seule
 * primitive qui accepte n'importe quelle transformation affine — rotation et
 * échelle non uniforme comprises, pour les formes, les images ET le texte.
 *
 *  - formes : remplissage en triangles (cercle en éventail, polygone concave
 *    triangulé par oreilles), contour en quadrilatères par segment ;
 *  - images : chargées une fois (chemin du projet, puis des ressources),
 *    dessinées sur un quadrilatère teinté par la couleur de l'élément ; une
 *    image introuvable est remplacée par une mire bien visible ;
 *  - texte : rastérisé par SDL_ttf à sa taille À L'ÉCRAN (net à tout zoom),
 *    en blanc — la couleur passe par la teinte des sommets, si bien qu'un
 *    texte qui change de couleur ne se re-rastérise pas. Les textures de
 *    texte inutilisées quelques secondes sont libérées.
 *
 * Ce fichier ne connaît ni l'éditeur ni le runtime : il dessine ce qu'on lui
 * donne, dans la vue qu'on lui donne (View2D).
 */
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/core.hpp"
#include "render3d/shape2d.hpp"
#include "sdl3/image.hpp"
#include "sdl3/render.hpp"
#include "sdl3/ttf.hpp"

#include "canvas2d.hpp"

namespace game_editor {

class Canvas2DRenderer {
public:
	/// Chemin d'une image tel qu'écrit dans le document → fichier à ouvrir.
	using Resolver = std::function<String(const String &)>;

	void SetResolver(Resolver resolver) { m_resolver = std::move(resolver); }
	/// Police des textes 2D (défaut : DejaVuSans des ressources, sinon une
	/// police du système).
	void SetFontPath(String path);

	/// Mesure réelle d'un texte (repli : estimation) — à passer à la
	/// sélection et aux collisions pour qu'elles collent au dessin.
	[[nodiscard]] math::FVector2 MeasureText(const String &text, float fontSize);

	[[nodiscard]] TextMeasure Measure();

	/// Remplit `rect` d'une couleur unie.
	static void FillRect(sdl3::Renderer &ren, sdl3::FRect rect, sdl3::Color color);

	/// Dessine les éléments dans l'ordre reçu.
	void Draw(sdl3::Renderer &ren, const View2D &view, const std::vector<DrawItem2D> &items);

	void DrawItem(sdl3::Renderer &ren, const Transform2D &toPixels, const CanvasItemDesc &item);

	// ── Primitives en pixels (aussi utiles aux repères de l'éditeur) ────────

	/// Ligne brisée épaisse (quadrilatère par segment, articulations
	/// arrondies au-delà de 2 px).
	static void Stroke(sdl3::Renderer &ren, const std::vector<sdl3::FPoint> &points, bool closed, float width,
					   sdl3::FColor color);

	static void FillDisc(sdl3::Renderer &ren, sdl3::FPoint center, float radius, sdl3::FColor color);

	[[nodiscard]] static sdl3::FColor ToF(sdl3::Color c) noexcept;

private:
	struct CachedText {
		sdl3::Texture texture;
		sdl3::FPoint size;
		uint64_t lastUse = 0;
	};

	[[nodiscard]] static sdl3::Vertex Vertex(sdl3::FPoint p, sdl3::FColor color, sdl3::FPoint uv = {0.f, 0.f});
	[[nodiscard]] static sdl3::FPoint ToPoint(math::FVector2 v) noexcept { return {v.x, v.y}; }

	/// Contour d'une ellipse, assez fin pour sa taille à l'écran.
	[[nodiscard]] static std::vector<math::FVector2> EllipsePoints(const CanvasItemDesc &item, const Transform2D &t);

	/// Remplissage (triangulé) puis contour d'un polygone local.
	void DrawOutlinedPolygon(sdl3::Renderer &ren, const Transform2D &t, const CanvasItemDesc &item,
							 const std::vector<math::FVector2> &local, bool convex);

	/// Quadrilatère texturé sur la boîte locale de l'élément.
	void TexturedQuad(sdl3::Renderer &ren, const sdl3::Texture &texture, const Transform2D &t, Bounds2D box,
					  sdl3::FColor tint, bool flipH, bool flipV);

	void DrawSprite(sdl3::Renderer &ren, const Transform2D &t, const CanvasItemDesc &item);

	void DrawText(sdl3::Renderer &ren, const Transform2D &t, const CanvasItemDesc &item);

	// ── Caches ───────────────────────────────────────────────────────────────

	[[nodiscard]] const sdl3::Texture *TextureFor(sdl3::Renderer &ren, const String &path);

	[[nodiscard]] sdl3::Font *FontFor(int pixels);

	[[nodiscard]] CachedText *TextFor(sdl3::Renderer &ren, const String &text, int pixels);

	void EvictStaleTexts();

	Resolver m_resolver;
	String m_fontPath;
	uint64_t m_frame = 0;
	std::unordered_map<std::string, Option<sdl3::Texture>> m_textures;
	std::unordered_map<int, Option<sdl3::Font>> m_fonts;
	std::unordered_map<std::string, CachedText> m_texts;
};

} // namespace game_editor
