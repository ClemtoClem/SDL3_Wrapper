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
	// Sans liste : les biomes du monde ordinaire (ceux d'une dimension restent
	// chez elle).
	for (const BiomeDef *biome : content.Biomes())
		if (def.biomes.empty() ? biome->dimension.IsEmpty()
							   : std::find(def.biomes.begin(), def.biomes.end(), biome->id) != def.biomes.end())
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
	if (m_def.kind == "islands")
		SetupIslands();
	else if (m_def.kind == "caverns")
		SetupCaverns();
	else if (m_def.kind == "maze")
		SetupMaze();
}

const BiomeDef *TerrainGenerator::ClimateBiome(const std::vector<const BiomeDef *> &biomes, int x, int z) const {
	if (biomes.empty())
		return nullptr;
	const float t = m_temperature.Sample01(float(x), float(z));
	const float h = m_humidity.Sample01(float(x), float(z));
	const BiomeDef *best = nullptr;
	float bestDistance = 1e30f;
	for (const BiomeDef *b : biomes) {
		const float dt = t - b->temperature, dh = h - b->humidity;
		if (const float d = dt * dt + dh * dh; d < bestDistance) {
			bestDistance = d;
			best = b;
		}
	}
	return best;
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
	if (m_def.kind == "islands") {
		const IslandColumn c = IslandAt(m_mainIslands, m_islandNoise, x, z);
		return c.present ? c.top : int(m_mainIslands.base);
	}
	if (m_def.kind == "caverns")
		return m_cavernFloor + 8;
	if (m_def.kind == "maze")
		return m_mazeFloor;
	if (m_def.kind == "flat") {
		int h = 0;
		for (const auto &[block, n] : m_def.layers)
			h += n;
		return h - 1;
	}
	return ColumnAt(x, z).height;
}

const BiomeDef *TerrainGenerator::BiomeAt(int x, int z) const {
	if (m_def.kind == "maze")
		return m_levels.empty() ? nullptr : MazeLevelAt(x, z).biome;
	if (m_def.kind == "islands" || m_def.kind == "caverns")
		return ClimateBiome(x, z);
	return m_def.kind == "flat" ? (m_biomes.empty() ? nullptr : m_biomes.front()) : ColumnAt(x, z).biome;
}

Option<int> TerrainGenerator::SkyCover(int x, int z) const {
	if (m_def.kind == "caverns")
		return Some(m_cavernRoof + 48); // la voûte du monde souterrain
	if (m_def.kind == "maze")
		return Some(m_mazeFloor + m_mazeHeight + 6);
	if (m_def.kind == "noise" || m_def.kind == "flat")
		return Some(SurfaceHeight(x, z));
	return NONE; // îles, script : ciel ouvert
}

void TerrainGenerator::Generate(Chunk &chunk, World &world) {
	if (m_def.kind == "flat")
		GenerateFlat(chunk);
	else if (m_def.kind == "script" && m_script)
		m_script(chunk, world, m_def);
	else if (m_def.kind == "islands")
		GenerateIslands(chunk, world);
	else if (m_def.kind == "caverns")
		GenerateCaverns(chunk, world);
	else if (m_def.kind == "maze")
		GenerateMaze(chunk, world);
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
		if (json::Str(deco.params, "where") == "ceiling")
			continue;
		if (PlaceDecoration(world, deco, *col.biome, x, y, z, salt, false, surface))
			return; // une décoration par colonne
	}
}

bool TerrainGenerator::PlaceDecoration(World &world, const Decoration &deco, const BiomeDef &biome, int x, int y, int z,
									   uint32_t salt, bool ceiling, BlockId surface) {
	if (RandomAt(m_seed, x, ceiling ? y : 0, z, salt) >= deco.chance * m_tuning.vegetation)
		return false;
	// Structures « bâties » (sans tronc : ni arbre ni pic) désactivables.
	if (!m_tuning.structures && !deco.structure.IsEmpty() && !deco.structure.EndsWith("_tree") && !deco.structure.Contains("_tree_") &&
		!deco.structure.Contains("oak") && !deco.structure.Contains("spike"))
		return false;
	if (!m_tuning.structures && (deco.feature == "spire" || deco.feature == "giant_tree") && !json::Bool(deco.params, "natural", false))
		return false;
	if (!deco.onBlocks.empty() &&
		std::none_of(deco.onBlocks.begin(), deco.onBlocks.end(), [&](const String &b) { return m_content.BlockIdOf(b) == surface; }))
		return false;
	if (!deco.feature.IsEmpty()) {
		if (deco.feature == "giant_tree")
			FeatureGiantTree(world, deco.params, biome.mod, x, y, z, salt);
		else if (deco.feature == "spire")
			FeatureSpire(world, deco.params, biome.mod, x, y, z, salt, ceiling);
		else if (deco.feature == "patch")
			FeaturePatch(world, deco.params, biome.mod, x, y, z, salt, ceiling);
		else if (deco.feature == "cluster")
			FeatureCluster(world, deco.params, biome.mod, x, y, z, salt, ceiling);
		else
			return false;
	} else if (!deco.structure.IsEmpty()) {
		const StructureDef *structure = m_content.FindStructure(deco.structure);
		if (!structure)
			return false;
		world.PlaceStructure(*structure, {x, y, z}, int(HashPosition(m_seed, x, y, z, salt) % 4));
	} else if (!deco.block.IsEmpty()) {
		world.GenerationWrite({x, y, z}, m_content.BlockIdOf(deco.block), false);
	} else {
		return false;
	}
	return true;
}

void TerrainGenerator::SetTuning(const GenerationTuning &tuning) {
	m_def.seaLevel += tuning.seaLevel - m_tuning.seaLevel;
	m_tuning = tuning;
}

// ── Paramètres des générateurs et éléments procéduraux ───────────────────────

namespace {

/// Intervalle « [min, max] » (ou nombre seul) d'un paramètre.
std::pair<float, float> RangeParam(const NodePtr &params, const char *key, float min, float max) {
	NodePtr v = json::At(params, key);
	if (v && v->IsArray() && v->GetSize() >= 2)
		return {float(json::Num(v->At(0), "", min)), float(json::Num(v->At(1), "", max))};
	if (v)
		return {float(json::Num(params, key, min)), float(json::Num(params, key, min))};
	return {min, max};
}

/// Valeur tirée dans un intervalle, déterministe.
float Pick(const std::pair<float, float> &range, float t) { return range.first + (range.second - range.first) * t; }

BlockId BlockParam(const ContentRegistry &content, const NodePtr &params, const char *key, const String &mod) {
	const String id = json::Str(params, key);
	return id.IsEmpty() ? AIR : content.BlockIdOf(ContentRegistry::Qualify(id, mod));
}

} // namespace

// ── "islands" ────────────────────────────────────────────────────────────────

