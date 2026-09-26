#pragma once
/**
 * ui::Factory — UiTheme (palette + tables de couleurs par widget/état),
 * WidgetBuilder (DSL fluent) et UiFactory (fabrique à thème).
 *
 * Usage :
 * @code{.cpp}
 * ui::LayoutSystem layout;
 * ui::UiFactory f(world, layout);          // thème sombre par défaut
 *
 * f.Panel().Size(300, 0).HAuto().Pad(16).Gap(10)
 *   .Children(
 *       f.Label("Options").FontSize(20),
 *       f.Row().Gap(8).Children(
 *           f.Label("Volume").GrowW(),
 *           f.Slider(0.f, 1.f, 0.8f).W(Dimension::Px(140))
 *               .OnChange([](float v) { ... })),
 *       f.Button("OK").OnClick([] { ... }))
 *   .Spawn();
 * @endcode
 */
#include <memory>

#include "components.hpp"
#include "glyphs.hpp"
#include "interaction.hpp"
#include "plot.hpp"
#include "styles.hpp"
#include "systems.hpp"
#include "viewport3d.hpp"

namespace ui {

// ============================================================================
// UiTheme
// ============================================================================

/// Construit une WidgetColors en ne renseignant que les 7 champs
/// effectivement lus par StyleFromColors() (bgNormal/bgHovered/bgPressed/
/// bgChecked/borderNormal/borderFocus/textNormal) — les autres champs
/// (bgFocus, border{Hovered,Pressed,Checked}, text{Hovered,Pressed,
/// Highlighted}) restent à leur valeur par défaut (transparent), réservés à
/// un usage futur. Volontairement une fonction (affectations de champs) et
/// non un agrégat désigné : GCC (-Wextra) avertit (-Werror en fait une
/// erreur) sur un agrégat désigné qui ne couvre pas tous les champs, même en
/// C++20.
[[nodiscard]] inline WidgetColors MakeWidgetColors(sdl3::FColor bgNormal, sdl3::FColor bgHovered,
													sdl3::FColor bgPressed, sdl3::FColor bgChecked,
													sdl3::FColor borderNormal, sdl3::FColor borderFocus,
													sdl3::FColor textNormal) {
	WidgetColors c;
	c.bgNormal = bgNormal;
	c.bgHovered = bgHovered;
	c.bgPressed = bgPressed;
	c.bgChecked = bgChecked;
	c.borderNormal = borderNormal;
	c.borderFocus = borderFocus;
	c.textNormal = textNormal;
	return c;
}

struct UiTheme {
	float fontSize = 14.f;

	/// Si vrai, `UiFactory::Panel()`/`button()` (et les widgets composites
	/// des phases suivantes) attachent automatiquement un style « verre »
	/// (cf. UiStyle::glassPanel/glassButton, styles.hpp) au lieu de leur
	/// remplissage plat habituel — activé par `UiTheme::Aero()`.
	bool glassDefault = false;

	// Palette de base
	sdl3::FColor panelBg{30 / 255.f, 32 / 255.f, 44 / 255.f, 235 / 255.f};
	sdl3::FColor text = sdl3::FColor::UI_TEXT_PRIMARY();
	sdl3::FColor muted{150 / 255.f, 156 / 255.f, 178 / 255.f, 1.f};
	sdl3::FColor track = sdl3::FColor::UI_PANEL_DARK();
	sdl3::FColor fill = sdl3::FColor::UI_ACCENT_BLUE();
	sdl3::FColor thumb = sdl3::FColor::UI_ACCENT_BLUE_LIGHT();
	sdl3::FColor border = sdl3::FColor::UI_BORDER_MUTED();
	sdl3::FColor fieldBg{20 / 255.f, 24 / 255.f, 36 / 255.f, 220 / 255.f};
	sdl3::FColor accent = sdl3::FColor::UI_ACCENT_BLUE_BRIGHT();

	// Tables par widget / état
	WidgetColors button{};
	WidgetColors toggle{};
	WidgetColors checkbox{};
	WidgetColors slider{};
	WidgetColors input{};
	WidgetColors dragValue{}; ///< normal=fond, checked=bande de remplissage, borderFocus=édition
	WidgetColors radio{};
	WidgetColors scrollbar{};
	WidgetColors knob{};
	WidgetColors combo{};    ///< pressed = inutilisé ; checked = item sélectionné
	WidgetColors listbox{};  ///< pressed = pouce de la barre inline
	WidgetColors expander{}; ///< normal/hovered = fond d'en-tête
	WidgetColors tabs{};     ///< checked = onglet actif ; borderFocus = soulignement
	sdl3::FColor spinner{};
	sdl3::FColor badgeBg{};
	sdl3::FColor badgeText{};

