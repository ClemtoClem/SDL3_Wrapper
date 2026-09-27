/* @file styles.hpp */

#pragma once
/**
 * ui::styles — système de styles dynamiques avec héritage parent→enfant,
 * inspiré de SDL3_ui (UI/Style.hpp), adapté à l'architecture ECS de ce
 * projet : chaque propriété de style est un petit type C++ nommé
 * (`prop::BackgroundColor`, `prop::FontSize`, ...) stocké dans un sac
 * type-erased indexé par type (`UiStyle`) — ajouter une propriété ne
 * demande qu'une ligne de macro, sans toucher au moteur de cascade.
 *
 * Cascade (comme CSS) :
 *   1. Style inline  (UiStyle sur l'entité)          — priorité maximale
 *   2. Classes       (UiClassList → UiStyleSheet)     — ordre de la liste,
 *      la dernière gagne (toujours précédée de "root" + "root-<type>",
 *      injectées par UiFactory::Spawn() — cf. factory.hpp)
 *   3. Héritage      (propriétés héritables du parent : text-color,
 *      font-size, text-align, opacity — seul sous-ensemble qui cascade
 *      automatiquement, comme en CSS où background/border/padding
 *      n'héritent pas)
 *
 * L'esthétique de TOUS les widgets dépend exclusivement de ce système : les
 * composants de widgets (UiLabel, UiButton, UiSlider...) ne stockent que
 * leurs champs structurels/fonctionnels (texte, valeur, min/max, coché...),
 * jamais de police/couleur/bordure — cf. components.hpp.
 *
 * Usage :
 * @code{.cpp}
 *   // Définir des classes (souvent au démarrage)
 *   factory.sheet.define("danger", UiStyle{}
 *       .SetBg({80, 20, 20}).SetTextColor({255, 80, 80})
 *       .setBordersRadius(4.f));
 *
 *   // Créer un widget avec classes + style inline
 *   auto btn = f.Button("Supprimer")
 *       .className("danger")
 *       .Style(UiStyle{}.setBordersWidth(2.f))
 *       .OnClick([] { ... })
 *       .Spawn();
 *
 *   // Changer dynamiquement (API par chemin "cible.propriete" — cf. ui::Ui)
 *   gui.CreateStyleClass("aero", UiStyle::glassButton());
 *   gui.setStyleProperty("menu_1.background-color", sdl3::FColor::UI_ACCENT_BLUE_PRIMARY());
 *
 *   // Dans un renderer, lire la valeur résolue :
 *   auto s = ui::getResolved(world, entity);
 *   sdl3::FColor bg = s.Bg({40, 40, 40});          // fallback si non résolu
 *   float fs = s.FontSize(14.f);
 * @endcode
 */
#include <any>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "components.hpp"

