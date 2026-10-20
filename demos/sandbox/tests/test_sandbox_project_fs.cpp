// Tests unitaires — le projet vu comme un SYSTÈME DE FICHIERS
// (examples/sandbox/document/project_fs.hpp) : chemins typés
// (/scenes, /objects, /scripts, /assets, nœuds), analyse et forme canonique,
// listings sans mélange scène/objet, existence, résolution, nœuds adressés
// DANS leur document, créations à nom unique, noms refusés, ressources sur
// disque ; et le Runtime qui ouvre, renomme et retire des documents TYPÉS.
//
// Écrit UNIQUEMENT sous build/tests/tmp/ (ignoré par git).
#include <sandbox/document/objects.hpp>
#include <sandbox/document/project_fs.hpp>
#include <sandbox/editor/asset_ops.hpp>
#include <sandbox/editor/assets.hpp>
#include <sandbox/engine/runtime.hpp>
#include "test.hpp"

#include <filesystem>
#include <fstream>

using namespace sandbox;

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
	project.documents.push_back(std::move(vitrine));
	project.documents.push_back(std::move(torche)); // mêlé aux scènes dans le stockage
	project.documents.push_back(std::move(donjon));
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

TEST(ProjectFs, RuntimeOpensRenamesAndRemovesTypedDocuments) {
	Project project = MakeProject();
	// Un OBJET homonyme d'une scène : deux dossiers, aucune confusion — même
	// une instance de l'objet « Vitrine » posée dans la scène « Vitrine ».
	SceneDesc homonyme;
	homonyme.kind = SceneKind::OBJECT;
	homonyme.SetName("Vitrine");
	(void)homonyme.tree.Create(homonyme.tree.Root(), String("Socle"));
	project.documents.push_back(std::move(homonyme));
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	runtime.OpenProject(std::move(project));

	ASSERT_TRUE(runtime.OpenScene(SceneRef{"Vitrine"}));
	EXPECT_EQ(runtime.ActivePath().ToString(), String("/scenes/Vitrine"));
	auto placed = runtime.InstantiateObject(ObjectRef{"Vitrine"});
	ASSERT_TRUE(placed.IsOk()); // pas un « cycle » : l'hôte est une scène
	EXPECT_TRUE(runtime.ActiveDocument()->tree.FindByName(String("Socle")).Valid());

	// L'objet ouvert ne remplace pas la scène active (celle qu'on enregistre).
	ASSERT_TRUE(runtime.OpenObject(ObjectRef{"Vitrine"}));
	EXPECT_TRUE(runtime.IsEditingObject());
	EXPECT_EQ(runtime.ActivePath().ToString(), String("/objects/Vitrine"));
	EXPECT_EQ(runtime.ActiveScene()->name, String("Vitrine"));
	EXPECT_TRUE(!runtime.ActiveScene()->IsObject());
	EXPECT_EQ(runtime.GetProject().activeScene, String("Vitrine"));
	EXPECT_TRUE(runtime.InstantiateObject(ObjectRef{"Vitrine"}).IsError()); // lui-même : refusé

	// Un chemin de nœud ouvre son document et sélectionne le nœud.
	ASSERT_TRUE(runtime.OpenDocument(ProjectPath::SceneNode("Vitrine", "Lumières/Soleil")));
	EXPECT_EQ(runtime.ActivePath().ToString(), String("/scenes/Vitrine"));
	EXPECT_TRUE(runtime.SelectedName().Unwrap() == String("Soleil"));
	EXPECT_FALSE(runtime.OpenDocument(ProjectPath::Scene("Absente")));
	EXPECT_FALSE(runtime.OpenScene(SceneRef{"Torche"})); // un objet n'est pas une scène

	// Renommer l'objet ouvert : l'édition suit, les instances aussi.
	ASSERT_TRUE(runtime.OpenObject(ObjectRef{"Torche"}));
	auto renamed = runtime.RenameDocument(ProjectPath::Object("Torche"), String("Flambeau"));
	ASSERT_TRUE(renamed.IsOk());
	EXPECT_EQ(runtime.ActivePath().ToString(), String("/objects/Flambeau"));
	EXPECT_TRUE(runtime.RenameDocument(ProjectPath::Object("Flambeau"), String("a/b")).IsError());

	// Retirer l'objet ouvert : retour à la scène active.
	ASSERT_TRUE(runtime.RemoveDocument(ProjectPath::Object("Flambeau")).IsOk());
	EXPECT_EQ(runtime.ActivePath().ToString(), String("/scenes/Vitrine"));
	EXPECT_TRUE(runtime.GetProject().Fs().ListObjects().size() == 1);
	// Une scène ne se retire pas si c'est la dernière ; ici il en reste une autre.
	ASSERT_TRUE(runtime.RemoveDocument(ProjectPath::Scene("Vitrine")).IsOk());
	EXPECT_EQ(runtime.ActivePath().ToString(), String("/scenes/Donjon"));
	EXPECT_TRUE(runtime.RemoveDocument(ProjectPath::Scene("Donjon")).IsError());
}

