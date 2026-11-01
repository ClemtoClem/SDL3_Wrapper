// Tests — éditeurs de biomes, générateurs et dimensions de Voxelia Studio
// sans fenêtre : documents du contenu réel, validation à blanc, réglages de
// mod référencés, dimension d'aperçu, aperçu autonome (monde ordinaire,
// labyrinthe des backrooms coupé au plafond, biome modifié qui change le
// terrain sans toucher la partie), plan du labyrinthe (étages, graines,
// gaines) et graphe des dimensions (chaîne des backrooms).
#include "test.hpp"

#include <algorithm>
#include <filesystem>

#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxelia/studio/worldgen_document.hpp>

using namespace voxelia::studio;
namespace json = voxel::json;
using voxel::BlockPos;

namespace {

voxel::GameOptions WorldgenOptions(const char *saves) {
	voxel::GameOptions o;
	o.modDirectories = {String("demos/voxelia/mods")};
	o.savesDirectory = String(saves);
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 200;
	return o;
}

bool Has(const std::vector<WorldgenDiagnostic> &list, const char *text) {
	return std::any_of(list.begin(), list.end(), [&](const WorldgenDiagnostic &d) { return d.message.Contains(text); });
}

/// Une partie ouverte (graine fixe) : la graine des dimensions en dépend.
struct Opened {
	voxel::Game game;
	explicit Opened(const char *saves) {
		std::filesystem::remove_all(saves);
		(void)game.LoadMods(WorldgenOptions(saves));
		REQUIRE(game.OpenWorld("worldgen", Some(uint32_t(42))).IsOk());
	}
};

int CountBlock(const WorldgenPreview &p, const String &id) {
	const voxel::BlockId want = p.content->BlockIdOf(id);
	int n = 0;
	for (const voxel::ChunkPos &cp : p.chunks) {
		const voxel::Chunk *c = p.world->GetChunk(cp);
		for (int y = 0; c && y < voxel::CHUNK_SIZE; ++y)
			for (int z = 0; z < voxel::CHUNK_SIZE; ++z)
				for (int x = 0; x < voxel::CHUNK_SIZE; ++x)
					n += c->Get(x, y, z) == want;
	}
	return n;
}

} // namespace

TEST(StudioWorldgen_DocumentsValidationSettings) {
	Opened o("build/tests/tmp/voxelia/studio_worldgen_docs");
	voxel::Game &game = o.game;
	Option<WorldgenDocument> dim = WorldgenDocument::FromContent(game.Content(), WorldgenKind::Dimension, "backrooms:backrooms");
	REQUIRE(dim.IsSome());
	CHECK(dim.Value().SourceFile().Contains("world.json"));
	CHECK(dim.Value().Validate(game.Content()).empty());
	CHECK_EQ(dim.Value().PreviewDimension(game.Content()), String("backrooms:backrooms"));
	CHECK(WorldgenDocument::FromContent(game.Content(), WorldgenKind::Biome, "core:absent").IsNone());
	// Générateur du labyrinthe : ses réglages de mod (étages par niveau).
	WorldgenDocument gen = WorldgenDocument::FromContent(game.Content(), WorldgenKind::Generator, "backrooms:backrooms").Unwrap();
	const std::vector<SettingRef> settings = gen.Settings();
	REQUIRE(!settings.empty());
	CHECK(std::any_of(settings.begin(), settings.end(), [](const SettingRef &s) { return s.id == "floors_level_0" && s.fallback.Contains("number_of_floors_per_level"); }));
	CHECK_EQ(gen.PreviewDimension(game.Content()), String("backrooms:backrooms"));
	// Diagnostics : références inconnues, valeurs hors bornes ; contenu vivant intact.
	gen.SetField("bedrock", json::MakeStr(String("core:socle_absent")));
	gen.SetField("kind", json::MakeStr(String("spirale")));
	const std::vector<WorldgenDiagnostic> genDiags = gen.Validate(game.Content());
	CHECK(Has(genDiags, "socle_absent"));
	CHECK(Has(genDiags, "spirale"));
	CHECK_EQ(game.Content().FindGenerator("backrooms:backrooms")->kind, String("maze"));
	WorldgenDocument biome = WorldgenDocument::FromContent(game.Content(), WorldgenKind::Biome, "core:living_jungle").Unwrap();
	biome.SetField("blocks.surface", json::MakeStr(String("core:mousse_absente")));
	biome.SetField("climate.temperature", json::MakeNum(3.0));
	const std::vector<WorldgenDiagnostic> biomeDiags = biome.Validate(game.Content());
	CHECK(Has(biomeDiags, "mousse_absente"));
	WorldgenDocument dimension = WorldgenDocument::FromContent(game.Content(), WorldgenKind::Dimension, "backrooms:level_2").Unwrap();
	dimension.SetField("generator", json::MakeStr(String("backrooms:nulle_part")));
	CHECK(Has(dimension.Validate(game.Content()), "nulle_part"));
	CHECK(dimension.SetJson(String(R"({"type": "dimension"})")).IsOk());
	const std::vector<WorldgenDiagnostic> broken = dimension.Validate(game.Content());
	REQUIRE(!broken.empty());
	CHECK(broken.front().severity == WorldgenDiagnostic::Severity::Error);
	// Nouveau biome : valide tel quel.
	WorldgenDocument fresh = WorldgenDocument::New(WorldgenKind::Biome, String("prairie_test"), String("core"), String(), String());
	CHECK(fresh.Validate(game.Content()).empty());
	game.CloseWorld();
}

