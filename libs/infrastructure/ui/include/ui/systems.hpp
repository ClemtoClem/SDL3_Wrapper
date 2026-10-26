#pragma once
/**
 * ui::systems — les systèmes qui rendent l'interface dynamique.
 *
 * Ils parcourent les archétypes UI du monde ECS :
 *
 *   ui_input     (PRE_UPDATE)  évènements SDL en file → survol, focus, gestes,
 *                              liaisons OnBind, comportements des briques
 *                              (Checkable, Range, Selection, PopupState…) ;
 *   ui_animation (UPDATE)      infobulles, appuis longs, menus au survol,
 *                              interrupteurs, indicateurs, défilement de texte ;
 *   ui_style     (POST_UPDATE) cascade CSS des widgets à restyler ;
 *   ui_layout    (POST_UPDATE) mise en page (flexbox, grille, pile, positionné) ;
 *   ui_render    (POST_UPDATE) dessin 2D via le Painter (sdl3::Renderer) et
 *                              vues 3D (render3d::Canvas).
 *
 * @code{.cpp}
 * ecs::Schedule schedule;
 * schedule.Add(ecs::Stage::PRE_UPDATE, "ui_input", ui::UiInputSystem);
 * schedule.Add(ecs::Stage::POST_UPDATE, "ui_render", ui::UiRenderSystem);
 * // ou, d'un coup : ui::Install(schedule);
 * @endcode
 *
 * Chaque système existe aussi en fonction directe (StyleSystem::Run…) pour
 * une boucle sans Schedule (cf. ui::Ui::Update / Render).
 */
#include <ecs/schedule.hpp>

#include "context.hpp"
#include "widget.hpp"

namespace ui {

// ============================================================================
// Systèmes
// ============================================================================

class StyleSystem {
public:
	/// Résout la cascade pour les widgets marqués (ou tous).
	static void Run(ecs::ArchetypeRegistry &world, Context &ctx);
};

class LayoutSystem {
public:
	/// Passe de mise en page si nécessaire (marquée, ou fenêtre redimensionnée).
	static bool Run(ecs::ArchetypeRegistry &world, Context &ctx, bool force = false);
};

class InputSystem {
public:
	/// Traite un évènement ; vrai s'il est consommé par l'interface (frappe
	/// dans le champ qui a le focus, clic sur un widget, Échap d'une modale…)
	/// — le reste de l'application doit alors l'ignorer.
	static bool HandleEvent(ecs::ArchetypeRegistry &world, Context &ctx, const sdl3::Event &ev);
};

class AnimationSystem {
public:
	static void Run(ecs::ArchetypeRegistry &world, Context &ctx, float dt);
};

class RenderSystem {
public:
	/// Dessine tout l'arbre (le renderer doit être prêt : entre Clear et Present).
	static void Run(ecs::ArchetypeRegistry &world, Context &ctx);
	/// Dessine un sous-arbre (effets shader, captures).
	static void DrawSubtree(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e);
};

// ============================================================================
// Fonctions pour ecs::Schedule
// ============================================================================

void UiInputSystem(ecs::SystemContext &ctx);
void UiAnimationSystem(ecs::SystemContext &ctx);
void UiStyleSystem(ecs::SystemContext &ctx);
void UiLayoutSystem(ecs::SystemContext &ctx);
void UiRenderSystem(ecs::SystemContext &ctx);

/// Ajoute les cinq systèmes (rendu compris si `withRender`).
void Install(ecs::Schedule &schedule, bool withRender = true);

/// Met un évènement en file pour `ui_input` (alternative à Ui::HandleEvent
/// pour une boucle entièrement pilotée par le Schedule).
void PushEvent(ecs::ArchetypeRegistry &world, const sdl3::Event &ev);

// ============================================================================
// Extensions (graphiques, node-graph, vues 3D, effets)
// ============================================================================

void RegisterExtension(Context &ctx, Extension ext);

// ============================================================================
// Popups, focus, sélecteurs
// ============================================================================

/// Ouvre un popup (racine portant `PopupState`) ancré sur `anchor` (invalide : le parent).
void OpenPopup(ecs::ArchetypeRegistry &world, ecs::Entity popup, ecs::Entity anchor = ecs::Entity{});
void OpenPopupAt(ecs::ArchetypeRegistry &world, ecs::Entity popup, sdl3::FPoint at);
void ClosePopup(ecs::ArchetypeRegistry &world, ecs::Entity popup);
/// Ferme `popup` et les popups ouverts après lui (sous-menus).
void ClosePopupChain(ecs::ArchetypeRegistry &world, ecs::Entity popup);
void CloseAllPopups(ecs::ArchetypeRegistry &world);
[[nodiscard]] bool IsPopupOpen(const ecs::ArchetypeRegistry &world, ecs::Entity popup);
/// PopupState ouvert le plus récent qui contient `e` (entité invalide sinon).
[[nodiscard]] ecs::Entity PopupOf(const ecs::ArchetypeRegistry &world, ecs::Entity e);
[[nodiscard]] bool HasOpenModal(const ecs::ArchetypeRegistry &world);

/// Donne le focus clavier (`visible` : obtenu au clavier / à la manette).
void SetFocus(ecs::ArchetypeRegistry &world, ecs::Entity e, bool visible = false);
void ClearFocus(ecs::ArchetypeRegistry &world);
[[nodiscard]] ecs::Entity FocusedWidget(const ecs::ArchetypeRegistry &world);

/// Vrai si `e` correspond au sélecteur (ancêtres compris pour les combinateurs).
[[nodiscard]] bool MatchesSelector(const ecs::ArchetypeRegistry &world, ecs::Entity e, const Selector &selector,
								   ecs::Entity scopeOwner = ecs::Entity{});

/// Widget le plus en avant sous `p` (dernière passe de mise en page).
[[nodiscard]] ecs::Entity HitTest(const ecs::ArchetypeRegistry &world, sdl3::FPoint p);

/// Glisser-déposer de charge en cours (DragSource → DropTarget), pour un
/// retour visuel propre à l'application (indicateur d'insertion…).
struct PayloadDrag {
	ecs::Entity source;	 ///< widget portant la DragSource
	ecs::Entity hovered; ///< DropTarget compatible survolée (invalide : aucune)
	sdl3::FPoint pointer;
};
[[nodiscard]] Option<PayloadDrag> ActivePayloadDrag(const ecs::ArchetypeRegistry &world);

/// Applique à `e` les changements d'un `Check` (radio : décoche le groupe)
/// et émet Toggle si `notify`.
void SetCheckState(ecs::ArchetypeRegistry &world, ecs::Entity e, bool on, bool notify);
/// Modifie un `Range` (valeur arrondie au pas, bornée), émet Change si `notify`.
void SetRangeValue(ecs::ArchetypeRegistry &world, ecs::Entity e, float value, bool notify);
/// Sélectionne `index` dans le `Selection` de `e`, émet Select si `notify`.
void SetSelection(ecs::ArchetypeRegistry &world, ecs::Entity e, int index, bool notify);
/// Change le texte d'un widget (et le curseur de son éditeur), émet TextChange si `notify`.
void SetTextValue(ecs::ArchetypeRegistry &world, ecs::Entity e, String text, bool notify);

} // namespace ui
