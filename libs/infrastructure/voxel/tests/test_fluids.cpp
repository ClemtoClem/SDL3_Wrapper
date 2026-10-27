// Tests — liquides (voxel/fluids.hpp) : étalement depuis une source, chute,
// retrait, sources renouvelables, mélange lave + eau, obstacles, courant,
// rendu en pente, seaux et règle « liquidFlow ». Écrit UNIQUEMENT sous
// build/tests/tmp/voxel/.
#include "test.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

#include <voxel/fluids.hpp>
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/meshing.hpp>
#include <voxel/worldgen.hpp>

using namespace voxel;
namespace fs = std::filesystem;

namespace {

void Load(ContentRegistry &content, const char *text) {
	auto parsed = json::Parse(String(text));
	if (parsed.IsError() || content.Load(parsed.Unwrap(), {"t", "", "test"}).IsError())
		std::printf("définition refusée : %s\n", text);
}

/// Sol de pierre (y < 10), eau renouvelable (portée 7), lave (portée 3,
/// figée par l'eau), herbe haute, liquide immobile.
struct Pond {
	ContentRegistry content;
	std::unique_ptr<World> world;
	std::unique_ptr<FluidSimulation> fluids;
	Pond() {
		Load(content, R"({"id": "stone"})");
		Load(content, R"({"id": "obsidian"})");
		Load(content, R"({"id": "tall_grass", "shape": "cross", "physics": {"solid": false}, "gameplay": {"replaceable": true}})");
		Load(content, R"({"id": "water", "tags": ["fluid"], "physics": {"solid": false}, "graphics": {"material": "TRANSPARENT"},
			"liquid": {"range": 7, "tick": 0.25, "renewable": true}})");
		Load(content, R"({"id": "lava", "tags": ["fluid"], "physics": {"solid": false},
			"liquid": {"range": 3, "tick": 1.5, "mix": [{"with": "water", "source": "obsidian", "flowing": "stone"}]}})");
		Load(content, R"({"id": "still", "tags": ["fluid"], "physics": {"solid": false}, "liquid": false})");
		Load(content, R"({"type": "generator", "id": "flat", "kind": "flat", "layers": [["stone", 10]]})");
		world = std::make_unique<World>(content);
		world->SetGenerator(MakeGenerator(content, "t:flat", 1));
		for (int y = 0; y <= 1; ++y)
			for (int z = -2; z <= 1; ++z)
				for (int x = -2; x <= 1; ++x)
					(void)world->EnsureChunk({x, y, z});
		fluids = std::make_unique<FluidSimulation>(*world);
	}
	void Put(const BlockPos &p, const char *id, uint8_t param = 0) { (void)world->SetBlock(p, String(id), param, ChangeCause::PLAYER); }
	[[nodiscard]] String At(const BlockPos &p) const { return world->BlockAt(p).id; }
	[[nodiscard]] int Level(const BlockPos &p) const { return LiquidLevel(world->GetParam(p)); }
};

} // namespace

TEST(Fluids_LevelsLiveBesideTheOrientationBits) {
	CHECK(LiquidSource(0));
	CHECK(LiquidSource(3)); // orientation seule : toujours une source
	CHECK_EQ(LiquidLevel(LiquidParam(5, false)), 5);
	CHECK(LiquidFalling(LiquidParam(0, true)));
	CHECK(!LiquidSource(LiquidParam(0, true)));
	CHECK_EQ(int(LiquidParam(7, true) & 3u), 0);
	// Hauteur : source 7/8, décroissante, pleine sous le même liquide.
	ContentRegistry content;
	Load(content, R"({"id": "water", "tags": ["fluid"], "liquid": {"range": 7}})");
	const BlockDef &water = *content.FindBlock("t:water");
	CHECK_NEAR(LiquidHeight(water, 0, false), 0.875f, 1e-6f);
	CHECK(LiquidHeight(water, LiquidParam(1, false), false) < 0.875f);
	CHECK(LiquidHeight(water, LiquidParam(7, false), false) < LiquidHeight(water, LiquidParam(6, false), false));
	CHECK_NEAR(LiquidHeight(water, LiquidParam(3, false), true), 1.f, 1e-6f);
}

