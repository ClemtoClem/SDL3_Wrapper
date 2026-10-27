// Tests — le contenu livré (mods/core et mods/extra) se charge sans erreur,
// ses scripts sont valides et une partie tourne sans fenêtre.
#include "test.hpp"

#include <filesystem>

#include <ecs/events.hpp>
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/meshing.hpp>
#include <voxel/scripting.hpp>

using namespace voxel;

namespace {

GameOptions Options(const char *saves) {
	GameOptions o;
	o.modDirectories = {String("demos/voxelia/mods")};
	o.savesDirectory = String(saves);
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 200;
	return o;
}

} // namespace

TEST(Mods_ShippedContentLoadsCleanly) {
	std::filesystem::remove_all("build/tests/tmp/voxelia/mods");
	Game game;
	const ModReport report = game.LoadMods(Options("build/tests/tmp/voxelia/mods"));
	for (const String &e : report.errors)
		std::printf("ERREUR %s\n", e.CStr());
	for (const String &w : report.warnings)
		std::printf("AVERTISSEMENT %s\n", w.CStr());
	CHECK(report.errors.empty());
	CHECK(report.warnings.empty());
	REQUIRE_EQ(report.mods.size(), size_t(3));
	CHECK(report.mods[0].id == "core"); // dépendance d'abord
	CHECK(report.mods[1].id == "backrooms" && report.mods[2].id == "extra");
	// Les dimensions : celles du mod core et les backrooms.
	CHECK_EQ(game.Content().Dimensions().size(), size_t(7));
	CHECK(game.Content().DefaultDimension() == "core:overworld");
	// Les exemples du cahier des charges sont présents tels quels.
	for (const char *id : {"core:grass", "core:water", "core:sand", "core:ice", "core:shelf", "core:shelf_fixed"})
		CHECK(game.Content().FindBlock(String(id)) != nullptr);
	// Le mod extra surcharge le générateur du mod core.
	const GeneratorDef *overworld = game.Content().FindGenerator("core:overworld");
	REQUIRE(overworld != nullptr);
	CHECK(overworld->mod == "extra");

	// Atlas complet : toutes les textures existent (fichiers ou procédurales).
	bool absent = false;
	TextureAtlas atlas;
	atlas.Build(game.Assets(), game.Content(), 16);
	for (const BlockDef &b : game.Content().Blocks())
		if (!b.IsAir())
			for (const String &face : b.look.faces)
				if (!face.IsEmpty() && !atlas.Contains(face))
					std::printf("texture absente de l'atlas : %s (%s)\n", face.CStr(), b.id.CStr()), absent = true;
	CHECK(!absent);
	for (const resources::MissingAsset &m : game.Assets().Missing())
		std::printf("MANQUANT %s : %s\n", m.context.CStr(), m.reason.CStr());
	CHECK(game.Assets().Missing().empty());

	// Textures composées (« a^b^filtre() ») : celles du mod extra combinent
	// des textures du mod core et un filtre.
	const BlockDef *dark = game.Content().FindBlock("extra:dark_grass");
	REQUIRE(dark != nullptr);
	CHECK(dark->look.Texture(Face::NORTH) == "extra:{(core:dirt^core:grass_top)^ligthing(0.5)}");
	std::shared_ptr<TextureImage> side = game.Assets().Texture(dark->look.Texture(Face::NORTH));
	std::shared_ptr<TextureImage> grass = game.Assets().Texture("core:grass_top");
	REQUIRE(side != nullptr && grass != nullptr);
	REQUIRE_EQ(side->Pixels().rgba.size(), grass->Pixels().rgba.size());
	CHECK(side->Pixels().At(3, 12)[3] == 255);                                            // opaque
	CHECK(int(side->Pixels().At(3, 12)[1]) <= int(grass->Pixels().At(3, 12)[1]) / 2 + 1); // assombrie

	// Sons du dépôt : variantes et jeux de sons par matière.
	const SoundDef *step = game.Content().FindSound("core:stone_footstep");
	REQUIRE(step != nullptr);
	CHECK_EQ(step->paths.size(), size_t(5));
	CHECK(std::filesystem::exists(step->paths.back().CStr()));
	const BlockDef *stone = game.Content().FindBlock("core:stone");
	REQUIRE(stone != nullptr);
	CHECK(stone->sounds.group == "core:stone");
	CHECK(stone->sounds.Get("step") == "core:stone_footstep");
	CHECK(stone->sounds.Get("hit") == "core:stone_hit");
	CHECK(game.Content().FindBlock("core:grass")->sounds.Get("step") == "core:step"); // exemple tel quel
	// Modèles : .obj, .b3d et json se chargent.
	for (const char *block : {"core:table", "core:chair", "core:lamp"}) {
		const BlockDef *def = game.Content().FindBlock(String(block));
		REQUIRE(def != nullptr);
		CHECK(game.Assets().Model(def->shape.model) != nullptr);
	}
}

