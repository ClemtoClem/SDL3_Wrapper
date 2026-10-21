// Tests unitaires — sandbox (éditeur de niveau 3D).
#include "test.hpp"

#include <filesystem>
#include <fstream>

#include <sandbox/app/app.hpp>
#include <sandbox/app/cli.hpp>
#include <sandbox/document/project_files.hpp>
#include <sandbox/document/project.hpp>
#include <sandbox/app/report.hpp>
#include <sandbox/engine/runtime.hpp>
#include <sandbox/editor/asset_ops.hpp>
#include <sandbox/app/scenarios.hpp>

using namespace sandbox;

namespace {

/// Fabrique un argv à partir d'un tableau de chaînes littérales.
Result<CommandLine, String> ParseArgs(std::vector<const char *> args) {
	std::vector<char *> argv;
	argv.push_back(const_cast<char *>("sandbox"));
	for (const char *arg : args)
		argv.push_back(const_cast<char *>(arg));
	return CommandLine::Parse(int(argv.size()), argv.data());
}

/// Le projet de démonstration, lu depuis ses fichiers de données : plus
/// rien n'en est construit dans le code de l'éditeur. Lecture seule — aucun
/// test n'écrit dans ce dossier.
constexpr const char *DEMO_PROJECT = "demos/sandbox/tests/data/demo/demo.json";

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

TEST(Cli_ParsesEveryOption) {
	auto parsed = ParseArgs({"--width=1280", "--height=720", "--theme=aero", "--scene=Circuit", "--scenario=tour",
							 "--frames=250", "--seed=7", "--report=out.json", "--report-format=json",
							 "--screenshot-dir=shots", "--screenshot=10:a.png", "--screenshot=20:b.png",
							 "--headless", "--verbose"});
	CHECK(parsed.IsOk());
	const CommandLine &options = parsed.Value();
	CHECK_EQ(options.windowWidth, 1280);
	CHECK_EQ(options.windowHeight, 720);
	CHECK_EQ(options.theme, "aero");
	CHECK_EQ(options.scene, "Circuit");
	CHECK_EQ(options.scenario, "tour");
	CHECK_EQ(options.frames, 250L);
	CHECK(options.randomSeed == 7u);
	CHECK_EQ(options.reportPath, "out.json");
	CHECK(options.reportFormat == ReportFormat::JSON);
	CHECK_EQ(options.screenshotDir, "shots");
	CHECK(options.headless);
	CHECK(options.verbose);
	CHECK(options.screenshots.size() == 2);
	CHECK_EQ(options.screenshots[0].frame, 10L);
	CHECK_EQ(options.screenshots[1].path, "b.png");
}

TEST(Cli_RejectsBadInputInsteadOfIgnoringIt) {
	// Une faute de frappe dans une commande censée produire un rapport doit
	// être signalée, jamais avalée en silence.
	CHECK(ParseArgs({"--widht=800"}).IsError());
	CHECK(ParseArgs({"--frames=zero"}).IsError());
	CHECK(ParseArgs({"--frames=-4"}).IsError());
	CHECK(ParseArgs({"--theme=neon"}).IsError());
	CHECK(ParseArgs({"--report-format=xml"}).IsError());
	CHECK(ParseArgs({"--screenshot=captures/a.png"}).IsError()); // numéro d'image manquant
	CHECK(ParseArgs({"--screenshot=12:"}).IsError());            // chemin vide
}

TEST(Cli_ScreenshotPathMayContainColons) {
	auto parsed = ParseArgs({"--screenshot=5:C:/temp/a.png"});
	CHECK(parsed.IsOk());
	CHECK(parsed.Value().screenshots.size() == 1);
	CHECK_EQ(parsed.Value().screenshots[0].path, "C:/temp/a.png");
}

TEST(Cli_DefaultsAreUsable) {
	auto parsed = ParseArgs({});
	CHECK(parsed.IsOk());
	CHECK(!parsed.Value().headless);
	CHECK_EQ(parsed.Value().frames, 0L);
	CHECK_EQ(parsed.Value().theme, "studio");
}

// ============================================================================
// Document de projet
// ============================================================================

TEST(Project_JsonRoundTripPreservesEverything) {
	Project original = LoadDemoProject();
	original.activeScene = "Circuit";

	String encoded = original.EncodeJson();
	auto decoded = Project::DecodeJson(encoded);
	CHECK(decoded.IsOk());
	const Project &reloaded = decoded.Value();

	CHECK_EQ(reloaded.name, original.name);
	CHECK_EQ(reloaded.activeScene, "Circuit");
	CHECK(reloaded.documents.size() == original.documents.size());
	CHECK(reloaded.TotalNodeCount() == original.TotalNodeCount());

	for (size_t i = 0; i < original.documents.size(); ++i) {
		const SceneDesc &before = original.documents[i];
		const SceneDesc &after = reloaded.documents[i];
		CHECK_EQ(after.name, before.name);
		CHECK_EQ(after.gameplayScript, before.gameplayScript);
		CHECK(after.NodeCount() == before.NodeCount());
		std::vector<scene::NodeId> beforeIds = before.Nodes();
		std::vector<scene::NodeId> afterIds = after.Nodes();
		for (size_t j = 0; j < beforeIds.size(); ++j) {
			const scene::Node &a = *before.tree.Get(beforeIds[j]);
			const scene::Node &b = *after.tree.Get(afterIds[j]);
			CHECK_EQ(b.name, a.name);
			CHECK(b.type == a.type);
			CHECK(VisualDesc::Read(b).shape == VisualDesc::Read(a).shape);
			CHECK(b.transform.position == a.transform.position);
			CHECK(b.transform.scale == a.transform.scale);
			CHECK(PhysicsDesc::Read(b).body == PhysicsDesc::Read(a).body);
			CHECK(VisualDesc::Read(b).material.kind == VisualDesc::Read(a).material.kind);
			// La HIÉRARCHIE aussi doit survivre à l'aller-retour.
			CHECK(after.tree.PathOf(afterIds[j]) == before.tree.PathOf(beforeIds[j]));
		}
	}
}

TEST(Project_WholeNumberCoordinatesSurviveTheRoundTrip) {
	// Régression : l'encodeur JSON écrivait un FLOAT de valeur entière sans
	// point décimal, le décodeur le relisait en INT, et toute coordonnée
	// entière revenait à zéro. Corrigé dans data/json.hpp ; ce test verrouille
	// le comportement côté éditeur, où les coordonnées entières sont la norme.
	Project project;
	SceneDesc scene;
	scene.name = "Test";
	NodeDesc object;
	object.name = "Bloc";
	object.transform.position = {5.f, 0.f, -12.f};
	object.transform.scale = {2.f, 1.f, 3.f};
	object.physics.mass = 4.f;
	(void)scene.Add(std::move(object));
	project.documents.push_back(std::move(scene));
	project.activeScene = "Test";

	auto decoded = Project::DecodeJson(project.EncodeJson());
	CHECK(decoded.IsOk());
	const scene::Node *reloaded = decoded.Value().documents[0].Find("Bloc");
	CHECK(reloaded != nullptr);
	CHECK_EQ(reloaded->transform.position.x, 5.f);
	CHECK_EQ(reloaded->transform.position.z, -12.f);
	CHECK_EQ(reloaded->transform.scale.z, 3.f);
	CHECK_EQ(PhysicsDesc::Read(*reloaded).mass, 4.f);
}

// Régression : le script de jeu d'une scène est lu sous
// `scenes/../scripts/X.gameplay.script` ; réenregistrer le projet ne doit pas
// le prendre pour un orphelin et l'effacer juste après l'avoir écrit.
TEST(Project_ResavingKeepsGameplayScriptFiles) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "game_editor_resave_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	const String manifest((dir / "resave.json").string().c_str());
	Project project = files::MakeBlankProject(String("Resave"));
	project.documents.front().gameplayScript = String("class Vide extends Scene {\n    fn on_update(dt) { }\n}\n");
	CHECK(files::SaveProject(project, manifest).IsOk());
	files::FileList loaded;
	auto reloaded = files::LoadProject(manifest, &loaded);
	CHECK(reloaded.IsOk());
	CHECK(files::SaveProject(reloaded.Value(), manifest, loaded).IsOk());
	auto again = files::LoadProject(manifest);
	CHECK(again.IsOk());
	CHECK_EQ(again.Value().documents.front().gameplayScript, project.documents.front().gameplayScript);
	CHECK_EQ(files::NormalizePath(String("/a/scenes/../scripts/./x")), "/a/scripts/x");
	CHECK_EQ(files::NormalizePath(String("../b/../c")), "../c");
	std::filesystem::remove_all(dir);
}

TEST(Project_RejectsMalformedDocuments) {
	CHECK(Project::DecodeJson(String("pas du json")).IsError());
	CHECK(Project::DecodeJson(String("{}")).IsError());                       // pas de `scenes`
	CHECK(Project::DecodeJson(String("{\"scenes\": []}")).IsError());         // aucune scène
	CHECK(Project::DecodeJson(String("{\"scenes\": [{}]}")).IsError());       // scène sans nom
	// Version de format plus récente : refusée explicitement, pas lue de travers.
	CHECK(Project::DecodeJson(String("{\"version\": 9999, \"scenes\": [{\"name\": \"a\"}]}")).IsError());
}

TEST(Project_ActiveSceneFallsBackToTheFirstOne) {
	Project project = LoadDemoProject();
	project.activeScene = "Scène inexistante";
	CHECK(project.ActiveDocument() != nullptr);
	CHECK_EQ(project.ActiveDocument()->name, project.documents.front().name);
	CHECK(!project.Open(ProjectPath::Scene("Toujours inexistante")));
	CHECK(project.Open(ProjectPath::Scene("Circuit")));
	CHECK_EQ(project.ActiveDocument()->name, "Circuit");
	CHECK_EQ(project.ActivePath().ToString(), String("/scenes/Circuit"));
}

TEST(SceneDesc_UniqueNamesAndChildDetachOnRemove) {
	SceneDesc scene;
	scene.name = "S";

	NodeDesc parent;
	parent.name = "Boîte";
	CHECK_EQ(scene.Add(parent), "Boîte");
	CHECK_EQ(scene.Add(parent), "Boîte 2"); // même nom demandé -> suffixé
	CHECK_EQ(scene.Add(parent), "Boîte 3");

	NodeDesc child;
	child.name = "Enfant";
	child.parent = "Boîte";
	scene.Add(child);

	CHECK(scene.Find("Enfant") != nullptr);
	CHECK(scene.Find("Enfant")->parent == scene.FindId("Boîte"));

	// Supprimer le parent supprime son SOUS-ARBRE. C'est le changement de
	// comportement assumé par le passage à un vrai arbre : dans la liste
	// plate, l'enfant remontait à la racine — ce qui éparpillait les roues
	// d'une voiture supprimée dans toute la scène.
	CHECK(scene.Remove("Boîte"));
	CHECK(scene.Find("Boîte") == nullptr);
	CHECK(scene.Find("Enfant") == nullptr);
	CHECK(!scene.Remove("Boîte")); // déjà supprimé
}

TEST(TransformDesc_EulerAndQuaternionRoundTrip) {
	// Repose sur math::FQuaternion::ToEuler, ajouté pour l'inspecteur. Le
	// transform d'édition est désormais celui de la bibliothèque
	// (scene::Transform) : quaternion en mémoire, degrés à l'affichage.
	TransformDesc transform;
	transform.SetEulerDegrees({18.f, -47.f, 33.f});
	math::FQuaternion rotation = transform.rotation;

	TransformDesc restored;
	restored.rotation = rotation;
	CHECK(sdl3::Abs(restored.EulerDegrees().x - 18.f) < 0.01f);
	CHECK(sdl3::Abs(restored.EulerDegrees().y - -47.f) < 0.01f);
	CHECK(sdl3::Abs(restored.EulerDegrees().z - 33.f) < 0.01f);
}

/// Nom du i-ème objet de la scène, dans l'ordre d'affichage de l'arbre
/// (parcours préfixe, racine exclue) — remplace l'ancien `objects[i].name`
/// d'une liste plate. L'ordre est stable, donc les tests qui visaient
/// « le 2e objet de la vitrine » visent toujours le même.
[[nodiscard]] static String ObjectNameAt(const SceneDesc &scene, size_t index) {
	std::vector<scene::NodeId> objects = scene.Nodes();
	return index < objects.size() ? scene.tree.Get(objects[index])->name : String();
}

// ============================================================================
// Contenu livré
// ============================================================================

TEST(Content_DemoProjectIsWellFormed) {
	Project project = LoadDemoProject();
	CHECK(project.documents.size() == 5);

	for (const SceneDesc &scene : project.documents) {
		CHECK(!scene.name.IsEmpty());
		CHECK(!scene.description.IsEmpty());
		CHECK(!scene.gameplayScript.IsEmpty());
		CHECK(scene.NodeCount() > 0);

		for (scene::NodeId id : scene.Nodes()) {
			const scene::Node &object = *scene.tree.Get(id);
			CHECK(!object.name.IsEmpty());
			// Unicité entre FRÈRES — la règle d'un arbre, et tout ce dont un
			// chemin a besoin pour être sans ambiguïté. L'unicité GLOBALE
			// n'est qu'une politique de l'éditeur pour les objets créés à la
			// main (cf. SceneDesc::UniqueName) : la scène « Assemblages »
			// contient délibérément deux « Tête », une par personnage.
			int count = 0;
			for (scene::NodeId sibling : scene.tree.ChildrenOf(object.parent))
				if (scene.tree.Get(sibling)->name == object.name)
					++count;
			CHECK_EQ(count, 1);
			// Le parent doit toujours exister dans l'arbre.
			CHECK(scene.tree.Contains(object.parent));
		}
		// Et l'arbre lui-même doit être intègre (cycles, liens croisés,
		// références pendouillantes — cf. scene::NodeTree::Validate).
		CHECK(scene.tree.Validate().Ok());
	}
}

TEST(Content_EveryScriptOfTheProjectCompiles) {
	// Un script (fichier .script du projet) qui ne compile pas ne se verrait
	// qu'au moment de lancer le mode Jeu, dans une scène précise : autant
	// l'attraper ici — scripts de jeu des scènes ET bibliothèque.
	Project project = LoadDemoProject();
	std::vector<String> sources;
	for (const SceneDesc &scene : project.documents)
		if (!scene.gameplayScript.IsEmpty())
			sources.push_back(scene.gameplayScript);
	for (const ScriptAsset &script : project.scripts)
		sources.push_back(script.source);
	CHECK(sources.size() >= 8u);
	for (const String &source : sources) {
		auto program = script::Parser::Compile(source.View());
		if (program.IsError())
			test::ReportFailure(__FILE__, __LINE__, program.Error().Format().CStr());
		CHECK(program.IsOk());
	}
}

