// Widgets composés : chacun n'est qu'un arbre de briques ; on vérifie leur
// comportement de bout en bout (évènements SDL → valeurs, évènements logiques).
#include "harness.hpp"
#include "test.hpp"

TEST(Checkable, CheckboxRadioAndSwitch) {
	Harness h;
	bool last = false;
	int toggles = 0;
	ui::Widget cb = h.Mount(ui::Checkbox("Option").OnToggle([&](bool on) {
		last = on;
		++toggles;
	}));
	h.Click(cb);
	CHECK(cb.Checked());
	CHECK(last);
	h.Click(cb);
	CHECK(!cb.Checked());
	CHECK_EQ(toggles, 2);
	CHECK(cb.Text() == "Option"); // libellé interne (TextSlot)
	cb.SetText("Autre");
	CHECK(cb.Find(".checkbox-label").Unwrap().Text() == "Autre");

	ui::Widget r1, r2;
	h.Mount(ui::Column().Children({ui::Radio("A").Ref(r1), ui::Radio("B").Ref(r2)}));
	h.Click(r1);
	CHECK(r1.Checked());
	h.Click(r2);
	CHECK(r2.Checked());
	CHECK(!r1.Checked());
	h.Click(r2); // un radio coché le reste
	CHECK(r2.Checked());

	ui::Widget sw = h.Mount(ui::Switch(false));
	h.Click(sw);
	CHECK(sw.Checked());
	ui::Widget thumb = sw.Find(".switch-thumb").Unwrap();
	const float x0 = thumb.Bounds().x;
	for (int i = 0; i < 10; ++i)
		h.Advance(0.05f);
	CHECK(thumb.Bounds().x > x0); // le pouce glisse (Check animé)
}

TEST(Range, SliderDragWheelAndKeys) {
	Harness h;
	float last = -1.f;
	ui::Widget s = h.Mount(ui::Slider(0.f, 100.f, 0.f, 1.f).Width(210.f).OnChange([&](float v) { last = v; }));
	const sdl3::FRect r = s.ContentBounds();
	h.Click(r.x + r.w - 1.f, r.y + r.h * 0.5f);
	CHECK(s.Value() > 95.f);
	CHECK(Near(last, s.Value()));
	h.Drag(r.x + r.w - 1.f, r.y + r.h * 0.5f, r.x + 1.f, r.y + r.h * 0.5f);
	CHECK(s.Value() < 5.f);
	const float before = s.Value();
	h.Wheel(r.x + r.w * 0.5f, r.y + r.h * 0.5f, 1.f);
	CHECK(s.Value() > before);
	s.Focus();
	const float v = s.Value();
	h.Key(SDLK_RIGHT);
	CHECK(Near(s.Value(), v + 1.f, 0.01f));
	h.Key(SDLK_END);
	CHECK(Near(s.Value(), 100.f));
	// Le pouce suit la valeur.
	ui::Widget thumb = s.Find(".slider-thumb").Unwrap();
	CHECK(thumb.Bounds().x + thumb.Bounds().w > r.x + r.w - 2.f);
}

TEST(Range, KnobAndDragValueAreRelative) {
	Harness h;
	ui::Widget k = h.Mount(ui::Knob(0.f, 1.f, 0.5f));
	const sdl3::FPoint c = Harness::Center(k);
	h.Drag(c.x, c.y, c.x, c.y - 60.f);
	CHECK(k.Value() > 0.7f);
	ui::Widget d = h.Mount(ui::DragValue(0.f, 100.f, 10.f, 0.f, 0.5f, 1));
	CHECK(d.Text() == "10.0");
	const sdl3::FPoint dc = Harness::Center(d);
	h.Drag(dc.x, dc.y, dc.x + 40.f, dc.y);
	CHECK(Near(d.Value(), 30.f, 0.6f));
	CHECK(d.Text() == String::From(d.Value(), 1));
	// Double clic : saisie directe.
	h.Click(dc.x, dc.y);
	h.Click(dc.x, dc.y);
	CHECK(d.Has<ui::TextEditor>());
	h.Key(SDLK_A, SDL_KMOD_LCTRL);
	h.Type("42");
	h.Key(SDLK_RETURN);
	CHECK(Near(d.Value(), 42.f));
	CHECK(!d.Has<ui::TextEditor>());
	CHECK(d.Text() == "42.0");
}

