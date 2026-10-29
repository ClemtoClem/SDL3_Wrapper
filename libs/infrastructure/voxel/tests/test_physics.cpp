// Tests — physique voxel : collisions, marches, glace, fluides, nodebox,
// raycasts et contacts (voxel/physics.hpp).
#include "test.hpp"

#include <voxel/json.hpp>
#include <ecs/events.hpp>
#include <voxel/physics.hpp>
#include <voxel/worldgen.hpp>

using namespace voxel;

namespace {

void Load(ContentRegistry &content, const char *text) {
	auto parsed = json::Parse(String(text));
	if (parsed.IsError() || content.Load(parsed.Unwrap(), {"t", "", "test"}).IsError())
		std::printf("définition refusée : %s\n", text);
}

struct Scene {
	ContentRegistry content;
	std::unique_ptr<World> world;
	ecs::ArchetypeRegistry registry;
	Scene() {
		Load(content, R"({"id": "stone"})");
		Load(content, R"({"id": "ice", "physics": {"surface": {"slipperiness": 0.95}}})");
		Load(content, R"({"id": "water", "tags": ["fluid"], "physics": {"solid": false}})");
		Load(content, R"({"id": "slab", "type": "nodebox", "nodebox": [[-0.5, -0.5, -0.5, 0.5, 0, 0.5]]})");
		Load(content, R"({"id": "ladder", "type": "nodebox", "tags": ["climbable"], "physics": {"solid": false},
			"nodebox": [[-0.5, -0.5, 0.4, 0.5, 0.5, 0.5]]})");
		Load(content, R"({"type": "generator", "id": "flat", "kind": "flat", "layers": [["stone", 10]]})");
		world = std::make_unique<World>(content);
		world->SetGenerator(MakeGenerator(content, "t:flat", 1));
		for (int y = -1; y <= 1; ++y)
			for (int z = -2; z <= 2; ++z)
				for (int x = -2; x <= 2; ++x)
					(void)world->EnsureChunk({x, y, z});
	}
	ecs::Entity Spawn(math::FVector3 at) {
		Transform t;
		t.position = at;
		return registry.SpawnBundle(t, Velocity{}, Collider{}, PhysicsBody{}, MoveIntent{});
	}
	void Run(float seconds) {
		for (float t = 0.f; t < seconds; t += 1.f / 60.f)
			StepPhysics(registry, *world, 1.f / 60.f);
	}
	Transform &T(ecs::Entity e) { return *registry.GetComponent<Transform>(e).Unwrap(); }
	PhysicsBody &B(ecs::Entity e) { return *registry.GetComponent<PhysicsBody>(e).Unwrap(); }
	MoveIntent &I(ecs::Entity e) { return *registry.GetComponent<MoveIntent>(e).Unwrap(); }
	Velocity &V(ecs::Entity e) { return *registry.GetComponent<Velocity>(e).Unwrap(); }
};

} // namespace

TEST(Physics_FallsLandsAndNeverTunnels) {
	Scene s;
	const ecs::Entity e = s.Spawn({0.5f, 20.f, 0.5f});
	s.Run(2.f);
	CHECK_NEAR(s.T(e).position.y, 10.f, 1e-3f); // sur la couche de pierre (y = 0..9)
	CHECK(s.B(e).onGround);
	CHECK(s.B(e).lastImpactSpeed > 10.f);
	// Très grande vitesse : la boîte s'arrête quand même sur le sol.
	s.V(e).linear = {0.f, -500.f, 0.f};
	s.T(e).position.y = 12.f;
	s.Run(0.1f);
	CHECK_NEAR(s.T(e).position.y, 10.f, 1e-3f);
}

TEST(Physics_WallsStopAndStepsAreClimbed) {
	Scene s;
	(void)s.world->SetBlock({3, 10, 0}, String("t:stone"));
	(void)s.world->SetBlock({3, 11, 0}, String("t:stone"));
	const ecs::Entity e = s.Spawn({0.5f, 10.f, 0.5f});
	s.I(e).wish = {1.f, 0.f, 0.f};
	s.Run(2.f);
	CHECK(s.T(e).position.x < 3.f - 0.29f && s.T(e).position.x > 2.6f); // contre le mur
	// Une marche d'une demi-dalle est montée sans sauter.
	(void)s.world->SetBlock({0, 10, 3}, String("t:slab"));
	s.T(e).position = {0.5f, 10.f, 1.5f};
	s.V(e).linear = {};
	s.I(e).wish = {0.f, 0.f, 1.f};
	s.Run(1.f);
	CHECK(s.T(e).position.z > 3.2f);
	CHECK(s.T(e).position.y >= 10.f);
	// Un mur de deux blocs ne se franchit pas ; le saut monte d'un bloc.
	s.T(e).position = {0.5f, 10.f, -1.5f};
	s.V(e).linear = {};
	s.I(e).wish = {};
	s.I(e).jump = true;
	s.Run(0.15f);
	CHECK(s.T(e).position.y > 10.5f);
}

