#pragma once
/**
 * Génération procédurale pilotée par les définitions (GeneratorDef,
 * BiomeDef, StructureDef).
 *
 *  - "flat"   : couches (bloc, épaisseur) empilées depuis y = 0.
 *  - "noise"  : deux bruits climatiques (température, humidité) désignent
 *               le biome de chaque colonne ; la hauteur est le mélange
 *               pondéré des hauteurs des biomes voisins dans le diagramme
 *               climatique (pas de falaise aux frontières) ; surface,
 *               sous-sol et remplissage viennent du biome ; eau sous le
 *               niveau de la mer ; grottes (bruit 3D) ; minerais
 *               ("ores" du générateur) ; décorations et structures des
 *               biomes, déterministes par colonne.
 *  - "islands": îles flottantes (« islands » : base, spread, threshold,
 *               height, thickness ; « mirror » : seconde couche d'îles
 *               RETOURNÉES, surface dessous ; « fragments » : débris en
 *               suspension) ; biomes et minerais comme "noise".
 *  - "caverns": monde souterrain fermé (« caverns » : floor, roof,
 *               frequency, threshold, lavaLevel, lava, roofBlock) ; les
 *               biomes décorent sols ET plafonds des cavernes.
 *  - "maze"   : salles et couloirs sans fin (« maze » : floor, height,
 *               cell, region, wallChance, doorWidth, outer, exit ;
 *               « levels » : zones aux blocs propres, cf. backrooms).
 *  - "script" : la fonction du mod remplit le chunk (cf. ScriptHost).
 *
 * Décorations des biomes : bloc, structure, ou élément PROCÉDURAL
 * (« feature ») — "giant_tree" (tronc creux, salles, sève, canopée),
 * "spire" (tour, monolithe, ruine), "patch" (poche de blocs dans le sol :
 * sables mouvants…), "cluster" (amas de cristaux). « where » : "floor"
 * (défaut) ou "ceiling" (pendu sous le plafond / sous une île).
 *
 * Tout est déterministe : même graine, mêmes chunks, dans n'importe quel
 * ordre de génération.
 */
#include <functional>
#include <memory>

#include <generators/noise.hpp>

#include "world.hpp"

namespace voxel {

/// Hasard déterministe d'une position (splitmix64).
[[nodiscard]] uint64_t HashPosition(uint32_t seed, int x, int y, int z, uint32_t salt = 0) noexcept;
/// Même chose dans [0, 1[.
[[nodiscard]] float RandomAt(uint32_t seed, int x, int y, int z, uint32_t salt = 0) noexcept;

/// Réglages de génération propres à un monde (écran de création).
struct GenerationTuning {
	float relief = 1.f;     ///< amplitude du relief
	float caves = 1.f;      ///< densité des grottes
	float ores = 1.f;       ///< abondance des minerais
	float vegetation = 1.f; ///< arbres et plantes
	bool structures = true; ///< bâtiments, ruines, rochers
	int seaLevel = 0;       ///< décalage du niveau de la mer
};

class TerrainGenerator final : public ChunkGenerator {
public:
	/// Fonction de génération d'un mod ("script") : remplit le chunk.
	using ScriptFn = std::function<void(Chunk &chunk, World &world, const GeneratorDef &def)>;

	TerrainGenerator(const ContentRegistry &content, const GeneratorDef &def, uint32_t seed, ScriptFn script = {});

	void Generate(Chunk &chunk, World &world) override;
	[[nodiscard]] int SurfaceHeight(int x, int z) const override;
	[[nodiscard]] const BiomeDef *BiomeAt(int x, int z) const override;
	[[nodiscard]] Option<int> SkyCover(int x, int z) const override;

	[[nodiscard]] const GeneratorDef &Definition() const noexcept { return m_def; }
	void SetTuning(const GenerationTuning &tuning);
	[[nodiscard]] const GenerationTuning &Tuning() const noexcept { return m_tuning; }

private:
	struct Column {
		int height;
		const BiomeDef *biome;
	};
	struct Ore {
		BlockId block, in;
		float chance;
		int minY, maxY, size;
	};
	/// Îles : une couche (normale ou retournée).
	struct IslandLayer {
		bool enabled = false, mirrored = false;
		float base = 80.f, spread = 20.f, threshold = 0.35f, height = 6.f, thickness = 18.f;
		std::vector<const BiomeDef *> biomes; ///< vide : ceux du générateur
	};
	struct IslandColumn {
		bool present = false;
		int top = 0, bottom = 0;
	};
	struct MazeLevel {
		String id;
		float weight = 1.f;
		BlockId floor = AIR, wall = AIR, ceiling = AIR, light = AIR, pillar = AIR, water = AIR, trim = AIR;
		int lightEvery = 4;
		bool open = false;        ///< pas de murs : salle immense à piliers
		float wallChance = 0.55f;
		float waterChance = 0.f;  ///< cellules inondées (piscines)
		float darkChance = 0.f;   ///< lampes éteintes
		const BiomeDef *biome = nullptr;
	};

