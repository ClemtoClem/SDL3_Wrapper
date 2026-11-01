# render3d_shadows_ibl_showcase — ombres et éclairage par image

`examples/render3d_shadows_ibl_showcase` · exécutable `render3d_shadows_ibl_showcase` · un seul fichier : [`src/main.cpp`](../examples/render3d_shadows_ibl_showcase/src/main.cpp) (241 lignes)

> Ombres et éclairage par image (IBL) de render3d

## Présentation

La démonstration la plus complète de [render3d](libs_infrastructure_render3d.md) :

- ombres directionnelle, projetée et ponctuelle ;
- IBL complet (irradiance, spéculaire préfiltré, BRDF LUT), chargé depuis
  une image `.hdr` équirectangulaire ;
- les autres nœuds de scène : `Points`, `LineSegments`, `Sprite`,
  `InstancedMesh`, `SkinnedMesh` et `Bone`, `LOD`.

## Modules utilisés

[`render3d`](libs_infrastructure_render3d.md), [`sdl3`](libs_infrastructure_sdl3.md), [`ui`](libs_infrastructure_ui.md)

## Lancer

```sh
cpp-maker run -m render3d_shadows_ibl_showcase
```

> **Fichier manquant** : l'exemple charge
> `assets/textures/equirectangularmaps/procedural_sky.hdr`, qui **n'est pas
> dans le dépôt**. Sans lui, il s'arrête avec
> `LoadEnvironmentFromHdr: Couldn't open …` et le code 1. Placez une image
> Radiance `.hdr` équirectangulaire à cet emplacement pour l'exécuter.

Comme toutes les applications du dépôt, l'exemple se lance **depuis la
racine du dépôt** : polices, shaders et ressources sont lus dans `assets/`.

> Pas de capture d'écran : la scène 3D est présentée directement dans la
> swapchain GPU, et l'outil de capture utilisé pour cette documentation ne
> lit que le rendu 2D. Voir la capture de
> [render3d](libs_infrastructure_render3d.md) pour un rendu du module.