TEST(Range, NumberInputButtonsAndTyping) {
	Harness h;
	ui::Widget n = h.Mount(ui::NumberInput(0.f, 10.f, 5.f, 1.f, 0));
	ui::Widget plus = n.Find(".plus").Unwrap(), minus = n.Find(".minus").Unwrap();
	h.Click(plus);
	CHECK(Near(n.Value(), 6.f));
	h.Click(minus);
	h.Click(minus);
	CHECK(Near(n.Value(), 4.f));
	ui::Widget value = n.Find(".number-value").Unwrap();
	h.Click(value);
	h.Key(SDLK_A, SDL_KMOD_LCTRL);
	h.Type("99");
	h.Key(SDLK_RETURN);
	CHECK(Near(n.Value(), 10.f)); // borné
	CHECK(value.Text() == "10");
}

TEST(Range, ProgressAndScrollBar) {
	Harness h;
	ui::Widget p = h.Mount(ui::Progress(0.f, 1.f, 0.25f).Width(200.f));
	ui::Widget fill = p.Find(".progress-fill").Unwrap();
	CHECK(Near(fill.Bounds().w, p.ContentBounds().w * 0.25f, 1.f));
	p.SetValue(1.f);
	h.Frame();
	CHECK(Near(fill.Bounds().w, p.ContentBounds().w, 1.f));
	float offset = -1.f;
	ui::Widget sb = h.Mount(ui::ScrollBar(400.f, 100.f).Width(200.f).OnChange([&](float v) { offset = v; }));
	ui::Widget thumb = sb.Find(".scrollbar-thumb").Unwrap();
	CHECK(Near(thumb.Bounds().w, sb.ContentBounds().w * 0.25f, 1.f));
	const sdl3::FRect r = sb.ContentBounds();
	h.Click(r.x + r.w - 2.f, r.y + r.h * 0.5f);
	CHECK(offset > 290.f);
}

TEST(Selection, ComboOpensSelectsAndCloses) {
	Harness h;
	int selected = -1;
	ui::Widget c = h.Mount(ui::Combo({"Un", "Deux", "Trois"}, 0).OnSelect([&](int i) { selected = i; }));
	CHECK(c.Text() == "Un");
	h.Click(c);
	CHECK(c.IsOpen());
	ui::Widget item = c.FindAll("combo-item")[2];
	CHECK(item.IsVisible());
	h.Click(item);
	CHECK_EQ(selected, 2);
	CHECK_EQ(c.Selected(), 2);
	CHECK(c.Text() == "Trois");
	CHECK(!c.IsOpen());
	// Clic extérieur : fermeture.
	h.Click(c);
	CHECK(c.IsOpen());
	h.Click(790.f, 590.f);
	CHECK(!c.IsOpen());
	// Nouveaux éléments.
	c.SetItems({"A", "B"});
	h.Frame();
	CHECK_EQ(c.FindAll("combo-item").size(), size_t(2));
	CHECK_EQ(c.Selected(), -1);
}

TEST(Selection, CategorizedComboOpensItsGroupOnHover) {
	Harness h;
	int selected = -1;
	ui::Widget c = h.Mount(ui::Combo({{"", {"Seul"}}, {"Scènes", {"A", "B"}}}, 0).OnSelect([&](int i) { selected = i; }));
	h.Click(c);
	ui::Widget group = c.Find(".combo-group").Unwrap();
	const sdl3::FPoint g = Harness::Center(group);
	h.Move(g.x, g.y);
	h.Advance(0.3f);
	ui::Widget b = c.FindAll("combo-item").back();
	CHECK(b.IsVisible());
	h.Click(b);
	CHECK_EQ(selected, 2);
	CHECK(!c.IsOpen());
}

