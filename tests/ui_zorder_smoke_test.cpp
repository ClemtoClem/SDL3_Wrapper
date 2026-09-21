// Tests unitaires — blocage des évènements par ordre d'affichage (z-order)
// dans ui::InputSystem (lib/include/ui/systems.hpp).
//
// Avant ce mécanisme, chaque passe Query<Widget, UiComputed> de Dispatch()
// décidait du survol avec le SEUL rectangle du widget : deux widgets
// superposés (item de menu au-dessus d'un bouton, popup par-dessus un
// panneau...) devenaient tous les deux survolés, et l'ordre des clics
// dépendait de l'ordre des types dans Dispatch, pas de ce que l'utilisateur
// voit. On vérifie ici que seul le widget le plus en AVANT — et ses ancêtres
// — reçoit l'évènement, dans l'ordre de dessin exact de RenderSystem::Run.
//
// Aucun GPU ni fenêtre : layout + input sont purement calculatoires (pilote
// vidéo "dummy"). Toutes les tailles/positions sont posées explicitement pour
// ne pas dépendre de la mesure du texte (aucune police n'est chargée ici).
#define USE_TEST

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>

// ─── Évènements SDL synthétiques (même forme que ui_smoke_test_6/7) ────────
static sdl3::Event MouseMoveEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_MOTION;
	ev.raw.motion.x = x;
	ev.raw.motion.y = y;
	return ev;
}
static sdl3::Event MouseDownEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = SDL_BUTTON_LEFT;
	ev.raw.button.clicks = 1;
	return ev;
}
static sdl3::Event MouseUpEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_BUTTON_UP;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = SDL_BUTTON_LEFT;
	return ev;
}
static sdl3::Event WheelEv(float x, float y, float wheelY) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_WHEEL;
	ev.raw.wheel.mouse_x = x;
	ev.raw.wheel.mouse_y = y;
	ev.raw.wheel.y = wheelY;
	return ev;
}

/// Monde UI minimal : registre + layout + input + factory, tout prêt.
struct Harness {
	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::UiFactory f{ar, layout};

	Harness() { setenv("SDL_VIDEODRIVER", "dummy", 0); }

	void Layout() { layout.Run(ar, 800.f, 600.f); }
	void Move(float x, float y) { input.HandleEvent(ar, MouseMoveEv(x, y), layout); }
	void Click(float x, float y) {
		input.HandleEvent(ar, MouseDownEv(x, y), layout);
		input.HandleEvent(ar, MouseUpEv(x, y), layout);
	}
	void Wheel(float x, float y, float dy) {
		Move(x, y); // la molette n'emporte pas de position dans InputSystem
		input.HandleEvent(ar, WheelEv(x, y, dy), layout);
	}
	[[nodiscard]] bool ButtonHovered(ecs::Entity e) {
		auto b = ar.GetComponent<ui::UiButton>(e);
		return b.IsSome() && b.Unwrap()->hovered;
	}
	[[nodiscard]] bool MenuItemHovered(ecs::Entity e) {
		auto mi = ar.GetComponent<ui::UiMenuItem>(e);
		return mi.IsSome() && mi.Unwrap()->hovered;
	}
};

// Popup ouvert recouvrant exactement `rect`, contenant UN item de menu de la
// même taille — la configuration décrite par la demande : "seul l'UiMenuItem
// survolé traite l'évènement".
struct Overlay {
	ecs::Entity popup{};
	ecs::Entity item{};
};
static Overlay SpawnMenuOverlay(Harness &h, float x, float y, float w, float wItem, int order = 10) {
	auto itemB = h.f.MenuItem("Ouvrir");
	itemB.Size(wItem, 30.f);
	auto popupB = h.f.Popup();
	popupB.Anchor(ui::Anchor::TopLeft).Offset(x, y).Size(w, 30.f).Pad(math::Sides{0.f});
	popupB.OverlayOrder(order);
	Overlay ov;
	ov.popup = popupB.Children(std::move(itemB)).Spawn();
	if (auto kids = h.ar.GetComponent<ui::UiChildren>(ov.popup); kids.IsSome() && !kids.Unwrap()->list.empty())
		ov.item = kids.Unwrap()->list[0];
	ui::OpenPopup(h.ar, h.layout, ov.popup, ecs::Entity{});
	return ov;
}

