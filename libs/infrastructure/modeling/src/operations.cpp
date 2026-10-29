// Définitions de modeling/operations.hpp
#include <modeling/operations.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace modeling {

namespace {

/// Ajoute un quadrilatère latéral orienté vers `outward`.
Option<FaceId> AddSide(Model &model, Loop quad, const Vec3 &outward, uint32_t material) {
	std::vector<Vec3> pts;
	for (VertexId v : quad)
		pts.push_back(model.Position(v));
	if (NewellNormal(pts).Dot(outward) < 0.f)
		std::reverse(quad.begin(), quad.end());
	return model.AddFaceFromVertices(quad, {}, FaceOptions{material});
}

bool PartOfSolid(const Model &model, FaceId face) {
	for (EdgeId e : model.EdgesOfFace(face))
		if (model.FacesOfEdge(e).size() > 1)
			return true;
	return false;
}

/// Toutes les faces qui touchent un sommet de `face` (autres que `face`)
/// lui sont-elles perpendiculaires ? Alors pousser/tirer = déplacer la face.
bool NeighboursPerpendicular(const Model &model, FaceId face) {
	const Face &f = model.Faces()[face];
	std::vector<VertexId> verts = f.outer;
	for (const Loop &h : f.holes)
		verts.insert(verts.end(), h.begin(), h.end());
	bool any = false;
	for (size_t g = 0; g < model.Faces().size(); ++g) {
		const Face &other = model.Faces()[g];
		if (!other.alive || g == face)
			continue;
		bool touches = false;
		auto check = [&](const Loop &loop) {
			for (VertexId v : loop)
				touches = touches || std::find(verts.begin(), verts.end(), v) != verts.end();
		};
		check(other.outer);
		for (const Loop &h : other.holes)
			check(h);
		if (!touches)
			continue;
		any = true;
		if (std::fabs(other.normal.Dot(f.normal)) > 1e-3f)
			return false;
	}
	// Les arêtes libres attachées aux sommets seraient aussi entraînées :
	// elles doivent être perpendiculaires (ou appartenir à la face).
	for (const Edge &e : model.Edges()) {
		if (!e.alive)
			continue;
		const bool ha = std::find(verts.begin(), verts.end(), e.a) != verts.end();
		const bool hb = std::find(verts.begin(), verts.end(), e.b) != verts.end();
		if (ha == hb)
			continue;
		const Vec3 d = (model.Position(e.b) - model.Position(e.a)).Normalize();
		if (std::fabs(std::fabs(d.Dot(f.normal)) - 1.f) > 1e-3f)
			return false;
	}
	return any;
}

} // namespace

