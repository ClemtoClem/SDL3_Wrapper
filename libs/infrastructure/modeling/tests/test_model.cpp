#include "test.hpp"
#include <modeling/operations.hpp>
#include <modeling/triangulate.hpp>

#include <cmath>

using namespace modeling;

namespace {

/// Volume d'un solide fermé (théorème de la divergence sur les triangles).
float Volume(const Model &m) {
	const MeshData mesh = BuildMesh(m);
	float v = 0.f;
	for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
		const Vec3 &a = mesh.positions[mesh.indices[i]], &b = mesh.positions[mesh.indices[i + 1]],
				   &c = mesh.positions[mesh.indices[i + 2]];
		v += a.Dot(b.Cross(c)) / 6.f;
	}
	return v;
}

/// Toute arête d'un solide fermé borde exactement deux faces.
bool Closed(const Model &m) {
	for (size_t e = 0; e < m.Edges().size(); ++e)
		if (m.Edges()[e].alive && m.FacesOfEdge(EdgeId(e)).size() != 2)
			return false;
	return true;
}

FaceId Ground(Model &m, float w = 2.f, float d = 1.f) {
	// Rectangle au sol, normale vers le haut.
	return m.AddFace({{0, 0, 0}, {0, 0, d}, {w, 0, d}, {w, 0, 0}}).Unwrap();
}

} // namespace

TEST(Model_FaceConstructionWeldsAndOrients) {
	Model m;
	const FaceId f = Ground(m);
	CHECK_EQ(m.VertexCount(), 4u);
	CHECK_EQ(m.EdgeCount(), 4u);
	CHECK_NEAR(m.Faces()[f].normal.y, 1.f, 1e-5f);
	CHECK_NEAR(m.FaceArea(f), 2.f, 1e-5f);
	// Même contour (autre sens, autre départ) : la face existante est rendue.
	CHECK_EQ(m.AddFace({{2, 0, 0}, {0, 0, 0}, {0, 0, 1}, {2, 0, 1}}).Unwrap(), f);
	// Non plan : refusé, sauf en triangulation automatique.
	CHECK(m.AddFace({{5, 0, 0}, {6, 0, 0}, {6, 1, 1}, {5, 0, 1}}).IsNone());
	CHECK(m.AddFace({{5, 0, 0}, {6, 0, 0}, {6, 1, 1}, {5, 0, 1}}, {}, {.autoTriangulate = true}).IsSome());
	// Face avec trou : aire déduite, trou orienté à l'inverse.
	const FaceId holed = m.AddFace({{0, 0, 5}, {4, 0, 5}, {4, 0, 9}, {0, 0, 9}},
								   {{{1, 0, 6}, {2, 0, 6}, {2, 0, 7}, {1, 0, 7}}}).Unwrap();
	CHECK_NEAR(m.FaceArea(holed), 15.f, 1e-4f);
	CHECK(!m.FaceContains(holed, {1.5f, 0, 6.5f}));
	CHECK(m.FaceContains(holed, {3.f, 0, 8.f}));
}

TEST(Model_CirclesArcsAndMaterials) {
	Model m;
	const uint32_t red = m.AddMaterial("Rouge", {1, 0, 0});
	CHECK_EQ(m.AddMaterial("Rouge", {0.9f, 0, 0}), red); // dédoublonné par nom
	const FaceId disk = m.AddCircle({0, 0, 0}, {0, 1, 0}, 1.f, {.segments = 64, .material = red}).Unwrap();
	CHECK_NEAR(m.FaceArea(disk), math::PI, 0.01f);
	CHECK_EQ(m.Faces()[disk].material, red);
	const uint32_t curve = m.Edges()[m.EdgesOfFace(disk)[0]].curve;
	CHECK(curve != INVALID_ID);
	const auto arc = m.AddArc({5, 0, 0}, {0, 1, 0}, {1, 0, 0}, 1.f, 0.f, math::PI, {.segments = 8});
	CHECK_EQ(arc.size(), 8u);
}

