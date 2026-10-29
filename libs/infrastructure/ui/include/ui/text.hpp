#pragma once
/**
 * ui::text — mesure et découpe de texte, indépendantes de toute police.
 *
 * SDL_ttf ne traite PAS les caractères de contrôle de la même façon selon
 * qu'on mesure ou qu'on dessine (`\n` compté comme un glyphe manquant à la
 * mesure mais respecté au dessin, `\t` dessiné comme une boîte…). Tout texte
 * affiché passe donc d'abord par `NormalizeDisplayText` (`\r\n`/`\r` → `\n`,
 * tabulations développées jusqu'au taquet suivant, autres contrôles retirés),
 * puis est mesuré LIGNE PAR LIGNE : ce qui est réservé par la mise en page est
 * exactement ce qui est dessiné.
 *
 * Toutes les fonctions reçoivent la mesure d'UNE ligne (`LineMeasure`) : la
 * police réelle quand le contexte en a une, l'heuristique sinon (tests sans
 * fenêtre).
 */
#include <functional>
#include <string_view>
#include <vector>

#include <core/core.hpp>
#include <sdl3/structs.hpp>

namespace ui {

/// Mesure d'une ligne déjà normalisée (largeur, hauteur) en pixels.
using LineMeasure = std::function<sdl3::FPoint(const String &)>;

[[nodiscard]] constexpr float CharWidthApprox(float fontSize) noexcept { return fontSize * 0.55f; }
[[nodiscard]] constexpr float LineHeightApprox(float fontSize) noexcept { return fontSize * 1.3f; }

inline constexpr int DEFAULT_TAB_STOP = 4;

/// Cellules d'affichage d'un point de code (à la manière de wcwidth) : 0
/// (marques combinantes, liants), 1, ou 2 (CJK, émojis).
[[nodiscard]] constexpr int CodepointCells(char32_t cp) noexcept {
	if (cp == 0)
		return 0;
	if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) || (cp >= 0x0591 && cp <= 0x05BD) ||
		(cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
		(cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2060 && cp <= 0x2064) ||
		(cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F) ||
		cp == 0xFEFF || (cp >= 0xE0100 && cp <= 0xE01EF))
		return 0;
	if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) || (cp >= 0x3041 && cp <= 0x33FF) ||
		(cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
		(cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE4F) ||
		(cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
		(cp >= 0x1F680 && cp <= 0x1F6FF) || (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x1FA70 && cp <= 0x1FAFF) ||
		(cp >= 0x20000 && cp <= 0x3FFFD))
		return 2;
	return 1;
}

/// Point de code suivant d'une chaîne UTF-16 (paires de substitution
/// recombinées, moitié isolée = U+FFFD).
[[nodiscard]] constexpr char32_t NextUtf16Codepoint(std::u16string_view text, size_t &i) noexcept {
	const char16_t unit = text[i++];
	if (unit >= 0xD800 && unit <= 0xDBFF) {
		if (i < text.size() && text[i] >= 0xDC00 && text[i] <= 0xDFFF) {
			const char16_t low = text[i++];
			return 0x10000 + ((char32_t(unit) - 0xD800) << 10) + (char32_t(low) - 0xDC00);
		}
		return 0xFFFD;
	}
	if (unit >= 0xDC00 && unit <= 0xDFFF)
		return 0xFFFD;
	return unit;
}

/// Nombre de caractères (points de code).
[[nodiscard]] size_t CharCount(const String &text) noexcept;
[[nodiscard]] constexpr size_t CharCount(std::u16string_view text) noexcept {
	size_t count = 0;
	for (size_t i = 0; i < text.size(); ++count)
		(void)NextUtf16Codepoint(text, i);
	return count;
}
[[nodiscard]] constexpr size_t CharCount(std::u32string_view text) noexcept { return text.size(); }