TEST(Selection, ListBoxClickCtrlShiftAndKeys) {
	Harness h;
	ui::Widget l = h.Mount(ui::ListBox({"a", "b", "c", "d"}, -1, true).Size(120.f, 200.f));
	auto rows = l.FindAll("list-item");
	h.Click(rows[1]);
	CHECK_EQ(l.Selected(), 1);
	h.Key(SDLK_LCTRL, SDL_KMOD_LCTRL);
	{
		const sdl3::FPoint c = Harness::Center(rows[3]);
		h.Move(c.x, c.y);
		sdl3::Event ev = h.Base(SDL_EVENT_MOUSE_BUTTON_DOWN);
		ev.raw.button.button = SDL_BUTTON_LEFT;
		ev.raw.button.down = true;
		ev.raw.button.x = c.x;
		ev.raw.button.y = c.y;
		SDL_SetModState(SDL_KMOD_LCTRL);
		h.Send(ev);
		h.Up(c.x, c.y);
		SDL_SetModState(SDL_KMOD_NONE);
	}
	CHECK_EQ(l.SelectedAll().size(), size_t(2));
	l.Focus();
	h.Key(SDLK_UP);
	CHECK_EQ(l.Selected(), 2);
	CHECK_EQ(l.SelectedAll().size(), size_t(1));
}

TEST(Selection, TabViewShowsOnlyTheActivePage) {
	Harness h;
	ui::Widget p0, p1;
	int active = -1;
	ui::Widget t = h.Mount(ui::TabView({"Un", "Deux"}, 0).OnSelect([&](int i) { active = i; }).Children({
		ui::Text("page 0").Ref(p0),
		ui::Text("page 1").Ref(p1),
	}));
	h.Frame();
	CHECK(p0.Get<ui::Rect>().Unwrap()->visible);
	CHECK(!p1.Get<ui::Rect>().Unwrap()->visible);
	h.Click(t.FindAll("tab")[1]);
	CHECK_EQ(active, 1);
	CHECK(!p0.Get<ui::Rect>().Unwrap()->visible);
	CHECK(p1.Get<ui::Rect>().Unwrap()->visible);
}

TEST(Collapsible, ExpanderAndTreeNode) {
	Harness h;
	ui::Widget body;
	ui::Widget e = h.Mount(ui::Expander("Section", true).Children({ui::Text("corps").Ref(body)}));
	CHECK(body.Get<ui::Rect>().Unwrap()->visible);
	h.Click(e.Find("expander-header").Unwrap());
	CHECK(!e.Checked());
	CHECK(!body.Get<ui::Rect>().Unwrap()->visible);

	int selected = -1;
	ui::Widget leaf;
	ui::Widget tree = h.Mount(ui::SelectionList(false).OnSelect([&](int i) { selected = i; }).Children({
		ui::TreeNode("Racine", 0, true).Children({ui::TreeNode("Feuille", 1).Ref(leaf)}),
	}));
	h.Click(leaf.Find("tree-header").Unwrap());
	CHECK_EQ(selected, 1);
	ui::Widget rootNode = tree.Find("tree-node").Unwrap();
	h.Click(rootNode.Find("tree-toggle").Unwrap());
	CHECK(!rootNode.Checked());
	CHECK_EQ(selected, 1); // la flèche replie sans sélectionner
	CHECK(!leaf.Get<ui::Rect>().Unwrap()->visible);
}

TEST(TextEdit, TypingCursorSelectionAndSubmit) {
	Harness h;
	String submitted, changed;
	ui::Widget in = h.Mount(ui::Input("nom").Width(200.f).OnSubmit([&](const String &s) { submitted = s; }).OnTextChange(
		[&](const String &s) { changed = s; }));
	h.Click(in);
	CHECK(h.Type("abc"));
	CHECK(in.Text() == "abc");
	CHECK(changed == "abc");
	h.Key(SDLK_LEFT);
	h.Key(SDLK_BACKSPACE);
	CHECK(in.Text() == "ac");
	h.Key(SDLK_HOME);
	h.Type("é");
	CHECK(in.Text() == "éac");
	h.Key(SDLK_END);
	h.Key(SDLK_LEFT, SDL_KMOD_LSHIFT);
	h.Type("Z");
	CHECK(in.Text() == "éaZ");
	CHECK(h.Key(SDLK_RETURN));
	CHECK(submitted == "éaZ");
	// Une touche capturée par le champ est consommée ; F5 passe à l'application.
	CHECK(h.Key(SDLK_X));
	CHECK(!h.Key(SDLK_F5));
	// Échap rend le focus.
	h.Key(SDLK_ESCAPE);
	CHECK(!h.gui.Focused().Valid());
	CHECK(!h.Key(SDLK_Q));
}

