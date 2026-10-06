// Smoke test : net::wifi (analyse de nmcli, commandes de cellule ad hoc, sans
// rien lancer) et net::chat (trames, balises, puis deux nœuds réels en boucle
// locale : identification, chat, fichier avec CRC, annulation, départ).
#define USE_TEST

#include "core/core.hpp"
#include "core/test.hpp"
#include "net/adhoc_chat.hpp"
#include "net/wifi.hpp"
#include "sdl3/sdl3.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace fs = std::filesystem;
using namespace net;

namespace {

/// nmcli simulé : rend une sortie selon la sous-commande, garde l'historique.
struct FakeNmcli {
	std::vector<std::vector<String>> calls;
	std::map<std::string, wifi::CommandResult> replies; ///< clé : argv joint par des espaces

	wifi::CommandRunner Runner() {
		return [this](const std::vector<String>& argv) -> Result<wifi::CommandResult, String> {
			calls.push_back(argv);
			std::string key;
			for (auto& a : argv)
				key += std::string(key.empty() ? "" : " ") + a.CStr();
			for (auto& [prefix, reply] : replies)
				if (key.rfind(prefix, 0) == 0)
					return Ok(reply);
			return Ok(wifi::CommandResult{0, ""});
		};
	}
};

[[nodiscard]] fs::path TempDir(const char* name) {
	auto dir = fs::temp_directory_path() / (std::string("adhoc_chat_test_") + name);
	fs::remove_all(dir);
	fs::create_directories(dir);
	return dir;
}

/// Fait tourner les deux nœuds jusqu'à `done` (ou ~4 s).
template <typename Pred>
bool PumpUntil(chat::ChatNode& a, chat::ChatNode& b, uint64_t& clock, Pred done) {
	for (int i = 0; i < 400; ++i) {
		clock += 10;
		a.Poll(clock);
		b.Poll(clock);
		if (done())
			return true;
		SDL_Delay(10);
	}
	return false;
}

[[nodiscard]] const chat::PeerInfo* PeerById(const std::vector<chat::PeerInfo>& peers,
											 uint64_t id) {
	for (auto& p : peers)
		if (p.id == id)
			return &p;
	return nullptr;
}

} // namespace

// ── nmcli : analyse ──────────────────────────────────────────────────────────

TEST(Wifi, SplitTerseLine) {
	auto f = wifi::SplitTerseLine("*:68\\:A3\\:78\\:98\\:5B\\:0C:Mon\\\\Réseau\\:x:Ad-Hoc:6");
	ASSERT_EQ(f.size(), size_t(5));
	EXPECT_TRUE(f[0] == "*");
	EXPECT_TRUE(f[1] == "68:A3:78:98:5B:0C");
	EXPECT_TRUE(f[2] == "Mon\\Réseau:x");
	EXPECT_TRUE(f[3] == "Ad-Hoc");
	auto empty = wifi::SplitTerseLine("a::");
	EXPECT_EQ(empty.size(), size_t(3));
	std::cout << "SplitTerseLine (échappements \\: et \\\\): ok\n";
}

TEST(Wifi, ParseNetworksAndDevices) {
	const char* scan =
		" :68\\:A3\\:78\\:89\\:70\\:31:Europole:Infra:11:2462 MHz:195 Mbit/s:79:WPA2\n"
		" :02\\:11\\:22\\:33\\:44\\:55:adhoc-chat:Ad-Hoc:6:2437 MHz:54 Mbit/s:40:\n"
		"*:20\\:37\\:F0\\:31\\:E9\\:69:Box:Infra:1:2412 MHz:405 Mbit/s:90:WPA1 WPA2\n"
		"ligne invalide\n";
	auto nets = wifi::ParseNetworks(scan);
	ASSERT_EQ(nets.size(), size_t(3));
	EXPECT_TRUE(nets[0].IsAdhoc()); // ad hoc d'abord
	EXPECT_TRUE(nets[0].ssid == "adhoc-chat");
	EXPECT_EQ(nets[0].channel, 6);
	EXPECT_EQ(nets[0].frequencyMHz, 2437);
	EXPECT_TRUE(nets[0].IsOpen());
	EXPECT_TRUE(nets[1].ssid == "Box"); // puis par signal
	EXPECT_TRUE(nets[1].inUse);
	EXPECT_TRUE(nets[1].security == "WPA1 WPA2");
	EXPECT_TRUE(nets[2].bssid == "68:A3:78:89:70:31");

	auto devs = wifi::ParseDevices(
		"wlp0s20f3:wifi:connected:Europole\nlo:loopback:connected (externally):lo\n"
		"p2p-dev-wlp0s20f3:wifi-p2p:disconnected:\n");
	ASSERT_EQ(devs.size(), size_t(1));
	EXPECT_TRUE(devs[0].name == "wlp0s20f3");
	EXPECT_TRUE(devs[0].connection == "Europole");

	auto caps = wifi::ParseCapabilities("WIFI-PROPERTIES.ADHOC:yes\nWIFI-PROPERTIES.IBSS-RSN:no\n"
										"WIFI-PROPERTIES.2GHZ:yes\nWIFI-PROPERTIES.5GHZ:yes\n");
	EXPECT_TRUE(caps.adhoc);
	EXPECT_FALSE(caps.ibssRsn);
	EXPECT_TRUE(caps.band5GHz);

	auto ips = wifi::ParseIpv4Addresses("IP4.ADDRESS[1]:169.254.12.7/16\nIP4.GATEWAY:\n");
	ASSERT_EQ(ips.size(), size_t(1));
	EXPECT_TRUE(ips[0] == "169.254.12.7/16");
	EXPECT_EQ(wifi::ChannelToFrequencyMHz(1), 2412);
	EXPECT_EQ(wifi::ChannelToFrequencyMHz(14), 2484);
	EXPECT_EQ(wifi::ChannelToFrequencyMHz(36), 5180);
	std::cout << "ParseNetworks / ParseDevices / capacités / IPv4: ok\n";
}

