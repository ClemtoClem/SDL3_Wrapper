// Tests unitaires — dataio::gltf (lecture de documents glTF 2.0).
//
// Exercés sur les VRAIS modèles du dépôt (assets/models/animals/*.gltf +
// leurs .bin), pas sur des documents fabriqués : c'est la seule façon de
// vérifier le décodage des accesseurs contre des données produites par un
// exportateur réel.
#include "test.hpp"
#include <dataio/documents/gltf.hpp>

#include <cmath>

using namespace dataio;

namespace {

constexpr const char *COW = "assets/models/animals/Cow.gltf";

} // namespace

TEST(Gltf, ParsesStructureWithoutTouchingTheBuffers) {
	// `loadBuffers = false` : inspecter un document sans lire ses mégaoctets
	// de géométrie, ce dont un navigateur de ressources a besoin.
	auto loaded = gltf::LoadFile(String(COW), false);
	if (loaded.IsError())
		test::ReportFailure(__FILE__, __LINE__, loaded.Error().CStr());
	ASSERT_TRUE(loaded.IsOk());

	const gltf::Document &document = loaded.Value();
	EXPECT_TRUE(document.version.StartsWith("2."));
	EXPECT_TRUE(document.meshes.size() >= 1);
	EXPECT_TRUE(document.accessors.size() > 0);
	EXPECT_TRUE(document.bufferViews.size() > 0);
	ASSERT_TRUE(document.buffers.size() == 1);
	EXPECT_EQ(document.buffers[0].uri, "Cow.bin");
	EXPECT_TRUE(document.buffers[0].data.empty()); // pas chargé, comme demandé
	EXPECT_TRUE(document.nodes.size() > 1);
	EXPECT_TRUE(document.materials.size() >= 1);

	// L'URI d'un tampon se résout par rapport au dossier du document.
	EXPECT_EQ(document.ResolveUri(String("Cow.bin")), "assets/models/animals/Cow.bin");
}

TEST(Gltf, DecodesPositionsAndIndicesFromTheBinaryBuffer) {
	auto loaded = gltf::LoadFile(String(COW));
	if (loaded.IsError())
		test::ReportFailure(__FILE__, __LINE__, loaded.Error().CStr());
	ASSERT_TRUE(loaded.IsOk());
	const gltf::Document &document = loaded.Value();
	ASSERT_TRUE(!document.buffers.empty() && !document.buffers[0].data.empty());

	ASSERT_TRUE(!document.meshes.empty() && !document.meshes[0].primitives.empty());
	const gltf::Primitive &primitive = document.meshes[0].primitives[0];

	int positionAccessor = primitive.Find("POSITION");
	ASSERT_TRUE(positionAccessor >= 0);
	auto positions = document.ReadFloats(positionAccessor);
	if (positions.IsError())
		test::ReportFailure(__FILE__, __LINE__, positions.Error().CStr());
	ASSERT_TRUE(positions.IsOk());

	// 3 flottants par sommet, et autant de sommets que l'accesseur l'annonce.
	const gltf::Accessor &accessor = document.accessors[size_t(positionAccessor)];
	EXPECT_TRUE(positions.Value().size() == accessor.count * 3);
	EXPECT_TRUE(accessor.count > 100);

	// Les coordonnées sont finies et dans un ordre de grandeur plausible pour
	// un modèle exporté (ni NaN, ni valeurs astronomiques : les deux
	// symptômes classiques d'un décalage d'offset ou d'un mauvais type).
	float maxAbs = 0.f;
	for (float value : positions.Value()) {
		EXPECT_TRUE(std::isfinite(value));
		float magnitude = value < 0.f ? -value : value;
		if (magnitude > maxAbs)
			maxAbs = magnitude;
	}
	EXPECT_TRUE(maxAbs > 0.01f);
	EXPECT_TRUE(maxAbs < 1000.f);

	// Indices : tous dans les bornes du tableau de sommets — la vérification
	// qui attrape un décodage d'indices erroné.
	ASSERT_TRUE(primitive.indices >= 0);
	auto indices = document.ReadIndices(primitive.indices);
	ASSERT_TRUE(indices.IsOk());
	EXPECT_TRUE(indices.Value().size() % 3 == 0); // TRIANGLES
	for (uint32_t index : indices.Value())
		EXPECT_TRUE(index < accessor.count);
}

