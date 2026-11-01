# net — réseau local : découverte, chat et fichiers ; Wi-Fi ad hoc

`libs/infrastructure/net` · couche **infrastructure** · `libnet.a` · espaces de noms `net::chat` et `net::wifi`

## Présentation

`net` repose sur SDL_net, à travers les enveloppes de
[`sdl3/net.hpp`](libs_infrastructure_sdl3.md). Il fournit deux briques :

- **`net::chat`** : découvrir les ordinateurs voisins d'un réseau local et
  échanger avec eux messages et fichiers. Tout est **non bloquant** et sans
  fil d'exécution : l'application appelle `ChatNode::Poll(now)` à chaque
  image.
- **`net::wifi`** : lister les réseaux Wi-Fi et créer ou rejoindre une
  cellule **IBSS (ad hoc)** sous Linux, en pilotant NetworkManager
  (`nmcli`).

Le module est utilisé par l'exemple [adhoc_chat_demo](example_adhoc_chat_demo.md).

## Architecture interne

| Fichier | Contenu |
|---|---|
| `adhoc_chat.hpp`, `src/adhoc_chat.cpp` | Ports par défaut, trames (`FrameType`, `Frame`, `FrameDecoder`, `ByteWriter`, `ByteReader`), balises (`Beacon`), `NodeConfig`, `PeerInfo`, `ChatMessage`, `TransferInfo`, `ChatNode` |
| `wifi.hpp`, `src/wifi.cpp` | `CommandRunner`, `RunCommand`, `WifiDevice`, `WifiNetwork`, `WifiCapabilities`, `AdhocConfig`, analyse des sorties de `nmcli`, commandes, `Scan`, `JoinAdhoc`, `LeaveAdhoc`, `DetectFirewall` |

Dépendances :

- `core` ;
- `sdl3` : `NetContext`, `TcpSocket`, `UdpSocket`, `Process` ;
- `archive` : CRC-32 des fichiers transférés.

## Fonctionnement de `net::chat`

- **Découverte (UDP, port 48620 par défaut)**
  - Chaque nœud diffuse toutes les `announceIntervalMs` (2 s) une balise :
    identifiant aléatoire de 64 bits, pseudo et port TCP.
  - La balise part vers l'adresse de diffusion générale et vers celle de
    chaque sous-réseau (169.254.255.255 en lien local). Une cellule ad hoc
    sans route par défaut la reçoit donc aussi.
  - Un pair muet depuis `peerTimeoutMs` (8 s) est oublié.
  - `Probe()` demande aux voisins de se signaler tout de suite.
  - `AddManualPeer(hôte, port)` contourne la diffusion quand elle est
    bloquée.
