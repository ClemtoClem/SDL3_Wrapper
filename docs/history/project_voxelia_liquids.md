---
name: project-voxelia-liquids
description: "Physique des liquides dans voxelia (2026-10-27) : écoulement (sources, niveaux, chute), sources renouvelables, lave + eau, rendu en pente, courant, seaux, règle « liquidFlow »."
metadata:
  type: project
---

**Moteur (libs/infrastructure/voxel)** — `fluids.hpp` / `fluids.cpp`
- État d'une cellule dans le param, bits 2 à 6 (`LiquidLevel`,
  `LiquidFalling`, `LiquidParam`) : niveau 0 = source, 1..15 = distance à une
  source, bit « tombant ». Les bits 0-1 (orientation) restent libres : coller
  ou tourner une structure ne change pas une source en eau qui coule ; un
  seau posé (orientation du regard) donne bien une source.
- `FluidSimulation` (une par monde, détruite avant lui) : branchée sur
  `onBlockChanged` (sauf génération : lacs et océans restent calmes) et
  `onChunkLoaded` (un chunk relu reprend l'écoulement de ses cellules qui
  coulent). File d'échéances par cellule, pas « liquid.tick » du liquide le
  plus rapide autour. Règles : chute d'abord ; étalement latéral seulement si
  le liquide ne peut pas descendre (une source s'étale toujours) ; niveau le
  plus bas des voisins + 1 jusqu'à « liquid.range » ; retrait sans
  alimentation ; source renouvelable entre deux sources sur un sol ;
  « liquid.mix » (lave : source → obsidienne, coulante → pierre) ; blocs
  « replaceable » emportés (`onWashedAway` : butin lâché par le Game).
  `Step(dt, budget)`, `Settle()` pour les tests.
- `LiquidHeight` (7/8 pour une source, décroissante, pleine sous le même
  liquide) et `LiquidFlow` (pente vers les niveaux plus hauts et les chutes).
- Contenu : `"liquid": {range, tick, renewable, current, flowingTexture,
  mix: [{with, source, flowing}]}` ; `"liquid": false` : fluide immobile.
- Rendu : surface en pente (hauteur de chaque coin = moyenne des cellules du
  même liquide qui le partagent), texture « flowingTexture » pour les cellules
  qui coulent (ajoutée à l'atlas) ; un liquide ne cache plus les faces de ses
  voisins (lave basse).
- Physique : `PhysicsBody::current` ; le courant décale la vitesse visée
  (objets et créatures dérivent), une chute d'eau pousse vers le bas.
- Seaux : objet « liquids » {liquide: seau plein} (le rayon traverse le
  liquide qui coule jusqu'à une source) ; seau plein « emptiesTo ».
- Règle de monde « liquidFlow » (création du monde, paramètres du monde).

**Contenu** : eau (portée 7, 0,25 s, renouvelable), lave (portée 3, 1,5 s,
figée par l'eau et la source chaude), source chaude, miel, marais toxique,
eau des backrooms ; seau vide, seaux d'eau et de lave.

**Tests** : voxel `test_fluids.cpp` (niveaux, étalement et retrait, chute,
renouvellement et murs, lave + eau et herbe emportée, pente du maillage,
seaux, courant, règle).
