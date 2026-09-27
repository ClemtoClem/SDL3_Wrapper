// Définitions de ui/scene.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "ui/scene.hpp"

namespace ui {

// ── SceneManager ─────────────────────────────────────────────────────────────

Scene & SceneManager::Add(String name, Scene::Builder builder) {
	auto key = String(name.c_str());
	scenes[key] = Scene(std::move(name), std::move(builder));
	return scenes[key];
}

Scene * SceneManager::Get(const String &name) {
	auto it = scenes.find(String(name.c_str()));
	return it != scenes.end() ? &it->second : nullptr;
}

bool SceneManager::Show(const String &name) {
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

bool SceneManager::Hide(const String &name) {
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

bool SceneManager::SwitchTo(const String &name) {
	if (!Has(name))
		return false;
	for (auto &[key, s] : scenes)
		if (s.visible && s.name != name)
			Hide(s.name);
	return Show(name);
}

bool SceneManager::Toggle(const String &name) {
	Scene *s = Get(name);
	if (!s)
		return false;
	return s->visible ? Hide(name) : Show(name);
}

bool SceneManager::Destroy(const String &name) {
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

bool SceneManager::Remove(const String &name) {
	if (!Has(name))
		return false;
	Destroy(name);
	scenes.erase(String(name.c_str()));
	return true;
}

std::vector<String> SceneManager::VisibleScenes() const {
	std::vector<String> v;
	for (auto &[key, s] : scenes)
		if (s.visible)
			v.push_back(s.name);
	return v;
}

void SceneManager::BuildIfNeeded(Scene &s) {
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

void SceneManager::ZeroComputed(ecs::ArchetypeRegistry &w, ecs::Entity e) {
	if (auto c = w.GetComponent<UiComputed>(e); c.IsSome())
		*c.Unwrap() = UiComputed{};
	if (auto children = w.GetComponent<UiChildren>(e); children.IsSome()) {
		std::vector<ecs::Entity> kids = children.Unwrap()->list;
		for (ecs::Entity k : kids)
			ZeroComputed(w, k);
	}
}

} // namespace ui
