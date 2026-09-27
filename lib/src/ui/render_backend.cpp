// Définitions de ui/render_backend.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "ui/render_backend.hpp"

namespace ui {

// ── SdlRendererBackend ───────────────────────────────────────────────────────

bool SdlRendererBackend::FillRoundedRect(const sdl3::FRect &rect, const math::Corners &c) {
	return m_renderer->FillRoundedRect(rect, c);
}

bool SdlRendererBackend::DrawArc(sdl3::FPoint center, float radius, float startAngleDeg, float endAngleDeg) {
	return m_renderer->DrawArc(center, radius, startAngleDeg, endAngleDeg);
}

bool SdlRendererBackend::DrawPie(sdl3::FPoint center, float radius, float startAngleDeg, float endAngleDeg) {
	return m_renderer->DrawPie(center, radius, startAngleDeg, endAngleDeg);
}

bool SdlRendererBackend::RenderGeometry(std::span<const sdl3::Vertex> vertices, std::span<const int> indices) {
	return m_renderer->RenderGeometry(vertices, indices);
}

bool SdlRendererBackend::Render(const sdl3::Texture &tex, const sdl3::FRect &src, const sdl3::FRect &dst) {
	return m_renderer->Render(tex, src, dst);
}

} // namespace ui
