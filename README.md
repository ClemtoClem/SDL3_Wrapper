# SDL3_Wrapper

Wrapper C++23 de SDL3 et ensemble de bibliothèques pour construire des
applications et des jeux 2D/3D, accompagnés de quatre applications complètes
qui les mettent en œuvre.

**Licence :** GNU Affero General Public License v3.0 (voir `LICENSE`).

## Présentation

SDL3_Wrapper part d'un **wrapper C++ de SDL3** (`sdl3::`) : des classes RAII,
sans exception ni pointeur C dans les signatures. Il ajoute au-dessus une
pile de modules indépendants qui couvre ce qu'une application interactive
demande :

- **Rendu** : 2D par `SDL_Renderer` ; 3D sur l'API GPU de SDL3, avec PBR,
  ombres, éclairage par image (IBL), animation et rendu hors écran.
- **Interface** : déclarative, construite sur un **ECS**, avec feuilles CSS,
  évènements façon JavaScript, plus de 50 widgets, graphiques, graphes de
  nœuds, vues 3D intégrées et plusieurs fenêtres.
- **Données** : JSON, XML, YAML, TOML, INI, CSV, CSS, HTML, glTF ; export
  OBJ, STL et PLY ; archives zip, 7z, tar, ISO et RAR avec compression et
  chiffrement ; base SQL embarquée.
- **Simulation et contenu** : physique de corps rigides, génération
  procédurale, modélisation 3D à la SketchUp, moteur de jeu voxel piloté
  par des mods.
- **Scripts** : un langage interprété embarqué, « Script », relié aux
  modules du dépôt.
- **Son** : moteur audio et traitement du signal.
- **Réseau** : réseau local et Wi-Fi ad hoc.
- **Outils** : journalisation, assertions, capture de performances et
  profileur.

Les **applications** montrent la bibliothèque à l'œuvre et servent de banc
d'essai :

- un émulateur Nintendo DS / GBA / GBC ;
- un éditeur de jeux 2D/3D (sandbox) ;
- un jeu voxel avec son éditeur intégré (voxelia et Voxelia Studio) ;
- un profileur inspiré de Google Orbit.

