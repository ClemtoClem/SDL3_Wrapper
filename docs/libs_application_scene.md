# scene — hiérarchie de nœuds générique

`libs/application/scene` · couche **application** · `libscene.a` · espace de noms `scene::`

## Présentation

`scene` fournit un **arbre de composition de nœuds**, dans l'esprit de Godot :
une scène est un arbre de nœuds, et un objet complexe se construit en
imbriquant des nœuds simples. Chaque nœud porte :

- un nom et un **type** sous forme de chaîne (`"Node"`, `"Node3D"`,
  `"MeshInstance"`…) ;
- une transformation locale (position, quaternion, échelle) ;
- des **composants** nommés, faits de propriétés typées ;
- des **propriétés libres** (`max_speed = 180`) : éditables dans un
  inspecteur, lisibles depuis un script et conservées par la sauvegarde.

L'arbre gère aussi les chemins (`Wheels/FrontLeft`), les transformations
héritées, la duplication, la validation et la sérialisation JSON. Les
**scènes empaquetées** (`PackedScene`) sont des hiérarchies réutilisables,
instanciées plusieurs fois.

Le module ne dépend ni de `render3d`, ni de `physics`, ni de `ui`. Il est
utilisé par la [sandbox](demo_sandbox.md) : document de scène, outliner,
inspecteur et instances d'objets.

## Architecture interne

| Fichier | Contenu |
|---|---|
| `scene.hpp` | En-tête parapluie |
| `property.hpp`, `src/property.cpp` | `NodeId`, `PropertyType`, `PropertyValue` (Nil, Bool, Int, Float, Str, Vec2, Vec3, Quat…), `PropertyMap` |
| `node.hpp`, `src/node.cpp` | `Transform` (dérive de `math::Transform3D`), `Component`, `node_type::NODE` et `NODE3D`, `Node` |
| `tree.hpp`, `src/tree.cpp` | `NodePath`, `ValidationIssue`, `ValidationReport`, `ReparentMode`, `NodeTree` |
| `packed.hpp`, `src/packed.cpp` | `PackedScene` |
| `type_registry.hpp`, `src/type_registry.cpp` | `NodeTypeInfo`, `NodeTypeRegistry` |

**Stockage** : les nœuds vivent dans des emplacements à génération. Un
`NodeId` est un couple (indice, génération), comme `ecs::Entity`. La parenté
est exprimée en `NodeId`, jamais en pointeur. Les transformations globales
sont invalidées en O(1).

Dépendances : `core`, `math`, `sdl3`, `dataio`.

## API publique

### `NodeTree`

| Domaine | Membres |
|---|---|
| Construction | `NodeTree(nomRacine)`, `Root()`, `Create(parent, nom, type)`, `Add(parent, Node, index)`, `Remove`, `RemoveChildren`, `Duplicate(source, parent)` |
| Hiérarchie | `ParentOf`, `ChildrenOf`, `AllNodes`, `Descendants`, `IsAncestorOf`, `DepthOf`, `Reparent(enfant, parent, ReparentMode)`, `MoveChild`, `Traverse` |
| Noms et chemins | `Rename`, `UniqueChildName`, `FindChild`, `FindByName`, `Resolve(chemin | NodePath)`, `PathOf` |
| Transformations | `LocalTransform`, `SetLocalTransform`/`Position`/`Rotation`/`Scale`, `GlobalTransform`, `SetGlobalTransform`/`Position` |
| Visibilité | `SetVisible`, `IsVisibleInTree` |
| Accès | `Get(id)` → `Node*`, `Contains`, `Size` |
| Fichiers | `ToJson`/`FromJson`, `EncodeJson`/`DecodeJson` |
| Validation | `Validate(ressourceExiste, typeConnu)` → `ValidationReport` (`Ok`, `ErrorCount`, `WarningCount`, `Format`) |

### `Node`

