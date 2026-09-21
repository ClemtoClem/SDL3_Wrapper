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
	void Merge(const UiStyle &other) {
		for (const auto &[k, v] : other.entries)
			entries[k] = v;
	}

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
	[[nodiscard]] static UiStyle Glass(sdl3::FColor base, float gloss = 0.35f, float glow = 0.45f, float radius = 8.f) {
		auto lighten = [](float v) { return sdl3::Clamp(v + 45.f / 255.f, 0.f, 1.f); };
		sdl3::FColor top(lighten(base.r), lighten(base.g), lighten(base.b), base.a);
		UiStyle s;
		s.SetBg(top)
			.SetBgGradient(base)
			.SetBorderColor(sdl3::FColor::UI_WHITE_SOFT())
			.SetBordersWidth(1.f)
			.SetBordersRadius(radius)
			.SetGloss(gloss)
			.SetGlow(glow)
			.SetGlowColor(sdl3::FColor::UI_WHITE_STRONG());
		return s;
	}

	/// Panneau vitré translucide (fond de carte/fenêtre).
	[[nodiscard]] static UiStyle GlassPanel(sdl3::FColor base = sdl3::FColor{70/255.f, 120/255.f, 190/255.f, 165/255.f}) {
		return Glass(base, 0.28f, 0.35f, 10.f);
	}

	/// Bouton vitré (reflet plus marqué, coins plus arrondis).
	[[nodiscard]] static UiStyle GlassButton(sdl3::FColor base = sdl3::FColor{60/255.f, 140/255.f, 220/255.f, 220/255.f}) {
		return Glass(base, 0.45f, 0.5f, 6.f);
	}

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
	UiStyleSheet &Define(String name, UiStyle style) {
		classes[String(name.c_str())] = std::move(style);
		return *this;
	}

	/// Supprime une classe.
	void Undefine(const String &name) { classes.erase(String(name.c_str())); }

	/// Cherche une classe. Retourne NONE si inexistante.
	[[nodiscard]] Option<const UiStyle *> Lookup(const String &name) const {
		auto it = classes.find(String(name.c_str()));
		if (it != classes.end())
			return Some(&it->second);
		return NONE;
	}

	/// Vrai si la classe existe.
	[[nodiscard]] bool Has(const String &name) const { return classes.count(String(name.c_str())) > 0; }

	/// Supprime toutes les classes.
	void Clear() { classes.clear(); }

	/// Marque toutes les entités portant cette classe comme dirty
	/// (appelé après une redéfinition de classe).
	void DirtyClassUsers(ecs::ArchetypeRegistry &world, const String &name) {
		world.Query<UiClassList>([&](ecs::Entity e, UiClassList &cls) {
			for (const auto &n : cls.names) {
				if (n == name) {
					if (!world.HasComponent<UiStyleDirty>(e))
						world.AddComponent(e, UiStyleDirty{});
					MarkSubtreeDirty(world, e);
					break;
				}
			}
		});
	}

	/// Marque dirty TOUT widget stylé par une classe, quelle qu'elle soit.
	///
	/// Nécessaire pour un changement de thème à chaud (cf.
	/// `UiFactory::SetTheme`) : redéfinir les classes « root-* » ne suffit
	/// pas, encore faut-il forcer la cascade à se re-résoudre sur les
	/// widgets DÉJÀ construits — sinon la nouvelle palette ne s'applique
	/// qu'aux widgets créés ensuite. Appeler `DirtyClassUsers` une fois par
	/// classe ferait le même travail en autant de parcours complets de
	/// l'ECS ; ici, un seul suffit.
	void DirtyAllUsers(ecs::ArchetypeRegistry &world) {
		std::vector<ecs::Entity> users;
		// Collecte d'abord, mutation ensuite : AddComponent pendant un Query
		// invalide l'itération (cf. l'avertissement en tête de ecs.hpp).
		world.Query<UiClassList>([&](ecs::Entity e, UiClassList &) { users.push_back(e); });
		for (ecs::Entity e : users) {
			if (!world.HasComponent<UiStyleDirty>(e))
				world.AddComponent(e, UiStyleDirty{});
			MarkSubtreeDirty(world, e);
		}
	}

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
	void Resolve(ecs::ArchetypeRegistry &world) {
		// Collecte d'abord (retirer UiStyleDirty migre l'entité vers un
		// autre archétype ; le faire PENDANT un Query<UiStyleDirty> invalide
		// l'itération en cours — d'où la collecte préalable dans un vector).
		std::vector<ecs::Entity> dirtyEntities;
		world.Query<UiStyleDirty>([&](ecs::Entity e, UiStyleDirty &) { dirtyEntities.push_back(e); });
		if (dirtyEntities.empty())
			return;

		// Résout depuis chaque racine (entité sans UiParent). Collecte
		// D'ABORD (même raison que dirtyEntities ci-dessus) : resolveEntity()
		// appelle get_or_add_component<UiComputedStyle> pour CHAQUE entité de
		// tout le sous-arbre — une migration d'archétype par entité pas
		// encore résolue une fois, qui peut réallouer le vecteur
		// d'archétypes. L'appeler PENDANT ce Query<UiComputed> corromprait
		// son itération exactement comme pour UiStyleDirty plus haut ; ça
		// n'avait jamais crashé jusqu'ici par pure chance (pas assez
		// d'archétypes DISTINCTS dans les scènes de test précédentes pour
		// déclencher une réallocation au bon moment) — confirmé crash réel
		// sur ui_aero_basics.cpp (Phase 10), dont la variété de widgets crée
		// nettement plus d'archétypes distincts que les tests antérieurs.
		std::vector<ecs::Entity> roots;
		world.Query<UiComputed>([&](ecs::Entity e, UiComputed &) {
			if (!world.HasComponent<UiParent>(e))
				roots.push_back(e);
		});
		for (ecs::Entity e : roots)
			ResolveEntity(world, e, nullptr);

		// Nettoie tous les marqueurs dirty collectés plus haut.
		for (ecs::Entity e : dirtyEntities)
			world.RemoveComponent<UiStyleDirty>(e);
	}

