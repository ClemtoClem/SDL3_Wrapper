// Tests — éditeur de blocs de Voxelia Studio sans fenêtre : document (source
// des mods, formulaire ⇄ JSON, annulation), validation à blanc, états
// fusionnés, enregistrement, et RECHARGEMENT À CHAUD dans une vraie partie
// (id numérique conservé, lumière et chunks invalidés).
#include "test.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxelia/studio/block_document.hpp>

using namespace voxelia::studio;
namespace json = voxel::json;

namespace {

voxel::GameOptions BlockOptions(const char *saves) {
	voxel::GameOptions o;
	o.modDirectories = {String("demos/voxelia/mods")};
	o.savesDirectory = String(saves);
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 200;
	return o;
}

bool HasDiagnostic(const std::vector<BlockDiagnostic> &list, const char *text) {
	for (const BlockDiagnostic &d : list)
		if (d.message.Contains(text))
			return true;
	return false;
}

} // namespace

TEST(StudioBlocks_DocumentFromModSource) {
	voxel::Game game;
	(void)game.LoadMods(BlockOptions("build/tests/tmp/voxelia/studio_blocks_doc"));
	// Ouvert depuis un ÉTAT : le document est le bloc entier, `states` compris.
	Option<BlockDocument> doc = BlockDocument::FromBlock(game.Content(), "backrooms:fluorescent_off");
	REQUIRE(doc.IsSome());
	CHECK(doc.Value().Id() == "backrooms:fluorescent");
	CHECK(doc.Value().Mod() == "backrooms");
	CHECK(doc.Value().SourceFile().EndsWith("world.json"));
	CHECK(doc.Value().Field("states.on.light") != nullptr);
	CHECK(doc.Value().FieldText("defaultState") == "on");
	CHECK(!doc.Value().Dirty());
	CHECK(doc.Value().Validate(game.Content()).empty());
	CHECK(BlockDocument::FromBlock(game.Content(), "core:inexistant").IsNone());
}

TEST(StudioBlocks_FormAndJsonShareTheDocument) {
	BlockDocument doc = BlockDocument::New("lanterne", "core", "demos/voxelia/mods/core", "build/tests/tmp/voxelia/x.json");
	CHECK(doc.Id() == "core:lanterne");
	// Formulaire → JSON.
	doc.SetField("light.range", json::MakeNum(9.0));
	CHECK(doc.Text().Contains("\"range\""));
	// JSON → formulaire.
	REQUIRE(doc.SetJson("{\"id\": \"lanterne\", \"light\": {\"range\": 12, \"color\": [255, 200, 120]}}").IsOk());
	CHECK(std::fabs(doc.FieldNumber("light.range") - 12.0) < 1e-9);
	// JSON invalide : refusé, document intact.
	CHECK(doc.SetJson("{ pas du json").IsError());
	CHECK(doc.SetJson("[1, 2]").IsError());
	CHECK(std::fabs(doc.FieldNumber("light.range") - 12.0) < 1e-9);
	// Retirer un champ.
	doc.SetField("light.color", nullptr);
	CHECK(doc.Field("light.color") == nullptr);
	CHECK(doc.Field("light.range") != nullptr);
}

TEST(StudioBlocks_UndoBySnapshots) {
	BlockDocument doc = BlockDocument::New("b", "core", "", "build/tests/tmp/voxelia/y.json");
	const size_t start = doc.UndoDepth();
	// Un glissé de curseur = UNE étape, quel que soit le nombre de valeurs.
	doc.BeginEdit();
	for (int i = 1; i <= 30; ++i)
		doc.SetField("light.range", json::MakeNum(double(i)));
	doc.EndEdit();
	CHECK_EQ(doc.UndoDepth(), start + 1);
	// Un geste sans effet : aucune étape.
	doc.BeginEdit();
	doc.SetField("light.range", json::MakeNum(30.0));
	doc.EndEdit();
	CHECK_EQ(doc.UndoDepth(), start + 1);
	CHECK(doc.Undo());
	CHECK(doc.Field("light") == nullptr);
	CHECK(doc.Redo());
	CHECK(std::fabs(doc.FieldNumber("light.range") - 30.0) < 1e-9);
	// 64 étapes au plus.
	for (int i = 0; i < 100; ++i)
		doc.SetField("gameplay.hardness", json::MakeNum(double(i)));
	CHECK_EQ(doc.UndoDepth(), BlockDocument::MAX_UNDO);
	CHECK_EQ(doc.RedoDepth(), size_t(0));
}

