#pragma once
/**
 * ui::nodegraph — éditeur de graphes de nœuds (style Blueprint) : canevas
 * infini (pan au bouton du milieu, zoom molette vers le curseur, grille),
 * nœuds déplaçables / redimensionnables / sélectionnables (clic, Ctrl, Maj,
 * rectangle de sélection), pins de formes variées, connexions (Bézier,
 * droites, orthogonales) avec flèches et points intermédiaires, copier /
 * couper / coller / supprimer, sérialisation dataio.
 *
 * Composition :
 *  - le canevas est un widget `node-graph` qui porte `GraphCanvas` ;
 *  - chaque nœud est un widget `graph-node` (enfant du canevas, position
 *    absolue) : son en-tête `graph-node-header`, son corps `graph-node-body` (le contenu
 *    fourni par l'application) et sa poignée `graph-node-grip` sont des
 *    widgets ordinaires, stylables en CSS. La position et l'échelle du nœud
 *    sont celles du canevas : l'extension pose `left/top/width/height` et
 *    `zoom` d'après `canvasPos`, le pan et le zoom ;
 *  - les pins sont des entités enfants du nœud portant `GraphPin` ;
 *  - grille, connexions, pins et rectangle de sélection sont dessinés par
 *    l'extension (enregistrée par ui::Ui).
 */
#include <functional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <dataio/node.hpp>

#include "builder.hpp"
#include "painter.hpp"

namespace ui {

enum class ConnectionStyle : uint8_t { BEZIER, STRAIGHT, ORTHOGONAL };
enum class ConnectionEndCap : uint8_t { NONE, ARROW };
enum class PinShape : uint8_t { CIRCLE, TRIANGLE, SQUARE, STAR };
enum class PinSide : uint8_t { Left, Right, Top, Bottom };

/// Connexion entre deux pins (donnée du canevas, pas une entité).
struct GraphConnection {
	ecs::Entity fromPin{};
	ecs::Entity toPin{};
	Color color{225 / 255.f, 225 / 255.f, 225 / 255.f, 235 / 255.f};
	float thickness = 2.5f;
	float opacity = 1.f;
	ConnectionStyle style = ConnectionStyle::BEZIER;
	ConnectionEndCap startCap = ConnectionEndCap::NONE;
	ConnectionEndCap endCap = ConnectionEndCap::ARROW;
	float arrowSize = 10.f;			 ///< unités du canevas
	float bezierControlOffset = 0.f; ///< 0 : automatique
	std::vector<sdl3::FPoint> waypoints; ///< points intermédiaires (coordonnées du canevas)
	/// Dessin personnalisé (le chemin est en coordonnées écran).
	std::function<void(Painter &, std::span<const sdl3::FPoint>, const GraphConnection &)> onCustomDraw;
};

struct NodeSelectionState {
	std::unordered_set<ecs::Entity> selected;
	ecs::Entity anchor{};
	ecs::Entity lastClicked{};
};

/// Sélection façon Explorateur sur un ordre de nœuds.
void ApplyNodeSelectionClick(NodeSelectionState &state, ecs::Entity clicked, bool ctrl, bool shift,
							 const std::vector<ecs::Entity> &order);

/// Brique : canevas d'un graphe.
struct GraphCanvas {
	sdl3::FPoint pan{0.f, 0.f};
	float zoom = 1.f;
	float minZoom = 0.15f, maxZoom = 4.f;
	Color bgColor{25 / 255.f, 26 / 255.f, 30 / 255.f, 1.f};
	Color gridColor{38 / 255.f, 39 / 255.f, 45 / 255.f, 1.f};
	Color gridColorMajor{55 / 255.f, 57 / 255.f, 66 / 255.f, 1.f};
	float gridSpacing = 24.f;
	int gridMajorEvery = 4;
	std::vector<GraphConnection> connections;
	NodeSelectionState selection;
};

/// Brique : nœud d'un graphe (le widget `graph-node`).
struct GraphNode {
	sdl3::FPoint canvasPos{0.f, 0.f};
	sdl3::FPoint size{200.f, 120.f};
	String title;
	Color headerColor{40 / 255.f, 90 / 255.f, 160 / 255.f, 1.f};
	bool resizable = true;
	bool movable = true;
	bool selected = false;
	bool disabled = false;
	bool hovered = false;
	float minWidth = 80.f, minHeight = 40.f;
	float headerHeight = 26.f;
	bool draggingMove = false;
	bool draggingResize = false;
};

/// Brique : pin d'un nœud (entité enfant du nœud, sans mise en page).
struct GraphPin {
	PinShape shape = PinShape::CIRCLE;
	Color color{240 / 255.f, 242 / 255.f, 248 / 255.f, 1.f};
	float size = 10.f;
	PinSide side = PinSide::Left;
	float sideOffset = 0.5f;
	bool connectable = true;
	bool connected = false;
	bool selected = false;
	bool hovered = false;
	sdl3::FRect screen{}; ///< boîte écran (calculée chaque image)
	std::function<void(Painter &, const sdl3::FRect &, const GraphPin &)> onCustomDraw;
};

/// Brique : rappels d'un canevas.
struct GraphCallbacks {
	std::function<void(const std::unordered_set<ecs::Entity> &)> onSelectionChanged;
	std::function<void(ecs::Entity, sdl3::FPoint)> onNodeMoved;
	std::function<void(ecs::Entity, sdl3::FPoint)> onNodeResized;
	std::function<void(ecs::Entity node, ecs::Entity pin, sdl3::FPoint screenPos)> onContextMenu;
};

// ── Construction ─────────────────────────────────────────────────────────────

/// Canevas (`node-graph`).
[[nodiscard]] WidgetBuilder NodeGraph(GraphCanvas config = {});
/// Ajoute un nœud au canevas ; `content` devient le corps du nœud.
Widget AddGraphNode(Widget canvas, String title, sdl3::FPoint canvasPos, sdl3::FPoint size, AnyWidget content,
					Color headerColor = Color{40 / 255.f, 90 / 255.f, 160 / 255.f, 1.f}, float innerZoom = 1.f);
/// Échelle du contenu d'un nœud (indépendante du zoom du canevas).
void SetNodeInnerZoom(Widget node, float zoom);
/// Détruit un nœud, ses pins et les connexions qui les touchent.
void DestroyGraphNode(ecs::ArchetypeRegistry &world, ecs::Entity node);
Widget AddGraphPin(Widget node, PinSide side, float sideOffset = 0.5f, PinShape shape = PinShape::CIRCLE,
				   Color color = Color{240 / 255.f, 242 / 255.f, 248 / 255.f, 1.f}, float size = 10.f);
void SetGraphCallbacks(Widget canvas, GraphCallbacks callbacks);

// ── Requêtes, géométrie ──────────────────────────────────────────────────────

[[nodiscard]] ecs::Entity CanvasOfNode(const ecs::ArchetypeRegistry &world, ecs::Entity node);
[[nodiscard]] ecs::Entity CanvasOfPin(const ecs::ArchetypeRegistry &world, ecs::Entity pin);
[[nodiscard]] ecs::Entity NodeOfPin(const ecs::ArchetypeRegistry &world, ecs::Entity pin);
[[nodiscard]] std::vector<ecs::Entity> NodesOf(const ecs::ArchetypeRegistry &world, ecs::Entity canvas);
[[nodiscard]] sdl3::FPoint CanvasToScreen(sdl3::FPoint canvasPt, const GraphCanvas &cv, const sdl3::FRect &canvasScreen) noexcept;
[[nodiscard]] sdl3::FPoint ScreenToCanvas(sdl3::FPoint screenPt, const GraphCanvas &cv, const sdl3::FRect &canvasScreen) noexcept;

// ── Connexions ───────────────────────────────────────────────────────────────

/// Relie deux pins du même canevas (nullptr si impossible ou déjà relié).
GraphConnection *ConnectPins(ecs::ArchetypeRegistry &world, ecs::Entity from, ecs::Entity to);
void DisconnectPins(ecs::ArchetypeRegistry &world, ecs::Entity from, ecs::Entity to);
void InsertWaypoint(GraphConnection &conn, size_t index, sdl3::FPoint canvasPoint);
void RemoveWaypoint(GraphConnection &conn, size_t index);

// ── Sélection, presse-papiers ────────────────────────────────────────────────

void SelectNode(ecs::ArchetypeRegistry &world, ecs::Entity node, bool additive = false);
void DeselectNode(ecs::ArchetypeRegistry &world, ecs::Entity node);
void ClearGraphSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvas);
void SelectAllNodes(ecs::ArchetypeRegistry &world, ecs::Entity canvas);
[[nodiscard]] bool IsNodeSelected(const ecs::ArchetypeRegistry &world, ecs::Entity node);
void DeleteGraphSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvas);
void CopyGraphSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvas);
void CutGraphSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvas);
[[nodiscard]] bool HasGraphClipboard(const ecs::ArchetypeRegistry &world);
/// Colle les nœuds copiés (titres, tailles, pins — pas les contenus) en `at`.
std::vector<Widget> PasteGraphClipboard(Widget canvas, sdl3::FPoint at);

