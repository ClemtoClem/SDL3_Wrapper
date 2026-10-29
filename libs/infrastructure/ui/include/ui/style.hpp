#pragma once
/**
 * ui::style — propriétés de style, valeurs déclarées et valeurs calculées.
 *
 * Chaque propriété configurable (par CSS ou par les builders fluents) est
 * déclarée UNE fois dans la liste `UI_STYLE_PROPERTIES` : son type C++, son
 * nom de membre, son nom CSS, son caractère héritable et sa valeur par défaut
 * (celle du code C++, utilisée quand aucune règle ni aucun parent ne la
 * définit). De cette liste découlent :
 *
 *  - `StyleProps`    : valeurs DÉCLARÉES (chaque champ est un Option, NONE =
 *                      non définie) — contenu d'une règle CSS, d'un style
 *                      inline posé par un builder ;
 *  - `ComputedStyle` : valeurs CALCULÉES (tous les champs résolus) — ce que
 *                      lisent la mise en page et le rendu ;
 *  - le registre nom CSS ↔ propriété (`PropFromName`, `InfoOf`).
 *
 * Cascade (cf. StyleSystem, systems.hpp), de la plus forte à la plus faible :
 *   1. style inline privé du widget (builder : `.BackgroundColor(...)`) ;
 *   2. règles privées : sélecteur `#id` ou `this` — jamais héritées ;
 *   3. style inline partagé (builder : `.Cascade(...)`) ;
 *   4. règles partagées : sélecteur de type ou de classe — héritables ;
 *   5. feuilles du module (thème, structure des widgets) ;
 *   6. pour une propriété héritable : valeur partagée du parent, puis de ses
 *      ancêtres jusqu'à la racine ;
 *   7. valeur par défaut C++ (`UI_STYLE_PROPERTIES`).
 * Dans un même niveau, la feuille importée par le widget le plus proche
 * l'emporte sur celles de ses ancêtres, puis la spécificité, puis l'ordre.
 */
#include <utility>
#include <vector>

#include "types.hpp"

