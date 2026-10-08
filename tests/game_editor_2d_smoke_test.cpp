// Tests unitaires — game_editor : l'environnement 2D (document, moteur pur,
// API des scripts). Aucune fenêtre : le dessin SDL (canvas2d_render.hpp)
// n'est pas exercé ici, seulement ce qu'il reçoit.
#define USE_TEST

#include "core/test.hpp"

#include "../examples/game_editor_demo/document/project.hpp"
#include "../examples/game_editor_demo/document/project_files.hpp"
#include "../examples/game_editor_demo/engine/canvas2d.hpp"
#include "../examples/game_editor_demo/engine/runtime.hpp"

#include <cmath>

using namespace game_editor;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

scene::Node Item2D(const char *name, CanvasItemDesc item, math::FVector2 pos, const char *type = node_kind::SHAPE2D) {
	scene::Node node;
	node.name = String(name);
	node.type = String(type);
	node.transform.position = {pos.x, pos.y, 0.f};
	item.Write(node);
	return node;
}

CanvasItemDesc Rect(float w, float h, int z = 0) {
	CanvasItemDesc item;
	item.size = {w, h};
	item.z = z;
	return item;
}

/// Projet vierge, une scène active.
struct Harness {
	ecs::ArchetypeRegistry registry;
	Runtime runtime{registry};

	Harness() { runtime.OpenProject(files::MakeBlankProject(String("2D"))); }

	SceneDesc &Scene() { return *runtime.ActiveScene(); }

	data::script::Value Run(const char *source) {
		auto result = runtime.GameplayVm().Run(StringView(source));
		if (result.IsError()) {
			test::ReportFailure(__FILE__, __LINE__, result.Error().Format().CStr());
			return data::script::Value::Nil();
		}
		return result.Value();
	}
	String Error(const char *source) {
		auto result = runtime.GameplayVm().Run(StringView(source));
		return result.IsOk() ? String() : result.Error().Format();
	}
};

} // namespace

// ============================================================================
// Moteur pur
// ============================================================================

TEST(Canvas2D, TransformsComposeAndInvert) {
	const Transform2D t = Transform2D::Translate(10.f, 20.f) * Transform2D::Rotate(90.f) * Transform2D::Scale(2.f);
	const math::FVector2 p = t.Apply({1.f, 0.f});
	// (1,0) ×2 → (2,0), tourné de 90° (horaire, Y vers le bas) → (0,2), décalé.
	EXPECT_TRUE(Near(p.x, 10.f) && Near(p.y, 22.f));
	Option<Transform2D> inverse = t.Inverse();
	ASSERT_TRUE(inverse.IsSome());
	const math::FVector2 back = inverse.Value().Apply(p);
	EXPECT_TRUE(Near(back.x, 1.f) && Near(back.y, 0.f));
	EXPECT_TRUE(Near(t.MeanScale(), 2.f));
	EXPECT_TRUE(Near(t.RotationDegrees(), 90.f));
	EXPECT_TRUE(Transform2D::Scale(0.f).Inverse().IsNone());
}

TEST(Canvas2D, DocumentRoundTrip) {
	CanvasItemDesc text;
	text.kind = CanvasItemKind::TEXT;
	text.text = String("Score : 12");
	text.fontSize = 18.f;
	text.align = TextAlign2D::RIGHT;
	text.color = sdl3::Color{10, 20, 30, 40};
	text.z = 7;
	scene::Node node;
	text.Write(node);
	const CanvasItemDesc back = CanvasItemDesc::Read(node);
	EXPECT_TRUE(back.kind == CanvasItemKind::TEXT && back.text == "Score : 12" && back.align == TextAlign2D::RIGHT);
	EXPECT_TRUE(back.z == 7 && back.color.a == 40 && Near(back.fontSize, 18.f));

	CanvasItemDesc poly;
	poly.kind = CanvasItemKind::POLYGON;
	poly.points = {{0.f, -5.f}, {4.f, 3.f}, {-4.f, 3.f}};
	poly.outline = 2.f;
	poly.filled = false;
	poly.Write(node); // remplace le composant
	const CanvasItemDesc polyBack = CanvasItemDesc::Read(node);
	EXPECT_TRUE(polyBack.kind == CanvasItemKind::POLYGON && polyBack.points.size() == 3u && !polyBack.filled);
	EXPECT_TRUE(Near(polyBack.points[1].x, 4.f));

	// La scène garde ses réglages 2D ; un ancien fichier sans eux reçoit les défauts.
	SceneDesc scene;
	scene.SetName(String("Menu"));
	scene.canvas.width = 800;
	scene.canvas.render3d = false;
	(void)scene.tree.Add(scene.tree.Root(), node);
	auto reloaded = SceneDesc::FromJson(scene.ToJson());
	ASSERT_TRUE(reloaded.IsOk());
	EXPECT_TRUE(reloaded.Value().canvas.width == 800 && !reloaded.Value().canvas.render3d);
	EXPECT_TRUE(CanvasItemDesc::Has(*reloaded.Value().Find(String("Menu") == "" ? String() : node.name)));
	auto legacy = data::Node::MakeObject();
	legacy->Set("name", data::Node::MakeString(String("Ancienne")));
	auto old = SceneDesc::FromJson(legacy);
	ASSERT_TRUE(old.IsOk());
	EXPECT_TRUE(old.Value().canvas.width == 1280 && old.Value().canvas.render3d);
}

