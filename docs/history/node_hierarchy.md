---
name: node-hierarchy
description: "Module scene:: — hiérarchie de nœuds générique (Node/NodeId/NodePath/NodeTree/PackedScene) : architecture, décisions, limites"
metadata:
  node_type: memory
  type: project
---

# Hiérarchie de nœuds — module `scene::`

Fondation du moteur, pas une fonctionnalité de l'éditeur : `lib/include/scene/`
ne dépend **ni de render3d, ni de physics, ni de ui**. L'éditeur de niveau s'en
sert ; le rendu et la physique sont *pilotés* depuis l'arbre, jamais l'inverse.

## 1. Où ça se place

```
                 Éditeur (panneaux, manipulateur, undo)
                              │
                              ▼
                  Document  =  scene::NodeTree
                              │
                              ▼
                        Runtime (level_editor::Runtime)
                 ┌────────────┼────────────┐
                 ▼            ▼            ▼
                ECS        Physics      Render3D
```

Le document reste la source de vérité, exactement comme avant ce chantier
(cf. `memory/project_level_editor_app.md`) : toute mutation écrit d'abord dans
l'arbre de nœuds, puis se répercute sur la scène vivante.

À ne pas confondre : `ui::Scene` = une PAGE d'interface ; `render3d::Object3D` =
le graphe de RENDU ; `scene::NodeTree` = la hiérarchie de DOCUMENT.

## 2. Fichiers

| Fichier | Contenu |
|---|---|
| `scene/property.hpp` | `NodeId`, `PropertyType`, `PropertyValue`, `PropertyMap` |
| `scene/node.hpp` | `Transform`, `ReparentMode`, `Component`, `Node`, `node_type::` |
| `scene/tree.hpp` | `NodePath`, `ValidationReport`, `NodeTree` |
| `scene/type_registry.hpp` | `NodeTypeInfo`, `NodeTypeRegistry` |
| `scene/packed.hpp` | `PackedScene` (scène réutilisable) + `component_type::SCENE_INSTANCE` |
| `scene/scene.hpp` | en-tête parapluie |