TEST(Gltf, NormalsAreUnitLength) {
	// Contrôle sémantique fort : si le décodage se trompait de type de
	// composante ou d'offset, les normales cesseraient d'être unitaires.
	auto loaded = gltf::LoadFile(String(COW));
	ASSERT_TRUE(loaded.IsOk());
	const gltf::Document &document = loaded.Value();
	const gltf::Primitive &primitive = document.meshes[0].primitives[0];

	int normalAccessor = primitive.Find("NORMAL");
	ASSERT_TRUE(normalAccessor >= 0);
	auto normals = document.ReadFloats(normalAccessor);
	ASSERT_TRUE(normals.IsOk());
	ASSERT_TRUE(normals.Value().size() >= 3);

	size_t checked = 0;
	for (size_t i = 0; i + 2 < normals.Value().size() && checked < 200; i += 3, ++checked) {
		float x = normals.Value()[i], y = normals.Value()[i + 1], z = normals.Value()[i + 2];
		float length = std::sqrt(x * x + y * y + z * z);
		EXPECT_TRUE(std::fabs(length - 1.f) < 0.02f);
	}
	EXPECT_TRUE(checked > 0);
}

TEST(Gltf, ReadsPbrMaterialFactors) {
	auto loaded = gltf::LoadFile(String(COW), false);
	ASSERT_TRUE(loaded.IsOk());
	ASSERT_TRUE(!loaded.Value().materials.empty());

	const gltf::Material &material = loaded.Value().materials[0];
	EXPECT_FALSE(material.name.IsEmpty());
	for (int i = 0; i < 4; ++i) {
		EXPECT_TRUE(material.baseColor[i] >= 0.f);
		EXPECT_TRUE(material.baseColor[i] <= 1.f);
	}
	EXPECT_TRUE(material.metallic >= 0.f && material.metallic <= 1.f);
	EXPECT_TRUE(material.roughness >= 0.f && material.roughness <= 1.f);
}

TEST(Gltf, NodeHierarchyIsConsistent) {
	auto loaded = gltf::LoadFile(String(COW), false);
	ASSERT_TRUE(loaded.IsOk());
	const gltf::Document &document = loaded.Value();

	for (const gltf::Node &node : document.nodes) {
		for (int child : node.children) {
			EXPECT_TRUE(child >= 0);
			EXPECT_TRUE(size_t(child) < document.nodes.size());
		}
		if (node.mesh >= 0)
			EXPECT_TRUE(size_t(node.mesh) < document.meshes.size());
		// Un quaternion exporté est unitaire.
		float length = std::sqrt(node.rotation[0] * node.rotation[0] + node.rotation[1] * node.rotation[1] +
								 node.rotation[2] * node.rotation[2] + node.rotation[3] * node.rotation[3]);
		EXPECT_TRUE(std::fabs(length - 1.f) < 0.01f);
	}
}

TEST(Gltf, RejectsBadDocumentsWithAClearMessage) {
	// Fichier absent.
	auto missing = gltf::LoadFile(String("assets/models/inexistant.gltf"));
	ASSERT_TRUE(missing.IsError());
	EXPECT_TRUE(missing.Error().Contains("glTF"));

	// JSON valide mais pas du glTF : c'est `asset.version` qui tranche.
	JsonDocument json;
	ASSERT_TRUE(json.DecodeStr(String("{\"meshes\": []}")).IsNone());
	auto notGltf = gltf::ParseJson(json.GetRoot());
	ASSERT_TRUE(notGltf.IsError());
	EXPECT_TRUE(notGltf.Error().Contains("asset.version"));

	// Version majeure non gérée.
	JsonDocument v1;
	ASSERT_TRUE(v1.DecodeStr(String("{\"asset\": {\"version\": \"1.0\"}}")).IsNone());
	auto legacy = gltf::ParseJson(v1.GetRoot());
	ASSERT_TRUE(legacy.IsError());
	EXPECT_TRUE(legacy.Error().Contains("non gérée"));
}

