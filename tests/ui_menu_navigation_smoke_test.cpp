// Tests unitaires — menus de ui:: : popups dessinés comme la liste d'un
// combo (entrées jointives), navigation automatique au survol dans les
// sous-menus et dans la barre de menus (InputSystem::NavigateMenus).
//
// Aucun GPU ni fenêtre : layout + input sont purement calculatoires (pilote
// vidéo "dummy"), toutes les tailles sont posées explicitement.
#define USE_TEST

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include <cstdlib>

namespace {

sdl3::Event ButtonEv(Uint32 type, float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = type;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = SDL_BUTTON_LEFT;
	ev.raw.button.clicks = 1;
	return ev;
}

sdl3::Event MotionEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_MOTION;
	ev.raw.motion.x = x;
	ev.raw.motion.y = y;
	return ev;
}

struct Harness {
	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::UiFactory f{ar, layout};
	ui::StyleSystem styles;

	Harness() {
		setenv("SDL_VIDEODRIVER", "dummy", 0);
		styles.sheet = &f.sheet;
	}

	/// Même ordre que ui::Ui::Update : style résolu, puis mise en page.
	void Layout() {
		styles.Resolve(ar);
		layout.MarkDirty();
		layout.Run(ar, 800.f, 600.f);
	}
	/// Survole le centre de `e` puis laisse s'écouler `seconds` : une image
	/// où le survol commence, puis la durée demandée.
	void Hover(ecs::Entity e, float seconds) {
		const sdl3::FRect r = Screen(e);
		input.HandleEvent(ar, MotionEv(r.x + r.w * 0.5f, r.y + r.h * 0.5f), layout);
		Tick(0.f);
		Tick(seconds);
	}
	void Tick(float seconds) {
		input.Tick(ar, layout, seconds);
		Layout();
	}
	void Click(ecs::Entity e) {
		const sdl3::FRect r = Screen(e);
		const float x = r.x + r.w * 0.5f, y = r.y + r.h * 0.5f;
		input.HandleEvent(ar, MotionEv(x, y), layout);
		input.HandleEvent(ar, ButtonEv(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y), layout);
		input.HandleEvent(ar, ButtonEv(SDL_EVENT_MOUSE_BUTTON_UP, x, y), layout);
		Layout();
	}
	[[nodiscard]] bool IsOpen(ecs::Entity popup) {
		auto state = ar.GetComponent<ui::UiPopupState>(popup);
		return state.IsSome() && state.Unwrap()->open && !ar.HasComponent<ui::UiHidden>(popup);
	}
	[[nodiscard]] sdl3::FRect Screen(ecs::Entity e) { return ar.GetComponent<ui::UiComputed>(e).Unwrap()->screen; }
	[[nodiscard]] ecs::Entity Child(ecs::Entity parent, size_t index) {
		return ar.GetComponent<ui::UiChildren>(parent).Unwrap()->list[index];
	}
	[[nodiscard]] ecs::Entity TriggerOf(ecs::Entity submenu) {
		return ar.GetComponent<ui::UiPopupState>(submenu).Unwrap()->trigger;
	}

	/// Menu contextuel : « Action », puis deux entrées à sous-menu.
	struct Menu {
		ecs::Entity root, first, second;
	};
	Menu MakeMenu() {
		Menu m;
		m.root = f.ContextMenu(f.MenuItem("Action"));
		m.first = f.SubMenu(m.root, "Premier", f.MenuItem("Un"), f.MenuItem("Deux"));
		m.second = f.SubMenu(m.root, "Second", f.MenuItem("Trois"));
		Layout();
		ui::OpenPopupAt(ar, layout, m.root, {100.f, 100.f});
		Layout();
		return m;
	}
};

bool Near(float a, float b) { return sdl3::Abs(a - b) < 0.01f; }

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// Apparence : entrées jointives, pleine largeur, comme une liste de combo
// ─────────────────────────────────────────────────────────────────────────

TEST(MenuLook, EntriesAreContiguousFullWidthRows) {
	Harness h;
	Harness::Menu m = h.MakeMenu();
	const sdl3::FRect popup = h.Screen(m.root);
	const sdl3::FRect a = h.Screen(h.Child(m.root, 0));
	const sdl3::FRect b = h.Screen(h.Child(m.root, 1));
	const sdl3::FRect c = h.Screen(h.Child(m.root, 2));
	EXPECT_TRUE(Near(a.y + a.h, b.y)); // aucun espacement entre deux entrées
	EXPECT_TRUE(Near(b.y + b.h, c.y));
	EXPECT_TRUE(Near(a.w, b.w));
	// Toute la largeur intérieure (bordure d'un pixel de chaque côté).
	EXPECT_TRUE(Near(a.w, popup.w - 2.f));
}

