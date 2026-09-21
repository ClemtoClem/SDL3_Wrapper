#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <span>

#include "../core/core.hpp"
#include "stdinc.hpp" // couche de base : math (min/max/clamp/lerp...), memory, ctype, env

namespace sdl3 {

// ============================================================================
// Color
// ============================================================================

/**
 * @brief X-macro listant la palette de couleurs nommées de la bibliothèque.
 *
 * `LIST_COLORS` n'est pas destiné à être utilisé directement : il énumère
 * chaque couleur comme `_c(NOM, RR, GG, BB, AA)` (composantes en hexadécimal,
 * sans le `0x` — ex. `_c(RED, FF, 00, 00, FF)` pour `#FF0000FF`). Pour
 * générer du code à partir de cette liste, définissez une macro `_c` avant
 * d'inclure `LIST_COLORS`, puis annulez-la avec `#undef _c` — c'est exactement
 * ce que font `Color` et `FColor` ci-dessous pour générer leurs méthodes
 * fabriques statiques (`Color::RED()`, `FColor::RED()`, etc.), une par
 * couleur de la liste, sans dupliquer les valeurs.
 */
#define LIST_COLORS \
	_c(TRANSPARENT, 00, 00, 00, 00);    /* #00000000 */ \
	_c(ALICE_BLUE, F0, F8, FF, FF);     /* #F0F8FFFF */ \
	_c(AMBER_GREEN, 9A, 80, 3A, FF);    /* #9A803AFF */ \
	_c(AMBER_SUN, FF, 99, 88, FF);      /* #FF9988FF */ \
	_c(ANTIQUE_WHITE, FA, EB, D7, FF);  /* #FAEBD7FF */ \
	_c(AQUA_MARINE, F7, FF, D4, FF);    /* #F7FFD4FF */ \
	_c(AZURE, F0, FF, FF, FF);          /* #F0FFFFFF */ \
	_c(BEIGE, F5, F5, DC, FF);          /* #F5F5DCFF */ \
	_c(BISQUE, FF, E4, C4, FF);         /* #FFE4C4FF */ \
	_c(BLACK, 00, 00, 00, FF);          /* #000000FF */ \
	_c(BLUE, 00, 00, FF, FF);           /* #0000FFFF */ \
	_c(BLUE_VIOLET, 50, 2A, E2, FF);    /* #502AE2FF */ \
	_c(BROWN, A5, 2A, 2A, FF); \
	_c(BURLY_WOOD, DE, B8, 87, FF); \
	_c(CADET_BLUE, 5F, 9E, A0, FF); \
	_c(CHARTREUSE, 7F, FF, 00, FF); \
	_c(CHOCOLATE, D2, 69, 1E, FF); \
	_c(CORAL, FF, 7F, 50, FF); \
	_c(CORN_FLOWER_BLUE, 64, 95, ED, FF); \
	_c(CORN_SILK, FF, F8, DC, FF); \
	_c(CRIMSON, DC, 14, 50, FF); \
	_c(CYAN, 00, FF, FF, FF); \
	_c(DARK_BLUE, 00, 00, 8B, FF); \
	_c(DARK_CYAN, 00, 8B, 8B, FF); \
	_c(DARK_GOLDEN_ROD, B8, 86, 0B, FF);        /* #B8860BFF */ \
	_c(DARK_GREY1, 10, 10, 10, FF);             /* #101010FF */ \
	_c(DARK_GREY2, 20, 20, 20, FF);             /* #202020FF */ \
	_c(DARK_GREY3, 30, 30, 30, FF);             /* #303030FF */ \
	_c(DARK_GREY4, 40, 40, 40, FF);             /* #404040FF */ \
	_c(DARK_GREY5, 50, 50, 50, FF);             /* #505050FF */ \
	_c(DARK_GREEN, 00, 64, 00, FF);             /* #006400FF */ \
	_c(DARK_KAKI, AD, A7, 5B, FF);              /* #AdA75BFF */ \
	_c(DARK_MAGENTA, 8B, 00, 8B, FF);           /* #8B008BFF */ \
	_c(DARK_OLIVER_GREEN, 45, 5B, 1F, FF);      /* #455B1FFF */ \
	_c(DARK_ORANGE, E7, 74, 00, FF);            /* #E77400FF */ \
	_c(DARK_ORCHILD, 99, 32, CC, FF);           /* #9932CCFF */ \
	_c(DARK_PINK, C7, 8D, 9B, FF); \
	_c(DARK_PURPLE, 61, 00, 94, FF); \
	_c(DARK_RED, 8B, 00, 00, FF); \
	_c(DARK_SALMON, E9, 96, 7A, FF); \
	_c(DARK_SEA_GREEN, 8F, BC, 8F, FF); \
	_c(DARK_SLATE_BLUE, 48, 3D, 8B, FF); \
	_c(DARK_STEEL_BLUE, 15, 27, 36, FF); \
	_c(DODGER_BLUE1, 00, 60, CF, FF); \
	_c(DODGER_BLUE2, 1E, 90, FF, FF); \
	_c(DODGER_BLUE3, 46, B8, FF, FF); \
	_c(FIRE_BRICK, B2, 22, 22, FF); \
	_c(FLORAL_WHITE, FF, FA, F0, FF); \
	_c(FOREST_GREEN, 22, 8B, 22, FF); \
	_c(GOLD, FF, D7, 00, FF); \
	_c(GOLD_ROD, DA, A5, 20, FF); \
	_c(GREY1, 70, 70, 70, FF); \
	_c(GREY2, 80, 80, 80, FF); \
	_c(GREY3, 90, 90, 90, FF); \
	_c(GREEN, 00, FF, 00, FF); \
	_c(GREEN_YELLOW, AD, FF, 2F, FF); \
	_c(HOT_PINK, FF, 69, B4, FF); \
	_c(INDIGO, 4B, 00, B2, FF); \
	_c(IVORY, FF, FF, F0, FF); \
	_c(KHAKI, F0, E6, 8C, FF); \
	_c(LARCHMERE, 70, BA, A7, FF); \
	_c(LAVENDER, E6, E6, FA, FF); \
	_c(LAWN_GREEN, 7C, FC, 00, FF); \
	_c(LIGHT_BLUE, AD, D8, E6, FF); \
	_c(LIGHT_CORAL, F0, 80, 80, FF); \
	_c(LIGHT_CYAN, E0, FF, FF, FF); \
	_c(LIGHT_GREEN, 90, EE, 90, FF); \
	_c(LIGHT_GREY1, C6, C6, C6, FF); \
	_c(LIGHT_GREY2, D3, D3, D3, FF); \
	_c(LIGHT_GREY3, DC, DC, DC, FF); \
	_c(LIGHT_ORANGE, FF, B4, 10, FF); \
	_c(LIGHT_PINK1, FF, B6, C1, FF); \
	_c(LIGHT_PINK2, FF, A9, DC, FF); \
	_c(LIGHT_PINK3, FF, BF, FF, FF); \
	_c(LIGHT_SALMON, FF, A0, 7A, FF); \
	_c(LIGHT_STEEL_BLUE, 16, 82, B4, FF); \
	_c(LIGHT_YELLOW, FF, D9, A6, FF); \
	_c(LIME_GREEN, 32, CD, 32, FF); \
	_c(MAGENTA, FF, 00, FF, FF); \
	_c(MAROON, 80, 00, 00, FF); \
	_c(MEDIUM_AQUA_MARINE, 66, CD, AA, FF); \
	_c(MEDIUM_GREY1, 1C, 1C, 1C, FF); \
	_c(MEDIUM_SLATE_BLUE, 7B, 68, EE, FF); \
	_c(MEDIUM_STEEL_BLUE, 38, 68, 90, FF); \
	_c(ORANGE, FF, 64, 00, FF); \
	_c(ORANGE_JUICE, FF, A5, 00, FF); \
	_c(ORANGE_RED, FF, 45, 00, FF);         /* #FF4500FF */ \
	_c(PERU, CD, 85, 3F, FF);               /* #CD853FFF */ \
	_c(PINK, FF, 81, CA, FF);               /* #FF81CAFF */ \
	_c(PURPLE, 99, 00, CC, FF);             /* #9900CCFF */ \
	_c(PURPLE2, 66, 30, 6C, FF);            /* #66306CFF */ \
	_c(RED, FF, 00, 00, FF);                /* #FF0000FF */ \
	_c(SIENNA, A0, 52, 2D, FF);             /* #87CEEBFF */ \
	_c(SKY_BLUE, 87, CE, EB, FF); \
	_c(STEEL_BLUE, 31, 5B, 7E, FF); \
	_c(SLATE_BLUE, 46, 82, B4, FF); \
	_c(SLATE_GREY, 7A, 8A, 7C, FF); \
	_c(TURQUOISE, 40, E0, D0, FF); \
	_c(VIOLET, EE, 82, EE, FF); \
	_c(WEAT, F5, DE, B3, FF); \
	_c(WHITE, FF, FF, FF, FF); \
	_c(WHITE_SMOKE, F5, F5, F5, FF); \
	_c(YELLOW, FF, FF, 00, FF); \
	_c(YELLOW_GREEN, 9A, CD, 32, FF); \
	/* ---- Palette ui:: (ajoutée lors de la migration FColor, cf. */ \
	/* memory/project_ui_ecs_module.md) — couleurs récurrentes du thème */ \
	/* Aero/dark de ui::, dédupliquées par valeur RGBA exacte. ---- */ \
	_c(UI_TEXT_PRIMARY, DC, DE, E8, FF);        /* #DCDEE8FF */ \
	_c(UI_TEXT_BRIGHT, EB, EB, EB, FF);         /* #EBEBEBFF */ \
	_c(UI_TEXT_MUTED, 78, 7C, 96, FF);          /* #787C96FF */ \
	_c(UI_BORDER_SLATE, 37, 3C, 54, FF);        /* #373C54FF */ \
	_c(UI_BORDER_MUTED, 46, 4C, 6E, FF);        /* #464C6EFF */ \
	_c(UI_BORDER_MUTED2, 46, 4E, 6E, FF);       /* #464E6EFF */ \
	_c(UI_PANEL_DARK, 2A, 2C, 3A, FF);          /* #2A2C3AFF */ \
	_c(UI_PANEL_DARK2, 2D, 30, 42, FF);         /* #2D3042FF */ \
	_c(UI_PANEL_DARKER, 1C, 1E, 2C, FF);        /* #1C1E2CFF */ \
	_c(UI_BG_DEEP, 18, 1A, 26, FF);             /* #181A26FF */ \
	_c(UI_WINDOW_BG, 0A, 0C, 14, FF);           /* #0A0C14FF */ \
	_c(UI_APP_BG, 10, 11, 19, FF);              /* #101119FF */ \
	_c(UI_ACCENT_BLUE, 46, 82, D2, FF);         /* #4682D2FF */ \
	_c(UI_ACCENT_BLUE_PRIMARY, 3C, 8C, DC, FF); /* #3C8CDCFF */ \
	_c(UI_ACCENT_BLUE_LIGHT, 64, A0, E6, FF);   /* #64A0E6FF */ \
	_c(UI_ACCENT_BLUE_BRIGHT, 56, 9E, E8, FF);  /* #569EE8FF */ \
	_c(UI_ACCENT_BLUE_HOVER, 78, AA, FA, FF);   /* #78AAFAFF */ \
	_c(UI_ACCENT_BLUE_DEEP, 3C, 64, AA, FF);    /* #3C64AAFF */ \
	_c(UI_ACCENT_BLUE_GLOW, 3C, 64, AA, A0);    /* #3C64AAA0 */ \
	_c(UI_ACCENT_BLUE_SKY, 46, 96, E6, FF);     /* #4696E6FF */ \
	_c(UI_ACCENT_SKY_BRIGHT, 5A, AA, EB, FF);   /* #5AAAEBFF */ \
	_c(UI_ACCENT_GREEN, 46, D2, 8C, FF);        /* #46D28CFF */ \
	_c(UI_NODE_HEADER_BLUE, 34, 56, 96, FF);    /* #345696FF */ \
	_c(UI_NODE_HEADER_NEUTRAL, 3C, 41, 4E, FF); /* #3C414EFF */ \
	_c(UI_AERO_TITLE_DARK, 12, 28, 46, FF);     /* #122846FF */ \
	_c(UI_AERO_TITLE_LIGHT, 3C, 6E, AA, FF);    /* #3C6EAAFF */ \
	_c(UI_AERO_GLASS_BLUE, 28, 5A, A0, C8);     /* #285AA0C8 */ \
	_c(UI_OVERLAY_DARK, 14, 15, 1C, BE);        /* #14151CBE */ \
	_c(UI_HIGHLIGHT_FAINT, FF, FF, FF, 18);     /* #FFFFFF18 */ \
	_c(UI_WHITE_SOFT, FF, FF, FF, 5A);          /* #FFFFFF5A */ \
	_c(UI_WHITE_STRONG, FF, FF, FF, C8);        /* #FFFFFFC8 */

/**
 * @brief Couleur RGBA 8 bits par composante (0-255), disposition SDL_Color.
 *
 * Représentation "octet" adaptée au stockage et à l'interop directe avec
 * l'API C de SDL (`operator SDL_Color()`). Pour les calculs de mélange,
 * d'assombrissement/éclaircissement, etc., préférer @ref FColor (0.0-1.0),
 * plus précis et sans arrondi intermédiaire — convertir via `ToFloat()` /
 * `FColor(Color)`.
 *
 * Une méthode fabrique statique par couleur de @ref LIST_COLORS est générée
 * ci-dessous (`Color::RED()`, `Color::WHITE()`, `Color::DARK_GREY1()`, ...).
 */
struct Color {
	uint8_t r = 0, g = 0, b = 0, a = 255;

