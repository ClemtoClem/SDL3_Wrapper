// Définitions de ui/nodegraph.hpp
#include <ui/nodegraph.hpp>
#include <ui/systems.hpp>
#include <ui/widgets.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace ui {

namespace {

/// État d'interaction des graphes (ressource du monde).
struct NodeGraphState {
	enum class Drag : uint8_t { None, Move, Resize, Connect, Waypoint, Pan, Marquee } drag = Drag::None;
	ecs::Entity canvas{};
	ecs::Entity node{};
	ecs::Entity fromPin{};
	sdl3::FPoint startMouse{};
	sdl3::FPoint startValue{};
	sdl3::FPoint mouse{};
	std::vector<std::pair<ecs::Entity, sdl3::FPoint>> groupStart;
	ptrdiff_t waypointConn = -1, waypointIndex = -1;
	bool marqueeAdditive = false;

	struct ClipPin {
		PinShape shape;
		Color color;
		float size;
		PinSide side;
		float sideOffset;
		bool connectable;
	};
	struct ClipNode {
		String title;
		sdl3::FPoint size;
		Color headerColor;
		bool resizable, movable;
		float minWidth, minHeight, headerHeight;
		sdl3::FPoint pos;
		std::vector<ClipPin> pins;
	};
	std::vector<ClipNode> clipboard;
};

NodeGraphState &GraphState(ecs::ArchetypeRegistry &world) {
	if (!world.resources.Contains<NodeGraphState>())
		world.resources.Insert(NodeGraphState{});
	return *world.resources.Get<NodeGraphState>().Unwrap();
}

Color Lighten(Color c, float k) { return Mix(c, Color{1.f, 1.f, 1.f, c.a}, k); }

sdl3::FRect BoxOf(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (auto r = world.GetComponent<Rect>(e); r.IsSome())
		return r.Unwrap()->box;
	return {};
}

bool Inside(const sdl3::FRect &r, sdl3::FPoint p) { return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }

void NotifySelection(ecs::ArchetypeRegistry &world, ecs::Entity canvas) {
	auto cv = world.GetComponent<GraphCanvas>(canvas);
	if (cv.IsNone())
		return;
	const std::unordered_set<ecs::Entity> selected = cv.Unwrap()->selection.selected;
	for (ecs::Entity n : NodesOf(world, canvas)) {
		const bool on = selected.count(n) > 0;
		if (auto gn = world.GetComponent<GraphNode>(n); gn.IsSome())
			gn.Unwrap()->selected = on;
		if (auto ws = world.GetComponent<WidgetState>(n); ws.IsSome()) {
			ws.Unwrap()->flags = on ? (ws.Unwrap()->flags | State::Selected) : (ws.Unwrap()->flags & ~State::Selected);
			MarkStyleDirty(world, n);
		}
	}
	if (auto cb = world.GetComponent<GraphCallbacks>(canvas); cb.IsSome() && cb.Unwrap()->onSelectionChanged) {
		const auto fn = cb.Unwrap()->onSelectionChanged;
		fn(selected);
	}
}

std::vector<ecs::Entity> PinsOf(const ecs::ArchetypeRegistry &world, ecs::Entity node) {
	std::vector<ecs::Entity> out;
	for (ecs::Entity c : ChildList(world, node))
		if (world.HasComponent<GraphPin>(c))
			out.push_back(c);
	return out;
}

/// Boîte écran d'un pin (d'après la boîte du nœud et le zoom du canevas).
sdl3::FRect PinScreen(const ecs::ArchetypeRegistry &world, ecs::Entity pin) {
	auto p = world.GetComponent<GraphPin>(pin);
	const ecs::Entity node = NodeOfPin(world, pin);
	if (p.IsNone() || !node.Valid())
		return {};
	float zoom = 1.f;
	if (auto cv = world.GetComponent<GraphCanvas>(CanvasOfNode(world, node)); cv.IsSome())
		zoom = cv.Unwrap()->zoom;
	const sdl3::FPoint a = detail::PinAnchor(p.Unwrap()->side, p.Unwrap()->sideOffset, BoxOf(world, node));
	const float half = std::max(1.f, p.Unwrap()->size * zoom * 0.5f);
	return {a.x - half, a.y - half, half * 2.f, half * 2.f};
}

ecs::Entity HitPin(const ecs::ArchetypeRegistry &world, sdl3::FPoint p) {
	ecs::Entity hit{};
	world.Query<GraphPin>([&](ecs::Entity e, const GraphPin &pin) {
		if (!hit.Valid() && pin.connectable && Inside(PinScreen(world, e), p))
			hit = e;
	});
	return hit;
}

} // namespace

// ============================================================================
// Sélection pure
// ============================================================================

