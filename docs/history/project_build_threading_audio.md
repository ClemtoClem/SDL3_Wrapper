# Chantier : compilation (PCH) + threading SDL3 + module `resources` + 3 exemples audio

Plan complet dans `~/.claude/plans/mossy-forging-fog.md` (Décisions #1-7, Phases 0-6). Ce fichier est
le journal de progression phase par phase — le plan est la spec, ceci est ce qui s'est réellement
passé (déviations, bugs trouvés, résultats de vérification).

Contexte : 4 demandes groupées par l'utilisateur (compilation accélérée, wrapper threading SDL3,
refonte du module `resources`, port de 3 exemples audio SDL3pp). Note de continuité : le répertoire
source de la librairie s'appelle `lib/` (renommé depuis `src/` entre le chantier précédent — plots
ImPlot — et celui-ci, hors session, tout le contenu a survécu intact).

## Phase 0 — En-tête précompilé (PCH), remplace un `lib.a` littéral

Décision utilisateur explicite (après compromis posé via AskUserQuestion) : PCH plutôt que `lib.a`,
puisque `lib/` est 100% header-only y compris pour les templates (`Option<T>`/`Result<T,E>`/
`ArchetypeRegistry`/composants `ui::`) — un `lib.a` n'aurait pu archiver que la fraction non-template
du code.

**Mécanique mise en œuvre** (après une passe de validation qui a trouvé des pièges GCC réels avant
l'implémentation — cf. plan, Décision #1) :
- `lib/prelude.hpp` (nouveau) agrège `core/core.hpp` + `sdl3/sdl3.hpp` + `ecs/ecs.hpp` + `ui/ui.hpp`
  + `data/data.hpp` (~39.5k lignes). Exclut délibérément `resources/` et le futur `audio/` (modules
  actifs de CE chantier, Phases 2-3 — les inclure invaliderait le PCH à chaque sauvegarde pendant ces
  phases).
- `PCH_FLAGS` (Makefile) : une seule variable réutilisée mot pour mot pour générer le `.gch` ET
  compiler chaque binaire consommateur — GCC rejette silencieusement un PCH dont les flags divergent
  ne serait-ce que d'un caractère (aucune erreur visible, juste une compilation plus lente que prévu
  sans qu'on s'en rende compte).
- `.gch` généré à `$(BUILDDIR)/prelude.hpp.gch` (pas à côté de `lib/prelude.hpp`, pour rester un
  artefact couvert par `clean`), consommé via `-I$(BUILDDIR)` PLACÉ AVANT `-I$(LIBDIR)` +
  `-include prelude.hpp` (sans préfixe `lib/`) — GCC cherche un `.gch` de façon POSITIONNELLE dans
  l'ordre de recherche des `-I`, pas via un `-o` de génération séparée ; un `.gch` mal placé est
  ignoré SANS AUCUNE erreur, y compris avec `-Winvalid-pch` (qui ne détecte qu'un PCH trouvé-mais-
  rejeté, jamais un PCH absent). Vérifié avec `-H` (le `.gch` apparaît préfixé `!` quand réellement
  chargé) — c'est la seule vérification positive fiable, faite explicitement.
- `.gch` posé en PRÉREQUIS EXPLICITE des règles de motif `examples`/`tests` (pas seulement inféré via
  `-MMD`), sans quoi un `.d` de binaire ne référence que `lib/prelude.hpp` lui-même (mtime inchangé)
  et jamais les en-têtes imbriqués absorbés DANS le PCH — `make` ne saurait pas qu'un binaire a
  besoin d'être relié après une régénération du `.gch`.

**Piège réel trouvé PENDANT l'implémentation (pas anticipé par le plan)** : 3 fichiers
(`examples/renderer.cpp`, `tests/string_smoke_test.cpp`, `tests/ecs_smoke_test.cpp`) font
`#define USE_TEST` AVANT `#include "core/core.hpp"` pour activer conditionnellement `core/test.hpp`
(`#ifdef TEST` dans `core.hpp`). Avec `-include prelude.hpp`, GCC traite `core.hpp` (via le PCH)
AVANT que le fichier source n'ait la moindre chance de poser sa propre macro — `TEST` n'a alors
plus aucun effet, `RUN_ALL_TESTS`/`test.hpp` ne sont jamais inclus, échec de compilation
(`'RUN_ALL_TESTS' was not declared`). **Corrigé par un mécanisme Makefile générique** plutôt qu'un
cas particulier codé en dur : `NO_PCH_TESTS`/`NO_PCH_EXAMPLES` détectent automatiquement (via
`grep -l '^#define USE_TEST'`) les fichiers concernés à l'évaluation du Makefile, et une règle EXPLICITE
générée par fichier (`$(foreach f,$(NO_PCH_TESTS),$(eval $(call NO_PCH_TEST_RULE,$(f))))`) les
compile SANS le PCH (flags/commande identiques à l'ancien Makefile pré-PCH) — une règle explicite
prime toujours sur la règle de motif en Make, donc ces 3 fichiers continuent de fonctionner sans
aucune modification de leur propre code. Leçon générale : tout fichier qui définit une macro avant
d'inclure un en-tête agrégé dans le PCH est structurellement incompatible avec `-include`, quel que
soit le projet — le détecter automatiquement (grep sur le patron `#define X` en tête de fichier)
plutôt que de maintenir une liste à la main est plus robuste (un futur fichier avec le même besoin
est automatiquement pris en charge).

**Bug PRÉ-EXISTANT découvert en testant (pas causé par ce chantier, PAS corrigé — hors périmètre)** :
`tests/ecs_smoke_test.cpp` échoue à la compilation avec `Option<RefMut<TimeResource>>::OkOr()` —
`core/interop.hpp:13` fait `Ok<T>(*value)` où `T = RefMut<TimeResource>`, mais
`RefMut<T>::RefMut(T&&) = delete` (`core/ref.hpp:84`) rend cette construction mal formée. **Vérifié
comme totalement indépendant du PCH** : reproduit avec une invocation `g++` complètement nue, sans
`-include`, sans aucun rapport avec le Makefile modifié — ce test était déjà cassé avant que ce
chantier ne commence (`string_smoke_test.cpp`, l'autre fichier `TEST`, compile et passe sans
problème). Signalé à l'utilisateur, pas corrigé (hors scope de "Phase 0 — PCH" ; toucherait
`core::Option<T>`/`core::RefMut<T>`/`ecs::Resources`, un sujet séparé).

