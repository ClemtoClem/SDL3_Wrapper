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
 *   ./build/debug/adhoc_chat_demo [options]
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
#include "core/core.hpp"
#include "net/adhoc_chat.hpp"
#include "net/wifi.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/chrome.hpp"
#include "ui/glyphs.hpp"
#include "ui/ui.hpp"

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
	return {r / 255.f, g / 255.f, b / 255.f, 1.f};
}

static constexpr int WIN_W = 1360;
static constexpr int WIN_H = 840;
static constexpr float FONT_PT = 14.f;

static constexpr sdl3::FColor COL_MUTED = Rgb(150, 156, 178);
static constexpr sdl3::FColor COL_ACCENT = Rgb(120, 190, 255);
static constexpr sdl3::FColor COL_OK = Rgb(120, 210, 140);
static constexpr sdl3::FColor COL_WARN = Rgb(240, 190, 90);
static constexpr sdl3::FColor COL_ERR = Rgb(240, 110, 110);
static constexpr sdl3::FColor COL_PANEL = Rgb(28, 30, 42);
static constexpr sdl3::FColor COL_ROW = Rgb(36, 39, 54);

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
	SDL_Time now = 0;
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
// Application
// ============================================================================

class AdhocChatApp {
public:
	AdhocChatApp(ecs::ArchetypeRegistry& ar, ui::Ui& gui, sdl3::Window& window,
				 chat::ChatNode& node, Options options)
		: m_ar(ar), m_gui(gui), m_f(gui.Factory()), m_window(window), m_node(node),
		  m_opt(std::move(options)) {
		m_adhoc.device = m_opt.device;
	}

	void Build();
	void Update();
	void OnDropFile(const String& path);
	[[nodiscard]] bool QuitRequested() const noexcept { return m_frame.CloseRequested(); }

private:
	// ── Construction ─────────────────────────────────────────────────────
	ecs::Entity BuildWifiColumn(ecs::Entity parent);
	ecs::Entity BuildChatColumn(ecs::Entity parent);
	ecs::Entity BuildPeersColumn(ecs::Entity parent);
	ui::WidgetBuilder Section(const char* title);
	ui::WidgetBuilder Muted(const String& text);

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
	void Log(const String& text);
	[[nodiscard]] bool WifiBusy() const {
		return m_probeJob.Running() || m_scanJob.Running() || m_configJob.Running();
	}

	// ── Chat / pairs / fichiers ──────────────────────────────────────────
	void SendMessage();
	void PickFiles();
	void SendFileToTargets(const String& path);
	void RefreshChat();
	void RefreshPeers(bool force);
	void RefreshTransfers(bool force);
	void RefreshHeader();
	void SetStatus(const String& text, sdl3::FColor color);

	// ── Aides ui ─────────────────────────────────────────────────────────
	void SetText(ecs::Entity e, const String& text);
	void SetColor(ecs::Entity e, sdl3::FColor color);
	[[nodiscard]] String InputText(ecs::Entity e) const;
	void SetInputText(ecs::Entity e, const String& text);
	void Clear(ecs::Entity parent);

	ecs::ArchetypeRegistry& m_ar;
	ui::Ui& m_gui;
	ui::UiFactory& m_f;
	sdl3::Window& m_window;
	chat::ChatNode& m_node;
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
	ecs::Entity m_headerInfo, m_nickInput;
	ecs::Entity m_deviceHolder, m_capsLabel, m_ipLabel, m_wifiState, m_networkList, m_scanButton;
	ecs::Entity m_ssidInput, m_channelInput, m_addressInput, m_passwordInput, m_joinButton,
		m_leaveButton;
	ecs::Entity m_logArea;
	ecs::Entity m_chatArea, m_messageInput;
	ecs::Entity m_peerList, m_targetLabel, m_hostInput, m_portInput;
	ecs::Entity m_transferList;

	// Rafraîchissements
	String m_chatSignature = "-";
	uint64_t m_lastRevision = 0;
	String m_peersSignature;
	String m_transfersSignature;
	std::vector<std::pair<uint32_t, ecs::Entity>> m_progressBars; ///< transfert → barre
	std::vector<std::pair<uint32_t, ecs::Entity>> m_progressLabels;
	uint64_t m_selectedPeer = 0; ///< 0 : tous les pairs
	uint64_t m_lastSlowRefresh = 0;

	// Fichiers choisis par le dialogue (rappel possiblement sur un autre fil)
	std::mutex m_pickedMutex;
	std::vector<String> m_picked;
};

// ── Aides ui ─────────────────────────────────────────────────────────────────

void AdhocChatApp::SetText(ecs::Entity e, const String& text) {
	if (auto l = m_ar.GetComponent<ui::UiLabel>(e); l.IsSome() && l.Unwrap()->text != text) {
		l.Unwrap()->text = text;
		m_gui.Layout().MarkDirty();
	}
}

void AdhocChatApp::SetColor(ecs::Entity e, sdl3::FColor color) {
	auto style = m_ar.GetOrAddComponent<ui::UiStyle>(e);
	if (style.IsSome()) {
		style.Unwrap()->SetTextColor(color);
		m_gui.Layout().MarkDirty();
	}
}

String AdhocChatApp::InputText(ecs::Entity e) const {
	if (auto in = m_ar.GetComponent<ui::UiInput>(e); in.IsSome())
		return in.Unwrap()->text;
	return {};
}

