// Définitions de modeling/inference.hpp
#include <modeling/inference.hpp>
#include <modeling/triangulate.hpp>

#include <cmath>

namespace modeling {

const char *InferenceLabel(InferenceKind kind) noexcept {
	switch (kind) {
	case InferenceKind::GROUND: return "Sol";
	case InferenceKind::ON_FACE: return "Sur la face";
	case InferenceKind::ON_EDGE: return "Sur l'arête";
	case InferenceKind::MIDPOINT: return "Milieu";
	case InferenceKind::ENDPOINT: return "Extrémité";
	case InferenceKind::ORIGIN: return "Origine";
	case InferenceKind::ON_AXIS_X: return "Sur l'axe rouge";
	case InferenceKind::ON_AXIS_Y: return "Sur l'axe vert";
	case InferenceKind::ON_AXIS_Z: return "Sur l'axe bleu";
	case InferenceKind::LOCKED_AXIS: return "Axe verrouillé";
	case InferenceKind::ON_PLANE: return "Sur le plan d'esquisse";
	case InferenceKind::NONE: break;
	}
	return "";
}

Color InferenceColor(InferenceKind kind) noexcept {
	switch (kind) {
	case InferenceKind::ENDPOINT: return {0.1f, 0.75f, 0.2f, 1.f}; // vert (comme SketchUp)
	case InferenceKind::MIDPOINT: return {0.15f, 0.55f, 0.95f, 1.f};
	case InferenceKind::ON_EDGE: return {0.9f, 0.15f, 0.15f, 1.f};
	case InferenceKind::ON_FACE: return {0.25f, 0.35f, 0.95f, 1.f};
	case InferenceKind::ORIGIN: return {0.95f, 0.85f, 0.2f, 1.f};
	case InferenceKind::ON_AXIS_X: return {0.95f, 0.2f, 0.2f, 1.f};
	case InferenceKind::ON_AXIS_Y: return {0.2f, 0.85f, 0.25f, 1.f};
	case InferenceKind::ON_AXIS_Z: return {0.25f, 0.45f, 1.f, 1.f};
	case InferenceKind::LOCKED_AXIS: return {0.95f, 0.4f, 0.9f, 1.f};
	case InferenceKind::ON_PLANE: return {0.45f, 0.55f, 0.75f, 1.f};
	case InferenceKind::GROUND:
	case InferenceKind::NONE: break;
	}
	return {0.6f, 0.6f, 0.6f, 1.f};
}

Vec3 AxisDirection(InferenceKind kind) noexcept {
	switch (kind) {
	case InferenceKind::ON_AXIS_X: return {1.f, 0.f, 0.f};
	case InferenceKind::ON_AXIS_Y: return {0.f, 1.f, 0.f};
	case InferenceKind::ON_AXIS_Z: return {0.f, 0.f, 1.f};
	default: return {0.f, 0.f, 0.f};
	}
}

Vec3 ClosestPointOnLine(const Vec3 &origin, const Vec3 &dirIn, const math::FRay &ray) noexcept {
	// Points les plus proches de deux droites (formule de Lumelsky).
	const Vec3 d1 = dirIn.LengthSq() > 1e-20f ? dirIn.Normalize() : Vec3{1.f, 0.f, 0.f};
	const Vec3 d2 = ray.direction;
	const Vec3 r = origin - ray.origin;
	const float b = d1.Dot(d2), c = d1.Dot(r), f = d2.Dot(r);
	const float denom = 1.f - b * b;
	if (std::fabs(denom) < 1e-8f)
		return origin; // parallèles
	const float s = (b * f - c) / denom;
	return origin + d1 * s;
}

Vec3 ClosestPointOnSegment(const Vec3 &a, const Vec3 &b, const math::FRay &ray) noexcept {
	const Vec3 ab = b - a;
	const float len = ab.Length();
	if (len < 1e-12f)
		return a;
	const Vec3 p = ClosestPointOnLine(a, ab, ray);
	const float t = math::Clamp((p - a).Dot(ab) / (len * len), 0.f, 1.f);
	return a + ab * t;
}

Option<Vec3> IntersectPlane(const math::FRay &ray, const Vec3 &point, const Vec3 &normal) noexcept {
	const float denom = normal.Dot(ray.direction);
	if (std::fabs(denom) < 1e-8f)
		return NONE;
	const float t = (point - ray.origin).Dot(normal) / denom;
	if (t <= 0.f)
		return NONE;
	return Some(ray.At(t));
}

namespace {

struct Candidate {
	Inference inference;
	int priority = 99;
	float pixels = 1e30f;
};

} // namespace

Inference Infer(const Model &model, const InferenceQuery &query, const InferenceSettings &settings) {
	// Axe imposé : tout point est contraint sur la droite issue de l'ancrage.
	if (query.anchor.IsSome() && query.lockedAxis.IsSome()) {
		Inference locked;
		locked.kind = InferenceKind::LOCKED_AXIS;
		locked.point = ClosestPointOnLine(query.anchor.Value(), query.lockedAxis.Value(), query.ray);
		return locked;
	}

	const float tol = settings.pixelTolerance;
	const Option<RayHit> hit = Raycast(model, query.ray);
	const float occluder = hit.IsSome() ? hit.Value().distance : 1e30f;
	auto visible = [&](const Vec3 &p) {
		// Un point au-delà de la face touchée est caché (petite marge pour
		// les points SUR la face, ses propres sommets en particulier).
		return (p - query.ray.origin).Dot(query.ray.direction) <= occluder + 1e-3f;
	};
	auto pixels = [&](const Vec3 &p) -> float {
		const Option<math::FVector2> s = query.project(p);
		return s.IsSome() ? s.Value().Distance(query.cursor) : 1e30f;
	};

	const Option<InferenceSettings::WorkPlane> &plane = settings.workPlane;
	const Vec3 planeNormal = plane.IsSome() ? plane.Value().normal.Normalize() : Vec3{0.f, 1.f, 0.f};
	auto onPlane = [&](const Vec3 &p) {
		return plane.IsNone() || std::fabs((p - plane.Value().origin).Dot(planeNormal)) <= 1e-4f;
	};

	Candidate best;
	auto offer = [&](const Inference &inf, int priority, float px) {
		if (px > tol || !onPlane(inf.point))
			return;
		if (priority < best.priority || (priority == best.priority && px < best.pixels))
			best = Candidate{inf, priority, px};
	};

	// Extrémités : sommets utilisés par une arête vivante.
	std::vector<bool> used(model.Vertices().size(), false);
	for (const Edge &e : model.Edges())
		if (e.alive)
			used[e.a] = used[e.b] = true;
	for (size_t v = 0; v < model.Vertices().size(); ++v) {
		if (!used[v] || !model.Vertices()[v].alive)
			continue;
		const Vec3 &p = model.Vertices()[v].position;
		if (!visible(p))
			continue;
		Inference inf;
		inf.kind = InferenceKind::ENDPOINT;
		inf.point = p;
		inf.vertex = VertexId(v);
		offer(inf, 0, pixels(p));
	}
	if (settings.inferOrigin) {
		Inference inf;
		inf.kind = InferenceKind::ORIGIN;
		offer(inf, 0, visible(inf.point) ? pixels(inf.point) + 0.5f : 1e30f);
	}
	// Milieux et points sur les arêtes.
	for (size_t e = 0; e < model.Edges().size(); ++e) {
		const Edge &edge = model.Edges()[e];
		if (!edge.alive)
			continue;
		const Vec3 a = model.Position(edge.a), b = model.Position(edge.b);
		const Vec3 mid = (a + b) * 0.5f;
		if (!edge.soft && visible(mid)) {
			Inference inf;
			inf.kind = InferenceKind::MIDPOINT;
			inf.point = mid;
			inf.edge = EdgeId(e);
			offer(inf, 1, pixels(mid));
		}
		const Vec3 on = ClosestPointOnSegment(a, b, query.ray);
		if (visible(on)) {
			Inference inf;
			inf.kind = InferenceKind::ON_EDGE;
			inf.point = on;
			inf.edge = EdgeId(e);
			offer(inf, 2, pixels(on));
		}
	}
	// Axes rouge/vert/bleu depuis le point d'ancrage.
	if (settings.inferAxes && query.anchor.IsSome()) {
		for (InferenceKind axis : {InferenceKind::ON_AXIS_X, InferenceKind::ON_AXIS_Y, InferenceKind::ON_AXIS_Z}) {
			const Vec3 p = ClosestPointOnLine(query.anchor.Value(), AxisDirection(axis), query.ray);
			if (p.DistanceSq(query.anchor.Value()) < 1e-10f)
				continue;
			Inference inf;
			inf.kind = axis;
			inf.point = p;
			offer(inf, 3, pixels(p));
		}
	}
	// Face touchée : sur un plan d'esquisse, seulement si elle lui est coplanaire.
	const bool hitUsable = hit.IsSome() && (plane.IsNone() || (onPlane(hit.Value().point) &&
																 std::fabs(hit.Value().normal.Dot(planeNormal)) > 0.999f));
	if (best.inference.IsValid()) {
		if (plane.IsSome()) {
			best.inference.planeNormal = planeNormal;
			if (hitUsable)
				best.inference.face = hit.Value().face;
		} else if (hit.IsSome()) {
			best.inference.face = hit.Value().face;
			best.inference.planeNormal = hit.Value().normal;
		}
		return best.inference;
	}
	if (plane.IsSome()) {
		if (hitUsable) {
			Inference inf;
			inf.kind = InferenceKind::ON_FACE;
			inf.point = hit.Value().point;
			inf.face = hit.Value().face;
			inf.planeNormal = planeNormal;
			return inf;
		}
		if (Option<Vec3> p = IntersectPlane(query.ray, plane.Value().origin, planeNormal); p.IsSome()) {
			Inference inf;
			inf.kind = InferenceKind::ON_PLANE;
			inf.point = p.Unwrap();
			inf.planeNormal = planeNormal;
			return inf;
		}
		return Inference{};
	}
	// Sinon : la face sous le curseur, puis le sol.
	if (hit.IsSome()) {
		Inference inf;
		inf.kind = InferenceKind::ON_FACE;
		inf.point = hit.Value().point;
		inf.face = hit.Value().face;
		inf.planeNormal = hit.Value().normal;
		return inf;
	}
	if (Option<Vec3> ground = IntersectPlane(query.ray, {0.f, settings.groundY, 0.f}, {0.f, 1.f, 0.f}); ground.IsSome()) {
		Inference inf;
		inf.kind = InferenceKind::GROUND;
		inf.point = ground.Unwrap();
		if (settings.gridStep > 0.f) {
			inf.point.x = std::round(inf.point.x / settings.gridStep) * settings.gridStep;
			inf.point.z = std::round(inf.point.z / settings.gridStep) * settings.gridStep;
		}
		return inf;
	}
	return Inference{};
}

} // namespace modeling
