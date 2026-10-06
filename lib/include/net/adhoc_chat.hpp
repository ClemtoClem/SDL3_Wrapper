#pragma once
/**
 * net::chat — découvrir les ordinateurs voisins d'un réseau local (cellule
 * Wi-Fi ad hoc ou n'importe quel LAN) et s'échanger messages et fichiers.
 *
 * Construit sur sdl3/net.hpp (SDL_net), sans fil d'exécution : l'application
 * appelle `ChatNode::Poll` à chaque image, tout y est non bloquant.
 *
 * DÉCOUVERTE (UDP, port `discoveryPort`) — chaque nœud diffuse toutes les
 * `announceIntervalMs` une BALISE « je suis là » : identifiant aléatoire de
 * 64 bits, pseudo, port TCP. Envoi à l'adresse de diffusion générale et à
 * celle du sous-réseau de chaque interface (169.254.255.255 pour une adresse
 * lien local, a.b.c.255 sinon) : une cellule ad hoc sans route par défaut
 * reçoit aussi. Un pair muet depuis `peerTimeoutMs` est oublié. `Probe()`
 * demande aux voisins de se signaler tout de suite. Si la diffusion est
 * bloquée, `AddManualPeer` connecte directement une adresse connue.
 *
 * ÉCHANGES (TCP, port `tcpPort`) — chaque nœud ouvre UNE connexion sortante
 * vers chaque pair et n'écrit que sur celle-là ; il lit sur les connexions
 * que les pairs lui ont ouvertes. Le flux est découpé en trames : magie
 * « AH », type, longueur (32 bits petit-boutiste), charge utile ; la première
 * trame d'une connexion est HELLO (identifiant, port, pseudo).
 *
 * FICHIERS — FILE_BEGIN (id, taille, nom), FILE_CHUNK (id, 32 Kio), FILE_END
 * (id, CRC-32). L'envoi est régulé sur `TcpSocket::PendingWrites` : la
 * mémoire tampon reste bornée quelle que soit la taille du fichier. Le
 * récepteur écrit `nom.part` dans `downloadDir`, vérifie taille et CRC, puis
 * renomme (sans écraser un fichier existant : « nom (2).ext »). Le nom reçu
 * est réduit à son dernier composant : un pair ne peut pas écrire ailleurs.
 *
 * Le trafic n'est PAS chiffré (au mieux par le WPA2 de la cellule) et les
 * fichiers sont acceptés automatiquement : c'est une démonstration.
 */
#include "core/core.hpp"
#include "sdl3/sdl3.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace net::chat {

inline constexpr uint16_t DEFAULT_DISCOVERY_PORT = 48620;
inline constexpr uint16_t DEFAULT_TCP_PORT = 48621;
inline constexpr size_t MAX_FRAME_PAYLOAD = 1u << 20;
inline constexpr size_t FILE_CHUNK_SIZE = 32u * 1024u;

// ── Protocole (pur, testé sans réseau) ───────────────────────────────────────

enum class FrameType : uint8_t {
	HELLO = 1,		 ///< u64 id, u16 port TCP, pseudo
	CHAT = 2,		 ///< texte UTF-8
	FILE_BEGIN = 3,	 ///< u32 id, u64 taille, nom
	FILE_CHUNK = 4,	 ///< u32 id, octets
	FILE_END = 5,	 ///< u32 id, u32 CRC-32
	FILE_CANCEL = 6, ///< u32 id
	BYE = 7,		 ///< le pair s'en va
};

struct Frame {
	FrameType type = FrameType::CHAT;
	std::vector<uint8_t> payload;
};

[[nodiscard]] std::vector<uint8_t> EncodeFrame(FrameType type, std::span<const uint8_t> payload);

/// Réassemble les trames d'un flux TCP reçu par morceaux quelconques.
class FrameDecoder {
public:
	void Feed(std::span<const uint8_t> bytes);
	/// Prochaine trame complète ; NONE s'il en manque encore une partie.
	[[nodiscard]] Option<Frame> Next();
	/// Flux invalide (magie, type ou taille) : la connexion est à fermer.
	[[nodiscard]] bool Corrupted() const noexcept { return m_corrupted; }

private:
	std::vector<uint8_t> m_buffer;
	size_t m_read = 0;
	bool m_corrupted = false;
};

/// Lecture / écriture petit-boutiste des charges utiles.
class ByteWriter {
public:
	ByteWriter& U8(uint8_t v);
	ByteWriter& U16(uint16_t v);
	ByteWriter& U32(uint32_t v);
	ByteWriter& U64(uint64_t v);
	ByteWriter& Bytes(std::span<const uint8_t> bytes);
	ByteWriter& Text(const String& text) {
		return Bytes({reinterpret_cast<const uint8_t*>(text.CStr()), text.size()});
	}
	[[nodiscard]] std::vector<uint8_t>& Data() noexcept { return m_data; }

private:
	std::vector<uint8_t> m_data;
};

