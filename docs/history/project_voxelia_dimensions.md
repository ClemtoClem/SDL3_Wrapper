---
name: project-voxelia-dimensions
description: "Dimensions dans voxelia (2026-10-26) : plusieurs mondes par sauvegarde, déclarés par les mods ; 5 dimensions du mod core, backrooms (mort par suffocation), sables mouvants, gravité inversée."
metadata:
  type: project
---

**Moteur (libs/infrastructure/voxel)**
- `"type": "dimension"` (DimensionDef) : générateur, ciel, heure figée,
  lumière, gravité, bornes d'apparition, `enterOnDeath` (causes de mort qui
  y mènent), portail de retour posé à l'arrivée.
- Un seul monde ACTIF (celui du joueur) : `Game::TravelTo` enregistre le
  monde courant (chunks, entités) puis crée l'autre — généré à l'arrivée,
  puis chunk par chunk. Voyage différé à la fin du pas s'il est demandé
  pendant un pas (portail, mort, script). `WorldSerial()` avance : le
  rendu repart de zéro.
- Sauvegarde : clés de chunks « dimension|x,y,z » (le monde ordinaire garde
  « x,y,z »), « dimension » dans les entités et le joueur ; données libres
  par dimension (`DimensionData`, `world.data` côté script).
- Portails : blocs `"portal": {"dimension": id | "@return", "delay"}` ;
  champs `"physics.gravityField": ±1`.
- Générateurs natifs : "islands" (+ couche « mirror » retournée, débris),
  "caverns", "maze" (niveaux, sorties) ; décorations procédurales
  "giant_tree", "spire", "patch", "cluster" (« where » : ceiling). Un biome
  `"dimension"` ne s'invite pas dans un générateur sans liste de biomes.
- Physique : sables mouvants (`physics.sinking`), joueur pris dans un bloc
  plein (`trapped`), gravité inversée (`gravityDirection`), suffocation
  (jauge d'air, `Survival::suffocating`), cause de mort (`Health::lastCause`).
- Règle de monde `fallingBlockSuffocation` (2026-10-27, active par défaut) :
  désactivée, un bloc tombé sur la tête (sable, gravier…) immobilise encore
  le joueur mais ne l'étouffe plus ; les sables mouvants et les blocs pleins
  non tombants étouffent toujours. Création du monde (Règles de survie),
  paramètres du monde, réglages en jeu.
- Tests : voxel `test_dimensions.cpp`, voxelia `Mods_EveryDimensionCanBeVisited`,
  `Mods_QuicksandSuffocationWakesThePlayerInTheBackrooms`.

**Contenu** : `mods/core` (world/dimensions.json, dimension_*.json,
blocks/items/recipes/creatures « dimensions.json », scripts/dimensions.script)
et `mods/backrooms` (world.json, init.script). Le JSON des dimensions du mod
core a été produit par un script Python (textures composées vérifiées).

**Application** : vue retournée en gravité inversée (souris et pas de côté
inversés), ciel/lumière par dimension, effet « darkness », objet lumineux
tenu = lumière, écran assombri et bulles sur fond rouge pendant la
suffocation. Corrigé au passage : la musique du menu redémarrait pendant
les deux images de chargement d'une partie (`Shell::StopMenuMusic`).

**Lumière recalculée au fil du déplacement (2026-10-26, suite)** : un chunk
qui arrive fait refaire ses six voisins et les chunks en dessous (colonnes
3 × 3) ; un bloc qui change d'opacité aussi. L'occlusion ambiante ne compte
plus un chunk non chargé comme plein (« carré sombre » d'un chunk maillé
avant ses voisins). Lumière du ciel par sommet dans le mailleur : blocs
opaques au-dessus comptés sur les chunks chargés, estimés au-delà par le
générateur (`ChunkGenerator::SkyCover`) ; désactivable par dimension
(`"light": {"sky": false}` : îles flottantes, noyau, backrooms).
Tests : `Mesher_LightIsRecomputedAsChunksArrive`, `Mesher_CavesUnderUnloadedTerrainAreDark`.
