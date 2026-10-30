// Tests — structures (voxel/formula.hpp, schematic.hpp, game.hpp) :
// formules, copier / coller / tourner / miroir, fichiers (natif, contenu de
// mod, WorldEdit, Minetest .mts), constructions par formule, dossiers des
// structures du monde et d'échange, API des scripts. Écrit UNIQUEMENT sous
// build/tests/tmp/voxel/.
#include "test.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

#include <voxel/formula.hpp>
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/physics.hpp>
#include <voxel/schematic.hpp>
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

double Eval(const char *text, std::initializer_list<double> values = {0.0, 0.0, 0.0}) {
	Result<Formula, String> f = Formula::Compile(String(text), {"x", "y", "z"});
	if (f.IsError()) {
		std::printf("formule refusée : %s (%s)\n", text, f.Error().CStr());
		return -12345.0;
	}
	return f.Value().Eval(values);
}

/// Monde plat de pierre (y < 10) avec quelques blocs de test.
struct Scene {
	ContentRegistry content;
	std::unique_ptr<World> world;
	Scene() {
		Load(content, R"({"id": "stone"})");
		Load(content, R"({"id": "dirt"})");
		Load(content, R"({"id": "planks"})");
		Load(content, R"({"id": "stairs", "type": "nodebox", "nodebox": [[-0.5, -0.5, -0.5, 0.5, 0, 0.5]]})");
		Load(content, R"({"type": "generator", "id": "flat", "kind": "flat", "layers": [["stone", 10]]})");
		world = std::make_unique<World>(content);
		world->SetGenerator(MakeGenerator(content, "t:flat", 1));
	}
};

String MakeMods(const char *name) {
	const fs::path root = fs::path("build/tests/tmp/voxel") / name;
	fs::remove_all(root);
	const fs::path mod = root / "mods" / "edt";
	Write(mod / "mod.json", R"({"id": "edt", "name": "Éditeur", "version": "1.0",
		"settings": [{"id": "maxCopy", "type": "int", "default": 12}]})");
	Write(mod / "content.json", R"([
		{"type": "texture", "id": "grey", "procedural": {"color": [128, 128, 128]}},
		{"id": "stone", "graphics": {"texture": "grey"}},
		{"id": "glass", "graphics": {"texture": "grey"}},
		{"type": "item", "id": "apple", "icon": "grey"},
		{"id": "chest", "graphics": {"texture": "grey"}, "container": {"slots": 9}},
		{"type": "generator", "id": "flat", "kind": "flat", "layers": [["stone", 10]]},
		{"type": "config", "values": {"world": {"generator": "edt:flat"},
		  "structures": {"aliases": {"default:cobble": "edt:stone"}}}}
	])");
	Write(mod / "init.script", R"(
voxel.command("limit", "/limit", "réglage du mod", fn(sender, args) {
    return format("{}", voxel.setting("maxCopy"))
})
voxel.command("build", "/build", "copie, tourne, colle, enregistre", fn(sender, args) {
    world.fill(0, 10, 0, 2, 11, 0, "stone")
    let info = structure.copy("a", 0, 10, 0, 2, 11, 0)
    structure.rotate("a", 1)
    let n = structure.paste("a", 10, 10, 10)
    let saved = structure.save("a", "mur")
    let loaded = structure.load("mur", "b")
    let bad = structure.load("../evasion", "c")
    let ball = structure.plot("boule", {expression: "x^2 + y^2 + z^2 <= 9", box: [-3, -3, -3, 3, 3, 3], block: "glass"})
    let big = structure.plot("trop", {expression: "x^2 + y^2 + z^2 <= 400", box: [-20, -20, -20, 20, 20, 20], block: "glass", max_size: [16, 16, 16]})
    return format("{} {} {} {} {} {} {} {}", info.blocks, n, saved.ok, loaded.size.z, bad.ok, ball.blocks, big.ok, formula.check("x +"))
})
)");
	return String(root.string().c_str());
}

