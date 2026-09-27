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
void ApplyNodeSelectionClick(NodeSelectionState &state, ecs::Entity clicked, bool ctrl, bool shift,
									const std::vector<ecs::Entity> &order);

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
[[nodiscard]] WidgetBuilder CanvasBuilder(UiFactory &f);

/// Attache UiNodeGraphCanvas à une entité déjà spawnée (cf. canvasBuilder()).
ecs::Entity AttachNodeGraphCanvas(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity, UiNodeGraphCanvas config = {});

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
[[nodiscard]] ecs::Entity AddGraphNode(UiFactory &f, ecs::Entity canvasEntity, String title, sdl3::FPoint canvasPos,
										 sdl3::FPoint size, WidgetBuilder content, sdl3::FColor headerColor = sdl3::FColor::UI_NODE_HEADER_BLUE(),
										 float innerZoom = 1.f);

/// Change l'inner-zoom d'un nœud APRÈS sa création (spec section 7 : "changer
/// indépendamment l'inner-zoom d'un nœud" fait partie des critères
/// d'acceptation). `content` (cf. addGraphNode()) est l'UNIQUE enfant direct
/// du nœud — poser `prop::InnerZoom` en style inline dessus suffit, la
/// cascade `EmBases::scale` de LayoutSystem::computeFonts() (Phase 0) fait le
/// reste sur tout son sous-arbre. `layout.MarkDirty()` est nécessaire ici
/// (contrairement à editInlineStyle() seul) car computeFonts() ne relit les
/// styles inline qu'à la prochaine VRAIE passe de layout, pas au prochain
/// appel de resolve() du StyleSystem (mécanismes découplés, cf. Phase 0).
void SetNodeInnerZoom(ecs::ArchetypeRegistry &world, LayoutSystem &layout, ecs::Entity node, float value);

/// Détruit un nœud (API programmatique, cf. spec section 6/9) : despawn en
/// cascade son sous-arbre de widgets (via despawnTree(), components.hpp),
/// SES pins (introuvables par UiParent — filtrées par UiGraphPin::node),
/// toute connexion référençant l'un de ces pins, et le retire de la
/// sélection du canvas (anchor réinitialisée si elle pointait dessus).
/// Collecte-puis-mute (mêmes précautions d'invalidation d'itérateur que le
/// reste du fichier) — les pins sont collectés AVANT tout despawn.
void DestroyGraphNode(ecs::ArchetypeRegistry &world, ecs::Entity node);

/// Ajoute un pin à un nœud déjà spawné. Spawn direct via l'ECS (pas de
/// WidgetBuilder — un pin n'a pas de sous-arbre de widgets) ; le UiComputed
/// est créé ICI, vide, plutôt que par updatePins() au premier appel : ça
/// évite un AddComponent() depuis l'intérieur d'une Query<UiGraphPin> plus
/// tard (même risque d'invalidation d'itérateur que documenté ailleurs dans
/// ce module — updatePins() n'a alors plus qu'à MUTER des champs existants).
[[nodiscard]] ecs::Entity AddGraphPin(ecs::ArchetypeRegistry &world, ecs::Entity node, PinSide side, float sideOffset = 0.5f,
										PinShape shape = PinShape::CIRCLE, sdl3::FColor color = sdl3::FColor::UI_TEXT_BRIGHT(),
										float size = 10.f);

