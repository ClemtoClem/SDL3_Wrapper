#include "game_view.hpp"

#include <algorithm>

#include "../emulator/settings.hpp"

namespace emulator_demo::app {

GameView::GameView(sdl3::Renderer &renderer, EmulatorSession &session) : m_session(session) {
	// Même disposition mémoire que le framebuffer émulé (0xAABBGGRR, soit
	// R,G,B,A en mémoire) : RGBA32, pas RGBA8888 (cf. Renderer::ReadPixels).
	const bool nds = session.System() == RomSystem::NDS;
	const int width = nds ? NDS_SCREEN_WIDTH : (session.System() == RomSystem::GBA ? 240 : 160);
	const int height = nds ? NDS_SCREEN_HEIGHT : (session.System() == RomSystem::GBA ? 160 : 144);

	auto top = sdl3::Texture::Create(renderer, width, height, sdl3::PixelFormat::RGBA32);
	if (top.IsOk()) {
		m_topTexture = std::move(top).Unwrap();
		(void)m_topTexture.SetScaleMode(SDL_SCALEMODE_NEAREST); // pixels nets
	} else {
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "GameView : texture impossible : %s", top.Error().CStr());
	}
	if (nds) {
		auto bottom = sdl3::Texture::Create(renderer, width, height, sdl3::PixelFormat::RGBA32);
		if (bottom.IsOk()) {
			m_bottomTexture = std::move(bottom).Unwrap();
			(void)m_bottomTexture.SetScaleMode(SDL_SCALEMODE_NEAREST);
		}
		if (auto cursor = sdl3::Cursor::FromSystem(sdl3::SystemCursor::DEFAULT); cursor.IsOk())
			m_defaultCursor = Some(std::move(cursor).Unwrap());
		if (auto cursor = sdl3::Cursor::FromSystem(sdl3::SystemCursor::POINTER); cursor.IsOk())
			m_pointerCursor = Some(std::move(cursor).Unwrap());
	}
}

void GameView::Update() {
	if (m_session.CopyLatestFrame(m_snapshot))
		m_uploadPending = true;
}

void GameView::Upload() {
	if (!m_uploadPending || m_snapshot.pixels.empty())
		return;
	m_uploadPending = false;
	const int pitch = m_snapshot.width * int(sizeof(uint32_t));
	(void)m_topTexture.Update(m_snapshot.pixels.data(), pitch);
	if (m_session.System() == RomSystem::NDS && m_bottomTexture.Get())
		(void)m_bottomTexture.Update(m_snapshot.pixels.data() + size_t(NDS_SCREEN_WIDTH) * NDS_SCREEN_HEIGHT, pitch);
}

sdl3::FRect GameView::Fit(const sdl3::FRect &viewport, float width, float height) {
	float scale = std::min(viewport.w / width, viewport.h / height);
	if (scale <= 0.f)
		scale = 1.f;
	float drawWidth = width * scale;
	float drawHeight = height * scale;
	return {viewport.x + (viewport.w - drawWidth) * 0.5f, viewport.y + (viewport.h - drawHeight) * 0.5f, drawWidth,
			drawHeight};
}

void GameView::Render(sdl3::Renderer &renderer, const sdl3::FRect &viewport) {
	if (!m_topTexture.Get() || m_snapshot.frame == 0)
		return;
	Upload();

	if (m_session.System() != RomSystem::NDS) {
		m_topRect = Fit(viewport, float(m_snapshot.width), float(m_snapshot.height));
		m_touchRect = {};
		(void)renderer.Render(m_topTexture, m_topRect);
		return;
	}

	// NDS : deux quadrilatères droits ; la disposition ne fait que les
	// déplacer l'un par rapport à l'autre, jamais tourner leur contenu.
	const bool vertical = Settings::getScreenLayout() != 0;
	const float w = float(NDS_SCREEN_WIDTH);
	const float h = float(NDS_SCREEN_HEIGHT);
	sdl3::FRect both = Fit(viewport, vertical ? w : w * 2.f, vertical ? h * 2.f : h);
	const float screenWidth = vertical ? both.w : both.w * 0.5f;
	const float screenHeight = vertical ? both.h * 0.5f : both.h;
	m_topRect = {both.x, both.y, screenWidth, screenHeight};
	m_touchRect = vertical ? sdl3::FRect{both.x, both.y + screenHeight, screenWidth, screenHeight}
						   : sdl3::FRect{both.x + screenWidth, both.y, screenWidth, screenHeight};
	(void)renderer.Render(m_topTexture, m_topRect);
	(void)renderer.Render(m_bottomTexture, m_touchRect);
}

bool GameView::HandleMouse(const sdl3::Event &event) {
	if (m_session.System() != RomSystem::NDS || m_touchRect.w <= 0.f)
		return false;
	auto inside = [this](float x, float y) { return m_touchRect.Contains(sdl3::FPoint{x, y}); };
	auto toScreen = [this](float x, float y) {
		int sx = int((x - m_touchRect.x) / m_touchRect.w * float(NDS_SCREEN_WIDTH));
		int sy = int((y - m_touchRect.y) / m_touchRect.h * float(NDS_SCREEN_HEIGHT));
		return std::pair<int, int>(std::clamp(sx, 0, NDS_SCREEN_WIDTH - 1), std::clamp(sy, 0, NDS_SCREEN_HEIGHT - 1));
	};

	if (event.IsMouseDown(SDL_BUTTON_LEFT)) {
		const auto &button = event.MouseButton();
		if (!inside(button.x, button.y))
			return false;
		m_mouseDown = true;
		auto [x, y] = toScreen(button.x, button.y);
		m_session.SetTouch(x, y, true);
		return true;
	}
	if (event.IsMouseMotion()) {
		const auto &motion = event.MouseMotion();
		bool hovering = inside(motion.x, motion.y);
		if (hovering != m_hoveringTouch) {
			m_hoveringTouch = hovering;
			Option<sdl3::Cursor> &cursor = hovering ? m_pointerCursor : m_defaultCursor;
			if (cursor.IsSome())
				(void)cursor.Value().Set();
		}
		if (m_mouseDown) {
			auto [x, y] = toScreen(motion.x, motion.y);
			m_session.SetTouch(x, y, true);
			return true;
		}
		return false;
	}
	if (event.IsMouseUp(SDL_BUTTON_LEFT) && m_mouseDown) {
		m_mouseDown = false;
		m_session.SetTouch(0, 0, false);
		return true;
	}
	return false;
}

} // namespace emulator_demo::app
