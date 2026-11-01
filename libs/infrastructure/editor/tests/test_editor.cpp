// Module editor : navigation, gizmo, palette de commandes, docks.
#include "test.hpp"
#include <editor/editor.hpp>

#include <cmath>

namespace {

constexpr float W = 800.f, H = 600.f;

editor::GizmoView ViewOf(const editor::OrbitNavigator &nav) { return {nav.ToCamera(W / H), W, H}; }

math::FVector2 Screen(const editor::OrbitNavigator &nav, const math::FVector3 &p) {
	return nav.ToCamera(W / H).WorldToScreen(p, W, H).Value();
}

bool Near3(const math::FVector3 &a, const math::FVector3 &b, float eps) { return (a - b).Length() <= eps; }

} // namespace

// ── Navigation ──────────────────────────────────────────────────────────────

TEST(Navigation, OrbitKeepsPivotAndDistance) {
	editor::OrbitNavigator nav;
	nav.pivot = {1.f, 2.f, 3.f};
	nav.distance = 7.f;
	const math::FVector3 before = nav.Eye();
	nav.Orbit(120.f, -40.f);
	CHECK(Near3(nav.pivot, {1.f, 2.f, 3.f}, 1e-5f));
	CHECK_NEAR((nav.Eye() - nav.pivot).Length(), 7.f, 1e-4f);
	CHECK(!Near3(nav.Eye(), before, 0.1f));
	nav.Orbit(0.f, 100000.f); // tangage borné
	CHECK(nav.pitch >= -editor::OrbitNavigator::PITCH_LIMIT - 1e-6f);
}

TEST(Navigation, PanKeepsThePointUnderTheCursor) {
	editor::OrbitNavigator nav;
	nav.distance = 12.f;
	const math::FVector2 before = Screen(nav, nav.pivot);
	const math::FVector3 world = nav.pivot;
	nav.Pan(40.f, -25.f, H);
	const math::FVector2 after = Screen(nav, world);
	CHECK_NEAR(after.x - before.x, 40.f, 0.5f);
	CHECK_NEAR(after.y - before.y, -25.f, 0.5f);
}

TEST(Navigation, DollyTowardACursorPointKeepsItInPlace) {
	editor::OrbitNavigator nav;
	nav.distance = 20.f;
	const math::FVector3 target{3.f, 0.5f, -2.f};
	const math::FVector2 before = Screen(nav, target);
	nav.Dolly(3.f, Some(target));
	CHECK(nav.distance < 20.f);
	const math::FVector2 after = Screen(nav, target);
	CHECK_NEAR(after.x, before.x, 0.5f);
	CHECK_NEAR(after.y, before.y, 0.5f);
}

TEST(Navigation, FrameShowsTheWholeBoxAndPresetsLookAlongAxes) {
	editor::OrbitNavigator nav;
	nav.Frame({-5.f, 0.f, -5.f}, {5.f, 4.f, 5.f}, W / H);
	for (float x : {-5.f, 5.f})
		for (float y : {0.f, 4.f})
			for (float z : {-5.f, 5.f}) {
				const math::FVector2 p = Screen(nav, {x, y, z});
				CHECK(p.x >= 0.f && p.x <= W && p.y >= 0.f && p.y <= H);
			}
	nav.SetView(editor::ViewPreset::Front);
	CHECK(Near3(nav.Forward(), {0.f, 0.f, -1.f}, 1e-4f));
	nav.SetView(editor::ViewPreset::Right);
	CHECK(Near3(nav.Forward(), {-1.f, 0.f, 0.f}, 1e-4f));
	nav.SetView(editor::ViewPreset::Top);
	CHECK(nav.Forward().y < -0.99f);
	CHECK(editor::ParseViewPreset("iso").IsSome());
	nav.LookFrom({10.f, 5.f, 10.f}, {0.f, 1.f, 0.f});
	CHECK(Near3(nav.Eye(), {10.f, 5.f, 10.f}, 1e-3f));
}

// ── Gizmo ───────────────────────────────────────────────────────────────────

