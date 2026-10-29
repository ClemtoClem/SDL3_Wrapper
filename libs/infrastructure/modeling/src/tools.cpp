// Définitions de modeling/tools.hpp
#include <modeling/operations.hpp>
#include <modeling/tools.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace modeling {

// ── Noms, raccourcis, cotes ──────────────────────────────────────────────────

const char *ToolName(ToolKind kind) noexcept {
	switch (kind) {
	case ToolKind::SELECT: return "Sélection";
	case ToolKind::LINE: return "Ligne";
	case ToolKind::RECTANGLE: return "Rectangle";
	case ToolKind::CIRCLE: return "Cercle";
	case ToolKind::POLYGON: return "Polygone";
	case ToolKind::PUSH_PULL: return "Pousser/Tirer";
	case ToolKind::OFFSET: return "Décalage";
	case ToolKind::FOLLOW_ME: return "Suivez-moi";
	case ToolKind::MOVE: return "Déplacer";
	case ToolKind::ROTATE: return "Rotation";
	case ToolKind::SCALE: return "Échelle";
	case ToolKind::ERASER: return "Gomme";
	case ToolKind::PAINT: return "Pot de peinture";
	case ToolKind::TAPE: return "Mètre";
	}
	return "";
}

char ToolShortcut(ToolKind kind) noexcept {
	switch (kind) {
	case ToolKind::SELECT: return ' ';
	case ToolKind::LINE: return 'l';
	case ToolKind::RECTANGLE: return 'r';
	case ToolKind::CIRCLE: return 'c';
	case ToolKind::POLYGON: return 'g';
	case ToolKind::PUSH_PULL: return 'p';
	case ToolKind::OFFSET: return 'f';
	case ToolKind::FOLLOW_ME: return 'w';
	case ToolKind::MOVE: return 'm';
	case ToolKind::ROTATE: return 'q';
	case ToolKind::SCALE: return 's';
	case ToolKind::ERASER: return 'e';
	case ToolKind::PAINT: return 'b';
	case ToolKind::TAPE: return 't';
	}
	return '\0';
}

Option<ToolKind> ToolFromShortcut(char key) noexcept {
	for (ToolKind k : ALL_TOOLS)
		if (ToolShortcut(k) == key)
			return Some(k);
	return NONE;
}

std::vector<float> ParseMeasurement(const String &text) {
	std::vector<float> values;
	String token;
	auto flush = [&]() -> bool {
		if (token.IsEmpty())
			return true;
		String t = token.ToLower().Replace(',', '.');
		token = String();
		float scale = 1.f;
		if (t.EndsWith("mm")) {
			scale = 0.001f;
			t = t.Substr(0, t.GetSize() - 2);
		} else if (t.EndsWith("cm")) {
			scale = 0.01f;
			t = t.Substr(0, t.GetSize() - 2);
		} else if (t.EndsWith("m")) {
			t = t.Substr(0, t.GetSize() - 1);
		} else if (t.EndsWith("°")) {
			t = t.Substr(0, t.GetSize() - String("°").GetSize());
		}
		if (t.IsEmpty())
			return false;
		char *end = nullptr;
		const float v = std::strtof(t.CStr(), &end);
		if (!end || *end != '\0')
			return false;
		values.push_back(v * scale);
		return true;
	};
	for (size_t i = 0; i < text.GetSize(); ++i) {
		const char c = text.CharAt(i);
		if (c == ';' || c == ' ' || c == 'x' || c == 'X' || c == '\t') {
			if (!flush())
				return {};
		} else {
			token += String(c);
		}
	}
	if (!flush())
		return {};
	return values;
}

String FormatLength(float meters) {
	return String::Format("%.3f m", double(meters)).Replace('.', ',');
}

String FormatAngle(float radians) {
	return String::Format("%.1f°", double(radians * 180.f / math::PI)).Replace('.', ',');
}

// ── Sélection ────────────────────────────────────────────────────────────────

bool Selection::HasFace(FaceId f) const { return std::find(faces.begin(), faces.end(), f) != faces.end(); }
bool Selection::HasEdge(EdgeId e) const { return std::find(edges.begin(), edges.end(), e) != edges.end(); }

void Selection::ToggleFace(FaceId f) {
	if (HasFace(f))
		std::erase(faces, f);
	else
		faces.push_back(f);
}

void Selection::ToggleEdge(EdgeId e) {
	if (HasEdge(e))
		std::erase(edges, e);
	else
		edges.push_back(e);
}

void Selection::Prune(const Model &model) {
	std::erase_if(faces, [&](FaceId f) { return !model.IsAlive(f); });
	std::erase_if(edges, [&](EdgeId e) { return !model.IsEdgeAlive(e); });
}

std::vector<VertexId> Selection::Vertices(const Model &model) const { return VerticesOf(model, faces, edges); }

ToolResult Tool::Type(ToolContext &, const std::vector<float> &) { return {}; }