TEST(Content_CircuitHasAnOrderedRingOfCheckpoints) {
	Project project = LoadDemoProject();
	const SceneDesc *circuit = project.Fs().FindScene(SceneRef{"Circuit"});
	CHECK(circuit != nullptr);

	std::vector<scene::NodeId> checkpoints = circuit->WithTag(String("checkpoint"));
	CHECK(checkpoints.size() == 8);

	// Le pilote automatique vise chaque point EN LIGNE DROITE : l'écart entre
	// la corde et l'arc doit rester dans la demi-largeur de piste, sinon la
	// voiture coupe dans le décor. On vérifie donc que deux points
	// consécutifs sont assez proches pour que ce soit vrai.
	for (size_t i = 0; i + 1 < checkpoints.size(); ++i) {
		math::FVector3 a = circuit->tree.GlobalPosition(checkpoints[i]);
		math::FVector3 b = circuit->tree.GlobalPosition(checkpoints[i + 1]);
		float distance = (b - a).Length();
		CHECK(distance > 10.f);
		CHECK(distance < 30.f);
	}

	// La voiture est posée sur le premier point, et c'est elle que suit la
	// caméra du mode Jeu.
	const scene::Node *car = circuit->Find("Voiture");
	CHECK(car != nullptr);
	CHECK_EQ(TagOf(*car), "camera_target");
	CHECK(PhysicsDesc::Read(*car).body == BodyKind::DYNAMIC);
	CHECK((car->transform.position - circuit->tree.GlobalPosition(checkpoints[0])).Length() < 2.f);
}

// ============================================================================
// Runtime — instanciation et commandes
// ============================================================================

TEST(Runtime_InstantiatesTheActiveSceneOnly) {
	Harness harness;
	const SceneDesc *scene = harness.runtime.ActiveDocument();
	CHECK(scene != nullptr);
	CHECK_EQ(scene->name, "Vitrine");
	// Une entité de runtime par objet du document, et pas une de plus (les
	// autres scènes du projet ne sont PAS instanciées).
	CHECK(harness.runtime.RuntimeNodeCount() == scene->NodeCount());
	CHECK(harness.runtime.RigidBodyCount() > 0);
	CHECK(harness.runtime.RigidBodyCount() < scene->NodeCount());
}

TEST(Runtime_SwitchingSceneRebuildsEverything) {
	Harness harness;
	CHECK(!harness.runtime.OpenScene(SceneRef{"Scène inexistante"}));
	CHECK(harness.runtime.OpenScene(SceneRef{"Laboratoire physique"}));

	const SceneDesc *scene = harness.runtime.ActiveDocument();
	CHECK(scene != nullptr);
	CHECK(harness.runtime.RuntimeNodeCount() == scene->NodeCount());
	// Aucun reliquat de la scène précédente.
	CHECK(harness.runtime.Registry().EntitiesWith<SceneNodeRef>().size() == scene->NodeCount());
}

TEST(Runtime_EditCommandsWriteToDocumentAndRuntimeTogether) {
	Harness harness;
	const char *name = "Cube plastique";

	CHECK(harness.runtime.SetPosition(String(name), {3.f, 2.f, 1.f}));
	const scene::Node *object = harness.runtime.ActiveDocument()->Find(name);
	CHECK(object != nullptr);
	CHECK(object->transform.position == math::FVector3(3.f, 2.f, 1.f));

	CHECK(harness.runtime.SetEulerDegrees(String(name), {0.f, 90.f, 0.f}));
	CHECK(sdl3::Abs(harness.runtime.ActiveDocument()->Find(name)->transform.EulerDegrees().y - 90.f) < 0.01f);

	// L'échelle est bornée : une valeur nulle produirait une matrice
	// singulière (objet invisible, normales dégénérées).
	CHECK(harness.runtime.SetScale(String(name), {0.f, -5.f, 2.f}));
	CHECK(harness.runtime.ActiveDocument()->Find(name)->transform.scale.x > 0.f);
	CHECK(harness.runtime.ActiveDocument()->Find(name)->transform.scale.y > 0.f);
	CHECK_EQ(harness.runtime.ActiveDocument()->Find(name)->transform.scale.z, 2.f);

	CHECK(harness.runtime.SetMaterialColor(String(name), sdl3::Color{10, 20, 30, 255}));
	CHECK(VisualDesc::Read(*harness.runtime.ActiveDocument()->Find(name)).material.baseColor.r == 10);

	// Objet inexistant : `false`, jamais un plantage.
	CHECK(!harness.runtime.SetPosition(String("Néant"), {}));
	CHECK(!harness.runtime.SetMaterialColor(String("Néant"), sdl3::Color::WHITE()));
}

TEST(Runtime_PhysicsComponentFollowsTheDocument) {
	Harness harness;
	const String name("Cube plastique");
	CHECK(harness.runtime.GetVelocity(name).IsNone()); // pas de corps au départ

	PhysicsDesc physics;
	physics.body = BodyKind::DYNAMIC;
	physics.mass = 5.f;
	CHECK(harness.runtime.SetPhysics(name, physics));
	CHECK(harness.runtime.GetVelocity(name).IsSome());
	CHECK(harness.runtime.SetVelocity(name, {1.f, 2.f, 3.f}));
	CHECK(harness.runtime.GetVelocity(name).Unwrap().y == 2.f);

	// Repasser à « aucun corps » retire bien le composant.
	physics.body = BodyKind::NONE;
	CHECK(harness.runtime.SetPhysics(name, physics));
	CHECK(harness.runtime.GetVelocity(name).IsNone());
}

TEST(Runtime_SpawnRemoveAndRename) {
	Harness harness;
	size_t before = harness.runtime.ActiveDocument()->NodeCount();

	NodeDesc object;
	object.name = "Cube plastique"; // nom déjà pris -> doit être suffixé
	object.shape = ShapeKind::SPHERE;
	Option<String> assigned = harness.runtime.SpawnNamedNode(object);
	CHECK(assigned.IsSome());
	CHECK(assigned.Unwrap() != "Cube plastique");
	CHECK(harness.runtime.ActiveDocument()->NodeCount() == before + 1);
	CHECK(harness.runtime.RuntimeNodeCount() == before + 1);

	// Renommage : refusé vers un nom déjà pris, accepté sinon.
	CHECK(!harness.runtime.RenameNode(assigned.Unwrap(), String("Cube plastique")));
	CHECK(harness.runtime.RenameNode(assigned.Unwrap(), String("Nouvelle sphère")));
	CHECK(harness.runtime.ActiveDocument()->Find("Nouvelle sphère") != nullptr);

	CHECK(harness.runtime.RemoveNode(String("Nouvelle sphère")));
	CHECK(harness.runtime.ActiveDocument()->NodeCount() == before);
	CHECK(harness.runtime.RuntimeNodeCount() == before);
	CHECK(!harness.runtime.RemoveNode(String("Nouvelle sphère")));
}

TEST(Runtime_SelectionIsSingleAndSurvivesNothingElse) {
	Harness harness;
	CHECK(harness.runtime.SelectedName().IsNone());

	CHECK(harness.runtime.Select(String("Tore")));
	CHECK_EQ(harness.runtime.SelectedName().Unwrap(), "Tore");
	CHECK(harness.runtime.SelectedNode() != nullptr);

	// Sélectionner ailleurs REMPLACE la sélection (une seule à la fois).
	CHECK(harness.runtime.Select(String("Balise")));
	CHECK_EQ(harness.runtime.SelectedName().Unwrap(), "Balise");
	CHECK(harness.runtime.Registry().EntitiesWith<Selected>().size() == 1);

	CHECK(!harness.runtime.Select(String("Néant")));
	CHECK(harness.runtime.SelectedName().IsNone());

	// Supprimer l'objet sélectionné vide la sélection.
	CHECK(harness.runtime.Select(String("Tore")));
	CHECK(harness.runtime.RemoveNode(String("Tore")));
	CHECK(harness.runtime.SelectedName().IsNone());
}

// ============================================================================
// Runtime — mode Jeu
// ============================================================================

TEST(Runtime_PlayThenStopRestoresTheSceneExactly) {
	Harness harness;
	CHECK(harness.runtime.OpenScene(SceneRef{"Laboratoire physique"}));

	// Instantané = l'ARBRE entier (copie de valeur, cf. scene::NodeTree).
	const scene::NodeTree before = harness.runtime.ActiveDocument()->tree;
	harness.runtime.Play();
	CHECK(harness.runtime.IsPlaying());

	for (int i = 0; i < 150; ++i)
		harness.runtime.Update(1.f / 60.f);

	// La partie a réellement FAIT quelque chose — sinon « restaure à
	// l'identique » ne prouverait rien. Le script de cette scène largue des
	// caisses : la scène compte donc plus d'objets qu'au départ, et au moins
	// une caisse est déjà descendue sous son altitude d'apparition.
	const SceneDesc *playing = harness.runtime.ActiveDocument();
	CHECK(playing->NodeCount() > before.Size() - 1);
	bool fell = false;
	for (scene::NodeId crate : playing->WithTag(String("debris")))
		if (playing->tree.GlobalPosition(crate).y < 9.f)
			fell = true;
	CHECK(fell);

	harness.runtime.Stop();
	CHECK(!harness.runtime.IsPlaying());

	// Stop restaure l'instantané ENTIER : les caisses larguées pendant la
	// partie disparaissent avec elle, et le projet enregistré est intact.
	const scene::NodeTree &after = harness.runtime.ActiveDocument()->tree;
	CHECK(after.Size() == before.Size());
	CHECK(harness.runtime.ActiveDocument()->WithTag(String("debris")).empty());
	CHECK(harness.runtime.RuntimeNodeCount() == before.Size() - 1);
	// Égalité STRUCTURELLE de l'arbre : mêmes nœuds, mêmes liens, mêmes
	// transforms — une comparaison champ à champ ne dirait rien de la
	// hiérarchie, qui est justement ce que le mode Jeu pourrait abîmer.
	CHECK(after == before);
}

TEST(Runtime_PlayIsReproducible) {
	// Deux parties, même graine, même pas de temps : mêmes positions finales.
	// C'est ce qui permet à un rapport d'exécution d'être comparé d'un run à
	// l'autre, et à un scénario d'affirmer un nombre de tours.
	auto simulate = [](uint64_t seed) {
		ecs::ArchetypeRegistry registry;
		Runtime runtime(registry);
		runtime.SetRandomSeed(seed);
		runtime.OpenProject(LoadDemoProject());
		(void)runtime.OpenScene(SceneRef{"Circuit"});
		runtime.Play();
		for (int i = 0; i < 240; ++i)
			runtime.Update(1.f / 60.f);
		const scene::Node *car = runtime.ActiveDocument()->Find("Voiture");
		return car ? car->transform.position : math::FVector3{};
	};
	math::FVector3 first = simulate(1234);
	math::FVector3 second = simulate(1234);
	CHECK((first - second).Length() < 1e-4f);
}

TEST(Runtime_CircuitIsActuallyPlayable) {
	// L'essai de jouabilité : sous pilote automatique, la voiture doit boucler
	// au moins un tour complet en un temps raisonnable — et la partie doit
	// rester exempte d'erreur de script.
	Harness harness;
	CHECK(harness.runtime.OpenScene(SceneRef{"Circuit"}));
	harness.runtime.Play();
	CHECK(harness.runtime.IsPlaying());

	for (int i = 0; i < 900; ++i) // 15 s simulées
		harness.runtime.Update(1.f / 60.f);

	CHECK_EQ(int(harness.runtime.ScriptErrorCount()), 0);

	Option<script::Value> laps = harness.runtime.GameplayGlobal(String("total_laps"));
	CHECK(laps.IsSome());
	CHECK(laps.Unwrap().AsNumber() >= 1.0);

	// La voiture est restée SUR la piste : à ~30 unités du centre, et pas
	// tombée à travers le sol.
	const scene::Node *car = harness.runtime.ActiveDocument()->Find("Voiture");
	CHECK(car != nullptr);
	float radius = math::FVector2{car->transform.position.x, car->transform.position.z}.Length();
	CHECK(radius > 24.f);
	CHECK(radius < 36.f);
	CHECK(car->transform.position.y > -1.f);
}

TEST(Runtime_ShowcaseAnimationScriptDrivesObjects) {
	Harness harness;
	const scene::Node *beacon = harness.runtime.ActiveDocument()->Find("Balise");
	CHECK(beacon != nullptr);
	float restingHeight = beacon->transform.position.y;

	harness.runtime.Play();
	for (int i = 0; i < 40; ++i)
		harness.runtime.Update(1.f / 60.f);

	CHECK_EQ(int(harness.runtime.ScriptErrorCount()), 0);
	const scene::Node *animated = harness.runtime.ActiveDocument()->Find("Balise");
	CHECK(animated != nullptr);
	CHECK(sdl3::Abs(animated->transform.position.y - restingHeight) > 0.05f);
}

TEST(Runtime_ABrokenGameplayScriptIsDisabledInsteadOfSpamming) {
	// Un rappel qui échoue à 60 Hz noierait la console et le rapport sous des
	// milliers de lignes identiques : il doit être désactivé au premier échec.
	Harness harness;
	SceneDesc *scene = harness.runtime.ActiveDocument();
	CHECK(scene != nullptr);
	scene->gameplayScript = String("class Cassee extends Scene { fn on_update(dt) { return inexistant() } }");

	harness.runtime.Play();
	for (int i = 0; i < 50; ++i)
		harness.runtime.Update(1.f / 60.f);
	CHECK_EQ(int(harness.runtime.ScriptErrorCount()), 1);
}

// ============================================================================
// API hôte exposée aux scripts
// ============================================================================

