// Smoke test : physics:: (lib/include/physics/) — shapes, broad-phase,
// narrow-phase manifolds, sequential-impulse solver with warm starting, and
// joints. Entirely CPU-only, no GPU/rendering involved.
#define USE_TEST
#include "core/test.hpp"
#include "ecs/ecs.hpp"
#include "physics/world.hpp"

using namespace physics;

// ─────────────────────────────────────────────────────────────────────────
// 1. Two spheres under gravity settle to their combined-radii separation.
// ─────────────────────────────────────────────────────────────────────────
TEST(PhysicsGravity, TwoSpheresSettleAtCombinedRadii) {
    ecs::ArchetypeRegistry registry;
    World world(registry);

    ecs::Entity ground = registry.SpawnBundle(RigidBody::MakeStatic(Sphere{{}, 1.f}, {0.f, 0.f, 0.f}));
    ecs::Entity ball = registry.SpawnBundle(RigidBody::MakeDynamic(Sphere{{}, 1.f}, {0.f, 5.f, 0.f}, /*mass*/ 1.f,
                                                                    /*restitution*/ 0.f, /*friction*/ 0.3f));

    constexpr float DT = 1.f / 60.f;
    for (int i = 0; i < 300; ++i)
        world.Step(DT);

    RigidBody &groundRb = registry.GetComponent<RigidBody>(ground).Value();
    RigidBody &ballRb = registry.GetComponent<RigidBody>(ball).Value();

    float separation = (ballRb.position - groundRb.position).Length();
    EXPECT_TRUE(std::abs(separation - 2.f) < 0.05f); // radiusA + radiusB = 2.0
    EXPECT_TRUE(ballRb.linearVelocity.Length() < 0.1f);
    // No lateral drift — the ball fell straight down.
    EXPECT_TRUE(std::abs(ballRb.position.x) < 1e-3f && std::abs(ballRb.position.z) < 1e-3f);
}