	[[nodiscard]] static UiTheme Dark() {
		UiTheme t;
		sdl3::FColor hover{55 / 255.f, 60 / 255.f, 84 / 255.f, 1.f};
		sdl3::FColor focus{120 / 255.f, 170 / 255.f, 250 / 255.f, 1.f};
		t.button = MakeWidgetColors(sdl3::FColor{40 / 255.f, 44 / 255.f, 64 / 255.f, 1.f},
									 sdl3::FColor{70 / 255.f, 78 / 255.f, 110 / 255.f, 1.f},
									 sdl3::FColor{24 / 255.f, 26 / 255.f, 40 / 255.f, 1.f}, t.fill,
									 sdl3::FColor{0.f, 0.f, 0.f, 120 / 255.f}, sdl3::FColor{1.f, 1.f, 1.f, 60 / 255.f},
									 t.text);
		t.toggle = MakeWidgetColors(t.track, hover, t.track, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.checkbox = t.toggle;
		t.slider = MakeWidgetColors(t.track, t.thumb, t.fill, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.input = MakeWidgetColors(sdl3::FColor{28 / 255.f, 30 / 255.f, 44 / 255.f, 1.f},
									sdl3::FColor{120 / 255.f, 124 / 255.f, 150 / 255.f, 1.f}, focus,
									sdl3::FColor::WHITE(), t.border, focus, t.text);
		t.dragValue = MakeWidgetColors(sdl3::FColor{28 / 255.f, 30 / 255.f, 44 / 255.f, 1.f}, hover, focus, t.fill,
										t.border, focus, t.text);
		t.radio = t.toggle;
		t.scrollbar = MakeWidgetColors(t.track, t.thumb, t.fill, t.thumb, sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.knob = t.toggle;
		t.combo = MakeWidgetColors(sdl3::FColor{28 / 255.f, 30 / 255.f, 44 / 255.f, 1.f}, hover, t.thumb,
									sdl3::FColor::UI_ACCENT_BLUE_DEEP(), t.border, focus, t.text);
		t.listbox = MakeWidgetColors(sdl3::FColor{24 / 255.f, 26 / 255.f, 38 / 255.f, 1.f}, hover, t.thumb,
									  sdl3::FColor::UI_ACCENT_BLUE_DEEP(), t.border, focus, t.text);
		t.expander = t.toggle;
		t.tabs = MakeWidgetColors(sdl3::FColor{24 / 255.f, 26 / 255.f, 36 / 255.f, 1.f}, hover, t.track,
								   sdl3::FColor{45 / 255.f, 52 / 255.f, 76 / 255.f, 1.f}, sdl3::FColor{}, t.accent,
								   t.text);
		t.spinner = t.accent;
		t.badgeBg = sdl3::FColor{200 / 255.f, 60 / 255.f, 50 / 255.f, 1.f};
		t.badgeText = sdl3::FColor::WHITE();
		return t;
	}

	[[nodiscard]] static UiTheme Light() {
		UiTheme t;
		t.panelBg = sdl3::FColor{240 / 255.f, 242 / 255.f, 245 / 255.f, 250 / 255.f};
		t.text = sdl3::FColor{20 / 255.f, 24 / 255.f, 32 / 255.f, 1.f};
		t.muted = sdl3::FColor{100 / 255.f, 105 / 255.f, 120 / 255.f, 1.f};
		t.track = sdl3::FColor{210 / 255.f, 214 / 255.f, 220 / 255.f, 1.f};
		t.fill = sdl3::FColor{40 / 255.f, 120 / 255.f, 210 / 255.f, 1.f};
		t.thumb = sdl3::FColor{60 / 255.f, 140 / 255.f, 230 / 255.f, 1.f};
		t.border = sdl3::FColor{180 / 255.f, 190 / 255.f, 205 / 255.f, 1.f};
		t.fieldBg = sdl3::FColor::WHITE();
		t.accent = sdl3::FColor{40 / 255.f, 120 / 255.f, 210 / 255.f, 1.f};
		sdl3::FColor hover{200 / 255.f, 205 / 255.f, 215 / 255.f, 1.f};
		sdl3::FColor focus = t.accent;
		t.button = MakeWidgetColors(sdl3::FColor{225 / 255.f, 230 / 255.f, 235 / 255.f, 1.f}, hover,
									 sdl3::FColor{180 / 255.f, 185 / 255.f, 195 / 255.f, 1.f}, t.fill,
									 sdl3::FColor{160 / 255.f, 168 / 255.f, 182 / 255.f, 1.f}, focus, t.text);
		t.toggle = MakeWidgetColors(t.track, hover, t.track, t.fill, sdl3::FColor{}, sdl3::FColor{},
									 sdl3::FColor::WHITE());
		t.checkbox = t.toggle;
		t.slider = MakeWidgetColors(t.track, t.thumb, t.fill, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.input = MakeWidgetColors(t.fieldBg, t.muted, focus, sdl3::FColor{20 / 255.f, 24 / 255.f, 32 / 255.f, 1.f},
									t.border, focus, t.text);
		t.dragValue = MakeWidgetColors(t.fieldBg, hover, focus, t.fill, t.border, focus, t.text);
		t.radio = t.toggle;
		t.scrollbar = MakeWidgetColors(t.track, t.thumb, t.fill, t.thumb, sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.knob = t.toggle;
		t.combo = MakeWidgetColors(t.fieldBg, hover, t.thumb, sdl3::FColor{150 / 255.f, 190 / 255.f, 240 / 255.f, 1.f},
									t.border, focus, t.text);
		t.listbox = t.combo;
		t.expander = t.toggle;
		t.tabs = MakeWidgetColors(sdl3::FColor{225 / 255.f, 228 / 255.f, 234 / 255.f, 1.f}, hover, t.track,
								   sdl3::FColor{205 / 255.f, 215 / 255.f, 232 / 255.f, 1.f}, sdl3::FColor{}, t.accent,
								   t.text);
		t.spinner = t.accent;
		t.badgeBg = sdl3::FColor{200 / 255.f, 60 / 255.f, 50 / 255.f, 1.f};
		t.badgeText = sdl3::FColor::WHITE();
		return t;
	}

	/// Thème « atelier » des éditeurs de jeu (Godot, Unity, Unreal) : gris
	/// neutres sans teinte, contraste modéré, un seul bleu pour la sélection
	/// et le focus. Là où `Dark()` est bleuté et contrasté pour une
	/// application, `Studio()` s'efface derrière le contenu — une vue 3D, du
	/// code — qui doit rester ce que l'œil voit en premier.
	[[nodiscard]] static UiTheme Studio() {
		UiTheme t;
		auto grey = [](int v, int a = 255) { return sdl3::FColor{v / 255.f, v / 255.f, v / 255.f, a / 255.f}; };
		t.fontSize = 14.f;
		t.panelBg = grey(40);
		t.text = grey(214);
		t.muted = grey(142);
		t.track = grey(58);
		t.fill = sdl3::FColor{66 / 255.f, 118 / 255.f, 196 / 255.f, 1.f};
		t.thumb = sdl3::FColor{98 / 255.f, 146 / 255.f, 214 / 255.f, 1.f};
		t.border = grey(24);
		t.fieldBg = grey(28);
		t.accent = sdl3::FColor{84 / 255.f, 140 / 255.f, 222 / 255.f, 1.f};
		const sdl3::FColor hover = grey(64);
		const sdl3::FColor focus = t.accent;
		const sdl3::FColor selection{46 / 255.f, 88 / 255.f, 150 / 255.f, 1.f};
		t.button = MakeWidgetColors(grey(52), hover, grey(36), t.fill, grey(22), focus, t.text);
		t.toggle = MakeWidgetColors(grey(30), hover, grey(30), t.fill, grey(20), focus, t.text);
		t.checkbox = t.toggle;
		t.slider = MakeWidgetColors(t.track, t.thumb, t.fill, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.input = MakeWidgetColors(t.fieldBg, t.muted, focus, sdl3::FColor::WHITE(), grey(20), focus, t.text);
		t.dragValue = MakeWidgetColors(t.fieldBg, grey(36), focus, t.fill, grey(20), focus, t.text);
		t.radio = t.toggle;
		t.scrollbar = MakeWidgetColors(grey(34), grey(88), grey(110), grey(88), sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.knob = t.toggle;
		t.combo = MakeWidgetColors(t.fieldBg, grey(36), t.thumb, selection, grey(20), focus, t.text);
		t.listbox = MakeWidgetColors(sdl3::FColor{}, grey(52), t.thumb, selection, grey(20), focus, t.text);
		t.expander = MakeWidgetColors(grey(46), grey(54), grey(46), grey(46), sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.tabs = MakeWidgetColors(grey(32), hover, grey(32), grey(40), sdl3::FColor{}, t.accent, t.text);
		t.spinner = t.accent;
		t.badgeBg = sdl3::FColor{196 / 255.f, 62 / 255.f, 52 / 255.f, 1.f};
		t.badgeText = sdl3::FColor::WHITE();
		return t;
	}

	/// Thème « verre » Frutiger Aero (Windows 7) : `glassDefault=true` fait
	/// que `panel()`/`button()`/`popup()` (cf. leurs corps) attachent un
	/// style `UiStyle::glassPanel()`/`glassButton()` inline au lieu de leur
	/// remplissage plat habituel — palette bleutée translucide, pas de rendu
	/// spécifique en plus de ce mécanisme déjà en place depuis la Phase 0.
	[[nodiscard]] static UiTheme Aero() {
		UiTheme t = Dark();
		t.glassDefault = true;
		t.panelBg = sdl3::FColor{70 / 255.f, 120 / 255.f, 190 / 255.f, 130 / 255.f};
		t.text = sdl3::FColor::WHITE();
		t.muted = sdl3::FColor{210 / 255.f, 220 / 255.f, 235 / 255.f, 255 / 255.f};
		t.track = sdl3::FColor{40 / 255.f, 60 / 255.f, 90 / 255.f, 140 / 255.f};
		t.fill = sdl3::FColor{60 / 255.f, 140 / 255.f, 230 / 255.f, 255 / 255.f};
		t.thumb = sdl3::FColor{120 / 255.f, 180 / 255.f, 240 / 255.f, 255 / 255.f};
		t.border = sdl3::FColor::UI_WHITE_SOFT();
		t.fieldBg = sdl3::FColor{20 / 255.f, 30 / 255.f, 50 / 255.f, 160 / 255.f};
		t.accent = sdl3::FColor{100 / 255.f, 180 / 255.f, 250 / 255.f, 255 / 255.f};
		sdl3::FColor hover{90 / 255.f, 150 / 255.f, 220 / 255.f, 160 / 255.f};
		sdl3::FColor focus{140 / 255.f, 200 / 255.f, 250 / 255.f, 1.f};
		t.button = MakeWidgetColors(sdl3::FColor{60 / 255.f, 120 / 255.f, 200 / 255.f, 150 / 255.f}, hover,
									 sdl3::FColor{40 / 255.f, 90 / 255.f, 160 / 255.f, 180 / 255.f}, t.fill,
									 sdl3::FColor{1.f, 1.f, 1.f, 70 / 255.f}, focus, t.text);
		t.toggle = MakeWidgetColors(t.track, hover, t.track, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.checkbox = t.toggle;
		t.slider = MakeWidgetColors(t.track, t.thumb, t.fill, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.input = MakeWidgetColors(sdl3::FColor{20 / 255.f, 30 / 255.f, 50 / 255.f, 170 / 255.f},
									sdl3::FColor{160 / 255.f, 190 / 255.f, 220 / 255.f, 1.f}, focus,
									sdl3::FColor::WHITE(), t.border, focus, t.text);
		t.dragValue = t.input;
		t.radio = t.toggle;
		t.scrollbar = MakeWidgetColors(t.track, t.thumb, t.fill, t.thumb, sdl3::FColor{}, sdl3::FColor{}, t.text);
		t.knob = t.toggle;
		t.combo = MakeWidgetColors(sdl3::FColor{30 / 255.f, 45 / 255.f, 70 / 255.f, 180 / 255.f}, hover, t.thumb,
									sdl3::FColor{60 / 255.f, 120 / 255.f, 200 / 255.f, 1.f}, t.border, focus, t.text);
		t.listbox = t.combo;
		t.expander = t.toggle;
		t.tabs = MakeWidgetColors(sdl3::FColor{30 / 255.f, 45 / 255.f, 70 / 255.f, 160 / 255.f}, hover, t.track,
								   sdl3::FColor{60 / 255.f, 110 / 255.f, 190 / 255.f, 1.f}, sdl3::FColor{}, t.accent,
								   t.text);
		t.spinner = t.accent;
		t.badgeBg = sdl3::FColor{220 / 255.f, 80 / 255.f, 70 / 255.f, 1.f};
		t.badgeText = sdl3::FColor::WHITE();
		return t;
	}
};

/// Convertit une table WidgetColors en style résolvable (normal→Bg,
/// hovered→BgHovered, pressed→BgPressed, checked→BgChecked, text→TextColor,
/// border→BorderColor, borderFocus→FocusedBorderColor). Base des classes
/// "root-<kind>" enregistrées par UiFactory (cf. registerRootClasses()).
[[nodiscard]] inline UiStyle StyleFromColors(const WidgetColors &c) {
	UiStyle s;
	s.SetBg(c.bgNormal)
		.SetBgHovered(c.bgHovered)
		.SetBgPressed(c.bgPressed)
		.SetBgChecked(c.bgChecked)
		.SetTextColor(c.textNormal)
		.SetBorderColor(c.borderNormal)
		.SetBorderFocus(c.borderFocus);
	return s;
}

// ============================================================================
// WidgetBuilder — accumule la configuration, Spawn() crée l'entité
// ============================================================================

class UiFactory;

class WidgetBuilder {
public:
	WidgetBuilder(ecs::ArchetypeRegistry &world, LayoutSystem &layout) : world(&world), layout(&layout) {}

	WidgetBuilder(WidgetBuilder &&) = default;
	WidgetBuilder &operator=(WidgetBuilder &&) = default;
	WidgetBuilder(const WidgetBuilder &) = delete;
	WidgetBuilder &operator=(const WidgetBuilder &) = delete;

	// ── Placement / taille ───────────────────────────────────────────────────

	WidgetBuilder &W(Dimension v) {
		Item().width = v;
		return *this;
	}
	WidgetBuilder &H(Dimension v) {
		Item().height = v;
		return *this;
	}
	WidgetBuilder &Size(float pxW, float pxH) { return W(Dimension::Px(pxW)).H(Dimension::Px(pxH)); }
	WidgetBuilder &WAuto() { return W(Dimension::Auto()); }
	WidgetBuilder &HAuto() { return H(Dimension::Auto()); }
	WidgetBuilder &GrowW(float weight = 1.f) { return W(Dimension::Grow(weight)); }
	WidgetBuilder &GrowH(float weight = 1.f) { return H(Dimension::Grow(weight)); }

	WidgetBuilder &Anchor(Anchor a) {
		rect.anchor = a;
		return *this;
	}
	WidgetBuilder &Offset(float x, float y) {
		rect.offset = {x, y};
		return *this;
	}
	WidgetBuilder &Clip() {
		rect.clipContent = true;
		return *this;
	}

	/// Mode d'attache au parent (cf. AttachLayout : Relative/Absolute/Fixed).
	WidgetBuilder &AttachLayout(AttachLayout a) {
		Item().attach = a;
		return *this;
	}
	/// Positionné en absolu dans le parent (ancre + offset, hors flow), mais
	/// toujours dessiné à sa place normale dans l'arbre (sous les widgets
	/// dessinés après lui — peut être recouvert par eux).
	WidgetBuilder &Absolute() { return AttachLayout(AttachLayout::ABSOLUTE); }
	/// Comme absolute(), mais en plus détaché de l'arbre de DESSIN : reporté
	/// par RenderSystem dans une passe overlay après tout le reste, pour
	/// flotter au premier plan quel que soit son parent logique (menus
	/// déroulants, HUD, panneaux flottants...).
	WidgetBuilder &Fixed() { return AttachLayout(AttachLayout::FIXED); }

	WidgetBuilder &Margin(math::Sides m) {
		Item().margin = m;
		return *this;
	}
	/// Même marge sur les quatre côtés — symétrique de `Pad(float)`, qui
	/// existait déjà ; son absence obligeait à écrire `Margin(math::Sides{0})`
	/// pour la valeur la plus courante.
	WidgetBuilder &Margin(float m) { return Margin(math::Sides(m)); }
	WidgetBuilder &AlignSelf(CrossAlign a) {
		Item().alignSelf = Some(a);
		return *this;
	}
	WidgetBuilder &MinSize(float mw, float mh) {
		Item().minWidth = Some(mw);
		Item().minHeight = Some(mh);
		return *this;
	}
	WidgetBuilder &MaxSize(float mw, float mh) {
		Item().maxWidth = Some(mw);
		Item().maxHeight = Some(mh);
		return *this;
	}

	WidgetBuilder &Hidden() {
		hidden = true;
		return *this;
	}
	/// Widget inerte et grisé (UiDisabled, récursif sur le sous-arbre).
	WidgetBuilder &Disabled() {
		disabled = true;
		return *this;
	}
	/// Widget transparent au pointeur (UiPointerThrough, NON récursif) : il
	/// est dessiné, mais c'est le widget situé DERRIÈRE lui qui reçoit survol
	/// et clics — cf. HitTestIndex (components.hpp). Pour une décoration posée
	/// par-dessus une zone interactive (étiquette, voile, cadre).
	WidgetBuilder &PointerThrough() {
		pointerThrough = true;
		return *this;
	}
	/// Infobulle affichée près de la souris après un court survol.
	WidgetBuilder &Tooltip(String text) {
		tooltip = Some(UiTooltip{std::move(text)});
		return *this;
	}
	WidgetBuilder &Name(String n) {
		name = Some(UiName{std::move(n)});
		return *this;
	}
	WidgetBuilder &Parent(ecs::Entity p) {
		parent = Some(p);
		return *this;
	}

	// ── Conteneur flow ───────────────────────────────────────────────────────

	WidgetBuilder &Column() {
		Flow().dir = LayoutDir::Column;
		return *this;
	}
	WidgetBuilder &Row() {
		Flow().dir = LayoutDir::Row;
		return *this;
	}
	WidgetBuilder &Gap(float g) {
		Flow().gap = g;
		return *this;
	}
	WidgetBuilder &Pad(float p) {
		Flow().padding = math::Sides(p);
		return *this;
	}
	WidgetBuilder &Pad(math::Sides p) {
		Flow().padding = p;
		return *this;
	}
	WidgetBuilder &Justify(Justify j) {
		Flow().justify = j;
		return *this;
	}
	WidgetBuilder &Align(CrossAlign a) {
		Flow().align = a;
		return *this;
	}
	/// Effet shader post-traitement du sous-arbre de ce widget (M23,
	/// shader_effect.hpp) — bas niveau, cf. `.TintShiftEffect()`/
	/// `.GlowEffect()` ci-dessous pour les deux presets tout faits. Composant
	/// pur (aucun état GPU dessus, cf. UiShaderEffect) : le pipeline/les
	/// textures réels sont gérés par ui::ShaderEffectSystem, appelé par
	/// ui::Ui::Render() si un render3d::Canvas a été enregistré
	/// (Ui::Initialize(render3d::Canvas&)) — no-op sinon, comme Viewport3D.
	WidgetBuilder &Shader(UiShaderEffect effect) {
		shaderEffect = Some(effect);
		return *this;
	}
	/// Préréglage : virage de teinte — outColor.rgb = lerp(src.rgb, src.rgb *
	/// `color`.rgb, `strength`). `strength`=0 laisse le contenu inchangé,
	/// `strength`=1 applique le virage plein.
	WidgetBuilder &TintShiftEffect(sdl3::FColor color, float strength = 1.f) {
		return Shader(UiShaderEffect{UiShaderEffectKind::TINT_SHIFT, color, strength});
	}
	/// Préréglage : halo radial — outColor.rgb = lerp(src.rgb, `color`.rgb,
	/// smoothstep(`innerRadius`, 0.5, distance(uv, (0.5,0.5)))). `innerRadius`
	/// dans [0, 0.5) : plus petit = halo plus large (commence plus près du
	/// centre).
	WidgetBuilder &GlowEffect(sdl3::FColor color, float innerRadius = 0.3f) {
		return Shader(UiShaderEffect{UiShaderEffectKind::GLOW, color, innerRadius});
	}

	/// Conteneur scrollable (clip + molette gérée par InputSystem).
	WidgetBuilder &Scrollable() {
		rect.clipContent = true;
		return *this;
	}

	// ── Style ────────────────────────────────────────────────────────────────
	// Toutes ces méthodes posent des propriétés dans le style INLINE de
	// l'entité (priorité maximale de la cascade, cf. styles.hpp) — elles
	// s'appliquent quel que soit le type de widget (un widget sans fond
	// propre, ex. UiLabel, ignore juste bg/border puisque RenderSystem ne
	// les lit que pour les widgets qui dessinent un fond).

	/// Pose une couleur de fond ET s'assure qu'un fond sera dessiné (ajoute
	/// le marqueur UiPanel si absent — utilisable sur n'importe quel widget,
	/// pas seulement `panel()`, ex: colorer le fond d'une liste scrollable).
	WidgetBuilder &Bg(sdl3::FColor c) {
		Panel();
		StyleMut().SetBg(c);
		return *this;
	}
	WidgetBuilder &Gradient(sdl3::FColor bottom) {
		Panel();
		StyleMut().SetBgGradient(bottom);
		return *this;
	}
	WidgetBuilder &BorderColor(sdl3::FColor c, float width = 1.f) {
		Panel();
		StyleMut().SetBorderColor(c).SetBordersWidth(width);
		return *this;
	}
	WidgetBuilder &Radius(float r) {
		StyleMut().SetBordersRadius(r);
		return *this;
	}
	WidgetBuilder &FontSize(float fs) {
		StyleMut().SetFontSize(fs);
		return *this;
	}
	/// Échelle de contenu du sous-arbre (cf. prop::InnerZoom, styles.hpp —
	/// EmBases::scale/Dimension::Resolve()) : 1.0 = normal, 0.5 = deux fois
	/// plus petit, 2.0 = deux fois plus grand — composé multiplicativement
	/// avec les ancêtres. Générique (node-graph inner-zoom en est le premier
	/// usage, cf. plan), pas de dépendance au node-graph ici.
	WidgetBuilder &InnerZoom(float v) {
		StyleMut().SetInnerZoom(v);
		return *this;
	}
	WidgetBuilder &TextColor(sdl3::FColor c) {
		StyleMut().SetTextColor(c);
		return *this;
	}
	// ── Débordement du texte (cf. TextOverflow, components.hpp) ─────────────
	//
	// Ces cinq méthodes décrivent CE QUI ARRIVE quand la chaîne est plus large
	// que le widget. Aucune ne contraint la largeur : un widget libre s'étend
	// toujours jusqu'à son texte. Elles n'agissent qu'une fois la largeur
	// bornée — par `W(...)`, par `MaxSize(...)` ou par l'étirement du parent.

	/// Mode générique (les quatre raccourcis ci-dessous sont plus lisibles).
	WidgetBuilder &TextOverflowMode(ui::TextOverflow mode) {
		OverflowMut().mode = mode;
		return *this;
	}

	/// Rogne au bord du widget — le comportement par défaut, explicité.
	WidgetBuilder &TextClip() { return TextOverflowMode(ui::TextOverflow::CLIP); }

	/// Rogne en terminant par « … ».
	WidgetBuilder &TextEllipsis() { return TextOverflowMode(ui::TextOverflow::ELLIPSIS); }

	/// Barre de défilement horizontale quand le texte ne tient pas.
	WidgetBuilder &TextScroll() { return TextOverflowMode(ui::TextOverflow::SCROLL); }

	/// Retour automatique à la ligne, avec l'alignement demandé
	/// (`TextAlign::Justify` répartit l'espace entre les mots). Le widget
	/// grandit en hauteur ; bornez-la (`H(...)`, `MaxSize`) pour obtenir à la
	/// place une barre de défilement verticale.
	WidgetBuilder &TextWrap(ui::TextAlign align = ui::TextAlign::Left) {
		StyleMut().SetTextAlign(align);
		return TextOverflowMode(ui::TextOverflow::WRAP);
	}

	/// Défilement automatique aller-retour. `speed` en pixels par seconde,
	/// `pause` en secondes à chaque extrémité.
	WidgetBuilder &TextMarquee(float speed = 40.f, float pause = 1.f) {
		UiTextOverflow &o = OverflowMut();
		o.speed = speed;
		o.pause = pause;
		o.mode = ui::TextOverflow::MARQUEE;
		return *this;
	}

	WidgetBuilder &TextAlign(TextAlign a) {
		StyleMut().SetTextAlign(a);
		return *this;
	}
	/// Bascule vers la police italique enregistrée (cf.
	/// RenderSystem::RegisterItalicFont()) — sans effet si aucune n'est
	/// enregistrée (retombe silencieusement sur la police normale).
	WidgetBuilder &Italic(bool v = true) {
		StyleMut().SetItalic(v);
		return *this;
	}
	/// Bascule vers la police grasse enregistrée (cf.
	/// RenderSystem::RegisterBoldFont()) — même comportement de repli
	/// silencieux que italic().
	WidgetBuilder &Bold(bool v = true) {
		StyleMut().SetBold(v);
		return *this;
	}
	/// Bande soulignée sous le texte (dessinée, pas un effet de police —
	/// fonctionne avec n'importe quelle combinaison bold()/italic()).
	WidgetBuilder &Underline(bool v = true) {
		StyleMut().SetUnderline(v);
		return *this;
	}
	/// Bande barrant le texte (dessinée, cf. underline()).
	WidgetBuilder &Strikethrough(bool v = true) {
		StyleMut().SetStrikethrough(v);
		return *this;
	}
	/// Fond plein derrière le texte façon surligneur — couleur via
	/// `.highlightColor(...)` (défaut jaune translucide si non posée).
	WidgetBuilder &Highlight(bool v = true) {
		StyleMut().SetHighlight(v);
		return *this;
	}
	WidgetBuilder &HighlightColor(sdl3::FColor c) {
		StyleMut().SetHighlightColor(c);
		return *this;
	}
	/// Effet « verre » (thème Aero) — bande de reflet + lueur de bordure,
	/// superposées au fond normal (cf. UiStyle::glass()/RenderSystem).
	WidgetBuilder &Glass(float gloss = 0.35f, float glow = 0.45f) {
		StyleMut().SetGloss(gloss).SetGlow(glow).SetGlowColor(sdl3::FColor::UI_WHITE_STRONG());
		return *this;
	}
	/// Mode d'ajustement d'une image (Fill/Contain/Cover/NONE).
	WidgetBuilder &Fit(ImageFit mode) {
		if (image.IsSome())
			image->fit = mode;
		return *this;
	}
	/// Restreint édition/copie/collage sur un UiInput ou UiInputArea (défaut :
	/// ReadWriteCopyAndPaste — tout autorisé). Voir IOMode.
	WidgetBuilder &IoMode(IOMode mode) {
		if (input.IsSome())
			input->ioMode = mode;
		if (inputArea.IsSome())
			inputArea->ioMode = mode;
		return *this;
	}
	/// Longueur maximale du texte (en octets) accepté par un UiInput/UiInputArea.
	WidgetBuilder &MaxLen(size_t n) {
		if (input.IsSome())
			input->maxLen = n;
		if (inputArea.IsSome())
			inputArea->maxLen = n;
		return *this;
	}

	// ── UiInputArea en éditeur de code ───────────────────────────────────────

	/// Mode « éditeur de code » d'un UiInputArea en un appel : chasse fixe,
	/// numéros de ligne, ligne du curseur surlignée, et coloration si
	/// `highlighter` est fourni. Sans effet sur les autres widgets.
	WidgetBuilder &CodeEditor(UiSyntaxHighlighter highlighter = nullptr) {
		if (inputArea.IsSome()) {
			inputArea->monospace = true;
			inputArea->lineNumbers = true;
			inputArea->highlightCurrentLine = true;
			inputArea->followTail = false;
			inputArea->highlighter = std::move(highlighter);
		}
		return *this;
	}
	/// Gouttière de numéros de ligne (UiInputArea).
	WidgetBuilder &LineNumbers(bool enabled = true) {
		if (inputArea.IsSome())
			inputArea->lineNumbers = enabled;
		return *this;
	}
	/// Police à chasse fixe (UiInputArea, cf. Ui::RegisterMonospaceFont).
	WidgetBuilder &Monospace(bool enabled = true) {
		if (inputArea.IsSome())
			inputArea->monospace = enabled;
		return *this;
	}
	/// Coloration syntaxique ligne par ligne (UiInputArea).
	WidgetBuilder &Highlighter(UiSyntaxHighlighter highlighter) {
		if (inputArea.IsSome())
			inputArea->highlighter = std::move(highlighter);
		return *this;
	}

	// ── Callbacks ────────────────────────────────────────────────────────────

	WidgetBuilder &OnClick(std::function<void()> fn) {
		Callbacks().onClick = std::move(fn);
		return *this;
	}
	WidgetBuilder &OnChange(std::function<void(float)> fn) {
		Callbacks().onChange = std::move(fn);
		return *this;
	}
	WidgetBuilder &OnToggle(std::function<void(bool)> fn) {
		Callbacks().onToggle = std::move(fn);
		return *this;
	}
	WidgetBuilder &onScroll(std::function<void(float)> fn) {
		Callbacks().onScroll = std::move(fn);
		return *this;
	}
	WidgetBuilder &onTextChange(std::function<void(const String &)> fn) {
		Callbacks().onTextChange = std::move(fn);
		return *this;
	}
	/// Notifié à CHAQUE modification du texte d'un UiInput/UiInputArea
	/// (frappe, collage, effacement) — un filtre de recherche « en direct ».
	WidgetBuilder &OnTextChange(std::function<void(const String &)> fn) {
		Callbacks().onTextChange = std::move(fn);
		return *this;
	}
	WidgetBuilder &OnSubmit(std::function<void(const String &)> fn) {
		Callbacks().onSubmit = std::move(fn);
		return *this;
	}

	// ── Enfants (DSL imbriqué) ───────────────────────────────────────────────

	template <typename... Builders> WidgetBuilder &Children(Builders &&...bs) {
		(kids.push_back(std::make_unique<WidgetBuilder>(std::move(bs))), ...);
		return *this;
	}

	// ── Spawn ────────────────────────────────────────────────────────────────

	ecs::Entity Spawn() {
		ecs::Entity e = world->Spawn();
		world->AddComponent(e, rect);
		if (flow.IsSome())
			world->AddComponent(e, *flow);
		if (item.IsSome())
			world->AddComponent(e, *item);
		if (panel.IsSome())
			world->AddComponent(e, *panel);
		if (label.IsSome())
			world->AddComponent(e, std::move(*label));
		if (button.IsSome())
			world->AddComponent(e, std::move(*button));
		if (toggle.IsSome())
			world->AddComponent(e, *toggle);
		if (checkbox.IsSome())
			world->AddComponent(e, *checkbox);
		if (slider.IsSome())
			world->AddComponent(e, *slider);
		if (progress.IsSome())
			world->AddComponent(e, *progress);
		if (separator.IsSome())
			world->AddComponent(e, *separator);
		if (input.IsSome())
			world->AddComponent(e, std::move(*input));
		if (inputArea.IsSome())
			world->AddComponent(e, std::move(*inputArea));
		if (dragValue.IsSome())
			world->AddComponent(e, std::move(*dragValue));
		if (colorSwatch.IsSome())
			world->AddComponent(e, *colorSwatch);
		if (svSquare.IsSome())
			world->AddComponent(e, *svSquare);
		if (hueSlider.IsSome())
			world->AddComponent(e, *hueSlider);
		if (alphaSlider.IsSome())
			world->AddComponent(e, *alphaSlider);
		if (selectable.IsSome())
			world->AddComponent(e, *selectable);
		if (treeNode.IsSome())
			world->AddComponent(e, std::move(*treeNode));
		if (reorderable.IsSome())
			world->AddComponent(e, *reorderable);
		if (selection.IsSome())
			world->AddComponent(e, *selection);
		if (menuBarItem.IsSome())
			world->AddComponent(e, std::move(*menuBarItem));
		if (menuItem.IsSome())
			world->AddComponent(e, std::move(*menuItem));
		if (table.IsSome())
			world->AddComponent(e, std::move(*table));
		if (plot.IsSome())
			world->AddComponent(e, std::move(*plot));
		if (calendar.IsSome())
			world->AddComponent(e, std::move(*calendar));
		if (resizeHandle.IsSome())
			world->AddComponent(e, std::move(*resizeHandle));
		if (textOverflow.IsSome())
			world->AddComponent(e, std::move(*textOverflow));
		if (dragPayload.IsSome())
			world->AddComponent(e, std::move(*dragPayload));
		if (dropTarget.IsSome())
			world->AddComponent(e, std::move(*dropTarget));
		if (image.IsSome())
			world->AddComponent(e, std::move(*image));
		if (viewport3d.IsSome())
			world->AddComponent(e, std::move(*viewport3d));
		if (shaderEffect.IsSome())
			world->AddComponent(e, *shaderEffect);
		if (icon.IsSome())
			world->AddComponent(e, std::move(*icon));
		if (radio.IsSome())
			world->AddComponent(e, std::move(*radio));
		if (scrollbar.IsSome())
			world->AddComponent(e, *scrollbar);
		if (knob.IsSome())
			world->AddComponent(e, *knob);
		if (canvas.IsSome())
			world->AddComponent(e, std::move(*canvas));
		// Visibilité initiale des enfants : expander replié = tous cachés ;
		// tabview = seul l'onglet actif visible (appliqué après le spawn des kids).
		const bool COLLAPSE_KIDS = expander.IsSome() && !expander->expanded;
		const int TAB_ACTIVE = tabview.IsSome() ? tabview->active : -1;
		if (combo.IsSome())
			world->AddComponent(e, std::move(*combo));
		if (listbox.IsSome())
			world->AddComponent(e, std::move(*listbox));
		if (expander.IsSome())
			world->AddComponent(e, std::move(*expander));
		if (tabview.IsSome())
			world->AddComponent(e, std::move(*tabview));
		if (spinner.IsSome())
			world->AddComponent(e, *spinner);
		if (badge.IsSome())
			world->AddComponent(e, std::move(*badge));
		if (tooltip.IsSome())
			world->AddComponent(e, std::move(*tooltip));
		if (popupState.IsSome())
			world->AddComponent(e, *popupState);
		if (overlayLayer.IsSome())
			world->AddComponent(e, *overlayLayer);
		if (callbacks.IsSome())
			world->AddComponent(e, std::move(*callbacks));
		if (name.IsSome())
			world->AddComponent(e, std::move(*name));
		if (hidden)
			world->AddComponent(e, UiHidden{});
		if (disabled)
			world->AddComponent(e, UiDisabled{});
		if (pointerThrough)
			world->AddComponent(e, UiPointerThrough{});
		if (style.IsSome())
			world->AddComponent(e, std::move(*style));
		{
			// "root" (défauts génériques) + "root-<type>" (défauts thémés du
			// widget, cf. UiFactory::registerRootClasses) sont toujours en
			// tête — priorité la plus basse, donc les classes explicites de
			// l'appelant (poussées après, cf. className()) et le style
			// inline ci-dessus l'emportent toujours (cascade CSS standard).
			std::vector<String> names{String("root")};
			if (auto k = KindClass(); k.IsSome())
				names.push_back(k.Unwrap());
			for (auto &n : classNames)
				names.push_back(n);
			world->AddComponent(e, UiClassList{std::move(names)});
		}
		// Toute entité fraîchement spawnée a besoin d'au moins une passe de
		// résolution de cascade (héritage fontSize/textColor/opacity depuis
		// le parent, classes, style inline) — le flag s'auto-nettoie après
		// le premier StyleSystem::Resolve() (cf. styles.hpp), donc une UI au
		// repos ne repaie jamais ce coût.
		world->AddComponent(e, UiStyleDirty{});
		world->AddComponent(e, UiComputed{});

		if (parent.IsSome())
			SetParent(*world, e, *parent);

		for (auto &kid : kids) {
			kid->parent = Some(e);
			kid->Spawn();
		}

		if (COLLAPSE_KIDS || TAB_ACTIVE >= 0) {
			if (auto ch = world->GetComponent<UiChildren>(e); ch.IsSome()) {
				std::vector<ecs::Entity> kids = ch.Unwrap()->list;
				for (size_t i = 0; i < kids.size(); ++i) {
					bool hide = COLLAPSE_KIDS || int(i) != TAB_ACTIVE;
					if (hide && !world->HasComponent<UiHidden>(kids[i]))
						world->AddComponent(kids[i], UiHidden{});
				}
			}
		}

		layout->MarkDirty();
		return e;
	}

	// ── Styles dynamiques ────────────────────────────────────────────────

	/// Pose un style inline (priorité maximale dans la cascade).
	WidgetBuilder &Style(UiStyle s) {
		style = Some(std::move(s));
		return *this;
	}

	/// Ajoute une classe CSS (peut être appelé plusieurs fois).
	WidgetBuilder &ClassName(String name) {
		classNames.push_back(std::move(name));
		return *this;
	}

	/// Pose la liste complète de classes (remplace les appels précédents à className).
	WidgetBuilder &ClassNames(std::vector<String> names) {
		classNames = std::move(names);
		return *this;
	}

	/// Fait de cette entité un conteneur de sélection (cf. UiSelection,
	/// interaction.hpp) : les UiSelectable/UiTreeNode descendants (n'importe
	/// où dans le sous-arbre, pas seulement enfants directs — cf.
	/// nearestSelectionAncestor) appliquent leurs clics contre l'état de
	/// sélection posé ICI. `multiSelect=false` restreint ctrl/shift à un
	/// remplacement pur (comme une ListBox).
	WidgetBuilder &SelectionRoot(bool multiSelect = true) {
		selection = Some(UiSelection{{}, multiSelect});
		return *this;
	}

	/// Rend cette entité déplaçable par glisser vertical parmi ses frères
	/// directs (cf. UiReorderable, interaction.hpp) — réordonne réellement
	/// UiChildren du parent. Combinable avec `selectable()`/`treeNode()`
	/// (Phase 4) ou n'importe quelle ligne d'une liste custom ; le PARENT
	/// reçoit la notification `onReorder(fromIndex, toIndex)` (cf.
	/// UiCallbacks — à poser sur le conteneur via `.onReorder()`, pas sur la
	/// ligne elle-même).
	/// Fait de ce widget une SOURCE de glisser-déposer transportant `id`
	/// (cf. UiDragPayload, interaction.hpp).
	WidgetBuilder &DragPayload(String kind, int64_t id) {
		dragPayload = Some(UiDragPayload{std::move(kind), id, false});
		return *this;
	}

	/// Fait de ce widget une CIBLE de dépôt. `accepts` vide = tout accepter.
	WidgetBuilder &DropTarget(String accepts = String()) {
		dropTarget = Some(UiDropTarget{std::move(accepts), false});
		return *this;
	}

	/// Notifié au dépôt sur CE widget, avec l'identifiant de la source.
	WidgetBuilder &OnDrop(std::function<void(int64_t)> fn) {
		Callbacks().onDrop = std::move(fn);
		return *this;
	}

	/// Notifié au clic DROIT sur ce widget (position écran du pointeur) —
	/// typiquement pour ouvrir un menu contextuel via `Ui::OpenPopupAt`.
	WidgetBuilder &OnContextMenu(std::function<void(float, float)> fn) {
		Callbacks().onContextMenu = std::move(fn);
		return *this;
	}

	WidgetBuilder &Reorderable() {
		reorderable = Some(UiReorderable{});
		return *this;
	}

	WidgetBuilder &OnReorder(std::function<void(int, int)> fn) {
		Callbacks().onReorder = std::move(fn);
		return *this;
	}

	/// Ajoute une série à ce plot (cf. ui/plot.hpp — `.Plot()` doit avoir été
	/// appelé avant, sinon un `Plot` vide est créé au vol). Couleur
	/// résolue depuis la palette catégorielle par défaut (round-robin) si
	/// `color` n'est pas précisé.
	WidgetBuilder &AddSeries(PlotSeriesKind kind, std::span<const float> x, std::span<const float> y,
							 String label = "", Option<sdl3::FColor> color = NONE) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		PlotSeries s;
		s.kind = kind;
		s.SetXy(x, y);
		s.label = std::move(label);
		s.color = color.IsSome() ? color.Unwrap() : PlotPaletteColor(plot->series.size());
		plot->series.push_back(std::move(s));
		return *this;
	}
	/// Ajoute une part à ce camembert (cf. ui/plot.hpp — `.PIE()` doit avoir
	/// été appelé avant, sinon un `Plot` vide en `PlotMode::XY` est créé au
	/// vol, ce qui n'a pas de sens pour une part : préférez toujours
	/// `.PIE().addPieSlice(...)`). Couleur résolue depuis la palette
	/// catégorielle par défaut (round-robin) si `color` n'est pas précisé.
	WidgetBuilder &AddPieSlice(float value, String label = "", Option<sdl3::FColor> color = NONE) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		PieSlice sl;
		sl.value = value;
		sl.label = std::move(label);
		sl.color = color.IsSome() ? color.Unwrap() : PlotPaletteColor(plot->pieSlices.size());
		plot->pieSlices.push_back(std::move(sl));
		return *this;
	}
	/// Pose la grille de valeurs du heatmap (Phase 6, cf. ui/plot.hpp
	/// `HeatmapData`) — `.heatmap()` doit avoir été appelé avant. `values`
	/// (ordre ligne principale) doit avoir au moins `rows*cols` éléments,
	/// sinon `drawHeatmap()` ne dessine rien plutôt que de risquer un accès
	/// hors bornes.
	WidgetBuilder &SetHeatmapData(int rows, int cols, std::span<const float> values,
								  Colormap cm = Colormap::VIRIDIS) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		plot->heatmap.rows = rows;
		plot->heatmap.cols = cols;
		plot->heatmap.values.assign(values.begin(), values.end());
		plot->heatmap.colormap = cm;
		return *this;
	}
	/// Ajoute une bougie à ce chandelier (Phase 6, cf. ui/plot.hpp
	/// `OhlcBar`) — `.Candlestick()` doit avoir été appelé avant.
	WidgetBuilder &AddOhlcBar(float x, float open, float high, float low, float close) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		OhlcBar bar;
		bar.x = x;
		bar.open = open;
		bar.high = high;
		bar.low = low;
		bar.close = close;
		plot->candleBars.push_back(bar);
		return *this;
	}
	/// Pose des barres d'erreur Y (Phase 5, cf. `PlotSeries::yError`) sur la
	/// DERNIÈRE série ajoutée — même pattern que `useSecondaryY()`, à
	/// appeler juste après un `addXSeries(...)`. `err` doit avoir la même
	/// taille que les `y[]` de cette série (sinon les indices en trop sont
	/// simplement ignorés au rendu, cf. `drawPlotSeries`).
	WidgetBuilder &SetYError(std::span<const float> err) {
		if (plot.IsSome() && !plot->series.empty())
			plot->series.back().yError.assign(err.begin(), err.end());
		return *this;
	}
	/// Pose l'échappatoire de rendu personnalisé (Phase 7, cf.
	/// `PlotSeries::onCustomDraw`) sur la DERNIÈRE série ajoutée — même
	/// pattern que `useSecondaryY()`/`setYError()`. Remplace ENTIÈREMENT le
	/// rendu par défaut de cette série (`kind`/couleur/marqueur/etc. ne sont
	/// alors utilisés que par la légende, pas par le tracé).
	WidgetBuilder &SetOnCustomDraw(std::function<void(sdl3::Renderer &, const sdl3::FRect &, const PlotSeries &,
													   const PlotAxis &, const PlotAxis &)>
										fn) {
		if (plot.IsSome() && !plot->series.empty())
			plot->series.back().onCustomDraw = std::move(fn);
		return *this;
	}
	/// Série Line, X implicite (indices 0..N-1).
	WidgetBuilder &AddLineSeries(std::span<const float> y, String label = "", Option<sdl3::FColor> color = NONE) {
		std::vector<float> x(y.size());
		for (size_t i = 0; i < x.size(); ++i)
			x[i] = float(i);
		return AddSeries(PlotSeriesKind::LINE, x, y, std::move(label), color);
	}
	/// Série Line, X explicite.
	WidgetBuilder &AddLineSeriesXy(std::span<const float> x, std::span<const float> y, String label = "",
								   Option<sdl3::FColor> color = NONE) {
		return AddSeries(PlotSeriesKind::LINE, x, y, std::move(label), color);
	}
	/// Série Scatter (nuage de points), X explicite.
	WidgetBuilder &AddScatterSeries(std::span<const float> x, std::span<const float> y, String label = "",
									Option<sdl3::FColor> color = NONE) {
		return AddSeries(PlotSeriesKind::SCATTER, x, y, std::move(label), color);
	}
	/// Série Step (« escalier », valeur maintenue jusqu'au x suivant), X explicite.
	WidgetBuilder &AddStepSeries(std::span<const float> x, std::span<const float> y, String label = "",
								 Option<sdl3::FColor> color = NONE) {
		return AddSeries(PlotSeriesKind::STEP, x, y, std::move(label), color);
	}
	/// Série Area (courbe + remplissage jusqu'à la ligne de base 0), X
	/// explicite — `fillOpacity` dans [0,1].
	WidgetBuilder &AddAreaSeries(std::span<const float> x, std::span<const float> y, String label = "",
								 Option<sdl3::FColor> color = NONE, float fillOpacity = 0.35f) {
		AddSeries(PlotSeriesKind::AREA, x, y, std::move(label), color);
		plot->series.back().fillOpacity = fillOpacity;
		return *this;
	}
	/// Série Stem (« lollipop » : ligne verticale depuis 0 + marqueur), X explicite.
	WidgetBuilder &AddStemSeries(std::span<const float> x, std::span<const float> y, String label = "",
								 Option<sdl3::FColor> color = NONE) {
		return AddSeries(PlotSeriesKind::STEM, x, y, std::move(label), color);
	}
	/// Série Bar (barres verticales depuis la ligne de base 0), X explicite
	/// — `barWidth` en unités DONNÉES (cf. PlotSeries::barWidth).
	WidgetBuilder &AddBarSeries(std::span<const float> x, std::span<const float> y, String label = "",
								Option<sdl3::FColor> color = NONE, float barWidth = 0.67f) {
		AddSeries(PlotSeriesKind::BAR, x, y, std::move(label), color);
		plot->series.back().barWidth = barWidth;
		return *this;
	}
	/// Série BarH (barres horizontales) — `x[i]` = longueur, `y[i]` =
	/// position/catégorie (cf. doc de PlotSeriesKind::BAR_H).
	WidgetBuilder &AddBarHSeries(std::span<const float> x, std::span<const float> y, String label = "",
								 Option<sdl3::FColor> color = NONE, float barWidth = 0.67f) {
		AddSeries(PlotSeriesKind::BAR_H, x, y, std::move(label), color);
		plot->series.back().barWidth = barWidth;
		return *this;
	}
	/// Série Histogram (barres jointives) — `x[]` = centres de classe, `y[]`
	/// = effectifs (pas de binning automatique, cf. doc de PlotSeriesKind).
	WidgetBuilder &AddHistogramSeries(std::span<const float> x, std::span<const float> y, String label = "",
									  Option<sdl3::FColor> color = NONE, float barWidth = 1.f) {
		AddSeries(PlotSeriesKind::HISTOGRAM, x, y, std::move(label), color);
		plot->series.back().barWidth = barWidth;
		return *this;
	}

	/// Style des graduations de l'axe X (cf. ui/plot.hpp `PlotAxis`) —
	/// `position` doit être `Top`/`Bottom` (`Left`/`Right` n'a pas de sens
	/// pour un axe X, non validé). `overlay=true` fait flotter les
	/// graduations par-dessus la zone de tracé (avec `boxColor` en fond) au
	/// lieu de réserver un espace dédié en permanence.
	WidgetBuilder &XTicks(PlotEdge position, bool overlay = false, Option<sdl3::FColor> boxColor = NONE) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		plot->xAxis.tickPosition = position;
		plot->xAxis.tickOverlay = overlay;
		if (boxColor.IsSome())
			plot->xAxis.tickBoxColor = boxColor.Unwrap();
		return *this;
	}
	/// Style des graduations de l'axe Y — `position` doit être `Left`/`Right`.
	WidgetBuilder &YTicks(PlotEdge position, bool overlay = false, Option<sdl3::FColor> boxColor = NONE) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		plot->yAxis.tickPosition = position;
		plot->yAxis.tickOverlay = overlay;
		if (boxColor.IsSome())
			plot->yAxis.tickBoxColor = boxColor.Unwrap();
		return *this;
	}
	/// Position/mode de la légende (cf. ui/plot.hpp `LegendPosition`) —
	/// `overlay=true` (défaut) la fait flotter par-dessus la zone de tracé
	/// (avec `boxColor` en fond), `false` réserve un espace dédié sur le
	/// bord correspondant.
	WidgetBuilder &Legend(LegendPosition position, bool overlay = true, Option<sdl3::FColor> boxColor = NONE) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		plot->legendPosition = position;
		plot->legendOverlay = overlay;
		if (boxColor.IsSome())
			plot->legendBoxColor = boxColor.Unwrap();
		return *this;
	}
	/// Affiche/masque entièrement la légende (visible par défaut dès qu'une
	/// série porte un `label`).
	WidgetBuilder &ShowLegend(bool show) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		plot->showLegend = show;
		return *this;
	}
	/// Échelle logarithmique (base 10, Phase 3) par axe — cf. doc
	/// `PlotAxis::logScale` (données strictement positives requises).
	WidgetBuilder &LogScaleX(bool enable = true) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		plot->xAxis.logScale = enable;
		return *this;
	}
	WidgetBuilder &LogScaleY(bool enable = true) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		plot->yAxis.logScale = enable;
		return *this;
	}
	/// Échelle logarithmique de l'axe Y SECONDAIRE (Phase 4, cf. `yAxis2`).
	WidgetBuilder &LogScaleY2(bool enable = true) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		plot->yAxis2.logScale = enable;
		return *this;
	}
	/// Bascule la DERNIÈRE série ajoutée (via `addLineSeries()`/etc.) sur
	/// l'axe Y secondaire `yAxis2` (Phase 4) — à appeler juste après l'ajout
	/// de la série concernée. `yAxis2` n'est dessiné/résolu que si au moins
	/// une série l'utilise (cf. `plotHasSecondaryY()`), pas de builder séparé
	/// pour "activer" l'axe lui-même.
	WidgetBuilder &UseSecondaryY(bool enable = true) {
		if (plot.IsSome() && !plot->series.empty())
			plot->series.back().useSecondaryY = enable;
		return *this;
	}
	/// Style des graduations de l'axe Y SECONDAIRE (Phase 4) — mêmes règles
	/// que `yGetTicks()`, `position` doit être `Left`/`Right`. Sans effet visible
	/// si aucune série n'a `useSecondaryY=true`.
	WidgetBuilder &YTicks2(PlotEdge position, bool overlay = false, Option<sdl3::FColor> boxColor = NONE) {
		if (plot.IsNone())
			plot = Some(UiPlot{});
		plot->yAxis2.tickPosition = position;
		plot->yAxis2.tickOverlay = overlay;
		if (boxColor.IsSome())
			plot->yAxis2.tickBoxColor = boxColor.Unwrap();
		return *this;
	}