namespace detail {
/// Point d'ancrage d'un pin sur le bord `side` d'un rectangle écran `s`.
[[nodiscard]] sdl3::FPoint PinAnchor(PinSide side, float sideOffset, const sdl3::FRect &s) noexcept;

/// Sommets d'une étoile à `points` branches (angle de départ vers le haut).
[[nodiscard]] std::vector<sdl3::FPoint> StarPoints(sdl3::FPoint center, float outerR, float innerR, int points);

/// Centre d'un rectangle écran (utilisé pour ancrer une connexion sur le
/// centre d'un pin plutôt que son coin, cf. RenderConnections()).
[[nodiscard]] inline sdl3::FPoint PinCenter(const sdl3::FRect &box) noexcept { return {box.x + box.w * 0.5f, box.y + box.h * 0.5f}; }

/// Échantillonne une Bézier cubique en `segments` segments (de Casteljau
/// direct, degré 3 fixe — pas besoin du degré arbitraire de
/// sdl3::Renderer::drawBezier ici puisqu'on a besoin des POINTS pour
/// construire un ruban épais, pas juste tracer une ligne 1px).
[[nodiscard]] std::vector<sdl3::FPoint> SampleCubicBezier(sdl3::FPoint p0, sdl3::FPoint p1, sdl3::FPoint p2, sdl3::FPoint p3, int segments);

/// Chemin ÉCRAN d'UN segment `a`→`b` (sans point intermédiaire), selon
/// `style`. `controlOffsetCanvas` (Bézier uniquement) est en unités CANVAS,
/// mis à l'échelle par `zoom` ; 0 = automatique (proportionnel à |dx|).
[[nodiscard]] std::vector<sdl3::FPoint> SegmentPath(sdl3::FPoint a, sdl3::FPoint b, ConnectionStyle style,
													  float controlOffsetCanvas, float zoom);

/// Chemin ÉCRAN complet d'une connexion `a`→`b`, routé à travers
/// `throughScreen` (points intermédiaires DÉJÀ convertis en coordonnées
/// écran, dans l'ordre) — chaîne segmentPath() sur chaque paire de points
/// consécutifs (a, through[0], ..., through[n-1], b) et concatène, en
/// évitant de dupliquer le point de jonction entre deux segments (cf.
/// GraphConnection::waypoints — Phase 4).
[[nodiscard]] std::vector<sdl3::FPoint> ConnectionPath(sdl3::FPoint a, std::span<const sdl3::FPoint> throughScreen, sdl3::FPoint b,
														 ConnectionStyle style, float controlOffsetCanvas,
														 float zoom);

/// Surcharge sans points intermédiaires (raccourci pour l'appelant qui sait
/// déjà qu'il n'y en a pas — évite de construire un span vide à chaque appel).
[[nodiscard]] std::vector<sdl3::FPoint> ConnectionPath(sdl3::FPoint a, sdl3::FPoint b, ConnectionStyle style,
														 float controlOffsetCanvas, float zoom);

/// Distance perpendiculaire de `p` au segment `[a,b]` (clampée aux
/// extrémités) — utilisé pour détecter un double-clic sur le TRACÉ d'une
/// connexion (ajout de point intermédiaire, cf. NodeGraphSystem::HandleEvent).
[[nodiscard]] float DistanceToSegment(sdl3::FPoint p, sdl3::FPoint a, sdl3::FPoint b) noexcept;
} // namespace detail

/// Insère un point intermédiaire à l'index `index` (canvas-space) — indices
/// hors bornes sont clampés à la fin (ajout en queue).
void InsertWaypoint(GraphConnection &conn, size_t index, sdl3::FPoint canvasPoint);

/// Retire le point intermédiaire à l'index `index` (no-op si hors bornes).
void RemoveWaypoint(GraphConnection &conn, size_t index);

/// Canvas propriétaire d'un pin (remonte pin.node -> UiParent.parent), ou une
/// ecs::Entity invalide si le pin/nœud n'a pas (plus) de parent valide.
[[nodiscard]] ecs::Entity CanvasOfPin(ecs::ArchetypeRegistry &world, ecs::Entity pin);

/// Canvas propriétaire d'un nœud (son UiParent direct), ou une ecs::Entity
/// invalide si `node` n'a pas de parent valide.
[[nodiscard]] ecs::Entity CanvasOfNode(ecs::ArchetypeRegistry &world, ecs::Entity node);

/// Point CANVAS-space -> écran, pour un point qui n'a pas de UiComputed
/// propre (waypoints — cf. GraphConnection::waypoints) : même formule que
/// NodeGraphSystem::Prepass() pour les nœuds (`(pt - pan) * zoom`), mais
/// appliquée manuellement puisque les waypoints ne passent jamais par
/// LayoutSystem. `canvasScreen` est le UiComputed.screen DÉJÀ résolu du
/// canvas lui-même (son origine écran absolue).
[[nodiscard]] sdl3::FPoint CanvasToScreen(sdl3::FPoint canvasPt, const UiNodeGraphCanvas &cv, const sdl3::FRect &canvasScreen) noexcept;

/// Inverse de canvasToScreen() — point écran -> CANVAS-space.
[[nodiscard]] sdl3::FPoint ScreenToCanvas(sdl3::FPoint screenPt, const UiNodeGraphCanvas &cv, const sdl3::FRect &canvasScreen) noexcept;

