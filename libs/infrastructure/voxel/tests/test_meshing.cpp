// Tests — atlas de textures, maillage des chunks et des formes, modèles
// .obj / .b3d / json (voxel/meshing.hpp, assets.hpp).
#include "test.hpp"

#include <voxel/json.hpp>
#include <voxel/meshing.hpp>

using namespace voxel;

namespace {

void Load(ContentRegistry &content, const char *text) {
	auto parsed = json::Parse(String(text));
	if (parsed.IsError() || content.Load(parsed.Unwrap(), {"t", "", "test"}).IsError())
		std::printf("définition refusée : %s\n", text);
}

struct Scene {
	ContentRegistry content;
	AssetManager assets;
	TextureAtlas atlas;
	Scene() {
		Load(content, R"({"type": "texture", "id": "stone", "procedural": {"color": [120, 120, 120], "pattern": "bricks"}})");
		Load(content, R"({"type": "texture", "id": "water", "procedural": {"color": [40, 80, 200], "alpha": 0.6}})");
		Load(content, R"({"id": "stone", "graphics": {"texture": "stone"}})");
		Load(content, R"({"id": "water", "tags": ["fluid"], "physics": {"solid": false},
			"graphics": {"texture": "water", "material": "TRANSPARENT"}})");
		Load(content, R"({"id": "slab", "type": "nodebox", "nodebox": [[-0.5, -0.5, -0.5, 0.5, 0, 0.5]], "graphics": {"texture": "stone"}})");
		Load(content, R"({"id": "flower", "shape": "cross", "graphics": {"texture": "nope", "material": "CUTOUT"}})");
		assets.Declare(content);
		atlas.Build(assets, content, 16);
	}
};

size_t Quads(const MeshBuffer &b) { return b.indices.size() / 6; }

} // namespace

TEST(Atlas_PacksTexturesAndMarksMissingOnes) {
	Scene s;
	CHECK(s.atlas.Count() >= 3); // stone, water, manquante
	CHECK(s.atlas.Contains("t:stone"));
	CHECK(!s.atlas.Contains("t:nope"));
	const Image &img = s.atlas.Pixels();
	CHECK(img.width >= 32 && (img.width & (img.width - 1)) == 0);
	// La texture inconnue utilise le damier magenta.
	const TextureAtlas::Rect missing = s.atlas.Lookup("t:nope");
	const int px = int(missing.u0 * float(img.width)), py = int(missing.v0 * float(img.height));
	CHECK_EQ(int(img.At(px, py)[0]), 255);
	CHECK_EQ(int(img.At(px, py)[1]), 0);
	// Texture procédurale : transparence demandée.
	const TextureAtlas::Rect water = s.atlas.Lookup("t:water");
	CHECK(img.At(int(water.u0 * float(img.width)) + 2, int(water.v0 * float(img.height)) + 2)[3] < 200);
}

TEST(Mesher_CullsHiddenFacesAndSeparatesTranslucent) {
	Scene s;
	World world(s.content);
	(void)world.EnsureChunk({0, 0, 0});
	ChunkMesher mesher(s.content, s.atlas, &s.assets);
	(void)world.SetBlock({4, 4, 4}, String("t:stone"));
	CHECK_EQ(Quads(mesher.Build(world, {0, 0, 0}).opaque), size_t(6));
	(void)world.SetBlock({5, 4, 4}, String("t:stone")); // face commune cachée des deux côtés
	CHECK_EQ(Quads(mesher.Build(world, {0, 0, 0}).opaque), size_t(10));
	// Eau : translucide, sans faces internes, surface abaissée.
	(void)world.SetBlock({8, 4, 8}, String("t:water"));
	(void)world.SetBlock({9, 4, 8}, String("t:water"));
	const ChunkMesh mesh = mesher.Build(world, {0, 0, 0});
	CHECK_EQ(Quads(mesh.translucent), size_t(10));
	float top = 0.f;
	for (const MeshVertex &v : mesh.translucent.vertices)
		top = std::max(top, v.position.y);
	CHECK_NEAR(top, 4.875f, 1e-5f);
	// Occlusion ambiante : un bloc posé contre une face assombrit ses coins.
	World w2(s.content);
	(void)w2.EnsureChunk({0, 0, 0});
	(void)w2.SetBlock({4, 4, 4}, String("t:stone"));
	(void)w2.SetBlock({5, 5, 4}, String("t:stone"));
	int darkened = 0;
	for (const MeshVertex &v : mesher.Build(w2, {0, 0, 0}).opaque.vertices)
		darkened += v.color[0] < 255;
	CHECK(darkened > 0);
}

