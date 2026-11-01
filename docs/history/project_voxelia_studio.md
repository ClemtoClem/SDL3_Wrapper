---
name: project-voxelia-studio
description: "Voxelia Studio, socle (2026-10-31) : éditeur dans sa propre fenêtre SDL, sur la MÊME partie que la fenêtre de jeu (aucune copie) — World Outliner et inspecteur sur le registre ECS vivant, vue 3D DIRECT partagée, console à code d'état, pause/pas à pas, rapport, captures. Ce qui reste à faire."
metadata:
  type: project
---

**2026-10-31.** Socle de Voxelia Studio (étapes 0 et 1 de
`demos/voxelia/IDE_voxelia_prompt.md`), branché sur le VRAI moteur.

> Une version précédente (≈15 000 lignes, studio « simulé » sur un
> `VoxelWorld` maison sans aucun module du moteur, `WindowManager` factice,
> `main.cpp` du jeu remplacé, build cassé) a été RETIRÉE de l'index à la
> demande de l'utilisateur et sauvegardée hors dépôt. Ne pas la réintroduire :
> tout ce qui suit lit et modifie la partie réelle.

## Architecture

```
App::RunWindowed (demos/voxelia/src/app.cpp)
 ├─ ui::WindowManager   (libs/ui, cf. project_ui_multi_window.md)
 │   ├─ fenêtre « game »   (principale)  → Shell → Session → voxel::Game + WorldRenderer
 │   └─ fenêtre « studio » (F10/--studio) → studio::Studio
 ├─ render3d::Canvas    UN SEUL, sur le device GPU du gestionnaire
 └─ studio::StudioReport (--report)
```

- Même processus, même `voxel::Game`, même scène `WorldRenderer::Root()` :
  la vue 3D du studio est une seconde `ui::Viewport3D` (DIRECT, aucune copie,
  device partagé) sur la scène de la partie, avec SA caméra.
- Le studio a son monde ECS d'interface et sa feuille CSS (`STUDIO_CSS`) ;
  celle du jeu n'est pas touchée.

## Fils (cf. project_voxelia_threads.md) — préservés

Boucle par image : `EndSimulation` → évènements → commandes → `Shell::Update`
→ **`Studio::Update` (le studio lit/modifie le jeu ICI)** →
`windows.Update` → `windows.Draw` (les deux fenêtres) → captures →
`BeginSimulation` → `windows.Present`. Le studio ne touche jamais au jeu
pendant un pas de « voxel-update » ; les fils « voxel-render-N » maillent
toujours des instantanés. La fenêtre du studio n'attend pas le balayage
(`vsync=false`) : seule la fenêtre de jeu le fait.

## Modules (demos/voxelia/{include/voxelia,src}/studio/)

| Fichier | Rôle |
|---|---|
| `console` | `CommandResult{status, message, confirmationPrompt, onConfirm}`, `ConsoleModel` : Ok ferme, Error garde ouverte, Warning ferme après 5 s, ConfirmRequired pose « [y/N] » (y/o/yes/oui), Pending jusqu'à `Resolve` ; historique, complétion, `help` ; repli : commandes du jeu (`Session::Command`). Modèle sans fenêtre. |
| `outliner` | `CollectEntities(game, filtre, caméra)` : entités du registre vivant, catégorie (joueur, créature, objet, bloc qui tombe, plateforme mobile), chunk, distance, filtres texte/catégorie/distance/vivants ; `InspectEntity` : sections par composant présent (Identité, Transform, Physique, Santé, Survie, Effets, IA, Plateforme mobile, Visuel). |
| `report` | fonctionnalités visitées (pire statut, visites, détail), cadence min/moy/p50/p95/p99/max, fils max, captures, compteurs, avertissements, erreurs ; texte + JSON. |
| `studio` | la fenêtre : barre d'outils (Pause/Reprendre, Pas, Suivre, Au joueur, état), outliner 5 Hz (reconstruit seulement si l'ensemble change), vue 3D, inspecteur (reconstruit si la structure change, valeurs mises à jour à chaque image), console. |

Ajouts au jeu : `Shell::HoldSimulation`/`SimulationHeld`/`StepSimulation`
(passent par le `paused` existant de `Session::Update`).

## Commandes (console, et `--exec=N:!studio LIGNE`)

`studio.select player|<entité>|<nom>`, `studio.deselect`, `studio.focus`,
`studio.follow`, `studio.camera player|x y z`, `studio.pause`, `studio.play`,
`studio.step [n]`, `studio.heal`, `studio.kill` (confirmation),
`studio.stats` (F3), `reload` (Warning : pas encore branché), `help` ; toute
autre ligne → commande du jeu. `!studio open|close`.

Vue 3D : clic droit maintenu + WASD/ZQSD (scancodes), E/Q monter/descendre,
Maj ×3, molette ; double-clic d'une ligne : caméra dessus ; F : idem pour la
sélection. Tangage borné ±1,5 rad. Échap : quitte la poursuite, puis
désélectionne — ne ferme rien. Ctrl+Q (toutes les fenêtres) quitte, F10
ouvre/ferme le studio, fermer la fenêtre de jeu quitte, fermer le studio le
détache seulement.

## Pièges payés

- **Caméra poursuite dans le terrain** : 6 blocs derrière la cible tombe dans
  la colline → vue entièrement noire (pris d'abord pour un bug du mode
  DIRECT : en READBACK sans poursuite tout s'affichait — deux variables
  mêlées). La poursuite lance un rayon cible → caméra et s'arrête avant le
  premier bloc `solid`. Cap pris sur la VITESSE, gardé à l'arrêt.
- Fermer le studio pendant `Dispatch` de SA fenêtre (F10 dans le studio)
  détruirait le contexte en cours d'usage : les raccourcis posent des
  drapeaux, traités après la boucle d'évènements.
- `onClose` du studio capture des références locales de `RunWindowed` : le
  studio est fermé explicitement avant la sortie de la fonction (sinon le
  gestionnaire, détruit en dernier, rappellerait `onClose` sur des objets
  morts).
- En debug, une partie neuve met ~1000 images à charger : un scénario qui
  agit avant obtient « aucune partie en cours » (Error rapportée en
  avertissement, comportement voulu).

## Vérification

- Tests sans fenêtre `demos/voxelia/tests/test_studio.cpp` (8) : console
  (5 codes et leurs effets, historique, complétion, découpage), outliner et
  inspecteur sur une VRAIE partie (joueur, cochon, objet au sol, filtres,
  mort, dégâts et poison vus en direct, entité disparue), rapport (pire
  statut, centiles, JSON relu). voxelia 29/29 sous ASan+UBSan, LSan, TSan.
- Exécution réelle (pilote SDL `offscreen`, vrai GPU, pas d'Xvfb sur la
  machine) :
  `SDL_VIDEO_DRIVER=offscreen voxelia --world=studio_demo --seed=4242 --studio --frames=1500 --exec=1110:'!studio studio.select player' … --screenshot-desktop=1400:desktop.png --studio-screenshot=1400:studio.png --report=rapport.txt`
  → rc=0, stderr 0 octet, rapport « succès », 17 fonctionnalités visitées,
  device GPU partagé, 2 fenêtres sur la capture du bureau.

