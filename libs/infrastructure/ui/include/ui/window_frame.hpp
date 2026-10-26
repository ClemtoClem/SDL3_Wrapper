#pragma once
/**
 * ui::WindowFrame — encadrement complet d'une fenêtre sans décoration en un
 * appel : barre de titre, zone de contenu, barre d'état, hit-test système et
 * police d'icônes.
 *
 * @code{.cpp}
 * auto window = sdl3::Window::Create("Titre", 1280, 720, ui::WindowFrame::WINDOW_FLAGS);
 * ui::WindowFrame frame;
 * frame.Build(gui, window, {.title = "Titre", .appIcon = Some(ui::MaterialIcons::DASHBOARD)});
 * frame.Content().Append(ui::Column().Grow().Children({...}));
 * while (running) {
 *     ...
 *     frame.Update();                 // hit-test, icône agrandir
 *     if (frame.CloseRequested()) running = false;
 * }
 * @endcode
 *
 * Structure : `window-frame` > (`titlebar`, `window-content`, `statusbar`).
 */
#include <functional>

#include "chrome.hpp"

namespace ui {

class Ui;

struct WindowFrameOptions {
	String title{};
	Option<MaterialIcons> appIcon = NONE;
	String status{};
	bool statusBar = true;
	bool moveHandle = true;
	bool minimizeButton = true;
	bool maximizeButton = true;
	/// Fermer : appelé en plus de CloseRequested().
	std::function<void()> onClose{};
	/// Fond de toute la fenêtre (NONE : aucun).
	Option<Color> background = NONE;
	/// Fonds des barres (NONE : ceux des feuilles).
	Option<Color> titleBackground = NONE;
	Option<Color> statusBackground = NONE;
	/// false : barre de titre sans fond (verre Aero).
	bool fillTitleBar = true;
	Option<float> titleHeight = NONE;
	Option<float> statusHeight = NONE;
	float contentPadding = 0.f;
	float contentGap = 0.f;
};

class WindowFrame {
public:
	/// Drapeaux de création : sans décoration système, redimensionnable.
	static constexpr sdl3::WindowFlags WINDOW_FLAGS = sdl3::window_flags::BORDERLESS | sdl3::window_flags::RESIZABLE;

	WindowFrame() = default;
	WindowFrame(const WindowFrame &) = delete;
	WindowFrame &operator=(const WindowFrame &) = delete;
	~WindowFrame() { m_chrome.Detach(); }

	/// Monte l'encadrement sous la racine de `gui` et branche le hit-test.
	void Build(Ui &gui, sdl3::Window &window, WindowFrameOptions options);

	void Update();

	[[nodiscard]] bool CloseRequested() const noexcept { return m_closeRequested; }
	void ClearCloseRequest() noexcept { m_closeRequested = false; }
	void RequestClose() noexcept { m_closeRequested = true; }

	[[nodiscard]] Widget Root() const noexcept { return m_root; }
	/// Parent du contenu de l'application (`window-content`).
	[[nodiscard]] Widget Content() const noexcept { return m_content; }
	[[nodiscard]] sdl3::FRect ContentRect() const { return m_content ? m_content.Bounds() : sdl3::FRect{}; }

	[[nodiscard]] const TitleBarWidgets &TitleBar() const noexcept { return m_titleBar; }
	[[nodiscard]] const StatusBarWidgets &StatusBar() const noexcept { return m_statusBar; }
	[[nodiscard]] WindowChrome &Chrome() noexcept { return m_chrome; }
	[[nodiscard]] bool HasIcons() const noexcept { return m_icons; }

	void SetTitle(String text);
	void SetStatus(String text, Option<Color> color = NONE);

private:
	sdl3::Window *m_window = nullptr;
	WindowChrome m_chrome;
	TitleBarWidgets m_titleBar;
	StatusBarWidgets m_statusBar;
	Widget m_root, m_content;
	bool m_icons = false;
	bool m_closeRequested = false;
};

} // namespace ui
