# Encadrement de fenêtre personnalisé — `ui::WindowFrame`

Depuis le 2026-10-06, tous les exemples s'ouvrent dans une fenêtre **sans
décoration système** encadrée par le module `ui` : barre de titre (icône
d'application, titre, poignée de déplacement, réduire / agrandir-restaurer /
fermer en icônes MaterialIcons), barre d'état avec poignée de
redimensionnement ↘, déplacement et redimensionnement par le hit-test du
système (`SDL_SetWindowHitTest`).

## Trois niveaux d'API

| Niveau | Fichier | Pour qui |
|---|---|---|
| `ui::WindowFrame` | `ui/window_frame.hpp` | Toute application `ui::` : un appel construit la racine (barre de titre, zone de contenu, barre d'état), charge la police d'icônes et branche le hit-test. `Content()` reçoit le contenu, `Update()` à chaque image, `CloseRequested()`. Construction depuis `ui::Ui` ou depuis UiFactory + LayoutSystem + RenderSystem (pipelines manuels). |
| `ui::CanvasWindowFrame` | `ui/canvas_window_frame.hpp` | Applications qui dessinent en 3D directement dans la swapchain (`render3d::Canvas`). L'interface est rendue en logiciel sur une surface ; seules les deux barres (opaques) sont copiées par-dessus la scène via `Canvas::SetOverlay`. Le rendu 3D reste complet (ombres, IBL, instanciation) — ce qu'un `Viewport3D` (rendu hors écran) ne permet pas. |
| `ui::TitleBar` / `ui::StatusBar` / `ui::WindowChrome` | `ui/chrome.hpp` | Applications à coquille propre (game_editor_demo, emulator_demo) : on place les barres où l'on veut, puis `WindowChrome::Attach(window, …, titleBar, &statusBar)`. `TitleBarWidgets::Update` fait suivre l'icône agrandir/restaurer à l'état réel de la fenêtre. |

```cpp
auto window = sdl3::Window::Create("Titre", 1280, 720, ui::WindowFrame::WINDOW_FLAGS);
...
ui::WindowFrame frame;
frame.Build(gui, window, {.title = "Titre", .appIcon = Some(ui::MaterialIcons::DASHBOARD), .status = "Prêt."});
f.Panel().GrowW().GrowH().Parent(frame.Content()).Spawn();
while (running) {
    ...
    gui.Tick(dt);
    frame.Update();
    if (frame.CloseRequested()) running = false;
}
```

## Exemples convertis

| Exemple | Méthode |
|---|---|
| ui_minimal, ui_plot_demo, ui_shader_effects_demo, ui_viewport3d_demo, audio_signal_generator, audio_spectrum_analyzer | `WindowFrame` sur `ui::Ui` ; contenu dans `Content()` (positions `Rpct` relatives à la fenêtre remplacées par `GrowW/GrowH` ou `Absolute + Anchor`). Hauteur de fenêtre +60 px quand le contenu était de taille fixe. |
| ui_aero_basics, ui_aero_lists, ui_aero_windows | `WindowFrame` avec `fillTitleBar = false` : le verre et le dégradé du bureau restent visibles. L'ancienne `TitleBar` texte et le `std::exit(0)` de fermeture disparaissent. |
| ui_node_graph_demo, audio_patchbay, ui_showcase | `WindowFrame` construit depuis les systèmes séparés. Racine sans fond : la grille du graphe est dessinée avant `render.Run()`. Aide clavier/souris dans la barre d'état. |
| renderer | `ui::Ui` ajouté au-dessus du rendu 2D : la scène est dessinée dans un viewport limité à la zone de contenu, les clics convertis dans ce repère. |
| render3d_cube, render3d_showcase, render3d_shadows_ibl_showcase | `CanvasWindowFrame` + `Canvas::SetOverlay`. |
| adhoc_chat_demo | `WindowFrame` (remplace l'assemblage manuel). |
| emulator_demo | `TitleBar` + `StatusBar` + `WindowChrome` dans sa coquille (remplace une barre texte et un hit-test maison sans redimensionnement). |
| game_editor_demo | `EditorUi::SetWindow` : barre de titre en tête de l'interface (page d'accueil comprise), poignée dans la barre d'état existante, hit-test rebranché à chaque reconstruction (changement de projet ou de thème). Sans fenêtre (tests), rien n'est ajouté. |

## Ajouts et corrections au passage

- `Canvas::SetOverlay` (render3d) : régions RGBA envoyées dans une texture GPU
  à la taille de la swapchain puis copiées par `SDL_BlitGPUTexture` après le
  rendu de la scène, avant la soumission.
- `WindowChrome` : non copiable, se détache en destruction (son rappel de
  hit-test pointait sur lui) ; `AddResizeGrip`.
- `TitleBarOptions::boldTitle` vaut `false` par défaut : le layout mesure avec
  la police normale, une vraie police grasse enregistrée rognait le titre
  (vu dans ui_showcase).
- `WindowFrame::Build` reprend le titre comme titre système (barre des
  tâches).
- **Couleurs des exemples** : `TextColor({150, 156, 178})` et
  `Bg({22, 24, 36, 255})` passent par `FColor(float…)` — composantes bornées
  à 1 et alpha 0 par défaut : texte transparent, ou fond blanc opaque pour la
  forme à 4 composantes. 23 occurrences corrigées en `sdl3::Color{…}`
  (octets, alpha 255).

## Vérifications

- `tests/ui_title_bar_smoke_test.cpp` : 6 tests (barre de titre, barre
  d'état, `WindowFrame` — contenu exactement entre les deux barres, état,
  titre système, fermeture — et `CanvasWindowFrame` — rendu logiciel, zones
  des barres).
- Sous Xvfb + xfwm4, capture de chaque exemple ; sur render3d_cube, fermeture
  par ×, et sur adhoc_chat_demo, déplacement, poignée, agrandir/restaurer et
  réduire.
- Non vérifié visuellement : render3d_shadows_ibl_showcase, qui exige
  `assets/textures/equirectangularmaps/procedural_sky.hdr`, absent du dépôt
  (même cause que l'échec de `hdr_loader_smoke_test`).
- Hypothèse : densité de pixels 1 pour `CanvasWindowFrame` (fenêtre en points
  = swapchain en pixels). Sur un écran HiDPI, les barres seraient copiées au
  mauvais endroit.
