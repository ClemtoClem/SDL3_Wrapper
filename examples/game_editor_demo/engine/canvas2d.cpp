// Définitions de canvas2d.hpp
#include "canvas2d.hpp"

namespace game_editor {

// ── Transform2D ──────────────────────────────────────────────────────────────

Transform2D Transform2D::Rotate(float degrees) noexcept {
	const float r = degrees * DEG2RAD, cs = std::cos(r), sn = std::sin(r);
	return {cs, sn, -sn, cs, 0.f, 0.f};
}

math::FVector2 Transform2D::Apply(math::FVector2 p) const noexcept {
	return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty};
}

Option<Transform2D> Transform2D::Inverse() const noexcept {
	const float det = Determinant();
	if (std::fabs(det) < 1e-12f)
		return NONE;
	const float inv = 1.f / det;
	Transform2D r{d * inv, -b * inv, -c * inv, a * inv, 0.f, 0.f};
	r.tx = -(r.a * tx + r.c * ty);
	r.ty = -(r.b * tx + r.d * ty);
	return Some(r);
}

Transform2D Local2D(const scene::Transform &t) noexcept {
	return Transform2D::Translate(t.position.x, t.position.y) * Transform2D::Rotate(t.EulerDegrees().z) *
		   Transform2D::Scale(t.scale.x, t.scale.y);
}

math::FVector2 EstimateText(const String &text, float fontSize) {
	size_t glyphs = 0;
	for (size_t i = 0; i < text.GetSize(); ++i)
		if ((uint8_t(text.CStr()[i]) & 0xC0) != 0x80) // octets de tête UTF-8
			++glyphs;
	return {float(glyphs) * fontSize * 0.56f, fontSize * 1.25f};
}

// ── Canvas2DFrame ────────────────────────────────────────────────────────────

void Canvas2DFrame::Sort() {
	std::stable_sort(items.begin(), items.end(), [](const DrawItem2D &x, const DrawItem2D &y) {
		return x.item.z != y.item.z ? x.item.z < y.item.z : x.order < y.order;
	});
}

Canvas2DFrame BuildCanvasFrame(const SceneDesc &scene) {
	Canvas2DFrame frame;
	size_t order = 0;
	const scene::NodeTree &tree = scene.tree;
	std::function<void(scene::NodeId, Transform2D, Space2D)> visit = [&](scene::NodeId id, Transform2D parent,
																		 Space2D space) {
		const scene::Node *node = tree.Get(id);
		if (!node || !node->visible)
			return;
		Transform2D world = parent;
		Space2D childSpace = space;
		if (Is2DNode(*node)) {
			world = parent * Local2D(node->transform);
			if (node->type == node_kind::CANVAS_LAYER) {
				childSpace = Space2D::SCREEN;
				world = Local2D(node->transform); // le calque part de l'écran
			}
			if (CanvasItemDesc::Has(*node))
				frame.items.push_back(DrawItem2D{id, CanvasItemDesc::Read(*node), world, childSpace, order++});
			if (Camera2DDesc::Has(*node)) {
				const Camera2DDesc camera = Camera2DDesc::Read(*node);
				Camera2DState state{id, world.Origin(), camera.zoom > 0.001f ? camera.zoom : 0.001f};
				frame.cameras.push_back(state);
				if (camera.current && frame.camera.IsNone())
					frame.camera = Some(state);
			}
		} else {
			world = Transform2D::Identity();
			childSpace = Space2D::WORLD;
		}
		for (scene::NodeId child : tree.ChildrenOf(id))
			visit(child, world, childSpace);
	};
	for (scene::NodeId child : tree.ChildrenOf(tree.Root()))
		visit(child, Transform2D::Identity(), Space2D::WORLD);
	frame.Sort();
	return frame;
}

Option<NodePlacement2D> PlaceNode2D(const SceneDesc &scene, scene::NodeId id) {
	const scene::NodeTree &tree = scene.tree;
	const scene::Node *target = tree.Get(id);
	if (!target || !Is2DNode(*target))
		return NONE;
	std::vector<const scene::Node *> chain; // du nœud vers la racine (exclue)
	for (scene::NodeId at = id; at.Valid() && at != tree.Root(); at = tree.ParentOf(at))
		if (const scene::Node *node = tree.Get(at))
			chain.push_back(node);
	NodePlacement2D placement;
	for (size_t i = chain.size(); i-- > 0;) {
		const scene::Node &node = *chain[i];
		if (!Is2DNode(node)) {
			placement = NodePlacement2D{};
			continue;
		}
		placement.parent = placement.transform;
		if (node.type == node_kind::CANVAS_LAYER) {
			placement.parent = Transform2D::Identity();
			placement.transform = Local2D(node.transform);
			if (i > 0)
				placement.space = Space2D::SCREEN;
		} else {
			placement.transform = placement.transform * Local2D(node.transform);
		}
	}
	return Some(placement);
}

