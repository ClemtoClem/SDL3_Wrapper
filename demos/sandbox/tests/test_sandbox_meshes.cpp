// Tests unitaires — maillages MODÉLISÉS du sandbox (engine/mesh_library.hpp,
// Runtime::CreateMeshNode / ConvertToMesh / MeshChanged) : un nœud MESH
// affiche un modeling::Model enregistré dans `meshes/<nom>.mesh`, relu à
// l'ouverture du projet et emporté par un « Enregistrer sous ».
//
// Écrit UNIQUEMENT sous build/tests/tmp/ (ignoré par git).
#include "test.hpp"

#include <cmath>
#include <filesystem>

#include <modeling/operations.hpp>
#include <render3d/shape.hpp>
#include <sandbox/document/project_files.hpp>
#include <sandbox/editor/modeling_session.hpp>
#include <sandbox/engine/runtime.hpp>

using namespace sandbox;

namespace {

String Scratch(const char *name) {
	const std::string path = std::string("build/tests/tmp/meshes/") + name;
	std::error_code ignored;
	std::filesystem::remove_all(path, ignored);
	std::filesystem::create_directories(path, ignored);
	return String(path.c_str());
}

/// Une boîte de 2 × 1 × 1 construite comme dans SketchUp : un rectangle tiré.
modeling::Model PulledBox() {
	modeling::Model model;
	Option<modeling::FaceId> base = model.AddRectangle({0, 0, 0}, {2, 0, 0}, {0, 0, 1});
	if (base.IsSome())
		(void)modeling::PushPull(model, base.Unwrap(), model.Faces()[base.Unwrap()].normal.y > 0.f ? 1.f : -1.f);
	return model;
}

size_t TrianglesOf(Runtime &runtime, scene::NodeId id) {
	auto *shape = dynamic_cast<render3d::Shape *>(runtime.FindObject3D(id));
	return shape ? shape->Geometry().Indices().size() / 3 : 0;
}

} // namespace

TEST(Meshes, LibraryNamesFilesUniquelyAndBuildsRenderMeshes) {
	MeshLibrary library;
	library.SetRoot(Scratch("library"));
	EXPECT_EQ(library.UniqueSource(String("Ma table")), "meshes/Ma_table.mesh");
	EXPECT_EQ(library.UniqueSource(String("")), "meshes/maillage.mesh");
	library.Replace(String("meshes/Ma_table.mesh"), PulledBox());
	EXPECT_EQ(library.UniqueSource(String("Ma table")), "meshes/Ma_table_2.mesh");

	// Six faces doubles (vues des deux côtés) : 6 × 2 triangles × 2.
	std::shared_ptr<render3d::Mesh> mesh = library.RenderMesh(String("meshes/Ma_table.mesh"));
	ASSERT_TRUE(mesh != nullptr);
	EXPECT_EQ(mesh->Indices().size() / 3, size_t(24));
	// Pas reconstruit tant que le modèle ne change pas.
	EXPECT_TRUE(library.RenderMesh(String("meshes/Ma_table.mesh")) == mesh);

	EXPECT_TRUE(library.IsDirty(String("meshes/Ma_table.mesh")));
	EXPECT_TRUE(library.SaveAll().empty());
	EXPECT_FALSE(library.IsDirty(String("meshes/Ma_table.mesh")));
	EXPECT_TRUE(files::IsFile(library.Resolve(String("meshes/Ma_table.mesh"))));
}

TEST(Meshes, PrimitivesConvertToEditableModels) {
	const modeling::Model box = MeshLibrary::FromPrimitive(render3d::PrimitiveSpec::Box(1.f, 2.f, 3.f));
	size_t faces = 0;
	for (const modeling::Face &f : box.Faces())
		faces += f.alive ? 1 : 0;
	EXPECT_EQ(faces, size_t(6)); // les triangles coplanaires redeviennent des faces
	const math::FAABB bounds = box.Bounds();
	EXPECT_TRUE(std::abs(bounds.max.y - bounds.min.y - 2.f) < 1e-4f);
}

