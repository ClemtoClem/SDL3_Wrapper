Ce prompt s'appuie sur la documentation existante du dépôt, qu'il faut lire **avant** d'écrire une ligne de code : `project_sandbox_app.md`, `project_sandbox.md`, `project_sandbox_fs.md`, `project_sandbox_objects.md`, `project_sandbox_scripts.md`, `project_sandbox_audio.md`, `project_ui_refonte.md`, `project_ui_ecs_module.md`, `project_voxelia_*.md` (threads, lighting, glow, liquids, furniture, block_states, dimensions, elevators, edit, backrooms_textures), `node_hierarchy.md`, `architecture_moteur.md`. Ces documents **font foi** : ils décrivent l'état réel du moteur, ses conventions, et les pièges déjà payés.

---

## Mission

Concevoir et implémenter **Voxelia Studio**, un environnement de développement intégré (IDE) complet pour le modding, l'édition et le playtest du contenu Voxelia (blocs, entités statiques et animées, structures, biomes, dimensions, scripts, JSON, textures, sons), branché **directement** sur le moteur existant. L'ambition est de fournir aux moddeurs une alternative crédible à ce que sont, dans l'industrie :

- **Godot** (scène / inspecteur / éditeur de script / docks / multi-fenêtres) ;
- **Unreal** (World Outliner, Details, éditeur Blueprint-lite, Live Coding / hot-reload) ;
- **Unity** (browseur de projet, mode Play/Stop, allocation multi-écrans) ;
- **Blender** (éditeurs nodaux, timeline, graphe de courbes, UV/paint, NLA) ;
- **Blockbench** (modélisation voxel/nodebox low-poly, animation, textures par face) ;
- **WorldEdit / MCEdit / Amulet** (édition de structures, formules, presse-papiers) ;
- **Aseprite / GIMP** (peinture pixel, calques, variantes) ;
- **Substance Painter / Krita** (peinture par canal, glow, mipmaps).

Le résultat doit être :

- **Fonctionnel** — chaque panneau modifie **réellement** le contenu du mod et se voit dans la prévisualisation 3D ; aucune coquille vide ;
- **Cohérent** avec l'architecture existante de Voxelia : multithreading `voxel-update` / `voxel-render-N`, viewport **DIRECT** (`sdl3::Texture::FromGpuTexture`), hot-reload, `ContentRegistry`, `Schematic`, `Formula`, `DimensionDef`, `BlockDef`/`states`, `FluidSimulation`, `Lighting`/`MeshVertex::light`, `TextureAtlas::Build/Glow`, `World::Snapshot`, `Game::TravelTo`, `Game::SetBlockState`, `Game::PasteSchematic`, `Game::SetHighlight`, `Worker`, `JobSystem` ;
- **Testable** sans écran (CLI, scénarios scriptés, captures PNG, rapport d'exécution) sur le modèle de `demos/sandbox` ;
- **Maintenable** : séparation **document ≠ runtime**, types forts (`ProjectPath`, `SceneRef`, `NodeRef`, `BlockRef`, `EntityRef`, `StructureRef`, `BiomeRef`, `DimensionRef`…), pas de singleton implicite, pas d'`UiFactory` (l'API `ui` est la refonte déclarative builders + ECS décrite dans `project_ui_refonte.md`).

---

## Contraintes non négociables

1. **Réutiliser `libs/`** — l'IDE est une couche application **par-dessus** les modules existants (`infrastructure/voxel`, `infrastructure/ui`, `infrastructure/render3d`, `infrastructure/sdl3`, `infrastructure/physics`, `infrastructure/ecs`, `infrastructure/dataio` (JSON/CSS/glTF/YAML), `infrastructure/math`, `infrastructure/audio` (via `sdl3/mixer`), `foundation/jobs`, `infrastructure/scene`). Aucun module n'est réécrit ; les manques révélés par l'IDE sont **ajoutés à la bibliothèque** avec leurs tests, comme `sandbox` l'ont fait (6 bugs corrigés + 8 fonctions ajoutées + 4 ruptures de compilation réparées dans `sandbox`).
2. **S'inspirer de `sandbox`** — ne pas réinventer : **étendre et généraliser**. Reprendre tel quel :
   - la séparation **document / runtime** (`project.hpp` / `runtime.hpp` dans `sandbox`) ;
   - les **docks à onglets** redimensionnables, l'inspecteur à sections, le Scene Tree hiérarchique, le manipulateur déplacer/tourner/redimensionner avec magnétisme, l'historique d'annulation par **instantanés** (`BeginEdit`/`EndEdit`, 64 étapes), le mode Play/Stop par instantané, le pilotage par **scénarios scriptés**, le **rapport d'exécution** texte + JSON, `--screenshot=N:PATH`, `--headless`, `--seed`, `--verbose`, `--jobs`, `--viewport`, `--assets-dir`, `--saves-dir`, `--scene=` / `--document=` ;
   - le **navigateur de ressources** typé (`AssetBrowserModel` + `AssetEntry::managed`) et son arborescence complète (Scènes / Scripts / Objets / Structures / Textures / Modèles / Sons / Biomes / Dimensions).
3. **Multithreading préservé** — fil de simulation `voxel-update` séparé des fils de maillage `voxel-render-N` (cf. `project_voxelia_threads.md`). Le viewport 3D utilise le mode **DIRECT**. Un panneau masqué ne se reconstruit pas ; un glissé n'entraîne **qu'un** `BeginEdit`/`EndEdit`. L'IDE **ne doit pas** paralléliser ce que le moteur sérialise déjà (solveur à impulsions physiques, ordre des paires), et la reproductibilité à graine égale (`--seed`) doit être **vérifiée**.
4. **Multi-fenêtres SDL3** (cf. Phase 0 ci-dessous). L'IDE peut afficher le jeu **dans une fenêtre dédiée**, l'édition dans une autre, avec partage d'état cohérent (voir plus bas).
5. **Hot-reload** — un script, une texture, un JSON de bloc, un `states`, un `light`, une dimension, un biome **doit** pouvoir être rechargé **sans quitter le monde actif**. Un rechargement cible `ContentRegistry`, `TextureAtlas::Build` + `ReleaseUnused`, le remaillage des chunks touchés (`TouchSphere`), le recalcul de lumière et d'occlusion. Les dimensions/biomes imposent un **avertissement clair** (le monde est déjà généré) et proposent « régénérer à partir de la graine » dans une **nouvelle dimension temporaire** plutôt que muter le monde vivant (même logique que `Game::TravelTo`).
6. **Tout pilotable** — chaque geste de l'IDE est accessible par **ligne de commande** ou par **script** (`editor.*`, `studio.*`, `gizmo_drag`, `camera.*`, `asset_*`, `node.*`, `world.*`, `structure.*`, `formula.*`, `entity.*`, `game.*`, `audio.*`), pour permettre les scénarios `--scenario=…` + captures + rapport.
7. **Tests** — chaque sous-système livré avec ses tests unitaires **sans fenêtre** (`tests/voxelia_studio_*_smoke_test.cpp`), plus scénarios d'intégration sous `xvfb-run` + ASan/UBSan avec **rc=0** et **stderr vide**. Le rapport recense : scènes/modèles/blocs/biomes/dimensions, threads simultanés max, cadence min/moy/centiles, scripts exécutés, **fonctionnalités visitées et leur statut**, captures écrites, avertissements/erreurs. La vérification de **convention graphique** (orientation, faces visibles, normales, couleur) se fait par **observation de pixels** (cf. `tests/render3d_orientation_smoke_test.cpp` de `sandbox`).
8. **Console de commande à code d'état** (cf. §12 ci-dessous) — pas seulement du texte : `CommandResult { status, message, confirmationPrompt }`. Succès = fermeture, erreur = console maintenue ouverte, avertissement = fermeture différée 5 s, confirmation = prompt `[y/N]` ou boutons.
9. **Zéro sortie prématurée** — Échap sort du mode Play / ferme un dialogue / désélectionne ; **quitter** = fermeture de fenêtre, menu Fichier › Quitter, Ctrl+Q.

