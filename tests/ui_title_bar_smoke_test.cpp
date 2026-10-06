// Smoke test : ui::TitleBar / ui::StatusBar / ui::SpawnIconButton (chrome.hpp)
// — boutons à icône (glyphe transparent au pointeur) ou texte de repli,
// options (icône d'application, poignée, boutons facultatifs), placement
// après layout, rappels, forme historique, barre d'état et sa poignée.
#define USE_TEST

#include "core/core.hpp"
#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/canvas_window_frame.hpp"
#include "ui/chrome.hpp"
#include "ui/ui.hpp"

#include <cstdlib>
#include <iostream>

namespace {

/// Fenêtre cachée (pilote vidéo factice) + layout + fabrique, pour chaque test.
struct Fixture {
	sdl3::SdlContext sdl;
	sdl3::Window window;
	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::UiFactory f{ar, layout};

	Fixture(sdl3::SdlContext s, sdl3::Window w) : sdl(std::move(s)), window(std::move(w)) {}

	static Option<Fixture*> Make() {
		setenv("SDL_VIDEODRIVER", "dummy", 0);
		auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO);
		if (!sdl)
			return NONE;
		auto win = sdl3::Window::Create("title-bar", 800, 600, sdl3::window_flags::HIDDEN);
		if (!win)
			return NONE;
		return Some(new Fixture(std::move(sdl.Value()), std::move(win.Value())));
	}
};

[[nodiscard]] String ButtonText(ecs::ArchetypeRegistry& ar, ecs::Entity e) {
	auto b = ar.GetComponent<ui::UiButton>(e);
	return b.IsSome() ? b.Unwrap()->text : String("<pas un bouton>");
}

[[nodiscard]] size_t ChildCount(ecs::ArchetypeRegistry& ar, ecs::Entity e) {
	auto c = ar.GetComponent<ui::UiChildren>(e);
	return c.IsSome() ? c.Unwrap()->list.size() : 0;
}

[[nodiscard]] sdl3::FRect Screen(ecs::ArchetypeRegistry& ar, ecs::Entity e) {
	auto c = ar.GetComponent<ui::UiComputed>(e);
	return c.IsSome() ? c.Unwrap()->screen : sdl3::FRect{};
}

} // namespace

TEST(TitleBar, IconButtonsWithGlyphs) {
	auto fx = Fixture::Make();
	ASSERT_TRUE(fx.IsSome());
	std::unique_ptr<Fixture> keep(*fx);
	auto& ar = keep->ar;

	bool closed = false;
	ui::TitleBarOptions o;
	o.title = "Démo";
	o.appIcon = Some(ui::MaterialIcons::WIFI_TETHERING);
	o.onClose = [&closed] { closed = true; };
	ui::TitleBarWidgets tb = ui::TitleBar(keep->f, keep->window, std::move(o));

	EXPECT_TRUE(tb.root.Valid());
	EXPECT_TRUE(tb.appIcon.Valid());
	EXPECT_TRUE(tb.moveHandle.Valid());
	EXPECT_TRUE(ar.HasComponent<ui::UiIcon>(tb.appIcon));
	ASSERT_EQ(tb.Buttons().size(), size_t(3));
	// Bouton à icône : texte vide, un seul enfant, le glyphe, transparent au pointeur.
	for (ecs::Entity b : tb.Buttons()) {
		EXPECT_TRUE(ButtonText(ar, b).IsEmpty());
		EXPECT_EQ(ChildCount(ar, b), size_t(1));
	}
	ASSERT_TRUE(tb.maximizeIcon.Valid());
	EXPECT_TRUE(ar.HasComponent<ui::UiPointerThrough>(tb.maximizeIcon));
	EXPECT_TRUE(ar.GetComponent<ui::UiIcon>(tb.maximizeIcon).Unwrap()->glyph ==
				ui::Glyphs::ToUtf8(ui::MaterialIcons::CROP_SQUARE));
	// Fenêtre non agrandie : Update laisse « agrandir ».
	tb.Update(ar, keep->window);
	EXPECT_TRUE(ar.GetComponent<ui::UiIcon>(tb.maximizeIcon).Unwrap()->glyph ==
				ui::Glyphs::ToUtf8(ui::MaterialIcons::CROP_SQUARE));

	tb.SetTitle(ar, "Nouveau titre");
	EXPECT_TRUE(ar.GetComponent<ui::UiLabel>(tb.title).Unwrap()->text == "Nouveau titre");

	// Fermer appelle onClose.
	auto cb = ar.GetComponent<ui::UiCallbacks>(tb.closeBtn);
	ASSERT_TRUE(cb.IsSome() && cb.Unwrap()->onClick);
	cb.Unwrap()->onClick();
	EXPECT_TRUE(closed);
	std::cout << "TitleBar (icônes, glyphe traversant, onClose, SetTitle): ok\n";
}

