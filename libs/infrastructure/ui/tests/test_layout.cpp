// Mise en page : modèle de boîte, flexbox, grille, pile, positionnement, défilement.
// Sans police, la mesure est l'heuristique : 0,55 em par caractère, 1,3 em de haut.
#include "harness.hpp"
#include "test.hpp"

namespace {

constexpr float FS = 14.f;
constexpr float CW = FS * 0.55f; // largeur d'un caractère
constexpr float LH = FS * 1.3f;	 // hauteur d'une ligne

/// Feuille neutre : aucun espacement du thème ne fausse les mesures.
void Neutral(Harness &h) {
	h.gui.AddStyleSheet("row, column, wrap, grid { gap: 0; } root { font-size: 14px; }");
}

} // namespace

TEST(BoxModel, ALabelIsExactlyItsText) {
	Harness h;
	Neutral(h);
	ui::Widget t = h.Mount(ui::Text("abcd"));
	CHECK(Near(t.Bounds().w, 4.f * CW));
	CHECK(Near(t.Bounds().h, LH));
}

TEST(BoxModel, PaddingAndBorderAddAroundTheText) {
	Harness h;
	Neutral(h);
	ui::Widget t = h.Mount(ui::Text("ab").Padding(math::Sides(1.f, 2.f, 3.f, 4.f)).Border(2.f, ui::Rgba(255, 0, 0)));
	CHECK(Near(t.Bounds().w, 2.f * CW + 1.f + 3.f + 4.f));
	CHECK(Near(t.Bounds().h, LH + 2.f + 4.f + 4.f));
	CHECK(Near(t.ContentBounds().x - t.Bounds().x, 3.f));
	// Une bordure transparente ne prend pas de place.
	ui::Widget u = h.Mount(ui::Text("ab").BorderWidth(5.f));
	CHECK(Near(u.Bounds().w, 2.f * CW));
}

TEST(Flex, ARowAddsChildrenGapsAndPadding) {
	Harness h;
	Neutral(h);
	ui::Widget r = h.Mount(ui::Row().Gap(10.f).Padding(5.f).Children({
		ui::Box().Size(20.f, 10.f),
		ui::Box().Size(30.f, 40.f),
	}));
	CHECK(Near(r.Bounds().w, 5.f + 20.f + 10.f + 30.f + 5.f));
	CHECK(Near(r.Bounds().h, 5.f + 40.f + 5.f));
	auto kids = r.Children();
	CHECK(Near(kids[1].Bounds().x, 5.f + 20.f + 10.f));
}

TEST(Flex, GrowSharesTheRemainingSpace) {
	Harness h;
	Neutral(h);
	ui::Widget a, b, c;
	h.Mount(ui::Row().Width(400.f).Children({
		ui::Box().Width(100.f).Height(10.f).Ref(a),
		ui::Box().Grow(1.f).Height(10.f).Ref(b),
		ui::Box().Grow(3.f).Height(10.f).Ref(c),
	}));
	CHECK(Near(a.Bounds().w, 100.f));
	CHECK(Near(b.Bounds().w, 75.f));
	CHECK(Near(c.Bounds().w, 225.f));
	CHECK(Near(c.Bounds().x, 175.f));
}

TEST(Flex, FrUnitIsAGrowWeight) {
	Harness h;
	Neutral(h);
	ui::Widget a, b;
	h.Mount(ui::Row().Width(300.f).Children({ui::Box().Width(ui::Length::Fr(1.f)).Ref(a),
											 ui::Box().Width(ui::Length::Fr(2.f)).Ref(b)}));
	CHECK(Near(a.Bounds().w, 100.f));
	CHECK(Near(b.Bounds().w, 200.f));
}