TEST(Gltf, OutOfRangeAccessorIsAnErrorNotAnOverread) {
	JsonDocument json;
	// Un accesseur qui annonce plus d'éléments que le tampon n'en contient :
	// doit être REFUSÉ, pas lu hors bornes.
	const char *source = "{\"asset\": {\"version\": \"2.0\"},"
						 " \"buffers\": [{\"byteLength\": 12}],"
						 " \"bufferViews\": [{\"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 12}],"
						 " \"accessors\": [{\"bufferView\": 0, \"componentType\": 5126, \"count\": 100,"
						 "                  \"type\": \"VEC3\"}]}";
	ASSERT_TRUE(json.DecodeStr(String(source)).IsNone());
	auto parsed = gltf::ParseJson(json.GetRoot());
	ASSERT_TRUE(parsed.IsOk());

	gltf::Document document = std::move(parsed).Unwrap();
	// Tampon non chargé : erreur explicite plutôt que lecture d'un vide.
	auto unloaded = document.ReadFloats(0);
	ASSERT_TRUE(unloaded.IsError());
	EXPECT_TRUE(unloaded.Error().Contains("non chargé"));

	// Tampon présent mais trop court pour ce que l'accesseur annonce.
	document.buffers[0].data.assign(12, 0);
	auto overrun = document.ReadFloats(0);
	ASSERT_TRUE(overrun.IsError());
	EXPECT_TRUE(overrun.Error().Contains("hors du tampon"));

	// Accesseur inexistant.
	EXPECT_TRUE(document.ReadFloats(42).IsError());
	EXPECT_TRUE(document.ReadFloats(-1).IsError());
}

// ── Écriture (gltf_writer.hpp) ───────────────────────────────────────────────

#include <dataio/documents/gltf_writer.hpp>
#include <dataio/documents/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

