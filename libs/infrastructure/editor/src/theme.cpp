// Définitions de theme.hpp et dock.hpp
#include <editor/dock.hpp>

#include <algorithm>

namespace editor {

sdl3::FColor Rgb(int r, int g, int b, int a) noexcept {
	return sdl3::FColor{float(r) / 255.f, float(g) / 255.f, float(b) / 255.f, float(a) / 255.f};
}

sdl3::Color ToColor(const sdl3::FColor &c) noexcept {
	auto channel = [](float v) { return uint8_t(sdl3::Clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
	return sdl3::Color{channel(c.r), channel(c.g), channel(c.b), 255};
}

float Luminance(const sdl3::FColor &c) noexcept { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

sdl3::FColor Mix(const sdl3::FColor &a, const sdl3::FColor &b, float t) noexcept {
	return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.f};
}

Palette PaletteOf(const ui::Palette &theme) {
	const sdl3::FColor black{0.f, 0.f, 0.f, 1.f};
	const sdl3::FColor panel{theme.panel.r, theme.panel.g, theme.panel.b, 1.f};
	Palette p;
	p.light = Luminance(panel) > 0.5f;
	// Sombre : le fond de fenêtre est un panneau assombri (panneau 40 → fond
	// 22, bandeau 30, barre d'outils 34). Clair : écarts plus faibles.
	p.base = Mix(panel, black, p.light ? 0.12f : 0.45f);
	p.header = Mix(panel, black, p.light ? 0.06f : 0.25f);
	p.toolbar = Mix(panel, black, p.light ? 0.03f : 0.15f);
	p.well = Mix(panel, black, p.light ? 0.18f : 0.65f);
	p.selection = Mix(panel, theme.accent, 0.5f);
	p.separator = Mix(panel, theme.text, 0.2f);
	p.ok = p.light ? Rgb(20, 130, 40) : Rgb(96, 200, 96);
	p.warning = p.light ? Rgb(176, 104, 0) : Rgb(236, 176, 72);
	p.error = p.light ? Rgb(200, 40, 30) : Rgb(236, 88, 72);
	return p;
}

String EditorCss(const ui::Palette &theme) {
	const Palette p = PaletteOf(theme);
	auto c = [](const sdl3::FColor &color) { return ui::CssColor(color); };
	String css = String::Format(
		":root { --ed-base: %s; --ed-header: %s; --ed-toolbar: %s; --ed-well: %s; --ed-selection: %s; "
		"--ed-separator: %s; --ed-ok: %s; --ed-warning: %s; --ed-error: %s; }\n",
		c(p.base).CStr(), c(p.header).CStr(), c(p.toolbar).CStr(), c(p.well).CStr(), c(p.selection).CStr(),
		c(p.separator).CStr(), c(p.ok).CStr(), c(p.warning).CStr(), c(p.error).CStr());
	css.Append(R"css(
/* Cadre de l'éditeur */
.editor-base { background-color: var(--ed-base); }
.editor-header { background-color: var(--ed-header); }
.editor-toolbar { background-color: var(--ed-toolbar); }
.editor-well { background-color: var(--ed-well); }
.editor-separator { background-color: var(--ed-separator); }
.selected { background-color: var(--ed-selection); }
.caption { font-size: 12px; color: var(--muted); text-overflow: ellipsis; overflow: hidden; }
/* Boutons d'icône plats */
.flat-icon { background-color: transparent; border-width: 0; border-radius: 4px; padding: 0;
             justify-content: center; align-items: center; }
.flat-icon:hover { background-color: var(--hover); }
.flat-icon:active { background-color: var(--pressed); }
.flat-icon.active { background-color: var(--ed-selection); }
/* Lignes de propriétés */
.prop-row { padding: 1px 2px; gap: 4px; align-items: center; }
.prop-label { font-size: 12px; color: var(--muted); text-overflow: ellipsis; overflow: hidden; }
.prop-field { height: 22px; font-size: 12px; }
.prop-section { font-size: 12px; font-weight: bold; padding: 6px 2px 2px 2px; }
.axis-letter { font-size: 12px; font-weight: bold; }
.color-bar { height: 20px; border-radius: 3px; border-width: 0; }
/* Panneaux à onglets */
.dock { gap: 0; padding: 0; background-color: var(--panel); }
.dock-header { gap: 0; padding: 0 4px 0 0; height: 30px; align-items: center; background-color: var(--ed-header); }
.dock-tabs { gap: 1px; padding: 0; align-items: flex-end; height: 100%; }
.dock-tab { gap: 0; padding: 0 2px; height: 27px; align-items: center; background-color: transparent; }
.dock-tab.closable { padding: 0 0 0 2px; }
.dock-tab.active { background-color: var(--panel); border-top: 2px solid var(--accent); }
.dock-tab-title { height: 24px; font-size: 13px; background-color: transparent; border-width: 0; color: var(--muted); }
.dock-tab.active .dock-tab-title { color: var(--text); }
.dock-extra { gap: 2px; padding: 0; align-items: center; }
.dock-body { gap: 0; padding: 0; overflow: hidden; }
.dock-page { gap: 4px; }
.search-field { gap: 4px; padding: 0; align-items: center; }
.search-field > input { height: 24px; font-size: 13px; }
/* Palette de commandes */
.command-palette { width: 560px; padding: 8px; gap: 6px; background-color: var(--panel); border-radius: 6px; }
.command-palette-input { height: 28px; font-size: 14px; }
.command-palette-list { gap: 1px; max-height: 360px; overflow: auto; }
.command-palette-row { padding: 3px 6px; gap: 8px; align-items: center; border-radius: 3px; background-color: transparent; }
.command-palette-row.current { background-color: var(--ed-selection); }
.command-palette-title { font-size: 13px; }
.command-palette-help { font-size: 11px; color: var(--muted); text-overflow: ellipsis; overflow: hidden; }
.command-palette-shortcut { font-size: 11px; color: var(--muted); }
)css");
	return css;
}

sdl3::FColor Readable(const ui::Palette &theme, const sdl3::FColor &color) {
	if (!PaletteOf(theme).light)
		return color;
	sdl3::FColor dark = Mix(color, sdl3::FColor{0.f, 0.f, 0.f, 1.f}, 0.45f);
	dark.a = color.a;
	return dark;
}

void SetHidden(ui::Widget widget, bool hidden) {
	if (widget && widget.IsVisible() == hidden)
		widget.SetVisible(!hidden);
}

ui::Widget IconButton(const ui::Palette &theme, ui::Widget parent, ui::MaterialIcons icon, const String &tooltip,
					  std::function<void()> action, float size, ui::Widget *outIcon, sdl3::FColor tint) {
	ui::WidgetBuilder glyph = ui::Icon(icon, size * 0.68f);
	glyph.Color(tint.a > 0.f ? Readable(theme, tint) : theme.text).PointerThrough();
	if (outIcon)
		glyph.Ref(*outIcon);
	ui::WidgetBuilder button = ui::IconButton(glyph, tooltip);
	button.Class("flat-icon").Size(size, size).OnClick(std::move(action));
	return parent.Append(button);
}

ui::Widget SearchField(const ui::Palette &theme, ui::Widget parent, const String &placeholder,
					   std::function<void(const String &)> onChange, float width) {
	ui::WidgetBuilder row = ui::Row();
	row.Class("search-field");
	if (width > 0.f)
		row.Width(width);
	else
		row.GrowWidth();
	ui::Widget input;
	row.Children({
		ui::Icon(ui::MaterialIcons::SEARCH, 15.f).Color(Readable(theme, theme.muted)).Size(17.f, 17.f).PointerThrough(),
		ui::Input(placeholder).Grow().Ref(input).OnTextChange(std::move(onChange)),
	});
	parent.Append(row);
	return input;
}

ui::Widget ResizeHandle(const ui::Palette &theme, ui::Widget parent, ui::Orientation orientation, ui::Widget target,
						bool before, float minSize, float maxSize) {
	const bool horizontal = orientation == ui::Orientation::Horizontal;
	ui::WidgetBuilder handle = ui::Row();
	handle.Padding(0.f);
	if (horizontal)
		handle.Width(5.f).GrowHeight();
	else
		handle.GrowWidth().Height(5.f);
	handle.Background(PaletteOf(theme).base);
	ui::ResizeHandle h;
	h.axis = orientation;
	h.after = !before;
	(before ? h.minBefore : h.minAfter) = minSize;
	h.maxSize = maxSize;
	handle.With(h);
	if (before)
		return parent.Append(handle);
	// La poignée PRÉCÈDE le panneau qu'elle redimensionne.
	const std::vector<ui::Widget> children = parent.Children();
	const auto at = std::find(children.begin(), children.end(), target);
	return parent.Insert(size_t(at - children.begin()), handle);
}

// ── DockPanel ────────────────────────────────────────────────────────────────

ui::Widget DockPanel::Build(ui::Widget parent, const std::function<void(ui::WidgetBuilder &)> &size) {
	// Reconstruction (changement de thème) : l'ancien arbre est déjà détruit.
	m_pages.clear();
	m_active = -1;
	ui::WidgetBuilder frame = ui::Column();
	frame.Class("dock");
	size(frame);
	frame.Children({
		ui::Row().Class("dock-header").GrowWidth().Ref(m_header).Children({
			ui::Row().Class("dock-tabs").Ref(m_tabs),
			ui::Spacer(),
			ui::Row().Class("dock-extra").Ref(m_extra),
		}),
		ui::Column().Class("dock-body").GrowWidth().GrowHeight().Ref(m_body),
	});
	m_frame = parent.Append(frame);
	m_menuButton = IconButton(m_gui.CurrentPalette(), m_header, ui::MaterialIcons::MENU, String("Menu du panneau"), [this] {
		if (!onMenu)
			return;
		const sdl3::FRect r = m_menuButton.Bounds();
		onMenu(r.x, r.y + r.h);
	}, 22.f);
	return m_frame;
}

ui::Widget DockPanel::AddPage(const String &title, bool scroll, bool closable, float pad) {
	ui::WidgetBuilder page = ui::Column();
	page.Class("dock-page").Padding(pad).GrowWidth().GrowHeight();
	if (scroll)
		page.Overflow(ui::Overflow::Auto);
	ui::Widget widget = m_body.Append(page);
	m_pages.push_back(PageInfo{title, widget, closable});
	SetActive(int(m_pages.size()) - 1);
	return widget;
}

void DockPanel::RemovePage(int index) {
	if (index < 0 || index >= int(m_pages.size()))
		return;
	m_pages[size_t(index)].content.Destroy();
	m_pages.erase(m_pages.begin() + index);
	SetActive(sdl3::Min(m_active, int(m_pages.size()) - 1));
}

void DockPanel::SetActive(int index) {
	if (m_pages.empty()) {
		m_active = -1;
		RebuildTabs();
		return;
	}
	m_active = sdl3::Clamp(index, 0, int(m_pages.size()) - 1);
	for (size_t i = 0; i < m_pages.size(); ++i)
		SetHidden(m_pages[i].content, int(i) != m_active);
	RebuildTabs();
	if (onActivate)
		onActivate(m_active);
}

void DockPanel::SetTitle(int index, const String &title) {
	if (index < 0 || index >= int(m_pages.size()) || m_pages[size_t(index)].title == title)
		return;
	m_pages[size_t(index)].title = title;
	RebuildTabs();
}

int DockPanel::Find(const String &title) const {
	for (size_t i = 0; i < m_pages.size(); ++i)
		if (m_pages[i].title == title)
			return int(i);
	return -1;
}

ui::Widget DockPanel::Page(int index) const {
	return index >= 0 && index < int(m_pages.size()) ? m_pages[size_t(index)].content : ui::Widget();
}

const String &DockPanel::Title(int index) const {
	static const String NONE_TITLE;
	return index >= 0 && index < int(m_pages.size()) ? m_pages[size_t(index)].title : NONE_TITLE;
}

void DockPanel::RebuildTabs() {
	if (!m_tabs.Valid())
		return;
	m_tabs.Clear();
	for (size_t i = 0; i < m_pages.size(); ++i) {
		const int index = int(i);
		ui::WidgetBuilder tab = ui::Row();
		tab.Class("dock-tab");
		if (int(i) == m_active)
			tab.Class("active");
		if (m_pages[i].closable)
			tab.Class("closable");
		tab.Child(ui::Button(m_pages[i].title).Class("dock-tab-title").OnClick([this, index] { SetActive(index); }));
		ui::Widget tabWidget = m_tabs.Append(tab);
		if (m_pages[i].closable)
			(void)IconButton(m_gui.CurrentPalette(), tabWidget, ui::MaterialIcons::CLOSE, String("Fermer"), [this, index] {
				if (onClose)
					onClose(index);
			}, 18.f, nullptr, m_gui.CurrentPalette().muted);
	}
}

} // namespace editor
