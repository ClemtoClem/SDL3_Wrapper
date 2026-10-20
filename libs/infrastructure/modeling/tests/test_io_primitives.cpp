#include "test.hpp"
#include <modeling/io.hpp>
#include <modeling/operations.hpp>
#include <modeling/primitives.hpp>

#include <cmath>
#include <filesystem>

using namespace modeling;

namespace {

float SolidVolume(const Model &m) {
	const MeshData mesh = BuildMesh(m);
	float v = 0.f;
	for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
		v += mesh.positions[mesh.indices[i]].Dot(
				 mesh.positions[mesh.indices[i + 1]].Cross(mesh.positions[mesh.indices[i + 2]])) /
			 6.f;
	return v;
}

bool Watertight(const Model &m) {
	for (size_t e = 0; e < m.Edges().size(); ++e)
		if (m.Edges()[e].alive && m.FacesOfEdge(EdgeId(e)).size() != 2)
			return false;
	return true;
}

String TempPath(const char *name) {
	return String((std::filesystem::temp_directory_path() / name).string().c_str());
}

} // namespace

TEST(Io_JsonRoundTripKeepsEverything) {
	Model m;
	const uint32_t wood = m.AddMaterial("Bois", {0.5f, 0.3f, 0.1f, 1.f}, 0.5f);
	const uint32_t tag = m.AddLayer("Meubles", false);
	const FaceId f = m.AddFace({{0, 0, 0}, {0, 0, 4}, {4, 0, 4}, {4, 0, 0}}, {{{1, 0, 1}, {2, 0, 1}, {2, 0, 2}, {1, 0, 2}}},
							   {.material = wood, .layer = tag})
						 .Unwrap();
	(void)PushPull(m, f, 1.f);
	(void)m.AddCircle({8, 0, 0}, {0, 1, 0}, 1.f, {.segments = 12, .filled = false});
	m.Edges()[0].soft = true;
	const Result<Model, String> back = Deserialize(Serialize(m));
	REQUIRE(back.IsOk());
	const Model &r = back.Value();
	CHECK_EQ(r.FaceCount(), m.FaceCount());
	CHECK_EQ(r.EdgeCount(), m.EdgeCount());
	CHECK_EQ(r.Materials().size(), 1u);
	CHECK_NEAR(r.Materials()[0].textureScale, 0.5f, 1e-6f);
	CHECK(!r.Layers()[tag].visible);
	CHECK_NEAR(SolidVolume(r), SolidVolume(m), 1e-4f);
	size_t holes = 0, soft = 0, curves = 0;
	for (const Face &face : r.Faces())
		holes += face.holes.size();
	for (const Edge &e : r.Edges()) {
		soft += e.soft;
		curves += e.curve != INVALID_ID;
	}
	CHECK(holes >= 1u);
	CHECK(soft >= 1u);
	CHECK_EQ(curves, 12u);
	CHECK(Deserialize(String("{\"format\": \"autre\"}")).IsError());
	CHECK(Deserialize(String("pas du json")).IsError());
}

TEST(Io_SaveLoadAndExport) {
	Model m;
	(void)PushPull(m, m.AddFace({{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}}).Unwrap(), 1.f);
	Paint(m, 0, m.AddMaterial("Rouge brique", {0.7f, 0.2f, 0.1f}));
	const String path = TempPath("modeling_test.mesh");
	REQUIRE(SaveModel(path, m).IsNone());
	const Result<Model, String> loaded = LoadModel(path);
	REQUIRE(loaded.IsOk());
	CHECK_EQ(loaded.Value().FaceCount(), 6u);
	for (const char *ext : {".obj", ".stl", ".ply"})
		CHECK(ExportModel(TempPath("modeling_export") + ext, m).IsNone());
	CHECK(std::filesystem::exists(TempPath("modeling_export.mtl").CStr())); // matériaux de l'OBJ
	CHECK(LoadModel(TempPath("absent.mesh")).IsError());
}

TEST(Io_FromTrianglesRebuildsEditableFaces) {
	// Cube indexé en 12 triangles (comme render3d::Mesh::Box) : 6 faces carrées.
	const Vec3 p[8] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
	const uint32_t idx[] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
							3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
	const Model m = FromTriangles(p, idx);
	CHECK_EQ(m.FaceCount(), 6u);
	CHECK_EQ(m.EdgeCount(), 12u); // diagonales disparues
	CHECK(Watertight(m));
	CHECK_NEAR(SolidVolume(m), 1.f, 1e-5f);
}

TEST(Primitives_ezGeometryShapesAreClosedSolids) {
	Model m;
	REQUIRE(AddPyramid(m, 2.f, 3.f).IsOk());
	CHECK_NEAR(SolidVolume(m), 4.f, 1e-4f); // b²h/3
	CHECK(Watertight(m));

	Model capsule;
	REQUIRE(AddCapsule(capsule, 0.5f, 1.f, 48, 12).IsOk());
	CHECK(Watertight(capsule));
	const float expected = math::PI * 0.25f * 1.f + 4.f / 3.f * math::PI * 0.125f;
	CHECK_NEAR(SolidVolume(capsule), expected, expected * 0.03f);

	Model stairs;
	REQUIRE(AddStairs(stairs, {1.f, 1.f, 2.f}, 4).IsOk());
	CHECK(Watertight(stairs));
	// Somme des marches : 1 × (h/4 × 2/4) × (1+2+3+4) = 1.25
	CHECK_NEAR(SolidVolume(stairs), 1.25f, 1e-4f);

	Model ramp;
	REQUIRE(AddRamp(ramp, {2.f, 1.f, 3.f}).IsOk());
	CHECK_NEAR(SolidVolume(ramp), 3.f, 1e-4f);

	Model arch;
	REQUIRE(AddArch(arch, 4.f, 0.5f, 1.f, 24).IsOk());
	CHECK(Watertight(arch));
	CHECK_NEAR(SolidVolume(arch), 0.5f * math::PI * (4.f - 2.25f), 0.05f);

	Model spiral;
	REQUIRE(AddSpiralStairs(spiral, 0.2f, 1.f, 3.f, 12).IsOk());
	CHECK(SolidVolume(spiral) > 0.f);
	CHECK(AddArch(arch, 1.f, 0.6f, 1.f).IsError());
}

TEST(Primitives_PlacementAndMaterial) {
	Model m;
	const uint32_t red = m.AddMaterial("Rouge", {1, 0, 0});
	REQUIRE(AddPyramid(m, 1.f, 1.f, true, {.transform = math::FMatrix4::Translate(10, 0, 0), .material = red}).IsOk());
	CHECK_NEAR(m.Bounds().min.x, 9.5f, 1e-5f);
	for (const Face &f : m.Faces())
		if (f.alive)
			CHECK_EQ(f.material, red);
}