void ApplyNodeSelectionClick(NodeSelectionState &state, ecs::Entity clicked, bool ctrl, bool shift,
							 const std::vector<ecs::Entity> &order) {
	auto itAnchor = shift ? std::find(order.begin(), order.end(), state.anchor) : order.end();
	if (shift && state.anchor.Valid() && itAnchor != order.end()) {
		auto itClicked = std::find(order.begin(), order.end(), clicked);
		if (itClicked != order.end()) {
			auto lo = std::min(itAnchor, itClicked), hi = std::max(itAnchor, itClicked);
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

// ============================================================================
// Construction
// ============================================================================

WidgetBuilder NodeGraph(GraphCanvas config) {
	WidgetBuilder b("node-graph");
	b.With(std::move(config)).Clip().Focusable();
	return b;
}

Widget AddGraphNode(Widget canvas, String title, sdl3::FPoint canvasPos, sdl3::FPoint size, AnyWidget content,
					Color headerColor, float innerZoom) {
	if (!canvas.Valid())
		return Widget();
	GraphNode gn;
	gn.title = title;
	gn.canvasPos = canvasPos;
	gn.size = size;
	gn.headerColor = headerColor;
	WidgetBuilder node("graph-node");
	node.With(gn).Absolute();
	WidgetBuilder header("graph-node-header");
	header.Height(gn.headerHeight).Background(headerColor).HoverStyle(Style().Background(Lighten(headerColor, 0.1f)));
	header.Children({Label(std::move(title)).Class("graph-node-title").PointerThrough()});
	WidgetBuilder body("graph-node-body");
	body.Grow().Zoom(innerZoom).Children({std::move(content)});
	WidgetBuilder grip("graph-node-grip");
	grip.Absolute().Anchor(Anchor::BottomRight).Size(14.f, 14.f).With(Shape{ShapeKind::Grip, 0.f, 0.f, 1.f});
	node.Children({header, body, grip});
	Widget w = canvas.Append(node);
	MarkLayoutDirty(*canvas.World());
	return w;
}

void SetNodeInnerZoom(Widget node, float zoom) {
	for (Widget c : node.Children())
		if (c.Type() == "graph-node-body")
			c.Zoom(zoom);
}

void DestroyGraphNode(ecs::ArchetypeRegistry &world, ecs::Entity node) {
	const ecs::Entity canvas = CanvasOfNode(world, node);
	const std::vector<ecs::Entity> pins = PinsOf(world, node);
	auto removed = [&](ecs::Entity p) { return std::find(pins.begin(), pins.end(), p) != pins.end(); };
	if (auto cv = world.GetComponent<GraphCanvas>(canvas); cv.IsSome()) {
		std::vector<GraphConnection> &conns = cv.Unwrap()->connections;
		std::vector<ecs::Entity> other;
		for (const GraphConnection &c : conns) {
			if (removed(c.fromPin) && !removed(c.toPin))
				other.push_back(c.toPin);
			else if (removed(c.toPin) && !removed(c.fromPin))
				other.push_back(c.fromPin);
		}
		std::erase_if(conns, [&](const GraphConnection &c) { return removed(c.fromPin) || removed(c.toPin); });
		for (ecs::Entity o : other) {
			bool still = false;
			for (const GraphConnection &c : conns)
				still = still || c.fromPin == o || c.toPin == o;
			if (auto p = world.GetComponent<GraphPin>(o); p.IsSome())
				p.Unwrap()->connected = still;
		}
		cv.Unwrap()->selection.selected.erase(node);
		if (cv.Unwrap()->selection.anchor == node)
			cv.Unwrap()->selection.anchor = ecs::Entity{};
	}
	Widget(world, node).Destroy();
}

Widget AddGraphPin(Widget node, PinSide side, float sideOffset, PinShape shape, Color color, float size) {
	if (!node.Valid())
		return Widget();
	GraphPin pin;
	pin.side = side;
	pin.sideOffset = sideOffset;
	pin.shape = shape;
	pin.color = color;
	pin.size = size;
	return node.Append(Box("graph-pin").With(std::move(pin)).Display(Display::None));
}

void SetGraphCallbacks(Widget canvas, GraphCallbacks callbacks) { canvas.Set(std::move(callbacks)); }

// ============================================================================
// Requêtes, géométrie
// ============================================================================

ecs::Entity CanvasOfNode(const ecs::ArchetypeRegistry &world, ecs::Entity node) {
	return AncestorWith<GraphCanvas>(world, node);
}

ecs::Entity NodeOfPin(const ecs::ArchetypeRegistry &world, ecs::Entity pin) { return AncestorWith<GraphNode>(world, pin); }

ecs::Entity CanvasOfPin(const ecs::ArchetypeRegistry &world, ecs::Entity pin) {
	const ecs::Entity node = NodeOfPin(world, pin);
	return node.Valid() ? CanvasOfNode(world, node) : ecs::Entity{};
}

std::vector<ecs::Entity> NodesOf(const ecs::ArchetypeRegistry &world, ecs::Entity canvas) {
	std::vector<ecs::Entity> out;
	for (ecs::Entity c : ChildList(world, canvas))
		if (world.HasComponent<GraphNode>(c))
			out.push_back(c);
	return out;
}

sdl3::FPoint CanvasToScreen(sdl3::FPoint canvasPt, const GraphCanvas &cv, const sdl3::FRect &canvasScreen) noexcept {
	return {canvasScreen.x + (canvasPt.x - cv.pan.x) * cv.zoom, canvasScreen.y + (canvasPt.y - cv.pan.y) * cv.zoom};
}

sdl3::FPoint ScreenToCanvas(sdl3::FPoint screenPt, const GraphCanvas &cv, const sdl3::FRect &canvasScreen) noexcept {
	const float zoom = std::max(0.0001f, cv.zoom);
	return {cv.pan.x + (screenPt.x - canvasScreen.x) / zoom, cv.pan.y + (screenPt.y - canvasScreen.y) / zoom};
}

namespace detail {

sdl3::FPoint PinAnchor(PinSide side, float sideOffset, const sdl3::FRect &s) noexcept {
	switch (side) {
	case PinSide::Left:
		return {s.x, s.y + sideOffset * s.h};
	case PinSide::Right:
		return {s.x + s.w, s.y + sideOffset * s.h};
	case PinSide::Top:
		return {s.x + sideOffset * s.w, s.y};
	case PinSide::Bottom:
		return {s.x + sideOffset * s.w, s.y + s.h};
	}
	return {s.x, s.y};
}

std::vector<sdl3::FPoint> StarPoints(sdl3::FPoint center, float outerR, float innerR, int points) {
	std::vector<sdl3::FPoint> pts;
	pts.reserve(size_t(points) * 2);
	const float step = math::PI / float(points);
	float angle = -math::PI * 0.5f;
	for (int i = 0; i < points * 2; ++i) {
		const float r = (i % 2 == 0) ? outerR : innerR;
		pts.push_back({center.x + r * std::cos(angle), center.y + r * std::sin(angle)});
		angle += step;
	}
	return pts;
}

std::vector<sdl3::FPoint> SampleCubicBezier(sdl3::FPoint p0, sdl3::FPoint p1, sdl3::FPoint p2, sdl3::FPoint p3, int segments) {
	std::vector<sdl3::FPoint> pts;
	pts.reserve(size_t(segments) + 1);
	for (int i = 0; i <= segments; ++i) {
		const float t = float(i) / float(segments), mt = 1.f - t;
		const float a = mt * mt * mt, b = 3.f * mt * mt * t, c = 3.f * mt * t * t, d = t * t * t;
		pts.push_back({a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y});
	}
	return pts;
}

namespace {

std::vector<sdl3::FPoint> SegmentPath(sdl3::FPoint a, sdl3::FPoint b, ConnectionStyle style, float controlOffset, float zoom) {
	switch (style) {
	case ConnectionStyle::STRAIGHT:
		return {a, b};
	case ConnectionStyle::ORTHOGONAL: {
		const float midX = (a.x + b.x) * 0.5f;
		return {a, {midX, a.y}, {midX, b.y}, b};
	}
	case ConnectionStyle::BEZIER:
		break;
	}
	const float dx = b.x - a.x;
	const float offset = controlOffset > 0.f ? controlOffset * zoom : math::Clamp(std::fabs(dx) * 0.5f, 30.f * zoom, 150.f * zoom);
	return SampleCubicBezier(a, {a.x + offset, a.y}, {b.x - offset, b.y}, b, 24);
}

} // namespace

std::vector<sdl3::FPoint> ConnectionPath(sdl3::FPoint a, std::span<const sdl3::FPoint> throughScreen, sdl3::FPoint b,
										 ConnectionStyle style, float controlOffsetCanvas, float zoom) {
	std::vector<sdl3::FPoint> pts;
	pts.push_back(a);
	pts.insert(pts.end(), throughScreen.begin(), throughScreen.end());
	pts.push_back(b);
	std::vector<sdl3::FPoint> path;
	for (size_t i = 0; i + 1 < pts.size(); ++i) {
		auto seg = SegmentPath(pts[i], pts[i + 1], style, controlOffsetCanvas, zoom);
		if (!path.empty() && !seg.empty())
			path.pop_back();
		path.insert(path.end(), seg.begin(), seg.end());
	}
	return path;
}

float DistanceToSegment(sdl3::FPoint p, sdl3::FPoint a, sdl3::FPoint b) noexcept {
	const sdl3::FPoint ab{b.x - a.x, b.y - a.y};
	const float lenSq = ab.x * ab.x + ab.y * ab.y;
	const float t = lenSq > 0.0001f ? math::Clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.f, 1.f) : 0.f;
	const float dx = p.x - (a.x + ab.x * t), dy = p.y - (a.y + ab.y * t);
	return std::sqrt(dx * dx + dy * dy);
}

int64_t PackColor(Color c) noexcept {
	auto b = [](float f) { return int64_t(std::lround(math::Clamp(f, 0.f, 1.f) * 255.f)); };
	return (b(c.r) << 24) | (b(c.g) << 16) | (b(c.b) << 8) | b(c.a);
}

Color UnpackColor(int64_t v) noexcept {
	return Rgba(int((v >> 24) & 0xFF), int((v >> 16) & 0xFF), int((v >> 8) & 0xFF), int(v & 0xFF));
}

} // namespace detail

// ============================================================================
// Connexions
// ============================================================================

GraphConnection *ConnectPins(ecs::ArchetypeRegistry &world, ecs::Entity from, ecs::Entity to) {
	if (!from.Valid() || !to.Valid() || from == to)
		return nullptr;
	auto pf = world.GetComponent<GraphPin>(from);
	auto pt = world.GetComponent<GraphPin>(to);
	if (pf.IsNone() || pt.IsNone() || !pf.Unwrap()->connectable || !pt.Unwrap()->connectable)
		return nullptr;
	const ecs::Entity canvas = CanvasOfPin(world, from);
	if (!canvas.Valid() || CanvasOfPin(world, to) != canvas)
		return nullptr;
	auto cv = world.GetComponent<GraphCanvas>(canvas);
	for (const GraphConnection &c : cv.Unwrap()->connections)
		if ((c.fromPin == from && c.toPin == to) || (c.fromPin == to && c.toPin == from))
			return nullptr;
	GraphConnection conn;
	conn.fromPin = from;
	conn.toPin = to;
	cv.Unwrap()->connections.push_back(std::move(conn));
	pf.Unwrap()->connected = true;
	pt.Unwrap()->connected = true;
	return &cv.Unwrap()->connections.back();
}

void DisconnectPins(ecs::ArchetypeRegistry &world, ecs::Entity from, ecs::Entity to) {
	auto cv = world.GetComponent<GraphCanvas>(CanvasOfPin(world, from));
	if (cv.IsNone())
		return;
	std::vector<GraphConnection> &conns = cv.Unwrap()->connections;
	std::erase_if(conns, [&](const GraphConnection &c) {
		return (c.fromPin == from && c.toPin == to) || (c.fromPin == to && c.toPin == from);
	});
	auto still = [&](ecs::Entity pin) {
		for (const GraphConnection &c : conns)
			if (c.fromPin == pin || c.toPin == pin)
				return true;
		return false;
	};
	if (auto p = world.GetComponent<GraphPin>(from); p.IsSome())
		p.Unwrap()->connected = still(from);
	if (auto p = world.GetComponent<GraphPin>(to); p.IsSome())
		p.Unwrap()->connected = still(to);
}

void InsertWaypoint(GraphConnection &conn, size_t index, sdl3::FPoint canvasPoint) {
	index = std::min(index, conn.waypoints.size());
	conn.waypoints.insert(conn.waypoints.begin() + ptrdiff_t(index), canvasPoint);
}

void RemoveWaypoint(GraphConnection &conn, size_t index) {
	if (index < conn.waypoints.size())
		conn.waypoints.erase(conn.waypoints.begin() + ptrdiff_t(index));
}

// ============================================================================
// Sélection, presse-papiers
// ============================================================================

void SelectNode(ecs::ArchetypeRegistry &world, ecs::Entity node, bool additive) {
	const ecs::Entity canvas = CanvasOfNode(world, node);
	auto cv = world.GetComponent<GraphCanvas>(canvas);
	if (cv.IsNone())
		return;
	if (!additive)
		cv.Unwrap()->selection.selected.clear();
	cv.Unwrap()->selection.selected.insert(node);
	cv.Unwrap()->selection.anchor = node;
	NotifySelection(world, canvas);
}

void DeselectNode(ecs::ArchetypeRegistry &world, ecs::Entity node) {
	const ecs::Entity canvas = CanvasOfNode(world, node);
	auto cv = world.GetComponent<GraphCanvas>(canvas);
	if (cv.IsNone())
		return;
	cv.Unwrap()->selection.selected.erase(node);
	NotifySelection(world, canvas);
}

void ClearGraphSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvas) {
	auto cv = world.GetComponent<GraphCanvas>(canvas);
	if (cv.IsNone())
		return;
	cv.Unwrap()->selection.selected.clear();
	cv.Unwrap()->selection.anchor = ecs::Entity{};
	NotifySelection(world, canvas);
}

void SelectAllNodes(ecs::ArchetypeRegistry &world, ecs::Entity canvas) {
	auto cv = world.GetComponent<GraphCanvas>(canvas);
	if (cv.IsNone())
		return;
	for (ecs::Entity n : NodesOf(world, canvas))
		cv.Unwrap()->selection.selected.insert(n);
	NotifySelection(world, canvas);
}

bool IsNodeSelected(const ecs::ArchetypeRegistry &world, ecs::Entity node) {
	auto cv = world.GetComponent<GraphCanvas>(CanvasOfNode(world, node));
	return cv.IsSome() && cv.Unwrap()->selection.selected.count(node) > 0;
}

void DeleteGraphSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvas) {
	auto cv = world.GetComponent<GraphCanvas>(canvas);
	if (cv.IsNone())
		return;
	const std::vector<ecs::Entity> nodes(cv.Unwrap()->selection.selected.begin(), cv.Unwrap()->selection.selected.end());
	for (ecs::Entity n : nodes)
		DestroyGraphNode(world, n);
	NotifySelection(world, canvas);
}

void CopyGraphSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvas) {
	auto cv = world.GetComponent<GraphCanvas>(canvas);
	if (cv.IsNone())
		return;
	NodeGraphState &st = GraphState(world);
	st.clipboard.clear();
	for (ecs::Entity n : NodesOf(world, canvas)) {
		if (!cv.Unwrap()->selection.selected.count(n))
			continue;
		const GraphNode gn = *world.GetComponent<GraphNode>(n).Unwrap();
		NodeGraphState::ClipNode c{gn.title, gn.size, gn.headerColor, gn.resizable, gn.movable, gn.minWidth,
								   gn.minHeight, gn.headerHeight, gn.canvasPos, {}};
		for (ecs::Entity p : PinsOf(world, n)) {
			const GraphPin pin = *world.GetComponent<GraphPin>(p).Unwrap();
			c.pins.push_back({pin.shape, pin.color, pin.size, pin.side, pin.sideOffset, pin.connectable});
		}
		st.clipboard.push_back(std::move(c));
	}
}

void CutGraphSelection(ecs::ArchetypeRegistry &world, ecs::Entity canvas) {
	CopyGraphSelection(world, canvas);
	DeleteGraphSelection(world, canvas);
}

bool HasGraphClipboard(const ecs::ArchetypeRegistry &world) {
	auto st = world.resources.Get<NodeGraphState>();
	return st.IsSome() && !st.Unwrap()->clipboard.empty();
}

std::vector<Widget> PasteGraphClipboard(Widget canvas, sdl3::FPoint at) {
	std::vector<Widget> out;
	if (!canvas.Valid())
		return out;
	const std::vector<NodeGraphState::ClipNode> clip = GraphState(*canvas.World()).clipboard;
	if (clip.empty())
		return out;
	const sdl3::FPoint base = clip.front().pos;
	for (const NodeGraphState::ClipNode &c : clip) {
		Widget n = AddGraphNode(canvas, c.title, {at.x + c.pos.x - base.x, at.y + c.pos.y - base.y}, c.size, Column(),
								c.headerColor);
		if (auto gn = n.Get<GraphNode>(); gn.IsSome()) {
			gn.Unwrap()->resizable = c.resizable;
			gn.Unwrap()->movable = c.movable;
			gn.Unwrap()->minWidth = c.minWidth;
			gn.Unwrap()->minHeight = c.minHeight;
			gn.Unwrap()->headerHeight = c.headerHeight;
		}
		for (const NodeGraphState::ClipPin &p : c.pins)
			(void)AddGraphPin(n, p.side, p.sideOffset, p.shape, p.color, p.size);
		out.push_back(n);
	}
	return out;
}

