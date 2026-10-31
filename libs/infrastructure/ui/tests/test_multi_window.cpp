// ui::WindowManager : plusieurs fenêtres, une interface (monde ECS, CSS,
// focus) par fenêtre, routage des évènements, raccourcis globaux, fermeture,
// échelle HiDPI. Contextes sans fenêtre d'abord, puis de vraies fenêtres SDL
// sur le pilote vidéo « offscreen » (capture du bureau vérifiée au pixel).
#include <filesystem>

#include <SDL3_image/SDL_image.h>
#include <ui/window_manager.hpp>

#include "test.hpp"

namespace {

ui::WindowSpec Headless(const char *title, const char *role, bool primary = false) {
	ui::WindowSpec spec;
	spec.title = title;
	spec.role = role;
	spec.width = 800;
	spec.height = 600;
	spec.primary = primary;
	spec.headless = true;
	return spec;
}

sdl3::Event Event(SDL_EventType type) {
	sdl3::Event ev;
	ev.raw.type = type;
	ev.raw.common.timestamp = 1'000'000'000ull;
	return ev;
}

sdl3::Event Motion(SDL_WindowID window, float x, float y) {
	sdl3::Event ev = Event(SDL_EVENT_MOUSE_MOTION);
	ev.raw.motion.windowID = window;
	ev.raw.motion.x = x;
	ev.raw.motion.y = y;
	return ev;
}

sdl3::Event Key(SDL_WindowID window, SDL_Keycode key, SDL_Keymod mod) {
	sdl3::Event ev = Event(SDL_EVENT_KEY_DOWN);
	ev.raw.key.windowID = window;
	ev.raw.key.key = key;
	ev.raw.key.mod = mod;
	ev.raw.key.down = true;
	return ev;
}

sdl3::Event WindowEvent(SDL_EventType type, SDL_WindowID window) {
	sdl3::Event ev = Event(type);
	ev.raw.window.windowID = window;
	return ev;
}

void Click(ui::WindowManager &windows, ui::WindowContext &w, float x, float y) {
	sdl3::Event move = Motion(w.RoutingId(), x, y);
	windows.Dispatch(move);
	sdl3::Event down = Event(SDL_EVENT_MOUSE_BUTTON_DOWN);
	down.raw.button.windowID = w.RoutingId();
	down.raw.button.button = SDL_BUTTON_LEFT;
	down.raw.button.down = true;
	down.raw.button.x = x;
	down.raw.button.y = y;
	down.raw.button.clicks = 1;
	windows.Dispatch(down);
	sdl3::Event up = down;
	up.raw.type = SDL_EVENT_MOUSE_BUTTON_UP;
	up.raw.button.down = false;
	windows.Dispatch(up);
	w.Gui().Layout();
}

} // namespace

TEST(MultiWindow, CreateCloseAndStableIds) {
	ui::WindowManager windows;
	std::vector<ui::WindowId> ids;
	for (int i = 0; i < 4; ++i) {
		auto w = windows.Create(Headless("Fenêtre", "panel"));
		REQUIRE(w.IsOk());
		ids.push_back(w.Value()->Id());
	}
	CHECK_EQ(windows.Count(), size_t(4));
	for (size_t i = 1; i < ids.size(); ++i)
		CHECK(ids[i] != ids[i - 1]);

	int detached = 0;
	ui::WindowContext *second = windows.Find(ids[1]);
	REQUIRE(second != nullptr);
	second->onClose = [&](ui::WindowContext &closing) {
		// Appelé AVANT la destruction, fenêtre déjà retirée du registre.
		CHECK_EQ(closing.Id(), ids[1]);
		CHECK(windows.Find(ids[1]) == nullptr);
		CHECK(closing.Root().Valid());
		++detached;
	};
	CHECK(windows.Close(ids[1]));
	CHECK_EQ(detached, 1);
	CHECK(!windows.Close(ids[1]));
	CHECK_EQ(windows.Count(), size_t(3));
	CHECK(windows.Find(ids[1]) == nullptr);

	// Un identifiant n'est jamais réutilisé.
	auto again = windows.Create(Headless("Nouvelle", "panel"));
	REQUIRE(again.IsOk());
	for (ui::WindowId old : ids)
		CHECK(again.Value()->Id() != old);
	// Ordre de création conservé.
	std::vector<ui::WindowContext *> all = windows.Windows();
	REQUIRE(all.size() == size_t(4));
	CHECK_EQ(all[0]->Id(), ids[0]);
	CHECK_EQ(all[3]->Id(), again.Value()->Id());
}