GameOptions Options(const String &root) {
	GameOptions o;
	o.modDirectories = {root + String("/mods")};
	o.savesDirectory = root + String("/saves");
	o.structureDirectory = root + String("/exchange");
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 64;
	return o;
}

} // namespace

TEST(Formula_ParsesMathsLikeAStudentWritesThem) {
	CHECK_NEAR(Eval("1 + 2 * 3"), 7.0, 1e-12);
	CHECK_NEAR(Eval("2^3^2"), 512.0, 1e-9);          // puissance associative à droite
	CHECK_NEAR(Eval("-2^2"), -4.0, 1e-12);           // - (2²)
	CHECK_NEAR(Eval("2x + 3(y + 1)", {4.0, 1.0, 0.0}), 14.0, 1e-12); // produits implicites
	CHECK_NEAR(Eval("sqrt(x^2 + y^2)", {3.0, 4.0, 0.0}), 5.0, 1e-12);
	CHECK_NEAR(Eval("max(1, x, 7, y)", {9.0, 2.0, 0.0}), 9.0, 1e-12);
	CHECK_NEAR(Eval("if(x > 0, 10, 20)", {-1.0, 0.0, 0.0}), 20.0, 1e-12);
	CHECK_NEAR(Eval("cos(pi)"), -1.0, 1e-12);
	CHECK_NEAR(Eval("mod(-1, 4)"), 3.0, 1e-12);
	CHECK_NEAR(Eval("x^2 + y^2 <= 25 and not z > 1", {3.0, 4.0, 0.0}), 1.0, 1e-12);
	CHECK_NEAR(Eval("x == 0.1 + 0.2", {0.3, 0.0, 0.0}), 1.0, 1e-12); // tolérance des comparaisons
	CHECK(Formula::Compile("x^2 + y^2 <= 25", {"x", "y"}).Value().IsCondition());
	CHECK(!Formula::Compile("x^2 + y^2 - 25", {"x", "y"}).Value().IsCondition());
	// Erreurs lisibles.
	CHECK(Formula::Compile("x +", {"x"}).IsError());
	CHECK(Formula::Compile("q * 2", {"x"}).Error().Contains("q"));
	CHECK(Formula::Compile("sin(1, 2)", {"x"}).IsError());
	CHECK(Formula::Compile("(x", {"x"}).IsError());
	CHECK(Formula::Compile("0 <= x <= 1", {"x"}).IsError()); // enchaînement refusé, avec explication
	CHECK(std::isnan(Eval("sqrt(-1)")));
}

