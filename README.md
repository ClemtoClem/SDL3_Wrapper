# SDL3_Wrapper

Wrapper C++23 de SDL3 et ensemble de bibliothèques pour construire des
applications et des jeux : rendu 2D/3D, UI retenue sur ECS, audio, physique,
réseau, scripts, formats de données, archives. Projet modulaire construit et
testé exclusivement avec **cpp-maker** (≥ 0.6, source : `cpp-maker-v0.6.cpp`).

## Démarrage

```sh
./cpp-maker build                  # tout le workspace (debug)
./cpp-maker test                   # compile et lance les tests de chaque module
./cpp-maker test -m physics        # un module (et ses dépendances)
./cpp-maker run ui_showcase        # lancer un exemple ou une démo
./cpp-maker run sandbox -- --help  # arguments après --
./cpp-maker modules                # modules, couches, dépendances
./cpp-maker graph --dot | dot -Tsvg > graph.svg
./cpp-maker doctor                 # environnement + dépendances implicites (#include)
```

Recompiler l'outil lui-même :
`g++ -std=c++23 -O2 -pthread -Wall -Wextra cpp-maker-v0.6.cpp -o cpp-maker`.

Dépendances système : SDL3, SDL3_ttf, SDL3_image, SDL3_mixer, SDL3_net
(pkg-config), shaderc et SPIRV-Cross (render3d), `glslc` ou
`glslangValidator` + `spirv-cross` pour les shaders.

## Architecture en couches

Trois couches, de la plus basse à la plus haute. **Un module ne dépend que de
sa couche ou d'une couche inférieure** : la règle est déclarée dans
`cpp-maker.toml` (`[layers]`) et chaque `module.toml` (`layer = "…"`), et
cpp-maker refuse de construire un projet qui la viole. La couche Foundation
interdit en plus toute dépendance (même transitive) aux paquets SDL.

```
application     scene · emulator · sandbox · examples/* · integration
                    │
infrastructure  sdl3 → platform, ecs, dataio, archive, resources, generators,
                       render3d → ui, audio, script → script_bindings,
                       physics, net, sql
                    │
foundation      core · math · jobs · events · diagnostics      (sans SDL)
```

### Niveau A — Foundation (`libs/foundation/`)

| Module | Rôle |
|---|---|
| `core` | `String`/`StringView`, `Option`/`Result` (aucune exception), `Ref`, UUID, `Wrapper` RAII |
| `math` | Vecteurs, matrices, quaternions, AABB, plans, rayons, frustums, `Transform2D/3D`, `Corners`/`Sides` |
| `jobs` | Vivier de fils et `ParallelFor` bloquant (bibliothèque standard) |
| `events` | `Signal<Args...>` à connexions RAII, `EventBus` typé (immédiat ou différé, file thread-safe) |
| `diagnostics` | `Logger` à niveaux/catégories et sinks, `DIAG_ASSERT`/`DIAG_VERIFY`, `Profiler`/`ScopedTimer` |

### Niveau B — Infrastructure et moteurs (`libs/infrastructure/`)

| Module | Rôle | Dépend de |
|---|---|---|
| `sdl3` | Wrappers C++ de SDL3, SDL_ttf, SDL_image, SDL_mixer, SDL_net | core, math |
| `platform` | `Application` (cycle de vie), `FixedStepClock`, `InputState`, événements SDL → bus, SDL_Log → diagnostics | sdl3, events, diagnostics |
| `ecs` | Entités, composants (archétypes), requêtes | core |
| `dataio` | JSON, XML, YAML, TOML, INI, CSV, CSS, HTML, glTF sur un arbre `dataio::Node` commun | core, sdl3 |
| `archive` | ZIP, 7z, RAR, TAR, ISO, gzip/bzip2/xz/zstd/LZMA/PPMd, AES | core, sdl3 |
| `resources` | Ressources, cache, chargement asynchrone, catalogue d'assets | sdl3, dataio |
| `generators` | Bruit, cartes de hauteur, terrains, labyrinthes, donjons, WFC, L-systèmes, Markov | math, ecs |
| `render3d` | Rendu SDL_GPU : graphe de scène, PBR, ombres, IBL, animation, portails | resources, ecs, dataio |
| `audio` | Clips audio (ressources), DSP | resources, sdl3 |
| `ui` | UI déclarative sur ECS : builders, styles CSS, évènements façon JS, widgets, flexbox, thèmes, node-graph, graphiques, viewport 3D | ecs, render3d, dataio |
| `script` | Langage de script : lexer, parseur, interpréteur, bibliothèque standard | math, sdl3, dataio |
| `script_bindings` | Liaisons du langage vers `ecs::`, `ui::`, `generators::` | script, ecs, ui, generators |
| `physics` | Corps rigides sur l'ECS : formes, phases large/étroite, solveur, articulations | math, ecs, jobs |
| `net` | Réseau au-dessus de SDL_net : découverte locale, chat ad hoc | sdl3, archive |
| `sql` | Base embarquée : parseur SQL, B+arbre, stockage binaire/JSON/CSV | sdl3, dataio |

