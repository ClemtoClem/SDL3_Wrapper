# physics — physique de corps rigides 3D sur l'ECS

`libs/infrastructure/physics` · couche **infrastructure** · `libphysics.a` · espace de noms `physics::`

## Présentation

`physics` simule des corps rigides en 3D. Un corps est un composant
`RigidBody` d'une entité [`ecs`](libs_infrastructure_ecs.md). Chaque
`World::Step(dt)` enchaîne :

1. **la phase large** : paires candidates, canonisées (identifiant le plus
   petit d'abord) pour rester stables d'une image à l'autre ;
2. **la phase étroite** : contacts sphère/sphère, sphère/boîte, boîte/boîte
   (SAT), et les combinaisons avec des capsules ;
3. **le solveur** : impulsions séquentielles, *warm starting* (impulsions
   gardées d'un pas à l'autre) et correction de Baumgarte ;
4. **l'intégration** : Euler semi-implicite.

Le module gère aussi des **articulations** (charnière, fixe, ressort) et la
**téléportation à travers des portails**. Il ne dépend que de `core`, `math`,
`ecs` et `jobs` : ni `render3d`, ni `ui`. Le parallélisme, s'il est activé
(`jobs::JobSystem`), donne un **résultat identique**, ordre des paires et
des contacts compris.

Le module est utilisé par la [sandbox](demo_sandbox.md) et par les scripts,
à travers [`script_bindings`](libs_infrastructure_script_bindings.md).
[voxel](libs_infrastructure_voxel.md) a sa propre physique, qui travaille
sur la grille de voxels.

## Architecture interne

| En-tête | Contenu |
|---|---|
| `shapes.hpp` | `Sphere`, `Box`, `Capsule` (chacune porte son centre et son orientation dans le monde), `Shape` (`std::variant`), `ShapeWorldAABB`, `SyncShapeTransform` |
| `rigidbody.hpp` | `RigidBody` (position, orientation, vitesses, masse, inertie diagonale, restitution, frottement) ; `MakeDynamic`, `MakeStatic` |
| `broadphase.hpp` | `BroadPhasePair`, `BroadPhase(registry, …)` |
| `narrowphase.hpp` | `ContactPoint`, `Manifold` (normale de A vers B), `Collide*`, `GenerateManifold` |
| `solver.hpp` | `SolverConfig`, `ContactConstraint`, `ContactKey`, `WarmStartCache`, `BuildContactConstraints`, `SolveContactConstraints` |
| `joints.hpp` | `HingeJoint`, `FixedJoint`, `SpringJoint`, `Solve*`, `ApplySpringForce` |
| `portal_teleport.hpp` | `PortalPlane`, `TeleportBodiesThroughPortals` |
| `world.hpp` | `WorldConfig`, `World` |

## API publique

### `World`

- **`World(ecs::ArchetypeRegistry&)`** : le monde ECS n'est pas possédé.
  L'application y mélange ses propres composants.
- **`config`** (`WorldConfig`) :
  - `gravity` : `{0, -9.81, 0}` par défaut ;
  - `solverIterations` (8) et `jointIterations` (4) ;
  - `solver` : `baumgarte` (0,2), `slop` (pénétration tolérée, 0,01) et
    `restitutionVelocityThreshold` (pas de rebond sous 1 m/s, pour éviter
    les tremblements).
- **`jobSystem`** : `jobs::JobSystem*` facultatif, non possédé.
- **`portalPlanes`** : portails actifs (vide par défaut).
- **`Step(dt)`** fait **un** pas de durée `dt`, sans accumulateur interne.
  L'appelant choisit un pas fixe stable, par exemple 1/60 ou 1/120 s, ou
  passe par un `FixedStepClock`.

### `RigidBody`

| Fabrique | Rôle |
|---|---|
| `MakeDynamic(forme, position, masse, restitution = 0.3, frottement = 0.5)` | Corps soumis aux forces ; l'inertie est calculée pour la forme pleine |
| `MakeStatic(forme, position, orientation, restitution, frottement)` | Masse infinie (sol, murs) |

On lit et modifie ses champs par `registry.GetComponent<RigidBody>(e)` :
`position`, `orientation`, `linearVelocity`, `angularVelocity`…

### Articulations

- **`HingeJoint`** : charnière, avec point et axe ;
- **`FixedJoint::Create(a, b, ancreA, …)`** : liaison rigide ;
- **`SpringJoint`** : ressort amorti.

Elles sont résolues par la même méthode que les contacts, en
`jointIterations` passes par pas.

## Exemple

```cpp
#include <cstdio>
#include <jobs/job_system.hpp>
#include <physics/world.hpp>

using namespace physics;

int main() {
	ecs::ArchetypeRegistry registry; // les corps sont des composants RigidBody d'entités
	World world(registry);
	jobs::JobSystem jobs; // facultatif : phase large et contacts répartis, résultat identique
	world.jobSystem = &jobs;

	// Sol statique (boîte plate), une caisse et une bille qui tombent
	registry.SpawnBundle(RigidBody::MakeStatic(Box{{}, {10.f, 0.5f, 10.f}}, {0.f, -0.5f, 0.f}));
	ecs::Entity crate = registry.SpawnBundle(RigidBody::MakeDynamic(Box{{}, {0.5f, 0.5f, 0.5f}}, {0.f, 4.f, 0.f}, 2.f));
	ecs::Entity ball = registry.SpawnBundle(RigidBody::MakeDynamic(Sphere{{}, 0.3f}, {0.2f, 8.f, 0.1f}, 0.5f, /*restitution*/ 0.6f));

	const float dt = 1.f / 120.f; // pas fixe choisi par l'appelant
	for (int step = 0; step <= 600; ++step) {
		world.Step(dt);
		if (step % 150 == 0) {
			auto c = registry.GetComponent<RigidBody>(crate);
			auto b = registry.GetComponent<RigidBody>(ball);
			std::printf("t=%.2f s  caisse y=%.2f  bille y=%.2f\n", step * dt, c.Unwrap()->position.y, b.Unwrap()->position.y);
		}
	}
	return 0;
}
```

Sortie :

```
t=0.00 s  caisse y=4.00  bille y=8.00
t=1.25 s  caisse y=0.59  bille y=1.42
t=2.50 s  caisse y=0.50  bille y=1.30
t=3.75 s  caisse y=0.50  bille y=1.28
t=5.00 s  caisse y=0.50  bille y=0.30
```

La caisse se pose sur le sol (y = 0,5, sa demi-hauteur). La bille rebondit,
reste un moment sur la caisse (1,0 + 0,3), puis glisse jusqu'au sol.

## Intégration

`modules = ["physics"]`.

- **Rendu** : pour afficher les corps, recopiez à chaque image
  `RigidBody::position` et `orientation` dans la transformation de rendu,
  par exemple dans `render3d::SceneTransform`.
- **Coût** : la phase large est en O(n²). Au-delà de quelques centaines de
  corps, donnez un `jobs::JobSystem` au monde.

## Tests

```sh
cpp-maker test -m physics
```

Les 15 tests couvrent :

- la mise au repos de sphères et de boîtes ;
- la conservation de l'énergie en choc élastique ;
- la charnière ;
- le cache de *warm starting* ;
- la téléportation par portail ;
- les contacts capsule/boîte et sphère/boîte.
