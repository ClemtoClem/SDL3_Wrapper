# Profiler (demos/profiler) et capture de performances (diagnostics::trace) — 2026-11-01

Profileur de programmes C++ bâti sur les modules du dépôt, inspiré de Google Orbit. Il sert à trouver les goulots d'étranglement : CPU, mémoire, cache, GPU, fils.

## Capture : `libs/foundation/diagnostics/trace.hpp`

`diagnostics` dépend désormais de `core`.

- **Instrumentation manuelle** : `DIAG_ZONE("nom")`, `DIAG_FRAME()`, `DIAG_VALUE("piste", v)`, `AsyncBegin`/`AsyncEnd`, `SetThreadName`, `Intern` pour les noms dynamiques.
  - Tampon par fil.
  - Hors capture, le coût se réduit à la lecture d'un booléen atomique.
- **Échantillonnage des piles** : minuterie `SIGPROF` (temps CPU du processus, 1000 Hz par défaut).
  - Le gestionnaire n'alloue rien : il écrit dans un tableau préalloué, avec `backtrace`, chauffé avant.
  - La symbolisation se fait hors ligne, grâce à la carte des modules jointe (`/proc/self/maps`).
- **Échantillonneur du processus** (fil `diag-sampler`, 100 Hz) :
  - **tous** les fils lus dans `/proc/self/task` (y compris ceux de SDL et du pilote), avec leur temps CPU précis (`schedstat`) et leur état (R, S, D…) ;
  - mémoire résidente et tas (`mallinfo2`, 10 Hz car il parcourt le tas) ;
  - défauts de page, changements de contexte, temps utilisateur et système ;
  - compteurs matériels `perf_event` (cycles, instructions, références et défauts de cache, mauvaises prédictions) si `perf_event_paranoid` ≤ 2. Sinon, la raison est écrite dans la capture.
- **Format « .vcap »** : JSON compact (zones `[tid, nom, début, fin, profondeur]`…).
- **Pilotage** :
  - `DIAG_TRACE=fichier` capture toute l'exécution ;
  - `DIAG_TRACE_HZ` règle l'échantillonnage ;
  - `DIAG_TRACE_CONTROL=1|wait` : `SIGUSR2` démarre et arrête une capture (`PollControl()` à chaque image), chaque arrêt écrit `<base>.<n>.vcap`.

## Instrumentation des bibliothèques et de voxelia

- **ecs** : chaque système d'un `Schedule` est une zone portant son nom. Le moteur voxel apparaît donc en entier (physics, ai, fluids, lights…) sans code ajouté.
- **ui** : `ui.styles`, `ui.layout`, `ui.viewport3d`, `ui.draw`, `ui.window.<rôle>` et `gpu.present` (présentation, donc attente du GPU).
- **render3d** : `render3d.offscreen`.
- **voxelia** :
  - phases de l'image : `frame.wait_simulation`, `frame.events`, `frame.update`, `frame.studio`, `frame.draw` ;
  - `voxel.tick` (fil de simulation), `voxel.mesh_chunk` (fils de maillage), `voxel.render_update` ;
  - pistes : `frame.ms`, chunks en attente, maillages, entités.

## Diagnostic depuis Voxelia Studio

Le diagnostic se lance sans ligne de commande ni variable d'environnement :
- **Démarrer et arrêter** : bouton « ● Diag. » de la barre de la vue 3D (« ■ 12 s » pendant la capture), menu Outils (« Démarrer le diagnostic », « Arrêter le diagnostic et exporter »), ou les commandes `studio.diag.start`, `studio.diag.stop` et `studio.diag` (bascule).
- **Export** : à l'arrêt, la capture est écrite dans le dossier des captures du profiler (`capturesDirectory` de `data/profiler/settings.json`, `data/profiler/captures` par défaut), sous le nom `voxelia-<date>-<heure>.vcap`.
- **Pilotage** : le `Shell` porte la capture (`StartDiagnostic`, `StopDiagnostic`, `FinishDiagnostic`). Fermer la fenêtre du studio ne l'interrompt donc pas.
- **Sortie** : quitter voxelia avec une capture en cours l'arrête proprement et l'exporte, à la fin de la boucle principale et, en filet, dans le destructeur du `Shell`.
- **Affichage** : les barres d'état du jeu et du studio montrent « ● diagnostic N s ».
- **Vérification** : scénario démarrer → arrêter (export), puis démarrer → quitter (export à la sortie) ; les deux captures se relisent dans `profiler report`.

