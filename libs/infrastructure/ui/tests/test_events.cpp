// Évènements : liaisons OnBind, bouillonnement, gestes, périphériques.
#include "harness.hpp"
#include "test.hpp"

using sdl3::ButtonEvent;
using sdl3::GamepadButton;
using sdl3::MouseButton;

TEST(Bind, TheExampleOfTheSpecification) {
	Harness h;
	ui::Widget viewport, quit;
	h.Mount(ui::Column().Children({
		ui::Button("Quitter").Id("quit").Class("sidebar-icon").Size(120.f, 30.f).Ref(quit),
		ui::Box("viewport").Size(100.f, 100.f).Ref(viewport),
	}));
	quit.OnBind({MouseButton::Left, ButtonEvent::MultiClicks, 4}, {GamepadButton::EAST, ButtonEvent::Clic, 0},
				[&](ui::WidgetEvent) { viewport.Hide(); });
	// Trois clics : rien ; le quatrième masque la vue.
	for (int i = 0; i < 3; ++i)
		h.Click(quit);
	CHECK(viewport.IsVisible());
	h.Click(quit);
	CHECK(!viewport.IsVisible());
	// Manette : bouton EAST sur le widget qui a le focus.
	viewport.Show();
	quit.Focus();
	h.PadClick(GamepadButton::EAST);
	CHECK(!viewport.IsVisible());
}

TEST(Bind, HasGetGetBindsAndOffBind) {
	Harness h;
	ui::Widget b = h.Mount(ui::Button("x").Size(60.f, 30.f));
	int hits = 0;
	const ui::Trigger dbl{MouseButton::Left, ButtonEvent::DoubleClic};
	const ui::Trigger right{MouseButton::Right, ButtonEvent::Clic};
	b.OnBind(dbl, right, [&](const ui::WidgetEvent &) { ++hits; });
	CHECK(b.HasBind(dbl));
	CHECK(b.HasBind(right));
	CHECK(!b.HasBind(ui::Trigger{MouseButton::Middle, ButtonEvent::Clic}));
	auto pair = b.GetBind(right);
	REQUIRE(pair.IsSome());
	CHECK_EQ(pair.Unwrap().first.size(), size_t(2));
	CHECK(pair.Unwrap().second != nullptr);
	CHECK_EQ(b.GetBinds().size(), size_t(1));
	h.Click(b, SDL_BUTTON_RIGHT);
	CHECK_EQ(hits, 1);
	b.OffBind(right);
	CHECK(!b.HasBind(right));
	CHECK(b.HasBind(dbl));
	h.Click(b, SDL_BUTTON_RIGHT);
	CHECK_EQ(hits, 1);
	h.Click(b);
	h.Click(b); // double clic
	CHECK_EQ(hits, 2);
	b.OffBind();
	CHECK(b.GetBinds().empty());
}

TEST(Bind, BuildersBindToo) {
	Harness h;
	int clicks = 0;
	auto builder = ui::Button("b").Size(60.f, 30.f).OnClick([&] { ++clicks; });
	CHECK(builder.HasBind({MouseButton::Left, ButtonEvent::Clic}));
	ui::Widget b = h.Mount(builder);
	h.Click(b);
	CHECK_EQ(clicks, 1);
}

TEST(Bubbling, AClickOnALabelReachesItsButton) {
	Harness h;
	int button = 0, page = 0;
	ui::Widget b;
	h.Mount(ui::Column().OnClick([&] { ++page; }).Children({
		ui::Button("libellé").Ref(b).OnClick([&] { ++button; }),
	}));
	ui::Widget label = b.Find(".button-label").Unwrap();
	h.Click(label);
	CHECK_EQ(button, 1);
	CHECK_EQ(page, 1); // remonte jusqu'aux ancêtres
}

