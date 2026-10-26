// Cascade : privé / partagé, portée des feuilles importées, héritage, états.
#include "harness.hpp"
#include "test.hpp"

namespace {

bool SameColor(ui::Color a, ui::Color b) {
	return std::fabs(a.r - b.r) < 0.01f && std::fabs(a.g - b.g) < 0.01f && std::fabs(a.b - b.b) < 0.01f &&
		   std::fabs(a.a - b.a) < 0.01f;
}

const ui::Color RED = ui::Rgba(255, 0, 0), GREEN = ui::Rgba(0, 255, 0), BLUE = ui::Rgba(0, 0, 255),
				YELLOW = ui::Rgba(255, 255, 0);

} // namespace

TEST(Cascade, TypeAndClassStylesAreInheritedByChildren) {
	Harness h;
	h.gui.AddStyleSheet("column.sidebar { color: #ff0000; font-size: 20px; } panel { color: #00ff00; }");
	ui::Widget label, inner;
	h.Mount(ui::Column().Class("sidebar").Children({
		ui::Text("a").Ref(label),
		ui::Panel().Children({ui::Text("b").Ref(inner)}),
	}));
	CHECK(SameColor(label.Computed()->color, RED));
	CHECK_NEAR(label.Computed()->fontSize, 20.f, 1e-3);
	// Le style du widget (panel) prime sur celui hérité de ses parents.
	CHECK(SameColor(inner.Computed()->color, GREEN));
	CHECK_NEAR(inner.Computed()->fontSize, 20.f, 1e-3);
}

TEST(Cascade, IdAndThisStylesArePrivate) {
	Harness h;
	h.gui.AddStyleSheet("#box { color: #ff0000; } .box { font-size: 18px; }");
	ui::Widget box, child;
	h.Mount(ui::Column().Id("box").Class("box").Ref(box).Children({ui::Text("x").Ref(child)}));
	CHECK(SameColor(box.Computed()->color, RED));
	// Privé : l'enfant n'hérite PAS de la couleur de #box…
	CHECK(!SameColor(child.Computed()->color, RED));
	// … mais hérite bien de la police de .box (partagée).
	CHECK_NEAR(child.Computed()->fontSize, 18.f, 1e-3);
}

TEST(Cascade, PrivateStylesWinOverSharedOnes) {
	Harness h;
	h.gui.AddStyleSheet("#b { color: #0000ff; } button.danger.big { color: #ff0000; }");
	ui::Widget b = h.Mount(ui::Button("ok").Id("b").Class("danger").Class("big"));
	CHECK(SameColor(b.Computed()->color, BLUE));
}

TEST(Cascade, ThisTargetsTheImportingWidget) {
	Harness h;
	ui::Widget outer, inner, sibling;
	h.Mount(ui::Column().Children({
		ui::Column().Ref(outer).Css("this { background-color: #00ff00; } text { color: #ff0000; }").Children({
			ui::Text("dedans").Ref(inner),
		}),
		ui::Text("dehors").Ref(sibling),
	}));
	CHECK(SameColor(outer.Computed()->backgroundColor, GREEN));
	CHECK(SameColor(inner.Computed()->color, RED));
	// La feuille importée par un widget ne vaut que pour sa descendance.
	CHECK(!SameColor(sibling.Computed()->color, RED));
}

TEST(Cascade, AWidgetSheetWinsOverItsParentsSheet) {
	Harness h;
	ui::Widget leaf;
	h.Mount(ui::Column().Class("outer").Css("column.outer text.leaf { color: #ff0000; }").Children({
		ui::Column().Css("text { color: #0000ff; }").Children({ui::Text("x").Class("leaf").Ref(leaf)}),
	}));
	// Même moins spécifique, la feuille du widget le plus proche l'emporte.
	CHECK(SameColor(leaf.Computed()->color, BLUE));
	// Un style privé (#id) reste prioritaire, d'où qu'il vienne.
	ui::Widget leaf2;
	h.Mount(ui::Column().Css("#leaf2 { color: #ff0000; }").Children({
		ui::Column().Css("text { color: #0000ff; }").Children({ui::Text("x").Id("leaf2").Ref(leaf2)}),
	}));
	CHECK(SameColor(leaf2.Computed()->color, RED));
}

TEST(Cascade, BuilderStylesBeatStyleSheets) {
	Harness h;
	h.gui.AddStyleSheet("#t { color: #ff0000; }");
	ui::Widget t = h.Mount(ui::Text("x").Id("t").TextColor(YELLOW));
	CHECK(SameColor(t.Computed()->color, YELLOW));
	// Cascade() : partagé avec les enfants.
	ui::Widget child;
	h.Mount(ui::Column().Cascade(ui::Style().TextColor(GREEN)).Children({ui::Text("y").Ref(child)}));
	CHECK(SameColor(child.Computed()->color, GREEN));
	// Style privé du builder : non hérité.
	ui::Widget child2;
	h.Mount(ui::Column().TextColor(BLUE).Children({ui::Text("z").Ref(child2)}));
	CHECK(!SameColor(child2.Computed()->color, BLUE));
}

