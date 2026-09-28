---
name: project-emulator-demo
description: "examples/emulator_demo — émulateur NDS/GBA (cœur dérivé de NooDS) + GB/GBC, lancement direct depuis .zip/.tar/.tar.gz/.gz (nouveau data::archive), porté sur l'API actuelle du wrapper, placé dans l'espace de noms emulator_demo, pilotable en ligne de commande (mode sans écran, appuis scriptés, états, captures, rapport texte/JSON) ; 13 bugs réels du cœur et de la coquille trouvés et corrigés, 3 ajouts à lib/."
metadata:
  type: project
---

prompt :
```
Vérifie et corrige le code d'application de démonstration de la librairie "emulator_demo" afin de la faire
fonctionner et ajoute un espace de nom pour l'application "emulator_demo". Créer plusieurs options en ligne de
commande (comme pour level_editor_demo avec la classe CommandLine) pour que la démonstration soit testable et de
lui demander générer un rapport d'éxécution.
```

Travail du 2026-09-13. Le code venait du projet voisin `System_Projects/EmulOs`
(copie non versionnée) et **ne compilait plus du tout** : écrit contre l'ancienne
API snake_case/camelCase du wrapper (`is_ok`, `unwrap`, `fromFile`,
`get_component`, `sdl3::Color` dans `ui::`…), antérieure au renommage
clang-tidy et à la migration `ui::` vers `FColor`. Le `Makefile` ne savait pas
non plus construire un exemple fait de plusieurs unités de traduction.

## État de départ → état final

| | Avant | Après |
|---|---|---|
| Compilation | ~500 erreurs (app) + ~80 (cœur) | 0 erreur, 0 avertissement (`-Wextra -Werror`, -O2) |
| Espace de noms | cœur global, coquille `app::` | `emulator_demo::` (cœur, CLI, rapport) et `emulator_demo::app::` (coquille) |
| Exceptions | `throw CoreError`, `throw runtime_error`, `catch bad_alloc` | aucune : `Result`, drapeau `StateArchive::failed`, `new (std::nothrow)` |
| Tests | aucun | `tests/emulator_demo_smoke_test.cpp` (16 tests) + scénarios `--headless` |
| Sanitizers | — | ASan/UBSan/LSan propres sur NDS, GBA, GBC, sans écran et fenêtré |

## Architecture (examples/emulator_demo/)

```
emulator_demo.cpp          main : CommandLine::Parse -> Demo::Run
emulator/                  cœur (NooDS : cpu, gpu 2D/3D, spu, mémoire, cartouches, HLE BIOS ; gbc/)
app/cli.hpp                emulator_demo::CommandLine + boutons + actions scriptées
app/report.hpp             RunReport (texte / JSON), FrameStats, ThreadTracker, empreinte d'image
app/demo.{hpp,cpp}         modes (aide, listes, sans écran, fenêtré), réglages, vérifications, code de sortie
app/emulator_session.*     UNE partie : cœur, fil d'émulation, entrées, états, dernière image
app/game_view.*            affichage seul (textures, letterbox, écran tactile)
app/application.*          coquille fenêtrée « EmulOS » (titre, menus, panneau, HUD, journal)
app/modals.*               ROMs, états, configuration, triche, réseau, à propos
app/audio_output.hpp, gamepad_manager.hpp, net_bridge.hpp, log_console.hpp, rom_metadata.*
```

### La décision structurante : `EmulatorSession` extraite de `GameView`

L'ancien `GameView` mêlait cœur, fil d'émulation, audio, états ET rendu. La
session ne connaît plus ni fenêtre ni renderer, ce qui permet :

- **`--headless`** : exactement le même code, image par image sur le fil
  principal (`RunFrame()`), sans GPU ni audio, déterministe et à cadence libre ;