TEST(TitleBar, LayoutPutsButtonsOnTheRight) {
	auto fx = Fixture::Make();
	ASSERT_TRUE(fx.IsSome());
	std::unique_ptr<Fixture> keep(*fx);
	auto& ar = keep->ar;
	ui::TitleBarOptions o;
	o.title = "Titre";
	o.height = 36.f;
	ui::TitleBarWidgets tb = ui::TitleBar(keep->f, keep->window, std::move(o));
	keep->layout.RunIfNeeded(ar, 800, 600);

	sdl3::FRect bar = Screen(ar, tb.root);
	EXPECT_TRUE(bar.w > 799.f && bar.h > 35.f && bar.h < 37.f);
	sdl3::FRect minR = Screen(ar, tb.minimizeBtn);
	sdl3::FRect maxR = Screen(ar, tb.maximizeBtn);
	sdl3::FRect closeR = Screen(ar, tb.closeBtn);
	EXPECT_TRUE(minR.x < maxR.x && maxR.x < closeR.x);
	EXPECT_TRUE(closeR.x + closeR.w > 780.f); // collé au bord droit (padding près)
	EXPECT_TRUE(Screen(ar, tb.title).x < 100.f);
	// Glyphe centré dans son bouton.
	sdl3::FRect ic = Screen(ar, tb.maximizeIcon);
	EXPECT_TRUE(std::abs((ic.x + ic.w / 2) - (maxR.x + maxR.w / 2)) < 1.5f);
	EXPECT_TRUE(std::abs((ic.y + ic.h / 2) - (maxR.y + maxR.h / 2)) < 1.5f);
	std::cout << "TitleBar (layout : boutons à droite, glyphe centré): ok\n";
}

TEST(TitleBar, TextFallbackAndOptionalButtons) {
	auto fx = Fixture::Make();
	ASSERT_TRUE(fx.IsSome());
	std::unique_ptr<Fixture> keep(*fx);
	auto& ar = keep->ar;
	ui::TitleBarOptions o;
	o.title = "Sans police d'icônes";
	o.icons = false;
	o.appIcon = Some(ui::MaterialIcons::WIFI); // ignorée sans police
	o.minimizeButton = false;
	ui::TitleBarWidgets tb = ui::TitleBar(keep->f, keep->window, std::move(o));
	EXPECT_FALSE(tb.appIcon.Valid());
	EXPECT_FALSE(tb.minimizeBtn.Valid());
	EXPECT_FALSE(tb.maximizeIcon.Valid());
	ASSERT_EQ(tb.Buttons().size(), size_t(2));
	EXPECT_TRUE(ButtonText(ar, tb.maximizeBtn) == "\xe2\x96\xa1");
	EXPECT_TRUE(ButtonText(ar, tb.closeBtn) == "\xc3\x97");
	EXPECT_EQ(ChildCount(ar, tb.closeBtn), size_t(0));
	tb.Update(ar, keep->window); // sans glyphe : sans effet, sans plantage

	// Forme historique : texte, pas de poignée ni de fond.
	ui::TitleBarWidgets legacy = ui::TitleBar(keep->f, keep->window, String("Aero"), nullptr);
	EXPECT_EQ(legacy.Buttons().size(), size_t(3));
	EXPECT_FALSE(legacy.moveHandle.Valid());
	EXPECT_TRUE(ButtonText(ar, legacy.minimizeBtn) == "\xe2\x80\x94");
	std::cout << "TitleBar (repli texte, boutons facultatifs, forme historique): ok\n";
}

TEST(StatusBar, TextAndGrip) {
	auto fx = Fixture::Make();
	ASSERT_TRUE(fx.IsSome());
	std::unique_ptr<Fixture> keep(*fx);
	auto& ar = keep->ar;
	ui::StatusBarWidgets sb = ui::StatusBar(keep->f, {.text = "Prêt."});
	ASSERT_TRUE(sb.grip.Valid());
	EXPECT_TRUE(ar.HasComponent<ui::UiIcon>(sb.grip));
	sb.SetText(ar, "Connecté", sdl3::FColor{0.f, 1.f, 0.f, 1.f});
	EXPECT_TRUE(ar.GetComponent<ui::UiLabel>(sb.label).Unwrap()->text == "Connecté");
	keep->layout.RunIfNeeded(ar, 800, 600);
	sdl3::FRect grip = Screen(ar, sb.grip);
	EXPECT_TRUE(grip.x + grip.w > 790.f); // coin droit

	ui::StatusBarWidgets plain = ui::StatusBar(keep->f, {.text = "x", .resizeGrip = false});
	EXPECT_FALSE(plain.grip.Valid());
	ui::StatusBarWidgets textGrip = ui::StatusBar(keep->f, {.text = "x", .icons = false});
	EXPECT_TRUE(ar.GetComponent<ui::UiLabel>(textGrip.grip).Unwrap()->text == "\xe2\x87\xb2");
	std::cout << "StatusBar (texte, couleur, poignée icône / texte / absente): ok\n";
}