	[[nodiscard]] Column ColumnAt(int x, int z) const;
	/// Biome climatique d'une colonne (sans calcul de hauteur), parmi `biomes`
	/// (par défaut : ceux du générateur).
	[[nodiscard]] const BiomeDef *ClimateBiome(int x, int z) const { return ClimateBiome(m_biomes, x, z); }
	[[nodiscard]] const BiomeDef *ClimateBiome(const std::vector<const BiomeDef *> &biomes, int x, int z) const;
	void GenerateFlat(Chunk &chunk);
	void GenerateNoise(Chunk &chunk, World &world);
	void Decorate(Chunk &chunk, World &world, int x, int z, const Column &column);

	void SetupIslands();
	[[nodiscard]] IslandColumn IslandAt(const IslandLayer &layer, const generators::Noise &noise, int x, int z) const;
	void GenerateIslands(Chunk &chunk, World &world);

	void SetupCaverns();
	[[nodiscard]] bool CavernSolid(int x, int y, int z) const;
	void GenerateCaverns(Chunk &chunk, World &world);

	void SetupMaze();
	[[nodiscard]] const MazeLevel &MazeLevelAt(int x, int z) const;
	[[nodiscard]] bool MazeWall(const MazeLevel &level, int x, int z) const;
	void GenerateMaze(Chunk &chunk, World &world);

	/// Pose une décoration (bloc, structure, élément procédural) en (x, y, z) :
	/// au-dessus du sol (`ceiling` faux) ou sous un plafond. Rend vrai si posée.
	bool PlaceDecoration(World &world, const Decoration &deco, const BiomeDef &biome, int x, int y, int z, uint32_t salt,
						 bool ceiling, BlockId surface);
	void FeatureGiantTree(World &world, const NodePtr &params, const String &mod, int x, int y, int z, uint32_t salt);
	void FeatureSpire(World &world, const NodePtr &params, const String &mod, int x, int y, int z, uint32_t salt, bool ceiling);
	void FeaturePatch(World &world, const NodePtr &params, const String &mod, int x, int y, int z, uint32_t salt, bool ceiling);
	void FeatureCluster(World &world, const NodePtr &params, const String &mod, int x, int y, int z, uint32_t salt, bool ceiling);

	const ContentRegistry &m_content;
	GeneratorDef m_def;
	uint32_t m_seed;
	ScriptFn m_script;
	std::vector<const BiomeDef *> m_biomes;
	std::vector<Ore> m_ores;
	generators::Noise m_height, m_detail, m_temperature, m_humidity, m_caves;
	BlockId m_fluid = AIR, m_bedrock = AIR;
	int m_bedrockLevel = -64;
	GenerationTuning m_tuning;
	// "islands"
	IslandLayer m_mainIslands, m_mirrorIslands;
	generators::Noise m_islandNoise, m_mirrorNoise, m_islandOffset, m_fragmentNoise;
	float m_fragments = 0.f;
	int m_fragmentMin = 40, m_fragmentMax = 140;
	BlockId m_fragmentBlock = AIR;
	// "caverns"
	int m_cavernFloor = -48, m_cavernRoof = 64, m_lavaLevel = -1000;
	float m_cavernThreshold = 0.2f, m_cavernVertical = 1.4f;
	BlockId m_lava = AIR, m_roofBlock = AIR;
	generators::Noise m_cavernNoise, m_chamberNoise;
	// "maze"
	int m_mazeFloor = 0, m_mazeHeight = 5, m_mazeCell = 7, m_mazeRegion = 80, m_doorWidth = 2;
	BlockId m_mazeOuter = AIR, m_mazeExit = AIR, m_mazeExitSign = AIR;
	float m_exitChance = 0.003f;
	std::vector<MazeLevel> m_levels;
	float m_levelWeight = 0.f;
};

/// Générateur du monde : le GeneratorDef demandé (ou le premier), sinon un
/// monde plat de secours.
[[nodiscard]] std::shared_ptr<TerrainGenerator> MakeGenerator(const ContentRegistry &content, const String &generatorId,
															  uint32_t seed, TerrainGenerator::ScriptFn script = {});

} // namespace voxel