TEST(Schematic_CopyRotateMirrorAndPaste) {
	Scene s;
	World &w = *s.world;
	// Une « L » de planches avec un escalier orienté.
	for (int x = 0; x < 3; ++x)
		(void)w.SetBlock({x, 10, 0}, String("t:planks"));
	(void)w.SetBlock({0, 10, 1}, String("t:planks"));
	(void)w.SetBlock({2, 11, 0}, String("t:stairs"), 1);
	Schematic copy = Schematic::Capture(w, {0, 10, 0}, {2, 11, 1});
	CHECK(copy.Size() == (BlockPos{3, 2, 2}));
	CHECK_EQ(copy.Solid(), int64_t(5));
	CHECK_EQ(copy.Filled(), int64_t(12)); // l'air compte (il efface au collage)
	CHECK_EQ(int(copy.ParamAt({2, 1, 0})), 1);

	// Collage tourné d'un quart : mêmes décalages que les parties d'un bloc.
	const int64_t changed = copy.Paste(w, {20, 10, 20}, {1, false, false});
	CHECK_EQ(changed, int64_t(5));
	for (const BlockPos &p : {BlockPos{0, 0, 0}, BlockPos{1, 0, 0}, BlockPos{2, 0, 0}, BlockPos{0, 0, 1}})
		CHECK(w.BlockAt(BlockPos{20, 10, 20} + RotateOffset(p, 1)).id == "t:planks");
	const BlockPos stair = BlockPos{20, 10, 20} + RotateOffset({2, 1, 0}, 1);
	CHECK(w.BlockAt(stair).id == "t:stairs");
	CHECK_EQ(int(w.GetParam(stair)), 2); // orientation tournée avec la structure
	// Rotated() donne la même chose qu'un collage tourné.
	const Schematic rotated = copy.Rotated(1);
	CHECK(rotated.Size() == (BlockPos{2, 2, 3}));
	(void)rotated.Paste(w, {40, 10, 20}, {0, false, false});
	CHECK(w.BlockAt(BlockPos{40, 10, 20} + RotateOffset({2, 1, 0}, 1)).id == "t:stairs");
	CHECK_EQ(int(w.GetParam(BlockPos{40, 10, 20} + RotateOffset({2, 1, 0}, 1))), 2);
	const auto [lo, hi] = copy.Bounds({20, 10, 20}, 1);
	CHECK(lo == (BlockPos{19, 10, 20}));
	CHECK(hi == (BlockPos{20, 11, 22}));

	// Miroir en x : la « L » change de côté, est ↔ ouest pour l'orientation.
	const Schematic mirrored = copy.Mirrored(0);
	CHECK(mirrored.BlockAt({2, 0, 1}) == "t:planks");
	CHECK(mirrored.BlockAt({0, 0, 1}) == AIR_ID);
	CHECK_EQ(int(mirrored.ParamAt({0, 1, 0})), 3);
	// Fusion : l'air copié n'efface rien.
	(void)w.SetBlock({31, 10, 21}, String("t:dirt"));
	(void)copy.Paste(w, {30, 10, 20}, {0, false, false});
	CHECK(w.BlockAt({31, 10, 21}).id == "t:dirt");
	(void)copy.Paste(w, {30, 10, 20}, {0, true, false});
	CHECK(w.BlockAt({31, 10, 21}).IsAir());
	// Remplacer et vider l'intérieur.
	Schematic cube({3, 3, 3});
	for (int i = 0; i < 27; ++i)
		cube.Set({i % 3, i / 9, (i / 3) % 3}, String("t:stone"));
	CHECK_EQ(cube.Hollow().Solid(), int64_t(26));
	CHECK_EQ(cube.Replace(String("t:stone"), String("t:dirt")), int64_t(27));
	CHECK_EQ(cube.Counts()[String("t:dirt")], int64_t(27));
}