private:
	void ResolveEntity(ecs::ArchetypeRegistry &world, ecs::Entity e, const UiStyle *parentResolved) {
		UiStyle result;

		// 1) Héritage depuis le parent (seules les propriétés héritables :
		// text-color, font-size, text-align, opacity, inner-zoom — comme en
		// CSS où background/border/padding n'héritent jamais par défaut).
		if (parentResolved) {
			if (auto *p = parentResolved->Get<prop::TextColor>())
				result.Set(*p);
			if (auto *p = parentResolved->Get<prop::FontSize>())
				result.Set(*p);
			if (auto *p = parentResolved->Get<prop::TextAlignProp>())
				result.Set(*p);
			if (auto *p = parentResolved->Get<prop::Opacity>())
				result.Set(*p);
			if (auto *p = parentResolved->Get<prop::InnerZoom>())
				result.Set(*p);
		}

		// 2) Classes (dans l'ordre de la liste ; la dernière gagne). La liste
		// commence toujours par "root"/"root-<type>" — cf. UiFactory::Spawn().
		if (auto classes = world.GetComponent<UiClassList>(e); classes.IsSome()) {
			for (const auto &name : classes.Unwrap()->names) {
				if (sheet) {
					if (auto s = sheet->Lookup(name); s.IsSome())
						result.Merge(*s.Unwrap());
				}
			}
		}

		// 3) Style inline (priorité maximale)
		if (auto inlineStyle = world.GetComponent<UiStyle>(e); inlineStyle.IsSome())
			result.Merge(*inlineStyle.Unwrap());

		// 4) Stocker le résultat
		auto stored = world.GetOrAddComponent<UiComputedStyle>(e);
		if (stored.IsSome())
			stored.Unwrap()->style = result;

		// 5) Propager le résultat résolu aux enfants pour l'héritage.
		//
		// Deux pièges d'invalidation ici, tous deux DÉJÀ RENCONTRÉS en vrai
		// (plantage use-after-free reproductible dès qu'une interface a assez
		// d'archétypes distincts — cf. examples/level_editor/, dont
		// l'inspecteur/outliner en crée beaucoup) :
		//
		//  a) on passait aux enfants un pointeur vers le `UiComputedStyle`
		//     STOCKÉ de cette entité. Or chaque appel récursif fait un
		//     `GetOrAddComponent<UiComputedStyle>` sur un enfant, ce qui peut
		//     faire migrer cet enfant d'archétype et RÉALLOUER le vecteur de
		//     composants où vit le nôtre : le pointeur du parent pendouille
		//     dès le premier enfant qui n'avait pas encore de style calculé.
		//     `result` est une copie locale, sur la pile de CET appel, avec
		//     exactement la même valeur — elle survit à toute la récursion.
		//
		//  b) la liste d'enfants était parcourue DIRECTEMENT dans le
		//     composant `UiChildren`, lui aussi susceptible d'être déplacé
		//     par ces mêmes migrations. On la copie donc avant d'itérer.
		std::vector<ecs::Entity> children;
		if (auto list = world.GetComponent<UiChildren>(e); list.IsSome())
			children = list.Unwrap()->list;
		for (ecs::Entity child : children)
			ResolveEntity(world, child, &result);
	}
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
	[[nodiscard]] bool Strikethrough(bool fallback = false) const noexcept {
		return Value<prop::Strikethrough>(fallback);
	}
	[[nodiscard]] bool Highlight(bool fallback = false) const noexcept { return Value<prop::Highlight>(fallback); }
	[[nodiscard]] sdl3::FColor HighlightColor(sdl3::FColor fallback) const noexcept {
		return Value<prop::HighlightTextColor>(fallback);
	}

	[[nodiscard]] sdl3::FColor Bg(sdl3::FColor fallback) const noexcept { return Value<prop::BackgroundColor>(fallback); }
	[[nodiscard]] sdl3::FColor BgHovered(sdl3::FColor fallback) const noexcept {
		return Value<prop::HoveredBackgroundColor>(fallback);
	}
	[[nodiscard]] sdl3::FColor BgPressed(sdl3::FColor fallback) const noexcept {
		return Value<prop::PressedBackgroundColor>(fallback);
	}
	[[nodiscard]] sdl3::FColor BgChecked(sdl3::FColor fallback) const noexcept {
		return Value<prop::CheckedBackgroundColor>(fallback);
	}
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
	[[nodiscard]] Option<sdl3::FColor> BgGradientOpt() const noexcept {
		if (style)
			if (const auto *p = style->Get<prop::BackgroundGradient>())
				return Some(p->value);
		return NONE;
	}
};

