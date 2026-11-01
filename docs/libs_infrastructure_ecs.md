# ecs — Entity-Component-System à archétypes

`libs/infrastructure/ecs` · couche **infrastructure** · `libecs.a` · espace de noms `ecs::`

## Présentation

`ecs` est un ECS orienté **archétypes**, inspiré de Bevy et Unity DOTS. Les
entités qui portent exactement le même ensemble de types de composants (leur
*signature*) sont stockées ensemble, colonne par colonne. Une requête ne
parcourt donc que des tableaux contigus.

Le module fournit aussi :

- des **ressources** : des singletons globaux typés ;
- un **tampon de commandes** pour différer les changements de structure ;
- un **ordonnanceur de systèmes** par étapes, avec pas fixe et profilage ;
- des **files d'évènements** doublement tamponnées ;
- des **relations** entre entités : hiérarchie et relations typées.

C'est la base de [`ui`](libs_infrastructure_ui.md) (chaque widget est une
entité), de [`physics`](libs_infrastructure_physics.md), de
[`voxel`](libs_infrastructure_voxel.md), du pont ECS ↔ scène de
[`render3d`](libs_infrastructure_render3d.md) et de
[`generators`](libs_infrastructure_generators.md).

## Architecture interne

| Fichier | Contenu |
|---|---|
| `include/ecs/ecs.hpp`, `src/ecs.cpp` | `Entity`, `EntityAllocator`, colonnes typées (`IAnyVec`/`AnyVecImpl<T>`), `Archetype`, filtres, `Resources`, `ArchetypeRegistry`, `CommandBuffer` |
| `include/ecs/schedule.hpp` | `Stage`, `SystemContext`, `SystemStats`, `Schedule` |
| `include/ecs/events.hpp` | `Events<T>`, `EventsOf<T>(world)`, `UpdateAllEvents`, `EntitySpawned`, `EntityDespawned` |
| `include/ecs/relations.hpp` | `ChildOf`, `Children`, `Relation<Tag>`, `SetParent`, `DespawnRecursive`, `RelatedTo`, `PruneRelations` |

Dépendances :

- `core`, pour `Option`, `Ref`, `RefMut` et `String` ;
- `diagnostics` : chaque système est une zone `DIAG_ZONE` dans les captures
  de performances.

**Optimisations internes** :

- chaque type de composant reçoit un identifiant dense (`TypeIdOf<T>()`) ;
- `Archetype::Has` et `Column` testent et accèdent en O(1) ;
- l'emplacement de chaque entité (archétype et ligne) est rangé dans une table
  indexée par `Entity::id`.

## API publique

### `Entity`

`{id, generation}`. `Valid()` et `operator bool` testent l'entité, et
`Entity::INVALID` est la valeur nulle. La génération invalide les anciennes
poignées quand un identifiant est recyclé (liste libre générationnelle de
`EntityAllocator`).

### `ArchetypeRegistry` (le monde)

