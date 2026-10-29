// Tests — atlas de textures, maillage des chunks et des formes, modèles
// .obj / .b3d / json (voxel/meshing.hpp, assets.hpp). Écrit UNIQUEMENT sous
// build/tests/tmp/voxel/.
#include "test.hpp"

#include <voxel/json.hpp>
#include <voxel/meshing.hpp>
#include <voxel/mods.hpp>
#include <voxel/worldgen.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

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
		darkened += v.light[3] < 1.f; // l'occlusion ambiante vit dans la lumière, plus dans la teinte
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

// Objet au sol : l'image en plaque mince, une facette de tranche par bord de pixel opaque.
TEST(Meshing_SpriteIsAThinExtrudedImage) {
	Scene s;
	Load(s.content, R"({"type": "texture", "id": "ghost", "procedural": {"color": [200, 200, 200], "alpha": 0.2}})");
	Load(s.content, R"({"type": "item", "id": "ghost", "icon": "ghost"})"); // l'atlas ne prend que les textures utilisées
	s.assets.Declare(s.content);
	s.atlas.Build(s.assets, s.content, 16);
	ChunkMesher mesher(s.content, s.atlas);
	// Image pleine de 16 × 16 : avant, arrière, et les 4 × 16 bords du pourtour.
	const MeshBuffer solid = mesher.BuildSprite("t:stone");
	CHECK_EQ(Quads(solid), size_t(2 + 4 * 16));
	float minZ = 1.f, maxZ = -1.f, minX = 1.f, maxY = 0.f;
	for (const MeshVertex &v : solid.vertices) {
		minZ = std::min(minZ, v.position.z);
		maxZ = std::max(maxZ, v.position.z);
		minX = std::min(minX, v.position.x);
		maxY = std::max(maxY, v.position.y);
	}
	CHECK_NEAR(maxZ - minZ, 1.f / 16.f, 1e-5f);
	CHECK_NEAR(minX, -0.5f, 1e-5f);
	CHECK_NEAR(maxY, 1.f, 1e-5f);
	// Image transparente : les deux faces seulement (le shader découpe), pas de tranche.
	CHECK_EQ(Quads(mesher.BuildSprite("t:ghost")), size_t(2));
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

/// Part du ciel × occlusion (0..255) des sommets d'une face du dessus à la
/// hauteur `y` au-dessus du bloc (x, z) d'un chunk : la plus forte des quatre.
int TopFaceLight(const MeshBuffer &b, int x, float y, int z) {
	int best = -1;
	for (const MeshVertex &v : b.vertices)
		if (v.normal.y > 0.9f && std::fabs(v.position.y - y) < 1e-3f && v.position.x >= float(x) - 1e-3f &&
			v.position.x <= float(x + 1) + 1e-3f && v.position.z >= float(z) - 1e-3f && v.position.z <= float(z + 1) + 1e-3f)
			best = std::max(best, int(std::lround(v.light[3] * 255.f)));
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
	// dans des chunks pas chargés (estimation du générateur) : aucun ciel.
	for (int z = 3; z <= 5; ++z)
		for (int x = 3; x <= 5; ++x)
			for (int y = 8; y <= 9; ++y)
				(void)world.SetBlock({x, y, z}, AIR);
	const int floor = TopFaceLight(mesher.Build(world, {0, 0, 0}).opaque, 4, 8.f, 4);
	CHECK_EQ(floor, 0);
}

TEST(Atlas_LightBlocksGetABrighterHigherContrastTexture) {
	// Une texture terne : gris de 90 à 140, un pixel transparent.
	Image dull;
	dull.width = dull.height = 4;
	for (int i = 0; i < 16; ++i) {
		const uint8_t v = uint8_t(90 + (i % 4) * 16);
		dull.rgba.insert(dull.rgba.end(), {v, uint8_t(v + 10), uint8_t(v - 20), uint8_t(i == 5 ? 0 : 255)});
	}
	const Image glow = TextureAtlas::Glow(dull);
	REQUIRE_EQ(glow.width, 4);
	REQUIRE_EQ(glow.height, 4);
	auto lum = [](const Image &im, int x, int y) { return 0.299 * im.At(x, y)[0] + 0.587 * im.At(x, y)[1] + 0.114 * im.At(x, y)[2]; };
	double before = 0.0, after = 0.0, brightest = 0.0;
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x) {
			before += lum(dull, x, y);
			after += lum(glow, x, y);
			brightest = std::max(brightest, lum(glow, x, y));
		}
	CHECK(after > before);   // plus lumineuse en moyenne…
	CHECK(brightest > 230.0); // …et ses parties claires presque blanches
	CHECK(lum(glow, 3, 0) - lum(glow, 0, 0) > (lum(dull, 3, 0) - lum(dull, 0, 0)) * 2.0); // plus contrastée
	CHECK_EQ(int(glow.At(1, 1)[3]), 0); // la transparence reste
	// Les teintes restent : le vert (canal le plus fort) domine toujours.
	CHECK(glow.At(3, 0)[1] >= glow.At(3, 0)[0] && glow.At(3, 0)[0] >= glow.At(3, 0)[2]);
	// Bande animée : la première image seulement.
	Image strip = dull;
	strip.height = 8;
	strip.rgba.insert(strip.rgba.end(), dull.rgba.begin(), dull.rgba.end());
	CHECK_EQ(TextureAtlas::Glow(strip).height, 4);

	// Dans l'atlas : variante « lueur » des textures des blocs lumineux seulement.
	Scene s;
	Load(s.content, R"({"type": "texture", "id": "lamp", "procedural": {"color": [150, 140, 100], "pattern": "bricks"}})");
	Load(s.content, R"({"id": "lamp", "graphics": {"texture": "lamp", "light": 14}})");
	Load(s.content, R"({"id": "dim_lamp", "graphics": {"texture": "stone", "light": 8, "glow": false}})");
	s.assets.Declare(s.content);
	s.atlas.Build(s.assets, s.content, 16);
	CHECK(s.atlas.Contains(TextureAtlas::GlowId("t:lamp")));
	CHECK(!s.atlas.Contains(TextureAtlas::GlowId("t:stone"))); // « glow »: false : lumière sans lueur
	const TextureAtlas::Rect plain = s.atlas.Lookup("t:stone");
	const TextureAtlas::Rect fallback = s.atlas.LookupGlow("t:stone");
	CHECK_NEAR(plain.u0, fallback.u0, 1e-6f);
}