- **discipline de fils** : tout ce qui touche le cœur (boutons, trames réseau,
  sauvegardes demandées par l'interface) s'exécute sur le fil d'émulation ENTRE
  deux images. L'ancien code appliquait les boutons depuis le fil principal et
  verrouillait un mutex pendant toute l'image pour sauvegarder (famine possible,
  `std::mutex` n'est pas équitable). Seule la copie de l'image terminée passe
  sous verrou.

Le jeu est dessiné par un widget `ui::Canvas` : l'ordre de l'arbre garantit que
HUD et boîtes passent au-dessus, sans l'ancienne astuce du panneau central rendu
transparent. Les actions déclenchées par un rappel de widget (ouvrir/fermer une
boîte, changer de ROM) sont DIFFÉRÉES en fin de traitement des évènements
(`m_deferred`), et les rappels des sélecteurs de fichiers natifs passent par
`Application::Post()` (fil quelconque).

## Ligne de commande

Les numéros d'image sont des images ÉMULÉES : un scénario est identique en
fenêtré et sans écran, quelle que soit la cadence d'affichage.

- ROM/console : `--rom` (ou positionnel), `--config`, `--no-save-config`,
  `--bios-dir` (à plat OU rangé par console comme `EmulOs/bios-firmware`),
  `--boot=direct|bios`, `--arm7-hle`, `--threaded-2d/3d`, `--layout`,
  `--state-path`, `--save-dir`
- fenêtre : `--width/--height`, `--no-audio`, `--speed=normal|unlimited`,
  `--hide-hud`, `--open=roms|saves|config|cheats|network|about`
- pilotage : `--frames`, `--press=N:BOUTON[:DURÉE]`, `--save-state=N`, `--load-state=N`
- observation : `--report`, `--report-format=text|json`, `--screenshot=N:CHEMIN`,
  `--screenshot-every=K`, `--screenshot-dir`, `--require-video`, `--verbose`
- modes : `--headless`, `--rom-info`, `--list-roms[=DIR]`, `--list-buttons`, `--help`

Les réglages forcés en ligne de commande ne sont PAS réécrits dans le fichier de
réglages (restaurés avant `Settings::save`, sauf s'ils ont été changés depuis
l'interface) ; le mode sans écran n'écrit jamais ce fichier.

**Code de sortie** : 0 ; 1 si la ROM ne démarre pas ou si une vérification
échoue (capture non écrite ou jamais atteinte, opération d'état scriptée en
échec ou non exécutée, `--require-video` sur une image uniforme, émulation
bloquée en fenêtré) ; 2 si la ligne de commande est invalide.

## Rapport d'exécution

ROM (console détectée par les octets, titre, éditeur, icône, démarrage ou cause
d'échec), réglages effectifs (+ présence des BIOS/firmware), images émulées et
secondes de console, vitesse en % du temps réel, cadence mesurée par le cœur,
audio (blocs envoyés), fronts de boutons pilotés, opérations d'état, **empreinte
de la dernière image** (FNV-1a, couleurs distinctes, % de pixels non noirs,
nombre d'images distinctes — de quoi affirmer « le jeu affiche quelque chose »
sans image de référence), cadence et centiles des images (fenêtre, ou coût d'une
image émulée sans écran), fils de la démo simultanés (`ThreadTracker`) ET fils
du processus (`/proc/self/status`), boîtes ouvertes, captures, avertissements et
erreurs du journal SDL.

## Bugs réels trouvés et corrigés

Dans le cœur (hérités de NooDS ou du portage EmulOs) :

1. **GBA sans BIOS : faux démarrage puis gel.** La condition de `Core::Core`,
   réécrite lors du portage, laissait démarrer une ROM GBA seule sans
   `gba_bios.bin` en boot direct ; le cœur n'a pas de BIOS GBA en HLE, le
   premier SWI sautait dans une zone vide. Désormais `ERROR_GBA_BIOS`, message
   explicite.
2. **Boucle infinie `Memory::readFallback`** (GBA, bus ouvert) : relit
   l'instruction au PC ; PC lui-même hors mémoire → récursion que -O2 change en
   boucle infinie. C'est ce qui figeait le point 1.
3. **Horloge RTC lue hors tableau** (UBSan) : registre « heure seule » indexé
   `writeCount/8 - 5` (-4..-2) au lieu de `+3` → les jeux lisant l'heure
   (Pokémon Rubis) recevaient des octets voisins.
4. **GBC : image 6,5 % trop longue.** VBlank = un bloc de 4560 cycles avec LY
   figé, puis lignes 144-153 rejouées comme visibles : 74 784 cycles au lieu de
   70 224, STAT faux pendant ces lignes. Plus : durées de mode réaffectées au
   lieu d'être cumulées (débordement perdu à chaque transition). Mesuré : 90 %
   → 101 % du temps réel.
5. **Fichiers `.cht` corrompus** : `snprintf` dans `char buf[18]` pour
   `"%08X %08X\n"` (18 caractères + NUL) → saut de ligne perdu, lignes collées.
6. **Comportements indéfinis du cœur ARM** (UBSan) : `1 << 32` dans la boucle
   de coût des multiplications (`m < 4` testé après le décalage), rotations
   `(v << (32 - s)) | (v >> s)` avec `s == 0` → `std::rotr`.
7. **AES DSi** : `y` déclaré dans la boucle mais relu au tour suivant (indéfini).
8. **Déréférencement de nullptr** possible : `auxAddress < saveSize` en non
   signé avec `saveSize == -1` (aucune puce détectée) → vrai → `save[...]`.
   Et `memcpy(dst, nullptr, 0)` dans `resizeSave`.
9. **Chargement d'état corrompu** : l'exception laissait un cœur à moitié
   chargé ; désormais instantané de secours restauré si l'archive échoue.
10. **Titres GBC** : 16 octets lus même sur cartouche CGB (drapeau + code
    fabricant) → « 10-PIN BOWLAXPP\x80 ».

Dans la coquille :

11. **Audio non cadencé** : `PutData` ne bloque jamais, la file du flux
    grossissait sans fin (latence croissante) et le jeu tournait trop vite
    (134 % mesurés sur GBA). La pompe attend maintenant que la file redescende
    sous deux blocs — c'est cette attente qui cadence le cœur.
12. **Fuite TLS SDL des `std::jthread`** (LSan) : un fil non créé par SDL qui
    appelle `SDL_SetError` doit appeler `SDL_CleanupTLS()` avant de sortir.
13. **Journal fichier en échec à chaque démarrage** : dossier `<exe>/logs`
    jamais créé. Et libellés multi-lignes mesurés comme une seule ligne
    (chevauchements), taille de libellé figée sans `WAuto/HAuto`.

## Lancement depuis une archive et boîte ROMs (2026-09-13, seconde demande)

prompt :
```
Améliore l'émulateur de système gbc, gba et nds afin de pouvoir directement extraire la rom d'une archive
(.tar, .tar.gz, .zip) et visionner les informations de la rom dans la ModalFrame ROMs
```

> **Remplacé le même jour** par le module modulaire décrit dans
> [[project-data-archive]] (zip/7z/tar.xz/iso/xz, écriture, AES-256,
> navigation) ; `rom_source` utilise désormais la façade `data::archive::Archive`
> et l'option `--archive-password`. Le paragraphe ci-dessous décrit la première
> version, en lecture seule.

**Nouveau module de bibliothèque `lib/include/data/archive.hpp`** (`data::archive`),
plutôt que lier zlib/libarchive (présents sur la machine) : le dépôt fait
croître sa propre bibliothèque, et un décodeur maison est testable et sans
exception.
- `Inflate` (RFC 1951 complet : stocké, Huffman fixe et dynamique ; table
  rapide 10 bits + parcours canonique « puff » ; plafond de taille contre les
  bombes de décompression), `Crc32`, `Gunzip` (RFC 1952, CRC et taille vérifiés).
- `Archive::FromBytes/Open` : détection par les OCTETS (zip, gzip→tar ou fichier
  seul, tar au checksum valide), entrées (nom, tailles, CRC, méthode), `Extract`
  avec CRC vérifié (zip, gzip). zip : répertoire central + en-tête local
  (longueurs d'extra différentes). tar : ustar/prefix, noms longs GNU `L`,
  en-têtes pax `path=`.
- Refus explicites : ZIP64, zip chiffré ou multi-volumes, méthodes ≠ 0/8, gzip
  multi-membres (1er seul), tar sparse. Archive entière en mémoire.
- Validation : 56 entrées générées par Python (zip stocké/deflate 1-6-9, tar
  GNU/pax/ustar, tar.gz, gz) + 4 stratégies zlib brutes (fixe, Huffman seul,
  RLE, niveau 0) **identiques octet pour octet** ; vraies ROMs extraites avec le
  CRC du fichier d'origine ; 2 100 archives corrompues + 3 000 flux aléatoires
  sous ASan/UBSan sans défaut (corruptions zip/gzip interceptées par le CRC ;
  tar n'a pas de CRC, ses corruptions de contenu passent — limite du format).
  `tests/archive_smoke_test.cpp` (10 tests, fixtures Python embarquées).

**Côté émulateur** (`app/rom_source.*`) :
- `LoadRom(chemin, entrée)` → ROM en mémoire + `ArchiveOrigin` ; la console est
  détectée par l'extension de l'entrée puis par l'en-tête.
- Les cœurs NDS/GBA lisent un CHEMIN (sections à la demande, rechargement au
  chargement d'état) : la ROM est **matérialisée** dans un cache
  (`--extract-dir`, défaut dossier de préférences SDL
  `~/.local/share/EmulOS/emulator_demo/roms`), sous `<CRC32>-<nom>`, réutilisée
  sans réécriture si la taille concorde.
- **Sauvegardes à côté de l'archive, pas dans le cache** : `Settings::registerRomAlias`
  (copie extraite → `<dossier de l'archive>/<nom de la ROM>`) ; `romBasePath` /
  `saveBasePath` résolvent l'alias pour le `.sav` du cœur, le `.cht`, la pile
  GBC et l'état rapide. Vérifié avec Metroid Fusion (écrit sa flash au
  démarrage) : `Metroid Fusion (...).sav`, pas `974E46AB-Metroid...sav`.
- CLI : `--rom=archive` (1re ROM), `--rom-entry=NOM`, `--extract-dir=DIR` ;
  `--list-roms` et `--rom-info` lisent les archives ; rapport : origine archive
  + code produit, région, version, matériel, cartouche, somme d'en-tête, CRC-32.
- Boîte ROMs : ROMs simples ET contenues dans les archives de `assets/roms` et
  `.`, première présélectionnée ; panneau clé/valeur (en-tête + section
  ARCHIVE : format, entrée, taille, taux de compression, méthode, ROMs incluses,
  état rapide présent). Boîte États : `ResolveLogicalRom` retrouve l'archive qui
  contient la ROM d'un `.state0`. Glisser-déposer et « Parcourir… » acceptent
  les archives.

**Métadonnées enrichies** (`RomMetadata`) : code produit + région (4e lettre),
version, matériel (NDS/DSi ; CGB seul/compatible/DMG + SGB), cartouche GB
(MBC…), type de sauvegarde GBA (chaînes `EEPROM_V`/`SRAM_V`/`FLASH1M_V`…),
capacité, CRC-32, **somme de contrôle d'en-tête** (CRC-16/MODBUS NDS,
complément GBA, somme GB) — les trois vérifiées valides sur les vraies ROMs du
dépôt, ce qui valide les implémentations.

Pièges payés : `%-10s` de printf aligne en OCTETS (accents UTF-8) → remplissage
par `ULength()` ; codes produit de homebrew « #### » → ignorés ; dans une
colonne `Scrollable`, des lignes à hauteur fixe font dessiner des pastilles de
défilement parasites par ligne → deux libellés multi-lignes côte à côte.

Limites : ouvrir la boîte ROMs lit TOUTES les archives (un tar.gz est
décompressé entièrement pour être listé — ~1 s pour 6 ROMs dont 16 Mio sous
ASan) et la sélection d'une ROM d'archive décompresse sur le fil principal.

## Ajouts à lib/

- `sdl3::Surface::CreateFromPixels(w, h, format, pixels, pitch)` — construire
  une surface depuis des octets (copie) : indispensable pour écrire en PNG un
  framebuffer émulé sans GPU.
- `sdl3::CleanupTls()` + garde `sdl3::ForeignThreadScope` (thread.hpp) — cf. bug 12.
- `IOStream::Read/Write(std::span)` appelaient `read`/`write` en minuscules :
  jamais instanciés, donc jamais compilés — corrigé.
- `Makefile` : exemples multi-fichiers (`EMULATOR_SRCS/OBJS`, lien de tous les
  `.o` prérequis via `$(filter %.o,$^)`) et `EMULATOR_OPT ?= -O2` pour le cœur.

## Mesures (Intel, 8 cœurs, -O2)

| ROM | ASan/UBSan (build par défaut) | sans sanitizers |
|---|---|---|
| GBC 10-Pin Bowling | 338 % | 1153 % |
| GBA Pokémon Rubis | 144 % | 591 % |
| NDS Jewel Warehouse (homebrew) | 52 % | 193 % |
| NDS Korg DS-10 | 71 % | 253 % |

(sans écran, cadence libre ; en fenêtré la cadence est tenue à ~100 % par
l'audio ou l'horloge). Les sanitizers coûtent ~4× : **la NDS n'atteint pas le
temps réel dans le build par défaut**. Build rapide, dans un dossier séparé
(make ne suit pas les changements de drapeaux) :
`make BUILDDIR=build-release EMULATOR_OPT="-O2 -fno-sanitize=all -Wno-error=stringop-overflow" LDFLAGS= build-release/bin/emulator_demo`
(`stringop-overflow` : faux positif de GCC sur `core/string.hpp` sans sanitizers).

## Pièges déjà payés

- **Les sauvegardes de cartouche s'écrivent à côté de la ROM** pendant
  l'exécution (le jeu écrit sa flash). Un test sur `assets/roms` a réécrit
  `Metroid Fusion (...).sav` ; restauré depuis la copie identique d'EmulOs
  (même date d'origine). D'où `--save-dir` : À UTILISER dans tout test.
- Pas de BIOS GBA dans le dépôt : les ROMs GBA exigent `--bios-dir`
  (`../../System_Projects/EmulOs/bios-firmware` sur cette machine). La NDS
  démarre sans rien (BIOS HLE + boot direct).
- `LSAN_OPTIONS=suppressions=tests/lsan_suppressions.txt` pour lancer le
  binaire hors de `make` (fuites internes de SDL/X11).
- Race préexistante non corrigée : la boîte « Codes de triche » modifie
  `ActionReplay::cheats` depuis le fil principal pendant que le fil
  d'émulation les applique (le mutex d'`ActionReplay` est privé).
- `sdl3::GamepadButton::Start` : seule valeur non MAJUSCULE de l'énumération.

## Commandes

```sh
make emulator_demo && ./build/bin/emulator_demo --help
./build/bin/emulator_demo --list-roms
./build/bin/emulator_demo --headless --rom="assets/roms/10-Pin Bowling (Europe).gbc" --save-dir=/tmp/saves \
    --frames=1500 --press=900:START --press=1000:A --save-state=1200 --load-state=1400 \
    --state-path=/tmp/gbc.state0 --screenshot=1500:/tmp/gbc.png --require-video --report=/dev/stdout
xvfb-run -a ./build/bin/emulator_demo --rom=assets/roms/j.nds --save-dir=/tmp/saves --no-save-config \
    --frames=300 --open=config --screenshot=290:captures/config.png --report=rapport.json --report-format=json
make build/bin/emulator_demo_smoke_test && ./build/bin/emulator_demo_smoke_test
```
