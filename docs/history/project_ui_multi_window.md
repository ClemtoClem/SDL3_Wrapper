---
name: project-ui-multi-window
description: "ui::WindowManager (2026-10-31) : plusieurs fenêtres SDL3, une interface (monde ECS, CSS, focus) par fenêtre, UN device GPU partagé (vues 3D DIRECT dans toutes les fenêtres), routage des évènements, raccourcis globaux, HiDPI, capture du bureau. Phase 0 de Voxelia Studio."
metadata:
  type: project
---

**2026-10-31.** `libs/infrastructure/ui` gère plusieurs fenêtres
(`ui/window_manager.hpp`). Rien de l'API existante n'a changé : une
application à une fenêtre continue d'écrire `ui::Ui gui(world, window, renderer)`.

## Modèle

- `ui::WindowContext` = une fenêtre SDL + son renderer + SON
  `ecs::ArchetypeRegistry` + SON `ui::Ui`. Un monde ECS par fenêtre, jamais
  partagé : les requêtes des systèmes ui ne traversent pas les fenêtres.
  Les feuilles CSS sont donc isolées naturellement (pas besoin de
  `@media (window-id …)`).
- `ui::WindowManager` : `Create(spec)`, `CreateOnDisplay(i, spec)`,
  `Close(id)` (appelle `onClose` AVANT destruction, fenêtre déjà retirée du
  registre), `Focus`, `Find`, `FindByRole`, `Windows()` (ordre de création),
  `ActiveWindow()`. Identifiants jamais réutilisés.
- `WindowSpec` : titre, taille, position ou écran cible, drapeaux SDL
  (RESIZABLE, BORDERLESS, FULLSCREEN, HIGH_PIXEL_DENSITY, ALWAYS_ON_TOP…),
  `renderDriver`, `vsync`, thème, feuilles, rôle, `primary`,
  `followDisplayScale`, `headless` (sans fenêtre SDL : tests).

## GPU : un seul device

Le gestionnaire POSSÈDE un `SDL_GPUDevice` et crée chaque renderer « gpu » sur
lui (`sdl3::Renderer::CreateOnGpuDevice` → `SDL_CreateGPURenderer`, SDL ≥ 3.4) :
une swapchain par fenêtre, un device. Un seul `render3d::Canvas`
(`Canvas::Create(renderer)` emprunte le device) sert toutes les fenêtres et
une `Viewport3D` en mode DIRECT s'affiche dans n'importe laquelle, sans copie.
Vérifié par test (même device pour deux renderers, et fermer la PREMIÈRE
fenêtre ne détruit pas le device des autres — c'est pour ça que le
gestionnaire, pas la première fenêtre, possède le device).

Ordre de destruction obligatoire : Canvas de l'application AVANT le
gestionnaire (il libère le device en dernier).

Pas de GPU (pilote absent) : repli sur le renderer par défaut,
`SharesGpuDevice()` faux ; la vue 3D repasse alors par READBACK (repli déjà
prévu par `Viewport3D`).

## Évènements

- `Dispatch(ev)` : tenue du registre (`Observe`), raccourcis globaux, puis
  l'interface de la fenêtre désignée par le `windowID` de l'évènement
  (`ui::EventWindowId` couvre souris, clavier, texte, doigts, stylet, dépôt,
  fenêtre). Sans fenêtre (manette) : la fenêtre active. Évènement de fenêtre
  inconnue : ignoré.
- `Observe(ev)` seul : pour une fenêtre dont l'application traite elle-même
  les évènements (le Shell de voxelia) — focus, `WINDOW_MOVED`, échelle
  d'écran, `CLOSE_REQUESTED`, `QUIT`.
- `Target(ev)`, `TriggerShortcut(ev, origin)` publics pour ce cas.
- Raccourcis globaux (`GlobalShortcut{key, mods, name, action(origin)}`) :
  essayés dans TOUTES les fenêtres ; modificateurs normalisés (gauche/droite
  indifférent, verrous ignorés, modificateur en trop = pas le raccourci) ;
  un champ de texte focalisé ne laisse passer que ce que
  `DefaultKeyPassThrough` accepte (Ctrl+Q oui, « W » non).
- Focus, capture du pointeur et du clavier : PAR fenêtre
  (`TextFieldHasFocus`, `PointerCapture`, `KeyboardCapture`).
- Fermeture : `HandleCloseRequests` ferme les secondaires ; la principale ou
  la dernière ⇒ `QuitRequested()`. Rien ne quitte de soi-même.

## HiDPI

`Ui::SetContentScale(s)` (nouveau) : le renderer dessine à l'échelle `s`
(`SDL_SetRenderScale`), la mise en page travaille en unités logiques
(`Context::WindowViewport()` = sortie du renderer / s), et `Dispatch`
convertit le pointeur (`SDL_ConvertEventToRenderCoordinates`). Tout est mis
à l'échelle, `px` compris (comme `devicePixelRatio`), pas seulement `rem`.
À l'échelle 1, comportement strictement identique à l'ancien (même calcul de
viewport). `WindowContext` suit `SDL_GetWindowDisplayScale` à la création et
sur `WINDOW_DISPLAY_SCALE_CHANGED` / `DISPLAY_CHANGED`.

## Image et captures

`Draw(clear, draw)` puis `Present()` (ou `Render` = les deux) : l'hôte glisse
entre les deux ce qui doit précéder l'attente du balayage (voxelia y lance le
pas de simulation). `RequestCapture(id, png)`, `RequestDesktopCapture(png)` :
lues avant Present ; le bureau est composé aux positions SUIVIES
(`DesktopPosition()` : demandée, puis `WINDOW_MOVED`) — le pilote offscreen ne
déplace pas ses fenêtres, la disposition voulue reste donc vérifiable.

## Ajouts et corrections dans les modules

- sdl3 : `Renderer::CreateOnGpuDevice`, `Renderer::CreateSharingGpu`,
  `Window::DisplayScale`, `Window::DisplayId`, `GetDisplays`,
  `GetDisplayBounds`, `GetDisplayContentScale`.
- **Bugs corrigés** : `sdl3::Window::IsVisible()` rendait vrai pour une
  fenêtre CACHÉE et `IsBordered()` vrai pour une fenêtre SANS bordure
  (drapeaux testés à l'envers ; aucun appelant dans le dépôt).
- ui : `Context::contentScale`, `Context::WindowViewport()`,
  `Ui::SetContentScale`/`ContentScale` ; le système d'entrée et `Ui::Layout`
  passent par `WindowViewport()`.

## Tests

`libs/infrastructure/ui/tests/test_multi_window.cpp` (9 tests, ui 113/113
sous ASan+UBSan, LSan et TSan) : création/fermeture de N contextes, ids
stables, un monde ECS par fenêtre, CSS isolées (même `#probe`, deux
largeurs), routage par windowID, focus et fenêtre active, raccourcis globaux
dans chaque fenêtre (et bloqués par un champ de texte), politique de
fermeture, échelle par fenêtre ; puis VRAIES fenêtres SDL sur le pilote
`offscreen` : capture du bureau vérifiée AU PIXEL (studio rouge à gauche, jeu
vert à droite, vide entre), et deux renderers sur le même device GPU.
