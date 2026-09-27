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
	float resizeBorder = 6.f;    ///< épaisseur de la bande de redimensionnement sur les 4 bords
	float w = 0.f, h = 0.f;      ///< taille de fenêtre courante (coins/bords)
	bool resizable = true;
};

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
	HitTestCache cache;
	uint64_t lastLayoutPass = 0;

	void RebuildIfNeeded();

	[[nodiscard]] SDL_HitTestResult HitTest(const SDL_Point &p) const;
};

// ============================================================================
// Barre de titre — icône + titre + réduire/agrandir/fermer
// ============================================================================

/// Entités d'une barre de titre construite par UiFactory::TitleBar() — à
/// passer telles quelles à WindowChrome::Attach() (buttonEntities =
/// {minimizeBtn, maximizeBtn, closeBtn}).
struct TitleBarWidgets {
	ecs::Entity root{};
	ecs::Entity minimizeBtn{};
	ecs::Entity maximizeBtn{};
	ecs::Entity closeBtn{};
};

/// Construit une barre de titre standard (titre + réduire/agrandir/fermer),
/// prête pour WindowChrome::Attach(). Réduire/agrandir appellent directement
/// `window.Minimize()`/`maximize()`/`restore()` (bascule agrandi/restauré
/// suivie via un état local capturé par les callbacks) ; fermer appelle
/// `onClose` si fourni, sinon `window.Hide()`. Glyphes texte simples (—/□/×)
/// plutôt que MaterialIcons : rendus par la police déjà chargée pour tout le
/// reste du texte, sans exiger que l'appelant ait enregistré une police
/// d'icônes avant de construire la barre (cf. UiFactory::Icon<E>() si une
/// vraie icône est préférée — remplacer le bouton après coup reste possible,
/// ce sont des UiButton ordinaires).
[[nodiscard]] TitleBarWidgets TitleBar(UiFactory &f, sdl3::Window &window, String title,
											  std::function<void()> onClose = nullptr);

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
