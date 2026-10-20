// Définitions de ui/kit.hpp — système de design de l'interface.
#include <voxelgame/ui/kit.hpp>

#include <algorithm>
#include <cmath>

#include <sdl3/image.hpp>

namespace voxelgame {

using ui::Dimension;
using ui::MaterialIcons;
using ui::WidgetBuilder;

sdl3::FColor Rgb(int r, int g, int b, int a) { return sdl3::FColor{float(r) / 255.f, float(g) / 255.f, float(b) / 255.f, float(a) / 255.f}; }

sdl3::FColor WithAlpha(sdl3::FColor c, float a) {
	c.a = a;
	return c;
}

// ── Police pixel ─────────────────────────────────────────────────────────────

bool PixelFont::Load(sdl3::Renderer &renderer, const String &path) {
	auto surface = sdl3::ImgLoad(path);
	if (!surface)
		return false;
	auto rgba = surface.Value().Convert(sdl3::PixelFormat::RGBA32);
	if (!rgba)
		return false;
	SDL_Surface *s = rgba.Value().Get();
	m_cell = s->w / 16;
	// Largeur de chaque glyphe : dernière colonne non vide (+1 d'espacement).
	for (int c = 0; c < 256; ++c) {
		const int gx = (c % 16) * m_cell, gy = (c / 16) * m_cell;
		int last = -1;
		for (int x = 0; x < m_cell; ++x)
			for (int y = 0; y < m_cell; ++y) {
				const uint8_t *px = static_cast<const uint8_t *>(s->pixels) + size_t((gy + y) * s->pitch + (gx + x) * 4);
				if (px[3] > 16)
					last = std::max(last, x);
			}
		m_width[size_t(c)] = c == ' ' ? m_cell / 2 : last < 0 ? 0 : last + 2;
	}
	auto texture = sdl3::Texture::CreateFromSurface(renderer, rgba.Value());
	if (!texture)
		return false;
	(void)texture.Value().SetScaleMode(SDL_SCALEMODE_NEAREST);
	m_texture = Some(std::move(texture).Unwrap());
	return true;
}

namespace {
/// Code CP437 approché d'un caractère (accents usuels du français).
int Glyph(uint32_t cp) {
	switch (cp) {
	case 0xE9: return 0x82; // é
	case 0xE8: return 0x8A; // è
	case 0xEA: return 0x88; // ê
	case 0xE0: return 0x85; // à
	case 0xE2: return 0x83; // â
	case 0xE7: return 0x87; // ç
	case 0xF4: return 0x93; // ô
	case 0xFB: return 0x96; // û
	case 0xF9: return 0x97; // ù
	case 0xEE: return 0x8C; // î
	case 0xEF: return 0x8B; // ï
	case 0xC9: return 0x90; // É
	default: return cp < 128 ? int(cp) : '?';
	}
}

template <typename F> void ForEachCodepoint(const String &text, F &&f) {
	const char *s = text.CStr();
	for (size_t i = 0; s[i];) {
		uint32_t cp = uint8_t(s[i]);
		int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
		cp &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : extra == 1 ? 0x1F : 0x7F;
		++i;
		while (extra-- > 0 && s[i])
			cp = (cp << 6) | (uint8_t(s[i++]) & 0x3F);
		f(cp);
	}
}
} // namespace

float PixelFont::Measure(const String &text, float scale) const {
	float w = 0.f;
	ForEachCodepoint(text, [&](uint32_t cp) { w += float(m_width[size_t(Glyph(cp)) & 255]) * scale; });
	return w;
}

void PixelFont::Draw(sdl3::Renderer &renderer, const String &text, float x, float y, float scale, sdl3::FColor color,
					 float shadow) const {
	if (!m_texture.IsSome())
		return;
	const sdl3::Texture &tex = m_texture.Value();
	auto pass = [&](float ox, float oy, sdl3::FColor c) {
		(void)const_cast<sdl3::Texture &>(tex).SetColorMod(uint8_t(c.r * 255.f), uint8_t(c.g * 255.f), uint8_t(c.b * 255.f));
		(void)const_cast<sdl3::Texture &>(tex).SetAlphaMod(uint8_t(c.a * 255.f));
		float cx = x + ox;
		ForEachCodepoint(text, [&](uint32_t cp) {
			const int g = Glyph(cp) & 255;
			const sdl3::FRect src{float((g % 16) * m_cell), float((g / 16) * m_cell), float(m_cell), float(m_cell)};
			(void)renderer.Render(tex, src, sdl3::FRect{cx, y + oy, float(m_cell) * scale, float(m_cell) * scale});
			cx += float(m_width[size_t(g)]) * scale;
		});
	};
	if (shadow > 0.f)
		pass(shadow * scale, shadow * scale, sdl3::FColor{color.r * 0.25f, color.g * 0.25f, color.b * 0.25f, color.a * 0.9f});
	pass(0.f, 0.f, color);
}

// ── Thème ────────────────────────────────────────────────────────────────────

Kit::Kit(ui::Ui &ui, const Settings &settings) : m_ui(ui), m_settings(settings) { Apply(); }

void Kit::Apply() {
	Palette p;
	const bool hc = m_settings.highContrast;
	p.bg0 = hc ? Rgb(0, 0, 0) : Rgb(12, 15, 20);
	p.bg1 = hc ? Rgb(10, 10, 12) : Rgb(21, 26, 34);
	p.bg2 = hc ? Rgb(26, 26, 30) : Rgb(31, 38, 48);
	p.bg3 = hc ? Rgb(48, 48, 56) : Rgb(43, 52, 66);
	p.line = hc ? Rgb(150, 150, 160) : Rgb(52, 62, 78);
	p.lineStrong = hc ? Rgb(230, 230, 240) : Rgb(84, 99, 120);
	p.text = hc ? Rgb(255, 255, 255) : Rgb(236, 241, 246);
	p.textMuted = hc ? Rgb(220, 220, 230) : Rgb(158, 170, 186);
	p.textDim = hc ? Rgb(170, 170, 180) : Rgb(104, 117, 135);
	// Daltonisme : l'action principale reste repérable (bleu au lieu de vert
	// pour protanopie/deutéranopie), danger/succès séparés en luminosité.
	const bool redGreen = m_settings.colorBlind == 1 || m_settings.colorBlind == 2;
	p.primary = redGreen ? Rgb(74, 150, 240) : Rgb(88, 196, 86);
	p.primaryHover = redGreen ? Rgb(104, 172, 250) : Rgb(112, 214, 108);
	p.primaryPressed = redGreen ? Rgb(52, 120, 205) : Rgb(66, 160, 66);
	p.onPrimary = redGreen ? Rgb(6, 18, 36) : Rgb(9, 28, 10);
	p.secondary = Rgb(80, 164, 226);
	p.accent = m_settings.colorBlind == 3 ? Rgb(236, 120, 200) : Rgb(242, 180, 62);
	p.danger = redGreen ? Rgb(236, 120, 40) : Rgb(229, 72, 77);
	p.warning = Rgb(245, 166, 36);
	p.success = redGreen ? Rgb(90, 170, 245) : Rgb(70, 196, 106);
	p.info = Rgb(80, 164, 226);
	p.scrim = Rgb(4, 6, 10, 170);
	p.health = Rgb(232, 67, 75);
	p.hunger = Rgb(217, 138, 58);
	p.stamina = Rgb(70, 180, 230);
	p.xp = Rgb(140, 224, 75);
	p.air = Rgb(127, 211, 255);
	p.armor = Rgb(190, 200, 214);
	m_palette = p;

	ui::UiTheme t = ui::UiTheme::Dark();
	t.fontSize = Size(Text::BODY);
	t.panelBg = p.bg1;
	t.text = p.text;
	t.muted = p.textMuted;
	t.track = p.bg3;
	t.fill = p.primary;
	t.thumb = p.text;
	t.border = p.line;
	t.fieldBg = p.bg0;
	t.accent = p.primary;
	const sdl3::FColor none{};
	t.button = ui::MakeWidgetColors(p.bg2, p.bg3, p.bg1, p.primary, p.line, p.primary, p.text);
	t.toggle = ui::MakeWidgetColors(p.bg3, p.lineStrong, p.bg3, p.primary, none, none, p.text);
	t.checkbox = t.toggle;
	t.slider = ui::MakeWidgetColors(p.bg3, p.text, p.primary, p.primary, none, none, p.text);
	t.input = ui::MakeWidgetColors(p.bg0, p.textDim, p.primary, p.text, p.line, p.primary, p.text);
	t.dragValue = ui::MakeWidgetColors(p.bg0, p.bg3, p.primary, p.primary, p.line, p.primary, p.text);
	t.radio = t.toggle;
	t.scrollbar = ui::MakeWidgetColors(WithAlpha(p.bg3, 0.4f), p.lineStrong, p.primary, p.lineStrong, none, none, p.text);
	t.knob = t.toggle;
	t.combo = ui::MakeWidgetColors(p.bg0, p.bg3, p.bg3, WithAlpha(p.primary, 0.35f), p.line, p.primary, p.text);
	t.listbox = ui::MakeWidgetColors(p.bg0, p.bg3, p.lineStrong, WithAlpha(p.primary, 0.35f), p.line, p.primary, p.text);
	t.expander = ui::MakeWidgetColors(p.bg2, p.bg3, p.bg2, p.bg2, none, none, p.text);
	t.tabs = ui::MakeWidgetColors(p.bg1, p.bg3, p.bg2, p.bg2, none, p.primary, p.text);
	t.spinner = p.primary;
	t.badgeBg = p.danger;
	t.badgeText = Rgb(255, 255, 255);
	m_ui.SetTheme(t);
}

float Kit::Size(Text role) const {
	float px = 14.f;
	switch (role) {
	case Text::DISPLAY: px = 34.f; break;
	case Text::H1: px = 26.f; break;
	case Text::H2: px = 20.f; break;
	case Text::H3: px = 16.f; break;
	case Text::BODY: px = 14.f; break;
	case Text::SMALL: px = 12.5f; break;
	case Text::CAPTION: px = 11.f; break;
	}
	return px * m_settings.textScale * m_settings.uiScale;
}

sdl3::FColor Kit::ToneColor(Tone tone) const {
	switch (tone) {
	case Tone::PRIMARY: return m_palette.primary;
	case Tone::INFO: return m_palette.info;
	case Tone::SUCCESS: return m_palette.success;
	case Tone::WARNING: return m_palette.warning;
	case Tone::DANGER: return m_palette.danger;
	case Tone::ACCENT: return m_palette.accent;
	default: return m_palette.textMuted;
	}
}

// ── Mise en page ─────────────────────────────────────────────────────────────

WidgetBuilder Kit::Column(float gap) {
	WidgetBuilder b = Factory().Column();
	b.Gap(gap * Scale()).Pad(0.f);
	return b;
}

WidgetBuilder Kit::Row(float gap) {
	WidgetBuilder b = Factory().Row();
	b.Gap(gap * Scale()).Pad(0.f).Align(ui::CrossAlign::Center);
	return b;
}

WidgetBuilder Kit::Spacer() {
	WidgetBuilder b = Factory().Column();
	b.GrowW().H(Dimension::Px(1.f)).PointerThrough();
	return b;
}

WidgetBuilder Kit::Card(float pad) {
	WidgetBuilder b = Factory().Column();
	b.Gap(10.f * Scale()).Pad(pad * Scale()).Radius(10.f * Scale()).Bg(m_palette.bg2).BorderColor(m_palette.line, 1.f);
	return b;
}

WidgetBuilder Kit::Surface(float alpha) {
	WidgetBuilder b = Factory().Column();
	b.Gap(12.f * Scale()).Pad(math::Sides(24.f * Scale(), 20.f * Scale())).Radius(12.f * Scale()).Bg(WithAlpha(m_palette.bg1, alpha));
	b.BorderColor(WithAlpha(m_palette.lineStrong, 0.6f), 1.f);
	return b;
}

WidgetBuilder Kit::Divider() {
	WidgetBuilder b = Factory().Column();
	b.GrowW().H(Dimension::Px(1.f)).Bg(m_palette.line).PointerThrough();
	return b;
}

WidgetBuilder Kit::Scroll(float gap) {
	WidgetBuilder b = Factory().Column();
	b.Gap(gap * Scale()).Pad(math::Sides(2.f, 2.f, 10.f * Scale(), 2.f)).Scrollable().Clip();
	return b;
}

// ── Texte ────────────────────────────────────────────────────────────────────

WidgetBuilder Kit::Label(String text, Text role) {
	WidgetBuilder b = Factory().Label(std::move(text));
	b.WAuto().HAuto().FontSize(Size(role)).TextColor(m_palette.text).TextEllipsis();
	if (role == Text::H1 || role == Text::H2 || role == Text::H3 || role == Text::DISPLAY)
		b.Bold();
	return b;
}

WidgetBuilder Kit::Muted(String text, Text role) {
	WidgetBuilder b = Label(std::move(text), role);
	b.TextColor(m_palette.textMuted);
	return b;
}

WidgetBuilder Kit::Heading(String text, Text role) { return Label(std::move(text), role); }

WidgetBuilder Kit::Paragraph(String text, Text role) {
	WidgetBuilder b = Label(std::move(text), role);
	b.GrowW().TextWrap().TextColor(m_palette.textMuted);
	return b;
}

WidgetBuilder Kit::PixelTitle(String text, float height, sdl3::FColor color, bool centered) {
	const float scale = std::max(1.f, height * Scale() / 8.f);
	const float width = m_pixel.Ready() ? m_pixel.Measure(text, scale) : float(text.GetSize()) * 8.f * scale;
	WidgetBuilder b = Factory().Canvas([this, text, scale, color, centered, width](sdl3::Renderer &ren, sdl3::FRect r) {
		const float x = centered ? r.x + (r.w - width) * 0.5f : r.x;
		m_pixel.Draw(ren, text, std::floor(x), std::floor(r.y), scale, color, 1.f);
	});
	if (centered)
		b.GrowW();
	else
		b.W(Dimension::Px(width + scale));
	b.H(Dimension::Px(8.f * scale + scale)).PointerThrough();
	return b;
}

// ── Commandes ────────────────────────────────────────────────────────────────

WidgetBuilder Kit::Button(String text, std::function<void()> fn, Tone tone) {
	const float textWidth = m_ui.Layout().measureText ? m_ui.Layout().measureText(text, Size(Text::BODY)).x : float(text.GetSize()) * 8.f;
	WidgetBuilder b = Factory().Button(std::move(text));
	b.W(Dimension::Px(textWidth + 34.f * Scale())).H(Dimension::Px(36.f * Scale())).Pad(math::Sides(16.f * Scale(), 0.f)).FontSize(Size(Text::BODY)).Radius(6.f * Scale());
	ui::UiStyle s;
	const Palette &p = m_palette;
	if (tone == Tone::PRIMARY) {
		s.SetBg(p.primary).SetBgHovered(p.primaryHover).SetBgPressed(p.primaryPressed).SetTextColor(p.onPrimary);
		s.SetBorderColor(p.primaryPressed).SetBold(true);
	} else if (tone == Tone::DANGER) {
		s.SetBg(WithAlpha(p.danger, 0.18f)).SetBgHovered(WithAlpha(p.danger, 0.32f)).SetBgPressed(WithAlpha(p.danger, 0.45f));
		s.SetTextColor(p.danger).SetBorderColor(WithAlpha(p.danger, 0.7f));
	} else {
		s.SetBg(p.bg2).SetBgHovered(p.bg3).SetBgPressed(p.bg1).SetTextColor(p.text).SetBorderColor(p.line);
	}
	s.SetBordersWidth(math::Sides(1.f, 1.f, 1.f, 3.f)); // liseré inférieur : relief « bloc »
	b.Style(std::move(s));
	b.OnClick(std::move(fn));
	return b;
}

WidgetBuilder Kit::GhostButton(String text, std::function<void()> fn) {
	const float textWidth = m_ui.Layout().measureText ? m_ui.Layout().measureText(text, Size(Text::BODY)).x : float(text.GetSize()) * 8.f;
	WidgetBuilder b = Factory().Button(std::move(text));
	b.W(Dimension::Px(textWidth + 26.f * Scale())).H(Dimension::Px(34.f * Scale())).Pad(math::Sides(12.f * Scale(), 0.f)).FontSize(Size(Text::BODY)).Radius(6.f * Scale());
	ui::UiStyle s;
	s.SetBg(WithAlpha(m_palette.bg3, 0.f)).SetBgHovered(WithAlpha(m_palette.bg3, 0.8f)).SetBgPressed(m_palette.bg2);
	s.SetTextColor(m_palette.textMuted).SetTextHovered(m_palette.text).SetBordersWidth(0.f);
	b.Style(std::move(s));
	b.OnClick(std::move(fn));
	return b;
}

ecs::Entity Kit::MenuButton(ecs::Entity parent, MaterialIcons icon, const String &text, const String &hint, std::function<void()> fn,
							bool primary) {
	const Palette &p = m_palette;
	WidgetBuilder row = Factory().Row();
	row.GrowW().H(Dimension::Px((primary ? 58.f : 48.f) * Scale())).Gap(12.f * Scale()).Pad(math::Sides(14.f * Scale(), 0.f));
	row.Align(ui::CrossAlign::Center).Radius(8.f * Scale()).OnClick(std::move(fn)).Parent(parent);
	
	ui::UiStyle s;
	if (primary)
		s.SetBg(p.primary).SetBgHovered(p.primaryHover).SetBgPressed(p.primaryPressed).SetBorderColor(p.primaryPressed);
	else
		s.SetBg(WithAlpha(p.bg2, 0.92f)).SetBgHovered(p.bg3).SetBgPressed(p.bg1).SetBorderColor(p.line).SetBorderHovered(p.lineStrong);
	s.SetBordersWidth(math::Sides(1.f, 1.f, 1.f, 3.f));
	row.Bg(sdl3::FColor{}).Style(std::move(s));
	const ecs::Entity e = row.Spawn();
	const sdl3::FColor fg = primary ? p.onPrimary : p.text;
	Icon(icon, (primary ? 26.f : 22.f), Some(primary ? p.onPrimary : p.primary)).PointerThrough().Parent(e).Spawn();
	WidgetBuilder col = Column(1.f);
	col.GrowW().PointerThrough().Parent(e);
	const ecs::Entity c = col.Spawn();
	WidgetBuilder title = Label(text, primary ? Text::H3 : Text::BODY);
	title.TextColor(fg).Bold().PointerThrough().Parent(c).Spawn();
	if (!hint.IsEmpty()) {
		WidgetBuilder h = Muted(hint, Text::CAPTION);
		h.TextColor(primary ? WithAlpha(p.onPrimary, 0.75f) : p.textDim).PointerThrough().Parent(c).Spawn();
	}
	return e;
}

WidgetBuilder Kit::IconButton(MaterialIcons icon, String tooltip, std::function<void()> fn, Tone tone) {
	const float size = 34.f * Scale();
	WidgetBuilder b = Factory().Row();
	b.Size(size, size).Justify(ui::Justify::Center).Align(ui::CrossAlign::Center).Radius(6.f * Scale());
	ui::UiStyle s;
	const sdl3::FColor fg = tone == Tone::NEUTRAL ? m_palette.textMuted : ToneColor(tone);
	s.SetBg(WithAlpha(m_palette.bg3, 0.f)).SetBgHovered(m_palette.bg3).SetBgPressed(m_palette.bg2).SetTextColor(fg);
	b.Bg(sdl3::FColor{}).Style(std::move(s));
	if (!tooltip.IsEmpty())
		b.Tooltip(std::move(tooltip));
	b.OnClick(std::move(fn));
	b.Children(Icon(icon, 20.f, Some(fg)).PointerThrough());
	return b;
}

WidgetBuilder Kit::Icon(MaterialIcons icon, float size, Option<sdl3::FColor> color) {
	WidgetBuilder b = Factory().Icon(icon, size * Scale());
	b.TextColor(color.IsSome() ? color.Value() : m_palette.textMuted);
	return b;
}

WidgetBuilder Kit::Input(String placeholder, String value) {
	WidgetBuilder b = Factory().Input(std::move(placeholder));
	b.GrowW().H(Dimension::Px(36.f * Scale())).FontSize(Size(Text::BODY)).Radius(6.f * Scale()).Pad(math::Sides(10.f * Scale(), 0.f));
	b.MaxLen(64);
	(void)value; // posé après création : SetInputText
	return b;
}

WidgetBuilder Kit::Toggle(bool value, std::function<void(bool)> fn) {
	WidgetBuilder b = Factory().Toggle(value);
	b.Size(44.f * Scale(), 24.f * Scale()).OnToggle(std::move(fn));
	return b;
}

WidgetBuilder Kit::Check(bool value, std::function<void(bool)> fn) {
	WidgetBuilder b = Factory().Checkbox(value);
	b.Size(20.f * Scale(), 20.f * Scale()).OnToggle(std::move(fn));
	return b;
}

WidgetBuilder Kit::Slider(float min, float max, float value, float step, std::function<void(float)> fn) {
	WidgetBuilder b = Factory().Slider(min, max, value, step);
	b.W(Dimension::Px(220.f * Scale())).H(Dimension::Px(22.f * Scale())).OnChange(std::move(fn));
	return b;
}

WidgetBuilder Kit::Combo(std::vector<String> items, int selected, std::function<void(int)> fn) {
	WidgetBuilder b = Factory().Combo(std::move(items), selected);
	b.W(Dimension::Px(220.f * Scale())).H(Dimension::Px(32.f * Scale())).FontSize(Size(Text::BODY));
	b.OnChange([fn = std::move(fn)](float v) { fn(int(v)); });
	return b;
}

WidgetBuilder Kit::Chip(String text, Tone tone) {
	const sdl3::FColor c = ToneColor(tone);
	WidgetBuilder b = Factory().Label(std::move(text));
	b.WAuto().H(Dimension::Px(20.f * Scale())).Pad(math::Sides(8.f * Scale(), 0.f)).FontSize(Size(Text::CAPTION)).Bold();
	b.TextColor(c).Bg(WithAlpha(c, 0.16f)).BorderColor(WithAlpha(c, 0.5f), 1.f).Radius(10.f * Scale()).PointerThrough();
	return b;
}

ecs::Entity Kit::Tabs(ecs::Entity parent, const std::vector<String> &tabs, int active, std::function<void(int)> onSelect) {
	WidgetBuilder bar = Row(4.f);
	bar.GrowW().H(Dimension::Px(40.f * Scale())).Parent(parent);
	const ecs::Entity row = bar.Spawn();
	for (int i = 0; i < int(tabs.size()); ++i) {
		const float tw = m_ui.Layout().measureText ? m_ui.Layout().measureText(tabs[size_t(i)], Size(Text::BODY)).x : 80.f;
		WidgetBuilder tab = Factory().Button(tabs[size_t(i)]);
		tab.W(Dimension::Px(tw + 30.f * Scale())).H(Dimension::Px(38.f * Scale())).Pad(math::Sides(14.f * Scale(), 0.f)).FontSize(Size(Text::BODY)).Radius(0.f);
		ui::UiStyle s;
		const bool on = i == active;
		s.SetBg(WithAlpha(m_palette.bg2, on ? 0.9f : 0.f)).SetBgHovered(m_palette.bg3).SetBgPressed(m_palette.bg2);
		s.SetTextColor(on ? m_palette.text : m_palette.textMuted).SetBold(on);
		s.SetBorderColor(on ? m_palette.primary : WithAlpha(m_palette.line, 0.f)).SetBordersWidth(math::Sides(0.f, 0.f, 0.f, 3.f));
		tab.Style(std::move(s)).OnClick([onSelect, i] { onSelect(i); }).Parent(row).Spawn();
	}
	return row;
}

// ── Lignes de réglages ───────────────────────────────────────────────────────

ecs::Entity Kit::SettingRow(ecs::Entity parent, const String &label, const String &description, WidgetBuilder control, bool locked) {
	WidgetBuilder row = Row(16.f);
	row.GrowW().Pad(math::Sides(12.f * Scale(), 10.f * Scale())).Radius(8.f * Scale()).Bg(WithAlpha(m_palette.bg2, 0.6f)).Parent(parent);
	const ecs::Entity e = row.Spawn();
	WidgetBuilder text = Column(2.f);
	text.GrowW().Parent(e);
	const ecs::Entity t = text.Spawn();
	WidgetBuilder title = Row(6.f);
	title.Parent(t);
	const ecs::Entity tr = title.Spawn();
	if (locked)
		Icon(MaterialIcons::LOCK, 14.f, Some(m_palette.textDim)).Parent(tr).Spawn();
	Label(label, Text::BODY).TextColor(locked ? m_palette.textMuted : m_palette.text).Parent(tr).Spawn();
	if (!description.IsEmpty())
		Paragraph(description, Text::CAPTION).TextColor(m_palette.textDim).Parent(t).Spawn();
	if (locked)
		control.Disabled();
	m_lastControl = control.Parent(e).Spawn();
	return e;
}

ecs::Entity Kit::ToggleRow(ecs::Entity parent, const String &label, const String &description, bool value, std::function<void(bool)> fn,
						   bool locked) {
	return SettingRow(parent, label, description, Toggle(value, std::move(fn)), locked);
}

ecs::Entity Kit::SliderRow(ecs::Entity parent, const String &label, const String &description, float min, float max, float value,
						   float step, std::function<String(float)> format, std::function<void(float)> fn) {
	WidgetBuilder group = Row(10.f);
	const ecs::Entity row = SettingRow(parent, label, description, std::move(group));
	// La valeur affichée suit le curseur.
	const ecs::Entity groupEntity = m_lastControl;
	WidgetBuilder valueLabel = Label(format(value), Text::SMALL);
	valueLabel.W(Dimension::Px(64.f * Scale())).TextAlign(ui::TextAlign::Right).TextColor(m_palette.textMuted).Parent(groupEntity);
	const ecs::Entity v = valueLabel.Spawn();
	Slider(min, max, value, step, [this, v, format, fn](float x) {
		SetText(v, format(x));
		fn(x);
	}).Parent(groupEntity).Spawn();
	return row;
}

ecs::Entity Kit::ComboRow(ecs::Entity parent, const String &label, const String &description, std::vector<String> items, int selected,
						  std::function<void(int)> fn, bool locked) {
	return SettingRow(parent, label, description, Combo(std::move(items), selected, std::move(fn)), locked);
}

ecs::Entity Kit::SectionTitle(ecs::Entity parent, const String &title, Option<MaterialIcons> icon) {
	WidgetBuilder row = Row(8.f);
	row.GrowW().Margin(math::Sides(0.f, 10.f * Scale(), 0.f, 2.f * Scale())).Parent(parent);
	const ecs::Entity e = row.Spawn();
	if (icon.IsSome())
		Icon(icon.Value(), 16.f, Some(m_palette.primary)).Parent(e).Spawn();
	// Majuscules, accents compris (ToUpper ne traite que l'ASCII).
	std::string up(title.ToUpper().CStr());
	for (auto [from, to] : {std::pair{"é", "É"}, {"è", "È"}, {"ê", "Ê"}, {"à", "À"}, {"ç", "Ç"}, {"ô", "Ô"}, {"î", "Î"}, {"û", "Û"}})
		for (size_t at = up.find(from); at != std::string::npos; at = up.find(from, at))
			up.replace(at, 2, to);
	Label(String(up.c_str()), Text::SMALL).TextColor(m_palette.primary).Bold().Parent(e).Spawn();
	Divider().Parent(e).Spawn();
	return e;
}

ecs::Entity Kit::ScreenHeader(ecs::Entity parent, const String &title, const String &subtitle, std::function<void()> back) {
	WidgetBuilder row = Row(12.f);
	row.GrowW().Parent(parent);
	const ecs::Entity e = row.Spawn();
	if (back)
		IconButton(MaterialIcons::ARROW_BACK, "Retour (Échap)", back).Parent(e).Spawn();
	WidgetBuilder col = Column(2.f);
	col.GrowW().Parent(e);
	const ecs::Entity c = col.Spawn();
	Label(title, Text::H1).Parent(c).Spawn();
	if (!subtitle.IsEmpty())
		Muted(subtitle, Text::SMALL).Parent(c).Spawn();
	return e;
}

ecs::Entity Kit::Footer(ecs::Entity parent) {
	Divider().Parent(parent).Spawn();
	WidgetBuilder row = Row(10.f);
	row.GrowW().Justify(ui::Justify::End).Parent(parent);
	return row.Spawn();
}

// ── Utilitaires ──────────────────────────────────────────────────────────────

void Kit::SetText(ecs::Entity label, const String &text) {
	if (auto l = m_ui.World().GetComponent<ui::UiLabel>(label); l.IsSome() && l.Unwrap()->text != text) {
		l.Unwrap()->text = text;
		m_ui.Layout().MarkDirty();
	}
	if (auto b = m_ui.World().GetComponent<ui::UiButton>(label); b.IsSome() && b.Unwrap()->text != text) {
		b.Unwrap()->text = text;
		m_ui.Layout().MarkDirty();
	}
}

void Kit::SetHidden(ecs::Entity e, bool hidden) {
	if (!m_ui.World().IsAlive(e))
		return;
	if (hidden && !m_ui.World().HasComponent<ui::UiHidden>(e))
		m_ui.World().AddComponent(e, ui::UiHidden{});
	else if (!hidden && m_ui.World().HasComponent<ui::UiHidden>(e))
		m_ui.World().RemoveComponent<ui::UiHidden>(e);
	m_ui.Layout().MarkDirty();
}

void Kit::SetInputText(ecs::Entity input, const String &text) {
	if (auto i = m_ui.World().GetComponent<ui::UiInput>(input); i.IsSome()) {
		i.Unwrap()->text = text;
		i.Unwrap()->cursor = text.GetSize();
		i.Unwrap()->selectionAnchor = text.GetSize();
		m_ui.Layout().MarkDirty();
	}
}

String Kit::InputText(ecs::Entity input) {
	if (auto i = m_ui.World().GetComponent<ui::UiInput>(input); i.IsSome())
		return i.Unwrap()->text;
	return String();
}

void Kit::Destroy(ecs::Entity e) {
	if (m_ui.World().IsAlive(e)) {
		ui::DespawnTree(m_ui.World(), e);
		m_ui.Layout().MarkDirty();
	}
}

void Kit::ClearChildren(ecs::Entity e) {
	std::vector<ecs::Entity> children;
	m_ui.World().Query<ui::UiParent>([&](ecs::Entity c, ui::UiParent &p) {
		if (p.parent == e)
			children.push_back(c);
	});
	for (ecs::Entity c : children)
		if (m_ui.World().IsAlive(c))
			ui::DespawnTree(m_ui.World(), c);
	m_ui.Layout().MarkDirty();
}

void Kit::SetColor(ecs::Entity e, sdl3::FColor color) {
	ui::UiStyle style;
	if (auto st = m_ui.World().GetComponent<ui::UiStyle>(e); st.IsSome())
		style = *st.Unwrap();
	style.SetTextColor(color);
	ui::SetInlineStyle(m_ui.World(), e, std::move(style));
}

} // namespace voxelgame