TEST(PushPull_FreeFaceBecomesAClosedBox) {
	Model m;
	const FaceId f = Ground(m);
	const FaceId top = PushPull(m, f, 3.f).Unwrap();
	CHECK_EQ(m.FaceCount(), 6u);
	CHECK(Closed(m));
	CHECK_NEAR(Volume(m), 6.f, 1e-4f);              // normales vers l'extérieur
	CHECK_NEAR(m.Faces()[top].normal.y, 1.f, 1e-5f); // dessus vers le haut
	CHECK_NEAR(m.Faces()[f].normal.y, -1.f, 1e-5f);  // fond retourné
}

TEST(PushPull_OnASolidMovesTheFaceBothWays) {
	Model m;
	const FaceId top = PushPull(m, Ground(m), 3.f).Unwrap();
	CHECK_EQ(PushPull(m, top, 1.f).Unwrap(), top); // tirer : la boîte grandit
	CHECK_EQ(m.FaceCount(), 6u);
	CHECK_NEAR(Volume(m), 8.f, 1e-4f);
	(void)PushPull(m, top, -2.5f);                  // pousser : elle rapetisse
	CHECK_NEAR(Volume(m), 3.f, 1e-4f);
	CHECK(Closed(m));
	CHECK(PushPull(m, top, -100.f).IsError());
}

TEST(PushPull_HalfOfATopFaceMakesAStepWithMergedSides) {
	// Boîte 2 × 1 × 1 ; une ligne coupe le dessus en x = 1 ; on tire la
	// moitié droite de 1 : les flancs avant/arrière de la marche sont
	// coplanaires avec ceux de la boîte et ne font plus qu'une face.
	Model m;
	const FaceId top = PushPull(m, Ground(m, 2.f, 1.f), 1.f).Unwrap();
	const VertexId a = m.SplitEdge(m.FindEdge(m.FindVertex({0, 1, 0}), m.FindVertex({2, 1, 0})), {1, 1, 0});
	const VertexId b = m.SplitEdge(m.FindEdge(m.FindVertex({0, 1, 1}), m.FindVertex({2, 1, 1})), {1, 1, 1});
	REQUIRE(a != INVALID_ID && b != INVALID_ID);
	const FaceId other = m.SplitFace(a, b).Unwrap();
	const FaceId right = m.FaceCentroid(top).x > 1.f ? top : other;
	CHECK(Closed(m));
	REQUIRE(PushPull(m, right, 1.f).IsOk());
	CHECK(Closed(m));
	CHECK_NEAR(Volume(m), 3.f, 1e-4f);
	// Boîte (6 faces) + dessus gauche + marche : avant et arrière en L fusionnés.
	CHECK_EQ(m.FaceCount(), 8u);
	size_t lShaped = 0;
	for (const Face &f : m.Faces())
		lShaped += f.alive && f.outer.size() == 6 ? 1 : 0;
	CHECK_EQ(lShaped, 2u);
}