namespace {

constexpr float EPS = 1e-6f;

ToolResult Failed(const String &message) {
	ToolResult r;
	r.error = Some(message);
	return r;
}

ToolResult Done(const String &message, bool changed = true) {
	ToolResult r;
	r.changed = changed;
	r.message = message;
	return r;
}

/// Base orthonormée d'un plan de dessin : axes rouge/bleu au sol.
void PlaneAxes(const Vec3 &normal, Vec3 &u, Vec3 &v) {
	if (std::fabs(normal.y) > 0.999f) {
		u = {1.f, 0.f, 0.f};
		v = {0.f, 0.f, normal.y > 0.f ? -1.f : 1.f};
		v = normal.Cross(u);
	} else {
		const PlaneInfo p = PlaneInfo::Make({0.f, 0.f, 0.f}, normal);
		u = p.u;
		v = p.v;
	}
}

/// Point du plan sous le curseur : le point inféré s'il est dans le plan,
/// sinon l'intersection du rayon.
Vec3 OnPlane(const ToolEvent &e, const Vec3 &origin, const Vec3 &normal) {
	if (e.at.IsValid() && std::fabs((e.at.point - origin).Dot(normal)) < 1e-4f)
		return e.at.point;
	if (Option<Vec3> p = IntersectPlane(e.ray, origin, normal); p.IsSome())
		return p.Unwrap();
	if (Option<Vec3> p = IntersectPlane(e.ray, origin, normal * -1.f); p.IsSome())
		return p.Unwrap();
	// Rayon parallèle : projection orthogonale du point inféré.
	return e.at.point - normal * (e.at.point - origin).Dot(normal);
}

/// Vertex du modèle correspondant au point inféré (coupe l'arête si besoin).
VertexId ResolveVertex(Model &m, const Inference &at) {
	if (at.kind == InferenceKind::ENDPOINT && at.vertex != INVALID_ID && m.Vertices()[at.vertex].alive)
		return at.vertex;
	if ((at.kind == InferenceKind::ON_EDGE || at.kind == InferenceKind::MIDPOINT) && m.IsEdgeAlive(at.edge))
		return m.SplitEdge(at.edge, at.point);
	return m.AddVertex(at.point);
}

std::vector<std::pair<Vec3, Vec3>> LoopLines(const std::vector<Vec3> &pts) {
	std::vector<std::pair<Vec3, Vec3>> out;
	for (size_t i = 0; i < pts.size(); ++i)
		out.push_back({pts[i], pts[(i + 1) % pts.size()]});
	return out;
}

/// Lignes des arêtes touchant un ensemble de sommets, déplacées par `move`.
std::vector<std::pair<Vec3, Vec3>> MovedEdges(const Model &m, const std::vector<VertexId> &verts,
											  const math::FMatrix4 &move) {
	std::vector<bool> moving(m.Vertices().size(), false);
	for (VertexId v : verts)
		if (v < moving.size())
			moving[v] = true;
	std::vector<std::pair<Vec3, Vec3>> out;
	for (const Edge &e : m.Edges()) {
		if (!e.alive || (!moving[e.a] && !moving[e.b]))
			continue;
		const Vec3 a = moving[e.a] ? move.TransformPoint(m.Position(e.a)) : m.Position(e.a);
		const Vec3 b = moving[e.b] ? move.TransformPoint(m.Position(e.b)) : m.Position(e.b);
		out.push_back({a, b});
	}
	return out;
}

/// Cible d'un outil de transformation : la sélection, sinon ce qui est
/// sous le curseur (face ou arête).
std::vector<VertexId> TargetVertices(const ToolContext &ctx, const Inference &at, std::vector<FaceId> *faces = nullptr) {
	if (!ctx.selection.IsEmpty()) {
		if (faces)
			*faces = ctx.selection.faces;
		return ctx.selection.Vertices(ctx.model);
	}
	if (at.edge != INVALID_ID && (at.kind == InferenceKind::ON_EDGE || at.kind == InferenceKind::MIDPOINT))
		return VerticesOf(ctx.model, {}, {at.edge});
	if (at.face != INVALID_ID) {
		if (faces)
			*faces = {at.face};
		return VerticesOf(ctx.model, {at.face});
	}
	return {};
}

// ── Sélection ────────────────────────────────────────────────────────────────

class SelectTool final : public Tool {
public:
	SelectTool() : Tool(ToolKind::SELECT) {}
	void Reset() override {}
	bool IsActive() const override { return false; }
	void Hover(ToolContext &, const ToolEvent &e) override { m_hover = e.at; }
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override {
		const bool add = e.shift || e.ctrl;
		if (!add)
			ctx.selection.Clear();
		if (e.at.edge != INVALID_ID && (e.at.kind == InferenceKind::ON_EDGE || e.at.kind == InferenceKind::MIDPOINT)) {
			ctx.selection.ToggleEdge(e.at.edge);
			return Done(String("Arête sélectionnée"), false);
		}
		if (e.at.face != INVALID_ID) {
			ctx.selection.ToggleFace(e.at.face);
			return Done(String::Format("%zu face(s) sélectionnée(s)", ctx.selection.faces.size()), false);
		}
		return Done(String(), false);
	}
	ToolPreview Preview(const ToolContext &ctx) const override {
		ToolPreview p;
		p.hint = String("Cliquez une face ou une arête (Maj : ajouter/retirer)");
		if (m_hover.face != INVALID_ID && ctx.model.IsAlive(m_hover.face))
			p.polygon = ctx.model.OuterPoints(m_hover.face);
		return p;
	}

private:
	Inference m_hover;
};

// ── Ligne ────────────────────────────────────────────────────────────────────

class LineTool final : public Tool {
public:
	LineTool() : Tool(ToolKind::LINE) {}
	void Reset() override {
		m_chain.clear();
		m_ids.clear();
	}
	bool IsActive() const override { return !m_ids.empty(); }
	void Hover(ToolContext &, const ToolEvent &e) override { m_cursor = e.at.point; }
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override { return Place(ctx, e.at); }
	ToolResult Type(ToolContext &ctx, const std::vector<float> &values) override {
		if (m_ids.empty() || values.empty() || values[0] <= 0.f)
			return Failed(String("Ligne : orientez la ligne puis tapez sa longueur"));
		const Vec3 dir = m_cursor - m_chain.back();
		if (dir.LengthSq() < EPS)
			return Failed(String("Ligne : orientez d'abord la ligne avec la souris"));
		Inference at;
		at.kind = InferenceKind::GROUND;
		at.point = m_chain.back() + dir.Normalize() * values[0];
		return Place(ctx, at);
	}
	ToolPreview Preview(const ToolContext &) const override {
		ToolPreview p;
		if (m_chain.empty()) {
			p.hint = String("Cliquez le point de départ");
			return p;
		}
		p.lines.push_back({m_chain.back(), m_cursor});
		p.hint = String("Cliquez le point suivant (Échap : terminer)");
		p.measure = String("Longueur ") + FormatLength(m_cursor.Distance(m_chain.back()));
		return p;
	}
	Option<Vec3> Anchor() const override {
		return m_chain.empty() ? Option<Vec3>(NONE) : Some(m_chain.back());
	}

private:
	ToolResult Place(ToolContext &ctx, const Inference &at) {
		Model &m = ctx.model;
		const VertexId v = ResolveVertex(m, at);
		if (m_ids.empty()) {
			m_ids.push_back(v);
			m_chain.push_back(m.Position(v));
			m_cursor = m_chain.back();
			return Done(String(), at.kind == InferenceKind::ON_EDGE || at.kind == InferenceKind::MIDPOINT);
		}
		const VertexId prev = m_ids.back();
		if (v == prev)
			return Done(String(), false);
		(void)m.AddEdge(prev, v);
		String message = String("Arête tracée");
		// Une ligne entre deux points du contour d'une face la coupe en deux.
		if (Option<FaceId> split = m.SplitFace(prev, v); split.IsSome())
			message = String("Face coupée en deux");
		if (v == m_ids.front() && m_ids.size() >= 3) {
			// Boucle refermée : une face, si les points sont coplanaires.
			if (Option<FaceId> f = m.AddFaceFromVertices(m_ids, {}, FaceOptions{ctx.material}); f.IsSome()) {
				message = m.Imprint(f.Unwrap()).IsSome() ? String("Face créée (empreinte sur la face)")
														 : String("Face créée");
			}
			Reset();
			return Done(message);
		}
		m_ids.push_back(v);
		m_chain.push_back(m.Position(v));
		return Done(message);
	}