TEST(ScriptApi_SpawnsAndEditsObjects) {
	Harness harness;
	size_t before = harness.runtime.ActiveDocument()->NodeCount();

	CHECK(harness.Run("for (i in range(0, 5)) {\n"
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

	const SceneDesc *scene = harness.runtime.ActiveDocument();
	CHECK(scene->NodeCount() == before + 5);
	CHECK(scene->WithTag(String("genere")).size() == 5);

	const scene::Node *third = scene->Find("Généré 2");
	CHECK(third != nullptr);
	CHECK(VisualDesc::Read(*third).shape == ShapeKind::SPHERE);
	CHECK(PhysicsDesc::Read(*third).body == BodyKind::DYNAMIC);
	CHECK_EQ(third->transform.position.x, 4.f);
	CHECK(VisualDesc::Read(*third).material.baseColor.r == 200);
	// Chaque objet créé par script existe AUSSI dans le runtime.
	CHECK(harness.runtime.RuntimeNodeCount() == before + 5);
}

TEST(ScriptApi_QueriesReturnNilForMissingObjects) {
	// Un script qui interroge un objet disparu doit pouvoir tester le
	// résultat, pas s'interrompre.
	Harness harness;
	CHECK(harness.Run("assert(node.position(\"Néant\") == nil)\n"
							"assert(node.velocity(\"Néant\") == nil)\n"
							"assert(node.distance(\"Néant\", \"Tore\") == nil)\n"
							"assert(scene.exists(\"Tore\"))\n"
							"assert(not scene.exists(\"Néant\"))"));
}

TEST(ScriptApi_PartialMaterialTableKeepsUntouchedFields) {
	Harness harness;
	const scene::Node *before = harness.runtime.ActiveDocument()->Find("Tore");
	CHECK(before != nullptr);
	sdl3::Color originalColor = VisualDesc::Read(*before).material.baseColor;

	CHECK(harness.Run("node.set_material(\"Tore\", {roughness: 0.11})"));

	const scene::Node *after = harness.runtime.ActiveDocument()->Find("Tore");
	CHECK(after != nullptr);
	const MaterialDesc material = VisualDesc::Read(*after).material;
	CHECK(sdl3::Abs(material.roughness - 0.11f) < 1e-5f);
	CHECK(material.baseColor.r == originalColor.r);
	CHECK(material.baseColor.g == originalColor.g);
}

TEST(ScriptApi_DrivesTheEditorItself) {
	Harness harness;
	CHECK(harness.Run("editor.select(\"Tore\")\n"
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

TEST(ScriptApi_HostHooksAreOptionalAndReturnFalseWhenAbsent) {
	// Le même scénario doit tourner avec ou sans écran : sans hôte graphique,
	// ces fonctions rendent `false` au lieu d'échouer.
	Harness harness;
	CHECK(harness.Run("assert(editor.screenshot(\"/tmp/rien.png\") == false)\n"
							"assert(editor.set_theme(\"light\") == false)\n"
							"assert(editor.open_panel(\"console\") == false)"));

	// ... et les utilisent dès qu'ils sont installés.
	bool captured = false;
	harness.runtime.onScreenshot = [&captured](const String &) {
		captured = true;
		return true;
	};
	CHECK(harness.Run("assert(editor.screenshot(\"/tmp/rien.png\"))"));
	CHECK(captured);
}

TEST(ScriptApi_PrintGoesToTheEditorConsole) {
	Harness harness;
	size_t before = harness.runtime.Log().size();
	CHECK(harness.Run("print(\"bonjour\")\neditor.log(\"deux\", 3)"));
	CHECK(harness.runtime.Log().size() == before + 2);
	CHECK_EQ(harness.runtime.Log()[before].text, "bonjour");
	CHECK_EQ(harness.runtime.Log()[before + 1].text, "deux 3");
	CHECK(harness.runtime.Log()[before].level == LogLevel::SCRIPT);
}

TEST(ScriptApi_DataCodecsAreAvailableToScripts) {
	// La couche `data::` vue depuis un script : c'est ce qui permet de lire un
	// .gltf ou un projet JSON depuis du script d'éditeur.
	Harness harness;
	CHECK(harness.Run("let doc = parse(\"json\", \"{\\\"meshes\\\": [1, 2, 3]}\")\n"
							"assert(len(doc.meshes) == 3)\n"
							"let text = encode(\"json\", {a: 1})\n"
							"assert(find(text, \"\\\"a\\\"\") >= 0)"));
}

// ============================================================================
// Import glTF
// ============================================================================

TEST(GltfImport_ImportsARealModelAsASceneObject) {
	Harness harness;
	size_t before = harness.runtime.ActiveDocument()->NodeCount();

	auto imported = harness.runtime.ImportGltf(String("assets/models/animals/Cow.gltf"));
	if (imported.IsError())
		test::ReportFailure(__FILE__, __LINE__, imported.Error().CStr());
	CHECK(imported.IsOk());

	const scene::Node *object = harness.runtime.ActiveDocument()->Find(imported.Value());
	CHECK(object != nullptr);
	CHECK(VisualDesc::Read(*object).shape == ShapeKind::MODEL);
	CHECK_EQ(VisualDesc::Read(*object).source, "assets/models/animals/Cow.gltf");
	CHECK_EQ(object->name, "Cow");
	// Le modèle est ramené à une taille exploitable : sans cet ajustement, un
	// import arrive soit invisible soit grand comme un mur.
	CHECK(object->transform.scale.x > 0.f);
	math::FVector3 fitted = VisualDesc::Read(*object).dimensions * object->transform.scale.x;
	float largest = sdl3::Max(sdl3::Max(fitted.x, fitted.y), fitted.z);
	CHECK(sdl3::Abs(largest - 3.f) < 0.1f);

	CHECK(harness.runtime.ActiveDocument()->NodeCount() == before + 1);
	CHECK(harness.runtime.RuntimeNodeCount() == before + 1);
}

TEST(GltfImport_SurvivesASaveLoadRoundTrip) {
	// Le document ne contient que le CHEMIN du modèle : après un aller-retour
	// JSON, la géométrie doit se recharger depuis le fichier.
	Harness harness;
	auto imported = harness.runtime.ImportGltf(String("assets/models/animals/Sheep.gltf"));
	CHECK(imported.IsOk());

	String encoded = harness.runtime.GetProject().EncodeJson();
	auto reloaded = Project::DecodeJson(encoded);
	CHECK(reloaded.IsOk());

	const scene::Node *object = reloaded.Value().ActiveDocument()->Find(imported.Value());
	CHECK(object != nullptr);
	CHECK(VisualDesc::Read(*object).shape == ShapeKind::MODEL);
	CHECK_EQ(VisualDesc::Read(*object).source, "assets/models/animals/Sheep.gltf");

	// Et le runtime sait bien reconstruire ce maillage à partir du chemin.
	auto mesh = Runtime::LoadGltfMesh(VisualDesc::Read(*object).source);
	CHECK(mesh.IsOk());
	CHECK(mesh.Value().Vertices().size() > 0);
}

TEST(GltfImport_MissingOrInvalidModelIsAnErrorNotACrash) {
	Harness harness;
	CHECK(harness.runtime.ImportGltf(String("assets/models/inexistant.gltf")).IsError());
	CHECK(Runtime::LoadGltfMesh(String()).IsError());
	// Un `.bin` n'est pas du JSON : erreur de décodage, pas de plantage.
	CHECK(Runtime::LoadGltfMesh(String("assets/models/animals/Cow.bin")).IsError());
}

TEST(GltfImport_ScriptsCanImportAndInspectModels) {
	Harness harness;
	CHECK(harness.Run("let info = scene.model_info(\"assets/models/animals/Horse.gltf\")\n"
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

TEST(Scenarios_AllAreDeclaredAndCompile) {
	std::vector<Scenario> scenarios = BuiltinScenarios();
	CHECK(scenarios.size() >= 5);
	for (const Scenario &scenario : scenarios) {
		CHECK(scenario.suggestedFrames > 0);
		CHECK(!String(scenario.description).IsEmpty());
		auto program = script::Parser::Compile(StringView(scenario.source));
		if (program.IsError())
			test::ReportFailure(__FILE__, __LINE__,
								String::Format("scénario `%s` : %s", scenario.name,
											   program.Error().Format().CStr())
									.CStr());
		CHECK(program.IsOk());
	}
	CHECK(FindScenario(String("tour")).IsSome());
	CHECK(FindScenario(String("inexistant")).IsNone());
}

TEST(Scenarios_SmokeScenarioVisitsEverySceneWithoutError) {
	// Le scénario d'intégration continue, joué ici exactement comme le ferait
	// `--headless --scenario=smoke`.
	Harness harness;
	harness.runtime.ToolVm().SetGlobal(String("shot_dir"), script::Value::Str(String("/tmp")));
	Option<Scenario> smoke = FindScenario(String("smoke"));
	CHECK(smoke.IsSome());
	CHECK(harness.Run(smoke.Unwrap().source));

	for (long frame = 1; frame <= 150; ++frame) {
		harness.runtime.CallToolHook(String("on_frame"), {script::Value::Number(double(frame))});
		harness.runtime.Update(1.f / 60.f);
	}
	// Les `assert` du scénario remontent comme erreurs de script : zéro erreur
	// signifie que chaque scène s'est chargée et jouée.
	CHECK_EQ(int(harness.runtime.ScriptErrorCount()), 0);
}

// ============================================================================
// Historique — annuler / rétablir
// ============================================================================

TEST(History_UndoAndRedoRestoreTheDocumentExactly) {
	Harness h;
	h.runtime.ClearHistory();
	const String name = ObjectNameAt(*h.runtime.ActiveDocument(), 1);
	const math::FVector3 start = h.runtime.ActiveDocument()->Find(name)->transform.position;

	CHECK(!h.runtime.CanUndo()); // rien n'a encore été édité
	CHECK(h.runtime.SetPosition(name, start + math::FVector3{2.f, 0.f, 0.f}));
	CHECK(h.runtime.CanUndo());
	CHECK(h.runtime.UndoLabel().Contains(name));

	CHECK(h.runtime.Undo());
	CHECK(h.runtime.ActiveDocument()->Find(name)->transform.position.Distance(start) < 1e-4f);
	CHECK(h.runtime.CanRedo());
	CHECK(h.runtime.Redo());
	CHECK(sdl3::Abs(h.runtime.ActiveDocument()->Find(name)->transform.position.x - (start.x + 2.f)) < 1e-4f);
	// Une nouvelle édition abandonne la branche rétablissable.
	CHECK(h.runtime.Undo());
	CHECK(h.runtime.SetPosition(name, start + math::FVector3{0.f, 5.f, 0.f}));
	CHECK(!h.runtime.CanRedo());
}

TEST(History_UndoBringsBackADeletedObjectWithItsChildren) {
	Harness h;
	h.runtime.ClearHistory();
	const size_t before = h.runtime.ActiveDocument()->NodeCount();
	const String name = ObjectNameAt(*h.runtime.ActiveDocument(), 2);

	CHECK(h.runtime.RemoveNode(name));
	CHECK_EQ(h.runtime.ActiveDocument()->NodeCount(), before - 1);
	CHECK(h.runtime.Undo());
	CHECK_EQ(h.runtime.ActiveDocument()->NodeCount(), before);
	CHECK(h.runtime.ActiveDocument()->Find(name) != nullptr);
	// L'objet est de nouveau instancié, pas seulement présent dans le document.
	CHECK_EQ(h.runtime.RuntimeNodeCount(), before);
}

TEST(History_PlayModeDoesNotPolluteTheHistory) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{String("Laboratoire physique")}));
	h.runtime.ClearHistory();
	h.runtime.Play();
	for (int i = 0; i < 30; ++i)
		h.runtime.Update(1.f / 60.f);
	h.runtime.Stop();
	// La physique écrit dans le document à chaque image : si elle était
	// enregistrée, annuler deviendrait inutilisable.
	CHECK_EQ(h.runtime.UndoDepth(), size_t(0));
}

TEST(History_ForgetsTheOldestStepsPastItsLimit) {
	Harness h;
	h.runtime.ClearHistory();
	const String name = ObjectNameAt(*h.runtime.ActiveDocument(), 1);
	for (size_t i = 0; i < Runtime::MAX_HISTORY + 10; ++i)
		CHECK(h.runtime.SetPosition(name, math::FVector3{float(i), 0.f, 0.f}));
	CHECK_EQ(h.runtime.UndoDepth(), Runtime::MAX_HISTORY);
}

// ============================================================================
// Sélection par rayon et manipulateur
// ============================================================================

TEST(Picking_ClickingTheCentreOfTheViewportSelectsWhatIsAimedAt) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveDocument(), 1); // « Cube plastique », plein
	CHECK(h.runtime.FocusOn(name)); // la caméra vise l'objet

	const math::FRay ray = h.runtime.ViewportRay(400.f, 300.f, 800.f, 600.f);
	Option<Runtime::PickHit> hit = h.runtime.PickAt(ray);
	CHECK(hit.IsSome());
	CHECK_EQ(hit.Value().name, name);
	CHECK(hit.Value().distance > 0.f);

	CHECK(h.runtime.SelectAt(ray));
	CHECK(h.runtime.SelectedName().IsSome());
	CHECK_EQ(h.runtime.SelectedName().Unwrap(), name);

	// Un rayon qui part à l'opposé ne touche rien et désélectionne.
	const math::FRay away{h.runtime.ActiveCamera().position, h.runtime.ActiveCamera().position -
															   h.runtime.ActiveDocument()->Find(name)->transform.position};
	CHECK(!h.runtime.SelectAt(away));
	CHECK(h.runtime.SelectedName().IsNone());
}

/// La sélection teste les TRIANGLES, pas une boîte englobante : viser le trou
/// d'un tore ne le sélectionne donc pas, le rayon passe au travers.
TEST(Picking_AimingThroughTheHoleOfATorusMissesIt) {
	Harness h;
	CHECK(h.runtime.FocusOn(String("Tore")));
	Option<Runtime::PickHit> hit = h.runtime.PickAt(h.runtime.ViewportRay(400.f, 300.f, 800.f, 600.f));
	CHECK(hit.IsSome());
	CHECK(hit.Value().name != "Tore"); // ce qui est derrière, à travers le trou

	// Visé sur son anneau (décalé vers le haut), il est bien touché.
	Option<Runtime::PickHit> onRing = h.runtime.PickAt(h.runtime.ViewportRay(400.f, 232.f, 800.f, 600.f));
	CHECK(onRing.IsSome());
	CHECK_EQ(onRing.Value().name, "Tore");
}

TEST(Picking_HiddenObjectsAreNotSelectable) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveDocument(), 1);
	CHECK(h.runtime.FocusOn(name));
	const math::FRay ray = h.runtime.ViewportRay(400.f, 300.f, 800.f, 600.f);
	CHECK(h.runtime.PickAt(ray).IsSome());

	CHECK(h.runtime.SetVisible(name, false));
	Option<Runtime::PickHit> hit = h.runtime.PickAt(ray);
	CHECK(hit.IsNone() || hit.Value().name != name);
}

TEST(Gizmo_DragMovesRotatesAndScalesAlongOneAxis) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveDocument(), 3);
	CHECK(h.runtime.Select(name));
	const TransformDesc start = h.runtime.ActiveDocument()->Find(name)->transform;

	h.runtime.SetGizmoMode(Runtime::GizmoMode::TRANSLATE);
	CHECK(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::X, 0.f, 3.f));
	const TransformDesc moved = h.runtime.ActiveDocument()->Find(name)->transform;
	CHECK(sdl3::Abs(moved.position.x - (start.position.x + 3.f)) < 0.05f);
	CHECK(sdl3::Abs(moved.position.y - start.position.y) < 1e-4f); // un seul axe bouge
	CHECK(sdl3::Abs(moved.position.z - start.position.z) < 1e-4f);

	h.runtime.SetGizmoMode(Runtime::GizmoMode::ROTATE);
	CHECK(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::Y, 0.f, 45.f));
	const TransformDesc turned = h.runtime.ActiveDocument()->Find(name)->transform;
	CHECK(sdl3::Abs(turned.EulerDegrees().y - (start.EulerDegrees().y + 45.f)) < 1.5f);

	h.runtime.SetGizmoMode(Runtime::GizmoMode::SCALE);
	CHECK(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::Z, 0.f, 0.5f));
	CHECK(h.runtime.ActiveDocument()->Find(name)->transform.scale.z > start.scale.z);
}

TEST(Gizmo_AWholeDragIsASingleUndoStep) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveDocument(), 3);
	CHECK(h.runtime.Select(name));
	const math::FVector3 start = h.runtime.ActiveDocument()->Find(name)->transform.position;
	h.runtime.ClearHistory();

	CHECK(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::X, 0.f, 4.f));
	// Le glissé émet une commande par pas de souris : elles doivent former UNE
	// étape, sinon annuler demanderait autant de Ctrl+Z que de pixels parcourus.
	CHECK_EQ(h.runtime.UndoDepth(), size_t(1));
	CHECK(h.runtime.Undo());
	CHECK(h.runtime.ActiveDocument()->Find(name)->transform.position.Distance(start) < 1e-4f);
}

TEST(Gizmo_SnappingAlignsOnTheGridWithoutMovingTheOtherAxes) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveDocument(), 3);
	CHECK(h.runtime.Select(name));
	const math::FVector3 start = h.runtime.ActiveDocument()->Find(name)->transform.position;

	h.runtime.SetGizmoMode(Runtime::GizmoMode::TRANSLATE);
	h.runtime.SetSnapEnabled(true);
	h.runtime.SetSnapSteps(1.f, 15.f, 0.25f);
	CHECK(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::Z, 0.f, 2.37f));

	const math::FVector3 end = h.runtime.ActiveDocument()->Find(name)->transform.position;
	CHECK(sdl3::Abs(end.z - sdl3::Round(end.z)) < 1e-3f); // sur la grille
	CHECK(sdl3::Abs(end.x - start.x) < 1e-4f);            // les autres axes n'ont pas bougé
	CHECK(sdl3::Abs(end.y - start.y) < 1e-4f);

	// Rotation magnétique : multiple du pas.
	h.runtime.SetGizmoMode(Runtime::GizmoMode::ROTATE);
	CHECK(h.runtime.ScriptedGizmoDrag(Runtime::GizmoAxis::Y, 0.f, 38.f));
	const float angle = h.runtime.ActiveDocument()->Find(name)->transform.EulerDegrees().y;
	CHECK(sdl3::Abs(angle / 15.f - sdl3::Round(angle / 15.f)) < 1e-3f);
}