TEST(Wifi, AdhocConfigAndCommands) {
	wifi::AdhocConfig cfg;
	cfg.device = "wlan0";
	cfg.ssid = "salle B";
	cfg.channel = 11;
	EXPECT_TRUE(cfg.Validate().IsNone());
	EXPECT_TRUE(cfg.ProfileName() == "adhoc-salle B");

	auto cmds = wifi::AdhocCommands(cfg);
	ASSERT_EQ(cmds.size(), size_t(2));
	String add = wifi::CommandLine(cmds[0]);
	EXPECT_TRUE(add.Contains("mode adhoc"));
	EXPECT_TRUE(add.Contains("wifi.channel 11"));
	EXPECT_TRUE(add.Contains("ipv4.method link-local"));
	EXPECT_TRUE(add.Contains("ssid 'salle B'"));
	EXPECT_TRUE(wifi::CommandLine(cmds[1]).Contains("connection up id 'adhoc-salle B'"));

	cfg.ipv4 = wifi::Ipv4Method::MANUAL;
	cfg.address = "10.42.0.300/24";
	EXPECT_TRUE(cfg.Validate().IsSome());
	cfg.address = "10.42.0.3/24";
	EXPECT_TRUE(cfg.Validate().IsNone());
	cfg.password = "court";
	EXPECT_TRUE(cfg.Validate().IsSome());
	cfg.password = "secret-wpa2";
	EXPECT_TRUE(cfg.Validate().IsNone());
	String withPsk = wifi::CommandLine(wifi::AdhocCommands(cfg)[0]);
	EXPECT_TRUE(withPsk.Contains("wifi-sec.key-mgmt wpa-psk"));
	EXPECT_FALSE(withPsk.Contains("secret-wpa2")); // masqué à l'affichage
	EXPECT_TRUE(withPsk.Contains("ipv4.addresses 10.42.0.3/24"));

	String manual = wifi::ManualInstructions(cfg);
	EXPECT_TRUE(manual.Contains("iw dev wlan0 ibss join 'salle B' 2462"));
	EXPECT_TRUE(manual.Contains("ip addr add 10.42.0.3/24 dev wlan0"));
	std::cout << "AdhocConfig::Validate / AdhocCommands / ManualInstructions: ok\n";
}

