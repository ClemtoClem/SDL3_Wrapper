---
name: project-voxelia-furniture
description: "Mobilier dans voxelia (2026-10-26) : blocs de plusieurs cellules, portes/trappes/volets à bascule, lits (réapparition, nuit), rangements enregistrés ; ressources tirées des mods Minetest."
metadata:
  type: project
---

**Moteur (libs/infrastructure/voxel)**
- Orientation : `FacingParam(yaw)` — à l'orientation 0 le joueur regarde vers +z et la face « nord » du bloc lui fait face. Corrige l'ancien calcul (escaliers posés à l'envers vers l'est et l'ouest). Le mailleur tourne aussi les TEXTURES avec le bloc (cubes et nodeboxes : façade d'un coffre, dessus d'un lit) ; une boîte qui dépasse de sa cellule prend les UV de la cellule voisine (volets ouverts).
- Blocs de plusieurs cellules : `"parts": [{"offset", "block"}]` sur le bloc principal, `"partOf": [dx, dy, dz]` sur les autres (décalages à l'orientation 0, tournés par `RotateOffset`). Posés ensemble (toutes les cellules libres), cassés ensemble (butin du bloc principal seulement). `Game::MainPart`.
- Clic droit (`Game::UseBlock`, avant la pose de l'objet tenu ; accroupi : on pose) : `"interact": {"toggle": id, "sound"}` bascule le bloc et ses parties (orientation gardée) ; `"interact": {"action": "sleep"}` : lit (`Game::Sleep` — point de réapparition, nuit passée sauf monstre à moins de 8 blocs ou dimension sans cycle) ; `"container": {"slots", "title", "columns", "openSound", "closeSound"}` : rangement (`UiRequest` « container »).
- Rangements : `Game::BlockInventory(pos)`, table `containers` de world.db (clé « [dimension|]x,y,z », créée à l'ouverture des anciennes bases) ; contenu lâché quand on casse le meuble, oublié si le bloc est remplacé.
- Joueur : `hasBed / bed / bedDimension` enregistrés ; réveil sur le lit après la mort s'il existe encore.
- Scripts : `world.container`, `world.container_add`, `world.toggle`, `world.use` ; événements `block_toggled`, `container_open`, `player_sleep`. `/setblock x y z bloc [rotation]` pose toutes les cellules.
- `Validate()` signale parties, bascules, actions et sons inconnus.

**Mod core** (`blocks/furniture.json`, `recipes/furniture.json`, catégorie de fabrication « Mobilier ») : 26 portes (22 essences, vitrée, japonaise, verre, fer), 8 trappes, 9 volets, 7 échelles, 16 lits, 8 rangements (coffre, grand coffre, tonneau, caisse, buffet, commode, étagère, armoire). Textures et sons des mods Minetest doors, xdecor, greek, animalia, celevator : `textures/blocks/furniture/CREDITS.txt`. Contenu généré par des scripts Python (furniture_assets.py, furniture.py) hors du dépôt.

**Voxelia** : écran de rangement (grille du meuble + inventaire, Maj + clic, « Tout ranger » / « Tout prendre », fermé si le meuble disparaît ou s'éloigne).

**Tests** : voxel `test_furniture.cpp` (orientation, textures tournées, portes, lit, coffre enregistré) ; voxelia `Mods_EveryPieceOfFurnitureWorks` (pose, usage et casse des 74 meubles du mod core).
