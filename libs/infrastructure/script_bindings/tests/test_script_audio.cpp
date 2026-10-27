// Tests unitaires — script : le son (script_audio.hpp) sur le mixeur hors
// ligne — voix, fondus, canaux. Écrit UNIQUEMENT sous build/tests/tmp/.
#include "test.hpp"
#include <audio/clip.hpp>
#include <script/script.hpp>
#include <script_bindings/script_audio.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>

using namespace script;
namespace fs = std::filesystem;

namespace {

/// Valeur d'une expression (le script la rend).
Value Run(Interpreter &vm, const String &expression) {
	const String source = String("return ") + expression;
	auto result = vm.Run(source.View());
	if (result.IsError()) {
		test::ReportFailure(__FILE__, __LINE__, result.Error().Format().CStr());
		return Value::Nil();
	}
	return result.Value();
}

String RunError(Interpreter &vm, const char *source) {
	auto result = vm.Run(StringView(source));
	return result.IsOk() ? String() : result.Error().Format();
}

/// Son WAV (PCM 16 bits mono) d'une sinusoïde d'une seconde.
String Tone(const char *name, float hz) {
	const fs::path dir = "build/tests/tmp/script_audio";
	fs::create_directories(dir);
	const fs::path path = dir / name;
	const uint32_t rate = 22050, frames = rate, bytes = frames * 2;
	std::ofstream out(path, std::ios::binary);
	auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), 4); };
	auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char *>(&v), 2); };
	out.write("RIFF", 4);
	u32(36 + bytes);
	out.write("WAVEfmt ", 8);
	u32(16);
	u16(1);
	u16(1);
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

} // namespace

TEST(ScriptAudio, ChannelsSwitchSoundsWithFades) {
	resources::ResourceCache cache;
	audio::RegisterResourceFactories(cache);
	audio::AudioEngine engine(cache);
	engine.Start(false);
	Interpreter vm;
	// Noms courts dans les scripts, résolus vers les fichiers de test.
	const String a = Tone("a.wav", 440.f), b = Tone("b.wav", 660.f);
	InstallAudioLibrary(vm, engine, [&](const String &name) { return name == "a" ? a : name == "b" ? b : name; });
	auto settle = [&](float seconds) {
		(void)cache.WaitIdle();
		for (float t = 0.f; t < seconds - 1e-4f; t += 0.05f)
			engine.Update(0.05f);
	};

	Value fade = Run(vm, "audio.channel_fade(\"musique\", {fade_in: 1, fade_out: 1})");
	EXPECT_TRUE(fade.IsMap());
	const Value first = Run(vm, "audio.play_on(\"musique\", \"a\", {loop: true})");
	EXPECT_TRUE(first.IsNumber());
	settle(1.f);
	EXPECT_TRUE(std::abs(Run(vm, "audio.volume(audio.channel(\"musique\"))").AsNumber() - 1.0) < 1e-3);
	// Le même son continue ; un autre le remplace en fondu enchaîné.
	EXPECT_TRUE(Run(vm, "audio.play_on(\"musique\", \"a\", {loop: true})").AsNumber() == first.AsNumber());
	const Value second = Run(vm, "audio.play_on(\"musique\", \"b\", {loop: true, fade_in: 0.5})");
	EXPECT_TRUE(second.AsNumber() != first.AsNumber());
	settle(0.5f);
	vm.SetGlobal(String("premier"), first);
	EXPECT_TRUE(Run(vm, "audio.playing(premier)").IsTruthy()); // encore en fondu de sortie (1 s)
	settle(0.6f);
	EXPECT_TRUE(!Run(vm, "audio.playing(premier)").IsTruthy());
	// Arrêt du canal en fondu ; voix isolée en fondu.
	EXPECT_TRUE(Run(vm, "audio.stop_channel(\"musique\", 0.3)").IsTruthy());
	EXPECT_TRUE(Run(vm, "audio.channel(\"musique\")").IsNil());
	const Value voice = Run(vm, "audio.play(\"a\", {volume: 0.8, loop: true, fade_in: 0.5})");
	settle(0.6f);
	vm.SetGlobal(String("v"), voice);
	EXPECT_TRUE(std::abs(Run(vm, "audio.volume(v)").AsNumber() - 0.8) < 1e-3);
	EXPECT_TRUE(Run(vm, "audio.fade(v, 0, 0.2, true)").IsTruthy());
	settle(0.3f);
	EXPECT_TRUE(!Run(vm, "audio.playing(v)").IsTruthy());
	EXPECT_EQ(engine.VoiceCount(), size_t(0));
	// Fautes de frappe : refusées.
	EXPECT_TRUE(RunError(vm, "audio.play_on(\"musique\", \"a\", {fade: 2})").Contains("inconnue"));
	EXPECT_TRUE(RunError(vm, "audio.play(\"a\", {position: [1, 2]})").Contains("[x, y, z]"));
}
