// Tests unitaires — modèle de boîte du layout : la taille d'un widget à
// taille automatique = contenu (texte ou enfants, espacements, marges des
// enfants) + padding + bordure, et ses enfants sont posés à l'intérieur.
//
// Mesure prévisible : 10 px par caractère, 20 px par ligne.
#include "test.hpp"
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

#include <cstdio>
#include <cstdlib>

using namespace ui;

namespace {

[[nodiscard]] bool Near(float a, float b, float eps = 1e-3f) {
	return (a - b) < eps && (b - a) < eps;
}

struct Harness {
	ecs::ArchetypeRegistry ar;
	LayoutSystem layout;
	StyleSystem style;
	UiFactory f{ar, layout};

	Harness() {
		setenv("SDL_VIDEODRIVER", "dummy", 0);
		layout.measureText = [](const String &line, float) -> sdl3::FPoint { return {float(CharCount(line)) * 10.f, 20.f}; };
	}
	void Layout() {
		style.Resolve(ar); // comme Ui::Render : styles résolus avant le layout
		layout.Run(ar, 800.f, 600.f);
	}
	[[nodiscard]] sdl3::FRect Screen(ecs::Entity e) { return ar.GetComponent<UiComputed>(e).Unwrap()->screen; }
	[[nodiscard]] WidgetBuilder Text(const char *text) {
		WidgetBuilder b = f.Label(String(text));
		b.WAuto().HAuto();
		return b;
	}
};

[[nodiscard]] bool SameRect(const sdl3::FRect &r, float x, float y, float w, float h) {
	const bool same = Near(r.x, x) && Near(r.y, y) && Near(r.w, w) && Near(r.h, h);
	if (!same)
		std::printf("    obtenu {%g, %g, %g, %g}, attendu {%g, %g, %g, %g}\n", double(r.x), double(r.y), double(r.w),
					double(r.h), double(x), double(y), double(w), double(h));
	return same;
}

constexpr sdl3::FColor LINE{1.f, 1.f, 1.f, 1.f};
constexpr sdl3::FColor BG{0.1f, 0.1f, 0.1f, 1.f};

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// 1. Un texte seul
// ─────────────────────────────────────────────────────────────────────────

TEST(BoxModel, ALabelIsExactlyItsText) {
	Harness h;
	auto label = h.Text("abcd");
	label.Anchor(Anchor::TopLeft).Offset(10.f, 20.f);
	ecs::Entity e = label.Spawn();
	h.Layout();
	EXPECT_TRUE(SameRect(h.Screen(e), 10.f, 20.f, 40.f, 20.f));
}

TEST(BoxModel, ALabelPaddingAddsAroundTheText) {
	Harness h;
	auto label = h.Text("abcd");
	label.Anchor(Anchor::TopLeft).Pad(math::Sides(5.f, 3.f));
	ecs::Entity e = label.Spawn();
	h.Layout();
	EXPECT_TRUE(SameRect(h.Screen(e), 0.f, 0.f, 40.f + 10.f, 20.f + 6.f));
}

TEST(BoxModel, ALabelBorderAddsAroundThePadding) {
	Harness h;
	auto label = h.Text("abcd");
	// Une bordure n'occupe de place que si elle est dessinée : sur un fond.
	label.Anchor(Anchor::TopLeft).Pad(math::Sides(5.f, 3.f)).Bg(BG).BorderColor(LINE, 2.f);
	ecs::Entity e = label.Spawn();
	h.Layout();
	EXPECT_TRUE(SameRect(h.Screen(e), 0.f, 0.f, 40.f + 10.f + 4.f, 20.f + 6.f + 4.f));
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Des conteneurs
// ─────────────────────────────────────────────────────────────────────────

TEST(BoxModel, ARowAddsChildrenGapsAndPadding) {
	Harness h;
	auto row = h.f.Row();
	row.Anchor(Anchor::TopLeft).Offset(100.f, 50.f).WAuto().HAuto().Pad(math::Sides(10.f, 4.f)).Gap(6.f);
	ecs::Entity r = row.Spawn();
	auto a = h.Text("ab");
	a.Parent(r);
	ecs::Entity ea = a.Spawn();
	auto b = h.Text("abc");
	b.Parent(r);
	ecs::Entity eb = b.Spawn();
	h.Layout();
	EXPECT_TRUE(SameRect(h.Screen(r), 100.f, 50.f, 20.f + 6.f + 30.f + 20.f, 20.f + 8.f));
	EXPECT_TRUE(SameRect(h.Screen(ea), 110.f, 54.f, 20.f, 20.f));
	EXPECT_TRUE(SameRect(h.Screen(eb), 110.f + 20.f + 6.f, 54.f, 30.f, 20.f));
}

TEST(BoxModel, AColumnAddsBorderPaddingAndChildMargins) {
	Harness h;
	auto column = h.f.Column();
	column.Anchor(Anchor::TopLeft).WAuto().HAuto().Pad(8.f).Bg(BG).BorderColor(LINE, 2.f).Gap(4.f);
	ecs::Entity c = column.Spawn();
	auto a = h.Text("abcde");
	a.Margin(3.f).Parent(c);
	ecs::Entity ea = a.Spawn();
	auto b = h.Text("ab");
	b.Parent(c);
	ecs::Entity eb = b.Spawn();
	h.Layout();
	// Largeur : le plus large des enfants (50 + 2 × 3 de marge) ; hauteur :
	// (20 + 6) + 4 + 20 ; plus 2 × 8 de padding et 2 × 2 de bordure.
	EXPECT_TRUE(SameRect(h.Screen(c), 0.f, 0.f, 56.f + 16.f + 4.f, 50.f + 16.f + 4.f));
	EXPECT_TRUE(SameRect(h.Screen(ea), 2.f + 8.f + 3.f, 2.f + 8.f + 3.f, 50.f, 20.f));
	// Alignement transversal par défaut : étiré sur la largeur de contenu.
	EXPECT_TRUE(SameRect(h.Screen(eb), 2.f + 8.f, 2.f + 8.f + 26.f + 4.f, 56.f, 20.f));
}

TEST(BoxModel, NestedContainersAccumulatePaddings) {
	Harness h;
	auto outer = h.f.Column();
	outer.Anchor(Anchor::TopLeft).WAuto().HAuto().Pad(8.f);
	ecs::Entity o = outer.Spawn();
	auto inner = h.f.Row();
	inner.WAuto().HAuto().Pad(4.f).Parent(o);
	ecs::Entity i = inner.Spawn();
	auto t = h.Text("abc");
	t.Parent(i);
	ecs::Entity et = t.Spawn();
	h.Layout();
	EXPECT_TRUE(SameRect(h.Screen(o), 0.f, 0.f, 30.f + 8.f + 16.f, 20.f + 8.f + 16.f));
	EXPECT_TRUE(SameRect(h.Screen(i), 8.f, 8.f, 30.f + 8.f, 20.f + 8.f));
	EXPECT_TRUE(SameRect(h.Screen(et), 12.f, 12.f, 30.f, 20.f));
}

TEST(BoxModel, AButtonFitsItsLabel) {
	Harness h;
	auto button = h.f.Button(String("abcd"));
	button.Anchor(Anchor::TopLeft).WAuto().HAuto();
	ecs::Entity e = button.Spawn();
	h.Layout();
	// Le texte entier tient dans la boîte, marges intérieures comprises.
	EXPECT_TRUE(h.Screen(e).w >= 40.f);
	EXPECT_TRUE(h.Screen(e).h >= 20.f);
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Pourcentages : relatifs à la zone de contenu du parent
// ─────────────────────────────────────────────────────────────────────────

TEST(BoxModel, APercentChildFillsTheParentContentBox) {
	Harness h;
	auto parent = h.f.Column();
	parent.Anchor(Anchor::TopLeft).Offset(10.f, 10.f).W(Dimension::Px(200.f)).H(Dimension::Px(100.f)).Pad(16.f);
	ecs::Entity p = parent.Spawn();
	auto child = h.f.Column();
	child.Absolute().Anchor(Anchor::TopLeft).W(Dimension::Pct(100.f)).H(Dimension::Pct(100.f)).Parent(p);
	ecs::Entity c = child.Spawn();
	h.Layout();
	EXPECT_TRUE(SameRect(h.Screen(c), 26.f, 26.f, 200.f - 32.f, 100.f - 32.f));
}

TEST(BoxModel, AnAbsoluteChildStartsInsideTheParentPadding) {
	// Un enfant absolu est placé dans la zone de contenu : le padding par
	// défaut d'un conteneur (8 px) le décale d'autant.
	Harness h;
	auto parent = h.f.Column();
	parent.Fixed().Anchor(Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f));
	ecs::Entity p = parent.Spawn();
	auto child = h.f.Column();
	child.Absolute().Anchor(Anchor::TopLeft).W(Dimension::Pct(100.f)).H(Dimension::Pct(100.f)).Parent(p);
	ecs::Entity c = child.Spawn();
	h.Layout();
	EXPECT_TRUE(SameRect(h.Screen(c), 8.f, 8.f, 800.f - 16.f, 600.f - 16.f));
}

TEST(BoxModel, FullScreenLayersStayInsideTheWindow) {
	// Structure des couches d'une application : racine détachée plein écran,
	// couche absolue plein écran, puis un canevas plein écran dedans — sans
	// padding, des couches plein écran couvrent exactement la fenêtre.
	Harness h;
	auto layer = h.f.Column();
	layer.Fixed().Anchor(Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f)).Pad(0.f);
	ecs::Entity l = layer.Spawn();
	auto hud = h.f.Column();
	hud.Absolute().Anchor(Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f)).Pad(0.f).Parent(l);
	ecs::Entity u = hud.Spawn();
	auto canvas = h.f.Canvas([](sdl3::Renderer &, sdl3::FRect) {});
	canvas.Absolute().Anchor(Anchor::TopLeft).W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f)).Parent(u);
	ecs::Entity c = canvas.Spawn();
	h.Layout();
	EXPECT_TRUE(SameRect(h.Screen(l), 0.f, 0.f, 800.f, 600.f));
	EXPECT_TRUE(SameRect(h.Screen(u), 0.f, 0.f, 800.f, 600.f));
	EXPECT_TRUE(SameRect(h.Screen(c), 0.f, 0.f, 800.f, 600.f));
}

