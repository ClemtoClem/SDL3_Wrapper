// Tests unitaires — examples/level_editor (éditeur de niveau 3D).
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

// Chemins relatifs : les en-têtes de `examples/` ne sont pas dans le chemin
// d'inclusion du Makefile (seul `lib/include` l'est), et une inclusion
// relative évite d'avoir à l'y ajouter juste pour ce test.
#include "../examples/level_editor_demo/app.hpp"
#include "../examples/level_editor_demo/cli.hpp"
#include "../examples/level_editor_demo/content.hpp"
#include "../examples/level_editor_demo/project.hpp"
#include "../examples/level_editor_demo/report.hpp"
#include "../examples/level_editor_demo/runtime.hpp"
#include "../examples/level_editor_demo/scenarios.hpp"

using namespace level_editor;

namespace {

/// Fabrique un argv à partir d'un tableau de chaînes littérales.
Result<CommandLine, String> ParseArgs(std::vector<const char *> args) {
	std::vector<char *> argv;
	argv.push_back(const_cast<char *>("level_editor_demo"));
	for (const char *arg : args)
		argv.push_back(const_cast<char *>(arg));
	return CommandLine::Parse(int(argv.size()), argv.data());
}

/// Runtime prêt à l'emploi sur le projet de démonstration, sans GPU.
struct Harness {
	ecs::ArchetypeRegistry registry;
	Runtime runtime{registry};

