# Architecture et conventions

Ce document décrit l'organisation du dépôt et les règles de code communes à
tous les modules. La compilation est traitée dans
[compilation](compilation.md).

## Organisation du dépôt

| Dossier | Contenu | Versionné |
|---|---|---|
| `libs/foundation/` | Modules sans SDL : `core`, `math`, `jobs`, `events`, `diagnostics` | oui |
| `libs/infrastructure/` | Wrapper SDL3 et moteurs : `sdl3`, `platform`, `ecs`, `dataio`, `archive`, `resources`, `generators`, `audio`, `modeling`, `render3d`, `ui`, `editor`, `script`, `script_bindings`, `physics`, `voxel`, `net`, `sql` | oui |
| `libs/application/` | `scene` | oui |
| `demos/` | Applications complètes : `emulator`, `sandbox`, `voxelia`, `profiler` | oui |
| `examples/` | 18 exemples d'un seul fichier | oui |
| `tests/` | Tests d'intégration, fichiers de suppression des sanitizers | oui |
| `assets/` | Ressources en lecture seule : polices, modèles, textures, sons, sources des shaders, BIOS de l'émulateur | sauf `shaders/bin/`, `roms/` et les `*.bin` |
| `data/` | Données écrites par les applications : réglages, sauvegardes, projets, captures de performances | non (`.gitignore`) |
| `captures/` | Captures d'écran utilisées par la documentation | oui |
| `docs/` | Cette documentation | oui |
| `script-vscode/` | Extension VS Code du langage Script | oui (sauf `out/`, `node_modules/`) |
| `build/`, `dist/`, `.out/`, `.backups/` | Produits de cpp-maker | non |

Chaque module suit la même disposition :

```
<module>/
  module.toml          configuration cpp-maker
  include/<module>/    en-têtes publics (#include <module/fichier.hpp>)
  src/                 implémentation
  tests/               tests (test.hpp, test_runner.cpp, test_*.cpp)
```

## Couches

```
application     scene · emulator · sandbox · voxelia · profiler · examples/* · tests/integration
                    │
infrastructure  sdl3 → platform, ecs, dataio, archive, resources, generators, audio,
                       modeling, render3d → ui → editor, script → script_bindings,
                       physics, voxel, net, sql
                    │
foundation      core · math · jobs · events · diagnostics          (sans SDL)
```

- Un module ne dépend que de sa couche ou d'une couche inférieure.
  cpp-maker le vérifie (`[layers]` et `layer = "…"` de chaque
  `module.toml`).
- La couche **foundation** n'a accès à aucun paquet SDL, même indirectement
  (`forbid_pkg_config`). Le code qui doit tourner sans SDL, et être testé
  sans fenêtre, y trouve sa place.

### Graphe des dépendances entre modules

| Module | Dépend de |
|---|---|
| `core`, `math`, `jobs`, `events` | — |
| `diagnostics` | core |
| `sdl3` | core, math + SDL3, SDL3_ttf, SDL3_image, SDL3_mixer, SDL3_net |
| `platform` | core, sdl3, events, diagnostics |
| `ecs` | core, diagnostics |
| `dataio`, `archive` | core, sdl3 |
| `resources` | core, sdl3, dataio |
| `generators` | core, math, ecs, dataio |
| `audio` | resources, sdl3, math |
| `modeling` | core, math, dataio |
| `render3d` | core, diagnostics, math, sdl3, ecs, dataio, resources + shaderc, SPIRV-Cross |
| `ui` | core, diagnostics, ecs, math, sdl3, dataio, render3d |
| `editor` | core, math, sdl3, ecs, render3d, ui |
| `script` | core, math, sdl3, dataio |
| `script_bindings` | script, ecs, ui, generators, audio, archive, physics, sql, dataio, render3d |
| `physics` | core, math, ecs, jobs |
| `voxel` | core, diagnostics, math, events, dataio, ecs, generators, resources, script, sql, archive |
| `net` | core, sdl3, archive |
| `sql` | core, sdl3, dataio |
| `scene` | core, math, sdl3, dataio |

