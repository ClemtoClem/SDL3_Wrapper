#pragma once
/**
 * sandbox::AudioEngine — le moteur audio de la bibliothèque (audio/engine.hpp :
 * voix, sons partagés du ResourceCache, spatialisation, mixeur hors ligne
 * sans périphérique), sous son nom historique dans le sandbox.
 */
#include <audio/engine.hpp>

namespace sandbox {

using AudioEngine = audio::AudioEngine;
using AudioPlayOptions = audio::AudioPlayOptions;

} // namespace sandbox
