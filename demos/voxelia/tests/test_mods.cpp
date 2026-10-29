// Tests — le contenu livré (mods core, backrooms, edit) se charge sans erreur,
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

namespace {

/// Le joueur regarde le sol, droit devant lui (vers +z), d'un peu haut.
void LookDown(Game &game, ecs::Entity player) {
	auto t = game.Registry().GetComponent<Transform>(player).Unwrap();
	t->yaw = 0.f;
	t->pitch = -1.2f;
}

String Run(Game &game, ecs::Entity player, const char *line) {
	Result<String, String> r = game.Execute(String(line), player);
	if (r.IsError())
		std::printf("%s → ERREUR %s\n", line, r.Error().CStr());
	return r.IsOk() ? r.Value() : r.Error();
}

} // namespace

TEST(Mods_EditBuildsShapesFormulasAndStoresStructures) {
	const char *dir = "build/tests/tmp/voxelia/edit";
	std::filesystem::remove_all(dir);
	GameOptions o = Options(dir);
	o.structureDirectory = String(dir) + String("/exchange");
	o.creative = true;
	// Limites réduites (réglages du mod pour ce monde).
	o.modSettings = json::Parse(String(R"({"edit": {"copy_max_x": 20, "copy_max_y": 20, "copy_max_z": 20,
		"shape_max_x": 24, "shape_max_y": 24, "shape_max_z": 24}})")).Value();
	Game game;
	const ModReport report = game.LoadMods(o);
	for (const String &e : report.errors)
		std::printf("ERREUR %s\n", e.CStr());
	REQUIRE(report.errors.empty());
	REQUIRE(game.OpenWorld("edition", Some(uint32_t(5))).IsOk());
	game.SetMode(GameMode::CREATIVE);
	const ecs::Entity player = game.JoinPlayer("Testeur");
	for (int i = 0; i < 60; ++i)
		game.Tick(1.f / 60.f);
	LookDown(game, player);
	REQUIRE(game.TargetBlock(player).IsSome());
	World &world = game.GetWorld();
	const BlockPos ground = game.TargetBlock(player).Value().previous; // cellule où les formes sont posées

	CHECK(Run(game, player, "/edit limits") == "Zone copiable : 20×20×20 · forme : 24×24×24 · annulations : 10");
	// Toutes les formes : posées sur le sol, centrées, puis annulées.
	const char *shapes[] = {"/edit shape line core:glass 8", "/edit shape circle core:glass 9 creuse", "/edit shape ellipse core:glass 11 1 7",
							"/edit shape rectangle core:glass 6 1 4 creuse", "/edit shape parallelepiped core:glass 5 4 3 creuse",
							"/edit shape sphere core:glass 9", "/edit shape ellipsoid core:glass 11 7 9", "/edit shape cylinder core:glass 7 6 7",
							"/edit shape cone core:glass 9 8 9", "/edit shape pyramid core:glass 9 5 9 creuse", "/edit shape dome core:glass 12 6 12",
							"/edit shape torus core:glass 15 4"};
	for (const char *line : shapes) {
		const String out = Run(game, player, line);
		std::printf("%s → %s\n", line, out.CStr());
		CHECK(out.Contains("blocs)"));
		CHECK(world.BlockAt(ground).id == "core:glass" || String(line).Contains("torus") || String(line).Contains("creuse"));
		CHECK(Run(game, player, "/edit undo").StartsWith("Annulé"));
		CHECK(!(world.BlockAt(ground).id == "core:glass"));
	}
	// Sphère de 9 : 9 de haut, posée sur le sol.
	(void)Run(game, player, "/edit shape sphere core:glass 9");
	CHECK(world.BlockAt(ground + BlockPos{0, 8, 0}).id == "core:glass");
	CHECK(!(world.BlockAt(ground + BlockPos{0, 9, 0}).id == "core:glass"));
	CHECK(world.BlockAt(ground + BlockPos{4, 4, 0}).id == "core:glass");
	CHECK(!(world.BlockAt(ground + BlockPos{5, 4, 0}).id == "core:glass"));
	// Limites des formes.
	CHECK(Run(game, player, "/edit shape sphere core:glass 30").Contains("Forme trop grande"));
	CHECK(Run(game, player, "/edit shape sphere core:nope 5").Contains("Bloc inconnu"));
	// Formule (z vers le haut) : un cône renversé.
	CHECK(Run(game, player, "/edit plot core:glass x^2 + y^2 <= z^2 and z >= 0 and z <= 6").Contains("Formule construite"));
	CHECK(Run(game, player, "/edit plot core:glass x^2 +").Contains("Formule"));

	// Formulaires : forme, formule paramétrique, exemples.
	(void)game.TakeUiRequests();
	uint64_t cursor = 0;
	auto submit = [&](const char *form, const char *values) {
		game.SubmitForm(player, String(form), json::Parse(String(values)).Value());
		ecs::EventsOf<GameMessage>(game.Registry()).Read(cursor, [&](const GameMessage &m) { std::printf("[%s] %s\n", form, m.text.CStr()); });
	};
	CHECK(Run(game, player, "/edit undo").StartsWith("Annulé"));
	CHECK(Run(game, player, "/edit undo").StartsWith("Annulé"));
	// Les outils : la baguette (deux coins), copier, coller.
	(void)Run(game, player, "/edit tools");
	auto inv = game.Registry().GetComponent<InventoryComponent>(player).Unwrap();
	auto pl = game.Registry().GetComponent<Player>(player).Unwrap();
	auto hold = [&](const char *tool) {
		for (int i = 0; i < inv->inventory.Size(); ++i)
			if (inv->inventory.Slot(i).item == tool)
				std::swap(inv->inventory.Slot(i), inv->inventory.Slot(pl->selectedSlot));
	};
	hold("edit:tool_shape");
	CHECK(game.UseHeldItem(player));
	std::vector<UiRequest> ui = game.TakeUiRequests();
	REQUIRE_EQ(ui.size(), size_t(1));
	CHECK(ui[0].kind == "form" && json::Str(ui[0].data, "id") == "edit:shape");
	submit("edit:shape", R"({"_button": "submit", "shape": "cylinder", "block": "core:glass", "sx": 5, "sy": 3, "sz": 5, "axis": "y", "hollow": false})");
	CHECK(world.BlockAt(ground + BlockPos{0, 2, 0}).id == "core:glass");
	hold("edit:tool_formula");
	const BlockPos origin = game.TargetBlock(player).Value().previous; // sur le cylindre
	CHECK(game.UseHeldItem(player));
	ui = game.TakeUiRequests();
	REQUIRE_EQ(ui.size(), size_t(1));
	submit("edit:formula", R"({"_button": "example", "example": "helix"})"); // rouvre le formulaire avec l'exemple
	ui = game.TakeUiRequests();
	REQUIRE_EQ(ui.size(), size_t(1));
	submit("edit:formula", R"J({"_button": "submit", "kind": "curve", "x": "5cos(t)", "y": "5sin(t)", "z": "t", "umin": "0", "umax": "2pi",
		"un": "40", "scale": "1", "z_up": true, "hollow": false, "block": "core:glass"})J");
	CHECK(world.BlockAt(origin + BlockPos{5, 0, 0}).id == "core:glass"); // t = 0 : (5, 0, 0)
	CHECK(world.BlockAt(origin + BlockPos{-5, 3, 0}).id == "core:glass"); // t = π : (−5, 0, π), z vers le haut

	// Sélection à la baguette (deux clics), copie, collage et limites de la zone.
	hold("edit:tool_wand");
	CHECK(game.UseHeldItem(player));
	auto t = game.Registry().GetComponent<Transform>(player).Unwrap();
	t->position.x += 3.f;
	CHECK(game.UseHeldItem(player));
	REQUIRE_EQ(game.Highlights().size(), size_t(1));
	CHECK(Run(game, player, "/edit pos1 0 70 0").StartsWith("Coin 1"));
	CHECK(Run(game, player, "/edit pos2 25 72 3").Contains("26×3×4"));
	CHECK(Run(game, player, "/edit copy").Contains("trop grande"));
	CHECK(Run(game, player, "/edit pos2 9 72 3").Contains("10×3×4"));
	CHECK(Run(game, player, "/edit copy").StartsWith("Copié : 10×3×4"));
	// Bibliothèque : enregistrer, exporter (Minetest), importer.
	submit("edit:library", R"({"_button": "save", "name": "bloc"})");
	submit("edit:library", R"({"_button": "export", "name": "bloc", "format": "mts"})");
	CHECK(game.StructureFiles() == std::vector<String>{String("bloc.vxs.json")});
	CHECK(game.StructureFiles(true) == std::vector<String>{String("bloc.mts")});
	submit("edit:library", R"({"_button": "import", "name": "venu_d_ailleurs", "shared": "bloc.mts"})");
	CHECK(game.StructureFiles().size() == size_t(2));
	CHECK(ScriptErrors(game) == 0);
	game.CloseWorld();
}
