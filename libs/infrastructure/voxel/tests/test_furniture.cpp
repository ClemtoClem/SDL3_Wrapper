// Tests — blocs interactifs (voxel/game.hpp) : orientation des blocs posés,
// blocs de plusieurs cellules (porte, lit), bascule ouverte / fermée, lit
// (réapparition, nuit passée), rangements (contenu enregistré, lâché quand
// on casse le meuble), textures qui suivent la rotation. Écrit UNIQUEMENT
// sous build/tests/tmp/voxel/.
#include "test.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/meshing.hpp>
#include <voxel/physics.hpp>
#include <voxel/scripting.hpp>

using namespace voxel;
namespace fs = std::filesystem;

namespace {

constexpr float QUARTER = 1.5707963f;

void Write(const fs::path &path, const char *text) {
	fs::create_directories(path.parent_path());
	std::ofstream(path) << text;
}

/// Porte (deux de haut, ouverte / fermée), lit (deux de long), coffre.
String MakeMods(const char *name) {
	const fs::path root = fs::path("build/tests/tmp/voxel") / name;
	fs::remove_all(root);
	const fs::path mod = root / "mods" / "furn";
	Write(mod / "mod.json", R"({"id": "furn", "name": "Mobilier", "version": "1.0"})");
	Write(mod / "content.json", R"([
		{"type": "texture", "id": "grey", "procedural": {"color": [128, 128, 128]}},
		{"id": "stone", "graphics": {"texture": "grey"}},
		{"type": "generator", "id": "flat", "kind": "flat", "layers": [["stone", 10]]},
		{"type": "config", "values": {"world": {"generator": "furn:flat"}}},
		{"type": "item", "id": "apple", "icon": "grey"},
		{"id": "door", "type": "nodebox", "nodebox": [[-0.5, -0.5, -0.5, 0.5, 0.5, -0.3125]], "graphics": {"texture": "grey"},
		 "parts": [{"offset": [0, 1, 0], "block": "door_top"}], "interact": {"toggle": "door_open"}},
		{"id": "door_top", "type": "nodebox", "nodebox": [[-0.5, -0.5, -0.5, 0.5, 0.5, -0.3125]], "graphics": {"texture": "grey"},
		 "partOf": [0, -1, 0], "item": false, "gameplay": {"drops": ""}},
		{"id": "door_open", "type": "nodebox", "nodebox": [[0.3125, -0.5, -0.5, 0.5, 0.5, 0.5]], "graphics": {"texture": "grey"},
		 "parts": [{"offset": [0, 1, 0], "block": "door_top_open"}], "interact": {"toggle": "door"},
		 "item": false, "gameplay": {"drops": "door"}},
		{"id": "door_top_open", "type": "nodebox", "nodebox": [[0.3125, -0.5, -0.5, 0.5, 0.5, 0.5]], "graphics": {"texture": "grey"},
		 "partOf": [0, -1, 0], "item": false, "gameplay": {"drops": ""}},
		{"id": "bed", "type": "nodebox", "nodebox": [[-0.5, -0.5, -0.5, 0.5, 0.0625, 0.5]], "graphics": {"texture": "grey"},
		 "parts": [{"offset": [0, 0, 1], "block": "bed_head"}], "interact": {"action": "sleep"}},
		{"id": "bed_head", "type": "nodebox", "nodebox": [[-0.5, -0.5, -0.5, 0.5, 0.0625, 0.5]], "graphics": {"texture": "grey"},
		 "partOf": [0, 0, -1], "interact": {"action": "sleep"}, "item": false, "gameplay": {"drops": ""}},
		{"id": "chest", "graphics": {"texture": "grey"}, "container": {"slots": 27, "title": "Coffre"}},
		{"type": "texture", "id": "lamp_off", "procedural": {"color": [60, 60, 60]}},
		{"type": "texture", "id": "lamp_on", "procedural": {"color": [250, 240, 200]}},
		{"id": "lamp", "graphics": {"textures": {"side": "grey", "bottom": "lamp_off"}}, "defaultState": "off",
		 "states": {
			"off": {"interact": {"toggle": "on"}},
			"on": {"graphics": {"textures": {"bottom": "lamp_on"}, "light": 12}, "interact": {"toggle": "off"}},
			"auto": {"id": "lamp_sensor", "sensor": {"radius": 4, "near": "auto_on"}},
			"auto_on": {"graphics": {"textures": {"bottom": "lamp_on"}, "light": 12}, "sensor": {"radius": 6, "far": "auto"}}}}
	])");
	Write(mod / "init.script", R"(
voxel.command("loot", "/loot", "remplit le coffre en 8, 10, 0", fn(sender, args) {
    let left = world.container_add(8, 10, 0, "furn:apple", 3)
    let c = world.container(8, 10, 0)
    return format("{} {} {}", len(c), c[0].count, left)
})
voxel.command("swing", "/swing", "ouvre la porte", fn(sender, args) {
    return format("{}", world.toggle(5, 10, 5))
})
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

Transform &T(Game &g, ecs::Entity e) { return *g.Registry().GetComponent<Transform>(e).Unwrap(); }

/// Objets au sol portant `item` (somme des piles).
int Dropped(Game &g, const char *item) {
	int count = 0;
	g.Registry().Query<DroppedItem>([&](ecs::Entity, DroppedItem &d) {
		if (d.stack.item == item)
			count += d.stack.count;
	});
	return count;
}

void Load(ContentRegistry &content, const char *text) {
	auto parsed = json::Parse(String(text));
	if (parsed.IsError() || content.Load(parsed.Unwrap(), {"t", "", "test"}).IsError())
		std::printf("définition refusée : %s\n", text);
}

} // namespace

TEST(Furniture_PlacedBlocksFaceTheirPlacer) {
	// Pour chaque regard : la boîte « du fond » (z près de 1 à l'orientation
	// 0) part du côté regardé, et le décalage +z des parties aussi.
	const Box far{{0.f, 0.f, 0.9f}, {1.f, 1.f, 1.f}};
	const struct {
		float yaw;
		BlockPos look;
	} cases[] = {{0.f, {0, 0, 1}}, {QUARTER, {1, 0, 0}}, {2.f * QUARTER, {0, 0, -1}}, {3.f * QUARTER, {-1, 0, 0}}};
	for (const auto &c : cases) {
		const uint8_t param = FacingParam(c.yaw);
		CHECK(RotateOffset({0, 0, 1}, param) == c.look);
		const Box b = RotateBlockBox(far, param);
		const math::FVector3 center = (b.min + b.max) * 0.5f - math::FVector3{0.5f, 0.5f, 0.5f};
		CHECK_NEAR(center.x, float(c.look.x) * 0.45f, 1e-4f);
		CHECK_NEAR(center.z, float(c.look.z) * 0.45f, 1e-4f);
	}
	CHECK_EQ(int(FacingParam(-QUARTER)), int(FacingParam(3.f * QUARTER)));
	CHECK(RotateOffset({1, 2, 0}, 2) == (BlockPos{-1, 2, 0}));
}

TEST(Mesher_TexturesFollowTheBlockRotation) {
	ContentRegistry content;
	AssetManager assets;
	TextureAtlas atlas;
	Load(content, R"({"type": "texture", "id": "side", "procedural": {"color": [90, 90, 90]}})");
	Load(content, R"({"type": "texture", "id": "front", "procedural": {"color": [200, 40, 40]}})");
	Load(content, R"({"id": "chest", "graphics": {"textures": {"side": "side", "north": "front"}}})");
	assets.Declare(content);
	atlas.Build(assets, content, 16);
	const TextureAtlas::Rect front = atlas.Lookup("t:front");
	ChunkMesher mesher(content, atlas, &assets);
	// Face avant (nord à l'orientation 0) : vers le joueur qui l'a posée.
	for (const float yaw : {0.f, QUARTER, 2.f * QUARTER, 3.f * QUARTER}) {
		World world(content);
		(void)world.EnsureChunk({0, 0, 0});
		const uint8_t param = FacingParam(yaw);
		(void)world.SetBlock({4, 4, 4}, String("t:chest"), param);
		const ChunkMesh mesh = mesher.Build(world, {0, 0, 0});
		const BlockPos toward = RotateOffset({0, 0, -1}, param);
		int frontVertices = 0, elsewhere = 0;
		for (const MeshVertex &v : mesh.opaque.vertices) {
			const bool inFront = v.uv.x >= front.u0 - 1e-6f && v.uv.x <= front.u1 + 1e-6f && v.uv.y >= front.v0 - 1e-6f &&
								 v.uv.y <= front.v1 + 1e-6f;
			const bool facing = std::lround(v.normal.x) == toward.x && std::lround(v.normal.z) == toward.z && std::lround(v.normal.y) == 0;
			frontVertices += inFront && facing;
			elsewhere += inFront && !facing;
		}
		CHECK_EQ(frontVertices, 4);
		CHECK_EQ(elsewhere, 0);
	}
}

TEST(Game_DoorsArePlacedToggledAndBrokenAsAWhole) {
	const String root = MakeMods("furniture_doors");
	Game game;
	const ModReport report = game.LoadMods(Options(root));
	for (const String &e : report.errors)
		std::printf("erreur : %s\n", e.CStr());
	REQUIRE(report.errors.empty());
	REQUIRE(game.OpenWorld("monde", Some(uint32_t(3))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Alix");
	World &world = game.GetWorld();
	T(game, player).yaw = QUARTER; // regarde vers +x
	REQUIRE(game.PlaceBlock(player, {5, 10, 5}, "furn:door"));
	CHECK(world.BlockAt({5, 10, 5}).id == "furn:door");
	CHECK(world.BlockAt({5, 11, 5}).id == "furn:door_top");
	CHECK_EQ(int(world.GetParam({5, 11, 5})), int(FacingParam(QUARTER)));
	CHECK(game.MainPart({5, 11, 5}) == (BlockPos{5, 10, 5}));
	// Pas de place pour la moitié haute : refusée, rien de posé.
	(void)world.SetBlock({7, 11, 7}, String("furn:stone"), 0, ChangeCause::SCRIPT);
	CHECK(!game.PlaceBlock(player, {7, 10, 7}, "furn:door"));
	CHECK(world.BlockAt({7, 10, 7}).IsAir());
	// Clic droit sur la moitié haute : toute la porte s'ouvre, puis se referme (script).
	CHECK(game.UseBlock(player, {5, 11, 5}));
	CHECK(world.BlockAt({5, 10, 5}).id == "furn:door_open");
	CHECK(world.BlockAt({5, 11, 5}).id == "furn:door_top_open");
	CHECK_EQ(int(world.GetParam({5, 10, 5})), int(FacingParam(QUARTER)));
	CHECK(game.Execute("/swing", player).Value() == "true");
	CHECK(world.BlockAt({5, 11, 5}).id == "furn:door_top");
	// Casser la moitié haute : les deux disparaissent, une seule porte tombe.
	CHECK(game.BreakBlock(player, {5, 11, 5}));
	CHECK(world.BlockAt({5, 10, 5}).IsAir());
	CHECK(world.BlockAt({5, 11, 5}).IsAir());
	CHECK_EQ(Dropped(game, "furn:door"), 1);
	game.CloseWorld();
}

TEST(Game_BedSetsTheRespawnPointAndSkipsTheNight) {
	const String root = MakeMods("furniture_bed");
	{
		Game game;
		REQUIRE(game.LoadMods(Options(root)).errors.empty());
		REQUIRE(game.OpenWorld("monde", Some(uint32_t(3))).IsOk());
		const ecs::Entity player = game.JoinPlayer("Alix");
		World &world = game.GetWorld();
		T(game, player).yaw = QUARTER;
		REQUIRE(game.PlaceBlock(player, {3, 10, -4}, "furn:bed"));
		CHECK(world.BlockAt({4, 10, -4}).id == "furn:bed_head"); // la tête plus loin, dans le regard
		// Le jour : point de réapparition seulement.
		game.SetGameTime(0.5 * double(game.Options().dayLength));
		CHECK(game.UseBlock(player, {4, 10, -4}));
		CHECK_NEAR(game.DayFraction(), 0.5f, 1e-4f);
		auto pl = game.Registry().GetComponent<Player>(player);
		REQUIRE(pl.IsSome());
		CHECK(pl.Unwrap()->hasBed);
		CHECK(pl.Unwrap()->bed == (BlockPos{3, 10, -4}));
		// La nuit : jusqu'au matin.
		game.SetGameTime(1.9 * double(game.Options().dayLength));
		CHECK(game.UseBlock(player, {3, 10, -4}));
		CHECK_NEAR(game.DayFraction(), 0.25f, 1e-4f);
		CHECK(game.GameTime() > 1.9 * double(game.Options().dayLength));
		// Mort : réveil sur le lit.
		T(game, player).position = T(game, player).previous = math::FVector3{20.f, 10.f, 20.f};
		game.Kill(player);
		CHECK_NEAR(T(game, player).position.x, 3.5f, 1e-4f);
		CHECK_NEAR(T(game, player).position.z, -3.5f, 1e-4f);
		REQUIRE(game.Save().IsOk());
		game.CloseWorld();
	}
	Game game;
	REQUIRE(game.LoadMods(Options(root)).errors.empty());
	REQUIRE(game.OpenWorld("monde").IsOk());
	const ecs::Entity player = game.JoinPlayer("Alix");
	auto pl = game.Registry().GetComponent<Player>(player);
	REQUIRE(pl.IsSome());
	CHECK(pl.Unwrap()->hasBed); // enregistré avec le joueur
	CHECK(pl.Unwrap()->bed == (BlockPos{3, 10, -4}));
	// Lit cassé (par la tête) : un lit tombe ; la mort ramène au point de départ.
	CHECK(game.BreakBlock(player, {4, 10, -4}));
	CHECK(game.GetWorld().BlockAt({3, 10, -4}).IsAir());
	CHECK_EQ(Dropped(game, "furn:bed"), 1);
	game.Kill(player);
	CHECK(!game.Registry().GetComponent<Player>(player).Unwrap()->hasBed);
	CHECK(std::fabs(T(game, player).position.x - 3.5f) > 0.6f || std::fabs(T(game, player).position.z + 3.5f) > 0.6f);
	game.CloseWorld();
}

TEST(Game_ContainersKeepTheirContentsAndDropThemWhenBroken) {
	const String root = MakeMods("furniture_chest");
	{
		Game game;
		REQUIRE(game.LoadMods(Options(root)).errors.empty());
		REQUIRE(game.OpenWorld("monde", Some(uint32_t(3))).IsOk());
		const ecs::Entity player = game.JoinPlayer("Alix");
		REQUIRE(game.PlaceBlock(player, {8, 10, 0}, "furn:chest"));
		// Ouvrir : demande d'interface « container ».
		(void)game.TakeUiRequests();
		CHECK(game.UseBlock(player, {8, 10, 0}));
		const std::vector<UiRequest> requests = game.TakeUiRequests();
		REQUIRE_EQ(requests.size(), size_t(1));
		CHECK(requests[0].kind == "container");
		CHECK_EQ(json::Int(requests[0].data, "slots", 0), 27);
		CHECK(json::Str(requests[0].data, "title") == "Coffre");
		Inventory *chest = game.BlockInventory({8, 10, 0});
		REQUIRE(chest != nullptr);
		CHECK_EQ(chest->Size(), 27);
		CHECK_EQ(chest->Add(game.Content(), "furn:apple", 5), 0);
		const Result<String, String> loot = game.Execute("/loot", player); // scripts : lire et remplir
		if (loot.IsError())
			std::printf("/loot : %s\n", loot.Error().CStr());
		CHECK(loot.IsOk() && loot.Value() == "27 8 0");
		CHECK(game.BlockInventory({8, 10, 1}) == nullptr);          // pas un rangement
		REQUIRE(game.Save().IsOk());
		game.CloseWorld();
	}
	Game game;
	REQUIRE(game.LoadMods(Options(root)).errors.empty());
	REQUIRE(game.OpenWorld("monde").IsOk());
	const ecs::Entity player = game.JoinPlayer("Alix");
	Inventory *chest = game.BlockInventory({8, 10, 0});
	REQUIRE(chest != nullptr);
	CHECK_EQ(chest->Count("furn:apple"), 8); // relu dans world.db
	// Cassé : le contenu tombe avec le coffre, et un nouveau coffre au même endroit est vide.
	CHECK(game.BreakBlock(player, {8, 10, 0}));
	CHECK_EQ(Dropped(game, "furn:apple"), 8);
	CHECK_EQ(Dropped(game, "furn:chest"), 1);
	CHECK(game.BlockInventory({8, 10, 0}) == nullptr);
	REQUIRE(game.PlaceBlock(player, {8, 10, 0}, "furn:chest"));
	REQUIRE(game.BlockInventory({8, 10, 0}) != nullptr);
	CHECK_EQ(game.BlockInventory({8, 10, 0})->Count("furn:apple"), 0);
	game.CloseWorld();
}

TEST(Game_BlockStatesSwapTexturesAndFollowSensors) {
	const String root = MakeMods("furniture_states");
	Game game;
	const ModReport report = game.LoadMods(Options(root));
	for (const String &e : report.errors)
		std::printf("erreur : %s\n", e.CStr());
	REQUIRE(report.errors.empty());
	// Un BlockDef par état : textures et lumière propres, le reste hérité.
	const ContentRegistry &content = game.Content();
	const BlockDef *off = content.FindBlock("furn:lamp");
	const BlockDef *on = content.FindBlock("furn:lamp_on");
	const BlockDef *sensor = content.FindBlock("furn:lamp_sensor"); // « id » explicite
	REQUIRE(off && on && sensor && content.FindBlock("furn:lamp_auto_on"));
	CHECK(off->stateName == "off" && on->stateName == "on" && on->stateGroup == "furn:lamp");
	CHECK(off->look.Texture(Face::BOTTOM) == "furn:lamp_off" && on->look.Texture(Face::BOTTOM) == "furn:lamp_on");
	CHECK(on->look.Texture(Face::EAST) == "furn:grey"); // hérité
	CHECK_EQ(off->look.lightEmission, 0);
	CHECK_EQ(on->look.lightEmission, 12);
	CHECK(off->toggle == "furn:lamp_on" && on->toggle == "furn:lamp"); // noms d'états → ids
	CHECK(content.BlockState(*on, "auto") == sensor);
	CHECK(json::Str(on->raw, "gameplay.drops") == "lamp"); // on ramasse la lampe (état par défaut)
	CHECK(content.FindItem("furn:lamp") != nullptr);
	CHECK(content.FindItem("furn:lamp_on") == nullptr);
	CHECK_NEAR(content.MaxSensorRadius(), 6.f, 1e-6f);

	REQUIRE(game.OpenWorld("monde", Some(uint32_t(3))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Alix");
	World &world = game.GetWorld();
	// Bascule et API de script.
	(void)world.SetBlock({3, 13, 3}, String("furn:lamp"), 2, ChangeCause::SCRIPT);
	CHECK(game.ToggleBlock({3, 13, 3}));
	CHECK(world.BlockAt({3, 13, 3}).id == "furn:lamp_on");
	CHECK_EQ(int(world.GetParam({3, 13, 3})), 2); // orientation gardée
	CHECK(game.Scripts().RunSource("world.set_state(3, 13, 3, \"off\")").IsOk());
	CHECK(world.BlockAt({3, 13, 3}).id == "furn:lamp");
	CHECK(!game.SetBlockState({3, 13, 3}, "disco"));
	// Détecteur : s'allume quand le joueur approche, s'éteint quand il s'en va.
	const math::FVector3 at = T(game, player).position;
	const BlockPos near = BlockPos::FromWorld(at) + BlockPos{2, 2, 0};
	const BlockPos far = BlockPos::FromWorld(at) + BlockPos{30, 2, 0};
	(void)world.SetBlock(near, String("furn:lamp_sensor"), 0, ChangeCause::SCRIPT);
	(void)world.SetBlock(far, String("furn:lamp_sensor"), 0, ChangeCause::SCRIPT);
	for (int i = 0; i < 30; ++i)
		game.Tick(1.f / 60.f);
	CHECK(world.BlockAt(near).id == "furn:lamp_auto_on");
	CHECK(world.BlockAt(far).id == "furn:lamp_sensor");
	T(game, player).position.x -= 8.f; // à 10 blocs
	for (int i = 0; i < 30; ++i)
		game.Tick(1.f / 60.f);
	CHECK(world.BlockAt(near).id == "furn:lamp_sensor");
	game.CloseWorld();
}