// ─────────────────────────────────────────────────────────────────────────
// 1. Le cas de la demande : un item de menu par-dessus un bouton.
// ─────────────────────────────────────────────────────────────────────────
TEST(UiZOrder, MenuItemOnTopIsTheOnlyWidgetHovered) {
	Harness h;
	int buttonClicks = 0, itemClicks = 0;
	auto btnB = h.f.Button("Jouer");
	btnB.Anchor(ui::Anchor::TopLeft).Offset(40.f, 40.f).Size(200.f, 40.f).OnClick([&] { buttonClicks++; });
	ecs::Entity btn = btnB.Spawn();

	Overlay ov = SpawnMenuOverlay(h, 40.f, 45.f, 200.f, 200.f);
	if (auto cbk = h.ar.GetOrAddComponent(ov.item, ui::UiCallbacks{}); cbk.IsSome())
		cbk.Unwrap()->onClick = [&] { itemClicks++; };
	h.Layout();

	// (60, 55) est DANS les deux : le bouton est derrière, l'item devant.
	h.Move(60.f, 55.f);
	EXPECT_TRUE(h.MenuItemHovered(ov.item));
	EXPECT_TRUE(!h.ButtonHovered(btn));
	EXPECT_TRUE(h.input.FrontMostWidget() == ov.item);
	EXPECT_TRUE(h.input.PointerOverUi());

	h.Click(60.f, 55.f);
	EXPECT_EQ(itemClicks, 1);
	EXPECT_EQ(buttonClicks, 0);
}

