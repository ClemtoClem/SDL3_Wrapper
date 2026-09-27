// Définitions de sdl3/net.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/net.hpp"

namespace sdl3 {

// ── NetContext ───────────────────────────────────────────────────────────────

NetContext::~NetContext() {
    if (owns)
        NET_Quit();
}

Result<NetContext, Error> NetContext::Create() {
    NetContext ctx;
    ctx.owns = NET_Init();
    if (!ctx)
        return Err(GetError());
    return Ok(std::move(ctx));
}

// ── IpAddress ────────────────────────────────────────────────────────────────

Result<IpAddress, StringView> IpAddress::Resolve(const String &host) {
    auto *addr = NET_ResolveHostname(host.c_str());
    if (!addr)
        return Err(GetError());
    return Ok(IpAddress(addr));
}

bool IpAddress::Wait(int timeoutMs) {
    if (!m_handle)
        return false;
    return NET_WaitUntilResolved(m_handle, timeoutMs) == NET_SUCCESS;
}

String IpAddress::ToString() const {
    if (!m_handle)
        return {};
    const char *s = NET_GetAddressString(m_handle);
    return String(s ? s : "");
}

// ── TcpSocket ────────────────────────────────────────────────────────────────

Result<TcpSocket, StringView> TcpSocket::Connect(IpAddress &addr, uint16_t port) {
    auto *s = NET_CreateClient(addr.Get(), port, 0);
    if (!s)
        return Err(GetError());
    return Ok(TcpSocket(s));
}

bool TcpSocket::WaitConnected(int timeoutMs) {
    return m_handle && NET_WaitUntilConnected(m_handle, timeoutMs) == NET_SUCCESS;
}

// ── TcpServer ────────────────────────────────────────────────────────────────

Result<TcpServer, StringView> TcpServer::Listen(uint16_t port, IpAddress *addr) {
    auto *s = NET_CreateServer(addr ? addr->Get() : nullptr, port, 0);
    if (!s)
        return Err(GetError());
    return Ok(TcpServer(s));
}

Option<TcpSocket> TcpServer::Accept() {
    if (!m_handle)
        return NONE;
    NET_StreamSocket *client = nullptr;
    if (!NET_AcceptClient(m_handle, &client) || !client)
        return NONE;
    return Some(TcpSocket(client));
}

// ── UdpSocket ────────────────────────────────────────────────────────────────

Result<UdpSocket, StringView> UdpSocket::Open(uint16_t localPort, IpAddress *addr) {
    auto *s = NET_CreateDatagramSocket(addr ? addr->Get() : nullptr, localPort, 0);
    if (!s)
        return Err(GetError());
    return Ok(UdpSocket(s));
}

bool UdpSocket::Send(IpAddress &dest, uint16_t port, const void *data, int len) {
    return m_handle && NET_SendDatagram(m_handle, dest.Get(), port, data, len);
}

Option<ReceivedDatagram> UdpSocket::Receive() {
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

} // namespace sdl3