TEST(TextEdit, MultilineReadOnlyAndPassword) {
	Harness h;
	ui::Widget area = h.Mount(ui::TextArea().Size(200.f, 80.f));
	h.Click(area);
	h.Type("un");
	h.Key(SDLK_RETURN);
	h.Type("deux");
	CHECK(area.Text() == "un\ndeux");
	h.Key(SDLK_UP);
	h.Key(SDLK_END);
	h.Type("!");
	CHECK(area.Text() == "un!\ndeux");

	ui::Widget log = h.Mount(ui::LogView().Size(200.f, 60.f));
	log.SetText("ligne");
	h.Click(log);
	CHECK(!h.Type("x")); // lecture seule : la frappe n'est pas captée
	CHECK(log.Text() == "ligne");

	ui::Widget pw = h.Mount(ui::Password().Width(120.f));
	h.Click(pw);
	h.Type("secret");
	CHECK(pw.Text() == "secret");
	CHECK(pw.Get<ui::TextEditor>().Unwrap()->password);
}

TEST(TextEdit, CaretFollowsTheMouseClick) {
	Harness h;
	ui::Widget in = h.Mount(ui::Input("", "abcdef").Width(300.f).Padding(0.f).BorderWidth(0.f));
	const sdl3::FRect c = in.ContentBounds();
	const float cw = 14.f * 0.55f;
	h.Click(c.x + cw * 3.f + 1.f, c.y + c.h * 0.5f);
	CHECK_EQ(in.Get<ui::TextEditor>().Unwrap()->cursor, size_t(3));
}

TEST(Popups, PopupModalAndEscape) {
	Harness h;
	ui::Widget pop = h.Mount(ui::Popup().Children({ui::Text("menu")}));
	CHECK(!pop.IsOpen());
	pop.OpenAt({100.f, 100.f});
	h.Frame();
	CHECK(pop.IsOpen());
	CHECK(Near(pop.Bounds().x, 100.f));
	h.Press(SDLK_ESCAPE);
	CHECK(!pop.IsOpen());

	int behind = 0;
	ui::Widget back = h.Mount(ui::Button("derrière").OnClick([&] { ++behind; }));
	ui::Widget inside;
	ui::Widget modal = h.Mount(ui::Modal(ui::Panel().Size(200.f, 100.f).Children({ui::Button("ok").Ref(inside)})));
	modal.Open();
	h.Frame();
	CHECK(h.gui.HasOpenModal());
	h.Click(back);
	CHECK_EQ(behind, 0); // bloqué par la modale
	CHECK(modal.IsOpen()); // un clic sur le voile ne ferme pas une modale
	CHECK(inside.Get<ui::Rect>().Unwrap()->visible);
	h.Press(SDLK_ESCAPE);
	CHECK(!modal.IsOpen());
	h.Click(back);
	CHECK_EQ(behind, 1);
}

