#pragma once
/**
 * ui::CanvasWindowFrame — l'encadrement de WindowFrame pour une application
 * qui dessine en 3D directement dans la swapchain (render3d::Canvas,
 * Begin()/End()) sans sdl3::Renderer.
 *
 * L'interface est rendue EN LOGICIEL sur une surface ; seules ses deux
 * barres, opaques, sont copiées par-dessus la scène (Canvas::SetOverlay) —
 * le rendu 3D reste complet (ombres, IBL, instanciation).
 *
 * @code{.cpp}
 * auto window = sdl3::Window::Create("Scène", 1280, 720, ui::WindowFrame::WINDOW_FLAGS);
 * auto canvas = render3d::Canvas::Create(window, 1280, 720);
 * auto frame = ui::CanvasWindowFrame::Create(window, {.title = "Scène"});
 * while (running) {
 *     while (auto ev = sdl3::PollEvent()) frame.Value()->HandleEvent(ev.Value());
 *     frame.Value()->Update(dt);
 *     if (canvas.Begin()) { canvas.DrawObject(scene); frame.Value()->Draw(canvas); canvas.End(); }
 * }
 * @endcode
 *
 * Coordonnées en pixels de fenêtre (densité 1).
 */
#include <memory>

#include <render3d/canvas.hpp>
#include <sdl3/ttf.hpp>

#include "ui.hpp"
#include "window_frame.hpp"

namespace ui {

class CanvasWindowFrame {
public:
	[[nodiscard]] static Result<std::unique_ptr<CanvasWindowFrame>, String> Create(sdl3::Window &window,
																					 WindowFrameOptions options,
																					 Theme theme = Theme::Dark);

	CanvasWindowFrame(const CanvasWindowFrame &) = delete;
	CanvasWindowFrame &operator=(const CanvasWindowFrame &) = delete;
	~CanvasWindowFrame();

	/// Vrai si l'évènement concernait l'interface.
	bool HandleEvent(const sdl3::Event &event);
	/// Avance l'interface, met à jour le hit-test et dessine sur la surface.
	void Update(float dt);
	/// Copie les barres par-dessus la scène — entre canvas.Begin() et End().
	void Draw(render3d::Canvas &canvas);

	[[nodiscard]] bool CloseRequested() const noexcept { return m_frame.CloseRequested(); }
	/// Le point (fenêtre) est-il sur une barre ?
	[[nodiscard]] bool IsOverChrome(sdl3::FPoint p) const;

	[[nodiscard]] WindowFrame &Frame() noexcept { return m_frame; }
	[[nodiscard]] Ui &Gui() noexcept { return *m_gui; }
	void SetStatus(String text, Option<Color> color = NONE) { m_frame.SetStatus(std::move(text), color); }

private:
	CanvasWindowFrame() = default;

	// Destruction inverse : cadre, interface, monde, rendu, surface, TTF.
	sdl3::TtfContext m_ttf;
	sdl3::Surface m_surface;
	sdl3::Renderer m_renderer;
	ecs::ArchetypeRegistry m_world;
	std::unique_ptr<Ui> m_gui;
	WindowFrame m_frame;
	sdl3::Window *m_window = nullptr;
};

} // namespace ui
