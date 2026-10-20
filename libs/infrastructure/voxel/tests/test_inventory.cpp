// Tests — inventaires et recettes (voxel/inventory.hpp).
#include "test.hpp"

#include <voxel/inventory.hpp>
#include <voxel/json.hpp>

using namespace voxel;

namespace {

void Load(ContentRegistry &content, const char *text) {
	auto parsed = json::Parse(String(text));
	if (parsed.IsError() || content.Load(parsed.Unwrap(), {"t", "", "test"}).IsError())
		std::printf("définition refusée : %s\n", text);
}

void MakeContent(ContentRegistry &c) {
	Load(c, R"({"id": "oak_log", "tags": ["logs"]})");
	Load(c, R"({"id": "birch_log", "tags": ["logs"]})");
	Load(c, R"({"id": "planks", "tags": ["planks"]})");
	Load(c, R"({"type": "item", "id": "stick", "tags": ["sticks"]})");
	Load(c, R"({"id": "cobble"})");
	Load(c, R"({"type": "tool", "id": "stone_pick", "tool": {"kind": "pickaxe", "speed": 4}})");
	Load(c, R"({"type": "recipe", "id": "planks", "ingredients": [{"tag": "logs"}], "result": {"item": "planks", "count": 4}})");
	Load(c, R"({"type": "recipe", "id": "sticks", "pattern": ["P", "P"], "key": {"P": "#planks"}, "result": {"item": "stick", "count": 4}})");
	Load(c, R"({"type": "recipe", "id": "stone_pick", "pattern": ["CCC", " S ", " S "], "key": {"C": "cobble", "S": "stick"}, "result": "stone_pick"})");
	Load(c, R"({"type": "recipe", "id": "hook", "pattern": ["C ", "CS"], "key": {"C": "cobble", "S": "stick"}, "result": "stick"})");
}

CraftGrid Grid(std::initializer_list<const char *> cells) {
	CraftGrid g;
	for (const char *c : cells)
		g.cells.push_back(String(c));
	return g;
}

} // namespace

TEST(Inventory_StacksRemovesAndSerializes) {
	ContentRegistry c;
	MakeContent(c);
	Inventory inv(4);
	CHECK_EQ(inv.Add(c, "t:cobble", 100), 0); // 64 + 36
	CHECK_EQ(inv.Count("t:cobble"), 100);
	CHECK_EQ(inv.Add(c, "t:stone_pick", 3), 1); // outils : pile de 1, 2 places libres
	CHECK(!inv.Remove("t:cobble", 101));
	CHECK(inv.Remove("t:cobble", 70));
	CHECK_EQ(inv.Count("t:cobble"), 30);
	const Inventory back = Inventory::FromJson(inv.ToJson());
	CHECK_EQ(back.Size(), 4);
	CHECK_EQ(back.Count("t:cobble"), 30);
	CHECK_EQ(back.Count("t:stone_pick"), 2);
}

TEST(Recipes_ShapedMirroredShapelessAndTags) {
	ContentRegistry c;
	MakeContent(c);
	// Sans forme, par tag : n'importe quel tronc donne des planches.
	CHECK(MatchRecipe(c, Grid({"", "t:birch_log", "", "", "", "", "", "", ""}))->id == "t:planks");
	// À forme, n'importe où dans la grille.
	CHECK(MatchRecipe(c, Grid({"", "", "t:planks", "", "", "t:planks", "", "", ""}))->id == "t:sticks");
	CHECK(MatchRecipe(c, Grid({"t:cobble", "t:cobble", "t:cobble", "", "t:stick", "", "", "t:stick", ""}))->id == "t:stone_pick");
	// Motif asymétrique : en miroir aussi.
	CHECK(MatchRecipe(c, Grid({"t:cobble", "", "", "t:cobble", "t:stick", "", "", "", ""}))->id == "t:hook");
	CHECK(MatchRecipe(c, Grid({"", "t:cobble", "", "t:stick", "t:cobble", "", "", "", ""}))->id == "t:hook");
	CHECK(MatchRecipe(c, Grid({"t:cobble", "", "", "", "t:stick", "", "", "", ""})) == nullptr);
}

TEST(Recipes_CraftingFromAnInventory) {
	ContentRegistry c;
	MakeContent(c);
	Inventory inv(9);
	(void)inv.Add(c, "t:oak_log", 1);
	(void)inv.Add(c, "t:cobble", 3);
	const RecipeDef *planks = c.FindRecipe("t:planks");
	const RecipeDef *sticks = c.FindRecipe("t:sticks");
	const RecipeDef *pick = c.FindRecipe("t:stone_pick");
	CHECK(!CanCraft(c, inv, *pick));
	CHECK_EQ(CraftFromInventory(c, inv, *planks, 5), 1); // un seul tronc
	CHECK_EQ(inv.Count("t:planks"), 4);
	CHECK_EQ(CraftFromInventory(c, inv, *sticks), 1);
	CHECK_EQ(inv.Count("t:stick"), 4);
	CHECK(CanCraft(c, inv, *pick));
	CHECK_EQ(CraftableRecipes(c, inv).size(), size_t(3)); // bâtons, pioche, crochet
	CHECK_EQ(CraftFromInventory(c, inv, *pick), 1);
	CHECK_EQ(inv.Count("t:stone_pick"), 1);
	CHECK_EQ(inv.Count("t:cobble"), 0);
}