Result<FaceId, String> PushPull(Model &model, FaceId face, float distance, const PushPullOptions &options) {
	if (!model.IsAlive(face))
		return Err(String("Pousser/Tirer : face inexistante"));
	if (std::fabs(distance) < 1e-6f)
		return Ok(face);
	const Face source = model.Faces()[face];
	const Vec3 n = source.normal;
	const Vec3 offset = n * distance;
	const bool solid = PartOfSolid(model, face);

	// Dessus d'un solide aux flancs perpendiculaires : on déplace la face.
	if (solid && !options.keepOriginal && NeighboursPerpendicular(model, face)) {
		std::vector<VertexId> verts = source.outer;
		for (const Loop &h : source.holes)
			verts.insert(verts.end(), h.begin(), h.end());
		// Pousser jusqu'à la face opposée (ou au-delà) écraserait le solide.
		const math::FAABB box = model.Bounds();
		const float extent = (box.max - box.min).Length();
		if (distance < 0.f && -distance >= extent)
			return Err(String("Pousser/Tirer : la distance traverse tout le solide"));
		model.TransformVertices(verts, math::FMatrix4::Translate(offset));
		model.WeldVertices();
		return Ok(face);
	}

	// Extrusion : nouvelle face décalée + faces latérales.
	auto moved = [&](const Loop &loop) {
		Loop out;
		for (VertexId v : loop)
			out.push_back(model.AddVertex(model.Position(v) + offset));
		return out;
	};
	const Loop topOuter = moved(source.outer);
	std::vector<Loop> topHoles;
	for (const Loop &h : source.holes)
		topHoles.push_back(moved(h));

	// Le solide est entre les deux plans. Pour un côté (a→b) d'un contour
	// orienté autour de n (contour trigonométrique, trous en sens inverse),
	// l'extérieur du solide est du côté (b − a) × n — exact même pour un
	// contour concave, contrairement à une direction issue du centroïde.
	const float sign = distance > 0.f ? 1.f : -1.f;
	// Creuser un solide (face d'un solide poussée vers l'arrière) ENLÈVE de
	// la matière : les parois de la poche regardent vers son intérieur et
	// son fond vers l'extérieur du solide (+n).
	const bool carve = solid && distance < 0.f && !options.keepOriginal;
	const float wallSign = carve ? -1.f : 1.f;
	std::vector<FaceId> sides;
	auto sideLoop = [&](const Loop &base, const Loop &top) {
		for (size_t i = 0; i < base.size(); ++i) {
			const size_t j = (i + 1) % base.size();
			const Vec3 outward = (model.Position(base[j]) - model.Position(base[i])).Cross(n) * wallSign;
			if (Option<FaceId> f = AddSide(model, {base[i], base[j], top[j], top[i]}, outward, source.material);
				f.IsSome())
				sides.push_back(f.Unwrap());
		}
	};
	sideLoop(source.outer, topOuter);
	for (size_t h = 0; h < source.holes.size(); ++h)
		sideLoop(source.holes[h], topHoles[h]);

	Option<FaceId> top = model.AddFaceFromVertices(topOuter, topHoles, FaceOptions{source.material, source.layer});
	if (top.IsNone())
		return Err(String("Pousser/Tirer : face d'arrivée dégénérée"));
	const float topSign = carve ? 1.f : sign;
	if (model.Faces()[top.Unwrap()].normal.Dot(n) * topSign < 0.f)
		model.FlipFace(top.Unwrap());

	if (solid && !options.keepOriginal) {
		model.RemoveFace(face); // le solide se prolonge
	} else if (model.Faces()[face].normal.Dot(n) * sign > 0.f) {
		model.FlipFace(face); // face libre : devient le fond du solide
	}
	// Les flancs coplanaires avec des faces existantes ne font plus qu'un.
	for (FaceId s : sides)
		if (model.IsAlive(s))
			(void)model.MergeCoplanar(s);
	return Ok(top.Unwrap());
}

Result<std::vector<Vec3>, String> OffsetOutline(const Model &model, FaceId face, float distance) {
	if (!model.IsAlive(face))
		return Err(String("Décalage : face inexistante"));
	if (std::fabs(distance) < 1e-6f)
		return Ok(model.OuterPoints(face));
	const Face source = model.Faces()[face];
	const PlaneInfo plane = model.FacePlane(face);
	std::vector<math::FVector2> poly;
	for (VertexId v : source.outer)
		poly.push_back(plane.Project(model.Position(v)));
	const size_t n = poly.size();

	// Chaque côté est décalé vers l'intérieur (à gauche, contour
	// trigonométrique) ; les nouveaux sommets sont les intersections des
	// côtés décalés voisins.
	std::vector<math::FVector2> shifted;
	for (size_t i = 0; i < n; ++i) {
		const math::FVector2 prev = poly[(i + n - 1) % n], cur = poly[i], next = poly[(i + 1) % n];
		math::FVector2 d0 = cur - prev, d1 = next - cur;
		if (d0.LengthSq() < 1e-12f || d1.LengthSq() < 1e-12f)
			return Err(String("Décalage : contour dégénéré"));
		d0 = d0.Normalize();
		d1 = d1.Normalize();
		const math::FVector2 n0{-d0.y, d0.x}, n1{-d1.y, d1.x};
		const math::FVector2 p0 = prev + n0 * distance, p1 = cur + n1 * distance;
		const float denom = d0.x * d1.y - d0.y * d1.x;
		if (std::fabs(denom) < 1e-6f) {
			shifted.push_back(cur + n1 * distance);
			continue;
		}
		const math::FVector2 w = p1 - p0;
		const float t = (w.x * d1.y - w.y * d1.x) / denom;
		shifted.push_back(p0 + d0 * t);
	}
	const float before = SignedArea(poly), after = SignedArea(shifted);
	if (after <= 0.f || (distance > 0.f && after >= before) || (distance < 0.f && after <= before))
		return Err(String("Décalage : distance trop grande pour ce contour"));
	// Les côtés décalés ne doivent pas s'être retournés.
	for (size_t i = 0; i < n; ++i) {
		const math::FVector2 a = poly[(i + 1) % n] - poly[i];
		const math::FVector2 b = shifted[(i + 1) % n] - shifted[i];
		if (a.Dot(b) <= 0.f)
			return Err(String("Décalage : distance trop grande pour ce contour"));
	}

	std::vector<Vec3> out;
	for (const math::FVector2 &q : shifted)
		out.push_back(plane.Unproject(q));
	return Ok(std::move(out));
}

