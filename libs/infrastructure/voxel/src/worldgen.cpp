// Définitions de voxel/worldgen.hpp
#include <voxel/json.hpp>
#include <voxel/worldgen.hpp>

#include <algorithm>
#include <cmath>

namespace voxel {

uint64_t HashPosition(uint32_t seed, int x, int y, int z, uint32_t salt) noexcept {
	uint64_t h = uint64_t(seed) * 0x9E3779B97F4A7C15ull ^ uint64_t(uint32_t(x)) * 0xBF58476D1CE4E5B9ull ^
				 uint64_t(uint32_t(y)) * 0x94D049BB133111EBull ^ uint64_t(uint32_t(z)) * 0xD6E8FEB86659FD93ull ^
				 uint64_t(salt) * 0xA0761D6478BD642Full;
	h ^= h >> 30;
	h *= 0xBF58476D1CE4E5B9ull;
	h ^= h >> 27;
	h *= 0x94D049BB133111EBull;
	h ^= h >> 31;
	return h;
}

float RandomAt(uint32_t seed, int x, int y, int z, uint32_t salt) noexcept {
	return float(HashPosition(seed, x, y, z, salt) >> 40) / float(1ull << 24);
}

namespace {

generators::Noise MakeNoise(uint32_t seed, float frequency, int octaves, float gain = 0.5f) {
	generators::NoiseSettings s;
	s.type = generators::NoiseType::SIMPLEX;
	s.fractal = octaves > 1 ? generators::FractalType::FBM : generators::FractalType::NONE;
	s.seed = seed;
	s.frequency = frequency;
	s.octaves = std::max(1, octaves);
	s.gain = gain;
	return generators::Noise(s);
}

} // namespace

TerrainGenerator::TerrainGenerator(const ContentRegistry &content, const GeneratorDef &def, uint32_t seed, ScriptFn script)
	: m_content(content), m_def(def), m_seed(seed), m_script(std::move(script)),
	  m_height(MakeNoise(seed, def.frequency, def.octaves)), m_detail(MakeNoise(seed + 7, def.frequency * 4.f, 2)),
	  m_temperature(MakeNoise(seed + 101, def.frequency * 0.25f, 2)), m_humidity(MakeNoise(seed + 202, def.frequency * 0.25f, 2)),
	  m_caves(MakeNoise(seed + 303, 0.045f, 2)) {
	for (const BiomeDef *biome : content.Biomes())
		if (def.biomes.empty() || std::find(def.biomes.begin(), def.biomes.end(), biome->id) != def.biomes.end())
			m_biomes.push_back(biome);
	m_fluid = content.BlockIdOf(def.fluid);
	m_bedrock = content.BlockIdOf(def.bedrock);
	m_bedrockLevel = json::Int(def.raw, "bedrockLevel", -64);
	if (NodePtr ores = json::At(def.raw, "ores"); ores && ores->IsArray())
		for (size_t i = 0; i < ores->GetSize(); ++i) {
			NodePtr o = ores->At(i);
			Ore ore;
			ore.block = content.BlockIdOf(ContentRegistry::Qualify(json::Str(o, "block"), def.mod));
			ore.in = content.BlockIdOf(ContentRegistry::Qualify(json::Str(o, "in"), def.mod));
			ore.chance = float(json::Num(o, "chance", 0.01));
			ore.minY = json::Int(o, "minY", -1000);
			ore.maxY = json::Int(o, "maxY", 1000);
			ore.size = std::max(1, json::Int(o, "size", 4));
			if (ore.block != AIR)
				m_ores.push_back(ore);
		}
}

TerrainGenerator::Column TerrainGenerator::ColumnAt(int x, int z) const {
	if (m_biomes.empty())
		return {m_def.seaLevel, nullptr};
	const float t = m_temperature.Sample01(float(x), float(z));
	const float h = m_humidity.Sample01(float(x), float(z));
	// Biome : le plus proche dans le diagramme climatique ; hauteur : mélange
	// pondéré (poids ∝ 1/d⁴) des biomes proches, pour des transitions douces.
	const BiomeDef *best = nullptr;
	float bestDistance = 1e30f, weightSum = 0.f, base = 0.f, variation = 0.f;
	for (const BiomeDef *b : m_biomes) {
		const float dt = t - b->temperature, dh = h - b->humidity;
		const float d = dt * dt + dh * dh;
		if (d < bestDistance) {
			bestDistance = d;
			best = b;
		}
		const float w = 1.f / (d * d + 1e-6f);
		weightSum += w;
		base += b->heightBase * w;
		variation += b->heightVariation * w;
	}
	base /= weightSum;
	variation /= weightSum;
	const float n = m_height.Sample(float(x), float(z));
	const float detail = m_detail.Sample(float(x), float(z)) * (best ? best->roughness : 0.5f);
	return {int(std::floor(base + n * variation * m_tuning.relief + detail * 2.f)), best};
}

int TerrainGenerator::SurfaceHeight(int x, int z) const {
	if (m_def.kind == "flat") {
		int h = 0;
		for (const auto &[block, n] : m_def.layers)
			h += n;
		return h - 1;
	}
	return ColumnAt(x, z).height;
}

const BiomeDef *TerrainGenerator::BiomeAt(int x, int z) const {
	return m_def.kind == "flat" ? (m_biomes.empty() ? nullptr : m_biomes.front()) : ColumnAt(x, z).biome;
}

void TerrainGenerator::Generate(Chunk &chunk, World &world) {
	if (m_def.kind == "flat")
		GenerateFlat(chunk);
	else if (m_def.kind == "script" && m_script)
		m_script(chunk, world, m_def);
	else
		GenerateNoise(chunk, world);
}

void TerrainGenerator::GenerateFlat(Chunk &chunk) {
	const BlockPos origin = chunk.Pos().Origin();
	for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
		const int y = origin.y + ly;
		BlockId id = AIR;
		int top = 0;
		for (const auto &[block, n] : m_def.layers) {
			if (y >= top && y < top + n)
				id = m_content.BlockIdOf(block);
			top += n;
		}
		if (id == AIR)
			continue;
		for (int lz = 0; lz < CHUNK_SIZE; ++lz)
			for (int lx = 0; lx < CHUNK_SIZE; ++lx)
				chunk.Set(lx, ly, lz, id);
	}
}