	std::vector<Vec3> m_chain;
	std::vector<VertexId> m_ids;
	Vec3 m_cursor;
};

// ── Formes planes : rectangle, cercle, polygone ──────────────────────────────

class ShapeTool final : public Tool {
public:
	explicit ShapeTool(ToolKind kind) : Tool(kind) {}
	void Reset() override { m_started = false; }
	bool IsActive() const override { return m_started; }
	void Hover(ToolContext &, const ToolEvent &e) override {
		if (m_started)
			m_cursor = OnPlane(e, m_origin, m_normal);
		else
			m_cursor = e.at.point;
	}
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override {
		if (!m_started) {
			m_started = true;
			m_origin = e.at.point;
			m_normal = e.at.planeNormal; // face touchée, plan d'esquisse, sinon +Y (sol)
			PlaneAxes(m_normal, m_u, m_v);
			m_cursor = m_origin;
			return Done(String(), false);
		}
		m_cursor = OnPlane(e, m_origin, m_normal);
		return Commit(ctx);
	}
	ToolResult Type(ToolContext &ctx, const std::vector<float> &values) override {
		if (!m_started || values.empty())
			return Failed(String("Tracez d'abord la forme, puis tapez ses dimensions"));
		if (Kind() == ToolKind::RECTANGLE) {
			if (values.size() < 2)
				return Failed(String("Rectangle : tapez « largeur;hauteur »"));
			const Vec3 d = m_cursor - m_origin;
			const float su = d.Dot(m_u) < 0.f ? -1.f : 1.f, sv = d.Dot(m_v) < 0.f ? -1.f : 1.f;
			m_cursor = m_origin + m_u * (values[0] * su) + m_v * (values[1] * sv);
		} else {
			Vec3 d = m_cursor - m_origin;
			if (d.LengthSq() < EPS)
				d = m_u;
			m_cursor = m_origin + d.Normalize() * values[0];
		}
		return Commit(ctx);
	}
	ToolPreview Preview(const ToolContext &ctx) const override {
		ToolPreview p;
		if (!m_started) {
			p.hint = String(Kind() == ToolKind::RECTANGLE ? "Cliquez le premier coin" : "Cliquez le centre");
			return p;
		}
		const std::vector<Vec3> pts = Points(ctx);
		p.lines = LoopLines(pts);
		if (Kind() == ToolKind::RECTANGLE) {
			const Vec3 d = m_cursor - m_origin;
			p.measure = String("Dimensions ") + FormatLength(std::fabs(d.Dot(m_u))) + String(" × ") +
						FormatLength(std::fabs(d.Dot(m_v)));
			p.hint = String("Cliquez le coin opposé (ou tapez largeur;hauteur)");
		} else {
			p.measure = String("Rayon ") + FormatLength(m_cursor.Distance(m_origin));
			p.guides.push_back({m_origin, m_cursor});
			p.hint = String("Cliquez pour fixer le rayon (ou tapez-le)");
		}
		return p;
	}
	Option<Vec3> Anchor() const override { return m_started ? Some(m_origin) : Option<Vec3>(NONE); }

private:
	std::vector<Vec3> Points(const ToolContext &ctx) const {
		std::vector<Vec3> pts;
		if (Kind() == ToolKind::RECTANGLE) {
			const Vec3 d = m_cursor - m_origin;
			const Vec3 a = m_u * d.Dot(m_u), b = m_v * d.Dot(m_v);
			return {m_origin, m_origin + a, m_origin + a + b, m_origin + b};
		}
		const int sides = Kind() == ToolKind::CIRCLE ? ctx.circleSegments : ctx.polygonSides;
		const float r = m_cursor.Distance(m_origin);
		Vec3 x = m_cursor - m_origin;
		x = r > EPS ? x / r : m_u;
		const Vec3 y = m_normal.Cross(x);
		for (int i = 0; i < math::Max(sides, 3); ++i) {
			const float a = 2.f * math::PI * float(i) / float(math::Max(sides, 3));
			pts.push_back(m_origin + x * (math::Cos(a) * r) + y * (math::Sin(a) * r));
		}
		return pts;
	}