namespace ui {

// ============================================================================
// Liste des propriétés
// ============================================================================

// X(Type, membre, "nom-css", héritable, valeurDéfaut)
#define UI_STYLE_PROPERTIES(X)                                                                                       \
	X(Display, display, "display", false, Display::Flex)                                                             \
	X(Position, position, "position", false, Position::Relative)                                                     \
	X(Anchor, anchor, "anchor", false, Anchor::TopLeft)                                                              \
	X(Length, top, "top", false, Length::Auto())                                                                     \
	X(Length, right, "right", false, Length::Auto())                                                                 \
	X(Length, bottom, "bottom", false, Length::Auto())                                                               \
	X(Length, left, "left", false, Length::Auto())                                                                   \
	X(Length, width, "width", false, Length::Auto())                                                                 \
	X(Length, height, "height", false, Length::Auto())                                                               \
	X(Length, minWidth, "min-width", false, Length::Auto())                                                          \
	X(Length, minHeight, "min-height", false, Length::Auto())                                                        \
	X(Length, maxWidth, "max-width", false, Length::Auto())                                                          \
	X(Length, maxHeight, "max-height", false, Length::Auto())                                                        \
	X(float, marginTop, "margin-top", false, 0.f)                                                                    \
	X(float, marginRight, "margin-right", false, 0.f)                                                                \
	X(float, marginBottom, "margin-bottom", false, 0.f)                                                              \
	X(float, marginLeft, "margin-left", false, 0.f)                                                                  \
	X(float, paddingTop, "padding-top", false, 0.f)                                                                  \
	X(float, paddingRight, "padding-right", false, 0.f)                                                              \
	X(float, paddingBottom, "padding-bottom", false, 0.f)                                                            \
	X(float, paddingLeft, "padding-left", false, 0.f)                                                                \
	X(FlexDirection, flexDirection, "flex-direction", false, FlexDirection::Column)                                  \
	X(FlexWrap, flexWrap, "flex-wrap", false, FlexWrap::NoWrap)                                                      \
	X(Justify, justifyContent, "justify-content", false, Justify::Start)                                             \
	X(Align, alignItems, "align-items", false, Align::Stretch)                                                       \
	X(Align, alignSelf, "align-self", false, Align::Auto)                                                            \
	X(float, flexGrow, "flex-grow", false, 0.f)                                                                      \
	X(float, flexShrink, "flex-shrink", false, 1.f)                                                                  \
	X(float, gap, "gap", false, 0.f)                                                                                 \
	X(int, gridColumns, "grid-columns", false, 1)                                                                    \
	X(Overflow, overflowX, "overflow-x", false, Overflow::Visible)                                                   \
	X(Overflow, overflowY, "overflow-y", false, Overflow::Visible)                                                   \
	X(int, zIndex, "z-index", false, 0)                                                                              \
	X(float, zoom, "zoom", false, 1.f)                                                                               \
	X(Color, backgroundColor, "background-color", false, Color(0.f, 0.f, 0.f, 0.f))                                  \
	X(Paint, backgroundImage, "background-image", false, Paint())                                                    \
	X(ObjectFit, objectFit, "object-fit", false, ObjectFit::Fill)                                                    \
	X(Color, borderColor, "border-color", false, Color(0.f, 0.f, 0.f, 0.f))                                          \
	X(float, borderTopWidth, "border-top-width", false, 0.f)                                                         \
	X(float, borderRightWidth, "border-right-width", false, 0.f)                                                     \
	X(float, borderBottomWidth, "border-bottom-width", false, 0.f)                                                   \
	X(float, borderLeftWidth, "border-left-width", false, 0.f)                                                       \
	X(float, borderTopLeftRadius, "border-top-left-radius", false, 0.f)                                              \
	X(float, borderTopRightRadius, "border-top-right-radius", false, 0.f)                                            \
	X(float, borderBottomRightRadius, "border-bottom-right-radius", false, 0.f)                                      \
	X(float, borderBottomLeftRadius, "border-bottom-left-radius", false, 0.f)                                        \
	X(Shadow, boxShadow, "box-shadow", false, Shadow())                                                              \
	X(float, opacity, "opacity", false, 1.f)                                                                         \
	X(Color, outlineColor, "outline-color", false, Color(0.f, 0.f, 0.f, 0.f))                                        \
	X(float, outlineWidth, "outline-width", false, 0.f)                                                              \
	X(float, outlineOffset, "outline-offset", false, 1.f)                                                            \
	X(float, gloss, "gloss", false, 0.f)                                                                             \
	X(float, glow, "glow", false, 0.f)                                                                               \
	X(Color, glowColor, "glow-color", false, Color(1.f, 1.f, 1.f, 1.f))                                              \
	X(Visibility, visibility, "visibility", true, Visibility::Visible)                                               \
	X(PointerEvents, pointerEvents, "pointer-events", false, PointerEvents::Auto)                                    \
	X(Cursor, cursor, "cursor", true, Cursor::DEFAULT)                                                               \
	X(Color, color, "color", true, Color(0.90f, 0.91f, 0.94f, 1.f))                                                  \
	X(String, fontFamily, "font-family", true, String())                                                             \
	X(float, fontSize, "font-size", true, 14.f)                                                                      \
	X(int, fontWeight, "font-weight", true, 400)                                                                     \
	X(FontStyle, fontStyle, "font-style", true, FontStyle::Normal)                                                   \
	X(float, lineHeight, "line-height", true, 0.f)                                                                   \
	X(TextAlign, textAlign, "text-align", true, TextAlign::Left)                                                     \
	X(VerticalAlign, verticalAlign, "vertical-align", false, VerticalAlign::Middle)                                  \
	X(TextDecoration, textDecoration, "text-decoration", false, TextDecoration::None)                                \
	X(Color, textDecorationColor, "text-decoration-color", false, Color(0.f, 0.f, 0.f, 0.f))                         \
	X(TextOverflow, textOverflow, "text-overflow", false, TextOverflow::Clip)                                        \
	X(int, tabSize, "tab-size", true, 4)                                                                             \
	X(float, marqueeSpeed, "marquee-speed", false, 40.f)                                                             \
	X(float, marqueePause, "marquee-pause", false, 1.f)                                                              \
	X(Color, accentColor, "accent-color", true, Color(0.26f, 0.53f, 0.96f, 1.f))                                     \
	X(Color, caretColor, "caret-color", true, Color(0.90f, 0.91f, 0.94f, 1.f))                                       \
	X(Color, placeholderColor, "placeholder-color", true, Color(0.55f, 0.57f, 0.65f, 1.f))                           \
	X(Color, selectionColor, "selection-color", true, Color(0.26f, 0.53f, 0.96f, 0.35f))                             \
	X(Color, scrollbarThumbColor, "scrollbar-thumb-color", true, Color(1.f, 1.f, 1.f, 0.28f))                        \
	X(Color, scrollbarTrackColor, "scrollbar-track-color", true, Color(1.f, 1.f, 1.f, 0.07f))                        \
	X(float, scrollbarWidth, "scrollbar-width", true, 8.f)                                                           \
	X(float, transitionDuration, "transition-duration", false, 0.f)

/// Identifiant d'une propriété (ordre de UI_STYLE_PROPERTIES).
enum class Prop : uint8_t {
#define UI_X(T, member, css, inherited, def) member,
	UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
		Count
};

inline constexpr size_t PROP_COUNT = size_t(Prop::Count);

struct PropInfo {
	const char *name;
	bool inherited;
};

[[nodiscard]] const PropInfo &InfoOf(Prop p) noexcept;
[[nodiscard]] Option<Prop> PropFromName(StringView cssName) noexcept;

// ============================================================================
// StyleProps — valeurs déclarées
// ============================================================================

/// Propriété personnalisée CSS (`--nom: valeur`), héritée comme `color`,
/// substituée par `var(--nom)`.
struct CustomProperty {
	String name; ///< sans les deux tirets
	String value;
};

/// Déclaration dont la valeur contient `var(...)` : analysée au moment de la
/// cascade, une fois les propriétés personnalisées du widget connues.
struct DeferredDeclaration {
	String property;
	String value;
};

struct StyleProps {
#define UI_X(T, member, css, inherited, def) Option<T> member;
	UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
	std::vector<CustomProperty> customs;
	std::vector<DeferredDeclaration> deferred;