TEST(Mesher_NodeBoxesCrossesAndEntityShapes) {
	Scene s;
	World world(s.content);
	(void)world.EnsureChunk({0, 0, 0});
	ChunkMesher mesher(s.content, s.atlas, &s.assets);
	(void)world.SetBlock({2, 2, 2}, String("t:slab"));
	const ChunkMesh slab = mesher.Build(world, {0, 0, 0});
	CHECK_EQ(Quads(slab.opaque), size_t(6));
	float maxY = 0.f;
	for (const MeshVertex &v : slab.opaque.vertices)
		maxY = std::max(maxY, v.position.y);
	CHECK_NEAR(maxY, 2.5f, 1e-5f);
	(void)world.SetBlock({2, 2, 2}, String("t:flower"));
	CHECK_EQ(Quads(mesher.Build(world, {0, 0, 0}).opaque), size_t(4)); // deux plans, deux côtés
	// Forme d'entité : cube de la taille du collisionneur, centré.
	ShapeDesc shape;
	shape.size = {0.6f, 1.8f, 0.6f};
	const MeshBuffer body = mesher.BuildShape(shape, Appearance{});
	CHECK_EQ(Quads(body), size_t(6));
	float minX = 1.f;
	for (const MeshVertex &v : body.vertices)
		minX = std::min(minX, v.position.x);
	CHECK_NEAR(minX, -0.3f, 1e-5f);
	CHECK_EQ(Quads(mesher.BuildBlock(s.content.BlockIdOf("t:stone"))), size_t(6));
}

TEST(Models_ObjB3dAndJsonAreParsed) {
	auto obj = ParseObj(String("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
							   "usemtl wood\nf 1/1 2/2 3/3 4/4\n"));
	REQUIRE(obj.IsOk());
	CHECK_EQ(obj.Value().indices.size(), size_t(6));
	CHECK(obj.Value().groups.front().texture == "wood");
	CHECK_EQ(obj.Value().normals.size(), size_t(4));
	CHECK(ParseObj(String("v 0 0 0\nf 1 2 3\n")).IsError());

	// B3D minimal : un nœud, un maillage, un triangle.
	std::vector<uint8_t> b3d;
	auto tag = [&](const char *t) { b3d.insert(b3d.end(), t, t + 4); };
	auto i32 = [&](int32_t v) { for (int k = 0; k < 4; ++k) b3d.push_back(uint8_t(uint32_t(v) >> (8 * k))); };
	auto f32 = [&](float f) { int32_t v; std::memcpy(&v, &f, 4); i32(v); };
	auto patch = [&](size_t at) { const int32_t len = int32_t(b3d.size() - at - 4); std::memcpy(&b3d[at], &len, 4); };
	tag("BB3D"); const size_t root = b3d.size(); i32(0); i32(1);
	tag("NODE"); const size_t node = b3d.size(); i32(0); b3d.push_back(0);
	for (float v : {0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f}) f32(v);
	tag("MESH"); const size_t meshAt = b3d.size(); i32(0); i32(-1);
	tag("VRTS"); const size_t vrts = b3d.size(); i32(0); i32(0); i32(1); i32(2);
	for (float v : {0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f}) f32(v);
	patch(vrts);
	tag("TRIS"); const size_t tris = b3d.size(); i32(0); i32(-1); i32(0); i32(1); i32(2);
	patch(tris); patch(meshAt); patch(node); patch(root);
	auto mesh = ParseB3D(b3d);
	REQUIRE(mesh.IsOk());
	CHECK_EQ(mesh.Value().positions.size(), size_t(3));
	CHECK_EQ(mesh.Value().indices.size(), size_t(3));
	CHECK_NEAR(mesh.Value().positions[2].z, -1.f, 1e-6f); // repère indirect retourné
	CHECK(ParseB3D({1, 2, 3}).IsError());

	auto json = ModelFromJson(json::Parse(String(R"({"boxes": [[-0.5, -0.5, -0.5, 0.5, 0.5, 0.5]], "texture": "box"})")).Unwrap());
	REQUIRE(json.IsOk());
	CHECK_EQ(json.Value().indices.size(), size_t(36));
}