	/// Ordre de tri du pass overlay Fixed (cf. UiOverlayLayer, systems.hpp) —
	/// utile pour empiler un sous-menu au-dessus de son menu parent, ou une
	/// modale au-dessus d'un popup déjà ouvert. Défaut 0 si jamais posé.
	WidgetBuilder &OverlayOrder(int order) {
		overlayLayer = Some(UiOverlayLayer{order});
		return *this;
	}

private:
	friend class UiFactory;

	ecs::ArchetypeRegistry *world;
	LayoutSystem *layout;

	UiRect rect{};
	Option<UiFlow> flow = NONE;
	Option<UiItem> item = NONE;
	Option<UiPanel> panel = NONE;
	Option<UiLabel> label = NONE;
	Option<UiButton> button = NONE;
	Option<UiToggle> toggle = NONE;
	Option<UiCheckbox> checkbox = NONE;
	Option<UiSlider> slider = NONE;
	Option<UiProgress> progress = NONE;
	Option<UiSeparator> separator = NONE;
	Option<UiInput> input = NONE;
	Option<UiInputArea> inputArea = NONE;
	Option<UiDragValue> dragValue = NONE;
	Option<UiColorSwatch> colorSwatch = NONE;
	Option<UiSVSquare> svSquare = NONE;
	Option<UiHueSlider> hueSlider = NONE;
	Option<UiAlphaSlider> alphaSlider = NONE;
	Option<UiSelectable> selectable = NONE;
	Option<UiTreeNode> treeNode = NONE;
	Option<UiReorderable> reorderable = NONE;
	Option<UiTextOverflow> textOverflow = NONE;
	Option<UiDragPayload> dragPayload = NONE;
	Option<UiDropTarget> dropTarget = NONE;
	Option<UiSelection> selection = NONE;
	Option<UiMenuBarItem> menuBarItem = NONE;
	Option<UiMenuItem> menuItem = NONE;
	Option<UiTable> table = NONE;
	Option<UiPlot> plot = NONE;
	Option<UiCalendar> calendar = NONE;
	Option<UiResizeHandle> resizeHandle = NONE;
	Option<UiImage> image = NONE;
	Option<UiViewport3D> viewport3d = NONE;
	Option<UiShaderEffect> shaderEffect = NONE;
	Option<UiIcon> icon = NONE;
	Option<UiRadio> radio = NONE;
	Option<UiScrollBar> scrollbar = NONE;
	Option<UiKnob> knob = NONE;
	Option<UiCanvas> canvas = NONE;
	Option<UiComboBox> combo = NONE;
	Option<ListBox> listbox = NONE;
	Option<UiExpander> expander = NONE;
	Option<UiTabView> tabview = NONE;
	Option<UiSpinner> spinner = NONE;
	Option<UiBadge> badge = NONE;
	Option<UiTooltip> tooltip = NONE;
	Option<UiPopupState> popupState = NONE;
	Option<UiOverlayLayer> overlayLayer = NONE;
	Option<UiCallbacks> callbacks = NONE;
	Option<UiName> name = NONE;
	Option<ecs::Entity> parent = NONE;
	Option<UiStyle> style = NONE;
	std::vector<String> classNames;
	bool hidden = false;
	bool disabled = false;
	bool pointerThrough = false;
	std::vector<std::unique_ptr<WidgetBuilder>> kids;

