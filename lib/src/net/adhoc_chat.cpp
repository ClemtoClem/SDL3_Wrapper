// Définitions de net/adhoc_chat.hpp
#include "net/adhoc_chat.hpp"

#include "data/archive/archive_crc.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <random>
#include <system_error>

namespace net::chat {

namespace fs = std::filesystem;

namespace {

constexpr uint8_t FRAME_MAGIC_0 = 'A';
constexpr uint8_t FRAME_MAGIC_1 = 'H';
constexpr size_t FRAME_HEADER = 7; // magie (2) + type (1) + longueur (4)
constexpr uint8_t BEACON_MAGIC[4] = {'A', 'H', 'C', 'B'};
constexpr uint8_t BEACON_VERSION = 1;
constexpr size_t MAX_NICKNAME = 48;
constexpr size_t MAX_MESSAGES = 1000;
constexpr int SEND_WINDOW = 256 * 1024; // octets en attente d'envoi au-delà desquels on patiente
constexpr int CHUNKS_PER_POLL = 32;		// borne le travail d'une image
constexpr uint64_t RETRY_DELAY_MS = 3000;
constexpr uint64_t ADDRESS_REFRESH_MS = 3000;

/// Sens d'une trame FILE_CANCEL.
constexpr uint8_t CANCEL_BY_SENDER = 0;	  ///< l'expéditeur abandonne son envoi
constexpr uint8_t CANCEL_BY_RECEIVER = 1; ///< le destinataire refuse ou abandonne

[[nodiscard]] uint64_t RandomId() {
	std::random_device rd;
	std::mt19937_64 gen((uint64_t(rd()) << 32) ^ rd());
	uint64_t id = 0;
	while (id == 0)
		id = gen();
	return id;
}

[[nodiscard]] String Truncated(const String& s, size_t maxBytes) {
	if (s.size() <= maxBytes)
		return s;
	size_t n = maxBytes;
	// ne pas couper un caractère UTF-8 en deux
	while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
		--n;
	return s.Substr(0, n);
}

[[nodiscard]] bool IsLoopback(const String& addr) {
	return addr.StartsWith("127.") || addr == "::1";
}

[[nodiscard]] String JoinPath(const String& dir, const String& name) {
	return String((fs::path(dir.CStr()) / fs::path(name.CStr())).string());
}

} // namespace

// ── Trames ──────────────────────────────────────────────────────────────────

std::vector<uint8_t> EncodeFrame(FrameType type, std::span<const uint8_t> payload) {
	ByteWriter w;
	w.U8(FRAME_MAGIC_0)
		.U8(FRAME_MAGIC_1)
		.U8(uint8_t(type))
		.U32(uint32_t(payload.size()))
		.Bytes(payload);
	return std::move(w.Data());
}

void FrameDecoder::Feed(std::span<const uint8_t> bytes) {
	// compacter de temps en temps plutôt qu'à chaque trame
	if (m_read > 0 && m_read >= m_buffer.size() / 2) {
		m_buffer.erase(m_buffer.begin(), m_buffer.begin() + std::ptrdiff_t(m_read));
		m_read = 0;
	}
	m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
}

Option<Frame> FrameDecoder::Next() {
	if (m_corrupted || m_buffer.size() - m_read < FRAME_HEADER)
		return NONE;
	const uint8_t* h = m_buffer.data() + m_read;
	uint8_t type = h[2];
	uint32_t len =
		uint32_t(h[3]) | uint32_t(h[4]) << 8 | uint32_t(h[5]) << 16 | uint32_t(h[6]) << 24;
	if (h[0] != FRAME_MAGIC_0 || h[1] != FRAME_MAGIC_1 || type < uint8_t(FrameType::HELLO) ||
		type > uint8_t(FrameType::BYE) || len > MAX_FRAME_PAYLOAD) {
		m_corrupted = true;
		return NONE;
	}
	if (m_buffer.size() - m_read < FRAME_HEADER + len)
		return NONE;
	Frame f;
	f.type = FrameType(type);
	f.payload.assign(h + FRAME_HEADER, h + FRAME_HEADER + len);
	m_read += FRAME_HEADER + len;
	if (m_read == m_buffer.size()) {
		m_buffer.clear();
		m_read = 0;
	}
	return Some(std::move(f));
}

// ── Octets ──────────────────────────────────────────────────────────────────

ByteWriter& ByteWriter::U8(uint8_t v) {
	m_data.push_back(v);
	return *this;
}
ByteWriter& ByteWriter::U16(uint16_t v) {
	return U8(uint8_t(v)).U8(uint8_t(v >> 8));
}
ByteWriter& ByteWriter::U32(uint32_t v) {
	return U16(uint16_t(v)).U16(uint16_t(v >> 16));
}
ByteWriter& ByteWriter::U64(uint64_t v) {
	return U32(uint32_t(v)).U32(uint32_t(v >> 32));
}
ByteWriter& ByteWriter::Bytes(std::span<const uint8_t> bytes) {
	m_data.insert(m_data.end(), bytes.begin(), bytes.end());
	return *this;
}

Option<uint8_t> ByteReader::U8() {
	if (m_pos >= m_data.size())
		return NONE;
	return Some(m_data[m_pos++]);
}
Option<uint16_t> ByteReader::U16() {
	if (m_data.size() - m_pos < 2)
		return NONE;
	uint16_t v = uint16_t(m_data[m_pos] | m_data[m_pos + 1] << 8);
	m_pos += 2;
	return Some(v);
}
Option<uint32_t> ByteReader::U32() {
	auto lo = U16();
	if (!lo)
		return NONE;
	auto hi = U16();
	if (!hi)
		return NONE;
	return Some(uint32_t(*lo) | uint32_t(*hi) << 16);
}
Option<uint64_t> ByteReader::U64() {
	auto lo = U32();
	if (!lo)
		return NONE;
	auto hi = U32();
	if (!hi)
		return NONE;
	return Some(uint64_t(*lo) | uint64_t(*hi) << 32);
}
std::span<const uint8_t> ByteReader::Rest() noexcept {
	auto rest = m_data.subspan(std::min(m_pos, m_data.size()));
	m_pos = m_data.size();
	return rest;
}
String ByteReader::RestText() {
	auto rest = Rest();
	return String(reinterpret_cast<const char*>(rest.data()), rest.size());
}

// ── Balises ─────────────────────────────────────────────────────────────────

std::vector<uint8_t> EncodeBeacon(const Beacon& beacon) {
	String nick = Truncated(beacon.nickname, MAX_NICKNAME);
	ByteWriter w;
	w.Bytes(BEACON_MAGIC)
		.U8(BEACON_VERSION)
		.U8(uint8_t(beacon.kind))
		.U64(beacon.nodeId)
		.U16(beacon.tcpPort);
	w.U8(uint8_t(nick.size())).Text(nick);
	return std::move(w.Data());
}

Option<Beacon> DecodeBeacon(std::span<const uint8_t> data) {
	if (data.size() < 4 || !std::equal(data.begin(), data.begin() + 4, BEACON_MAGIC))
		return NONE;
	ByteReader r(data.subspan(4));
	auto version = r.U8();
	auto kind = r.U8();
	auto id = r.U64();
	auto port = r.U16();
	auto len = r.U8();
	if (!version || *version != BEACON_VERSION || !kind || *kind < 1 || *kind > 3 || !id ||
		*id == 0 || !port || !len)
		return NONE;
	auto rest = r.Rest();
	if (rest.size() < *len)
		return NONE;
	Beacon b;
	b.kind = BeaconKind(*kind);
	b.nodeId = *id;
	b.tcpPort = *port;
	b.nickname = String(reinterpret_cast<const char*>(rest.data()), *len);
	return Some(std::move(b));
}

Option<String> SubnetBroadcast(const String& ipv4) {
	auto parts = ipv4.Split('.');
	if (parts.size() != 4)
		return NONE;
	for (auto& p : parts)
		if (p.IsEmpty() || !p.IsNumeric() || p.ToInt32() < 0 || p.ToInt32() > 255)
			return NONE;
	if (parts[0] == "127" || parts[0] == "0")
		return NONE;
	if (parts[0] == "169" && parts[1] == "254")
		return Some(String("169.254.255.255"));
	return Some(parts[0] + "." + parts[1] + "." + parts[2] + ".255");
}

String SanitizeFileName(const String& name) {
	String base = name.Replace('\\', '/');
	size_t slash = base.Rfind('/');
	if (slash != String::NPOS)
		base = base.Substr(slash + 1);
	String clean;
	for (char c : base) {
		auto u = static_cast<unsigned char>(c);
		if (u < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
			c == '|')
			clean.Append('_');
		else
			clean.Append(c);
	}
	clean = clean.Trim();
	while (clean.StartsWith("."))
		clean = clean.Substr(1); // ni « .. », ni fichier caché imposé
	if (clean.IsEmpty())
		return "fichier";
	return Truncated(clean, 200);
}

String FreePath(const String& dir, const String& name) {
	String candidate = JoinPath(dir, name);
	std::error_code ec;
	if (!fs::exists(candidate.CStr(), ec))
		return candidate;
	size_t dot = name.Rfind('.');
	String stem = (dot == String::NPOS || dot == 0) ? name : name.Substr(0, dot);
	String ext = (dot == String::NPOS || dot == 0) ? String() : name.Substr(dot);
	for (int i = 2;; ++i) {
		candidate = JoinPath(dir, String::Format("%s (%d)%s", stem.CStr(), i, ext.CStr()));
		if (!fs::exists(candidate.CStr(), ec))
			return candidate;
	}
}

// ── Informations ────────────────────────────────────────────────────────────

String PeerInfo::Label() const {
	String name = nickname.IsEmpty() ? String("?") : nickname;
	return String::Format("%s (%s:%u)", name.CStr(), address.CStr(), unsigned(tcpPort));
}

double TransferInfo::Rate(uint64_t nowMs) const noexcept {
	uint64_t end = endMs ? endMs : nowMs;
	if (end <= startMs)
		return 0.0;
	return double(done) * 1000.0 / double(end - startMs);
}

const char* TransferStateName(TransferState state) noexcept {
	switch (state) {
	case TransferState::RUNNING:
		return "en cours";
	case TransferState::DONE:
		return "terminé";
	case TransferState::FAILED:
		return "échec";
	case TransferState::CANCELLED:
		return "annulé";
	}
	return "?";
}

// ── État interne ────────────────────────────────────────────────────────────

struct ChatNode::Peer {
	PeerInfo info;
	sdl3::IpAddress addr;
	sdl3::TcpSocket sock; ///< notre connexion sortante (vide : aucune)
	bool helloSent = false;
	uint64_t retryAt = 0;
	std::vector<std::vector<uint8_t>> pending; ///< trames en attente de connexion
	bool isIpv4 = false;
};

struct ChatNode::IncomingFile {
	uint32_t remoteId = 0;
	uint32_t transferId = 0;
	std::FILE* fp = nullptr;
	String partPath;
	String name;
	uint64_t size = 0;
	uint64_t written = 0;
	uint32_t crc = 0;
};

struct ChatNode::Incoming {
	sdl3::TcpSocket sock;
	sdl3::IpAddress remote;
	String address;
	FrameDecoder decoder;
	uint64_t peerId = 0; ///< connu après HELLO
	std::vector<IncomingFile> files;
	bool closed = false;
};

struct ChatNode::OutgoingFile {
	uint32_t transferId = 0;
	uint64_t peerId = 0;
	std::FILE* fp = nullptr;
	uint64_t size = 0;
	uint64_t sent = 0;
	uint32_t crc = 0;
	bool begun = false;
	bool finished = false;
};

// ── Cycle de vie ────────────────────────────────────────────────────────────

ChatNode::ChatNode(NodeConfig config) : m_config(std::move(config)), m_id(RandomId()) {}

Result<std::unique_ptr<ChatNode>, String> ChatNode::Start(NodeConfig config) {
	config.nickname = Truncated(config.nickname.Trim(), MAX_NICKNAME);
	if (config.nickname.IsEmpty())
		config.nickname = "anonyme";
	if (config.downloadDir.IsEmpty()) {
		const char* dl = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
		config.downloadDir = dl ? JoinPath(dl, "adhoc_chat") : String("adhoc_chat_recus");
	}
	std::error_code ec;
	fs::create_directories(config.downloadDir.CStr(), ec);
	if (ec)
		return Err(String::Format("dossier de réception %s : %s", config.downloadDir.CStr(),
								  ec.message().c_str()));

	std::unique_ptr<ChatNode> node(new ChatNode(std::move(config)));
	if (node->m_config.broadcast) {
		auto udp = sdl3::UdpSocket::Open(node->m_config.discoveryPort,
										 sdl3::UdpOptions{.allowBroadcast = true});
		if (!udp)
			return Err(String::Format("port de découverte UDP %u : %s",
									  unsigned(node->m_config.discoveryPort),
									  String(udp.Error()).CStr()));
		node->m_udp = std::move(udp.Value());
	}
	String lastError;
	for (int i = 0; i < 32 && !node->m_server.Get(); ++i) {
		uint16_t port = uint16_t(node->m_config.tcpPort + i);
		auto server = sdl3::TcpServer::Listen(port);
		if (server) {
			node->m_server = std::move(server.Value());
			node->m_tcpPort = port;
		} else {
			lastError = String(server.Error());
		}
	}
	if (!node->m_server.Get())
		return Err(String::Format("aucun port TCP libre à partir de %u : %s",
								  unsigned(node->m_config.tcpPort), lastError.CStr()));
	return Ok(std::move(node));
}

ChatNode::~ChatNode() {
	// Prévenir les pairs : ils nous retirent sans attendre l'expiration.
	SendBeacon(BeaconKind::BYE);
	for (auto& p : m_peers) {
		if (p->sock.Get() && p->helloSent) {
			auto bye = EncodeFrame(FrameType::BYE, {});
			p->sock.Send(bye.data(), int(bye.size()));
			p->sock.WaitDrained(100);
		}
	}
	for (auto& f : m_fileSends)
		if (f->fp)
			std::fclose(f->fp);
	for (auto& in : m_incoming) {
		for (auto& f : in->files) {
			if (f.fp)
				std::fclose(f.fp);
			std::error_code ec;
			fs::remove(f.partPath.CStr(), ec);
		}
	}
}

// ── Boucle ──────────────────────────────────────────────────────────────────

void ChatNode::Poll(uint64_t nowMs) {
	m_now = nowMs;
	if (!m_started || nowMs - m_lastAddressRefresh >= ADDRESS_REFRESH_MS)
		RefreshLocalAddresses(nowMs);
	if (!m_started || nowMs - m_lastAnnounce >= m_config.announceIntervalMs) {
		SendBeacon(m_started ? BeaconKind::ANNOUNCE : BeaconKind::PROBE);
		m_lastAnnounce = nowMs;
	}
	m_started = true;
	PollDiscovery(nowMs);
	PollAccept(nowMs);
	PollIncoming(nowMs);
	PollOutgoing(nowMs);
	PollFileSends(nowMs);
	ExpirePeers(nowMs);
}

void ChatNode::RefreshLocalAddresses(uint64_t now) {
	m_lastAddressRefresh = now;
	std::vector<String> addresses;
	std::vector<String> broadcasts;
	for (auto& a : sdl3::IpAddress::LocalAddresses()) {
		String s = a.ToString();
		if (s.IsEmpty() || IsLoopback(s))
			continue;
		addresses.push_back(s);
		if (auto b = SubnetBroadcast(s);
			b && std::find(broadcasts.begin(), broadcasts.end(), *b) == broadcasts.end())
			broadcasts.push_back(*b);
	}
	if (addresses != m_localAddresses) {
		m_localAddresses = std::move(addresses);
		Touch();
	}
	if (broadcasts != m_broadcastNames) {
		m_broadcastNames = broadcasts;
		m_broadcastTargets.clear();
		for (auto& b : broadcasts)
			if (auto addr = sdl3::IpAddress::Resolve(b))
				m_broadcastTargets.push_back(std::move(addr.Value()));
	}
}

void ChatNode::SendBeacon(BeaconKind kind) {
	if (!m_udp.Get())
		return;
	auto bytes = EncodeBeacon({kind, m_id, m_tcpPort, m_config.nickname});
	m_udp.Broadcast(m_config.discoveryPort, bytes.data(), int(bytes.size()));
	for (auto& target : m_broadcastTargets)
		if (target.Status() == NET_SUCCESS)
			m_udp.Send(target, m_config.discoveryPort, bytes.data(), int(bytes.size()));
}

void ChatNode::Probe() {
	SendBeacon(BeaconKind::PROBE);
	m_lastAnnounce = m_now;
}

void ChatNode::PollDiscovery(uint64_t now) {
	if (!m_udp.Get())
		return;
	bool answer = false;
	for (int guard = 0; guard < 256; ++guard) {
		auto dgram = m_udp.Receive();
		if (!dgram)
			break;
		auto beacon = DecodeBeacon(dgram->data);
		if (!beacon || beacon->nodeId == m_id)
			continue; // autre programme, ou notre propre diffusion revenue
		if (beacon->kind == BeaconKind::BYE) {
			if (Peer* p = FindPeer(beacon->nodeId)) {
				System(p->info.nickname + " a quitté le réseau", now);
				FailFileSends(*p, "le pair est parti", now);
				m_peers.erase(std::find_if(m_peers.begin(), m_peers.end(),
										   [&](auto& q) { return q.get() == p; }));
				Touch();
			}
			continue;
		}
		TouchPeer(beacon->nodeId, beacon->nickname, std::move(dgram->sender), beacon->tcpPort, now);
		answer = answer || beacon->kind == BeaconKind::PROBE;
	}
	if (answer) // une seule réponse même si plusieurs sondes sont arrivées
		SendBeacon(BeaconKind::ANNOUNCE);
}

void ChatNode::PollAccept(uint64_t now) {
	(void)now;
	for (int guard = 0; guard < 64; ++guard) {
		auto client = m_server.Accept();
		if (!client)
			break;
		auto in = std::make_unique<Incoming>();
		in->sock = std::move(*client);
		in->remote = in->sock.RemoteAddress();
		in->address = in->remote.ToString();
		m_incoming.push_back(std::move(in));
	}
}

void ChatNode::PollIncoming(uint64_t now) {
	uint8_t buf[64 * 1024];
	for (auto& inPtr : m_incoming) {
		Incoming& in = *inPtr;
		if (in.closed)
			continue;
		for (int guard = 0; guard < 64; ++guard) {
			int n = in.sock.Receive(buf, int(sizeof buf));
			if (n < 0) {
				CloseIncoming(in, "connexion fermée", now);
				break;
			}
			if (n == 0)
				break;
			in.decoder.Feed({buf, size_t(n)});
			while (auto frame = in.decoder.Next()) {
				HandleFrame(in, *frame, now);
				if (in.closed)
					break;
			}
			if (in.closed)
				break;
			if (in.decoder.Corrupted()) {
				CloseIncoming(in, "flux invalide", now);
				break;
			}
		}
	}
	std::erase_if(m_incoming, [](auto& in) { return in->closed; });
}

void ChatNode::PollOutgoing(uint64_t now) {
	for (auto& pPtr : m_peers) {
		Peer& p = *pPtr;
		if (!p.sock.Get()) {
			if (now < p.retryAt || p.addr.Status() != NET_SUCCESS || p.info.tcpPort == 0)
				continue;
			auto sock = sdl3::TcpSocket::Connect(p.addr, p.info.tcpPort);
			if (!sock) {
				DropConnection(p, String(sock.Error()), now);
				continue;
			}
			p.sock = std::move(sock.Value());
			p.info.status = "connexion…";
			Touch();
		}
		switch (p.sock.Status()) {
		case NET_WAITING:
			break;
		case NET_FAILURE:
			DropConnection(p, String(SDL_GetError()), now);
			break;
		case NET_SUCCESS:
			if (!p.helloSent) {
				auto hello = EncodeFrame(FrameType::HELLO, HelloPayload());
				p.helloSent = p.sock.Send(hello.data(), int(hello.size()));
				for (auto& frame : p.pending)
					p.helloSent = p.helloSent && p.sock.Send(frame.data(), int(frame.size()));
				p.pending.clear();
				if (!p.helloSent) {
					DropConnection(p, "écriture impossible", now);
					break;
				}
				p.info.connected = true;
				p.info.status = "connecté";
				Touch();
			} else if (p.sock.PendingWrites() < 0) {
				DropConnection(p, "connexion perdue", now);
			}
			break;
		}
	}
}

void ChatNode::PollFileSends(uint64_t now) {
	std::vector<uint8_t> chunk(FILE_CHUNK_SIZE);
	for (auto& fPtr : m_fileSends) {
		OutgoingFile& f = *fPtr;
		if (f.finished)
			continue;
		TransferInfo* t = FindTransfer(f.transferId);
		Peer* p = FindPeer(f.peerId);
		if (!t || !p) {
			if (t) {
				t->state = TransferState::FAILED;
				t->error = "pair disparu";
				t->endMs = now;
			}
			f.finished = true;
			Touch();
			continue;
		}
		if (!p->info.connected || !p->sock.Get())
			continue; // attendre la connexion
		if (!f.begun) {
			ByteWriter w;
			w.U32(f.transferId).U64(f.size).Text(t->fileName);
			if (!SendFrame(*p, FrameType::FILE_BEGIN, w.Data()))
				continue;
			f.begun = true;
		}
		for (int i = 0; i < CHUNKS_PER_POLL && !f.finished; ++i) {
			int pendingBytes = p->sock.PendingWrites();
			if (pendingBytes < 0 || pendingBytes > SEND_WINDOW)
				break;
			size_t n = std::fread(chunk.data(), 1, chunk.size(), f.fp);
			if (n > 0) {
				f.crc = data::archive::Crc32({chunk.data(), n}, f.crc);
				ByteWriter w;
				w.U32(f.transferId).Bytes({chunk.data(), n});
				if (!SendFrame(*p, FrameType::FILE_CHUNK, w.Data()))
					break;
				f.sent += n;
				t->done = f.sent;
				Touch();
			}
			if (n < chunk.size()) {
				bool readError = std::ferror(f.fp) != 0 || f.sent != f.size;
				if (readError) {
					ByteWriter w;
					w.U32(f.transferId).U8(CANCEL_BY_SENDER);
					SendFrame(*p, FrameType::FILE_CANCEL, w.Data());
					t->state = TransferState::FAILED;
					t->error = "lecture du fichier interrompue";
				} else {
					ByteWriter w;
					w.U32(f.transferId).U32(f.crc);
					SendFrame(*p, FrameType::FILE_END, w.Data());
					t->state = TransferState::DONE;
				}
				t->endMs = now;
				f.finished = true;
				Touch();
			}
		}
	}
	for (auto& f : m_fileSends)
		if (f->finished && f->fp) {
			std::fclose(f->fp);
			f->fp = nullptr;
		}
	std::erase_if(m_fileSends, [](auto& f) { return f->finished; });
}

void ChatNode::ExpirePeers(uint64_t now) {
	for (auto it = m_peers.begin(); it != m_peers.end();) {
		Peer& p = **it;
		bool linked = std::any_of(m_incoming.begin(), m_incoming.end(), [&](auto& in) {
			return p.info.id != 0 && in->peerId == p.info.id;
		});
		if (p.info.manual || linked || now - p.info.lastSeenMs < m_config.peerTimeoutMs) {
			++it;
			continue;
		}
		System(p.info.nickname + " ne répond plus", now);
		FailFileSends(p, "le pair ne répond plus", now);
		it = m_peers.erase(it);
		Touch();
	}
}

// ── Trames reçues ───────────────────────────────────────────────────────────

void ChatNode::HandleFrame(Incoming& in, Frame& frame, uint64_t now) {
	ByteReader r(frame.payload);
	if (frame.type == FrameType::HELLO) {
		auto id = r.U64();
		auto port = r.U16();
		if (id && *id == m_id) {
			// Un pair manuel désignait ce programme lui-même (sa propre
			// adresse et son propre port) : il n'a rien à faire dans la liste.
			auto self = [&](auto& p) {
				bool local = IsLoopback(p->info.address) ||
							 std::find(m_localAddresses.begin(), m_localAddresses.end(),
									   p->info.address) != m_localAddresses.end();
				return p->info.manual && p->info.id == 0 && p->info.tcpPort == m_tcpPort && local;
			};
			if (std::erase_if(m_peers, self) > 0)
				System("Pair manuel retiré : c'est l'adresse de ce programme lui-même", now);
			CloseIncoming(in, "connexion à soi-même", now);
			return;
		}
		if (!id || !port || *id == 0) {
			CloseIncoming(in, "HELLO invalide", now);
			return;
		}
		in.peerId = *id;
		TouchPeer(*id, Truncated(r.RestText(), MAX_NICKNAME), in.remote.Clone(), *port, now);
		return;
	}
	if (in.peerId == 0) {
		CloseIncoming(in, "trame avant HELLO", now);
		return;
	}
	Peer* peer = FindPeer(in.peerId);
	String author = peer ? peer->info.nickname : String("?");
	if (peer)
		peer->info.lastSeenMs = now;

	switch (frame.type) {
	case FrameType::CHAT: {
		ChatMessage m;
		m.timeMs = now;
		m.peerId = in.peerId;
		m.author = author;
		m.text = r.RestText();
		m_messages.push_back(std::move(m));
		if (m_messages.size() > MAX_MESSAGES)
			m_messages.erase(m_messages.begin());
		Touch();
		break;
	}
	case FrameType::FILE_BEGIN: {
		auto id = r.U32();
		auto size = r.U64();
		if (!id || !size)
			break;
		IncomingFile f;
		f.remoteId = *id;
		f.size = *size;
		f.name = SanitizeFileName(r.RestText());
		f.partPath = FreePath(m_config.downloadDir, f.name + ".part");
		f.transferId = m_nextTransfer++;
		TransferInfo t;
		t.id = f.transferId;
		t.outgoing = false;
		t.peerId = in.peerId;
		t.peerName = author;
		t.fileName = f.name;
		t.path = f.partPath;
		t.size = f.size;
		t.startMs = now;
		f.fp = std::fopen(f.partPath.CStr(), "wb");
		if (!f.fp) {
			t.state = TransferState::FAILED;
			t.error = "impossible d'écrire " + f.partPath;
			t.endMs = now;
			if (peer) {
				ByteWriter w;
				w.U32(f.remoteId).U8(CANCEL_BY_RECEIVER);
				SendFrame(*peer, FrameType::FILE_CANCEL, w.Data());
			}
		} else {
			in.files.push_back(std::move(f));
		}
		m_transfers.push_back(std::move(t));
		Touch();
		break;
	}
	case FrameType::FILE_CHUNK: {
		auto id = r.U32();
		auto it = std::find_if(in.files.begin(), in.files.end(),
							   [&](auto& f) { return id && f.remoteId == *id; });
		if (it == in.files.end())
			break; // fichier refusé ou annulé : les morceaux en vol sont ignorés
		auto bytes = r.Rest();
		TransferInfo* t = FindTransfer(it->transferId);
		bool ok = it->written + bytes.size() <= it->size &&
				  std::fwrite(bytes.data(), 1, bytes.size(), it->fp) == bytes.size();
		if (!ok) {
			std::fclose(it->fp);
			std::error_code ec;
			fs::remove(it->partPath.CStr(), ec);
			if (t) {
				t->state = TransferState::FAILED;
				t->error = it->written + bytes.size() > it->size ? "taille dépassée"
																 : "écriture impossible";
				t->endMs = now;
			}
			if (peer) {
				ByteWriter w;
				w.U32(it->remoteId).U8(CANCEL_BY_RECEIVER);
				SendFrame(*peer, FrameType::FILE_CANCEL, w.Data());
			}
			in.files.erase(it);
		} else {
			it->crc = data::archive::Crc32(bytes, it->crc);
			it->written += bytes.size();
			if (t)
				t->done = it->written;
		}
		Touch();
		break;
	}
	case FrameType::FILE_END: {
		auto id = r.U32();
		auto crc = r.U32();
		auto it = std::find_if(in.files.begin(), in.files.end(),
							   [&](auto& f) { return id && f.remoteId == *id; });
		if (it == in.files.end() || !crc)
			break;
		std::fclose(it->fp);
		it->fp = nullptr;
		TransferInfo* t = FindTransfer(it->transferId);
		std::error_code ec;
		if (it->written != it->size || *crc != it->crc) {
			fs::remove(it->partPath.CStr(), ec);
			if (t) {
				t->state = TransferState::FAILED;
				t->error = it->written != it->size ? "fichier incomplet"
												   : "CRC-32 différent : fichier corrompu";
			}
		} else {
			String finalPath = FreePath(m_config.downloadDir, it->name);
			fs::rename(it->partPath.CStr(), finalPath.CStr(), ec);
			if (t) {
				t->state = ec ? TransferState::FAILED : TransferState::DONE;
				t->path = ec ? it->partPath : finalPath;
				t->error = ec ? String(ec.message()) : String();
			}
			if (!ec)
				System(String::Format("Fichier reçu de %s : %s", author.CStr(), finalPath.CStr()),
					   now);
		}
		if (t)
			t->endMs = now;
		in.files.erase(it);
		Touch();
		break;
	}
	case FrameType::FILE_CANCEL: {
		auto id = r.U32();
		auto by = r.U8();
		if (!id || !by)
			break;
		if (*by == CANCEL_BY_SENDER) {
			auto it = std::find_if(in.files.begin(), in.files.end(),
								   [&](auto& f) { return f.remoteId == *id; });
			if (it == in.files.end())
				break;
			std::fclose(it->fp);
			std::error_code ec;
			fs::remove(it->partPath.CStr(), ec);
			if (TransferInfo* t = FindTransfer(it->transferId)) {
				t->state = TransferState::CANCELLED;
				t->error = "annulé par l'expéditeur";
				t->endMs = now;
			}
			in.files.erase(it);
		} else {
			// le destinataire refuse un fichier que nous lui envoyons
			for (auto& f : m_fileSends) {
				if (f->transferId != *id || f->peerId != in.peerId || f->finished)
					continue;
				f->finished = true;
				if (TransferInfo* t = FindTransfer(f->transferId)) {
					t->state = TransferState::CANCELLED;
					t->error = "refusé par le destinataire";
					t->endMs = now;
				}
			}
		}
		Touch();
		break;
	}
	case FrameType::BYE: {
		if (Peer* p = FindPeer(in.peerId)) {
			System(p->info.nickname + " a quitté le réseau", now);
			FailFileSends(*p, "le pair est parti", now);
			std::erase_if(m_peers, [&](auto& q) { return q.get() == p; });
		}
		CloseIncoming(in, "le pair est parti", now);
		Touch();
		break;
	}
	case FrameType::HELLO:
		break;
	}
}

void ChatNode::CloseIncoming(Incoming& in, const String& reason, uint64_t now) {
	for (auto& f : in.files) {
		if (f.fp)
			std::fclose(f.fp);
		std::error_code ec;
		fs::remove(f.partPath.CStr(), ec);
		if (TransferInfo* t = FindTransfer(f.transferId)) {
			t->state = TransferState::FAILED;
			t->error = reason;
			t->endMs = now;
		}
	}
	in.files.clear();
	in.closed = true;
	in.sock = sdl3::TcpSocket();
	Touch();
}

// ── Pairs ───────────────────────────────────────────────────────────────────

ChatNode::Peer* ChatNode::FindPeer(uint64_t id) {
	if (id == 0)
		return nullptr;
	for (auto& p : m_peers)
		if (p->info.id == id)
			return p.get();
	return nullptr;
}

ChatNode::Peer& ChatNode::TouchPeer(uint64_t id, const String& nickname, sdl3::IpAddress addr,
									uint16_t port, uint64_t now) {
	String address = addr.ToString();
	bool ipv4 = addr.IsIpv4();
	Peer* p = FindPeer(id);
	if (!p) {
		// un pair manuel pas encore identifié, à la même adresse ?
		for (auto& q : m_peers)
			if (q->info.id == 0 && q->info.manual && q->info.tcpPort == port &&
				(q->info.address == address || q->addr.SameAs(addr)))
				p = q.get();
		if (p) {
			// Le pseudo provisoire d'un pair manuel est son adresse : pas
			// de « s'appelle maintenant » pour lui.
			p->info.id = id;
			if (!nickname.IsEmpty()) {
				p->info.nickname = nickname;
				System(nickname + " a répondu à l'adresse " + p->info.address, now);
			}
		} else {
			auto fresh = std::make_unique<Peer>();
			fresh->info.id = id;
			fresh->info.status = "découvert";
			p = fresh.get();
			m_peers.push_back(std::move(fresh));
			System((nickname.IsEmpty() ? String("?") : nickname) + " a rejoint le réseau (" +
					   address + ")",
				   now);
		}
	}
	// Une adresse IPv4 remplace une IPv6 (lien local IPv6 : portée souvent
	// absente de la chaîne, connexion incertaine) ; sinon, la première reste.
	bool takeAddress = !p->addr.Get() || (ipv4 && !p->isIpv4 && !p->info.connected);
	if (takeAddress && addr.Get()) {
		p->addr = std::move(addr);
		p->isIpv4 = ipv4;
		p->info.address = address;
		p->info.tcpPort = port;
	}
	if (!nickname.IsEmpty() && nickname != p->info.nickname) {
		if (!p->info.nickname.IsEmpty())
			System(p->info.nickname + " s'appelle maintenant " + nickname, now);
		p->info.nickname = nickname;
	}
	if (p->info.tcpPort != port && !p->info.connected)
		p->info.tcpPort = port;
	p->info.lastSeenMs = now;
	Touch();
	return *p;
}

Option<String> ChatNode::AddManualPeer(const String& host, uint16_t tcpPort) {
	String h = host.Trim();
	if (h.IsEmpty())
		return Some(String("adresse vide"));
	if (tcpPort == 0)
		return Some(String("port TCP invalide"));
	for (auto& p : m_peers)
		if (p->info.address == h && p->info.tcpPort == tcpPort)
			return Some(String("pair déjà connu"));
	auto addr = sdl3::IpAddress::Resolve(h);
	if (!addr)
		return Some(String(addr.Error()));
	auto p = std::make_unique<Peer>();
	p->addr = std::move(addr.Value());
	p->info.manual = true;
	p->info.nickname = h;
	p->info.address = h;
	p->info.tcpPort = tcpPort;
	p->info.status = "résolution…";
	p->info.lastSeenMs = m_now;
	p->isIpv4 = true;
	m_peers.push_back(std::move(p));
	Touch();
	return NONE;
}

std::vector<PeerInfo> ChatNode::Peers() const {
	std::vector<PeerInfo> out;
	out.reserve(m_peers.size());
	for (auto& p : m_peers)
		out.push_back(p->info);
	std::stable_sort(out.begin(), out.end(), [](const PeerInfo& a, const PeerInfo& b) {
		return a.nickname.Compare(b.nickname) < 0;
	});
	return out;
}

void ChatNode::SetNickname(const String& nickname) {
	String n = Truncated(nickname.Trim(), MAX_NICKNAME);
	if (n.IsEmpty() || n == m_config.nickname)
		return;
	m_config.nickname = n;
	// Les pairs connectés relisent HELLO ; les autres verront la balise.
	for (auto& p : m_peers)
		if (p->info.connected)
			SendFrame(*p, FrameType::HELLO, HelloPayload());
	SendBeacon(BeaconKind::ANNOUNCE);
	Touch();
}

std::vector<uint8_t> ChatNode::HelloPayload() const {
	ByteWriter w;
	w.U64(m_id).U16(m_tcpPort).Text(m_config.nickname);
	return std::move(w.Data());
}

bool ChatNode::SendFrame(Peer& peer, FrameType type, std::span<const uint8_t> payload) {
	auto bytes = EncodeFrame(type, payload);
	if (!peer.info.connected || !peer.sock.Get()) {
		if (type == FrameType::CHAT)
			peer.pending.push_back(std::move(bytes));
		return false;
	}
	if (!peer.sock.Send(bytes.data(), int(bytes.size()))) {
		DropConnection(peer, "écriture impossible", m_now);
		return false;
	}
	return true;
}

void ChatNode::DropConnection(Peer& peer, const String& reason, uint64_t now) {
	peer.sock = sdl3::TcpSocket();
	peer.helloSent = false;
	peer.info.connected = false;
	peer.info.status = "injoignable : " + reason;
	peer.retryAt = now + RETRY_DELAY_MS;
	FailFileSends(peer, reason, now);
	Touch();
}

void ChatNode::FailFileSends(Peer& peer, const String& reason, uint64_t now) {
	for (auto& f : m_fileSends) {
		if (f->peerId != peer.info.id || f->finished || !f->begun)
			continue; // un envoi pas encore commencé attend la reconnexion
		f->finished = true;
		if (TransferInfo* t = FindTransfer(f->transferId)) {
			t->state = TransferState::FAILED;
			t->error = reason;
			t->endMs = now;
		}
	}
}

// ── Envois ──────────────────────────────────────────────────────────────────

void ChatNode::SendChat(const String& text) {
	String t = text.Trim();
	if (t.IsEmpty())
		return;
	auto bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(t.CStr()), t.size());
	for (auto& p : m_peers)
		SendFrame(*p, FrameType::CHAT, bytes);
	ChatMessage m;
	m.timeMs = m_now;
	m.author = m_config.nickname;
	m.text = t;
	m.outgoing = true;
	m_messages.push_back(std::move(m));
	if (m_messages.size() > MAX_MESSAGES)
		m_messages.erase(m_messages.begin());
	Touch();
}

