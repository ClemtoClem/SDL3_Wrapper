#pragma once
/**
 * ui::Builder — builders fluents façon Flutter.
 *
 * Un builder est une VALEUR qui accumule la configuration d'un widget
 * (identité, style, briques atomiques, liaisons, enfants) ; rien n'existe
 * dans le monde ECS tant qu'il n'est pas matérialisé (`Mount`, `Build`,
 * `Widget::Append`). On compose donc l'arbre entier de façon déclarative :
 *
 * @code{.cpp}
 * ui::Widget quit;
 * gui.Mount(ui::Column().Gap(8).Padding(16).Children({
 *     ui::Text("Options").FontSize(20).Bold(),
 *     ui::Row().Gap(8).Children({
 *         ui::Text("Volume").Grow(),
 *         ui::Slider(0.f, 1.f, 0.8f).Width(140.f).OnChange([](float v) { ... }),
 *     }),
 *     ui::Button("Quitter").Id("quit").Class("danger").Ref(quit),
 * }));
 * @endcode
 *
 * Les widgets composés (curseur, menu déroulant, onglets…) sont des builders
 * dont `Expand` décrit la structure interne avec les mêmes briques
 * atomiques ; leurs enfants internes portent des classes (`slider-track`,
 * `slider-thumb`…) que les feuilles de style peuvent cibler.
 */
#include <functional>
#include <initializer_list>
#include <memory>
#include <vector>

#include "widget.hpp"

namespace ui {

class BuilderBase;

/// Builder quelconque (enfant d'un autre builder) — copie partagée immuable.
class AnyWidget {
public:
	AnyWidget() = default;
	template <typename B>
		requires std::is_base_of_v<BuilderBase, std::decay_t<B>>
	AnyWidget(B &&builder) : m_builder(std::make_shared<std::decay_t<B>>(std::forward<B>(builder))) {}

	[[nodiscard]] bool IsEmpty() const noexcept { return !m_builder; }
	[[nodiscard]] const BuilderBase *Get() const noexcept { return m_builder.get(); }
	/// Matérialise sous `parent` (invalide : sous la racine du contexte).
	Widget Build(ecs::ArchetypeRegistry &world, ecs::Entity parent, Option<size_t> index = NONE) const;

private:
	std::shared_ptr<const BuilderBase> m_builder;
};

/// Conteneur d'éléments générés (liste, menu déroulant, onglets) :
/// `Widget::SetItems` le vide et le remplit avec `make(texte, index)`.
struct ItemsHost {
	std::function<AnyWidget(const String &, int)> make;
};

/// Configuration accumulée par un builder.
struct WidgetSpec {
	Element element;
	InlineStyle style;
	std::vector<String> imports;
	std::vector<String> css;
	std::vector<std::function<void(ecs::ArchetypeRegistry &, ecs::Entity)>> components;
	Bindings bindings;
	std::vector<AnyWidget> children;
	std::vector<Widget *> refs;
	std::vector<std::function<void(Widget)>> built;
	bool hidden = false;
	bool disabled = false;
	String tooltip;
};

class BuilderBase {
public:
	virtual ~BuilderBase() = default;

	WidgetSpec spec;

