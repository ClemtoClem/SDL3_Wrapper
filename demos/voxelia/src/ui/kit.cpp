// Définitions de ui/kit.hpp — système de design de l'interface.
#include <voxelia/ui/kit.hpp>

#include <algorithm>
#include <cmath>

#include <sdl3/image.hpp>

namespace voxelia {

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

namespace {
const char *ToneClass(Tone tone) {
	switch (tone) {
	case Tone::PRIMARY: return "tone-primary";
	case Tone::INFO: return "tone-info";
	case Tone::SUCCESS: return "tone-success";
	case Tone::WARNING: return "tone-warning";
	case Tone::DANGER: return "tone-danger";
	case Tone::ACCENT: return "tone-accent";
	default: return "tone-neutral";
	}
}

const char *RoleClass(Text role) {
	switch (role) {
	case Text::DISPLAY: return "text-display";
	case Text::H1: return "text-h1";
	case Text::H2: return "text-h2";
	case Text::H3: return "text-h3";
	case Text::SMALL: return "text-small";
	case Text::CAPTION: return "text-caption";
	default: return "text-body";
	}
}
} // namespace

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

	// Thème de base des widgets ui:: (champs, listes, menus…) aux couleurs du jeu.
	ui::Palette t = ui::Palette::Of(ui::Theme::Dark);
	t.appBg = p.bg0;
	t.panel = p.bg1;
	t.surface = p.bg2;
	t.field = p.bg0;
	t.hover = p.bg3;
	t.pressed = p.bg1;
	t.border = p.line;
	t.borderStrong = p.lineStrong;
	t.text = p.text;
	t.muted = p.textMuted;
	t.onAccent = p.onPrimary;
	t.accent = p.primary;
	t.accentHover = p.primaryHover;
	t.accentPressed = p.primaryPressed;
	t.danger = p.danger;
	t.success = p.success;
	t.warning = p.warning;
	t.scrollThumb = p.lineStrong;
	t.scrollTrack = WithAlpha(p.bg3, 0.4f);
	t.radius = 6.f * Scale();
	t.fontSize = Size(Text::BODY);
	m_ui.SetTheme(t);

	// Feuille du système de design, régénérée à chaque changement.
	if (m_sheet)
		m_ui.RemoveStyleSheet(m_sheet);
	m_sheet = ui::StyleSheet::Parse(Css().View(), String(), String("<voxelia>"));
	m_ui.AddStyleSheet(m_sheet);
}