class ByteReader {
public:
	explicit ByteReader(std::span<const uint8_t> data) : m_data(data) {}
	[[nodiscard]] Option<uint8_t> U8();
	[[nodiscard]] Option<uint16_t> U16();
	[[nodiscard]] Option<uint32_t> U32();
	[[nodiscard]] Option<uint64_t> U64();
	/// Le reste des octets.
	[[nodiscard]] std::span<const uint8_t> Rest() noexcept;
	[[nodiscard]] String RestText();

private:
	std::span<const uint8_t> m_data;
	size_t m_pos = 0;
};

enum class BeaconKind : uint8_t {
	ANNOUNCE = 1, ///< je suis là
	PROBE = 2,	  ///< je suis là, et signalez-vous
	BYE = 3,	  ///< je pars
};

/// Datagramme de découverte.
struct Beacon {
	BeaconKind kind = BeaconKind::ANNOUNCE;
	uint64_t nodeId = 0;
	uint16_t tcpPort = 0;
	String nickname;
};

[[nodiscard]] std::vector<uint8_t> EncodeBeacon(const Beacon& beacon);
/// NONE : pas une balise de ce protocole (autre programme sur le port…).
[[nodiscard]] Option<Beacon> DecodeBeacon(std::span<const uint8_t> data);

/// Adresse de diffusion du sous-réseau d'une adresse IPv4 (« 169.254.3.7 » →
/// « 169.254.255.255 », « 10.42.0.2 » → « 10.42.0.255 ») ; NONE pour la
/// boucle locale, l'IPv6 ou une chaîne invalide.
[[nodiscard]] Option<String> SubnetBroadcast(const String& ipv4);

/// Dernier composant d'un nom reçu, sans caractère dangereux (« ../a/b.txt »
/// → « b.txt ») ; « fichier » s'il ne reste rien.
[[nodiscard]] String SanitizeFileName(const String& name);

/// `dir/name` s'il est libre, sinon `dir/base (2).ext`, `(3)`…
[[nodiscard]] String FreePath(const String& dir, const String& name);

// ── Nœud ─────────────────────────────────────────────────────────────────────

struct NodeConfig {
	String nickname = "anonyme";
	uint16_t discoveryPort = DEFAULT_DISCOVERY_PORT;
	/// Premier port TCP essayé ; les suivants (+1…+31) si occupé (plusieurs
	/// instances sur une même machine).
	uint16_t tcpPort = DEFAULT_TCP_PORT;
	String downloadDir; ///< dossier des fichiers reçus (créé au besoin)
	uint64_t announceIntervalMs = 2000;
	uint64_t peerTimeoutMs = 8000;
	bool broadcast = true; ///< false : uniquement des pairs manuels
};

struct PeerInfo {
	uint64_t id = 0; ///< 0 : pair manuel pas encore identifié
	String nickname;
	String address;
	uint16_t tcpPort = 0;
	bool manual = false;
	bool connected = false; ///< notre connexion sortante est établie
	String status;			///< « connecté », « connexion… », « injoignable : … »
	uint64_t lastSeenMs = 0;

	[[nodiscard]] String Label() const;
};

struct ChatMessage {
	uint64_t timeMs = 0;
	uint64_t peerId = 0; ///< 0 : nous, ou message système
	String author;
	String text;
	bool outgoing = false;
	bool system = false; ///< arrivée / départ d'un pair, fichier reçu…
};

enum class TransferState : uint8_t { RUNNING, DONE, FAILED, CANCELLED };

struct TransferInfo {
	uint32_t id = 0; ///< local, unique dans le nœud
	bool outgoing = true;
	uint64_t peerId = 0;
	String peerName;
	String fileName;
	String path; ///< source (envoi) ou destination (réception)
	uint64_t size = 0;
	uint64_t done = 0;
	TransferState state = TransferState::RUNNING;
	String error;
	uint64_t startMs = 0;
	uint64_t endMs = 0;

	[[nodiscard]] float Progress() const noexcept {
		return size ? float(double(done) / double(size)) : 1.f;
	}
	/// Débit moyen en octets par seconde.
	[[nodiscard]] double Rate(uint64_t nowMs) const noexcept;
};

[[nodiscard]] const char* TransferStateName(TransferState state) noexcept;

class ChatNode {
public:
	/// Ouvre le socket de découverte et le serveur TCP. NET_Init doit avoir
	/// été fait (sdl3::NetContext).
	[[nodiscard]] static Result<std::unique_ptr<ChatNode>, String> Start(NodeConfig config);
	~ChatNode();

