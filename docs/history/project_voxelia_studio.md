---
name: project-voxelia-studio
description: "Voxelia Studio, socle (2026-10-31) : éditeur dans sa propre fenêtre SDL, sur la MÊME partie que la fenêtre de jeu (aucune copie) — World Outliner et inspecteur sur le registre ECS vivant, vue 3D DIRECT partagée, console à code d'état, pause/pas à pas, rapport, captures. Ce qui reste à faire."
metadata:
  type: project
---

**2026-10-31.** Socle de Voxelia Studio (étapes 0 et 1 de
`demos/voxelia/IDE_voxelia_prompt.md`), branché sur le VRAI moteur.

> Une version précédente (≈15 000 lignes, studio « simulé » sur un
> `VoxelWorld` maison sans aucun module du moteur, `WindowManager` factice,
> `main.cpp` du jeu remplacé, build cassé) a été RETIRÉE de l'index à la
> demande de l'utilisateur et sauvegardée hors dépôt. Ne pas la réintroduire :
> tout ce qui suit lit et modifie la partie réelle.

## Architecture

```
App::RunWindowed (demos/voxelia/src/app.cpp)
 ├─ ui::WindowManager   (libs/ui, cf. project_ui_multi_window.md)
 │   ├─ fenêtre « game »   (principale)  → Shell → Session → voxel::Game + WorldRenderer
 │   └─ fenêtre « studio » (F10/--studio) → studio::Studio
 ├─ render3d::Canvas    UN SEUL, sur le device GPU du gestionnaire
 └─ studio::StudioReport (--report)
```

- Même processus, même `voxel::Game`, même scène `WorldRenderer::Root()` :
  la vue 3D du studio est une seconde `ui::Viewport3D` (DIRECT, aucune copie,
  device partagé) sur la scène de la partie, avec SA caméra.
- Le studio a son monde ECS d'interface et sa feuille CSS (`STUDIO_CSS`) ;
  celle du jeu n'est pas touchée.

## Fils (cf. project_voxelia_threads.md) — préservés

Boucle par image : `EndSimulation` → évènements → commandes → `Shell::Update`
→ **`Studio::Update` (le studio lit/modifie le jeu ICI)** →
`windows.Update` → `windows.Draw` (les deux fenêtres) → captures →
`BeginSimulation` → `windows.Present`. Le studio ne touche jamais au jeu
pendant un pas de « voxel-update » ; les fils « voxel-render-N » maillent
toujours des instantanés. La fenêtre du studio n'attend pas le balayage
(`vsync=false`) : seule la fenêtre de jeu le fait.

## Modules (demos/voxelia/{include/voxelia,src}/studio/)

| Fichier | Rôle |
|---|---|
| `console` | `CommandResult{status, message, confirmationPrompt, onConfirm}`, `ConsoleModel` : Ok ferme, Error garde ouverte, Warning ferme après 5 s, ConfirmRequired pose « [y/N] » (y/o/yes/oui), Pending jusqu'à `Resolve` ; historique, complétion, `help` ; repli : commandes du jeu (`Session::Command`). Modèle sans fenêtre. |
| `outliner` | `CollectEntities(game, filtre, caméra)` : entités du registre vivant, catégorie (joueur, créature, objet, bloc qui tombe, plateforme mobile), chunk, distance, filtres texte/catégorie/distance/vivants ; `InspectEntity` : sections par composant présent (Identité, Transform, Physique, Santé, Survie, Effets, IA, Plateforme mobile, Visuel). |
| `report` | fonctionnalités visitées (pire statut, visites, détail), cadence min/moy/p50/p95/p99/max, fils max, captures, compteurs, avertissements, erreurs ; texte + JSON. |
| `studio` | la fenêtre : barre d'outils (Pause/Reprendre, Pas, Suivre, Au joueur, état), outliner 5 Hz (reconstruit seulement si l'ensemble change), vue 3D, inspecteur (reconstruit si la structure change, valeurs mises à jour à chaque image), console. |

Ajouts au jeu : `Shell::HoldSimulation`/`SimulationHeld`/`StepSimulation`
(passent par le `paused` existant de `Session::Update`).

## Commandes (console, et `--exec=N:!studio LIGNE`)

