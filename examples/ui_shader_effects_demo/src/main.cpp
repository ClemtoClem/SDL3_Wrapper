/**
 * ui_shader_effects_demo — ui::ShaderEffect : quelques panneaux 2D
 * ordinaires (texte, bouton, curseur), chacun post-traité par un effet GPU
 * (TINT_SHIFT / GLOW). Le renderer 2D de l'interface et le render3d::Canvas
 * (device GPU des effets) partagent la même fenêtre.
 *
 * Chaque widget portant un ShaderEffect est dessiné dans une texture cible,
 * relu, envoyé au GPU, passé dans le pipeline de l'effet puis affiché à la
 * place de son sous-arbre (cf. ui/shader_effect.hpp). Sans GPU, les panneaux
 * restent dessinés normalement. Les widgets restent interactifs.
 *
 *   cpp-maker run ui_shader_effects_demo
 */
#include <iostream>

#include <core/core.hpp>
#include <render3d/canvas.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 900;
static constexpr int WIN_H = 440;

static constexpr const char *EFFECTS_CSS = R"css(
#samples { gap: 24px; padding: 24px; }
.sample { gap: 8px; align-items: center; }
.sample > label { font-size: 15px; }
.card { width: 180px; height: 180px; padding: 12px; gap: 10px; background-color: #598cd9; border-radius: 10px;
        justify-content: center; align-items: center; }
.card.white { background-color: #ffffff; color: #202020; }
)css";

static ui::WidgetBuilder Card(const char *caption, Option<ui::ShaderEffect> effect, bool white = false) {
	ui::WidgetBuilder card = ui::Panel().Class("card").Children({
		ui::Label("Contenu réel"),
		ui::Button("Bouton"),
		ui::Slider(0.f, 1.f, 0.5f).Width(140.f),
	});
	if (white)
		card.Class("white");
	if (effect.IsSome())
		card.With(effect.Value());
	return ui::Column().Class("sample").Children({ui::Label(caption), card});
}

int main() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::cerr << "TTF init: " << ttf.Error().CStr() << "\n";
		return 1;
	}
	auto winRes = sdl3::Window::Create(u8"ui:: - effets de shader", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
	if (!winRes) {
		std::cerr << "Window: " << winRes.Error().CStr() << "\n";
		return 1;
	}
	auto &window = winRes.Value();
	auto renRes = sdl3::Renderer::Create(window);
	if (!renRes) {
		std::cerr << "Renderer: " << renRes.Error().CStr() << "\n";
		return 1;
	}
	auto &ren = renRes.Value();
	// Device GPU des effets, sur la même fenêtre que le renderer 2D.
	auto canvasRes = render3d::Canvas::Create(window, window.GetWidth(), window.GetHeight());
	if (!canvasRes)
		std::cerr << "Canvas (effets désactivés) : " << canvasRes.Error().CStr() << "\n";

	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, window, ren);
	gui.AddStyleSheet(EFFECTS_CSS);
	if (canvasRes)
		gui.Initialize(canvasRes.Value());

	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "ui:: - effets de shader", .appIcon = Some(ui::MaterialIcons::AUTO_AWESOME),
				 .status = canvasRes ? "Référence, TintShift, Glow." : "Pas de device GPU : effets désactivés."});
	frame.Content().Append(ui::Row().Id("samples").Absolute().Anchor(ui::Anchor::Center).Children({
		Card("Normal", NONE),
		Card("TintShift (orange, 0.6)", Some(ui::TintShift(ui::Color{1.f, 0.55f, 0.15f, 1.f}, 0.6f))),
		Card("Glow (rouge)", Some(ui::GlowEffect(ui::Color{1.f, 0.15f, 0.15f, 1.f}, 0.3f)), true),
	}));

	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		const uint64_t now = sdl3::GetTicksMS();
		const float dt = float(now - lastTick) / 1000.f;
		lastTick = now;
		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}
			gui.HandleEvent(e);
		}
		gui.Update(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		ren.SetDrawColor(gui.CurrentPalette().appBg);
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	return 0;
}