	#define _c(name, r, g, b, a) [[nodiscard]] static constexpr Color name(){ return {(0x##r), (0x##g), (0x##b), (0x##a)}; }
	LIST_COLORS
	#undef _c

	constexpr Color() = default;
	constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}
	constexpr explicit Color(const SDL_Color& c) : r(c.r), g(c.g), b(c.b), a(c.a) {}

	[[nodiscard]] constexpr operator SDL_Color() const { return {r, g, b, a}; }

	/// Conversion vers `SDL_FColor` (composantes normalisées 0.0-1.0).
	[[nodiscard]] constexpr SDL_FColor ToFloat() const { return {r / 255.f, g / 255.f, b / 255.f, a / 255.f}; }

	constexpr bool operator==(const Color &other) const {
		return r == other.r && g == other.g && b == other.b && a == other.a;
	}
};

/**
 * @brief Couleur RGBA en virgule flottante, composantes normalisées et
 * bornées à [0, 1] (toute valeur hors bornes est silencieusement clampée par
 * le constructeur et par chaque opération).
 *
 * Format naturel pour les calculs colorimétriques (mélange, luminance,
 * assombrissement...) et pour l'upload GPU (`SDL_FColor`, uniforms de
 * shader) — voir @ref Color pour l'équivalent 8 bits/composante utilisé pour
 * le stockage et l'interop SDL directe.
 *
 * Une méthode fabrique statique par couleur de @ref LIST_COLORS est générée
 * ci-dessous (`FColor::RED()`, `FColor::WHITE()`, ...), avec conversion
 * automatique des octets hexadécimaux (0-255) vers [0, 1].
 */
