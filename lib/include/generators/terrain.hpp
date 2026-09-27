#pragma once
/**
 * generators — terrain procédural complet, à la TerraForge3D : une PILE de
 * couches de bruit (chacune avec son mélange, son intensité, son masque),
 * suivie d'un traitement (île, courbe, terrasses, érosions, mer), puis de la
 * CLASSIFICATION en biomes (hauteur × pente → couleur, poids de texture) et
 * de la production d'un MAILLAGE (positions, normales, UV, couleurs) prêt à
 * être envoyé au moteur de rendu ou exporté en OBJ.
 *
 * @code
 * generators::TerrainSettings t;
 * t.width = t.height = 257;
 * t.layers.push_back({.noise = {.type = generators::NoiseType::SIMPLEX, .fractal = generators::FractalType::RIDGED,
 *                               .frequency = 0.006f, .octaves = 6}, .strength = 1.f});
 * t.erosion = generators::HydraulicErosion{.droplets = 40000};
 * generators::HeightMap map = generators::GenerateTerrain(t);
 * generators::TerrainMesh mesh = generators::BuildTerrainMesh(map, {.cellSize = 1.f, .heightScale = 60.f},
 *                                                             generators::DefaultBiomes());
 * @endcode
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../core/core.hpp"
#include "heightmap.hpp"
#include "noise.hpp"

namespace generators {

/// Une couche de la pile : un bruit, mélangé au résultat des couches
/// précédentes, éventuellement MASQUÉ par une autre couche (ex. : montagnes
/// seulement là où un bruit basse fréquence est haut).
struct TerrainLayer {
	NoiseSettings noise;
	BlendMode blend = BlendMode::ADD;
	float strength = 1.f;
	/// Indice d'une couche PRÉCÉDENTE servant de masque [0, 1] (-1 : aucun).
	int maskLayer = -1;
	float maskLow = 0.f, maskHigh = 1.f; ///< bande du masque (lissée par maskFeather)
	float maskFeather = 0.1f;
	bool enabled = true;
};

struct TerrainSettings {
	int width = 257, height = 257;
	float scale = 1.f; ///< taille du monde par case (pour le bruit)
	std::vector<TerrainLayer> layers;
	bool normalize = true;
	float islandFalloff = 0.f; ///< > 0 : île (exposant du dégradé)
	std::vector<std::pair<float, float>> curve; ///< courbe finale (vide : aucune)
	int terraces = 0;
	float terraceSharpness = 0.5f;
	Option<HydraulicErosion> erosion = NONE;
	Option<ThermalErosion> thermal = NONE;
	float smooth = 0.f;      ///< flou final (sigma, 0 : aucun)
	float seaLevel = -1.f;   ///< < 0 : pas de mer
};

/// Terrain par défaut (continents, collines, montagnes masquées).
[[nodiscard]] TerrainSettings DefaultTerrain(uint32_t seed = 1);

/// Empile les couches puis applique le traitement.
[[nodiscard]] HeightMap GenerateTerrain(const TerrainSettings &t);

// ── Biomes ──────────────────────────────────────────────────────────────────

/// Règle de biome : une bande de hauteur (carte normalisée) ET de pente
/// (degrés). La première règle qui correspond l'emporte ; les poids servent
/// aux mélanges de textures (splatmap).
struct BiomeRule {
	String name;
	float minHeight = 0.f, maxHeight = 1.f;
	float minSlope = 0.f, maxSlope = 90.f;
	std::array<uint8_t, 3> color{128, 128, 128};
	float feather = 0.03f; ///< transition douce (hauteur ; ×30 pour la pente)
};

[[nodiscard]] std::vector<BiomeRule> DefaultBiomes(float seaLevel = 0.3f);

/// Indice de la règle qui s'applique (-1 : aucune).
[[nodiscard]] int ClassifyBiome(const std::vector<BiomeRule> &rules, float height, float slopeDeg);

/// Poids (non normalisés) de chaque règle en un point : la « splatmap ».
[[nodiscard]] std::vector<float> BiomeWeights(const std::vector<BiomeRule> &rules, float height, float slopeDeg);

/// Couleur mélangée des biomes en un point.
[[nodiscard]] std::array<uint8_t, 3> BiomeColor(const std::vector<BiomeRule> &rules, float height, float slopeDeg);

// ── Maillage ────────────────────────────────────────────────────────────────

struct TerrainMeshSettings {
	float cellSize = 1.f;     ///< distance entre deux cases (X et Z)
	float heightScale = 50.f; ///< hauteur pour une valeur de 1
	int step = 1;             ///< 1 case sur `step` (niveau de détail)
	bool centered = true;     ///< origine au centre du terrain
};

/// Maillage indexé (Y vers le haut, triangles CCW vus d'en haut).
struct TerrainMesh {
	std::vector<float> positions; ///< x, y, z
	std::vector<float> normals;   ///< x, y, z
	std::vector<float> uvs;       ///< u, v
	std::vector<uint8_t> colors;  ///< r, g, b (biomes ; vide sans règles)
	std::vector<uint32_t> indices;
	int columns = 0, rows = 0;

	[[nodiscard]] size_t VertexCount() const noexcept { return positions.size() / 3; }
	[[nodiscard]] size_t TriangleCount() const noexcept { return indices.size() / 3; }

	/// Export Wavefront OBJ (positions, normales, UV ; couleurs en extension
	/// « v x y z r g b » que lisent Blender et MeshLab).
	[[nodiscard]] Option<String> SaveObj(const String &path) const;
};

[[nodiscard]] TerrainMesh BuildTerrainMesh(const HeightMap &map, const TerrainMeshSettings &s = {},
												  const std::vector<BiomeRule> &biomes = {});

} // namespace generators
