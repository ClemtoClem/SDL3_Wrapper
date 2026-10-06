#pragma once
/**
 * net::wifi — scanner les réseaux Wi-Fi et créer / rejoindre une cellule
 * IBSS (« ad hoc ») sous Linux, via NetworkManager (`nmcli`).
 *
 * SDL_net ne parle qu'IP : il ne sait ni lister les réseaux radio ni changer
 * le mode d'une carte. Ce module pilote donc `nmcli` (sous-processus
 * sdl3::Process) et analyse sa sortie « terse » (`-t -e yes` : champs séparés
 * par `:`, les `:` et `\` des valeurs échappés par `\`).
 *
 * Cellule IBSS : tous les ordinateurs utilisent le même SSID sur le même
 * canal ; le premier crée la cellule, les suivants la rejoignent — c'est la
 * même commande. L'adressage IPv4 est soit « lien local » (169.254.x.x,
 * choisi par chacun, sans serveur DHCP), soit une adresse fixe par machine
 * (10.42.0.N/24 par exemple). Une carte n'ayant qu'une radio, passer en ad
 * hoc coupe la connexion Wi-Fi en cours.
 *
 * Droits : NetworkManager autorise en général l'utilisateur de la session
 * locale (polkit) ; sinon `nmcli` répond « Not authorized » —
 * `IsPermissionError` le reconnaît et `ManualInstructions` donne les
 * commandes à lancer en administrateur (nmcli via sudo, ou iw + ip).
 *
 * Toutes les fonctions qui lancent `nmcli` prennent un `CommandRunner` : par
 * défaut `RunCommand` (processus réel), remplaçable dans les tests. Elles
 * BLOQUENT (un scan dure quelques secondes) : une interface les appelle
 * depuis un fil de travail.
 */
#include "core/core.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace net::wifi {

/// Sortie d'une commande : stdout et stderr mêlés, code de sortie.
struct CommandResult {
	int exitCode = -1;
	String output;

	[[nodiscard]] bool Ok() const noexcept { return exitCode == 0; }
};

/// Lance `argv` (argv[0] cherché dans le PATH) ; Err si le lancement même
/// échoue (programme absent…).
using CommandRunner = std::function<Result<CommandResult, String>(const std::vector<String>& argv)>;

/// Processus réel, environnement en `LC_ALL=C` pour une sortie non traduite.
[[nodiscard]] Result<CommandResult, String> RunCommand(const std::vector<String>& argv);

/// Une interface réseau Wi-Fi vue par NetworkManager.
struct WifiDevice {
	String name;	   ///< wlp0s20f3, wlan0…
	String state;	   ///< connected, disconnected, unavailable…
	String connection; ///< profil actif (vide : aucun)
};

/// Ce que la carte sait faire (WIFI-PROPERTIES de `nmcli device show`).
struct WifiCapabilities {
	bool adhoc = false;	  ///< mode IBSS
	bool ibssRsn = false; ///< IBSS chiffré WPA2 (RSN)
	bool band2GHz = false;
	bool band5GHz = false;
};

enum class WifiMode : uint8_t { INFRA, ADHOC, MESH, OTHER };

/// Un réseau vu au scan.
struct WifiNetwork {
	String ssid; ///< vide : réseau masqué
	String bssid;
	WifiMode mode = WifiMode::OTHER;
	int channel = 0;
	int frequencyMHz = 0;
	int signal = 0; ///< 0..100
	String rate;
	String security; ///< vide : ouvert
	bool inUse = false;

	[[nodiscard]] bool IsAdhoc() const noexcept { return mode == WifiMode::ADHOC; }
	[[nodiscard]] bool IsOpen() const noexcept { return security.IsEmpty() || security == "--"; }
};

[[nodiscard]] const char* WifiModeName(WifiMode mode) noexcept;

/// Comment chaque machine obtient son adresse IPv4 dans la cellule.
enum class Ipv4Method : uint8_t {
	LINK_LOCAL, ///< 169.254.x.x automatique (rien à saisir, plus lent à converger)
	MANUAL,		///< adresse fixe, unique par machine (ex. 10.42.0.N/24)
};

/// Paramètres de la cellule ad hoc.
struct AdhocConfig {
	String device; ///< interface Wi-Fi (vide : celle que choisit NetworkManager)
	String ssid = "adhoc-chat";
	int channel = 6; ///< 1..13 (2,4 GHz) — le même pour tous
	Ipv4Method ipv4 = Ipv4Method::LINK_LOCAL;
	String address;		   ///< MANUAL : « 10.42.0.N/24 »
	String password;	   ///< vide : cellule ouverte ; sinon WPA2 IBSS-RSN (8 caractères min.)
	String connectionName; ///< profil NetworkManager (vide : « adhoc-<ssid> »)

