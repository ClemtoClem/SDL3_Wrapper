---
name: project-game-editor-app
description: "examples/game_editor — éditeur de niveau 3D complet (menus/docks à onglets/inspecteur/console/profileur, 3 scènes dont un circuit de voiture jouable, sélection au clic + manipulateur déplacer/tourner/redimensionner avec magnétisme, annuler/rétablir, scénarios scriptés, captures d'écran, rapport d'exécution) et les bugs et manques de la bibliothèque qu'il a fait sortir (6 bugs corrigés, 8 ajouts, 4 ruptures de compilation préexistantes réparées)."
metadata:
  type: project
---

prompt:
```
Ta mission est d'implémenter entièrement un éditeur de niveau de jeux vidéos 3D en ce basant seulement sur le wrapper C++23 en cours de développement ( @examples/game_editor_demo.cpp  ). Le développement de cette démo permettra d'identifier les points faibles de la librairie qui nécessiterons un développement ou un raffinement (comme le module ui pour l'interface dynamique, le module generator pour générer des structure, le module audio pour le son, sfx et autre, le module math pour les forme géométrique et fonctions mathématique, le module render3D d'affichage 3D ou le module physics de calcul de la physique, le module resources pour gérer les ressources dans un pool, le module sql pour gérer une base de données, le module ecs) afin de cette démo fonctionne correctement.
Le résultat attendu est une démo moderne, maintenable, testée en s'inspirant des application actuelle telle que blender, godot ou unrealengine. L'ui de la démo devra être fluide et complète avec des panneaux, des onglets, des bar d'outils pour se rapprocher le plus possible d'une interface professionnelle.
Pour que la démonstration soit testable implémente des options en ligne de commandes afin de manipuler l'application en ligne de commandes afin de lui demander de générer un rapport d’exécution (listes des scènes, listes de objets, nb de thread max activés en même temps, fps min mesuré, fps max mesuré, script en exécutions....) et lui demander de prendre des screenshoot pour observer l'évolution de l’application (ouverture d'un panneau, changement de thème, animation dans la scène 3D, ajout d'un objet, configuration des propriété visuelle des objets, configuration des propriété physique des objets, test de la scène pour vérifier sa jouabilité (exemple petit circuit de voiture) et tester les performance de l'application).
L'implémentation nécessite une analyse fine de la libraire existante, car l'implémentation et l'amélioration de la librairie s’effectuera en parallèle de l'implémentation de l'application de démonstration.
Décris dans le répertoire memory au format markdown les améliorations effectuées dans la librairie et décrit les fonctionnalités développées pour la démo.
Développe au sein de la démo un langage de programmation interprété pour programmer les animations et autre pour créer un exemple jouable (circuit de voiture). Pour cela utilise le module data pour les format json (gltf) des textures, et étends ces fonctionnalité pour permettre de faire de l'analyse syntaxique de language scripté et leur exécution (like lua ou python).
```

Réécriture complète de la démo d'éditeur de niveau (2026-09-12), à la demande
explicite de l'utilisateur : « une démo moderne, maintenable, testée »
inspirée de Blender/Godot/Unreal, pilotable en ligne de commande, capable de
produire un rapport d'exécution et des captures d'écran, avec un langage de
script interprété pour les animations et un exemple jouable (circuit de
voiture). Remplace le capstone minimal décrit dans [[project-game-editor]]
(`editor.hpp`, 887 lignes, supprimé).

## Architecture — 8 fichiers, une responsabilité chacun

| Fichier | Rôle | Dépendances |
|---|---|---|
| `cli.hpp` | options de ligne de commande | `core::` seulement |
| `project.hpp` | **document** : projet → scènes → objets, ⇄ JSON | `core`, `data`, `math` |
| `content.hpp` | les 3 scènes livrées + leurs scripts de gameplay | `project.hpp` |
| `runtime.hpp` | document → scène vivante + API hôte des scripts | `ecs`, `render3d`, `physics`, `data::script` |
| `panels.hpp` | l'interface complète | `ui::` |
| `scenarios.hpp` | 6 scénarios scriptés qui pilotent l'éditeur | — |
| `report.hpp` | rapport d'exécution (texte / JSON) | `data::json` |
| `app.hpp` | fenêtre, boucle d'images, captures, mode sans écran | tout |

### Les deux décisions structurantes

1. **Document ≠ runtime.** `project.hpp` ne dépend NI de `render3d::`, NI de
   `physics::`, NI de `ui::`. Toute mutation passe par une commande de
   `Runtime` qui écrit d'ABORD dans le document PUIS répercute sur la scène
   vivante. Conséquences : sauvegarder = sérialiser le document ; l'interface,
   les scripts et le chargement empruntent le même chemin donc se comportent
   pareil ; et tout est testable sans fenêtre.
2. **`Runtime` ne tient qu'un `render3d::Canvas*` FACULTATIF.** Sans lui,
   scène, physique, scripts et rapport fonctionnent à l'identique (seul le
   rendu des portails disparaît). C'est ce qui rend possibles à la fois
   `--headless` et `tests/game_editor_smoke_test.cpp` (37 tests sans GPU).

Le seul flux « à l'envers » est `MirrorSimulationToDocument` (physique et
animation recopiées dans le document à chaque image pendant le mode Jeu) ;
Stop restaure l'instantané pris au démarrage, donc jouer ne modifie jamais le
projet enregistré — y compris les objets créés par script pendant la partie.

## Ce que la démo fait

