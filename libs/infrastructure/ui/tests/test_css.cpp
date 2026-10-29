// Feuilles de style : valeurs, sélecteurs, règles-@, ressources.
#include "harness.hpp"
#include "test.hpp"

#include <filesystem>
#include <fstream>

namespace {

bool SameColor(ui::Color a, ui::Color b) {
	return std::fabs(a.r - b.r) < 0.01f && std::fabs(a.g - b.g) < 0.01f && std::fabs(a.b - b.b) < 0.01f &&
		   std::fabs(a.a - b.a) < 0.01f;
}

std::filesystem::path ScratchDir(const char *name) {
	auto dir = std::filesystem::temp_directory_path() / "ui-tests" / name;
	std::filesystem::create_directories(dir);
	return dir;
}

void WriteFile(const std::filesystem::path &p, const char *text) {
	std::ofstream out(p);
	out << text;
}

} // namespace

TEST(CssValues, ColorsInEveryNotation) {
	CHECK(SameColor(ui::ParseColor("#ff0000").Unwrap(), ui::Rgba(255, 0, 0)));
	CHECK(SameColor(ui::ParseColor("#0f08").Unwrap(), ui::Rgba(0, 255, 0, 136)));
	CHECK(SameColor(ui::ParseColor("rgb(10, 20, 30)").Unwrap(), ui::Rgba(10, 20, 30)));
	CHECK(SameColor(ui::ParseColor("rgba(10 20 30 / 50%)").Unwrap(), ui::Rgba(10, 20, 30, 128)));
	CHECK(SameColor(ui::ParseColor("hsl(0, 100%, 50%)").Unwrap(), ui::Rgba(255, 0, 0)));
	CHECK(SameColor(ui::ParseColor("steelblue").Unwrap(), ui::Rgba(70, 130, 180)));
	CHECK(SameColor(ui::ParseColor("transparent").Unwrap(), ui::Rgba(0, 0, 0, 0)));
	CHECK(ui::ParseColor("pas-une-couleur").IsNone());
}

TEST(CssValues, LengthsAndCalc) {
	CHECK(ui::ParseLength("12px").Unwrap() == ui::Length::Px(12.f));
	CHECK(ui::ParseLength("50%").Unwrap() == ui::Length::Pct(50.f));
	CHECK(ui::ParseLength("2em").Unwrap() == ui::Length::Em(2.f));
	CHECK(ui::ParseLength("1fr").Unwrap() == ui::Length::Fr(1.f));
	CHECK(ui::ParseLength("auto").Unwrap().IsAuto());
	const ui::Length calc = ui::ParseLength("calc(100% - 16px)").Unwrap();
	CHECK(calc.unit == ui::Unit::Pct);
	CHECK_NEAR(calc.offset, -16.f, 1e-4);
	ui::LengthBasis basis;
	basis.parent = 200.f;
	CHECK_NEAR(calc.Resolve(basis), 184.f, 1e-3);
}

TEST(CssValues, ShorthandsFollowCssOrder) {
	ui::StyleProps p;
	CHECK(ui::SetDeclaration(p, "padding", "1px 2px 3px 4px"));
	CHECK_EQ(p.paddingTop.Unwrap(), 1.f);
	CHECK_EQ(p.paddingRight.Unwrap(), 2.f);
	CHECK_EQ(p.paddingBottom.Unwrap(), 3.f);
	CHECK_EQ(p.paddingLeft.Unwrap(), 4.f);
	CHECK(ui::SetDeclaration(p, "border-radius", "1px 2px 3px 4px"));
	CHECK_EQ(p.borderTopLeftRadius.Unwrap(), 1.f);
	CHECK_EQ(p.borderTopRightRadius.Unwrap(), 2.f);
	CHECK_EQ(p.borderBottomRightRadius.Unwrap(), 3.f);
	CHECK_EQ(p.borderBottomLeftRadius.Unwrap(), 4.f);
	CHECK(ui::SetDeclaration(p, "border", "2px solid #00ff00"));
	CHECK_EQ(p.borderLeftWidth.Unwrap(), 2.f);
	CHECK(SameColor(p.borderColor.Unwrap(), ui::Rgba(0, 255, 0)));
	CHECK(ui::SetDeclaration(p, "overflow", "hidden auto"));
	CHECK(p.overflowX.Unwrap() == ui::Overflow::Hidden);
	CHECK(p.overflowY.Unwrap() == ui::Overflow::Auto);
	CHECK(ui::SetDeclaration(p, "background", "linear-gradient(to right, #000, #fff)"));
	CHECK(p.backgroundImage.Unwrap().kind == ui::PaintKind::Linear);
	CHECK_NEAR(p.backgroundImage.Unwrap().angle, 90.f, 1e-4);
	CHECK(ui::SetDeclaration(p, "text-decoration", "underline line-through"));
	CHECK(ui::HasDecoration(p.textDecoration.Unwrap(), ui::TextDecoration::Underline));
	CHECK(ui::HasDecoration(p.textDecoration.Unwrap(), ui::TextDecoration::LineThrough));
	CHECK(ui::SetDeclaration(p, "cursor", "pointer"));
	CHECK(p.cursor.Unwrap() == sdl3::SystemCursor::POINTER);
	CHECK(!ui::SetDeclaration(p, "propriete-inconnue", "1"));
	CHECK(!ui::SetDeclaration(p, "width", "pas-une-longueur"));
}