Result<FaceId, String> Offset(Model &model, FaceId face, float distance) {
	if (!model.IsAlive(face))
		return Err(String("Décalage : face inexistante"));
	if (std::fabs(distance) < 1e-6f)
		return Ok(face);
	Result<std::vector<Vec3>, String> outline = OffsetOutline(model, face, distance);
	if (outline.IsError())
		return Err(outline.Error());
	const Face source = model.Faces()[face];
	Loop loop;
	for (const Vec3 &p : outline.Value())
		loop.push_back(model.AddVertex(p));
	if (distance > 0.f) {
		// Face intérieure (avec les trous d'origine) + anneau.
		Option<FaceId> inner =
			model.AddFaceFromVertices(loop, source.holes, FaceOptions{source.material, source.layer});
		if (inner.IsNone())
			return Err(String("Décalage : face intérieure dégénérée"));
		Loop hole = loop;
		std::reverse(hole.begin(), hole.end());
		model.Faces()[face].holes = {hole};
		model.InvalidateIndices();
		return Ok(inner.Unwrap());
	}
	Option<FaceId> ring =
		model.AddFaceFromVertices(loop, {source.outer}, FaceOptions{source.material, source.layer});
	if (ring.IsNone())
		return Err(String("Décalage : anneau dégénéré"));
	if (model.Faces()[ring.Unwrap()].normal.Dot(source.normal) < 0.f)
		model.FlipFace(ring.Unwrap());
	return Ok(ring.Unwrap());
}

