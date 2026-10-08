# Objets réutilisables (`.object`) — game_editor_demo

Depuis le 2026-10-06, un projet de l'éditeur contient deux genres de
documents :

- les **scènes** (`scenes/<Nom>.scene`) : l'élément FINAL — arbre de nœuds,
  réglages du monde (gravité, soleil, ambiance, ciel), caméra, calque 2D de
  l'interface, script de jeu ;
- les **objets** (`objects/<Nom>.object`) : une arborescence de nœuds
  réutilisable, sans monde, sans caméra, sans calque 2D ni script de jeu.

Un objet s'édite seul (il s'ouvre comme une scène : arbre, inspecteur, vue
3D). On le pose dans des scènes — ou dans d'autres objets — sous forme
d'**instances** : un nœud qui porte le composant `ObjectInstance { source =
nom de l'objet }` et son transform (position, rotation, taille). Le contenu
d'une instance n'est **jamais enregistré** : il est régénéré à partir de
l'objet. Modifier l'objet modifie donc toutes ses instances, dans toutes les
scènes, sans rien dupliquer sur le disque.

## Fonctionnement

| Fichier | Rôle |
|---|---|
| `document/objects.hpp` | `IsInstance`, `SourceOf`, `IsGenerated`, `MakeInstance`, `EditableAncestor`, `StripGenerated` (ce qui s'enregistre), `ExpandInstance(s)`, `ExpandProject` (objets dans l'ordre des dépendances, puis scènes), `DependsOn`, `RenameReferences`. |
| `document/project.hpp` | `SceneKind { SCENE, OBJECT }` sur `SceneDesc` ; `Project::FindObject`, `ObjectNames`, `SceneCount` ; `SceneNames` ne liste plus que les scènes. |
| `document/project_files.hpp` | Format `game_editor.object` v1 ; manifeste : liste `objects` ; dossier `objects/` ; développement des instances au chargement. |
| `engine/runtime.hpp` | `IsEditingObject`, `RefreshObjectInstances` (à chaque changement de document), `AddEmptyObject`, `CreateObjectFromNode`, `InstantiateObject`, `DetachInstance`, `DefineObjectFromNode`. Renommer un objet renomme ses références ; le supprimer laisse ses instances vides (référence et transform conservés). |
| `engine/script_owners.cpp` | Base de script `ObjectAsset`. |

- **Contenu généré** : copie des enfants de la racine de l'objet, identifiants
  neufs, références internes re-câblées, propriété `object_generated`. Une
  instance imbriquée (objet dans un objet) est développée avec l'objet qui la
  contient.
- **Cycles** : un objet qui se contiendrait (A → B → A) est refusé à
  l'instanciation et signalé au développement (l'instance reste vide).
- **Édition** : hors partie, sélectionner un nœud généré sélectionne
  l'instance qui le porte ; supprimer, renommer, déplacer ou dupliquer un nœud
  généré est refusé (« modifiez l'objet lui-même »). En partie, les scripts
  font ce qu'ils veulent (tuer un monstre qui fait partie d'une instance).
- **Partie** : `load("Objet")` est refusé — un objet s'instancie, il ne se
  joue pas comme une scène.

## Dans l'éditeur