// ── Bounds2D ─────────────────────────────────────────────────────────────────

bool Bounds2D::Contains(math::FVector2 p) const noexcept {
	return p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y;
}

void Bounds2D::Grow(float margin) noexcept {
	min.x -= margin;
	min.y -= margin;
	max.x += margin;
	max.y += margin;
}

Bounds2D LocalBounds(const CanvasItemDesc &item, const TextMeasure &measure) {
	auto box = [&item](math::FVector2 size) {
		return item.centered ? Bounds2D{{-size.x * 0.5f, -size.y * 0.5f}, {size.x * 0.5f, size.y * 0.5f}}
							 : Bounds2D{{0.f, 0.f}, size};
	};
	switch (item.kind) {
		case CanvasItemKind::POLYGON:
		case CanvasItemKind::LINE: {
			if (item.points.empty())
				return Bounds2D{};
			Bounds2D b{item.points.front(), item.points.front()};
			for (const math::FVector2 &p : item.points) {
				b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y)};
				b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y)};
			}
			b.Grow(item.outline * 0.5f);
			return b;
		}
		case CanvasItemKind::TEXT: {
			const math::FVector2 size = measure ? measure(item.text, item.fontSize) : EstimateText(item.text, item.fontSize);
			const float left = item.align == TextAlign2D::LEFT	 ? 0.f
							   : item.align == TextAlign2D::RIGHT ? -size.x
																  : -size.x * 0.5f;
			const float top = item.centered ? -size.y * 0.5f : 0.f;
			return Bounds2D{{left, top}, {left + size.x, top + size.y}};
		}
		default:
			return box(item.size);
	}
}

float DistanceToSegment(math::FVector2 p, math::FVector2 a, math::FVector2 b) noexcept {
	const float dx = b.x - a.x, dy = b.y - a.y;
	const float len2 = dx * dx + dy * dy;
	float t = len2 > 0.f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.f;
	t = std::clamp(t, 0.f, 1.f);
	const float qx = a.x + t * dx - p.x, qy = a.y + t * dy - p.y;
	return std::sqrt(qx * qx + qy * qy);
}

bool PointInPolygon(math::FVector2 p, const std::vector<math::FVector2> &poly) noexcept {
	bool inside = false;
	for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
		const math::FVector2 &a = poly[i], &b = poly[j];
		if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
			inside = !inside;
	}
	return inside;
}

bool HitsLocal(const CanvasItemDesc &item, math::FVector2 local, float slack, const TextMeasure &measure) {
	switch (item.kind) {
		case CanvasItemKind::CIRCLE: {
			const math::FVector2 center = item.centered ? math::FVector2{} : item.size * 0.5f;
			const float rx = std::max(item.size.x * 0.5f, 0.001f) + slack;
			const float ry = std::max(item.size.y * 0.5f, 0.001f) + slack;
			const float nx = (local.x - center.x) / rx, ny = (local.y - center.y) / ry;
			return nx * nx + ny * ny <= 1.f;
		}
		case CanvasItemKind::POLYGON:
			if (item.points.size() >= 3 && PointInPolygon(local, item.points))
				return true;
			[[fallthrough]];
		case CanvasItemKind::LINE: {
			const float reach = std::max(item.outline, 1.f) * 0.5f + slack;
			const size_t n = item.points.size();
			const size_t segments = item.kind == CanvasItemKind::POLYGON ? n : (n > 0 ? n - 1 : 0);
			for (size_t i = 0; i < segments; ++i)
				if (DistanceToSegment(local, item.points[i], item.points[(i + 1) % n]) <= reach)
					return true;
			return false;
		}
		default: {
			Bounds2D bounds = LocalBounds(item, measure);
			bounds.Grow(slack);
			return bounds.Contains(local);
		}
	}
}

// ── View2D ───────────────────────────────────────────────────────────────────

const Transform2D & View2D::For(Space2D space) const noexcept {
	return space == Space2D::SCREEN ? screen : world;
}

