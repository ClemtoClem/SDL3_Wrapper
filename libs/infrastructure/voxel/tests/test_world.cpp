// Tests — monde (chunks, blocs, événements, chargement/déchargement,
// persistance) et génération procédurale (voxel/world.hpp, worldgen.hpp).
#include "test.hpp"

#include <map>

#include <voxel/json.hpp>
#include <voxel/worldgen.hpp>

using namespace voxel;

namespace {

void Load(ContentRegistry &content, const char *text) {
	auto parsed = json::Parse(String(text));
	if (parsed.IsError() || content.Load(parsed.Unwrap(), {"t", "", "test"}).IsError())
		std::printf("définition refusée : %s\n", text);
}

/// Contenu minimal : pierre, terre, herbe, eau, sable, tronc, feuilles, une
/// plaine avec des arbres (structure qui déborde sur les chunks voisins).
void MakeContent(ContentRegistry &content) {
	Load(content, R"({"id": "stone", "graphics": {"texture": "stone"}})");
	Load(content, R"({"id": "dirt", "graphics": {"texture": "dirt"}})");
	Load(content, R"({"id": "grass", "graphics": {"texture": "grass"}})");
	Load(content, R"({"id": "water", "tags": ["fluid"], "physics": {"solid": false}, "graphics": {"texture": "water", "material": "TRANSPARENT"}})");
	Load(content, R"({"id": "sand", "simulation": {"falling": {"enabled": true}}})");
	Load(content, R"({"id": "log"})");
	Load(content, R"({"id": "leaves", "graphics": {"material": "CUTOUT"}})");
	Load(content, R"({"type": "structure", "id": "tree", "anchor": [2, 0, 2], "palette": {"L": "log", "#": "leaves"},
		"layers": [["", "", "  L"], ["", "", "  L"], ["#####", "#####", "##L##", "#####", "#####"], [" ### ", " ###", " #L# ", " ### "], ["", "  #"]]})");
	Load(content, R"({"type": "biome", "id": "plains", "climate": {"temperature": 0.5, "humidity": 0.5},
		"terrain": {"height": 20, "variation": 6}, "blocks": {"surface": "grass", "subsurface": "dirt", "filler": "stone", "underwater": "sand"},
		"decorations": [{"structure": "tree", "chance": 0.02, "on": ["grass"]}]})");
	Load(content, R"({"type": "biome", "id": "desert", "climate": {"temperature": 0.9, "humidity": 0.1},
		"terrain": {"height": 18, "variation": 3}, "blocks": {"surface": "sand", "subsurface": "sand", "filler": "stone"}})");
	Load(content, R"({"type": "generator", "id": "overworld", "seaLevel": 16, "fluid": "water", "caves": 0.5,
		"ores": [{"block": "dirt", "in": "stone", "chance": 0.01}]})");
	Load(content, R"({"type": "generator", "id": "flat", "kind": "flat", "layers": [["stone", 3], ["dirt", 2], ["grass", 1]]})");
}

/// Stockage en mémoire (la vraie sauvegarde est testée dans test_save).
struct MemoryStorage final : ChunkStorage {
	std::map<ChunkPos, std::vector<BlockId>> chunks;
	bool LoadChunk(Chunk &chunk) override {
		auto it = chunks.find(chunk.Pos());
		if (it == chunks.end())
			return false;
		chunk.Assign(it->second, {});
		return true;
	}
	void SaveChunk(const Chunk &chunk) override { chunks[chunk.Pos()] = chunk.Blocks(); }
};

} // namespace

TEST(World_CoordinatesFloorTowardMinusInfinity) {
	CHECK(ChunkOf({-1, 0, 15}) == (ChunkPos{-1, 0, 0}));
	CHECK(LocalOf({-1, -17, 16}) == (BlockPos{15, 15, 0}));
	CHECK(BlockPos::FromWorld({-0.2f, 3.9f, 0.f}) == (BlockPos{-1, 3, 0}));
	CHECK(Opposite(Face::TOP) == Face::BOTTOM && Opposite(Face::EAST) == Face::WEST);
}

TEST(World_SetBlockEventsAndChunkStorage) {
	ContentRegistry content;
	MakeContent(content);
	World world(content);
	int events = 0;
	BlockChange last;
	auto connection = world.onBlockChanged.Connect([&](const BlockChange &c) {
		++events;
		last = c;
	});
	CHECK(world.SetBlock({3, 40, -5}, String("t:stone"), 0, ChangeCause::PLAYER));
	CHECK(!world.SetBlock({3, 40, -5}, String("t:stone"))); // inchangé
	CHECK_EQ(events, 1);
	CHECK(last.previous == AIR && last.current == content.BlockIdOf("t:stone") && last.cause == ChangeCause::PLAYER);
	CHECK(world.BlockAt({3, 40, -5}).id == "t:stone");
	Chunk *chunk = world.GetChunk(ChunkOf({3, 40, -5}));
	REQUIRE(chunk != nullptr);
	CHECK(chunk->IsModified() && !chunk->IsUniform());
	// Remis à l'air : le chunk reste modifié (à enregistrer) mais vide.
	CHECK(world.SetBlock({3, 40, -5}, AIR));
	CHECK(chunk->IsEmpty());

	// Déchargement : un chunk modifié part dans le stockage et revient.
	MemoryStorage storage;
	world.SetStorage(&storage);
	CHECK(world.SetBlock({100, 5, 100}, String("t:dirt"), 0, ChangeCause::PLAYER));
	world.UnloadChunk(ChunkOf({100, 5, 100}));
	CHECK(!world.IsLoaded({100, 5, 100}));
	CHECK_EQ(storage.chunks.size(), size_t(1));
	(void)world.EnsureChunk(ChunkOf({100, 5, 100}));
	CHECK(world.BlockAt({100, 5, 100}).id == "t:dirt");
}