- **Interface** : barre de menus (7 menus), barre d'outils à icônes Material,
  dock gauche (Outliner / Scènes / Ressources), viewport 3D, dock bas
  (Console / éditeur de Script exécutable / Profileur avec graphe de cadence),
  dock droit (Transform / Matériau / Physique / Monde), barre d'état. Trois
  thèmes commutables à chaud.
- **Trois scènes** : *Vitrine* (matériaux, paire de portails, animations),
  *Circuit* (piste fermée de 88 objets, jouable), *Laboratoire physique*
  (empilement + largage scripté).
- **Ligne de commande** : `--scenario`, `--script`, `--frames`, `--report`
  (`text`/`json`), `--screenshot=N:CHEMIN`, `--headless`, `--theme`,
  `--scene`, `--seed`, `--list-scenarios`, `--list-scenes`…
- **Rapport** : scènes, objets (nom/forme/corps/position/matériau), fils
  d'exécution simultanés max, cadence min/max/moyenne + centiles, scripts
  exécutés, compteurs de jeu (tours bouclés, meilleur tour), sessions de jeu,
  captures écrites, avertissements/erreurs.

## Le circuit de voiture — ce qui l'a rendu jouable

Le C++ pose la GÉOMÉTRIE, le script pose le COMPORTEMENT. Trois pièges réels :

1. **Orientation tangentielle.** Une rotation de `θ` autour de Y envoie l'axe
   local +Z sur `(sin θ, 0, cos θ)` — la direction RADIALE, pas la tangente.
   Les rails (longs sur leur axe Z) dépassaient donc vers le centre de la
   piste, et la voiture démarrait DANS un rail : catapultée à la première
   image. Correction : `yaw = θ + 90°`.
2. **Nombre de points de passage.** Le pilote automatique vise le point
   suivant en ligne droite ; l'écart corde/arc vaut `R·(1−cos(π/N))`, soit
   8,8 unités pour 4 points (hors d'une piste large de 9) contre 2,3 pour 8.
   Passé de 4 à 8.
3. **Frottement.** Le script impose la vitesse image par image ; le solveur
   applique une impulsion de frottement bornée par `µ·impulsion normale`.
   Avec 900 kg et `µ=0,9`, la voiture était clouée au sol. `µ=0,05` modélise
   des roues qui roulent — ce qui est la situation.

Le **pilote automatique** n'est pas un gadget : c'est ce qui rend la scène
jouable SANS CLAVIER, donc vérifiable en `--headless` et reproductible. Dès
qu'une touche est pressée, le joueur reprend la main. Vérifié : 3 tours en
24,7 s simulées, meilleur tour 7,07 s, zéro erreur de script.

## Bugs et manques de la bibliothèque trouvés en construisant ça

**Corrigés dans `lib/` :**

1. **`ui::StyleSystem::ResolveEntity` — use-after-free** (plantage
   reproductible). Le style résolu du parent était passé aux enfants par
   POINTEUR vers son composant ECS stocké ; chaque appel récursif fait un
   `GetOrAddComponent<UiComputedStyle>` qui peut faire migrer l'enfant
   d'archétype et RÉALLOUER le vecteur où vit celui du parent. La liste
   d'enfants était de surcroît parcourue directement dans son composant.
   Correction : passer `&result` (copie locale sur la pile) et copier la liste
   d'enfants avant d'itérer.
2. **`ui::Viewport3DSystem` — rouge et bleu inversés dans TOUT viewport 3D.**
   La texture d'affichage était créée au format par défaut `RGBA8888`, un
   format PACKED dont l'ordre mémoire en little-endian est `[A,B,G,R]`, alors
   que les pixels viennent du GPU en `R8G8B8A8_UNORM`, soit l'ordre littéral
   `[R,G,B,A]`. Correction : `PixelFormat::RGBA32`. Le piège était déjà
   documenté en long sur `Renderer::ReadPixels` — un commentaire du fichier
   affirmait l'équivalence inverse.
3. **`ui::` — fond noir OPAQUE codé en dur** pour `UiSelectable`, `UiTreeNode`
   et `UiMenuBarItem` au repos (`FColor::BLACK()` au lieu du fond résolu) :
   invisible en thème sombre, barre de menus et outliner illisibles en thème
   clair. Correction : fond RÉSOLU, transparent par défaut.
4. **`data::JsonDocument` — le type FLOAT se perdait à l'aller-retour.** Un
   `5.0` s'écrivait `5` et se relisait en nœud INT ; tout lecteur consultant
   `floatValue` récupérait 0. Contourné côté LECTURE seulement en 2026-08 (cf.
   [[project-game-editor]]) ; corrigé à la source : point décimal forcé.
5. **`data::YamlDocument` — un scalaire cité suivi d'un commentaire gardait
   ses guillemets** (`"Hello: World"  # note`). Le commentaire était retiré
   APRÈS l'examen des guillemets. Faisait échouer
   `data_smoke_test_5::Yaml::QuotedStringsAndComments` depuis un moment.
6. **`sdl3::keyboard::IsPressed` déréférençait un pointeur nul** quand le
   sous-système d'évènements n'est pas initialisé — exactement le cas du mode
   sans écran.

**Ajouts à `lib/` :**

7. `sdl3::ImgSavePng` / `ImgSaveJpg` — le wrapper savait charger une image
   mais pas en écrire une. Indispensable à toute capture d'écran.
8. `math::FQuaternion::ToEuler` — inverse exact de `FromEuler` (ordre YXZ),
   blocage de cardan résolu par convention documentée. Sans lui, un
   inspecteur ne peut afficher la rotation d'un objet tourné par autre chose
   que lui-même (script, physique, animation) : l'astuce `EditorEuler` de la
   version précédente désynchronisait dès qu'un script tournait un objet.
9. `render3d::Mesh::Box(w, h, d)` — seul `Cube(size)` uniforme existait, ce
   qui obligeait à compenser par une échelle non uniforme sur le nœud,
   écrasant le champ `scale` que l'utilisateur édite.
10. `ui::UiFactory::SetTheme` / `ui::Ui::SetTheme` /
    `UiStyleSheet::DirtyAllUsers` — le thème n'était modifiable qu'à la
    construction ; écrire dans `theme` après coup ne touchait que les widgets
    créés ENSUITE.
11. `data::gltf` (`lib/include/data/gltf.hpp`) — lecture de documents glTF 2.0
    par-dessus `data::JsonDocument` : hiérarchie, maillages/primitives,
    matériaux PBR, images/textures, et surtout DÉCODAGE des accesseurs
    (`buffers` → `bufferViews` → `accessors`) vers `float`/`uint32_t`, bornes
    vérifiées. Indépendant de `render3d::` (il rend une description neutre) ;
    non gérés et signalés comme tels : `.glb`, accesseurs creux, animations,
    peaux, attributs entrelacés. Utilisé par le panneau « Ressources » de
    l'éditeur (import d'un modèle en un clic) et exposé aux scripts
    (`scene.import`, `scene.model_info`). 7 tests dans
    `tests/gltf_smoke_test.cpp`, exercés sur les vrais modèles du dépôt.

