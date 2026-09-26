// Tests unitaires — trois règles de mise en page et d'interaction de ui:: :
//   1. la bordure d'un panneau fait partie de sa boîte (modèle border-box) ;
//   2. les barres de défilement automatiques n'apparaissent qu'en cas de
//      débordement réel, et occupent leur propre bande (UiRect::gutter) au
//      lieu de recouvrir le contenu ;
//   3. le widget qui a le focus clavier traite la frappe EN PREMIER et la
//      consomme (sdl3::Event::Consume), sauf les raccourcis que la règle de
//      transmission laisse passer à l'application (F1–F12, Ctrl+S…).
//
// Aucun GPU ni fenêtre : layout + input sont purement calculatoires (pilote
// vidéo "dummy"), toutes les tailles sont posées explicitement.
#define USE_TEST

#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include <cstdlib>

namespace {

sdl3::Event KeyEv(SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE, bool down = true) {
	sdl3::Event ev{};
	ev.raw.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	ev.raw.key.key = key;
	ev.raw.key.mod = mod;
	ev.raw.key.down = down;
	return ev;
}

sdl3::Event TextEv(const char *text) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_TEXT_INPUT;
	ev.raw.text.text = text;
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
	/// Passe l'évènement à l'interface ; vrai s'il en ressort consommé.
	bool Send(sdl3::Event ev) {
		(void)input.HandleEvent(ar, ev, layout);
		return ev.IsConsumed();
	}
	[[nodiscard]] sdl3::FRect Screen(ecs::Entity e) { return ar.GetComponent<ui::UiComputed>(e).Unwrap()->screen; }
	[[nodiscard]] const ui::UiRect &Rect(ecs::Entity e) { return *ar.GetComponent<ui::UiRect>(e).Unwrap(); }
	[[nodiscard]] ecs::Entity Child(ecs::Entity parent, size_t index) {
		return ar.GetComponent<ui::UiChildren>(parent).Unwrap()->list[index];
	}

	/// Colonne 200×100 sans marge ni espacement, dont l'unique enfant fait
	/// `childW`×`childH` (0 = étiré sur la largeur).
	ecs::Entity Scroller(float childW, float childH) {
		auto box = f.Column();
		box.Anchor(ui::Anchor::TopLeft).Offset(0.f, 0.f).Size(200.f, 100.f).Pad(0.f).Gap(0.f).Scrollable();
		{
			auto child = f.Column();
			child.Pad(0.f).H(ui::Dimension::Px(childH));
			if (childW > 0.f)
				child.W(ui::Dimension::Px(childW));
			else
				child.GrowW();
			box.Children(std::move(child));
		}
		ecs::Entity e = box.Spawn();
		Layout();
		return e;
	}

	ecs::Entity FocusedInput(ui::IOMode mode = ui::IOMode::READ_WRITE_COPY_AND_PASTE) {
		auto field = f.Input();
		field.Anchor(ui::Anchor::TopLeft).Offset(10.f, 10.f).Size(200.f, 30.f).IoMode(mode);
		ecs::Entity e = field.Spawn();
		Layout();
		ar.GetComponent<ui::UiInput>(e).Unwrap()->focused = true;
		return e;
	}
	[[nodiscard]] ui::UiInput &InputOf(ecs::Entity e) { return *ar.GetComponent<ui::UiInput>(e).Unwrap(); }
};

bool Near(float a, float b) { return sdl3::Abs(a - b) < 0.01f; }

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// 1. Bordures
// ─────────────────────────────────────────────────────────────────────────

