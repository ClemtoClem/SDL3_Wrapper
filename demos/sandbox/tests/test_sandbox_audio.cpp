// Tests unitaires — game_editor : ressources partagées et son.
//  - deux nœuds de la même primitive montrent la MÊME géométrie (cache), et
//    modifier l'un ne touche jamais l'autre ;
//  - un modèle glTF se charge en arrière-plan derrière un cube d'attente ;
//  - le composant AudioSource fait l'aller-retour par le document ;
//  - en partie, les sources « jouer au démarrage » ont une voix, coupée à
//    l'arrêt ; l'API de script `audio.*` joue, règle et arrête des voix.
#include <sandbox/document/project_files.hpp>
#include <sandbox/engine/runtime.hpp>
#include "test.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace sandbox;

namespace {

/// Un WAV de silence (PCM 16 bits mono, 8000 Hz) : assez long pour être
/// encore en lecture pendant tout le test, même sur un vrai périphérique.
String WriteWav(const char* name, int frames) {
	const std::string dir = "build/tests/tmp/game_editor_audio";
	std::filesystem::create_directories(dir);
	const std::string path = dir + "/" + name;
	FILE* file = std::fopen(path.c_str(), "wb");
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, file); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, file); };
	const uint32_t dataBytes = uint32_t(frames) * 2;
	std::fwrite("RIFF", 1, 4, file);
	u32(36 + dataBytes);
	std::fwrite("WAVEfmt ", 1, 8, file);
	u32(16);
	u16(1);
	u16(1);
	u32(8000);
	u32(16000);
	u16(2);
	u16(16);
	std::fwrite("data", 1, 4, file);
	u32(dataBytes);
	for (int i = 0; i < frames; ++i)
		u16(0);
	std::fclose(file);
	return String(path.c_str());
}

struct Harness {
	ecs::ArchetypeRegistry registry;
	Runtime runtime{registry};

	Harness() {
		// Jamais de son réel pendant les tests (et un résultat identique sur
		// une machine sans carte son).
		setenv("SDL_AUDIO_DRIVER", "dummy", 0);
		runtime.OpenProject(files::MakeBlankProject(String("Audio")));
	}

	render3d::Shape* ShapeOf(scene::NodeId id) {
		return dynamic_cast<render3d::Shape*>(runtime.FindObject3D(id));
	}

	script::Value Run(const char* source) {
		auto result = runtime.GameplayVm().Run(StringView(source));
		if (result.IsError()) {
			test::ReportFailure(__FILE__, __LINE__, result.Error().Format().CStr());
			return script::Value::Nil();
		}
		return result.Value();
	}
};

NodeDesc Box(const char* name, math::FVector3 size) {
	NodeDesc box;
	box.name = String(name);
	box.dimensions = size;
	return box;
}

} // namespace

TEST(EditorResources, PrimitivesShareGeometryAndEditsDontLeak) {
	Harness h;
	const scene::NodeId a = h.runtime.SpawnNode(Box("A", {2.f, 1.f, 1.f})).Unwrap();
	const scene::NodeId b = h.runtime.SpawnNode(Box("B", {2.f, 1.f, 1.f})).Unwrap();
	ASSERT_TRUE(h.ShapeOf(a) && h.ShapeOf(b));
	EXPECT_TRUE(h.ShapeOf(a)->SharedGeometry() == h.ShapeOf(b)->SharedGeometry());
	const std::shared_ptr<render3d::Mesh> shared = h.ShapeOf(b)->SharedGeometry();
	const size_t vertices = shared->Vertices().size();

	// Changer les dimensions de A lui donne une AUTRE géométrie ; B garde la sienne.
	VisualDesc visual = VisualDesc::Read(*h.runtime.FindNode(a));
	visual.dimensions = {5.f, 1.f, 1.f};
	ASSERT_TRUE(h.runtime.SetVisual(a, visual));
	EXPECT_FALSE(h.ShapeOf(a)->SharedGeometry() == h.ShapeOf(b)->SharedGeometry());
	EXPECT_TRUE(h.ShapeOf(b)->SharedGeometry() == shared);
	EXPECT_EQ(shared->Vertices().size(), vertices);
	EXPECT_TRUE(std::fabs((shared->LocalBounds().max.x) - (1.f)) <= 1e-4f);
}