TEST(MultiWindow, OneEcsWorldPerWindow) {
	ui::WindowManager windows;
	ui::WindowContext *a = windows.Create(Headless("A", "studio")).Value();
	ui::WindowContext *b = windows.Create(Headless("B", "game")).Value();
	CHECK(&a->World() != &b->World());
	CHECK(&a->Gui().Ctx() != &b->Gui().Ctx());
	a->Gui().Mount(ui::Box().Id("only-in-a"));
	CHECK(a->Gui().Find("#only-in-a").IsSome());
	CHECK(b->Gui().Find("#only-in-a").IsNone());
	CHECK_EQ(windows.FindByRole("game"), b);
	CHECK(windows.FindByRole("absent") == nullptr);
}

TEST(MultiWindow, StyleSheetsAreIsolated) {
	ui::WindowManager windows;
	ui::WindowSpec studio = Headless("Studio", "studio");
	studio.css = "#probe { width: 321px; height: 40px; }";
	ui::WindowSpec game = Headless("Jeu", "game");
	game.css = "#probe { width: 77px; height: 40px; }";
	ui::WindowContext *a = windows.Create(studio).Value();
	ui::WindowContext *b = windows.Create(game).Value();
	ui::Widget pa = a->Gui().Mount(ui::Column().Children({ui::Box().Id("probe")}));
	ui::Widget pb = b->Gui().Mount(ui::Column().Children({ui::Box().Id("probe")}));
	(void)pa;
	(void)pb;
	a->Gui().Layout();
	b->Gui().Layout();
	CHECK_NEAR(a->Gui().Find("#probe").Value().Bounds().w, 321.f, 0.5f);
	CHECK_NEAR(b->Gui().Find("#probe").Value().Bounds().w, 77.f, 0.5f);
}

TEST(MultiWindow, EventsRouteToTheirWindow) {
	ui::WindowManager windows;
	ui::WindowContext *a = windows.Create(Headless("A", "studio", true)).Value();
	ui::WindowContext *b = windows.Create(Headless("B", "game")).Value();

	sdl3::Event toB = Motion(b->RoutingId(), 120.f, 80.f);
	ui::DispatchResult r = windows.Dispatch(toB);
	CHECK_EQ(r.target, b);
	CHECK_NEAR(b->Gui().Ctx().input.pointer.x, 120.f, 0.01f);
	CHECK(a->Gui().Ctx().input.pointer.x < 0.f); // A n'a rien vu

	// Focus : la fenêtre active reçoit les évènements sans fenêtre.
	CHECK_EQ(windows.ActiveWindow(), a);
	sdl3::Event focus = WindowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED, b->RoutingId());
	windows.Dispatch(focus);
	CHECK_EQ(windows.ActiveWindow(), b);
	sdl3::Event pad = Event(SDL_EVENT_GAMEPAD_BUTTON_DOWN);
	CHECK_EQ(windows.Dispatch(pad).target, b);

	// Évènement de fenêtre inconnue : pas redistribué.
	sdl3::Event stray = WindowEvent(SDL_EVENT_WINDOW_MOUSE_ENTER, 0x7777u);
	CHECK(windows.Dispatch(stray).target == nullptr);

	// La disposition du bureau suit les déplacements.
	sdl3::Event moved = WindowEvent(SDL_EVENT_WINDOW_MOVED, b->RoutingId());
	moved.raw.window.data1 = 1920;
	moved.raw.window.data2 = 40;
	windows.Dispatch(moved);
	CHECK_EQ(b->DesktopPosition().x, 1920);
	CHECK_EQ(b->DesktopPosition().y, 40);
	CHECK_EQ(a->DesktopPosition().x, 0);

	CHECK(windows.Focus(a->Id()));
	CHECK_EQ(windows.ActiveWindow(), a);
	CHECK(!windows.Focus(9999u));
}