TEST(Mods_AGameRunsWithShippedContent) {
	std::filesystem::remove_all("build/tests/tmp/voxelia/run");
	Game game;
	(void)game.LoadMods(Options("build/tests/tmp/voxelia/run"));
	REQUIRE(game.OpenWorld("essai", Some(uint32_t(1234))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Testeur");
	for (int i = 0; i < 240; ++i)
		game.Tick(1.f / 60.f);
	CHECK(game.Registry().GetComponent<PhysicsBody>(player).Unwrap()->onGround);
	// Commandes du mod (script) et du moteur.
	CHECK(game.Execute("/night").IsOk());
	CHECK(game.DayFraction() > 0.8f);
	CHECK(game.Execute("/sethome").IsOk());
	CHECK(game.Execute("/give core:coal 2").IsOk());
	CHECK(game.Execute("/give core:sand 2").IsOk());
	auto cooked = game.Execute("/cook glass 2");
	CHECK(cooked.IsOk() && cooked.Value().Contains("2"));
	CHECK(game.Execute("/rubies").IsOk());
	CHECK(game.Execute("/house").IsOk());
	CHECK(game.Execute("/spawn core:merchant").IsOk());
	CHECK(game.Execute("/spawn core:ghost").IsOk());
	for (int i = 0; i < 120; ++i)
		game.Tick(1.f / 60.f);
	// TNT : allumée par clic droit (script), le bloc disparaît.
	auto t = game.Registry().GetComponent<Transform>(player).Unwrap();
	const BlockPos tnt = BlockPos::FromWorld(t->position) + BlockPos{3, 0, 0};
	(void)game.GetWorld().SetBlock(tnt, String("core:tnt"));
	t->yaw = 1.5707963f; // regarde vers +x
	t->pitch = -0.35f;
	(void)game.Scripts().Emit("block_use", {script::Value::Int(tnt.x), script::Value::Int(tnt.y), script::Value::Int(tnt.z),
											script::Value::Str("core:tnt"), game.Scripts().EntityValue(player)});
	for (int i = 0; i < 200; ++i)
		game.Tick(1.f / 60.f);
	CHECK(game.GetWorld().BlockAt(tnt).IsAir());
	for (const String &line : game.Log())
		if (line.StartsWith("[script") || line.StartsWith("[erreur"))
			std::printf("%s\n", line.CStr());
	bool scriptErrors = false;
	for (const String &line : game.Log())
		scriptErrors = scriptErrors || line.StartsWith("[script");
	CHECK(!scriptErrors);
	game.CloseWorld();
}

namespace {

/// Le joueur est-il debout (sol sous les pieds ou, gravité inversée, au-dessus de la tête) ?
bool Standing(Game &game, ecs::Entity player) {
	return game.Registry().GetComponent<PhysicsBody>(player).Unwrap()->onGround;
}

size_t ScriptErrors(const Game &game) {
	size_t n = 0;
	for (const String &line : game.Log())
		if (line.StartsWith("[script") || line.StartsWith("[erreur"))
			std::printf("JOURNAL %s\n", line.CStr()), ++n;
	return n;
}

} // namespace

TEST(Mods_EveryDimensionCanBeVisited) {
	std::filesystem::remove_all("build/tests/tmp/voxelia/dimensions");
	Game game;
	(void)game.LoadMods(Options("build/tests/tmp/voxelia/dimensions"));
	REQUIRE(game.OpenWorld("dimensions", Some(uint32_t(77))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Voyageur");
	for (int i = 0; i < 120; ++i)
		game.Tick(1.f / 60.f);
	for (const char *id : {"core:inverted", "core:living", "core:fragmented", "core:core", "core:void", "backrooms:backrooms"}) {
		REQUIRE(game.TravelTo(player, id).IsOk());
		CHECK(game.CurrentDimension() == id);
		for (int i = 0; i < 180; ++i)
			game.Tick(1.f / 60.f);
		const math::FVector3 p = game.Registry().GetComponent<Transform>(player).Unwrap()->position;
		std::printf("  %s : arrivée (%.1f, %.1f, %.1f), %d chunks\n", id, double(p.x), double(p.y), double(p.z),
					int(game.GetWorld().LoadedChunkCount()));
		CHECK(Standing(game, player));
		CHECK_NEAR(game.Registry().GetComponent<Health>(player).Unwrap()->current, 20.f, 4.f);
		CHECK(game.CurrentDimension() == id); // pas renvoyé ailleurs (mort, portail sous les pieds)
	}
	CHECK(game.Execute("/dimension overworld", player).IsOk());
	for (int i = 0; i < 30; ++i)
		game.Tick(1.f / 60.f);
	CHECK(game.CurrentDimension() == "core:overworld");
	CHECK(game.Save().IsOk());
	CHECK_EQ(ScriptErrors(game), size_t(0));
}

TEST(Mods_QuicksandSuffocationWakesThePlayerInTheBackrooms) {
	std::filesystem::remove_all("build/tests/tmp/voxelia/quicksand");
	Game game;
	(void)game.LoadMods(Options("build/tests/tmp/voxelia/quicksand"));
	REQUIRE(game.OpenWorld("sables", Some(uint32_t(5))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Imprudent");
	for (int i = 0; i < 180 && !Standing(game, player); ++i)
		game.Tick(1.f / 60.f);
	REQUIRE(Standing(game, player));
	// Une fosse de sables mouvants sous le joueur, au fond de pierre.
	const BlockPos feet = BlockPos::FromWorld(game.Registry().GetComponent<Transform>(player).Unwrap()->position);
	for (int dy = -7; dy <= -1; ++dy)
		for (int dz = -3; dz <= 3; ++dz)
			for (int dx = -3; dx <= 3; ++dx)
				(void)game.GetWorld().SetBlock(feet + BlockPos{dx, dy, dz}, String(dy == -7 || std::abs(dx) == 3 || std::abs(dz) == 3 ? "core:stone" : "core:quicksand"));
	bool suffocated = false;
	for (int i = 0; i < 60 * 40 && game.CurrentDimension() == "core:overworld"; ++i) {
		game.Tick(1.f / 60.f);
		suffocated = suffocated || game.Registry().GetComponent<Survival>(player).Unwrap()->suffocating;
	}
	CHECK(suffocated); // la jauge d'air s'est vidée…
	CHECK(game.CurrentDimension() == "backrooms:backrooms"); // …et l'on se réveille dans les backrooms
	for (int i = 0; i < 60; ++i)
		game.Tick(1.f / 60.f);
	CHECK(Standing(game, player));
	CHECK_EQ(ScriptErrors(game), size_t(0));
}

TEST(Mods_EveryPieceOfFurnitureWorks) {
	std::filesystem::remove_all("build/tests/tmp/voxelia/furniture");
	Game game;
	(void)game.LoadMods(Options("build/tests/tmp/voxelia/furniture"));
	REQUIRE(game.OpenWorld("meubles", Some(uint32_t(77))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Testeur");
	for (int i = 0; i < 60; ++i)
		game.Tick(1.f / 60.f);
	World &world = game.GetWorld();
	const BlockPos base = BlockPos::FromWorld(game.Registry().GetComponent<Transform>(player).Unwrap()->position) + BlockPos{-15, 18, -15};
	game.Registry().GetComponent<Transform>(player).Unwrap()->yaw = 0.7f; // orientation 1/4 de tour : décalages tournés
	(void)game.TakeUiRequests();
	int tested = 0, doors = 0, beds = 0, containers = 0;
	for (const ItemDef *item : game.Content().Items()) {
		const BlockDef *def = game.Content().FindBlock(item->placesBlock);
		if (!def || !def->HasTag("furniture") || item->mod != "core")
			continue;
		// Une case dégagée sur une dalle de pierre, au-dessus du terrain.
		const BlockPos at = base + BlockPos{(tested % 10) * 3, 0, (tested / 10) * 3};
		for (int dx = -1; dx <= 1; ++dx)
			for (int dz = -1; dz <= 1; ++dz) {
				(void)world.SetBlock(at + BlockPos{dx, -1, dz}, String("core:stone"), 0, ChangeCause::SCRIPT);
				for (int dy = 0; dy <= 2; ++dy)
					(void)world.SetBlock(at + BlockPos{dx, dy, dz}, AIR, 0, ChangeCause::SCRIPT);
			}
		++tested;
		if (!game.PlaceBlock(player, at, def->id)) {
			std::printf("pose refusée : %s\n", def->id.CStr());
			CHECK(false);
			continue;
		}
		const uint8_t param = world.GetParam(at);
		for (const BlockDef::Part &part : def->parts) {
			CHECK(world.BlockAt(at + RotateOffset(part.offset, param)).id == part.block);
			CHECK(game.MainPart(at + RotateOffset(part.offset, param)) == at);
		}
		if (!def->toggle.IsEmpty()) {
			++doors;
			const BlockPos use = def->parts.empty() ? at : at + RotateOffset(def->parts[0].offset, param);
			CHECK(game.UseBlock(player, use));
			CHECK(world.BlockAt(at).id == def->toggle);
			CHECK(game.UseBlock(player, at));
			CHECK(world.BlockAt(at).id == def->id);
		}
		if (def->use == "sleep") {
			++beds;
			CHECK(game.UseBlock(player, at));
		}
		if (def->IsContainer()) {
			++containers;
			CHECK(game.UseBlock(player, at));
			const std::vector<UiRequest> ui = game.TakeUiRequests();
			CHECK(ui.size() == 1 && ui[0].kind == "container" && json::Int(ui[0].data, "slots", 0) == def->containerSlots);
		}
		CHECK(game.BreakBlock(player, def->parts.empty() ? at : at + RotateOffset(def->parts.back().offset, param)));
		CHECK(world.BlockAt(at).IsAir());
		for (const BlockDef::Part &part : def->parts)
			CHECK(world.BlockAt(at + RotateOffset(part.offset, param)).IsAir());
	}
	std::printf("%d meubles (%d à bascule, %d lits, %d rangements) ", tested, doors, beds, containers);
	CHECK(tested >= 60);
	CHECK(doors >= 40);
	CHECK_EQ(beds, 16);
	CHECK(containers >= 8);
	CHECK(game.Content().FindRecipe("core:bed_blue") && game.Content().FindRecipe("core:oak_door") && game.Content().FindRecipe("core:chest"));
	CHECK_EQ(ScriptErrors(game), size_t(0));
	game.CloseWorld();
}
