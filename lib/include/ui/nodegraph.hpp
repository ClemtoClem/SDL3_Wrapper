#pragma once
/**
 * ui::nodegraph — éditeur de node-graph réutilisable (cf. plan
 * mossy-forging-fog.md, section "Node-Graph Editor"). Inspiré de
 * thedmd/imgui-node-editor, adapté à l'architecture ECS retenue de ce
 * module : les NŒUDS sont des entités ECS réelles (peuvent contenir
 * n'importe quel widget `ui::` enfant), les CONNEXIONS/points intermédiaires
 * sont des données pures (pas d'entité par élément — évite l'explosion
 * d'entités sur un graphe de centaines de connexions, même idiome que
 * UiTable.rows/Plot.values).
 *
 * Séparation éditeur/utilisateur (cf. spec, section 9) :
 *   - L'ÉDITEUR (ce fichier) gère : navigation (pan/zoom), positionnement,
 *     déplacement, redimensionnement, sélection, connexions, état du canvas.
 *   - L'UTILISATEUR fournit : le contenu des nœuds (widgets enfants
 *     arbitraires, `UiFactory` normal), le rendu personnalisé des
 *     connecteurs/connexions (callbacks d'échappement).
 *
 * Intégration dans la boucle applicative (cf. examples/ui_node_graph_demo.cpp,
 * Phase 9) :
 *
 *   nodeGraph.Prepass(ar);              // AVANT layout.RunIfNeeded() : réécrit
 *                                        // la position/taille écran des nœuds
 *                                        // depuis la transform canvas (pan/zoom)
 *   layout.RunIfNeeded(ar, w, h);
 *   nodeGraph.UpdatePins(ar);           // APRÈS layout : positionne les pins sur
 *                                        // le bord de leurs nœuds (dépend de leur
 *                                        // UiComputed déjà à jour)
 *   while (auto ev = sdl3::PollEvent()) {
 *       gui.HandleEvent(e);              // widgets normaux (contenu des nœuds)
 *       nodeGraph.HandleEvent(ar, e, layout); // interactions canvas/nœuds/pins/clavier
 *       // Ctrl+V n'est PAS géré par handleEvent() (pas de UiFactory& dispo) :
 *       // if (e.IsKeyDown(SDLK_v) && ctrlHeld) nodeGraph.PasteClipboard(f, canvasE, curseurCanvas);
 *   }
 *   nodeGraph.Tick(ar, layout, dt);     // une fois par frame : auto-scroll pendant un drag
 *   nodeGraph.RenderGrid(ar, ren);      // AVANT gui.Render() : fond quadrillé
 *   nodeGraph.RenderConnections(ar, ren); // APRÈS la grille : traits sous les nœuds
 *   gui.Render();                       // dessine panneaux/nœuds/contenu normalement
 *   nodeGraph.RenderHeaders(ar, ren);   // APRÈS gui.Render() : bandeaux de titre
 *   nodeGraph.RenderPins(ar, ren);      // APRÈS les en-têtes : pins toujours au-dessus
 *   nodeGraph.RenderMarquee(ren);       // EN DERNIER : rectangle de sélection en cours
 *
 * Persistance (spec section 10, cf. serializeGraph()/deserializeGraph()) :
 *   data::NodePtr doc = ui::SerializeGraph(ar, canvasE);
 *   data::JsonDocument json; json.SetRoot(doc);
 *   sdl3::IOStream::FromFile("graph.json", "wb")->write(json.encodeStr());
 *   // ... plus tard : ui::DeserializeGraph(f, nodeGraph, layout, canvasE, doc);
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "factory.hpp"
#include "render_backend.hpp"
#include "../data/node.hpp"
#include "../sdl3/structs.hpp"
#include "../sdl3/input.hpp"

namespace ui {

// ============================================================================
// Composants
// ============================================================================

/// Tracé d'une connexion entre deux pins (cf. spec section 4).
enum class ConnectionStyle : uint8_t { BEZIER, STRAIGHT, ORTHOGONAL };

/// Extrémité d'une connexion — aucune décoration, ou une flèche pleine
/// orientée selon la tangente locale du tracé (cf. spec section 4).
enum class ConnectionEndCap : uint8_t { NONE, ARROW };

/// Une connexion entre deux pins — donnée PURE sur
/// `UiNodeGraphCanvas::connections` (PAS une entité, cf. Décision #1 du plan :
/// un graphe de centaines de connexions ne justifie pas une entité chacune,
/// contrairement aux pins qui ont besoin d'un hit-test individuel). `fromPin`/
/// `toPin` référencent des `UiGraphPin` déjà spawnés (cf. addGraphPin()) —
/// une connexion dont un pin a été détruit est silencieusement ignorée au
/// rendu (cf. NodeGraphSystem::RenderConnections()), jamais retirée
/// automatiquement de la liste (même limitation, documentée, que
/// UiGraphPin::node — pas de despawn en cascade tant que Phase 5/6 ne
/// l'introduit pas). Construite via connectPins(), pas directement.
struct GraphConnection {
	ecs::Entity fromPin{};
	ecs::Entity toPin{};
	sdl3::FColor color{225 / 255.f, 225 / 255.f, 225 / 255.f, 235 / 255.f}; ///< gris clair neutre — cf. thème par défaut (Phase 7, style Blueprint)
	float thickness = 2.5f;
	float opacity = 1.f; ///< multiplie color.a au rendu (pas un second canal séparé sur color)
	ConnectionStyle style = ConnectionStyle::BEZIER;
	ConnectionEndCap startCap = ConnectionEndCap::NONE;
	ConnectionEndCap endCap = ConnectionEndCap::ARROW;
	float arrowSize = 10.f; ///< unités CANVAS, mis à l'échelle du zoom au rendu

	/// Force horizontale des points de contrôle Bézier, en unités CANVAS,
	/// mise à l'échelle du zoom au rendu ; 0 = automatique (proportionnel à
	/// la distance horizontale entre les deux pins).
	float bezierControlOffset = 0.f;

	/// Points intermédiaires déplaçables par l'utilisateur, en unités CANVAS
	/// — vide par défaut. Phase 3 ne route QU'entre les deux pins ; Phase 4
	/// généralisera detail::ConnectionPath() pour router à travers ces
	/// points, dans les 3 styles.
	std::vector<sdl3::FPoint> waypoints;

	/// Échappatoire de rendu personnalisé (même idiome que UiGraphPin) — si
	/// posé, REMPLACE le rendu par défaut ; reçoit le chemin ÉCRAN déjà
	/// calculé selon `style`.
	std::function<void(sdl3::Renderer &, std::span<const sdl3::FPoint> pathScreen, const GraphConnection &)> onCustomDraw;
};

/// État de sélection des nœuds d'un canvas — variante ecs::Entity-keyed de
/// `interaction.hpp`'s `SelectionState` (Décision #4 du plan : les nœuds
/// sont créés/détruits par IDENTITÉ, pas par position dans une liste, donc
/// une clé `ecs::Entity` plutôt que l'`int` positionnel de l'original). Même
/// sémantique Ctrl/Maj qu'`applySelectionClick` — cf. applyNodeSelectionClick().
struct NodeSelectionState {
	std::unordered_set<ecs::Entity> selected;
	ecs::Entity anchor{};      ///< point de départ d'une extension Maj+clic
	ecs::Entity lastClicked{}; ///< dernier nœud cliqué (informatif)
};

/// Applique un clic sur un nœud à l'état de sélection, façon Explorer/Finder
/// (même sémantique que `applySelectionClick`, cf. interaction.hpp) :
///   - clic simple : sélection unique = {clicked}, ancre = clicked
///   - Ctrl+clic    : bascule `clicked` sans toucher aux autres
///   - Maj+clic (ancre valide et présente dans `order`) : sélection = tous
///     les nœuds de `order` compris entre l'ancre et `clicked` (bornes
///     incluses) — `order` est typiquement l'ordre de `UiChildren` du canvas
///     (ordre de création, pas position spatiale : un nœud n'a pas d'"index"
///     naturel sur un canvas 2D libre, cf. Décision #4).
/// Pure — ne touche pas l'ECS au-delà de `state` lui-même ; l'appelant reste
/// responsable de répercuter le résultat sur UiGraphNode::selected (cf.
/// NodeGraphSystem — cette fonction reste testable/réutilisable seule).
inline void ApplyNodeSelectionClick(NodeSelectionState &state, ecs::Entity clicked, bool ctrl, bool shift,
									const std::vector<ecs::Entity> &order) {
	auto itAnchor = shift ? std::find(order.begin(), order.end(), state.anchor) : order.end();
	if (shift && state.anchor.Valid() && itAnchor != order.end()) {
		auto itClicked = std::find(order.begin(), order.end(), clicked);
		if (itClicked != order.end()) {
			auto lo = sdl3::Min(itAnchor, itClicked), hi = sdl3::Max(itAnchor, itClicked);
			state.selected.clear();
			for (auto it = lo; it <= hi; ++it)
				state.selected.insert(*it);
		}
	} else if (ctrl) {
		if (state.selected.count(clicked))
			state.selected.erase(clicked);
		else
			state.selected.insert(clicked);
		state.anchor = clicked;
	} else {
		state.selected.clear();
		state.selected.insert(clicked);
		state.anchor = clicked;
	}
	state.lastClicked = clicked;
}

/// Zone d'édition du node-graph — un panel normal auquel ce composant est
/// attaché (cf. createNodeGraphCanvas()). Sépare clairement coordonnées
/// CANVAS (position/taille des nœuds, indépendantes du zoom) de coordonnées
/// VIEWPORT (écran) : `pan` est un point CANVAS-space qui correspond au coin
/// haut-gauche du viewport ; `zoom` est le facteur d'échelle canvas→écran.
/// `screenPos = (canvasPos - pan) * zoom` (relatif à l'origine du contenu du
/// canvas, cf. NodeGraphSystem::Prepass()).
struct UiNodeGraphCanvas {
	sdl3::FPoint pan{0.f, 0.f};
	float zoom = 1.f;
	float minZoom = 0.15f, maxZoom = 4.f;

	// Grille de fond — personnalisable (cf. spec section 1). Valeurs par
	// défaut inspirées de l'éditeur de Blueprints Unreal (fond ardoise très
	// sombre, grille à peine visible — cf. spec section 8, Phase 7).
	sdl3::FColor bgColor{25 / 255.f, 26 / 255.f, 30 / 255.f, 1.f};
	sdl3::FColor gridColor{38 / 255.f, 39 / 255.f, 45 / 255.f, 1.f};
	sdl3::FColor gridColorMajor{55 / 255.f, 57 / 255.f, 66 / 255.f, 1.f};
	float gridSpacing = 24.f; ///< espacement en unités CANVAS entre lignes mineures
	int gridMajorEvery = 4;   ///< une ligne sur N est tracée plus marquée

	/// Connexions du graphe — DONNÉES PURES (cf. GraphConnection, Décision #1
	/// du plan : pas d'entité par connexion, même idiome que UiTable.rows).
	/// Ne pas manipuler directement — passer par connectPins()/disconnectPins().
	std::vector<GraphConnection> connections;

	/// Sélection de nœuds — cf. NodeSelectionState. Ne pas manipuler
	/// directement — passer par les méthodes de NodeGraphSystem
	/// (selectNode()/deselectNode()/clearSelection()/...) qui répercutent
	/// aussi l'état sur UiGraphNode::selected (rendu du contour de sélection).
	NodeSelectionState selection;
};

/// Un nœud — entité ECS réelle (cf. createGraphNode()) dont le CONTENU
/// (widgets enfants) est entièrement fourni par l'utilisateur ; seule la
/// boîte (position/taille CANVAS-space) et l'en-tête sont gérés par
/// l'éditeur. `canvasPos`/`size` sont la source de vérité — `UiRect.offset`/
/// `UiItem.width/height` sont réécrits à partir d'elles à chaque frame par
/// NodeGraphSystem::Prepass(), jamais l'inverse.
struct UiGraphNode {
	sdl3::FPoint canvasPos{0.f, 0.f};
	sdl3::FPoint size{200.f, 120.f};
	String title;
	/// Couleur d'en-tête neutre par défaut — cf. thème par défaut (Phase 7,
	/// style Blueprint) : un nœud "catégorisé" par l'utilisateur pose
	/// typiquement sa propre couleur (bleu=fonction, rouge=évènement, etc.,
	/// convention Blueprint), cette valeur n'est qu'un point de départ.
	sdl3::FColor headerColor = sdl3::FColor::UI_NODE_HEADER_BLUE();
	bool resizable = true;
	bool movable = true;
	bool selected = false;
	/// Désactivé : ignore tout drag/resize/sélection à la souris (même
	/// esprit que le marqueur générique UiDisabled — cf. spec section 8,
	/// état "désactivé"), rendu grisé par RenderHeaders().
	bool disabled = false;
	/// Survolé — mis à jour par NodeGraphSystem::HandleEvent(), comme
	/// UiGraphPin::hovered ; éclaircit l'en-tête au rendu.
	bool hovered = false;
	float minWidth = 80.f, minHeight = 40.f;
	float headerHeight = 26.f;
	// État d'interaction (géré par NodeGraphSystem) — sert aussi d'indicateur
	// "actif" (spec section 8) : un nœud en cours de manipulation EST son
	// propre état actif, pas besoin d'un champ dédié séparé.
	bool draggingMove = false;
	bool draggingResize = false;
};

/// Forme visuelle par défaut d'un pin (cf. spec section 3). Un rendu
/// personnalisé (UiGraphPin::onCustomDraw) peut ignorer cette valeur.
enum class PinShape : uint8_t { CIRCLE, TRIANGLE, SQUARE, STAR };

/// Bord du nœud sur lequel un pin est ancré ; `sideOffset` (0..1) place le pin
/// le long de ce bord (0 = coin de départ, 1 = coin d'arrivée) — normalisé
/// plutôt qu'en pixels pour rester correct quand le nœud est redimensionné.
enum class PinSide : uint8_t { Left, Right, Top, Bottom };

/// Un connecteur — PETITE entité ECS (cf. Décision #1 du plan : besoin d'un
/// hit-test individuel et d'un UiComputed propre pour ancrer les connexions,
/// Phase 3), mais SANS UiParent/UiRect/UiItem : sa position ne passe jamais
/// par LayoutSystem (qui l'ignorerait de toute façon, n'ayant pas de UiRect),
/// elle est entièrement recalculée par NodeGraphSystem::UpdatePins() à partir
/// du UiComputed déjà résolu de son nœud propriétaire. `node` est donc la
/// seule référence de hiérarchie — le despawn en cascade (pins + connexions
/// qui les référencent) passe par destroyGraphNode(), jamais par un despawn
/// direct du nœud (qui laisserait pins/connexions orphelins).
struct UiGraphPin {
	ecs::Entity node{}; ///< nœud propriétaire
	PinShape shape = PinShape::CIRCLE;
	/// Blanc cassé neutre par défaut — cf. thème par défaut (Phase 7, style
	/// Blueprint : les pins "exec" sont blancs, les pins typés colorés au
	/// choix de l'utilisateur selon son propre système de types).
	sdl3::FColor color = sdl3::FColor::UI_TEXT_BRIGHT();
	/// Diamètre en unités CANVAS (mis à l'échelle du zoom courant à chaque
	/// updatePins(), comme la taille des nœuds — cohérence visuelle avec le
	/// reste du canvas plutôt qu'une taille fixe à l'écran).
	float size = 10.f;
	PinSide side = PinSide::Left;
	float sideOffset = 0.5f; ///< position normalisée [0,1] le long de `side`

	// États visuels (cf. spec section 3) — connectable/connected/selected
	// sont posés par l'appelant ou par la logique de connexion (Phase 3) ;
	// hovered est géré par NodeGraphSystem::HandleEvent().
	bool connectable = true;
	bool connected = false;
	bool selected = false;
	bool hovered = false;

	/// Échappatoire de rendu personnalisé (même idiome que UiCanvas::onDraw /
	/// UiCalendar::onDaySelected) — si posé, REMPLACE entièrement le rendu
	/// par défaut de RenderPins() pour ce pin ; sinon les options
	/// déclaratives ci-dessus s'appliquent.
	std::function<void(sdl3::Renderer &, const sdl3::FRect &box, const UiGraphPin &)> onCustomDraw;
};

/// Hooks de changement optionnels (cf. spec section 6/9) — composant SÉPARÉ
/// attaché au canvas (même idiome que `UiCallbacks`, jamais embarqué
/// directement dans `UiNodeGraphCanvas`) : absent = aucun coût, pas de
/// `std::function` vide à ignorer sur le chemin chaud. Posés par
/// `world.AddComponent(canvasEntity, NodeGraphCallbacks{...})`.
struct NodeGraphCallbacks {
	/// Après toute mise à jour de NodeSelectionState (clic, marquee, API
	/// programmatique) — reçoit l'ensemble sélectionné À JOUR.
	std::function<void(const std::unordered_set<ecs::Entity> &)> onSelectionChanged;
	/// Au relâchement d'un glisser de déplacement (une fois par nœud du
	/// groupe déplacé), avec la nouvelle canvasPos.
	std::function<void(ecs::Entity, sdl3::FPoint)> onNodeMoved;
	/// Au relâchement d'un glisser de redimensionnement, avec la nouvelle taille.
	std::function<void(ecs::Entity, sdl3::FPoint)> onNodeResized;
	/// Clic droit sur le canvas — `node`/`pin` sont l'ecs::Entity survolée
	/// (invalide si aucune) et `screenPos` la position du clic. L'ÉDITEUR ne
	/// construit AUCUN menu lui-même (cf. spec section 9 : le contenu reste
	/// à la charge de l'utilisateur) — ce hook fournit juste le CONTEXTE ;
	/// à l'appelant de bâtir un `UiFactory::Popup()/menu()` positionné à
	/// `screenPos` (cf. positionPopupBelow, ui/systems.hpp).
	std::function<void(ecs::Entity node, ecs::Entity pin, sdl3::FPoint screenPos)> onContextMenu;
};

/// Copie d'un pin dans le presse-papiers interne (cf. ClipboardNode) — mêmes
/// champs déclaratifs qu'UiGraphPin, sans `node`/états d'interaction
/// (connected/selected/hovered, non pertinents pour une copie) ni
/// `onCustomDraw` (un std::function ne se "recopie" pas de façon
/// significative d'un nœud à un autre — l'appelant qui en a posé un sur
/// l'original devra le reposer sur le pin collé, cf. pasteClipboard()).
struct ClipboardPin {
	PinShape shape = PinShape::CIRCLE;
	sdl3::FColor color = sdl3::FColor::UI_TEXT_BRIGHT();
	float size = 10.f;
	PinSide side = PinSide::Left;
	float sideOffset = 0.5f;
	bool connectable = true;
};

/// Copie d'un nœud dans le presse-papiers interne (cf. NodeGraphSystem::
/// copySelection()/cutSelection()/pasteClipboard() — Décision #7 du plan).
/// Ne capture QUE ce que l'éditeur possède : chrome (titre/taille/couleur/
/// contraintes) + pins. Le CONTENU (widgets enfants arbitraires fournis par
/// l'utilisateur) n'est PAS cloné — un nœud collé a un corps VIDE, cohérent
/// avec la séparation éditeur/utilisateur (spec section 9) : l'éditeur ne
/// sait rien du contenu, il ne peut donc pas le dupliquer de façon générique.
struct ClipboardNode {
	String title;
	sdl3::FPoint size{200.f, 120.f};
	sdl3::FColor headerColor = sdl3::FColor::UI_NODE_HEADER_BLUE();
	bool resizable = true, movable = true;
	float minWidth = 80.f, minHeight = 40.f, headerHeight = 26.f;
	sdl3::FPoint originalCanvasPos{}; ///< pour préserver les positions RELATIVES entre nœuds copiés ensemble
	std::vector<ClipboardPin> pins;
};

// ============================================================================
// Construction (réutilise UiFactory — pas de duplication du DSL de widget)
// ============================================================================

/// Construit le canvas : un panel plein-conteneur, transparent par défaut
/// (le fond visible est la grille dessinée par RenderGrid(), pas un UiPanel
/// opaque — sinon elle masquerait la grille). `.clip()` pour que les nœuds
/// hors-cadre ne débordent pas visuellement.
[[nodiscard]] inline WidgetBuilder CanvasBuilder(UiFactory &f) {
	auto canvas = f.Column();
	// `.Column()` porte un UiFlow avec un padding par défaut (8px, cf.
	// UiFlow{}) — DOIT être explicitement remis à zéro ici : un enfant
	// .Absolute() se positionne relativement à la boîte de CONTENU du
	// parent (après padding), donc un padding non nul décalerait
	// silencieusement `canvasPos={0,0}` loin du coin réel du canvas.
	canvas.Pad(0.f);
	canvas.Clip();
	return canvas;
}

/// Attache UiNodeGraphCanvas à une entité déjà spawnée (cf. canvasBuilder()).
inline ecs::Entity AttachNodeGraphCanvas(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity, UiNodeGraphCanvas config = {}) {
	world.AddComponent(canvasEntity, std::move(config));
	return canvasEntity;
}

/// Ajoute un nœud à un canvas déjà spawné. `content` est un WidgetBuilder NON
/// spawné (typiquement `f.Column().Children(...)`) formant le corps du
/// nœud — l'en-tête (titre + couleur) est réservé via padding-top et dessiné
/// séparément par NodeGraphSystem::RenderHeaders(), même idiome que
/// UiExpander/UiTabView/UiTreeNode (padding-top réservant la place d'un
/// en-tête dessiné par le widget plutôt que composé comme un enfant).
/// `innerZoom` (Phase 7, spec section 7) échelonne UNIQUEMENT `content` (pas
/// le nœud lui-même ni son en-tête) via le mécanisme générique de
/// `EmBases::scale`/`prop::InnerZoom` posé en Phase 0 — 1.0 = taille normale,
/// modifiable après coup via setNodeInnerZoom().
[[nodiscard]] inline ecs::Entity AddGraphNode(UiFactory &f, ecs::Entity canvasEntity, String title, sdl3::FPoint canvasPos,
										 sdl3::FPoint size, WidgetBuilder content, sdl3::FColor headerColor = sdl3::FColor::UI_NODE_HEADER_BLUE(),
										 float innerZoom = 1.f) {
	auto node = f.Panel();
	node.Absolute().Anchor(Anchor::TopLeft).Parent(canvasEntity);
	UiGraphNode gn;
	gn.title = std::move(title);
	gn.canvasPos = canvasPos;
	gn.size = size;
	gn.headerColor = headerColor;
	node.Pad(math::Sides{6.f, gn.headerHeight + 6.f, 6.f, 6.f});
	node.Gap(4.f);
	if (innerZoom != 1.f)
		content.InnerZoom(innerZoom);
	node.Children(std::move(content));
	ecs::Entity e = node.Spawn();
	f.World().AddComponent(e, std::move(gn));
	return e;
}

/// Change l'inner-zoom d'un nœud APRÈS sa création (spec section 7 : "changer
/// indépendamment l'inner-zoom d'un nœud" fait partie des critères
/// d'acceptation). `content` (cf. addGraphNode()) est l'UNIQUE enfant direct
/// du nœud — poser `prop::InnerZoom` en style inline dessus suffit, la
/// cascade `EmBases::scale` de LayoutSystem::computeFonts() (Phase 0) fait le
/// reste sur tout son sous-arbre. `layout.MarkDirty()` est nécessaire ici
/// (contrairement à editInlineStyle() seul) car computeFonts() ne relit les
/// styles inline qu'à la prochaine VRAIE passe de layout, pas au prochain
/// appel de resolve() du StyleSystem (mécanismes découplés, cf. Phase 0).
inline void SetNodeInnerZoom(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity node, float value) {
	auto ch = world.GetComponent<UiChildren>(node);
	if (ch.IsNone() || ch.Unwrap()->list.empty())
		return;
	ecs::Entity contentRoot = ch.Unwrap()->list.front();
	EditInlineStyle(world, contentRoot, [value](UiStyle &s) { s.SetInnerZoom(value); });
	layout.MarkDirty();
}

/// Détruit un nœud (API programmatique, cf. spec section 6/9) : despawn en
/// cascade son sous-arbre de widgets (via despawnTree(), components.hpp),
/// SES pins (introuvables par UiParent — filtrées par UiGraphPin::node),
/// toute connexion référençant l'un de ces pins, et le retire de la
/// sélection du canvas (anchor réinitialisée si elle pointait dessus).
/// Collecte-puis-mute (mêmes précautions d'invalidation d'itérateur que le
/// reste du fichier) — les pins sont collectés AVANT tout despawn.
inline void DestroyGraphNode(ecs::ArchetypeRegistry &world, ecs::Entity node) {
	auto parent = world.GetComponent<UiParent>(node);
	ecs::Entity canvasE = parent.IsSome() ? parent.Unwrap()->parent : ecs::Entity{};

	std::vector<ecs::Entity> pinsToRemove;
	world.Query<UiGraphPin>([&](ecs::Entity e, UiGraphPin &pin) {
		if (pin.node == node)
			pinsToRemove.push_back(e);
	});
	auto isRemoved = [&](ecs::Entity p) {
		return std::find(pinsToRemove.begin(), pinsToRemove.end(), p) != pinsToRemove.end();
	};

	if (canvasE.Valid()) {
		if (auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasE); cv.IsSome()) {
			auto &conns = cv.Unwrap()->connections;
			// Pins de l'AUTRE côté d'une connexion supprimée : leur `connected`
			// doit être réévalué APRÈS la suppression (même règle que
			// disconnectPins() — un pin peut avoir plusieurs connexions).
			std::vector<ecs::Entity> affectedOther;
			for (const GraphConnection &c : conns) {
				bool fromRemoved = isRemoved(c.fromPin), toRemoved = isRemoved(c.toPin);
				if (fromRemoved && !toRemoved)
					affectedOther.push_back(c.toPin);
				else if (toRemoved && !fromRemoved)
					affectedOther.push_back(c.fromPin);
			}
			conns.erase(std::remove_if(conns.begin(), conns.end(),
									   [&](const GraphConnection &c) {
										   return isRemoved(c.fromPin) || isRemoved(c.toPin);
									   }),
					   conns.end());
			for (ecs::Entity other : affectedOther) {
				bool stillConnected = false;
				for (const GraphConnection &c : conns)
					if (c.fromPin == other || c.toPin == other) {
						stillConnected = true;
						break;
					}
				if (!stillConnected)
					if (auto p = world.GetComponent<UiGraphPin>(other); p.IsSome())
						p.Unwrap()->connected = false;
			}
			cv.Unwrap()->selection.selected.erase(node);
			if (cv.Unwrap()->selection.anchor == node)
				cv.Unwrap()->selection.anchor = ecs::Entity{};
		}
	}
	for (ecs::Entity p : pinsToRemove)
		world.Despawn(p);
	DespawnTree(world, node);
}

/// Ajoute un pin à un nœud déjà spawné. Spawn direct via l'ECS (pas de
/// WidgetBuilder — un pin n'a pas de sous-arbre de widgets) ; le UiComputed
/// est créé ICI, vide, plutôt que par updatePins() au premier appel : ça
/// évite un AddComponent() depuis l'intérieur d'une Query<UiGraphPin> plus
/// tard (même risque d'invalidation d'itérateur que documenté ailleurs dans
/// ce module — updatePins() n'a alors plus qu'à MUTER des champs existants).
[[nodiscard]] inline ecs::Entity AddGraphPin(ecs::ArchetypeRegistry &world, ecs::Entity node, PinSide side, float sideOffset = 0.5f,
										PinShape shape = PinShape::CIRCLE, sdl3::FColor color = sdl3::FColor::UI_TEXT_BRIGHT(),
										float size = 10.f) {
	UiGraphPin pin;
	pin.node = node;
	pin.side = side;
	pin.sideOffset = sideOffset;
	pin.shape = shape;
	pin.color = color;
	pin.size = size;
	return world.SpawnBundle(std::move(pin), UiComputed{});
}

namespace detail {
/// Point d'ancrage d'un pin sur le bord `side` d'un rectangle écran `s`.
[[nodiscard]] inline sdl3::FPoint PinAnchor(PinSide side, float sideOffset, const sdl3::FRect &s) noexcept {
	switch (side) {
	case PinSide::Left: return {s.x, s.y + sideOffset * s.h};
	case PinSide::Right: return {s.x + s.w, s.y + sideOffset * s.h};
	case PinSide::Top: return {s.x + sideOffset * s.w, s.y};
	case PinSide::Bottom: return {s.x + sideOffset * s.w, s.y + s.h};
	}
	return {s.x, s.y};
}

/// Sommets d'une étoile à `points` branches (angle de départ vers le haut).
[[nodiscard]] inline std::vector<sdl3::FPoint> StarPoints(sdl3::FPoint center, float outerR, float innerR, int points) {
	std::vector<sdl3::FPoint> pts;
	pts.reserve(size_t(points) * 2);
	float step = sdl3::PI_F / float(points);
	float angle = -sdl3::PI_F * 0.5f;
	for (int i = 0; i < points * 2; ++i) {
		float r = (i % 2 == 0) ? outerR : innerR;
		pts.push_back({center.x + r * std::cos(angle), center.y + r * std::sin(angle)});
		angle += step;
	}
	return pts;
}

/// Centre d'un rectangle écran (utilisé pour ancrer une connexion sur le
/// centre d'un pin plutôt que son coin, cf. RenderConnections()).
[[nodiscard]] inline sdl3::FPoint PinCenter(const sdl3::FRect &box) noexcept { return {box.x + box.w * 0.5f, box.y + box.h * 0.5f}; }

/// Échantillonne une Bézier cubique en `segments` segments (de Casteljau
/// direct, degré 3 fixe — pas besoin du degré arbitraire de
/// sdl3::Renderer::drawBezier ici puisqu'on a besoin des POINTS pour
/// construire un ruban épais, pas juste tracer une ligne 1px).
[[nodiscard]] inline std::vector<sdl3::FPoint> SampleCubicBezier(sdl3::FPoint p0, sdl3::FPoint p1, sdl3::FPoint p2, sdl3::FPoint p3, int segments) {
	std::vector<sdl3::FPoint> pts;
	pts.reserve(size_t(segments) + 1);
	for (int i = 0; i <= segments; ++i) {
		float t = float(i) / float(segments);
		float mt = 1.f - t;
		float a = mt * mt * mt, b = 3.f * mt * mt * t, c = 3.f * mt * t * t, d = t * t * t;
		pts.push_back({a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y});
	}
	return pts;
}

/// Chemin ÉCRAN d'UN segment `a`→`b` (sans point intermédiaire), selon
/// `style`. `controlOffsetCanvas` (Bézier uniquement) est en unités CANVAS,
/// mis à l'échelle par `zoom` ; 0 = automatique (proportionnel à |dx|).
[[nodiscard]] inline std::vector<sdl3::FPoint> SegmentPath(sdl3::FPoint a, sdl3::FPoint b, ConnectionStyle style,
													  float controlOffsetCanvas, float zoom) {
	switch (style) {
	case ConnectionStyle::STRAIGHT:
		return {a, b};
	case ConnectionStyle::ORTHOGONAL: {
		float midX = (a.x + b.x) * 0.5f;
		return {a, {midX, a.y}, {midX, b.y}, b};
	}
	case ConnectionStyle::BEZIER:
	default: {
		float dx = b.x - a.x;
		float offset = controlOffsetCanvas > 0.f ? controlOffsetCanvas * zoom
												  : sdl3::Clamp(std::abs(dx) * 0.5f, 30.f * zoom, 150.f * zoom);
		sdl3::FPoint p1{a.x + offset, a.y}, p2{b.x - offset, b.y};
		return SampleCubicBezier(a, p1, p2, b, 24);
	}
	}
}

/// Chemin ÉCRAN complet d'une connexion `a`→`b`, routé à travers
/// `throughScreen` (points intermédiaires DÉJÀ convertis en coordonnées
/// écran, dans l'ordre) — chaîne segmentPath() sur chaque paire de points
/// consécutifs (a, through[0], ..., through[n-1], b) et concatène, en
/// évitant de dupliquer le point de jonction entre deux segments (cf.
/// GraphConnection::waypoints — Phase 4).
[[nodiscard]] inline std::vector<sdl3::FPoint> ConnectionPath(sdl3::FPoint a, std::span<const sdl3::FPoint> throughScreen, sdl3::FPoint b,
														 ConnectionStyle style, float controlOffsetCanvas,
														 float zoom) {
	std::vector<sdl3::FPoint> pts;
	pts.reserve(throughScreen.size() + 2);
	pts.push_back(a);
	pts.insert(pts.end(), throughScreen.begin(), throughScreen.end());
	pts.push_back(b);

	std::vector<sdl3::FPoint> path;
	for (size_t i = 0; i + 1 < pts.size(); ++i) {
		auto seg = SegmentPath(pts[i], pts[i + 1], style, controlOffsetCanvas, zoom);
		if (!path.empty() && !seg.empty())
			path.pop_back(); // le dernier point du segment précédent == le premier de celui-ci
		path.insert(path.end(), seg.begin(), seg.end());
	}
	return path;
}

/// Surcharge sans points intermédiaires (raccourci pour l'appelant qui sait
/// déjà qu'il n'y en a pas — évite de construire un span vide à chaque appel).
[[nodiscard]] inline std::vector<sdl3::FPoint> ConnectionPath(sdl3::FPoint a, sdl3::FPoint b, ConnectionStyle style,
														 float controlOffsetCanvas, float zoom) {
	return ConnectionPath(a, std::span<const sdl3::FPoint>{}, b, style, controlOffsetCanvas, zoom);
}

/// Distance perpendiculaire de `p` au segment `[a,b]` (clampée aux
/// extrémités) — utilisé pour détecter un double-clic sur le TRACÉ d'une
/// connexion (ajout de point intermédiaire, cf. NodeGraphSystem::HandleEvent).
[[nodiscard]] inline float DistanceToSegment(sdl3::FPoint p, sdl3::FPoint a, sdl3::FPoint b) noexcept {
	sdl3::FPoint ab{b.x - a.x, b.y - a.y};
	float lenSq = ab.x * ab.x + ab.y * ab.y;
	float t = lenSq > 0.0001f ? sdl3::Clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.f, 1.f) : 0.f;
	sdl3::FPoint proj{a.x + ab.x * t, a.y + ab.y * t};
	float dx = p.x - proj.x, dy = p.y - proj.y;
	return std::sqrt(dx * dx + dy * dy);
}
} // namespace detail

/// Insère un point intermédiaire à l'index `index` (canvas-space) — indices
/// hors bornes sont clampés à la fin (ajout en queue).
inline void InsertWaypoint(GraphConnection &conn, size_t index, sdl3::FPoint canvasPoint) {
	index = sdl3::Min(index, conn.waypoints.size());
	conn.waypoints.insert(conn.waypoints.begin() + ptrdiff_t(index), canvasPoint);
}

/// Retire le point intermédiaire à l'index `index` (no-op si hors bornes).
inline void RemoveWaypoint(GraphConnection &conn, size_t index) {
	if (index < conn.waypoints.size())
		conn.waypoints.erase(conn.waypoints.begin() + ptrdiff_t(index));
}

/// Canvas propriétaire d'un pin (remonte pin.node -> UiParent.parent), ou une
/// ecs::Entity invalide si le pin/nœud n'a pas (plus) de parent valide.
[[nodiscard]] inline ecs::Entity CanvasOfPin(ecs::ArchetypeRegistry &world, ecs::Entity pin) {
	auto p = world.GetComponent<UiGraphPin>(pin);
	if (p.IsNone())
		return ecs::Entity{};
	auto parent = world.GetComponent<UiParent>(p.Unwrap()->node);
	if (parent.IsNone())
		return ecs::Entity{};
	return parent.Unwrap()->parent;
}

/// Canvas propriétaire d'un nœud (son UiParent direct), ou une ecs::Entity
/// invalide si `node` n'a pas de parent valide.
[[nodiscard]] inline ecs::Entity CanvasOfNode(ecs::ArchetypeRegistry &world, ecs::Entity node) {
	auto parent = world.GetComponent<UiParent>(node);
	return parent.IsSome() ? parent.Unwrap()->parent : ecs::Entity{};
}

/// Point CANVAS-space -> écran, pour un point qui n'a pas de UiComputed
/// propre (waypoints — cf. GraphConnection::waypoints) : même formule que
/// NodeGraphSystem::Prepass() pour les nœuds (`(pt - pan) * zoom`), mais
/// appliquée manuellement puisque les waypoints ne passent jamais par
/// LayoutSystem. `canvasScreen` est le UiComputed.screen DÉJÀ résolu du
/// canvas lui-même (son origine écran absolue).
[[nodiscard]] inline sdl3::FPoint CanvasToScreen(sdl3::FPoint canvasPt, const UiNodeGraphCanvas &cv, const sdl3::FRect &canvasScreen) noexcept {
	return {canvasScreen.x + (canvasPt.x - cv.pan.x) * cv.zoom, canvasScreen.y + (canvasPt.y - cv.pan.y) * cv.zoom};
}

/// Inverse de canvasToScreen() — point écran -> CANVAS-space.
[[nodiscard]] inline sdl3::FPoint ScreenToCanvas(sdl3::FPoint screenPt, const UiNodeGraphCanvas &cv, const sdl3::FRect &canvasScreen) noexcept {
	float zoom = sdl3::Max(0.0001f, cv.zoom);
	return {cv.pan.x + (screenPt.x - canvasScreen.x) / zoom, cv.pan.y + (screenPt.y - canvasScreen.y) / zoom};
}

/// Crée une connexion entre deux pins — refusée (retourne nullptr, aucun
/// effet) si `from`/`to` sont invalides ou identiques, si l'un des deux pins
/// n'est pas `connectable`, s'ils n'appartiennent pas au MÊME canvas, ou si
/// une connexion existe déjà entre eux (dans un sens ou l'autre — pas de
/// doublon). Retourne un pointeur vers la connexion insérée pour que
/// l'appelant personnalise ses champs juste après (couleur/style/épaisseur/
/// caps...) sans avoir à la rechercher.
inline GraphConnection *ConnectPins(ecs::ArchetypeRegistry &world, ecs::Entity from, ecs::Entity to) {
	if (!from.Valid() || !to.Valid() || from == to)
		return nullptr;
	auto pinFrom = world.GetComponent<UiGraphPin>(from);
	auto pinTo = world.GetComponent<UiGraphPin>(to);
	if (pinFrom.IsNone() || pinTo.IsNone() || !pinFrom.Unwrap()->connectable || !pinTo.Unwrap()->connectable)
		return nullptr;
	ecs::Entity canvasE = CanvasOfPin(world, from);
	if (!canvasE.Valid() || CanvasOfPin(world, to) != canvasE)
		return nullptr;
	auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasE);
	if (cv.IsNone())
		return nullptr;
	for (GraphConnection &c : cv.Unwrap()->connections)
		if ((c.fromPin == from && c.toPin == to) || (c.fromPin == to && c.toPin == from))
			return nullptr;
	cv.Unwrap()->connections.push_back(GraphConnection{});
	GraphConnection &added = cv.Unwrap()->connections.back();
	added.fromPin = from;
	added.toPin = to;
	pinFrom.Unwrap()->connected = true;
	pinTo.Unwrap()->connected = true;
	return &added;
}

/// Retire toute connexion entre `from` et `to` (dans un sens ou l'autre). Ne
/// remet `connected` à false sur un pin que s'il ne lui reste plus AUCUNE
/// connexion (un pin peut avoir plusieurs connexions simultanées).
inline void DisconnectPins(ecs::ArchetypeRegistry &world, ecs::Entity from, ecs::Entity to) {
	ecs::Entity canvasE = CanvasOfPin(world, from);
	if (!canvasE.Valid())
		return;
	auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasE);
	if (cv.IsNone())
		return;
	auto &conns = cv.Unwrap()->connections;
	conns.erase(std::remove_if(conns.begin(), conns.end(),
							   [&](const GraphConnection &c) {
								   return (c.fromPin == from && c.toPin == to) ||
										  (c.fromPin == to && c.toPin == from);
							   }),
			   conns.end());
	auto stillConnected = [&](ecs::Entity pin) {
		for (const GraphConnection &c : conns)
			if (c.fromPin == pin || c.toPin == pin)
				return true;
		return false;
	};
	if (auto p = world.GetComponent<UiGraphPin>(from); p.IsSome() && !stillConnected(from))
		p.Unwrap()->connected = false;
	if (auto p = world.GetComponent<UiGraphPin>(to); p.IsSome() && !stillConnected(to))
		p.Unwrap()->connected = false;
}

// ============================================================================
// NodeGraphSystem — prepass transform + interactions + rendu grille/en-têtes
// ============================================================================

class NodeGraphSystem {
public:
	/// Nécessaire pour que le titre des nœuds soit dessiné (cf.
	/// RenderHeaders()) — même contrainte que RenderSystem::SetTextEngine :
	/// sans elle, le texte est simplement omis (utile en headless).
	void SetTextEngine(sdl3::TextEngine &engine, sdl3::Font &font) {
		m_engine = &engine;
		m_font = &font;
	}

	/// À appeler juste AVANT layout.RunIfNeeded() chaque frame (cf. en-tête
	/// du fichier) : réécrit UiRect.offset/UiItem.width/height de chaque
	/// UiGraphNode depuis la transform pan/zoom de son UiNodeGraphCanvas
	/// parent — AUCUNE modification du placement lui-même dans LayoutSystem
	/// (cf. plan, Décision #2) : le reste du pipeline traite ensuite le
	/// nœud comme un enfant `.Absolute()` ordinaire.
	void Prepass(ecs::ArchetypeRegistry &world) {
		world.Query<UiGraphNode, UiParent, UiRect, UiItem>(
			[&](ecs::Entity, UiGraphNode &node, UiParent &parent, UiRect &rect, UiItem &item) {
				auto canvas = world.GetComponent<UiNodeGraphCanvas>(parent.parent);
				if (canvas.IsNone())
					return;
				const UiNodeGraphCanvas &cv = *canvas.Unwrap();
				rect.offset = {(node.canvasPos.x - cv.pan.x) * cv.zoom, (node.canvasPos.y - cv.pan.y) * cv.zoom};
				item.width = Dimension::Px(node.size.x * cv.zoom);
				item.height = Dimension::Px(node.size.y * cv.zoom);
			});
	}

	/// À appeler juste APRÈS layout.RunIfNeeded() chaque frame (cf. en-tête
	/// du fichier) : positionne chaque UiGraphPin sur le bord de son nœud
	/// (side/sideOffset), à partir du UiComputed.screen DÉJÀ résolu du nœud
	/// — ne touche jamais LayoutSystem, ni n'ajoute de composant (cf. le
	/// commentaire d'addGraphPin() sur pourquoi UiComputed est créé une
	/// fois, à la construction).
	void UpdatePins(ecs::ArchetypeRegistry &world) {
		world.Query<UiGraphPin, UiComputed>([&](ecs::Entity, UiGraphPin &pin, UiComputed &c) {
			auto nodeComputed = world.GetComponent<UiComputed>(pin.node);
			if (nodeComputed.IsNone()) {
				c.screen = {};
				return;
			}
			float zoom = 1.f;
			if (auto parent = world.GetComponent<UiParent>(pin.node); parent.IsSome())
				if (auto cv = world.GetComponent<UiNodeGraphCanvas>(parent.Unwrap()->parent); cv.IsSome())
					zoom = cv.Unwrap()->zoom;
			sdl3::FPoint anchor = detail::PinAnchor(pin.side, pin.sideOffset, nodeComputed.Unwrap()->screen);
			float half = sdl3::Max(1.f, pin.size * zoom * 0.5f);
			c.screen = {anchor.x - half, anchor.y - half, half * 2.f, half * 2.f};
		});
	}

	/// Pin sous le point écran `(x,y)`, ou une ecs::Entity invalide si aucun (les
	/// pins non `connectable` sont ignorés — réservé aux interactions de
	/// connexion, Phase 3, mais réutilisé dès Phase 2 pour le survol/hit-test
	/// de base).
	[[nodiscard]] ecs::Entity HitTestPin(ecs::ArchetypeRegistry &world, float x, float y) const {
		ecs::Entity hit{};
		world.Query<UiGraphPin, UiComputed>([&](ecs::Entity e, UiGraphPin &pin, UiComputed &c) {
			if (hit.Valid() || !pin.connectable)
				return;
			if (c.screen.Contains({x, y}))
				hit = e;
		});
		return hit;
	}

	/// Sélectionne `node` (API programmatique, cf. spec section 6/9) —
	/// `additive=false` remplace la sélection courante, `true` l'ajoute sans
	/// toucher aux autres (comme un Ctrl+clic). No-op si `node` n'a pas de
	/// UiGraphNode ou de canvas parent valide.
	void SelectNode(ecs::ArchetypeRegistry &world, ecs::Entity node, bool additive = false) {
		ecs::Entity canvasE = CanvasOfNode(world, node);
		if (!canvasE.Valid())
			return;
		auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasE);
		if (cv.IsNone())
			return;
		if (!additive)
			cv.Unwrap()->selection.selected.clear();
		cv.Unwrap()->selection.selected.insert(node);
		cv.Unwrap()->selection.anchor = node;
		SyncSelectionVisuals(world, canvasE, *cv.Unwrap());
	}

	/// Retire `node` de la sélection de son canvas (no-op s'il n'y était pas).
	void DeselectNode(ecs::ArchetypeRegistry &world, ecs::Entity node) {
		ecs::Entity canvasE = CanvasOfNode(world, node);
		if (!canvasE.Valid())
			return;
		auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasE);
		if (cv.IsNone())
			return;
		cv.Unwrap()->selection.selected.erase(node);
		SyncSelectionVisuals(world, canvasE, *cv.Unwrap());
	}

	/// Vide la sélection d'un canvas.
	void ClearSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity) {
		auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasEntity);
		if (cv.IsNone())
			return;
		cv.Unwrap()->selection.selected.clear();
		cv.Unwrap()->selection.anchor = ecs::Entity{};
		SyncSelectionVisuals(world, canvasEntity, *cv.Unwrap());
	}

	/// Sélectionne tous les nœuds d'un canvas.
	void SelectAllNodes(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity) {
		auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasEntity);
		if (cv.IsNone())
			return;
		world.Query<UiGraphNode, UiParent>([&](ecs::Entity e, UiGraphNode &, UiParent &p) {
			if (p.parent == canvasEntity)
				cv.Unwrap()->selection.selected.insert(e);
		});
		SyncSelectionVisuals(world, canvasEntity, *cv.Unwrap());
	}

	[[nodiscard]] bool IsNodeSelected(ecs::ArchetypeRegistry &world, ecs::Entity node) const {
		ecs::Entity canvasE = CanvasOfNode(world, node);
		if (!canvasE.Valid())
			return false;
		auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasE);
		return cv.IsSome() && cv.Unwrap()->selection.selected.count(node) > 0;
	}

	/// Détruit tous les nœuds actuellement sélectionnés d'un canvas (API
	/// programmatique, cf. spec section 6 — réutilisée par la touche
	/// Suppr/Retour arrière dans handleEvent()). Copie la liste AVANT de
	/// détruire (destroyGraphNode() modifie selection.selected lui-même).
	void DeleteSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity) {
		auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasEntity);
		if (cv.IsNone())
			return;
		std::vector<ecs::Entity> toDelete(cv.Unwrap()->selection.selected.begin(), cv.Unwrap()->selection.selected.end());
		for (ecs::Entity e : toDelete)
			DestroyGraphNode(world, e);
	}

	/// Copie les nœuds sélectionnés d'un canvas dans le presse-papiers
	/// interne (remplace tout contenu précédent) — cf. ClipboardNode :
	/// chrome + pins, PAS le contenu (widgets enfants), cf. sa doc.
	void CopySelection(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity) {
		auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasEntity);
		if (cv.IsNone())
			return;
		clipboard.clear();
		for (ecs::Entity e : cv.Unwrap()->selection.selected) {
			auto n = world.GetComponent<UiGraphNode>(e);
			if (n.IsNone())
				continue;
			ClipboardNode cn;
			cn.title = n.Unwrap()->title;
			cn.size = n.Unwrap()->size;
			cn.headerColor = n.Unwrap()->headerColor;
			cn.resizable = n.Unwrap()->resizable;
			cn.movable = n.Unwrap()->movable;
			cn.minWidth = n.Unwrap()->minWidth;
			cn.minHeight = n.Unwrap()->minHeight;
			cn.headerHeight = n.Unwrap()->headerHeight;
			cn.originalCanvasPos = n.Unwrap()->canvasPos;
			world.Query<UiGraphPin>([&](ecs::Entity, UiGraphPin &pin) {
				if (pin.node == e)
					cn.pins.push_back({pin.shape, pin.color, pin.size, pin.side, pin.sideOffset, pin.connectable});
			});
			clipboard.push_back(std::move(cn));
		}
	}

	/// Copie puis détruit la sélection (couper, cf. spec section 6).
	void CutSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity) {
		CopySelection(world, canvasEntity);
		DeleteSelection(world, canvasEntity);
	}

	[[nodiscard]] bool HasClipboard() const noexcept { return !clipboard.empty(); }

	/// Recrée les nœuds du presse-papiers sur `canvasEntity`, en préservant
	/// leurs positions RELATIVES les unes aux autres, translatées pour que le
	/// PREMIER nœud copié atterrisse à `atCanvasPos` (typiquement la position
	/// canvas du curseur au moment du collage). Nécessite un `UiFactory&`
	/// (contrairement au reste de l'API de cette classe) puisque coller
	/// implique de vraiment SPAWNER des entités avec du layout — c'est pour
	/// cette raison que le Ctrl+V n'est PAS géré automatiquement dans
	/// handleEvent() (qui n'a accès qu'à l'ecs::ArchetypeRegistry) : l'application
	/// doit détecter Ctrl+V elle-même et appeler cette méthode. Retourne les
	/// ecs::Entity créées (vide si le presse-papiers est vide).
	std::vector<ecs::Entity> PasteClipboard(UiFactory &f, ecs::Entity canvasEntity, sdl3::FPoint atCanvasPos) {
		std::vector<ecs::Entity> pasted;
		if (clipboard.empty())
			return pasted;
		sdl3::FPoint basePos = clipboard.front().originalCanvasPos;
		for (const ClipboardNode &cn : clipboard) {
			sdl3::FPoint relative{cn.originalCanvasPos.x - basePos.x, cn.originalCanvasPos.y - basePos.y};
			sdl3::FPoint newPos{atCanvasPos.x + relative.x, atCanvasPos.y + relative.y};
			auto content = f.Column(); // corps vide — le contenu n'est jamais cloné, cf. ClipboardNode
			ecs::Entity e = AddGraphNode(f, canvasEntity, cn.title, newPos, cn.size, std::move(content), cn.headerColor);
			if (auto n = f.World().GetComponent<UiGraphNode>(e); n.IsSome()) {
				n.Unwrap()->resizable = cn.resizable;
				n.Unwrap()->movable = cn.movable;
				n.Unwrap()->minWidth = cn.minWidth;
				n.Unwrap()->minHeight = cn.minHeight;
				n.Unwrap()->headerHeight = cn.headerHeight;
			}
			for (const ClipboardPin &cp : cn.pins)
				(void)AddGraphPin(f.World(), e, cp.side, cp.sideOffset, cp.shape, cp.color, cp.size);
			pasted.push_back(e);
		}
		return pasted;
	}

	/// Mises à jour continues indépendantes des évènements — actuellement
	/// uniquement l'auto-scroll pendant un drag de nœud proche du bord du
	/// canvas (spec section 1/6) : contrairement au reste des interactions,
	/// il doit continuer à défiler même si la souris reste immobile au bord,
	/// donc ne peut PAS être piloté uniquement par les évènements de
	/// mouvement — à appeler une fois par frame, avec un vrai dt (même rôle
	/// que InputSystem::Tick(), même raison d'être).
	void Tick(ecs::ArchetypeRegistry &world, LayoutSystem &layout, float dt) {
		if (!(dragNode.Valid() && dragMode == DragMode::MOVE))
			return;
		ecs::Entity canvasE = CanvasOfNode(world, dragNode);
		auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasE);
		auto c = world.GetComponent<UiComputed>(canvasE);
		if (cv.IsNone() || c.IsNone())
			return;
		const sdl3::FRect &s = c.Unwrap()->screen;
		constexpr float EDGE = 24.f, SPEED = 480.f; // px écran / seconde
		sdl3::FPoint scrollScreen{0.f, 0.f};
		if (mouseX < s.x + EDGE)
			scrollScreen.x = -SPEED * dt;
		else if (mouseX > s.x + s.w - EDGE)
			scrollScreen.x = SPEED * dt;
		if (mouseY < s.y + EDGE)
			scrollScreen.y = -SPEED * dt;
		else if (mouseY > s.y + s.h - EDGE)
			scrollScreen.y = SPEED * dt;
		if (scrollScreen.x == 0.f && scrollScreen.y == 0.f)
			return;
		float zoom = sdl3::Max(0.0001f, cv.Unwrap()->zoom);
		sdl3::FPoint canvasDelta{scrollScreen.x / zoom, scrollScreen.y / zoom};
		// Le pan ET les positions de départ du groupe avancent du MÊME delta
		// canvas : le nœud glissé reste visuellement ancré près du bord (le
		// curseur, lui, ne bouge pas) pendant que la vue défile sous lui —
		// cf. journal de progression (memory/) pour le raisonnement complet.
		cv.Unwrap()->pan = {cv.Unwrap()->pan.x + canvasDelta.x, cv.Unwrap()->pan.y + canvasDelta.y};
		for (auto &[e, startPos] : groupDragStart) {
			startPos = {startPos.x + canvasDelta.x, startPos.y + canvasDelta.y};
			if (auto n = world.GetComponent<UiGraphNode>(e); n.IsSome())
				n.Unwrap()->canvasPos = {startPos.x + (mouseX - dragStartMouse.x) / zoom,
										 startPos.y + (mouseY - dragStartMouse.y) / zoom};
		}
		layout.MarkDirty();
	}

	/// Un évènement SDL discret — glisser un en-tête déplace le nœud,
	/// glisser la poignée de coin le redimensionne, molette au-dessus du
	/// canvas zoome (vers le curseur), clic-milieu fait défiler (pan).
	/// Appelé par l'application EN PLUS de `gui.HandleEvent()`, pas à sa
	/// place (cf. en-tête du fichier) — reste un système séparé, autonome,
	/// pour ne rien changer à InputSystem/systems.hpp.
	void HandleEvent(ecs::ArchetypeRegistry &world, const sdl3::Event &ev, LayoutSystem &layout) {
		if (ev.IsMouseMotion()) {
			mouseX = ev.MouseMotion().x;
			mouseY = ev.MouseMotion().y;
		} else if (ev.IsMouseDown(SDL_BUTTON_LEFT) || ev.IsMouseDown(SDL_BUTTON_MIDDLE) ||
				  ev.IsMouseDown(SDL_BUTTON_RIGHT)) {
			mouseX = ev.MouseButton().x;
			mouseY = ev.MouseButton().y;
		} else if (ev.IsMouseUp(SDL_BUTTON_LEFT) || ev.IsMouseUp(SDL_BUTTON_MIDDLE)) {
			mouseX = ev.MouseButton().x;
			mouseY = ev.MouseButton().y;
		}
		const float MX = mouseX, MY = mouseY;

		// ── Raccourcis clavier : Suppr/Retour arrière, Ctrl+C, Ctrl+X ───────
		// (cf. spec section 6, Décision #7) — indépendants de tout état de
		// drag en cours. S'appliquent à TOUS les canvas ayant une sélection
		// non vide (un seul canvas actif à la fois dans l'usage courant, donc
		// pas besoin de suivre "le" canvas ayant le focus clavier). Ctrl+V
		// n'est PAS géré ici — cf. pasteClipboard(), qui a besoin d'un
		// UiFactory& que cette méthode n'a pas.
		if (ev.IsKeyDown()) {
			bool ctrl = (ev.Key().mod & SDL_KMOD_CTRL) != 0;
			SDL_Keycode key = ev.Keycode();
			if (key == SDLK_DELETE || key == SDLK_BACKSPACE) {
				std::vector<ecs::Entity> canvases;
				world.Query<UiNodeGraphCanvas>([&](ecs::Entity e, UiNodeGraphCanvas &cv) {
					if (!cv.selection.selected.empty())
						canvases.push_back(e);
				});
				for (ecs::Entity ce : canvases)
					DeleteSelection(world, ce);
				layout.MarkDirty();
			} else if (ctrl && key == SDLK_C) {
				world.Query<UiNodeGraphCanvas>([&](ecs::Entity e, UiNodeGraphCanvas &cv) {
					if (!cv.selection.selected.empty())
						CopySelection(world, e);
				});
			} else if (ctrl && key == SDLK_X) {
				std::vector<ecs::Entity> canvases;
				world.Query<UiNodeGraphCanvas>([&](ecs::Entity e, UiNodeGraphCanvas &cv) {
					if (!cv.selection.selected.empty())
						canvases.push_back(e);
				});
				for (ecs::Entity ce : canvases)
					CutSelection(world, ce);
				layout.MarkDirty();
			}
			return;
		}

		// Widget le plus en avant sous le pointeur (cf. HitTestIndex,
		// components.hpp) : un menu déroulé, un popup ou un panneau flottant
		// dessiné PAR-DESSUS le canvas doit garder le pointeur pour lui —
		// sinon le même clic active l'entrée de menu ET démarre un drag de
		// nœud derrière elle. Seuls les gestes DÉJÀ en cours (connexion,
		// drag, pan, marquee) ignorent ce blocage : ils ont capté la souris
		// au départ et doivent pouvoir sortir du canvas, comme n'importe quel
		// drag de widget (cf. InputSystem::Dispatch).
		const bool gestureInProgress = connectFromPin.Valid() || dragWaypointCanvas.Valid() || dragNode.Valid() ||
									   panCanvas.Valid() || marqueeCanvas.Valid();
		bool pointerBlocked = false;
		if (!gestureInProgress)
			hitIndex.Refresh(world, layout.PassCount());
		if (ecs::Entity front = gestureInProgress ? ecs::Entity{} : hitIndex.TopMostAt(world, sdl3::FPoint{MX, MY});
			front.Valid()) {
			pointerBlocked = true;
			world.Query<UiNodeGraphCanvas>([&](ecs::Entity ce, UiNodeGraphCanvas &) {
				if (pointerBlocked && IsDescendantOrSelf(world, front, ce))
					pointerBlocked = false;
			});
		}

		// ── Clic droit : menu contextuel (cf. NodeGraphCallbacks::onContextMenu) ─
		// L'ÉDITEUR ne construit aucun menu — il détecte juste le clic,
		// identifie le nœud/pin visé (invalide si fond vide) et délègue.
		if (ev.IsMouseDown(SDL_BUTTON_RIGHT)) {
			if (pointerBlocked)
				return;
			world.Query<UiNodeGraphCanvas, UiComputed>([&](ecs::Entity ce, UiNodeGraphCanvas &, UiComputed &c) {
				if (!c.screen.Contains({MX, MY}))
					return;
				auto cb = world.GetComponent<NodeGraphCallbacks>(ce);
				if (cb.IsNone() || !cb.Unwrap()->onContextMenu)
					return;
				ecs::Entity hitPin = HitTestPin(world, MX, MY);
				ecs::Entity hitNode{};
				world.Query<UiGraphNode, UiComputed>([&](ecs::Entity ne, UiGraphNode &, UiComputed &nc) {
					if (hitNode.Valid())
						return;
					if (nc.screen.Contains({MX, MY}))
						hitNode = ne;
				});
				cb.Unwrap()->onContextMenu(hitNode, hitPin, {MX, MY});
			});
			return;
		}

		// ── Survol des pins ──────────────────────────────────────────────────
		// Mis à jour sur chaque mouvement de souris (sauf pendant un drag de
		// nœud/pan en cours — le survol reste figé le temps du geste, comme
		// pour la plupart des widgets de ce module). Pas de garde
		// d'invalidation d'itérateur ici : seule mutation de champ, aucun
		// add/remove_component (cf. addGraphPin()).
		if (ev.IsMouseMotion() && !dragNode.Valid() && !panCanvas.Valid()) {
			// Pointeur capté par un widget dessiné au-dessus du canvas : plus
			// aucun pin ni nœud n'est survolé (surlignage éteint sous le
			// menu, plutôt que figé sur le dernier survol).
			ecs::Entity hoveredPin = pointerBlocked ? ecs::Entity{} : HitTestPin(world, MX, MY);
			world.Query<UiGraphPin>([&](ecs::Entity e, UiGraphPin &pin) { pin.hovered = (e == hoveredPin); });

			// Survol des nœuds (spec section 8, état "hover") — sur la boîte
			// ENTIÈRE du nœud (pas juste l'en-tête) : éclaircit l'en-tête au
			// survol même en pointant le corps, cf. DrawTitle/RenderHeaders().
			ecs::Entity hoveredNode{};
			if (!pointerBlocked)
				world.Query<UiGraphNode, UiComputed>([&](ecs::Entity e, UiGraphNode &, UiComputed &c) {
					if (!hoveredNode.Valid() && c.screen.Contains({MX, MY}))
						hoveredNode = e;
				});
			world.Query<UiGraphNode>([&](ecs::Entity e, UiGraphNode &node) { node.hovered = (e == hoveredNode); });
		}

		// ── Connexion pin→pin en cours ──────────────────────────────────────
		// Le point d'arrivée "élastique" est simplement mouseX/mouseY (mis
		// à jour ci-dessus) — RenderConnections() lit directement ces deux
		// membres pour dessiner le trait temporaire, pas besoin de le stocker
		// deux fois.
		if (connectFromPin.Valid()) {
			if (ev.IsMouseUp(SDL_BUTTON_LEFT)) {
				ecs::Entity target = HitTestPin(world, MX, MY);
				if (target.Valid() && target != connectFromPin)
					ConnectPins(world, connectFromPin, target);
				connectFromPin = ecs::Entity{};
			}
			return;
		}

		// ── Glisser d'un point intermédiaire en cours ───────────────────────
		// Ne touche JAMAIS LayoutSystem (les waypoints n'y participent pas,
		// cf. canvasToScreen()) — donc pas de layout.MarkDirty() ici,
		// contrairement au drag de nœud : RenderConnections() relit
		// conn.waypoints à chaque frame, la mise à jour est déjà "temps réel"
		// par construction (aucun cache à invalider).
		if (dragWaypointCanvas.Valid()) {
			if (ev.IsMouseMotion()) {
				if (auto cv = world.GetComponent<UiNodeGraphCanvas>(dragWaypointCanvas); cv.IsSome()) {
					float zoom = sdl3::Max(0.0001f, cv.Unwrap()->zoom);
					sdl3::FPoint canvasDelta{(MX - dragWaypointStartMouse.x) / zoom,
									   (MY - dragWaypointStartMouse.y) / zoom};
					auto &conns = cv.Unwrap()->connections;
					if (dragWaypointConn >= 0 && size_t(dragWaypointConn) < conns.size()) {
						auto &wps = conns[size_t(dragWaypointConn)].waypoints;
						if (dragWaypointIndex >= 0 && size_t(dragWaypointIndex) < wps.size())
							wps[size_t(dragWaypointIndex)] = {dragWaypointStartValue.x + canvasDelta.x,
																dragWaypointStartValue.y + canvasDelta.y};
					}
				}
			}
			if (ev.IsMouseUp(SDL_BUTTON_LEFT))
				dragWaypointCanvas = ecs::Entity{};
			return;
		}

		// ── Drag de nœud en cours (déplacement OU redimensionnement) ───────
		if (dragNode.Valid()) {
			if (ev.IsMouseMotion()) {
				auto node = world.GetComponent<UiGraphNode>(dragNode);
				auto parent = world.GetComponent<UiParent>(dragNode);
				if (node.IsSome() && parent.IsSome()) {
					if (auto canvas = world.GetComponent<UiNodeGraphCanvas>(parent.Unwrap()->parent);
						canvas.IsSome()) {
						float zoom = sdl3::Max(0.0001f, canvas.Unwrap()->zoom);
						sdl3::FPoint canvasDelta{(MX - dragStartMouse.x) / zoom, (MY - dragStartMouse.y) / zoom};
						if (dragMode == DragMode::MOVE) {
							// Déplacement de GROUPE : le même delta, appliqué depuis la
							// position de départ propre à CHAQUE nœud sélectionné (cf.
							// groupDragStart) — pas seulement le nœud "meneur" saisi.
							for (auto &[e, startPos] : groupDragStart)
								if (auto n = world.GetComponent<UiGraphNode>(e); n.IsSome())
									n.Unwrap()->canvasPos = {startPos.x + canvasDelta.x, startPos.y + canvasDelta.y};
						} else {
							node.Unwrap()->size = {
								sdl3::Max(node.Unwrap()->minWidth, dragStartValue.x + canvasDelta.x),
								sdl3::Max(node.Unwrap()->minHeight, dragStartValue.y + canvasDelta.y)};
						}
						layout.MarkDirty();
					}
				}
			}
			if (ev.IsMouseUp(SDL_BUTTON_LEFT)) {
				ecs::Entity canvasE = CanvasOfNode(world, dragNode);
				auto cb = world.GetComponent<NodeGraphCallbacks>(canvasE);
				if (dragMode == DragMode::MOVE) {
					if (cb.IsSome() && cb.Unwrap()->onNodeMoved)
						for (auto &[e, startPos] : groupDragStart)
							if (auto n = world.GetComponent<UiGraphNode>(e); n.IsSome())
								cb.Unwrap()->onNodeMoved(e, n.Unwrap()->canvasPos);
				} else if (cb.IsSome() && cb.Unwrap()->onNodeResized) {
					if (auto n = world.GetComponent<UiGraphNode>(dragNode); n.IsSome())
						cb.Unwrap()->onNodeResized(dragNode, n.Unwrap()->size);
				}
				if (auto node = world.GetComponent<UiGraphNode>(dragNode); node.IsSome())
					node.Unwrap()->draggingMove = node.Unwrap()->draggingResize = false;
				dragNode = ecs::Entity{};
				groupDragStart.clear();
			}
			return;
		}

		// ── Pan en cours (clic-milieu) ──────────────────────────────────────
		if (panCanvas.Valid()) {
			if (ev.IsMouseMotion()) {
				if (auto canvas = world.GetComponent<UiNodeGraphCanvas>(panCanvas); canvas.IsSome()) {
					float zoom = sdl3::Max(0.0001f, canvas.Unwrap()->zoom);
					canvas.Unwrap()->pan = {panStartPan.x - (MX - panStartMouse.x) / zoom,
											panStartPan.y - (MY - panStartMouse.y) / zoom};
					layout.MarkDirty();
				}
			}
			if (ev.IsMouseUp(SDL_BUTTON_MIDDLE))
				panCanvas = ecs::Entity{};
			return;
		}

		// ── Marquee (rectangle de sélection) en cours ───────────────────────
		// Pas de mutation ECS pendant le geste (le rectangle lui-même est
		// {marqueeStart, mouseX/Y}, lu directement par RenderMarquee()) —
		// la sélection ne se calcule qu'au relâchement.
		if (marqueeCanvas.Valid()) {
			if (ev.IsMouseUp(SDL_BUTTON_LEFT)) {
				if (auto cv = world.GetComponent<UiNodeGraphCanvas>(marqueeCanvas); cv.IsSome()) {
					sdl3::FRect marquee{sdl3::Min(marqueeStart.x, MX), sdl3::Min(marqueeStart.y, MY),
								 sdl3::Abs(MX - marqueeStart.x), sdl3::Abs(MY - marqueeStart.y)};
					if (!marqueeAdditive)
						cv.Unwrap()->selection.selected.clear();
					world.Query<UiGraphNode, UiParent, UiComputed>(
						[&](ecs::Entity e, UiGraphNode &node, UiParent &p, UiComputed &c) {
							if (p.parent == marqueeCanvas && !node.disabled && marquee.Intersects(c.screen))
								cv.Unwrap()->selection.selected.insert(e);
						});
					cv.Unwrap()->selection.anchor = ecs::Entity{};
					cv.Unwrap()->selection.lastClicked = ecs::Entity{};
					SyncSelectionVisuals(world, marqueeCanvas, *cv.Unwrap());
				}
				marqueeCanvas = ecs::Entity{};
			}
			return;
		}

		// ── Démarrage d'une interaction ─────────────────────────────────────
		// (tous les gestes en cours ont déjà rendu la main ci-dessus)
		if (pointerBlocked)
			return;
		if (ev.IsMouseDown(SDL_BUTTON_LEFT)) {
			// Un pin est prioritaire sur tout le reste (un pin peut visuellement
			// chevaucher la bande d'en-tête d'un nœud) — démarre un glisser de
			// connexion pin→pin plutôt qu'un drag de nœud.
			if (ecs::Entity hitPin = HitTestPin(world, MX, MY); hitPin.Valid()) {
				connectFromPin = hitPin;
				return;
			}
			// Poignée de point intermédiaire existante : double-clic la
			// supprime, simple clic la glisse (cf. spec section 5).
			{
				ecs::Entity hitCanvas{};
				ptrdiff_t hitConn = -1, hitWp = -1;
				sdl3::FPoint hitCanvasPt{};
				world.Query<UiNodeGraphCanvas, UiComputed>([&](ecs::Entity ce, UiNodeGraphCanvas &cv, UiComputed &c) {
					if (hitConn >= 0)
						return;
					for (size_t ci = 0; ci < cv.connections.size() && hitConn < 0; ++ci) {
						const auto &wps = cv.connections[ci].waypoints;
						for (size_t wi = 0; wi < wps.size(); ++wi) {
							sdl3::FPoint screen = CanvasToScreen(wps[wi], cv, c.screen);
							if (std::abs(MX - screen.x) <= 7.f && std::abs(MY - screen.y) <= 7.f) {
								hitCanvas = ce;
								hitConn = ptrdiff_t(ci);
								hitWp = ptrdiff_t(wi);
								hitCanvasPt = wps[wi];
								break;
							}
						}
					}
				});
				if (hitConn >= 0) {
					if (ev.MouseButton().clicks >= 2) {
						if (auto cv = world.GetComponent<UiNodeGraphCanvas>(hitCanvas); cv.IsSome())
							RemoveWaypoint(cv.Unwrap()->connections[size_t(hitConn)], size_t(hitWp));
					} else {
						dragWaypointCanvas = hitCanvas;
						dragWaypointConn = hitConn;
						dragWaypointIndex = hitWp;
						dragWaypointStartMouse = {MX, MY};
						dragWaypointStartValue = hitCanvasPt;
					}
					return;
				}
			}
			// Double-clic sur le TRACÉ d'une connexion (hors poignée existante) :
			// insère un nouveau point intermédiaire à l'endroit cliqué. Le test
			// de proximité utilise le POLYGONE DE CONTRÔLE (pin, waypoints...,
			// pin), pas la courbe échantillonnée réellement dessinée — approximation
			// volontaire (exacte pour droite/orthogonal, "assez proche" pour
			// Bézier avec un décalage de contrôle raisonnable) qui évite de
			// dupliquer la logique de découpage en segments de connectionPath().
			if (ev.MouseButton().clicks >= 2) {
				ecs::Entity hitCanvas{};
				ptrdiff_t hitConn = -1;
				size_t insertIndex = 0;
				sdl3::FRect hitCanvasScreen{};
				world.Query<UiNodeGraphCanvas, UiComputed>([&](ecs::Entity ce, UiNodeGraphCanvas &cv, UiComputed &c) {
					if (hitConn >= 0)
						return;
					for (size_t ci = 0; ci < cv.connections.size() && hitConn < 0; ++ci) {
						const GraphConnection &conn = cv.connections[ci];
						auto fromC = world.GetComponent<UiComputed>(conn.fromPin);
						auto toC = world.GetComponent<UiComputed>(conn.toPin);
						if (fromC.IsNone() || toC.IsNone())
							continue;
						std::vector<sdl3::FPoint> ctrl;
						ctrl.push_back(detail::PinCenter(fromC.Unwrap()->screen));
						for (sdl3::FPoint wp : conn.waypoints)
							ctrl.push_back(CanvasToScreen(wp, cv, c.screen));
						ctrl.push_back(detail::PinCenter(toC.Unwrap()->screen));
						for (size_t i = 0; i + 1 < ctrl.size(); ++i) {
							if (detail::DistanceToSegment({MX, MY}, ctrl[i], ctrl[i + 1]) <= 8.f) {
								hitCanvas = ce;
								hitConn = ptrdiff_t(ci);
								insertIndex = i;
								hitCanvasScreen = c.screen;
								break;
							}
						}
					}
				});
				if (hitConn >= 0) {
					if (auto cv = world.GetComponent<UiNodeGraphCanvas>(hitCanvas); cv.IsSome()) {
						sdl3::FPoint canvasPt = ScreenToCanvas({MX, MY}, *cv.Unwrap(), hitCanvasScreen);
						InsertWaypoint(cv.Unwrap()->connections[size_t(hitConn)], insertIndex, canvasPt);
					}
					return;
				}
			}
			// Poignée de redimensionnement (coin bas-droit) prioritaire sur le
			// header/déplacement (les deux zones peuvent se chevaucher visuellement).
			ecs::Entity hitResize{};
			world.Query<UiGraphNode, UiComputed>([&](ecs::Entity e, UiGraphNode &node, UiComputed &c) {
				if (hitResize.Valid() || !node.resizable || node.disabled)
					return;
				sdl3::FRect grip{c.screen.x + c.screen.w - 14.f, c.screen.y + c.screen.h - 14.f, 14.f, 14.f};
				if (grip.Contains({MX, MY}))
					hitResize = e;
			});
			if (hitResize.Valid()) {
				if (auto node = world.GetComponent<UiGraphNode>(hitResize); node.IsSome()) {
					node.Unwrap()->draggingResize = true;
					dragNode = hitResize;
					dragMode = DragMode::RESIZE;
					dragStartMouse = {MX, MY};
					dragStartValue = node.Unwrap()->size;
				}
				return;
			}
			ecs::Entity hitHeader{};
			world.Query<UiGraphNode, UiComputed>([&](ecs::Entity e, UiGraphNode &node, UiComputed &c) {
				if (hitHeader.Valid() || !node.movable || node.disabled)
					return;
				sdl3::FRect header{c.screen.x, c.screen.y, c.screen.w, node.headerHeight};
				if (header.Contains({MX, MY}))
					hitHeader = e;
			});
			if (hitHeader.Valid()) {
				ecs::Entity canvasE = CanvasOfNode(world, hitHeader);
				bool ctrlDown = (sdl3::keyboard::Mods() & SDL_KMOD_CTRL) != 0;
				bool shiftDown = (sdl3::keyboard::Mods() & SDL_KMOD_SHIFT) != 0;
				if (auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasE); cv.IsSome()) {
					// Cliquer un nœud DÉJÀ sélectionné (sans modificateur) ne
					// doit PAS réduire la sélection à ce seul nœud — sinon un
					// simple clic-glisser sur un groupe le briserait avant même
					// de pouvoir le déplacer ensemble (cf. Décision UX standard
					// des éditeurs de node-graph).
					bool alreadySelected = cv.Unwrap()->selection.selected.count(hitHeader) > 0;
					if (ctrlDown || shiftDown || !alreadySelected) {
						std::vector<ecs::Entity> order;
						if (auto ch = world.GetComponent<UiChildren>(canvasE); ch.IsSome())
							order = ch.Unwrap()->list;
						ApplyNodeSelectionClick(cv.Unwrap()->selection, hitHeader, ctrlDown, shiftDown, order);
						SyncSelectionVisuals(world, canvasE, *cv.Unwrap());
					}
					// Ctrl/Maj = geste de sélection pur (bascule/plage), pas de
					// déplacement — seul un clic simple démarre un drag.
					if (!ctrlDown && !shiftDown) {
						if (auto node = world.GetComponent<UiGraphNode>(hitHeader); node.IsSome())
							node.Unwrap()->draggingMove = true;
						dragNode = hitHeader;
						dragMode = DragMode::MOVE;
						dragStartMouse = {MX, MY};
						groupDragStart.clear();
						for (ecs::Entity sel : cv.Unwrap()->selection.selected)
							if (auto n = world.GetComponent<UiGraphNode>(sel); n.IsSome())
								groupDragStart.push_back({sel, n.Unwrap()->canvasPos});
					}
				}
				return;
			}
			// Fond vide du canvas : démarre un rectangle de sélection (marquee,
			// cf. Décision #7) — Ctrl maintenu ÉTEND la sélection existante au
			// relâchement au lieu de la remplacer.
			world.Query<UiNodeGraphCanvas, UiComputed>([&](ecs::Entity e, UiNodeGraphCanvas &, UiComputed &c) {
				if (marqueeCanvas.Valid() || !c.screen.Contains({MX, MY}))
					return;
				marqueeCanvas = e;
				marqueeStart = {MX, MY};
				marqueeAdditive = (sdl3::keyboard::Mods() & SDL_KMOD_CTRL) != 0;
			});
		} else if (ev.IsMouseDown(SDL_BUTTON_MIDDLE)) {
			world.Query<UiNodeGraphCanvas, UiComputed>([&](ecs::Entity e, UiNodeGraphCanvas &cv, UiComputed &c) {
				if (panCanvas.Valid() || !c.screen.Contains({MX, MY}))
					return;
				panCanvas = e;
				panStartMouse = {MX, MY};
				panStartPan = cv.pan;
			});
		} else if (ev.IsMouseWheel()) {
			world.Query<UiNodeGraphCanvas, UiComputed>([&](ecs::Entity, UiNodeGraphCanvas &cv, UiComputed &c) {
				if (!c.screen.Contains({MX, MY}))
					return;
				float factor = ev.MouseWheel().y > 0.f ? 1.1f : (1.f / 1.1f);
				float newZoom = sdl3::Clamp(cv.zoom * factor, cv.minZoom, cv.maxZoom);
				// Zoom vers le curseur : le point canvas sous la souris reste fixe à l'écran.
				float oldZoom = sdl3::Max(0.0001f, cv.zoom);
				sdl3::FPoint mouseCanvas{cv.pan.x + (MX - c.screen.x) / oldZoom, cv.pan.y + (MY - c.screen.y) / oldZoom};
				cv.pan = {mouseCanvas.x - (MX - c.screen.x) / newZoom, mouseCanvas.y - (MY - c.screen.y) / newZoom};
				cv.zoom = newZoom;
				layout.MarkDirty();
			});
		}
	}

	/// Fond quadrillé — à appeler AVANT gui.Render() (cf. en-tête du
	/// fichier) : le canvas lui-même reste sans UiPanel opaque pour laisser
	/// cette grille visible en dessous des nœuds (dessinés normalement par
	/// RenderSystem juste après).
	void RenderGrid(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren) {
		world.Query<UiNodeGraphCanvas, UiComputed>([&](ecs::Entity, UiNodeGraphCanvas &cv, UiComputed &c) {
			const sdl3::FRect &s = c.screen;
			if (s.w <= 0.f || s.h <= 0.f)
				return;
			ren.SetClipRect(sdl3::Rect{int(s.x), int(s.y), int(s.w) + 1, int(s.h) + 1});
			ren.SetDrawColor(cv.bgColor);
			ren.FillRect(s);

			float step = cv.gridSpacing * cv.zoom;
			if (step > 3.f) { // sous ~3px, la grille ne ferait plus que du bruit visuel
				// Décalage pour que la grille suive le pan (ligne à x=0 canvas visible).
				float offX = sdl3::Fmod(-cv.pan.x * cv.zoom, step);
				float offY = sdl3::Fmod(-cv.pan.y * cv.zoom, step);
				if (offX < 0.f)
					offX += step;
				if (offY < 0.f)
					offY += step;
				int ix = int((-cv.pan.x * cv.zoom - offX) / step + 0.5f); // index de ligne pour le "major every"
				for (float x = s.x + offX; x < s.x + s.w; x += step, ++ix) {
					ren.SetDrawColor((ix % cv.gridMajorEvery == 0) ? cv.gridColorMajor : cv.gridColor);
					ren.DrawLine(x, s.y, x, s.y + s.h);
				}
				int iy = int((-cv.pan.y * cv.zoom - offY) / step + 0.5f);
				for (float y = s.y + offY; y < s.y + s.h; y += step, ++iy) {
					ren.SetDrawColor((iy % cv.gridMajorEvery == 0) ? cv.gridColorMajor : cv.gridColor);
					ren.DrawLine(s.x, y, s.x + s.w, y);
				}
			}
			ren.ClearClipRect();
		});
	}

	/// Connexions (traits reliant deux pins) — à appeler APRÈS RenderGrid()
	/// et AVANT gui.Render() (cf. en-tête du fichier) : les traits doivent
	/// passer visuellement SOUS le corps opaque des nœuds, pas par-dessus.
	/// Dessine aussi le trait "élastique" temporaire pendant un glisser
	/// pin→pin en cours (cf. handleEvent()).
	void RenderConnections(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren) {
		world.Query<UiNodeGraphCanvas, UiComputed>([&](ecs::Entity canvasE, UiNodeGraphCanvas &cv, UiComputed &c) {
			if (c.screen.w <= 0.f || c.screen.h <= 0.f)
				return;
			ren.SetClipRect(sdl3::Rect{int(c.screen.x), int(c.screen.y), int(c.screen.w) + 1, int(c.screen.h) + 1});
			for (size_t ci = 0; ci < cv.connections.size(); ++ci) {
				const GraphConnection &conn = cv.connections[ci];
				auto fromC = world.GetComponent<UiComputed>(conn.fromPin);
				auto toC = world.GetComponent<UiComputed>(conn.toPin);
				if (fromC.IsNone() || toC.IsNone())
					continue;
				sdl3::FPoint a = detail::PinCenter(fromC.Unwrap()->screen);
				sdl3::FPoint b = detail::PinCenter(toC.Unwrap()->screen);
				std::vector<sdl3::FPoint> throughScreen;
				throughScreen.reserve(conn.waypoints.size());
				for (sdl3::FPoint wp : conn.waypoints)
					throughScreen.push_back(CanvasToScreen(wp, cv, c.screen));
				auto path = detail::ConnectionPath(a, throughScreen, b, conn.style, conn.bezierControlOffset, cv.zoom);
				if (conn.onCustomDraw && ren.NativeRenderer()) {
					conn.onCustomDraw(*ren.NativeRenderer(), path, conn);
				} else {
					DrawDefaultConnection(ren, conn, path, cv.zoom);
				}
				for (size_t wi = 0; wi < throughScreen.size(); ++wi) {
					bool dragging = dragWaypointCanvas == canvasE && dragWaypointConn == ptrdiff_t(ci) &&
									dragWaypointIndex == ptrdiff_t(wi);
					DrawWaypointHandle(ren, throughScreen[wi], dragging);
				}
			}
			if (connectFromPin.Valid() && CanvasOfPin(world, connectFromPin) == canvasE) {
				if (auto fromC = world.GetComponent<UiComputed>(connectFromPin); fromC.IsSome()) {
					sdl3::FPoint a = detail::PinCenter(fromC.Unwrap()->screen);
					auto path = detail::ConnectionPath(a, {mouseX, mouseY}, ConnectionStyle::BEZIER, 0.f, cv.zoom);
					GraphConnection temp;
					temp.color = sdl3::FColor{255/255.f, 255/255.f, 255/255.f, 160/255.f};
					temp.thickness = 2.f;
					temp.endCap = ConnectionEndCap::NONE;
					DrawDefaultConnection(ren, temp, path, cv.zoom);
				}
			}
			ren.ClearClipRect();
		});
	}

	/// Bandeaux de titre des nœuds — à appeler APRÈS gui.Render() (cf.
	/// en-tête du fichier), pour dessiner par-dessus le corps déjà rendu du
	/// nœud. LIMITATION CONNUE (Phase 1) : dessinés dans une passe séparée de
	/// TOUS les corps de nœuds, donc l'en-tête d'un nœud en arrière-plan
	/// peut chevaucher le CONTENU d'un nœud au premier plan si les deux se
	/// superposent visuellement — acceptable pour cette phase (rare en usage
	/// normal), à revoir si un vrai z-ordering par nœud devient nécessaire.
	void RenderHeaders(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren) {
		world.Query<UiGraphNode, UiComputed>([&](ecs::Entity, UiGraphNode &node, UiComputed &c) {
			const sdl3::FRect &s = c.screen;
			if (s.w <= 0.f || s.h <= 0.f)
				return;
			sdl3::FRect header{s.x, s.y, s.w, sdl3::Min(node.headerHeight, s.h)};
			ren.SetClipRect(sdl3::Rect{int(s.x), int(s.y), int(s.w) + 1, int(s.h) + 1});
			// États visuels (spec section 8) : désactivé grisé prioritaire sur
			// survol/actif (un nœud désactivé ne réagit à rien) ; sinon un
			// glisser en cours (actif) ou le survol éclaircissent l'en-tête,
			// même lambda `lighten` que drawDefaultPin()/drawDefaultConnection().
			sdl3::FColor hc = node.headerColor;
			if (node.disabled) {
				hc.a = hc.a / 2.f;
			} else if (node.draggingMove || node.draggingResize) {
				hc.Lighten(0.215);
			} else if (node.hovered) {
				hc.Lighten(0.098);
			}
			ren.SetDrawColor(hc);
			ren.FillRect(header);
			DrawTitle(ren, node.title, header);
			if (node.selected) {
				ren.SetDrawColor(sdl3::FColor{1.f, 200/255.f, 60/255.f, 1.f});
				ren.DrawRect({s.x + 0.5f, s.y + 0.5f, s.w - 1.f, s.h - 1.f});
			}
			if (node.resizable) {
				sdl3::FColor gripColor{1.f, 1.f, 1.f, node.draggingResize ? 200/255.f : 90/255.f};
				ren.SetDrawColor(gripColor);
				float gx = s.x + s.w - 4.f, gy = s.y + s.h - 4.f;
				for (int i = 1; i <= 3; ++i)
					ren.DrawLine(gx - float(i) * 3.f, gy, gx, gy - float(i) * 3.f);
			}
			ren.ClearClipRect();
		});
	}

	/// Rendu des pins — à appeler APRÈS RenderHeaders() (cf. en-tête du
	/// fichier) : un pin doit rester cliquable/visible même s'il chevauche
	/// visuellement un en-tête de nœud voisin. Pas de SetClipRect ici (les
	/// pins débordent volontairement du bord du nœud, contrairement au
	/// contenu/en-tête qui sont clippés à sa boîte).
	void RenderPins(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren) {
		world.Query<UiGraphPin, UiComputed>([&](ecs::Entity, UiGraphPin &pin, UiComputed &c) {
			if (c.screen.w <= 0.f)
				return;
			if (pin.onCustomDraw) {
				if (auto *nr = ren.NativeRenderer()) {
					pin.onCustomDraw(*nr, c.screen, pin);
					return;
				}
			}
			DrawDefaultPin(ren, pin, c.screen);
		});
	}

	/// Rectangle de sélection (marquee) en cours — à appeler en DERNIER (cf.
	/// en-tête du fichier), par-dessus tout le reste. No-op si aucun marquee
	/// actif.
	void RenderMarquee(IUiRenderBackend &ren) {
		if (!marqueeCanvas.Valid())
			return;
		sdl3::FRect r{sdl3::Min(marqueeStart.x, mouseX), sdl3::Min(marqueeStart.y, mouseY),
			   sdl3::Abs(mouseX - marqueeStart.x), sdl3::Abs(mouseY - marqueeStart.y)};
		ren.SetDrawColor(sdl3::FColor{120/255.f, 170/255.f, 1.f, 60/255.f});
		ren.FillRect(r);
		ren.SetDrawColor(sdl3::FColor{140/255.f, 190/255.f, 1.f, 220/255.f});
		ren.DrawRect(r);
	}

private:
	enum class DragMode { MOVE, RESIZE };
	float mouseX = 0.f, mouseY = 0.f;
	/// Ordre de dessin mémorisé (cf. HitTestIndex) — reconstruit seulement
	/// quand le layout re-tourne, jamais à chaque évènement.
	HitTestIndex hitIndex;

	ecs::Entity dragNode{};
	DragMode dragMode = DragMode::MOVE;
	sdl3::FPoint dragStartMouse{}, dragStartValue{};
	/// Position CANVAS de départ de CHAQUE nœud sélectionné, capturée au
	/// press (mode Move uniquement — le redimensionnement reste single-nœud,
	/// cf. handleEvent()) : le déplacement de groupe applique le MÊME delta
	/// écran/zoom à chacun depuis sa propre position de départ, plutôt que de
	/// dériver les autres nœuds de dragStartValue (qui reste celle du nœud
	/// "meneur" uniquement).
	std::vector<std::pair<ecs::Entity, sdl3::FPoint>> groupDragStart;
	ecs::Entity panCanvas{};
	sdl3::FPoint panStartMouse{}, panStartPan{};
	/// Marquee (rectangle de sélection, cf. Décision #7) — actif entre un
	/// clic gauche sur fond vide du canvas et son relâchement.
	ecs::Entity marqueeCanvas{};
	sdl3::FPoint marqueeStart{};
	bool marqueeAdditive = false;
	/// Presse-papiers interne (couper/copier/coller, cf. ClipboardNode).
	std::vector<ClipboardNode> clipboard;
	/// Pin d'origine d'un glisser de connexion en cours (invalide = aucun),
	/// cf. handleEvent()/RenderConnections(). Le point d'arrivée "élastique"
	/// est simplement mouseX/mouseY, pas besoin d'un champ séparé.
	ecs::Entity connectFromPin{};
	/// Point intermédiaire en cours de glisser (référencé par INDEX, pas par
	/// ecs::Entity — les waypoints n'en ont pas, cf. GraphConnection::waypoints ;
	/// cette référence indexée ne survit qu'à l'intérieur d'un seul geste
	/// continu, pendant lequel rien d'autre ne peut modifier la liste des
	/// connexions/waypoints dans ce modèle synchrone). -1 = aucun glisser.
	ecs::Entity dragWaypointCanvas{};
	ptrdiff_t dragWaypointConn = -1, dragWaypointIndex = -1;
	sdl3::FPoint dragWaypointStartMouse{}, dragWaypointStartValue{};

	sdl3::TextEngine *m_engine = nullptr;
	sdl3::Font *m_font = nullptr;
	// Cache clé-chaîne (comme RenderSystem::strCache) : les titres sont peu
	// nombreux et changent rarement, une clé par titre distinct suffit.
	struct CachedTitle {
		sdl3::FColor color;
		sdl3::Text text;
	};
	std::unordered_map<String, CachedTitle> titleCache;

	/// Répercute `cv.selection.selected` sur `UiGraphNode::selected` pour
	/// chaque nœud du canvas (rendu du contour de sélection, cf.
	/// RenderHeaders()), puis déclenche `NodeGraphCallbacks::onSelectionChanged`
	/// si le canvas en porte un — point de sortie UNIQUE de toute mutation de
	/// sélection (clic, marquee, API programmatique), pour que ces deux
	/// effets de bord ne se dupliquent jamais entre les appelants.
	void SyncSelectionVisuals(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity, const UiNodeGraphCanvas &cv) {
		world.Query<UiGraphNode, UiParent>([&](ecs::Entity e, UiGraphNode &node, UiParent &p) {
			if (p.parent == canvasEntity)
				node.selected = cv.selection.selected.count(e) > 0;
		});
		if (auto cb = world.GetComponent<NodeGraphCallbacks>(canvasEntity);
			cb.IsSome() && cb.Unwrap()->onSelectionChanged)
			cb.Unwrap()->onSelectionChanged(cv.selection.selected);
	}

	/// Texte du titre, centré verticalement, aligné à gauche avec une petite
	/// marge — même esprit que RenderSystem::drawTextCentered mais autonome
	/// (NodeGraphSystem n'a pas accès aux méthodes privées de RenderSystem).
	void DrawTitle(IUiRenderBackend &ren, const String &title, const sdl3::FRect &box) {
		(void)ren;
		if (!m_engine || !m_font || title.IsEmpty())
			return;
		sdl3::FColor color{240/255.f, 242/255.f, 248/255.f, 1.f};
		String key(title.c_str());
		auto it = titleCache.find(key);
		if (it == titleCache.end()) {
			auto res = sdl3::Text::Create(*m_engine, *m_font, title);
			if (!res)
				return;
			it = titleCache.insert_or_assign(std::move(key), CachedTitle{color, std::move(res.Value())}).first;
			it->second.text.SetColor(color);
		}
		sdl3::FPoint sz{0.f, 0.f};
		if (auto s = it->second.text.GetSize(); s.IsSome())
			sz = {float(s.Unwrap().x), float(s.Unwrap().y)};
		float y = box.y + (box.h - sz.y) * 0.5f;
		it->second.text.Draw(box.x + 8.f, y);
	}

	/// Rendu par défaut d'un pin (cf. spec section 3 : forme/couleur/état) —
	/// REMPLI si `connected`, contour seul sinon (convention courante des
	/// éditeurs de node-graph : un pin non connecté se distingue au premier
	/// coup d'œil). Le survol éclaircit la couleur ; la sélection ajoute un
	/// anneau doré (même couleur que la sélection de nœud, pour cohérence
	/// visuelle — cf. RenderHeaders()).
	void DrawDefaultPin(IUiRenderBackend &ren, const UiGraphPin &pin, const sdl3::FRect &box) {
		sdl3::FPoint center{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
		float r = box.w * 0.5f;
		sdl3::FColor col = pin.color;
		if (pin.hovered) {
			col.Lighten(0.176);
		}
		ren.SetDrawColor(col);
		switch (pin.shape) {
		case PinShape::CIRCLE:
			if (pin.connected)
				ren.FillCircle(center, r);
			else
				ren.DrawCircle(center, r);
			break;
		case PinShape::SQUARE:
			if (pin.connected)
				ren.FillRect(box);
			else
				ren.DrawRect(box);
			break;
		case PinShape::TRIANGLE: {
			std::array<sdl3::FPoint, 3> tri{sdl3::FPoint{center.x, box.y}, sdl3::FPoint{box.x + box.w, box.y + box.h},
									  sdl3::FPoint{box.x, box.y + box.h}};
			if (pin.connected)
				ren.FillPolygon(tri);
			else
				ren.DrawPolygon(tri);
			break;
		}
		case PinShape::STAR: {
			auto pts = detail::StarPoints(center, r, r * 0.45f, 5);
			if (pin.connected)
				ren.FillPolygon(pts);
			else
				ren.DrawPolygon(pts);
			break;
		}
		}
		if (pin.selected) {
			ren.SetDrawColor(sdl3::FColor{1.f, 200/255.f, 60/255.f, 1.f});
			ren.DrawCircle(center, r + 3.f);
		}
	}

	/// Ajoute un quad (2 triangles) formant un segment de ruban épais entre
	/// `a` et `b`, de demi-largeur `halfW`, perpendiculaire à la direction du
	/// segment — cf. Décision #6 du plan ("l'épaisseur de connexion se rend
	/// via un petit quad, pas plusieurs lignes parallèles").
	static void AppendThickSegment(std::vector<sdl3::Vertex> &verts, sdl3::FPoint a, sdl3::FPoint b, float halfW,
								   sdl3::FColor color) {
		sdl3::FPoint dir{b.x - a.x, b.y - a.y};
		float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
		if (len < 0.0001f)
			return;
		sdl3::FPoint n{-dir.y / len * halfW, dir.x / len * halfW};
		sdl3::Vertex a0{{a.x + n.x, a.y + n.y}, color, {0.f, 0.f}};
		sdl3::Vertex a1{{a.x - n.x, a.y - n.y}, color, {0.f, 0.f}};
		sdl3::Vertex b0{{b.x + n.x, b.y + n.y}, color, {0.f, 0.f}};
		sdl3::Vertex b1{{b.x - n.x, b.y - n.y}, color, {0.f, 0.f}};
		verts.insert(verts.end(), {a0, a1, b0, a1, b1, b0});
	}

	/// Triangle plein orienté selon la tangente locale `from`→`tip` — `tip`
	/// est la pointe (généralement une extrémité de connexion).
	static void DrawArrowHead(IUiRenderBackend &ren, sdl3::FPoint from, sdl3::FPoint tip, float size, sdl3::FColor color) {
		sdl3::FPoint dir{tip.x - from.x, tip.y - from.y};
		float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
		if (len < 0.0001f)
			return;
		sdl3::FPoint u{dir.x / len, dir.y / len};
		sdl3::FPoint n{-u.y, u.x};
		sdl3::FPoint back{tip.x - u.x * size, tip.y - u.y * size};
		std::array<sdl3::FPoint, 3> tri{tip, sdl3::FPoint{back.x + n.x * size * 0.5f, back.y + n.y * size * 0.5f},
								  sdl3::FPoint{back.x - n.x * size * 0.5f, back.y - n.y * size * 0.5f}};
		ren.SetDrawColor(color);
		ren.FillPolygon(tri);
	}

	/// Rendu par défaut d'une connexion : ruban épais (cf. appendThickSegment)
	/// le long de `path` (déjà résolu selon le style — droite/orthogonal/
	/// Bézier échantillonnée), petits disques aux articulations internes pour
	/// combler les coutures entre segments, flèche(s) optionnelle(s) aux
	/// extrémités (cf. GraphConnection::startCap/endCap).
	void DrawDefaultConnection(IUiRenderBackend &ren, const GraphConnection &conn, std::span<const sdl3::FPoint> path,
							   float zoom) {
		if (path.size() < 2)
			return;
		sdl3::FColor col = conn.color;
		col.a = sdl3::Clamp(col.a * conn.opacity, 0.f, 1.f);
		ren.SetDrawColor(col);
		sdl3::FColor fcol = ren.GetDrawColorFloat();
		float halfW = sdl3::Max(0.5f, conn.thickness * zoom * 0.5f);

		std::vector<sdl3::Vertex> verts;
		verts.reserve((path.size() - 1) * 6);
		for (size_t i = 0; i + 1 < path.size(); ++i)
			AppendThickSegment(verts, path[i], path[i + 1], halfW, fcol);
		ren.RenderGeometry(verts);
		if (path.size() > 2) {
			ren.SetDrawColor(col);
			for (size_t i = 1; i + 1 < path.size(); ++i)
				ren.FillCircle(path[i], halfW);
		}

		float arrow = conn.arrowSize * zoom;
		if (conn.endCap == ConnectionEndCap::ARROW)
			DrawArrowHead(ren, path[path.size() - 2], path.back(), arrow, col);
		if (conn.startCap == ConnectionEndCap::ARROW)
			DrawArrowHead(ren, path[1], path.front(), arrow, col);
	}

	/// Poignée visuelle d'un point intermédiaire (cf. spec section 5 :
	/// "poignées visuelles") — petit carré, plus grand/clair pendant le
	/// glisser pour un retour visuel clair.
	void DrawWaypointHandle(IUiRenderBackend &ren, sdl3::FPoint screenPos, bool dragging) {
		float half = dragging ? 6.f : 4.5f;
		sdl3::FRect box{screenPos.x - half, screenPos.y - half, half * 2.f, half * 2.f};
		ren.SetDrawColor(dragging ? sdl3::FColor{1.f, 220/255.f, 120/255.f, 1.f} : sdl3::FColor{230/255.f, 232/255.f, 238/255.f, 255/255.f});
		ren.FillRect(box);
		ren.SetDrawColor(sdl3::FColor{20/255.f, 21/255.f, 28/255.f, 1.f});
		ren.DrawRect(box);
	}
};

// ============================================================================
// Persistance — data::Node (cf. Décision #8 du plan : réutilise l'arbre
// commun data:: déjà existant plutôt qu'un nouveau format ; le CHOIX du
// format texte final (JSON le plus probable, mais XML/YAML/TOML tout aussi
// possibles via data.hpp) reste à l'appelant — ces fonctions ne produisent/
// ne consomment que l'arbre intermédiaire.
// ============================================================================

namespace detail {
[[nodiscard]] inline int64_t PackColor(sdl3::FColor c) noexcept {
	sdl3::Color b = c.ToByte();
	return (int64_t(b.r) << 24) | (int64_t(b.g) << 16) | (int64_t(b.b) << 8) | int64_t(b.a);
}
[[nodiscard]] inline sdl3::Color UnpackColor(int64_t v) noexcept {
	return {uint8_t((v >> 24) & 0xFF), uint8_t((v >> 16) & 0xFF), uint8_t((v >> 8) & 0xFF), uint8_t(v & 0xFF)};
}
[[nodiscard]] inline data::NodePtr PointNode(sdl3::FPoint p) {
	auto n = data::Node::MakeObject();
	n->Set("x", data::Node::MakeFloat(double(p.x)));
	n->Set("y", data::Node::MakeFloat(double(p.y)));
	return n;
}
[[nodiscard]] inline sdl3::FPoint PointFrom(const data::NodePtr &n) {
	if (!n)
		return {};
	auto x = n->Get("x"), y = n->Get("y");
	return {x ? float(x->floatValue) : 0.f, y ? float(y->floatValue) : 0.f};
}
} // namespace detail

/// Sérialise l'état complet d'un canvas (spec section 10) : zoom/pan, nœuds
/// (position/taille/couleur/contraintes/inner-zoom + leurs pins), connexions
/// (style/couleur/épaisseur/caps/waypoints), sélection. Le CONTENU des nœuds
/// n'est PAS sérialisé — même limitation documentée que ClipboardNode
/// (Phase 6) : l'éditeur ne connaît pas le contenu arbitraire fourni par
/// l'utilisateur, il ne peut donc pas le (dé)sérialiser génériquement. Les
/// identités ECS (ecs::Entity, éphémères) ne sont JAMAIS écrites telles quelles —
/// des clés de chaîne séquentielles ("n0", "p0", ...) assurent un document
/// stable indépendant de l'ordre/valeur interne des ecs::Entity, cohérent au sein
/// de CE document (pas besoin d'être stable entre deux appels).
[[nodiscard]] inline data::NodePtr SerializeGraph(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity) {
	auto root = data::Node::MakeObject();
	auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasEntity);
	if (cv.IsNone())
		return root;

	root->Set("pan", detail::PointNode(cv.Unwrap()->pan));
	root->Set("zoom", data::Node::MakeFloat(double(cv.Unwrap()->zoom)));

	std::unordered_map<uint32_t, String> nodeKeyOf, pinKeyOf;
	int pinCounter = 0;
	auto nodesArr = data::Node::MakeArray();
	auto selArr = data::Node::MakeArray();

	world.Query<UiGraphNode, UiParent>([&](ecs::Entity e, UiGraphNode &node, UiParent &p) {
		if (p.parent != canvasEntity)
			return;
		String key = "n" + String::From(nodeKeyOf.size());
		nodeKeyOf[e.id] = key;

		auto n = data::Node::MakeObject();
		n->Set("key", data::Node::MakeString(key));
		n->Set("title", data::Node::MakeString(String(node.title.c_str())));
		n->Set("pos", detail::PointNode(node.canvasPos));
		n->Set("size", detail::PointNode(node.size));
		n->Set("header_color", data::Node::MakeInt(detail::PackColor(node.headerColor)));
		n->Set("resizable", data::Node::MakeBool(node.resizable));
		n->Set("movable", data::Node::MakeBool(node.movable));
		n->Set("disabled", data::Node::MakeBool(node.disabled));
		n->Set("selected", data::Node::MakeBool(node.selected));
		n->Set("min_w", data::Node::MakeFloat(double(node.minWidth)));
		n->Set("min_h", data::Node::MakeFloat(double(node.minHeight)));
		n->Set("header_h", data::Node::MakeFloat(double(node.headerHeight)));

		float innerZoom = 1.f;
		if (auto ch = world.GetComponent<UiChildren>(e); ch.IsSome() && !ch.Unwrap()->list.empty())
			if (auto s = world.GetComponent<UiStyle>(ch.Unwrap()->list.front()); s.IsSome())
				if (const auto *iz = s.Unwrap()->Get<prop::InnerZoom>())
					innerZoom = iz->value;
		n->Set("inner_zoom", data::Node::MakeFloat(double(innerZoom)));

		auto pinsArr = data::Node::MakeArray();
		world.Query<UiGraphPin>([&](ecs::Entity pe, UiGraphPin &pin) {
			if (pin.node != e)
				return;
			String pkey = "p" + String::From(pinCounter++);
			pinKeyOf[pe.id] = pkey;
			auto pn = data::Node::MakeObject();
			pn->Set("key", data::Node::MakeString(pkey));
			pn->Set("shape", data::Node::MakeInt(int64_t(pin.shape)));
			pn->Set("side", data::Node::MakeInt(int64_t(pin.side)));
			pn->Set("side_offset", data::Node::MakeFloat(double(pin.sideOffset)));
			pn->Set("size", data::Node::MakeFloat(double(pin.size)));
			pn->Set("color", data::Node::MakeInt(detail::PackColor(pin.color)));
			pn->Set("connectable", data::Node::MakeBool(pin.connectable));
			pinsArr->Push(pn);
		});
		n->Set("pins", pinsArr);
		nodesArr->Push(n);

		if (node.selected)
			selArr->Push(data::Node::MakeString(key));
	});
	root->Set("nodes", nodesArr);
	root->Set("selection", selArr);

	auto connsArr = data::Node::MakeArray();
	for (const GraphConnection &c : cv.Unwrap()->connections) {
		auto fromIt = pinKeyOf.find(c.fromPin.id), toIt = pinKeyOf.find(c.toPin.id);
		if (fromIt == pinKeyOf.end() || toIt == pinKeyOf.end())
			continue; // pin déjà détruit (données incohérentes) : ignorée plutôt qu'un document cassé
		auto cn = data::Node::MakeObject();
		cn->Set("from", data::Node::MakeString(fromIt->second));
		cn->Set("to", data::Node::MakeString(toIt->second));
		cn->Set("color", data::Node::MakeInt(detail::PackColor(c.color)));
		cn->Set("thickness", data::Node::MakeFloat(double(c.thickness)));
		cn->Set("opacity", data::Node::MakeFloat(double(c.opacity)));
		cn->Set("style", data::Node::MakeInt(int64_t(c.style)));
		cn->Set("start_cap", data::Node::MakeInt(int64_t(c.startCap)));
		cn->Set("end_cap", data::Node::MakeInt(int64_t(c.endCap)));
		cn->Set("arrow_size", data::Node::MakeFloat(double(c.arrowSize)));
		cn->Set("bezier_control_offset", data::Node::MakeFloat(double(c.bezierControlOffset)));
		auto wps = data::Node::MakeArray();
		for (sdl3::FPoint wp : c.waypoints)
			wps->Push(detail::PointNode(wp));
		cn->Set("waypoints", wps);
		connsArr->Push(cn);
	}
	root->Set("connections", connsArr);
	return root;
}

/// Reconstruit un graphe depuis un document produit par serializeGraph() —
/// spawn de nouveaux nœuds/pins (via `f`/`world`), connexions recréées via
/// connectPins() (donc re-validées : deux pins déjà connectés ou invalides
/// dans le document ne planteraient pas, cf. sa propre doc), sélection
/// restaurée via `nodeGraph` (pour que les hooks/visuels restent cohérents,
/// cf. NodeGraphSystem::SelectNode()). `content` des nœuds reste VIDE (même
/// limitation que pasteClipboard() — cf. sa doc) : à l'appelant de repeupler
/// si besoin (ex: reconstruire depuis un type de nœud stocké séparément).
/// Retourne les nœuds créés (clé document -> ecs::Entity), pour un usage avancé.
inline std::unordered_map<String, ecs::Entity> DeserializeGraph(UiFactory &f, NodeGraphSystem &nodeGraph,
																 LayoutSystem &layout, ecs::Entity canvasEntity,
																 const data::NodePtr &root) {
	std::unordered_map<String, ecs::Entity> nodeByKey;
	std::unordered_map<String, ecs::Entity> pinByKey;
	if (!root)
		return nodeByKey;

	ecs::ArchetypeRegistry &world = f.World();
	if (auto cv = world.GetComponent<UiNodeGraphCanvas>(canvasEntity); cv.IsSome()) {
		if (auto pan = root->Get("pan"))
			cv.Unwrap()->pan = detail::PointFrom(pan);
		if (auto zoom = root->Get("zoom"))
			cv.Unwrap()->zoom = float(zoom->floatValue);
	}

	if (auto nodesArr = root->Get("nodes")) {
		for (size_t i = 0; i < nodesArr->GetSize(); ++i) {
			auto n = nodesArr->At(i);
			if (!n)
				continue;
			String title = n->Get("title") ? String(n->Get("title")->stringValue.c_str()) : String();
			sdl3::FPoint pos = detail::PointFrom(n->Get("pos"));
			sdl3::FPoint size = n->Get("size") ? detail::PointFrom(n->Get("size")) : sdl3::FPoint{200.f, 120.f};
			sdl3::FColor headerColor = n->Get("header_color") ? detail::UnpackColor(n->Get("header_color")->intValue)
														: sdl3::FColor::UI_NODE_HEADER_BLUE();
			float innerZoom = n->Get("inner_zoom") ? float(n->Get("inner_zoom")->floatValue) : 1.f;

			auto content = f.Column();
			ecs::Entity e = AddGraphNode(f, canvasEntity, title, pos, size, std::move(content), headerColor, innerZoom);
			if (auto node = world.GetComponent<UiGraphNode>(e); node.IsSome()) {
				if (auto v = n->Get("resizable"))
					node.Unwrap()->resizable = v->boolValue;
				if (auto v = n->Get("movable"))
					node.Unwrap()->movable = v->boolValue;
				if (auto v = n->Get("disabled"))
					node.Unwrap()->disabled = v->boolValue;
				if (auto v = n->Get("min_w"))
					node.Unwrap()->minWidth = float(v->floatValue);
				if (auto v = n->Get("min_h"))
					node.Unwrap()->minHeight = float(v->floatValue);
				if (auto v = n->Get("header_h"))
					node.Unwrap()->headerHeight = float(v->floatValue);
			}
			String key = n->Get("key") ? n->Get("key")->stringValue : ("n" + String::From(i));
			nodeByKey[key] = e;

			if (auto pinsArr = n->Get("pins")) {
				for (size_t j = 0; j < pinsArr->GetSize(); ++j) {
					auto pn = pinsArr->At(j);
					if (!pn)
						continue;
					PinSide side = pn->Get("side") ? PinSide(pn->Get("side")->intValue) : PinSide::Left;
					float sideOffset = pn->Get("side_offset") ? float(pn->Get("side_offset")->floatValue) : 0.5f;
					PinShape shape = pn->Get("shape") ? PinShape(pn->Get("shape")->intValue) : PinShape::CIRCLE;
					sdl3::FColor color = pn->Get("color") ? detail::UnpackColor(pn->Get("color")->intValue)
													: sdl3::FColor::UI_TEXT_BRIGHT();
					float psize = pn->Get("size") ? float(pn->Get("size")->floatValue) : 10.f;
					ecs::Entity pe = AddGraphPin(world, e, side, sideOffset, shape, color, psize);
					if (auto pin = world.GetComponent<UiGraphPin>(pe); pin.IsSome())
						if (auto v = pn->Get("connectable"))
							pin.Unwrap()->connectable = v->boolValue;
					String pkey = pn->Get("key") ? pn->Get("key")->stringValue : ("p" + String::From(j));
					pinByKey[pkey] = pe;
				}
			}
		}
	}

	if (auto connsArr = root->Get("connections")) {
		for (size_t i = 0; i < connsArr->GetSize(); ++i) {
			auto cn = connsArr->At(i);
			if (!cn || !cn->Get("from") || !cn->Get("to"))
				continue;
			auto fromIt = pinByKey.find(cn->Get("from")->stringValue);
			auto toIt = pinByKey.find(cn->Get("to")->stringValue);
			if (fromIt == pinByKey.end() || toIt == pinByKey.end())
				continue;
			GraphConnection *conn = ConnectPins(world, fromIt->second, toIt->second);
			if (!conn)
				continue;
			if (auto v = cn->Get("color"))
				conn->color = detail::UnpackColor(v->intValue);
			if (auto v = cn->Get("thickness"))
				conn->thickness = float(v->floatValue);
			if (auto v = cn->Get("opacity"))
				conn->opacity = float(v->floatValue);
			if (auto v = cn->Get("style"))
				conn->style = ConnectionStyle(v->intValue);
			if (auto v = cn->Get("start_cap"))
				conn->startCap = ConnectionEndCap(v->intValue);
			if (auto v = cn->Get("end_cap"))
				conn->endCap = ConnectionEndCap(v->intValue);
			if (auto v = cn->Get("arrow_size"))
				conn->arrowSize = float(v->floatValue);
			if (auto v = cn->Get("bezier_control_offset"))
				conn->bezierControlOffset = float(v->floatValue);
			if (auto wps = cn->Get("waypoints"))
				for (size_t w = 0; w < wps->GetSize(); ++w)
					conn->waypoints.push_back(detail::PointFrom(wps->At(w)));
		}
	}

	if (auto selArr = root->Get("selection")) {
		nodeGraph.ClearSelection(world, canvasEntity);
		for (size_t i = 0; i < selArr->GetSize(); ++i) {
			auto s = selArr->At(i);
			if (!s)
				continue;
			auto it = nodeByKey.find(s->stringValue);
			if (it != nodeByKey.end())
				nodeGraph.SelectNode(world, it->second, /*additive=*/true);
		}
	}

	layout.MarkDirty();
	return nodeByKey;
}

} // namespace ui