TEST(Gizmo, PicksAxesAndTranslatesAlongThem) {
	editor::OrbitNavigator nav;
	nav.distance = 10.f;
	const editor::GizmoView view = ViewOf(nav);
	editor::TransformGizmo g;
	const float s = g.WorldSize(view);
	const math::FVector2 mid = Screen(nav, {s * 0.7f, 0.f, 0.f});
	CHECK(g.Pick(view, mid.x, mid.y) == editor::GizmoHandle::X);
	const math::FVector2 center = Screen(nav, {0.f, 0.f, 0.f});
	CHECK(g.Pick(view, center.x, center.y) == editor::GizmoHandle::All);
	CHECK(g.Pick(view, 5.f, 5.f) == editor::GizmoHandle::None);

	CHECK(g.Begin(view, mid.x, mid.y));
	const math::FVector2 target = Screen(nav, {s * 0.7f + 2.f, 0.f, 0.f});
	const editor::GizmoDelta d = g.Drag(view, target.x, target.y);
	CHECK_NEAR(d.translation.x, 2.f, 0.02f);
	CHECK_NEAR(d.translation.y, 0.f, 1e-4f);
	CHECK_NEAR(d.translation.z, 0.f, 1e-4f);
	g.translateSnap = 0.5f;
	const math::FVector2 off = Screen(nav, {s * 0.7f + 1.3f, 0.f, 0.f});
	CHECK_NEAR(g.Drag(view, off.x, off.y).translation.x, 1.5f, 1e-4f);
	g.End();
	CHECK(!g.Active());
	CHECK(!g.Strokes(view).empty());
}

TEST(Gizmo, PlaneHandleStaysInItsPlane) {
	editor::OrbitNavigator nav;
	nav.distance = 10.f;
	const editor::GizmoView view = ViewOf(nav);
	editor::TransformGizmo g;
	const float s = g.WorldSize(view);
	const math::FVector2 inside = Screen(nav, {s * 0.32f, 0.f, s * 0.32f}); // plan ZX
	CHECK(g.Pick(view, inside.x, inside.y) == editor::GizmoHandle::ZX);
	CHECK(g.Begin(view, inside.x, inside.y));
	const math::FVector2 to = Screen(nav, {s * 0.32f + 1.f, 0.f, s * 0.32f - 2.f});
	const editor::GizmoDelta d = g.Drag(view, to.x, to.y);
	CHECK_NEAR(d.translation.x, 1.f, 0.02f);
	CHECK_NEAR(d.translation.y, 0.f, 1e-4f);
	CHECK_NEAR(d.translation.z, -2.f, 0.02f);
}

TEST(Gizmo, RotatesAroundTheGrabbedRingWithSnap) {
	editor::OrbitNavigator nav;
	nav.distance = 10.f;
	nav.pitch = -1.2f; // vue plongeante : l'anneau Y est bien ouvert
	const editor::GizmoView view = ViewOf(nav);
	editor::TransformGizmo g;
	g.mode = editor::GizmoMode::Rotate;
	const float r = g.WorldSize(view) * 0.9f;
	const math::FVector2 start = Screen(nav, {r, 0.f, 0.f});
	CHECK(g.Pick(view, start.x, start.y) == editor::GizmoHandle::Y);
	CHECK(g.Begin(view, start.x, start.y));
	const math::FVector2 quarter = Screen(nav, {0.f, 0.f, -r}); // +90° autour de +Y
	editor::GizmoDelta d = g.Drag(view, quarter.x, quarter.y);
	CHECK(Near3(d.rotationAxis, {0.f, 1.f, 0.f}, 1e-5f));
	CHECK_NEAR(d.angle, 3.14159265f * 0.5f, 0.02f);
	g.rotateSnap = 3.14159265f / 12.f; // 15°
	const math::FVector2 near = Screen(nav, {r * std::cos(0.7f), 0.f, -r * std::sin(0.7f)});
	d = g.Drag(view, near.x, near.y);
	CHECK_NEAR(d.angle, 3.f * 3.14159265f / 12.f, 1e-4f); // 40° → 45°
}

TEST(Gizmo, ScalesAlongAnAxisAndUniformly) {
	editor::OrbitNavigator nav;
	nav.distance = 10.f;
	const editor::GizmoView view = ViewOf(nav);
	editor::TransformGizmo g;
	g.mode = editor::GizmoMode::Scale;
	const float s = g.WorldSize(view);
	const math::FVector2 tip = Screen(nav, {0.f, s * 0.8f, 0.f});
	CHECK(g.Begin(view, tip.x, tip.y));
	CHECK(g.ActiveHandle() == editor::GizmoHandle::Y);
	const math::FVector2 twice = Screen(nav, {0.f, s * 1.6f, 0.f});
	editor::GizmoDelta d = g.Drag(view, twice.x, twice.y);
	CHECK_NEAR(d.scale.y, 2.f, 0.02f);
	CHECK_NEAR(d.scale.x, 1.f, 1e-6f);
	g.End();
	const math::FVector2 c = Screen(nav, {0.f, 0.f, 0.f});
	CHECK(g.Begin(view, c.x + 5.f, c.y));
	CHECK(g.ActiveHandle() == editor::GizmoHandle::All);
	d = g.Drag(view, c.x + 10.f, c.y);
	CHECK_NEAR(d.scale.x, 2.f, 1e-4f);
	CHECK_NEAR(d.scale.z, 2.f, 1e-4f);
}

