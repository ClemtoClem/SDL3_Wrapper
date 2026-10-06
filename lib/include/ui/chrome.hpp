#pragma once
/**
 * ui::chrome — chrome de fenêtre borderless (Phase 9) : barre de titre
 * dessinée par des widgets ui:: (icône/titre/réduire/agrandir/fermer, zone
 * de drag) plutôt que par la décoration système. Deux variantes, cf.
 * Décisions d'architecture du plan (mossy-forging-fog.md) :
 *
 *   - WindowChrome : hit-test AU NIVEAU OS (sdl3::Window::SetHitTest) — pour
 *     une vraie fenêtre SDL3 borderless (examples/ui_aero_*.cpp).
 *   - PanelChrome  : drag/resize gérés à l'ECS (cf. interaction.hpp
 *     UiDraggable), PAS d'OS hit-test — pour un "bureau simulé" (Phase 10)
 *     où plusieurs panneaux flottants vivent DANS une seule fenêtre SDL3.
 */
#include <vector>

#include "factory.hpp"

namespace ui {

// ============================================================================
// WindowChrome — barre de titre OS-level (fenêtre borderless réelle)
// ============================================================================

/// Cache POD reconstruit UNIQUEMENT quand layout.RunIfNeeded() a
/// effectivement tourné depuis le dernier appel (même garde dirty-flag que
/// LayoutSystem lui-même, pas de second mécanisme — cf. Décision
/// d'architecture #2 du plan). Le callback passé à Window::SetHitTest ne
/// touche JAMAIS l'ECS : il ne fait qu'un test point-dans-rect sur CE cache,
/// capturé par pointeur — sûr à appeler depuis n'importe quel thread/
/// contexte où SDL invoque le callback de hit-test.
struct HitTestCache {
	sdl3::FRect titleBar{};            ///< bande draggable (hors boutons)
	std::vector<sdl3::FRect> excluded; ///< boutons de la barre de titre — NORMAL pour rester cliquables
	std::vector<sdl3::FRect> grips;    ///< poignées de redimensionnement (coin bas-droit)
	float resizeBorder = 6.f;    ///< épaisseur de la bande de redimensionnement sur les 4 bords
	float w = 0.f, h = 0.f;      ///< taille de fenêtre courante (coins/bords)
	bool resizable = true;
};

struct TitleBarWidgets;
struct StatusBarWidgets;

class WindowChrome {
public:
	/// `titleBarEntity` : l'entité racine de la barre de titre (typiquement
	/// un `.Row()` en haut de la fenêtre, cf. UiFactory::TitleBar()) — son
	/// UiComputed.screen après layout définit la bande draggable.
	/// `buttonEntities` : réduire/agrandir/fermer (ou tout autre widget de la
	/// barre) — exclus du drag pour rester cliquables normalement (coins
	/// testés avant bords, bords avant bande draggable — cf. hitTest()).
	void Attach(sdl3::Window &window, ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity titleBarEntity,
				std::vector<ecs::Entity> buttonEntities = {}, bool resizable = true);

	/// Poignée de redimensionnement visible (ex. une icône dans le coin
	/// bas-droit) : la saisir redimensionne la fenêtre par le coin bas-droit,
	/// au-delà de la fine bande `resizeBorder` des bords. Plusieurs appels
	/// ajoutent plusieurs poignées. Sans effet si la fenêtre n'est pas
	/// redimensionnable.
	void AddResizeGrip(ecs::Entity grip);

	/// Raccourci : barre de titre (bande draggable, boutons exclus) et, si
	/// fournie, poignée de la barre d'état.
	void Attach(sdl3::Window &window, ecs::ArchetypeRegistry &world, LayoutSystem &layout,
				const TitleBarWidgets &titleBar, const StatusBarWidgets *statusBar = nullptr,
				bool resizable = true);

	/// Retire le hit-test (fenêtre redevient un widget normal côté OS —
	/// utile pour repasser en fenêtré/plein écran décoré, si jamais).
	void Detach();

	/// À appeler une fois par frame (avant/après Ui::Render(), l'ordre
	/// n'importe pas) — ne reconstruit le cache que si layout.RunIfNeeded()
	/// a effectivement tourné depuis le dernier appel.
	void Update() { RebuildIfNeeded(); }

private:
	sdl3::Window *m_window = nullptr;
	ecs::ArchetypeRegistry *m_world = nullptr;
	LayoutSystem *m_layout = nullptr;
	ecs::Entity m_titleBarEntity{};
	std::vector<ecs::Entity> m_buttonEntities;
	std::vector<ecs::Entity> m_gripEntities;
	HitTestCache cache;
	uint64_t lastLayoutPass = 0;

	void RebuildIfNeeded();

