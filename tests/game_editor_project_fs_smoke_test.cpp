// Tests unitaires — le projet vu comme un SYSTÈME DE FICHIERS
// (examples/game_editor_demo/document/project_fs.hpp) : chemins typés
// (/scenes, /objects, /scripts, /assets, nœuds), analyse et forme canonique,
// listings sans mélange scène/objet, existence, résolution, nœuds adressés
// DANS leur document, créations à nom unique, noms refusés, ressources sur
// disque.
//
// Écrit UNIQUEMENT sous build/tests/tmp/ (ignoré par git).
#define USE_TEST

#include "../examples/game_editor_demo/document/objects.hpp"
#include "../examples/game_editor_demo/document/project_fs.hpp"
#include "core/test.hpp"

#include <filesystem>
#include <fstream>

using namespace game_editor;

namespace {

String Scratch(const char* name) {
	const std::string path = std::string("build/tests/tmp/project_fs/") + name;
	std::error_code ignored;
	std::filesystem::remove_all(path, ignored);
	std::filesystem::create_directories(path, ignored);
	return String(path.c_str());
}

/// Projet : scène « Vitrine » (Lumières/Soleil, Tore), scène « Donjon »
/// (script de jeu), objet « Torche » (Flammes), objet homonyme d'aucune
/// scène, script de bibliothèque « torchlight ».
Project MakeProject() {
	Project project;
	project.name = "Essai";
	SceneDesc vitrine;
	vitrine.SetName("Vitrine");
	const scene::NodeId lights = vitrine.tree.Create(vitrine.tree.Root(), String("Lumières"));
	(void)vitrine.tree.Create(lights, String("Soleil"));
	(void)vitrine.tree.Create(vitrine.tree.Root(), String("Tore"));
	SceneDesc donjon;
	donjon.SetName("Donjon");
	donjon.gameplayScript = "class Donjon extends Scene {}";
	SceneDesc torche;
	torche.kind = SceneKind::OBJECT;
	torche.SetName("Torche");
	(void)torche.tree.Create(torche.tree.Root(), String("Flammes"));
	project.scenes.push_back(std::move(vitrine));
	project.scenes.push_back(std::move(torche)); // mêlé aux scènes dans le stockage
	project.scenes.push_back(std::move(donjon));
	project.scripts.push_back(ScriptAsset{String("torchlight"), String("Flamme"), String("class T extends Behaviour {}")});
	project.activeScene = "Vitrine";
	return project;
}

} // namespace

