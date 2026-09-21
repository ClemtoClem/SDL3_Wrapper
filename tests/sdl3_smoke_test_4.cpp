// Smoke test : audio (AudioStream, loadWav) et mixer (Mixer, MixAudio, MixTrack).
// Utilise le driver audio "dummy" — pas de son réellement joué sur le matériel.
#include "sdl3/sdl3.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
	setenv("SDL_AUDIODRIVER", "dummy", 0);

	using namespace sdl3;

	auto sdl = sdl3::SdlContext::Create(init_flags::AUDIO);
	if (!sdl) {
		std::cerr << "sdl init failed: " << sdl3::GetError().CStr() << "\n";
		return 1;
	}

	// --- AudioStream ---
	auto streamRes = AudioStream::OpenPlayback(AudioSpec::StereoF32());
	if (!streamRes) {
		std::cerr << "audio stream failed: " << streamRes.Error().CStr() << "\n";
		return 1;
	}
	auto &stream = streamRes.Value();

	std::vector<float> silence(256 * 2, 0.f);
	stream.PutData(std::span<const float>(silence));
	std::cout << "audio stream available=" << stream.Available() << "\n";
	stream.SetGain(0.5f);
	std::cout << "audio stream gain=" << stream.Gain() << "\n";
	stream.Resume();
	stream.Pause();

	// --- WAV loading (skip gracefully if no .wav asset is present) ---
	auto wavRes = LoadWav("assets/sounds/backrooms_amb_city.ogg");
	if (!wavRes)
		std::cout << "loadWav on .ogg failed as expected (WAV-only loader): " << wavRes.Error().CStr() << "\n";

	// --- Mixer ---
	auto mixCtx = MixerContext::Create();
	if (!mixCtx) {
		std::cerr << "mixer init failed: " << mixCtx.Error().CStr() << "\n";
		return 1;
	}

	AudioSpec spec;
	auto mixerRes = Mixer::Create(spec);
	if (!mixerRes) {
		std::cerr << "mixer device failed: " << mixerRes.Error().CStr() << "\n";
		return 1;
	}
	auto &mixer = mixerRes.Value();

	auto audioRes = mixer.LoadAudio("assets/sounds/backrooms_amb_city.ogg");
	if (!audioRes) {
		std::cerr << "loadAudio failed: " << audioRes.Error().CStr() << "\n";
		return 1;
	}
	auto &audio = audioRes.Value();
	std::cout << "audio duration=" << audio.Duration() << " sample frames\n";

	auto trackRes = mixer.CreateTrack();
	if (!trackRes) {
		std::cerr << "createTrack failed: " << trackRes.Error().CStr() << "\n";
		return 1;
	}
	auto &track = trackRes.Value();
	track.SetAudio(audio);
	track.Play(0);
	std::cout << "track playing=" << track.IsPlaying() << " gain=" << track.Gain() << "\n";
	track.Stop();
	mixer.StopAll();

	std::cout << "smoke test 4 done\n";
	return 0;
}