`studio.select player|<entité>|<nom>`, `studio.deselect`, `studio.focus`,
`studio.follow`, `studio.camera player|x y z`, `studio.pause`, `studio.play`,
`studio.step [n]`, `studio.heal`, `studio.kill` (confirmation),
`studio.stats` (F3), `reload` (Warning : pas encore branché), `help` ; toute
autre ligne → commande du jeu. `!studio open|close`.

Vue 3D : clic droit maintenu + WASD/ZQSD (scancodes), E/Q monter/descendre,
Maj ×3, molette ; double-clic d'une ligne : caméra dessus ; F : idem pour la
sélection. Tangage borné ±1,5 rad. Échap : quitte la poursuite, puis
désélectionne — ne ferme rien. Ctrl+Q (toutes les fenêtres) quitte, F10
ouvre/ferme le studio, fermer la fenêtre de jeu quitte, fermer le studio le
détache seulement.

## Pièges payés

- **Caméra poursuite dans le terrain** : 6 blocs derrière la cible tombe dans
  la colline → vue entièrement noire (pris d'abord pour un bug du mode
  DIRECT : en READBACK sans poursuite tout s'affichait — deux variables
  mêlées). La poursuite lance un rayon cible → caméra et s'arrête avant le
  premier bloc `solid`. Cap pris sur la VITESSE, gardé à l'arrêt.
- Fermer le studio pendant `Dispatch` de SA fenêtre (F10 dans le studio)
  détruirait le contexte en cours d'usage : les raccourcis posent des
  drapeaux, traités après la boucle d'évènements.
- `onClose` du studio capture des références locales de `RunWindowed` : le
  studio est fermé explicitement avant la sortie de la fonction (sinon le
  gestionnaire, détruit en dernier, rappellerait `onClose` sur des objets
  morts).
- En debug, une partie neuve met ~1000 images à charger : un scénario qui
  agit avant obtient « aucune partie en cours » (Error rapportée en
  avertissement, comportement voulu).

## Vérification

- Tests sans fenêtre `demos/voxelia/tests/test_studio.cpp` (8) : console
  (5 codes et leurs effets, historique, complétion, découpage), outliner et
  inspecteur sur une VRAIE partie (joueur, cochon, objet au sol, filtres,
  mort, dégâts et poison vus en direct, entité disparue), rapport (pire
  statut, centiles, JSON relu). voxelia 29/29 sous ASan+UBSan, LSan, TSan.
- Exécution réelle (pilote SDL `offscreen`, vrai GPU, pas d'Xvfb sur la
  machine) :
  `SDL_VIDEO_DRIVER=offscreen voxelia --world=studio_demo --seed=4242 --studio --frames=1500 --exec=1110:'!studio studio.select player' … --screenshot-desktop=1400:desktop.png --studio-screenshot=1400:studio.png --report=rapport.txt`
  → rc=0, stderr 0 octet, rapport « succès », 17 fonctionnalités visitées,
  device GPU partagé, 2 fenêtres sur la capture du bureau.

## Reste à faire (étapes 2+ du prompt)

Sélection au clic dans la vue (`PickMeshFace`), gizmo, éditeur de blocs et
hot-reload (`reload` répond Warning), atelier de textures, formes/animation,
structures (`Schematic`/`Formula`), biomes, dimensions, navigateur de
ressources, Mod Manager, undo par instantanés, édition des valeurs dans
l'inspecteur (seuls Soigner/Tuer existent), vérification `--seed`,
scénarios nommés `--scenario=`.

`script_bindings` (et donc `sandbox`), cassé à HEAD et dans l'index, a été
réparé le 2026-10-31 :
include `render3d/shape.hpp` manquant, `Option` dans un ternaire, étoile en
trop sur une `sql::Row`, bindings `dataio` rétablis (`modeling`/`net`
référencés mais jamais implémentés : retirés), dépendances déclarées ;
**UB** dans `dataio.mesh.save` avec `colors` (`delete` d'un objet de la pile
+ fuite) ; **régression** de `ColorOf` : l'alpha par défaut était divisé
par 255 avec des composantes 0-255 (`[0, 255, 0]` presque transparent) ;
message `[x, y, z]` des options de position rétabli ; `script_color.hpp`
rendu autonome ; `test_dataio_script.cpp` (exemple non compilable) changé en
vrai test.