### Niveau C — Application

| Module | Rôle |
|---|---|
| `libs/application/scene` | Graphe de scène : nœuds, propriétés, scènes empaquetées, registre de types |
| `demos/emulator` | Émulateur NDS / GBA / GBC (cœur dérivé de NooDS) avec coquille « EmulOS » |
| `demos/sandbox` | Éditeur de jeu 2D/3D (projets, scènes, scripts, physique, assets) |
| `demos/voxelia` | Jeux 3D dans un environnement voxel cubique généré procéduralement |
| `examples/<nom>` | 18 exemples, un exécutable chacun (`./cpp-maker run <nom>`) |
| `tests/integration` | Tests qui composent plusieurs modules (ex. cache de ressources + fabriques render3d/audio) |

## Conventions

- **Inclusions** : toujours `<module/fichier.hpp>` entre modules ; les
  guillemets ne servent qu'à l'intérieur d'un même dossier.
- **Espaces de noms** = noms de modules (`dataio::`, `archive::`, `script::`,
  `render3d::`…).
- **Aucune exception** : `Option<T>` / `Result<T, E>`. Seuls les tests en
  ignorent la règle quand c'est inévitable.
- **Testabilité** : la logique doit se tester sans fenêtre ni GPU. Les
  modules Foundation n'ont pas accès à SDL ; `platform` sépare la logique pure
  (`FixedStepClock`, `InputState`, `TranslateEvent`) de la boucle SDL.

## Tests

Chaque module a un dossier `tests/` compilé en un exécutable
(`build/<toolchain>/<mode>/tests/<module>-tests`). Le gabarit `test.hpp`
accepte deux styles :

```cpp
TEST(Nom)          { CHECK(x); CHECK_EQ(a, b); REQUIRE(ptr); }
TEST(Suite, Nom)   { EXPECT_TRUE(x); EXPECT_EQ(a, b); ASSERT_TRUE(ptr); }
```

`REQUIRE`/`ASSERT_*` arrêtent le test ; `SKIP("raison")` l'ignore (les tests
qui ont besoin d'une fenêtre ou d'un backend SDL_GPU le font quand il n'y en a
pas). Les tests s'exécutent depuis la racine du projet : les chemins de
données sont relatifs à la racine (`assets/…`, `demos/sandbox/tests/data/…`).
En mode `asan`, `tests/lsan_suppressions.txt` est appliqué automatiquement
(`[tests.env]`).

Les shaders (`assets/shaders/src`) sont recompilés par cpp-maker avant le
build quand une source change (`[generate.shaders]`).

## Correspondance avec l'ancien projet (`../SDL3_Wrapper`)

| Ancien | Nouveau |
|---|---|
| `lib/include/data/{node,document,json,…}` | `dataio` (`dataio::`) |
| `lib/include/data/archive/*` | `archive` (`archive::`) |
| `lib/include/data/script/*` | `script` + `script_bindings` (`script::`) |
| `lib/*/jobs, net, physics, render3d, sql` | modules homonymes |
| `lib/scene` | `libs/application/scene` |
| `examples/emulator_demo` | `demos/emulator` (`emulator::`) |
| `examples/sandbox` | `demos/sandbox` (`sandbox::`) |
| `examples/*.cpp` | `examples/<nom>/src/main.cpp` |
| `tests/*_smoke_test.cpp` | `tests/` du module concerné |
| `saves/emulator_demo`, `saves/sandbox` | `saves/emulator`, `saves/sandbox` |
| `docs/*.md` | `docs/history/` (notes de conception d'origine, chemins d'époque) |
| `Makefile` | `cpp-maker.toml` + `module.toml` |

Changements d'API notables pendant la migration : `math` ne dépend plus de
SDL (`FVector2` n'hérite plus de `sdl3::FPoint`, conversions implicites dans
les deux sens côté sdl3 ; `math::Sqrt/Abs/Clamp…` remplacent `sdl3::…` pour le
code sans SDL), `sdl3::Corners`/`Sides` sont des alias de `math::`,
`jobs::JobSystem` repose sur `std::thread`, `generators` utilise
`std::uint32_t` au lieu de `Uint32`, et les liaisons `script_ecs/ui/generator`
s'incluent désormais par `<script_bindings/…>`.