TEST(SplitFace_LineAcrossAFaceCutsIt) {
	Model m;
	const FaceId f = m.AddFace({{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {2, 0, 1}, {2, 0, 0}, {1, 0, 0}}).Unwrap();
	const Option<FaceId> other = m.SplitFace(m.FindVertex({1, 0, 0}), m.FindVertex({1, 0, 1}));
	REQUIRE(other.IsSome());
	CHECK_EQ(m.FaceCount(), 2u);
	CHECK_NEAR(m.FaceArea(f) + m.FaceArea(other.Unwrap()), 2.f, 1e-5f);
	CHECK_NEAR(m.FaceArea(f), 1.f, 1e-5f);
	// Les deux moitiés, tirées ensemble, refusionnent avec MergeCoplanar.
	CHECK_EQ(m.MergeCoplanar(f), f);
	CHECK_EQ(m.FaceCount(), 1u);
	CHECK_EQ(m.Faces()[f].outer.size(), 4u); // sommets alignés cicatrisés
}

TEST(Offset_InsetAndOutset) {
	Model m;
	const FaceId f = m.AddFace({{0, 0, 0}, {0, 0, 4}, {4, 0, 4}, {4, 0, 0}}).Unwrap();
	const FaceId inner = Offset(m, f, 1.f).Unwrap();
	CHECK_NEAR(m.FaceArea(inner), 4.f, 1e-4f);
	CHECK_NEAR(m.FaceArea(f), 12.f, 1e-4f); // anneau
	CHECK(Offset(m, inner, 5.f).IsError());   // trop grand
	const FaceId ring = Offset(m, f, -1.f).Unwrap();
	CHECK_NEAR(m.FaceArea(ring), 36.f - 16.f, 1e-3f);
}

TEST(FollowMe_SweepsAProfileAlongAPath) {
	Model m;
	// Profil carré 0.2 × 0.2 dans le plan x = 0, chemin en L le long de x puis z.
	const FaceId profile = m.AddFace({{0, 0, 0}, {0, 0.2f, 0}, {0, 0.2f, 0.2f}, {0, 0, 0.2f}}).Unwrap();
	const auto end = FollowMe(m, profile, {{0, 0.1f, 0.1f}, {2, 0.1f, 0.1f}, {2, 0.1f, 2}});
	REQUIRE(end.IsOk());
	CHECK(Closed(m));
	CHECK(Volume(m) > 0.f);
	// Volume d'une barre en L à onglet : (2 + 1.9) × 0.04 environ.
	CHECK_NEAR(Volume(m), 0.04f * 3.8f, 0.01f);
}

TEST(Revolve_BuildsASmoothClosedSolid) {
	Model m;
	const auto faces = Revolve(m, {0, 0, 0}, {0, 1, 0}, {{0.5f, 0.f}, {0.5f, 1.f}}, {.segments = 48});
	REQUIRE(faces.IsOk());
	CHECK(Closed(m));
	CHECK_NEAR(Volume(m), math::PI * 0.25f, 0.01f); // cylindre r = 0.5, h = 1
	size_t soft = 0;
	for (const Edge &e : m.Edges())
		soft += e.alive && e.soft ? 1 : 0;
	CHECK_EQ(soft, 48u); // les génératrices, pas les bords des disques
	// Lissage : les normales d'un coin de flanc ne sont plus celle de la face.
	const MeshData mesh = BuildMesh(m);
	CHECK(mesh.TriangleCount() > 0u);
}

TEST(Extrude_PrismAlongANormalOrAnyDirection) {
	Model m;
	REQUIRE(Extrude(m, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {0, 2, 0}).IsOk());
	CHECK_NEAR(std::fabs(Volume(m)), 2.f, 1e-4f);
	Model slanted;
	REQUIRE(Extrude(slanted, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {1, 2, 0}).IsOk());
	CHECK_NEAR(std::fabs(Volume(slanted)), 2.f, 1e-3f); // prisme oblique : même volume
}

TEST(Transforms_MoveRotateScaleAndErase) {
	Model m;
	(void)PushPull(m, Ground(m, 1.f, 1.f), 1.f);
	std::vector<VertexId> all;
	for (size_t v = 0; v < m.Vertices().size(); ++v)
		if (m.Vertices()[v].alive)
			all.push_back(VertexId(v));
	Move(m, all, {10, 0, 0});
	CHECK_NEAR(m.Bounds().min.x, 10.f, 1e-5f);
	Scale(m, all, {10, 0, 0}, {2, 2, 2});
	CHECK_NEAR(Volume(m), 8.f, 1e-3f);
	Rotate(m, all, {10, 0, 0}, {0, 1, 0}, math::PI * 0.5f);
	CHECK_NEAR(Volume(m), 8.f, 1e-3f);
	Erase(m, {m.EdgesOfFace(0)[0]});
	CHECK_EQ(m.FaceCount(), 4u); // l'arête emporte ses deux faces
}

TEST(BuildMesh_GroupsByMaterialAndMapsTrianglesToFaces) {
	Model m;
	const uint32_t red = m.AddMaterial("Rouge", {1, 0, 0});
	const FaceId a = Ground(m);
	const FaceId b = m.AddFace({{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}).Unwrap();
	Paint(m, b, red);
	const MeshData mesh = BuildMesh(m);
	CHECK_EQ(mesh.TriangleCount(), 4u);
	CHECK_EQ(mesh.groups.size(), 2u);
	CHECK_EQ(mesh.triangleFace.size(), 4u);
	CHECK(std::find(mesh.triangleFace.begin(), mesh.triangleFace.end(), a) != mesh.triangleFace.end());
	const auto hit = Raycast(m, math::FRay({0.5f, 5, 0.5f}, {0, -1, 0}));
	REQUIRE(hit.IsSome());
	CHECK_EQ(hit.Value().face, b);
	CHECK_NEAR(hit.Value().point.y, 1.f, 1e-5f);
}
