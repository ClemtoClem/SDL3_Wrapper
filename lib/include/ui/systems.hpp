#pragma once
/**
 * ui::systems — les trois passes du pipeline UI :
 *
 *   LayoutSystem — measure (post-ordre, mémoïsé) + place (pré-ordre) ;
 *                  ne recalcule QUE si marqué dirty ou si la fenêtre a changé
 *                  (dirty-flag à la GTK4 : une UI au repos coûte ~0).
 *   InputSystem  — hover/press/click/drag/focus + callbacks applicatifs.
 *   RenderSystem — dessin via sdl3::Renderer (rects arrondis, dégradés,
 *                  cercles) + texte TTF avec cache d'objets sdl3::Text.
 */
#include <algorithm>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "../sdl3/events.hpp"
#include "../sdl3/input.hpp"
#include "../sdl3/render.hpp"
#include "../sdl3/ttf.hpp"
#include "components.hpp"
#include "interaction.hpp"
#include "plot.hpp"
#include "render_backend.hpp"
#include "styles.hpp"
#include "viewport3d.hpp"

namespace ui {

// ── Helpers ──────────────────────────────────────────────────────────────────
// (l'intersection et le test de contenance viennent de sdl3::FRect)

[[nodiscard]] float ClampOpt(float v, const Option<float> &lo, const Option<float> &hi) noexcept;

// ============================================================================
// Auto-scrollbar geometry — barre fine dessinée en surimpression sur le bord
// droit/bas d'un conteneur `clipContent` dont le contenu déborde. Une seule
// source de vérité pour le rect du "pouce" (thumb), utilisée à la fois par
// RenderSystem (dessin) et InputSystem (hit-test + drag), pour qu'ils ne
// puissent jamais diverger.
// ============================================================================

constexpr float K_SCROLLBAR_THICKNESS = UiRect::SCROLLBAR_THICKNESS;
constexpr float K_SCROLLBAR_MIN_THUMB = 20.f;

/// Épaisseur de bordure d'un style résolu, telle que RenderSystem la dessine
/// (contours concentriques d'un pixel, `.top` représentatif des 4 côtés) :
/// LayoutSystem la réserve dans la boîte, pour que le contenu ne la recouvre
/// pas et qu'elle ne soit pas rognée.
[[nodiscard]] math::Sides BorderSides(const ResolvedStyle &rs) noexcept;

/// Piste verticale : bande réservée au bord droit (cf. UiRect::gutter), qui
/// s'arrête au-dessus de la piste horizontale.
[[nodiscard]] sdl3::FRect VScrollbarTrackRect(const UiRect &r, const sdl3::FRect &screen) noexcept;

/// Piste horizontale : bande réservée au bord bas.
[[nodiscard]] sdl3::FRect HScrollbarTrackRect(const UiRect &r, const sdl3::FRect &screen) noexcept;

/// Rect (dans l'espace écran) du pouce vertical, ou un rect vide si pas de
/// débordement vertical.
[[nodiscard]] sdl3::FRect VScrollbarThumbRect(const UiRect &r, const sdl3::FRect &screen) noexcept;

/// Rect (dans l'espace écran) du pouce horizontal, ou un rect vide si pas de
/// débordement horizontal.
[[nodiscard]] sdl3::FRect HScrollbarThumbRect(const UiRect &r, const sdl3::FRect &screen) noexcept;

// ============================================================================
// Découpage de texte pour UiInput/UiInputArea — une seule source de vérité
// (heuristique caractère fixe, PAS de mesure TTF réelle) partagée entre
// InputSystem (calcul de UiRect.content pour le scroll/la scrollbar) et
// RenderSystem (dessin) : s'ils mesuraient différemment, la hauteur/largeur
// de contenu ne correspondrait pas aux lignes réellement dessinées. Même
// approximation que LayoutSystem::measureText par défaut.
// ============================================================================

[[nodiscard]] constexpr float CharWidthApprox(float fontSize) noexcept { return fontSize * 0.55f; }
[[nodiscard]] constexpr float LineHeightApprox(float fontSize) noexcept { return fontSize * 1.3f; }

// ============================================================================
// Clip : du rect flottant au rect entier
// ============================================================================

/**
 * Convertit un rect de clip flottant en rect entier, bords arrondis VERS
 * L'EXTÉRIEUR (floor en haut-gauche, ceil en bas-droite).
 *
 * L'ancienne forme, `{int(x), int(y), int(w) + 1, int(h) + 1}`, tronquait
 * l'origine (donc pouvait la remonter d'un pixel) PUIS ajoutait un pixel
 * plein à la taille : la région autorisée débordait ainsi jusqu'à deux pixels
 * sur les bords bas et droit. Assez pour qu'un glyphe morde sur la bordure du
 * widget parent — ce qui se voyait sur les champs de texte.
 *
 * Arrondir vers l'extérieur (et non vers l'intérieur) reste le bon choix :
 * rogner mangerait une ligne de pixels du contenu à CHAQUE niveau
 * d'imbrication, alors qu'ici le débordement est borné à moins d'un pixel et
 * ne s'accumule pas (le clip d'un enfant est déjà l'intersection de celui de
 * son parent).
 */
/**
 * Zone de CONTENU d'un widget encadré : sa boîte moins l'épaisseur de sa
 * bordure. Le texte d'un champ doit y rester : un cadre dont le trait est
 * traversé par un glyphe se lit comme un défaut d'affichage, et c'est
 * exactement ce que montrait le champ de script de l'éditeur (première ligne
 * coupée en deux par la bordure haute).
 */
[[nodiscard]] sdl3::FRect InsetRect(const sdl3::FRect &r, float inset) noexcept;

/// Marge de confort entre le bord d'un widget et son texte, quand le widget
/// l'a réservée dans sa mesure (cf. RenderSystem::DrawTextCentered).
inline constexpr float TEXT_INSET = 8.f;

/**
 * Abscisse à laquelle poser un texte de largeur `textWidth` dans `box`.
 *
 * La marge `inset` suppose que le widget l'a RÉSERVÉE dans sa mesure — c'est
 * le cas d'un bouton (sa mesure ajoute 24 px), pas d'un `UiLabel`, dont la
 * boîte fait exactement la largeur du texte. Elle est donc rabotée quand il
 * n'y a pas la place : le texte ne sort jamais de sa propre boîte, quel que
 * soit l'alignement.
 */
[[nodiscard]] float TextOriginX(const sdl3::FRect &box, float textWidth, TextAlign align,
									   float inset = TEXT_INSET) noexcept;

/// Débordement maximal du halo de lueur « verre » autour d'un widget —
/// À GARDER ÉGAL au plus grand `outset` de RenderSystem::DrawGlowRing
/// (1 + (anneaux - 1) x 1,5, plus l'épaisseur du trait). C'est le seul
/// dessin qui sort de la boîte d'un widget PAR CONCEPTION, et donc la seule
/// marge que le clip lui accorde.
inline constexpr float GLOW_MAX_OUTSET = 5.f;

/// Épaisseur de bordure des champs encadrés (cf. DrawRoundedRect).
inline constexpr float FIELD_BORDER = 1.5f;

[[nodiscard]] sdl3::Rect ToClipRect(const sdl3::FRect &r) noexcept;

// ============================================================================
// Codes d'échappement et caractères de contrôle
// ============================================================================
//
// SDL_ttf ne traite PAS les codes d'échappement de la même façon selon qu'on
// MESURE ou qu'on DESSINE, et c'est un piège coûteux (mesuré avec DejaVuSans
// 16 pt, cf. tests/ui_text_metrics_smoke_test.cpp) :
//
//     texte        TTF_GetStringSize (mesure)   TTF_Text (dessin)
//     "abc"                29 x 19                  29 x 19
//     "abc\nabc"           68 x 19  <-- FAUX        29 x 38  <-- correct
//     "a\tb"               30 x 19                  30 x 19  (boîte « glyphe manquant »)
//     "a\r\nb"             40 x 19                  10 x 38
//
// Autrement dit : la mesure compte chaque `\n`, `\r` ou `\t` comme un glyphe
// MANQUANT de ~10 px et reste sur UNE ligne, alors que le rendu, lui, casse
// bien la ligne sur `\n`. Le layout réservait donc une boîte d'une seule
// ligne, trop large, dans laquelle le texte débordait par le bas.
//
// La réponse tient en deux temps :
//  1. NormalizeDisplayText() met la chaîne sous une forme où mesure et dessin
//     ne peuvent plus diverger : `\r\n` et `\r` deviennent `\n`, les
//     tabulations deviennent des espaces jusqu'au taquet suivant, et les
//     autres caractères de contrôle (non affichables) disparaissent ;
//  2. MeasureTextBlock() mesure LIGNE PAR LIGNE : largeur = la plus large,
//     hauteur = nombre de lignes x hauteur de ligne.
//
// Pourquoi des taquets comptés en CARACTÈRES et non en pixels : le rendu d'un
// libellé est un seul objet TTF_Text ; pour aligner sur des taquets en pixels
// il faudrait dessiner segment par segment. En développant les tabulations en
// espaces une fois pour toutes, la mesure et le dessin voient EXACTEMENT la
// même chaîne — l'alignement est approximatif en police proportionnelle, mais
// ce qui est réservé correspond toujours à ce qui est dessiné, ce qui est la
// propriété dont dépend tout le reste du layout.

/// Nombre de CARACTÈRES d'une chaîne UTF-8 (les octets de continuation
/// 10xxxxxx ne comptent pas). L'heuristique de mesure sans police s'en sert :
/// compter les octets donnait « Réglages » pour 10 caractères au lieu de 8, et
/// surestimait donc de 25 % la largeur de tout texte accentué.
[[nodiscard]] size_t CharCount(const String &text) noexcept;

/// Largeur d'un taquet de tabulation, en caractères.
inline constexpr int DEFAULT_TAB_STOP = 4;

// ── Largeur d'affichage des caractères (UTF-8, UTF-16, UTF-32) ─────────────
//
// Compter les caractères ne suffit pas à estimer la place d'un texte : un
// idéogramme ou un émoji occupe DEUX cellules, une marque combinante (accent
// posé sur la lettre précédente), un sélecteur de variante ou un liant sans
// chasse n'en occupent AUCUNE. Et hors du plan multilingue de base (émojis…),
// un caractère pèse quatre octets en UTF-8, deux unités en UTF-16 (paire de
// substitution) et une seule en UTF-32 : chaque encodage doit être décodé en
// points de code avant d'être compté.

/// Cellules d'affichage d'un point de code (à la manière de wcwidth) : 0, 1
/// ou 2.
[[nodiscard]] constexpr int CodepointCells(char32_t cp) noexcept {
	if (cp == 0)
		return 0;
	// Sans chasse : marques combinantes, liants, sélecteurs de variante.
	if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) || (cp >= 0x0591 && cp <= 0x05BD) ||
		(cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
		(cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2060 && cp <= 0x2064) ||
		(cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F) ||
		cp == 0xFEFF || (cp >= 0xE0100 && cp <= 0xE01EF))
		return 0;
	// Pleine chasse : Hangul, CJK, formes pleine largeur, émojis.
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
/// recombinées ; une moitié isolée vaut U+FFFD), `i` avancé d'autant.
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

/// Nombre de CARACTÈRES (points de code) d'une chaîne UTF-16 : une paire de
/// substitution compte pour un.
[[nodiscard]] constexpr size_t CharCount(std::u16string_view text) noexcept {
	size_t count = 0;
	for (size_t i = 0; i < text.size(); ++count)
		(void)NextUtf16Codepoint(text, i);
	return count;
}

/// Nombre de caractères d'une chaîne UTF-32 : une unité = un point de code.
[[nodiscard]] constexpr size_t CharCount(std::u32string_view text) noexcept { return text.size(); }

/// Largeur d'affichage, en cellules, d'une chaîne UTF-8 (cf. CodepointCells).
[[nodiscard]] size_t DisplayCells(const String &text) noexcept;

/// Largeur d'affichage, en cellules, d'une chaîne UTF-16.
[[nodiscard]] constexpr size_t DisplayCells(std::u16string_view text) noexcept {
	size_t cells = 0;
	for (size_t i = 0; i < text.size();)
		cells += size_t(CodepointCells(NextUtf16Codepoint(text, i)));
	return cells;
}

/// Largeur d'affichage, en cellules, d'une chaîne UTF-32.
[[nodiscard]] constexpr size_t DisplayCells(std::u32string_view text) noexcept {
	size_t cells = 0;
	for (char32_t cp : text)
		cells += size_t(CodepointCells(cp));
	return cells;
}

/// Cellules du caractère UTF-8 qui commence à l'octet `i` de `text`.
[[nodiscard]] int CellsAt(const String &text, size_t i) noexcept;

/// Caractère de contrôle C0 (hors `\n`, `\r`, `\t`, traités à part) ou DEL :
/// aucun glyphe à dessiner, mais SDL_ttf en dessine une boîte.
[[nodiscard]] bool IsNonPrintableControl(unsigned char c) noexcept;

/**
 * Forme affichable d'une chaîne : `\r\n` et `\r` normalisés en `\n`,
 * tabulations développées jusqu'au taquet suivant, autres caractères de
 * contrôle retirés.
 *
 * Les colonnes sont comptées en CARACTÈRES et non en octets : une lettre
 * accentuée occupe deux octets en UTF-8 mais une seule colonne, et compter
 * les octets décalerait les taquets d'un texte français.
 *
 * Chaîne sans aucun caractère de contrôle (le cas courant) : rendue telle
 * quelle, sans allocation.
 */
[[nodiscard]] String NormalizeDisplayText(const String &text, int tabStop = DEFAULT_TAB_STOP);

// ── Grille d'un champ éditable ─────────────────────────────────────────────
//
// UiInput/UiInputArea placent curseur, sélection et clics sur une grille de
// cellules de largeur fixe (cf. CharWidthApprox) — une approximation assumée,
// mais qui doit décrire les MÊMES cellules que ce qui est dessiné. Depuis que
// le dessin développe les tabulations (cf. NormalizeDisplayText), compter les
// octets ne suffit plus : une tabulation occupe plusieurs cellules, et un
// caractère accentué en occupe UNE alors qu'il pèse deux octets.

/// Nombre de cellules occupées par `line` jusqu'à `byteOffset` (exclu).
[[nodiscard]] size_t DisplayColumn(const String &line, size_t byteOffset,
										  int tabStop = DEFAULT_TAB_STOP) noexcept;

/// Largeur d'une ligne entière, en cellules.
[[nodiscard]] size_t DisplayColumns(const String &line, int tabStop = DEFAULT_TAB_STOP) noexcept;

/// Inverse de DisplayColumn : décalage en octets de la cellule `column`.
///
/// Retombe toujours sur une frontière de caractère. Une cellule tombant AU
/// MILIEU d'une tabulation (qui en occupe plusieurs) est ramenée au bord le
/// plus proche — comme dans n'importe quel éditeur de texte : cliquer juste
/// après le texte place le curseur avant la tabulation, cliquer près du
/// caractère suivant le place après.
[[nodiscard]] size_t ColumnToByteOffset(const String &line, size_t column,
											   int tabStop = DEFAULT_TAB_STOP) noexcept;

/// Place occupée par un texte, sauts de ligne compris.
struct TextMetrics {
	float width = 0.f;      ///< largeur de la ligne la plus large
	float height = 0.f;     ///< lineCount * lineHeight
	float lineHeight = 0.f; ///< hauteur d'UNE ligne
	size_t lineCount = 1;   ///< toujours >= 1 (une chaîne vide occupe une ligne)
};

/**
 * Mesure un texte en tenant compte des codes d'échappement.
 *
 * `measureLine` mesure UNE ligne déjà normalisée (c'est le rôle de
 * `LayoutSystem::measureText`, que les applications remplacent par une vraie
 * mesure TTF) : découper les lignes ici plutôt que dans chaque application
 * fait que toutes en profitent sans changer une ligne de leur code.
 *
 * La hauteur de ligne retenue est la PLUS GRANDE rendue par `measureLine`
 * (une ligne vide rend souvent 0) avec repli sur `LineHeightApprox`, pour
 * qu'un texte se terminant par `\n` réserve bien une ligne de plus.
 */
[[nodiscard]] TextMetrics
MeasureTextBlock(const String &text, float fontSize,
				 const std::function<sdl3::FPoint(const String &, float)> &measureLine,
				 int tabStop = DEFAULT_TAB_STOP);

// ── Retour automatique à la ligne, mesuré ──────────────────────────────────
//
// `WrapText` (plus bas) découpe à la louche, en supposant des caractères de
// largeur fixe : c'est ce dont un champ de saisie a besoin, parce que sa
// grille de curseur fonctionne déjà ainsi. Un LIBELLÉ, lui, est dessiné avec
// la vraie police : le découper au caractère laisserait des lignes trop
// courtes ou débordantes. D'où cette seconde découpe, qui mesure réellement.

/// Une ligne issue du retour automatique, avec sa largeur mesurée.
struct WrappedLine {
	String text;
	float width = 0.f;
	/// Vrai pour la dernière ligne d'un paragraphe (elle n'est jamais
	/// justifiée : étirer les mots d'une fin de phrase la rendrait illisible).
	bool lastOfParagraph = false;
};

/**
 * Découpe `text` en lignes tenant dans `maxWidth`, en coupant AUX ESPACES.
 *
 * Un mot plus long que la largeur disponible est coupé au caractère (sinon il
 * déborderait indéfiniment) ; les sauts de ligne explicites sont respectés,
 * comme partout ailleurs (cf. NormalizeDisplayText).
 *
 * `measureLine` est la mesure d'une ligne, déjà normalisée — la même que
 * partout (cf. MeasureTextBlock).
 */
[[nodiscard]] std::vector<WrappedLine>
WrapTextToWidth(const String &text, float fontSize, float maxWidth,
				const std::function<sdl3::FPoint(const String &, float)> &measureLine,
				int tabStop = DEFAULT_TAB_STOP);

/// Hauteur occupée par `text` replié dans `maxWidth`.
[[nodiscard]] float WrappedHeight(const String &text, float fontSize, float maxWidth,
										 const std::function<sdl3::FPoint(const String &, float)> &measureLine,
										 int tabStop = DEFAULT_TAB_STOP);

/// Une ligne affichée (enveloppée ou brute) avec son décalage en octets dans
/// le texte d'origine — nécessaire pour la navigation clavier (Haut/Bas,
/// Origine/Fin) et le hit-test souris, qui doivent retrouver une position
/// absolue dans `text` à partir d'une ligne/colonne affichée.
struct TextLine {
	size_t offset;
	String text;
};

/// Découpe `text` en lignes de longueur brute (octets) tenant dans `maxWidth`
/// à `fontSize` (UiInput : word-wrap) — coupe au dernier espace si possible,
/// sinon au caractère pour un "mot" plus long que `maxWidth` à lui seul.
/// `\n` n'a pas de sens particulier ici (UiInput est mono-ligne logique).
[[nodiscard]] std::vector<TextLine> WrapText(const String &text, float fontSize, float maxWidth);

/// Découpe `text` en lignes sur `\n` (UiInputArea : pas de word-wrap, chaque
/// ligne logique = une ligne affichée).
[[nodiscard]] String StripTrailingReturn(String line);

[[nodiscard]] std::vector<TextLine> SplitLines(const String &text);

/// Trouve l'index de la ligne (dans `lines`, résultat de wrapText/splitLines)
/// qui contient le décalage `pos` — la dernière dont `offset <= pos`.
[[nodiscard]] size_t LineIndexOf(const std::vector<TextLine> &lines, size_t pos) noexcept;

/// Recule d'un point de code UTF-8 depuis `pos` (jamais sous 0).
[[nodiscard]] size_t PrevCodepoint(const String &text, size_t pos) noexcept;

/// Avance d'un point de code UTF-8 depuis `pos` (jamais au-delà de text.size()).
[[nodiscard]] size_t NextCodepoint(const String &text, size_t pos) noexcept;

/// Décalage (octets, borné à une frontière de point de code) sous la souris
/// en (mx, my), pour un widget dont le contenu tient dans `lines` — mêmes
/// marges (8px/4px) et défilement que le rendu (cf. RenderSystem). Utilisé
/// pour placer le curseur au clic et pendant un glisser de sélection.
[[nodiscard]] size_t HitTestOffset(const std::vector<TextLine> &lines, const String &text, float fontSize,
										  const sdl3::FRect &s, const sdl3::FPoint &scroll, float mx, float my,
										  int tabStop = DEFAULT_TAB_STOP, float cellWidth = 0.f) noexcept;

/// Ajuste r.scroll.y au minimum nécessaire pour que la ligne `lineIndex`
/// (dans les lignes affichées, enveloppées ou brutes) reste visible dans la
/// zone `c.screen` — utilisé pour garder le curseur de UiInput/UiInputArea
/// visible après un déplacement clavier/souris.
void ScrollIntoView(UiRect &r, const UiComputed &c, size_t lineIndex, float fontSize) noexcept;

/// Défilement HORIZONTAL minimal pour que la colonne `column` (cellules
/// d'affichage) reste visible — le pendant de ScrollIntoView pour les lignes
/// longues d'un éditeur de code, où le texte ne se replie pas.
void ScrollColumnIntoView(UiRect &r, float viewWidth, size_t column, float cellWidth) noexcept;

// ── Géométrie d'un UiInputArea (mode éditeur de code) ───────────────────────
// Une seule définition partagée par la saisie, la mise en page et le rendu :
// si l'un plaçait le texte autrement que les autres, le clic tomberait à côté
// du caractère visé.

/// Largeur d'une cellule de caractère : celle mesurée sur la police dessinée
/// si le rendu l'a publiée, l'approximation sinon.
[[nodiscard]] float AreaCellWidth(const UiInputArea &f, float fontSize) noexcept;

/// Largeur de la gouttière de numéros (0 sans numéros) : au moins trois
/// chiffres, pour que la marge ne saute pas en passant de la ligne 99 à 100.
[[nodiscard]] float AreaGutterWidth(const UiInputArea &f, size_t lineCount, float cellWidth) noexcept;

/// Boîte du TEXTE (à droite de la gouttière) — c'est elle, et non le rect du
/// widget, que reçoivent HitTestOffset/DrawCaret/DrawSelection.
[[nodiscard]] sdl3::FRect AreaTextBox(const sdl3::FRect &screen, float gutter) noexcept;

// ============================================================================
// LayoutSystem
// ============================================================================

class LayoutSystem {
public:
	/// Mesure du texte (largeur, hauteur). Par défaut : heuristique sans TTF ;
	/// brancher une vraie mesure via `measureText = ...` quand une Font existe.
	/// Mesure d'UNE ligne (largeur, hauteur). Les sauts de ligne, tabulations
	/// et caractères de contrôle sont traités en amont par MeasureText() :
	/// une implémentation branchée ici n'a jamais à s'en soucier.
	std::function<sdl3::FPoint(const String &, float fontSize)> measureText = [](const String &s,
																				 float fs) -> sdl3::FPoint {
		return {float(DisplayCells(s)) * CharWidthApprox(fs), LineHeightApprox(fs)};
	};