	// NB : Option<T>::Unwrap() renvoie une COPIE — pour muter en place il faut
	// passer par operator*/operator-> qui renvoient bien une référence.
	UiFlow &Flow() {
		if (flow.IsNone())
			flow = Some(UiFlow{});
		return *flow;
	}
	UiItem &Item() {
		if (item.IsNone())
			item = Some(UiItem{});
		return *item;
	}
	UiPanel &Panel() {
		if (panel.IsNone())
			panel = Some(UiPanel{});
		return *panel;
	}
	UiCallbacks &Callbacks() {
		if (callbacks.IsNone())
			callbacks = Some(UiCallbacks{});
		return *callbacks;
	}
	UiStyle &StyleMut() {
		if (style.IsNone())
			style = Some(UiStyle{});
		return *style;
	}

	UiTextOverflow &OverflowMut() {
		if (textOverflow.IsNone())
			textOverflow = Some(UiTextOverflow{});
		return *textOverflow;
	}

	/// Nom de la classe de défauts thémés correspondant au type de widget
	/// déjà posé sur ce builder (ex: "root-button"), ou NONE si ce type n'a
	/// pas de défauts spécifiques (image/canvas/tooltip...). Utilisé par
	/// Spawn() pour préfixer la UiClassList — cf. UiFactory::registerRootClasses.
	[[nodiscard]] Option<String> KindClass() const {
		if (panel.IsSome())
			return Some(String("root-panel"));
		if (label.IsSome())
			return Some(String("root-label"));
		if (button.IsSome())
			return Some(String("root-button"));
		if (toggle.IsSome())
			return Some(String("root-toggle"));
		if (checkbox.IsSome())
			return Some(String("root-checkbox"));
		if (slider.IsSome())
			return Some(String("root-slider"));
		if (progress.IsSome())
			return Some(String("root-progress"));
		if (separator.IsSome())
			return Some(String("root-separator"));
		if (input.IsSome())
			return Some(String("root-input"));
		if (inputArea.IsSome())
			return Some(String("root-input"));
		if (dragValue.IsSome())
			return Some(String("root-dragvalue"));
		if (colorSwatch.IsSome())
			return Some(String("root-colorswatch"));
		if (selectable.IsSome())
			return Some(String("root-selectable"));
		if (treeNode.IsSome())
			return Some(String("root-treenode"));
		if (menuBarItem.IsSome())
			return Some(String("root-menubaritem"));
		if (menuItem.IsSome())
			return Some(String("root-menuitem"));
		if (table.IsSome())
			return Some(String("root-table"));
		if (plot.IsSome())
			return Some(String("root-plot"));
		if (calendar.IsSome())
			return Some(String("root-calendar"));
		if (resizeHandle.IsSome())
			return Some(String("root-splitterhandle"));
		if (icon.IsSome())
			return Some(String("root-icon"));
		if (radio.IsSome())
			return Some(String("root-radio"));
		if (scrollbar.IsSome())
			return Some(String("root-scrollbar"));
		if (knob.IsSome())
			return Some(String("root-knob"));
		if (combo.IsSome())
			return Some(String("root-combo"));
		if (listbox.IsSome())
			return Some(String("root-listbox"));
		if (expander.IsSome())
			return Some(String("root-expander"));
		if (tabview.IsSome())
			return Some(String("root-tabview"));
		if (spinner.IsSome())
			return Some(String("root-spinner"));
		if (badge.IsSome())
			return Some(String("root-badge"));
		return NONE;
	}
};