String Kit::Css() const {
	const Palette &p = m_palette;
	const float s = Scale();
	auto c = [](sdl3::FColor color) { return ui::CssColor(color); };
	auto a = [](sdl3::FColor color, float alpha) { return ui::CssColor(WithAlpha(color, alpha)); };
	auto px = [](float v) { return String::Format("%.1fpx", double(v)); };
	String css;
	auto add = [&css](const String &rule) {
		css.Append(rule);
		css.Append('\n');
	};
	// Variables et typographie.
	add(String::Format(":root { --vx-bg0: %s; --vx-bg1: %s; --vx-bg2: %s; --vx-bg3: %s; --vx-line: %s; "
					   "--vx-line-strong: %s; --vx-text: %s; --vx-muted: %s; --vx-dim: %s; --vx-primary: %s; "
					   "--vx-on-primary: %s; --vx-accent: %s; --vx-danger: %s; }",
					   c(p.bg0).CStr(), c(p.bg1).CStr(), c(p.bg2).CStr(), c(p.bg3).CStr(), c(p.line).CStr(),
					   c(p.lineStrong).CStr(), c(p.text).CStr(), c(p.textMuted).CStr(), c(p.textDim).CStr(),
					   c(p.primary).CStr(), c(p.onPrimary).CStr(), c(p.accent).CStr(), c(p.danger).CStr()));
	for (Text role : {Text::DISPLAY, Text::H1, Text::H2, Text::H3, Text::BODY, Text::SMALL, Text::CAPTION})
		add(String::Format(".%s { font-size: %s; }", RoleClass(role), px(Size(role)).CStr()));
	add(".text-display, .text-h1, .text-h2, .text-h3 { font-weight: bold; }");
	add(".vx-text { color: var(--vx-text); text-overflow: marquee; marquee-speed: 35; marquee-pause: 1.2; }");
	add(".vx-text.muted { color: var(--vx-muted); }");
	add(".vx-paragraph { text-overflow: wrap; color: var(--vx-muted); }");
	// Surfaces.
	add(String::Format(".vx-card { gap: %s; border-radius: %s; background-color: var(--vx-bg2); border: 1px solid "
					   "var(--vx-line); }",
					   px(10.f * s).CStr(), px(10.f * s).CStr()));
	add(String::Format(".vx-surface { gap: %s; padding: %s %s; border-radius: %s; border: 1px solid %s; }",
					   px(12.f * s).CStr(), px(20.f * s).CStr(), px(24.f * s).CStr(), px(12.f * s).CStr(),
					   a(p.lineStrong, 0.6f).CStr()));
	add(".vx-divider { height: 1px; background-color: var(--vx-line); }");
	add(String::Format(".vx-scroll { padding: 2px %s 2px 2px; }", px(10.f * s).CStr()));
	// Boutons : relief « bloc » (liseré inférieur plus épais).
	add(String::Format(".vx-button { height: %s; padding: 0 %s; border-radius: %s; font-size: %s; "
					   "background-color: var(--vx-bg2); color: var(--vx-text); border-color: var(--vx-line); "
					   "border-width: 1px 1px 3px 1px; }",
					   px(36.f * s).CStr(), px(16.f * s).CStr(), px(6.f * s).CStr(), px(Size(Text::BODY)).CStr()));
	add(".vx-button:hover { background-color: var(--vx-bg3); }");
	add(".vx-button:active { background-color: var(--vx-bg1); }");
	add(String::Format(".vx-button.tone-primary { background-color: var(--vx-primary); color: var(--vx-on-primary); "
					   "border-color: %s; font-weight: bold; }",
					   c(p.primaryPressed).CStr()));
	add(String::Format(".vx-button.tone-primary:hover { background-color: %s; }", c(p.primaryHover).CStr()));
	add(String::Format(".vx-button.tone-primary:active { background-color: %s; }", c(p.primaryPressed).CStr()));
	add(String::Format(".vx-button.tone-danger { background-color: %s; color: var(--vx-danger); border-color: %s; }",
					   a(p.danger, 0.18f).CStr(), a(p.danger, 0.7f).CStr()));
	add(String::Format(".vx-button.tone-danger:hover { background-color: %s; }", a(p.danger, 0.32f).CStr()));
	add(String::Format(".vx-button.tone-danger:active { background-color: %s; }", a(p.danger, 0.45f).CStr()));
	add(String::Format(".vx-ghost { height: %s; padding: 0 %s; border-radius: %s; font-size: %s; border-width: 0; "
					   "background-color: transparent; color: var(--vx-muted); }",
					   px(34.f * s).CStr(), px(12.f * s).CStr(), px(6.f * s).CStr(), px(Size(Text::BODY)).CStr()));
	add(String::Format(".vx-ghost:hover { background-color: %s; color: var(--vx-text); }", a(p.bg3, 0.8f).CStr()));
	add(".vx-ghost:active { background-color: var(--vx-bg2); }");
	// Grands boutons des menus.
	add(String::Format(".vx-menu-button { flex-direction: row; align-items: center; width: 1fr; height: %s; gap: %s; "
					   "padding: 0 %s; border-radius: %s; "
					   "background-color: %s; border-color: var(--vx-line); border-width: 1px 1px 3px 1px; }",
					   px(48.f * s).CStr(), px(12.f * s).CStr(), px(14.f * s).CStr(), px(8.f * s).CStr(),
					   a(p.bg2, 0.92f).CStr()));
	add(".vx-menu-button:hover { background-color: var(--vx-bg3); border-color: var(--vx-line-strong); }");
	add(".vx-menu-button:active { background-color: var(--vx-bg1); }");
	add(String::Format(".vx-menu-button.tone-primary { height: %s; background-color: var(--vx-primary); "
					   "border-color: %s; }",
					   px(58.f * s).CStr(), c(p.primaryPressed).CStr()));
	add(String::Format(".vx-menu-button.tone-primary:hover { background-color: %s; }", c(p.primaryHover).CStr()));
	add(String::Format(".vx-menu-button.tone-primary:active { background-color: %s; }", c(p.primaryPressed).CStr()));
	add(".vx-menu-title { font-weight: bold; color: var(--vx-text); }");
	add(".vx-menu-hint { color: var(--vx-dim); }");
	add(".tone-primary .vx-menu-title { color: var(--vx-on-primary); }");
	add(String::Format(".tone-primary .vx-menu-hint { color: %s; }", a(p.onPrimary, 0.75f).CStr()));
	add(String::Format(".vx-icon-button { flex-direction: row; width: %s; height: %s; justify-content: center; "
					   "align-items: center; "
					   "border-radius: %s; background-color: transparent; }",
					   px(34.f * s).CStr(), px(34.f * s).CStr(), px(6.f * s).CStr()));
	add(".vx-icon-button:hover { background-color: var(--vx-bg3); }");
	add(".vx-icon-button:active { background-color: var(--vx-bg2); }");
	// Champs et commandes.
	add(String::Format(".vx-input { height: %s; font-size: %s; border-radius: %s; padding: 0 %s; }",
					   px(36.f * s).CStr(), px(Size(Text::BODY)).CStr(), px(6.f * s).CStr(), px(10.f * s).CStr()));
	add(String::Format(".vx-switch { width: %s; height: %s; border-radius: %s; }", px(44.f * s).CStr(),
					   px(24.f * s).CStr(), px(12.f * s).CStr()));
	add(String::Format(".vx-switch .switch-thumb { width: %s; height: %s; }", px(18.f * s).CStr(),
					   px(18.f * s).CStr()));
	add(String::Format(".vx-check .checkbox-box { width: %s; height: %s; }", px(20.f * s).CStr(), px(20.f * s).CStr()));
	add(String::Format(".vx-slider { width: %s; height: %s; }", px(220.f * s).CStr(), px(22.f * s).CStr()));
	add(String::Format(".vx-combo { width: %s; height: %s; font-size: %s; }", px(220.f * s).CStr(),
					   px(32.f * s).CStr(), px(Size(Text::BODY)).CStr()));
	// Pastilles et tons.
	add(String::Format(".vx-chip { height: %s; padding: 0 %s; font-size: %s; font-weight: bold; border-radius: %s; "
					   "border-width: 1px; }",
					   px(20.f * s).CStr(), px(8.f * s).CStr(), px(Size(Text::CAPTION)).CStr(), px(10.f * s).CStr()));
	for (Tone tone : {Tone::NEUTRAL, Tone::PRIMARY, Tone::INFO, Tone::SUCCESS, Tone::WARNING, Tone::DANGER, Tone::ACCENT}) {
		const sdl3::FColor col = ToneColor(tone);
		add(String::Format(".vx-chip.%s { color: %s; background-color: %s; border-color: %s; }", ToneClass(tone),
						   c(col).CStr(), a(col, 0.16f).CStr(), a(col, 0.5f).CStr()));
		add(String::Format(".vx-icon-button.%s { color: %s; }", ToneClass(tone),
						   c(tone == Tone::NEUTRAL ? p.textMuted : col).CStr()));
	}
	// Onglets.
	add(String::Format(".vx-tabs { height: %s; gap: %s; }", px(40.f * s).CStr(), px(4.f * s).CStr()));
	add(String::Format(".vx-tab { height: %s; padding: 0 %s; font-size: %s; border-radius: 0; border-width: 0 0 3px 0; "
					   "border-color: transparent; background-color: transparent; color: var(--vx-muted); }",
					   px(38.f * s).CStr(), px(14.f * s).CStr(), px(Size(Text::BODY)).CStr()));
	add(".vx-tab:hover { background-color: var(--vx-bg3); }");
	add(String::Format(".vx-tab.on { background-color: %s; color: var(--vx-text); font-weight: bold; "
					   "border-color: var(--vx-primary); }",
					   a(p.bg2, 0.9f).CStr()));
	// Lignes de réglages.
	add(String::Format(".vx-setting { padding: %s %s; border-radius: %s; background-color: %s; gap: %s; }",
					   px(10.f * s).CStr(), px(12.f * s).CStr(), px(8.f * s).CStr(), a(p.bg2, 0.6f).CStr(),
					   px(16.f * s).CStr()));
	add(".vx-setting.locked .vx-setting-label { color: var(--vx-muted); }");
	add(".vx-setting-description { color: var(--vx-dim); }");
	add(".vx-section-title { color: var(--vx-primary); font-weight: bold; }");
	return css;
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
	WidgetBuilder b = ui::Column();
	b.Gap(gap * Scale()).Padding(0.f);
	return b;
}

