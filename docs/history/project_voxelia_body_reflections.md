---
name: project-voxelia-body-reflections
description: "Voxelia (2026-10-31) : le joueur est une entité visible comme une autre (vue à la 3e personne et dans le studio), caméra réglable devant le visage du skin, objets tenus en main droite et gauche (inventaire, X), réflexions des textures par lancer de rayons dans une grille de voxels (« reflectivity », 1 à 16 rebonds)."
metadata:
  type: project
---

## Le corps du joueur

- `SpawnPlayer` ne cache plus le joueur : c'est une entité comme une autre,
  dessinée par `WorldRenderer::UpdateRig` à la 1re personne, à la 3e et dans
  la vue du studio (« Au joueur », `studio.camera x y z [vers x y z]`).
- Points d'attache des modèles articulés (`voxel::RigModel::eye`,
  `rightHand`, `leftHand`, type `RigAnchor` : partie + point en pixels,
  repère du modèle au repos). JSON facultatif :
  `"eye": {"part": "head", "at": [0, 28, 4.5]}`,
  `"hands": {"right": {"part": "right_arm", "at": [-6, 12, 0]}, "left": …}`.
  Par défaut : devant la face avant (+z, calques gonflés compris) de la
  première partie `look`, à mi-hauteur de sa première boîte ; mains au bas de
  la première boîte de `right_arm` / `left_arm`.
- `RigPointInModel(rig, part, point, input)` : position d'un point d'une
  partie une fois le modèle posé (chaîne des parents, rotations de
  `PoseRig`) — sert à la caméra.
- Gestes (`RigPoseInput::holdRight/holdLeft/swing`) : un bras qui tient un
  objet se lève de 18° vers l'avant ; le bras droit frappe (sinus, 70°)
  pendant qu'on casse un bloc.

## La caméra du joueur

- `Session::FaceAnchor` : le point d'yeux du rig + 1,5 px devant la face,
  tourné avec la tête par le regard (même pose que le rendu), à l'échelle et
  à l'orientation de l'entité. La caméra s'y place : la tête reste derrière
  le plan proche, le corps, les mains et les pieds sont visibles en regardant
  vers le bas. Sans modèle articulé (ou gravité inversée) : l'ancienne
  hauteur d'yeux.
- Réglages (Commandes > Caméra du joueur) : `cameraForward`, `cameraUp`,
  `cameraSide` (blocs, −1..1 : avant du regard, haut, droite) et
  `cameraTilt` (degrés, −30..30, ajoutés au regard) ; le champ de vision
  (`fov`) existait déjà. Tout à 0 par défaut : devant le visage du skin.
- `!view first|third` (scénarios) bascule la vue sans touche simulée.

## Objets tenus

- `Equipment::offhand` : la main gauche (la droite tient l'emplacement
  choisi). `Game::OffhandItem`, `Game::SwapHands`, enregistrée dans la
  sauvegarde du joueur (« offhand », un inventaire d'une case) ; scripts :
  `entity.offhand(e)`, `entity.set_offhand(e, objet [, nombre])`,
  `entity.swap_hands(e)`.
- Inventaire : un emplacement « Main gauche » dans la carte Équipement (clic :
  échange avec la pile tenue à la souris) et l'action « Tenir en main
  gauche » de l'objet sélectionné. Touche « Échanger les mains » : X.
- Rendu (`WorldRenderer::UpdateHeld`) : l'objet est un enfant de la partie
  du bras, à son point d'attache ; un bloc plein en miniature, le reste
  (outils, torches, fleurs…) en plaque mince de profil, la poignée dans la
  main. La lumière d'une torche tenue en main gauche éclaire comme en main
  droite (la plus forte des deux).

## Réflexions

Algorithme (inversé, de la caméra vers la scène) :

1. rayon primaire : la rastérisation elle-même (la surface la plus proche du
   pixel) ;
2. couleur directe : l'éclairage BAKED habituel (sommets, ciel, soleil,
   lumières ponctuelles) ;
