#pragma once
/**
 * Événements ECS : files typées doublement tamponnées (à la Bevy).
 *
 * Un système ÉMET (`Send`) pendant l'image ; tous les systèmes LISENT ce qui
 * a été émis pendant l'image courante ET la précédente, si bien qu'un
 * lecteur placé AVANT l'émetteur dans l'ordre d'exécution ne perd rien.
 * `Update()` (appelé une fois par image, cf. Schedule) fait tourner les
 * tampons : un événement vit donc exactement deux images.
 *
 * Les files vivent dans les ressources du monde : `EventsOf<T>(world)` les
 * crée au premier usage. Un lecteur qui ne doit traiter chaque événement
 * qu'UNE fois tient un `EventCursor` (numéro du dernier événement lu).
 *
 * Événements d'entités fournis : EntitySpawned, EntityDespawned (émis par
 * DespawnRecursive, relations.hpp, et par les fabriques des jeux).
 */
#include <cstdint>
#include <functional>
#include <vector>

#include "ecs.hpp"

namespace ecs {

struct EntitySpawned {
	Entity entity;
};
struct EntityDespawned {
	Entity entity;
};

template <typename T> class Events {
public:
	void Send(T event) {
		m_current.push_back(Stamped{m_next++, std::move(event)});
	}

	/// Tous les événements encore vivants (image précédente puis courante).
	template <typename Fn> void ForEach(Fn &&fn) const {
		for (const Stamped &e : m_previous)
			fn(e.event);
		for (const Stamped &e : m_current)
			fn(e.event);
	}

	/// Événements postérieurs au curseur, qui avance : chacun n'est lu
	/// qu'une fois par CE lecteur.
	template <typename Fn> void Read(uint64_t &cursor, Fn &&fn) const {
		auto visit = [&](const std::vector<Stamped> &list) {
			for (const Stamped &e : list)
				if (e.serial >= cursor) {
					fn(e.event);
					cursor = e.serial + 1;
				}
		};
		visit(m_previous);
		visit(m_current);
	}

	[[nodiscard]] size_t Size() const noexcept { return m_previous.size() + m_current.size(); }
	[[nodiscard]] bool IsEmpty() const noexcept { return Size() == 0; }

	/// Fin d'image : les événements de l'image précédente disparaissent.
	void Update() {
		m_previous.swap(m_current);
		m_current.clear();
	}
	void Clear() {
		m_previous.clear();
		m_current.clear();
	}

private:
	struct Stamped {
		uint64_t serial;
		T event;
	};
	std::vector<Stamped> m_previous, m_current;
	uint64_t m_next = 0;
};

/// Rotation de toutes les files d'un monde (une fonction par type, posée
/// par EventsOf au premier usage) — appelée par Schedule::Update.
struct EventUpdaters {
	std::vector<std::function<void(ArchetypeRegistry &)>> updaters;
};

/// File d'événements `T` du monde (créée au premier usage).
template <typename T> Events<T> &EventsOf(ArchetypeRegistry &world) {
	if (!world.resources.Contains<Events<T>>()) {
		world.resources.Insert(Events<T>{});
		if (!world.resources.Contains<EventUpdaters>())
			world.resources.Insert(EventUpdaters{});
		world.resources.Get<EventUpdaters>().Unwrap()->updaters.push_back([](ArchetypeRegistry &w) {
			if (auto events = w.resources.Get<Events<T>>(); events.IsSome())
				events.Unwrap()->Update();
		});
	}
	return *world.resources.Get<Events<T>>().Unwrap();
}

/// Fait tourner toutes les files (fin d'image).
inline void UpdateAllEvents(ArchetypeRegistry &world) {
	if (auto updaters = world.resources.Get<EventUpdaters>(); updaters.IsSome())
		for (auto &update : updaters.Unwrap()->updaters)
			update(world);
}

} // namespace ecs