	/// Matérialise le widget (et sa descendance) dans `world`, sous `parent`
	/// (invalide : la racine du contexte, ou une racine libre sans contexte).
	Widget Build(ecs::ArchetypeRegistry &world, ecs::Entity parent = ecs::Entity{},
				 Option<size_t> index = NONE) const;

protected:
	/// Widgets composés : complète la spécification (structure interne) au
	/// moment de la construction — `s` est une copie de `spec`.
	virtual void Expand(WidgetSpec &s) const { (void)s; }
};

template <typename D> class Builder : public BuilderBase, public StyleSetters<D>, public EventMethods<D> {
public:
	Builder() = default;
	explicit Builder(String type) { spec.element.type = std::move(type); }

	// ── Identité ────────────────────────────────────────────────────────────
	D &Id(String id) {
		spec.element.id = std::move(id);
		return Self();
	}
	/// Ajoute une classe de style (cumulable).
	D &Class(String name) {
		spec.element.classes.push_back(std::move(name));
		return Self();
	}
	D &Classes(std::vector<String> names) {
		for (String &n : names)
			spec.element.classes.push_back(std::move(n));
		return Self();
	}
	/// Change le type CSS (sélecteurs de type).
	D &Type(String type) {
		spec.element.type = std::move(type);
		return Self();
	}

	// ── Styles ──────────────────────────────────────────────────────────────
	/// Feuille de style propre à ce widget (et à sa descendance) — `this`
	/// y désigne ce widget.
	D &Import(String path) {
		spec.imports.push_back(std::move(path));
		return Self();
	}
	/// Texte CSS propre à ce widget (même portée qu'Import).
	D &Css(String text) {
		spec.css.push_back(std::move(text));
		return Self();
	}
	/// Fusionne un style dans le style inline privé.
	D &ApplyStyle(const Style &s) {
		spec.style.own.Merge(s.Props());
		return Self();
	}
	/// Style inline partagé : hérité par les enfants (comme une classe).
	D &Cascade(const Style &s) {
		spec.style.shared.Merge(s.Props());
		return Self();
	}
	/// Variante appliquée quand le widget est dans tous les états de `mask`.
	D &StateStyle(State mask, const Style &s, bool shared = false) {
		spec.style.states.push_back({mask, s.Props(), shared});
		return Self();
	}
	D &HoverStyle(const Style &s) { return StateStyle(State::Hover, s); }
	D &ActiveStyle(const Style &s) { return StateStyle(State::Active, s); }
	D &FocusStyle(const Style &s) { return StateStyle(State::Focus, s); }
	D &CheckedStyle(const Style &s) { return StateStyle(State::Checked, s); }
	D &DisabledStyle(const Style &s) { return StateStyle(State::Disabled, s); }
	D &SelectedStyle(const Style &s) { return StateStyle(State::Selected, s); }
	D &OpenStyle(const Style &s) { return StateStyle(State::Open, s); }

	// ── État initial ────────────────────────────────────────────────────────
	D &Hidden(bool on = true) {
		spec.hidden = on;
		return Self();
	}
	D &Disabled(bool on = true) {
		spec.disabled = on;
		return Self();
	}
	D &Tooltip(String text) {
		spec.tooltip = std::move(text);
		return Self();
	}

	// ── Briques ─────────────────────────────────────────────────────────────
	/// Ajoute une brique atomique quelconque (composant ECS).
	template <typename T> D &With(T component) {
		spec.components.push_back([c = std::move(component)](ecs::ArchetypeRegistry &w, ecs::Entity e) mutable {
			w.AddComponent(e, std::move(c));
		});
		return Self();
	}
	D &Focusable(int tabIndex = 0) { return With(ui::Focusable{tabIndex}); }
	D &Pressable() { return With(ui::Pressable{}); }
	/// Source de glisser-déposer (`label` : fantôme près du pointeur).
	D &DragSource(String kind, int64_t id, String label = String()) {
		return With(ui::DragSource{std::move(kind), id, std::move(label), 1});
	}
	D &DropTarget(String accepts = String(), bool highlight = true) {
		return With(ui::DropTarget{std::move(accepts), highlight});
	}
	D &Reorderable() { return With(ui::Reorderable{}); }
	/// Menu contextuel ouvert au clic droit (cf. ui::ContextMenu).
	D &ContextMenu(AnyWidget menu) {
		With(PopupTrigger{PopupOpen::ContextMenu, Placement::Point});
		spec.children.push_back(std::move(menu));
		return Self();
	}

	// ── Enfants, références ─────────────────────────────────────────────────
	D &Children(std::initializer_list<AnyWidget> children) {
		for (const AnyWidget &c : children)
			spec.children.push_back(c);
		return Self();
	}
	D &Children(std::vector<AnyWidget> children) {
		for (AnyWidget &c : children)
			spec.children.push_back(std::move(c));
		return Self();
	}
	D &Child(AnyWidget child) {
		spec.children.push_back(std::move(child));
		return Self();
	}
	/// Reçoit la poignée du widget une fois construit.
	D &Ref(Widget &out) {
		spec.refs.push_back(&out);
		return Self();
	}
	/// Appelé une fois le widget et ses enfants construits.
	D &OnBuilt(std::function<void(Widget)> fn) {
		spec.built.push_back(std::move(fn));
		return Self();
	}
	/// Droits d'un champ de saisie (lecture seule, copie, collage…).
	D &EditMode(IOMode mode) {
		return OnBuilt([mode](Widget w) {
			if (auto ed = w.Get<TextEditor>(); ed.IsSome())
				ed.Unwrap()->mode = mode;
		});
	}
	/// Longueur maximale d'un champ de saisie (attribut HTML `maxlength`).
	D &MaxLength(size_t n) {
		return OnBuilt([n](Widget w) {
			if (auto ed = w.Get<TextEditor>(); ed.IsSome())
				ed.Unwrap()->maxLength = n;
		});
	}

	// ── Cibles des mixins ───────────────────────────────────────────────────
	[[nodiscard]] StyleProps &Props() noexcept { return spec.style.own; }
	uint32_t AddBinding(std::vector<Trigger> triggers, EventCallback cb, bool internal) {
		return spec.bindings.Add(std::move(triggers), std::move(cb), internal);
	}
	[[nodiscard]] Bindings *BindingsPtr() noexcept { return &spec.bindings; }

	/// Matérialise sous un widget existant.
	Widget BuildInto(Widget parent) const {
		return parent.Valid() ? Build(*parent.World(), parent.Entity()) : Widget();
	}

protected:
	D &Self() { return static_cast<D &>(*this); }
};

/// Builder universel : boîte, conteneur ou widget composé du catalogue
/// (widgets.hpp). `SetExpand` lui donne une structure interne calculée à la
/// construction — par exemple ranger les enfants fournis par l'application
/// dans le corps d'un panneau repliable.
class WidgetBuilder : public Builder<WidgetBuilder> {
public:
	explicit WidgetBuilder(String type = "box") : Builder(std::move(type)) {}

	WidgetBuilder &SetExpand(std::function<void(WidgetSpec &)> expand) {
		m_expand = std::move(expand);
		return *this;
	}

protected:
	void Expand(WidgetSpec &s) const override {
		if (m_expand)
			m_expand(s);
	}

private:
	std::function<void(WidgetSpec &)> m_expand;
};

} // namespace ui