void TerrainGenerator::GenerateNoise(Chunk &chunk, World &world) {
	const BlockPos origin = chunk.Pos().Origin();
	Column columns[CHUNK_SIZE][CHUNK_SIZE];
	int maxHeight = -1 << 30;
	for (int lz = 0; lz < CHUNK_SIZE; ++lz)
		for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
			columns[lz][lx] = ColumnAt(origin.x + lx, origin.z + lz);
			maxHeight = std::max(maxHeight, columns[lz][lx].height);
		}
	// Chunk entièrement au-dessus du relief et de la mer : rien à faire
	// (sinon structures débordant des voisins, déjà en attente).
	if (origin.y > maxHeight + 1 && origin.y > m_def.seaLevel) {
		for (int lz = 0; lz < CHUNK_SIZE; ++lz)
			for (int lx = 0; lx < CHUNK_SIZE; ++lx)
				Decorate(chunk, world, origin.x + lx, origin.z + lz, columns[lz][lx]);
		return;
	}
	for (int lz = 0; lz < CHUNK_SIZE; ++lz)
		for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
			const Column &col = columns[lz][lx];
			const BiomeDef *biome = col.biome;
			const BlockId surface = biome ? m_content.BlockIdOf(biome->surface) : AIR;
			const BlockId subsurface = biome ? m_content.BlockIdOf(biome->subsurface) : AIR;
			const BlockId filler = biome ? m_content.BlockIdOf(biome->filler) : AIR;
			const BlockId underwater = biome && !biome->underwater.IsEmpty() ? m_content.BlockIdOf(biome->underwater) : subsurface;
			const int depth = biome ? biome->subsurfaceDepth : 3;
			const int x = origin.x + lx, z = origin.z + lz;
			for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
				const int y = origin.y + ly;
				BlockId id = AIR;
				if (y <= col.height) {
					if (y == col.height)
						id = col.height < m_def.seaLevel ? underwater : surface;
					else if (y > col.height - depth)
						id = subsurface;
					else
						id = filler;
					if (id == AIR)
						id = filler;
					// Grottes : bruit 3D, jamais à fleur d'eau.
					if (m_def.caves > 0.f && y < col.height - 1 && !(col.height <= m_def.seaLevel && y > col.height - 4)) {
						const float c = m_caves.Sample(float(x), float(y) * 1.6f, float(z));
						if (std::fabs(c) < m_def.caves * m_tuning.caves * 0.18f)
							id = AIR;
					}
					// Minerais dans le remplissage.
					for (const Ore &ore : m_ores)
						if (id == ore.in && y >= ore.minY && y <= ore.maxY && RandomAt(m_seed, x, y, z, 17) < ore.chance * m_tuning.ores)
							id = ore.block;
				} else if (y <= m_def.seaLevel) {
					id = m_fluid;
				}
				if (m_bedrock != AIR && y <= m_bedrockLevel)
					id = m_bedrock;
				if (id != AIR)
					chunk.Set(lx, ly, lz, id);
			}
		}
	for (int lz = 0; lz < CHUNK_SIZE; ++lz)
		for (int lx = 0; lx < CHUNK_SIZE; ++lx)
			Decorate(chunk, world, origin.x + lx, origin.z + lz, columns[lz][lx]);
}