namespace ui {

// ============================================================================
// prop:: — types de propriétés de style (un type C++ = une clé de style)
// ============================================================================

namespace prop {

#define UI_PROP_BOOL(S, Name)                                                                                       \
	struct S {                                                                                                       \
		bool value;                                                                                                 \
		static constexpr const char *kName = Name;                                                                   \
	};
#define UI_PROP_COLOR(S, Name)                                                                                       \
	struct S {                                                                                                       \
		sdl3::FColor value;                                                                                                 \
		static constexpr const char *kName = Name;                                                                   \
	};
#define UI_PROP_FLOAT(S, Name)                                                                                       \
	struct S {                                                                                                       \
		float value;                                                                                                 \
		static constexpr const char *kName = Name;                                                                   \
	};
#define UI_PROP_SIDES(S, Name)                                                                                       \
	struct S {                                                                                                       \
		math::Sides value;                                                                                                 \
		static constexpr const char *kName = Name;                                                                   \
	};
#define UI_PROP_ALIGN(S, Name)                                                                                       \
	struct S {                                                                                                       \
		TextAlign value;                                                                                             \
		static constexpr const char *kName = Name;                                                                   \
	};
#define UI_PROP_CORNERS(S, Name)                                                                                       \
	struct S {                                                                                                       \
		math::Corners value;                                                                                             \
		static constexpr const char *kName = Name;                                                                   \
	};

UI_PROP_BOOL(Enable, "enable")
UI_PROP_BOOL(Visible, "visible")
// Formatage de texte — cf. RenderSystem::drawWidget (bloc UiLabel). Bold/
// Italic sélectionnent une police enregistrée (registerBoldFont()/
// registerItalicFont()/registerBoldItalicFont()), sans effet (retombe sur la
// police normale) si non enregistrée. Underline/Strikethrough sont dessinés
// manuellement (une bande pleine sous/à travers le texte, indépendamment de
// la police — fonctionne avec n'importe quelle combinaison Bold/Italic sans
// exiger une police par combinaison). Highlight active un fond plein DERRIÈRE
// le texte, coloré par `prop::HighlightTextColor` (ci-dessous — déclaré de
// longue date mais jamais câblé avant cette fonctionnalité).
UI_PROP_BOOL(Italic, "italic")
UI_PROP_BOOL(Bold, "bold")
UI_PROP_BOOL(Underline, "underline")
UI_PROP_BOOL(Strikethrough, "strikethrough")
UI_PROP_BOOL(Highlight, "highlight")

// Couleurs visuelles (non-héritables, sauf TextColor)
UI_PROP_COLOR(BackgroundColor, "background-color")
UI_PROP_COLOR(BackgroundGradient, "background-gradient")
UI_PROP_COLOR(HoveredBackgroundColor, "background-hover-color")
UI_PROP_COLOR(PressedBackgroundColor, "background-pressed-color")
UI_PROP_COLOR(CheckedBackgroundColor, "background-checked-color")
UI_PROP_COLOR(FocusedBackgroundColor, "background-focus-color")

UI_PROP_COLOR(TextColor, "text-color") // héritable
UI_PROP_COLOR(HoveredTextColor, "text-hover-color")
UI_PROP_COLOR(PressedTextColor, "text-pressed-color")
UI_PROP_COLOR(CheckedTextColor, "text-checked-color")
UI_PROP_COLOR(FocusedTextColor, "text-focus-color")
UI_PROP_COLOR(HighlightTextColor, "text-highlight-color") // héritable

UI_PROP_COLOR(BorderColor, "border-color")
UI_PROP_COLOR(HoveredBorderColor, "border-hover-color")
UI_PROP_COLOR(PressedBorderColor, "border-pressed-color")
UI_PROP_COLOR(CheckedBorderColor, "border-checked-color")
UI_PROP_COLOR(FocusedBorderColor, "border-focus-color")

// Dimensions visuelles (non-héritables)
UI_PROP_SIDES(BordersWidth, "borders-width")
UI_PROP_CORNERS(BordersRadius, "borders-radius")

// Typographie (héritable sauf mention)
UI_PROP_FLOAT(FontSize, "font-size") // héritable
UI_PROP_ALIGN(TextAlignProp, "text-align") // héritable

// Échelle de contenu (héritable, composée MULTIPLICATIVEMENT le long de
// l'arbre — cf. EmBases::scale/LayoutSystem::computeFonts()/Dimension::
// resolve() dans components.hpp/systems.hpp). Node-graph inner-zoom, mais
// générique : n'importe quel sous-arbre peut s'en servir pour se
// redimensionner en bloc. LIMITATION CONNUE (partagée avec FontSize) :
// LayoutSystem::computeFonts() ne lit que le style INLINE d'une entité (pas
// la cascade complète via UiClassList), donc une classe qui poserait
// InnerZoom sans style inline explicite ne serait pas prise en compte pour
// le LAYOUT — seulement pour la lecture via ResolvedStyle::value<InnerZoom>.
UI_PROP_FLOAT(InnerZoom, "inner-zoom") // héritable

// Layout (non-héritables, surchargent UiFlow)
UI_PROP_SIDES(Margin, "margin")
UI_PROP_SIDES(Padding, "padding")
UI_PROP_FLOAT(Gap, "gap")

// Opacité (héritable)
UI_PROP_FLOAT(Opacity, "opacity")

// Effet « verre » (thème Aero — non-héritables) : bande de reflet
// translucide sur le haut du rect (Gloss) + anneaux de bordure à alpha
// décroissant autour (Glow/GlowColor). Cf. UiStyle::glass().
UI_PROP_FLOAT(Gloss, "gloss")
UI_PROP_FLOAT(Glow, "glow")
UI_PROP_COLOR(GlowColor, "glow-color")

#undef UI_PROP_BOOL
#undef UI_PROP_COLOR
#undef UI_PROP_FLOAT
#undef UI_PROP_SIDES
#undef UI_PROP_ALIGN
#undef UI_PROP_CORNERS

} // namespace prop

// ============================================================================
// UiStyle — sac de propriétés type-erased (inline sur une entité, ou
// définition de classe dans un UiStyleSheet)
// ============================================================================

class UiStyle {
public:
	/// Pose (ou remplace) une propriété typée. Retourne *this pour le chaînage.
	template <typename P> UiStyle &Set(P p) {
		entries[std::type_index(typeid(P))] = Entry{std::any(std::move(p)), P::kName};
		return *this;
	}