TEST(Canvas2D, FrameOrdersSpacesAndHides) {
	SceneDesc scene;
	scene.SetName(String("S"));
	const scene::NodeId root = scene.tree.Root();
	const scene::NodeId back = scene.tree.Add(root, Item2D("Fond", Rect(100, 100, -1), {0, 0}));
	const scene::NodeId parent = scene.tree.Add(root, Item2D("Parent", Rect(10, 10), {100, 50}));
	(void)scene.tree.Add(parent, Item2D("Enfant", Rect(4, 4), {5, 0}));
	scene::Node layer;
	layer.name = String("HUD");
	layer.type = String(node_kind::CANVAS_LAYER);
	const scene::NodeId hud = scene.tree.Add(root, layer);
	(void)scene.tree.Add(hud, Item2D("Vie", Rect(50, 8, 5), {20, 20}));
	scene::Node hidden = Item2D("Caché", Rect(1, 1), {0, 0});
	hidden.visible = false;
	(void)scene.tree.Add(root, hidden);
	scene::Node group; // groupe 3D : ses enfants 2D repartent de l'origine
	group.name = String("Groupe3D");
	group.type = String(node_kind::GROUP);
	group.transform.position = {1000.f, 1000.f, 0.f};
	const scene::NodeId g = scene.tree.Add(root, group);
	(void)scene.tree.Add(g, Item2D("Sous3D", Rect(1, 1), {3, 4}));

	const Canvas2DFrame frame = BuildCanvasFrame(scene);
	ASSERT_TRUE(frame.items.size() == 5u);
	EXPECT_TRUE(frame.items.front().id == back);           // z = -1 d'abord
	EXPECT_EQ(scene.tree.Get(frame.items.back().id)->name, "Vie"); // z = 5 en dernier
	EXPECT_TRUE(frame.items.back().space == Space2D::SCREEN);
	for (const DrawItem2D &item : frame.items) {
		const String name = scene.tree.Get(item.id)->name;
		EXPECT_TRUE(name != "Caché");
		if (name == "Enfant")
			EXPECT_TRUE(Near(item.transform.Origin().x, 105.f) && Near(item.transform.Origin().y, 50.f));
		if (name == "Sous3D")
			EXPECT_TRUE(Near(item.transform.Origin().x, 3.f) && item.space == Space2D::WORLD);
	}
	Option<NodePlacement2D> placement = PlaceNode2D(scene, scene.FindId(String("Enfant")));
	ASSERT_TRUE(placement.IsSome());
	EXPECT_TRUE(Near(placement.Value().parent.Origin().x, 100.f));
	EXPECT_TRUE(PlaceNode2D(scene, g).IsNone());
}

TEST(Canvas2D, ViewsMapReferenceScreenAndCamera) {
	// Vue deux fois plus large que haute : bandes sur les côtés.
	const View2D game = View2D::Game({0.f, 0.f, 2000.f, 720.f}, {1280.f, 720.f}, NONE);
	EXPECT_TRUE(Near(game.frame.x, 360.f) && Near(game.frame.w, 1280.f));
	const math::FVector2 corner = game.screen.Apply({1280.f, 720.f});
	EXPECT_TRUE(Near(corner.x, 1640.f) && Near(corner.y, 720.f));
	// Caméra : le point visé arrive au centre de l'écran de référence.
	const View2D followed = View2D::Game({0.f, 0.f, 1280.f, 720.f}, {1280.f, 720.f},
										 Some(Camera2DState{scene::NodeId{}, {5000.f, 100.f}, 2.f}));
	const math::FVector2 center = followed.world.Apply({5000.f, 100.f});
	EXPECT_TRUE(Near(center.x, 640.f) && Near(center.y, 360.f));
	EXPECT_TRUE(Near(followed.world.MeanScale(), 2.f));
	// Éditeur : `center` au milieu de la vue.
	const View2D editor = View2D::Editor({100.f, 0.f, 400.f, 300.f}, {1280.f, 720.f}, {640.f, 360.f}, 0.25f);
	const math::FVector2 mid = editor.world.Apply({640.f, 360.f});
	EXPECT_TRUE(Near(mid.x, 300.f) && Near(mid.y, 150.f));
	EXPECT_TRUE(Near(View2D::FitZoom({0.f, 0.f, 640.f, 720.f}, {1280.f, 720.f}, 1.f), 0.5f));
}

