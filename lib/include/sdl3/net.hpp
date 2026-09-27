#pragma once
#include <SDL3_net/SDL_net.h>
#include <span>
#include <vector>

#include "../core/core.hpp"

namespace sdl3 {

// ============================================================================
// NetContext — RAII NET_Init / NET_Quit
// ============================================================================

class NetContext {
    bool owns = false;

public:
    NetContext() = default;
    ~NetContext();

    NetContext(const NetContext &) = delete;
    NetContext &operator=(const NetContext &) = delete;
    NetContext(NetContext &&o) noexcept : owns(o.owns) { o.owns = false; }
    NetContext &operator=(NetContext &&o) noexcept {
        if (this != &o) {
            if (owns)
                NET_Quit();
            owns = o.owns;
            o.owns = false;
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return owns; }

    [[nodiscard]] static Result<NetContext, Error> Create();
};

// ============================================================================
// IpAddress — reference-counted NET_Address (hostname resolution)
// ============================================================================

class IpAddress : public Wrapper<NET_Address, NET_UnrefAddress> {
public:
    using Wrapper::Wrapper;

    [[nodiscard]] static Result<IpAddress, StringView> Resolve(const String &host);

    /// Block until the hostname is resolved.
    /// timeout_ms <= 0 means wait indefinitely.
    bool Wait(int timeoutMs = -1);

    [[nodiscard]] String ToString() const;
};

// ============================================================================
// TcpSocket — RAII stream (TCP) client socket
// ============================================================================

class TcpSocket : public Wrapper<NET_StreamSocket, NET_DestroyStreamSocket> {
public:
    using Wrapper::Wrapper;

    /// Connect to a resolved address on the given port.
    [[nodiscard]] static Result<TcpSocket, StringView> Connect(IpAddress &addr, uint16_t port);

    /// Block until connected. Returns true on success.
    bool WaitConnected(int timeoutMs = -1);

    /// Send raw bytes. Returns true on success.
    bool Send(const void *data, int len) { return m_handle && NET_WriteToStreamSocket(m_handle, data, len); }
    template <typename T> bool Send(std::span<const T> data) { return send(data.data(), int(data.size_bytes())); }

    /// Receive raw bytes. Returns bytes read (0 = no data yet) or -1 on error.
    int Receive(void *data, int len) { return m_handle ? NET_ReadFromStreamSocket(m_handle, data, len) : -1; }
    template <typename T> int Receive(std::span<T> data) { return receive(data.data(), int(data.size_bytes())); }
};

// ============================================================================
// TcpServer — RAII server socket (accepts TCP connections)
// ============================================================================

class TcpServer : public Wrapper<NET_Server, NET_DestroyServer> {
public:
    using Wrapper::Wrapper;

    /// Listen on port (all interfaces if addr is nullptr).
    [[nodiscard]] static Result<TcpServer, StringView> Listen(uint16_t port, IpAddress *addr = nullptr);

    /// Non-blocking accept — returns NONE if no client is waiting.
    [[nodiscard]] Option<TcpSocket> Accept();
};

// ============================================================================
// UdpSocket — RAII datagram (UDP) socket
// ============================================================================

struct ReceivedDatagram {
    String senderAddr;
    uint16_t port = 0;
    std::vector<uint8_t> data;
};

class UdpSocket : public Wrapper<NET_DatagramSocket, NET_DestroyDatagramSocket> {
public:
    using Wrapper::Wrapper;

    /// Open a UDP socket, optionally bound to a local port (0 = any).
    [[nodiscard]] static Result<UdpSocket, StringView> Open(uint16_t localPort = 0, IpAddress *addr = nullptr);

    bool Send(IpAddress &dest, uint16_t port, const void *data, int len);
    template <typename T> bool Send(IpAddress &dest, uint16_t port, std::span<const T> data) {
        return send(dest, port, data.data(), int(data.size_bytes()));
    }

    /// Non-blocking receive — returns NONE if no datagram is available.
    [[nodiscard]] Option<ReceivedDatagram> Receive();
};

} // namespace sdl3