TEST(Wifi, JoinAndLeaveWithFakeRunner) {
	wifi::AdhocConfig cfg;
	cfg.device = "wlan0";
	FakeNmcli ok;
	auto report = wifi::JoinAdhoc(cfg, ok.Runner());
	EXPECT_TRUE(report.ok);
	ASSERT_EQ(ok.calls.size(), size_t(3)); // delete (sans erreur), add, up
	EXPECT_TRUE(ok.calls[0][2] == "delete");
	EXPECT_TRUE(ok.calls[1][2] == "add");
	EXPECT_TRUE(ok.calls[2][4] == "up");

	FakeNmcli denied;
	denied.replies["nmcli connection add"] = {
		4, "Error: Failed to add 'adhoc-adhoc-chat' connection: Insufficient privileges"};
	auto refused = wifi::JoinAdhoc(cfg, denied.Runner());
	EXPECT_FALSE(refused.ok);
	EXPECT_TRUE(refused.permissionDenied);
	EXPECT_TRUE(refused.error.Contains("Insufficient privileges"));
	EXPECT_EQ(denied.calls.size(), size_t(2)); // on s'arrête à l'échec

	FakeNmcli leave;
	auto left = wifi::LeaveAdhoc(cfg, leave.Runner());
	EXPECT_TRUE(left.ok);
	ASSERT_EQ(leave.calls.size(), size_t(3)); // down, delete, device connect
	EXPECT_TRUE(leave.calls[2][2] == "connect");

	FakeNmcli scan;
	scan.replies["nmcli -t -e yes -f IN-USE"] = {
		0, " :02\\:11\\:22\\:33\\:44\\:55:cell:Ad-Hoc:6:2437 MHz:54 Mbit/s:40:\n"};
	auto nets = wifi::Scan("wlan0", true, scan.Runner());
	ASSERT_TRUE(nets.IsOk());
	EXPECT_EQ(nets.Value().size(), size_t(1));
	EXPECT_TRUE(scan.calls[0].back() == "wlan0");
	std::cout << "JoinAdhoc / LeaveAdhoc / Scan (nmcli simulé, refus de droits): ok\n";
}

// ── Protocole ────────────────────────────────────────────────────────────────

TEST(Chat, FramesSurviveArbitrarySplits) {
	std::vector<uint8_t> stream;
	std::vector<uint8_t> big(100000);
	for (size_t i = 0; i < big.size(); ++i)
		big[i] = uint8_t(i * 7);
	for (auto& f :
		 {chat::EncodeFrame(chat::FrameType::CHAT, std::vector<uint8_t>{'s', 'a', 'l', 'u', 't'}),
		  chat::EncodeFrame(chat::FrameType::FILE_CHUNK, big),
		  chat::EncodeFrame(chat::FrameType::BYE, {})})
		stream.insert(stream.end(), f.begin(), f.end());

	chat::FrameDecoder dec;
	std::vector<chat::Frame> frames;
	size_t step = 1;
	for (size_t pos = 0; pos < stream.size(); pos += step, step = step * 3 % 977 + 1) {
		size_t n = std::min(step, stream.size() - pos);
		dec.Feed({stream.data() + pos, n});
		while (auto f = dec.Next())
			frames.push_back(std::move(*f));
	}
	ASSERT_EQ(frames.size(), size_t(3));
	EXPECT_TRUE(frames[0].type == chat::FrameType::CHAT);
	EXPECT_EQ(frames[0].payload.size(), size_t(5));
	EXPECT_TRUE(frames[1].payload == big);
	EXPECT_TRUE(frames[2].type == chat::FrameType::BYE);
	EXPECT_FALSE(dec.Corrupted());

	chat::FrameDecoder bad;
	std::vector<uint8_t> garbage = {'G', 'E', 'T', ' ', '/', ' ', 'H'};
	bad.Feed(garbage);
	EXPECT_TRUE(bad.Next().IsNone());
	EXPECT_TRUE(bad.Corrupted());
	std::cout << "FrameDecoder (découpage arbitraire, flux invalide): ok\n";
}

TEST(Chat, BeaconsAndNames) {
	chat::Beacon b{chat::BeaconKind::PROBE, 0x1122334455667788ull, 48621, "Élodie"};
	auto bytes = chat::EncodeBeacon(b);
	auto back = chat::DecodeBeacon(bytes);
	ASSERT_TRUE(back.IsSome());
	EXPECT_TRUE(back->kind == chat::BeaconKind::PROBE);
	EXPECT_EQ(back->nodeId, 0x1122334455667788ull);
	EXPECT_EQ(back->tcpPort, uint16_t(48621));
	EXPECT_TRUE(back->nickname == "Élodie");
	bytes[0] = 'X';
	EXPECT_TRUE(chat::DecodeBeacon(bytes).IsNone());
	bytes = chat::EncodeBeacon(b);
	bytes.resize(bytes.size() - 2); // pseudo tronqué
	EXPECT_TRUE(chat::DecodeBeacon(bytes).IsNone());

	EXPECT_TRUE(*chat::SubnetBroadcast("169.254.3.7") == "169.254.255.255");
	EXPECT_TRUE(*chat::SubnetBroadcast("10.42.0.2") == "10.42.0.255");
	EXPECT_TRUE(chat::SubnetBroadcast("127.0.0.1").IsNone());
	EXPECT_TRUE(chat::SubnetBroadcast("fe80::1").IsNone());

	EXPECT_TRUE(chat::SanitizeFileName("../../etc/passwd") == "passwd");
	EXPECT_TRUE(chat::SanitizeFileName("C:\\Users\\a\\photo.jpg") == "photo.jpg");
	EXPECT_TRUE(chat::SanitizeFileName("..") == "fichier");
	EXPECT_TRUE(chat::SanitizeFileName("a:b?.txt") == "a_b_.txt");

	auto dir = TempDir("names");
	std::ofstream(dir / "note.txt") << "x";
	EXPECT_TRUE(chat::FreePath(String(dir.string()), "note.txt") ==
				String((dir / "note (2).txt").string()));
	EXPECT_TRUE(chat::FreePath(String(dir.string()), "autre.txt") ==
				String((dir / "autre.txt").string()));
	fs::remove_all(dir);
	std::cout << "Balises / SubnetBroadcast / SanitizeFileName / FreePath: ok\n";
}