/// Récupère la vue résolue pour une entité.
[[nodiscard]] inline ResolvedStyle GetResolved(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (auto c = world.GetComponent<UiComputedStyle>(e); c.IsSome())
		return ResolvedStyle(&c.Unwrap()->style);
	return ResolvedStyle{};
}

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

[[nodiscard]] inline const std::unordered_map<String, BoolSetter> &BoolPropRegistry() {
	static const std::unordered_map<String, BoolSetter> TABLE = {
		{prop::Enable::kName, [](UiStyle &s, bool v) { s.Set(prop::Enable{v}); }},
		{prop::Visible::kName, [](UiStyle &s, bool v) { s.Set(prop::Visible{v}); }},
		{prop::Italic::kName, [](UiStyle &s, bool v) { s.Set(prop::Italic{v}); }},
		{prop::Bold::kName, [](UiStyle &s, bool v) { s.Set(prop::Bold{v}); }},
		{prop::Underline::kName, [](UiStyle &s, bool v) { s.Set(prop::Underline{v}); }},
		{prop::Strikethrough::kName, [](UiStyle &s, bool v) { s.Set(prop::Strikethrough{v}); }},
		{prop::Highlight::kName, [](UiStyle &s, bool v) { s.Set(prop::Highlight{v}); }},
	};
	return TABLE;
}