TEST(Flex, JustifyAndAlign) {
	Harness h;
	Neutral(h);
	ui::Widget a, b;
	h.Mount(ui::Row().Size(300.f, 100.f).JustifyContent(ui::Justify::SpaceBetween).AlignItems(ui::Align::Center).Children({
		ui::Box().Size(50.f, 20.f).Ref(a),
		ui::Box().Size(50.f, 40.f).Ref(b),
	}));
	CHECK(Near(a.Bounds().x, 0.f));
	CHECK(Near(b.Bounds().x, 250.f));
	CHECK(Near(a.Bounds().y, 40.f));
	CHECK(Near(b.Bounds().y, 30.f));
	ui::Widget c;
	ui::Widget col = h.Mount(ui::Column().Size(200.f, 100.f).Center().Children({ui::Box().Size(40.f, 20.f).Ref(c)}));
	CHECK(Near(c.Bounds().x - col.Bounds().x, 80.f));
	CHECK(Near(c.Bounds().y - col.Bounds().y, 40.f));
}

TEST(Flex, StretchFillsTheCrossAxisUnlessSized) {
	Harness h;
	Neutral(h);
	ui::Widget a, b;
	h.Mount(ui::Column().Width(200.f).Children({ui::Box().Height(10.f).Ref(a), ui::Box().Size(50.f, 10.f).Ref(b)}));
	CHECK(Near(a.Bounds().w, 200.f));
	CHECK(Near(b.Bounds().w, 50.f));
}

TEST(Flex, PercentChildFillsTheParentContentBox) {
	Harness h;
	Neutral(h);
	ui::Widget a;
	h.Mount(ui::Column().Size(200.f, 100.f).Padding(10.f).AlignItems(ui::Align::Start).Children({
		ui::Box().Width(ui::Length::Pct(50.f)).Height(ui::Length::Pct(100.f).Plus(-20.f)).Ref(a),
	}));
	CHECK(Near(a.Bounds().w, 90.f));
	CHECK(Near(a.Bounds().h, 60.f));
}

TEST(Flex, MarginsSeparateSiblings) {
	Harness h;
	Neutral(h);
	ui::Widget a, b;
	h.Mount(ui::Column().AlignItems(ui::Align::Start).Children({
		ui::Box().Size(10.f, 10.f).Margin(5.f).Ref(a),
		ui::Box().Size(10.f, 10.f).Ref(b),
	}));
	CHECK(Near(a.Bounds().x, 5.f));
	CHECK(Near(b.Bounds().y, 20.f));
}

TEST(Flex, MinAndMaxClampTheSize) {
	Harness h;
	Neutral(h);
	ui::Widget a, b;
	h.Mount(ui::Row().Width(600.f).Children({
		ui::Box().Grow().MaxWidth(ui::Length::Px(100.f)).Height(5.f).Ref(a),
		ui::Box().Width(10.f).MinWidth(ui::Length::Px(40.f)).Height(5.f).Ref(b),
	}));
	CHECK(Near(a.Bounds().w, 100.f));
	CHECK(Near(b.Bounds().w, 40.f));
}

TEST(Flex, WrapBreaksIntoLines) {
	Harness h;
	Neutral(h);
	ui::Widget w = h.Mount(ui::Wrap().Width(100.f).Children({
		ui::Box().Size(40.f, 10.f), ui::Box().Size(40.f, 10.f), ui::Box().Size(40.f, 20.f),
	}));
	auto kids = w.Children();
	CHECK(Near(kids[1].Bounds().x, 40.f));
	CHECK(Near(kids[2].Bounds().x, 0.f));
	CHECK(Near(kids[2].Bounds().y, 10.f));
	CHECK(Near(w.Bounds().h, 30.f));
}

TEST(Grid, EqualColumnsAndRows) {
	Harness h;
	Neutral(h);
	ui::Widget g = h.Mount(ui::Grid(3).Width(300.f).Gap(0.f).Children({
		ui::Box().Height(10.f), ui::Box().Height(30.f), ui::Box().Height(10.f), ui::Box().Height(20.f),
	}));
	auto kids = g.Children();
	CHECK(Near(kids[1].Bounds().x, 100.f));
	CHECK(Near(kids[1].Bounds().w, 100.f));
	CHECK(Near(kids[3].Bounds().y, 30.f));
	CHECK(Near(g.Bounds().h, 50.f));
}

