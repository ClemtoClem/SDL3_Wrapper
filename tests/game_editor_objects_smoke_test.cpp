// Tests unitaires — OBJETS réutilisables de l'éditeur de jeux
// (examples/game_editor_demo/document/objects.hpp, Runtime::*Object*) :
// instances qui ne sont que des références, contenu régénéré, objets
// imbriqués, cycles refusés, fichiers `.object`, propagation des
// modifications aux scènes, sélection, conversion d'un nœud en objet,
// détachement, et base de script `ObjectAsset`.
//
// Écrit UNIQUEMENT sous build/tests/tmp/ (ignoré par git).
#define USE_TEST

#include "../examples/game_editor_demo/document/objects.hpp"
#include "../examples/game_editor_demo/document/project_files.hpp"
#include "../examples/game_editor_demo/engine/runtime.hpp"
#include "core/test.hpp"

#include <filesystem>

using namespace game_editor;

namespace {

String Scratch(const char* name) {
	const std::string path = std::string("build/tests/tmp/objects/") + name;
	std::error_code ignored;
	std::filesystem::remove_all(path, ignored);
	std::filesystem::create_directories(path, ignored);
	return String(path.c_str());
}

SceneDesc MakeObject(const char* name) {
	SceneDesc object;
	object.kind = SceneKind::OBJECT;
	object.SetName(String(name));
	return object;
}

/// Ajoute un nœud enfant (boîte) sous `parent` (nom), rend son identifiant.
scene::NodeId AddBox(SceneDesc& document, const char* name, const char* parent = "") {
	NodeDesc box; // boîte par défaut
	box.name = String(name);
	box.parent = String(parent);
	return document.AddNode(std::move(box));
}

scene::NodeId AddInstance(SceneDesc& document, const char* name, const char* objectName) {
	scene::NodeId id = document.tree.Create(document.tree.Root(), String(name));
	objects::MakeInstance(*document.tree.Get(id), String(objectName));
	return id;
}

size_t GeneratedCount(const scene::NodeTree& tree) {
	size_t n = 0;
	tree.Traverse(tree.Root(),
				  [&](scene::NodeId, const scene::Node& node) { n += objects::IsGenerated(node); });
	return n;
}

/// Projet : Roue (1 boîte), Voiture (Caisse + 2 instances de Roue),
/// scène « Garage » avec 2 voitures.
Project MakeGarage() {
	Project project;
	project.name = "Garage";
	SceneDesc roue = MakeObject("Roue");
	AddBox(roue, "Jante");
	SceneDesc voiture = MakeObject("Voiture");
	AddBox(voiture, "Caisse");
	AddInstance(voiture, "Roue avant", "Roue");
	AddInstance(voiture, "Roue arrière", "Roue");
	SceneDesc garage;
	garage.SetName("Garage");
	AddInstance(garage, "Voiture rouge", "Voiture");
	AddInstance(garage, "Voiture bleue", "Voiture");
	// Volontairement : la scène avant les objets dont elle dépend.
	project.scenes.push_back(std::move(garage));
	project.scenes.push_back(std::move(voiture));
	project.scenes.push_back(std::move(roue));
	project.activeScene = "Garage";
	return project;
}

} // namespace

// ── Document ─────────────────────────────────────────────────────────────────

TEST(Objects, NestedInstancesAreGeneratedInDependencyOrder) {
	Project project = MakeGarage();
	objects::ExpandReport report = objects::ExpandProject(project);
	EXPECT_TRUE(report.errors.empty());
	const SceneDesc& garage = *project.FindScene("Garage");
	// Par voiture : Caisse, 2 roues (instances générées), 2 jantes = 5 nœuds.
	EXPECT_EQ(GeneratedCount(garage.tree), size_t(10));
	EXPECT_EQ(project.SceneCount(), size_t(1));
	EXPECT_EQ(project.ObjectNames().size(), size_t(2));
	EXPECT_EQ(project.SceneNames().size(), size_t(1));

	// Ré-développer ne duplique rien.
	(void)objects::ExpandProject(project);
	EXPECT_EQ(GeneratedCount(project.FindScene("Garage")->tree), size_t(10));

	// Ce qui s'enregistre : les deux références, sans leur contenu.
	scene::NodeTree saved = objects::StripGenerated(project.FindScene("Garage")->tree);
	EXPECT_EQ(saved.Size(), size_t(3)); // racine + 2 instances
	EXPECT_EQ(GeneratedCount(saved), size_t(0));

	// Une modification de l'objet profond se propage jusqu'à la scène.
	AddBox(*project.Fs().FindObject(ObjectRef{"Roue"}), "Pneu");
	(void)objects::ExpandProject(project);
	EXPECT_EQ(GeneratedCount(project.FindScene("Garage")->tree), size_t(14));
}