[[nodiscard]] inline const std::unordered_map<String, ColorSetter> &ColorPropRegistry() {
	static const std::unordered_map<String, ColorSetter> TABLE = {
		{prop::BackgroundColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::BackgroundColor{c}); }},
		{prop::BackgroundGradient::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::BackgroundGradient{c}); }},
		{prop::HoveredBackgroundColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::HoveredBackgroundColor{c}); }},
		{prop::PressedBackgroundColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::PressedBackgroundColor{c}); }},
		{prop::CheckedBackgroundColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::CheckedBackgroundColor{c}); }},
		{prop::FocusedBackgroundColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::FocusedBackgroundColor{c}); }},
		{prop::TextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::TextColor{c}); }},
		{prop::HoveredTextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::HoveredTextColor{c}); }},
		{prop::PressedTextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::PressedTextColor{c}); }},
		{prop::CheckedTextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::CheckedTextColor{c}); }},
		{prop::FocusedTextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::FocusedTextColor{c}); }},
		{prop::BorderColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::BorderColor{c}); }},
		{prop::HoveredBorderColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::HoveredBorderColor{c}); }},
		{prop::PressedBorderColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::PressedBorderColor{c}); }},
		{prop::CheckedBorderColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::CheckedBorderColor{c}); }},
		{prop::FocusedBorderColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::FocusedBorderColor{c}); }},
		{prop::GlowColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::GlowColor{c}); }},
		{prop::HighlightTextColor::kName, [](UiStyle &s, sdl3::FColor c) { s.Set(prop::HighlightTextColor{c}); }},
	};
	return TABLE;
}

[[nodiscard]] inline const std::unordered_map<String, FloatSetter> &FloatPropRegistry() {
	static const std::unordered_map<String, FloatSetter> TABLE = {
		{prop::FontSize::kName, [](UiStyle &s, float v) { s.Set(prop::FontSize{v}); }},
		{prop::InnerZoom::kName, [](UiStyle &s, float v) { s.Set(prop::InnerZoom{v}); }},
		{prop::Gap::kName, [](UiStyle &s, float v) { s.Set(prop::Gap{v}); }},
		{prop::Opacity::kName, [](UiStyle &s, float v) { s.Set(prop::Opacity{v}); }},
		{prop::Gloss::kName, [](UiStyle &s, float v) { s.Set(prop::Gloss{v}); }},
		{prop::Glow::kName, [](UiStyle &s, float v) { s.Set(prop::Glow{v}); }},
	};
	return TABLE;
}

[[nodiscard]] inline const std::unordered_map<String, AlignSetter> &AlignPropRegistry() {
	static const std::unordered_map<String, AlignSetter> TABLE = {
		{prop::TextAlignProp::kName, [](UiStyle &s, TextAlign a) { s.Set(prop::TextAlignProp{a}); }},
	};
	return TABLE;
}

[[nodiscard]] inline const std::unordered_map<String, SidesSetter> &SidesPropRegistry() {
	static const std::unordered_map<String, SidesSetter> TABLE = {
		{prop::Padding::kName, [](UiStyle &s, math::Sides v) { s.Set(prop::Padding{v}); }},
		{prop::Margin::kName, [](UiStyle &s, math::Sides v) { s.Set(prop::Margin{v}); }},
		{prop::BordersWidth::kName, [](UiStyle &s, math::Sides v) { s.Set(prop::BordersWidth{v}); }},
	};
	return TABLE;
}

[[nodiscard]] inline const std::unordered_map<String, CornersSetter> &CornersPropRegistry() {
	static const std::unordered_map<String, CornersSetter> TABLE = {
		{prop::BordersRadius::kName, [](UiStyle &s, math::Corners v) { s.Set(prop::BordersRadius{v}); }},
	};
	return TABLE;
}

} // namespace detail

