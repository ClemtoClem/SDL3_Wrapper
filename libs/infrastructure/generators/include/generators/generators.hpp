#pragma once
/**
 * generators — boîte à outils de génération procédurale :
 *
 *   random.hpp     hasard reproductible (PCG32, hachages de coordonnées)
 *   noise.hpp      bruits Perlin / Simplex / Value / Worley, fractales, distorsion
 *   heightmap.hpp  cartes de hauteur : mélanges, filtres, masques, érosions, export
 *   terrain.hpp    terrain en couches (à la TerraForge3D), biomes, maillage, OBJ
 *   scatter.hpp    dispersion (disques de Poisson, grille bruitée)
 *   dungeon.hpp    labyrinthes, salles et labyrinthes, BSP, grottes
 *   lsystem.hpp    L-systèmes et tortue 3D (plantes, arbres)
 *   wfc.hpp        Wave Function Collapse (tuiles)
 *   markov.hpp     noms procéduraux (chaîne de Markov)
 *   maze.hpp       grille d'entités et configuration 3D des labyrinthes
 */

#include "generator.hpp"
#include "maze.hpp"
#include "random.hpp"
#include "noise.hpp"
#include "heightmap.hpp"
#include "terrain.hpp"
#include "scatter.hpp"
#include "dungeon.hpp"
#include "lsystem.hpp"
#include "wfc.hpp"
#include "markov.hpp"
