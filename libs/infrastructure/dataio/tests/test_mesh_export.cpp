#include "test.hpp"
#include <dataio/mesh_export.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {

// Deux triangles formant un carré unité dans le plan XY.
const float POSITIONS[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
const float NORMALS[] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
const uint32_t INDICES[] = {0, 1, 2, 0, 2, 3};

dataio::TriangleMeshView Square() {
	dataio::TriangleMeshView v;
	v.positions = POSITIONS;
	v.normals = NORMALS;
	v.indices = INDICES;
	return v;
}

} // namespace

TEST(MeshExport_ObjWithMaterials) {
	const dataio::MeshMaterialGroup groups[] = {{String("Rouge"), 1, 0, 0, 1, 0, 3}, {String("Vert"), 0, 1, 0, 1, 3, 3}};
	dataio::TriangleMeshView v = Square();
	v.materials = groups;
	const String obj = dataio::EncodeObj(v, {String("essai"), String("carre.mtl")});
	CHECK(obj.Contains("mtllib carre.mtl"));
	CHECK(obj.Contains("usemtl Rouge"));
	CHECK(obj.Contains("usemtl Vert"));
	CHECK(obj.Contains("f 1//1 2//2 3//3"));
	const String mtl = dataio::EncodeMtl(groups);
	CHECK(mtl.Contains("newmtl Vert\nKd 0.0000 1.0000 0.0000"));
}

TEST(MeshExport_StlBinaryLayoutAndAscii) {
	const std::vector<uint8_t> stl = dataio::EncodeStlBinary(Square());
	CHECK_EQ(stl.size(), size_t(84 + 2 * 50));
	uint32_t count = 0;
	std::memcpy(&count, stl.data() + 80, 4);
	CHECK_EQ(count, 2u);
	float nz = 0.f;
	std::memcpy(&nz, stl.data() + 84 + 8, 4);
	CHECK_NEAR(nz, 1.f, 1e-6f); // normale recalculée
	CHECK(dataio::EncodeStlAscii(Square()).Contains("facet normal 0 0 1"));
}

TEST(MeshExport_PlyAndSaveByExtension) {
	const String ply = dataio::EncodePly(Square());
	CHECK(ply.Contains("element vertex 4"));
	CHECK(ply.Contains("element face 2"));
	const String dir = String(std::filesystem::temp_directory_path().string().c_str());
	for (const char *ext : {".obj", ".stl", ".ply"}) {
		const String path = dir + "/dataio_mesh_export" + ext;
		CHECK(dataio::SaveMesh(path, Square()).IsNone());
		CHECK(std::filesystem::file_size(path.CStr()) > 0u);
	}
	CHECK(dataio::SaveMesh(dir + "/x.fbx", Square()).IsSome());
}
