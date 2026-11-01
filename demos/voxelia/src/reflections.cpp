// Définitions de voxelia/reflections.hpp
#include <voxelia/reflections.hpp>

#include <algorithm>
#include <cmath>

#include <render3d/texture_loader.hpp>
#include <voxel/raygrid.hpp>

namespace voxelia {

using namespace voxel;

namespace {

constexpr int SIZE_X = ReflectionVolume::CHUNKS_X * CHUNK_SIZE, SIZE_Y = ReflectionVolume::CHUNKS_Y * CHUNK_SIZE,
			  SIZE_Z = ReflectionVolume::CHUNKS_Z * CHUNK_SIZE;

/// Reste positif (les chunks sous l'origine ont des coordonnées négatives).
int Wrap(int v, int n) { return ((v % n) + n) % n; }

} // namespace

Result<bool, String> ReflectionVolume::Initialize(const TextureAtlas &atlas, const ChunkMesher &mesher) {
	m_canvas.SetVoxelReflections(NONE);
	m_grid = NONE;
	m_blocks = NONE;
	m_reflectivity = NONE;
	Reset();
	const std::vector<uint8_t> &map = atlas.Reflectivity();
	if (map.empty())
		return Ok(true); // aucune texture ne réfléchit
	sdl3::GpuDevice &device = m_canvas.Device();
	const Image &pixels = atlas.Pixels();
	auto reflectivity = render3d::CreateDataTexture(device, SDL_GPU_TEXTUREFORMAT_R8_UNORM, uint32_t(pixels.width), uint32_t(pixels.height));
	if (reflectivity.IsError())
		return Err(reflectivity.Error());
	const render3d::TextureRegionData mapRegion{0, 0, 0, uint32_t(pixels.width), uint32_t(pixels.height), 1, map};
	if (auto sent = render3d::UploadTextureRegions(device, reflectivity.Value(), {&mapRegion, 1}); sent.IsError())
		return Err(sent.Error());
	m_mesher = &mesher;
	if (auto table = UploadBlockTable(nullptr); table.IsError())
		return Err(table.Error());
	auto grid = render3d::CreateDataTexture(device, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT, SIZE_X, SIZE_Y, SIZE_Z);
	if (grid.IsError())
		return Err(grid.Error());
	m_reflectivity = Some(std::move(reflectivity).Unwrap());
	m_grid = Some(std::move(grid).Unwrap());
	return Ok(true);
}

Result<bool, String> ReflectionVolume::UploadBlockTable(const BiomeDef *biome) {
	int rows = 0;
	const std::vector<float> table = m_mesher->RayBlockTable(m_blocksPerRow, biome, rows);
	if (m_blocks.IsNone()) {
		auto blocks =
			render3d::CreateDataTexture(m_canvas.Device(), SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, uint32_t(m_blocksPerRow), uint32_t(rows));
		if (blocks.IsError())
			return Err(blocks.Error());
		m_blocks = Some(std::move(blocks).Unwrap());
	}
	const render3d::TextureRegionData region{
		0, 0, 0, uint32_t(m_blocksPerRow), uint32_t(rows), 1, {reinterpret_cast<const uint8_t *>(table.data()), table.size() * sizeof(float)}};
	if (auto sent = render3d::UploadTextureRegions(m_canvas.Device(), m_blocks.Value(), {&region, 1}); sent.IsError())
		return Err(sent.Error());
	m_biome = biome;
	return Ok(true);
}

void ReflectionVolume::Reset() {
	m_slots.assign(size_t(CHUNKS_X * CHUNKS_Y * CHUNKS_Z), Slot{});
	m_pending = 0;
}

ReflectionVolume::Slot &ReflectionVolume::SlotOf(const ChunkPos &p) {
	return m_slots[size_t((Wrap(p.z, CHUNKS_Z) * CHUNKS_Y + Wrap(p.y, CHUNKS_Y)) * CHUNKS_X + Wrap(p.x, CHUNKS_X))];
}

void ReflectionVolume::Update(const World &world, bool sky, const math::FVector3 &camera, bool enabled, int maxBounces,
							  const math::FVector3 &skyColor, const BiomeDef *biome, size_t budget) {
	if (!enabled || m_grid.IsNone()) {
		m_canvas.SetVoxelReflections(NONE);
		m_pending = 0;
		return;
	}
	if (biome != m_biome)
		(void)UploadBlockTable(biome); // en échec : les teintes précédentes restent
	// Grille centrée sur le chunk de la caméra.
	const ChunkPos centre{int(std::floor(camera.x)) >> CHUNK_SHIFT, int(std::floor(camera.y)) >> CHUNK_SHIFT,
						  int(std::floor(camera.z)) >> CHUNK_SHIFT};
	const ChunkPos low{centre.x - CHUNKS_X / 2, centre.y - CHUNKS_Y / 2, centre.z - CHUNKS_Z / 2};
	std::vector<std::pair<int, ChunkPos>> stale;
	for (int z = 0; z < CHUNKS_Z; ++z)
		for (int y = 0; y < CHUNKS_Y; ++y)
			for (int x = 0; x < CHUNKS_X; ++x) {
				const ChunkPos p{low.x + x, low.y + y, low.z + z};
				const Slot &slot = SlotOf(p);
				const Chunk *chunk = world.GetChunk(p);
				const bool current = slot.filled && slot.chunk == p && slot.loaded == (chunk != nullptr) &&
									 (!chunk || slot.revision == chunk->Revision());
				if (!current) {
					const int dx = p.x - centre.x, dy = p.y - centre.y, dz = p.z - centre.z;
					stale.push_back({dx * dx + dy * dy + dz * dz, p});
				}
			}
	std::sort(stale.begin(), stale.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	m_pending = stale.size();
	const size_t count = std::min(stale.size(), budget);
	if (count > 0) {
		std::vector<std::vector<uint16_t>> cells(count, std::vector<uint16_t>(raygrid::CHUNK_TEXELS));
		std::vector<render3d::TextureRegionData> regions;
		for (size_t i = 0; i < count; ++i) {
			const ChunkPos p = stale[i].second;
			const Chunk *chunk = world.GetChunk(p);
			(void)raygrid::FillChunk(world, p, sky, cells[i]);
			Slot &slot = SlotOf(p);
			slot = Slot{p, chunk ? chunk->Revision() : 0, true, chunk != nullptr};
			regions.push_back(render3d::TextureRegionData{
				uint32_t(Wrap(p.x, CHUNKS_X) * CHUNK_SIZE), uint32_t(Wrap(p.y, CHUNKS_Y) * CHUNK_SIZE),
				uint32_t(Wrap(p.z, CHUNKS_Z) * CHUNK_SIZE), CHUNK_SIZE, CHUNK_SIZE, CHUNK_SIZE,
				{reinterpret_cast<const uint8_t *>(cells[i].data()), cells[i].size() * sizeof(uint16_t)}});
		}
		if (render3d::UploadTextureRegions(m_canvas.Device(), m_grid.Value(), regions).IsOk()) {
			m_uploads += count;
			m_pending -= count;
		} else {
			for (size_t i = 0; i < count; ++i)
				SlotOf(stale[i].second).filled = false;
		}
	}
	render3d::VoxelReflections v{MakeRef(m_grid.Value()), MakeRef(m_blocks.Value()), MakeRef(m_reflectivity.Value())};
	v.origin = low.Origin().ToVector();
	v.size = {float(SIZE_X), float(SIZE_Y), float(SIZE_Z)};
	v.maxBounces = std::clamp(maxBounces, 1, 16);
	v.maxDistance = 96.f;
	v.blocksPerRow = m_blocksPerRow;
	v.skyColor = skyColor;
	m_canvas.SetVoxelReflections(Some(v));
}

} // namespace voxelia
