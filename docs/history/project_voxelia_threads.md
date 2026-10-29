---
name: project-voxelia-threads
description: "Multithreading de voxelia (2026-10-28) : fil de simulation, fils de maillage sur instantanés du monde, vue 3D sans aller-retour GPU→CPU ; architectures mesurées sur le vrai GPU."
metadata:
  type: project
---

## Architecture retenue (défaut)

Le fil principal orchestre chaque image (`App::RunWindowed`) :

1. `Shell::EndSimulation` : attend le pas de simulation lancé à l'image précédente ;
2. événements, commandes, `Session::Update` (actions, caméra, scène), interface, enregistrement GPU ;
3. `Shell::BeginSimulation` : `Game::Tick` part sur le fil « voxel-update » ;
4. `Present` pendant que la simulation avance.

- `voxelia::Worker` (worker.hpp) : fil de travail sur `<sdl3/thread.hpp>` (Thread, Mutex, Condition), file de tâches, `Post` / `Wait` ; sans fil, exécution sur place.
- Simulation : l'interpréteur de scripts renvoie l'API de l'hôte vers son « fil principal » (`Interpreter::CallOnMainThread`) ; le fil de simulation s'en déclare propriétaire pendant le pas (`SetMainThread`), le fil principal le redevient à `EndSimulation` — sans cela, interblocage (le fil principal attend la simulation qui attend le fil principal).
- Maillage : `WorldRenderer` confie les chunks à 2 fils « voxel-render-N ». Il leur passe un instantané `World::Snapshot` : copies figées et partagées des chunks dans la portée de la lumière (±33 blocs), refaites seulement quand le chunk change. Le cache `LightCells` est rempli à la copie. Le générateur est partagé, car immuable après construction. Résultats rendus sous mutex, posés par le fil principal, et jetés si les réglages ou la dimension ont changé (`m_meshSerial`). `World::m_chunks` est passé en `shared_ptr`.
- Vue 3D (ui::Viewport3D, `ViewportTransfer`) : le goulot était là, pas dans la simulation. Le mode historique READBACK rendait hors écran, attendait le GPU, rapatriait les pixels, attendait encore, puis les renvoyait dans une texture du renderer, soit ~70 % du temps du fil principal bloqué dans `WaitForFences`. DIRECT (`sdl3::Texture::FromGpuTexture`, SDL ≥ 3.4) affiche la texture rendue sans copie ni attente. PIPELINED est un anneau de 3 cibles à téléchargement asynchrone. READBACK reste le défaut de la bibliothèque.

Options : `--threads=on|off`, `--mesh-threads=N` (défaut 2), `--viewport=direct|pipelined|readback`. Statistiques en fin de partie (images de jeu seulement : img/s, 99e centile, chunks en attente, ms par phase) ; panneau F3 : pas de simulation et maillage en ms.

## Mesures (Iris Xe, `SDL_VIDEO_DRIVER=offscreen`, release, médiane de 3)

| architecture | immobile | en déplacement | chunks en attente (dépl.) |
| --- | --- | --- | --- |
| séquentiel, readback (avant) | 77 img/s | 40 | 126 |
| séquentiel, direct | 189 | 69 | 124 |
| fils, readback | 69 | 69 | 128 |
| fils, pipelined | 156 | 125 | 194 |
| fils, direct, 1 fil de maillage | 204 | 169 | 354 |
| **fils, direct, 2 fils de maillage** | **227** | **164** | **91** |
| fils, direct, 3 fils de maillage | 174 | 149 | 88 |

Sous Xvfb tout est faussé : pas de DRI3, donc Vulkan passe par llvmpipe.

Écartés : lancer la simulation avant l'interface (le HUD et les écrans lisent l'ECS et le monde pendant `gui.Render` — une trentaine d'accès à faire passer par des instantanés). Prochaine piste : en déplacement, le fil principal attend ~3 ms la simulation, dont la génération des chunks (`UpdateLoadedArea` dans `Tick`) est le gros ; la paralléliser demande un générateur et des écritures inter-chunks sûres entre fils.

Vérifié : TSan sur une partie réelle avec déplacements (aucune course dans le code du dépôt ; une inversion d'ordre de verrous interne au backend Vulkan de SDL, mono-fil, supprimée), tests asan voxel / ui / render3d / sdl3.