	Harness() { runtime.OpenProject(MakeDemoProject()); }

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
	EXPECT_EQ(parsed.Value().theme, "dark");
}

// ============================================================================
// Document de projet
// ============================================================================

TEST(Project, JsonRoundTripPreservesEverything) {
	Project original = MakeDemoProject();
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
		ASSERT_TRUE(after.objects.size() == before.objects.size());
		for (size_t j = 0; j < before.objects.size(); ++j) {
			const ObjectDesc &a = before.objects[j];
			const ObjectDesc &b = after.objects[j];
			EXPECT_EQ(b.name, a.name);
			EXPECT_TRUE(b.shape == a.shape);
			EXPECT_TRUE(b.transform.position == a.transform.position);
			EXPECT_TRUE(b.transform.scale == a.transform.scale);
			EXPECT_TRUE(b.physics.body == a.physics.body);
			EXPECT_TRUE(b.material.kind == a.material.kind);
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
	scene.objects.push_back(std::move(object));
	project.scenes.push_back(std::move(scene));
	project.activeScene = "Test";

	auto decoded = Project::DecodeJson(project.EncodeJson());
	ASSERT_TRUE(decoded.IsOk());
	const ObjectDesc *reloaded = decoded.Value().scenes[0].Find("Bloc");
	ASSERT_TRUE(reloaded != nullptr);
	EXPECT_EQ(reloaded->transform.position.x, 5.f);
	EXPECT_EQ(reloaded->transform.position.z, -12.f);
	EXPECT_EQ(reloaded->transform.scale.z, 3.f);
	EXPECT_EQ(reloaded->physics.mass, 4.f);
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
	Project project = MakeDemoProject();
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
	EXPECT_EQ(scene.Find("Enfant")->parent, "Boîte");

	// Supprimer le parent doit DÉTACHER l'enfant, pas laisser un lien mort
	// qui ne se résoudrait plus au prochain chargement.
	EXPECT_TRUE(scene.Remove("Boîte"));
	EXPECT_TRUE(scene.Find("Boîte") == nullptr);
	ASSERT_TRUE(scene.Find("Enfant") != nullptr);
	EXPECT_TRUE(scene.Find("Enfant")->parent.IsEmpty());
	EXPECT_FALSE(scene.Remove("Boîte")); // déjà supprimé
}

TEST(TransformDesc, EulerAndQuaternionRoundTrip) {
	// Repose sur math::FQuaternion::ToEuler, ajouté pour l'inspecteur.
	TransformDesc transform;
	transform.eulerDeg = {18.f, -47.f, 33.f};
	math::FQuaternion rotation = transform.Rotation();

	TransformDesc restored;
	restored.SetRotation(rotation);
	EXPECT_TRUE(sdl3::Abs(restored.eulerDeg.x - 18.f) < 0.01f);
	EXPECT_TRUE(sdl3::Abs(restored.eulerDeg.y - -47.f) < 0.01f);
	EXPECT_TRUE(sdl3::Abs(restored.eulerDeg.z - 33.f) < 0.01f);
}

// ============================================================================
// Contenu livré
// ============================================================================

TEST(Content, DemoProjectIsWellFormed) {
	Project project = MakeDemoProject();
	ASSERT_TRUE(project.scenes.size() == 3);

	for (const SceneDesc &scene : project.scenes) {
		EXPECT_FALSE(scene.name.IsEmpty());
		EXPECT_FALSE(scene.description.IsEmpty());
		EXPECT_FALSE(scene.gameplayScript.IsEmpty());
		EXPECT_TRUE(scene.objects.size() > 0);

		for (const ObjectDesc &object : scene.objects) {
			EXPECT_FALSE(object.name.IsEmpty());
			// Chaque nom est unique dans sa scène (l'identité des objets en
			// dépend : scripts, liens de parenté, rapport).
			int count = 0;
			for (const ObjectDesc &other : scene.objects)
				if (other.name == object.name)
					++count;
			EXPECT_EQ(count, 1);
			// Un lien de parenté doit toujours résoudre.
			if (!object.parent.IsEmpty())
				EXPECT_TRUE(scene.Find(object.parent) != nullptr);
		}
	}
}

TEST(Content, EveryGameplayScriptCompiles) {
	// Un script embarqué qui ne compile pas ne se verrait qu'au moment de
	// lancer le mode Jeu, dans une scène précise : autant l'attraper ici.
	for (const char *source : {SHOWCASE_SCRIPT, CIRCUIT_SCRIPT, PHYSICS_SCRIPT}) {
		auto program = data::script::Parser::Compile(StringView(source));
		if (program.IsError())
			test::ReportFailure(__FILE__, __LINE__, program.Error().Format().CStr());
		EXPECT_TRUE(program.IsOk());
	}
}

TEST(Content, CircuitHasAnOrderedRingOfCheckpoints) {
	Project project = MakeDemoProject();
	const SceneDesc *circuit = project.FindScene("Circuit");
	ASSERT_TRUE(circuit != nullptr);

	std::vector<const ObjectDesc *> checkpoints = circuit->WithTag(String("checkpoint"));
	ASSERT_TRUE(checkpoints.size() == 8);

	// Le pilote automatique vise chaque point EN LIGNE DROITE : l'écart entre
	// la corde et l'arc doit rester dans la demi-largeur de piste, sinon la
	// voiture coupe dans le décor. On vérifie donc que deux points
	// consécutifs sont assez proches pour que ce soit vrai.
	for (size_t i = 0; i + 1 < checkpoints.size(); ++i) {
		math::FVector3 a = checkpoints[i]->transform.position;
		math::FVector3 b = checkpoints[i + 1]->transform.position;
		float distance = (b - a).Length();
		EXPECT_TRUE(distance > 10.f);
		EXPECT_TRUE(distance < 30.f);
	}

	// La voiture est posée sur le premier point, et c'est elle que suit la
	// caméra du mode Jeu.
	const ObjectDesc *car = circuit->Find("Voiture");
	ASSERT_TRUE(car != nullptr);
	EXPECT_EQ(car->tag, "camera_target");
	EXPECT_TRUE(car->physics.body == BodyKind::DYNAMIC);
	EXPECT_TRUE((car->transform.position - checkpoints[0]->transform.position).Length() < 2.f);
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
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == scene->objects.size());
	EXPECT_TRUE(harness.runtime.RigidBodyCount() > 0);
	EXPECT_TRUE(harness.runtime.RigidBodyCount() < scene->objects.size());
}

TEST(Runtime, SwitchingSceneRebuildsEverything) {
	Harness harness;
	EXPECT_FALSE(harness.runtime.SwitchScene("Scène inexistante"));
	ASSERT_TRUE(harness.runtime.SwitchScene("Laboratoire physique"));

	const SceneDesc *scene = harness.runtime.ActiveScene();
	ASSERT_TRUE(scene != nullptr);
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == scene->objects.size());
	// Aucun reliquat de la scène précédente.
	EXPECT_TRUE(harness.runtime.Registry().EntitiesWith<SceneObjectRef>().size() == scene->objects.size());
}