	[[nodiscard]] String ProfileName() const;
	/// NONE si cohérent, la raison sinon.
	[[nodiscard]] Option<String> Validate() const;
};

// ── Analyse (pure, testée sans nmcli) ─────────────────────────────────────

/// Découpe une ligne terse (`-t -e yes`) en champs, échappements retirés.
[[nodiscard]] std::vector<String> SplitTerseLine(StringView line);

/// Sortie de `nmcli -t -e yes -f DEVICE,TYPE,STATE,CONNECTION device status` :
/// seules les interfaces de TYPE wifi sont gardées.
[[nodiscard]] std::vector<WifiDevice> ParseDevices(StringView output);

/// Sortie de `nmcli -t -e yes -f IN-USE,BSSID,SSID,MODE,CHAN,FREQ,RATE,
/// SIGNAL,SECURITY device wifi list` ; triée : ad hoc d'abord, puis par
/// signal décroissant.
[[nodiscard]] std::vector<WifiNetwork> ParseNetworks(StringView output);

/// Sortie de `nmcli -t -f WIFI-PROPERTIES device show <dev>`.
[[nodiscard]] WifiCapabilities ParseCapabilities(StringView output);

/// Lignes `IP4.ADDRESS[n]:a.b.c.d/m` de `nmcli -t -f IP4.ADDRESS device show`.
[[nodiscard]] std::vector<String> ParseIpv4Addresses(StringView output);

[[nodiscard]] int ChannelToFrequencyMHz(int channel) noexcept;

/// La sortie de nmcli signale-t-elle un refus de droits (polkit, root) ?
[[nodiscard]] bool IsPermissionError(StringView output);

// ── Commandes ─────────────────────────────────────────────────────────────

/// Commandes qui créent puis activent le profil ad hoc (sans l'éventuelle
/// suppression préalable d'un profil de même nom).
[[nodiscard]] std::vector<std::vector<String>> AdhocCommands(const AdhocConfig& config);

/// Une commande en une ligne de shell (arguments entre quotes au besoin) —
/// pour l'affichage ; le mot de passe est masqué si `hideSecrets`.
[[nodiscard]] String CommandLine(const std::vector<String>& argv, bool hideSecrets = true);

/// Ce qu'un administrateur tape pour obtenir la même cellule à la main :
/// variante nmcli (sudo) et variante iw + ip.
[[nodiscard]] String ManualInstructions(const AdhocConfig& config);

// ── Exécution (bloquante) ─────────────────────────────────────────────────

/// nmcli est-il installé et NetworkManager joignable ?
[[nodiscard]] bool IsAvailable(const CommandRunner& run = RunCommand);

[[nodiscard]] Result<std::vector<WifiDevice>, String>
ListDevices(const CommandRunner& run = RunCommand);

[[nodiscard]] Result<WifiCapabilities, String> Capabilities(const String& device,
															const CommandRunner& run = RunCommand);

/// Adresses IPv4 actuelles de l'interface (« a.b.c.d/m »).
[[nodiscard]] Result<std::vector<String>, String>
Ipv4Addresses(const String& device, const CommandRunner& run = RunCommand);

/// Réseaux visibles. `rescan` : relance un balayage radio (quelques secondes)
/// au lieu de rendre le dernier résultat connu.
[[nodiscard]] Result<std::vector<WifiNetwork>, String> Scan(const String& device, bool rescan,
															const CommandRunner& run = RunCommand);

/// Bilan d'une opération de configuration.
struct ConfigureReport {
	bool ok = false;
	bool permissionDenied = false; ///< nmcli a refusé faute de droits
	std::vector<String> log;	   ///< « $ commande » puis sa sortie, dans l'ordre
	String error;				   ///< dernière erreur (vide si ok)
};

/// Crée la cellule ou la rejoint (même commande) : supprime un profil de même
/// nom, le recrée, l'active.
[[nodiscard]] ConfigureReport JoinAdhoc(const AdhocConfig& config,
										const CommandRunner& run = RunCommand);

/// Pare-feu actif sur la machine : il bloque en général la découverte
/// (diffusions UDP entrantes) et les connexions TCP des pairs.
struct FirewallInfo {
	String name;				  ///< « ufw », « firewalld »
	std::vector<String> commands; ///< à lancer en administrateur pour ouvrir les ports
};

/// Détecte ufw / firewalld (`systemctl is-active`, sans droits) ; NONE si
/// aucun n'est actif.
[[nodiscard]] Option<FirewallInfo> DetectFirewall(uint16_t udpPort, uint16_t tcpFirst,
												  uint16_t tcpLast,
												  const CommandRunner& run = RunCommand);

/// Désactive et supprime le profil ; NetworkManager reprend ensuite la
/// connexion habituelle de l'interface.
[[nodiscard]] ConfigureReport LeaveAdhoc(const AdhocConfig& config,
										 const CommandRunner& run = RunCommand);

} // namespace net::wifi