TEST(Gizmo_AxisPickingRequiresAimingAtTheHandle) {
	Harness h;
	const String name = ObjectNameAt(*h.runtime.ActiveDocument(), 3);
	CHECK(h.runtime.Select(name));
	h.runtime.SetGizmoMode(Runtime::GizmoMode::TRANSLATE);
	const math::FVector3 origin = h.runtime.ActiveDocument()->Find(name)->transform.position;
	const float size = h.runtime.GizmoSize();
	CHECK(size > 0.f);

	// Rayon qui traverse le milieu de la flèche X : attrapé.
	const math::FVector3 onAxis = origin + math::FVector3{size * 0.6f, 0.f, 0.f};
	const math::FRay hitRay{onAxis + math::FVector3{0.f, 4.f, 0.f}, {0.f, -1.f, 0.f}};
	CHECK(h.runtime.PickGizmoAxis(hitRay) == Runtime::GizmoAxis::X);

	// Rayon qui passe loin à côté : rien.
	const math::FRay missRay{origin + math::FVector3{0.f, 4.f, size * 6.f}, {0.f, -1.f, 0.f}};
	CHECK(h.runtime.PickGizmoAxis(missRay) == Runtime::GizmoAxis::NONE);

	// Sans sélection, il n'y a pas de manipulateur à attraper.
	h.runtime.ClearSelection();
	CHECK(h.runtime.PickGizmoAxis(hitRay) == Runtime::GizmoAxis::NONE);
}