	/// Lit une propriété typée. nullptr si non posée sur CE UiStyle (pas de
	/// résolution de cascade ici — voir ResolvedStyle pour la valeur finale).
	template <typename P> [[nodiscard]] const P *Get() const {
		auto it = entries.find(std::type_index(typeid(P)));
		return it == entries.end() ? nullptr : std::any_cast<P>(&it->second.value);
	}
	template <typename P> [[nodiscard]] bool Has() const noexcept { return Get<P>() != nullptr; }

	/// Fusionne un autre style dans celui-ci (l'autre gagne sur les conflits,
	/// propriété par propriété — clé = type C++, pas de notion d'ordre interne).
	void Merge(const UiStyle &other);

	[[nodiscard]] bool IsEmpty() const noexcept { return entries.empty(); }

	// ── Setters fluents (ergonomie — équivalents à .Set(prop::Xxx{v})) ─────
	UiStyle &SetEnable(bool v) { return Set(prop::Enable(v)); }
	UiStyle &Disable() { return Set(prop::Enable(false)); }
	UiStyle &Enable() { return Set(prop::Enable(true)); }
	
	UiStyle &SetVisible(bool v) { return Set(prop::Visible(v)); }
	UiStyle &Hidden() { return Set(prop::Visible(false)); }
	UiStyle &Show() { return Set(prop::Visible(true)); }

	UiStyle &SetItalic(bool v = true) { return Set(prop::Italic(v)); }
	UiStyle &SetBold(bool v = true) { return Set(prop::Bold(v)); }
	UiStyle &SetUnderline(bool v = true) { return Set(prop::Underline(v)); }
	UiStyle &SetStrikethrough(bool v = true) { return Set(prop::Strikethrough(v)); }
	UiStyle &SetHighlight(bool v = true) { return Set(prop::Highlight(v)); }
	/// Réinitialise tout formatage de texte (raccourci "normal" — pas un
	/// simple UiStyle{} vide, qui n'ÉCRASERAIT rien dans la cascade : ceci
	/// pose explicitement chaque propriété à false pour surcharger une
	/// classe/un parent qui en aurait activé).
	UiStyle &SetNormal() { return SetItalic(false).SetBold(false).SetUnderline(false).SetStrikethrough(false); }

	UiStyle &SetBg(sdl3::FColor c) { return Set(prop::BackgroundColor{c}); }
	UiStyle &SetBgGradient(sdl3::FColor c) { return Set(prop::BackgroundGradient{c}); }
	UiStyle &SetBgHovered(sdl3::FColor c) { return Set(prop::HoveredBackgroundColor{c}); }
	UiStyle &SetBgPressed(sdl3::FColor c) { return Set(prop::PressedBackgroundColor{c}); }
	UiStyle &SetBgChecked(sdl3::FColor c) { return Set(prop::CheckedBackgroundColor{c}); }
	UiStyle &SetBgFocus(sdl3::FColor c) { return Set(prop::FocusedBackgroundColor{c}); }
	
	UiStyle &SetTextColor(sdl3::FColor c) { return Set(prop::TextColor{c}); }
	UiStyle &SetTextHovered(sdl3::FColor c) { return Set(prop::HoveredTextColor{c}); }
	UiStyle &SetTextPressed(sdl3::FColor c) { return Set(prop::PressedTextColor{c}); }
	UiStyle &SetTextChecked(sdl3::FColor c) { return Set(prop::CheckedTextColor{c}); }
	UiStyle &SetTextFocus(sdl3::FColor c) { return Set(prop::FocusedTextColor{c}); }
	UiStyle &SetHighlightColor(sdl3::FColor c) { return Set(prop::HighlightTextColor{c}); }