/// Crée une connexion entre deux pins — refusée (retourne nullptr, aucun
/// effet) si `from`/`to` sont invalides ou identiques, si l'un des deux pins
/// n'est pas `connectable`, s'ils n'appartiennent pas au MÊME canvas, ou si
/// une connexion existe déjà entre eux (dans un sens ou l'autre — pas de
/// doublon). Retourne un pointeur vers la connexion insérée pour que
/// l'appelant personnalise ses champs juste après (couleur/style/épaisseur/
/// caps...) sans avoir à la rechercher.
GraphConnection *ConnectPins(ecs::ArchetypeRegistry &world, ecs::Entity from, ecs::Entity to);

/// Retire toute connexion entre `from` et `to` (dans un sens ou l'autre). Ne
/// remet `connected` à false sur un pin que s'il ne lui reste plus AUCUNE
/// connexion (un pin peut avoir plusieurs connexions simultanées).
void DisconnectPins(ecs::ArchetypeRegistry &world, ecs::Entity from, ecs::Entity to);

// ============================================================================
// NodeGraphSystem — prepass transform + interactions + rendu grille/en-têtes
// ============================================================================

class NodeGraphSystem {
public:
	/// Nécessaire pour que le titre des nœuds soit dessiné (cf.
	/// RenderHeaders()) — même contrainte que RenderSystem::SetTextEngine :
	/// sans elle, le texte est simplement omis (utile en headless).
	void SetTextEngine(sdl3::TextEngine &engine, sdl3::Font &font);

	/// À appeler juste AVANT layout.RunIfNeeded() chaque frame (cf. en-tête
	/// du fichier) : réécrit UiRect.offset/UiItem.width/height de chaque
	/// UiGraphNode depuis la transform pan/zoom de son UiNodeGraphCanvas
	/// parent — AUCUNE modification du placement lui-même dans LayoutSystem
	/// (cf. plan, Décision #2) : le reste du pipeline traite ensuite le
	/// nœud comme un enfant `.Absolute()` ordinaire.
	void Prepass(ecs::ArchetypeRegistry &world);

	/// À appeler juste APRÈS layout.RunIfNeeded() chaque frame (cf. en-tête
	/// du fichier) : positionne chaque UiGraphPin sur le bord de son nœud
	/// (side/sideOffset), à partir du UiComputed.screen DÉJÀ résolu du nœud
	/// — ne touche jamais LayoutSystem, ni n'ajoute de composant (cf. le
	/// commentaire d'addGraphPin() sur pourquoi UiComputed est créé une
	/// fois, à la construction).
	void UpdatePins(ecs::ArchetypeRegistry &world);

	/// Pin sous le point écran `(x,y)`, ou une ecs::Entity invalide si aucun (les
	/// pins non `connectable` sont ignorés — réservé aux interactions de
	/// connexion, Phase 3, mais réutilisé dès Phase 2 pour le survol/hit-test
	/// de base).
	[[nodiscard]] ecs::Entity HitTestPin(ecs::ArchetypeRegistry &world, float x, float y) const;

	/// Sélectionne `node` (API programmatique, cf. spec section 6/9) —
	/// `additive=false` remplace la sélection courante, `true` l'ajoute sans
	/// toucher aux autres (comme un Ctrl+clic). No-op si `node` n'a pas de
	/// UiGraphNode ou de canvas parent valide.
	void SelectNode(ecs::ArchetypeRegistry &world, ecs::Entity node, bool additive = false);

	/// Retire `node` de la sélection de son canvas (no-op s'il n'y était pas).
	void DeselectNode(ecs::ArchetypeRegistry &world, ecs::Entity node);