- **Échanges (TCP, port 48621 par défaut, puis +1…+31 s'il est occupé)**
  - Chaque nœud ouvre une connexion sortante vers chaque pair.
  - Le flux est découpé en trames : `"AH"`, type, longueur sur 32 bits
    petit-boutiste, charge. La première trame est `HELLO`.
- **Fichiers**
  - Trames `FILE_BEGIN`, `FILE_CHUNK` (32 Kio) et `FILE_END` (avec CRC-32).
  - L'envoi est régulé sur les écritures en attente : la mémoire reste bornée.
  - Le récepteur écrit `nom.part`, vérifie la taille et le CRC, puis renomme
    sans écraser un fichier existant (`nom (2).ext`).
  - Le nom reçu est réduit à son dernier composant : un pair ne peut pas
    écrire hors de `downloadDir`.

> Le trafic n'est **pas chiffré** (au mieux par le WPA2 de la cellule), et
> les fichiers sont acceptés automatiquement : c'est une démonstration.

## API publique

### `ChatNode`

| Membre | Rôle |
|---|---|
| `static Result<unique_ptr<ChatNode>, String> Start(NodeConfig)` | Ouvre les sockets |
| `Poll(nowMs)` | À chaque image : découverte, connexions, réception, envoi des fichiers |
| `SendChat(texte)`, `SendFile(pair, chemin)` → id de transfert, `CancelTransfer`, `ClearFinishedTransfers` | Échanges |
| `AddManualPeer(hôte, port)`, `Probe()`, `SetNickname` | Pairs |
| `Peers()`, `Messages()`, `Transfers()`, `Revision()` | État, avec `Revision` qui change à chaque modification (rafraîchissement de l'UI) |
| `Id()`, `TcpPort()`, `DiscoveryPort()`, `DownloadDir()`, `LocalAddresses()` | Identité |

`NodeConfig` : `nickname`, `discoveryPort`, `tcpPort`, `downloadDir`,
`announceIntervalMs`, `peerTimeoutMs`, `broadcast` (faux = pairs manuels
uniquement).

### `net::wifi`

- **Analyse pure**, testée sans `nmcli` :
  - `SplitTerseLine`, `ParseDevices`, `ParseNetworks` ;
  - `ParseCapabilities`, `ParseIpv4Addresses` ;
  - `ChannelToFrequencyMHz`, `IsPermissionError`.
- **Commandes** :
  - `AdhocCommands(config)` rend les commandes à lancer ;
  - `CommandLine(argv, hideSecrets)` les affiche, mot de passe masqué ;
  - `ManualInstructions(config)` rend l'équivalent pour un administrateur,
    en `nmcli` avec sudo ou en `iw` + `ip`.
- **Exécution, bloquante** (à appeler depuis un fil de travail) :
  - `IsAvailable`, `Capabilities`, `Scan` ;
  - `JoinAdhoc` et `LeaveAdhoc` rendent un `ConfigureReport` ;
  - `DetectFirewall` détecte un pare-feu.

Chaque fonction prend un `CommandRunner` (par défaut `RunCommand`, avec
`LC_ALL=C`), remplaçable dans les tests.

Contraintes : passer en ad hoc **coupe la connexion Wi-Fi en cours**, car
une carte n'a qu'une radio. Les droits dépendent de polkit : en cas de refus,
`IsPermissionError` le détecte et `ManualInstructions` donne les commandes à
lancer en administrateur.

## Exemple

Deux nœuds dans le même processus, reliés par la boucle locale :

```cpp
#include <cstdio>
#include <net/adhoc_chat.hpp>
#include <sdl3/sdl3.hpp>

int main() {
	auto sdl = sdl3::SdlContext::Create(0);
	auto netCtx = sdl3::NetContext::Create(); // NET_Init / NET_Quit
	if (!sdl || !netCtx)
		return 1;

	// Deux nœuds dans le même processus, sans diffusion : pair ajouté à la main
	net::chat::NodeConfig a{.nickname = "Ada", .tcpPort = 47100, .downloadDir = "recus_ada", .broadcast = false};
	net::chat::NodeConfig b{.nickname = "Bob", .tcpPort = 47200, .downloadDir = "recus_bob", .broadcast = false};
	auto ada = net::chat::ChatNode::Start(a);
	auto bob = net::chat::ChatNode::Start(b);
	if (!ada || !bob) {
		std::fprintf(stderr, "démarrage impossible\n");
		return 1;
	}
	(void)ada.Value()->AddManualPeer("127.0.0.1", bob.Value()->TcpPort());
	(void)bob.Value()->AddManualPeer("127.0.0.1", ada.Value()->TcpPort());

	bool sent = false;
	for (int i = 0; i < 300; ++i) { // Poll non bloquant, à chaque « image »
		uint64_t now = sdl3::GetTicksMS();
		ada.Value()->Poll(now);
		bob.Value()->Poll(now);
		auto peers = ada.Value()->Peers();
		if (!sent && !peers.empty() && peers[0].connected) {
			ada.Value()->SendChat("Bonjour Bob !");
			sent = true;
		}
		sdl3::DelayMS(10);
	}
	for (const net::chat::ChatMessage &m : bob.Value()->Messages())
		std::printf("[%s] %s : %s\n", m.system ? "système" : "chat", m.author.CStr(), m.text.CStr());
	return 0;
}
```

Sortie :

```
[système]  : Ada a répondu à l'adresse 127.0.0.1
[chat] Ada : Bonjour Bob !
```

## Intégration

`modules = ["net"]`.

1. Créez un `sdl3::NetContext` (`NET_Init`) avant le premier nœud.
2. Appelez `Poll` à chaque image depuis le fil principal.
3. Ouvrez dans le pare-feu le port UDP de découverte et la plage TCP.

## Tests

```sh
cpp-maker test -m net
```

`test_net_adhoc_chat.cpp` couvre :

- le codage des trames et des balises ;
- l'analyse des sorties de `nmcli` ;
- les commandes ad hoc ;
- un échange réel sur la boucle locale.
