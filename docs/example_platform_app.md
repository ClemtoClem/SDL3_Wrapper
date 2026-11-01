# platform_app — application minimale sur platform::Application

`examples/platform_app` · exécutable `platform_app` · un seul fichier : [`src/main.cpp`](../examples/platform_app/src/main.cpp) (77 lignes)

> Application minimale sur platform::Application : boucle à pas fixe, bus d'événements, diagnostics

## Présentation

Une application complète en environ 80 lignes sur
[`platform`](libs_infrastructure_platform.md) :

- un carré rebondit dans la fenêtre, avec une simulation à 120 Hz et un rendu
  interpolé ;
- les touches arrivent par le bus d'évènements (`platform::KeyChanged`) ;
- les mesures passent par [`diagnostics`](libs_foundation_diagnostics.md).

À la fermeture, le programme affiche le nombre d'images et de pas
simulés.

![Capture de platform_app](../captures/example_platform_app.png)

## Modules utilisés

[`platform`](libs_infrastructure_platform.md), [`events`](libs_foundation_events.md), [`diagnostics`](libs_foundation_diagnostics.md), [`sdl3`](libs_infrastructure_sdl3.md)

## Lancer

```sh
cpp-maker run -m platform_app
cpp-maker run -m platform_app -- --frames=300     # s'arrête seul
```

Espace change la couleur du carré ; Échap quitte.

Comme toutes les applications du dépôt, l'exemple se lance **depuis la
racine du dépôt** : polices, shaders et ressources sont lus dans `assets/`.
