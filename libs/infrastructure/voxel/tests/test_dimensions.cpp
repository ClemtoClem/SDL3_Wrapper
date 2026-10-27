// Tests — dimensions (voxel/game.hpp) : voyage, chunks et entités propres à
// chaque dimension, sauvegarde, portails, mort par suffocation ; générateurs
// "islands", "caverns" et "maze" ; physique des sables mouvants, de la
// gravité inversée et du joueur pris dans le sable. Écrit UNIQUEMENT sous
// build/tests/tmp/voxel/.
#include "test.hpp"

#include <filesystem>
#include <fstream>

#include <ecs/events.hpp>
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/physics.hpp>
#include <voxel/scripting.hpp>
#include <voxel/worldgen.hpp>

using namespace voxel;
namespace fs = std::filesystem;

namespace {

void Write(const fs::path &path, const char *text) {
	fs::create_directories(path.parent_path());
	std::ofstream(path) << text;
}

void Load(ContentRegistry &content, const char *text) {
	auto parsed = json::Parse(String(text));
	if (parsed.IsError() || content.Load(parsed.Unwrap(), {"t", "", "test"}).IsError())
		std::printf("définition refusée : %s\n", text);
}

/// Deux dimensions plates (pierre / sable), un portail dans chaque sens et
/// des backrooms où mène la suffocation.
String MakeMods(const char *name) {
	const fs::path root = fs::path("build/tests/tmp/voxel") / name;
	fs::remove_all(root);
	const fs::path mod = root / "mods" / "dims";
	Write(mod / "mod.json", R"({"id": "dims", "name": "Dimensions", "version": "1.0"})");
	Write(mod / "content.json", R"([
		{"type": "texture", "id": "grey", "procedural": {"color": [128, 128, 128]}},
		{"id": "stone", "graphics": {"texture": "grey"}},
		{"id": "sand", "graphics": {"texture": "grey"}, "simulation": {"falling": {"enabled": true}}},
		{"id": "carpet", "graphics": {"texture": "grey"}},
		{"id": "wall", "graphics": {"texture": "grey"}},
		{"id": "lamp", "graphics": {"texture": "grey", "light": 12}},
		{"id": "outer", "graphics": {"texture": "grey"}, "gameplay": {"hardness": -1}},
		{"id": "portal_other", "graphics": {"texture": "grey", "material": "TRANSLUCENT"}, "physics": {"solid": false},
		 "portal": {"dimension": "other", "delay": 0.5}},
		{"id": "portal_back", "graphics": {"texture": "grey", "material": "TRANSLUCENT"}, "physics": {"solid": false},
		 "portal": {"dimension": "@return", "delay": 0.5}},
		{"type": "generator", "id": "stone_flat", "kind": "flat", "layers": [["stone", 10]]},
		{"type": "generator", "id": "sand_flat", "kind": "flat", "layers": [["sand", 20]]},
		{"type": "generator", "id": "rooms", "kind": "maze",
		 "maze": {"floor": 0, "height": 5, "cell": 6, "region": 60, "outer": "outer"},
		 "levels": [{"id": "lobby", "floor": "carpet", "wall": "wall", "ceiling": "wall", "light": "lamp"}]},
		{"type": "dimension", "id": "overworld", "displayName": "Monde"},
		{"type": "dimension", "id": "other", "displayName": "Autre monde", "generator": "sand_flat",
		 "sky": {"color": [60, 20, 90], "sun": false}, "time": {"cycle": false, "fixed": 0.9},
		 "physics": {"gravity": 0.5}, "arrival": {"portal": "portal_back"}},
		{"type": "dimension", "id": "backrooms", "displayName": "Backrooms", "generator": "rooms",
		 "spawn": {"top": 4, "bottom": -1}, "enterOnDeath": ["suffocation"]},
		{"type": "config", "values": {"world": {"generator": "dims:stone_flat", "dimension": "dims:overworld"}}}
	])");
	Write(mod / "init.script", R"(
voxel.on("dimension_changed", fn(player, to, from) {
    let visits = world.data("visits")
    if (visits == nil) { visits = 0 }
    world.set_data("visits", visits + 1)
})
voxel.command("where", "/where <dimension>", "dimension et visites", fn(sender, args) {
    return format("{} {}", world.dimension(), world.data("visits", args[0]))
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

void Run(Game &g, float seconds) {
	for (float t = 0.f; t < seconds; t += 1.f / 60.f)
		g.Tick(1.f / 60.f);
}

Transform &T(Game &g, ecs::Entity e) { return *g.Registry().GetComponent<Transform>(e).Unwrap(); }

/// Bloc d'un monde, son chunk chargé (généré) au besoin.
const BlockDef &At(World &world, const BlockPos &p) {
	(void)world.EnsureChunk(ChunkOf(p));
	return world.BlockAt(p);
}

/// Monde de physique : sol de pierre à y = 0..9, sables mouvants et plafond.
struct Scene {
	ContentRegistry content;
	std::unique_ptr<World> world;
	ecs::ArchetypeRegistry registry;
	Scene() {
		Load(content, R"({"id": "stone"})");
		Load(content, R"({"id": "quicksand", "physics": {"solid": false, "sinking": 0.6}})");
		Load(content, R"({"type": "generator", "id": "flat", "kind": "flat", "layers": [["stone", 10]]})");
		world = std::make_unique<World>(content);
		world->SetGenerator(MakeGenerator(content, "t:flat", 1));
		for (int y = -1; y <= 2; ++y)
			for (int z = -2; z <= 2; ++z)
				for (int x = -2; x <= 2; ++x)
					(void)world->EnsureChunk({x, y, z});
	}
	ecs::Entity Spawn(math::FVector3 at, bool player = false) {
		Transform t;
		t.position = t.previous = at;
		const ecs::Entity e = registry.SpawnBundle(t, Velocity{}, Collider{}, PhysicsBody{}, MoveIntent{});
		if (player)
			registry.AddComponent(e, Player{});
		return e;
	}
	void Run(float seconds) {
		for (float t = 0.f; t < seconds; t += 1.f / 60.f)
			StepPhysics(registry, *world, 1.f / 60.f);
	}
	Transform &Tr(ecs::Entity e) { return *registry.GetComponent<Transform>(e).Unwrap(); }
	PhysicsBody &B(ecs::Entity e) { return *registry.GetComponent<PhysicsBody>(e).Unwrap(); }
	MoveIntent &I(ecs::Entity e) { return *registry.GetComponent<MoveIntent>(e).Unwrap(); }
};

} // namespace

TEST(Physics_QuicksandSwallowsSlowlyAndHoldsTheWalker) {
	Scene s;
	// Poche de sables mouvants de 4 blocs de profondeur sous (0, 9, 0).
	for (int y = 6; y <= 9; ++y)
		for (int z = -1; z <= 1; ++z)
			for (int x = -1; x <= 1; ++x)
				(void)s.world->SetBlock({x, y, z}, String("t:quicksand"));
	const ecs::Entity e = s.Spawn({0.5f, 10.f, 0.5f});
	s.Run(1.f);
	CHECK(s.B(e).inSinking);
	const float after1 = s.Tr(e).position.y;
	CHECK(after1 < 10.f && after1 > 8.5f); // s'enfonce, LENTEMENT (pas une chute)
	// Marcher : presque aucune avance.
	s.I(e).wish = {1.f, 0.f, 0.f};
	const float x0 = s.Tr(e).position.x;
	s.Run(1.f);
	CHECK(s.Tr(e).position.x - x0 < 1.f);
	// Jusqu'au fond de la poche (y = 6), la tête sous la surface.
	s.Run(6.f);
	CHECK_NEAR(s.Tr(e).position.y, 6.f, 0.1f);
}

TEST(Physics_InvertedGravityFallsUpAndLandsOnTheCeiling) {
	Scene s;
	for (int z = -2; z <= 2; ++z)
		for (int x = -2; x <= 2; ++x)
			(void)s.world->SetBlock({x, 20, z}, String("t:stone")); // plafond à y = 20
	const ecs::Entity e = s.Spawn({0.5f, 12.f, 0.5f});
	s.B(e).gravityDirection = -1.f;
	s.Run(2.f);
	CHECK(s.B(e).onGround);              // « sol » : le plafond
	CHECK_NEAR(s.Tr(e).position.y + 1.8f, 20.f, 0.05f); // le haut du corps contre le plafond
	CHECK(s.B(e).groundBlock == s.content.BlockIdOf("t:stone"));
	// Sauter « vers le bas », puis retomber vers le plafond.
	s.I(e).jump = true;
	s.Run(0.1f);
	s.I(e).jump = false;
	CHECK(s.Tr(e).position.y + 1.8f < 19.8f);
	s.Run(2.f);
	CHECK(s.B(e).onGround);
}

TEST(Physics_SandOnTheHeadTrapsThePlayer) {
	Scene s;
	const ecs::Entity e = s.Spawn({0.5f, 10.f, 0.5f}, true);
	s.Run(0.5f);
	(void)s.world->SetBlock({0, 11, 0}, String("t:stone")); // un bloc tombé sur la tête
	s.I(e).wish = {1.f, 0.f, 0.f};
	s.I(e).jump = true;
	s.Run(1.f);
	CHECK(s.B(e).trapped);
	CHECK_NEAR(s.Tr(e).position.x, 0.5f, 1e-4f); // immobilisé
	// Bloc cassé : libre.
	(void)s.world->SetBlock({0, 11, 0}, AIR);
	s.Run(0.5f);
	CHECK(!s.B(e).trapped);
	CHECK(s.Tr(e).position.x > 0.6f);
}

TEST(WorldGen_IslandsCavernsAndMazeAreDeterministic) {
	ContentRegistry content;
	Load(content, R"({"id": "stone"})");
	Load(content, R"({"id": "grass"})");
	Load(content, R"({"id": "lava", "tags": ["fluid"], "physics": {"solid": false}})");
	Load(content, R"({"id": "bedrock"})");
	Load(content, R"({"id": "carpet"})");
	Load(content, R"({"id": "wall"})");
	Load(content, R"({"id": "exit", "physics": {"solid": false}})");
	Load(content, R"({"type": "biome", "id": "isle", "dimension": "sky", "blocks": {"surface": "grass", "subsurface": "stone", "filler": "stone"}})");
	Load(content, R"({"type": "generator", "id": "isles", "kind": "islands", "biomes": ["isle"],
		"islands": {"base": 40, "spread": 6, "threshold": 0.3, "height": 4, "thickness": 10, "frequency": 0.03}})");
	Load(content, R"({"type": "generator", "id": "caves", "kind": "caverns", "bedrock": "bedrock", "biomes": ["isle"],
		"caverns": {"floor": -20, "roof": 40, "lavaLevel": -12, "lava": "lava", "roofBlock": "stone"}})");
	Load(content, R"({"type": "generator", "id": "rooms", "kind": "maze",
		"maze": {"floor": 0, "height": 5, "cell": 6, "region": 60, "outer": "stone", "exit": {"block": "exit", "chance": 0.2}},
		"levels": [{"id": "lobby", "floor": "carpet", "wall": "wall", "ceiling": "stone"}]})");
	// Le biome d'une dimension ne s'invite pas dans un générateur sans liste.
	Load(content, R"({"type": "generator", "id": "plain", "kind": "noise"})");
	auto plain = MakeGenerator(content, "t:plain", 5);
	CHECK(plain->BiomeAt(0, 0) == nullptr);

