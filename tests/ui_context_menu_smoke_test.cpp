// Tests unitaires — menus contextuels (clic droit) dans ui:: :
// UiCallbacks::onContextMenu, UiFactory::ContextMenu, ui::OpenPopupAt et le
// maintien des popups racines dans la fenêtre (LayoutSystem::Run).
//
// Aucun GPU ni fenêtre : layout + input sont purement calculatoires (pilote
// vidéo "dummy"), toutes les tailles sont posées explicitement.
#define USE_TEST

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include <cstdlib>

namespace {

sdl3::Event ButtonEv(Uint32 type, Uint8 button, float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = type;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = button;
	ev.raw.button.clicks = 1;
	return ev;
}

struct Harness {
	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::UiFactory f{ar, layout};

	Harness() { setenv("SDL_VIDEODRIVER", "dummy", 0); }

	void Layout() { layout.Run(ar, 800.f, 600.f); }
	void RightClick(float x, float y) {
		input.HandleEvent(ar, ButtonEv(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, x, y), layout);
		input.HandleEvent(ar, ButtonEv(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT, x, y), layout);
		Layout();
	}
	void LeftClick(float x, float y) {
		input.HandleEvent(ar, ButtonEv(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, x, y), layout);
		input.HandleEvent(ar, ButtonEv(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, x, y), layout);
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
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// 1. Le clic droit notifie, avec la position du pointeur ; le gauche, non.
// ─────────────────────────────────────────────────────────────────────────
TEST(ContextMenu, RightClickNotifiesWithThePointerPosition) {
	Harness h;
	int calls = 0;
	float gotX = -1.f, gotY = -1.f;
	auto row = h.f.Button("Ligne");
	row.Anchor(ui::Anchor::TopLeft).Offset(20.f, 20.f).Size(200.f, 30.f);
	row.OnContextMenu([&](float x, float y) {
		++calls;
		gotX = x;
		gotY = y;
	});
	(void)row.Spawn();
	h.Layout();

	h.LeftClick(60.f, 30.f);
	EXPECT_EQ(calls, 0);

	h.RightClick(60.f, 31.f);
	EXPECT_EQ(calls, 1);
	EXPECT_EQ(gotX, 60.f);
	EXPECT_EQ(gotY, 31.f);

	h.RightClick(500.f, 500.f); // hors du widget
	EXPECT_EQ(calls, 1);
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Le plus PROCHE porteur l'emporte : la ligne avant le panneau.
// ─────────────────────────────────────────────────────────────────────────
TEST(ContextMenu, TheNearestBearerWinsAndAncestorsAreTheFallback) {
	Harness h;
	int panelCalls = 0, rowCalls = 0;
	auto panel = h.f.Panel();
	panel.Anchor(ui::Anchor::TopLeft).Offset(0.f, 0.f).Size(400.f, 300.f).Pad(math::Sides{0.f}).Gap(0.f);
	panel.OnContextMenu([&](float, float) { ++panelCalls; });
	{
		auto row = h.f.Button("Avec menu");
		row.Size(200.f, 30.f).OnContextMenu([&](float, float) { ++rowCalls; });
		auto plain = h.f.Button("Sans menu");
		plain.Size(200.f, 30.f);
		panel.Children(std::move(row), std::move(plain));
	}
	(void)panel.Spawn();
	h.Layout();

	h.RightClick(50.f, 15.f); // la ligne qui a son propre menu
	EXPECT_EQ(rowCalls, 1);
	EXPECT_EQ(panelCalls, 0);

	h.RightClick(50.f, 45.f); // une ligne sans menu : le panneau répond
	EXPECT_EQ(rowCalls, 1);
	EXPECT_EQ(panelCalls, 1);

	h.RightClick(300.f, 200.f); // le fond du panneau
	EXPECT_EQ(panelCalls, 2);
}

// ─────────────────────────────────────────────────────────────────────────
// 3. OpenPopupAt : le menu apparaît au point demandé, se referme au clic
//    extérieur (droit comme gauche).
// ─────────────────────────────────────────────────────────────────────────
TEST(ContextMenu, OpenPopupAtPlacesTheMenuUnderThePointerAndAnOutsideClickClosesIt) {
	Harness h;
	auto itemA = h.f.MenuItem("Dupliquer");
	itemA.Size(160.f, 26.f);
	ecs::Entity menu = h.f.ContextMenu(std::move(itemA));
	h.Layout();
	EXPECT_FALSE(h.IsOpen(menu));

	ui::OpenPopupAt(h.ar, h.layout, menu, sdl3::FPoint{120.f, 80.f});
	h.Layout();
	EXPECT_TRUE(h.IsOpen(menu));
	EXPECT_EQ(h.Screen(menu).x, 120.f);
	EXPECT_EQ(h.Screen(menu).y, 80.f);

	h.RightClick(600.f, 500.f);
	EXPECT_FALSE(h.IsOpen(menu));

	ui::OpenPopupAt(h.ar, h.layout, menu, sdl3::FPoint{120.f, 80.f});
	h.Layout();
	h.LeftClick(600.f, 500.f);
	EXPECT_FALSE(h.IsOpen(menu));
}

// ─────────────────────────────────────────────────────────────────────────
// 4. Ouvert près d'un bord, le menu est RAMENÉ dans la fenêtre.
// ─────────────────────────────────────────────────────────────────────────
TEST(ContextMenu, AMenuOpenedNearTheEdgeStaysInsideTheWindow) {
	Harness h;
	auto item = h.f.MenuItem("Renommer");
	item.Size(180.f, 26.f);
	ecs::Entity menu = h.f.ContextMenu(std::move(item));
	ui::OpenPopupAt(h.ar, h.layout, menu, sdl3::FPoint{790.f, 595.f});
	h.Layout();
	const sdl3::FRect r = h.Screen(menu);
	EXPECT_TRUE(r.w > 0.f && r.h > 0.f);
	EXPECT_TRUE(r.x + r.w <= 800.f + 1e-3f);
	EXPECT_TRUE(r.y + r.h <= 600.f + 1e-3f);
	EXPECT_TRUE(r.x >= 0.f && r.y >= 0.f);
}

// ─────────────────────────────────────────────────────────────────────────
// 5. Un clic droit DANS un menu ouvert ne déclenche pas le menu de ce qui
//    est dessous, et ne referme pas le menu.
// ─────────────────────────────────────────────────────────────────────────
TEST(ContextMenu, ARightClickInsideAnOpenMenuDoesNotReachWhatIsBehind) {
	Harness h;
	int behindCalls = 0;
	auto panel = h.f.Panel();
	panel.Anchor(ui::Anchor::TopLeft).Offset(0.f, 0.f).Size(800.f, 600.f);
	panel.OnContextMenu([&](float, float) { ++behindCalls; });
	(void)panel.Spawn();

	auto item = h.f.MenuItem("Supprimer");
	item.Size(160.f, 26.f);
	ecs::Entity menu = h.f.ContextMenu(std::move(item));
	ui::OpenPopupAt(h.ar, h.layout, menu, sdl3::FPoint{100.f, 100.f});
	h.Layout();

	const sdl3::FRect r = h.Screen(menu);
	h.RightClick(r.x + 20.f, r.y + 12.f);
	EXPECT_EQ(behindCalls, 0);
	EXPECT_TRUE(h.IsOpen(menu));
}

// ─────────────────────────────────────────────────────────────────────────
// 6. Sous-menu d'un menu contextuel : l'action d'une feuille s'exécute et
//    toute la chaîne se referme.
// ─────────────────────────────────────────────────────────────────────────
TEST(ContextMenu, ASubmenuLeafRunsItsActionAndClosesTheWholeChain) {
	Harness h;
	int created = 0;
	auto duplicate = h.f.MenuItem("Dupliquer");
	duplicate.Size(180.f, 26.f);
	ecs::Entity menu = h.f.ContextMenu(std::move(duplicate));
	auto light = h.f.MenuItem("Lumière ponctuelle");
	light.Size(180.f, 26.f).OnClick([&] { ++created; });
	ecs::Entity submenu = h.f.SubMenu(menu, "Créer un nœud enfant", std::move(light));

	ui::OpenPopupAt(h.ar, h.layout, menu, sdl3::FPoint{50.f, 50.f});
	h.Layout();

	// Le déclencheur du sous-menu est ajouté APRÈS les items passés à
	// ContextMenu (cf. SubMenu) : c'est le 2e enfant du popup.
	ecs::Entity trigger = h.Child(menu, 1);
	const sdl3::FRect t = h.Screen(trigger);
	h.LeftClick(t.x + 10.f, t.y + t.h * 0.5f);
	EXPECT_TRUE(h.IsOpen(submenu));
	// Le sous-menu se déploie à DROITE de son déclencheur.
	EXPECT_TRUE(h.Screen(submenu).x >= t.x + t.w - 1e-3f);

	ecs::Entity leaf = h.Child(submenu, 0);
	const sdl3::FRect l = h.Screen(leaf);
	h.LeftClick(l.x + 10.f, l.y + l.h * 0.5f);
	EXPECT_EQ(created, 1);
	EXPECT_FALSE(h.IsOpen(submenu));
	EXPECT_FALSE(h.IsOpen(menu));
}

// ─────────────────────────────────────────────────────────────────────────
// 7. Une modale ouverte bloque les menus contextuels de ce qui est derrière.
// ─────────────────────────────────────────────────────────────────────────
TEST(ContextMenu, AnOpenModalBlocksContextMenusBehindIt) {
	Harness h;
	int behindCalls = 0;
	auto row = h.f.Button("Derrière");
	row.Anchor(ui::Anchor::TopLeft).Offset(10.f, 10.f).Size(200.f, 30.f);
	row.OnContextMenu([&](float, float) { ++behindCalls; });
	(void)row.Spawn();

	auto dialog = h.f.Panel();
	dialog.Size(200.f, 100.f);
	ecs::Entity modal = h.f.Modal(std::move(dialog)).Spawn();
	h.input.OpenModal(h.ar, h.layout, modal);
	h.Layout();

	h.RightClick(50.f, 20.f);
	EXPECT_EQ(behindCalls, 0);

	h.input.CloseModal(h.ar, h.layout, modal);
	h.Layout();
	h.RightClick(50.f, 20.f);
	EXPECT_EQ(behindCalls, 1);
}

int main() { return RUN_ALL_TESTS(); }