	UiStyle &SetBorderColor(sdl3::FColor c) { return Set(prop::BorderColor{c}); }
	UiStyle &SetBorderHovered(sdl3::FColor c) { return Set(prop::HoveredBorderColor{c}); }
	UiStyle &SetBorderPressed(sdl3::FColor c) { return Set(prop::PressedBorderColor{c}); }
	UiStyle &SetBorderChecked(sdl3::FColor c) { return Set(prop::CheckedBorderColor{c}); }
	UiStyle &SetBorderFocus(sdl3::FColor c) { return Set(prop::FocusedBorderColor{c}); }
	
	UiStyle &SetBordersWidth(math::Sides v) { return Set(prop::BordersWidth{v}); }
	UiStyle &SetBordersWidth(float all) { return Set(prop::BordersWidth{math::Sides(all)}); }
	
	UiStyle &SetBordersRadius(math::Corners v) { return Set(prop::BordersRadius{v}); }
	UiStyle &SetBordersRadius(float all) { return Set(prop::BordersRadius{math::Corners(all)}); }
   
	UiStyle &SetFontSize(float v) { return Set(prop::FontSize{v}); }
	UiStyle &SetTextAlign(TextAlign a) { return Set(prop::TextAlignProp{a}); }
	UiStyle &SetInnerZoom(float v) { return Set(prop::InnerZoom{v}); }
	
	UiStyle &SetMargin(math::Sides v) { return Set(prop::Margin{v}); }
	UiStyle &SetMargin(float all) { return Set(prop::Margin{math::Sides(all)}); }
	
	UiStyle &SetPadding(math::Sides v) { return Set(prop::Padding{v}); }
	UiStyle &SetPadding(float all) { return Set(prop::Padding{math::Sides(all)}); }
	
	UiStyle &SetGap(float v) { return Set(prop::Gap{v}); }
	
	UiStyle &SetOpacity(float v) { return Set(prop::Opacity{v}); }
	
	UiStyle &SetGloss(float v) { return Set(prop::Gloss{v}); }
	UiStyle &SetGlow(float v) { return Set(prop::Glow{v}); }
	UiStyle &SetGlowColor(sdl3::FColor c) { return Set(prop::GlowColor{c}); }

	// ── Présets « verre » (thème Aero — Windows 7), personnalisables en
	// chaînant les setters fluents ci-dessus sur la valeur retournée ────────

	/// Préset générique : fond en dégradé vertical (clair en haut → `base`
	/// en bas), coins arrondis, reflet + lueur de bordure. Base de tous les
	/// autres présets `glassXxx()` — personnalisable via les setters fluents
	/// (ex: `UiStyle::glass({40,110,190}).setBordersRadius(12.f)`).
	[[nodiscard]] static UiStyle Glass(sdl3::FColor base, float gloss = 0.35f, float glow = 0.45f, float radius = 8.f);

	/// Panneau vitré translucide (fond de carte/fenêtre).
	[[nodiscard]] static UiStyle GlassPanel(sdl3::FColor base = sdl3::FColor{70/255.f, 120/255.f, 190/255.f, 165/255.f});