	/// Taille de police racine — base de DimUnit::Rem, et police héritée par
	/// les widgets sans fontSize propre (UiFactory l'aligne sur son thème).
	float rootFontSize = 14.f;

	/// Largeur d'un taquet de tabulation, en caractères (cf.
	/// NormalizeDisplayText).
	int tabStop = DEFAULT_TAB_STOP;

	/**
	 * Place occupée par un texte, CODES D'ÉCHAPPEMENT COMPRIS — c'est ce que
	 * le layout appelle, et non `measureText` directement.
	 *
	 * `measureText` reste le point de branchement des applications et ne
	 * mesure qu'UNE ligne : le découpage en lignes, les tabulations et les
	 * caractères de contrôle sont traités ici, une fois, pour tout le monde
	 * (cf. MeasureTextBlock).
	 */
	[[nodiscard]] sdl3::FPoint MeasureText(const String &text, float fontSize) const;

	/// Même mesure pour un texte UTF-16 (paires de substitution comprises).
	[[nodiscard]] sdl3::FPoint MeasureText(std::u16string_view text, float fontSize) const;
	/// Même mesure pour un texte UTF-32.
	[[nodiscard]] sdl3::FPoint MeasureText(std::u32string_view text, float fontSize) const;

	/// Métriques complètes (nombre de lignes, hauteur de ligne) quand la seule
	/// taille ne suffit pas.
	[[nodiscard]] TextMetrics MeasureTextMetrics(const String &text, float fontSize) const;