WidgetBuilder Kit::Row(float gap) {
	WidgetBuilder b = ui::Row();
	b.Gap(gap * Scale()).Padding(0.f).AlignItems(ui::Align::Center);
	return b;
}

WidgetBuilder Kit::Spacer() { return ui::Spacer(); }

WidgetBuilder Kit::Card(float pad) {
	WidgetBuilder b = ui::Column();
	b.Class("vx-card").Padding(pad * Scale());
	return b;
}

WidgetBuilder Kit::Surface(float alpha) {
	WidgetBuilder b = ui::Column();
	b.Class("vx-surface").Background(WithAlpha(m_palette.bg1, alpha));
	return b;
}

WidgetBuilder Kit::Divider() {
	WidgetBuilder b = ui::Box("divider");
	b.Class("vx-divider").GrowWidth().PointerThrough();
	return b;
}

WidgetBuilder Kit::Scroll(float gap) {
	WidgetBuilder b = ui::ScrollView();
	b.Class("vx-scroll").Gap(gap * Scale());
	return b;
}

// ── Texte ────────────────────────────────────────────────────────────────────

WidgetBuilder Kit::Label(String text, Text role) {
	WidgetBuilder b = ui::Label(std::move(text));
	b.Class("vx-text").Class(RoleClass(role));
	return b;
}

