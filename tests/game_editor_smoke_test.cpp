// Tests unitaires — examples/game_editor (éditeur de niveau 3D).
//
// Couvre les couches qui n'ont besoin NI de fenêtre NI de GPU : ligne de
// commande, document de projet (+ aller-retour JSON), contenu livré, runtime
// (instanciation, commandes d'édition, mode Jeu, physique), API hôte exposée
// aux scripts, scénarios intégrés et rapport d'exécution.
//
// C'est précisément parce que `Runtime` ne tient qu'un `render3d::Canvas*`
// FACULTATIF que ce fichier existe : sans ce choix, rien de tout cela ne
// serait vérifiable sans serveur d'affichage.
#define USE_TEST

#include "core/test.hpp"

#include <filesystem>

// Chemins relatifs : les en-têtes de `examples/` ne sont pas dans le chemin
// d'inclusion du Makefile (seul `lib/include` l'est), et une inclusion
// relative évite d'avoir à l'y ajouter juste pour ce test.
#include "../examples/game_editor_demo/app/app.hpp"
#include "../examples/game_editor_demo/app/cli.hpp"
#include "../examples/game_editor_demo/document/project_files.hpp"
#include "../examples/game_editor_demo/document/project.hpp"
#include "../examples/game_editor_demo/app/report.hpp"
#include "../examples/game_editor_demo/engine/runtime.hpp"
#include "../examples/game_editor_demo/app/scenarios.hpp"

using namespace game_editor;

namespace {

/// Fabrique un argv à partir d'un tableau de chaînes littérales.
Result<CommandLine, String> ParseArgs(std::vector<const char *> args) {
	std::vector<char *> argv;
	argv.push_back(const_cast<char *>("game_editor_demo"));
	for (const char *arg : args)
		argv.push_back(const_cast<char *>(arg));
	return CommandLine::Parse(int(argv.size()), argv.data());
}

/// Le projet de démonstration, lu depuis ses fichiers de données : plus
/// rien n'en est construit dans le code de l'éditeur. Lecture seule — aucun
/// test n'écrit dans ce dossier.
constexpr const char *DEMO_PROJECT = "tests/data/game_editor_demo/demo/demo.json";

Project LoadDemoProject() {
	auto project = files::LoadProject(String(DEMO_PROJECT));
	if (project.IsError()) {
		test::ReportFailure(__FILE__, __LINE__, project.Error().CStr());
		return files::MakeBlankProject(String("introuvable"));
	}
	return std::move(project).Unwrap();
}

/// Runtime prêt à l'emploi sur le projet de démonstration, sans GPU.
struct Harness {
	ecs::ArchetypeRegistry registry;
	Runtime runtime{registry};

	Harness() { runtime.OpenProject(LoadDemoProject()); }

	/// Exécute une source dans l'interpréteur outil et fait échouer le test
	/// avec le message d'erreur formaté si elle ne compile/tourne pas.
	bool Run(const char *source) {
		auto result = runtime.RunToolScript(String(source));
		if (result.IsError()) {
			test::ReportFailure(__FILE__, __LINE__, result.Error().Format().CStr());
			return false;
		}
		return true;
	}
};

} // namespace

// ============================================================================
// Ligne de commande
// ============================================================================

TEST(Cli, ParsesEveryOption) {
	auto parsed = ParseArgs({"--width=1280", "--height=720", "--theme=aero", "--scene=Circuit", "--scenario=tour",
							 "--frames=250", "--seed=7", "--report=out.json", "--report-format=json",
							 "--screenshot-dir=shots", "--screenshot=10:a.png", "--screenshot=20:b.png",
							 "--headless", "--verbose"});
	ASSERT_TRUE(parsed.IsOk());
	const CommandLine &options = parsed.Value();
	EXPECT_EQ(options.windowWidth, 1280);
	EXPECT_EQ(options.windowHeight, 720);
	EXPECT_EQ(options.theme, "aero");
	EXPECT_EQ(options.scene, "Circuit");
	EXPECT_EQ(options.scenario, "tour");
	EXPECT_EQ(options.frames, 250L);
	EXPECT_TRUE(options.randomSeed == 7u);
	EXPECT_EQ(options.reportPath, "out.json");
	EXPECT_TRUE(options.reportFormat == ReportFormat::JSON);
	EXPECT_EQ(options.screenshotDir, "shots");
	EXPECT_TRUE(options.headless);
	EXPECT_TRUE(options.verbose);
	ASSERT_TRUE(options.screenshots.size() == 2);
	EXPECT_EQ(options.screenshots[0].frame, 10L);
	EXPECT_EQ(options.screenshots[1].path, "b.png");
}

TEST(Cli, RejectsBadInputInsteadOfIgnoringIt) {
	// Une faute de frappe dans une commande censée produire un rapport doit
	// être signalée, jamais avalée en silence.
	EXPECT_TRUE(ParseArgs({"--widht=800"}).IsError());
	EXPECT_TRUE(ParseArgs({"--frames=zero"}).IsError());
	EXPECT_TRUE(ParseArgs({"--frames=-4"}).IsError());
	EXPECT_TRUE(ParseArgs({"--theme=neon"}).IsError());
	EXPECT_TRUE(ParseArgs({"--report-format=xml"}).IsError());
	EXPECT_TRUE(ParseArgs({"--screenshot=captures/a.png"}).IsError()); // numéro d'image manquant
	EXPECT_TRUE(ParseArgs({"--screenshot=12:"}).IsError());            // chemin vide
}

TEST(Cli, ScreenshotPathMayContainColons) {
	auto parsed = ParseArgs({"--screenshot=5:C:/temp/a.png"});
	ASSERT_TRUE(parsed.IsOk());
	ASSERT_TRUE(parsed.Value().screenshots.size() == 1);
	EXPECT_EQ(parsed.Value().screenshots[0].path, "C:/temp/a.png");
}

TEST(Cli, DefaultsAreUsable) {
	auto parsed = ParseArgs({});
	ASSERT_TRUE(parsed.IsOk());
	EXPECT_FALSE(parsed.Value().headless);
	EXPECT_EQ(parsed.Value().frames, 0L);
	EXPECT_EQ(parsed.Value().theme, "studio");
}

// ============================================================================
// Document de projet
// ============================================================================

TEST(Project, JsonRoundTripPreservesEverything) {
	Project original = LoadDemoProject();
	original.activeScene = "Circuit";

	String encoded = original.EncodeJson();
	auto decoded = Project::DecodeJson(encoded);
	ASSERT_TRUE(decoded.IsOk());
	const Project &reloaded = decoded.Value();

	EXPECT_EQ(reloaded.name, original.name);
	EXPECT_EQ(reloaded.activeScene, "Circuit");
	ASSERT_TRUE(reloaded.scenes.size() == original.scenes.size());
	EXPECT_TRUE(reloaded.TotalObjectCount() == original.TotalObjectCount());

	for (size_t i = 0; i < original.scenes.size(); ++i) {
		const SceneDesc &before = original.scenes[i];
		const SceneDesc &after = reloaded.scenes[i];
		EXPECT_EQ(after.name, before.name);
		EXPECT_EQ(after.gameplayScript, before.gameplayScript);
		ASSERT_TRUE(after.ObjectCount() == before.ObjectCount());
		std::vector<scene::NodeId> beforeIds = before.Objects();
		std::vector<scene::NodeId> afterIds = after.Objects();
		for (size_t j = 0; j < beforeIds.size(); ++j) {
			const scene::Node &a = *before.tree.Get(beforeIds[j]);
			const scene::Node &b = *after.tree.Get(afterIds[j]);
			EXPECT_EQ(b.name, a.name);
			EXPECT_TRUE(b.type == a.type);
			EXPECT_TRUE(VisualDesc::Read(b).shape == VisualDesc::Read(a).shape);
			EXPECT_TRUE(b.transform.position == a.transform.position);
			EXPECT_TRUE(b.transform.scale == a.transform.scale);
			EXPECT_TRUE(PhysicsDesc::Read(b).body == PhysicsDesc::Read(a).body);
			EXPECT_TRUE(VisualDesc::Read(b).material.kind == VisualDesc::Read(a).material.kind);
			// La HIÉRARCHIE aussi doit survivre à l'aller-retour.
			EXPECT_TRUE(after.tree.PathOf(afterIds[j]) == before.tree.PathOf(beforeIds[j]));
		}
	}
}

TEST(Project, WholeNumberCoordinatesSurviveTheRoundTrip) {
	// Régression : l'encodeur JSON écrivait un FLOAT de valeur entière sans
	// point décimal, le décodeur le relisait en INT, et toute coordonnée
	// entière revenait à zéro. Corrigé dans data/json.hpp ; ce test verrouille
	// le comportement côté éditeur, où les coordonnées entières sont la norme.
	Project project;
	SceneDesc scene;
	scene.name = "Test";
	ObjectDesc object;
	object.name = "Bloc";
	object.transform.position = {5.f, 0.f, -12.f};
	object.transform.scale = {2.f, 1.f, 3.f};
	object.physics.mass = 4.f;
	(void)scene.Add(std::move(object));
	project.scenes.push_back(std::move(scene));
	project.activeScene = "Test";

	auto decoded = Project::DecodeJson(project.EncodeJson());
	ASSERT_TRUE(decoded.IsOk());
	const scene::Node *reloaded = decoded.Value().scenes[0].Find("Bloc");
	ASSERT_TRUE(reloaded != nullptr);
	EXPECT_EQ(reloaded->transform.position.x, 5.f);
	EXPECT_EQ(reloaded->transform.position.z, -12.f);
	EXPECT_EQ(reloaded->transform.scale.z, 3.f);
	EXPECT_EQ(PhysicsDesc::Read(*reloaded).mass, 4.f);
}

// Régression : le script de jeu d'une scène est lu sous
// `scenes/../scripts/X.gameplay.script` ; réenregistrer le projet ne doit pas
// le prendre pour un orphelin et l'effacer juste après l'avoir écrit.
TEST(Project, ResavingKeepsGameplayScriptFiles) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "game_editor_resave_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	const String manifest((dir / "resave.json").string().c_str());
	Project project = files::MakeBlankProject(String("Resave"));
	project.scenes.front().gameplayScript = String("fn on_update(dt) { }\n");
	ASSERT_TRUE(files::SaveProject(project, manifest).IsOk());
	files::FileList loaded;
	auto reloaded = files::LoadProject(manifest, &loaded);
	ASSERT_TRUE(reloaded.IsOk());
	ASSERT_TRUE(files::SaveProject(reloaded.Value(), manifest, loaded).IsOk());
	auto again = files::LoadProject(manifest);
	ASSERT_TRUE(again.IsOk());
	EXPECT_EQ(again.Value().scenes.front().gameplayScript, project.scenes.front().gameplayScript);
	EXPECT_EQ(files::NormalizePath(String("/a/scenes/../scripts/./x")), "/a/scripts/x");
	EXPECT_EQ(files::NormalizePath(String("../b/../c")), "../c");
	std::filesystem::remove_all(dir);
}

TEST(Project, RejectsMalformedDocuments) {
	EXPECT_TRUE(Project::DecodeJson(String("pas du json")).IsError());
	EXPECT_TRUE(Project::DecodeJson(String("{}")).IsError());                       // pas de `scenes`
	EXPECT_TRUE(Project::DecodeJson(String("{\"scenes\": []}")).IsError());         // aucune scène
	EXPECT_TRUE(Project::DecodeJson(String("{\"scenes\": [{}]}")).IsError());       // scène sans nom
	// Version de format plus récente : refusée explicitement, pas lue de travers.
	EXPECT_TRUE(Project::DecodeJson(String("{\"version\": 9999, \"scenes\": [{\"name\": \"a\"}]}")).IsError());
}

TEST(Project, ActiveSceneFallsBackToTheFirstOne) {
	Project project = LoadDemoProject();
	project.activeScene = "Scène inexistante";
	ASSERT_TRUE(project.ActiveScene() != nullptr);
	EXPECT_EQ(project.ActiveScene()->name, project.scenes.front().name);
	EXPECT_FALSE(project.SetActiveScene("Toujours inexistante"));
	EXPECT_TRUE(project.SetActiveScene("Circuit"));
	EXPECT_EQ(project.ActiveScene()->name, "Circuit");
}

TEST(SceneDesc, UniqueNamesAndChildDetachOnRemove) {
	SceneDesc scene;
	scene.name = "S";

	ObjectDesc parent;
	parent.name = "Boîte";
	EXPECT_EQ(scene.Add(parent), "Boîte");
	EXPECT_EQ(scene.Add(parent), "Boîte 2"); // même nom demandé -> suffixé
	EXPECT_EQ(scene.Add(parent), "Boîte 3");

	ObjectDesc child;
	child.name = "Enfant";
	child.parent = "Boîte";
	scene.Add(child);

	ASSERT_TRUE(scene.Find("Enfant") != nullptr);
	EXPECT_TRUE(scene.Find("Enfant")->parent == scene.FindId("Boîte"));

	// Supprimer le parent supprime son SOUS-ARBRE. C'est le changement de
	// comportement assumé par le passage à un vrai arbre : dans la liste
	// plate, l'enfant remontait à la racine — ce qui éparpillait les roues
	// d'une voiture supprimée dans toute la scène.
	EXPECT_TRUE(scene.Remove("Boîte"));
	EXPECT_TRUE(scene.Find("Boîte") == nullptr);
	EXPECT_TRUE(scene.Find("Enfant") == nullptr);
	EXPECT_FALSE(scene.Remove("Boîte")); // déjà supprimé
}

TEST(TransformDesc, EulerAndQuaternionRoundTrip) {
	// Repose sur math::FQuaternion::ToEuler, ajouté pour l'inspecteur. Le
	// transform d'édition est désormais celui de la bibliothèque
	// (scene::Transform) : quaternion en mémoire, degrés à l'affichage.
	TransformDesc transform;
	transform.SetEulerDegrees({18.f, -47.f, 33.f});
	math::FQuaternion rotation = transform.rotation;

	TransformDesc restored;
	restored.rotation = rotation;
	EXPECT_TRUE(sdl3::Abs(restored.EulerDegrees().x - 18.f) < 0.01f);
	EXPECT_TRUE(sdl3::Abs(restored.EulerDegrees().y - -47.f) < 0.01f);
	EXPECT_TRUE(sdl3::Abs(restored.EulerDegrees().z - 33.f) < 0.01f);
}

