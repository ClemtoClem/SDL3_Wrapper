// Définitions de ui/canvas_window_frame.hpp
#include <ui/canvas_window_frame.hpp>

namespace ui {

Result<std::unique_ptr<CanvasWindowFrame>, String> CanvasWindowFrame::Create(sdl3::Window &window,
																			 WindowFrameOptions options, Theme theme) {
	std::unique_ptr<CanvasWindowFrame> self(new CanvasWindowFrame());
	self->m_window = &window;

	auto ttf = sdl3::TtfContext::Create();
	if (!ttf)
		return Err(String("TTF : ") + String(ttf.Error().CStr()));
	self->m_ttf = std::move(ttf.Value());

	// Surface à la taille de l'écran : agrandir ne demande rien de recréer.
	sdl3::Point size = window.GetSize();
	SDL_Rect bounds{};
	if (SDL_DisplayID display = SDL_GetDisplayForWindow(window.Get()); display && SDL_GetDisplayBounds(display, &bounds)) {
		size.x = math::Max(size.x, bounds.w);
		size.y = math::Max(size.y, bounds.h);
	}
	auto surface = sdl3::Surface::Create(size.x, size.y, sdl3::PixelFormat::RGBA32); // octets R, G, B, A
	if (!surface)
		return Err(String("surface : ") + String(surface.Error().CStr()));
	self->m_surface = std::move(surface.Value());
	auto renderer = sdl3::Renderer::CreateSoftware(self->m_surface);
	if (!renderer)
		return Err(String("rendu logiciel : ") + String(renderer.Error()));
	self->m_renderer = std::move(renderer.Value());

	self->m_gui = std::make_unique<Ui>(self->m_world, window, self->m_renderer, theme);
	// Seule la scène 3D occupe le contenu ; les barres sont copiées sans
	// mélange : elles doivent être opaques.
	options.background = NONE;
	options.fillTitleBar = true;
	if (options.titleBackground.IsNone())
		options.titleBackground = Some(WithAlpha(self->m_gui->CurrentPalette().panel, 1.f));
	if (options.statusBackground.IsNone())
		options.statusBackground = Some(WithAlpha(self->m_gui->CurrentPalette().panel, 1.f));
	self->m_frame.Build(*self->m_gui, window, std::move(options));
	return Ok(std::move(self));
}

CanvasWindowFrame::~CanvasWindowFrame() = default;

bool CanvasWindowFrame::HandleEvent(const sdl3::Event &event) { return m_gui->HandleEvent(event); }

void CanvasWindowFrame::Update(float dt) {
	m_gui->Update(dt);
	m_frame.Update();
	m_renderer.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	m_renderer.Clear();
	m_gui->Render();
	m_renderer.Present(); // rendu logiciel : vide la file dans la surface
}

void CanvasWindowFrame::Draw(render3d::Canvas &canvas) {
	std::vector<sdl3::Rect> regions;
	for (const Widget &w : {m_frame.TitleBar().root, m_frame.StatusBar().root})
		if (w) {
			const sdl3::FRect r = w.Bounds();
			regions.push_back({int(r.x), int(r.y), int(r.w + 0.5f), int(r.h + 0.5f)});
		}
	SDL_Surface *s = m_surface.Get();
	if (regions.empty() || !s || !SDL_LockSurface(s))
		return;
	canvas.SetOverlay(static_cast<const uint8_t *>(s->pixels), uint32_t(s->w), uint32_t(s->h), uint32_t(s->pitch),
					  regions);
	SDL_UnlockSurface(s);
}

bool CanvasWindowFrame::IsOverChrome(sdl3::FPoint p) const {
	for (const Widget &w : {m_frame.TitleBar().root, m_frame.StatusBar().root})
		if (w && w.Bounds().Contains(p))
			return true;
	return false;
}

} // namespace ui