**Ruptures de compilation préexistantes réparées** (aucune n'était liée à ce
travail ; toutes empêchaient `make check` d'aller au bout, et la première est
signalée jusque dans un commentaire du Makefile) :

- `Option<T>::OkOr` / `OkOrElse` (`core/interop.hpp`) faisaient `Ok<T>(*value)`
  alors que `Option<T>::value` EST le T stocké. Ne compilait pour aucun `T` —
  d'où `tests/ecs_smoke_test.cpp` cassé de longue date ; ce test n'avait de
  surcroît **pas de `main()`**. Corrigés : 13 tests qui repassent.
- `render3d/texture_loader.hpp` passait la constante SDL brute
  `SDL_PIXELFORMAT_RGBA32` à `Surface::Convert(sdl3::PixelFormat)`.
- `tests/sdl3_smoke_test_8.cpp` utilisait d'anciennes constantes
  `tray_entry::*` ; `sdl3::TrayEntryFlags` (enum class) n'avait pas
  d'opérateurs de combinaison — ajoutés (`|`, `&`, `|=`, `HasFlag`).
- `tests/sdl3_smoke_test_11.cpp` et `tests/ui_shader_effects_smoke_test.cpp`
  passaient des types C bruts (`SDL_Vertex`, `SDL_TEXTUREACCESS_TARGET`) là où
  le wrapper attend ses propres types.

## Performance et rendu — deuxième passe (2026-09-12/13)

Demande explicite : « les fps sont trop bas, optimise la librairie pour
atteindre 60 img/s », puis « intègre du multithreading ». La première chose
faite a été de MESURER, et elle a renversé le diagnostic.

### Ce que la mesure a montré

| Configuration | Avant | Après |
|---|---|---|
| Simulation pure (`--headless`, -O2, scène Circuit) | 88 570 img/s | — |
| Vitrine, GPU réel (Intel Iris Xe) | 84 img/s moy. / min 37 | **102 moy. / min 44** |
| Circuit (92 objets) | 72 moy. / min 27 | **101 moy. / min 56** |
| Laboratoire physique | — | **121 moy. / min 64** |

Deux enseignements qui valaient tout le reste :

1. **Le CPU n'a jamais été le problème** : 0,01 ms par image pour scripts +
   physique + synchronisation ECS. Tout le temps était dans le rendu.
2. **Les premières mesures étaient faussées par l'environnement** : elles
   passaient par `xvfb` (Vulkan LOGICIEL, `llvmpipe`), qui donne 6 img/s là où
   le GPU réel de la machine en donne 100. Toujours vérifier `DISPLAY` et
   `vulkaninfo` avant de conclure quoi que ce soit sur les performances.

Un chronométrage PAR PHASE a été ajouté pour ne plus optimiser à l'aveugle :
`ui::UiFrameTimings` (styles / mise en page / viewport 3D / effets / dessin 2D,
publié par `ui::Ui::LastFrameTimings()`), plus `Runtime::SimulationMs()` /
`PortalMs()`, agrégés par `game_editor::PhaseStats` et publiés dans le
rapport (texte et JSON) ainsi que dans l'onglet Profil.

### Optimisations de la bibliothèque

- **Culling de frustum** dans `render3d::Canvas` (`SetFrustumCullingEnabled`,
  actif par défaut) : chaque `DrawMesh*` teste la boîte englobante du maillage
  transformée contre le frustum de la passe. Appuyé sur un nouveau
  `Mesh::LocalBounds()` mémorisé. Compteurs `CulledDrawCount()` /
  `SubmittedDrawCount()` pour le profilage.
- **Portail hors champ = coût nul** : `RenderPortalRecursive` testait déjà la
  visibilité avant de RÉCURSER, mais l'appel de plus haut niveau rendait la
  scène entière même portail dans le dos.
