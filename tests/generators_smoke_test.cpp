// Tests unitaires — generators:: (génération procédurale) : hasard
// reproductible, bruits, cartes de hauteur et érosion, terrain en couches,
// dispersion, donjons, L-systèmes, WFC, noms de Markov.
//
// Écrit seulement sous build/tests/tmp/ (exports d'images et d'OBJ).
#define USE_TEST

#include "core/test.hpp"
#include "generators/generators.hpp"

#include <cmath>
#include <filesystem>

using namespace generators;

namespace {

bool EveryWalkableReachable(const Dungeon &d) {
	const Grid<int> dist = DistanceField(d.tiles, d.entrance);
	for (int y = 0; y < d.tiles.Height(); ++y)
		for (int x = 0; x < d.tiles.Width(); ++x)
			if (IsWalkable(d.tiles.At(x, y)) && dist.At(x, y) < 0)
				return false;
	return true;
}

} // namespace

TEST(Generators, RandomIsReproducibleAndUniform) {
	Rng a(42), b(42), c(43);
	for (int i = 0; i < 100; ++i)
		EXPECT_EQ(a.Next(), b.Next());
	EXPECT_TRUE(Rng(42).Next() != c.Next());
	Rng r(7);
	int counts[6] = {};
	for (int i = 0; i < 60000; ++i)
		++counts[r.Int(0, 5)];
	for (int k : counts)
		EXPECT_TRUE(k > 9000 && k < 11000);
	EXPECT_EQ(Rng(1).Weighted({0.0, 0.0, 5.0}), int64_t(2));
	EXPECT_EQ(Rng(1).Weighted({0.0, 0.0}), int64_t(-1));
	EXPECT_EQ(Hash2(3, 4, 9), Hash2(3, 4, 9));
}

TEST(Generators, NoisesAreBoundedSmoothAndSeeded) {
	for (NoiseType type : {NoiseType::PERLIN, NoiseType::SIMPLEX, NoiseType::VALUE, NoiseType::WORLEY})
		for (FractalType fractal : {FractalType::NONE, FractalType::FBM, FractalType::RIDGED, FractalType::BILLOW,
									FractalType::PING_PONG}) {
			NoiseSettings s;
			s.type = type;
			s.fractal = fractal;
			s.seed = 5;
			s.frequency = 0.05f;
			Noise noise(s);
			float lo = 1e9f, hi = -1e9f, jump = 0.f;
			for (int y = 0; y < 60; ++y)
				for (int x = 0; x < 60; ++x) {
					const float v = noise.Sample(float(x), float(y));
					lo = std::min(lo, v);
					hi = std::max(hi, v);
					jump = std::max(jump, std::fabs(noise.Sample(float(x) + 0.01f, float(y)) - v));
					EXPECT_TRUE(noise.Sample01(float(x), float(y)) >= 0.f && noise.Sample01(float(x), float(y)) <= 1.f);
				}
			EXPECT_TRUE(lo >= -1.6f && hi <= 1.6f);
			EXPECT_TRUE(hi > lo);   // pas constant
			EXPECT_TRUE(jump < 0.5f); // continu (un pas minuscule, un écart minuscule)
		}
	NoiseSettings s;
	s.seed = 1;
	Noise a(s), b(s);
	s.seed = 2;
	Noise c(s);
	EXPECT_TRUE(a.Sample(12.5f, 7.25f) == b.Sample(12.5f, 7.25f));
	EXPECT_TRUE(a.Sample(12.5f, 7.25f) != c.Sample(12.5f, 7.25f));
	s.warpAmplitude = 30.f;
	Noise warped(s);
	EXPECT_TRUE(warped.Sample(12.5f, 7.25f) != c.Sample(12.5f, 7.25f));
	EXPECT_TRUE(std::isfinite(warped.Sample(1.f, 2.f, 3.f)));
}

TEST(Generators, HeightmapFiltersMasksAndErosion) {
	HeightMap map(64, 64);
	NoiseSettings s;
	s.frequency = 0.05f;
	map.Fill(Noise(s)).Normalize();
	EXPECT_TRUE(std::fabs(map.Min()) < 1e-6f && std::fabs(map.Max() - 1.f) < 1e-6f);
	HeightMap terraced = map;
	terraced.Terrace(4, 1.f);
	for (float v : terraced.Data())
		EXPECT_TRUE(std::fabs(v * 4.f - std::round(v * 4.f)) < 1e-4f); // marches nettes
	HeightMap curved = map;
	curved.Curve({{0.f, 0.f}, {0.5f, 0.1f}, {1.f, 1.f}});
	EXPECT_TRUE(curved.Mean() < map.Mean());
	HeightMap smooth = map;
	smooth.Smooth(3.f);
	EXPECT_TRUE(smooth.SlopeMap().Mean() < map.SlopeMap().Mean());
	const HeightMap mask = map.HeightMask(0.4f, 0.6f, 0.05f);
	EXPECT_TRUE(mask.Min() >= 0.f && mask.Max() <= 1.f);

	// Érosions : la matière bouge (et ne se crée pas).
	HeightMap eroded = map;
	HydraulicErosion rain;
	rain.droplets = 4000;
	rain.seed = 3;
	eroded.Scale(20.f).ErodeHydraulic(rain);
	HeightMap reference = map;
	reference.Scale(20.f);
	double moved = 0.0;
	for (size_t i = 0; i < eroded.Size(); ++i)
		moved += std::fabs(eroded.Data()[i] - reference.Data()[i]);
	EXPECT_TRUE(moved > 1.0);
	EXPECT_TRUE(eroded.Mean() <= reference.Mean() + 1e-3f);
	HeightMap slid = map;
	ThermalErosion thermal;
	thermal.iterations = 20;
	slid.Scale(10.f).ErodeThermal(thermal);
	double before = 0.0, after = 0.0;
	for (size_t i = 0; i < map.Size(); ++i) {
		before += map.Data()[i] * 10.f;
		after += slid.Data()[i];
	}
	EXPECT_TRUE(std::fabs(before - after) < 1e-2 * before); // conservation de la matière
}

