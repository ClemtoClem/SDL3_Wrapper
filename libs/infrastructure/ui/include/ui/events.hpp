#pragma once
/**
 * ui::events — liaison d'évènements façon JavaScript (`OnBind`).
 *
 * Un `Trigger` décrit UN évènement attendu : une source (bouton de souris,
 * touche, bouton de manette ou de joystick, doigt, stylet, molette, pointeur,
 * fenêtre, dépôt de fichier, saisie de texte… ou évènement logique d'un
 * widget : valeur changée, case cochée, élément sélectionné…), un code
 * (quel bouton, quelle touche — `ANY` pour tous), un geste (`sdl3::ButtonEvent`
 * : appui, relâchement, clic, double clic, N clics, appui long, répétition)
 * et un paramètre (nombre de clics, durée d'appui en ms).
 *
 * @code{.cpp}
 * buttonQuit.OnBind({sdl3::MouseButton::Left, sdl3::ButtonEvent::MultiClicks, 4},
 *                   {sdl3::GamepadButton::EAST, sdl3::ButtonEvent::Clic, 0},
 *                   [&](ui::WidgetEvent) { workViewport3d.Hide(); });
 * @endcode
 *
 * Une liaison réagit à N'IMPORTE LEQUEL de ses déclencheurs. Les évènements
 * d'entrée (pointeur, clavier, manette) remontent du widget visé vers ses
 * ancêtres (« bouillonnement », comme en JavaScript) jusqu'à ce qu'un rappel
 * appelle `StopPropagation()` ; un clic sur le libellé d'un bouton atteint
 * donc le bouton. Les évènements logiques (Change, Toggle, Select…) sont
 * émis sur le widget qui porte la valeur et ne remontent pas.
 */
#include <functional>
#include <utility>
#include <vector>

#include <ecs/ecs.hpp>
#include <sdl3/events.hpp>
#include <sdl3/input.hpp>

#include "types.hpp"

namespace ui {

class Widget;

/// Origine d'un évènement.
enum class InputSource : uint8_t {
	// Boutons (gestes reconnus : sdl3::ButtonEvent)
	MouseButton,
	Key,
	GamepadButton,
	JoystickButton,
	Finger,
	PenButton,
	// Pointeur et axes
	Wheel,
	PointerMove,
	PointerEnter,
	PointerLeave,
	GamepadAxis,
	JoystickAxis,
	JoystickHat,
	// Système
	Window,
	Drop,
	TextInput,
	// Évènements logiques des widgets
	Change,		 ///< valeur numérique (slider, knob, champ numérique…) — event.value
	Toggle,		 ///< état coché/déplié — event.checked
	Select,		 ///< sélection d'une liste/combo/onglets — event.index
	Submit,		 ///< Entrée dans un champ — event.text
	TextChange,	 ///< texte modifié — event.text
	Scroll,		 ///< défilement — event.value (décalage)
	Open,		 ///< popup ouvert
	Close,		 ///< popup fermé
	Focus,		 ///< focus clavier gagné
	Blur,		 ///< focus clavier perdu
	DragStart,	 ///< début de glisser-déposer (source)
	DropReceived, ///< dépôt reçu (cible) — event.payload, event.text = genre
	Reorder,	 ///< réordonnancement (conteneur) — event.index → event.index2
	ContextMenu, ///< demande de menu contextuel (clic droit, touche Menu)
	Custom,		 ///< évènement applicatif (ui::Emit) — event.text = nom
};

/// Évènement attendu par une liaison.
struct Trigger {
	static constexpr int ANY = -1;

	InputSource source = InputSource::Custom;
	int code = ANY;
	sdl3::ButtonEvent gesture = sdl3::ButtonEvent::Press;
	int param = 0;
	uint16_t mods = 0; ///< modificateurs requis (sdl3::keyboard::Keymod), 0 = aucun exigé

	constexpr Trigger() = default;
	constexpr Trigger(sdl3::MouseButton b, sdl3::ButtonEvent g = sdl3::ButtonEvent::Clic, int p = 0,
					  sdl3::keyboard::Keymod m = sdl3::keyboard::Keymod::NONE) noexcept
		: source(InputSource::MouseButton), code(int(b)), gesture(g), param(p), mods(uint16_t(m)) {}
	constexpr Trigger(sdl3::GamepadButton b, sdl3::ButtonEvent g = sdl3::ButtonEvent::Clic, int p = 0) noexcept
		: source(InputSource::GamepadButton), code(int(b)), gesture(g), param(p) {}
	constexpr Trigger(sdl3::keyboard::Key k, sdl3::ButtonEvent g = sdl3::ButtonEvent::Press, int p = 0,
					  sdl3::keyboard::Keymod m = sdl3::keyboard::Keymod::NONE) noexcept
		: source(InputSource::Key), code(int(k)), gesture(g), param(p), mods(uint16_t(m)) {}
	constexpr Trigger(InputSource s, int c = ANY, sdl3::ButtonEvent g = sdl3::ButtonEvent::Press, int p = 0) noexcept
		: source(s), code(c), gesture(g), param(p) {}