TEST(GltfWriter, SceneRoundTripsThroughTheReader) {
	gltf::SceneDescription scene;
	scene.generator = "test";
	gltf::MeshDescription tri;
	tri.name = "triangle";
	tri.positions = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 2.f, 0.f};
	tri.normals = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
	tri.uvs = {0.f, 1.f, 1.f, 1.f, 0.f, 0.f};
	tri.indices = {0, 1, 2};
	tri.material = 0;
	scene.meshes.push_back(tri);
	gltf::MaterialDescription skin;
	skin.name = "skin";
	skin.baseColorUri = "skin.png";
	scene.materials.push_back(skin);
	gltf::NodeDescription body{"body", 0, -1, {0.f, 1.f, 0.f}};
	gltf::NodeDescription head{"head", 0, 0, {0.f, 0.5f, 0.f}, {0.f, 0.7071068f, 0.f, 0.7071068f}};
	scene.nodes = {body, head};
	gltf::AnimationDescription nod{"nod", {}};
	nod.channels.push_back({1, "rotation", {0.f, 1.f}, {0.f, 0.f, 0.f, 1.f, 0.f, 0.7071068f, 0.f, 0.7071068f}, "LINEAR"});
	nod.channels.push_back({0, "translation", {0.f, 0.5f, 1.f}, {0.f, 1.f, 0.f, 0.f, 1.2f, 0.f, 0.f, 1.f, 0.f}, "STEP"});
	scene.animations.push_back(nod);

	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "dataio_gltf_writer";
	std::filesystem::remove_all(dir);
	const String path((dir / "scene.gltf").string().c_str());
	Option<String> error = gltf::SaveScene(path, scene);
	if (error.IsSome())
		test::ReportFailure(__FILE__, __LINE__, error.Value().CStr());
	ASSERT_TRUE(error.IsNone());
	EXPECT_TRUE(std::filesystem::exists(dir / "scene.bin"));

	auto loaded = gltf::LoadFile(path, true);
	if (loaded.IsError())
		test::ReportFailure(__FILE__, __LINE__, loaded.Error().CStr());
	ASSERT_TRUE(loaded.IsOk());
	const gltf::Document &doc = loaded.Value();
	EXPECT_EQ(doc.generator, "test");
	ASSERT_TRUE(doc.meshes.size() == 1);
	ASSERT_TRUE(doc.nodes.size() == 2);
	EXPECT_EQ(doc.nodes[1].name, "head");
	ASSERT_TRUE(doc.nodes[0].children.size() == 1);
	EXPECT_EQ(doc.nodes[0].children[0], 1);
	EXPECT_TRUE(std::fabs(doc.nodes[1].rotation[1] - 0.7071068f) < 1e-6f);
	const gltf::Primitive &p = doc.meshes[0].primitives[0];
	auto positions = doc.ReadFloats(p.Find("POSITION"));
	ASSERT_TRUE(positions.IsOk());
	EXPECT_TRUE(positions.Value() == tri.positions);
	auto uvs = doc.ReadFloats(p.Find("TEXCOORD_0"));
	ASSERT_TRUE(uvs.IsOk());
	EXPECT_TRUE(uvs.Value() == tri.uvs);
	auto indices = doc.ReadIndices(p.indices);
	ASSERT_TRUE(indices.IsOk());
	EXPECT_TRUE(indices.Value() == tri.indices);
	ASSERT_TRUE(doc.materials.size() == 1);
	ASSERT_TRUE(doc.images.size() == 1);
	EXPECT_EQ(doc.images[0].uri, "skin.png");

	// Animations (non lues par le lecteur) : contrôlées dans le JSON.
	std::ifstream in((dir / "scene.gltf").string());
	std::stringstream text;
	text << in.rdbuf();
	JsonDocument json;
	ASSERT_TRUE(json.DecodeStr(String(text.str().c_str())).IsNone());
	NodePtr animations = json.GetRoot()->Get("animations");
	ASSERT_TRUE(animations && animations->GetSize() == 1);
	NodePtr samplers = animations->At(0)->Get("samplers");
	ASSERT_TRUE(samplers && samplers->GetSize() == 2);
	EXPECT_EQ(samplers->At(1)->Get("interpolation")->stringValue, "STEP");
	auto times = doc.ReadFloats(int(samplers->At(1)->Get("input")->intValue));
	ASSERT_TRUE(times.IsOk());
	EXPECT_TRUE(times.Value() == (std::vector<float>{0.f, 0.5f, 1.f}));
	std::filesystem::remove_all(dir);
}

TEST(GltfWriter, RejectsInconsistentScenes) {
	gltf::SceneDescription scene;
	gltf::MeshDescription m;
	m.positions = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
	m.indices = {0, 1, 3};
	scene.meshes.push_back(m);
	EXPECT_TRUE(gltf::ValidateScene(scene).Value().Contains("hors des"));
	scene.meshes[0].indices = {0, 1, 2};
	scene.meshes[0].uvs = {0.f, 0.f};
	EXPECT_TRUE(gltf::ValidateScene(scene).Value().Contains("UV"));
	scene.meshes[0].uvs.clear();
	scene.nodes.push_back({"a", 0, 1});
	EXPECT_TRUE(gltf::ValidateScene(scene).Value().Contains("précéder"));
	scene.nodes[0].parent = -1;
	gltf::AnimationDescription a{"x", {}};
	a.channels.push_back({0, "rotation", {0.f, 1.f}, {0.f, 0.f, 0.f}, "LINEAR"});
	scene.animations.push_back(a);
	EXPECT_TRUE(gltf::ValidateScene(scene).Value().Contains("valeurs"));
	scene.animations[0].channels[0].values = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
	scene.animations[0].channels[0].interpolation = "CUBICSPLINE";
	EXPECT_TRUE(gltf::ValidateScene(scene).Value().Contains("CUBICSPLINE"));
	scene.animations[0].channels[0].interpolation = "LINEAR";
	EXPECT_TRUE(gltf::ValidateScene(scene).IsNone());
}
