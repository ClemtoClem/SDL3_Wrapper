---
name: game-editor-scripts
description: "game_editor_demo — les scripts de jeu DÉFINISSENT des classes dérivées des bases C++ du moteur (Scene, Behaviour, Node3D, Mesh3D, Node2D, PhysicsBody, Light3D, Gameplay, SceneAsset), instanciées et appelées par le moteur, avec cycle de vie RAII."
metadata:
  type: project
---

# Scripts de jeu de game_editor_demo : classes dérivées des bases du moteur

Depuis le 2026-10-05, un script de jeu ne déclare plus des fonctions globales
(`fn on_update(dt)`, `fn on_start(self)` + tables indexées par `self`) : il
**définit des classes** qui dérivent des **bases écrites en C++** par le
moteur (`examples/game_editor_demo/engine/script_owners.hpp`), et c'est le
moteur qui les instancie et appelle leurs méthodes. Le mécanisme générique
(héritage d'un type de l'hôte, registre, séquence RAII) est dans la
bibliothèque : `lib/include/data/script/script_owners.hpp`, cf.
[le guide du langage](project_script.md#bases-fournies-par-lhôte-owners).

Un script à l'ancienne est signalé explicitement (« aucune classe dérivée de
`Scene` » / « de `Behaviour` ») au lancement du mode Jeu.

## Les bases

Toutes sont dans l'espace de noms `game` et aussi globales.

| Base | Rôle | Méthodes principales |
|------|------|----------------------|
| `Scene` | Contrôleur de la scène : le script de scène en définit **une** classe concrète, instanciée au lancement | `time()`, `load(scène)`, `quit()`, `state()`, propriété `scene_name` |
| `Behaviour` | Comportement d'un nœud **existant** : un script de la bibliothèque en définit une classe ; chaque nœud qui porte le script (composant Script) reçoit son instance. Créée à la main : `super.init("NomDuNœud")` | méthodes de nœud 3D |
| `Node3D` | Nœud (groupe, ou forme si `shape`) **créé** par l'objet, retiré à sa destruction | méthodes de nœud 3D |
| `Mesh3D` | Objet visible créé par l'objet (boîte par défaut) | méthodes de nœud 3D + `set_mesh(chemin)`, `set_shape(forme[, sx, sy, sz])`, `set_material({…})` |
| `Node2D` | Nœud 2D créé par l'objet (options de `node2d.spawn`) | celles de `node2d.*` sans le premier argument : `position()`, `move(dx, dy)`, `set_text(t)`, `overlaps(autre)`… |
| `PhysicsBody` | Corps rigide du nœud porté par une base déclarée **avant** elle | `set_body({kind, mass, collider, restitution, friction})`, `velocity()`, `set_velocity(x, y, z)`, `impulse(x, y, z)`, `body_kind()` |
| `Light3D` | Lumière de ce même nœud (créée si absente, options `light: {…}`) | `intensity()`, `set_intensity(v)`, `set_color(r, g, b)`, `set_light({…})` |
| `Gameplay` | Abonnements à des évènements | `on(évènement, fn)`, `emit(évènement, …)`, `off(évènement)`, `forget_listeners()`, `listeners([évènement])` |
| `SceneAsset` | Fichier `.scene` réutilisable (`super.init("scenes/x.scene")`) ; ses instances sont retirées à la destruction | `instantiate({parent, pos, rot, scale})`, `instances()`, `clear()`, propriété `path` |

Méthodes de nœud communes (Behaviour, Node3D, Mesh3D ; les trois premières et
`prop`/`tag` aussi pour Node2D) : `node()` (nom ou chemin, la forme de l'API
`object.*`), `exists()`, `path()`, `child(nom)`, `prop(clé[, valeur])`,
`tag()`, `set_tag(t)`, propriété `name` ; en 3D : `position()`,
`set_position(x, y, z)`, `world_position()`, `rotation()`,
`set_rotation(x, y, z)` (degrés), `scale()`, `set_scale(x, y, z)`,
`set_visible(b)`, `set_color(r, g, b)`, `size()`, `forward()`,
`to_local(x, y, z)`, `to_world(x, y, z)`.

Construction : `super.init(…)` passe les mêmes arguments à chaque base ;
celles du moteur prennent la première chaîne comme nom et la première table
comme options (mêmes clés que `scene.spawn`, plus `model` et `light`). Une
seule base porteuse de nœud par objet (Node3D, Mesh3D, Node2D ou Behaviour).

## Ce que le moteur appelle

- `Scene` : `on_start()`, `on_trigger(zone, autre, évènement)`,
  `on_trigger_exit(…)`, `on_mouse_mode(mode)`.
- `Behaviour` attachée par l'éditeur : `on_start()` une fois TOUTES les
  Behaviour de la scène (ou de la pièce instanciée) construites ;
  `on_trigger(autre, évènement)` / `on_trigger_exit(…)` si son nœud est une
  zone.