TEST(Popups, MenuBarSubmenuAndChainClose) {
	Harness h;
	int saved = 0, exported = 0;
	ui::Widget file, edit;
	h.Mount(ui::MenuBar().Children({
		ui::Menu("Fichier").Ref(file).Children({
			ui::MenuItem("Enregistrer", "Ctrl+S").OnClick([&] { ++saved; }),
			ui::SubMenu("Exporter").Children({ui::MenuItem("PNG").OnClick([&] { ++exported; })}),
		}),
		ui::Menu("Édition").Ref(edit).Children({ui::MenuItem("Annuler")}),
	}));
	h.Click(file);
	CHECK(file.IsOpen());
	// Barre de menus : survoler l'autre menu bascule dessus.
	const sdl3::FPoint e = Harness::Center(edit);
	h.Move(e.x, e.y);
	h.Advance(0.01f);
	CHECK(edit.IsOpen());
	CHECK(!file.IsOpen());
	h.Click(file);
	CHECK(file.IsOpen());
	// Sous-menu ouvert au survol, feuille : action puis fermeture de toute la chaîne.
	ui::Widget sub = file.Find(".submenu").Unwrap();
	const sdl3::FPoint s = Harness::Center(sub);
	h.Move(s.x, s.y);
	h.Advance(0.3f);
	CHECK(sub.IsOpen());
	ui::Widget png = sub.FindAll("menu-item").back();
	h.Click(png);
	CHECK_EQ(exported, 1);
	CHECK(!sub.IsOpen());
	CHECK(!file.IsOpen());
	h.Click(file);
	h.Click(file.FindAll("menu-item")[0]);
	CHECK_EQ(saved, 1);
}

TEST(Popups, ContextMenuOpensAtThePointer) {
	Harness h;
	int renamed = 0;
	sdl3::FPoint asked{};
	ui::Widget target = h.Mount(ui::Panel().Size(200.f, 100.f).OnContextMenu([&](sdl3::FPoint p) { asked = p; })
									.ContextMenu(ui::ContextMenu().Children({
										ui::MenuItem("Renommer").OnClick([&] { ++renamed; }),
									})));
	const sdl3::FPoint c = Harness::Center(target);
	h.Click(c.x, c.y, SDL_BUTTON_RIGHT);
	CHECK(Near(asked.x, c.x));
	ui::Widget menu = target.Find(".context-menu").Unwrap();
	CHECK(menu.IsOpen());
	CHECK(Near(menu.Bounds().x, c.x));
	h.Click(menu.Find("menu-item").Unwrap());
	CHECK_EQ(renamed, 1);
	CHECK(!menu.IsOpen());
}

TEST(Table, SortResizeAndSelect) {
	Harness h;
	int sorted = -1, selected = -1;
	ui::Widget t = h.Mount(ui::Table({{"Nom", 100.f}, {"Âge", 80.f}}, {{"Zoé", "30"}, {"Alice", "4"}, {"Marc", "12"}})
							   .OnChange([&](float c) { sorted = int(c); })
							   .OnSelect([&](int i) { selected = i; }));
	auto headers = t.FindAll("table-header-cell");
	h.Click(headers[1]);
	CHECK_EQ(sorted, 1);
	auto rows = t.FindAll("table-row");
	CHECK(rows[0].Find(".table-cell-text").Unwrap().Text() == "Alice"); // 4 < 12 < 30 (numérique)
	CHECK(headers[1].Find(".sort-indicator").Unwrap().Text() == "▲");
	h.Click(rows[2]);
	CHECK_EQ(selected, 0); // index de la ligne d'origine (Zoé)
	// Redimensionnement de colonne.
	ui::Widget resizer = headers[0].Find(".table-resizer").Unwrap();
	const sdl3::FPoint r = Harness::Center(resizer);
	h.Drag(r.x, r.y, r.x + 50.f, r.y);
	CHECK(Near(t.Get<ui::TableModel>().Unwrap()->columns[0].width, 150.f, 1.f));
	CHECK(Near(rows[0].Children()[0].Bounds().w, 150.f, 1.f));
}

TEST(Splitter, TheHandleResizesTheFirstPane) {
	Harness h;
	ui::Widget s = h.Mount(ui::Splitter(ui::Box().Fill(), ui::Box().Fill(), ui::Orientation::Horizontal, 100.f).Size(400.f, 100.f));
	auto parts = s.Children();
	ui::Widget handle = parts[1];
	const sdl3::FPoint c = Harness::Center(handle);
	h.Drag(c.x, c.y, c.x + 60.f, c.y);
	CHECK(Near(parts[0].Bounds().w, 160.f, 1.f));
	CHECK(Near(parts[2].Bounds().x + parts[2].Bounds().w, s.Bounds().x + 400.f, 1.f));
}