## Étape 2 — éditeur de blocs et rechargement à chaud (2026-10-31)

### Moteur (libs/infrastructure/voxel)

- `ContentRegistry::LoadContext::definition` + `BlockDef::source`,
  `sourceFile`, `baseDir` : la définition TELLE QU'ÉCRITE (`states` compris,
  la même pour tous les états) et son fichier — `raw` n'est que l'état
  fusionné.
- `ContentRegistry::ForgetLoadWarnings(owner)` : un rechargement ne répète
  pas les avertissements de l'ancienne version.
- `Chunk::InvalidateContent`, `World::ContentChanged` : portée max des
  lumières relue (elle était figée à la construction du monde), sources de
  lumière de chaque chunk relues, chunks à remailler.
- `Game::ReloadDefinition(def, ctx)` : `Load` (un id connu est remplacé, son
  **id numérique conservé** : les chunks restent valides, rien à régénérer),
  `Finalize`, `ContentChanged` ; rend les ids des blocs, états compris.

### Fils — l'ordre est imposé

Les fils « voxel-render-N » LISENT le `ContentRegistry` pendant le maillage
(et `Load` peut réallouer `m_blocks`). `Session::ReloadDefinitions(lot)` :
`EndSimulation` → `WorldRenderer::Quiesce` (attend les fils de maillage) →
`TextureReferences` (avant) → `Game::ReloadDefinition` × N →
`AtlasMissing(blocs, avant)` → `WorldRenderer::ReloadContent(atlas)` :
mailleurs refaits (ils précalculent le rendu de chaque bloc), chunks et
entités remaillés au fil des images, maillages en vol jetés (`m_meshSerial`).

### Studio