	auto blocks = [&](const char *generator, uint32_t seed, const std::vector<ChunkPos> &order) {
		World world(content);
		world.SetGenerator(MakeGenerator(content, generator, seed));
		for (const ChunkPos &p : order)
			(void)world.EnsureChunk(p);
		std::vector<BlockId> out;
		for (int y = -24; y < 48; ++y)
			for (int x = 0; x < 32; x += 3)
				out.push_back(world.GetBlock({x, y, 5}));
		return out;
	};
	std::vector<ChunkPos> forward, backward;
	for (int y = -2; y <= 3; ++y)
		for (int x = -1; x <= 2; ++x)
			for (int z = -1; z <= 1; ++z)
				forward.push_back({x, y, z});
	backward.assign(forward.rbegin(), forward.rend());
	for (const char *g : {"t:isles", "t:caves", "t:rooms"})
		CHECK(blocks(g, 9, forward) == blocks(g, 9, backward));

	// Îles : de la terre en l'air, du vide dessous et autour.
	{
		World world(content);
		world.SetGenerator(MakeGenerator(content, "t:isles", 9));
		int land = 0, groundLevel = 0;
		for (int x = -64; x < 64; x += 2)
			for (int z = -64; z < 64; z += 2) {
				for (int cy = 0; cy <= 4; ++cy)
					(void)world.EnsureChunk(ChunkOf({x, cy * CHUNK_SIZE, z}));
				if (Option<int> top = world.HighestSolid(x, z, 0, 70); top.IsSome())
					++land;
				groundLevel += !At(world, {x, 0, z}).IsAir();
			}
		CHECK(land > 50);         // des îles…
		CHECK(land < 64 * 64);    // …pas un continent
		CHECK_EQ(groundLevel, 0); // et rien en bas
		(void)land;
	}
	// Cavernes : toit et plancher de socle, des salles au milieu, de la lave au fond.
	{
		World world(content);
		world.SetGenerator(MakeGenerator(content, "t:caves", 9));
		int air = 0, lava = 0;
		for (int x = 0; x < 64; x += 2)
			for (int z = 0; z < 64; z += 2) {
				CHECK(At(world, {x, -20, z}).id == "t:bedrock");
				CHECK(At(world, {x, 40, z}).id == "t:bedrock");
				for (int y = -19; y < 40; y += 3) {
					const BlockPos p{x, y, z};
					air += At(world, p).IsAir();
					lava += At(world, p).id == "t:lava";
				}
			}
		CHECK(air > 200);
		CHECK(lava > 0);
	}
	// Labyrinthe : plancher, plafond, des murs, des sorties, de la roche autour.
	{
		World world(content);
		world.SetGenerator(MakeGenerator(content, "t:rooms", 9));
		int walls = 0, open = 0, exits = 0;
		for (int x = -60; x < 60; ++x)
			for (int z = -60; z < 60; z += 2) {
				CHECK(At(world, {x, 0, z}).id == "t:carpet");
				CHECK(At(world, {x, 5, z}).id == "t:stone");
				CHECK(At(world, {x, -3, z}).id == "t:stone");
				const BlockPos p{x, 2, z};
				walls += At(world, p).id == "t:wall";
				open += At(world, p).IsAir();
				exits += At(world, {x, 1, z + 1}).id == "t:exit";
			}
		CHECK(walls > 100);
		CHECK(open > walls); // surtout des salles
		CHECK(exits > 0);
	}
}