	void MarkDirty() noexcept { dirty = true; }
	[[nodiscard]] bool Dirty() const noexcept { return dirty; }
	[[nodiscard]] uint64_t PassCount() const noexcept { return passes; }

	/// Lance une passe seulement si nécessaire (dirty ou fenêtre redimensionnée).
	/// Retourne true si une passe a effectivement eu lieu.
	bool RunIfNeeded(ecs::ArchetypeRegistry &world, float screenW, float screenH);

	/// Passe complète inconditionnelle.
	void Run(ecs::ArchetypeRegistry &world, float screenW, float screenH);

private:
	bool dirty = true;
	float lastW = -1.f, lastH = -1.f;
	uint64_t passes = 0;

	// ── Une passe de layout sur des instantanés des composants ──────────────
	struct Pass {
		LayoutSystem &sys;
		ecs::ArchetypeRegistry &world;
		float rootW, rootH;

		std::unordered_map<uint32_t, UiRect> rects;
		std::unordered_map<uint32_t, UiFlow> flows;
		std::unordered_map<uint32_t, UiItem> items;
		std::unordered_map<uint32_t, std::vector<ecs::Entity>> children;
		std::unordered_set<uint32_t> hidden;
		std::vector<ecs::Entity> roots;

		std::unordered_map<uint32_t, sdl3::FPoint> measured;   // mémorisation measure()
		std::unordered_map<uint32_t, UiComputed> out;    // résultats
		std::unordered_map<uint32_t, sdl3::FPoint> newContent; // contenu auto des conteneurs
		std::unordered_map<uint32_t, sdl3::FPoint> newSizes;   // tailles résolues (scroll)
		std::unordered_map<uint32_t, sdl3::FPoint> newGutters; // place des barres de défilement
		std::unordered_map<uint32_t, math::Sides> borders;     // épaisseur de bordure dessinée
		std::unordered_map<uint32_t, EmBases> fonts;     // bases Em/Pem/Rem par entité