	ToolResult Commit(ToolContext &ctx) {
		const std::vector<Vec3> pts = Points(ctx);
		Reset();
		Model &m = ctx.model;
		Option<FaceId> face = NONE;
		if (Kind() == ToolKind::RECTANGLE) {
			face = m.AddFace(pts, {}, FaceOptions{ctx.material});
		} else {
			// Cercle et polygone : une courbe (arêtes liées) + sa face.
			const std::vector<EdgeId> edges = m.AddPolyline(pts, true);
			(void)edges;
			face = m.AddFace(pts, {}, FaceOptions{ctx.material});
		}
		if (face.IsNone())
			return Failed(String::Format("%s : forme trop petite", Name()));
		const bool imprinted = m.Imprint(face.Unwrap()).IsSome();
		return Done(String::Format("%s créé%s", Name(), imprinted ? " (empreinte sur la face)" : ""));
	}

	bool m_started = false;
	Vec3 m_origin, m_cursor, m_normal{0.f, 1.f, 0.f}, m_u{1.f, 0.f, 0.f}, m_v{0.f, 0.f, 1.f};
};

// ── Pousser/Tirer ────────────────────────────────────────────────────────────

class PushPullTool final : public Tool {
public:
	PushPullTool() : Tool(ToolKind::PUSH_PULL) {}
	void Reset() override { m_face = INVALID_ID; }
	bool IsActive() const override { return m_face != INVALID_ID; }
	void Hover(ToolContext &ctx, const ToolEvent &e) override {
		m_hover = e.at.face;
		if (!IsActive() || !ctx.model.IsAlive(m_face))
			return;
		// Hauteur d'un autre point inféré (« pousser jusqu'à cette arête »),
		// sinon le point de la normale le plus proche du rayon.
		const bool other = e.at.IsValid() && e.at.kind != InferenceKind::GROUND && e.at.kind != InferenceKind::ON_PLANE &&
						   e.at.face != m_face;
		const Vec3 p = other ? e.at.point : ClosestPointOnLine(m_start, m_normal, e.ray);
		m_distance = (p - m_start).Dot(m_normal);
	}
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override {
		if (!IsActive()) {
			if (e.at.face == INVALID_ID || !ctx.model.IsAlive(e.at.face))
				return Failed(String("Pousser/Tirer : cliquez une face"));
			m_face = e.at.face;
			m_start = e.at.point;
			m_normal = ctx.model.Faces()[m_face].normal;
			m_distance = 0.f;
			return Done(String(), false);
		}
		m_keep = e.ctrl;
		return Commit(ctx, m_distance);
	}
	ToolResult Type(ToolContext &ctx, const std::vector<float> &values) override {
		if (values.empty())
			return Failed(String("Pousser/Tirer : tapez une distance"));
		if (!IsActive()) {
			// Comme SketchUp : la distance tapée s'applique à la face survolée.
			if (m_hover == INVALID_ID || !ctx.model.IsAlive(m_hover))
				return Failed(String("Pousser/Tirer : survolez ou cliquez une face"));
			m_face = m_hover;
			return Commit(ctx, values[0]);
		}
		// Geste en cours : la cote suit le sens indiqué à la souris (une face
		// posée au sol regarde vers le bas : la tirer vers le haut l'extrude).
		return Commit(ctx, m_distance < 0.f ? -values[0] : values[0]);
	}
	ToolPreview Preview(const ToolContext &ctx) const override {
		ToolPreview p;
		if (!IsActive() || !ctx.model.IsAlive(m_face)) {
			p.hint = String("Cliquez la face à pousser ou tirer");
			if (m_hover != INVALID_ID && ctx.model.IsAlive(m_hover))
				p.polygon = ctx.model.OuterPoints(m_hover);
			return p;
		}
		const std::vector<Vec3> base = ctx.model.OuterPoints(m_face);
		const Vec3 offset = m_normal * m_distance;
		std::vector<Vec3> top;
		for (const Vec3 &b : base) {
			top.push_back(b + offset);
			p.lines.push_back({b, b + offset});
		}
		const auto loop = LoopLines(top);
		p.lines.insert(p.lines.end(), loop.begin(), loop.end());
		p.polygon = top;
		p.measure = String("Distance ") + FormatLength(m_distance);
		p.hint = String("Cliquez pour fixer la distance (Ctrl : garder la face de départ)");
		return p;
	}
	Option<Vec3> Anchor() const override { return IsActive() ? Some(m_start) : Option<Vec3>(NONE); }

private:
	ToolResult Commit(ToolContext &ctx, float distance) {
		const FaceId face = m_face;
		Reset();
		auto r = PushPull(ctx.model, face, distance, PushPullOptions{m_keep});
		m_keep = false;
		if (r.IsError())
			return Failed(r.Error());
		ctx.selection.Prune(ctx.model);
		return Done(String("Pousser/Tirer de ") + FormatLength(distance));
	}

