#pragma once
/**
 * ui::interaction — primitives transverses réutilisables par plusieurs
 * familles de widgets, pour éviter de dupliquer la même logique dans
 * chacune :
 *
 *   - Sélection multiple façon Explorer/Finder (clic simple / Ctrl+clic /
 *     Maj+clic) — partagée par Table (Phase 6), TreeNode (Phase 4) et
 *     Selectable (Phase 4).
 *   - Poignée de redimensionnement générique (drag 1D → delta appliqué à un
 *     accesseur) — partagée par Splitter (Phase 8) et les colonnes
 *     redimensionnables de Table (Phase 6).
 *
 * Ces deux primitives ne dessinent ni ne pilotent seules aucun widget visible
 * — elles sont composées PAR les widgets concrets des phases suivantes.
 * Le drag de UiResizeHandle est néanmoins câblé dans InputSystem::dispatch()
 * (systems.hpp) dès cette phase, pour que Table/Splitter n'aient qu'à
 * attacher le composant sans dupliquer la boucle presse/glisser/relâche.
 */
#include <algorithm>
#include <unordered_set>

#include "components.hpp"

namespace ui {

// ============================================================================
// Sélection multiple partagée (Table / TreeNode / Selectable)
// ============================================================================

/// État de sélection pur, indépendant de l'ECS — `index` est la position
/// dans l'ordre linéaire propre à chaque consommateur (index de ligne pour
/// Table/Selectable, index dans les lignes VISIBLES aplaties pour TreeNode).
struct SelectionState {
	std::unordered_set<int> selected;
	int anchor = -1;      ///< point de départ d'une extension Maj+clic
	int lastClicked = -1; ///< dernier index cliqué (informatif)
};

/// Composant conteneur (Table/TreeNode/Selectable) portant l'état de
/// sélection. `multiSelect = false` restreint `applySelectionClick` à un
/// remplacement pur (comme une ListBox) même si ctrl/shift sont enfoncés.
struct UiSelection {
	SelectionState state;
	bool multiSelect = true;
};

/// Applique un clic à l'état de sélection, façon Explorer/Finder :
///   - clic simple           : sélection unique = {index}, ancre = index
///   - Ctrl+clic              : bascule `index` sans toucher aux autres
///   - Maj+clic (ancre valide) : sélection = plage [ancre, index]
/// Sans multi-sélection (ou ancre encore invalide), ctrl/shift sont ignorés
/// et le clic se comporte comme un clic simple. Pure — ne touche pas l'ECS,
/// chaque widget appelant calcule `index` selon son propre hit-test puis
/// applique lui-même les conséquences (callback onChange, etc.).
inline void ApplySelectionClick(SelectionState &state, int index, bool ctrl, bool shift, bool multiSelect = true) {
	if (multiSelect && shift && state.anchor >= 0) {
		state.selected.clear();
		int lo = sdl3::Min(state.anchor, index), hi = sdl3::Max(state.anchor, index);
		for (int i = lo; i <= hi; ++i)
			state.selected.insert(i);
	} else if (multiSelect && ctrl) {
		if (state.selected.count(index))
			state.selected.erase(index);
		else
			state.selected.insert(index);
		state.anchor = index;
	} else {
		state.selected.clear();
		state.selected.insert(index);
		state.anchor = index;
	}
	state.lastClicked = index;
}

/// Remonte les UiParent depuis `e` (lui-même inclus) jusqu'à trouver une
/// entité porteuse d'un UiSelection — le "conteneur" logique d'une liste/
/// d'un arbre sélectionnable (UiSelectable/UiTreeNode, Phase 4). ecs::Entity{}
/// invalide si aucun ancêtre n'en porte (widget hors d'un conteneur de
/// sélection — clic ignoré par l'appelant).
[[nodiscard]] inline ecs::Entity NearestSelectionAncestor(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	ecs::Entity cur = e;
	while (cur.Valid()) {
		if (world.HasComponent<UiSelection>(cur))
			return cur;
		auto p = world.GetComponent<UiParent>(cur);
		if (p.IsNone())
			break;
		cur = p.Unwrap()->parent;
	}
	return ecs::Entity{};
}

// ============================================================================
// Poignée de redimensionnement générique (Splitter / colonnes de Table)
// ============================================================================

/// Drag 1D générique : traduit un déplacement souris en nouvelle taille pour
/// le panneau AVANT la poignée, via `setBefore` — épingle explicitement une
/// dimension en pixels (cf. UiItem.width/height : passer de Grow/Auto à
/// Px), les siblings Grow absorbent le reste via l'algorithme de layout déjà
/// existant (LayoutSystem::resolveSize/autoSize, non modifié). `getAfter`/
/// `setAfter` sont optionnels (utiles seulement si le panneau après n'est
/// PAS en Grow, ex: deux colonnes de Table toutes deux en Px) ; laissés
/// vides (nullptr) si le sibling après est en Grow.
///
/// `orient` décrit l'AXE DU GLISSER (pas la ligne visuelle de la poignée) :
/// Horizontal = glisse selon X (poignée verticale typique entre 2 panneaux
/// côte à côte) ; Vertical = glisse selon Y (poignée horizontale entre 2
/// panneaux empilés).
struct UiResizeHandle {
	Orientation orient = Orientation::Horizontal;
	std::function<float()> getBefore;
	std::function<void(float)> setBefore;
	std::function<float()> getAfter;
	std::function<void(float)> setAfter;
	float minBefore = 20.f, maxBefore = 1.0e9f;
	float minAfter = 20.f, maxAfter = 1.0e9f;
	// État de drag (géré par InputSystem — cf. systems.hpp dispatch()).
	bool dragging = false;
	bool hovered = false;
	float dragStartMouse = 0.f;
	float dragStartBefore = 0.f;
	float dragStartAfter = 0.f;
};

/// Résout UN pas de drag : appelé par InputSystem à chaque frame où le bouton
/// reste enfoncé sur une poignée déjà verrouillée (cf. resizeDrag). Pure côté
/// ECS — se contente d'appeler les accesseurs de `h`.
inline void ResolveResizeDrag(UiResizeHandle &h, float mouseCoord) {
	float delta = mouseCoord - h.dragStartMouse;
	if (h.setBefore) {
		float newBefore = sdl3::Clamp(h.dragStartBefore + delta, h.minBefore, h.maxBefore);
		h.setBefore(newBefore);
	}
	if (h.setAfter) {
		// Signe opposé : si le panneau avant grandit de +delta, celui après
		// doit rétrécir d'autant (cas des deux panneaux en Px explicite).
		float newAfter = sdl3::Clamp(h.dragStartAfter - delta, h.minAfter, h.maxAfter);
		h.setAfter(newAfter);
	}
}

// ============================================================================
// Réordonnancement par glisser (Selectable/TreeNode — Phase 4 ; réutilisable
// par les lignes de Table, Phase 6)
// ============================================================================

/// Poignée de réordonnancement par glisser vertical : contrairement à
/// UiResizeHandle (redimensionne 2 panneaux via des accesseurs), ceci DÉPLACE
/// l'entité elle-même parmi ses frères/sœurs directs (cf. reorderChild() —
/// l'ordre du UiChildren du parent EST la vérité, pas un état séparé à
/// resynchroniser). `dragMoved` (seuil de mouvement, cf. InputSystem::
/// dispatch()) évite qu'un simple clic de sélection (UiSelectable/
/// UiTreeNode, souvent posé sur la MÊME entité) ne déclenche un
/// réordonnancement parasite.
/**
 * Glisser-déposer ENTRE widgets (par opposition à UiReorderable, qui ne sait
 * que réordonner des frères d'une même liste).
 *
 * Deux marqueurs : `UiDragPayload` fait d'un widget une SOURCE transportant un
 * identifiant applicatif, `UiDropTarget` fait d'un widget une CIBLE qui
 * accepte un certain genre de charge. Au relâchement au-dessus d'une cible
 * compatible, `UiCallbacks::onDrop` est appelé sur la CIBLE avec l'identifiant
 * de la source.
 *
 * Pourquoi un identifiant applicatif (`int64_t`) et pas l'entité source :
 * l'entité d'interface est un détail de l'arbre d'UI, reconstruit à chaque
 * rafraîchissement de panneau ; ce que l'application veut savoir, c'est
 * « QUOI a été déposé » — un nœud de scène, une ressource, une ligne de
 * tableau — et c'est elle qui sait traduire cet identifiant.
 *
 * `kind` évite les dépôts absurdes (déposer une couleur sur un arbre de
 * scène) sans que l'application ait à le vérifier elle-même.
 */
struct UiDragPayload {
	String kind;      ///< genre de charge ("node", "asset"…)
	int64_t id = 0;   ///< identifiant applicatif transporté
	bool dragging = false; ///< vrai pendant le glissé (permet un rendu estompé)
};

/// Cible de dépôt. `hovered` est vrai quand un glissé COMPATIBLE la survole —
/// c'est ce qui permet de la surligner pendant le geste.
struct UiDropTarget {
	String accepts;        ///< genre accepté ; vide = tout accepter
	bool hovered = false;
};

struct UiReorderable {
	bool dragging = false;
	bool dragMoved = false;
	float dragStartMouseY = 0.f;
};

/// Déplace `child` juste avant `target` dans le UiChildren de leur parent
/// COMMUN `parent` (retire puis réinsère — un seul élément bouge, pas de tri
/// global). No-op si l'un des deux n'est pas un enfant direct de `parent`.
inline void ReorderChild(ecs::ArchetypeRegistry &world, ecs::Entity parent, ecs::Entity child, ecs::Entity target) {
	auto ch = world.GetComponent<UiChildren>(parent);
	if (ch.IsNone())
		return;
	auto &list = ch.Unwrap()->list;
	auto itChild = std::find(list.begin(), list.end(), child);
	if (itChild == list.end())
		return;
	list.erase(itChild);
	auto itTarget = std::find(list.begin(), list.end(), target); // ré-cherché : l'erase a pu décaler `target`
	if (itTarget == list.end()) {
		list.push_back(child); // `target` a disparu entre-temps (despawn concurrent) : replace en fin
		return;
	}
	list.insert(itTarget, child);
}

// ============================================================================
// Glisser générique (déplacement / redimensionnement) — panneaux flottants
// "bureau simulé" (Phase 9/10 : cf. ui::PanelChrome, chrome.hpp) — PAS d'OS
// hit-test, contrairement à ui::WindowChrome qui pilote sdl3::Window::
// setHitTest pour une vraie fenêtre borderless.
// ============================================================================

/// Glisser une entité (typiquement la zone de titre d'un panneau flottant)
/// pour déplacer OU redimensionner `target` (ecs::Entity{} = soi-même) :
/// `resizeMode=false` (défaut) modifie `UiRect.offset` de `target` (le
/// panneau suit la souris) ; `resizeMode=true` modifie `UiItem.width/height`
/// (poignée de coin — `target` doit alors avoir une taille explicite en Px,
/// pas Auto/Grow, sans quoi le redimensionnement n'aurait aucun effet
/// visible). Câblé dans InputSystem::dispatch() (systems.hpp) sur le même
/// motif latch-puis-suit que UiResizeHandle/UiReorderable.
struct UiDraggable {
	ecs::Entity target{};
	bool resizeMode = false;
	float minWidth = 100.f, minHeight = 60.f;
	// État (géré par InputSystem)
	bool dragging = false;
	sdl3::FPoint dragStartMouse{};
	sdl3::FPoint dragStartOffset{}; ///< UiRect.offset au press (mode déplacement)
	sdl3::FPoint dragStartSize{};   ///< UiItem.width/height.value au press (mode redimensionnement)
};

} // namespace ui
