#include "test.hpp"
#include <events/events.hpp>

#include <string>
#include <thread>
#include <vector>

using events::Connection;
using events::EventBus;
using events::ScopedConnection;
using events::Signal;

TEST(Signal, EmitCallsEverySlotInConnectionOrder) {
	Signal<int> s;
	std::vector<int> calls;
	(void)s.Connect([&](int v) { calls.push_back(v * 10); });
	(void)s.Connect([&](int v) { calls.push_back(v * 100); });
	s.Emit(2);
	CHECK(calls == (std::vector<int>{20, 200}));
	CHECK_EQ(s.SlotCount(), 2u);
}

TEST(Signal, DisconnectStopsDelivery) {
	Signal<> s;
	int n = 0;
	Connection c = s.Connect([&] { ++n; });
	s.Emit();
	CHECK(c.IsConnected());
	c.Disconnect();
	s.Emit();
	CHECK_EQ(n, 1);
	CHECK(!c.IsConnected());
	CHECK(s.IsEmpty());
}

TEST(Signal, ScopedConnectionDisconnectsOnDestruction) {
	Signal<> s;
	int n = 0;
	{
		ScopedConnection c = s.Connect([&] { ++n; });
		s.Emit();
	}
	s.Emit();
	CHECK_EQ(n, 1);
}

TEST(Signal, ConnectionOutlivesSignalSafely) {
	Connection c;
	{
		Signal<> s;
		c = s.Connect([] {});
		CHECK(c.IsConnected());
	}
	CHECK(!c.IsConnected());
	c.Disconnect(); // sans effet, pas de crash
}

TEST(Signal, SlotsMayDisconnectAndConnectDuringEmit) {
	Signal<> s;
	std::vector<std::string> calls;
	Connection second;
	Connection first = s.Connect([&] {
		calls.push_back("first");
		second.Disconnect();                                  // retire un slot pas encore appelé
		(void)s.Connect([&] { calls.push_back("late"); });     // appelé à l'émission suivante
	});
	second = s.Connect([&] { calls.push_back("second"); });
	s.Emit();
	CHECK(calls == (std::vector<std::string>{"first"}));
	first.Disconnect();
	s.Emit();
	CHECK(calls == (std::vector<std::string>{"first", "late"}));
}

TEST(Signal, EmptySlotIsIgnored) {
	Signal<int> s;
	Connection c = s.Connect(nullptr);
	CHECK(!c.IsConnected());
	s.Emit(1);
	CHECK(s.IsEmpty());
}

namespace {
struct Resized {
	int w, h;
};
struct Quit {};
} // namespace

TEST(EventBus, PublishReachesOnlySubscribersOfThatType) {
	EventBus bus;
	int resized = 0, quit = 0;
	ScopedConnection a = bus.Subscribe<Resized>([&](const Resized &e) { resized += e.w * e.h; });
	ScopedConnection b = bus.Subscribe<Quit>([&](const Quit &) { ++quit; });
	bus.Publish(Resized{2, 3});
	CHECK_EQ(resized, 6);
	CHECK_EQ(quit, 0);
	bus.Publish(Quit{});
	CHECK_EQ(quit, 1);
	CHECK_EQ(bus.SubscriberCount<Resized>(), 1u);
}

TEST(EventBus, PublishWithoutSubscriberIsANoOp) {
	EventBus bus;
	bus.Publish(Quit{});
	CHECK_EQ(bus.SubscriberCount<Quit>(), 0u);
}

TEST(EventBus, EnqueueIsDeferredUntilDispatchInFifoOrder) {
	EventBus bus;
	std::vector<int> seen;
	ScopedConnection c = bus.Subscribe<Resized>([&](const Resized &e) { seen.push_back(e.w); });
	bus.Enqueue(Resized{1, 0});
	bus.Enqueue(Resized{2, 0});
	CHECK(seen.empty());
	CHECK_EQ(bus.PendingCount(), 2u);
	CHECK_EQ(bus.Dispatch(), 2u);
	CHECK(seen == (std::vector<int>{1, 2}));
	CHECK_EQ(bus.PendingCount(), 0u);
}

TEST(EventBus, EventsEnqueuedDuringDispatchWaitForTheNextOne) {
	EventBus bus;
	int n = 0;
	ScopedConnection c = bus.Subscribe<Quit>([&](const Quit &) {
		++n;
		bus.Enqueue(Quit{}); // ré-enfile : ne doit pas boucler dans ce Dispatch
	});
	bus.Enqueue(Quit{});
	CHECK_EQ(bus.Dispatch(), 1u);
	CHECK_EQ(n, 1);
	CHECK_EQ(bus.PendingCount(), 1u);
	bus.ClearQueue();
	CHECK_EQ(bus.Dispatch(), 0u);
}

TEST(EventBus, EnqueueIsSafeFromOtherThreads) {
	EventBus bus;
	int total = 0;
	ScopedConnection c = bus.Subscribe<Resized>([&](const Resized &e) { total += e.w; });
	std::vector<std::thread> producers;
	for (int t = 0; t < 4; ++t)
		producers.emplace_back([&bus] {
			for (int i = 0; i < 250; ++i)
				bus.Enqueue(Resized{1, 0});
		});
	for (auto &p : producers)
		p.join();
	CHECK_EQ(bus.Dispatch(), 1000u);
	CHECK_EQ(total, 1000);
}
