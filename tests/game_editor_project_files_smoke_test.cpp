// Tests unitaires — stockage des projets de l'éditeur (examples/game_editor_demo/
// project_files.hpp) : un dossier par projet (manifeste .json, assets/,
// scenes/*.scene, scripts/*.script), scènes et scripts « enregistrés sous » en
// un seul fichier, lecture des anciens manifestes tout-en-un.
//
// Écrit UNIQUEMENT sous build/tests/tmp/ (ignoré par git) ; lit le projet de
// démonstration de tests/data/ sans jamais le modifier.
#define USE_TEST

#include "core/test.hpp"

#include <filesystem>

#include "../examples/game_editor_demo/document/project_files.hpp"
#include "../examples/game_editor_demo/engine/runtime.hpp"

using namespace game_editor;

namespace {

constexpr const char *DEMO_PROJECT = "tests/data/game_editor_demo/demo/demo.json";

/// Dossier de travail vidé au début de chaque test.
String Scratch(const char *name) {
	const std::string path = std::string("build/tests/tmp/project_files/") + name;
	std::error_code ignored;
	std::filesystem::remove_all(path, ignored);
	std::filesystem::create_directories(path, ignored);
	return String(path.c_str());
}

bool Exists(const String &path) { return sdl3::filesystem::PathInfo(path).IsSome(); }

} // namespace

TEST(ProjectFiles, CreatingAProjectLaysOutItsFolder) {
	const String root = Scratch("create");
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	EXPECT_FALSE(runtime.HasProject()); // aucun projet tant qu'on n'en ouvre pas

	auto created = runtime.CreateProject(root, String("Mon jeu"));
	ASSERT_TRUE(created.IsOk());
	EXPECT_TRUE(runtime.HasProject());
	const String dir = files::Join(root, String("Mon jeu"));
	EXPECT_EQ(runtime.ProjectPath(), files::Join(dir, String("Mon jeu.json")));
	for (const char *sub : {"assets", "scenes", "scripts"})
		EXPECT_TRUE(files::IsDirectory(files::Join(dir, String(sub))));
	EXPECT_TRUE(files::IsFile(files::Join(dir, String("scenes/Scène principale.scene"))));
	// Un projet vierge : une scène vide, aucun script.
	EXPECT_EQ(runtime.GetProject().scenes.size(), size_t(1));
	EXPECT_EQ(runtime.GetProject().TotalObjectCount(), size_t(0));
	EXPECT_TRUE(runtime.GetProject().scripts.empty());
	// Un second projet du même nom est refusé (le dossier existe).
	EXPECT_TRUE(runtime.CreateProject(root, String("Mon jeu")).IsError());
	// Il apparaît dans la liste des projets.
	std::vector<files::ProjectEntry> projects = files::ListProjects(root);
	ASSERT_TRUE(projects.size() == 1u);
	EXPECT_EQ(projects[0].name, "Mon jeu");

	runtime.CloseProject();
	EXPECT_FALSE(runtime.HasProject());
	EXPECT_TRUE(runtime.ActiveScene() == nullptr);
}

TEST(ProjectFiles, SaveSplitsTheProjectIntoDataFilesAndLoadsItBack) {
	const String dir = Scratch("roundtrip");
	auto demo = files::LoadProject(String(DEMO_PROJECT));
	ASSERT_TRUE(demo.IsOk());
	const Project &original = demo.Value();

	const String manifest = files::Join(dir, String("demo.json"));
	auto written = files::SaveProject(original, manifest);
	ASSERT_TRUE(written.IsOk());
	// Une scène = un .scene ; un script = un .script ; le jeu d'une scène à part.
	EXPECT_TRUE(Exists(files::Join(dir, String("scenes/Donjon.scene"))));
	EXPECT_TRUE(Exists(files::Join(dir, String("scripts/torchlight.script"))));
	EXPECT_TRUE(Exists(files::Join(dir, String("scripts/Circuit.gameplay.script"))));
	auto script = files::ReadText(files::Join(dir, String("scripts/torchlight.script")));
	ASSERT_TRUE(script.IsOk());
	EXPECT_EQ(script.Value(), original.FindScript(String("torchlight"))->source); // du Script brut, pas du JSON

	files::FileList files;
	auto loaded = files::LoadProject(manifest, &files);
	ASSERT_TRUE(loaded.IsOk());
	EXPECT_EQ(loaded.Value().name, original.name);
	EXPECT_EQ(loaded.Value().activeScene, original.activeScene);
	ASSERT_TRUE(loaded.Value().scenes.size() == original.scenes.size());
	for (size_t i = 0; i < original.scenes.size(); ++i) {
		EXPECT_EQ(loaded.Value().scenes[i].name, original.scenes[i].name);
		EXPECT_EQ(loaded.Value().scenes[i].ObjectCount(), original.scenes[i].ObjectCount());
		EXPECT_EQ(loaded.Value().scenes[i].gameplayScript, original.scenes[i].gameplayScript);
	}
	EXPECT_EQ(loaded.Value().scripts.size(), original.scripts.size());
	EXPECT_EQ(files.size(), written.Value().size());
}

TEST(ProjectFiles, RenamedScenesLeaveNoOrphanFiles) {
	const String root = Scratch("orphans");
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	ASSERT_TRUE(runtime.CreateProject(root, String("P")).IsOk());
	const String dir = runtime.ProjectDirectory();
	const String before = files::Join(dir, String("scenes/Scène principale.scene"));
	EXPECT_TRUE(Exists(before));

	runtime.GetProject().scenes.front().SetName(String("Niveau 1"));
	runtime.GetProject().activeScene = String("Niveau 1");
	ASSERT_TRUE(runtime.SaveProject().IsOk());
	EXPECT_TRUE(Exists(files::Join(dir, String("scenes/Niveau 1.scene"))));
	EXPECT_FALSE(Exists(before)); // l'ancien fichier de la scène renommée a disparu

	// Un fichier que le projet n'a pas écrit n'est jamais effacé.
	const String foreign = files::Join(dir, String("scenes/prefab.scene"));
	ASSERT_TRUE(files::WriteText(foreign, String("{}")).IsOk());
	ASSERT_TRUE(runtime.SaveProject().IsOk());
	EXPECT_TRUE(Exists(foreign));
}