- Champs : `name`, `type`, `transform`, `visible`, `locked`, `components`
  et `properties`.
- Composants : `FindComponent`, `HasComponent`, `SetComponent`,
  `RemoveComponent`, `ComponentProperty`.
- Propriétés libres : `Get(clé)` et `Set(clé, valeur)`.

### `PackedScene`

- `FromSubtree(arbre, racine)`, puis `SetSource(chemin)`.
- `InstantiateInto(cible, parent, nom)` instancie la scène.
- `IsInstanceRoot` et `InstanceSource` reconnaissent une racine d'instance et
  le fichier dont elle vient.
- `ToJson` et `FromJson` pour la sérialisation.

### `NodeTypeRegistry`

- `WithBuiltins()` crée un registre avec les types prédéfinis.
- `Register(NodeTypeInfo)` ajoute un type, décrit par :
  - `name`, `label`, `category`, `icon` ;
  - composants et propriétés posés par défaut ;
  - le droit d'avoir des enfants.
- `Find`, `LabelOf` et `IconOf` lisent le registre.
- `Make(type, nom)` et `Create(arbre, parent, type, nom)` construisent un
  nœud.

## Exemple

```cpp
#include <cstdio>
#include <scene/scene.hpp>

int main() {
	// Une voiture : un arbre de nœuds, des composants et des propriétés libres
	scene::NodeTree car("Car");
	scene::NodeId body = car.Create(car.Root(), "Body");
	car.Get(body)->SetComponent({.type = "MeshRenderer", .props = {}});
	car.Get(body)->Set("max_speed", scene::PropertyValue::Float(180.0));
	scene::NodeId wheels = car.Create(car.Root(), "Wheels", scene::node_type::NODE);
	for (const char *name : {"FrontLeft", "FrontRight", "RearLeft", "RearRight"})
		car.Create(wheels, name);
	car.SetLocalPosition(car.Resolve("Wheels/FrontLeft"), {-0.8f, 0.3f, 1.2f});
	car.SetLocalPosition(wheels, {0.f, 0.f, 0.5f}); // déplacer le groupe déplace ses enfants

	scene::NodeId wheel = car.Resolve("Wheels/FrontLeft");
	math::FVector3 world = car.GlobalTransform(wheel).position;
	std::printf("%s en monde : (%.1f, %.1f, %.1f)\n", car.PathOf(wheel).CStr(), world.x, world.y, world.z);

	// Une scène réutilisable, instanciée trois fois dans une autre scène
	scene::PackedScene packed = scene::PackedScene::FromSubtree(car, car.Root());
	packed.SetSource("scenes/car.scene");
	scene::NodeTree track("RaceTrack");
	for (int i = 1; i <= 3; ++i)
		packed.InstantiateInto(track, track.Root(), "Car0" + String::From(i));
	std::printf("circuit : %zu nœuds\n", track.Size());

	// Sérialisation : aller-retour JSON
	String json = track.EncodeJson();
	auto reloaded = scene::NodeTree::DecodeJson(json);
	std::printf("relu : %zu nœuds, %zu octets de JSON\n", reloaded ? reloaded.Value().Size() : 0, json.GetSize());
	return 0;
}
```

Sortie :

```
/Car/Wheels/FrontLeft en monde : (-0.8, 0.3, 1.7)
circuit : 22 nœuds
relu : 22 nœuds, 13042 octets de JSON
```

## Intégration

`modules = ["scene"]`. Le module appartient à la couche **application** :
seuls les modules de cette couche peuvent en dépendre.

Pour l'afficher, l'application traduit l'arbre en objets de rendu.
La sandbox le fait par exemple dans `demos/sandbox/src/engine/`.

## Tests

```sh
cpp-maker test -m scene
```

`test_scene_node.cpp` couvre :

- la hiérarchie et les transformations héritées ;
- les chemins, la duplication et le reparentage ;
- la validation ;
- les allers-retours JSON et les scènes empaquetées.