TEST(Physics_IceSlidesAndWaterSlowsAndFloats) {
	Scene s;
	auto slideAfterStop = [&](const char *floor) {
		for (int x = -8; x <= 8; ++x)
			for (int z = -2; z <= 2; ++z)
				(void)s.world->SetBlock({x, 9, z}, String(floor));
		const ecs::Entity e = s.Spawn({-6.f, 10.f, 0.5f});
		s.I(e).wish = {1.f, 0.f, 0.f};
		s.Run(1.f);
		s.I(e).wish = {};
		const float stopAt = s.T(e).position.x;
		s.Run(1.f);
		const float slide = s.T(e).position.x - stopAt;
		s.registry.Despawn(e);
		return slide;
	};
	const float onStone = slideAfterStop("t:stone");
	const float onIce = slideAfterStop("t:ice");
	CHECK(onIce > onStone * 3.f);

	// Eau : chute amortie, et la nage remonte.
	for (int y = 10; y <= 14; ++y)
		for (int z = -1; z <= 1; ++z)
			for (int x = -1; x <= 1; ++x)
				(void)s.world->SetBlock({x, y, z}, String("t:water"));
	const ecs::Entity swimmer = s.Spawn({0.5f, 13.f, 0.5f});
	s.Run(0.5f);
	CHECK(s.B(swimmer).inFluid);
	CHECK(s.T(swimmer).position.y > 10.5f); // pas encore au fond
	s.I(swimmer).jump = true;
	const float y0 = s.T(swimmer).position.y;
	s.Run(0.5f);
	CHECK(s.T(swimmer).position.y > y0);
}

TEST(Physics_RaycastHitsNodeBoxesAndEntities) {
	Scene s;
	(void)s.world->SetBlock({0, 10, 3}, String("t:slab"));
	// Le rayon passe au-dessus de la demi-dalle (y = 10,7) : pas de touche
	// sur elle, il continue jusqu'au bloc plein suivant.
	(void)s.world->SetBlock({0, 10, 6}, String("t:stone"));
	Option<BlockHit> over = RaycastBlocks(*s.world, {0.5f, 10.7f, 0.5f}, {0.f, 0.f, 1.f}, 20.f);
	REQUIRE(over.IsSome());
	CHECK(over.Value().block == (BlockPos{0, 10, 6}));
	CHECK(over.Value().face == Face::NORTH);
	CHECK(over.Value().previous == (BlockPos{0, 10, 5}));
	Option<BlockHit> slab = RaycastBlocks(*s.world, {0.5f, 10.2f, 0.5f}, {0.f, 0.f, 1.f}, 20.f);
	REQUIRE(slab.IsSome());
	CHECK(slab.Value().block == (BlockPos{0, 10, 3}));
	CHECK_NEAR(slab.Value().distance, 2.5f, 1e-4f);
	// Vers le bas : la face du dessus du sol.
	Option<BlockHit> down = RaycastBlocks(*s.world, {5.5f, 15.f, 5.5f}, {0.f, -1.f, 0.f}, 20.f);
	REQUIRE(down.IsSome());
	CHECK(down.Value().face == Face::TOP && down.Value().block.y == 9);
	// Entités.
	const ecs::Entity e = s.Spawn({0.5f, 10.f, -3.f});
	Option<EntityHit> hit = RaycastEntities(s.registry, {0.5f, 11.f, 0.5f}, {0.f, 0.f, -1.f}, 10.f);
	REQUIRE(hit.IsSome());
	CHECK(hit.Value().entity == e);
	CHECK_NEAR(hit.Value().distance, 3.5f - 0.3f, 1e-3f);
}

TEST(Physics_EntitiesPushEachOtherAndReportContacts) {
	Scene s;
	const ecs::Entity a = s.Spawn({0.5f, 10.f, 0.5f});
	const ecs::Entity b = s.Spawn({0.7f, 10.f, 0.5f});
	s.Run(0.05f);
	CHECK(ecs::EventsOf<EntityContact>(s.registry).Size() > 0);
	s.Run(1.f);
	CHECK(std::fabs(s.T(a).position.x - s.T(b).position.x) > 0.5f);
}
