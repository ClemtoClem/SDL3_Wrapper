// Tests — éditeur de structures de Voxelia Studio sans fenêtre : document
// (annulation bornée, transformations, cellules, ancre, comptes), formules
// (vérification avec la position de l'erreur, tracés implicite et
// paramétrique), fichiers dans les quatre formats avec résolution des noms,
// et capture / collage / annulation dans une vraie partie.
#include "test.hpp"

#include <filesystem>
#include <fstream>

#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxelia/studio/structure_document.hpp>

using namespace voxelia::studio;
using voxel::BlockPos;
using voxel::PlotSpec;

namespace {

voxel::GameOptions StructureOptions(const char *saves) {
	voxel::GameOptions o;
	o.modDirectories = {String("demos/voxelia/mods")};
	o.savesDirectory = String(saves);
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 200;
	return o;
}

/// Tour 3 × 4 × 3 : pierre, fenêtre de verre, planches au sommet.
voxel::Schematic Tower() {
	voxel::Schematic s(BlockPos{3, 4, 3});
	for (int y = 0; y < 4; ++y)
		for (int z = 0; z < 3; ++z)
			for (int x = 0; x < 3; ++x)
				s.Set({x, y, z}, String(y == 3 ? "core:planks" : "core:stone"));
	s.Set({1, 1, 0}, String("core:glass"));
	return s;
}

} // namespace

TEST(StudioStructures_TransformsAndUndo) {
	StructureDocument doc(Tower(), String("tour"));
	CHECK(!doc.Dirty());
	const uint64_t v0 = doc.Version();
	doc.Rotate(1);
	CHECK(doc.Dirty());
	CHECK(doc.Version() > v0);
	// La fenêtre a tourné avec la tour : plus sur la face z = 0, mais sur un côté.
	CHECK(doc.Schematic().BlockAt({1, 1, 0}) != String("core:glass"));
	CHECK(doc.Schematic().BlockAt({2, 1, 1}) == String("core:glass") || doc.Schematic().BlockAt({0, 1, 1}) == String("core:glass"));
	REQUIRE(doc.Mirror(0).IsOk());
	CHECK(doc.Mirror(5).IsError());
	REQUIRE(doc.Hollow().IsOk());
	CHECK(doc.Schematic().BlockAt({1, 1, 1}).IsEmpty()); // l'intérieur est vidé
	CHECK(doc.Hollow(7).IsError());
	CHECK_EQ(doc.UndoDepth(), size_t(3));
	// Comptes : du plus fréquent au plus rare.
	const auto counts = doc.Counts();
	REQUIRE(counts.size() >= size_t(3));
	CHECK_EQ(counts.front().first, String("core:stone"));
	for (size_t i = 1; i < counts.size(); ++i)
		CHECK(counts[i - 1].second >= counts[i].second);
	// Remplacement : une étape seulement s'il y a quelque chose à remplacer.
	REQUIRE(doc.ReplaceBlock(String("core:planks"), String("core:bricks")).Value() > 0);
	CHECK_EQ(doc.UndoDepth(), size_t(4));
	CHECK_EQ(doc.ReplaceBlock(String("core:absent"), String("core:bricks")).Value(), int64_t(0));
	CHECK_EQ(doc.UndoDepth(), size_t(4));
	// Cellules et ancre bornées à la boîte.
	REQUIRE(doc.SetCell({0, 0, 0}, String()).IsOk());
	CHECK(doc.Schematic().BlockAt({0, 0, 0}).IsEmpty());
	CHECK(doc.SetCell({9, 0, 0}, String("core:stone")).IsError());
	REQUIRE(doc.SetAnchor({1, 0, 1}).IsOk());
	CHECK(doc.SetAnchor({0, -1, 0}).IsError());
	// Tout s'annule dans l'ordre, puis se rétablit.
	while (doc.Undo()) {
	}
	CHECK_EQ(doc.Schematic().BlockAt({1, 1, 0}), String("core:glass"));
	CHECK_EQ(doc.Schematic().BlockAt({1, 3, 1}), String("core:planks"));
	CHECK_EQ(doc.RedoDepth(), size_t(6));
	CHECK(doc.Redo());
	CHECK(doc.Schematic().BlockAt({1, 1, 0}) != String("core:glass")); // la rotation est revenue
	// 64 étapes au plus.
	for (int i = 0; i < 100; ++i)
		doc.Rotate(1);
	CHECK_EQ(doc.UndoDepth(), StructureDocument::MAX_UNDO);
	CHECK_EQ(doc.RedoDepth(), size_t(0));
}

