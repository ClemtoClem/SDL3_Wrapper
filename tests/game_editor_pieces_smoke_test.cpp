// Tests unitaires — game_editor : assemblage d'un niveau à partir de scènes
// emballées (« pièces ») — instanciation PLACÉE (position et rotation posées
// avant la construction, corps physiques compris), chemins relatifs au
// projet, scripts de nœud des instances en mode Jeu.
#define USE_TEST

#include "core/test.hpp"

#include "../examples/game_editor_demo/document/project_files.hpp"
#include "../examples/game_editor_demo/engine/runtime.hpp"

#include <cmath>
#include <filesystem>

using namespace game_editor;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

/// Une « pièce » : un pivot, un mur solide au NORD (+z) et une torche
/// scriptée — enregistrée comme scène emballée dans un dossier de projet.
struct Harness {
	ecs::ArchetypeRegistry registry;
	Runtime runtime{registry};
	std::filesystem::path dir;

	Harness() {
		dir = std::filesystem::temp_directory_path() / "game_editor_pieces_test";
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir / "pieces");
		Project project = files::MakeBlankProject(String("Pièces"));
		ScriptAsset torch;
		torch.name = String("torche");
		torch.source = String("var vus = []\nclass Torche extends Behaviour { fn on_start() { vus.append(this.node()) } }\n");
		project.scripts.push_back(torch);
		(void)files::SaveProject(project, String((dir / "pieces.json").string().c_str()));
		(void)runtime.LoadProjectFile(String((dir / "pieces.json").string().c_str()));

		ObjectDesc piece = ObjectDesc::Group(String("Pièce"));
		const scene::NodeId root = runtime.SpawnNode(piece).Unwrap();
		ObjectDesc wall;
		wall.name = String("Mur");
		wall.dimensions = {4.f, 3.f, 0.5f};
		wall.transform.position = {0.f, 1.5f, 3.f};
		wall.physics.body = BodyKind::STATIC;
		wall.physics.halfExtents = {2.f, 1.5f, 0.25f};
		(void)runtime.SpawnNode(wall, root);
		ObjectDesc lamp = ObjectDesc::Light(String("Torche"), LightDesc{});
		lamp.script = String("torche");
		(void)runtime.SpawnNode(lamp, root);
		ASSERT_TRUE(runtime.SavePackedScene(root, String((dir / "pieces" / "piece.scene").string().c_str())).IsOk());
		(void)runtime.RemoveNode(root);
	}
	~Harness() { std::filesystem::remove_all(dir); }

	data::script::Value Run(const char *source) {
		auto result = runtime.GameplayVm().Run(StringView(source));
		if (result.IsError()) {
			test::ReportFailure(__FILE__, __LINE__, result.Error().Format().CStr());
			return data::script::Value::Nil();
		}
		return result.Value();
	}
};

} // namespace

TEST(Pieces, PlacedInstanceRotatesItsContentAndColliders) {
	Harness h;
	scene::Transform place;
	place.position = {10.f, 0.f, 0.f};
	place.SetEulerDegrees({0.f, 90.f, 0.f}); // un quart de tour : le nord passe à l'est
	// Chemin RELATIF : cherché dans le dossier du projet.
	auto created = h.runtime.InstantiateSceneFile(String("pieces/piece.scene"), scene::NodeId{}, Some(place));
	ASSERT_TRUE(created.IsOk());
	const SceneDesc &scene = *h.runtime.ActiveScene();
	const scene::NodeId wall = h.runtime.ResolveId(scene.tree.PathOf(created.Value()) + String("/Mur"));
	ASSERT_TRUE(wall.Valid());
	const math::FVector3 world = scene.tree.GlobalPosition(wall);
	EXPECT_TRUE(Near(world.x, 13.f) && Near(world.z, 0.f)); // (0, 1.5, 3) tourné vers +x, décalé de 10
	// Le corps statique est né au bon endroit (pas à l'emplacement du fichier).
	Option<ecs::Entity> entity = h.runtime.FindEntity(wall);
	ASSERT_TRUE(entity.IsSome());
	auto body = h.registry.GetComponent<physics::RigidBody>(entity.Unwrap());
	ASSERT_TRUE(body.IsSome());
	EXPECT_TRUE(Near(body.Unwrap()->position.x, 13.f) && Near(body.Unwrap()->position.z, 0.f));
}

TEST(Pieces, ScriptsOfInstancesStartWithUnambiguousHandles) {
	Harness h;
	h.runtime.Play();
	// node.instantiate(chemin, {parent, pos, rot}) depuis un script de jeu.
	h.Run("node.create_group(\"Niveau\")\n"
		  "let a = node.instantiate(\"pieces/piece.scene\", {parent: \"Niveau\", pos: [0, 0, 0]})\n"
		  "let b = node.instantiate(\"pieces/piece.scene\", {parent: \"Niveau\", pos: [8, 0, 0], rot: [0, 180, 0]})\n");
	EXPECT_EQ(h.Run("return [a, b]").ToDisplayString(), "[Pièce, Pièce 2]");
	EXPECT_EQ(h.Run("let p = node.world_position(node.path(b) .. \"/Mur\")\nreturn [math.round(p[0]), math.round(p[2])]")
				  .ToDisplayString(),
			  "[8, -3]");
	// Deux torches portent le même nom : chacune reçoit son CHEMIN en `self`.
	const scene::NodeTree &tree = h.runtime.ActiveScene()->tree;
	std::vector<scene::NodeId> torches;
	tree.Traverse(tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
		if (node.name == "Torche")
			torches.push_back(id);
	});
	ASSERT_TRUE(torches.size() == 2u);
	EXPECT_TRUE(h.runtime.ScriptHandle(torches[0]).Contains('/'));
	EXPECT_TRUE(h.runtime.ScriptHandle(torches[0]) != h.runtime.ScriptHandle(torches[1]));
	EXPECT_EQ(h.runtime.ScriptHandle(tree.Get(tree.Root())->id), tree.Get(tree.Root())->name);
	EXPECT_TRUE(h.runtime.ScriptErrorCount() == 0);
	h.runtime.Stop();
	// L'arrêt restaure la scène d'avant la partie : les instances disparaissent.
	EXPECT_TRUE(h.runtime.ActiveScene()->Find(String("Niveau")) == nullptr);
}

int main() { return RUN_ALL_TESTS(); }
