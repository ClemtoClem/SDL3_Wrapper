// Définitions de ui/factory.hpp
#include "ui/factory.hpp"

namespace ui {

WidgetColors MakeWidgetColors(sdl3::FColor bgNormal, sdl3::FColor bgHovered,
		sdl3::FColor bgPressed, sdl3::FColor bgChecked,
		sdl3::FColor borderNormal, sdl3::FColor borderFocus,
		sdl3::FColor textNormal) {
	WidgetColors c;
	c.bgNormal = bgNormal;
	c.bgHovered = bgHovered;
	c.bgPressed = bgPressed;
	c.bgChecked = bgChecked;
	c.borderNormal = borderNormal;
	c.borderFocus = borderFocus;
	c.textNormal = textNormal;
	return c;
}

// ── UiTheme ──────────────────────────────────────────────────────────────────

UiTheme UiTheme::Dark() {
	UiTheme t;
	sdl3::FColor hover{55 / 255.f, 60 / 255.f, 84 / 255.f, 1.f};
	sdl3::FColor focus{120 / 255.f, 170 / 255.f, 250 / 255.f, 1.f};
	t.button = MakeWidgetColors(sdl3::FColor{40 / 255.f, 44 / 255.f, 64 / 255.f, 1.f},
								 sdl3::FColor{70 / 255.f, 78 / 255.f, 110 / 255.f, 1.f},
								 sdl3::FColor{24 / 255.f, 26 / 255.f, 40 / 255.f, 1.f}, t.fill,
								 sdl3::FColor{0.f, 0.f, 0.f, 120 / 255.f}, sdl3::FColor{1.f, 1.f, 1.f, 60 / 255.f},
								 t.text);
	t.toggle = MakeWidgetColors(t.track, hover, t.track, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.checkbox = t.toggle;
	t.slider = MakeWidgetColors(t.track, t.thumb, t.fill, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.input = MakeWidgetColors(sdl3::FColor{28 / 255.f, 30 / 255.f, 44 / 255.f, 1.f},
								sdl3::FColor{120 / 255.f, 124 / 255.f, 150 / 255.f, 1.f}, focus,
								sdl3::FColor::WHITE(), t.border, focus, t.text);
	t.dragValue = MakeWidgetColors(sdl3::FColor{28 / 255.f, 30 / 255.f, 44 / 255.f, 1.f}, hover, focus, t.fill,
									t.border, focus, t.text);
	t.radio = t.toggle;
	t.scrollbar = MakeWidgetColors(t.track, t.thumb, t.fill, t.thumb, sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.knob = t.toggle;
	t.combo = MakeWidgetColors(sdl3::FColor{28 / 255.f, 30 / 255.f, 44 / 255.f, 1.f}, hover, t.thumb,
								sdl3::FColor::UI_ACCENT_BLUE_DEEP(), t.border, focus, t.text);
	t.listbox = MakeWidgetColors(sdl3::FColor{24 / 255.f, 26 / 255.f, 38 / 255.f, 1.f}, hover, t.thumb,
								  sdl3::FColor::UI_ACCENT_BLUE_DEEP(), t.border, focus, t.text);
	t.expander = t.toggle;
	t.tabs = MakeWidgetColors(sdl3::FColor{24 / 255.f, 26 / 255.f, 36 / 255.f, 1.f}, hover, t.track,
							   sdl3::FColor{45 / 255.f, 52 / 255.f, 76 / 255.f, 1.f}, sdl3::FColor{}, t.accent,
							   t.text);
	t.spinner = t.accent;
	t.badgeBg = sdl3::FColor{200 / 255.f, 60 / 255.f, 50 / 255.f, 1.f};
	t.badgeText = sdl3::FColor::WHITE();
	return t;
}

UiTheme UiTheme::Light() {
	UiTheme t;
	t.panelBg = sdl3::FColor{240 / 255.f, 242 / 255.f, 245 / 255.f, 250 / 255.f};
	t.text = sdl3::FColor{20 / 255.f, 24 / 255.f, 32 / 255.f, 1.f};
	t.muted = sdl3::FColor{100 / 255.f, 105 / 255.f, 120 / 255.f, 1.f};
	t.track = sdl3::FColor{210 / 255.f, 214 / 255.f, 220 / 255.f, 1.f};
	t.fill = sdl3::FColor{40 / 255.f, 120 / 255.f, 210 / 255.f, 1.f};
	t.thumb = sdl3::FColor{60 / 255.f, 140 / 255.f, 230 / 255.f, 1.f};
	t.border = sdl3::FColor{180 / 255.f, 190 / 255.f, 205 / 255.f, 1.f};
	t.fieldBg = sdl3::FColor::WHITE();
	t.accent = sdl3::FColor{40 / 255.f, 120 / 255.f, 210 / 255.f, 1.f};
	sdl3::FColor hover{200 / 255.f, 205 / 255.f, 215 / 255.f, 1.f};
	sdl3::FColor focus = t.accent;
	t.button = MakeWidgetColors(sdl3::FColor{225 / 255.f, 230 / 255.f, 235 / 255.f, 1.f}, hover,
								 sdl3::FColor{180 / 255.f, 185 / 255.f, 195 / 255.f, 1.f}, t.fill,
								 sdl3::FColor{160 / 255.f, 168 / 255.f, 182 / 255.f, 1.f}, focus, t.text);
	t.toggle = MakeWidgetColors(t.track, hover, t.track, t.fill, sdl3::FColor{}, sdl3::FColor{},
								 sdl3::FColor::WHITE());
	t.checkbox = t.toggle;
	t.slider = MakeWidgetColors(t.track, t.thumb, t.fill, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.input = MakeWidgetColors(t.fieldBg, t.muted, focus, sdl3::FColor{20 / 255.f, 24 / 255.f, 32 / 255.f, 1.f},
								t.border, focus, t.text);
	t.dragValue = MakeWidgetColors(t.fieldBg, hover, focus, t.fill, t.border, focus, t.text);
	t.radio = t.toggle;
	t.scrollbar = MakeWidgetColors(t.track, t.thumb, t.fill, t.thumb, sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.knob = t.toggle;
	t.combo = MakeWidgetColors(t.fieldBg, hover, t.thumb, sdl3::FColor{150 / 255.f, 190 / 255.f, 240 / 255.f, 1.f},
								t.border, focus, t.text);
	t.listbox = t.combo;
	t.expander = t.toggle;
	t.tabs = MakeWidgetColors(sdl3::FColor{225 / 255.f, 228 / 255.f, 234 / 255.f, 1.f}, hover, t.track,
							   sdl3::FColor{205 / 255.f, 215 / 255.f, 232 / 255.f, 1.f}, sdl3::FColor{}, t.accent,
							   t.text);
	t.spinner = t.accent;
	t.badgeBg = sdl3::FColor{200 / 255.f, 60 / 255.f, 50 / 255.f, 1.f};
	t.badgeText = sdl3::FColor::WHITE();
	return t;
}

UiTheme UiTheme::Studio() {
	UiTheme t;
	auto grey = [](int v, int a = 255) { return sdl3::FColor{v / 255.f, v / 255.f, v / 255.f, a / 255.f}; };
	t.fontSize = 14.f;
	t.panelBg = grey(40);
	t.text = grey(214);
	t.muted = grey(142);
	t.track = grey(58);
	t.fill = sdl3::FColor{66 / 255.f, 118 / 255.f, 196 / 255.f, 1.f};
	t.thumb = sdl3::FColor{98 / 255.f, 146 / 255.f, 214 / 255.f, 1.f};
	t.border = grey(24);
	t.fieldBg = grey(28);
	t.accent = sdl3::FColor{84 / 255.f, 140 / 255.f, 222 / 255.f, 1.f};
	const sdl3::FColor hover = grey(64);
	const sdl3::FColor focus = t.accent;
	const sdl3::FColor selection{46 / 255.f, 88 / 255.f, 150 / 255.f, 1.f};
	t.button = MakeWidgetColors(grey(52), hover, grey(36), t.fill, grey(22), focus, t.text);
	t.toggle = MakeWidgetColors(grey(30), hover, grey(30), t.fill, grey(20), focus, t.text);
	t.checkbox = t.toggle;
	t.slider = MakeWidgetColors(t.track, t.thumb, t.fill, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.input = MakeWidgetColors(t.fieldBg, t.muted, focus, sdl3::FColor::WHITE(), grey(20), focus, t.text);
	t.dragValue = MakeWidgetColors(t.fieldBg, grey(36), focus, t.fill, grey(20), focus, t.text);
	t.radio = t.toggle;
	t.scrollbar = MakeWidgetColors(grey(34), grey(88), grey(110), grey(88), sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.knob = t.toggle;
	t.combo = MakeWidgetColors(t.fieldBg, grey(36), t.thumb, selection, grey(20), focus, t.text);
	t.listbox = MakeWidgetColors(sdl3::FColor{}, grey(52), t.thumb, selection, grey(20), focus, t.text);
	t.expander = MakeWidgetColors(grey(46), grey(54), grey(46), grey(46), sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.tabs = MakeWidgetColors(grey(32), hover, grey(32), grey(40), sdl3::FColor{}, t.accent, t.text);
	t.spinner = t.accent;
	t.badgeBg = sdl3::FColor{196 / 255.f, 62 / 255.f, 52 / 255.f, 1.f};
	t.badgeText = sdl3::FColor::WHITE();
	return t;
}

UiTheme UiTheme::Aero() {
	UiTheme t = Dark();
	t.glassDefault = true;
	t.panelBg = sdl3::FColor{70 / 255.f, 120 / 255.f, 190 / 255.f, 130 / 255.f};
	t.text = sdl3::FColor::WHITE();
	t.muted = sdl3::FColor{210 / 255.f, 220 / 255.f, 235 / 255.f, 255 / 255.f};
	t.track = sdl3::FColor{40 / 255.f, 60 / 255.f, 90 / 255.f, 140 / 255.f};
	t.fill = sdl3::FColor{60 / 255.f, 140 / 255.f, 230 / 255.f, 255 / 255.f};
	t.thumb = sdl3::FColor{120 / 255.f, 180 / 255.f, 240 / 255.f, 255 / 255.f};
	t.border = sdl3::FColor::UI_WHITE_SOFT();
	t.fieldBg = sdl3::FColor{20 / 255.f, 30 / 255.f, 50 / 255.f, 160 / 255.f};
	t.accent = sdl3::FColor{100 / 255.f, 180 / 255.f, 250 / 255.f, 255 / 255.f};
	sdl3::FColor hover{90 / 255.f, 150 / 255.f, 220 / 255.f, 160 / 255.f};
	sdl3::FColor focus{140 / 255.f, 200 / 255.f, 250 / 255.f, 1.f};
	t.button = MakeWidgetColors(sdl3::FColor{60 / 255.f, 120 / 255.f, 200 / 255.f, 150 / 255.f}, hover,
								 sdl3::FColor{40 / 255.f, 90 / 255.f, 160 / 255.f, 180 / 255.f}, t.fill,
								 sdl3::FColor{1.f, 1.f, 1.f, 70 / 255.f}, focus, t.text);
	t.toggle = MakeWidgetColors(t.track, hover, t.track, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.checkbox = t.toggle;
	t.slider = MakeWidgetColors(t.track, t.thumb, t.fill, t.fill, sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.input = MakeWidgetColors(sdl3::FColor{20 / 255.f, 30 / 255.f, 50 / 255.f, 170 / 255.f},
								sdl3::FColor{160 / 255.f, 190 / 255.f, 220 / 255.f, 1.f}, focus,
								sdl3::FColor::WHITE(), t.border, focus, t.text);
	t.dragValue = t.input;
	t.radio = t.toggle;
	t.scrollbar = MakeWidgetColors(t.track, t.thumb, t.fill, t.thumb, sdl3::FColor{}, sdl3::FColor{}, t.text);
	t.knob = t.toggle;
	t.combo = MakeWidgetColors(sdl3::FColor{30 / 255.f, 45 / 255.f, 70 / 255.f, 180 / 255.f}, hover, t.thumb,
								sdl3::FColor{60 / 255.f, 120 / 255.f, 200 / 255.f, 1.f}, t.border, focus, t.text);
	t.listbox = t.combo;
	t.expander = t.toggle;
	t.tabs = MakeWidgetColors(sdl3::FColor{30 / 255.f, 45 / 255.f, 70 / 255.f, 160 / 255.f}, hover, t.track,
							   sdl3::FColor{60 / 255.f, 110 / 255.f, 190 / 255.f, 1.f}, sdl3::FColor{}, t.accent,
							   t.text);
	t.spinner = t.accent;
	t.badgeBg = sdl3::FColor{220 / 255.f, 80 / 255.f, 70 / 255.f, 1.f};
	t.badgeText = sdl3::FColor::WHITE();
	return t;
}

UiStyle StyleFromColors(const WidgetColors &c) {
	UiStyle s;
	s.SetBg(c.bgNormal)
		.SetBgHovered(c.bgHovered)
		.SetBgPressed(c.bgPressed)
		.SetBgChecked(c.bgChecked)
		.SetTextColor(c.textNormal)
		.SetBorderColor(c.borderNormal)
		.SetBorderFocus(c.borderFocus);
	return s;
}

// ── WidgetBuilder ────────────────────────────────────────────────────────────

WidgetBuilder & WidgetBuilder::W(Dimension v) {
	Item().width = v;
	return *this;
}

WidgetBuilder & WidgetBuilder::H(Dimension v) {
	Item().height = v;
	return *this;
}

WidgetBuilder & WidgetBuilder::Offset(float x, float y) {
	rect.offset = {x, y};
	return *this;
}

WidgetBuilder & WidgetBuilder::Clip() {
	rect.clipContent = true;
	return *this;
}

WidgetBuilder & WidgetBuilder::Margin(math::Sides m) {
	Item().margin = m;
	return *this;
}

WidgetBuilder & WidgetBuilder::AlignSelf(CrossAlign a) {
	Item().alignSelf = Some(a);
	return *this;
}

WidgetBuilder & WidgetBuilder::MinSize(float mw, float mh) {
	Item().minWidth = Some(mw);
	Item().minHeight = Some(mh);
	return *this;
}

WidgetBuilder & WidgetBuilder::MaxSize(float mw, float mh) {
	Item().maxWidth = Some(mw);
	Item().maxHeight = Some(mh);
	return *this;
}

WidgetBuilder & WidgetBuilder::Hidden() {
	hidden = true;
	return *this;
}

WidgetBuilder & WidgetBuilder::Disabled() {
	disabled = true;
	return *this;
}

WidgetBuilder & WidgetBuilder::PointerThrough() {
	pointerThrough = true;
	return *this;
}

WidgetBuilder & WidgetBuilder::Tooltip(String text) {
	tooltip = Some(UiTooltip{std::move(text)});
	return *this;
}

WidgetBuilder & WidgetBuilder::Name(String n) {
	name = Some(UiName{std::move(n)});
	return *this;
}

WidgetBuilder & WidgetBuilder::Parent(ecs::Entity p) {
	parent = Some(p);
	return *this;
}

WidgetBuilder & WidgetBuilder::Column() {
	Flow().dir = LayoutDir::Column;
	return *this;
}

WidgetBuilder & WidgetBuilder::Row() {
	Flow().dir = LayoutDir::Row;
	return *this;
}

WidgetBuilder & WidgetBuilder::Gap(float g) {
	Flow().gap = g;
	return *this;
}

WidgetBuilder & WidgetBuilder::Pad(float p) {
	Flow().padding = math::Sides(p);
	return *this;
}

WidgetBuilder & WidgetBuilder::Pad(math::Sides p) {
	Flow().padding = p;
	return *this;
}

WidgetBuilder & WidgetBuilder::Align(CrossAlign a) {
	Flow().align = a;
	return *this;
}

WidgetBuilder & WidgetBuilder::Shader(UiShaderEffect effect) {
	shaderEffect = Some(effect);
	return *this;
}

WidgetBuilder & WidgetBuilder::TintShiftEffect(sdl3::FColor color, float strength) {
	return Shader(UiShaderEffect{UiShaderEffectKind::TINT_SHIFT, color, strength});
}

WidgetBuilder & WidgetBuilder::GlowEffect(sdl3::FColor color, float innerRadius) {
	return Shader(UiShaderEffect{UiShaderEffectKind::GLOW, color, innerRadius});
}

WidgetBuilder & WidgetBuilder::Scrollable() {
	rect.clipContent = true;
	return *this;
}

WidgetBuilder & WidgetBuilder::Bg(sdl3::FColor c) {
	Panel();
	StyleMut().SetBg(c);
	return *this;
}

WidgetBuilder & WidgetBuilder::Gradient(sdl3::FColor bottom) {
	Panel();
	StyleMut().SetBgGradient(bottom);
	return *this;
}

WidgetBuilder & WidgetBuilder::BorderColor(sdl3::FColor c, float width) {
	Panel();
	StyleMut().SetBorderColor(c).SetBordersWidth(width);
	return *this;
}

WidgetBuilder & WidgetBuilder::Radius(float r) {
	StyleMut().SetBordersRadius(r);
	return *this;
}

WidgetBuilder & WidgetBuilder::FontSize(float fs) {
	StyleMut().SetFontSize(fs);
	return *this;
}

WidgetBuilder & WidgetBuilder::InnerZoom(float v) {
	StyleMut().SetInnerZoom(v);
	return *this;
}

WidgetBuilder & WidgetBuilder::TextColor(sdl3::FColor c) {
	StyleMut().SetTextColor(c);
	return *this;
}

WidgetBuilder & WidgetBuilder::TextOverflowMode(ui::TextOverflow mode) {
	OverflowMut().mode = mode;
	return *this;
}

WidgetBuilder & WidgetBuilder::TextMarquee(float speed, float pause) {
	UiTextOverflow &o = OverflowMut();
	o.speed = speed;
	o.pause = pause;
	o.mode = ui::TextOverflow::MARQUEE;
	return *this;
}

WidgetBuilder & WidgetBuilder::Italic(bool v) {
	StyleMut().SetItalic(v);
	return *this;
}

WidgetBuilder & WidgetBuilder::Bold(bool v) {
	StyleMut().SetBold(v);
	return *this;
}

WidgetBuilder & WidgetBuilder::Underline(bool v) {
	StyleMut().SetUnderline(v);
	return *this;
}

WidgetBuilder & WidgetBuilder::Strikethrough(bool v) {
	StyleMut().SetStrikethrough(v);
	return *this;
}

WidgetBuilder & WidgetBuilder::Highlight(bool v) {
	StyleMut().SetHighlight(v);
	return *this;
}

WidgetBuilder & WidgetBuilder::HighlightColor(sdl3::FColor c) {
	StyleMut().SetHighlightColor(c);
	return *this;
}

WidgetBuilder & WidgetBuilder::Glass(float gloss, float glow) {
	StyleMut().SetGloss(gloss).SetGlow(glow).SetGlowColor(sdl3::FColor::UI_WHITE_STRONG());
	return *this;
}

WidgetBuilder & WidgetBuilder::Fit(ImageFit mode) {
	if (image.IsSome())
		image->fit = mode;
	return *this;
}

WidgetBuilder & WidgetBuilder::IoMode(IOMode mode) {
	if (input.IsSome())
		input->ioMode = mode;
	if (inputArea.IsSome())
		inputArea->ioMode = mode;
	return *this;
}

WidgetBuilder & WidgetBuilder::MaxLen(size_t n) {
	if (input.IsSome())
		input->maxLen = n;
	if (inputArea.IsSome())
		inputArea->maxLen = n;
	return *this;
}

WidgetBuilder & WidgetBuilder::CodeEditor(UiSyntaxHighlighter highlighter) {
	if (inputArea.IsSome()) {
		inputArea->monospace = true;
		inputArea->lineNumbers = true;
		inputArea->highlightCurrentLine = true;
		inputArea->followTail = false;
		inputArea->highlighter = std::move(highlighter);
	}
	return *this;
}

WidgetBuilder & WidgetBuilder::LineNumbers(bool enabled) {
	if (inputArea.IsSome())
		inputArea->lineNumbers = enabled;
	return *this;
}

WidgetBuilder & WidgetBuilder::Monospace(bool enabled) {
	if (inputArea.IsSome())
		inputArea->monospace = enabled;
	return *this;
}

WidgetBuilder & WidgetBuilder::Highlighter(UiSyntaxHighlighter highlighter) {
	if (inputArea.IsSome())
		inputArea->highlighter = std::move(highlighter);
	return *this;
}

WidgetBuilder & WidgetBuilder::OnClick(std::function<void()> fn) {
	Callbacks().onClick = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::OnChange(std::function<void(float)> fn) {
	Callbacks().onChange = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::OnToggle(std::function<void(bool)> fn) {
	Callbacks().onToggle = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::onScroll(std::function<void(float)> fn) {
	Callbacks().onScroll = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::onTextChange(std::function<void(const String &)> fn) {
	Callbacks().onTextChange = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::OnTextChange(std::function<void(const String &)> fn) {
	Callbacks().onTextChange = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::OnSubmit(std::function<void(const String &)> fn) {
	Callbacks().onSubmit = std::move(fn);
	return *this;
}

ecs::Entity WidgetBuilder::Spawn() {
	ecs::Entity e = world->Spawn();
	world->AddComponent(e, rect);
	if (flow.IsSome())
		world->AddComponent(e, *flow);
	if (item.IsSome())
		world->AddComponent(e, *item);
	if (panel.IsSome())
		world->AddComponent(e, *panel);
	if (label.IsSome())
		world->AddComponent(e, std::move(*label));
	if (button.IsSome())
		world->AddComponent(e, std::move(*button));
	if (toggle.IsSome())
		world->AddComponent(e, *toggle);
	if (checkbox.IsSome())
		world->AddComponent(e, *checkbox);
	if (slider.IsSome())
		world->AddComponent(e, *slider);
	if (progress.IsSome())
		world->AddComponent(e, *progress);
	if (separator.IsSome())
		world->AddComponent(e, *separator);
	if (input.IsSome())
		world->AddComponent(e, std::move(*input));
	if (inputArea.IsSome())
		world->AddComponent(e, std::move(*inputArea));
	if (dragValue.IsSome())
		world->AddComponent(e, std::move(*dragValue));
	if (colorSwatch.IsSome())
		world->AddComponent(e, *colorSwatch);
	if (svSquare.IsSome())
		world->AddComponent(e, *svSquare);
	if (hueSlider.IsSome())
		world->AddComponent(e, *hueSlider);
	if (alphaSlider.IsSome())
		world->AddComponent(e, *alphaSlider);
	if (selectable.IsSome())
		world->AddComponent(e, *selectable);
	if (treeNode.IsSome())
		world->AddComponent(e, std::move(*treeNode));
	if (reorderable.IsSome())
		world->AddComponent(e, *reorderable);
	if (selection.IsSome())
		world->AddComponent(e, *selection);
	if (menuBarItem.IsSome())
		world->AddComponent(e, std::move(*menuBarItem));
	if (menuItem.IsSome())
		world->AddComponent(e, std::move(*menuItem));
	if (table.IsSome())
		world->AddComponent(e, std::move(*table));
	if (plot.IsSome())
		world->AddComponent(e, std::move(*plot));
	if (calendar.IsSome())
		world->AddComponent(e, std::move(*calendar));
	if (resizeHandle.IsSome())
		world->AddComponent(e, std::move(*resizeHandle));
	if (textOverflow.IsSome())
		world->AddComponent(e, std::move(*textOverflow));
	if (dragPayload.IsSome())
		world->AddComponent(e, std::move(*dragPayload));
	if (dropTarget.IsSome())
		world->AddComponent(e, std::move(*dropTarget));
	if (image.IsSome())
		world->AddComponent(e, std::move(*image));
	if (viewport3d.IsSome())
		world->AddComponent(e, std::move(*viewport3d));
	if (shaderEffect.IsSome())
		world->AddComponent(e, *shaderEffect);
	if (icon.IsSome())
		world->AddComponent(e, std::move(*icon));
	if (radio.IsSome())
		world->AddComponent(e, std::move(*radio));
	if (scrollbar.IsSome())
		world->AddComponent(e, *scrollbar);
	if (knob.IsSome())
		world->AddComponent(e, *knob);
	if (canvas.IsSome())
		world->AddComponent(e, std::move(*canvas));
	// Visibilité initiale des enfants : expander replié = tous cachés ;
	// tabview = seul l'onglet actif visible (appliqué après le spawn des kids).
	const bool COLLAPSE_KIDS = expander.IsSome() && !expander->expanded;
	const int TAB_ACTIVE = tabview.IsSome() ? tabview->active : -1;
	if (combo.IsSome())
		world->AddComponent(e, std::move(*combo));
	if (listbox.IsSome())
		world->AddComponent(e, std::move(*listbox));
	if (expander.IsSome())
		world->AddComponent(e, std::move(*expander));
	if (tabview.IsSome())
		world->AddComponent(e, std::move(*tabview));
	if (spinner.IsSome())
		world->AddComponent(e, *spinner);
	if (badge.IsSome())
		world->AddComponent(e, std::move(*badge));
	if (tooltip.IsSome())
		world->AddComponent(e, std::move(*tooltip));
	if (popupState.IsSome())
		world->AddComponent(e, *popupState);
	if (overlayLayer.IsSome())
		world->AddComponent(e, *overlayLayer);
	if (callbacks.IsSome())
		world->AddComponent(e, std::move(*callbacks));
	if (name.IsSome())
		world->AddComponent(e, std::move(*name));
	if (hidden)
		world->AddComponent(e, UiHidden{});
	if (disabled)
		world->AddComponent(e, UiDisabled{});
	if (pointerThrough)
		world->AddComponent(e, UiPointerThrough{});
	if (style.IsSome())
		world->AddComponent(e, std::move(*style));
	{
		// "root" (défauts génériques) + "root-<type>" (défauts thémés du
		// widget, cf. UiFactory::registerRootClasses) sont toujours en
		// tête — priorité la plus basse, donc les classes explicites de
		// l'appelant (poussées après, cf. className()) et le style
		// inline ci-dessus l'emportent toujours (cascade CSS standard).
		std::vector<String> names{String("root")};
		if (auto k = KindClass(); k.IsSome())
			names.push_back(k.Unwrap());
		for (auto &n : classNames)
			names.push_back(n);
		world->AddComponent(e, UiClassList{std::move(names)});
	}
	// Toute entité fraîchement spawnée a besoin d'au moins une passe de
	// résolution de cascade (héritage fontSize/textColor/opacity depuis
	// le parent, classes, style inline) — le flag s'auto-nettoie après
	// le premier StyleSystem::Resolve() (cf. styles.hpp), donc une UI au
	// repos ne repaie jamais ce coût.
	world->AddComponent(e, UiStyleDirty{});
	world->AddComponent(e, UiComputed{});

	if (parent.IsSome())
		SetParent(*world, e, *parent);

	for (auto &kid : kids) {
		kid->parent = Some(e);
		kid->Spawn();
	}

	if (COLLAPSE_KIDS || TAB_ACTIVE >= 0) {
		if (auto ch = world->GetComponent<UiChildren>(e); ch.IsSome()) {
			std::vector<ecs::Entity> kids = ch.Unwrap()->list;
			for (size_t i = 0; i < kids.size(); ++i) {
				bool hide = COLLAPSE_KIDS || int(i) != TAB_ACTIVE;
				if (hide && !world->HasComponent<UiHidden>(kids[i]))
					world->AddComponent(kids[i], UiHidden{});
			}
		}
	}

	layout->MarkDirty();
	return e;
}

WidgetBuilder & WidgetBuilder::Style(UiStyle s) {
	style = Some(std::move(s));
	return *this;
}

WidgetBuilder & WidgetBuilder::ClassName(String name) {
	classNames.push_back(std::move(name));
	return *this;
}

WidgetBuilder & WidgetBuilder::ClassNames(std::vector<String> names) {
	classNames = std::move(names);
	return *this;
}

WidgetBuilder & WidgetBuilder::SelectionRoot(bool multiSelect) {
	selection = Some(UiSelection{{}, multiSelect});
	return *this;
}

WidgetBuilder & WidgetBuilder::DragPayload(String kind, int64_t id, String label) {
	UiDragPayload payload;
	payload.kind = std::move(kind);
	payload.id = id;
	payload.label = std::move(label);
	dragPayload = Some(std::move(payload));
	return *this;
}

WidgetBuilder & WidgetBuilder::DropTarget(String accepts, bool highlight) {
	UiDropTarget target;
	target.accepts = std::move(accepts);
	target.highlight = highlight;
	dropTarget = Some(std::move(target));
	return *this;
}

WidgetBuilder & WidgetBuilder::OnDoubleClick(std::function<void()> fn) {
	Callbacks().onDoubleClick = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::OnDragStart(std::function<void()> fn) {
	Callbacks().onDragStart = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::OnDrop(std::function<void(int64_t)> fn) {
	Callbacks().onDrop = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::OnContextMenu(std::function<void(float, float)> fn) {
	Callbacks().onContextMenu = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::Reorderable() {
	reorderable = Some(UiReorderable{});
	return *this;
}

WidgetBuilder & WidgetBuilder::OnReorder(std::function<void(int, int)> fn) {
	Callbacks().onReorder = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::AddSeries(PlotSeriesKind kind, std::span<const float> x, std::span<const float> y,
		String label, Option<sdl3::FColor> color) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	PlotSeries s;
	s.kind = kind;
	s.SetXy(x, y);
	s.label = std::move(label);
	s.color = color.IsSome() ? color.Unwrap() : PlotPaletteColor(plot->series.size());
	plot->series.push_back(std::move(s));
	return *this;
}

WidgetBuilder & WidgetBuilder::AddPieSlice(float value, String label, Option<sdl3::FColor> color) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	PieSlice sl;
	sl.value = value;
	sl.label = std::move(label);
	sl.color = color.IsSome() ? color.Unwrap() : PlotPaletteColor(plot->pieSlices.size());
	plot->pieSlices.push_back(std::move(sl));
	return *this;
}

WidgetBuilder & WidgetBuilder::SetHeatmapData(int rows, int cols, std::span<const float> values, Colormap cm) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	plot->heatmap.rows = rows;
	plot->heatmap.cols = cols;
	plot->heatmap.values.assign(values.begin(), values.end());
	plot->heatmap.colormap = cm;
	return *this;
}

WidgetBuilder & WidgetBuilder::AddOhlcBar(float x, float open, float high, float low, float close) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	OhlcBar bar;
	bar.x = x;
	bar.open = open;
	bar.high = high;
	bar.low = low;
	bar.close = close;
	plot->candleBars.push_back(bar);
	return *this;
}

WidgetBuilder & WidgetBuilder::SetYError(std::span<const float> err) {
	if (plot.IsSome() && !plot->series.empty())
		plot->series.back().yError.assign(err.begin(), err.end());
	return *this;
}

WidgetBuilder & WidgetBuilder::SetOnCustomDraw(std::function<void(sdl3::Renderer &, const sdl3::FRect &, const PlotSeries &,
		const PlotAxis &, const PlotAxis &)>
		fn) {
	if (plot.IsSome() && !plot->series.empty())
		plot->series.back().onCustomDraw = std::move(fn);
	return *this;
}

WidgetBuilder & WidgetBuilder::AddLineSeries(std::span<const float> y, String label, Option<sdl3::FColor> color) {
	std::vector<float> x(y.size());
	for (size_t i = 0; i < x.size(); ++i)
		x[i] = float(i);
	return AddSeries(PlotSeriesKind::LINE, x, y, std::move(label), color);
}

WidgetBuilder & WidgetBuilder::AddLineSeriesXy(std::span<const float> x, std::span<const float> y, String label,
		Option<sdl3::FColor> color) {
	return AddSeries(PlotSeriesKind::LINE, x, y, std::move(label), color);
}

WidgetBuilder & WidgetBuilder::AddScatterSeries(std::span<const float> x, std::span<const float> y, String label,
		Option<sdl3::FColor> color) {
	return AddSeries(PlotSeriesKind::SCATTER, x, y, std::move(label), color);
}

WidgetBuilder & WidgetBuilder::AddStepSeries(std::span<const float> x, std::span<const float> y, String label,
		Option<sdl3::FColor> color) {
	return AddSeries(PlotSeriesKind::STEP, x, y, std::move(label), color);
}

WidgetBuilder & WidgetBuilder::AddAreaSeries(std::span<const float> x, std::span<const float> y, String label,
		Option<sdl3::FColor> color, float fillOpacity) {
	AddSeries(PlotSeriesKind::AREA, x, y, std::move(label), color);
	plot->series.back().fillOpacity = fillOpacity;
	return *this;
}

WidgetBuilder & WidgetBuilder::AddStemSeries(std::span<const float> x, std::span<const float> y, String label,
		Option<sdl3::FColor> color) {
	return AddSeries(PlotSeriesKind::STEM, x, y, std::move(label), color);
}

WidgetBuilder & WidgetBuilder::AddBarSeries(std::span<const float> x, std::span<const float> y, String label,
		Option<sdl3::FColor> color, float barWidth) {
	AddSeries(PlotSeriesKind::BAR, x, y, std::move(label), color);
	plot->series.back().barWidth = barWidth;
	return *this;
}

WidgetBuilder & WidgetBuilder::AddBarHSeries(std::span<const float> x, std::span<const float> y, String label,
		Option<sdl3::FColor> color, float barWidth) {
	AddSeries(PlotSeriesKind::BAR_H, x, y, std::move(label), color);
	plot->series.back().barWidth = barWidth;
	return *this;
}

WidgetBuilder & WidgetBuilder::AddHistogramSeries(std::span<const float> x, std::span<const float> y, String label,
		Option<sdl3::FColor> color, float barWidth) {
	AddSeries(PlotSeriesKind::HISTOGRAM, x, y, std::move(label), color);
	plot->series.back().barWidth = barWidth;
	return *this;
}

WidgetBuilder & WidgetBuilder::XTicks(PlotEdge position, bool overlay, Option<sdl3::FColor> boxColor) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	plot->xAxis.tickPosition = position;
	plot->xAxis.tickOverlay = overlay;
	if (boxColor.IsSome())
		plot->xAxis.tickBoxColor = boxColor.Unwrap();
	return *this;
}

WidgetBuilder & WidgetBuilder::YTicks(PlotEdge position, bool overlay, Option<sdl3::FColor> boxColor) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	plot->yAxis.tickPosition = position;
	plot->yAxis.tickOverlay = overlay;
	if (boxColor.IsSome())
		plot->yAxis.tickBoxColor = boxColor.Unwrap();
	return *this;
}

WidgetBuilder & WidgetBuilder::Legend(LegendPosition position, bool overlay, Option<sdl3::FColor> boxColor) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	plot->legendPosition = position;
	plot->legendOverlay = overlay;
	if (boxColor.IsSome())
		plot->legendBoxColor = boxColor.Unwrap();
	return *this;
}

WidgetBuilder & WidgetBuilder::ShowLegend(bool show) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	plot->showLegend = show;
	return *this;
}

WidgetBuilder & WidgetBuilder::LogScaleX(bool enable) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	plot->xAxis.logScale = enable;
	return *this;
}

WidgetBuilder & WidgetBuilder::LogScaleY(bool enable) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	plot->yAxis.logScale = enable;
	return *this;
}

WidgetBuilder & WidgetBuilder::LogScaleY2(bool enable) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	plot->yAxis2.logScale = enable;
	return *this;
}

WidgetBuilder & WidgetBuilder::UseSecondaryY(bool enable) {
	if (plot.IsSome() && !plot->series.empty())
		plot->series.back().useSecondaryY = enable;
	return *this;
}

WidgetBuilder & WidgetBuilder::YTicks2(PlotEdge position, bool overlay, Option<sdl3::FColor> boxColor) {
	if (plot.IsNone())
		plot = Some(UiPlot{});
	plot->yAxis2.tickPosition = position;
	plot->yAxis2.tickOverlay = overlay;
	if (boxColor.IsSome())
		plot->yAxis2.tickBoxColor = boxColor.Unwrap();
	return *this;
}

WidgetBuilder & WidgetBuilder::OverlayOrder(int order) {
	overlayLayer = Some(UiOverlayLayer{order});
	return *this;
}

UiFlow & WidgetBuilder::Flow() {
	if (flow.IsNone())
		flow = Some(UiFlow{});
	return *flow;
}

UiItem & WidgetBuilder::Item() {
	if (item.IsNone())
		item = Some(UiItem{});
	return *item;
}

UiPanel & WidgetBuilder::Panel() {
	if (panel.IsNone())
		panel = Some(UiPanel{});
	return *panel;
}

UiCallbacks & WidgetBuilder::Callbacks() {
	if (callbacks.IsNone())
		callbacks = Some(UiCallbacks{});
	return *callbacks;
}

UiStyle & WidgetBuilder::StyleMut() {
	if (style.IsNone())
		style = Some(UiStyle{});
	return *style;
}

UiTextOverflow & WidgetBuilder::OverflowMut() {
	if (textOverflow.IsNone())
		textOverflow = Some(UiTextOverflow{});
	return *textOverflow;
}

Option<String> WidgetBuilder::KindClass() const {
	if (panel.IsSome())
		return Some(String("root-panel"));
	if (label.IsSome())
		return Some(String("root-label"));
	if (button.IsSome())
		return Some(String("root-button"));
	if (toggle.IsSome())
		return Some(String("root-toggle"));
	if (checkbox.IsSome())
		return Some(String("root-checkbox"));
	if (slider.IsSome())
		return Some(String("root-slider"));
	if (progress.IsSome())
		return Some(String("root-progress"));
	if (separator.IsSome())
		return Some(String("root-separator"));
	if (input.IsSome())
		return Some(String("root-input"));
	if (inputArea.IsSome())
		return Some(String("root-input"));
	if (dragValue.IsSome())
		return Some(String("root-dragvalue"));
	if (colorSwatch.IsSome())
		return Some(String("root-colorswatch"));
	if (selectable.IsSome())
		return Some(String("root-selectable"));
	if (treeNode.IsSome())
		return Some(String("root-treenode"));
	if (menuBarItem.IsSome())
		return Some(String("root-menubaritem"));
	if (menuItem.IsSome())
		return Some(String("root-menuitem"));
	if (table.IsSome())
		return Some(String("root-table"));
	if (plot.IsSome())
		return Some(String("root-plot"));
	if (calendar.IsSome())
		return Some(String("root-calendar"));
	if (resizeHandle.IsSome())
		return Some(String("root-splitterhandle"));
	if (icon.IsSome())
		return Some(String("root-icon"));
	if (radio.IsSome())
		return Some(String("root-radio"));
	if (scrollbar.IsSome())
		return Some(String("root-scrollbar"));
	if (knob.IsSome())
		return Some(String("root-knob"));
	if (combo.IsSome())
		return Some(String("root-combo"));
	if (listbox.IsSome())
		return Some(String("root-listbox"));
	if (expander.IsSome())
		return Some(String("root-expander"));
	if (tabview.IsSome())
		return Some(String("root-tabview"));
	if (spinner.IsSome())
		return Some(String("root-spinner"));
	if (badge.IsSome())
		return Some(String("root-badge"));
	return NONE;
}

// ── UiFactory ────────────────────────────────────────────────────────────────

UiFactory::UiFactory(ecs::ArchetypeRegistry &world, LayoutSystem &layout, UiTheme t)
	: theme(std::move(t)), world(&world), layout(&layout) {
	// Base de DimUnit::Rem et police héritée par défaut = police du thème.
	this->layout->rootFontSize = theme.fontSize;
	RegisterRootClasses();
}

void UiFactory::SetTheme(UiTheme newTheme) {
	theme = std::move(newTheme);
	layout->rootFontSize = theme.fontSize;
	RegisterRootClasses();
	sheet.DirtyAllUsers(*world);
	layout->MarkDirty();
}

WidgetBuilder UiFactory::Column() {
	WidgetBuilder b(*world, *layout);
	b.Column();
	return b;
}

WidgetBuilder UiFactory::Row() {
	WidgetBuilder b(*world, *layout);
	b.Row();
	return b;
}

WidgetBuilder UiFactory::Panel() {
	auto b = Column();
	b.panel = Some(UiPanel{});
	if (theme.glassDefault)
		b.Style(UiStyle::GlassPanel(theme.panelBg));
	return b;
}

WidgetBuilder UiFactory::Label(String text) {
	WidgetBuilder b(*world, *layout);
	b.label = Some(UiLabel{std::move(text)});
	return b;
}

WidgetBuilder UiFactory::Button(String text) {
	WidgetBuilder b(*world, *layout);
	UiButton btn;
	btn.text = std::move(text);
	b.button = Some(std::move(btn));
	if (theme.glassDefault)
		b.Style(UiStyle::GlassButton(theme.fill));
	return b;
}

WidgetBuilder UiFactory::Toggle(bool checked) {
	WidgetBuilder b(*world, *layout);
	UiToggle t;
	t.checked = checked;
	t.animT = checked ? 1.f : 0.f;
	b.toggle = Some(t);
	return b;
}

WidgetBuilder UiFactory::Checkbox(bool checked) {
	WidgetBuilder b(*world, *layout);
	UiCheckbox c;
	c.checked = checked;
	b.checkbox = Some(c);
	return b;
}

WidgetBuilder UiFactory::Slider(float min, float max, float value, float step, Orientation orient) {
	WidgetBuilder b(*world, *layout);
	UiSlider s;
	s.min = min;
	s.max = max;
	s.value = value;
	s.step = step;
	s.orient = orient;
	b.slider = Some(s);
	return b;
}

WidgetBuilder UiFactory::Progress(float min, float max, float value) {
	WidgetBuilder b(*world, *layout);
	UiProgress p;
	p.min = min;
	p.max = max;
	p.value = value;
	b.progress = Some(p);
	return b;
}

WidgetBuilder UiFactory::Separator(Orientation orient) {
	WidgetBuilder b(*world, *layout);
	UiSeparator s;
	s.orient = orient;
	b.separator = Some(s);
	return b;
}

WidgetBuilder UiFactory::Input(String placeholder) {
	WidgetBuilder b(*world, *layout);
	UiInput f;
	f.placeholder = std::move(placeholder);
	b.input = Some(std::move(f));
	b.Scrollable();
	return b;
}

WidgetBuilder UiFactory::InputArea(String placeholder) {
	WidgetBuilder b(*world, *layout);
	UiInputArea f;
	f.placeholder = std::move(placeholder);
	b.inputArea = Some(std::move(f));
	b.Scrollable();
	return b;
}

WidgetBuilder UiFactory::DragValue(float min, float max, float value, float step, float speed, int decimals) {
	WidgetBuilder b(*world, *layout);
	UiDragValue d;
	d.min = min;
	d.max = max;
	d.value = sdl3::Clamp(value, min, max);
	d.step = step;
	d.speed = speed;
	d.decimals = decimals;
	b.dragValue = Some(d);
	return b;
}

WidgetBuilder UiFactory::InputNumber(float min, float max, float value, float step, int decimals,
		std::function<void(float)> onChange) {
	static uint32_t sCounter = 0;
	String fieldName = String::Format("__ui_inputNumber_%u", ++sCounter);
	ecs::ArchetypeRegistry *world = this->world;

	auto applyDelta = [world, fieldName, min, max, decimals, onChange](float delta) {
		auto e = FindByName(*world, fieldName);
		if (e.IsNone())
			return;
		auto in = world->GetComponent<UiInput>(e.Unwrap());
		if (in.IsNone())
			return;
		float v = sdl3::Clamp(in.Unwrap()->text.ToFloat() + delta, min, max);
		in.Unwrap()->text = String::From(v, decimals);
		in.Unwrap()->cursor = in.Unwrap()->selectionAnchor = in.Unwrap()->text.size();
		if (onChange)
			onChange(v);
	};

	float side = theme.fontSize + 14.f;
	WidgetBuilder minusBtn = Button("-");
	minusBtn.Size(side, side).OnClick([applyDelta, step] { applyDelta(-step); });
	WidgetBuilder plusBtn = Button("+");
	plusBtn.Size(side, side).OnClick([applyDelta, step] { applyDelta(step); });

	WidgetBuilder field = Input();
	field.Name(fieldName).GrowW();
	field.input->text = String::From(sdl3::Clamp(value, min, max), decimals);
	if (onChange) {
		field.OnSubmit([world, fieldName, min, max, decimals, onChange](const String &text) {
			float v = sdl3::Clamp(text.ToFloat(), min, max);
			if (auto e = FindByName(*world, fieldName); e.IsSome())
				if (auto in = world->GetComponent<UiInput>(e.Unwrap()); in.IsSome()) {
					in.Unwrap()->text = String::From(v, decimals);
					in.Unwrap()->cursor = in.Unwrap()->selectionAnchor = in.Unwrap()->text.size();
				}
			onChange(v);
		});
	}

	auto b = Row();
	b.Gap(4.f).WAuto().HAuto().Align(CrossAlign::Center);
	b.Children(std::move(minusBtn), std::move(field), std::move(plusBtn));
	return b;
}

WidgetBuilder UiFactory::ColorSwatch(sdl3::FColor initial) {
	WidgetBuilder b(*world, *layout);
	b.colorSwatch = Some(UiColorSwatch{initial});
	return b;
}

WidgetBuilder UiFactory::SvSquare(float h, float s, float v) {
	WidgetBuilder b(*world, *layout);
	b.svSquare = Some(UiSVSquare{h, s, v});
	return b;
}

WidgetBuilder UiFactory::HueSlider(float hue) {
	WidgetBuilder b(*world, *layout);
	b.hueSlider = Some(UiHueSlider{hue});
	return b;
}

WidgetBuilder UiFactory::AlphaSlider(float alpha, sdl3::FColor baseColor) {
	WidgetBuilder b(*world, *layout);
	b.alphaSlider = Some(UiAlphaSlider{alpha, baseColor});
	return b;
}

WidgetBuilder UiFactory::ColorPicker(sdl3::FColor initial, std::function<void(sdl3::FColor)> onChange) {
	static uint32_t sCounter = 0;
	String prefix = String::Format("__ui_colorPicker_%u_", ++sCounter);
	String svName = prefix + "sv", hueName = prefix + "hue", alphaName = prefix + "alpha",
		   hexName = prefix + "hex", swatchName = prefix + "swatch";

	HSV hsv = ColorToHsv(initial);
	ecs::ArchetypeRegistry *world = this->world;

	// Relit l'état des 3 sous-widgets, recompose la sdl3::FColor pleine (le
	// slider de teinte est la source de vérité pour `h` — le carré SV
	// n'en garde qu'une COPIE pour teinter son dégradé), resynchronise le
	// champ hex + l'aperçu, puis notifie l'appelant. Appelé après CHAQUE
	// ajustement (cf. les callbacks "ping" de UiSVSquare/UiHueSlider/
	// UiAlphaSlider dans systems.hpp dispatch()) — pas de système dédié :
	// la resynchronisation reste ponctuelle, pilotée par l'évènement.
	auto resync = [world, svName, hueName, alphaName, hexName, swatchName, onChange] {
		float h = 0.f, s = 1.f, v = 1.f, a = 1.f;
		if (auto e = FindByName(*world, svName); e.IsSome())
			if (auto c = world->GetComponent<UiSVSquare>(e.Unwrap()); c.IsSome()) {
				h = c.Unwrap()->h;
				s = c.Unwrap()->s;
				v = c.Unwrap()->v;
			}
		if (auto e = FindByName(*world, hueName); e.IsSome())
			if (auto c = world->GetComponent<UiHueSlider>(e.Unwrap()); c.IsSome())
				h = c.Unwrap()->hue;
		if (auto e = FindByName(*world, alphaName); e.IsSome())
			if (auto c = world->GetComponent<UiAlphaSlider>(e.Unwrap()); c.IsSome())
				a = c.Unwrap()->alpha;
		sdl3::FColor rgb = HsvToColor(h, s, v, a);
		if (auto e = FindByName(*world, svName); e.IsSome())
			if (auto c = world->GetComponent<UiSVSquare>(e.Unwrap()); c.IsSome())
				c.Unwrap()->h = h;
		if (auto e = FindByName(*world, alphaName); e.IsSome())
			if (auto c = world->GetComponent<UiAlphaSlider>(e.Unwrap()); c.IsSome())
				c.Unwrap()->baseColor = sdl3::FColor{rgb.r, rgb.g, rgb.b, 1.f};
		if (auto e = FindByName(*world, hexName); e.IsSome())
			if (auto c = world->GetComponent<UiInput>(e.Unwrap()); c.IsSome())
				c.Unwrap()->text = ColorToHex(rgb);
		if (auto e = FindByName(*world, swatchName); e.IsSome())
			if (auto c = world->GetComponent<UiColorSwatch>(e.Unwrap()); c.IsSome())
				c.Unwrap()->color = rgb;
		if (onChange)
			onChange(rgb);
	};

	WidgetBuilder sv = SvSquare(hsv.h, hsv.s, hsv.v);
	sv.Name(svName).Size(160.f, 160.f).OnChange([resync](float) { resync(); });
	WidgetBuilder hue = HueSlider(hsv.h);
	hue.Name(hueName).Size(160.f, 16.f).OnChange([resync](float) { resync(); });
	// `FColor` est déjà en [0, 1] : l'ancienne division par 255 (vestige
	// de sdl3::Color) ouvrait le sélecteur sur un alpha quasi nul, et la
	// PREMIÈRE retouche rendait la couleur transparente.
	WidgetBuilder alpha = AlphaSlider(initial.a, sdl3::FColor{initial.r, initial.g, initial.b, 1.f});
	alpha.Name(alphaName).Size(160.f, 16.f).OnChange([resync](float) { resync(); });
	WidgetBuilder preview = ColorSwatch(initial);
	preview.Name(swatchName).Size(28.f, 28.f);

	WidgetBuilder hex = Input();
	hex.Name(hexName).GrowW();
	hex.input->text = ColorToHex(initial);
	hex.OnSubmit([world, svName, hueName, alphaName, resync](const String &text) {
		sdl3::FColor c = HexToColor(text);
		HSV parsed = ColorToHsv(c);
		if (auto e = FindByName(*world, svName); e.IsSome())
			if (auto comp = world->GetComponent<UiSVSquare>(e.Unwrap()); comp.IsSome()) {
				comp.Unwrap()->h = parsed.h;
				comp.Unwrap()->s = parsed.s;
				comp.Unwrap()->v = parsed.v;
			}
		if (auto e = FindByName(*world, hueName); e.IsSome())
			if (auto comp = world->GetComponent<UiHueSlider>(e.Unwrap()); comp.IsSome())
				comp.Unwrap()->hue = parsed.h;
		if (auto e = FindByName(*world, alphaName); e.IsSome())
			if (auto comp = world->GetComponent<UiAlphaSlider>(e.Unwrap()); comp.IsSome())
				comp.Unwrap()->alpha = c.a; // FColor : déjà en [0, 1]
		resync();
	});

	auto previewRow = Row();
	previewRow.Gap(8.f).Align(CrossAlign::Center).Children(std::move(preview), std::move(hex));

	auto p = Popup();
	p.Column().Gap(8.f).Pad(10.f).WAuto().HAuto();
	p.Children(std::move(sv), std::move(hue), std::move(alpha), std::move(previewRow));
	return p;
}

WidgetBuilder UiFactory::Selectable(String text, int index) {
	WidgetBuilder b(*world, *layout);
	UiSelectable sel;
	sel.index = index;
	b.selectable = Some(sel);
	b.Row().Gap(6.f).Align(CrossAlign::Center).Pad(math::Sides{8.f, 4.f, 8.f, 4.f});
	b.WAuto().HAuto();
	b.Children(Label(std::move(text)));
	return b;
}

WidgetBuilder UiFactory::TreeNode(String title, int index, int depth, bool expanded) {
	WidgetBuilder b(*world, *layout);
	UiTreeNode t;
	t.title = std::move(title);
	t.index = index;
	t.depth = depth;
	t.expanded = expanded;
	t.headerHeight = theme.fontSize + 12.f;
	b.Column().Gap(2.f);
	b.Pad(math::Sides{24.f, t.headerHeight + 2.f, 8.f, 4.f});
	b.WAuto().HAuto(); // recalcul requis au repli/dépli, cf. expander()
	b.treeNode = Some(std::move(t));
	return b;
}

WidgetBuilder UiFactory::MenuBar() {
	auto b = Row();
	b.Gap(2.f).Pad(math::Sides{4.f, 2.f}).WAuto().HAuto();
	return b;
}

WidgetBuilder UiFactory::MenuItem(String text, String shortcut) {
	WidgetBuilder b(*world, *layout);
	UiMenuItem mi;
	mi.text = std::move(text);
	mi.shortcut = std::move(shortcut);
	b.menuItem = Some(std::move(mi));
	b.WAuto().HAuto();
	return b;
}

WidgetBuilder UiFactory::Table(std::vector<UiTableColumn> columns, std::vector<std::vector<String>> rows,
		bool multiSelect, bool reorderable) {
	WidgetBuilder b(*world, *layout);
	UiTable t;
	t.columns = std::move(columns);
	t.rows = std::move(rows);
	t.rowHeight = theme.fontSize + 12.f;
	t.headerHeight = theme.fontSize + 16.f;
	t.reorderable = reorderable;
	b.table = Some(std::move(t));
	b.selection = Some(UiSelection{{}, multiSelect});
	b.Size(400.f, 240.f);
	return b;
}

WidgetBuilder UiFactory::Plot(String title) {
	WidgetBuilder b(*world, *layout);
	UiPlot p;
	p.title = std::move(title);
	p.yAxis.tickPosition = PlotEdge::Left;  // PlotAxis::tickPosition défaut Bottom, valide pour X mais pas Y
	p.yAxis2.tickPosition = PlotEdge::Right; // bord opposé à yAxis par défaut (Phase 4)
	b.plot = Some(std::move(p));
	b.Size(300.f, 200.f);
	return b;
}

WidgetBuilder UiFactory::PlotLines(std::span<const float> values, String title, Option<sdl3::FColor> color) {
	WidgetBuilder b = Plot(std::move(title));
	b.AddLineSeries(values, "", color);
	return b;
}

WidgetBuilder UiFactory::Pie(String title) {
	WidgetBuilder b(*world, *layout);
	UiPlot p;
	p.mode = PlotMode::PIE;
	p.title = std::move(title);
	b.plot = Some(std::move(p));
	b.Size(260.f, 220.f);
	return b;
}

WidgetBuilder UiFactory::Heatmap(String title) {
	WidgetBuilder b(*world, *layout);
	UiPlot p;
	p.mode = PlotMode::HEATMAP;
	p.showLegend = false;
	p.title = std::move(title);
	b.plot = Some(std::move(p));
	b.Size(260.f, 220.f);
	return b;
}

WidgetBuilder UiFactory::Candlestick(String title) {
	WidgetBuilder b(*world, *layout);
	UiPlot p;
	p.mode = PlotMode::CANDLE;
	p.showLegend = false;
	p.yAxis.tickPosition = PlotEdge::Left;
	p.title = std::move(title);
	b.plot = Some(std::move(p));
	b.Size(360.f, 220.f);
	return b;
}

ecs::Entity UiFactory::Splitter(WidgetBuilder before, WidgetBuilder after,
		Orientation orient, float initialBeforeSize,
		float minBefore, float minAfter) {
	const bool HORIZ = orient == Orientation::Horizontal;

	WidgetBuilder container(*world, *layout);
	if (HORIZ)
		container.Row();
	else
		container.Column();
	container.Gap(0.f).WAuto().HAuto();
	ecs::Entity containerE = container.Spawn();

	before.Parent(containerE);
	if (HORIZ)
		before.W(Dimension::Px(initialBeforeSize));
	else
		before.H(Dimension::Px(initialBeforeSize));
	ecs::Entity beforeE = before.Spawn();

	WidgetBuilder handleB(*world, *layout);
	if (HORIZ)
		handleB.Size(6.f, 0.f).HAuto();
	else
		handleB.Size(0.f, 6.f).WAuto();
	handleB.Parent(containerE);
	UiResizeHandle h;
	h.orient = orient;
	h.minBefore = minBefore;
	h.maxBefore = 1.0e9f;
	ecs::ArchetypeRegistry *world = this->world;
	h.getBefore = [world, beforeE, HORIZ]() -> float {
		if (auto it = world->GetComponent<UiItem>(beforeE); it.IsSome())
			return HORIZ ? it.Unwrap()->width.value : it.Unwrap()->height.value;
		return 0.f;
	};
	h.setBefore = [world, beforeE, HORIZ](float v) {
		if (auto it = world->GetComponent<UiItem>(beforeE); it.IsSome()) {
			if (HORIZ)
				it.Unwrap()->width = Dimension::Px(v);
			else
				it.Unwrap()->height = Dimension::Px(v);
		}
	};
	handleB.resizeHandle = Some(std::move(h));
	handleB.Spawn();

	after.Parent(containerE);
	if (HORIZ)
		after.GrowW();
	else
		after.GrowH();
	after.Spawn();
	(void)minAfter; // réservé : un 2e panneau explicitement dimensionné (non-Grow) l'utiliserait via setAfter

	return containerE;
}

WidgetBuilder UiFactory::DatePicker(int year, int month, int selectedDay, std::function<void(int, int, int)> onChange) {
	WidgetBuilder cal(*world, *layout);
	UiCalendar c;
	c.year = year;
	c.month = month;
	c.selectedDay = selectedDay;
	c.onDaySelected = std::move(onChange);
	cal.calendar = Some(std::move(c));
	cal.Size(224.f, 200.f);

	auto p = Popup();
	p.Column().Pad(math::Sides{4.f}).WAuto().HAuto();
	p.Children(std::move(cal));
	return p;
}

WidgetBuilder UiFactory::Image(String textureKey, float w, float h) {
	WidgetBuilder b(*world, *layout);
	b.image = Some(UiImage{std::move(textureKey)});
	b.Size(w, h);
	return b;
}

WidgetBuilder UiFactory::Viewport3D(render3d::Object3D &root, render3d::Camera camera) {
	WidgetBuilder b(*world, *layout);
	UiViewport3D vp;
	vp.root = &root;
	vp.camera = camera;
	b.viewport3d = Some(std::move(vp));
	return b;
}

WidgetBuilder UiFactory::Radio(String group, String text, bool checked) {
	WidgetBuilder b(*world, *layout);
	UiRadio r;
	r.group = std::move(group);
	r.text = std::move(text);
	r.checked = checked;
	b.radio = Some(std::move(r));
	return b;
}

WidgetBuilder UiFactory::Scrollbar(float contentSize, float viewSize, Orientation orient) {
	WidgetBuilder b(*world, *layout);
	UiScrollBar s;
	s.contentSize = contentSize;
	s.viewSize = viewSize;
	s.orient = orient;
	b.scrollbar = Some(s);
	return b;
}

WidgetBuilder UiFactory::Knob(float min, float max, float value, float step) {
	WidgetBuilder b(*world, *layout);
	UiKnob k;
	k.min = min;
	k.max = max;
	k.step = step;
	k.value = sdl3::Clamp(value, min, max);
	b.knob = Some(k);
	return b;
}

WidgetBuilder UiFactory::Canvas(std::function<void(sdl3::Renderer &, sdl3::FRect)> fn) {
	WidgetBuilder b(*world, *layout);
	b.canvas = Some(UiCanvas{std::move(fn)});
	return b;
}

WidgetBuilder UiFactory::Combo(std::vector<String> items, int selected) {
	WidgetBuilder b(*world, *layout);
	UiComboBox cb;
	cb.items = std::move(items);
	cb.selected = cb.items.empty() ? -1 : sdl3::Clamp(selected, -1, int(cb.items.size()) - 1);
	cb.itemHeight = theme.fontSize + 12.f;
	b.combo = Some(std::move(cb));
	return b;
}

WidgetBuilder UiFactory::Combo(std::vector<ComboCategory> categories, int selected) {
	WidgetBuilder b(*world, *layout);
	UiComboBox cb;
	cb.SetCategories(std::move(categories));
	cb.selected = cb.items.empty() ? -1 : sdl3::Clamp(selected, -1, int(cb.items.size()) - 1);
	cb.itemHeight = theme.fontSize + 12.f;
	b.combo = Some(std::move(cb));
	return b;
}

WidgetBuilder UiFactory::Listbox(std::vector<String> items, int selected) {
	WidgetBuilder b(*world, *layout);
	ListBox lb;
	lb.items = std::move(items);
	lb.selected = lb.items.empty() ? -1 : sdl3::Clamp(selected, -1, int(lb.items.size()) - 1);
	lb.itemHeight = theme.fontSize + 10.f;
	b.listbox = Some(std::move(lb));
	return b;
}

WidgetBuilder UiFactory::Expander(String title, bool expanded) {
	WidgetBuilder b(*world, *layout);
	UiExpander x;
	x.title = std::move(title);
	x.expanded = expanded;
	x.headerHeight = theme.fontSize + 14.f;
	b.Column().Gap(6.f);
	// Le padding haut réserve la place de l'en-tête dessiné par le widget.
	b.Pad(math::Sides{8.f, x.headerHeight + 6.f, 8.f, 8.f});
	// UiItem Auto explicite : la hauteur doit se recalculer à chaque
	// repli/dépli (sans lui, sizeVals() gèle sur la taille écrite par la
	// passe précédente dans UiRect.size).
	b.WAuto().HAuto();
	b.expander = Some(std::move(x));
	return b;
}

WidgetBuilder UiFactory::Tabview(std::vector<String> tabs, int active) {
	WidgetBuilder b(*world, *layout);
	UiTabView tv;
	tv.tabs = std::move(tabs);
	tv.active = tv.tabs.empty() ? 0 : sdl3::Clamp(active, 0, int(tv.tabs.size()) - 1);
	tv.tabHeight = theme.fontSize + 16.f;
	b.Column().Gap(6.f);
	// Le padding haut réserve la barre d'onglets dessinée par le widget.
	b.Pad(math::Sides{8.f, tv.tabHeight + 6.f, 8.f, 8.f});
	b.WAuto().HAuto(); // idem expander : recalcul au changement d'onglet
	b.tabview = Some(std::move(tv));
	return b;
}

WidgetBuilder UiFactory::Spinner(float thickness) {
	WidgetBuilder b(*world, *layout);
	UiSpinner sp;
	sp.thickness = thickness;
	b.spinner = Some(sp);
	return b;
}

WidgetBuilder UiFactory::Badge(String text) {
	WidgetBuilder b(*world, *layout);
	UiBadge bd;
	bd.text = std::move(text);
	b.badge = Some(std::move(bd));
	// Taille réduite posée en style inline (computeFonts() du layout ne
	// lit que le style inline, pas les classes — cf. systems.hpp).
	b.Style(UiStyle{}.SetFontSize(theme.fontSize * 0.85f));
	// Une pastille épouse son contenu (pas de Stretch du conteneur).
	b.AlignSelf(CrossAlign::Start);
	return b;
}

WidgetBuilder UiFactory::Popup() {
	WidgetBuilder b(*world, *layout);
	b.Fixed().Hidden();
	b.popupState = Some(UiPopupState{});
	if (theme.glassDefault)
		b.Style(UiStyle::GlassPanel(theme.panelBg));
	else
		b.Bg(theme.panelBg).BorderColor(theme.border).Radius(6.f);
	return b;
}

WidgetBuilder UiFactory::MenuPopup(int overlayOrder) {
	WidgetBuilder b(*world, *layout);
	b.Fixed().Hidden();
	b.popupState = Some(UiPopupState{});
	if (theme.glassDefault)
		b.Style(UiStyle::GlassPanel(theme.panelBg));
	else
		b.Bg(theme.combo.bgNormal.a > 0.f ? theme.combo.bgNormal : theme.fieldBg)
			.BorderColor(theme.combo.borderFocus)
			.Radius(4.f);
	b.Column().Gap(0.f).Pad(math::Sides{0.f, MENU_POPUP_PAD_Y}).WAuto().HAuto();
	b.OverlayOrder(overlayOrder);
	return b;
}

WidgetBuilder UiFactory::Modal(WidgetBuilder content) {
	WidgetBuilder wrapper(*world, *layout);
	wrapper.Fixed().Hidden();
	wrapper.W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f));
	wrapper.popupState = Some(UiPopupState{false, true, ecs::Entity{}});

	WidgetBuilder scrim(*world, *layout);
	scrim.panel = Some(UiPanel{});
	scrim.Style(UiStyle{}.SetBg(sdl3::FColor{0/255.f, 0/255.f, 0/255.f, 140/255.f}));
	scrim.Absolute().Anchor(Anchor::TopLeft);
	scrim.W(Dimension::Rpct(100.f)).H(Dimension::Rpct(100.f));

	content.Absolute().Anchor(Anchor::Center);

	wrapper.Children(std::move(scrim), std::move(content));
	return wrapper;
}

void UiFactory::RegisterRootClasses() {
	sheet.Define("root", UiStyle{}.SetFontSize(theme.fontSize).SetTextColor(theme.text).SetOpacity(1.f));
	sheet.Define("root-panel",
				 UiStyle{}.SetBg(theme.panelBg).SetBorderColor(theme.border).SetBordersRadius(8.f));
	sheet.Define("root-label", UiStyle{}.SetTextColor(theme.text));
	sheet.Define("root-button", StyleFromColors(theme.button).SetBordersRadius(6.f));
	sheet.Define("root-toggle", StyleFromColors(theme.toggle));
	sheet.Define("root-checkbox", StyleFromColors(theme.checkbox));
	sheet.Define("root-slider", StyleFromColors(theme.slider));
	sheet.Define("root-progress", UiStyle{}.SetBg(theme.track).SetBgChecked(theme.fill).SetBordersRadius(4.f));
	sheet.Define("root-separator", UiStyle{}.SetBg(theme.border));
	sheet.Define("root-input", StyleFromColors(theme.input));
	sheet.Define("root-dragvalue", StyleFromColors(theme.dragValue).SetBordersRadius(4.f));
	sheet.Define("root-colorswatch", UiStyle{}.SetBorderColor(theme.border).SetBordersRadius(4.f));
	// Réutilise la table listbox (même sémantique : normal=fond, hovered
	// /checked=surbrillance de ligne survolée/sélectionnée) — pas de
	// nouvelle table WidgetColors dédiée, Selectable/TreeNode n'ont pas
	// de bordure/fond au repos distincts d'une ListBox.
	sheet.Define("root-selectable", StyleFromColors(theme.listbox).SetBordersRadius(4.f));
	sheet.Define("root-treenode", StyleFromColors(theme.listbox).SetBordersRadius(4.f));
	sheet.Define("root-menubaritem", StyleFromColors(theme.listbox).SetBordersRadius(4.f));
	// Entrées de menu = lignes de la liste d'un combo (mêmes couleurs).
	sheet.Define("root-menuitem", StyleFromColors(theme.combo).SetBordersRadius(0.f));
	sheet.Define("root-table", StyleFromColors(theme.listbox).SetBordersRadius(4.f));
	sheet.Define("root-plot", UiStyle{}.SetBg(theme.fieldBg).SetBorderColor(theme.border).SetBordersRadius(4.f)
								   .SetBgChecked(theme.accent));
	sheet.Define("root-calendar",
				 UiStyle{}.SetBg(theme.fieldBg).SetBorderColor(theme.border).SetBordersRadius(4.f)
					 .SetBgChecked(theme.fill));
	sheet.Define("root-splitterhandle", UiStyle{}.SetBg(theme.border).SetBgHovered(theme.accent));
	sheet.Define("root-icon", UiStyle{}.SetTextColor(theme.text));
	sheet.Define("root-radio", StyleFromColors(theme.radio));
	sheet.Define("root-scrollbar", StyleFromColors(theme.scrollbar));
	sheet.Define("root-knob", StyleFromColors(theme.knob));
	sheet.Define("root-combo", StyleFromColors(theme.combo));
	sheet.Define("root-listbox", StyleFromColors(theme.listbox));
	sheet.Define("root-expander", StyleFromColors(theme.expander));
	sheet.Define("root-tabview", StyleFromColors(theme.tabs));
	sheet.Define("root-spinner", UiStyle{}.SetBg(theme.spinner));
	sheet.Define("root-badge", UiStyle{}.SetBg(theme.badgeBg).SetTextColor(theme.badgeText));
}

} // namespace ui