| Membre | Rôle |
|---|---|
| `Entity Spawn()` | Entité vide |
| `Entity SpawnBundle(comps...)` | Entité créée directement dans l'archétype final, sans migration |
| `bool Despawn(Entity)`, `bool IsAlive(Entity)` | Destruction et test |
| `void AddComponent<T>(e, T)` | Ajout ou remplacement ; migre l'entité vers l'archétype étendu |
| `bool RemoveComponent<T>(e)` | Retrait (migration vers l'archétype réduit) |
| `Option<RefMut<T>> GetComponent<T>(e)` (et version `const` → `Option<Ref<T>>`) | Accès |
| `bool HasComponent<T>(e)` | Test |
| `Option<RefMut<T>> GetOrAddComponent<T>(e, défaut)` | Accès avec création |
| `void Query<Comps...>(fn)` | Visite `fn(Entity, Comps&...)` pour chaque entité qui correspond. Les **filtres** placés dans `Comps` (`With<T>`, `Without<T>`, `And<...>`, `Or<...>`, `All`) restreignent la recherche sans être passés à `fn` |
| `auto QueryVec<Comps...>()` | Résultats dans un vecteur de tuples |
| `auto QuerySingle<Comps...>()` | L'unique entité qui correspond, ou `NONE` (aucune, ou plusieurs) |
| `std::vector<Entity> EntitiesWith<T>()` | Entités porteuses de `T` |
| `EntityCount()`, `ArchetypeCount()`, `Clear()` | État du monde |
| `Resources resources` | Singletons du monde |

> **Ne pas modifier la structure pendant une requête.** Créer, détruire,
> ajouter ou retirer un composant pendant `Query` invaliderait les colonnes
> parcourues. Passez par un `CommandBuffer`, comme le fait `Schedule`.

### `Resources`

- `Insert(R)` ajoute un singleton.
- `Get<R>()` rend `Option<RefMut<R>>`, ou `Option<Ref<R>>` en `const`.
- `GetResult<R>()` rend `Result<RefMut<R>, String>`.
- `Contains<R>()` et `Remove<R>()` testent et retirent.

> Le commentaire d'en-tête de `ecs.hpp` montre encore une API en minuscules
> (`resources.insert`, `get_or_throw`). L'API réelle est
> `resources.Insert` / `Get` / `GetResult`.

### `CommandBuffer`

- `Push(fn)` met en file une modification quelconque.
- `AddComponent<T>(e, T)`, `RemoveComponent<T>(e)` et `Despawn(e)` sont
  des raccourcis.
- `Flush(world)` applique la file ; `IsEmpty()` et `GetSize()` la
  décrivent.

### `Schedule`

Les systèmes sont des fonctions nommées `void(SystemContext&)`, rattachées à
une étape :

1. `STARTUP` : une seule fois, à la première image ;
2. `PRE_UPDATE` ;
3. `FIXED_UPDATE` : à pas fixe `fixedDt` (1/60 s), rattrapé au plus
   `maxFixedSteps` (5) fois par image ;
4. `UPDATE` ;
5. `POST_UPDATE`.

À l'intérieur d'une étape, les systèmes s'exécutent par `order` croissant,
puis dans l'ordre d'ajout.

| Membre | Rôle |
|---|---|
| `Add(stage, nom, fn, order = 0, runIf = {})` | Ajoute ou remplace (même nom) ; `runIf(world)` conditionne l'exécution |
| `Remove(nom)`, `SetEnabled(nom, bool)`, `Has(nom)` | Gestion |
| `Update(world, dt)` | Une image complète, puis rotation des files d'évènements |
| `FixedAlpha()`, `Frame()`, `Commands()` | Interpolation, numéro d'image, tampon partagé |
| `Stats()` | `SystemStats {name, stage, enabled, lastMicros, totalMicros, runs}` |

`SystemContext` contient `{world, commands, dt, frame}`. Le tampon
`commands` est appliqué **après chaque étape**.

### Évènements (`events.hpp`)

- `EventsOf<T>(world)` rend la file des évènements `T`, rangée dans les
  ressources et créée au premier usage.
- `Send(T)` émet un évènement.
- `ForEach(fn)` parcourt tous les évènements vivants.
- `Read(cursor, fn)` ne livre que ceux postérieurs au curseur : chaque
  lecteur traite chaque évènement une seule fois.
- Un évènement vit **deux images**. Un lecteur placé avant l'émetteur dans
  l'ordre d'exécution ne perd donc rien.

### Relations (`relations.hpp`)

- **Hiérarchie** :
  - `SetParent(world, enfant, parent)` rend faux si l'opération créerait un
    cycle ; `RemoveParent`, `ParentOf` et `ChildrenOf` complètent la
    hiérarchie ;
  - `DespawnRecursive(world, e)` détruit l'entité et sa descendance, et émet
    `EntityDespawned`.
  - Les composants `ChildOf` et `Children` sont tenus cohérents par ces
    fonctions.
- **Relations typées** : `Relation<Tag>{cible}` ; `RelatedTo<Tag>(world, cible)`
  et `PruneRelations<Tag>(world)`, qui retire les relations vers des cibles
  mortes.

## Exemple

```cpp
#include <cstdio>
#include <ecs/ecs.hpp>
#include <ecs/events.hpp>
#include <ecs/relations.hpp>
#include <ecs/schedule.hpp>

struct Position { float x = 0, y = 0; };
struct Velocity { float dx = 0, dy = 0; };
struct Health { int hp = 100; };
struct Gravity { float g = -9.8f; };
struct Died { ecs::Entity who; };

int main() {
	ecs::ArchetypeRegistry world;
	world.resources.Insert(Gravity{});

	ecs::Entity ship = world.SpawnBundle(Position{0, 10}, Velocity{1, 0}, Health{100});
	ecs::Entity rock = world.SpawnBundle(Position{5, 5}, Velocity{0, 0});
	world.AddComponent(rock, Health{5}); // migration vers l'archétype {Position, Velocity, Health}
	ecs::Entity turret = world.SpawnBundle(Position{0, 0});
	ecs::SetParent(world, turret, ship);

	ecs::Schedule schedule;
	schedule.Add(ecs::Stage::FIXED_UPDATE, "physique", [](ecs::SystemContext &ctx) {
		float g = ctx.world.resources.Get<Gravity>().Unwrap()->g;
		ctx.world.Query<Position, Velocity>([&](ecs::Entity, Position &p, Velocity &v) {
			v.dy += g * ctx.dt;
			p.x += v.dx * ctx.dt;
			p.y += v.dy * ctx.dt;
		});
	});
	schedule.Add(ecs::Stage::UPDATE, "dégâts", [](ecs::SystemContext &ctx) {
		ctx.world.Query<Health>([&](ecs::Entity e, Health &h) {
			h.hp -= 10;
			if (h.hp <= 0) {
				ecs::EventsOf<Died>(ctx.world).Send(Died{e});
				ctx.commands.Despawn(e); // différé : appliqué après l'étape
			}
		});
	});
	uint64_t cursor = 0;
	schedule.Add(ecs::Stage::POST_UPDATE, "annonces", [&cursor](ecs::SystemContext &ctx) {
		ecs::EventsOf<Died>(ctx.world).Read(cursor, [](const Died &d) { std::printf("entité %u détruite\n", d.who.id); });
	});

	for (int frame = 0; frame < 3; ++frame)
		schedule.Update(world, 1.f / 60.f);

	if (auto p = world.GetComponent<Position>(ship); p.IsSome())
		std::printf("vaisseau : vivant=%d y=%.3f\n", world.IsAlive(ship), p.Unwrap()->y);
	std::printf("entités : %zu, enfants du vaisseau : %zu\n", world.EntityCount(), ecs::ChildrenOf(world, ship).size());
	world.Query<Position, ecs::Without<Velocity>>([](ecs::Entity e, Position &) { std::printf("statique : %u\n", e.id); });
	for (const ecs::SystemStats &s : schedule.Stats())
		std::printf("%-10s %llu passages\n", s.name.CStr(), (unsigned long long)s.runs);
	return 0;
}
```

Sortie :

```
entité 1 détruite
vaisseau : vivant=1 y=9.984
entités : 2, enfants du vaisseau : 1
statique : 2
```

## Intégration

`modules = ["ecs"]`.

- Un `ArchetypeRegistry` par monde : l'UI de chaque fenêtre a le sien, et le
  jeu a le sien.
- Un composant est n'importe quel type déplaçable. Il n'y a ni enregistrement
  ni macro à écrire.
- `Despawn` ne détruit pas les enfants. Utilisez `DespawnRecursive` pour
  une hiérarchie.

## Tests

```sh
cpp-maker test -m ecs
```

`test_ecs.cpp` couvre le stockage, les requêtes, les filtres, les ressources
et les relations ; `test_schedule.cpp` couvre les étapes, le pas fixe et les
évènements.
