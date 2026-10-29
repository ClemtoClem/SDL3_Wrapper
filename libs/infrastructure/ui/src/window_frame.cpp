// Définitions de ui/window_frame.hpp
#include <ui/ui.hpp>
#include <ui/window_frame.hpp>

namespace ui {

void WindowFrame::Build(Ui &gui, sdl3::Window &window, WindowFrameOptions options) {
	m_window = &window;
	m_icons = gui.GetFonts().HasFamily(Glyphs::FontFamily<MaterialIcons>());
	// Le titre système (barre des tâches) suit celui de la barre.
	if (!options.title.IsEmpty())
		window.SetTitle(options.title);

	TitleBarOptions title;
	title.title = options.title;
	title.appIcon = options.appIcon;
	title.moveHandle = options.moveHandle;
	title.minimizeButton = options.minimizeButton;
	title.maximizeButton = options.maximizeButton;
	title.icons = m_icons;
	title.fillBackground = options.fillTitleBar;
	title.onClose = [this, onClose = std::move(options.onClose)] {
		m_closeRequested = true;
		if (onClose)
			onClose();
	};
	WidgetBuilder titleBar = ui::TitleBar(window, std::move(title));
	if (options.titleBackground.IsSome())
		titleBar.Background(options.titleBackground.Value());
	if (options.titleHeight.IsSome())
		titleBar.Height(options.titleHeight.Value());

	WidgetBuilder root("window-frame");
	root.Fill();
	if (options.background.IsSome())
		root.Background(options.background.Value());
	root.Child(std::move(titleBar));
	WidgetBuilder content("window-content");
	content.Padding(options.contentPadding).Gap(options.contentGap);
	root.Child(std::move(content));
	if (options.statusBar) {
		WidgetBuilder status = ui::StatusBar({options.status, true, m_icons});
		if (options.statusBackground.IsSome())
			status.Background(options.statusBackground.Value());
		if (options.statusHeight.IsSome())
			status.Height(options.statusHeight.Value());
		root.Child(std::move(status));
	}
	m_root = gui.Mount(root);
	m_content = m_root.FindType("window-content").UnwrapOr(Widget());
	m_titleBar = TitleBarWidgets::From(m_root);
	m_statusBar = StatusBarWidgets::From(m_root);
	gui.Layout();
	m_chrome.Attach(window, m_titleBar, options.statusBar ? &m_statusBar : nullptr);
}

void WindowFrame::Update() {
	if (!m_window)
		return;
	m_chrome.Update();
	m_titleBar.Update(*m_window);
}

void WindowFrame::SetTitle(String text) {
	if (m_window)
		m_window->SetTitle(text);
	m_titleBar.SetTitle(std::move(text));
}

void WindowFrame::SetStatus(String text, Option<Color> color) { m_statusBar.SetText(std::move(text), color); }

} // namespace ui
