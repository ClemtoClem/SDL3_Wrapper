#pragma once
/**
 * ui::CanvasWindowFrame — l'encadrement de fenêtre de ui::WindowFrame pour
 * une application qui dessine en 3D DIRECTEMENT dans la swapchain
 * (render3d::Canvas, Begin()/End()) et n'a donc pas de sdl3::Renderer sur
 * lequel poser une interface.
 *
 * L'interface (barre de titre, barre d'état) est rendue EN LOGICIEL sur une
 * surface, puis seules ses deux barres, opaques, sont copiées par-dessus la
 * scène via Canvas::SetOverlay : le rendu 3D reste complet (ombres, IBL,
 * instanciation), contrairement à un widget Viewport3D qui passe par le
 * rendu hors écran.
 *
 * @code{.cpp}
 * auto window = sdl3::Window::Create("Scène", 1280, 720, ui::WindowFrame::WINDOW_FLAGS);
 * auto canvas = render3d::Canvas::Create(window, 1280, 720);
 * auto frame = ui::CanvasWindowFrame::Create(window, {.title = "Scène"});
 * while (running) {
 *     while (auto ev = sdl3::PollEvent()) frame->HandleEvent(ev.Value());
 *     frame->Update(dt);                      // interface + hit-test
 *     if (frame->CloseRequested()) running = false;
 *     if (canvas.Begin()) {
 *         canvas.DrawObject(scene);
 *         frame->Draw(canvas);                // barres par-dessus la scène
 *         canvas.End();
 *     }
 * }
 * @endcode
 *
 * La surface est dimensionnée une fois pour toutes à l'écran de la fenêtre :
 * la fenêtre peut être redimensionnée ou agrandie sans rien recréer. Les
 * coordonnées supposent une densité de pixels de 1 (fenêtre en points =
 * swapchain en pixels).
 */
#include "../render3d/canvas.hpp"
#include "../sdl3/sdl3.hpp"
#include "ui.hpp"
#include "window_frame.hpp"

#include <memory>

namespace ui {

class CanvasWindowFrame {
public:
	/// Charge une police système (DejaVu Sans, FreeSans, Arial) de `fontPt`
	/// points, crée la surface, le rendu logiciel, l'interface et le cadre.
	[[nodiscard]] static Result<std::unique_ptr<CanvasWindowFrame>, String>
	Create(sdl3::Window& window, WindowFrameOptions options, float fontPt = 14.f);

	CanvasWindowFrame(const CanvasWindowFrame&) = delete;
	CanvasWindowFrame& operator=(const CanvasWindowFrame&) = delete;
	~CanvasWindowFrame();

	/// Transmet l'évènement à l'interface (boutons, infobulles) ; vrai s'il
	/// la concernait.
	bool HandleEvent(const sdl3::Event& event);

	/// Avance l'interface, met à jour le hit-test et dessine les barres sur
	/// la surface.
	void Update(float dt);

	/// Copie les barres par-dessus la scène — entre canvas.Begin() et End().
	void Draw(render3d::Canvas& canvas);

	[[nodiscard]] bool CloseRequested() const noexcept { return m_frame.CloseRequested(); }
	/// Le point (en coordonnées de fenêtre) est-il sur une barre (évènements
	/// souris à ne pas transmettre à la scène) ?
	[[nodiscard]] bool IsOverChrome(sdl3::FPoint p) const;

	[[nodiscard]] WindowFrame& Frame() noexcept { return m_frame; }
	[[nodiscard]] Ui& Gui() noexcept { return *m_gui; }
	void SetStatus(const String& text, sdl3::FColor color = {}) { m_frame.SetStatus(text, color); }

private:
	CanvasWindowFrame() = default;

	// Ordre de destruction inverse : cadre, interface, ECS, moteur de texte,
	// rendu, surface, police, TTF.
	sdl3::TtfContext m_ttf;
	Option<sdl3::Font> m_font;
	sdl3::Surface m_surface;
	sdl3::Renderer m_renderer;
	Option<sdl3::TextEngine> m_engine;
	ecs::ArchetypeRegistry m_world;
	std::unique_ptr<Ui> m_gui;
	WindowFrame m_frame;
	sdl3::Window* m_window = nullptr;
	float m_fontPt = 14.f;
};

} // namespace ui
