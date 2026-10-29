// Définitions de ui/widgets.hpp — chaque widget est un arbre de briques atomiques
#include <ui/systems.hpp>
#include <ui/widgets.hpp>

#include <algorithm>
#include <memory>

namespace ui {

namespace {

/// Range des propriétés communes à un champ éditable.
WidgetBuilder EditorBox(String type, String placeholder, String value, bool multiline) {
	WidgetBuilder b(std::move(type));
	TextEditor ed;
	ed.placeholder = std::move(placeholder);
	ed.multiline = multiline;
	ed.maxLength = multiline ? (1u << 22) : 4096;
	ed.cursor = ed.anchor = value.size();
	b.With(TextContent{std::move(value)}).With(std::move(ed)).Focusable();
	return b;
}

} // namespace

// ============================================================================
// Conteneurs
// ============================================================================

WidgetBuilder Box(String type) { return WidgetBuilder(std::move(type)); }
WidgetBuilder Column() { return WidgetBuilder("column"); }
WidgetBuilder Row() { return WidgetBuilder("row"); }
WidgetBuilder Wrap() { return WidgetBuilder("wrap"); }
WidgetBuilder Stack() { return WidgetBuilder("stack"); }

WidgetBuilder Grid(int columns) {
	WidgetBuilder b("grid");
	b.GridColumns(columns);
	return b;
}

WidgetBuilder ScrollView() { return WidgetBuilder("scroll-view"); }
WidgetBuilder Panel() { return WidgetBuilder("panel"); }

WidgetBuilder Spacer() {
	WidgetBuilder b("spacer");
	b.PointerThrough();
	return b;
}

WidgetBuilder Separator(Orientation orientation) {
	WidgetBuilder b("separator");
	b.Class(orientation == Orientation::Horizontal ? "horizontal" : "vertical");
	return b;
}

// ============================================================================
// Contenus
// ============================================================================

WidgetBuilder Text(String text) {
	WidgetBuilder b("text");
	b.With(TextContent{std::move(text)});
	return b;
}

WidgetBuilder Label(String text) {
	WidgetBuilder b("label");
	b.With(TextContent{std::move(text)});
	return b;
}

WidgetBuilder Heading(String text, int level) {
	WidgetBuilder b("heading");
	b.Class(String("h") + String::From(math::Clamp(level, 1, 6))).With(TextContent{std::move(text)});
	return b;
}

WidgetBuilder Badge(String text) {
	WidgetBuilder b("badge");
	b.With(TextContent{std::move(text)});
	return b;
}

WidgetBuilder Image(String source) {
	WidgetBuilder b("image");
	b.With(ImageContent{std::move(source)});
	return b;
}

WidgetBuilder Icon(String glyph, String family, float size) {
	WidgetBuilder b("icon");
	b.With(IconContent{std::move(glyph), std::move(family)}).FontSize(size);
	return b;
}

WidgetBuilder ShapeBox(ShapeKind kind, float thickness) {
	WidgetBuilder b("shape");
	Shape s;
	s.kind = kind;
	s.thickness = thickness;
	b.With(s);
	return b;
}

WidgetBuilder Canvas(std::function<void(Painter &, const sdl3::FRect &)> draw) {
	WidgetBuilder b("canvas");
	b.With(CanvasPainter{std::move(draw)});
	return b;
}

// ============================================================================
// Commandes
// ============================================================================

WidgetBuilder Button(String text) {
	WidgetBuilder b("button");
	b.Pressable().Focusable();
	if (!text.IsEmpty())
		b.Child(Label(std::move(text)).Class("button-label").With(TextSlot{}).PointerThrough());
	return b;
}

WidgetBuilder IconButton(WidgetBuilder icon, String tooltip) {
	WidgetBuilder b("button");
	b.Class("icon-button").Pressable().Focusable();
	if (!tooltip.IsEmpty())
		b.Tooltip(std::move(tooltip));
	b.Child(icon.PointerThrough());
	return b;
}

WidgetBuilder Switch(bool on) {
	WidgetBuilder b("switch");
	b.With(Check{on, on ? 1.f : 0.f}).With(Checkable{}).Pressable().Focusable();
	b.Child(Box("switch-thumb").Class("switch-thumb").With(RangeView{RangePart::CheckThumb, Orientation::Horizontal}));
	return b;
}

WidgetBuilder Checkbox(String label, bool checked) {
	WidgetBuilder b("checkbox");
	b.With(Check{checked, checked ? 1.f : 0.f}).With(Checkable{}).Pressable().Focusable();
	b.Child(Box("checkbox-box")
				.Class("checkbox-box")
				.Children({ShapeBox(ShapeKind::Check, 2.f).Class("checkbox-mark").With(ShowWhen{VisibleIf::Checked})}));
	if (!label.IsEmpty())
		b.Child(Label(std::move(label)).Class("checkbox-label").With(TextSlot{}));
	return b;
}

WidgetBuilder Radio(String label, String group, bool checked) {
	WidgetBuilder b("radio");
	b.With(Check{checked, checked ? 1.f : 0.f}).With(Checkable{CheckMode::Radio, std::move(group)}).Pressable().Focusable();
	b.Child(Box("radio-box")
				.Class("radio-box")
				.Children({ShapeBox(ShapeKind::Dot).Class("radio-mark").With(ShowWhen{VisibleIf::Checked})}));
	if (!label.IsEmpty())
		b.Child(Label(std::move(label)).Class("radio-label").With(TextSlot{}));
	return b;
}

WidgetBuilder Slider(float min, float max, float value, float step, Orientation orientation) {
	WidgetBuilder b("slider");
	const bool vertical = orientation == Orientation::Vertical;
	b.Class(vertical ? "vertical" : "horizontal");
	b.With(Range{min, max, math::Clamp(value, min, max), step, 0.f});
	RangeInput input;
	input.mode = RangeMode::Track;
	input.axis = orientation;
	b.With(input).Focusable();
	b.Children({
		Box("slider-track").Class("slider-track").Children({
			Box("slider-fill").Class("slider-fill").With(RangeView{RangePart::Fill, orientation}),
		}),
		Box("slider-thumb").Class("slider-thumb").With(RangeView{RangePart::Thumb, orientation}),
	});
	return b;
}

WidgetBuilder Progress(float min, float max, float value) {
	WidgetBuilder b("progress");
	b.With(Range{min, max, math::Clamp(value, min, max), 0.f, 0.f});
	b.Child(Box("progress-fill").Class("progress-fill").With(RangeView{RangePart::Fill, Orientation::Horizontal}));
	return b;
}

WidgetBuilder Spinner(float thickness, float speed) {
	WidgetBuilder b("spinner");
	Shape s;
	s.kind = ShapeKind::Arc;
	s.start = 0.f;
	s.sweep = 270.f;
	s.thickness = thickness;
	b.With(s).With(Spin{speed, 0.f});
	return b;
}

WidgetBuilder Knob(float min, float max, float value, float step) {
	WidgetBuilder b("knob");
	b.With(Range{min, max, math::Clamp(value, min, max), step, 0.f});
	RangeInput input;
	input.mode = RangeMode::Relative;
	input.axis = Orientation::Vertical;
	input.speed = (max - min) / 150.f;
	b.With(input).Focusable();
	auto shape = [](ShapeKind kind, float thickness) {
		Shape sh;
		sh.kind = kind;
		sh.start = 135.f;
		sh.sweep = 270.f;
		sh.thickness = thickness;
		return sh;
	};
	// Piste fixe (dans le flux, pleine boîte), arc de valeur et aiguille liés au Range.
	b.Children({
		Box("knob-part").Class("knob-track").With(shape(ShapeKind::Arc, 4.f)).PointerThrough(),
		Box("knob-part").Class("knob-value").With(shape(ShapeKind::Arc, 4.f))
			.With(RangeView{RangePart::Angle, Orientation::Vertical}).PointerThrough(),
		Box("knob-part").Class("knob-needle").With(shape(ShapeKind::Line, 2.f))
			.With(RangeView{RangePart::Angle, Orientation::Vertical}).PointerThrough(),
	});
	return b;
}

WidgetBuilder ScrollBar(float content, float view, Orientation orientation) {
	WidgetBuilder b("scrollbar");
	b.Class(orientation == Orientation::Horizontal ? "horizontal" : "vertical");
	b.With(Range{0.f, std::max(content, view), 0.f, 0.f, view});
	RangeInput input;
	input.mode = RangeMode::Track;
	input.axis = orientation;
	input.invert = true;
	b.With(input);
	b.Child(Box("scrollbar-thumb").Class("scrollbar-thumb").With(RangeView{RangePart::Thumb, orientation, true}));
	return b;
}

// ============================================================================
// Saisie
// ============================================================================

WidgetBuilder Input(String placeholder, String value) {
	return EditorBox("input", std::move(placeholder), std::move(value), false);
}

WidgetBuilder Password(String placeholder) {
	WidgetBuilder b = EditorBox("input", std::move(placeholder), String(), false);
	b.Class("password");
	b.SetExpand([](WidgetSpec &s) {
		s.components.push_back([](ecs::ArchetypeRegistry &w, ecs::Entity e) {
			if (auto ed = w.GetComponent<TextEditor>(e); ed.IsSome()) {
				ed.Unwrap()->password = true;
				ed.Unwrap()->mode = IOMode::ReadWrite;
			}
		});
	});
	return b;
}

WidgetBuilder TextArea(String placeholder, String value) {
	return EditorBox("textarea", std::move(placeholder), std::move(value), true);
}

WidgetBuilder CodeEditor(String value, SyntaxHighlighter highlighter) {
	WidgetBuilder b = EditorBox("textarea", String(), std::move(value), true);
	b.Class("code");
	b.SetExpand([highlighter = std::move(highlighter)](WidgetSpec &s) {
		s.components.push_back([highlighter](ecs::ArchetypeRegistry &w, ecs::Entity e) {
			if (auto ed = w.GetComponent<TextEditor>(e); ed.IsSome()) {
				ed.Unwrap()->lineNumbers = true;
				ed.Unwrap()->monospace = true;
				ed.Unwrap()->highlightLine = true;
				ed.Unwrap()->highlighter = highlighter;
				ed.Unwrap()->cursor = ed.Unwrap()->anchor = 0;
			}
		});
	});
	return b;
}

WidgetBuilder LogView(SyntaxHighlighter highlighter) {
	WidgetBuilder b = EditorBox("textarea", String(), String(), true);
	b.Class("log");
	b.SetExpand([highlighter = std::move(highlighter)](WidgetSpec &s) {
		s.components.push_back([highlighter](ecs::ArchetypeRegistry &w, ecs::Entity e) {
			if (auto ed = w.GetComponent<TextEditor>(e); ed.IsSome()) {
				ed.Unwrap()->mode = IOMode::ReadAndCopy;
				ed.Unwrap()->followTail = true;
				ed.Unwrap()->monospace = true;
				ed.Unwrap()->highlighter = highlighter;
			}
		});
	});
	return b;
}

WidgetBuilder DragValue(float min, float max, float value, float step, float speed, int decimals) {
	WidgetBuilder b("drag-value");
	value = math::Clamp(value, min, max);
	b.With(Range{min, max, value, step, 0.f});
	RangeInput input;
	input.mode = RangeMode::Relative;
	input.axis = Orientation::Horizontal;
	input.speed = speed > 0.f ? speed : std::max(step, (max - min) / 400.f);
	input.wheel = false;
	b.With(input).With(RangeLabel{decimals, String(), String(), nullptr}).With(TextContent{String::From(value, decimals)});
	b.With(EditOnDoubleClick{}).Focusable();
	return b;
}

WidgetBuilder NumberInput(float min, float max, float value, float step, int decimals) {
	WidgetBuilder b("number-input");
	value = math::Clamp(value, min, max);
	b.With(Range{min, max, value, step, 0.f});
	RangeInput wheelOnly;
	wheelOnly.mode = RangeMode::Relative;
	wheelOnly.speed = 0.f;
	b.With(wheelOnly);
	EditOnDoubleClick edit;
	edit.singleClick = true;
	b.Children({
		Button("−").Class("number-step").Class("minus").With(RangeStep{-1.f}),
		Box("number-value").Class("number-value").With(RangeLabel{decimals, String(), String(), nullptr})
			.With(TextContent{String::From(value, decimals)}).With(edit).Focusable(),
		Button("+").Class("number-step").Class("plus").With(RangeStep{1.f}),
	});
	return b;
}

// ============================================================================
// Listes et sélection
// ============================================================================

WidgetBuilder Selectable(String text, int index) {
	WidgetBuilder b("selectable");
	b.Pressable().With(SelectItem{index}).With(TextContent{std::move(text)});
	return b;
}

WidgetBuilder SelectionList(bool multiple) {
	WidgetBuilder b("selection-list");
	Selection sel;
	sel.multiple = multiple;
	b.With(sel).Focusable();
	return b;
}

namespace {

WidgetBuilder ComboItem(const String &text, int index) {
	WidgetBuilder b("combo-item");
	b.Pressable().With(SelectItem{index}).With(TextContent{text});
	return b;
}

WidgetBuilder ComboShell(std::vector<String> flat, int selected, WidgetBuilder list) {
	WidgetBuilder b("combo");
	Selection sel;
	if (selected >= 0 && size_t(selected) < flat.size())
		sel.SelectOnly(selected);
	b.With(sel).Pressable().Focusable().With(PopupTrigger{PopupOpen::Click, Placement::Below});
	const String initial = selected >= 0 && size_t(selected) < flat.size() ? flat[size_t(selected)] : String();
	PopupState popup;
	popup.closeOnItemClick = true;
	popup.matchAnchorWidth = true;
	b.Children({
		Label(initial).Class("combo-text").With(SelectionText{flat, String()}).With(TextSlot{}).PointerThrough(),
		ShapeBox(ShapeKind::ChevronDown, 1.5f).Class("combo-arrow").PointerThrough(),
		Box("popup").Class("combo-popup").With(popup).Children({list}),
	});
	return b;
}

} // namespace

WidgetBuilder Combo(std::vector<String> items, int selected) {
	WidgetBuilder list("column");
	list.Class("combo-list").With(ItemsHost{[](const String &t, int i) { return AnyWidget(ComboItem(t, i)); }});
	for (size_t i = 0; i < items.size(); ++i)
		list.Child(ComboItem(items[i], int(i)));
	return ComboShell(std::move(items), selected, std::move(list));
}

WidgetBuilder Combo(std::vector<ComboCategory> categories, int selected) {
	std::vector<String> flat;
	WidgetBuilder list("column");
	list.Class("combo-list");
	for (const ComboCategory &cat : categories) {
		if (cat.title.IsEmpty()) {
			for (const String &it : cat.items) {
				list.Child(ComboItem(it, int(flat.size())));
				flat.push_back(it);
			}
			continue;
		}
		WidgetBuilder sub("column");
		sub.Class("combo-list");
		for (const String &it : cat.items) {
			sub.Child(ComboItem(it, int(flat.size())));
			flat.push_back(it);
		}
		PopupState popup;
		popup.placement = Placement::Right;
		popup.closeOnItemClick = true;
		WidgetBuilder group("combo-item");
		group.Class("combo-group").Pressable().With(PopupTrigger{PopupOpen::Hover, Placement::Right});
		group.Children({
			Label(cat.title).Grow().PointerThrough(),
			ShapeBox(ShapeKind::ChevronRight, 1.5f).Class("combo-group-arrow").PointerThrough(),
			Box("popup").Class("combo-popup").With(popup).Children({sub}),
		});
		list.Child(group);
	}
	return ComboShell(std::move(flat), selected, std::move(list));
}

WidgetBuilder ListBox(std::vector<String> items, int selected, bool multiple) {
	WidgetBuilder b("listbox");
	Selection sel;
	sel.multiple = multiple;
	if (selected >= 0)
		sel.SelectOnly(selected);
	b.With(sel).Focusable().With(ItemsHost{[](const String &t, int i) {
		return AnyWidget(Selectable(t, i).Type("list-item"));
	}});
	for (size_t i = 0; i < items.size(); ++i)
		b.Child(Selectable(items[i], int(i)).Type("list-item"));
	return b;
}

WidgetBuilder TreeNode(String title, int index, bool expanded) {
	WidgetBuilder b("tree-node");
	b.With(Check{expanded, expanded ? 1.f : 0.f});
	b.SetExpand([title = std::move(title), index](WidgetSpec &s) {
		const bool leaf = s.children.empty();
		std::vector<AnyWidget> user = std::move(s.children);
		s.children.clear();
		WidgetBuilder header("tree-header");
		header.Pressable().With(SelectItem{index});
		WidgetBuilder toggle("tree-toggle");
		toggle.With(ToggleParent{});
		if (!leaf)
			toggle.Children({
				ShapeBox(ShapeKind::ChevronRight, 1.5f).Class("tree-arrow").With(ShowWhen{VisibleIf::Unchecked}).PointerThrough(),
				ShapeBox(ShapeKind::ChevronDown, 1.5f).Class("tree-arrow").With(ShowWhen{VisibleIf::Checked}).PointerThrough(),
			});
		else
			toggle.Class("leaf");
		header.Children({toggle, Label(title).Class("tree-title").With(TextSlot{}).PointerThrough()});
		s.children.push_back(header);
		if (!leaf) {
			WidgetBuilder body("tree-children");
			body.With(ShowWhen{VisibleIf::Checked}).With(ContentSlot{}).Children(std::move(user));
			s.children.push_back(body);
		} else {
			s.element.classes.push_back("leaf");
		}
	});
	return b;
}

WidgetBuilder Expander(String title, bool expanded) {
	WidgetBuilder b("expander");
	b.With(Check{expanded, expanded ? 1.f : 0.f});
	b.SetExpand([title = std::move(title)](WidgetSpec &s) {
		std::vector<AnyWidget> user = std::move(s.children);
		s.children.clear();
		WidgetBuilder header("expander-header");
		header.Pressable().With(ToggleParent{}).Focusable();
		header.Children({
			ShapeBox(ShapeKind::ChevronRight, 1.5f).Class("expander-arrow").With(ShowWhen{VisibleIf::Unchecked}).PointerThrough(),
			ShapeBox(ShapeKind::ChevronDown, 1.5f).Class("expander-arrow").With(ShowWhen{VisibleIf::Checked}).PointerThrough(),
			Label(title).Class("expander-title").With(TextSlot{}).PointerThrough(),
		});
		WidgetBuilder body("expander-body");
		body.With(ShowWhen{VisibleIf::Checked}).With(ContentSlot{}).Children(std::move(user));
		s.children.push_back(header);
		s.children.push_back(body);
	});
	return b;
}

WidgetBuilder TabView(std::vector<String> tabs, int active) {
	WidgetBuilder b("tabview");
	Selection sel;
	sel.SelectOnly(active);
	b.With(sel);
	b.SetExpand([tabs = std::move(tabs)](WidgetSpec &s) {
		std::vector<AnyWidget> pages = std::move(s.children);
		s.children.clear();
		auto tab = [](const String &t, int i) {
			WidgetBuilder button("tab");
			button.Pressable().Focusable().With(SelectItem{i}).With(TextContent{t});
			return AnyWidget(button);
		};
		WidgetBuilder bar("tab-bar");
		bar.With(ItemsHost{tab});
		for (size_t i = 0; i < tabs.size(); ++i)
			bar.Child(tab(tabs[i], int(i)));
		s.children.push_back(bar);
		WidgetBuilder body("tab-body");
		for (size_t i = 0; i < pages.size(); ++i)
			body.Child(Box("tab-page").With(ShowWhen{VisibleIf::Selected, int(i)}).Children({pages[i]}));
		s.children.push_back(body);
	});
	return b;
}

// ============================================================================
// Popups, menus, dialogues
// ============================================================================

WidgetBuilder Popup(Placement placement) {
	WidgetBuilder b("popup");
	PopupState p;
	p.placement = placement;
	b.With(p);
	return b;
}

WidgetBuilder Modal(AnyWidget content) {
	WidgetBuilder b("modal");
	PopupState p;
	p.modal = true;
	p.placement = Placement::Center;
	p.closeOnOutside = false;
	b.With(p).Children({std::move(content)});
	return b;
}

WidgetBuilder MenuBar() {
	WidgetBuilder b("menubar");
	b.With(MenuStrip{});
	return b;
}

WidgetBuilder Menu(String title) {
	WidgetBuilder b("menu");
	b.Pressable().Focusable().With(PopupTrigger{PopupOpen::Click, Placement::Below});
	b.SetExpand([title = std::move(title)](WidgetSpec &s) {
		std::vector<AnyWidget> items = std::move(s.children);
		s.children.clear();
		PopupState popup;
		popup.closeOnItemClick = true;
		s.children.push_back(Label(title).Class("menu-title").With(TextSlot{}).PointerThrough());
		s.children.push_back(Box("popup").Class("menu-popup").With(popup).With(ContentSlot{}).Children(std::move(items)));
	});
	return b;
}

WidgetBuilder MenuItem(String text, String shortcut) {
	WidgetBuilder b("menu-item");
	b.Pressable();
	b.Child(Label(std::move(text)).Class("menu-text").With(TextSlot{}).PointerThrough());
	if (!shortcut.IsEmpty())
		b.Child(Label(std::move(shortcut)).Class("menu-shortcut").PointerThrough());
	return b;
}

WidgetBuilder SubMenu(String text) {
	WidgetBuilder b("menu-item");
	b.Class("submenu").Pressable().With(PopupTrigger{PopupOpen::Hover, Placement::Right});
	b.SetExpand([text = std::move(text)](WidgetSpec &s) {
		std::vector<AnyWidget> items = std::move(s.children);
		s.children.clear();
		PopupState popup;
		popup.closeOnItemClick = true;
		popup.placement = Placement::Right;
		s.children.push_back(Label(text).Class("menu-text").With(TextSlot{}).PointerThrough());
		s.children.push_back(ShapeBox(ShapeKind::ChevronRight, 1.5f).Class("menu-arrow").PointerThrough());
		s.children.push_back(Box("popup").Class("menu-popup").With(popup).With(ContentSlot{}).Children(std::move(items)));
	});
	return b;
}

WidgetBuilder MenuSeparator() {
	WidgetBuilder b("menu-separator");
	b.PointerThrough();
	return b;
}

WidgetBuilder ContextMenu() {
	WidgetBuilder b("popup");
	PopupState p;
	p.placement = Placement::Point;
	p.closeOnItemClick = true;
	b.Class("menu-popup").Class("context-menu").With(p);
	return b;
}

// ============================================================================
// Widgets complexes
// ============================================================================

WidgetBuilder Table(std::vector<TableColumn> columns, std::vector<std::vector<String>> rows, bool multiple,
					bool reorderable) {
	WidgetBuilder b("table");
	TableModel model;
	model.columns = columns;
	Selection sel;
	sel.multiple = multiple;
	b.With(model).With(sel).Focusable();
	WidgetBuilder header("table-header");
	for (size_t c = 0; c < columns.size(); ++c) {
		WidgetBuilder cell("table-header-cell");
		cell.With(TableCell{int(c)});
		if (columns[c].sortable)
			cell.With(TableHeader{int(c)}).Pressable();
		cell.Children({
			Label(columns[c].title).Class("table-header-title").Grow().PointerThrough(),
			Label(String()).Class("sort-indicator").With(SortIndicator{int(c)}).PointerThrough(),
			Box("table-resizer").Class("table-resizer").With(ColumnResizer{int(c)}),
		});
		header.Child(cell);
	}
	WidgetBuilder body("table-body");
	for (size_t r = 0; r < rows.size(); ++r) {
		WidgetBuilder row("table-row");
		row.Pressable().With(SelectItem{int(r)});
		if (reorderable)
			row.Reorderable();
		for (size_t c = 0; c < columns.size(); ++c)
			row.Child(Box("table-cell").With(TableCell{int(c)}).Children({
				Label(c < rows[r].size() ? rows[r][c] : String()).Class("table-cell-text").Ellipsis().PointerThrough(),
			}));
		body.Child(row);
	}
	b.Children({header, body});
	return b;
}

WidgetBuilder Splitter(AnyWidget before, AnyWidget after, Orientation orientation, float initialBefore, float minBefore,
					   float minAfter) {
	const bool horizontal = orientation == Orientation::Horizontal;
	WidgetBuilder b("splitter");
	b.Class(horizontal ? "horizontal" : "vertical");
	WidgetBuilder first("splitter-pane");
	if (horizontal)
		first.Width(initialBefore);
	else
		first.Height(initialBefore);
	first.Shrink(0.f).Children({std::move(before)});
	WidgetBuilder handle("splitter-handle");
	handle.With(ResizeHandle{orientation, minBefore, minAfter});
	WidgetBuilder second("splitter-pane");
	second.Grow().Children({std::move(after)});
	b.Children({first, handle, second});
	return b;
}

WidgetBuilder ColorSwatch(Color color) {
	WidgetBuilder b("color-swatch");
	b.With(ColorModel{color}).Pressable().Focusable();
	b.Child(Box("color-swatch-fill").Class("color-swatch-fill").Background(color).PointerThrough());
	// Synchronise la couleur affichée à chaque SetColorValue.
	b.OnBuilt([](Widget w) {
		w.AddBinding({Trigger::Of(InputSource::Custom)},
					 [](const WidgetEvent &e) {
						 if (e.text != "color-sync")
							 return;
						 Widget sw = e.Current();
						 for (Widget c : sw.Children())
							 if (c.HasClass("color-swatch-fill"))
								 c.Background(sw.ColorValue());
					 },
					 true);
	});
	return b;
}

namespace {

/// État partagé du sélecteur de couleur entre ses parties.
struct PickerParts {
	Widget root, sv, hue, alpha, hex, preview;
	float h = 0.f, s = 0.f, v = 0.f, a = 1.f;
	bool syncing = false;
	std::function<void(Color)> onChange;
};

void SyncPicker(PickerParts &p, bool notify) {
	if (p.syncing)
		return;
	p.syncing = true;
	const Color c = HsvToColor(p.h, p.s, p.v, p.a);
	p.sv.BackgroundImage(Paint::Corners4(Rgba(255, 255, 255), HsvToColor(p.h, 1.f, 1.f), Rgba(0, 0, 0), Rgba(0, 0, 0)));
	if (auto r = p.sv.Get<Range2D>(); r.IsSome())
		r.Unwrap()->value = {p.s, 1.f - p.v};
	p.hue.SetValue(p.h);
	p.alpha.SetValue(p.a);
	if (auto track = p.alpha.Find(".slider-track"); track.IsSome())
		track.Unwrap().BackgroundImage(Paint::LinearGradient(WithAlpha(c, 0.f), WithAlpha(c, 1.f), 90.f));
	if (FocusedWidget(*p.hex.World()) != p.hex.Entity())
		p.hex.SetText(ColorToHex(c));
	p.preview.SetColorValue(c, false);
	if (auto m = p.root.Get<ColorModel>(); m.IsSome())
		m.Unwrap()->value = c;
	MarkLayoutDirty(*p.root.World());
	p.syncing = false;
	if (notify) {
		WidgetEvent ev;
		ev.value = c.a;
		p.root.Emit(Trigger::Of(InputSource::Change), ev, false);
		if (p.onChange)
			p.onChange(c);
	}
}

} // namespace

WidgetBuilder ColorPicker(Color initial, std::function<void(Color)> onChange) {
	auto parts = std::make_shared<PickerParts>();
	parts->onChange = std::move(onChange);
	const Hsv hsv = ColorToHsv(initial);
	parts->h = hsv.h;
	parts->s = hsv.s;
	parts->v = hsv.v;
	parts->a = initial.a;

	WidgetBuilder b("color-picker");
	b.With(ColorModel{initial}).Ref(parts->root);
	RangeInput pad;
	pad.mode = RangeMode::Track;
	Range2D svValue;
	svValue.value = {hsv.s, 1.f - hsv.v};
	WidgetBuilder sv("color-sv");
	sv.With(svValue).With(pad).Ref(parts->sv);
	sv.Child(Box("color-sv-thumb").Class("color-sv-thumb").With(RangeView{RangePart::Thumb2D, Orientation::Horizontal}).PointerThrough());
	sv.OnChange([parts](float) {
		if (auto r = parts->sv.Get<Range2D>(); r.IsSome()) {
			parts->s = r.Unwrap()->value.x;
			parts->v = 1.f - r.Unwrap()->value.y;
		}
		SyncPicker(*parts, true);
	});
	WidgetBuilder hue = Slider(0.f, 360.f, hsv.h);
	hue.Class("hue").Ref(parts->hue).OnChange([parts](float h) {
		if (parts->syncing)
			return;
		parts->h = h;
		SyncPicker(*parts, true);
	});
	WidgetBuilder alpha = Slider(0.f, 1.f, initial.a);
	alpha.Class("alpha").Ref(parts->alpha).OnChange([parts](float a) {
		if (parts->syncing)
			return;
		parts->a = a;
		SyncPicker(*parts, true);
	});
	WidgetBuilder hex = Input(String(), ColorToHex(initial));
	hex.Class("color-hex").Ref(parts->hex).OnSubmit([parts](const String &text) {
		if (auto c = ParseHexColor(text.Trim().View()); c.IsSome()) {
			const Hsv x = ColorToHsv(c.Unwrap());
			parts->h = x.h;
			parts->s = x.s;
			parts->v = x.v;
			parts->a = c.Unwrap().a;
			SyncPicker(*parts, true);
		}
	});
	WidgetBuilder preview = ColorSwatch(initial);
	preview.Class("color-preview").Ref(parts->preview);
	b.Children({sv, hue, alpha, Row().Class("color-picker-row").Children({preview, hex.Grow()})});
	b.OnBuilt([parts](Widget) { SyncPicker(*parts, false); });
	// Couleur imposée par le code (Widget::SetColorValue) : parties resynchronisées.
	b.OnBuilt([parts](Widget w) {
		w.AddBinding({Trigger::Of(InputSource::Custom)},
					 [parts](const WidgetEvent &e) {
						 if (e.text != "color-sync" || parts->syncing)
							 return;
						 const Color c = parts->root.ColorValue();
						 const Hsv x = ColorToHsv(c);
						 parts->h = x.h;
						 parts->s = x.s;
						 parts->v = x.v;
						 parts->a = c.a;
						 SyncPicker(*parts, false);
					 },
					 true);
	});
	return b;
}

const char *MonthNameFr(int month) noexcept {
	constexpr const char *K_NAMES[12] = {"Janvier", "Février", "Mars",		"Avril",   "Mai",	   "Juin",
										 "Juillet", "Août",	   "Septembre", "Octobre", "Novembre", "Décembre"};
	if (month < 1 || month > 12)
		return "";
	return K_NAMES[month - 1];
}

namespace {

struct CalendarParts {
	int year = 2026, month = 1, day = -1;
	Widget title;
	std::vector<Widget> days; ///< 42 cases
	std::function<void(int, int, int)> onSelect;
};

void RefreshCalendar(CalendarParts &c) {
	c.title.SetText(String(MonthNameFr(c.month)) + " " + String::From(c.year));
	const int first = FirstWeekdayOfMonth(c.year, c.month);
	const int count = DaysInMonth(c.year, c.month);
	for (size_t i = 0; i < c.days.size(); ++i) {
		const int d = int(i) - first + 1;
		Widget cell = c.days[i];
		if (d < 1 || d > count) {
			cell.SetText(String());
			cell.Disable();
			cell.SetChecked(false);
			cell.AddClass("outside");
		} else {
			cell.SetText(String::From(d));
			cell.Enable();
			cell.RemoveClass("outside");
			cell.SetChecked(d == c.day);
		}
	}
}

} // namespace

WidgetBuilder Calendar(int year, int month, int day, std::function<void(int, int, int)> onSelect) {
	auto parts = std::make_shared<CalendarParts>();
	parts->year = year;
	parts->month = math::Clamp(month, 1, 12);
	parts->day = day;
	parts->onSelect = std::move(onSelect);
	parts->days.resize(42);
	WidgetBuilder b("calendar");
	auto shift = [parts](int delta) {
		parts->month += delta;
		if (parts->month < 1) {
			parts->month = 12;
			--parts->year;
		} else if (parts->month > 12) {
			parts->month = 1;
			++parts->year;
		}
		parts->day = -1;
		RefreshCalendar(*parts);
	};
	WidgetBuilder header = Row().Class("calendar-header").Children({
		Button("‹").Class("calendar-nav").OnClick([shift] { shift(-1); }),
		Label(String()).Class("calendar-title").Grow().Ref(parts->title),
		Button("›").Class("calendar-nav").OnClick([shift] { shift(1); }),
	});
	WidgetBuilder names = Grid(7).Class("calendar-weekdays");
	for (const char *n : {"Lu", "Ma", "Me", "Je", "Ve", "Sa", "Di"})
		names.Child(Label(n).Class("calendar-weekday"));
	WidgetBuilder grid = Grid(7).Class("calendar-days");
	for (size_t i = 0; i < 42; ++i) {
		WidgetBuilder cell("calendar-day");
		cell.Pressable().With(Check{}).With(TextContent{}).Ref(parts->days[i]);
		cell.OnClick([parts, i] {
			const int d = int(i) - FirstWeekdayOfMonth(parts->year, parts->month) + 1;
			if (d < 1 || d > DaysInMonth(parts->year, parts->month))
				return;
			parts->day = d;
			RefreshCalendar(*parts);
			if (parts->onSelect)
				parts->onSelect(parts->year, parts->month, d);
		});
		grid.Child(cell);
	}
	b.Children({header, names, grid});
	b.OnBuilt([parts](Widget) { RefreshCalendar(*parts); });
	return b;
}

WidgetBuilder DatePicker(int year, int month, int day, std::function<void(int, int, int)> onSelect) {
	auto label = [](int y, int m, int d) {
		return d > 0 ? String::Format("%02d/%02d/%04d", d, m, y) : String("--/--/----");
	};
	auto button = std::make_shared<Widget>();
	WidgetBuilder b("date-picker");
	b.Pressable().Focusable().With(PopupTrigger{PopupOpen::Click, Placement::Below}).Ref(*button);
	b.Children({
		Label(label(year, month, day)).Class("date-text").With(TextSlot{}).PointerThrough(),
		Popup(Placement::Below).Class("date-popup").Children({
			Calendar(year, month, day, [button, label, onSelect](int y, int m, int d) {
				button->SetText(label(y, m, d));
				button->Close();
				if (onSelect)
					onSelect(y, m, d);
			}),
		}),
	});
	return b;
}

} // namespace ui
