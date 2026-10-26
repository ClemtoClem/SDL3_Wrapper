#pragma once
/**
 * ui::SceneManager — écrans retenus, construits paresseusement.
 *
 * Une scène est un builder matérialisé au premier `Show` ; la cacher ne
 * détruit rien (valeurs, texte saisi, défilement conservés) : naviguer entre
 * écrans ne coûte qu'une bascule de visibilité.
 *
 * @code{.cpp}
 * ui::SceneManager scenes(gui.Root());
 * scenes.Add("menu", [] { return ui::Column().Fill().Children({...}); });
 * scenes.Add("options", [] { return ui::Panel().Children({...}); });
 * scenes.SwitchTo("menu");      // construit, affiche
 * scenes.SwitchTo("options");   // cache "menu" (état conservé)
 * @endcode
 */
#include <functional>
#include <map>
#include <vector>

#include "builder.hpp"
#include "widget.hpp"

namespace ui {

class SceneManager {
public:
	using Factory = std::function<AnyWidget()>;

	struct Scene {
		String name;
		Factory factory;
		Widget root;
		bool visible = false;
		[[nodiscard]] bool Built() const noexcept { return root.Valid(); }
	};

	/// Les scènes sont construites sous `parent` (la racine en général).
	explicit SceneManager(Widget parent) : m_parent(parent) {}

	Scene &Add(String name, Factory factory);
	[[nodiscard]] bool Has(StringView name) const;
	[[nodiscard]] Scene *Get(StringView name);
	/// Widget racine de la scène (la construit au besoin, sans l'afficher).
	[[nodiscard]] Widget RootOf(StringView name);

	/// Affiche la scène (construite au premier appel) ; faux si inconnue.
	bool Show(StringView name);
	/// Cache sans détruire.
	bool Hide(StringView name);
	/// Cache les autres, affiche `name`.
	bool SwitchTo(StringView name);
	bool Toggle(StringView name);
	/// Détruit les widgets (reconstruits au prochain Show).
	bool Destroy(StringView name);
	bool Remove(StringView name);

	[[nodiscard]] std::vector<String> VisibleScenes() const;
	/// Dernière scène affichée par SwitchTo (vide : aucune).
	[[nodiscard]] const String &Current() const noexcept { return m_current; }

private:
	void Build(Scene &s);

	Widget m_parent;
	std::map<String, Scene, std::less<>> m_scenes;
	String m_current;
};

} // namespace ui
