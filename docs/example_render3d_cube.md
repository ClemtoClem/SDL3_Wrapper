# render3d_cube — premier rendu render3d

`examples/render3d_cube` · exécutable `render3d_cube` · un seul fichier : [`src/main.cpp`](../examples/render3d_cube/src/main.cpp) (103 lignes)

> Premier rendu render3d bout en bout : Canvas + cube texturé

## Présentation

Le plus petit programme [render3d](libs_infrastructure_render3d.md) de bout
en bout :

- un `Canvas` ;
- un `Mesh::Cube` avec un matériau Phong (`Material::Plastic`) ;
- une `Camera` ;
- une `DirectionalLight` et une `AmbientLight` ;
- le cube tourne.

L'encadrement de fenêtre (`ui::CanvasWindowFrame`) est rendu en logiciel
puis copié par-dessus la scène 3D (`Canvas::SetOverlay`).

## Modules utilisés

[`render3d`](libs_infrastructure_render3d.md), [`sdl3`](libs_infrastructure_sdl3.md), [`ui`](libs_infrastructure_ui.md)

## Lancer

```sh
cpp-maker run -m render3d_cube
```

Échap ou × pour quitter.

Comme toutes les applications du dépôt, l'exemple se lance **depuis la
racine du dépôt** : polices, shaders et ressources sont lus dans `assets/`.

> Pas de capture d'écran : la scène 3D est présentée directement dans la
> swapchain GPU, et l'outil de capture utilisé pour cette documentation ne
> lit que le rendu 2D. Voir la capture de
> [render3d](libs_infrastructure_render3d.md) pour un rendu du module.