void AdhocChatApp::SetInputText(ecs::Entity e, const String& text) {
	if (auto in = m_ar.GetComponent<ui::UiInput>(e); in.IsSome()) {
		in.Unwrap()->text = text;
		in.Unwrap()->cursor = in.Unwrap()->selectionAnchor = text.size();
		m_gui.Layout().MarkDirty();
	}
}

void AdhocChatApp::Clear(ecs::Entity parent) {
	std::vector<ecs::Entity> children;
	if (auto list = m_ar.GetComponent<ui::UiChildren>(parent); list.IsSome())
		children = list.Unwrap()->list;
	for (ecs::Entity child : children)
		ui::DespawnTree(m_ar, child);
	if (auto list = m_ar.GetComponent<ui::UiChildren>(parent); list.IsSome())
		list.Unwrap()->list.clear();
	m_gui.Layout().MarkDirty();
}

ui::WidgetBuilder AdhocChatApp::Section(const char* title) {
	return std::move(m_f.Label(title).FontSize(16.f).Bold().TextColor(COL_ACCENT));
}

ui::WidgetBuilder AdhocChatApp::Muted(const String& text) {
	return std::move(m_f.Label(text).TextColor(COL_MUTED).TextWrap().GrowW());
}

// ── Construction ─────────────────────────────────────────────────────────────

