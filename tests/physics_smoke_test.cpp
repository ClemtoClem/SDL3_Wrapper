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

// ─────────────────────────────────────────────────────────────────────────
// Capsule vs box — the collision a first-person character needs to stand on
// a floor and be stopped by a wall. It used to be a documented "no contact"
// stub: a capsule fell straight through every box.
// ─────────────────────────────────────────────────────────────────────────
namespace {
bool Near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }
constexpr float PI = 3.14159265358979323846f;
} // namespace

TEST(PhysicsCapsuleBox, AStandingCapsuleTouchesTheTopFaceWithOneDownwardContact) {
    const Box floor{{0.f, 0.f, 0.f}, {5.f, 0.5f, 5.f}};
    // Segment from y = 0.8 to 1.8, radius 0.35: bottom of the capsule at 0.45,
    // 0.05 below the top face (0.5).
    const Capsule capsule{{0.f, 1.3f, 0.f}, 0.5f, 0.35f};
    Manifold m;
    ASSERT_TRUE(CollideCapsuleBox(capsule, floor, m));
    EXPECT_TRUE(Near(m.normal.x, 0.f) && Near(m.normal.y, -1.f) && Near(m.normal.z, 0.f)); // capsule -> box
    EXPECT_EQ(m.pointCount, 1); // the upper end is far away: no extra point
    EXPECT_TRUE(Near(m.points[0].penetration, 0.05f));
    EXPECT_TRUE(Near(m.points[0].worldPoint.y, 0.45f));
}

TEST(PhysicsCapsuleBox, SeparatedShapesDoNotCollide) {
    const Box floor{{0.f, 0.f, 0.f}, {5.f, 0.5f, 5.f}};
    Manifold m;
    EXPECT_FALSE(CollideCapsuleBox(Capsule{{0.f, 1.5f, 0.f}, 0.5f, 0.35f}, floor, m)); // 0.15 above
    EXPECT_FALSE(CollideCapsuleBox(Capsule{{7.f, 0.f, 0.f}, 0.5f, 0.35f}, floor, m));  // beside
}

TEST(PhysicsCapsuleBox, ALyingCapsuleGetsContactsAtBothEnds) {
    const Box floor{{0.f, 0.f, 0.f}, {5.f, 0.5f, 5.f}};
    // Lying along X (rotated 90 degrees about Z), resting 0.02 into the face.
    const Capsule capsule{{0.f, 0.83f, 0.f}, 1.f, 0.35f,
                          math::FQuaternion::FromAxisAngle({0.f, 0.f, 1.f}, PI * 0.5f)};
    Manifold m;
    ASSERT_TRUE(CollideCapsuleBox(capsule, floor, m));
    EXPECT_TRUE(Near(m.normal.y, -1.f));
    EXPECT_TRUE(m.pointCount >= 2); // a line contact, not a pivot point
    float minX = 1e9f, maxX = -1e9f;
    for (int i = 0; i < m.pointCount; ++i) {
        minX = std::min(minX, m.points[i].worldPoint.x);
        maxX = std::max(maxX, m.points[i].worldPoint.x);
        EXPECT_TRUE(Near(m.points[i].penetration, 0.02f));
    }
    EXPECT_TRUE(Near(minX, -1.f) && Near(maxX, 1.f));
}

TEST(PhysicsCapsuleBox, ArgumentOrderOnlyFlipsTheNormal) {
    ecs::ArchetypeRegistry registry;
    const Shape box = Box{{0.f, 0.f, 0.f}, {5.f, 0.5f, 5.f}};
    const Shape capsule = Capsule{{0.f, 1.3f, 0.f}, 0.5f, 0.35f};
    Manifold capsuleFirst, boxFirst;
    ASSERT_TRUE(GenerateManifold(ecs::Entity{}, capsule, ecs::Entity{}, box, capsuleFirst));
    ASSERT_TRUE(GenerateManifold(ecs::Entity{}, box, ecs::Entity{}, capsule, boxFirst));
    EXPECT_TRUE(Near(capsuleFirst.normal.y, -1.f)); // A (capsule) -> B (box)
    EXPECT_TRUE(Near(boxFirst.normal.y, 1.f));      // A (box) -> B (capsule)
    EXPECT_TRUE(Near(capsuleFirst.points[0].penetration, boxFirst.points[0].penetration));
}

