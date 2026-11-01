// Tests — réflexions : réflectivité des textures (TextureDef, carte de
// l'atlas), table des blocs des rayons (ChunkMesher::RayBlockTable) et grille
// des rayons (voxel/raygrid.hpp : blocs qui arrêtent les rayons, part de ciel,
// lumière des sources). Le tracé lui-même est testé sur GPU (render3d,
// OffscreenSmokeTest.VoxelReflectionsTraceTheGrid).
#include "test.hpp"

#include <voxel/json.hpp>
#include <voxel/meshing.hpp>
#include <voxel/raygrid.hpp>

using namespace voxel;

namespace {

void Load(ContentRegistry &content, const char *text) {
	auto parsed = json::Parse(String(text));
	if (parsed.IsError() || content.Load(parsed.Unwrap(), {"t", "", "test"}).IsError())
		std::printf("définition refusée : %s\n", text);
}

struct Mirrors {
	ContentRegistry content;
	std::unique_ptr<World> world;
	Mirrors() {
		Load(content, R"({"type": "texture", "id": "grey", "procedural": {"color": [128, 128, 128]}})");
		Load(content, R"({"type": "texture", "id": "mirror", "procedural": {"color": [200, 200, 220]}, "reflectivity": 1})");
		Load(content, R"({"type": "texture", "id": "half", "procedural": {"color": [90, 90, 90]}, "reflectivity": 0.5})");
		Load(content, R"({"type": "texture", "id": "too_much", "procedural": {"color": [90, 90, 90]}, "reflectivity": 3})");
		Load(content, R"({"id": "stone", "graphics": {"texture": "grey"}})");
		Load(content, R"({"id": "mirror_block", "graphics": {"texture": "mirror"}})");
		Load(content, R"({"id": "glass", "graphics": {"texture": "grey", "material": "TRANSLUCENT"}})");
		Load(content, R"({"id": "slab", "type": "nodebox", "nodebox": [[-0.5, -0.5, -0.5, 0.5, 0, 0.5]], "graphics": {"texture": "grey"}})");
		Load(content, R"({"id": "lamp", "graphics": {"texture": "half"},
			"light": {"type": "point", "color": [255, 0, 0], "intensity": 4, "range": 8}})");
		world = std::make_unique<World>(content);
		for (int y = -1; y <= 2; ++y)
			for (int z = -1; z <= 1; ++z)
				for (int x = -1; x <= 1; ++x)
					(void)world->EnsureChunk({x, y, z});
	}
	void Put(const BlockPos &p, const char *id) { (void)world->SetBlock(p, String("t:") + String(id), 0, ChangeCause::SCRIPT); }
	BlockId Id(const char *id) const { return content.BlockIdOf(String("t:") + String(id)); }
};

const uint16_t *Texel(const std::vector<uint16_t> &cells, int x, int y, int z) {
	return &cells[size_t(((z * CHUNK_SIZE + y) * CHUNK_SIZE + x) * raygrid::CHANNELS)];
}

} // namespace

TEST(RayGrid_TextureReflectivityAndAtlasMap) {
	Mirrors m;
	CHECK_NEAR(m.content.FindTexture("t:mirror")->reflectivity, 1.f, 1e-6f);
	CHECK_NEAR(m.content.FindTexture("t:half")->reflectivity, 0.5f, 1e-6f);
	CHECK_NEAR(m.content.FindTexture("t:too_much")->reflectivity, 1.f, 1e-6f); // borné à 1
	CHECK_NEAR(m.content.FindTexture("t:grey")->reflectivity, 0.f, 1e-6f);
	AssetManager assets;
	assets.Declare(m.content);
	TextureAtlas atlas;
	atlas.Build(assets, m.content, 16);
	const std::vector<uint8_t> &map = atlas.Reflectivity();
	REQUIRE_EQ(map.size(), size_t(atlas.Pixels().width) * size_t(atlas.Pixels().height));
	auto at = [&](const char *id) {
		const TextureAtlas::Rect r = atlas.Lookup(String(id));
		const int x = int((r.u0 + r.u1) * 0.5f * float(atlas.Pixels().width));
		const int y = int((r.v0 + r.v1) * 0.5f * float(atlas.Pixels().height));
		return int(map[size_t(y) * size_t(atlas.Pixels().width) + size_t(x)]);
	};
	CHECK_EQ(at("t:mirror"), 255);
	CHECK_EQ(at("t:half"), 128);
	CHECK_EQ(at("t:grey"), 0);
	// Aucune texture qui réfléchit : pas de carte.
	ContentRegistry plain;
	Load(plain, R"({"type": "texture", "id": "grey", "procedural": {"color": [128, 128, 128]}})");
	Load(plain, R"({"id": "stone", "graphics": {"texture": "grey"}})");
	AssetManager plainAssets;
	plainAssets.Declare(plain);
	TextureAtlas plainAtlas;
	plainAtlas.Build(plainAssets, plain, 16);
	CHECK(plainAtlas.Reflectivity().empty());
}