- **Panneau de gauche, trois onglets** : « Arbre de scène » (les scènes en
  tête, puis l'arbre du document ouvert), « Scènes » (les scènes seules) et
  « Objets » (les objets réutilisables : clic = ouvrir ; boutons Nouvel
  objet, Créer un objet à partir de la sélection, et sur chaque ligne Poser
  une instance sous la sélection du document ouvert).
- **Menu Objets** : Nouvel objet, Créer un objet à partir de la sélection (le
  nœud devient une instance au même endroit), Rendre l'instance
  indépendante, Actualiser les instances, Poser une instance ▸, Ouvrir un
  objet ▸.
- **Arbre** : une instance porte l'icône d'objet et « (Objet : Nom) » ; son
  contenu est grisé, ni déplaçable ni cible de dépôt ; menu contextuel :
  Créer un objet à partir du nœud, Ouvrir l'objet, Rendre indépendant.
- **Inspecteur** : section « Instance d'objet » (objet, nombre de nœuds
  générés, Ouvrir l'objet, Rendre indépendant) ; l'onglet Monde d'un objet
  n'a pas de réglages (ils appartiennent aux scènes).
- **Navigateur de ressources** : dossier « Objets » ; double-clic = ouvrir ;
  menu « Poser une instance » ; déposer la vignette d'un objet dans la vue 3D
  y pose une instance.
- **Sélecteur de document** de la vue : une liste à deux sous-menus,
  « Scènes (n) ▸ » et « Objets (n) ▸ » (combo par catégories du module ui),
  tenue à jour quand on ajoute, renomme ou supprime une scène ou un objet.

## Scripts

```
class Lampes extends ObjectAsset {}

class Salle extends Scene {
    let lampes = nil
    fn on_start() {
        this.lampes = Lampes("Lampe")             # ou "objects/Lampe.object"
        this.lampes.instantiate({pos: [0, 3, 0], rot: [0, 90, 0], scale: [1, 1, 1]})
        # Définir (ou redéfinir) un objet à partir de nœuds construits en jeu :
        scene.spawn({name: "Abat-jour", shape: "cone"})
        Lampes("Abat-jour seul").define("Abat-jour")
    }
}
```

`instantiate(opts)` rend le nom (ou chemin) de l'instance ; `instances()`,
`clear()` (retire les instances posées par cet objet de script ; aussi à sa
destruction), `exists()`, `define(nœud)`, propriété `name`.

## Exemple : le donjon de Projet_1

`saves/game_editor_demo/projects/Projet_1` (« Les Cryptes de Script ») est
bâti sur des objets — 30 dans `objects/` ; aucune scène ne recopie leur JSON
(« Atelier des pièces » : 26 instances, 17 Ko au lieu de 885 Ko) :

| Objets | Rôle |
|---|---|
| `torche`, `mur_porte`, `plafond`, `dallage` | Blocs communs, posés en instances DANS les pièces (objets imbriqués) : la torche (29 copies auparavant), le côté de salle percé d'une porte (posé tourné de 0/90/180/270°), le plafond et le sol des salles. La flamme d'une torche sans `seed` prend sa phase de sa position. |
| `entree`, `sortie`, `couloir_*`, `carrefour`, `cul_de_sac`, `salle_*`, `crypte`, `porte_muree` | Pièces de 8 m, portes de 3 m centrées sur les côtés. |
| `escalier_descente` / `escalier_arrivee`, `puits_echelle` / `echelle_arrivee` | Passages entre étages : même case, même rotation, l'un au-dessus de l'autre (étage k à y = −7 k). Nœud « Passage » (scripts `escalier`, `echelle`) et repère « Arrivée ». |
| `egout_droit`, `egout_angle`, `egout_t`, `egout_carrefour`, `egout_cul_de_sac`, `egout_salle` | Égouts : trottoirs, garde-corps, canal dont l'eau s'écoule (script `eau-courante` : écume qui file, cascade, tourbillon). |
| `squelette`, `zombie`, `araignee`, `pirana` | Monstres animés : nœud « Corps » + script de l'espèce (`scripts/mobs`, base commune `Monstre` du module `monstres`). |

- **Génération** : plusieurs étages (Facile 2 × 3×3, Normal 3 × 4×3,
  Difficile 3 × 4×4) ; chaque étage commence à la case où descend le
  précédent ; le deuxième est celui des égouts ; la sortie est au dernier.
- **Donjon** : pose les pièces et les monstres avec des `ObjectAsset`,
  publie la carte dans `game.state().donjon`, relie les passages (propriété
  `vers`) et joue les traversées demandées (`demande_transit` : fondu, le
  joueur réapparaît à l'« Arrivée » de l'autre étage). Un coup d'épée
  incrémente la propriété `coups` du « Corps » ; le monstre clignote, meurt
  à 0 PV, s'effondre et retire son instance ; ses coups et sa mort reviennent
  par `degats_joueur` / `morts`.
- **Monstres** : poursuite de case en case (parcours en largeur sur les
  ouvertures, glissement le long des murs) ; l'araignée bondit, le zombie est
  lent et robuste, le piranha nage dans le canal et bondit pour mordre (on ne
  le touche que hors de l'eau).
- **Scénarios fixes** : « Donjon-étage-supérieur » (graine 2026, Facile) et
  « Égouts » (graine 2027, Normal, départ à l'étage 2) ; « Atelier des
  pièces » montre une instance de chaque pièce et de chaque monstre.

## Vérifications

- `tests/game_editor_objects_smoke_test.cpp` (7 tests) : imbrication et ordre
  des dépendances, redéveloppement sans duplication, enregistrement des seules
  références, propagation d'une modification profonde, cycles et objets
  manquants, renommage, nœud éditable, aller-retour des fichiers, runtime
  (ouvrir/modifier un objet puis revenir à la scène, sélection redirigée,
  instanciation avec transform, refus d'un cycle, renommage suivi),
  conversion d'un nœud en objet, détachement, `ObjectAsset` en partie.
- Dans l'éditeur réel (Xvfb), sur une copie de Projet_1 : sélection →
  « Créer un objet à partir de la sélection » → l'objet s'ouvre, on y ajoute
  une sphère → retour à la scène : la sphère y est ; Enregistrer : le
  manifeste liste l'objet, la scène ne garde que la référence.