// Hors de l'item, le bouton redevient normalement survolable.
TEST(UiZOrder, ButtonStillReactsWhereNothingCoversIt) {
	Harness h;
	int buttonClicks = 0;
	auto btnB = h.f.Button("Jouer");
	btnB.Anchor(ui::Anchor::TopLeft).Offset(40.f, 40.f).Size(200.f, 40.f).OnClick([&] { buttonClicks++; });
	ecs::Entity btn = btnB.Spawn();
	SpawnMenuOverlay(h, 40.f, 45.f, 60.f, 60.f); // ne couvre que la gauche du bouton
	h.Layout();

	h.Move(200.f, 55.f);
	EXPECT_TRUE(h.ButtonHovered(btn));
	EXPECT_TRUE(h.input.FrontMostWidget() == btn);
	h.Click(200.f, 55.f);
	EXPECT_EQ(buttonClicks, 1);
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Cible invalide au-dessus du vide — ce que l'application interroge pour
//    savoir si l'UI capte le pointeur (viewport 3D, carte...).
// ─────────────────────────────────────────────────────────────────────────
TEST(UiZOrder, PointerOverEmptySpaceHasNoTarget) {
	Harness h;
	auto btnB = h.f.Button("Jouer");
	btnB.Anchor(ui::Anchor::TopLeft).Offset(40.f, 40.f).Size(200.f, 40.f);
	btnB.Spawn();
	h.Layout();

	h.Move(700.f, 500.f);
	EXPECT_TRUE(!h.input.FrontMostWidget().Valid());
	EXPECT_TRUE(!h.input.PointerOverUi());
	EXPECT_TRUE(!ui::HitTestTopMost(h.ar, sdl3::FPoint{700.f, 500.f}).Valid());
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Un ANCÊTRE du widget de premier plan garde ses évènements : sans cela,
//    un conteneur scrollable perdrait la molette dès que le curseur est sur
//    un de ses enfants (c'est-à-dire presque toujours).
// ─────────────────────────────────────────────────────────────────────────
TEST(UiZOrder, AncestorOfTheFrontWidgetStillGetsTheWheel) {
	Harness h;
	auto panelB = h.f.Panel();
	panelB.Anchor(ui::Anchor::TopLeft).Offset(20.f, 20.f).Size(220.f, 100.f).Scrollable().Pad(math::Sides{0.f}).Gap(0.f);
	ecs::Entity first{};
	{
		auto b1 = h.f.Button("A");
		b1.Size(200.f, 60.f);
		auto b2 = h.f.Button("B");
		b2.Size(200.f, 60.f);
		auto b3 = h.f.Button("C");
		b3.Size(200.f, 60.f);
		panelB.Children(std::move(b1), std::move(b2), std::move(b3));
	}
	ecs::Entity panel = panelB.Spawn();
	if (auto kids = h.ar.GetComponent<ui::UiChildren>(panel); kids.IsSome())
		first = kids.Unwrap()->list[0];
	h.Layout();

	h.Move(100.f, 50.f);
	EXPECT_TRUE(h.input.FrontMostWidget() == first); // l'enfant est devant
	EXPECT_TRUE(h.ButtonHovered(first));

	float before = h.ar.GetComponent<ui::UiRect>(panel).Unwrap()->scroll.y;
	h.Wheel(100.f, 50.f, -1.f);
	float after = h.ar.GetComponent<ui::UiRect>(panel).Unwrap()->scroll.y;
	EXPECT_TRUE(after > before);
}

// ─────────────────────────────────────────────────────────────────────────
// 4. UiPointerThrough : décoration posée par-dessus une zone interactive.
// ─────────────────────────────────────────────────────────────────────────
TEST(UiZOrder, PointerThroughOverlayLetsTheWidgetBehindReact) {
	Harness h;
	auto btnB = h.f.Button("Jouer");
	btnB.Anchor(ui::Anchor::TopLeft).Offset(40.f, 40.f).Size(200.f, 40.f);
	ecs::Entity btn = btnB.Spawn();

	auto veilB = h.f.Panel();
	veilB.Fixed().Anchor(ui::Anchor::TopLeft).Offset(40.f, 40.f).Size(200.f, 40.f).PointerThrough();
	ecs::Entity veil = veilB.Spawn();
	h.Layout();

	h.Move(60.f, 55.f);
	EXPECT_TRUE(h.input.FrontMostWidget() == btn);
	EXPECT_TRUE(h.ButtonHovered(btn));

	// Le même voile SANS le marqueur bloque, lui.
	h.ar.RemoveComponent<ui::UiPointerThrough>(veil);
	h.Move(60.f, 55.f);
	EXPECT_TRUE(h.input.FrontMostWidget() == veil);
	EXPECT_TRUE(!h.ButtonHovered(btn));
}

// ─────────────────────────────────────────────────────────────────────────
// 5. Entre deux overlays superposés, c'est UiOverlayLayer::order qui tranche
//    — exactement comme à l'affichage (RenderSystem::Run les trie pareil).
// ─────────────────────────────────────────────────────────────────────────
TEST(UiZOrder, HigherOverlayOrderWinsOverLowerOne) {
	Harness h;
	Overlay low = SpawnMenuOverlay(h, 40.f, 40.f, 200.f, 200.f, /*order=*/5);
	Overlay high = SpawnMenuOverlay(h, 40.f, 40.f, 200.f, 200.f, /*order=*/20);
	h.Layout();

	h.Move(60.f, 50.f);
	EXPECT_TRUE(h.input.FrontMostWidget() == high.item);
	EXPECT_TRUE(h.MenuItemHovered(high.item));
	EXPECT_TRUE(!h.MenuItemHovered(low.item));
}

// Un overlay fermé (UiHidden) n'est pas dessiné : il ne doit rien bloquer.
TEST(UiZOrder, ClosedPopupDoesNotBlockWhatIsUnderIt) {
	Harness h;
	auto btnB = h.f.Button("Jouer");
	btnB.Anchor(ui::Anchor::TopLeft).Offset(40.f, 40.f).Size(200.f, 40.f);
	ecs::Entity btn = btnB.Spawn();
	Overlay ov = SpawnMenuOverlay(h, 40.f, 40.f, 200.f, 200.f);
	ui::ClosePopup(h.ar, h.layout, ov.popup);
	h.Layout();

	h.Move(60.f, 50.f);
	EXPECT_TRUE(h.input.FrontMostWidget() == btn);
	EXPECT_TRUE(h.ButtonHovered(btn));
	EXPECT_TRUE(!h.MenuItemHovered(ov.item));
}

// Un widget DÉSACTIVÉ au premier plan reste inerte… mais continue de bloquer
// ce qui est derrière lui : il est visible, donc il capte le pointeur.
TEST(UiZOrder, DisabledFrontWidgetStaysInertAndStillBlocks) {
	Harness h;
	auto btnB = h.f.Button("Jouer");
	btnB.Anchor(ui::Anchor::TopLeft).Offset(40.f, 40.f).Size(200.f, 40.f);
	ecs::Entity btn = btnB.Spawn();
	Overlay ov = SpawnMenuOverlay(h, 40.f, 40.f, 200.f, 200.f);
	ui::SetEnabled(h.ar, ov.item, false);
	h.Layout();

	h.Move(60.f, 50.f);
	EXPECT_TRUE(!h.MenuItemHovered(ov.item));
	EXPECT_TRUE(!h.ButtonHovered(btn));
}

// ─────────────────────────────────────────────────────────────────────────
// 6. La liste déroulante d'un combo ouvert est dessinée par-dessus TOUT
//    (dernière passe de RenderSystem::Run) et son rectangle n'est pas celui
//    du widget : le hit-test doit la traiter à part.
// ─────────────────────────────────────────────────────────────────────────
TEST(UiZOrder, OpenComboDropdownBlocksTheWidgetsUnderIt) {
	Harness h;
	auto comboB = h.f.Combo({String("Un"), String("Deux"), String("Trois")}, 0);
	comboB.Anchor(ui::Anchor::TopLeft).Offset(40.f, 40.f).Size(200.f, 30.f);
	ecs::Entity combo = comboB.Spawn();
	auto btnB = h.f.Button("Jouer");
	btnB.Anchor(ui::Anchor::TopLeft).Offset(40.f, 80.f).Size(200.f, 60.f);
	ecs::Entity btn = btnB.Spawn();
	h.Layout();

	h.Click(60.f, 50.f); // ouvre la liste
	ASSERT_TRUE(h.ar.GetComponent<ui::UiComboBox>(combo).Unwrap()->open);

	// (60, 90) : dans la liste déroulante ET dans le bouton.
	const sdl3::FRect dd =
		h.ar.GetComponent<ui::UiComboBox>(combo).Unwrap()->DropdownRect(h.ar.GetComponent<ui::UiComputed>(combo).Unwrap()->screen);
	ASSERT_TRUE(dd.Contains(sdl3::FPoint{60.f, 90.f}));
	h.Move(60.f, 90.f);
	EXPECT_TRUE(h.input.FrontMostWidget() == combo);
	EXPECT_TRUE(!h.ButtonHovered(btn));
	EXPECT_TRUE(h.ar.GetComponent<ui::UiComboBox>(combo).Unwrap()->hoveredItem >= 0);
}

// ─────────────────────────────────────────────────────────────────────────
// 7. Coût du filtre — garde-fou de performance.
//
// La première version reconstruisait l'ordre de dessin À CHAQUE appel :
// mesuré à 5–7 ms par test sur 1200 widgets en build de debug (-O0 + ASan),
// soit, avec deux appels par évènement souris, la chute de 60 à 25 images/s
// observée sur level_editor_demo. L'index n'est donc reconstruit que lorsque
// la géométrie change (cf. HitTestIndex::Refresh) — ces deux tests verrouillent
// cette propriété, l'un structurellement (le compteur de reconstructions),
// l'autre en temps (borne RELATIVE au coût d'une passe de layout, donc
// indépendante de la machine).
// ─────────────────────────────────────────────────────────────────────────

/// UI large : 200 rangées de 5 widgets, comme les listes d'un vrai éditeur.
static void BuildLargeUi(Harness &h) {
	auto rootB = h.f.Column();
	rootB.Fixed().Anchor(ui::Anchor::TopLeft).Pad(4).Gap(2).W(ui::Dimension::Px(600)).HAuto();
	ecs::Entity root = rootB.Spawn();
	for (int i = 0; i < 200; ++i) {
		h.f.Row()
			.Gap(4)
			.Parent(root)
			.Children(h.f.Label(String("item ").Append(i)),
					  h.f.Progress(0.f, 1.f, float(i % 100) / 100.f).H(ui::Dimension::Px(6)).GrowW(),
					  h.f.Button("x").FontSize(10), h.f.Checkbox(i % 2 == 0), h.f.Toggle(i % 3 == 0))
			.Spawn();
	}
}

TEST(UiZOrder, IndexIsRebuiltOnlyWhenGeometryOrEntityCountChanges) {
	Harness h;
	BuildLargeUi(h);
	h.Layout();

	h.Move(100.f, 300.f);
	const uint64_t afterFirst = h.input.HitIndex().Revision();
	EXPECT_TRUE(afterFirst > 0);
	// L'index ne contient que ce qui est réellement DESSINÉ : les rangées qui
	// débordent sous le bas de la fenêtre ont un clip vide et sont écartées,
	// exactement comme RenderSystem::DrawTree les écarte. Il est donc bien
	// plus court que les 1201 entités de l'arbre.
	std::cout << "index = " << h.input.HitIndex().Size() << " widgets pour " << h.ar.EntityCount()
			  << " entites\n";
	EXPECT_TRUE(h.input.HitIndex().Size() > 50);
	EXPECT_TRUE(h.input.HitIndex().Size() <= h.ar.EntityCount());

	for (int i = 0; i < 50; ++i) {
		h.Move(100.f + float(i), 300.f + float(i % 7));
		h.input.Tick(h.ar, h.layout, 0.016f);
	}
	EXPECT_EQ(h.input.HitIndex().Revision(), afterFirst); // aucune reconstruction

	h.Layout(); // nouvelle passe de layout = géométrie neuve
	h.Move(120.f, 300.f);
	const uint64_t afterLayout = h.input.HitIndex().Revision();
	EXPECT_TRUE(afterLayout > afterFirst);

	auto extra = h.f.Button("extra"); // création d'entité sans passe de layout
	extra.Anchor(ui::Anchor::TopLeft).Offset(700.f, 40.f).Size(60.f, 20.f);
	extra.Spawn();
	h.Move(130.f, 300.f);
	EXPECT_TRUE(h.input.HitIndex().Revision() > afterLayout);
}

TEST(UiZOrder, DispatchStaysCheapOnALargeUi) {
	using Clock = std::chrono::steady_clock;
	auto ms = [](Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); };

	Harness h;
	BuildLargeUi(h);
	auto t0 = Clock::now();
	h.Layout();
	const double layoutMs = ms(t0);

	constexpr int N = 100;
	t0 = Clock::now();
	for (int i = 0; i < N; ++i) {
		h.Move(100.f + float(i % 17), 300.f + float(i % 11));
		h.input.Tick(h.ar, h.layout, 0.016f);
	}
	const double perFrameMs = ms(t0) / N;

	std::cout << "1200 widgets : layout=" << layoutMs << "ms, evenement+tick=" << perFrameMs << "ms\n";
	// Une passe de layout complète coûte des dizaines de ms ici ; traiter un
	// évènement doit rester un ordre de grandeur en dessous. Avec la
	// reconstruction par appel, ce rapport était d'environ 0,4 — il est
	// maintenant autour de 0,01.
	EXPECT_TRUE(perFrameMs < layoutMs * 0.1);
}

int main() { return RUN_ALL_TESTS(); }
