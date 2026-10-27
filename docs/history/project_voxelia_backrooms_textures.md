---
name: project-voxelia-backrooms-textures
description: "Backrooms raffinés (2026-10-27) : textures 2048 px gardées en 256 px avec mipmaps, variantes « nom.N.png », néons encastrés ou pendus allumables, panneaux EXIT fléchés vers les sorties, nouveaux murs/sols/plafonds par niveau."
metadata:
  type: project
---

Demande : mod backrooms plus raffiné — nouvelles textures (fournies par
l'utilisateur en 2048 px, cf. `textures/prompt_generation_image.md`), lampes de
plafond allumées / éteintes, lampes EXIT, murs, sols et plafonds neufs.

Moteur :
- `TextureAtlas::Build(assets, content, tileSize, maxTileSize)` : chaque texture
  garde sa résolution (puissance de deux entre 16 et `maxTileSize`, 256 dans
  voxelia) ; réduction par moyenne de zones pondérée par l'alpha (`Resample`) —
  avant, tout était échantillonné au plus proche en 16 × 16. Tuiles carrées
  rangées en ordre de Morton, des plus grandes aux plus petites : chacune est
  alignée sur sa taille, d'où des `Mipmaps()` (jusqu'à la tuile de 16 réduite à
  un pixel) qui ne mélangent jamais deux tuiles. `render3d::CreateTextureFromPixels`
  accepte ces niveaux. Voxelia libère ensuite les images sources
  (`ReleaseUnused`). Coût mesuré (release) : ~1 s de plus au chargement d'un
  monde avec les 25 images de 2048 px.
- Variantes de textures : `"variants": [ids]` d'une TextureDef ; les fichiers
  `nom.1.png`, `nom.2.png`… d'un mod (au moins deux) forment la texture `nom`.
  Le mailleur tire la variante d'après la position du bloc (`LookupVariants`,
  `BlockRender::variants`) ; ailleurs (icônes, particules), la première.
- Générateur "maze", niveaux : `lightOff` (néon éteint, sinon le plafond),
  `lightHalf` + `halfChance`, `lightHanging` (lampe dans la dernière cellule
  libre, sous un plafond intact). Sorties : `exit.signLeft` / `signRight`,
  `signRange` (cellules), `signHeight` — panneaux sur les murs des cellules
  voisines d'une sortie, flèche du côté de la sortie la plus proche (plaqués
  côté +z à l'orientation 0, comme un panneau posé face au mur).

Mod backrooms (ids existants conservés pour les sauvegardes) :
- Niveau 0 : papier peint jaune, moquette (texture procédurale générée,
  `backrooms_lobby_carpet.png`, l'ancienne ayant été supprimée), plafond crépi
  (variantes `backrooms_ceil.1/.2`), néons encastrés `fluorescent` /
  `fluorescent_off` / `fluorescent_half` (dalles recadrées sans leur marge
  sombre, `lamp_panel_*`), plus de plinthe.
- Hôtel : papier peint rouge, moquette en laine, même plafond, plus de plinthe.
- Hôpital : couloirs (vinyle, soubassement vert `hospital_wall_low` + murs
  blancs, deux variantes chacun) et nouveau niveau `hospital_ward` (faïence,
  dalles anciennes), même biome ; faux plafond à dalles ; plafonniers à
  grille pendus `hospital_light(_off)`.
- Parking : béton (murs, sol `parking_floor`, plafond), piliers en béton,
  réglettes pendues `parking_light(_off)`.
- Lampes : clic droit = interrupteur (bascule, `core:other/finger_click`).
- EXIT : `exit_sign` au-dessus de la porte d'ascenseur devient une plaque
  double face (`backrooms_lamp_exit.png`, tirée de la flèche droite, flèche
  ôtée) ; `exit_sign_left` / `exit_sign_right` muraux, lumineux.
- Textures restées sans usage par le générateur (non supprimées, blocs encore
  définis) : `backrooms_asphalt`, `backrooms_hotel_carpet`,
  `backrooms_hospital_floor`, `backrooms_hospital_wall`, `backrooms_pillar`.
  `baseboard` et `mahogany` (32 px) ne servent plus que de rares décorations
  au sol des biomes.

Tests : `Atlas_KeepsTextureResolutionsAndBuildsMipmaps`,
`Textures_VariantsArePickedPerBlock`, `WorldGen_MazeLampStatesAndExitSigns`.
