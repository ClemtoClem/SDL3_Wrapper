// Tests — atlas de textures, maillage des chunks et des formes, modèles
// .obj / .b3d / json (voxel/meshing.hpp, assets.hpp).
#include "test.hpp"

#include <voxel/json.hpp>
#include <voxel/meshing.hpp>
#include <voxel/worldgen.hpp>

#include <cmath>

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

namespace {

/// Luminosité (rouge, 0..255) des sommets d'une face du dessus à la hauteur `y`
/// au-dessus du bloc (x, z) d'un chunk : la plus forte des quatre.
int TopFaceLight(const MeshBuffer &b, int x, float y, int z) {
	int best = -1;
	for (const MeshVertex &v : b.vertices)
		if (v.normal.y > 0.9f && std::fabs(v.position.y - y) < 1e-3f && v.position.x >= float(x) - 1e-3f &&
			v.position.x <= float(x + 1) + 1e-3f && v.position.z >= float(z) - 1e-3f && v.position.z <= float(z + 1) + 1e-3f)
			best = std::max(best, int(v.color[0]));
	return best;
}

} // namespace

TEST(Mesher_LightIsRecomputedAsChunksArrive) {
	Scene s;
	Load(s.content, R"({"type": "generator", "id": "flat16", "kind": "flat", "layers": [["stone", 16]]})");
	World world(s.content);
	world.SetGenerator(MakeGenerator(s.content, "t:flat16", 1));
	ChunkMesher mesher(s.content, s.atlas, &s.assets);
	for (int z = -1; z <= 1; ++z)
		for (int x = -1; x <= 1; ++x)
			(void)world.EnsureChunk({x, 0, z});
	// Surface en y = 15, chunk du dessus pas encore chargé : la face n'est pas
	// encore montrée (bord du monde chargé)…
	CHECK_EQ(TopFaceLight(mesher.Build(world, {0, 0, 0}).opaque, 4, 16.f, 4), -1);

	// …le chunk du dessus arrive : celui du dessous est à refaire, et sa
	// surface paraît, pleinement éclairée — sans le « carré sombre » d'un
	// chunk maillé avant ses voisins (coins du dessus pas encore chargés).
	const uint64_t before = world.GetChunk({0, 0, 0})->Revision();
	(void)world.EnsureChunk({0, 1, 0});
	CHECK(world.GetChunk({0, 0, 0})->Revision() > before);
	CHECK_EQ(TopFaceLight(mesher.Build(world, {0, 0, 0}).opaque, 15, 16.f, 15), 255); // coin : voisins du dessus absents
	CHECK_EQ(TopFaceLight(mesher.Build(world, {0, 0, 0}).opaque, 4, 16.f, 4), 255);

	// Un toit au-dessus d'une partie du chunk : le sol dessous s'assombrit,
	// et le chunk est à refaire sans que le toit le touche directement.
	const uint64_t beforeRoof = world.GetChunk({0, 0, 0})->Revision();
	for (int z = 2; z <= 6; ++z)
		for (int x = 2; x <= 6; ++x)
			for (int y = 20; y <= 22; ++y)
				(void)world.SetBlock({x, y, z}, String("t:stone"));
	CHECK(world.GetChunk({0, 0, 0})->Revision() > beforeRoof);
	const ChunkMesh roofed = mesher.Build(world, {0, 0, 0});
	const int shaded = TopFaceLight(roofed.opaque, 4, 16.f, 4), open = TopFaceLight(roofed.opaque, 12, 16.f, 12);
	CHECK(shaded > 0 && shaded < open * 2 / 3);
	CHECK_EQ(open, 255);
	// Désactivée (îles flottantes) : plus d'ombre du ciel.
	mesher.options.skyLight = false;
	CHECK_EQ(TopFaceLight(mesher.Build(world, {0, 0, 0}).opaque, 4, 16.f, 4), 255);
}

TEST(Mesher_CavesUnderUnloadedTerrainAreDark) {
	Scene s;
	Load(s.content, R"({"type": "generator", "id": "deep", "kind": "flat", "layers": [["stone", 48]]})");
	World world(s.content);
	world.SetGenerator(MakeGenerator(s.content, "t:deep", 1));
	ChunkMesher mesher(s.content, s.atlas, &s.assets);
	for (int z = -1; z <= 1; ++z)
		for (int x = -1; x <= 1; ++x)
			(void)world.EnsureChunk({x, 0, z});
	// Une cavité en y = 8..9 : 32 blocs de roche au-dessus, dont la plupart
	// dans des chunks pas chargés (estimation du générateur).
	for (int z = 3; z <= 5; ++z)
		for (int x = 3; x <= 5; ++x)
			for (int y = 8; y <= 9; ++y)
				(void)world.SetBlock({x, y, z}, AIR);
	const int floor = TopFaceLight(mesher.Build(world, {0, 0, 0}).opaque, 4, 8.f, 4);
	CHECK(floor > 0 && floor < 70);
}
