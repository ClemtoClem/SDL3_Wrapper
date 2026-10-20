#pragma once
/**
 * ui::scene — système de scènes retenues (retained-mode).
 *
 * Une Scene est un ensemble nommé de racines UI construites paresseusement
 * (le builder n'est appelé qu'au premier show()). Cacher/montrer une scène ne
 * détruit RIEN : les entités restent en mémoire avec leur état (valeurs de
 * sliders, texte saisi, position de scroll...), seule la visibilité change —
 * c'est ce qui rend la navigation entre écrans quasi gratuite, à la GTK4
 * (GtkStack), au lieu de reconstruire l'arbre à chaque changement d'écran.
 *
 * @code{.cpp}
 * ui::SceneManager scenes(factory);
 *
 * scenes.Add("menu", [](ui::UiFactory& f) {
 *     return std::vector{ f.Panel().Children(...).Spawn() };
 * });
 * scenes.Add("options", [](ui::UiFactory& f) { ... });
 *
 * scenes.SwitchTo("menu");     // construit "menu" (1re fois), l'affiche
 * scenes.SwitchTo("options");  // cache "menu" (état conservé), affiche "options"
 * scenes.SwitchTo("menu");     // ré-affichage instantané, aucun rebuild
 * @endcode
 */
#include <unordered_map>

#include "factory.hpp"

namespace ui {

// ============================================================================
// Scene
// ============================================================================

class Scene {
public:
	using Builder = std::function<std::vector<ecs::Entity>(UiFactory &)>;

	Scene() = default;
	Scene(String name, Builder builder) : name(std::move(name)), builder(std::move(builder)) {}

	[[nodiscard]] const String &SceneName() const noexcept { return name; }
	[[nodiscard]] bool Built() const noexcept { return built; }
	[[nodiscard]] bool Visible() const noexcept { return visible; }
	[[nodiscard]] const std::vector<ecs::Entity> &Roots() const noexcept { return roots; }

private:
	friend class SceneManager;

	String name;
	Builder builder;
	std::vector<ecs::Entity> roots;
	bool built = false;
	bool visible = false;
};

// ============================================================================
// SceneManager
// ============================================================================

class SceneManager {
public:
	explicit SceneManager(UiFactory &factory) : factory(&factory) {}

	/// Enregistre une scène (construction différée au premier show()).
	Scene &Add(String name, Scene::Builder builder);

	[[nodiscard]] bool Has(const String &name) const { return scenes.contains(String(name.c_str())); }

	[[nodiscard]] Scene *Get(const String &name);

	/// Affiche la scène (la construit au premier appel). Retourne false si inconnue.
	bool Show(const String &name);

	/// Cache la scène sans rien détruire (l'état des widgets est conservé).
	bool Hide(const String &name);

	/// Cache toutes les scènes visibles puis affiche `name`.
	bool SwitchTo(const String &name);

	bool Toggle(const String &name);

	/// Détruit les entités de la scène (rebuild au prochain show()).
	bool Destroy(const String &name);

	/// Retire la scène du gestionnaire (après destruction de ses entités).
	bool Remove(const String &name);

	[[nodiscard]] std::vector<String> VisibleScenes() const;

	[[nodiscard]] UiFactory &Factory() noexcept { return *factory; }

private:
	UiFactory *factory;
	std::unordered_map<String, Scene> scenes;

	void BuildIfNeeded(Scene &s);

	static void ZeroComputed(ecs::ArchetypeRegistry &w, ecs::Entity e);
};

} // namespace ui