TEST(Fluids_WaterSpreadsFromASourceAndDriesWhenItIsRemoved) {
	Pond s;
	s.Put({0, 10, 0}, "t:water");
	CHECK(s.fluids->Settle() > 0);
	// Distance de Manhattan = niveau, jusqu'à la portée (7).
	CHECK(s.At({0, 10, 0}) == "t:water");
	CHECK_EQ(s.Level({0, 10, 0}), 0);
	CHECK_EQ(s.Level({3, 10, 0}), 3);
	CHECK_EQ(s.Level({-3, 10, 4}), 7);
	CHECK(s.At({7, 10, 0}) == "t:water");
	CHECK(s.world->BlockAt({8, 10, 0}).IsAir());
	CHECK(s.world->BlockAt({4, 10, 4}).IsAir());
	CHECK(s.world->BlockAt({0, 11, 0}).IsAir()); // ne monte pas
	// Courant : du haut vers le bas de la pente.
	const math::FVector3 flow = LiquidFlow(*s.world, {3, 10, 0});
	CHECK(flow.x > 0.9f);
	CHECK(std::fabs(LiquidFlow(*s.world, {0, 10, 0}).x) < 1e-4f); // source au centre : symétrique
	// Sans source, tout se retire.
	s.Put({0, 10, 0}, "core:air");
	(void)s.fluids->Settle();
	for (int x = -8; x <= 8; ++x)
		for (int z = -8; z <= 8; ++z)
			CHECK(s.world->BlockAt({x, 10, z}).IsAir());
	CHECK_EQ(s.fluids->Pending(), size_t(0));
}

TEST(Fluids_WaterFallsThenSpreadsWhereItLands) {
	Pond s;
	s.Put({0, 16, 0}, "t:water");
	(void)s.fluids->Settle();
	for (int y = 11; y <= 15; ++y) {
		CHECK(s.At({0, y, 0}) == "t:water");
		CHECK(LiquidFalling(s.world->GetParam({0, y, 0})));
	}
	CHECK(LiquidFalling(s.world->GetParam({0, 10, 0})));
	// Étalé au sol depuis le pied des chutes (la source en l'air alimente
	// aussi ses quatre voisines, qui tombent à leur tour).
	CHECK_EQ(s.Level({3, 10, 0}), 2);
	CHECK_NEAR(LiquidFlow(*s.world, {0, 13, 0}).y, -1.f, 1e-6f);
	// La source en l'air s'étale d'une cellule, qui tombe aussitôt (elle ne
	// s'étale pas plus loin tant qu'elle peut descendre).
	CHECK(s.At({1, 16, 0}) == "t:water");
	CHECK(s.world->BlockAt({2, 16, 0}).IsAir());
	CHECK(LiquidFalling(s.world->GetParam({1, 15, 0})));
}

TEST(Fluids_TwoSourcesRenewAThirdAndWallsStopTheFlow) {
	Pond s;
	// Une tranchée de trois de long, murée.
	for (int x = -1; x <= 3; ++x)
		for (int z = -1; z <= 1; ++z)
			if (z != 0 || x == -1 || x == 3)
				s.Put({x, 10, z}, "t:stone");
	s.Put({0, 10, 0}, "t:water");
	s.Put({2, 10, 0}, "t:water");
	(void)s.fluids->Settle();
	CHECK(LiquidSource(s.world->GetParam({1, 10, 0}))); // nouvelle source
	s.Put({0, 10, 0}, "core:air");
	(void)s.fluids->Settle();
	CHECK(LiquidSource(s.world->GetParam({1, 10, 0}))); // reste une source
	CHECK(s.At({0, 10, 0}) == "t:water");                // et ré-alimente sa voisine
	CHECK(s.world->BlockAt({0, 11, 0}).IsAir());
	CHECK(s.At({-1, 10, 0}) == "t:stone"); // le mur arrête l'eau
	// Un liquide immobile ne bouge pas.
	s.Put({20, 10, 20}, "t:still");
	CHECK_EQ(s.fluids->Settle(), 0);
	CHECK(s.world->BlockAt({21, 10, 20}).IsAir());
}

