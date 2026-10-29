#pragma once
/**
 * ui::Widget — poignée légère (monde + entité) sur un widget matérialisé.
 *
 * Elle n'ajoute aucun état : tout vit dans les composants de l'entité. On
 * l'obtient d'un builder (`Mount`, `.Ref(w)`, `.OnBuilt(...)`), d'une
 * recherche (`ui.Find("#quit")`, `w.FindAll(".item")`) ou d'un évènement
 * (`event.Target()`).
 *
 * Elle offre les mêmes setters de style que les builders (style inline privé,
 * appliqué à la prochaine passe) et la même API d'évènements (`OnBind`…).
 */
#include <vector>

#include "context.hpp"
#include "event_methods.hpp"
#include "style_setters.hpp"

namespace ui {

class AnyWidget;

class Widget : public StyleSetters<Widget>, public EventMethods<Widget> {
public:
	Widget() = default;
	Widget(ecs::ArchetypeRegistry &world, ecs::Entity entity) noexcept : m_world(&world), m_entity(entity) {}

	[[nodiscard]] bool Valid() const noexcept { return m_world && m_world->IsAlive(m_entity); }
	[[nodiscard]] explicit operator bool() const noexcept { return Valid(); }
	[[nodiscard]] ecs::Entity Entity() const noexcept { return m_entity; }
	[[nodiscard]] ecs::ArchetypeRegistry *World() const noexcept { return m_world; }
	bool operator==(const Widget &o) const noexcept { return m_entity == o.m_entity && m_world == o.m_world; }

	// ── Identité ────────────────────────────────────────────────────────────
	[[nodiscard]] String Type() const;
	[[nodiscard]] String Id() const;
	Widget &SetId(String id);
	[[nodiscard]] bool HasClass(StringView name) const;
	Widget &AddClass(String name);
	Widget &RemoveClass(StringView name);
	/// Rend vrai si la classe est présente après bascule.
	bool ToggleClass(String name);

	// ── Arbre ───────────────────────────────────────────────────────────────
	[[nodiscard]] Option<Widget> Parent() const;
	[[nodiscard]] std::vector<Widget> Children() const;
	[[nodiscard]] size_t ChildCount() const;
	/// Construit `child` comme dernier enfant.
	Widget Append(const AnyWidget &child);
	/// Construit `child` à la position `index` parmi les enfants.
	Widget Insert(size_t index, const AnyWidget &child);
	/// Détruit tous les enfants.
	Widget &Clear();
	/// Déplace ce widget (et sa descendance) en dernier enfant de `parent`
	/// (comme `appendChild` d'un nœud déjà dans le DOM). Faux si `parent`
	/// est un descendant de ce widget.
	bool MoveTo(Widget parent);
	/// Vrai si `other` est ce widget ou l'un de ses descendants (`Node.contains`).
	[[nodiscard]] bool Contains(const Widget &other) const;
	/// Détruit le widget et sa descendance (la poignée devient invalide).
	void Destroy();
	/// Premier descendant (lui compris) qui correspond au sélecteur CSS.
	[[nodiscard]] Option<Widget> Find(StringView selector) const;
	[[nodiscard]] std::vector<Widget> FindAll(StringView selector) const;
	/// Premier descendant de type `type` (ex. "popup").
	[[nodiscard]] Option<Widget> FindType(StringView type) const;

	// ── Visibilité, activation ──────────────────────────────────────────────
	Widget &Show();
	Widget &Hide();
	Widget &SetVisible(bool visible);
	[[nodiscard]] bool IsVisible() const;
	Widget &Enable();
	Widget &Disable();
	Widget &SetEnabled(bool enabled);
	[[nodiscard]] bool IsEnabled() const;

	// ── Style ───────────────────────────────────────────────────────────────
	/// Cible des setters hérités (style inline PRIVÉ) — marque le widget à restyler.
	[[nodiscard]] StyleProps &Props();
	Widget &SetStyle(const Style &style);
	/// Style inline partagé (hérité par les enfants).
	Widget &Cascade(const Style &style);
	Widget &ClearStyle();
	/// Déclaration CSS textuelle sur le style privé.
	Widget &SetProperty(StringView property, StringView value);
	/// Importe une feuille pour ce widget et sa descendance.
	Result<bool, String> Import(const String &path);
	Widget &AddStyleSheet(StyleSheetPtr sheet);
	[[nodiscard]] const ComputedStyle *Computed() const;
	/// Boîte calculée à l'écran (boîte de bordure).
	[[nodiscard]] sdl3::FRect Bounds() const;
	[[nodiscard]] sdl3::FRect ContentBounds() const;
	[[nodiscard]] State GetState() const;
	[[nodiscard]] bool IsHovered() const { return HasState(GetState(), State::Hover); }
	[[nodiscard]] bool IsFocused() const { return HasState(GetState(), State::Focus); }
	[[nodiscard]] bool IsPressed() const { return HasState(GetState(), State::Active); }

