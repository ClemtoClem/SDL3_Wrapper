// Smoke test : render3d::Camera::ClipObliqueNearPlane (lib/include/render3d/
// camera.hpp) — the Lengyel oblique-near-plane-clipping technique, adapted
// (not literally ported) from the NonEuclidean reference project for this
// codebase's Vulkan-convention (column-major, Z in [0,1]) math::FMatrix4.
// Entirely CPU-only, no GPU/window involved.
#define USE_TEST
#include "core/test.hpp"
#include "render3d/camera.hpp"

using namespace render3d;
using namespace math;

static constexpr float EPS = 1e-4f;

// ─────────────────────────────────────────────────────────────────────────
// 1. A point exactly on an axis-aligned portal plane lands at z_ndc ~= 0
//    (this codebase's near value) after the clip — the closed-form check
//    the plan itself calls for.
// ─────────────────────────────────────────────────────────────────────────
TEST(Portal, PlanePointsLandAtNearZndc) {
    Camera cam;
    cam.position = {0.f, 0.f, 0.f};
    cam.target = {0.f, 0.f, -1.f};
    cam.up = {0.f, 1.f, 0.f};

    FVector3 planePos{0.f, 0.f, -5.f};
    FVector3 planeNormal{0.f, 0.f, -1.f};

    FMatrix4 clipped = cam.ClipObliqueNearPlane(planePos, planeNormal);
    FMatrix4 vp = clipped * cam.ViewMatrix();

    FVector3 points[] = {{0.f, 0.f, -5.f}, {2.f, 1.f, -5.f}, {-3.f, -2.f, -5.f}};
    for (const FVector3 &p : points) {
        FVector4 clip = vp * FVector4{p, 1.f};
        float zNdc = clip.z / clip.w;
        EXPECT_TRUE(std::abs(zNdc) < EPS);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 2. The far plane is preserved (z_ndc ~= 1, both before and after) — the
//    far-corner-preservation constraint the whole derivation hinges on.
// ─────────────────────────────────────────────────────────────────────────
TEST(Portal, FarPlaneIsPreserved) {
    Camera cam;
    cam.position = {0.f, 0.f, 0.f};
    cam.target = {0.f, 0.f, -1.f};
    cam.up = {0.f, 1.f, 0.f};

    FMatrix4 origVp = cam.ViewProjectionMatrix();
    FMatrix4 clipped = cam.ClipObliqueNearPlane({0.f, 0.f, -5.f}, {0.f, 0.f, -1.f});
    FMatrix4 newVp = clipped * cam.ViewMatrix();

    FVector3 farPoint{0.f, 0.f, -cam.farPlane};
    FVector4 before = origVp * FVector4{farPoint, 1.f};
    FVector4 after = newVp * FVector4{farPoint, 1.f};
    EXPECT_TRUE(std::abs(before.z / before.w - 1.f) < EPS);
    EXPECT_TRUE(std::abs(after.z / after.w - 1.f) < EPS);
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Geometry between the camera and the portal plane is clipped away
//    (z_ndc < 0, outside Vulkan's [0,1] near/far range); geometry beyond the
//    plane stays inside the valid [0,1] range.
// ─────────────────────────────────────────────────────────────────────────
TEST(Portal, ClipsInFrontKeepsBehind) {
    Camera cam;
    cam.position = {0.f, 0.f, 0.f};
    cam.target = {0.f, 0.f, -1.f};
    cam.up = {0.f, 1.f, 0.f};

    FMatrix4 clipped = cam.ClipObliqueNearPlane({0.f, 0.f, -5.f}, {0.f, 0.f, -1.f});
    FMatrix4 vp = clipped * cam.ViewMatrix();

    FVector4 inFront = vp * FVector4{{0.f, 0.f, -2.f}, 1.f};
    FVector4 behind = vp * FVector4{{0.f, 0.f, -10.f}, 1.f};

    EXPECT_TRUE(inFront.z / inFront.w < 0.f);
    float behindNdc = behind.z / behind.w;
    EXPECT_TRUE(behindNdc > 0.f && behindNdc < 1.f);
}

// ─────────────────────────────────────────────────────────────────────────
// 4. An oblique (non-axis-aligned) plane — exercises the sign-selection
//    logic for real, not just the trivial axis-aligned case above.
// ─────────────────────────────────────────────────────────────────────────
TEST(Portal, ObliquePlanePointsLandAtNearZndc) {
    Camera cam;
    cam.position = {0.f, 0.f, 0.f};
    cam.target = {0.f, 0.f, -1.f};
    cam.up = {0.f, 1.f, 0.f};

    FVector3 planePos{0.f, 0.f, -5.f};
    FVector3 planeNormal = FVector3{0.3f, 0.2f, -1.f}.Normalize();

    FMatrix4 clipped = cam.ClipObliqueNearPlane(planePos, planeNormal);
    FMatrix4 vp = clipped * cam.ViewMatrix();

    float nx = planeNormal.x, ny = planeNormal.y, nz = planeNormal.z;
    float d = -(nx * planePos.x + ny * planePos.y + nz * planePos.z);
    FVector2 xys[] = {{0.f, 0.f}, {2.f, 1.f}, {-2.f, -1.5f}};
    for (const FVector2 &xy : xys) {
        float z = -(nx * xy.x + ny * xy.y + d) / nz;
        FVector4 clip = vp * FVector4{{xy.x, xy.y, z}, 1.f};
        EXPECT_TRUE(std::abs(clip.z / clip.w) < EPS);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 5. A non-trivial (translated + rotated) camera pose — confirms the
//    world->view plane transform composes correctly, not just for a camera
//    sitting at the origin looking down -Z.
// ─────────────────────────────────────────────────────────────────────────
TEST(Portal, WorksWithTranslatedRotatedCamera) {
    Camera cam;
    FVector3 baseEye{3.f, 1.f, 2.f};
    FMatrix4 rot = FMatrix4::RotateY(40.f * 3.14159265f / 180.f);
    FVector3 eye = rot.TransformPoint(baseEye);
    FVector3 fwd = rot.TransformDir(FVector3{0.f, 0.f, -1.f});
    cam.position = eye;
    cam.target = eye + fwd;
    cam.up = {0.f, 1.f, 0.f};

    FVector3 planePos = eye + fwd * 5.f;
    FVector3 planeNormal = -fwd;

    FMatrix4 clipped = cam.ClipObliqueNearPlane(planePos, planeNormal);
    FMatrix4 vp = clipped * cam.ViewMatrix();

    FVector4 center = vp * FVector4{planePos, 1.f};
    EXPECT_TRUE(std::abs(center.z / center.w) < EPS);

    FVector3 right = fwd.Cross(cam.up).Normalize();
    FVector3 up = right.Cross(fwd).Normalize();
    FVector3 offCenter = planePos + right * 1.5f + up * 0.7f;
    FVector4 clip = vp * FVector4{offCenter, 1.f};
    EXPECT_TRUE(std::abs(clip.z / clip.w) < EPS);

    FVector3 farPoint = eye + fwd * cam.farPlane;
    FMatrix4 origVp = cam.ViewProjectionMatrix();
    FVector4 farBefore = origVp * FVector4{farPoint, 1.f};
    FVector4 farAfter = vp * FVector4{farPoint, 1.f};
    EXPECT_TRUE(std::abs(farBefore.z / farBefore.w - 1.f) < EPS);
    EXPECT_TRUE(std::abs(farAfter.z / farAfter.w - 1.f) < EPS);
}

int main() {
    return RUN_ALL_TESTS();
}