TEST(ProjectFs, ToolScriptsBrowseTheProjectByPath) {
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	runtime.OpenProject(MakeProject());
	auto run = [&](const char* source) {
		auto result = runtime.RunToolScript(String(source));
		if (result.IsError())
			test::ReportFailure(__FILE__, __LINE__, (String(source) + String(" → ") + result.Error().Format()).CStr());
		return result;
	};
	for (const char* check : {"assert(len(editor.scenes()) == 2)", "assert(len(editor.objects()) == 1)",
							  "assert(len(editor.list(\"/\")) == 4)",
							  "assert(editor.list(\"/scenes/Vitrine/Lumières\")[0] == \"Soleil\")",
							  "assert(editor.kind(\"/objects/Torche\") == \"Objet\")",
							  "assert(editor.kind(\"Torche\") == \"Objet\")",
							  "assert(editor.kind(\"/objects/Absent\") == nil)", "assert(editor.open_document(\"/objects/Torche\"))"})
		EXPECT_TRUE(run(check).IsOk());
	EXPECT_EQ(runtime.ActivePath().ToString(), String("/objects/Torche"));
	ASSERT_TRUE(run("assert(editor.open_document(\"/scenes/Vitrine/Tore\"))").IsOk());
	EXPECT_EQ(runtime.ActivePath().ToString(), String("/scenes/Vitrine"));
	EXPECT_TRUE(runtime.SelectedName().Unwrap() == String("Tore"));
	ASSERT_TRUE(run("assert(not editor.open_document(\"/scenes/Absente\"))").IsOk());
}

TEST(ProjectFs, ObjectNamespaceIsADeprecatedAliasOfNode) {
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	runtime.OpenProject(MakeProject());
	ASSERT_TRUE(runtime.OpenScene(SceneRef{"Vitrine"}));
	auto warnings = [&] {
		int n = 0;
		for (const LogEntry& entry : runtime.Log())
			n += entry.level == LogLevel::WARNING && entry.text.Contains("object.set_position") ? 1 : 0;
		return n;
	};
	ASSERT_TRUE(runtime.RunToolScript(String("node.set_position(\"Tore\", 1, 2, 3)")).IsOk());
	EXPECT_EQ(warnings(), 0);
	// L'ancien nom marche encore, et avertit UNE fois.
	ASSERT_TRUE(runtime.RunToolScript(String("object.set_position(\"Tore\", 4, 5, 6)\n"
											  "object.set_position(\"Tore\", 4, 5, 6)\n"
											  "assert(node.position(\"Tore\")[1] == 5)"))
					.IsOk());
	EXPECT_EQ(warnings(), 1);
}