struct FColor {
	float r, g, b, a;

	#define _c(name, r, g, b, a)                                                                                     \
		[[nodiscard]] static constexpr FColor name() {                                                               \
			return {(0x##r) / 255.f, (0x##g) / 255.f, (0x##b) / 255.f, (0x##a) / 255.f};                             \
		}
	LIST_COLORS
	#undef _c

	// =====================================================================
	// Construction
	// =====================================================================

	/// Construit depuis des composantes déjà normalisées ; toute valeur hors
	/// [0, 1] est clampée (voir la garantie de classe ci-dessus).
	constexpr FColor(
		float r_ = 0.f,
		float g_ = 0.f,
		float b_ = 0.f,
		float a_ = 0.f)
		: r(Clamp(r_, 0.f, 1.f))
		, g(Clamp(g_, 0.f, 1.f))
		, b(Clamp(b_, 0.f, 1.f))
		, a(Clamp(a_, 0.f, 1.f)) {
	}

	/// `SDL_FColor` est déjà normalisé (0.0-1.0) : copie directe, sans conversion.
	constexpr FColor(const SDL_FColor& color)
		: r(color.r),
		  g(color.g),
		  b(color.b),
		  a(color.a) {
	}

	/// Convertit depuis @ref Color : les octets (0-255) sont normalisés vers [0, 1].
	constexpr FColor(const Color& color)
		: r(color.r / 255.f),
		  g(color.g / 255.f),
		  b(color.b / 255.f),
		  a(color.a / 255.f) {
	}

	// =====================================================================
	// Conversion SDL
	// =====================================================================

	/// Conversion vers `SDL_Color` (octets 0-255) — troncature, pas d'arrondi.
	[[nodiscard]]
	constexpr operator SDL_Color() const {
		return {
			static_cast<Uint8>(r * 255.f),
			static_cast<Uint8>(g * 255.f),
			static_cast<Uint8>(b * 255.f),
			static_cast<Uint8>(a * 255.f)
		};
	}

	/// Conversion vers `SDL_FColor` : déjà le même format, copie directe.
	[[nodiscard]]
	constexpr operator SDL_FColor() const {
		return { r, g, b, a };
	}

	/// Équivalent explicite de `operator SDL_Color()`, mais vers @ref Color
	/// (le type de la bibliothèque plutôt que le type C de SDL).
	[[nodiscard]] constexpr Color ToByte() const {
		return {uint8_t(r * 255), uint8_t(g * 255), uint8_t(b * 255), uint8_t(a * 255)};
	}


	// =====================================================================
	// Comparaison
	// =====================================================================

	/// Égalité exacte des 4 composantes (pas de tolérance — deux couleurs
	/// visuellement identiques mais issues de calculs flottants différents
	/// peuvent comparer inégales).
	[[nodiscard]]
	constexpr bool operator==(const FColor& c) const {
		return r == c.r &&
				g == c.g &&
				b == c.b &&
				a == c.a;
	}

	[[nodiscard]]
	constexpr bool operator!=(const FColor& c) const {
		return !(*this == c);
	}


	// =====================================================================
	// Opérations entre couleurs
	// =====================================================================
	// Arithmétique composante par composante (RGBA). Le résultat repasse par
	// le constructeur clampant : chaque composante reste dans [0, 1] même si
	// l'opération dépasserait ces bornes (ex. addition de deux couleurs claires).

	[[nodiscard]]
	constexpr FColor operator+(const FColor& c) const {
		return {
			r + c.r,
			g + c.g,
			b + c.b,
			a + c.a
		};
	}

	[[nodiscard]]
	constexpr FColor operator-(const FColor& c) const {
		return {
			r - c.r,
			g - c.g,
			b - c.b,
			a - c.a
		};
	}

	[[nodiscard]]
	constexpr FColor operator*(const FColor& c) const {
		return {
			r * c.r,
			g * c.g,
			b * c.b,
			a * c.a
		};
	}

	[[nodiscard]]
	constexpr FColor operator/(const FColor& c) const {
		return {
			c.r != 0.f ? r / c.r : 0.f,
			c.g != 0.f ? g / c.g : 0.f,
			c.b != 0.f ? b / c.b : 0.f,
			c.a != 0.f ? a / c.a : 0.f
		};
	}


	// =====================================================================
	// Opérations avec un scalaire
	// =====================================================================
	// Comme ci-dessus (résultat clampé), mais avec un facteur unique appliqué
	// aux 4 composantes — pratique pour un fondu global (`color * alpha`).

	[[nodiscard]]
	constexpr FColor operator*(float value) const {
		return {
			r * value,
			g * value,
			b * value,
			a * value
		};
	}

	/// Division par zéro : retourne la couleur par défaut (noir transparent)
	/// plutôt que de propager `inf`/`nan`.
	[[nodiscard]]
	constexpr FColor operator/(float value) const {
		if (value == 0.f)
			return {};

		return {
			r / value,
			g / value,
			b / value,
			a / value
		};
	}


	// =====================================================================
	// Assignations
	// =====================================================================
	// Formes composées (`+=`, `*=`, ...) des opérateurs binaires ci-dessus —
	// mêmes garanties de clamping, rien de spécifique à documenter par méthode.

	constexpr FColor& operator+=(const FColor& c) {
		return *this = *this + c;
	}

	constexpr FColor& operator-=(const FColor& c) {
		return *this = *this - c;
	}

	constexpr FColor& operator*=(const FColor& c) {
		return *this = *this * c;
	}

	constexpr FColor& operator/=(const FColor& c) {
		return *this = *this / c;
	}

	constexpr FColor& operator*=(float value) {
		return *this = *this * value;
	}

	constexpr FColor& operator/=(float value) {
		return *this = *this / value;
	}


	// =====================================================================
	// Darken
	// =====================================================================
	// Convention utilisée pour Darken/Lighten/Invert/Grayscale/Saturate/
	// Desaturate ci-après : la forme au présent (`Darken`) modifie *this en
	// place et le retourne (chaînable) ; la forme au participe passé
	// (`Darkened`) opère sur une copie et laisse l'original intact — même
	// paire que `Reset`/`Reseted` n'existe pas dans ce fichier, mais le
	// principe est identique à `Applied`/`AppliedRGB` plus bas.

	/**
	 * @brief Assombrit vers le noir (alpha inchangé).
	 *
	 * `factor` est clampé à [0, 1] : 0 donne noir (RGB à 0), 1 laisse la
	 * couleur inchangée.
	 *
	 * @code
	 * FColor c{1.f, .5f, .2f};
	 * c.Darken(0.5f); // -> {0.5f, 0.25f, 0.1f, ...}
	 * @endcode
	 */
	constexpr FColor& Darken(float factor) {
		factor = Clamp(factor, 0.f, 1.f);

		r *= factor;
		g *= factor;
		b *= factor;

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Darkened(float factor) const {
		FColor result = *this;
		return result.Darken(factor);
	}


	// =====================================================================
	// Lighten
	// =====================================================================

	/**
	 * @brief Éclaircit vers le blanc (alpha inchangé).
	 *
	 * `factor` clampé à [0, 1] : 0 laisse la couleur inchangée, 1 donne
	 * blanc. Interpolation composante par composante vers 1.0 (pas une
	 * simple addition — évite tout dépassement).
	 */
	constexpr FColor& Lighten(float factor) {
		factor = Clamp(factor, 0.f, 1.f);

		r += (1.f - r) * factor;
		g += (1.f - g) * factor;
		b += (1.f - b) * factor;

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Lightened(float factor) const {
		FColor result = *this;
		return result.Lighten(factor);
	}


	// =====================================================================
	// Inversion
	// =====================================================================

	/// Inverse RGB (`1 - composante`) ; l'alpha n'est volontairement pas
	/// touché (inverser la transparence n'a généralement pas de sens visuel).
	constexpr FColor& Invert() {
		r = 1.f - r;
		g = 1.f - g;
		b = 1.f - b;

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Inverted() const {
		FColor result = *this;
		return result.Invert();
	}


	// =====================================================================
	// Grayscale
	// =====================================================================

	constexpr FColor& Grayscale() {
		// Luminance perceptuelle.
		const float luminance =
			r * 0.2126f +
			g * 0.7152f +
			b * 0.0722f;

		r = luminance;
		g = luminance;
		b = luminance;

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Grayscaled() const {
		FColor result = *this;
		return result.Grayscale();
	}


	// =====================================================================
	// Saturation
	// =====================================================================

	constexpr FColor& Saturate(float factor) {
		const float luminance =
			r * 0.2126f +
			g * 0.7152f +
			b * 0.0722f;

		factor = std::max(factor, 0.f);

		r = luminance + (r - luminance) * factor;
		g = luminance + (g - luminance) * factor;
		b = luminance + (b - luminance) * factor;

		r = Clamp(r, 0.f, 1.f);
		g = Clamp(g, 0.f, 1.f);
		b = Clamp(b, 0.f, 1.f);

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Saturated(float factor) const {
		FColor result = *this;
		return result.Saturate(factor);
	}


	// =====================================================================
	// Désaturation
	// =====================================================================

	constexpr FColor& Desaturate(float factor) {
		factor = Clamp(factor, 0.f, 1.f);

		const float luminance =
			r * 0.2126f +
			g * 0.7152f +
			b * 0.0722f;

		r += (luminance - r) * factor;
		g += (luminance - g) * factor;
		b += (luminance - b) * factor;

		return *this;
	}

	[[nodiscard]]
	constexpr FColor Desaturated(float factor) const {
		FColor result = *this;
		return result.Desaturate(factor);
	}


	// =====================================================================
	// Alpha
	// =====================================================================

	constexpr FColor& SetAlpha(float alpha) {
		a = Clamp(alpha, 0.f, 1.f);
		return *this;
	}

	constexpr FColor& MultiplyAlpha(float factor) {
		a = Clamp(a * factor, 0.f, 1.f);
		return *this;
	}

	[[nodiscard]]
	constexpr FColor WithAlpha(float alpha) const {
		FColor result = *this;
		return result.SetAlpha(alpha);
	}


	// =====================================================================
	// Lerp
	// =====================================================================

	[[nodiscard]]
	static constexpr FColor Lerp(
		const FColor& a,
		const FColor& b,
		float factor, float alpha = 0.f) {
		factor = Clamp(factor, 0.f, 1.f);

		return {
			a.r + (b.r - a.r) * factor,
			a.g + (b.g - a.g) * factor,
			a.b + (b.b - a.b) * factor,
			(alpha == 0.f) ? (a.a + (b.a - a.a) * factor) : alpha
		};
	}


	constexpr FColor& LerpTo(
		const FColor& other,
		float factor) {
		*this = Lerp(*this, other, factor);
		return *this;
	}


	// =====================================================================
	// Blend
	// =====================================================================

	[[nodiscard]]
	static constexpr FColor Blend(
		const FColor& source,
		const FColor& destination) {
		const float outAlpha =
			source.a + destination.a * (1.f - source.a);

		if (outAlpha <= 0.f)
			return {};

		return {
			(
				source.r * source.a +
				destination.r * destination.a * (1.f - source.a)
			) / outAlpha,

			(
				source.g * source.a +
				destination.g * destination.a * (1.f - source.a)
			) / outAlpha,

			(
				source.b * source.a +
				destination.b * destination.a * (1.f - source.a)
			) / outAlpha,

			outAlpha
		};
	}


	// =====================================================================
	// Multiply blending
	// =====================================================================

	[[nodiscard]]
	constexpr FColor Multiply(const FColor& c) const {
		return {
			r * c.r,
			g * c.g,
			b * c.b,
			a
		};
	}


	// =====================================================================
	// Screen blending
	// =====================================================================

	[[nodiscard]]
	constexpr FColor Screen(const FColor& c) const {
		return {
			1.f - (1.f - r) * (1.f - c.r),
			1.f - (1.f - g) * (1.f - c.g),
			1.f - (1.f - b) * (1.f - c.b),
			a
		};
	}


	// =====================================================================
	// Overlay blending
	// =====================================================================

	[[nodiscard]]
	constexpr FColor Overlay(const FColor& c) const {
		const auto overlay = [](float base, float blend)
		{
			return base < 0.5f
				? 2.f * base * blend
				: 1.f - 2.f * (1.f - base) * (1.f - blend);
		};

		return {
			overlay(r, c.r),
			overlay(g, c.g),
			overlay(b, c.b),
			a
		};
	}


	// =====================================================================
	// Apply
	// =====================================================================

	/*
		* Applique une fonction à chaque composante RGBA.
		*
		* Exemple :
		*
		* color.Apply([](float x) {
		*     return x * x;
		* });
		*/

	template<std::invocable<float> F>
	constexpr FColor& Apply(F&& function) {
		r = Clamp(
			static_cast<float>(std::invoke(function, r)),
			0.f,
			1.f
		);

		g = Clamp(
			static_cast<float>(std::invoke(function, g)),
			0.f,
			1.f
		);

		b = Clamp(
			static_cast<float>(std::invoke(function, b)),
			0.f,
			1.f
		);

		a = Clamp(
			static_cast<float>(std::invoke(function, a)),
			0.f,
			1.f
		);

		return *this;
	}


	template<std::invocable<float> F>
	[[nodiscard]]
	FColor Applied(F&& function) const {
		FColor result = *this;
		result.Apply(std::forward<F>(function));
		return result;
	}


	// =====================================================================
	// ApplyRGB
	// =====================================================================

	template<std::invocable<float> F>
	constexpr FColor& ApplyRGB(F&& function) {
		r = Clamp(
			static_cast<float>(std::invoke(function, r)),
			0.f,
			1.f
		);

		g = Clamp(
			static_cast<float>(std::invoke(function, g)),
			0.f,
			1.f
		);

		b = Clamp(
			static_cast<float>(std::invoke(function, b)),
			0.f,
			1.f
		);

		return *this;
	}


	template<std::invocable<float> F>
	[[nodiscard]]
	FColor AppliedRGB(F&& function) const {
		FColor result = *this;
		result.ApplyRGB(std::forward<F>(function));
		return result;
	}


	// =====================================================================
	// ApplyAlpha
	// =====================================================================

	/// Comme @ref Apply, mais uniquement sur l'alpha — RGB inchangé.
	template<std::invocable<float> F>
	constexpr FColor& ApplyAlpha(F&& function) {
		a = Clamp(
			static_cast<float>(std::invoke(function, a)),
			0.f,
			1.f
		);

		return *this;
	}


	template<std::invocable<float> F>
	[[nodiscard]]
	FColor AppliedAlpha(F&& function) const {
		FColor result = *this;
		result.ApplyAlpha(std::forward<F>(function));
		return result;
	}
};

// ============================================================================
// Point / FPoint
// ============================================================================

struct FPoint;

struct Point {
	int x = 0, y = 0;

	constexpr Point() = default;
	constexpr Point(int x, int y) : x(x), y(y) {}
	constexpr explicit Point(SDL_Point p) : x(p.x), y(p.y) {}
	constexpr explicit Point(FPoint p);

	[[nodiscard]] constexpr operator SDL_Point() const { return {x, y}; }

	[[nodiscard]] constexpr Point operator+(Point o) const { return {x + o.x, y + o.y}; }
	[[nodiscard]] constexpr Point operator-(Point o) const { return {x - o.x, y - o.y}; }
	[[nodiscard]] constexpr Point operator*(int s) const { return {x * s, y * s}; }
	[[nodiscard]] constexpr Point operator/(int s) const { return {x / s, y / s}; }

	constexpr bool operator==(const Point &other) const { return (x == other.x) && (y == other.y); }
};

struct FPoint {
	float x = 0.f, y = 0.f;

	constexpr FPoint() = default;
	constexpr FPoint(float x, float y) : x(x), y(y) {}
	constexpr explicit FPoint(SDL_FPoint p) : x(p.x), y(p.y) {}
	constexpr explicit FPoint(Point p) : x(float(p.x)), y(float(p.y)) {}

	[[nodiscard]] constexpr operator SDL_FPoint() const { return {x, y}; }
	[[nodiscard]] constexpr operator SDL_Point() const {
		return {static_cast<int>(Round(x)), static_cast<int>(Round(y))};
	}

	[[nodiscard]] constexpr FPoint operator+(FPoint o) const { return {x + o.x, y + o.y}; }
	[[nodiscard]] constexpr FPoint operator-(FPoint o) const { return {x - o.x, y - o.y}; }
	[[nodiscard]] constexpr FPoint operator*(float s) const { return {x * s, y * s}; }
	[[nodiscard]] constexpr FPoint operator/(float s) const { return {x / s, y / s}; }

	constexpr bool operator==(const FPoint &other) const { return (x == other.x) && (y == other.y); }
};

constexpr Point::Point(FPoint r) :
	x(static_cast<int>(Round(r.x))), y(static_cast<int>(Round(r.y))) {}

// ============================================================================
// Rect / FRect
// ============================================================================

struct FRect;

struct Rect {
	int x = 0, y = 0, w = 0, h = 0;

	constexpr Rect() = default;
	constexpr Rect(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {}
	constexpr Rect(Point pos, Point size) : x(pos.x), y(pos.y), w(size.x), h(size.y) {}
	constexpr explicit Rect(SDL_Rect r) : x(r.x), y(r.y), w(r.w), h(r.h) {}
	constexpr explicit Rect(FRect r);

	[[nodiscard]] constexpr operator SDL_Rect() const { return {x, y, w, h}; }

	[[nodiscard]] constexpr Point Pos() const { return {x, y}; }
	[[nodiscard]] constexpr Point GetSize() const { return {w, h}; }
	[[nodiscard]] constexpr Point Center() const { return {x + w / 2, y + h / 2}; }
	[[nodiscard]] constexpr bool Contains(Point p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
	[[nodiscard]] constexpr bool Intersects(Rect o) const {
		return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y;
	}
	constexpr bool operator==(const Rect &other) const {
		return (x == other.x) && (y == other.y) && (w == other.w) && (h == other.h);
	}
};

struct FRect {
	float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

	constexpr FRect() = default;
	constexpr FRect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}
	constexpr FRect(FPoint pos, FPoint size) : x(pos.x), y(pos.y), w(size.x), h(size.y) {}
	constexpr explicit FRect(SDL_FRect r) : x(r.x), y(r.y), w(r.w), h(r.h) {}
	constexpr explicit FRect(Rect r) : x(float(r.x)), y(float(r.y)), w(float(r.w)), h(float(r.h)) {}

	[[nodiscard]] constexpr operator SDL_FRect() const { return {x, y, w, h}; }
	[[nodiscard]] constexpr operator SDL_Rect() const {
		return {static_cast<int>(Round(x)), static_cast<int>(Round(y)),
				static_cast<int>(Round(w)), static_cast<int>(Round(h))};
	}

	[[nodiscard]] constexpr FPoint Pos() const { return {x, y}; }
	[[nodiscard]] constexpr FPoint GetSize() const { return {w, h}; }
	[[nodiscard]] constexpr FPoint Center() const { return {x + w * .5f, y + h * .5f}; }
	[[nodiscard]] constexpr bool Contains(FPoint p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
	[[nodiscard]] constexpr bool Intersects(const FRect &o) const {
		return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y;
	}
	/// Rectangle d'intersection (taille nulle si disjoint).
	[[nodiscard]] constexpr FRect Intersection(const FRect &o) const {
		float x1 = x > o.x ? x : o.x;
		float y1 = y > o.y ? y : o.y;
		float x2 = (x + w < o.x + o.w) ? x + w : o.x + o.w;
		float y2 = (y + h < o.y + o.h) ? y + h : o.y + o.h;
		return {x1, y1, x2 > x1 ? x2 - x1 : 0.f, y2 > y1 ? y2 - y1 : 0.f};
	}
	constexpr bool operator==(const FRect &other) const {
		return (x == other.x) && (y == other.y) && (w == other.w) && (h == other.h);
	}
};

constexpr Rect::Rect(FRect r) :
	x(static_cast<int>(Round(r.x))), y(static_cast<int>(Round(r.y))),
	w(static_cast<int>(Round(r.w))), h(static_cast<int>(Round(r.h))) {}

// ============================================================================
// Corners / Sides — rayons de coins et épaisseurs de bords (types génériques :
// bordures arrondies de Renderer::DrawRoundedRect/FillRoundedRect ci-dessus,
// padding/marges du module ui...). Définis ICI (sdl3::, pas math::) pour
// casser un cycle d'inclusion : render.hpp (juste au-dessus dans sdl3.hpp)
// les utilise, et math.hpp inclut sdl3.hpp avant d'avoir eu la chance de
// définir quoi que ce soit lui-même — math::Corners/math::Sides restent
// disponibles comme de simples alias (voir math.hpp) pour tout le code
// existant qui les nomme ainsi.
// ============================================================================

struct Corners {
	float tl = 0.f, tr = 0.f, bl = 0.f, br = 0.f;

	constexpr Corners() = default;
	constexpr explicit Corners(float radius) : tl(radius), tr(radius), bl(radius), br(radius) {}
	constexpr Corners(float tl, float tr, float bl, float br) : tl(tl), tr(tr), bl(bl), br(br) {}
};

struct Sides {
	float left = 0.f, top = 0.f, right = 0.f, bottom = 0.f;

	constexpr Sides() = default;
	constexpr explicit Sides(float width) : left(width), top(width), right(width), bottom(width) {}
	constexpr Sides(float horizontal, float vertical)
		: left(horizontal), top(vertical), right(horizontal), bottom(vertical) {}
	constexpr Sides(float left, float top, float right, float bottom)
		: left(left), top(top), right(right), bottom(bottom) {}

	/// Étendue horizontale totale (left + right).
	[[nodiscard]] constexpr float H() const noexcept { return left + right; }
	/// Étendue verticale totale (top + bottom).
	[[nodiscard]] constexpr float V() const noexcept { return top + bottom; }
};

// ============================================================================
// Vertex
// ============================================================================

struct Vertex {
	FPoint position;  /**< Vertex position, in SDL_Renderer coordinates  */
	FColor color;     /**< Vertex color */
	FPoint texCoord; /**< Normalized texture coordinates, if needed */

	constexpr Vertex() = default;
	constexpr Vertex(const FPoint& position, const FColor& color, const FPoint& texCoord)
		: position(position), color(color), texCoord(texCoord) {}
	constexpr explicit Vertex(const SDL_Vertex& v) : position(v.position), color(v.color), texCoord(v.tex_coord) {}

	[[nodiscard]] constexpr operator SDL_Vertex() const { return {position, color, texCoord}; }

	[[nodiscard]] constexpr bool operator==(const Vertex &other) const {
		return position == other.position && color == other.color && texCoord == other.texCoord;
	}
};

// ============================================================================
// Enums
// ============================================================================

enum class BlendMode : int {
	NONE	= SDL_BLENDMODE_NONE,
	BLEND	= SDL_BLENDMODE_BLEND,
	ADD		= SDL_BLENDMODE_ADD,
	MOD		= SDL_BLENDMODE_MOD,
	MUL		= SDL_BLENDMODE_MUL,
};

enum class FlipMode {
	NONE		= SDL_FLIP_NONE,
	Horizontal	= SDL_FLIP_HORIZONTAL,
	Vertical	= SDL_FLIP_VERTICAL,
};

} // namespace sdl3