void TerrainGenerator::SetupIslands() {
	auto layer = [&](const char *key, bool mirrored) {
		IslandLayer l;
		NodePtr p = json::At(m_def.raw, key);
		l.enabled = p && p->IsObject();
		l.mirrored = mirrored;
		l.base = float(json::Num(p, "base", mirrored ? 150.0 : 80.0));
		l.spread = float(json::Num(p, "spread", 20.0));
		l.threshold = float(json::Num(p, "threshold", 0.35));
		l.height = float(json::Num(p, "height", 6.0));
		l.thickness = float(json::Num(p, "thickness", 18.0));
		for (const String &id : json::StrList(p, "biomes"))
			if (const BiomeDef *b = m_content.FindBiome(ContentRegistry::Qualify(id, m_def.mod)))
				l.biomes.push_back(b);
		return l;
	};
	m_mainIslands = layer("islands", false);
	m_mainIslands.enabled = true;
	m_mirrorIslands = layer("mirror", true);
	const float frequency = float(json::Num(m_def.raw, "islands.frequency", m_def.frequency));
	m_islandNoise = MakeNoise(m_seed + 401, frequency, 4);
	m_mirrorNoise = MakeNoise(m_seed + 402, float(json::Num(m_def.raw, "mirror.frequency", frequency)), 4);
	m_islandOffset = MakeNoise(m_seed + 403, frequency * 0.5f, 2);
	m_fragments = float(json::Num(m_def.raw, "fragments.density", 0.0));
	m_fragmentMin = json::Int(m_def.raw, "fragments.minY", 40);
	m_fragmentMax = json::Int(m_def.raw, "fragments.maxY", 140);
	m_fragmentBlock = BlockParam(m_content, m_def.raw, "fragments.block", m_def.mod);
	m_fragmentNoise = MakeNoise(m_seed + 404, float(json::Num(m_def.raw, "fragments.frequency", 0.06)), 2);
}

TerrainGenerator::IslandColumn TerrainGenerator::IslandAt(const IslandLayer &layer, const generators::Noise &noise, int x, int z) const {
	IslandColumn c;
	if (!layer.enabled)
		return c;
	const float n = noise.Sample01(float(x), float(z));
	if (n <= layer.threshold)
		return c;
	// k : 0 au bord de l'île, 1 en son cœur. Dôme au-dessus, cône en dessous.
	const float k = std::min(1.f, (n - layer.threshold) / std::max(0.05f, 1.f - layer.threshold) * 2.2f);
	const float center = layer.base + m_islandOffset.Sample(float(x), float(z)) * layer.spread;
	const float detail = m_detail.Sample(float(x), float(z)) * 1.5f;
	const int dome = int(std::floor(std::sqrt(k) * layer.height * m_tuning.relief + detail));
	const int depth = 1 + int(std::floor(k * k * layer.thickness));
	if (!layer.mirrored) {
		c.top = int(std::floor(center)) + dome;
		c.bottom = c.top - depth;
	} else { // surface DESSOUS : le dôme pend vers le bas
		c.bottom = int(std::floor(center)) - dome;
		c.top = c.bottom + depth;
	}
	c.present = c.top >= c.bottom;
	return c;
}

void TerrainGenerator::GenerateIslands(Chunk &chunk, World &world) {
	const BlockPos origin = chunk.Pos().Origin();
	for (int lz = 0; lz < CHUNK_SIZE; ++lz)
		for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
			const int x = origin.x + lx, z = origin.z + lz;
			for (const IslandLayer *layer : {&m_mainIslands, &m_mirrorIslands}) {
				const IslandColumn c = IslandAt(*layer, layer->mirrored ? m_mirrorNoise : m_islandNoise, x, z);
				if (!c.present || c.bottom > origin.y + CHUNK_SIZE - 1 + 1 || c.top < origin.y - 1)
					continue;
				const BiomeDef *biome = layer->biomes.empty() ? ClimateBiome(x, z) : ClimateBiome(layer->biomes, x, z);
				const BlockId surface = biome ? m_content.BlockIdOf(biome->surface) : AIR;
				const BlockId subsurface = biome ? m_content.BlockIdOf(biome->subsurface) : AIR;
				const BlockId filler = biome ? m_content.BlockIdOf(biome->filler) : AIR;
				const BlockId under = biome && !biome->underwater.IsEmpty() ? m_content.BlockIdOf(biome->underwater) : filler;
				const int depth = biome ? biome->subsurfaceDepth : 3;
				for (int y = std::max(c.bottom, origin.y); y <= std::min(c.top, origin.y + CHUNK_SIZE - 1); ++y) {
					// Distance à la face « habitable » (dessus, ou dessous si retournée).
					const int fromSurface = layer->mirrored ? y - c.bottom : c.top - y;
					BlockId id = fromSurface == 0 ? surface : fromSurface < depth ? subsurface : filler;
					if (!layer->mirrored && y == c.bottom && c.top - c.bottom > 2)
						id = under; // dessous rocheux
					if (layer->mirrored && y == c.top && c.top - c.bottom > 2)
						id = under;
					if (id == AIR)
						id = filler;
					for (const Ore &ore : m_ores)
						if (id == ore.in && y >= ore.minY && y <= ore.maxY && RandomAt(m_seed, x, y, z, 17) < ore.chance * m_tuning.ores)
							id = ore.block;
					if (id != AIR)
						chunk.Set(lx, y - origin.y, lz, id);
				}
				if (!biome)
					continue;
				// Décorations : sur la face habitable ; « ceiling » : pendues de l'autre côté.
				const int above = layer->mirrored ? c.bottom - 1 : c.top + 1;
				const int below = layer->mirrored ? c.top + 1 : c.bottom - 1;
				uint32_t salt = layer->mirrored ? 3000 : 1000;
				bool placedAbove = false, placedBelow = false;
				for (const Decoration &deco : biome->decorations) {
					++salt;
					const bool hanging = json::Str(deco.params, "where") == "ceiling";
					const int y = hanging ? below : above;
					if (y < origin.y || y >= origin.y + CHUNK_SIZE || (hanging ? placedBelow : placedAbove))
						continue;
					// Une île retournée : ses plantes poussent vers le bas.
					if (PlaceDecoration(world, deco, *biome, x, y, z, salt, hanging != layer->mirrored, surface))
						(hanging ? placedBelow : placedAbove) = true;
				}
			}
			// Débris en suspension entre les îles.
			if (m_fragments > 0.f && m_fragmentBlock != AIR)
				for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
					const int y = origin.y + ly;
					if (y < m_fragmentMin || y > m_fragmentMax || chunk.Get(lx, ly, lz) != AIR)
						continue;
					if (m_fragmentNoise.Sample(float(x), float(y) * 1.3f, float(z)) > 1.f - m_fragments * 0.6f)
						chunk.Set(lx, ly, lz, m_fragmentBlock);
				}
		}
}

// ── "caverns" ────────────────────────────────────────────────────────────────

void TerrainGenerator::SetupCaverns() {
	NodePtr p = json::At(m_def.raw, "caverns");
	m_cavernFloor = json::Int(p, "floor", -48);
	m_cavernRoof = json::Int(p, "roof", 64);
	m_cavernThreshold = float(json::Num(p, "threshold", 0.2));
	m_cavernVertical = float(json::Num(p, "verticalScale", 1.4));
	m_lavaLevel = json::Int(p, "lavaLevel", -1000);
	m_lava = BlockParam(m_content, p, "lava", m_def.mod);
	m_roofBlock = BlockParam(m_content, p, "roofBlock", m_def.mod);
	const float frequency = float(json::Num(p, "frequency", 0.022));
	m_cavernNoise = MakeNoise(m_seed + 501, frequency, 3);
	m_chamberNoise = MakeNoise(m_seed + 502, frequency * 0.35f, 2);
}