TEST(Bubbling, StopPropagationKeepsTheEventLocal) {
	Harness h;
	int outer = 0;
	ui::Widget b;
	h.Mount(ui::Column().OnClick([&] { ++outer; }).Children({
		ui::Button("x").Ref(b).OnClick([](const ui::WidgetEvent &e) { e.StopPropagation(); }),
	}));
	h.Click(b);
	CHECK_EQ(outer, 0);
}

TEST(Bubbling, ARowsContextMenuCanHideItsListsMenu) {
	Harness h;
	int list = 0, row = 0;
	ui::Widget first, second;
	h.Mount(ui::Column().OnContextMenu([&](sdl3::FPoint) { ++list; }).Children({
		ui::Button("propre").Ref(first).OnContextMenu(ui::EventCallback([&](const ui::WidgetEvent &e) {
			e.StopPropagation();
			++row;
		})),
		ui::Button("sans menu").Ref(second),
	}));
	h.Click(first, SDL_BUTTON_RIGHT);
	CHECK_EQ(row, 1);
	CHECK_EQ(list, 0);
	h.Click(second, SDL_BUTTON_RIGHT); // sans menu propre : celui de la liste
	CHECK_EQ(list, 1);
}

TEST(Gestures, AClickNeedsPressAndReleaseOnTheSameWidget) {
	Harness h;
	int a = 0, b = 0;
	ui::Widget wa, wb;
	h.Mount(ui::Row().Children({
		ui::Button("a").Size(50.f, 30.f).Ref(wa).OnClick([&] { ++a; }),
		ui::Button("b").Size(50.f, 30.f).Ref(wb).OnClick([&] { ++b; }),
	}));
	const sdl3::FPoint pa = Harness::Center(wa), pb = Harness::Center(wb);
	h.Down(pa.x, pa.y);
	h.Move(pb.x, pb.y);
	h.Up(pb.x, pb.y);
	CHECK_EQ(a, 0);
	CHECK_EQ(b, 0);
}

TEST(Gestures, PressReleaseAndHold) {
	Harness h;
	std::vector<String> log;
	ui::Widget b = h.Mount(ui::Box().Size(50.f, 50.f));
	b.OnBind({MouseButton::Left, ButtonEvent::Press}, [&](ui::WidgetEvent) { log.push_back("press"); });
	b.OnBind({MouseButton::Left, ButtonEvent::Release}, [&](ui::WidgetEvent) { log.push_back("release"); });
	b.OnBind({MouseButton::Left, ButtonEvent::Hold, 300}, [&](ui::WidgetEvent) { log.push_back("hold"); });
	const sdl3::FPoint c = Harness::Center(b);
	h.Down(c.x, c.y);
	h.Advance(0.1f);
	CHECK_EQ(log.size(), size_t(1));
	h.Advance(0.3f);
	h.Advance(0.3f);
	h.Up(c.x, c.y);
	REQUIRE_EQ(log.size(), size_t(3));
	CHECK(log[0] == "press");
	CHECK(log[1] == "hold"); // une seule fois par appui
	CHECK(log[2] == "release");
}

TEST(Keyboard, KeysGoToTheFocusedWidgetAndBubble) {
	Harness h;
	int saved = 0, global = 0;
	ui::Widget b = h.Mount(ui::Button("x"));
	b.OnKey(sdl3::keyboard::Key::S, [&] { ++saved; }, sdl3::keyboard::Keymod::CTRL);
	h.gui.Root().OnKey(sdl3::keyboard::Key::F5, [&] { ++global; });
	h.Key(SDLK_S, SDL_KMOD_LCTRL);
	CHECK_EQ(saved, 0); // pas de focus
	b.Focus();
	h.Key(SDLK_S, SDL_KMOD_NONE);
	CHECK_EQ(saved, 0); // Ctrl exigé
	h.Key(SDLK_S, SDL_KMOD_LCTRL);
	CHECK_EQ(saved, 1);
	h.Key(SDLK_F5);
	CHECK_EQ(global, 1); // remonte jusqu'à la racine
}