WidgetBuilder Kit::Muted(String text, Text role) {
	WidgetBuilder b = Label(std::move(text), role);
	b.Class("muted");
	return b;
}

WidgetBuilder Kit::Heading(String text, Text role) { return Label(std::move(text), role); }

WidgetBuilder Kit::Paragraph(String text, Text role) {
	WidgetBuilder b = ui::Label(std::move(text));
	b.Class("vx-paragraph").Class(RoleClass(role)).GrowWidth();
	return b;
}

WidgetBuilder Kit::PixelTitle(String text, float height, sdl3::FColor color, bool centered) {
	const float scale = std::max(1.f, height * Scale() / 8.f);
	const float width = m_pixel.Ready() ? m_pixel.Measure(text, scale) : float(text.GetSize()) * 8.f * scale;
	WidgetBuilder b = ui::Canvas([this, text, scale, color, centered, width](ui::Painter &painter, const sdl3::FRect &r) {
		if (!painter.Renderer())
			return;
		const float x = centered ? r.x + (r.w - width) * 0.5f : r.x;
		m_pixel.Draw(*painter.Renderer(), text, std::floor(x), std::floor(r.y), scale, color, 1.f);
	});
	if (centered)
		b.GrowWidth();
	else
		b.Width(width + scale);
	b.Height(8.f * scale + scale).PointerThrough();
	return b;
}

// ── Commandes ────────────────────────────────────────────────────────────────

WidgetBuilder Kit::Button(String text, std::function<void()> fn, Tone tone) {
	WidgetBuilder b = ui::Button(std::move(text));
	b.Class("vx-button").Class(ToneClass(tone)).OnClick(std::move(fn));
	return b;
}

WidgetBuilder Kit::GhostButton(String text, std::function<void()> fn) {
	WidgetBuilder b = ui::Button(std::move(text));
	b.Class("vx-ghost").OnClick(std::move(fn));
	return b;
}