// ============================================================================
// UiFactory
// ============================================================================

class UiFactory {
public:
	UiTheme theme;

	/// Classes CSS-like partagées par tous les widgets de cette factory
	/// (`sheet.define("danger", UiStyle{}.SetBg(...))`, puis `.className
	/// ("danger")` sur un WidgetBuilder). Résolu par le StyleSystem de la
	/// façade `Ui` (cf. ui.hpp) — voir styles.hpp pour la cascade complète.
	UiStyleSheet sheet;

	UiFactory(ecs::ArchetypeRegistry &world, LayoutSystem &layout, UiTheme t = UiTheme::Dark())
		: theme(std::move(t)), world(&world), layout(&layout) {
		// Base de DimUnit::Rem et police héritée par défaut = police du thème.
		this->layout->rootFontSize = theme.fontSize;
		RegisterRootClasses();
	}

	[[nodiscard]] ecs::ArchetypeRegistry &World() noexcept { return *world; }
	[[nodiscard]] LayoutSystem &Layout() noexcept { return *layout; }

	/// Remplace le thème À CHAUD : réenregistre les classes « root-* » avec
	/// la nouvelle palette et force la cascade à se re-résoudre sur tous les
	/// widgets déjà construits.
	///
	/// Sans cette méthode, le thème n'était modifiable qu'à la construction
	/// de `UiFactory` — écrire dans `theme` après coup ne touchait que les
	/// widgets créés ENSUITE, laissant l'interface à moitié repeinte.
	/// Manque relevé en implémentant le sélecteur de thème de l'éditeur de
	/// niveau, où l'utilisateur bascule dark/light/aero à tout moment.
	void SetTheme(UiTheme newTheme) {
		theme = std::move(newTheme);
		layout->rootFontSize = theme.fontSize;
		RegisterRootClasses();
		sheet.DirtyAllUsers(*world);
		layout->MarkDirty();
	}

	// ── Conteneurs ───────────────────────────────────────────────────────────

	[[nodiscard]] WidgetBuilder Column() {
		WidgetBuilder b(*world, *layout);
		b.Column();
		return b;
	}
	[[nodiscard]] WidgetBuilder Row() {
		WidgetBuilder b(*world, *layout);
		b.Row();
		return b;
	}

	/// Colonne avec fond de panneau thémé (carte) — cf. classe "root-panel"
	/// pour les défauts (fond/bordure/radius) ; en thème verre (Aero), un
	/// style glassPanel inline remplace le fond plat.
	[[nodiscard]] WidgetBuilder Panel() {
		auto b = Column();
		b.panel = Some(UiPanel{});
		if (theme.glassDefault)
			b.Style(UiStyle::GlassPanel(theme.panelBg));
		return b;
	}

	// ── Widgets ──────────────────────────────────────────────────────────────
	// Toutes les couleurs/tailles de police par défaut viennent désormais des
	// classes "root"/"root-<type>" (cf. registerRootClasses()) résolues par
	// le style, pas de champs sur les composants — cf. components.hpp.

	[[nodiscard]] WidgetBuilder Label(String text) {
		WidgetBuilder b(*world, *layout);
		b.label = Some(UiLabel{std::move(text)});
		return b;
	}

	[[nodiscard]] WidgetBuilder Button(String text) {
		WidgetBuilder b(*world, *layout);
		UiButton btn;
		btn.text = std::move(text);
		b.button = Some(std::move(btn));
		if (theme.glassDefault)
			b.Style(UiStyle::GlassButton(theme.fill));
		return b;
	}

	[[nodiscard]] WidgetBuilder Toggle(bool checked = false) {
		WidgetBuilder b(*world, *layout);
		UiToggle t;
		t.checked = checked;
		t.animT = checked ? 1.f : 0.f;
		b.toggle = Some(t);
		return b;
	}

	[[nodiscard]] WidgetBuilder Checkbox(bool checked = false) {
		WidgetBuilder b(*world, *layout);
		UiCheckbox c;
		c.checked = checked;
		b.checkbox = Some(c);
		return b;
	}

	[[nodiscard]] WidgetBuilder Slider(float min, float max, float value, float step = 0.f,
									   Orientation orient = Orientation::Horizontal) {
		WidgetBuilder b(*world, *layout);
		UiSlider s;
		s.min = min;
		s.max = max;
		s.value = value;
		s.step = step;
		s.orient = orient;
		b.slider = Some(s);
		return b;
	}

	[[nodiscard]] WidgetBuilder Progress(float min, float max, float value) {
		WidgetBuilder b(*world, *layout);
		UiProgress p;
		p.min = min;
		p.max = max;
		p.value = value;
		b.progress = Some(p);
		return b;
	}

	[[nodiscard]] WidgetBuilder Separator(Orientation orient = Orientation::Horizontal) {
		WidgetBuilder b(*world, *layout);
		UiSeparator s;
		s.orient = orient;
		b.separator = Some(s);
		return b;
	}

	/// Champ de saisie mono-ligne (word-wrap + défilement vertical
	/// automatique si le texte enveloppé dépasse la hauteur du widget —
	/// cf. UiInput). `.ioMode()` pour restreindre édition/copie/collage.
	[[nodiscard]] WidgetBuilder Input(String placeholder = "") {
		WidgetBuilder b(*world, *layout);
		UiInput f;
		f.placeholder = std::move(placeholder);
		b.input = Some(std::move(f));
		b.Scrollable();
		return b;
	}

	/// Éditeur/visionneuse multi-ligne, sans word-wrap, défilement vertical
	/// ET horizontal automatique (cf. UiInputArea). Usage typique : `.ioMode
	/// (IOMode::ReadAndCopyOnly)` pour une visionneuse de logs copiable.
	[[nodiscard]] WidgetBuilder InputArea(String placeholder = "") {
		WidgetBuilder b(*world, *layout);
		UiInputArea f;
		f.placeholder = std::move(placeholder);
		b.inputArea = Some(std::move(f));
		b.Scrollable();
		return b;
	}

	/// Valeur numérique façon ImGui::DragFloat — glisser change `value`
	/// (`speed` unités/pixel), DOUBLE-clic bascule en saisie texte directe
	/// (cf. UiDragValue). `step` : quantification (0 = continu).
	[[nodiscard]] WidgetBuilder DragValue(float min, float max, float value, float step = 0.f, float speed = 1.f,
										  int decimals = 2) {
		WidgetBuilder b(*world, *layout);
		UiDragValue d;
		d.min = min;
		d.max = max;
		d.value = sdl3::Clamp(value, min, max);
		d.step = step;
		d.speed = speed;
		d.decimals = decimals;
		b.dragValue = Some(d);
		return b;
	}