	/// Les champs définis dans `over` remplacent ceux de *this.
	void Merge(const StyleProps &over);
	/// Ne garde que les propriétés héritables (et les propriétés personnalisées).
	void MergeInherited(const StyleProps &over);

	// Groupes de côtés/coins (les quatre propriétés longues d'un raccourci).
	void SetMargin(const math::Sides &s);
	void SetPadding(const math::Sides &s);
	void SetBorderWidth(const math::Sides &s);
	void SetBorderRadius(const math::Corners &c);
	[[nodiscard]] bool Has(Prop p) const noexcept;
	void Clear(Prop p) noexcept;
	[[nodiscard]] bool IsEmpty() const noexcept;
	/// Valeur d'une propriété personnalisée (`--nom`), NONE si absente.
	[[nodiscard]] Option<String> Custom(StringView name) const;
	void SetCustom(String name, String value);
};

// ============================================================================
// États (pseudo-classes)
// ============================================================================

/// Pseudo-classes d'état, combinables (masque).
enum class State : uint16_t {
	None = 0,
	Hover = 1 << 0,        ///< :hover — sous le pointeur (le widget ou un descendant)
	Active = 1 << 1,       ///< :active — enfoncé
	Focus = 1 << 2,        ///< :focus — a le focus clavier
	FocusVisible = 1 << 3, ///< :focus-visible — focus obtenu au clavier/à la manette
	Checked = 1 << 4,      ///< :checked — coché, activé, déplié
	Disabled = 1 << 5,     ///< :disabled
	Selected = 1 << 6,     ///< :selected — élément sélectionné d'une liste
	Open = 1 << 7,         ///< :open — popup ouvert (sur le popup et son déclencheur)
	DragOver = 1 << 8,     ///< :drag-over — cible de dépôt survolée par un glissé compatible
	Dragging = 1 << 9,     ///< :dragging — source d'un glissé en cours
};

[[nodiscard]] constexpr State operator|(State a, State b) noexcept { return State(uint16_t(a) | uint16_t(b)); }
[[nodiscard]] constexpr State operator&(State a, State b) noexcept { return State(uint16_t(a) & uint16_t(b)); }
[[nodiscard]] constexpr State operator~(State a) noexcept { return State(uint16_t(~uint16_t(a))); }
[[nodiscard]] constexpr bool HasState(State set, State flags) noexcept {
	return (uint16_t(set) & uint16_t(flags)) == uint16_t(flags);
}
[[nodiscard]] Option<State> StateFromName(StringView pseudo) noexcept;

/// Style appliqué quand le widget est dans tous les états de `mask`.
struct StateStyle {
	State mask = State::None;
	StyleProps props;
	bool shared = false; ///< vrai : héritable par les enfants (cf. `.Cascade`)
};

// ============================================================================
// ComputedStyle — valeurs calculées
// ============================================================================

struct ComputedStyle {
#define UI_X(T, member, css, inherited, def) T member = def;
	UI_STYLE_PROPERTIES(UI_X)
#undef UI_X
	/// Zoom cumulé (produit des `zoom` des ancêtres et du widget) : les
	/// longueurs en pixels et la police sont déjà à cette échelle.
	float scale = 1.f;
	/// Opacité cumulée (produit des `opacity` des ancêtres).
	float effectiveOpacity = 1.f;