WidgetBuilder Kit::MenuButton(MaterialIcons icon, const String &text, const String &hint, std::function<void()> fn,
							  bool primary) {
	const Palette &p = m_palette;
	WidgetBuilder row("menu-button");
	row.Class("vx-menu-button").Class(primary ? "tone-primary" : "tone-neutral").Pressable().Focusable();
	row.OnClick(std::move(fn));
	row.Child(Icon(icon, primary ? 26.f : 22.f, Some(primary ? p.onPrimary : p.primary)).PointerThrough());
	WidgetBuilder col = Column(1.f);
	col.Grow().PointerThrough();
	col.Child(Label(text, primary ? Text::H3 : Text::BODY).Class("vx-menu-title").PointerThrough());
	if (!hint.IsEmpty())
		col.Child(Label(hint, Text::CAPTION).Class("vx-menu-hint").PointerThrough());
	row.Child(col);
	return row;
}

WidgetBuilder Kit::IconButton(MaterialIcons icon, String tooltip, std::function<void()> fn, Tone tone) {
	WidgetBuilder b("icon-button");
	b.Class("vx-icon-button").Class(ToneClass(tone)).Pressable().Focusable();
	if (!tooltip.IsEmpty())
		b.Tooltip(std::move(tooltip));
	b.OnClick(std::move(fn));
	b.Child(ui::Icon(icon, 20.f * Scale()).PointerThrough());
	return b;
}

WidgetBuilder Kit::Icon(MaterialIcons icon, float size, Option<sdl3::FColor> color) {
	WidgetBuilder b = ui::Icon(icon, size * Scale());
	b.Color(color.IsSome() ? color.Value() : m_palette.textMuted);
	return b;
}

WidgetBuilder Kit::Input(String placeholder, String value) {
	WidgetBuilder b = ui::Input(std::move(placeholder), std::move(value));
	b.Class("vx-input").GrowWidth();
	b.OnBuilt([](ui::Widget w) {
		if (auto ed = w.Get<ui::TextEditor>(); ed.IsSome())
			ed.Unwrap()->maxLength = 64;
	});
	return b;
}

WidgetBuilder Kit::Toggle(bool value, std::function<void(bool)> fn) {
	WidgetBuilder b = ui::Switch(value);
	b.Class("vx-switch").OnToggle(std::move(fn));
	return b;
}

WidgetBuilder Kit::Check(bool value, std::function<void(bool)> fn) {
	WidgetBuilder b = ui::Checkbox(String(), value);
	b.Class("vx-check").OnToggle(std::move(fn));
	return b;
}

WidgetBuilder Kit::Slider(float min, float max, float value, float step, std::function<void(float)> fn) {
	WidgetBuilder b = ui::Slider(min, max, value, step);
	b.Class("vx-slider").OnChange(std::move(fn));
	return b;
}

WidgetBuilder Kit::Combo(std::vector<String> items, int selected, std::function<void(int)> fn) {
	WidgetBuilder b = ui::Combo(std::move(items), selected);
	b.Class("vx-combo").OnSelect(std::move(fn));
	return b;
}

WidgetBuilder Kit::Chip(String text, Tone tone) {
	WidgetBuilder b = ui::Label(std::move(text));
	b.Class("vx-chip").Class(ToneClass(tone)).PointerThrough();
	return b;
}

ui::Widget Kit::Tabs(ui::Widget parent, const std::vector<String> &tabs, int active, std::function<void(int)> onSelect) {
	WidgetBuilder bar = Row(4.f);
	bar.Class("vx-tabs").GrowWidth();
	for (int i = 0; i < int(tabs.size()); ++i) {
		WidgetBuilder tab = ui::Button(tabs[size_t(i)]);
		tab.Class("vx-tab").OnClick([onSelect, i] { onSelect(i); });
		if (i == active)
			tab.Class("on");
		bar.Child(tab);
	}
	return parent.Append(bar);
}

// ── Lignes de réglages ───────────────────────────────────────────────────────