TEST(ProjectFiles, ScenesAndScriptsSaveAsSingleFiles) {
	const String dir = Scratch("save_as");
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	ASSERT_TRUE(runtime.LoadProjectFile(String(DEMO_PROJECT)).IsOk());
	ASSERT_TRUE(runtime.SwitchScene(String("Vitrine")));
	const size_t objects = runtime.ActiveScene()->ObjectCount();
	const String gameplay = runtime.ActiveScene()->gameplayScript;

	// Scène : UN fichier, script de jeu compris.
	const String scenePath = files::Join(dir, String("vitrine.scene"));
	ASSERT_TRUE(runtime.SaveSceneAs(scenePath).IsOk());
	EXPECT_EQ(files::FormatOf(scenePath), files::SCENE_FORMAT);
	auto scene = files::LoadSceneFile(scenePath);
	ASSERT_TRUE(scene.IsOk());
	EXPECT_EQ(scene.Value().ObjectCount(), objects);
	EXPECT_EQ(scene.Value().gameplayScript, gameplay);

	// Réimportée : ajoutée sous un nom libre, et activée.
	const size_t scenes = runtime.GetProject().scenes.size();
	auto imported = runtime.ImportSceneFile(scenePath);
	ASSERT_TRUE(imported.IsOk());
	EXPECT_EQ(imported.Value(), "Vitrine 2");
	EXPECT_EQ(runtime.GetProject().scenes.size(), scenes + 1);
	EXPECT_EQ(runtime.ActiveScene()->name, "Vitrine 2");
	// Un sous-arbre réutilisable (scene.packed) ne s'ouvre pas comme scène.
	const String packed = files::Join(dir, String("sous_arbre.scene"));
	ASSERT_TRUE(runtime.SavePackedScene(runtime.ActiveScene()->Objects().front(), packed).IsOk());
	EXPECT_TRUE(runtime.ImportSceneFile(packed).IsError());

	// Script : du Script brut, réimportable dans la bibliothèque.
	const String scriptPath = files::Join(dir, String("torche.script"));
	ASSERT_TRUE(runtime.SaveScriptAs(String("torchlight"), scriptPath).IsOk());
	auto importedScript = runtime.ImportScriptFile(scriptPath);
	ASSERT_TRUE(importedScript.IsOk());
	EXPECT_EQ(importedScript.Value(), "torche");
	ASSERT_TRUE(runtime.GetProject().FindScript(String("torche")) != nullptr);
	EXPECT_EQ(runtime.GetProject().FindScript(String("torche"))->source,
			  runtime.GetProject().FindScript(String("torchlight"))->source);
	// Le script de jeu d'une scène : clé `@Scène`.
	ASSERT_TRUE(runtime.SaveScriptAs(String("@Vitrine"), files::Join(dir, String("jeu.script"))).IsOk());
	EXPECT_EQ(files::ReadText(files::Join(dir, String("jeu.script"))).Value(), gameplay);
}

TEST(ProjectFiles, LegacyAllInOneManifestsStillOpen) {
	const String dir = Scratch("legacy");
	auto demo = files::LoadProject(String(DEMO_PROJECT));
	ASSERT_TRUE(demo.IsOk());
	// Ancien format : tout dans le .json (version 4).
	const String legacy = files::Join(dir, String("ancien.json"));
	ASSERT_TRUE(files::WriteText(legacy, demo.Value().EncodeJson()).IsOk());

	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	ASSERT_TRUE(runtime.LoadProjectFile(legacy).IsOk());
	EXPECT_EQ(runtime.GetProject().scenes.size(), demo.Value().scenes.size());
	// Enregistré : le voilà éclaté en fichiers.
	ASSERT_TRUE(runtime.SaveProject().IsOk());
	EXPECT_TRUE(Exists(files::Join(dir, String("scenes/Donjon.scene"))));
	auto manifest = files::ReadJson(legacy);
	ASSERT_TRUE(manifest.IsOk());
	EXPECT_EQ(json::Int(manifest.Value()->Get("version")), files::PROJECT_VERSION);
}

TEST(ProjectFiles, ManifestsResolveFromAFolderOrAFile) {
	const String root = Scratch("resolve");
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	ASSERT_TRUE(runtime.CreateProject(root, String("Démo")).IsOk());
	const String dir = files::Join(root, String("Démo"));
	EXPECT_EQ(files::ResolveManifest(dir), files::Join(dir, String("Démo.json")));
	EXPECT_EQ(files::ResolveManifest(dir + String("/")), files::Join(dir, String("Démo.json")));
	EXPECT_EQ(files::ResolveManifest(files::Join(dir, String("Démo.json"))), files::Join(dir, String("Démo.json")));
	EXPECT_TRUE(files::ResolveManifest(files::Join(root, String("absent"))).IsEmpty());
	// Noms de dossier : les séparateurs deviennent `_`, les accents restent.
	EXPECT_EQ(files::SafeFileName(String("a/b:c")), "a_b_c");
	EXPECT_EQ(files::SafeFileName(String("  Été  ")), "Été");
	EXPECT_EQ(files::SafeFileName(String("..")), "sans_nom");
}

int main() { return RUN_ALL_TESTS(); }