TEST(Positioned, AbsoluteChildrenUseAnchorsAndOffsets) {
	Harness h;
	Neutral(h);
	ui::Widget a, b, c;
	h.Mount(ui::Stack().Size(200.f, 100.f).Children({
		ui::Box().Absolute().Size(20.f, 10.f).Offset(5.f, 6.f).Ref(a),
		ui::Box().Absolute().Size(20.f, 10.f).Anchor(ui::Anchor::Center).Ref(b),
		ui::Box().Absolute().Size(20.f, 10.f).Right(4.f).Bottom(2.f).Ref(c),
	}));
	CHECK(Near(a.Bounds().x, 5.f));
	CHECK(Near(a.Bounds().y, 6.f));
	CHECK(Near(b.Bounds().x, 90.f));
	CHECK(Near(b.Bounds().y, 45.f));
	CHECK(Near(c.Bounds().x, 176.f));
	CHECK(Near(c.Bounds().y, 88.f));
}

TEST(Positioned, FixedIgnoresItsParentAndIsDrawnLast) {
	Harness h;
	Neutral(h);
	ui::Widget f;
	h.Mount(ui::Column().Size(100.f, 100.f).Offset(50.f, 50.f).Absolute().Clip().Children({
		ui::Box().Fixed().Size(30.f, 30.f).Offset(400.f, 300.f).Ref(f),
	}));
	CHECK(Near(f.Bounds().x, 400.f));
	CHECK(Near(f.Bounds().y, 300.f));
	// Échappe au clip du parent et passe au premier plan.
	CHECK(ui::HitTest(h.world, {415.f, 315.f}) == f.Entity());
}

TEST(Positioned, RootChildrenFillTheWindowWhenUnsized) {
	Harness h({640.f, 480.f});
	ui::Widget page = h.gui.Mount(ui::Column());
	h.Frame();
	CHECK(Near(page.Bounds().w, 640.f));
	CHECK(Near(page.Bounds().h, 480.f));
	h.gui.SetViewport({320.f, 200.f});
	h.Frame();
	CHECK(Near(page.Bounds().w, 320.f));
}

TEST(Scroll, AVerticalBarTakesItsOwnBand) {
	Harness h;
	Neutral(h);
	ui::Widget view, child;
	h.Mount(ui::ScrollView().Size(100.f, 50.f).Ref(view).Children({ui::Box().Height(200.f).Ref(child)}));
	const ui::Scroll s = *view.Get<ui::Scroll>().Unwrap();
	CHECK(s.gutter.x > 0.f);
	CHECK(Near(child.Bounds().w, 100.f - s.gutter.x));
	CHECK(Near(s.content.y, 200.f));
	h.Wheel(50.f, 25.f, -1.f);
	CHECK(view.ScrollOffset().y > 0.f);
	CHECK(Near(child.Bounds().y, -view.ScrollOffset().y));
}

TEST(Scroll, ContentThatFitsShowsNoBarAndTheBarCanGoAway) {
	Harness h;
	Neutral(h);
	ui::Widget view, child;
	h.Mount(ui::ScrollView().Size(100.f, 50.f).Ref(view).Children({ui::Box().Height(20.f).Ref(child)}));
	CHECK_EQ(view.Get<ui::Scroll>().Unwrap()->gutter.x, 0.f);
	child.Height(300.f);
	h.Frame();
	CHECK(view.Get<ui::Scroll>().Unwrap()->gutter.x > 0.f);
	child.Height(10.f);
	h.Frame();
	CHECK_EQ(view.Get<ui::Scroll>().Unwrap()->gutter.x, 0.f);
}

TEST(TextLayout, WrapMakesTheWidgetTallerNotWider) {
	Harness h;
	Neutral(h);
	ui::Widget t;
	h.Mount(ui::Column().Width(60.f).AlignItems(ui::Align::Start).Children({
		ui::Text("aaaa bbbb cccc dddd").TextWrap().Width(ui::Length::Pct(100.f)).Ref(t),
	}));
	CHECK(Near(t.Bounds().w, 60.f));
	CHECK(t.Bounds().h > LH * 2.5f);
}