/// Nom du i-ème objet de la scène, dans l'ordre d'affichage de l'arbre
/// (parcours préfixe, racine exclue) — remplace l'ancien `objects[i].name`
/// d'une liste plate. L'ordre est stable, donc les tests qui visaient
/// « le 2e objet de la vitrine » visent toujours le même.
[[nodiscard]] static String ObjectNameAt(const SceneDesc &scene, size_t index) {
	std::vector<scene::NodeId> objects = scene.Objects();
	return index < objects.size() ? scene.tree.Get(objects[index])->name : String();
}

// ============================================================================
// Contenu livré
// ============================================================================

TEST(Content, DemoProjectIsWellFormed) {
	Project project = LoadDemoProject();
	ASSERT_TRUE(project.scenes.size() == 5);

	for (const SceneDesc &scene : project.scenes) {
		EXPECT_FALSE(scene.name.IsEmpty());
		EXPECT_FALSE(scene.description.IsEmpty());
		EXPECT_FALSE(scene.gameplayScript.IsEmpty());
		EXPECT_TRUE(scene.ObjectCount() > 0);

		for (scene::NodeId id : scene.Objects()) {
			const scene::Node &object = *scene.tree.Get(id);
			EXPECT_FALSE(object.name.IsEmpty());
			// Unicité entre FRÈRES — la règle d'un arbre, et tout ce dont un
			// chemin a besoin pour être sans ambiguïté. L'unicité GLOBALE
			// n'est qu'une politique de l'éditeur pour les objets créés à la
			// main (cf. SceneDesc::UniqueName) : la scène « Assemblages »
			// contient délibérément deux « Tête », une par personnage.
			int count = 0;
			for (scene::NodeId sibling : scene.tree.ChildrenOf(object.parent))
				if (scene.tree.Get(sibling)->name == object.name)
					++count;
			EXPECT_EQ(count, 1);
			// Le parent doit toujours exister dans l'arbre.
			EXPECT_TRUE(scene.tree.Contains(object.parent));
		}
		// Et l'arbre lui-même doit être intègre (cycles, liens croisés,
		// références pendouillantes — cf. scene::NodeTree::Validate).
		EXPECT_TRUE(scene.tree.Validate().Ok());
	}
}

TEST(Content, EveryScriptOfTheProjectCompiles) {
	// Un script (fichier .script du projet) qui ne compile pas ne se verrait
	// qu'au moment de lancer le mode Jeu, dans une scène précise : autant
	// l'attraper ici — scripts de jeu des scènes ET bibliothèque.
	Project project = LoadDemoProject();
	std::vector<String> sources;
	for (const SceneDesc &scene : project.scenes)
		if (!scene.gameplayScript.IsEmpty())
			sources.push_back(scene.gameplayScript);
	for (const ScriptAsset &script : project.scripts)
		sources.push_back(script.source);
	EXPECT_TRUE(sources.size() >= 8u);
	for (const String &source : sources) {
		auto program = data::script::Parser::Compile(source.View());
		if (program.IsError())
			test::ReportFailure(__FILE__, __LINE__, program.Error().Format().CStr());
		EXPECT_TRUE(program.IsOk());
	}
}

TEST(Content, CircuitHasAnOrderedRingOfCheckpoints) {
	Project project = LoadDemoProject();
	const SceneDesc *circuit = project.FindScene("Circuit");
	ASSERT_TRUE(circuit != nullptr);

	std::vector<scene::NodeId> checkpoints = circuit->WithTag(String("checkpoint"));
	ASSERT_TRUE(checkpoints.size() == 8);

	// Le pilote automatique vise chaque point EN LIGNE DROITE : l'écart entre
	// la corde et l'arc doit rester dans la demi-largeur de piste, sinon la
	// voiture coupe dans le décor. On vérifie donc que deux points
	// consécutifs sont assez proches pour que ce soit vrai.
	for (size_t i = 0; i + 1 < checkpoints.size(); ++i) {
		math::FVector3 a = circuit->tree.GlobalPosition(checkpoints[i]);
		math::FVector3 b = circuit->tree.GlobalPosition(checkpoints[i + 1]);
		float distance = (b - a).Length();
		EXPECT_TRUE(distance > 10.f);
		EXPECT_TRUE(distance < 30.f);
	}

	// La voiture est posée sur le premier point, et c'est elle que suit la
	// caméra du mode Jeu.
	const scene::Node *car = circuit->Find("Voiture");
	ASSERT_TRUE(car != nullptr);
	EXPECT_EQ(TagOf(*car), "camera_target");
	EXPECT_TRUE(PhysicsDesc::Read(*car).body == BodyKind::DYNAMIC);
	EXPECT_TRUE((car->transform.position - circuit->tree.GlobalPosition(checkpoints[0])).Length() < 2.f);
}

// ============================================================================
// Runtime — instanciation et commandes
// ============================================================================

TEST(Runtime, InstantiatesTheActiveSceneOnly) {
	Harness harness;
	const SceneDesc *scene = harness.runtime.ActiveScene();
	ASSERT_TRUE(scene != nullptr);
	EXPECT_EQ(scene->name, "Vitrine");
	// Une entité de runtime par objet du document, et pas une de plus (les
	// autres scènes du projet ne sont PAS instanciées).
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == scene->ObjectCount());
	EXPECT_TRUE(harness.runtime.RigidBodyCount() > 0);
	EXPECT_TRUE(harness.runtime.RigidBodyCount() < scene->ObjectCount());
}

TEST(Runtime, SwitchingSceneRebuildsEverything) {
	Harness harness;
	EXPECT_FALSE(harness.runtime.SwitchScene("Scène inexistante"));
	ASSERT_TRUE(harness.runtime.SwitchScene("Laboratoire physique"));

	const SceneDesc *scene = harness.runtime.ActiveScene();
	ASSERT_TRUE(scene != nullptr);
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == scene->ObjectCount());
	// Aucun reliquat de la scène précédente.
	EXPECT_TRUE(harness.runtime.Registry().EntitiesWith<SceneObjectRef>().size() == scene->ObjectCount());
}

TEST(Runtime, EditCommandsWriteToDocumentAndRuntimeTogether) {
	Harness harness;
	const char *name = "Cube plastique";

	ASSERT_TRUE(harness.runtime.SetPosition(String(name), {3.f, 2.f, 1.f}));
	const scene::Node *object = harness.runtime.ActiveScene()->Find(name);
	ASSERT_TRUE(object != nullptr);
	EXPECT_TRUE(object->transform.position == math::FVector3(3.f, 2.f, 1.f));

	ASSERT_TRUE(harness.runtime.SetEulerDegrees(String(name), {0.f, 90.f, 0.f}));
	EXPECT_TRUE(sdl3::Abs(harness.runtime.ActiveScene()->Find(name)->transform.EulerDegrees().y - 90.f) < 0.01f);

	// L'échelle est bornée : une valeur nulle produirait une matrice
	// singulière (objet invisible, normales dégénérées).
	ASSERT_TRUE(harness.runtime.SetScale(String(name), {0.f, -5.f, 2.f}));
	EXPECT_TRUE(harness.runtime.ActiveScene()->Find(name)->transform.scale.x > 0.f);
	EXPECT_TRUE(harness.runtime.ActiveScene()->Find(name)->transform.scale.y > 0.f);
	EXPECT_EQ(harness.runtime.ActiveScene()->Find(name)->transform.scale.z, 2.f);

	ASSERT_TRUE(harness.runtime.SetMaterialColor(String(name), sdl3::Color{10, 20, 30, 255}));
	EXPECT_TRUE(VisualDesc::Read(*harness.runtime.ActiveScene()->Find(name)).material.baseColor.r == 10);

	// Objet inexistant : `false`, jamais un plantage.
	EXPECT_FALSE(harness.runtime.SetPosition(String("Néant"), {}));
	EXPECT_FALSE(harness.runtime.SetMaterialColor(String("Néant"), sdl3::Color::WHITE()));
}

TEST(Runtime, PhysicsComponentFollowsTheDocument) {
	Harness harness;
	const String name("Cube plastique");
	EXPECT_TRUE(harness.runtime.GetVelocity(name).IsNone()); // pas de corps au départ

	PhysicsDesc physics;
	physics.body = BodyKind::DYNAMIC;
	physics.mass = 5.f;
	ASSERT_TRUE(harness.runtime.SetPhysics(name, physics));
	EXPECT_TRUE(harness.runtime.GetVelocity(name).IsSome());
	ASSERT_TRUE(harness.runtime.SetVelocity(name, {1.f, 2.f, 3.f}));
	EXPECT_TRUE(harness.runtime.GetVelocity(name).Unwrap().y == 2.f);

	// Repasser à « aucun corps » retire bien le composant.
	physics.body = BodyKind::NONE;
	ASSERT_TRUE(harness.runtime.SetPhysics(name, physics));
	EXPECT_TRUE(harness.runtime.GetVelocity(name).IsNone());
}

TEST(Runtime, SpawnRemoveAndRename) {
	Harness harness;
	size_t before = harness.runtime.ActiveScene()->ObjectCount();

	ObjectDesc object;
	object.name = "Cube plastique"; // nom déjà pris -> doit être suffixé
	object.shape = ShapeKind::SPHERE;
	Option<String> assigned = harness.runtime.SpawnObject(object);
	ASSERT_TRUE(assigned.IsSome());
	EXPECT_TRUE(assigned.Unwrap() != "Cube plastique");
	EXPECT_TRUE(harness.runtime.ActiveScene()->ObjectCount() == before + 1);
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == before + 1);

	// Renommage : refusé vers un nom déjà pris, accepté sinon.
	EXPECT_FALSE(harness.runtime.RenameObject(assigned.Unwrap(), String("Cube plastique")));
	ASSERT_TRUE(harness.runtime.RenameObject(assigned.Unwrap(), String("Nouvelle sphère")));
	EXPECT_TRUE(harness.runtime.ActiveScene()->Find("Nouvelle sphère") != nullptr);

	ASSERT_TRUE(harness.runtime.RemoveObject(String("Nouvelle sphère")));
	EXPECT_TRUE(harness.runtime.ActiveScene()->ObjectCount() == before);
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == before);
	EXPECT_FALSE(harness.runtime.RemoveObject(String("Nouvelle sphère")));
}

TEST(Runtime, SelectionIsSingleAndSurvivesNothingElse) {
	Harness harness;
	EXPECT_TRUE(harness.runtime.SelectedName().IsNone());

	ASSERT_TRUE(harness.runtime.Select(String("Tore")));
	EXPECT_EQ(harness.runtime.SelectedName().Unwrap(), "Tore");
	ASSERT_TRUE(harness.runtime.SelectedObject() != nullptr);

	// Sélectionner ailleurs REMPLACE la sélection (une seule à la fois).
	ASSERT_TRUE(harness.runtime.Select(String("Balise")));
	EXPECT_EQ(harness.runtime.SelectedName().Unwrap(), "Balise");
	EXPECT_TRUE(harness.runtime.Registry().EntitiesWith<Selected>().size() == 1);

	EXPECT_FALSE(harness.runtime.Select(String("Néant")));
	EXPECT_TRUE(harness.runtime.SelectedName().IsNone());

	// Supprimer l'objet sélectionné vide la sélection.
	ASSERT_TRUE(harness.runtime.Select(String("Tore")));
	ASSERT_TRUE(harness.runtime.RemoveObject(String("Tore")));
	EXPECT_TRUE(harness.runtime.SelectedName().IsNone());
}

// ============================================================================
// Runtime — mode Jeu
// ============================================================================

TEST(Runtime, PlayThenStopRestoresTheSceneExactly) {
	Harness harness;
	ASSERT_TRUE(harness.runtime.SwitchScene("Laboratoire physique"));

	// Instantané = l'ARBRE entier (copie de valeur, cf. scene::NodeTree).
	const scene::NodeTree before = harness.runtime.ActiveScene()->tree;
	harness.runtime.Play();
	EXPECT_TRUE(harness.runtime.IsPlaying());

	for (int i = 0; i < 150; ++i)
		harness.runtime.Update(1.f / 60.f);

	// La partie a réellement FAIT quelque chose — sinon « restaure à
	// l'identique » ne prouverait rien. Le script de cette scène largue des
	// caisses : la scène compte donc plus d'objets qu'au départ, et au moins
	// une caisse est déjà descendue sous son altitude d'apparition.
	const SceneDesc *playing = harness.runtime.ActiveScene();
	EXPECT_TRUE(playing->ObjectCount() > before.Size() - 1);
	bool fell = false;
	for (scene::NodeId crate : playing->WithTag(String("debris")))
		if (playing->tree.GlobalPosition(crate).y < 9.f)
			fell = true;
	EXPECT_TRUE(fell);

	harness.runtime.Stop();
	EXPECT_FALSE(harness.runtime.IsPlaying());

	// Stop restaure l'instantané ENTIER : les caisses larguées pendant la
	// partie disparaissent avec elle, et le projet enregistré est intact.
	const scene::NodeTree &after = harness.runtime.ActiveScene()->tree;
	ASSERT_TRUE(after.Size() == before.Size());
	EXPECT_TRUE(harness.runtime.ActiveScene()->WithTag(String("debris")).empty());
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == before.Size() - 1);
	// Égalité STRUCTURELLE de l'arbre : mêmes nœuds, mêmes liens, mêmes
	// transforms — une comparaison champ à champ ne dirait rien de la
	// hiérarchie, qui est justement ce que le mode Jeu pourrait abîmer.
	EXPECT_TRUE(after == before);
}

TEST(Runtime, PlayIsReproducible) {
	// Deux parties, même graine, même pas de temps : mêmes positions finales.
	// C'est ce qui permet à un rapport d'exécution d'être comparé d'un run à
	// l'autre, et à un scénario d'affirmer un nombre de tours.
	auto simulate = [](uint64_t seed) {
		ecs::ArchetypeRegistry registry;
		Runtime runtime(registry);
		runtime.SetRandomSeed(seed);
		runtime.OpenProject(LoadDemoProject());
		(void)runtime.SwitchScene("Circuit");
		runtime.Play();
		for (int i = 0; i < 240; ++i)
			runtime.Update(1.f / 60.f);
		const scene::Node *car = runtime.ActiveScene()->Find("Voiture");
		return car ? car->transform.position : math::FVector3{};
	};
	math::FVector3 first = simulate(1234);
	math::FVector3 second = simulate(1234);
	EXPECT_TRUE((first - second).Length() < 1e-4f);
}