bool TerrainGenerator::CavernSolid(int x, int y, int z) const {
	if (y <= m_cavernFloor || y >= m_cavernRoof)
		return true;
	// Tunnels (bruit 3D) et salles immenses (bruit lent), resserrés près du
	// plancher et du toit du monde.
	const float tunnels = 1.f - std::fabs(m_cavernNoise.Sample(float(x), float(y) * m_cavernVertical, float(z)));
	const float chambers = m_chamberNoise.Sample(float(x), float(y) * 1.8f, float(z));
	const float span = float(m_cavernRoof - m_cavernFloor);
	const float h = (float(y - m_cavernFloor) / span) * 2.f - 1.f; // -1 en bas, 1 en haut
	const float edge = h > 0.f ? h * h * h * h * 0.6f : 0.f; // voûtes resserrées sous le toit
	const float open = std::max(tunnels - (1.f - m_cavernThreshold * m_tuning.caves), chambers - 0.35f) - edge;
	return open <= 0.f;
}

void TerrainGenerator::GenerateCaverns(Chunk &chunk, World &world) {
	const BlockPos origin = chunk.Pos().Origin();
	if (origin.y > m_cavernRoof + 1 || origin.y + CHUNK_SIZE < m_cavernFloor - 1) {
		if (origin.y > m_cavernRoof + 1 && origin.y < m_cavernRoof + 48 && m_roofBlock != AIR)
			chunk.Fill(m_roofBlock); // au-dessus du toit : roche massive
		return;
	}
	bool solid[CHUNK_SIZE][CHUNK_SIZE + 2][CHUNK_SIZE];
	for (int lz = 0; lz < CHUNK_SIZE; ++lz)
		for (int lx = 0; lx < CHUNK_SIZE; ++lx)
			for (int ly = -1; ly <= CHUNK_SIZE; ++ly)
				solid[lz][ly + 1][lx] = CavernSolid(origin.x + lx, origin.y + ly, origin.z + lz);
	for (int lz = 0; lz < CHUNK_SIZE; ++lz)
		for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
			const int x = origin.x + lx, z = origin.z + lz;
			const BiomeDef *biome = ClimateBiome(x, z);
			const BlockId floor = biome ? m_content.BlockIdOf(biome->surface) : AIR;
			const BlockId wall = biome ? m_content.BlockIdOf(biome->subsurface) : AIR;
			const BlockId filler = biome ? m_content.BlockIdOf(biome->filler) : AIR;
			const BlockId ceiling = biome && !biome->underwater.IsEmpty() ? m_content.BlockIdOf(biome->underwater) : wall;
			for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
				const int y = origin.y + ly;
				BlockId id = AIR;
				if (y <= m_cavernFloor || y >= m_cavernRoof) {
					id = m_bedrock != AIR ? m_bedrock : filler;
				} else if (solid[lz][ly + 1][lx]) {
					const bool airAbove = !solid[lz][ly + 2][lx], airBelow = !solid[lz][ly][lx];
					id = airAbove ? floor : airBelow ? ceiling : filler;
					if (id == AIR)
						id = filler;
					for (const Ore &ore : m_ores)
						if ((id == ore.in || (ore.in == AIR && id == filler)) && y >= ore.minY && y <= ore.maxY &&
							RandomAt(m_seed, x, y, z, 17) < ore.chance * m_tuning.ores)
							id = ore.block;
				} else if (y <= m_lavaLevel && m_lava != AIR) {
					id = m_lava;
				}
				if (id != AIR)
					chunk.Set(lx, ly, lz, id);
			}
			if (!biome)
				continue;
			// Décorations des sols (au-dessus) et des plafonds (en dessous).
			for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
				const int y = origin.y + ly;
				if (solid[lz][ly + 1][lx] || y <= m_lavaLevel)
					continue;
				const bool onFloor = solid[lz][ly][lx], underCeiling = solid[lz][ly + 2][lx];
				if (!onFloor && !underCeiling)
					continue;
				uint32_t salt = 2000;
				for (const Decoration &deco : biome->decorations) {
					++salt;
					const bool hanging = json::Str(deco.params, "where") == "ceiling";
					if ((hanging && !underCeiling) || (!hanging && !onFloor))
						continue;
					if (PlaceDecoration(world, deco, *biome, x, y, z, salt + uint32_t(y & 255) * 64u, hanging, hanging ? ceiling : floor))
						break;
				}
			}
		}
}

// ── "maze" ───────────────────────────────────────────────────────────────────

