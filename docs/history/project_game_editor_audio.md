---
name: game-editor-audio
description: "game_editor_demo — ressources partagées du ResourceCache (primitives et modèles glTF chargés en arrière-plan) et son sur SDL3_mixer : AudioEngine, composant AudioSource, écoute dans l'explorateur, API de script audio.*."
metadata:
  type: project
---

# Ressources partagées et son dans game_editor_demo

Depuis le 2026-10-12, l'éditeur utilise l'étape 1 de la
[refonte du moteur](architecture_moteur.md) : ses maillages et ses sons sont
des `resources::Resource` d'un `ResourceCache` du `Runtime`
(`Runtime::Resources()`, aussi rangé dans le monde ECS sous
`resources::CacheRef`).

## Maillages

- **Primitives** : `Runtime::PrimitiveOf(visual)` traduit la forme d'un nœud
  en `render3d::PrimitiveSpec` ; `render3d::Primitive(cache, spec)` rend la
  ressource (même spec → même ressource). Deux boîtes de mêmes dimensions
  montrent donc la **même** géométrie (`Shape::SharedGeometry()`).
- **Jamais modifier une géométrie partagée** : changer la forme d'un nœud
  (`SetVisual`) lui pose une AUTRE géométrie (`Shape::SetGeometry`), il ne
  réécrit pas celle qu'il partage.
- **Modèles glTF** : `render3d::ImportMesh(cache, chemin, ASYNC)`. Le nœud
  montre un cube d'attente tant que la ressource se charge ;
  `ResolvePendingMeshes()` (à chaque `Update`) lui pose la vraie géométrie
  dès qu'elle est prête. Un échec écrit l'erreur dans la console et garde le
  cube. Toutes les instances du modèle partagent la géométrie. Les vignettes
  de l'explorateur passent par le même cache.
- `ImportGltf` charge le modèle (SYNC) dans le cache pour ses dimensions :
  le nœud créé ensuite ne relit pas le fichier.
- Les références d'assets introuvables (`ResourceCache::Missing()`) sont
  écrites une fois chacune dans la console (`ReportMissingAssets`).
- Limite : un modèle modifié sur le disque n'est pas relu tant qu'il est en
  mémoire.

## Son

### Moteur (`engine/audio.hpp`)

`AudioEngine` repose uniquement sur les enveloppes C++ de
`lib/sdl3/mixer.hpp` : `MixerContext`, `Mixer`, `MixTrack`,
`MixAudio::FromPcm`, `AudioDecoder::DecodeFile`.

- **Démarrage paresseux** : `Runtime::Audio()` ouvre le périphérique par
  défaut au premier son. Faute de périphérique, l'éditeur prend un mixeur
  **hors ligne** : tout fonctionne, en silence.
- **Sons** : un son est une `audio::AudioClipResource`, décodée en
  arrière-plan. Les WAV passent par `sdl3::LoadWav`, les autres formats
  (OGG, MP3, FLAC…) par `AudioDecoder::DecodeFile`.
- **Voix** : une voix est une lecture en cours, désignée par un numéro
  (`VoiceId`). On peut la demander avant que son son soit prêt : elle
  démarre dès qu'il l'est.
- **Spatialisation** : la position monde d'une voix est convertie dans le
  repère de la caméra active (`Camera::ViewMatrix`). C'est le repère
  qu'attend SDL3_mixer : x à droite, y en haut, −z devant.

### Composant AudioSource

Le composant porte les champs suivants :

| Champ | Rôle |
|---|---|
| `clip` | le son à jouer |
| `volume` | de 0 à 2 |
| `loop` | lecture en boucle |
| `playOnStart` | démarre avec la partie |
| `spatial` | panoramique et atténuation selon la caméra ; sinon le son est « dans la tête » |

Il est disponible par plusieurs chemins :
- type de nœud « Source audio » (catégorie Audio) ;
- « Ajouter un composant » ;
- un son glissé de l'explorateur dans la vue (`Runtime::AddAudioSource`).

