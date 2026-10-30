# Architecture du moteur et de l'éditeur — plan de refonte

Refonte commencée le 2026-10-12. Ce document est la référence : chaque étape
le met à jour (colonne « État »).

```
ASSETS ──références──▶ NŒUDS ──assemblage──▶ OBJETS ──instanciation──▶ SCÈNES
Mesh, Texture,          GroupNode, MeshNode,    ObjectDefinition        Scene : instances,
Material, Shader,       CameraNode, LightNode,  (arbre de nœuds         nœuds placés, caméra
AudioClip               SpriteNode              réutilisable)           active, environnement,
                                                                        réglages rendu/physique/jeu
```

## Fondation : l'ECS

**Tout repose sur l'ECS** (`lib/include/ecs`, registre à archétypes). Un
nœud est une ENTITÉ ; son genre est l'ensemble de ses composants. Les
« classes » de nœuds du schéma sont des composants (et des poignées typées
pour les manipuler), pas une hiérarchie de classes C++ parallèle à l'ECS :

| Nœud | Composants |
|---|---|
| tout nœud | `NodeInfo { Uuid id; String name; }`, `LocalTransform` (Transform3D), `WorldTransform` (matrice, calculée), `Parent`, `Children` |
| `GroupNode` | rien de plus (structure : transmet son transform) |
| `MeshNode` | `MeshRenderer { AssetRef<Mesh>; AssetRef<Material>; }` |
| `CameraNode` | `CameraComponent` |
| `LightNode` | `LightComponent` |
| `SpriteNode` | `SpriteComponent { AssetRef<Texture>; … }`, `LocalTransform2D` |
| instance d'objet | `ObjectInstance { AssetRef<ObjectDefinition>; overrides }` — ses nœuds générés portent `InstanceMember` |

Les services globaux sont des **ressources ECS** (`registry.Resources()`) :
`AssetManager`, `NodeIndex` (Uuid → Entity), `SceneSettings` de la scène
chargée. Les traitements sont des **systèmes** : `TransformSystem`
(matrices monde), `RenderExtractionSystem` (→ `RenderQueue`),
`PhysicsSyncSystem`, `ScriptSystem`. Les données PERSISTANTES (définitions
d'objets, scènes sur disque) sont des données explicites, chargées dans un
monde ECS et réécrites depuis lui par les sérialiseurs.

## Modules

| Module | Contenu | Bibliothèque (`lib/`) |
|---|---|---|
| Core | `Uuid`, `Transform3D`, `Transform2D`, sérialisation JSON (`data::Node`) | `core/uuid.hpp`, `math/transform.hpp`, `data/json.hpp` |
| Assets | `AssetId`, `AssetRef<T>`, catalogue ; `resources::Resource` (chargement asynchrone, états, identité) et `ResourceCache` (le gestionnaire d'assets : résolution par id, fabriques par nature, catalogue, références manquantes ; ressource ECS) ; ressources concrètes enveloppant les types existants : `render3d::MeshResource` (Mesh), `TextureResource`, `MaterialResource` (Material + références), `ShaderResource`, `audio::AudioClipResource` (WavData) | `resources/` + `render3d/resources.hpp` + `audio/clip.hpp` |
| SceneGraph | composants de nœud (cf. ci-dessus), `NodeIndex`, opérations de hiérarchie (créer, détruire avec descendants, re-parenter en gardant le transform monde), `TransformSystem` | `scene/` |
| Objects | `ObjectDefinition` (arbre de nœuds réutilisable : données, pas entités), `ObjectInstance` + `NodeOverride`, instanciation dans un monde, copie indépendante | `scene/` |
| Scenes | `Scene` (enregistrement de nœuds + instances) et `SceneSettings` (environnement, skybox, caméra active, rendu, physique, jeu) — n'hérite PAS d'un objet ; chargement dans / extraction depuis un monde ECS | `scene/` |
| Rendering | systèmes : parcours des entités → extraction → `RenderQueue` → `Renderer` ; maillages = assets partagés (cache GPU par asset) | `render3d/` |
| Physics | corps et formes en composants, synchronisés par système | `physics/` |
| Editor | `Selection`, `UndoRedo` + `ICommand` (`SetTransformCommand`, `RenameNodeCommand`, `AddNodeCommand`, `RemoveNodeCommand`, `ReparentNodeCommand`, `SetMaterialCommand`…) qui désignent les entités par `Uuid`, panneaux | `examples/sandbox/editor` |

## Règles

- **Améliorer l'existant** plutôt que doubler : les ressources sont des
  `resources::Resource` (chargement asynchrone de l'`AsyncLoader`) qui
  enveloppent `render3d::Mesh`, `render3d::Material`, `sdl3::WavData` ; le
  gestionnaire d'assets est `ResourceCache`, étendu.

- **Propriété** : la scène possède sa hiérarchie, l'`ObjectDefinition` la
  sienne ; un parent possède ses enfants ; le `ResourceCache` possède les
  ressources (`std::shared_ptr`), une `render3d::Shape` partage la géométrie
  qu'elle montre. Les autres relations sont des
  identifiants (`NodeId`, `AssetId`) ou des pointeurs non propriétaires de
  courte durée.
- **Identifiants stables** : une commande d'annulation ne garde jamais de
  pointeur, seulement des identifiants, résolus à l'exécution.
- **Définition ≠ instance** : une instance ne stocke que sa référence, son
  transform et ses surcharges ; le contenu est résolu à partir de la
  définition.
- **Asset ≠ nœud** : un `MeshNode` référence un maillage, il ne le contient
  pas. Deux `MeshNode` du même maillage partagent la géométrie.
- **État sauvegardé ≠ état de l'éditeur** : sélection, poignées, aperçus ne
  sont pas dans les fichiers.
- **Sérialisation explicite** : des identifiants et des données nommées, une
  version de format, des migrations, des références manquantes signalées.

## Étapes

| # | Étape | État |
|---|---|---|
| 1 | Core et ressources : `Uuid`, `Transform3D/2D` (`scene::Transform` en dérive), `AssetId`/`AssetRef`/catalogue, `Resource` identifiée, `ResourceCache` par id (async), ressources maillage/texture/matériau/shader/son, primitives partagées par clé, lecteur glTF dans `Mesh::FromGltf` | fait — `tests/resources_asset_smoke_test.cpp` ; l'éditeur s'en sert (primitives et modèles partagés, chargement en arrière-plan, son sur SDL3_mixer) : cf. [ressources et son de l'éditeur](project_game_editor_audio.md), `tests/game_editor_audio_smoke_test.cpp` |
| 2 | SceneGraph sur l'ECS : composants de nœud, `NodeIndex`, hiérarchie, `TransformSystem` | à faire |
| 3 | Rendu minimal : maillages partagés (cache GPU par asset), extraction → file de rendu | à faire |
| 4 | `ObjectDefinition`, `ObjectInstanceNode`, `NodeOverride`, copie indépendante | à faire |
| 5 | `Scene`, `SceneSettings`, `Environment` (caméra active, lumière, skybox) | à faire |
| 6 | Sérialisation : `ObjectSerializer`, `SceneSerializer`, versions et migrations | à faire |
| 7 | Éditeur : `Selection`, `ICommand` + annuler/rétablir, panneaux sur le nouveau modèle | à faire |
| 8 | Optimisations : matrices en cache, extraction, chargement asynchrone, instanciation GPU | à faire |