3. si la texture réfléchit, rayon secondaire dans la direction
   R = I − 2 (I·N) N, parcouru cellule par cellule (DDA d'Amanatides et Woo)
   dans une grille de voxels autour de la caméra, jusqu'au premier bloc dont
   la texture est opaque à cet endroit ;
4. le point touché est éclairé comme une face du monde (lumière des sources
   et part de ciel de la cellule devant la face, ambiante, soleil, lumières
   ponctuelles) ; s'il réfléchit lui aussi, le rayon repart — au plus N
   réflexions, tant qu'il n'est pas éteint (la part transmise, produit des
   réflectivités, sous 1 %). Couleur finale : mélange direct / reflet selon
   la réflectivité (1 : miroir, seul le reflet).

- Données : `TextureDef::reflectivity` (« reflectivity » 0..1 dans la
  définition d'une texture ; ses variantes en héritent ; la « lueur » prend
  celle de l'originale). `TextureAtlas::Reflectivity()` : carte R8 de même
  disposition que l'atlas (vide si rien ne réfléchit : aucune grille).
- `voxel/raygrid.hpp` : `StopsRays` (cube plein, visible, ni liquide ni
  translucide — le feuillage découpé aussi, le shader teste l'alpha) et
  `FillChunk` (texels RGBA16_UINT : bloc, part de l'ambiante, lumière des
  sources r|g<<8, b sur 0..4). `ChunkMesher::RayBlockTable` : par bloc 8
  lignes (rectangles des 6 faces, teinte + masque des faces teintées par le
  biome, teinte du biome + lumineux).
- render3d : `Material::voxelReflections`, `Canvas::SetVoxelReflections`
  (`VoxelReflections` : grille 3D torique, table, réflectivité, origine,
  taille, rebonds, distance, ciel), `ShaderBuilder::VoxelReflections`,
  chunks `SAMPLER_VOXEL_REFLECTIONS` / `VOXEL_REFLECTION_FUNC` ; les champs
  vont en fin de LightUBO (les 4 UBO fragment de SDL_GPU sont pris), les
  échantillonneurs après albedo / ombres / IBL, dans `End()` comme dans
  `RenderObjectOffscreen`. `CreateDataTexture` / `UploadTextureRegions`
  (texture_loader) pour les grilles et tables.
- voxelia : `ReflectionVolume` — 8 × 4 × 8 chunks (128 × 64 × 128), torique ;
  les chunks entrés, modifiés ou chargés depuis partent quelques-uns par
  image, les plus proches d'abord, en une passe de copie ; table refaite au
  changement de biome de la caméra et à chaque rechargement du contenu.
  Matériaux des chunks (opaque, translucide) seulement.
- Réglages (Graphismes > Qualité) : « Réflexions » (activées) et
  « Réflexions successives » `reflectionBounces` = 4 par défaut, bornées à
  1..16 à la lecture.
- Mod core : réflectivité de l'eau (0,3), du verre (0,15), des blocs de fer
  (0,45), d'or (0,55), de diamant (0,35), des glaces (0,25–0,3) ; nouveau
  bloc `core:mirror` (texture `core:mirror`, réflectivité 1).

## Vérifications

- voxel : `Rig_EyeAndHandAnchors`, `Rig_PointInModelFollowsThePose`,
  `Rig_HeldItemsAndSwingRaiseTheArms`, `Game_OffhandIsSwappedAndSaved`,
  `RayGrid_*` (réflectivité et carte, table des blocs, cellules : blocs qui
  arrêtent les rayons, ciel, lumière d'une lampe et sa portée).
- render3d (GPU réel) : `OffscreenSmokeTest.VoxelReflectionsTraceTheGrid` —
  un sol miroir montre un mur rouge qui n'existe que dans la grille ; sans
  grille, noir ; grille vide, le ciel ; mur miroir lui aussi : 1 rebond →
  rouge, 2 → ciel.
- voxelia : `Settings_*` (valeurs par défaut, bornes, aller-retour).
- Scénarios hors écran : joueur vu de dos (3e personne) et de face (studio)
  avec pioche et torche en main ; première personne vers le bas (bras,
  corps) ; murs et sol de miroirs (4 rebonds : reflets successifs ; 1 : le
  miroir d'en face seulement éclairé ; désactivées : surfaces grises).

## Limites

- Seuls les blocs sont dans la grille : les entités (le joueur compris) ne
  se reflètent pas ; formes (escaliers, dalles, plantes, torches), liquides
  et verre sont traversés par les rayons réfléchis.
- La lumière des sources dans la grille n'est pas occultée (portée seule) ;
  la part de ciel est celle de la colonne ; pas de Fresnel (réflectivité
  constante quel que soit l'angle) ; au-delà de 96 cellules ou de la grille,
  le rayon voit la couleur du ciel (pas le cubemap).
- Une révision de chunk ne renvoie que ce chunk : une lampe posée juste à
  côté ne rallume pas les cellules du chunk voisin avant qu'il change.
