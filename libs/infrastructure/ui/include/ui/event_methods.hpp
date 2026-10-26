#pragma once
/**
 * ui::EventMethods — API d'évènements commune aux builders et aux widgets :
 * `OnBind` / `OffBind` / `HasBind` / `GetBind` / `GetBinds`, et les raccourcis
 * (`OnClick`, `OnChange`, `OnToggle`…) qui ne sont que des `OnBind` sur les
 * déclencheurs usuels.
 *
 * `D` doit fournir :
 *   uint32_t AddBinding(std::vector<Trigger>, EventCallback, bool internal);
 *   Bindings *BindingsPtr();   // nullptr si aucune liaison n'existe encore
 */
#include "events.hpp"

namespace ui {

template <typename D> class EventMethods {
public:
	// ── Liaisons génériques ─────────────────────────────────────────────────
	D &OnBind(Trigger a, EventCallback cb) { return Bind({a}, std::move(cb)); }
	D &OnBind(Trigger a, Trigger b, EventCallback cb) { return Bind({a, b}, std::move(cb)); }
	D &OnBind(Trigger a, Trigger b, Trigger c, EventCallback cb) { return Bind({a, b, c}, std::move(cb)); }
	D &OnBind(Trigger a, Trigger b, Trigger c, Trigger d, EventCallback cb) { return Bind({a, b, c, d}, std::move(cb)); }
	D &OnBind(Trigger a, Trigger b, Trigger c, Trigger d, Trigger e, EventCallback cb) {
		return Bind({a, b, c, d, e}, std::move(cb));
	}
	D &OnBind(Trigger a, Trigger b, Trigger c, Trigger d, Trigger e, Trigger f, EventCallback cb) {
		return Bind({a, b, c, d, e, f}, std::move(cb));
	}
	/// Composition d'un nombre quelconque de déclencheurs.
	D &OnBindAll(std::vector<Trigger> triggers, EventCallback cb) { return Bind(std::move(triggers), std::move(cb)); }

	/// Désactive un déclencheur (retiré de toutes les liaisons qui l'écoutent).
	D &OffBind(const Trigger &t) {
		if (Bindings *b = Self().BindingsPtr())
			(void)b->Remove(t);
		return Self();
	}
	/// Retire toutes les liaisons posées par l'application.
	D &OffBind() {
		if (Bindings *b = Self().BindingsPtr())
			b->RemoveAll(false);
		return Self();
	}
	[[nodiscard]] bool HasBind(const Trigger &t) { return Self().BindingsPtr() && Self().BindingsPtr()->Has(t); }
	/// Déclencheurs et rappel de la liaison qui écoute `t`.
	[[nodiscard]] Option<BindPair> GetBind(const Trigger &t) {
		if (Bindings *b = Self().BindingsPtr())
			return b->Get(t);
		return NONE;
	}
	[[nodiscard]] std::vector<BindPair> GetBinds() {
		if (Bindings *b = Self().BindingsPtr())
			return b->All();
		return {};
	}

	// ── Raccourcis ──────────────────────────────────────────────────────────
	/// Clic gauche, Entrée/Espace avec le focus, bouton SOUTH de la manette.
	D &OnClick(EventCallback cb) {
		return Bind({{sdl3::MouseButton::Left, sdl3::ButtonEvent::Clic},
					 {sdl3::keyboard::Key::RETURN, sdl3::ButtonEvent::Press},
					 {sdl3::keyboard::Key::KP_ENTER, sdl3::ButtonEvent::Press},
					 {sdl3::keyboard::Key::SPACE, sdl3::ButtonEvent::Press},
					 {sdl3::GamepadButton::SOUTH, sdl3::ButtonEvent::Clic}},
					std::move(cb));
	}
	D &OnClick(std::function<void()> fn) {
		return OnClick(EventCallback([fn = std::move(fn)](const WidgetEvent &) { fn(); }));
	}
	D &OnDoubleClick(std::function<void()> fn) {
		return Bind({{sdl3::MouseButton::Left, sdl3::ButtonEvent::DoubleClic}},
					[fn = std::move(fn)](const WidgetEvent &) { fn(); });
	}
	D &OnRightClick(std::function<void(sdl3::FPoint)> fn) {
		return Bind({{sdl3::MouseButton::Right, sdl3::ButtonEvent::Clic}},
					[fn = std::move(fn)](const WidgetEvent &e) { fn(e.pointer); });
	}
	/// Clic droit (ou touche Menu) : position du pointeur, point d'entrée d'un menu contextuel.
	/// L'évènement remonte (comme `contextmenu` en JS) : un élément qui a son
	/// propre menu dans un conteneur qui en a un appelle `StopPropagation()`.
	D &OnContextMenu(EventCallback cb) { return Bind({Trigger::Of(InputSource::ContextMenu)}, std::move(cb)); }
	D &OnContextMenu(std::function<void(sdl3::FPoint)> fn) {
		return OnContextMenu(EventCallback([fn = std::move(fn)](const WidgetEvent &e) { fn(e.pointer); }));
	}
	/// Entrée (vrai) / sortie (faux) du pointeur.
	D &OnHover(std::function<void(bool)> fn) {
		return Bind({Trigger::PointerEnter(), Trigger::PointerLeave()}, [fn = std::move(fn)](const WidgetEvent &e) {
			fn(e.trigger.source == InputSource::PointerEnter);
		});
	}
	D &OnPointerMove(std::function<void(sdl3::FPoint)> fn) {
		return Bind({Trigger::PointerMove()}, [fn = std::move(fn)](const WidgetEvent &e) { fn(e.pointer); });
	}
	D &OnWheel(std::function<void(sdl3::FPoint)> fn) {
		return Bind({Trigger::Wheel()}, [fn = std::move(fn)](const WidgetEvent &e) { fn(e.wheel); });
	}
	D &OnKey(sdl3::keyboard::Key key, std::function<void()> fn, sdl3::keyboard::Keymod mods = sdl3::keyboard::Keymod::NONE) {
		return Bind({{key, sdl3::ButtonEvent::Press, 0, mods}}, [fn = std::move(fn)](const WidgetEvent &) { fn(); });
	}
	/// Valeur numérique modifiée (curseur, potentiomètre, champ numérique…).
	D &OnChange(std::function<void(float)> fn) {
		return Bind({Trigger::Of(InputSource::Change)}, [fn = std::move(fn)](const WidgetEvent &e) { fn(e.value); });
	}
	/// État coché / déplié modifié.
	D &OnToggle(std::function<void(bool)> fn) {
		return Bind({Trigger::Of(InputSource::Toggle)}, [fn = std::move(fn)](const WidgetEvent &e) { fn(e.checked); });
	}
	/// Élément sélectionné (liste, menu déroulant, onglets, table).
	D &OnSelect(std::function<void(int)> fn) {
		return Bind({Trigger::Of(InputSource::Select)}, [fn = std::move(fn)](const WidgetEvent &e) { fn(e.index); });
	}
	D &OnSubmit(std::function<void(const String &)> fn) {
		return Bind({Trigger::Of(InputSource::Submit)}, [fn = std::move(fn)](const WidgetEvent &e) { fn(e.text); });
	}
	D &OnTextChange(std::function<void(const String &)> fn) {
		return Bind({Trigger::Of(InputSource::TextChange)},
					[fn = std::move(fn)](const WidgetEvent &e) { fn(e.text); });
	}
	D &OnScroll(std::function<void(float)> fn) {
		return Bind({Trigger::Of(InputSource::Scroll)}, [fn = std::move(fn)](const WidgetEvent &e) { fn(e.value); });
	}
	D &OnOpen(std::function<void()> fn) {
		return Bind({Trigger::Of(InputSource::Open)}, [fn = std::move(fn)](const WidgetEvent &) { fn(); });
	}
	D &OnClose(std::function<void()> fn) {
		return Bind({Trigger::Of(InputSource::Close)}, [fn = std::move(fn)](const WidgetEvent &) { fn(); });
	}
	/// Focus gagné (vrai) / perdu (faux).
	D &OnFocus(std::function<void(bool)> fn) {
		return Bind({Trigger::Of(InputSource::Focus), Trigger::Of(InputSource::Blur)},
					[fn = std::move(fn)](const WidgetEvent &e) { fn(e.trigger.source == InputSource::Focus); });
	}
	D &OnDragStart(std::function<void()> fn) {
		return Bind({Trigger::Of(InputSource::DragStart)}, [fn = std::move(fn)](const WidgetEvent &) { fn(); });
	}
	/// Dépôt reçu : identifiant transporté par la source.
	D &OnDrop(std::function<void(int64_t)> fn) {
		return Bind({Trigger::Of(InputSource::DropReceived)},
					[fn = std::move(fn)](const WidgetEvent &e) { fn(e.payload); });
	}
	/// Fichier déposé depuis le système (chemin).
	D &OnFileDrop(std::function<void(const String &)> fn) {
		return Bind({Trigger::DropFile()}, [fn = std::move(fn)](const WidgetEvent &e) { fn(e.text); });
	}
	/// Enfant déplacé de `from` à `to` (conteneur de Reorderable).
	D &OnReorder(std::function<void(int, int)> fn) {
		return Bind({Trigger::Of(InputSource::Reorder)},
					[fn = std::move(fn)](const WidgetEvent &e) { fn(e.index, e.index2); });
	}
	/// Évènement applicatif nommé (cf. ui::Emit).
	D &OnCustom(String name, EventCallback cb) {
		return Bind({Trigger::Of(InputSource::Custom)}, [name = std::move(name), cb = std::move(cb)](const WidgetEvent &e) {
			if (e.text == name)
				cb(e);
		});
	}

protected:
	D &Bind(std::vector<Trigger> triggers, EventCallback cb, bool internal = false) {
		(void)Self().AddBinding(std::move(triggers), std::move(cb), internal);
		return Self();
	}

private:
	D &Self() { return static_cast<D &>(*this); }
};

} // namespace ui
