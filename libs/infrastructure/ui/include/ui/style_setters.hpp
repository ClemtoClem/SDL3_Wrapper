#pragma once
/**
 * ui::StyleSetters — setters fluents de TOUTES les propriétés de style,
 * partagés par `ui::Style` (valeur autonome), les builders et `ui::Widget` :
 * tout ce qu'une feuille CSS peut configurer l'est aussi en C++.
 *
 * @code{.cpp}
 * ui::Button("OK").Padding(8.f, 4.f).Background(ui::Rgba(40, 120, 220))
 *     .BorderRadius(6.f).HoverStyle(ui::Style().Background(ui::Rgba(60, 140, 240)));
 * @endcode
 *
 * `D` doit fournir `StyleProps &Props()` (la cible des setters).
 */
#include "style.hpp"

namespace ui {

template <typename D> class StyleSetters {
public:
	// ── Affichage et positionnement ─────────────────────────────────────────
	D &Display(ui::Display v) { return Set(&StyleProps::display, v); }
	D &Hide() { return Display(ui::Display::None); }
	D &Position(ui::Position v) { return Set(&StyleProps::position, v); }
	/// Hors flux, dans la boîte de contenu du parent.
	D &Absolute() { return Position(ui::Position::Absolute); }
	/// Hors flux, contre la fenêtre, dessiné au-dessus de tout.
	D &Fixed() { return Position(ui::Position::Fixed); }
	D &Anchor(ui::Anchor v) { return Set(&StyleProps::anchor, v); }
	D &Top(Length v) { return Set(&StyleProps::top, v); }
	D &Right(Length v) { return Set(&StyleProps::right, v); }
	D &Bottom(Length v) { return Set(&StyleProps::bottom, v); }
	D &Left(Length v) { return Set(&StyleProps::left, v); }
	D &Top(float px) { return Top(Length::Px(px)); }
	D &Right(float px) { return Right(Length::Px(px)); }
	D &Bottom(float px) { return Bottom(Length::Px(px)); }
	D &Left(float px) { return Left(Length::Px(px)); }
	/// Décalage depuis le point d'accroche (left/top).
	D &Offset(float x, float y) { return Left(Length::Px(x)).Top(Length::Px(y)); }
	D &ZIndex(int v) { return Set(&StyleProps::zIndex, v); }
	D &Zoom(float v) { return Set(&StyleProps::zoom, v); }

	// ── Taille ──────────────────────────────────────────────────────────────
	D &Width(Length v) { return Set(&StyleProps::width, v); }
	D &Height(Length v) { return Set(&StyleProps::height, v); }
	D &Width(float px) { return Width(Length::Px(px)); }
	D &Height(float px) { return Height(Length::Px(px)); }
	D &Size(float w, float h) { return Width(Length::Px(w)).Height(Length::Px(h)); }
	D &Size(Length w, Length h) { return Width(w).Height(h); }
	/// Part de l'espace restant (flex-grow sur l'axe principal du parent).
	D &Grow(float weight = 1.f) { return Set(&StyleProps::flexGrow, weight); }
	D &Shrink(float weight) { return Set(&StyleProps::flexShrink, weight); }
	/// Occupe toute la largeur / hauteur de contenu du parent.
	D &FillWidth() { return Width(Length::Pct(100.f)); }
	D &FillHeight() { return Height(Length::Pct(100.f)); }
	D &Fill() { return FillWidth().FillHeight(); }
	/// Occupe l'espace restant sur un axe, quelle que soit la direction du
	/// parent (`width: 1fr` / `height: 1fr`) : croît sur l'axe principal,
	/// s'étire sur l'axe secondaire.
	D &GrowWidth(float weight = 1.f) { return Width(Length::Fr(weight)); }
	D &GrowHeight(float weight = 1.f) { return Height(Length::Fr(weight)); }
	D &MinWidth(Length v) { return Set(&StyleProps::minWidth, v); }
	D &MinHeight(Length v) { return Set(&StyleProps::minHeight, v); }
	D &MaxWidth(Length v) { return Set(&StyleProps::maxWidth, v); }
	D &MaxHeight(Length v) { return Set(&StyleProps::maxHeight, v); }
	D &MinSize(float w, float h) { return MinWidth(Length::Px(w)).MinHeight(Length::Px(h)); }
	D &MaxSize(float w, float h) { return MaxWidth(Length::Px(w)).MaxHeight(Length::Px(h)); }

	// ── Boîte ───────────────────────────────────────────────────────────────
	D &Margin(const math::Sides &s) {
		Self().Props().SetMargin(s);
		return Self();
	}
	D &Margin(float all) { return Margin(math::Sides(all)); }
	D &Margin(float horizontal, float vertical) { return Margin(math::Sides(horizontal, vertical)); }
	D &MarginTop(float v) { return Set(&StyleProps::marginTop, v); }
	D &MarginRight(float v) { return Set(&StyleProps::marginRight, v); }
	D &MarginBottom(float v) { return Set(&StyleProps::marginBottom, v); }
	D &MarginLeft(float v) { return Set(&StyleProps::marginLeft, v); }
	D &Padding(const math::Sides &s) {
		Self().Props().SetPadding(s);
		return Self();
	}
	D &Padding(float all) { return Padding(math::Sides(all)); }
	D &Padding(float horizontal, float vertical) { return Padding(math::Sides(horizontal, vertical)); }
	D &PaddingTop(float v) { return Set(&StyleProps::paddingTop, v); }
	D &PaddingRight(float v) { return Set(&StyleProps::paddingRight, v); }
	D &PaddingBottom(float v) { return Set(&StyleProps::paddingBottom, v); }
	D &PaddingLeft(float v) { return Set(&StyleProps::paddingLeft, v); }

	// ── Flexbox / grille ────────────────────────────────────────────────────
	D &Direction(FlexDirection v) { return Set(&StyleProps::flexDirection, v); }
	D &Horizontal() { return Direction(FlexDirection::Row); }
	D &Vertical() { return Direction(FlexDirection::Column); }
	D &Wrap(bool on = true) { return Set(&StyleProps::flexWrap, on ? FlexWrap::Wrap : FlexWrap::NoWrap); }
	D &JustifyContent(Justify v) { return Set(&StyleProps::justifyContent, v); }
	D &AlignItems(Align v) { return Set(&StyleProps::alignItems, v); }
	D &AlignSelf(Align v) { return Set(&StyleProps::alignSelf, v); }
	/// Centre le contenu sur les deux axes.
	D &Center() { return JustifyContent(Justify::Center).AlignItems(Align::Center); }
	D &Gap(float px) { return Set(&StyleProps::gap, px); }
	D &GridColumns(int n) { return Set(&StyleProps::gridColumns, n); }

	// ── Débordement ─────────────────────────────────────────────────────────
	D &Overflow(ui::Overflow v) { return Set(&StyleProps::overflowX, v).Set(&StyleProps::overflowY, v); }
	D &OverflowX(ui::Overflow v) { return Set(&StyleProps::overflowX, v); }
	D &OverflowY(ui::Overflow v) { return Set(&StyleProps::overflowY, v); }
	/// Rogne les enfants à sa boîte (`overflow: hidden` — remplace donc un
	/// `Scrollable()` précédent ; une boîte qui défile rogne déjà).
	D &Clip() { return Overflow(ui::Overflow::Hidden); }
	/// Défilement (barres automatiques, molette, glisser des pouces).
	D &Scrollable() { return Overflow(ui::Overflow::Auto); }

	// ── Fond, bordure, effets ───────────────────────────────────────────────
	D &Background(ui::Color c) { return Set(&StyleProps::backgroundColor, c); }
	D &BackgroundColor(ui::Color c) { return Background(c); }
	D &BackgroundImage(Paint p) { return Set(&StyleProps::backgroundImage, std::move(p)); }
	/// Dégradé linéaire (angle CSS : 180 = vers le bas, 90 = vers la droite).
	D &Gradient(ui::Color from, ui::Color to, float angleDeg = 180.f) {
		return BackgroundImage(Paint::LinearGradient(from, to, angleDeg));
	}
	D &ObjectFit(ui::ObjectFit v) { return Set(&StyleProps::objectFit, v); }
	D &BorderColor(ui::Color c) { return Set(&StyleProps::borderColor, c); }
	D &BorderWidth(const math::Sides &s) {
		Self().Props().SetBorderWidth(s);
		return Self();
	}
	D &BorderWidth(float all) { return BorderWidth(math::Sides(all)); }
	D &Border(float width, ui::Color c) { return BorderWidth(width).BorderColor(c); }
	D &BorderRadius(const math::Corners &c) {
		Self().Props().SetBorderRadius(c);
		return Self();
	}
	D &BorderRadius(float all) { return BorderRadius(math::Corners(all)); }
	D &BoxShadow(Shadow s) { return Set(&StyleProps::boxShadow, s); }
	D &BoxShadow(float x, float y, float blur, ui::Color c) {
		Shadow s;
		s.x = x;
		s.y = y;
		s.blur = blur;
		s.color = c;
		return BoxShadow(s);
	}
	D &Opacity(float v) { return Set(&StyleProps::opacity, v); }
	D &Outline(float width, ui::Color c) { return Set(&StyleProps::outlineWidth, width).Set(&StyleProps::outlineColor, c); }
	D &OutlineOffset(float px) { return Set(&StyleProps::outlineOffset, px); }
	D &Gloss(float v) { return Set(&StyleProps::gloss, v); }
	D &Glow(float v) { return Set(&StyleProps::glow, v); }
	D &GlowColor(ui::Color c) { return Set(&StyleProps::glowColor, c); }
	/// Effet « verre » (thème Aero) : reflet + lueur de bordure.
	D &Glass(float gloss = 0.35f, float glow = 0.45f) { return Gloss(gloss).Glow(glow); }

	// ── Interaction ─────────────────────────────────────────────────────────
	D &Visibility(ui::Visibility v) { return Set(&StyleProps::visibility, v); }
	D &PointerEvents(ui::PointerEvents v) { return Set(&StyleProps::pointerEvents, v); }
	/// Transparent au pointeur : le widget derrière reçoit survol et clics.
	D &PointerThrough() { return PointerEvents(ui::PointerEvents::None); }
	D &Cursor(ui::Cursor v) { return Set(&StyleProps::cursor, v); }

	// ── Texte ───────────────────────────────────────────────────────────────
	D &Color(ui::Color c) { return Set(&StyleProps::color, c); }
	D &TextColor(ui::Color c) { return Color(c); }
	D &FontFamily(String family) { return Set(&StyleProps::fontFamily, std::move(family)); }
	D &FontSize(float px) { return Set(&StyleProps::fontSize, px); }
	D &FontWeight(int w) { return Set(&StyleProps::fontWeight, w); }
	D &Bold(bool on = true) { return FontWeight(on ? 700 : 400); }
	D &FontStyle(ui::FontStyle v) { return Set(&StyleProps::fontStyle, v); }
	D &Italic(bool on = true) { return FontStyle(on ? ui::FontStyle::Italic : ui::FontStyle::Normal); }
	D &LineHeight(float multiple) { return Set(&StyleProps::lineHeight, multiple); }
	D &TextAlign(ui::TextAlign v) { return Set(&StyleProps::textAlign, v); }
	D &VerticalAlign(ui::VerticalAlign v) { return Set(&StyleProps::verticalAlign, v); }
	D &TextDecoration(ui::TextDecoration v) { return Set(&StyleProps::textDecoration, v); }
	D &Underline(bool on = true) { return Decorate(ui::TextDecoration::Underline, on); }
	D &Strikethrough(bool on = true) { return Decorate(ui::TextDecoration::LineThrough, on); }
	D &Overline(bool on = true) { return Decorate(ui::TextDecoration::Overline, on); }
	D &DecorationColor(ui::Color c) { return Set(&StyleProps::textDecorationColor, c); }
	D &TextOverflow(ui::TextOverflow v) { return Set(&StyleProps::textOverflow, v); }
	D &TextClip() { return TextOverflow(ui::TextOverflow::Clip); }
	D &Ellipsis() { return TextOverflow(ui::TextOverflow::Ellipsis); }
	D &TextScroll() { return TextOverflow(ui::TextOverflow::Scroll); }
	/// Retour à la ligne (la boîte grandit ; bornée, une barre apparaît).
	D &TextWrap(ui::TextAlign align = ui::TextAlign::Left) { return TextOverflow(ui::TextOverflow::Wrap).TextAlign(align); }
	D &Marquee(float speedPxPerSecond = 40.f, float pauseSeconds = 1.f) {
		return TextOverflow(ui::TextOverflow::Marquee)
			.Set(&StyleProps::marqueeSpeed, speedPxPerSecond)
			.Set(&StyleProps::marqueePause, pauseSeconds);
	}
	D &TabSize(int columns) { return Set(&StyleProps::tabSize, columns); }

	// ── Couleurs d'accompagnement ───────────────────────────────────────────
	D &AccentColor(ui::Color c) { return Set(&StyleProps::accentColor, c); }
	D &CaretColor(ui::Color c) { return Set(&StyleProps::caretColor, c); }
	D &PlaceholderColor(ui::Color c) { return Set(&StyleProps::placeholderColor, c); }
	D &SelectionColor(ui::Color c) { return Set(&StyleProps::selectionColor, c); }
	D &ScrollbarColors(ui::Color thumb, ui::Color track) {
		return Set(&StyleProps::scrollbarThumbColor, thumb).Set(&StyleProps::scrollbarTrackColor, track);
	}
	D &ScrollbarWidth(float px) { return Set(&StyleProps::scrollbarWidth, px); }
	D &TransitionDuration(float seconds) { return Set(&StyleProps::transitionDuration, seconds); }

	// ── Générique ───────────────────────────────────────────────────────────
	/// Propriété personnalisée CSS (`--nom`), utilisable par `var(--nom)`.
	D &Var(StringView name, StringView value) {
		String n(name);
		if (n.StartsWith("--"))
			n = n.Substr(2);
		Self().Props().SetCustom(std::move(n), String(value));
		return Self();
	}
	/// Déclaration CSS textuelle (`Declare("border", "1px solid #333")`).
	D &Declare(StringView property, StringView value) {
		(void)SetDeclaration(Self().Props(), property, value);
		return Self();
	}
	/// Fusionne des valeurs déjà construites.
	D &Merge(const StyleProps &props) {
		Self().Props().Merge(props);
		return Self();
	}

protected:
	template <typename T, typename V> D &Set(Option<T> StyleProps::*member, V &&value) {
		Self().Props().*member = Some(T(std::forward<V>(value)));
		return Self();
	}
	D &Decorate(ui::TextDecoration flag, bool on) {
		StyleProps &p = Self().Props();
		const uint8_t cur = p.textDecoration.IsSome() ? uint8_t(p.textDecoration.Unwrap()) : 0;
		const uint8_t next = on ? uint8_t(cur | uint8_t(flag)) : uint8_t(cur & ~uint8_t(flag));
		p.textDecoration = Some(ui::TextDecoration(next));
		return Self();
	}

private:
	D &Self() { return static_cast<D &>(*this); }
};

/// Ensemble de valeurs de style autonome (variantes d'état, `.Cascade`,
/// `Widget::SetStyle`).
class Style : public StyleSetters<Style> {
public:
	Style() = default;
	explicit Style(StyleProps props) : m_props(std::move(props)) {}
	/// Analyse un bloc de déclarations CSS : `Style("color: red; padding: 4px")`.
	explicit Style(StringView css);

	[[nodiscard]] StyleProps &Props() noexcept { return m_props; }
	[[nodiscard]] const StyleProps &Props() const noexcept { return m_props; }

private:
	StyleProps m_props;
};

} // namespace ui
