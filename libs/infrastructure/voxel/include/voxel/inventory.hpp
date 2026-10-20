#pragma once
/**
 * Inventaires et fabrication.
 *
 *  - Inventory : emplacements d'objets empilés (taille de pile de chaque
 *    ItemDef), ajout/retrait, sérialisation JSON (sauvegardes).
 *  - Recettes : grille à forme (SHAPED, motif retrouvé n'importe où dans la
 *    grille, ou en miroir), sans forme (SHAPELESS), cuisson (COOKING) ; un
 *    ingrédient est un objet ou un TAG (« #planks »).
 *  - Fabrication directe depuis un inventaire (ce que fait la console et
 *    l'interface simple du jeu) : ingrédients comptés, consommés, résultat
 *    ajouté.
 */
#include <vector>

#include "components.hpp"
#include "content.hpp"

namespace voxel {

class Inventory {
public:
	explicit Inventory(int size = 36) : m_slots(size_t(std::max(1, size))) {}

	[[nodiscard]] int Size() const noexcept { return int(m_slots.size()); }
	[[nodiscard]] ItemStack &Slot(int i) { return m_slots[size_t(i)]; }
	[[nodiscard]] const ItemStack &Slot(int i) const { return m_slots[size_t(i)]; }

	/// Ajoute ; rend ce qui n'a pas trouvé de place.
	int Add(const ContentRegistry &content, const String &item, int count, NodePtr meta = nullptr);
	/// Retire `count` objets (faux, et rien de retiré, s'il en manque).
	bool Remove(const String &item, int count);
	[[nodiscard]] int Count(const String &item) const;
	/// Objets correspondant à un ingrédient (objet ou #tag).
	[[nodiscard]] int CountMatching(const ContentRegistry &content, const Ingredient &ingredient) const;
	bool RemoveMatching(const ContentRegistry &content, const Ingredient &ingredient, int count);
	void Clear();
	[[nodiscard]] bool IsEmpty() const;

	[[nodiscard]] NodePtr ToJson() const;
	[[nodiscard]] static Inventory FromJson(const NodePtr &json, int size = 36);

private:
	std::vector<ItemStack> m_slots;
};

/// Composant : inventaire d'une entité (joueur, coffre, créature).
struct InventoryComponent {
	Inventory inventory{36};
};

[[nodiscard]] bool IngredientMatches(const ContentRegistry &content, const Ingredient &ingredient, const String &item);

/// Grille de fabrication (largeur × hauteur, lignes de haut en bas).
struct CraftGrid {
	int width = 3, height = 3;
	std::vector<String> cells; ///< id d'objet ou vide
	[[nodiscard]] const String &At(int x, int y) const { return cells[size_t(y * width + x)]; }
};

/// Recette satisfaite par la grille (NONE : aucune).
[[nodiscard]] const RecipeDef *MatchRecipe(const ContentRegistry &content, const CraftGrid &grid);

/// Ingrédients nécessaires (somme par ingrédient) d'une recette.
[[nodiscard]] std::vector<Ingredient> RecipeNeeds(const RecipeDef &recipe);
/// La recette peut-elle être faite avec l'inventaire ?
[[nodiscard]] bool CanCraft(const ContentRegistry &content, const Inventory &inventory, const RecipeDef &recipe);
/// Fabrique `times` fois (s'arrête dès qu'il manque) ; rend le nombre de fabrications.
int CraftFromInventory(const ContentRegistry &content, Inventory &inventory, const RecipeDef &recipe, int times = 1);
/// Recettes réalisables avec l'inventaire.
[[nodiscard]] std::vector<const RecipeDef *> CraftableRecipes(const ContentRegistry &content, const Inventory &inventory);

} // namespace voxel
