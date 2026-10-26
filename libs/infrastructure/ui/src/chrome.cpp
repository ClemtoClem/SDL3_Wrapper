// Définitions de ui/chrome.hpp
#include <ui/chrome.hpp>
#include <ui/context.hpp>

namespace ui {

// ── Boutons, barre de titre ──────────────────────────────────────────────────

WidgetBuilder ChromeButton(MaterialIcons glyph, String fallback, bool icons, String tooltip) {
	WidgetBuilder b = icons ? IconButton(Icon(glyph, 18.f), std::move(tooltip)) : Button(std::move(fallback));
	b.Class("titlebar-button");
	if (!icons && !tooltip.IsEmpty())
		b.Tooltip(std::move(tooltip));
	return b;
}

WidgetBuilder TitleBar(sdl3::Window &window, TitleBarOptions options) {
	WidgetBuilder bar("titlebar");
	bar.Id("titlebar");
	if (!options.fillBackground)
		bar.Class("transparent");
	if (options.appIcon.IsSome() && options.icons)
		bar.Child(Icon(options.appIcon.Value(), 20.f).Class("titlebar-icon"));
	bar.Child(Label(options.title).Class("titlebar-title"));
	if (options.moveHandle)
		bar.Child((options.icons ? Icon(MaterialIcons::OPEN_WITH, 18.f) : Label(String("\xe2\x9c\xa5"))) // ✥
					  .Class("titlebar-move")
					  .Tooltip("Glisser la barre de titre pour déplacer la fenêtre"));
	bar.Child(Spacer());
	sdl3::Window *win = &window;
	if (options.minimizeButton)
		bar.Child(ChromeButton(MaterialIcons::MINIMIZE, String("\xe2\x80\x94"), options.icons, "Réduire")
					  .Id("titlebar-minimize")
					  .OnClick([win](WidgetEvent) { win->Minimize(); }));
	if (options.maximizeButton)
		bar.Child(ChromeButton(MaterialIcons::CROP_SQUARE, String("\xe2\x96\xa1"), options.icons,
							   "Agrandir / restaurer")
					  .Id("titlebar-maximize")
					  .OnClick([win](WidgetEvent) {
						  if (win->IsMaximized())
							  win->Restore();
						  else
							  win->Maximize();
					  }));
	bar.Child(ChromeButton(MaterialIcons::CLOSE, String("\xc3\x97"), options.icons, "Fermer")
				  .Id("titlebar-close")
				  .OnClick([win, onClose = std::move(options.onClose)](WidgetEvent) {
					  if (onClose)
						  onClose();
					  else
						  win->Hide();
				  }));
	return bar;
}

namespace {
Widget FindOr(Widget root, StringView selector) {
	auto w = root.Find(selector);
	return w.IsSome() ? w.Value() : Widget();
}
} // namespace

TitleBarWidgets TitleBarWidgets::From(Widget bar) {
	TitleBarWidgets tb;
	tb.root = FindOr(bar, "titlebar");
	if (!tb.root)
		return tb;
	tb.appIcon = FindOr(tb.root, ".titlebar-icon");
	tb.title = FindOr(tb.root, ".titlebar-title");
	tb.moveHandle = FindOr(tb.root, ".titlebar-move");
	tb.minimizeBtn = FindOr(tb.root, "#titlebar-minimize");
	tb.maximizeBtn = FindOr(tb.root, "#titlebar-maximize");
	tb.closeBtn = FindOr(tb.root, "#titlebar-close");
	return tb;
}

std::vector<Widget> TitleBarWidgets::Buttons() const {
	std::vector<Widget> out;
	for (const Widget &w : {minimizeBtn, maximizeBtn, closeBtn})
		if (w)
			out.push_back(w);
	return out;
}

void TitleBarWidgets::Update(const sdl3::Window &window) const {
	if (!maximizeBtn)
		return;
	auto icon = maximizeBtn.Find("icon");
	if (icon.IsNone())
		return;
	Widget iconWidget = icon.Value();
	auto content = iconWidget.Get<IconContent>();
	if (content.IsNone())
		return;
	const String glyph =
		Glyphs::ToUtf8(window.IsMaximized() ? MaterialIcons::FILTER_NONE : MaterialIcons::CROP_SQUARE);
	if (content.Unwrap()->glyph != glyph) {
		content.Unwrap()->glyph = glyph;
		MarkLayoutDirty(*iconWidget.World());
	}
}

void TitleBarWidgets::SetTitle(String text) const {
	if (Widget t = title; t)
		t.SetText(std::move(text));
}

// ── Barre d'état ─────────────────────────────────────────────────────────────

WidgetBuilder StatusBar(StatusBarOptions options) {
	WidgetBuilder bar("statusbar");
	bar.Id("statusbar");
	bar.Child(Label(options.text).Class("statusbar-text"));
	if (options.resizeGrip)
		bar.Child((options.icons ? Icon(MaterialIcons::SOUTH_EAST, 18.f) : Label(String("\xe2\x87\xb2"))) // ⇲
					  .Class("statusbar-grip")
					  .Tooltip("Glisser pour redimensionner la fenêtre"));
	return bar;
}

StatusBarWidgets StatusBarWidgets::From(Widget bar) {
	StatusBarWidgets sb;
	sb.root = FindOr(bar, "statusbar");
	if (!sb.root)
		return sb;
	sb.label = FindOr(sb.root, ".statusbar-text");
	sb.grip = FindOr(sb.root, ".statusbar-grip");
	return sb;
}

void StatusBarWidgets::SetText(String text, Option<Color> color) const {
	Widget l = label;
	if (!l)
		return;
	l.SetText(std::move(text));
	if (color.IsSome())
		l.Color(color.Value());
}

// ── WindowChrome ─────────────────────────────────────────────────────────────

void WindowChrome::Attach(sdl3::Window &window, Widget titleBar, std::vector<Widget> buttons, bool resizable) {
	m_window = &window;
	m_titleBar = titleBar;
	m_buttons = std::move(buttons);
	m_cache.resizable = resizable;
	Rebuild(true);
	window.SetHitTest([this](const SDL_Point &p) { return HitTest(p); });
}

void WindowChrome::Attach(sdl3::Window &window, const TitleBarWidgets &titleBar, const StatusBarWidgets *statusBar,
						  bool resizable) {
	Attach(window, titleBar.root, titleBar.Buttons(), resizable);
	if (statusBar && statusBar->grip)
		AddResizeGrip(statusBar->grip);
}

void WindowChrome::AddResizeGrip(Widget grip) {
	m_grips.push_back(grip);
	Rebuild(true);
}

void WindowChrome::Detach() {
	if (m_window)
		m_window->SetHitTest(nullptr);
	m_window = nullptr;
}

void WindowChrome::Update() { Rebuild(false); }

void WindowChrome::Rebuild(bool force) {
	if (!m_titleBar.World())
		return;
	Context *ctx = ContextOf(*m_titleBar.World());
	const uint64_t pass = ctx ? ctx->layoutPass : 0;
	if (!force && pass == m_lastPass)
		return;
	m_lastPass = pass;
	m_cache.titleBar = m_titleBar ? m_titleBar.Bounds() : sdl3::FRect{};
	m_cache.excluded.clear();
	for (const Widget &b : m_buttons)
		if (b)
			m_cache.excluded.push_back(b.Bounds());
	m_cache.grips.clear();
	for (const Widget &g : m_grips)
		if (g)
			m_cache.grips.push_back(g.Bounds());
	const sdl3::Point size = m_window ? m_window->GetSize() : sdl3::Point{0, 0};
	// Sans taille de fenêtre (pas encore affichée) : celle de l'interface.
	m_cache.w = size.x > 0 ? float(size.x) : (ctx ? ctx->viewport.x : 0.f);
	m_cache.h = size.y > 0 ? float(size.y) : (ctx ? ctx->viewport.y : 0.f);
}

SDL_HitTestResult WindowChrome::HitTest(const SDL_Point &p) const {
	const sdl3::FPoint fp{float(p.x), float(p.y)};
	for (const sdl3::FRect &r : m_cache.excluded)
		if (r.Contains(fp))
			return SDL_HITTEST_NORMAL;
	if (m_cache.resizable) {
		for (const sdl3::FRect &r : m_cache.grips)
			if (r.Contains(fp))
				return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
		// Coins avant bords (un coin est aussi dans ses deux bandes).
		const float rb = m_cache.resizeBorder;
		const bool left = fp.x < rb, right = fp.x > m_cache.w - rb;
		const bool top = fp.y < rb, bottom = fp.y > m_cache.h - rb;
		if (top && left)
			return SDL_HITTEST_RESIZE_TOPLEFT;
		if (top && right)
			return SDL_HITTEST_RESIZE_TOPRIGHT;
		if (bottom && left)
			return SDL_HITTEST_RESIZE_BOTTOMLEFT;
		if (bottom && right)
			return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
		if (top)
			return SDL_HITTEST_RESIZE_TOP;
		if (bottom)
			return SDL_HITTEST_RESIZE_BOTTOM;
		if (left)
			return SDL_HITTEST_RESIZE_LEFT;
		if (right)
			return SDL_HITTEST_RESIZE_RIGHT;
	}
	if (m_cache.titleBar.Contains(fp))
		return SDL_HITTEST_DRAGGABLE;
	return SDL_HITTEST_NORMAL;
}

// ── Panneau flottant ─────────────────────────────────────────────────────────

void MakeFloatingPanel(Widget handle, Option<Widget> grip, sdl3::FPoint minSize) {
	handle.Set(MoveHandle{false, minSize});
	if (grip.IsSome())
		grip.Value().Set(MoveHandle{true, minSize});
}

} // namespace ui
