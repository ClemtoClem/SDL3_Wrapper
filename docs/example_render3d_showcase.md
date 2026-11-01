# render3d_showcase — vitrine render3d

`examples/render3d_showcase` · exécutable `render3d_showcase` · un seul fichier : [`src/main.cpp`](../examples/render3d_showcase/src/main.cpp) (152 lignes)

> Vitrine render3d : matériaux, lumières, animation, environnement

## Présentation

Ce qui s'ajoute au cube :

- une hiérarchie d'`Object3D` (parent et enfants) ;
- des géométries procédurales (boîte, sphère, tore, nœud torique) ;
- un matériau PBR métal/rugosité compilé à l'exécution par `ShaderBuilder`
  (`Material::Pbr`) ;
- deux lumières ponctuelles et un projecteur (`Canvas::SetLights`).

## Modules utilisés

[`render3d`](libs_infrastructure_render3d.md), [`sdl3`](libs_infrastructure_sdl3.md), [`ui`](libs_infrastructure_ui.md)

## Lancer

```sh
cpp-maker run -m render3d_showcase
```

Comme toutes les applications du dépôt, l'exemple se lance **depuis la
racine du dépôt** : polices, shaders et ressources sont lus dans `assets/`.

> Pas de capture d'écran : la scène 3D est présentée directement dans la
> swapchain GPU, et l'outil de capture utilisé pour cette documentation ne
> lit que le rendu 2D. Voir la capture de
> [render3d](libs_infrastructure_render3d.md) pour un rendu du module.