	FaceId m_face = INVALID_ID, m_hover = INVALID_ID;
	Vec3 m_start, m_normal;
	float m_distance = 0.f;
	bool m_keep = false;
};

// ── Décalage ─────────────────────────────────────────────────────────────────

/// Distance signée d'un point au contour (positive à l'intérieur).
float SignedDistanceToOutline(const std::vector<math::FVector2> &poly, const math::FVector2 &p) {
	float best = 1e30f;
	for (size_t i = 0; i < poly.size(); ++i) {
		const math::FVector2 a = poly[i], ab = poly[(i + 1) % poly.size()] - a;
		const float t = ab.LengthSq() > 0.f ? math::Clamp((p - a).Dot(ab) / ab.LengthSq(), 0.f, 1.f) : 0.f;
		best = math::Min(best, (a + ab * t).Distance(p));
	}
	return math::PointInPolygon(poly, p) ? best : -best;
}

class OffsetTool final : public Tool {
public:
	OffsetTool() : Tool(ToolKind::OFFSET) {}
	void Reset() override { m_face = INVALID_ID; }
	bool IsActive() const override { return m_face != INVALID_ID; }
	void Hover(ToolContext &ctx, const ToolEvent &e) override {
		m_hover = e.at.face;
		if (!IsActive() || !ctx.model.IsAlive(m_face))
			return;
		const PlaneInfo plane = ctx.model.FacePlane(m_face);
		const Vec3 p = OnPlane(e, plane.origin, plane.normal);
		std::vector<math::FVector2> poly;
		for (const Vec3 &q : ctx.model.OuterPoints(m_face))
			poly.push_back(plane.Project(q));
		m_distance = SignedDistanceToOutline(poly, plane.Project(p)) - m_startDistance;
	}
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override {
		if (!IsActive()) {
			if (e.at.face == INVALID_ID || !ctx.model.IsAlive(e.at.face))
				return Failed(String("Décalage : cliquez une face"));
			m_face = e.at.face;
			const PlaneInfo plane = ctx.model.FacePlane(m_face);
			std::vector<math::FVector2> poly;
			for (const Vec3 &q : ctx.model.OuterPoints(m_face))
				poly.push_back(plane.Project(q));
			m_startDistance = SignedDistanceToOutline(poly, plane.Project(e.at.point));
			m_distance = 0.f;
			return Done(String(), false);
		}
		return Commit(ctx, m_distance);
	}
	ToolResult Type(ToolContext &ctx, const std::vector<float> &values) override {
		if (values.empty())
			return Failed(String("Décalage : tapez une distance"));
		if (!IsActive()) {
			if (m_hover == INVALID_ID || !ctx.model.IsAlive(m_hover))
				return Failed(String("Décalage : survolez ou cliquez une face"));
			m_face = m_hover;
			return Commit(ctx, values[0]);
		}
		// Geste en cours : la cote suit le sens indiqué à la souris (une face
		// posée au sol regarde vers le bas : la tirer vers le haut l'extrude).
		return Commit(ctx, m_distance < 0.f ? -values[0] : values[0]);
	}
	ToolPreview Preview(const ToolContext &ctx) const override {
		ToolPreview p;
		if (!IsActive() || !ctx.model.IsAlive(m_face)) {
			p.hint = String("Cliquez la face à décaler");
			if (m_hover != INVALID_ID && ctx.model.IsAlive(m_hover))
				p.polygon = ctx.model.OuterPoints(m_hover);
			return p;
		}
		if (auto outline = OffsetOutline(ctx.model, m_face, m_distance); outline.IsOk())
			p.lines = LoopLines(outline.Value());
		p.measure = String("Décalage ") + FormatLength(m_distance);
		p.hint = String("Cliquez pour fixer le décalage (vers l'intérieur ou l'extérieur)");
		return p;
	}

private:
	ToolResult Commit(ToolContext &ctx, float distance) {
		const FaceId face = m_face;
		Reset();
		auto r = Offset(ctx.model, face, distance);
		if (r.IsError())
			return Failed(r.Error());
		return Done(String("Décalage de ") + FormatLength(distance));
	}