**Résultats de vérification** :
- `-H` confirme le PCH réellement chargé (`!` devant `build/prelude.hpp.gch`), pas juste absent
  d'erreur.
- Régression complète : les 9 exemples + 21/22 tests (tout sauf `ecs_smoke_test`, pré-cassé) passent —
  compilation ET exécution (`SDL_VIDEODRIVER=dummy`, exit 0 pour les tests, exit 124/timeout sain
  pour les exemples).
- **Cas (a)** (toucher un en-tête DANS la fermeture, ex. `lib/ui/plot.hpp`) : les 30 binaires
  recompilent bien tous (comportement correct d'un PCH partagé). Comparaison HONNÊTE de la vitesse :
  une première mesure naïve (PCH-clean vs PCH-après-touche) donnait un temps quasi identique
  (5m4.46s vs 5m4.59s) car les DEUX scénarios régénèrent le `.gch` depuis zéro — ce n'est PAS la
  bonne comparaison. La comparaison RÉELLE (PCH vs AUCUN PCH, même 30 binaires, même séquentialité —
  sans `-j`, pour matcher le comportement par défaut de `make`) : **13m22s sans PCH → 5m4s avec PCH,
  soit ~62% de réduction du temps de build total.** Un micro-benchmark sur un seul petit fichier
  (`ui_smoke_test_1.cpp`) ne montrait qu'un gain modeste (~10%, 11.8s→10.6s) — les gros exemples
  (showcase, plot_demo, node_graph_demo, aero_patchbay) bénéficient BEAUCOUP plus proportionnellement
  du partage du parsing d'en-têtes, d'où l'écart entre le micro-benchmark et le résultat agrégé.
- **Cas (b)** (toucher un en-tête HORS fermeture, ex. `lib/resources/cache.hpp`) : confirmé que le
  `.gch` n'est PAS régénéré (mtime inchangé après le touch — `14:47:51` avant, toujours `14:47:51`
  après). La démonstration complète "seul LE binaire concerné recompile" n'a pas pu être faite avec
  un VRAI consommateur (zéro exemple/test n'utilise encore `resources/` à ce stade — cf. Phase 2 à
  venir) ; se fera naturellement une fois Phase 2/3 ajoutent de vrais consommateurs hors fermeture.

**Corrections annexes faites en passant** (repérées pendant la recherche préalable) : `#pragma once`
ajouté à `lib/core/core.hpp` (seul en-tête de tout `lib/` à en manquer, 74/75 en avaient déjà un —
inoffensif avant ce chantier puisque son contenu n'est que des `#include` déjà gardés, mais
`prelude.hpp` doit avoir le sien dès sa création pour ne pas répéter l'oubli).

## Phase 1 — Wrapper threading SDL3 (`lib/sdl3/thread.hpp`)

`SDL_process.h`/`SDL_storage.h` étaient déjà entièrement wrappés avant ce chantier (`process.hpp`/
`storage.hpp`) — le nouveau fichier couvre exactement ce qui manquait : `SDL_thread.h` +
`SDL_mutex.h` + `SDL_atomic.h` (mutuellement référencés, un seul fichier). Inclus dans `sdl3.hpp`
juste après `log.hpp` (qui utilise déjà `std::jthread`/`mutex`/`condition_variable` en interne — un
voisinage naturel), avant `sdl_context.hpp`.

**Classes livrées** : `SpinLock`+`SpinLockGuard`, `AtomicInt`/`AtomicU32`/`AtomicPointer<T>`
(types VALEUR non copiables, même sémantique que `std::atomic` — copier un atomique n'a pas de
sens), `Mutex`+`MutexGuard` (`Wrapper<SDL_Mutex,SDL_DestroyMutex>` + garde scopée calquée sur
`PropertiesLock` de `misc.hpp`), `RWLock`+`RWLockReadGuard`+`RWLockWriteGuard`, `Semaphore`,
`Condition`, `InitState`, `TLS<T>`, et `Thread` (le cas particulier du fichier).

**`Thread` ne dérive PAS de `Wrapper<T,Deleter>`** — `SDL_Thread` n'a pas de destructeur unique (il
faut `SDL_WaitThread` OU `SDL_DetachThread`, jamais les deux), donc `Thread` gère son propre état
move-only avec un destructeur qui **joint par défaut** (comme `std::jthread`, PAS `std::thread` qui
`std::terminate()` sans join/detach explicite) — cohérent avec la préférence du projet pour la
sécurité par défaut. `wait()` renvoie `Option<int>` (code de sortie), même convention que
`Process::wait()` déjà établie — cohérence directe entre les deux modules "gestion de ressources
système" de `sdl3::`. Point d'entrée `SDL_ThreadFunction` wrappé via `std::function<int()>` +
contexte alloué au tas + trampoline C (même famille que `dialog.hpp`), libéré DANS le trampoline une
fois la fonction terminée.