	/// Vide la sélection d'un canvas.
	void ClearSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity);

	/// Sélectionne tous les nœuds d'un canvas.
	void SelectAllNodes(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity);

	[[nodiscard]] bool IsNodeSelected(ecs::ArchetypeRegistry &world, ecs::Entity node) const;

	/// Détruit tous les nœuds actuellement sélectionnés d'un canvas (API
	/// programmatique, cf. spec section 6 — réutilisée par la touche
	/// Suppr/Retour arrière dans handleEvent()). Copie la liste AVANT de
	/// détruire (destroyGraphNode() modifie selection.selected lui-même).
	void DeleteSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity);

	/// Copie les nœuds sélectionnés d'un canvas dans le presse-papiers
	/// interne (remplace tout contenu précédent) — cf. ClipboardNode :
	/// chrome + pins, PAS le contenu (widgets enfants), cf. sa doc.
	void CopySelection(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity);

	/// Copie puis détruit la sélection (couper, cf. spec section 6).
	void CutSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity);

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
	std::vector<ecs::Entity> PasteClipboard(UiFactory &f, ecs::Entity canvasEntity, sdl3::FPoint atCanvasPos);

	/// Mises à jour continues indépendantes des évènements — actuellement
	/// uniquement l'auto-scroll pendant un drag de nœud proche du bord du
	/// canvas (spec section 1/6) : contrairement au reste des interactions,
	/// il doit continuer à défiler même si la souris reste immobile au bord,
	/// donc ne peut PAS être piloté uniquement par les évènements de
	/// mouvement — à appeler une fois par frame, avec un vrai dt (même rôle
	/// que InputSystem::Tick(), même raison d'être).
	void Tick(ecs::ArchetypeRegistry &world, LayoutSystem &layout, float dt);

	/// Un évènement SDL discret — glisser un en-tête déplace le nœud,
	/// glisser la poignée de coin le redimensionne, molette au-dessus du
	/// canvas zoome (vers le curseur), clic-milieu fait défiler (pan).
	/// Appelé par l'application EN PLUS de `gui.HandleEvent()`, pas à sa
	/// place (cf. en-tête du fichier) — reste un système séparé, autonome,
	/// pour ne rien changer à InputSystem/systems.hpp.
	void HandleEvent(ecs::ArchetypeRegistry &world, const sdl3::Event &ev, LayoutSystem &layout);

	/// Fond quadrillé — à appeler AVANT gui.Render() (cf. en-tête du
	/// fichier) : le canvas lui-même reste sans UiPanel opaque pour laisser
	/// cette grille visible en dessous des nœuds (dessinés normalement par
	/// RenderSystem juste après).
	void RenderGrid(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren);

	/// Connexions (traits reliant deux pins) — à appeler APRÈS RenderGrid()
	/// et AVANT gui.Render() (cf. en-tête du fichier) : les traits doivent
	/// passer visuellement SOUS le corps opaque des nœuds, pas par-dessus.
	/// Dessine aussi le trait "élastique" temporaire pendant un glisser
	/// pin→pin en cours (cf. handleEvent()).
	void RenderConnections(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren);

	/// Bandeaux de titre des nœuds — à appeler APRÈS gui.Render() (cf.
	/// en-tête du fichier), pour dessiner par-dessus le corps déjà rendu du
	/// nœud. LIMITATION CONNUE (Phase 1) : dessinés dans une passe séparée de
	/// TOUS les corps de nœuds, donc l'en-tête d'un nœud en arrière-plan
	/// peut chevaucher le CONTENU d'un nœud au premier plan si les deux se
	/// superposent visuellement — acceptable pour cette phase (rare en usage
	/// normal), à revoir si un vrai z-ordering par nœud devient nécessaire.
	void RenderHeaders(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren);

	/// Rendu des pins — à appeler APRÈS RenderHeaders() (cf. en-tête du
	/// fichier) : un pin doit rester cliquable/visible même s'il chevauche
	/// visuellement un en-tête de nœud voisin. Pas de SetClipRect ici (les
	/// pins débordent volontairement du bord du nœud, contrairement au
	/// contenu/en-tête qui sont clippés à sa boîte).
	void RenderPins(ecs::ArchetypeRegistry &world, IUiRenderBackend &ren);

	/// Rectangle de sélection (marquee) en cours — à appeler en DERNIER (cf.
	/// en-tête du fichier), par-dessus tout le reste. No-op si aucun marquee
	/// actif.
	void RenderMarquee(IUiRenderBackend &ren);

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
	void SyncSelectionVisuals(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity, const UiNodeGraphCanvas &cv);

	/// Texte du titre, centré verticalement, aligné à gauche avec une petite
	/// marge — même esprit que RenderSystem::drawTextCentered mais autonome
	/// (NodeGraphSystem n'a pas accès aux méthodes privées de RenderSystem).
	void DrawTitle(IUiRenderBackend &ren, const String &title, const sdl3::FRect &box);

	/// Rendu par défaut d'un pin (cf. spec section 3 : forme/couleur/état) —
	/// REMPLI si `connected`, contour seul sinon (convention courante des
	/// éditeurs de node-graph : un pin non connecté se distingue au premier
	/// coup d'œil). Le survol éclaircit la couleur ; la sélection ajoute un
	/// anneau doré (même couleur que la sélection de nœud, pour cohérence
	/// visuelle — cf. RenderHeaders()).
	void DrawDefaultPin(IUiRenderBackend &ren, const UiGraphPin &pin, const sdl3::FRect &box);

	/// Ajoute un quad (2 triangles) formant un segment de ruban épais entre
	/// `a` et `b`, de demi-largeur `halfW`, perpendiculaire à la direction du
	/// segment — cf. Décision #6 du plan ("l'épaisseur de connexion se rend
	/// via un petit quad, pas plusieurs lignes parallèles").
	static void AppendThickSegment(std::vector<sdl3::Vertex> &verts, sdl3::FPoint a, sdl3::FPoint b, float halfW,
								   sdl3::FColor color);

	/// Triangle plein orienté selon la tangente locale `from`→`tip` — `tip`
	/// est la pointe (généralement une extrémité de connexion).
	static void DrawArrowHead(IUiRenderBackend &ren, sdl3::FPoint from, sdl3::FPoint tip, float size, sdl3::FColor color);

	/// Rendu par défaut d'une connexion : ruban épais (cf. appendThickSegment)
	/// le long de `path` (déjà résolu selon le style — droite/orthogonal/
	/// Bézier échantillonnée), petits disques aux articulations internes pour
	/// combler les coutures entre segments, flèche(s) optionnelle(s) aux
	/// extrémités (cf. GraphConnection::startCap/endCap).
	void DrawDefaultConnection(IUiRenderBackend &ren, const GraphConnection &conn, std::span<const sdl3::FPoint> path,
							   float zoom);

	/// Poignée visuelle d'un point intermédiaire (cf. spec section 5 :
	/// "poignées visuelles") — petit carré, plus grand/clair pendant le
	/// glisser pour un retour visuel clair.
	void DrawWaypointHandle(IUiRenderBackend &ren, sdl3::FPoint screenPos, bool dragging);
};

