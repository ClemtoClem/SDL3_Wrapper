#include "test.hpp"
#include <math/math.hpp>

using namespace math;

TEST(MathVector3_DotCrossNormalize) {
    FVector3 x{1.f, 0.f, 0.f};
    FVector3 y{0.f, 1.f, 0.f};

    CHECK(x.Dot(y) == 0.f);
    CHECK(x.Dot(x) == 1.f);

    FVector3 z = x.Cross(y);
    CHECK(z.x == 0.f);
    CHECK(z.y == 0.f);
    CHECK(z.z == 1.f);

    FVector3 v{3.f, 4.f, 0.f};
    CHECK(v.Length() == 5.f);
    FVector3 n = v.Normalize();
    CHECK(std::abs(n.Length() - 1.f) < 1e-5f);
}

TEST(MathMatrix4_PerspectiveLookAtRoundTrip) {
    FMatrix4 view = FMatrix4::LookAt({0.f, 0.f, -5.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    FMatrix4 proj = FMatrix4::Perspective(60.f * (3.14159265f / 180.f), 16.f / 9.f, 0.1f, 100.f);
    FMatrix4 viewProj = proj * view;

    // Un point au centre de la scène doit se projeter près du centre du NDC (x,y ~ 0),
    // avec une profondeur dans [0, 1] (convention Vulkan visée par ce module).
    FVector3 clip = viewProj.TransformPoint({0.f, 0.f, 0.f});
    CHECK(std::abs(clip.x) < 1e-4f);
    CHECK(std::abs(clip.y) < 1e-4f);
    CHECK(clip.z >= 0.f && clip.z <= 1.f);

    FMatrix4 identity = view * view.Inverse();
    for (int i = 0; i < 16; ++i)
        CHECK(std::abs(identity.m[i] - FMatrix4::Identity().m[i]) < 1e-3f);
}

TEST(MathQuaternion_SlerpAndRotate) {
    FQuaternion identity = FQuaternion::Identity();
    FQuaternion halfTurnY = FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, 3.14159265f);

    FQuaternion mid = identity.Slerp(halfTurnY, 0.5f);
    FVector3 rotated = mid.Rotate({0.f, 0.f, 1.f});
    // À mi-chemin d'un demi-tour autour de Y, (0,0,1) doit pointer vers (1,0,0) ou (-1,0,0).
    CHECK(std::abs(std::abs(rotated.x) - 1.f) < 1e-3f);
    CHECK(std::abs(rotated.z) < 1e-3f);

    FVector3 unrotated = halfTurnY.Rotate({1.f, 0.f, 0.f});
    CHECK(std::abs(unrotated.x - (-1.f)) < 1e-3f);
}

TEST(MathRay_IntersectsAABBPlaneTriangle) {
    FAABB box{{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
    CHECK(box.Contains({0.f, 0.f, 0.f}));
    CHECK(box.IsValid());

    FRay ray({0.f, 0.f, -5.f}, {0.f, 0.f, 1.f});
    float tMin = 0.f, tMax = 0.f;
    CHECK(ray.Intersects(box, tMin, tMax));
    CHECK(tMin > 0.f && tMin < tMax);

    FPlane plane({0.f, 0.f, -1.f}, {0.f, 0.f, 0.f});
    float t = 0.f;
    CHECK(ray.Intersects(plane, t));
    CHECK(std::abs(t - 5.f) < 1e-3f);

    FVector3 v0{-1.f, -1.f, 0.f}, v1{1.f, -1.f, 0.f}, v2{0.f, 1.f, 0.f};
    float triT = 0.f, u = 0.f, v = 0.f;
    CHECK(ray.Intersects(v0, v1, v2, triT, u, v));
    CHECK(std::abs(triT - 5.f) < 1e-3f);

    FAABB missBox{{10.f, 10.f, 10.f}, {11.f, 11.f, 11.f}};
    CHECK(!ray.Intersects(missBox, tMin, tMax));
}

TEST(MathFrustum_CullsOutsideBoxes) {
    FMatrix4 view = FMatrix4::LookAt({0.f, 0.f, -5.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    FMatrix4 proj = FMatrix4::Perspective(60.f * (3.14159265f / 180.f), 1.f, 0.1f, 100.f);
    FFrustum frustum = FFrustum::FromViewProj(proj * view);

    FAABB inside{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    CHECK(frustum.Intersects(inside));

    FAABB behindCamera{{-0.5f, -0.5f, -100.f}, {0.5f, 0.5f, -90.f}};
    CHECK(!frustum.Intersects(behindCamera));
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
    return math::Abs(math::Abs(dot) - 1.f) < tolerance;
}

constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;

} // namespace

TEST(MathQuaternion_ToEulerRoundTripsThroughFromEuler) {
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
                CHECK(SameRotation(original, rebuilt));
            }
        }
    }
}

TEST(MathQuaternion_ToEulerRecoversTheOriginalAnglesAwayFromGimbalLock) {
    // Hors singularité, les angles eux-mêmes (pas seulement la rotation)
    // doivent revenir tels quels.
    FQuaternion q = FQuaternion::FromEuler(20.f * DEG2RAD, -50.f * DEG2RAD, 35.f * DEG2RAD);
    FVector3 euler = q.ToEuler();
    CHECK(math::Abs(euler.x - 20.f * DEG2RAD) < 1e-4f);
    CHECK(math::Abs(euler.y - -50.f * DEG2RAD) < 1e-4f);
    CHECK(math::Abs(euler.z - 35.f * DEG2RAD) < 1e-4f);
}

TEST(MathQuaternion_ToEulerHandlesGimbalLockWithoutNaN) {
    // Pitch à ±90° : yaw et roll deviennent le même axe. La convention
    // documentée (roll = 0) doit malgré tout rebâtir la MÊME rotation.
    for (float pitchDeg : {90.f, -90.f}) {
        for (float yawDeg : {0.f, 60.f, -120.f}) {
            FQuaternion original = FQuaternion::FromEuler(pitchDeg * DEG2RAD, yawDeg * DEG2RAD, 25.f * DEG2RAD);
            FVector3 euler = original.ToEuler();
            CHECK(euler.x == euler.x); // pas de NaN
            CHECK(euler.z == 0.f);     // roll épinglé à 0, cf. doc
            FQuaternion rebuilt = FQuaternion::FromEuler(euler.x, euler.y, euler.z);
            CHECK(SameRotation(original, rebuilt, 1e-3f));
        }
    }
}

TEST(MathQuaternion_ToEulerOfIdentityIsZero) {
    FVector3 euler = FQuaternion::Identity().ToEuler();
    CHECK(math::Abs(euler.x) < 1e-6f);
    CHECK(math::Abs(euler.y) < 1e-6f);
    CHECK(math::Abs(euler.z) < 1e-6f);
}

TEST(MathAabb_ExpandIsValidAfterASinglePoint) {
    // Régression : `Expand` utilisait `if/else if` par axe. Le premier point
    // inséré dans une boîte par défaut (min=+1e30, max=-1e30) prenait la
    // branche `p < min` et SAUTAIT `p > max` — la boîte restait invalide, et
    // une suite de points décroissants ne mettait jamais `max` à jour.
    FAABB box;
    CHECK(!box.IsValid());

    box.Expand(FVector3{2.f, 3.f, 4.f});
    CHECK(box.IsValid());
    CHECK(box.min == FVector3(2.f, 3.f, 4.f));
    CHECK(box.max == FVector3(2.f, 3.f, 4.f));

    // Points strictement DÉCROISSANTS : c'est le cas que l'ancien code ratait.
    FAABB decreasing;
    decreasing.Expand(FVector3{5.f, 5.f, 5.f});
    decreasing.Expand(FVector3{1.f, 1.f, 1.f});
    decreasing.Expand(FVector3{-3.f, -3.f, -3.f});
    CHECK(decreasing.IsValid());
    CHECK(decreasing.min == FVector3(-3.f, -3.f, -3.f));
    CHECK(decreasing.max == FVector3(5.f, 5.f, 5.f));

    // Et dans l'autre sens, pour être sûr que rien n'a été cassé au passage.
    FAABB increasing;
    increasing.Expand(FVector3{-3.f, -3.f, -3.f});
    increasing.Expand(FVector3{5.f, 5.f, 5.f});
    CHECK(increasing.min == FVector3(-3.f, -3.f, -3.f));
    CHECK(increasing.max == FVector3(5.f, 5.f, 5.f));

    // Expand(FAABB) s'appuie sur Expand(point) : même vérification.
    FAABB merged;
    merged.Expand(FAABB{{0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}});
    merged.Expand(FAABB{{-2.f, -2.f, -2.f}, {-1.f, -1.f, -1.f}});
    CHECK(merged.min == FVector3(-2.f, -2.f, -2.f));
    CHECK(merged.max == FVector3(1.f, 1.f, 1.f));
}