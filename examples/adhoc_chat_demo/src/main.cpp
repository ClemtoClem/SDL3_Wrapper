/**
 * adhoc_chat_demo — chat et échange de fichiers entre ordinateurs reliés par
 * une cellule Wi-Fi IBSS (« ad hoc »), sans point d'accès ni serveur.
 *
 * Trois colonnes :
 *
 *  - WI-FI : interfaces et capacités de la carte (mode ad hoc, IBSS-RSN),
 *    scan des réseaux visibles (cellules ad hoc en tête ; un clic recopie
 *    SSID et canal), création / adhésion à une cellule et départ, via
 *    NetworkManager (net/wifi.hpp → nmcli). Le journal montre chaque
 *    commande et sa sortie ; si les droits manquent, les commandes à lancer
 *    en administrateur (nmcli ou iw + ip). Un pare-feu actif est signalé
 *    avec les ports à ouvrir.
 *  - CHAT : messages à tous les pairs, arrivées / départs, fichiers reçus.
 *  - PAIRS ET FICHIERS : voisins découverts par diffusion UDP (bouton
 *    « Sonder » pour une réponse immédiate), ajout manuel d'une adresse,
 *    envoi d'un fichier au pair sélectionné (ou à tous) par le bouton ou en
 *    déposant le fichier sur la fenêtre, transferts avec progression, débit
 *    et annulation.
 *
 * Toute la partie réseau est dans la bibliothèque (net/adhoc_chat.hpp,
 * construit sur sdl3/net.hpp) ; ce fichier n'est que l'interface. Les
 * commandes nmcli bloquent (un scan dure quelques secondes) : elles tournent
 * dans des tâches std::async dont l'interface relève le résultat.
 *
 *   cpp-maker run adhoc_chat_demo -- [options]
 *     --nick NOM             pseudo (défaut : $USER)
 *     --discovery-port N     port UDP de découverte (défaut 48620)
 *     --tcp-port N           premier port TCP essayé (défaut 48621)
 *     --download-dir DOSSIER fichiers reçus (défaut ~/Téléchargements/adhoc_chat)
 *     --peer HÔTE[:PORT]     pair à joindre directement (répétable)
 *     --no-broadcast         pas de découverte par diffusion
 *     --no-wifi              ne lance pas nmcli (chat sur le réseau actuel)
 *     --device IFACE         interface Wi-Fi à utiliser
 *
 * Essai sur une seule machine : lancer deux fois le programme ; la seconde
 * instance prend le port TCP suivant et les deux se découvrent (si le
 * pare-feu laisse passer la diffusion) ou se joignent avec
 * `--peer 127.0.0.1:48621`.
 */
#include <core/core.hpp>
#include <net/adhoc_chat.hpp>
#include <net/wifi.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>

namespace wifi = net::wifi;
namespace chat = net::chat;

// ============================================================================
// Constantes
// ============================================================================

/// Couleur en octets (0-255), opaque — le constructeur de FColor attend
/// des composantes normalisées et un alpha explicite.
static constexpr sdl3::FColor Rgb(int r, int g, int b) {
	return {static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f, static_cast<float>(b) / 255.f, 1.f};
}

static constexpr int WIN_W = 1360;
static constexpr int WIN_H = 840;

static constexpr sdl3::FColor COL_MUTED = Rgb(150, 156, 178);
static constexpr sdl3::FColor COL_ACCENT = Rgb(120, 190, 255);
static constexpr sdl3::FColor COL_OK = Rgb(120, 210, 140);
static constexpr sdl3::FColor COL_WARN = Rgb(240, 190, 90);
static constexpr sdl3::FColor COL_ERR = Rgb(240, 110, 110);

// ============================================================================
// Options de la ligne de commande
// ============================================================================

struct Options {
	chat::NodeConfig node;
	std::vector<std::pair<String, uint16_t>> peers;
	bool wifi = true;
	String device;
};

static void PrintUsage() {
	std::cout << "adhoc_chat_demo [--nick NOM] [--discovery-port N] [--tcp-port N] [--download-dir "
				 "DOSSIER]\n"
				 "                [--peer HÔTE[:PORT]]... [--no-broadcast] [--no-wifi] [--device "
				 "IFACE]\n";
}

static Option<Options> ParseOptions(int argc, char** argv) {
	Options o;
	const char* user = std::getenv("USER");
	o.node.nickname = user && *user ? user : "anonyme";
	for (int i = 1; i < argc; ++i) {
		String a = argv[i];
		auto next = [&]() -> Option<String> {
			if (i + 1 >= argc)
				return NONE;
			return Some(String(argv[++i]));
		};
		Option<String> v;
		if (a == "--help" || a == "-h") {
			PrintUsage();
			return NONE;
		} else if (a == "--no-broadcast") {
			o.node.broadcast = false;
		} else if (a == "--no-wifi") {
			o.wifi = false;
		} else if ((a == "--nick" || a == "--download-dir" || a == "--device" ||
					a == "--discovery-port" || a == "--tcp-port" || a == "--peer") &&
				   (v = next())) {
			if (a == "--nick")
				o.node.nickname = *v;
			else if (a == "--download-dir")
				o.node.downloadDir = *v;
			else if (a == "--device")
				o.device = *v;
			else if (a == "--discovery-port")
				o.node.discoveryPort = uint16_t(v->ToInt32());
			else if (a == "--tcp-port")
				o.node.tcpPort = uint16_t(v->ToInt32());
			else {
				size_t colon = v->Rfind(':');
				if (colon != String::NPOS && v->Count(':') == 1)
					o.peers.push_back(
						{v->Substr(0, colon), uint16_t(v->Substr(colon + 1).ToInt32())});
				else
					o.peers.push_back({*v, chat::DEFAULT_TCP_PORT});
			}
		} else {
			std::cerr << "Option inconnue ou incomplète : " << a.CStr() << "\n";
			PrintUsage();
			return NONE;
		}
	}
	return Some(std::move(o));
}

