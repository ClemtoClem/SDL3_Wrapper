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
	Scene &Add(String name, Scene::Builder builder) {
		auto key = String(name.c_str());
		scenes[key] = Scene(std::move(name), std::move(builder));
		return scenes[key];
	}

	[[nodiscard]] bool Has(const String &name) const { return scenes.contains(String(name.c_str())); }

	[[nodiscard]] Scene *Get(const String &name) {
		auto it = scenes.find(String(name.c_str()));
		return it != scenes.end() ? &it->second : nullptr;
	}

	/// Affiche la scène (la construit au premier appel). Retourne false si inconnue.
	bool Show(const String &name) {
		Scene *s = Get(name);
		if (!s)
			return false;
		BuildIfNeeded(*s);
		if (s->visible)
			return true;
		s->visible = true;
		ecs::ArchetypeRegistry &w = factory->World();
		for (ecs::Entity root : s->roots)
			if (w.IsAlive(root))
				w.RemoveComponent<UiHidden>(root);
		factory->Layout().MarkDirty();
		return true;
	}

	/// Cache la scène sans rien détruire (l'état des widgets est conservé).
	bool Hide(const String &name) {
		Scene *s = Get(name);
		if (!s || !s->visible)
			return s != nullptr;
		s->visible = false;
		ecs::ArchetypeRegistry &w = factory->World();
		for (ecs::Entity root : s->roots) {
			if (!w.IsAlive(root))
				continue;
			w.AddComponent(root, UiHidden{});
			// Neutralise les rects calculés du sous-arbre : l'InputSystem ne
			// doit plus toucher les widgets d'une scène cachée (leurs anciens
			// UiComputed resteraient sinon cliquables jusqu'à la prochaine passe).
			ZeroComputed(w, root);
		}
		factory->Layout().MarkDirty();
		return true;
	}

	/// Cache toutes les scènes visibles puis affiche `name`.
	bool SwitchTo(const String &name) {
		if (!Has(name))
			return false;
		for (auto &[key, s] : scenes)
			if (s.visible && s.name != name)
				Hide(s.name);
		return Show(name);
	}

	bool Toggle(const String &name) {
		Scene *s = Get(name);
		if (!s)
			return false;
		return s->visible ? Hide(name) : Show(name);
	}

	/// Détruit les entités de la scène (rebuild au prochain show()).
	bool Destroy(const String &name) {
		Scene *s = Get(name);
		if (!s)
			return false;
		ecs::ArchetypeRegistry &w = factory->World();
		for (ecs::Entity root : s->roots)
			if (w.IsAlive(root))
				DespawnTree(w, root);
		s->roots.clear();
		s->built = false;
		s->visible = false;
		factory->Layout().MarkDirty();
		return true;
	}

	/// Retire la scène du gestionnaire (après destruction de ses entités).
	bool Remove(const String &name) {
		if (!Has(name))
			return false;
		Destroy(name);
		scenes.erase(String(name.c_str()));
		return true;
	}

	[[nodiscard]] std::vector<String> VisibleScenes() const {
		std::vector<String> v;
		for (auto &[key, s] : scenes)
			if (s.visible)
				v.push_back(s.name);
		return v;
	}

	[[nodiscard]] UiFactory &Factory() noexcept { return *factory; }

private:
	UiFactory *factory;
	std::unordered_map<String, Scene> scenes;

	void BuildIfNeeded(Scene &s) {
		if (s.built)
			return;
		s.roots = s.builder ? s.builder(*factory) : std::vector<ecs::Entity>{};
		s.built = true;
		// Les scènes naissent cachées ; show() rend visible juste après.
		ecs::ArchetypeRegistry &w = factory->World();
		for (ecs::Entity root : s.roots)
			if (w.IsAlive(root) && !w.HasComponent<UiHidden>(root))
				w.AddComponent(root, UiHidden{});
	}

	static void ZeroComputed(ecs::ArchetypeRegistry &w, ecs::Entity e) {
		if (auto c = w.GetComponent<UiComputed>(e); c.IsSome())
			*c.Unwrap() = UiComputed{};
		if (auto children = w.GetComponent<UiChildren>(e); children.IsSome()) {
			std::vector<ecs::Entity> kids = children.Unwrap()->list;
			for (ecs::Entity k : kids)
				ZeroComputed(w, k);
		}
	}
};

} // namespace ui