/// Pose UNE propriété de style par son nom CSS-like (ex: "background-color")
/// sur un UiStyle existant. Utilisé par Ui::setStyleProperty (chemin
/// "cible.propriete") — retourne false (no-op silencieux) si le nom est
/// inconnu pour ce type de valeur : permet des scripts/présets tolérants
/// aux fautes de frappe sans planter l'appli, cohérent avec le reste de
/// l'API ui:: qui préfère les Option/bool aux exceptions.
inline bool SetStyleProp(UiStyle &style, const String &name, bool value) {
	auto &table = detail::BoolPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}
inline bool SetStyleProp(UiStyle &style, const String &name, sdl3::FColor value) {
	auto &table = detail::ColorPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}
inline bool SetStyleProp(UiStyle &style, const String &name, float value) {
	auto &table = detail::FloatPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}
inline bool SetStyleProp(UiStyle &style, const String &name, TextAlign value) {
	auto &table = detail::AlignPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}
inline bool SetStyleProp(UiStyle &style, const String &name, math::Sides value) {
	auto &table = detail::SidesPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}
inline bool SetStyleProp(UiStyle &style, const String &name, math::Corners value) {
	auto &table = detail::CornersPropRegistry();
	auto it = table.find(String(name.c_str()));
	if (it == table.end())
		return false;
	it->second(style, value);
	return true;
}

// ============================================================================
// Fonctions libres — manipulation dynamique des styles
// ============================================================================

/// Pose ou remplace le style inline d'une entité et marque dirty (avec sous-arbre).
inline void SetInlineStyle(ecs::ArchetypeRegistry &world, ecs::Entity e, UiStyle style) {
	// Retirer l'ancien s'il existe pour éviter la duplication dans l'archétype.
	world.RemoveComponent<UiStyle>(e);
	world.AddComponent(e, std::move(style));
	if (!world.HasComponent<UiStyleDirty>(e))
		world.AddComponent(e, UiStyleDirty{});
	MarkSubtreeDirty(world, e);
}

/// Modifie le style inline via un callback (évite de reconstruire tout le UiStyle).
inline void EditInlineStyle(ecs::ArchetypeRegistry &world, ecs::Entity e, std::function<void(UiStyle &)> fn) {
	auto existing = world.GetComponent<UiStyle>(e);
	UiStyle s;
	if (existing.IsSome())
		s = *existing.Unwrap();
	fn(s);
	SetInlineStyle(world, e, std::move(s));
}

/// Ajoute une classe à l'entité (idempotent).
inline void AddClass(ecs::ArchetypeRegistry &world, ecs::Entity e, String name) {
	auto cls = world.GetOrAddComponent<UiClassList>(e);
	if (cls.IsNone())
		return;
	auto &names = cls.Unwrap()->names;
	for (const auto &n : names)
		if (n == name)
			return; // déjà présente
	names.push_back(std::move(name));
	if (!world.HasComponent<UiStyleDirty>(e))
		world.AddComponent(e, UiStyleDirty{});
	MarkSubtreeDirty(world, e);
}

/// Retire une classe de l'entité.
inline void RemoveClass(ecs::ArchetypeRegistry &world, ecs::Entity e, const String &name) {
	auto cls = world.GetComponent<UiClassList>(e);
	if (cls.IsNone())
		return;
	auto &names = cls.Unwrap()->names;
	auto it = std::find_if(names.begin(), names.end(), [&](const String &s) { return s == name; });
	if (it != names.end()) {
		names.erase(it);
		if (names.empty())
			world.RemoveComponent<UiClassList>(e);
		if (!world.HasComponent<UiStyleDirty>(e))
			world.AddComponent(e, UiStyleDirty{});
		MarkSubtreeDirty(world, e);
	}
}