---

## Phase 0 — module `ui` : multi-fenêtres SDL3

> **Prérequis.** Avant l'IDE, `libs/infrastructure/ui` (refonte builders + ECS, cf. `project_ui_refonte.md`) ne gère qu'une seule fenêtre. Il faut le doter d'un **contexte multi-fenêtres** propre, sans casser l'API existante (18 exemples, émulateur, voxelia, sandbox, script_bindings doivent continuer à compiler et passer leurs tests : ui 104, sandbox 156, script_bindings 11, émulateur 22).

### Objectifs

- **`ui::WindowManager`** (ou équivalent) : registre de fenêtres SDL3, chacune avec son propre `WindowFrame`/`WindowChrome`/feuille CSS, son propre `SceneManager` / `ecs::ArchetypeRegistry` / `Schedule` (un monde ECS par fenêtre, pas un seul monde partagé — sinon les requêtes système traversent les fenêtres).
- **`ui::WindowContext`** : poignée `{ sdl3::Window*, sdl3::Renderer*, ecs::World*, Widget root }`. `WindowManager::Create(spec)`, `Close(id)`, `Focus(id)`, `Windows()`, `ActiveWindow()`.
- **Rendu** : chaque fenêtre a son `renderer` (`sdl3::Renderer::FromWindow`), son `ui_render` système, son `Present()` ; le main loop `ui::App` itère sur `WindowManager`. La **fermeture d'une fenêtre** déclenche proprement l'arrêt (ou détache le contexte applicatif qui y était attaché — voir plus bas).
- **Partage GPU/Vulkan** : le `render3d::Canvas` (Vulkan) partage son `VkInstance`/device entre fenêtres, avec un `Surface`/`Swapchain` **par fenêtre**. Le mode DIRECT de `Viewport3D` (texture GPU importée) doit pouvoir traverser : un viewport 3D rendu dans la fenêtre A peut-il être affiché dans la fenêtre B ? **Non par défaut** — SDL3 ne garantit pas le partage de textures entre deux `SDL_GPUDevice` distincts. Convention : **un seul device GPU partagé**, une swapchain par fenêtre. Si un viewport 3D doit migrer d'une fenêtre à l'autre, on repasse par READBACK ou PIPELINED (cf. `project_voxelia_threads.md`), jamais par une texture partagée « magique ».
- **Focus & routage évènements** : la fenêtre qui a le focus reçoit les évènements SDL3 ; les raccourcis globaux (Ctrl+Q, Ctrl+Shift+R, F3, F9) fonctionnent dans **toutes** les fenêtres avec routage explicite, pas par variable globale. Chaque `WindowContext` a son `TextFieldHasFocus()` / `PointerCapture()` / `KeyboardCapture()` local.
- **`WindowSpec`** : titre, taille, position (ou centrée), flags (`RESIZABLE`, `BORDERLESS`, `FULLSCREEN`, `HIGH_DPI`, `ALWAYS_ON_TOP`), écran cible (`SDL_GetDisplays`), feuille CSS propre (`ShellCss()`, `StudioCss()`, `GameCss()`).
- **DPI** : `WindowFrame` gère le `SDL_GetDisplayContentScale` par fenêtre (une fenêtre sur écran HiDPI et l'autre sur écran standard doit rester lisible) ; les feuilles CSS peuvent recevoir une unité `rem` mise à l'échelle par fenêtre.
- **Multi-écrans** : `WindowManager::CreateOnDisplay(displayIndex, spec)` pour placer la fenêtre de jeu sur le second moniteur.
- **Tests** : `tests/ui_multi_window_smoke_test.cpp` (sans GPU) — création/fermeture de N contextes, routage d'évènements vers la bonne fenêtre, CSS isolé par fenêtre, `WindowFrame` HiDPI simulé. Et un scénario Xvfb réel montrant deux fenêtres distinctes côte à côte (capture d'écran du bureau entier `--screenshot-desktop=N:PATH`).

### Non-objectifs explicites de la Phase 0

- Pas de fenêtres flottantes **internes** (tool windows style ImGui) — l'IDE utilise des **docks** dans une fenêtre principale, et des **fenêtres SDL3 séparées** pour le jeu et éventuellement la timeline d'animation. Un dock flottant serait un autre chantier.
- Pas de rendu 2D dans le renderer d'une fenêtre A depuis le thread de la fenêtre B — tout est fait par le fil principal de la fenêtre concernée.

### API ajoutée à `libs/infrastructure/ui`

À documenter dans `memory/voxelia_studio_lib_additions.md`, avec tests :
- `ui::WindowManager`, `ui::WindowContext`, `ui::WindowSpec`, `ui::WindowId` ;
- `ui::App::Windows()` et `ui::App::RunWindowed` itérant sur N fenêtres ;
- `ui::WindowFrame::SetContentScale(float)` ;
- extensions CSS `@media (window-id …)`, `@media (display-scale …)` si nécessaire.

---

## Ce que l'application expose (inventaire fonctionnel, étendu)

L'IDE couvre **l'intégralité du contenu d'un mod Voxelia**. Les sections suivantes numérotent les panneaux ; la **Phase 1** ci-dessous détaille les fondations communes.

### 1. Mod Manager (cf. prompt initial, conservé)

- Liste des mods (nom, version, dépendances, ordre), activation/désactivation à chaud, vue des définitions fournies (blocs avec `states`, items, recettes, créatures, dimensions, biomes, scripts, sons, textures).
- **Diagnostic** `ContentRegistry::Validate` (types, négatifs, couleurs 0..1, angles inversés, clés inconnues, parties manquantes, `interact.toggle` inconnu, sons introuvables, `light.range > 32`, `light.innerAngle >= outerAngle`, `optics` hors bornes, `sensor.radius` négatif, `formula` syntaxe, dimension cible inconnue, portail cyclique, `structure`/`schematic` corrompu) — affiché en rouge/orange dans la console et le panneau, avec **lien cliquable** vers le champ fautif (bloc, script, JSON).
- Édition de `mod.json` / `world.json` (éditeur JSON schema-aware, cf. §10).
- Bouton **« Recharger le mod »** (Ctrl+Shift+R) → hot-reload ciblé (cf. §« Hot-reload »).