// ── Deux nœuds en boucle locale ──────────────────────────────────────────────

TEST(Chat, TwoNodesChatAndFile) {
	auto netCtx = sdl3::NetContext::Create();
	ASSERT_TRUE(netCtx.IsOk());
	auto dirA = TempDir("a");
	auto dirB = TempDir("b");

	// Pas de diffusion : le test ne dépend pas du réseau de la machine. B
	// connaît A par son adresse ; A apprend B par la trame HELLO.
	chat::NodeConfig ca{.nickname = "Alice",
						.tcpPort = 48731,
						.downloadDir = String(dirA.string()),
						.broadcast = false};
	chat::NodeConfig cb{.nickname = "Bob",
						.tcpPort = 48731,
						.downloadDir = String(dirB.string()),
						.broadcast = false};
	auto na = chat::ChatNode::Start(ca);
	ASSERT_TRUE(na.IsOk());
	auto nb = chat::ChatNode::Start(cb);
	ASSERT_TRUE(nb.IsOk());
	auto& a = *na.Value();
	auto& b = *nb.Value();
	EXPECT_TRUE(b.TcpPort() != a.TcpPort()); // port suivant pris automatiquement

	EXPECT_TRUE(b.AddManualPeer("127.0.0.1", a.TcpPort()).IsNone());
	EXPECT_TRUE(b.AddManualPeer("127.0.0.1", a.TcpPort()).IsSome()); // doublon refusé
	uint64_t clock = 1000;
	bool linked = PumpUntil(a, b, clock, [&] {
		auto pa = a.Peers();
		auto pb = b.Peers();
		auto* bobSeenByA = PeerById(pa, b.Id());
		auto* aliceSeenByB = PeerById(pb, a.Id());
		return bobSeenByA && bobSeenByA->connected && aliceSeenByB && aliceSeenByB->connected;
	});
	ASSERT_TRUE(linked);
	EXPECT_EQ(b.Peers().size(), size_t(1)); // le pair manuel a été identifié, pas dupliqué
	EXPECT_TRUE(PeerById(b.Peers(), a.Id())->nickname == "Alice");

	// Chat dans les deux sens.
	a.SendChat("Bonjour Bob");
	b.SendChat("Salut Alice ✓");
	auto lastText = [](chat::ChatNode& n) {
		for (auto it = n.Messages().rbegin(); it != n.Messages().rend(); ++it)
			if (!it->system && !it->outgoing)
				return it->text;
		return String();
	};
	ASSERT_TRUE(PumpUntil(a, b, clock, [&] {
		return lastText(a) == "Salut Alice ✓" && lastText(b) == "Bonjour Bob";
	}));

	// Renommage propagé.
	a.SetNickname("Alice B.");
	ASSERT_TRUE(PumpUntil(a, b, clock,
						  [&] { return PeerById(b.Peers(), a.Id())->nickname == "Alice B."; }));

	// Fichier de 1 Mo (plusieurs fenêtres d'envoi), vérifié octet par octet.
	auto src = dirA / "données.bin";
	std::vector<char> content(1024 * 1024 + 123);
	for (size_t i = 0; i < content.size(); ++i)
		content[i] = char((i * 2654435761u) >> 13);
	std::ofstream(src, std::ios::binary).write(content.data(), std::streamsize(content.size()));
	auto sent = a.SendFile(b.Id(), String(src.string()));
	ASSERT_TRUE(sent.IsOk());
	auto findT = [](chat::ChatNode& n, bool outgoing) -> const chat::TransferInfo* {
		for (auto& t : n.Transfers())
			if (t.outgoing == outgoing)
				return &t;
		return nullptr;
	};
	ASSERT_TRUE(PumpUntil(a, b, clock, [&] {
		auto* in = findT(b, false);
		return in && in->state != chat::TransferState::RUNNING;
	}));
	auto* in = findT(b, false);
	EXPECT_TRUE(in->state == chat::TransferState::DONE);
	EXPECT_TRUE(findT(a, true)->state == chat::TransferState::DONE);
	EXPECT_EQ(in->done, uint64_t(content.size()));
	EXPECT_TRUE(in->fileName == "données.bin");
	std::ifstream got(in->path.CStr(), std::ios::binary);
	std::vector<char> received((std::istreambuf_iterator<char>(got)),
							   std::istreambuf_iterator<char>());
	EXPECT_TRUE(received == content);
	EXPECT_FALSE(fs::exists(dirB / "données.bin.part"));

	// Second envoi du même nom : pas d'écrasement.
	ASSERT_TRUE(a.SendFile(b.Id(), String(src.string())).IsOk());
	ASSERT_TRUE(PumpUntil(a, b, clock, [&] {
		return b.Transfers().size() == 2 && b.Transfers()[1].state == chat::TransferState::DONE;
	}));
	EXPECT_TRUE(fs::exists(dirB / "données (2).bin"));

	// Annulation par le destinataire : l'expéditeur s'arrête aussi.
	b.ClearFinishedTransfers();
	a.ClearFinishedTransfers();
	EXPECT_TRUE(b.Transfers().empty());
	std::vector<char> huge(16 * 1024 * 1024, 'z');
	auto bigSrc = dirA / "gros.bin";
	std::ofstream(bigSrc, std::ios::binary).write(huge.data(), std::streamsize(huge.size()));
	ASSERT_TRUE(a.SendFile(b.Id(), String(bigSrc.string())).IsOk());
	ASSERT_TRUE(PumpUntil(a, b, clock,
						  [&] { return !b.Transfers().empty() && b.Transfers()[0].done > 0; }));
	b.CancelTransfer(b.Transfers()[0].id);
	ASSERT_TRUE(PumpUntil(a, b, clock,
						  [&] { return a.Transfers()[0].state != chat::TransferState::RUNNING; }));
	EXPECT_TRUE(a.Transfers()[0].state == chat::TransferState::CANCELLED);
	EXPECT_TRUE(b.Transfers()[0].state == chat::TransferState::CANCELLED);
	EXPECT_FALSE(fs::exists(dirB / "gros.bin.part"));
	EXPECT_FALSE(fs::exists(dirB / "gros.bin"));

	// Départ d'Alice (destruction du nœud : trame BYE) : Bob la retire de
	// ses pairs. `a` n'est plus valable, on ne fait plus tourner que Bob.
	na.Value().reset();
	uint64_t t = clock;
	bool gone = false;
	for (int i = 0; i < 300 && !gone; ++i) {
		t += 10;
		b.Poll(t);
		gone = b.Peers().empty();
		SDL_Delay(10);
	}
	EXPECT_TRUE(gone);
	fs::remove_all(dirA);
	fs::remove_all(dirB);
	std::cout << "Deux nœuds : identification, chat, renommage, fichier 1 Mo + CRC, annulation, "
				 "départ: ok\n";
}