**Bug de compréhension d'API trouvé en écrivant le test (pas dans le wrapper lui-même)** :
`SDL_ShouldQuit()` ne renvoie PAS "faux tant qu'aucune désinit n'a été demandée" comme une première
lecture rapide le suggérait — sa doc dit explicitement : si l'état est `INITIALIZED`, elle fait
transitionner vers `UNINITIALIZING` ET renvoie VRAI pour l'appelant qui doit alors faire le
nettoyage. Donc juste après `setInitialized(true)`, `shouldQuit()` doit renvoyer VRAI, pas FAUX — le
premier jet du test avait l'assertion inversée, planté immédiatement, corrigé en relisant la doc
SDL3 (`SDL_mutex.h`) directement plutôt qu'en devinant sur la sémantique du nom de fonction seul.
Test final couvre le cycle complet `shouldInit()→setInitialized(true)→shouldQuit()→
setInitialized(false)→shouldInit()` (retour à l'état initial), pas juste un aller simple.

**Discipline de test** : `tests/thread_smoke_test.cpp` privilégie des tests de CONTENTION RÉELLE
multi-thread plutôt que des vérifications mono-thread superficielles — `Mutex`/`SpinLock` testés en
faisant incrémenter un compteur `int` NON atomique par 8 threads × 5000 fois chacun sous le verrou :
si le verrou ne sérialisait pas réellement l'accès, des incréments "torn" feraient que le total final
soit strictement inférieur à 40000 — un signal de correction net, pas une supposition. `RWLock` testé
en faisant tenir un verrou d'écriture par un thread en arrière-plan (`AtomicInt` comme signal de
synchronisation) puis en vérifiant depuis le thread principal que `tryLockRead()`/`tryLockWrite()`
échouent TOUS LES DEUX pendant que le verrou est tenu, puis réussissent après relâchement. `TLS<T>`
testé avec 2 threads posant des valeurs DIFFÉRENTES sur le MÊME objet `TLS<T>` partagé et vérifiant
qu'aucun ne voit la valeur de l'autre (la propriété définissante du stockage local au thread).
Stabilité vérifiée sur 5 exécutions consécutives (aucun flake).

**Régression complète** : 22/22 tests passent (les 21 précédents + le nouveau
`thread_smoke_test`), 9/9 exemples tournent sainement — `ecs_smoke_test` reste la SEULE exception,
toujours le même bug pré-existant documenté en Phase 0 (indépendant de ce chantier).

## Phase 2 — Module `resources` (`lib/resources/`)

Refonte de `resource.hpp`/`pool.hpp`/`cache.hpp` (0 consommateur avant ce chantier, aucune
sous-classe concrète de `Resource` nulle part) + nouveau `bytes_resource.hpp` (première sous-classe
concrète, jamais existé) + nouveau `resources.hpp` (agrégateur, manquant — même patron que
`sdl3.hpp`/`ui.hpp`/`ecs.hpp`/`data.hpp`).

