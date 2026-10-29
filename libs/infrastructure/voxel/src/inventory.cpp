// Définitions de voxel/inventory.hpp
#include <voxel/inventory.hpp>
#include <voxel/json.hpp>

#include <algorithm>
#include <map>

namespace voxel {

namespace {

int MaxStack(const ContentRegistry &content, const String &item) {
	const ItemDef *def = content.FindItem(item);
	return def ? std::max(1, def->maxStack) : 64;
}

} // namespace

int Inventory::Add(const ContentRegistry &content, const String &item, int count, NodePtr meta) {
	if (item.IsEmpty() || count <= 0)
		return 0;
	const int maxStack = MaxStack(content, item);
	// D'abord compléter les piles existantes (sans métadonnées), puis les vides.
	if (!meta)
		for (ItemStack &slot : m_slots)
			if (count > 0 && slot.item == item && !slot.meta && slot.count < maxStack) {
				const int moved = std::min(count, maxStack - slot.count);
				slot.count += moved;
				count -= moved;
			}
	for (ItemStack &slot : m_slots)
		if (count > 0 && slot.IsEmpty()) {
			const int moved = std::min(count, maxStack);
			slot = ItemStack{item, moved, 0, meta};
			count -= moved;
		}
	return count;
}

int Inventory::Count(const String &item) const {
	int total = 0;
	for (const ItemStack &slot : m_slots)
		if (slot.item == item)
			total += slot.count;
	return total;
}

bool Inventory::Remove(const String &item, int count) {
	if (Count(item) < count)
		return false;
	for (auto it = m_slots.rbegin(); it != m_slots.rend() && count > 0; ++it)
		if (it->item == item) {
			const int taken = std::min(count, it->count);
			it->count -= taken;
			count -= taken;
			if (it->count <= 0)
				*it = ItemStack{};
		}
	return true;
}

int Inventory::CountMatching(const ContentRegistry &content, const Ingredient &ingredient) const {
	int total = 0;
	for (const ItemStack &slot : m_slots)
		if (!slot.IsEmpty() && IngredientMatches(content, ingredient, slot.item))
			total += slot.count;
	return total;
}

bool Inventory::RemoveMatching(const ContentRegistry &content, const Ingredient &ingredient, int count) {
	if (CountMatching(content, ingredient) < count)
		return false;
	for (auto it = m_slots.rbegin(); it != m_slots.rend() && count > 0; ++it)
		if (!it->IsEmpty() && IngredientMatches(content, ingredient, it->item)) {
			const int taken = std::min(count, it->count);
			it->count -= taken;
			count -= taken;
			if (it->count <= 0)
				*it = ItemStack{};
		}
	return true;
}

void Inventory::Clear() {
	for (ItemStack &slot : m_slots)
		slot = ItemStack{};
}

bool Inventory::IsEmpty() const {
	return std::all_of(m_slots.begin(), m_slots.end(), [](const ItemStack &s) { return s.IsEmpty(); });
}

NodePtr Inventory::ToJson() const {
	NodePtr root = dataio::Node::MakeObject();
	root->Set("size", json::MakeInt(int64_t(m_slots.size())));
	NodePtr slots = dataio::Node::MakeArray();
	for (size_t i = 0; i < m_slots.size(); ++i) {
		const ItemStack &s = m_slots[i];
		if (s.IsEmpty())
			continue;
		NodePtr slot = dataio::Node::MakeObject();
		slot->Set("slot", json::MakeInt(int64_t(i)));
		slot->Set("item", json::MakeStr(s.item));
		slot->Set("count", json::MakeInt(s.count));
		if (s.wear)
			slot->Set("wear", json::MakeInt(s.wear));
		if (s.meta)
			slot->Set("meta", s.meta->Clone());
		slots->Push(slot);
	}
	root->Set("slots", slots);
	return root;
}

Inventory Inventory::FromJson(const NodePtr &root, int size) {
	Inventory inventory(json::Int(root, "size", size));
	if (NodePtr slots = json::At(root, "slots"); slots && slots->IsArray())
		for (size_t i = 0; i < slots->GetSize(); ++i) {
			NodePtr s = slots->At(i);
			const int index = json::Int(s, "slot", -1);
			if (index < 0 || index >= inventory.Size())
				continue;
			inventory.m_slots[size_t(index)] =
				ItemStack{json::Str(s, "item"), json::Int(s, "count", 1), json::Int(s, "wear", 0), json::At(s, "meta")};
		}
	return inventory;
}

bool IngredientMatches(const ContentRegistry &content, const Ingredient &ingredient, const String &item) {
	if (!ingredient.IsTag())
		return ingredient.item == item;
	const String tag = ingredient.item.Substring(1);
	if (content.ItemHasTag(item, tag))
		return true;
	// « #core:planks » accepte aussi le tag court « planks » des définitions.
	const size_t colon = tag.Find(":");
	return colon != String::NPOS && content.ItemHasTag(item, tag.Substring(colon + 1));
}

namespace {

/// Boîte englobante des cases occupées de la grille.
struct Bounds {
	int x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1;
	[[nodiscard]] bool Empty() const { return x1 < 0; }
};

Bounds GridBounds(const CraftGrid &grid) {
	Bounds b;
	for (int y = 0; y < grid.height; ++y)
		for (int x = 0; x < grid.width; ++x)
			if (!grid.At(x, y).IsEmpty()) {
				b.x0 = std::min(b.x0, x);
				b.y0 = std::min(b.y0, y);
				b.x1 = std::max(b.x1, x);
				b.y1 = std::max(b.y1, y);
			}
	return b;
}

bool MatchShaped(const ContentRegistry &content, const RecipeDef &r, const CraftGrid &grid, bool mirror) {
	const Bounds b = GridBounds(grid);
	if (b.Empty())
		return false;
	const int ph = int(r.pattern.size());
	int pw = 0;
	for (const String &row : r.pattern)
		pw = std::max(pw, int(row.GetSize()));
	// Motif rogné de ses colonnes/lignes vides.
	int px0 = pw, px1 = -1, py0 = ph, py1 = -1;
	for (int y = 0; y < ph; ++y)
		for (int x = 0; x < int(r.pattern[size_t(y)].GetSize()); ++x)
			if (r.pattern[size_t(y)].CharAt(size_t(x)) != ' ') {
				px0 = std::min(px0, x);
				px1 = std::max(px1, x);
				py0 = std::min(py0, y);
				py1 = std::max(py1, y);
			}
	if (px1 < 0 || px1 - px0 != b.x1 - b.x0 || py1 - py0 != b.y1 - b.y0)
		return false;
	for (int y = 0; y <= py1 - py0; ++y)
		for (int x = 0; x <= px1 - px0; ++x) {
			const int sx = mirror ? (px1 - x) : (px0 + x);
			const String &row = r.pattern[size_t(py0 + y)];
			const char symbol = sx < int(row.GetSize()) ? row.CharAt(size_t(sx)) : ' ';
			const String &cell = grid.At(b.x0 + x, b.y0 + y);
			if (symbol == ' ') {
				if (!cell.IsEmpty())
					return false;
				continue;
			}
			auto it = r.key.find(symbol);
			if (it == r.key.end() || cell.IsEmpty() || !IngredientMatches(content, it->second, cell))
				return false;
		}
	return true;
}

bool MatchShapeless(const ContentRegistry &content, const RecipeDef &r, const CraftGrid &grid) {
	std::vector<String> items;
	for (const String &cell : grid.cells)
		if (!cell.IsEmpty())
			items.push_back(cell);
	std::vector<Ingredient> needs;
	for (const Ingredient &ing : r.ingredients)
		for (int i = 0; i < ing.count; ++i)
			needs.push_back(Ingredient{ing.item, 1});
	if (items.size() != needs.size())
		return false;
	// Les ingrédients exacts d'abord, les tags ensuite (plus permissifs).
	std::stable_sort(needs.begin(), needs.end(), [](const Ingredient &a, const Ingredient &b) { return !a.IsTag() && b.IsTag(); });
	for (const Ingredient &need : needs) {
		auto it = std::find_if(items.begin(), items.end(), [&](const String &item) { return IngredientMatches(content, need, item); });
		if (it == items.end())
			return false;
		items.erase(it);
	}
	return true;
}

} // namespace

const RecipeDef *MatchRecipe(const ContentRegistry &content, const CraftGrid &grid) {
	for (const RecipeDef *r : content.Recipes()) {
		if (r->kind == RecipeKind::SHAPED && (MatchShaped(content, *r, grid, false) || MatchShaped(content, *r, grid, true)))
			return r;
		if (r->kind == RecipeKind::SHAPELESS && MatchShapeless(content, *r, grid))
			return r;
	}
	return nullptr;
}

std::vector<Ingredient> RecipeNeeds(const RecipeDef &recipe) {
	std::vector<Ingredient> needs;
	auto add = [&](const Ingredient &ing, int count) {
		for (Ingredient &n : needs)
			if (n.item == ing.item) {
				n.count += count;
				return;
			}
		needs.push_back(Ingredient{ing.item, count});
	};
	if (recipe.kind == RecipeKind::SHAPED) {
		for (const String &row : recipe.pattern)
			for (size_t i = 0; i < row.GetSize(); ++i)
				if (auto it = recipe.key.find(row.CharAt(i)); it != recipe.key.end())
					add(it->second, it->second.count);
	} else {
		for (const Ingredient &ing : recipe.ingredients)
			add(ing, ing.count);
	}
	return needs;
}

bool CanCraft(const ContentRegistry &content, const Inventory &inventory, const RecipeDef &recipe) {
	for (const Ingredient &need : RecipeNeeds(recipe))
		if (inventory.CountMatching(content, need) < need.count)
			return false;
	return true;
}

int CraftFromInventory(const ContentRegistry &content, Inventory &inventory, const RecipeDef &recipe, int times) {
	int made = 0;
	for (; made < times && CanCraft(content, inventory, recipe); ++made) {
		// Sur une copie : validée seulement si le résultat trouve sa place.
		Inventory trial = inventory;
		for (const Ingredient &need : RecipeNeeds(recipe))
			(void)trial.RemoveMatching(content, need, need.count);
		if (trial.Add(content, recipe.result, recipe.resultCount) > 0)
			break;
		inventory = std::move(trial);
	}
	return made;
}

std::vector<const RecipeDef *> CraftableRecipes(const ContentRegistry &content, const Inventory &inventory) {
	std::vector<const RecipeDef *> out;
	for (const RecipeDef *r : content.Recipes())
		if (r->kind != RecipeKind::COOKING && CanCraft(content, inventory, *r))
			out.push_back(r);
	return out;
}

} // namespace voxel
