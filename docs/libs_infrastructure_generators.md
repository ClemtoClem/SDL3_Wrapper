# generators — génération procédurale

`libs/infrastructure/generators` · couche **infrastructure** · `libgenerators.a` · espace de noms `generators::`

## Présentation

`generators` réunit des outils de génération procédurale **reproductibles** :
une même graine produit le même résultat sur toutes les machines.

| Domaine | Contenu |
|---|---|
| Hasard | `Rng` (PCG32), hachages de coordonnées entières (`Hash*`, `HashFloat2/3`) |
| Bruit | Perlin, Simplex, Value et Worley ; fractales FBM, Ridged, Billow et Ping-pong ; distorsion de domaine |
| Cartes de hauteur | Mélanges, masques, filtres (courbe, terrasses, flou), érosion hydraulique et thermique, cartes de pente et de normales, export PGM, RAW 16 bits et PPM |
| Terrain | Pile de couches de bruit (dans l'esprit de TerraForge3D), île, mer, biomes (hauteur × pente), maillage avec export OBJ |
| Niveaux sur grille | Labyrinthes parfaits (retour sur trace, Prim, arbre binaire), *Rooms and Mazes* (Bob Nystrom), BSP, grottes par automate cellulaire, champ de distances, régions, entrée et sortie |
| Wave Function Collapse | Grille de tuiles soumise à des règles d'adjacence |
| L-systèmes | Réécriture stochastique et tortue 3D (plantes, arbres) |
| Dispersion | Disques de Poisson (Bridson), avec densité variable ; grille perturbée |
| Noms | Chaîne de Markov de caractères |
| Labyrinthes 3D | `generators::maze` : grille d'entités ECS et configuration 3D (`Config3D`, `Room3D`, `EntityMap`) |

Ces outils servent à [`voxel`](libs_infrastructure_voxel.md) (génération
du monde, labyrinthes des backrooms), à la [sandbox](demo_sandbox.md) et aux
scripts, à travers [`script_bindings`](libs_infrastructure_script_bindings.md).

## Architecture interne

| En-tête (`include/generators/`) | Contenu principal |
|---|---|
| `generators.hpp` | En-tête parapluie |
| `generator.hpp` | `Generator<Array>` : interface `Result<Array, String> Generate()` |
| `random.hpp` | `Rng` (`Seed`, `Next`, `Float`, `Range`, `Chance`, `Normal`, `Shuffle`, `InDisk`…), hachages |
| `noise.hpp` | `NoiseType`, `FractalType`, `CellularReturn`, `DistanceMetric`, `NoiseSettings`, `Noise` (`Sample`, `Sample01`, `Perlin2/3`, `Simplex2/3`, `Value2`…) |
| `heightmap.hpp` | `BlendMode`, `Blend`, `HydraulicErosion`, `ThermalErosion`, `HeightMap` |
| `terrain.hpp` | `TerrainLayer`, `TerrainSettings`, `DefaultTerrain`, `GenerateTerrain`, `BiomeRule`, `DefaultBiomes`, `ClassifyBiome`, `BiomeWeights`, `TerrainMeshSettings`, `TerrainMesh`, `BuildTerrainMesh` |
| `dungeon.hpp` | `Tile`, `Grid<T>`, `Rect`, `Cell`, `Dungeon`, `DistanceField`, `Regions`, `CarveMaze`, `GenerateMaze`, `GenerateRoomsAndMazes`, `GenerateBsp`, `GenerateCaves` |
| `wfc.hpp` | `WfcTile`, `WfcResult`, `WaveFunctionCollapse` (`Constrain`, `Run`) |
| `lsystem.hpp` | `LRule`, `ExpandLSystem`, `TurtleSettings`, `TurtleSegment`, `InterpretTurtle` |
| `scatter.hpp` | `Point2`, `PoissonDisk`, `JitteredGrid` |
| `markov.hpp` | `MarkovNames` |
| `maze.hpp` | `generators::maze::Config3D`, `Room3D`, `EntityMap` |

Dépendances : `core`, `math`, `ecs` (pour `maze.hpp`) et `dataio`.

## API publique : points clés

- **`Rng`** : PCG32 défini au bit près. Les distributions de `<random>`
  dépendent de l'implémentation de la bibliothèque standard, celles-ci non.
- **`HeightMap(w, h, remplissage)`** :
  - toutes les opérations sont **en place et chaînables** : `AddNoise`,
    `Combine`, `Normalize`, `Terrace`, `ErodeHydraulic`,
    `ErodeThermal`, `Resized`… ;
  - cartes dérivées : `SlopeMap`, `HeightMask`, `SlopeMask` ;
  - export : `SavePgm(chemin, 16bits)`, `SaveRaw16`, `SavePpm(chemin, fnCouleur)`.
    Ils rendent `Option<String>` (l'erreur, ou `NONE`).
- **Terrain** :
  - `GenerateTerrain(TerrainSettings)` applique couches, masques, île,
    courbe, terrasses, érosion et mer ;
  - `BuildTerrainMesh(map, réglages, biomes)` produit un maillage indexé (Y
    vers le haut) avec normales, UV et couleurs de biome ;
  - `TerrainMesh::SaveObj` l'exporte.
- **`Dungeon`** : une grille `Grid<Tile>`, ses salles, son entrée et sa
  sortie. `Rows()` la rend en texte (`TileChar`), `Count(tile)` compte
  un type de case.
- **`WaveFunctionCollapse(tuiles).Run(w, h, graine, essais)`** rend
  `Result<WfcResult, String>`. Il recommence en cas de contradiction, dans
  la limite des essais.
- **`ExpandLSystem(axiome, règles, itérations, graine)`** puis
  **`InterpretTurtle(programme, réglages)`** produisent des segments 3D.

## Exemple

```cpp
#include <cstdio>
#include <generators/generators.hpp>

using namespace generators;

int main() {
	// 1. Carte de hauteur : bruit fractal, normalisation, terrasses, érosion
	NoiseSettings ns;
	ns.type = NoiseType::SIMPLEX;
	ns.fractal = FractalType::RIDGED;
	ns.seed = 42;
	ns.frequency = 0.02f;
	HeightMap map(128, 128);
	map.AddNoise(Noise(ns)).Normalize().Terrace(6, 0.5f).ErodeHydraulic({.droplets = 5000});
	(void)map.SavePgm("terrain.pgm", true);

	// 2. Maillage avec couleurs de biomes, exporté en OBJ
	TerrainMesh mesh = BuildTerrainMesh(map, {.cellSize = 1.f, .heightScale = 30.f}, DefaultBiomes());
	std::printf("terrain : %zu sommets, %zu triangles\n", mesh.VertexCount(), mesh.TriangleCount());
	(void)mesh.SaveObj("terrain.obj");

	// 3. Labyrinthe parfait, reproductible par graine
	Dungeon maze = GenerateMaze(21, 11, 7, MazeAlgorithm::BACKTRACKER);
	for (const String &row : maze.Rows())
		std::printf("%s\n", row.CStr());

	// 4. Dispersion naturelle et noms procéduraux
	std::vector<Point2> trees = PoissonDisk(100.f, 100.f, 6.f, 3);
	MarkovNames names({"Avalon", "Bretagne", "Camelot", "Carduel", "Lothian", "Tintagel", "Brocéliande"}, 2);
	Rng rng(2026);
	std::printf("%zu arbres ; villages : %s, %s\n", trees.size(), names.Generate(rng).CStr(), names.Generate(rng).CStr());
	return 0;
}
```

Sortie :

```
terrain : 16384 sommets, 32258 triangles
#####################
#>#,,,,,,,,,,,#,,,,,#
#,#,#########,#####,#
#,#,#,,,,,,,#,,,,,,,#
#,#,#,#############,#
#,#,#,,,,,,,,,,,,,,,#
#,#,#####,###########
#,#,,,,,#,#,,,,,,,,,#
#,#####,#,#,#######,#
#,,,,,,,#,,,#<,,,,,,#
#####################
180 arbres ; villages : Lothiande, Brocélian
```

## Intégration

`modules = ["generators"]`, puis `#include <generators/generators.hpp>`.
Tout est calculé sur le processeur, sans SDL ni GPU. Le maillage produit
s'envoie à [`render3d`](libs_infrastructure_render3d.md) ou s'exporte avec
[`dataio::SaveMesh`](libs_infrastructure_dataio.md).

## Tests

```sh
cpp-maker test -m generators
```