TEST(Meshes, MeshNodesAreSavedWithTheProjectAndReloaded) {
	const String root = Scratch("project");
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	ASSERT_TRUE(runtime.CreateProject(root, String("Atelier")).IsOk());

	Option<scene::NodeId> id = runtime.CreateMeshNode(String("Établi"), PulledBox());
	ASSERT_TRUE(id.IsSome());
	EXPECT_EQ(TrianglesOf(runtime, id.Unwrap()), size_t(24));
	const String source = VisualDesc::Read(*runtime.FindNode(id.Unwrap())).source;
	EXPECT_EQ(source, "meshes/Établi.mesh");

	// Modification du modèle : les nœuds qui l'affichent sont mis à jour.
	Result<modeling::Model *, String> model = runtime.Meshes().Get(source);
	ASSERT_TRUE(model.IsOk());
	(void)model.Value()->AddRectangle({5, 0, 0}, {1, 0, 0}, {0, 0, 1});
	runtime.MeshChanged(source);
	EXPECT_EQ(TrianglesOf(runtime, id.Unwrap()), size_t(28));

	ASSERT_TRUE(runtime.SaveProject().IsOk());
	const String dir = files::Join(root, String("Atelier"));
	EXPECT_TRUE(files::IsFile(files::Join(dir, source)));

	// Réouverture : le modèle est relu depuis son fichier.
	Runtime reopened(registry);
	ASSERT_TRUE(reopened.LoadProjectFile(files::Join(dir, String("Atelier.json"))).IsOk());
	const scene::NodeId again = reopened.ResolveId(String("Établi"));
	ASSERT_TRUE(again.Valid());
	EXPECT_EQ(TrianglesOf(reopened, again), size_t(28));

	// « Enregistrer sous » ailleurs : le maillage suit le projet.
	const String other = Scratch("copy");
	ASSERT_TRUE(reopened.SaveProjectFile(files::Join(other, String("Atelier.json"))).IsOk());
	EXPECT_TRUE(files::IsFile(files::Join(other, source)));
}

TEST(Meshes, ConvertingAPrimitiveKeepsTheNodeAndItsMaterial) {
	const String root = Scratch("convert");
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	ASSERT_TRUE(runtime.CreateProject(root, String("Conv")).IsOk());
	NodeDesc crate;
	crate.name = String("Caisse");
	crate.shape = ShapeKind::BOX;
	crate.material.baseColor = sdl3::Color{200, 40, 40, 255};
	Option<scene::NodeId> id = runtime.SpawnNode(crate);
	ASSERT_TRUE(id.IsSome());

	Result<String, String> converted = runtime.ConvertToMesh(id.Unwrap());
	ASSERT_TRUE(converted.IsOk());
	const VisualDesc visual = VisualDesc::Read(*runtime.FindNode(id.Unwrap()));
	EXPECT_TRUE(visual.shape == ShapeKind::MESH);
	EXPECT_EQ(visual.source, converted.Value());
	EXPECT_EQ(int(visual.material.baseColor.r), 200);
	EXPECT_EQ(TrianglesOf(runtime, id.Unwrap()), size_t(24));
	// Une lumière ou un groupe ne se convertit pas.
	Option<scene::NodeId> group = runtime.CreateGroup(String("Groupe"));
	ASSERT_TRUE(group.IsSome());
	EXPECT_TRUE(runtime.ConvertToMesh(group.Unwrap()).IsError());
}

namespace {

size_t AliveFaces(const modeling::Model &model) {
	size_t n = 0;
	for (const modeling::Face &f : model.Faces())
		n += f.alive ? 1 : 0;
	return n;
}

} // namespace