### 2. Vue 3D temps réel — viewport DIRECT, multi-fenêtre

- Viewport principal qui **fait tourner le jeu** (ou une scène d'aperçu) dans une fenêtre **DIRECT** (`sdl3::Texture::FromGpuTexture`, `TextureAccess::TARGET`), sans `WaitIdle`.
- **Deux modes de fenêtre** : (a) intégré au dock central ; (b) **détaché** dans une fenêtre SDL3 séparée (Phase 0), typiquement sur le second écran — permet le playtest temps réel pendant l'édition.
- Contrôles (cf. `project_sandbox_app.md`) : clic droit vol libre + WASD/EQ, molette = dolly, clic milieu = panoramique, Alt+clic gauche = orbite autour de la sélection, flèches/Page haut-bas sans bouton. Pivot d'orbite **figé au début du geste** ; tangage borné ±1,5 rad.
- Manipulateur **déplacer / tourner / redimensionner** (W/E/R), magnétisme X aux grilles (1, 1/2, 1/4 bloc, angles, facteur) — **un axe à la fois** (tirer X ne doit pas faire sauter Y et Z), pilotable par script (`gizmo_drag(axe, de, à)`).
- **Sélection au clic** par test triangulaire (`render3d::PickMeshFace`), **pas** par boîte englobante (viser le trou d'un tore ne le sélectionne pas). Objets masqués ignorés. `Camera::ScreenPointToRay` + `WorldToScreen` pour le pixel → rayon.
- Overlays : repères de blocs interactifs, capteurs, portails, lignes de chunk, grille de construction, gizmo (`Material::depthTest = false` pour le gizmo, sinon il est mangé par la géométrie), surlignage de sélection (`Game::SetHighlight` — arêtes d'épaisseur constante), fantôme de pose.
- **Modes d'aperçu** : bloc isolé, entité isolée (avec son IA active sur un sol plat), structure isolée, biome isolé (4×4 chunks), dimension isolée, monde chargé complet, `Game::TravelTo` à chaud (prévenir du déchargement du monde courant).
- **Pause/Play/Step** du monde actif, **stats F3** : ms simulation, ms maillage, chunks en attente, img/s, `ui::UiFrameTimings` (styles/layout/viewport 3D/effets/dessin 2D), `Runtime::SimulationMs`, `Runtime::PortalMs`, threads simultanés max. Alimente le rapport.
- **Caméra de jeu** (mode Play d'une scène) : caméra subjective du joueur, ou caméra poursuite avec cap pris sur le **vecteur vitesse** (cf. piège documenté).

### 3. Atelier de textures — peinture 2D style Aseprite/GIMP

> Panneau demandé explicitement : « un panneau permet de faire du dessin 2D de texture ».

- Canevas matriciel **avec grille pixel**, jusqu'à 2048 px en **source** (conservée dans le projet, non chargée par `TextureAtlas::Build`) et 256 px en **rendu** (export + mipmaps). L'utilisateur **choisit** la résolution de rendu dans `TextureAtlas::Build` — le panneau affiche les deux.
- Outils (Aseprite-compatibles) : crayon, gomme, pipette, remplissage par zone (flood fill avec seuil alpha), ligne, rectangle, ellipse, déplacement, symétrie (H/V/HV), sélection rectangulaire, déplacement de sélection. Brosses de taille 1..N, opacité, mode de fusion (normal, multiply, screen, overlay), flou léger, netteté.
- **Calques multiples** (visibilité, opacité, mode de fusion, réorganisation, groupes), **palette** avec historique pipette, undo/redo par trait.
- **Aperçu live des mipmaps** : chaîne `Mipmaps()` jusqu'à 16→1 px dans l'ordre de Morton, comme `TextureAtlas::Build` la générera — voir immédiatement si un pixel disparaît à basse résolution.
- **Variantes** (`nom.1.png`, `nom.2.png`… regroupées sous `nom`) : ajouter / renommer / supprimer / prévisualiser sur un bloc témoin avec la sélection « position du bloc » qui détermine la variante (`BlockRender::variants`, `LookupVariants`). Génération procédurale possible (bruit de Perlin, dégradés, motifs) pour les variantes.
- **Variante « lueur »** (`TextureAtlas::Glow`) : génération/édition de la version haute-luminosité pour les blocs `glow` (étirement de la luminance entre 4ᵉ et 98ᵉ centiles, teinte conservée, gamma 0,75, premières images des bandes animées), avec **aperçu comparatif côte à côte** « éclairé par le ciel » vs « pleine luminosité » (`ChunkMesh::emissive`, matériau non éclairé).
- Import / export PNG, JPG (`sdl3::ImgSavePng`/`ImgSaveJpg` déjà présents).
- **Pipelines d'atlas** : bouton « Reconstruire l'atlas » qui appelle `TextureAtlas::Build(assets, content, tileSize, maxTileSize)` avec les paramètres en cours, affiche temps, occupation, liste des tuiles, et **relance le remaillage** des chunks touchés (`TouchSphere`).
- **Peinture sur modèle 3D** (bonus) : projection de la texture 2D sur les faces d'un modèle nodebox ou glTF — pipette sur le viewport 3D qui sélectionne le pixel correspondant sur le canevas. Nécessite le calcul UV par face (voir §5).

### 4. Éditeur de blocs — schema-aware, preview live

> Panneau demandé implicitement par « créer/modifier/supprimer/dupliquer des blocs ».

- Formulaire structuré pour **tous les champs** d'un `BlockDef`, exactement comme le prompt initial le liste : Identité, Graphismes (textures par face top/side/bottom/north/south/east/west, variantes, glow, bandes animées), Physique (solide/liquide/plein, `replaceable`, `drops`, `physics.gravityField`, `physics.sinking`), **Lumière** (`light` objet complet, avec héritage `light: N` / `graphics.light: N`, tous les champs `enabled, type, color, intensity, range, attenuation, direction, innerAngle, outerAngle, radius, offset`), **Optique** (`optics` {opaque, transmittance, fullCell} avec aperçu DDA sur scène témoin), **États** (`states`, `defaultState` — un **onglet par état**, id d'état `id_état` ou `"id"` ou `defaultState`), **Capteurs** (`sensor` {radius, near, far, sound} avec sphère visualisée et bloc « near »/« far »), **Interaction** (`interact.toggle`, `interact.action: sleep`), **Conteneur** (`container` {slots, title, columns, openSound, closeSound}), **Liquide** (`liquid` {range, tick, renewable, current, flowingTexture, mix}), **Multi-cellules** (`parts`, `partOf` — l'éditeur pose les parties dans le viewport avec `RotateOffset`), **Portail** (`portal` {dimension, delay}), **Inventaire / craft**.
- **Preview en direct** sur un bloc témoin dans le viewport (DIRECT), ré-éclairage si `light`/`optics` change, rechargement des chunks si le bloc est déjà placé. **Validation live** : `ContentRegistry::Validate` à la volée, avertissements en souligné ondulé.
- **Édition JSON brute** (onglet à côté du formulaire) : les deux vues **partagent le même document**.
- **Différence visible** : l'onglet « états fusionnés » montre le **JSON résultant** de `LoadBlockStates` pour chaque état (l'utilisateur voit le diff avec le `defaultState`).
- **Insertion dans le monde** : bouton « poser à la sélection », `Game::SetBlockState` + orientation, utilitaire multi-cellules (`Game::MainPart`, `RotateOffset`).
- **Workbench-style nodebox / formes 3D** — voir §5 (l'éditeur de blocs réutilise le même atelier de formes que l'éditeur d'entités).

### 5. Atelier de formes 3D & animation — style Blockbench/Workbench

> Panneau demandé : « créer comme sur Workbench les formes 3D des blocs et entités », « modélisation 3D et animation 3D », « arborescence de nœuds », « export .obj », « modifier le nodebox », « textures par face ».

- **Deux modes de forme** :
  - **Nodebox** (recommandé, compatible moteur) : arbre de boîtes/plans/cônes/cylindres/sphères, extrusions à partir d'une face, miroir, symétrie, échelle par axe, pivot, groupes hiérarchiques. Rotation par quaternion avec édition d'Euler (`math::FQuaternion::ToEuler`/`FromEuler`, ordre YXZ documenté). C'est **le** format que le moteur sait poser dans le monde (cf. `parts` / `RotateOffset`).
  - **Modèle externe** : import `.obj` (cf. `core/models/stairs/`), import `.glTF` via `data::gltf` (`render3d::ImportMesh(cache, chemin, ASYNC)`), export `.vxs.json` (structures) et `.obj` (Blender).
- **Arborescence de nœuds** : chaque nœud = un groupe ou une forme, avec transform (translation, rotation quaternion, échelle). Le panneau affiche l'arbre, permet renommage, duplication, suppression, glisser-déposer de reparentage, menu contextuel « Create Child Node ▸ Box / Cone / Cylinder / Sphere / Plane / Extrusion » (style Godot).
- **Aperçu PBR** dans le viewport (matériau réel : `Material::bakedLighting` ou non éclairé si `glow: true`, `Material::objectLight` pour les objets mobiles).
- **Textures par face** : onglet « UV » qui affiche le **dépliage UV** du nodebox (chaque face = rectangle texturé). L'utilisateur peut : (a) choisir une texture par face parmi celles du mod ; (b) peindre sur le dépliage (réutilise l'atelier §3) ; (c) choisir une variante ; (d) mapper un modèle glTF sur l'atlas du mod.
- **Timeline d'animation** (au-dessus de la vue 3D, style Blender/Unreal) :
  - Clip = `render3d::AnimationClip` (liste de keyframes).
  - Canaux : position, rotation (quaternion → Euler pour l'édition, aller-retour documenté), échelle, couleur, UV (bandes animées), luminance/opacité.
  - Courbes Bézier / linéaire / step **par canal**, éditeur de courbes (graphe) avec zoom/pan, aimantation, gestion des keyframes.
  - Scrubbing, lecture/pause, boucle, événements `on_event(t)`.
  - Aperçu dans le viewport avec **le même pipeline** que le jeu (`AnimationMixer::Update`).
  - NLA / couches (bonus) : superposition de clips, blend.
- **Composants d'entité** (même architecture que l'inspecteur `sandbox`) : `Visual` (structure, nodebox, modèle), `Light` (créatures lumineuses), `PhysicsBody` (`kind, mass, collider, restitution, friction`), `AudioSource`, `Container`, `Spawn`, `AI` (script + cibles/agressivité), `Health` (`lastCause`), `Camera`, `Trigger`.
- **Édition des créatures** : catalogue, dimensions du collider, animations `idle/walk/attack/death`, dégâts, PV, IA (script `mobs`), sons, dimensions d'apparition, `spawn` dans les biomes/dimensions.
- **Preview de comportement** : lancer l'entité seule sur un sol plat dans le viewport avec son script d'IA actif ; bouton « suivre » la caméra ; **inspection en direct** dans le panneau §10.

### 6. Éditeur de structures — API `Schematic` + `Formula`

- Import / export `.vxs.json`, `.we`, `.mts` (les trois formats natifs de `Schematic`), avec `ResolveNames` + alias `structures.aliases` du mod edit.
- Outils `Schematic` en UI : `Capture`, `Paste` (rotation, mirroring, fusion air, air seulement), `Bounds`, `Rotated`, `Mirrored`, `Hollow` (formes plates comprises), `Replace`, `Counts`. Presse-papiers nommés intégrés à une **bibliothèque** de structures du projet (`<projet>/structures/`).
- **Formula** : éditeur de formule avec coloration, aide-mémoire (`+ - * / % ^`, comparaisons, `and/or/not`, produits implicites, `if(c,a,b)`, `pi/e/tau`, fonctions usuelles), bouton « compiler » (`Formula::Compile`, erreur avec position), bouton « tracer » (`Plot(PlotSpec)` en 3D — condition → région pleine, expression → coquille, courbe/surface/solide paramétrés, convention math ou jeu, échelle, enveloppe, taille max, temps max).
- **Placement dans le monde** : glisser une structure dans le viewport ou « poser à la sélection » (`Game::PasteSchematic`), avec aperçu fantôme (`Game::SetHighlight`).
- **Opérations** : `FillRegion` (all, hollow, walls, outline, keep), `ReplaceRegion`, `SetHighlight`.
- **Arborescence de structure** (bonus) : les structures peuvent contenir des **blocs**, des **entités** (spawn) et des **références à d'autres structures** — l'éditeur affiche l'arbre et détecte les cycles.

### 7. Éditeur de biomes

- Formulaire structuré : identité, `dimension` (biome `"dimension"`), couches de terrain par profondeur (variantes `nom.N.png`), décorations (`giant_tree`, `spire`, `patch`, `cluster` avec `where: ceiling|floor|wall`, densité, taille min/max, bloc), liquides, générateur associé (`islands`, `caverns`, `maze`, `flat`) et ses paramètres.
- **Preview de biome** : génération d'un carré de **4×4 chunks** avec la graine courante, dans le viewport, ciel/lumière du biome appliqués (`DimensionDef.light`).
- **Édition de la carte de biomes** si le générateur expose une carte (`ChunkGenerator`) — point-à-point sur une carte 2D, même zone de bruit.
- **Paramètres procéduraux** : voir §8 (générateur maze).
- **Hot-reload partiel** : un biome modifié **ne régénère pas** les chunks existants (avertissement clair + option « régénérer dans une dimension temporaire »).

### 8. Éditeur de dimensions et générateurs

- Formulaire `DimensionDef` complet : `id`, `name`, `generator` (type + config), `sky`, `time`, `light {sky, ambient, base, exposure}`, `gravity` (`gravityDirection`), bornes d'apparition, `enterOnDeath`, portail de retour, `DimensionData` libre.
- **Générateur `maze`** : `levels` (liste de `Level`s ou `floors: {count, levels}` — répétition), `lightOff`, `lightHalf` + `halfChance`, `lightHanging`, `lightAuto` + `autoChance`, `exit.signLeft`/`signRight` + `signRange`/`signHeight`, `stairs {block, fill, chance}`, `elevator` (gaine 7×7, `cell`, `pit`, `overrun`), `elevators: [{id, floors: all|first|last|[de, à]}]`, `poolDepth`, sorties multiples, `biome` de chaque étage.
- **Graines par étage** (`seed + 7919k`) affichées et figées.
- **Paramètres du mod figés** dans le monde (`setting: id`, `fallback`, `Game::ResolveSettings`) — visibles, modifiables, avec avertissement si le monde a déjà généré.
- **Preview dédié** : carré de terrain de la dimension courante, bascule `Game::TravelTo` (décharge le monde courant — prévenir).
- **Portails** : création/édition à la volée, aperçu de l'effet (délai), destinations, test « traverser ».
- **Ascenseurs** : affichage des gaines (`world.maze_shaft(dim, id)`), paliers, moteurs. Formulaire des destinations (`elevators.destinations`, modes `elevator`/`surface` avec kiosque, `__back`).
- **Backrooms** : niveaux 0/2/4/5/37 = dimensions distinctes ; chaîne 0→2→4→5→37→surface. L'IDE doit **dessiner la chaîne** et la tester (scénario dédié).

### 9. Éditeur de scripts et de JSON

- **Éditeur de code** (réutiliser le module de `sandbox` + son lexer) : coloration (`Scene`, `Behaviour`, `Node3D`, `Mesh3D`, `Node2D`, `PhysicsBody`, `Light3D`, `Gameplay`, `SceneAsset`, `ObjectAsset`, `Block`, `State`, `LightSource`, `Sensor`, `Container`, `Portal`, `Fluid`, `WorldEditor`, `StructureAsset`, `FormulaAsset` ; `import` ; API `world.*`, `structure.*`, `formula.*`, `entity.*`, `game.*`, `node.*`, `audio.*`, `editor.*`, `studio.*`, `camera.*`).
- **Complétion** sur les API et sur les blocs/entités/dimensions connus (`ContentRegistry`). **Go-to-definition** entre scripts, vers les JSON de blocs, vers les dimensions, vers les structures.
- **Diagnostic à la frappe** : `Formula::Compile` pour les formules, validation JSON schema-aware pour les blocs/entités/biomes/dimensions (via `ContentRegistry::Validate` + schémas), analyse syntaxique du langage de script (`Runtime::OutlineScript` de `sandbox` — sans exécuter, comme le recommande `project_sandbox_scripts.md`).
- **Preview immédiate** : un JSON de bloc modifié met à jour le bloc témoin et, si hot-reload autorisé, le monde. Un script modifié peut être rechargé, son état d'exécution repris.
- **Éditeur JSON schema-aware** : navigation arborescente (fold/unfold), copier-coller, formatage, tri des clés, suggestion des clés autorisées à chaque niveau (types et bornes : 0..255 couleurs, angles, distance max 32, etc.), **saut vers la valeur référencée** (`"block": "mod:name"` clique et ouvre la définition). Bouton « appliquer à chaud ».
- **Onglet `defaultState` / états fusionnés** : diff entre l'état par défaut et les surcharges, sur le JSON résultant de `LoadBlockStates`.
- **Scripts de mod** (`mods/core/scripts/…`) : open/save/reload, association aux états de blocs (`sensor.near/far`), déclenchement par `interact.toggle`, exécution `on_start`/`on_update`/`on_trigger` si `Behaviour`.

### 10. World Inspector — outliner temps réel + inspecteur d'entités

> Panneau demandé : « lister en temps réel les entités présentes dans le monde ouvert et visualiser leurs propriétés (position, orientation, taille, propriété physique, visuelle, IA, entité faim/soif/poison…) ».

- **Liste dynamique** de toutes les entités et blocs interactifs chargés dans les chunks actifs, **groupés par dimension** puis par **chunk**, puis par **type** (créature, bloc interactif, conteneur, portail, structure mobile, entité lumineuse, plateforme mobile `Carrier`…). Rafraîchissement **5 Hz** en lecture (`Game::World` + ECS), 60 Hz pour la sélection courante.
- **Filtres** : par type, par nom, par état (survie/AI), par texte libre, par distance à la caméra, par dimension.
- **Inspection en direct** :
  - **Transform** : position monde, orientation géographique (yaw/pitch/roll), orientation de bloc, **boîtes de collision**.
  - **Visuel** : variante de texture courante (`LookupVariants`), phase d'animation, couleur, glow, bande animée en cours.
  - **Survie** : jauge d'air, suffocation (`Survival::suffocating`), `Health::lastCause`, faim, soif, poison, `trapped`, `PhysicsBody::inFluid`, sables mouvants (`physics.sinking`), **état dans le liquide courant** (`PhysicsBody::current`).
  - **Décisions d'IA** : cible courante, agressivité, comportement (`entity.target`), état de la machine à états du script `mobs`.
  - **Composant `Carrier`** si l'entité est debout sur une plateforme mobile.
  - **Rangement** si `container` (`Game::BlockInventory`) — ouvrir l'écran de rangement comme en jeu (réutiliser `ui::` + `Game::BlockInventory`).
  - **Portail** : destination, délai restant.
  - **Script** : classes vivantes portées par l'entité (`Behaviour` attachée, `Mesh3D` créé par un script), champs exposés, bouton « Détruire » (séquence RAII complète).
- **Navigation** : double-clic téléporte la caméra à l'entité ; bouton « suivre » (caméra suit la cible) ; bouton « inspecter ses chunks ».
- **Édition à la volée** :
  - Changer l'état d'un bloc via `Game::SetBlockState` (ex. forcer `fluorescent auto_on`).
  - Forcer la variante de texture d'un bloc (`LookupVariants` / `BlockRender::variants`).
  - Déplacer, tourner, ré-échelonner une entité.
  - Modifier PV / faim / air / soif / poison (debug).
  - Forcer une décision d'IA (cible, agressivité) via `entity.target`.
  - Déclencher un capteur, ouvrir/fermer un conteneur, allumer/éteindre une lampe — tout ce que ferait un clic droit (`world.use(x,y,z)`, `interact.toggle`).
- **Multi-fenêtre** : ce panneau peut vivre dans la fenêtre d'édition **ou** dans une fenêtre séparée (Phase 0) — la fenêtre de jeu montre le monde, la fenêtre d'inspection liste les entités.

### 11. Undo / Redo (transverse)

- Reprendre l'approche `sandbox` : instantanés de **document** (bloc, entité, structure, biome, dimension, script, texture, animation clip), pas de commandes inverses à écrire.
- Un glissé = **une** étape (`BeginEdit`/`EndEdit`), 64 étapes, la plus ancienne oubliée au-delà.
- **Le mode Play n'entre pas dans l'historique** — la physique écrit dans le document à chaque image, `Stop` restaure l'instantané pris au démarrage.
- Déclencheurs : modification de formulaire, application de JSON, déplacement gizmo, pose de structure, renommage, peinture sur texture (par trait), édition de keyframe.

### 12. Console de commande — code d'état

> Panneau demandé : « les commandes retournent un code, en fonction de ce code, le jeu doit laisser la console ouverte ou la fermer ».

- Amélioration de la console existante : chaque commande retourne `CommandResult { status, message, confirmationPrompt }` avec `status ∈ { Ok, Error, Warning, ConfirmRequired, Pending }`.
- Comportement par code :
  - **Ok** → la console **se ferme** (auto-close par défaut, réglable).
  - **Error** → console **ouverte**, message en rouge, curseur sur la ligne.
  - **Warning** → console **ouverte 5 s**, puis auto-close.
  - **ConfirmRequired** → console **ouverte** avec prompt `[y/N]` ou boutons selon le type (`Delete`, `Overwrite`, `Discard`).
  - **Pending** → console reste ouverte tant que la commande asynchrone (chargement, génération) n'est pas terminée.
- **Réutiliser `EditorUi` / `Runtime::Execute`** existant, avec `CommandResult`.
- Historique (Ctrl+↑/↓), complétion (noms de blocs, entités, dimensions, structures, scripts, commandes `world.*`, `structure.*`, `formula.*`, `entity.*`, `game.*`, `editor.*`, `studio.*`, `camera.*`, `audio.*`), coloration du résultat.
- Filtres de log (info / warning / error / script), marque-pages.
- **Commandes de hot-reload** : `reload mod <nom>`, `reload all`, `reload texture <nom>`, `reload block <id>`, `reload script <chemin>`, `reload dimension <id>` (avec confirmation), `reload biome <id>` (avertissement).
- **Commande `/reload`** disponible en jeu (mode Play) — recharge le mod actif sans quitter le monde.

### 13. Navigateur de ressources (Asset Browser)

- Réutiliser `AssetBrowserModel` de `sandbox` (navigation pure, testée sans fenêtre).
- Racines : projet mod, `structures/`, `textures/`, `models/`, `sounds/`, `scripts/`, `data/`, dossiers de sauvegardes, `assets/` (lecture seule).
- **Vignettes réelles** : textures avec aperçu, modèles glTF rendus dans un petit `Viewport3D` à la demande, structures prévisualisées en 3D miniature, scripts avec état de compilation (`Runtime::OutlineScript`).
- Opérations fichier : nouveau dossier, renommer, dupliquer, supprimer, déplacer (glisser), couper/coller, sélection multiple (Ctrl/Maj), pilotage script (`editor.ui("select_assets", …)`, `asset_new_folder`, `asset_rename`, `asset_duplicate`, `asset_delete`, `asset_move`).
- Séparation typée `ProjectFs` du `sandbox` (cf. `project_sandbox_fs.md`), avec les types supplémentaires `BlockRef`, `EntityRef`, `StructureRef`, `BiomeRef`, `DimensionRef`, `TextureRef`, `ModelRef`.

### 14. Mod Manager — onglet dédié (cf. §1)

- Dépendances et ordre, activation/désactivation, diagnostic, fichiers fournis par chaque mod.
- **Hot-reload global** : Ctrl+Shift+R recharge le mod actif ou tous, avec avertissement si le monde est en cours (les dimensions/biomes ne se régénèrent pas, seuls les blocs/entités/scripts/textures sont rechargés).

### 15. Panneau de prévisualisation d'éclairage

- Vue focalisée sur la lumière d'un bloc ou d'une entité : rendu du cône/direction/faisceau, sphère de portée, `Transmittance` DDA sur grille témoin.
- Aperçu **comparatif** des variantes `light` (objet / niveau historique / désactivé).
- Overlay « éclairement du monde » : `MeshVertex::light` (rgb = direct, a = ciel × occlusion ambiante) sur les surfaces, pour diagnostiquer les « carrés sombres » (bug documenté dans `project_voxelia_lighting.md`).
- Sélection d'une source dans le viewport → édition dans l'inspecteur.

---

## Fonctionnalités transverses

### Hot-reload (obligatoire, priorité haute)

Documenté dans `memory/voxelia_studio_hot_reload.md` (ce qui se recharge, ce qui ne peut pas, pourquoi) :

- **Scripts** : rechargement de l'interpréteur, relance de `on_start`, reprise des nœuds si possible, sinon redémarrage de la scène de test avec l'état courant du monde conservé (`world.data`). Désactivation du script fautif en cas d'erreur (comme en jeu).
- **Textures / atlas** : rechargement ciblé `TextureAtlas::Build` + `ReleaseUnused`, remaillage des chunks touchés, prévisualisation dans le viewport.
- **JSON de blocs / états** : `ContentRegistry::LoadBlockStates` sur le sous-arbre concerné, recalcul de la lumière et de l'optique, remaillage des chunks concernés (`TouchSphere`).
- **Dimensions / biomes** : hot-reload **partiel** (les chunks générés ne sont pas régénérés), avertissement clair ; option « régénérer à partir de la graine » dans une dimension temporaire.
- **Sons** : rechargement du `ResourceCache` (les ressources déjà chargées restent — un son modifié n'est pas relu tant qu'il est en mémoire, cf. `project_sandbox_audio.md`).
- **Bouton « Recharger le mod »** dans le Mod Manager et raccourci Ctrl+Shift+R.
- **En jeu** (mode Play) : commande `/reload` + `--hot-reload` en CLI pour recharger au démarrage.

### Undo / Redo (transverse) — cf. §11

### Système de fichiers projet (typed paths)

- `ProjectPath`, `SceneRef`, `ObjectRef`, `ScriptFileRef`, `AssetRef`, `NodeRef` de `sandbox`, plus `BlockRef`, `EntityRef`, `StructureRef`, `BiomeRef`, `DimensionRef`, `TextureRef`, `ModelRef`, `SoundRef`.
- Document ouvert + `ActiveDocument()` ; ne pas confondre bloc / entité / structure / biome / dimension / nœud / ressource / script.
- Vocabulaire : `block.*`, `entity.*`, `structure.*`, `biome.*`, `dimension.*`, `node.*`, `script.*`, `texture.*`, `model.*`, `sound.*` (garder l'alias `object.*` pour la compat, déprécié).

### Intégration du langage de script

- Réutiliser le langage de `sandbox` (classes dérivées des bases C++ `Scene`, `Behaviour`, `Node3D`, `Mesh3D`, `Node2D`, `PhysicsBody`, `Light3D`, `Gameplay`, `SceneAsset`, `ObjectAsset`), et **ajouter les bases** nécessaires à Voxelia :
  - `Block`, `State`, `LightSource`, `Sensor`, `Container`, `Portal`, `Fluid` pour éditer les blocs depuis un script.
  - `WorldEditor` (`world.set_block`, `world.fill`, `world.replace`, `world.count`, `world.spawn_structure`, `world.structure_set`, `world.state`, `world.set_state`, `world.container`, `world.container_add`, `world.toggle`, `world.use`, `world.load(boîte)`, `world.maze_shaft(dim, id)`).
  - `StructureAsset`, `FormulaAsset`.
  - Exposer **la totalité** des fonctions de `project_voxelia_edit.md` (`structure.*`, `formula.check`, `formula.eval`, `entity.target`).
- Les fonctions d'un mod s'exécutent avec **ce mod** pour contexte (`voxel.setting` — déjà en place).
- **API `studio.*`** : `studio.select_block(id)`, `studio.select_entity(id)`, `studio.preview_biome(id)`, `studio.run_scenario(name)`, `studio.reload_mod(name)`, `studio.screenshot(path)`, `studio.ui(command, arg)`.

### Saisie clavier et routage — multi-fenêtre

- Reprendre `HandleEvent` mutable avec **consommation** d'évènements, plus de filtre `TextFieldHasFocus()` global — chaque `WindowContext` a le sien.
- Raccourcis configurables : Ctrl+S, Ctrl+Shift+S, Ctrl+Z/Y, W/E/R (manipulateur), X (magnétisme), F (focus), F3 (stats), F9 (mode Play), Ctrl+Shift+R (recharger le mod), Tab (mode), Suppr, F2 (renommer), Ctrl+D (dupliquer), Ctrl+X/V/A.
- Échappe : sort du mode Play, ferme un dialogue, désélectionne — **ne quitte pas** l'application (quitter = fermeture de fenêtre, menu Fichier › Quitter, Ctrl+Q).

### Rapport d'exécution

- Réutiliser `report.hpp` de `sandbox` : scènes, objets, blocs, entités, structures, biomes, dimensions, **threads simultanés max**, cadence min/moy/centiles, scripts exécutés, compteurs de jeu (blocs placés, entités spawnées, portails traversés, dimensions visitées, formules tracées, structures capturées), captures écrites, **fonctionnalités visitées et leur statut**, avertissements/erreurs.
- Format **texte** et **JSON**.
- Capture `--screenshot=N:PATH` et `--screenshot-desktop=N:PATH` (bureau entier pour la vérif multi-fenêtre) à des frames clés.

---

## Architecture et livrables attendus

Livre le **code C++23 complet** (headers + implémentations), sous `demos/voxelia_studio/` (ou équivalent — laisse la place à un module séparé si l'architecture existante le permet) :

| Fichier | Rôle |
|---|---|
| `cli.hpp` | options de ligne de commande |
| `project/` | document (blocs, entités, structures, biomes, dimensions, scripts, textures, sons), ⇄ JSON, `ProjectFs` typée |
| `content/` | chargement, hot-reload, `ContentRegistry::Validate`, `TextureAtlas` |
| `runtime/` | document → monde vivant, hot-reload, `Game` branché, scripts hôtes |
| `ui/kit.hpp` | palette, apparence par type, colorations (script/JSON/log/formule), `DockPanel` |
| `ui/scene_tree.hpp` | arbre des blocs/entités/structures/biomes/dimensions, menu contextuel, renommage, drag-drop reparentage |
| `ui/inspector.hpp` | inspecteur à sections par composant, éditeur JSON, formulaire générique |
| `ui/texture_editor.hpp` | canevas, outils, mipmaps, variantes, glow |
| `ui/block_editor.hpp` | formulaire complet d'un bloc, états, preview live |
| `ui/entity_editor.hpp` | modélisation nodebox, timeline d'animation, graphe de courbes |
| `ui/structure_editor.hpp` | `Schematic` + `Formula`, formats `.vxs.json`/`.we`/`.mts` |
| `ui/biome_editor.hpp` | formulaire, carte de biomes, génération de preview |
| `ui/dimension_editor.hpp` | formulaire `DimensionDef`, maze, générateurs, portails, ascenseurs |
| `ui/world_inspector.hpp` | outliner + inspecteur d'entités en direct |
| `ui/console.hpp` | console avec code d'état |
| `ui/assets.hpp` | navigateur de ressources |
| `ui/documents.hpp` | zone centrale à onglets |
| `ui/panels.hpp` | assemblage (menus, docks, toolbar, status bar, thème) |
| `panels/preview.hpp` | viewport 3D DIRECT, gizmo, sélection, caméra |
| `scenarios.hpp` | scénarios scriptés (test, screenshots) |
| `report.hpp` | rapport texte/JSON |
| `app.hpp` | boucle, captures, mode headless, multi-fenêtre |
| `windows/` | `WindowManager` dérivé de la Phase 0 (`ui::WindowContext`, `StudioWindow`, `GameWindow`, `InspectorWindow`) |

**Plus les ajouts nécessaires à `libs/`** (à documenter au format `memory/*.md` comme dans `project_sandbox_app.md` et `project_voxelia_*.md`) :
- `ui::WindowManager`, `ui::WindowContext`, `ui::WindowSpec`, `ui::WindowId`, extensions CSS `@media (window-id …)` ;
- `render3d::Mesh::Box(w,h,d)` (déjà là dans `sandbox`), `Mesh::LocalBounds()`, `Material::depthTest`, `Camera::ScreenPointToRay`, `WorldToScreen`, `math::FRay::DistanceTo`, `math::FQuaternion::ToEuler` (déjà là) ;
- `Schematic::GetCell(x,y,z)` public si manquant ;
- `ContentRegistry::UnloadBlock` sélectif pour le hot-reload ;
- `render3d::Material::wireframe` pour les overlays de collision ;
- `TextureAtlas::Build` avec paramètre `maxTileSize` (déjà là) ;
- **chaque ajout vient avec ses tests**.

---

## Vérification

- **Tests unitaires sans fenêtre** pour chaque sous-système : chargement/sauvegarde JSON (round-trip exact, y compris Float vs Int), éditeur de blocs (états fusionnés, validate), éditeur de structures (quatre formats, transformations, formules, presse-papiers), éditeur de textures (mipmaps, variantes, glow), éditeur de biomes (paramètres → `ChunkGenerator`), éditeur de dimensions (paramètres → `DimensionDef`, graines figées), World Outliner (extraction des entités/blocs interactifs), console (codes d'état), hot-reload (texture modifiée → atlas reconstruit, chunk remaillé), **Phase 0** (multi-fenêtre : création/fermeture N contextes, routage évènements, CSS isolé).
- **Scénarios d'intégration** sous `xvfb-run` + ASan/UBSan avec captures :
  - `windows` : deux fenêtres SDL3 côte à côte, jeu + édition, captures desktop ;
  - `blocks` : créer un bloc, ajouter des `states`, changer sa lumière, vérifier `MeshVertex::light` en preview ;
  - `textures` : peindre, générer un variant, générer le glow, reconstruire l'atlas, poser le bloc dans le viewport ;
  - `entities` : créer un nodebox, une animation, un comportement, lancer le playtest, vérifier la mort ;
  - `structures` : capturer une région du monde généré, appliquer une formule, exporter en `.we` et `.mts`, réimporter dans une autre dimension ;
  - `biomes` : changer un biome, régénérer un carré de 4×4 chunks, vérifier les couches ;
  - `dimensions` : créer une dimension, un portail, l'ascenseur, visiter ; chaîne backrooms 0→2→4→5→37→surface ;
  - `world_inspector` : charger un monde, filtrer les entités, forcer un état de capteur, vérifier la bascule, éditer une décision d'IA, ouvrir un conteneur ;
  - `hot_reload` : éditer un JSON de bloc **pendant** que le monde tourne, constater la mise à jour visuelle/lumière sans rechargement ; `/reload` en console ;
  - `console` : quatre codes d'état testés en séquence (Ok/Warning/Error/ConfirmRequired) avec captures avant/après fermeture.
- **Cas d'erreur testés** : JSON invalide (type attendu vs fourni), `interact.toggle` vers un état inexistant, `structure.save` avec `/` dans le nom, formule avec parenthèse non fermée, portail vers une dimension inexistante, cycle d'instances, `TextureAtlas::Build` sur un fichier corrompu, `light.innerAngle >= outerAngle`, `sensor.radius < 0`, `dimension` cyclique, `glTF` accesseur hors tampon.
- **Critère d'acceptation** : tous les scénarios **rc=0**, **0 avertissement inattendu** en stderr, **tests unitaires verts**, **aucune régression** sur les suites existantes (ui 104, sandbox 156, script_bindings 11, émulateur 22, gltf 7, game_editor 53 + 40 + 132 + 10 + 7, voxel/voxelia toutes), et un **rapport d'exécution** qui liste chaque fonctionnalité visitée par le scénario et son statut.

---

## Livrables documentaires

Dans `memory/`, au format markdown comme les autres projets :

- `voxelia_studio_architecture.md` (modules, dépendances, séparation document/runtime, décisions structurantes, **multi-fenêtre**).
- `voxelia_studio_panels.md` (un fichier par panneau : rôle, entrées, sorties, raccourcis, API script).
- `voxelia_studio_workbench.md` (nodebox, UV, textures par face, timeline, graphe de courbes, export `.obj` / `.glTF`).
- `voxelia_studio_lib_additions.md` (bugs trouvés dans `libs/` et corrigés, fonctions ajoutées, **ruptures de compilation préexistantes réparées**, sur le modèle de `project_sandbox_app.md` — c'est la **mémoire** des pièges déjà payés : `ui::StyleSystem::ResolveEntity` use-after-free, `ui::Viewport3DSystem` RGBA/BGRA, `sdl3::keyboard::IsPressed` null, `Option<T>::OkOr`, `render3d/texture_loader.hpp` `SDL_PIXELFORMAT_RGBA32`, `tray_entry::*`, `SDL_Vertex` brut, `FAABB::Expand`, `Mesh::Box` manquant, `FQuaternion::ToEuler` manquant, `data::gltf` absent, `WindowManager` absent, etc.).
- `voxelia_studio_hot_reload.md` (ce qui se recharge, ce qui ne peut pas, et pourquoi : dimensions/biomes, sons, ressources glTF déjà chargées).
- `voxelia_studio_verification.md` (matrice fonctionnalité → test → scénario).
- `voxelia_studio_threads.md` (comment l'IDE préserve `voxel-update` + `voxel-render-N` + DIRECT, ce qu'il n'accélère pas, mesures).

---

## Ordre d'implémentation suggéré

**Étape 0 — Phase 0 UI multi-fenêtres** (bloquant pour tout le reste) : `ui::WindowManager`, `WindowContext`, `WindowSpec`, routage évènements, CSS isolé par fenêtre, tests sans GPU, exemple Xvfb deux fenêtres. Aucune ligne de Voxelia Studio tant que la Phase 0 ne compile pas et que les suites existantes ne passent pas.

**Étape 1 — Socle Studio** : arborescence projet, `ProjectFs` typée (Bloc/Entité/Structure/Biome/Dimension/Texture/Son), CLI, headless, rapport, dock principal, viewport DIRECT, **fenêtre de jeu détachée** (via Phase 0).

**Étape 2 — Éditeur de blocs** (formulaire + JSON schema-aware + validate + preview + hot-reload basique) : c'est le cœur — tout le reste s'y rattache.

**Étape 3 — Atelier de formes 3D + animation** (nodebox, UV, textures par face, timeline, graphe de courbes, export `.obj`) : partagé avec l'éditeur d'entités.

**Étape 4 — Éditeur de structures** (`Schematic` + `Formula`) : déjà très outillé côté moteur, mise en UI.

**Étape 5 — Atelier de textures** (canevas + variantes + glow + rebuild atlas + peinture sur modèle 3D).

**Étape 6 — Éditeur d'entités** (composants, créatures, IA, preview de comportement) — réutilise massivement `sandbox` et l'atelier de formes.

**Étape 7 — Éditeur de biomes** puis **éditeur de dimensions** (maze + ascenseurs + backrooms chain).

**Étape 8 — World Inspector** (outliner + inspecteur d'entités en direct, §10) et **console code d'état** (§12).

**Étape 9 — Mod Manager** + diagnostic + hot-reload complet (scripts + textures + JSON + recalcul lumière), scénario dédié.

**Étape 10 — Scénarios d'intégration** et captures ; documentation.

Chaque étape est livrée avec ses tests unitaires verts et ses scénarios rc=0. Une étape qui révèle un manque dans `libs/` **l'ajoute** (avec tests) avant de poursuivre — c'est le mode de fonctionnement qui a fait ses preuves sur `sandbox`.

Pour la compilation et les tests utilise les commandes suivantes :
```sh
cpp-maker build -m voxelia --sanitizer all --output sortie.txt
cpp-maker build release -m voxelia --output sortie.txt
cpp-maker test -m voxelia --sanitizer all --output sortie.txt
cpp-maker test release -m voxelia --output sortie.txt
cpp-maker run -m voxelia
cpp-maker help # pour en savoir plus
```

Si l'execution ne fonctionne pas utilise ces commandes avant cpp-maker:
```sh
env -u GTK_PATH -u GTK_MODULES cpp-maker ...
```