Result<FaceId, String> FollowMe(Model &model, FaceId profile, const std::vector<Vec3> &pathIn) {
	if (!model.IsAlive(profile))
		return Err(String("Suivez-moi : profil inexistant"));
	std::vector<Vec3> path;
	for (const Vec3 &p : pathIn)
		if (path.empty() || path.back().DistanceSq(p) > 1e-10f)
			path.push_back(p);
	if (path.size() < 2)
		return Err(String("Suivez-moi : le chemin doit avoir au moins deux points"));
	const bool closed = path.size() > 3 && path.front().DistanceSq(path.back()) < 1e-8f;
	if (closed)
		path.pop_back();
	const size_t count = path.size();
	const Face source = model.Faces()[profile];
	if (!source.holes.empty())
		return Err(String("Suivez-moi : profil avec trous non pris en charge"));

	auto direction = [&](size_t i) { // segment i → i+1
		return (path[(i + 1) % count] - path[i % count]).Normalize();
	};
	auto projectAlong = [](const std::vector<Vec3> &ring, const Vec3 &dir, const Vec3 &planePoint,
						   const Vec3 &planeNormal) {
		std::vector<Vec3> out;
		const float denom = dir.Dot(planeNormal);
		for (const Vec3 &p : ring) {
			const float t = std::fabs(denom) < 1e-8f ? 0.f : (planePoint - p).Dot(planeNormal) / denom;
			out.push_back(p + dir * t);
		}
		return out;
	};

	std::vector<Vec3> ring = model.OuterPoints(profile);
	std::vector<std::vector<Vec3>> rings;
	const size_t segments = closed ? count : count - 1;
	if (closed) {
		// Premier anneau sur le plan bissecteur du point de départ.
		const Vec3 bis = (direction(count - 1) + direction(0)).Normalize();
		ring = projectAlong(ring, direction(0), path[0], bis);
	}
	rings.push_back(ring);
	for (size_t i = 1; i <= segments; ++i) {
		const Vec3 d = direction(i - 1);
		Vec3 planeNormal = d;
		if (closed || i < count - 1) {
			const Vec3 next = direction(i % count);
			const Vec3 bis = d + next;
			planeNormal = bis.LengthSq() > 1e-8f ? bis.Normalize() : d;
		}
		ring = projectAlong(ring, d, path[i % count], planeNormal);
		rings.push_back(ring);
	}

	const size_t pn = rings[0].size();
	std::vector<std::vector<VertexId>> ids;
	for (const auto &r : rings) {
		std::vector<VertexId> row;
		for (const Vec3 &p : r)
			row.push_back(model.AddVertex(p));
		ids.push_back(std::move(row));
	}
	// Le profil peut être une courbe : ses arêtes adoucies le restent le long du balayage.
	std::vector<bool> softProfile(pn, false);
	for (size_t k = 0; k < pn; ++k) {
		const EdgeId e = model.FindEdge(source.outer[k], source.outer[(k + 1) % pn]);
		const EdgeId prev = model.FindEdge(source.outer[(k + pn - 1) % pn], source.outer[k]);
		softProfile[k] = e != INVALID_ID && prev != INVALID_ID && model.Edges()[e].curve != INVALID_ID &&
						 model.Edges()[e].curve == model.Edges()[prev].curve;
	}
	for (size_t i = 0; i + 1 < ids.size(); ++i) {
		// Le profil (et donc chaque anneau projeté) tourne dans le sens
		// trigonométrique autour de sa normale : l'extérieur d'un côté
		// (a→b) est (b − a) × normale, même pour un profil concave.
		const Vec3 ringNormal = NewellNormal(rings[i]);
		for (size_t k = 0; k < pn; ++k) {
			const size_t k2 = (k + 1) % pn;
			const Vec3 outward = (rings[i][k2] - rings[i][k]).Cross(ringNormal);
			(void)AddSide(model, {ids[i][k], ids[i][k2], ids[i + 1][k2], ids[i + 1][k]}, outward, source.material);
			// Arête longitudinale au milieu d'une courbe du profil : adoucie.
			if (softProfile[k])
				if (EdgeId e = model.FindEdge(ids[i][k], ids[i + 1][k]); e != INVALID_ID)
					model.SetEdgeSmoothing(e, true, true);
		}
	}
	if (closed) {
		// Dernier anneau = premier : on referme, le profil d'origine disparaît.
		model.RemoveFace(profile);
		model.WeldVertices();
		return Ok(profile);
	}
	// Extrémités : début (le profil, s'il est en place) et fin.
	if (model.Faces()[profile].normal.Dot(direction(0)) > 0.f)
		model.FlipFace(profile);
	Loop endLoop = ids.back();
	Option<FaceId> end = model.AddFaceFromVertices(endLoop, {}, FaceOptions{source.material, source.layer});
	if (end.IsNone())
		return Err(String("Suivez-moi : extrémité dégénérée"));
	if (model.Faces()[end.Unwrap()].normal.Dot(direction(count - 2)) < 0.f)
		model.FlipFace(end.Unwrap());
	return Ok(end.Unwrap());
}

