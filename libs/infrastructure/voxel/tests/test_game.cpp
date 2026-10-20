// Tests — partie complète SANS fenêtre (voxel/game.hpp) : mods JSON et
// scripts, monde, joueur, actions, console, artisanat, sauvegarde,
// migrations. Écrit UNIQUEMENT sous build/tests/tmp/voxel/.
#include "test.hpp"

#include <filesystem>
#include <fstream>

#include <ecs/events.hpp>
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/scripting.hpp>

using namespace voxel;
namespace fs = std::filesystem;

namespace {

void Write(const fs::path &path, const char *text) {
	fs::create_directories(path.parent_path());
	std::ofstream(path) << text;
}

/// Un mod de test complet, écrit sur disque.
String MakeMods(const char *name) {
	const fs::path root = fs::path("build/tests/tmp/voxel") / name;
	fs::remove_all(root);
	const fs::path mod = root / "mods" / "basic";
	Write(mod / "mod.json", R"({"id": "basic", "name": "Basique", "version": "1.2"})");
	Write(mod / "blocks.json", R"([
		{"id": "stone", "graphics": {"texture": "stone"}, "gameplay": {"hardness": 1.5, "tool": "pickaxe", "drops": "cobble"}},
		{"id": "cobble", "graphics": {"texture": "stone"}},
		{"id": "dirt", "graphics": {"texture": "stone"}},
		{"id": "grass", "graphics": {"texture": "stone"}, "gameplay": {"drops": "dirt"}},
		{"id": "sand", "graphics": {"texture": "stone"}, "simulation": {"falling": {"enabled": true}}},
		{"id": "water", "tags": ["fluid"], "physics": {"solid": false}, "graphics": {"texture": "stone", "material": "TRANSPARENT"}},
		{"id": "log", "tags": ["logs"], "graphics": {"texture": "stone"}},
		{"id": "planks", "tags": ["planks"], "graphics": {"texture": "stone"}},
		{"id": "bedrock", "graphics": {"texture": "stone"}, "gameplay": {"hardness": -1}}
	])");
	Write(mod / "textures" / "stone.json", R"({"type": "texture", "id": "stone", "procedural": {"color": [128, 128, 128], "noise": 0.2}})");
	Write(mod / "items.json", R"([
		{"type": "item", "id": "stick", "tags": ["sticks"], "icon": "stone"},
		{"type": "tool", "id": "pick", "icon": "stone", "tool": {"kind": "pickaxe", "speed": 5, "durability": 2}}
	])");
	Write(mod / "recipes.json", R"([
		{"type": "recipe", "id": "planks", "ingredients": ["#logs"], "result": {"item": "planks", "count": 4}},
		{"type": "recipe", "id": "stick", "pattern": ["P", "P"], "key": {"P": "#planks"}, "result": {"item": "stick", "count": 4}}
	])");
	Write(mod / "world.json", R"([
		{"type": "biome", "id": "plain", "terrain": {"height": 10, "variation": 0, "roughness": 0},
		 "blocks": {"surface": "grass", "subsurface": "dirt", "filler": "stone"}},
		{"type": "generator", "id": "main", "seaLevel": 2, "fluid": "water", "bedrock": "bedrock", "bedrockLevel": -20},
		{"type": "creature", "id": "blob", "physics": {"collider": [0.8, 0.8, 0.8]}, "health": 4,
		 "behaviors": ["hop", "basic:spin"], "drops": [["stick", 2]]},
		{"type": "config", "values": {"player": {"starterKit": [["basic:log", 2], ["basic:sand", 3]]}, "world": {"generator": "basic:main"}}}
	])");
	Write(mod / "init.script", R"(
voxel.register({type: "block", id: "glass", graphics: {texture: "stone", material: "TRANSLUCENT"}})
voxel.on("block_broken", fn(x, y, z, block, actor) {
    if (block == "basic:glass") { return false }   # le verre est protégé
    return true
})
let spins = 0
voxel.behavior("spin", fn(e, params, dt) {
    spins += 1
    entity.look(e, spins * 0.1)
})
voxel.command("spins", "/spins", "tours du blob", fn(sender, args) { return format("{}", spins) })
voxel.on("player_join", fn(p) { entity.set_prop(p, "joined", true) })
)");
	return String(root.string().c_str());
}

GameOptions Options(const String &root) {
	GameOptions o;
	o.modDirectories = {root + String("/mods")};
	o.savesDirectory = root + String("/saves");
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 64;
	return o;
}

void Run(Game &g, float seconds) {
	for (float t = 0.f; t < seconds; t += 1.f / 60.f)
		g.Tick(1.f / 60.f);
}

} // namespace