TEST(Keyboard, EnterAndSpaceActivateTheFocusedWidget) {
	Harness h;
	int clicks = 0;
	ui::Widget b = h.Mount(ui::Button("ok").OnClick([&] { ++clicks; }));
	b.Focus();
	h.Key(SDLK_RETURN);
	CHECK_EQ(clicks, 1);
	ui::Widget cb = h.Mount(ui::Checkbox("c"));
	cb.Focus();
	h.Key(SDLK_SPACE);
	CHECK(cb.Checked());
}

TEST(Keyboard, TabCyclesTheFocus) {
	Harness h;
	ui::Widget a, b;
	h.Mount(ui::Column().Children({ui::Button("a").Ref(a), ui::Text("pas focalisable"), ui::Button("b").Ref(b)}));
	h.Key(SDLK_TAB);
	CHECK(h.gui.Focused() == a);
	h.Key(SDLK_TAB);
	CHECK(h.gui.Focused() == b);
	h.Key(SDLK_TAB, SDL_KMOD_LSHIFT);
	CHECK(h.gui.Focused() == a);
	CHECK(ui::HasState(a.GetState(), ui::State::FocusVisible));
}

TEST(Gamepad, DPadMovesTheFocusAndSouthActivates) {
	Harness h;
	int second = 0;
	ui::Widget a, b;
	h.Mount(ui::Column().Children({ui::Button("a").Ref(a), ui::Button("b").Ref(b).OnClick([&] { ++second; })}));
	h.PadClick(GamepadButton::D_PAD_DOWN);
	CHECK(h.gui.Focused() == a); // premier focus
	h.PadClick(GamepadButton::D_PAD_DOWN);
	CHECK(h.gui.Focused() == b);
	h.PadClick(GamepadButton::SOUTH);
	CHECK_EQ(second, 1);
}

TEST(Pointer, EnterLeaveAndConsumption) {
	Harness h;
	int enter = 0, leave = 0;
	ui::Widget b = h.Mount(ui::Box().Size(50.f, 50.f).OnHover([&](bool in) { (in ? enter : leave)++; }));
	const sdl3::FPoint c = Harness::Center(b);
	CHECK(h.Move(c.x, c.y));	   // au-dessus de l'interface : consommé
	CHECK(!h.Move(700.f, 550.f)); // dans le vide : pas consommé
	CHECK_EQ(enter, 1);
	CHECK_EQ(leave, 1);
	// Transparent au pointeur : le widget derrière reçoit les évènements.
	int behind = 0;
	ui::Widget back = h.Mount(ui::Stack().Size(60.f, 60.f).OnClick([&] { ++behind; }).Children({
		ui::Box().Size(60.f, 60.f).PointerThrough(),
	}));
	h.Click(back);
	CHECK_EQ(behind, 1);
}

TEST(Window, WindowEventsReachEveryListener) {
	Harness h;
	int resized = 0;
	ui::Widget a = h.Mount(ui::Box());
	a.OnBind(ui::Trigger::Window(sdl3::EventType::WINDOW_RESIZED), [&](ui::WidgetEvent) { ++resized; });
	sdl3::Event ev = h.Base(SDL_EVENT_WINDOW_RESIZED);
	h.Send(ev);
	CHECK_EQ(resized, 1);
}

TEST(Custom, NamedApplicationEvents) {
	Harness h;
	String got;
	ui::Widget page, child;
	h.Mount(ui::Column().Ref(page).OnCustom("save", [&](const ui::WidgetEvent &e) { got = e.text; }).Children({
		ui::Box().Ref(child),
	}));
	ui::Emit(child, "save");
	CHECK(got == "save");
}

TEST(Disabled, DisabledWidgetsAreInert) {
	Harness h;
	int clicks = 0;
	ui::Widget b = h.Mount(ui::Button("x").Size(60.f, 30.f).Disabled().OnClick([&] { ++clicks; }));
	h.Click(b);
	CHECK_EQ(clicks, 0);
	b.Enable();
	h.Frame();
	h.Click(b);
	CHECK_EQ(clicks, 1);
}