	ChatNode(const ChatNode&) = delete;
	ChatNode& operator=(const ChatNode&) = delete;

	/// Avance tout : balises, connexions, trames, envois de fichiers.
	void Poll(uint64_t nowMs);

	/// Message à tous les pairs connus.
	void SendChat(const String& text);
	/// Envoie un fichier à un pair (id de PeerInfo) ; rend l'id du transfert.
	[[nodiscard]] Result<uint32_t, String> SendFile(uint64_t peerId, const String& path);
	void CancelTransfer(uint32_t id);
	/// Retire de la liste les transferts terminés.
	void ClearFinishedTransfers();

	/// Pair à joindre directement (diffusion bloquée, autre sous-réseau…) ;
	/// NONE si accepté, la raison sinon.
	[[nodiscard]] Option<String> AddManualPeer(const String& host, uint16_t tcpPort);
	/// Diffuse une balise PROBE : les voisins répondent sans attendre.
	void Probe();
	void SetNickname(const String& nickname);

	[[nodiscard]] uint64_t Id() const noexcept { return m_id; }
	[[nodiscard]] const String& Nickname() const noexcept { return m_config.nickname; }
	[[nodiscard]] uint16_t TcpPort() const noexcept { return m_tcpPort; }
	[[nodiscard]] uint16_t DiscoveryPort() const noexcept { return m_config.discoveryPort; }
	[[nodiscard]] const String& DownloadDir() const noexcept { return m_config.downloadDir; }
	/// Adresses IP de cette machine (hors boucle locale), mises à jour au fil
	/// de l'eau : une interface ad hoc qui monte y apparaît.
	[[nodiscard]] const std::vector<String>& LocalAddresses() const noexcept {
		return m_localAddresses;
	}

	[[nodiscard]] std::vector<PeerInfo> Peers() const;
	[[nodiscard]] const std::vector<ChatMessage>& Messages() const noexcept { return m_messages; }
	[[nodiscard]] const std::vector<TransferInfo>& Transfers() const noexcept {
		return m_transfers;
	}
	/// Change à chaque modification visible (pairs, messages, transferts) :
	/// l'interface ne reconstruit ses listes que si elle a bougé.
	[[nodiscard]] uint64_t Revision() const noexcept { return m_revision; }

private:
	struct Peer;
	struct Incoming;
	struct OutgoingFile;
	struct IncomingFile;

	explicit ChatNode(NodeConfig config);

	void RefreshLocalAddresses(uint64_t now);
	void SendBeacon(BeaconKind kind);
	void PollDiscovery(uint64_t now);
	void PollAccept(uint64_t now);
	void PollIncoming(uint64_t now);
	void PollOutgoing(uint64_t now);
	void PollFileSends(uint64_t now);
	void ExpirePeers(uint64_t now);

	void HandleFrame(Incoming& in, Frame& frame, uint64_t now);
	void CloseIncoming(Incoming& in, const String& reason, uint64_t now);
	Peer& TouchPeer(uint64_t id, const String& nickname, sdl3::IpAddress addr, uint16_t port,
					uint64_t now);
	Peer* FindPeer(uint64_t id);
	bool SendFrame(Peer& peer, FrameType type, std::span<const uint8_t> payload);
	void DropConnection(Peer& peer, const String& reason, uint64_t now);
	void FailFileSends(Peer& peer, const String& reason, uint64_t now);
	TransferInfo* FindTransfer(uint32_t id);
	void System(const String& text, uint64_t now);
	[[nodiscard]] std::vector<uint8_t> HelloPayload() const;
	void Touch() noexcept { ++m_revision; }

	NodeConfig m_config;
	uint64_t m_id = 0;
	uint16_t m_tcpPort = 0;
	sdl3::UdpSocket m_udp;
	sdl3::TcpServer m_server;
	std::vector<std::unique_ptr<Peer>> m_peers;
	std::vector<std::unique_ptr<Incoming>> m_incoming;
	std::vector<std::unique_ptr<OutgoingFile>> m_fileSends;
	std::vector<sdl3::IpAddress> m_broadcastTargets;
	std::vector<String> m_broadcastNames;
	std::vector<String> m_localAddresses;
	std::vector<ChatMessage> m_messages;
	std::vector<TransferInfo> m_transfers;
	uint64_t m_lastAnnounce = 0;
	uint64_t m_lastAddressRefresh = 0;
	uint64_t m_now = 0;
	uint32_t m_nextTransfer = 1;
	uint64_t m_revision = 1;
	bool m_started = false;
};

} // namespace net::chat