`cpp-maker graph --dot | dot -Tsvg > graph.svg` dessine ce graphe.

## Conventions de code

- **C++23** : le dépôt compile avec `-Wall -Wextra -Wpedantic -Wconversion
  -Wsign-conversion -Wshadow…`, sans avertissement en mode release.
- **Aucune exception.** Une opération qui peut échouer rend `Option<T>` ou
  `Result<T, E>` ([core](libs_foundation_core.md)).
  - Les interpréteurs, les codecs et les archives propagent leurs erreurs
    comme des valeurs.
  - Le langage Script n'utilise pas non plus d'exceptions, même pour
    `return`, `break` et `continue`.
  - Le code du dépôt ne lève pas d'exception. Les deux `catch` présents
    interceptent celles d'une bibliothèque tierce (SPIRV-Cross, dans
    `render3d/src/shader_compiler.cpp`) ou du chargement asynchrone
    (`resources/src/cache.cpp`), pour les convertir en erreurs.
- **Chaînes** : `String` et `StringView` de `core`, pas `std::string`. Il
  existe un constructeur d'interopérabilité depuis `std::string` pour les API
  standard. S'il manque une fonction à `String`, on l'ajoute à `String`.
- **Pointeurs C** : aucun pointeur C brut (SDL ou autre) n'apparaît dans les
  signatures publiques du wrapper `sdl3`.
  - Les ressources possédées dérivent de `Wrapper<T, Deleter>`, les
    ressources empruntées de `Borrowed<T>`.
  - Les structures C sont recopiées dans des structures C++ à champs nommés.
  - Les paramètres de sortie deviennent des types de retour.
- **Inclusions** : `#include <module/fichier.hpp>` entre modules ; les
  guillemets ne servent qu'à l'intérieur d'un même dossier.
- **Espaces de noms** : celui de chaque module porte son nom (`dataio::`,
  `render3d::`, `ui::`…). Les types de `core` sont dans l'espace de noms
  global.
- **Testabilité** : la logique se teste sans fenêtre ni GPU.
  - Exemples : `FixedStepClock` et `InputState` (platform), `Game`
    (voxel), gizmo et navigation (editor), outils de `modeling`.
  - Le document de la sandbox est séparé de son runtime pour la même raison.
- **Nommage** : les types et fonctions publiques en *PascalCase* (`Load`,
  `GetSize`, `IsEmpty`), les membres privés préfixés `m_`, les constantes en
  `MAJUSCULES`.

## Données et ressources à l'exécution

Les applications désignent `assets/` et `data/` par des **chemins relatifs à
la racine du dépôt**. Cela vaut aussi pour les exécutables de test, que
cpp-maker lance depuis la racine.

| Application | Lit | Écrit |
|---|---|---|
| emulator | `assets/roms`, `assets/bios-firmware` | `data/emulator/config.ini`, `data/emulator/states/` |
| sandbox | `assets/`, `demos/sandbox/tests/data/` | `data/sandbox/projects/` |
| voxelia | `demos/voxelia/mods/`, `assets/` | `data/voxelia/` (réglages, sauvegardes, captures) |
| profiler | `data/profiler/captures/` | `data/profiler/settings.json`, `data/profiler/captures/` |
| ui (tous) | `assets/fonts` (DejaVu, MaterialIcons, CupertinoIcons) | — |
| render3d | `assets/shaders/bin`, `assets/textures` | — |

## Tests

- Chaque module a son dossier `tests/`, compilé en un exécutable
  `build/<toolchain>/<mode>/tests/<module>-tests`.
- Le gabarit `test.hpp` et la commande `cpp-maker test` sont décrits dans
  [compilation](compilation.md#tests).
- Les tests qui composent plusieurs modules sont dans
  [tests/integration](tests_integration.md).
- Les applications se vérifient aussi de bout en bout par leur ligne de
  commande : `--headless`, `--frames`, `--screenshot`, `--report` (voir
  chaque [démo](demos.md)).
