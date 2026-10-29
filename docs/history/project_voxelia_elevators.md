---
name: project-voxelia-elevators
description: "Ascenseurs et backrooms à niveaux (2026-10-27) : mod « elevators » (cabine mobile, portes, machine à états, transfert entre dimensions), escaliers du core, un niveau backrooms par dimension avec étages, escaliers et gaine."
metadata:
  type: project
---

Inspiré des mods Luanti `elevator`, `stairs` (Stairs Redo, MIT : modèles OBJ
repris dans `core/models/stairs/`) et du jeu `backroomtest`.

## Moteur (libs/infrastructure/voxel)

- **Assemblage de blocs mobile** : `SpawnStructure` (entité `STRUCTURE_KIND`,
  `Visual::structure` = schéma partagé, `structureSerial` pour remailler),
  maillé par `ChunkMesher::BuildStructure` dans un monde temporaire ; voxelia
  le dessine (`WorldRenderer::UpdateStructure` : opaque, lumineux, translucide,
  sans ombre). Script : `world.spawn_structure(clip, x, y, z [, floor])`,
  `world.structure_set(entité, lx, ly, lz, bloc [, param])`.
- **`Carrier`** (système « carriers », priorité 11) : les corps debout dans la
  boîte `ride` d'une plateforme mobile sont emportés, posés sur son plancher,
  gardés sous son plafond, sans dégâts de chute.
- `Game::SetBlockState` change aussi les parties d'un bloc multi-cellules
  (haut des portes).
- **Générateur maze** : `floors` (nombre ou liste de levels par étage, sol de
  l'étage k en `floor + k(height+1)`, graine `seed + 7919k`), `stairs`
  (`block`, `fill`, `chance` : volée vers +x et trémie), `elevator` (gaine 7×7
  en `cell`, palier par étage côté `side`, `pit`, `overrun`, moteur au sommet).

## Mod `elevators` (voir son README)

Scripts découpés par rôle (registre, construction, portes, afficheurs, sons,
mouvement, destinations, commandes). Machine à états explicite (`EL_STATES`,
transitions refusées sinon). Trajet : profil trapézoïdal, la cabine (blocs à
l'arrêt) devient une structure mobile pendant le trajet, recalée par
rétroaction de vitesse, reposée en blocs à l'étage. Sauvegarde : `world.data`
par dimension + la cabine en structure (`elevator_cabin_<id>`), reprise au
chargement. Textures (3 styles, afficheurs 7 segments) et sons générés par des
scripts Python hors dépôt.

**Transfert entre dimensions** : un seul profil sur D1 (gaine de départ) + D2
(6 blocs, gaine d'arrivée) ; à D1 la cabine est reposée au départ, le joueur
voyage, et `dimension_changed` fait apparaître une cabine identique au même
instant du profil ; le canal sonore du moteur continue. Destinations déclarées
en JSON (`elevators.destinations`, modes `elevator` et `surface` avec kiosque),
« Retour » (`__back`) à l'arrivée.

Pièges rencontrés : `structure.save` refuse un nom avec « / » (sans erreur
visible) ; affecter `nil` à une clé de map la garde (`el_clear_active`) ;
insérer des champs au milieu de `Visual` cassait les initialisations agrégées
(champs ajoutés à la fin) ; une destination extérieure demandée portes
ouvertes se perdait à la fermeture (`wants_out`).

## Backrooms

Niveaux 0, 2, 4, 5, 37 = dimensions distinctes, 1 à 3 étages, escaliers,
ascenseur en cellule [1, 0] ; chaîne 0 → 2 → 4 → 5 → 37 → surface.

Tests : `demos/voxelia/tests/test_elevators.cpp` (A trajet, B appels, C
niveaux/étages, D+E changement de niveau et surface, F passagers, G reprise,
H sans backrooms, I cas limites) ; `cpp-maker test asan -m voxelia`.
Limites : un seul monde chargé, donc seul le joueur local change de
dimension ; contrepoids et poulies non animés.

## Nombre d'étages réglable (2026-10-28)

Réglages du mod backrooms : `number_of_floors_per_level` (3 à 16, défaut 4)
et `floors_level_<n>` (0 = réglage général). Moteur : dans un générateur,
`{"setting": "id", "fallback": …}` vaut le réglage du mod
(`Game::ResolveSettings`), FIGÉ dans le monde à la première lecture
(metadata `settings.modSettings`, aussi rendu par `voxel.setting`) ; maze
`"floors": {"count", "levels"}` (liste répétée). Ascenseurs : destination
`"motor": "auto"` = `world.maze_shaft(dimension)` (moteur de la gaine
générée) ; `world.load(boîte)` charge la gaine entière avant sa mise en
service et la cabine avant un départ (sinon, avec 16 étages, le moteur à
y = 102 n'est jamais chargé depuis le bas). Test
`Backrooms_FloorsFollowTheModSettings`.

## Sortie du dernier étage vers la surface (2026-10-28)

Générateur maze : `"elevators": [...]` (plusieurs gaines, `id`, `floors` =
all / first / last / [de, à]) ; hors de ses étages la cellule d'une gaine est
une salle ordinaire ; `MazeElevatorMotor(id)`, `world.maze_shaft(dim, id)`.
Backrooms : niveau 37 = gaine « main » (arrivée) + gaine « surface » (dernier
étage seulement, cellule [−1, 0], fosse 1). Ascenseurs : destination
`"shaft"`, `"fromShaft"` (offerte par cette gaine seule, préparée
automatiquement) ; style de cabine déduit des paliers générés. Test D+E :
la surface n'est plus proposée par l'ascenseur d'arrivée, elle part du
dernier étage.