TEST(Mesher_LightBlocksAreFullBrightInTheirOwnBuffer) {
	Scene s;
	Load(s.content, R"({"id": "lamp", "graphics": {"texture": "stone", "light": 14}})");
	Load(s.content, R"({"id": "torch", "type": "nodebox", "nodebox": [[-0.06, -0.5, -0.06, 0.06, 0.1, 0.06]], "graphics": {"texture": "stone", "light": 12}})");
	s.assets.Declare(s.content);
	s.atlas.Build(s.assets, s.content, 16);
	World world(s.content);
	(void)world.EnsureChunk({0, 0, 0});
	(void)world.EnsureChunk({0, 1, 0});
	// Sous un plafond (ombre du ciel) et contre des pierres (occlusion).
	for (int x = 2; x <= 8; ++x)
		for (int z = 2; z <= 8; ++z)
			(void)world.SetBlock({x, 10, z}, String("t:stone"));
	(void)world.SetBlock({5, 8, 5}, String("t:lamp"));
	(void)world.SetBlock({5, 8, 6}, String("t:stone"));
	(void)world.SetBlock({4, 8, 5}, String("t:torch"));
	ChunkMesher mesher(s.content, s.atlas, &s.assets);
	const ChunkMesh mesh = mesher.Build(world, {0, 0, 0});
	CHECK(!mesh.emissive.IsEmpty());
	CHECK_EQ(Quads(mesh.emissive), size_t(5 + 6)); // la lampe (une face cachée) et la torche
	for (const MeshVertex &v : mesh.emissive.vertices)
		CHECK(v.color[0] == 255 && v.color[1] == 255 && v.color[2] == 255);
	// Les pierres voisines, elles, restent assombries (dans le tampon ordinaire).
	bool shaded = false;
	for (const MeshVertex &v : mesh.opaque.vertices)
		shaded = shaded || v.light[3] < 1.f;
	CHECK(shaded);
	CHECK_EQ(mesh.lights.size(), size_t(2));
}

