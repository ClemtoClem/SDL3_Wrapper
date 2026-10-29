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
 *               cell, region, wallChance, doorWidth, poolDepth, outer, exit ;
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

/// Gaine d'ascenseur générée par un labyrinthe (vue des scripts : world.maze_shaft).
struct MazeShaftInfo {
	BlockPos motor;  ///< moteur, au-dessus du centre de la gaine
	int bottom = 0;  ///< fond de la fosse (premier vide)
	int size = 7;    ///< côté extérieur de la gaine (impair : 5, 7 ou 9)
	String side;     ///< côté des paliers (n, s, e, w)
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
	/// Labyrinthe : nombre d'étages, et moteur d'une de ses gaines d'ascenseur
	/// (`id` vide : la première ; NONE : pas de gaine).
	[[nodiscard]] int MazeFloors() const noexcept { return m_mazeFloors; }
	[[nodiscard]] Option<BlockPos> MazeElevatorMotor(const String &id = String()) const;
	/// Même gaine, avec son fond, sa taille et le côté de ses paliers.
	[[nodiscard]] Option<MazeShaftInfo> MazeElevatorShaft(const String &id = String()) const;
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
		float waterChance = 0.f;  ///< cellules inondées : bassins creusés, eau au ras du sol
		float darkChance = 0.f;   ///< lampes éteintes
		/// Néon éteint (« lightOff » ; sinon le plafond) et à moitié allumé
		/// (« lightHalf », une lampe sur « halfChance »).
		BlockId lightOff = AIR, lightHalf = AIR;
		float halfChance = 0.f;
		BlockId lightAuto = AIR;  ///< « lightAuto » : néon à détecteur (s'allume à l'approche)
		float autoChance = 0.f;
		bool lightHanging = false; ///< néon pendu sous le plafond (dernière cellule libre) et non encastré
		/// « vent » : bloc posé DANS le plafond (grille de ventilation), dans une
		/// part « ventChance » des cellules, loin des murs et des néons.
		BlockId vent = AIR;
		float ventChance = 0.f;
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
	/// Niveau de la colonne (x, z) à l'étage `floor` (« floorLevels » : un
	/// niveau imposé par étage ; sinon les régions).
	[[nodiscard]] const MazeLevel &MazeLevelAt(int x, int z, int floor = 0) const;
	[[nodiscard]] bool MazeWall(const MazeLevel &level, int x, int z, int floor = 0) const;
	/// Mur de la colonne (x, z) à l'étage `floor`, frontières entre niveaux
	/// comprises (le hall devant l'ascenseur reste ouvert).
	[[nodiscard]] bool MazeColumnWall(int x, int z, int floor = 0) const;
	/// La cellule (cx, cz) a-t-elle une porte de sortie en son centre ?
	[[nodiscard]] bool MazeExitCell(int cx, int cz, int floor = 0) const;
	/// Panneau fléché vers la sortie la plus proche, sur le mur voisin de la
	/// colonne (x, z) ; AIR si aucun.
	[[nodiscard]] BlockId MazeExitSign(int x, int z, uint8_t &param, int floor = 0) const;
	/// Cage d'escalier montant de l'étage `floor` au suivant dans la cellule ?
	[[nodiscard]] bool MazeStairCell(int cx, int cz, int floor) const;
	/// Cellule réservée (gaine d'ascenseur et son hall) : ni escalier, ni
	/// bassin, ni sortie.
	[[nodiscard]] bool MazeReservedCell(int cx, int cz) const;
	/// Plancher (y) de l'étage `floor`.
	[[nodiscard]] int MazeFloorY(int floor) const { return m_mazeFloor + floor * (m_mazeHeight + 1); }
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
	BlockId m_mazeSignLeft = AIR, m_mazeSignRight = AIR; ///< panneaux muraux « ← EXIT », « EXIT → »
	int m_mazeSignRange = 2, m_mazeSignHeight = 3;
	int m_poolDepth = 2; ///< profondeur des bassins (« poolDepth »), eau au ras du sol
	int m_mazeFloors = 1;               ///< « floors » : étages superposés (dalle de 2 blocs entre eux)
	std::vector<int> m_floorLevels;     ///< « floorLevels » : niveau (indice) de chaque étage, −1 : régions
	BlockId m_stairBlock = AIR;         ///< « stairs.block » : marche (orientée, montant vers +x)
	BlockId m_stairFill = AIR;          ///< « stairs.fill » : sous les marches (sinon le mur du niveau)
	float m_stairChance = 0.f;          ///< « stairs.chance » : cellules à escalier, par étage
	/// « elevator » (une) ou « elevators » (plusieurs) : gaine traversant les
	/// étages `from`..`to`, un palier par étage, moteur au sommet (blocs du mod
	/// « elevators », la cabine est posée par son script). Centre au milieu de
	/// la cellule `cell`, portes côté `side`, gaine de côté `size` (impair, 5 à 9).
	struct MazeElevator {
		String id = "main";
		String side = "s";
		int cx = 0, cz = 0;
		int half = 3; ///< du centre aux parois de la gaine : (size − 1) / 2
		int dx = 0, dz = 1, ux = -1, uz = 0;
		uint8_t doorParam = 0;
		BlockId wall = AIR, rail = AIR, motor = AIR, door = AIR, doorTop = AIR, frame = AIR, call = AIR, display = AIR, arrow = AIR;
		int pit = 6, overrun = 7;
		int from = 0, to = 0; ///< étages desservis (bornes comprises)
		/// Bas (fond de la fosse) et haut (dernier vide sous le moteur) de la gaine.
		int bottom = 0, top = 0;
	};
	std::vector<MazeElevator> m_elevators;
	/// Gaine dont la cellule est (cx, cz) ; nullptr : aucune.
	[[nodiscard]] const MazeElevator *MazeShaftAt(int cx, int cz) const;
	/// Colonne d'une gaine d'ascenseur (x, z) : la pose et rend la gaine (nullptr : aucune).
	const MazeElevator *MazeElevatorColumn(Chunk &chunk, int lx, int lz, int x, int z);
	float m_exitChance = 0.003f;
	std::vector<MazeLevel> m_levels;
	float m_levelWeight = 0.f;
};

/// Générateur du monde : le GeneratorDef demandé (ou le premier), sinon un
/// monde plat de secours.
[[nodiscard]] std::shared_ptr<TerrainGenerator> MakeGenerator(const ContentRegistry &content, const String &generatorId,
															  uint32_t seed, TerrainGenerator::ScriptFn script = {});

} // namespace voxel
