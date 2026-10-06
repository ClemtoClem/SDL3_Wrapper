// Tests unitaires — clic des lignes sélectionnables (UiSelectable, en-tête de
// UiTreeNode) dans ui::InputSystem (lib/include/ui/systems.hpp).
//
// `WidgetBuilder::OnClick` posé sur un Selectable n'était jamais appelé :
// seuls les boutons, pastilles et items de menu déclenchaient `onClick`. Or
// c'est exactement le geste d'une ligne d'arbre de scène, d'une liste de
// scènes ou d'une arborescence de dossiers — toutes inertes dans l'éditeur.
// On vérifie ici le contrat : appui PUIS relâchement sur la ligne ; ni un
// relâchement ailleurs, ni un glisser-déposer, ni un clic sur un bouton
// placé DANS la ligne ne valent clic de la ligne.
//
// Aucun GPU ni fenêtre (pilote vidéo "dummy"), tailles posées explicitement.
#define USE_TEST

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include <cstdlib>

static sdl3::Event MouseMoveEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_MOTION;
	ev.raw.motion.x = x;
	ev.raw.motion.y = y;
	return ev;
}
static sdl3::Event MouseButtonEv(bool down, float x, float y, int clicks = 1) {
	sdl3::Event ev{};
	ev.raw.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = SDL_BUTTON_LEFT;
	ev.raw.button.clicks = uint8_t(clicks);
	return ev;
}

struct Harness {
	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::UiFactory f{ar, layout};

	Harness() { setenv("SDL_VIDEODRIVER", "dummy", 0); }

	void Layout() { layout.Run(ar, 800.f, 600.f); }
	void Move(float x, float y) { input.HandleEvent(ar, MouseMoveEv(x, y), layout); }
	void Down(float x, float y, int clicks = 1) {
		input.HandleEvent(ar, MouseButtonEv(true, x, y, clicks), layout);
	}
	void Up(float x, float y) { input.HandleEvent(ar, MouseButtonEv(false, x, y), layout); }
	void Click(float x, float y) {
		Move(x, y);
		Down(x, y);
		Up(x, y);
	}

	/// Une ligne de 300 × 30 à (0, y), sans UiSelection ancêtre (comme les
	/// lignes d'arbre de l'éditeur).
	ecs::Entity Row(float y, int index, int& clicks) {
		auto row = f.Selectable(String(), index);
		row.Anchor(ui::Anchor::TopLeft).Offset(0.f, y).Size(300.f, 30.f);
		row.OnClick([&clicks] { ++clicks; });
		return row.Spawn();
	}
};

TEST(UiSelectableClick, PressThenReleaseOnTheRowCallsOnClick) {
	Harness h;
	int clicks = 0;
	(void)h.Row(10.f, 0, clicks);
	h.Layout();
	h.Click(50.f, 25.f);
	EXPECT_EQ(clicks, 1);
	h.Click(50.f, 25.f);
	EXPECT_EQ(clicks, 2);
}

TEST(UiSelectableClick, ReleasingElsewhereIsNotAClick) {
	Harness h;
	int first = 0, second = 0;
	(void)h.Row(10.f, 0, first);
	(void)h.Row(50.f, 1, second);
	h.Layout();
	h.Move(50.f, 25.f);
	h.Down(50.f, 25.f);
	h.Move(50.f, 65.f);
	h.Up(50.f, 65.f);
	EXPECT_EQ(first, 0);
	EXPECT_EQ(second, 0);
}

TEST(UiSelectableClick, AButtonInsideTheRowKeepsItsOwnClick) {
	Harness h;
	int rowClicks = 0, buttonClicks = 0;
	auto button = h.f.Button("▸");
	button.Size(24.f, 24.f).OnClick([&] { ++buttonClicks; });
	auto row = h.f.Selectable(String(), 0);
	row.Anchor(ui::Anchor::TopLeft).Offset(0.f, 10.f).Size(300.f, 30.f).OnClick([&] {
		++rowClicks;
	});
	(void)row.Children(std::move(button)).Spawn();
	h.Layout();
	h.Click(20.f, 25.f); // sur le bouton (le Selectable est une rangée : il est à gauche)
	EXPECT_EQ(buttonClicks, 1);
	EXPECT_EQ(rowClicks, 0);
	h.Click(250.f, 25.f); // ailleurs sur la ligne
	EXPECT_EQ(rowClicks, 1);
}

