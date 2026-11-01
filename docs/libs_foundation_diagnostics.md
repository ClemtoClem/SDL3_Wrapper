# diagnostics — journal, assertions, instrumentation et capture de performances

`libs/foundation/diagnostics` · couche **foundation** · `libdiagnostics.a` · espaces de noms `diagnostics::` et `diagnostics::trace::`

## Présentation

Ce module réunit quatre services, sans dépendance à SDL :

1. **Journal** (`Logger`) : niveaux, catégories, sinks interchangeables
   (console, fichier, mémoire, ou toute `std::function`).
2. **Assertions** (`DIAG_ASSERT`, `DIAG_VERIFY`) : leur gestionnaire d'échec
   est remplaçable, et elles n'utilisent pas d'exceptions.
3. **Chronométrage agrégé** (`Profiler`, `ScopedTimer`, `DIAG_PROFILE_SCOPE`) :
   durées moyennes, minimales et maximales par nom, et compteurs.
4. **Capture de performances** (`diagnostics::trace`), dans l'esprit de Google
   Orbit :
   - zones chronométrées, images, pistes de valeurs et intervalles
     asynchrones ;
   - échantillonnage des piles d'appels ;
   - suivi des fils, de la mémoire et des compteurs matériels.

   Les captures sont écrites au format `.vcap` (JSON) et analysées par le
   [profiler](demo_profiler.md).

Le module dépend de [`core`](libs_foundation_core.md), pour `String` et
`Result` dans l'API de capture. Les commentaires de `diagnostics.hpp` qui
disent le contraire datent d'avant `trace.hpp`.

[`platform`](libs_infrastructure_platform.md) raccorde `SDL_Log` au journal.
`ecs`, `ui`, `render3d`, `voxel` et `voxelia` sont instrumentés avec
`DIAG_ZONE`.

## Architecture interne