Result<FaceId, String> Extrude(Model &model, const std::vector<Vec3> &outline, const Vec3 &vector, uint32_t material) {
	Option<FaceId> base = model.AddFace(outline, {}, FaceOptions{material});
	if (base.IsNone())
		return Err(String("Extrusion : contour dégénéré ou non plan"));
	const Vec3 n = model.Faces()[base.Unwrap()].normal;
	const float along = vector.Dot(n);
	if (std::fabs(along) < 1e-6f)
		return Err(String("Extrusion : direction parallèle au contour"));
	// Direction quelconque : suivez-moi sur un chemin d'un segment.
	if ((vector - n * along).LengthSq() > 1e-10f) {
		const Vec3 c = model.FaceCentroid(base.Unwrap());
		return FollowMe(model, base.Unwrap(), {c, c + vector});
	}
	return PushPull(model, base.Unwrap(), along);
}

Result<std::vector<FaceId>, String> Revolve(Model &model, const Vec3 &origin, const Vec3 &axisIn,
											const std::vector<math::FVector2> &profile, const RevolveOptions &options) {
	if (profile.size() < 2)
		return Err(String("Révolution : le profil doit avoir au moins deux points"));
	const PlaneInfo frame = PlaneInfo::Make(origin, axisIn);
	const Vec3 axis = frame.normal;
	const int segments = math::Max(options.segments, 3);
	const bool full = options.angle >= 2.f * math::PI - 1e-4f;
	const int ringCount = full ? segments : segments + 1;
	const size_t rows = profile.size();
	std::vector<std::vector<VertexId>> rings(rows);
	for (size_t r = 0; r < rows; ++r) {
		const float radius = profile[r].x, height = profile[r].y;
		for (int i = 0; i < ringCount; ++i) {
			const float a = options.angle * float(i) / float(segments);
			const Vec3 p = origin + axis * height + frame.u * (math::Cos(a) * radius) + frame.v * (math::Sin(a) * radius);
			rings[r].push_back(model.AddVertex(p));
		}
	}
	std::vector<FaceId> faces;
	const size_t rowPairs = options.closed ? rows : rows - 1;
	for (size_t r = 0; r < rowPairs; ++r) {
		const auto &lower = rings[r], &upper = rings[(r + 1) % rows];
		for (int i = 0; i < segments; ++i) {
			const int i2 = (i + 1) % ringCount;
			Loop quad{lower[size_t(i)], lower[size_t(i2)], upper[size_t(i2)], upper[size_t(i)]};
			// Sommets confondus sur l'axe : triangle.
			std::vector<VertexId> clean;
			for (VertexId v : quad)
				if (std::find(clean.begin(), clean.end(), v) == clean.end())
					clean.push_back(v);
			if (clean.size() < 3)
				continue;
			// Vers l'extérieur : à l'opposé de l'axe.
			const Vec3 mid = (model.Position(lower[size_t(i)]) + model.Position(upper[size_t(i2)])) * 0.5f;
			Vec3 outward = mid - origin;
			outward = outward - axis * outward.Dot(axis);
			std::vector<Vec3> pts;
			for (VertexId v : clean)
				pts.push_back(model.Position(v));
			if (NewellNormal(pts).Dot(outward) < 0.f)
				std::reverse(clean.begin(), clean.end());
			if (Option<FaceId> f = model.AddFaceFromVertices(clean, {}, FaceOptions{options.material, 0, true, true});
				f.IsSome())
				faces.push_back(f.Unwrap());
		}
	}
	// Révolution partielle : les méridiens de début et de fin restent visibles.
	if (!full)
		for (size_t r = 0; r + 1 < rows; ++r)
			for (int i : {0, segments})
				if (EdgeId e = model.FindEdge(rings[r][size_t(i)], rings[r + 1][size_t(i)]); e != INVALID_ID)
					model.SetEdgeSmoothing(e, false, false);
	if (!options.closed && full) {
		auto cap = [&](size_t r, bool up) {
			if (profile[r].x <= 1e-6f)
				return;
			Loop loop = rings[r];
			if (Option<FaceId> f = model.AddFaceFromVertices(loop, {}, FaceOptions{options.material}); f.IsSome()) {
				if ((model.Faces()[f.Unwrap()].normal.Dot(axis) > 0.f) != up)
					model.FlipFace(f.Unwrap());
				faces.push_back(f.Unwrap());
			}
		};
		cap(0, profile.front().y > profile.back().y);
		cap(rows - 1, profile.back().y >= profile.front().y);
		// Les arêtes des bords des capuchons restent visibles.
		for (size_t r : {size_t(0), rows - 1})
			for (int i = 0; i < ringCount; ++i)
				if (EdgeId e = model.FindEdge(rings[r][size_t(i)], rings[r][size_t((i + 1) % ringCount)]);
					e != INVALID_ID)
					model.SetEdgeSmoothing(e, false, false);
	}
	return Ok(std::move(faces));
}

