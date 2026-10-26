---
name: project-ui-refonte
description: "Refonte complète de libs/infrastructure/ui (2026-10-26) : builders déclaratifs + briques ECS, CSS (dataio/css), évènements façon JS (sdl3/input), systèmes du Schedule ; tous les exemples, l'émulateur, voxelia, sandbox et script_bindings réécrits dessus."
metadata:
  type: project
---

**2026-10-26.** `libs/infrastructure/ui` réécrit de zéro ; l'ancienne API
(`UiFactory`, `UiRect`/`UiItem`/`UiComputed`, `UiSelectable`, `UiInput`,
`OpenModal`/`CloseModal`, `SetTextEngine`, `Tick`…) n'existe plus.

**Principes (demande de l'utilisateur, à respecter) :**
- l'interface reste sur l'ECS (`ecs::ArchetypeRegistry`), ses systèmes
  tournent dans `ecs::Schedule` (`ui_input`, `ui_animation`, `ui_style`,
  `ui_layout`, `ui_render`) ; idiomes du dépôt obligatoires (`core::String`,
  `Option`/`Result`, `math::Corners`/`Sides`, aucune exception) ;
- builders fluents façon Flutter faits de briques POD atomiques
  (`components.hpp`) ; `Mount` matérialise, `ui::Widget` est une poignée
  (monde + entité) ;
- CSS analysé par `dataio/documents/css.hpp` : feuilles globales ou propres
  à un widget, `@import`, `@font-face`, images ; sélecteurs type / `#id` /
  `.classe` / `this`, héritage, spécificité ; **tout ce que le CSS règle se
  règle aussi au builder** (`style_setters.hpp`, partagé par builders et
  `Widget`) ;
- évènements façon JS (`OnBind`/`OffBind`/`HasBind`/`GetBind`/`GetBinds`)
  sur `sdl3/input.hpp`, tous périphériques ; bouillonnement et
  `StopPropagation()` ;
- rendu 2D par `sdl3::Renderer`, vues 3D et effets par render3d (Vulkan)
  pour sandbox et voxelia (`Viewport3D`, `ShaderEffect`).

**Ce qui a été réécrit dessus :** les 18 exemples (vérifiés à l'écran sous
Xvfb), l'émulateur (`WindowFrame` + feuille `ShellCss()`), voxelia (kit
devenu une feuille CSS générée `.vx-*`), sandbox (éditeur complet : arbre de
scène, ressources, inspecteur, documents, mode Jeu), `script_bindings`
(`UiBinding{registry, root}`). Tests : ui 104, sandbox 156, script_bindings
11, émulateur 22 ; scénarios `interface`, `smoke` et `themes` du sandbox
sans erreur.

**Pièges payés pendant le portage (lire avant de toucher) :**
- `Clip()` = `overflow: hidden` et **remplace** un `Scrollable()`
  précédent : `.Scrollable().Clip()` donnait une liste qui ne défile plus
  (une boîte qui défile rogne déjà) ;
- le menu contextuel **remonte** comme `contextmenu` en JS : une ligne qui
  a son menu dans une liste qui en a un appelle `StopPropagation()`
  (surcharge `OnContextMenu(EventCallback)`), sinon la liste rouvre le sien ;
- un clic d'entrée de menu ferme la chaîne de menus (`CloseMenuChain`),
  pas une boîte de dialogue que l'action vient d'ouvrir au-dessus ;
- `selectable`, `list-item`, `combo-item` sont des rangées (`flex-direction:
  row`) dans le thème ; l'état `:selected` vient du `Selection` du plus
  proche ancêtre (`SelectItem{index}`), pas d'un booléen par ligne ;
- le survol n'est recalculé qu'aux mouvements du pointeur : la bulle d'aide
  vérifie que le widget survolé est encore sous le pointeur ;
- longueur `fr` (`GrowWidth`/`GrowHeight`) : croît sur l'axe principal du
  parent, s'étire sur l'axe secondaire ; min-content CSS sur les deux axes
  et boucle de gel au rétrécissement ; un conteneur qui défile ne rétrécit
  jamais ses enfants.

**API ajoutée en cours de portage :** `ContentSlot` (`Append`/`Insert`/
`Clear` visent le corps d'un `Expander`, d'un `Menu`…), `Widget::MoveTo`
(`appendChild` d'un nœud existant), `Widget::Contains` (`Node.contains`),
`ActivePayloadDrag` (retour visuel d'un glisser-déposer propre à
l'application), `ResizeHandle::after`/`maxSize` (panneaux ancrés à droite ou
en bas), `Ui::RemoveStyleSheet`, `Builder::MaxLength`/`EditMode`,
`SceneManager`, `TitleBar`/`StatusBar`/`WindowChrome`/`WindowFrame`.
