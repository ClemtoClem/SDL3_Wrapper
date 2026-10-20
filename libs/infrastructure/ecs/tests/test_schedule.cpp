// Tests — ordonnanceur de systèmes, événements et relations (schedule.hpp,
// events.hpp, relations.hpp).
#include "test.hpp"

#include <ecs/relations.hpp>
#include <ecs/schedule.hpp>

namespace {
struct Pos {
	float y = 0.f;
};
struct Vel {
	float vy = 0.f;
};
struct Ping {
	int value;
};
struct OwnedBy {};
} // namespace

TEST(Schedule_RunsStagesInOrderWithAFixedStep) {
	ecs::ArchetypeRegistry world;
	ecs::Schedule schedule;
	schedule.fixedDt = 0.1f;
	std::vector<String> trace;
	schedule.Add(ecs::Stage::UPDATE, "update", [&](ecs::SystemContext &) { trace.push_back("update"); });
	schedule.Add(ecs::Stage::FIXED_UPDATE, "fixed", [&](ecs::SystemContext &c) {
		CHECK_NEAR(c.dt, 0.1f, 1e-6f);
		trace.push_back("fixed");
	});
	schedule.Add(ecs::Stage::STARTUP, "startup", [&](ecs::SystemContext &) { trace.push_back("startup"); });
	schedule.Add(ecs::Stage::UPDATE, "first", [&](ecs::SystemContext &) { trace.push_back("first"); }, -1);
	schedule.Update(world, 0.25f); // 2 pas fixes, reste 0,05
	REQUIRE_EQ(trace.size(), size_t(5));
	CHECK(trace[0] == "startup");
	CHECK(trace[1] == "fixed" && trace[2] == "fixed");
	CHECK(trace[3] == "first" && trace[4] == "update");
	CHECK_NEAR(schedule.FixedAlpha(), 0.5f, 1e-4f);
	trace.clear();
	schedule.SetEnabled("update", false);
	schedule.Update(world, 0.05f); // complète un pas fixe
	CHECK_EQ(trace.size(), size_t(2));
	CHECK_EQ(schedule.Stats().size(), size_t(4));
}

TEST(Schedule_FlushesCommandsAfterEachStage) {
	ecs::ArchetypeRegistry world;
	ecs::Schedule schedule;
	schedule.Add(ecs::Stage::PRE_UPDATE, "spawn", [](ecs::SystemContext &c) {
		c.commands.Push([](ecs::ArchetypeRegistry &w) { (void)w.SpawnBundle(Pos{10.f}, Vel{-1.f}); });
	});
	int seen = 0;
	schedule.Add(ecs::Stage::UPDATE, "move", [&](ecs::SystemContext &c) {
		c.world.Query<Pos, Vel>([&](ecs::Entity, Pos &p, Vel &v) {
			p.y += v.vy;
			++seen;
		});
	});
	schedule.Update(world, 0.016f);
	CHECK_EQ(seen, 1); // l'entité créée en PRE_UPDATE est visible en UPDATE
}

TEST(Events_LiveTwoFramesAndCursorsReadOnce) {
	ecs::ArchetypeRegistry world;
	auto &events = ecs::EventsOf<Ping>(world);
	events.Send({1});
	events.Send({2});
	uint64_t cursor = 0;
	int sum = 0;
	events.Read(cursor, [&](const Ping &p) { sum += p.value; });
	CHECK_EQ(sum, 3);
	events.Read(cursor, [&](const Ping &p) { sum += p.value; });
	CHECK_EQ(sum, 3); // déjà lus
	ecs::UpdateAllEvents(world);
	CHECK_EQ(events.Size(), size_t(2)); // encore vivants une image
	ecs::UpdateAllEvents(world);
	CHECK(events.IsEmpty());
}

TEST(Relations_HierarchyAndTypedRelations) {
	ecs::ArchetypeRegistry world;
	const ecs::Entity root = world.SpawnBundle(Pos{});
	const ecs::Entity child = world.SpawnBundle(Pos{});
	const ecs::Entity grandChild = world.SpawnBundle(Pos{});
	CHECK(ecs::SetParent(world, child, root));
	CHECK(ecs::SetParent(world, grandChild, child));
	CHECK(!ecs::SetParent(world, root, grandChild)); // cycle refusé
	CHECK(ecs::ParentOf(world, grandChild).Unwrap() == child);
	CHECK_EQ(ecs::ChildrenOf(world, root).size(), size_t(1));

	const ecs::Entity sword = world.SpawnBundle(ecs::Relation<OwnedBy>{child});
	CHECK_EQ(ecs::RelatedTo<OwnedBy>(world, child).size(), size_t(1));

	CHECK_EQ(ecs::DespawnRecursive(world, child), size_t(2));
	CHECK(world.IsAlive(root) && !world.IsAlive(child) && !world.IsAlive(grandChild));
	CHECK(ecs::ChildrenOf(world, root).empty());
	CHECK_EQ(ecs::EventsOf<ecs::EntityDespawned>(world).Size(), size_t(2));
	CHECK_EQ(ecs::PruneRelations<OwnedBy>(world), size_t(1));
	CHECK(!world.HasComponent<ecs::Relation<OwnedBy>>(sword));
}
