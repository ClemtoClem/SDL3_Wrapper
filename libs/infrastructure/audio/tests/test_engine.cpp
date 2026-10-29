#include "test.hpp"
// Tests — audio::AudioEngine sur le mixeur HORS LIGNE : fondus des voix et
// canaux (un son à la fois, fondu enchaîné ou séquentiel). Écrit UNIQUEMENT
// sous build/tests/tmp/audio/.
#include <audio/clip.hpp>
#include <audio/engine.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>

using namespace audio;
namespace fs = std::filesystem;

namespace {

/// Son WAV (PCM 16 bits mono) d'une sinusoïde.
String WriteTone(const char *name, float hz, float seconds) {
	const fs::path dir = "build/tests/tmp/audio";
	fs::create_directories(dir);
	const fs::path path = dir / name;
	const uint32_t rate = 22050, frames = uint32_t(float(rate) * seconds), bytes = frames * 2;
	std::ofstream out(path, std::ios::binary);
	auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), 4); };
	auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char *>(&v), 2); };
	out.write("RIFF", 4);
	u32(36 + bytes);
	out.write("WAVEfmt ", 8);
	u32(16);
	u16(1); // PCM
	u16(1); // mono
	u32(rate);
	u32(rate * 2);
	u16(2);
	u16(16);
	out.write("data", 4);
	u32(bytes);
	for (uint32_t i = 0; i < frames; ++i)
		u16(uint16_t(int16_t(8000.f * std::sin(6.2831853f * hz * float(i) / float(rate)))));
	return String(fs::absolute(path).string().c_str());
}

struct Bench {
	resources::ResourceCache cache;
	AudioEngine engine{cache};
	resources::AssetRef<AudioClipResource> a, b;
	Bench() {
		RegisterResourceFactories(cache);
		engine.Start(false);
		a = ImportAudio(cache, WriteTone("a.wav", 440.f, 2.f), resources::LoadPolicy::SYNC);
		b = ImportAudio(cache, WriteTone("b.wav", 660.f, 2.f), resources::LoadPolicy::SYNC);
	}
	void Run(float seconds) {
		for (float t = 0.f; t < seconds - 1e-4f; t += 0.05f)
			engine.Update(0.05f);
	}
};

} // namespace

TEST(Engine_VoicesFadeAndStopGently) {
	Bench s;
	REQUIRE(s.engine.IsStarted());
	AudioPlayOptions loop;
	loop.loop = true;
	const AudioEngine::VoiceId v = s.engine.Play(s.a, loop);
	s.engine.Update(0.f);
	REQUIRE(s.engine.IsPlaying(v));
	CHECK(s.engine.FadeTo(v, 0.5f, 1.f));
	s.Run(0.5f);
	CHECK_NEAR(s.engine.EffectiveVolume(v), 0.75f, 0.05f); // à mi-chemin
	s.Run(0.6f);
	CHECK_NEAR(s.engine.EffectiveVolume(v), 0.5f, 1e-4f);
	// Arrêt en douceur : la voix vit jusqu'à la fin du fondu.
	CHECK(s.engine.Stop(v, 1.f));
	s.Run(0.5f);
	CHECK(s.engine.IsPlaying(v));
	CHECK(s.engine.EffectiveVolume(v) < 0.5f);
	s.Run(0.6f);
	CHECK(!s.engine.IsPlaying(v));
}

TEST(Engine_ChannelsCrossfadeOneSoundAtATime) {
	Bench s;
	AudioPlayOptions loop;
	loop.loop = true;
	s.engine.SetChannelFade("musique", ChannelFade{1.f, 1.f});
	const AudioEngine::VoiceId first = s.engine.PlayOnChannel("musique", s.a, loop);
	s.engine.Update(0.f);
	CHECK_EQ(s.engine.ChannelVoice("musique"), first);
	CHECK_NEAR(s.engine.EffectiveVolume(first), 0.f, 1e-4f); // fondu d'entrée
	s.Run(1.f);
	CHECK_NEAR(s.engine.EffectiveVolume(first), 1.f, 1e-4f);

	// Le même son : il continue (pas de nouvelle voix).
	CHECK_EQ(s.engine.PlayOnChannel("musique", s.a, loop), first);

	// Un autre son : fondu enchaîné, l'ancien s'éteint, le nouveau monte.
	const AudioEngine::VoiceId second = s.engine.PlayOnChannel("musique", s.b, loop);
	CHECK(second != first);
	CHECK_EQ(s.engine.ChannelVoice("musique"), second);
	s.Run(0.5f);
	CHECK(s.engine.IsPlaying(first) && s.engine.IsPlaying(second));
	CHECK_NEAR(s.engine.EffectiveVolume(first), 0.5f, 0.06f);
	CHECK_NEAR(s.engine.EffectiveVolume(second), 0.5f, 0.06f);
	s.Run(0.6f);
	CHECK(!s.engine.IsPlaying(first));
	CHECK_NEAR(s.engine.EffectiveVolume(second), 1.f, 1e-4f);

	// Séquentiel : le nouveau attend que l'ancien se soit tu.
	const AudioEngine::VoiceId third = s.engine.PlayOnChannel("musique", s.a, loop, Some(ChannelFade{0.5f, 1.f, true}));
	s.Run(0.5f);
	CHECK(s.engine.IsPlaying(second));
	CHECK_NEAR(s.engine.EffectiveVolume(third), 0.f, 1e-4f);
	s.Run(0.6f);
	CHECK(!s.engine.IsPlaying(second));
	s.Run(0.6f);
	CHECK_NEAR(s.engine.EffectiveVolume(third), 1.f, 1e-4f);

	// Un autre canal est indépendant ; l'arrêt d'un canal le libère aussitôt.
	const AudioEngine::VoiceId ambience = s.engine.PlayOnChannel("ambiance", s.b, loop);
	CHECK(s.engine.StopChannel("musique", Some(0.5f)));
	CHECK_EQ(s.engine.ChannelVoice("musique"), AudioEngine::NO_VOICE);
	s.Run(0.6f);
	CHECK(!s.engine.IsPlaying(third));
	CHECK(s.engine.IsPlaying(ambience));
	CHECK(!s.engine.StopChannel("musique"));
}