TEST(Color, PickerAndSwatch) {
	Harness h;
	ui::Color last{};
	ui::Widget p = h.Mount(ui::ColorPicker(ui::Rgba(255, 0, 0), [&](ui::Color c) { last = c; }));
	ui::Widget hex = p.Find(".color-hex").Unwrap();
	CHECK(hex.Text() == "#FF0000FF");
	ui::Widget hue = p.Find(".hue").Unwrap();
	hue.SetValue(120.f, true);
	h.Frame();
	CHECK(last.g > 0.9f && last.r < 0.1f);
	CHECK(hex.Text() == "#00FF00FF");
	h.Click(hex);
	h.Key(SDLK_A, SDL_KMOD_LCTRL);
	h.Type("#0000FF80");
	h.Key(SDLK_RETURN);
	CHECK(last.b > 0.9f);
	CHECK(Near(last.a, 0.5f, 0.01f));
	h.gui.ClearFocus(); // un champ en cours de saisie n'est pas réécrit
	p.SetColorValue(ui::Rgba(255, 255, 0));
	h.Frame();
	CHECK(hex.Text() == "#FFFF00FF");
	ui::Widget sw = h.Mount(ui::ColorSwatch(ui::Rgba(10, 20, 30)));
	sw.SetColorValue(ui::Rgba(200, 0, 0));
	h.Frame();
	CHECK(sw.Find(".color-swatch-fill").Unwrap().Computed()->backgroundColor.r > 0.7f);
}

TEST(Dates, CalendarNavigationAndSelection) {
	CHECK_EQ(ui::DaysInMonth(2024, 2), 29);
	CHECK_EQ(ui::DaysInMonth(2023, 2), 28);
	CHECK_EQ(ui::FirstWeekdayOfMonth(2026, 1), 3); // jeudi
	Harness h;
	int y = 0, m = 0, d = 0;
	ui::Widget cal = h.Mount(ui::Calendar(2026, 1, -1, [&](int yy, int mm, int dd) {
		y = yy;
		m = mm;
		d = dd;
	}));
	CHECK(cal.Find(".calendar-title").Unwrap().Text() == "Janvier 2026");
	auto days = cal.FindAll("calendar-day");
	REQUIRE_EQ(days.size(), size_t(42));
	h.Click(days[3 + 14]); // le 15
	CHECK_EQ(d, 15);
	CHECK(days[17].Checked());
	h.Click(cal.FindAll(".calendar-nav")[1]);
	CHECK(cal.Find(".calendar-title").Unwrap().Text() == "Février 2026");
	CHECK(y == 2026 && m == 1);
}

TEST(DragDrop, PayloadsReachCompatibleTargets) {
	Harness h;
	int64_t dropped = 0;
	int starts = 0;
	ui::Widget src, ok, refused;
	h.Mount(ui::Row().Gap(20.f).Children({
		ui::Box().Size(50.f, 50.f).DragSource("node", 42, "nœud").OnDragStart([&] { ++starts; }).Ref(src),
		ui::Box().Size(50.f, 50.f).DropTarget("node").OnDrop([&](int64_t id) { dropped = id; }).Ref(ok),
		ui::Box().Size(50.f, 50.f).DropTarget("asset").OnDrop([&](int64_t id) { dropped = -id; }).Ref(refused),
	}));
	const sdl3::FPoint a = Harness::Center(src), b = Harness::Center(ok), c = Harness::Center(refused);
	h.Down(a.x, a.y);
	h.Move(a.x + 10.f, a.y);
	h.Move(b.x, b.y);
	CHECK(ui::HasState(ok.GetState(), ui::State::DragOver));
	h.Up(b.x, b.y);
	CHECK_EQ(dropped, 42);
	CHECK_EQ(starts, 1);
	dropped = 0;
	h.Drag(a.x, a.y, c.x, c.y);
	CHECK_EQ(dropped, 0); // genre refusé
	// Un simple clic sur la source n'est pas un dépôt.
	int clicks = 0;
	src.OnClick([&] { ++clicks; });
	h.Click(src);
	CHECK_EQ(clicks, 1);
}