		Pass(LayoutSystem &s, ecs::ArchetypeRegistry &w, float rw, float rh) : sys(s), world(w), rootW(rw), rootH(rh) {}

		void Snapshot();

		/// Précalcule les bases Em/Pem/Rem ET l'échelle de contenu composée
		/// (cf. EmBases::scale — node-graph inner-zoom, Phase 0 du plan
		/// node-graph) : la police effective d'un widget est sa fontSize
		/// propre (DÉJÀ multipliée par l'échelle cumulée à ce niveau), sinon
		/// celle héritée du parent TELLE QUELLE (pas de second facteur —
		/// hériter recopie une valeur déjà à l'échelle, cf. le commentaire
		/// de Dimension::Resolve()) — DFS depuis les racines, hérite de
		/// rootFontSize/échelle 1.0 au sommet. Fait une fois par passe, les
		/// Dimension::em/pem/scale se résolvent ensuite en O(1).
		void ComputeFonts();

		[[nodiscard]] EmBases FontsOf(ecs::Entity e) const;

		[[nodiscard]] UiItem ItemOf(ecs::Entity e) const;

		/// Spécifications de taille : UiItem si présent, sinon la taille px du
		/// UiRect — et Auto (taille intrinsèque) pour tout axe laissé à 0.
		[[nodiscard]] std::pair<Dimension, Dimension> SizeVals(ecs::Entity e) const;

		[[nodiscard]] std::vector<ecs::Entity> VisibleChildren(ecs::Entity e) const;

		/// Taille intrinsèque d'un widget feuille (sans UiFlow).
		[[nodiscard]] sdl3::FPoint Intrinsic(ecs::Entity e);

		/// Taille auto (rétrécir au contenu) sur un axe.
		float AutoSize(ecs::Entity e, bool isW);

		/// Texte d'un widget textuel (libellé ou bouton) — les deux seuls que
		/// le retour automatique concerne aujourd'hui.
		[[nodiscard]] const String *TextOf(ecs::Entity e) const;

		/// Hauteur d'un widget à retour automatique replié dans `availableWidth`,
		/// NONE s'il n'est pas dans ce cas.
		///
		/// Cette hauteur ne peut pas être calculée par Measure() : elle dépend
		/// de la largeur que le widget REÇOIT, connue seulement au placement.
		[[nodiscard]] Option<float> WrappedHeightOf(ecs::Entity e, float availableWidth);

		/**
		 * Taille du CONTENU logique d'un widget textuel qui déborde — ce qui
		 * fait apparaître, sans une ligne de code de plus, la barre de
		 * défilement correspondante (cf. UiRect::MaxScroll, l'auto-scrollbar
		 * de RenderSystem::DrawScrollbars, et la molette/le pouce déjà gérés
		 * par InputSystem).
		 *
		 * NONE pour les autres modes : leur contenu logique EST leur boîte.
		 */
		[[nodiscard]] Option<sdl3::FPoint> TextContentSize(ecs::Entity e, const sdl3::FRect &screen);

		/// Épaisseur de bordure de `e` (zéro sans bordure dessinée).
		[[nodiscard]] math::Sides BorderOf(ecs::Entity e) const;

		/// Taille intrinsèque de `e` (post-ordre, mémoïsée).
		sdl3::FPoint Measure(ecs::Entity e);

		/// Résout la taille pixel de `e` selon les dimensions de contenu du parent.
		///
		/// Grow résout ici à "remplit le parent" (parentW/parentH), pas à 0 :
		/// hors du repartitionnement pondéré multi-enfants de placeLinear() (qui
		/// écrase ki.main sur l'axe principal après coup, donc insensible à cette
		/// valeur), resolveSize() sert à placer un enfant seul face à une boîte
		/// déjà connue — racine dans run(), enfant absolu dans place(), et surtout
		/// l'axe TRANSVERSE d'un enfant en flow (ex: .GrowW() sur une Row dans une
		/// Column) : il n'y a alors aucun autre widget avec qui partager l'espace,
		/// donc Grow == tout l'espace disponible.
		std::pair<float, float> ResolveSize(ecs::Entity e, float parentW, float parentH);

		void Place(ecs::Entity e, sdl3::FRect screen, sdl3::FRect drawClip);

		void PlaceLinear(ecs::Entity parent, const UiFlow &flow, sdl3::FRect box, sdl3::FPoint origin, sdl3::FRect childClip,
						 const std::vector<ecs::Entity> &kids);