void TerrainGenerator::SetupMaze() {
	NodePtr p = json::At(m_def.raw, "maze");
	m_mazeFloor = json::Int(p, "floor", 0);
	m_mazeHeight = std::max(3, json::Int(p, "height", 5));
	m_mazeCell = std::max(3, json::Int(p, "cell", 7));
	m_mazeRegion = std::max(m_mazeCell * 2, json::Int(p, "region", 80));
	m_doorWidth = std::clamp(json::Int(p, "doorWidth", 2), 1, m_mazeCell - 2);
	m_poolDepth = std::clamp(json::Int(p, "poolDepth", 2), 1, 5);
	m_mazeOuter = BlockParam(m_content, p, "outer", m_def.mod);
	m_mazeExit = BlockParam(m_content, p, "exit.block", m_def.mod);
	m_mazeExitSign = BlockParam(m_content, p, "exit.sign", m_def.mod);
	m_exitChance = float(json::Num(p, "exit.chance", 0.003));
	m_mazeSignLeft = BlockParam(m_content, p, "exit.signLeft", m_def.mod);
	m_mazeSignRight = BlockParam(m_content, p, "exit.signRight", m_def.mod);
	m_mazeSignRange = std::clamp(json::Int(p, "exit.signRange", 2), 0, 8);
	m_mazeSignHeight = std::clamp(json::Int(p, "exit.signHeight", 3), 1, m_mazeHeight - 1);
	if (NodePtr levels = json::At(m_def.raw, "levels"); levels && levels->IsArray())
		for (size_t i = 0; i < levels->GetSize(); ++i) {
			NodePtr l = levels->At(i);
			MazeLevel level;
			level.id = json::Str(l, "id");
			level.weight = std::max(0.f, float(json::Num(l, "weight", 1.0)));
			level.floor = BlockParam(m_content, l, "floor", m_def.mod);
			level.wall = BlockParam(m_content, l, "wall", m_def.mod);
			level.ceiling = BlockParam(m_content, l, "ceiling", m_def.mod);
			level.light = BlockParam(m_content, l, "light", m_def.mod);
			level.pillar = BlockParam(m_content, l, "pillar", m_def.mod);
			level.water = BlockParam(m_content, l, "water", m_def.mod);
			level.trim = BlockParam(m_content, l, "trim", m_def.mod);
			level.lightEvery = std::max(2, json::Int(l, "lightEvery", 4));
			level.open = json::Bool(l, "open", false);
			level.wallChance = float(json::Num(l, "wallChance", 0.55));
			level.waterChance = float(json::Num(l, "waterChance", 0.0));
			level.darkChance = float(json::Num(l, "darkChance", 0.0));
			level.lightOff = BlockParam(m_content, l, "lightOff", m_def.mod);
			level.lightHalf = BlockParam(m_content, l, "lightHalf", m_def.mod);
			level.halfChance = float(json::Num(l, "halfChance", 0.0));
			level.lightAuto = BlockParam(m_content, l, "lightAuto", m_def.mod);
			level.autoChance = float(json::Num(l, "autoChance", 0.0));
			level.lightHanging = json::Bool(l, "lightHanging", false);
			level.vent = BlockParam(m_content, l, "vent", m_def.mod);
			level.ventChance = float(json::Num(l, "ventChance", 0.0));
			if (const String biome = json::Str(l, "biome"); !biome.IsEmpty())
				level.biome = m_content.FindBiome(ContentRegistry::Qualify(biome, m_def.mod));
			if (level.wall == AIR)
				level.wall = m_mazeOuter;
			m_levelWeight += level.weight;
			m_levels.push_back(level);
		}
	// Étages : un nombre ; la liste des niveaux de chaque étage ; ou
	// {"count": N, "levels": [...]} — N étages dont les niveaux suivent la
	// liste, répétée au besoin (N vient souvent d'un réglage du mod).
	m_floorLevels.clear();
	auto levelIndex = [this](const String &id) {
		for (size_t k = 0; k < m_levels.size(); ++k)
			if (m_levels[k].id == id)
				return int(k);
		return -1;
	};
	if (NodePtr floors = json::At(p, "floors"); floors && floors->IsArray()) {
		for (size_t i = 0; i < floors->GetSize(); ++i)
			m_floorLevels.push_back(levelIndex(json::Str(floors->At(i), "")));
		m_mazeFloors = std::clamp(int(m_floorLevels.size()), 1, 16);
	} else if (floors && floors->IsObject()) {
		m_mazeFloors = std::clamp(json::Int(floors, "count", 1), 1, 16);
		if (NodePtr levels = json::At(floors, "levels"); levels && levels->IsArray() && levels->GetSize() > 0)
			for (int k = 0; k < m_mazeFloors; ++k)
				m_floorLevels.push_back(levelIndex(json::Str(levels->At(size_t(k) % levels->GetSize()), "")));
	} else {
		m_mazeFloors = std::clamp(json::Int(p, "floors", 1), 1, 16);
	}
	m_stairBlock = BlockParam(m_content, p, "stairs.block", m_def.mod);
	m_stairFill = BlockParam(m_content, p, "stairs.fill", m_def.mod);
	m_stairChance = float(json::Num(p, "stairs.chance", 0.0));
	// Gaines d'ascenseur (blocs du mod « elevators », s'il est là) : « elevator »
	// (une) ou « elevators » (plusieurs, chacune son « id » et ses étages).
	m_elevators.clear();
	auto addElevator = [&](const NodePtr &e) {
		if (!e || !e->IsObject())
			return;
		MazeElevator el;
		el.id = json::Str(e, "id", "main");
		el.cx = json::Int(e->Get(String("cell")) ? e->Get(String("cell"))->At(0) : nullptr, "", 1);
		el.cz = json::Int(e->Get(String("cell")) ? e->Get(String("cell"))->At(1) : nullptr, "", 0);
		const String side = json::Str(e, "side", "s");
		if (side == "n") { el.dx = 0; el.dz = -1; el.ux = 1; el.uz = 0; el.doorParam = 0; }
		else if (side == "e") { el.dx = 1; el.dz = 0; el.ux = 0; el.uz = 1; el.doorParam = 1; }
		else if (side == "w") { el.dx = -1; el.dz = 0; el.ux = 0; el.uz = -1; el.doorParam = 1; }
		else { el.dx = 0; el.dz = 1; el.ux = -1; el.uz = 0; el.doorParam = 0; }
		el.wall = BlockParam(m_content, e, "blocks.wall", m_def.mod);
		el.rail = BlockParam(m_content, e, "blocks.rail", m_def.mod);
		el.motor = BlockParam(m_content, e, "blocks.motor", m_def.mod);
		el.door = BlockParam(m_content, e, "blocks.door", m_def.mod);
		el.doorTop = BlockParam(m_content, e, "blocks.doorTop", m_def.mod);
		el.frame = BlockParam(m_content, e, "blocks.frame", m_def.mod);
		el.call = BlockParam(m_content, e, "blocks.call", m_def.mod);
		el.display = BlockParam(m_content, e, "blocks.display", m_def.mod);
		el.arrow = BlockParam(m_content, e, "blocks.arrow", m_def.mod);
		el.pit = std::clamp(json::Int(e, "pit", 6), 1, 16);
		el.overrun = std::clamp(json::Int(e, "overrun", 7), 1, 16);
		// Étages desservis : "all" (défaut), "first", "last", ou [de, à]
		// (indices négatifs : depuis le dernier, −1 étant le dernier).
		const int last = m_mazeFloors - 1;
		auto floorIndex = [last](int i) { return std::clamp(i < 0 ? last + 1 + i : i, 0, last); };
		el.from = 0;
		el.to = last;
		if (NodePtr f = json::At(e, "floors"); f && f->IsString()) {
			const String which = json::Str(f, "");
			if (which == "first")
				el.to = 0;
			else if (which == "last")
				el.from = last;
		} else if (f && f->IsArray() && f->GetSize() == 2) {
			el.from = floorIndex(json::Int(f->At(0), "", 0));
			el.to = std::max(el.from, floorIndex(json::Int(f->At(1), "", last)));
		}
		el.bottom = MazeFloorY(el.from) - el.pit;
		el.top = MazeFloorY(el.to) + 4 + el.overrun;
		// Sans les blocs (mod « elevators » absent), pas de gaine.
		if (el.wall != AIR && el.motor != AIR && el.door != AIR)
			m_elevators.push_back(el);
	};
	addElevator(json::At(p, "elevator"));
	if (NodePtr list = json::At(p, "elevators"); list && list->IsArray())
		for (size_t i = 0; i < list->GetSize(); ++i)
			addElevator(list->At(i));
	if (m_levels.empty()) { // un seul niveau, de pierre
		MazeLevel level;
		level.id = "default";
		level.floor = level.wall = level.ceiling = m_mazeOuter;
		m_levels.push_back(level);
		m_levelWeight = 1.f;
	}
}