TEST(MultiWindow, GlobalShortcutsWorkInEveryWindow) {
	ui::WindowManager windows;
	ui::WindowContext *a = windows.Create(Headless("A", "studio", true)).Value();
	ui::WindowContext *b = windows.Create(Headless("B", "game")).Value();
	std::vector<ui::WindowId> origins;
	windows.AddGlobalShortcut({SDLK_Q, SDL_KMOD_CTRL, "quit", [&](ui::WindowContext &o) { origins.push_back(o.Id()); }});
	windows.AddGlobalShortcut({SDLK_W, SDL_KMOD_NONE, "gizmo.move", [&](ui::WindowContext &o) { origins.push_back(o.Id()); }});

	sdl3::Event qa = Key(a->RoutingId(), SDLK_Q, SDL_KMOD_LCTRL);
	sdl3::Event qb = Key(b->RoutingId(), SDLK_Q, SDL_KMOD_RCTRL); // droite = gauche
	CHECK_EQ(windows.Dispatch(qa).shortcut, String("quit"));
	CHECK(qa.IsConsumed());
	CHECK_EQ(windows.Dispatch(qb).shortcut, String("quit"));
	REQUIRE(origins.size() == size_t(2));
	CHECK_EQ(origins[0], a->Id());
	CHECK_EQ(origins[1], b->Id());

	// Modificateur en trop : pas le raccourci.
	sdl3::Event qShift = Key(a->RoutingId(), SDLK_Q, SDL_Keymod(SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT));
	CHECK(windows.Dispatch(qShift).shortcut.IsEmpty());

	// Champ de texte focalisé dans B : « W » est de la saisie, Ctrl+Q passe.
	ui::Widget input = b->Gui().Mount(ui::Column().Children({ui::Input("nom").Id("name").Size(200.f, 30.f)}));
	(void)input;
	b->Gui().Layout();
	const sdl3::FRect box = b->Gui().Find("#name").Value().Bounds();
	Click(windows, *b, box.x + box.w * 0.5f, box.y + box.h * 0.5f);
	CHECK(b->TextFieldHasFocus());
	CHECK(b->KeyboardCapture());
	CHECK(!a->TextFieldHasFocus()); // focus LOCAL à la fenêtre
	origins.clear();
	sdl3::Event wInB = Key(b->RoutingId(), SDLK_W, SDL_KMOD_NONE);
	CHECK(windows.Dispatch(wInB).shortcut.IsEmpty());
	sdl3::Event wInA = Key(a->RoutingId(), SDLK_W, SDL_KMOD_NONE);
	CHECK_EQ(windows.Dispatch(wInA).shortcut, String("gizmo.move"));
	sdl3::Event qInB = Key(b->RoutingId(), SDLK_Q, SDL_KMOD_LCTRL);
	CHECK_EQ(windows.Dispatch(qInB).shortcut, String("quit"));
	REQUIRE(origins.size() == size_t(2));
	CHECK_EQ(origins[0], a->Id());
	CHECK_EQ(origins[1], b->Id());

	CHECK(windows.RemoveGlobalShortcut("gizmo.move"));
	CHECK(!windows.RemoveGlobalShortcut("gizmo.move"));
}