| Fichier | Contenu |
|---|---|
| `include/diagnostics/diagnostics.hpp` | En-tête parapluie (`log`, `assert`, `profiler`) |
| `log.hpp`, `src/log.cpp` | `Level`, `Record`, `Logger`, `ConsoleSink`, `FileSink`, `MemorySink`, macros `DIAG_*` |
| `assert.hpp`, `src/assert.cpp` | `AssertionFailure`, `SetAssertHandler`, `DIAG_ASSERT`, `DIAG_VERIFY` |
| `profiler.hpp`, `src/profiler.cpp` | `TimingStats`, `Profiler`, `ScopedTimer`, `DIAG_PROFILE_SCOPE` |
| `trace.hpp`, `src/trace.cpp` | Capture de performances (à inclure à part : il n'est pas dans le parapluie) |

## API publique

### Journal (`log.hpp`)

- `enum class Level { Trace, Debug, Info, Warn, Error, Critical, Off }` et `LevelName(level)`.
- `struct Record { level, category, message, time, thread, file, line }`. Les vues ne sont valides que pendant l'appel du sink.
- `using Sink = std::function<void(const Record&)>` et `FormatRecord(record)`, qui produit `2026-10-17 13:45:12.345 [Info ] render: message`.

| Membre de `Logger` | Rôle |
|---|---|
| `static Logger &Global()` | Journal du processus (utilisé par les macros) |
| `size_t AddSink(Sink)`, `RemoveSink(id)`, `ClearSinks()` | Gestion des sinks |
| `SetLevel(Level)` / `GetLevel()` | Niveau minimal global (défaut `Info`) |
| `SetCategoryLevel(cat, Level)`, `ClearCategoryLevels()` | Niveau propre à une catégorie (prioritaire) |
| `bool IsEnabled(level, cat)` | Filtre |
| `Write(level, cat, message, file, line)`, `Log(level, cat, fmt, args…)` | Émission (`std::format`) |

Sinks fournis :

- **`ConsoleSink()`** : écrit sur stderr.
- **`FileSink(path)`** : ajoute au fichier, qui est vidé à chaque message de
  niveau `Error` ou plus. Rend un sink vide si le fichier ne peut pas être
  ouvert.
- **`MemorySink(capacity)`** : conserve les N derniers messages.
  - `AsSink()` rend le sink à passer à `AddSink` ;
  - `Entries()`, `Count(auMoins)` et `Clear()` permettent de les consulter.
  - On s'en sert pour une console intégrée ou dans les tests.

Macros (journal global) :

- `DIAG_LOG(level, cat, fmt, …)` ;
- `DIAG_TRACE`, `DIAG_DEBUG`, `DIAG_INFO`, `DIAG_WARN`, `DIAG_ERROR` et
  `DIAG_CRITICAL (cat, fmt, …)`.

Toutes les méthodes sont sûres entre fils. Les sinks sont appelés sous verrou,
l'un après l'autre : un sink ne doit donc pas rappeler le `Logger`.

> `DIAG_TRACE(cat, …)` est la macro de journal au niveau `Trace`. Elle n'a
> rien à voir avec la variable d'environnement `DIAG_TRACE` de la capture
> (voir plus bas).

### Assertions (`assert.hpp`)

- **`DIAG_ASSERT(cond, "fmt", args…)`** n'est vérifiée que si
  `DIAG_ENABLE_ASSERTS` vaut 1, ce qui est le cas par défaut hors `NDEBUG`.
  En mode `release`, elle disparaît.
- **`DIAG_VERIFY(cond, …)`** est **toujours** évaluée et rend `cond`. Usage
  typique : `if (!DIAG_VERIFY(ptr, "…")) return;`
- **En cas d'échec**, un message `Critical` est journalisé dans la catégorie
  `"assert"`, puis le gestionnaire est appelé. Par défaut, il appelle
  `std::abort`.
- **`SetAssertHandler(handler)`** remplace le gestionnaire et rend l'ancien ;
  `nullptr` restaure le défaut. Les tests s'en servent pour vérifier qu'une
  assertion échoue sans arrêter le programme.

### Chronométrage agrégé (`profiler.hpp`)

| Élément | Rôle |
|---|---|
| `TimingStats {count, total, min, max}` | `TotalMs`, `MeanMs`, `MinMs`, `MaxMs` |
| `Profiler::Global()` | Instance du processus |
| `Record(nom, durée)`, `Timing(nom)`, `Timings()` | Mesures (triées par nom) |
| `Increment(compteur, delta)`, `Counter(nom)`, `Counters()` | Compteurs |
| `Reset()` | Remise à zéro |
| `ScopedTimer(nom, profiler)` | Mesure sa propre durée de vie (`steady_clock`) |
| `DIAG_PROFILE_SCOPE(nom)` | Chronomètre la portée courante |

### Capture de performances (`trace.hpp`, `diagnostics::trace::`)

| Élément | Rôle |
|---|---|
| `struct Options` | `maxZonesPerThread` (2²⁰), `samplingHz` (1000, échantillons de pile par seconde de temps CPU ; 0 = aucun), `maxSamples` (200 000), `processHz` (100, relevés du processus et des fils), `hardwareCounters` (vrai) |
| `bool Start(options)` / `bool Stop()` / `bool Active()` | Démarre (efface la capture précédente ; faux si une capture est déjà en cours) et arrête |
| `Result<bool, String> Save(path)` | Écrit la dernière capture (après `Stop`) au format `.vcap` |
| `bool StartFromEnvironment()` | Lit les variables d'environnement décrites plus bas ; vrai si une capture a démarré |
| `void PollControl()` | À appeler à chaque image : traite les demandes reçues par `SIGUSR2` |
| `void FinishFromEnvironment()` | En fin de programme : arrête et écrit la capture lancée par l'environnement |
| `SetThreadName(nom)`, `const char *Intern(nom)` | Nom du fil ; chaîne durable pour un nom de zone calculé |
| `Frame()`, `Value(nom, v)`, `AsyncBegin/AsyncEnd(nom, id)`, `NowNs()` | Images, pistes de valeurs, intervalles qui ne suivent pas la pile, horloge (`CLOCK_MONOTONIC`) |
| `class Zone` | Portée chronométrée (RAII) |
| `DIAG_ZONE(nom)`, `DIAG_FRAME()`, `DIAG_VALUE(nom, v)` | Macros d'instrumentation |

Hors capture, une zone ne coûte que la lecture d'un booléen atomique. Le nom
d'une zone doit vivre jusqu'à `Save` : utilisez un littéral ou `Intern()`.

**Variables d'environnement** (lues par `StartFromEnvironment`) :

| Variable | Effet |
|---|---|
| `DIAG_TRACE=fichier.vcap` | Capture dès le démarrage, écrite à la sortie par `FinishFromEnvironment` |
| `DIAG_TRACE_HZ=N` | Fréquence d'échantillonnage des piles (0 = aucun) |
| `DIAG_TRACE_CONTROL=1` | `SIGUSR2` démarre et arrête une capture. Chaque arrêt écrit `<base>.<n>.vcap` |
| `DIAG_TRACE_CONTROL=wait` | Comme `1`, sans démarrer de capture au lancement : le profiler l'enverra lui-même |

## Fonctionnement de la capture

- **Zones** : chaque fil a son propre tampon de zones (début, fin,
  profondeur). Il n'y a pas de verrou sur le chemin rapide.
- **Échantillonnage des piles** : une minuterie `SIGPROF` mesure le temps CPU
  du processus, et `backtrace` relève la pile du fil interrompu. Les adresses
  sont symbolisées **hors ligne** par le profiler, grâce à la carte des
  modules (`/proc/self/maps`) jointe à la capture.
- **Échantillonneur du processus** : un fil dédié lit `/proc` à `processHz`.
  Il relève :
  - tous les fils, y compris ceux de SDL et du pilote GPU, avec leur temps
    CPU et leur état ;
  - la mémoire résidente et le tas (`mallinfo2`) ;
  - les défauts de page et les changements de contexte ;
  - les compteurs matériels `perf_event` (cycles, instructions, défauts de
    cache, mauvaises prédictions), si `perf_event_paranoid` vaut 2 au plus.
- **Plate-forme** : l'échantillonnage, `/proc`, `perf_event` et `SIGUSR2` ne
  sont compilés que sous Linux (`#ifdef __linux__`). Ailleurs, seules les
  zones, images et valeurs sont enregistrées.

## Exemple

```cpp
#include <cstdio>
#include <diagnostics/diagnostics.hpp>
#include <diagnostics/trace.hpp>

static int Work(int n) {
	DIAG_PROFILE_SCOPE("work"); // statistiques agrégées
	DIAG_ZONE("work");          // zone dans la capture
	int acc = 0;
	for (int i = 0; i < n; ++i)
		acc += i % 7;
	return acc;
}

int main() {
	auto &log = diagnostics::Logger::Global();
	log.AddSink(diagnostics::ConsoleSink());
	diagnostics::MemorySink memory(64);
	log.AddSink(memory.AsSink());
	log.SetCategoryLevel("noisy", diagnostics::Level::Error);

	DIAG_INFO("app", "démarrage, {} arguments", 1);
	DIAG_DEBUG("app", "invisible : niveau global Info");
	DIAG_WARN("noisy", "filtré par la catégorie");

	int value = 0;
	if (!DIAG_VERIFY(value == 0, "valeur inattendue {}", value))
		return 1;

	diagnostics::trace::Start();
	for (int frame = 0; frame < 3; ++frame) {
		DIAG_VALUE("frame.work", Work(100000));
		DIAG_FRAME();
	}
	diagnostics::trace::Stop();
	Result<bool, String> saved = diagnostics::trace::Save("data/profiler/captures/demo.vcap");

	for (auto &[name, stats] : diagnostics::Profiler::Global().Timings())
		DIAG_INFO("perf", "{} : {} appels, {:.3f} ms en moyenne", name, stats.count, stats.MeanMs());
	std::printf("messages en mémoire : %zu, capture écrite : %d\n", memory.Count(), saved.IsOk());
}
```

Pour instrumenter une application entière, sur le modèle de `voxelia` :

```cpp
int main() {
	bool running = true;
	int frame = 0;
	diagnostics::trace::StartFromEnvironment();
	while (running) {
		DIAG_ZONE("frame");
		// … mise à jour, rendu …
		DIAG_FRAME();
		diagnostics::trace::PollControl();
		running = ++frame < 600;
	}
	diagnostics::trace::FinishFromEnvironment();
}
```

```sh
DIAG_TRACE=data/profiler/captures/jeu.vcap ./build/host/release/bin/voxelia
```

## Intégration

`modules = ["diagnostics"]`. Le journal ne s'affiche que si un sink est
ajouté : aucun n'est installé par défaut.

## Tests

```sh
cpp-maker test -m diagnostics
```

Les 14 tests couvrent le journal, les assertions, le chronométrage et une
capture complète.