	/// Champ numérique façon `<input type=number>` : boutons +/- de part et
	/// d'autre d'un UiInput (COMPOSITE — pas de nouveau composant leaf, cf.
	/// plan Phase 3). `onChange` est notifié par les boutons ET par la saisie
	/// directe (Entrée dans le champ) ; jamais pendant la frappe elle-même.
	[[nodiscard]] WidgetBuilder InputNumber(float min, float max, float value, float step = 1.f, int decimals = 2,
											std::function<void(float)> onChange = nullptr) {
		static uint32_t sCounter = 0;
		String fieldName = String::Format("__ui_inputNumber_%u", ++sCounter);
		ecs::ArchetypeRegistry *world = this->world;

		auto applyDelta = [world, fieldName, min, max, decimals, onChange](float delta) {
			auto e = FindByName(*world, fieldName);
			if (e.IsNone())
				return;
			auto in = world->GetComponent<UiInput>(e.Unwrap());
			if (in.IsNone())
				return;
			float v = sdl3::Clamp(in.Unwrap()->text.ToFloat() + delta, min, max);
			in.Unwrap()->text = String::From(v, decimals);
			in.Unwrap()->cursor = in.Unwrap()->selectionAnchor = in.Unwrap()->text.size();
			if (onChange)
				onChange(v);
		};

		float side = theme.fontSize + 14.f;
		WidgetBuilder minusBtn = Button("-");
		minusBtn.Size(side, side).OnClick([applyDelta, step] { applyDelta(-step); });
		WidgetBuilder plusBtn = Button("+");
		plusBtn.Size(side, side).OnClick([applyDelta, step] { applyDelta(step); });

		WidgetBuilder field = Input();
		field.Name(fieldName).GrowW();
		field.input->text = String::From(sdl3::Clamp(value, min, max), decimals);
		if (onChange) {
			field.OnSubmit([world, fieldName, min, max, decimals, onChange](const String &text) {
				float v = sdl3::Clamp(text.ToFloat(), min, max);
				if (auto e = FindByName(*world, fieldName); e.IsSome())
					if (auto in = world->GetComponent<UiInput>(e.Unwrap()); in.IsSome()) {
						in.Unwrap()->text = String::From(v, decimals);
						in.Unwrap()->cursor = in.Unwrap()->selectionAnchor = in.Unwrap()->text.size();
					}
				onChange(v);
			});
		}

		auto b = Row();
		b.Gap(4.f).WAuto().HAuto().Align(CrossAlign::Center);
		b.Children(std::move(minusBtn), std::move(field), std::move(plusBtn));
		return b;
	}

	/// Pastille de couleur cliquable (cf. UiColorSwatch) — typiquement
	/// déclencheur d'un `colorPicker()` voisin via `.OnClick([&]{
	/// ui::OpenPopup(world, layout, pickerEntity, swatchEntity); })`.
	[[nodiscard]] WidgetBuilder ColorSwatch(sdl3::FColor initial = sdl3::FColor::WHITE()) {
		WidgetBuilder b(*world, *layout);
		b.colorSwatch = Some(UiColorSwatch{initial});
		return b;
	}

	/// Carré saturation/valeur brut (cf. UiSVSquare) — brique interne de
	/// `colorPicker()`, exposée pour composer un sélecteur sur mesure.
	[[nodiscard]] WidgetBuilder SvSquare(float h = 0.f, float s = 1.f, float v = 1.f) {
		WidgetBuilder b(*world, *layout);
		b.svSquare = Some(UiSVSquare{h, s, v});
		return b;
	}

	/// Glissière de teinte brute [0,360) (cf. UiHueSlider) — brique interne
	/// de `colorPicker()`.
	[[nodiscard]] WidgetBuilder HueSlider(float hue = 0.f) {
		WidgetBuilder b(*world, *layout);
		b.hueSlider = Some(UiHueSlider{hue});
		return b;
	}

	/// Glissière alpha brute [0,1] (cf. UiAlphaSlider) — brique interne de
	/// `colorPicker()`.
	[[nodiscard]] WidgetBuilder AlphaSlider(float alpha = 1.f, sdl3::FColor baseColor = sdl3::FColor::WHITE()) {
		WidgetBuilder b(*world, *layout);
		b.alphaSlider = Some(UiAlphaSlider{alpha, baseColor});
		return b;
	}

	/// Sélecteur de couleur complet, en popup (cf. UiFactory::Popup(), Phase
	/// 1) : carré SV + glissière de teinte + glissière alpha + champ hex +
	/// aperçu, tous synchronisés entre eux. À ouvrir/fermer via
	/// `ui::OpenPopup`/`closePopup` (typiquement déclenché par un
	/// `colorSwatch()` voisin). `onChange` reçoit la sdl3::FColor complète (RGBA) à
	/// chaque ajustement — jamais pendant la frappe dans le champ hex.
	[[nodiscard]] WidgetBuilder ColorPicker(sdl3::FColor initial, std::function<void(sdl3::FColor)> onChange = nullptr) {
		static uint32_t sCounter = 0;
		String prefix = String::Format("__ui_colorPicker_%u_", ++sCounter);
		String svName = prefix + "sv", hueName = prefix + "hue", alphaName = prefix + "alpha",
			   hexName = prefix + "hex", swatchName = prefix + "swatch";

		HSV hsv = ColorToHsv(initial);
		ecs::ArchetypeRegistry *world = this->world;

		// Relit l'état des 3 sous-widgets, recompose la sdl3::FColor pleine (le
		// slider de teinte est la source de vérité pour `h` — le carré SV
		// n'en garde qu'une COPIE pour teinter son dégradé), resynchronise le
		// champ hex + l'aperçu, puis notifie l'appelant. Appelé après CHAQUE
		// ajustement (cf. les callbacks "ping" de UiSVSquare/UiHueSlider/
		// UiAlphaSlider dans systems.hpp dispatch()) — pas de système dédié :
		// la resynchronisation reste ponctuelle, pilotée par l'évènement.
		auto resync = [world, svName, hueName, alphaName, hexName, swatchName, onChange] {
			float h = 0.f, s = 1.f, v = 1.f, a = 1.f;
			if (auto e = FindByName(*world, svName); e.IsSome())
				if (auto c = world->GetComponent<UiSVSquare>(e.Unwrap()); c.IsSome()) {
					h = c.Unwrap()->h;
					s = c.Unwrap()->s;
					v = c.Unwrap()->v;
				}
			if (auto e = FindByName(*world, hueName); e.IsSome())
				if (auto c = world->GetComponent<UiHueSlider>(e.Unwrap()); c.IsSome())
					h = c.Unwrap()->hue;
			if (auto e = FindByName(*world, alphaName); e.IsSome())
				if (auto c = world->GetComponent<UiAlphaSlider>(e.Unwrap()); c.IsSome())
					a = c.Unwrap()->alpha;
			sdl3::FColor rgb = HsvToColor(h, s, v, a);
			if (auto e = FindByName(*world, svName); e.IsSome())
				if (auto c = world->GetComponent<UiSVSquare>(e.Unwrap()); c.IsSome())
					c.Unwrap()->h = h;
			if (auto e = FindByName(*world, alphaName); e.IsSome())
				if (auto c = world->GetComponent<UiAlphaSlider>(e.Unwrap()); c.IsSome())
					c.Unwrap()->baseColor = sdl3::FColor{rgb.r, rgb.g, rgb.b, 1.f};
			if (auto e = FindByName(*world, hexName); e.IsSome())
				if (auto c = world->GetComponent<UiInput>(e.Unwrap()); c.IsSome())
					c.Unwrap()->text = ColorToHex(rgb);
			if (auto e = FindByName(*world, swatchName); e.IsSome())
				if (auto c = world->GetComponent<UiColorSwatch>(e.Unwrap()); c.IsSome())
					c.Unwrap()->color = rgb;
			if (onChange)
				onChange(rgb);
		};

		WidgetBuilder sv = SvSquare(hsv.h, hsv.s, hsv.v);
		sv.Name(svName).Size(160.f, 160.f).OnChange([resync](float) { resync(); });
		WidgetBuilder hue = HueSlider(hsv.h);
		hue.Name(hueName).Size(160.f, 16.f).OnChange([resync](float) { resync(); });
		// `FColor` est déjà en [0, 1] : l'ancienne division par 255 (vestige
		// de sdl3::Color) ouvrait le sélecteur sur un alpha quasi nul, et la
		// PREMIÈRE retouche rendait la couleur transparente.
		WidgetBuilder alpha = AlphaSlider(initial.a, sdl3::FColor{initial.r, initial.g, initial.b, 1.f});
		alpha.Name(alphaName).Size(160.f, 16.f).OnChange([resync](float) { resync(); });
		WidgetBuilder preview = ColorSwatch(initial);
		preview.Name(swatchName).Size(28.f, 28.f);

		WidgetBuilder hex = Input();
		hex.Name(hexName).GrowW();
		hex.input->text = ColorToHex(initial);
		hex.OnSubmit([world, svName, hueName, alphaName, resync](const String &text) {
			sdl3::FColor c = HexToColor(text);
			HSV parsed = ColorToHsv(c);
			if (auto e = FindByName(*world, svName); e.IsSome())
				if (auto comp = world->GetComponent<UiSVSquare>(e.Unwrap()); comp.IsSome()) {
					comp.Unwrap()->h = parsed.h;
					comp.Unwrap()->s = parsed.s;
					comp.Unwrap()->v = parsed.v;
				}
			if (auto e = FindByName(*world, hueName); e.IsSome())
				if (auto comp = world->GetComponent<UiHueSlider>(e.Unwrap()); comp.IsSome())
					comp.Unwrap()->hue = parsed.h;
			if (auto e = FindByName(*world, alphaName); e.IsSome())
				if (auto comp = world->GetComponent<UiAlphaSlider>(e.Unwrap()); comp.IsSome())
					comp.Unwrap()->alpha = c.a; // FColor : déjà en [0, 1]
			resync();
		});

		auto previewRow = Row();
		previewRow.Gap(8.f).Align(CrossAlign::Center).Children(std::move(preview), std::move(hex));

		auto p = Popup();
		p.Column().Gap(8.f).Pad(10.f).WAuto().HAuto();
		p.Children(std::move(sv), std::move(hue), std::move(alpha), std::move(previewRow));
		return p;
	}

	/// Ligne sélectionnable façon ImGui::Selectable (cf. UiSelectable) —
	/// surbrillance persistante pilotée par le UiSelection du plus proche
	/// ancêtre (posez `.SelectionRoot()` sur le conteneur racine de la
	/// liste). `index` doit être unique dans ce conteneur (position
	/// logique — PAS forcément l'ordre d'ajout si des lignes sont filtrées).
	/// Combinez avec `.reorderable()` pour le glisser-déposer (Phase 4).
	[[nodiscard]] WidgetBuilder Selectable(String text, int index) {
		WidgetBuilder b(*world, *layout);
		UiSelectable sel;
		sel.index = index;
		b.selectable = Some(sel);
		b.Row().Gap(6.f).Align(CrossAlign::Center).Pad(math::Sides{8.f, 4.f, 8.f, 4.f});
		b.WAuto().HAuto();
		b.Children(Label(std::move(text)));
		return b;
	}

	/// Nœud d'arbre repliable ET sélectionnable (cf. UiTreeNode) — l'en-tête
	/// replie/déplie les enfants ajoutés via `.Children(...)` (typiquement
	/// d'autres `treeNode()`/`selectable()`, formant l'arbre récursivement) ;
	/// un clic ailleurs sur l'en-tête sélectionne, comme `selectable()`
	/// (même UiSelection ancêtre requis). `depth` est purement informatif —
	/// l'indentation vient du padding cumulé de chaque niveau imbriqué, pas
	/// d'un calcul dépendant de `depth`.
	[[nodiscard]] WidgetBuilder TreeNode(String title, int index, int depth = 0, bool expanded = true) {
		WidgetBuilder b(*world, *layout);
		UiTreeNode t;
		t.title = std::move(title);
		t.index = index;
		t.depth = depth;
		t.expanded = expanded;
		t.headerHeight = theme.fontSize + 12.f;
		b.Column().Gap(2.f);
		b.Pad(math::Sides{24.f, t.headerHeight + 2.f, 8.f, 4.f});
		b.WAuto().HAuto(); // recalcul requis au repli/dépli, cf. expander()
		b.treeNode = Some(std::move(t));
		return b;
	}

	/// Barre de menu horizontale (ex: menu principal d'une fenêtre) —
	/// conteneur simple, les entrées s'ajoutent via `menu()` APRÈS l'avoir
	/// spawnée (cf. menu() : chaque entrée a besoin de l'ecs::Entity de la barre
	/// déjà existante pour s'y attacher, comme son propre popup déroulant).
	[[nodiscard]] WidgetBuilder MenuBar() {
		auto b = Row();
		b.Gap(2.f).Pad(math::Sides{4.f, 2.f}).WAuto().HAuto();
		return b;
	}

	/// Entrée de barre de menu SIMPLE, sans son propre menu déroulant — texte
	/// cliquable façon UiButton (peu utilisé directement ; préférez
	/// `menu()` pour une vraie entrée de barre de menu AVEC déroulant).
	[[nodiscard]] WidgetBuilder MenuItem(String text, String shortcut = "") {
		WidgetBuilder b(*world, *layout);
		UiMenuItem mi;
		mi.text = std::move(text);
		mi.shortcut = std::move(shortcut);
		b.menuItem = Some(std::move(mi));
		b.WAuto().HAuto();
		return b;
	}

