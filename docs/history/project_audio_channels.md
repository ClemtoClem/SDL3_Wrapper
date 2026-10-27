---
name: project-audio-channels
description: "Canaux et fondus audio (2026-10-27) : audio::AudioEngine (FadeTo, Stop en fondu, PlayOnChannel/StopChannel), bibliothèque script `audio.*` de script_bindings, game.ambient sur canaux dans voxel, ambiances des backrooms et musique du core sans superposition."
metadata:
  type: project
---

Constat : dans les backrooms, l'ambiance d'un niveau (fichiers de 2 à 9 min)
était relancée toutes les 25 à 45 s sans arrêter la précédente — plusieurs
couches superposées, et rien ne s'arrêtait en changeant de niveau ; la musique
du core jouait aussi dans les backrooms.

- `audio::AudioEngine` : fondus par voix (`FadeTo`, `Stop(voix, fondu)`,
  `EffectiveVolume`) ; CANAUX nommés (`PlayOnChannel`, `StopChannel`,
  `ChannelVoice`, `SetChannelFade` / `ChannelFadeOf`) — une voix à la fois,
  l'ancienne fondue en sortie, la nouvelle en entrée, en fondu enchaîné ou
  `sequential` ; le même son déjà en cours continue (sauf `restart`).
  `Update()` mesure le temps réel, `Update(dt)` pour les tests. Les fondus
  n'avancent que sur une voix qui joue (un son encore en chargement attend).
- `script_bindings` (dépend désormais d'`audio`) : `InstallAudioLibrary(vm,
  engine, résolveur)` — `audio.play/stop/stop_all/playing/volume/fade/pitch/
  set_position/master`, `audio.play_on/stop_channel/channel/channel_fade` ;
  options refusées si inconnues. Le sandbox garde son propre `audio.*`
  (nœuds suivis, chemins du projet) : pas encore branché sur la bibliothèque.
- voxel : `Game::PlayAmbient(son, volume, {channel, fadeIn, fadeOut, loop})`,
  `StopAmbient(canal, fondu)` ; `SoundEvent` porte canal, fondus, boucle,
  arrêt. Script : `game.ambient(id [, volume] [, {volume, channel, fade_in,
  fade_out, loop}])`, `game.stop_ambient(canal [, fondu])`. La musique d'une
  dimension passe sur le canal « dimension » (et s'éteint en fondu vers une
  dimension sans musique). Voxelia joue ces événements sur ses canaux ; une
  catégorie à volume nul éteint le canal ; « mod:x_amb_y » / « _music »
  comptent dans les volumes ambiance / musique.
- backrooms : ambiance du niveau en boucle sur le canal « backrooms »,
  changée (fondus 5 s / 4 s) seulement quand le biome change, éteinte en
  quittant la dimension. core : canaux « music » et « ambience », seulement
  dans les dimensions `core:` (éteints en fondu ailleurs).
- Au passage : chemins de sons cassés par le déplacement des sons corrigés
  dans `core/sounds/assets.json` (hibou, lave, crunch_splat, squelching).

Tests : `Engine_VoicesFadeAndStopGently`, `Engine_ChannelsCrossfadeOneSoundAtATime`
(audio, mixeur hors ligne), `ScriptAudio.ChannelsSwitchSoundsWithFades`,
`Game_AmbientChannelsCarryFades`.
