#pragma once
/**
 * NetBridge — transport TCP pour le multijoueur local, indépendant du module
 * Wifi émulé : `EmulatorSession` relie les deux (`Wifi::onPacketSent` ->
 * `SendPacket`, `PollReceived` -> `Wifi::injectPacket`). Trames
 * `[uint32_t longueur][charge utile]`.
 */
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "sdl3/sdl3.hpp"

#include "report.hpp"

namespace emulator_demo::app {

enum class NetStatus : uint8_t { DISCONNECTED, HOSTING, CONNECTING, CONNECTED };

[[nodiscard]] inline const char *NetStatusText(NetStatus status) {
	switch (status) {
	case NetStatus::HOSTING:
		return "Hôte : en attente d'un joueur…";
	case NetStatus::CONNECTING:
		return "Connexion…";
	case NetStatus::CONNECTED:
		return "Connecté";
	case NetStatus::DISCONNECTED:
		break;
	}
	return "Déconnecté";
}

class NetBridge {
public:
	explicit NetBridge(ThreadTracker *threads = nullptr) : m_threads(threads) {}
	~NetBridge() { Disconnect(); }

	NetBridge(const NetBridge &) = delete;
	NetBridge &operator=(const NetBridge &) = delete;

	/// Écoute `port` ; un fil attend le client jusqu'à Disconnect().
	bool Host(uint16_t port) {
		Disconnect();
		if (!EnsureContext())
			return false;
		auto server = sdl3::TcpServer::Listen(port);
		if (!server.IsOk())
			return false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_server = Some(std::move(server).Unwrap());
		}
		m_status.store(NetStatus::HOSTING, std::memory_order_relaxed);
		m_acceptThread = std::jthread([this](std::stop_token stop) { AcceptLoop(stop); });
		return true;
	}

	/// Résout `address` et bloque jusqu'à la connexion (ou l'échec).
	bool Join(const String &address, uint16_t port) {
		Disconnect();
		if (!EnsureContext())
			return false;
		m_status.store(NetStatus::CONNECTING, std::memory_order_relaxed);
		auto fail = [this] {
			m_status.store(NetStatus::DISCONNECTED, std::memory_order_relaxed);
			return false;
		};
		auto resolved = sdl3::IpAddress::Resolve(address);
		if (!resolved.IsOk())
			return fail();
		sdl3::IpAddress ip = std::move(resolved).Unwrap();
		if (!ip.Wait())
			return fail();
		auto socket = sdl3::TcpSocket::Connect(ip, port);
		if (!socket.IsOk())
			return fail();
		sdl3::TcpSocket connected = std::move(socket).Unwrap();
		if (!connected.WaitConnected())
			return fail();
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_socket = Some(std::move(connected));
		}
		m_status.store(NetStatus::CONNECTED, std::memory_order_relaxed);
		return true;
	}

	void Disconnect() {
		if (m_acceptThread.joinable()) {
			m_acceptThread.request_stop();
			m_acceptThread.join();
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		m_socket = NONE;
		m_server = NONE;
		m_incoming.clear();
		m_receiveBuffer.clear();
		m_status.store(NetStatus::DISCONNECTED, std::memory_order_relaxed);
	}

	/// Encadre et envoie immédiatement si un pair est connecté (rien n'est
	/// mis en file sinon). Appelé depuis le fil d'émulation.
	void SendPacket(std::span<const uint8_t> data) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_socket.IsNone())
			return;
		std::vector<uint8_t> frame(sizeof(uint32_t) + data.size());
		uint32_t length = uint32_t(data.size());
		std::memcpy(frame.data(), &length, sizeof(uint32_t));
		if (!data.empty())
			std::memcpy(frame.data() + sizeof(uint32_t), data.data(), data.size());
		(void)m_socket.Value().Send(frame.data(), int(frame.size()));
	}

	/// Plus ancienne trame complète reçue, s'il y en a une.
	[[nodiscard]] Option<std::vector<uint8_t>> PollReceived() {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_incoming.empty())
			return NONE;
		std::vector<uint8_t> front = std::move(m_incoming.front());
		m_incoming.pop_front();
		return Some(std::move(front));
	}

	/// Lit les octets disponibles et réassemble les trames — une fois par
	/// image depuis le fil principal.
	void Pump() {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_socket.IsNone())
			return;
		uint8_t chunk[4096];
		for (;;) {
			int received = m_socket.Value().Receive(chunk, int(sizeof(chunk)));
			if (received <= 0)
				break;
			m_receiveBuffer.insert(m_receiveBuffer.end(), chunk, chunk + received);
		}
		for (;;) {
			if (m_receiveBuffer.size() < sizeof(uint32_t))
				break;
			uint32_t length = 0;
			std::memcpy(&length, m_receiveBuffer.data(), sizeof(uint32_t));
			if (m_receiveBuffer.size() < sizeof(uint32_t) + length)
				break;
			auto begin = m_receiveBuffer.begin() + long(sizeof(uint32_t));
			m_incoming.emplace_back(begin, begin + long(length));
			m_receiveBuffer.erase(m_receiveBuffer.begin(), begin + long(length));
		}
	}

	[[nodiscard]] NetStatus Status() const { return m_status.load(std::memory_order_relaxed); }

private:
	/// SDL_net n'est initialisé qu'à la première utilisation : la plupart des
	/// sessions n'en ont jamais besoin.
	bool EnsureContext() {
		if (m_context.IsSome())
			return true;
		auto context = sdl3::NetContext::Create();
		if (!context.IsOk())
			return false;
		m_context = Some(std::move(context).Unwrap());
		return true;
	}

	void AcceptLoop(std::stop_token stop) {
		sdl3::ForeignThreadScope sdlThread; // fil std::jthread qui appelle SDL : TLS à libérer
		ThreadTracker::Scope tracked(m_threads);
		while (!stop.stop_requested()) {
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_server.IsSome()) {
					Option<sdl3::TcpSocket> client = m_server.Value().Accept();
					if (client.IsSome()) {
						m_socket = Some(std::move(client).Unwrap());
						m_status.store(NetStatus::CONNECTED, std::memory_order_relaxed);
						return;
					}
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}

	ThreadTracker *m_threads;
	Option<sdl3::NetContext> m_context = NONE;
	Option<sdl3::TcpServer> m_server = NONE;
	Option<sdl3::TcpSocket> m_socket = NONE;
	std::atomic<NetStatus> m_status{NetStatus::DISCONNECTED};
	std::mutex m_mutex;
	std::deque<std::vector<uint8_t>> m_incoming;
	std::vector<uint8_t> m_receiveBuffer;
	std::jthread m_acceptThread;
};

} // namespace emulator_demo::app