TEST(Runtime, CircuitIsActuallyPlayable) {
	// L'essai de jouabilité : sous pilote automatique, la voiture doit boucler
	// au moins un tour complet en un temps raisonnable — et la partie doit
	// rester exempte d'erreur de script.
	Harness harness;
	ASSERT_TRUE(harness.runtime.SwitchScene("Circuit"));
	harness.runtime.Play();
	ASSERT_TRUE(harness.runtime.IsPlaying());

	for (int i = 0; i < 900; ++i) // 15 s simulées
		harness.runtime.Update(1.f / 60.f);

	EXPECT_EQ(int(harness.runtime.ScriptErrorCount()), 0);

	Option<data::script::Value> laps = harness.runtime.GameplayGlobal(String("total_laps"));
	ASSERT_TRUE(laps.IsSome());
	EXPECT_TRUE(laps.Unwrap().AsNumber() >= 1.0);

	// La voiture est restée SUR la piste : à ~30 unités du centre, et pas
	// tombée à travers le sol.
	const scene::Node *car = harness.runtime.ActiveScene()->Find("Voiture");
	ASSERT_TRUE(car != nullptr);
	float radius = math::FVector2{car->transform.position.x, car->transform.position.z}.Length();
	EXPECT_TRUE(radius > 24.f);
	EXPECT_TRUE(radius < 36.f);
	EXPECT_TRUE(car->transform.position.y > -1.f);
}

TEST(Runtime, ShowcaseAnimationScriptDrivesObjects) {
	Harness harness;
	const scene::Node *beacon = harness.runtime.ActiveScene()->Find("Balise");
	ASSERT_TRUE(beacon != nullptr);
	float restingHeight = beacon->transform.position.y;

	harness.runtime.Play();
	for (int i = 0; i < 40; ++i)
		harness.runtime.Update(1.f / 60.f);

	EXPECT_EQ(int(harness.runtime.ScriptErrorCount()), 0);
	const scene::Node *animated = harness.runtime.ActiveScene()->Find("Balise");
	ASSERT_TRUE(animated != nullptr);
	EXPECT_TRUE(sdl3::Abs(animated->transform.position.y - restingHeight) > 0.05f);
}

TEST(Runtime, ABrokenGameplayScriptIsDisabledInsteadOfSpamming) {
	// Un rappel qui échoue à 60 Hz noierait la console et le rapport sous des
	// milliers de lignes identiques : il doit être désactivé au premier échec.
	Harness harness;
	SceneDesc *scene = harness.runtime.ActiveScene();
	ASSERT_TRUE(scene != nullptr);
	scene->gameplayScript = String("fn on_update(dt) { return inexistant() }");

	harness.runtime.Play();
	for (int i = 0; i < 50; ++i)
		harness.runtime.Update(1.f / 60.f);
	EXPECT_EQ(int(harness.runtime.ScriptErrorCount()), 1);
}

// ============================================================================
// API hôte exposée aux scripts
// ============================================================================

TEST(ScriptApi, SpawnsAndEditsObjects) {
	Harness harness;
	size_t before = harness.runtime.ActiveScene()->ObjectCount();

	ASSERT_TRUE(harness.Run("for (i in range(0, 5)) {\n"
							"    scene.spawn({\n"
							"        name: \"Généré \" .. i,\n"
							"        shape: \"sphere\",\n"
							"        size: [1, 1, 1],\n"
							"        pos: [i * 2, 3, 0],\n"
							"        color: [200, 100, 50],\n"
							"        body: \"dynamic\",\n"
							"        mass: 2,\n"
							"        tag: \"genere\"\n"
							"    })\n"
							"}"));

	const SceneDesc *scene = harness.runtime.ActiveScene();
	EXPECT_TRUE(scene->ObjectCount() == before + 5);
	EXPECT_TRUE(scene->WithTag(String("genere")).size() == 5);

	const scene::Node *third = scene->Find("Généré 2");
	ASSERT_TRUE(third != nullptr);
	EXPECT_TRUE(VisualDesc::Read(*third).shape == ShapeKind::SPHERE);
	EXPECT_TRUE(PhysicsDesc::Read(*third).body == BodyKind::DYNAMIC);
	EXPECT_EQ(third->transform.position.x, 4.f);
	EXPECT_TRUE(VisualDesc::Read(*third).material.baseColor.r == 200);
	// Chaque objet créé par script existe AUSSI dans le runtime.
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == before + 5);
}

TEST(ScriptApi, QueriesReturnNilForMissingObjects) {
	// Un script qui interroge un objet disparu doit pouvoir tester le
	// résultat, pas s'interrompre.
	Harness harness;
	ASSERT_TRUE(harness.Run("assert(object.position(\"Néant\") == nil)\n"
							"assert(object.velocity(\"Néant\") == nil)\n"
							"assert(object.distance(\"Néant\", \"Tore\") == nil)\n"
							"assert(scene.exists(\"Tore\"))\n"
							"assert(not scene.exists(\"Néant\"))"));
}

TEST(ScriptApi, PartialMaterialTableKeepsUntouchedFields) {
	Harness harness;
	const scene::Node *before = harness.runtime.ActiveScene()->Find("Tore");
	ASSERT_TRUE(before != nullptr);
	sdl3::Color originalColor = VisualDesc::Read(*before).material.baseColor;

	ASSERT_TRUE(harness.Run("object.set_material(\"Tore\", {roughness: 0.11})"));

	const scene::Node *after = harness.runtime.ActiveScene()->Find("Tore");
	ASSERT_TRUE(after != nullptr);
	const MaterialDesc material = VisualDesc::Read(*after).material;
	EXPECT_TRUE(sdl3::Abs(material.roughness - 0.11f) < 1e-5f);
	EXPECT_TRUE(material.baseColor.r == originalColor.r);
	EXPECT_TRUE(material.baseColor.g == originalColor.g);
}

TEST(ScriptApi, DrivesTheEditorItself) {
	Harness harness;
	ASSERT_TRUE(harness.Run("editor.select(\"Tore\")\n"
							"assert(editor.selected() == \"Tore\")\n"
							"assert(editor.scene() == \"Vitrine\")\n"
							"assert(len(editor.scenes()) == 5)\n"
							"assert(editor.switch_scene(\"Circuit\"))\n"
							"assert(editor.scene() == \"Circuit\")\n"
							"assert(editor.play())\n"
							"assert(editor.is_playing())\n"
							"assert(editor.stop())\n"
							"assert(not editor.is_playing())"));
}

TEST(ScriptApi, HostHooksAreOptionalAndReturnFalseWhenAbsent) {
	// Le même scénario doit tourner avec ou sans écran : sans hôte graphique,
	// ces fonctions rendent `false` au lieu d'échouer.
	Harness harness;
	ASSERT_TRUE(harness.Run("assert(editor.screenshot(\"/tmp/rien.png\") == false)\n"
							"assert(editor.set_theme(\"light\") == false)\n"
							"assert(editor.open_panel(\"console\") == false)"));

	// ... et les utilisent dès qu'ils sont installés.
	bool captured = false;
	harness.runtime.onScreenshot = [&captured](const String &) {
		captured = true;
		return true;
	};
	ASSERT_TRUE(harness.Run("assert(editor.screenshot(\"/tmp/rien.png\"))"));
	EXPECT_TRUE(captured);
}

TEST(ScriptApi, PrintGoesToTheEditorConsole) {
	Harness harness;
	size_t before = harness.runtime.Log().size();
	ASSERT_TRUE(harness.Run("print(\"bonjour\")\neditor.log(\"deux\", 3)"));
	ASSERT_TRUE(harness.runtime.Log().size() == before + 2);
	EXPECT_EQ(harness.runtime.Log()[before].text, "bonjour");
	EXPECT_EQ(harness.runtime.Log()[before + 1].text, "deux 3");
	EXPECT_TRUE(harness.runtime.Log()[before].level == LogLevel::SCRIPT);
}

TEST(ScriptApi, DataCodecsAreAvailableToScripts) {
	// La couche `data::` vue depuis un script : c'est ce qui permet de lire un
	// .gltf ou un projet JSON depuis du script d'éditeur.
	Harness harness;
	ASSERT_TRUE(harness.Run("let doc = parse(\"json\", \"{\\\"meshes\\\": [1, 2, 3]}\")\n"
							"assert(len(doc.meshes) == 3)\n"
							"let text = encode(\"json\", {a: 1})\n"
							"assert(find(text, \"\\\"a\\\"\") >= 0)"));
}

// ============================================================================
// Import glTF
// ============================================================================

TEST(GltfImport, ImportsARealModelAsASceneObject) {
	Harness harness;
	size_t before = harness.runtime.ActiveScene()->ObjectCount();

	auto imported = harness.runtime.ImportGltf(String("assets/models/animals/Cow.gltf"));
	if (imported.IsError())
		test::ReportFailure(__FILE__, __LINE__, imported.Error().CStr());
	ASSERT_TRUE(imported.IsOk());

	const scene::Node *object = harness.runtime.ActiveScene()->Find(imported.Value());
	ASSERT_TRUE(object != nullptr);
	EXPECT_TRUE(VisualDesc::Read(*object).shape == ShapeKind::MODEL);
	EXPECT_EQ(VisualDesc::Read(*object).source, "assets/models/animals/Cow.gltf");
	EXPECT_EQ(object->name, "Cow");
	// Le modèle est ramené à une taille exploitable : sans cet ajustement, un
	// import arrive soit invisible soit grand comme un mur.
	EXPECT_TRUE(object->transform.scale.x > 0.f);
	math::FVector3 fitted = VisualDesc::Read(*object).dimensions * object->transform.scale.x;
	float largest = sdl3::Max(sdl3::Max(fitted.x, fitted.y), fitted.z);
	EXPECT_TRUE(sdl3::Abs(largest - 3.f) < 0.1f);

	EXPECT_TRUE(harness.runtime.ActiveScene()->ObjectCount() == before + 1);
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == before + 1);
}

TEST(GltfImport, SurvivesASaveLoadRoundTrip) {
	// Le document ne contient que le CHEMIN du modèle : après un aller-retour
	// JSON, la géométrie doit se recharger depuis le fichier.
	Harness harness;
	auto imported = harness.runtime.ImportGltf(String("assets/models/animals/Sheep.gltf"));
	ASSERT_TRUE(imported.IsOk());

	String encoded = harness.runtime.GetProject().EncodeJson();
	auto reloaded = Project::DecodeJson(encoded);
	ASSERT_TRUE(reloaded.IsOk());

	const scene::Node *object = reloaded.Value().ActiveScene()->Find(imported.Value());
	ASSERT_TRUE(object != nullptr);
	EXPECT_TRUE(VisualDesc::Read(*object).shape == ShapeKind::MODEL);
	EXPECT_EQ(VisualDesc::Read(*object).source, "assets/models/animals/Sheep.gltf");

	// Et le runtime sait bien reconstruire ce maillage à partir du chemin.
	auto mesh = Runtime::LoadGltfMesh(VisualDesc::Read(*object).source);
	ASSERT_TRUE(mesh.IsOk());
	EXPECT_TRUE(mesh.Value().Vertices().size() > 0);
}

TEST(GltfImport, MissingOrInvalidModelIsAnErrorNotACrash) {
	Harness harness;
	EXPECT_TRUE(harness.runtime.ImportGltf(String("assets/models/inexistant.gltf")).IsError());
	EXPECT_TRUE(Runtime::LoadGltfMesh(String()).IsError());
	// Un `.bin` n'est pas du JSON : erreur de décodage, pas de plantage.
	EXPECT_TRUE(Runtime::LoadGltfMesh(String("assets/models/animals/Cow.bin")).IsError());
}

TEST(GltfImport, ScriptsCanImportAndInspectModels) {
	Harness harness;
	ASSERT_TRUE(harness.Run("let info = scene.model_info(\"assets/models/animals/Horse.gltf\")\n"
							"assert(info, \"model_info a échoué\")\n"
							"assert(info.meshes >= 1)\n"
							"assert(starts_with(info.version, \"2.\"))\n"
							"assert(scene.model_info(\"/rien.gltf\") == nil)\n"
							"let name = scene.import(\"assets/models/animals/Horse.gltf\", 4)\n"
							"assert(name, \"import a échoué\")\n"
							"assert(scene.exists(name))"));
}

// ============================================================================
// Scénarios intégrés
// ============================================================================

TEST(Scenarios, AllAreDeclaredAndCompile) {
	std::vector<Scenario> scenarios = BuiltinScenarios();
	EXPECT_TRUE(scenarios.size() >= 5);
	for (const Scenario &scenario : scenarios) {
		EXPECT_TRUE(scenario.suggestedFrames > 0);
		EXPECT_FALSE(String(scenario.description).IsEmpty());
		auto program = data::script::Parser::Compile(StringView(scenario.source));
		if (program.IsError())
			test::ReportFailure(__FILE__, __LINE__,
								String::Format("scénario `%s` : %s", scenario.name,
											   program.Error().Format().CStr())
									.CStr());
		EXPECT_TRUE(program.IsOk());
	}
	EXPECT_TRUE(FindScenario(String("tour")).IsSome());
	EXPECT_TRUE(FindScenario(String("inexistant")).IsNone());
}

TEST(Scenarios, SmokeScenarioVisitsEverySceneWithoutError) {
	// Le scénario d'intégration continue, joué ici exactement comme le ferait
	// `--headless --scenario=smoke`.
	Harness harness;
	harness.runtime.ToolVm().SetGlobal(String("shot_dir"), data::script::Value::Str(String("/tmp")));
	Option<Scenario> smoke = FindScenario(String("smoke"));
	ASSERT_TRUE(smoke.IsSome());
	ASSERT_TRUE(harness.Run(smoke.Unwrap().source));

	for (long frame = 1; frame <= 150; ++frame) {
		harness.runtime.CallToolHook(String("on_frame"), {data::script::Value::Number(double(frame))});
		harness.runtime.Update(1.f / 60.f);
	}
	// Les `assert` du scénario remontent comme erreurs de script : zéro erreur
	// signifie que chaque scène s'est chargée et jouée.
	EXPECT_EQ(int(harness.runtime.ScriptErrorCount()), 0);
}

// ============================================================================
// Historique — annuler / rétablir
// ============================================================================