	[[nodiscard]] static constexpr Trigger JoystickButton(int button, sdl3::ButtonEvent g = sdl3::ButtonEvent::Clic,
														   int p = 0) noexcept {
		return Trigger(InputSource::JoystickButton, button, g, p);
	}
	[[nodiscard]] static constexpr Trigger Finger(sdl3::ButtonEvent g = sdl3::ButtonEvent::Clic, int p = 0) noexcept {
		return Trigger(InputSource::Finger, ANY, g, p);
	}
	[[nodiscard]] static constexpr Trigger Pen(int button = ANY, sdl3::ButtonEvent g = sdl3::ButtonEvent::Clic,
											   int p = 0) noexcept {
		return Trigger(InputSource::PenButton, button, g, p);
	}
	[[nodiscard]] static constexpr Trigger GamepadAxis(sdl3::GamepadAxis axis, int thresholdPermille = 500) noexcept {
		return Trigger(InputSource::GamepadAxis, int(axis), sdl3::ButtonEvent::Press, thresholdPermille);
	}
	[[nodiscard]] static constexpr Trigger JoystickAxis(int axis, int thresholdPermille = 500) noexcept {
		return Trigger(InputSource::JoystickAxis, axis, sdl3::ButtonEvent::Press, thresholdPermille);
	}
	[[nodiscard]] static constexpr Trigger JoystickHat(int hat = ANY) noexcept { return Trigger(InputSource::JoystickHat, hat); }
	[[nodiscard]] static constexpr Trigger Wheel() noexcept { return Trigger(InputSource::Wheel); }
	[[nodiscard]] static constexpr Trigger PointerMove() noexcept { return Trigger(InputSource::PointerMove); }
	[[nodiscard]] static constexpr Trigger PointerEnter() noexcept { return Trigger(InputSource::PointerEnter); }
	[[nodiscard]] static constexpr Trigger PointerLeave() noexcept { return Trigger(InputSource::PointerLeave); }
	/// Évènement de fenêtre (`sdl3::EventType::WINDOW_RESIZED`…), ANY = tous.
	[[nodiscard]] static constexpr Trigger Window(int eventType = ANY) noexcept { return Trigger(InputSource::Window, eventType); }
	[[nodiscard]] static constexpr Trigger Window(sdl3::EventType type) noexcept {
		return Trigger(InputSource::Window, int(type));
	}
	[[nodiscard]] static constexpr Trigger DropFile() noexcept { return Trigger(InputSource::Drop); }
	[[nodiscard]] static constexpr Trigger Text() noexcept { return Trigger(InputSource::TextInput); }
	[[nodiscard]] static constexpr Trigger Of(InputSource logical) noexcept { return Trigger(logical); }

	/// Vrai si l'évènement survenu `occurred` satisfait ce déclencheur.
	[[nodiscard]] bool Matches(const Trigger &occurred) const noexcept;
	[[nodiscard]] bool IsButton() const noexcept { return source <= InputSource::PenButton; }

	constexpr bool operator==(const Trigger &) const = default;
};

/// Évènement transmis aux rappels.
struct WidgetEvent {
	ecs::ArchetypeRegistry *world = nullptr;
	ecs::Entity target{};		 ///< widget visé (le plus en avant sous le pointeur, ou le focus)
	ecs::Entity currentTarget{}; ///< widget dont la liaison est en cours d'exécution
	Trigger trigger;			 ///< ce qui est survenu
	const sdl3::Event *raw = nullptr;

	sdl3::FPoint pointer{};
	sdl3::FPoint delta{};
	sdl3::FPoint wheel{};
	int clicks = 0;
	uint16_t mods = 0;
	float value = 0.f;
	int index = -1;
	int index2 = -1;
	bool checked = false;
	int64_t payload = 0;
	String text;

	[[nodiscard]] Widget Target() const;
	[[nodiscard]] Widget Current() const;
	/// Arrête la remontée vers les ancêtres (utilisable depuis un rappel).
	void StopPropagation() const noexcept { m_stopped = true; }
	/// Annule le comportement par défaut des briques (bascule, sélection…).
	void PreventDefault() const noexcept { m_prevented = true; }
	[[nodiscard]] bool Stopped() const noexcept { return m_stopped; }
	[[nodiscard]] bool DefaultPrevented() const noexcept { return m_prevented; }
	[[nodiscard]] bool Ctrl() const noexcept { return (mods & uint16_t(sdl3::keyboard::Keymod::CTRL)) != 0; }
	[[nodiscard]] bool Shift() const noexcept { return (mods & uint16_t(sdl3::keyboard::Keymod::SHIFT)) != 0; }
	[[nodiscard]] bool Alt() const noexcept { return (mods & uint16_t(sdl3::keyboard::Keymod::ALT)) != 0; }

private:
	mutable bool m_stopped = false;
	mutable bool m_prevented = false;
};

using EventCallback = std::function<void(const WidgetEvent &)>;
/// Paire « déclencheurs, rappel » (cf. Widget::GetBind / GetBinds).
using BindPair = std::pair<std::vector<Trigger>, EventCallback>;

struct Binding {
	uint32_t id = 0;
	std::vector<Trigger> triggers;
	EventCallback callback;
	bool enabled = true;
	/// Liaison posée par un widget composé (mise à jour de son libellé…) :
	/// invisible pour HasBind/GetBinds/OffBind.
	bool internal = false;
};

/// Composant : liaisons d'un widget.
struct Bindings {
	std::vector<Binding> list;
	uint32_t nextId = 1;

	uint32_t Add(std::vector<Trigger> triggers, EventCallback cb, bool internal = false);
	/// Retire `t` de toutes les liaisons publiques (une liaison vidée disparaît) ;
	/// rend le nombre de liaisons touchées.
	size_t Remove(const Trigger &t);
	bool RemoveId(uint32_t id);
	void RemoveAll(bool includeInternal = false);
	[[nodiscard]] bool Has(const Trigger &t) const;
	[[nodiscard]] Option<BindPair> Get(const Trigger &t) const;
	[[nodiscard]] std::vector<BindPair> All() const;
	[[nodiscard]] bool Listens(const Trigger &occurred) const;
};

/// Nom lisible d'un déclencheur (débogage, menus de raccourcis).
[[nodiscard]] String DescribeTrigger(const Trigger &t);

} // namespace ui
