#pragma once
/**
 * ui::IUiRenderBackend — seam entre RenderSystem et le périphérique de
 * dessin réel (M20 du plan d'expansion moteur). Couvre exactement les
 * primitives que RenderSystem/NodeGraphSystem/PlotSeries appelaient jusque
 * là directement sur `sdl3::Renderer&` — mêmes noms, mêmes signatures, pour
 * que le threading de l'interface dans systems.hpp/nodegraph.hpp/plot.hpp ne
 * change QUE le type du paramètre `ren`, jamais les corps de fonction.
 *
 * `SdlRendererBackend` est le backend "d'aujourd'hui" vu à travers cette
 * interface : redirection 1:1 vers un `sdl3::Renderer&` réel, comportement
 * identique à avant M20. Un futur backend GPU (`render3d::Canvas`-based)
 * implémentera la même interface sans toucher au code de dessin existant.
 */
#include <span>

#include "../sdl3/structs.hpp"
#include "../sdl3/render.hpp"
#include "components.hpp"

namespace ui {

class IUiRenderBackend {
public:
	virtual ~IUiRenderBackend() = default;

	virtual bool SetDrawColor(sdl3::Color c) = 0;
	virtual bool SetDrawColor(sdl3::FColor c) = 0;
	virtual bool SetBlendMode(sdl3::BlendMode mode) = 0;
	virtual bool SetClipRect(const sdl3::Rect &r) = 0;
	virtual bool ClearClipRect() = 0;
	[[nodiscard]] virtual sdl3::FColor GetDrawColorFloat() const = 0;

	virtual bool DrawLine(float x1, float y1, float x2, float y2) = 0;
	virtual bool DrawLine(sdl3::FPoint a, sdl3::FPoint b) = 0;
	virtual bool DrawRect(const sdl3::FRect &r) = 0;
	virtual bool FillRect(const sdl3::FRect &r) = 0;
	virtual bool DrawRoundedRect(const sdl3::FRect &rect, const math::Corners &c) = 0;
	virtual bool FillRoundedRect(const sdl3::FRect &rect, const math::Corners &c) = 0;
	virtual bool DrawCircle(sdl3::FPoint center, float radius) = 0;
	virtual bool FillCircle(sdl3::FPoint center, float radius) = 0;
	virtual bool DrawArc(sdl3::FPoint center, float radius, float startAngleDeg, float endAngleDeg) = 0;
	virtual bool DrawPie(sdl3::FPoint center, float radius, float startAngleDeg, float endAngleDeg) = 0;
	virtual bool DrawPolygon(std::span<const sdl3::FPoint> points) = 0;
	virtual bool FillPolygon(std::span<const sdl3::FPoint> points) = 0;

	virtual bool RenderGeometry(std::span<const sdl3::Vertex> vertices, std::span<const int> indices = {}) = 0;

	virtual bool Render(const sdl3::Texture &tex, const sdl3::FRect &dst) = 0;
	virtual bool Render(const sdl3::Texture &tex, const sdl3::FRect &src, const sdl3::FRect &dst) = 0;

	/// Échappatoire pour les callbacks applicatifs écrits contre
	/// `sdl3::Renderer&` (UiCanvas::onDraw, PlotSeries::onCustomDraw,
	/// UiGraphPin/UiGraphConnection::onCustomDraw — gardés inchangés pour ne
	/// pas casser les exemples existants, voir M20 du plan) : nullptr sous un
	/// backend qui n'a pas de sdl3::Renderer réel, auquel cas RenderSystem
	/// saute simplement l'appel du callback plutôt que de planter.
	[[nodiscard]] virtual sdl3::Renderer *NativeRenderer() noexcept { return nullptr; }
};

class SdlRendererBackend : public IUiRenderBackend {
	sdl3::Renderer *m_renderer;

public:
	explicit SdlRendererBackend(sdl3::Renderer &renderer) noexcept : m_renderer(&renderer) {}

	[[nodiscard]] sdl3::Renderer &Renderer() noexcept { return *m_renderer; }

	bool SetDrawColor(sdl3::Color c) override { return m_renderer->SetDrawColor(c); }
	bool SetDrawColor(sdl3::FColor c) override { return m_renderer->SetDrawColor(c); }
	bool SetBlendMode(sdl3::BlendMode mode) override { return m_renderer->SetBlendMode(mode); }
	bool SetClipRect(const sdl3::Rect &r) override { return m_renderer->SetClipRect(r); }
	bool ClearClipRect() override { return m_renderer->ClearClipRect(); }
	[[nodiscard]] sdl3::FColor GetDrawColorFloat() const override { return m_renderer->GetDrawColorFloat(); }

	bool DrawLine(float x1, float y1, float x2, float y2) override { return m_renderer->DrawLine(x1, y1, x2, y2); }
	bool DrawLine(sdl3::FPoint a, sdl3::FPoint b) override { return m_renderer->DrawLine(a, b); }
	bool DrawRect(const sdl3::FRect &r) override { return m_renderer->DrawRect(r); }
	bool FillRect(const sdl3::FRect &r) override { return m_renderer->FillRect(r); }
	bool DrawRoundedRect(const sdl3::FRect &rect, const math::Corners &c) override { return m_renderer->DrawRoundedRect(rect, c); }
	bool FillRoundedRect(const sdl3::FRect &rect, const math::Corners &c) override {
		return m_renderer->FillRoundedRect(rect, c);
	}
	bool DrawCircle(sdl3::FPoint center, float radius) override { return m_renderer->DrawCircle(center, radius); }
	bool FillCircle(sdl3::FPoint center, float radius) override { return m_renderer->FillCircle(center, radius); }
	bool DrawArc(sdl3::FPoint center, float radius, float startAngleDeg, float endAngleDeg) override {
		return m_renderer->DrawArc(center, radius, startAngleDeg, endAngleDeg);
	}
	bool DrawPie(sdl3::FPoint center, float radius, float startAngleDeg, float endAngleDeg) override {
		return m_renderer->DrawPie(center, radius, startAngleDeg, endAngleDeg);
	}
	bool DrawPolygon(std::span<const sdl3::FPoint> points) override { return m_renderer->DrawPolygon(points); }
	bool FillPolygon(std::span<const sdl3::FPoint> points) override { return m_renderer->FillPolygon(points); }

	bool RenderGeometry(std::span<const sdl3::Vertex> vertices, std::span<const int> indices) override {
		return m_renderer->RenderGeometry(vertices, indices);
	}

	bool Render(const sdl3::Texture &tex, const sdl3::FRect &dst) override { return m_renderer->Render(tex, dst); }
	bool Render(const sdl3::Texture &tex, const sdl3::FRect &src, const sdl3::FRect &dst) override {
		return m_renderer->Render(tex, src, dst);
	}

	[[nodiscard]] sdl3::Renderer *NativeRenderer() noexcept override { return m_renderer; }
};

} // namespace ui