TEST(Border, AnAutoSizedPanelGrowsByItsBorderOnEachSide) {
	Harness h;
	auto panel = h.f.Column();
	panel.Anchor(ui::Anchor::TopLeft).Offset(20.f, 20.f).Pad(0.f).Gap(0.f).BorderColor(sdl3::FColor::WHITE(), 3.f);
	{
		auto child = h.f.Column();
		child.Pad(0.f).Size(100.f, 40.f);
		panel.Children(std::move(child));
	}
	ecs::Entity e = panel.Spawn();
	h.Layout();

	const sdl3::FRect outer = h.Screen(e);
	EXPECT_TRUE(Near(outer.w, 106.f));
	EXPECT_TRUE(Near(outer.h, 46.f));
	// Le contenu commence À L'INTÉRIEUR de la bordure : il ne la recouvre pas.
	const sdl3::FRect inner = h.Screen(h.Child(e, 0));
	EXPECT_TRUE(Near(inner.x, outer.x + 3.f));
	EXPECT_TRUE(Near(inner.y, outer.y + 3.f));
	EXPECT_TRUE(inner.x + inner.w <= outer.x + outer.w - 3.f + 0.01f);
}

TEST(Border, ChildrenAreClippedInsideTheBorder) {
	Harness h;
	auto panel = h.f.Column();
	panel.Anchor(ui::Anchor::TopLeft).Offset(0.f, 0.f).Size(120.f, 80.f).Pad(0.f).BorderColor(sdl3::FColor::WHITE(), 2.f);
	ecs::Entity e = panel.Spawn();
	h.Layout();

	const sdl3::FRect clip = h.ar.GetComponent<ui::UiComputed>(e).Unwrap()->childClip;
	EXPECT_TRUE(Near(clip.x, 2.f));
	EXPECT_TRUE(Near(clip.y, 2.f));
	EXPECT_TRUE(Near(clip.w, 116.f));
	EXPECT_TRUE(Near(clip.h, 76.f));
}