TEST(Game_ModsWorldPlayerActionsAndScripts) {
	const String root = MakeMods("game");
	Game game;
	const ModReport report = game.LoadMods(Options(root));
	for (const String &e : report.errors)
		std::printf("erreur : %s\n", e.CStr());
	REQUIRE(report.errors.empty());
	REQUIRE_EQ(report.mods.size(), size_t(1));
	CHECK(game.Content().FindBlock("basic:glass") != nullptr); // déclaré par script
	REQUIRE(game.OpenWorld("monde", Some(uint32_t(7))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Alice");
	Run(game, 2.f);
	auto &t = *game.Registry().GetComponent<Transform>(player).Unwrap();
	CHECK_NEAR(t.position.y, 11.f, 0.05f); // debout sur l'herbe (surface y = 10)
	CHECK(game.Registry().GetComponent<PhysicsBody>(player).Unwrap()->onGround);
	auto &inv = game.Registry().GetComponent<InventoryComponent>(player).Unwrap()->inventory;
	CHECK_EQ(inv.Count("basic:log"), 2); // équipement de départ (configuration)

	// Casser l'herbe sous ses pieds : elle laisse de la terre, ramassée.
	const BlockPos below = BlockPos::FromWorld(t.position) + BlockPos{0, -1, 0};
	CHECK(game.GetWorld().BlockAt(below).id == "basic:grass");
	CHECK(game.BreakBlock(player, below));
	Run(game, 1.5f);
	CHECK_EQ(inv.Count("basic:dirt"), 1);
	CHECK(game.BreakTime(player, {5, -20, 5}) < 0.f); // socle incassable

	// Poser du sable en l'air : il tombe et redevient un bloc au sol.
	const BlockPos air{4, 15, 4};
	auto &selected = game.Registry().GetComponent<Player>(player).Unwrap()->selectedSlot;
	for (int i = 0; i < inv.Size(); ++i)
		if (inv.Slot(i).item == "basic:sand")
			selected = i;
	CHECK(game.PlaceBlock(player, air, "basic:sand"));
	CHECK_EQ(inv.Count("basic:sand"), 2);
	Run(game, 2.f);
	CHECK(game.GetWorld().BlockAt(air).IsAir());
	CHECK(game.GetWorld().BlockAt({4, 11, 4}).id == "basic:sand");

	// Le verre est protégé par un script (annulation).
	(void)game.GetWorld().SetBlock({8, 11, 8}, String("basic:glass"));
	CHECK(!game.BreakBlock(player, {8, 11, 8}));
	CHECK(game.GetWorld().BlockAt({8, 11, 8}).id == "basic:glass");

	// Console : artisanat par tags, objets, commandes de script.
	CHECK(game.Execute("/craft planks", player).IsOk());
	CHECK_EQ(inv.Count("basic:planks"), 4);
	CHECK(game.Execute("/craft basic:stick", player).IsOk());
	CHECK_EQ(inv.Count("basic:stick"), 4);
	CHECK(game.Execute("/give pick", player).IsOk());
	CHECK(game.Execute("/frobnicate").IsError());
	CHECK(!game.Complete("/cra").empty());

	// Créature : comportement intégré + comportement scripté, mort et butin.
	const ecs::Entity blob = game.SpawnEntity("basic:blob", t.position + math::FVector3{2.f, 0.f, 0.f});
	REQUIRE(blob.Valid());
	Run(game, 1.f);
	auto spins = game.Execute("/spins", player);
	CHECK(spins.IsOk() && std::atoi(spins.Value().CStr()) > 30);
	game.Damage(blob, 100.f, player);
	Run(game, 0.2f);
	CHECK(!game.Registry().IsAlive(blob));
	CHECK(game.Scripts().RunSource("1 + 1").IsOk());
	CHECK(game.Scripts().RunSource("let = ").IsError()); // validation
	game.CloseWorld();
}

TEST(Game_SaveReloadAndSaveMigration) {
	const String root = MakeMods("save");
	{
		Game game;
		(void)game.LoadMods(Options(root));
		REQUIRE(game.OpenWorld("persistant", Some(uint32_t(3))).IsOk());
		const ecs::Entity player = game.JoinPlayer("Bob");
		Run(game, 1.f);
		(void)game.GetWorld().SetBlock({1, 11, 1}, String("basic:planks"), 0, ChangeCause::PLAYER);
		(void)game.Execute("/give stick 5", player);
		(void)game.SpawnEntity("basic:blob", {3.f, 11.f, 3.f});
		REQUIRE(game.Save().IsOk());
	}
	{
		Game game;
		(void)game.LoadMods(Options(root));
		REQUIRE(game.OpenWorld("persistant").IsOk());
		CHECK_EQ(game.Metadata().seed, 3u);
		CHECK_EQ(game.Metadata().mods.size(), size_t(1));
		const ecs::Entity player = game.JoinPlayer("Bob");
		Run(game, 0.5f);
		CHECK(game.GetWorld().BlockAt({1, 11, 1}).id == "basic:planks");
		CHECK_EQ(game.Registry().GetComponent<InventoryComponent>(player).Unwrap()->inventory.Count("basic:stick"), 5);
		size_t blobs = 0;
		game.Registry().Query<EntityKind>([&](ecs::Entity, EntityKind &k) { blobs += k.id == "basic:blob"; });
		CHECK_EQ(blobs, size_t(1));
		game.CloseWorld();
	}
	// Sauvegarde de version 1 : migrée à l'ouverture.
	const fs::path old = fs::path(root.CStr()) / "saves" / "ancien";
	Write(old / "world.json", R"({"format": "voxel.world", "version": 1, "name": "ancien", "seed": 9, "spawn": [0.5, 30, 0.5], "time": 123})");
	Write(old / "players.json", R"([{"name": "Carol", "position": [0.5, 30, 0.5], "inventory": {"size": 36, "slots": [{"slot": 0, "item": "basic:stick", "count": 3}]}}])");
	Game game;
	(void)game.LoadMods(Options(root));
	REQUIRE(game.OpenWorld("ancien").IsOk());
	CHECK_EQ(game.Metadata().version, SAVE_VERSION);
	CHECK_NEAR(game.Metadata().gameTime, 123.0, 1e-9);
	CHECK_NEAR(game.Metadata().spawn.y, 30.f, 1e-5f);
	CHECK(fs::exists(old / "players" / "Carol.json"));
	const ecs::Entity carol = game.JoinPlayer("Carol");
	CHECK_EQ(game.Registry().GetComponent<InventoryComponent>(carol).Unwrap()->inventory.Count("basic:stick"), 3);
	game.CloseWorld();
}