// ============================================================================
// Sérialisation
// ============================================================================

namespace {

dataio::NodePtr PointNode(sdl3::FPoint p) {
	auto n = dataio::Node::MakeObject();
	n->Set("x", dataio::Node::MakeFloat(double(p.x)));
	n->Set("y", dataio::Node::MakeFloat(double(p.y)));
	return n;
}

double Number(const dataio::NodePtr &n) {
	if (!n)
		return 0.0;
	return n->IsInt() ? double(n->intValue) : n->floatValue;
}

sdl3::FPoint PointFrom(const dataio::NodePtr &n) {
	if (!n)
		return {};
	return {float(Number(n->Get("x"))), float(Number(n->Get("y")))};
}

} // namespace

dataio::NodePtr SerializeGraph(ecs::ArchetypeRegistry &world, ecs::Entity canvas) {
	auto root = dataio::Node::MakeObject();
	auto cv = world.GetComponent<GraphCanvas>(canvas);
	if (cv.IsNone())
		return root;
	root->Set("pan", PointNode(cv.Unwrap()->pan));
	root->Set("zoom", dataio::Node::MakeFloat(double(cv.Unwrap()->zoom)));
	std::unordered_map<uint32_t, String> pinKey;
	int pinCounter = 0;
	auto nodes = dataio::Node::MakeArray();
	auto sel = dataio::Node::MakeArray();
	int nodeCounter = 0;
	for (ecs::Entity e : NodesOf(world, canvas)) {
		const GraphNode gn = *world.GetComponent<GraphNode>(e).Unwrap();
		const String key = String("n") + String::From(nodeCounter++);
		auto n = dataio::Node::MakeObject();
		n->Set("key", dataio::Node::MakeString(key));
		n->Set("title", dataio::Node::MakeString(gn.title));
		n->Set("pos", PointNode(gn.canvasPos));
		n->Set("size", PointNode(gn.size));
		n->Set("header_color", dataio::Node::MakeInt(detail::PackColor(gn.headerColor)));
		n->Set("resizable", dataio::Node::MakeBool(gn.resizable));
		n->Set("movable", dataio::Node::MakeBool(gn.movable));
		n->Set("disabled", dataio::Node::MakeBool(gn.disabled));
		n->Set("min_w", dataio::Node::MakeFloat(double(gn.minWidth)));
		n->Set("min_h", dataio::Node::MakeFloat(double(gn.minHeight)));
		n->Set("header_h", dataio::Node::MakeFloat(double(gn.headerHeight)));
		float innerZoom = 1.f;
		for (Widget c : Widget(world, e).Children())
			if (c.Type() == "graph-node-body")
				if (auto inl = c.Get<InlineStyle>(); inl.IsSome() && inl.Unwrap()->own.zoom.IsSome())
					innerZoom = inl.Unwrap()->own.zoom.Unwrap();
		n->Set("inner_zoom", dataio::Node::MakeFloat(double(innerZoom)));
		auto pins = dataio::Node::MakeArray();
		for (ecs::Entity pe : PinsOf(world, e)) {
			const GraphPin pin = *world.GetComponent<GraphPin>(pe).Unwrap();
			const String pk = String("p") + String::From(pinCounter++);
			pinKey[pe.id] = pk;
			auto pn = dataio::Node::MakeObject();
			pn->Set("key", dataio::Node::MakeString(pk));
			pn->Set("shape", dataio::Node::MakeInt(int64_t(pin.shape)));
			pn->Set("side", dataio::Node::MakeInt(int64_t(pin.side)));
			pn->Set("side_offset", dataio::Node::MakeFloat(double(pin.sideOffset)));
			pn->Set("size", dataio::Node::MakeFloat(double(pin.size)));
			pn->Set("color", dataio::Node::MakeInt(detail::PackColor(pin.color)));
			pn->Set("connectable", dataio::Node::MakeBool(pin.connectable));
			pins->Push(pn);
		}
		n->Set("pins", pins);
		nodes->Push(n);
		if (gn.selected)
			sel->Push(dataio::Node::MakeString(key));
	}
	root->Set("nodes", nodes);
	root->Set("selection", sel);
	auto conns = dataio::Node::MakeArray();
	for (const GraphConnection &c : cv.Unwrap()->connections) {
		auto f = pinKey.find(c.fromPin.id), t = pinKey.find(c.toPin.id);
		if (f == pinKey.end() || t == pinKey.end())
			continue;
		auto cn = dataio::Node::MakeObject();
		cn->Set("from", dataio::Node::MakeString(f->second));
		cn->Set("to", dataio::Node::MakeString(t->second));
		cn->Set("color", dataio::Node::MakeInt(detail::PackColor(c.color)));
		cn->Set("thickness", dataio::Node::MakeFloat(double(c.thickness)));
		cn->Set("opacity", dataio::Node::MakeFloat(double(c.opacity)));
		cn->Set("style", dataio::Node::MakeInt(int64_t(c.style)));
		cn->Set("start_cap", dataio::Node::MakeInt(int64_t(c.startCap)));
		cn->Set("end_cap", dataio::Node::MakeInt(int64_t(c.endCap)));
		cn->Set("arrow_size", dataio::Node::MakeFloat(double(c.arrowSize)));
		cn->Set("bezier_control_offset", dataio::Node::MakeFloat(double(c.bezierControlOffset)));
		auto wps = dataio::Node::MakeArray();
		for (sdl3::FPoint wp : c.waypoints)
			wps->Push(PointNode(wp));
		cn->Set("waypoints", wps);
		conns->Push(cn);
	}
	root->Set("connections", conns);
	return root;
}

