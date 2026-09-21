// Smoke test : render3d::PickMeshFace (lib/include/render3d/picking.hpp) —
// M30 (Phase 9 du plan). Entirely CPU-only, no GPU/window involved — known
// meshes built directly (not loaded from a file), rays aimed at hand-picked
// points whose expected hit point/t/triangle/group/barycentric coordinates
// are derived independently below (not just "didn't crash").
#define USE_TEST
#include "core/test.hpp"
#include "render3d/picking.hpp"

#include <cmath>

using namespace render3d;
using namespace math;

static constexpr float EPS = 1e-4f;

// ─────────────────────────────────────────────────────────────────────────
// 1. A single quad (2 triangles, one explicit group with a known non-zero
//    materialIndex) in the local XY plane at z=0. Ray aimed at local point
//    (0.5,-0.5,0), independently verified (by hand, see the task's own
//    derivation) to lie in triangle 0 with barycentric u=0.5, v=0.25.
// ─────────────────────────────────────────────────────────────────────────
TEST(Picking, QuadHitReportsExactPointTriangleGroupAndBarycentric) {
    std::vector<Vertex3D> verts = {
        {{-1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}, sdl3::Color::WHITE()},
        {{1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}, sdl3::Color::WHITE()},
        {{1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}, sdl3::Color::WHITE()},
        {{-1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}, sdl3::Color::WHITE()},
    };
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    Mesh quad(verts, idx);
    quad.AddGroup(0, 6, /*materialIndex*/ 7);

    FRay ray{{0.5f, -0.5f, 5.f}, {0.f, 0.f, -1.f}};
    FMatrix4 world = FMatrix4::Identity();

    auto hit = PickMeshFace(ray, quad, world);
    ASSERT_TRUE(hit.IsSome());
    const PickResult &r = hit.Value();

    EXPECT_TRUE((r.point - FVector3{0.5f, -0.5f, 0.f}).Length() < EPS);
    EXPECT_TRUE(std::abs(r.t - 5.f) < EPS);
    EXPECT_TRUE(r.triangleIndex == 0);
    EXPECT_TRUE(r.groupIndex == 0);
    EXPECT_TRUE(r.materialIndex == 7);
    EXPECT_TRUE(std::abs(r.u - 0.5f) < EPS);
    EXPECT_TRUE(std::abs(r.v - 0.25f) < EPS);
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Same quad, same LOCAL hit point, but with a non-trivial world matrix
//    (uniform scale 2 + translation) — confirms `t`/`point` are correctly
//    recomputed in WORLD space (not a raw local-space `t`, which would be
//    in the wrong units under a scaling worldMatrix — see picking.hpp's own
//    doc comment on this exact point).
// ─────────────────────────────────────────────────────────────────────────
TEST(Picking, HitUnderScaledTranslatedWorldMatrixReportsWorldSpaceTAndPoint) {
    std::vector<Vertex3D> verts = {
        {{-1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}, sdl3::Color::WHITE()},
        {{1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}, sdl3::Color::WHITE()},
        {{1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}, sdl3::Color::WHITE()},
        {{-1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}, sdl3::Color::WHITE()},
    };
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    Mesh quad(verts, idx);

    // world = Translate(10,0,0) * Scale(2) : local (0.5,-0.5,0) -> world (11,-1,0).
    FMatrix4 world = FMatrix4::Translate(10.f, 0.f, 0.f) * FMatrix4::Scale(2.f);

    FRay ray{{11.f, -1.f, 10.f}, {0.f, 0.f, -1.f}};
    auto hit = PickMeshFace(ray, quad, world);
    ASSERT_TRUE(hit.IsSome());
    const PickResult &r = hit.Value();

    EXPECT_TRUE((r.point - FVector3{11.f, -1.f, 0.f}).Length() < EPS);
    EXPECT_TRUE(std::abs(r.t - 10.f) < EPS); // world-space distance, NOT the local-space t (which would be 5)
    EXPECT_TRUE(r.triangleIndex == 0);
    EXPECT_TRUE(std::abs(r.u - 0.5f) < EPS);
    EXPECT_TRUE(std::abs(r.v - 0.25f) < EPS);
}

// ─────────────────────────────────────────────────────────────────────────
// 3. A cube (6 groups, one per face, materialIndex == faceIndex per
//    Mesh::Cube()'s own doc comment) — ray aimed at the +Z face (group 4)
//    at a point independently verified to land in that face's first
//    triangle (triangle index 8 == 24/3, face 4 starts at index 24).
// ─────────────────────────────────────────────────────────────────────────
TEST(Picking, CubeHitReportsCorrectFaceGroupAndMaterialIndex) {
    Mesh cube = Mesh::Cube(2.f); // half-extent 1

    FRay ray{{0.5f, -0.5f, 5.f}, {0.f, 0.f, -1.f}};
    FMatrix4 world = FMatrix4::Identity();

    auto hit = PickMeshFace(ray, cube, world);
    ASSERT_TRUE(hit.IsSome());
    const PickResult &r = hit.Value();

    EXPECT_TRUE((r.point - FVector3{0.5f, -0.5f, 1.f}).Length() < EPS);
    EXPECT_TRUE(std::abs(r.t - 4.f) < EPS);
    EXPECT_TRUE(r.triangleIndex == 8);
    EXPECT_TRUE(r.groupIndex == 4);
    EXPECT_TRUE(r.materialIndex == 4);
}

// ─────────────────────────────────────────────────────────────────────────
// 4. A ray that misses the mesh entirely returns NONE.
// ─────────────────────────────────────────────────────────────────────────
TEST(Picking, MissingRayReturnsNone) {
    Mesh cube = Mesh::Cube(2.f);
    FRay ray{{10.f, 10.f, 5.f}, {0.f, 0.f, -1.f}}; // well outside the cube's [-1,1] XY footprint
    FMatrix4 world = FMatrix4::Identity();

    auto hit = PickMeshFace(ray, cube, world);
    EXPECT_TRUE(hit.IsNone());
}

// ─────────────────────────────────────────────────────────────────────────
// 5. Camera::ScreenPointToRay / WorldToScreen — the bridge between a mouse
//    pixel and PickMeshFace, and back. Expected values derived from the
//    camera's own geometry (not from the implementation): a camera at z=5
//    looking at the origin with a 60° vertical field of view.
// ─────────────────────────────────────────────────────────────────────────
TEST(Camera, ScreenPointToRayAimsWhereThePixelPoints) {
    Camera camera;
    camera.position = {0.f, 0.f, 5.f};
    camera.target = {0.f, 0.f, 0.f};
    camera.aspect = 2.f;

    // Centre pixel: straight down the camera's forward axis (-Z here).
    FRay centre = camera.ScreenPointToRay(400.f, 300.f, 800.f, 600.f);
    EXPECT_TRUE(std::abs(centre.direction.x - (0.f)) < EPS);
    EXPECT_TRUE(std::abs(centre.direction.y - (0.f)) < EPS);
    EXPECT_TRUE(std::abs(centre.direction.z - (-1.f)) < EPS);
    // Starts ON the near plane, not at the camera position.
    EXPECT_TRUE(std::abs(centre.origin.z - (camera.position.z - camera.nearPlane)) < 1e-3f);

    // Top edge, vertical half-angle = fovY/2 = 30° → tan 30° = 0.5774.
    FRay top = camera.ScreenPointToRay(400.f, 0.f, 800.f, 600.f);
    EXPECT_TRUE(top.direction.y > 0.f); // screen Y grows downwards, world Y upwards
    EXPECT_TRUE(std::abs(top.direction.y / -top.direction.z - (0.57735f)) < 1e-3f);

    // Right edge: same angle scaled by the aspect ratio.
    FRay right = camera.ScreenPointToRay(800.f, 300.f, 800.f, 600.f);
    EXPECT_TRUE(right.direction.x > 0.f);
    EXPECT_TRUE(std::abs(right.direction.x / -right.direction.z - (0.57735f * camera.aspect)) < 1e-3f);
}

TEST(Camera, ScreenRayAndWorldToScreenAreInverses) {
    Camera camera;
    camera.position = {3.f, 4.f, 9.f};
    camera.target = {0.f, 1.f, 0.f};
    camera.aspect = 16.f / 9.f;

    // A pixel → a ray → a point on that ray → back to the same pixel.
    const FVector2 pixel{612.f, 233.f};
    FRay ray = camera.ScreenPointToRay(pixel.x, pixel.y, 1280.f, 720.f);
    Option<FVector2> back = camera.WorldToScreen(ray.At(12.f), 1280.f, 720.f);
    ASSERT_TRUE(back.IsSome());
    EXPECT_TRUE(std::abs(back.Value().x - (pixel.x)) < 0.01f);
    EXPECT_TRUE(std::abs(back.Value().y - (pixel.y)) < 0.01f);

    // A point BEHIND the camera has no pixel — the perspective divide would
    // otherwise mirror it onto a plausible-looking but wrong one.
    FVector3 behind = camera.position + (camera.position - camera.target);
    EXPECT_TRUE(camera.WorldToScreen(behind, 1280.f, 720.f).IsNone());
}

TEST(Camera, ScreenRayPicksTheMeshUnderThePixel) {
    Camera camera;
    camera.position = {0.f, 0.f, 6.f};
    camera.target = {0.f, 0.f, 0.f};
    camera.aspect = 1.f;
    Mesh cube = Mesh::Cube(2.f);

    // Centre pixel hits the +Z face at z=1, i.e. 5 units from the camera
    // (4.9 from the near-plane ray origin).
    Option<PickResult> hit = PickMeshFace(camera.ScreenPointToRay(300.f, 300.f, 600.f, 600.f), cube,
                                          FMatrix4::Identity());
    ASSERT_TRUE(hit.IsSome());
    EXPECT_TRUE(std::abs(hit.Value().point.z - (1.f)) < EPS);
    EXPECT_TRUE(std::abs(hit.Value().t - (4.9f)) < 1e-3f);

    // A corner pixel looks past the cube entirely.
    EXPECT_TRUE(PickMeshFace(camera.ScreenPointToRay(2.f, 2.f, 600.f, 600.f), cube, FMatrix4::Identity()).IsNone());
}

TEST(Ray, DistanceToMeasuresFromTheOriginForPointsBehind) {
    FRay ray{{0.f, 0.f, 0.f}, {0.f, 0.f, -1.f}};
    // Beside the ray, 3 units along it: the perpendicular distance.
    EXPECT_TRUE(std::abs(ray.DistanceTo({2.f, 0.f, -3.f}) - (2.f)) < EPS);
    // Exactly on the ray.
    EXPECT_TRUE(std::abs(ray.DistanceTo({0.f, 0.f, -7.f}) - (0.f)) < EPS);
    // BEHIND the origin: measured from the origin (half-line, not line), so
    // a handle behind the viewer is never "under the cursor".
    EXPECT_TRUE(std::abs(ray.DistanceTo({0.f, 0.f, 4.f}) - (4.f)) < EPS);
    EXPECT_TRUE(std::abs(ray.DistanceTo({3.f, 0.f, 4.f}) - (5.f)) < EPS);
}

int main() {
    return RUN_ALL_TESTS();
}