		void WriteBack();
	};
};

// ============================================================================
// InputSystem — consomme directement les sdl3::Event (pas de structure
// intermédiaire à la charge de l'appelant : agréger plusieurs évènements
// dans un seul struct par frame perdait/écrasait des positions de souris et
// pouvait faire cohabiter pressed+released dans le même passage, d'où des
// widgets "corrompus" lors d'un clic rapide). Deux points d'entrée :
//
//   handleEvent(world, ev, layout) — un évènement SDL discret (bouton,
//     mouvement, molette, texte, touche) : hit-test + interaction immédiats,
//     avec les coordonnées EXACTES de CET évènement.
//   tick(world, layout, dt) — mises à jour dépendantes du temps réel et
//     indépendantes des évènements (animation toggle/spinner, clignotement
//     du curseur de saisie, minuteur d'infobulle) : à appeler une fois par
//     frame, en dehors de la boucle de poll.
// ============================================================================

/// Ouvre un popup ancré (non modal, cf. UiFactory::Popup()) : l'affiche et
/// mémorise `trigger` (pour la fermeture au clic extérieur, cf.
/// InputSystem::dispatch — les modales passent par InputSystem::OpenModal
/// à la place, qui gère en plus la pile de blocage). Déclarée AVANT
/// InputSystem (et non juste après, comme initialement en Phase 1) : Phase 5
/// (MenuBar/Menu) l'appelle depuis `InputSystem::dispatch()` — une fonction
/// libre appelée depuis le CORPS d'une méthode définie inline dans une
/// classe doit être déclarée avant cette classe, le "complete-class context"
/// ne s'applique qu'aux membres de la classe elle-même, pas aux fonctions
/// libres déclarées plus loin dans le fichier.
void OpenPopup(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity popupRoot, ecs::Entity trigger);

/// Ouvre un popup ancré au point écran `at` (coin haut-gauche) — menu
/// contextuel ouvert au clic droit (cf. UiCallbacks::onContextMenu). Sans
/// déclencheur : n'importe quel clic en dehors du popup le referme.
void OpenPopupAt(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity popupRoot, sdl3::FPoint at);

/// Ferme un popup ancré ouvert via `openPopup`.
void ClosePopup(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity popupRoot);

/// Remonte les UiParent depuis `e` (lui-même inclus) jusqu'à trouver une
/// entité porteuse d'un UiPopupState — le popup CONTENANT logiquement `e`
/// (cf. UiFactory::Menu()/subMenu(), Phase 5 : les items de menu sont
/// parentés sous leur popup). ecs::Entity{} invalide si `e` n'est dans aucun popup.
[[nodiscard]] ecs::Entity NearestPopupAncestor(ecs::ArchetypeRegistry &world, ecs::Entity e);

/// Repositionne un popup Fixed RACINE (sans UiParent — son ancre/offset se
/// résolvent alors directement contre la fenêtre, cf. LayoutSystem::Run())
/// juste sous `triggerScreen`, aligné à gauche — utilisé à l'OUVERTURE d'un
/// menu déroulant (cf. UiFactory::Menu()) ; la position est figée tant que
/// le menu reste ouvert (comportement standard, pas de suivi en direct d'un
/// redimensionnement de fenêtre pendant l'interaction).
void PositionPopupBelow(ecs::ArchetypeRegistry &world, ecs::Entity popupRoot, const sdl3::FRect &triggerScreen);

/// Marge verticale d'un popup de menu (cf. UiFactory::MenuPopup), au-dessus
/// de la première entrée et sous la dernière.
inline constexpr float MENU_POPUP_PAD_Y = 4.f;

/// Comme positionPopupBelow, mais à DROITE de `triggerScreen` — sous-menus
/// (cf. UiFactory::subMenu()). Remonte de la marge et de la bordure du popup
/// pour que la première entrée du sous-menu soit au niveau de celle qui l'ouvre.
void PositionPopupRightOf(ecs::ArchetypeRegistry &world, ecs::Entity popupRoot, const sdl3::FRect &triggerScreen);

/// Ferme les sous-menus ouverts depuis les entrées de `popup` (et, en
/// cascade, les leurs) — `popup` lui-même reste ouvert. `keep` : un
/// sous-menu à épargner (celui qu'on est en train d'ouvrir).
void CloseSubmenusOf(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity popup,
							ecs::Entity keep = ecs::Entity{});

/// Ferme toute la chaîne de popups contenant `clickedItem` (cf.
/// UiFactory::Menu()/subMenu()) : son popup direct, puis — si ce popup a été
/// ouvert par un item lui-même situé dans un autre popup (cas d'un
/// sous-menu) — celui-là aussi, et ainsi de suite jusqu'à la racine de la
/// chaîne. Appelé quand un UiMenuItem SANS sous-menu est cliqué (exécute
/// puis ferme tout, comme un clic de menu classique).
void CloseMenuChain(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity clickedItem);

class InputSystem {
public:
	float wheelSpeed = 40.f;

	/// Largeur d'un taquet de tabulation, en caractères. À garder égale à
	/// `LayoutSystem::tabStop` et `RenderSystem::tabStop` : les trois
	/// décrivent la MÊME grille — ce qui est réservé, ce qui est dessiné, et
	/// où le clic tombe.
	int tabStop = DEFAULT_TAB_STOP;

	/// Infobulle courante (à passer à RenderSystem::Run pour l'afficher).
	struct Tooltip {
		bool visible = false;
		String text;
		sdl3::FPoint pos{};
	};
	Tooltip tooltip;
	float tooltipDelay = 0.6f; ///< secondes de survol avant affichage

	/// Ouvre une modale : l'affiche, la pousse en haut de la pile modale —
	/// tant qu'elle y reste, dispatch() bloque tout le reste de l'arbre (cf.
	/// hitOk) et Échap la referme. `modalRoot` doit porter UiPopupState
	/// (cf. UiFactory::Modal()).
	void OpenModal(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity modalRoot);

	/// Ferme la modale du sommet de la pile SI elle correspond à `modalRoot`
	/// (no-op sinon — évite de dépiler la mauvaise modale par erreur si les
	/// appels s'entremêlent).
	void CloseModal(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity modalRoot);

	[[nodiscard]] bool HasOpenModal() const noexcept { return !modalStack.empty(); }

	/// Widget le plus en avant sous le pointeur au dernier évènement traité
	/// (cf. HitTestIndex), entité invalide si le pointeur ne survole aucun
	/// widget dessiné. C'est LUI (et lui seul, avec ses ancêtres) qui a le
	/// droit de réagir — les widgets restés derrière sont inertes.
	[[nodiscard]] ecs::Entity FrontMostWidget() const noexcept { return frontMost; }

	/// Vrai si le pointeur est capté par l'UI — un widget est dessiné sous
	/// lui. Permet à l'application de ne PAS traiter le clic de son côté
	/// (picking 3D d'un viewport, raccourcis de carte...) quand l'UI
	/// s'interpose.
	[[nodiscard]] bool PointerOverUi() const noexcept { return frontMost.Valid(); }

	/// Widget le plus en avant sous un point QUELCONQUE, en réutilisant
	/// l'index déjà tenu à jour par cette InputSystem — à préférer à
	/// ui::HitTestTopMost() dans tout code appelé à chaque évènement (celui-ci
	/// reconstruit l'index à chaque appel, cf. HitTestIndex).
	[[nodiscard]] ecs::Entity FrontMostAt(ecs::ArchetypeRegistry &world, LayoutSystem &layout, sdl3::FPoint p);

	/// Index de hit-test de cette InputSystem — à invalider explicitement
	/// après un changement structurel qui ne passe ni par une passe de layout
	/// ni par une création/destruction d'entité (cf. HitTestIndex::Refresh).
	[[nodiscard]] HitTestIndex &HitIndex() noexcept { return hitIndex; }

	// ── Focus clavier ───────────────────────────────────────────────────────
	//
	// Le widget qui a le focus clavier (champ de saisie, zone de texte,
	// valeur en cours d'édition) reçoit la frappe EN PREMIER. Il en réclame
	// l'exclusivité — caractères, pavé numérique, flèches, retour arrière,
	// espace, Entrée, Échap, Ctrl+A/C/V/X/Z/Y… — sauf pour les touches que
	// la règle de transmission (keyPassThrough) laisse passer au reste de
	// l'application : par défaut F1 à F24 et les combinaisons Ctrl/Alt/Cmd
	// qui ne sont pas de l'édition de texte (Ctrl+S, Ctrl+Q…), c'est-à-dire
	// les raccourcis de l'application.

	/// Règle de transmission : vrai si l'évènement clavier `ev` doit
	/// CONTINUER vers les autres destinataires alors que `focus` a le focus.
	/// Vide = DefaultKeyPassThrough.
	std::function<bool(const sdl3::Event &ev, ecs::Entity focus)> keyPassThrough;

	/// Règle par défaut de keyPassThrough (cf. en-tête de section).
	[[nodiscard]] static bool DefaultKeyPassThrough(const sdl3::Event &ev) noexcept;

	/// Widget qui a le focus clavier, entité invalide sinon.
	[[nodiscard]] ecs::Entity KeyboardFocus(ecs::ArchetypeRegistry &world) const;

	/// Retire le focus clavier (le champ cesse de capter la frappe).
	void ClearKeyboardFocus(ecs::ArchetypeRegistry &world) const;

	/// Vrai si `ev` est réservé au widget `focus` (cf. keyPassThrough ; un
	/// champ en lecture seule ne réserve que la navigation et la copie).
	[[nodiscard]] bool ClaimsKey(ecs::ArchetypeRegistry &world, const sdl3::Event &ev, ecs::Entity focus) const;

	/// Traite UN évènement SDL et le marque consommé (Event::Consume) s'il
	/// ne doit pas aller plus loin — frappe captée par le widget qui a le
	/// focus, Échap qui ferme une modale. L'appelant transmet ensuite le
	/// même évènement à ses propres traitements, qui ignorent un évènement
	/// consommé. Un évènement déjà consommé en entrée n'est pas traité.
	bool HandleEvent(ecs::ArchetypeRegistry &world, sdl3::Event &ev, LayoutSystem &layout);

	/// Traite UN évènement SDL ; vrai s'il est consommé (cf. la surcharge
	/// non-const, qui le marque). Ne fait rien pour les évènements sans effet
	/// sur l'UI (fenêtre, joystick...).
	bool HandleEvent(ecs::ArchetypeRegistry &world, const sdl3::Event &ev, LayoutSystem &layout);

	/// Mises à jour continues indépendantes des évènements discrets : à
	/// appeler une fois par frame (animations, clignotement, infobulle).
	void Tick(ecs::ArchetypeRegistry &world, LayoutSystem &layout, float dt);

