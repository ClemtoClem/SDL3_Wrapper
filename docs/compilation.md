# Compilation de SDL3_Wrapper

SDL3_Wrapper se construit, se teste et se lance **exclusivement** avec
[cpp-maker](https://github.com/ClemtoClem/cpp-maker). Il n'y a ni CMake ni
Makefile. Ce document explique comment préparer la machine, puis comment
compiler, configurer, tester et nettoyer le projet.

## Prérequis

### Plate-forme

Le projet est développé et vérifié sous **Linux** (Ubuntu 26.04, GCC 15.2).
Plusieurs parties sont propres à Linux :

| Partie | Dépendance Linux |
|---|---|
| Capture de performances | `diagnostics::trace` : `/proc`, `SIGPROF`, `perf_event`, `SIGUSR2` |
| Profiler | Lecture ELF, `fork`/`exec`, `addr2line` |
| Wi-Fi ad hoc | `net::wifi` : NetworkManager, `nmcli` |

Le reste n'utilise que SDL3 et la bibliothèque standard. `cpp-maker.toml`
déclare aussi une chaîne croisée ARM (`armhf`, voir
[Toolchains](#toolchains-et-compilation-croisée)).

### Outils

| Outil | Rôle | Remarque |
|---|---|---|
| **cpp-maker** ≥ 0.6.0 | Construction, tests, exécution | `minimum_cpp_maker_version` de `cpp-maker.toml` ; la version 0.7.0 est utilisée ici |
| Compilateur **C++23** (et C17) | GCC ou Clang | Le projet impose `standard = "c++23"`. cpp-maker lui-même se compile avec g++ 13+ ou clang 16+ |
| `ar`, `pkg-config` | Archives statiques, découverte des bibliothèques | Indispensables |
| `glslc` (shaderc), ou à défaut `glslangValidator` | Shaders GLSL → SPIR-V | **Obligatoire** : `assets/shaders/compiler.sh` génère les shaders au premier build |
| `spirv-cross` (programme) | SPIR-V → MSL (Metal) | Facultatif : sans lui, seul le SPIR-V est régénéré |
| `dxc` | DXIL (Direct3D 12) | Facultatif, ignoré avec un avertissement |
| `ninja` | Backend `--backend ninja` | Facultatif |
| `valgrind`, `gdb`, `addr2line` | Mode `valgrind`, débogage, symbolisation du profiler | Facultatifs |
| Node.js et npm | Extension VS Code `script-vscode` | Facultatifs, voir [tools_script_vscode](tools_script_vscode.md) |

### Bibliothèques

Le module [`sdl3`](libs_infrastructure_sdl3.md) déclare les bibliothèques
SDL **par pkg-config** ; elles doivent donc être trouvées par
`pkg-config --modversion <paquet>`.

| Paquet pkg-config | Bibliothèque | Version minimale | Version utilisée ici |
|---|---|---|---|
| `sdl3` | SDL3 | **3.4.0** (`SDL_CreateGPURenderer`, `SDL_GetGPURendererDevice`) | 3.5.0 |
| `sdl3-ttf` | SDL3_ttf | 3.x | 3.3.0 |
| `sdl3-image` | SDL3_image | 3.x | 3.5.0 |
| `sdl3-mixer` | SDL3_mixer | 3.x (API `MIX_*`) | 3.3.0 |
| `sdl3-net` | SDL3_net | 3.x (API `NET_*`) | 3.3.0 |
| `shaderc` | shaderc (module `render3d`) | — | 2026.1.1 |

Le module [`render3d`](libs_infrastructure_render3d.md) se lie en plus aux
bibliothèques **SPIRV-Cross** `spirv-cross-msl`, `spirv-cross-glsl`,
`spirv-cross-core` et `spirv-cross-util` (déclarées en `system`).

Sur la machine de développement :

- SDL3 et ses extensions sont compilés depuis leurs sources et installés
  dans `/usr/local`.
- shaderc et SPIRV-Cross viennent des paquets Ubuntu `libshaderc-dev`,
  `glslc` et `libspirv-cross-c-shared-dev`. Ce dernier fournit les
  bibliothèques statiques `libspirv-cross-*.a`.

Pour exécuter les applications, il faut aussi :

- **un GPU compatible SDL_GPU** : Vulkan sous Linux et Windows, Metal sous
  macOS. Les tests qui en ont besoin sont ignorés (`SKIP`) sans GPU ;
- **un pilote vidéo** : `SDL_VIDEO_DRIVER=offscreen` permet de tout lancer
  sans écran.

## cpp-maker dans SDL3_Wrapper

### Rôle

cpp-maker est un moteur de production pour C et C++, écrit en C++23. Il lit
la configuration TOML du projet et :

- découvre les sources ;
- compile en parallèle et de façon incrémentale ;
- exécute les étapes de génération (shaders) ;
- lie les bibliothèques et les exécutables ;
- lance les programmes et les tests, sanitizers compris ;
- vérifie les couches d'architecture ;
- produit `compile_commands.json` ;
- emballe le projet (`tar.gz`).

### Fichiers de configuration

| Fichier | Rôle |
|---|---|
| `cpp-maker.toml` (racine) | Projet, couches, liste des modules (`[workspace]`), chaînes de compilation, modes, avertissements, génération des shaders, environnement des tests, empaquetage |
| `<module>/module.toml` | Un par module : nom, couche, type (`static`, `executable`, `header-only`), sources, en-têtes publics, dépendances (`modules`, `pkg_config`, `system`), options de compilation, dossier de tests |
| `.cpp-maker.lock` | Verrou posé pendant un build (non versionné) |

Extrait de `cpp-maker.toml` :

```toml
[project]
name = "SDL3_Wrapper"
standard = "c++23"
c_standard = "c17"
minimum_cpp_maker_version = "0.6.0"

[layers]
order = ["foundation", "infrastructure", "application"]

[layers.foundation]
forbid_pkg_config = ["sdl3", "sdl3-ttf", "sdl3-image", "sdl3-mixer", "sdl3-net", "shaderc"]

[workspace]
modules = ["libs/foundation/core", "libs/foundation/math", …, "demos/profiler"]
```

### Modules et couches

Le dépôt est un **workspace** de 47 modules :

- 24 bibliothèques statiques ;
- 4 démos exécutables ;
- 18 exemples exécutables ;
- 1 module de tests d'intégration.

Chaque module appartient à une couche, et cpp-maker **refuse de construire**
un projet où un module dépend d'une couche supérieure :

```
application     scene · demos/* · examples/* · tests/integration
infrastructure  sdl3, platform, ecs, dataio, archive, resources, generators, audio, modeling,
                render3d, ui, editor, script, script_bindings, physics, voxel, net, sql
foundation      core · math · jobs · events · diagnostics          (aucun paquet SDL, même indirect)
```

Les dépendances d'un module sont tirées automatiquement : `-m ui` compile
aussi `core`, `math`, `sdl3`, `ecs`, `dataio`, `render3d`…

Pour inspecter le graphe :

```sh
cpp-maker modules                           # tableau : module, type, couche, dossier, dépendances
cpp-maker graph                             # arbre des dépendances
cpp-maker graph --dot | dot -Tsvg > graph.svg
cpp-maker info -m voxel                     # détail d'un module, fermeture transitive, dépendants
cpp-maker doctor --no-fix                   # environnement + dépendances implicites (#include)
```

## Préparer l'environnement

### 1. Installer cpp-maker

Le dépôt officiel ne publie pas de binaire : on compile l'outil depuis ses
sources. Commande donnée par son manuel (README, v0.8.0), à lancer dans le
dossier des sources :

```sh
git clone https://github.com/ClemtoClem/cpp-maker.git
cd cpp-maker            # les sources .cpp sont dans src/ selon le README
g++ -std=c++23 -O2 -pthread -o cpp-maker \
    main.cpp util.cpp proc.cpp toml.cpp toolchain.cpp cfg.cpp \
    build.cpp deps.cpp elf.cpp output.cpp sanitize.cpp \
    cmd-backup.cpp cmd-build.cpp cmd-config.cpp cmd-deploy.cpp \
    cmd-helpers.cpp cmd-project.cpp
sudo install -m 755 cpp-maker /usr/local/bin/cpp-maker
cpp-maker --version
```

Le manuel indique de placer le binaire dans le `PATH`
(`/usr/local/bin/cpp-maker`). Sous Windows (MinGW-w64), remplacez `-pthread`
par `-static-libgcc -static-libstdc++`.

### 2. Installer les dépendances

Sur Debian/Ubuntu :

```sh
sudo apt install build-essential pkg-config git \
                 libshaderc-dev glslc libspirv-cross-c-shared-dev
```

SDL3 (≥ 3.4) et ses extensions, s'ils ne sont pas fournis par la
distribution en version suffisante, se compilent depuis leurs dépôts
officiels et s'installent dans `/usr/local`, d'où pkg-config les trouve. Il
s'agit de SDL, SDL_ttf, SDL_image, SDL_mixer et SDL_net.

Vérification :

```sh
pkg-config --modversion sdl3 sdl3-ttf sdl3-image sdl3-mixer sdl3-net shaderc
cpp-maker doctor --no-fix
```

`cpp-maker doctor` contrôle la configuration, les outils externes (`ar`,
`pkg-config`, `ninja`, `valgrind`…), les compilateurs et les sanitizers
disponibles.

**Sans `--no-fix`**, `doctor` **modifie les `module.toml`** pour y ajouter
les dépendances déduites des `#include`.

### 3. Terminal de VS Code (snap)

Dans le terminal intégré de VS Code installé en **snap**, les variables
`GTK_PATH` et `GTK_MODULES` pointent vers les bibliothèques GTK du snap.
Les programmes lancés depuis ce terminal héritent de ces variables, et SDL
peut charger GTK (dialogues natifs, zone de notification). Retirez-les pour
les commandes du projet :

```sh
env -u GTK_PATH -u GTK_MODULES cpp-maker run -m voxelia
```

## Compiler

Toutes les commandes se lancent **depuis la racine du dépôt**.

```sh
cpp-maker build                         # tout le workspace, mode debug (défaut du projet)
cpp-maker build release                 # mode release (le mode est l'argument positionnel)
cpp-maker build -m voxelia              # un module et ses dépendances
cpp-maker build -m ui,editor --tests    # plusieurs modules, avec leurs exécutables de test
cpp-maker build -j 4                    # 4 tâches parallèles (défaut : cœurs - 1)
cpp-maker build -n                      # aperçu des tâches, sans rien lancer
cpp-maker build -k                      # continue après une erreur
cpp-maker build -v                      # affiche chaque commande
cpp-maker build --backend ninja         # génère build.ninja et l'exécute
cpp-maker build -m ui --output ui.log   # journal complet dans .out/ui.log
```

La compilation est **incrémentale**. L'empreinte d'une tâche couvre :

- la version du compilateur et la ligne de commande ;
- la taille et la date de chaque entrée ;
- les `#include` transitifs.

Modifier un en-tête ne recompile donc que ce qui l'inclut.

### Lancer une application

```sh
cpp-maker run -m sandbox                       # compile (debug) puis lance
cpp-maker run release -m voxelia               # version optimisée
cpp-maker run -m emulator -- --help            # arguments après « -- » (ou --args)
cpp-maker run --mode valgrind -m ui_minimal    # sous valgrind
```

> Le module se désigne par **`-m`**. `cpp-maker run ui_minimal` est refusé
> (« mode inconnu »), car l'argument positionnel est le mode. Les
> applications lisent `assets/` et écrivent dans `data/` **par des chemins
> relatifs** : lancez-les depuis la racine du dépôt.

### Fichiers générés

| Chemin | Contenu |
|---|---|
| `build/<toolchain>/<mode>/bin/` | Exécutables (`emulator`, `sandbox`, `voxelia`, `profiler`, exemples) |
| `build/<toolchain>/<mode>/lib/` | Bibliothèques statiques (`libcore.a`, `libui.a`…) |
| `build/<toolchain>/<mode>/obj/` | Fichiers objets |
| `build/<toolchain>/<mode>/tests/` | Exécutables de test (`<module>-tests`) |
| `build/<toolchain>/<mode>/compile_commands.json` | Base de compilation, régénérée à chaque build |
| `build/generate/` | Empreintes des étapes de génération |
| `.out/` | Journaux demandés par `--output`, `--stdout` et `--stderr` (`[paths] out`) |
| `.backups/` | Exports de `cpp-maker backup` (`[paths] backups`) |
| `dist/` | Paquets de `cpp-maker package` |

Exemple : `build/host/release/bin/voxelia`. Avec un sanitizer, le mode
dérivé a son propre dossier, par exemple `build/host/debug+address/`.

Pour l'intégration avec un éditeur (clangd, extension C/C++ de VS Code),
générez la base de compilation à la racine :

```sh
cpp-maker generate compdb               # écrit ./compile_commands.json
```

### Shaders

`cpp-maker.toml` déclare une étape de génération, **exécutée automatiquement
avant le build** quand une de ses entrées change :

```toml
[generate.shaders]
command = "assets/shaders/compiler.sh"   # GLSL → SPIR-V / MSL (/ DXIL si dxc)
inputs = ["assets/shaders/src", "assets/shaders/compiler.sh"]
outputs = ["assets/shaders/bin"]
```

`compiler.sh` compile chaque `assets/shaders/src/*.vert|frag` :

1. en **SPIR-V** (`.spv`), avec `glslc -O` ou `glslangValidator -V` ;
2. en **MSL** (`.msl`), avec le programme `spirv-cross` s'il est présent ;
3. en **DXIL**, avec `dxc` s'il est présent.

L'absence de `spirv-cross` ou de `dxc` donne un avertissement, pas une
erreur.

Les shaders compilés (`assets/shaders/bin/`) **ne sont pas versionnés**, car
`.gitignore` exclut `bin*/`. Sur un dépôt fraîchement cloné, le premier
build les produit : `glslc` ou `glslangValidator` est donc **obligatoire**.
Les fichiers `.msl` ne servent que sous macOS (Metal).

Les shaders PBR et voxel générés **à l'exécution** par
`render3d::ShaderBuilder` sont compilés par la bibliothèque shaderc liée au
programme. Ils ne passent pas par cette étape.

## Configuration

### Modes de compilation

Le mode se donne en argument positionnel (`cpp-maker build release`) ou par
`--mode`.

| Mode | Options | Origine | Usage |
|---|---|---|---|
| `debug` (**défaut**) | `-O0 -g3 -DDEBUG` | redéfini par le projet | Développement ; `DIAG_ASSERT` actif |
| `release` | `-O2 -DNDEBUG` | redéfini par le projet | Version optimisée (voxelia, mesures) |
| `relwithdebinfo` | `-O2 -g -DNDEBUG` | prédéfini | Profilage, analyse de plantage |
| `minsize` | `-Os -DNDEBUG` | prédéfini | Taille minimale |
| `asan` | base `debug` + `sanitizer = "address"` | redéfini par le projet | Erreurs mémoire et fuites |
| `tsan` | `-O1 -g3 -fsanitize=thread` | prédéfini | Courses de données |
| `ubsan` | `-O1 -g3 -fsanitize=undefined` | prédéfini | Comportements indéfinis |
| `msan` | `-fsanitize=memory` | prédéfini, Clang seulement | Mémoire non initialisée |
| `lsan` | `-fsanitize=leak` | prédéfini | Fuites seulement |
| `valgrind` | `-O0 -g3`, exécuté sous valgrind | prédéfini | Memcheck (raccourci `cpp-maker valgrind`) |

Ajouter un mode au projet :

```toml
[build.modes.asan-ub]
base = "debug"
sanitizer = "address,undefined"
```

### Avertissements et options par module

Le projet active pour tous les modules :

```
-Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wconversion -Wsign-conversion
-Wdouble-promotion -Wundef -Wcast-align -Wcast-qual -Woverloaded-virtual -Wnon-virtual-dtor
```

Options propres à certains modules (`[flags] cxxflags` de leur
`module.toml`) :

| Module | Options | Raison |
|---|---|---|
| `sdl3` | `-Werror -Wsign-compare` | Aucun avertissement toléré dans le wrapper |
| `emulator` | `-Wsign-compare -O2` | `-O2` **même en debug** : émulation temps réel |

### Tests

```sh
cpp-maker test                          # tous les modules, mode debug
cpp-maker test -m physics               # un module (et la compilation de ses dépendances)
cpp-maker test -m ui --filter Pointer   # seulement les tests dont le nom contient « Pointer »
cpp-maker test release -m voxel
cpp-maker test --timeout 3000           # délai par exécutable (défaut : [tests] timeout = 300 s)
cpp-maker test --junit rapport.xml      # rapport JUnit
```

- Chaque module a un dossier `tests/`, compilé en un exécutable
  `build/<tc>/<mode>/tests/<module>-tests`, lancé **depuis la racine du
  dépôt**.
- Le module `voxelia` porte son délai à 1800 s (`[tests] timeout` de son
  `module.toml`).

Le gabarit `tests/test.hpp`, généré par cpp-maker, accepte deux styles :

```cpp
TEST(Nom)          { CHECK(x); CHECK_EQ(a, b); REQUIRE(ptr); }
TEST(Suite, Nom)   { EXPECT_TRUE(x); EXPECT_EQ(a, b); ASSERT_TRUE(ptr); }
```

- `CHECK` et `EXPECT_*` continuent le test après un échec ; `REQUIRE` et
  `ASSERT_*` l'arrêtent.
- `SKIP("raison")` ignore un test, par exemple sans fenêtre ni GPU.
- Code de sortie : **0** si tout réussit, **1** au moindre échec, **77** si
  tous les tests exécutés ont été ignorés.

Sans écran (intégration continue, SSH) :

```sh
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy cpp-maker test
```

### Sanitizers et vérifications

```sh
cpp-maker test --mode asan                          # AddressSanitizer (+ LeakSanitizer)
cpp-maker test -m ui --sanitize address,undefined   # sanitizers ajoutés au mode courant
cpp-maker test -m ecs --sanitize all --timeout 3000 # une passe par runtime : address+undefined, thread, leak…
cpp-maker check                                     # tests en debug, asan et tsan, rapport agrégé
cpp-maker check --modes debug,asan,ubsan
```

Les fuites et courses connues **des bibliothèques tierces** (pilotes X11 et
Mesa, dbus, SDL_net) sont ignorées grâce aux fichiers de `tests/`,
transmis par `[tests.env]` :

```toml
[tests.env]
LSAN_OPTIONS = "suppressions={root}/tests/lsan_suppressions.txt"
TSAN_OPTIONS = "suppressions={root}/tests/tsan_suppressions.txt"
VALGRIND_OPTS = "--suppressions={root}/tests/valgrind.supp"
```

Voir [tests_integration](tests_integration.md).

### Toolchains et compilation croisée

| Toolchain | Définition | État |
|---|---|---|
| `host` | `cc = "auto"`, `cxx = "auto"` | Défaut (`default_toolchain`) |
| `armhf` | `prefix = "arm-linux-gnueabihf-"` | Déclarée |
| `aarch64`, `win64` (MinGW-w64), `clang-arm64` | Exemples commentés dans `cpp-maker.toml` | À décommenter |

```sh
cpp-maker toolchain list                # toolchains et préfixes détectés
cpp-maker build release -t armhf        # compilation croisée ARM 32 bits
cpp-maker build -t all                  # toutes les toolchains configurées
```

Une compilation croisée exige les bibliothèques SDL3, shaderc et SPIRV-Cross
**de la cible** (sysroot) ; elle n'a pas été vérifiée pour ce projet.

### Empaquetage

```sh
cpp-maker package                       # dist/SDL3_Wrapper-<version>-<os>-<arch>.tar.gz
cpp-maker package release --format tar.gz,zip
cpp-maker install --prefix /opt/sdl3_wrapper
```

`[package]` demande le format `tar.gz` et des binaires dépouillés de leurs
symboles (`strip = true`). Les ressources (`assets/`) ne sont **pas**
ajoutées automatiquement à l'archive, car `extra_files` n'est pas renseigné.

## Nettoyer et tout recompiler

```sh
cpp-maker clean                         # supprime build/
cpp-maker clean release                 # seulement build/<toolchain>/release
cpp-maker clean -t host release         # une toolchain et un mode
cpp-maker clean --all                   # build/ et dist/
cpp-maker build --clean                 # vide le dossier du mode puis recompile tout
```

`clean` ne touche jamais aux sources. Sans `--all`, il laisse
`build/generate/` (empreintes des shaders) : pour forcer la régénération des
shaders, supprimez ce dossier ou modifiez une source GLSL.

## Résolution des problèmes

| Symptôme | Cause et solution |
|---|---|
| `Error: mode inconnu : 'voxelia'` | Le module est passé en position. Écrivez `cpp-maker run -m voxelia` |
| `Package sdl3 was not found in the pkg-config search path` | SDL3 n'est pas installé, ou son `.pc` n'est pas dans le chemin. Vérifiez `pkg-config --modversion sdl3`, et `PKG_CONFIG_PATH=/usr/local/lib/pkgconfig` pour une installation dans `/usr/local` |
| `undefined reference to SDL_CreateGPURenderer` | SDL3 est antérieur à 3.4.0. Installez une version plus récente |
| `cannot find -lspirv-cross-msl` | Bibliothèques statiques SPIRV-Cross absentes. Installez `libspirv-cross-c-shared-dev` (Ubuntu) ou compilez SPIRV-Cross |
| `ERREUR : ni glslc ni glslangValidator trouvés` | Étape `[generate.shaders]` : installez `glslc` ou `glslang-tools` |
| `AVERTISSEMENT : spirv-cross introuvable` | Seul le SPIR-V est régénéré ; sans conséquence sous Linux (Vulkan) |
| Un module de la couche foundation est refusé | Il dépend (même indirectement) d'un paquet SDL interdit par `[layers.foundation] forbid_pkg_config`. Déplacez le code dans une couche supérieure |
| Erreur de couche à la configuration | Un module dépend d'un module d'une couche supérieure. Consultez `cpp-maker graph` |
| `#include` d'un autre module introuvable | La dépendance manque dans le `module.toml`. `cpp-maker doctor` la détecte (`--no-fix` pour seulement la signaler) |
| Un test « SKIP » sans GPU, ou une application sans fenêtre | Pas de périphérique Vulkan, ou pas d'affichage. Utilisez `SDL_VIDEO_DRIVER=offscreen` ; sans GPU, les vues 3D restent vides |
| Ressources introuvables (`assets/…`, mods de voxelia) | L'application a été lancée hors de la racine du dépôt. Relancez depuis la racine |
| `render3d_shadows_ibl_showcase` s'arrête aussitôt | `assets/textures/equirectangularmaps/procedural_sky.hdr` est absent du dépôt |
| Fuites signalées dans `libX11` ou `libdbus` sous ASan | Fuites des bibliothèques tierces, ignorées par `tests/lsan_suppressions.txt` (automatique pour `cpp-maker test`) |
| Comportement étrange depuis le terminal de VS Code | Variables `GTK_PATH` et `GTK_MODULES` du snap. Préfixez avec `env -u GTK_PATH -u GTK_MODULES` |
| Un build attend ou refuse de démarrer | Un autre cpp-maker tient le verrou `.cpp-maker.lock` pendant toute sa compilation (`run` le relâche avant de lancer le programme). Attendez la fin du build en cours |