void TerrainGenerator::Decorate(Chunk &chunk, World &world, int x, int z, const Column &col) {
	// La décoration appartient au chunk qui contient le bloc AU-DESSUS de
	// la surface : une seule fois par colonne, quel que soit l'ordre.
	const BlockPos origin = chunk.Pos().Origin();
	const int y = col.height + 1;
	if (!col.biome || y < origin.y || y >= origin.y + CHUNK_SIZE || col.height < m_def.seaLevel)
		return;
	const BlockId surface = m_content.BlockIdOf(col.biome->surface);
	uint32_t salt = 1000;
	for (const Decoration &deco : col.biome->decorations) {
		++salt;
		if (RandomAt(m_seed, x, 0, z, salt) >= deco.chance * m_tuning.vegetation)
			continue;
		// Structures « bâties » (sans tronc : ni arbre ni pic) désactivables.
		if (!m_tuning.structures && !deco.structure.IsEmpty() && !deco.structure.EndsWith("_tree") && !deco.structure.Contains("_tree_") &&
			!deco.structure.Contains("oak") && !deco.structure.Contains("spike"))
			continue;
		if (!deco.onBlocks.empty() &&
			std::none_of(deco.onBlocks.begin(), deco.onBlocks.end(), [&](const String &b) { return m_content.BlockIdOf(b) == surface; }))
			continue;
		if (!deco.structure.IsEmpty()) {
			if (const StructureDef *structure = m_content.FindStructure(deco.structure))
				world.PlaceStructure(*structure, {x, y, z}, int(HashPosition(m_seed, x, y, z, salt) % 4));
		} else if (!deco.block.IsEmpty()) {
			world.GenerationWrite({x, y, z}, m_content.BlockIdOf(deco.block), false);
		}
		return; // une décoration par colonne
	}
	(void)chunk;
}

void TerrainGenerator::SetTuning(const GenerationTuning &tuning) {
	m_def.seaLevel += tuning.seaLevel - m_tuning.seaLevel;
	m_tuning = tuning;
}

std::shared_ptr<TerrainGenerator> MakeGenerator(const ContentRegistry &content, const String &generatorId, uint32_t seed,
												TerrainGenerator::ScriptFn script) {
	const GeneratorDef *def = generatorId.IsEmpty() ? nullptr : content.FindGenerator(generatorId);
	if (!def && !content.Generators().empty())
		def = content.Generators().front();
	if (def)
		return std::make_shared<TerrainGenerator>(content, *def, seed, std::move(script));
	// Secours : monde plat d'un seul bloc solide, s'il en existe.
	GeneratorDef flat;
	flat.id = "voxel:fallback_flat";
	flat.kind = "flat";
	for (const BlockDef &b : content.Blocks())
		if (b.solid && !b.IsAir()) {
			flat.layers.push_back({b.id, 4});
			break;
		}
	return std::make_shared<TerrainGenerator>(content, flat, seed, std::move(script));
}

} // namespace voxel