TEST(Fluids_LavaHardensOnWaterAndGrassIsWashedAway) {
	Pond s;
	int washed = 0;
	s.fluids->onWashedAway = [&](const BlockPos &, const BlockDef &def) { washed += def.id == "t:tall_grass"; };
	s.Put({3, 10, 0}, "t:tall_grass");
	s.Put({0, 10, 0}, "t:water");
	(void)s.fluids->Settle();
	CHECK_EQ(washed, 1);
	CHECK(s.At({3, 10, 0}) == "t:water");
	s.Put({0, 10, 1}, "t:lava"); // une source de lave posée contre la source d'eau
	(void)s.fluids->Settle();
	CHECK(s.At({0, 10, 1}) == "t:obsidian");
	// Lave qui coule et rencontre de l'eau : pierre.
	Pond t;
	t.Put({0, 10, 0}, "t:lava");
	(void)t.fluids->Settle();
	CHECK_EQ(t.Level({2, 10, 0}), 2);
	CHECK(t.world->BlockAt({4, 10, 0}).IsAir()); // portée 3
	t.Put({3, 10, 0}, "t:water");
	t.fluids->Step(0.f); // pas encore : la lave est lente
	(void)t.fluids->Settle();
	CHECK(t.At({2, 10, 0}) == "t:stone");
	CHECK(t.At({0, 10, 0}) == "t:lava");
}

TEST(Mesher_FlowingWaterSlopesDownFromItsSource) {
	Pond s;
	AssetManager assets;
	TextureAtlas atlas;
	assets.Declare(s.content);
	atlas.Build(assets, s.content, 16);
	s.Put({0, 10, 0}, "t:water");
	(void)s.fluids->Settle();
	ChunkMesher mesher(s.content, atlas, &assets);
	const ChunkMesh mesh = mesher.Build(*s.world, {0, 0, 0});
	// Sommets de la surface au-dessus des cellules x = 1 et x = 6 (z = 0).
	float near = 0.f, far = 1e9f;
	for (const MeshVertex &v : mesh.translucent.vertices)
		if (v.normal.y > 0.5f && v.position.z >= 0.f && v.position.z <= 1.f) {
			if (v.position.x >= 1.f && v.position.x <= 2.f)
				near = std::max(near, v.position.y);
			if (v.position.x >= 6.f && v.position.x <= 7.f)
				far = std::min(far, v.position.y);
		}
	CHECK(near > 10.6f);
	CHECK(far < 10.4f);
	CHECK(near > far);
}