TEST(WindowFrame, ContentBetweenBarsAndClose) {
	auto fx = Fixture::Make();
	ASSERT_TRUE(fx.IsSome());
	std::unique_ptr<Fixture> keep(*fx);
	auto& ar = keep->ar;
	auto ttf = sdl3::TtfContext::Create(); // pour ouvrir la police d'icônes
	ASSERT_TRUE(ttf.IsOk());
	ui::RenderSystem render;
	bool closedCallback = false;
	ui::WindowFrame frame;
	frame.Build(keep->f, keep->layout, render, keep->window,
				{.title = "Cadre",
				 .appIcon = Some(ui::MaterialIcons::WIDGETS),
				 .status = "Prêt.",
				 .onClose = [&closedCallback] { closedCallback = true; }});
	// Police d'icônes trouvée dans assets/fonts (tests lancés depuis la racine).
	EXPECT_TRUE(frame.HasIcons());
	EXPECT_TRUE(render.HasFont(ui::Glyphs::FontFamily<ui::MaterialIcons>()));
	EXPECT_TRUE(String(SDL_GetWindowTitle(keep->window.Get())) == "Cadre");

	auto content = keep->f.Panel();
	content.GrowW().GrowH().Parent(frame.Content());
	ecs::Entity contentE = content.Spawn();
	keep->layout.RunIfNeeded(ar, 800, 600);
	sdl3::FRect title = Screen(ar, frame.TitleBar().root);
	sdl3::FRect status = Screen(ar, frame.StatusBar().root);
	sdl3::FRect area = frame.ContentRect();
	EXPECT_TRUE(title.y < 1.f && title.h > 35.f && title.h < 37.f);
	EXPECT_TRUE(status.y + status.h > 599.f && status.h > 23.f && status.h < 25.f);
	EXPECT_TRUE(std::abs(area.y - (title.y + title.h)) < 1.f);
	EXPECT_TRUE(std::abs((area.y + area.h) - status.y) < 1.f);
	EXPECT_TRUE(std::abs(Screen(ar, contentE).h - area.h) < 1.f);

	frame.SetStatus("Connecté");
	EXPECT_TRUE(ar.GetComponent<ui::UiLabel>(frame.StatusBar().label).Unwrap()->text == "Connecté");
	frame.SetTitle("Autre");
	EXPECT_TRUE(ar.GetComponent<ui::UiLabel>(frame.TitleBar().title).Unwrap()->text == "Autre");

	EXPECT_FALSE(frame.CloseRequested());
	auto cb = ar.GetComponent<ui::UiCallbacks>(frame.TitleBar().closeBtn);
	ASSERT_TRUE(cb.IsSome() && cb.Unwrap()->onClick);
	cb.Unwrap()->onClick();
	EXPECT_TRUE(frame.CloseRequested());
	EXPECT_TRUE(closedCallback);
	frame.ClearCloseRequest();
	frame.RequestClose();
	EXPECT_TRUE(frame.CloseRequested());
	frame.Update(); // hit-test reconstruit, icône agrandir : sans plantage
	std::cout << "WindowFrame (contenu entre les barres, état, titre, fermeture): ok\n";
}

TEST(CanvasWindowFrame, SoftwareUiAndChromeHitTest) {
	auto fx = Fixture::Make();
	ASSERT_TRUE(fx.IsSome());
	std::unique_ptr<Fixture> keep(*fx);
	auto frame = ui::CanvasWindowFrame::Create(keep->window, {.title = "Scène 3D", .status = "x"});
	ASSERT_TRUE(frame.IsOk());
	auto& f = *frame.Value();
	f.Update(0.016f);							  // layout + rendu logiciel des barres
	EXPECT_TRUE(f.IsOverChrome({400.f, 10.f}));	  // barre de titre
	EXPECT_TRUE(f.IsOverChrome({400.f, 590.f}));  // barre d'état
	EXPECT_FALSE(f.IsOverChrome({400.f, 300.f})); // scène
	EXPECT_FALSE(f.CloseRequested());
	f.Frame().RequestClose();
	EXPECT_TRUE(f.CloseRequested());
	std::cout << "CanvasWindowFrame (interface logicielle, zones des barres): ok\n";
}

int main() {
	return RUN_ALL_TESTS();
}
