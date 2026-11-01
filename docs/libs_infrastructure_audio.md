# audio — sons, moteur audio et traitement du signal

`libs/infrastructure/audio` · couche **infrastructure** · `libaudio.a` · espace de noms `audio::`

## Présentation

`audio` réunit trois briques :

1. **`AudioClipResource`** : un son sous forme de ressource
   ([`resources`](libs_infrastructure_resources.md)). Il est chargé et
   **décodé en PCM en arrière-plan** par le `ResourceCache`, identifié par un
   `AssetId` stable et partagé par tous ceux qui le jouent. Le WAV est
   toujours pris en charge ; OGG, MP3, FLAC… le sont par les décodeurs de
   SDL3_mixer.
2. **`AudioEngine`** : le son des jeux et des éditeurs, sur SDL3_mixer.
   - Une **voix** est une lecture en cours, désignée par un identifiant.
     Elle peut être demandée avant que son son soit prêt, et démarre dès qu'il
     l'est.
   - Le volume, la hauteur et les fondus se règlent par voix.
   - Les **canaux nommés** ne jouent qu'un son à la fois, avec un fondu
     enchaîné ou séquentiel : idéal pour la musique et les ambiances.
   - Les voix **spatialisées** sont placées par rapport à l'auditeur (la
     caméra).
   - Sans périphérique audio, le moteur utilise un mixeur hors ligne.
3. **DSP** (`dsp.hpp`) : signaux, fenêtres, FFT radix-2, spectre en dB,
   RMS et crête, filtres biquadratiques RBJ (passe-bas, passe-haut,
   passe-bande) et écrêtage doux.

Les exemples [audio_signal_generator](example_audio_signal_generator.md),
[audio_spectrum_analyzer](example_audio_spectrum_analyzer.md) et
[audio_patchbay](example_audio_patchbay.md) se servent du DSP. La
[sandbox](demo_sandbox.md) et [voxelia](demo_voxelia.md) se servent du moteur.

## Architecture interne

