// Définitions de ui/ui.hpp
#include "ui/ui.hpp"

namespace ui {

// ── UiFrameTimings ───────────────────────────────────────────────────────────

double UiFrameTimings::TotalMs() const noexcept {
	return styleMs + layoutMs + viewport3dMs + shaderEffectMs + drawMs;
}

// ── Ui ───────────────────────────────────────────────────────────────────────

Ui::Ui(ecs::ArchetypeRegistry &world, sdl3::Window &window, sdl3::Renderer &renderer, UiTheme theme)
	: world(&world), window(&window), factory(world, layout, std::move(theme)) {
	this->window->StartTextInput();
	style.sheet = &factory.sheet;
	Initialize(renderer);
}

void Ui::Initialize(sdl3::Renderer &renderer) {
	sdlBackend = Some(SdlRendererBackend(renderer));
	backend = &sdlBackend.Value();
}

void Ui::SetTheme(UiTheme theme) {
	render.scrollbarTrack = theme.scrollbar.bgNormal;
	render.scrollbarThumb = theme.scrollbar.bgHovered;
	render.tooltipBg = sdl3::FColor{theme.fieldBg.r, theme.fieldBg.g, theme.fieldBg.b, 0.97f};
	render.tooltipBorder = theme.border;
	render.tooltipText = theme.text;
	render.dragAccent = theme.accent;
	factory.SetTheme(std::move(theme));
}

void Ui::CreateStyleClass(String name, UiStyle style) {
	factory.sheet.Define(name, std::move(style));
	factory.sheet.DirtyClassUsers(*world, name);
}

void Ui::SetKeyPassThrough(std::function<bool(const sdl3::Event &, ecs::Entity)> rule) {
	input.keyPassThrough = std::move(rule);
}

void Ui::Render() {
	// Chronométrage par étape (cf. UiFrameTimings) : cinq lectures
	// d'horloge par image, pour que l'application puisse dire OÙ part son
	// temps de rendu au lieu de constater seulement qu'il est élevé.
	const double tickRate = double(sdl3::GetPerformanceFrequency());
	uint64_t mark = sdl3::GetPerformanceCounter();
	auto elapsedMs = [&mark, tickRate]() {
		uint64_t now = sdl3::GetPerformanceCounter();
		double ms = double(now - mark) * 1000.0 / tickRate;
		mark = now;
		return ms;
	};

	style.Resolve(*world);
	timings.styleMs = elapsedMs();

	auto sz = window->GetSize();
	layout.RunIfNeeded(*world, float(sz.x), float(sz.y));
	timings.layoutMs = elapsedMs();

	timings.viewport3dMs = 0.0;
	timings.shaderEffectMs = 0.0;
	if (canvasForViewports) {
		viewport3d.Update(*world, *canvasForViewports, *backend);
		timings.viewport3dMs = elapsedMs();
		shaderEffect.Update(*world, *canvasForViewports, *backend, render);
		timings.shaderEffectMs = elapsedMs();
	}
	render.Run(*world, *backend, &input.tooltip);
	timings.drawMs = elapsedMs();
}

} // namespace ui