// ============================================================================
// Petits utilitaires
// ============================================================================

static String HumanSize(double bytes) {
	const char* units[] = {"o", "Kio", "Mio", "Gio", "Tio"};
	int u = 0;
	while (bytes >= 1024.0 && u < 4) {
		bytes /= 1024.0;
		++u;
	}
	return u == 0 ? String::Format("%.0f %s", bytes, units[u])
				  : String::Format("%.1f %s", bytes, units[u]);
}

/// Heure locale (hh:mm:ss) d'un instant exprimé en ticks SDL.
static String ClockOf(uint64_t ticksMs) {
	sdl3::Time now = 0;
	if (!SDL_GetCurrentTime(&now))
		return "--:--";
	SDL_Time at = now - SDL_Time(SDL_GetTicks() - ticksMs) * 1000000;
	SDL_DateTime dt;
	if (!SDL_TimeToDateTime(at, &dt, true))
		return "--:--";
	return String::Format("%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
}

/// Résultat d'une tâche lancée sans bloquer l'interface.
template <typename T> struct Job {
	std::future<T> future;

	[[nodiscard]] bool Running() const { return future.valid(); }
	template <typename Fn> void Start(Fn&& fn) {
		future = std::async(std::launch::async, std::forward<Fn>(fn));
	}
	/// Le résultat, une seule fois, quand il est prêt.
	[[nodiscard]] Option<T> Take() {
		if (!future.valid() ||
			future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			return NONE;
		return Some(future.get());
	}
};

/// Premier relevé de la configuration Wi-Fi (fait hors du fil de l'interface).
struct WifiProbe {
	bool available = false;
	std::vector<wifi::WifiDevice> devices;
	String error;
	Option<wifi::FirewallInfo> firewall;
};

/// État de l'interface utilisée : capacités et adresses.
struct DeviceInfo {
	String device;
	Option<wifi::WifiCapabilities> caps;
	std::vector<String> ipv4;
};

// ============================================================================
// Feuille de style
// ============================================================================

static constexpr const char *CHAT_CSS = R"css(
window-frame { background-color: #12131b; }
#titlebar { background-color: #1e2130; }
#page { padding: 10px 8px 2px 10px; gap: 8px; }
#header { gap: 10px; }
#header-info { color: #969cb2; flex-grow: 1; text-overflow: ellipsis; overflow: hidden; }
#body { gap: 10px; align-items: stretch; }
.column { background-color: #1c1e2a; border-radius: 6px; padding: 10px; gap: 6px; border-width: 0; }
#wifi { width: 430px; overflow-y: auto; }
#chat { flex-grow: 1; }
#peers { width: 400px; }
.section { font-size: 16px; font-weight: bold; color: #78beff; }
.muted { color: #969cb2; }
.wrap { text-overflow: wrap; width: 100%; }
.well { background-color: #161822; padding: 4px; gap: 2px; overflow-y: auto; }
.field-label { width: 110px; }
.row-card { background-color: #242736; border-radius: 4px; padding: 3px 6px; gap: 1px; }
.row-card.selected { background-color: #303e60; }
.small { font-size: 12px; }
.ok { color: #78d28c; }
.warn { color: #f0be5a; }
.err { color: #f06e6e; }
.accent { color: #78beff; }
selectable.selected { background-color: #303e60; }
)css";

/// Longueur maximale d'un champ de saisie.
static ui::WidgetBuilder MaxLen(ui::WidgetBuilder input, size_t n) {
	input.OnBuilt([n](ui::Widget w) {
		if (auto ed = w.Get<ui::TextEditor>(); ed.IsSome())
			ed.Unwrap()->maxLength = n;
	});
	return input;
}

// ============================================================================
// Application
// ============================================================================

class AdhocChatApp {
public:
	AdhocChatApp(ui::Ui &gui, sdl3::Window &window, chat::ChatNode &node, Options options)
		: m_gui(gui), m_window(window), m_node(node), m_opt(std::move(options)) {
		m_adhoc.device = m_opt.device;
	}

	void Build();
	void Update();
	void OnDropFile(const String &path);
	[[nodiscard]] bool QuitRequested() const noexcept { return m_frame.CloseRequested(); }

private:
	// ── Construction ─────────────────────────────────────────────────────
	ui::AnyWidget WifiColumn();
	ui::AnyWidget ChatColumn();
	ui::AnyWidget PeersColumn();
	static ui::WidgetBuilder Section(const char *title) { return ui::Label(title).Class("section"); }
	static ui::WidgetBuilder Muted(String text) { return ui::Label(std::move(text)).Class("muted").Class("wrap"); }
	ui::AnyWidget Field(const char *label, ui::WidgetBuilder input, ui::Widget &ref);

	// ── Wi-Fi ────────────────────────────────────────────────────────────
	void StartProbe();
	void StartDeviceInfo();
	void StartScan();
	void StartJoin();
	void StartLeave();
	void PollJobs();
	void ReadAdhocForm();
	void RebuildDeviceCombo();
	void RebuildNetworks();
	void RefreshWifiStatus();
	void Log(const String &text);
	[[nodiscard]] bool WifiBusy() const {
		return m_probeJob.Running() || m_scanJob.Running() || m_configJob.Running();
	}

	// ── Chat / pairs / fichiers ──────────────────────────────────────────
	void SendMessage();
	void PickFiles();
	void SendFileToTargets(const String &path);
	void RefreshChat();
	void RefreshPeers(bool force);
	void RefreshTransfers(bool force);
	void RefreshHeader();
	void SetStatus(const String &text, sdl3::FColor color) { m_frame.SetStatus(text, Some(color)); }
	/// Classe de couleur sémantique (ok / warn / err / muted) exclusive.
	static void Tint(ui::Widget w, const char *cls);

	ui::Ui &m_gui;
	sdl3::Window &m_window;
	chat::ChatNode &m_node;
	Options m_opt;
	ui::WindowFrame m_frame;

	// Wi-Fi
	wifi::AdhocConfig m_adhoc;
	WifiProbe m_probe;
	DeviceInfo m_deviceInfo;
	std::vector<wifi::WifiNetwork> m_networks;
	String m_log;
	bool m_joined = false;
	Job<WifiProbe> m_probeJob;
	Job<DeviceInfo> m_deviceJob;
	Job<Result<std::vector<wifi::WifiNetwork>, String>> m_scanJob;
	Job<wifi::ConfigureReport> m_configJob;
	bool m_configIsJoin = true;
	uint64_t m_deviceInfoAt = 0;

	// Widgets
	ui::Widget m_headerInfo, m_nickInput;
	ui::Widget m_deviceHolder, m_capsLabel, m_ipLabel, m_wifiState, m_networkList, m_scanButton;
	ui::Widget m_ssidInput, m_channelInput, m_addressInput, m_passwordInput, m_joinButton, m_leaveButton;
	ui::Widget m_logArea;
	ui::Widget m_chatArea, m_messageInput;
	ui::Widget m_peerList, m_targetLabel, m_hostInput, m_portInput;
	ui::Widget m_transferList;

	// Rafraîchissements
	String m_chatSignature = "-";
	uint64_t m_lastRevision = 0;
	String m_peersSignature;
	String m_transfersSignature;
	std::vector<std::pair<uint32_t, ui::Widget>> m_progressBars; ///< transfert → barre
	std::vector<std::pair<uint32_t, ui::Widget>> m_progressLabels;
	uint64_t m_selectedPeer = 0; ///< 0 : tous les pairs
	uint64_t m_lastSlowRefresh = 0;

	// Fichiers choisis par le dialogue (rappel possiblement sur un autre fil)
	std::mutex m_pickedMutex;
	std::vector<String> m_picked;
};

void AdhocChatApp::Tint(ui::Widget w, const char *cls) {
	for (const char *c : {"ok", "warn", "err", "muted", "accent"})
		w.RemoveClass(c);
	w.AddClass(cls);
}

// ── Construction ─────────────────────────────────────────────────────────────

ui::AnyWidget AdhocChatApp::Field(const char *label, ui::WidgetBuilder input, ui::Widget &ref) {
	return ui::Row().Gap(6.f).FillWidth().Children({
		ui::Label(label).Class("field-label"),
		input.Grow().Ref(ref),
	});
}

void AdhocChatApp::Build() {
	m_gui.AddStyleSheet(CHAT_CSS);
	// Fenêtre sans décoration : barre de titre, déplacement, réduire /
	// agrandir / fermer, barre d'état et poignée de redimensionnement.
	m_frame.Build(m_gui, m_window,
				  {.title = "Chat Wi-Fi ad hoc (IBSS)", .appIcon = Some(ui::MaterialIcons::WIFI_TETHERING),
				   .status = "Prêt."});

	auto applyNick = [this] {
		m_node.SetNickname(m_nickInput.Text());
		SetStatus("Pseudo : " + m_node.Nickname(), COL_OK);
	};
	m_frame.Content().Append(ui::Column().Id("page").Grow().FillWidth().Children({
		// ── En-tête : informations locales, pseudo ──
		ui::Row().Id("header").FillWidth().Children({
			ui::Label("").Id("header-info").Ref(m_headerInfo),
			ui::Label("Pseudo"),
			MaxLen(ui::Input("pseudo", m_node.Nickname()), 48).Width(160.f).Ref(m_nickInput).OnSubmit(
				[applyNick](const String &) { applyNick(); }),
			ui::Button("Appliquer").OnClick(applyNick),
		}),
		// ── Corps : trois colonnes ──
		ui::Row().Id("body").Grow().FillWidth().Children({WifiColumn(), ChatColumn(), PeersColumn()}),
	}));

	RefreshHeader();
	RebuildNetworks();
	RefreshPeers(true);
	RefreshTransfers(true);
	if (m_opt.wifi) {
		StartProbe();
	} else {
		Log("Wi-Fi : désactivé (--no-wifi) — le chat utilise le réseau actuel.");
		RefreshWifiStatus();
	}
	for (auto &[host, port] : m_opt.peers) {
		if (auto err = m_node.AddManualPeer(host, port))
			Log("Pair " + host + " : " + *err);
		else
			Log(String::Format("Pair manuel ajouté : %s:%u", host.CStr(), unsigned(port)));
	}
}

ui::AnyWidget AdhocChatApp::WifiColumn() {
	return ui::Panel().Class("column").Id("wifi").Children({
		Section("Wi-Fi"),
		ui::Row().Gap(6.f).FillWidth().Children({
			ui::Label("Interface").Width(70.f),
			ui::Row().Grow().Ref(m_deviceHolder),
			ui::Button("Actualiser").Tooltip("Relire les interfaces, capacités, adresses et pare-feu").OnClick([this] {
				if (m_opt.wifi && !WifiBusy())
					StartProbe();
			}),
		}),
		Muted("").Ref(m_capsLabel),
		Muted("").Ref(m_ipLabel),
		ui::Label("").Class("wrap").Ref(m_wifiState),
		// ── Réseaux visibles ──
		ui::Row().Gap(6.f).FillWidth().Children({
			ui::Label("Réseaux visibles").Bold().Grow(),
			ui::Button("Scanner")
				.Tooltip("Balayage radio (quelques secondes) ; les cellules ad hoc sont en tête")
				.Ref(m_scanButton)
				.OnClick([this] {
					if (m_opt.wifi && !WifiBusy())
						StartScan();
				}),
		}),
		ui::Column().Class("well").FillWidth().Height(170.f).Ref(m_networkList),
		// ── Cellule ad hoc ──
		ui::Separator(),
		ui::Label("Cellule ad hoc").Bold(),
		Field("SSID", MaxLen(ui::Input("adhoc-chat", m_adhoc.ssid), 32), m_ssidInput),
		Field("Canal (1-13)", MaxLen(ui::Input("6", String::Format("%d", m_adhoc.channel)), 2), m_channelInput),
		ui::Row().Gap(6.f).FillWidth().Children({
			ui::Label("Adressage IPv4").Class("field-label"),
			ui::Combo(std::vector<String>{"Lien local (169.254.x.x)", "Adresse fixe"}, 0).Grow().OnSelect([this](int i) {
				m_adhoc.ipv4 = i == 1 ? wifi::Ipv4Method::MANUAL : wifi::Ipv4Method::LINK_LOCAL;
				m_addressInput.SetEnabled(m_adhoc.ipv4 == wifi::Ipv4Method::MANUAL);
			}),
		}),
		Field("Adresse fixe", MaxLen(ui::Input("10.42.0.N/24, unique par machine"), 18).Disabled(), m_addressInput),
		Field("Mot de passe", MaxLen(ui::Input("vide : cellule ouverte (WPA2 : 8+ car.)"), 63), m_passwordInput),
		ui::Row().Gap(6.f).FillWidth().Children({
			ui::Button("Créer / rejoindre")
				.Grow()
				.Tooltip("Même SSID et même canal sur chaque ordinateur ; la connexion Wi-Fi en cours sera coupée")
				.Ref(m_joinButton)
				.OnClick([this] {
					if (m_opt.wifi && !WifiBusy())
						StartJoin();
				}),
			ui::Button("Quitter la cellule")
				.Grow()
				.Tooltip("Supprime le profil ad hoc ; NetworkManager reprend la connexion habituelle")
				.Ref(m_leaveButton)
				.OnClick([this] {
					if (m_opt.wifi && !WifiBusy())
						StartLeave();
				}),
		}),
		ui::Label("Journal (commandes et sorties, copiable)").Bold(),
		ui::LogView([](const String &line, std::vector<ui::TextSpan> &out) {
			if (line.StartsWith("$ "))
				out.push_back({0, line.size(), COL_ACCENT});
			else if (line.StartsWith("!"))
				out.push_back({0, line.size(), COL_WARN});
			else if (line.StartsWith("  sudo "))
				out.push_back({0, line.size(), COL_OK});
		})
			.FillWidth()
			.Height(190.f)
			.FontSize(12.f)
			.Ref(m_logArea),
	});
}

ui::AnyWidget AdhocChatApp::ChatColumn() {
	// « [hh:mm:ss] Pseudo : texte » — l'auteur en couleur ; les lignes
	// système (« * … ») en gris, les nôtres en vert.
	auto highlight = [this](const String &line, std::vector<ui::TextSpan> &out) {
		const size_t close = line.Find("] ");
		if (close == String::NPOS)
			return;
		out.push_back({0, close + 1, COL_MUTED});
		if (line.Substr(close + 2).StartsWith("* ")) {
			out.push_back({close + 2, line.size(), COL_MUTED});
			return;
		}
		const size_t colon = line.Find(" : ", close);
		if (colon != String::NPOS) {
			const bool mine = line.Substr(close + 2, colon - close - 2) == m_node.Nickname();
			out.push_back({close + 2, colon, mine ? COL_OK : COL_ACCENT});
		}
	};
	return ui::Panel().Class("column").Id("chat").Children({
		Section("Chat"),
		ui::LogView(highlight).FillWidth().Grow().Ref(m_chatArea),
		ui::Row().Gap(6.f).FillWidth().Children({
			MaxLen(ui::Input("Message à tous les pairs — Entrée pour envoyer"), 4000)
				.Grow()
				.Ref(m_messageInput)
				.OnSubmit([this](const String &) { SendMessage(); }),
			ui::Button("Envoyer").OnClick([this] { SendMessage(); }),
		}),
	});
}

ui::AnyWidget AdhocChatApp::PeersColumn() {
	return ui::Panel().Class("column").Id("peers").Children({
		ui::Row().Gap(6.f).FillWidth().Children({
			Section("Pairs").Grow(),
			ui::Button("Sonder").Tooltip("Diffuse une demande : les voisins répondent tout de suite").OnClick([this] {
				m_node.Probe();
				SetStatus("Sonde diffusée.", COL_MUTED);
			}),
		}),
		ui::Column().Class("well").FillWidth().Height(210.f).Ref(m_peerList),
		ui::Row().Gap(6.f).FillWidth().Children({
			MaxLen(ui::Input("adresse IP du pair"), 64).Grow().Ref(m_hostInput),
			MaxLen(ui::Input("port", String::Format("%u", unsigned(chat::DEFAULT_TCP_PORT))), 5)
				.Width(64.f)
				.Ref(m_portInput),
			ui::Button("Ajouter")
				.Tooltip("Joindre un pair directement (diffusion bloquée, autre sous-réseau)")
				.OnClick([this] {
					const String hostText = m_hostInput.Text().Trim();
					const int p = m_portInput.Text().Trim().ToInt32();
					if (auto err = m_node.AddManualPeer(hostText, uint16_t(p > 0 && p < 65536 ? p : 0)))
						SetStatus("Pair manuel : " + *err, COL_ERR);
					else {
						SetStatus("Pair manuel ajouté : " + hostText, COL_OK);
						m_hostInput.SetText("");
					}
				}),
		}),
		ui::Separator(),
		ui::Row().Gap(6.f).FillWidth().Children({
			Section("Fichiers").Grow(),
			ui::Button("Envoyer…").Tooltip("Choisir des fichiers ; ou les déposer sur la fenêtre").OnClick([this] {
				PickFiles();
			}),
		}),
		Muted("").Ref(m_targetLabel),
		ui::Column().Class("well").FillWidth().Grow().Gap(4.f).Ref(m_transferList),
		ui::Row().Gap(6.f).FillWidth().Children({
			ui::Button("Dossier de réception").Grow().Tooltip(m_node.DownloadDir()).OnClick([this] {
				sdl3::OpenUrl("file://" + m_node.DownloadDir());
			}),
			ui::Button("Effacer les terminés").Grow().OnClick([this] { m_node.ClearFinishedTransfers(); }),
		}),
	});
}

// ── Wi-Fi ────────────────────────────────────────────────────────────────────

void AdhocChatApp::Log(const String &text) {
	if (!m_log.IsEmpty())
		m_log.Append('\n');
	m_log.Append(text);
	m_logArea.SetText(m_log); // le journal suit la fin du texte
}

void AdhocChatApp::StartProbe() {
	m_wifiState.SetText("Lecture de la configuration Wi-Fi…");
	const uint16_t udp = m_node.DiscoveryPort();
	const uint16_t tcp = m_opt.node.tcpPort;
	m_probeJob.Start([udp, tcp] {
		WifiProbe p;
		p.firewall = wifi::DetectFirewall(udp, tcp, uint16_t(tcp + 31));
		p.available = wifi::IsAvailable();
		if (!p.available) {
			p.error = "NetworkManager (nmcli) introuvable ou arrêté";
			return p;
		}
		auto devs = wifi::ListDevices();
		if (devs)
			p.devices = std::move(devs.Value());
		else
			p.error = devs.Error();
		return p;
	});
}

void AdhocChatApp::StartDeviceInfo() {
	if (m_adhoc.device.IsEmpty() || m_deviceJob.Running())
		return;
	String dev = m_adhoc.device;
	m_deviceInfoAt = SDL_GetTicks();
	m_deviceJob.Start([dev] {
		DeviceInfo info;
		info.device = dev;
		if (auto caps = wifi::Capabilities(dev))
			info.caps = Some(caps.Value());
		if (auto ips = wifi::Ipv4Addresses(dev))
			info.ipv4 = std::move(ips.Value());
		return info;
	});
}

void AdhocChatApp::StartScan() {
	String dev = m_adhoc.device;
	m_scanButton.SetText("Scan…");
	SetStatus("Balayage radio en cours…", COL_MUTED);
	m_scanJob.Start([dev] { return wifi::Scan(dev, true); });
}

void AdhocChatApp::ReadAdhocForm() {
	m_adhoc.ssid = m_ssidInput.Text().Trim();
	m_adhoc.channel = m_channelInput.Text().Trim().ToInt32();
	m_adhoc.address = m_addressInput.Text().Trim();
	m_adhoc.password = m_passwordInput.Text();
	m_adhoc.connectionName = String();
}

void AdhocChatApp::StartJoin() {
	ReadAdhocForm();
	if (auto problem = m_adhoc.Validate()) {
		SetStatus("Cellule : " + *problem, COL_ERR);
		return;
	}
	if (m_adhoc.device.IsEmpty()) {
		SetStatus("Aucune interface Wi-Fi choisie.", COL_ERR);
		return;
	}
	Log(String::Format("\n— Cellule « %s », canal %d (%d MHz) sur %s", m_adhoc.ssid.CStr(), m_adhoc.channel,
					   wifi::ChannelToFrequencyMHz(m_adhoc.channel), m_adhoc.device.CStr()));
	m_wifiState.SetText("Création / adhésion à la cellule… (jusqu'à 30 s)");
	Tint(m_wifiState, "warn");
	m_configIsJoin = true;
	wifi::AdhocConfig cfg = m_adhoc;
	m_configJob.Start([cfg] { return wifi::JoinAdhoc(cfg); });
}

void AdhocChatApp::StartLeave() {
	ReadAdhocForm();
	Log("\n— Départ de la cellule « " + m_adhoc.ssid + " »");
	m_wifiState.SetText("Départ de la cellule…");
	Tint(m_wifiState, "warn");
	m_configIsJoin = false;
	wifi::AdhocConfig cfg = m_adhoc;
	m_configJob.Start([cfg] { return wifi::LeaveAdhoc(cfg); });
}

void AdhocChatApp::PollJobs() {
	if (auto probe = m_probeJob.Take()) {
		m_probe = std::move(*probe);
		if (!m_probe.error.IsEmpty())
			Log("! Wi-Fi : " + m_probe.error);
		if (m_adhoc.device.IsEmpty() && !m_probe.devices.empty())
			m_adhoc.device = m_probe.devices.front().name;
		for (auto &d : m_probe.devices)
			Log(String::Format("Interface %s : %s%s%s", d.name.CStr(), d.state.CStr(),
							   d.connection.IsEmpty() ? "" : " — ", d.connection.CStr()));
		if (m_probe.firewall) {
			Log("! Pare-feu " + m_probe.firewall->name +
				" actif : il bloque en général la découverte et les connexions des pairs. Pour ouvrir les ports :");
			for (auto &cmd : m_probe.firewall->commands)
				Log("  " + cmd);
		}
		RebuildDeviceCombo();
		StartDeviceInfo();
		RefreshWifiStatus();
	}
	if (auto info = m_deviceJob.Take()) {
		m_deviceInfo = std::move(*info);
		RefreshWifiStatus();
	}
	if (auto scan = m_scanJob.Take()) {
		m_scanButton.SetText("Scanner");
		if (*scan) {
			m_networks = std::move(scan->Value());
			const size_t adhoc =
				size_t(std::count_if(m_networks.begin(), m_networks.end(), [](auto &n) { return n.IsAdhoc(); }));
			SetStatus(String::Format("%zu réseau(x) visible(s), dont %zu cellule(s) ad hoc.", m_networks.size(), adhoc),
					  COL_OK);
		} else {
			SetStatus("Scan : " + scan->Error(), COL_ERR);
			Log("! Scan : " + scan->Error());
		}
		RebuildNetworks();
	}
	if (auto report = m_configJob.Take()) {
		for (auto &line : report->log)
			Log(line);
		if (report->ok) {
			m_joined = m_configIsJoin;
			Log(m_configIsJoin ? "Cellule active. Les pairs apparaissent dès que leurs balises arrivent."
							   : "Profil ad hoc supprimé.");
			SetStatus(m_configIsJoin ? "Cellule ad hoc active." : "Cellule quittée.", COL_OK);
			m_node.Probe();
		} else {
			Log("! Échec : " + report->error);
			SetStatus("Wi-Fi : " + report->error, COL_ERR);
			if (report->permissionDenied) {
				Log("! Droits insuffisants pour NetworkManager. Commandes à lancer en administrateur :");
				Log(wifi::ManualInstructions(m_adhoc));
			}
		}
		StartDeviceInfo();
		RefreshWifiStatus();
	}
	// Adresses : une adresse lien local met quelques secondes à apparaître.
	if (m_opt.wifi && !m_adhoc.device.IsEmpty() && SDL_GetTicks() - m_deviceInfoAt > 5000)
		StartDeviceInfo();

	const bool usable = m_opt.wifi && !WifiBusy();
	m_joinButton.SetEnabled(usable);
	m_leaveButton.SetEnabled(usable);
	m_scanButton.SetEnabled(usable);
}

void AdhocChatApp::RebuildDeviceCombo() {
	m_deviceHolder.Clear();
	if (m_probe.devices.empty()) {
		m_deviceHolder.Append(ui::Label(m_opt.wifi ? "aucune interface Wi-Fi" : "—").Class("muted"));
		return;
	}
	std::vector<String> names;
	int selected = 0;
	for (size_t i = 0; i < m_probe.devices.size(); ++i) {
		names.push_back(m_probe.devices[i].name);
		if (m_probe.devices[i].name == m_adhoc.device)
			selected = int(i);
	}
	m_deviceHolder.Append(ui::Combo(names, selected).Grow().OnSelect([this](int i) {
		if (size_t(i) < m_probe.devices.size()) {
			m_adhoc.device = m_probe.devices[size_t(i)].name;
			m_deviceInfoAt = 0;
			StartDeviceInfo();
		}
	}));
}

void AdhocChatApp::RebuildNetworks() {
	m_networkList.Clear();
	if (m_networks.empty()) {
		m_networkList.Append(ui::Label("Aucun réseau (lancez un scan).").Class("muted"));
		return;
	}
	int index = 0;
	for (auto &n : m_networks) {
		const String ssid = n.ssid.IsEmpty() ? String("(masqué)") : n.ssid;
		const String text = String::Format("%s%-8s %s  · canal %d · %d %% · %s", n.inUse ? "● " : "",
										   wifi::WifiModeName(n.mode), ssid.CStr(), n.channel, n.signal,
										   n.IsOpen() ? "ouvert" : n.security.CStr());
		ui::WidgetBuilder row = ui::Selectable(text, index++);
		row.FillWidth().Ellipsis().Tooltip(n.bssid + " — " + String::Format("%d MHz, %s", n.frequencyMHz, n.rate.CStr()));
		if (n.IsAdhoc())
			row.Class("ok");
		else if (n.inUse)
			row.Class("accent");
		wifi::WifiNetwork copy = n;
		row.OnClick([this, copy] {
			if (!copy.ssid.IsEmpty())
				m_ssidInput.SetText(copy.ssid);
			if (copy.channel >= 1 && copy.channel <= 14)
				m_channelInput.SetText(String::Format("%d", copy.channel));
			SetStatus(copy.IsAdhoc() ? "Cellule ad hoc choisie : « Créer / rejoindre » pour y entrer."
									 : "Pas une cellule ad hoc : seuls le SSID et le canal sont repris.",
					  copy.IsAdhoc() ? COL_OK : COL_WARN);
		});
		m_networkList.Append(row);
	}
}

void AdhocChatApp::RefreshWifiStatus() {
	if (!m_opt.wifi) {
		m_wifiState.SetText("Configuration Wi-Fi désactivée.");
		return;
	}
	if (!m_probe.available && !m_probeJob.Running()) {
		m_wifiState.SetText("NetworkManager indisponible : configurez la cellule à la main (cf. journal).");
		Tint(m_wifiState, "err");
		if (m_log.Find("iw dev") == String::NPOS)
			Log(wifi::ManualInstructions(m_adhoc));
		return;
	}
	if (m_deviceInfo.device == m_adhoc.device && m_deviceInfo.caps) {
		auto &caps = *m_deviceInfo.caps;
		m_capsLabel.SetText(String::Format("Capacités : ad hoc %s · IBSS chiffré (RSN) %s · 2,4 GHz %s · 5 GHz %s",
										   caps.adhoc ? "oui" : "NON", caps.ibssRsn ? "oui" : "non",
										   caps.band2GHz ? "oui" : "non", caps.band5GHz ? "oui" : "non"));
		Tint(m_capsLabel, caps.adhoc ? "muted" : "err");
	}
	m_ipLabel.SetText(m_deviceInfo.ipv4.empty() ? String("Adresse IPv4 : aucune")
												: "Adresse IPv4 : " + String::Join(m_deviceInfo.ipv4, ", "));
	String connection;
	for (auto &d : m_probe.devices)
		if (d.name == m_adhoc.device)
			connection = d.connection;
	if (m_configJob.Running())
		return;
	if (m_joined) {
		m_wifiState.SetText("Dans la cellule « " + m_adhoc.ssid + " » (profil " + m_adhoc.ProfileName() + ").");
		Tint(m_wifiState, "ok");
	} else {
		m_wifiState.SetText(connection.IsEmpty()
								? String("Pas de cellule ad hoc active.")
								: "Connexion actuelle : " + connection + " (pas une cellule de ce programme).");
		Tint(m_wifiState, "muted");
	}
}

// ── Chat / pairs / fichiers ──────────────────────────────────────────────────

void AdhocChatApp::SendMessage() {
	const String text = m_messageInput.Text().Trim();
	if (text.IsEmpty())
		return;
	m_node.SendChat(text);
	m_messageInput.SetText("");
	if (m_node.Peers().empty())
		SetStatus("Aucun pair pour l'instant : le message n'est parti chez personne.", COL_WARN);
}

void AdhocChatApp::PickFiles() {
	sdl3::dialog::ShowOpenFile(
		[this](const sdl3::DialogResult &r, int) {
			if (!r.Ok())
				return;
			std::lock_guard lock(m_pickedMutex);
			m_picked.insert(m_picked.end(), r.files.begin(), r.files.end());
		},
		m_window, {}, "", true);
}

void AdhocChatApp::OnDropFile(const String &path) { SendFileToTargets(path); }

void AdhocChatApp::SendFileToTargets(const String &path) {
	std::vector<chat::PeerInfo> targets;
	for (auto &p : m_node.Peers())
		if (p.id != 0 && (m_selectedPeer == 0 || p.id == m_selectedPeer))
			targets.push_back(p);
	if (targets.empty()) {
		SetStatus("Aucun pair identifié à qui envoyer « " + path + " ».", COL_WARN);
		return;
	}
	int ok = 0;
	for (auto &p : targets) {
		auto res = m_node.SendFile(p.id, path);
		if (res)
			++ok;
		else
			SetStatus("Envoi à " + p.nickname + " : " + res.Error(), COL_ERR);
	}
	if (ok)
		SetStatus(String::Format("Envoi de « %s » à %d pair(s).",
								 String(std::filesystem::path(path.CStr()).filename().string()).CStr(), ok),
				  COL_OK);
}

void AdhocChatApp::RefreshChat() {
	auto &msgs = m_node.Messages();
	// Le nombre de messages plafonne (les plus anciens partent) : la
	// signature inclut donc le dernier message.
	const String sig =
		msgs.empty() ? String("0")
					 : String::Format("%zu|%llu|", msgs.size(), (unsigned long long)msgs.back().timeMs) + msgs.back().text;
	if (sig == m_chatSignature)
		return;
	m_chatSignature = sig;
	String text;
	for (auto &m : msgs) {
		if (!text.IsEmpty())
			text.Append('\n');
		text.Append("[" + ClockOf(m.timeMs) + "] ");
		if (m.system)
			text.Append("* " + m.text);
		else
			text.Append((m.author.IsEmpty() ? String("?") : m.author) + " : " + m.text.Replace("\n", "\n    "));
	}
	m_chatArea.SetText(msgs.empty() ? String("Aucun message pour l'instant.") : text);
}

void AdhocChatApp::RefreshPeers(bool force) {
	auto peers = m_node.Peers();
	String sig = String::Format("%llu|", (unsigned long long)m_selectedPeer);
	for (auto &p : peers)
		sig.Append(String::Format("%llu/%s/%s/%d/%s;", (unsigned long long)p.id, p.nickname.CStr(), p.address.CStr(),
								  int(p.connected), p.status.CStr()));
	if (!force && sig == m_peersSignature)
		return;
	m_peersSignature = sig;
	if (m_selectedPeer != 0 && std::none_of(peers.begin(), peers.end(), [&](auto &p) { return p.id == m_selectedPeer; }))
		m_selectedPeer = 0;

	m_peerList.Clear();
	ui::WidgetBuilder all = ui::Selectable(String::Format("Tous les pairs (%zu)", peers.size()), 0);
	all.FillWidth().OnClick([this] {
		m_selectedPeer = 0;
		RefreshPeers(true);
	});
	if (m_selectedPeer == 0)
		all.Class("selected");
	m_peerList.Append(all);
	int index = 1;
	for (auto &p : peers) {
		const uint64_t id = p.id;
		ui::WidgetBuilder card = ui::Column().Class("row-card").FillWidth();
		if (id == m_selectedPeer)
			card.Class("selected");
		card.Children({
			ui::Selectable((p.connected ? "● " : "○ ") + (p.nickname.IsEmpty() ? String("?") : p.nickname) +
							   (p.manual ? "  (manuel)" : ""),
						   index++)
				.FillWidth()
				.Class(p.connected ? "ok" : "warn")
				.OnClick([this, id] {
					if (id == 0) {
						SetStatus("Pair pas encore identifié : attendez sa réponse.", COL_WARN);
						return;
					}
					m_selectedPeer = id;
					RefreshPeers(true);
				}),
			ui::Label(String::Format("%s:%u — %s", p.address.CStr(), unsigned(p.tcpPort), p.status.CStr()))
				.Class("muted")
				.Class("small")
				.Ellipsis()
				.FillWidth(),
		});
		m_peerList.Append(card);
	}
	if (peers.empty())
		m_peerList.Append(Muted("Aucun pair : lancez le programme sur un autre ordinateur de la cellule."));

	String target = "Destinataire des fichiers : tous les pairs identifiés";
	for (auto &p : peers)
		if (p.id == m_selectedPeer && m_selectedPeer != 0)
			target = "Destinataire des fichiers : " + p.nickname;
	m_targetLabel.SetText(target + " — ou déposez un fichier sur la fenêtre.");
}

void AdhocChatApp::RefreshTransfers(bool force) {
	auto &ts = m_node.Transfers();
	String sig;
	for (auto &t : ts)
		sig.Append(String::Format("%u/%d;", t.id, int(t.state)));
	const uint64_t now = SDL_GetTicks();
	if (force || sig != m_transfersSignature) {
		m_transfersSignature = sig;
		m_transferList.Clear();
		m_progressBars.clear();
		m_progressLabels.clear();
		if (ts.empty())
			m_transferList.Append(ui::Label("Aucun transfert.").Class("muted"));
		for (auto it = ts.rbegin(); it != ts.rend(); ++it) {
			const chat::TransferInfo &t = *it;
			const char *tint = t.state == chat::TransferState::DONE	   ? "ok"
							   : t.state == chat::TransferState::RUNNING ? "accent"
																		 : "err";
			ui::WidgetBuilder head = ui::Row().Gap(6.f).FillWidth();
			head.Child(ui::Label((t.outgoing ? "↑ " : "↓ ") + t.fileName + (t.outgoing ? " → " : " ← ") + t.peerName)
						   .Class(tint)
						   .Ellipsis()
						   .Grow()
						   .Tooltip(t.path));
			if (t.state == chat::TransferState::RUNNING) {
				const uint32_t id = t.id;
				head.Child(ui::Button("Annuler").OnClick([this, id] { m_node.CancelTransfer(id); }));
			}
			ui::Widget card = m_transferList.Append(ui::Column().Class("row-card").FillWidth().Gap(3.f).Children({
				head,
				ui::Progress(0.f, 1.f, t.Progress()).FillWidth(),
				ui::Label("").Class("muted").Class("small").Ellipsis().FillWidth(),
			}));
			std::vector<ui::Widget> parts = card.Children();
			m_progressBars.push_back({t.id, parts[1]});
			m_progressLabels.push_back({t.id, parts[2]});
		}
	}
	// Progression mise à jour en place (sans reconstruire les widgets).
	for (auto &t : ts) {
		for (auto &[id, bar] : m_progressBars)
			if (id == t.id)
				bar.SetValue(t.Progress());
		for (auto &[id, label] : m_progressLabels) {
			if (id != t.id)
				continue;
			String line = String::Format("%s / %s · %s/s · %s", HumanSize(double(t.done)).CStr(),
										 HumanSize(double(t.size)).CStr(), HumanSize(t.Rate(now)).CStr(),
										 chat::TransferStateName(t.state));
			if (!t.error.IsEmpty())
				line.Append(" : " + t.error);
			label.SetText(line);
		}
	}
}

void AdhocChatApp::RefreshHeader() {
	String addrs;
	for (auto &a : m_node.LocalAddresses()) {
		if (a.Contains(':'))
			continue; // IPv6 : liste trop longue pour l'en-tête
		addrs.Append(addrs.IsEmpty() ? "" : ", ");
		addrs.Append(a);
	}
	m_headerInfo.SetText(String::Format("Moi : %s · TCP %u · découverte UDP %u · adresses : %s",
										m_node.Nickname().CStr(), unsigned(m_node.TcpPort()),
										unsigned(m_node.DiscoveryPort()), addrs.IsEmpty() ? "aucune" : addrs.CStr()));
}

void AdhocChatApp::Update() {
	m_frame.Update();
	m_node.Poll(SDL_GetTicks());
	PollJobs();
	{
		std::vector<String> picked;
		{
			std::lock_guard lock(m_pickedMutex);
			picked.swap(m_picked);
		}
		for (auto &p : picked)
			SendFileToTargets(p);
	}
	const bool changed = m_node.Revision() != m_lastRevision;
	RefreshChat();
	if (changed) {
		RefreshPeers(false);
		RefreshTransfers(false);
		RefreshHeader();
		m_lastRevision = m_node.Revision();
	}
	// Débit et états visibles même sans changement (une fois par seconde).
	const uint64_t now = SDL_GetTicks();
	if (now - m_lastSlowRefresh > 1000) {
		m_lastSlowRefresh = now;
		RefreshTransfers(false);
	}
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char **argv) {
	auto optRes = ParseOptions(argc, argv);
	if (!optRes)
		return 1;
	Options options = std::move(*optRes);

	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::cerr << "TTF init: " << ttf.Error().CStr() << "\n";
		return 1;
	}
	auto netCtx = sdl3::NetContext::Create();
	if (!netCtx) {
		std::cerr << "SDL_net init: " << netCtx.Error().CStr() << "\n";
		return 1;
	}
	auto nodeRes = chat::ChatNode::Start(options.node);
	if (!nodeRes) {
		std::cerr << "Réseau : " << nodeRes.Error().CStr() << "\n";
		return 1;
	}
	chat::ChatNode &node = *nodeRes.Value();

	auto winRes = sdl3::Window::Create(u8"Chat Wi-Fi ad hoc - SDL3 Wrapper", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
	if (!winRes) {
		std::cerr << "Window: " << winRes.Error().CStr() << "\n";
		return 1;
	}
	auto &window = winRes.Value();
	auto renRes = sdl3::Renderer::Create(window);
	if (!renRes) {
		std::cerr << "Renderer: " << renRes.Error().CStr() << "\n";
		return 1;
	}
	auto &ren = renRes.Value();

	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, window, ren);
	AdhocChatApp app(gui, window, node, std::move(options));
	app.Build();

	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		const uint64_t now = sdl3::GetTicksMS();
		const float dt = float(now - lastTick) / 1000.f;
		lastTick = now;
		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit()) {
				running = false;
				break;
			}
			if (e.Type() == SDL_EVENT_DROP_FILE && e.raw.drop.data) {
				app.OnDropFile(e.raw.drop.data);
				continue;
			}
			gui.HandleEvent(e);
		}
		app.Update();
		if (app.QuitRequested())
			running = false;
		gui.Update(dt);
		ren.SetDrawColor(gui.CurrentPalette().appBg);
		ren.Clear();
		gui.Render();
		ren.Present();
		SDL_Delay(5); // le réseau est sondé à chaque image : inutile de tourner à vide
	}
	return 0;
}