TEST(CssSelectors, SpecificityAndPrivacy) {
	auto type = ui::ParseSelector("button").Unwrap();
	auto cls = ui::ParseSelector("button.danger:hover").Unwrap();
	auto id = ui::ParseSelector("#quit").Unwrap();
	auto self = ui::ParseSelector("this > .item").Unwrap();
	auto mine = ui::ParseSelector("panel this").Unwrap();
	CHECK(type.specificity < cls.specificity);
	CHECK(cls.specificity < id.specificity);
	CHECK(!type.IsPrivate());
	CHECK(!cls.IsPrivate());
	CHECK(id.IsPrivate());
	CHECK(!self.IsPrivate()); // le sujet est .item : partagé
	CHECK(mine.IsPrivate());
	CHECK_EQ(self.combinators.size(), size_t(1));
	CHECK(self.combinators[0] == ui::Combinator::Child);
	CHECK(ui::ParseSelector("a + b").IsError());
	CHECK(ui::ParseSelector("::before").IsError());
	CHECK_EQ(ui::ParseSelectorList("a, b.c, #d").size(), size_t(3));
}

TEST(CssSheet, RulesFontFacesAndWarnings) {
	auto sheet = ui::StyleSheet::Parse(R"css(
		/* commentaire */
		button, .big { padding: 4px; color: red }
		@font-face { font-family: "Pixel"; src: url("fonts/pixel.ttf") format("truetype"); font-weight: bold; }
		@media screen { label { color: blue } }
		panel { colr: red; }
		panel { background: url(img/bg.png) }
	)css",
									   "assets/ui");
	CHECK_EQ(sheet->Rules().size(), size_t(3));
	CHECK_EQ(sheet->Rules()[0].selectors.size(), size_t(2));
	REQUIRE_EQ(sheet->FontFaces().size(), size_t(1));
	CHECK(sheet->FontFaces()[0].family == "Pixel");
	CHECK(sheet->FontFaces()[0].source == "assets/ui/fonts/pixel.ttf");
	CHECK_EQ(sheet->FontFaces()[0].weight, 700);
	CHECK(!sheet->Warnings().empty()); // « colr » inconnue
	const ui::Paint &bg = sheet->Rules()[2].props.backgroundImage.Unwrap();
	CHECK(bg.kind == ui::PaintKind::Image);
	CHECK(bg.source == "assets/ui/img/bg.png");
}

TEST(CssSheet, ImportsAreResolvedRelativelyAndCyclesIgnored) {
	const auto dir = ScratchDir("imports");
	WriteFile(dir / "a.css", "@import \"sub/b.css\";\nbutton { color: #010203; }\n");
	std::filesystem::create_directories(dir / "sub");
	WriteFile(dir / "sub" / "b.css", "@import url(../a.css);\nlabel { color: #040506; }\n");
	auto sheet = ui::StyleSheet::Load(String((dir / "a.css").string().c_str()));
	REQUIRE(sheet.IsOk());
	// b.css (importé) d'abord, puis a.css ; l'import cyclique de a.css est ignoré.
	REQUIRE_EQ(sheet.Value()->Rules().size(), size_t(2));
	CHECK(sheet.Value()->Rules()[0].selectors[0].Subject().type == "label");
	CHECK(sheet.Value()->Rules()[1].selectors[0].Subject().type == "button");
	CHECK(ui::StyleSheet::Load("/chemin/inexistant.css").IsError());
}

TEST(CssSheet, CustomPropertiesAndVar) {
	Harness h;
	h.gui.AddStyleSheet(R"css(
		root { --brand: #112233; --gap: 10px; }
		panel { background-color: var(--brand); gap: var(--gap); border-color: var(--missing, #ff0000); }
	)css");
	ui::Widget p = h.Mount(ui::Panel());
	const ui::ComputedStyle &cs = *p.Computed();
	CHECK(SameColor(cs.backgroundColor, ui::Rgba(0x11, 0x22, 0x33)));
	CHECK_NEAR(cs.gap, 10.f, 1e-4);
	CHECK(SameColor(cs.borderColor, ui::Rgba(255, 0, 0)));
}