TEST(StudioBlocks_ValidateWithoutTouchingLiveContent) {
	voxel::Game game;
	(void)game.LoadMods(BlockOptions("build/tests/tmp/voxelia/studio_blocks_validate"));
	BlockDocument doc = BlockDocument::FromBlock(game.Content(), "backrooms:fluorescent").Unwrap();
	const float liveRange = game.Content().FindBlock("backrooms:fluorescent")->look.light.range;

	// > outerAngle, quelle que soit la valeur réglée dans le mod.
	const double outer = doc.FieldNumber("states.on.light.outerAngle");
	REQUIRE(outer > 0.0);
	doc.SetField("states.on.light.innerAngle", json::MakeNum(std::min(outer + 10.0, 179.0)));
	doc.SetField("states.on.light.outerAngle", json::MakeNum(std::min(outer, 169.0)));
	doc.SetField("states.on.light.color", json::MakeStr(String("pas une couleur")));
	doc.SetField("states.on.interact.toggle", json::MakeStr(String("backrooms:nulle_part")));
	doc.SetField("states.on.light.range", json::MakeNum(3.0));
	const std::vector<BlockDiagnostic> diags = doc.Validate(game.Content());
	CHECK(HasDiagnostic(diags, "innerAngle > outerAngle"));
	CHECK(HasDiagnostic(diags, "light.color"));
	CHECK(HasDiagnostic(diags, "nulle_part"));
	bool fieldFound = false;
	for (const BlockDiagnostic &d : diags)
		fieldFound = fieldFound || d.field == "light.innerAngle";
	CHECK(fieldFound);
	// Le contenu vivant n'a pas bougé.
	CHECK(std::fabs(game.Content().FindBlock("backrooms:fluorescent")->look.light.range - liveRange) < 1e-6f);

	// États par défaut inconnu : erreur de chargement.
	doc.SetField("defaultState", json::MakeStr(String("fantome")));
	const std::vector<BlockDiagnostic> broken = doc.Validate(game.Content());
	REQUIRE(!broken.empty());
	CHECK(broken.front().severity == BlockDiagnostic::Severity::Error);
	CHECK(broken.front().message.Contains("fantome"));
}

TEST(StudioBlocks_MergedStatesShowDifferences) {
	voxel::Game game;
	(void)game.LoadMods(BlockOptions("build/tests/tmp/voxelia/studio_blocks_states"));
	BlockDocument doc = BlockDocument::FromBlock(game.Content(), "backrooms:fluorescent").Unwrap();
	const std::vector<MergedState> states = doc.MergedStates(game.Content());
	REQUIRE(states.size() >= 2);
	const MergedState *on = nullptr, *off = nullptr;
	for (const MergedState &s : states) {
		if (s.name == "on")
			on = &s;
		if (s.name == "off")
			off = &s;
	}
	REQUIRE(on != nullptr);
	REQUIRE(off != nullptr);
	CHECK(on->isDefault);
	CHECK(on->id == "backrooms:fluorescent");
	CHECK(off->id == "backrooms:fluorescent_off");
	CHECK(on->differences.empty());
	auto differs = [&](const char *path) {
		return std::find(off->differences.begin(), off->differences.end(), String(path)) != off->differences.end();
	};
	CHECK(differs("light.enabled"));
	CHECK(differs("path"));
	CHECK(off->json.Contains("fluorescent_off.obj"));
}

TEST(StudioBlocks_SaveReplacesTheDefinitionInItsFile) {
	const std::filesystem::path dir("build/tests/tmp/voxelia/studio_blocks_save");
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	const String file((dir / "blocks.json").string().c_str());
	{
		std::ofstream out(file.CStr());
		out << "// En-tête du fichier : gardé.\n[\n    // Bloc a : intouché.\n    {\"id\": \"a\", \"displayName\": \"A\", /* note */ \"x\": 0.06},\n"
			   "    // Bloc b.\n    {\"id\": \"b\", \"displayName\": \"B\"}\n]\n";
	}
	BlockDocument doc = BlockDocument::New("b", "test", dir.string().c_str(), file);
	doc.SetField("displayName", json::MakeStr(String("B modifié")));
	REQUIRE(doc.Save().IsOk());
	CHECK(!doc.Dirty());
	BlockDocument added = BlockDocument::New("c", "test", dir.string().c_str(), file);
	REQUIRE(added.Save().IsOk());
	std::ifstream in(file.CStr());
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	CHECK(text.rfind("// En-tête du fichier : gardé.", 0) == 0);
	// Le reste du fichier est gardé tel quel : commentaires, autres définitions.
	CHECK(text.find("    // Bloc a : intouché.\n    {\"id\": \"a\", \"displayName\": \"A\", /* note */ \"x\": 0.06},") !=
		  std::string::npos);
	CHECK(text.find("    // Bloc b.\n    {") != std::string::npos);
	CHECK(text.find("B modifié") != std::string::npos);
	auto parsed = json::ReadFile(file);
	REQUIRE(parsed.IsOk());
	REQUIRE(parsed.Value()->IsArray());
	REQUIRE(parsed.Value()->GetSize() == size_t(3));
	CHECK(json::Str(parsed.Value()->At(0), "displayName") == "A");
	CHECK(json::Str(parsed.Value()->At(1), "displayName") == "B modifié");
	CHECK(json::Str(parsed.Value()->At(2), "id") == "c");
}