- **Tout objet vivant** dérivé d'une base (Scene, Behaviour, Mesh3D, Node2D…,
  y compris ceux créés à la main) : `on_update(dt)` à chaque image, dans
  l'ordre de création.
- Destruction (`on_destroy()` puis `deinit()`, puis bases en ordre inverse) :
  `destroy()`, retrait de son nœud (par n'importe qui : `scene.remove`, une
  autre ressource, l'éditeur), ou fin de la scène jouée. Une erreur dans un
  rappel désactive le script fautif pour la partie.

## Dans l'éditeur

L'interface lit les objets de script dans les registres C++ de
`data/script/script_owners.hpp`, et analyse les scripts SANS les exécuter
(`engine/script_outline.hpp`, `Runtime::OutlineScript`) : un script de scène
peut créer des objets dès son premier niveau, l'éditeur ne doit pas le lancer
pour savoir ce qu'il définit. L'analyse relève les classes (espaces de noms
compris), résout leurs bases — classe du fichier, base du moteur, classe d'un
module importé (lu dans la bibliothèque du projet, en mémoire) — et applique
les règles du moteur (seules les classes FEUILLES comptent).

- **Inspecteur, section Script** : classe instanciée et ses bases
  (`Torche (Behaviour, Light3D)`), rappels définis, modules importés ; en
  orange si le moteur n'attachera rien au nœud (aucune Behaviour, ou
  plusieurs) — visible en édition, plus seulement au lancement.
- **Inspecteur, section « Objets de script »** (en partie) : chaque objet
  vivant qui porte le nœud — Behaviour attachée, Mesh3D créé par la scène… —,
  ses champs (instantané, « Actualiser ») et « Détruire » (séquence RAII
  complète).
- **Arbre de scène** (en partie) : la classe des objets qui portent un nœud,
  à la suite de son type.
- **Encart du mode Jeu (F9)** : objets vivants regroupés par classe
  (`Sphere (Mesh3D, PhysicsBody) ×3`).
- **Bibliothèque et navigateur de ressources** : rôle de chaque script
  (comportement, scène, module) ; erreur de syntaxe ou script de scène refusé
  en rouge/orange, module en gris. Un module à plusieurs Behaviour (`sprint`)
  est valide : il ne serait refusé qu'attaché à un nœud — le bouton
  « Attacher » le signale.
- **Éditeur de code** : la barre d'état dit ce que le moteur fera du script
  (« Compile ✓ — scène CircuitOvale (Course, Scene) ») ou pourquoi il le
  refusera ; les noms des bases du moteur et `import` sont colorés.
- **Scripts ▸ Vérifier tous les scripts** applique les mêmes règles.

## Organisation recommandée d'un projet

Les scripts de la **bibliothèque** du projet sont aussi des **modules**
importables : `import "course"` lit d'abord le script `course` du projet en
mémoire (modifications non enregistrées comprises), puis
`<projet>/scripts/course.script`. Les projets livrés s'en servent :

- `project1` : module `demo` (`abstract class SceneDemo extends Scene` :
  bandeau de titre, retour à l'accueil) dont dérivent les scènes ; `Sphere
  extends Mesh3D, PhysicsBody`, `Colonne extends Mesh3D` (terrain),
  `Etoile extends Node2D` (jeu 2D), `Orbite extends Behaviour, Light3D`.
- `project2` : module `course` (circuits fermés : `Bolide extends Behaviour,
  PhysicsBody` sur les voitures de la scène, décor `Arbre`/`Rocher`,
  `abstract class Course extends Scene`) et module `sprint` (circuits en
  ligne : voiture créée par la scène, adhérence selon l'étiquette `road_*`
  du tronçon, `Booster`, `Obstacle` animé selon `obstacle_mode`) ; chaque
  scène de circuit tient en une ligne : `class CircuitOvale extends
  course.Course {}`.
- `project3` : `Piece extends SceneAsset` (un par modèle de pièce),
  `Squelette extends SceneAsset` (le tuer le retire), `Grille` (classe
  ordinaire), Behaviour `Joueur`, `Torche`, `Coffre` sur les nœuds.

## Pièges rencontrés

- Retirer un nœud **pendant une partie** reconstruisait tout le runtime : tous
  les corps physiques repartaient de leur état initial. Le retrait en partie
  ne détruit désormais que les entités du sous-arbre.
- Instancier une pièce en partie remettait à zéro les zones `once` déjà
  déclenchées (`PrepareTriggers` effaçait l'état) : séparé en
  `PrepareTriggers(resetState)`.
- `Stop()` passait `m_playing` à faux AVANT la fin de scène : chaque objet
  détruit empilait une étape d'annulation et reconstruisait le runtime.
- Un objet tenu par deux références croisées n'est jamais libéré (comptage de
  références) : éviter scène ↔ objet en champs mutuels.