TEST(MultiWindow, CloseRequestsPolicy) {
	ui::WindowManager windows;
	ui::WindowContext *studio = windows.Create(Headless("Studio", "studio", true)).Value();
	ui::WindowContext *game = windows.Create(Headless("Jeu", "game")).Value();
	const ui::WindowId gameId = game->Id();
	bool gameDetached = false;
	game->onClose = [&](ui::WindowContext &) { gameDetached = true; };

	// Fermer une fenêtre secondaire : elle seule disparaît.
	sdl3::Event closeGame = WindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED, game->RoutingId());
	windows.Dispatch(closeGame);
	CHECK(game->CloseRequested());
	CHECK_EQ(windows.HandleCloseRequests(), size_t(1));
	CHECK(gameDetached);
	CHECK(windows.Find(gameId) == nullptr);
	CHECK(!windows.QuitRequested());

	// Fermer la principale : arrêt demandé, la fenêtre reste à l'hôte.
	sdl3::Event closeStudio = WindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED, studio->RoutingId());
	windows.Dispatch(closeStudio);
	CHECK_EQ(windows.HandleCloseRequests(), size_t(0));
	CHECK(windows.QuitRequested());
	CHECK_EQ(windows.Count(), size_t(1));

	// La dernière fenêtre, même secondaire, arrête aussi.
	ui::WindowManager other;
	ui::WindowContext *only = other.Create(Headless("Seule", "inspector")).Value();
	only->RequestClose();
	other.HandleCloseRequests();
	CHECK(other.QuitRequested());

	ui::WindowManager quitting;
	sdl3::Event quit = Event(SDL_EVENT_QUIT);
	quitting.Dispatch(quit);
	CHECK(quitting.QuitRequested());
}

TEST(MultiWindow, ContentScalePerWindow) {
	ui::WindowManager windows;
	ui::WindowContext *hidpi = windows.Create(Headless("HiDPI", "studio")).Value();
	ui::WindowContext *standard = windows.Create(Headless("Standard", "game")).Value();
	hidpi->SetContentScale(2.f);
	CHECK_NEAR(hidpi->ContentScale(), 2.f, 1e-6f);
	CHECK_NEAR(standard->ContentScale(), 1.f, 1e-6f);
	// 800×600 pixels à l'échelle 2 : 400×300 unités logiques.
	CHECK_NEAR(hidpi->Gui().Viewport().x, 400.f, 0.01f);
	CHECK_NEAR(hidpi->Gui().Viewport().y, 300.f, 0.01f);
	CHECK_NEAR(standard->Gui().Viewport().x, 800.f, 0.01f);
	hidpi->SetContentScale(1.f);
	CHECK_NEAR(hidpi->Gui().Viewport().x, 800.f, 0.01f);
	hidpi->SetContentScale(-3.f); // invalide : 1
	CHECK_NEAR(hidpi->ContentScale(), 1.f, 1e-6f);
}

// ── Vraies fenêtres SDL (pilote vidéo offscreen) ─────────────────────────────

namespace {

struct OffscreenVideo {
	bool ok = false;
	OffscreenVideo() {
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
		ok = SDL_InitSubSystem(SDL_INIT_VIDEO);
	}
	~OffscreenVideo() {
		if (ok)
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
		SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
	}
};

ui::WindowSpec Real(const char *title, int x, int y, const char *driver) {
	ui::WindowSpec spec;
	spec.title = title;
	spec.width = 160;
	spec.height = 120;
	spec.position = Some(sdl3::Point{x, y});
	spec.renderDriver = driver;
	spec.vsync = false;
	spec.followDisplayScale = false;
	return spec;
}

bool PixelIs(SDL_Surface *s, int x, int y, Uint8 r, Uint8 g, Uint8 b) {
	Uint8 pr = 0, pg = 0, pb = 0, pa = 0;
	if (!SDL_ReadSurfacePixel(s, x, y, &pr, &pg, &pb, &pa))
		return false;
	return std::abs(int(pr) - r) <= 2 && std::abs(int(pg) - g) <= 2 && std::abs(int(pb) - b) <= 2;
}

} // namespace