// ── Sérialisation ────────────────────────────────────────────────────────────

[[nodiscard]] dataio::NodePtr SerializeGraph(ecs::ArchetypeRegistry &world, ecs::Entity canvas);
/// Recrée nœuds, pins et connexions ; rend les nœuds par clé du document.
std::unordered_map<String, ecs::Entity> DeserializeGraph(Widget canvas, const dataio::NodePtr &root);

namespace detail {
[[nodiscard]] sdl3::FPoint PinAnchor(PinSide side, float sideOffset, const sdl3::FRect &s) noexcept;
[[nodiscard]] std::vector<sdl3::FPoint> StarPoints(sdl3::FPoint center, float outerR, float innerR, int points);
[[nodiscard]] inline sdl3::FPoint PinCenter(const sdl3::FRect &box) noexcept {
	return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
}
[[nodiscard]] std::vector<sdl3::FPoint> SampleCubicBezier(sdl3::FPoint p0, sdl3::FPoint p1, sdl3::FPoint p2,
														  sdl3::FPoint p3, int segments);
[[nodiscard]] std::vector<sdl3::FPoint> ConnectionPath(sdl3::FPoint a, std::span<const sdl3::FPoint> throughScreen,
													   sdl3::FPoint b, ConnectionStyle style, float controlOffsetCanvas,
													   float zoom);
[[nodiscard]] float DistanceToSegment(sdl3::FPoint p, sdl3::FPoint a, sdl3::FPoint b) noexcept;
[[nodiscard]] int64_t PackColor(Color c) noexcept;
[[nodiscard]] Color UnpackColor(int64_t v) noexcept;
} // namespace detail

/// Rendu et interaction des graphes (enregistrée par ui::Ui).
void RegisterNodeGraphExtension(Context &ctx);

} // namespace ui