TEST(Game_DimensionsKeepTheirOwnChunksEntitiesAndData) {
	const String root = MakeMods("dimensions");
	{
		Game game;
		const ModReport report = game.LoadMods(Options(root));
		for (const String &e : report.errors)
			std::printf("erreur : %s\n", e.CStr());
		REQUIRE(report.errors.empty());
		REQUIRE_EQ(game.Content().Dimensions().size(), size_t(3));
		REQUIRE(game.OpenWorld("monde", Some(uint32_t(4))).IsOk());
		CHECK(game.CurrentDimension() == "dims:overworld");
		const ecs::Entity player = game.JoinPlayer("Dana");
		Run(game, 1.f);
		CHECK_NEAR(T(game, player).position.y, 10.f, 0.05f);
		(void)game.GetWorld().SetBlock({2, 10, 2}, String("dims:carpet"), 0, ChangeCause::PLAYER);
		const uint64_t serial = game.WorldSerial();

		// Voyage : le monde est remplacé, généré à l'arrivée.
		REQUIRE(game.TravelTo(player, "dims:other").IsOk());
		CHECK(game.CurrentDimension() == "dims:other");
		CHECK(game.WorldSerial() > serial);
		CHECK(game.GetWorld().BlockAt({2, 10, 2}).id == "dims:sand"); // pas le tapis de l'autre monde
		CHECK_NEAR(game.DayFraction(), 0.9f, 1e-5f);                  // heure figée
		CHECK_NEAR(game.Physics().gravity, 14.f, 1e-3f);              // gravité réduite de moitié
		Run(game, 1.5f);
		CHECK_NEAR(T(game, player).position.y, 20.f, 0.05f);
		// Portail de retour posé près de l'arrivée.
		bool portal = false;
		for (int dx = -4; dx <= 4; ++dx)
			for (int dz = -4; dz <= 4; ++dz)
				portal = portal || game.GetWorld().BlockAt(BlockPos::FromWorld(T(game, player).position) + BlockPos{dx, 0, dz}).id == "dims:portal_back";
		CHECK(portal);
		(void)game.GetWorld().SetBlock({3, 20, 3}, String("dims:stone"), 0, ChangeCause::PLAYER);

		// Retour : le tapis est toujours là, les données des scripts aussi.
		REQUIRE(game.TravelTo(player, "dims:overworld").IsOk());
		CHECK(game.GetWorld().BlockAt({2, 10, 2}).id == "dims:carpet");
		CHECK(game.Execute("/where dims:other", player).Value() == "dims:overworld 1");
		// Et de nouveau là-bas, pour y enregistrer le joueur.
		REQUIRE(game.Scripts().RunSource("entity.travel(entity.players()[0], \"dims:other\")").IsOk());
		CHECK(game.CurrentDimension() == "dims:other");
		CHECK(game.GetWorld().BlockAt({3, 20, 3}).id == "dims:stone");
		CHECK(game.Execute("/where dims:other", player).Value() == "dims:other 2");
		REQUIRE(game.Save().IsOk());
		game.CloseWorld();
	}
	// Rechargement : le joueur se retrouve dans sa dimension.
	Game game;
	(void)game.LoadMods(Options(root));
	REQUIRE(game.OpenWorld("monde").IsOk());
	const ecs::Entity player = game.JoinPlayer("Dana");
	CHECK(game.CurrentDimension() == "dims:other");
	CHECK(game.GetWorld().BlockAt({3, 20, 3}).id == "dims:stone");
	Run(game, 0.5f);
	CHECK(T(game, player).position.y > 19.f);
	game.CloseWorld();
}