	/// Délai de survol (s) avant qu'une entrée de menu déploie son sous-menu
	/// — ou replie celui d'une voisine : assez court pour paraître immédiat,
	/// assez long pour traverser en diagonale une entrée voisine sans refermer
	/// le sous-menu visé.
	float submenuDelay = 0.2f;

private:
	float mouseX = 0.f, mouseY = 0.f;
	// Widget le plus en avant sous le pointeur, recalculé une fois par
	// évènement (Dispatch) et par frame (Tick) — cf. HitTestIndex et
	// IsFrontMost : c'est ce qui empêche deux widgets superposés de réagir
	// tous les deux au même survol/clic.
	ecs::Entity frontMost{};
	/// frontMost + ses ancêtres (cf. IsFrontMost) — vide quand le pointeur
	/// n'est au-dessus d'aucun widget, ce qui vaut "aucun blocage".
	std::vector<ecs::Entity> frontMostChain;
	/// Ordre de dessin mémorisé, reconstruit seulement quand la géométrie
	/// change (cf. HitTestIndex).
	HitTestIndex hitIndex;
	bool down = false;
	bool rightDown = false; ///< cf. Frame::rightDown (UiPlot::boxZooming, Phase 3)
	ecs::Entity lastTip{};
	float hoverTime = 0.f;
	/// Entrée de menu survolée et depuis combien de temps (cf. NavigateMenus).
	ecs::Entity menuHover{};
	float menuHoverTime = 0.f;

	/**
	 * Navigation automatique dans les menus, au survol :
	 *  - une entrée à sous-menu le déploie à sa droite après `submenuDelay`,
	 *    et replie celui d'une voisine ;
	 *  - une entrée simple replie, après le même délai, les sous-menus
	 *    ouverts depuis son propre popup ;
	 *  - dans la barre de menus, dès qu'un menu est ouvert, survoler une
	 *    autre entrée bascule immédiatement sur son menu.
	 */
	void NavigateMenus(ecs::ArchetypeRegistry &world, LayoutSystem &layout, float dt);

	[[nodiscard]] static ecs::Entity UiParentOf(ecs::ArchetypeRegistry &world, ecs::Entity e);

	// Drag en cours sur un pouce d'auto-scrollbar (un seul à la fois).
	ecs::Entity scrollDrag{};
	bool scrollDragVertical = true;
	float scrollDragStartMouse = 0.f;
	float scrollDragStartOffset = 0.f;

	// Glisser-souris en cours pour étendre une sélection de texte dans un
	// UiInput/UiInputArea (un seul à la fois, comme scrollDrag ci-dessus).
	ecs::Entity textDrag{};

	// Drag en cours sur une UiResizeHandle (un seul à la fois, cf.
	// interaction.hpp — splitters, colonnes de tableau).
	ecs::Entity resizeDrag{};

	// Drag en cours sur une UiReorderable (un seul à la fois, cf.
	// interaction.hpp — Selectable/TreeNode, Phase 4) — `reorderTarget` :
	// frère le plus proche du curseur au moment du relâchement (position
	// d'insertion), recalculé à chaque mouvement.
	ecs::Entity reorderDrag{};
	ecs::Entity reorderTarget{};

	// Drag en cours sur une UiDraggable (un seul à la fois, cf.
	// interaction.hpp — panneaux flottants du "bureau simulé", Phase 9/10).
	ecs::Entity draggableDrag{};

	// Glisser-déposer ENTRE widgets (cf. UiDragPayload/UiDropTarget,
	// interaction.hpp). `dragPayloadSource` est armé dès l'enfoncement, mais
	// le glissé ne commence vraiment qu'après un petit déplacement : sans ce
	// seuil, un simple clic sur une ligne d'arbre deviendrait un dépôt sur
	// elle-même, et plus rien ne serait cliquable.
	ecs::Entity dragPayloadSource{};
	ecs::Entity dropHover{};
	float dragPayloadStartX = 0.f, dragPayloadStartY = 0.f;
	bool dragPayloadMoved = false;

	// Pile des modales ouvertes (cf. openModal/closeModal, public plus bas) —
	// le sommet bloque tout le reste de l'arbre dans dispatch() (hitOk).
	std::vector<ecs::Entity> modalStack;

	/// Vrai si `e` a le droit de réagir au pointeur cette frame : c'est la
	/// cible de premier plan elle-même, ou l'un de ses ANCÊTRES. Les ancêtres
	/// restent servis parce qu'un widget composite n'est presque jamais la
	/// feuille dessinée en dernier : un UiButton est recouvert par son propre
	/// libellé, un conteneur scrollable par tout son contenu — les exclure
	/// rendrait la moitié des widgets inertes. En revanche un FRÈRE resté
	/// derrière ne passe plus : c'est exactement le blocage recherché.
	///
	/// Cible invalide (pointeur au-dessus du vide, ou entité hors de tout
	/// arbre dessiné — cas des mondes montés à la main dans les tests) :
	/// aucun blocage, on retombe sur le comportement historique du rect seul.
	[[nodiscard]] bool IsFrontMost(ecs::Entity e) const noexcept;

	/// Recalcule la cible de premier plan et sa chaîne d'ancêtres.
	void UpdateFrontMost(ecs::ArchetypeRegistry &world, LayoutSystem &layout, sdl3::FPoint p);

	/// État d'UN évènement à traiter — détail d'implémentation privé
	/// (remplace l'ancienne InputState publique que l'appelant devait
	/// construire lui-même par agrégation, source de corruption).
	struct Frame {
		float mouseX = 0.f, mouseY = 0.f;
		bool down = false;
		bool pressed = false;
		bool released = false;
		bool doubleClick = false; ///< pressed avec clicks>=2 (SDL) — cf. UiDragValue
		bool rightDown = false;   ///< bouton DROIT — cf. UiPlot::boxZooming (Phase 3), seul consommateur
		bool rightPressed = false;
		bool rightReleased = false;
		float wheelY = 0.f;
		String textInput;
		bool backspace = false;
		bool del = false; ///< touche Suppr (supprime après le curseur)
		bool enter = false;
		bool escape = false; ///< Échap : ferme la modale du sommet de la pile
		bool copy = false;   ///< Ctrl+C
		bool paste = false;  ///< Ctrl+V
		bool cut = false;    ///< Ctrl+X
		bool selectAll = false; ///< Ctrl+A
		bool arrowLeft = false, arrowRight = false, arrowUp = false, arrowDown = false;
		bool home = false, end = false;
		bool shift = false; ///< Maj enfoncée (flèches/Origine/Fin → étend la sélection)
	};

	/// Déplacement du curseur (flèches/Origine/Fin), avec ou sans extension
	/// de sélection (Maj). `lines` : lignes actuellement affichées
	/// (enveloppées pour UiInput, brutes pour UiInputArea) — nécessaires pour
	/// Haut/Bas et Origine/Fin, qui opèrent sur la ligne AFFICHÉE courante
	/// (pas sur le texte entier). Sans Maj, une sélection existante se
	/// referme sur le bord gauche/droit (Gauche/Droite) plutôt que de
	/// bouger d'un cran depuis `cursor` — comportement standard.
	static void MoveCursor(const String &text, const std::vector<TextLine> &lines, size_t &cursor,
						   size_t &selectionAnchor, const Frame &in);

	/// Insertion/suppression/copie/collage/coupe à la position du CURSEUR
	/// (remplace toute sélection active), partagés entre UiInput et
	/// UiInputArea. `enterInsertsNewline` : true pour UiInputArea (Entrée =
	/// retour à la ligne) ; pour UiInput (Entrée = soumission), l'appelant
	/// traite `in.enter` lui-même et passe false ici.
	[[nodiscard]] static bool ApplyTextEdit(String &text, size_t &cursor, size_t &selectionAnchor, IOMode mode,
											size_t maxLen, const Frame &in, bool enterInsertsNewline);

	/// Hit-test + interaction pour tous les widgets, pour UN évènement.
	/// Les callbacks applicatifs (onClick/onChange/...) sont copiés dans
	/// `pending` et exécutés APRÈS toutes les queries : un callback peut
	/// muter l'ECS de façon structurelle (ex: changer de scène), ce qui
	/// réalloue le vecteur d'archétypes et invaliderait une itération en
	/// cours si on l'appelait directement depuis l'intérieur d'un Query().
	void Dispatch(ecs::ArchetypeRegistry &world, const Frame &in, LayoutSystem &layout);
};

// ============================================================================
// RenderSystem
// ============================================================================

class RenderSystem {
public:
	/// Moteur de texte optionnel : sans lui, le texte est simplement omis
	/// (utile pour les tests headless). Pointeurs non-possédants.
	void SetTextEngine(sdl3::TextEngine *engine, sdl3::Font *font);