// ─────────────────────────────────────────────────────────────────────────
// 4. Police réellement dessinée : un texte gras est mesuré en gras
// ─────────────────────────────────────────────────────────────────────────

TEST(BoxModel, ABoldLabelIsMeasuredWithTheBoldFont) {
	Harness h;
	// Gras : 12 px par caractère, normal : 10 px (cf. Harness).
	h.layout.measureStyledText = [](const String &line, float, bool bold, bool) -> sdl3::FPoint {
		return {float(CharCount(line)) * (bold ? 12.f : 10.f), 20.f};
	};
	auto regular = h.Text("abcd");
	regular.Anchor(Anchor::TopLeft);
	ecs::Entity r = regular.Spawn();
	auto bold = h.Text("abcd");
	bold.Anchor(Anchor::TopLeft).Offset(0.f, 40.f).Bold();
	ecs::Entity b = bold.Spawn();
	h.Layout();
	EXPECT_TRUE(SameRect(h.Screen(r), 0.f, 0.f, 40.f, 20.f));
	EXPECT_TRUE(SameRect(h.Screen(b), 0.f, 40.f, 48.f, 20.f));
}

TEST(BoxModel, AWrappedLabelWithPaddingGrowsByItsPadding) {
	Harness h;
	auto column = h.f.Column();
	column.Anchor(Anchor::TopLeft).W(Dimension::Px(136.f)).HAuto().Pad(0.f);
	ecs::Entity c = column.Spawn();
	// 120 px de contenu (136 − 2 × 8 de padding) moins 2 × 8 de marge de
	// texte : 104 px, soit 10 caractères par ligne → « aaaa bbbb cccc » sur
	// 2 lignes de 20 px, plus 2 × 8 de padding vertical.
	auto text = h.f.Label(String("aaaa bbbb cccc"));
	text.GrowW().HAuto().TextWrap().Pad(8.f).Parent(c);
	ecs::Entity t = text.Spawn();
	for (int i = 0; i < 3; ++i) // hauteur des textes à la ligne : connue après placement
		h.Layout();
	EXPECT_TRUE(SameRect(h.Screen(t), 0.f, 0.f, 136.f, 2.f * 20.f + 16.f));
}