TEST(Canvas2D, PickingAndOverlaps) {
	std::vector<DrawItem2D> items;
	items.push_back(DrawItem2D{scene::NodeId{}, Rect(100, 100), Transform2D::Translate(50, 50), Space2D::WORLD, 0});
	CanvasItemDesc circle;
	circle.kind = CanvasItemKind::CIRCLE;
	circle.size = {20.f, 20.f};
	items.push_back(DrawItem2D{scene::NodeId{}, circle, Transform2D::Translate(90, 90), Space2D::WORLD, 1});
	const View2D view = View2D::Editor({0.f, 0.f, 200.f, 200.f}, {200.f, 200.f}, {100.f, 100.f}, 1.f);
	EXPECT_TRUE(PickItem(items, view, {90.f, 90.f}).Value() == 1u);   // le cercle, devant
	EXPECT_TRUE(PickItem(items, view, {10.f, 10.f}).Value() == 0u);   // le coin du carré
	EXPECT_TRUE(PickItem(items, view, {150.f, 20.f}).IsNone());
	// Trait : attrapable à quelques pixels près seulement.
	CanvasItemDesc line;
	line.kind = CanvasItemKind::LINE;
	line.points = {{0.f, 0.f}, {100.f, 0.f}};
	line.outline = 2.f;
	EXPECT_TRUE(HitsLocal(line, {50.f, 2.5f}, 2.f));
	EXPECT_FALSE(HitsLocal(line, {50.f, 8.f}, 2.f));
	// Recouvrements (boîtes orientées) : un carré tourné de 45° touche son voisin.
	DrawItem2D a{scene::NodeId{}, Rect(10, 10), Transform2D::Translate(0, 0), Space2D::WORLD, 0};
	DrawItem2D b{scene::NodeId{}, Rect(10, 10), Transform2D::Translate(12, 0), Space2D::WORLD, 0};
	EXPECT_FALSE(Overlaps(a, b));
	b.transform = Transform2D::Translate(12, 0) * Transform2D::Rotate(45.f);
	EXPECT_TRUE(Overlaps(a, b));
}

TEST(Canvas2D, TemplatesAndDuplicationKeepComponents) {
	Option<NodeDesc> text = MakeNodeFromTemplate(String("label2d"));
	ASSERT_TRUE(text.IsSome());
	const scene::Node node = text.Value().ToNode();
	EXPECT_EQ(node.type, String(node_kind::LABEL2D));
	EXPECT_TRUE(CanvasItemDesc::Read(node).kind == CanvasItemKind::TEXT && Is2DNode(node));
	// NodeDesc::FromNode (duplication, copier-coller) garde le composant 2D.
	const scene::Node copy = NodeDesc::FromNode(node).ToNode();
	EXPECT_TRUE(CanvasItemDesc::Has(copy) && CanvasItemDesc::Read(copy).text == CanvasItemDesc::Read(node).text);
	Option<NodeDesc> camera = MakeNodeFromTemplate(String("camera2d"));
	ASSERT_TRUE(camera.IsSome() && Camera2DDesc::Has(camera.Value().ToNode()));
}

// ============================================================================
// API des scripts
// ============================================================================