TEST(Meshes, ModelingSessionDrawsAndPullsLikeSketchUp) {
	const String root = Scratch("session");
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	ASSERT_TRUE(runtime.CreateProject(root, String("Session")).IsOk());
	Option<scene::NodeId> id = runtime.CreateMeshNode(String("Bloc"), modeling::Model{});
	ASSERT_TRUE(id.IsSome());
	render3d::Camera &camera = runtime.ActiveCamera();
	camera.position = {6.f, 7.f, 9.f};
	camera.target = {1.f, 0.f, 1.f};
	auto px = [&](const math::FVector3 &p) { return camera.WorldToScreen(p, 800.f, 600.f).Unwrap(); };

	ModelingSession session(runtime);
	ASSERT_TRUE(session.Begin(id.Unwrap()).IsOk());
	session.SetViewportSize(800.f, 600.f);
	EXPECT_TRUE(session.CurrentTool() == modeling::ToolKind::LINE);

	// Rectangle au sol : premier coin, puis cotes tapées « 2;1,5 » + Entrée.
	EXPECT_TRUE(session.TypeChar('r'));
	EXPECT_TRUE(session.CurrentTool() == modeling::ToolKind::RECTANGLE);
	session.PointerMove(px({0, 0, 0}));
	EXPECT_TRUE(session.Hovered().kind == modeling::InferenceKind::ORIGIN);
	EXPECT_TRUE(session.Click(px({0, 0, 0})));
	session.PointerMove(px({2, 0, 1.5f}));
	for (char c : String("2;1,5"))
		EXPECT_TRUE(session.TypeChar(c));
	EXPECT_EQ(session.Typed(), "2;1,5");
	EXPECT_TRUE(session.Commit());
	EXPECT_EQ(AliveFaces(*session.Model()), size_t(1));

	// Pousser/tirer la face d'un mètre : clic, la souris monte, cote tapée.
	EXPECT_TRUE(session.TypeChar('p'));
	EXPECT_TRUE(session.Click(px({1.f, 0.f, 0.75f})));
	session.PointerMove(px({1.f, 0.4f, 0.75f}));
	EXPECT_TRUE(session.TypeChar('1'));
	EXPECT_TRUE(session.Commit());
	EXPECT_EQ(AliveFaces(*session.Model()), size_t(6));
	EXPECT_TRUE(std::abs(session.Model()->Bounds().max.y - 1.f) < 1e-3f);
	// Le nœud affiche la boîte (6 faces × 2 triangles, vues des deux côtés).
	EXPECT_EQ(TrianglesOf(runtime, id.Unwrap()), size_t(24));

	// Le coin du haut s'accroche comme une extrémité ; l'aperçu le dit.
	session.SetTool(modeling::ToolKind::LINE);
	session.PointerMove(px({2.f, 1.f, 1.5f}));
	EXPECT_TRUE(session.Hovered().kind == modeling::InferenceKind::ENDPOINT);
	EXPECT_TRUE(session.BuildOverlay().marker.IsSome());

	// Annulation propre au modèle.
	EXPECT_TRUE(session.Undo());
	EXPECT_EQ(AliveFaces(*session.Model()), size_t(1));
	EXPECT_TRUE(session.Redo());
	EXPECT_EQ(AliveFaces(*session.Model()), size_t(6));

	// Toit à deux pans : ligne de faîtage d'un milieu à l'autre (elle coupe
	// la face du haut), sélectionnée puis montée de 0,5 m.
	session.SetTool(modeling::ToolKind::LINE);
	session.PointerMove(px({0.f, 1.f, 0.75f}));
	EXPECT_TRUE(session.Click(px({0.f, 1.f, 0.75f})));
	session.PointerMove(px({2.f, 1.f, 0.75f}));
	EXPECT_TRUE(session.Click(px({2.f, 1.f, 0.75f})));
	EXPECT_EQ(AliveFaces(*session.Model()), size_t(7));
	session.SetTool(modeling::ToolKind::SELECT);
	EXPECT_TRUE(session.Click(px({1.f, 1.f, 0.75f})));
	EXPECT_EQ(session.Selection().edges.size(), size_t(1));
	session.SetTool(modeling::ToolKind::MOVE);
	EXPECT_TRUE(session.Click(px({1.f, 1.f, 0.75f})));
	session.PointerMove(px({1.f, 2.f, 0.75f}));
	EXPECT_TRUE(session.TypeChar('0') && session.TypeChar(',') && session.TypeChar('5'));
	EXPECT_TRUE(session.Commit());
	EXPECT_TRUE(std::abs(session.Model()->Bounds().max.y - 1.5f) < 1e-3f);
	EXPECT_EQ(AliveFaces(*session.Model()), size_t(7));

	// Échap vide la sélection, puis, sans rien en cours, laisse l'éditeur
	// quitter la session.
	EXPECT_TRUE(session.Escape());
	EXPECT_TRUE(session.Selection().IsEmpty());
	EXPECT_FALSE(session.Escape());
	session.End();
	EXPECT_FALSE(session.IsActive());
	EXPECT_TRUE(runtime.Meshes().IsDirty(String("meshes/Bloc.mesh")));
}

