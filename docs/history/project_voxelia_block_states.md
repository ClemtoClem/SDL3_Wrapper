---
name: project-voxelia-block-states
description: "États de blocs et capteurs (2026-10-27) : « states » JSON (un BlockDef par état, textures/lumière/interaction propres), capteurs de présence, néons des backrooms à détecteur ; bassins au ras du sol ; liquides réveillés par les corps qui y entrent."
metadata:
  type: project
---

- Blocs à ÉTATS (`"states": {nom: surcharges}`, `"defaultState"`) :
  `ContentRegistry::LoadBlockStates` fusionne chaque état dans la définition
  (objets complétés clé par clé : `graphics.textures.bottom` seul suffit) et
  charge un BlockDef par état — l'état par défaut garde l'id du bloc, les
  autres `id_état` (ou leur `"id"`) : les anciens ids (`fluorescent_off`…)
  restent valides pour les sauvegardes. Les autres états : pas d'objet,
  `drops` = le bloc. Un nom d'état dans `interact.toggle`, `sensor.near/far`
  désigne le bloc de cet état. `BlockDef::stateGroup/stateName/stateIds`,
  `ContentRegistry::BlockState`, `Game::SetBlockState`, scripts
  `world.state(x, y, z)` / `world.set_state(x, y, z, nom)`. Le mailleur,
  l'atlas, la lumière n'ont rien de spécial à faire : chaque état est un bloc.
- CAPTEURS (`"sensor": {radius, near, far, sound}`) : système « sensors »
  (toutes les 0,25 s) — un joueur à moins de `radius` fait passer à `near`,
  plus aucun à `far` ; le son est joué en quittant l'état qui le porte. Les
  blocs à l'état « proche » sont surveillés même hors de portée (joueur
  téléporté loin).
- backrooms : néons encastrés, plafonniers d'hôpital, réglettes de parking =
  un bloc chacun, états on / off (/ half) / auto (détecteur, éteint) /
  auto_on ; textures « on » / « off » selon l'état. Générateur : `lightAuto`
  + `autoChance` (0,15) en plus de `lightOff` (éteints pour de bon) et
  `lightHalf`.
- Bassins des piscines : `maze.poolDepth` (2) — eau du sol vers le bas, plus
  rien au-dessus ; fond au sol du niveau, bords carrelés (mur du niveau) au
  lieu du néant.
- Liquides : `FluidSimulation::Disturb(boîte)` ; le système « fluids »
  l'appelle pour chaque corps (`PhysicsBody::inFluid` : joueur, créature,
  bloc qui tombe…) à chaque cellule franchie (`stirredCell`) — un liquide
  généré instable se remet à couler quand on y entre.

Tests : `Game_BlockStatesSwapTexturesAndFollowSensors`,
`Fluids_BodiesWakeUpGeneratedLiquid`, bassins dans
`WorldGen_MazeLampStatesAndExitSigns`.