TEST(StudioStructures_UndoKeepsAMemoryBudget) {
	// De grosses structures : les plus anciennes étapes partent bien avant 64,
	// mais la dernière reste toujours annulable.
	const BlockPos half{128, 64, 128}; // 1 M cellules : 4 tiennent dans le budget
	StructureDocument doc(voxel::Schematic(half), String("grande"));
	for (int i = 0; i < 10; ++i)
		doc.Replace(voxel::Schematic(half));
	CHECK_EQ(doc.UndoDepth(), size_t(StructureDocument::MAX_UNDO_CELLS / (128 * 64 * 128)));
	const BlockPos huge{256, 128, 256}; // seule, elle dépasse le budget
	doc.Replace(voxel::Schematic(huge));
	doc.Replace(voxel::Schematic(huge));
	CHECK_EQ(doc.UndoDepth(), size_t(1));
	CHECK(doc.Undo());
	CHECK_EQ(doc.Schematic().Size().x, 256);
}

TEST(StudioStructures_FormulasAreCheckedThenPlotted) {
	PlotSpec spec;
	spec.expression = "x^2 + y^2 + z^2 <= 4^2";
	spec.block = "core:stone";
	spec.min = {-5, -5, -5};
	spec.max = {5, 5, 5};
	CHECK(StructureDocument::CheckFormula(spec).IsOk());
	// L'erreur dit où.
	spec.expression = "x^2 + (y";
	Result<bool, String> bad = StructureDocument::CheckFormula(spec);
	REQUIRE(bad.IsError());
	CHECK(bad.Error().Contains("position"));
	spec.expression = "t < 3"; // pas une variable d'un tracé implicite
	CHECK(StructureDocument::CheckFormula(spec).Error().Contains("t"));
	// Paramétrique : l'expression fautive est nommée.
	spec.kind = PlotSpec::Kind::CURVE;
	spec.fx = "4 cos(t)";
	spec.fy = "4 sin(t";
	spec.fz = "t";
	CHECK(StructureDocument::CheckFormula(spec).Error().StartsWith("y :"));
	CHECK_EQ(StructureDocument::FormulaVariables(PlotSpec::Kind::SURFACE).size(), size_t(2));
	CHECK_EQ(StructureDocument::FormulaVariables(PlotSpec::Kind::SOLID).size(), size_t(3));

	// Tracés : une boule pleine (environ 4/3 π r³), puis une hélice.
	StructureDocument doc(voxel::Schematic(), String("formule"));
	spec.kind = PlotSpec::Kind::IMPLICIT;
	spec.expression = "x^2 + y^2 + z^2 <= 4^2";
	Result<int64_t, String> ball = doc.Plot(spec);
	REQUIRE(ball.IsOk());
	CHECK(ball.Value() > 200 && ball.Value() < 330);
	CHECK_EQ(doc.Schematic().Size().x, 9);
	CHECK_EQ(doc.UndoDepth(), size_t(1));
	spec.hollow = true;
	Result<int64_t, String> shell = doc.Plot(spec);
	REQUIRE(shell.IsOk());
	CHECK(shell.Value() < ball.Value()); // l'enveloppe seule
	spec.kind = PlotSpec::Kind::CURVE;
	spec.hollow = false;
	spec.fy = "4 sin(t)";
	spec.range[0][0] = 0.0;
	spec.range[0][1] = 6.2832;
	Result<int64_t, String> helix = doc.Plot(spec);
	REQUIRE(helix.IsOk());
	CHECK(helix.Value() > 10);
	CHECK(doc.Schematic().Size().z >= 6); // monte de 2π (z vers le haut : convention maths)
	// Sans bloc ou au-delà des limites : refus, le document ne change pas.
	const uint64_t before = doc.Version();
	spec.block = String();
	CHECK(doc.Plot(spec).IsError());
	spec.block = "core:stone";
	spec.kind = PlotSpec::Kind::IMPLICIT;
	spec.expression = "x^2 + y^2 + z^2 <= 50^2";
	spec.min = {-50, -50, -50};
	spec.max = {50, 50, 50};
	spec.maxSize = {32, 32, 32};
	CHECK(doc.Plot(spec).IsError());
	CHECK_EQ(doc.Version(), before);
}