TEST(Textures_ComposeOverlaysAndFilters) {
	// Deux images 2×2 : un fond rouge opaque et un calque vert à moitié vide.
	auto load = [](const String &name) -> Result<Image, String> {
		Image image;
		image.width = image.height = 2;
		if (name == "terre.png")
			image.rgba = {200, 0, 0, 255, 200, 0, 0, 255, 200, 0, 0, 255, 200, 0, 0, 255};
		else if (name == "herbe.png")
			image.rgba = {0, 200, 0, 255, 0, 200, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0};
		else
			return Err(String("absent : ") + name);
		return Ok(std::move(image));
	};
	auto composed = ComposeTexture("(terre.png^herbe.png)^ligthing(0.5)", load);
	REQUIRE(composed.IsOk());
	const Image &img = composed.Value();
	CHECK_EQ(img.width, 2);
	CHECK(img.At(0, 0)[0] == 0 && img.At(0, 0)[1] == 100);   // herbe, assombrie
	CHECK(img.At(0, 1)[0] == 100 && img.At(0, 1)[3] == 255); // terre visible dessous

	auto flipped = ComposeTexture("terre.png ^ herbe.png ^ flipy() ^ opacity(0.5)", load);
	REQUIRE(flipped.IsOk());
	CHECK(flipped.Value().At(0, 1)[1] == 200 && flipped.Value().At(0, 1)[3] == 128);
	auto flat = ComposeTexture("color(0, 0, 255)^resize(4, 4)", load);
	REQUIRE(flat.IsOk());
	CHECK(flat.Value().width == 4 && flat.Value().At(3, 3)[2] == 255);

	CHECK(ComposeTexture("terre.png^inconnu(1)", load).IsError());
	CHECK(ComposeTexture("(terre.png^herbe.png", load).IsError());
	CHECK(ComposeTexture("lighting(0.5)", load).IsError());
	CHECK(ComposeTexture("terre.png^absent.png", load).IsError());
}

TEST(Textures_ExpressionsInDefinitionsBecomeTextures) {
	ContentRegistry content;
	Load(content, R"({"type": "texture", "id": "stone", "procedural": {"color": [120, 120, 120]}})");
	Load(content, R"({"type": "texture", "id": "moss", "procedural": {"color": [0, 160, 0], "alpha": 0.5}})");
	Load(content, R"({"type": "texture", "id": "mossy", "compose": "stone^moss"})");
	Load(content, R"j({"id": "mossy_stone", "graphics": {"textures": {"top": "mossy", "side": "stone^moss^lighting(0.8)"}}})j");
	const BlockDef *b = content.FindBlock("t:mossy_stone");
	REQUIRE(b != nullptr);
	CHECK(b->look.Texture(Face::TOP) == "t:mossy");
	CHECK(b->look.Texture(Face::NORTH) == "t:{stone^moss^lighting(0.8)}");
	CHECK(content.Validate().empty());
	AssetManager assets;
	assets.Declare(content);
	std::shared_ptr<TextureImage> side = assets.Texture(b->look.Texture(Face::NORTH));
	std::shared_ptr<TextureImage> top = assets.Texture("t:mossy");
	REQUIRE(side != nullptr && top != nullptr);
	CHECK(side->Pixels().At(0, 0)[1] < top->Pixels().At(0, 0)[1]); // lighting(0.8)
	CHECK(top->Pixels().At(0, 0)[1] > 120);                      // la mousse verdit la pierre
}