	[[nodiscard]] SDL_HitTestResult HitTest(const SDL_Point &p) const;
};

// ============================================================================
// Barre de titre — icône + titre + poignée + réduire/agrandir/fermer
// ============================================================================

/// Bouton dont le contenu est une icône MaterialIcons centrée, ou le texte
/// `fallback` si la police d'icônes n'est pas disponible (`icons` faux). Le
/// glyphe est un enfant TRANSPARENT AU POINTEUR (PointerThrough) : le clic,
/// le survol et l'infobulle vont au bouton. `iconOut` reçoit l'entité du
/// glyphe (vide sans icône) — pour en changer plus tard (cf.
/// TitleBarWidgets::Update).
ecs::Entity SpawnIconButton(UiFactory &f, ecs::Entity parent, MaterialIcons glyph, const String &fallback, bool icons,
							std::function<void()> onClick, const String &tooltip = "", float width = 40.f,
							float height = 28.f, ecs::Entity *iconOut = nullptr);

/// Ouvre la police MaterialIcons (assets/fonts/ du répertoire courant, puis
/// à côté de l'exécutable : `<base>/assets/fonts/` et `<base>/../../assets/
/// fonts/`) ; NONE si introuvable. À enregistrer ensuite via
/// `RenderSystem::RegisterFont(Glyphs::FontFamily<MaterialIcons>(), font)`
/// — la police doit vivre aussi longtemps que l'interface.
[[nodiscard]] Option<sdl3::Font> OpenMaterialIconFont(float size = 22.f);

struct TitleBarOptions {
	String title;
	/// Icône d'application avant le titre (NONE : aucune).
	Option<MaterialIcons> appIcon = NONE;
	/// Poignée « déplacer » après le titre : purement indicative, elle fait
	/// partie de la bande draggable comme tout point vide de la barre.
	bool moveHandle = true;
	bool minimizeButton = true;
	bool maximizeButton = true;
	/// Police MaterialIcons enregistrée (cf. RenderSystem::HasFont) ; sinon
	/// les boutons retombent sur des glyphes texte (—, □, ×, ✥).
	bool icons = true;
	/// Fermer : `onClose` si fourni, sinon `window.Hide()`.
	std::function<void()> onClose;
	float height = 36.f;
	float titleSize = 16.f;
	bool boldTitle = true;
	/// false : aucun fond (le verre d'une fenêtre Aero reste visible).
	bool fillBackground = true;
	/// Fond (alpha nul : celui du thème, `panelBg` opacifié).
	sdl3::FColor background{};
};

/// Entités d'une barre de titre construite par TitleBar() — à passer à
/// WindowChrome::Attach() (`Buttons()` comme buttonEntities).
struct TitleBarWidgets {
	ecs::Entity root{};
	ecs::Entity appIcon{};
	ecs::Entity title{};
	ecs::Entity moveHandle{};
	ecs::Entity minimizeBtn{};
	ecs::Entity maximizeBtn{};
	ecs::Entity maximizeIcon{}; ///< glyphe du bouton agrandir (vide sans icônes)
	ecs::Entity closeBtn{};

	/// Boutons cliquables, à exclure de la bande draggable.
	[[nodiscard]] std::vector<ecs::Entity> Buttons() const;

	/// À appeler à chaque image : l'icône du bouton agrandir suit l'état RÉEL
	/// de la fenêtre (agrandie → « restaurer »), qui peut aussi changer par
	/// le gestionnaire de fenêtres (double-clic, raccourci, bord d'écran).
	void Update(ecs::ArchetypeRegistry &world, const sdl3::Window &window) const;

	/// Change le texte du titre.
	void SetTitle(ecs::ArchetypeRegistry &world, const String &text) const;
};

/// Construit la barre de titre (pleine largeur, hauteur fixe) sous `parent`
/// (vide : à la racine). Agrandir bascule selon `window.IsMaximized()` —
/// l'état réel, pas un état local qui se désynchronise dès que le
/// gestionnaire de fenêtres agrandit lui-même.
[[nodiscard]] TitleBarWidgets TitleBar(UiFactory &f, sdl3::Window &window, TitleBarOptions options,
									   ecs::Entity parent = ecs::Entity{});

/// Forme historique : titre seul, glyphes texte (—/□/×), sans poignée.
[[nodiscard]] TitleBarWidgets TitleBar(UiFactory &f, sdl3::Window &window, String title,
									   std::function<void()> onClose = nullptr);

// ============================================================================
// Barre d'état — texte + poignée de redimensionnement
// ============================================================================

struct StatusBarOptions {
	String text;
	bool resizeGrip = true; ///< poignée ↘ dans le coin bas-droit
	bool icons = true;		///< glyphe MaterialIcons SOUTH_EAST, sinon ⇲
	float height = 24.f;
};

struct StatusBarWidgets {
	ecs::Entity root{};
	ecs::Entity label{};
	ecs::Entity grip{}; ///< à passer à WindowChrome::AddResizeGrip()

	/// Change le message (et sa couleur ; alpha nul : `muted` du thème).
	void SetText(ecs::ArchetypeRegistry &world, const String &text, sdl3::FColor color = {}) const;
};

[[nodiscard]] StatusBarWidgets StatusBar(UiFactory &f, StatusBarOptions options, ecs::Entity parent = ecs::Entity{});

// ============================================================================
// PanelChrome — panneau flottant "bureau simulé" (Phase 9/10) : drag/resize
// gérés à l'ECS (cf. interaction.hpp UiDraggable), PAS d'OS hit-test.
// ============================================================================

class PanelChrome {
public:
	/// `panelEntity` doit être positionné en `.Absolute()` avec une taille
	/// PIXEL explicite (pas Auto/Grow, sans quoi le redimensionnement
	/// n'aurait aucun effet visible) ; `titleBarEntity` est la zone qui
	/// déplace le panneau au glisser (typiquement son en-tête) ;
	/// `resizeGripEntity` (optionnel, ex: un petit widget en coin
	/// bas-droit) redimensionne `panelEntity` au glisser. Attache
	/// directement les composants UiDraggable nécessaires — pas de
	/// WidgetBuilder ici, les entités existent déjà.
	static void Attach(ecs::ArchetypeRegistry &world, ecs::Entity panelEntity, ecs::Entity titleBarEntity,
					   ecs::Entity resizeGripEntity = ecs::Entity{}, float minWidth = 160.f, float minHeight = 100.f);
};

} // namespace ui