TEST(Generators, LayeredTerrainBiomesMeshAndExports) {
	TerrainSettings t = DefaultTerrain(9);
	t.width = t.height = 65;
	t.islandFalloff = 2.f;
	HydraulicErosion rain;
	rain.droplets = 2000;
	t.erosion = Some(rain);
	t.seaLevel = 0.2f;
	const HeightMap map = GenerateTerrain(t);
	EXPECT_TRUE(map.Min() >= 0.2f - 1e-6f && map.Max() <= 1.f + 1e-6f);
	// Reproductible.
	EXPECT_TRUE(GenerateTerrain(t).Data() == map.Data());

	const std::vector<BiomeRule> biomes = DefaultBiomes(0.3f);
	EXPECT_EQ(biomes[size_t(ClassifyBiome(biomes, 0.95f, 5.f))].name, "neige");
	EXPECT_EQ(biomes[size_t(ClassifyBiome(biomes, 0.5f, 60.f))].name, "falaise");
	const std::vector<float> weights = BiomeWeights(biomes, 0.5f, 10.f);
	float total = 0.f;
	for (float w : weights)
		total += w;
	EXPECT_TRUE(std::fabs(total - 1.f) < 1e-4f);

	TerrainMeshSettings meshSettings;
	meshSettings.step = 2;
	const TerrainMesh mesh = BuildTerrainMesh(map, meshSettings, biomes);
	EXPECT_EQ(mesh.VertexCount(), size_t(33 * 33));
	EXPECT_EQ(mesh.TriangleCount(), size_t(32 * 32 * 2));
	EXPECT_EQ(mesh.colors.size(), mesh.positions.size());

	std::error_code ignored;
	std::filesystem::create_directories("build/tests/tmp/generators", ignored);
	EXPECT_TRUE(map.SavePgm(String("build/tests/tmp/generators/terrain.pgm"), true).IsNone());
	EXPECT_TRUE(map.SaveRaw16(String("build/tests/tmp/generators/terrain.r16")).IsNone());
	EXPECT_TRUE(mesh.SaveObj(String("build/tests/tmp/generators/terrain.obj")).IsNone());
	EXPECT_EQ(std::filesystem::file_size("build/tests/tmp/generators/terrain.r16"), uintmax_t(65 * 65 * 2));
	EXPECT_TRUE(map.SavePgm(String("build/tests/tmp/absent/x.pgm")).IsSome());
}

TEST(Generators, PoissonDiskKeepsItsDistance) {
	const std::vector<Point2> points = PoissonDisk(100.f, 60.f, 5.f, 11);
	EXPECT_TRUE(points.size() > 80);
	for (size_t i = 0; i < points.size(); ++i)
		for (size_t j = i + 1; j < points.size(); ++j) {
			const float dx = points[i].x - points[j].x, dy = points[i].y - points[j].y;
			EXPECT_TRUE(dx * dx + dy * dy >= 25.f - 1e-3f);
		}
	// Densité nulle à gauche : aucun point dans la moitié gauche.
	const auto sparse = PoissonDisk(100.f, 60.f, 5.f, 11, [](float x, float) { return x < 50.f ? 0.f : 1.f; });
	for (const Point2 &p : sparse)
		EXPECT_TRUE(p.x >= 50.f);
	EXPECT_EQ(JitteredGrid(10.f, 10.f, 5.f, 0.f, 1).size(), size_t(4));
}

