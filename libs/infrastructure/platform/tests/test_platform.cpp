#include "test.hpp"
#include <platform/platform.hpp>

#include <diagnostics/log.hpp>

#include <cstdlib>
#include <cstring>
#include <vector>

using namespace platform;

// ── FixedStepClock ───────────────────────────────────────────────────────────

TEST(FixedStepClock, AccumulatesUntilAWholeStep) {
	FixedStepClock clock({.stepSeconds = 0.01, .maxFrameTime = 1.0, .maxStepsPerFrame = 100});
	CHECK_EQ(clock.Advance(0.004), 0);
	CHECK_EQ(clock.Advance(0.004), 0);
	CHECK_EQ(clock.Advance(0.004), 1); // 0.012 → 1 pas, reste 0.002
	CHECK_NEAR(clock.Alpha(), 0.2, 1e-9);
	CHECK_EQ(clock.Advance(0.05), 5);
	CHECK_EQ(clock.TotalSteps(), 6u);
	CHECK_NEAR(clock.SimulatedSeconds(), 0.06, 1e-12);
}

TEST(FixedStepClock, ClampsHugeFramesAndCapsStepsPerFrame) {
	FixedStepClock clock({.stepSeconds = 0.01, .maxFrameTime = 0.25, .maxStepsPerFrame = 8});
	CHECK_EQ(clock.Advance(10.0), 8);        // borné à 0.25 s puis à 8 pas
	CHECK(clock.DroppedSeconds() > 9.8);     // 9.75 (borne) + 0.17 (pas en trop)
	CHECK(clock.Alpha() >= 0.0 && clock.Alpha() < 1.0);
	CHECK_EQ(clock.Advance(-1.0), 0);        // durée négative ignorée
	clock.Reset();
	CHECK_EQ(clock.TotalSteps(), 0u);
}

// ── InputState ───────────────────────────────────────────────────────────────

TEST(InputState, KeyTransitionsLastOneFrame) {
	InputState in;
	in.BeginFrame();
	in.OnKey(SDL_SCANCODE_SPACE, true);
	CHECK(in.IsDown(SDL_SCANCODE_SPACE));
	CHECK(in.WasPressed(SDL_SCANCODE_SPACE));
	in.BeginFrame();
	in.OnKey(SDL_SCANCODE_SPACE, true); // répétition : pas une nouvelle pression
	CHECK(in.IsDown(SDL_SCANCODE_SPACE));
	CHECK(!in.WasPressed(SDL_SCANCODE_SPACE));
	in.BeginFrame();
	in.OnKey(SDL_SCANCODE_SPACE, false);
	CHECK(!in.IsDown(SDL_SCANCODE_SPACE));
	CHECK(in.WasReleased(SDL_SCANCODE_SPACE));
	CHECK(!in.IsDown(-1));
	CHECK(!in.IsDown(100000));
}

TEST(InputState, MouseDeltaWheelAndReleaseAll) {
	InputState in;
	in.OnMouseMove(10.f, 20.f);
	in.BeginFrame();
	in.OnMouseMove(15.f, 18.f);
	in.OnMouseWheel(0.f, 1.f);
	in.OnMouseWheel(0.f, 2.f);
	in.OnMouseButton(MouseButton::Left, true);
	in.OnKey(SDL_SCANCODE_W, true);
	CHECK_NEAR(in.MouseDeltaX(), 5.f, 1e-6);
	CHECK_NEAR(in.MouseDeltaY(), -2.f, 1e-6);
	CHECK_NEAR(in.WheelY(), 3.f, 1e-6);
	CHECK(in.WasPressed(MouseButton::Left));
	in.ReleaseAll();
	CHECK(!in.IsDown(MouseButton::Left));
	CHECK(!in.IsDown(SDL_SCANCODE_W));
	CHECK(in.WasReleased(SDL_SCANCODE_W));
}

// ── TranslateEvent : SDL_Event synthétiques, sans SDL_Init ──────────────────

namespace {
sdl3::Event Make(SDL_EventType type) {
	sdl3::Event e;
	std::memset(&e.raw, 0, sizeof e.raw);
	e.raw.type = type;
	return e;
}
} // namespace

TEST(TranslateEvent, KeyboardUpdatesInputAndPublishes) {
	events::EventBus bus;
	InputState in;
	std::vector<KeyChanged> keys;
	events::ScopedConnection c = bus.Subscribe<KeyChanged>([&](const KeyChanged &k) { keys.push_back(k); });
	sdl3::Event e = Make(SDL_EVENT_KEY_DOWN);
	e.raw.key.scancode = SDL_SCANCODE_A;
	e.raw.key.key = SDLK_A;
	e.raw.key.down = true;
	CHECK(TranslateEvent(e, in, bus));
	CHECK(in.IsDown(SDL_SCANCODE_A));
	REQUIRE_EQ(keys.size(), 1u);
	CHECK_EQ(keys[0].scancode, int(SDL_SCANCODE_A));
	CHECK(keys[0].down);
}

