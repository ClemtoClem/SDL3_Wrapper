# Chat Wi-Fi ad hoc (IBSS) — `adhoc_chat_demo`

Démonstration (2026-10-06) : plusieurs ordinateurs reliés par une cellule
Wi-Fi IBSS (« ad hoc »), sans point d'accès ni serveur, se découvrent et
s'échangent messages et fichiers. Toute la logique est dans `lib/` ; l'exemple
n'est que l'interface.

```
./build/debug/adhoc_chat_demo [--nick NOM] [--discovery-port N] [--tcp-port N]
    [--download-dir DOSSIER] [--peer HÔTE[:PORT]]... [--no-broadcast] [--no-wifi] [--device IFACE]
```

## Ce qui a été ajouté à la bibliothèque

| Fichier | Rôle |
|---|---|
| `sdl3/net.hpp` (étendu) | `Send`/`Receive(std::span)` corrigés (ils appelaient `send`/`receive` inexistants) ; `UdpOptions` (diffusion, réutilisation du port), `UdpSocket::Broadcast`, `WaitInput` ; `ReceivedDatagram::sender` (adresse réutilisable pour répondre) ; `TcpSocket::Status`/`PendingWrites`/`WaitDrained`/`RemoteAddress` ; `IpAddress::Share`/`Clone`/`Status`/`Bytes`/`IsIpv4`/`SameAs`/`LocalAddresses`. |
| `net/wifi.hpp` | Wi-Fi par NetworkManager (`nmcli`) : interfaces, capacités (ad hoc, IBSS-RSN), scan (sortie *terse* échappée), création / adhésion / départ d'une cellule (IPv4 lien local ou fixe, WPA2 facultatif), journal des commandes, refus de droits reconnu + commandes manuelles (`sudo nmcli` ou `iw` + `ip`), détection du pare-feu (ufw, firewalld). Exécuteur de commandes injectable : tout est testé sans `nmcli`. |
| `net/adhoc_chat.hpp` | `ChatNode` : découverte par balises UDP, connexions TCP par pair, trames (`AH`, type, longueur), chat, fichiers (32 Kio par trame, CRC-32, `.part` puis renommage sans écrasement, nom reçu assaini), annulation dans les deux sens, départ (`BYE`). Sans fil d'exécution : `Poll()` à chaque image. |
| `ui/chrome.hpp` (étendu) | `TitleBar(f, window, TitleBarOptions, parent)` : icône d'application, titre, poignée de déplacement, réduire / agrandir-restaurer / fermer en icônes MaterialIcons (repli texte sans police), `TitleBarWidgets::Update` (icône selon l'état RÉEL de la fenêtre), `SetTitle`, `Buttons()`. `StatusBar` (texte + poignée ↘), `SpawnIconButton`, `OpenMaterialIconFont`, `WindowChrome::AddResizeGrip` et `Attach(window, …, titleBar, &statusBar)`, `RenderSystem::HasFont`. L'ancienne forme `TitleBar(f, window, titre, onClose)` est conservée (exemples Aero inchangés). |

## Protocole

- **Découverte** (UDP, 48620) : balise `AHCB` v1 (annonce / sonde / départ,
  identifiant 64 bits, port TCP, pseudo) toutes les 2 s ; pair oublié après
  8 s sans balise ni connexion entrante. Envoyée en diffusion ET vers
  l'adresse de diffusion du sous-réseau de chaque interface
  (`169.254.255.255` en lien local, `a.b.c.255` sinon) — indispensable, voir
  plus bas.
- **Échanges** (TCP, 48621 puis +1… si occupé) : chaque nœud écrit sur SA
  connexion sortante vers un pair et lit les connexions entrantes ; première
  trame `HELLO`. Un pair manuel (`--peer`, bouton « Ajouter ») est identifié
  à sa réponse ; s'il désigne le programme lui-même, il est retiré.
- **Envoi régulé** sur `PendingWrites` (256 Kio en vol au plus) : la mémoire
  reste bornée quelle que soit la taille du fichier.

## Pièges rencontrés

- `FColor{150, 156, 178}` donne une couleur **transparente** (composantes
  bornées à 1, alpha par défaut 0) : la démo utilise `Rgb(r, g, b)`.
- **SDL_net, socket lié à « toutes les adresses »** : `FindBroadcastAddress`
  ne trouve pas d'interface pour `0.0.0.0`, donc `Broadcast()` (adresse NULL)
  ne part qu'en multicast IPv6 `ff02::1`. D'où les envois dirigés vers
  `x.x.x.255`.
- **Fuite dans SDL_net** (`SDL_net.c`, `FindBroadcastAddress`) : l'adresse
  `iface` n'est libérée que sur le chemin d'erreur. Tout socket UDP ouvert
  avec `allowBroadcast` perd une adresse par handle (247 octets signalés par
  ASan à la sortie de la démo). Correctif d'une ligne, hors de ce dépôt :
  `NET_UnrefAddress(iface);` avant `return retval;`. Le test unitaire n'ouvre
  pas de socket de diffusion et reste propre.
- **Pare-feu** : avec ufw actif (`DEFAULT_INPUT_POLICY="DROP"`), même ses
  propres diffusions ne reviennent pas ; la démo le détecte et affiche
  `sudo ufw allow 48620/udp` et `sudo ufw allow 48621:48652/tcp`.
- Passer en ad hoc **coupe la connexion Wi-Fi en cours** (une seule radio) ;
  « Quitter la cellule » supprime le profil et rend l'interface à
  NetworkManager.

## Vérifications

- `tests/net_adhoc_chat_smoke_test.cpp` (8 tests) : analyse nmcli, commandes,
  refus de droits simulé, trames découpées arbitrairement, balises, noms de
  fichiers ; deux nœuds réels en boucle locale (identification, chat dans les
  deux sens, renommage, fichier de 1 Mo vérifié octet par octet, pas
  d'écrasement, annulation par le destinataire, départ) ; pair manuel
  désignant le nœud lui-même.
- `tests/ui_title_bar_smoke_test.cpp` (4 tests) : boutons à icône (glyphe
  traversant, centré), repli texte, boutons facultatifs, forme historique,
  placement après layout, `onClose`, barre d'état et poignée.
- Sous Xvfb + xfwm4, deux instances : chat, départ détecté, scan réel
  (50 réseaux), déplacement par la barre de titre, redimensionnement par la
  poignée, agrandir / restaurer (icône qui bascule), réduire, fermer.
- Non vérifié ici : la création réelle d'une cellule (elle aurait coupé le
  Wi-Fi de la machine de développement) et le dépôt de fichier sur la
  fenêtre en interface (le transfert lui-même est couvert par le test).