TEST(StudioWorldgen_PreviewIsStandaloneAndFollowsTheDocument) {
	Opened o("build/tests/tmp/voxelia/studio_worldgen_preview");
	voxel::Game &game = o.game;
	const String overworld = game.Content().DefaultDimension();
	// Monde ordinaire : 2 × 2 chunks autour de la surface, la partie n'en sait rien.
	const size_t loadedBefore = game.GetWorld().LoadedChunks().size();
	Result<WorldgenPreview, String> p = BuildPreview(game, game.Content(), overworld, BlockPos{0, 0, 0}, 2, NONE);
	REQUIRE(p.IsOk());
	CHECK(!p.Value().chunks.empty());
	CHECK_EQ(game.GetWorld().LoadedChunks().size(), loadedBefore);
	CHECK(p.Value().generator->SurfaceHeight(0, 0) == game.DimensionGenerator(game.Content(), overworld)->SurfaceHeight(0, 0));
	CHECK(BuildPreview(game, game.Content(), overworld, BlockPos{0, 0, 0}, 0, NONE).IsError());
	// Biome du monde ordinaire modifié (surface) : l'aperçu le montre, pas la partie.
	const voxel::BiomeDef *here = p.Value().generator->BiomeAt(0, 0);
	REQUIRE(here != nullptr);
	WorldgenDocument biome = WorldgenDocument::FromContent(game.Content(), WorldgenKind::Biome, here->id).Unwrap();
	biome.SetField("blocks.surface", json::MakeStr(String("core:gold_block")));
	Result<voxel::ContentRegistry, String> scratch = biome.Scratch(game.Content());
	REQUIRE(scratch.IsOk());
	Result<WorldgenPreview, String> golden = BuildPreview(game, scratch.Value(), overworld, BlockPos{0, 0, 0}, 2, NONE);
	REQUIRE(golden.IsOk());
	CHECK(CountBlock(golden.Value(), String("core:gold_block")) > 50);
	CHECK_EQ(CountBlock(p.Value(), String("core:gold_block")), 0);
	CHECK(game.Content().FindBiome(here->id)->surface != "core:gold_block");
	// Labyrinthe des backrooms : étages, graines « + 7919 × étage », coupe au premier plafond.
	Result<WorldgenPreview, String> maze = BuildPreview(game, game.Content(), String("backrooms:backrooms"), BlockPos{0, 0, 0}, 2, NONE);
	REQUIRE(maze.IsOk());
	const voxel::MazeLayout layout = maze.Value().generator->Maze();
	REQUIRE(!layout.floors.empty());
	CHECK_EQ(maze.Value().cutY, layout.floors.front().ceilingY - 1);
	for (const voxel::MazeLayout::Floor &f : layout.floors)
		CHECK_EQ(f.seed, game.DimensionSeed(String("backrooms:backrooms")) + uint32_t(f.index) * 7919u);
	CHECK(!layout.levels.empty());
	CHECK(!layout.shafts.empty()); // la gaine d'ascenseur du hall
	// Au-dessus de la coupe : rien.
	int above = 0;
	for (const voxel::ChunkPos &cp : maze.Value().chunks)
		if (const voxel::Chunk *c = maze.Value().world->GetChunk(cp))
			for (int y = 0; y < voxel::CHUNK_SIZE; ++y)
				if (cp.Origin().y + y > maze.Value().cutY)
					for (int z = 0; z < voxel::CHUNK_SIZE; ++z)
						for (int x = 0; x < voxel::CHUNK_SIZE; ++x)
							above += c->Get(x, y, z) != voxel::AIR;
	CHECK_EQ(above, 0);
	// Un générateur non labyrinthe n'a pas de plan.
	CHECK(p.Value().generator->Maze().floors.empty());
	game.CloseWorld();
}

TEST(StudioWorldgen_DimensionGraphDrawsTheBackroomsChain) {
	voxel::Game game;
	(void)game.LoadMods(WorldgenOptions("build/tests/tmp/voxelia/studio_worldgen_graph"));
	const DimensionGraph g = BuildDimensionGraph(game.Content());
	REQUIRE(!g.nodes.empty());
	CHECK_EQ(g.nodes.front(), game.Content().DefaultDimension());
	auto link = [&](const char *from, const char *to, const char *kind) {
		return std::any_of(g.links.begin(), g.links.end(),
						   [&](const DimensionLink &l) { return l.from == from && l.to == to && l.kind == kind; });
	};
	// Mort par suffocation : le niveau 0 ; puis les ascenseurs 0 → 2 → 4 → 5.
	CHECK(link("*", "backrooms:backrooms", "death"));
	CHECK(link("backrooms:backrooms", "backrooms:level_2", "elevator"));
	CHECK(link("backrooms:level_2", "backrooms:level_4", "elevator"));
	CHECK(link("backrooms:level_4", "backrooms:level_5", "elevator"));
	for (const DimensionLink &l : g.links)
		if (l.kind != "portal" && l.to != "@return")
			CHECK(std::find(g.nodes.begin(), g.nodes.end(), l.to) != g.nodes.end() || l.to.Contains("surface"));
}