std::unordered_map<String, ecs::Entity> DeserializeGraph(Widget canvas, const dataio::NodePtr &root) {
	std::unordered_map<String, ecs::Entity> nodeByKey, pinByKey;
	if (!root || !canvas.Valid())
		return nodeByKey;
	ecs::ArchetypeRegistry &world = *canvas.World();
	if (auto cv = canvas.Get<GraphCanvas>(); cv.IsSome()) {
		if (auto pan = root->Get("pan"))
			cv.Unwrap()->pan = PointFrom(pan);
		if (auto zoom = root->Get("zoom"))
			cv.Unwrap()->zoom = float(Number(zoom));
	}
	if (auto nodes = root->Get("nodes"))
		for (size_t i = 0; i < nodes->GetSize(); ++i) {
			auto n = nodes->At(i);
			if (!n)
				continue;
			const String title = n->Get("title") ? n->Get("title")->stringValue : String();
			const sdl3::FPoint size = n->Get("size") ? PointFrom(n->Get("size")) : sdl3::FPoint{200.f, 120.f};
			const Color header = n->Get("header_color") ? detail::UnpackColor(n->Get("header_color")->intValue)
													   : Color{40 / 255.f, 90 / 255.f, 160 / 255.f, 1.f};
			const float innerZoom = n->Get("inner_zoom") ? float(Number(n->Get("inner_zoom"))) : 1.f;
			Widget node = AddGraphNode(canvas, title, PointFrom(n->Get("pos")), size, Column(), header, innerZoom);
			if (auto gn = node.Get<GraphNode>(); gn.IsSome()) {
				if (auto v = n->Get("resizable"))
					gn.Unwrap()->resizable = v->boolValue;
				if (auto v = n->Get("movable"))
					gn.Unwrap()->movable = v->boolValue;
				if (auto v = n->Get("disabled"))
					gn.Unwrap()->disabled = v->boolValue;
				if (auto v = n->Get("min_w"))
					gn.Unwrap()->minWidth = float(Number(v));
				if (auto v = n->Get("min_h"))
					gn.Unwrap()->minHeight = float(Number(v));
				if (auto v = n->Get("header_h"))
					gn.Unwrap()->headerHeight = float(Number(v));
			}
			const String key = n->Get("key") ? n->Get("key")->stringValue : String("n") + String::From(int(i));
			nodeByKey[key] = node.Entity();
			if (auto pins = n->Get("pins"))
				for (size_t j = 0; j < pins->GetSize(); ++j) {
					auto pn = pins->At(j);
					if (!pn)
						continue;
					Widget pin = AddGraphPin(node, pn->Get("side") ? PinSide(pn->Get("side")->intValue) : PinSide::Left,
											 pn->Get("side_offset") ? float(Number(pn->Get("side_offset"))) : 0.5f,
											 pn->Get("shape") ? PinShape(pn->Get("shape")->intValue) : PinShape::CIRCLE,
											 pn->Get("color") ? detail::UnpackColor(pn->Get("color")->intValue)
															  : Color{240 / 255.f, 242 / 255.f, 248 / 255.f, 1.f},
											 pn->Get("size") ? float(Number(pn->Get("size"))) : 10.f);
					if (auto v = pn->Get("connectable"))
						if (auto gp = pin.Get<GraphPin>(); gp.IsSome())
							gp.Unwrap()->connectable = v->boolValue;
					const String pk = pn->Get("key") ? pn->Get("key")->stringValue : String("p") + String::From(int(j));
					pinByKey[pk] = pin.Entity();
				}
		}
	if (auto conns = root->Get("connections"))
		for (size_t i = 0; i < conns->GetSize(); ++i) {
			auto cn = conns->At(i);
			if (!cn || !cn->Get("from") || !cn->Get("to"))
				continue;
			auto f = pinByKey.find(cn->Get("from")->stringValue), t = pinByKey.find(cn->Get("to")->stringValue);
			if (f == pinByKey.end() || t == pinByKey.end())
				continue;
			GraphConnection *conn = ConnectPins(world, f->second, t->second);
			if (!conn)
				continue;
			if (auto v = cn->Get("color"))
				conn->color = detail::UnpackColor(v->intValue);
			if (auto v = cn->Get("thickness"))
				conn->thickness = float(Number(v));
			if (auto v = cn->Get("opacity"))
				conn->opacity = float(Number(v));
			if (auto v = cn->Get("style"))
				conn->style = ConnectionStyle(v->intValue);
			if (auto v = cn->Get("start_cap"))
				conn->startCap = ConnectionEndCap(v->intValue);
			if (auto v = cn->Get("end_cap"))
				conn->endCap = ConnectionEndCap(v->intValue);
			if (auto v = cn->Get("arrow_size"))
				conn->arrowSize = float(Number(v));
			if (auto v = cn->Get("bezier_control_offset"))
				conn->bezierControlOffset = float(Number(v));
			if (auto wps = cn->Get("waypoints"))
				for (size_t w = 0; w < wps->GetSize(); ++w)
					conn->waypoints.push_back(PointFrom(wps->At(w)));
		}
	if (auto sel = root->Get("selection")) {
		ClearGraphSelection(world, canvas.Entity());
		for (size_t i = 0; i < sel->GetSize(); ++i)
			if (auto s = sel->At(i))
				if (auto it = nodeByKey.find(s->stringValue); it != nodeByKey.end())
					SelectNode(world, it->second, true);
	}
	MarkLayoutDirty(world);
	return nodeByKey;
}

// ============================================================================
// Extension : placement, dessin, interaction
// ============================================================================