| Fichier | Contenu |
|---|---|
| `audio.hpp` | En-tête parapluie (`clip.hpp` et `dsp.hpp` seulement : **`engine.hpp` s'inclut à part**) |
| `clip.hpp`, `src/clip.cpp` | `AudioClipResource`, `RegisterResourceFactories`, `IsAudioFile`, `ImportAudio` |
| `engine.hpp`, `src/engine.cpp` | `AudioPlayOptions`, `ChannelFade`, `AudioEngine` |
| `dsp.hpp`, `src/dsp.cpp` | `Signal`, fenêtres, FFT, mesures, biquads, `SoftClip` |

Dépendances : `resources`, `sdl3` (`WavData`, mixeur), `math` (positions).

## API publique

### Sons (`clip.hpp`)

| Élément | Rôle |
|---|---|
| `AudioClipResource::FromFile(source, cheminDisque)` | Ressource non chargée |
| `Frequency()`, `Channels()`, `DurationSeconds()` | Valables quand `IsReady()` |
| `RegisterResourceFactories(cache)` | Apprend au cache à recréer les sons (`AssetType::AUDIO_CLIP`) |
| `IsAudioFile(chemin)` | Extension reconnue |
| `ImportAudio(cache, chemin, LoadPolicy)` | Catalogue le fichier et lance son chargement ; rend un `AssetRef<AudioClipResource>` |

> Les décodeurs OGG et MP3 demandent que SDL3_mixer ait été initialisé
> **une fois, sur le fil principal** (`sdl3::MixerContext::Create`, ou
> `AudioEngine::Start`), avant que ces sons ne se chargent sur un fil de
> fond.

### `AudioEngine`

| Membre | Rôle |
|---|---|
| `AudioEngine(ResourceCache&)` | Le moteur emprunte le cache de son propriétaire |
| `Start(allowDevice = true)`, `Shutdown()`, `IsStarted()`, `HasDevice()`, `Status()` | Ouvre le périphérique par défaut, ou à défaut un mixeur hors ligne |
| `VoiceId Play(AssetRef | chemin, AudioPlayOptions)` | Nouvelle voix (`NO_VOICE` = 0 en cas d'échec) |
| `Stop(voix [, fondu])`, `StopAll()`, `FadeTo(voix, volume, secondes, stopAtEnd)` | Arrêt et fondus |
| `PlayOnChannel(canal, son, options)`, `StopChannel(canal, fondu)`, `ChannelVoice(canal)`, `SetChannelFade(canal, ChannelFade)` | Canaux à un seul son (musique, ambiance) |
| `IsPlaying`, `SetVolume`, `EffectiveVolume`, `SetPitch`, `SetPosition` | Réglages d'une voix |
| `SetMasterVolume`, `SetListener(matriceVue)` | Volume général ; auditeur (x à droite, y en haut, −z devant) |
| `Update()` / `Update(dt)` | **Une fois par image** : démarre les voix dont le son est devenu prêt, avance les fondus, spatialise |
| `VoiceCount()`, `TakeErrors()` | État et erreurs accumulées |

Options d'une voix (`AudioPlayOptions`) :

- `volume` (0 à 2) et `loop` ;
- `pitch` : rapport de fréquences (0,5 = une octave plus bas) ;
- `spatial` et `position` (monde).

Fondu d'un canal (`ChannelFade`) :

- `fadeIn` et `fadeOut`, en secondes ;
- `sequential` : le nouveau son attend la fin du fondu de sortie ;
- `restart` : rejoue le son en cours depuis le début.

### DSP (`dsp.hpp`)

- **`Signal {samples, sampleRate}`**.
- **Fenêtres** : `WindowHann`, `WindowHamming`, `WindowBlackman`, `ApplyWindow(data, WindowFunction)`.
- **FFT** :
  - `ProcessFFT(samples)` : Cooley-Tukey ; la taille doit être une puissance
    de deux (`IsPowerOfTwo`) ;
  - `ProcessFFTMagnitudeDb(spectre, plancher)` : spectre en dB ;
  - `ProcessFFTFrequencies(taille, fréquence)` : fréquence de chaque case.
  - Pour un signal réel, seule la première moitié du spectre est utile.
- **Mesures** : `GetRMS`, `GetPeak`.
- **Filtres** :
  - `BiQuadDesign(kind, fs, coupure, q)` calcule les coefficients ;
  - `BiQuadSetDesign(state, …)` les recalcule sans réinitialiser la ligne à
    retard, ce qui permet de balayer la coupure en direct ;
  - `BiQuadStep(state, x)` filtre un échantillon.
- **Écrêtage** : `SoftClip(x | span, seuil)`, avec un coude en tanh.

## Exemple

```cpp
#include <cmath>
#include <cstdio>
#include <audio/audio.hpp>
#include <audio/engine.hpp>
#include <sdl3/sdl3.hpp>

int main() {
	// ── DSP : un signal de 440 Hz + 3 kHz, filtré passe-bas, puis analysé ──
	audio::Signal sig;
	sig.sampleRate = 48000;
	for (int i = 0; i < 4096; ++i) {
		float t = float(i) / float(sig.sampleRate);
		sig.samples.push_back(0.5f * std::sin(2.f * 3.14159265f * 440.f * t) + 0.3f * std::sin(2.f * 3.14159265f * 3000.f * t));
	}
	audio::BiQuadState lowPass;
	audio::BiQuadSetDesign(lowPass, audio::BiQuadKind::LOW_PASS, float(sig.sampleRate), 1000.f);
	for (float &s : sig.samples)
		s = audio::BiQuadStep(lowPass, s);

	audio::ApplyWindow(sig.samples, audio::WindowFunction::Hann);
	std::vector<audio::Complex> spectrum = audio::ProcessFFT(sig.samples);
	std::vector<float> db = audio::ProcessFFTMagnitudeDb(spectrum);
	std::vector<float> freq = audio::ProcessFFTFrequencies(sig.samples.size(), sig.sampleRate);
	size_t peak = 1;
	for (size_t i = 1; i < sig.samples.size() / 2; ++i)
		if (db[i] > db[peak])
			peak = i;
	std::printf("pic à %.0f Hz, RMS %.3f\n", freq[peak], audio::GetRMS(sig.samples));

	// ── Moteur : sons partagés du ResourceCache, joués par des voix ──
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::AUDIO);
	resources::ResourceCache cache("assets/sounds");
	audio::RegisterResourceFactories(cache);
	audio::AudioEngine engine(cache);
	engine.Start(/*allowDevice=*/false); // mixeur hors ligne : aucun périphérique requis
	auto music = audio::ImportAudio(cache, "game-music-cave.ogg");
	audio::AudioEngine::VoiceId v = engine.PlayOnChannel("music", music, {.volume = 0.8f, .loop = true});
	engine.SetChannelFade("music", {.fadeIn = 1.f, .fadeOut = 1.f});
	for (int i = 0; i < 50 && !engine.IsPlaying(v); ++i) {
		cache.WaitIdle(std::chrono::milliseconds(20));
		engine.Update(0.02f); // démarre la voix dès que le son est décodé
	}
	std::printf("moteur : %s, voix active : %d\n", engine.Status().CStr(), engine.IsPlaying(v));
	for (const String &e : engine.TakeErrors())
		std::printf("erreur : %s\n", e.CStr());
	return 0;
}
```

Sortie (`SDL_AUDIO_DRIVER=dummy`) :

```
pic à 445 Hz, RMS 0.213
moteur : hors ligne, voix active : 1
```

Le pic tombe sur la case de 445 Hz : avec 4096 échantillons à 48 kHz, la
résolution est de 11,7 Hz.

## Intégration

`modules = ["audio"]`.

1. Créez le `ResourceCache`.
2. Appelez `audio::RegisterResourceFactories(cache)`.
3. Construisez l'`AudioEngine` et appelez `Start()` **sur le fil
   principal**.
4. Appelez `engine.Update(dt)` à chaque image.

## Tests

```sh
cpp-maker test -m audio
```

`test_dsp.cpp` couvre la FFT, les fenêtres et les filtres ;
`test_engine.cpp` couvre les voix, les canaux et les fondus, sur le mixeur
hors ligne.