TEST(Schematic_FilesRoundTripInEveryFormat) {
	Scene s;
	Schematic a({3, 2, 4});
	a.Set({0, 0, 0}, String("t:stone"));
	a.Set({2, 0, 3}, String("t:stairs"), 3);
	a.Set({1, 1, 2}, String(AIR_ID));
	a.anchor = {1, 0, 2};
	a.name = "Essai";
	a.containers[{0, 0, 0}] = json::Parse(String(R"([{"item": "t:apple", "count": 3}])")).Value();
	const fs::path dir("build/tests/tmp/voxel/schematic_files");
	fs::remove_all(dir);
	auto check = [&](const Schematic &b, bool exact) {
		CHECK(b.Size() == a.Size());
		CHECK(b.BlockAt({0, 0, 0}) == "t:stone");
		CHECK(b.BlockAt({2, 0, 3}) == "t:stairs");
		CHECK_EQ(int(b.ParamAt({2, 0, 3})), 3);
		CHECK(b.BlockAt({1, 0, 1}).IsEmpty()); // cellule vide
		if (exact) {
			CHECK(b.BlockAt({1, 1, 2}) == AIR_ID);
			CHECK(b.anchor == a.anchor);
		}
	};
	for (const char *file : {"a.vxs.json", "a.json", "a.we", "a.mts"}) {
		const String path((dir / file).string().c_str());
		REQUIRE(a.Save(path).IsOk());
		Result<Schematic, String> b = Schematic::Load(path);
		if (b.IsError())
			std::printf("%s : %s\n", file, b.Error().CStr());
		REQUIRE(b.IsOk());
		// WorldEdit n'enregistre ni l'air ni le point d'ancrage ; ses bornes
		// sont celles des blocs : on recale sur la même boîte.
		if (std::string(file) == "a.we") {
			CHECK(b.Value().Size() == (BlockPos{3, 1, 4}));
			CHECK(b.Value().BlockAt({2, 0, 3}) == "t:stairs");
			CHECK_EQ(int(b.Value().ParamAt({2, 0, 3})), 3);
			continue;
		}
		check(b.Value(), std::string(file) != "a.mts");
	}
	// Natif : le contenu des rangements est conservé.
	Result<Schematic, String> native = Schematic::Load(String((dir / "a.vxs.json").string().c_str()));
	CHECK_EQ(native.Value().containers.size(), size_t(1));
	CHECK(native.Value().name == "Essai");
	// Définition de contenu : chargée telle quelle par un mod (orientations comprises).
	std::ifstream in(dir / "a.json");
	auto def = json::Parse(String(std::string(std::istreambuf_iterator<char>(in), {}).c_str()));
	REQUIRE(def.IsOk());
	REQUIRE(s.content.Load(def.Value(), {"t", "", "test"}).IsOk());
	const StructureDef *structure = s.content.FindStructure("t:a");
	REQUIRE(structure != nullptr);
	s.world->PlaceStructure(*structure, {50, 20, 50}, 1, false);
	const BlockPos stair = BlockPos{50, 20, 50} + RotateOffset(BlockPos{2, 0, 3} - a.anchor, 1);
	CHECK(s.world->BlockAt(stair).id == "t:stairs");
	CHECK_EQ(int(s.world->GetParam(stair)), 0); // 3 + 1 quart de tour
	// WorldEdit de Minetest (clés nues, métadonnées imbriquées, +z au nord).
	Result<Schematic, String> we = Schematic::FromWorldEdit(String(
		R"(5:return {{x = 0, y = 0, z = 0, name = "core:cobble"}, {["x"] = 1, ["y"] = 0, ["z"] = 2, ["name"] = "core:chest", ["param2"] = 0, ["meta"] = {["fields"] = {["infotext"] = "a {b}"}}}})"));
	REQUIRE(we.IsOk());
	CHECK(we.Value().Size() == (BlockPos{2, 1, 3}));
	CHECK(we.Value().BlockAt({0, 0, 2}) == "core:cobble"); // z inversé
	CHECK_EQ(int(we.Value().ParamAt({1, 0, 0})), 2);         // façade au sud (Minetest 0)
	// Fichiers abîmés : refusés proprement.
	CHECK(Schematic::FromMts({'M', 'T', 'S', 'M', 0, 4, 0, 2}).IsError());
	CHECK(Schematic::FromWorldEdit(String("5:return {}")).IsError());
	CHECK(Schematic::Load(String("build/tests/tmp/voxel/absent.mts")).IsError());
	CHECK(Schematic::FormatOf(String("maison.txt")).IsEmpty());
}