TEST(Chat, ManualPeerPointingToItselfIsDropped) {
	auto netCtx = sdl3::NetContext::Create();
	ASSERT_TRUE(netCtx.IsOk());
	auto dir = TempDir("self");
	chat::NodeConfig c{.nickname = "Seul",
					   .tcpPort = 48771,
					   .downloadDir = String(dir.string()),
					   .broadcast = false};
	auto n = chat::ChatNode::Start(c);
	ASSERT_TRUE(n.IsOk());
	auto& node = *n.Value();
	EXPECT_TRUE(node.AddManualPeer("127.0.0.1", node.TcpPort()).IsNone());
	EXPECT_EQ(node.Peers().size(), size_t(1));
	bool dropped = false;
	uint64_t clock = 1000;
	for (int i = 0; i < 300 && !dropped; ++i) {
		clock += 10;
		node.Poll(clock);
		dropped = node.Peers().empty();
		SDL_Delay(10);
	}
	EXPECT_TRUE(dropped);
	EXPECT_TRUE(!node.Messages().empty() && node.Messages().back().text.Contains("lui-même"));
	n.Value().reset();
	fs::remove_all(dir);
	std::cout << "Pair manuel désignant ce nœud lui-même : retiré: ok\n";
}

int main() {
	return RUN_ALL_TESTS();
}