Result<std::vector<Vec3>, String> ChainEdges(const Model &model, const std::vector<EdgeId> &edges, const Vec3 &near) {
	std::vector<EdgeId> alive;
	for (EdgeId e : edges)
		if (model.IsEdgeAlive(e) && std::find(alive.begin(), alive.end(), e) == alive.end())
			alive.push_back(e);
	if (alive.empty())
		return Err(String("Chemin : aucune arête sélectionnée"));
	auto degree = [&](VertexId v) {
		int d = 0;
		for (EdgeId e : alive)
			d += model.Edges()[e].Has(v) ? 1 : 0;
		return d;
	};
	// Départ : une extrémité libre (degré 1) la plus proche de `near`, ou
	// n'importe quel sommet pour une boucle.
	VertexId start = INVALID_ID;
	float best = 1e30f;
	bool open = false;
	for (EdgeId e : alive)
		for (VertexId v : {model.Edges()[e].a, model.Edges()[e].b}) {
			const int d = degree(v);
			if (d > 2)
				return Err(String("Chemin : les arêtes se ramifient"));
			if (d == 1) {
				open = true;
				const float dist = model.Position(v).DistanceSq(near);
				if (dist < best) {
					best = dist;
					start = v;
				}
			}
		}
	if (!open)
		start = model.Edges()[alive[0]].a;
	std::vector<Vec3> path{model.Position(start)};
	std::vector<bool> used(alive.size(), false);
	VertexId cur = start;
	for (size_t step = 0; step < alive.size(); ++step) {
		size_t next = SIZE_MAX;
		for (size_t i = 0; i < alive.size(); ++i)
			if (!used[i] && model.Edges()[alive[i]].Has(cur)) {
				next = i;
				break;
			}
		if (next == SIZE_MAX)
			return Err(String("Chemin : les arêtes ne forment pas une chaîne continue"));
		used[next] = true;
		cur = model.Edges()[alive[next]].Other(cur);
		path.push_back(model.Position(cur));
	}
	return Ok(std::move(path));
}

Model ExtractFaces(const Model &model, const std::vector<FaceId> &faces) {
	Model out;
	out.Materials() = model.Materials();
	out.Layers() = model.Layers();
	for (FaceId f : faces) {
		if (!model.IsAlive(f))
			continue;
		const Face &face = model.Faces()[f];
		std::vector<Vec3> outer;
		for (VertexId v : face.outer)
			outer.push_back(model.Position(v));
		std::vector<std::vector<Vec3>> holes;
		for (const Loop &h : face.holes) {
			std::vector<Vec3> hole;
			for (VertexId v : h)
				hole.push_back(model.Position(v));
			holes.push_back(std::move(hole));
		}
		(void)out.AddFace(outer, holes, FaceOptions{face.material, face.layer});
	}
	// Drapeaux des arêtes (adoucies, lissées) recopiés.
	for (const Edge &e : model.Edges()) {
		if (!e.alive || (!e.soft && !e.smooth))
			continue;
		const VertexId a = out.FindVertex(model.Position(e.a)), b = out.FindVertex(model.Position(e.b));
		if (EdgeId id = out.FindEdge(a, b); id != INVALID_ID) {
			out.SetEdgeSmoothing(id, e.soft, e.smooth);
		}
	}
	return out;
}

