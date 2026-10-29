/**
 * ui_minimal — exemple minimal du module ui:: (une trentaine de lignes d'UI).
 *
 * Montre le strict nécessaire : la façade ui::Ui, l'arbre déclaratif
 * (builders façon Flutter), le cadre de fenêtre et quelques évènements. Pour
 * la démonstration complète de tous les widgets, voir ui_showcase.
 *
 *   cpp-maker run ui_minimal
 */
#include <format>
#include <iostream>

#include <core/core.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 900;
static constexpr int WIN_H = 600;

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

	// ── Interface ────────────────────────────────────────────────────────────
	// Polices par défaut (DejaVu, icônes) chargées automatiquement.
	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, window, ren);

	// Fenêtre sans décoration système : barre de titre (déplacer, réduire,
	// agrandir, fermer), barre d'état et poignée de redimensionnement.
	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "ui:: - exemple minimal", .appIcon = Some(ui::MaterialIcons::WIDGETS),
				 .status = "Échap ou × pour quitter."});

	// ── Une carte centrée avec quelques widgets ──────────────────────────────
	int clicks = 0;
	frame.Content().Append(ui::Panel()
							   .Absolute()
							   .Anchor(ui::Anchor::Center)
							   .Width(360.f)
							   .Gap(10.f)
							   .Padding(16.f)
							   .Children({
								   ui::Label("Bonjour ui:: !").FontSize(20.f),
								   ui::Label("Un panneau, un slider, un bouton.").Color(ui::Rgba(150, 156, 178)),
								   ui::Separator(),
								   ui::Row().Children({
									   ui::Label("Volume").Width(80.f),
									   ui::Slider(0.f, 100.f, 50.f, 1.f)
										   .Grow()
										   .Tooltip("Glissez ou molette")
										   .OnChange([](float v) { std::cout << std::format("volume={:.0f}\n", v); }),
								   }),
								   ui::Row().Children({
									   ui::Button("Cliquez-moi").Grow().OnClick([&clicks, &frame] {
										   std::cout << "clic n° " << ++clicks << "\n";
										   frame.SetStatus(std::format("{} clic(s)", clicks));
									   }),
									   ui::Switch(true).Tooltip("Un interrupteur"),
								   }),
							   }));

	// ── Boucle ───────────────────────────────────────────────────────────────
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
