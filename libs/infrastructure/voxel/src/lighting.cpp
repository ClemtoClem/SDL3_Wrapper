// Définitions de voxel/lighting.hpp
#include <voxel/lighting.hpp>
#include <voxel/physics.hpp>

#include <algorithm>
#include <cmath>

namespace voxel::lighting {

namespace {

/// Lecture rapide de la grille : le chunk courant est gardé tant que le
/// parcours y reste (un parcours DDA change rarement de chunk).
class Cursor {
public:
	explicit Cursor(const World &world) : m_world(world) {}
	/// Chunk de la cellule `p` (nullptr : non chargé) ; `local` ses coordonnées dedans.
	const Chunk *At(const BlockPos &p, BlockPos &local) {
		const ChunkPos cp = ChunkOf(p);
		if (!m_valid || !(cp == m_pos)) {
			m_pos = cp;
			m_chunk = m_world.GetChunk(cp);
			m_valid = true;
		}
		local = LocalOf(p);
		return m_chunk;
	}

private:
	const World &m_world;
	ChunkPos m_pos{0, 0, 0};
	const Chunk *m_chunk = nullptr;
	bool m_valid = false;
};

/// Le segment [a, b] coupe-t-il la boîte ? (méthode des plans, t ∈ [0, 1])
bool SegmentHitsBox(const math::FVector3 &a, const math::FVector3 &b, const Box &box) {
	float t0 = 0.f, t1 = 1.f;
	const float o[3] = {a.x, a.y, a.z}, d[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
	const float lo[3] = {box.min.x, box.min.y, box.min.z}, hi[3] = {box.max.x, box.max.y, box.max.z};
	for (int i = 0; i < 3; ++i) {
		if (std::fabs(d[i]) < 1e-9f) {
			if (o[i] < lo[i] || o[i] > hi[i])
				return false;
			continue;
		}
		float u = (lo[i] - o[i]) / d[i], v = (hi[i] - o[i]) / d[i];
		if (u > v)
			std::swap(u, v);
		t0 = std::max(t0, u);
		t1 = std::min(t1, v);
		if (t0 > t1)
			return false;
	}
	return true;
}

/// Parcours DDA (Amanatides & Woo) des cellules traversées par [a, b] ;
/// `visit(cellule)` rend faux pour arrêter.
template <typename Visit> void Traverse(const math::FVector3 &a, const math::FVector3 &b, Visit &&visit) {
	BlockPos cell = BlockPos::FromWorld(a);
	const BlockPos last = BlockPos::FromWorld(b);
	const float d[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
	const float o[3] = {a.x, a.y, a.z};
	int step[3], c[3] = {cell.x, cell.y, cell.z};
	float tMax[3], tDelta[3];
	for (int i = 0; i < 3; ++i) {
		if (d[i] > 0.f) {
			step[i] = 1;
			tDelta[i] = 1.f / d[i];
			tMax[i] = (float(c[i] + 1) - o[i]) / d[i];
		} else if (d[i] < 0.f) {
			step[i] = -1;
			tDelta[i] = -1.f / d[i];
			tMax[i] = (float(c[i]) - o[i]) / d[i];
		} else {
			step[i] = 0;
			tDelta[i] = tMax[i] = 1e30f;
		}
	}
	// Garde-fou : jamais plus de cellules que la somme des écarts (+ marge).
	const int limit = std::abs(last.x - cell.x) + std::abs(last.y - cell.y) + std::abs(last.z - cell.z) + 3;
	for (int n = 0; n <= limit; ++n) {
		if (!visit(BlockPos{c[0], c[1], c[2]}))
			return;
		if (c[0] == last.x && c[1] == last.y && c[2] == last.z)
			return;
		const int axis = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
		if (tMax[axis] > 1.f)
			return; // fin du segment
		c[axis] += step[axis];
		tMax[axis] += tDelta[axis];
	}
}

float Window(float d, float range) {
	const float x = d / range, x2 = x * x;
	const float w = std::clamp(1.f - x2 * x2, 0.f, 1.f);
	return w * w;
}

float Smoothstep(float e0, float e1, float x) {
	if (e1 <= e0)
		return x >= e1 ? 1.f : 0.f;
	const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

/// Quart de tour (sens de RotateBlockBox) appliqué à un vecteur.
math::FVector3 Rotate(math::FVector3 v, uint8_t param) {
	for (int r = 0; r < (param & 3); ++r)
		v = {-v.z, v.y, v.x};
	return v;
}

} // namespace

float SrgbToLinear(float c) noexcept {
	c = std::clamp(c, 0.f, 1.f);
	return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

Rgb SrgbToLinear(uint8_t r, uint8_t g, uint8_t b) noexcept {
	return {SrgbToLinear(float(r) / 255.f), SrgbToLinear(float(g) / 255.f), SrgbToLinear(float(b) / 255.f)};
}

int EquivalentLevel(const LightDef &light) noexcept {
	if (!light.Active())
		return 0;
	return std::clamp(int(std::lround(light.intensity / LegacyIntensity(1))), 1, 15);
}

Source Source::ForBlock(const BlockDef &block, const BlockPos &pos, uint8_t param) {
	Source s;
	s.def = &block.look.light;
	s.cell = pos;
	s.position = pos.Center() + Rotate(block.look.light.offset, param);
	s.direction = Rotate(block.look.light.direction, param);
	return s;
}

Source Source::ForEntity(const LightDef &light, const math::FVector3 &position) {
	Source s;
	s.def = &light;
	s.cell = BlockPos::FromWorld(position);
	s.position = position;
	s.direction = light.direction;
	return s;
}

void GatherSources(const World &world, const math::FVector3 &lo, const math::FVector3 &hi, std::vector<Source> &out) {
	const float reach = world.MaxLightRange();
	auto reaches = [&](const math::FVector3 &p, float range) {
		// Distance de la source à la boîte ≤ portée.
		const float dx = std::max({lo.x - p.x, 0.f, p.x - hi.x});
		const float dy = std::max({lo.y - p.y, 0.f, p.y - hi.y});
		const float dz = std::max({lo.z - p.z, 0.f, p.z - hi.z});
		return dx * dx + dy * dy + dz * dz <= range * range;
	};
	if (reach > 0.f) {
		const ContentRegistry &content = world.Content();
		const ChunkPos c0 = ChunkOf(BlockPos::FromWorld(lo - math::FVector3{reach, reach, reach}));
		const ChunkPos c1 = ChunkOf(BlockPos::FromWorld(hi + math::FVector3{reach, reach, reach}));
		for (int cy = c0.y; cy <= c1.y; ++cy)
			for (int cz = c0.z; cz <= c1.z; ++cz)
				for (int cx = c0.x; cx <= c1.x; ++cx) {
					const Chunk *chunk = world.GetChunk({cx, cy, cz});
					if (!chunk)
						continue;
					const BlockPos origin = ChunkPos{cx, cy, cz}.Origin();
					for (uint16_t index : chunk->LightCells(content)) {
						const int x = index % CHUNK_SIZE, z = (index / CHUNK_SIZE) % CHUNK_SIZE, y = index / (CHUNK_SIZE * CHUNK_SIZE);
						const BlockDef &def = content.Block(chunk->Get(x, y, z));
						const Source s = Source::ForBlock(def, origin + BlockPos{x, y, z}, chunk->Param(x, y, z));
						if (reaches(s.position, def.look.light.range))
							out.push_back(s);
					}
				}
	}
	for (const World::DynamicLight &l : world.DynamicLights())
		if (l.light.Active() && reaches(l.position, l.light.range))
			out.push_back(Source::ForEntity(l.light, l.position));
}

Rgb Unoccluded(const Source &source, const math::FVector3 &point, const math::FVector3 &normal, math::FVector3 &toSource) {
	const LightDef &l = *source.def;
	if (!l.Active())
		return {0.f, 0.f, 0.f};
	math::FVector3 L;
	float scalar = l.intensity;
	if (l.type == LightDef::Type::DIRECTIONAL) {
		// Faisceau parallèle : un cylindre de rayon `radius` le long de
		// `direction` ; pas d'inverse du carré (les rayons ne divergent pas).
		const math::FVector3 rel = point - source.position;
		const float along = rel.Dot(source.direction);
		if (along < 0.f || along > l.range)
			return {0.f, 0.f, 0.f};
		if ((rel - source.direction * along).Length() > l.radius)
			return {0.f, 0.f, 0.f};
		L = source.direction * -1.f;
		toSource = point - source.direction * along;
		scalar *= (l.falloff == LightDef::Falloff::LINEAR ? std::max(0.f, 1.f - along / l.range) : 1.f) * Window(along, l.range);
	} else {
		const math::FVector3 toLight = source.position - point;
		const float d2 = toLight.Dot(toLight);
		const float d = std::sqrt(d2);
		if (d > l.range)
			return {0.f, 0.f, 0.f};
		L = d > 1e-6f ? toLight * (1.f / d) : math::FVector3{0.f, 1.f, 0.f};
		toSource = source.position;
		switch (l.falloff) {
		case LightDef::Falloff::INVERSE_SQUARE: scalar /= std::max(d2, MIN_DISTANCE_SQ); break;
		case LightDef::Falloff::LINEAR: scalar *= std::max(0.f, 1.f - d / l.range); break;
		case LightDef::Falloff::NONE: break;
		}
		scalar *= Window(d, l.range);
		if (l.type == LightDef::Type::SPOT)
			scalar *= Smoothstep(l.outerCos, l.innerCos, (L * -1.f).Dot(source.direction));
	}
	// Loi de Lambert (sans normale : éclairement scalaire, toutes directions).
	if (normal.x != 0.f || normal.y != 0.f || normal.z != 0.f)
		scalar *= std::max(0.f, normal.Dot(L));
	if (!(scalar > 0.f) || !std::isfinite(scalar))
		return {0.f, 0.f, 0.f};
	return {l.color[0] * scalar, l.color[1] * scalar, l.color[2] * scalar};
}

Rgb Transmittance(const World &world, const math::FVector3 &a, const math::FVector3 &b, const BlockPos &skip) {
	Rgb t{1.f, 1.f, 1.f};
	Cursor cursor(world);
	const ContentRegistry &content = world.Content();
	Traverse(a, b, [&](const BlockPos &cell) {
		if (cell == skip)
			return true;
		BlockPos local;
		const Chunk *chunk = cursor.At(cell, local);
		if (!chunk)
			return true; // inconnu : rien ne l'arrête
		const BlockDef &def = content.Block(chunk->Get(local.x, local.y, local.z));
		const OpticsDef &o = def.optics;
		if (o.Clear())
			return true;
		bool hit = o.fullCell;
		if (!hit)
			for (const Box &box : SelectionBoxes(world, cell))
				if (SegmentHitsBox(a, b, box)) {
					hit = true;
					break;
				}
		if (hit)
			for (int k = 0; k < 3; ++k)
				t[size_t(k)] *= o.transmittance[size_t(k)];
		return t[0] + t[1] + t[2] > 1e-4f;
	});
	if (t[0] + t[1] + t[2] <= 1e-4f)
		return {0.f, 0.f, 0.f};
	return t;
}

Rgb DirectIrradiance(const World &world, const math::FVector3 &point, const math::FVector3 &normal, const std::vector<Source> &sources) {
	Rgb e{0.f, 0.f, 0.f};
	for (const Source &s : sources) {
		math::FVector3 end;
		const Rgb c = Unoccluded(s, point, normal, end);
		if (c[0] + c[1] + c[2] <= 0.f)
			continue;
		const Rgb t = Transmittance(world, point, end, s.cell);
		for (int k = 0; k < 3; ++k)
			e[size_t(k)] += c[size_t(k)] * t[size_t(k)];
	}
	return e;
}

float SkyAccess(const World &world, const math::FVector3 &point) {
	static const math::FVector3 DIRS[5] = {
		{0.f, 1.f, 0.f}, {0.7071f, 0.7071f, 0.f}, {-0.7071f, 0.7071f, 0.f}, {0.f, 0.7071f, 0.7071f}, {0.f, 0.7071f, -0.7071f}};
	static constexpr float WEIGHTS[5] = {0.4f, 0.15f, 0.15f, 0.15f, 0.15f};
	constexpr float LENGTH = 32.f;
	const ContentRegistry &content = world.Content();
	ChunkGenerator *generator = world.Generator();
	const BlockPos start = BlockPos::FromWorld(point);
	float access = 0.f;
	for (int r = 0; r < 5; ++r) {
		Cursor cursor(world);
		float t = 1.f;
		bool escaped = true;
		Traverse(point, point + DIRS[r] * LENGTH, [&](const BlockPos &cell) {
			if (cell == start)
				return true;
			BlockPos local;
			const Chunk *chunk = cursor.At(cell, local);
			if (!chunk) {
				// Hors des chunks chargés : sous le relief estimé, couvert.
				if (generator)
					if (Option<int> cover = generator->SkyCover(cell.x, cell.z); cover.IsSome() && cell.y <= cover.Unwrap())
						escaped = false;
				return false;
			}
			if (chunk->IsUniform() && chunk->UniformBlock() == AIR)
				return true;
			const OpticsDef &o = content.Block(chunk->Get(local.x, local.y, local.z)).optics;
			if (!o.Clear() && o.fullCell)
				t *= (o.transmittance[0] + o.transmittance[1] + o.transmittance[2]) / 3.f;
			if (t <= 1e-3f) {
				escaped = false;
				return false;
			}
			return true;
		});
		if (escaped)
			access += WEIGHTS[r] * t;
	}
	return std::clamp(access, 0.f, 1.f);
}

} // namespace voxel::lighting