ui::Widget Kit::SettingRow(ui::Widget parent, const String &label, const String &description, WidgetBuilder control,
						   bool locked) {
	WidgetBuilder title = Row(6.f);
	if (locked)
		title.Child(Icon(MaterialIcons::LOCK, 14.f, Some(m_palette.textDim)));
	title.Child(Label(label, Text::BODY).Class("vx-setting-label"));
	WidgetBuilder text = Column(2.f);
	text.Grow().Child(title);
	if (!description.IsEmpty())
		text.Child(Paragraph(description, Text::CAPTION).Class("vx-setting-description"));
	if (locked)
		control.Disabled();
	control.Ref(m_lastControl);
	WidgetBuilder row = Row(16.f);
	row.Class("vx-setting").GrowWidth().Children({text, control});
	if (locked)
		row.Class("locked");
	return parent.Append(row);
}

ui::Widget Kit::ToggleRow(ui::Widget parent, const String &label, const String &description, bool value,
						  std::function<void(bool)> fn, bool locked) {
	return SettingRow(parent, label, description, Toggle(value, std::move(fn)), locked);
}

ui::Widget Kit::SliderRow(ui::Widget parent, const String &label, const String &description, float min, float max,
						  float value, float step, std::function<String(float)> format, std::function<void(float)> fn) {
	// La valeur affichée suit le curseur.
	auto valueLabel = std::make_shared<ui::Widget>();
	WidgetBuilder group = Row(10.f);
	group.Children({
		Label(format(value), Text::SMALL).Class("muted").Width(64.f * Scale()).TextAlign(ui::TextAlign::Right).Ref(*valueLabel),
		Slider(min, max, value, step,
			   [valueLabel, format, fn](float x) {
				   valueLabel->SetText(format(x));
				   fn(x);
			   }),
	});
	return SettingRow(parent, label, description, group);
}

ui::Widget Kit::ComboRow(ui::Widget parent, const String &label, const String &description, std::vector<String> items,
						 int selected, std::function<void(int)> fn, bool locked) {
	return SettingRow(parent, label, description, Combo(std::move(items), selected, std::move(fn)), locked);
}

ui::Widget Kit::SectionTitle(ui::Widget parent, const String &title, Option<MaterialIcons> icon) {
	WidgetBuilder row = Row(8.f);
	row.GrowWidth().Margin(math::Sides(0.f, 10.f * Scale(), 0.f, 2.f * Scale()));
	if (icon.IsSome())
		row.Child(Icon(icon.Value(), 16.f, Some(m_palette.primary)));
	// Majuscules, accents compris (ToUpper ne traite que l'ASCII).
	std::string up(title.ToUpper().CStr());
	for (auto [from, to] : {std::pair{"é", "É"}, {"è", "È"}, {"ê", "Ê"}, {"à", "À"}, {"ç", "Ç"}, {"ô", "Ô"}, {"î", "Î"}, {"û", "Û"}})
		for (size_t at = up.find(from); at != std::string::npos; at = up.find(from, at))
			up.replace(at, 2, to);
	row.Child(Label(String(up.c_str()), Text::SMALL).Class("vx-section-title"));
	row.Child(Divider());
	return parent.Append(row);
}

ui::Widget Kit::ScreenHeader(ui::Widget parent, const String &title, const String &subtitle, std::function<void()> back) {
	WidgetBuilder row = Row(12.f);
	row.GrowWidth();
	if (back)
		row.Child(IconButton(MaterialIcons::ARROW_BACK, "Retour (Échap)", back));
	WidgetBuilder col = Column(2.f);
	col.Grow().Child(Label(title, Text::H1));
	if (!subtitle.IsEmpty())
		col.Child(Muted(subtitle, Text::SMALL));
	row.Child(col);
	return parent.Append(row);
}

ui::Widget Kit::Footer(ui::Widget parent) {
	parent.Append(Divider());
	WidgetBuilder row = Row(10.f);
	row.GrowWidth().JustifyContent(ui::Justify::End);
	return parent.Append(row);
}

// ── Utilitaires ──────────────────────────────────────────────────────────────

void Kit::SetText(ui::Widget label, const String &text) {
	if (label && label.Text() != text)
		label.SetText(text);
}

void Kit::SetHidden(ui::Widget w, bool hidden) {
	if (w && w.IsVisible() == hidden)
		w.SetVisible(!hidden);
}

} // namespace voxelia
