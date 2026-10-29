#pragma once
/**
 * script — le son (audio::AudioEngine) vu depuis le langage (« Script »),
 * dans l'espace de noms `audio`.
 *
 *   let v = audio.play("sons/porte.wav", {volume: 0.8, position: [2, 0, 5]})
 *   audio.fade(v, 0.2, 1.5)                  # vers 20 % en 1,5 s
 *   audio.stop(v, 2)                         # fondu de sortie de 2 s
 *
 * CANAUX : un seul son à la fois par canal ; en changer fond l'ancien en
 * sortie et le nouveau en entrée (en même temps, ou l'un après l'autre avec
 * `sequential`). Le même son déjà en cours continue, sauf `restart`.
 *
 *   audio.channel_fade("musique", {fade_in: 3, fade_out: 4})   # réglage du canal
 *   audio.play_on("musique", "musiques/foret.ogg", {loop: true})
 *   audio.play_on("musique", "musiques/grotte.ogg", {loop: true, fade_out: 1})
 *   audio.stop_channel("musique", 5)
 *   audio.channel("musique")                 # voix en cours (nil sinon)
 *
 * Options de `audio.play` : volume, loop, pitch, position ([x, y, z] :
 * voix spatialisée), spatial, fade_in. Celles de `audio.play_on` ajoutent
 * fade_out, sequential, restart. Une clé inconnue est une erreur.
 */
#include <functional>

#include <audio/engine.hpp>
#include <script/script_std.hpp>

namespace script {

/// Chemin d'un son tel qu'écrit dans un script → chemin à charger (projet,
/// mod…) ; par défaut, tel quel.
using AudioPathResolver = std::function<String(const String &)>;

/// Installe l'espace de noms `audio` (cf. en-tête) ; `engine` doit survivre
/// à l'interpréteur.
void InstallAudioLibrary(Interpreter &vm, audio::AudioEngine &engine, AudioPathResolver resolve = {});

} // namespace script