/// Bascule une classe (ajoute si absente, retire si présente).
/// Retourne true si la classe est présente après le toggle.
inline bool ToggleClass(ecs::ArchetypeRegistry &world, ecs::Entity e, String name) {
	bool present = HasClass(world, e, name);
	if (present)
		RemoveClass(world, e, name);
	else
		AddClass(world, e, std::move(name));
	return !present;
}

/// Vrai si l'entité porte cette classe.
[[nodiscard]] inline bool HasClass(ecs::ArchetypeRegistry &world, ecs::Entity e, const String &name) {
	auto cls = world.GetComponent<UiClassList>(e);
	if (cls.IsNone())
		return false;
	for (const auto &n : cls.Unwrap()->names)
		if (n == name)
			return true;
	return false;
}

/// Remplace toute la liste de classes.
inline void SetClasses(ecs::ArchetypeRegistry &world, ecs::Entity e, std::vector<String> names) {
	if (names.empty()) {
		world.RemoveComponent<UiClassList>(e);
	} else {
		world.RemoveComponent<UiClassList>(e);
		world.AddComponent(e, UiClassList{std::move(names)});
	}
	if (!world.HasComponent<UiStyleDirty>(e))
		world.AddComponent(e, UiStyleDirty{});
	MarkSubtreeDirty(world, e);
}

/// Supprime le style inline d'une entité (revenir aux classes/héritage/défaut).
inline void RemoveInlineStyle(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (world.HasComponent<UiStyle>(e)) {
		world.RemoveComponent<UiStyle>(e);
		if (!world.HasComponent<UiStyleDirty>(e))
			world.AddComponent(e, UiStyleDirty{});
		MarkSubtreeDirty(world, e);
	}
}

/// Marque une entité et tout son sous-arbre comme dirty.
inline void MarkSubtreeDirty(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (auto children = world.GetComponent<UiChildren>(e); children.IsSome()) {
		for (ecs::Entity c : children.Unwrap()->list) {
			if (!world.HasComponent<UiStyleDirty>(c)) {
				world.AddComponent(c, UiStyleDirty{});
				MarkSubtreeDirty(world, c);
			}
		}
	}
}

/// Marque une seule entité comme dirty (sans le sous-arbre ;
/// utile si seule la valeur locale change et l'héritage n'est pas impacté).
inline void MarkStyleDirty(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (!world.HasComponent<UiStyleDirty>(e))
		world.AddComponent(e, UiStyleDirty{});
}

// ============================================================================
// Helpers pour le LayoutSystem — taille de police effective
// ============================================================================

/// Renvoie la taille de police effective d'un widget : UiComputedStyle
/// (résolu depuis "root"/classes/héritage/style inline) > themeDefault.
[[nodiscard]] inline float GetEffectiveFontSize(ecs::ArchetypeRegistry &world, ecs::Entity e, float themeDefault = 14.f) {
	return GetResolved(world, e).FontSize(themeDefault);
}

/// Renvoie le padding effectif : UiComputedStyle > UiFlow.padding > fallback.
[[nodiscard]] inline math::Sides GetEffectivePadding(ecs::ArchetypeRegistry &world, ecs::Entity e, math::Sides fallback = math::Sides{0.f}) {
	math::Sides flowPad = fallback;
	if (auto f = world.GetComponent<UiFlow>(e); f.IsSome())
		flowPad = f.Unwrap()->padding;
	return GetResolved(world, e).Padding(flowPad);
}

/// Renvoie le gap effectif : UiComputedStyle > UiFlow.gap > fallback.
[[nodiscard]] inline float GetEffectiveGap(ecs::ArchetypeRegistry &world, ecs::Entity e, float fallback = 6.f) {
	float flowGap = fallback;
	if (auto f = world.GetComponent<UiFlow>(e); f.IsSome())
		flowGap = f.Unwrap()->gap;
	return GetResolved(world, e).Gap(flowGap);
}

/// Renvoie l'opacité effective : UiComputedStyle > 1.f.
[[nodiscard]] inline float GetEffectiveOpacity(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	return GetResolved(world, e).Opacity(1.f);
}

} // namespace ui
