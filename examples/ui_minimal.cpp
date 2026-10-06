/**
 * ui_minimal — exemple minimal du module ui:: (une trentaine de lignes d'UI).
 *
 * Montre le strict nécessaire : la façade ui::Ui (pipeline complet géré
 * automatiquement), factory + builder DSL, et deux callbacks. Pour la
 * démonstration complète de tous les widgets, voir ui_showcase.cpp.
 *
 *   make examples && ./build/examples/ui_minimal
 */
#include <format>
#include <iostream>

#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

// ============================================================================
// Constantes
// ============================================================================

static constexpr int WIN_W = 900;
static constexpr int WIN_H = 600;
static constexpr float FONT_PT = 14.f;

int main() {
	// ── Init SDL / TTF ───────────────────────────────────────────────────────
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
	auto fontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
	if (!fontRes) {
		std::cerr << "Font: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto &font = fontRes.Value();

	auto winRes = sdl3::Window::Create(u8"ui:: - exemple minimal", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
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
	auto engRes = sdl3::TextEngine::Create(ren);
	if (!engRes) {
		std::cerr << "TextEngine: " << engRes.Error().CStr() << "\n";
		return 1;
	}
	auto &eng = engRes.Value();

	// ── Pipeline UI ──────────────────────────────────────────────────────────
	// ui::Ui regroupe layout/input/render/factory et gère la boucle pour
	// nous : plus qu'à lui transmettre les évènements et appeler tick()/render().
	ecs::ArchetypeRegistry ar;
	ui::Ui gui(ar, window, ren);
	ui::LayoutSystem &layout = gui.Layout();
	ui::UiFactory &f = gui.Factory();

	gui.SetTextEngine(eng, font);
	layout.measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};

	// ── Encadrement de fenêtre ───────────────────────────────────────────────
	// Fenêtre sans décoration système : barre de titre (déplacer, réduire,
	// agrandir, fermer), barre d'état et poignée de redimensionnement.
	ui::WindowFrame frame;
	frame.Build(gui, window, {.title = "ui:: - exemple minimal", .appIcon = Some(ui::MaterialIcons::WIDGETS),
							  .status = "Échap ou × pour quitter."});

	// ── Une carte centrée avec quelques widgets ──────────────────────────────
	int clicks = 0;
	auto card = f.Panel();
	card.Absolute()
		.Anchor(ui::Anchor::Center)
		.Parent(frame.Content())
		.W(ui::Dimension::Px(360))
		.Gap(10.f)
		.Pad(16.f)
		.Children(f.Label("Bonjour ui:: !").FontSize(20.f),
				  f.Label("Un panneau, un slider, un bouton.").TextColor(sdl3::Color{150, 156, 178}), f.Separator(),
				  f.Row().Gap(8.f).Children(
					  f.Label("Volume").W(ui::Dimension::Px(80)),
					  f.Slider(0.f, 100.f, 50.f, 1.f).GrowW().Tooltip("Glissez ou molette").OnChange([](float v) {
						  std::cout << std::format("volume={:.0f}\n", v);
					  })),
				  f.Row().Gap(8.f).Children(f.Button("Cliquez-moi").GrowW().OnClick([&clicks] {
					  std::cout << "clic n° " << ++clicks << "\n";
				  }),
											f.Toggle(true).Tooltip("Un interrupteur")));
	card.Spawn();

	// ── Boucle ───────────────────────────────────────────────────────────────
	// Chaque évènement SDL est transmis directement à gui.HandleEvent() — pas
	// de structure agrégée par frame, qui écraserait des positions et pouvait
	// mélanger presse/relâche d'un même clic rapide dans une seule passe.
	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();

	while (running) {
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}
			gui.HandleEvent(e);
		}
		gui.Tick(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		ren.SetDrawColor(sdl3::FColor::UI_APP_BG());
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	return 0;
}