## Profiler

- **Modules** : `capture` (lecteur dédié, sans DOM JSON), `symbols`, `analysis`, `recorder`, `settings`, `app` (ligne de commande) et `viewer`.
- **Symbolisation** :
  - lecteur ELF64 (`.symtab`, sinon `.dynsym`, segments PT_LOAD) ;
  - adresse d'exécution → module → adresse virtuelle → fonction démêlée ;
  - lignes de source à la demande (`addr2line`, DWARF).
- **Analyse** :
  - images (percentiles, saccades, avec les zones de la pire image) ;
  - zones (nombre, total, temps propre, min, max, moyenne, écart type, par image) ;
  - fils (CPU, part d'un cœur, états) ;
  - échantillons (fonctions, temps propre et inclusif ; arbres descendant et ascendant) ;
  - mémoire (croissance mesurée APRÈS la mise en route), cache (IPC, MPKI), GPU ;
  - **diagnostic** classé par gravité ;
  - comparaison de deux captures, export Chrome Trace (chrome://tracing, ui.perfetto.dev).
- **Commandes** : `record` (toute l'exécution, ou `--delay S --duration S` piloté par SIGUSR2), `attach PID --duration S`, `list`, `report`, `tree [--bottom-up]`, `compare`, `export --chrome`, `view`.
- **Rangement** :
  - captures dans `data/profiler/captures/<programme>-<date>-<heure>.vcap` ; les commandes sans fichier prennent la plus récente ;
  - réglages dans `data/profiler/settings.json`, créé au premier lancement : dossier des captures, fréquence d'échantillonnage, objectif d'images par seconde, lignes du rapport, délai et durée par défaut, dernière commande, captures récentes. `--settings` désigne un autre fichier.
- **Visualiseur**, sur ui, `editor` et `ui::WindowFrame` :
  - chronologie à la Orbit : piste des images (budget dépassé en rouge), mémoire, puis par fil sa bande d'état, ses zones empilées et ses marques d'échantillons ;
  - navigation : molette = zoom au curseur, glisser droit = défilement, glisser gauche = plage (toutes les statistiques suivent), clic = zone, W/A/S/D/F/Origine/Échap ;
  - docks : Fils / Capture, Diagnostic / Fonctions instrumentées / Échantillonnage / Arbres, Sélection (lignes de source les plus échantillonnées) ;
  - menus : ouvrir, captures récentes, comparer, exporter, lancer et capturer, capturer un processus ;
  - Fichier › Réglages… (Ctrl+,) : boîte qui modifie `settings.json`. Elle couvre le dossier des captures, l'échantillonnage, l'objectif d'images par seconde, le délai et la durée par défaut, l'arrêt du programme après une capture et le nombre de lignes des rapports. Elle offre aussi « Valeurs par défaut » et « Oublier les récentes ». Les nombres sont vérifiés (bornes, message d'erreur). L'enregistrement s'applique tout de suite : liste des captures, et nouvelle analyse si l'objectif change.

## Vérification

- **Tests** :
  - `diagnostics` 14/14 et `profiler` 4/4, sous ASan+UBSan, TSan et LSan ;
  - capture réelle relue, échantillons symbolisés jusqu'à la boucle chaude du test, réglages en aller-retour.
- **Démonstration** :
  - sur voxelia (overworld + studio, 20 s), le rapport sort en 0,7 s ;
  - `record --delay 12 --duration 4` capture le régime établi sans le chargement.

## Limites

- Compteurs matériels refusés tant que `kernel.perf_event_paranoid` vaut 4. La capture le dit et donne la commande pour les autoriser.
- Les fonctions internes de la libc (symboles locaux absents de `.dynsym`) restent sous la forme `[libc.so.6+0x…]`.
- Pas de minutage GPU par requête : SDL_GPU n'expose pas les horodatages. Le GPU est mesuré par l'attente à la présentation et par le temps des rendus hors écran.
