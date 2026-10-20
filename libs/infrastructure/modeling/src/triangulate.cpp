// Définitions de modeling/triangulate.hpp
#include <modeling/triangulate.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace modeling {

namespace {

using V2 = math::FVector2;

} // namespace

namespace {

bool LayerVisible(const Model &model, uint32_t layer) {
	return layer >= model.Layers().size() || model.Layers()[layer].visible;
}

} // namespace

std::vector<EdgeId> VisibleEdges(const Model &model) {
	std::vector<EdgeId> out;
	for (size_t i = 0; i < model.Edges().size(); ++i) {
		const Edge &e = model.Edges()[i];
		if (e.alive && !e.soft && LayerVisible(model, e.layer))
			out.push_back(EdgeId(i));
	}
	return out;
}

MeshData BuildMesh(const Model &model, const BuildOptions &options) {
	MeshData mesh;
	const auto &faces = model.Faces();
	const auto &edges = model.Edges();

	// Adjacence sommet → faces, et arêtes lisses par paire de sommets.
	std::multimap<VertexId, FaceId> facesOfVertex;
	for (size_t f = 0; f < faces.size(); ++f) {
		if (!faces[f].alive)
			continue;
		for (VertexId v : faces[f].outer)
			facesOfVertex.insert({v, FaceId(f)});
		for (const Loop &h : faces[f].holes)
			for (VertexId v : h)
				facesOfVertex.insert({v, FaceId(f)});
	}
	// Précalculs du lissage : arêtes lisses par sommet, arêtes (clés
	// triées) de chaque face, aires des faces.
	auto key = [](VertexId x, VertexId y) {
		return x < y ? (uint64_t(x) << 32) | y : (uint64_t(y) << 32) | x;
	};
	std::vector<std::vector<VertexId>> smoothAt(model.Vertices().size());
	bool anySmooth = false;
	for (const Edge &e : edges)
		if (e.alive && e.smooth) {
			smoothAt[e.a].push_back(e.b);
			smoothAt[e.b].push_back(e.a);
			anySmooth = true;
		}
	std::vector<std::vector<uint64_t>> faceKeys;
	std::vector<float> areas;
	if (anySmooth) {
		faceKeys.resize(faces.size());
		areas.resize(faces.size(), 0.f);
		for (size_t f = 0; f < faces.size(); ++f) {
			if (!faces[f].alive)
				continue;
			auto add = [&](const Loop &l) {
				for (size_t i = 0; i < l.size(); ++i)
					faceKeys[f].push_back(key(l[i], l[(i + 1) % l.size()]));
			};
			add(faces[f].outer);
			for (const Loop &h : faces[f].holes)
				add(h);
			std::sort(faceKeys[f].begin(), faceKeys[f].end());
			areas[f] = math::Max(model.FaceArea(FaceId(f)), 1e-6f);
		}
	}
	auto smoothBetween = [&](FaceId f, FaceId g, VertexId around) {
		// f et g partagent-elles une arête lisse passant par `around` ?
		for (VertexId other : smoothAt[around]) {
			const uint64_t k = key(around, other);
			if (std::binary_search(faceKeys[f].begin(), faceKeys[f].end(), k) &&
				std::binary_search(faceKeys[g].begin(), faceKeys[g].end(), k))
				return true;
		}
		return false;
	};
	auto cornerNormal = [&](FaceId f, VertexId v) {
		// Faces atteintes depuis f en franchissant des arêtes lisses autour de v.
		std::vector<FaceId> group{f};
		auto [begin, end] = facesOfVertex.equal_range(v);
		bool grew = !smoothAt[v].empty();
		while (grew) {
			grew = false;
			for (auto it = begin; it != end; ++it) {
				const FaceId g = it->second;
				if (std::find(group.begin(), group.end(), g) != group.end())
					continue;
				for (FaceId h : group)
					if (smoothBetween(h, g, v)) {
						group.push_back(g);
						grew = true;
						break;
					}
			}
		}
		if (group.size() == 1)
			return faces[f].normal;
		Vec3 n{0.f, 0.f, 0.f};
		for (FaceId g : group)
			n += faces[g].normal * areas[g];
		return n.LengthSq() > 1e-20f ? n.Normalize() : faces[f].normal;
	};

	// Faces triées par matériau : un groupe d'indices par matériau.
	std::vector<FaceId> order;
	for (size_t f = 0; f < faces.size(); ++f)
		if (faces[f].alive && (options.includeHiddenLayers || LayerVisible(model, faces[f].layer)))
			order.push_back(FaceId(f));
	std::stable_sort(order.begin(), order.end(),
					 [&](FaceId a, FaceId b) { return faces[a].material < faces[b].material; });

	for (FaceId f : order) {
		const Face &face = faces[f];
		const PlaneInfo plane = model.FacePlane(f);
		std::vector<VertexId> corner;
		std::vector<V2> outer2;
		std::vector<std::vector<V2>> holes2;
		for (VertexId v : face.outer) {
			corner.push_back(v);
			outer2.push_back(plane.Project(model.Position(v)));
		}
		for (const Loop &h : face.holes) {
			std::vector<V2> hp;
			for (VertexId v : h) {
				corner.push_back(v);
				hp.push_back(plane.Project(model.Position(v)));
			}
			holes2.push_back(std::move(hp));
		}
		std::vector<uint32_t> local;
		if (!math::TriangulatePolygon(outer2, holes2, local))
			continue;

		const Material *mat = face.material < model.Materials().size() ? &model.Materials()[face.material] : nullptr;
		const float scale = mat && mat->textureScale > 1e-6f ? mat->textureScale : 1.f;
		const Color color = mat ? mat->color : options.defaultColor;
		const uint32_t base = uint32_t(mesh.positions.size());
		for (size_t i = 0; i < corner.size(); ++i) {
			const Vec3 &p = model.Position(corner[i]);
			mesh.positions.push_back(p);
			mesh.normals.push_back(anySmooth ? cornerNormal(f, corner[i]) : face.normal);
			const V2 q = plane.Project(p);
			mesh.uvs.push_back({q.x / scale, q.y / scale});
			mesh.colors.push_back(color);
		}
		// Le sens des triangles suit la normale de la face.
		for (size_t t = 0; t + 2 < local.size(); t += 3) {
			uint32_t a = local[t], b = local[t + 1], c = local[t + 2];
			const Vec3 tn = (mesh.positions[base + b] - mesh.positions[base + a])
								.Cross(mesh.positions[base + c] - mesh.positions[base + a]);
			if (tn.Dot(face.normal) < 0.f)
				std::swap(b, c);
			if (mesh.groups.empty() || mesh.groups.back().material != face.material)
				mesh.groups.push_back(MeshGroup{uint32_t(mesh.indices.size()), 0, face.material});
			mesh.indices.insert(mesh.indices.end(), {base + a, base + b, base + c});
			mesh.groups.back().count += 3;
			mesh.triangleFace.push_back(f);
		}
		if (options.doubleSided) {
			const uint32_t back = uint32_t(mesh.positions.size());
			for (size_t i = 0; i < corner.size(); ++i) {
				mesh.positions.push_back(mesh.positions[base + i]);
				mesh.normals.push_back(mesh.normals[base + i] * -1.f);
				mesh.uvs.push_back(mesh.uvs[base + i]);
				mesh.colors.push_back(color);
			}
			const size_t count = mesh.indices.size();
			for (size_t t = count - (local.size() / 3) * 3; t + 2 < count; t += 3) {
				mesh.indices.insert(mesh.indices.end(), {mesh.indices[t] - base + back, mesh.indices[t + 2] - base + back,
														 mesh.indices[t + 1] - base + back});
				mesh.groups.back().count += 3;
				mesh.triangleFace.push_back(f);
			}
		}
	}
	return mesh;
}

Option<RayHit> Raycast(const Model &model, const math::FRay &ray, bool includeHiddenLayers) {
	Option<RayHit> best = NONE;
	const auto &faces = model.Faces();
	for (size_t f = 0; f < faces.size(); ++f) {
		const Face &face = faces[f];
		if (!face.alive || (!includeHiddenLayers && !LayerVisible(model, face.layer)))
			continue;
		const float denom = face.normal.Dot(ray.direction);
		if (std::fabs(denom) < 1e-8f)
			continue;
		const Vec3 origin = model.Position(face.outer[0]);
		const float t = (origin - ray.origin).Dot(face.normal) / denom;
		if (t <= 1e-6f || (best.IsSome() && t >= best.Value().distance))
			continue;
		const Vec3 p = ray.At(t);
		if (!model.FaceContains(FaceId(f), p, 1e-5f))
			continue;
		best = Some(RayHit{FaceId(f), t, p, face.normal});
	}
	return best;
}

} // namespace modeling