	/// Valeurs par défaut du code C++.
	[[nodiscard]] static const ComputedStyle &Initial() noexcept;

	/// Copie dans *this les valeurs définies dans `props`.
	void Apply(const StyleProps &props);

	[[nodiscard]] math::Sides Margin() const noexcept {
		return math::Sides(marginLeft, marginTop, marginRight, marginBottom);
	}
	[[nodiscard]] math::Sides Padding() const noexcept {
		return math::Sides(paddingLeft, paddingTop, paddingRight, paddingBottom);
	}
	[[nodiscard]] math::Sides BorderWidth() const noexcept {
		return math::Sides(borderLeftWidth, borderTopWidth, borderRightWidth, borderBottomWidth);
	}
	[[nodiscard]] math::Corners BorderRadius() const noexcept {
		return math::Corners(borderTopLeftRadius, borderTopRightRadius, borderBottomLeftRadius, borderBottomRightRadius);
	}
	[[nodiscard]] bool HasBorder() const noexcept {
		return borderColor.a > 0.f &&
			   (borderTopWidth > 0.f || borderRightWidth > 0.f || borderBottomWidth > 0.f || borderLeftWidth > 0.f);
	}
	/// Épaisseur de bordure qui occupe de la place dans la boîte (zéro sans
	/// bordure visible) — ce que la mise en page réserve.
	[[nodiscard]] math::Sides BorderBox() const noexcept { return HasBorder() ? BorderWidth() : math::Sides(); }

	[[nodiscard]] bool ClipsX() const noexcept { return overflowX != Overflow::Visible; }
	[[nodiscard]] bool ClipsY() const noexcept { return overflowY != Overflow::Visible; }
	[[nodiscard]] bool Clips() const noexcept { return ClipsX() || ClipsY(); }
	[[nodiscard]] bool IsRow() const noexcept {
		return flexDirection == FlexDirection::Row || flexDirection == FlexDirection::RowReverse;
	}
	[[nodiscard]] bool IsBold() const noexcept { return fontWeight >= 600; }
	[[nodiscard]] bool IsItalic() const noexcept { return fontStyle == FontStyle::Italic; }
	/// Hauteur d'une ligne de texte (line-height en multiple, 0 = police).
	[[nodiscard]] float LineHeightFor(float measured) const noexcept {
		return lineHeight > 0.f ? lineHeight * fontSize : measured;
	}
};

// ============================================================================
// Analyse des valeurs CSS
// ============================================================================

/// Contexte d'analyse : dossier de la feuille (chemins `url(...)` relatifs).
struct ValueContext {
	String baseDir;
};

[[nodiscard]] Option<Color> ParseColor(StringView text) noexcept;
[[nodiscard]] Option<Length> ParseLength(StringView text) noexcept;
/// Nombre de pixels (accepte « 12 », « 12px »).
[[nodiscard]] Option<float> ParsePixels(StringView text) noexcept;

/// Pose une déclaration `nom: valeur` (raccourcis compris : margin, padding,
/// border, border-radius, overflow, background, flex, outline, inset, font).
/// Les valeurs contenant `var(` sont différées (cf. DeferredDeclaration).
/// Rend faux si la propriété est inconnue ou la valeur invalide — une faute de
/// frappe dans une feuille ne doit jamais interrompre l'application.
bool SetDeclaration(StyleProps &props, StringView name, StringView value, const ValueContext &ctx = {});

/// Remplace les `var(--x, défaut)` de `value` à partir de `vars`.
[[nodiscard]] String SubstituteVars(StringView value, const std::vector<CustomProperty> &vars);

/// Texte CSS d'une valeur calculée (débogage, inspection, scripts).
[[nodiscard]] String DescribeProperty(const ComputedStyle &style, Prop p);

} // namespace ui