// ============================================================================
// Persistance — data::Node (cf. Décision #8 du plan : réutilise l'arbre
// commun data:: déjà existant plutôt qu'un nouveau format ; le CHOIX du
// format texte final (JSON le plus probable, mais XML/YAML/TOML tout aussi
// possibles via data.hpp) reste à l'appelant — ces fonctions ne produisent/
// ne consomment que l'arbre intermédiaire.
// ============================================================================

namespace detail {
[[nodiscard]] int64_t PackColor(sdl3::FColor c) noexcept;
[[nodiscard]] sdl3::Color UnpackColor(int64_t v) noexcept;
[[nodiscard]] data::NodePtr PointNode(sdl3::FPoint p);
[[nodiscard]] sdl3::FPoint PointFrom(const data::NodePtr &n);
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
[[nodiscard]] data::NodePtr SerializeGraph(ecs::ArchetypeRegistry &world, ecs::Entity canvasEntity);

/// Reconstruit un graphe depuis un document produit par serializeGraph() —
/// spawn de nouveaux nœuds/pins (via `f`/`world`), connexions recréées via
/// connectPins() (donc re-validées : deux pins déjà connectés ou invalides
/// dans le document ne planteraient pas, cf. sa propre doc), sélection
/// restaurée via `nodeGraph` (pour que les hooks/visuels restent cohérents,
/// cf. NodeGraphSystem::SelectNode()). `content` des nœuds reste VIDE (même
/// limitation que pasteClipboard() — cf. sa doc) : à l'appelant de repeupler
/// si besoin (ex: reconstruire depuis un type de nœud stocké séparément).
/// Retourne les nœuds créés (clé document -> ecs::Entity), pour un usage avancé.
std::unordered_map<String, ecs::Entity> DeserializeGraph(UiFactory &f, NodeGraphSystem &nodeGraph,
																 LayoutSystem &layout, ecs::Entity canvasEntity,
																 const data::NodePtr &root);

} // namespace ui