View2D View2D::Game(sdl3::FRect viewport, math::FVector2 reference, const Option<Camera2DState> &camera) {
	View2D view;
	const float scale = std::max(1e-4f, std::min(viewport.w / reference.x, viewport.h / reference.y));
	const float ox = viewport.x + (viewport.w - reference.x * scale) * 0.5f;
	const float oy = viewport.y + (viewport.h - reference.y * scale) * 0.5f;
	view.screen = Transform2D::Translate(ox, oy) * Transform2D::Scale(scale);
	view.frame = sdl3::FRect{ox, oy, reference.x * scale, reference.y * scale};
	Transform2D worldToReference = Transform2D::Identity();
	if (camera.IsSome())
		worldToReference = Transform2D::Translate(reference * 0.5f) * Transform2D::Scale(camera.Value().zoom) *
						   Transform2D::Translate(camera.Value().position * -1.f);
	view.world = view.screen * worldToReference;
	return view;
}

View2D View2D::Editor(sdl3::FRect viewport, math::FVector2 reference, math::FVector2 center, float zoom) {
	View2D view;
	view.world = Transform2D::Translate(viewport.x + viewport.w * 0.5f, viewport.y + viewport.h * 0.5f) *
				 Transform2D::Scale(zoom) * Transform2D::Translate(center * -1.f);
	view.screen = view.world;
	const math::FVector2 corner = view.world.Apply({0.f, 0.f});
	view.frame = sdl3::FRect{corner.x, corner.y, reference.x * zoom, reference.y * zoom};
	return view;
}

float View2D::FitZoom(sdl3::FRect viewport, math::FVector2 reference, float margin) {
	return std::max(1e-3f, std::min(viewport.w / reference.x, viewport.h / reference.y) * margin);
}

Option<size_t> PickItem(const std::vector<DrawItem2D> &items, const View2D &view,
		math::FVector2 pixel, float slackPixels,
		const TextMeasure &measure) {
	for (size_t i = items.size(); i-- > 0;) {
		const DrawItem2D &item = items[i];
		const Transform2D toPixels = view.For(item.space) * item.transform;
		Option<Transform2D> inverse = toPixels.Inverse();
		if (inverse.IsNone())
			continue;
		const float slack = slackPixels / std::max(1e-4f, toPixels.MeanScale());
		if (HitsLocal(item.item, inverse.Value().Apply(pixel), slack, measure))
			return Some(i);
	}
	return NONE;
}

std::array<math::FVector2, 4> ScreenCorners(const DrawItem2D &item, const View2D &view, const TextMeasure &measure) {
	const Bounds2D b = LocalBounds(item.item, measure);
	const Transform2D t = view.For(item.space) * item.transform;
	return {t.Apply(b.min), t.Apply({b.max.x, b.min.y}), t.Apply(b.max), t.Apply({b.min.x, b.max.y})};
}

bool Overlaps(const DrawItem2D &x, const DrawItem2D &y, const TextMeasure &measure) {
	auto corners = [&measure](const DrawItem2D &item) {
		const Bounds2D b = LocalBounds(item.item, measure);
		const Transform2D &t = item.transform;
		return std::array<math::FVector2, 4>{t.Apply(b.min), t.Apply({b.max.x, b.min.y}), t.Apply(b.max),
											 t.Apply({b.min.x, b.max.y})};
	};
	const auto p = corners(x), q = corners(y);
	auto separated = [](const std::array<math::FVector2, 4> &s, const std::array<math::FVector2, 4> &o) {
		for (int i = 0; i < 2; ++i) {
			const math::FVector2 edge{s[i + 1].x - s[i].x, s[i + 1].y - s[i].y};
			const math::FVector2 axis{-edge.y, edge.x};
			float minS = 1e30f, maxS = -1e30f, minO = 1e30f, maxO = -1e30f;
			for (const math::FVector2 &v : s) {
				const float d = v.x * axis.x + v.y * axis.y;
				minS = std::min(minS, d);
				maxS = std::max(maxS, d);
			}
			for (const math::FVector2 &v : o) {
				const float d = v.x * axis.x + v.y * axis.y;
				minO = std::min(minO, d);
				maxO = std::max(maxO, d);
			}
			if (maxS < minO || maxO < minS)
				return true;
		}
		return false;
	};
	return !separated(p, q) && !separated(q, p);
}

} // namespace game_editor