TEST(Cascade, UndefinedValuesFallBackToRootThenToCppDefaults) {
	Harness h;
	h.gui.AddStyleSheet("root { font-size: 17px; }");
	ui::Widget t = h.Mount(ui::Column().Children({ui::Column().Children({ui::Text("x")})}));
	ui::Widget text = t.Find("text").Unwrap();
	CHECK_NEAR(text.Computed()->fontSize, 17.f, 1e-3);
	// Propriété jamais définie : valeur par défaut du code.
	CHECK_EQ(text.Computed()->tabSize, ui::ComputedStyle::Initial().tabSize);
}

TEST(Cascade, StatePseudoClassesFollowTheInteraction) {
	Harness h;
	h.gui.AddStyleSheet("button.t { background-color: #0000ff; } button.t:hover { background-color: #ff0000; }"
						"button.t:active { background-color: #00ff00; }");
	ui::Widget b = h.Mount(ui::Button("survol").Class("t").Size(100.f, 30.f));
	CHECK(SameColor(b.Computed()->backgroundColor, BLUE));
	const sdl3::FPoint c = Harness::Center(b);
	h.Move(c.x, c.y);
	CHECK(SameColor(b.Computed()->backgroundColor, RED));
	h.Down(c.x, c.y);
	CHECK(SameColor(b.Computed()->backgroundColor, GREEN));
	h.Up(c.x, c.y);
	h.Move(700.f, 500.f);
	CHECK(SameColor(b.Computed()->backgroundColor, BLUE));
}

TEST(Cascade, BuilderStateVariants) {
	Harness h;
	ui::Widget b = h.Mount(ui::Button("x").Size(80.f, 30.f).Background(BLUE).HoverStyle(ui::Style().Background(RED)));
	CHECK(SameColor(b.Computed()->backgroundColor, BLUE));
	h.Move(Harness::Center(b).x, Harness::Center(b).y);
	CHECK(SameColor(b.Computed()->backgroundColor, RED));
}

TEST(Cascade, CheckedAndSelectedAreDerivedFromTheBricks) {
	Harness h;
	h.gui.AddStyleSheet("checkbox:checked { background-color: #ff0000; } list-item:selected { color: #00ff00; }");
	ui::Widget cb = h.Mount(ui::Checkbox("c", false));
	CHECK(!SameColor(cb.Computed()->backgroundColor, RED));
	cb.SetChecked(true);
	h.Frame();
	CHECK(SameColor(cb.Computed()->backgroundColor, RED));
	ui::Widget list = h.Mount(ui::ListBox({"a", "b"}, 1));
	auto items = list.FindAll("list-item");
	REQUIRE_EQ(items.size(), size_t(2));
	CHECK(SameColor(items[1].Computed()->color, GREEN));
	CHECK(!SameColor(items[0].Computed()->color, GREEN));
}

TEST(Cascade, DescendantAndChildCombinators) {
	Harness h;
	h.gui.AddStyleSheet(".menu > text { color: #ff0000; } .menu text.deep { color: #0000ff; }");
	ui::Widget direct, deep;
	h.Mount(ui::Column().Class("menu").Children({
		ui::Text("a").Ref(direct),
		ui::Row().Children({ui::Text("b").Class("deep").Ref(deep)}),
	}));
	CHECK(SameColor(direct.Computed()->color, RED));
	CHECK(SameColor(deep.Computed()->color, BLUE));
}

TEST(Cascade, ZoomScalesFontsAndLengths) {
	Harness h;
	ui::Widget t;
	h.Mount(ui::Column().Zoom(2.f).Children({ui::Text("x").FontSize(10.f).Width(50.f).Ref(t)}));
	CHECK_NEAR(t.Computed()->fontSize, 20.f, 1e-3);
	CHECK_NEAR(t.Bounds().w, 100.f, 0.5);
}

TEST(Cascade, ThemesCanBeSwitchedLive) {
	Harness h;
	ui::Widget p = h.Mount(ui::Panel());
	const ui::Color dark = p.Computed()->backgroundColor;
	h.gui.SetTheme(ui::Theme::Light);
	h.Frame();
	CHECK(!SameColor(p.Computed()->backgroundColor, dark));
	// Une application peut ne redéfinir que la palette.
	h.gui.AddStyleSheet("root { --panel: #123456; }");
	h.Frame();
	CHECK(SameColor(p.Computed()->backgroundColor, ui::Rgba(0x12, 0x34, 0x56)));
}