TEST(Border, OnlyADrawnBorderTakesRoom) {
	Harness h;
	// Colonne nue : pas de fond, pas de bordure, rien de réservé.
	auto plain = h.f.Column();
	plain.Anchor(ui::Anchor::TopLeft).Offset(0.f, 0.f).Size(50.f, 50.f).Pad(0.f);
	ecs::Entity bare = plain.Spawn();
	// Panneau : la classe "root-panel" lui donne une bordure d'un pixel.
	auto card = h.f.Panel();
	card.Anchor(ui::Anchor::TopLeft).Offset(100.f, 0.f).Size(50.f, 50.f).Pad(0.f);
	ecs::Entity panel = card.Spawn();
	h.Layout();
	EXPECT_TRUE(Near(h.ar.GetComponent<ui::UiComputed>(bare).Unwrap()->childClip.w, 50.f));
	EXPECT_TRUE(Near(h.ar.GetComponent<ui::UiComputed>(panel).Unwrap()->childClip.w, 48.f));
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Barres de défilement
// ─────────────────────────────────────────────────────────────────────────

TEST(Scrollbar, ContentThatFitsShowsNoBar) {
	Harness h;
	ecs::Entity e = h.Scroller(0.f, 100.f);
	EXPECT_EQ(h.Rect(e).MaxScroll().y, 0.f);
	EXPECT_EQ(h.Rect(e).gutter.x, 0.f);
	EXPECT_EQ(ui::VScrollbarThumbRect(h.Rect(e), h.Screen(e)).w, 0.f);
}

TEST(Scrollbar, ASubPixelOverflowShowsNoBar) {
	Harness h;
	ecs::Entity e = h.Scroller(0.f, 100.6f); // arrondi de placement, pas un vrai débordement
	EXPECT_EQ(h.Rect(e).MaxScroll().y, 0.f);
	EXPECT_EQ(h.Rect(e).gutter.x, 0.f);
}

TEST(Scrollbar, AVerticalBarTakesItsOwnBandInsteadOfCoveringTheContent) {
	Harness h;
	ecs::Entity e = h.Scroller(0.f, 300.f);
	const ui::UiRect &r = h.Rect(e);
	EXPECT_EQ(r.gutter.x, ui::K_SCROLLBAR_THICKNESS);
	EXPECT_EQ(r.gutter.y, 0.f);

	// L'enfant étiré s'arrête AVANT la barre.
	const sdl3::FRect child = h.Screen(h.Child(e, 0));
	EXPECT_TRUE(Near(child.w, 200.f - ui::K_SCROLLBAR_THICKNESS));
	const sdl3::FRect thumb = ui::VScrollbarThumbRect(r, h.Screen(e));
	EXPECT_TRUE(thumb.w > 0.f);
	EXPECT_TRUE(child.x + child.w <= thumb.x + 0.01f);
	// Et il est découpé à la zone visible, barre exclue.
	const sdl3::FRect clip = h.ar.GetComponent<ui::UiComputed>(e).Unwrap()->childClip;
	EXPECT_TRUE(Near(clip.w, 200.f - ui::K_SCROLLBAR_THICKNESS));
	// Défilement maximal : jusqu'à voir le bas du contenu.
	EXPECT_TRUE(Near(r.MaxScroll().y, 200.f));
}

TEST(Scrollbar, TheVerticalBarCanMakeTheHorizontalOneNecessary) {
	Harness h;
	// 196 px tiennent dans 200, mais plus dans les 192 laissés par la barre verticale.
	ecs::Entity e = h.Scroller(196.f, 300.f);
	const ui::UiRect &r = h.Rect(e);
	EXPECT_EQ(r.gutter.x, ui::K_SCROLLBAR_THICKNESS);
	EXPECT_EQ(r.gutter.y, ui::K_SCROLLBAR_THICKNESS);
	EXPECT_TRUE(r.MaxScroll().x > 0.f);

	// Les deux pistes ne se chevauchent pas : la verticale s'arrête au-dessus
	// de l'horizontale.
	const sdl3::FRect screen = h.Screen(e);
	const sdl3::FRect vTrack = ui::VScrollbarTrackRect(r, screen);
	const sdl3::FRect hTrack = ui::HScrollbarTrackRect(r, screen);
	EXPECT_TRUE(Near(vTrack.y + vTrack.h, hTrack.y));
	EXPECT_TRUE(Near(hTrack.x + hTrack.w, vTrack.x));
}

TEST(Scrollbar, TheBarDisappearsWhenTheContentShrinks) {
	Harness h;
	ecs::Entity e = h.Scroller(0.f, 300.f);
	EXPECT_EQ(h.Rect(e).gutter.x, ui::K_SCROLLBAR_THICKNESS);

	ecs::Entity child = h.Child(e, 0);
	h.ar.GetComponent<ui::UiItem>(child).Unwrap()->height = ui::Dimension::Px(50.f);
	h.Layout();
	EXPECT_EQ(h.Rect(e).gutter.x, 0.f);
	EXPECT_TRUE(Near(h.Screen(child).w, 200.f));
}

TEST(Scrollbar, ASingleLineFieldNeverScrollsVertically) {
	Harness h;
	auto field = h.f.Input("Rechercher");
	field.Anchor(ui::Anchor::TopLeft).Offset(0.f, 0.f).Size(200.f, 16.f); // plus bas qu'une ligne + marges
	ecs::Entity e = field.Spawn();
	h.Layout();
	h.input.Tick(h.ar, h.layout, 0.016f); // calcule le contenu des champs
	EXPECT_EQ(h.Rect(e).MaxScroll().y, 0.f);
	EXPECT_EQ(h.Rect(e).gutter.x, 0.f);
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Focus clavier
// ─────────────────────────────────────────────────────────────────────────

TEST(KeyboardFocus, TheFocusedFieldTakesTheTypingAndConsumesIt) {
	Harness h;
	ecs::Entity field = h.FocusedInput();
	EXPECT_TRUE(h.input.KeyboardFocus(h.ar) == field);

	EXPECT_TRUE(h.Send(TextEv("a")));
	EXPECT_TRUE(h.InputOf(field).text == String("a"));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_A)));			// la touche elle-même, pas seulement le texte
	EXPECT_TRUE(h.Send(KeyEv(SDLK_SPACE)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_KP_5)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_LEFT)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_BACKSPACE)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_LSHIFT, SDL_KMOD_LSHIFT)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_C, SDL_KMOD_LCTRL)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_V, SDL_KMOD_LCTRL)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_X, SDL_KMOD_LCTRL)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_A, SDL_KMOD_LCTRL)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_A, SDL_KMOD_NONE, false))); // relâchement aussi
}