const TerrainGenerator::MazeLevel &TerrainGenerator::MazeLevelAt(int x, int z, int floor) const {
	if (floor >= 0 && floor < int(m_floorLevels.size()) && m_floorLevels[size_t(floor)] >= 0)
		return m_levels[size_t(m_floorLevels[size_t(floor)])];
	// Régions carrées (bords décalés par du bruit) : chacune tire son niveau.
	const float wobble = m_detail.Sample(float(x), float(z)) * float(m_mazeCell);
	const int rx = int(std::floor((float(x) + wobble) / float(m_mazeRegion)));
	const int rz = int(std::floor((float(z) - wobble) / float(m_mazeRegion)));
	// Autour de l'origine : toujours le premier niveau (le « hall » d'arrivée).
	if (std::abs(x) < m_mazeRegion / 2 && std::abs(z) < m_mazeRegion / 2)
		return m_levels.front();
	float pick = RandomAt(m_seed, rx, 0, rz, 611) * m_levelWeight;
	for (const MazeLevel &level : m_levels) {
		if (pick < level.weight)
			return level;
		pick -= level.weight;
	}
	return m_levels.back();
}

bool TerrainGenerator::MazeWall(const MazeLevel &level, int x, int z, int floor) const {
	const uint32_t seed = m_seed + uint32_t(floor) * 7919u; // un plan par étage
	const int cell = m_mazeCell;
	const int cx = int(std::floor(float(x) / float(cell))), cz = int(std::floor(float(z) / float(cell)));
	const int lx = x - cx * cell, lz = z - cz * cell;
	if (level.open) // grande salle : piliers seulement
		return lx == 0 && lz == 0 && (cx & 1) == 0 && (cz & 1) == 0;
	if (lx == 0 && lz == 0)
		return true; // coins
	// Cellule murée (cul-de-sac) : rare.
	if (RandomAt(seed, cx, 1, cz, 613) < 0.04f)
		return true;
	auto segment = [&](int sx, int sz, uint32_t salt, int along) {
		if (RandomAt(seed, sx, 2, sz, salt) >= level.wallChance)
			return false;
		const int door = 1 + int(HashPosition(seed, sx, 3, sz, salt) % uint64_t(std::max(1, cell - 1 - m_doorWidth)));
		// Un mur sur deux garde une porte.
		const bool hasDoor = RandomAt(seed, sx, 4, sz, salt) < 0.75f;
		return !(hasDoor && along >= door && along < door + m_doorWidth);
	};
	if (lx == 0)
		return segment(cx, cz, 617, lz);
	if (lz == 0)
		return segment(cx, cz, 619, lx);
	return false;
}

Option<BlockPos> TerrainGenerator::MazeElevatorMotor(const String &id) const {
	if (m_def.kind != "maze")
		return NONE;
	for (const MazeElevator &e : m_elevators)
		if (id.IsEmpty() || e.id == id) // comme MazeElevatorColumn : au centre de la cellule, au-dessus de la réserve
			return Some(BlockPos{e.cx * m_mazeCell + m_mazeCell / 2, e.top + 1, e.cz * m_mazeCell + m_mazeCell / 2});
	return NONE;
}

const TerrainGenerator::MazeElevator *TerrainGenerator::MazeShaftAt(int cx, int cz) const {
	for (const MazeElevator &e : m_elevators)
		if (cx == e.cx && cz == e.cz)
			return &e;
	return nullptr;
}

bool TerrainGenerator::MazeReservedCell(int cx, int cz) const {
	for (const MazeElevator &e : m_elevators)
		if ((cx == e.cx && cz == e.cz) || (cx == e.cx + e.dx && cz == e.cz + e.dz))
			return true;
	return false;
}

bool TerrainGenerator::MazeStairCell(int cx, int cz, int floor) const {
	if (m_stairBlock == AIR || floor < 0 || floor + 1 >= m_mazeFloors || MazeReservedCell(cx, cz))
		return false;
	return RandomAt(m_seed, cx, 9 + floor, cz, 643) < m_stairChance;
}

bool TerrainGenerator::MazeColumnWall(int x, int z, int floor) const {
	const int cell = m_mazeCell;
	const int cx = int(std::floor(float(x) / float(cell))), cz = int(std::floor(float(z) / float(cell)));
	const int mx = x - cx * cell, mz = z - cz * cell;
	// Le hall devant une gaine : sa frontière avec la gaine reste ouverte.
	for (const MazeElevator &e : m_elevators) {
		const int fx = e.cx + e.dx, fz = e.cz + e.dz;
		if (cx == fx && cz == fz && ((e.dz > 0 && mz == 0 && mx >= 1) || (e.dx > 0 && mx == 0 && mz >= 1)))
			return false;
		if (cx == e.cx && cz == e.cz)
			return false; // la gaine s'en charge (et, hors de ses étages, une salle sans cloison)
	}
	const MazeLevel &level = MazeLevelAt(x, z, floor);
	if (MazeWall(level, x, z, floor))
		return true;
	// Une frontière de niveaux est un mur continu, percé de portes.
	if (&MazeLevelAt(x + 1, z, floor) != &level || &MazeLevelAt(x, z + 1, floor) != &level)
		return (mx + mz) % 9 > 2;
	return false;
}

bool TerrainGenerator::MazeExitCell(int cx, int cz, int floor) const {
	if (MazeReservedCell(cx, cz) || MazeStairCell(cx, cz, floor) || MazeStairCell(cx, cz, floor - 1))
		return false;
	return m_mazeExit != AIR && RandomAt(m_seed, cx, 7 + floor * 13, cz, 631) < m_exitChance &&
		   !MazeColumnWall(cx * m_mazeCell + m_mazeCell / 2, cz * m_mazeCell + m_mazeCell / 2, floor);
}

BlockId TerrainGenerator::MazeExitSign(int x, int z, uint8_t &param, int floor) const {
	if (m_mazeSignLeft == AIR && m_mazeSignRight == AIR)
		return AIR;
	const int cell = m_mazeCell;
	const int cx = int(std::floor(float(x) / float(cell))), cz = int(std::floor(float(z) / float(cell)));
	const int mx = x - cx * cell, mz = z - cz * cell;
	// Un panneau au milieu de chaque pan de mur : la colonne longe un mur
	// (direction d), à mi-longueur de la cellule.
	struct Side {
		int dx, dz;
		uint8_t param; ///< orientation du panneau (plaqué côté +z à l'orientation 0)
	};
	static constexpr Side SIDES[4] = {{0, 1, 0}, {-1, 0, 1}, {0, -1, 2}, {1, 0, 3}};
	const Side *side = nullptr;
	for (const Side &s : SIDES) {
		const int along = s.dx != 0 ? mz : mx;
		if (along == cell / 2 && MazeColumnWall(x + s.dx, z + s.dz, floor) && !MazeColumnWall(x, z, floor)) {
			side = &s;
			break;
		}
	}
	if (!side)
		return AIR;
	// Sortie la plus proche dans les cellules voisines (pas la sienne : la
	// porte est alors en vue, sous son propre panneau).
	int best = -1, ex = 0, ez = 0;
	for (int j = -m_mazeSignRange; j <= m_mazeSignRange; ++j)
		for (int i = -m_mazeSignRange; i <= m_mazeSignRange; ++i) {
			if ((i == 0 && j == 0) || !MazeExitCell(cx + i, cz + j, floor))
				continue;
			const int px = (cx + i) * cell + cell / 2, pz = (cz + j) * cell + cell / 2;
			const int d = (px - x) * (px - x) + (pz - z) * (pz - z);
			if (best < 0 || d < best) {
				best = d;
				ex = px;
				ez = pz;
			}
		}
	if (best < 0)
		return AIR;
	// Face au mur (regard vers d), la droite est (−dz, dx) : la flèche
	// pointe du côté où se trouve la sortie.
	const int toRight = (ex - x) * -side->dz + (ez - z) * side->dx;
	param = side->param;
	const BlockId preferred = toRight >= 0 ? m_mazeSignRight : m_mazeSignLeft;
	return preferred != AIR ? preferred : toRight >= 0 ? m_mazeSignLeft : m_mazeSignRight;
}