Result<uint32_t, String> ChatNode::SendFile(uint64_t peerId, const String& path) {
	Peer* p = FindPeer(peerId);
	if (!p)
		return Err(String("pair inconnu (pas encore identifié ?)"));
	std::error_code ec;
	if (!fs::is_regular_file(path.CStr(), ec))
		return Err("pas un fichier : " + path);
	uint64_t size = fs::file_size(path.CStr(), ec);
	if (ec)
		return Err(String(ec.message()));
	std::FILE* fp = std::fopen(path.CStr(), "rb");
	if (!fp)
		return Err("lecture impossible : " + path);

	auto f = std::make_unique<OutgoingFile>();
	f->transferId = m_nextTransfer++;
	f->peerId = peerId;
	f->fp = fp;
	f->size = size;
	TransferInfo t;
	t.id = f->transferId;
	t.outgoing = true;
	t.peerId = peerId;
	t.peerName = p->info.nickname;
	t.fileName = SanitizeFileName(String(fs::path(path.CStr()).filename().string()));
	t.path = path;
	t.size = size;
	t.startMs = m_now;
	m_transfers.push_back(std::move(t));
	uint32_t id = f->transferId;
	m_fileSends.push_back(std::move(f));
	Touch();
	return Ok(id);
}

void ChatNode::CancelTransfer(uint32_t id) {
	TransferInfo* t = FindTransfer(id);
	if (!t || t->state != TransferState::RUNNING)
		return;
	if (t->outgoing) {
		for (auto& f : m_fileSends) {
			if (f->transferId != id || f->finished)
				continue;
			if (f->begun)
				if (Peer* p = FindPeer(f->peerId)) {
					ByteWriter w;
					w.U32(id).U8(CANCEL_BY_SENDER);
					SendFrame(*p, FrameType::FILE_CANCEL, w.Data());
				}
			f->finished = true;
		}
	} else {
		for (auto& in : m_incoming) {
			auto it = std::find_if(in->files.begin(), in->files.end(),
								   [&](auto& f) { return f.transferId == id; });
			if (it == in->files.end())
				continue;
			if (Peer* p = FindPeer(in->peerId)) {
				ByteWriter w;
				w.U32(it->remoteId).U8(CANCEL_BY_RECEIVER);
				SendFrame(*p, FrameType::FILE_CANCEL, w.Data());
			}
			std::fclose(it->fp);
			std::error_code ec;
			fs::remove(it->partPath.CStr(), ec);
			in->files.erase(it);
		}
	}
	t->state = TransferState::CANCELLED;
	t->error = "annulé";
	t->endMs = m_now;
	Touch();
}

void ChatNode::ClearFinishedTransfers() {
	auto before = m_transfers.size();
	std::erase_if(m_transfers, [](auto& t) { return t.state != TransferState::RUNNING; });
	if (before != m_transfers.size())
		Touch();
}

TransferInfo* ChatNode::FindTransfer(uint32_t id) {
	for (auto& t : m_transfers)
		if (t.id == id)
			return &t;
	return nullptr;
}

void ChatNode::System(const String& text, uint64_t now) {
	ChatMessage m;
	m.timeMs = now;
	m.text = text;
	m.system = true;
	m_messages.push_back(std::move(m));
	if (m_messages.size() > MAX_MESSAGES)
		m_messages.erase(m_messages.begin());
	Touch();
}

} // namespace net::chat