**Changements de type, dans l'ordre de priorité de la demande** :
- `namespace sdl` → `namespace resources` (le seul module de tout le projet à utiliser `sdl` — partout
  ailleurs c'est `sdl3::` pour le wrapping direct d'API SDL, ou un namespace de domaine nu comme
  `data::`/`ecs::`/`math::` pour le reste ; `resources::` matche ce second groupe, ce n'est pas un
  wrapper direct d'API SDL).
- `std::string`/`std::string_view` → `String`/`StringView` partout dans l'API publique (`Resource::
  Path()`, toutes les clés `Pool<T>`/`Registry`/`ResourceCache`).
- `Pool<T>::Get()`/`Registry::Get()` : `shared_ptr<T>` nu (nullptr au lieu de trouvé) →
  `Option<shared_ptr<T>>` — plus de sentinelle nulle qui fuite hors de l'abstraction déjà établie
  ailleurs (`sdl3::filesystem::prefPath`/`pathInfo`).
- `Resource::Load()`/`DoLoad()` : `bool` → `Result<bool, String>`. **`Result<void, E>` n'existe PAS
  dans ce projet** (`Ok<T>{T value;}` ne compile pas avec `T=void`, pas de spécialisation) — suivi le
  précédent DÉJÀ établi ailleurs (`sdl3::CheckError(bool) -> Result<bool, Error>`, `sdl3/error.hpp`)
  plutôt que d'inventer un type `Unit`/tag vide. `String` (possédée), pas `StringView` : `DoLoad()`
  peut s'exécuter sur un thread de fond via `AsyncLoader`, et `sdl3::GetError()` est un buffer
  thread-local — copier le message AVANT de retourner évite qu'un appel SDL ultérieur sur ce MÊME
  thread n'écrase le message avant qu'un autre thread ne le lise.
- `AsyncLoader` migré de `std::jthread`/`std::mutex`/`std::condition_variable`/`std::atomic` vers
  `sdl3::Thread`/`sdl3::Mutex`+`MutexGuard`/`sdl3::Condition`/`sdl3::AtomicInt` (Phase 1 du même
  chantier) — `std::atomic<size_t> pending` reste `std::atomic` (pas d'équivalent SDL pour un
  compteur `size_t`). **Bug latent CORRIGÉ en migrant, pas porté tel quel** : la version `jthread`
  d'origine attendait sur une `condition_variable` PLAINE (pas `condition_variable_any`) avec un
  prédicat `stop_requested()` vérifié manuellement — `jthread::~jthread()` appelle `request_stop()`
  mais ne réveille JAMAIS un `wait()` bloqué sur une cv plaine, donc `~AsyncLoader()` pouvait bloquer
  indéfiniment si un worker était inactif à la fermeture. `sdl3::Thread` n'a de toute façon pas
  l'intégration `jthread`/`stop_token` — le destructeur pose maintenant le flag d'arrêt PUIS appelle
  explicitement `cv.broadcast()` AVANT de joindre chaque thread, correction quasi gratuite puisque
  ce chemin était de toute façon réécrit.

**`BytesResource`** (nouveau) : charge le contenu brut d'un fichier via `sdl3::readFile()` —
indépendante d'un renderer/fenêtre, démontre pour la première fois que le module fonctionne
réellement de bout en bout (chargement sync ET async réels, pas juste la scaffolding).

**Test réel utilisé pour le fichier de démonstration** : `tests/smoke_test_7.cpp` référençait
`"assets/sounds/SOUNDS.md"`, qui **n'existe pas** dans `assets/` (fichier fantôme, probablement
supprimé/renommé depuis — ce test pré-existant n'a jamais été vérifié par cette session). Utilisé à
la place `assets/textures/default_particle.png` (82 octets, le plus petit fichier de `assets/`) —
`BytesResource` charge n'importe quel fichier en octets bruts, peu importe son format réel, donc un
petit PNG convient parfaitement pour un test de chargement rapide et répété (8 chargements au total
dans la suite de tests, dont 6 en parallèle via `AsyncLoader`).

**Régression complète** : 22/22 tests passent (21 précédents + `thread_smoke_test` de la Phase 1 +
le nouveau `resources_smoke_test`), 9/9 exemples tournent sainement (`ecs_smoke_test` toujours la
seule exception pré-existante). `resources_smoke_test.cpp` stable sur 5 exécutions consécutives
(couvre `AsyncLoader` avec un VRAI pool de `sdl3::Thread`, pas juste des vérifications mono-thread).

**Leçon annexe (script de vérification, pas le code)** : une boucle bash `for ex in ...; do timeout 3
"$ex"; echo "$(basename "$ex"): exit=$?"; done` capture le MAUVAIS code de sortie — `$(basename ...)`
tourne dans un sous-shell qui écrase `$?` avant que `echo` ne le lise, donnant `exit=0` pour tout même
quand `timeout` a réellement expiré à 124. Toujours capturer `code=$?` sur la ligne IMMÉDIATEMENT
après la commande à vérifier, jamais dans la même commande qu'une substitution `$(...)`.

## Phase 3 — Fondations audio (`lib/audio/dsp.hpp` + compléments `sdl3::`)

Nouveau répertoire `lib/audio/` (premier fichier : `dsp.hpp`) — utilitaires numériques DSP
génériques, calqués sur le patron de `lib/math/math.hpp` (fonctions libres + petits structs,
`namespace audio`, dépend de `sdl3::stdinc.hpp` pour `sin/cos/sqrt/log10/abs/max/PI_F` comme le fait
déjà `math.hpp`, PAS des consommateurs SDL_audio — d'où « indépendant de SDL3 » dans la décision du
plan : indépendant de l'API AUDIO de SDL3, pas de tout le SDK). Contenu : `Signal{vector<float>
samples; int sampleRate}`, fenêtrage `WindowHann/Hamming/Blackman/Rectangular` (+ `ApplyWindow` par
enum), `ProcessFFT()` (Cooley-Tukey radix-2 itératif, in-place après copie, taille = puissance de 2
uniquement, `assert` en debug), `ProcessFFTMagnitudeDb()`/`ProcessFFTFrequencies()`, `GetRMS()`/`GetPeak()`,
`BiQuadState`/`BiQuadCoeffs`/`BiQuadDesign()`/`BiQuadSetDesign()`/`BiQuadStep()` (formules RBJ
« Audio EQ Cookbook », LowPass/HighPass/BandPass), `SoftClip()` (genou `tanh`, scalaire + span).

**Bug de calcul trouvé PAR LE TEST, pas par relecture** : première version de `ProcessFFTMagnitudeDb()`
divisait chaque bin par `N` seulement (`|X[k]|/N`) — pour une sinusoïde plein-échelle tombant pile
sur un bin, ça donne un magnitude de 0.5 (soit -6.02 dB) au lieu de ~0 dBFS attendu, parce que
l'énergie d'un signal réel se répartit pour moitié dans le bin miroir de la moitié haute du spectre
(non retournée par `fftMagnitudeDb`, qui ne garde que les `N/2` premiers bins). Corrigé en doublant
le facteur d'échelle pour tous les bins SAUF le DC (`i==0` → `1/N`, sinon `2/N`) — repli correct de
l'énergie miroir. Sans le test dédié avec assertion `magDb[targetBin] > -6.f` (échouée à ~-6.02 au
premier essai), ce facteur ×2 manquant serait passé inaperçu (le code compilait et « avait l'air »
de produire un spectre plausible).

**Test dédié** (`tests/dsp_smoke_test.cpp`, PAS juste "ne plante pas") : sinusoïde à fréquence EXACTE
d'un bin FFT (1024 échantillons, bin 23 → 990.527 Hz à 44100 Hz) → pic doit être exactement au bin
23, à moins de 6 dB de 0 dBFS, dominant le DC de plus de 20 dB ; fenêtrage Hann → bords quasi nuls,
centre proche de 1 ; RMS/Peak sur un signal carré plein-échelle connu (RMS=1.0, Peak=1.0 exactement)
; filtre passe-bas RBJ à 500 Hz → gain mesuré (rapport des pics en régime établi, après 2000
échantillons de stabilisation) quasi inchangé à 100 Hz (>-1 dB) et nettement atténué à 5 kHz (mesuré
-40.7 dB, largement sous le seuil -20 dB) ; `softClip` transparent sous le seuil, borné et symétrique
au-dessus. Résultats réels observés : pic FFT à -3.6e-6 dB (quasi exactement 0 dBFS, comme attendu
pour une sinusoïde plein-échelle après la correction ×2), passe-bas -0.007 dB @100Hz / -40.7 dB
@5kHz.

**Compléments `lib/sdl3/audio.hpp`** : `AudioDevice{SDL_AudioDeviceID id; String name}` +
`enumeratePlaybackDevices()`/`EnumerateRecordingDevices()` (libèrent le tableau `SDL_free` retourné
par `SDL_GetAudioPlaybackDevices`/`RecordingDevices`, noms via `SDL_GetAudioDeviceName`) ;
`AudioStream::OpenPlayback/OpenRecording` gagnent une surcharge `(spec, SDL_AudioDeviceID)` (l'ancienne
surcharge `(spec)` devient un simple appel à la nouvelle avec `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK/
RECORDING`, zéro duplication de logique) ; `AudioStream::Queued()` (`SDL_GetAudioStreamQueued`, pour
réguler l'avance d'un producteur qui remplit le flux — nécessaire pour un patchbay/générateur qui
pousse des échantillons en continu sans dérive de latence).

**Complément `lib/sdl3/mixer.hpp`** : nouvelle classe `AudioDecoder : public Wrapper<MIX_AudioDecoder,
MIX_DestroyAudioDecoder>` — `Create(path, props=0)`, `GetFormat(SDL_AudioSpec&)`, `Decode(buffer,
buflen, spec=nullptr)`. Décodage streaming vers un buffer PCM fourni par l'appelant (tous formats
supportés par SDL3_mixer — WAV/OGG/FLAC/MP3/...), délibérément DIFFÉRENT de `MixAudio`+`MixTrack`
(qui jouent directement sur un `Mixer`/périphérique) : c'est la primitive retenue pour le futur nœud
Playlist de l'exemple patchbay, dont la sortie doit alimenter le graphe DSP plutôt que d'aller
directement aux haut-parleurs.

**Régression complète** : tous les tests pré-existants + `thread_smoke_test` (Phase 1) +
`resources_smoke_test` (Phase 2) + le nouveau `dsp_smoke_test` passent (23/24 binaires — seul
`ecs_smoke_test` reste en échec de compilation, toujours le même bug pré-existant documenté en
Phase 0, indépendant de ce chantier). 9/9 exemples tournent sainement (`SDL_VIDEODRIVER=dummy`,
`exit=124` = timeout propre, aucun crash).

## Phase 4 — Exemple `audio_spectruanalyzer`

Nouveau `examples/audio_spectruanalyzer.cpp` — PAS un port littéral de SDL3pp/examples/audio/
05_spectruanalyzer.cpp (widgets/API totalement différents), même comportement fonctionnel :
liste de périphériques d'enregistrement (`f.listbox` + `sdl3::EnumerateRecordingDevices()`, Phase 3),
gain, plage de fréquences min/max, presets taille FFT (256..4096)/fréquence d'échantillonnage
(8k..48k)/fenêtre spectrale (Rect/Hann/Hamming/Blackman), deux `ui::UiPlot` (forme d'onde en Line,
spectre en Area) mis à jour chaque frame via `PlotSeries::setY()`/`setXY()`.

**Architecture retenue, différente du reste du module `ui::` jusqu'ici** :
- **Deux racines indépendantes côte à côte** (`left`/`right`, chacune un `f.column()` spawné
  séparément) plutôt qu'un unique conteneur `f.row()` parent — `LayoutSystem` itère TOUTES les
  entités sans `UiParent` comme racines (`systems.hpp:301`), donc plusieurs racines top-level
  fonctionnent nativement (même patron que `ui_aero_basics.cpp` : `desktop`/`tb.root`/`content`
  sont 3 spawns indépendants). Positionnement par `anchor(TopLeft)` + `offset()` +
  `Dimension::rpct(100).plus(-320.f)` (« 100% moins la largeur du panneau de gauche ») plutôt que
  par flex-grow — `growW()` n'a de sens qu'ENTRE ENFANTS d'un même conteneur flex, pas entre
  racines indépendantes (piège rencontré : premier jet avec `right.growW()` sur une racine, sans
  effet puisqu'il n'y a pas de parent flex pour la faire grandir).
- **Highlighting « actif » des boutons presets via une CLASSE de style dynamique**
  (`gui.createStyleClass("preset-active", ...)` une fois, puis `ui::addClass`/`removeClass` par
  entité au clic) plutôt que la réécriture manuelle de couleurs par bouton (patron SDL3pp
  original, `SDL::UI::Style& s = ui.GetStyle(...)`) — colle à la Style Engine v2 de ce projet
  (cascade par classes, cf. chantier `[Historique]` Aero) : `addClass` ajoutée APRÈS les classes
  `root`/`root-button` posées par `f.button()` gagne toujours la cascade (dernière classe = plus
  prioritaire), donc pas besoin de gérer l'ordre manuellement.
- **Axes de plot à plage FIXE** (`yAxis.min/max` + `autoFit=false`, posé directement sur le
  composant `Plot` après spawn via `ar.GetComponent<ui::UiPlot>(e)`) plutôt que l'auto-fit par
  défaut de `PlotAxis` — un spectre/une forme d'onde dont les axes sautent à chaque frame selon le
  signal du moment serait illisible ; la plage est un choix d'ANALYSE (gain, freqMin/freqMax), pas
  une propriété des données affichées à cet instant. `xAxis`/`yAxis` remis à jour explicitement
  dans les callbacks `onChange` des sliders freq min/max et des boutons de taille FFT (les axes ne
  se corrigent pas tout seuls puisque `autoFit=false`).
- **FFT sur un bloc de taille = plus grande puissance de 2 ≤ taille du tampon courant**, calculée
  en O(1) par doublement (`while (fftLen*2 <= wn) fftLen *= 2`) puis une seule copie du bloc le
  plus récent — PAS un rognage par `erase(begin())` répété jusqu'à tomber sur une puissance de 2
  (première version écrite, corrigée avant même de compiler par relecture : O(n²) dans le pire cas,
  jusqu'à ~4096² copies/frame pour le pire alignement de taille de tampon).

**Bug trouvé et corrigé, PAS dans mon code — dans `ui/plot.hpp` (module déjà livré, chantier
`[Historique]` plots)** : `kPlotTickMargin = 22.f` (marge réservée par axe pour les labels de
graduation) trop étroite pour un label négatif façon `"-1.0"`/`"-80"` — le signe `-` se retrouvait
positionné hors de la zone de tracé réservée et disparaissait (clippé), rendant les axes négatifs
illisibles (`-1.0` affiché comme `1.0`, `-80` comme `80`, etc. — repéré en zoomant sur une capture
d'écran Xvfb du plot de forme d'onde, plage `[-1,1]`). Root-cause confirmée par lecture de
`generateGetTicks()`/`String::from(float,int)` (tous deux corrects, le signe est bien dans la chaîne
générée) — le problème est uniquement la marge de mise en page trop étroite pour la CONTENIR.
Corrigé en élargissant `kPlotTickMargin` à `32.f` (`lib/ui/plot.hpp:411`) — changement d'une seule
constante, sans risque (marge élargie = strictement plus d'espace réservé, jamais moins), vérifié
sans régression sur `ui_smoke_test_8` (le test dédié du chantier plots) et `ui_plot_demo` (capture
Xvfb re-vérifiée). Ce bug préexistait pour TOUT plot avec un axe à valeurs négatives (y compris
potentiellement le graphe temps réel de `ui_showcase.cpp`, jamais zoomé d'assez près pour être
repéré avant) — pas spécifique à cet exemple, corrigé au niveau du widget partagé.

**Vérification visuelle réelle (Xvfb + xwd + ffmpeg, patron établi Phase 7 du chantier plots)** :
capture avec aucun périphérique sélectionné (état initial : listbox avec 2 périphériques réels
détectés sur cette machine, boutons presets par défaut bien surlignés en bleu — 2048/44k/Hann,
axes négatifs lisibles après le fix ci-dessus) ; capture avec un clic simulé (`xdotool`) sur le
premier périphérique de la liste — confirme le pipeline audio RÉEL de bout en bout : ouverture
`sdl3::AudioStream::OpenRecording`, tampon glissant alimenté, forme d'onde affichant du bruit de
micro authentique (léger, non nul), spectre affichant une vraie courbe FFT avec des pics
plausibles en basses fréquences (bruit de fond typique), libellés « Actif »/« Enregistrement »
mis à jour. Pas seulement un exit-code sain — un comportement fonctionnel réellement observé.

**Régression complète après Phase 4 + le fix `plot.hpp`** : tous les tests pré-existants +
`thread_smoke_test` + `resources_smoke_test` + `dsp_smoke_test` passent (seul `ecs_smoke_test`
toujours en échec, bug pré-existant documenté Phase 0, sans lien). 10/10 exemples (les 9
précédents + le nouveau `audio_spectruanalyzer`) tournent sainement.

## Phase 5 — Exemple `audio_signal_generator`

Nouveau `examples/audio_signal_generator.cpp` — PAS un port littéral de SDL3pp/examples/audio/
06_signal_generator.cpp, même comportement : 4 cartes oscillateur (toggle + 5 formes d'onde +
sliders fréquence/amplitude), volume maître, forme d'onde composite + spectre FFT (`ui::UiPlot`,
même patron que Phase 4), VU-mètres RMS/Peak (`f.progress` + `UiStyle::setBgChecked` par couleur).
`Oscillator::nextSample()` (accumulateur de phase + séries de Fourier tronquées pour Square/
Triangle/Sawtooth band-limited, bruit blanc pour Noise) porté DIRECTEMENT de l'original — c'est de
la synthèse harmonique générique indépendante de SDL3pp/de ce projet, aucune raison de la
réécrire. `audio::SoftClip()`/`GetRMS()`/`GetPeak()`/`ProcessFFT()`/`ProcessFFTMagnitudeDb()`/`ProcessFFTFrequencies()`/
`WindowHann()` (Phase 3) réutilisés pour tout le reste (limiteur de sortie, mesure de niveaux,
analyse spectrale) plutôt que dupliqués localement. Sortie audio via `sdl3::AudioStream::
OpenPlayback` + `Queued()` (Phase 3) pour réguler le remplissage du flux (génère un bloc de
`kBlockSize=1024` échantillons tant que `Queued() < kBufferSec*sampleRate*sizeof(float)`).

**Bug trouvé et corrigé DANS CE FICHIER (pas dans un module partagé, contrairement à Phase 4)** :
`Dimension::rpct(100)` utilisé pour la largeur de la carte oscillateur (`card.w(...)`) — `Rpct` =
« % de la taille de la RACINE/FENÊTRE » (`components.hpp:45`), PAS « % du parent immédiat »
(c'est `Pct`, une unité différente). `card` est un vrai enfant de `left` (colonne 320px), pas une
racine — avec `rpct(100)` il se voyait donc attribuer 100% de la largeur de la FENÊTRE (1280px)
au lieu de 100% de la largeur de `left`, débordant largement du panneau de gauche (repéré sur
capture Xvfb : un seul bouton de forme d'onde « Sine » gigantesque au lieu des 5 boutons attendus,
le toggle marche/arrêt invisible car poussé hors du panneau visible). Root-cause : confusion entre
`Rpct` et `Pct` — `rpct(100)` n'est correct QUE sur une entité RACINE (où parent == fenêtre, donc
les deux unités coïncident, ce qui masque le bug dans TOUS les exemples existants qui l'utilisent
UNIQUEMENT sur des racines — `ui_aero_basics.cpp`'s `desktop`/`content`, et le `right` de la Phase 4
elle-même, qui EST une racine donc son usage de `rpct` y était correct). Corrigé en remplaçant par
`Dimension::pct(100)` sur `card` (le seul endroit de ce fichier où l'entité n'est PAS une racine).
Leçon retenue : `rpct`/`pct` ne sont interchangeables que sur une entité racine — à vérifier
explicitement à chaque nouvel usage plutôt que de copier un patron d'exemple existant sans
regarder si le contexte (racine vs enfant) est le même.

**Vérification visuelle réelle (Xvfb + xwd + ffmpeg)** : capture initiale (aucun oscillateur actif,
3 premiers pré-configurés Sine/Triangle/Carré avec leurs boutons de forme correctement surlignés,
5 boutons de forme bien distribués sur toute la largeur de la carte après le fix ci-dessus) ;
capture avec un clic simulé (`xdotool`) sur le toggle de l'Oscillateur 1 (Sine, 262 Hz, amp 0.5,
volume maître 0.7) — confirme le pipeline DSP réellement CALCULÉ, pas juste affiché : sinusoïde
propre et régulière dans le plot de forme d'onde, pic spectral net et unique en basse fréquence
(cohérent avec 262 Hz sur un axe 0-20000 Hz), et surtout **RMS/Peak numériquement vérifiés** :
affichage -12.2 dB RMS / -9.1 dB Peak, à comparer aux valeurs théoriques attendues pour une
sinusoïde d'amplitude 0.5 × gain maître 0.7 : `Peak = 20·log10(0.5×0.7) ≈ -9.12 dB` et
`RMS = 20·log10(0.5×0.7/√2) ≈ -12.14 dB` — concordance quasi exacte, confirmant que la chaîne
complète (synthèse → soft-clip → mesure RMS/Peak → conversion dB → normalisation barre de
progression) est mathématiquement correcte de bout en bout, pas seulement « ça s'affiche ».

**Régression complète** : tous les tests (mêmes 23/24 qu'avant, `ecs_smoke_test` toujours
l'unique exception pré-existante et sans lien) passent. 11/11 exemples (les 10 précédents + le
nouveau `audio_signal_generator`) tournent sainement.

## Phase 6 — Exemple `audio_patchbay` (le plus gros morceau, et le plus dur à déboguer)

Nouveau `examples/audio_patchbay.cpp` (~1200 lignes) — PAS un port littéral de SDL3pp/examples/
audio/07_audio_patchbay.cpp (qui réplique un éditeur de node-graph fait main + fils Bézier dessinés
à la main). Réutilise `ui::nodegraph.hpp` (chantier `[Historique]` Node-Graph) TEL QUEL — wiring
manuel des systèmes (`ArchetypeRegistry`/`LayoutSystem`/`InputSystem`/`RenderSystem`/`StyleSystem`/
`UiFactory`/`NodeGraphSystem`, patron exact de `ui_node_graph_demo.cpp`, pas la façade `ui::Ui` qui
ne permet pas d'intercaler `NodeGraphSystem` entre layout et rendu). 10 types de blocs : Audio In/
Out, Filter (LowPass/HighPass, pas BandStop — audio::BiQuadKind existant), Amp, Mixer (Add/
Multiply/Average — 3 modes, pas les 4 de l'original qui ajoutait un "WeightedDynamic" custom),
Delay (écho à contre-réaction, formule portée directement — DSP générique), Oscillator (formes
naïves non limitées en bande, portées de l'Osc du PATCHBAY original — différent de l'Oscillator
band-limited de la Phase 5, l'original utilisait déjà 2 implémentations distinctes pour ces 2
exemples), Scope/Spectrum (`ui::UiPlot` — Décision #6 du plan — au lieu des canvas dessinés à la
main de l'original), Playlist (`sdl3::AudioDecoder`, Phase 3, + `sdl3::dialog::showOpenFile` —
sélecteur de fichiers natif OS plutôt que l'explorateur d'arborescence fait main de l'original,
simplification déraisonnable à répliquer pour peu de valeur ajoutée).

**Modèle DSP délibérément SÉPARÉ de l'ECS** (`namespace dsp` : `BlockType`/`AudioBus`/
`AudioInState`...`PlaylistState`/`NodeState` = `std::variant<...>` des 10/`Node`/`Connection`/
`Graph`) — respecte la règle FERME du plan (Décision #7) : le thread audio de fond
(`sdl3::Thread`, Phase 1) ne touche JAMAIS `ecs::ArchetypeRegistry`, seulement `dsp::Graph` protégé
par UN SEUL `sdl3::Mutex` partagé (pas un double instantané séparé UI→DSP/DSP→UI comme évoqué dans
le plan — un unique verrou couvrant tout `dsp::Graph` est plus simple et tout aussi correct
puisque les DEUX sens y passent par le MÊME verrou). Topologie du graphe (`graph.connections`)
RE-DÉRIVÉE chaque frame des `GraphConnection` visuelles du node-graph (`resyncConnections()`,
traduction pin Entity → (nodeId,port,isOutput) via une table `pinInfo` tenue par l'app) plutôt que
mise à jour par callback — plus simple, coût négligeable pour un graphe de quelques dizaines de
connexions max, et couvre nativement les connexions faites par glisser-déposer (mécanisme déjà
intégré à `nodegraph.hpp`, pas de hook `onConnect` à ajouter).

**Bug latent de l'ORIGINAL corrigé en simplifiant, pas porté tel quel** : le patchbay SDL3pp
original ne régénère un bloc Oscillator/Playlist que si `out.valid==false` (`if (out.valid) break;`)
— un système de contre-pression qui ne fonctionne QUE pour une chaîne à un seul maillon (Osc→
AudioOut direct, seul cas où le bus lu par AudioOut EST le bus de sortie de l'Osc) : dès qu'un nœud
intermédiaire s'intercale (Osc→Filter→AudioOut), seul le bus DIRECTEMENT lu par AudioOut est
invalidé — celui de Filter, pas celui d'Osc — donc Osc ne régénère plus JAMAIS après le premier
appel, et Filter re-filtre indéfiniment le même bloc figé (l'oscillateur "gèle" dès qu'un nœud le
sépare de la sortie). Corrigé en simplifiant : tous les nœuds régénèrent INCONDITIONNELLEMENT à
chaque appel de `processGraph()`, le thread de fond étant cadencé à ~`kBufferSize/kSampleRate`
secondes (~23 ms pour 1024@44100) plutôt qu'un poll fixe à 5 ms — AudioOut reste l'unique garde de
contre-pression réelle via `Queued()` (Phase 3).

### Le vrai morceau : un bug de mise en page confirmé, pré-existant, dans `ui::nodegraph.hpp`

**Symptôme** : un bloc nouvellement ajouté au patchbay s'affichait comme une esquille de quelques
pixels de large collée au bord gauche de la fenêtre au lieu de sa boîte complète (~200-320px) à sa
position demandée — alors que `UiGraphNode::canvasPos`/`size` ET `UiItem.width/height` (Px explicite)
ET `UiRect.offset` étaient TOUS les trois confirmés corrects par instrumentation directe
(`std::cerr` sur les composants bruts). Isolé avec certitude, par élimination méthodique (chaque
variable changée UNE À LA FOIS, avec capture d'écran ou lecture directe de `UiComputed` après
chaque changement) :
- **PAS causé par** : le type de contenu (testé avec un `f.label()` unique, toujours cassé), la
  taille demandée (testée à plusieurs valeurs), le texte du titre (`"Playlist"` vs `"ZZZTEST"` vs
  `"Audio In"` réattribué à un AUTRE type de bloc — cassé et pas cassé dans les deux sens selon des
  combinaisons qui semblaient d'abord corréler puis se sont avérées être une fausse piste), la
  position dans le tableau de la barre d'outils, ni — point de départ de l'investigation — le fait
  d'avoir zéro pin sur un côté (`numIn==0` OU `numOut==0` : Filter avec du contenu INCHANGÉ mais
  `numIn` forcé à 0 reproduit le bug ; `Mixer`, qui A des pins des deux côtés, s'est AUSSI avéré
  cassé une fois testé à un id/ordre de création différent — invalidant l'hypothèse « un seul
  côté » comme cause UNIQUE, mais elle restait un déclencheur partiel).
- **Contournement partiel appliqué en premier** (gardé, inoffensif) : quand `numIn==0` ou
  `numOut==0`, un pin invisible (`alpha=0`, `connectable=false`) est ajouté sur le côté manquant —
  a réellement corrigé AudioIn/AudioOut/Oscillator/Scope/Spectrum de façon reproductible, mais PAS
  Mixer (qui a pourtant des pins des deux côtés nativement) ni Playlist (même avec le spacer) une
  fois testés à un ordre de création différent — preuve que ce n'était qu'UN symptôme partiel, pas
  la cause racine.
- **CORRECTIF RÉEL, qui élimine le bug pour LES 10 TYPES DE BLOCS, sans exception, vérifié sur
  capture d'écran** : à la fin de `addBlock()`, forcer une résolution de mise en page SYNCHRONE et
  INCONDITIONNELLE (`nodeGraph.prepass(ar); layout.run(ar, winW, winH); nodeGraph.updatePins(ar);`
  — `layout.run()`, pas `layout.runIfNeeded()`) immédiatement après la création du nœud, plutôt que
  de compter sur `layout.markDirty()` + la passe `runIfNeeded()` naturelle de la frame suivante
  dans la boucle principale. Root cause probable (non confirmée avec certitude absolue, mais
  cohérente avec toutes les observations) : un nœud ajouté via `.parent(canvasEntity)` explicite +
  `AddComponent(e, UiGraphNode{})` APRÈS `.Spawn()` (patron de `addGraphNode()`, cf. Décision #1
  du chantier Node-Graph) échappe au premier cycle `markDirty()`→`runIfNeeded()` d'une façon qui ne
  se manifeste pas de façon fiable pour les 2-3 premiers nœuds d'une session (d'où les faux
  positifs "Filter marche" en tout début d'investigation) mais devient systématique au-delà — le
  déclencheur exact (compteur d'entités ? ré-allocation d'une table interne du LayoutSystem à une
  certaine taille ?) n'a PAS été identifié avec certitude dans le temps disponible, mais le
  correctif (résolution synchrone immédiate, qui contourne le mécanisme dirty-flag entièrement
  pour le nœud fraîchement créé) est robuste et vérifié empiriquement sur les 10 types de blocs
  simultanément, y compris les deux cas qui restaient cassés avec le seul contournement par pin
  invisible (Mixer, Playlist).
- **À signaler pour une future session** : ce bug affecte potentiellement TOUT usage de
  `ui::nodegraph.hpp` qui ajoute des nœuds dynamiquement en cours de session (pas seulement ce
  patchbay) — `ui_node_graph_demo.cpp` ne l'a jamais rencontré car tous ses nœuds sont créés une
  fois au début AVANT la première frame (donc AVANT que `layout.runIfNeeded()` ait jamais tourné,
  contexte différent). Root-cause précise à investiguer dans `lib/ui/nodegraph.hpp`/`systems.hpp`
  (LayoutSystem) si le temps le permet un jour — piste la plus prometteuse : le cache `measured`
  (mémoïsation de `LayoutSystem::measure()`) ou l'interaction entre `AddComponent()` post-spawn et
  la garde dirty-flag de `runIfNeeded()`.

**Vérification** : régression complète propre (mêmes 23/24 tests, `ecs_smoke_test` seule exception
pré-existante ; 12/12 exemples sains). Vérification visuelle réelle (Xvfb) : capture avec LES 10
TYPES DE BLOCS simultanément à l'écran, chacun avec son contenu complet et ses pins correctement
positionnés (confirmé après le correctif de synchronisation ci-dessus — AVANT ce correctif, 3-4
blocs sur 10 s'affichaient en esquille cassée selon l'ordre de création). Connexion par glisser-
déposer NON vérifiée par capture d'écran automatisée (la simulation de drag `xdotool mousedown/
mousemove/mouseup` n'a pas été fiable dans ce harnais Xvfb sans gestionnaire de fenêtres — limite
de l'outil de test, pas un défaut applicatif constaté) — vérifiée à la place par relecture attentive
du code de `resyncConnections()`/`pinInfo` (traduction correcte pin→port, dédoublonnage par port
d'entrée, pins invisibles bien exclus de `pinInfo` donc non connectables) et par le fait que le
mécanisme de glisser-déposer lui-même est du code PARTAGÉ, déjà testé dans `ui_node_graph_demo.cpp`
(chantier `[Historique]` antérieur).

Avec Phase 6, les QUATRE parties de la demande initiale de ce chantier (Makefile/PCH, wrapper
threading, module resources, 3 exemples audio) sont maintenant TERMINÉES.