const TerrainGenerator::MazeElevator *TerrainGenerator::MazeElevatorColumn(Chunk &chunk, int lx, int lz, int x, int z) {
	const int cell = m_mazeCell;
	const MazeElevator *found = nullptr;
	for (const MazeElevator &candidate : m_elevators)
		if (std::abs(x - (candidate.cx * cell + cell / 2)) <= 3 && std::abs(z - (candidate.cz * cell + cell / 2)) <= 3)
			found = &candidate;
	if (!found)
		return nullptr;
	const MazeElevator &e = *found;
	const int ax = x - (e.cx * cell + cell / 2), az = z - (e.cz * cell + cell / 2);
	const BlockPos origin = chunk.Pos().Origin();
	const int bottom = e.bottom;
	const int top = e.top; // dernier vide sous le toit
	const bool wallColumn = std::abs(ax) == 3 || std::abs(az) == 3;
	// Mur du côté des portes : la colonne de la porte, du bouton, de la flèche.
	const bool doorWall = (e.dz != 0 && az == 3 * e.dz) || (e.dx != 0 && ax == 3 * e.dx);
	const int u = e.dz != 0 ? ax * e.ux : az * e.uz; // position le long du mur (porte en 0)
	const bool backWall = (e.dz != 0 && az == -3 * e.dz) || (e.dx != 0 && ax == -3 * e.dx);
	const int along = e.dz != 0 ? ax : az;
	for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
		const int y = origin.y + ly;
		if (y < bottom - 1 || y > top + 1)
			continue;
		BlockId id = AIR;
		uint8_t param = 0;
		if (y == bottom - 1 || y == top + 1) {
			id = (y == top + 1 && ax == 0 && az == 0 && e.motor != AIR) ? e.motor : e.wall;
		} else if (!wallColumn) {
			id = AIR; // l'intérieur de la gaine (la cabine est posée par le mod « elevators »)
		} else {
			id = e.wall;
			if (backWall && std::abs(along) == 1 && e.rail != AIR)
				id = e.rail;
			if (doorWall && std::abs(u) <= 1) {
				// Palier de l'étage dont le plancher de cabine est sous y.
				for (int k = e.from; k <= e.to; ++k) {
					const int f = MazeFloorY(k);
					const int v = y - f; // 1, 2 : porte ; 3 : afficheur ; …
					if (v < 1 || v > 3)
						continue;
					if (u == 0 && v == 1 && e.door != AIR) { id = e.door; param = e.doorParam; }
					else if (u == 0 && v == 2 && e.doorTop != AIR) { id = e.doorTop; param = e.doorParam; }
					else if (u == 0 && v == 3 && e.display != AIR) id = e.display;
					else if (u == -1 && v == 3 && e.arrow != AIR) id = e.arrow;
					else if (u == 1 && v == 2 && e.call != AIR) id = e.call;
					else if (e.frame != AIR) id = e.frame;
				}
			}
		}
		if (id != AIR)
			chunk.Set(lx, ly, lz, id, param);
		else
			chunk.Set(lx, ly, lz, AIR);
	}
	return &e;
}

