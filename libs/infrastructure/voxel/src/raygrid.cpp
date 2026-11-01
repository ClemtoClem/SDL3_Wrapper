// Définitions de voxel/raygrid.hpp
#include <voxel/raygrid.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <voxel/lighting.hpp>

namespace voxel::raygrid {

namespace {

/// Transmission moyenne (0..1) d'une cellule pour la lumière du ciel.
float SkyTransmission(const ContentRegistry &content, BlockId id) {
	const OpticsDef &o = content.Block(id).optics;
	if (o.Clear() || !o.fullCell)
		return 1.f;
	return (o.transmittance[0] + o.transmittance[1] + o.transmittance[2]) / 3.f;
}

uint16_t Encode(float light) { return uint16_t(std::clamp(int(std::lround(light / 4.f * 255.f)), 0, 255)); }

} // namespace

bool StopsRays(const BlockDef &block) noexcept {
	const bool translucent = block.look.material == RenderMaterial::TRANSLUCENT || block.look.material == RenderMaterial::TRANSPARENT;
	return !block.IsAir() && !block.fluid && !translucent && block.look.material != RenderMaterial::INVISIBLE &&
		   block.shape.kind == ShapeKind::CUBE;
}

bool FillChunk(const World &world, const ChunkPos &pos, bool sky, std::span<uint16_t> out) {
	constexpr int N = CHUNK_SIZE;
	const ContentRegistry &content = world.Content();
	const Chunk *chunk = world.GetChunk(pos);
	for (size_t i = 0; i < out.size(); i += CHANNELS) {
		out[i] = 0;
		out[i + 1] = 255;
		out[i + 2] = out[i + 3] = 0;
	}
	if (!chunk || out.size() < CHUNK_TEXELS)
		return false;
	auto texel = [&](int x, int y, int z) { return &out[size_t(((z * N + y) * N + x) * CHANNELS)]; };
	const BlockPos origin = pos.Origin();
	// Sources dont la portée atteint le chunk.
	std::vector<lighting::Source> sources;
	const math::FVector3 lo = origin.ToVector(), hi = lo + math::FVector3{float(N), float(N), float(N)};
	lighting::GatherSources(world, lo, hi, sources);
	// Part de ciel de chaque colonne au sommet du chunk : transmissions des
	// cellules au-dessus (chunks chargés), sinon le relief estimé.
	std::vector<float> top(size_t(N * N), 1.f);
	if (sky)
		for (int z = 0; z < N; ++z)
			for (int x = 0; x < N; ++x) {
				float t = 1.f;
				for (int dy = 1; dy <= SKY_SCAN && t > 1e-3f; ++dy) {
					const BlockPos cell{origin.x + x, origin.y + N - 1 + dy, origin.z + z};
					const Chunk *above = world.GetChunk(ChunkPos{pos.x, (cell.y >> CHUNK_SHIFT), pos.z});
					if (!above) {
						if (ChunkGenerator *generator = world.Generator())
							if (Option<int> cover = generator->SkyCover(cell.x, cell.z); cover.IsSome() && cell.y <= cover.Unwrap())
								t = 0.f;
						break;
					}
					if (above->IsUniform() && above->UniformBlock() == AIR) {
						dy += N - 1 - (cell.y & (N - 1)); // le reste de ce chunk vide
						continue;
					}
					t *= SkyTransmission(content, above->Get(cell.x & (N - 1), cell.y & (N - 1), cell.z & (N - 1)));
				}
				top[size_t(z * N + x)] = t;
			}
	for (int z = 0; z < N; ++z)
		for (int x = 0; x < N; ++x) {
			float t = top[size_t(z * N + x)];
			for (int y = N - 1; y >= 0; --y) {
				const BlockId id = chunk->Get(x, y, z);
				uint16_t *c = texel(x, y, z);
				const BlockDef &def = content.Block(id);
				if (StopsRays(def)) {
					c[0] = uint16_t(id);
					c[1] = 0;
				} else {
					c[1] = uint16_t(std::clamp(int(std::lround(t * 255.f)), 0, 255));
					if (!sources.empty()) {
						const math::FVector3 centre{float(origin.x + x) + 0.5f, float(origin.y + y) + 0.5f, float(origin.z + z) + 0.5f};
						lighting::Rgb sum{0.f, 0.f, 0.f};
						for (const lighting::Source &s : sources) {
							math::FVector3 toSource;
							const lighting::Rgb e = lighting::Unoccluded(s, centre, {0.f, 0.f, 0.f}, toSource);
							for (int k = 0; k < 3; ++k)
								sum[size_t(k)] += e[size_t(k)];
						}
						c[2] = uint16_t(Encode(sum[0]) | (Encode(sum[1]) << 8));
						c[3] = Encode(sum[2]);
					}
				}
				if (sky)
					t *= SkyTransmission(content, id);
			}
		}
	return true;
}

} // namespace voxel::raygrid