Ajouts à `math/math.hpp` faits pour ce module : `FQuaternion::FromMatrix`,
`math::TRS` et `math::DecomposeTRS` (l'inverse de `ComposeTRS`, qui manquait).

## 3. Décisions d'architecture, et leur raison

**Le type d'un nœud est une chaîne, pas une classe.** Pas de
`class MeshInstance : public Node3D`. Une hiérarchie d'héritage ajouterait un
deuxième modèle d'objets : non copiable trivialement (donc pas d'instantané
pour l'annulation), non sérialisable sans table de types maison, et fermé —
l'utilisateur ne pourrait pas déclarer son propre type sans recompiler. Avec un
type-chaîne + composants, **un type inconnu se charge, se sauvegarde et
s'affiche quand même** (vérifié par un test). `NodeTypeRegistry` dit ce que
chaque chaîne signifie (libellé, icône, composants par défaut) ; il est
facultatif, `NodeTree` marche sans lui.

**Le nœud est une valeur, la parenté est un identifiant.** Aucun pointeur vers
le parent ou les enfants : `NodeId`. C'est ce qui rend l'arbre copiable en une
ligne (instantané d'annulation, duplication) et comparable champ à champ dans
les tests.

**`NodeId` = (indice, génération)**, même forme qu'`ecs::Entity`. Un identifiant
conservé après suppression est *détecté* comme périmé au lieu de désigner le
nœud qui a repris l'emplacement — test dédié. Le **nom n'est jamais une
identité** : il est éditorial, et l'unicité n'est imposée qu'entre frères (c'est
tout ce dont un chemin a besoin).

**Propriétés typées plutôt que `data::Node` brut.** `data::` reste le format de
FICHIER (aucune deuxième implémentation de JSON), mais deux choses ne se disent
pas dans un arbre non typé : une **référence de nœud**, qui doit être
reconnaissable pour être re-câblée à la duplication, et le **sens** d'un vecteur
ou d'une couleur, dont l'inspecteur a besoin pour choisir son widget.

**Transform : invalidation en O(1), recalcul en O(profondeur).** Écrire un
transform ne parcourt pas le sous-arbre. Chaque nœud retient une « époque » qui
change quand son monde change, et l'époque de son parent au moment du dernier
calcul : un désaccord suffit à savoir qu'il faut recalculer. Mesuré : 100
déplacements de la racine d'un arbre de 100 000 nœuds = **0,02 ms**.

**Reparentage : `KEEP_LOCAL` ou `KEEP_GLOBAL`.** Le second recalcule le local
pour que le nœud ne bouge pas à l'écran — c'est ce que veut un glisser-déposer
d'outliner. Un reparentage qui créerait un **cycle est refusé sans rien
modifier** (un arbre qui n'en est plus un casse tout parcours).

**Duplication : les références internes suivent, les externes restent.** Une
copie de voiture vise sa propre caméra, mais garde le même circuit comme cible.
Même règle à l'instanciation d'une `PackedScene`, à ceci près qu'une référence
*sortante* y est neutralisée plutôt que laissée pendouillante (elle désignerait
un nœud absent de la scène emballée).

**Instancier copie, ça ne crée pas de lien vivant.** Un lien vivant impose de
savoir, pour chaque valeur, si elle vient de la source ou si l'utilisateur l'a
redéfinie — c'est le système d'*override*, explicitement rangé dans les suites.
La racine de chaque instance garde sa provenance (composant `SceneInstance`),
donc un tel système pourra être ajouté **sans changer le format**.

**Identifiants conservés à la relecture.** Le fichier porte les `NodeId`, et le
chargeur les respecte : une référence notée ailleurs (sélection de l'éditeur,
variable de script) survit au rechargement. Garde-fou : indices absurdes ou
dupliqués (fichier trafiqué) → renumérotation + re-câblage des références, au
lieu d'un refus.

## 4. Performances mesurées

`tests/scene_node_smoke_test.cpp`, build de debug (`-O0` + ASan, donc ~20 à 50×
plus lent qu'en release) :

| nœuds | création | lecture transforms | relecture (cache) | 100 déplacements | copie | sauvegarde | chargement | chemin | validation |
|---|---|---|---|---|---|---|---|---|---|
| 1 000 | 7 ms | 2,4 ms | 1,4 ms | 0,02 ms | 2,1 ms | 122 ms | 236 ms | 0,09 ms | 3,4 ms |
| 10 000 | 67 ms | 20 ms | 13 ms | 0,02 ms | 18 ms | 1,15 s | 2,2 s | 0,35 ms | 47 ms |
| 100 000 | 662 ms | 192 ms | 129 ms | 0,02 ms | 345 ms | 11,9 s | 23,0 s | 3,5 ms | 1,96 s |

**Trois comportements quadratiques trouvés par ce banc d'essai et corrigés** —
ils n'auraient jamais été visibles sur une scène de démonstration :
1. chargement : recherche linéaire dans la table de remappage → table de
   hachage (513 s → 157 s à 100 000 nœuds) ;
2. chargement : `AllocateSlotAt` parcourait la liste des emplacements libres à
   chaque nœud → la liste n'est plus chaînée pendant la lecture et est
   reconstruite une fois à la fin (157 s → **23 s**) ;
3. validation : les doublons de noms étaient cherchés par nœud contre toute sa
   fratrie → une passe par parent (39,5 s → **1,96 s**).

Coût restant assumé : sauvegarde/chargement passent par `data::JsonDocument`
(un `shared_ptr` par valeur), ce qui domine les 12 s / 23 s à 100 000 nœuds.
C'est le prix de la réutilisation du format `data::` plutôt que d'un second
encodeur.

## 5. Limites connues

- `Resolve` compare les segments aux noms des enfants **linéairement** : un
  parent de 10 000 enfants rend la résolution de chemin coûteuse (3,5 ms
  mesurés). Un index nom → enfant par parent serait la suite naturelle.
- La sérialisation est **imbriquée** (un nœud porte ses enfants) : lisible et
  copiable-collable à la main, mais un arbre très profond consomme la pile à
  l'encodage (`NodeToJson` est récursif ; le parcours, lui, est itératif).
- `DecomposeTRS` ne peut pas représenter un **cisaillement** (parent à échelle
  non uniforme × enfant tourné) : la rotation rendue est le repère
  orthonormalisé le plus proche, et `TRS::sheared` le signale.

## 6. Intégration dans l'éditeur de niveau (migration du document)

`level_editor::SceneDesc` **ne contient plus** `std::vector<ObjectDesc>` mais un
`scene::NodeTree`. Ce que `ObjectDesc` portait est devenu des composants :

| avant (liste plate) | après (arbre) |
|---|---|
| `ObjectDesc{shape, dimensions, segments, source, material}` | composant `MeshInstance` (`VisualDesc::Read/Write`) |
| `ObjectDesc{body, collider, halfExtents, mass…}` | composant `RigidBody` (`PhysicsDesc::Read/Write`) |
| `ObjectDesc::tag` | propriété libre `tag` du nœud |
| `ObjectDesc::parent` (un NOM) | vraie parenté d'arbre, profondeur libre |
| identité = le nom | identité = `scene::NodeId` |

`ObjectDesc` **survit comme constructeur** (`ToNode()`/`FromNode()`) : contenu
livré, import glTF, scripts et tests s'en servent pour décrire un objet à
créer. Une fois le nœud créé, la seule source de vérité est l'arbre.

**Compatibilité des fichiers** : `FORMAT_VERSION` passe de 2 à 3 ; un projet
en version 2 est **converti** en arbre à la lecture (`SceneDesc::FromLegacyObjects`,
deux passes — créer, puis reparenter en `KEEP_LOCAL`, car le transform de
l'ancien format était déjà relatif au parent). Test dédié.

**Ce que la migration a corrigé, et qui était un bug latent** :
`MirrorSimulationToDocument` recopiait la position MONDE du solveur dans le
transform du document, qui est LOCAL. C'était sans conséquence tant que la
scène était plate ; avec une hiérarchie, la roue d'une voiture se serait
retrouvée à « position de la voiture + la sienne » à chaque image. La
conversion passe désormais par `SetGlobalTransform`. Symétriquement,
`AttachBody` crée le corps à la position **monde** du nœud.

**Runtime** : `SceneObjectRef` porte un `NodeId` (et non un nom — il survit au
renommage) ; une table `NodeId → ecs::Entity` remplace le balayage de toutes
les entités à chaque commande. Chaque commande existe en deux formes,
par identifiant (interface) et par nom ou chemin (scripts, tests).

**Commandes hiérarchiques ajoutées** : `CreateGroup`, `ReparentNode`,
`MoveNodeInParent`, `DuplicateNode`, `RemoveNode` (sous-arbre),
`SavePackedScene`, `InstantiateSceneFile`. Toutes annulables — l'historique
prenait déjà un instantané du document, il prend maintenant l'ARBRE, donc
annuler un reparentage ou une suppression de sous-arbre marche sans code
spécifique.

**Duplication/instanciation : seule la RACINE est renommée.** Les nœuds
internes gardent leur nom (« Voiture 2 > Roues > AvantGauche » se lit, et un
script y accède par chemin comme dans l'original). L'unicité entre frères est
acquise par construction ; l'unicité GLOBALE reste une politique de l'éditeur
pour les objets créés à la main (`SceneDesc::UniqueName`), parce que les
scripts et le rapport désignent les objets par leur nom seul.

**La racine de l'arbre porte le nom de la scène** (`SceneDesc::SetName`) :
c'est ce qui rend `/Assemblages/Robot/Corps/Tête` écrivable dans un script.

## 7. Interface

**Outliner** : arbre récursif (`ui::TreeNode` pour un nœud avec enfants,
`ui::Selectable` pour une feuille) — l'indentation vient de l'imbrication
réelle, plus d'un préfixe d'espaces, et la profondeur n'est plus limitée à 1.
L'état replié/déplié est retenu PAR NŒUD, il survit donc au rafraîchissement.
Glisser-déposer : déposer une ligne sur une autre en fait un ENFANT (le refus
d'un cycle est traité par l'arbre lui-même).

**Primitive ajoutée à `ui::`** : le glisser-déposer ENTRE widgets n'existait
pas (`UiReorderable` ne sait que réordonner des frères). Nouveaux
`UiDragPayload` (source, transporte un identifiant applicatif) et
`UiDropTarget` (cible, avec un genre accepté), `UiCallbacks::onDrop`, et les
props `.DragPayload()/.DropTarget()/.OnDrop()`. La cible est cherchée dans la
chaîne du widget de premier plan — donc la même machinerie que le blocage
z-order ajouté plus tôt. Un seuil de 4 px distingue le clic du glissé.

**Inspecteur** : section « Nœud » (nom, type, **chemin hiérarchique**), position
MONDE et parent affichés dès qu'un objet est enfant d'un autre, case
« Verrouillé », et un nœud structurel dit « aucune apparence » au lieu
d'afficher des réglages de matériau qui ne s'appliqueraient à rien.

**Manipulateur 3D** : raisonne en MONDE (`SelectionWorldPosition`,
`SetGlobalPosition`) — déplacer l'enfant d'un parent déplacé ne le fait plus
sauter. Magnétisme, annulation groupée et rotation/échelle inchangés.

## 8. Scripts

Espace `node.*` ajouté, en complément de `object.*` (qui reste la façon la plus
courte de désigner un objet par son nom) : `path`, `parent`, `children`,
`find` (chemins relatifs compris), `world_position`, `create_group`,
`reparent` (`"local"` pour garder le transform local), `duplicate`, `set_tag`,
`prop` (lecture/écriture d'une propriété libre), `save_scene`, `instantiate`.

## 9. Contenu et scénario de démonstration

Scène livrée **« Assemblages »** : un robot articulé (4 niveaux :
`Robot > Corps > Tête > Œil`), un véhicule (groupe de roues), un personnage
(visuel / collision / caméra séparés) et un bâtiment (structure + mobilier) —
quatre natures différentes pour montrer qu'un seul mécanisme les sert. Elle
contient délibérément deux « Tête » (une par personnage) : c'est le CHEMIN qui
les distingue.

Scénario **`--scenario=node_hierarchy`** (10 étapes, 10 captures, assertions
qui font échouer le scénario) : arbre de départ, déplacement d'un parent,
transform local d'un enfant, reparentage sans déplacement visible, duplication
de sous-arbre, suppression + annulation + rétablissement, scène emballée
instanciée deux fois, aller-retour fichier.