TEST(MenuLook, ASubmenuLinesUpWithTheEntryThatOpensIt) {
	Harness h;
	Harness::Menu m = h.MakeMenu();
	h.Click(h.Child(m.root, 1));
	ASSERT_TRUE(h.IsOpen(m.first));
	const sdl3::FRect entry = h.Screen(h.Child(m.root, 1));
	const sdl3::FRect firstRow = h.Screen(h.Child(m.first, 0));
	EXPECT_TRUE(Near(firstRow.y, entry.y));
	EXPECT_TRUE(h.Screen(m.first).x >= entry.x + entry.w - 0.01f);
}

// ─────────────────────────────────────────────────────────────────────────
// Navigation au survol
// ─────────────────────────────────────────────────────────────────────────

TEST(MenuNavigation, HoveringAnEntryOpensItsSubmenuAfterAShortDelay) {
	Harness h;
	Harness::Menu m = h.MakeMenu();
	ecs::Entity premier = h.Child(m.root, 1);
	h.Hover(premier, 0.05f);
	EXPECT_FALSE(h.IsOpen(m.first)); // traversée rapide : rien ne bouge
	h.Tick(h.input.submenuDelay);
	EXPECT_TRUE(h.IsOpen(m.first));
	EXPECT_TRUE(h.TriggerOf(m.first) == premier);
}

TEST(MenuNavigation, HoveringASiblingSwapsTheOpenSubmenu) {
	Harness h;
	Harness::Menu m = h.MakeMenu();
	h.Hover(h.Child(m.root, 1), 0.5f);
	ASSERT_TRUE(h.IsOpen(m.first));
	h.Hover(h.Child(m.root, 2), 0.5f);
	EXPECT_TRUE(h.IsOpen(m.second));
	EXPECT_FALSE(h.IsOpen(m.first));
}

TEST(MenuNavigation, HoveringAPlainEntryFoldsTheSubmenus) {
	Harness h;
	Harness::Menu m = h.MakeMenu();
	h.Hover(h.Child(m.root, 1), 0.5f);
	ASSERT_TRUE(h.IsOpen(m.first));
	h.Hover(h.Child(m.root, 0), 0.5f);
	EXPECT_FALSE(h.IsOpen(m.first));
	EXPECT_TRUE(h.IsOpen(m.root));
}

TEST(MenuNavigation, MovingIntoTheSubmenuKeepsItOpen) {
	Harness h;
	Harness::Menu m = h.MakeMenu();
	h.Hover(h.Child(m.root, 1), 0.5f);
	ASSERT_TRUE(h.IsOpen(m.first));
	h.Hover(h.Child(m.first, 1), 0.5f);
	EXPECT_TRUE(h.IsOpen(m.first));
	EXPECT_TRUE(h.IsOpen(m.root));
}

TEST(MenuNavigation, ClickingAnOpenSubmenuEntryDoesNotCloseIt) {
	Harness h;
	Harness::Menu m = h.MakeMenu();
	ecs::Entity premier = h.Child(m.root, 1);
	h.Hover(premier, 0.5f);
	ASSERT_TRUE(h.IsOpen(m.first));
	h.Click(premier);
	EXPECT_TRUE(h.IsOpen(m.first));
}

TEST(MenuNavigation, ClosingTheMenuFromALeafClosesTheWholeChain) {
	Harness h;
	Harness::Menu m = h.MakeMenu();
	h.Hover(h.Child(m.root, 1), 0.5f);
	ASSERT_TRUE(h.IsOpen(m.first));
	h.Click(h.Child(m.first, 0));
	EXPECT_FALSE(h.IsOpen(m.first));
	EXPECT_FALSE(h.IsOpen(m.root));
}

TEST(MenuNavigation, TheMenuBarFollowsThePointerOnceAMenuIsOpen) {
	Harness h;
	auto barBuilder = h.f.MenuBar();
	barBuilder.Anchor(ui::Anchor::TopLeft).Offset(0.f, 0.f);
	ecs::Entity bar = barBuilder.Spawn();
	ecs::Entity file = h.f.Menu(bar, "Fichier", h.f.MenuItem("Ouvrir"));
	ecs::Entity edit = h.f.Menu(bar, "Édition", h.f.MenuItem("Copier"));
	h.Layout();
	ecs::Entity fileEntry = h.Child(bar, 0), editEntry = h.Child(bar, 1);

	// Rien d'ouvert : le simple survol n'ouvre rien.
	h.Hover(editEntry, 0.5f);
	EXPECT_FALSE(h.IsOpen(edit));

	h.Click(fileEntry);
	ASSERT_TRUE(h.IsOpen(file));
	h.Hover(editEntry, 0.f); // bascule immédiate, sans délai
	EXPECT_TRUE(h.IsOpen(edit));
	EXPECT_FALSE(h.IsOpen(file));
	const sdl3::FRect entry = h.Screen(editEntry);
	EXPECT_TRUE(Near(h.Screen(edit).x, entry.x));
	EXPECT_TRUE(Near(h.Screen(edit).y, entry.y + entry.h));
}

int main() { return RUN_ALL_TESTS(); }
