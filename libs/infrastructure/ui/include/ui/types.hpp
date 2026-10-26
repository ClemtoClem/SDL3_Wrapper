#pragma once
/**
 * ui::types — types valeur élémentaires du module ui (aucune dépendance à
 * l'ECS) : longueurs CSS, énumérations de mise en page et de typographie,
 * peintures de fond, ombres.
 *
 * Les structures géométriques viennent des autres modules : `sdl3::FRect` /
 * `sdl3::FPoint` pour les rectangles et points, `sdl3::FColor` pour les
 * couleurs, `math::Sides` pour marges/padding/bordures et `math::Corners`
 * pour les rayons de coins.
 */
#include <cstdint>

#include <core/core.hpp>
#include <math/math.hpp>
#include <sdl3/input.hpp>
#include <sdl3/structs.hpp>

namespace ui {

using Color = sdl3::FColor;

/// Couleur depuis des composantes 0..255.
[[nodiscard]] constexpr Color Rgba(int r, int g, int b, int a = 255) noexcept {
	return Color{float(r) / 255.f, float(g) / 255.f, float(b) / 255.f, float(a) / 255.f};
}

/// Même couleur, alpha remplacé.
[[nodiscard]] constexpr Color WithAlpha(Color c, float a) noexcept { return Color{c.r, c.g, c.b, a}; }

/// Mélange linéaire de deux couleurs (t = 0 : a, t = 1 : b).
[[nodiscard]] constexpr Color Mix(Color a, Color b, float t) noexcept {
	return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// ============================================================================
// Length — longueur CSS résoluble
// ============================================================================

enum class Unit : uint8_t {
	Px,   ///< pixels
	Pct,  ///< % de la boîte de contenu du parent (sur l'axe concerné)
	Em,   ///< multiple de la taille de police du widget
	Rem,  ///< multiple de la taille de police de la racine
	Vw,   ///< % de la largeur de la fenêtre
	Vh,   ///< % de la hauteur de la fenêtre
	Auto, ///< taille intrinsèque (contenu)
	Fr,   ///< part de l'espace restant sur l'axe principal (poids)
};

/// Bases de résolution d'une longueur.
struct LengthBasis {
	float parent = 0.f;   ///< taille de contenu du parent sur l'axe
	float fontSize = 14.f;
	float rootFontSize = 14.f;
	float viewportW = 0.f;
	float viewportH = 0.f;
	float scale = 1.f; ///< zoom cumulé (cf. propriété `zoom`)
};

struct Length {
	float value = 0.f;
	Unit unit = Unit::Auto;
	float offset = 0.f; ///< pixels ajoutés après résolution (`calc(50% - 8px)`)

	[[nodiscard]] static constexpr Length Px(float v) noexcept { return {v, Unit::Px, 0.f}; }
	[[nodiscard]] static constexpr Length Pct(float v) noexcept { return {v, Unit::Pct, 0.f}; }
	[[nodiscard]] static constexpr Length Em(float v) noexcept { return {v, Unit::Em, 0.f}; }
	[[nodiscard]] static constexpr Length Rem(float v) noexcept { return {v, Unit::Rem, 0.f}; }
	[[nodiscard]] static constexpr Length Vw(float v) noexcept { return {v, Unit::Vw, 0.f}; }
	[[nodiscard]] static constexpr Length Vh(float v) noexcept { return {v, Unit::Vh, 0.f}; }
	[[nodiscard]] static constexpr Length Auto() noexcept { return {0.f, Unit::Auto, 0.f}; }
	[[nodiscard]] static constexpr Length Fr(float weight = 1.f) noexcept { return {weight, Unit::Fr, 0.f}; }

	/// Ajoute un décalage fixe en pixels : `Length::Pct(100).Plus(-16)`.
	[[nodiscard]] constexpr Length Plus(float px) const noexcept { return {value, unit, offset + px}; }

	[[nodiscard]] constexpr bool IsAuto() const noexcept { return unit == Unit::Auto; }
	[[nodiscard]] constexpr bool IsFr() const noexcept { return unit == Unit::Fr; }
	/// Vrai si la longueur dépend de la taille du parent (inconnue pendant la mesure).
	[[nodiscard]] constexpr bool IsRelative() const noexcept { return unit == Unit::Pct; }

	/// Résout en pixels (Auto et Fr résolvent à `offset` seul).
	[[nodiscard]] constexpr float Resolve(const LengthBasis &b) const noexcept {
		float base = 0.f;
		switch (unit) {
		case Unit::Px:
			base = value * b.scale;
			break;
		case Unit::Pct:
			base = value / 100.f * b.parent;
			break;
		case Unit::Em:
			base = value * b.fontSize;
			break;
		case Unit::Rem:
			base = value * b.rootFontSize * b.scale;
			break;
		case Unit::Vw:
			base = value / 100.f * b.viewportW;
			break;
		case Unit::Vh:
			base = value / 100.f * b.viewportH;
			break;
		case Unit::Auto:
		case Unit::Fr:
			break;
		}
		return base + offset * b.scale;
	}

	constexpr bool operator==(const Length &) const = default;
};

// ============================================================================
// Énumérations de mise en page
// ============================================================================

/// Mode d'affichage d'un widget.
enum class Display : uint8_t {
	Flex,  ///< flexbox (direction par flex-direction)
	Grid,  ///< grille à colonnes égales (grid-columns)
	Stack, ///< enfants superposés dans la boîte de contenu
	None,  ///< non affiché, ne prend aucune place
};

enum class FlexDirection : uint8_t { Row, Column, RowReverse, ColumnReverse };
enum class FlexWrap : uint8_t { NoWrap, Wrap };
enum class Justify : uint8_t { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
enum class Align : uint8_t { Auto, Start, Center, End, Stretch };

/// Mode de positionnement (cf. CSS `position`) :
///  - Relative : participe au flux du parent ;
///  - Absolute : hors flux, placé dans la boîte de contenu du parent ;
///  - Fixed    : hors flux, placé contre la fenêtre et dessiné au-dessus de
///    tout le reste (popups, menus, modales, HUD) tout en restant, dans
///    l'arbre, l'enfant de son parent logique.
enum class Position : uint8_t { Relative, Absolute, Fixed };

/// Point d'accroche d'un widget positionné (Absolute/Fixed) : le point
/// correspondant du widget est aligné sur le même point de la boîte de
/// référence, puis décalé de `left`/`top` (ou `right`/`bottom`).
enum class Anchor : uint8_t { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };

[[nodiscard]] constexpr sdl3::FPoint AnchorFactors(Anchor a) noexcept {
	switch (a) {
	case Anchor::TopLeft:
		return {0.f, 0.f};
	case Anchor::Top:
		return {0.5f, 0.f};
	case Anchor::TopRight:
		return {1.f, 0.f};
	case Anchor::Left:
		return {0.f, 0.5f};
	case Anchor::Center:
		return {0.5f, 0.5f};
	case Anchor::Right:
		return {1.f, 0.5f};
	case Anchor::BottomLeft:
		return {0.f, 1.f};
	case Anchor::Bottom:
		return {0.5f, 1.f};
	case Anchor::BottomRight:
		return {1.f, 1.f};
	}
	return {0.f, 0.f};
}

enum class Overflow : uint8_t { Visible, Hidden, Scroll, Auto };
enum class Orientation : uint8_t { Horizontal, Vertical };
enum class Visibility : uint8_t { Visible, Hidden };
enum class PointerEvents : uint8_t { Auto, None };

/// Curseur souris (propriété CSS `cursor`) : celui du module sdl3.
using Cursor = sdl3::SystemCursor;

// ============================================================================
// Typographie
// ============================================================================

enum class TextAlign : uint8_t { Left, Center, Right, Justify };
enum class VerticalAlign : uint8_t { Top, Middle, Bottom };
enum class FontStyle : uint8_t { Normal, Italic };

/// Décoration de texte (combinables).
enum class TextDecoration : uint8_t { None = 0, Underline = 1, LineThrough = 2, Overline = 4 };

[[nodiscard]] constexpr TextDecoration operator|(TextDecoration a, TextDecoration b) noexcept {
	return TextDecoration(uint8_t(a) | uint8_t(b));
}
[[nodiscard]] constexpr bool HasDecoration(TextDecoration set, TextDecoration flag) noexcept {
	return (uint8_t(set) & uint8_t(flag)) != 0;
}

/// Que faire d'un texte plus large que sa boîte. Le débordement ne se pose
/// que si la largeur est contrainte : sans contrainte, la boîte s'élargit
/// jusqu'au texte.
enum class TextOverflow : uint8_t {
	Clip,     ///< rogné net
	Ellipsis, ///< rogné, terminé par « … »
	Scroll,   ///< barre de défilement horizontale
	Wrap,     ///< retour à la ligne (la boîte grandit en hauteur)
	Marquee,  ///< défilement automatique aller-retour
};

/// Ajustement d'une image dans sa boîte (cf. CSS object-fit).
enum class ObjectFit : uint8_t { Fill, Contain, Cover, None, ScaleDown };

// ============================================================================
// Paint — fond dessiné (dégradés, images)
// ============================================================================

enum class PaintKind : uint8_t {
	None,
	Linear,   ///< dégradé linéaire 2 couleurs (angle en degrés, 180 = vers le bas)
	Bilinear, ///< 4 coins (haut-gauche, haut-droit, bas-gauche, bas-droit)
	Hue,      ///< spectre de teinte (angle 90 = vers la droite)
	Checker,  ///< damier (fond de transparence)
	Image,    ///< image (`url(...)`)
};

struct Paint {
	PaintKind kind = PaintKind::None;
	Color colors[4]{};
	float angle = 180.f;
	String source; ///< chemin ou clé de texture (PaintKind::Image)

	[[nodiscard]] static Paint LinearGradient(Color from, Color to, float angleDeg = 180.f) {
		Paint p;
		p.kind = PaintKind::Linear;
		p.colors[0] = from;
		p.colors[1] = to;
		p.angle = angleDeg;
		return p;
	}
	[[nodiscard]] static Paint Corners4(Color tl, Color tr, Color bl, Color br) {
		Paint p;
		p.kind = PaintKind::Bilinear;
		p.colors[0] = tl;
		p.colors[1] = tr;
		p.colors[2] = bl;
		p.colors[3] = br;
		return p;
	}
	[[nodiscard]] static Paint HueSpectrum(float angleDeg = 90.f) {
		Paint p;
		p.kind = PaintKind::Hue;
		p.angle = angleDeg;
		return p;
	}
	[[nodiscard]] static Paint Checkerboard() {
		Paint p;
		p.kind = PaintKind::Checker;
		return p;
	}
	[[nodiscard]] static Paint FromImage(String source) {
		Paint p;
		p.kind = PaintKind::Image;
		p.source = std::move(source);
		return p;
	}

	bool operator==(const Paint &o) const {
		return kind == o.kind && angle == o.angle && source == o.source && colors[0] == o.colors[0] &&
			   colors[1] == o.colors[1] && colors[2] == o.colors[2] && colors[3] == o.colors[3];
	}
};

/// Ombre portée (cf. CSS box-shadow, sans `inset`).
struct Shadow {
	float x = 0.f, y = 0.f;
	float blur = 0.f;
	float spread = 0.f;
	Color color{0.f, 0.f, 0.f, 0.f};

	[[nodiscard]] bool IsVisible() const noexcept { return color.a > 0.f; }
	bool operator==(const Shadow &) const = default;
};

// ============================================================================
// Couleurs — conversions
// ============================================================================

struct Hsv {
	float h = 0.f, s = 0.f, v = 0.f;
};

[[nodiscard]] Color HsvToColor(float h, float s, float v, float a = 1.f) noexcept;
[[nodiscard]] Hsv ColorToHsv(Color c) noexcept;
/// "#RRGGBBAA" (majuscules).
[[nodiscard]] String ColorToHex(Color c);
/// "#RGB", "#RGBA", "#RRGGBB" ou "#RRGGBBAA" (le '#' est facultatif).
[[nodiscard]] Option<Color> ParseHexColor(StringView hex) noexcept;

} // namespace ui