	/// Bouton vitré (reflet plus marqué, coins plus arrondis).
	[[nodiscard]] static UiStyle GlassButton(sdl3::FColor base = sdl3::FColor{60/255.f, 140/255.f, 220/255.f, 220/255.f});

private:
	struct Entry {
		std::any value;
		const char *name;
	};
	std::unordered_map<std::type_index, Entry> entries;
};

// ============================================================================
// UiClassList — composant : liste de noms de classes référencées
// ============================================================================

struct UiClassList {
	std::vector<String> names;
};

// ============================================================================
// UiComputedStyle — composant : résultat résolu de la cascade
// ============================================================================

struct UiComputedStyle {
	UiStyle style;
};

// ============================================================================
// UiStyleDirty — marqueur : l'entité doit être re-résolue
// ============================================================================

struct UiStyleDirty {};

// Forward declarations — définies plus bas ("Fonctions libres"), mais déjà
// utilisées par UiStyleSheet::dirtyClassUsers et par les fonctions libres
// qui suivent (setInlineStyle, addClass, toggleClass, ...).
void MarkSubtreeDirty(ecs::ArchetypeRegistry &world, ecs::Entity e);
[[nodiscard]] bool HasClass(ecs::ArchetypeRegistry &world, ecs::Entity e, const String &name);

// ============================================================================
// UiStyleSheet — registre de classes nommées
// ============================================================================

class UiStyleSheet {
public:
	/// Définit (ou redéfinit) une classe. Retourne *this pour le chaînage.
	UiStyleSheet &Define(String name, UiStyle style);

	/// Supprime une classe.
	void Undefine(const String &name) { classes.erase(String(name.c_str())); }

	/// Cherche une classe. Retourne NONE si inexistante.
	[[nodiscard]] Option<const UiStyle *> Lookup(const String &name) const;

	/// Vrai si la classe existe.
	[[nodiscard]] bool Has(const String &name) const { return classes.count(String(name.c_str())) > 0; }

	/// Supprime toutes les classes.
	void Clear() { classes.clear(); }

	/// Marque toutes les entités portant cette classe comme dirty
	/// (appelé après une redéfinition de classe).
	void DirtyClassUsers(ecs::ArchetypeRegistry &world, const String &name);

	/// Marque dirty TOUT widget stylé par une classe, quelle qu'elle soit.
	///
	/// Nécessaire pour un changement de thème à chaud (cf.
	/// `UiFactory::SetTheme`) : redéfinir les classes « root-* » ne suffit
	/// pas, encore faut-il forcer la cascade à se re-résoudre sur les
	/// widgets DÉJÀ construits — sinon la nouvelle palette ne s'applique
	/// qu'aux widgets créés ensuite. Appeler `DirtyClassUsers` une fois par
	/// classe ferait le même travail en autant de parcours complets de
	/// l'ECS ; ici, un seul suffit.
	void DirtyAllUsers(ecs::ArchetypeRegistry &world);

	/// Nombre de classes définies.
	[[nodiscard]] size_t GetSize() const noexcept { return classes.size(); }

private:
	std::unordered_map<String, UiStyle> classes;
};

// ============================================================================
// StyleSystem — résout la cascade chaque frame (si dirty)
// ============================================================================

class StyleSystem {
public:
	/// Feuille de styles contenant les définitions de classes.
	/// Peut être nullptr (seuls les styles inline et l'héritage fonctionnent).
	UiStyleSheet *sheet = nullptr;

	/// Résout la cascade pour toutes les entités dirty.
	/// Doit être appelé APRÈS InputSystem (états hover/press à jour)
	/// et AVANT LayoutSystem (besoin de fontSize résolu pour les unités em).
	void Resolve(ecs::ArchetypeRegistry &world);

private:
	void ResolveEntity(ecs::ArchetypeRegistry &world, ecs::Entity e, const UiStyle *parentResolved);
};

// ============================================================================
// ResolvedStyle — vue légère pour les renderers (accède à UiComputedStyle)
// ============================================================================

class ResolvedStyle {
	const UiStyle *style = nullptr;

public:
	ResolvedStyle() = default;
	explicit ResolvedStyle(const UiStyle *style) noexcept : style(style) {}

	/// Accès générique : valeur résolue de la propriété `P`, ou `fallback`
	/// si non résolue. Les accesseurs nommés ci-dessous en sont de simples
	/// spécialisations pour l'ergonomie des call sites (RenderSystem, etc).
	template <typename P> [[nodiscard]] bool Has() const noexcept { return style && style->Has<P>(); }
	template <typename P>
	[[nodiscard]] auto Value(decltype(P::value) fallback) const noexcept -> decltype(P::value) {
		if (style)
			if (const P *p = style->Get<P>())
				return p->value;
		return fallback;
	}

