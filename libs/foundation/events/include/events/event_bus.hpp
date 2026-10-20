#pragma once
/**
 * events::EventBus — distribution d'événements par TYPE, découplant
 * émetteurs et récepteurs (aucun des deux ne connaît l'autre).
 *
 * @code{.cpp}
 * struct WindowResized { int width, height; };
 *
 * events::EventBus bus;
 * events::ScopedConnection c = bus.Subscribe<WindowResized>(
 *     [](const WindowResized &e) { ... });
 *
 * bus.Publish(WindowResized{800, 600});   // immédiat, sur le fil appelant
 * bus.Enqueue(WindowResized{1024, 768});  // différé, depuis n'importe quel fil
 * bus.Dispatch();                         // délivre la file (fil principal)
 * @endcode
 *
 * Un type d'événement est n'importe quel type copiable/déplaçable : aucune
 * hiérarchie à respecter, aucun identifiant à déclarer.
 *
 * ── Fils d'exécution ────────────────────────────────────────────────────
 * Subscribe/Publish/Dispatch s'appellent depuis un seul fil (en général le
 * fil principal). Enqueue est le seul point d'entrée sûr depuis d'autres
 * fils : l'événement est copié dans une file protégée, puis délivré au
 * prochain Dispatch(). Un événement mis en file PENDANT un Dispatch() est
 * délivré au Dispatch() suivant — un gestionnaire qui ré-enfile ne peut donc
 * pas faire boucler indéfiniment un même Dispatch().
 */
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "signal.hpp"

namespace events {

class EventBus {
public:
	EventBus() = default;
	EventBus(const EventBus &) = delete;
	EventBus &operator=(const EventBus &) = delete;

	/// Abonne `handler` aux événements de type E.
	template <class E> [[nodiscard]] Connection Subscribe(std::function<void(const E &)> handler) {
		return ChannelFor<E>().signal.Connect(std::move(handler));
	}

	/// Délivre `event` immédiatement à tous les abonnés de E.
	template <class E> void Publish(const E &event) {
		auto it = m_channels.find(std::type_index(typeid(E)));
		if (it != m_channels.end())
			static_cast<Channel<E> &>(*it->second).signal.Emit(event);
	}

	/// Met `event` en file ; délivré au prochain Dispatch(). Sûr depuis tout fil.
	template <class E> void Enqueue(E event) {
		std::lock_guard<std::mutex> lock(m_queueMutex);
		m_queue.emplace_back([e = std::move(event)](EventBus &bus) { bus.Publish(e); });
	}

	/// Délivre, dans l'ordre d'arrivée, les événements en file au moment de
	/// l'appel. Retourne le nombre d'événements délivrés.
	size_t Dispatch();

	/// Nombre d'événements en attente dans la file.
	[[nodiscard]] size_t PendingCount() const;

	/// Nombre d'abonnés connectés au type E.
	template <class E> [[nodiscard]] size_t SubscriberCount() const {
		auto it = m_channels.find(std::type_index(typeid(E)));
		return it == m_channels.end() ? 0 : static_cast<const Channel<E> &>(*it->second).signal.SlotCount();
	}

	/// Vide la file sans rien délivrer.
	void ClearQueue();

private:
	struct ChannelBase {
		virtual ~ChannelBase() = default;
	};
	template <class E> struct Channel final : ChannelBase {
		Signal<const E &> signal;
	};

	template <class E> Channel<E> &ChannelFor() {
		auto &slot = m_channels[std::type_index(typeid(E))];
		if (!slot)
			slot = std::make_unique<Channel<E>>();
		return static_cast<Channel<E> &>(*slot);
	}

	std::unordered_map<std::type_index, std::unique_ptr<ChannelBase>> m_channels;
	mutable std::mutex m_queueMutex;
	std::vector<std::function<void(EventBus &)>> m_queue;
};

} // namespace events
