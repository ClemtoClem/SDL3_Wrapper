// Tests unitaires — module scene:: (hiérarchie de nœuds générique).
//
// Couvre ce qu'une fondation de moteur doit garantir AVANT que quoi que ce
// soit s'appuie dessus : identité stable, hiérarchie sans cycle, transforms
// hérités, chemins, duplication avec re-câblage des références, validation
// d'un arbre venu d'un fichier, aller-retour de sérialisation, et tenue en
// charge jusqu'à 100 000 nœuds.
//
// Aucun GPU, aucune fenêtre : `scene::` ne dépend ni de render3d, ni de
// physics, ni de ui — c'est précisément ce qui rend ce fichier possible.
#define USE_TEST

#include "core/test.hpp"
#include "scene/scene.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>

using namespace scene;

namespace {

constexpr float EPS = 1e-4f;

[[nodiscard]] bool Near(float a, float b, float eps = EPS) {
	return (a - b) < eps && (b - a) < eps;
}
[[nodiscard]] bool NearVec(const math::FVector3& a, const math::FVector3& b, float eps = EPS) {
	return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

/// La voiture de la spécification — assez profonde pour que l'héritage des
/// transforms ait un sens à plusieurs niveaux.
struct Car {
	NodeTree tree;
	NodeId car, body, mesh, wheels, frontLeft, frontRight, engine, camera;

	Car() : tree(String("Scene")) {
		car = tree.Create(tree.Root(), String("Car"));
		body = tree.Create(car, String("Body"));
		mesh = tree.Create(body, String("Mesh"));
		wheels = tree.Create(car, String("Wheels"));
		frontLeft = tree.Create(wheels, String("FrontLeft"));
		frontRight = tree.Create(wheels, String("FrontRight"));
		engine = tree.Create(car, String("Engine"));
		camera = tree.Create(car, String("Camera"));
	}
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// 1. Création et identité
// ─────────────────────────────────────────────────────────────────────────

TEST(SceneNode, ANewTreeHasARootAndNothingElse) {
	NodeTree tree(String("Scene"));
	ASSERT_TRUE(tree.Root().Valid());
	EXPECT_EQ(tree.Size(), size_t(1));
	EXPECT_TRUE(tree.Get(tree.Root())->name == String("Scene"));
	EXPECT_TRUE(!tree.ParentOf(tree.Root()).Valid());
	EXPECT_TRUE(tree.ChildrenOf(tree.Root()).empty());
}

TEST(SceneNode, CreatesChildrenAndGrandchildrenInOrder) {
	Car car;
	EXPECT_EQ(car.tree.Size(), size_t(9));
	EXPECT_EQ(car.tree.ChildrenOf(car.car).size(), size_t(4));
	EXPECT_TRUE(car.tree.ChildrenOf(car.car)[0] == car.body);
	EXPECT_TRUE(car.tree.ChildrenOf(car.car)[3] == car.camera);
	EXPECT_TRUE(car.tree.ParentOf(car.mesh) == car.body);
	EXPECT_EQ(car.tree.DepthOf(car.mesh), size_t(3));
}

TEST(SceneNode, IdentityIsTheIdNotTheName) {
	Car car;
	NodeId id = car.frontLeft;
	EXPECT_TRUE(car.tree.Rename(id, String("RoueAvantGauche")));
	// Le nœud n'a pas changé d'identité…
	EXPECT_TRUE(car.tree.Get(id) != nullptr);
	EXPECT_TRUE(car.tree.Get(id)->name == String("RoueAvantGauche"));
	// …et deux nœuds peuvent porter le même nom sans se confondre.
	NodeId twin = car.tree.Create(car.wheels, String("RoueAvantGauche"));
	EXPECT_TRUE(twin != id);
	EXPECT_EQ(car.tree.ChildrenOf(car.wheels).size(), size_t(3));
}

TEST(SceneNode, AStaleIdIsDetectedInsteadOfPointingAtTheNodeThatTookItsPlace) {
	NodeTree tree;
	NodeId first = tree.Create(tree.Root(), String("A"));
	EXPECT_TRUE(tree.Remove(first));
	EXPECT_TRUE(tree.Get(first) == nullptr);
	// Le nouvel emplacement réutilise l'indice, mais PAS la génération.
	NodeId second = tree.Create(tree.Root(), String("B"));
	EXPECT_EQ(second.index, first.index);
	EXPECT_TRUE(second != first);
	EXPECT_TRUE(tree.Get(first) == nullptr);
	EXPECT_TRUE(tree.Get(second) != nullptr);
}

TEST(SceneNode, UniqueChildNameOnlyDisambiguatesSiblings) {
	NodeTree tree;
	NodeId a = tree.Create(tree.Root(), String("Wall"));
	tree.Create(tree.Root(), tree.UniqueChildName(tree.Root(), String("Wall")));
	EXPECT_TRUE(tree.Get(tree.ChildrenOf(tree.Root())[1])->name == String("Wall 2"));
	// Sous un AUTRE parent, « Wall » reste disponible.
	EXPECT_TRUE(tree.UniqueChildName(a, String("Wall")) == String("Wall"));
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Chemins
// ─────────────────────────────────────────────────────────────────────────

TEST(SceneNode, ResolvesAbsoluteAndRelativePaths) {
	Car car;
	EXPECT_TRUE(car.tree.Resolve(String("/Scene/Car/Wheels/FrontLeft")) == car.frontLeft);
	EXPECT_TRUE(car.tree.Resolve(String("/Car/Wheels")) == car.wheels); // racine implicite
	EXPECT_TRUE(car.tree.Resolve(String("Wheels/FrontRight"), car.car) == car.frontRight);
	EXPECT_TRUE(car.tree.Resolve(String("../FrontRight"), car.frontLeft) == car.frontRight);
	EXPECT_TRUE(car.tree.Resolve(String("../../Engine"), car.frontLeft) == car.engine);
	EXPECT_TRUE(car.tree.Resolve(String("."), car.engine) == car.engine);
	EXPECT_TRUE(!car.tree.Resolve(String("/Scene/Car/Nope")).Valid());
	// Remonter au-delà de la racine y reste, sans échouer.
	EXPECT_TRUE(car.tree.Resolve(String("../../../../../.."), car.mesh) == car.tree.Root());
}

TEST(SceneNode, PathOfIsTheInverseOfResolve) {
	Car car;
	for (NodeId id : car.tree.AllNodes()) {
		String path = car.tree.PathOf(id);
		EXPECT_TRUE(car.tree.Resolve(path) == id);
	}
	EXPECT_TRUE(car.tree.PathOf(car.frontLeft) == String("/Scene/Car/Wheels/FrontLeft"));
}

TEST(SceneNode, FindByNameSearchesTheWholeSubtree) {
	Car car;
	EXPECT_TRUE(car.tree.FindByName(String("Mesh")) == car.mesh);
	EXPECT_TRUE(car.tree.FindByName(String("Mesh"), car.wheels) == NodeId{});
	EXPECT_TRUE(car.tree.FindChild(car.car, String("Engine")) == car.engine);
	EXPECT_TRUE(!car.tree.FindChild(car.car, String("Mesh")).Valid()); // petit-enfant, pas enfant
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Transforms hiérarchiques
// ─────────────────────────────────────────────────────────────────────────

TEST(SceneTransform, ChildFollowsParentTranslation) {
	Car car;
	car.tree.SetLocalPosition(car.car, {10.f, 0.f, 5.f});
	car.tree.SetLocalPosition(car.frontLeft, {1.f, 0.f, 0.f});
	EXPECT_TRUE(NearVec(car.tree.GlobalPosition(car.frontLeft), {11.f, 0.f, 5.f}));
	// Le local, lui, n'a pas bougé.
	EXPECT_TRUE(NearVec(car.tree.LocalTransform(car.frontLeft).position, {1.f, 0.f, 0.f}));
}

TEST(SceneTransform, ChildFollowsParentRotation) {
	Car car;
	car.tree.SetLocalPosition(car.frontLeft, {1.f, 0.f, 0.f});
	// Un quart de tour autour de Y amène +X sur -Z (repère main droite).
	car.tree.SetLocalRotation(car.car,
							  math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, 1.57079633f));
	math::FVector3 world = car.tree.GlobalPosition(car.frontLeft);
	EXPECT_TRUE(Near(world.x, 0.f, 1e-3f));
	EXPECT_TRUE(Near(world.z, -1.f, 1e-3f));
}

TEST(SceneTransform, ChildFollowsParentScale) {
	Car car;
	car.tree.SetLocalPosition(car.frontLeft, {2.f, 0.f, 0.f});
	car.tree.SetLocalScale(car.car, {3.f, 3.f, 3.f});
	EXPECT_TRUE(NearVec(car.tree.GlobalPosition(car.frontLeft), {6.f, 0.f, 0.f}));
	EXPECT_TRUE(NearVec(car.tree.GlobalTransform(car.frontLeft).scale, {3.f, 3.f, 3.f}));
}

TEST(SceneTransform, NestedTransformsComposeThroughEveryLevel) {
	Car car;
	car.tree.SetLocalPosition(car.car, {10.f, 0.f, 0.f});
	car.tree.SetLocalPosition(car.wheels, {0.f, 1.f, 0.f});
	car.tree.SetLocalPosition(car.frontLeft, {0.f, 0.f, 2.f});
	EXPECT_TRUE(NearVec(car.tree.GlobalPosition(car.frontLeft), {10.f, 1.f, 2.f}));
	// Bouger un maillon intermédiaire déplace bien tout ce qui pend dessous.
	car.tree.SetLocalPosition(car.wheels, {0.f, 5.f, 0.f});
	EXPECT_TRUE(NearVec(car.tree.GlobalPosition(car.frontLeft), {10.f, 5.f, 2.f}));
}

TEST(SceneTransform, SetGlobalTransformComputesTheRightLocalOne) {
	Car car;
	car.tree.SetLocalPosition(car.car, {10.f, 0.f, 5.f});
	car.tree.SetLocalRotation(car.car, math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, 0.7f));
	car.tree.SetLocalScale(car.car, {2.f, 2.f, 2.f});

	Transform wanted;
	wanted.position = {4.f, 1.f, -3.f};
	wanted.rotation = math::FQuaternion::FromEuler(0.2f, 0.3f, -0.1f);
	wanted.scale = {1.f, 1.f, 1.f};
	EXPECT_TRUE(car.tree.SetGlobalTransform(car.frontLeft, wanted));

	Transform got = car.tree.GlobalTransform(car.frontLeft);
	EXPECT_TRUE(NearVec(got.position, wanted.position, 1e-3f));
	EXPECT_TRUE(NearVec(got.scale, wanted.scale, 1e-3f));
	// Quaternion : q et -q décrivent la même rotation, on compare donc la
	// valeur absolue de leur produit scalaire.
	const float dot = got.rotation.x * wanted.rotation.x + got.rotation.y * wanted.rotation.y +
					  got.rotation.z * wanted.rotation.z + got.rotation.w * wanted.rotation.w;
	EXPECT_TRUE(Near(sdl3::Abs(dot), 1.f, 1e-3f));
}

TEST(SceneTransform, DecomposeIsTheExactInverseOfCompose) {
	// math::DecomposeTRS a été ajouté pour ce module : on verrouille son
	// contrat ici, y compris le cas miroir (échelle négative).
	math::FVector3 t{3.f, -2.f, 7.f};
	math::FQuaternion r = math::FQuaternion::FromEuler(0.4f, -1.2f, 2.9f);
	for (math::FVector3 s : {math::FVector3{2.f, 0.5f, 1.5f}, math::FVector3{-2.f, 0.5f, 1.5f}}) {
		math::FMatrix4 m = math::ComposeTRS(t, r, s);
		math::TRS d = math::DecomposeTRS(m);
		math::FMatrix4 back = math::ComposeTRS(d.translation, d.rotation, d.scale);
		for (int i = 0; i < 16; ++i)
			EXPECT_TRUE(Near(m.m[i], back.m[i], 1e-4f));
		EXPECT_TRUE(!d.sheared);
	}
}

TEST(SceneTransform, VisibilityIsInherited) {
	Car car;
	EXPECT_TRUE(car.tree.IsVisibleInTree(car.mesh));
	EXPECT_TRUE(car.tree.SetVisible(car.body, false));
	EXPECT_TRUE(!car.tree.IsVisibleInTree(car.mesh));  // hérité
	EXPECT_TRUE(car.tree.Get(car.mesh)->visible);	   // mais sa visibilité PROPRE est intacte
	EXPECT_TRUE(car.tree.IsVisibleInTree(car.engine)); // branche voisine non affectée
}

// ─────────────────────────────────────────────────────────────────────────
// 4. Reparentage
// ─────────────────────────────────────────────────────────────────────────

TEST(SceneReparent, KeepLocalMovesTheNodeWithItsNewParent) {
	Car car;
	car.tree.SetLocalPosition(car.car, {10.f, 0.f, 0.f});
	car.tree.SetLocalPosition(car.engine, {0.f, 2.f, 0.f});
	NodeId shed = car.tree.Create(car.tree.Root(), String("Shed"));
	car.tree.SetLocalPosition(shed, {-20.f, 0.f, 0.f});

	EXPECT_TRUE(car.tree.Reparent(car.engine, shed, ReparentMode::KEEP_LOCAL));
	EXPECT_TRUE(car.tree.ParentOf(car.engine) == shed);
	EXPECT_TRUE(NearVec(car.tree.LocalTransform(car.engine).position, {0.f, 2.f, 0.f}));
	EXPECT_TRUE(NearVec(car.tree.GlobalPosition(car.engine), {-20.f, 2.f, 0.f}));
}

TEST(SceneReparent, KeepGlobalLeavesTheNodeWhereItIsOnScreen) {
	Car car;
	car.tree.SetLocalPosition(car.car, {10.f, 0.f, 0.f});
	car.tree.SetLocalRotation(car.car, math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, 0.9f));
	car.tree.SetLocalPosition(car.engine, {0.f, 2.f, 0.f});
	math::FVector3 before = car.tree.GlobalPosition(car.engine);

	NodeId shed = car.tree.Create(car.tree.Root(), String("Shed"));
	car.tree.SetLocalPosition(shed, {-20.f, 3.f, 4.f});
	car.tree.SetLocalScale(shed, {2.f, 2.f, 2.f});

	EXPECT_TRUE(car.tree.Reparent(car.engine, shed, ReparentMode::KEEP_GLOBAL));
	EXPECT_TRUE(car.tree.ParentOf(car.engine) == shed);
	EXPECT_TRUE(NearVec(car.tree.GlobalPosition(car.engine), before, 1e-3f));
	// Le local, lui, a forcément changé pour compenser.
	EXPECT_TRUE(!NearVec(car.tree.LocalTransform(car.engine).position, {0.f, 2.f, 0.f}));
}

TEST(SceneReparent, RefusesToCreateACycleWithoutTouchingTheTree) {
	Car car;
	// Mettre la voiture sous sa propre roue ferait un cycle.
	EXPECT_TRUE(!car.tree.Reparent(car.car, car.frontLeft));
	EXPECT_TRUE(!car.tree.Reparent(car.car, car.car));
	EXPECT_TRUE(car.tree.ParentOf(car.car) == car.tree.Root());
	EXPECT_TRUE(car.tree.Validate().Ok());
	// La racine ne se déplace pas non plus.
	EXPECT_TRUE(!car.tree.Reparent(car.tree.Root(), car.car));
}

TEST(SceneReparent, ChildOrderIsStableAndControllable) {
	Car car;
	// Insertion en tête plutôt qu'en queue.
	NodeId spoiler = car.tree.Create(car.car, String("Spoiler"), String(node_type::NODE3D), 0);
	EXPECT_TRUE(car.tree.ChildrenOf(car.car)[0] == spoiler);
	// Réordonnancement pur.
	EXPECT_TRUE(car.tree.MoveChild(spoiler, 2));
	EXPECT_TRUE(car.tree.ChildrenOf(car.car)[2] == spoiler);
	// Reparentage à un rang précis.
	EXPECT_TRUE(car.tree.Reparent(spoiler, car.wheels, ReparentMode::KEEP_LOCAL, 1));
	EXPECT_TRUE(car.tree.ChildrenOf(car.wheels)[1] == spoiler);
	EXPECT_TRUE(car.tree.Validate().Ok());
}

// ─────────────────────────────────────────────────────────────────────────
// 5. Suppression
// ─────────────────────────────────────────────────────────────────────────

TEST(SceneRemove, RemovingANodeRemovesItsWholeSubtree) {
	Car car;
	size_t before = car.tree.Size();
	EXPECT_TRUE(car.tree.Remove(car.wheels));
	EXPECT_EQ(car.tree.Size(), before - 3); // Wheels + 2 roues
	EXPECT_TRUE(car.tree.Get(car.frontLeft) == nullptr);
	EXPECT_TRUE(car.tree.Get(car.frontRight) == nullptr);
	EXPECT_EQ(car.tree.ChildrenOf(car.car).size(), size_t(3));
	EXPECT_TRUE(car.tree.Validate().Ok());
}

TEST(SceneRemove, RemoveChildrenKeepsTheNodeItself) {
	Car car;
	EXPECT_TRUE(car.tree.RemoveChildren(car.wheels));
	EXPECT_TRUE(car.tree.Get(car.wheels) != nullptr);
	EXPECT_TRUE(car.tree.ChildrenOf(car.wheels).empty());
	EXPECT_TRUE(car.tree.Validate().Ok());
}

TEST(SceneRemove, TheRootCannotBeRemoved) {
	Car car;
	EXPECT_TRUE(!car.tree.Remove(car.tree.Root()));
	EXPECT_TRUE(car.tree.Get(car.tree.Root()) != nullptr);
}

// ─────────────────────────────────────────────────────────────────────────
// 6. Composants et propriétés
// ─────────────────────────────────────────────────────────────────────────

TEST(SceneComponents, NodesCarryTypedComponentsAndFreeProperties) {
	NodeTree tree;
	NodeId car = tree.Create(tree.Root(), String("Car"));
	Node* node = tree.Get(car);

	Component mesh;
	mesh.type = String("MeshInstance");
	mesh.props.Set(String("resource"), PropertyValue::Resource(String("car_body.gltf")));
	node->SetComponent(std::move(mesh));

	node->Set(String("max_speed"), PropertyValue::Float(180.0));
	node->Set(String("team"), PropertyValue::Str(String("blue")));

	EXPECT_TRUE(node->HasComponent(String("MeshInstance")));
	EXPECT_TRUE(node->ComponentProperty(String("MeshInstance"), String("resource"))->AsString() ==
				String("car_body.gltf"));
	EXPECT_TRUE(Near(node->Get(String("max_speed"))->AsFloat(), 180.f));
	EXPECT_TRUE(node->Get(String("team"))->AsString() == String("blue"));

	// Remplacement par type (un composant par type et par nœud).
	Component other;
	other.type = String("MeshInstance");
	other.props.Set(String("resource"), PropertyValue::Resource(String("truck.gltf")));
	node->SetComponent(std::move(other));
	EXPECT_EQ(node->components.size(), size_t(1));
	EXPECT_TRUE(node->RemoveComponent(String("MeshInstance")));
	EXPECT_TRUE(!node->HasComponent(String("MeshInstance")));
}

// ─────────────────────────────────────────────────────────────────────────
// 7. Duplication
// ─────────────────────────────────────────────────────────────────────────

TEST(SceneDuplicate, CopiesTheWholeSubtreeWithFreshIds) {
	Car car;
	NodeId copy = car.tree.Duplicate(car.car);
	ASSERT_TRUE(copy.Valid());
	EXPECT_TRUE(copy != car.car);
	EXPECT_EQ(car.tree.Size(), size_t(9 + 8));
	EXPECT_EQ(car.tree.Descendants(copy).size(), car.tree.Descendants(car.car).size());
	// Même forme, mêmes noms, mêmes rangs.
	NodeId copiedWheel = car.tree.Resolve(String("Wheels/FrontLeft"), copy);
	ASSERT_TRUE(copiedWheel.Valid());
	EXPECT_TRUE(copiedWheel != car.frontLeft);
	EXPECT_TRUE(car.tree.Validate().Ok());
}

TEST(SceneDuplicate, InternalReferencesFollowTheCopyAndExternalOnesDoNot) {
	Car car;
	NodeId track = car.tree.Create(car.tree.Root(), String("Track"));
	// La caméra vise le moteur (référence INTERNE au sous-arbre copié)…
	car.tree.Get(car.camera)->Set(String("target"), PropertyValue::NodeRef(car.engine));
	// …et le moteur vise le circuit (référence EXTERNE).
	car.tree.Get(car.engine)->Set(String("track"), PropertyValue::NodeRef(track));

	NodeId copy = car.tree.Duplicate(car.car);
	ASSERT_TRUE(copy.Valid());
	NodeId copiedCamera = car.tree.Resolve(String("Camera"), copy);
	NodeId copiedEngine = car.tree.Resolve(String("Engine"), copy);
	ASSERT_TRUE(copiedCamera.Valid() && copiedEngine.Valid());

	EXPECT_TRUE(car.tree.Get(copiedCamera)->Get(String("target"))->AsNodeRef() == copiedEngine);
	EXPECT_TRUE(car.tree.Get(copiedCamera)->Get(String("target"))->AsNodeRef() != car.engine);
	EXPECT_TRUE(car.tree.Get(copiedEngine)->Get(String("track"))->AsNodeRef() == track);
	EXPECT_TRUE(car.tree.Validate().Ok());
}

TEST(SceneDuplicate, RemapsReferencesNestedInsideArraysAndComponents) {
	Car car;
	Component script;
	script.type = String("Script");
	script.props.Set(String("watch"),
					 PropertyValue::Array({PropertyValue::NodeRef(car.frontLeft),
										   PropertyValue::NodeRef(car.frontRight)}));
	car.tree.Get(car.engine)->SetComponent(std::move(script));

	NodeId copy = car.tree.Duplicate(car.car);
	NodeId copiedEngine = car.tree.Resolve(String("Engine"), copy);
	ASSERT_TRUE(copiedEngine.Valid());
	const PropertyValue* watch =
		car.tree.Get(copiedEngine)->ComponentProperty(String("Script"), String("watch"));
	ASSERT_TRUE(watch != nullptr);
	ASSERT_EQ(watch->AsArray().size(), size_t(2));
	EXPECT_TRUE(watch->AsArray()[0].AsNodeRef() != car.frontLeft);
	EXPECT_TRUE(watch->AsArray()[0].AsNodeRef() ==
				car.tree.Resolve(String("Wheels/FrontLeft"), copy));
	EXPECT_TRUE(watch->AsArray()[1].AsNodeRef() ==
				car.tree.Resolve(String("Wheels/FrontRight"), copy));
}

// ─────────────────────────────────────────────────────────────────────────
// 8. Sérialisation
// ─────────────────────────────────────────────────────────────────────────

TEST(SceneSerialize, SaveThenLoadRebuildsAnIdenticalTree) {
	Car car;
	car.tree.SetLocalPosition(car.car, {1.5f, -2.f, 3.25f});
	car.tree.SetLocalScale(car.frontLeft, {0.5f, 0.5f, 0.5f});
	car.tree.Get(car.car)->Set(String("max_speed"), PropertyValue::Float(180.0));
	car.tree.Get(car.camera)->Set(String("target"), PropertyValue::NodeRef(car.engine));
	Component mesh;
	mesh.type = String("MeshInstance");
	mesh.props.Set(String("resource"), PropertyValue::Resource(String("car_body.gltf")));
	car.tree.Get(car.body)->SetComponent(std::move(mesh));

	String json = car.tree.EncodeJson();
	auto reloaded = NodeTree::DecodeJson(json);
	ASSERT_TRUE(reloaded.IsOk());
	const NodeTree& back = reloaded.Value();

	EXPECT_TRUE(back == car.tree);
	EXPECT_EQ(back.Size(), car.tree.Size());
	// Les identifiants sont conservés : une référence notée AILLEURS reste
	// valable après rechargement.
	EXPECT_TRUE(back.Get(car.engine) != nullptr);
	EXPECT_TRUE(back.Get(car.camera)->Get(String("target"))->AsNodeRef() == car.engine);
	EXPECT_TRUE(back.PathOf(car.frontLeft) == String("/Scene/Car/Wheels/FrontLeft"));
	// Et le second aller-retour est stable octet pour octet.
	EXPECT_TRUE(back.EncodeJson() == json);
}

TEST(SceneSerialize, RejectsAFutureFormatAndAMalformedDocument) {
	auto tooNew =
		NodeTree::DecodeJson(String("{\"format\":\"scene.tree\",\"version\":99,\"root\":{}}"));
	EXPECT_TRUE(tooNew.IsError());
	auto noRoot = NodeTree::DecodeJson(String("{\"format\":\"scene.tree\",\"version\":1}"));
	EXPECT_TRUE(noRoot.IsError());
	auto notJson = NodeTree::DecodeJson(String("pas du json"));
	EXPECT_TRUE(notJson.IsError());
}

TEST(SceneSerialize, RenumbersInsteadOfFailingWhenAFileHasAbsurdIds) {
	// Fichier écrit à la main : identifiants dupliqués. L'arbre doit se
	// charger quand même, en renumérotant.
	String json = String("{\"format\":\"scene.tree\",\"version\":1,\"root\":{\"id\":\"0:0\","
						 "\"name\":\"Scene\",\"children\":["
						 "{\"id\":\"3:0\",\"name\":\"A\"},{\"id\":\"3:0\",\"name\":\"B\"}]}}");
	auto tree = NodeTree::DecodeJson(json);
	ASSERT_TRUE(tree.IsOk());
	EXPECT_EQ(tree.Value().Size(), size_t(3));
	EXPECT_TRUE(tree.Value().Validate().Ok());
	EXPECT_TRUE(tree.Value().Resolve(String("/Scene/B")).Valid());
}

// ─────────────────────────────────────────────────────────────────────────
// 9. Validation
// ─────────────────────────────────────────────────────────────────────────

TEST(SceneValidate, AcceptsAHealthyTree) {
	Car car;
	ValidationReport report = car.tree.Validate();
	EXPECT_TRUE(report.Ok());
	EXPECT_EQ(report.ErrorCount(), size_t(0));
}

TEST(SceneValidate, ReportsADanglingNodeReference) {
	Car car;
	car.tree.Get(car.camera)->Set(String("target"), PropertyValue::NodeRef(car.engine));
	EXPECT_TRUE(car.tree.Remove(car.engine));
	ValidationReport report = car.tree.Validate();
	EXPECT_TRUE(!report.Ok());
	EXPECT_TRUE(report.Format().Contains("référence vers un nœud disparu"));
}

TEST(SceneValidate, ReportsAMissingResourceAndAnUnknownType) {
	NodeTree tree;
	NodeId id = tree.Create(tree.Root(), String("Body"), String("MeshInstance"));
	Component mesh;
	mesh.type = String("MeshInstance");
	mesh.props.Set(String("resource"), PropertyValue::Resource(String("absent.gltf")));
	tree.Get(id)->SetComponent(std::move(mesh));

	auto exists = [](const String& path) { return path == String("present.gltf"); };
	auto known = [](const String& type) {
		return type == String("Node") || type == String("Node3D");
	};
	ValidationReport report = tree.Validate(exists, known);
	EXPECT_TRUE(!report.Ok());
	EXPECT_TRUE(report.Format().Contains("ressource introuvable"));
	EXPECT_TRUE(report.Format().Contains("type de nœud inconnu"));
	EXPECT_TRUE(report.WarningCount() >= size_t(1));
}

TEST(SceneValidate, ReportsSiblingsSharingANameAndAZeroScale) {
	NodeTree tree;
	tree.Create(tree.Root(), String("Wall"));
	tree.Create(tree.Root(), String("Wall"));
	NodeId flat = tree.Create(tree.Root(), String("Flat"));
	tree.SetLocalScale(flat, {1.f, 0.f, 1.f});
	ValidationReport report = tree.Validate();
	EXPECT_TRUE(report.Ok()); // ce sont des AVERTISSEMENTS, pas des erreurs
	EXPECT_TRUE(report.Format().Contains("partagé avec un frère"));
	EXPECT_TRUE(report.Format().Contains("échelle nulle"));
}

TEST(SceneValidate, ReportsANonFiniteTransform) {
	NodeTree tree;
	NodeId id = tree.Create(tree.Root(), String("Broken"));
	tree.Get(id)->transform.position.x = 0.f / 0.f; // NaN
	ValidationReport report = tree.Validate();
	EXPECT_TRUE(!report.Ok());
	EXPECT_TRUE(report.Format().Contains("transform non fini"));
}

// ─────────────────────────────────────────────────────────────────────────
// 10. Types de nœuds (registre extensible)
// ─────────────────────────────────────────────────────────────────────────

TEST(SceneTypes, RegistryKnowsBuiltinsAndAcceptsNewTypes) {
	NodeTypeRegistry registry = NodeTypeRegistry::WithBuiltins();
	EXPECT_TRUE(registry.Knows(String(node_type::NODE)));
	EXPECT_TRUE(registry.Knows(String(node_type::NODE3D)));
	EXPECT_TRUE(!registry.Knows(String("MeshInstance")));

	NodeTypeInfo mesh;
	mesh.name = String("MeshInstance");
	mesh.label = String("Maillage");
	mesh.category = String("3D");
	mesh.icon = String("▣");
	Component component;
	component.type = String("Mesh");
	component.props.Set(String("shape"), PropertyValue::Str(String("box")));
	mesh.defaultComponents.push_back(std::move(component));
	mesh.defaultProperties.Set(String("cast_shadows"), PropertyValue::Bool(true));
	registry.Register(std::move(mesh));

	EXPECT_TRUE(registry.Knows(String("MeshInstance")));
	EXPECT_TRUE(registry.LabelOf(String("MeshInstance")) == String("Maillage"));
	// Un type inconnu reste lisible : son nom technique sert de libellé.
	EXPECT_TRUE(registry.LabelOf(String("Inconnu")) == String("Inconnu"));

	Node node = registry.Make(String("MeshInstance"), String("Body"));
	EXPECT_TRUE(node.HasComponent(String("Mesh")));
	EXPECT_TRUE(node.ComponentProperty(String("Mesh"), String("shape"))->AsString() ==
				String("box"));
	EXPECT_TRUE(node.Get(String("cast_shadows"))->AsBool());
}

TEST(SceneTypes, CreateThroughTheRegistryKeepsSiblingNamesUnique) {
	NodeTypeRegistry registry = NodeTypeRegistry::WithBuiltins();
	NodeTree tree;
	NodeId first = registry.Create(tree, tree.Root(), String(node_type::NODE3D), String("Cube"));
	NodeId second = registry.Create(tree, tree.Root(), String(node_type::NODE3D), String("Cube"));
	EXPECT_TRUE(tree.Get(first)->name == String("Cube"));
	EXPECT_TRUE(tree.Get(second)->name == String("Cube 2"));
	EXPECT_TRUE(tree.Validate({}, registry.TypeChecker()).Ok());
}

TEST(SceneTypes, AnUnknownTypeSurvivesASaveLoadRoundTrip) {
	// Un fichier écrit par une version plus récente (ou par un greffon
	// absent) ne doit RIEN perdre : le type est conservé tel quel.
	NodeTree tree;
	NodeId id = tree.Create(tree.Root(), String("Futur"), String("HologramEmitter"));
	tree.Get(id)->Set(String("beam"), PropertyValue::Float(2.5));
	auto back = NodeTree::DecodeJson(tree.EncodeJson());
	ASSERT_TRUE(back.IsOk());
	NodeId reloaded = back.Value().Resolve(String("/Scene/Futur"));
	ASSERT_TRUE(reloaded.Valid());
	EXPECT_TRUE(back.Value().Get(reloaded)->type == String("HologramEmitter"));
	EXPECT_TRUE(Near(back.Value().Get(reloaded)->Get(String("beam"))->AsFloat(), 2.5f));
}

// ─────────────────────────────────────────────────────────────────────────
// 11. Scènes réutilisables (PackedScene)
// ─────────────────────────────────────────────────────────────────────────

TEST(ScenePacked, PacksASubtreeAndInstantiatesItSeveralTimes) {
	Car car;
	car.tree.SetLocalPosition(car.car, {5.f, 0.f, 0.f});
	PackedScene packed = PackedScene::FromSubtree(car.tree, car.car);
	packed.SetSource(String("content/Car.scene"));
	EXPECT_EQ(packed.Tree().Size(), size_t(8)); // la voiture et ses 7 descendants

	NodeTree track(String("RaceTrack"));
	NodeId first = packed.InstantiateInto(track, track.Root(), String("Car01"));
	NodeId second = packed.InstantiateInto(track, track.Root(), String("Car02"));
	NodeId third = packed.InstantiateInto(track, track.Root()); // nom repris de la scène
	ASSERT_TRUE(first.Valid() && second.Valid() && third.Valid());
	EXPECT_TRUE(track.Get(first)->name == String("Car01"));
	EXPECT_TRUE(track.Get(third)->name == String("Car"));
	EXPECT_EQ(track.Size(), size_t(1 + 3 * 8));
	EXPECT_TRUE(track.Validate().Ok());

	// Chaque instance est indépendante : bouger l'une ne bouge pas l'autre.
	track.SetLocalPosition(first, {100.f, 0.f, 0.f});
	EXPECT_TRUE(NearVec(track.GlobalPosition(track.Resolve(String("Wheels/FrontLeft"), first)),
						{100.f, 0.f, 0.f}));
	EXPECT_TRUE(NearVec(track.GlobalPosition(track.Resolve(String("Wheels/FrontLeft"), second)),
						{5.f, 0.f, 0.f}));
}

TEST(ScenePacked, InstancesRememberWhereTheyComeFrom) {
	Car car;
	PackedScene packed = PackedScene::FromSubtree(car.tree, car.car);
	packed.SetSource(String("content/Car.scene"));
	NodeTree track(String("RaceTrack"));
	NodeId instance = packed.InstantiateInto(track, track.Root(), String("Car01"));
	EXPECT_TRUE(PackedScene::IsInstanceRoot(*track.Get(instance)));
	EXPECT_TRUE(PackedScene::InstanceSource(*track.Get(instance)) == String("content/Car.scene"));
	// Les nœuds INTERNES d'une instance n'en sont pas la racine.
	EXPECT_TRUE(
		!PackedScene::IsInstanceRoot(*track.Get(track.Resolve(String("Wheels"), instance))));
}

TEST(ScenePacked, InternalReferencesFollowEachInstance) {
	Car car;
	car.tree.Get(car.camera)->Set(String("target"), PropertyValue::NodeRef(car.frontLeft));
	PackedScene packed = PackedScene::FromSubtree(car.tree, car.car);

	NodeTree track(String("RaceTrack"));
	NodeId first = packed.InstantiateInto(track, track.Root(), String("Car01"));
	NodeId second = packed.InstantiateInto(track, track.Root(), String("Car02"));

	NodeId cameraA = track.Resolve(String("Camera"), first);
	NodeId wheelA = track.Resolve(String("Wheels/FrontLeft"), first);
	NodeId cameraB = track.Resolve(String("Camera"), second);
	NodeId wheelB = track.Resolve(String("Wheels/FrontLeft"), second);
	EXPECT_TRUE(track.Get(cameraA)->Get(String("target"))->AsNodeRef() == wheelA);
	EXPECT_TRUE(track.Get(cameraB)->Get(String("target"))->AsNodeRef() == wheelB);
	EXPECT_TRUE(wheelA != wheelB);
	EXPECT_TRUE(track.Validate().Ok());
}

TEST(ScenePacked, OutgoingReferencesAreNeutralisedRatherThanLeftDangling) {
	Car car;
	NodeId track = car.tree.Create(car.tree.Root(), String("Track"));
	// Référence SORTANTE : le moteur vise un nœud hors du sous-arbre emballé.
	car.tree.Get(car.engine)->Set(String("track"), PropertyValue::NodeRef(track));
	PackedScene packed = PackedScene::FromSubtree(car.tree, car.car);

	NodeTree target(String("Level"));
	NodeId instance = packed.InstantiateInto(target, target.Root());
	NodeId engine = target.Resolve(String("Engine"), instance);
	ASSERT_TRUE(engine.Valid());
	EXPECT_TRUE(!target.Get(engine)->Get(String("track"))->AsNodeRef().Valid());
	// Et surtout : l'arbre reste valide (pas de référence pendouillante).
	EXPECT_TRUE(target.Validate().Ok());
}

TEST(ScenePacked, SurvivesASaveLoadRoundTrip) {
	Car car;
	PackedScene packed = PackedScene::FromSubtree(car.tree, car.car);
	packed.SetSource(String("content/Car.scene"));
	auto back = PackedScene::DecodeJson(packed.EncodeJson());
	ASSERT_TRUE(back.IsOk());
	EXPECT_TRUE(back.Value().Tree() == packed.Tree());
	EXPECT_TRUE(back.Value().Source() == String("content/Car.scene"));
}

// ─────────────────────────────────────────────────────────────────────────
// 12. Charge — le système doit tenir des scènes réelles
// ─────────────────────────────────────────────────────────────────────────

TEST(ScenePerformance, ScalesToAHundredThousandNodes) {
	using Clock = std::chrono::steady_clock;
	auto ms = [](Clock::time_point t0) {
		return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
	};

	for (int count : {1000, 10000, 100000}) {
		NodeTree tree;
		auto t0 = Clock::now();
		// Arbre réaliste : des grappes de 10 enfants sous des groupes.
		NodeId group = tree.Create(tree.Root(), String("Group"));
		std::vector<NodeId> leaves;
		leaves.reserve(size_t(count));
		for (int i = 0; i < count; ++i) {
			if (i % 10 == 0)
				group = tree.Create(tree.Root(), String::Format("Group %d", i / 10));
			leaves.push_back(tree.Create(group, String::Format("Node %d", i)));
		}
		const double buildMs = ms(t0);

		t0 = Clock::now();
		math::FVector3 sum;
		for (NodeId id : leaves)
			sum = sum + tree.GlobalPosition(id);
		const double firstReadMs = ms(t0);

		// Deuxième lecture : tout est en cache, elle doit être bien plus
		// rapide que la première — c'est ce que le cache promet.
		t0 = Clock::now();
		for (NodeId id : leaves)
			sum = sum + tree.GlobalPosition(id);
		const double cachedReadMs = ms(t0);

		// Déplacer UNE racine ne doit pas coûter le sous-arbre (invalidation
		// en O(1), recalcul seulement à la lecture).
		t0 = Clock::now();
		for (int i = 0; i < 100; ++i)
			tree.SetLocalPosition(tree.ChildrenOf(tree.Root())[0], {float(i), 0.f, 0.f});
		const double moveMs = ms(t0);

		t0 = Clock::now();
		String json = tree.EncodeJson();
		const double saveMs = ms(t0);
		t0 = Clock::now();
		auto reloaded = NodeTree::DecodeJson(json);
		const double loadMs = ms(t0);
		ASSERT_TRUE(reloaded.IsOk());
		EXPECT_EQ(reloaded.Value().Size(), tree.Size());

		t0 = Clock::now();
		NodeId found = tree.Resolve(tree.PathOf(leaves.back()));
		const double findMs = ms(t0);
		EXPECT_TRUE(found == leaves.back());

		t0 = Clock::now();
		ValidationReport report = tree.Validate();
		const double validateMs = ms(t0);
		EXPECT_TRUE(report.Ok());

		t0 = Clock::now();
		{
			NodeTree copy = tree; // copie complète (instantané d'annulation)
			EXPECT_EQ(copy.Size(), tree.Size());
		}
		const double copyMs = ms(t0);

		std::printf("%7d nœuds : création %8.2f ms · lecture %7.2f ms · relecture cache %7.2f ms · "
					"100 déplacements %6.3f ms · copie %7.2f ms · sauvegarde %8.2f ms · chargement "
					"%8.2f ms · "
					"chemin %6.3f ms · validation %7.2f ms\n",
					count, buildMs, firstReadMs, cachedReadMs, moveMs, copyMs, saveMs, loadMs,
					findMs, validateMs);
		std::fflush(stdout);

		EXPECT_TRUE(cachedReadMs <= firstReadMs);
		EXPECT_TRUE(moveMs < firstReadMs); // l'écriture ne parcourt pas le sous-arbre
	}
}

int main() {
	return RUN_ALL_TESTS();
}