	[[nodiscard]] bool HasEnable() const noexcept { return Has<prop::Enable>(); }
	[[nodiscard]] bool HasVisible() const noexcept { return Has<prop::Visible>(); }
	[[nodiscard]] bool HasItalic() const noexcept { return Has<prop::Italic>(); }
	[[nodiscard]] bool HasBold() const noexcept { return Has<prop::Bold>(); }
	[[nodiscard]] bool HasUnderline() const noexcept { return Has<prop::Underline>(); }
	[[nodiscard]] bool HasStrikethrough() const noexcept { return Has<prop::Strikethrough>(); }
	[[nodiscard]] bool HasHighlight() const noexcept { return Has<prop::Highlight>(); }
	[[nodiscard]] bool HasHighlightColor() const noexcept { return Has<prop::HighlightTextColor>(); }
	[[nodiscard]] bool HasBg() const noexcept { return Has<prop::BackgroundColor>(); }
	[[nodiscard]] bool HasBgGradient() const noexcept { return Has<prop::BackgroundGradient>(); }
	[[nodiscard]] bool HasBgHovered() const noexcept { return Has<prop::HoveredBackgroundColor>(); }
	[[nodiscard]] bool HasBgPressed() const noexcept { return Has<prop::PressedBackgroundColor>(); }
	[[nodiscard]] bool HasBgChecked() const noexcept { return Has<prop::CheckedBackgroundColor>(); }
	[[nodiscard]] bool HasBgFocus() const noexcept { return Has<prop::FocusedBackgroundColor>(); }
	[[nodiscard]] bool HasTextColor() const noexcept { return Has<prop::TextColor>(); }
	[[nodiscard]] bool HasTextHovered() const noexcept { return Has<prop::HoveredTextColor>(); }
	[[nodiscard]] bool HasTextPressed() const noexcept { return Has<prop::PressedTextColor>(); }
	[[nodiscard]] bool HasTextChecked() const noexcept { return Has<prop::CheckedTextColor>(); }
	[[nodiscard]] bool HasTextFocus() const noexcept { return Has<prop::FocusedTextColor>(); }
	[[nodiscard]] bool HasBorderColor() const noexcept { return Has<prop::BorderColor>(); }
	[[nodiscard]] bool HasBorderHovered() const noexcept { return Has<prop::HoveredBorderColor>(); }
	[[nodiscard]] bool HasBorderPressed() const noexcept { return Has<prop::PressedBorderColor>(); }
	[[nodiscard]] bool HasBorderChecked() const noexcept { return Has<prop::CheckedBorderColor>(); }
	[[nodiscard]] bool HasBorderFocus() const noexcept { return Has<prop::FocusedBorderColor>(); }
	[[nodiscard]] bool HasBordersWidth() const noexcept { return Has<prop::BordersWidth>(); }
	[[nodiscard]] bool HasBordersRadius() const noexcept { return Has<prop::BordersRadius>(); }
	[[nodiscard]] bool HasFontSize() const noexcept { return Has<prop::FontSize>(); }
	[[nodiscard]] bool HasTextAlign() const noexcept { return Has<prop::TextAlignProp>(); }
	[[nodiscard]] bool HasPadding() const noexcept { return Has<prop::Padding>(); }
	[[nodiscard]] bool HasGap() const noexcept { return Has<prop::Gap>(); }
	[[nodiscard]] bool HasOpacity() const noexcept { return Has<prop::Opacity>(); }
	[[nodiscard]] bool HasGloss() const noexcept { return Has<prop::Gloss>(); }
	[[nodiscard]] bool HasGlow() const noexcept { return Has<prop::Glow>(); }
	[[nodiscard]] bool HasGlowColor() const noexcept { return Has<prop::GlowColor>(); }

	[[nodiscard]] bool Enable(bool fallback) const noexcept { return Value<prop::Enable>(fallback); }
	[[nodiscard]] bool Visible(bool fallback) const noexcept { return Value<prop::Visible>(fallback); }
	[[nodiscard]] bool Italic(bool fallback = false) const noexcept { return Value<prop::Italic>(fallback); }
	[[nodiscard]] bool Bold(bool fallback = false) const noexcept { return Value<prop::Bold>(fallback); }
	[[nodiscard]] bool Underline(bool fallback = false) const noexcept { return Value<prop::Underline>(fallback); }
	[[nodiscard]] bool Strikethrough(bool fallback = false) const noexcept;
	[[nodiscard]] bool Highlight(bool fallback = false) const noexcept { return Value<prop::Highlight>(fallback); }
	[[nodiscard]] sdl3::FColor HighlightColor(sdl3::FColor fallback) const noexcept;