TEST(Meshes, EveryNodeOfAnObjectCanBeModeledFromAnInstance) {
	const String root = Scratch("object");
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	ASSERT_TRUE(runtime.CreateProject(root, String("Objets")).IsOk());
	Option<scene::NodeId> table = runtime.CreateGroup(String("Table"));
	ASSERT_TRUE(table.IsSome());
	NodeDesc top;
	top.name = String("Plateau");
	top.shape = ShapeKind::BOX;
	top.dimensions = {2.f, 0.1f, 1.f};
	ASSERT_TRUE(runtime.SpawnNode(top, table.Unwrap()).IsSome());
	auto object = runtime.CreateObjectFromNode(table.Unwrap(), String("Table"));
	ASSERT_TRUE(object.IsOk());

	// Le plateau vu DANS l'instance est un nœud généré : il renvoie au nœud
	// « Plateau » de l'objet « Table ».
	const SceneDesc *scene = runtime.ActiveDocument();
	ASSERT_TRUE(scene != nullptr);
	scene::NodeId generated{};
	for (scene::NodeId id : scene->tree.AllNodes())
		if (scene->tree.Get(id)->name == "Plateau")
			generated = id;
	ASSERT_TRUE(generated.Valid());
	Option<std::pair<String, String>> owner = runtime.ObjectNodeOf(generated);
	ASSERT_TRUE(owner.IsSome());
	EXPECT_EQ(owner.Value().first, "Table");
	EXPECT_EQ(owner.Value().second, "Table/Plateau"); // l'objet contient le nœud « Table » lui-même

	// L'ouvrir pour le modéliser : l'objet devient le document édité.
	Result<scene::NodeId, String> opened = runtime.OpenObjectNode(generated);
	ASSERT_TRUE(opened.IsOk());
	EXPECT_TRUE(runtime.ActiveDocument()->IsObject());
	EXPECT_EQ(runtime.FindNode(opened.Value())->name, "Plateau");
	ModelingSession session(runtime);
	ASSERT_TRUE(session.Begin(opened.Value()).IsOk());
	EXPECT_EQ(AliveFaces(*session.Model()), size_t(6));
	session.End();

	// De retour dans la scène, l'instance montre le maillage modélisé.
	ASSERT_TRUE(runtime.OpenScene(SceneRef{scene->name}));
	runtime.RefreshObjectInstances();
	bool meshShown = false;
	for (scene::NodeId id : runtime.ActiveDocument()->tree.AllNodes()) {
		const scene::Node *node = runtime.ActiveDocument()->tree.Get(id);
		if (node->name == "Plateau" && VisualDesc::Has(*node))
			meshShown = VisualDesc::Read(*node).shape == ShapeKind::MESH;
	}
	EXPECT_TRUE(meshShown);
}

