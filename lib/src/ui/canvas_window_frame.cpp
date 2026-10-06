// Définitions de ui/canvas_window_frame.hpp
#include "ui/canvas_window_frame.hpp"

namespace ui {

Result<std::unique_ptr<CanvasWindowFrame>, String>
CanvasWindowFrame::Create(sdl3::Window& window, WindowFrameOptions options, float fontPt) {
	std::unique_ptr<CanvasWindowFrame> self(new CanvasWindowFrame());
	self->m_window = &window;
	self->m_fontPt = fontPt;

	auto ttf = sdl3::TtfContext::Create();
	if (!ttf)
		return Err(String("TTF : ") + String(ttf.Error().CStr()));
	self->m_ttf = std::move(ttf.Value());
	auto font = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, fontPt);
	if (!font)
		return Err(String("police : ") + String(font.Error().CStr()));
	self->m_font = Some(std::move(font.Value()));

	// Surface à la taille de l'écran de la fenêtre (au moins celle de la
	// fenêtre) : agrandir ou redimensionner ne demande rien de recréer.
	sdl3::Point size = window.GetSize();
	SDL_Rect bounds{};
	if (SDL_DisplayID display = SDL_GetDisplayForWindow(window.Get());
		display && SDL_GetDisplayBounds(display, &bounds)) {
		size.x = sdl3::Max(size.x, bounds.w);
		size.y = sdl3::Max(size.y, bounds.h);
	}
	auto surface =
		sdl3::Surface::Create(size.x, size.y, sdl3::PixelFormat::RGBA32); // octets R, G, B, A
	if (!surface)
		return Err(String("surface : ") + String(surface.Error().CStr()));
	self->m_surface = std::move(surface.Value());
	auto renderer = sdl3::Renderer::CreateSoftware(self->m_surface);
	if (!renderer)
		return Err(String("rendu logiciel : ") + String(renderer.Error()));
	self->m_renderer = std::move(renderer.Value());
	auto engine = sdl3::TextEngine::Create(self->m_renderer);
	if (!engine)
		return Err(String("moteur de texte : ") + String(engine.Error().CStr()));
	self->m_engine = Some(std::move(engine.Value()));

	self->m_gui = std::make_unique<Ui>(self->m_world, window, self->m_renderer);
	self->m_gui->SetTextEngine(*self->m_engine, *self->m_font);
	sdl3::Font* fontPtr = &*self->m_font;
	self->m_gui->Layout().measureText = [fontPtr, fontPt](const String& s,
														  float fs) -> sdl3::FPoint {
		if (auto sz = fontPtr->Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / fontPt), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};
	// Pas de fond sur la racine : seule la scène 3D occupe le contenu.
	options.background = {};
	// Les deux barres sont copiées sans mélange par-dessus la scène : elles
	// doivent être opaques.
	sdl3::FColor opaque = self->m_gui->Theme().panelBg;
	opaque.a = 1.f;
	if (options.statusBackground.a < 1.f)
		options.statusBackground = opaque;
	options.fillTitleBar = true;
	self->m_frame.Build(*self->m_gui, window, std::move(options));
	return Ok(std::move(self));
}

CanvasWindowFrame::~CanvasWindowFrame() = default;

bool CanvasWindowFrame::HandleEvent(const sdl3::Event& event) {
	return m_gui->HandleEvent(event);
}

void CanvasWindowFrame::Update(float dt) {
	m_gui->Tick(dt);
	m_frame.Update();
	m_renderer.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	m_renderer.Clear();
	m_gui->Render();
	m_renderer.Present(); // rendu logiciel : vide la file de commandes dans la surface
}

void CanvasWindowFrame::Draw(render3d::Canvas& canvas) {
	std::vector<sdl3::Rect> regions;
	for (ecs::Entity e : {m_frame.TitleBar().root, m_frame.StatusBar().root}) {
		if (!e.Valid())
			continue;
		if (auto c = m_world.GetComponent<UiComputed>(e); c.IsSome()) {
			const sdl3::FRect& r = c.Unwrap()->screen;
			regions.push_back({int(r.x), int(r.y), int(r.w + 0.5f), int(r.h + 0.5f)});
		}
	}
	SDL_Surface* s = m_surface.Get();
	if (regions.empty() || !s || !SDL_LockSurface(s))
		return;
	canvas.SetOverlay(static_cast<const uint8_t*>(s->pixels), uint32_t(s->w), uint32_t(s->h),
					  uint32_t(s->pitch), regions);
	SDL_UnlockSurface(s);
}

bool CanvasWindowFrame::IsOverChrome(sdl3::FPoint p) const {
	for (ecs::Entity e : {m_frame.TitleBar().root, m_frame.StatusBar().root})
		if (auto c = m_world.GetComponent<UiComputed>(e);
			c.IsSome() && c.Unwrap()->screen.Contains(p))
			return true;
	return false;
}

} // namespace ui