- **Plus d'allocation GPU par image** : `DownloadColorTexture` créait un
  tampon de transfert à chaque téléchargement (donc par viewport et par
  image). Il appartient désormais à l'`OffscreenTarget`.
- **Deux barrières globales supprimées par viewport et par image** :
  `Canvas::RenderObjectOffscreen` et le téléchargement faisaient chacun un
  `WaitIdle()` — attendre que le device ENTIER se vide, rendu 2D de
  `SDL_Renderer` compris. Remplacés par une attente sur la CLÔTURE de la
  soumission concernée.
- **`sdl3::Renderer::SetVSync`/`GetVSync`** : aucun moyen n'existait de régler
  la synchronisation verticale.
- Côté démo : un panneau MASQUÉ ne se reconstruit plus (l'outliner,
  l'inspecteur, la console et le profileur se reconstruisaient même
  invisibles, à 10-20 ms l'à-coup).

### Multithreading

`jobs::JobSystem` (`lib/include/jobs/job_system.hpp`) — vivier de fils
persistants sur `sdl3::Thread`/`Mutex`/`Condition`, `ParallelFor` bloquant où
le fil appelant participe au travail. `JobSystem(0)` exécute tout en ligne,
ce qui donne le point de comparaison exact en test.

Utilisé par `physics::World` (phase large en O(n²) et génération de
manifolds), via un `jobSystem` FACULTATIF : sans lui, comportement d'origine
inchangé. **L'ordre des paires et des contacts est rigoureusement préservé**
(concaténation par ligne croissante, compaction dans l'ordre des paires) —
sans quoi le solveur à impulsions séquentielles donnerait un résultat
différent et la reproductibilité promise par `--seed` tomberait. Vérifié :
200 corps, `--jobs=0` et `--jobs=7` donnent la MÊME position finale au
centième près.

Gain mesuré sur 200 corps dynamiques : 4,38 → 3,77 ms de simulation (~14 %).
Modeste, et c'est normal : les 8 itérations du solveur séquentiel dominent et
ne sont pas parallélisables sans changer l'algorithme. `--jobs=N` permet de
comparer.

### Trois bugs de rendu, tous liés, trouvés par l'utilisateur

Signalés successivement : « la scène est inversée verticalement », puis « la
face du dessous est affichée », puis « inversion des normales ». C'était **un
seul défaut d'origine et sa compensation** :

1. `math::FMatrix4::Perspective`/`Ortho` retournaient Y « pour le NDC Vulkan ».
   Or SDL_GPU normalise les NDC sur la convention D3D12/Metal — **+Y vers le
   haut** — et fait lui-même la conversion pour Vulkan (SDL_gpu.h, section
   « Coordinate System »). C'était donc un SECOND retournement : toute scène
   sortait tête en bas.
2. En compensation, `Canvas` réglait `front_face` sur `CLOCKWISE`. Une fois le
   retournement supprimé, ce réglage éliminait les faces AVANT et gardait les
   arrière — on voyait l'intérieur des objets. Remis à `COUNTER_CLOCKWISE`,
   qui est la convention réelle des maillages du module.
3. Les normales et l'éclairage, eux, n'avaient rien : mesure faite, une face
   au soleil rend 255 et une face à l'ombre 0.

**Les deux réglages doivent changer ENSEMBLE** : ils décrivent le même sens.
`tests/render3d_orientation_smoke_test.cpp` (6 tests) verrouille les trois
propriétés par OBSERVATION DE PIXELS — c'est la seule façon de vérifier une
convention graphique, et ces bugs se compensaient précisément parce que
personne ne les avait jamais observés séparément.

### Et un quatrième, en chemin

`math::FAABB::Expand` utilisait `if/else if` par axe : le premier point inséré
dans une boîte vide prenait la branche `p < min` et sautait `p > max`, laissant
la boîte INVALIDE, et une suite de points décroissants ne mettait jamais `max`
à jour. Tous les utilisateurs étaient touchés — dont
`physics::Capsule::WorldAABB()` et les bornes de maillage du culling.
Régression verrouillée dans `tests/math_smoke_test.cpp`.

## Pièges propres à l'application, déjà payés

- **Double construction de l'interface.** `SetTheme` reconstruisait l'arbre,
  et `RunWindowed` l'appelait AVANT `Build()` : deux interfaces superposées,
  dont une figée (la barre d'état affichait ses valeurs initiales par-dessus
  les vraies). `SetTheme` ne reconstruit désormais que si un arbre existe.
- **Les popups de menu sont des RACINES INDÉPENDANTES** (`UiFactory::Menu` le
  documente) : `DespawnTree(m_root)` ne les touche pas, il faut les détruire
  séparément à chaque reconstruction.
- **Une `sdl3::Font` enregistrée par pointeur** doit l'être depuis son
  emplacement DÉFINITIF (l'enregistrer depuis une locale qu'on retourne
  ensuite donne un use-after-return), et être détruite AVANT le `TtfContext`
  (donc pas dans une variable statique).
- **La caméra de poursuite lit le cap de sa cible** : utiliser le lacet de la
  caméra libre la faisait traîner dans une direction fixe du monde. Au-delà
  d'une vitesse notable, le cap est pris sur le VECTEUR VITESSE (insensible
  aux à-coups d'orientation lors d'un contact), et la vitesse angulaire du
  corps est remise à zéro dès qu'un script écrit une orientation explicite.

## Troisième passe (2026-09-20) : édition directe dans le viewport

Demande : « continue de développer le game editor ». Manquaient les gestes
que fait tout éditeur 3D : cliquer un objet DANS la vue, le tirer, et annuler.

### Ce qui a été ajouté à l'application

- **Sélection au clic** : `Runtime::ViewportRay` (pixel → rayon) +
  `PickAt`/`SelectAt`, qui testent les TRIANGLES via
  `render3d::PickMeshFace`. Viser le trou d'un tore ne le sélectionne donc
  pas — une boîte englobante, si. Les objets masqués sont ignorés.
- **Manipulateur** (déplacer / tourner / redimensionner, W/E/R) : trois
  flèches, trois anneaux, trois poignées, bâtis en nœuds `render3d::Shape`
  QUI NE SONT PAS DANS LE DOCUMENT (donc ni enregistrés, ni listés, ni
  sélectionnables). Taille proportionnelle à la distance à la caméra, axe
  survolé grossi. `PickGizmoAxis` teste la distance du rayon au segment de
  l'axe (à l'anneau pour la rotation), avec une tolérance proportionnelle à
  la taille affichée : ce qui est visuellement épais est attrapable.