TEST(Objects, CyclesAndMissingObjectsAreReported) {
	Project project = MakeGarage();
	AddInstance(*project.Fs().FindObject(ObjectRef{"Roue"}), "Mise en abyme", "Voiture"); // Roue → Voiture → Roue
	AddInstance(*project.FindScene("Garage"), "Fantôme", "Inexistant");
	objects::ExpandReport report = objects::ExpandProject(project);
	EXPECT_TRUE(report.errors.size() >= 2);
	bool cycle = false, missing = false;
	for (const String& e : report.errors) {
		cycle = cycle || e.Contains("cycle");
		missing = missing || e.Contains("introuvable");
	}
	EXPECT_TRUE(cycle);
	EXPECT_TRUE(missing);
	// Le développement termine et reste borné malgré le cycle.
	EXPECT_TRUE(GeneratedCount(project.FindScene("Garage")->tree) < 100);
	EXPECT_TRUE(objects::DependsOn(project, project.Fs().FindObject(ObjectRef{"Voiture"})->tree, "Voiture"));
}

TEST(Objects, RenameReferencesAndEditableAncestor) {
	Project project = MakeGarage();
	(void)objects::ExpandProject(project);
	EXPECT_EQ(objects::RenameReferences(project, "Roue", "Pneumatique"), 2);
	project.Fs().FindObject(ObjectRef{"Roue"})->SetName("Pneumatique");
	EXPECT_TRUE(objects::ExpandProject(project).errors.empty());

	const SceneDesc& garage = *project.FindScene("Garage");
	scene::NodeId rouge = garage.FindId("Voiture rouge");
	scene::NodeId deep;
	garage.tree.Traverse(rouge, [&](scene::NodeId id, const scene::Node& node) {
		if (node.name == "Jante" && !deep.Valid())
			deep = id;
	});
	ASSERT_TRUE(deep.Valid());
	EXPECT_TRUE(objects::EditableAncestor(garage.tree, deep) == rouge);
	EXPECT_TRUE(objects::EditableAncestor(garage.tree, rouge) == rouge);
}

// ── Fichiers ─────────────────────────────────────────────────────────────────

TEST(Objects, ObjectFilesRoundTrip) {
	const String dir = Scratch("files");
	const String manifest = files::Join(dir, String("Garage.json"));
	Project project = MakeGarage();
	(void)objects::ExpandProject(project);
	ASSERT_TRUE(files::SaveProject(project, manifest).IsOk());
	EXPECT_TRUE(files::IsFile(files::Join(dir, String("objects/Roue.object"))));
	EXPECT_TRUE(files::IsFile(files::Join(dir, String("objects/Voiture.object"))));
	EXPECT_TRUE(files::IsFile(files::Join(dir, String("scenes/Garage.scene"))));
	EXPECT_EQ(files::FormatOf(files::Join(dir, String("objects/Roue.object"))),
			  files::OBJECT_FORMAT);
	// Le fichier de scène ne contient que les références.
	auto sceneText = files::ReadText(files::Join(dir, String("scenes/Garage.scene")));
	ASSERT_TRUE(sceneText.IsOk());
	EXPECT_TRUE(sceneText.Value().Contains("ObjectInstance"));
	EXPECT_FALSE(sceneText.Value().Contains("Jante"));
	EXPECT_FALSE(sceneText.Value().Contains(objects::GENERATED_PROPERTY));

	auto loaded = files::LoadProject(manifest);
	ASSERT_TRUE(loaded.IsOk());
	EXPECT_EQ(loaded.Value().ObjectNames().size(), size_t(2));
	EXPECT_TRUE(loaded.Value().Fs().FindObject(ObjectRef{"Voiture"}) != nullptr);
	EXPECT_EQ(GeneratedCount(loaded.Value().FindScene("Garage")->tree),
			  size_t(10)); // développé au chargement
	EXPECT_EQ(loaded.Value().activeScene, "Garage");
	std::filesystem::remove_all(dir.CStr());
}