TEST(TextLayout, MultiLineTextIsAsTallAsItsLines) {
	Harness h;
	Neutral(h);
	ui::Widget t = h.Mount(ui::Text("a\nbb\nccc"));
	CHECK(Near(t.Bounds().h, 3.f * LH));
	CHECK(Near(t.Bounds().w, 3.f * CW));
}

TEST(TextLayout, EllipsisKeepsTheLongestPrefixThatFits) {
	const ui::LineMeasure m = ui::HeuristicMeasure(FS);
	const String cut = ui::TruncateWithEllipsis("abcdefghij", 5.f * CW, m);
	CHECK(cut.EndsWith("…"));
	CHECK(m(cut).x <= 5.f * CW + 0.01f);
	CHECK(ui::TruncateWithEllipsis("abc", 0.5f, m).IsEmpty());
}

TEST(TextUtils, EscapesTabsAndColumns) {
	CHECK(ui::NormalizeDisplayText("a\r\nb\rc") == "a\nb\nc");
	CHECK(ui::NormalizeDisplayText("a\tb") == "a   b");
	CHECK(ui::NormalizeDisplayText("é\tx") == "é   x");
	CHECK(ui::NormalizeDisplayText("a\x01" "b") == "ab");
	const String line("a\tbé");
	CHECK_EQ(ui::DisplayColumn(line, 2), size_t(4));
	CHECK_EQ(ui::ColumnToByteOffset(line, 4), size_t(2));
	CHECK_EQ(ui::DisplayCells(String("日本")), size_t(4));
	CHECK_EQ(ui::CharCount(std::u16string_view(u"a😀")), size_t(2));
}

TEST(Hidden, HiddenWidgetsTakeNoRoomAndShowWhenBricks) {
	Harness h;
	Neutral(h);
	ui::Widget a, b;
	h.Mount(ui::Row().Children({ui::Box().Size(10.f, 10.f).Ref(a), ui::Box().Size(10.f, 10.f).Ref(b)}));
	a.Hide();
	h.Frame();
	CHECK(Near(b.Bounds().x, 0.f));
	CHECK(!a.IsVisible());
	a.Show();
	h.Frame();
	CHECK(Near(b.Bounds().x, 10.f));
}

TEST(Flex, ColumnItemsKeepTheirContentHeight) {
	Harness h;
	std::vector<ui::AnyWidget> items;
	for (int i = 0; i < 20; ++i)
		items.push_back(ui::Label("ligne").Height(20.f));
	std::vector<ui::AnyWidget> autos;
	for (int i = 0; i < 20; ++i)
		autos.push_back(ui::Label("ligne"));
	ui::Widget scroll = h.Mount(ui::ScrollView().Size(200.f, 100.f).Gap(0.f).Children(std::move(items)));
	ui::Widget column = h.Mount(ui::Column().Size(200.f, 100.f).Gap(0.f).Children(std::move(autos)));
	// Conteneur défilant : rien ne rétrécit, le contenu déborde.
	for (const ui::Widget &c : scroll.Children())
		CHECK(Near(c.Bounds().h, 20.f));
	// Colonne simple : un texte ne rétrécit pas sous sa hauteur de ligne.
	for (const ui::Widget &c : column.Children())
		CHECK(c.Bounds().h >= 14.f);
}

TEST(Flex, AGrowingListLeavesRoomForItsSiblings) {
	Harness h;
	std::vector<String> items;
	for (int i = 0; i < 60; ++i)
		items.push_back("élément");
	ui::Widget list, button;
	h.Mount(ui::Column().Size(300.f, 300.f).Gap(8.f).Children({
		ui::Label("titre"),
		ui::ListBox(items, 0).FillWidth().Grow().Ref(list),
		ui::Row().Children({ui::Button("Jouer").Ref(button)}),
	}));
	const sdl3::FRect b = button.Bounds();
	CHECK(b.y + b.h <= 300.5f + 8.f); // le bouton reste dans la colonne
	CHECK(list.Bounds().h < 300.f);
}