**Technologies** : C++23, SDL3 ≥ 3.4 avec SDL3_ttf, SDL3_image, SDL3_mixer et
SDL3_net ; shaderc et SPIRV-Cross pour les shaders ; Vulkan (ou Metal) par
SDL_GPU. Le projet se construit avec
[cpp-maker](https://github.com/ClemtoClem/cpp-maker), sous Linux.

```sh
cpp-maker build                      # tout le dépôt (debug)
cpp-maker test                       # tous les tests
cpp-maker run release -m voxelia     # lancer une application
```

## Architecture

Le dépôt est un workspace cpp-maker de **47 modules**, répartis en trois
couches. Un module ne dépend que de sa couche ou d'une couche inférieure, et
cpp-maker vérifie cette règle.

| Couche | Modules | Rôle |
|---|---|---|
| **foundation** (`libs/foundation`) | `core`, `math`, `jobs`, `events`, `diagnostics` | Types de base, mathématiques, fils, évènements, diagnostics ; aucune dépendance à SDL |
| **infrastructure** (`libs/infrastructure`) | `sdl3`, `platform`, `ecs`, `dataio`, `archive`, `resources`, `generators`, `audio`, `modeling`, `render3d`, `ui`, `editor`, `script`, `script_bindings`, `physics`, `voxel`, `net`, `sql` | Wrapper SDL3 et moteurs |
| **application** | `libs/application/scene`, `demos/*`, `examples/*`, `tests/integration` | Graphe de scène, applications, exemples, tests d'intégration |

Les ressources en lecture seule sont dans `assets/`. Les données écrites par
les applications (réglages, sauvegardes, projets, captures de performances)
vont dans `data/`. Les applications se lancent depuis la racine du dépôt.
Le détail est dans [architecture](docs/architecture.md).

## Documentation

### Général

- [Compilation](docs/compilation.md) : prérequis, installation de cpp-maker, compilation, modes, tests, sanitizers, nettoyage, résolution des problèmes.
- [Architecture et conventions](docs/architecture.md) : organisation du dépôt, couches, graphe des dépendances, règles de code, données à l'exécution.

### Modules — couche foundation

- [core](docs/libs_foundation_core.md) : `String`/`StringView`, `Option`/`Result` (sans exception), `Ref`, enveloppes RAII, `Uuid`, Unicode.
- [math](docs/libs_foundation_math.md) : vecteurs, matrices, quaternions, volumes, rayons, frustums, transformations, polygones et triangulation.
- [jobs](docs/libs_foundation_jobs.md) : vivier de fils et `ParallelFor`.
- [events](docs/libs_foundation_events.md) : signaux typés et bus d'évènements immédiat ou différé.
- [diagnostics](docs/libs_foundation_diagnostics.md) : journal, assertions, chronométrage, capture de performances `.vcap`.

### Modules — couche infrastructure

- [sdl3](docs/libs_infrastructure_sdl3.md) : wrapper C++ de SDL3, SDL3_ttf, SDL3_image, SDL3_mixer et SDL3_net.
- [platform](docs/libs_infrastructure_platform.md) : cycle de vie d'une application, pas fixe, état des entrées.
- [ecs](docs/libs_infrastructure_ecs.md) : ECS à archétypes, ordonnanceur de systèmes, évènements, relations.
- [dataio](docs/libs_infrastructure_dataio.md) : JSON, XML, YAML, TOML, INI, CSV, CSS, HTML sur un arbre commun ; glTF ; export de maillages.
- [archive](docs/libs_infrastructure_archive.md) : zip, 7z, tar, ISO, RAR, gzip, xz, bzip2, zstd, AES, système de fichiers virtuel.
- [resources](docs/libs_infrastructure_resources.md) : ressources, cache, chargement asynchrone, catalogue d'assets à identifiants stables.
- [generators](docs/libs_infrastructure_generators.md) : bruits, cartes de hauteur, terrains, labyrinthes, donjons, WFC, L-systèmes, Markov.
- [audio](docs/libs_infrastructure_audio.md) : sons partagés, moteur audio (voix, canaux, fondus, spatialisation), DSP.
- [modeling](docs/libs_infrastructure_modeling.md) : maillage éditable et outils de modélisation à la SketchUp.
- [render3d](docs/libs_infrastructure_render3d.md) : rendu 3D sur SDL_GPU : scène, matériaux, lumières, ombres, IBL, animation, hors écran.
- [ui](docs/libs_infrastructure_ui.md) : interface déclarative sur l'ECS : builders, CSS, évènements, widgets, plusieurs fenêtres.
- [editor](docs/libs_infrastructure_editor.md) : briques des éditeurs 3D : docks, navigation orbitale, gizmo, palette de commandes.
- [script](docs/libs_infrastructure_script.md) : langage de script interprété embarqué.
- [script_bindings](docs/libs_infrastructure_script_bindings.md) : les modules du dépôt vus depuis Script.
- [physics](docs/libs_infrastructure_physics.md) : corps rigides 3D sur l'ECS, articulations, portails.
- [voxel](docs/libs_infrastructure_voxel.md) : moteur de jeu voxel piloté par les données (mods, monde, entités, sauvegardes).
- [net](docs/libs_infrastructure_net.md) : découverte, chat et fichiers sur un réseau local ; Wi-Fi ad hoc.
- [sql](docs/libs_infrastructure_sql.md) : base de données embarquée, SQL, B+arbre, stockage binaire.

### Modules — couche application

- [scene](docs/libs_application_scene.md) : hiérarchie de nœuds générique, scènes empaquetées, registre de types.

### Applications

- [Démos](docs/demos.md) : présentation des quatre applications.
  - [emulator](docs/demo_emulator.md) : émulateur Nintendo DS, GBA et GBC « EmulOS ».
  - [sandbox](docs/demo_sandbox.md) : éditeur de jeux 2D/3D.
  - [voxelia](docs/demo_voxelia.md) : jeu voxel et Voxelia Studio.
  - [profiler](docs/demo_profiler.md) : profileur de programmes instrumentés, inspiré de Google Orbit.
- [Exemples](docs/examples.md) : les 18 exemples, classés par module.
  - [platform_app](docs/example_platform_app.md) : application minimale sur `platform::Application`.
  - [renderer](docs/example_renderer.md) : rendu 2D avec `sdl3::Renderer`.
  - [ui_minimal](docs/example_ui_minimal.md) : exemple minimal du module ui.
  - [ui_showcase](docs/example_ui_showcase.md) : vitrine de tous les widgets.
  - [ui_aero_basics](docs/example_ui_aero_basics.md) : thème Frutiger Aero, widgets de base.
  - [ui_aero_lists](docs/example_ui_aero_lists.md) : thème Frutiger Aero, listes, arbres, tableaux.
  - [ui_aero_windows](docs/example_ui_aero_windows.md) : thème Frutiger Aero, bureau et fenêtres internes.
  - [ui_node_graph_demo](docs/example_ui_node_graph_demo.md) : éditeur de graphes de nœuds.
  - [ui_plot_demo](docs/example_ui_plot_demo.md) : galerie des graphiques.
  - [ui_shader_effects_demo](docs/example_ui_shader_effects_demo.md) : effets de shader sur des widgets.
  - [ui_viewport3d_demo](docs/example_ui_viewport3d_demo.md) : scène 3D dans des widgets.
  - [render3d_cube](docs/example_render3d_cube.md) : premier rendu render3d.
  - [render3d_showcase](docs/example_render3d_showcase.md) : vitrine render3d.
  - [render3d_shadows_ibl_showcase](docs/example_render3d_shadows_ibl_showcase.md) : ombres et éclairage par image.
  - [audio_signal_generator](docs/example_audio_signal_generator.md) : générateur de signaux.
  - [audio_spectrum_analyzer](docs/example_audio_spectrum_analyzer.md) : analyseur de spectre.
  - [audio_patchbay](docs/example_audio_patchbay.md) : routage audio par graphe de nœuds.
  - [adhoc_chat_demo](docs/example_adhoc_chat_demo.md) : chat et fichiers en réseau local ou Wi-Fi ad hoc.

### Documentation complémentaire

- [Guide du langage Script](docs/script_language.md) : syntaxe, bibliothèque standard, intégration C++.
- [Extension VS Code](docs/tools_script_vscode.md) : coloration, diagnostics et navigation pour les fichiers `.script`.
- [Tests d'intégration et suppressions](docs/tests_integration.md) : tests qui composent plusieurs modules, fichiers de suppression des sanitizers.

Les captures d'écran de la documentation sont dans [`captures/`](captures/).