TEST(Game_PortalsLeadThereAndBack) {
	const String root = MakeMods("portals");
	Game game;
	(void)game.LoadMods(Options(root));
	REQUIRE(game.OpenWorld("monde", Some(uint32_t(4))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Eve");
	Run(game, 1.f);
	const BlockPos at = BlockPos::FromWorld(T(game, player).position);
	(void)game.GetWorld().SetBlock(at, String("dims:portal_other"));
	(void)game.GetWorld().SetBlock(at + BlockPos{0, 1, 0}, String("dims:portal_other"));
	Run(game, 0.3f);
	CHECK(game.CurrentDimension() == "dims:overworld"); // le portail demande un instant
	Run(game, 0.6f);
	CHECK(game.CurrentDimension() == "dims:other");
	auto &p = *game.Registry().GetComponent<Player>(player).Unwrap();
	CHECK(p.returnDimension == "dims:overworld");
	// Marcher jusqu'au portail de retour posé à l'arrivée.
	Run(game, 1.f);
	BlockPos back{0, -1000, 0};
	for (int dx = -4; dx <= 4; ++dx)
		for (int dz = -4; dz <= 4; ++dz)
			for (int dy = -1; dy <= 1; ++dy)
				if (const BlockPos b = BlockPos::FromWorld(T(game, player).position) + BlockPos{dx, dy, dz};
					game.GetWorld().BlockAt(b).id == "dims:portal_back")
					back = b;
	REQUIRE(back.y > -1000);
	T(game, player).position = T(game, player).previous = back.ToVector() + math::FVector3{0.5f, 0.f, 0.5f};
	Run(game, 1.f);
	CHECK(game.CurrentDimension() == "dims:overworld");
	// De retour près du portail d'origine (sans y retomber aussitôt).
	CHECK((T(game, player).position - at.ToVector()).Length() < 3.f);
	Run(game, 1.5f);
	CHECK(game.CurrentDimension() == "dims:overworld");
	game.CloseWorld();
}

TEST(Game_SuffocationFillsTheAirGaugeThenLeadsToTheBackrooms) {
	const String root = MakeMods("suffocation");
	Game game;
	(void)game.LoadMods(Options(root));
	REQUIRE(game.OpenWorld("monde", Some(uint32_t(4))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Finn");
	Run(game, 1.f);
	// Deux blocs de sable tombent sur le joueur : le premier à ses pieds, le
	// second à hauteur de tête — il est pris, et suffoque.
	const BlockPos head = BlockPos::FromWorld(T(game, player).position) + BlockPos{0, 1, 0};
	(void)game.GetWorld().SetBlock(head + BlockPos{0, 4, 0}, String("dims:sand"));
	(void)game.GetWorld().SetBlock(head + BlockPos{0, 3, 0}, String("dims:sand"));
	Run(game, 1.5f);
	CHECK(game.GetWorld().BlockAt(head).id == "dims:sand");
	auto sv = [&] { return game.Registry().GetComponent<Survival>(player).Unwrap(); };
	CHECK(sv()->suffocating);
	CHECK(sv()->air < sv()->maxAir); // le HUD montre la jauge d'air
	CHECK(game.Registry().GetComponent<PhysicsBody>(player).Unwrap()->trapped);
	Run(game, 25.f);
	// Mort par suffocation → les backrooms, debout dans une salle.
	CHECK(game.CurrentDimension() == "dims:backrooms");
	CHECK(!sv()->suffocating);
	Run(game, 1.f);
	const Transform &t = T(game, player);
	CHECK_NEAR(t.position.y, 1.f, 0.05f); // sur le tapis (plancher y = 0)
	CHECK(game.GetWorld().BlockAt(BlockPos::FromWorld(t.position) + BlockPos{0, -1, 0}).id == "dims:carpet");
	CHECK_NEAR(game.Registry().GetComponent<Health>(player).Unwrap()->current, 20.f, 1e-4f);
	// Une autre mort (cause quelconque) ramène au monde ordinaire.
	game.Kill(player);
	Run(game, 0.1f);
	CHECK(game.CurrentDimension() == "dims:overworld");
	game.CloseWorld();
}

TEST(Game_FallingBlockSuffocationCanBeTurnedOff) {
	const String root = MakeMods("no_suffocation");
	Game game;
	(void)game.LoadMods(Options(root));
	REQUIRE(game.OpenWorld("monde", Some(uint32_t(4))).IsOk());
	CHECK(game.Rule("fallingBlockSuffocation")); // actif par défaut
	game.SetRule("fallingBlockSuffocation", false);
	const ecs::Entity player = game.JoinPlayer("Finn");
	Run(game, 1.f);
	const BlockPos head = BlockPos::FromWorld(T(game, player).position) + BlockPos{0, 1, 0};
	(void)game.GetWorld().SetBlock(head + BlockPos{0, 4, 0}, String("dims:sand"));
	(void)game.GetWorld().SetBlock(head + BlockPos{0, 3, 0}, String("dims:sand"));
	Run(game, 1.5f);
	REQUIRE(game.GetWorld().BlockAt(head).id == "dims:sand");
	auto sv = [&] { return game.Registry().GetComponent<Survival>(player).Unwrap(); };
	// Coincé sous le sable, mais sans perdre d'air ni de santé.
	CHECK(game.Registry().GetComponent<PhysicsBody>(player).Unwrap()->trapped);
	Run(game, 15.f);
	CHECK(!sv()->suffocating);
	CHECK_NEAR(sv()->air, sv()->maxAir, 1e-4f);
	CHECK_NEAR(game.Registry().GetComponent<Health>(player).Unwrap()->current, 20.f, 1e-4f);
	CHECK(game.CurrentDimension() == "dims:overworld");
	// Réactivée : le joueur suffoque aussitôt.
	game.SetRule("fallingBlockSuffocation", true);
	Run(game, 1.f);
	CHECK(sv()->suffocating);
	CHECK(sv()->air < sv()->maxAir);
	game.CloseWorld();
}