TEST(World_FallingBlocksAreReportedToTheSimulation) {
	ContentRegistry content;
	MakeContent(content);
	World world(content);
	(void)world.SetBlock({0, 10, 0}, String("t:sand"));
	auto unstable = world.TakeUnstableBlocks();
	REQUIRE_EQ(unstable.size(), size_t(1));
	CHECK(unstable[0] == (BlockPos{0, 10, 0}));
	(void)world.SetBlock({0, 20, 0}, String("t:stone"));
	(void)world.SetBlock({0, 21, 0}, String("t:sand"));
	CHECK(world.TakeUnstableBlocks().empty()); // soutenu
	(void)world.SetBlock({0, 20, 0}, AIR);       // on retire le support
	CHECK_EQ(world.TakeUnstableBlocks().size(), size_t(1));
}

TEST(WorldGen_FlatAndNoiseGeneratorsAreDeterministic) {
	ContentRegistry content;
	MakeContent(content);
	{
		World flat(content);
		flat.SetGenerator(MakeGenerator(content, "t:flat", 1));
		(void)flat.EnsureChunk({0, 0, 0});
		CHECK(flat.BlockAt({3, 0, 3}).id == "t:stone");
		CHECK(flat.BlockAt({3, 4, 3}).id == "t:dirt");
		CHECK(flat.BlockAt({3, 5, 3}).id == "t:grass");
		CHECK(flat.BlockAt({3, 6, 3}).IsAir());
		CHECK_EQ(flat.Generator()->SurfaceHeight(0, 0), 5);
	}
	// Deux mondes de même graine, chunks générés dans des ordres opposés :
	// mêmes blocs partout, arbres à cheval sur deux chunks compris.
	std::vector<ChunkPos> order;
	for (int y = 0; y <= 2; ++y)
		for (int z = -2; z <= 2; ++z)
			for (int x = -2; x <= 2; ++x)
				order.push_back({x, y, z});
	World a(content), b(content);
	a.SetGenerator(MakeGenerator(content, "t:overworld", 42));
	b.SetGenerator(MakeGenerator(content, "t:overworld", 42));
	for (const ChunkPos &p : order)
		(void)a.EnsureChunk(p);
	for (auto it = order.rbegin(); it != order.rend(); ++it)
		(void)b.EnsureChunk(*it);
	size_t differences = 0, logs = 0, water = 0;
	for (int y = 0; y < 3 * CHUNK_SIZE; ++y)
		for (int z = -2 * CHUNK_SIZE + 4; z < 3 * CHUNK_SIZE - 4; ++z)
			for (int x = -2 * CHUNK_SIZE + 4; x < 3 * CHUNK_SIZE - 4; ++x) {
				const BlockId ida = a.GetBlock({x, y, z});
				differences += ida != b.GetBlock({x, y, z});
				logs += content.Block(ida).id == "t:log";
				water += content.Block(ida).id == "t:water";
			}
	CHECK_EQ(differences, size_t(0));
	CHECK(logs > 0); // des arbres ont poussé
	(void)water;
	// La hauteur de surface annoncée est celle du terrain généré.
	const int h = a.Generator()->SurfaceHeight(5, 7);
	CHECK(a.BlockAt({5, h, 7}).solid || a.BlockAt({5, h, 7}).fluid || a.BlockAt({5, h, 7}).IsAir() == false);
	CHECK(a.Generator()->BiomeAt(5, 7) != nullptr);
}

TEST(World_LoadsNearestChunksWithinBudgetAndUnloadsFarOnes) {
	ContentRegistry content;
	MakeContent(content);
	World world(content);
	world.SetGenerator(MakeGenerator(content, "t:flat", 1));
	const BlockPos player{8, 8, 8};
	size_t loaded = 0;
	for (int i = 0; i < 20; ++i)
		loaded += world.UpdateLoadedArea({player}, 2, 1, 6);
	CHECK(world.IsLoaded(player));
	CHECK_EQ(loaded, world.LoadedChunkCount());
	const size_t count = world.LoadedChunkCount();
	CHECK(count > 20 && count < 80);
	// Le joueur part loin : l'ancienne zone est déchargée.
	for (int i = 0; i < 30; ++i)
		(void)world.UpdateLoadedArea({{1000, 8, 1000}}, 2, 1, 6);
	CHECK(!world.IsLoaded(player));
	CHECK(world.IsLoaded({1000, 8, 1000}));
}
