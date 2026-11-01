# voxel — moteur de jeu voxel piloté par les données

`libs/infrastructure/voxel` · couche **infrastructure** · `libvoxel.a` · espace de noms `voxel::`

## Présentation

`voxel` est le moteur du jeu [voxelia](demo_voxelia.md). La classe `Game` est
**une partie complète, sans fenêtre ni GPU**. Elle regroupe :

- le contenu, chargé depuis des **mods** (JSON et scripts) ;
- le monde en chunks cubiques de 16³, sa génération et ses dimensions ;
- les entités ECS : joueurs, créatures, objets au sol, blocs qui tombent ;
- la physique voxel, les liquides et l'éclairage ;
- les inventaires et la fabrication ;
- les sauvegardes et la console.

L'application n'y ajoute que l'affichage, le son et les entrées. Les tests
pilotent ainsi une partie entière sans rien de tout cela.

| Sous-système | Fichier | Contenu |
|---|---|---|
| Coordonnées | `coords.hpp` | `BlockPos`, `ChunkPos`, `Face`, `Box` ; monde en flottants (Y vers le haut), le bloc (x, y, z) occupe [x, x+1[³ |
| Contenu | `content.hpp`, `json.hpp`, `mods.hpp` | `ContentRegistry` ; définitions `BLOCK`, `ITEM`, `CREATURE`, `ENTITY`, `RECIPE`, `BIOME`, `STRUCTURE`, `GENERATOR`, `TEXTURE`, `MODEL`, `SOUND`, `SOUND_SET`, `SHADER`, `BEHAVIOR`, `CONFIG`, `DIMENSION` ; lecture JSON tolérante ; `ModLoader`, `ModInfo`, `ModReport` |
| Monde | `world.hpp` | `World`, `Chunk`, `ChunkStorage`, `ChunkGenerator`, `BlockChange`, `ChangeCause`, `BlockCursor` (une seule recherche de chunk par changement de chunk) |
| Génération | `worldgen.hpp` | `TerrainGenerator` : `flat`, terrain à biomes, labyrinthes à étages (`MazeLayout`), structures |
| Entités | `components.hpp`, `entities.hpp` | Composants ECS (`Health`, `Collider`, `MoveIntent`, `Behaviors`, `Equipment`, `ItemStack`…) et fabriques ; comportements (IA) ; évènements `EntityDied`, `DimensionChanged`, `SoundEvent`, `GameMessage` |
| Physique | `physics.hpp` | Collisions avec les blocs (cube, *nodebox*, orientés), déplacement et marches, rayons vers les blocs et les entités (`BlockHit`, `EntityHit`) |
| Liquides | `fluids.hpp` | `FluidSimulation` : sources, niveaux, chute, retrait, courant, sources renouvelables |
| Lumière | `lighting.hpp`, `raygrid.hpp` | Sources, transport direct (inverse du carré, cônes), occlusion ; grille des rayons réfléchis pour `render3d::VoxelReflections` |
| Maillage | `meshing.hpp` | `TextureAtlas`, `ChunkMesher`, `ChunkMesh`, `MeshBuffer` ; calcul sur CPU, testable |
| Modèles articulés | `rig.hpp` | `RigModel`, parties, boîtes, clips, courbes de Bézier, mélange de clips |
| Constructions | `schematic.hpp`, `formula.hpp` | `Schematic` (copier, coller, faire pivoter, contenu des rangements), `Formula` (constructions par formule mathématique) |
| Inventaires | `inventory.hpp` | `Inventory`, `CraftGrid`, `InventoryComponent` |
| Ressources | `assets.hpp` | `AssetManager` sur `resources::ResourceCache` : images, textures, modèles, rigs |
| Sauvegardes | `save.hpp` | `WorldSave`, `WorldMetadata` : un dossier par monde (`world.json`, chunks, entités, structures), base `sql` |
| Scripts | `scripting.hpp` | `ScriptHost` : chargement et validation des scripts des mods, espaces `voxel`, `game`, `world`, `entity`, `content`, `gen` |
| Partie | `game.hpp` | `GameOptions`, `GameMode`, `Difficulty`, `UiRequest`, `Game` |

Dépendances : `core`, `diagnostics`, `math`, `events`, `dataio`, `ecs`,
`generators`, `resources`, `script`, `sql`, `archive`.

## Fonctionnement d'une partie

Les systèmes `ecs::Schedule` de `Game` :

| Étape | Systèmes |
|---|---|
| `PRE_UPDATE` | `world.stream` : chunks autour des joueurs |
| `FIXED_UPDATE` | `ai`, `physics`, `portals` (portails, champs de gravité), `falling_blocks`, `items` (ramassage, fusion, durée de vie), `health` (chutes, morts, butin) |
| `UPDATE` | `scripts.tick`, `sounds.idle` |
| `POST_UPDATE` | `events.dispatch` (évènements ECS → scripts) |

Un mod peut ajouter ses propres systèmes (`voxel.system`).

**Dimensions** :

- un seul monde est **actif** à la fois, celui du joueur ;
- changer de dimension enregistre et décharge le monde courant, puis ouvre
  l'autre ;
- chaque dimension garde ses chunks, ses entités et ses données dans la même
  sauvegarde.

**Actions** : casser ou poser un bloc, utiliser un objet, interagir,
blesser, faire apparaître une entité, fabriquer, téléporter…

- Elles sont offertes aux scripts sous forme d'évènements : `block_broken`,
  `block_placed`, `entity_died`, `player_join`…
- Un script peut **annuler** une action en rendant `false`.

### Structure d'un mod

```
mods/<mod>/
  mod.json             {"id", "name", "version", "description", "dependencies": [...], "optionalDependencies": [...]}
  **.json              définitions (un objet ou un tableau d'objets par fichier)
  textures/…/x.png     enregistrées d'office sous l'id « mod:x »
  sounds/…/x.wav|ogg   idem (sons)
  models/…/x.obj|b3d   idem (modèles)
  shaders/x.vert + x.frag   shader « mod:x »
  init.script, scripts/…/x.script   exécutés après les JSON du mod
```

Ordre de chargement des mods :

- ils sont chargés dans l'ordre de leurs **dépendances**, puis par nom ;
- une dépendance manquante ou circulaire écarte le mod ;
- un mod peut **surcharger** une définition (même id) d'un mod dont il
  dépend.

Les mods livrés sont dans `demos/voxelia/mods` : `core`, `backrooms`,
`edit` et `elevators` (voir [voxelia](demo_voxelia.md#fonctionnalités-du-jeu-mods-livrés)).

## API publique : `Game`

| Domaine | Membres |
|---|---|
| Contenu | `LoadMods(GameOptions)` → `ModReport`, `Content()`, `ReloadDefinition`, `ModSetting`, `UpdateModSettings` |
| Monde | `OpenWorld(nom, graine, générateur)`, `HasWorld`, `GetWorld()`, `StreamChunks(budget)`, `SetViewRadius`, `Save()`, `CloseWorld()` |
| Boucle | `Tick(dt)` |
| Dimensions | `TravelTo(joueur, dimension, position)`, `DimensionData`, `TimeInDimension`, `DimensionGenerator` |
| Règles | `Mode`/`SetMode` (`SURVIVAL`, `CREATIVE`, `ADVENTURE`, `SPECTATOR`), `GetDifficulty`/`SetDifficulty`, `Rule`, `RuleNum`, `SetRule`, `SetCreative` |
| Joueurs et entités | `JoinPlayer(nom)`, `LocalPlayer`, `SpawnEntity(id, pieds)`, `DropItem`, `Damage`, `Kill`, `Interact`, `AddEffect`, `AddXp`, `Eat`, `EquipHeld` |
| Blocs | `BreakTime`, `BreakBlock`, `PlaceBlock`, `UseHeldItem`, `UseBlock`, `ToggleBlock`, `SetBlockState`, `TargetBlock`, `TargetEntity`, `MainPart` |
| Fabrication | `Craft(joueur, recette, fois)`, `CraftBlocker`, `NearbyStations`, `Trade` |
| Interface | `RequestUi` / `TakeUiRequests` : la partie demande à l'application d'ouvrir un écran |
| Console | `Execute(ligne, émetteur)` → `Result<String, String>` |

`GameOptions` :

- `modDirectories` (`{"mods"}`), `disabledMods`, `modOrder`, `modSettings` ;
- `savesDirectory` (`"saves"`), `structureDirectory` ;
- `viewRadius` (6) et `verticalRadius` (3), en chunks ;
- `chunkBudget` (6 chunks par image) ;
- `creative` et `dayLength` (600 s).

Commandes de console intégrées :

- `/help`, `/give`, `/tp`, `/spawn`, `/time`, `/seed`, `/save`, `/recipes`,
  `/craft` ;
- `/fly`, `/creative`, `/kill`, `/heal`, `/clear` ;
- `/setblock`, `/fill`, `/structure`, `/pos` ;
- `/dimension`, `/dimensions`, `/mods`, `/gc`, `/stats`, `/script`.

Les mods en ajoutent avec `voxel.command`.

## Exemple : une partie sans fenêtre

```cpp
#include <cstdio>
#include <voxel/game.hpp>

int main() {
	voxel::GameOptions options;
	options.modDirectories = {"demos/voxelia/mods"}; // core, backrooms, edit, elevators
	options.savesDirectory = "data/voxel-doc-saves"; // dossier des sauvegardes
	options.viewRadius = 3;

	voxel::Game game; // la partie complète, sans fenêtre ni GPU
	voxel::ModReport report = game.LoadMods(options);
	for (const voxel::ModInfo &mod : report.mods)
		std::printf("mod %s %s\n", mod.id.CStr(), mod.version.CStr());
	for (const String &e : report.errors)
		std::printf("erreur : %s\n", e.CStr());

	if (auto opened = game.OpenWorld("doc", Some(1234u)); opened.IsError()) {
		std::printf("monde : %s\n", opened.Error().CStr());
		return 1;
	}
	ecs::Entity player = game.JoinPlayer("Ada");
	for (int i = 0; i < 120; ++i) { // 2 s de jeu : streaming des chunks, IA, physique…
		game.StreamChunks(16);
		game.Tick(1.f / 60.f);
	}

	// Les actions passent par la console, comme pour un joueur ou un script
	for (const char *cmd : {"/give core:torch 4", "/tp 0 40 0", "/setblock 2 40 0 core:stone"})
		if (auto r = game.Execute(cmd, player); r.IsOk())
			std::printf("%s → %s\n", cmd, r.Value().CStr());
		else
			std::printf("%s → erreur : %s\n", cmd, r.Error().CStr());

	std::printf("bloc en (2, 40, 0) : %s\n", game.GetWorld().BlockAt(voxel::BlockPos{2, 40, 0}).id.CStr());
	(void)game.Save();
	game.CloseWorld();
	return 0;
}
```

Sortie (exécuté depuis la racine du dépôt) :

```
mod core 1.0.0
mod elevators 1.1.0
mod backrooms 1.0.0
mod edit 1.0.0
/give core:torch 4 → 4 × core:torch
/tp 0 40 0 → téléporté
/setblock 2 40 0 core:stone → bloc posé
bloc en (2, 40, 0) : core:stone
```

Extrait d'un script de mod (`demos/voxelia/mods/core/init.script`) :

```
voxel.on("player_join", fn(player) {
    game.say("Bienvenue ! …")
})
voxel.command("sethome", "/sethome", "mémorise la position actuelle", fn(sender, args) {
    let p = entity.pos(sender)
    entity.set_prop(sender, "home", [p.x, p.y, p.z])
    return "maison mémorisée"
})
```

## Intégration

`modules = ["voxel"]`.

- **Contenu** : le moteur ne contient aucun bloc ni objet. Tout vient des
  mods, qu'il faut fournir (`GameOptions::modDirectories`).
- **Rendu** : l'application maille les chunks (`ChunkMesher`) et envoie les
  tampons à `render3d`. Voxelia le fait sur des fils de maillage, à partir
  d'instantanés du monde.
- **Performances** :
  - limitez le chargement par image avec `StreamChunks(budget)` ;
  - les zones `voxel.tick`, `voxel.mesh_chunk`… apparaissent dans les
    captures du [profiler](demo_profiler.md).

## Tests

```sh
cpp-maker test -m voxel
```

Une centaine de tests couvrent :

- le contenu et les dimensions ;
- les liquides, le mobilier, la partie complète, les inventaires ;
- l'éclairage, le maillage, la physique ;
- la grille de rayons, les rigs, les structures et le monde.
