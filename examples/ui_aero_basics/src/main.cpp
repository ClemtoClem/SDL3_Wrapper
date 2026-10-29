/**
 * ui_aero_basics — thème Frutiger Aero (Windows 7), fenêtre SDL3 sans
 * décoration dont la barre de titre est dessinée par l'interface (ui::chrome).
 *
 * Contrôles de base et édition de valeurs : boutons, interrupteur, case,
 * curseur, DragValue (double-clic pour taper), NumberInput (−/+), pastille de
 * couleur ouvrant un ColorPicker en popup. Le fond « bureau » en dégradé vient
 * d'une feuille CSS ; la barre de titre est transparente (verre).
 *
 *   cpp-maker run ui_aero_basics
 */
#include <format>
#include <iostream>

#include <core/core.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 720;
static constexpr int WIN_H = 560;

static constexpr const char *AERO_CSS = R"css(
window-frame { background-image: linear-gradient(to bottom, #1d4f8a, #6fa8dc); }
#controls { padding: 16px; gap: 12px; }
#controls > row { gap: 8px; }
.caption { width: 120px; color: #ffffff; }
)css";

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
	auto winRes = sdl3::Window::Create(u8"ui:: Aero - contrôles", WIN_W, WIN_H,
									   ui::WindowFrame::WINDOW_FLAGS | sdl3::window_flags::TRANSPARENT);
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

	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, window, ren, ui::Theme::Aero);
	gui.AddStyleSheet(AERO_CSS);

	// Barre de titre sans fond : le dégradé du bureau reste visible.
	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "Contrôles Aero", .appIcon = Some(ui::MaterialIcons::TUNE),
				 .status = "Boutons, curseurs, valeurs et couleurs.", .fillTitleBar = false});

	int clicks = 0;
	const ui::Color initial = ui::Rgba(80, 160, 230);
	ui::Widget swatch, pickerPopup;
	auto caption = [](const char *text) { return ui::Label(text).Class("caption"); };

	frame.Content().Append(ui::Column().Id("controls").Grow().FillWidth().Children({
		ui::Row().Children({
			caption("Boutons :"),
			ui::Button("Cliquez-moi").OnClick([&clicks] { std::cout << "clic n° " << ++clicks << "\n"; }),
			ui::Switch(true).Tooltip("Interrupteur"),
			ui::Checkbox("", false).Tooltip("Case à cocher"),
		}),
		ui::Row().Children({
			caption("Volume :"),
			ui::Slider(0.f, 100.f, 60.f).Grow().OnChange([](float v) { std::cout << std::format("volume={:.0f}\n", v); }),
		}),
		ui::Row().Children({
			caption("Valeur glissée :"),
			ui::DragValue(0.f, 100.f, 42.f, 1.f, 0.5f, 1).Tooltip("Glisser, ou double-clic pour taper"),
		}),
		ui::Row().Children({caption("Quantité :"), ui::NumberInput(0.f, 10.f, 3.f, 1.f, 0)}),
		ui::Row().Children({
			caption("Couleur :"),
			ui::ColorSwatch(initial).Size(28.f, 28.f).Ref(swatch).OnClick([&] { pickerPopup.OpenBelow(swatch); }),
			ui::Popup().Ref(pickerPopup).Children({
				ui::ColorPicker(initial, [&swatch](ui::Color c) { swatch.SetColorValue(c); }),
			}),
		}),
		ui::Progress(0.f, 1.f, 0.65f).Height(10.f).FillWidth(),
	}));

	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		const uint64_t now = sdl3::GetTicksMS();
		const float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit()) {
				running = false;
				break;
			}
			// Échap ferme d'abord les popups, puis quitte.
			if (gui.HandleEvent(e))
				continue;
			if (e.IsKeyDown(SDLK_ESCAPE))
				running = false;
		}
		gui.Update(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		ren.SetDrawColor(sdl3::Color{0, 0, 0, 0});
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	return 0;
}
