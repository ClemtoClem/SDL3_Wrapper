// Définitions de gizmo.hpp
#include <editor/gizmo.hpp>
#include <editor/theme.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

namespace {

constexpr int RING_SEGMENTS = 64;

struct ModeName {
	GizmoMode mode;
	const char *id;
};
constexpr ModeName MODES[] = {{GizmoMode::Translate, "translate"}, {GizmoMode::Rotate, "rotate"}, {GizmoMode::Scale, "scale"}};

math::FVector3 AxisOf(GizmoHandle h) noexcept {
	switch (h) {
	case GizmoHandle::X: return {1.f, 0.f, 0.f};
	case GizmoHandle::Y: return {0.f, 1.f, 0.f};
	case GizmoHandle::Z: return {0.f, 0.f, 1.f};
	// Plans : leur normale.
	case GizmoHandle::YZ: return {1.f, 0.f, 0.f};
	case GizmoHandle::ZX: return {0.f, 1.f, 0.f};
	case GizmoHandle::XY: return {0.f, 0.f, 1.f};
	default: return {0.f, 0.f, 0.f};
	}
}

bool IsAxis(GizmoHandle h) noexcept { return h == GizmoHandle::X || h == GizmoHandle::Y || h == GizmoHandle::Z; }
bool IsPlane(GizmoHandle h) noexcept { return h == GizmoHandle::XY || h == GizmoHandle::YZ || h == GizmoHandle::ZX; }

sdl3::FColor ColorOf(GizmoHandle h) noexcept {
	switch (h) {
	case GizmoHandle::X:
	case GizmoHandle::YZ: return AXIS_X;
	case GizmoHandle::Y:
	case GizmoHandle::ZX: return AXIS_Y;
	case GizmoHandle::Z:
	case GizmoHandle::XY: return AXIS_Z;
	default: return sdl3::FColor{0.9f, 0.9f, 0.9f, 1.f};
	}
}

/// Les deux axes d'un plan (dans l'ordre de son nom).
std::pair<math::FVector3, math::FVector3> PlaneAxes(GizmoHandle h) noexcept {
	switch (h) {
	case GizmoHandle::XY: return {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
	case GizmoHandle::YZ: return {{0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
	default: return {{0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}};
	}
}

float SegmentDistance(math::FVector2 p, math::FVector2 a, math::FVector2 b) noexcept {
	const math::FVector2 ab = b - a;
	const float len2 = ab.Dot(ab);
	const float t = len2 > 1e-6f ? std::clamp((p - a).Dot(ab) / len2, 0.f, 1.f) : 0.f;
	return (p - (a + ab * t)).Length();
}

/// Point dans un quadrilatère convexe projeté (signes des produits vectoriels).
bool InsideQuad(math::FVector2 p, const math::FVector2 (&q)[4]) noexcept {
	int positive = 0, negative = 0;
	for (int i = 0; i < 4; ++i) {
		const math::FVector2 e = q[(i + 1) % 4] - q[i], v = p - q[i];
		const float cross = e.x * v.y - e.y * v.x;
		(cross >= 0.f ? positive : negative)++;
	}
	return positive == 4 || negative == 4;
}

float Snap(float value, float step) noexcept { return step > 0.f ? std::round(value / step) * step : value; }

Option<math::FVector2> Project(const GizmoView &view, const math::FVector3 &p) {
	return view.camera.WorldToScreen(p, view.width, view.height);
}

} // namespace

const char *GizmoModeId(GizmoMode mode) noexcept {
	for (const ModeName &m : MODES)
		if (m.mode == mode)
			return m.id;
	return "translate";
}

Option<GizmoMode> ParseGizmoMode(StringView id) noexcept {
	for (const ModeName &m : MODES)
		if (id == StringView(m.id))
			return Some(m.mode);
	return NONE;
}

float TransformGizmo::WorldSize(const GizmoView &view) const noexcept {
	const float distance = (view.camera.position - position).Length();
	return distance * 2.f * std::tan(view.camera.fovYRadians * 0.5f) * sizePixels / std::max(1.f, view.height);
}

GizmoHandle TransformGizmo::Pick(const GizmoView &view, float x, float y) const {
	if (!visible)
		return GizmoHandle::None;
	const Option<math::FVector2> center = Project(view, position);
	if (center.IsNone())
		return GizmoHandle::None;
	const math::FVector2 cursor{x, y};
	const float s = WorldSize(view);
	if (mode == GizmoMode::Rotate) {
		GizmoHandle best = GizmoHandle::None;
		float bestDistance = pickRadius;
		for (GizmoHandle h : {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z}) {
			const auto [u, v] = PlaneAxes(h == GizmoHandle::X ? GizmoHandle::YZ : h == GizmoHandle::Y ? GizmoHandle::ZX : GizmoHandle::XY);
			Option<math::FVector2> previous = NONE;
			for (int i = 0; i <= RING_SEGMENTS; ++i) {
				const float a = 2.f * 3.14159265f * float(i) / float(RING_SEGMENTS);
				const Option<math::FVector2> p = Project(view, position + (u * std::cos(a) + v * std::sin(a)) * (s * 0.9f));
				if (p.IsSome() && previous.IsSome()) {
					const float d = SegmentDistance(cursor, previous.Value(), p.Value());
					if (d < bestDistance)
						bestDistance = d, best = h;
				}
				previous = p;
			}
		}
		return best;
	}
	if ((cursor - center.Value()).Length() <= pickRadius * 1.4f)
		return GizmoHandle::All;
	if (mode == GizmoMode::Translate)
		for (GizmoHandle h : {GizmoHandle::XY, GizmoHandle::YZ, GizmoHandle::ZX}) {
			const auto [u, v] = PlaneAxes(h);
			const math::FVector3 corners[4] = {position + (u * 0.22f + v * 0.22f) * s, position + (u * 0.42f + v * 0.22f) * s,
											   position + (u * 0.42f + v * 0.42f) * s, position + (u * 0.22f + v * 0.42f) * s};
			math::FVector2 quad[4];
			bool ok = true;
			for (int i = 0; i < 4 && ok; ++i) {
				const Option<math::FVector2> p = Project(view, corners[i]);
				ok = p.IsSome();
				if (ok)
					quad[i] = p.Value();
			}
			if (ok && InsideQuad(cursor, quad))
				return h;
		}
	GizmoHandle best = GizmoHandle::None;
	float bestDistance = pickRadius;
	for (GizmoHandle h : {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z}) {
		const Option<math::FVector2> tip = Project(view, position + AxisOf(h) * s);
		if (tip.IsNone())
			continue;
		const float d = SegmentDistance(cursor, center.Value(), tip.Value());
		if (d < bestDistance)
			bestDistance = d, best = h;
	}
	return best;
}

GizmoHandle TransformGizmo::Hover(const GizmoView &view, float x, float y) {
	m_hovered = Active() ? m_active : Pick(view, x, y);
	return m_hovered;
}

Option<math::FVector3> TransformGizmo::PlaneHit(const math::FRay &ray, const math::FVector3 &normal) const {
	const float denom = normal.Dot(ray.direction);
	if (std::fabs(denom) < 1e-6f)
		return NONE;
	const float t = normal.Dot(m_startPosition - ray.origin) / denom;
	if (t < 0.f)
		return NONE;
	return Some(ray.origin + ray.direction * t);
}

float TransformGizmo::AxisParameter(const math::FRay &ray, const math::FVector3 &axis) const {
	// Point de l'axe (passant par la position de départ) le plus proche du
	// rayon : minimise |w + t·d − s·r|².
	const math::FVector3 w = m_startPosition - ray.origin;
	const float b = axis.Dot(ray.direction);
	const float denom = 1.f - b * b;
	if (denom < 1e-6f)
		return 0.f; // axe vu de bout
	return (b * ray.direction.Dot(w) - axis.Dot(w)) / denom;
}

bool TransformGizmo::Begin(const GizmoView &view, float x, float y) {
	const GizmoHandle h = Pick(view, x, y);
	if (h == GizmoHandle::None)
		return false;
	m_startPosition = position;
	const math::FRay ray = view.camera.ScreenPointToRay(x, y, view.width, view.height);
	const float s = WorldSize(view);
	if (mode == GizmoMode::Scale && h == GizmoHandle::All) {
		const Option<math::FVector2> c = Project(view, position);
		m_startScreenCenter = c.IsSome() ? c.Value() : math::FVector2{x, y};
		m_startScreenDistance = std::max(4.f, (math::FVector2{x, y} - m_startScreenCenter).Length());
	} else if (IsAxis(h) && mode != GizmoMode::Rotate) {
		m_startParameter = AxisParameter(ray, AxisOf(h));
		if (mode == GizmoMode::Scale && std::fabs(m_startParameter) < s * 1e-3f)
			m_startParameter = s;
	} else {
		math::FVector3 normal = AxisOf(h);
		if (h == GizmoHandle::All) // déplacement dans le plan de l'écran
			normal = (view.camera.target - view.camera.position).Normalize();
		const Option<math::FVector3> hit = PlaneHit(ray, normal);
		if (hit.IsNone())
			return false;
		m_planeNormal = normal;
		m_startHit = hit.Value();
	}
	m_active = h;
	m_hovered = h;
	return true;
}

GizmoDelta TransformGizmo::Drag(const GizmoView &view, float x, float y) const {
	GizmoDelta delta;
	if (!Active())
		return delta;
	const math::FRay ray = view.camera.ScreenPointToRay(x, y, view.width, view.height);
	switch (mode) {
	case GizmoMode::Translate:
		if (IsAxis(m_active)) {
			const float t = Snap(AxisParameter(ray, AxisOf(m_active)) - m_startParameter, translateSnap);
			delta.translation = AxisOf(m_active) * t;
		} else if (const Option<math::FVector3> hit = PlaneHit(ray, m_planeNormal); hit.IsSome()) {
			const math::FVector3 d = hit.Value() - m_startHit;
			delta.translation = {Snap(d.x, translateSnap), Snap(d.y, translateSnap), Snap(d.z, translateSnap)};
			if (IsPlane(m_active)) // reste rigoureusement dans le plan
				delta.translation = delta.translation - m_planeNormal * delta.translation.Dot(m_planeNormal);
		}
		break;
	case GizmoMode::Rotate:
		if (const Option<math::FVector3> hit = PlaneHit(ray, m_planeNormal); hit.IsSome()) {
			const math::FVector3 v0 = m_startHit - m_startPosition, v1 = hit.Value() - m_startPosition;
			delta.rotationAxis = m_planeNormal;
			delta.angle = Snap(std::atan2(m_planeNormal.Dot(v0.Cross(v1)), v0.Dot(v1)), rotateSnap);
		}
		break;
	case GizmoMode::Scale:
		if (m_active == GizmoHandle::All) {
			const float d = (math::FVector2{x, y} - m_startScreenCenter).Length();
			const float f = std::max(0.001f, Snap(d / m_startScreenDistance, scaleSnap));
			delta.scale = {f, f, f};
		} else if (IsAxis(m_active)) {
			const float f = std::max(0.001f, Snap(AxisParameter(ray, AxisOf(m_active)) / m_startParameter, scaleSnap));
			const math::FVector3 a = AxisOf(m_active);
			delta.scale = {a.x > 0.f ? f : 1.f, a.y > 0.f ? f : 1.f, a.z > 0.f ? f : 1.f};
		}
		break;
	}
	return delta;
}

std::vector<GizmoStroke> TransformGizmo::Strokes(const GizmoView &view) const {
	std::vector<GizmoStroke> out;
	if (!visible)
		return out;
	const Option<math::FVector2> center = Project(view, position);
	if (center.IsNone())
		return out;
	const float s = WorldSize(view);
	const GizmoHandle lit = Active() ? m_active : m_hovered;
	auto color = [&](GizmoHandle h) { return h == lit ? HIGHLIGHT : ColorOf(h); };
	auto line = [&](const math::FVector3 &a, const math::FVector3 &b, sdl3::FColor c, float width) {
		const Option<math::FVector2> pa = Project(view, a), pb = Project(view, b);
		if (pa.IsSome() && pb.IsSome())
			out.push_back({pa.Value(), pb.Value(), c, width});
	};
	auto square = [&](math::FVector2 at, float half, sdl3::FColor c) {
		const math::FVector2 p[4] = {{at.x - half, at.y - half}, {at.x + half, at.y - half}, {at.x + half, at.y + half}, {at.x - half, at.y + half}};
		for (int i = 0; i < 4; ++i)
			out.push_back({p[i], p[(i + 1) % 4], c, 2.f});
	};
	if (mode == GizmoMode::Rotate) {
		for (GizmoHandle h : {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z}) {
			const auto [u, v] = PlaneAxes(h == GizmoHandle::X ? GizmoHandle::YZ : h == GizmoHandle::Y ? GizmoHandle::ZX : GizmoHandle::XY);
			for (int i = 0; i < RING_SEGMENTS; ++i) {
				const float a0 = 2.f * 3.14159265f * float(i) / float(RING_SEGMENTS);
				const float a1 = 2.f * 3.14159265f * float(i + 1) / float(RING_SEGMENTS);
				line(position + (u * std::cos(a0) + v * std::sin(a0)) * (s * 0.9f),
					 position + (u * std::cos(a1) + v * std::sin(a1)) * (s * 0.9f), color(h), h == lit ? 3.f : 2.f);
			}
		}
		return out;
	}
	if (mode == GizmoMode::Translate)
		for (GizmoHandle h : {GizmoHandle::XY, GizmoHandle::YZ, GizmoHandle::ZX}) {
			const auto [u, v] = PlaneAxes(h);
			const math::FVector3 q[4] = {position + (u * 0.22f + v * 0.22f) * s, position + (u * 0.42f + v * 0.22f) * s,
										 position + (u * 0.42f + v * 0.42f) * s, position + (u * 0.22f + v * 0.42f) * s};
			for (int i = 0; i < 4; ++i)
				line(q[i], q[(i + 1) % 4], color(h), 1.5f);
		}
	for (GizmoHandle h : {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z}) {
		const math::FVector3 tip = position + AxisOf(h) * s;
		line(position, tip, color(h), h == lit ? 3.5f : 2.5f);
		const Option<math::FVector2> pt = Project(view, tip);
		if (pt.IsNone())
			continue;
		if (mode == GizmoMode::Scale) {
			square(pt.Value(), 4.f, color(h));
		} else { // pointe de flèche
			math::FVector2 dir = pt.Value() - center.Value();
			const float length = dir.Length();
			if (length < 1e-3f)
				continue;
			dir = dir * (1.f / length);
			const math::FVector2 normal{-dir.y, dir.x};
			const math::FVector2 back = pt.Value() - dir * 10.f;
			out.push_back({pt.Value(), back + normal * 5.f, color(h), 2.5f});
			out.push_back({pt.Value(), back - normal * 5.f, color(h), 2.5f});
		}
	}
	square(center.Value(), 5.f, color(GizmoHandle::All));
	return out;
}

} // namespace editor