/// Largeur en cellules d'affichage.
[[nodiscard]] size_t DisplayCells(const String &text) noexcept;
[[nodiscard]] constexpr size_t DisplayCells(std::u16string_view text) noexcept {
	size_t cells = 0;
	for (size_t i = 0; i < text.size();)
		cells += size_t(CodepointCells(NextUtf16Codepoint(text, i)));
	return cells;
}
[[nodiscard]] constexpr size_t DisplayCells(std::u32string_view text) noexcept {
	size_t cells = 0;
	for (char32_t cp : text)
		cells += size_t(CodepointCells(cp));
	return cells;
}

/// Cellules du caractère UTF-8 commençant à l'octet `i`.
[[nodiscard]] int CellsAt(const String &text, size_t i) noexcept;
[[nodiscard]] bool IsNonPrintableControl(unsigned char c) noexcept;

/// Forme affichable (cf. en-tête). Sans caractère de contrôle : rendue telle quelle.
[[nodiscard]] String NormalizeDisplayText(const String &text, int tabStop = DEFAULT_TAB_STOP);

/// Colonne d'affichage (cellules) de l'octet `byteOffset` de `line`.
[[nodiscard]] size_t DisplayColumn(const String &line, size_t byteOffset, int tabStop = DEFAULT_TAB_STOP) noexcept;
[[nodiscard]] size_t DisplayColumns(const String &line, int tabStop = DEFAULT_TAB_STOP) noexcept;
/// Inverse de DisplayColumn, toujours sur une frontière de caractère.
[[nodiscard]] size_t ColumnToByteOffset(const String &line, size_t column, int tabStop = DEFAULT_TAB_STOP) noexcept;

/// Recule / avance d'un point de code UTF-8.
[[nodiscard]] size_t PrevCodepoint(const String &text, size_t pos) noexcept;
[[nodiscard]] size_t NextCodepoint(const String &text, size_t pos) noexcept;
/// Début / fin du mot autour de `pos` (double-clic, Ctrl+flèches).
[[nodiscard]] size_t WordStart(const String &text, size_t pos) noexcept;
[[nodiscard]] size_t WordEnd(const String &text, size_t pos) noexcept;

/// Place occupée par un texte (sauts de ligne compris).
struct TextMetrics {
	float width = 0.f;
	float height = 0.f;
	float lineHeight = 0.f;
	size_t lineCount = 1;
};

[[nodiscard]] TextMetrics MeasureTextBlock(const String &text, float fontSize, const LineMeasure &measureLine,
										   int tabStop = DEFAULT_TAB_STOP);

/// Ligne issue du retour automatique.
struct WrappedLine {
	String text;
	float width = 0.f;
	bool lastOfParagraph = false;
	size_t offset = 0; ///< octet de début dans le texte NORMALISÉ
};

/// Découpe aux espaces dans `maxWidth` (mot trop long coupé au caractère,
/// sauts de ligne explicites respectés).
[[nodiscard]] std::vector<WrappedLine> WrapTextToWidth(const String &text, float maxWidth,
													   const LineMeasure &measureLine,
													   int tabStop = DEFAULT_TAB_STOP);

/// Ligne logique d'un texte éditable, avec son décalage en octets.
struct TextLine {
	size_t offset = 0;
	String text;
};

/// Découpe sur `\n` (un `\r` final est retiré de l'affichage, pas des décalages).
[[nodiscard]] std::vector<TextLine> SplitLines(const String &text);
/// Index de la ligne contenant `pos`.
[[nodiscard]] size_t LineIndexOf(const std::vector<TextLine> &lines, size_t pos) noexcept;

/// Plus long préfixe de `text` qui tienne dans `maxWidth` une fois « … »
/// ajouté (recherche dichotomique sur les frontières de caractères) ; chaîne
/// vide si même « … » ne tient pas.
[[nodiscard]] String TruncateWithEllipsis(const String &text, float maxWidth, const LineMeasure &measureLine);

/// Mesure heuristique sans police (cellules × 0,55 em, 1,3 em de haut).
[[nodiscard]] LineMeasure HeuristicMeasure(float fontSize);

} // namespace ui