// ─────────────────────────────────────────────────────────────────────────
// 2. A box resting on a static ground box stays at rest over many steps —
//    the test that validates the SOLVER's long-run stability, not just a
//    single impulse resolution.
// ─────────────────────────────────────────────────────────────────────────
TEST(PhysicsGravity, BoxRestsStablyOnGround) {
    ecs::ArchetypeRegistry registry;
    World world(registry);

    ecs::Entity groundEnt =
        registry.SpawnBundle(RigidBody::MakeStatic(Box{{}, {5.f, 0.5f, 5.f}}, {0.f, 0.f, 0.f}));
    ecs::Entity boxEnt = registry.SpawnBundle(RigidBody::MakeDynamic(Box{{}, {0.5f, 0.5f, 0.5f}}, {0.f, 3.f, 0.f},
                                                                      /*mass*/ 1.f, /*restitution*/ 0.f,
                                                                      /*friction*/ 0.5f));

    constexpr float DT = 1.f / 60.f;
    for (int i = 0; i < 300; ++i)
        world.Step(DT);

    RigidBody &box = registry.GetComponent<RigidBody>(boxEnt).Value();

    // Resting height: ground top (0.5) + box half-height (0.5).
    EXPECT_TRUE(std::abs(box.position.y - 1.f) < 0.05f);
    EXPECT_TRUE(std::abs(box.position.x) < 0.05f && std::abs(box.position.z) < 0.05f);
    EXPECT_TRUE(box.linearVelocity.Length() < 0.1f);

    // Orientation must stay near identity — no tipping over 300 steps.
    float angleDeviation = 2.f * sdl3::Acos(sdl3::Clamp(std::abs(box.orientation.w), -1.f, 1.f));
    EXPECT_TRUE(angleDeviation < 0.05f);

    // Run another 100 steps: the box should stay put (not sink further, not jitter).
    float heightBefore = box.position.y;
    for (int i = 0; i < 100; ++i)
        world.Step(DT);
    EXPECT_TRUE(std::abs(box.position.y - heightBefore) < 0.02f);

    (void)groundEnt;
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Known-velocity elastic sphere-sphere collision (equal masses,
//    restitution = 1.0) matches the textbook closed-form result: for equal
//    masses, velocities exchange exactly. Momentum AND kinetic energy must
//    both be conserved. Zero gravity, near-zero initial penetration (well
//    under the solver's slop) so Baumgarte contributes nothing and the
//    result isolates pure restitution physics.
// ─────────────────────────────────────────────────────────────────────────
TEST(PhysicsCollision, ElasticSphereSphereConservesMomentumAndEnergy) {
    ecs::ArchetypeRegistry registry;
    World world(registry);
    world.config.gravity = {0.f, 0.f, 0.f};

    RigidBody rbA = RigidBody::MakeDynamic(Sphere{{}, 1.f}, {-0.999f, 0.f, 0.f}, 1.f, /*restitution*/ 1.f,
                                            /*friction*/ 0.f);
    RigidBody rbB = RigidBody::MakeDynamic(Sphere{{}, 1.f}, {0.999f, 0.f, 0.f}, 1.f, /*restitution*/ 1.f,
                                            /*friction*/ 0.f);
    rbA.linearVelocity = {2.f, 0.f, 0.f};
    rbB.linearVelocity = {-2.f, 0.f, 0.f};

    ecs::Entity a = registry.SpawnBundle(rbA);
    ecs::Entity b = registry.SpawnBundle(rbB);

    float massA = 1.f, massB = 1.f;
    math::FVector3 vA0 = rbA.linearVelocity, vB0 = rbB.linearVelocity;
    float momentum0 = massA * vA0.x + massB * vB0.x;
    float ke0 = 0.5f * massA * vA0.LengthSq() + 0.5f * massB * vB0.LengthSq();

    world.Step(1.f / 60.f);

    RigidBody &finalA = registry.GetComponent<RigidBody>(a).Value();
    RigidBody &finalB = registry.GetComponent<RigidBody>(b).Value();

    // Equal-mass elastic collision: velocities exchange exactly.
    EXPECT_TRUE(std::abs(finalA.linearVelocity.x - (-2.f)) < 1e-2f);
    EXPECT_TRUE(std::abs(finalB.linearVelocity.x - (2.f)) < 1e-2f);
    EXPECT_TRUE(std::abs(finalA.linearVelocity.y) < 1e-3f && std::abs(finalA.linearVelocity.z) < 1e-3f);

    float momentum1 = massA * finalA.linearVelocity.x + massB * finalB.linearVelocity.x;
    float ke1 = 0.5f * massA * finalA.linearVelocity.LengthSq() + 0.5f * massB * finalB.linearVelocity.LengthSq();

    EXPECT_TRUE(std::abs(momentum1 - momentum0) < 1e-2f);
    EXPECT_TRUE(std::abs(ke1 - ke0) < 1e-2f);
}

// ─────────────────────────────────────────────────────────────────────────
// 4. A hinge joint constrains relative rotation to stay about its
//    configured axis: body B is given a perturbing angular velocity with
//    components off the hinge axis, and after several steps the hinge axis
//    (as carried by B's orientation) must still point close to its original
//    world direction — i.e. B has only spun about the axis, not swung away
//    from it.
// ─────────────────────────────────────────────────────────────────────────
TEST(PhysicsJoints, HingeConstrainsRotationToAxis) {
    ecs::ArchetypeRegistry registry;
    World world(registry);
    world.config.gravity = {0.f, 0.f, 0.f};

    RigidBody pivotRb = RigidBody::MakeStatic(Sphere{{}, 0.3f}, {0.f, 0.f, 0.f});
    RigidBody armRb = RigidBody::MakeDynamic(Sphere{{}, 0.3f}, {2.f, 0.f, 0.f}, 1.f, 0.f, 0.f);
    armRb.angularVelocity = {2.f, 1.f, 2.f}; // deliberately off-axis perturbation

    ecs::Entity pivot = registry.SpawnBundle(pivotRb);
    ecs::Entity arm = registry.SpawnBundle(armRb);

    HingeJoint hinge;
    hinge.a = pivot;
    hinge.b = arm;
    hinge.anchorLocalA = {0.f, 0.f, 0.f};
    hinge.anchorLocalB = {-2.f, 0.f, 0.f}; // so the world anchor coincides with `pivot`'s position
    hinge.axisLocalA = {0.f, 1.f, 0.f};
    hinge.axisLocalB = {0.f, 1.f, 0.f};
    registry.SpawnBundle(hinge);

    constexpr float DT = 1.f / 60.f;
    for (int i = 0; i < 90; ++i)
        world.Step(DT);

    RigidBody &finalArm = registry.GetComponent<RigidBody>(arm).Value();
    math::FVector3 axisWorld = finalArm.orientation.Rotate({0.f, 1.f, 0.f});

    // The hinge axis itself is unaffected by spin about that same axis, so if
    // the joint held, axisWorld should still be close to world +Y — even
    // though B has been free-spinning about it the whole time.
    EXPECT_TRUE((axisWorld - math::FVector3{0.f, 1.f, 0.f}).Length() < 0.15f);

    // The anchor point should still coincide (point constraint held).
    math::FVector3 anchorWorld = finalArm.position + finalArm.orientation.Rotate(hinge.anchorLocalB);
    EXPECT_TRUE(anchorWorld.Length() < 0.1f);
}

// ─────────────────────────────────────────────────────────────────────────
// 5. (nice-to-have) Warm starting isn't a dead code path: after a couple of
//    resting-contact steps, the persistent impulse cache is non-empty and
//    holds non-trivial accumulated impulses (not just zeros).
// ─────────────────────────────────────────────────────────────────────────
TEST(PhysicsSolver, WarmStartCacheIsPopulated) {
    ecs::ArchetypeRegistry registry;
    World world(registry);

    registry.SpawnBundle(RigidBody::MakeStatic(Sphere{{}, 1.f}, {0.f, 0.f, 0.f}));
    registry.SpawnBundle(RigidBody::MakeDynamic(Sphere{{}, 1.f}, {0.f, 1.9f, 0.f}, 1.f, 0.f, 0.3f));

    constexpr float DT = 1.f / 60.f;
    for (int i = 0; i < 10; ++i)
        world.Step(DT);

    const WarmStartCache &cache = world.GetWarmStartCache();
    EXPECT_TRUE(!cache.empty());

    bool foundNonTrivial = false;
    for (const auto &[key, impulse] : cache) {
        (void)key;
        if (impulse.normal > 0.01f)
            foundNonTrivial = true;
    }
    EXPECT_TRUE(foundNonTrivial);
}

// ─────────────────────────────────────────────────────────────────────────
// 6. (M30, Phase 9) Portal teleport, end-to-end through World::Step: a
//    dynamic body moving in a straight line (zero gravity, no collisions ->
//    EXACT linear motion, no discretization error) crosses a registered
//    PortalPlane with a translation-only delta. The step at which the
//    crossing happens is discovered by the loop itself (not hand-picked in
//    advance), then the expected post-teleport position is computed from
//    the closed-form straight-line motion independently of the World::Step
//    code path under test. Confirms: position gets exactly `delta`'s
//    translation added on top of the crossing-step's already-integrated
//    (pre-teleport) position; velocity/orientation are UNCHANGED (a pure
//    translation's TransformDir ignores the translation column entirely).
// ─────────────────────────────────────────────────────────────────────────
TEST(PhysicsPortal, StraightLineBodyTeleportsAcrossPlaneWithTranslationOnlyDelta) {
    ecs::ArchetypeRegistry registry;
    World world(registry);
    world.config.gravity = {0.f, 0.f, 0.f};

    RigidBody rb = RigidBody::MakeDynamic(Sphere{{}, 0.1f}, {0.f, 0.f, -2.f}, 1.f, /*restitution*/ 0.f,
                                           /*friction*/ 0.f);
    rb.linearVelocity = {0.f, 0.f, 5.5f};
    ecs::Entity e = registry.SpawnBundle(rb);

    PortalPlane plane;
    plane.position = {0.f, 0.f, 0.f};
    plane.normal = {0.f, 0.f, -1.f}; // front side: z < 0 -- where the body starts
    plane.delta = math::FMatrix4::Translate(100.f, 0.f, 0.f);
    plane.deltaInv = math::FMatrix4::Translate(-100.f, 0.f, 0.f);
    world.portalPlanes.push_back(plane);

    constexpr float DT = 1.f / 60.f;
    int crossingStep = -1;
    for (int n = 1; n <= 200; ++n) {
        world.Step(DT);
        RigidBody &cur = registry.GetComponent<RigidBody>(e).Value();
        if (cur.position.x > 50.f) {
            crossingStep = n;
            break;
        }
    }
    ASSERT_TRUE(crossingStep > 0);

    RigidBody &final_ = registry.GetComponent<RigidBody>(e).Value();

    // Closed-form: with zero gravity/collisions, position advances exactly
    // linearly every step, so the pre-teleport z on the crossing step is
    // known exactly from the initial z, velocity, dt, and step count.
    float expectedPreTeleportZ = -2.f + 5.5f * DT * float(crossingStep);
    math::FVector3 expectedPos{100.f, 0.f, expectedPreTeleportZ};
    EXPECT_TRUE((final_.position - expectedPos).Length() < 1e-3f);

    EXPECT_TRUE((final_.linearVelocity - math::FVector3{0.f, 0.f, 5.5f}).Length() < 1e-3f);
    EXPECT_TRUE(std::abs(final_.orientation.w - 1.f) < 1e-3f); // orientation untouched (identity)
}

// ─────────────────────────────────────────────────────────────────────────
// 7. (M30) TeleportBodiesThroughPortals called directly (not through a full
//    simulation loop) with a delta that has BOTH a rotation (90 deg about
//    Y) and a translation — confirms position/velocity/angularVelocity all
//    get `delta`/`TransformDir` applied correctly, AND exercises
//    portal_detail::QuaternionFromRotationMatrix end-to-end: the expected
//    orientation is computed independently via FQuaternion::Rotate()
//    directly (not by reasoning about QuaternionFromRotationMatrix's own
//    internals), comparing rotated test vectors rather than raw quaternion
//    components (sidesteps the harmless q-vs-(-q) sign ambiguity).
// ─────────────────────────────────────────────────────────────────────────
TEST(PhysicsPortal, TeleportAppliesRotationAndTranslationToPositionVelocityOrientation) {
    RigidBody rb = RigidBody::MakeDynamic(Sphere{{}, 0.3f}, {0.f, 0.f, -1.5f}, 1.f);
    rb.linearVelocity = {0.f, 0.f, 2.f};
    rb.angularVelocity = {1.f, 0.f, 0.f};
    // rb.orientation left at its default (Identity()).

    math::FVector3 prevPosition{0.f, 0.f, -1.5f};
    rb.position = {0.f, 0.f, 0.5f}; // this step's already-integrated (pre-teleport) position

    math::FQuaternion rotQ = math::FQuaternion::FromAxisAngle({0.f, 1.f, 0.f}, sdl3::PI_F * 0.5f);
    math::FMatrix4 delta = math::FMatrix4::Translate(10.f, 0.f, 0.f) * rotQ.ToMat4();

    PortalPlane plane;
    plane.position = {0.f, 0.f, 0.f};
    plane.normal = {0.f, 0.f, -1.f};
    plane.delta = delta;
    plane.deltaInv = delta.Inverse();

    std::vector<PortalPlane> planes{plane};
    TeleportBodiesThroughPortals(rb, prevPosition, planes);

    math::FVector3 expectedPos = delta.TransformPoint({0.f, 0.f, 0.5f});
    EXPECT_TRUE((rb.position - expectedPos).Length() < 1e-4f);

    math::FVector3 expectedVel = delta.TransformDir({0.f, 0.f, 2.f});
    EXPECT_TRUE((rb.linearVelocity - expectedVel).Length() < 1e-4f);

    math::FVector3 expectedAngVel = delta.TransformDir({1.f, 0.f, 0.f});
    EXPECT_TRUE((rb.angularVelocity - expectedAngVel).Length() < 1e-4f);

    math::FVector3 testVec{0.3f, 0.7f, -0.4f};
    math::FVector3 expectedRotated = rotQ.Rotate(testVec); // independent of QuaternionFromRotationMatrix
    math::FVector3 actualRotated = rb.orientation.Rotate(testVec);
    EXPECT_TRUE((expectedRotated - actualRotated).Length() < 1e-4f);
}

int main() {
    return RUN_ALL_TESTS();
}