TEST(Generators, DungeonsAreConnected) {
	const Dungeon maze = GenerateMaze(31, 21, 4);
	EXPECT_TRUE(EveryWalkableReachable(maze));
	// Labyrinthe parfait : (cases) - 1 passages, donc aucune boucle.
	size_t walkable = 0;
	for (Tile t : maze.tiles.Cells())
		walkable += IsWalkable(t) ? 1 : 0;
	EXPECT_EQ(walkable, size_t(15 * 10 * 2 - 1));
	for (MazeAlgorithm algo : {MazeAlgorithm::PRIM, MazeAlgorithm::BINARY_TREE})
		EXPECT_TRUE(EveryWalkableReachable(GenerateMaze(25, 25, 8, algo)));

	RoomsAndMazesConfig c;
	c.seed = 12;
	const Dungeon rooms = GenerateRoomsAndMazes(c);
	EXPECT_TRUE(rooms.rooms.size() >= 5);
	EXPECT_TRUE(EveryWalkableReachable(rooms));
	EXPECT_EQ(rooms.Count(Tile::ENTRANCE), size_t(1));
	EXPECT_EQ(rooms.Count(Tile::EXIT), size_t(1));
	EXPECT_TRUE(rooms.entranceRoom != rooms.exitRoom);
	// Sans culs-de-sac : chaque couloir a au moins deux issues.
	for (int y = 1; y < rooms.tiles.Height() - 1; ++y)
		for (int x = 1; x < rooms.tiles.Width() - 1; ++x)
			if (rooms.tiles.At(x, y) == Tile::CORRIDOR) {
				int exits = 0;
				for (int d = 0; d < 4; ++d)
					exits += IsWalkable(rooms.tiles.At(x + DIR_X[d], y + DIR_Y[d])) ? 1 : 0;
				EXPECT_TRUE(exits >= 2);
			}
	// Reproductible, et la configuration 3D historique s'y branche.
	EXPECT_TRUE(GenerateRoomsAndMazes(c).tiles.Cells() == rooms.tiles.Cells());
	EXPECT_TRUE(EveryWalkableReachable(GenerateRoomsAndMazes(FromConfig3D(maze::Config3D{}, 41, 41, 3))));

	BspConfig bsp;
	bsp.seed = 5;
	const Dungeon partitioned = GenerateBsp(bsp);
	EXPECT_TRUE(partitioned.rooms.size() >= 4);
	EXPECT_TRUE(EveryWalkableReachable(partitioned));

	CaveConfig caves;
	caves.seed = 6;
	const Dungeon cave = GenerateCaves(caves);
	EXPECT_TRUE(cave.Count(Tile::FLOOR) > 200);
	EXPECT_TRUE(EveryWalkableReachable(cave));
	EXPECT_EQ(Regions(cave.tiles).second, 1);
	EXPECT_EQ(rooms.Rows().size(), size_t(rooms.tiles.Height()));
}

TEST(Generators, LSystemsTurtlesWfcAndNames) {
	const String koch = ExpandLSystem(String("F"), {{'F', String("F+F-F-F+F"), 1.f}}, 2);
	EXPECT_EQ(koch.GetSize(), size_t(5 * 9 + 4)); // chacun des 5 F devient 9 symboles, + 4 signes
	TurtleSettings turtle;
	turtle.angleDegrees = 90.f;
	const auto segments = InterpretTurtle(String("F[+F]F"), turtle);
	EXPECT_EQ(segments.size(), size_t(3));
	EXPECT_TRUE(std::fabs(segments[2].by - 2.f) < 1e-5f); // la branche n'a pas déplacé le tronc
	EXPECT_EQ(segments[1].depth, 1);
	// Stochastique mais reproductible.
	const std::vector<LRule> bush = {{'F', String("F[+F]"), 1.f}, {'F', String("F[-F]"), 1.f}};
	EXPECT_EQ(ExpandLSystem(String("F"), bush, 4, 7), ExpandLSystem(String("F"), bush, 4, 7));

	// WFC : herbe / côte / eau — l'eau ne touche jamais l'herbe.
	std::vector<WfcTile> tiles = {
		{String("herbe"), {String("h"), String("h"), String("h"), String("h")}, 3.f},
		{String("eau"), {String("e"), String("e"), String("e"), String("e")}, 2.f},
		{String("côte H"), {String("h"), String("h"), String("e"), String("e")}, 1.f},
		{String("côte B"), {String("e"), String("e"), String("h"), String("h")}, 1.f},
	};
	WaveFunctionCollapse wfc(tiles);
	auto result = wfc.Run(20, 20, 4);
	ASSERT_TRUE(result.IsOk());
	const WfcResult &grid = result.Value();
	for (int y = 0; y < 20; ++y)
		for (int x = 0; x + 1 < 20; ++x)
			EXPECT_EQ(tiles[size_t(grid.At(x, y))].sides[1], tiles[size_t(grid.At(x + 1, y))].sides[3]);
	// Le haut (« a ») ne va jamais contre le bas (« c ») : aucune grille possible.
	std::vector<WfcTile> impossible = {{String("a"), {String("a"), String("b"), String("c"), String("b")}, 1.f}};
	EXPECT_TRUE(WaveFunctionCollapse(impossible).Run(3, 3, 1, 2).IsError());

	MarkovNames names({String("aldoria"), String("belmora"), String("cendrial"), String("dravonne"), String("eldrin"),
					   String("falorin"), String("galdor")},
					  2);
	Rng rng(5);
	const String name = names.Generate(rng, 4, 10);
	EXPECT_TRUE(name.GetSize() >= 4 && name.GetSize() <= 10);
	EXPECT_TRUE(MarkovNames({}, 2).Generate(rng).IsEmpty());
}

int main() { return RUN_ALL_TESTS(); }