TEST(StudioStructures_FilesInEveryFormatWithNameResolution) {
	voxel::Game game;
	(void)game.LoadMods(StructureOptions("build/tests/tmp/voxelia/studio_structures_files"));
	const std::filesystem::path dir("build/tests/tmp/voxelia/studio_structures_files/out");
	std::filesystem::remove_all(dir);
	StructureDocument doc(Tower(), String("tour"));
	for (const char *ext : {".vxs.json", ".json", ".we", ".mts"}) {
		const String path((dir / (std::string("tour") + ext)).string().c_str());
		Result<String, String> saved = doc.Save(path);
		REQUIRE(saved.IsOk());
		CHECK(!doc.Dirty());
		std::vector<String> unknown;
		Result<StructureDocument, String> back = StructureDocument::Load(path, game.Content(), &unknown);
		REQUIRE(back.IsOk());
		CHECK(unknown.empty());
		CHECK_EQ(back.Value().Name(), String("tour"));
		const voxel::Schematic &s = back.Value().Schematic();
		CHECK_EQ(s.Size().y, 4);
		CHECK_EQ(s.BlockAt({1, 1, 0}), String("core:glass"));
		CHECK_EQ(s.BlockAt({1, 3, 1}), String("core:planks"));
	}
	CHECK(doc.Save(String((dir / "tour.txt").string().c_str())).IsError()); // format inconnu
	CHECK(StructureDocument(voxel::Schematic(), String("vide")).Save(String((dir / "vide.vxs.json").string().c_str())).IsError());
	// Noms d'un autre jeu : « default:stone » → alias ; « mod:nom » → un mod chargé ; inconnu : vidé et signalé.
	{
		std::ofstream out(dir / "minetest.we");
		out << "5:return {{x=0, y=0, z=0, name=\"default:stone\"}, {x=1, y=0, z=0, name=\"moreblocks:glass\"}, "
			   "{x=2, y=0, z=0, name=\"nulle:part\"}}";
	}
	std::vector<String> unknown;
	Result<StructureDocument, String> imported =
		StructureDocument::Load(String((dir / "minetest.we").string().c_str()), game.Content(), &unknown);
	REQUIRE(imported.IsOk());
	CHECK_EQ(imported.Value().Schematic().BlockAt({1, 0, 0}), String("core:glass"));
	REQUIRE_EQ(unknown.size(), size_t(1));
	CHECK_EQ(unknown.front(), String("nulle:part"));
	CHECK(imported.Value().Schematic().BlockAt({2, 0, 0}).IsEmpty());
}

TEST(StudioStructures_CapturePasteAndRestoreInALiveWorld) {
	const char *saves = "build/tests/tmp/voxelia/studio_structures_world";
	std::filesystem::remove_all(saves);
	voxel::Game game;
	(void)game.LoadMods(StructureOptions(saves));
	REQUIRE(game.OpenWorld("structures", Some(uint32_t(11)), String("core:flat")).IsOk());
	(void)game.JoinPlayer("Testeur");
	for (int i = 0; i < 60; ++i)
		game.Tick(1.f / 60.f);
	// Ce que fait l'éditeur : capture d'une région construite, document, collage ailleurs, retour.
	REQUIRE(game.FillRegion({0, 20, 0}, {2, 23, 2}, String("core:stone"), String("hollow")).IsOk());
	StructureDocument doc(game.CaptureRegion({0, 20, 0}, {2, 23, 2}), String("boite"));
	CHECK_EQ(doc.Schematic().Size().x, 3);
	CHECK(doc.Schematic().BlockAt({1, 1, 1}) == voxel::AIR_ID); // évidée : de l'air capturé
	doc.Rotate(1);
	const BlockPos at{10, 20, 10};
	const auto [lo, hi] = doc.Schematic().Bounds(at, 0);
	const voxel::Schematic before = game.CaptureRegion(lo, hi);
	CHECK(game.PasteSchematic(doc.Schematic(), at) > 0);
	CHECK(game.GetWorld().BlockAt({10, 20, 10}).id == "core:stone");
	// Annulation du collage : la région d'avant revient.
	voxel::Schematic::PasteOptions restore;
	restore.keepAir = true;
	(void)game.PasteSchematic(before, lo, restore);
	CHECK(game.GetWorld().BlockAt({10, 20, 10}).id != "core:stone");
	game.CloseWorld();
}
