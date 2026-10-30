# Le projet comme système de fichiers — sandbox

Depuis le 2026-10-12, chaque élément d'un projet de l'éditeur a une **adresse
typée** et un **type C++ à lui**. Une scène, un objet, un script, une ressource
et un nœud ne se confondent plus, ni dans le code ni dans l'interface.

## Le problème

Avant, presque tout était un `SceneDesc` dans `Project::scenes`, distingué par
un champ `kind`. Le mot « objet » désignait deux choses :
- **un nœud** : `ObjectDesc`, `Runtime::FindObject`, `ObjectCount`, l'espace
  de script `object.*` ;
- **un objet réutilisable** (`.object`).

Concrètement :
- `Project::FindScene(nom)` pouvait rendre un objet ;
- `Project::activeScene` pouvait contenir le nom d'un objet, et partait ainsi
  dans le manifeste ;
- dupliquer un objet depuis le navigateur échouait, car l'opération passait
  par `DuplicateScene` ;
- une scène et un objet de même nom pouvaient faire refuser une instance comme
  un « cycle ».

## Les adresses (`document/project_fs.hpp`)

```
/                                racine du projet
/scenes, /objects, /scripts      dossiers
/scenes/Donjon                   une SCÈNE
/objects/Torche                  un OBJET (peut porter le nom d'une scène)
/scripts/player                  un script de bibliothèque
/scripts/@Donjon                 le script de JEU de la scène Donjon
/assets/models/k.gltf            une RESSOURCE (disque)
/scenes/Donjon/Joueur/Vue        un NŒUD, dans SON document
/objects/Torche/Flamme           un nœud d'objet
```

| Type | Rôle |
|---|---|
| `ProjectPath` | Une adresse : `Kind` (ROOT, *_FOLDER, SCENE, OBJECT, SCRIPT, GAMEPLAY_SCRIPT, ASSET, SCENE_NODE, OBJECT_NODE), `Parse`/`ToString` (aller-retour exact), `Document()`, `Parent()`, prédicats. |
| `SceneRef`, `ObjectRef`, `ScriptFileRef`, `AssetRef`, `NodeRef` | Types forts : passer une scène là où un objet est attendu ne compile pas. `NodeRef` porte son document et son identifiant. |
| `ProjectFs` (`project.Fs()`) | Vue « système de fichiers » : `ListScenes`/`ListObjects`/`ListScripts`/`List(path)`, `Exists`, `Resolve` (nom nu : scène, puis objet, puis script), `FindScene`/`FindObject`/`FindDocument`/`FindNode`, `AddScene`/`AddObject`/`Remove`/`Documents()`, noms uniques par dossier, `WhyInvalidName`, `WhySelfInstance`, ressources sur disque (`AssetDiskPath`/`AssetDisplayPath`). |

Le stockage est `Project::documents` (scènes **et** objets). C'est un détail de
la couche document : le moteur, l'éditeur et l'application passent par
`Fs()`. Les anciens `Project::FindScene/FindObject/SceneNames/ObjectNames/
SceneCount` sont `[[deprecated]]`. Avec `-Werror`, plus aucun code ne les
appelle.

## Document ouvert

- `Project::activeScene` est **toujours une scène** : c'est elle qu'on enregistre
  et qu'une partie lance.
- `Project::editingObject` est l'objet ouvert par-dessus. Il n'est pas
  enregistré : un projet se rouvre toujours sur une scène.
- `ActiveDocument()` donne le document édité, `ActiveScene()` la scène, et
  `ActivePath()` vaut `/scenes/X` ou `/objects/X`.

## Le vocabulaire « nœud » / « objet »

