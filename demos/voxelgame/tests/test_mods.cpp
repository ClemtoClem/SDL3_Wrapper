// Tests — le contenu livré (mods/core et mods/extra) se charge sans erreur,
// ses scripts sont valides et une partie tourne sans fenêtre.
#include "test.hpp"

#include <filesystem>

#include <ecs/events.hpp>
#include <voxel/game.hpp>
#include <voxel/meshing.hpp>
#include <voxel/scripting.hpp>

using namespace voxel;

namespace {

GameOptions Options(const char *saves) {
	GameOptions o;
	o.modDirectories = {String("demos/voxelgame/mods")};
	o.savesDirectory = String(saves);
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 200;
	return o;
}

} // namespace

TEST(Mods_ShippedContentLoadsCleanly) {
	std::filesystem::remove_all("build/tests/tmp/voxelgame/mods");
	Game game;
	const ModReport report = game.LoadMods(Options("build/tests/tmp/voxelgame/mods"));
	for (const String &e : report.errors)
		std::printf("ERREUR %s\n", e.CStr());
	for (const String &w : report.warnings)
		std::printf("AVERTISSEMENT %s\n", w.CStr());
	CHECK(report.errors.empty());
	CHECK(report.warnings.empty());
	REQUIRE_EQ(report.mods.size(), size_t(2));
	CHECK(report.mods[0].id == "core" && report.mods[1].id == "extra"); // dépendance d'abord
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
	std::filesystem::remove_all("build/tests/tmp/voxelgame/run");
	Game game;
	(void)game.LoadMods(Options("build/tests/tmp/voxelgame/run"));
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
