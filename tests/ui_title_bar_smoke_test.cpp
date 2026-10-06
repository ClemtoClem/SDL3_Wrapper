// Smoke test : ui::TitleBar / ui::StatusBar / ui::SpawnIconButton (chrome.hpp)
// — boutons à icône (glyphe transparent au pointeur) ou texte de repli,
// options (icône d'application, poignée, boutons facultatifs), placement
// après layout, rappels, forme historique, barre d'état et sa poignée.
#define USE_TEST

#include "core/core.hpp"
#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
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

int main() {
	return RUN_ALL_TESTS();
}