Dans l'inspecteur, la section « Source audio » expose ces réglages et un
bouton « Écouter / arrêter ».

Cycle de vie :
- **Au lancement d'une partie**, `StartAudioSources` lance les sources
  `playOnStart`. Chaque voix spatialisée suit son nœud.
- **Pendant la partie**, supprimer le nœud arrête sa voix.
- **À l'arrêt**, `EndScenePlay` coupe tout.

Dans l'explorateur, un double-clic sur un son l'écoute, et un second
double-clic arrête l'écoute.

### Scripts : `audio.*`

| Fonction | Effet |
|---|---|
| `audio.play(son, {volume, loop, pitch, position, node, spatial})` | joue un son, rend le numéro de voix ; `position: [x, y, z]` spatialise, `node: "Nom"` suit un nœud (nœud inconnu : erreur) |
| `audio.stop(v)`, `audio.stop_all()` | arrête |
| `audio.playing(v)` | vrai tant que la voix existe (en attente ou en lecture) |
| `audio.volume(v, x)`, `audio.pitch(v, x)`, `audio.set_position(v, x, y, z)` | règle une voix |
| `audio.master([x])` | volume général (rend la valeur courante) |
| `audio.preload(sons…)` (chemins ou listes) | décode à l'avance (en arrière-plan) : le premier `audio.play` part sans attendre |
| `audio.play_source(nœud)`, `audio.stop_source(nœud)` | pilote le composant AudioSource d'un nœud |

Un chemin de son relatif est cherché **d'abord dans le dossier du projet**
(`Runtime::ResolveProjectPath`), puis depuis le dossier courant : un projet
qui range ses sons dans son `assets/sounds/` les emporte avec lui.

## Exemple : l'ambiance sonore de Projet_1

Les sons utilisés (140 `.ogg`, 4,6 Mo) sont copiés dans
`data/game_editor_demo/projects/Projet_1/assets/sounds/`.

Le module `scripts/sons.script` (`const sons = import "sons"`) fournit :
- `variante(famille, options)`, qui tire une variante au hasard
  (`default_stone_footstep.1…5`) avec un léger écart de hauteur ;
- `Ambiance`, des couches en boucle en fondu enchaîné ;
- `Proximite`, une boucle accrochée à un nœud, qui n'a de voix qu'à portée
  de la caméra ;
- `Rumeurs`, des bruits isolés au hasard autour de la caméra.

| Où | Sons |
|---|---|
| Menu | musique du titre, souffle de caverne, clics de bois sur les boutons, torches qui crépitent |
| Génération | porte qui grince, vent, une pierre posée par étape du journal ; décodage anticipé des sons de la partie |
| Donjon | ambiance par genre d'étage en fondu (salles hautes : musique de caverne et vent ; égouts : gouttes ; profondeurs : plaintes) ; bruits isolés (éboulis, grincements, soupirs) ; pas selon le sol (dalles, trottoirs, eau) ; épée, coups reçus, or ; échelons ou marches pendant une traversée ; plouf dans un canal ; tintement du portail ; fin de partie |
| Monstres (`monstres.script`) | par espèce : pas au rythme de l'animation, cri en repérant le joueur, coup reçu, attaque, mort — accrochés au corps, muets au-delà de 14 m |
| Coffres, torches, canaux | grincement et tintement de l'or ; crépitement à 7 m ; murmure de l'eau et cascades |

Les sons ont été choisis sans écoute, d'après leur nom et leur
spectrogramme. Les correspondances sont donc approximatives : par exemple
`default_ambience_hot_stone_1` (des craquements brefs) sert de feu, et la
série `sulfur` (un souffle continu) sert d'eau courante.

## Tests

`tests/game_editor_audio_smoke_test.cpp` couvre :
- la géométrie partagée et non modifiée par l'édition d'un autre nœud ;
- le cube d'attente remplacé par le modèle chargé ;
- l'aller-retour du composant par le document ;
- les voix lancées et arrêtées en partie ;
- l'API `audio.*`.

Le test force `SDL_AUDIO_DRIVER=dummy`.
