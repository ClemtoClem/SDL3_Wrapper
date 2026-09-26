// Smoke test : math:: (lib/math/math.hpp) — FVector2/3/4, FMatrix4,
// FQuaternion, FAABB, FPlane, FRay, FFrustum. Aucune dépendance SDL/GPU.
#define USE_TEST
#include "core/test.hpp"
#include "math/math.hpp"

using namespace math;

TEST(MathVector3, DotCrossNormalize) {
    FVector3 x{1.f, 0.f, 0.f};
    FVector3 y{0.f, 1.f, 0.f};

    EXPECT_TRUE(x.Dot(y) == 0.f);
    EXPECT_TRUE(x.Dot(x) == 1.f);

    FVector3 z = x.Cross(y);
    EXPECT_TRUE(z.x == 0.f);
    EXPECT_TRUE(z.y == 0.f);
    EXPECT_TRUE(z.z == 1.f);

    FVector3 v{3.f, 4.f, 0.f};
    EXPECT_TRUE(v.Length() == 5.f);
    FVector3 n = v.Normalize();
    EXPECT_TRUE(std::abs(n.Length() - 1.f) < 1e-5f);
}

TEST(MathMatrix4, PerspectiveLookAtRoundTrip) {
    FMatrix4 view = FMatrix4::LookAt({0.f, 0.f, -5.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    FMatrix4 proj = FMatrix4::Perspective(60.f * (3.14159265f / 180.f), 16.f / 9.f, 0.1f, 100.f);
    FMatrix4 viewProj = proj * view;

    // Un point au centre de la scène doit se projeter près du centre du NDC (x,y ~ 0),
    // avec une profondeur dans [0, 1] (convention Vulkan visée par ce module).
    FVector3 clip = viewProj.TransformPoint({0.f, 0.f, 0.f});
    EXPECT_TRUE(std::abs(clip.x) < 1e-4f);
    EXPECT_TRUE(std::abs(clip.y) < 1e-4f);
    EXPECT_TRUE(clip.z >= 0.f && clip.z <= 1.f);

    FMatrix4 identity = view * view.Inverse();
    for (int i = 0; i < 16; ++i)
        EXPECT_TRUE(std::abs(identity.m[i] - FMatrix4::Identity().m[i]) < 1e-3f);
}

TEST(MathQuaternion, SlerpAndRotate) {
    FQuaternion identity = FQuaternion::Identity();
    FQuaternion halfTurnY = FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, 3.14159265f);

    FQuaternion mid = identity.Slerp(halfTurnY, 0.5f);
    FVector3 rotated = mid.Rotate({0.f, 0.f, 1.f});
    // À mi-chemin d'un demi-tour autour de Y, (0,0,1) doit pointer vers (1,0,0) ou (-1,0,0).
    EXPECT_TRUE(std::abs(std::abs(rotated.x) - 1.f) < 1e-3f);
    EXPECT_TRUE(std::abs(rotated.z) < 1e-3f);

    FVector3 unrotated = halfTurnY.Rotate({1.f, 0.f, 0.f});
    EXPECT_TRUE(std::abs(unrotated.x - (-1.f)) < 1e-3f);
}

TEST(MathRay, IntersectsAABBPlaneTriangle) {
    FAABB box{{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
    EXPECT_TRUE(box.Contains({0.f, 0.f, 0.f}));
    EXPECT_TRUE(box.IsValid());

    FRay ray({0.f, 0.f, -5.f}, {0.f, 0.f, 1.f});
    float tMin = 0.f, tMax = 0.f;
    EXPECT_TRUE(ray.Intersects(box, tMin, tMax));
    EXPECT_TRUE(tMin > 0.f && tMin < tMax);

    FPlane plane({0.f, 0.f, -1.f}, {0.f, 0.f, 0.f});
    float t = 0.f;
    EXPECT_TRUE(ray.Intersects(plane, t));
    EXPECT_TRUE(std::abs(t - 5.f) < 1e-3f);

    FVector3 v0{-1.f, -1.f, 0.f}, v1{1.f, -1.f, 0.f}, v2{0.f, 1.f, 0.f};
    float triT = 0.f, u = 0.f, v = 0.f;
    EXPECT_TRUE(ray.Intersects(v0, v1, v2, triT, u, v));
    EXPECT_TRUE(std::abs(triT - 5.f) < 1e-3f);

    FAABB missBox{{10.f, 10.f, 10.f}, {11.f, 11.f, 11.f}};
    EXPECT_FALSE(ray.Intersects(missBox, tMin, tMax));
}

TEST(MathFrustum, CullsOutsideBoxes) {
    FMatrix4 view = FMatrix4::LookAt({0.f, 0.f, -5.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    FMatrix4 proj = FMatrix4::Perspective(60.f * (3.14159265f / 180.f), 1.f, 0.1f, 100.f);
    FFrustum frustum = FFrustum::FromViewProj(proj * view);

    FAABB inside{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    EXPECT_TRUE(frustum.Intersects(inside));

    FAABB behindCamera{{-0.5f, -0.5f, -100.f}, {0.5f, 0.5f, -90.f}};
    EXPECT_FALSE(frustum.Intersects(behindCamera));
}

// ============================================================================
// FQuaternion::ToEuler — inverse exact de FromEuler (ajouté pour l'inspecteur
// de l'éditeur de niveau, cf. memory/project_game_editor_app.md)
// ============================================================================

namespace {

/// Deux quaternions décrivent la MÊME rotation s'ils sont égaux ou opposés
/// (q et -q) : c'est donc ça qu'on compare, pas les composantes brutes.
bool SameRotation(const FQuaternion &a, const FQuaternion &b, float tolerance = 1e-4f) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return sdl3::Abs(sdl3::Abs(dot) - 1.f) < tolerance;
}

constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;

} // namespace

TEST(MathQuaternion, ToEulerRoundTripsThroughFromEuler) {
    // Balayage : pour chaque triplet, FromEuler -> ToEuler -> FromEuler doit
    // redonner la même ROTATION (pitch reste hors du blocage de cardan ici).
    const float angles[] = {-170.f, -90.f, -45.f, -1.f, 0.f, 1.f, 30.f, 89.f, 150.f, 179.f};
    const float pitches[] = {-85.f, -40.f, 0.f, 12.f, 85.f};

    for (float pitchDeg : pitches) {
        for (float yawDeg : angles) {
            for (float rollDeg : angles) {
                FQuaternion original =
                    FQuaternion::FromEuler(pitchDeg * DEG2RAD, yawDeg * DEG2RAD, rollDeg * DEG2RAD);
                FVector3 euler = original.ToEuler();
                FQuaternion rebuilt = FQuaternion::FromEuler(euler.x, euler.y, euler.z);
                EXPECT_TRUE(SameRotation(original, rebuilt));
            }
        }
    }
}

TEST(MathQuaternion, ToEulerRecoversTheOriginalAnglesAwayFromGimbalLock) {
    // Hors singularité, les angles eux-mêmes (pas seulement la rotation)
    // doivent revenir tels quels.
    FQuaternion q = FQuaternion::FromEuler(20.f * DEG2RAD, -50.f * DEG2RAD, 35.f * DEG2RAD);
    FVector3 euler = q.ToEuler();
    EXPECT_TRUE(sdl3::Abs(euler.x - 20.f * DEG2RAD) < 1e-4f);
    EXPECT_TRUE(sdl3::Abs(euler.y - -50.f * DEG2RAD) < 1e-4f);
    EXPECT_TRUE(sdl3::Abs(euler.z - 35.f * DEG2RAD) < 1e-4f);
}

TEST(MathQuaternion, ToEulerHandlesGimbalLockWithoutNaN) {
    // Pitch à ±90° : yaw et roll deviennent le même axe. La convention
    // documentée (roll = 0) doit malgré tout rebâtir la MÊME rotation.
    for (float pitchDeg : {90.f, -90.f}) {
        for (float yawDeg : {0.f, 60.f, -120.f}) {
            FQuaternion original = FQuaternion::FromEuler(pitchDeg * DEG2RAD, yawDeg * DEG2RAD, 25.f * DEG2RAD);
            FVector3 euler = original.ToEuler();
            EXPECT_TRUE(euler.x == euler.x); // pas de NaN
            EXPECT_TRUE(euler.z == 0.f);     // roll épinglé à 0, cf. doc
            FQuaternion rebuilt = FQuaternion::FromEuler(euler.x, euler.y, euler.z);
            EXPECT_TRUE(SameRotation(original, rebuilt, 1e-3f));
        }
    }
}

TEST(MathQuaternion, ToEulerOfIdentityIsZero) {
    FVector3 euler = FQuaternion::Identity().ToEuler();
    EXPECT_TRUE(sdl3::Abs(euler.x) < 1e-6f);
    EXPECT_TRUE(sdl3::Abs(euler.y) < 1e-6f);
    EXPECT_TRUE(sdl3::Abs(euler.z) < 1e-6f);
}

TEST(MathAabb, ExpandIsValidAfterASinglePoint) {
    // Régression : `Expand` utilisait `if/else if` par axe. Le premier point
    // inséré dans une boîte par défaut (min=+1e30, max=-1e30) prenait la
    // branche `p < min` et SAUTAIT `p > max` — la boîte restait invalide, et
    // une suite de points décroissants ne mettait jamais `max` à jour.
    FAABB box;
    EXPECT_FALSE(box.IsValid());

    box.Expand(FVector3{2.f, 3.f, 4.f});
    EXPECT_TRUE(box.IsValid());
    EXPECT_TRUE(box.min == FVector3(2.f, 3.f, 4.f));
    EXPECT_TRUE(box.max == FVector3(2.f, 3.f, 4.f));

    // Points strictement DÉCROISSANTS : c'est le cas que l'ancien code ratait.
    FAABB decreasing;
    decreasing.Expand(FVector3{5.f, 5.f, 5.f});
    decreasing.Expand(FVector3{1.f, 1.f, 1.f});
    decreasing.Expand(FVector3{-3.f, -3.f, -3.f});
    EXPECT_TRUE(decreasing.IsValid());
    EXPECT_TRUE(decreasing.min == FVector3(-3.f, -3.f, -3.f));
    EXPECT_TRUE(decreasing.max == FVector3(5.f, 5.f, 5.f));

    // Et dans l'autre sens, pour être sûr que rien n'a été cassé au passage.
    FAABB increasing;
    increasing.Expand(FVector3{-3.f, -3.f, -3.f});
    increasing.Expand(FVector3{5.f, 5.f, 5.f});
    EXPECT_TRUE(increasing.min == FVector3(-3.f, -3.f, -3.f));
    EXPECT_TRUE(increasing.max == FVector3(5.f, 5.f, 5.f));

    // Expand(FAABB) s'appuie sur Expand(point) : même vérification.
    FAABB merged;
    merged.Expand(FAABB{{0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}});
    merged.Expand(FAABB{{-2.f, -2.f, -2.f}, {-1.f, -1.f, -1.f}});
    EXPECT_TRUE(merged.min == FVector3(-2.f, -2.f, -2.f));
    EXPECT_TRUE(merged.max == FVector3(1.f, 1.f, 1.f));
}

int main() {
    return RUN_ALL_TESTS();
}