| Avant | Après |
|---|---|
| `ObjectDesc` | `NodeDesc` |
| `Runtime::FindObject` / `RemoveObject` / `RenameObject` / `SpawnObject` | `FindNode` / `RemoveNode` / `RenameNode` / `SpawnNamedNode` |
| `Runtime::FindNode` (rendait un `Object3D`) | `FindObject3D` |
| `SelectedObject`, `onObjectChanged`, `SceneObjectRef`, `ObjectCount`, `Objects()` | `SelectedNode`, `onNodeChanged`, `SceneNodeRef`, `NodeCount`, `Nodes()` |
| `Runtime::ActiveScene()` (rendait aussi un objet) | `ActiveDocument()` ; `ActiveScene()` = la scène seule |
| `Runtime::ProjectPath()` (le manifeste) | `ManifestPath()` |
| `SwitchScene(nom)` | `OpenScene(SceneRef)`, `OpenObject(ObjectRef)`, `OpenDocument(ProjectPath)` (un chemin de nœud ouvre son document et sélectionne le nœud) |
| `RenameScene` / `RemoveScene` / `DuplicateScene` | `RenameDocument` / `RemoveDocument` / `DuplicateDocument(ProjectPath)` |
| `AddEmptyScene`, `AddEmptyObject`, `ImportSceneFile`, `CreateObjectFromNode`… (rendaient un nom) | rendent `SceneRef` / `ObjectRef` |
| `InstantiateObject(nom)`, `DefineObjectFromNode(nom)`, `RequestScene(nom)`, `LoadSceneInPlay(nom)`, `SetGameplayScript(nom)` | prennent `ObjectRef` / `SceneRef` |
| historique d'annulation : nom de scène | `ProjectPath document` |
| `objects::ExpandInstance(…, String owner)` | `…, ProjectPath owner` (seul un OBJET peut faire un cycle) |

Les termes « objet de script » (`OwnerObject`, `MapObject`, `ScriptObjectInfo`)
et `render3d::Object3D` gardent leur sens : ce ne sont pas des nœuds.

## Scripts

- Les nœuds se manipulent par **`node.*`** : `node.position`,
  `node.set_position`, `rotation`, `scale`, `set_color`, `set_visible`,
  `set_material`, `set_physics`, `velocity`, `set_velocity`, `impulse`,
  `distance` (en plus de `prop`, `path`, `children`…).
- **`object.*`** reste un alias obsolète. Il fonctionne encore et écrit un
  avertissement dans la console, une fois par fonction.
- Les projets fournis (Projet_1, données de test), les scénarios et la
  documentation sont migrés.
- `load("Donjon")` et `game.load_scene("/scenes/Donjon")` acceptent un nom ou un
  chemin. `ObjectAsset` accepte `"Torche"`, `"/objects/Torche"` ou
  `"objects/Torche.object"`.
- Console d'outils :
  - `editor.scenes()` et `editor.objects()` ;
  - `editor.list("/scenes/Donjon/Niveau")` liste les enfants ;
  - `editor.kind("/objects/Torche")` répond « Objet » ;
  - `editor.open_document("/scenes/X/Joueur")` ouvre et sélectionne
    (`editor.open` ouvre toujours un projet).
- `game.scenes()` ne liste plus les objets.

## Dans l'éditeur

- **Explorateur** (ancien « Ressources ») :
  - emplacements `/`, `/scenes`, `/objects`, `/scripts`, et le disque pour les
    ressources ;
  - chaque entrée porte son `ProjectPath`, et les opérations (renommer,
    supprimer, dupliquer) en dépendent ;
  - une scène ou un objet se parcourt comme un dossier : arbre, fil d'Ariane,
    menu « Parcourir les nœuds » ;
  - double-cliquer un nœud ouvre son document et le sélectionne ;
  - l'infobulle et le pied indiquent nature et chemin, par exemple
    « Nœud (objet) /objects/carrefour/Mur est (droite) » ;
  - un nœud ne se renomme et ne se supprime que dans l'arbre de son document.
- **Inspecteur** : lignes « Chemin » (`/scenes/Donjon/Joueur`) et « Nature ».
- **Barre d'état** : « Projet · /objects/Torche ».
- **Sélecteur de document** : sous-menus Scènes et Objets, qui ouvrent par
  `OpenScene` ou `OpenObject` selon le sous-menu.
- **Ligne de commande** : `--scene=` accepte un nom ou un chemin
  (`--scene=/objects/Torche`).
- **Rapport JSON** : `scene_count` ne compte plus que les scènes, et
  `reusable_objects`, `node_count`, `active_document` sont ajoutés.

## Vérifications

`tests/game_editor_project_fs_smoke_test.cpp` (10 tests) couvre :
- les chemins (analyse, aller-retour, navigation) ;
- les listings sans mélange ;
- l'existence et la résolution ;
- les nœuds dans leur document ;
- les créations à nom unique et les noms refusés ;
- les ressources sur disque ;
- le Runtime : homonymes scène/objet, objet ouvert, chemin de nœud, renommage
  et retrait typés ;
- la console d'outils ;
- l'alias `object.*` ;
- l'explorateur : entrées typées, parcours des nœuds, duplication d'un objet.

Les suites existantes de l'éditeur (2d, input, objects, pieces,
project_files, smoke : 132 tests) passent toutes.