	/// Enregistre une police d'icônes sous son nom de famille logique (cf.
	/// ui::Glyphs::FontFamily<E>()) afin que les UiIcon qui la référencent
	/// s'y résolvent au rendu. Pointeur non-possédant, comme setTextEngine —
	/// la police doit vivre au moins aussi longtemps que le RenderSystem.
	void RegisterFont(StringView family, sdl3::Font &font) { iconFonts.insert_or_assign(String(family), &font); }

	/// Enregistre la police à utiliser pour le texte marqué `prop::Italic`
	/// (cf. WidgetBuilder::italic()/styles.hpp) — un objet sdl3::Font DISTINCT
	/// ouvert avec FontStyle::ITALIC, pas juste `.SetStyle()` sur la police
	/// normale (qui mettrait TOUT le texte en italique, cf. font étant
	/// unique et partagé par tous les widgets). Pointeur non-possédant,
	/// comme setTextEngine/registerFont. Sans appel, `prop::Italic` est posé
	/// sans effet (retombe sur la police normale, cf. drawWidget UiLabel).
	void RegisterItalicFont(sdl3::Font &font) { italicFont = &font; }
	/// Police à chasse fixe des UiInputArea en mode `monospace` (éditeur de
	/// code). Pointeur non-possédant, comme les autres polices.
	void RegisterMonospaceFont(sdl3::Font &font) { monoFont = &font; }
	/// Idem pour `prop::Bold` (gras).
	void RegisterBoldFont(sdl3::Font &font) { boldFont = &font; }
	/// Idem pour `prop::Bold` ET `prop::Italic` posés ensemble — sans elle,
	/// gras+italique retombe sur la police grasse seule (cf. pickTextFont()),
	/// éventuellement moins fidèle visuellement mais jamais absente.
	void RegisterBoldItalicFont(sdl3::Font &font) { boldItalicFont = &font; }

	/// Pool de textures pour UiImage (clé → texture).
	std::unordered_map<String, sdl3::Texture> textures;

	/// Texture d'effet composée par entité (M23, ui::ShaderEffectSystem,
	/// shader_effect.hpp) — SEULE copie possédée de chaque texture d'effet :
	/// UiShaderEffect (components.hpp) reste un composant pur, sans état GPU
	/// dessus (cf. sa doc). Alimenté par ShaderEffectSystem::Update, lu ici
	/// par DrawWidget/DrawTree ci-dessous. Une entrée absente (widget jamais
	/// traité par ShaderEffectSystem, ou dernier traitement en échec sans
	/// texture précédente) fait retomber DrawTree sur le rendu NORMAL du
	/// sous-arbre — dégradation "gratuite", cf. shader_effect.hpp en-tête.
	std::unordered_map<ecs::Entity, sdl3::Texture> effectTextures;

	/// Largeur d'un taquet de tabulation, en caractères — à garder d'accord
	/// avec LayoutSystem::tabStop, sinon ce qui est dessiné n'occupe plus la
	/// place qui a été réservée.
	int tabStop = DEFAULT_TAB_STOP;

	// Couleurs des infobulles (indépendantes du thème de la factory).
	sdl3::FColor tooltipBg{25 / 255.f, 27 / 255.f, 38 / 255.f, 245 / 255.f};
	sdl3::FColor tooltipBorder{70 / 255.f, 76 / 255.f, 110 / 255.f, 1.f};
	sdl3::FColor tooltipText = sdl3::FColor::UI_TEXT_PRIMARY();

	// Couleurs de l'auto-scrollbar (conteneurs `.Scrollable()` en débordement).
	sdl3::FColor scrollbarTrack{1.f, 1.f, 1.f, 18 / 255.f};
	sdl3::FColor scrollbarThumb{1.f, 1.f, 1.f, 70 / 255.f};

	/// Dessine l'UI. `tip` (optionnel) : infobulle calculée par InputSystem,
	/// dessinée en overlay avec les listes des combos ouverts.
	void Run(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren, const InputSystem::Tooltip *tip = nullptr);

	void ClearTextCache();

private:
	sdl3::TextEngine *m_engine = nullptr;
	sdl3::Font *m_font = nullptr;
	// Variantes de police pour prop::Bold/Italic (cf. registerBoldFont() /
	// registerItalicFont() / registerBoldItalicFont()) — nullptr = non
	// enregistrée, cf. pickTextFont() pour la logique de repli.
	sdl3::Font *italicFont = nullptr;
	sdl3::Font *boldFont = nullptr;
	sdl3::Font *boldItalicFont = nullptr;
	/// Police à chasse fixe (cf. RegisterMonospaceFont) ; la largeur de sa
	/// cellule est mesurée par taille (cf. m_cellWidths).
	sdl3::Font *monoFont = nullptr;
	/// Polices d'icônes additionnelles, indexées par nom de famille logique
	/// (cf. registerFont / ui::Glyphs::FontFamily). Pointeurs non-possédants.
	std::unordered_map<String, sdl3::Font *> iconFonts;

	/// Copies redimensionnées des polices (cf. FontAt), par (police, taille en
	/// quarts de point). Déclarées AVANT les caches de textes : un texte mis en
	/// forme doit être détruit avant sa police, et les membres sont détruits
	/// dans l'ordre inverse de leur déclaration.
	std::map<std::pair<const sdl3::Font *, int>, std::unique_ptr<sdl3::Font>> m_sizedFonts;
	/// Largeur de cellule mesurée par police à chasse fixe (cf. UiInputArea).
	std::unordered_map<const sdl3::Font *, float> m_cellWidths;
	/// Taille de police résolue du widget en cours de dessin (0 = celle de la
	/// police chargée) — cf. FontAt et DrawWidget.
	float m_currentFontSize = 0.f;

	struct CachedText {
		String str;
		sdl3::FColor color;
		const sdl3::Font *font = nullptr; ///< police utilisée à la création (invalide le cache si elle change)
		sdl3::Text text;
	};
	std::unordered_map<uint32_t, CachedText> textCache;
	// Cache secondaire clé = chaîne (items de listes, onglets, infobulle) —
	// borné par le nombre de chaînes distinctes affichées.
	struct CachedStr {
		sdl3::FColor color;
		sdl3::Text text;
	};
	std::unordered_map<String, CachedStr> strCache;

public:
	// `overlayEntry` : true uniquement pour l'appel qui (re-)lance le dessin
	// À LA RACINE d'un sous-arbre AttachLayout::Fixed depuis la passe overlay
	// de run(). Un Fixed rencontré pendant la descente normale (overlayEntry
	// = false, valeur par défaut) est sauté ici — il est dessiné séparément,
	// par-dessus tout le reste — pour éviter de le dessiner deux fois.
	//
	// PUBLIC (pas juste un détail interne de Run()) depuis M23 : ui::
	// ShaderEffectSystem (shader_effect.hpp) l'appelle directement sur UNE
	// entité précise pour capturer son sous-arbre dans une texture cible
	// dédiée, avant post-traitement GPU — cf. shader_effect.hpp en-tête pour
	// le pipeline complet.
	void DrawTree(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren, ecs::Entity e, bool overlayEntry = false);

private:

	void DrawWidget(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren, ecs::Entity e, const UiComputed &c);

	/// Liste déroulée d'un combo — en overlay, hors de tout clip.
	/// Piste + pouce sur les bords droit/bas de `screen`, un par axe en
	/// débordement (cf. vScrollbarThumbRect/hScrollbarThumbRect, la même
	/// géométrie que celle utilisée par InputSystem pour le drag).
	void DrawScrollbars(IUiRenderBackend &ren, const UiRect &r, const sdl3::FRect &screen);

	void DrawDropdown(IUiRenderBackend &ren, const UiComboBox &cb, const sdl3::FRect &box, const ResolvedStyle &rs);

	/// Taille du texte des infobulles (px).
	static constexpr float TOOLTIP_FONT_SIZE = 13.f;

	void DrawTooltip(IUiRenderBackend &ren, const InputSystem::Tooltip &tip);