TEST(UiSelectableClick, ADropIsNotAClick) {
	Harness h;
	int sourceClicks = 0, targetClicks = 0;
	int64_t dropped = -1;
	auto source = h.f.Selectable(String(), 0);
	source.Anchor(ui::Anchor::TopLeft).Offset(0.f, 10.f).Size(300.f, 30.f);
	source.DragPayload(String("node"), 42).OnClick([&] { ++sourceClicks; });
	(void)source.Spawn();
	auto target = h.f.Selectable(String(), 1);
	target.Anchor(ui::Anchor::TopLeft).Offset(0.f, 50.f).Size(300.f, 30.f);
	target.DropTarget(String("node")).OnDrop([&](int64_t id) { dropped = id; }).OnClick([&] {
		++targetClicks;
	});
	(void)target.Spawn();
	h.Layout();
	h.Move(50.f, 25.f);
	h.Down(50.f, 25.f);
	h.Move(52.f, 40.f);
	h.Move(55.f, 65.f);
	h.Up(55.f, 65.f);
	EXPECT_EQ(int(dropped), 42);
	EXPECT_EQ(sourceClicks, 0);
	EXPECT_EQ(targetClicks, 0);
	// Un simple clic sur la source reste un clic (le seuil de glissé n'est
	// pas franchi).
	h.Click(50.f, 25.f);
	EXPECT_EQ(sourceClicks, 1);
}

TEST(UiSelectableClick, DoubleClickCallsOnDoubleClickOnRowsAndButtons) {
	Harness h;
	int rowClicks = 0, rowDouble = 0, buttonDouble = 0;
	auto row = h.f.Selectable(String(), 0);
	row.Anchor(ui::Anchor::TopLeft).Offset(0.f, 10.f).Size(300.f, 30.f);
	row.OnClick([&] { ++rowClicks; }).OnDoubleClick([&] { ++rowDouble; });
	(void)row.Spawn();
	auto button = h.f.Button("Ouvrir");
	button.Anchor(ui::Anchor::TopLeft).Offset(0.f, 100.f).Size(120.f, 30.f).OnDoubleClick([&] {
		++buttonDouble;
	});
	(void)button.Spawn();
	h.Layout();
	h.Click(50.f, 25.f); // simple clic : pas de double
	EXPECT_EQ(rowDouble, 0);
	h.Down(50.f, 25.f, 2); // second appui rapproché (SDL : clicks = 2)
	h.Up(50.f, 25.f);
	EXPECT_EQ(rowDouble, 1);
	EXPECT_EQ(rowClicks, 2); // chaque appui-relâchement reste un clic
	h.Move(20.f, 115.f);
	h.Down(20.f, 115.f, 2);
	h.Up(20.f, 115.f);
	EXPECT_EQ(buttonDouble, 1);
}

TEST(UiSelectableClick, DragStartFiresOnceAndThePayloadFollowsThePointer) {
	Harness h;
	int starts = 0;
	auto source = h.f.Selectable(String(), 0);
	source.Anchor(ui::Anchor::TopLeft).Offset(0.f, 10.f).Size(300.f, 30.f);
	source.DragPayload(String("asset"), 7, String("mur.scene"));
	ecs::Entity entity{};
	source.OnDragStart([&] {
		++starts;
		// Moment d'ajuster le fantôme (sélection multiple).
		if (auto payload = h.ar.GetComponent<ui::UiDragPayload>(entity); payload.IsSome()) {
			payload.Unwrap()->count = 3;
			payload.Unwrap()->label = String("3 éléments");
		}
	});
	entity = source.Spawn();
	auto target = h.f.Selectable(String(), 1);
	target.Anchor(ui::Anchor::TopLeft)
		.Offset(0.f, 60.f)
		.Size(300.f, 30.f)
		.DropTarget(String("asset"), false);
	ecs::Entity targetEntity = target.Spawn();
	h.Layout();
	h.Move(50.f, 25.f);
	h.Down(50.f, 25.f);
	h.Move(52.f, 27.f); // sous le seuil : pas encore un glissé
	EXPECT_EQ(starts, 0);
	h.Move(60.f, 45.f);
	h.Move(70.f, 75.f);
	EXPECT_EQ(starts, 1);
	auto payload = h.ar.GetComponent<ui::UiDragPayload>(entity);
	ASSERT_TRUE(payload.IsSome());
	EXPECT_TRUE(payload.Unwrap()->dragging);
	EXPECT_EQ(payload.Unwrap()->label, "3 éléments");
	EXPECT_EQ(payload.Unwrap()->count, 3);
	EXPECT_TRUE(payload.Unwrap()->pointer.x == 70.f && payload.Unwrap()->pointer.y == 75.f);
	auto drop = h.ar.GetComponent<ui::UiDropTarget>(targetEntity);
	ASSERT_TRUE(drop.IsSome());
	EXPECT_TRUE(drop.Unwrap()->hovered);
	EXPECT_FALSE(drop.Unwrap()->highlight); // la cible dessine son propre indicateur
	h.Up(70.f, 75.f);
	EXPECT_FALSE(h.ar.GetComponent<ui::UiDragPayload>(entity).Unwrap()->dragging);
	EXPECT_EQ(starts, 1);
}

int main() {
	return RUN_ALL_TESTS();
}