TEST(Schematic_PlotsFormulasLikeMathplot) {
	PlotSpec ball;
	ball.expression = "x^2 + y^2 + z^2 <= 10^2";
	ball.min = {-10, -10, -10};
	ball.max = {10, 10, 10};
	ball.block = "t:stone";
	Result<Schematic, String> solid = Plot(ball);
	REQUIRE(solid.IsOk());
	CHECK(solid.Value().Size() == (BlockPos{21, 21, 21}));
	CHECK(solid.Value().anchor == (BlockPos{10, 10, 10})); // l'origine de la formule
	const double volume = 4.0 / 3.0 * M_PI * 1000.0;
	CHECK(std::fabs(double(solid.Value().Solid()) - volume) < volume * 0.03);
	// Coquille : expression qui s'annule sur la sphère.
	ball.expression = "x^2 + y^2 + z^2 - 10^2";
	Result<Schematic, String> shell = Plot(ball);
	REQUIRE(shell.IsOk());
	CHECK(shell.Value().Solid() < solid.Value().Solid() / 3);
	CHECK(shell.Value().BlockAt({20, 10, 10}) == "t:stone"); // sur la surface
	CHECK(shell.Value().BlockAt({10, 10, 10}).IsEmpty());    // pas au centre
	// Creuser une forme pleine : pareil, en une couche.
	ball.expression = "x^2 + y^2 + z^2 <= 10^2";
	ball.hollow = true;
	CHECK(Plot(ball).Value().Solid() < solid.Value().Solid() / 3);
	// Convention mathématique : z monte. Un plan z = 0 est horizontal dans le jeu.
	PlotSpec plane;
	plane.expression = "z";
	plane.min = {-3, -3, -3};
	plane.max = {3, 3, 3};
	plane.block = "t:stone";
	CHECK(Plot(plane).Value().Size() == (BlockPos{7, 1, 7}));
	plane.zUp = false;
	CHECK(Plot(plane).Value().Size() == (BlockPos{7, 7, 1}));
	// Courbe paramétrique : une hélice continue (pas de trou entre deux points).
	PlotSpec helix;
	helix.kind = PlotSpec::Kind::CURVE;
	helix.fx = "8cos(t)";
	helix.fy = "8sin(t)";
	helix.fz = "t";
	helix.range[0][1] = 4.0 * M_PI;
	helix.samples[0] = 16; // peu de points : les segments comblent
	helix.block = "t:stone";
	Result<Schematic, String> curve = Plot(helix);
	REQUIRE(curve.IsOk());
	CHECK_EQ(curve.Value().Size().y, 14); // z ∈ [0, 4π] → 13 blocs de haut + 1
	CHECK(curve.Value().Solid() > 100);
	// Surface paramétrique : un disque plein (pas de trous entre les rayons).
	PlotSpec disc;
	disc.kind = PlotSpec::Kind::SURFACE;
	disc.fx = "u cos(v)";
	disc.fy = "u sin(v)";
	disc.fz = "0";
	disc.range[0][1] = 6.0;
	disc.range[1][1] = 2.0 * M_PI;
	disc.samples[0] = 4;
	disc.samples[1] = 12;
	disc.block = "t:stone";
	Result<Schematic, String> surface = Plot(disc);
	REQUIRE(surface.IsOk());
	CHECK(std::fabs(double(surface.Value().Solid()) - M_PI * 36.0) < 25.0);
	// Limites : taille et formules fautives.
	ball.hollow = false;
	ball.maxSize = {16, 16, 16};
	CHECK(Plot(ball).Error().Contains("trop grande"));
	ball.expression = "x^2 +";
	CHECK(Plot(ball).IsError());
	ball.expression = "x > 100";
	ball.maxSize = {64, 64, 64};
	CHECK(Plot(ball).Error().Contains("aucun bloc"));
}