	/// Entrée de barre de menu + son menu déroulant, en un seul appel : spawn
	/// l'item comme enfant de `bar` (déjà spawnée, cf. menuBar()) et son
	/// popup comme racine INDÉPENDANTE (nécessaire pour que son ancre/offset
	/// se résolvent en coordonnées fenêtre absolues via positionPopupBelow,
	/// cf. systems.hpp — un popup Fixed enfant se positionnerait relativement
	/// à la boîte de contenu de SON parent, pas de la fenêtre). `items...`
	/// sont typiquement des `menuItem(...)` — pour une entrée AVEC sous-menu,
	/// utilisez `subMenu()` séparément avec le popup RETOURNÉ ici comme
	/// `parentPopup` (les entrées avec sous-menu atterrissent alors APRÈS
	/// celles passées ici, cf. subMenu()). Retourne l'ecs::Entity du POPUP (pas de
	/// l'item) — utile pour `subMenu()` ou une fermeture programmatique via
	/// `ui::closePopup`.
	template <typename... Builders> ecs::Entity Menu(ecs::Entity bar, String text, Builders &&...items) {
		WidgetBuilder itemBuilder(*world, *layout);
		itemBuilder.menuBarItem = Some(UiMenuBarItem{std::move(text), ecs::Entity{}, false});
		itemBuilder.WAuto().HAuto();
		itemBuilder.Parent(bar);
		ecs::Entity itemEntity = itemBuilder.Spawn();

		WidgetBuilder popupBuilder = MenuPopup(10);
		popupBuilder.Children(std::forward<Builders>(items)...);
		ecs::Entity popupEntity = popupBuilder.Spawn();

		if (auto mb = world->GetComponent<UiMenuBarItem>(itemEntity); mb.IsSome())
			mb.Unwrap()->menuPopup = popupEntity;
		return popupEntity;
	}

	/// Menu contextuel : un popup de menu SANS entrée de barre, à ouvrir au
	/// clic droit via `ui::OpenPopupAt` (cf. WidgetBuilder::OnContextMenu).
	/// Mêmes items que `Menu()` — `MenuItem(...)` pour une action, puis
	/// `SubMenu(popupRetourné, ...)` pour une entrée à sous-menu (qui se
	/// déploie à droite, et se referme avec toute la chaîne au clic d'une
	/// feuille). Retourne l'entité du POPUP, racine indépendante comme celle
	/// de `Menu()` : c'est ce qui lui permet d'apparaître par-dessus tout.
	template <typename... Builders> ecs::Entity ContextMenu(Builders &&...items) {
		WidgetBuilder popupBuilder = MenuPopup(10);
		popupBuilder.Children(std::forward<Builders>(items)...);
		return popupBuilder.Spawn();
	}

	/// Entrée de menu AVEC sous-menu : comme `menu()`, mais l'item déclencheur
	/// est un `UiMenuItem` (pas `UiMenuBarItem`) parenté sous `parentPopup`
	/// (l'ecs::Entity retournée par `menu()`/`subMenu()` du niveau parent — PAS
	/// l'entité d'un item). Le sous-menu s'ouvre au clic, positionné à
	/// DROITE de l'item (cf. positionPopupRightOf). Retourne l'ecs::Entity du
	/// popup du SOUS-menu (pour un niveau supplémentaire si besoin — le plan
	/// n'en requiert qu'un, mais le mécanisme est général).
	template <typename... Builders> ecs::Entity SubMenu(ecs::Entity parentPopup, String text, Builders &&...items) {
		WidgetBuilder itemBuilder(*world, *layout);
		UiMenuItem mi;
		mi.text = std::move(text);
		mi.hasSubmenu = true;
		itemBuilder.menuItem = Some(std::move(mi));
		itemBuilder.WAuto().HAuto();
		itemBuilder.Parent(parentPopup);
		ecs::Entity itemEntity = itemBuilder.Spawn();

		WidgetBuilder popupBuilder = MenuPopup(11);
		popupBuilder.Children(std::forward<Builders>(items)...);
		ecs::Entity popupEntity = popupBuilder.Spawn();

		if (auto mi2 = world->GetComponent<UiMenuItem>(itemEntity); mi2.IsSome())
			mi2.Unwrap()->submenuPopup = popupEntity;
		return popupEntity;
	}

	/// Table façon ImGui::Table (cf. UiTable, components.hpp) : `columns`
	/// définit l'en-tête (titre + largeur initiale + triable ou non),
	/// `rows[i]` doit avoir autant d'entrées que `columns` (cellules
	/// manquantes tolérées, affichées vides). Colonnes redimensionnables par
	/// glisser sur leur bordure droite, triables par clic d'en-tête (cycle
	/// Aucun→Croissant→Décroissant), multi-sélection de lignes (clic/Ctrl+
	/// clic/Maj+clic — cf. UiSelection, posée automatiquement) ;
	/// `reorderable=true` permet en plus de glisser une ligne pour la
	/// déplacer (notifie `.onReorder(fromIndex,toIndex)`). Le tri notifie
	/// `.OnChange(columnIndexAsFloat)` et VIDE la sélection courante (les
	/// index ne correspondraient plus aux bonnes lignes après un tri, cf.
	/// le composant pour la limitation).
	[[nodiscard]] WidgetBuilder Table(std::vector<UiTableColumn> columns, std::vector<std::vector<String>> rows,
									  bool multiSelect = true, bool reorderable = false) {
		WidgetBuilder b(*world, *layout);
		UiTable t;
		t.columns = std::move(columns);
		t.rows = std::move(rows);
		t.rowHeight = theme.fontSize + 12.f;
		t.headerHeight = theme.fontSize + 16.f;
		t.reorderable = reorderable;
		b.table = Some(std::move(t));
		b.selection = Some(UiSelection{{}, multiSelect});
		b.Size(400.f, 240.f);
		return b;
	}

	/// Plot vide (cf. ui/plot.hpp) — ajoutez des séries via
	/// `.AddLineSeries()`/`.AddScatterSeries()` (WidgetBuilder) puis
	/// `.Spawn()`. `title` est un texte optionnel superposé en haut, centré.
	[[nodiscard]] WidgetBuilder Plot(String title = "") {
		WidgetBuilder b(*world, *layout);
		UiPlot p;
		p.title = std::move(title);
		p.yAxis.tickPosition = PlotEdge::Left;  // PlotAxis::tickPosition défaut Bottom, valide pour X mais pas Y
		p.yAxis2.tickPosition = PlotEdge::Right; // bord opposé à yAxis par défaut (Phase 4)
		b.plot = Some(std::move(p));
		b.Size(300.f, 200.f);
		return b;
	}

	/// Raccourci pour un plot à une seule série Line, X implicite (indices
	/// 0..N-1) — même forme d'appel que l'ancien `plotLines()`, reconstruit
	/// sur le nouveau système (`.Plot().AddLineSeries(...)`, cf. ci-dessous).
	[[nodiscard]] WidgetBuilder PlotLines(std::span<const float> values, String title = "",
										  Option<sdl3::FColor> color = NONE) {
		WidgetBuilder b = Plot(std::move(title));
		b.AddLineSeries(values, "", color);
		return b;
	}

	/// Camembert vide (Phase 5, `PlotMode::PIE`) — ajoutez des parts via
	/// `.addPieSlice(...)` (WidgetBuilder) puis `.Spawn()`. Pas d'axes ni de
	/// navigation (pan/zoom/box-zoom, Phase 3) sur ce mode, cf. doc de
	/// `PlotMode`.
	[[nodiscard]] WidgetBuilder Pie(String title = "") {
		WidgetBuilder b(*world, *layout);
		UiPlot p;
		p.mode = PlotMode::PIE;
		p.title = std::move(title);
		b.plot = Some(std::move(p));
		b.Size(260.f, 220.f);
		return b;
	}

	/// Heatmap vide (Phase 6, `PlotMode::HEATMAP`) — posez la grille via
	/// `.setHeatmapData(...)` (WidgetBuilder) puis `.Spawn()`. Pas d'axes ni
	/// de légende sur ce mode (une grille de couleur n'a pas d'"entrée").
	[[nodiscard]] WidgetBuilder Heatmap(String title = "") {
		WidgetBuilder b(*world, *layout);
		UiPlot p;
		p.mode = PlotMode::HEATMAP;
		p.showLegend = false;
		p.title = std::move(title);
		b.plot = Some(std::move(p));
		b.Size(260.f, 220.f);
		return b;
	}

	/// Chandelier vide (Phase 6, `PlotMode::CANDLE`) — ajoutez des bougies
	/// via `.AddOhlcBar(...)` (WidgetBuilder) puis `.Spawn()`. Graduations/
	/// grille dans le style XY (réutilise `xAxis`/`yAxis`), mais toujours
	/// auto-ajustées (pas de pan/zoom/box-zoom sur ce mode, cf. doc de
	/// `resolveCandleAxis()`) et pas de légende (une seule série de bougies).
	[[nodiscard]] WidgetBuilder Candlestick(String title = "") {
		WidgetBuilder b(*world, *layout);
		UiPlot p;
		p.mode = PlotMode::CANDLE;
		p.showLegend = false;
		p.yAxis.tickPosition = PlotEdge::Left;
		p.title = std::move(title);
		b.plot = Some(std::move(p));
		b.Size(360.f, 220.f);
		return b;
	}

	/// Panneau redimensionnable en 2 (cf. UiResizeHandle, interaction.hpp —
	/// RÉUTILISE le composant/l'entité directement, pas juste l'algorithme,
	/// contrairement aux colonnes de UiTable qui le réimplémentent en
	/// interne). `before`/`after` sont deux builders NON encore spawnés
	/// (typiquement `panel()`/`column()` avec leur contenu) ; la poignée
	/// s'insère entre les deux, glissable, épinglant la taille de `before`
	/// en pixels pendant le drag (`after` en Grow absorbe le reste — cf.
	/// resolveResizeDrag()). Retourne l'ecs::Entity du CONTENEUR (row/column).
	[[nodiscard]] ecs::Entity Splitter(WidgetBuilder before, WidgetBuilder after,
								  Orientation orient = Orientation::Horizontal, float initialBeforeSize = 200.f,
								  float minBefore = 50.f, float minAfter = 50.f) {
		const bool HORIZ = orient == Orientation::Horizontal;

		WidgetBuilder container(*world, *layout);
		if (HORIZ)
			container.Row();
		else
			container.Column();
		container.Gap(0.f).WAuto().HAuto();
		ecs::Entity containerE = container.Spawn();

		before.Parent(containerE);
		if (HORIZ)
			before.W(Dimension::Px(initialBeforeSize));
		else
			before.H(Dimension::Px(initialBeforeSize));
		ecs::Entity beforeE = before.Spawn();

		WidgetBuilder handleB(*world, *layout);
		if (HORIZ)
			handleB.Size(6.f, 0.f).HAuto();
		else
			handleB.Size(0.f, 6.f).WAuto();
		handleB.Parent(containerE);
		UiResizeHandle h;
		h.orient = orient;
		h.minBefore = minBefore;
		h.maxBefore = 1.0e9f;
		ecs::ArchetypeRegistry *world = this->world;
		h.getBefore = [world, beforeE, HORIZ]() -> float {
			if (auto it = world->GetComponent<UiItem>(beforeE); it.IsSome())
				return HORIZ ? it.Unwrap()->width.value : it.Unwrap()->height.value;
			return 0.f;
		};
		h.setBefore = [world, beforeE, HORIZ](float v) {
			if (auto it = world->GetComponent<UiItem>(beforeE); it.IsSome()) {
				if (HORIZ)
					it.Unwrap()->width = Dimension::Px(v);
				else
					it.Unwrap()->height = Dimension::Px(v);
			}
		};
		handleB.resizeHandle = Some(std::move(h));
		handleB.Spawn();

		after.Parent(containerE);
		if (HORIZ)
			after.GrowW();
		else
			after.GrowH();
		after.Spawn();
		(void)minAfter; // réservé : un 2e panneau explicitement dimensionné (non-Grow) l'utiliserait via setAfter

		return containerE;
	}

	/// Sélecteur de date, en popup (cf. UiFactory::Popup(), Phase 1) : grille
	/// calendrier mensuel (cf. UiCalendar, components.hpp) avec navigation
	/// précédent/suivant. `onChange` reçoit (année, mois, jour) au clic sur
	/// une case — signature à 3 valeurs, donc posée DIRECTEMENT sur le
	/// composant (UiCalendar::onDaySelected) plutôt que via
	/// UiCallbacks::onChange (qui n'en porte qu'une).
	[[nodiscard]] WidgetBuilder DatePicker(int year, int month, int selectedDay = -1,
										   std::function<void(int, int, int)> onChange = nullptr) {
		WidgetBuilder cal(*world, *layout);
		UiCalendar c;
		c.year = year;
		c.month = month;
		c.selectedDay = selectedDay;
		c.onDaySelected = std::move(onChange);
		cal.calendar = Some(std::move(c));
		cal.Size(224.f, 200.f);

		auto p = Popup();
		p.Column().Pad(math::Sides{4.f}).WAuto().HAuto();
		p.Children(std::move(cal));
		return p;
	}

	[[nodiscard]] WidgetBuilder Image(String textureKey, float w, float h) {
		WidgetBuilder b(*world, *layout);
		b.image = Some(UiImage{std::move(textureKey)});
		b.Size(w, h);
		return b;
	}

	/// Widget affichant une scène render3d:: indépendante (M22, viewport3d.hpp)
	/// — `root` DOIT rester en vie au moins aussi longtemps que le widget (cf.
	/// UiViewport3D::root, non possédé : Object3D est non-copiable/non-
	/// déplaçable). Aucune taille par défaut posée ici (contrairement à
	/// `Image()`) : un Viewport3D n'a pas de dimension "intrinsèque" comme une
	/// texture chargée — l'appelant pose `.Size(...)`/`.GrowW()`/etc. comme pour
	/// n'importe quel autre widget ; Viewport3DSystem::Update lit simplement la
	/// taille résolue par LayoutSystem chaque frame (UiComputed.screen).
	[[nodiscard]] WidgetBuilder Viewport3D(render3d::Object3D &root, render3d::Camera camera = {}) {
		WidgetBuilder b(*world, *layout);
		UiViewport3D vp;
		vp.root = &root;
		vp.camera = camera;
		b.viewport3d = Some(std::move(vp));
		return b;
	}