TEST(RayGrid_BlockTableHoldsFacesTintsAndGlow) {
	Mirrors m;
	AssetManager assets;
	assets.Declare(m.content);
	TextureAtlas atlas;
	atlas.Build(assets, m.content, 16);
	ChunkMesher mesher(m.content, atlas, &assets);
	int rows = 0;
	const int perRow = 4;
	const std::vector<float> table = mesher.RayBlockTable(perRow, nullptr, rows);
	const int count = int(m.content.BlockCount());
	CHECK_EQ(rows, ((count + perRow - 1) / perRow) * 8);
	REQUIRE_EQ(table.size(), size_t(perRow * rows * 4));
	auto row = [&](BlockId id, int r) { return &table[((size_t(id / perRow) * 8 + size_t(r)) * perRow + size_t(id % perRow)) * 4]; };
	const BlockId mirror = m.Id("mirror_block");
	const TextureAtlas::Rect rect = atlas.Lookup("t:mirror");
	for (int f = 0; f < FACE_COUNT; ++f) {
		CHECK_NEAR(row(mirror, f)[0], rect.u0, 1e-6f);
		CHECK_NEAR(row(mirror, f)[3], rect.v1, 1e-6f);
	}
	CHECK_NEAR(row(mirror, 6)[0], 1.f, 1e-6f); // teinte blanche
	CHECK_NEAR(row(mirror, 6)[3], 0.f, 1e-6f); // aucune face teintée par le biome
	CHECK_NEAR(row(mirror, 7)[3], 0.f, 1e-6f); // pas lumineux
	CHECK_NEAR(row(m.Id("lamp"), 7)[3], 1.f, 1e-6f);
}

TEST(RayGrid_ChunkCellsStopRaysAndCarryLight) {
	Mirrors m;
	// Un mur de miroirs et de pierre, du verre et une dalle (traversés), une lampe rouge.
	m.Put({2, 3, 5}, "mirror_block");
	m.Put({3, 3, 5}, "stone");
	m.Put({4, 3, 5}, "glass");
	m.Put({5, 3, 5}, "slab");
	CHECK(raygrid::StopsRays(m.content.Block(m.Id("stone"))));
	CHECK(!raygrid::StopsRays(m.content.Block(m.Id("glass"))));
	CHECK(!raygrid::StopsRays(m.content.Block(m.Id("slab"))));
	CHECK(!raygrid::StopsRays(m.content.Block(AIR)));
	std::vector<uint16_t> cells(raygrid::CHUNK_TEXELS);
	REQUIRE(raygrid::FillChunk(*m.world, {0, 0, 0}, true, cells));
	CHECK_EQ(int(Texel(cells, 2, 3, 5)[0]), int(m.Id("mirror_block")));
	CHECK_EQ(int(Texel(cells, 3, 3, 5)[0]), int(m.Id("stone")));
	CHECK_EQ(int(Texel(cells, 4, 3, 5)[0]), 0);
	CHECK_EQ(int(Texel(cells, 5, 3, 5)[0]), 0);
	// Ciel : ouvert au-dessus d'une colonne vide, nul sous la pierre ; dimension
	// sans ciel : l'ambiante (sa lumière de base) partout, sous la pierre aussi.
	CHECK_EQ(int(Texel(cells, 8, 8, 8)[1]), 255);
	CHECK_EQ(int(Texel(cells, 3, 2, 5)[1]), 0);
	CHECK(Texel(cells, 4, 2, 5)[1] > 0); // sous le verre : il en passe
	std::vector<uint16_t> dark(raygrid::CHUNK_TEXELS);
	REQUIRE(raygrid::FillChunk(*m.world, {0, 0, 0}, false, dark));
	CHECK_EQ(int(Texel(dark, 8, 8, 8)[1]), 255);
	CHECK_EQ(int(Texel(dark, 3, 2, 5)[1]), 255);
	// Lampe rouge : la cellule voisine reçoit du rouge, pas de vert ni de bleu ; au-delà de la portée, rien.
	m.Put({8, 8, 8}, "lamp");
	REQUIRE(raygrid::FillChunk(*m.world, {0, 0, 0}, true, cells));
	const uint16_t *near = Texel(cells, 9, 8, 8);
	CHECK((near[2] & 255) > 50);
	CHECK_EQ(int(near[2] >> 8), 0);
	CHECK_EQ(int(near[3]), 0);
	std::vector<uint16_t> far(raygrid::CHUNK_TEXELS);
	REQUIRE(raygrid::FillChunk(*m.world, {1, 0, 1}, true, far)); // à plus de 8 blocs
	CHECK_EQ(int(Texel(far, 15, 8, 15)[2] & 255), 0);
	// Chunk absent : rien, ciel ouvert.
	CHECK(!raygrid::FillChunk(*m.world, {9, 9, 9}, true, cells));
	CHECK_EQ(int(cells[0]), 0);
	CHECK_EQ(int(cells[1]), 255);
}