	/// Rend la texture dans `dst` selon le mode d'ajustement demandé.
	void DrawImageFit(IUiRenderBackend &ren, const sdl3::Texture &tex, const sdl3::FRect &dst, ImageFit fit);

	/// Sélectionne la police à utiliser pour du texte marqué bold/italic (cf.
	/// prop::Bold/prop::Italic, styles.hpp) — repli en cascade sur ce qui est
	/// effectivement enregistré (jamais d'absence de rendu) : bold+italic
	/// préfère la variante combinée, sinon gras seul, sinon italique seul,
	/// sinon la police normale.
public:
	/**
	 * `base` à la taille `size` (points). Le texte d'un widget se dessinait
	 * TOUJOURS à la taille de chargement de la police, quel que soit son
	 * `fontSize` : un titre en 18 ou une légende en 11 sortaient en 14 (la
	 * mise en page, elle, mesurait bien la taille demandée — le texte
	 * débordait ou flottait dans sa boîte), et une icône de 14 dans un bouton
	 * de 18 s'affichait à la taille de chargement de la police d'icônes.
	 *
	 * Une copie par taille (TTF_CopyFont + SetSize), créée à la première
	 * demande et gardée : changer la taille de la police PARTAGÉE re-mettrait
	 * en page tous les textes déjà créés avec elle.
	 */
	[[nodiscard]] sdl3::Font *FontAt(sdl3::Font *base, float size);

private:
	/// Police de dessin effective : `font` (ou la police normale) à la taille
	/// du widget en cours de dessin.
	[[nodiscard]] sdl3::Font *DrawFont(sdl3::Font *font);

	[[nodiscard]] sdl3::Font *PickTextFont(bool bold, bool italic) const;

	/// Mesure une chaîne : vraie mesure TTF si possible, sinon heuristique.
	/// Place occupée par un texte, codes d'échappement compris — même règle
	/// que LayoutSystem::MeasureText, sinon les décorations (surlignage,
	/// soulignement, barré) et les colonnes de raccourcis d'un menu se
	/// placeraient à côté du texte réellement dessiné.
	[[nodiscard]] sdl3::FPoint MeasureCached(const String &text, float fontSize);

	/// Texte via le cache clé-chaîne, centré verticalement dans `rowH`.
	void DrawTextRaw([[maybe_unused]] IUiRenderBackend &ren, const String &rawText, sdl3::FColor color, float x,
					 float y, float rowH, sdl3::Font *font = nullptr);

	/// Surbrillance de la sélection (rects semi-transparents, une bande par
	/// ligne couverte) pour UiInput/UiInputArea — dessinée AVANT le texte
	/// pour qu'il reste lisible par-dessus. Pas de sélection (cursor ==
	/// selectionAnchor) : ne dessine rien.
	void DrawSelection(IUiRenderBackend &ren, const std::vector<TextLine> &lines, float fontSize, size_t cursor,
					   size_t selectionAnchor, const sdl3::FRect &s, const sdl3::FPoint &scroll, sdl3::FColor accent,
					   float cellWidth = 0.f);

	/// Curseur clignotant à la position réelle de `cursor` (ligne + colonne),
	/// pas toujours en fin de texte — partagé entre UiInput et UiInputArea.
	void DrawCaret(IUiRenderBackend &ren, const std::vector<TextLine> &lines, float fontSize, size_t cursor,
				   const sdl3::FRect &s, const sdl3::FPoint &scroll, sdl3::FColor color, float cellWidth = 0.f);

	void DrawVGradient(IUiRenderBackend &ren, const sdl3::FRect &r, sdl3::FColor top, sdl3::FColor bottom);

	/// Dégradé horizontal 2 couleurs (cf. drawVGradient, même construction —
	/// gauche → droite au lieu de haut → bas). Utilisé par la glissière alpha
	/// (opaque → transparent) et le fond de secours des segments de teinte.
	void DrawHGradient(IUiRenderBackend &ren, const sdl3::FRect &r, sdl3::FColor left, sdl3::FColor right);

	/// Dégradé bilinéaire 4 coins (interpolation des 4 couleurs sur le
	/// rectangle) — carré SV du sélecteur de couleur (cf. UiSVSquare) :
	/// blanc→teinte en haut, noir→noir en bas.
	void DrawQuadGradient(IUiRenderBackend &ren, const sdl3::FRect &r, sdl3::FColor tl, sdl3::FColor tr, sdl3::FColor bl, sdl3::FColor br);

	/// Piste arc-en-ciel de la glissière de teinte (cf. UiHueSlider) : 6
	/// segments égaux, un par transition de l'hexagone HSV (rouge→jaune→
	/// vert→cyan→bleu→magenta→rouge), chacun un dégradé horizontal 2 couleurs.
	void DrawHueBar(IUiRenderBackend &ren, const sdl3::FRect &s);

	/// Damier gris clair/gris moyen — fond conventionnel de transparence
	/// (glissière alpha, pastille de couleur avec alpha < 255).
	void DrawCheckerboard(IUiRenderBackend &ren, const sdl3::FRect &s, const math::Corners &radius);

	/// Bande de reflet translucide (blanc → transparent) sur le haut d'un
	/// rect, façon bouton/panneau « verre » Aero. Rectangulaire (ignore le
	/// radius du dessous, comme drawVGradient déjà pour UiPanel.gradient) —
	/// insetée pour rester visuellement crédible malgré les coins arrondis.
	void DrawGlossHighlight(IUiRenderBackend &ren, const sdl3::FRect &s, float radius, float strength);

	/// Anneaux de lueur (alpha décroissant) autour d'un rect — effet « glow »
	/// Aero. Dessinés hors du rect (outset croissant), donc visibles tant que
	/// le clip hérité du parent laisse la marge nécessaire.
	void DrawGlowRing(IUiRenderBackend &ren, const sdl3::FRect &s, float radius, float strength, sdl3::FColor glowColor);

	/// `font` : police à utiliser pour CE dessin (nullptr = police globale
	/// font — cas de tous les widgets textuels). UiIcon passe explicitement
	/// sa propre police d'icônes, résolue via iconFonts.
	/**
	 * Dessine le texte d'un widget selon son mode de débordement (cf.
	 * TextOverflow) — le point d'entrée unique des libellés.
	 *
	 * Les modes ne changent QUE l'affichage : la place réservée, elle, a été
	 * décidée par le layout (largeur du texte pour CLIP/ELLIPSIS/SCROLL/
	 * MARQUEE, hauteur repliée pour WRAP), et les barres de défilement
	 * viennent de UiRect::content. Ici on ne fait que peindre, dans le clip
	 * déjà posé par DrawTree — aucun mode ne peut donc déborder du widget.
	 */
	void DrawOverflowText(IUiRenderBackend &ren, ecs::ArchetypeRegistry &world, ecs::Entity e, const String &text,
						  sdl3::FColor color, const sdl3::FRect &box, TextAlign align, sdl3::Font *font);

public:
	/// Le plus long préfixe de `text` qui tienne dans `maxWidth` une fois « … »
	/// ajouté. Public : c'est une opération de MESURE (pas de dessin), utile
	/// à une application qui veut savoir ce qui sera réellement lisible, et
	/// testable sans police ni GPU. Recherche DICHOTOMIQUE sur les frontières de caractères : une
	/// recherche linéaire mesurerait autant de préfixes qu'il y a de lettres,
	/// à chaque image, pour chaque libellé tronqué.
	[[nodiscard]] String TruncateWithEllipsis(const String &text, float fontSize, float maxWidth);

private:
	/// Une ligne repliée, posée selon l'alignement. `Justify` répartit
	/// l'espace restant ENTRE LES MOTS — sauf sur la dernière ligne d'un
	/// paragraphe, qu'étirer rendrait illisible.
	void DrawWrappedLine(IUiRenderBackend &ren, const WrappedLine &line, sdl3::FColor color, const sdl3::FRect &box,
						 float y, float lineHeight, TextAlign align, sdl3::Font *font, float fontSize);

	/// Mesure d'UNE ligne avec la police donnée (celle du widget, qui peut
	/// être grasse ou italique) — repli sur l'heuristique sans police.
	[[nodiscard]] sdl3::FPoint MeasureLineWith(const String &line, float fontSize, sdl3::Font *font);

	void DrawTextCentered([[maybe_unused]] IUiRenderBackend &ren, ecs::Entity e, const String &rawText,
						  sdl3::FColor color, const sdl3::FRect &box, TextAlign align, sdl3::Font *font = nullptr);
};

} // namespace ui