TEST(Flex, FrLengthsFillTheRemainingSpaceOnEitherAxis) {
	Harness h;
	ui::Widget inRow, inColumn, fixed;
	h.Mount(ui::Row().Size(400.f, 100.f).Gap(0.f).AlignItems(ui::Align::Start).Children({
		ui::Box().Size(100.f, 20.f).Ref(fixed),
		ui::Box().GrowWidth().GrowHeight().Ref(inRow),
	}));
	h.Mount(ui::Column().Size(300.f, 200.f).Gap(0.f).AlignItems(ui::Align::Start).Children({
		ui::Box().Height(50.f),
		ui::Box().GrowWidth().GrowHeight().Ref(inColumn),
	}));
	CHECK(Near(inRow.Bounds().w, 300.f)); // axe principal : le reste
	CHECK(Near(inRow.Bounds().h, 100.f)); // axe secondaire : étiré
	CHECK(Near(inColumn.Bounds().w, 300.f));
	CHECK(Near(inColumn.Bounds().h, 150.f));
}

TEST(Flex, ButtonsDoNotShrinkBelowTheirLabel) {
	Harness h;
	ui::Widget button, title;
	h.Mount(ui::Row().Width(200.f).Gap(0.f).Children({
		ui::Label("Un titre bien trop long pour la rangée").Ellipsis().Grow().Ref(title),
		ui::Button("Nouveau monde").Ref(button),
	}));
	const float labelW = button.Find(".button-label").Unwrap().Bounds().w;
	CHECK(button.Bounds().w >= labelW); // le bouton garde son libellé entier
	CHECK(title.Bounds().w < 200.f - labelW + 1.f); // le titre, lui, se tronque
}

TEST(Flex, ARowIsAsTallAsItsWrappedText) {
	Harness h;
	ui::Widget row, text;
	h.Mount(ui::Column().Width(300.f).Gap(0.f).Children({
		ui::Row().Gap(10.f).AlignItems(ui::Align::Start).Ref(row).Children({
			ui::Column().Grow().Children({
				ui::Label("Titre"),
				ui::Label("Une description assez longue pour passer à la ligne plusieurs fois dans la colonne.")
					.TextWrap()
					.GrowWidth()
					.Ref(text),
			}),
			ui::Switch(true),
		}),
	}));
	CHECK(text.Bounds().h > 30.f); // plusieurs lignes
	CHECK(row.Bounds().y + row.Bounds().h >= text.Bounds().y + text.Bounds().h - 0.5f);
}

TEST(Flex, NestedFrColumnsKeepAScrollingListInsideTheWindow) {
	// Panneau d'éditeur : rangée → colonne → corps (1fr) → page (1fr) →
	// [bandeau, liste défilante (1fr)] : la liste défile, elle ne déborde pas.
	Harness h;
	ui::Widget list;
	std::vector<ui::AnyWidget> rows;
	for (int i = 0; i < 80; ++i)
		rows.push_back(ui::Label("ligne"));
	h.Mount(ui::Row().Size(400.f, 300.f).Gap(0.f).Children({
		ui::Column().Width(200.f).GrowHeight().Gap(0.f).Children({
			ui::Row().Height(24.f),
			ui::Column().GrowWidth().GrowHeight().Gap(0.f).Children({
				ui::Column().GrowWidth().GrowHeight().Gap(0.f).Children({
					ui::Column().Height(60.f),
					ui::Column().GrowWidth().GrowHeight().Scrollable().Ref(list).Children(std::move(rows)),
				}),
			}),
		}),
	}));
	const sdl3::FRect b = list.Bounds();
	CHECK(Near(b.y + b.h, 300.f));
	CHECK(Near(b.h, 300.f - 24.f - 60.f));
}
