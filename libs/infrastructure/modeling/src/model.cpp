// Définitions de modeling/model.hpp
#include <modeling/model.hpp>

#include <algorithm>
#include <cmath>
#include <array>
#include <map>
#include <unordered_map>

namespace modeling {

// ── Outils géométriques ──────────────────────────────────────────────────────

PlaneInfo PlaneInfo::Make(const Vec3 &origin, const Vec3 &normal) noexcept {
	PlaneInfo p;
	p.origin = origin;
	p.normal = normal.LengthSq() > 1e-20f ? normal.Normalize() : Vec3{0.f, 1.f, 0.f};
	// u horizontal autant que possible : les textures des murs restent droites.
	if (std::fabs(p.normal.y) > 0.999f) {
		const Vec3 x{1.f, 0.f, 0.f};
		p.u = (x - p.normal * x.Dot(p.normal)).Normalize();
	} else {
		p.u = Vec3{0.f, 1.f, 0.f}.Cross(p.normal).Normalize();
	}
	p.v = p.normal.Cross(p.u);
	return p;
}

Vec3 NewellNormal(const std::vector<Vec3> &points) noexcept {
	Vec3 n{0.f, 0.f, 0.f};
	const size_t count = points.size();
	for (size_t i = 0; i < count; ++i) {
		const Vec3 &a = points[i];
		const Vec3 &b = points[(i + 1) % count];
		n.x += (a.y - b.y) * (a.z + b.z);
		n.y += (a.z - b.z) * (a.x + b.x);
		n.z += (a.x - b.x) * (a.y + b.y);
	}
	return n.LengthSq() > 1e-20f ? n.Normalize() : Vec3{0.f, 0.f, 0.f};
}

namespace {

bool OnBorder(const std::vector<math::FVector2> &poly, const math::FVector2 &p, float tolerance) {
	return math::OnPolygonBorder(poly, p, tolerance);
}

bool StrictlyInside(const std::vector<math::FVector2> &poly, const math::FVector2 &p) {
	return math::PointInPolygon(poly, p);
}

/// Point dans un polygone 2D ; les points du bord comptent comme intérieurs.
bool PointInPolygon(const std::vector<math::FVector2> &poly, const math::FVector2 &p, float tolerance) {
	return OnBorder(poly, p, tolerance) || StrictlyInside(poly, p);
}

/// Retire les sommets consécutifs identiques (et la fermeture répétée).
Loop CleanLoop(const Loop &loop) {
	Loop out;
	for (VertexId v : loop)
		if (out.empty() || out.back() != v)
			out.push_back(v);
	while (out.size() > 1 && out.front() == out.back())
		out.pop_back();
	return out;
}

bool SameCycle(const Loop &a, const Loop &b) {
	if (a.size() != b.size() || a.empty())
		return false;
	const auto start = std::find(b.begin(), b.end(), a[0]);
	if (start == b.end())
		return false;
	const size_t offset = size_t(start - b.begin());
	const size_t n = a.size();
	bool forward = true, backward = true;
	for (size_t i = 0; i < n; ++i) {
		forward = forward && a[i] == b[(offset + i) % n];
		backward = backward && a[i] == b[(offset + n - i) % n];
	}
	return forward || backward;
}

} // namespace

// ── Matériaux et calques ─────────────────────────────────────────────────────

uint32_t Model::AddMaterial(const String &name, Color color, float textureScale) {
	if (Option<uint32_t> existing = FindMaterial(name); existing.IsSome()) {
		m_materials[existing.Unwrap()].color = color;
		m_materials[existing.Unwrap()].textureScale = textureScale;
		Touch();
		return existing.Unwrap();
	}
	m_materials.push_back(Material{name, color, textureScale});
	Touch();
	return uint32_t(m_materials.size() - 1);
}

Option<uint32_t> Model::FindMaterial(const String &name) const {
	for (size_t i = 0; i < m_materials.size(); ++i)
		if (m_materials[i].name == name)
			return Some(uint32_t(i));
	return NONE;
}

void Model::EnsureDefaultLayer() {
	if (m_layers.empty())
		m_layers.push_back(Layer{String("Layer0"), true});
}

uint32_t Model::AddLayer(const String &name, bool visible) {
	EnsureDefaultLayer();
	for (size_t i = 0; i < m_layers.size(); ++i)
		if (m_layers[i].name == name)
			return uint32_t(i);
	m_layers.push_back(Layer{name, visible});
	Touch();
	return uint32_t(m_layers.size() - 1);
}

// ── Géométrie élémentaire ────────────────────────────────────────────────────

namespace {

constexpr float GRID_CELL = 0.01f;

int64_t Cell(float v) { return int64_t(std::floor(v / GRID_CELL)); }

uint64_t CellKey(int64_t x, int64_t y, int64_t z) {
	// 21 bits signés par axe : ± 10 km à 1 cm près.
	auto pack = [](int64_t c) { return uint64_t(c + (int64_t(1) << 20)) & 0x1FFFFF; };
	return (pack(x) << 42) | (pack(y) << 21) | pack(z);
}

} // namespace

void Model::IndexVertex(VertexId v) {
	const Vec3 &p = m_vertices[v].position;
	m_vertexGrid[CellKey(Cell(p.x), Cell(p.y), Cell(p.z))].push_back(v);
}

void Model::RebuildVertexIndex() const {
	m_vertexGrid.clear();
	for (size_t i = 0; i < m_vertices.size(); ++i)
		if (m_vertices[i].alive) {
			const Vec3 &p = m_vertices[i].position;
			m_vertexGrid[CellKey(Cell(p.x), Cell(p.y), Cell(p.z))].push_back(VertexId(i));
		}
	m_vertexIndexDirty = false;
}

void Model::RebuildEdgeIndex() const {
	m_edgeIndex.clear();
	for (size_t i = 0; i < m_edges.size(); ++i)
		if (m_edges[i].alive)
			m_edgeIndex[EdgeKey(m_edges[i].a, m_edges[i].b)] = EdgeId(i);
	m_edgeIndexDirty = false;
}

void Model::RebuildFaceCache() const {
	m_faceCache.assign(m_edges.size(), {});
	for (size_t f = 0; f < m_faces.size(); ++f) {
		if (!m_faces[f].alive)
			continue;
		auto add = [&](const Loop &loop) {
			for (size_t i = 0; i < loop.size(); ++i) {
				const EdgeId e = FindEdge(loop[i], loop[(i + 1) % loop.size()]);
				if (e != INVALID_ID && (m_faceCache[e].empty() || m_faceCache[e].back() != FaceId(f)))
					m_faceCache[e].push_back(FaceId(f));
			}
		};
		add(m_faces[f].outer);
		for (const Loop &h : m_faces[f].holes)
			add(h);
	}
	m_faceCacheVersion = m_faceVersion;
}

VertexId Model::FindVertex(const Vec3 &p, float tolerance) const {
	const float tol2 = tolerance * tolerance;
	if (tolerance > GRID_CELL) {
		for (size_t i = 0; i < m_vertices.size(); ++i)
			if (m_vertices[i].alive && m_vertices[i].position.DistanceSq(p) <= tol2)
				return VertexId(i);
		return INVALID_ID;
	}
	if (m_vertexIndexDirty)
		RebuildVertexIndex();
	VertexId best = INVALID_ID;
	float bestDist = tol2;
	const int64_t cx = Cell(p.x), cy = Cell(p.y), cz = Cell(p.z);
	for (int64_t dx = -1; dx <= 1; ++dx)
		for (int64_t dy = -1; dy <= 1; ++dy)
			for (int64_t dz = -1; dz <= 1; ++dz) {
				auto it = m_vertexGrid.find(CellKey(cx + dx, cy + dy, cz + dz));
				if (it == m_vertexGrid.end())
					continue;
				for (VertexId v : it->second) {
					if (!m_vertices[v].alive)
						continue;
					// Le plus petit indice dans la tolérance : même résultat
					// que la recherche linéaire (déterminisme).
					if (m_vertices[v].position.DistanceSq(p) <= bestDist && (best == INVALID_ID || v < best))
						best = v;
				}
			}
	return best;
}

VertexId Model::AddVertex(const Vec3 &p) {
	if (VertexId existing = FindVertex(p); existing != INVALID_ID)
		return existing;
	m_vertices.push_back(Vertex{p, true});
	const VertexId v = VertexId(m_vertices.size() - 1);
	if (!m_vertexIndexDirty)
		IndexVertex(v);
	Touch();
	return v;
}

EdgeId Model::FindEdge(VertexId a, VertexId b) const {
	if (m_edgeIndexDirty)
		RebuildEdgeIndex();
	auto it = m_edgeIndex.find(EdgeKey(a, b));
	if (it == m_edgeIndex.end())
		return INVALID_ID;
	const Edge &e = m_edges[it->second];
	if (e.alive && ((e.a == a && e.b == b) || (e.a == b && e.b == a)))
		return it->second;
	return INVALID_ID;
}

EdgeId Model::AddEdge(VertexId a, VertexId b) {
	if (a == b || a == INVALID_ID || b == INVALID_ID)
		return INVALID_ID;
	if (EdgeId existing = FindEdge(a, b); existing != INVALID_ID)
		return existing;
	EnsureDefaultLayer();
	Edge e;
	e.a = a;
	e.b = b;
	m_edges.push_back(e);
	const EdgeId id = EdgeId(m_edges.size() - 1);
	if (!m_edgeIndexDirty)
		m_edgeIndex[EdgeKey(a, b)] = id;
	Touch();
	return id;
}

void Model::SetEdgeSmoothing(EdgeId e, bool soft, bool smooth) {
	if (!IsEdgeAlive(e))
		return;
	m_edges[e].soft = soft;
	m_edges[e].smooth = smooth;
	Touch();
}

bool Model::FinishFace(Face &face) const {
	std::vector<Vec3> pts;
	for (VertexId v : face.outer)
		pts.push_back(m_vertices[v].position);
	const Vec3 n = NewellNormal(pts);
	if (n.LengthSq() < 0.5f)
		return false;
	face.normal = n;
	for (Loop &hole : face.holes) {
		std::vector<Vec3> hp;
		for (VertexId v : hole)
			hp.push_back(m_vertices[v].position);
		if (NewellNormal(hp).Dot(n) > 0.f)
			std::reverse(hole.begin(), hole.end());
	}
	return true;
}

Option<FaceId> Model::AddFaceFromVertices(const Loop &outerIn, const std::vector<Loop> &holesIn,
										  const FaceOptions &options) {
	Face face;
	face.outer = CleanLoop(outerIn);
	if (face.outer.size() < 3)
		return NONE;
	for (const Loop &h : holesIn) {
		Loop hole = CleanLoop(h);
		if (hole.size() >= 3)
			face.holes.push_back(std::move(hole));
	}
	if (!FinishFace(face))
		return NONE;

	// Planéité : tolérance relative à la taille du contour.
	std::vector<Vec3> pts;
	for (VertexId v : face.outer)
		pts.push_back(m_vertices[v].position);
	Vec3 centroid{0.f, 0.f, 0.f};
	float extent = 0.f;
	for (const Vec3 &p : pts)
		centroid += p;
	centroid = centroid / float(pts.size());
	for (const Vec3 &p : pts)
		extent = math::Max(extent, p.Distance(centroid));
	const float tolerance = math::Max(1e-4f, extent * 1e-4f);
	bool planar = true;
	for (const Vec3 &p : pts)
		planar = planar && std::fabs((p - centroid).Dot(face.normal)) <= tolerance;
	for (const Loop &hole : face.holes)
		for (VertexId v : hole)
			planar = planar && std::fabs((m_vertices[v].position - centroid).Dot(face.normal)) <= tolerance;
	if (!planar) {
		if (!options.autoTriangulate || !face.holes.empty())
			return NONE;
		// Éventail de triangles plans ; les arêtes internes sont adoucies.
		Option<FaceId> first = NONE;
		for (size_t i = 1; i + 1 < face.outer.size(); ++i) {
			FaceOptions tri = options;
			tri.autoTriangulate = false;
			Option<FaceId> id = AddFaceFromVertices({face.outer[0], face.outer[i], face.outer[i + 1]}, {}, tri);
			if (first.IsNone())
				first = id;
		}
		for (size_t i = 2; i + 1 < face.outer.size(); ++i)
			if (EdgeId e = FindEdge(face.outer[0], face.outer[i]); e != INVALID_ID)
				m_edges[e].soft = m_edges[e].smooth = true;
		return first;
	}

	for (size_t i = 0; i < m_faces.size(); ++i)
		if (m_faces[i].alive && SameCycle(m_faces[i].outer, face.outer))
			return Some(FaceId(i));

	face.material = options.material;
	face.layer = options.layer;
	auto addLoopEdges = [&](const Loop &loop, bool outer) {
		for (size_t i = 0; i < loop.size(); ++i) {
			const EdgeId e = AddEdge(loop[i], loop[(i + 1) % loop.size()]);
			if (e == INVALID_ID)
				continue;
			if (outer && options.softEdges)
				m_edges[e].soft = true;
			if (outer && options.smoothEdges)
				m_edges[e].smooth = true;
			m_edges[e].layer = options.layer;
		}
	};
	addLoopEdges(face.outer, true);
	for (const Loop &hole : face.holes)
		addLoopEdges(hole, false);
	m_faces.push_back(std::move(face));
	FacesChanged();
	Touch();
	return Some(FaceId(m_faces.size() - 1));
}

Option<FaceId> Model::AddFace(const std::vector<Vec3> &outer, const std::vector<std::vector<Vec3>> &holes,
							  const FaceOptions &options) {
	Loop loop;
	for (const Vec3 &p : outer)
		loop.push_back(AddVertex(p));
	std::vector<Loop> holeLoops;
	for (const auto &h : holes) {
		Loop hl;
		for (const Vec3 &p : h)
			hl.push_back(AddVertex(p));
		holeLoops.push_back(std::move(hl));
	}
	return AddFaceFromVertices(loop, holeLoops, options);
}

std::vector<EdgeId> Model::AddPolyline(const std::vector<Vec3> &points, bool closed, uint32_t layer) {
	std::vector<EdgeId> out;
	const size_t n = points.size();
	const uint32_t curve = n > 2 ? NewCurveId() : INVALID_ID;
	for (size_t i = 0; i + (closed ? 0 : 1) < n; ++i) {
		const EdgeId e = AddEdge(points[i], points[(i + 1) % n]);
		if (e == INVALID_ID)
			continue;
		m_edges[e].layer = layer;
		m_edges[e].curve = curve;
		out.push_back(e);
	}
	return out;
}

namespace {

std::vector<Vec3> RegularPoints(const Vec3 &center, const Vec3 &normal, float radius, int sides) {
	const PlaneInfo plane = PlaneInfo::Make(center, normal);
	std::vector<Vec3> pts;
	sides = math::Max(sides, 3);
	for (int i = 0; i < sides; ++i) {
		const float a = 2.f * math::PI * float(i) / float(sides);
		pts.push_back(center + plane.u * (math::Cos(a) * radius) + plane.v * (math::Sin(a) * radius));
	}
	return pts;
}

} // namespace

Option<FaceId> Model::AddCircle(const Vec3 &center, const Vec3 &normal, float radius, const CurveOptions &options) {
	const std::vector<Vec3> pts = RegularPoints(center, normal, radius, options.segments);
	const std::vector<EdgeId> edges = AddPolyline(pts, true, options.layer);
	if (!options.filled)
		return NONE;
	return AddFace(pts, {}, FaceOptions{options.material, options.layer, false, false, false});
}

Option<FaceId> Model::AddPolygon(const Vec3 &center, const Vec3 &normal, float radius, int sides,
								 const CurveOptions &options) {
	CurveOptions o = options;
	o.segments = sides;
	return AddCircle(center, normal, radius, o);
}

std::vector<EdgeId> Model::AddArc(const Vec3 &center, const Vec3 &normal, const Vec3 &xAxis, float radius,
								  float startAngle, float endAngle, const CurveOptions &options) {
	const Vec3 n = normal.Normalize();
	const Vec3 u = (xAxis - n * xAxis.Dot(n)).Normalize();
	const Vec3 v = n.Cross(u);
	const int segments = math::Max(options.segments, 1);
	std::vector<Vec3> pts;
	for (int i = 0; i <= segments; ++i) {
		const float a = startAngle + (endAngle - startAngle) * float(i) / float(segments);
		pts.push_back(center + u * (math::Cos(a) * radius) + v * (math::Sin(a) * radius));
	}
	return AddPolyline(pts, false, options.layer);
}

Option<FaceId> Model::AddRectangle(const Vec3 &corner, const Vec3 &side1, const Vec3 &side2,
								   const FaceOptions &options) {
	return AddFace({corner, corner + side1, corner + side1 + side2, corner + side2}, {}, options);
}

// ── Accès ────────────────────────────────────────────────────────────────────

size_t Model::VertexCount() const noexcept {
	return size_t(std::count_if(m_vertices.begin(), m_vertices.end(), [](const Vertex &v) { return v.alive; }));
}

size_t Model::EdgeCount() const noexcept {
	return size_t(std::count_if(m_edges.begin(), m_edges.end(), [](const Edge &e) { return e.alive; }));
}

size_t Model::FaceCount() const noexcept {
	return size_t(std::count_if(m_faces.begin(), m_faces.end(), [](const Face &f) { return f.alive; }));
}

std::vector<FaceId> Model::FacesOfEdge(EdgeId e) const {
	if (!IsEdgeAlive(e))
		return {};
	if (m_faceCacheVersion != m_faceVersion || e >= m_faceCache.size())
		RebuildFaceCache();
	std::vector<FaceId> out;
	for (FaceId f : m_faceCache[e])
		if (m_faces[f].alive)
			out.push_back(f);
	return out;
}

std::vector<EdgeId> Model::EdgesOfFace(FaceId f) const {
	std::vector<EdgeId> out;
	if (!IsAlive(f))
		return out;
	auto add = [&](const Loop &loop) {
		for (size_t i = 0; i < loop.size(); ++i)
			if (EdgeId e = FindEdge(loop[i], loop[(i + 1) % loop.size()]); e != INVALID_ID)
				out.push_back(e);
	};
	add(m_faces[f].outer);
	for (const Loop &h : m_faces[f].holes)
		add(h);
	return out;
}

std::vector<Vec3> Model::OuterPoints(FaceId f) const {
	std::vector<Vec3> pts;
	for (VertexId v : m_faces[f].outer)
		pts.push_back(m_vertices[v].position);
	return pts;
}

PlaneInfo Model::FacePlane(FaceId f) const {
	return PlaneInfo::Make(m_vertices[m_faces[f].outer[0]].position, m_faces[f].normal);
}

Vec3 Model::FaceCentroid(FaceId f) const {
	// Centre de gravité de la surface (et non des sommets) : un point qui
	// tombe bien DANS la face pour un contour convexe.
	const PlaneInfo plane = FacePlane(f);
	std::vector<math::FVector2> poly;
	for (VertexId v : m_faces[f].outer)
		poly.push_back(plane.Project(m_vertices[v].position));
	float area = 0.f, cx = 0.f, cy = 0.f;
	for (size_t i = 0; i < poly.size(); ++i) {
		const math::FVector2 &a = poly[i], &b = poly[(i + 1) % poly.size()];
		const float cross = a.x * b.y - b.x * a.y;
		area += cross;
		cx += (a.x + b.x) * cross;
		cy += (a.y + b.y) * cross;
	}
	if (std::fabs(area) < 1e-12f) {
		Vec3 c{0.f, 0.f, 0.f};
		for (VertexId v : m_faces[f].outer)
			c += m_vertices[v].position;
		return c / float(m_faces[f].outer.size());
	}
	return plane.Unproject({cx / (3.f * area), cy / (3.f * area)});
}

float Model::FaceArea(FaceId f) const {
	const PlaneInfo plane = FacePlane(f);
	auto area = [&](const Loop &loop) {
		std::vector<math::FVector2> poly;
		for (VertexId v : loop)
			poly.push_back(plane.Project(m_vertices[v].position));
		return std::fabs(SignedArea(poly));
	};
	float total = area(m_faces[f].outer);
	for (const Loop &h : m_faces[f].holes)
		total -= area(h);
	return total;
}

math::FAABB Model::Bounds() const {
	math::FAABB box;
	for (const Vertex &v : m_vertices) {
		if (!v.alive)
			continue;
		box.min = Vec3::Min(box.min, v.position);
		box.max = Vec3::Max(box.max, v.position);
	}
	return box;
}

bool Model::FaceContains(FaceId f, const Vec3 &p, float tolerance) const {
	if (!IsAlive(f))
		return false;
	const PlaneInfo plane = FacePlane(f);
	auto poly = [&](const Loop &loop) {
		std::vector<math::FVector2> out;
		for (VertexId v : loop)
			out.push_back(plane.Project(m_vertices[v].position));
		return out;
	};
	const math::FVector2 q = plane.Project(p);
	if (!PointInPolygon(poly(m_faces[f].outer), q, tolerance))
		return false;
	for (const Loop &h : m_faces[f].holes) {
		const std::vector<math::FVector2> hp = poly(h);
		if (StrictlyInside(hp, q) && !OnBorder(hp, q, tolerance))
			return false; // dans un trou
	}
	return true;
}

// ── Édition ──────────────────────────────────────────────────────────────────

void Model::RemoveFace(FaceId f) {
	if (!IsAlive(f))
		return;
	m_faces[f].alive = false;
	FacesChanged();
	Touch();
}

void Model::RemoveEdge(EdgeId e) {
	if (!IsEdgeAlive(e))
		return;
	for (FaceId f : FacesOfEdge(e))
		m_faces[f].alive = false;
	m_edges[e].alive = false;
	FacesChanged();
	Touch();
}

void Model::RemoveOrphanVertices() {
	std::vector<bool> used(m_vertices.size(), false);
	for (const Edge &e : m_edges)
		if (e.alive)
			used[e.a] = used[e.b] = true;
	for (const Face &f : m_faces) {
		if (!f.alive)
			continue;
		for (VertexId v : f.outer)
			used[v] = true;
		for (const Loop &h : f.holes)
			for (VertexId v : h)
				used[v] = true;
	}
	for (size_t i = 0; i < m_vertices.size(); ++i)
		if (!used[i] && m_vertices[i].alive) {
			m_vertices[i].alive = false;
			Touch();
		}
}

void Model::FlipFace(FaceId f) {
	if (!IsAlive(f))
		return;
	Face &face = m_faces[f];
	std::reverse(face.outer.begin(), face.outer.end());
	for (Loop &h : face.holes)
		std::reverse(h.begin(), h.end());
	face.normal = face.normal * -1.f;
	Touch();
}

void Model::UpdateFaceNormal(FaceId f) {
	if (!IsAlive(f))
		return;
	Face &face = m_faces[f];
	std::vector<Vec3> pts = OuterPoints(f);
	const Vec3 n = NewellNormal(pts);
	if (n.LengthSq() > 0.5f)
		face.normal = n;
	else {
		face.alive = false; // aplatie par la transformation
		FacesChanged();
	}
	Touch();
}

void Model::TransformVertices(const std::vector<VertexId> &vertices, const math::FMatrix4 &matrix) {
	std::vector<bool> moved(m_vertices.size(), false);
	for (VertexId v : vertices) {
		if (v >= m_vertices.size() || moved[v] || !m_vertices[v].alive)
			continue;
		moved[v] = true;
		m_vertices[v].position = matrix.TransformPoint(m_vertices[v].position);
	}
	m_vertexIndexDirty = true;
	for (size_t f = 0; f < m_faces.size(); ++f) {
		if (!m_faces[f].alive)
			continue;
		bool touched = false;
		for (VertexId v : m_faces[f].outer)
			touched = touched || moved[v];
		for (const Loop &h : m_faces[f].holes)
			for (VertexId v : h)
				touched = touched || moved[v];
		if (touched)
			UpdateFaceNormal(FaceId(f));
	}
	Touch();
}

void Model::Transform(const math::FMatrix4 &matrix) {
	std::vector<VertexId> all;
	for (size_t i = 0; i < m_vertices.size(); ++i)
		if (m_vertices[i].alive)
			all.push_back(VertexId(i));
	TransformVertices(all, matrix);
}

void Model::WeldVertices(float tolerance) {
	// Grille de hachage : seules les cellules voisines sont comparées.
	const float cell = math::Max(tolerance, 1e-6f) * 2.f;
	auto key = [cell](const Vec3 &p) {
		const int64_t x = int64_t(std::floor(p.x / cell)), y = int64_t(std::floor(p.y / cell)),
					  z = int64_t(std::floor(p.z / cell));
		return std::array<int64_t, 3>{x, y, z};
	};
	std::map<std::array<int64_t, 3>, std::vector<VertexId>> grid;
	std::vector<VertexId> remap(m_vertices.size());
	for (size_t i = 0; i < m_vertices.size(); ++i) {
		remap[i] = VertexId(i);
		if (!m_vertices[i].alive)
			continue;
		const auto k = key(m_vertices[i].position);
		VertexId found = INVALID_ID;
		for (int64_t dx = -1; dx <= 1 && found == INVALID_ID; ++dx)
			for (int64_t dy = -1; dy <= 1 && found == INVALID_ID; ++dy)
				for (int64_t dz = -1; dz <= 1 && found == INVALID_ID; ++dz) {
					auto it = grid.find({k[0] + dx, k[1] + dy, k[2] + dz});
					if (it == grid.end())
						continue;
					for (VertexId other : it->second)
						if (m_vertices[other].position.DistanceSq(m_vertices[i].position) <= tolerance * tolerance) {
							found = other;
							break;
						}
				}
		if (found != INVALID_ID) {
			remap[i] = found;
			m_vertices[i].alive = false;
		} else {
			grid[k].push_back(VertexId(i));
		}
	}
	bool changed = false;
	m_vertexIndexDirty = m_edgeIndexDirty = true;
	FacesChanged();
	for (Edge &e : m_edges) {
		if (!e.alive)
			continue;
		const VertexId a = remap[e.a], b = remap[e.b];
		changed = changed || a != e.a || b != e.b;
		e.a = a;
		e.b = b;
		if (a == b)
			e.alive = false;
	}
	// Doublons d'arêtes : on garde la première, en cumulant les drapeaux.
	for (size_t i = 0; i < m_edges.size(); ++i) {
		if (!m_edges[i].alive)
			continue;
		for (size_t j = i + 1; j < m_edges.size(); ++j) {
			Edge &o = m_edges[j];
			if (o.alive && ((o.a == m_edges[i].a && o.b == m_edges[i].b) || (o.a == m_edges[i].b && o.b == m_edges[i].a))) {
				m_edges[i].soft = m_edges[i].soft && o.soft;
				m_edges[i].smooth = m_edges[i].smooth && o.smooth;
				o.alive = false;
			}
		}
	}
	for (Face &f : m_faces) {
		if (!f.alive)
			continue;
		for (VertexId &v : f.outer)
			v = remap[v];
		f.outer = CleanLoop(f.outer);
		for (Loop &h : f.holes) {
			for (VertexId &v : h)
				v = remap[v];
			h = CleanLoop(h);
		}
		std::erase_if(f.holes, [](const Loop &h) { return h.size() < 3; });
		if (f.outer.size() < 3)
			f.alive = false;
	}
	if (changed)
		Touch();
}

namespace {

struct HalfEdge {
	VertexId from, to;
};

} // namespace

FaceId Model::MergeCoplanar(FaceId f, float angleTolerance) {
	if (!IsAlive(f))
		return f;
	const PlaneInfo plane = FacePlane(f);
	const uint32_t material = m_faces[f].material;
	auto coplanar = [&](FaceId g) {
		const Face &face = m_faces[g];
		if (!face.alive || face.material != material || face.normal.Dot(plane.normal) < 1.f - angleTolerance)
			return false;
		for (VertexId v : face.outer)
			if (std::fabs(plane.Distance(m_vertices[v].position)) > 1e-4f)
				return false;
		return true;
	};

	// Faces coplanaires connexes par leurs arêtes.
	std::vector<FaceId> group{f};
	std::vector<bool> inGroup(m_faces.size(), false);
	inGroup[f] = true;
	for (size_t i = 0; i < group.size(); ++i)
		for (EdgeId e : EdgesOfFace(group[i]))
			for (FaceId g : FacesOfEdge(e))
				if (!inGroup[g] && coplanar(g)) {
					inGroup[g] = true;
					group.push_back(g);
				}
	if (group.size() == 1)
		return f;

	// Demi-arêtes orientées : une arête intérieure apparaît dans les deux
	// sens et s'annule ; il reste le bord de l'union.
	std::vector<HalfEdge> half;
	auto addLoop = [&](const Loop &loop) {
		for (size_t i = 0; i < loop.size(); ++i)
			half.push_back({loop[i], loop[(i + 1) % loop.size()]});
	};
	for (FaceId g : group) {
		addLoop(m_faces[g].outer);
		for (const Loop &h : m_faces[g].holes)
			addLoop(h);
	}
	std::vector<bool> cancelled(half.size(), false);
	std::vector<std::pair<VertexId, VertexId>> interior;
	for (size_t i = 0; i < half.size(); ++i) {
		if (cancelled[i])
			continue;
		for (size_t j = i + 1; j < half.size(); ++j)
			if (!cancelled[j] && half[j].from == half[i].to && half[j].to == half[i].from) {
				cancelled[i] = cancelled[j] = true;
				interior.push_back({half[i].from, half[i].to});
				break;
			}
	}
	std::multimap<VertexId, size_t> outgoing;
	for (size_t i = 0; i < half.size(); ++i)
		if (!cancelled[i])
			outgoing.insert({half[i].from, i});

	std::vector<Loop> loops;
	std::vector<bool> used(half.size(), false);
	for (size_t i = 0; i < half.size(); ++i) {
		if (cancelled[i] || used[i])
			continue;
		Loop loop;
		size_t cur = i;
		while (!used[cur]) {
			used[cur] = true;
			loop.push_back(half[cur].from);
			const VertexId next = half[cur].to;
			size_t nextEdge = SIZE_MAX;
			for (auto [it, end] = outgoing.equal_range(next); it != end; ++it)
				if (!used[it->second]) {
					nextEdge = it->second;
					break;
				}
			if (nextEdge == SIZE_MAX)
				break;
			cur = nextEdge;
		}
		if (loop.size() >= 3)
			loops.push_back(std::move(loop));
	}
	if (loops.empty())
		return f;

	// Le contour extérieur est la boucle d'aire positive la plus grande.
	size_t outerIndex = 0;
	float best = -1e30f;
	std::vector<float> areas;
	for (size_t i = 0; i < loops.size(); ++i) {
		std::vector<math::FVector2> poly;
		for (VertexId v : loops[i])
			poly.push_back(plane.Project(m_vertices[v].position));
		areas.push_back(SignedArea(poly));
		if (areas.back() > best) {
			best = areas.back();
			outerIndex = i;
		}
	}
	Face merged = m_faces[f];
	merged.outer = loops[outerIndex];
	merged.holes.clear();
	for (size_t i = 0; i < loops.size(); ++i)
		if (i != outerIndex && areas[i] < 0.f)
			merged.holes.push_back(loops[i]);
	for (FaceId g : group)
		m_faces[g].alive = false;
	m_faces[f] = merged;
	m_faces[f].alive = true;
	FacesChanged();

	// Arêtes intérieures devenues inutiles.
	for (auto [a, b] : interior)
		if (EdgeId e = FindEdge(a, b); e != INVALID_ID && FacesOfEdge(e).empty())
			m_edges[e].alive = false;

	// Cicatrisation : un sommet entre deux arêtes alignées, sans autre arête,
	// disparaît (l'arête est recousue).
	std::vector<VertexId> candidates = m_faces[f].outer;
	for (const Loop &h : m_faces[f].holes)
		candidates.insert(candidates.end(), h.begin(), h.end());
	for (VertexId v : candidates) {
		std::vector<EdgeId> around;
		for (size_t i = 0; i < m_edges.size(); ++i)
			if (m_edges[i].alive && m_edges[i].Has(v))
				around.push_back(EdgeId(i));
		if (around.size() != 2)
			continue;
		const VertexId p = m_edges[around[0]].Other(v), n = m_edges[around[1]].Other(v);
		const Vec3 d1 = (m_vertices[v].position - m_vertices[p].position).Normalize();
		const Vec3 d2 = (m_vertices[n].position - m_vertices[v].position).Normalize();
		if (d1.Dot(d2) < 1.f - 1e-5f || FindEdge(p, n) != INVALID_ID)
			continue;
		const Edge model = m_edges[around[0]];
		m_edges[around[0]].alive = false;
		m_edges[around[1]].alive = false;
		const EdgeId joined = AddEdge(p, n);
		m_edges[joined].soft = model.soft;
		m_edges[joined].smooth = model.smooth;
		m_edges[joined].layer = model.layer;
		for (Face &face : m_faces) {
			if (!face.alive)
				continue;
			std::erase(face.outer, v);
			for (Loop &h : face.holes)
				std::erase(h, v);
		}
		m_vertices[v].alive = false;
		FacesChanged();
	}
	Touch();
	return f;
}

Option<FaceId> Model::Imprint(FaceId inner) {
	if (!IsAlive(inner))
		return NONE;
	const Face in = m_faces[inner];
	for (size_t fi = 0; fi < m_faces.size(); ++fi) {
		if (fi == inner || !m_faces[fi].alive)
			continue;
		const Face &host = m_faces[fi];
		if (std::fabs(host.normal.Dot(in.normal)) < 1.f - 1e-4f)
			continue;
		const PlaneInfo plane = FacePlane(FaceId(fi));
		std::vector<math::FVector2> outer;
		for (VertexId o : host.outer)
			outer.push_back(plane.Project(m_vertices[o].position));
		bool inside = true;
		size_t onBorder = 0;
		for (VertexId v : in.outer) {
			const Vec3 &p = m_vertices[v].position;
			if (std::fabs(plane.Distance(p)) > 1e-4f || !FaceContains(FaceId(fi), p, 1e-5f)) {
				inside = false;
				break;
			}
			if (math::OnPolygonBorder(outer, plane.Project(p), 1e-5f))
				++onBorder;
		}
		if (!inside)
			continue;
		if (onBorder == in.outer.size()) {
			// Tous les sommets sur le bord : une face qui la recouvre si tous
			// ses côtés y sont aussi, sinon une bande qui la traverse.
			bool covering = true;
			for (size_t k = 0; k < in.outer.size() && covering; ++k) {
				const Vec3 mid =
					(m_vertices[in.outer[k]].position + m_vertices[in.outer[(k + 1) % in.outer.size()]].position) * 0.5f;
				covering = math::OnPolygonBorder(outer, plane.Project(mid), 1e-5f);
			}
			if (covering)
				continue;
		}
		if (in.normal.Dot(host.normal) < 0.f)
			FlipFace(inner);
		if (onBorder == 0) {
			Loop hole = m_faces[inner].outer;
			std::reverse(hole.begin(), hole.end()); // sens inverse de l'hôte
			m_faces[fi].holes.push_back(std::move(hole));
			FacesChanged();
			Touch();
			return Some(FaceId(fi));
		}
		if (onBorder >= 2 && CutAlongInner(FaceId(fi), inner))
			return Some(FaceId(fi));
	}
	return NONE;
}

bool Model::CutAlongInner(FaceId host, FaceId inner) {
	// 1. Les sommets de la forme posés sur le bord de l'hôte deviennent des
	//    sommets de son contour (arête coupée puis sommets soudés).
	{
		const Loop innerLoop = m_faces[inner].outer;
		for (VertexId v : innerLoop) {
			const Vec3 p = m_vertices[v].position;
			const Loop &outer = m_faces[host].outer;
			if (std::find(outer.begin(), outer.end(), v) != outer.end())
				continue;
			for (size_t i = 0; i < outer.size(); ++i) {
				const Vec3 &a = m_vertices[outer[i]].position, &b = m_vertices[outer[(i + 1) % outer.size()]].position;
				const Vec3 ab = b - a;
				const float t = (p - a).Dot(ab) / math::Max(ab.LengthSq(), 1e-20f);
				if (t <= 0.f || t >= 1.f || (a + ab * t).DistanceSq(p) > 1e-8f)
					continue;
				if (EdgeId e = FindEdge(outer[i], outer[(i + 1) % outer.size()]); e != INVALID_ID)
					(void)SplitEdge(e, p);
				break;
			}
		}
		WeldVertices();
		InvalidateIndices();
	}
	if (!IsAlive(host) || !IsAlive(inner))
		return false;

	// 2. Le contour de la forme se découpe en chaînes d'un sommet du bord au
	//    suivant ; chaque chaîne qui passe par l'intérieur coupe l'hôte (ou
	//    la partie de l'hôte qui la contient).
	const Loop loop = m_faces[inner].outer;
	auto onOuter = [&](FaceId f, VertexId v) {
		const Loop &o = m_faces[f].outer;
		return std::find(o.begin(), o.end(), v) != o.end();
	};
	std::vector<FaceId> parts{host};
	size_t start = loop.size();
	for (size_t i = 0; i < loop.size(); ++i)
		if (onOuter(host, loop[i])) {
			start = i;
			break;
		}
	if (start == loop.size())
		return false;
	std::vector<VertexId> chain{loop[start]};
	bool cut = false;
	for (size_t k = 1; k <= loop.size(); ++k) {
		const VertexId v = loop[(start + k) % loop.size()];
		chain.push_back(v);
		bool border = false;
		for (FaceId f : parts)
			border = border || (IsAlive(f) && onOuter(f, v));
		if (!border)
			continue;
		if (chain.size() > 2 || FindEdge(chain[0], chain[1]) == INVALID_ID || !IsBorderEdge(parts, chain[0], chain[1])) {
			for (FaceId f : parts) {
				if (!IsAlive(f) || !onOuter(f, chain.front()) || !onOuter(f, chain.back()))
					continue;
				if (Option<FaceId> other = SplitFaceAlong(f, chain); other.IsSome()) {
					parts.push_back(other.Unwrap());
					cut = true;
					break;
				}
			}
		}
		chain = {v};
	}
	if (!cut)
		return false;
	// 3. La partie identique à la forme fait doublon : la forme (et son
	//    matériau) la remplace.
	std::vector<VertexId> key = loop;
	std::sort(key.begin(), key.end());
	for (FaceId f : parts) {
		if (!IsAlive(f))
			continue;
		std::vector<VertexId> other = m_faces[f].outer;
		std::sort(other.begin(), other.end());
		if (other == key && m_faces[f].holes.empty())
			RemoveFace(f);
	}
	FacesChanged();
	Touch();
	return true;
}

bool Model::IsBorderEdge(const std::vector<FaceId> &faces, VertexId a, VertexId b) const {
	for (FaceId f : faces) {
		if (!IsAlive(f))
			continue;
		const Loop &o = m_faces[f].outer;
		for (size_t i = 0; i < o.size(); ++i) {
			const VertexId x = o[i], y = o[(i + 1) % o.size()];
			if ((x == a && y == b) || (x == b && y == a))
				return true;
		}
	}
	return false;
}

Option<FaceId> Model::SplitFaceAlong(FaceId f, const std::vector<VertexId> &path) {
	if (!IsAlive(f) || path.size() < 2 || path.front() == path.back())
		return NONE;
	if (path.size() == 2) {
		// Une seule arête : SplitFace, restreint à cette face.
		const Loop &o = m_faces[f].outer;
		if (std::find(o.begin(), o.end(), path[0]) == o.end() || std::find(o.begin(), o.end(), path[1]) == o.end())
			return NONE;
	}
	Face &face = m_faces[f];
	const auto ia = std::find(face.outer.begin(), face.outer.end(), path.front());
	const auto ib = std::find(face.outer.begin(), face.outer.end(), path.back());
	if (ia == face.outer.end() || ib == face.outer.end())
		return NONE;
	const size_t n = face.outer.size();
	const size_t i = size_t(ia - face.outer.begin()), j = size_t(ib - face.outer.begin());
	if (path.size() == 2 && ((i + 1) % n == j || (j + 1) % n == i))
		return NONE; // déjà une arête du contour
	// Le chemin doit passer à l'intérieur de la face.
	const Vec3 mid = path.size() == 2 ? (m_vertices[path[0]].position + m_vertices[path[1]].position) * 0.5f
									  : m_vertices[path[1]].position;
	if (!FaceContains(f, mid, -1e-6f) && path.size() == 2)
		return NONE;
	// first : contour de a à b, puis le chemin à rebours (b → a) ;
	// second : contour de b à a, puis le chemin (a → b).
	Loop first, second;
	for (size_t k = i;; k = (k + 1) % n) {
		first.push_back(face.outer[k]);
		if (k == j)
			break;
	}
	for (size_t k = path.size() - 2; k >= 1; --k)
		first.push_back(path[k]);
	for (size_t k = j;; k = (k + 1) % n) {
		second.push_back(face.outer[k]);
		if (k == i)
			break;
	}
	for (size_t k = 1; k + 1 < path.size(); ++k)
		second.push_back(path[k]);
	for (size_t k = 0; k + 1 < path.size(); ++k)
		(void)AddEdge(path[k], path[k + 1]);
	Face &host = m_faces[f]; // AddEdge ne touche pas aux faces, mais restons prudents
	const std::vector<Loop> holes = host.holes;
	Face other = host;
	host.outer = first;
	other.outer = second;
	host.holes.clear();
	other.holes.clear();
	const PlaneInfo plane = PlaneInfo::Make(m_vertices[path.front()].position, host.normal);
	std::vector<math::FVector2> poly;
	for (VertexId v : first)
		poly.push_back(plane.Project(m_vertices[v].position));
	for (const Loop &h : holes) {
		if (PointInPolygon(poly, plane.Project(m_vertices[h[0]].position), 0.f))
			host.holes.push_back(h);
		else
			other.holes.push_back(h);
	}
	m_faces.push_back(std::move(other));
	FacesChanged();
	Touch();
	return Some(FaceId(m_faces.size() - 1));
}

VertexId Model::SplitEdge(EdgeId e, const Vec3 &point) {
	if (!IsEdgeAlive(e))
		return INVALID_ID;
	const Edge edge = m_edges[e];
	const Vec3 a = m_vertices[edge.a].position, b = m_vertices[edge.b].position;
	const Vec3 ab = b - a;
	const float t = math::Clamp((point - a).Dot(ab) / math::Max(ab.LengthSq(), 1e-20f), 0.f, 1.f);
	const Vec3 p = a + ab * t;
	if (p.DistanceSq(a) <= WELD_EPSILON * WELD_EPSILON)
		return edge.a;
	if (p.DistanceSq(b) <= WELD_EPSILON * WELD_EPSILON)
		return edge.b;
	const std::vector<FaceId> faces = FacesOfEdge(e);
	m_vertices.push_back(Vertex{p, true});
	const VertexId v = VertexId(m_vertices.size() - 1);
	if (!m_vertexIndexDirty)
		IndexVertex(v);
	FacesChanged();
	// Insertion dans les contours, entre les deux extrémités.
	auto insert = [&](Loop &loop) {
		for (size_t i = 0; i < loop.size(); ++i) {
			const size_t j = (i + 1) % loop.size();
			if ((loop[i] == edge.a && loop[j] == edge.b) || (loop[i] == edge.b && loop[j] == edge.a)) {
				loop.insert(loop.begin() + std::ptrdiff_t(j == 0 ? loop.size() : j), v);
				return;
			}
		}
	};
	for (FaceId f : faces) {
		insert(m_faces[f].outer);
		for (Loop &h : m_faces[f].holes)
			insert(h);
	}
	m_edges[e].alive = false;
	for (VertexId end : {edge.a, edge.b}) {
		const EdgeId half = AddEdge(end, v);
		m_edges[half].soft = edge.soft;
		m_edges[half].smooth = edge.smooth;
		m_edges[half].curve = edge.curve;
		m_edges[half].layer = edge.layer;
	}
	Touch();
	return v;
}

Option<FaceId> Model::SplitFace(VertexId a, VertexId b) {
	if (a == b)
		return NONE;
	for (size_t fi = 0; fi < m_faces.size(); ++fi) {
		Face &face = m_faces[fi];
		if (!face.alive)
			continue;
		const auto ia = std::find(face.outer.begin(), face.outer.end(), a);
		const auto ib = std::find(face.outer.begin(), face.outer.end(), b);
		if (ia == face.outer.end() || ib == face.outer.end())
			continue;
		const size_t n = face.outer.size();
		size_t i = size_t(ia - face.outer.begin()), j = size_t(ib - face.outer.begin());
		if ((i + 1) % n == j || (j + 1) % n == i)
			continue; // déjà une arête du contour
		// Le segment doit passer à l'intérieur de la face.
		const Vec3 mid = (m_vertices[a].position + m_vertices[b].position) * 0.5f;
		if (!FaceContains(FaceId(fi), mid))
			continue;
		Loop first, second;
		for (size_t k = i;; k = (k + 1) % n) {
			first.push_back(face.outer[k]);
			if (k == j)
				break;
		}
		for (size_t k = j;; k = (k + 1) % n) {
			second.push_back(face.outer[k]);
			if (k == i)
				break;
		}
		(void)AddEdge(a, b);
		const std::vector<Loop> holes = face.holes;
		Face other = face;
		face.outer = first;
		other.outer = second;
		face.holes.clear();
		other.holes.clear();
		// Chaque trou va dans la partie qui le contient.
		const PlaneInfo plane = PlaneInfo::Make(m_vertices[a].position, face.normal);
		std::vector<math::FVector2> poly;
		for (VertexId v : first)
			poly.push_back(plane.Project(m_vertices[v].position));
		for (const Loop &h : holes) {
			if (PointInPolygon(poly, plane.Project(m_vertices[h[0]].position), 0.f))
				face.holes.push_back(h);
			else
				other.holes.push_back(h);
		}
		m_faces.push_back(std::move(other));
		FacesChanged();
		Touch();
		return Some(FaceId(m_faces.size() - 1));
	}
	return NONE;
}

std::vector<FaceId> Model::Append(const Model &other, const math::FMatrix4 &transform) {
	std::vector<uint32_t> materials, layers;
	for (const Material &m : other.m_materials)
		materials.push_back(AddMaterial(m.name, m.color, m.textureScale));
	for (const Layer &l : other.m_layers)
		layers.push_back(AddLayer(l.name, l.visible));
	auto layerOf = [&](uint32_t l) { return l < layers.size() ? layers[l] : 0u; };
	std::vector<VertexId> vmap(other.m_vertices.size(), INVALID_ID);
	for (size_t i = 0; i < other.m_vertices.size(); ++i)
		if (other.m_vertices[i].alive)
			vmap[i] = AddVertex(transform.TransformPoint(other.m_vertices[i].position));
	const uint32_t curveBase = m_nextCurve;
	uint32_t maxCurve = 0;
	for (const Edge &e : other.m_edges) {
		if (!e.alive)
			continue;
		const EdgeId id = AddEdge(vmap[e.a], vmap[e.b]);
		if (id == INVALID_ID)
			continue;
		m_edges[id].soft = e.soft;
		m_edges[id].smooth = e.smooth;
		m_edges[id].layer = layerOf(e.layer);
		if (e.curve != INVALID_ID) {
			m_edges[id].curve = curveBase + e.curve;
			maxCurve = math::Max(maxCurve, e.curve + 1);
		}
	}
	m_nextCurve = curveBase + maxCurve;
	std::vector<FaceId> added;
	for (const Face &f : other.m_faces) {
		if (!f.alive)
			continue;
		Loop outer;
		for (VertexId v : f.outer)
			outer.push_back(vmap[v]);
		std::vector<Loop> holes;
		for (const Loop &h : f.holes) {
			Loop hole;
			for (VertexId v : h)
				hole.push_back(vmap[v]);
			holes.push_back(std::move(hole));
		}
		const uint32_t material = f.material < materials.size() ? materials[f.material] : INVALID_ID;
		if (Option<FaceId> id = AddFaceFromVertices(outer, holes, FaceOptions{material, layerOf(f.layer)}); id.IsSome())
			added.push_back(id.Unwrap());
	}
	// AddFaceFromVertices ne recopie pas les drapeaux des arêtes existantes.
	return added;
}

void Model::Compact() {
	std::vector<VertexId> vmap(m_vertices.size(), INVALID_ID);
	std::vector<Vertex> vertices;
	RemoveOrphanVertices();
	for (size_t i = 0; i < m_vertices.size(); ++i)
		if (m_vertices[i].alive) {
			vmap[i] = VertexId(vertices.size());
			vertices.push_back(m_vertices[i]);
		}
	std::vector<Edge> edges;
	for (const Edge &e : m_edges)
		if (e.alive && vmap[e.a] != INVALID_ID && vmap[e.b] != INVALID_ID) {
			Edge c = e;
			c.a = vmap[e.a];
			c.b = vmap[e.b];
			edges.push_back(c);
		}
	std::vector<Face> faces;
	for (const Face &f : m_faces) {
		if (!f.alive)
			continue;
		Face c = f;
		for (VertexId &v : c.outer)
			v = vmap[v];
		for (Loop &h : c.holes)
			for (VertexId &v : h)
				v = vmap[v];
		faces.push_back(std::move(c));
	}
	m_vertices = std::move(vertices);
	m_edges = std::move(edges);
	m_faces = std::move(faces);
	m_vertexIndexDirty = m_edgeIndexDirty = true;
	FacesChanged();
	Touch();
}

void Model::Clear() {
	m_vertices.clear();
	m_edges.clear();
	m_faces.clear();
	m_vertexIndexDirty = m_edgeIndexDirty = true;
	FacesChanged();
	m_nextCurve = 0;
	Touch();
}

} // namespace modeling
