---
name: project-voxelia-lighting
description: "Refonte de l'éclairage voxel (2026-10-27) : sources JSON (« light »), transport direct en linéaire (inverse du carré, cône, Lambert), occlusion DDA par la géométrie (« optics »), part de ciel par rayons, cumul additif, invalidations localisées, matériau BAKED de render3d (ACES + sRGB)."
metadata:
  type: project
---

## Diagnostic (avant)

- Les lampes étaient des lumières ponctuelles du SHADER : les 16 plus proches
  de la caméra seulement, toutes de la même couleur orange (255, 196, 130),
  intensité/portée tirées du niveau 0..15, sans aucune occlusion → la
  lumière traversait les murs, les autres lampes étaient ignorées et la
  sélection changeait en bougeant.
- Ambiante appliquée partout (0,25 à 0,6 × luminosité × `light.ambient`), un
  faux « soleil diffus » dans les mondes sans soleil, ombre du ciel par
  COMPTE de blocs au-dessus de la colonne avec un plancher à 20 % (grottes et
  pièces closes jamais noires), le réglage « luminosité » augmentait
  l'ambiante.
- Tout en espace gamma, sans tone mapping.

## Modèle (après) — voxel/lighting.hpp

- Conventions uniques : couleurs JSON sRGB 0..255 (ou "#rrggbb") → linéaire ;
  intensité = éclairement à 1 bloc face à la source ; distances en blocs,
  angles en degrés (demi-angles du cône) ; directions dans le repère du
  bloc, tournées avec lui (orientation du param).
- « light » d'un bloc ou d'une entité : enabled, type (point | spot |
  directional), color, intensity, range (≤ 32, calculée sinon), attenuation
  (inverse_square | linear | none), direction, innerAngle/outerAngle,
  radius (faisceau directionnel), offset (position dans la cellule).
  Valeurs invalides corrigées et signalées par `ContentRegistry::Validate`
  (types, négatifs, couleurs 0..1, angles inversés, clés inconnues…).
- PRIORITÉS : objet « light » > « light »: N / « graphics.light »: N
  (niveau historique : point blanc chaud, I = 0,4 N, portée N + 1) ;
  « light »: false ou enabled false : aucune émission. `lightEmission`
  (aspect lumineux des textures) = « graphics.light » s'il est donné, sinon
  déduit de l'intensité, toujours 0 pour une lumière inactive. Les états
  (« states ») surchargent « light » comme le reste.
- Contribution : couleur × I × atténuation × distribution × max(0, n·L) × T.
  Inverse du carré régularisé (d² ≥ 0,25), fenêtre (1 − (d/r)⁴)² pour finir
  sans marche ; spot : smoothstep entre les cos des demi-angles ; faisceau
  directionnel : cylindre parallèle, sans inverse du carré. Les sources
  s'ADDITIONNENT par canal (deux lampes = double, rouge + bleu = magenta).
- Occlusion : `Transmittance` parcourt la grille en DDA (Amanatides & Woo) ;
  « optics » de chaque bloc (déduite du matériau : opaque 0, verre/eau
  0,85 × teinte, feuillage 0,45, plantes 0,85 ; « optics »: {opaque,
  transmittance, fullCell} pour la changer) ; formes partielles (portes,
  dalles, échelles) : seuls les rayons qui touchent leurs boîtes sont
  filtrés. La collision n'intervient pas.
- Ciel : `SkyAccess`, cinq rayons (zénith + quatre à 45°) jusqu'à 32 blocs,
  estimation du générateur au-delà des chunks chargés. Grottes et pièces
  closes : 0.
- Dimension : « light »: {sky, ambient (× ambiante du ciel), base (lumière
  uniforme, 0 possible), exposure}. Sans ciel et sans « base » :
  `ambient` en tient lieu (historique, mondes « inverted », « core »,
  « void »). Backrooms : base 0,06.

## Où c'est calculé

- Mailleur : chaque sommet reçoit `MeshVertex::light` (rgb = direct, a = part
  de ciel × occlusion ambiante), moyenne des cellules libres autour du sommet
  du côté éclairé (lissé, sans raccord entre chunks). La teinte (couleur du
  sommet) ne porte plus d'éclairage.
- `World` : cellules lumineuses par chunk (`Chunk::LightCells`, refaites à
  chaque modification), sources des entités (`SetDynamicLights`, par le
  système « lights » du jeu, 5 fois par seconde), invalidations : un bloc
  qui change de lumière ou d'optique fait refaire les chunks des sphères de
  portée concernées (`TouchSphere`) ; un chunk qui arrive, celles de ses
  sources.
- render3d : `Vertex3D::light` (location 8), `Material::bakedLighting` →
  `LightingModel::BAKED` (linéaire, + ambiante et soleil × part de ciel,
  + `Canvas::SetBaseLight`, exposition `Canvas::SetExposure`, ACES puis
  sRGB), `Material::objectLight` pour les objets mobiles (éclairés là où ils
  sont). La seule lumière de shader restante : l'objet tenu en main, à la
  place de l'œil (pas d'occlusion à calculer : elle n'atteint que ce que la
  caméra voit).

## Limites assumées

- Lumière DIRECTE seulement : pas de rebond (les plafonds au-dessus de
  lampes dirigées vers le bas restent sombres, d'où la « base » des
  backrooms), pas de caustiques ni de diffusion volumétrique.
- Pas de variation temporelle (scintillement) : la lumière est figée dans
  les sommets, l'animer imposerait de refaire les chunks en continu.
- Les entités lumineuses refont les chunks de leur portée quand elles
  changent de cellule (créatures lumineuses rares) ; un objet mobile reçoit
  un éclairement sans direction (la moitié de l'éclairement scalaire).
- Coût mesuré (release, chunk de salles à lampes tous les 4 blocs) : ~3 ms
  par chunk éclairé contre 0,4 ms sans (`Lighting_MeshingCostIsMeasured`).

Tests : `test_lighting.cpp` (A à L du cahier des charges + coût),
`OffscreenSmokeTest.BakedLightingUsesVertexLightOnly` (render3d).