std::vector<VertexId> VerticesOf(const Model &model, const std::vector<FaceId> &faces, const std::vector<EdgeId> &edges) {
	std::vector<VertexId> out;
	auto add = [&](VertexId v) {
		if (std::find(out.begin(), out.end(), v) == out.end())
			out.push_back(v);
	};
	for (FaceId f : faces) {
		if (!model.IsAlive(f))
			continue;
		for (VertexId v : model.Faces()[f].outer)
			add(v);
		for (const Loop &h : model.Faces()[f].holes)
			for (VertexId v : h)
				add(v);
	}
	for (EdgeId e : edges)
		if (model.IsEdgeAlive(e)) {
			add(model.Edges()[e].a);
			add(model.Edges()[e].b);
		}
	return out;
}

void Move(Model &model, const std::vector<VertexId> &vertices, const Vec3 &delta) {
	model.TransformVertices(vertices, math::FMatrix4::Translate(delta));
	model.WeldVertices();
}

void Rotate(Model &model, const std::vector<VertexId> &vertices, const Vec3 &center, const Vec3 &axis, float angle) {
	const math::FMatrix4 m = math::FMatrix4::Translate(center) * math::FMatrix4::Rotate(axis.Normalize(), angle) *
							 math::FMatrix4::Translate(center * -1.f);
	model.TransformVertices(vertices, m);
	model.WeldVertices();
}

void Scale(Model &model, const std::vector<VertexId> &vertices, const Vec3 &center, const Vec3 &factors) {
	const math::FMatrix4 m =
		math::FMatrix4::Translate(center) * math::FMatrix4::Scale(factors) * math::FMatrix4::Translate(center * -1.f);
	model.TransformVertices(vertices, m);
	// Échelle négative sur un nombre impair d'axes : les faces se retournent.
	if (factors.x * factors.y * factors.z < 0.f) {
		for (size_t f = 0; f < model.Faces().size(); ++f)
			if (model.Faces()[f].alive) {
				bool touched = false;
				for (VertexId v : model.Faces()[f].outer)
					touched = touched || std::find(vertices.begin(), vertices.end(), v) != vertices.end();
				if (touched)
					model.FlipFace(FaceId(f));
			}
	}
	model.WeldVertices();
}

void Erase(Model &model, const std::vector<EdgeId> &edges, const std::vector<FaceId> &faces) {
	for (FaceId f : faces)
		model.RemoveFace(f);
	for (EdgeId e : edges)
		model.RemoveEdge(e);
	model.RemoveOrphanVertices();
}

void Paint(Model &model, FaceId face, uint32_t material) {
	if (!model.IsAlive(face))
		return;
	model.Faces()[face].material = material;
	model.Touch();
}

size_t SoftenEdges(Model &model, float maxAngle, const std::vector<FaceId> &faces) {
	const float minCos = math::Cos(maxAngle);
	std::vector<EdgeId> soften;
	for (size_t e = 0; e < model.Edges().size(); ++e) {
		if (!model.Edges()[e].alive)
			continue;
		const std::vector<FaceId> around = model.FacesOfEdge(EdgeId(e));
		if (around.size() != 2)
			continue;
		if (!faces.empty() && (std::find(faces.begin(), faces.end(), around[0]) == faces.end() ||
							   std::find(faces.begin(), faces.end(), around[1]) == faces.end()))
			continue;
		const float c = std::as_const(model).Faces()[around[0]].normal.Dot(std::as_const(model).Faces()[around[1]].normal);
		if (c >= minCos && c < 1.f - 1e-6f)
			soften.push_back(EdgeId(e));
	}
	for (EdgeId e : soften)
		model.SetEdgeSmoothing(e, true, true);
	return soften.size();
}

} // namespace modeling