TEST(ProjectFs, PathsParseAndPrintCanonically) {
	using K = ProjectPath::Kind;
	struct Case {
		const char* text;
		K kind;
		const char* canonical;
	};
	const Case cases[] = {
		{"/", K::ROOT, "/"},
		{"scenes", K::SCENES_FOLDER, "/scenes"},
		{"/objects/", K::OBJECTS_FOLDER, "/objects"},
		{"/scenes/Vitrine", K::SCENE, "/scenes/Vitrine"},
		{"/scenes/Vitrine/Lumières/Soleil", K::SCENE_NODE, "/scenes/Vitrine/Lumières/Soleil"},
		{"/objects/Torche/Flammes", K::OBJECT_NODE, "/objects/Torche/Flammes"},
		{"/scripts/torchlight", K::SCRIPT, "/scripts/torchlight"},
		{"/scripts/@Donjon", K::GAMEPLAY_SCRIPT, "/scripts/@Donjon"},
		{"/assets/models/knight.gltf", K::ASSET, "/assets/models/knight.gltf"},
	};
	for (const Case& c : cases) {
		auto parsed = ProjectPath::Parse(StringView(c.text));
		ASSERT_TRUE(parsed.IsSome());
		EXPECT_TRUE(parsed.Unwrap().kind == c.kind);
		EXPECT_EQ(parsed.Unwrap().ToString(), String(c.canonical));
		// Aller-retour : la forme canonique se relit à l'identique.
		EXPECT_TRUE(ProjectPath::Parse(parsed.Unwrap().ToString().View()).Unwrap() == parsed.Unwrap());
	}
	EXPECT_TRUE(ProjectPath::Parse(StringView("/inconnu/x")).IsNone());
	EXPECT_TRUE(ProjectPath::Parse(StringView("/scripts/a/b")).IsNone());
	EXPECT_TRUE(ProjectPath::Parse(StringView("/scenes/.cache")).IsNone());

	// Prédicats et navigation.
	const ProjectPath node = ProjectPath::SceneNode("Vitrine", "Lumières/Soleil");
	EXPECT_TRUE(node.IsNode() && node.IsScene() && !node.IsObject() && !node.IsDocument());
	EXPECT_TRUE(node.Document() == ProjectPath::Scene("Vitrine"));
	EXPECT_TRUE(node.Parent() == ProjectPath::SceneNode("Vitrine", "Lumières"));
	EXPECT_TRUE(node.Parent().Parent() == ProjectPath::Scene("Vitrine"));
	EXPECT_TRUE(ProjectPath::Scene("Vitrine").Parent() == ProjectPath::ScenesFolder());
	EXPECT_TRUE(ProjectPath::Asset("models/k.gltf").Parent() == ProjectPath::Asset("models"));
	EXPECT_TRUE(ProjectPath::Asset("k.gltf").Parent() == ProjectPath::AssetsFolder());
	EXPECT_TRUE(ProjectPath::ScriptsFolder().Parent() == ProjectPath::Root());
	EXPECT_TRUE(!ProjectPath::Root().Parent().IsValid());
	EXPECT_TRUE(ScriptFileRef{"@Donjon"}.Path() == ProjectPath::GameplayScript("Donjon"));
	EXPECT_EQ(ScriptFileRef{"@Donjon"}.HostScene(), String("Donjon"));
	EXPECT_EQ(AssetRef{"models/k.gltf"}.Name(), String("k.gltf"));
	EXPECT_EQ(AssetRef{"models/k.gltf"}.ParentDir(), String("models"));
}

TEST(ProjectFs, ListingsNeverMixScenesAndObjects) {
	Project project = MakeProject();
	const ProjectFs fs = project.Fs();
	const std::vector<String> scenes = fs.ListScenes();
	const std::vector<String> objectsList = fs.ListObjects();
	ASSERT_EQ(scenes.size(), size_t(2));
	EXPECT_EQ(scenes[0], String("Vitrine"));
	EXPECT_EQ(scenes[1], String("Donjon"));
	ASSERT_EQ(objectsList.size(), size_t(1));
	EXPECT_EQ(objectsList[0], String("Torche"));
	EXPECT_EQ(fs.List(ProjectPath::Root()).size(), size_t(4));
	const std::vector<String> scripts = fs.List(ProjectPath::ScriptsFolder());
	ASSERT_EQ(scripts.size(), size_t(2));
	EXPECT_EQ(scripts[0], String("torchlight"));
	EXPECT_EQ(scripts[1], String("@Donjon")); // seule scène avec un script de jeu
	const std::vector<String> vitrine = fs.List(ProjectPath::Scene("Vitrine"));
	ASSERT_EQ(vitrine.size(), size_t(2));
	EXPECT_EQ(vitrine[0], String("Lumières"));
	EXPECT_EQ(fs.List(ProjectPath::SceneNode("Vitrine", "Lumières")).size(), size_t(1));
	EXPECT_EQ(fs.List(ProjectPath::Object("Torche"))[0], String("Flammes"));

	// Les types forts ne confondent jamais scène et objet.
	EXPECT_TRUE(fs.FindScene(SceneRef{"Vitrine"}) != nullptr);
	EXPECT_TRUE(fs.FindScene(SceneRef{"Torche"}) == nullptr);
	EXPECT_TRUE(fs.FindObject(ObjectRef{"Torche"}) != nullptr);
	EXPECT_TRUE(fs.FindObject(ObjectRef{"Vitrine"}) == nullptr);
	EXPECT_TRUE(fs.FindScript(ScriptFileRef{"torchlight"}) != nullptr);
	EXPECT_TRUE(fs.FindScript(ScriptFileRef{"@Donjon"}) == nullptr); // vit dans la scène
	EXPECT_EQ(fs.GameplayScriptSource(StringView("Donjon")), String("class Donjon extends Scene {}"));
}