// ── Runtime ──────────────────────────────────────────────────────────────────

TEST(Objects, EditingAnObjectUpdatesItsInstancesInScenes) {
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	runtime.OpenProject(MakeGarage());
	ASSERT_TRUE(runtime.SwitchScene("Garage"));
	EXPECT_FALSE(runtime.IsEditingObject());

	// Ouvrir l'objet, le modifier, revenir à la scène : les deux voitures
	// suivent, sans rien dupliquer.
	ASSERT_TRUE(runtime.SwitchScene("Voiture"));
	EXPECT_TRUE(runtime.IsEditingObject());
	AddBox(*runtime.ActiveScene(), "Aileron");
	ASSERT_TRUE(runtime.SwitchScene("Garage"));
	size_t ailerons = 0;
	runtime.ActiveScene()->tree.Traverse(
		runtime.ActiveScene()->tree.Root(),
		[&](scene::NodeId, const scene::Node& node) { ailerons += node.name == "Aileron"; });
	EXPECT_EQ(ailerons, size_t(2));

	// Sélectionner un nœud généré sélectionne l'instance qui le porte.
	scene::NodeId rouge = runtime.ResolveId("Voiture rouge");
	scene::NodeId caisse;
	runtime.ActiveScene()->tree.Traverse(rouge, [&](scene::NodeId id, const scene::Node& node) {
		if (node.name == "Caisse")
			caisse = id;
	});
	ASSERT_TRUE(caisse.Valid());
	EXPECT_TRUE(runtime.Select(caisse));
	EXPECT_TRUE(runtime.SelectedId() == rouge);

	// Instancier par l'API (comme le fera l'interface ou un script).
	scene::Transform at;
	at.position = math::FVector3{5.f, 0.f, 2.f};
	at.scale = math::FVector3{2.f, 2.f, 2.f};
	auto created = runtime.InstantiateObject("Voiture", scene::NodeId{}, Some(at));
	ASSERT_TRUE(created.IsOk());
	const scene::Node* third = runtime.ActiveScene()->tree.Get(created.Value());
	EXPECT_EQ(objects::SourceOf(*third), "Voiture");
	EXPECT_TRUE(third->transform.scale == at.scale);
	EXPECT_EQ(runtime.ActiveScene()->tree.ChildrenOf(created.Value()).size(),
			  size_t(4)); // Caisse, 2 roues, Aileron

	// Un objet ne peut pas se contenir lui-même (directement ou non).
	ASSERT_TRUE(runtime.SwitchScene("Roue"));
	EXPECT_TRUE(runtime.InstantiateObject("Voiture").IsError());
	EXPECT_TRUE(runtime.InstantiateObject("Roue").IsError());

	// Renommer un objet : les instances suivent.
	auto renamed = runtime.RenameScene("Roue", "Pneu");
	ASSERT_TRUE(renamed.IsOk());
	ASSERT_TRUE(runtime.SwitchScene("Garage"));
	size_t jantes = 0;
	runtime.ActiveScene()->tree.Traverse(
		runtime.ActiveScene()->tree.Root(),
		[&](scene::NodeId, const scene::Node& node) { jantes += node.name == "Jante"; });
	EXPECT_EQ(jantes, size_t(6)); // 3 voitures × 2 roues
}