TEST(History, UndoAndRedoRestoreTheDocumentExactly) {
	Harness h;
	h.runtime.ClearHistory();
	const String name = ObjectNameAt(*h.runtime.ActiveScene(), 1);
	const math::FVector3 start = h.runtime.ActiveScene()->Find(name)->transform.position;

	EXPECT_FALSE(h.runtime.CanUndo()); // rien n'a encore été édité
	ASSERT_TRUE(h.runtime.SetPosition(name, start + math::FVector3{2.f, 0.f, 0.f}));
	ASSERT_TRUE(h.runtime.CanUndo());
	EXPECT_TRUE(h.runtime.UndoLabel().Contains(name));

	ASSERT_TRUE(h.runtime.Undo());
	EXPECT_TRUE(h.runtime.ActiveScene()->Find(name)->transform.position.Distance(start) < 1e-4f);
	ASSERT_TRUE(h.runtime.CanRedo());
	ASSERT_TRUE(h.runtime.Redo());
	EXPECT_TRUE(sdl3::Abs(h.runtime.ActiveScene()->Find(name)->transform.position.x - (start.x + 2.f)) < 1e-4f);
	// Une nouvelle édition abandonne la branche rétablissable.
	ASSERT_TRUE(h.runtime.Undo());
	ASSERT_TRUE(h.runtime.SetPosition(name, start + math::FVector3{0.f, 5.f, 0.f}));
	EXPECT_FALSE(h.runtime.CanRedo());
}

TEST(History, UndoBringsBackADeletedObjectWithItsChildren) {
	Harness h;
	h.runtime.ClearHistory();
	const size_t before = h.runtime.ActiveScene()->ObjectCount();
	const String name = ObjectNameAt(*h.runtime.ActiveScene(), 2);

	ASSERT_TRUE(h.runtime.RemoveObject(name));
	EXPECT_EQ(h.runtime.ActiveScene()->ObjectCount(), before - 1);
	ASSERT_TRUE(h.runtime.Undo());
	EXPECT_EQ(h.runtime.ActiveScene()->ObjectCount(), before);
	ASSERT_TRUE(h.runtime.ActiveScene()->Find(name) != nullptr);
	// L'objet est de nouveau instancié, pas seulement présent dans le document.
	EXPECT_EQ(h.runtime.RuntimeObjectCount(), before);
}

TEST(History, PlayModeDoesNotPolluteTheHistory) {
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene(String("Laboratoire physique")));
	h.runtime.ClearHistory();
	h.runtime.Play();
	for (int i = 0; i < 30; ++i)
		h.runtime.Update(1.f / 60.f);
	h.runtime.Stop();
	// La physique écrit dans le document à chaque image : si elle était
	// enregistrée, annuler deviendrait inutilisable.
	EXPECT_EQ(h.runtime.UndoDepth(), size_t(0));
}

TEST(History, ForgetsTheOldestStepsPastItsLimit) {
	Harness h;
	h.runtime.ClearHistory();
	const String name = ObjectNameAt(*h.runtime.ActiveScene(), 1);
	for (size_t i = 0; i < Runtime::MAX_HISTORY + 10; ++i)
		ASSERT_TRUE(h.runtime.SetPosition(name, math::FVector3{float(i), 0.f, 0.f}));
	EXPECT_EQ(h.runtime.UndoDepth(), Runtime::MAX_HISTORY);
}

// ============================================================================
// Sélection par rayon et manipulateur
// ============================================================================

TEST(Picking, ClickingTheCentreOfTheViewportSelectsWhatIsAimedAt) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveScene(), 1); // « Cube plastique », plein
	ASSERT_TRUE(h.runtime.FocusOn(name)); // la caméra vise l'objet

	const math::FRay ray = h.runtime.ViewportRay(400.f, 300.f, 800.f, 600.f);
	Option<Runtime::PickHit> hit = h.runtime.PickAt(ray);
	ASSERT_TRUE(hit.IsSome());
	EXPECT_EQ(hit.Value().name, name);
	EXPECT_TRUE(hit.Value().distance > 0.f);

	ASSERT_TRUE(h.runtime.SelectAt(ray));
	ASSERT_TRUE(h.runtime.SelectedName().IsSome());
	EXPECT_EQ(h.runtime.SelectedName().Unwrap(), name);

	// Un rayon qui part à l'opposé ne touche rien et désélectionne.
	const math::FRay away{h.runtime.ActiveCamera().position, h.runtime.ActiveCamera().position -
															   h.runtime.ActiveScene()->Find(name)->transform.position};
	EXPECT_FALSE(h.runtime.SelectAt(away));
	EXPECT_TRUE(h.runtime.SelectedName().IsNone());
}

/// La sélection teste les TRIANGLES, pas une boîte englobante : viser le trou
/// d'un tore ne le sélectionne donc pas, le rayon passe au travers.
TEST(Picking, AimingThroughTheHoleOfATorusMissesIt) {
	Harness h;
	ASSERT_TRUE(h.runtime.FocusOn(String("Tore")));
	Option<Runtime::PickHit> hit = h.runtime.PickAt(h.runtime.ViewportRay(400.f, 300.f, 800.f, 600.f));
	ASSERT_TRUE(hit.IsSome());
	EXPECT_TRUE(hit.Value().name != "Tore"); // ce qui est derrière, à travers le trou

	// Visé sur son anneau (décalé vers le haut), il est bien touché.
	Option<Runtime::PickHit> onRing = h.runtime.PickAt(h.runtime.ViewportRay(400.f, 232.f, 800.f, 600.f));
	ASSERT_TRUE(onRing.IsSome());
	EXPECT_EQ(onRing.Value().name, "Tore");
}

TEST(Picking, HiddenObjectsAreNotSelectable) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveScene(), 1);
	ASSERT_TRUE(h.runtime.FocusOn(name));
	const math::FRay ray = h.runtime.ViewportRay(400.f, 300.f, 800.f, 600.f);
	ASSERT_TRUE(h.runtime.PickAt(ray).IsSome());

	ASSERT_TRUE(h.runtime.SetVisible(name, false));
	Option<Runtime::PickHit> hit = h.runtime.PickAt(ray);
	EXPECT_TRUE(hit.IsNone() || hit.Value().name != name);
}

TEST(Gizmo, DragMovesRotatesAndScalesAlongOneAxis) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveScene(), 3);
	ASSERT_TRUE(h.runtime.Select(name));
	const TransformDesc start = h.runtime.ActiveScene()->Find(name)->transform;

	h.runtime.SetGizmoMode(Runtime::GizmoMode::TRANSLATE);
	ASSERT_TRUE(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::X, 0.f, 3.f));
	const TransformDesc moved = h.runtime.ActiveScene()->Find(name)->transform;
	EXPECT_TRUE(sdl3::Abs(moved.position.x - (start.position.x + 3.f)) < 0.05f);
	EXPECT_TRUE(sdl3::Abs(moved.position.y - start.position.y) < 1e-4f); // un seul axe bouge
	EXPECT_TRUE(sdl3::Abs(moved.position.z - start.position.z) < 1e-4f);

	h.runtime.SetGizmoMode(Runtime::GizmoMode::ROTATE);
	ASSERT_TRUE(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::Y, 0.f, 45.f));
	const TransformDesc turned = h.runtime.ActiveScene()->Find(name)->transform;
	EXPECT_TRUE(sdl3::Abs(turned.EulerDegrees().y - (start.EulerDegrees().y + 45.f)) < 1.5f);

	h.runtime.SetGizmoMode(Runtime::GizmoMode::SCALE);
	ASSERT_TRUE(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::Z, 0.f, 0.5f));
	EXPECT_TRUE(h.runtime.ActiveScene()->Find(name)->transform.scale.z > start.scale.z);
}

TEST(Gizmo, AWholeDragIsASingleUndoStep) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveScene(), 3);
	ASSERT_TRUE(h.runtime.Select(name));
	const math::FVector3 start = h.runtime.ActiveScene()->Find(name)->transform.position;
	h.runtime.ClearHistory();

	ASSERT_TRUE(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::X, 0.f, 4.f));
	// Le glissé émet une commande par pas de souris : elles doivent former UNE
	// étape, sinon annuler demanderait autant de Ctrl+Z que de pixels parcourus.
	EXPECT_EQ(h.runtime.UndoDepth(), size_t(1));
	ASSERT_TRUE(h.runtime.Undo());
	EXPECT_TRUE(h.runtime.ActiveScene()->Find(name)->transform.position.Distance(start) < 1e-4f);
}

TEST(Gizmo, SnappingAlignsOnTheGridWithoutMovingTheOtherAxes) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveScene(), 3);
	ASSERT_TRUE(h.runtime.Select(name));
	const math::FVector3 start = h.runtime.ActiveScene()->Find(name)->transform.position;

	h.runtime.SetGizmoMode(Runtime::GizmoMode::TRANSLATE);
	h.runtime.SetSnapEnabled(true);
	h.runtime.SetSnapSteps(1.f, 15.f, 0.25f);
	ASSERT_TRUE(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::Z, 0.f, 2.37f));

	const math::FVector3 end = h.runtime.ActiveScene()->Find(name)->transform.position;
	EXPECT_TRUE(sdl3::Abs(end.z - sdl3::Round(end.z)) < 1e-3f); // sur la grille
	EXPECT_TRUE(sdl3::Abs(end.x - start.x) < 1e-4f);            // les autres axes n'ont pas bougé
	EXPECT_TRUE(sdl3::Abs(end.y - start.y) < 1e-4f);

	// Rotation magnétique : multiple du pas.
	h.runtime.SetGizmoMode(Runtime::GizmoMode::ROTATE);
	ASSERT_TRUE(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::Y, 0.f, 38.f));
	const float angle = h.runtime.ActiveScene()->Find(name)->transform.EulerDegrees().y;
	EXPECT_TRUE(sdl3::Abs(angle / 15.f - sdl3::Round(angle / 15.f)) < 1e-3f);
}

TEST(Gizmo, AxisPickingRequiresAimingAtTheHandle) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveScene(), 3);
	ASSERT_TRUE(h.runtime.Select(name));
	h.runtime.SetGizmoMode(Runtime::GizmoMode::TRANSLATE);
	const math::FVector3 origin = h.runtime.ActiveScene()->Find(name)->transform.position;
	const float size = h.runtime.GizmoSize();
	EXPECT_TRUE(size > 0.f);

	// Rayon qui traverse le milieu de la flèche X : attrapé.
	const math::FVector3 onAxis = origin + math::FVector3{size * 0.6f, 0.f, 0.f};
	const math::FRay hitRay{onAxis + math::FVector3{0.f, 4.f, 0.f}, {0.f, -1.f, 0.f}};
	EXPECT_TRUE(h.runtime.PickGizmoAxis(hitRay) == Runtime::GizmoAxis::X);

	// Rayon qui passe loin à côté : rien.
	const math::FRay missRay{origin + math::FVector3{0.f, 4.f, size * 6.f}, {0.f, -1.f, 0.f}};
	EXPECT_TRUE(h.runtime.PickGizmoAxis(missRay) == Runtime::GizmoAxis::NONE);

	// Sans sélection, il n'y a pas de manipulateur à attraper.
	h.runtime.ClearSelection();
	EXPECT_TRUE(h.runtime.PickGizmoAxis(hitRay) == Runtime::GizmoAxis::NONE);
}