TEST(EditorResources, ModelsLoadInBackgroundBehindAPlaceholder) {
	Harness h;
	NodeDesc cow;
	cow.name = String("Vache");
	cow.shape = ShapeKind::MODEL;
	cow.source = String("assets/models/animals/Cow.gltf");
	const scene::NodeId id = h.runtime.SpawnNode(cow).Unwrap();
	ASSERT_TRUE(h.ShapeOf(id) != nullptr);
	// Une seconde vache partage la ressource, qu'elle soit prête ou non.
	cow.name = String("Vache 2");
	const scene::NodeId twin = h.runtime.SpawnNode(cow).Unwrap();

	auto ref = render3d::ImportMesh(h.runtime.Resources(), cow.source);
	for (int i = 0; i < 2000 && !h.runtime.Resources().GetReady(ref); ++i)
		sdl3::DelayMS(2);
	ASSERT_TRUE(h.runtime.Resources().GetReady(ref) != nullptr);
	h.runtime.Update(1.f / 60.f); // pose la géométrie chargée à la place du cube
	const std::shared_ptr<render3d::Mesh> geometry =
		h.runtime.Resources().GetReady(ref)->SharedGeometry();
	EXPECT_TRUE(h.ShapeOf(id)->SharedGeometry() == geometry);
	EXPECT_TRUE(h.ShapeOf(twin)->SharedGeometry() == geometry);
	EXPECT_TRUE(geometry->Vertices().size() > size_t(24));
}

TEST(EditorAudio, AudioSourceRoundTripsThroughTheDocument) {
	AudioSourceDesc source;
	source.clip = String("assets/sounds/bip.wav");
	source.volume = 0.5f;
	source.loop = true;
	source.playOnStart = false;
	source.spatial = false;
	scene::Node node;
	source.Write(node);
	ASSERT_TRUE(AudioSourceDesc::Has(node));
	const AudioSourceDesc back = AudioSourceDesc::Read(node);
	EXPECT_TRUE(back.clip == source.clip);
	EXPECT_TRUE(std::fabs((back.volume) - (0.5f)) <= 1e-5f);
	EXPECT_TRUE(back.loop);
	EXPECT_FALSE(back.playOnStart);
	EXPECT_FALSE(back.spatial);

	Option<NodeDesc> made = MakeNodeFromTemplate(String("audio_source"));
	ASSERT_TRUE(made.IsSome());
	EXPECT_TRUE(AudioSourceDesc::Has(made.Value().ToNode()));
}

TEST(EditorAudio, PlayModeStartsSourcesAndStopSilencesThem) {
	Harness h;
	const String wav = WriteWav("boucle.wav", 8000 * 30);
	auto created = h.runtime.AddAudioSource(wav);
	ASSERT_TRUE(created.IsOk());
	const scene::NodeId source = created.Value();
	AudioSourceDesc desc = AudioSourceDesc::Read(*h.runtime.FindNode(source));
	desc.loop = true;
	ASSERT_TRUE(h.runtime.SetAudioSource(source, desc));

	h.runtime.Play();
	const AudioEngine::VoiceId voice = h.runtime.VoiceOf(source);
	ASSERT_TRUE(voice != AudioEngine::NO_VOICE);
	EXPECT_TRUE(h.runtime.Audio().IsStarted());
	for (int i = 0; i < 5; ++i)
		h.runtime.Update(1.f / 60.f);
	EXPECT_TRUE(h.runtime.Audio().IsPlaying(voice));
	// Volume réglé en cours de partie : la voix suit.
	desc.volume = 0.25f;
	EXPECT_TRUE(h.runtime.SetAudioSource(source, desc));
	h.runtime.Stop();
	EXPECT_FALSE(h.runtime.Audio().IsPlaying(voice));
	EXPECT_EQ(h.runtime.Audio().VoiceCount(), size_t(0));
}