	/// Icône glyphe (Cupertino/Material...) — `value` est contraint par le
	/// concept `GlyphSet` à une valeur d'un `enum class` d'icônes déclaré
	/// dans glyphs.hpp (jamais un entier ou un nom en chaîne). La police
	/// correspondante doit être chargée et enregistrée sous son nom de
	/// famille via `RenderSystem::RegisterFont(Glyphs::FontFamily<E>(), font)`
	/// pour que l'icône se dessine (cf. Glyphs::fontFile<E>() pour le fichier
	/// à charger, dans assets/fonts/). Teinte : text-color (classe "root-icon").
	template <GlyphSet E>
	[[nodiscard]] WidgetBuilder Icon(E value, float size = 16.f) {
		WidgetBuilder b(*world, *layout);
		UiIcon ic;
		ic.glyph = Glyphs::ToUtf8(value);
		ic.font = String(Glyphs::FontFamily<E>());
		ic.size = size;
		b.icon = Some(std::move(ic));
		b.Size(size, size);
		return b;
	}

	/// Bouton radio : un seul coché à la fois parmi ceux du même `group`.
	[[nodiscard]] WidgetBuilder Radio(String group, String text, bool checked = false) {
		WidgetBuilder b(*world, *layout);
		UiRadio r;
		r.group = std::move(group);
		r.text = std::move(text);
		r.checked = checked;
		b.radio = Some(std::move(r));
		return b;
	}

	/// Barre de défilement autonome (viewSize visible sur contentSize total).
	[[nodiscard]] WidgetBuilder Scrollbar(float contentSize, float viewSize,
										  Orientation orient = Orientation::Horizontal) {
		WidgetBuilder b(*world, *layout);
		UiScrollBar s;
		s.contentSize = contentSize;
		s.viewSize = viewSize;
		s.orient = orient;
		b.scrollbar = Some(s);
		return b;
	}

	/// Potentiomètre rotatif, valeur normalisée [0,1].
	[[nodiscard]] WidgetBuilder Knob(float value = 0.f) { return Knob(0.f, 1.f, value, 0.f); }

	/// Potentiomètre rotatif à plage [min, max], quantifié par `step` (0 = continu).
	[[nodiscard]] WidgetBuilder Knob(float min, float max, float value, float step = 0.f) {
		WidgetBuilder b(*world, *layout);
		UiKnob k;
		k.min = min;
		k.max = max;
		k.step = step;
		k.value = sdl3::Clamp(value, min, max);
		b.knob = Some(k);
		return b;
	}

	/// Zone de dessin libre — `fn(renderer, rectEcran)` appelée au rendu.
	[[nodiscard]] WidgetBuilder Canvas(std::function<void(sdl3::Renderer &, sdl3::FRect)> fn) {
		WidgetBuilder b(*world, *layout);
		b.canvas = Some(UiCanvas{std::move(fn)});
		return b;
	}

	/// Liste déroulante. `selected` : index initial (-1 = aucun). onChange
	/// reçoit l'index sélectionné (en float).
	[[nodiscard]] WidgetBuilder Combo(std::vector<String> items, int selected = 0) {
		WidgetBuilder b(*world, *layout);
		UiComboBox cb;
		cb.items = std::move(items);
		cb.selected = cb.items.empty() ? -1 : sdl3::Clamp(selected, -1, int(cb.items.size()) - 1);
		cb.itemHeight = theme.fontSize + 12.f;
		b.combo = Some(std::move(cb));
		return b;
	}

	/// Liste sélectionnable à scroll interne. onChange reçoit l'index.
	[[nodiscard]] WidgetBuilder Listbox(std::vector<String> items, int selected = -1) {
		WidgetBuilder b(*world, *layout);
		ListBox lb;
		lb.items = std::move(items);
		lb.selected = lb.items.empty() ? -1 : sdl3::Clamp(selected, -1, int(lb.items.size()) - 1);
		lb.itemHeight = theme.fontSize + 10.f;
		b.listbox = Some(std::move(lb));
		return b;
	}

	/// Section repliable : les enfants forment le corps, l'en-tête les
	/// replie/déplie. onToggle reçoit le nouvel état.
	[[nodiscard]] WidgetBuilder Expander(String title, bool expanded = true) {
		WidgetBuilder b(*world, *layout);
		UiExpander x;
		x.title = std::move(title);
		x.expanded = expanded;
		x.headerHeight = theme.fontSize + 14.f;
		b.Column().Gap(6.f);
		// Le padding haut réserve la place de l'en-tête dessiné par le widget.
		b.Pad(math::Sides{8.f, x.headerHeight + 6.f, 8.f, 8.f});
		// UiItem Auto explicite : la hauteur doit se recalculer à chaque
		// repli/dépli (sans lui, sizeVals() gèle sur la taille écrite par la
		// passe précédente dans UiRect.size).
		b.WAuto().HAuto();
		b.expander = Some(std::move(x));
		return b;
	}

	/// Conteneur à onglets : un enfant par entrée de `tabs`, seul l'actif est
	/// visible. onChange reçoit l'index de l'onglet activé.
	[[nodiscard]] WidgetBuilder Tabview(std::vector<String> tabs, int active = 0) {
		WidgetBuilder b(*world, *layout);
		UiTabView tv;
		tv.tabs = std::move(tabs);
		tv.active = tv.tabs.empty() ? 0 : sdl3::Clamp(active, 0, int(tv.tabs.size()) - 1);
		tv.tabHeight = theme.fontSize + 16.f;
		b.Column().Gap(6.f);
		// Le padding haut réserve la barre d'onglets dessinée par le widget.
		b.Pad(math::Sides{8.f, tv.tabHeight + 6.f, 8.f, 8.f});
		b.WAuto().HAuto(); // idem expander : recalcul au changement d'onglet
		b.tabview = Some(std::move(tv));
		return b;
	}

	/// Indicateur de chargement animé (arc tournant).
	[[nodiscard]] WidgetBuilder Spinner(float thickness = 3.f) {
		WidgetBuilder b(*world, *layout);
		UiSpinner sp;
		sp.thickness = thickness;
		b.spinner = Some(sp);
		return b;
	}

	/// Pastille de notification (compteur, statut...).
	[[nodiscard]] WidgetBuilder Badge(String text) {
		WidgetBuilder b(*world, *layout);
		UiBadge bd;
		bd.text = std::move(text);
		b.badge = Some(std::move(bd));
		// Taille réduite posée en style inline (computeFonts() du layout ne
		// lit que le style inline, pas les classes — cf. systems.hpp).
		b.Style(UiStyle{}.SetFontSize(theme.fontSize * 0.85f));
		// Une pastille épouse son contenu (pas de Stretch du conteneur).
		b.AlignSelf(CrossAlign::Start);
		return b;
	}

	/// Popup ancré (menu contextuel, liste de suggestions, dropdown d'un
	/// color picker...) : caché par défaut, à ouvrir/fermer via
	/// `ui::OpenPopup`/`ui::closePopup` (positionné au clic ouvrant — poser
	/// `.Anchor(...)`/`.Offset(...)` avant `openPopup` pour le placer près de
	/// son déclencheur). Pas de scrim, se ferme au clic extérieur ou Échap
	/// n'affecte QUE les modales — cf. InputSystem::dispatch. Composez son
	/// contenu via `.Children(...)` comme n'importe quel conteneur.
	[[nodiscard]] WidgetBuilder Popup() {
		WidgetBuilder b(*world, *layout);
		b.Fixed().Hidden();
		b.popupState = Some(UiPopupState{});
		if (theme.glassDefault)
			b.Style(UiStyle::GlassPanel(theme.panelBg));
		else
			b.Bg(theme.panelBg).BorderColor(theme.border).Radius(6.f);
		return b;
	}

	/// Popup de MENU (déroulant de barre, menu contextuel, sous-menu), dessiné
	/// comme la liste d'un combo ouvert : un seul fond (celui des champs),
	/// un liseré d'accent, et des entrées en lignes pleine largeur SANS
	/// espacement ni fond propre au repos — seule la ligne survolée se
	/// détache (cf. RenderSystem, UiMenuItem). Thème verre : le panneau
	/// « verre » habituel.
	[[nodiscard]] WidgetBuilder MenuPopup(int overlayOrder) {
		WidgetBuilder b(*world, *layout);
		b.Fixed().Hidden();
		b.popupState = Some(UiPopupState{});
		if (theme.glassDefault)
			b.Style(UiStyle::GlassPanel(theme.panelBg));
		else
			b.Bg(theme.combo.bgNormal.a > 0.f ? theme.combo.bgNormal : theme.fieldBg)
				.BorderColor(theme.combo.borderFocus)
				.Radius(4.f);
		b.Column().Gap(0.f).Pad(math::Sides{0.f, MENU_POPUP_PAD_Y}).WAuto().HAuto();
		b.OverlayOrder(overlayOrder);
		return b;
	}

	/// Boîte de dialogue modale, centrée, avec fond assombri qui bloque tout
	/// le reste de l'interface tant qu'elle est ouverte (cf.
	/// InputSystem::OpenModal/closeModal — Échap la referme). `content` est
	/// le panneau du dialogue lui-même : dimensionnez-le normalement (ex:
	/// `f.Panel().Size(400, 240).Children(...)`), il est automatiquement
	/// enveloppé avec le scrim plein écran et centré.
	[[nodiscard]] WidgetBuilder Modal(WidgetBuilder content) {
		WidgetBuilder wrapper(*world, *layout);
		wrapper.Fixed().Hidden();
		wrapper.W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f));
		wrapper.popupState = Some(UiPopupState{false, true, ecs::Entity{}});

		WidgetBuilder scrim(*world, *layout);
		scrim.panel = Some(UiPanel{});
		scrim.Style(UiStyle{}.SetBg(sdl3::FColor{0/255.f, 0/255.f, 0/255.f, 140/255.f}));
		scrim.Absolute().Anchor(Anchor::TopLeft);
		scrim.W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f));

		content.Absolute().Anchor(Anchor::Center);

		wrapper.Children(std::move(scrim), std::move(content));
		return wrapper;
	}

private:
	ecs::ArchetypeRegistry *world;
	LayoutSystem *layout;

	/// Enregistre "root" (défauts génériques : police/couleur de texte du
	/// thème) et "root-<type>" (une classe par type de widget doté d'une
	/// table WidgetColors dans UiTheme) — appelé une fois à la construction.
	/// Ces classes sont toujours en tête de UiClassList (cf. Spawn()) : la
	/// cascade les surcharge en priorité la plus basse.
	void RegisterRootClasses() {
		sheet.Define("root", UiStyle{}.SetFontSize(theme.fontSize).SetTextColor(theme.text).SetOpacity(1.f));
		sheet.Define("root-panel",
					 UiStyle{}.SetBg(theme.panelBg).SetBorderColor(theme.border).SetBordersRadius(8.f));
		sheet.Define("root-label", UiStyle{}.SetTextColor(theme.text));
		sheet.Define("root-button", StyleFromColors(theme.button).SetBordersRadius(6.f));
		sheet.Define("root-toggle", StyleFromColors(theme.toggle));
		sheet.Define("root-checkbox", StyleFromColors(theme.checkbox));
		sheet.Define("root-slider", StyleFromColors(theme.slider));
		sheet.Define("root-progress", UiStyle{}.SetBg(theme.track).SetBgChecked(theme.fill).SetBordersRadius(4.f));
		sheet.Define("root-separator", UiStyle{}.SetBg(theme.border));
		sheet.Define("root-input", StyleFromColors(theme.input));
		sheet.Define("root-dragvalue", StyleFromColors(theme.dragValue).SetBordersRadius(4.f));
		sheet.Define("root-colorswatch", UiStyle{}.SetBorderColor(theme.border).SetBordersRadius(4.f));
		// Réutilise la table listbox (même sémantique : normal=fond, hovered
		// /checked=surbrillance de ligne survolée/sélectionnée) — pas de
		// nouvelle table WidgetColors dédiée, Selectable/TreeNode n'ont pas
		// de bordure/fond au repos distincts d'une ListBox.
		sheet.Define("root-selectable", StyleFromColors(theme.listbox).SetBordersRadius(4.f));
		sheet.Define("root-treenode", StyleFromColors(theme.listbox).SetBordersRadius(4.f));
		sheet.Define("root-menubaritem", StyleFromColors(theme.listbox).SetBordersRadius(4.f));
		// Entrées de menu = lignes de la liste d'un combo (mêmes couleurs).
		sheet.Define("root-menuitem", StyleFromColors(theme.combo).SetBordersRadius(0.f));
		sheet.Define("root-table", StyleFromColors(theme.listbox).SetBordersRadius(4.f));
		sheet.Define("root-plot", UiStyle{}.SetBg(theme.fieldBg).SetBorderColor(theme.border).SetBordersRadius(4.f)
									   .SetBgChecked(theme.accent));
		sheet.Define("root-calendar",
					 UiStyle{}.SetBg(theme.fieldBg).SetBorderColor(theme.border).SetBordersRadius(4.f)
						 .SetBgChecked(theme.fill));
		sheet.Define("root-splitterhandle", UiStyle{}.SetBg(theme.border).SetBgHovered(theme.accent));
		sheet.Define("root-icon", UiStyle{}.SetTextColor(theme.text));
		sheet.Define("root-radio", StyleFromColors(theme.radio));
		sheet.Define("root-scrollbar", StyleFromColors(theme.scrollbar));
		sheet.Define("root-knob", StyleFromColors(theme.knob));
		sheet.Define("root-combo", StyleFromColors(theme.combo));
		sheet.Define("root-listbox", StyleFromColors(theme.listbox));
		sheet.Define("root-expander", StyleFromColors(theme.expander));
		sheet.Define("root-tabview", StyleFromColors(theme.tabs));
		sheet.Define("root-spinner", UiStyle{}.SetBg(theme.spinner));
		sheet.Define("root-badge", UiStyle{}.SetBg(theme.badgeBg).SetTextColor(theme.badgeText));
	}
};

} // namespace ui
