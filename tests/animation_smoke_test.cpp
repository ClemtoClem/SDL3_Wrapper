// Smoke test : render3d::animation.hpp (M27) — KeyframeTrack sampling
// (FVector3/Lerp and FQuaternion/Slerp) plus AnimationMixer end-to-end
// wiring (name resolution + SetPosition()/SetRotation() dispatch). Entirely
// CPU-only, no GPU/Canvas/window needed.
#define USE_TEST
#include "core/test.hpp"
#include "render3d/animation.hpp"

#include <cmath>

using namespace render3d;

// ─────────────────────────────────────────────────────────────────────────
// 1. KeyframeTrack<FVector3> — exact Lerp at endpoints and midpoint.
// ─────────────────────────────────────────────────────────────────────────
TEST(AnimationTrack, Vector3TrackSamplesLerpExactly) {
    KeyframeTrack<math::FVector3> track;
    track.targetName = "target";
    track.keyframes = {{0.f, {0.f, 0.f, 0.f}}, {1.f, {10.f, 0.f, 0.f}}};

    EXPECT_TRUE(track.Sample(0.f) == math::FVector3(0.f, 0.f, 0.f));
    EXPECT_TRUE(track.Sample(0.5f) == math::FVector3(5.f, 0.f, 0.f));
    EXPECT_TRUE(track.Sample(1.f) == math::FVector3(10.f, 0.f, 0.f));

    // Outside the track's time range: clamps to the nearest endpoint.
    EXPECT_TRUE(track.Sample(-1.f) == math::FVector3(0.f, 0.f, 0.f));
    EXPECT_TRUE(track.Sample(5.f) == math::FVector3(10.f, 0.f, 0.f));
}

// ─────────────────────────────────────────────────────────────────────────
// 2. KeyframeTrack<FQuaternion> — Slerp, verified independently: the
//    sampled-at-0.5 quaternion must rotate a known test vector by exactly
//    HALF the total angle (computed here with plain std::cos/std::sin, not
//    by calling Slerp/FromAxisAngle again — that would be circular).
// ─────────────────────────────────────────────────────────────────────────
TEST(AnimationTrack, QuaternionTrackSamplesSlerpExactly) {
    constexpr float HALF_PI = 1.57079632679f; // 90 degrees, total track rotation
    KeyframeTrack<math::FQuaternion> track;
    track.targetName = "target";
    track.keyframes = {{0.f, math::FQuaternion::Identity()},
                        {1.f, math::FQuaternion::FromAxisAngle({0.f, 0.f, 1.f}, HALF_PI)}};

    constexpr float EPS = 1e-4f;
    math::FVector3 probe{1.f, 0.f, 0.f};

    // t=0 -> identity: probe unchanged.
    math::FVector3 at0 = track.Sample(0.f).Rotate(probe);
    EXPECT_TRUE(std::abs(at0.x - 1.f) < EPS && std::abs(at0.y - 0.f) < EPS && std::abs(at0.z - 0.f) < EPS);

    // t=0.5 -> half the total rotation, i.e. 45 degrees about Z. Expected
    // rotated vector computed directly from trigonometry, independently of
    // FQuaternion::Slerp/FromAxisAngle.
    math::FVector3 at05 = track.Sample(0.5f).Rotate(probe);
    float expected45X = std::cos(HALF_PI * 0.5f);
    float expected45Y = std::sin(HALF_PI * 0.5f);
    EXPECT_TRUE(std::abs(at05.x - expected45X) < EPS);
    EXPECT_TRUE(std::abs(at05.y - expected45Y) < EPS);
    EXPECT_TRUE(std::abs(at05.z) < EPS);

    // t=1 -> full 90 degree rotation: (1,0,0) -> (0,1,0).
    math::FVector3 at1 = track.Sample(1.f).Rotate(probe);
    EXPECT_TRUE(std::abs(at1.x - 0.f) < EPS && std::abs(at1.y - 1.f) < EPS && std::abs(at1.z - 0.f) < EPS);
}

// ─────────────────────────────────────────────────────────────────────────
// 3. AnimationClip + AnimationMixer end-to-end: FindByName() resolution and
//    SetPosition()/SetRotation() dispatch, driven across several Update()
//    calls summing to a known cumulative time.
// ─────────────────────────────────────────────────────────────────────────
TEST(AnimationMixer, UpdatesNamedObject3DAcrossClipPlayback) {
    Object3D root;
    auto childOwned = std::make_unique<Object3D>();
    childOwned->SetName(String("target"));
    Object3D &child = root.Add(std::move(childOwned));

    constexpr float HALF_PI = 1.57079632679f;

    AnimationClip clip;
    clip.name = "clip";

    KeyframeTrack<math::FVector3> positionTrack;
    positionTrack.targetName = "target";
    positionTrack.keyframes = {{0.f, {0.f, 0.f, 0.f}}, {1.f, {10.f, 0.f, 0.f}}};
    clip.positionTracks.push_back(positionTrack);

    KeyframeTrack<math::FQuaternion> rotationTrack;
    rotationTrack.targetName = "target";
    rotationTrack.keyframes = {{0.f, math::FQuaternion::Identity()},
                                {1.f, math::FQuaternion::FromAxisAngle({0.f, 0.f, 1.f}, HALF_PI)}};
    clip.rotationTracks.push_back(rotationTrack);

    AnimationMixer mixer(root);
    mixer.Play(clip, /*loop=*/false);

    // Two Update() calls summing to a cumulative time of 0.5 (both 0.25
    // steps are exactly representable in float, so the sum is exact).
    mixer.Update(0.25f);
    mixer.Update(0.25f);

    EXPECT_TRUE(child.Position() == math::FVector3(5.f, 0.f, 0.f));

    constexpr float EPS = 1e-4f;
    math::FVector3 rotated = child.Rotation().Rotate({1.f, 0.f, 0.f});
    float expected45X = std::cos(HALF_PI * 0.5f);
    float expected45Y = std::sin(HALF_PI * 0.5f);
    EXPECT_TRUE(std::abs(rotated.x - expected45X) < EPS);
    EXPECT_TRUE(std::abs(rotated.y - expected45Y) < EPS);
    EXPECT_TRUE(std::abs(rotated.z) < EPS);

    // A third Update() brings cumulative time to 1.0 (clip is non-looping,
    // so it clamps there): fully at the last keyframe.
    mixer.Update(0.5f);
    EXPECT_TRUE(child.Position() == math::FVector3(10.f, 0.f, 0.f));
    math::FVector3 rotatedFull = child.Rotation().Rotate({1.f, 0.f, 0.f});
    EXPECT_TRUE(std::abs(rotatedFull.x - 0.f) < EPS && std::abs(rotatedFull.y - 1.f) < EPS &&
                std::abs(rotatedFull.z) < EPS);
}

int main() { return RUN_ALL_TESTS(); }
