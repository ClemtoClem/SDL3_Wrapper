// Définitions de modeling/primitives.hpp
#include <modeling/operations.hpp>
#include <modeling/primitives.hpp>

namespace modeling {

namespace {

/// Construit dans un modèle local puis l'ajoute, placé, au modèle cible.
template <class Build>
Result<std::vector<FaceId>, String> Place(Model &model, const PrimitiveOptions &options, Build &&build) {
	Model local;
	if (Option<String> err = build(local); err.IsSome())
		return Err(err.Unwrap());
	if (options.material != INVALID_ID)
		for (Face &f : local.Faces())
			f.material = INVALID_ID; // repris ci-dessous
	std::vector<FaceId> faces = model.Append(local, options.transform);
	// Append ne reprend pas les drapeaux sur des arêtes déjà présentes : on
	// recopie l'adoucissement d'après le modèle local.
	for (const Edge &e : local.Edges()) {
		if (!e.alive || (!e.soft && !e.smooth))
			continue;
		const VertexId a = model.FindVertex(options.transform.TransformPoint(local.Position(e.a)), 1e-4f);
		const VertexId b = model.FindVertex(options.transform.TransformPoint(local.Position(e.b)), 1e-4f);
		if (EdgeId id = model.FindEdge(a, b); id != INVALID_ID) {
			model.SetEdgeSmoothing(id, e.soft, e.smooth);
		}
	}
	if (options.material != INVALID_ID)
		for (FaceId f : faces)
			Paint(model, f, options.material);
	return Ok(std::move(faces));
}

Option<String> Check(bool ok, const char *what) { return ok ? Option<String>(NONE) : Some(String(what)); }

} // namespace

Result<std::vector<FaceId>, String> AddPyramid(Model &model, float baseSize, float height, bool cap,
											   const PrimitiveOptions &options) {
	return Place(model, options, [&](Model &m) -> Option<String> {
		if (baseSize <= 0.f || height <= 0.f)
			return Some(String("Pyramide : dimensions positives attendues"));
		const float h = baseSize * 0.5f;
		const Vec3 apex{0.f, height, 0.f};
		const Vec3 base[4] = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
		for (int i = 0; i < 4; ++i)
			(void)m.AddFace({base[i], apex, base[(i + 1) % 4]});
		if (cap)
			(void)m.AddFace({base[0], base[1], base[2], base[3]});
		// Faces vers l'extérieur.
		for (size_t f = 0; f < m.Faces().size(); ++f)
			if ((m.FaceCentroid(FaceId(f)) - Vec3{0.f, height * 0.25f, 0.f}).Dot(m.Faces()[f].normal) < 0.f)
				m.FlipFace(FaceId(f));
		return NONE;
	});
}

Result<std::vector<FaceId>, String> AddHalfSphere(Model &model, float radius, int segments, int stacks, bool cap,
												  const PrimitiveOptions &options) {
	return Place(model, options, [&](Model &m) -> Option<String> {
		if (radius <= 0.f)
			return Some(String("Demi-sphère : rayon positif attendu"));
		std::vector<math::FVector2> profile;
		stacks = math::Max(stacks, 2);
		for (int i = 0; i <= stacks; ++i) {
			const float a = 0.5f * math::PI * float(i) / float(stacks);
			profile.push_back({radius * math::Cos(a), radius * math::Sin(a)});
		}
		profile.back().x = 0.f;
		auto faces = Revolve(m, {0, 0, 0}, {0, 1, 0}, profile, RevolveOptions{segments});
		if (faces.IsError())
			return Some(faces.Error());
		if (!cap) {
			// La face du bas est le seul disque (normale vers le bas).
			for (size_t f = 0; f < m.Faces().size(); ++f)
				if (m.Faces()[f].alive && m.Faces()[f].normal.y < -0.99f)
					m.RemoveFace(FaceId(f));
		}
		return NONE;
	});
}

Result<std::vector<FaceId>, String> AddCapsule(Model &model, float radius, float height, int segments, int stacks,
											   const PrimitiveOptions &options) {
	return Place(model, options, [&](Model &m) -> Option<String> {
		if (radius <= 0.f || height < 0.f)
			return Some(String("Capsule : rayon positif et hauteur ≥ 0 attendus"));
		stacks = math::Max(stacks, 2);
		std::vector<math::FVector2> profile;
		for (int i = stacks; i >= 0; --i) { // hémisphère bas : du pôle à l'équateur
			const float a = 0.5f * math::PI * float(i) / float(stacks);
			profile.push_back({radius * math::Cos(a), radius - radius * math::Sin(a)});
		}
		for (int i = 0; i <= stacks; ++i) { // hémisphère haut
			const float a = 0.5f * math::PI * float(i) / float(stacks);
			profile.push_back({radius * math::Cos(a), radius + height + radius * math::Sin(a)});
		}
		profile.front().x = 0.f;
		profile.back().x = 0.f;
		if (height <= 1e-6f) { // sphère : l'équateur est dupliqué
			profile.erase(profile.begin() + stacks + 1);
		}
		auto faces = Revolve(m, {0, 0, 0}, {0, 1, 0}, profile, RevolveOptions{segments});
		return faces.IsError() ? Some(faces.Error()) : Option<String>(NONE);
	});
}

Result<std::vector<FaceId>, String> AddStairs(Model &model, const Vec3 &size, int steps, const PrimitiveOptions &options) {
	return Place(model, options, [&](Model &m) -> Option<String> {
		if (Option<String> e = Check(size.x > 0.f && size.y > 0.f && size.z > 0.f && steps >= 1,
									 "Escalier : dimensions positives et au moins une marche attendues");
			e.IsSome())
			return e;
		// Profil en dents de scie dans le plan x = -largeur/2, extrudé vers +x.
		const float rise = size.y / float(steps), run = size.z / float(steps);
		const float x = -size.x * 0.5f;
		std::vector<Vec3> profile{{x, 0, 0}, {x, 0, size.z}};
		for (int i = steps; i >= 1; --i) {
			profile.push_back({x, rise * float(i), run * float(i)});
			profile.push_back({x, rise * float(i), run * float(i - 1)});
		}
		Option<FaceId> f = m.AddFace(profile);
		if (f.IsNone())
			return Some(String("Escalier : profil dégénéré"));
		const float dir = m.Faces()[f.Unwrap()].normal.x > 0.f ? 1.f : -1.f;
		auto top = PushPull(m, f.Unwrap(), size.x * dir);
		return top.IsError() ? Some(top.Error()) : Option<String>(NONE);
	});
}

Result<std::vector<FaceId>, String> AddSpiralStairs(Model &model, float innerRadius, float outerRadius, float height,
													int steps, float totalAngle, const PrimitiveOptions &options) {
	return Place(model, options, [&](Model &m) -> Option<String> {
		if (innerRadius < 0.f || outerRadius <= innerRadius || height <= 0.f || steps < 1)
			return Some(String("Escalier hélicoïdal : rayons croissants, hauteur positive et marches attendus"));
		const float rise = height / float(steps), sweep = totalAngle / float(steps);
		for (int i = 0; i < steps; ++i) {
			// Secteur d'anneau au niveau de la marche, tiré de la hauteur d'une marche.
			const float a0 = sweep * float(i), a1 = sweep * float(i + 1), y = rise * float(i);
			const int arc = 4;
			std::vector<Vec3> sector;
			for (int k = 0; k <= arc; ++k) {
				const float a = a0 + (a1 - a0) * float(k) / float(arc);
				sector.push_back({outerRadius * math::Cos(a), y, outerRadius * math::Sin(a)});
			}
			if (innerRadius > 1e-6f)
				for (int k = arc; k >= 0; --k) {
					const float a = a0 + (a1 - a0) * float(k) / float(arc);
					sector.push_back({innerRadius * math::Cos(a), y, innerRadius * math::Sin(a)});
				}
			else
				sector.push_back({0.f, y, 0.f});
			Option<FaceId> f = m.AddFace(sector);
			if (f.IsNone())
				return Some(String("Escalier hélicoïdal : marche dégénérée"));
			if (m.Faces()[f.Unwrap()].normal.y < 0.f)
				m.FlipFace(f.Unwrap());
			if (auto top = PushPull(m, f.Unwrap(), rise); top.IsError())
				return Some(top.Error());
		}
		return NONE;
	});
}

Result<std::vector<FaceId>, String> AddArch(Model &model, float width, float thickness, float depth, int segments,
											float angle, const PrimitiveOptions &options) {
	return Place(model, options, [&](Model &m) -> Option<String> {
		const float outer = width * 0.5f, inner = outer - thickness;
		if (width <= 0.f || thickness <= 0.f || inner <= 0.f || depth <= 0.f)
			return Some(String("Arche : largeur, épaisseur (< largeur/2) et profondeur positives attendues"));
		segments = math::Max(segments, 2);
		const float start = 0.5f * (math::PI - angle);
		std::vector<Vec3> outline;
		for (int i = 0; i <= segments; ++i) {
			const float a = start + angle * float(i) / float(segments);
			outline.push_back({outer * math::Cos(a), outer * math::Sin(a), -depth * 0.5f});
		}
		for (int i = segments; i >= 0; --i) {
			const float a = start + angle * float(i) / float(segments);
			outline.push_back({inner * math::Cos(a), inner * math::Sin(a), -depth * 0.5f});
		}
		Option<FaceId> f = m.AddFace(outline);
		if (f.IsNone())
			return Some(String("Arche : profil dégénéré"));
		if (m.Faces()[f.Unwrap()].normal.z > 0.f)
			m.FlipFace(f.Unwrap());
		if (auto top = PushPull(m, f.Unwrap(), -depth); top.IsError())
			return Some(top.Error());
		// L'intrados et l'extrados sont des surfaces courbes : adoucies.
		(void)SoftenEdges(m, math::PI / float(segments) * 1.5f);
		return NONE;
	});
}

Result<std::vector<FaceId>, String> AddRamp(Model &model, const Vec3 &size, const PrimitiveOptions &options) {
	return Place(model, options, [&](Model &m) -> Option<String> {
		if (size.x <= 0.f || size.y <= 0.f || size.z <= 0.f)
			return Some(String("Rampe : dimensions positives attendues"));
		const float x = -size.x * 0.5f;
		Option<FaceId> f = m.AddFace({{x, 0, 0}, {x, 0, size.z}, {x, size.y, size.z}});
		if (f.IsNone())
			return Some(String("Rampe : profil dégénéré"));
		const float dir = m.Faces()[f.Unwrap()].normal.x > 0.f ? 1.f : -1.f;
		auto top = PushPull(m, f.Unwrap(), size.x * dir);
		return top.IsError() ? Some(top.Error()) : Option<String>(NONE);
	});
}

} // namespace modeling