TEST(Gizmo, ScriptsDriveSelectionAndTheGizmo) {
	Harness h;
	ASSERT_TRUE(h.Run(R"SLED(
        editor.select("Tore")
        editor.focus("Tore")
        assert(editor.gizmo_mode("rotate") == "rotate", "mode non appliqué")
        let avant = object.rotation("Tore")
        assert(editor.gizmo_drag("y", 0, 90), "glissé refusé")
        let apres = object.rotation("Tore")
        assert(apres[1] > avant[1] + 80, "rotation insuffisante")
        assert(editor.undo(), "rien à annuler")
        let retour = object.rotation("Tore")
        assert(retour[1] < avant[1] + 0.01, "l'annulation n'a pas rendu l'angle d'origine")
        let touche = editor.select_at(400, 300, 800, 600)
        assert(touche != nil, "aucun objet sous le centre du viewport")
    )SLED"));
}

// ============================================================================
// Caméra — navigation libre dans la vue
// ============================================================================

TEST(Camera, DollyMovesAlongTheViewAxisOnly) {
	Harness h;
	const math::FVector3 start = h.runtime.ActiveCamera().position;
	const math::FVector3 forward = h.runtime.CameraForward();

	h.runtime.DollyCamera(5.f);
	const math::FVector3 end = h.runtime.ActiveCamera().position;
	// Exactement 5 unités, exactement sur l'axe de visée.
	EXPECT_TRUE(sdl3::Abs((end - start).Length() - 5.f) < 1e-3f);
	EXPECT_TRUE((end - (start + forward * 5.f)).Length() < 1e-3f);
	// La direction de visée, elle, n'a pas bougé.
	EXPECT_TRUE((h.runtime.CameraForward() - forward).Length() < 1e-5f);

	h.runtime.DollyCamera(-5.f);
	EXPECT_TRUE((h.runtime.ActiveCamera().position - start).Length() < 1e-3f);
}

TEST(Camera, PanSlidesInTheScreenPlaneNotAlongTheView) {
	Harness h;
	const math::FVector3 start = h.runtime.ActiveCamera().position;
	const math::FVector3 forward = h.runtime.CameraForward();
	const math::FVector3 right = h.runtime.CameraRight();
	const math::FVector3 up = h.runtime.CameraUp();

	h.runtime.PanCamera(3.f, 2.f);
	const math::FVector3 delta = h.runtime.ActiveCamera().position - start;
	EXPECT_TRUE((delta - (right * 3.f + up * 2.f)).Length() < 1e-3f);
	// Rien le long de la visée : un panoramique ne rapproche pas du décor.
	EXPECT_TRUE(sdl3::Abs(delta.Dot(forward)) < 1e-3f);
	// Les axes sont orthonormés : le repère de la caméra est cohérent.
	EXPECT_TRUE(sdl3::Abs(right.Dot(up)) < 1e-5f);
	EXPECT_TRUE(sdl3::Abs(right.Dot(forward)) < 1e-5f);
	EXPECT_TRUE(sdl3::Abs(up.Length() - 1.f) < 1e-5f);
}

TEST(Camera, OrbitKeepsTheDistanceAndKeepsLookingAtThePivot) {
	Harness h;
	ASSERT_TRUE(h.runtime.Select(String("Cristal")));
	ASSERT_TRUE(h.runtime.FocusOn(String("Cristal")));
	const math::FVector3 pivot = h.runtime.CameraPivot();
	const float radius = (h.runtime.ActiveCamera().position - pivot).Length();
	const math::FVector3 start = h.runtime.ActiveCamera().position;

	for (int i = 0; i < 12; ++i)
		h.runtime.OrbitCamera(0.13f, 0.f, pivot);

	const math::FVector3 end = h.runtime.ActiveCamera().position;
	EXPECT_TRUE((end - start).Length() > 1.f);                          // la caméra a tourné
	EXPECT_TRUE(sdl3::Abs((end - pivot).Length() - radius) < 1e-2f);    // sans s'éloigner
	// … et elle regarde toujours le pivot.
	const math::FVector3 toPivot = (pivot - end).Normalize();
	EXPECT_TRUE((h.runtime.CameraForward() - toPivot).Length() < 1e-3f);
}

TEST(Camera, OrbitDownRaisesTheCameraAndCannotFlipOver) {
	Harness h;
	ASSERT_TRUE(h.runtime.Select(String("Cristal")));
	ASSERT_TRUE(h.runtime.FocusOn(String("Cristal")));
	const math::FVector3 pivot = h.runtime.CameraPivot();
	const float startHeight = h.runtime.ActiveCamera().position.y;
	const float radius = (h.runtime.ActiveCamera().position - pivot).Length();

	// Tangage négatif = vue plongeante : la caméra monte au-dessus du pivot.
	for (int i = 0; i < 8; ++i)
		h.runtime.OrbitCamera(0.f, -0.09f, pivot);
	EXPECT_TRUE(h.runtime.ActiveCamera().position.y > startHeight + 1.f);
	EXPECT_TRUE(h.runtime.CameraForward().y < 0.f); // elle regarde vers le bas

	// Bornes : même en insistant, la vue ne passe jamais la verticale
	// (au-delà, elle se retournerait), et la distance au pivot tient.
	for (int i = 0; i < 200; ++i)
		h.runtime.OrbitCamera(0.f, -0.2f, pivot);
	EXPECT_TRUE(h.runtime.CameraUp().y > 0.f);
	EXPECT_TRUE(h.runtime.CameraForward().y < 0.f);
	EXPECT_TRUE(sdl3::Abs((h.runtime.ActiveCamera().position - pivot).Length() - radius) < 1e-2f);
}

TEST(Camera, PivotIsTheSelectionOtherwiseWhatIsInFront) {
	Harness h;
	ASSERT_TRUE(h.runtime.Select(String("Cristal")));
	const math::FVector3 selected = h.runtime.ActiveScene()->Find(String("Cristal"))->transform.position;
	EXPECT_TRUE((h.runtime.CameraPivot() - selected).Length() < 1e-4f);

	h.runtime.ClearSelection();
	// Sans sélection : un point droit devant, donc sur l'axe de visée.
	const math::FVector3 pivot = h.runtime.CameraPivot();
	const math::FVector3 toPivot = pivot - h.runtime.ActiveCamera().position;
	EXPECT_TRUE((toPivot.Normalize() - h.runtime.CameraForward()).Length() < 1e-3f);
	EXPECT_TRUE(h.runtime.PivotDistance() > 0.5f);
}

TEST(Camera, FlyMovesForwardRightAndAlongWorldUp) {
	Harness h;
	const math::FVector3 start = h.runtime.ActiveCamera().position;
	const math::FVector3 forward = h.runtime.CameraForward();
	const math::FVector3 right = h.runtime.CameraRight();

	h.runtime.MoveCamera({2.f, 3.f, 4.f});
	const math::FVector3 delta = h.runtime.ActiveCamera().position - start;
	// `y` suit le HAUT DU MONDE (monter reste monter, même en piquant du nez).
	EXPECT_TRUE((delta - (forward * 4.f + right * 2.f + math::FVector3{0.f, 3.f, 0.f})).Length() < 1e-3f);
}

TEST(Camera, ScriptsDriveEveryCameraMove) {
	Harness h;
	ASSERT_TRUE(h.Run(R"SLED(
        editor.select("Cristal")
        editor.focus("Cristal")
        let pivot = camera.pivot()
        let depart = camera.position()

        fn distance(a, b) {
            let dx = a[0] - b[0]
            let dy = a[1] - b[1]
            let dz = a[2] - b[2]
            return math.sqrt(dx * dx + dy * dy + dz * dz)
        }

        let rayon = distance(depart, pivot)
        for (i in range(0, 10)) { camera.orbit(0.15, 0) }
        assert(distance(camera.position(), depart) > 1, "l'orbite n'a pas bougé la caméra")
        assert(math.abs(distance(camera.position(), pivot) - rayon) < 0.1, "l'orbite a changé la distance au pivot")

        let avant = camera.position()
        camera.dolly(-4)
        assert(math.abs(distance(camera.position(), avant) - 4) < 0.01, "la molette n'a pas reculé de 4 unités")

        avant = camera.position()
        camera.pan(2, 1)
        assert(distance(camera.position(), avant) > 1, "le panoramique n'a pas bougé la caméra")
    )SLED"));
}

// ============================================================================
// Rapport d'exécution
// ============================================================================

TEST(Report, FrameStatsExcludeTheFirstFrame) {
	FrameStats stats;
	stats.Push(0.5); // première image (compilation des pipelines) : hors extrêmes
	stats.Push(1.0 / 60.0);
	stats.Push(1.0 / 30.0);
	stats.Push(1.0 / 60.0);

	EXPECT_EQ(stats.Count(), 4L);
	EXPECT_TRUE(sdl3::Abs(stats.FirstFrameSeconds() - 0.5) < 1e-9);
	// Image la plus LONGUE (1/30 s) => cadence MINIMALE (30 img/s).
	EXPECT_TRUE(sdl3::Abs(stats.MinFps() - 30.0) < 0.01);
	EXPECT_TRUE(sdl3::Abs(stats.MaxFps() - 60.0) < 0.01);
	EXPECT_TRUE(stats.AverageFps() > 30.0 && stats.AverageFps() < 60.0);
	EXPECT_TRUE(stats.PercentileMs(99.0) >= stats.PercentileMs(50.0));
}

TEST(Report, ThreadTrackerCountsTheConcurrentPeak) {
	ThreadTracker tracker;
	EXPECT_EQ(tracker.Peak(), 1); // le fil principal compte pour 1
	{
		ThreadTracker::Scope first(tracker);
		EXPECT_EQ(tracker.Live(), 2);
		{
			ThreadTracker::Scope second(tracker);
			EXPECT_EQ(tracker.Live(), 3);
		}
		EXPECT_EQ(tracker.Live(), 2);
	}
	EXPECT_EQ(tracker.Live(), 1);
	EXPECT_EQ(tracker.Peak(), 3);       // le maximum SIMULTANÉ est retenu
	EXPECT_EQ(tracker.TotalStarted(), 3);
}

TEST(Report, TextAndJsonCarryTheSameFacts) {
	Project project = LoadDemoProject();
	RunReport report;
	report.commandLine = "game_editor_demo --scenario=tour";
	report.scenario = "tour";
	report.mode = "fenêtré";
	report.randomSeed = 42;
	report.completed = true;
	report.frames.Push(0.4);
	report.frames.Push(1.0 / 60.0);
	report.frames.Push(1.0 / 45.0);
	report.peakThreads = 2;
	report.totalThreads = 2;
	report.scriptRuns = 3;
	report.screenshots.push_back(ScreenshotRecord{10, String("captures/a.png"), true, String()});
	report.playSessions.push_back(PlaySessionRecord{String("Circuit"), 20, 200, 3.0});
	report.gameplayCounters.emplace_back(String("total_laps"), String("3"));

	String text = report.ToText(project);
	EXPECT_TRUE(text.Contains("RAPPORT D'EXÉCUTION"));
	EXPECT_TRUE(text.Contains("Vitrine"));
	EXPECT_TRUE(text.Contains("Circuit"));
	EXPECT_TRUE(text.Contains("captures/a.png"));
	EXPECT_TRUE(text.Contains("total_laps"));
	EXPECT_TRUE(text.Contains("Fils simultanés"));

	// Le JSON doit être RELISIBLE : c'est ce qu'un test d'intégration
	// interroge.
	String json = report.ToJson(project);
	data::JsonDocument document;
	auto error = document.DecodeStr(json);
	ASSERT_TRUE(error.IsNone());
	data::NodePtr root = document.GetRoot();
	ASSERT_TRUE(root && root->IsObject());
	EXPECT_EQ(root->Get("format")->stringValue, "game_editor.report");
	EXPECT_TRUE(root->Get("run")->Get("completed")->boolValue);
	EXPECT_TRUE(root->Get("performance")->Get("threads_peak_concurrent")->intValue == 2);
	EXPECT_TRUE(root->Get("project")->Get("scene_count")->intValue == 5);
	EXPECT_TRUE(root->Get("screenshots")->GetSize() == 1);
	EXPECT_TRUE(root->Get("play_sessions")->At(0)->Get("scene")->stringValue == "Circuit");
	// Les objets de chaque scène sont listés, avec leur position.
	data::NodePtr firstScene = root->Get("project")->Get("scenes")->At(0);
	ASSERT_TRUE(firstScene && firstScene->Get("objects")->GetSize() > 0);
	EXPECT_TRUE(firstScene->Get("objects")->At(0)->Get("position")->GetSize() == 3);
}

TEST(Report, CollectsWarningsAndErrorsFromTheEditorLog) {
	Harness harness;
	harness.runtime.LogWarning(String("attention"));
	harness.runtime.LogError(String("échec"));
	harness.runtime.LogInfo(String("juste une info"));

	RunReport report;
	report.CollectDiagnostics(harness.runtime);
	ASSERT_TRUE(report.warnings.size() == 1);
	ASSERT_TRUE(report.errors.size() == 1);
	EXPECT_TRUE(report.warnings[0].Contains("attention"));
	EXPECT_TRUE(report.errors[0].Contains("échec"));
}

// ============================================================================
// Hiérarchie de nœuds — l'éditeur au-dessus de scene::NodeTree
// ============================================================================

TEST(Hierarchy, GroupsCanBeBuiltAndChildrenFollowTheirParent) {
	Harness h;
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Voiture"));
	ASSERT_TRUE(group.IsSome());

	ObjectDesc wheel;
	wheel.name = "Roue";
	wheel.shape = ShapeKind::CYLINDER;
	wheel.transform.position = {1.f, 0.f, 0.f};
	Option<scene::NodeId> child = h.runtime.SpawnNode(wheel, group.Unwrap());
	ASSERT_TRUE(child.IsSome());

	const SceneDesc *scene = h.runtime.ActiveScene();
	EXPECT_TRUE(scene->tree.ParentOf(child.Unwrap()) == group.Unwrap());
	EXPECT_TRUE(scene->tree.PathOf(child.Unwrap()).EndsWith("/Voiture/Roue"));

	// Déplacer le PARENT déplace l'enfant dans le monde, sans toucher à son
	// transform local — c'est tout l'intérêt de la hiérarchie.
	ASSERT_TRUE(h.runtime.SetPosition(group.Unwrap(), {10.f, 0.f, 5.f}));
	EXPECT_TRUE((scene->tree.GlobalPosition(child.Unwrap()) - math::FVector3{11.f, 0.f, 5.f}).Length() < 1e-4f);
	EXPECT_TRUE(scene->tree.Get(child.Unwrap())->transform.position.x == 1.f);

	// Et le nœud de RENDU suit. La synchronisation ECS -> Object3D a lieu
	// une fois par image (cf. Runtime::Update) et non à chaque commande : on
	// avance donc d'une image avant de lire la matrice monde du nœud 3D.
	h.runtime.Update(1.f / 60.f);
	render3d::Object3D *object3d = h.runtime.FindNode(child.Unwrap());
	ASSERT_TRUE(object3d != nullptr);
	math::FMatrix4 world = object3d->WorldMatrix();
	EXPECT_TRUE(sdl3::Abs(world.m[12] - 11.f) < 1e-3f);
	EXPECT_TRUE(sdl3::Abs(world.m[14] - 5.f) < 1e-3f);
}

TEST(Hierarchy, ReparentKeepsTheObjectWhereItIsOnScreen) {
	Harness h;
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Groupe"));
	ASSERT_TRUE(group.IsSome());
	ASSERT_TRUE(h.runtime.SetPosition(group.Unwrap(), {20.f, 3.f, -7.f}));

	scene::NodeId cube = h.runtime.ResolveId(String("Cube plastique"));
	ASSERT_TRUE(cube.Valid());
	const SceneDesc *scene = h.runtime.ActiveScene();
	const math::FVector3 before = scene->tree.GlobalPosition(cube);

	ASSERT_TRUE(h.runtime.ReparentNode(cube, group.Unwrap()));
	EXPECT_TRUE(scene->tree.ParentOf(cube) == group.Unwrap());
	EXPECT_TRUE((scene->tree.GlobalPosition(cube) - before).Length() < 1e-3f);

	// En mode « local », le nœud SUIT son nouveau parent (utile pour poser
	// une roue à la même place relative dans chaque voiture).
	scene::NodeId torus = h.runtime.ResolveId(String("Tore"));
	ASSERT_TRUE(h.runtime.ReparentNode(torus, group.Unwrap(), scene::ReparentMode::KEEP_LOCAL));
	EXPECT_TRUE((scene->tree.GlobalPosition(torus) - scene->tree.Get(torus)->transform.position).Length() > 1.f);
}

TEST(Hierarchy, ACycleIsRefusedAndLeavesNoUndoStep) {
	Harness h;
	Option<scene::NodeId> parent = h.runtime.CreateGroup(String("Parent"));
	ASSERT_TRUE(parent.IsSome());
	Option<scene::NodeId> child = h.runtime.CreateGroup(String("Enfant"), parent.Unwrap());
	ASSERT_TRUE(child.IsSome());

	const size_t undoBefore = h.runtime.UndoDepth();
	EXPECT_FALSE(h.runtime.ReparentNode(parent.Unwrap(), child.Unwrap()));
	// Refusé ET sans trace : une étape d'annulation qui ne défait rien
	// désoriente plus qu'elle n'aide.
	EXPECT_EQ(h.runtime.UndoDepth(), undoBefore);
	EXPECT_TRUE(h.runtime.ActiveScene()->tree.ParentOf(child.Unwrap()) == parent.Unwrap());
	EXPECT_TRUE(h.runtime.ActiveScene()->tree.Validate().Ok());
}

TEST(Hierarchy, DeletingAParentDeletesItsWholeSubtree) {
	Harness h;
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Véhicule"));
	ASSERT_TRUE(group.IsSome());
	ObjectDesc part;
	part.name = "Carrosserie";
	Option<scene::NodeId> body = h.runtime.SpawnNode(part, group.Unwrap());
	ASSERT_TRUE(body.IsSome());
	part.name = "Phare";
	Option<scene::NodeId> light = h.runtime.SpawnNode(part, body.Unwrap());
	ASSERT_TRUE(light.IsSome());

	const size_t before = h.runtime.ActiveScene()->ObjectCount();
	ASSERT_TRUE(h.runtime.RemoveNode(group.Unwrap()));
	EXPECT_EQ(h.runtime.ActiveScene()->ObjectCount(), before - 3);
	EXPECT_TRUE(h.runtime.ActiveScene()->Find("Phare") == nullptr);
	// Le runtime aussi : pas d'entité ni de nœud 3D orphelin.
	EXPECT_EQ(h.runtime.RuntimeObjectCount(), h.runtime.ActiveScene()->ObjectCount());

	// …et l'annulation restitue l'arbre ENTIER, petits-enfants compris.
	ASSERT_TRUE(h.runtime.Undo());
	EXPECT_EQ(h.runtime.ActiveScene()->ObjectCount(), before);
	EXPECT_TRUE(h.runtime.ActiveScene()->Find("Phare") != nullptr);
	EXPECT_TRUE(h.runtime.ActiveScene()->tree.Validate().Ok());
}

TEST(Hierarchy, DuplicateCopiesTheWholeSubtreeWithUniqueNames) {
	Harness h;
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Voiture"));
	ASSERT_TRUE(group.IsSome());
	ObjectDesc wheel;
	wheel.name = "Roue";
	ASSERT_TRUE(h.runtime.SpawnNode(wheel, group.Unwrap()).IsSome());

	const size_t before = h.runtime.ActiveScene()->ObjectCount();
	Option<scene::NodeId> copy = h.runtime.DuplicateNode(group.Unwrap());
	ASSERT_TRUE(copy.IsSome());
	const SceneDesc *scene = h.runtime.ActiveScene();
	EXPECT_EQ(scene->ObjectCount(), before + 2); // le groupe ET sa roue
	EXPECT_TRUE(scene->tree.ChildrenOf(copy.Unwrap()).size() == 1);
	// Les noms restent uniques dans la scène (les scripts désignent par nom).
	EXPECT_TRUE(scene->tree.Get(copy.Unwrap())->name != String("Voiture"));
	EXPECT_TRUE(scene->tree.Validate().Ok());

	ASSERT_TRUE(h.runtime.Undo());
	EXPECT_EQ(h.runtime.ActiveScene()->ObjectCount(), before);
}

TEST(Hierarchy, AParentedBodyIsSimulatedAtItsWorldPosition) {
	// Le bug que la hiérarchie rend possible : la physique travaille en
	// MONDE, le document en LOCAL. Un corps enfant doit donc être créé à sa
	// position monde, et la simulation recopiée en local.
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Laboratoire physique"));
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Support"));
	ASSERT_TRUE(group.IsSome());
	ASSERT_TRUE(h.runtime.SetPosition(group.Unwrap(), {0.f, 12.f, 0.f}));

	ObjectDesc crate;
	crate.name = "Caisse suspendue";
	crate.physics.body = BodyKind::DYNAMIC;
	crate.physics.mass = 2.f;
	crate.transform.position = {0.f, 0.f, 0.f}; // local : à l'origine DU SUPPORT
	Option<scene::NodeId> id = h.runtime.SpawnNode(crate, group.Unwrap());
	ASSERT_TRUE(id.IsSome());

	const SceneDesc *scene = h.runtime.ActiveScene();
	EXPECT_TRUE(sdl3::Abs(scene->tree.GlobalPosition(id.Unwrap()).y - 12.f) < 1e-4f);

	h.runtime.Play();
	for (int i = 0; i < 30; ++i)
		h.runtime.Update(1.f / 60.f);

	// La caisse est tombée : sa position MONDE a baissé, et son local aussi
	// (le support, lui, n'a pas bougé).
	const float world = scene->tree.GlobalPosition(id.Unwrap()).y;
	const float local = scene->tree.Get(id.Unwrap())->transform.position.y;
	EXPECT_TRUE(world < 12.f);
	EXPECT_TRUE(local < 0.f);
	// Et la conversion est cohérente : monde = parent + local.
	EXPECT_TRUE(sdl3::Abs(world - (12.f + local)) < 1e-3f);
	h.runtime.Stop();
}

TEST(Hierarchy, LegacyFlatProjectsAreConvertedIntoATree) {
	// Un projet au format 2 (liste plate + champ `parent` portant un nom)
	// doit continuer de s'ouvrir, et donner un vrai arbre.
	String legacy = String(
		"{\"format\":\"game_editor.project\",\"version\":2,\"name\":\"Ancien\",\"active_scene\":\"S\","
		"\"scenes\":[{\"name\":\"S\",\"objects\":["
		"{\"name\":\"Voiture\",\"shape\":\"box\",\"transform\":{\"position\":[10,0,0]}},"
		"{\"name\":\"Roue\",\"parent\":\"Voiture\",\"shape\":\"cylinder\","
		"\"transform\":{\"position\":[1,0,0]}}]}]}");
	auto project = Project::DecodeJson(legacy);
	ASSERT_TRUE(project.IsOk());
	const SceneDesc &scene = project.Value().scenes[0];
	EXPECT_EQ(scene.ObjectCount(), size_t(2));

	scene::NodeId car = scene.FindId(String("Voiture"));
	scene::NodeId wheel = scene.FindId(String("Roue"));
	ASSERT_TRUE(car.Valid() && wheel.Valid());
	EXPECT_TRUE(scene.tree.ParentOf(wheel) == car);
	// Le transform de l'ancien format était DÉJÀ relatif au parent : il ne
	// doit surtout pas être recalculé à l'import.
	EXPECT_TRUE(scene.tree.Get(wheel)->transform.position.x == 1.f);
	EXPECT_TRUE(sdl3::Abs(scene.tree.GlobalPosition(wheel).x - 11.f) < 1e-4f);
	EXPECT_TRUE(scene.tree.Validate().Ok());
}

TEST(Hierarchy, SavingAndReloadingKeepsTheTreeAndTheComponents) {
	Harness h;
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Ensemble"));
	ASSERT_TRUE(group.IsSome());
	ObjectDesc part;
	part.name = "Pièce";
	part.shape = ShapeKind::TORUS;
	part.material.kind = MaterialKind::METAL;
	part.physics.body = BodyKind::STATIC;
	part.tag = "pièce";
	ASSERT_TRUE(h.runtime.SpawnNode(part, group.Unwrap()).IsSome());
	h.runtime.GetProject().scenes[0].tree.Get(group.Unwrap())
		->Set(String("max_speed"), scene::PropertyValue::Float(180.0));

	auto reloaded = Project::DecodeJson(h.runtime.GetProject().EncodeJson());
	ASSERT_TRUE(reloaded.IsOk());
	const SceneDesc *scene = reloaded.Value().FindScene(h.runtime.ActiveScene()->name);
	ASSERT_TRUE(scene != nullptr);

	scene::NodeId reloadedGroup = scene->FindId(String("Ensemble"));
	scene::NodeId reloadedPart = scene->FindId(String("Pièce"));
	ASSERT_TRUE(reloadedGroup.Valid() && reloadedPart.Valid());
	EXPECT_TRUE(scene->tree.ParentOf(reloadedPart) == reloadedGroup);
	const scene::Node *node = scene->tree.Get(reloadedPart);
	EXPECT_TRUE(VisualDesc::Read(*node).shape == ShapeKind::TORUS);
	EXPECT_TRUE(VisualDesc::Read(*node).material.kind == MaterialKind::METAL);
	EXPECT_TRUE(PhysicsDesc::Read(*node).body == BodyKind::STATIC);
	EXPECT_EQ(TagOf(*node), "pièce");
	// La propriété LIBRE survit elle aussi.
	EXPECT_TRUE(sdl3::Abs(scene->tree.Get(reloadedGroup)->Get(String("max_speed"))->AsFloat() - 180.f) < 1e-3f);
}

TEST(Hierarchy, ScriptsNavigateAndReshapeTheTree) {
	Harness h;
	ASSERT_TRUE(h.Run("node.create_group(\"Voiture\")\n"
					  "node.create_group(\"Roues\", \"Voiture\")\n"
					  "assert(node.parent(\"Roues\") == \"Voiture\")\n"
					  "assert(node.path(\"Roues\") != nil)\n"
					  "node.reparent(\"Tore\", \"Roues\")\n"
					  "assert(node.parent(\"Tore\") == \"Roues\")\n"
					  "let kids = node.children(\"Roues\")\n"
					  "assert(len(kids) == 1)\n"
					  "assert(node.find(\"../..\", \"Tore\") == \"Voiture\")\n"
					  "node.prop(\"Voiture\", \"max_speed\", 180)\n"
					  "assert(node.prop(\"Voiture\", \"max_speed\") == 180)\n"
					  "let copie = node.duplicate(\"Voiture\")\n"
					  "assert(copie != nil and copie != \"Voiture\")"));

	const SceneDesc *scene = h.runtime.ActiveScene();
	EXPECT_TRUE(scene->FindId(String("Voiture")).Valid());
	EXPECT_TRUE(scene->tree.Validate().Ok());
	// Le reparentage garde la position monde par défaut.
	scene::NodeId torus = scene->FindId(String("Tore"));
	ASSERT_TRUE(torus.Valid());
	EXPECT_TRUE(scene->tree.ParentOf(torus) == scene->FindId(String("Roues")));
}

TEST(Hierarchy, ASubtreeCanBeSavedAsASceneAndInstantiatedSeveralTimes) {
	Harness h;
	Option<scene::NodeId> car = h.runtime.CreateGroup(String("Modèle"));
	ASSERT_TRUE(car.IsSome());
	ObjectDesc part;
	part.name = "Roue";
	part.transform.position = {1.f, 0.f, 0.f};
	ASSERT_TRUE(h.runtime.SpawnNode(part, car.Unwrap()).IsSome());
	ASSERT_TRUE(h.runtime.SetPosition(car.Unwrap(), {5.f, 0.f, 0.f}));

	const String path = String("/tmp/game_editor_modele.scene");
	auto saved = h.runtime.SavePackedScene(car.Unwrap(), path);
	ASSERT_TRUE(saved.IsOk());

	const size_t before = h.runtime.ActiveScene()->ObjectCount();
	auto first = h.runtime.InstantiateSceneFile(path);
	auto second = h.runtime.InstantiateSceneFile(path);
	ASSERT_TRUE(first.IsOk() && second.IsOk());

	const SceneDesc *scene = h.runtime.ActiveScene();
	EXPECT_EQ(scene->ObjectCount(), before + 4); // deux instances de 2 nœuds
	EXPECT_TRUE(scene->tree.Get(first.Value())->name != scene->tree.Get(second.Value())->name);
	// Chaque instance sait d'où elle vient…
	EXPECT_TRUE(scene::PackedScene::InstanceSource(*scene->tree.Get(first.Value())) == path);
	// …et elles sont indépendantes : bouger l'une ne bouge pas l'autre.
	ASSERT_TRUE(h.runtime.SetPosition(first.Value(), {50.f, 0.f, 0.f}));
	scene::NodeId wheelA = scene->tree.Resolve(String("Roue"), first.Value());
	scene::NodeId wheelB = scene->tree.Resolve(String("Roue"), second.Value());
	ASSERT_TRUE(wheelA.Valid() && wheelB.Valid());
	EXPECT_TRUE(sdl3::Abs(scene->tree.GlobalPosition(wheelA).x - 51.f) < 1e-3f);
	EXPECT_TRUE(sdl3::Abs(scene->tree.GlobalPosition(wheelB).x - 6.f) < 1e-3f);
	EXPECT_TRUE(scene->tree.Validate().Ok());

	// L'instanciation est annulable comme le reste. La dernière commande
	// était le DÉPLACEMENT ci-dessus : on l'annule d'abord, puis la seconde
	// instanciation — l'historique empile bien chaque geste séparément.
	ASSERT_TRUE(h.runtime.Undo());
	EXPECT_TRUE(sdl3::Abs(scene->tree.GlobalPosition(wheelA).x - 6.f) < 1e-3f);
	ASSERT_TRUE(h.runtime.Undo());
	EXPECT_EQ(h.runtime.ActiveScene()->ObjectCount(), before + 2);
}

TEST(Content, TheAssemblySceneShowsRealHierarchies) {
	// La scène livrée doit VRAIMENT être profonde — sinon elle ne démontre
	// rien de ce que la hiérarchie apporte.
	Project project = LoadDemoProject();
	const SceneDesc *scene = project.FindScene("Assemblages");
	ASSERT_TRUE(scene != nullptr);
	EXPECT_TRUE(scene->tree.Validate().Ok());

	scene::NodeId eye = scene->tree.Resolve(String("/Assemblages/Robot/Corps/Tête/Œil"));
	ASSERT_TRUE(eye.Valid());
	EXPECT_EQ(scene->tree.DepthOf(eye), size_t(4));

	// Le même nom « Tête » existe deux fois dans la scène, à des endroits
	// différents : c'est le chemin qui les distingue.
	scene::NodeId robotHead = scene->tree.Resolve(String("/Assemblages/Robot/Corps/Tête"));
	scene::NodeId charHead = scene->tree.Resolve(String("/Assemblages/Personnage/Visuel/Corps/Tête"));
	ASSERT_TRUE(robotHead.Valid() && charHead.Valid());
	EXPECT_TRUE(robotHead != charHead);

	// Une main est bien emportée par le corps du robot : sa position monde
	// tient compte de tous les maillons intermédiaires.
	scene::NodeId hand = scene->tree.Resolve(String("/Assemblages/Robot/Corps/BrasDroit/Bras/Avant-bras/Main"));
	ASSERT_TRUE(hand.Valid());
	const math::FVector3 world = scene->tree.GlobalPosition(hand);
	EXPECT_TRUE(sdl3::Abs(world.x - (-7.f + 1.2f)) < 1e-3f); // robot + épaule
	EXPECT_TRUE(world.y > 0.f && world.y < 3.f);             // le long du bras

	// Les quatre assemblages demandés sont là, et chacun a des enfants.
	for (const char *name : {"Robot", "Véhicule", "Personnage", "Bâtiment"}) {
		scene::NodeId id = scene->tree.FindChild(scene->tree.Root(), String(name));
		ASSERT_TRUE(id.Valid());
		EXPECT_TRUE(!scene->tree.ChildrenOf(id).empty());
	}
}

TEST(Content, MovingAnAssemblyRootMovesItsWholeSubtree) {
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Assemblages"));
	const SceneDesc *scene = h.runtime.ActiveScene();
	scene::NodeId robot = scene->tree.FindChild(scene->tree.Root(), String("Robot"));
	scene::NodeId hand = scene->tree.Resolve(String("/Assemblages/Robot/Corps/BrasDroit/Bras/Avant-bras/Main"));
	ASSERT_TRUE(robot.Valid() && hand.Valid());

	const math::FVector3 before = scene->tree.GlobalPosition(hand);
	ASSERT_TRUE(h.runtime.SetPosition(robot, {20.f, 0.f, 0.f}));
	const math::FVector3 after = scene->tree.GlobalPosition(hand);
	EXPECT_TRUE(sdl3::Abs((after.x - before.x) - 27.f) < 1e-3f); // -7 -> +20
	EXPECT_TRUE(sdl3::Abs(after.y - before.y) < 1e-4f);
}

// ============================================================================
// Donjon : lumières, caméras, déclencheurs, scripts de nœud
// ============================================================================

namespace {

/// Nombre de nœuds de la scène active portant ce type.
int CountType(const SceneDesc &scene, const char *type) {
	int count = 0;
	for (scene::NodeId id : scene.Objects())
		if (scene.tree.Get(id)->type == type)
			++count;
	return count;
}

const scene::Node *Node(Harness &h, const char *name) { return h.runtime.ActiveScene()->Find(String(name)); }

void Simulate(Harness &h, int frames) {
	for (int i = 0; i < frames; ++i)
		h.runtime.Update(1.f / 60.f);
}

} // namespace

TEST(Dungeon, IsOrganisedInFoldersWithLightsCameraAndTriggers) {
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Donjon"));
	const SceneDesc &scene = *h.runtime.ActiveScene();
	EXPECT_EQ(CountType(scene, node_kind::FOLDER), 4); // Environnement, Objets dynamiques, Déclencheurs, Chemin
	EXPECT_EQ(CountType(scene, node_kind::LIGHT), 5);
	EXPECT_EQ(CountType(scene, node_kind::TRIGGER), 3);
	EXPECT_EQ(CountType(scene, node_kind::CAMERA), 1);
	// Les chemins lisent comme la maquette : Environnement > Torches > Torche 1.
	EXPECT_TRUE(scene.tree.Resolve(String("/Donjon/Environnement/Torches/Torche 1")).Valid());
	EXPECT_TRUE(scene.tree.Resolve(String("/Donjon/Objets dynamiques/Coffre/Couvercle/Planche")).Valid());
	EXPECT_TRUE(scene.tree.Resolve(String("/Donjon/Déclencheurs/Zone du coffre")).Valid());

	// Tous les scripts attachés existent dans la bibliothèque et compilent.
	for (const ScriptAsset &script : h.runtime.GetProject().scripts)
		EXPECT_TRUE(Runtime::CheckScript(script.source).IsNone());
	for (scene::NodeId id : scene.Objects())
		if (ScriptRef::Has(*scene.tree.Get(id)))
			EXPECT_TRUE(h.runtime.GetProject().FindScript(ScriptRef::Read(*scene.tree.Get(id)).script) != nullptr);
}

TEST(Dungeon, TorchesBecomePointLightsAtTheirWorldPosition) {
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Donjon"));
	Simulate(h, 1);
	ASSERT_TRUE(h.runtime.PointLights().size() == 5u);
	const scene::NodeId torch = h.runtime.ResolveId(String("Torche 1"));
	const math::FVector3 world = h.runtime.ActiveScene()->tree.GlobalPosition(torch);
	const render3d::PointLight &light = h.runtime.PointLights().front();
	EXPECT_TRUE((light.position - world).Length() < 1e-4f);
	EXPECT_TRUE(light.intensity == 3.2f);
	EXPECT_TRUE(light.distance == 11.f);

	// Masquer le dossier « Torches » éteint les cinq (visibilité HÉRITÉE).
	ASSERT_TRUE(h.runtime.SetVisible(h.runtime.ResolveId(String("Torches")), false));
	Simulate(h, 1);
	EXPECT_TRUE(h.runtime.PointLights().empty());
}

TEST(Dungeon, TheGuidedTourOpensTheChestThenTheDoorAndReachesTheExit) {
	// L'essai de jouabilité du donjon, sans clavier ni souris : le script
	// `player` suit les étapes, les zones de déclenchement réveillent le
	// script de scène, qui pose des propriétés que les scripts du coffre et
	// de la porte lisent. Toute la chaîne est donc exercée.
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Donjon"));
	const math::FVector3 doorBefore = Node(h, "Porte")->transform.position;
	h.runtime.Play();
	ASSERT_TRUE(h.runtime.IsPlaying());
	Simulate(h, 60 * 30);

	EXPECT_EQ(int(h.runtime.ScriptErrorCount()), 0);
	EXPECT_TRUE(h.runtime.TriggerCount() >= 3u);
	ASSERT_TRUE(h.runtime.GameplayGlobal(String("chest_open")).IsSome());
	EXPECT_TRUE(h.runtime.GameplayGlobal(String("chest_open")).Unwrap().IsTruthy());
	EXPECT_TRUE(h.runtime.GameplayGlobal(String("escaped")).Unwrap().IsTruthy());
	// Le couvercle a pivoté, la herse est montée, le joueur est passé.
	EXPECT_TRUE(Node(h, "Couvercle")->transform.EulerDegrees().x < -60.f);
	EXPECT_TRUE(Node(h, "Porte")->transform.position.y > doorBefore.y + 3.f);
	EXPECT_TRUE(Node(h, "Joueur")->transform.position.z > 44.f);
	EXPECT_TRUE(Node(h, "Joueur")->transform.position.y > 0.f); // pas tombé à travers le sol

	// Stop restaure le document : porte baissée, coffre fermé.
	h.runtime.Stop();
	EXPECT_TRUE((Node(h, "Porte")->transform.position - doorBefore).Length() < 1e-4f);
	EXPECT_FALSE(Node(h, "Coffre")->Get(String("open"))->AsBool());
}

TEST(Dungeon, TheCurrentCameraNodeGivesThePlayView) {
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Donjon"));
	h.runtime.Play();
	Simulate(h, 30);
	const scene::NodeId eye = h.runtime.ResolveId(String("Vue"));
	EXPECT_TRUE(h.runtime.CurrentCamera() == eye);
	const math::FVector3 eyeWorld = h.runtime.ActiveScene()->tree.GlobalPosition(eye);
	EXPECT_TRUE((h.runtime.ActiveCamera().position - eyeWorld).Length() < 0.05f);
	constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
	EXPECT_TRUE(sdl3::Abs(h.runtime.ActiveCamera().fovYRadians - 75.f * DEG2RAD) < 1e-4f);
}

TEST(NodeScripts, TorchesFlickerIndependentlyAndStopRestoresThem) {
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Donjon"));
	const float BASE = LightDesc::Read(*Node(h, "Torche 1")).intensity;
	h.runtime.Play();
	bool changed = false, different = false;
	for (int i = 0; i < 60; ++i) {
		Simulate(h, 1);
		const float a = LightDesc::Read(*Node(h, "Torche 1")).intensity;
		const float b = LightDesc::Read(*Node(h, "Torche 2")).intensity;
		changed = changed || sdl3::Abs(a - BASE) > 0.05f;
		different = different || sdl3::Abs(a - b) > 0.05f;
		// Toujours dans les bornes des propriétés (0,72 à 1,18 × la base).
		EXPECT_TRUE(a >= BASE * 0.72f - 1e-3f && a <= BASE * 1.18f + 1e-3f);
	}
	EXPECT_TRUE(changed);
	EXPECT_TRUE(different);
	std::vector<ScriptStatus> statuses = h.runtime.ScriptStatuses();
	bool torchRunning = false;
	for (const ScriptStatus &status : statuses)
		if (status.name == "torchlight")
			torchRunning = status.running && status.nodes == 5u;
	EXPECT_TRUE(torchRunning);

	h.runtime.Stop();
	EXPECT_TRUE(LightDesc::Read(*Node(h, "Torche 1")).intensity == BASE);
}

TEST(NodeScripts, EachNodeReceivesItsOwnSelf) {
	Harness h;
	(void)h.runtime.AddScript(String("tagger"), String("fn on_start(self) { node.prop(self, \"seen\", self) }"));
	const scene::NodeId a = h.runtime.ResolveId(String("Tore"));
	const scene::NodeId b = h.runtime.ResolveId(String("Cristal"));
	ASSERT_TRUE(a.Valid() && b.Valid());
	ASSERT_TRUE(h.runtime.SetScriptRef(a, String("tagger")));
	ASSERT_TRUE(h.runtime.SetScriptRef(b, String("tagger")));
	h.runtime.Play();
	Simulate(h, 1);
	EXPECT_EQ(Node(h, "Tore")->Get(String("seen"))->AsString(), "Tore");
	EXPECT_EQ(Node(h, "Cristal")->Get(String("seen"))->AsString(), "Cristal");
}

TEST(NodeScripts, ABrokenScriptIsDisabledAloneAndReported) {
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Donjon"));
	(void)h.runtime.AddScript(String("broken"), String("fn on_update(self, dt) { let x = nil + 1 }"));
	ASSERT_TRUE(h.runtime.SetScriptRef(h.runtime.ResolveId(String("Sortie")), String("broken")));
	h.runtime.Play();
	Simulate(h, 10);
	bool brokenStopped = false, torchesRunning = false;
	for (const ScriptStatus &status : h.runtime.ScriptStatuses()) {
		if (status.name == "broken")
			brokenStopped = !status.running && !status.error.IsEmpty();
		if (status.name == "torchlight")
			torchesRunning = status.running;
	}
	EXPECT_TRUE(brokenStopped);
	EXPECT_TRUE(torchesRunning);
	EXPECT_EQ(int(h.runtime.ScriptErrorCount()), 1); // une fois, pas une par image
}

TEST(Triggers, OnlyTransitionsAreNotifiedAndOnceMeansOnce) {
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Donjon"));
	// Script de scène de test : compte les entrées et sorties.
	ASSERT_TRUE(h.runtime.SetGameplayScript(String("Donjon"),
											String("let enters = 0\nlet exits = 0\n"
												   "fn on_trigger(zone, other, event) { enters += 1 }\n"
												   "fn on_trigger_exit(zone, other, event) { exits += 1 }"))
					.IsNone());
	// Une zone RÉPÉTABLE, loin du chemin du joueur.
	ObjectDesc zone = ObjectDesc::Group(String("Zone test"));
	zone.type = String();
	zone.trigger = Some(TriggerDesc{{1.f, 2.f, 1.f}, String("test"), false});
	zone.transform.position = {0.f, 1.f, -30.f};
	ASSERT_TRUE(h.runtime.SpawnNode(zone).IsSome());
	// Le joueur ne doit pas bouger tout seul : sa visite guidée est coupée.
	ASSERT_TRUE(h.runtime.SetScriptRef(h.runtime.ResolveId(String("Joueur")), String()));

	h.runtime.Play();
	const scene::NodeId player = h.runtime.ResolveId(String("Joueur"));
	auto teleport = [&](float z) {
		ASSERT_TRUE(h.runtime.SetPosition(player, {0.f, 1.f, z}));
		Simulate(h, 3);
	};
	teleport(-30.f); // entre
	teleport(-30.2f); // reste dedans : rien
	teleport(-10.f); // sort
	teleport(-30.f); // rentre (répétable)
	EXPECT_EQ(int(h.runtime.GameplayGlobal(String("enters")).Unwrap().AsNumber()), 2);
	EXPECT_EQ(int(h.runtime.GameplayGlobal(String("exits")).Unwrap().AsNumber()), 1);

	// « once » : la zone du coffre ne notifie qu'une entrée.
	teleport(26.f);
	teleport(10.f);
	teleport(26.f);
	EXPECT_EQ(int(h.runtime.GameplayGlobal(String("enters")).Unwrap().AsNumber()), 3);
}

TEST(Components, AddRemoveEditAsJsonAndUndo) {
	Harness h;
	const scene::NodeId id = h.runtime.ResolveId(String("Tore"));
	ASSERT_TRUE(id.Valid());
	ASSERT_TRUE(h.runtime.AddComponentOfType(id, String(component::LIGHT)));
	EXPECT_TRUE(LightDesc::Has(*Node(h, "Tore")));
	Simulate(h, 1);
	EXPECT_TRUE(h.runtime.PointLights().size() == 1u);

	// Édition « en JSON » : les propriétés remplacent celles du composant.
	scene::PropertyMap props;
	props.Set(String("kind"), scene::PropertyValue::Str(String("spot")));
	props.Set(String("intensity"), scene::PropertyValue::Float(5.0));
	ASSERT_TRUE(h.runtime.SetComponentProps(id, String(component::LIGHT), props));
	Simulate(h, 1);
	EXPECT_TRUE(h.runtime.PointLights().empty());
	ASSERT_TRUE(h.runtime.SpotLights().size() == 1u);
	EXPECT_TRUE(h.runtime.SpotLights().front().intensity == 5.f);

	ASSERT_TRUE(h.runtime.Undo()); // retour à la lumière ponctuelle
	ASSERT_TRUE(h.runtime.Undo()); // plus de lumière du tout
	EXPECT_FALSE(LightDesc::Has(*Node(h, "Tore")));
	Simulate(h, 1);
	EXPECT_TRUE(h.runtime.PointLights().empty() && h.runtime.SpotLights().empty());

	EXPECT_FALSE(h.runtime.AddComponentOfType(id, String("Inconnu")));
	EXPECT_FALSE(h.runtime.RemoveComponentOfType(id, String(component::CAMERA)));
}

TEST(Components, ASpotLightPointsAlongTheNodesLocalDown) {
	Harness h;
	LightDesc spot;
	spot.kind = LightKind::SPOT;
	ObjectDesc lamp = ObjectDesc::Light(String("Lampe"), spot);
	lamp.transform.SetEulerDegrees({90.f, 0.f, 0.f}); // -Y local tourné de 90° autour de X
	ASSERT_TRUE(h.runtime.SpawnNode(lamp).IsSome());
	Simulate(h, 1);
	ASSERT_TRUE(h.runtime.SpotLights().size() == 1u);
	const math::FVector3 dir = h.runtime.SpotLights().front().direction;
	// Rx(90°)·(0,-1,0) = (0, 0, -1).
	EXPECT_TRUE(sdl3::Abs(dir.x) < 1e-4f && sdl3::Abs(dir.y) < 1e-4f && sdl3::Abs(dir.z + 1.f) < 1e-4f);
}

TEST(Helpers, LightsCamerasAndZonesArePickableInEditOnly) {
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Donjon"));
	const scene::NodeId torch = h.runtime.ResolveId(String("Torche 1"));
	const scene::NodeId zone = h.runtime.ResolveId(String("Sortie"));
	EXPECT_TRUE(h.runtime.HasVisibleHelper(torch));
	EXPECT_TRUE(h.runtime.HasVisibleHelper(zone));
	EXPECT_TRUE(h.runtime.HasVisibleHelper(h.runtime.ResolveId(String("Vue"))));

	// Un rayon qui vise le repère de la torche la sélectionne.
	const math::FVector3 at = h.runtime.ActiveScene()->tree.GlobalPosition(torch);
	const math::FVector3 from = at + math::FVector3{2.f, 0.f, 0.f} * (at.x < 0.f ? 1.f : -1.f);
	EXPECT_TRUE(h.runtime.SelectAt(math::FRay{from, (at - from).Normalize()}));
	EXPECT_TRUE(h.runtime.SelectedId() == torch);

	h.runtime.Play();
	EXPECT_FALSE(h.runtime.HasVisibleHelper(torch));
	h.runtime.Stop();
	EXPECT_TRUE(h.runtime.HasVisibleHelper(h.runtime.ResolveId(String("Torche 1"))));
}

TEST(Project, TheScriptLibrarySurvivesTheRoundTripAndOldFilesStillLoad) {
	Project project = LoadDemoProject();
	ASSERT_TRUE(project.scripts.size() == 5u);
	auto reloaded = Project::DecodeJson(project.EncodeJson());
	ASSERT_TRUE(reloaded.IsOk());
	ASSERT_TRUE(reloaded.Value().scripts.size() == 5u);
	EXPECT_EQ(reloaded.Value().FindScript(String("torchlight"))->source, project.FindScript(String("torchlight"))->source);
	// Composants : lumière et zone relues à l'identique.
	const SceneDesc *dungeon = reloaded.Value().FindScene(String("Donjon"));
	ASSERT_TRUE(dungeon != nullptr);
	const LightDesc light = LightDesc::Read(*dungeon->Find(String("Torche 3")));
	EXPECT_TRUE(light.intensity == 3.2f && light.range == 11.f && light.color.r == 255);
	EXPECT_EQ(TriggerDesc::Read(*dungeon->Find(String("Sortie"))).event, "exit");

	// Un fichier d'avant la bibliothèque (v3, sans `scripts`) se lit.
	String legacy = project.EncodeJson();
	auto document = Project::DecodeJson(legacy);
	ASSERT_TRUE(document.IsOk());
	Project old = std::move(document).Unwrap();
	old.scripts.clear();
	auto oldReloaded = Project::DecodeJson(old.EncodeJson());
	ASSERT_TRUE(oldReloaded.IsOk());
	EXPECT_TRUE(oldReloaded.Value().scripts.empty());
}

TEST(Scripts, CheckReportsTheFirstErrorWithItsLine) {
	EXPECT_TRUE(Runtime::CheckScript(String("let a = 1\nfn f() { return a }")).IsNone());
	Option<data::script::ScriptError> error = Runtime::CheckScript(String("let a = 1\nlet = 2\n"));
	ASSERT_TRUE(error.IsSome());
	EXPECT_EQ(error.Unwrap().line, 2);

	Harness h;
	EXPECT_TRUE(h.runtime.SetScriptSource(String("door"), String("fn on_update(self, dt) {")).IsSome());
	// Le texte est gardé malgré l'erreur : on ne perd pas un travail en cours.
	EXPECT_EQ(h.runtime.GetProject().FindScript(String("door"))->source, "fn on_update(self, dt) {");
}

TEST(ScriptApi, LightsAreDrivenByName) {
	Harness h;
	ASSERT_TRUE(h.runtime.SwitchScene("Donjon"));
	ASSERT_TRUE(h.Run("light.set_intensity(\"Torche 2\", 0.5)\n"
					  "light.set_color(\"Torche 2\", 10, 20, 30)\n"
					  "assert(light.intensity(\"Torche 2\") == 0.5)\n"
					  "assert(light.intensity(\"Pavé\") == nil)\n"
					  "assert(len(light.list()) == 5)\n"
					  "let d = input.mouse_delta()\n"
					  "assert(d[0] == 0 and d[1] == 0)"));
	const LightDesc light = LightDesc::Read(*Node(h, "Torche 2"));
	EXPECT_TRUE(light.intensity == 0.5f);
	EXPECT_TRUE(light.color.r == 10 && light.color.g == 20 && light.color.b == 30);
}

// ============================================================================
// Interface : briques testables sans fenêtre
// ============================================================================

TEST(Templates, EveryMenuEntryBuildsTheRightKindOfNode) {
	for (const NodeTemplate &entry : NODE_TEMPLATES) {
		Option<ObjectDesc> desc = MakeNodeFromTemplate(String(entry.key));
		ASSERT_TRUE(desc.IsSome());
		const scene::Node node = desc.Value().ToNode();
		EXPECT_EQ(node.name, String(entry.label));
		if (String(entry.category) == "3D")
			EXPECT_TRUE(VisualDesc::Has(node));
		if (String(entry.category) == "Lumière")
			EXPECT_TRUE(LightDesc::Has(node) && node.type == node_kind::LIGHT);
	}
	EXPECT_TRUE(LightDesc::Read(MakeNodeFromTemplate(String("spot_light")).Value().ToNode()).kind == LightKind::SPOT);
	EXPECT_TRUE(MakeNodeFromTemplate(String("trigger")).Value().ToNode().type == node_kind::TRIGGER);
	EXPECT_TRUE(MakeNodeFromTemplate(String("folder")).Value().ToNode().type == node_kind::FOLDER);
	EXPECT_TRUE(MakeNodeFromTemplate(String("inconnu")).IsNone());

	// Créé sous un parent : l'enfant naît au pivot du parent, et s'annule.
	Harness h;
	const scene::NodeId parent = h.runtime.ResolveId(String("Tore"));
	Option<scene::NodeId> child = h.runtime.SpawnNode(MakeNodeFromTemplate(String("point_light")).Unwrap(), parent);
	ASSERT_TRUE(child.IsSome());
	EXPECT_TRUE(h.runtime.ActiveScene()->tree.ParentOf(child.Unwrap()) == parent);
	ASSERT_TRUE(h.runtime.Undo());
	EXPECT_FALSE(h.runtime.ActiveScene()->tree.Contains(child.Unwrap()));
}

namespace {
/// Morceaux colorés d'une ligne, sous forme lisible : « let|kw », « 42|num »…
std::vector<String> Spans(void (*highlight)(const String &, std::vector<ui::UiTextSpan> &), const String &line) {
	std::vector<ui::UiTextSpan> spans;
	highlight(line, spans);
	std::vector<String> out;
	for (const ui::UiTextSpan &span : spans)
		out.push_back(line.Substr(span.begin, span.end - span.begin));
	return out;
}
} // namespace

TEST(Syntax, ScriptTokensAreColouredWithoutOverlap) {
	const String line("let n = node.prop(self, \"seed\") + 1.5  # bruit");
	const std::vector<String> spans = Spans(kit::syntax::HighlightSled, line);
	const std::vector<String> expected = {"let", "node", "prop", "\"seed\"", "1.5", "# bruit"};
	ASSERT_TRUE(spans.size() == expected.size());
	for (size_t i = 0; i < expected.size(); ++i)
		EXPECT_EQ(spans[i], expected[i]);

	std::vector<ui::UiTextSpan> raw;
	kit::syntax::HighlightSled(line, raw);
	for (size_t i = 1; i < raw.size(); ++i)
		EXPECT_TRUE(raw[i].begin >= raw[i - 1].end); // triés, disjoints
	// Un « # » DANS une chaîne n'ouvre pas de commentaire.
	EXPECT_EQ(Spans(kit::syntax::HighlightSled, String("print(\"#1\")")).back(), "\"#1\"");
}

TEST(Syntax, JsonKeysAreTellApartFromStringValues) {
	std::vector<ui::UiTextSpan> spans;
	const String line("  \"name\": \"Torche\", \"on\": true, \"n\": -2.5e3 }");
	kit::syntax::HighlightJson(line, spans);
	auto colorOf = [&](const char *token) {
		const size_t at = line.Find(token);
		for (const ui::UiTextSpan &span : spans)
			if (span.begin == at)
				return span.color;
		return sdl3::FColor{};
	};
	EXPECT_TRUE(colorOf("\"name\"") == kit::syntax::Colors().key);
	EXPECT_TRUE(colorOf("\"Torche\"") == kit::syntax::Colors().string);
	EXPECT_TRUE(colorOf("true") == kit::syntax::Colors().literal);
	EXPECT_TRUE(colorOf("-2.5e3") == kit::syntax::Colors().number);
	EXPECT_TRUE(colorOf("}") == kit::syntax::Colors().brace);
	EXPECT_TRUE(bool(kit::syntax::ForName(String("scene.JSON"))));
}

TEST(Syntax, ConsoleLevelsColourTheirPrefix) {
	std::vector<ui::UiTextSpan> spans;
	kit::syntax::HighlightLog(String("[err   ] boum"), spans);
	ASSERT_TRUE(spans.size() == 1u);
	EXPECT_EQ(spans[0].end, size_t(13)); // une erreur se colore sur toute la ligne
	spans.clear();
	kit::syntax::HighlightLog(String("[warn  ] attention"), spans);
	ASSERT_TRUE(spans.size() == 1u);
	EXPECT_EQ(spans[0].end, size_t(8)); // un avertissement, son seul préfixe
	spans.clear();
	kit::syntax::HighlightLog(String("sans niveau"), spans);
	EXPECT_TRUE(spans.empty());
}

TEST(AssetBrowser, NavigatesProjectFoldersAndTheDisk) {
	Project project = LoadDemoProject();
	AssetBrowserModel model(&project, String("assets"));
	EXPECT_EQ(model.Location(), AssetBrowserModel::ROOT);
	EXPECT_FALSE(model.CanBack() || model.CanUp());

	// Racine : les dossiers du projet, puis ceux du disque — sans les
	// données de l'émulateur.
	bool scenes = false, scripts = false, models = false, bios = false;
	for (const AssetEntry &entry : model.Entries()) {
		scenes = scenes || entry.location == AssetBrowserModel::SCENES;
		scripts = scripts || entry.location == AssetBrowserModel::SCRIPTS;
		models = models || entry.location == "assets/models";
		bios = bios || entry.name == "bios-firmware";
	}
	EXPECT_TRUE(scenes && scripts && models);
	EXPECT_FALSE(bios);

	// Scripts : la bibliothèque + un script de jeu par scène, tous compilent.
	ASSERT_TRUE(model.Navigate(String(AssetBrowserModel::SCRIPTS)));
	const std::vector<AssetEntry> list = model.Entries();
	EXPECT_EQ(list.size(), project.scripts.size() + project.scenes.size());
	for (const AssetEntry &entry : list)
		EXPECT_FALSE(entry.broken);
	EXPECT_EQ(model.Entries(String("TORCH")).size(), size_t(1)); // filtre insensible à la casse

	// Disque : dossiers d'abord, fil d'Ariane, précédent / suivant / parent.
	ASSERT_TRUE(model.Navigate(String("assets/models/animals")));
	const auto crumbs = model.Breadcrumb();
	ASSERT_TRUE(crumbs.size() == 3u);
	EXPECT_EQ(crumbs[1].first, "models");
	EXPECT_EQ(crumbs[2].second, "assets/models/animals");
	bool sawGltf = false, sawBin = false;
	for (const AssetEntry &entry : model.Entries()) {
		sawGltf = sawGltf || entry.kind == AssetKind::MODEL;
		sawBin = sawBin || entry.name.EndsWith(".bin"); // tampons glTF masqués
	}
	EXPECT_TRUE(sawGltf);
	EXPECT_FALSE(sawBin);
	ASSERT_TRUE(model.Up());
	EXPECT_EQ(model.Location(), "assets/models");
	ASSERT_TRUE(model.Up());
	EXPECT_EQ(model.Location(), AssetBrowserModel::ROOT); // assets/x remonte à la racine du projet
	ASSERT_TRUE(model.Back());
	EXPECT_EQ(model.Location(), "assets/models");
	ASSERT_TRUE(model.Forward());
	EXPECT_EQ(model.Location(), AssetBrowserModel::ROOT);
	EXPECT_FALSE(model.Navigate(String("assets/models/animals/Cow.gltf"))); // un fichier n'est pas un dossier
	EXPECT_TRUE(AssetBrowserModel::KindOf(String("Sol.PNG")) == AssetKind::TEXTURE);
	EXPECT_TRUE(AssetBrowserModel::KindOf(String("vehicule.scene")) == AssetKind::SCENE);
}

TEST(AssetBrowser, SavesAreAFolderOfTheirOwn) {
	Project project = LoadDemoProject();
	AssetBrowserModel model(&project, String("assets"), String("tests"));
	bool saves = false;
	for (const AssetEntry &entry : model.Entries())
		saves = saves || (entry.name == "Sauvegardes" && entry.location == "tests");
	EXPECT_TRUE(saves);
	ASSERT_TRUE(model.Navigate(String("tests")));
	const auto crumbs = model.Breadcrumb();
	ASSERT_TRUE(crumbs.size() == 2u);
	EXPECT_EQ(crumbs[1].first, "Sauvegardes");
	ASSERT_TRUE(model.Up());
	EXPECT_EQ(model.Location(), AssetBrowserModel::ROOT);
}

int main() { return RUN_ALL_TESTS(); }
