// Définitions de ui/window_frame.hpp
#include "ui/window_frame.hpp"

#include "ui/ui.hpp"

namespace ui {

WindowFrame::~WindowFrame() {
	// Le rappel de hit-test pointe sur m_chrome : le retirer avant de mourir.
	m_chrome.Detach();
}

void WindowFrame::Build(Ui& gui, sdl3::Window& window, WindowFrameOptions options) {
	Build(gui.Factory(), gui.Layout(), gui.RenderSystem(), window, std::move(options));
}

void WindowFrame::Build(UiFactory& f, LayoutSystem& layout, RenderSystem& render,
						sdl3::Window& window, WindowFrameOptions options) {
	m_world = &f.World();
	m_layout = &layout;
	m_window = &window;

	StringView family = Glyphs::FontFamily<MaterialIcons>();
	if (!render.HasFont(family)) {
		m_iconFont = OpenMaterialIconFont();
		if (m_iconFont)
			render.RegisterFont(family, *m_iconFont);
	}
	m_icons = render.HasFont(family);
	// Le titre système (barre des tâches, sélecteur de fenêtres) suit celui
	// de la barre de titre.
	if (!options.title.IsEmpty())
		window.SetTitle(options.title);

	WidgetBuilder root = f.Column();
	root.Gap(0.f)
		.Pad(0.f)
		.Fixed()
		.Anchor(Anchor::TopLeft)
		.W(Dimension::Rpct(100.f))
		.H(Dimension::Rpct(100.f));
	if (options.background.a > 0.f)
		root.Bg(options.background);
	m_root = root.Spawn();

	TitleBarOptions title;
	title.title = options.title;
	title.appIcon = options.appIcon;
	title.moveHandle = options.moveHandle;
	title.minimizeButton = options.minimizeButton;
	title.maximizeButton = options.maximizeButton;
	title.icons = m_icons;
	title.height = options.titleHeight;
	title.background = options.titleBackground;
	title.fillBackground = options.fillTitleBar;
	title.onClose = [this, onClose = std::move(options.onClose)] {
		m_closeRequested = true;
		if (onClose)
			onClose();
	};
	m_titleBar = ui::TitleBar(f, window, std::move(title), m_root);

	WidgetBuilder content = f.Column();
	content.Gap(options.contentGap).Pad(options.contentPadding).GrowW().GrowH().Parent(m_root);
	m_content = content.Spawn();

	if (options.statusBar) {
		StatusBarOptions status;
		status.text = options.status;
		status.icons = m_icons;
		status.height = options.statusHeight;
		status.background = options.statusBackground;
		m_statusBar = ui::StatusBar(f, std::move(status), m_root);
	}
	m_chrome.Attach(window, *m_world, layout, m_titleBar,
					options.statusBar ? &m_statusBar : nullptr);
}

void WindowFrame::Update() {
	if (!m_world || !m_window)
		return;
	m_chrome.Update();
	m_titleBar.Update(*m_world, *m_window);
}

sdl3::FRect WindowFrame::ContentRect() const {
	if (!m_world)
		return {};
	auto c = m_world->GetComponent<UiComputed>(m_content);
	return c.IsSome() ? c.Unwrap()->screen : sdl3::FRect{};
}

void WindowFrame::SetTitle(const String& text) {
	if (m_world) {
		m_titleBar.SetTitle(*m_world, text);
		m_layout->MarkDirty();
	}
	if (m_window)
		m_window->SetTitle(text);
}

void WindowFrame::SetStatus(const String& text, sdl3::FColor color) {
	if (!m_world || !m_statusBar.label.Valid())
		return;
	m_statusBar.SetText(*m_world, text, color);
	m_layout->MarkDirty();
}

} // namespace ui