- **Magnétisme** (X) : grille du monde, angles, facteur d'échelle. Il
  s'applique SEULEMENT à l'axe tiré — sinon tirer X faisait aussi sauter Y
  et Z au premier accrochage (vu à la première capture d'écran).
- **Annuler / rétablir** (Ctrl+Z, Ctrl+Y, menu, barre d'outils) : pile
  d'INSTANTANÉS du document (objets + sélection), pas de commandes inverses.
  Une suppression en cascade ou un renommage qui réécrit les liens s'annule
  alors exactement, et une nouvelle commande n'a rien à déclarer pour être
  annulable. 64 étapes, la plus ancienne oubliée au-delà.
- **Un glissé = UNE étape** : `BeginEdit`/`EndEdit` regroupent les dizaines
  de commandes émises pendant un glissé. Sans ça, annuler un déplacement
  demanderait autant de Ctrl+Z que de pixels parcourus.
- **Le mode Jeu n'entre pas dans l'historique** : la physique écrit dans le
  document à chaque image, et `Stop` restaure déjà l'instantané de départ.
- **Pilotage par script** : `editor.undo/redo/undo_label`, `gizmo_mode`,
  `snap`, `snap_steps`, `select_at(x, y, w, h)` (un clic en pixels),
  `gizmo_drag(axe, de, à)` (un glissé complet), `object.scale` en lecture.
  Le scénario `edition` rejoue donc exactement les gestes de la souris, et
  VÉRIFIE chaque étape par `assert`.
- **Vol caméra sous clic droit maintenu** (Unity/Unreal) : sans ça, W, E et R
  ne pouvaient pas servir aussi aux modes du manipulateur.

### Navigation complète dans la vue (demande de suite)

« Déplacer la caméra dans tous les sens lors de l'édition » — il manquait
tout ce qui ne se fait pas en volant vers l'avant :

| Geste | Effet |
|---|---|
| clic DROIT maintenu | orienter la vue + ZQSD/WASD voler, E/Q monter/descendre, Maj accélérer |
| clic MILIEU maintenu | panoramique : la vue glisse dans SON plan d'écran (on tire le décor) |
| molette | avancer / reculer le long de la visée |
| ALT + clic gauche | orbiter autour de la sélection (ou de ce qu'on regarde) |
| flèches, Page haut/bas | mêmes déplacements SANS tenir de bouton (clavier seul) |

Trois points qui ne vont pas de soi :

1. **Le pivot d'orbite est figé au début du geste.** Sinon il suit la
   sélection — qui bouge si on la manipule — et la caméra dérive.
2. **Panoramique et molette sont proportionnels à `PivotDistance()`** : le
   même mouvement de souris parcourt un petit objet finement et un circuit
   entier rapidement, et un cran de molette ne traverse jamais l'objet visé.
3. **L'orbite garde les sens du regard libre** (souris à droite = vue vers la
   droite, souris vers le bas = vue plongeante) ; comme elle tourne autour
   du pivot au lieu de pivoter sur place, la caméra MONTE quand on plonge.
   Le tangage est borné aux mêmes ±1,5 rad : la vue ne se retourne jamais.

Côté scripts : `camera.pan`, `camera.dolly`, `camera.orbit`, `camera.pivot`
s'ajoutent à `camera.position/set_position/look/move`, d'où le scénario
`camera` (orbite, plongée, recul, panoramique, vol libre — 6 captures), qui
VÉRIFIE que l'orbite conserve la distance au pivot.

### Bugs trouvés par les captures d'écran

1. **Le manipulateur était mangé par la géométrie** : dessiné avec le test de
   profondeur, il disparaissait dans l'objet qu'il manipule. D'où l'ajout de
   `Material::depthTest` (ci-dessous) — un éditeur doit pouvoir attraper un
   axe même à l'intérieur d'un objet.
2. **L'inspecteur ne suivait pas le manipulateur** : ses champs ne se
   rafraîchissaient qu'au changement de SÉLECTION, donc un objet tiré à la
   souris affichait encore son ancienne position. D'où `onObjectChanged`,
   muet pendant le mode Jeu (y reconstruire l'inspecteur 60 fois par seconde
   coûterait plus que tout le reste).

### Ajouts à `lib/` (suite de la liste ci-dessus)

12. `render3d::Camera::ScreenPointToRay(x, y, w, h)` et `WorldToScreen` — le
    chaînon manquant entre un pixel de souris et `PickMeshFace`, qui n'était
    « branché à aucun code interactif » (son propre en-tête le disait).
    Un-projette DEUX points (près/loin) au lieu de supposer que les rayons
    convergent sur la position de la caméra : vrai aussi en projection
    orthographique. `WorldToScreen` rend NONE derrière la caméra, où la
    division perspective donnerait un pixel plausible mais faux.
13. `math::FRay::DistanceTo(point)` — distance à la DEMI-droite : un point
    derrière l'origine est mesuré depuis l'origine, donc une poignée derrière
    l'observateur n'est jamais « sous le curseur ».
14. `render3d::Material::depthTest` (+ `PipelineKey`) — matériau dessiné
    par-dessus la scène, sans test ni écriture de profondeur : manipulateurs,
    silhouettes de sélection, axes de débogage.

## Vérification

- `tests/game_editor_smoke_test.cpp` — 53 tests, aucun GPU : ligne de
  commande, aller-retour JSON (dont coordonnées entières), contenu livré
  (scripts compilent, noms uniques, parents résolus, anneau de points de
  passage), runtime (instanciation, commandes, sélection, physique),
  mode Jeu (restauration exacte, reproductibilité à graine égale, **le
  circuit boucle au moins un tour et la voiture reste sur la piste**), API
  hôte des scripts, scénarios, rapport.
- `tests/script_smoke_test.cpp` — 40 tests ([[project-script-language]]).
- `tests/gltf_smoke_test.cpp` — 7 tests sur les modèles réels du dépôt :
  structure sans chargement des tampons, décodage positions/indices (indices
  tous dans les bornes), **normales unitaires** (le contrôle sémantique qui
  attrape un mauvais type de composante ou un offset décalé), facteurs PBR,
  cohérence de la hiérarchie, et refus argumenté d'un accesseur hors tampon.
- Exécutions réelles sous `xvfb-run` + ASan/UBSan : scénario `tour` (820
  images, 15 captures PNG), `circuit`, `themes`, `edition` (8 captures), tous
  à code de sortie 0.
- Navigation : 7 tests de plus (molette exactement sur l'axe de visée,
  panoramique orthogonal à la visée et repère orthonormé, orbite qui conserve
  la distance ET continue de viser le pivot, plongée qui élève la caméra,
  bornes de tangage infranchissables, pivot = sélection sinon point devant,
  vol dont le `y` suit le haut du MONDE, et pilotage complet par script).
- Troisième passe : 16 tests de plus (historique : annulation exacte, objet
  supprimé rendu AVEC ses enfants, mode Jeu qui ne pollue pas la pile, limite
  de 64 ; sélection : centre du viewport, objet masqué ignoré, trou du tore
  traversé ; manipulateur : un axe à la fois, glissé = une étape, magnétisme
  sans dérive des autres axes, prise d'axe à la tolérance près, pilotage par
  script) et 4 tests de bibliothèque dans `tests/picking_smoke_test.cpp`
  (rayon central, bords à tan 30°, aller-retour pixel → monde → pixel, point
  derrière la caméra refusé, demi-droite de `DistanceTo`).

## Commandes

```sh
make game_editor_demo && ./build/bin/game_editor_demo --help
./build/bin/game_editor_demo --list-scenarios
xvfb-run -a ./build/bin/game_editor_demo --scenario=tour \
    --screenshot-dir=captures --report=captures/tour.json --report-format=json
./build/bin/game_editor_demo --headless --scenario=smoke --frames=200 --verbose
./build/bin/game_editor_demo --headless --scenario=circuit --frames=1500 --report=circuit.txt

# Édition directe : clic, manipulateur, magnétisme, annulation (8 captures)
xvfb-run -a ./build/bin/game_editor_demo --scenario=edition --frames=140 \
    --screenshot-dir=captures

# Navigation : orbite, plongée, molette, panoramique, vol libre (6 captures)
xvfb-run -a ./build/bin/game_editor_demo --scenario=camera --frames=110 \
    --screenshot-dir=captures
```

Un scénario dont un `assert` échoue fait échouer le processus (code 1) : c'est
ce qui en fait une vérification et pas une démonstration.

## Passage à une hiérarchie de nœuds — 2026-09-21

Le document de l'éditeur n'est plus une LISTE plate d'objets avec un champ
`parent` portant un nom : c'est un `scene::NodeTree` (nouveau module
`lib/include/scene/`, cf. [node_hierarchy.md](node_hierarchy.md), qui fait
foi pour l'architecture et les décisions).

Ce qu'il faut savoir avant de retoucher l'éditeur :

- `SceneDesc::objects` **n'existe plus** ; c'est `SceneDesc::tree`. Les
  itérations passent par `scene.Objects()` (ordre d'affichage) ou
  `tree.Traverse(...)`, et `ObjectCount()` remplace `objects.size()`.
- `SceneDesc::Find(nom)` rend un `scene::Node*`, pas un `ObjectDesc*`. La
  forme, le matériau et la physique sont des COMPOSANTS :
  `VisualDesc::Read(node)` / `PhysicsDesc::Read(node)`, écrits par `Write(node)`.
- `ObjectDesc` reste, mais comme CONSTRUCTEUR d'objets (`ToNode()`), employé
  par le contenu livré, l'import glTF, les scripts et les tests.
- `TransformDesc` est un alias de `scene::Transform` : la rotation est un
  quaternion, `EulerDegrees()`/`SetEulerDegrees()` font l'aller-retour avec la
  forme éditée. `transform.eulerDeg` n'existe plus.
- Les commandes du runtime existent par IDENTIFIANT (`scene::NodeId`) et par
  NOM/CHEMIN. `SelectedId()`/`Select(NodeId)` sont la forme de référence.
- Format de projet en version **3** ; les fichiers en version 2 sont convertis
  à la lecture.
- Supprimer un objet supprime désormais son SOUS-ARBRE (avant : les enfants
  remontaient à la racine).

Quatre scènes livrées au lieu de trois (« Assemblages » démontre la
hiérarchie), et un scénario `node_hierarchy` qui vérifie tout le cycle.

## Refonte « éditeur de jeu » d'après maquettes — 2026-09-26

Demande : « améliorer l'éditeur de jeu afin de proposer une alternative
complète à Godot, Unreal », interface inspirée de 8 maquettes
(`captures/editor_image_1..8.jpeg` : Scene Tree, Inspector à sections, menu
contextuel « Create Child Node ▸ 3D / Light / Control / Node », Asset Manager
à vignettes, console, éditeur de script/JSON, mode Test plein écran). Une
« alternative complète » à des moteurs de plusieurs millions de lignes n'est
pas un objectif atteignable en une passe : ce qui suit reproduit l'INTERFACE
des maquettes et les fonctionnalités qu'elles montrent, de bout en bout.

### Interface (panels.hpp réécrit, + 5 fichiers)

| fichier | rôle |
|---|---|
| `kit.hpp` | `UiContext`, apparence par type de nœud (`LookOf`), colorations syntaxiques (script/JSON/journal, fonctions pures), `PropertyRows` (lignes d'inspecteur), `DockPanel` (panneau à onglets + × + ≡) |
| `scene_tree.hpp` | arbre : recherche, + / ≡, flèches, icône colorée + « (Type) » gris, menu contextuel à sous-menus, renommage (F2, boîte modale), glisser-déposer de reparentage, dépli des ancêtres de la sélection |
| `inspector.hpp` | inspecteur à sections repliables par composant avec menu ⋮ (réinitialiser / retirer / modifier en JSON), « Ajouter un composant » ; `LibraryPanel` : matériaux prêts à appliquer, scripts (état de compilation), monde |
| `assets.hpp` | `AssetBrowserModel` (navigation pure, testée) + panneau : ← → ↑, fil d'Ariane, arbre des dossiers, vignettes (textures réelles, modèles glTF rendus dans un petit `Viewport3D` à la demande), coches de compilation, zoom ; racines « Projet » (scènes, scripts) + `assets/` + « Sauvegardes » |
| `documents.hpp` | zone centrale à onglets : vue 3D + documents de code (script de bibliothèque, script de scène, composant/propriétés en JSON, fichier du disque EN LECTURE SEULE, console de script) ; « • » si modifié, Ctrl+S vérifie et enregistre |
| `panels.hpp` | assemblage : 6 menus, docks redimensionnables (poignées `UiResizeHandle`), barre de la vue (manipulateur, magnétisme, ▶, plein écran, aller à un nœud, vue seule), trièdre d'orientation + « ‹ Persp », mode Jeu plein écran, console, profileur, barre d'état ; `UiCommand` (`editor.ui(...)`) |

Thème par défaut `studio` (`UiTheme::Studio`, gris neutres). Échap ne quitte
plus l'application (il sort du mode Jeu, ferme un dialogue, désélectionne) :
on quitte par la fenêtre, Fichier › Quitter ou Ctrl+Q.

Piège de mise en page rencontré partout : un `UiFlow` a **8 px de marge par
défaut** et tout conteneur qui déborde affiche une barre de défilement sans
opt-in ; et `Pad()` sur un BOUTON en fait un conteneur mesuré par ses enfants
(aucun) — boutons aplatis. Chaque conteneur de ces fichiers pose donc sa
marge explicitement.

### Document et runtime

- Composants `Light` (ponctuelle / projecteur, couleur, intensité, portée,
  angle, pénombre, ombres), `Camera` (champ, caméra du jeu), `Trigger` (boîte,
  évènement, une seule fois), `Script` (nom dans la bibliothèque) ; types
  Folder / Light / Camera / Trigger. `Project::scripts` : bibliothèque de
  scripts (format de projet v4, un v3 se lit tel quel).
- Lumières rendues (`Canvas::SetLights`, position MONDE, visibilité héritée),
  repères d'édition cliquables (sphère de lumière, boîte de caméra, boîte de
  zone en fil de fer) masqués en mode Jeu.
- Scripts de NŒUD : un interpréteur par script utilisé, partagé par ses
  nœuds, `on_start(self)` / `on_update(self, dt)` / `on_trigger(self, …)` ;
  une erreur désactive CE script seulement. Zones : `on_trigger` /
  `on_trigger_exit` sur transitions uniquement, `once` respecté.
- Caméra courante d'un nœud en mode Jeu (vue subjective), caméra mise à jour
  APRÈS la simulation (sinon une image de retard, tremblement visible).
- `SetVisual` remplace le maillage SUR PLACE (tirer « Dimensions » ne
  reconstruit plus toute la scène à chaque mouvement).
- `ResolveId` : cache nom → nœud validé par `NodeTree::StructureStamp`.
- API script : `light.*`, `input.mouse_delta()`, `editor.ui(commande, arg)`.
- Scène **Donjon** (≈ 450 nœuds) : couloir, torches vacillantes (script
  `torchlight` paramétré par les propriétés du nœud), coffre, herse,
  squelette, joueur à la première personne (capsule physique, visite guidée
  par étapes `waypoint` sans clavier), trois zones de déclenchement.

### Ressources et sauvegardes

L'utilisateur a séparé `assets/` (ressources, lecture seule) de
`saves/<demo>/` (sauvegardes). Options `--assets-dir` et `--saves-dir`
(défaut `saves/game_editor_demo`, sous-dossiers `projects/`, `scenes/`,
`scripts/`), globales de script `assets_dir` et `saves_dir`. Le scénario
`node_hierarchy` écrit dans `saves_dir`. Lancer les scénarios avec un
`--saves-dir` de brouillon pour ne pas toucher aux sauvegardes réelles.

### Vérification

`--scenario=interface` rejoue les écrans des maquettes (8 captures, chaque
étape vérifiée). Tests : `game_editor_smoke_test` 92 (dont donjon, scripts
de nœud, zones, composants, gabarits, colorations, navigateur de ressources),
et côté bibliothèque les suites listées dans project_ui_ecs_module.md.

## Thème appliqué à toute l'application, routage clavier — 2026-09-26

- `kit::Palette`/`kit::PaletteOf(theme|ctx)` : fond, bandeaux, barres d'outils,
  vignettes, sélection, filets, ok/warning/error, tous DÉRIVÉS du thème
  (luminance du `panelBg` → clair/sombre). Plus aucun `kit::Rgb` de chrome codé
  en dur. `kit::Readable(ctx, c)` fonce les teintes pastel d'icônes sur thème
  clair (appliqué par `Glyph`/`IconButton`). Coloration syntaxique :
  `kit::syntax::Colors()` (jeu `Scheme::Dark()`/`Light()`), choisi par
  `EditorUi::SetTheme`.
- Bug corrigé : `DockPanel::Build` ne vidait pas `m_pages`/`m_active` ; au
  changement de thème (`Rebuild`), les onglets se dupliquaient et les pages
  pointaient vers des entités détruites (panneaux vides).
- Clavier : `app.hpp` passe un `sdl3::Event&` MUTABLE à `gui.HandleEvent`
  puis à `editorUi.HandleEvent`, qui ignore tout évènement consommé. Le vieux
  filtre `TextFieldHasFocus()` des raccourcis a disparu (il reste pour
  `PollCameraKeys`, qui lit l'état du clavier et non des évènements).
- Vérif : scénario `themes` (bascule à chaud des 4 thèmes) + `interface` +
  `node_hierarchy`, rc=0, 0 erreur.

## Navigateur de ressources : arborescence et opérations sur les fichiers (2026-10-05)

- **Arborescence complète** : « Scènes » et « Scripts » montrent d'emblée tous
  les sous-dossiers de `<projet>/scenes` et `<projet>/scripts` (pièces
  `.scene`, modules…), à côté des éléments du projet. Les chemins disque de
  ces deux dossiers SONT « Scènes » / « Scripts » (`AssetBrowserModel::
  Canonical`) : un seul emplacement, un seul contenu, un seul fil d'Ariane
  (« Projet › Scènes › pieces »). Flèche = déplier/replier, état retenu.
- **Opérations** (`editor/asset_ops.hpp`, testées sans fenêtre) : nouveau
  dossier, renommer, dupliquer, supprimer (avec confirmation), déplacer
  (glisser sur un dossier de la grille ou de l'arbre, ou couper/coller).
  Les ÉLÉMENTS DU PROJET (`AssetEntry::managed`) passent par le projet
  (`Runtime::RenameScene/RemoveScene/DuplicateScene/RenameScript/
  RemoveScript/DuplicateScript` ; renommer un script met à jour les nœuds qui
  le portent dans toutes les scènes) — l'enregistrement écrit et efface leurs
  fichiers ; ils ne se déplacent pas. Le reste agit sur le disque. Seul le
  dossier du projet se modifie (ressources partagées en lecture seule ;
  manifeste et dossiers `scenes/`, `scripts/`, `assets/` protégés). Les
  fichiers que le projet possède encore (`Runtime::ProjectFiles`, ex. l'ancien
  fichier d'une scène renommée avant enregistrement) ne sont jamais montrés
  comme fichiers libres.
- **Sélection multiple** : clic, Ctrl+clic, Maj+clic (plage depuis l'ancre),
  Ctrl+A ; renommer plusieurs éléments les numérote (« mur », « mur 2 »…),
  extension gardée. Clavier (pointeur sur le panneau, prioritaire sur les
  raccourcis de la scène) : Suppr, F2, Ctrl+D, Ctrl+X, Ctrl+V, Ctrl+A,
  Entrée, Retour arrière, Échap. Pilotage par script : `editor.ui(
  "select_assets", "a|b")`, `asset_new_folder`, `asset_rename`,
  `asset_duplicate`, `asset_delete`, `asset_move`.
- **Bibliothèque `ui::`** : `UiSelectable`/`UiTreeNode` déclenchent enfin
  `onClick` (appui puis relâchement sur la ligne, ni après un dépôt ni sur un
  bouton intérieur) ; `onDoubleClick` (boutons et lignes) ; `onDragStart` ;
  fantôme générique (`UiDragPayload::label`/`count`, dessiné près du
  pointeur avec une pastille « N ») ; surlignage des cibles de dépôt
  survolées (`UiDropTarget::highlight`, désactivable quand la cible dessine
  son propre indicateur, comme l'arbre de scène). Tests :
  `tests/ui_selectable_click_smoke_test.cpp`.