TEST(Gizmo_ScriptsDriveSelectionAndTheGizmo) {
	Harness h;
	CHECK(h.Run(R"SCRIPT(
        editor.select("Tore")
        editor.focus("Tore")
        assert(editor.gizmo_mode("rotate") == "rotate", "mode non appliqué")
        let avant = node.rotation("Tore")
        assert(editor.gizmo_drag("y", 0, 90), "glissé refusé")
        let apres = node.rotation("Tore")
        assert(apres[1] > avant[1] + 80, "rotation insuffisante")
        assert(editor.undo(), "rien à annuler")
        let retour = node.rotation("Tore")
        assert(retour[1] < avant[1] + 0.01, "l'annulation n'a pas rendu l'angle d'origine")
        let touche = editor.select_at(400, 300, 800, 600)
        assert(touche != nil, "aucun objet sous le centre du viewport")
    )SCRIPT"));
}

// ============================================================================
// Caméra — navigation libre dans la vue
// ============================================================================

TEST(Camera_DollyMovesAlongTheViewAxisOnly) {
	Harness h;
	const math::FVector3 start = h.runtime.ActiveCamera().position;
	const math::FVector3 forward = h.runtime.CameraForward();

	h.runtime.DollyCamera(5.f);
	const math::FVector3 end = h.runtime.ActiveCamera().position;
	// Exactement 5 unités, exactement sur l'axe de visée.
	CHECK(sdl3::Abs((end - start).Length() - 5.f) < 1e-3f);
	CHECK((end - (start + forward * 5.f)).Length() < 1e-3f);
	// La direction de visée, elle, n'a pas bougé.
	CHECK((h.runtime.CameraForward() - forward).Length() < 1e-5f);

	h.runtime.DollyCamera(-5.f);
	CHECK((h.runtime.ActiveCamera().position - start).Length() < 1e-3f);
}

TEST(Camera_PanSlidesInTheScreenPlaneNotAlongTheView) {
	Harness h;
	const math::FVector3 start = h.runtime.ActiveCamera().position;
	const math::FVector3 forward = h.runtime.CameraForward();
	const math::FVector3 right = h.runtime.CameraRight();
	const math::FVector3 up = h.runtime.CameraUp();

	h.runtime.PanCamera(3.f, 2.f);
	const math::FVector3 delta = h.runtime.ActiveCamera().position - start;
	CHECK((delta - (right * 3.f + up * 2.f)).Length() < 1e-3f);
	// Rien le long de la visée : un panoramique ne rapproche pas du décor.
	CHECK(sdl3::Abs(delta.Dot(forward)) < 1e-3f);
	// Les axes sont orthonormés : le repère de la caméra est cohérent.
	CHECK(sdl3::Abs(right.Dot(up)) < 1e-5f);
	CHECK(sdl3::Abs(right.Dot(forward)) < 1e-5f);
	CHECK(sdl3::Abs(up.Length() - 1.f) < 1e-5f);
}

TEST(Camera_OrbitKeepsTheDistanceAndKeepsLookingAtThePivot) {
	Harness h;
	CHECK(h.runtime.Select(String("Cristal")));
	CHECK(h.runtime.FocusOn(String("Cristal")));
	const math::FVector3 pivot = h.runtime.CameraPivot();
	const float radius = (h.runtime.ActiveCamera().position - pivot).Length();
	const math::FVector3 start = h.runtime.ActiveCamera().position;

	for (int i = 0; i < 12; ++i)
		h.runtime.OrbitCamera(0.13f, 0.f, pivot);

	const math::FVector3 end = h.runtime.ActiveCamera().position;
	CHECK((end - start).Length() > 1.f);                          // la caméra a tourné
	CHECK(sdl3::Abs((end - pivot).Length() - radius) < 1e-2f);    // sans s'éloigner
	// … et elle regarde toujours le pivot.
	const math::FVector3 toPivot = (pivot - end).Normalize();
	CHECK((h.runtime.CameraForward() - toPivot).Length() < 1e-3f);
}

TEST(Camera_OrbitDownRaisesTheCameraAndCannotFlipOver) {
	Harness h;
	CHECK(h.runtime.Select(String("Cristal")));
	CHECK(h.runtime.FocusOn(String("Cristal")));
	const math::FVector3 pivot = h.runtime.CameraPivot();
	const float startHeight = h.runtime.ActiveCamera().position.y;
	const float radius = (h.runtime.ActiveCamera().position - pivot).Length();

	// Tangage négatif = vue plongeante : la caméra monte au-dessus du pivot.
	for (int i = 0; i < 8; ++i)
		h.runtime.OrbitCamera(0.f, -0.09f, pivot);
	CHECK(h.runtime.ActiveCamera().position.y > startHeight + 1.f);
	CHECK(h.runtime.CameraForward().y < 0.f); // elle regarde vers le bas

	// Bornes : même en insistant, la vue ne passe jamais la verticale
	// (au-delà, elle se retournerait), et la distance au pivot tient.
	for (int i = 0; i < 200; ++i)
		h.runtime.OrbitCamera(0.f, -0.2f, pivot);
	CHECK(h.runtime.CameraUp().y > 0.f);
	CHECK(h.runtime.CameraForward().y < 0.f);
	CHECK(sdl3::Abs((h.runtime.ActiveCamera().position - pivot).Length() - radius) < 1e-2f);
}

TEST(Camera_PivotIsTheSelectionOtherwiseWhatIsInFront) {
	Harness h;
	CHECK(h.runtime.Select(String("Cristal")));
	const math::FVector3 selected = h.runtime.ActiveDocument()->Find(String("Cristal"))->transform.position;
	CHECK((h.runtime.CameraPivot() - selected).Length() < 1e-4f);

	h.runtime.ClearSelection();
	// Sans sélection : un point droit devant, donc sur l'axe de visée.
	const math::FVector3 pivot = h.runtime.CameraPivot();
	const math::FVector3 toPivot = pivot - h.runtime.ActiveCamera().position;
	CHECK((toPivot.Normalize() - h.runtime.CameraForward()).Length() < 1e-3f);
	CHECK(h.runtime.PivotDistance() > 0.5f);
}

TEST(Camera_FlyMovesForwardRightAndAlongWorldUp) {
	Harness h;
	const math::FVector3 start = h.runtime.ActiveCamera().position;
	const math::FVector3 forward = h.runtime.CameraForward();
	const math::FVector3 right = h.runtime.CameraRight();

	h.runtime.MoveCamera({2.f, 3.f, 4.f});
	const math::FVector3 delta = h.runtime.ActiveCamera().position - start;
	// `y` suit le HAUT DU MONDE (monter reste monter, même en piquant du nez).
	CHECK((delta - (forward * 4.f + right * 2.f + math::FVector3{0.f, 3.f, 0.f})).Length() < 1e-3f);
}

TEST(Camera_ScriptsDriveEveryCameraMove) {
	Harness h;
	CHECK(h.Run(R"SCRIPT(
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
    )SCRIPT"));
}

// ============================================================================
// Rapport d'exécution
// ============================================================================

TEST(Report_FrameStatsExcludeTheFirstFrame) {
	FrameStats stats;
	stats.Push(0.5); // première image (compilation des pipelines) : hors extrêmes
	stats.Push(1.0 / 60.0);
	stats.Push(1.0 / 30.0);
	stats.Push(1.0 / 60.0);

	CHECK_EQ(stats.Count(), 4L);
	CHECK(sdl3::Abs(stats.FirstFrameSeconds() - 0.5) < 1e-9);
	// Image la plus LONGUE (1/30 s) => cadence MINIMALE (30 img/s).
	CHECK(sdl3::Abs(stats.MinFps() - 30.0) < 0.01);
	CHECK(sdl3::Abs(stats.MaxFps() - 60.0) < 0.01);
	CHECK(stats.AverageFps() > 30.0 && stats.AverageFps() < 60.0);
	CHECK(stats.PercentileMs(99.0) >= stats.PercentileMs(50.0));
}

TEST(Report_ThreadTrackerCountsTheConcurrentPeak) {
	ThreadTracker tracker;
	CHECK_EQ(tracker.Peak(), 1); // le fil principal compte pour 1
	{
		ThreadTracker::Scope first(tracker);
		CHECK_EQ(tracker.Live(), 2);
		{
			ThreadTracker::Scope second(tracker);
			CHECK_EQ(tracker.Live(), 3);
		}
		CHECK_EQ(tracker.Live(), 2);
	}
	CHECK_EQ(tracker.Live(), 1);
	CHECK_EQ(tracker.Peak(), 3);       // le maximum SIMULTANÉ est retenu
	CHECK_EQ(tracker.TotalStarted(), 3);
}

TEST(Report_TextAndJsonCarryTheSameFacts) {
	Project project = LoadDemoProject();
	RunReport report;
	report.commandLine = "sandbox --scenario=tour";
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
	CHECK(text.Contains("RAPPORT D'EXÉCUTION"));
	CHECK(text.Contains("Vitrine"));
	CHECK(text.Contains("Circuit"));
	CHECK(text.Contains("captures/a.png"));
	CHECK(text.Contains("total_laps"));
	CHECK(text.Contains("Fils simultanés"));

	// Le JSON doit être RELISIBLE : c'est ce qu'un test d'intégration
	// interroge.
	String json = report.ToJson(project);
	dataio::JsonDocument document;
	auto error = document.DecodeStr(json);
	CHECK(error.IsNone());
	dataio::NodePtr root = document.GetRoot();
	CHECK(root && root->IsObject());
	CHECK_EQ(root->Get("format")->stringValue, "sandbox.report");
	CHECK(root->Get("run")->Get("completed")->boolValue);
	CHECK(root->Get("performance")->Get("threads_peak_concurrent")->intValue == 2);
	CHECK(root->Get("project")->Get("scene_count")->intValue == 5);
	CHECK(root->Get("screenshots")->GetSize() == 1);
	CHECK(root->Get("play_sessions")->At(0)->Get("scene")->stringValue == "Circuit");
	// Les objets de chaque scène sont listés, avec leur position.
	dataio::NodePtr firstScene = root->Get("project")->Get("scenes")->At(0);
	CHECK(firstScene && firstScene->Get("objects")->GetSize() > 0);
	CHECK(firstScene->Get("objects")->At(0)->Get("position")->GetSize() == 3);
}

TEST(Report_CollectsWarningsAndErrorsFromTheEditorLog) {
	Harness harness;
	harness.runtime.LogWarning(String("attention"));
	harness.runtime.LogError(String("échec"));
	harness.runtime.LogInfo(String("juste une info"));

	RunReport report;
	report.CollectDiagnostics(harness.runtime);
	CHECK(report.warnings.size() == 1);
	CHECK(report.errors.size() == 1);
	CHECK(report.warnings[0].Contains("attention"));
	CHECK(report.errors[0].Contains("échec"));
}

// ============================================================================
// Hiérarchie de nœuds — l'éditeur au-dessus de scene::NodeTree
// ============================================================================

TEST(Hierarchy_GroupsCanBeBuiltAndChildrenFollowTheirParent) {
	Harness h;
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Voiture"));
	CHECK(group.IsSome());

	NodeDesc wheel;
	wheel.name = "Roue";
	wheel.shape = ShapeKind::CYLINDER;
	wheel.transform.position = {1.f, 0.f, 0.f};
	Option<scene::NodeId> child = h.runtime.SpawnNode(wheel, group.Unwrap());
	CHECK(child.IsSome());

	const SceneDesc *scene = h.runtime.ActiveDocument();
	CHECK(scene->tree.ParentOf(child.Unwrap()) == group.Unwrap());
	CHECK(scene->tree.PathOf(child.Unwrap()).EndsWith("/Voiture/Roue"));

	// Déplacer le PARENT déplace l'enfant dans le monde, sans toucher à son
	// transform local — c'est tout l'intérêt de la hiérarchie.
	CHECK(h.runtime.SetPosition(group.Unwrap(), {10.f, 0.f, 5.f}));
	CHECK((scene->tree.GlobalPosition(child.Unwrap()) - math::FVector3{11.f, 0.f, 5.f}).Length() < 1e-4f);
	CHECK(scene->tree.Get(child.Unwrap())->transform.position.x == 1.f);

	// Et le nœud de RENDU suit. La synchronisation ECS -> Object3D a lieu
	// une fois par image (cf. Runtime::Update) et non à chaque commande : on
	// avance donc d'une image avant de lire la matrice monde du nœud 3D.
	h.runtime.Update(1.f / 60.f);
	render3d::Object3D *object3d = h.runtime.FindObject3D(child.Unwrap());
	CHECK(object3d != nullptr);
	math::FMatrix4 world = object3d->WorldMatrix();
	CHECK(sdl3::Abs(world.m[12] - 11.f) < 1e-3f);
	CHECK(sdl3::Abs(world.m[14] - 5.f) < 1e-3f);
}

TEST(Hierarchy_ReparentKeepsTheObjectWhereItIsOnScreen) {
	Harness h;
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Groupe"));
	CHECK(group.IsSome());
	CHECK(h.runtime.SetPosition(group.Unwrap(), {20.f, 3.f, -7.f}));

	scene::NodeId cube = h.runtime.ResolveId(String("Cube plastique"));
	CHECK(cube.Valid());
	const SceneDesc *scene = h.runtime.ActiveDocument();
	const math::FVector3 before = scene->tree.GlobalPosition(cube);

	CHECK(h.runtime.ReparentNode(cube, group.Unwrap()));
	CHECK(scene->tree.ParentOf(cube) == group.Unwrap());
	CHECK((scene->tree.GlobalPosition(cube) - before).Length() < 1e-3f);

	// En mode « local », le nœud SUIT son nouveau parent (utile pour poser
	// une roue à la même place relative dans chaque voiture).
	scene::NodeId torus = h.runtime.ResolveId(String("Tore"));
	CHECK(h.runtime.ReparentNode(torus, group.Unwrap(), scene::ReparentMode::KEEP_LOCAL));
	CHECK((scene->tree.GlobalPosition(torus) - scene->tree.Get(torus)->transform.position).Length() > 1.f);
}

TEST(Hierarchy_ACycleIsRefusedAndLeavesNoUndoStep) {
	Harness h;
	Option<scene::NodeId> parent = h.runtime.CreateGroup(String("Parent"));
	CHECK(parent.IsSome());
	Option<scene::NodeId> child = h.runtime.CreateGroup(String("Enfant"), parent.Unwrap());
	CHECK(child.IsSome());

	const size_t undoBefore = h.runtime.UndoDepth();
	CHECK(!h.runtime.ReparentNode(parent.Unwrap(), child.Unwrap()));
	// Refusé ET sans trace : une étape d'annulation qui ne défait rien
	// désoriente plus qu'elle n'aide.
	CHECK_EQ(h.runtime.UndoDepth(), undoBefore);
	CHECK(h.runtime.ActiveDocument()->tree.ParentOf(child.Unwrap()) == parent.Unwrap());
	CHECK(h.runtime.ActiveDocument()->tree.Validate().Ok());
}

TEST(Hierarchy_DeletingAParentDeletesItsWholeSubtree) {
	Harness h;
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Véhicule"));
	CHECK(group.IsSome());
	NodeDesc part;
	part.name = "Carrosserie";
	Option<scene::NodeId> body = h.runtime.SpawnNode(part, group.Unwrap());
	CHECK(body.IsSome());
	part.name = "Phare";
	Option<scene::NodeId> light = h.runtime.SpawnNode(part, body.Unwrap());
	CHECK(light.IsSome());

	const size_t before = h.runtime.ActiveDocument()->NodeCount();
	CHECK(h.runtime.RemoveNode(group.Unwrap()));
	CHECK_EQ(h.runtime.ActiveDocument()->NodeCount(), before - 3);
	CHECK(h.runtime.ActiveDocument()->Find("Phare") == nullptr);
	// Le runtime aussi : pas d'entité ni de nœud 3D orphelin.
	CHECK_EQ(h.runtime.RuntimeNodeCount(), h.runtime.ActiveDocument()->NodeCount());

	// …et l'annulation restitue l'arbre ENTIER, petits-enfants compris.
	CHECK(h.runtime.Undo());
	CHECK_EQ(h.runtime.ActiveDocument()->NodeCount(), before);
	CHECK(h.runtime.ActiveDocument()->Find("Phare") != nullptr);
	CHECK(h.runtime.ActiveDocument()->tree.Validate().Ok());
}

TEST(Hierarchy_DuplicateCopiesTheWholeSubtreeWithUniqueNames) {
	Harness h;
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Voiture"));
	CHECK(group.IsSome());
	NodeDesc wheel;
	wheel.name = "Roue";
	CHECK(h.runtime.SpawnNode(wheel, group.Unwrap()).IsSome());

	const size_t before = h.runtime.ActiveDocument()->NodeCount();
	Option<scene::NodeId> copy = h.runtime.DuplicateNode(group.Unwrap());
	CHECK(copy.IsSome());
	const SceneDesc *scene = h.runtime.ActiveDocument();
	CHECK_EQ(scene->NodeCount(), before + 2); // le groupe ET sa roue
	CHECK(scene->tree.ChildrenOf(copy.Unwrap()).size() == 1);
	// Les noms restent uniques dans la scène (les scripts désignent par nom).
	CHECK(scene->tree.Get(copy.Unwrap())->name != String("Voiture"));
	CHECK(scene->tree.Validate().Ok());

	CHECK(h.runtime.Undo());
	CHECK_EQ(h.runtime.ActiveDocument()->NodeCount(), before);
}

TEST(Hierarchy_AParentedBodyIsSimulatedAtItsWorldPosition) {
	// Le bug que la hiérarchie rend possible : la physique travaille en
	// MONDE, le document en LOCAL. Un corps enfant doit donc être créé à sa
	// position monde, et la simulation recopiée en local.
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Laboratoire physique"}));
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Support"));
	CHECK(group.IsSome());
	CHECK(h.runtime.SetPosition(group.Unwrap(), {0.f, 12.f, 0.f}));

	NodeDesc crate;
	crate.name = "Caisse suspendue";
	crate.physics.body = BodyKind::DYNAMIC;
	crate.physics.mass = 2.f;
	crate.transform.position = {0.f, 0.f, 0.f}; // local : à l'origine DU SUPPORT
	Option<scene::NodeId> id = h.runtime.SpawnNode(crate, group.Unwrap());
	CHECK(id.IsSome());

	const SceneDesc *scene = h.runtime.ActiveDocument();
	CHECK(sdl3::Abs(scene->tree.GlobalPosition(id.Unwrap()).y - 12.f) < 1e-4f);

	h.runtime.Play();
	for (int i = 0; i < 30; ++i)
		h.runtime.Update(1.f / 60.f);

	// La caisse est tombée : sa position MONDE a baissé, et son local aussi
	// (le support, lui, n'a pas bougé).
	const float world = scene->tree.GlobalPosition(id.Unwrap()).y;
	const float local = scene->tree.Get(id.Unwrap())->transform.position.y;
	CHECK(world < 12.f);
	CHECK(local < 0.f);
	// Et la conversion est cohérente : monde = parent + local.
	CHECK(sdl3::Abs(world - (12.f + local)) < 1e-3f);
	h.runtime.Stop();
}

TEST(Hierarchy_LegacyFlatProjectsAreConvertedIntoATree) {
	// Un projet au format 2 (liste plate + champ `parent` portant un nom)
	// doit continuer de s'ouvrir, et donner un vrai arbre.
	String legacy = String(
		"{\"format\":\"sandbox.project\",\"version\":2,\"name\":\"Ancien\",\"active_scene\":\"S\","
		"\"scenes\":[{\"name\":\"S\",\"objects\":["
		"{\"name\":\"Voiture\",\"shape\":\"box\",\"transform\":{\"position\":[10,0,0]}},"
		"{\"name\":\"Roue\",\"parent\":\"Voiture\",\"shape\":\"cylinder\","
		"\"transform\":{\"position\":[1,0,0]}}]}]}");
	auto project = Project::DecodeJson(legacy);
	CHECK(project.IsOk());
	const SceneDesc &scene = project.Value().documents[0];
	CHECK_EQ(scene.NodeCount(), size_t(2));

	scene::NodeId car = scene.FindId(String("Voiture"));
	scene::NodeId wheel = scene.FindId(String("Roue"));
	CHECK(car.Valid() && wheel.Valid());
	CHECK(scene.tree.ParentOf(wheel) == car);
	// Le transform de l'ancien format était DÉJÀ relatif au parent : il ne
	// doit surtout pas être recalculé à l'import.
	CHECK(scene.tree.Get(wheel)->transform.position.x == 1.f);
	CHECK(sdl3::Abs(scene.tree.GlobalPosition(wheel).x - 11.f) < 1e-4f);
	CHECK(scene.tree.Validate().Ok());
}

TEST(Hierarchy_SavingAndReloadingKeepsTheTreeAndTheComponents) {
	Harness h;
	Option<scene::NodeId> group = h.runtime.CreateGroup(String("Ensemble"));
	CHECK(group.IsSome());
	NodeDesc part;
	part.name = "Pièce";
	part.shape = ShapeKind::TORUS;
	part.material.kind = MaterialKind::METAL;
	part.physics.body = BodyKind::STATIC;
	part.tag = "pièce";
	CHECK(h.runtime.SpawnNode(part, group.Unwrap()).IsSome());
	h.runtime.GetProject().documents[0].tree.Get(group.Unwrap())
		->Set(String("max_speed"), scene::PropertyValue::Float(180.0));

	auto reloaded = Project::DecodeJson(h.runtime.GetProject().EncodeJson());
	CHECK(reloaded.IsOk());
	const SceneDesc *scene = reloaded.Value().Fs().FindScene(SceneRef{h.runtime.ActiveDocument()->name});
	CHECK(scene != nullptr);

	scene::NodeId reloadedGroup = scene->FindId(String("Ensemble"));
	scene::NodeId reloadedPart = scene->FindId(String("Pièce"));
	CHECK(reloadedGroup.Valid() && reloadedPart.Valid());
	CHECK(scene->tree.ParentOf(reloadedPart) == reloadedGroup);
	const scene::Node *node = scene->tree.Get(reloadedPart);
	CHECK(VisualDesc::Read(*node).shape == ShapeKind::TORUS);
	CHECK(VisualDesc::Read(*node).material.kind == MaterialKind::METAL);
	CHECK(PhysicsDesc::Read(*node).body == BodyKind::STATIC);
	CHECK_EQ(TagOf(*node), "pièce");
	// La propriété LIBRE survit elle aussi.
	CHECK(sdl3::Abs(scene->tree.Get(reloadedGroup)->Get(String("max_speed"))->AsFloat() - 180.f) < 1e-3f);
}

TEST(Hierarchy_ScriptsNavigateAndReshapeTheTree) {
	Harness h;
	CHECK(h.Run("node.create_group(\"Voiture\")\n"
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

	const SceneDesc *scene = h.runtime.ActiveDocument();
	CHECK(scene->FindId(String("Voiture")).Valid());
	CHECK(scene->tree.Validate().Ok());
	// Le reparentage garde la position monde par défaut.
	scene::NodeId torus = scene->FindId(String("Tore"));
	CHECK(torus.Valid());
	CHECK(scene->tree.ParentOf(torus) == scene->FindId(String("Roues")));
}

TEST(Hierarchy_ASubtreeCanBeSavedAsASceneAndInstantiatedSeveralTimes) {
	Harness h;
	Option<scene::NodeId> car = h.runtime.CreateGroup(String("Modèle"));
	CHECK(car.IsSome());
	NodeDesc part;
	part.name = "Roue";
	part.transform.position = {1.f, 0.f, 0.f};
	CHECK(h.runtime.SpawnNode(part, car.Unwrap()).IsSome());
	CHECK(h.runtime.SetPosition(car.Unwrap(), {5.f, 0.f, 0.f}));

	const String path = String("/tmp/game_editor_modele.scene");
	auto saved = h.runtime.SavePackedScene(car.Unwrap(), path);
	CHECK(saved.IsOk());

	const size_t before = h.runtime.ActiveDocument()->NodeCount();
	auto first = h.runtime.InstantiateSceneFile(path);
	auto second = h.runtime.InstantiateSceneFile(path);
	CHECK(first.IsOk() && second.IsOk());

	const SceneDesc *scene = h.runtime.ActiveDocument();
	CHECK_EQ(scene->NodeCount(), before + 4); // deux instances de 2 nœuds
	CHECK(scene->tree.Get(first.Value())->name != scene->tree.Get(second.Value())->name);
	// Chaque instance sait d'où elle vient…
	CHECK(scene::PackedScene::InstanceSource(*scene->tree.Get(first.Value())) == path);
	// …et elles sont indépendantes : bouger l'une ne bouge pas l'autre.
	CHECK(h.runtime.SetPosition(first.Value(), {50.f, 0.f, 0.f}));
	scene::NodeId wheelA = scene->tree.Resolve(String("Roue"), first.Value());
	scene::NodeId wheelB = scene->tree.Resolve(String("Roue"), second.Value());
	CHECK(wheelA.Valid() && wheelB.Valid());
	CHECK(sdl3::Abs(scene->tree.GlobalPosition(wheelA).x - 51.f) < 1e-3f);
	CHECK(sdl3::Abs(scene->tree.GlobalPosition(wheelB).x - 6.f) < 1e-3f);
	CHECK(scene->tree.Validate().Ok());

	// L'instanciation est annulable comme le reste. La dernière commande
	// était le DÉPLACEMENT ci-dessus : on l'annule d'abord, puis la seconde
	// instanciation — l'historique empile bien chaque geste séparément.
	CHECK(h.runtime.Undo());
	CHECK(sdl3::Abs(scene->tree.GlobalPosition(wheelA).x - 6.f) < 1e-3f);
	CHECK(h.runtime.Undo());
	CHECK_EQ(h.runtime.ActiveDocument()->NodeCount(), before + 2);
}

TEST(Content_TheAssemblySceneShowsRealHierarchies) {
	// La scène livrée doit VRAIMENT être profonde — sinon elle ne démontre
	// rien de ce que la hiérarchie apporte.
	Project project = LoadDemoProject();
	const SceneDesc *scene = project.Fs().FindScene(SceneRef{"Assemblages"});
	CHECK(scene != nullptr);
	CHECK(scene->tree.Validate().Ok());

	scene::NodeId eye = scene->tree.Resolve(String("/Assemblages/Robot/Corps/Tête/Œil"));
	CHECK(eye.Valid());
	CHECK_EQ(scene->tree.DepthOf(eye), size_t(4));

	// Le même nom « Tête » existe deux fois dans la scène, à des endroits
	// différents : c'est le chemin qui les distingue.
	scene::NodeId robotHead = scene->tree.Resolve(String("/Assemblages/Robot/Corps/Tête"));
	scene::NodeId charHead = scene->tree.Resolve(String("/Assemblages/Personnage/Visuel/Corps/Tête"));
	CHECK(robotHead.Valid() && charHead.Valid());
	CHECK(robotHead != charHead);

	// Une main est bien emportée par le corps du robot : sa position monde
	// tient compte de tous les maillons intermédiaires.
	scene::NodeId hand = scene->tree.Resolve(String("/Assemblages/Robot/Corps/BrasDroit/Bras/Avant-bras/Main"));
	CHECK(hand.Valid());
	const math::FVector3 world = scene->tree.GlobalPosition(hand);
	CHECK(sdl3::Abs(world.x - (-7.f + 1.2f)) < 1e-3f); // robot + épaule
	CHECK(world.y > 0.f && world.y < 3.f);             // le long du bras

	// Les quatre assemblages demandés sont là, et chacun a des enfants.
	for (const char *name : {"Robot", "Véhicule", "Personnage", "Bâtiment"}) {
		scene::NodeId id = scene->tree.FindChild(scene->tree.Root(), String(name));
		CHECK(id.Valid());
		CHECK(!scene->tree.ChildrenOf(id).empty());
	}
}

TEST(Content_MovingAnAssemblyRootMovesItsWholeSubtree) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Assemblages"}));
	const SceneDesc *scene = h.runtime.ActiveDocument();
	scene::NodeId robot = scene->tree.FindChild(scene->tree.Root(), String("Robot"));
	scene::NodeId hand = scene->tree.Resolve(String("/Assemblages/Robot/Corps/BrasDroit/Bras/Avant-bras/Main"));
	CHECK(robot.Valid() && hand.Valid());

	const math::FVector3 before = scene->tree.GlobalPosition(hand);
	CHECK(h.runtime.SetPosition(robot, {20.f, 0.f, 0.f}));
	const math::FVector3 after = scene->tree.GlobalPosition(hand);
	CHECK(sdl3::Abs((after.x - before.x) - 27.f) < 1e-3f); // -7 -> +20
	CHECK(sdl3::Abs(after.y - before.y) < 1e-4f);
}

// ============================================================================
// Donjon : lumières, caméras, déclencheurs, scripts de nœud
// ============================================================================

namespace {

/// Nombre de nœuds de la scène active portant ce type.
int CountType(const SceneDesc &scene, const char *type) {
	int count = 0;
	for (scene::NodeId id : scene.Nodes())
		if (scene.tree.Get(id)->type == type)
			++count;
	return count;
}

const scene::Node *Node(Harness &h, const char *name) { return h.runtime.ActiveDocument()->Find(String(name)); }

void Simulate(Harness &h, int frames) {
	for (int i = 0; i < frames; ++i)
		h.runtime.Update(1.f / 60.f);
}

} // namespace

TEST(Dungeon_IsOrganisedInFoldersWithLightsCameraAndTriggers) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Donjon"}));
	const SceneDesc &scene = *h.runtime.ActiveDocument();
	CHECK_EQ(CountType(scene, node_kind::FOLDER), 4); // Environnement, Objets dynamiques, Déclencheurs, Chemin
	CHECK_EQ(CountType(scene, node_kind::LIGHT), 5);
	CHECK_EQ(CountType(scene, node_kind::TRIGGER), 3);
	CHECK_EQ(CountType(scene, node_kind::CAMERA), 1);
	// Les chemins lisent comme la maquette : Environnement > Torches > Torche 1.
	CHECK(scene.tree.Resolve(String("/Donjon/Environnement/Torches/Torche 1")).Valid());
	CHECK(scene.tree.Resolve(String("/Donjon/Objets dynamiques/Coffre/Couvercle/Planche")).Valid());
	CHECK(scene.tree.Resolve(String("/Donjon/Déclencheurs/Zone du coffre")).Valid());

	// Tous les scripts attachés existent dans la bibliothèque et compilent.
	for (const ScriptAsset &script : h.runtime.GetProject().scripts)
		CHECK(Runtime::CheckScript(script.source).IsNone());
	for (scene::NodeId id : scene.Nodes())
		if (ScriptRef::Has(*scene.tree.Get(id)))
			CHECK(h.runtime.GetProject().FindScript(ScriptRef::Read(*scene.tree.Get(id)).script) != nullptr);
}

TEST(Dungeon_TorchesBecomePointLightsAtTheirWorldPosition) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Donjon"}));
	Simulate(h, 1);
	CHECK(h.runtime.PointLights().size() == 5u);
	const scene::NodeId torch = h.runtime.ResolveId(String("Torche 1"));
	const math::FVector3 world = h.runtime.ActiveDocument()->tree.GlobalPosition(torch);
	const render3d::PointLight &light = h.runtime.PointLights().front();
	CHECK((light.position - world).Length() < 1e-4f);
	CHECK(light.intensity == 3.2f);
	CHECK(light.distance == 11.f);

	// Masquer le dossier « Torches » éteint les cinq (visibilité HÉRITÉE).
	CHECK(h.runtime.SetVisible(h.runtime.ResolveId(String("Torches")), false));
	Simulate(h, 1);
	CHECK(h.runtime.PointLights().empty());
}

TEST(Dungeon_TheGuidedTourOpensTheChestThenTheDoorAndReachesTheExit) {
	// L'essai de jouabilité du donjon, sans clavier ni souris : le script
	// `player` suit les étapes, les zones de déclenchement réveillent le
	// script de scène, qui pose des propriétés que les scripts du coffre et
	// de la porte lisent. Toute la chaîne est donc exercée.
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Donjon"}));
	const math::FVector3 doorBefore = Node(h, "Porte")->transform.position;
	h.runtime.Play();
	CHECK(h.runtime.IsPlaying());
	Simulate(h, 60 * 30);

	CHECK_EQ(int(h.runtime.ScriptErrorCount()), 0);
	CHECK(h.runtime.TriggerCount() >= 3u);
	CHECK(h.runtime.GameplayGlobal(String("chest_open")).IsSome());
	CHECK(h.runtime.GameplayGlobal(String("chest_open")).Unwrap().IsTruthy());
	CHECK(h.runtime.GameplayGlobal(String("escaped")).Unwrap().IsTruthy());
	// Le couvercle a pivoté, la herse est montée, le joueur est passé.
	CHECK(Node(h, "Couvercle")->transform.EulerDegrees().x < -60.f);
	CHECK(Node(h, "Porte")->transform.position.y > doorBefore.y + 3.f);
	CHECK(Node(h, "Joueur")->transform.position.z > 44.f);
	CHECK(Node(h, "Joueur")->transform.position.y > 0.f); // pas tombé à travers le sol

	// Stop restaure le document : porte baissée, coffre fermé.
	h.runtime.Stop();
	CHECK((Node(h, "Porte")->transform.position - doorBefore).Length() < 1e-4f);
	CHECK(!Node(h, "Coffre")->Get(String("open"))->AsBool());
}

TEST(Dungeon_TheCurrentCameraNodeGivesThePlayView) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Donjon"}));
	h.runtime.Play();
	Simulate(h, 30);
	const scene::NodeId eye = h.runtime.ResolveId(String("Vue"));
	CHECK(h.runtime.CurrentCamera() == eye);
	const math::FVector3 eyeWorld = h.runtime.ActiveDocument()->tree.GlobalPosition(eye);
	CHECK((h.runtime.ActiveCamera().position - eyeWorld).Length() < 0.05f);
	constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
	CHECK(sdl3::Abs(h.runtime.ActiveCamera().fovYRadians - 75.f * DEG2RAD) < 1e-4f);
}