void TerrainGenerator::GenerateMaze(Chunk &chunk, World &world) {
	const BlockPos origin = chunk.Pos().Origin();
	const int floor0 = m_mazeFloor;
	const int lastCeiling = MazeFloorY(m_mazeFloors - 1) + m_mazeHeight;
	int shellBelow = 6, shellAbove = 6;
	for (const MazeElevator &e : m_elevators) {
		shellBelow = std::max(shellBelow, floor0 - e.bottom + 2);
		shellAbove = std::max(shellAbove, e.top + 6 - lastCeiling);
	}
	if (origin.y > lastCeiling + shellAbove || origin.y + CHUNK_SIZE <= floor0 - shellBelow)
		return;
	const int cell = m_mazeCell;
	for (int lz = 0; lz < CHUNK_SIZE; ++lz)
		for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
			const int x = origin.x + lx, z = origin.z + lz;
			const int cx = int(std::floor(float(x) / float(cell))), cz = int(std::floor(float(z) / float(cell)));
			const int mx = x - cx * cell, mz = z - cz * cell;
			// La roche autour des étages (sous, au-dessus).
			for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
				const int y = origin.y + ly;
				if ((y < floor0 && y >= floor0 - shellBelow) || (y > lastCeiling && y <= lastCeiling + shellAbove))
					chunk.Set(lx, ly, lz, m_mazeOuter);
			}
			// Une gaine occupe sa colonne de son fond à son toit ; hors de là
			// (étages qu'elle ne dessert pas), le labyrinthe continue.
			const MazeElevator *shaft = MazeElevatorColumn(chunk, lx, lz, x, z);
			for (int k = 0; k < m_mazeFloors; ++k) {
				if (shaft && MazeFloorY(k) >= shaft->bottom - 1 && MazeFloorY(k) + m_mazeHeight <= shaft->top + 1)
					continue;
				const int floor = MazeFloorY(k), ceiling = floor + m_mazeHeight;
				if (floor - (k == 0 ? m_poolDepth : 0) > origin.y + CHUNK_SIZE - 1 || ceiling + 1 < origin.y)
					continue;
				const MazeLevel &level = MazeLevelAt(x, z, k);
				const uint32_t fs = m_seed + uint32_t(k) * 7919u;
				const bool wall = MazeColumnWall(x, z, k);
				// Escaliers : montée de cet étage vers le suivant (k), trémie
				// venue de l'étage du dessous (k − 1) ; le long de +x, bande mz 2..4.
				const bool stairUp = MazeStairCell(cx, cz, k) && !wall;
				const bool stairBelow = MazeStairCell(cx, cz, k - 1) && !wall;
				const bool band = mz >= 2 && mz <= 4 && mx >= 1 && mx <= cell - 1;
				const int step = mx - 1; // 0 : première marche
				const bool special = stairUp || stairBelow || MazeReservedCell(cx, cz);
				const bool flooded = k == 0 && !wall && !special && level.water != AIR && RandomAt(fs, cx, 5, cz, 623) < level.waterChance;
				// Néons : allumés, éteints (« darkChance ») ou à moitié allumés.
				const int every = level.lightEvery;
				BlockId lamp = AIR;
				if (level.light != AIR && !wall && !(stairUp && band) && !(stairBelow && band) &&
					((x % every) + every) % every == every / 2 && ((z % every) + every) % every == every / 2) {
					if (RandomAt(fs, x, 6, z, 629) < level.darkChance)
						lamp = level.lightOff;
					else if (level.lightHalf != AIR && RandomAt(fs, x, 8, z, 637) < level.halfChance)
						lamp = level.lightHalf;
					else if (level.lightAuto != AIR && RandomAt(fs, x, 9, z, 641) < level.autoChance)
						lamp = level.lightAuto;
					else
						lamp = level.light;
				}
				const int lampY = level.lightHanging ? ceiling - 1 : ceiling;
				// Grille de ventilation dans le plafond : une par cellule tirée, hors du milieu (néons).
				const bool vent = level.vent != AIR && !wall && !special && !(lamp != AIR && lampY == ceiling) && mx == 2 &&
								  mz == cell - 3 && RandomAt(fs, cx, 12, cz, 653) < level.ventChance;
				const bool exit = !wall && !special && mx == cell / 2 && mz == cell / 2 && MazeExitCell(cx, cz, k);
				uint8_t signParam = 0;
				const BlockId sign = wall || flooded || exit ? AIR : MazeExitSign(x, z, signParam, k);
				for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
					const int y = origin.y + ly;
					if (y < floor - (k == 0 ? m_poolDepth : 0) || y > ceiling + 1)
						continue;
					if (shaft && y >= shaft->bottom - 1 && y <= shaft->top + 1)
						continue; // fond de la gaine qui descend dans cet étage
					BlockId id = AIR;
					uint8_t param = 0;
					if (y == ceiling + 1)
						continue; // la dalle : plancher de l'étage suivant, posé par lui
					if (y < floor) {
						// Bassins creusés sous le sol (eau au ras du sol), étage 0 ;
						// leurs bords et leur fond sont carrelés comme les murs.
						if (level.water == AIR || y < floor - m_poolDepth)
							id = m_mazeOuter;
						else if (y == floor - m_poolDepth)
							id = level.floor;
						else
							id = flooded ? level.water : level.wall;
					} else if (stairUp && band) {
						// Marches de 1 bloc montant vers +x, pleines dessous ; au-dessus,
						// tout est dégagé (plafond compris) pour passer la tête.
						const int top = floor + 1 + step;
						if (y == floor)
							id = level.floor;
						else if (y == top) {
							id = m_stairBlock;
							param = 3;
						} else if (y < top) {
							id = m_stairFill != AIR ? m_stairFill : level.wall;
						}
					} else if (y == floor) {
						id = flooded ? level.water : level.floor;
						if (stairBelow && band && step < cell - 2)
							id = AIR; // trémie de l'escalier qui arrive d'en bas
					} else if (y == ceiling) {
						id = lamp != AIR && lampY == ceiling ? lamp : vent ? level.vent : level.ceiling;
					} else if (wall) {
						id = (y == floor + 1 && level.trim != AIR) ? level.trim
															   : (level.pillar != AIR && mx == 0 && mz == 0 ? level.pillar : level.wall);
					} else if (stairBelow && band && step < cell - 2) {
						id = AIR; // l'escalier d'en bas traverse cet étage-ci : rien dessous
					} else if (exit && y == floor + 1) {
						id = m_mazeExit;
					} else if (exit && y == floor + 2 && m_mazeExitSign != AIR) {
						id = m_mazeExitSign;
					} else if (lamp != AIR && y == lampY) {
						id = lamp;
					} else if (sign != AIR && y == floor + m_mazeSignHeight) {
						id = sign;
						param = signParam;
					}
					if (id != AIR)
						chunk.Set(lx, ly, lz, id, param);
				}
				// Décorations du biome du niveau (mobilier, flaques…).
				if (level.biome && !wall && !flooded && !exit && !special && floor + 1 >= origin.y && floor + 1 < origin.y + CHUNK_SIZE) {
					uint32_t salt = 4000 + uint32_t(k) * 97u;
					for (const Decoration &deco : level.biome->decorations) {
						++salt;
						if (PlaceDecoration(world, deco, *level.biome, x, floor + 1, z, salt, false, level.floor))
							break;
					}
				}
			}
		}
}

// ── Éléments procéduraux ─────────────────────────────────────────────────────

void TerrainGenerator::FeatureGiantTree(World &world, const NodePtr &params, const String &mod, int x, int y, int z, uint32_t salt) {
	const BlockId trunk = BlockParam(m_content, params, "trunk", mod);
	const BlockId leaves = BlockParam(m_content, params, "leaves", mod);
	const BlockId sap = BlockParam(m_content, params, "sap", mod);
	const BlockId fruit = BlockParam(m_content, params, "fruit", mod);
	const BlockId floorBlock = BlockParam(m_content, params, "floor", mod);
	const BlockId climb = BlockParam(m_content, params, "climb", mod);
	const BlockId core = BlockParam(m_content, params, "core", mod);
	if (trunk == AIR)
		return;
	auto rnd = [&](uint32_t k) { return RandomAt(m_seed, x, y, z, salt * 31u + k); };
	const int height = int(Pick(RangeParam(params, "height", 36.f, 60.f), rnd(1)));
	const float radius = Pick(RangeParam(params, "radius", 3.f, 5.f), rnd(2));
	const bool hollow = json::Bool(params, "hollow", true);
	const float sapChance = float(json::Num(params, "sapChance", 0.04));
	const int door = int(rnd(3) * 4.f); // façade de l'entrée
	const int ir = int(std::ceil(radius)) + 3;
	for (int dy = -4; dy <= height; ++dy) {
		// Base évasée (racines), tronc régulier ensuite.
		const float r = radius + (dy < 3 ? float(3 - dy) * 0.7f : 0.f);
		for (int dz = -ir; dz <= ir; ++dz)
			for (int dx = -ir; dx <= ir; ++dx) {
				const float d = std::sqrt(float(dx * dx + dz * dz));
				if (d > r)
					continue;
				const BlockPos p{x + dx, y + dy, z + dz};
				const bool shell = d > r - 1.3f;
				if (hollow && !shell && dy >= 0 && dy < height - 3) {
					// Salles empilées : un plancher tous les 8 blocs, troué pour monter.
					const bool storey = dy % 8 == 0;
					const bool hole = dx == 1 && dz == 0;
					world.GenerationWrite(p, storey && !hole ? (floorBlock != AIR ? floorBlock : trunk) : AIR, true);
					if (core != AIR && dy == height / 2 && dx == 0 && dz == 0)
						world.GenerationWrite(p, core, true); // cœur de l'arbre (au milieu)
					continue;
				}
				BlockId id = trunk;
				if (sap != AIR && shell && dy > 2 && RandomAt(m_seed, p.x, p.y, p.z, salt + 7) < sapChance)
					id = sap;
				world.GenerationWrite(p, id, true);
			}
		// Liane intérieure pour grimper d'un étage à l'autre.
		if (hollow && climb != AIR && dy > 0 && dy < height - 3)
			world.GenerationWrite({x + 1, y + dy, z}, dy % 8 == 0 ? AIR : climb, true);
	}
	// Entrée : trois blocs de haut, deux de large, dans la façade tirée.
	static constexpr int DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
	if (hollow)
		for (int dy = 1; dy <= 3; ++dy)
			for (int step = 0; step <= ir; ++step)
				for (int w = 0; w < 2; ++w) {
					const int dx = DIRS[door][0] * step + (DIRS[door][0] == 0 ? w : 0);
					const int dz = DIRS[door][1] * step + (DIRS[door][1] == 0 ? w : 0);
					if (float(dx * dx + dz * dz) >= (radius - 1.3f) * (radius - 1.3f))
						world.GenerationWrite({x + dx, y + dy, z + dz}, AIR, true);
				}
	// Branches et canopée.
	const int top = y + height;
	const float canopy = radius * 2.8f + 3.f;
	for (int b = 0; b < 4; ++b) {
		const float angle = (float(b) + rnd(10 + uint32_t(b))) * 1.5708f;
		const int start = top - int(float(height) * 0.25f) - b * 2;
		for (int i = 0; i < int(canopy); ++i) {
			const int bx = x + int(std::cos(angle) * float(i)), bz = z + int(std::sin(angle) * float(i));
			world.GenerationWrite({bx, start + i / 3, bz}, trunk, false);
		}
	}
	if (leaves != AIR) {
		const int rc = int(std::ceil(canopy)), rv = int(std::ceil(canopy * 0.55f));
		for (int dy = -rv; dy <= rv; ++dy)
			for (int dz = -rc; dz <= rc; ++dz)
				for (int dx = -rc; dx <= rc; ++dx) {
					const float e = float(dx * dx + dz * dz) / (canopy * canopy) + float(dy * dy) / float(rv * rv);
					const BlockPos p{x + dx, top + dy, z + dz};
					if (e > 1.f || RandomAt(m_seed, p.x, p.y, p.z, salt + 11) < e * 0.35f)
						continue;
					const bool glow = fruit != AIR && RandomAt(m_seed, p.x, p.y, p.z, salt + 13) < 0.015f;
					world.GenerationWrite(p, glow ? fruit : leaves, false);
				}
	}
}