TEST(ProjectFs, ExistenceAndResolution) {
	Project project = MakeProject();
	const ProjectFs fs = project.Fs();
	EXPECT_TRUE(fs.Exists(ProjectPath::Scene("Vitrine")));
	EXPECT_TRUE(!fs.Exists(ProjectPath::Scene("Torche")));
	EXPECT_TRUE(fs.Exists(ProjectPath::Object("Torche")));
	EXPECT_TRUE(fs.Exists(ProjectPath::SceneNode("Vitrine", "Lumières/Soleil")));
	EXPECT_TRUE(fs.Exists(ProjectPath::GameplayScript("Donjon")));
	EXPECT_TRUE(!fs.Exists(ProjectPath::GameplayScript("Vitrine"))); // script de jeu vide
	// Un chemin reste un chemin : « Soleil » n'est pas un enfant direct de la racine.
	EXPECT_TRUE(!fs.Exists(ProjectPath::SceneNode("Vitrine", "Soleil")));

	// Nom nu : scène, puis objet, puis script.
	EXPECT_TRUE(fs.Resolve(StringView("Vitrine")).Unwrap() == ProjectPath::Scene("Vitrine"));
	EXPECT_TRUE(fs.Resolve(StringView("Torche")).Unwrap() == ProjectPath::Object("Torche"));
	EXPECT_TRUE(fs.Resolve(StringView("torchlight")).Unwrap() == ProjectPath::Script("torchlight"));
	EXPECT_TRUE(fs.Resolve(StringView("Personne")).IsNone());
	EXPECT_TRUE(fs.Resolve(StringView("/scenes/Absente")).IsSome());
	EXPECT_TRUE(fs.Resolve(StringView("/scenes/Absente"), true).IsNone());
}

TEST(ProjectFs, NodesAreAddressedInsideTheirDocument) {
	Project project = MakeProject();
	const ProjectFs fs = project.Fs();
	const NodeRef soleil = fs.FindNode(ProjectPath::Scene("Vitrine"), StringView("Lumières/Soleil"));
	ASSERT_TRUE(soleil.IsValid());
	EXPECT_TRUE(soleil.IsInScene());
	EXPECT_EQ(soleil.path, String("Lumières/Soleil")); // relatif, sans la racine
	EXPECT_EQ(soleil.FullPath().ToString(), String("/scenes/Vitrine/Lumières/Soleil"));
	EXPECT_EQ(fs.NodePathOf(ProjectPath::Scene("Vitrine"), soleil.id), String("Lumières/Soleil"));
	// Le chemin d'un nœud désigne aussi son document.
	EXPECT_TRUE(fs.FindNode(ProjectPath::SceneNode("Vitrine", "Tore"), StringView("Lumières")).IsValid());

	const NodeRef flammes = fs.FindNode(ProjectPath::Object("Torche"), StringView("Flammes"));
	ASSERT_TRUE(flammes.IsValid());
	EXPECT_TRUE(flammes.IsInObject());
	EXPECT_EQ(flammes.FullPath().ToString(), String("/objects/Torche/Flammes"));
	// Même identifiant, autre document : jamais confondus.
	EXPECT_TRUE(!(fs.NodeRefOf(ProjectPath::Scene("Vitrine"), flammes.id) == flammes));

	const NodeRef root = fs.FindNode(ProjectPath::Scene("Vitrine"), StringView(""));
	EXPECT_TRUE(root.IsValid() && root.path.IsEmpty());
	EXPECT_TRUE(!fs.FindNode(ProjectPath::Script("torchlight"), StringView("x")).IsValid());
}