TEST(NodeScripts_TorchesFlickerIndependentlyAndStopRestoresThem) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Donjon"}));
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
		CHECK(a >= BASE * 0.72f - 1e-3f && a <= BASE * 1.18f + 1e-3f);
	}
	CHECK(changed);
	CHECK(different);
	std::vector<ScriptStatus> statuses = h.runtime.ScriptStatuses();
	bool torchRunning = false;
	for (const ScriptStatus &status : statuses)
		if (status.name == "torchlight")
			torchRunning = status.running && status.nodes == 5u;
	CHECK(torchRunning);

	h.runtime.Stop();
	CHECK(LightDesc::Read(*Node(h, "Torche 1")).intensity == BASE);
}

TEST(NodeScripts_EachNodeReceivesItsOwnSelf) {
	Harness h;
	(void)h.runtime.AddScript(String("tagger"),
							 String("class Marqueur extends Behaviour { fn on_start() { this.prop(\"seen\", this.node()) } }"));
	const scene::NodeId a = h.runtime.ResolveId(String("Tore"));
	const scene::NodeId b = h.runtime.ResolveId(String("Cristal"));
	CHECK(a.Valid() && b.Valid());
	CHECK(h.runtime.SetScriptRef(a, String("tagger")));
	CHECK(h.runtime.SetScriptRef(b, String("tagger")));
	h.runtime.Play();
	Simulate(h, 1);
	CHECK_EQ(Node(h, "Tore")->Get(String("seen"))->AsString(), "Tore");
	CHECK_EQ(Node(h, "Cristal")->Get(String("seen"))->AsString(), "Cristal");
}

TEST(NodeScripts_ABrokenScriptIsDisabledAloneAndReported) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Donjon"}));
	(void)h.runtime.AddScript(String("broken"), String("class Casse extends Behaviour { fn on_update(dt) { let x = nil + 1 } }"));
	CHECK(h.runtime.SetScriptRef(h.runtime.ResolveId(String("Sortie")), String("broken")));
	h.runtime.Play();
	Simulate(h, 10);
	bool brokenStopped = false, torchesRunning = false;
	for (const ScriptStatus &status : h.runtime.ScriptStatuses()) {
		if (status.name == "broken")
			brokenStopped = !status.running && !status.error.IsEmpty();
		if (status.name == "torchlight")
			torchesRunning = status.running;
	}
	CHECK(brokenStopped);
	CHECK(torchesRunning);
	CHECK_EQ(int(h.runtime.ScriptErrorCount()), 1); // une fois, pas une par image
}

// ============================================================================
// Bases du moteur dérivées par les scripts (Scene, Behaviour, Mesh3D…)
// ============================================================================

TEST(EngineBases_EachNodeGetsItsOwnBehaviourInstanceDestroyedWithTheScene) {
	Harness h;
	(void)h.runtime.AddScript(String("compteur"),
							  String("var detruits = []\n"
									 "class Compteur extends Behaviour {\n"
									 "    let images = 0\n"
									 "    fn on_update(dt) { this.images += 1\n"
									 "                       this.prop(\"images\", this.images) }\n"
									 "    fn on_destroy() { detruits.append(this.node()) }\n"
									 "}\n"));
	CHECK(h.runtime.SetScriptRef(h.runtime.ResolveId(String("Tore")), String("compteur")));
	CHECK(h.runtime.SetScriptRef(h.runtime.ResolveId(String("Cristal")), String("compteur")));
	h.runtime.Play();
	Simulate(h, 3);
	// Un état PAR NŒUD, dans les champs de l'instance (plus de table par `self`).
	CHECK(Node(h, "Tore")->Get(String("images"))->AsFloat() == 3.f);
	CHECK(Node(h, "Cristal")->Get(String("images"))->AsFloat() == 3.f);
	size_t behaviours = 0;
	for (const script::OwnerInfo &info : h.runtime.LiveOwners())
		behaviours += info.typeName == "game.Behaviour" ? 1 : static_cast<size_t>(0);
	CHECK_EQ(behaviours, size_t(2));
	h.runtime.Stop();
	CHECK_EQ(h.runtime.LiveOwners().size(), size_t(0));
	CHECK_EQ(int(h.runtime.ScriptErrorCount()), 0);
}

TEST(EngineBases_AScriptWithoutBehaviourClassIsReported) {
	Harness h;
	(void)h.runtime.AddScript(String("ancien"), String("fn on_update(self, dt) { }"));
	CHECK(h.runtime.SetScriptRef(h.runtime.ResolveId(String("Tore")), String("ancien")));
	h.runtime.Play();
	Simulate(h, 2);
	bool reported = false;
	for (const ScriptStatus &status : h.runtime.ScriptStatuses())
		if (status.name == "ancien")
			reported = !status.running && status.error.Contains("Behaviour");
	CHECK(reported);
	h.runtime.Stop();
}

TEST(EngineBases_MeshAndBodyAreCreatedThenRemovedInRaiiOrder) {
	Harness h;
	CHECK(h.runtime.SetGameplayScript(
					SceneRef{"Vitrine"},
					String("var journal = []\n"
						   "class Bille extends Mesh3D, PhysicsBody {\n"
						   "    fn init(n) { super.init(\"Bille \" .. n, {shape: \"sphere\", pos: [n, 6, 0], body: \"dynamic\"}) }\n"
						   "    fn on_destroy() { journal.append(\"on_destroy \" .. this.name) }\n"
						   "    fn deinit() { journal.append(\"deinit\") }\n"
						   "}\n"
						   "class Essai extends Scene {\n"
						   "    let billes = []\n"
						   "    fn on_start() { this.billes = [Bille(1), Bille(2)]\n"
						   "                    this.billes[0].impulse(0, 3, 0) }\n"
						   "    fn on_update(dt) { if (this.time() > 0.1 and not this.billes[1].is_destroyed()) {\n"
						   "        this.billes[1].destroy() } }\n"
						   "}\n"))
					.IsNone());
	h.runtime.Play();
	CHECK(Node(h, "Bille 1") != nullptr && Node(h, "Bille 2") != nullptr);
	CHECK(PhysicsDesc::Read(*Node(h, "Bille 1")).body == BodyKind::DYNAMIC);
	Simulate(h, 12);
	// Détruite à la main : son nœud est parti, l'autre bille vole toujours —
	// le retrait d'un nœud en partie ne remet pas la physique à zéro.
	CHECK(Node(h, "Bille 2") == nullptr);
	Option<math::FVector3> v = h.runtime.GetVelocity(h.runtime.ResolveId(String("Bille 1")));
	CHECK(v.IsSome());
	CHECK(sdl3::Abs(v.Unwrap().y) > 0.01f);
	CHECK_EQ(h.runtime.GameplayGlobal(String("journal")).Unwrap().ToDisplayString(),
			  "[on_destroy Bille 2, deinit]");
	CHECK_EQ(h.runtime.LiveOwners().size(), size_t(1 * 2 + 1)); // Bille 1 (2 bases) + Scene
	h.runtime.Stop();
	CHECK(Node(h, "Bille 1") == nullptr);
	CHECK_EQ(h.runtime.LiveOwners().size(), size_t(0));
	CHECK_EQ(int(h.runtime.ScriptErrorCount()), 0);
	// La partie n'a laissé aucune étape d'annulation.
	CHECK(h.runtime.UndoLabel().IsEmpty());
}

TEST(EngineBases_BasesRefuseInconsistentCombinations) {
	Harness h;
	h.runtime.Play();
	auto error = [&](const char *source) -> String {
		auto result = h.runtime.GameplayVm().Run(StringView(source));
		return result.IsOk() ? String() : result.Error().Format();
	};
	CHECK(error("class A extends PhysicsBody, Mesh3D {}\nA()").Contains("déclarée AVANT"));
	CHECK(error("class B extends Mesh3D, Node3D {}\nB()").Contains("porte déjà un nœud"));
	CHECK(error("class C extends Behaviour {}\nC()").Contains("s'attache à un nœud"));
	CHECK(error("class D extends Behaviour {}\nD(\"Inexistant\")").Contains("introuvable"));
	CHECK(error("class E extends SceneAsset {}\nE()").Contains("chemin"));
	h.runtime.Stop();
	CHECK_EQ(h.runtime.LiveOwners().size(), size_t(0));
}

TEST(EngineBases_GameplayEventsReachListenersAndAreForgottenOnDestroy) {
	Harness h;
	h.runtime.Play();
	auto result = h.runtime.GameplayVm().Run(StringView(
		"class Cible extends Gameplay {\n"
		"    let pv = 10\n"
		"    fn init() { super.init()\n"
		"                this.on(\"hit\", fn(d) { this.pv -= d }) }\n"
		"}\n"
		"let c = Cible()\n"
		"c.emit(\"hit\", 3)\nc.emit(\"hit\", 2)\n"
		"let avant = c.listeners()\n"
		"c.destroy()\n"
		"return [c.pv, avant, c is Gameplay]"));
	CHECK(result.IsOk());
	CHECK_EQ(result.Value().ToDisplayString(), "[5, 1, true]");
	h.runtime.Stop();
}

// ============================================================================
// Analyse statique des scripts (ce que le moteur en fera, sans exécution)
// ============================================================================