TEST(Atlas_KeepsTextureResolutionsAndBuildsMipmaps) {
	ContentRegistry content;
	Load(content, R"({"type": "texture", "id": "big", "procedural": {"color": [200, 40, 40], "noise": 0, "size": 64}})");
	Load(content, R"({"type": "texture", "id": "mid", "procedural": {"color": [40, 200, 40], "noise": 0, "size": 16}})");
	Load(content, R"({"type": "texture", "id": "tiny", "procedural": {"color": [40, 40, 200], "noise": 0, "size": 8}})");
	Load(content, R"({"id": "big", "graphics": {"texture": "big"}})");
	Load(content, R"({"id": "mid", "graphics": {"texture": "mid"}})");
	Load(content, R"({"id": "tiny", "graphics": {"texture": "tiny"}})");
	AssetManager assets;
	assets.Declare(content);
	TextureAtlas atlas;
	atlas.Build(assets, content, 16, 32);
	const Image &img = atlas.Pixels();
	// Résolution propre à chaque texture, bornée à [16, 32] (moins un demi-texel par bord).
	auto width = [&](const char *id) { const TextureAtlas::Rect r = atlas.Lookup(id); return (r.u1 - r.u0) * float(img.width); };
	CHECK_NEAR(width("t:big"), 31.f, 0.01f);
	CHECK_NEAR(width("t:mid"), 15.f, 0.01f);
	CHECK_NEAR(width("t:tiny"), 15.f, 0.01f);
	// Tuiles disjointes.
	struct Px {
		int x0, y0, x1, y1;
	};
	std::vector<Px> tiles;
	for (const char *id : {"t:big", "t:mid", "t:tiny", TextureAtlas::MISSING}) {
		const TextureAtlas::Rect r = atlas.Lookup(id);
		tiles.push_back({int(std::floor(r.u0 * float(img.width))), int(std::floor(r.v0 * float(img.height))),
						 int(std::ceil(r.u1 * float(img.width))), int(std::ceil(r.v1 * float(img.height)))});
	}
	for (size_t a = 0; a < tiles.size(); ++a)
		for (size_t b = a + 1; b < tiles.size(); ++b)
			CHECK(tiles[a].x1 <= tiles[b].x0 || tiles[b].x1 <= tiles[a].x0 || tiles[a].y1 <= tiles[b].y0 || tiles[b].y1 <= tiles[a].y0);
	// Réduction : une texture unie le reste.
	const uint8_t *center = img.At((tiles[0].x0 + tiles[0].x1) / 2, (tiles[0].y0 + tiles[0].y1) / 2);
	CHECK(std::abs(int(center[0]) - 200) <= 3 && std::abs(int(center[1]) - 40) <= 3);
	// Mipmaps : de la tuile de base (16) à un pixel, sans mélange entre tuiles.
	REQUIRE_EQ(atlas.Mipmaps().size(), size_t(4));
	CHECK_EQ(atlas.Mipmaps()[0].width, img.width / 2);
	CHECK_EQ(atlas.Mipmaps()[3].width, img.width / 16);
	const Image &quarter = atlas.Mipmaps()[1];
	const uint8_t *blue = quarter.At(tiles[2].x0 / 4 + 1, tiles[2].y0 / 4 + 1);
	CHECK(blue[2] > 180 && blue[0] < 60 && blue[1] < 60);
	// Moyenne pondérée par l'alpha : un damier noir/blanc devient gris, un
	// pixel transparent ne noircit pas son voisin.
	Image checker;
	checker.width = checker.height = 2;
	checker.rgba = {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255};
	const Image grey = TextureAtlas::Resample(checker, 1);
	CHECK(std::abs(int(grey.rgba[0]) - 128) <= 1 && grey.rgba[3] == 255);
	checker.rgba = {255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 255};
	const Image red = TextureAtlas::Resample(checker, 1);
	CHECK(red.rgba[0] == 255 && red.rgba[3] == 128);
}