void AdhocChatApp::Build() {
	// Fenêtre sans décoration : encadrement du module ui (barre de titre,
	// déplacement, réduire / agrandir / fermer, barre d'état et poignée de
	// redimensionnement).
	m_frame.Build(m_gui, m_window,
				  {.title = "Chat Wi-Fi ad hoc (IBSS)",
				   .appIcon = Some(ui::MaterialIcons::WIFI_TETHERING),
				   .status = "Prêt.",
				   .background = Rgb(18, 19, 27),
				   .titleBackground = Rgb(30, 33, 48)});

	auto root = m_f.Column();
	root.Gap(8.f).Pad(math::Sides{10.f, 8.f, 10.f, 2.f}).GrowW().GrowH().Parent(m_frame.Content());
	ecs::Entity rootE = root.Spawn();

	// ── En-tête : pseudo, adresses locales ───────────────────────────────
	auto header = m_f.Row();
	header.Gap(10.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(rootE);
	ecs::Entity headerE = header.Spawn();
	auto info = m_f.Label("");
	info.TextColor(COL_MUTED).GrowW().TextEllipsis().Parent(headerE);
	m_headerInfo = info.Spawn();
	m_f.Label("Pseudo").Parent(headerE).Spawn();
	auto nick = m_f.Input("pseudo");
	nick.W(ui::Dimension::Px(160)).MaxLen(48).Parent(headerE).OnSubmit([this](const String& s) {
		m_node.SetNickname(s);
		SetStatus("Pseudo : " + m_node.Nickname(), COL_OK);
	});
	m_nickInput = nick.Spawn();
	SetInputText(m_nickInput, m_node.Nickname());
	m_f.Button("Appliquer")
		.Parent(headerE)
		.OnClick([this] {
			m_node.SetNickname(InputText(m_nickInput));
			SetStatus("Pseudo : " + m_node.Nickname(), COL_OK);
		})
		.Spawn();

	// ── Corps : trois colonnes ───────────────────────────────────────────
	auto body = m_f.Row();
	body.Gap(10.f).GrowW().GrowH().Parent(rootE);
	ecs::Entity bodyE = body.Spawn();
	BuildWifiColumn(bodyE);
	BuildChatColumn(bodyE);
	BuildPeersColumn(bodyE);

	RefreshHeader();
	if (m_opt.wifi) {
		StartProbe();
	} else {
		Log("Wi-Fi : désactivé (--no-wifi) — le chat utilise le réseau actuel.");
		RefreshWifiStatus();
	}
	for (auto& [host, port] : m_opt.peers) {
		if (auto err = m_node.AddManualPeer(host, port))
			Log("Pair " + host + " : " + *err);
		else
			Log(String::Format("Pair manuel ajouté : %s:%u", host.CStr(), unsigned(port)));
	}
}

ecs::Entity AdhocChatApp::BuildWifiColumn(ecs::Entity parent) {
	auto col = m_f.Panel();
	col.Gap(6.f)
		.Pad(10.f)
		.W(ui::Dimension::Px(430))
		.GrowH()
		.Scrollable()
		.Clip()
		.Bg(COL_PANEL)
		.Radius(6.f)
		.Parent(parent);
	ecs::Entity c = col.Spawn();

	Section("Wi-Fi").Parent(c).Spawn();
	auto devRow = m_f.Row();
	devRow.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(c);
	ecs::Entity devRowE = devRow.Spawn();
	m_f.Label("Interface").W(ui::Dimension::Px(70)).Parent(devRowE).Spawn();
	auto holder = m_f.Row();
	holder.GrowW().HAuto().Parent(devRowE);
	m_deviceHolder = holder.Spawn();
	m_f.Button("Actualiser")
		.Tooltip("Relire les interfaces, capacités, adresses et pare-feu")
		.Parent(devRowE)
		.OnClick([this] {
			if (m_opt.wifi && !WifiBusy())
				StartProbe();
		})
		.Spawn();

	auto caps = Muted("");
	caps.Parent(c);
	m_capsLabel = caps.Spawn();
	auto ip = Muted("");
	ip.Parent(c);
	m_ipLabel = ip.Spawn();
	auto state = m_f.Label("");
	state.TextWrap().GrowW().Parent(c);
	m_wifiState = state.Spawn();

	// ── Réseaux visibles ─────────────────────────────────────────────────
	auto scanRow = m_f.Row();
	scanRow.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(c);
	ecs::Entity scanRowE = scanRow.Spawn();
	m_f.Label("Réseaux visibles").Bold().GrowW().Parent(scanRowE).Spawn();
	auto scan = m_f.Button("Scanner");
	scan.Tooltip("Balayage radio (quelques secondes) ; les cellules ad hoc sont en tête")
		.Parent(scanRowE)
		.OnClick([this] {
			if (m_opt.wifi && !WifiBusy())
				StartScan();
		});
	m_scanButton = scan.Spawn();
	auto list = m_f.Column();
	list.Gap(2.f)
		.Pad(4.f)
		.GrowW()
		.H(ui::Dimension::Px(170))
		.Scrollable()
		.Clip()
		.Bg(Rgb(22, 24, 34))
		.Parent(c);
	m_networkList = list.Spawn();

	// ── Cellule ad hoc ───────────────────────────────────────────────────
	m_f.Separator().Parent(c).Spawn();
	m_f.Label("Cellule ad hoc").Bold().Parent(c).Spawn();
	auto field = [&](const char* label, ui::WidgetBuilder& input) {
		auto row = m_f.Row();
		row.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(c);
		ecs::Entity r = row.Spawn();
		m_f.Label(label).W(ui::Dimension::Px(110)).Parent(r).Spawn();
		input.GrowW().Parent(r);
		return input.Spawn();
	};
	m_ssidInput = field("SSID", m_f.Input("adhoc-chat").MaxLen(32));
	SetInputText(m_ssidInput, m_adhoc.ssid);
	m_channelInput = field("Canal (1-13)", m_f.Input("6").MaxLen(2));
	SetInputText(m_channelInput, String::Format("%d", m_adhoc.channel));
	{
		auto row = m_f.Row();
		row.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(c);
		ecs::Entity r = row.Spawn();
		m_f.Label("Adressage IPv4").W(ui::Dimension::Px(110)).Parent(r).Spawn();
		m_f.Combo({"Lien local (169.254.x.x)", "Adresse fixe"}, 0)
			.GrowW()
			.Parent(r)
			.OnChange([this](float v) {
				m_adhoc.ipv4 =
					int(v) == 1 ? wifi::Ipv4Method::MANUAL : wifi::Ipv4Method::LINK_LOCAL;
				ui::SetEnabled(m_ar, m_addressInput, m_adhoc.ipv4 == wifi::Ipv4Method::MANUAL);
			})
			.Spawn();
	}
	m_addressInput =
		field("Adresse fixe", m_f.Input("10.42.0.N/24, unique par machine").MaxLen(18));
	ui::SetEnabled(m_ar, m_addressInput, false);
	m_passwordInput =
		field("Mot de passe", m_f.Input("vide : cellule ouverte (WPA2 : 8+ car.)").MaxLen(63));

	auto actions = m_f.Row();
	actions.Gap(6.f).GrowW().HAuto().Parent(c);
	ecs::Entity a = actions.Spawn();
	auto join = m_f.Button("Créer / rejoindre");
	join.GrowW()
		.Tooltip("Même SSID et même canal sur chaque ordinateur ; la connexion Wi-Fi en cours sera "
				 "coupée")
		.Parent(a)
		.OnClick([this] {
			if (m_opt.wifi && !WifiBusy())
				StartJoin();
		});
	m_joinButton = join.Spawn();
	auto leave = m_f.Button("Quitter la cellule");
	leave.GrowW()
		.Tooltip("Supprime le profil ad hoc ; NetworkManager reprend la connexion habituelle")
		.Parent(a)
		.OnClick([this] {
			if (m_opt.wifi && !WifiBusy())
				StartLeave();
		});
	m_leaveButton = leave.Spawn();

	m_f.Label("Journal (commandes et sorties, copiable)").Bold().Parent(c).Spawn();
	auto logArea = m_f.InputArea("");
	logArea.IoMode(ui::IOMode::READ_AND_COPY_ONLY)
		.GrowW()
		.H(ui::Dimension::Px(190))
		.FontSize(12.f)
		.Parent(c);
	logArea.Highlighter([](const String& line, std::vector<ui::UiTextSpan>& out) {
		if (line.StartsWith("$ "))
			out.push_back({0, line.size(), COL_ACCENT});
		else if (line.StartsWith("!"))
			out.push_back({0, line.size(), COL_WARN});
		else if (line.StartsWith("  sudo "))
			out.push_back({0, line.size(), COL_OK});
	});
	m_logArea = logArea.Spawn();
	return c;
}

ecs::Entity AdhocChatApp::BuildChatColumn(ecs::Entity parent) {
	auto col = m_f.Panel();
	col.Gap(6.f).Pad(10.f).GrowW().GrowH().Bg(COL_PANEL).Radius(6.f).Parent(parent);
	ecs::Entity c = col.Spawn();
	Section("Chat").Parent(c).Spawn();

	auto area = m_f.InputArea("Aucun message pour l'instant.");
	area.IoMode(ui::IOMode::READ_AND_COPY_ONLY).GrowW().GrowH().Parent(c);
	// « [hh:mm:ss] Pseudo : texte » — l'auteur en couleur ; les lignes
	// système (« * … ») en gris, les nôtres en vert.
	area.Highlighter([this](const String& line, std::vector<ui::UiTextSpan>& out) {
		size_t close = line.Find("] ");
		if (close == String::NPOS)
			return;
		out.push_back({0, close + 1, COL_MUTED});
		if (line.Substr(close + 2).StartsWith("* ")) {
			out.push_back({close + 2, line.size(), COL_MUTED});
			return;
		}
		size_t colon = line.Find(" : ", close);
		if (colon != String::NPOS) {
			bool mine = line.Substr(close + 2, colon - close - 2) == m_node.Nickname();
			out.push_back({close + 2, colon, mine ? COL_OK : COL_ACCENT});
		}
	});
	m_chatArea = area.Spawn();

	auto row = m_f.Row();
	row.Gap(6.f).GrowW().HAuto().Parent(c);
	ecs::Entity r = row.Spawn();
	auto input = m_f.Input("Message à tous les pairs — Entrée pour envoyer");
	input.GrowW().MaxLen(4000).Parent(r).OnSubmit([this](const String&) { SendMessage(); });
	m_messageInput = input.Spawn();
	m_f.Button("Envoyer").Parent(r).OnClick([this] { SendMessage(); }).Spawn();
	return c;
}

ecs::Entity AdhocChatApp::BuildPeersColumn(ecs::Entity parent) {
	auto col = m_f.Panel();
	col.Gap(6.f)
		.Pad(10.f)
		.W(ui::Dimension::Px(400))
		.GrowH()
		.Bg(COL_PANEL)
		.Radius(6.f)
		.Parent(parent);
	ecs::Entity c = col.Spawn();

	auto head = m_f.Row();
	head.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(c);
	ecs::Entity h = head.Spawn();
	Section("Pairs").GrowW().Parent(h).Spawn();
	m_f.Button("Sonder")
		.Tooltip("Diffuse une demande : les voisins répondent tout de suite")
		.Parent(h)
		.OnClick([this] {
			m_node.Probe();
			SetStatus("Sonde diffusée.", COL_MUTED);
		})
		.Spawn();

	auto list = m_f.Column();
	list.Gap(2.f)
		.Pad(4.f)
		.GrowW()
		.H(ui::Dimension::Px(210))
		.Scrollable()
		.Clip()
		.Bg(Rgb(22, 24, 34))
		.Parent(c);
	m_peerList = list.Spawn();

	auto manual = m_f.Row();
	manual.Gap(6.f).GrowW().HAuto().Parent(c);
	ecs::Entity m = manual.Spawn();
	auto host = m_f.Input("adresse IP du pair");
	host.GrowW().MaxLen(64).Parent(m);
	m_hostInput = host.Spawn();
	auto port = m_f.Input("port");
	port.W(ui::Dimension::Px(64)).MaxLen(5).Parent(m);
	m_portInput = port.Spawn();
	SetInputText(m_portInput, String::Format("%u", unsigned(chat::DEFAULT_TCP_PORT)));
	m_f.Button("Ajouter")
		.Tooltip("Joindre un pair directement (diffusion bloquée, autre sous-réseau)")
		.Parent(m)
		.OnClick([this] {
			String hostText = InputText(m_hostInput).Trim();
			int p = InputText(m_portInput).Trim().ToInt32();
			if (auto err = m_node.AddManualPeer(hostText, uint16_t(p > 0 && p < 65536 ? p : 0)))
				SetStatus("Pair manuel : " + *err, COL_ERR);
			else {
				SetStatus("Pair manuel ajouté : " + hostText, COL_OK);
				SetInputText(m_hostInput, "");
			}
		})
		.Spawn();

	m_f.Separator().Parent(c).Spawn();
	auto fhead = m_f.Row();
	fhead.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(c);
	ecs::Entity fh = fhead.Spawn();
	Section("Fichiers").GrowW().Parent(fh).Spawn();
	m_f.Button("Envoyer…")
		.Tooltip("Choisir des fichiers ; ou les déposer sur la fenêtre")
		.Parent(fh)
		.OnClick([this] { PickFiles(); })
		.Spawn();
	auto target = Muted("");
	target.Parent(c);
	m_targetLabel = target.Spawn();

	auto transfers = m_f.Column();
	transfers.Gap(4.f).Pad(4.f).GrowW().GrowH().Scrollable().Clip().Bg(Rgb(22, 24, 34)).Parent(c);
	m_transferList = transfers.Spawn();

	auto bottom = m_f.Row();
	bottom.Gap(6.f).GrowW().HAuto().Parent(c);
	ecs::Entity b = bottom.Spawn();
	m_f.Button("Dossier de réception")
		.GrowW()
		.Tooltip(m_node.DownloadDir())
		.Parent(b)
		.OnClick([this] { sdl3::OpenUrl("file://" + m_node.DownloadDir()); })
		.Spawn();
	m_f.Button("Effacer les terminés")
		.GrowW()
		.Parent(b)
		.OnClick([this] { m_node.ClearFinishedTransfers(); })
		.Spawn();
	return c;
}

// ── Wi-Fi ────────────────────────────────────────────────────────────────────

void AdhocChatApp::Log(const String& text) {
	if (!m_log.IsEmpty())
		m_log.Append('\n');
	m_log.Append(text);
	if (auto area = m_ar.GetComponent<ui::UiInputArea>(m_logArea); area.IsSome()) {
		area.Unwrap()->text = m_log;
		area.Unwrap()->cursor = area.Unwrap()->selectionAnchor = m_log.size();
		m_gui.Layout().MarkDirty();
	}
}

void AdhocChatApp::StartProbe() {
	SetText(m_wifiState, "Lecture de la configuration Wi-Fi…");
	uint16_t udp = m_node.DiscoveryPort();
	uint16_t tcp = m_opt.node.tcpPort;
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
	SetText(m_scanButton, "Scan…");
	SetStatus("Balayage radio en cours…", COL_MUTED);
	m_scanJob.Start([dev] { return wifi::Scan(dev, true); });
}

void AdhocChatApp::ReadAdhocForm() {
	m_adhoc.ssid = InputText(m_ssidInput).Trim();
	m_adhoc.channel = InputText(m_channelInput).Trim().ToInt32();
	m_adhoc.address = InputText(m_addressInput).Trim();
	m_adhoc.password = InputText(m_passwordInput);
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
	Log(String::Format("\n— Cellule « %s », canal %d (%d MHz) sur %s", m_adhoc.ssid.CStr(),
					   m_adhoc.channel, wifi::ChannelToFrequencyMHz(m_adhoc.channel),
					   m_adhoc.device.CStr()));
	SetText(m_wifiState, "Création / adhésion à la cellule… (jusqu'à 30 s)");
	SetColor(m_wifiState, COL_WARN);
	m_configIsJoin = true;
	wifi::AdhocConfig cfg = m_adhoc;
	m_configJob.Start([cfg] { return wifi::JoinAdhoc(cfg); });
}

void AdhocChatApp::StartLeave() {
	ReadAdhocForm();
	Log("\n— Départ de la cellule « " + m_adhoc.ssid + " »");
	SetText(m_wifiState, "Départ de la cellule…");
	SetColor(m_wifiState, COL_WARN);
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
		for (auto& d : m_probe.devices)
			Log(String::Format("Interface %s : %s%s%s", d.name.CStr(), d.state.CStr(),
							   d.connection.IsEmpty() ? "" : " — ", d.connection.CStr()));
		if (m_probe.firewall) {
			Log("! Pare-feu " + m_probe.firewall->name +
				" actif : il bloque en général la découverte et les connexions des pairs. Pour "
				"ouvrir les ports :");
			for (auto& cmd : m_probe.firewall->commands)
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
		SetText(m_scanButton, "Scanner");
		if (*scan) {
			m_networks = std::move(scan->Value());
			size_t adhoc = size_t(std::count_if(m_networks.begin(), m_networks.end(),
												[](auto& n) { return n.IsAdhoc(); }));
			SetStatus(String::Format("%zu réseau(x) visible(s), dont %zu cellule(s) ad hoc.",
									 m_networks.size(), adhoc),
					  COL_OK);
		} else {
			SetStatus("Scan : " + scan->Error(), COL_ERR);
			Log("! Scan : " + scan->Error());
		}
		RebuildNetworks();
	}
	if (auto report = m_configJob.Take()) {
		for (auto& line : report->log)
			Log(line);
		if (report->ok) {
			m_joined = m_configIsJoin;
			Log(m_configIsJoin
					? "Cellule active. Les pairs apparaissent dès que leurs balises arrivent."
					: "Profil ad hoc supprimé.");
			SetStatus(m_configIsJoin ? "Cellule ad hoc active." : "Cellule quittée.", COL_OK);
			m_node.Probe();
		} else {
			Log("! Échec : " + report->error);
			SetStatus("Wi-Fi : " + report->error, COL_ERR);
			if (report->permissionDenied) {
				Log("! Droits insuffisants pour NetworkManager. Commandes à lancer en "
					"administrateur :");
				Log(wifi::ManualInstructions(m_adhoc));
			}
		}
		StartDeviceInfo();
		RefreshWifiStatus();
	}
	// Adresses : une adresse lien local met quelques secondes à apparaître.
	if (m_opt.wifi && !m_adhoc.device.IsEmpty() && SDL_GetTicks() - m_deviceInfoAt > 5000)
		StartDeviceInfo();

	bool busy = WifiBusy();
	ui::SetEnabled(m_ar, m_joinButton, m_opt.wifi && !busy);
	ui::SetEnabled(m_ar, m_leaveButton, m_opt.wifi && !busy);
	ui::SetEnabled(m_ar, m_scanButton, m_opt.wifi && !busy);
}

void AdhocChatApp::RebuildDeviceCombo() {
	Clear(m_deviceHolder);
	if (m_probe.devices.empty()) {
		m_f.Label(m_opt.wifi ? "aucune interface Wi-Fi" : "—")
			.TextColor(COL_MUTED)
			.Parent(m_deviceHolder)
			.Spawn();
		return;
	}
	std::vector<String> names;
	int selected = 0;
	for (size_t i = 0; i < m_probe.devices.size(); ++i) {
		names.push_back(m_probe.devices[i].name);
		if (m_probe.devices[i].name == m_adhoc.device)
			selected = int(i);
	}
	m_f.Combo(names, selected)
		.GrowW()
		.Parent(m_deviceHolder)
		.OnChange([this](float v) {
			size_t i = size_t(v);
			if (i < m_probe.devices.size()) {
				m_adhoc.device = m_probe.devices[i].name;
				m_deviceInfoAt = 0;
				StartDeviceInfo();
			}
		})
		.Spawn();
}

void AdhocChatApp::RebuildNetworks() {
	Clear(m_networkList);
	if (m_networks.empty()) {
		m_f.Label("Aucun réseau (lancez un scan).")
			.TextColor(COL_MUTED)
			.Parent(m_networkList)
			.Spawn();
		return;
	}
	int index = 0;
	for (auto& n : m_networks) {
		String ssid = n.ssid.IsEmpty() ? String("(masqué)") : n.ssid;
		String text = String::Format("%s%-8s %s  · canal %d · %d %% · %s", n.inUse ? "● " : "",
									 wifi::WifiModeName(n.mode), ssid.CStr(), n.channel, n.signal,
									 n.IsOpen() ? "ouvert" : n.security.CStr());
		auto row = m_f.Selectable(text, index++);
		row.GrowW().TextEllipsis().Tooltip(
			n.bssid + " — " + String::Format("%d MHz, %s", n.frequencyMHz, n.rate.CStr()));
		if (n.IsAdhoc())
			row.TextColor(COL_OK);
		else if (n.inUse)
			row.TextColor(COL_ACCENT);
		wifi::WifiNetwork copy = n;
		row.Parent(m_networkList).OnClick([this, copy] {
			if (!copy.ssid.IsEmpty())
				SetInputText(m_ssidInput, copy.ssid);
			if (copy.channel >= 1 && copy.channel <= 14)
				SetInputText(m_channelInput, String::Format("%d", copy.channel));
			SetStatus(copy.IsAdhoc()
						  ? "Cellule ad hoc choisie : « Créer / rejoindre » pour y entrer."
						  : "Pas une cellule ad hoc : seuls le SSID et le canal sont repris.",
					  copy.IsAdhoc() ? COL_OK : COL_WARN);
		});
		row.Spawn();
	}
}

void AdhocChatApp::RefreshWifiStatus() {
	if (!m_opt.wifi) {
		SetText(m_wifiState, "Configuration Wi-Fi désactivée.");
		return;
	}
	if (!m_probe.available && !m_probeJob.Running()) {
		SetText(m_wifiState,
				"NetworkManager indisponible : configurez la cellule à la main (cf. journal).");
		SetColor(m_wifiState, COL_ERR);
		if (m_log.Find("iw dev") == String::NPOS)
			Log(wifi::ManualInstructions(m_adhoc));
		return;
	}
	if (m_deviceInfo.device == m_adhoc.device && m_deviceInfo.caps) {
		auto& caps = *m_deviceInfo.caps;
		SetText(
			m_capsLabel,
			String::Format("Capacités : ad hoc %s · IBSS chiffré (RSN) %s · 2,4 GHz %s · 5 GHz %s",
						   caps.adhoc ? "oui" : "NON", caps.ibssRsn ? "oui" : "non",
						   caps.band2GHz ? "oui" : "non", caps.band5GHz ? "oui" : "non"));
		SetColor(m_capsLabel, caps.adhoc ? COL_MUTED : COL_ERR);
	}
	SetText(m_ipLabel, m_deviceInfo.ipv4.empty()
						   ? String("Adresse IPv4 : aucune")
						   : "Adresse IPv4 : " + String::Join(m_deviceInfo.ipv4, ", "));
	String connection;
	for (auto& d : m_probe.devices)
		if (d.name == m_adhoc.device)
			connection = d.connection;
	if (m_configJob.Running())
		return;
	if (m_joined) {
		SetText(m_wifiState,
				"Dans la cellule « " + m_adhoc.ssid + " » (profil " + m_adhoc.ProfileName() + ").");
		SetColor(m_wifiState, COL_OK);
	} else {
		SetText(m_wifiState, connection.IsEmpty() ? String("Pas de cellule ad hoc active.")
												  : "Connexion actuelle : " + connection +
														" (pas une cellule de ce programme).");
		SetColor(m_wifiState, COL_MUTED);
	}
}

// ── Chat / pairs / fichiers ──────────────────────────────────────────────────

void AdhocChatApp::SendMessage() {
	String text = InputText(m_messageInput).Trim();
	if (text.IsEmpty())
		return;
	m_node.SendChat(text);
	SetInputText(m_messageInput, "");
	if (m_node.Peers().empty())
		SetStatus("Aucun pair pour l'instant : le message n'est parti chez personne.", COL_WARN);
}

void AdhocChatApp::PickFiles() {
	sdl3::dialog::ShowOpenFile(
		[this](const sdl3::DialogResult& r, int) {
			if (!r.Ok())
				return;
			std::lock_guard lock(m_pickedMutex);
			m_picked.insert(m_picked.end(), r.files.begin(), r.files.end());
		},
		m_window, {}, "", true);
}

void AdhocChatApp::OnDropFile(const String& path) {
	SendFileToTargets(path);
}

void AdhocChatApp::SendFileToTargets(const String& path) {
	std::vector<chat::PeerInfo> targets;
	for (auto& p : m_node.Peers())
		if (p.id != 0 && (m_selectedPeer == 0 || p.id == m_selectedPeer))
			targets.push_back(p);
	if (targets.empty()) {
		SetStatus("Aucun pair identifié à qui envoyer « " + path + " ».", COL_WARN);
		return;
	}
	int ok = 0;
	for (auto& p : targets) {
		auto res = m_node.SendFile(p.id, path);
		if (res)
			++ok;
		else
			SetStatus("Envoi à " + p.nickname + " : " + res.Error(), COL_ERR);
	}
	if (ok)
		SetStatus(String::Format(
					  "Envoi de « %s » à %d pair(s).",
					  String(std::filesystem::path(path.CStr()).filename().string()).CStr(), ok),
				  COL_OK);
}

void AdhocChatApp::RefreshChat() {
	auto& msgs = m_node.Messages();
	// Le nombre de messages plafonne (les plus anciens partent) : la
	// signature inclut donc le dernier message.
	String sig = msgs.empty() ? String("0")
							  : String::Format("%zu|%llu|", msgs.size(),
											   (unsigned long long)msgs.back().timeMs) +
									msgs.back().text;
	if (sig == m_chatSignature)
		return;
	m_chatSignature = sig;
	String text;
	for (auto& m : msgs) {
		if (!text.IsEmpty())
			text.Append('\n');
		text.Append("[" + ClockOf(m.timeMs) + "] ");
		if (m.system)
			text.Append("* " + m.text);
		else
			text.Append((m.author.IsEmpty() ? String("?") : m.author) + " : " +
						m.text.Replace("\n", "\n    "));
	}
	if (auto area = m_ar.GetComponent<ui::UiInputArea>(m_chatArea); area.IsSome()) {
		area.Unwrap()->text = text;
		area.Unwrap()->cursor = area.Unwrap()->selectionAnchor = text.size();
		m_gui.Layout().MarkDirty();
	}
}

void AdhocChatApp::RefreshPeers(bool force) {
	auto peers = m_node.Peers();
	String sig = String::Format("%llu|", (unsigned long long)m_selectedPeer);
	for (auto& p : peers)
		sig.Append(String::Format("%llu/%s/%s/%d/%s;", (unsigned long long)p.id, p.nickname.CStr(),
								  p.address.CStr(), int(p.connected), p.status.CStr()));
	if (!force && sig == m_peersSignature)
		return;
	m_peersSignature = sig;
	if (m_selectedPeer != 0 &&
		std::none_of(peers.begin(), peers.end(), [&](auto& p) { return p.id == m_selectedPeer; }))
		m_selectedPeer = 0;

	Clear(m_peerList);
	auto all = m_f.Selectable(String::Format("Tous les pairs (%zu)", peers.size()), 0);
	all.GrowW().Parent(m_peerList).OnClick([this] {
		m_selectedPeer = 0;
		RefreshPeers(true);
	});
	if (m_selectedPeer == 0)
		all.Bg(Rgb(48, 62, 96));
	all.Spawn();
	int index = 1;
	for (auto& p : peers) {
		auto row = m_f.Column();
		row.Gap(1.f)
			.Pad(math::Sides{6.f, 3.f})
			.GrowW()
			.HAuto()
			.Radius(4.f)
			.Bg(p.id == m_selectedPeer ? Rgb(48, 62, 96) : COL_ROW);
		row.Parent(m_peerList);
		ecs::Entity r = row.Spawn();
		auto name = m_f.Selectable((p.connected ? "● " : "○ ") +
									   (p.nickname.IsEmpty() ? String("?") : p.nickname) +
									   (p.manual ? "  (manuel)" : ""),
								   index++);
		uint64_t id = p.id;
		name.GrowW().TextColor(p.connected ? COL_OK : COL_WARN).Parent(r).OnClick([this, id] {
			if (id == 0) {
				SetStatus("Pair pas encore identifié : attendez sa réponse.", COL_WARN);
				return;
			}
			m_selectedPeer = id;
			RefreshPeers(true);
		});
		name.Spawn();
		m_f.Label(
			   String::Format("%s:%u — %s", p.address.CStr(), unsigned(p.tcpPort), p.status.CStr()))
			.TextColor(COL_MUTED)
			.FontSize(12.f)
			.TextEllipsis()
			.GrowW()
			.Parent(r)
			.Spawn();
	}
	if (peers.empty())
		m_f.Label("Aucun pair : lancez le programme sur un autre ordinateur de la cellule.")
			.TextColor(COL_MUTED)
			.TextWrap()
			.GrowW()
			.Parent(m_peerList)
			.Spawn();

	String target = "Destinataire des fichiers : tous les pairs identifiés";
	for (auto& p : peers)
		if (p.id == m_selectedPeer && m_selectedPeer != 0)
			target = "Destinataire des fichiers : " + p.nickname;
	SetText(m_targetLabel, target + " — ou déposez un fichier sur la fenêtre.");
}

void AdhocChatApp::RefreshTransfers(bool force) {
	auto& ts = m_node.Transfers();
	String sig;
	for (auto& t : ts)
		sig.Append(String::Format("%u/%d;", t.id, int(t.state)));
	uint64_t now = SDL_GetTicks();
	if (force || sig != m_transfersSignature) {
		m_transfersSignature = sig;
		Clear(m_transferList);
		m_progressBars.clear();
		m_progressLabels.clear();
		if (ts.empty())
			m_f.Label("Aucun transfert.").TextColor(COL_MUTED).Parent(m_transferList).Spawn();
		for (auto it = ts.rbegin(); it != ts.rend(); ++it) {
			const chat::TransferInfo& t = *it;
			auto card = m_f.Column();
			card.Gap(3.f).Pad(6.f).GrowW().HAuto().Radius(4.f).Bg(COL_ROW).Parent(m_transferList);
			ecs::Entity c = card.Spawn();
			auto head = m_f.Row();
			head.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(c);
			ecs::Entity h = head.Spawn();
			sdl3::FColor color = t.state == chat::TransferState::DONE	   ? COL_OK
								 : t.state == chat::TransferState::RUNNING ? COL_ACCENT
																		   : COL_ERR;
			m_f.Label((t.outgoing ? "↑ " : "↓ ") + t.fileName + (t.outgoing ? " → " : " ← ") +
					  t.peerName)
				.TextColor(color)
				.TextEllipsis()
				.GrowW()
				.Tooltip(t.path)
				.Parent(h)
				.Spawn();
			if (t.state == chat::TransferState::RUNNING) {
				uint32_t id = t.id;
				m_f.Button("Annuler")
					.Parent(h)
					.OnClick([this, id] { m_node.CancelTransfer(id); })
					.Spawn();
			}
			auto bar = m_f.Progress(0.f, 1.f, t.Progress());
			bar.GrowW().Parent(c);
			m_progressBars.push_back({t.id, bar.Spawn()});
			auto label = m_f.Label("");
			label.TextColor(COL_MUTED).FontSize(12.f).TextEllipsis().GrowW().Parent(c);
			m_progressLabels.push_back({t.id, label.Spawn()});
		}
	}
	// Progression mise à jour en place (sans reconstruire les widgets).
	for (auto& t : ts) {
		for (auto& [id, e] : m_progressBars)
			if (id == t.id)
				if (auto p = m_ar.GetComponent<ui::UiProgress>(e); p.IsSome())
					p.Unwrap()->value = t.Progress();
		for (auto& [id, e] : m_progressLabels) {
			if (id != t.id)
				continue;
			String line =
				String::Format("%s / %s · %s/s · %s", HumanSize(double(t.done)).CStr(),
							   HumanSize(double(t.size)).CStr(), HumanSize(t.Rate(now)).CStr(),
							   chat::TransferStateName(t.state));
			if (!t.error.IsEmpty())
				line.Append(" : " + t.error);
			SetText(e, line);
		}
	}
}

void AdhocChatApp::RefreshHeader() {
	String addrs;
	for (auto& a : m_node.LocalAddresses()) {
		if (a.Contains(':'))
			continue; // IPv6 : liste trop longue pour l'en-tête
		addrs.Append(addrs.IsEmpty() ? "" : ", ");
		addrs.Append(a);
	}
	SetText(m_headerInfo, String::Format("Moi : %s · TCP %u · découverte UDP %u · adresses : %s",
										 m_node.Nickname().CStr(), unsigned(m_node.TcpPort()),
										 unsigned(m_node.DiscoveryPort()),
										 addrs.IsEmpty() ? "aucune" : addrs.CStr()));
}

void AdhocChatApp::SetStatus(const String& text, sdl3::FColor color) {
	m_frame.SetStatus(text, color);
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
		for (auto& p : picked)
			SendFileToTargets(p);
	}
	bool changed = m_node.Revision() != m_lastRevision;
	RefreshChat();
	if (changed) {
		RefreshPeers(false);
		RefreshTransfers(false);
		RefreshHeader();
		m_lastRevision = m_node.Revision();
	}
	// Débit et états visibles même sans changement (une fois par seconde).
	uint64_t now = SDL_GetTicks();
	if (now - m_lastSlowRefresh > 1000) {
		m_lastSlowRefresh = now;
		RefreshTransfers(false);
	}
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char** argv) {
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
	chat::ChatNode& node = *nodeRes.Value();

	auto fontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
	if (!fontRes) {
		std::cerr << "Font: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto& font = fontRes.Value();
	auto winRes =
		sdl3::Window::Create(u8"Chat Wi-Fi ad hoc - SDL3 Wrapper", WIN_W, WIN_H,
							 ui::WindowFrame::WINDOW_FLAGS | sdl3::window_flags::TRANSPARENT);
	if (!winRes) {
		std::cerr << "Window: " << winRes.Error().CStr() << "\n";
		return 1;
	}
	auto& window = winRes.Value();
	auto renRes = sdl3::Renderer::Create(window);
	if (!renRes) {
		std::cerr << "Renderer: " << renRes.Error().CStr() << "\n";
		return 1;
	}
	auto& ren = renRes.Value();
	auto engRes = sdl3::TextEngine::Create(ren);
	if (!engRes) {
		std::cerr << "TextEngine: " << engRes.Error().CStr() << "\n";
		return 1;
	}
	auto& eng = engRes.Value();

	ecs::ArchetypeRegistry ar;
	ui::Ui gui(ar, window, ren);
	gui.SetTextEngine(eng, font);
	gui.Layout().measureText = [&font](const String& s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};

	AdhocChatApp app(ar, gui, window, node, std::move(options));
	app.Build();

	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - lastTick) / 1000.f;
		lastTick = now;
		while (auto ev = sdl3::PollEvent()) {
			auto& e = ev.Value();
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
		gui.Tick(dt);
		ren.SetDrawColor(sdl3::FColor::TRANSPARENT());
		ren.Clear();
		gui.Render();
		ren.Present();
		SDL_Delay(5); // le réseau est sondé à chaque image : inutile de tourner à vide
	}
	return 0;
}