TEST(ProjectFs, CreationsGetUniqueNamesAndBadNamesAreRefused) {
	Project project = MakeProject();
	ProjectFs fs = project.Fs();
	EXPECT_EQ(fs.CreateScene("Vitrine").name, String("Vitrine 2"));
	// Un objet peut porter le nom d'une scène : ce sont deux dossiers distincts.
	EXPECT_EQ(fs.CreateObject("Vitrine").name, String("Vitrine"));
	EXPECT_EQ(fs.CreateObject("Torche").name, String("Torche 2"));
	EXPECT_EQ(fs.CreateScript("torchlight", "class X extends Behaviour {}").name, String("torchlight_2"));
	EXPECT_TRUE(fs.ObjectExists(StringView("Vitrine")) && fs.SceneExists(StringView("Vitrine")));
	EXPECT_EQ(fs.ListScenes().size(), size_t(3));
	EXPECT_EQ(fs.ListObjects().size(), size_t(3));

	EXPECT_TRUE(ProjectFs::WhyInvalidName(StringView("Salle 1")).IsNone());
	EXPECT_TRUE(ProjectFs::WhyInvalidName(StringView("  ")).IsSome());
	EXPECT_TRUE(ProjectFs::WhyInvalidName(StringView("..")).IsSome());
	EXPECT_TRUE(ProjectFs::WhyInvalidName(StringView(".cache")).IsSome());
	EXPECT_TRUE(ProjectFs::WhyInvalidName(StringView("a/b")).IsSome());

	// Un objet ne s'instancie pas dans lui-même ni dans ce qui le contient.
	SceneDesc* torche = fs.FindObject(ObjectRef{"Torche"});
	ASSERT_TRUE(torche != nullptr);
	EXPECT_TRUE(fs.WhySelfInstance(ObjectRef{"Torche"}, *torche).IsSome());
	EXPECT_TRUE(fs.WhySelfInstance(ObjectRef{"Torche"}, *fs.FindScene(SceneRef{"Vitrine"})).IsNone());
	EXPECT_TRUE(fs.WhySelfInstance(ObjectRef{"Absent"}, *torche).IsSome());
}

TEST(ProjectFs, AssetsMapToTheDisk) {
	const String root = Scratch("assets");
	std::filesystem::create_directories(std::string(root.CStr()) + "/models");
	std::ofstream(std::string(root.CStr()) + "/models/k.gltf") << "{}";
	std::ofstream(std::string(root.CStr()) + "/notes.txt") << "x";
	Project project = MakeProject();
	ProjectFs fs = project.Fs();
	EXPECT_TRUE(!fs.Exists(ProjectPath::AssetsFolder())); // pas de racine : pas de /assets
	fs.SetAssetsRoot(root + "/");
	EXPECT_TRUE(fs.Exists(ProjectPath::AssetsFolder()));
	const std::vector<String> top = fs.List(ProjectPath::AssetsFolder());
	ASSERT_EQ(top.size(), size_t(2));
	EXPECT_EQ(top[0], String("models"));
	EXPECT_EQ(fs.List(ProjectPath::Asset("models"))[0], String("k.gltf"));
	EXPECT_TRUE(fs.Exists(ProjectPath::Asset("models/k.gltf")));
	EXPECT_TRUE(fs.Exists(ProjectPath::Asset("models"))); // un sous-dossier existe aussi
	EXPECT_TRUE(!fs.Exists(ProjectPath::Asset("models/absent.gltf")));
	const String disk = fs.AssetDiskPath(AssetRef{"models/k.gltf"});
	EXPECT_EQ(disk, root + "/models/k.gltf");
	EXPECT_EQ(fs.AssetDisplayPath(disk.View()), String("models/k.gltf"));
	EXPECT_TRUE(fs.AssetDisplayPath(StringView("/ailleurs/k.gltf")).IsEmpty());
}

int main() {
	return RUN_ALL_TESTS();
}