TEST(ScriptOutlines_RolesFollowTheEngineRules) {
	const ModuleSource none = [](const String &) -> Option<String> { return NONE; };
	ScriptOutline behaviour = OutlineScript(
		String("class Torche extends Behaviour, Light3D {\n    fn on_update(dt) { }\n    fn deinit() { }\n}\n"),
		ScriptUse::LIBRARY, none);
	CHECK(behaviour.role == ScriptRole::BEHAVIOUR);
	CHECK_EQ(behaviour.mainClass, "Torche");
	CHECK(behaviour.Find(String("Torche")) != nullptr);
	CHECK_EQ(behaviour.Find(String("Torche"))->Signature(), "Torche (Behaviour, Light3D)");
	CHECK_EQ(behaviour.Summary(), "comportement Torche (Behaviour, Light3D)");
	CHECK(behaviour.Find(String("Torche"))->hooks.size() == 2u);

	// Un parent abstrait et une feuille : seule la feuille compte ; les bases
	// héritées du parent sont reportées sur la feuille.
	ScriptOutline inherited = OutlineScript(String("abstract class Base extends Scene { }\n"
												   "namespace jeu { class Niveau extends Base, Gameplay { } }\n"),
											ScriptUse::SCENE, none);
	CHECK(inherited.role == ScriptRole::SCENE);
	CHECK_EQ(inherited.mainClass, "jeu.Niveau");
	CHECK_EQ(inherited.Find(String("jeu.Niveau"))->Signature(), "jeu.Niveau (Base, Scene, Gameplay)");

	CHECK(OutlineScript(String("fn on_update(dt) { }"), ScriptUse::SCENE, none).role == ScriptRole::INVALID);
	ScriptOutline two = OutlineScript(String("class A extends Behaviour {}\nclass B extends Behaviour {}"),
									  ScriptUse::LIBRARY, none);
	// Plusieurs Behaviour : un module valide (importable), mais pas attachable.
	CHECK(two.role == ScriptRole::MODULE);
	CHECK(two.attachProblem.Contains("A, B"));
	CHECK_EQ(two.Summary(), "module : 2 classes dont 2 Behaviour");
	CHECK(OutlineScript(String("class Outil { }"), ScriptUse::LIBRARY, none).role == ScriptRole::MODULE);
	CHECK(OutlineScript(String(""), ScriptUse::SCENE, none).role == ScriptRole::EMPTY);
	ScriptOutline broken = OutlineScript(String("class {"), ScriptUse::LIBRARY, none);
	CHECK(broken.error.IsSome());
	CHECK(broken.Summary().StartsWith("erreur ligne"));
	// Une classe locale du même nom qu'une base du moteur la masque.
	ScriptOutline shadow = OutlineScript(String("class Scene { }\nclass X extends Scene { }"), ScriptUse::SCENE, none);
	CHECK(shadow.role == ScriptRole::INVALID);
}

TEST(ScriptOutlines_ImportedModulesAreResolvedWithoutRunning) {
	// Le module définit une base abstraite ET une feuille Behaviour : comme à
	// l'exécution, une Behaviour d'un module importé compte.
	const ModuleSource modules = [](const String &specifier) -> Option<String> {
		if (specifier == "course")
			return Some(String("abstract class Course extends Scene { fn on_update(dt) { } }\n"
							   "editor.save()  # jamais exécuté par l'analyse\n"));
		return NONE;
	};
	ScriptOutline scene = OutlineScript(String("const course = import \"course\"\nclass Ovale extends course.Course {}\n"),
										ScriptUse::SCENE, modules);
	CHECK(scene.role == ScriptRole::SCENE);
	CHECK_EQ(scene.mainClass, "Ovale");
	CHECK(scene.imports.size() == 1u);
	CHECK_EQ(scene.Find(String("Ovale"))->Signature(), "Ovale (Course, Scene)");
	ScriptOutline missing = OutlineScript(String("const x = import \"absent\"\nclass S extends x.Base {}\n"),
										  ScriptUse::SCENE, modules);
	CHECK(missing.role == ScriptRole::INVALID);
	CHECK(missing.Find(String("S")) != nullptr);
	CHECK(missing.Find(String("S"))->unresolved.size() == 1u);
}

TEST(ScriptOutlines_AgreeWithTheEngineOnTheDemoProject) {
	Harness h;
	const Project &project = h.runtime.GetProject();
	for (const ScriptAsset &script : project.scripts) {
		const ScriptOutline outline = h.runtime.OutlineScript(script.source, ScriptUse::LIBRARY);
		CHECK(outline.role == ScriptRole::BEHAVIOUR);
	}
	for (const SceneDesc &scene : project.documents)
		if (!scene.gameplayScript.IsEmpty())
			CHECK(h.runtime.OutlineScript(scene.gameplayScript, ScriptUse::SCENE).role == ScriptRole::SCENE);
}

TEST(EngineBases_LiveObjectsAreListedPerNodeAndDestroyableFromTheEditor) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Donjon"}));
	h.runtime.Play();
	Simulate(h, 2);
	const scene::NodeId torch = h.runtime.ResolveId(String("Torche 1"));
	std::vector<ScriptObjectInfo> objects = h.runtime.ScriptObjectsOf(torch);
	CHECK(objects.size() == 1u);
	CHECK_EQ(objects[0].Signature(), "Torche (Behaviour, Light3D)");
	CHECK(objects[0].attached);
	CHECK_EQ(objects[0].origin, "torchlight");
	bool hasBase = false;
	for (const auto &[name, value] : objects[0].Fields())
		hasBase = hasBase || name == "base";
	CHECK(hasBase);
	bool sceneObject = false;
	for (const ScriptObjectInfo &info : h.runtime.LiveScriptObjects())
		sceneObject = sceneObject || (info.origin == "scène" && info.className == "Donjon");
	CHECK(sceneObject);
	// Détruire depuis l'éditeur : séquence RAII, le nœud (non possédé) reste.
	CHECK(h.runtime.DestroyScriptObject(objects[0]));
	CHECK(!h.runtime.DestroyScriptObject(objects[0]));
	CHECK(h.runtime.ScriptObjectsOf(torch).empty());
	CHECK(Node(h, "Torche 1") != nullptr);
	Simulate(h, 2);
	CHECK_EQ(int(h.runtime.ScriptErrorCount()), 0);
	h.runtime.Stop();
	CHECK(h.runtime.LiveScriptObjects().empty());
}

TEST(Triggers_OnlyTransitionsAreNotifiedAndOnceMeansOnce) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Donjon"}));
	// Script de scène de test : compte les entrées et sorties.
	CHECK(h.runtime.SetGameplayScript(SceneRef{"Donjon"},
											String("class Compteur extends Scene {\n"
												   "    let enters = 0\n    let exits = 0\n"
												   "    fn on_trigger(zone, other, event) { this.enters += 1 }\n"
												   "    fn on_trigger_exit(zone, other, event) { this.exits += 1 }\n"
												   "}"))
					.IsNone());
	// Une zone RÉPÉTABLE, loin du chemin du joueur.
	NodeDesc zone = NodeDesc::Group(String("Zone test"));
	zone.type = String();
	zone.trigger = Some(TriggerDesc{{1.f, 2.f, 1.f}, String("test"), false});
	zone.transform.position = {0.f, 1.f, -30.f};
	CHECK(h.runtime.SpawnNode(zone).IsSome());
	// Le joueur ne doit pas bouger tout seul : sa visite guidée est coupée.
	CHECK(h.runtime.SetScriptRef(h.runtime.ResolveId(String("Joueur")), String()));

	h.runtime.Play();
	const scene::NodeId player = h.runtime.ResolveId(String("Joueur"));
	auto teleport = [&](float z) {
		CHECK(h.runtime.SetPosition(player, {0.f, 1.f, z}));
		Simulate(h, 3);
	};
	teleport(-30.f); // entre
	teleport(-30.2f); // reste dedans : rien
	teleport(-10.f); // sort
	teleport(-30.f); // rentre (répétable)
	CHECK_EQ(int(h.runtime.GameplayGlobal(String("enters")).Unwrap().AsNumber()), 2);
	CHECK_EQ(int(h.runtime.GameplayGlobal(String("exits")).Unwrap().AsNumber()), 1);

	// « once » : la zone du coffre ne notifie qu'une entrée.
	teleport(26.f);
	teleport(10.f);
	teleport(26.f);
	CHECK_EQ(int(h.runtime.GameplayGlobal(String("enters")).Unwrap().AsNumber()), 3);
}

TEST(Components_AddRemoveEditAsJsonAndUndo) {
	Harness h;
	const scene::NodeId id = h.runtime.ResolveId(String("Tore"));
	CHECK(id.Valid());
	CHECK(h.runtime.AddComponentOfType(id, String(component::LIGHT)));
	CHECK(LightDesc::Has(*Node(h, "Tore")));
	Simulate(h, 1);
	CHECK(h.runtime.PointLights().size() == 1u);

	// Édition « en JSON » : les propriétés remplacent celles du composant.
	scene::PropertyMap props;
	props.Set(String("kind"), scene::PropertyValue::Str(String("spot")));
	props.Set(String("intensity"), scene::PropertyValue::Float(5.0));
	CHECK(h.runtime.SetComponentProps(id, String(component::LIGHT), props));
	Simulate(h, 1);
	CHECK(h.runtime.PointLights().empty());
	CHECK(h.runtime.SpotLights().size() == 1u);
	CHECK(h.runtime.SpotLights().front().intensity == 5.f);

	CHECK(h.runtime.Undo()); // retour à la lumière ponctuelle
	CHECK(h.runtime.Undo()); // plus de lumière du tout
	CHECK(!LightDesc::Has(*Node(h, "Tore")));
	Simulate(h, 1);
	CHECK(h.runtime.PointLights().empty() && h.runtime.SpotLights().empty());

	CHECK(!h.runtime.AddComponentOfType(id, String("Inconnu")));
	CHECK(!h.runtime.RemoveComponentOfType(id, String(component::CAMERA)));
}

TEST(Components_ASpotLightPointsAlongTheNodesLocalDown) {
	Harness h;
	LightDesc spot;
	spot.kind = LightKind::SPOT;
	NodeDesc lamp = NodeDesc::Light(String("Lampe"), spot);
	lamp.transform.SetEulerDegrees({90.f, 0.f, 0.f}); // -Y local tourné de 90° autour de X
	CHECK(h.runtime.SpawnNode(lamp).IsSome());
	Simulate(h, 1);
	CHECK(h.runtime.SpotLights().size() == 1u);
	const math::FVector3 dir = h.runtime.SpotLights().front().direction;
	// Rx(90°)·(0,-1,0) = (0, 0, -1).
	CHECK(sdl3::Abs(dir.x) < 1e-4f && sdl3::Abs(dir.y) < 1e-4f && sdl3::Abs(dir.z + 1.f) < 1e-4f);
}

TEST(Helpers_LightsCamerasAndZonesArePickableInEditOnly) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Donjon"}));
	const scene::NodeId torch = h.runtime.ResolveId(String("Torche 1"));
	const scene::NodeId zone = h.runtime.ResolveId(String("Sortie"));
	CHECK(h.runtime.HasVisibleHelper(torch));
	CHECK(h.runtime.HasVisibleHelper(zone));
	CHECK(h.runtime.HasVisibleHelper(h.runtime.ResolveId(String("Vue"))));

	// Un rayon qui vise le repère de la torche la sélectionne.
	const math::FVector3 at = h.runtime.ActiveDocument()->tree.GlobalPosition(torch);
	const math::FVector3 from = at + math::FVector3{2.f, 0.f, 0.f} * (at.x < 0.f ? 1.f : -1.f);
	CHECK(h.runtime.SelectAt(math::FRay{from, (at - from).Normalize()}));
	CHECK(h.runtime.SelectedId() == torch);

	h.runtime.Play();
	CHECK(!h.runtime.HasVisibleHelper(torch));
	h.runtime.Stop();
	CHECK(h.runtime.HasVisibleHelper(h.runtime.ResolveId(String("Torche 1"))));
}

TEST(Project_TheScriptLibrarySurvivesTheRoundTripAndOldFilesStillLoad) {
	Project project = LoadDemoProject();
	CHECK(project.scripts.size() == 5u);
	auto reloaded = Project::DecodeJson(project.EncodeJson());
	CHECK(reloaded.IsOk());
	CHECK(reloaded.Value().scripts.size() == 5u);
	CHECK_EQ(reloaded.Value().FindScript(String("torchlight"))->source, project.FindScript(String("torchlight"))->source);
	// Composants : lumière et zone relues à l'identique.
	const SceneDesc *dungeon = reloaded.Value().Fs().FindScene(SceneRef{String("Donjon")});
	CHECK(dungeon != nullptr);
	const LightDesc light = LightDesc::Read(*dungeon->Find(String("Torche 3")));
	CHECK(light.intensity == 3.2f && light.range == 11.f && light.color.r == 255);
	CHECK_EQ(TriggerDesc::Read(*dungeon->Find(String("Sortie"))).event, "exit");

	// Un fichier d'avant la bibliothèque (v3, sans `scripts`) se lit.
	String legacy = project.EncodeJson();
	auto document = Project::DecodeJson(legacy);
	CHECK(document.IsOk());
	Project old = std::move(document).Unwrap();
	old.scripts.clear();
	auto oldReloaded = Project::DecodeJson(old.EncodeJson());
	CHECK(oldReloaded.IsOk());
	CHECK(oldReloaded.Value().scripts.empty());
}

TEST(Scripts_CheckReportsTheFirstErrorWithItsLine) {
	CHECK(Runtime::CheckScript(String("let a = 1\nfn f() { return a }")).IsNone());
	Option<script::ScriptError> error = Runtime::CheckScript(String("let a = 1\nlet = 2\n"));
	CHECK(error.IsSome());
	CHECK_EQ(error.Unwrap().line, 2);

	Harness h;
	CHECK(h.runtime.SetScriptSource(String("door"), String("class Herse extends Behaviour {")).IsSome());
	// Le texte est gardé malgré l'erreur : on ne perd pas un travail en cours.
	CHECK_EQ(h.runtime.GetProject().FindScript(String("door"))->source, "class Herse extends Behaviour {");
}

TEST(ScriptApi_LightsAreDrivenByName) {
	Harness h;
	CHECK(h.runtime.OpenScene(SceneRef{"Donjon"}));
	CHECK(h.Run("light.set_intensity(\"Torche 2\", 0.5)\n"
					  "light.set_color(\"Torche 2\", 10, 20, 30)\n"
					  "assert(light.intensity(\"Torche 2\") == 0.5)\n"
					  "assert(light.intensity(\"Pavé\") == nil)\n"
					  "assert(len(light.list()) == 5)\n"
					  "let d = input.mouse_delta()\n"
					  "assert(d[0] == 0 and d[1] == 0)"));
	const LightDesc light = LightDesc::Read(*Node(h, "Torche 2"));
	CHECK(light.intensity == 0.5f);
	CHECK(light.color.r == 10 && light.color.g == 20 && light.color.b == 30);
}

// ============================================================================
// Interface : briques testables sans fenêtre
// ============================================================================

