// Définitions de ui/scene.hpp
#include <ui/scene.hpp>

namespace ui {

SceneManager::Scene &SceneManager::Add(String name, Factory factory) {
	Scene &s = m_scenes[name];
	if (s.root)
		s.root.Destroy();
	s = Scene{name, std::move(factory), Widget(), false};
	return s;
}

bool SceneManager::Has(StringView name) const { return m_scenes.find(name) != m_scenes.end(); }

SceneManager::Scene *SceneManager::Get(StringView name) {
	auto it = m_scenes.find(name);
	return it != m_scenes.end() ? &it->second : nullptr;
}

void SceneManager::Build(Scene &s) {
	if (s.root || !s.factory || !m_parent)
		return;
	// Construite cachée : Show la rend visible juste après.
	AnyWidget w = s.factory();
	s.root = m_parent.Append(w);
	s.root.Hide();
	s.visible = false;
}

Widget SceneManager::RootOf(StringView name) {
	Scene *s = Get(name);
	if (!s)
		return Widget();
	Build(*s);
	return s->root;
}

bool SceneManager::Show(StringView name) {
	Scene *s = Get(name);
	if (!s)
		return false;
	Build(*s);
	if (s->root)
		s->root.Show();
	s->visible = true;
	return true;
}

bool SceneManager::Hide(StringView name) {
	Scene *s = Get(name);
	if (!s)
		return false;
	if (s->root)
		s->root.Hide();
	s->visible = false;
	return true;
}

bool SceneManager::SwitchTo(StringView name) {
	if (!Has(name))
		return false;
	for (auto &[key, s] : m_scenes)
		if (s.visible && key != name)
			Hide(key.View());
	m_current = String(name);
	return Show(name);
}

bool SceneManager::Toggle(StringView name) {
	Scene *s = Get(name);
	if (!s)
		return false;
	return s->visible ? Hide(name) : Show(name);
}

bool SceneManager::Destroy(StringView name) {
	Scene *s = Get(name);
	if (!s)
		return false;
	if (s->root)
		s->root.Destroy();
	s->root = Widget();
	s->visible = false;
	return true;
}

bool SceneManager::Remove(StringView name) {
	if (!Destroy(name))
		return false;
	m_scenes.erase(m_scenes.find(name));
	if (m_current == name)
		m_current = String();
	return true;
}

std::vector<String> SceneManager::VisibleScenes() const {
	std::vector<String> out;
	for (const auto &[key, s] : m_scenes)
		if (s.visible)
			out.push_back(key);
	return out;
}

} // namespace ui