TEST(Meshes, CameraOrbitsAroundAFixedPointAndZoomsTowardTheCursor) {
	const String root = Scratch("camera");
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	ASSERT_TRUE(runtime.CreateProject(root, String("Camera")).IsOk());
	NodeDesc crate;
	crate.name = String("Caisse");
	crate.shape = ShapeKind::BOX;
	crate.transform.position = {2.f, 0.5f, -3.f};
	ASSERT_TRUE(runtime.SpawnNode(crate).IsSome());
	render3d::Camera &camera = runtime.ActiveCamera();

	// Orbite : le point visé garde son pixel (SketchUp), la caméra tourne.
	const math::FVector3 pivot{1.f, 0.f, -2.f};
	const math::FVector2 before = camera.WorldToScreen(pivot, 800.f, 600.f).Unwrap();
	const math::FVector3 eyeBefore = camera.position;
	runtime.OrbitAround(0.4f, -0.2f, pivot);
	const math::FVector2 after = camera.WorldToScreen(pivot, 800.f, 600.f).Unwrap();
	EXPECT_TRUE(before.Distance(after) < 0.5f);
	EXPECT_TRUE(std::abs((camera.position - pivot).Length() - (eyeBefore - pivot).Length()) < 1e-3f);
	EXPECT_TRUE(camera.position.Distance(eyeBefore) > 0.1f);

	// Zoom vers le curseur : la caméra avance le long du rayon du curseur.
	const math::FRay ray = camera.ScreenPointToRay(400.f, 300.f, 800.f, 600.f);
	const math::FVector3 start = camera.position;
	runtime.ZoomTowards(ray, 0.5f);
	const math::FVector3 moved = camera.position - start;
	EXPECT_TRUE(moved.Length() > 1e-3f);
	EXPECT_TRUE(moved.Normalize().Dot(ray.direction) > 0.999f);

	// Boîte englobante d'un nœud : la caisse unité centrée en (2, 0,5, -3).
	Option<math::FAABB> box = runtime.NodeWorldBounds(runtime.ResolveId(String("Caisse")));
	ASSERT_TRUE(box.IsSome());
	EXPECT_TRUE(std::abs(box.Value().max.x - 2.5f) < 1e-3f && std::abs(box.Value().min.z + 3.5f) < 1e-3f);
	ASSERT_TRUE(runtime.SceneBounds().IsSome());
	runtime.FrameBounds(box.Unwrap());
	EXPECT_TRUE(camera.WorldToScreen({2.f, 0.5f, -3.f}, 800.f, 600.f).IsSome());
}

TEST(Meshes, SketchPlaneKeepsDrawingOnTheChosenPlane) {
	const String root = Scratch("sketch");
	ecs::ArchetypeRegistry registry;
	Runtime runtime(registry);
	ASSERT_TRUE(runtime.CreateProject(root, String("Esquisse")).IsOk());
	Option<scene::NodeId> id = runtime.CreateMeshNode(String("Panneau"), modeling::Model{});
	ASSERT_TRUE(id.IsSome());
	render3d::Camera &camera = runtime.ActiveCamera();
	camera.position = {1.f, 1.f, 6.f};
	camera.target = {1.f, 1.f, 0.f};
	auto px = [&](const math::FVector3 &p) { return camera.WorldToScreen(p, 800.f, 600.f).Unwrap(); };
	ModelingSession session(runtime);
	ASSERT_TRUE(session.Begin(id.Unwrap()).IsOk());
	session.SetViewportSize(800.f, 600.f);
	// Plan de face (z = 0) : un rectangle dessiné de face est vertical.
	session.SetSketchPlane(Some(modeling::InferenceSettings::WorkPlane{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}}));
	EXPECT_TRUE(session.IsSketching());
	ASSERT_TRUE(session.SketchPlaneWorld().IsSome());
	session.SetTool(modeling::ToolKind::RECTANGLE);
	session.PointerMove(px({0.3f, 0.2f, 0.f}));
	EXPECT_TRUE(session.Hovered().kind == modeling::InferenceKind::ON_PLANE);
	EXPECT_TRUE(session.Click(px({0.3f, 0.2f, 0.f})));
	session.PointerMove(px({1.8f, 1.6f, 0.f}));
	EXPECT_TRUE(session.Click(px({1.8f, 1.6f, 0.f})));
	ASSERT_EQ(AliveFaces(*session.Model()), size_t(1));
	EXPECT_TRUE(std::abs(std::abs(session.Model()->Faces()[0].normal.z) - 1.f) < 1e-4f);
	const math::FAABB box = session.Model()->Bounds();
	EXPECT_TRUE(std::abs(box.max.z) < 1e-4f && std::abs(box.min.z) < 1e-4f);
	session.SetSketchPlane(NONE);
	EXPECT_FALSE(session.IsSketching());
}