TEST(Runtime, EditCommandsWriteToDocumentAndRuntimeTogether) {
	Harness harness;
	const char *name = "Cube plastique";

	ASSERT_TRUE(harness.runtime.SetPosition(String(name), {3.f, 2.f, 1.f}));
	const ObjectDesc *object = harness.runtime.ActiveScene()->Find(name);
	ASSERT_TRUE(object != nullptr);
	EXPECT_TRUE(object->transform.position == math::FVector3(3.f, 2.f, 1.f));

	ASSERT_TRUE(harness.runtime.SetEulerDegrees(String(name), {0.f, 90.f, 0.f}));
	EXPECT_EQ(harness.runtime.ActiveScene()->Find(name)->transform.eulerDeg.y, 90.f);

	// L'échelle est bornée : une valeur nulle produirait une matrice
	// singulière (objet invisible, normales dégénérées).
	ASSERT_TRUE(harness.runtime.SetScale(String(name), {0.f, -5.f, 2.f}));
	EXPECT_TRUE(harness.runtime.ActiveScene()->Find(name)->transform.scale.x > 0.f);
	EXPECT_TRUE(harness.runtime.ActiveScene()->Find(name)->transform.scale.y > 0.f);
	EXPECT_EQ(harness.runtime.ActiveScene()->Find(name)->transform.scale.z, 2.f);

	ASSERT_TRUE(harness.runtime.SetMaterialColor(String(name), sdl3::Color{10, 20, 30, 255}));
	EXPECT_TRUE(harness.runtime.ActiveScene()->Find(name)->material.baseColor.r == 10);

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
	size_t before = harness.runtime.ActiveScene()->objects.size();

	ObjectDesc object;
	object.name = "Cube plastique"; // nom déjà pris -> doit être suffixé
	object.shape = ShapeKind::SPHERE;
	Option<String> assigned = harness.runtime.SpawnObject(object);
	ASSERT_TRUE(assigned.IsSome());
	EXPECT_TRUE(assigned.Unwrap() != "Cube plastique");
	EXPECT_TRUE(harness.runtime.ActiveScene()->objects.size() == before + 1);
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == before + 1);

	// Renommage : refusé vers un nom déjà pris, accepté sinon.
	EXPECT_FALSE(harness.runtime.RenameObject(assigned.Unwrap(), String("Cube plastique")));
	ASSERT_TRUE(harness.runtime.RenameObject(assigned.Unwrap(), String("Nouvelle sphère")));
	EXPECT_TRUE(harness.runtime.ActiveScene()->Find("Nouvelle sphère") != nullptr);

	ASSERT_TRUE(harness.runtime.RemoveObject(String("Nouvelle sphère")));
	EXPECT_TRUE(harness.runtime.ActiveScene()->objects.size() == before);
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

	std::vector<ObjectDesc> before = harness.runtime.ActiveScene()->objects;
	harness.runtime.Play();
	EXPECT_TRUE(harness.runtime.IsPlaying());

	for (int i = 0; i < 150; ++i)
		harness.runtime.Update(1.f / 60.f);

	// La partie a réellement FAIT quelque chose — sinon « restaure à
	// l'identique » ne prouverait rien. Le script de cette scène largue des
	// caisses : la scène compte donc plus d'objets qu'au départ, et au moins
	// une caisse est déjà descendue sous son altitude d'apparition.
	const SceneDesc *playing = harness.runtime.ActiveScene();
	EXPECT_TRUE(playing->objects.size() > before.size());
	bool fell = false;
	for (const ObjectDesc *crate : playing->WithTag(String("debris")))
		if (crate->transform.position.y < 9.f)
			fell = true;
	EXPECT_TRUE(fell);

	harness.runtime.Stop();
	EXPECT_FALSE(harness.runtime.IsPlaying());

	// Stop restaure l'instantané ENTIER : les caisses larguées pendant la
	// partie disparaissent avec elle, et le projet enregistré est intact.
	const std::vector<ObjectDesc> &after = harness.runtime.ActiveScene()->objects;
	ASSERT_TRUE(after.size() == before.size());
	EXPECT_TRUE(harness.runtime.ActiveScene()->WithTag(String("debris")).empty());
	EXPECT_TRUE(harness.runtime.RuntimeObjectCount() == before.size());
	for (size_t i = 0; i < before.size(); ++i) {
		EXPECT_EQ(after[i].name, before[i].name);
		EXPECT_TRUE(after[i].transform.position == before[i].transform.position);
		EXPECT_TRUE(after[i].transform.eulerDeg == before[i].transform.eulerDeg);
	}
}