TEST(ProjectFs, ExplorerBrowsesTheProjectWithTypedEntries) {
	Project project = MakeProject();
	AssetBrowserModel model(&project, String("build/tests/tmp/project_fs/inexistant"));
	// Un chemin disque absolu n'est pas un chemin de projet.
	EXPECT_TRUE(AssetBrowserModel::ProjectLocation(String("/scenes/Vitrine")).IsSome());
	EXPECT_TRUE(AssetBrowserModel::ProjectLocation(String("/home/clement/x")).IsNone());
	EXPECT_TRUE(AssetBrowserModel::ProjectLocation(String("assets/models")).IsNone());

	// Chaque entrée dit ce qu'elle désigne.
	const std::vector<AssetEntry> scenes = model.List(String(AssetBrowserModel::SCENES));
	ASSERT_EQ(scenes.size(), size_t(2));
	for (const AssetEntry& entry : scenes)
		EXPECT_TRUE(entry.path.kind == ProjectPath::Kind::SCENE && entry.managed);
	const std::vector<AssetEntry> objectsList = model.List(String(AssetBrowserModel::OBJECTS));
	ASSERT_EQ(objectsList.size(), size_t(1));
	EXPECT_TRUE(objectsList[0].path == ProjectPath::Object("Torche"));
	EXPECT_TRUE(objectsList[0].browsable);
	const std::vector<AssetEntry> scripts = model.List(String(AssetBrowserModel::SCRIPTS));
	ASSERT_EQ(scripts.size(), size_t(2));
	bool gameplay = false;
	for (const AssetEntry& entry : scripts)
		gameplay = gameplay || entry.path == ProjectPath::GameplayScript("Donjon");
	EXPECT_TRUE(gameplay);

	// Une scène se parcourt comme un dossier : ses nœuds, typés.
	ASSERT_TRUE(model.Navigate(String("/scenes/Vitrine")));
	std::vector<AssetEntry> nodes = model.Entries();
	ASSERT_EQ(nodes.size(), size_t(2));
	EXPECT_TRUE(nodes[0].kind == AssetKind::NODE);
	EXPECT_EQ(nodes[0].location, String("/scenes/Vitrine/Lumières"));
	EXPECT_TRUE(nodes[0].path == ProjectPath::SceneNode("Vitrine", "Lumières"));
	EXPECT_TRUE(nodes[0].browsable); // Lumières a un enfant
	EXPECT_TRUE(!nodes[1].browsable); // Tore n'en a pas
	ASSERT_TRUE(model.Navigate(nodes[0].location));
	ASSERT_EQ(model.Entries().size(), size_t(1));
	EXPECT_TRUE(model.Entries()[0].path == ProjectPath::SceneNode("Vitrine", "Lumières/Soleil"));
	// Fil d'Ariane et remontée suivent les niveaux typés.
	const auto crumbs = model.Breadcrumb();
	ASSERT_EQ(crumbs.size(), size_t(4));
	EXPECT_EQ(crumbs[1].first, String("Scènes"));
	EXPECT_EQ(crumbs[2].first, String("Vitrine"));
	EXPECT_EQ(crumbs[3].first, String("Lumières"));
	ASSERT_TRUE(model.Up());
	EXPECT_EQ(model.Location(), String("/scenes/Vitrine"));
	ASSERT_TRUE(model.Up());
	EXPECT_EQ(model.Location(), String("/scenes"));
	// Un script ne se parcourt pas ; un document absent non plus.
	EXPECT_TRUE(!model.IsFolder(String("/scripts/torchlight")));
	EXPECT_TRUE(!model.IsFolder(String("/scenes/Absente")));

	// Un nœud ne se renomme ni ne se supprime depuis l'explorateur (l'arbre
	// de son document le fait) ; une scène, si.
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	runtime.OpenProject(MakeProject());
	AssetBrowserModel live(&runtime.GetProject(), String("build/tests/tmp/project_fs/inexistant"));
	AssetOperations ops(runtime, live);
	EXPECT_TRUE(ops.WhyLocked(nodes[1], "rename").IsSome());
	EXPECT_TRUE(ops.WhyLocked(scenes[0], "rename").IsNone());
	// Dupliquer un OBJET (n'aboutissait pas avant : passé à DuplicateScene).
	const AssetOpReport copy = ops.Duplicate({live.List(String(AssetBrowserModel::OBJECTS))[0]});
	EXPECT_EQ(copy.done, 1);
	ASSERT_EQ(copy.created.size(), size_t(1));
	EXPECT_EQ(copy.created[0], String("/objects/Torche (copie)"));
	EXPECT_EQ(runtime.GetProject().Fs().ListObjects().size(), size_t(2));
}