TEST(MultiWindow, RealWindowsSideBySideDesktopCapture) {
	OffscreenVideo video;
	if (!video.ok) {
		SKIP("pilote vidéo offscreen indisponible");
		return;
	}
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ui_multi_window_test";
	std::filesystem::create_directories(dir);
	const String desktopPath((dir / "desktop.png").string().c_str());
	const String studioPath((dir / "studio.png").string().c_str());
	{
		ui::WindowManager windows;
		auto studio = windows.Create(Real("Studio", 0, 0, "software"));
		REQUIRE(studio.IsOk());
		auto game = windows.Create(Real("Jeu", 200, 0, "software"));
		REQUIRE(game.IsOk());
		ui::WindowContext *s = studio.Value();
		ui::WindowContext *g = game.Value();
		CHECK(!s->IsHeadless());
		CHECK(s->RoutingId() != g->RoutingId());
		CHECK_EQ(s->RoutingId(), s->Window()->GetId());
		CHECK(s->Window()->IsVisible());
		CHECK_EQ(g->DesktopPosition().x, 200);

		// Évènement réel : l'identifiant SDL de la fenêtre route vers elle.
		sdl3::Event ev = Motion(g->Window()->GetId(), 10.f, 10.f);
		CHECK_EQ(windows.Dispatch(ev).target, g);

		windows.RequestCapture(s->Id(), studioPath);
		windows.RequestDesktopCapture(desktopPath);
		windows.Render({0.f, 0.f, 0.f, 1.f}, [&](ui::WindowContext &w) {
			// Studio rouge, jeu vert : chaque fenêtre dessine dans SON renderer.
			(void)w.Renderer()->SetDrawColor(w.Id() == s->Id() ? sdl3::FColor{1.f, 0.f, 0.f, 1.f}
																: sdl3::FColor{0.f, 1.f, 0.f, 1.f});
			w.Renderer()->Clear();
		});
		for (const String &e : windows.CaptureErrors())
			std::fprintf(stderr, "\n  %s", e.CStr());
		CHECK(windows.CaptureErrors().empty());
	}
	SDL_Surface *desktop = IMG_Load(desktopPath.CStr());
	REQUIRE(desktop != nullptr);
	CHECK_EQ(desktop->w, 360); // 0..160 et 200..360
	CHECK_EQ(desktop->h, 120);
	CHECK(PixelIs(desktop, 80, 60, 255, 0, 0));  // studio
	CHECK(PixelIs(desktop, 280, 60, 0, 255, 0)); // jeu
	CHECK(PixelIs(desktop, 180, 60, 0, 0, 0));   // entre les deux : vide
	SDL_DestroySurface(desktop);
	SDL_Surface *single = IMG_Load(studioPath.CStr());
	REQUIRE(single != nullptr);
	CHECK_EQ(single->w, 160);
	CHECK(PixelIs(single, 10, 10, 255, 0, 0));
	SDL_DestroySurface(single);
	std::filesystem::remove_all(dir);
}

TEST(MultiWindow, RealWindowsShareOneGpuDevice) {
	OffscreenVideo video;
	if (!video.ok) {
		SKIP("pilote vidéo offscreen indisponible");
		return;
	}
	ui::WindowManager windows;
	auto a = windows.Create(Real("A", 0, 0, "gpu"));
	REQUIRE(a.IsOk());
	if (!a.Value()->SharesGpuDevice()) {
		SKIP("pas de device GPU sur ce pilote vidéo (repli sur le renderer par défaut vérifié)");
		return;
	}
	auto b = windows.Create(Real("B", 200, 0, "gpu"));
	REQUIRE(b.IsOk());
	CHECK(b.Value()->SharesGpuDevice());
	SDL_GPUDevice *device = windows.GpuDevice();
	REQUIRE(device != nullptr);
	CHECK_EQ(SDL_GetGPURendererDevice(a.Value()->Renderer()->Get()), device);
	CHECK_EQ(SDL_GetGPURendererDevice(b.Value()->Renderer()->Get()), device);
	// Fermer la PREMIÈRE fenêtre ne détruit pas le device des autres.
	CHECK(windows.Close(a.Value()->Id()));
	windows.Render();
	CHECK_EQ(SDL_GetGPURendererDevice(b.Value()->Renderer()->Get()), device);
}