- `BlockDocument` (modèle, sans fenêtre) : formulaire et JSON sur le même
  document, chemins pointés, annulation par instantanés (geste =
  `BeginEdit`/`EndEdit`, 64 étapes), validation À BLANC sur une COPIE du
  registre (diagnostics filtrés sur le bloc et ses états, champ fautif
  extrait du message), états fusionnés et leurs différences avec l'état par
  défaut, enregistrement dans le fichier du mod (définition remplacée dans
  le tableau ; l'en-tête de commentaires `//` est gardé, les commentaires
  INTERNES sont perdus — limite de l'écriture JSON).
- `BlockEditor` (panneau) : dock gauche « Outliner | Blocs » (catalogue
  filtrable), dock droit « Inspecteur | Bloc » ; portée « bloc commun » ou
  un état (`states.<nom>.<champ>`, champs hérités marqués « · ») ;
  formulaire (identité, textures, physique, lumière complète, interaction,
  capteur, conteneur), onglet JSON, onglet États ; diagnostics 0,3 s après
  la dernière modification ; un curseur glissé = une étape d'annulation
  (geste clos après 0,4 s) ; Appliquer à chaud (Ctrl+Maj+R), Enregistrer,
  Poser (4 blocs devant la caméra ; l'état de la portée), ↶/↷ (Ctrl+Z/Y).
- Console : `studio.block.{open,new,set,scope,apply,save,place,undo,redo}`,
  `reload` (le bloc ouvert), `reload block <id>` (depuis le disque),
  `reload mod <nom>` (un lot ; Warning voulu : biomes et dimensions ne
  régénèrent pas le terrain), `reload dimension|biome` (Warning : étape 7).

### Piège payé

Premier essai : `reload mod` rechargeait bloc par bloc ET `AtlasMissing`
signalait toute texture absente de l'atlas — or des faces n'y sont jamais
(expressions, modèles) : l'atlas (textures jusqu'à 2048 px) était refait
pour CHAQUE bloc, plus de 10 min en debug. Désormais : un lot = une attente
des fils, un atlas, des mailleurs ; et l'atlas n'est refait que pour une
texture NOUVELLEMENT référencée. Mesuré (debug) : un bloc 69 ms, le mod
`elevators` entier (100 définitions, 314 blocs) 350 ms.

### Vérification

- `demos/voxelia/tests/test_studio_blocks.cpp` (7) : document depuis un
  ÉTAT (bloc entier ouvert), formulaire ⇄ JSON (JSON invalide refusé sans
  perte), annulation (glissé = 1 étape, geste sans effet = 0, 64 max),
  validation à blanc (angles inversés, couleur, bascule vers un bloc
  inconnu, état par défaut inconnu = Error ; contenu vivant intact), états
  fusionnés (`off` diffère par `light.enabled`, `path`), enregistrement
  (remplacement, ajout, en-tête gardé), rechargement à chaud dans un monde
  vivant (même id numérique, chunk invalidé, sources de lumière −1 puis
  rétablies, définition invalide refusée sans dégât).
- Scénario réel (offscreen) : ouvrir le néon, portée « on », angle invalide
  (diagnostic affiché) puis corrigé, couleur/portée, appliquer, poser,
  annuler/rétablir, `reload block`, `reload mod elevators` → rc=0, stderr
  vide, 16 fonctionnalités, néon posé visible dans la vue.

## Étape 3 — atelier de formes et animation (2026-10-31)

Le format « Blockbench » du moteur, ce sont les **rigs** des entités (modèle
`format: "rig"`, cf. voxel/rig.hpp : parties à pivot, boîtes à UV façon
skin, mouvements `walk` / `idle`). L'atelier édite CE format — pas un format
parallèle que le jeu ne lirait pas.

### Moteur (libs/infrastructure/voxel)

- **Animations à images clés** dans les rigs : `"animations": {nom: {length,
  loop, tracks: [{part, channel: rotation|position, keys: [{t, v, interp:
  linear|step|smooth}]}]}}` → `RigClip` / `RigTrack` / `RigKey`,
  `SampleRigTrack` (smooth = Hermite à tangentes nulles), `RigClipTime`
  (boucle / borne). Erreurs : partie inconnue, canal inconnu, durée ≤ 0.
- `PoseRig(rig, partie, RigPoseInput)` : repos + walk/idle sinusoïdaux +
  regard + clip `idle` (permanent) + clip `walk` (durée = un pas, poids =
  intensité de marche), ou un clip IMPOSÉ (éditeur). `WorldRenderer::UpdateRig`
  l'utilise : **les entités du jeu jouent les clips**, décalages de position
  compris.
- `RigRotation(degrés)` : LA convention (x, puis y, puis z — q = Rz·Ry·Rx),
  auparavant écrite en dur dans le rendu.
- `RigBoxUv(boîte)` : le dépliage d'une boîte — vérifié par test identique,
  face par face (miroir compris), aux UV que produit `BuildRigPart`.
- `AssetManager::ForgetRig(id)` ; `Game::ReloadDefinition` l'appelle pour un
  modèle ; `ModelDef::source/sourceFile/baseDir` comme pour les blocs.
- `Session::ReloadDefinitions` refait l'affichage dès qu'UNE définition est
  rechargée (avant : seulement si des blocs l'étaient — un modèle seul
  n'était pas remaillé).

### Studio

- `DefinitionDocument` : base extraite de `BlockDocument` (JSON, chemins,
  annulation par instantanés, enregistrement) ; `BlockDocument` et
  `RigDocument` en dérivent. `Mutate` = une opération structurelle = une étape.
- `RigDocument` : hiérarchie (ajout, suppression — enfants au grand-parent,
  pistes retirées —, renommage propagé aux `parent` et aux pistes,
  reparentage SANS cycle, duplication ; ordre « parent d'abord » maintenu,
  affichage en ordre d'arbre), pivot, rotation de repos, boîtes (ajout,
  édition, suppression, MIROIR x = 0), dépliage (`RigBoxUv`), validation
  (`RigFromJson`, noms en double, tailles nulles, face hors texture — calques
  `only64` contrôlés contre la hauteur doublée —, clés hors du clip), clips
  et clés (même temps à 1 ms près = remplacée ; dernière clé ôtée = piste
  ôtée), courbes échantillonnées par le moteur, **export OBJ** (posé au repos
  ou par un clip, UV v vers le haut).
- `ModelEditor` (panneau « Modèle », catalogue « Modèles ») : aperçu 3D
  animé (scène render3d PROPRE, construite par BuildRigPart / PoseRig /
  RigRotation, skin réel, orbite), onglets Parties (arbre, renommer, parent,
  enfant, dupliquer, supprimer, pivot, rotation + quaternion affiché),
  Boîtes / UV (champs, miroir, vue du dépliage colorée par face), Animation
  (clips, lecture, tête de lecture, clé de la partie au temps courant,
  interpolation, liste des clés, graphe des courbes x/y/z), JSON ;
  Appliquer à chaud, Enregistrer, Exporter .obj, annuler/rétablir ; un
  curseur glissé = une étape.
- Console : `studio.model.{open,new,part,part.add,part.remove,part.rename,
  part.parent,part.pivot,part.rotation,box.add,box.mirror,clip.add,key,play,
  time,export,apply,save,undo,redo}`.

### Pièges payés

- **Aperçu sans tronc ni bras** : BuildRigPart recevait la taille DÉCLARÉE
  du skin (64×32) alors que l'image chargée faisait 64×64 : les UV, divisées
  par 32, tombaient dans les calques transparents du skin 64×64. Le jeu passe
  la taille RÉELLE de l'image (ce qui active aussi uv64 / only64) : l'aperçu
  aussi. Diagnostiqué en écrivant positions et bornes de la scène (justes) —
  la faute était donc dans la texture, pas dans la géométrie.
- Lumière par défaut d'une `Viewport3D` : soleil vertical, ambiante 40/255 —
  côtés presque noirs ; l'atelier règle soleil oblique et ambiante franche.
- Calques `only64` signalés à tort « hors texture » sur `core:biped` : ils
  ne servent qu'aux skins deux fois plus hauts.

### Vérification

- voxel `test_rig.cpp` (6) : analyse des clips et erreurs, interpolations,
  temps bouclé / borné, pose combinée (idle sinus + clip, walk pondéré,
  clip imposé, partie hors bornes), convention de rotation, `RigBoxUv` =
  UV du maillage (avec et sans miroir), rig rechargé relu (l'ancien reste
  valide pour qui le tient).
- voxelia `test_studio_rig.cpp` (8) : vrai `core:biped` ouvert et sans
  diagnostic, invariants de hiérarchie (cycle refusé, parent avant enfant,
  enfants au grand-parent), renommage dans parents et pistes, boîtes /
  miroir / UV / hors texture, clés et courbes (valeurs exactes), annulation
  (glissé = 1 étape), export OBJ (comptes v / vt / f, hauteur au repos puis
  posée par un clip de position), rechargement dans une partie (le moteur
  joue le nouveau clip idle).
- Scénario réel (offscreen) : ouvrir `core:biped`, clip idle à 3 clés
  douces, chapeau (2 boîtes + miroir), lecture, export OBJ (120 triangles),
  appliquer à chaud (40 ms), annuler / rétablir → rc=0, stderr vide,
  biped complet et texturé dans l'aperçu.

## Étape 3 bis — nodebox, glTF, Bézier, mélange (2026-10-31)

### Bézier à poignées (moteur : voxel/rig)

- `RigInterp::BEZIER` ; `RigKey::in` / `out` (`RigHandle` : décalage de
  temps, décalage de valeur). JSON : `"interp": "bezier", "in": {"t", "v"},
  "out": {"t", "v"}` ; `out` d'une clé sert au segment suivant, `in` au
  segment qui arrive ; absentes = un tiers de l'intervalle, pente nulle
  (≡ smooth). `RigSegmentBezier` borne les temps des poignées au segment :
  la courbe reste une FONCTION du temps ; évaluation par dichotomie sur x(s)
  puis valeur au même s. La courbe peut dépasser ses clés (testé).
- Atelier : interpolation `bezier`, poignées de la clé sélectionnée (entrée
  / sortie, t et valeur), « Poignées auto », « Lisser la piste » (tangentes
  de Catmull-Rom, extrémités à plat) ; le graphe montre les points de
  contrôle en nuage. Remplacer la valeur d'une clé garde ses poignées.
  Limite : poignées éditées par champs, pas glissées dans le graphe
  (`ui::Plot` n'a pas de points saisissables).

### Mélange de clips (moteur)

- `"layers": [{"clip", "weight", "speed", "mode": "mix"|"add"}]` →
  `RigModel::layers`, jouées en permanence par-dessus idle / walk par
  `PoseRig` (donc par le JEU) : `add` ajoute la piste pondérée, `mix` amène
  la pose vers celle du clip (repos + piste), sur les seuls canaux animés.
  `RigPoseInput::layers` : couches imposées (éditeur) ; un clip imposé les
  ignore. Erreurs : clip inconnu, mode inconnu.
- Atelier : section « Mélange » (clip, poids, vitesse, mode, retrait) et
  « Aperçu du mélange » (la pose du jeu au temps courant).

### glTF

- **dataio : écrivain** `dataio/documents/gltf_writer.hpp` (n'existait
  pas) : `SaveScene(chemin, SceneDescription)` → `.gltf` + `.bin` ; maillages
  (POSITION avec min/max, NORMAL, TEXCOORD_0, indices u32), matériaux
  texturés (échantillonneur au plus proche, alphaMode MASK), hiérarchie de
  nœuds (TRS), animations (translation / rotation / échelle, LINEAR / STEP) ;
  `ValidateScene` refuse indices hors bornes, tailles incohérentes, parent
  après l'enfant, temps décroissants, CUBICSPLINE. Testé par ALLER-RETOUR
  avec le lecteur du dépôt.
- **voxel : lecteur de modèles** `LoadGltfMesh(chemin, nœud)` — scène
  entière (transformations monde, normales par l'inverse-transposée) ou un
  nœud dans son repère ; un groupe par primitive texturé par l'image du
  matériau. Format `gltf` pour les modèles (`ModelDef`, formes de blocs
  `"type": "gltf"`, modèles d'entités) ; parties de rig maillées :
  `"model": "fichier.gltf", "node": "Nom"` (relatif au mod, blocs → pixels,
  posé au pivot) — `RigFromJson(def, baseDir)`.
- **Atelier** : « Exporter glTF » (un nœud par partie, maillages, chaque
  clip échantillonné à 30 i/s par `PoseRig` — Bézier, paliers, positions
  compris —, skin copié à côté) ; « Importer glTF » (chaque nœud → une
  partie : pivot, rotation de repos, parent, maillage par model + node ;
  skin du glTF repris si le modèle n'en a pas). Limites : le lecteur glTF ne
  lit pas les animations (import de géométrie et de hiérarchie seulement),
  ni `.glb` ni base64 ; l'échelle des nœuds est ignorée à l'import.
- `RigEulerFromQuaternion` (inverse de RigRotation, avec blocage de cardan).

### Nodebox des blocs

- `BlockDocument` : boîtes en PIXELS 0..16 (JSON moteur −0,5..0,5), lues
  sous les trois formes (`"nodebox": [...]`, `{fixed}`, `"shape"`), par
  portée (un état hérite des boîtes du bloc commun), ajout / édition /
  suppression / miroir (axe) / quart de tour, le bloc devient
  `"type": "nodebox"` ; `Loaded()` : le BlockDef que le moteur chargera.
- `WorldRenderer::BlockPreviewMesh` / `BlockMaterial` : le bloc maillé par
  le MAILLEUR DES CHUNKS (ChunkMesher::BuildShape, atlas de la partie).
- Éditeur de blocs : onglet « Forme » (aperçu 3D, boîtes au glissé = une
  étape, miroirs x / z, tourner, cube plein) ; console
  `studio.block.box.{add,mirror,rotate,remove}`.

### Pièges payés

- `dataio::gltf::Primitive::Find` rend l'ACCESSEUR d'un attribut, pas sa
  position dans `attributes` — indexer `attributes` avec plantait (assertion
  du vecteur) ; le test de l'écrivain passait par coïncidence (accesseur n°
  = position n°).
- Angles depuis un quaternion : `asin` près de ±90° perd ~0,02° (89,98°
  au lieu de 90°) — `atan2(−m20, √(m21² + m22²))` ; et cas du blocage de
  cardan traité à part.
- Export glTF puis réimport sans tronc ni bras : les UV étaient faites pour
  la taille DÉCLARÉE (64×32) et l'image copiée faisait 64×64 — même piège
  que l'aperçu, ailleurs : l'export reçoit la taille réelle du skin.
- `studio.block.set graphics.texture "core:x"` : la console ôte les
  guillemets ; un mot nu qui n'est pas du JSON est désormais un texte.

### Vérification

- voxel `test_rig.cpp` (+3) : poignées (dépassement, bornage, lecture JSON),
  couches (mix, add, canal non animé intact, imposées, erreurs), glTF
  (scène / nœud / absent / partie maillée / modèle chargé par l'AssetManager).
- dataio `test_gltf.cpp` (+2) : aller-retour de l'écrivain, refus.
- voxelia : rig (+3 : poignées et lissage, couches, export puis import glTF
  avec géométrie identique, skin 64×64), blocs (+2 : nodebox, nodebox par
  état chargées par le moteur).
- Scénarios réels : table nodebox (5 boîtes, 60 triangles) appliquée et
  posée ; biped animé, lissé, poignée relevée, deux couches, export glTF
  (2 animations), réimport complet et texturé → rc=0, stderr vide.

## Étape 4 — éditeur de structures (2026-10-31)

Le moteur avait déjà presque tout (`Schematic` : capture, collage, rotation,
miroir, enveloppe, remplacement, comptes, quatre formats ; `Formula` et
`Plot` ; `Game` : remplissage, remplacement, presse-papiers, fichiers,
surlignages) : l'étape est surtout la mise en interface.

### Moteur

- `voxel::TokenizeFormula(text, variables)` : morceaux d'une formule
  (nombre, variable, fonction, constante, mot-clé, opérateur, parenthèse,
  inconnu) avec leur place — coloration de l'éditeur, sans compiler ;
  `FormulaFunctions()` pour l'aide-mémoire. Les opérateurs de deux
  caractères sont partagés avec l'analyseur.
- `WorldRenderer::StructureMeshes(schematic)` : une structure maillée comme un
  petit monde (mailleur des chunks, `BuildStructure`) ;
  `WorldRenderer::SetGhost(schematic, coin)` : fantôme translucide bleuté,
  vu des deux côtés, refait au rechargement du contenu.

### Studio

- `StructureDocument` (sans fenêtre) : la structure, ses annulations par
  instantanés (64 au plus, et au plus 4 M cellules en tout, la dernière
  toujours gardée), rotation, miroirs, enveloppe (plate selon un axe),
  remplacement (une étape seulement s'il change quelque chose), cellules,
  ancre, comptes triés ; `CheckFormula` (toutes les expressions du genre,
  erreur préfixée de l'expression fautive : « y : position 6 : … ») et
  `Plot` ; lecture avec résolution des noms (alias « structures.aliases »,
  puis « mod:nom » → un mod chargé ; restés inconnus : rendus) ; écriture
  dans le format de l'extension.
- `StructureEditor` : onglet « Structures » à gauche (bibliothèque d'échange,
  structures du monde, presse-papiers, relus toutes les 2 s), onglet
  « Structure » à droite — aperçu 3D en orbite, transformations, comptes
  (un clic : « remplacer ce bloc »), enregistrement / presse-papiers ;
  sous-onglet Formule (genre, éditeur coloré, x / y / z paramétriques, bloc,
  échelle, convention z vers le haut, enveloppe, boîte, plages, « Vérifier »,
  « Tracer », aide-mémoire) ; sous-onglet Monde (coins A / B surlignés en
  bleu, capture, remplissage all / hollow / walls / outline / keep, collage
  au bloc visé avec rotation et air posé / fusionné / seulement dans l'air,
  fantôme + boîte surlignée en jaune, annulation du dernier collage — la
  région écrasée est capturée avant).
- Commandes `studio.structure.*` : new, open (nom, `world:`, `clipboard:`,
  chemin), save, clipboard, rotate, mirror, hollow, replace, set, anchor,
  undo, redo, formula (kind, block, scale, zup, hollow, bounds, range,
  samples, x, y, z, expr), check, plot, corner, capture, fill,
  replace_region, paste, paste.options, paste.undo, ghost, info.

### Vérification

- voxelia : `StudioStructures_*` (5) — transformations et annulation,
  budget mémoire des annulations, formules (position de l'erreur,
  expression fautive nommée, boule pleine / creuse, hélice, refus au-delà
  des limites sans toucher le document), quatre formats aller-retour et
  noms Minetest résolus / signalés, capture-collage-restauration dans une
  partie ; voxel : `Formula_TokensForTheEditor`.
- Scénario studio hors écran : boîte de briques remplie (hollow) entre deux
  coins, capturée, tournée, enregistrée dans la bibliothèque ; boule d'or
  tracée (123 blocs), fantôme affiché puis collée ; formule fautive →
  « position 9 : « ) » attendu » ; boule enregistrée en .we dans le monde ;
  « maison » rouverte (aperçu, comptes) — rc=0, stderr vide.

### Non fait

Arborescence de structures (entités, références à d'autres structures,
cycles) : `Schematic` ne porte que des blocs et des rangements ; glisser une
structure dans la vue (le collage se fait au bloc visé ou aux coordonnées).

## Étape 5 — atelier de textures (2026-10-31)

### Moteur

- `voxel::SaveImageFile(image, path, jpgQuality)` : PNG, ou JPG selon
  l'extension (`sdl3::ImgSavePng` / `ImgSaveJpg`), dossiers créés.
- `AssetManager::ReloadTexture(id)` : entrée du catalogue mise à jour, image
  résidente déchargée (relue au prochain `Texture(id)`) ;
  `Game::ReloadDefinition` l'appelle pour une définition de texture.
- `TextureAtlas::Tiles()` (tuiles rangées, sans les alias des textures à
  variantes, triées) et `Occupancy()` ; `voxel::VariantIndex(p, n)` : la
  variante que le mailleur tire en `p` (le mailleur l'utilise lui-même).
- `WorldRenderer::RebuildAtlas(tuile, max)` (puissances de deux 2..2048) et
  `AtlasInfo()` : taille, tuiles, occupation, temps ; `Initialize` et
  `ReloadContent` passent par le même `BuildAtlas`.

### Studio

- `TextureDocument` (sans fenêtre) : source jusqu'à 2048 px, rendu jusqu'à
  256 px par `TextureAtlas::Resample` / `Glow` (exactement l'atlas) ;
  calques (visibilité, opacité, normal / multiply / screen / overlay, nom,
  groupe, ordre, duplication, fusion vers le bas) ; pinceau (taille — carré
  jusqu'à 2 px, disque ensuite, 3 px en croix comme Aseprite —, opacité,
  mode, symétrie H / V / HV, couleur et couleur secondaire) ; sélection
  rectangulaire qui borne toute peinture ; crayon et gomme par traits
  continus (Bresenham), ligne, rectangle et ellipse (contour ou plein),
  remplissage 4-connexe (tolérance par composante + seuil alpha : une zone
  transparente se remplit quelle que soit sa couleur), pipette (composée ou
  calque) et palette des 24 couleurs récentes, déplacement du calque ou de
  la sélection, flou 3 × 3 et netteté pondérés par l'alpha, motifs (bruit de
  valeur à octaves, dégradé, briques, damier) ; annulation PAR TRAIT
  (BeginStroke / EndStroke), 64 étapes et 256 Mo au plus ; export PNG / JPG
  (source ou rendu), import, projet en calques (`layers.json` + un PNG par
  calque).
- `TextureEditor` : onglet « Textures » à gauche (catalogue filtrable du
  contenu), onglet « Texture » à droite — outils, couleur (#rrggbbaa),
  taille, mode, symétrie, plein ; palette cliquable ; toile à grille de
  pixels (zoom molette, forme en cours de tracé, sélection) ; sous-onglets
  Calques, Aperçu (rendu 8..256 px : chaîne des mipmaps jusqu'à 1 px agrandie
  à la même taille, « éclairé par le ciel » — linéaire, ACES — à côté de la
  « lueur »), Variantes (liste, ajout copie / bruit, suppression avec
  confirmation, bloc témoin 6 × 6 où chaque case montre la variante que le
  jeu tire à cette position), Atlas (tuile, max, « Reconstruire l'atlas »,
  bilan, liste des tuiles avec leur taille), Fichier.
- Enregistrer : l'image source est écrite dans le fichier de la texture (ou
  `<mod>/textures/<nom>.png`), la texture relue, l'atlas reconstruit et tout
  remaillé. Une texture procédurale ou composée devient un fichier pour la
  partie, avec un avertissement : son JSON de mod n'est pas réécrit.
- Variantes : le jeu ne les voit que s'il n'y a PAS de « nom.png » à côté des
  « nom.N.png » ; ajouter une variante à une texture seule déplace donc
  « nom.png » en « nom.1.png » (dit dans le message), puis écrit « nom.2.png »,
  déclare la variante et la liste de la texture mère, et reconstruit l'atlas.
- Commandes `studio.texture.*` : open, new, import, export, save,
  project.save, project.open, undo, redo, tool, apply, brush (color,
  secondary, size, opacity, blend, symmetry, filled), fill.options, filter
  (blur, sharpen, clear), generate, layer (add, select, remove, duplicate,
  merge, move, opacity, blend, visible, group), select, render, atlas,
  variant.add, variant.remove, view, info. La souris sur la toile passe par
  `Studio::HandleEvent` avant tout le reste.

### Vérification

- voxelia : `StudioTextures_*` (7) — modes de fusion, symétrie HV, disque du
  pinceau, trait sans trou en une étape, sélection, gomme ; contour, ellipse
  symétrique, remplissage (zone, tolérance), pipette et palette, déplacement
  de la sélection, flou / netteté, motifs ; calques (multiply, opacité,
  visibilité, ordre, groupe, duplication, fusion qui ne change pas l'image) ;
  rendu identique à `Resample`, chaîne 16 → 1, lueur identique à `Glow`, un
  pixel qui disparaît au niveau 2 px ; fichiers PNG / JPG / projet ;
  texture d'un mod réécrite → `ReloadTexture` → atlas qui la prend ;
  `VariantIndex`. voxel : `Atlas_TilesAndOccupancy`.
- Scénario studio hors écran sur un mod temporaire : ligne, rectangle plein
  en symétrie H, ellipse, calque « Ombre » en multiply rempli dans une
  sélection (64 px = la sélection), pipette ; enregistré dans le mod, atlas
  reconstruit, blocs posés montrant la peinture ; variante ajoutée
  (peinture.png → peinture.1.png + peinture.2.png) ; atlas à 128 px max
  (2048², 1540 tuiles, 56 %, 4 s en debug) ; JPG et projet exportés —
  rc=0, stderr vide.

### Non fait

Peinture sur le modèle 3D (bonus : pipette dans la vue 3D, projection par
face) ; renommage d'une variante (suppression + ajout) ; le JSON d'une
texture procédurale ou composée n'est pas réécrit par l'enregistrement.

## Étape 6 — éditeur d'entités (2026-10-31)

### Moteur

- **Bug corrigé** (jeu compris) : sans paramètre « speed », les comportements
  intégrés prenaient pour base `MoveIntent::speed`, que `Steer` réécrit à
  chaque pas — « wander » (× 0,5) la divisait par deux à chaque pas : toute
  créature qui erre sans « speed » s'arrêtait presque aussitôt (cochons,
  vaches… immobiles) ; « flee » (× 1,4) l'emballait. La base est désormais
  `PhysicsBody::maxSpeed` (follow × 1, flee × 1,4, wander × 0,5). Test
  `Behaviors_DefaultSpeedsComeFromTheBody`.
- `EntityDef::source / sourceFile / baseDir` (comme les blocs et modèles) ;
  `ContentRegistry::BuiltinBehaviors()` ; `ScriptHost::BehaviorIds()`
  (comportements déclarés par `voxel.behavior`).

### Studio

- `EntityDocument` (DefinitionDocument) : comportements dans leur ordre
  d'exécution (ajout, retrait, déplacement, paramètres — « "wander" » devient
  un objet au premier paramètre), butin, skins ; `Validate` à blanc (copie du
  contenu) : chargement, boîte de collision (0..16), PV, masse, vitesse,
  rebond, modèle, skins, sons et jeux de sons, butin, comportements (intégrés,
  définis, déclarés par script), apparition (chance 0..1, max, biomes, blocs) ;
  `Components` : ce que SpawnFromDef donnera (Collider, PhysicsBody,
  MoveIntent, EntityKind, Health, Visual + lumière, Properties,
  SoundEmitter, Behaviors, Persistent).
- `EntityEditor` : onglet « Entités » (catalogue par genre, filtre),
  onglet « Entité » — formulaire (identité, physique, gameplay, apparence,
  lumière, sons, apparition ; un champ absent montre la valeur du moteur),
  IA (liste ordonnée, ▲▼✕, « clé valeur » par comportement), butin et skins,
  composants, JSON ; diagnostics en direct ; « Appliquer à chaud »
  (Session::ReloadDefinition, refusé s'il reste une erreur), « Enregistrer ».
  Les entités déjà apparues gardent leurs composants (copiés à l'apparition,
  leur pointeur de définition reste valide) ; l'aperçu est refait.
- **Aperçu de comportement** : arène close devant la caméra du studio (sol de
  pierre 13 × 13, murs et toit de verre, intérieur vidé — la région est
  capturée avant), l'entité seule au centre, IA active ; le studio la
  sélectionne (outliner + inspecteur en direct), centre la caméra et la suit ;
  le panneau montre ses PV et sa position. « Arrêter » la retire SANS butin
  (`ecs::DespawnRecursive`, pas `Kill`) et rend la région.
- Commandes `studio.entity.*` : open, new, set, apply, save, undo, redo,
  behavior.add / remove / move / param, drop.add / remove, preview,
  preview.stop, check.

### Vérification

- voxelia : `StudioEntities_*` (3) — document du zombie (comportements et
  paramètres, raccourci devenu objet, butin, skins, annulation) ; validation
  (le zombie du mod est propre ; boîte, PV, modèle, chance, biome, son,
  comportement et butin inconnus signalés ; contenu vivant intact ;
  définition qui ne se charge pas) ; composants du cochon ; création,
  enregistrement, rechargement à chaud (nouvelle apparition à la nouvelle
  boîte et aux nouveaux PV, l'ancienne inchangée, pointeur valide) et IA qui
  déplace la créature.
- Scénario studio : cochon à 25 PV et errance plus rapide appliqués à chaud
  (49 ms), aperçu dans l'arène suivi par la caméra, arrêt (pré rendu,
  entité retirée), comportement inconnu signalé sur le zombie — rc=0,
  stderr vide.

### Non fait

Animations `idle/walk/attack/death` éditées ici (elles sont dans l'atelier de
modèles) ; arène dans une dimension temporaire (elle est bâtie dans le monde
courant, puis rendue) ; paramètres d'IA par formulaire typé (saisie
« clé valeur »).

## Étape 7 — éditeurs de biomes, générateurs et dimensions (2026-11-01)

### Moteur

- `Game::DimensionGenerator(content, dimension)` : le générateur exact que
  `CreateWorld` construirait (réglages des mods résolus), pour n'importe quel
  contenu — `CreateWorld` s'en sert lui-même ; `Game::DimensionSeed`.
- « seedFrom » (dimension) : la graine d'une autre dimension (chaîne bornée à
  8 sauts) — la copie temporaire de l'éditeur a donc le même terrain.
- `TerrainGenerator::Maze()` → `MazeLayout` : étages (sol, plafond, graine
  « graine + 7919 × étage » — celle de MazeWall —, niveau imposé), niveaux
  déclarés, gaines (cellule, taille, côté, étages desservis).
- `BiomeDef / GeneratorDef / DimensionDef::source / sourceFile / baseDir`.

### Studio

- `WorldgenDocument` (biome, générateur, dimension) : `Scratch` (le contenu
  avec le document chargé), `Validate` à blanc (blocs des couches et des
  décorations, structures, climat 0..1, genre du générateur, biomes, couches,
  générateur et ciel d'une dimension, bornes d'apparition, gravité nulle),
  `Settings` (réglages de mod « {"setting": …} » et leur chemin),
  `PreviewDimension` (la dimension, celle du biome, ou celle qui utilise le
  générateur ; le monde ordinaire sinon).
- `BuildPreview` : un monde AUTONOME (sa copie du contenu, le générateur et
  la graine de la dimension) de N × N chunks — la tranche des étages d'un
  labyrinthe (coupé au plafond du premier : on voit les couloirs d'en haut),
  sinon ± 24 blocs autour de la surface ; la partie n'est pas touchée
  (`WorldRenderer::ChunkMeshes` maille ce monde avec l'atlas de la partie).
- `BuildDimensionGraph` : morts (« enterOnDeath »), ascenseurs
  (« elevators.destinations » et leurs « from »), portails (« portal.dimension »).
- `WorldgenEditor` : onglet « Monde » (dimensions, générateurs, biomes),
  onglet « Monde » — formulaire selon le genre (champs absents : valeur du
  moteur), aperçu 3D en orbite, sous-onglets Carte (biomes du générateur sur
  512 × 512 blocs, légende), Labyrinthe (graine de la dimension, étages,
  niveaux, gaines), Réglages (valeur figée dans ce monde, repli,
  avertissement si des chunks existent), Chaîne (graphe dessiné, dimension
  courante en jaune), JSON ; diagnostics en direct. « Appliquer à chaud » :
  rechargé, avec l'AVERTISSEMENT que les chunks déjà générés ne changent pas ;
  « Régénérer dans une dimension temporaire » (confirmation) : document
  rechargé, copie « <dimension>_studio » (seedFrom, sans enterOnDeath), le
  joueur y est envoyé ; « Revenir ».
- Commandes `studio.world.*` : open, new, set, apply, save, undo, redo,
  preview [chunks] [coupe], regenerate, return, check, graph.

### Vérification

- voxelia : `StudioWorldgen_*` (3) — documents réels, validation (références,
  genre, générateur inconnu, définition qui ne se charge pas), réglages du
  labyrinthe (« floors_level_0 » et son repli) ; aperçu autonome (la partie ne
  charge rien de plus, même générateur), biome modifié (surface en or) visible
  dans l'aperçu seulement, labyrinthe coupé (rien au-dessus), graines des
  étages, gaine ; graphe : mort → niveau 0, ascenseurs 0 → 2 → 4 → 5.
- Scénario studio : plaines dorées dans l'aperçu (forêts voisines intactes),
  labyrinthe du niveau 0 vu d'en haut avec sa gaine (18 chunks, 2,2 s en
  debug), graphe (13 liens), rechargement à chaud averti, régénération du
  niveau 2 dans « backrooms:level_2_studio » puis retour — rc=0, stderr vide.

### Non fait

Édition point à point de la carte des biomes (le générateur « noise » les
choisit par climat : rien à peindre) ; portails créés à la volée et test
« traverser » ; gaines dessinées dans la vue 3D (listées) ; la dimension
temporaire reste dans la sauvegarde du monde.

## Catalogue des blocs sur son fil (2026-11-01)

- Le catalogue de l'éditeur de blocs attend que la partie soit prête (tous
  les mods chargés : `Session::Ready`) — jusque-là un spinner et « chargement
  des mods… » ; il n'affiche plus « aucune partie » pour toujours quand le
  studio s'ouvre avant la fin du chargement.
- La recherche (parcours des blocs, filtre, libellés) tourne sur le fil
  « studio-catalog » (`Worker`) ; le résultat est posé dans l'interface au
  fil principal ; un spinner tourne pendant la recherche. Une demande plus
  récente (filtre retapé) rend caduc un résultat en vol (numéro de demande).
- Le catalogue repart quand le contenu change, d'où qu'il vienne :
  `voxel::Game::ContentGeneration()` avance à chaque `LoadMods` et à chaque
  `ReloadDefinition` réussi.
- Sûreté : le fil LIT le ContentRegistry ; la partie appelle ses « gardes »
  (`Game::AddContentGuard`) AVANT tout changement du contenu, et celle du
  catalogue attend la fin de la recherche (pointeur faible : inerte si
  l'éditeur a disparu ; retirée à sa destruction). Limite : un script qui
  enregistre une définition pendant la partie (`voxel.register`) passe
  par ContentRegistry::Load sans garde — c'est le cas au chargement des
  mods, pas en jeu.
- Commandes : `studio.block.filter [texte]`, `studio.tab left|right <n>`
  (onglets des docks, pour les scénarios).
- Vérifié : `Content_GuardsRunBeforeEveryChangeAndGenerationAdvances` (garde
  appelée avant chargement et rechargement, génération qui avance, pas pour
  un refus, garde retirée) ; scénario (spinner pendant le chargement,
  catalogue une fois prêt, filtre « verre » — 1,8 ms sur le fil) ; scénario
  sous ThreadSanitizer (3 filtres, rechargement à chaud d'un bloc pendant que
  le catalogue est servi) : AUCUNE course de données. TSan y signale
  seulement une inversion d'ordre de verrous INTERNE au pilote Vulkan de
  SDL3 (`VULKAN_Submit` / `VULKAN_INTERNAL_RemoveMemoryUsedRegion`, sous
  `WindowManager::Present`, fil principal seul : sans risque réel), étrangère
  au catalogue.

## Interface à la sandbox et étape 8 — World Inspector, console (2026-11-01)

### Module partagé `editor` (libs/infrastructure/editor)

Le cadre d'éditeur de la sandbox (palette dérivée du thème, feuille
`.editor-*` / `.dock-*`, `DockPanel`, boutons d'icône, champ de recherche,
poignées de redimensionnement) vit désormais dans un module commun. La
sandbox s'en sert (`kit::DockPanel` en hérite, `kit::Palette` / `EditorCss`
/ `IconButton` / `SearchField` / `ResizeHandle` y renvoient). Le module
ajoute ce qu'il faut pour éditer en 3D :

- `OrbitNavigator` : orbite autour d'un point (celui sous le curseur),
  panoramique qui garde ce point sous le curseur, zoom vers le curseur,
  cadrage d'une boîte, vues normalisées (face, dessus, iso…), vol, `LookFrom` ;
- `TransformGizmo` : déplacer (axes, plans, écran), tourner (anneaux),
  échelle (axes, uniforme) à taille constante à l'écran. Un glissé rend la
  transformation TOTALE depuis le début du geste, avec des accroches (pas,
  15°, ¼). `Strokes` donne le tracé en pixels ;
- `CommandPalette` (recherche floue, récents d'abord) et `CommandPaletteView`
  (modale ↑/↓/Entrée/Échap), idée reprise d'IngeTrazo.

Les 11 tests passent sans fenêtre, sanitizers compris. Ils pilotent la
navigation et le gizmo en pixels.

### Studio

Même architecture que la sandbox :
- `ui::WindowFrame` (`WindowFrameOptions`) : barre de titre et barre d'état
  personnalisées, fenêtre `WINDOW_FLAGS` sans décoration ;
- menus Fichier / Édition / Affichage / Simulation / Outils / Aide ;
- docks : Outliner | Éléments à gauche, Vue 3D au centre, catalogues |
  Console en bas, Inspecteur | éditeurs à droite.

- Vue 3D :
  - c'est une surface de l'application (`ui::AppSurface`) ;
  - barre d'outils : simulation, poursuite, gizmo et accroche, cadrage, vues ;
  - repère d'axes et gizmo dessinés sur un calque.
  - Souris :
    - clic gauche : sélectionne l'entité la plus proche (`RaycastEntities`), sinon le bloc (`RaycastBlocks`), ou saisit le gizmo ;
    - double-clic : cadre la sélection ;
    - milieu : orbite autour du point visé, Maj pour le panoramique ;
    - molette : zoom vers le curseur ;
    - droit maintenu : vol, comme avant.
  - Raccourcis : W/E/R pour le mode du gizmo, F pour cadrer.
  - Le gizmo déplace et oriente l'entité (cap, tangage) et en règle l'échelle visuelle.
- « Éléments » : le navigateur du contenu, façon MCreator.
  - Par genre (blocs, modèles, structures, textures, entités, biomes, générateurs, dimensions) : un nombre, l'ouverture du catalogue et un bouton +.
  - Le bouton + ouvre l'assistant `studio.element.new <genre> <id> [mod]`.
- Palette de commandes (F1, Ctrl+Maj+P) : toutes les commandes enregistrées.
- Commandes et raccourcis génériques :
  - `studio.undo` / `redo` / `save` / `apply` (Ctrl+Z, Y, S, Maj+R) agissent sur le document de la page active du dock de droite ;
  - `studio.view`, `studio.gizmo`, `studio.palette` ;
  - `studio.tab left|right|catalog` : `left n` avec n ≥ 1 vise toujours les catalogues, pour ne pas casser les scénarios.

### Étape 8

- World Inspector (§10) :
  - `InspectorRow::field` marque une valeur modifiable. Elle s'affiche en champ ; Entrée lance `studio.inspect.set <champ> <valeur>`, donc la modification passe par la console et le rapport.
  - Le texte affiché se relit tel quel (`ParseNumbers` lit « 1.5, 64, -3 », « 45° », « 8 / 10 »).
  - `ApplyEntityEdit` :
    - position (sans interpolation, vitesse remise à zéro), vitesse, cap, tangage ;
    - PV et PV max, faim, saturation, endurance, air, expérience ;
    - gravité (signe = sens), masse, frottement, rebond, vitesse max, vol, noclip ;
    - échelle, visibilité ;
    - cible d'IA forcée ou retirée, effet (`effect poison 5 2`).
  - Blocs :
    - `InspectBlock` donne l'identité, l'état, les états possibles, la bascule, l'action et le contenu du rangement ;
    - `ApplyBlockEdit` gère `state` (`SetBlockState`), `use` (clic droit, comme en jeu) et `toggle` ;
    - boutons Utiliser / Basculer ;
    - `studio.inspect.block x y z` sélectionne un bloc.
  - Outliner : filtre par genre (liste déroulante), « vivants », distance maximale (curseur).
- Console (§12) :
  - filtres du journal (erreurs, avertissements, résultats, saisies) ;
  - marque-pages : un clic marque une ligne, qui reste visible malgré les filtres et suit sa ligne quand le journal déborde ;
  - complétion par Tab (préfixe commun, candidats listés) ;
  - `ConsoleModel::Commands()` alimente la palette.
- Tests : `StudioInspector_ParseNumbersAcceptsDisplayedFormats`, `…_EditsEntitiesOnTheFly`, `…_InspectsAndUsesBlocks`, `StudioConsole_FiltersAndBookmarks`, sur une vraie partie.

### Non fait

- Rechargements `reload all | texture | script` : étape 9.
- Écran de rangement du jeu ouvert depuis l'inspecteur : le contenu est listé, pas éditable.
- Inspecteur dans une fenêtre séparée.
- Classes de script portées par une entité.
- La sandbox utilise le cadre commun, mais garde sa navigation et son gizmo (dans `Runtime`). Le module `editor` est prêt pour qu'elle les adopte.

## Fenêtre de jeu encadrée, studio depuis les menus (2026-11-01)

La fenêtre de jeu a désormais, comme celle du studio, un `ui::WindowFrame`
(`WindowFrameOptions`).
- **Cadre** :
  - `WindowSpec::flags = WindowFrame::WINDOW_FLAGS` (fenêtre sans décoration) ;
  - barre de titre personnalisée : déplacer, agrandir, fermer, ce qui quitte le jeu ;
  - barre d'état : monde, dimension, img/s, « F10 : Voxelia Studio » ; au menu, la version.
- **Zone de jeu** : les couches du `Shell` (vue 3D, HUD, écrans, notifications) se superposent dans la pile `#game-area`, contenu du cadre ; sans fenêtre, elles restent sous la racine.
- **Plein écran** : les deux barres sont masquées.
- **Studio depuis les menus** :
  - « Ouvrir le studio » dans le menu principal et le menu pause ;
  - `Shell::OpenStudio()` déclenche `onOpenStudio`, branché par l'application ;
  - le studio s'ouvre, ou revient au premier plan s'il est déjà ouvert ;
  - F10 garde son effet de bascule.
- **Vérification** : un clic injecté sur chacun des deux boutons ouvre le studio (rapport : `studio.open`, `windows.studio`), avec captures du menu, du jeu et de la pause.

## Performances : le jeu et le studio (2026-11-01)

Mesures en hors écran, sur Intel Iris Xe (GPU intégré partagé avec le bureau : ±15 % d'une exécution à l'autre). Le rapport de fin de partie donne maintenant, par fenêtre, styles et mise en page (avec le nombre de widgets restylés et de passes par image), vues 3D et dessin.

| cas | avant | après |
|---|---|---|
| debug, monde plat + studio | ~5 img/s | 108 img/s |
| release, monde plat + studio | 92 img/s | 210 img/s |
| release, overworld, jeu seul | 131 img/s | 150 img/s |
| release, overworld + studio | 81 img/s | 95–120 img/s |
| debug, overworld + studio | ~4 img/s | 87 img/s |
| chargement d'un monde (debug) | ~1000 images | ~120 images |

### Corrections, par module

- **ecs** : identifiant DENSE par type de composant, et colonnes des archétypes indexées par lui. `std::type_index` comparait les noms de types (`strcmp`, 20 % du temps) et les hachait (8 %) à chaque `GetComponent`. La table entité → emplacement est indexée par `Entity::id`, et `typeToArchetypes` par l'identifiant dense.
- **ui, styles** :
  - index des sélecteurs par clé du sujet (id, classe, type, universel) : un élément ne teste plus toutes les règles ;
  - les données de l'élément sont calculées une seule fois, avec son rang transmis par le parent. Avant, `DataOf` cherchait ce rang parmi les frères POUR CHAQUE sélecteur, ce qui était quadratique sur une longue liste.
- **ui, mise en page** :
  - cache des mesures de texte d'une passe à l'autre ;
  - changement de texte LOCAL : un texte dont la taille mesurée ne change pas ne relance plus une passe complète (`MarkTextDirty`, `Rect::text`) ;
  - les sous-arbres entièrement rognés ne sont pas dessinés.
- **ui, fenêtres et vues** :
  - `WindowSpec::maxFps` : fenêtre secondaire dessinée et présentée au plus N fois par seconde ;
  - `Viewport3DState::maxFps` : idem pour une vue 3D.
- **render3d** (rendu hors écran) : le pipeline n'est plus re-lié ni recherché quand il ne change pas, et la matrice des normales d'une simple translation n'est plus inversée.
- **voxel** :
  - `BlockCursor` : lecture de blocs en série, une recherche de chunk par changement de chunk et non par bloc ; boîtes de collision de la physique ;
  - `Game::StreamChunks(budget)`.
- **voxelia** :
  - chargement des chunks à budget de TEMPS (~20 ms par image) au lieu de 8 chunks par image ;
  - fils de maillage selon la machine (cœurs logiques / 2 − 1, de 2 à 6) ;
  - barre d'état du studio à 4 Hz ;
  - studio à 60 images/s (fenêtre et vue 3D) ; le jeu garde toute la cadence.

### Ce qui limite encore

- La présentation (attente du GPU intégré, 2,5 à 5 ms).
- Le rendu du monde DEUX fois (jeu et studio).
- En debug, le code non optimisé. Pour mesurer les performances, utiliser `cpp-maker run -m voxelia --mode release`.

## Reste à faire (étapes 9+ du prompt)

Mod Manager et rechargement global (étape 9), scénarios nommés `--scenario=`
(étape 10) ; peinture sur modèle 3D, poignées de Bézier glissées, animations
glTF importées, `.glb`, arborescence de structures, navigateur de
ressources, vérification `--seed`.

`script_bindings` (et donc `sandbox`), cassé à HEAD et dans l'index, a été
réparé le 2026-10-31 :
include `render3d/shape.hpp` manquant, `Option` dans un ternaire, étoile en
trop sur une `sql::Row`, bindings `dataio` rétablis (`modeling`/`net`
référencés mais jamais implémentés : retirés), dépendances déclarées ;
**UB** dans `dataio.mesh.save` avec `colors` (`delete` d'un objet de la pile
+ fuite) ; **régression** de `ColorOf` : l'alpha par défaut était divisé
par 255 avec des composantes 0-255 (`[0, 255, 0]` presque transparent) ;
message `[x, y, z]` des options de position rétabli ; `script_color.hpp`
rendu autonome ; `test_dataio_script.cpp` (exemple non compilable) changé en
vrai test.
