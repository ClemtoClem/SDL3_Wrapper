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
    ~NetContext() {
        if (owns)
            NET_Quit();
    }

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

    [[nodiscard]] static Result<NetContext, Error> Create() {
        NetContext ctx;
        ctx.owns = NET_Init();
        if (!ctx)
            return Err(GetError());
        return Ok(std::move(ctx));
    }
};

// ============================================================================
// IpAddress — reference-counted NET_Address (hostname resolution)
// ============================================================================

class IpAddress : public Wrapper<NET_Address, NET_UnrefAddress> {
public:
    using Wrapper::Wrapper;

    [[nodiscard]] static Result<IpAddress, StringView> Resolve(const String &host) {
        auto *addr = NET_ResolveHostname(host.c_str());
        if (!addr)
            return Err(GetError());
        return Ok(IpAddress(addr));
    }

    /// Block until the hostname is resolved.
    /// timeout_ms <= 0 means wait indefinitely.
    bool Wait(int timeoutMs = -1) {
        if (!m_handle)
            return false;
        return NET_WaitUntilResolved(m_handle, timeoutMs) == NET_SUCCESS;
    }

    [[nodiscard]] String ToString() const {
        if (!m_handle)
            return {};
        const char *s = NET_GetAddressString(m_handle);
        return String(s ? s : "");
    }
};

// ============================================================================
// TcpSocket — RAII stream (TCP) client socket
// ============================================================================

class TcpSocket : public Wrapper<NET_StreamSocket, NET_DestroyStreamSocket> {
public:
    using Wrapper::Wrapper;

    /// Connect to a resolved address on the given port.
    [[nodiscard]] static Result<TcpSocket, StringView> Connect(IpAddress &addr, uint16_t port) {
        auto *s = NET_CreateClient(addr.Get(), port, 0);
        if (!s)
            return Err(GetError());
        return Ok(TcpSocket(s));
    }

    /// Block until connected. Returns true on success.
    bool WaitConnected(int timeoutMs = -1) {
        return m_handle && NET_WaitUntilConnected(m_handle, timeoutMs) == NET_SUCCESS;
    }

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
    [[nodiscard]] static Result<TcpServer, StringView> Listen(uint16_t port, IpAddress *addr = nullptr) {
        auto *s = NET_CreateServer(addr ? addr->Get() : nullptr, port, 0);
        if (!s)
            return Err(GetError());
        return Ok(TcpServer(s));
    }

    /// Non-blocking accept — returns NONE if no client is waiting.
    [[nodiscard]] Option<TcpSocket> Accept() {
        if (!m_handle)
            return NONE;
        NET_StreamSocket *client = nullptr;
        if (!NET_AcceptClient(m_handle, &client) || !client)
            return NONE;
        return Some(TcpSocket(client));
    }
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
    [[nodiscard]] static Result<UdpSocket, StringView> Open(uint16_t localPort = 0, IpAddress *addr = nullptr) {
        auto *s = NET_CreateDatagramSocket(addr ? addr->Get() : nullptr, localPort, 0);
        if (!s)
            return Err(GetError());
        return Ok(UdpSocket(s));
    }

    bool Send(IpAddress &dest, uint16_t port, const void *data, int len) {
        return m_handle && NET_SendDatagram(m_handle, dest.Get(), port, data, len);
    }
    template <typename T> bool Send(IpAddress &dest, uint16_t port, std::span<const T> data) {
        return send(dest, port, data.data(), int(data.size_bytes()));
    }

    /// Non-blocking receive — returns NONE if no datagram is available.
    [[nodiscard]] Option<ReceivedDatagram> Receive() {
        if (!m_handle)
            return NONE;
        NET_Datagram *dgram = nullptr;
        if (!NET_ReceiveDatagram(m_handle, &dgram) || !dgram)
            return NONE;

        ReceivedDatagram result;
        result.port = dgram->port;
        result.data.assign(static_cast<const uint8_t *>(dgram->buf),
                           static_cast<const uint8_t *>(dgram->buf) + dgram->buflen);
        const char *addrStr = dgram->addr ? NET_GetAddressString(dgram->addr) : nullptr;
        result.senderAddr = String(addrStr ? addrStr : "");
        NET_DestroyDatagram(dgram);
        return Some(std::move(result));
    }
};

} // namespace sdl3