TEST(PhysicsCapsuleBox, AnInclinedFaceGivesItsOwnNormal) {
    // Box tilted 30 degrees about Z; a vertical capsule pressed on its top face.
    const math::FQuaternion tilt = math::FQuaternion::FromAxisAngle({0.f, 0.f, 1.f}, PI / 6.f);
    const Box ramp{{0.f, 0.f, 0.f}, {5.f, 0.5f, 5.f}, tilt};
    const math::FVector3 up = tilt.Rotate({0.f, 1.f, 0.f});
    // Centre of the capsule's LOWER sphere placed 0.3 above the face along
    // its normal (radius 0.35 : 0.05 of penetration).
    const math::FVector3 lowerSphere = up * (0.5f + 0.3f);
    const Capsule capsule{lowerSphere + math::FVector3{0.f, 0.5f, 0.f}, 0.5f, 0.35f};
    Manifold m;
    ASSERT_TRUE(CollideCapsuleBox(capsule, ramp, m));
    EXPECT_TRUE(Near(m.normal.x, -up.x, 1e-2f) && Near(m.normal.y, -up.y, 1e-2f));
    EXPECT_TRUE(Near(m.points[0].penetration, 0.05f, 1e-2f));
}

TEST(PhysicsCapsuleBox, ADeeplyEmbeddedCapsuleIsStillPushedOut) {
    const Box block{{0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}};
    const Capsule capsule{{0.f, 0.7f, 0.f}, 0.2f, 0.3f}; // segment inside the block
    Manifold m;
    ASSERT_TRUE(CollideCapsuleBox(capsule, block, m));
    EXPECT_TRUE(m.points[0].penetration > 0.3f); // deeper than the radius alone
    EXPECT_TRUE(Near(m.normal.y, -1.f));          // out through the nearest (top) face
}

TEST(PhysicsCapsuleBox, ADynamicCapsuleRestsOnTheGroundAndIsStoppedByAWall) {
    ecs::ArchetypeRegistry registry;
    World world(registry);
    (void)registry.SpawnBundle(RigidBody::MakeStatic(Box{{}, {10.f, 0.5f, 10.f}}, {0.f, 0.f, 0.f}));
    (void)registry.SpawnBundle(RigidBody::MakeStatic(Box{{}, {0.25f, 2.f, 10.f}}, {3.f, 2.f, 0.f}));
    ecs::Entity body = registry.SpawnBundle(
        RigidBody::MakeDynamic(Capsule{{}, 0.5f, 0.35f}, {0.f, 3.f, 0.f}, /*mass*/ 70.f, /*restitution*/ 0.f,
                               /*friction*/ 0.f));

    constexpr float DT = 1.f / 60.f;
    for (int i = 0; i < 240; ++i)
        world.Step(DT);
    RigidBody &rb = registry.GetComponent<RigidBody>(body).Value();
    // Ground top (0.5) + half height (0.5) + radius (0.35).
    EXPECT_TRUE(Near(rb.position.y, 1.35f, 0.05f));
    EXPECT_TRUE(rb.linearVelocity.Length() < 0.1f);

    // Walk into the wall (inner face at x = 2.75) for two seconds.
    for (int i = 0; i < 120; ++i) {
        rb.linearVelocity.x = 4.f;
        world.Step(DT);
    }
    EXPECT_TRUE(rb.position.x < 2.75f - 0.35f + 0.05f); // stopped at the wall, not through it
    EXPECT_TRUE(rb.position.y > 1.2f);                   // still standing on the floor
}

// Regression — two sign errors of CollideSphereBox found while writing the
// capsule case: the contact point sat on the FAR side of the sphere, and a
// sphere whose centre is inside the box got a normal pushing it further in.
TEST(PhysicsSphereBox, TheContactIsOnTheNearSideAndAnEmbeddedSphereIsPushedOut) {
    const Box floor{{0.f, 0.f, 0.f}, {5.f, 0.5f, 5.f}};
    Manifold m;
    ASSERT_TRUE(CollideSphereBox(Sphere{{0.f, 0.9f, 0.f}, 0.5f}, floor, m));
    EXPECT_TRUE(Near(m.normal.y, -1.f));                // sphere -> box
    EXPECT_TRUE(Near(m.points[0].worldPoint.y, 0.4f));  // bottom of the sphere, not its top (1.4)

    ASSERT_TRUE(CollideSphereBox(Sphere{{0.f, 0.4f, 0.f}, 0.2f}, floor, m)); // centre inside, near the top
    EXPECT_TRUE(Near(m.normal.y, -1.f));                // still sphere -> box: the solver lifts it
    EXPECT_TRUE(Near(m.points[0].penetration, 0.1f + 0.2f));
}

int main() {
    return RUN_ALL_TESTS();
}