TEST(Game_BucketsCarryWaterAndTheCurrentCarriesItems) {
	const fs::path root("build/tests/tmp/voxel/fluids_game");
	fs::remove_all(root);
	fs::create_directories(root / "mods" / "liq");
	std::ofstream(root / "mods" / "liq" / "mod.json") << R"({"id": "liq", "name": "Liquides", "version": "1.0"})";
	std::ofstream(root / "mods" / "liq" / "content.json") << R"([
		{"type": "texture", "id": "grey", "procedural": {"color": [128, 128, 128]}},
		{"id": "stone", "graphics": {"texture": "grey"}},
		{"id": "water", "tags": ["fluid"], "physics": {"solid": false}, "graphics": {"texture": "grey", "material": "TRANSPARENT"},
		 "liquid": {"range": 7, "tick": 0.25, "renewable": true, "current": 2.5}},
		{"type": "item", "id": "bucket", "icon": "grey", "liquids": {"liq:water": "liq:water_bucket"}},
		{"type": "item", "id": "water_bucket", "icon": "grey", "places": "liq:water", "emptiesTo": "bucket", "maxStack": 1},
		{"type": "item", "id": "pebble", "icon": "grey"},
		{"type": "generator", "id": "flat", "kind": "flat", "layers": [["stone", 10]]},
		{"type": "config", "values": {"world": {"generator": "liq:flat"}}}
	])";
	GameOptions o;
	o.modDirectories = {String((root / "mods").string().c_str())};
	o.savesDirectory = String((root / "saves").string().c_str());
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 64;
	Game game;
	REQUIRE(game.LoadMods(o).errors.empty());
	REQUIRE(game.OpenWorld("monde", Some(uint32_t(2))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Noé");
	auto run = [&](float seconds) {
		for (float t = 0.f; t < seconds; t += 1.f / 60.f)
			game.Tick(1.f / 60.f);
	};
	run(0.5f);
	World &world = game.GetWorld();
	auto t = game.Registry().GetComponent<Transform>(player).Unwrap();
	t->position = t->previous = math::FVector3{0.5f, 10.f, 0.5f};
	t->yaw = 0.f;    // vers +z
	t->pitch = -0.6f; // le sol, deux blocs devant
	auto body = game.Registry().GetComponent<PhysicsBody>(player).Unwrap();
	body->flies = true; // immobile pendant l'essai (pas emporté par son propre ruisseau)
	// Composants relus à chaque fois : l'ECS peut déplacer le joueur.
	auto hand = [&]() -> ItemStack & {
		return game.Registry().GetComponent<InventoryComponent>(player).Unwrap()->inventory.Slot(
			game.Registry().GetComponent<Player>(player).Unwrap()->selectedSlot);
	};
	hand() = ItemStack{"liq:water_bucket", 1, 0, nullptr};
	// Seau plein : pose une source, redevient vide ; l'eau coule.
	REQUIRE(game.UseHeldItem(player));
	CHECK(hand().item == "liq:bucket");
	run(3.f);
	int wet = 0;
	for (int x = -6; x <= 6; ++x)
		for (int z = -6; z <= 8; ++z)
			wet += world.BlockAt({x, 10, z}).fluid;
	CHECK(wet > 40);
	// Seau vide : reprend la source (visée à travers l'eau) ; l'eau se retire.
	REQUIRE(game.UseHeldItem(player));
	CHECK(hand().item == "liq:water_bucket");
	run(4.f);
	wet = 0;
	for (int x = -8; x <= 8; ++x)
		for (int z = -8; z <= 10; ++z)
			wet += world.BlockAt({x, 10, z}).fluid;
	CHECK_EQ(wet, 0);
	// Le courant emporte un objet tombé dans un ruisseau.
	for (int x = 20; x <= 30; ++x)
		for (int z = -1; z <= 1; ++z)
			if (z != 0)
				(void)world.SetBlock({x, 10, z}, String("liq:stone"), 0, ChangeCause::PLAYER);
	(void)world.SetBlock({20, 10, 0}, String("liq:water"), 0, ChangeCause::PLAYER);
	run(3.f);
	const ecs::Entity pebble = game.DropItem(ItemStack{"liq:pebble", 1, 0, nullptr}, {22.5f, 10.2f, 0.5f}, {0.f, 0.f, 0.f});
	run(1.5f);
	REQUIRE(game.Registry().IsAlive(pebble));
	CHECK(game.Registry().GetComponent<Transform>(pebble).Unwrap()->position.x > 23.5f);
	// Règle « liquidFlow » : plus rien ne coule.
	game.SetRule("liquidFlow", false);
	(void)world.SetBlock({0, 10, -5}, String("liq:water"), 0, ChangeCause::PLAYER);
	run(2.f);
	CHECK(world.BlockAt({1, 10, -5}).IsAir());
	game.SetRule("liquidFlow", true);
	run(1.f);
	CHECK(world.BlockAt({1, 10, -5}).fluid);
	game.CloseWorld();
}