TEST(Objects, NodeToObjectAndDetach) {
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	Project project;
	SceneDesc scene;
	scene.SetName("Salle");
	AddBox(scene, "Table");
	AddBox(scene, "Plateau", "Table");
	AddBox(scene, "Pied", "Table");
	scene.tree.Get(scene.FindId("Table"))->transform.position = math::FVector3{3.f, 0.f, 1.f};
	project.scenes.push_back(std::move(scene));
	project.activeScene = "Salle";
	runtime.OpenProject(std::move(project));

	auto object = runtime.CreateObjectFromNode(runtime.ResolveId("Table"));
	ASSERT_TRUE(object.IsOk());
	EXPECT_EQ(object.Value(), "Table"); // le nom du nœud (aucun document ne le porte)
	const SceneDesc* table = runtime.GetProject().Fs().FindObject(ObjectRef{object.Value()});
	ASSERT_TRUE(table != nullptr);
	EXPECT_EQ(table->NodeCount(), size_t(3));
	// Le nœud est devenu une instance, au même endroit, au contenu généré.
	scene::NodeId instance = runtime.ResolveId("Table");
	const scene::Node* node = runtime.ActiveScene()->tree.Get(instance);
	EXPECT_TRUE(objects::IsInstance(*node));
	EXPECT_TRUE(node->transform.position == (math::FVector3{3.f, 0.f, 1.f}));
	EXPECT_EQ(GeneratedCount(runtime.ActiveScene()->tree), size_t(3));

	// Rendre indépendant : le contenu devient propre à la scène.
	ASSERT_TRUE(runtime.DetachInstance(instance).IsOk());
	EXPECT_FALSE(objects::IsInstance(*runtime.ActiveScene()->tree.Get(instance)));
	EXPECT_EQ(GeneratedCount(runtime.ActiveScene()->tree), size_t(0));
	runtime.RefreshObjectInstances();
	EXPECT_EQ(runtime.ActiveScene()->NodeCount(),
			  size_t(4)); // Table + son contenu, plus rien de régénéré
}

TEST(Objects, ScriptsInstantiateAndDefineObjects) {
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	Project project = MakeGarage();
	project.FindScene("Garage")->gameplayScript = String(R"(
class Voitures extends ObjectAsset {}
class Lampes extends ObjectAsset {}

class Atelier extends Scene {
    let voitures = nil
    let lampe = nil
    fn on_start() {
        this.voitures = Voitures("objects/Voiture.object")
        this.voitures.instantiate({pos: [10, 0, 0], rot: [0, 90, 0], scale: [1, 1, 1]})
        this.voitures.instantiate({pos: [-10, 0, 0]})
        scene.spawn({name: "Abat-jour", shape: "cone"})
        this.lampe = Lampes("Lampe")
        editor.log(format("lampe avant : {}", this.lampe.exists()))
        this.lampe.define("Abat-jour")
        this.lampe.instantiate({pos: [0, 3, 0]})
    }
}
)");
	runtime.OpenProject(std::move(project));
	ASSERT_TRUE(runtime.SwitchScene("Garage"));
	runtime.Play();
	for (int i = 0; i < 3; ++i)
		runtime.Update(1.f / 60.f);
	ASSERT_TRUE(runtime.IsPlaying());
	size_t voitures = 0, lampes = 0;
	const scene::NodeTree& tree = runtime.ActiveScene()->tree;
	tree.Traverse(tree.Root(), [&](scene::NodeId, const scene::Node& node) {
		if (objects::IsInstance(node) && !objects::IsGenerated(node)) {
			voitures += objects::SourceOf(node) == "Voiture";
			lampes += objects::SourceOf(node) == "Lampe";
		}
	});
	EXPECT_EQ(voitures, size_t(4)); // 2 de la scène + 2 du script
	EXPECT_EQ(lampes, size_t(1));
	EXPECT_TRUE(runtime.GetProject().Fs().FindObject(ObjectRef{"Lampe"}) != nullptr);
	runtime.Stop();
	// Charger un objet comme une scène, en partie, est refusé.
	runtime.Play();
	EXPECT_FALSE(runtime.LoadSceneInPlay("Voiture"));
	runtime.Stop();
}

int main() {
	return RUN_ALL_TESTS();
}
