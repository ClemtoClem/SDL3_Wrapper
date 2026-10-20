#pragma once
/**
 * events::Signal — signal typé à connexions multiples (patron observateur).
 *
 * @code{.cpp}
 * events::Signal<int, int> resized;
 * events::ScopedConnection c = resized.Connect([](int w, int h) { ... });
 * resized.Emit(800, 600);   // appelle chaque slot connecté, dans l'ordre
 * @endcode
 *
 * ── Durée de vie ────────────────────────────────────────────────────────
 * Connect() rend une Connection : un simple jeton qui permet de se
 * déconnecter, valide même si le signal a déjà été détruit (le jeton ne
 * référence qu'un état partagé, jamais le signal). ScopedConnection se
 * déconnecte d'elle-même à sa destruction : c'est la forme à préférer pour
 * un objet qui écoute un signal plus durable que lui.
 *
 * ── Réentrance ──────────────────────────────────────────────────────────
 * Un slot peut, pendant Emit(), se déconnecter, déconnecter un autre slot
 * ou en connecter un nouveau : Emit() parcourt un instantané de la liste,
 * saute les slots déconnectés entre-temps, et un slot connecté pendant
 * l'émission ne sera appelé qu'à l'émission suivante.
 *
 * ── Fils d'exécution ────────────────────────────────────────────────────
 * Un Signal n'est PAS synchronisé : il s'utilise depuis un seul fil. Pour
 * publier depuis un autre fil, passer par EventBus::Enqueue (event_bus.hpp).
 */
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace events {

namespace detail {
struct SlotState {
	std::atomic<bool> connected{true};
};
} // namespace detail

/// Jeton de connexion : copiable, sans effet à la destruction.
class Connection {
public:
	Connection() = default;
	explicit Connection(std::weak_ptr<detail::SlotState> state) noexcept : m_state(std::move(state)) {}

	/// Déconnecte le slot (sans effet s'il l'est déjà ou si le signal n'existe plus).
	void Disconnect() noexcept;
	/// Vrai tant que le slot est connecté à un signal vivant.
	[[nodiscard]] bool IsConnected() const noexcept;

private:
	std::weak_ptr<detail::SlotState> m_state;
};

/// Connexion RAII : déconnecte le slot à sa destruction. Déplaçable, non copiable.
class ScopedConnection {
public:
	ScopedConnection() = default;
	ScopedConnection(Connection c) noexcept : m_connection(std::move(c)) {}
	~ScopedConnection() { m_connection.Disconnect(); }

	ScopedConnection(const ScopedConnection &) = delete;
	ScopedConnection &operator=(const ScopedConnection &) = delete;
	ScopedConnection(ScopedConnection &&o) noexcept : m_connection(std::exchange(o.m_connection, {})) {}
	ScopedConnection &operator=(ScopedConnection &&o) noexcept {
		if (this != &o) {
			m_connection.Disconnect();
			m_connection = std::exchange(o.m_connection, {});
		}
		return *this;
	}

	void Disconnect() noexcept { m_connection.Disconnect(); }
	/// Abandonne la connexion sans déconnecter le slot (il vit alors aussi longtemps que le signal).
	Connection Release() noexcept { return std::exchange(m_connection, {}); }
	[[nodiscard]] bool IsConnected() const noexcept { return m_connection.IsConnected(); }

private:
	Connection m_connection;
};

template <class... Args> class Signal {
public:
	using Slot = std::function<void(Args...)>;

	Signal() = default;
	Signal(const Signal &) = delete;
	Signal &operator=(const Signal &) = delete;
	Signal(Signal &&) noexcept = default;
	Signal &operator=(Signal &&) noexcept = default;
	~Signal() { DisconnectAll(); }

	/// Connecte `slot` ; un slot vide est ignoré (connexion déjà rompue).
	[[nodiscard]] Connection Connect(Slot slot) {
		if (!slot)
			return {};
		auto state = std::make_shared<detail::SlotState>();
		m_slots.push_back({state, std::make_shared<Slot>(std::move(slot))});
		return Connection(state);
	}

	/// Appelle chaque slot connecté, dans l'ordre de connexion.
	void Emit(Args... args) {
		const std::vector<Entry> snapshot = m_slots;
		++m_emitting;
		for (const Entry &e : snapshot)
			if (e.state->connected.load(std::memory_order_acquire))
				(*e.slot)(args...);
		if (--m_emitting == 0)
			Compact();
	}
	void operator()(Args... args) { Emit(std::forward<Args>(args)...); }

	/// Nombre de slots encore connectés.
	[[nodiscard]] size_t SlotCount() const noexcept {
		size_t n = 0;
		for (const Entry &e : m_slots)
			n += e.state->connected.load(std::memory_order_acquire) ? 1 : 0;
		return n;
	}
	[[nodiscard]] bool IsEmpty() const noexcept { return SlotCount() == 0; }

	void DisconnectAll() noexcept {
		for (Entry &e : m_slots)
			e.state->connected.store(false, std::memory_order_release);
		if (m_emitting == 0)
			m_slots.clear();
	}

private:
	struct Entry {
		std::shared_ptr<detail::SlotState> state;
		std::shared_ptr<Slot> slot; // partagé : l'instantané d'Emit ne copie pas les std::function
	};

	void Compact() {
		std::erase_if(m_slots, [](const Entry &e) { return !e.state->connected.load(std::memory_order_acquire); });
	}

	std::vector<Entry> m_slots;
	int m_emitting = 0;
};

} // namespace events