TEST(StudioBlocks_HotReloadInALiveWorld) {
	const char *saves = "build/tests/tmp/voxelia/studio_blocks_reload";
	std::filesystem::remove_all(saves);
	voxel::Game game;
	(void)game.LoadMods(BlockOptions(saves));
	REQUIRE(game.OpenWorld("reload", Some(uint32_t(7))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Testeur");
	for (int i = 0; i < 60; ++i)
		game.Tick(1.f / 60.f);
	const math::FVector3 feet = game.Registry().GetComponent<voxel::Transform>(player).Unwrap()->position;
	const voxel::BlockPos at = voxel::BlockPos::FromWorld(feet) + voxel::BlockPos{2, 2, 0};
	voxel::World &world = game.GetWorld();
	const voxel::BlockId numeric = game.Content().BlockIdOf("backrooms:fluorescent");
	REQUIRE(numeric != voxel::AIR);
	REQUIRE(world.SetBlock(at, String("backrooms:fluorescent"), 0, voxel::ChangeCause::SCRIPT));
	const voxel::Chunk *chunk = world.GetChunk(voxel::ChunkOf(at));
	REQUIRE(chunk != nullptr);
	const uint64_t revision = chunk->Revision();
	const size_t lightsBefore = chunk->LightCells(game.Content()).size();
	CHECK(lightsBefore >= 1);

	// Éteint dans TOUS les états, portée réduite : rechargé à chaud.
	BlockDocument doc = BlockDocument::FromBlock(game.Content(), "backrooms:fluorescent").Unwrap();
	doc.SetField("states.on.light.enabled", json::MakeBool(false));
	auto reloaded = game.ReloadDefinition(doc.Json(), doc.Context());
	REQUIRE(reloaded.IsOk());
	CHECK(std::find(reloaded.Value().begin(), reloaded.Value().end(), String("backrooms:fluorescent_off")) !=
		  reloaded.Value().end());
	// Même id numérique : le bloc posé est toujours le néon, sans rien régénérer.
	CHECK_EQ(game.Content().BlockIdOf("backrooms:fluorescent"), numeric);
	CHECK_EQ(world.GetBlock(at), numeric);
	CHECK(!game.Content().Block(numeric).look.light.Active());
	// Chunk à remailler, sources de lumière relues.
	CHECK(chunk->Revision() > revision);
	CHECK_EQ(chunk->LightCells(game.Content()).size(), lightsBefore - 1);
	// La partie continue.
	for (int i = 0; i < 30; ++i)
		game.Tick(1.f / 60.f);
	// Recharger la définition d'origine rallume.
	BlockDocument original = BlockDocument::FromBlock(game.Content(), "backrooms:fluorescent").Unwrap();
	original.SetField("states.on.light.enabled", nullptr);
	REQUIRE(game.ReloadDefinition(original.Json(), original.Context()).IsOk());
	CHECK(game.Content().Block(numeric).look.light.Active());
	CHECK_EQ(chunk->LightCells(game.Content()).size(), lightsBefore);
	// Une définition invalide est refusée sans rien casser.
	original.SetField("defaultState", json::MakeStr(String("fantome")));
	CHECK(game.ReloadDefinition(original.Json(), original.Context()).IsError());
	CHECK(game.Content().Block(numeric).look.light.Active());
}

TEST(StudioBlocks_NodeboxShapes) {
	voxel::Game game;
	(void)game.LoadMods(BlockOptions("build/tests/tmp/voxelia/studio_blocks_nodebox"));
	BlockDocument doc = BlockDocument::New("table", "core", "demos/voxelia/mods/core", "build/tests/tmp/voxelia/nb.json");
	CHECK(doc.NodeBoxes().empty());
	// Plateau + un pied ; miroirs pour les trois autres pieds.
	REQUIRE(doc.AddNodeBox({0.f, 14.f, 0.f, 16.f, 16.f, 16.f}).IsOk());
	REQUIRE(doc.AddNodeBox({1.f, 0.f, 1.f, 3.f, 14.f, 3.f}).IsOk());
	CHECK(doc.FieldText("type") == "nodebox");
	REQUIRE(doc.MirrorNodeBox(1, 0).IsOk()); // x
	REQUIRE(doc.MirrorNodeBox(1, 2).IsOk()); // z
	REQUIRE(doc.MirrorNodeBox(3, 0).IsOk());
	std::vector<BlockDocument::NodeBox> boxes = doc.NodeBoxes();
	REQUIRE(boxes.size() == size_t(5));
	CHECK(std::fabs(boxes[2][0] - 13.f) < 1e-4f && std::fabs(boxes[2][3] - 15.f) < 1e-4f);
	// JSON : convention du moteur (−0,5 … 0,5).
	CHECK(std::fabs(doc.FieldNumber("nodebox.0.1") - (14.0 / 16.0 - 0.5)) < 1e-6);
	// Refus : boîte plate, hors bloc, indice inconnu, dernière boîte.
	CHECK(doc.AddNodeBox({0.f, 0.f, 0.f, 0.f, 4.f, 4.f}).IsError());
	CHECK(doc.AddNodeBox({0.f, 0.f, 0.f, 40.f, 4.f, 4.f}).IsError());
	CHECK(doc.SetNodeBox(9, boxes[0]).IsError());
	// Quart de tour autour de y : (x, z) → (16 − z, x).
	REQUIRE(doc.SetNodeBox(1, {1.f, 0.f, 5.f, 3.f, 14.f, 7.f}).IsOk());
	REQUIRE(doc.RotateNodeBoxes().IsOk());
	boxes = doc.NodeBoxes();
	CHECK(std::fabs(boxes[1][0] - 9.f) < 1e-4f && std::fabs(boxes[1][3] - 11.f) < 1e-4f);
	CHECK(std::fabs(boxes[1][2] - 1.f) < 1e-4f && std::fabs(boxes[1][5] - 3.f) < 1e-4f);
	// Le moteur charge la forme telle qu'éditée.
	Option<voxel::BlockDef> loaded = doc.Loaded(game.Content());
	REQUIRE(loaded.IsSome());
	CHECK(loaded.Value().shape.kind == voxel::ShapeKind::NODEBOX);
	CHECK_EQ(loaded.Value().shape.boxes.size(), size_t(5));
	CHECK(std::fabs(loaded.Value().shape.boxes[0].min.y - 14.f / 16.f) < 1e-5f);
	CHECK(doc.Validate(game.Content()).empty());
	REQUIRE(doc.RemoveNodeBox(0).IsOk());
	CHECK_EQ(doc.NodeBoxes().size(), size_t(4));
}

TEST(StudioBlocks_NodeboxPerState) {
	voxel::Game game;
	(void)game.LoadMods(BlockOptions("build/tests/tmp/voxelia/studio_blocks_nodebox_states"));
	BlockDocument doc = BlockDocument::New("trappe", "core", "demos/voxelia/mods/core", "build/tests/tmp/voxelia/nb2.json");
	REQUIRE(doc.AddNodeBox({0.f, 0.f, 0.f, 16.f, 3.f, 16.f}).IsOk()); // fermée : à plat
	REQUIRE(doc.SetJson(doc.Text().Replace("\"type\"", "\"defaultState\": \"closed\", \"states\": {\"closed\": {}, \"open\": {}}, \"type\"")).IsOk());
	// L'état « open » hérite des boîtes, puis a les siennes.
	CHECK_EQ(doc.NodeBoxes("open").size(), size_t(1));
	REQUIRE(doc.SetNodeBoxes({{0.f, 0.f, 13.f, 16.f, 16.f, 16.f}}, String("open")).IsOk());
	CHECK(std::fabs(doc.NodeBoxes("open")[0][2] - 13.f) < 1e-4f);
	CHECK(std::fabs(doc.NodeBoxes()[0][4] - 3.f) < 1e-4f); // le bloc commun n'a pas bougé
	CHECK(doc.SetNodeBoxes({{0.f, 0.f, 0.f, 1.f, 1.f, 1.f}}, String("absent")).IsError());
	Option<voxel::BlockDef> open = doc.Loaded(game.Content(), String("open"));
	REQUIRE(open.IsSome());
	REQUIRE(open.Value().shape.boxes.size() == size_t(1));
	CHECK(std::fabs(open.Value().shape.boxes[0].min.z - 13.f / 16.f) < 1e-5f);
}