TEST(Canvas2DApi, NodesSpawnMoveStyleAndCollide) {
	Harness h;
	h.runtime.Play();
	EXPECT_EQ(h.Run("return node2d.spawn({name: \"Joueur\", kind: \"rect\", pos: [100, 100], size: [20, 20], "
					"color: \"#ff0000\", z: 3})")
				  .ToDisplayString(),
			  "Joueur");
	h.Run("node2d.spawn({name: \"Mur\", kind: \"rect\", pos: [130, 100], size: [20, 20]})\n"
		  "node2d.spawn({name: \"Titre\", kind: \"text\", text: \"Bonjour\", pos: [640, 40], size: 30})");
	EXPECT_EQ(h.Run("return node2d.overlaps(\"Joueur\", \"Mur\")").ToDisplayString(), "false");
	EXPECT_EQ(h.Run("node2d.move(\"Joueur\", 15, 0)\nreturn node2d.overlaps(\"Joueur\", \"Mur\")").ToDisplayString(),
			  "true");
	EXPECT_EQ(h.Run("return node2d.position(\"Joueur\")").ToDisplayString(), "[115, 100]");
	EXPECT_EQ(h.Run("return node2d.at(122, 101)").ToDisplayString(), "Joueur"); // zone commune : z = 3 devant
	EXPECT_EQ(h.Run("node2d.set_color(\"Titre\", [0, 255, 0])\nnode2d.set_text(\"Titre\", \"Salut\")\n"
					"return [node2d.text(\"Titre\"), node2d.color(\"Titre\")]")
				  .ToDisplayString(),
			  "[Salut, [0, 255, 0, 255]]");
	EXPECT_EQ(h.Run("return node2d.bounds(\"Mur\")").ToDisplayString(), "[120, 90, 20, 20]");
	EXPECT_TRUE(h.Run("return node2d.position(\"Personne\")").IsNil());
	EXPECT_TRUE(h.Error("node2d.spawn({kind: \"hexagone\"})").Contains("inconnu"));
	EXPECT_EQ(h.Run("return canvas.size()").ToDisplayString(), "[1280, 720]");
	// L'arrêt restaure la scène d'avant la partie.
	h.runtime.Stop();
	EXPECT_TRUE(h.Scene().Find(String("Joueur")) == nullptr);
}

TEST(Canvas2DApi, ImmediateDrawingsLastOneFrame) {
	Harness h;
	h.runtime.Play();
	h.Run("draw2d.rect(10, 10, 100, 20, \"#20202080\")\n"
		  "draw2d.text(\"PV 3\", 12, 12, {size: 16, color: [255, 255, 255]})\n"
		  "draw2d.circle(50, 50, 8, [255, 200, 0], {space: \"world\", z: -2})\n"
		  "draw2d.polyline([[0, 0], [10, 10], [20, 0]], \"#ffffff\", {width: 3})");
	const Canvas2DFrame frame = h.runtime.Frame2D();
	ASSERT_TRUE(frame.items.size() == 4u);
	EXPECT_TRUE(frame.items.front().item.kind == CanvasItemKind::CIRCLE); // z = -2
	EXPECT_TRUE(frame.items.front().space == Space2D::WORLD);
	EXPECT_TRUE(frame.items.back().space == Space2D::SCREEN);
	EXPECT_TRUE(h.Error("draw2d.rect(0, 0, 1, 1, \"rouge\")").Contains("couleur"));
	EXPECT_TRUE(h.Error("draw2d.rect(0, 0, 1, 1, [1, 1, 1], {space: \"lune\"})").Contains("space"));
	h.runtime.Update(1.f / 60.f); // nouvelle image : les dessins sont repartis
	EXPECT_TRUE(h.runtime.ScriptDrawings2D().empty());
	h.runtime.Stop();
}

TEST(Canvas2DApi, CameraAndPointerMapping) {
	Harness h;
	SceneDesc &scene = h.Scene();
	scene::Node camera;
	camera.name = String("Cam");
	camera.type = String(node_kind::CAMERA2D);
	camera.transform.position = {2000.f, 0.f, 0.f};
	Camera2DDesc{2.f, false}.Write(camera);
	(void)scene.tree.Add(scene.tree.Root(), camera);
	h.runtime.Play();
	EXPECT_TRUE(h.Run("return canvas.camera()").IsNil()); // pas encore « du jeu »
	EXPECT_EQ(h.Run("canvas.set_camera(\"Cam\")\nreturn canvas.camera()").ToDisplayString(), "[2000, 0, 2]");
	// La vue du jeu occupe 640×360 à partir de (100, 50) dans la fenêtre.
	h.runtime.SetView2DRect({100.f, 50.f, 640.f, 360.f});
	Option<math::FVector2> screen = h.runtime.PointerTo2D(420.f, 230.f, Space2D::SCREEN);
	ASSERT_TRUE(screen.IsSome());
	EXPECT_TRUE(Near(screen.Value().x, 640.f) && Near(screen.Value().y, 360.f));
	Option<math::FVector2> world = h.runtime.PointerTo2D(420.f, 230.f, Space2D::WORLD);
	ASSERT_TRUE(world.IsSome());
	EXPECT_TRUE(Near(world.Value().x, 2000.f) && Near(world.Value().y, 0.f));
	EXPECT_TRUE(h.runtime.PointerTo2D(10.f, 10.f, Space2D::SCREEN).IsNone());
	h.runtime.Stop();
}

int main() { return RUN_ALL_TESTS(); }