TEST(KeyboardFocus, FunctionKeysAndApplicationShortcutsGoThrough) {
	Harness h;
	(void)h.FocusedInput();
	for (SDL_Keycode key : {SDLK_F1, SDLK_F2, SDLK_F5, SDLK_F12})
		EXPECT_FALSE(h.Send(KeyEv(key)));
	EXPECT_FALSE(h.Send(KeyEv(SDLK_S, SDL_KMOD_LCTRL)));
	EXPECT_FALSE(h.Send(KeyEv(SDLK_Q, SDL_KMOD_LCTRL)));
	// AltGr compose un caractère : ce n'est pas un raccourci.
	EXPECT_TRUE(h.Send(KeyEv(SDLK_0, SDL_Keymod(SDL_KMOD_RALT | SDL_KMOD_LCTRL))));
}

TEST(KeyboardFocus, WithoutFocusNothingIsConsumed) {
	Harness h;
	auto field = h.f.Input();
	field.Anchor(ui::Anchor::TopLeft).Offset(10.f, 10.f).Size(200.f, 30.f);
	(void)field.Spawn();
	h.Layout();
	EXPECT_FALSE(h.input.KeyboardFocus(h.ar).Valid());
	EXPECT_FALSE(h.Send(KeyEv(SDLK_W)));
	EXPECT_FALSE(h.Send(TextEv("w")));
	EXPECT_FALSE(h.Send(KeyEv(SDLK_DELETE)));
}

TEST(KeyboardFocus, EscapeReleasesTheFieldBeforeClosingAnything) {
	Harness h;
	ecs::Entity field = h.FocusedInput();
	EXPECT_TRUE(h.Send(KeyEv(SDLK_ESCAPE)));
	EXPECT_FALSE(h.InputOf(field).focused);
	EXPECT_FALSE(h.input.KeyboardFocus(h.ar).Valid());
	// Plus de focus : la frappe suivante repart vers l'application.
	EXPECT_FALSE(h.Send(KeyEv(SDLK_W)));
}

TEST(KeyboardFocus, AReadOnlyFieldOnlyKeepsNavigationAndCopy) {
	Harness h;
	(void)h.FocusedInput(ui::IOMode::READ_AND_COPY_ONLY);
	EXPECT_FALSE(h.Send(KeyEv(SDLK_W)));
	EXPECT_FALSE(h.Send(TextEv("w")));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_DOWN)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_C, SDL_KMOD_LCTRL)));
}

TEST(KeyboardFocus, TheTransmissionRuleIsConfigurable) {
	Harness h;
	(void)h.FocusedInput();
	h.input.keyPassThrough = [](const sdl3::Event &ev, ecs::Entity) { return ev.IsKeyDown(SDLK_TAB); };
	EXPECT_FALSE(h.Send(KeyEv(SDLK_TAB)));
	EXPECT_TRUE(h.Send(KeyEv(SDLK_F5))); // la règle remplace celle par défaut
}

TEST(KeyboardFocus, AnEventAlreadyConsumedIsNotProcessed) {
	Harness h;
	ecs::Entity field = h.FocusedInput();
	sdl3::Event ev = TextEv("z");
	ev.Consume();
	EXPECT_TRUE(h.input.HandleEvent(h.ar, ev, h.layout));
	EXPECT_TRUE(h.InputOf(field).text.IsEmpty());
	ev.Release();
	EXPECT_FALSE(ev.IsConsumed());
}

TEST(KeyboardFocus, CtrlASelectsTheWholeText) {
	Harness h;
	ecs::Entity field = h.FocusedInput();
	(void)h.Send(TextEv("abc"));
	(void)h.Send(KeyEv(SDLK_A, SDL_KMOD_LCTRL));
	EXPECT_EQ(h.InputOf(field).selectionAnchor, size_t(0));
	EXPECT_EQ(h.InputOf(field).cursor, size_t(3));
	(void)h.Send(TextEv("x")); // remplace la sélection
	EXPECT_TRUE(h.InputOf(field).text == String("x"));
}

int main() { return RUN_ALL_TESTS(); }