	// ── Contenu et valeurs ──────────────────────────────────────────────────
	[[nodiscard]] String Text() const;
	Widget &SetText(String text);
	Widget &SetImage(String source);
	Widget &SetTooltip(String text);
	[[nodiscard]] float Value() const;
	/// `notify` : émet Change comme une modification de l'utilisateur.
	Widget &SetValue(float value, bool notify = false);
	Widget &SetRange(float min, float max);
	[[nodiscard]] bool Checked() const;
	Widget &SetChecked(bool checked, bool notify = false);
	[[nodiscard]] int Selected() const;
	[[nodiscard]] std::vector<int> SelectedAll() const;
	Widget &SetSelected(int index, bool notify = false);
	/// Remplace les éléments d'une liste / d'un menu déroulant / d'onglets.
	Widget &SetItems(std::vector<String> items);
	[[nodiscard]] ui::Color ColorValue() const;
	Widget &SetColorValue(ui::Color c, bool notify = false);

	// ── Popups ──────────────────────────────────────────────────────────────
	/// Ouvre le popup (ce widget, ou son premier popup descendant).
	Widget &Open();
	Widget &OpenAt(sdl3::FPoint at);
	Widget &OpenBelow(const Widget &anchor);
	Widget &Close();
	[[nodiscard]] bool IsOpen() const;

	// ── Focus, défilement ───────────────────────────────────────────────────
	Widget &Focus();
	Widget &Blur();
	Widget &ScrollTo(sdl3::FPoint offset);
	[[nodiscard]] sdl3::FPoint ScrollOffset() const;
	/// Fait défiler les ancêtres pour rendre le widget visible.
	Widget &ScrollIntoView();

	// ── Évènements ──────────────────────────────────────────────────────────
	uint32_t AddBinding(std::vector<Trigger> triggers, EventCallback cb, bool internal);
	[[nodiscard]] Bindings *BindingsPtr();
	/// Émet un évènement sur ce widget (comme le ferait l'interface).
	void Emit(Trigger trigger, WidgetEvent event = {}, bool bubble = false);

	// ── Composants ──────────────────────────────────────────────────────────
	template <typename T> [[nodiscard]] Option<RefMut<T>> Get() {
		if (!Valid())
			return NONE;
		return m_world->GetComponent<T>(m_entity);
	}
	template <typename T> [[nodiscard]] bool Has() const { return Valid() && m_world->HasComponent<T>(m_entity); }
	template <typename T> Widget &Set(T component) {
		if (Valid()) {
			m_world->AddComponent(m_entity, std::move(component));
			MarkLayoutDirty(*m_world);
			MarkStyleDirty(*m_world, m_entity);
		}
		return *this;
	}
	template <typename T> Widget &Remove() {
		if (Valid()) {
			m_world->RemoveComponent<T>(m_entity);
			MarkLayoutDirty(*m_world);
		}
		return *this;
	}

	// Les setters de style « Hide » (display: none) sont masqués par
	// Hide()/Show() ci-dessus (composant Hidden), plus usuels sur un widget.
	using StyleSetters<Widget>::Display;

private:
	ecs::ArchetypeRegistry *m_world = nullptr;
	ecs::Entity m_entity{};
};

/// Émet `trigger` sur `target` (bulles vers les ancêtres si `bubble`) et
/// exécute les liaisons concernées.
void DispatchEvent(ecs::ArchetypeRegistry &world, ecs::Entity target, WidgetEvent event, bool bubble);

/// Émet un évènement applicatif nommé (cf. EventMethods::OnCustom).
void Emit(Widget target, const String &name, bool bubble = true);

/// Widgets correspondant à un sélecteur, dans tout l'arbre de `world`.
[[nodiscard]] std::vector<Widget> QueryAll(ecs::ArchetypeRegistry &world, StringView selector);
[[nodiscard]] Option<Widget> Query(ecs::ArchetypeRegistry &world, StringView selector);

} // namespace ui