	[[nodiscard]] sdl3::FColor Bg(sdl3::FColor fallback) const noexcept { return Value<prop::BackgroundColor>(fallback); }
	[[nodiscard]] sdl3::FColor BgHovered(sdl3::FColor fallback) const noexcept;
	[[nodiscard]] sdl3::FColor BgPressed(sdl3::FColor fallback) const noexcept;
	[[nodiscard]] sdl3::FColor BgChecked(sdl3::FColor fallback) const noexcept;
	[[nodiscard]] sdl3::FColor BgFocus(sdl3::FColor fallback) const noexcept { return Value<prop::FocusedBackgroundColor>(fallback); }
	[[nodiscard]] sdl3::FColor TextColor(sdl3::FColor fallback) const noexcept { return Value<prop::TextColor>(fallback); }
	[[nodiscard]] sdl3::FColor BorderColor(sdl3::FColor fallback) const noexcept { return Value<prop::BorderColor>(fallback); }
	[[nodiscard]] sdl3::FColor BorderFocus(sdl3::FColor fallback) const noexcept { return Value<prop::FocusedBorderColor>(fallback); }
	[[nodiscard]] math::Sides BordersWidth(math::Sides fallback) const noexcept { return Value<prop::BordersWidth>(fallback); }
	[[nodiscard]] math::Corners BordersRadius(math::Corners fallback) const noexcept { return Value<prop::BordersRadius>(fallback); }
	[[nodiscard]] float FontSize(float fallback) const noexcept { return Value<prop::FontSize>(fallback); }
	[[nodiscard]] ui::TextAlign TextAlign(ui::TextAlign fb) const noexcept { return Value<prop::TextAlignProp>(fb); }
	[[nodiscard]] float InnerZoom(float fallback = 1.f) const noexcept { return Value<prop::InnerZoom>(fallback); }
	[[nodiscard]] math::Sides Padding(math::Sides fallback) const noexcept { return Value<prop::Padding>(fallback); }
	[[nodiscard]] float Gap(float fallback) const noexcept { return Value<prop::Gap>(fallback); }
	[[nodiscard]] float Opacity(float fallback) const noexcept { return Value<prop::Opacity>(fallback); }
	[[nodiscard]] float Gloss(float fallback) const noexcept { return Value<prop::Gloss>(fallback); }
	[[nodiscard]] float Glow(float fallback) const noexcept { return Value<prop::Glow>(fallback); }
	[[nodiscard]] sdl3::FColor GlowColor(sdl3::FColor fallback) const noexcept { return Value<prop::GlowColor>(fallback); }