	FaceId m_face = INVALID_ID, m_hover = INVALID_ID;
	float m_startDistance = 0.f, m_distance = 0.f;
};

// ── Suivez-moi ───────────────────────────────────────────────────────────────

class FollowMeTool final : public Tool {
public:
	FollowMeTool() : Tool(ToolKind::FOLLOW_ME) {}
	void Reset() override {}
	bool IsActive() const override { return false; }
	void Hover(ToolContext &, const ToolEvent &e) override { m_hover = e.at.face; }
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override {
		if (ctx.selection.edges.empty())
			return Failed(String("Suivez-moi : sélectionnez d'abord le chemin (arêtes), puis cliquez le profil"));
		if (e.at.face == INVALID_ID || !ctx.model.IsAlive(e.at.face))
			return Failed(String("Suivez-moi : cliquez la face du profil"));
		auto path = ChainEdges(ctx.model, ctx.selection.edges, ctx.model.FaceCentroid(e.at.face));
		if (path.IsError())
			return Failed(path.Error());
		auto r = FollowMe(ctx.model, e.at.face, path.Value());
		if (r.IsError())
			return Failed(r.Error());
		ctx.selection.Clear();
		return Done(String("Profil balayé le long du chemin"));
	}
	ToolPreview Preview(const ToolContext &ctx) const override {
		ToolPreview p;
		p.hint = ctx.selection.edges.empty() ? String("Sélectionnez le chemin (outil Sélection), puis revenez ici")
											 : String("Cliquez la face du profil à balayer");
		if (m_hover != INVALID_ID && ctx.model.IsAlive(m_hover))
			p.polygon = ctx.model.OuterPoints(m_hover);
		for (EdgeId e : ctx.selection.edges)
			if (ctx.model.IsEdgeAlive(e))
				p.guides.push_back({ctx.model.Position(ctx.model.Edges()[e].a), ctx.model.Position(ctx.model.Edges()[e].b)});
		return p;
	}

private:
	FaceId m_hover = INVALID_ID;
};

// ── Déplacer / Rotation / Échelle ────────────────────────────────────────────

class MoveTool final : public Tool {
public:
	MoveTool() : Tool(ToolKind::MOVE) {}
	void Reset() override {
		m_active = false;
		m_targets.clear();
		m_faces.clear();
	}
	bool IsActive() const override { return m_active; }
	void Hover(ToolContext &, const ToolEvent &e) override { m_cursor = e.at.point; }
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override {
		if (!m_active) {
			m_targets = TargetVertices(ctx, e.at, &m_faces);
			if (m_targets.empty())
				return Failed(String("Déplacer : sélectionnez ou survolez ce qui doit bouger"));
			m_active = true;
			m_base = e.at.point;
			m_cursor = m_base;
			return Done(String(), false);
		}
		m_copy = e.ctrl;
		return Commit(ctx, e.at.point - m_base);
	}
	ToolResult Type(ToolContext &ctx, const std::vector<float> &values) override {
		if (!m_active || values.empty())
			return Failed(String("Déplacer : orientez le déplacement puis tapez la distance"));
		Vec3 d = m_cursor - m_base;
		if (d.LengthSq() < EPS)
			return Failed(String("Déplacer : orientez d'abord le déplacement"));
		return Commit(ctx, d.Normalize() * values[0]);
	}
	ToolPreview Preview(const ToolContext &ctx) const override {
		ToolPreview p;
		if (!m_active) {
			p.hint = String("Cliquez le point de départ du déplacement");
			return p;
		}
		p.lines = MovedEdges(ctx.model, m_targets, math::FMatrix4::Translate(m_cursor - m_base));
		p.guides.push_back({m_base, m_cursor});
		p.measure = String("Distance ") + FormatLength(m_cursor.Distance(m_base));
		p.hint = String("Cliquez le point d'arrivée (Ctrl : copier)");
		return p;
	}
	Option<Vec3> Anchor() const override { return m_active ? Some(m_base) : Option<Vec3>(NONE); }

private:
	ToolResult Commit(ToolContext &ctx, const Vec3 &delta) {
		const std::vector<VertexId> targets = m_targets;
		const std::vector<FaceId> faces = m_faces;
		const bool copy = m_copy;
		Reset();
		m_copy = false;
		if (copy) {
			if (faces.empty())
				return Failed(String("Copier : sélectionnez des faces"));
			const Model part = ExtractFaces(ctx.model, faces);
			const std::vector<FaceId> added = ctx.model.Append(part, math::FMatrix4::Translate(delta));
			ctx.selection.Clear();
			ctx.selection.faces = added;
			return Done(String("Copie déplacée de ") + FormatLength(delta.Length()));
		}
		Move(ctx.model, targets, delta);
		ctx.selection.Prune(ctx.model);
		return Done(String("Déplacement de ") + FormatLength(delta.Length()));
	}