TEST(DragDrop, ReorderableRows) {
	Harness h;
	int from = -1, to = -1;
	ui::Widget list = h.Mount(ui::Column().Gap(0.f).OnReorder([&](int f, int t) {
		from = f;
		to = t;
	}).Children({
		ui::Text("a").Height(20.f).Reorderable(),
		ui::Text("b").Height(20.f).Reorderable(),
		ui::Text("c").Height(20.f).Reorderable(),
	}));
	auto rows = list.Children();
	const sdl3::FPoint a = Harness::Center(rows[0]);
	const sdl3::FRect last = rows[2].Bounds();
	h.Drag(a.x, a.y, a.x, last.y + last.h - 2.f);
	CHECK_EQ(from, 0);
	CHECK_EQ(to, 2);
	CHECK(list.Children()[2].Text() == "a");
}

TEST(Tooltip, AppearsAfterADelay) {
	Harness h;
	ui::Widget b = h.Mount(ui::Button("?").Tooltip("Aide"));
	const sdl3::FPoint c = Harness::Center(b);
	h.Move(c.x, c.y);
	h.Advance(0.3f);
	CHECK(!h.gui.Ctx().input.tooltipVisible);
	h.Advance(0.5f);
	CHECK(h.gui.Ctx().input.tooltipVisible);
	CHECK(h.gui.Ctx().input.tooltipText == "Aide");
	h.Move(790.f, 590.f);
	h.Advance(0.01f);
	CHECK(!h.gui.Ctx().input.tooltipVisible);
}

TEST(Tooltip, DisappearsWhenItsWidgetIsHiddenUnderAStillPointer) {
	Harness h;
	ui::Widget b = h.Mount(ui::Button("?").Tooltip("Aide"));
	const sdl3::FPoint c = Harness::Center(b);
	h.Move(c.x, c.y);
	h.Advance(0.8f);
	CHECK(h.gui.Ctx().input.tooltipVisible);
	b.Hide(); // ex. le bouton qui bascule en plein écran masque l'éditeur
	h.Frame();
	h.Advance(0.01f);
	CHECK(!h.gui.Ctx().input.tooltipVisible);
}

TEST(Queries, FindByTypeIdClassAndDestroy) {
	Harness h;
	h.Mount(ui::Column().Id("page").Children({
		ui::Button("a").Class("sidebar-icon"),
		ui::Button("b").Class("sidebar-icon").Id("quit"),
		ui::Text("c"),
	}));
	CHECK_EQ(h.gui.FindAll("button").size(), size_t(2));
	CHECK_EQ(h.gui.FindAll(".sidebar-icon").size(), size_t(2));
	CHECK_EQ(h.gui.FindAll("#page > button.sidebar-icon").size(), size_t(2));
	ui::Widget quit = h.gui.Find("#quit").Unwrap();
	CHECK(quit.Text() == "b");
	quit.Destroy();
	CHECK(!quit.Valid());
	CHECK(h.gui.Find("#quit").IsNone());
	ui::Widget page = h.gui.Find("#page").Unwrap();
	page.Append(ui::Text("ajouté").Id("new"));
	CHECK(h.gui.Find("#new").IsSome());
	page.Clear();
	CHECK_EQ(page.ChildCount(), size_t(0));
}

TEST(Popups, ModalOpenedFromASiblingButton) {
	Harness h;
	ui::Widget modal, open;
	h.Mount(ui::Stack().Children({
		ui::Row().Children({ui::Button("Ouvrir").Ref(open).OnClick([&] { modal.Open(); })}),
		ui::Modal(ui::Panel().Size(200.f, 100.f).Children({ui::Button("Fermer").OnClick([&] { modal.Close(); })}))
			.Ref(modal),
	}));
	CHECK(!modal.IsOpen());
	h.Click(open);
	CHECK(modal.IsOpen());
	CHECK(h.gui.HasOpenModal());
	CHECK(modal.Bounds().w > 700.f); // voile plein écran
	ui::Widget close = modal.Find("button").Unwrap();
	CHECK(close.Bounds().w > 0.f);
	h.Click(close);
	CHECK(!modal.IsOpen());
}