// ── Palette de commandes ────────────────────────────────────────────────────

TEST(Palette, FuzzySearchRanksWordStartsAndRecentCommands) {
	CHECK(editor::FuzzyScore(String("pse"), String("Pause")).IsSome());
	CHECK(editor::FuzzyScore(String("xyz"), String("Pause")).IsNone());
	CHECK(editor::FuzzyScore(String("sp"), String("studio.pause")).Value() >
		  editor::FuzzyScore(String("sp"), String("dispose")).Value());
	editor::CommandPalette palette;
	int paused = 0, played = 0;
	palette.Add({String("studio.pause"), String("Suspendre la simulation"), String(), String("F6"), [&] { ++paused; }});
	palette.Add({String("studio.play"), String("Reprendre la simulation"), String(), String(), [&] { ++played; }});
	palette.Add({String("studio.step"), String("Avancer d'un pas"), String(), String(), nullptr});
	std::vector<size_t> found = palette.Search(String("suspendre"));
	CHECK_EQ(found.size(), size_t(1));
	CHECK(palette.Entry(found[0]).id == "studio.pause");
	found = palette.Search(String("susp")); // « studio.step » aussi, mais après
	CHECK(palette.Entry(found[0]).id == "studio.pause");
	found = palette.Search(String("simulation"));
	CHECK_EQ(found.size(), size_t(2));
	CHECK(palette.Run(1));
	CHECK_EQ(played, 1);
	CHECK(!palette.Run(2)); // sans action
	CHECK(palette.Recent().front() == "studio.play");
	CHECK_EQ(palette.Search(String()).size(), size_t(3));
}

TEST(Palette, TheViewFiltersMovesAndRuns) {
	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, sdl3::FPoint{800.f, 600.f});
	ui::Widget root = gui.Mount(ui::Column().Grow());
	editor::CommandPalette palette;
	int a = 0, b = 0;
	palette.Add({String("view.top"), String("Vue de dessus"), String(), String(), [&] { ++a; }});
	palette.Add({String("view.front"), String("Vue de face"), String(), String(), [&] { ++b; }});
	editor::CommandPaletteView view(gui, palette);
	view.Build(root);
	gui.Layout();
	CHECK(!view.IsOpen());
	view.Open();
	gui.Layout();
	CHECK(view.IsOpen());
	CHECK_EQ(view.Shown().size(), size_t(2));
	view.SetQuery(String("vue"));
	view.Move(1);
	CHECK_EQ(view.Current(), 1);
	const size_t chosen = view.Shown()[1];
	CHECK(view.Accept());
	CHECK(!view.IsOpen());
	CHECK_EQ(a + b, 1);
	CHECK((chosen == 0 ? a : b) == 1);
}

// ── Docks ───────────────────────────────────────────────────────────────────

TEST(Dock, PagesTabsAndActivation) {
	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, sdl3::FPoint{800.f, 600.f});
	ui::Widget root = gui.Mount(ui::Row().Grow());
	editor::DockPanel dock(gui);
	dock.Build(root, [](ui::WidgetBuilder &b) { b.Width(300.f).GrowHeight(); });
	int activated = -1;
	dock.onActivate = [&](int i) { activated = i; };
	ui::Widget first = dock.AddPage(String("Outliner"), true);
	ui::Widget second = dock.AddPage(String("Blocs"), false, true);
	gui.Layout();
	CHECK_EQ(dock.Count(), 2);
	CHECK_EQ(dock.Active(), 1);
	CHECK(!first.IsVisible());
	CHECK(second.IsVisible());
	CHECK_EQ(dock.Find(String("Outliner")), 0);
	dock.SetActive(0);
	CHECK_EQ(activated, 0);
	CHECK(first.IsVisible());
	dock.RemovePage(1);
	CHECK_EQ(dock.Count(), 1);
	CHECK(dock.Title(0) == "Outliner");
	CHECK(dock.Title(5).Length() == 0);
}