void TerrainGenerator::FeatureSpire(World &world, const NodePtr &params, const String &mod, int x, int y, int z, uint32_t salt, bool ceiling) {
	const BlockId block = BlockParam(m_content, params, "block", mod);
	const BlockId accent = BlockParam(m_content, params, "accent", mod);
	const BlockId crown = BlockParam(m_content, params, "crown", mod);
	const BlockId inside = BlockParam(m_content, params, "inside", mod);
	if (block == AIR)
		return;
	auto rnd = [&](uint32_t k) { return RandomAt(m_seed, x, y, z, salt * 37u + k); };
	const int height = int(Pick(RangeParam(params, "height", 10.f, 30.f), rnd(1)));
	const float radius = Pick(RangeParam(params, "radius", 1.f, 3.f), rnd(2));
	const bool hollow = json::Bool(params, "hollow", false);
	const bool taper = json::Bool(params, "taper", true);
	const bool square = json::Bool(params, "square", false);
	const float ruined = float(json::Num(params, "ruined", 0.0));
	const int dir = ceiling ? -1 : 1;
	const int ir = int(std::ceil(radius)) + 1;
	for (int i = -3; i <= height; ++i) {
		const float r = taper && i > 0 ? std::max(0.6f, radius * (1.f - float(i) / float(height) * 0.65f)) : radius;
		for (int dz = -ir; dz <= ir; ++dz)
			for (int dx = -ir; dx <= ir; ++dx) {
				const float d = square ? float(std::max(std::abs(dx), std::abs(dz))) : std::sqrt(float(dx * dx + dz * dz));
				if (d > r)
					continue;
				const BlockPos p{x + dx, y + i * dir, z + dz};
				// Ruine : de plus en plus de blocs manquent vers le sommet.
				if (ruined > 0.f && i > 1 && RandomAt(m_seed, p.x, p.y, p.z, salt + 3) < ruined * float(i) / float(height))
					continue;
				const bool shell = d > r - 1.f;
				if (hollow && !shell && i >= 0) {
					world.GenerationWrite(p, i > 0 && i % 6 == 0 ? (accent != AIR ? accent : block) : (inside != AIR && i == 1 && dx == 0 && dz == 0 ? inside : AIR), true);
					continue;
				}
				world.GenerationWrite(p, accent != AIR && i > 0 && i % 5 == 0 ? accent : block, true);
			}
	}
	if (hollow) // porte
		for (int i = 1; i <= 2; ++i)
			for (int k = 0; k <= ir; ++k)
				world.GenerationWrite({x + k, y + i * dir, z}, AIR, true);
	if (crown != AIR)
		world.GenerationWrite({x, y + (height + 1) * dir, z}, crown, true);
}

void TerrainGenerator::FeaturePatch(World &world, const NodePtr &params, const String &mod, int x, int y, int z, uint32_t salt, bool ceiling) {
	const BlockId block = BlockParam(m_content, params, "block", mod);
	const BlockId rim = BlockParam(m_content, params, "rim", mod);
	if (block == AIR)
		return;
	const float radius = Pick(RangeParam(params, "radius", 2.f, 4.f), RandomAt(m_seed, x, y, z, salt * 41u));
	const int depth = std::max(1, json::Int(params, "depth", 3));
	const int ir = int(std::ceil(radius)) + 1;
	const int dir = ceiling ? 1 : -1; // vers l'intérieur de la roche
	for (int dz = -ir; dz <= ir; ++dz)
		for (int dx = -ir; dx <= ir; ++dx) {
			const float d = std::sqrt(float(dx * dx + dz * dz)) + m_detail.Sample(float(x + dx), float(z + dz)) * 0.8f;
			if (d > radius + 0.8f)
				continue;
			const bool edge = d > radius;
			// Plus profond au centre.
			const int deep = edge ? 1 : std::max(1, int(float(depth) * (1.f - d / (radius + 1.f)) + 0.99f));
			for (int k = 0; k < deep; ++k) {
				const BlockPos p{x + dx, y + dir * (1 + k), z + dz};
				world.GenerationWrite(p, edge && rim != AIR ? rim : edge && rim == AIR ? AIR : block, true);
				if (edge && rim == AIR)
					break;
			}
		}
}

void TerrainGenerator::FeatureCluster(World &world, const NodePtr &params, const String &mod, int x, int y, int z, uint32_t salt, bool ceiling) {
	const BlockId block = BlockParam(m_content, params, "block", mod);
	const BlockId tip = BlockParam(m_content, params, "tip", mod);
	if (block == AIR)
		return;
	const int size = std::max(1, int(Pick(RangeParam(params, "size", 1.f, 3.f), RandomAt(m_seed, x, y, z, salt * 43u))));
	const int dir = ceiling ? -1 : 1;
	for (int dy = 0; dy < size * 2; ++dy)
		for (int dz = -size; dz <= size; ++dz)
			for (int dx = -size; dx <= size; ++dx) {
				const float r = float(size) * (1.f - float(dy) / float(size * 2));
				if (float(dx * dx + dz * dz) > r * r + 0.3f)
					continue;
				const BlockPos p{x + dx, y + dy * dir, z + dz};
				if (RandomAt(m_seed, p.x, p.y, p.z, salt + 5) < 0.25f)
					continue;
				world.GenerationWrite(p, block, false);
			}
	if (tip != AIR)
		world.GenerationWrite({x, y + size * 2 * dir, z}, tip, false);
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