	bool m_active = false, m_copy = false;
	std::vector<VertexId> m_targets;
	std::vector<FaceId> m_faces;
	Vec3 m_base, m_cursor;
};

class RotateTool final : public Tool {
public:
	RotateTool() : Tool(ToolKind::ROTATE) {}
	void Reset() override {
		m_step = 0;
		m_targets.clear();
	}
	bool IsActive() const override { return m_step > 0; }
	void Hover(ToolContext &, const ToolEvent &e) override {
		if (m_step == 0) {
			m_cursor = e.at.point;
			return;
		}
		m_cursor = OnPlane(e, m_center, m_axis);
		if (m_step == 2)
			m_angle = Snapped(AngleTo(m_cursor));
	}
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override {
		if (m_step == 0) {
			m_targets = TargetVertices(ctx, e.at);
			if (m_targets.empty())
				return Failed(String("Rotation : sélectionnez ou survolez ce qui doit tourner"));
			m_center = e.at.point;
			m_axis = e.at.face != INVALID_ID ? e.at.planeNormal : Vec3{0.f, 1.f, 0.f};
			m_step = 1;
			return Done(String(), false);
		}
		const Vec3 p = OnPlane(e, m_center, m_axis);
		if (m_step == 1) {
			const Vec3 d = p - m_center;
			if (d.LengthSq() < EPS)
				return Failed(String("Rotation : cliquez une direction de référence"));
			m_reference = d.Normalize();
			m_step = 2;
			m_angle = 0.f;
			return Done(String(), false);
		}
		return Commit(ctx, Snapped(AngleTo(p)));
	}
	ToolResult Type(ToolContext &ctx, const std::vector<float> &values) override {
		if (m_step != 2 || values.empty())
			return Failed(String("Rotation : placez le centre et la référence, puis tapez l'angle (°)"));
		return Commit(ctx, values[0] * math::PI / 180.f);
	}
	ToolPreview Preview(const ToolContext &ctx) const override {
		ToolPreview p;
		if (m_step == 0) {
			p.hint = String("Cliquez le centre de rotation (le plan suit la face survolée)");
			return p;
		}
		p.guides.push_back({m_center, m_step == 2 ? m_center + m_reference * m_cursor.Distance(m_center) : m_cursor});
		if (m_step == 1) {
			p.hint = String("Cliquez la direction de référence");
			return p;
		}
		p.guides.push_back({m_center, m_cursor});
		p.lines = MovedEdges(ctx.model, m_targets, Matrix(m_angle));
		p.measure = String("Angle ") + FormatAngle(m_angle);
		p.hint = String("Cliquez pour fixer l'angle (ou tapez-le en degrés)");
		return p;
	}
	Option<Vec3> Anchor() const override { return m_step > 0 ? Some(m_center) : Option<Vec3>(NONE); }

private:
	float AngleTo(const Vec3 &p) const {
		const Vec3 d = p - m_center;
		if (d.LengthSq() < EPS)
			return 0.f;
		const Vec3 n = d.Normalize();
		return math::Atan2(m_reference.Cross(n).Dot(m_axis), m_reference.Dot(n));
	}
	/// Accroche aux multiples de 15° (comme le rapporteur de SketchUp).
	static float Snapped(float angle) {
		const float step = math::PI / 12.f;
		const float snapped = std::round(angle / step) * step;
		return std::fabs(snapped - angle) < 2.f * math::PI / 180.f ? snapped : angle;
	}
	math::FMatrix4 Matrix(float angle) const {
		return math::FMatrix4::Translate(m_center) * math::FMatrix4::Rotate(m_axis, angle) *
			   math::FMatrix4::Translate(m_center * -1.f);
	}
	ToolResult Commit(ToolContext &ctx, float angle) {
		const std::vector<VertexId> targets = m_targets;
		const Vec3 center = m_center, axis = m_axis;
		Reset();
		Rotate(ctx.model, targets, center, axis, angle);
		ctx.selection.Prune(ctx.model);
		return Done(String("Rotation de ") + FormatAngle(angle));
	}

	int m_step = 0;
	std::vector<VertexId> m_targets;
	Vec3 m_center, m_axis{0.f, 1.f, 0.f}, m_reference{1.f, 0.f, 0.f}, m_cursor;
	float m_angle = 0.f;
};

class ScaleTool final : public Tool {
public:
	ScaleTool() : Tool(ToolKind::SCALE) {}
	void Reset() override {
		m_active = false;
		m_targets.clear();
	}
	bool IsActive() const override { return m_active; }
	void Hover(ToolContext &, const ToolEvent &e) override {
		if (!m_active)
			return;
		const Vec3 p = ClosestPointOnLine(m_center, m_start - m_center, e.ray);
		m_factor = Factor(p);
	}
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override {
		if (!m_active) {
			m_targets = TargetVertices(ctx, e.at);
			if (m_targets.empty())
				return Failed(String("Échelle : sélectionnez ou survolez ce qui doit changer de taille"));
			math::FAABB box;
			for (VertexId v : m_targets) {
				box.min = Vec3::Min(box.min, ctx.model.Position(v));
				box.max = Vec3::Max(box.max, ctx.model.Position(v));
			}
			m_center = (box.min + box.max) * 0.5f;
			m_start = e.at.point;
			if (m_start.DistanceSq(m_center) < EPS)
				m_start = box.max;
			m_factor = 1.f;
			m_active = true;
			return Done(String(), false);
		}
		return Commit(ctx, m_factor);
	}
	ToolResult Type(ToolContext &ctx, const std::vector<float> &values) override {
		if (!m_active || values.empty() || std::fabs(values[0]) < EPS)
			return Failed(String("Échelle : tapez un facteur non nul"));
		return Commit(ctx, values[0]);
	}
	ToolPreview Preview(const ToolContext &ctx) const override {
		ToolPreview p;
		if (!m_active) {
			p.hint = String("Cliquez un point de la forme à mettre à l'échelle");
			return p;
		}
		const math::FMatrix4 m = math::FMatrix4::Translate(m_center) * math::FMatrix4::Scale(m_factor) *
								 math::FMatrix4::Translate(m_center * -1.f);
		p.lines = MovedEdges(ctx.model, m_targets, m);
		p.guides.push_back({m_center, m_start});
		p.measure = String::Format("Facteur %.2f", double(m_factor)).Replace('.', ',');
		p.hint = String("Cliquez pour fixer le facteur (ou tapez-le)");
		return p;
	}

private:
	float Factor(const Vec3 &p) const {
		const Vec3 ref = m_start - m_center;
		const float len = ref.Length();
		return len < EPS ? 1.f : (p - m_center).Dot(ref) / (len * len);
	}
	ToolResult Commit(ToolContext &ctx, float factor) {
		const std::vector<VertexId> targets = m_targets;
		const Vec3 center = m_center;
		Reset();
		if (std::fabs(factor) < EPS)
			return Failed(String("Échelle : facteur nul"));
		Scale(ctx.model, targets, center, {factor, factor, factor});
		ctx.selection.Prune(ctx.model);
		return Done(String::Format("Échelle × %.2f", double(factor)).Replace('.', ','));
	}