TEST(TranslateEvent, MouseResizeQuitAndFocus) {
	events::EventBus bus;
	InputState in;
	int quits = 0, focusLost = 0;
	WindowResized size;
	MouseButtonChanged click;
	events::ScopedConnection a = bus.Subscribe<QuitRequested>([&](const QuitRequested &) { ++quits; });
	events::ScopedConnection b = bus.Subscribe<WindowResized>([&](const WindowResized &r) { size = r; });
	events::ScopedConnection c = bus.Subscribe<MouseButtonChanged>([&](const MouseButtonChanged &m) { click = m; });
	events::ScopedConnection d = bus.Subscribe<FocusChanged>([&](const FocusChanged &f) { focusLost += !f.focused; });

	sdl3::Event m = Make(SDL_EVENT_MOUSE_BUTTON_DOWN);
	m.raw.button.button = SDL_BUTTON_RIGHT;
	m.raw.button.down = true;
	m.raw.button.x = 4.f;
	m.raw.button.y = 7.f;
	m.raw.button.clicks = 2;
	CHECK(TranslateEvent(m, in, bus));
	CHECK(in.IsDown(MouseButton::Right));
	CHECK(click.button == MouseButton::Right && click.clicks == 2);
	CHECK_NEAR(in.MouseY(), 7.f, 1e-6);

	sdl3::Event r = Make(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
	r.raw.window.data1 = 640;
	r.raw.window.data2 = 480;
	CHECK(TranslateEvent(r, in, bus));
	CHECK(size.width == 640 && size.height == 480);

	CHECK(TranslateEvent(Make(SDL_EVENT_WINDOW_FOCUS_LOST), in, bus));
	CHECK(!in.IsDown(MouseButton::Right)); // la perte de focus relâche tout
	CHECK_EQ(focusLost, 1);

	CHECK(TranslateEvent(Make(SDL_EVENT_QUIT), in, bus));
	CHECK_EQ(quits, 1);
	CHECK(!TranslateEvent(Make(SDL_EVENT_SENSOR_UPDATE), in, bus));
}

// ── Pont de journalisation et application sans fenêtre ──────────────────────

TEST(SdlLog, PrioritiesMapToDiagnosticsLevels) {
	CHECK(ToDiagnosticsLevel(sdl3::LogPriority::Verbose) == diagnostics::Level::Trace);
	CHECK(ToDiagnosticsLevel(sdl3::LogPriority::Warn) == diagnostics::Level::Warn);
	CHECK(ToDiagnosticsLevel(sdl3::LogPriority::Critical) == diagnostics::Level::Critical);
}

namespace {
class CountingApp final : public Application {
public:
	int inits = 0, updates = 0, renders = 0, shutdowns = 0;
	uint64_t fixedSteps = 0;

protected:
	bool OnInit() override { return ++inits == 1; }
	void OnFixedUpdate(double) override { ++fixedSteps; }
	void OnUpdate(double) override { ++updates; }
	void OnRender(double) override { ++renders; }
	void OnShutdown() override { ++shutdowns; }
};

class QuitOnFirstUpdate final : public Application {
protected:
	void OnUpdate(double) override { GetEvents().Publish(QuitRequested{}); }
};
} // namespace

TEST(Application, HeadlessLoopRunsTheLifecycle) {
	CountingApp app;
	const int code = app.Run({.headless = true, .maxFrames = 5});
	if (code != 0)
		SKIP("SDL_Init(EVENTS) indisponible");
	CHECK_EQ(app.inits, 1);
	CHECK_EQ(app.updates, 5);
	CHECK_EQ(app.renders, 0); // headless : pas de rendu
	CHECK_EQ(app.shutdowns, 1);
	CHECK_EQ(app.FrameCount(), 5u);
	CHECK_EQ(app.fixedSteps, app.GetClock().TotalSteps());
}

TEST(Application, QuitRequestedOnTheBusStopsTheLoop) {
	QuitOnFirstUpdate app;
	if (app.Run({.headless = true, .maxFrames = 1000}) != 0)
		SKIP("SDL_Init(EVENTS) indisponible");
	CHECK_EQ(app.FrameCount(), 1u);
	CHECK(!app.IsRunning());
}
