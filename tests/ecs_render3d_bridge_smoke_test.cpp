// Smoke test : render3d::ecs_bridge (lib/include/render3d/ecs_bridge.hpp) —
// SceneSyncSystem syncing ECS SceneTransform/SceneParent components onto an
// existing Object3D scene graph. Entirely CPU-only, no GPU/Canvas/window
// involved (Object3D/ecs:: are pure CPU structures).
#define USE_TEST
#include "core/test.hpp"
#include "ecs/ecs.hpp"
#include "render3d/ecs_bridge.hpp"
#include "render3d/object3d.hpp"

#include <cmath>
#include <memory>

using namespace render3d;

// ─────────────────────────────────────────────────────────────────────────
// 1. A SceneTransform drives an Object3D's world pose every Sync() call —
//    checked via a known point transformed through WorldMatrix(), not raw
//    field peeking, and re-checked after mutating the transform to prove
//    it's live, not just applied at spawn time.
// ─────────────────────────────────────────────────────────────────────────
TEST(Render3dEcsBridge, TransformSyncDrivesObject3DEveryCall) {
    ecs::ArchetypeRegistry registry;

    Object3D sceneRoot;
    Object3D &child = sceneRoot.Add(std::make_unique<Object3D>());

    math::FQuaternion rot90Y = math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, 3.14159265f * 0.5f);
    ecs::Entity e = registry.SpawnBundle(SceneNode{&child}, SceneTransform{{2.f, 0.f, 0.f}, rot90Y, {1.f, 1.f, 1.f}});

    SceneSyncSystem::Sync(registry, sceneRoot);

    // Point at local origin, transformed by child's WorldMatrix, should land
    // at the SceneTransform's position (scene root is identity).
    math::FVector3 worldOrigin = child.WorldMatrix().TransformPoint({0.f, 0.f, 0.f});
    EXPECT_TRUE(std::abs(worldOrigin.x - 2.f) < 1e-4f);
    EXPECT_TRUE(std::abs(worldOrigin.y - 0.f) < 1e-4f);
    EXPECT_TRUE(std::abs(worldOrigin.z - 0.f) < 1e-4f);

    // Rotation is also applied: a local +X point should be rotated 90° about
    // Y, then translated.
    math::FVector3 worldX = child.WorldMatrix().TransformPoint({1.f, 0.f, 0.f});
    math::FVector3 expectedX = rot90Y.Rotate({1.f, 0.f, 0.f}) + math::FVector3{2.f, 0.f, 0.f};
    EXPECT_TRUE((worldX - expectedX).Length() < 1e-4f);

    // Mutate the ECS component and re-Sync: the Object3D must follow.
    registry.GetComponent<SceneTransform>(e).Value()->position = math::FVector3{0.f, 5.f, 0.f};
    registry.GetComponent<SceneTransform>(e).Value()->rotation = math::FQuaternion::Identity();
    registry.GetComponent<SceneTransform>(e).Value()->scale = math::FVector3{2.f, 2.f, 2.f};

    SceneSyncSystem::Sync(registry, sceneRoot);

    math::FVector3 movedOrigin = child.WorldMatrix().TransformPoint({0.f, 0.f, 0.f});
    EXPECT_TRUE(std::abs(movedOrigin.x - 0.f) < 1e-4f);
    EXPECT_TRUE(std::abs(movedOrigin.y - 5.f) < 1e-4f);
    EXPECT_TRUE(std::abs(movedOrigin.z - 0.f) < 1e-4f);

    // Scale of 2 should double the extent of a local +X point.
    math::FVector3 scaledX = child.WorldMatrix().TransformPoint({1.f, 0.f, 0.f});
    EXPECT_TRUE(std::abs(scaledX.x - 2.f) < 1e-4f);
    EXPECT_TRUE(std::abs(scaledX.y - 5.f) < 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────
// 2. A null SceneNode::node is skipped silently (best-effort, out-of-scope
//    dangling-pointer detection) — Sync() must not crash.
// ─────────────────────────────────────────────────────────────────────────
TEST(Render3dEcsBridge, NullSceneNodeIsSkippedSafely) {
    ecs::ArchetypeRegistry registry;
    Object3D sceneRoot;

    registry.SpawnBundle(SceneNode{nullptr}, SceneTransform{{1.f, 2.f, 3.f}});
    SceneSyncSystem::Sync(registry, sceneRoot); // must not crash
    EXPECT_TRUE(sceneRoot.Children().empty());
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Reparenting: two Object3D nodes both start directly under sceneRoot
//    (as a real app would build them). SetSceneParent() declares in the ECS
//    that one should become the other's child; Sync() must actually move
//    the owning unique_ptr (not duplicate it), and the child's world pose
//    must then compose through the new parent's transform.
// ─────────────────────────────────────────────────────────────────────────
TEST(Render3dEcsBridge, ReparentMovesOwnershipAndAffectsWorldMatrix) {
    ecs::ArchetypeRegistry registry;
    Object3D sceneRoot;

    Object3D &parentObj = sceneRoot.Add(std::make_unique<Object3D>());
    Object3D &childObj = sceneRoot.Add(std::make_unique<Object3D>());

    ecs::Entity parentEntity = registry.SpawnBundle(SceneNode{&parentObj}, SceneTransform{});
    ecs::Entity childEntity = registry.SpawnBundle(SceneNode{&childObj}, SceneTransform{{1.f, 0.f, 0.f}});

    // Before Sync(): both still direct children of sceneRoot.
    EXPECT_TRUE(sceneRoot.Children().size() == 2);

    SetSceneParent(registry, childEntity, parentEntity);
    SceneSyncSystem::Sync(registry, sceneRoot);

    EXPECT_TRUE(childObj.Parent() == &parentObj);

    // Ownership genuinely transferred: sceneRoot now holds only parentObj,
    // parentObj holds childObj.
    EXPECT_TRUE(sceneRoot.Children().size() == 1);
    EXPECT_TRUE(sceneRoot.Children()[0].get() == &parentObj);
    EXPECT_TRUE(parentObj.Children().size() == 1);
    EXPECT_TRUE(parentObj.Children()[0].get() == &childObj);

    // World pose composes through the parent: give the parent a non-identity
    // transform, re-Sync, and confirm the child's WorldMatrix reflects it.
    registry.GetComponent<SceneTransform>(parentEntity).Value()->position = math::FVector3{10.f, 0.f, 0.f};
    SceneSyncSystem::Sync(registry, sceneRoot);

    math::FVector3 childWorldOrigin = childObj.WorldMatrix().TransformPoint({0.f, 0.f, 0.f});
    // childObj's own local position is {1,0,0} (its SceneTransform), plus
    // the parent's now {10,0,0} -> world {11,0,0}.
    EXPECT_TRUE(std::abs(childWorldOrigin.x - 11.f) < 1e-4f);
    EXPECT_TRUE(std::abs(childWorldOrigin.y - 0.f) < 1e-4f);
    EXPECT_TRUE(std::abs(childWorldOrigin.z - 0.f) < 1e-4f);
}

int main() {
    return RUN_ALL_TESTS();
}
