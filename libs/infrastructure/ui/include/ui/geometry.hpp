#pragma once
/**
 * ui::geometry — géométrie partagée par la saisie et le rendu : s'ils la
 * calculaient chacun de leur côté, le clic tomberait à côté de ce qui est
 * dessiné (pouce d'une barre de défilement, caractère sous le curseur).
 */
#include "context.hpp"

namespace ui {

// ── Barres de défilement automatiques ───────────────────────────────────────

struct ScrollbarGeometry {
	sdl3::FRect vTrack{}, vThumb{};
	sdl3::FRect hTrack{}, hThumb{};
	bool vertical = false, horizontal = false;
};

/// Pistes et pouces d'un conteneur défilant (`box` : boîte de bordure).
[[nodiscard]] ScrollbarGeometry ScrollbarsOf(const sdl3::FRect &box, const math::Sides &border, const Scroll &s,
											 float thickness);

// ── Texte éditable ──────────────────────────────────────────────────────────

/// Disposition du texte d'un widget éditable (ou défilant).
struct TextGeometry {
	sdl3::FRect box{};		  ///< zone du texte (à droite de la gouttière de numéros)
	sdl3::FPoint scroll{};	  ///< décalage de défilement
	float lineHeight = 0.f;
	float gutter = 0.f;		  ///< largeur de la gouttière (numéros de ligne)
	std::vector<TextLine> lines;
	LineMeasure measure;
	int tabSize = 4;
	bool password = false;

	/// Abscisse (fenêtre) de l'octet `offset` de la ligne `line`.
	[[nodiscard]] float XOf(size_t line, size_t offset) const;
	/// Ordonnée (fenêtre) du haut de la ligne `line`.
	[[nodiscard]] float YOf(size_t line) const noexcept { return box.y - scroll.y + float(line) * lineHeight; }
	/// Octet (dans le texte entier) sous le point `p`.
	[[nodiscard]] size_t OffsetAt(sdl3::FPoint p) const;
	/// Texte affiché d'une ligne (tabulations développées, masque).
	[[nodiscard]] String Display(size_t line, size_t upTo = String::npos) const;
};

[[nodiscard]] TextGeometry TextGeometryOf(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e);

/// Fait défiler `e` pour que le curseur de son éditeur reste visible.
void KeepCaretVisible(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e);

} // namespace ui