namespace {

/// Place chaque nœud d'après sa position canevas, le pan et le zoom.
void TickGraphs(ecs::ArchetypeRegistry &world, Context &ctx, float dt) {
	NodeGraphState &st = GraphState(world);
	// Défilement automatique quand un nœud glissé approche du bord.
	if (st.drag == NodeGraphState::Drag::Move && world.IsAlive(st.canvas))
		if (auto cv = world.GetComponent<GraphCanvas>(st.canvas); cv.IsSome()) {
			const sdl3::FRect s = BoxOf(world, st.canvas);
			constexpr float EDGE = 24.f, SPEED = 480.f;
			sdl3::FPoint scroll{0.f, 0.f};
			if (st.mouse.x < s.x + EDGE)
				scroll.x = -SPEED * dt;
			else if (st.mouse.x > s.x + s.w - EDGE)
				scroll.x = SPEED * dt;
			if (st.mouse.y < s.y + EDGE)
				scroll.y = -SPEED * dt;
			else if (st.mouse.y > s.y + s.h - EDGE)
				scroll.y = SPEED * dt;
			if (scroll.x != 0.f || scroll.y != 0.f) {
				const float zoom = std::max(0.0001f, cv.Unwrap()->zoom);
				const sdl3::FPoint d{scroll.x / zoom, scroll.y / zoom};
				cv.Unwrap()->pan = {cv.Unwrap()->pan.x + d.x, cv.Unwrap()->pan.y + d.y};
				for (auto &[e, start] : st.groupStart) {
					start = {start.x + d.x, start.y + d.y};
					if (auto n = world.GetComponent<GraphNode>(e); n.IsSome())
						n.Unwrap()->canvasPos = {start.x + (st.mouse.x - st.startMouse.x) / zoom,
												 start.y + (st.mouse.y - st.startMouse.y) / zoom};
				}
			}
		}
	std::vector<std::pair<ecs::Entity, GraphCanvas *>> canvases;
	world.Query<GraphCanvas>([&](ecs::Entity e, GraphCanvas &cv) { canvases.push_back({e, &cv}); });
	for (auto [canvas, cvPtr] : canvases) {
		const GraphCanvas cv = *cvPtr;
		for (ecs::Entity n : NodesOf(world, canvas)) {
			const GraphNode gn = *world.GetComponent<GraphNode>(n).Unwrap();
			auto inl = world.GetComponent<InlineStyle>(n);
			if (inl.IsNone())
				continue;
			const StyleProps &own = inl.Unwrap()->own;
			const Length left = Length::Px(gn.canvasPos.x - cv.pan.x), top = Length::Px(gn.canvasPos.y - cv.pan.y);
			const Length w = Length::Px(gn.size.x), h = Length::Px(gn.size.y);
			const bool same = own.left.IsSome() && own.left.Unwrap() == left && own.top.IsSome() && own.top.Unwrap() == top &&
							  own.width.IsSome() && own.width.Unwrap() == w && own.height.IsSome() &&
							  own.height.Unwrap() == h && own.zoom.IsSome() && own.zoom.Unwrap() == cv.zoom;
			if (same)
				continue;
			Widget(world, n).Left(left).Top(top).Width(w).Height(h).Zoom(cv.zoom);
			// L'en-tête suit la hauteur d'en-tête du nœud.
			for (Widget c : Widget(world, n).Children())
				if (c.Type() == "graph-node-header")
					c.Height(gn.headerHeight);
			(void)ctx;
		}
	}
}

void DrawArrowHead(Painter &p, sdl3::FPoint from, sdl3::FPoint tip, float size, Color c) {
	const sdl3::FPoint d{tip.x - from.x, tip.y - from.y};
	const float len = std::sqrt(d.x * d.x + d.y * d.y);
	if (len < 0.0001f)
		return;
	const sdl3::FPoint u{d.x / len, d.y / len}, n{-u.y, u.x};
	const sdl3::FPoint back{tip.x - u.x * size, tip.y - u.y * size};
	const sdl3::FPoint tri[3] = {tip, {back.x + n.x * size * 0.5f, back.y + n.y * size * 0.5f},
								 {back.x - n.x * size * 0.5f, back.y - n.y * size * 0.5f}};
	p.FillPolygon(tri, c);
}

void DrawConnection(Painter &p, const GraphConnection &conn, std::span<const sdl3::FPoint> path, float zoom) {
	if (path.size() < 2)
		return;
	const Color col = WithAlpha(conn.color, math::Clamp(conn.color.a * conn.opacity, 0.f, 1.f));
	p.Polyline(path, std::max(1.f, conn.thickness * zoom), col);
	const float arrow = conn.arrowSize * zoom;
	if (conn.endCap == ConnectionEndCap::ARROW)
		DrawArrowHead(p, path[path.size() - 2], path.back(), arrow, col);
	if (conn.startCap == ConnectionEndCap::ARROW)
		DrawArrowHead(p, path[1], path.front(), arrow, col);
}

void DrawPin(Painter &p, const GraphPin &pin, const sdl3::FRect &box) {
	const sdl3::FPoint c{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	const float r = box.w * 0.5f;
	const Color col = pin.hovered ? Lighten(pin.color, 0.18f) : pin.color;
	switch (pin.shape) {
	case PinShape::CIRCLE:
		if (pin.connected)
			p.FillCircle(c, r, col);
		else
			p.StrokeCircle(c, r, 1.5f, col);
		break;
	case PinShape::SQUARE:
		if (pin.connected)
			p.FillRect(box, col);
		else
			p.StrokeRoundedRect(box, math::Corners(), math::Sides(1.5f), col);
		break;
	case PinShape::TRIANGLE: {
		const sdl3::FPoint tri[3] = {{c.x, box.y}, {box.x + box.w, box.y + box.h}, {box.x, box.y + box.h}};
		if (pin.connected)
			p.FillPolygon(tri, col);
		else
			p.Polyline(tri, 1.5f, col, true);
		break;
	}
	case PinShape::STAR: {
		const auto pts = detail::StarPoints(c, r, r * 0.45f, 5);
		if (pin.connected)
			p.FillPolygon(pts, col);
		else
			p.Polyline(pts, 1.5f, col, true);
		break;
	}
	}
	if (pin.selected)
		p.StrokeCircle(c, r + 3.f, 1.5f, Rgba(255, 200, 60));
}

void DrawGraphConnections(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e, const Rect &rect, const GraphCanvas &cv) {
	Painter &p = ctx.painter;
	NodeGraphState &st = GraphState(world);
	for (size_t ci = 0; ci < cv.connections.size(); ++ci) {
		const GraphConnection &conn = cv.connections[ci];
		if (!world.IsAlive(conn.fromPin) || !world.IsAlive(conn.toPin))
			continue;
		const sdl3::FPoint a = detail::PinCenter(PinScreen(world, conn.fromPin));
		const sdl3::FPoint b = detail::PinCenter(PinScreen(world, conn.toPin));
		std::vector<sdl3::FPoint> through;
		for (sdl3::FPoint wp : conn.waypoints)
			through.push_back(CanvasToScreen(wp, cv, rect.box));
		const auto path = detail::ConnectionPath(a, through, b, conn.style, conn.bezierControlOffset, cv.zoom);
		if (conn.onCustomDraw)
			conn.onCustomDraw(p, path, conn);
		else
			DrawConnection(p, conn, path, cv.zoom);
		for (size_t wi = 0; wi < through.size(); ++wi) {
			const bool dragging = st.drag == NodeGraphState::Drag::Waypoint && st.canvas == e &&
								  st.waypointConn == ptrdiff_t(ci) && st.waypointIndex == ptrdiff_t(wi);
			const float half = dragging ? 6.f : 4.5f;
			const sdl3::FRect box{through[wi].x - half, through[wi].y - half, half * 2.f, half * 2.f};
			p.FillRect(box, dragging ? Rgba(255, 220, 120) : Rgba(230, 232, 238));
			p.StrokeRoundedRect(box, math::Corners(), math::Sides(1.f), Rgba(20, 21, 28));
		}
	}
	if (st.drag == NodeGraphState::Drag::Connect && CanvasOfPin(world, st.fromPin) == e) {
		const sdl3::FPoint a = detail::PinCenter(PinScreen(world, st.fromPin));
		GraphConnection temp;
		temp.color = Rgba(255, 255, 255, 160);
		temp.thickness = 2.f;
		temp.endCap = ConnectionEndCap::NONE;
		DrawConnection(p, temp, detail::ConnectionPath(a, {}, st.mouse, ConnectionStyle::BEZIER, 0.f, cv.zoom), cv.zoom);
	}
}

void DrawGraphBackground(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e, const Rect &rect, const ComputedStyle &) {
	auto cvRef = world.GetComponent<GraphCanvas>(e);
	if (cvRef.IsNone())
		return;
	const GraphCanvas &cv = *cvRef.Unwrap();
	Painter &p = ctx.painter;
	const sdl3::FRect s = rect.box;
	p.FillRect(s, cv.bgColor);
	const float step = cv.gridSpacing * cv.zoom;
	if (step <= 3.f) {
		DrawGraphConnections(world, ctx, e, rect, cv);
		return;
	}
	float offX = std::fmod(-cv.pan.x * cv.zoom, step), offY = std::fmod(-cv.pan.y * cv.zoom, step);
	if (offX < 0.f)
		offX += step;
	if (offY < 0.f)
		offY += step;
	int ix = int((-cv.pan.x * cv.zoom - offX) / step + 0.5f);
	for (float x = s.x + offX; x < s.x + s.w; x += step, ++ix)
		p.FillRect({std::floor(x), s.y, 1.f, s.h}, (ix % cv.gridMajorEvery == 0) ? cv.gridColorMajor : cv.gridColor);
	int iy = int((-cv.pan.y * cv.zoom - offY) / step + 0.5f);
	for (float y = s.y + offY; y < s.y + s.h; y += step, ++iy)
		p.FillRect({s.x, std::floor(y), s.w, 1.f}, (iy % cv.gridMajorEvery == 0) ? cv.gridColorMajor : cv.gridColor);
	DrawGraphConnections(world, ctx, e, rect, cv);
}

void DrawGraphOverlay(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e, const Rect &rect, const ComputedStyle &) {
	auto cvRef = world.GetComponent<GraphCanvas>(e);
	if (cvRef.IsNone())
		return;
	Painter &p = ctx.painter;
	NodeGraphState &st = GraphState(world);
	p.PushClip(rect.box);
	for (ecs::Entity n : NodesOf(world, e))
		for (ecs::Entity pin : PinsOf(world, n)) {
			const GraphPin gp = *world.GetComponent<GraphPin>(pin).Unwrap();
			const sdl3::FRect box = PinScreen(world, pin);
			if (gp.onCustomDraw)
				gp.onCustomDraw(p, box, gp);
			else
				DrawPin(p, gp, box);
		}
	if (st.drag == NodeGraphState::Drag::Marquee && st.canvas == e) {
		const sdl3::FRect r{std::min(st.startMouse.x, st.mouse.x), std::min(st.startMouse.y, st.mouse.y),
							std::fabs(st.mouse.x - st.startMouse.x), std::fabs(st.mouse.y - st.startMouse.y)};
		p.FillRect(r, Rgba(120, 170, 255, 60));
		p.StrokeRoundedRect(r, math::Corners(), math::Sides(1.f), Rgba(140, 190, 255, 220));
	}
	p.PopClip();
}

bool GraphKey(ecs::ArchetypeRegistry &world, const sdl3::Event &ev) {
	if (!ev.IsKeyDown())
		return false;
	const ecs::Entity focus = FocusedWidget(world);
	const ecs::Entity canvas = NearestWith<GraphCanvas>(world, focus);
	if (!canvas.Valid() || focus != canvas)
		return false; // un champ d'un nœud garde ses touches
	const bool ctrl = (ev.Key().mod & SDL_KMOD_CTRL) != 0;
	switch (ev.Keycode()) {
	case SDLK_DELETE:
	case SDLK_BACKSPACE:
		DeleteGraphSelection(world, canvas);
		return true;
	case SDLK_C:
		if (!ctrl)
			return false;
		CopyGraphSelection(world, canvas);
		return true;
	case SDLK_X:
		if (!ctrl)
			return false;
		CutGraphSelection(world, canvas);
		return true;
	case SDLK_V: {
		if (!ctrl)
			return false;
		auto cv = world.GetComponent<GraphCanvas>(canvas);
		const NodeGraphState &st = GraphState(world);
		const sdl3::FPoint at = ScreenToCanvas(st.mouse, *cv.Unwrap(), BoxOf(world, canvas));
		(void)PasteGraphClipboard(Widget(world, canvas), at);
		return true;
	}
	case SDLK_A:
		if (!ctrl)
			return false;
		SelectAllNodes(world, canvas);
		return true;
	default:
		return false;
	}
}

bool GraphPointer(ecs::ArchetypeRegistry &world, Context &ctx, const sdl3::Event &ev) {
	NodeGraphState &st = GraphState(world);
	sdl3::FPoint m = st.mouse;
	if (ev.IsMouseMotion())
		m = {ev.MouseMotion().x, ev.MouseMotion().y};
	else if (ev.IsMouseDown() || ev.IsMouseUp())
		m = {ev.MouseButton().x, ev.MouseButton().y};
	else if (ev.IsMouseWheel())
		m = {ev.MouseWheel().mouse_x, ev.MouseWheel().mouse_y};
	st.mouse = m;
	const bool ctrlDown = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
	const bool shiftDown = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;

	// ── Gestes en cours ─────────────────────────────────────────────────────
	if (st.drag != NodeGraphState::Drag::None) {
		const ecs::Entity canvas = st.canvas;
		auto cv = world.GetComponent<GraphCanvas>(canvas);
		if (cv.IsNone()) {
			st.drag = NodeGraphState::Drag::None;
			return false;
		}
		const float zoom = std::max(0.0001f, cv.Unwrap()->zoom);
		const sdl3::FPoint delta{(m.x - st.startMouse.x) / zoom, (m.y - st.startMouse.y) / zoom};
		if (ev.IsMouseMotion()) {
			switch (st.drag) {
			case NodeGraphState::Drag::Move:
				for (auto &[e, start] : st.groupStart)
					if (auto n = world.GetComponent<GraphNode>(e); n.IsSome())
						n.Unwrap()->canvasPos = {start.x + delta.x, start.y + delta.y};
				break;
			case NodeGraphState::Drag::Resize:
				if (auto n = world.GetComponent<GraphNode>(st.node); n.IsSome())
					n.Unwrap()->size = {std::max(n.Unwrap()->minWidth, st.startValue.x + delta.x),
										std::max(n.Unwrap()->minHeight, st.startValue.y + delta.y)};
				break;
			case NodeGraphState::Drag::Waypoint: {
				auto &conns = cv.Unwrap()->connections;
				if (st.waypointConn >= 0 && size_t(st.waypointConn) < conns.size()) {
					auto &wps = conns[size_t(st.waypointConn)].waypoints;
					if (st.waypointIndex >= 0 && size_t(st.waypointIndex) < wps.size())
						wps[size_t(st.waypointIndex)] = {st.startValue.x + delta.x, st.startValue.y + delta.y};
				}
				break;
			}
			case NodeGraphState::Drag::Pan:
				cv.Unwrap()->pan = {st.startValue.x - delta.x, st.startValue.y - delta.y};
				break;
			default:
				break;
			}
			ctx.MarkLayoutDirty();
			return true;
		}
		if (ev.IsMouseUp()) {
			const auto drag = st.drag;
			st.drag = NodeGraphState::Drag::None;
			switch (drag) {
			case NodeGraphState::Drag::Connect: {
				const ecs::Entity target = HitPin(world, m);
				if (target.Valid() && target != st.fromPin)
					(void)ConnectPins(world, st.fromPin, target);
				break;
			}
			case NodeGraphState::Drag::Move:
			case NodeGraphState::Drag::Resize: {
				auto cb = world.GetComponent<GraphCallbacks>(canvas);
				const GraphCallbacks callbacks = cb.IsSome() ? *cb.Unwrap() : GraphCallbacks{};
				for (auto &[e, start] : st.groupStart)
					if (auto n = world.GetComponent<GraphNode>(e); n.IsSome())
						n.Unwrap()->draggingMove = false;
				if (auto n = world.GetComponent<GraphNode>(st.node); n.IsSome()) {
					n.Unwrap()->draggingResize = false;
					if (drag == NodeGraphState::Drag::Resize && callbacks.onNodeResized)
						callbacks.onNodeResized(st.node, n.Unwrap()->size);
				}
				if (drag == NodeGraphState::Drag::Move && callbacks.onNodeMoved)
					for (auto &[e, start] : st.groupStart)
						if (auto n = world.GetComponent<GraphNode>(e); n.IsSome())
							callbacks.onNodeMoved(e, n.Unwrap()->canvasPos);
				st.groupStart.clear();
				break;
			}
			case NodeGraphState::Drag::Marquee: {
				const sdl3::FRect r{std::min(st.startMouse.x, m.x), std::min(st.startMouse.y, m.y),
									std::fabs(m.x - st.startMouse.x), std::fabs(m.y - st.startMouse.y)};
				if (!st.marqueeAdditive)
					cv.Unwrap()->selection.selected.clear();
				for (ecs::Entity n : NodesOf(world, canvas))
					if (!world.GetComponent<GraphNode>(n).Unwrap()->disabled && r.Intersects(BoxOf(world, n)))
						cv.Unwrap()->selection.selected.insert(n);
				cv.Unwrap()->selection.anchor = ecs::Entity{};
				NotifySelection(world, canvas);
				break;
			}
			default:
				break;
			}
			ctx.MarkLayoutDirty();
			return true;
		}
		return true;
	}

	// ── Survol des pins ─────────────────────────────────────────────────────
	if (ev.IsMouseMotion()) {
		const ecs::Entity front = HitTest(world, m);
		const bool overCanvas = NearestWith<GraphCanvas>(world, front).Valid();
		const ecs::Entity hovered = overCanvas ? HitPin(world, m) : ecs::Entity{};
		world.Query<GraphPin>([&](ecs::Entity e, GraphPin &pin) { pin.hovered = e == hovered; });
		return hovered.Valid();
	}

	if (!ev.IsMouseDown() && !ev.IsMouseWheel())
		return false;
	const ecs::Entity front = HitTest(world, m);
	const ecs::Entity canvas = NearestWith<GraphCanvas>(world, front);
	if (!canvas.Valid())
		return false;
	auto cv = world.GetComponent<GraphCanvas>(canvas);
	const sdl3::FRect cs = BoxOf(world, canvas);
	const ecs::Entity node = NearestWith<GraphNode>(world, front);

	if (ev.IsMouseWheel()) {
		// Un conteneur défilant dans un nœud garde sa molette.
		for (Option<ecs::Entity> cur = Some(front); cur.IsSome() && cur.Unwrap() != canvas;
			 cur = ecs::ParentOf(world, cur.Unwrap()))
			if (auto sc = world.GetComponent<Scroll>(cur.Unwrap()); sc.IsSome() && sc.Unwrap()->MaxOffset().y > 0.f)
				return false;
		const float factor = ev.MouseWheel().y > 0.f ? 1.1f : 1.f / 1.1f;
		const float newZoom = math::Clamp(cv.Unwrap()->zoom * factor, cv.Unwrap()->minZoom, cv.Unwrap()->maxZoom);
		const sdl3::FPoint under = ScreenToCanvas(m, *cv.Unwrap(), cs);
		cv.Unwrap()->pan = {under.x - (m.x - cs.x) / newZoom, under.y - (m.y - cs.y) / newZoom};
		cv.Unwrap()->zoom = newZoom;
		ctx.MarkLayoutDirty();
		return true;
	}

	const uint8_t button = ev.MouseButton().button;
	if (button == SDL_BUTTON_RIGHT) {
		auto cb = world.GetComponent<GraphCallbacks>(canvas);
		if (cb.IsNone() || !cb.Unwrap()->onContextMenu)
			return false;
		const auto fn = cb.Unwrap()->onContextMenu;
		fn(node, HitPin(world, m), m);
		return true;
	}
	if (button == SDL_BUTTON_MIDDLE) {
		st.drag = NodeGraphState::Drag::Pan;
		st.canvas = canvas;
		st.startMouse = m;
		st.startValue = cv.Unwrap()->pan;
		return true;
	}
	if (button != SDL_BUTTON_LEFT)
		return false;
	SetFocus(world, canvas, false);
	// Pin : nouvelle connexion
	if (const ecs::Entity pin = HitPin(world, m); pin.Valid() && CanvasOfPin(world, pin) == canvas) {
		st.drag = NodeGraphState::Drag::Connect;
		st.canvas = canvas;
		st.fromPin = pin;
		return true;
	}
	// Points intermédiaires : double clic supprime, simple clic glisse.
	for (size_t ci = 0; ci < cv.Unwrap()->connections.size(); ++ci) {
		auto &wps = cv.Unwrap()->connections[ci].waypoints;
		for (size_t wi = 0; wi < wps.size(); ++wi) {
			const sdl3::FPoint sp = CanvasToScreen(wps[wi], *cv.Unwrap(), cs);
			if (std::fabs(m.x - sp.x) <= 7.f && std::fabs(m.y - sp.y) <= 7.f) {
				if (ev.MouseButton().clicks >= 2) {
					RemoveWaypoint(cv.Unwrap()->connections[ci], wi);
				} else {
					st.drag = NodeGraphState::Drag::Waypoint;
					st.canvas = canvas;
					st.waypointConn = ptrdiff_t(ci);
					st.waypointIndex = ptrdiff_t(wi);
					st.startMouse = m;
					st.startValue = wps[wi];
				}
				return true;
			}
		}
	}
	// Double clic sur le tracé : nouveau point intermédiaire.
	if (ev.MouseButton().clicks >= 2 && front == canvas)
		for (size_t ci = 0; ci < cv.Unwrap()->connections.size(); ++ci) {
			GraphConnection &conn = cv.Unwrap()->connections[ci];
			std::vector<sdl3::FPoint> ctrl{detail::PinCenter(PinScreen(world, conn.fromPin))};
			for (sdl3::FPoint wp : conn.waypoints)
				ctrl.push_back(CanvasToScreen(wp, *cv.Unwrap(), cs));
			ctrl.push_back(detail::PinCenter(PinScreen(world, conn.toPin)));
			for (size_t i = 0; i + 1 < ctrl.size(); ++i)
				if (detail::DistanceToSegment(m, ctrl[i], ctrl[i + 1]) <= 8.f) {
					InsertWaypoint(conn, i, ScreenToCanvas(m, *cv.Unwrap(), cs));
					return true;
				}
		}
	if (node.Valid()) {
		GraphNode &gn = *world.GetComponent<GraphNode>(node).Unwrap();
		if (gn.disabled)
			return true;
		const ecs::Entity grip = NearestWith<Shape>(world, front);
		const bool onGrip = grip.Valid() && Widget(world, grip).Type() == "graph-node-grip";
		bool onHeader = false;
		for (Option<ecs::Entity> cur = Some(front); cur.IsSome() && cur.Unwrap() != node; cur = ecs::ParentOf(world, cur.Unwrap()))
			if (Widget(world, cur.Unwrap()).Type() == "graph-node-header")
				onHeader = true;
		if (onGrip && gn.resizable) {
			gn.draggingResize = true;
			st.drag = NodeGraphState::Drag::Resize;
			st.canvas = canvas;
			st.node = node;
			st.startMouse = m;
			st.startValue = gn.size;
			return true;
		}
		if (!onHeader && !(front == node))
			return false; // corps : aux widgets du contenu
		const bool already = cv.Unwrap()->selection.selected.count(node) > 0;
		if (ctrlDown || shiftDown || !already) {
			ApplyNodeSelectionClick(cv.Unwrap()->selection, node, ctrlDown, shiftDown, NodesOf(world, canvas));
			NotifySelection(world, canvas);
		}
		if (!ctrlDown && !shiftDown && gn.movable) {
			auto gn2 = world.GetComponent<GraphNode>(node);
			gn2.Unwrap()->draggingMove = true;
			st.drag = NodeGraphState::Drag::Move;
			st.canvas = canvas;
			st.node = node;
			st.startMouse = m;
			st.groupStart.clear();
			for (ecs::Entity sel : cv.Unwrap()->selection.selected)
				if (auto n = world.GetComponent<GraphNode>(sel); n.IsSome())
					st.groupStart.push_back({sel, n.Unwrap()->canvasPos});
		}
		return true;
	}
	if (front == canvas) {
		st.drag = NodeGraphState::Drag::Marquee;
		st.canvas = canvas;
		st.startMouse = m;
		st.marqueeAdditive = ctrlDown;
		return true;
	}
	return false;
}

} // namespace

void RegisterNodeGraphExtension(Context &ctx) {
	Extension ext;
	ext.name = "nodegraph";
	ext.tick = TickGraphs;
	ext.draw = DrawGraphBackground;
	ext.drawOver = DrawGraphOverlay;
	ext.event = [](ecs::ArchetypeRegistry &world, Context &c, const sdl3::Event &ev) {
		bool any = false;
		world.Query<GraphCanvas>([&](ecs::Entity, GraphCanvas &) { any = true; });
		if (!any)
			return false;
		if (ev.IsKeyDown())
			return GraphKey(world, ev);
		return GraphPointer(world, c, ev);
	};
	RegisterExtension(ctx, std::move(ext));
}

} // namespace ui
