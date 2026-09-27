// Définitions de generators/terrain.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "generators/terrain.hpp"

namespace generators {

TerrainSettings DefaultTerrain(uint32_t seed) {
	TerrainSettings t;
	TerrainLayer continents;
	continents.noise.type = NoiseType::SIMPLEX;
	continents.noise.frequency = 0.004f;
	continents.noise.octaves = 4;
	continents.noise.seed = seed;
	continents.blend = BlendMode::SET;
	TerrainLayer mountains;
	mountains.noise.type = NoiseType::SIMPLEX;
	mountains.noise.fractal = FractalType::RIDGED;
	mountains.noise.frequency = 0.012f;
	mountains.noise.octaves = 6;
	mountains.noise.seed = seed + 1;
	mountains.blend = BlendMode::ADD;
	mountains.strength = 0.7f;
	mountains.maskLayer = 0;
	mountains.maskLow = 0.55f;
	mountains.maskHigh = 1.f;
	mountains.maskFeather = 0.15f;
	TerrainLayer hills;
	hills.noise.type = NoiseType::PERLIN;
	hills.noise.frequency = 0.03f;
	hills.noise.octaves = 4;
	hills.noise.seed = seed + 2;
	hills.blend = BlendMode::ADD;
	hills.strength = 0.12f;
	t.layers = {continents, mountains, hills};
	return t;
}

HeightMap GenerateTerrain(const TerrainSettings &t) {
	HeightMap map(t.width, t.height, 0.f);
	std::vector<HeightMap> layerMaps;
	layerMaps.reserve(t.layers.size());
	for (const TerrainLayer &layer : t.layers) {
		HeightMap own(t.width, t.height);
		own.Fill(Noise(layer.noise), t.scale);
		HeightMap mask;
		const HeightMap *maskPtr = nullptr;
		if (layer.maskLayer >= 0 && size_t(layer.maskLayer) < layerMaps.size()) {
			mask = layerMaps[size_t(layer.maskLayer)].HeightMask(layer.maskLow, layer.maskHigh, layer.maskFeather);
			maskPtr = &mask;
		}
		if (layer.enabled)
			map.Combine(own, layer.blend, layer.strength, maskPtr);
		layerMaps.push_back(std::move(own));
	}
	if (t.normalize)
		map.Normalize();
	if (t.islandFalloff > 0.f)
		map.IslandFalloff(t.islandFalloff).Clamp(0.f, 1.f);
	if (!t.curve.empty())
		map.Curve(t.curve);
	if (t.terraces > 0)
		map.Terrace(t.terraces, t.terraceSharpness);
	if (t.erosion.IsSome())
		map.ErodeHydraulic(t.erosion.Value());
	if (t.thermal.IsSome())
		map.ErodeThermal(t.thermal.Value());
	if (t.smooth > 0.f)
		map.Smooth(t.smooth);
	if (t.normalize)
		map.Normalize();
	if (t.seaLevel >= 0.f)
		map.SeaLevel(t.seaLevel);
	return map;
}

std::vector<BiomeRule> DefaultBiomes(float seaLevel) {
	return {
		{String("eau profonde"), 0.f, seaLevel - 0.08f, 0.f, 90.f, {28, 60, 120}, 0.02f},
		{String("eau"), 0.f, seaLevel + 0.001f, 0.f, 90.f, {45, 95, 160}, 0.02f},
		{String("plage"), seaLevel, seaLevel + 0.04f, 0.f, 20.f, {214, 198, 140}, 0.02f},
		{String("falaise"), seaLevel, 1.f, 38.f, 90.f, {110, 104, 98}, 0.02f},
		{String("prairie"), seaLevel, 0.55f, 0.f, 38.f, {92, 150, 62}, 0.03f},
		{String("forêt"), 0.55f, 0.72f, 0.f, 38.f, {48, 104, 50}, 0.03f},
		{String("roche"), 0.72f, 0.86f, 0.f, 90.f, {128, 120, 112}, 0.03f},
		{String("neige"), 0.86f, 1.f, 0.f, 90.f, {240, 244, 248}, 0.03f},
	};
}

int ClassifyBiome(const std::vector<BiomeRule> &rules, float height, float slopeDeg) {
	for (size_t i = 0; i < rules.size(); ++i) {
		const BiomeRule &r = rules[i];
		if (height >= r.minHeight && height <= r.maxHeight && slopeDeg >= r.minSlope && slopeDeg <= r.maxSlope)
			return int(i);
	}
	return -1;
}

std::vector<float> BiomeWeights(const std::vector<BiomeRule> &rules, float height, float slopeDeg) {
	std::vector<float> weights(rules.size(), 0.f);
	float total = 0.f;
	for (size_t i = 0; i < rules.size(); ++i) {
		const BiomeRule &r = rules[i];
		weights[i] = HeightMap::Band(height, r.minHeight, r.maxHeight, r.feather) *
					 HeightMap::Band(slopeDeg, r.minSlope, r.maxSlope, r.feather * 30.f);
		total += weights[i];
	}
	if (total > 0.f)
		for (float &w : weights)
			w /= total;
	return weights;
}

std::array<uint8_t, 3> BiomeColor(const std::vector<BiomeRule> &rules, float height, float slopeDeg) {
	const std::vector<float> weights = BiomeWeights(rules, height, slopeDeg);
	float c[3] = {0, 0, 0};
	float total = 0.f;
	for (size_t i = 0; i < rules.size(); ++i) {
		for (int k = 0; k < 3; ++k)
			c[k] += weights[i] * float(rules[i].color[size_t(k)]);
		total += weights[i];
	}
	if (total <= 0.f) {
		const int index = ClassifyBiome(rules, height, slopeDeg);
		return index >= 0 ? rules[size_t(index)].color : std::array<uint8_t, 3>{128, 128, 128};
	}
	return {uint8_t(c[0]), uint8_t(c[1]), uint8_t(c[2])};
}

// ── TerrainMesh ──────────────────────────────────────────────────────────────

Option<String> TerrainMesh::SaveObj(const String &path) const {
	std::FILE *file = std::fopen(path.CStr(), "w");
	if (!file)
		return Some(String::Format("impossible d'écrire %s", path.CStr()));
	std::fprintf(file, "# Terrain généré (generators::TerrainMesh)\n");
	const bool withColor = colors.size() == positions.size();
	for (size_t i = 0; i < VertexCount(); ++i) {
		if (withColor)
			std::fprintf(file, "v %g %g %g %.3f %.3f %.3f\n", positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2],
						 colors[i * 3] / 255.0, colors[i * 3 + 1] / 255.0, colors[i * 3 + 2] / 255.0);
		else
			std::fprintf(file, "v %g %g %g\n", positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
	}
	for (size_t i = 0; i < VertexCount(); ++i)
		std::fprintf(file, "vt %g %g\n", uvs[i * 2], uvs[i * 2 + 1]);
	for (size_t i = 0; i < VertexCount(); ++i)
		std::fprintf(file, "vn %g %g %g\n", normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		const uint32_t a = indices[i] + 1, b = indices[i + 1] + 1, c = indices[i + 2] + 1;
		std::fprintf(file, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, c, c, c);
	}
	return std::fclose(file) == 0 ? Option<String>(NONE) : Some(String::Format("écriture incomplète de %s", path.CStr()));
}

TerrainMesh BuildTerrainMesh(const HeightMap &map, const TerrainMeshSettings &s, const std::vector<BiomeRule> &biomes) {
	TerrainMesh mesh;
	const int step = std::max(1, s.step);
	mesh.columns = (map.Width() - 1) / step + 1;
	mesh.rows = (map.Height() - 1) / step + 1;
	const float originX = s.centered ? -float(map.Width() - 1) * s.cellSize * 0.5f : 0.f;
	const float originZ = s.centered ? -float(map.Height() - 1) * s.cellSize * 0.5f : 0.f;
	HeightMap slope;
	if (!biomes.empty())
		slope = map.SlopeMap(s.cellSize, s.heightScale);
	for (int r = 0; r < mesh.rows; ++r)
		for (int c = 0; c < mesh.columns; ++c) {
			const int x = std::min(c * step, map.Width() - 1), y = std::min(r * step, map.Height() - 1);
			const float h = map.At(x, y);
			mesh.positions.insert(mesh.positions.end(),
								  {originX + float(x) * s.cellSize, h * s.heightScale, originZ + float(y) * s.cellSize});
			float nx, ny, nz;
			map.Normal(x, y, s.cellSize, s.heightScale, nx, ny, nz);
			mesh.normals.insert(mesh.normals.end(), {nx, ny, nz});
			mesh.uvs.insert(mesh.uvs.end(),
							{float(x) / float(std::max(1, map.Width() - 1)), float(y) / float(std::max(1, map.Height() - 1))});
			if (!biomes.empty()) {
				const auto color = BiomeColor(biomes, h, slope.At(x, y));
				mesh.colors.insert(mesh.colors.end(), {color[0], color[1], color[2]});
			}
		}
	for (int r = 0; r + 1 < mesh.rows; ++r)
		for (int c = 0; c + 1 < mesh.columns; ++c) {
			const uint32_t i0 = uint32_t(r * mesh.columns + c), i1 = i0 + 1;
			const uint32_t i2 = i0 + uint32_t(mesh.columns), i3 = i2 + 1;
			mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
		}
	return mesh;
}

} // namespace generators