	bool m_active = false;
	std::vector<VertexId> m_targets;
	Vec3 m_center, m_start;
	float m_factor = 1.f;
};

// ── Gomme, peinture, mètre ───────────────────────────────────────────────────

class EraserTool final : public Tool {
public:
	EraserTool() : Tool(ToolKind::ERASER) {}
	void Reset() override {}
	bool IsActive() const override { return false; }
	void Hover(ToolContext &, const ToolEvent &e) override { m_hover = e.at; }
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override {
		if (e.at.edge != INVALID_ID && (e.at.kind == InferenceKind::ON_EDGE || e.at.kind == InferenceKind::MIDPOINT)) {
			Erase(ctx.model, {e.at.edge});
			ctx.selection.Prune(ctx.model);
			return Done(String("Arête effacée"));
		}
		if (e.at.face != INVALID_ID && ctx.model.IsAlive(e.at.face)) {
			Erase(ctx.model, {}, {e.at.face});
			ctx.selection.Prune(ctx.model);
			return Done(String("Face effacée"));
		}
		return Done(String(), false);
	}
	ToolPreview Preview(const ToolContext &ctx) const override {
		ToolPreview p;
		p.hint = String("Cliquez une arête (et ses faces) ou une face à effacer");
		if (m_hover.edge != INVALID_ID && ctx.model.IsEdgeAlive(m_hover.edge) &&
			(m_hover.kind == InferenceKind::ON_EDGE || m_hover.kind == InferenceKind::MIDPOINT))
			p.lines.push_back({ctx.model.Position(ctx.model.Edges()[m_hover.edge].a),
							   ctx.model.Position(ctx.model.Edges()[m_hover.edge].b)});
		else if (m_hover.face != INVALID_ID && ctx.model.IsAlive(m_hover.face))
			p.polygon = ctx.model.OuterPoints(m_hover.face);
		return p;
	}

private:
	Inference m_hover;
};

class PaintTool final : public Tool {
public:
	PaintTool() : Tool(ToolKind::PAINT) {}
	void Reset() override {}
	bool IsActive() const override { return false; }
	void Hover(ToolContext &, const ToolEvent &e) override { m_hover = e.at.face; }
	ToolResult Click(ToolContext &ctx, const ToolEvent &e) override {
		if (e.at.face == INVALID_ID || !ctx.model.IsAlive(e.at.face))
			return Failed(String("Peinture : cliquez une face"));
		// Ctrl : toutes les faces sélectionnées.
		if (e.ctrl && !ctx.selection.faces.empty()) {
			for (FaceId f : ctx.selection.faces)
				Paint(ctx.model, f, ctx.material);
			return Done(String::Format("%zu face(s) peinte(s)", ctx.selection.faces.size()));
		}
		Paint(ctx.model, e.at.face, ctx.material);
		return Done(String("Face peinte"));
	}
	ToolPreview Preview(const ToolContext &ctx) const override {
		ToolPreview p;
		p.hint = String("Cliquez une face pour lui appliquer le matériau courant");
		if (m_hover != INVALID_ID && ctx.model.IsAlive(m_hover))
			p.polygon = ctx.model.OuterPoints(m_hover);
		return p;
	}

private:
	FaceId m_hover = INVALID_ID;
};

class TapeTool final : public Tool {
public:
	TapeTool() : Tool(ToolKind::TAPE) {}
	void Reset() override { m_active = false; }
	bool IsActive() const override { return m_active; }
	void Hover(ToolContext &, const ToolEvent &e) override { m_cursor = e.at.point; }
	ToolResult Click(ToolContext &, const ToolEvent &e) override {
		if (!m_active) {
			m_active = true;
			m_start = e.at.point;
			m_cursor = m_start;
			return Done(String(), false);
		}
		m_active = false;
		m_last = e.at.point.Distance(m_start);
		return Done(String("Distance mesurée : ") + FormatLength(m_last), false);
	}
	ToolPreview Preview(const ToolContext &) const override {
		ToolPreview p;
		if (!m_active) {
			p.hint = String("Cliquez le premier point à mesurer");
			if (m_last > 0.f)
				p.measure = String("Dernière mesure ") + FormatLength(m_last);
			return p;
		}
		p.guides.push_back({m_start, m_cursor});
		p.measure = String("Distance ") + FormatLength(m_cursor.Distance(m_start));
		p.hint = String("Cliquez le second point");
		return p;
	}
	Option<Vec3> Anchor() const override { return m_active ? Some(m_start) : Option<Vec3>(NONE); }

private:
	bool m_active = false;
	Vec3 m_start, m_cursor;
	float m_last = 0.f;
};

} // namespace

std::unique_ptr<Tool> MakeTool(ToolKind kind) {
	switch (kind) {
	case ToolKind::SELECT: return std::make_unique<SelectTool>();
	case ToolKind::LINE: return std::make_unique<LineTool>();
	case ToolKind::RECTANGLE:
	case ToolKind::CIRCLE:
	case ToolKind::POLYGON: return std::make_unique<ShapeTool>(kind);
	case ToolKind::PUSH_PULL: return std::make_unique<PushPullTool>();
	case ToolKind::OFFSET: return std::make_unique<OffsetTool>();
	case ToolKind::FOLLOW_ME: return std::make_unique<FollowMeTool>();
	case ToolKind::MOVE: return std::make_unique<MoveTool>();
	case ToolKind::ROTATE: return std::make_unique<RotateTool>();
	case ToolKind::SCALE: return std::make_unique<ScaleTool>();
	case ToolKind::ERASER: return std::make_unique<EraserTool>();
	case ToolKind::PAINT: return std::make_unique<PaintTool>();
	case ToolKind::TAPE: return std::make_unique<TapeTool>();
	}
	return std::make_unique<SelectTool>();
}

} // namespace modeling