TEST(EditorAudio, ScriptsPlayAndStopVoices) {
	Harness h;
	const String wav = WriteWav("bip.wav", 8000 * 30);
	(void)h.runtime.SpawnNode(Box("Haut-parleur", {1.f, 1.f, 1.f}));
	h.runtime.Play();
	h.Run(String::Format(
			  "var v = audio.play(\"%s\", {volume: 0.8, loop: true, node: \"Haut-parleur\"})\n"
			  "var w = audio.play(\"%s\", {position: [3, 0, 0], pitch: 1.5})\n",
			  wav.CStr(), wav.CStr())
			  .CStr());
	h.runtime.Update(1.f / 60.f);
	EXPECT_TRUE(h.Run("return audio.playing(v) and audio.playing(w)").IsTruthy());
	EXPECT_TRUE(h.Run("return audio.volume(v, 0.3)").IsTruthy());
	EXPECT_TRUE(h.Run("return audio.stop(v)").IsTruthy());
	EXPECT_FALSE(h.Run("return audio.playing(v)").IsTruthy());
	EXPECT_TRUE(std::fabs((float(h.Run("return audio.master(0.5)").AsNumber())) - (0.5f)) <= 1e-5f);
	// Un nœud inconnu est une erreur de script, pas un silence.
	auto bad =
		h.runtime.GameplayVm().Run(StringView("audio.play(\"x.wav\", {node: \"Personne\"})"));
	EXPECT_TRUE(bad.IsError());
	h.Run("audio.stop_all()");
	EXPECT_EQ(h.runtime.Audio().VoiceCount(), size_t(0));
	h.runtime.Stop();
}

TEST(EditorAudio, SoundsAreFoundInTheProjectFolderAndPreloaded) {
	ecs::ArchetypeRegistry registry;
	Runtime runtime{registry};
	setenv("SDL_AUDIO_DRIVER", "dummy", 0);
	const std::filesystem::path dir =
		std::filesystem::temp_directory_path() / "game_editor_audio_project";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir / "assets" / "sounds");
	// Le son n'existe QUE dans le dossier du projet.
	const String wav = WriteWav("projet.wav", 8000 * 30);
	std::filesystem::copy_file(wav.CStr(), dir / "assets" / "sounds" / "projet.wav");
	(void)files::SaveProject(files::MakeBlankProject(String("Sons")),
							 String((dir / "sons.json").string().c_str()));
	ASSERT_TRUE(runtime.LoadProjectFile(String((dir / "sons.json").string().c_str())).IsOk());
	runtime.Play();
	auto preload =
		runtime.GameplayVm().Run(StringView("return audio.preload(\"assets/sounds/projet.wav\")"));
	ASSERT_TRUE(preload.IsOk());
	EXPECT_EQ(int(preload.Value().AsNumber()), 1);
	auto played = runtime.GameplayVm().Run(StringView(
		"var v = audio.play(\"assets/sounds/projet.wav\", {loop: true})\nreturn audio.playing(v)"));
	ASSERT_TRUE(played.IsOk());
	EXPECT_TRUE(played.Value().IsTruthy());
	for (int i = 0; i < 3; ++i)
		runtime.Update(1.f / 60.f);
	EXPECT_TRUE(runtime.GameplayVm().Run(StringView("return audio.playing(v)")).Value().IsTruthy());
	EXPECT_TRUE(runtime.Audio().TakeErrors().empty());
	runtime.Stop();
	std::filesystem::remove_all(dir);
}