TEST(Textures_VariantsArePickedPerBlock) {
	// Fichiers « nom.1 », « nom.2 » d'un mod : une texture « nom » à variantes.
	namespace fs = std::filesystem;
	const fs::path root = "build/tests/tmp/voxel/variants";
	fs::remove_all(root);
	fs::create_directories(root / "vary" / "textures");
	std::ofstream(root / "vary" / "mod.json") << R"({"id": "vary", "name": "Variantes", "version": "1.0"})";
	for (const char *name : {"wall.1.png", "wall.2.png", "wall.10.png", "plain.png", "lonely.1.png"})
		std::ofstream(root / "vary" / "textures" / name) << "";
	ContentRegistry registry;
	(void)ModLoader::LoadAll(registry, {String(root.string().c_str())});
	const TextureDef *wall = registry.FindTexture("vary:wall");
	REQUIRE(wall != nullptr);
	REQUIRE_EQ(wall->variants.size(), size_t(3));
	CHECK(wall->variants[0] == "vary:wall.1" && wall->variants[1] == "vary:wall.2" && wall->variants[2] == "vary:wall.10");
	CHECK(registry.FindTexture("vary:plain") && registry.FindTexture("vary:plain")->variants.empty());
	CHECK(registry.FindTexture("vary:lonely") == nullptr); // une seule variante : pas de groupe

	// Atlas et maillage : chaque bloc tire sa variante d'après sa position.
	Scene s;
	Load(s.content, R"({"type": "texture", "id": "red", "procedural": {"color": [220, 30, 30]}})");
	Load(s.content, R"({"type": "texture", "id": "blue", "procedural": {"color": [30, 30, 220]}})");
	Load(s.content, R"({"type": "texture", "id": "patch", "variants": ["red", "blue"]})");
	Load(s.content, R"({"id": "patch", "graphics": {"texture": "patch"}})");
	s.assets.Declare(s.content);
	s.atlas.Build(s.assets, s.content, 16);
	CHECK_EQ(s.atlas.LookupVariants("t:patch").size(), size_t(2));
	CHECK_NEAR(s.atlas.Lookup("t:patch").u0, s.atlas.Lookup("t:red").u0, 1e-6f); // la première par défaut
	World world(s.content);
	(void)world.EnsureChunk({0, 0, 0});
	for (int x = 0; x < 8; ++x)
		for (int z = 0; z < 8; ++z)
			(void)world.SetBlock({x, 4, z}, String("t:patch"));
	ChunkMesher mesher(s.content, s.atlas, &s.assets);
	auto used = [&] {
		std::vector<float> out;
		for (const MeshVertex &v : mesher.Build(world, {0, 0, 0}).opaque.vertices)
			out.push_back(v.uv.x);
		return out;
	};
	const std::vector<float> first = used();
	std::set<int> tiles;
	const TextureAtlas::Rect red = s.atlas.Lookup("t:red"), blue = s.atlas.Lookup("t:blue");
	for (float u : first)
		tiles.insert(u >= red.u0 - 1e-5f && u <= red.u1 + 1e-5f ? 0 : u >= blue.u0 - 1e-5f && u <= blue.u1 + 1e-5f ? 1 : 2);
	CHECK(tiles == std::set<int>({0, 1})); // les deux variantes, rien d'autre
	CHECK(used() == first);                // et toujours les mêmes aux mêmes places
}