	/// Pour le dégradé : renvoie Some si résolu, NONE sinon (le renderer décide).
	[[nodiscard]] Option<sdl3::FColor> BgGradientOpt() const noexcept;
};

/// Récupère la vue résolue pour une entité.
[[nodiscard]] ResolvedStyle GetResolved(ecs::ArchetypeRegistry &world, ecs::Entity e);

// ============================================================================
// Registre dynamique nom↔propriété — pour l'API par chemin
// ("cible.propriete", cf. ui::Ui::setStyleProperty)
// ============================================================================

namespace detail {

using BoolSetter = void (*)(UiStyle &, bool);
using ColorSetter = void (*)(UiStyle &, sdl3::FColor);
using FloatSetter = void (*)(UiStyle &, float);
using AlignSetter = void (*)(UiStyle &, TextAlign);
using SidesSetter = void (*)(UiStyle &, math::Sides);
using CornersSetter = void (*)(UiStyle &, math::Corners);

[[nodiscard]] const std::unordered_map<String, BoolSetter> &BoolPropRegistry();

[[nodiscard]] const std::unordered_map<String, ColorSetter> &ColorPropRegistry();

[[nodiscard]] const std::unordered_map<String, FloatSetter> &FloatPropRegistry();

[[nodiscard]] const std::unordered_map<String, AlignSetter> &AlignPropRegistry();

[[nodiscard]] const std::unordered_map<String, SidesSetter> &SidesPropRegistry();

[[nodiscard]] const std::unordered_map<String, CornersSetter> &CornersPropRegistry();

} // namespace detail

/// Pose UNE propriété de style par son nom CSS-like (ex: "background-color")
/// sur un UiStyle existant. Utilisé par Ui::setStyleProperty (chemin
/// "cible.propriete") — retourne false (no-op silencieux) si le nom est
/// inconnu pour ce type de valeur : permet des scripts/présets tolérants
/// aux fautes de frappe sans planter l'appli, cohérent avec le reste de
/// l'API ui:: qui préfère les Option/bool aux exceptions.
bool SetStyleProp(UiStyle &style, const String &name, bool value);
bool SetStyleProp(UiStyle &style, const String &name, sdl3::FColor value);
bool SetStyleProp(UiStyle &style, const String &name, float value);
bool SetStyleProp(UiStyle &style, const String &name, TextAlign value);
bool SetStyleProp(UiStyle &style, const String &name, math::Sides value);
bool SetStyleProp(UiStyle &style, const String &name, math::Corners value);

// ============================================================================
// Fonctions libres — manipulation dynamique des styles
// ============================================================================

/// Pose ou remplace le style inline d'une entité et marque dirty (avec sous-arbre).
void SetInlineStyle(ecs::ArchetypeRegistry &world, ecs::Entity e, UiStyle style);

/// Modifie le style inline via un callback (évite de reconstruire tout le UiStyle).
void EditInlineStyle(ecs::ArchetypeRegistry &world, ecs::Entity e, std::function<void(UiStyle &)> fn);

/// Ajoute une classe à l'entité (idempotent).
void AddClass(ecs::ArchetypeRegistry &world, ecs::Entity e, String name);

/// Retire une classe de l'entité.
void RemoveClass(ecs::ArchetypeRegistry &world, ecs::Entity e, const String &name);

/// Bascule une classe (ajoute si absente, retire si présente).
/// Retourne true si la classe est présente après le toggle.
bool ToggleClass(ecs::ArchetypeRegistry &world, ecs::Entity e, String name);

/// Vrai si l'entité porte cette classe.
[[nodiscard]] bool HasClass(ecs::ArchetypeRegistry &world, ecs::Entity e, const String &name);

/// Remplace toute la liste de classes.
void SetClasses(ecs::ArchetypeRegistry &world, ecs::Entity e, std::vector<String> names);

/// Supprime le style inline d'une entité (revenir aux classes/héritage/défaut).
void RemoveInlineStyle(ecs::ArchetypeRegistry &world, ecs::Entity e);

/// Marque une entité et tout son sous-arbre comme dirty.
void MarkSubtreeDirty(ecs::ArchetypeRegistry &world, ecs::Entity e);

/// Marque une seule entité comme dirty (sans le sous-arbre ;
/// utile si seule la valeur locale change et l'héritage n'est pas impacté).
void MarkStyleDirty(ecs::ArchetypeRegistry &world, ecs::Entity e);

// ============================================================================
// Helpers pour le LayoutSystem — taille de police effective
// ============================================================================

/// Renvoie la taille de police effective d'un widget : UiComputedStyle
/// (résolu depuis "root"/classes/héritage/style inline) > themeDefault.
[[nodiscard]] float GetEffectiveFontSize(ecs::ArchetypeRegistry &world, ecs::Entity e, float themeDefault = 14.f);

/// Renvoie le padding effectif : UiComputedStyle > UiFlow.padding > fallback.
[[nodiscard]] math::Sides GetEffectivePadding(ecs::ArchetypeRegistry &world, ecs::Entity e, math::Sides fallback = math::Sides{0.f});

/// Renvoie le gap effectif : UiComputedStyle > UiFlow.gap > fallback.
[[nodiscard]] float GetEffectiveGap(ecs::ArchetypeRegistry &world, ecs::Entity e, float fallback = 6.f);

/// Renvoie l'opacité effective : UiComputedStyle > 1.f.
[[nodiscard]] float GetEffectiveOpacity(ecs::ArchetypeRegistry &world, ecs::Entity e);

} // namespace ui