TEST(Game_StructuresLiveInTheWorldAndTheExchangeFolder) {
	const String root = MakeMods("structures_game");
	Game game;
	const ModReport report = game.LoadMods(Options(root));
	for (const String &e : report.errors)
		std::printf("erreur : %s\n", e.CStr());
	REQUIRE(report.errors.empty());
	REQUIRE(game.OpenWorld("monde", Some(uint32_t(9))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Ana");
	// Les fonctions des mods gardent leur mod pour contexte (réglages…).
	CHECK(game.Execute("/limit", player).Value() == "12");

	// Un coffre rempli, copié puis collé ailleurs : le contenu suit.
	(void)game.GetWorld().SetBlock({3, 10, 3}, String("edt:chest"));
	REQUIRE(game.BlockInventory({3, 10, 3}) != nullptr);
	(void)game.BlockInventory({3, 10, 3})->Add(game.Content(), "edt:apple", 5);
	Schematic copy = game.CaptureRegion({2, 10, 2}, {4, 11, 4});
	CHECK_EQ(copy.containers.size(), size_t(1));
	CHECK(game.PasteSchematic(copy, {20, 10, 20}) > 0);
	REQUIRE(game.BlockInventory({21, 10, 21}) != nullptr);
	CHECK_EQ(game.BlockInventory({21, 10, 21})->Count("edt:apple"), 5);

	// Fichiers : dossier du monde, dossier d'échange, noms refusés.
	const Result<String, String> saved = game.SaveStructureFile(copy, "coffre");
	REQUIRE(saved.IsOk());
	CHECK(saved.Value().EndsWith("/saves/monde/structures/coffre.vxs.json"));
	CHECK(game.SaveStructureFile(copy, "coffre.mts", true).IsOk());
	CHECK(game.StructureFiles() == std::vector<String>{String("coffre.vxs.json")});
	CHECK(game.StructureFiles(true) == std::vector<String>{String("coffre.mts")});
	CHECK(game.SaveStructureFile(copy, "../dehors").IsError());
	CHECK(game.SaveStructureFile(copy, "a/b").IsError());
	CHECK(game.LoadStructureFile("coffre").Value().containers.size() == size_t(1));
	CHECK(game.LoadStructureFile("coffre", true).IsOk()); // extension retrouvée
	CHECK(game.DeleteStructureFile("coffre.mts", true).IsOk());
	CHECK(game.StructureFiles(true).empty());
	// Import d'un fichier Minetest : alias de la configuration.
	Write(fs::path(game.StructuresDirectory(true).CStr()) / "mt.we", R"(5:return {{x=0,y=0,z=0,name="core:cobble"},{x=1,y=0,z=0,name="mod_absent:truc"}})");
	std::vector<String> unknown;
	Result<Schematic, String> mt = game.LoadStructureFile("mt.we", true, &unknown);
	REQUIRE(mt.IsOk());
	CHECK(mt.Value().BlockAt({0, 0, 0}) == "edt:stone");
	CHECK(unknown == std::vector<String>{String("mod_absent:truc")});

	// Zones : remplir (modes), remplacer.
	CHECK_EQ(game.FillRegion({0, 20, 0}, {4, 24, 4}, "edt:stone", "hollow").Value(), int64_t(98));
	CHECK(game.GetWorld().BlockAt({2, 22, 2}).IsAir());
	CHECK_EQ(game.FillRegion({10, 20, 0}, {14, 24, 4}, "edt:stone", "walls").Value(), int64_t(80));
	CHECK_EQ(game.FillRegion({20, 20, 0}, {24, 24, 4}, "edt:stone", "outline").Value(), int64_t(44));
	CHECK_EQ(game.ReplaceRegion({0, 20, 0}, {4, 24, 4}, "edt:stone", "edt:glass").Value(), int64_t(98));
	CHECK(game.FillRegion({0, 0, 0}, {1, 1, 1}, "edt:nope").IsError());
	CHECK(game.FillRegion({0, 0, 0}, {1, 1, 1}, "edt:stone", "spiral").IsError());

	// Scripts : copier, tourner, coller, enregistrer, relire, formules, limites.
	Result<String, String> built = game.Execute("/build", player);
	if (built.IsError())
		std::printf("/build : %s\n", built.Error().CStr());
	CHECK(built.IsOk() && built.Value().StartsWith("6 6 true 3 false "));
	CHECK(built.IsOk() && built.Value().EndsWith(" false position 4 : formule incomplète"));
	CHECK(game.GetWorld().BlockAt({10, 10, 12}).id == "edt:stone"); // la ligne tournée vers +z
	CHECK(game.Clipboard("boule") != nullptr && game.Clipboard("boule")->Solid() > 100);
	// Surlignage (dessiné par l'application).
	game.SetHighlight("sel", {5, 5, 5}, {1, 1, 1}, {255, 0, 0, 255});
	REQUIRE_EQ(game.Highlights().size(), size_t(1));
	CHECK(game.Highlights()[0].min == (BlockPos{1, 1, 1}));
	game.ClearHighlight("sel");
	CHECK(game.Highlights().empty());
	game.CloseWorld();
}