TEST(Checkable, SwitchInsideAScrollingColumn) {
	Harness h;
	ui::Widget sw;
	bool on = false;
	std::vector<ui::AnyWidget> filler;
	for (int i = 0; i < 30; ++i)
		filler.push_back(ui::Label("ligne"));
	h.Mount(ui::Column().Size(300.f, 400.f).Overflow(ui::Overflow::Auto).Children({
		ui::Column().Children({
			ui::Row().Children({ui::Label("Oscillateur").Grow(), ui::Switch(false).Ref(sw).OnToggle([&](bool v) { on = v; })}),
		}),
		ui::Column().Children(std::move(filler)),
	}));
	h.Click(sw);
	CHECK(sw.Checked());
	CHECK(on);
}

TEST(Composite, AppendTargetsTheBodyOfAnExpander) {
	Harness h;
	ui::Widget exp = h.Mount(ui::Expander("Détails", false));
	ui::Widget added = exp.Append(ui::Label("ajouté après coup"));
	h.Frame();
	CHECK(added.Parent().Unwrap().Type() == "expander-body");
	CHECK(!added.IsVisible() || added.Bounds().h == 0.f); // replié : caché
	h.Click(exp.FindType("expander-header").Unwrap());
	CHECK(added.Bounds().h > 0.f);
	exp.Clear(); // vide le corps, garde l'en-tête
	CHECK(!added.Valid());
	CHECK(exp.FindType("expander-header").IsSome());
}

TEST(Composite, MenusGrowTheirPopupImperatively) {
	Harness h;
	ui::Widget bar = h.Mount(ui::MenuBar());
	ui::Widget file = bar.Append(ui::Menu("Fichier"));
	int opened = 0;
	file.Append(ui::MenuItem("Ouvrir").OnClick([&] { ++opened; }));
	ui::Widget recent = file.Append(ui::SubMenu("Récents"));
	recent.Append(ui::MenuItem("projet.json"));
	h.Frame();
	CHECK_EQ(file.FindType("popup").Unwrap().FindAll("menu-item").size(), size_t(3));
	h.Click(file);
	CHECK(file.FindType("popup").Unwrap().IsOpen());
	h.Click(file.Find("menu-item").Unwrap());
	CHECK_EQ(opened, 1);
}

TEST(Composite, ContextMenuShowsEveryItemAppendedAfterASubmenu) {
	Harness h;
	ui::Widget menu = h.Mount(ui::ContextMenu());
	ui::Widget create = menu.Append(ui::SubMenu("Créer"));
	create.Append(ui::MenuItem("Cube"));
	ui::Widget duplicate = menu.Append(ui::MenuItem("Dupliquer", "Ctrl+D"));
	ui::Widget remove = menu.Append(ui::MenuItem("Supprimer", "Suppr"));
	h.Frame();
	menu.OpenAt(sdl3::FPoint{100.f, 100.f});
	h.Frame();
	const sdl3::FRect box = menu.Bounds();
	CHECK(duplicate.Bounds().h > 0.f);
	CHECK(remove.Bounds().y >= duplicate.Bounds().y + duplicate.Bounds().h - 0.5f);
	CHECK(box.y + box.h >= remove.Bounds().y + remove.Bounds().h - 0.5f);
	CHECK(create.Bounds().y < duplicate.Bounds().y);
}

TEST(Composite, AMenuEntryCanOpenADialog) {
	Harness h;
	ui::Widget dialog = h.Mount(ui::Modal(ui::Panel().Size(200.f, 100.f)));
	ui::Widget menu = h.Mount(ui::ContextMenu());
	ui::Widget rename = menu.Append(ui::MenuItem("Renommer…").OnClick([&] { dialog.Open(); }));
	h.Frame();
	menu.OpenAt(sdl3::FPoint{40.f, 40.f});
	h.Frame();
	h.Click(rename);
	CHECK(!menu.IsOpen()); // le menu se ferme…
	CHECK(dialog.IsOpen()); // …pas la boîte que son entrée vient d'ouvrir
}