TEST(Runtime, PlayIsReproducible) {
	// Deux parties, même graine, même pas de temps : mêmes positions finales.
	// C'est ce qui permet à un rapport d'exécution d'être comparé d'un run à
	// l'autre, et à un scénario d'affirmer un nombre de tours.
	auto simulate = [](uint64_t seed) {
		ecs::ArchetypeRegistry registry;
		Runtime runtime(registry);
		runtime.SetRandomSeed(seed);
		runtime.OpenProject(MakeDemoProject());
		(void)runtime.SwitchScene("Circuit");
		runtime.Play();
		for (int i = 0; i < 240; ++i)
			runtime.Update(1.f / 60.f);
		const ObjectDesc *car = runtime.ActiveScene()->Find("Voiture");
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
	const ObjectDesc *car = harness.runtime.ActiveScene()->Find("Voiture");
	ASSERT_TRUE(car != nullptr);
	float radius = math::FVector2{car->transform.position.x, car->transform.position.z}.Length();
	EXPECT_TRUE(radius > 24.f);
	EXPECT_TRUE(radius < 36.f);
	EXPECT_TRUE(car->transform.position.y > -1.f);
}

TEST(Runtime, ShowcaseAnimationScriptDrivesObjects) {
	Harness harness;
	const ObjectDesc *beacon = harness.runtime.ActiveScene()->Find("Balise");
	ASSERT_TRUE(beacon != nullptr);
	float restingHeight = beacon->transform.position.y;

	harness.runtime.Play();
	for (int i = 0; i < 40; ++i)
		harness.runtime.Update(1.f / 60.f);

	EXPECT_EQ(int(harness.runtime.ScriptErrorCount()), 0);
	const ObjectDesc *animated = harness.runtime.ActiveScene()->Find("Balise");
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
	size_t before = harness.runtime.ActiveScene()->objects.size();

	ASSERT_TRUE(harness.Run("for i in range(0, 5) {\n"
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
	EXPECT_TRUE(scene->objects.size() == before + 5);
	EXPECT_TRUE(scene->WithTag(String("genere")).size() == 5);

	const ObjectDesc *third = scene->Find("Généré 2");
	ASSERT_TRUE(third != nullptr);
	EXPECT_TRUE(third->shape == ShapeKind::SPHERE);
	EXPECT_TRUE(third->physics.body == BodyKind::DYNAMIC);
	EXPECT_EQ(third->transform.position.x, 4.f);
	EXPECT_TRUE(third->material.baseColor.r == 200);
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
	const ObjectDesc *before = harness.runtime.ActiveScene()->Find("Tore");
	ASSERT_TRUE(before != nullptr);
	sdl3::Color originalColor = before->material.baseColor;

	ASSERT_TRUE(harness.Run("object.set_material(\"Tore\", {roughness: 0.11})"));

	const ObjectDesc *after = harness.runtime.ActiveScene()->Find("Tore");
	ASSERT_TRUE(after != nullptr);
	EXPECT_TRUE(sdl3::Abs(after->material.roughness - 0.11f) < 1e-5f);
	EXPECT_TRUE(after->material.baseColor.r == originalColor.r);
	EXPECT_TRUE(after->material.baseColor.g == originalColor.g);
}

TEST(ScriptApi, DrivesTheEditorItself) {
	Harness harness;
	ASSERT_TRUE(harness.Run("editor.select(\"Tore\")\n"
							"assert(editor.selected() == \"Tore\")\n"
							"assert(editor.scene() == \"Vitrine\")\n"
							"assert(len(editor.scenes()) == 3)\n"
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
	size_t before = harness.runtime.ActiveScene()->objects.size();

	auto imported = harness.runtime.ImportGltf(String("assets/models/animals/Cow.gltf"));
	if (imported.IsError())
		test::ReportFailure(__FILE__, __LINE__, imported.Error().CStr());
	ASSERT_TRUE(imported.IsOk());

	const ObjectDesc *object = harness.runtime.ActiveScene()->Find(imported.Value());
	ASSERT_TRUE(object != nullptr);
	EXPECT_TRUE(object->shape == ShapeKind::MODEL);
	EXPECT_EQ(object->source, "assets/models/animals/Cow.gltf");
	EXPECT_EQ(object->name, "Cow");
	// Le modèle est ramené à une taille exploitable : sans cet ajustement, un
	// import arrive soit invisible soit grand comme un mur.
	EXPECT_TRUE(object->transform.scale.x > 0.f);
	math::FVector3 fitted = object->dimensions * object->transform.scale.x;
	float largest = sdl3::Max(sdl3::Max(fitted.x, fitted.y), fitted.z);
	EXPECT_TRUE(sdl3::Abs(largest - 3.f) < 0.1f);

	EXPECT_TRUE(harness.runtime.ActiveScene()->objects.size() == before + 1);
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

	const ObjectDesc *object = reloaded.Value().ActiveScene()->Find(imported.Value());
	ASSERT_TRUE(object != nullptr);
	EXPECT_TRUE(object->shape == ShapeKind::MODEL);
	EXPECT_EQ(object->source, "assets/models/animals/Sheep.gltf");

	// Et le runtime sait bien reconstruire ce maillage à partir du chemin.
	auto mesh = Runtime::LoadGltfMesh(object->source);
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
	const String name = h.runtime.ActiveScene()->objects[1].name;
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
	const size_t before = h.runtime.ActiveScene()->objects.size();
	const String name = h.runtime.ActiveScene()->objects[2].name;

	ASSERT_TRUE(h.runtime.RemoveObject(name));
	EXPECT_EQ(h.runtime.ActiveScene()->objects.size(), before - 1);
	ASSERT_TRUE(h.runtime.Undo());
	EXPECT_EQ(h.runtime.ActiveScene()->objects.size(), before);
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
	const String name = h.runtime.ActiveScene()->objects[1].name;
	for (size_t i = 0; i < Runtime::MAX_HISTORY + 10; ++i)
		ASSERT_TRUE(h.runtime.SetPosition(name, math::FVector3{float(i), 0.f, 0.f}));
	EXPECT_EQ(h.runtime.UndoDepth(), Runtime::MAX_HISTORY);
}

// ============================================================================
// Sélection par rayon et manipulateur
// ============================================================================

TEST(Picking, ClickingTheCentreOfTheViewportSelectsWhatIsAimedAt) {
	Harness h;
	const String name = h.runtime.ActiveScene()->objects[1].name; // « Cube plastique », plein
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
	const String name = h.runtime.ActiveScene()->objects[1].name;
	ASSERT_TRUE(h.runtime.FocusOn(name));
	const math::FRay ray = h.runtime.ViewportRay(400.f, 300.f, 800.f, 600.f);
	ASSERT_TRUE(h.runtime.PickAt(ray).IsSome());

	ASSERT_TRUE(h.runtime.SetVisible(name, false));
	Option<Runtime::PickHit> hit = h.runtime.PickAt(ray);
	EXPECT_TRUE(hit.IsNone() || hit.Value().name != name);
}

TEST(Gizmo, DragMovesRotatesAndScalesAlongOneAxis) {
	Harness h;
	const String name = h.runtime.ActiveScene()->objects[3].name;
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
	EXPECT_TRUE(sdl3::Abs(turned.eulerDeg.y - (start.eulerDeg.y + 45.f)) < 1.5f);

	h.runtime.SetGizmoMode(Runtime::GizmoMode::SCALE);
	ASSERT_TRUE(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::Z, 0.f, 0.5f));
	EXPECT_TRUE(h.runtime.ActiveScene()->Find(name)->transform.scale.z > start.scale.z);
}

TEST(Gizmo, AWholeDragIsASingleUndoStep) {
	Harness h;
	const String name = h.runtime.ActiveScene()->objects[3].name;
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
	const String name = h.runtime.ActiveScene()->objects[3].name;
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
	const float angle = h.runtime.ActiveScene()->Find(name)->transform.eulerDeg.y;
	EXPECT_TRUE(sdl3::Abs(angle / 15.f - sdl3::Round(angle / 15.f)) < 1e-3f);
}

TEST(Gizmo, AxisPickingRequiresAimingAtTheHandle) {
	Harness h;
	const String name = h.runtime.ActiveScene()->objects[3].name;
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
            return sqrt(dx * dx + dy * dy + dz * dz)
        }

        let rayon = distance(depart, pivot)
        for i in range(0, 10) { camera.orbit(0.15, 0) }
        assert(distance(camera.position(), depart) > 1, "l'orbite n'a pas bougé la caméra")
        assert(abs(distance(camera.position(), pivot) - rayon) < 0.1, "l'orbite a changé la distance au pivot")

        let avant = camera.position()
        camera.dolly(-4)
        assert(abs(distance(camera.position(), avant) - 4) < 0.01, "la molette n'a pas reculé de 4 unités")

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
	Project project = MakeDemoProject();
	RunReport report;
	report.commandLine = "level_editor_demo --scenario=tour";
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
	EXPECT_EQ(root->Get("format")->stringValue, "level_editor.report");
	EXPECT_TRUE(root->Get("run")->Get("completed")->boolValue);
	EXPECT_TRUE(root->Get("performance")->Get("threads_peak_concurrent")->intValue == 2);
	EXPECT_TRUE(root->Get("project")->Get("scene_count")->intValue == 3);
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

int main() { return RUN_ALL_TESTS(); }