TEST(Templates_EveryMenuEntryBuildsTheRightKindOfNode) {
	for (const NodeTemplate &entry : NODE_TEMPLATES) {
		Option<NodeDesc> desc = MakeNodeFromTemplate(String(entry.key));
		CHECK(desc.IsSome());
		const scene::Node node = desc.Value().ToNode();
		CHECK_EQ(node.name, String(entry.label));
		if (String(entry.category) == "3D")
			CHECK(VisualDesc::Has(node));
		if (String(entry.category) == "Lumière")
			CHECK(LightDesc::Has(node) && node.type == node_kind::LIGHT);
	}
	CHECK(LightDesc::Read(MakeNodeFromTemplate(String("spot_light")).Value().ToNode()).kind == LightKind::SPOT);
	CHECK(MakeNodeFromTemplate(String("trigger")).Value().ToNode().type == node_kind::TRIGGER);
	CHECK(MakeNodeFromTemplate(String("folder")).Value().ToNode().type == node_kind::FOLDER);
	CHECK(MakeNodeFromTemplate(String("inconnu")).IsNone());

	// Créé sous un parent : l'enfant naît au pivot du parent, et s'annule.
	Harness h;
	const scene::NodeId parent = h.runtime.ResolveId(String("Tore"));
	Option<scene::NodeId> child = h.runtime.SpawnNode(MakeNodeFromTemplate(String("point_light")).Unwrap(), parent);
	CHECK(child.IsSome());
	CHECK(h.runtime.ActiveDocument()->tree.ParentOf(child.Unwrap()) == parent);
	CHECK(h.runtime.Undo());
	CHECK(!h.runtime.ActiveDocument()->tree.Contains(child.Unwrap()));
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

TEST(Syntax_ScriptTokensAreColouredWithoutOverlap) {
	const String line("let n = node.prop(self, \"seed\") + 1.5  # bruit");
	const std::vector<String> spans = Spans(kit::syntax::HighlightSled, line);
	const std::vector<String> expected = {"let", "node", "prop", "\"seed\"", "1.5", "# bruit"};
	CHECK(spans.size() == expected.size());
	for (size_t i = 0; i < expected.size(); ++i)
		CHECK_EQ(spans[i], expected[i]);

	std::vector<ui::UiTextSpan> raw;
	kit::syntax::HighlightSled(line, raw);
	for (size_t i = 1; i < raw.size(); ++i)
		CHECK(raw[i].begin >= raw[i - 1].end); // triés, disjoints
	// Un « # » DANS une chaîne n'ouvre pas de commentaire.
	CHECK_EQ(Spans(kit::syntax::HighlightSled, String("print(\"#1\")")).back(), "\"#1\"");
}

TEST(Syntax_JsonKeysAreTellApartFromStringValues) {
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
	CHECK(colorOf("\"name\"") == kit::syntax::Colors().key);
	CHECK(colorOf("\"Torche\"") == kit::syntax::Colors().string);
	CHECK(colorOf("true") == kit::syntax::Colors().literal);
	CHECK(colorOf("-2.5e3") == kit::syntax::Colors().number);
	CHECK(colorOf("}") == kit::syntax::Colors().brace);
	CHECK(bool(kit::syntax::ForName(String("scene.JSON"))));
}

TEST(Syntax_ConsoleLevelsColourTheirPrefix) {
	std::vector<ui::UiTextSpan> spans;
	kit::syntax::HighlightLog(String("[err   ] boum"), spans);
	CHECK(spans.size() == 1u);
	CHECK_EQ(spans[0].end, size_t(13)); // une erreur se colore sur toute la ligne
	spans.clear();
	kit::syntax::HighlightLog(String("[warn  ] attention"), spans);
	CHECK(spans.size() == 1u);
	CHECK_EQ(spans[0].end, size_t(8)); // un avertissement, son seul préfixe
	spans.clear();
	kit::syntax::HighlightLog(String("sans niveau"), spans);
	CHECK(spans.empty());
}

TEST(AssetBrowser_NavigatesProjectFoldersAndTheDisk) {
	Project project = LoadDemoProject();
	AssetBrowserModel model(&project, String("assets"));
	CHECK_EQ(model.Location(), AssetBrowserModel::ROOT);
	CHECK(!model.CanBack() || model.CanUp());

	// Racine : les dossiers du projet, puis ceux du disque — sans les
	// données de l'émulateur.
	bool scenes = false, scripts = false, models = false, bios = false;
	for (const AssetEntry &entry : model.Entries()) {
		scenes = scenes || entry.location == AssetBrowserModel::SCENES;
		scripts = scripts || entry.location == AssetBrowserModel::SCRIPTS;
		models = models || entry.location == "assets/models";
		bios = bios || entry.name == "bios-firmware";
	}
	CHECK(scenes && scripts && models);
	CHECK(!bios);

	// Scripts : la bibliothèque + un script de jeu par scène, tous compilent.
	CHECK(model.Navigate(String(AssetBrowserModel::SCRIPTS)));
	const std::vector<AssetEntry> list = model.Entries();
	CHECK_EQ(list.size(), project.scripts.size() + project.documents.size());
	for (const AssetEntry &entry : list)
		CHECK(!entry.broken);
	CHECK_EQ(model.Entries(String("TORCH")).size(), size_t(1)); // filtre insensible à la casse

	// Disque : dossiers d'abord, fil d'Ariane, précédent / suivant / parent.
	CHECK(model.Navigate(String("assets/models/animals")));
	const auto crumbs = model.Breadcrumb();
	CHECK(crumbs.size() == 3u);
	CHECK_EQ(crumbs[1].first, "models");
	CHECK_EQ(crumbs[2].second, "assets/models/animals");
	bool sawGltf = false, sawBin = false;
	for (const AssetEntry &entry : model.Entries()) {
		sawGltf = sawGltf || entry.kind == AssetKind::MODEL;
		sawBin = sawBin || entry.name.EndsWith(".bin"); // tampons glTF masqués
	}
	CHECK(sawGltf);
	CHECK(!sawBin);
	CHECK(model.Up());
	CHECK_EQ(model.Location(), "assets/models");
	CHECK(model.Up());
	CHECK_EQ(model.Location(), AssetBrowserModel::ROOT); // assets/x remonte à la racine du projet
	CHECK(model.Back());
	CHECK_EQ(model.Location(), "assets/models");
	CHECK(model.Forward());
	CHECK_EQ(model.Location(), AssetBrowserModel::ROOT);
	CHECK(!model.Navigate(String("assets/models/animals/Cow.gltf"))); // un fichier n'est pas un dossier
	CHECK(AssetBrowserModel::KindOf(String("Sol.PNG")) == AssetKind::TEXTURE);
	CHECK(AssetBrowserModel::KindOf(String("vehicule.scene")) == AssetKind::SCENE);
}

TEST(AssetBrowser_SavesAreAFolderOfTheirOwn) {
	// N'importe quel dossier existant fait office de dossier de sauvegardes.
	constexpr const char *SAVES_DIR = "demos/sandbox/tests/data";
	Project project = LoadDemoProject();
	AssetBrowserModel model(&project, String("assets"), String(SAVES_DIR));
	bool saves = false;
	for (const AssetEntry &entry : model.Entries())
		saves = saves || (entry.name == "Sauvegardes" && entry.location == SAVES_DIR);
	CHECK(saves);
	CHECK(model.Navigate(String(SAVES_DIR)));
	const auto crumbs = model.Breadcrumb();
	CHECK(crumbs.size() == 2u);
	CHECK_EQ(crumbs[1].first, "Sauvegardes");
	CHECK(model.Up());
	CHECK_EQ(model.Location(), AssetBrowserModel::ROOT);
}

// ── Opérations sur les fichiers (navigateur de ressources) ──────────────────

namespace {

/// Copie du projet de démonstration dans un dossier temporaire, ouverte par
/// un runtime : les opérations y écrivent sans toucher aux données des tests.
struct FileHarness {
	std::filesystem::path dir = std::filesystem::temp_directory_path() / "game_editor_asset_ops";
	ecs::ArchetypeRegistry registry;
	Runtime runtime{registry};
	AssetBrowserModel model;
	String root;

	FileHarness() {
		std::filesystem::remove_all(dir);
		std::filesystem::copy(std::filesystem::path(DEMO_PROJECT).parent_path(), dir,
							  std::filesystem::copy_options::recursive);
		std::filesystem::create_directories(dir / "scenes" / "pieces");
		std::ofstream(dir / "scenes" / "pieces" / "mur.scene") << "{}";
		std::filesystem::create_directories(dir / "scripts" / "outils");
		std::ofstream(dir / "scripts" / "outils" / "maths.script") << "fn double(x) { return 2 * x }";
		(void)runtime.LoadProjectFile(String((dir / "demo.json").string().c_str()));
		root = String(dir.string().c_str());
		model = AssetBrowserModel(&runtime.GetProject(), String("assets"), root, String("Dossier du projet"));
		model.SetProjectDirectory(root);
	}
	~FileHarness() { std::filesystem::remove_all(dir); }

	Option<AssetEntry> Entry(const String &location, const char *name) {
		for (const AssetEntry &entry : model.List(location))
			if (entry.name == name)
				return Some(entry);
		return NONE;
	}
};

} // namespace

TEST(AssetFiles_ScenesAndScriptsShowTheirSubfolders) {
	FileHarness h;
	// « Scènes » : les scènes du projet ET le sous-dossier `pieces/` du disque,
	// sans doublon des fichiers que le projet écrit lui-même.
	Option<AssetEntry> pieces = h.Entry(String(AssetBrowserModel::SCENES), "pieces");
	CHECK(pieces.IsSome());
	CHECK(pieces.Value().kind == AssetKind::FOLDER && !pieces.Value().managed);
	CHECK(h.Entry(String(AssetBrowserModel::SCENES), "Donjon").IsSome());
	CHECK(h.Entry(String(AssetBrowserModel::SCENES), "Donjon").Value().managed);
	CHECK(h.Entry(String(AssetBrowserModel::SCENES), "Donjon.scene").IsNone());
	CHECK(h.Entry(String(AssetBrowserModel::SCRIPTS), "outils").IsSome());
	CHECK(h.Entry(String(AssetBrowserModel::SCRIPTS), "torchlight.script").Value().managed);
	// Le chemin disque du dossier EST « Scènes » ; un sous-dossier remonte à lui.
	CHECK(h.model.Navigate(h.model.SceneFolder()));
	CHECK_EQ(h.model.Location(), AssetBrowserModel::SCENES);
	CHECK(h.model.Navigate(pieces.Value().location));
	const auto crumbs = h.model.Breadcrumb();
	CHECK(crumbs.size() == 3u);
	CHECK_EQ(crumbs[1].first, "Scènes");
	CHECK_EQ(crumbs[2].first, "pieces");
	CHECK(h.model.Up());
	CHECK_EQ(h.model.Location(), AssetBrowserModel::SCENES);
}

TEST(AssetFiles_DiskFilesAreCreatedRenamedDuplicatedMovedAndDeleted) {
	FileHarness h;
	AssetOperations ops(h.runtime, h.model);
	const String scenes(AssetBrowserModel::SCENES);
	auto folder = ops.CreateFolder(scenes, String("niveaux"));
	CHECK(folder.IsOk());
	CHECK(ops.CreateFolder(scenes, String("niveaux")).IsError()); // existe déjà
	CHECK(ops.CreateFolder(scenes, String("a/b")).IsError());     // séparateur interdit
	CHECK(ops.CreateFolder(String("assets/models"), String("x")).IsError()); // ressources partagées

	// Renommer un fichier garde son extension si on ne la donne pas.
	const String pieces = h.model.SceneFolder() + String("/pieces");
	AssetEntry wall = h.Entry(pieces, "mur.scene").Unwrap();
	auto renamed = ops.Rename(wall, String("muraille"));
	CHECK(renamed.IsOk());
	CHECK(renamed.Value().EndsWith("/pieces/muraille.scene"));

	// Dupliquer, puis déplacer la sélection (copie + original) dans `niveaux/`.
	AssetEntry wall2 = h.Entry(pieces, "muraille.scene").Unwrap();
	AssetOpReport copies = ops.Duplicate({wall2});
	CHECK(copies.done == 1 && copies.Ok());
	CHECK(h.Entry(pieces, "muraille (copie).scene").IsSome());
	AssetOpReport moved = ops.Move({h.Entry(pieces, "muraille.scene").Unwrap(),
									h.Entry(pieces, "muraille (copie).scene").Unwrap()},
								   folder.Value());
	CHECK_EQ(moved.done, 2);
	CHECK(h.Entry(folder.Value(), "muraille (copie).scene").IsSome());
	CHECK(h.model.List(pieces).empty());

	// Un dossier ne se range pas en lui-même ; la structure du projet ne bouge pas.
	AssetEntry levels = h.Entry(scenes, "niveaux").Unwrap();
	CHECK(!ops.Move({levels}, levels.location).Ok());
	AssetEntry scenesDir = h.Entry(h.root, "scenes").Unwrap();
	CHECK(ops.WhyLocked(scenesDir, "delete").IsSome());
	CHECK(ops.WhyLocked(h.Entry(String("assets"), "models").Unwrap(), "rename").IsSome());

	// Supprimer un dossier non vide (récursif).
	AssetOpReport deleted = ops.Delete({levels});
	CHECK_EQ(deleted.done, 1);
	CHECK(h.Entry(scenes, "niveaux").IsNone());
}

TEST(AssetFiles_ProjectItemsGoThroughTheProject) {
	FileHarness h;
	AssetOperations ops(h.runtime, h.model);
	const String scenes(AssetBrowserModel::SCENES), scripts(AssetBrowserModel::SCRIPTS);
	// Renommer un script : les nœuds qui le portent suivent.
	auto renamed = ops.Rename(h.Entry(scripts, "torchlight.script").Unwrap(), String("flamme.script"));
	CHECK(renamed.IsOk());
	CHECK_EQ(renamed.Value(), String(scripts) + String("/flamme"));
	CHECK(h.runtime.GetProject().FindScript(String("flamme")) != nullptr);
	CHECK(h.runtime.OpenScene(SceneRef{String("Donjon")}));
	CHECK_EQ(ScriptRef::Read(*h.runtime.ActiveDocument()->Find(String("Torche 1"))).script, "flamme");

	// Dupliquer et renommer une scène ; le script de jeu suit son nom.
	AssetOpReport copy = ops.Duplicate({h.Entry(scenes, "Vitrine").Unwrap()});
	CHECK(copy.done == 1);
	CHECK(h.runtime.GetProject().Fs().FindScene(SceneRef{"Vitrine (copie)"}) != nullptr);
	CHECK(ops.Rename(h.Entry(scenes, "Vitrine (copie)").Unwrap(), String("Galerie")).IsOk());
	CHECK(h.Entry(scripts, "Galerie.main.script").IsSome());
	CHECK(ops.WhyLocked(h.Entry(scripts, "Galerie.main.script").Unwrap(), "rename").IsSome());
	CHECK(ops.WhyLocked(h.Entry(scenes, "Galerie").Unwrap(), "move").IsSome());

	// Renommée, une scène laisse son ancien fichier jusqu'à l'enregistrement :
	// il appartient encore au projet, il n'est pas montré comme fichier libre.
	CHECK(ops.Rename(h.Entry(scenes, "Assemblages").Unwrap(), String("Ateliers")).IsOk());
	CHECK(std::filesystem::exists(h.dir / "scenes" / "Assemblages.scene"));
	h.model.SetOwnedFiles(h.runtime.ProjectFiles());
	CHECK(h.Entry(scenes, "Assemblages.scene").IsNone());
	CHECK(h.Entry(scenes, "Ateliers").IsSome());

	// Supprimer : la scène quitte le projet ; l'enregistrement efface ses fichiers.
	const size_t before = h.runtime.GetProject().documents.size();
	AssetOpReport deleted = ops.Delete({h.Entry(scenes, "Galerie").Unwrap(), h.Entry(scripts, "door.script").Unwrap()});
	CHECK_EQ(deleted.done, 2);
	CHECK_EQ(h.runtime.GetProject().documents.size(), before - 1);
	CHECK(h.runtime.GetProject().FindScript(String("door")) == nullptr);
	CHECK(h.runtime.SaveProject().IsOk());
	CHECK(!std::filesystem::exists(h.dir / "scripts" / "door.script"));
	CHECK(std::filesystem::exists(h.dir / "scripts" / "flamme.script"));
	CHECK(!std::filesystem::exists(h.dir / "scripts" / "torchlight.script"));
	CHECK(!std::filesystem::exists(h.dir / "scenes" / "Assemblages.scene"));
	CHECK(std::filesystem::exists(h.dir / "scenes" / "Ateliers.scene"));
	// Les fichiers hors projet (sous-dossiers) survivent à l'enregistrement.
	CHECK(std::filesystem::exists(h.dir / "scripts" / "outils" / "maths.script"));
	CHECK(std::filesystem::exists(h.dir / "scenes" / "pieces" / "mur.scene"));
}