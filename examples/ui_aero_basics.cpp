/**
 * ui_aero_basics — thème Frutiger Aero (Windows 7), fenêtre SDL3 borderless
 * dont la barre de titre est dessinée par des widgets ui:: (cf. ui::chrome).
 *
 * Démontre les contrôles de base + l'édition de valeurs (Phase 3) : boutons/
 * toggle/checkbox/slider, UiDragValue (double-clic pour taper une valeur),
 * inputNumber() (composite +/-), ColorSwatch + ColorPicker en popup.
 *
 *   make examples && ./build/examples/ui_aero_basics
 */
#include <format>
#include <iostream>

#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 720;
static constexpr int WIN_H = 560;
static constexpr float FONT_PT = 14.f;

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
	auto fontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
	if (!fontRes) {
		std::cerr << "Font: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto &font = fontRes.Value();

	auto winRes = sdl3::Window::Create(u8"ui:: Aero - contrôles", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS | sdl3::window_flags::TRANSPARENT);
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

	ecs::ArchetypeRegistry ar;
	ui::Ui gui(ar, window, ren, ui::UiTheme::Aero());
	ui::UiFactory &f = gui.Factory();

	gui.SetTextEngine(eng, font);
	gui.Layout().measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};

	// ── Fond « bureau » + fenêtre applicative flottante avec chrome OS ──────
	auto desktop = f.Panel();
	desktop.Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Rpct(100)).H(ui::Dimension::Rpct(100));
	desktop.Style(ui::UiStyle{}.SetBg(sdl3::FColor::UI_AERO_TITLE_DARK()).SetBgGradient(sdl3::FColor::UI_AERO_TITLE_LIGHT()));
	desktop.Spawn();

	// Encadrement de fenêtre « verre » : barre de titre sans fond (le dégradé
	// du bureau reste visible), barre d'état et poignée de redimensionnement.
	ui::WindowFrame frame;
	frame.Build(gui, window, {.title = u8"Contrôles Aero", .appIcon = Some(ui::MaterialIcons::TUNE),
							  .status = "Boutons, curseurs, valeurs et couleurs.", .fillTitleBar = false});

	// ── Contenu : contrôles + édition de valeurs ────────────────────────────
	int clicks = 0;
	float dragVal = 42.f;
	sdl3::FColor pickedColor{80 / 255.f, 160 / 255.f, 230 / 255.f, 1.f};

	auto content = f.Column();
	content.Gap(12.f).Pad(16.f).GrowW().GrowH().Parent(frame.Content());

	auto swatch = f.ColorSwatch(pickedColor);
	swatch.Size(28.f, 28.f).Name("colorSwatch");

	auto picker = f.ColorPicker(pickedColor, [](sdl3::FColor) {});
	ecs::Entity pickerE = picker.Spawn();

	content.Children(
		f.Row().Gap(8.f).Children(f.Label("Boutons :").W(ui::Dimension::Px(120)),
								  f.Button("Cliquez-moi").OnClick([&clicks] { std::cout << "clic n° " << ++clicks << "\n"; }),
								  f.Toggle(true).Tooltip("Interrupteur"), f.Checkbox(false).Tooltip("Case à cocher")),
		f.Row().Gap(8.f).Align(ui::CrossAlign::Center).Children(
			f.Label("Volume :").W(ui::Dimension::Px(120)),
			f.Slider(0.f, 100.f, 60.f).GrowW().OnChange([](float v) { std::cout << std::format("volume={:.0f}\n", v); })),
		f.Row().Gap(8.f).Align(ui::CrossAlign::Center).Children(
			f.Label("Valeur glissée :").W(ui::Dimension::Px(120)),
			f.DragValue(0.f, 100.f, dragVal, 1.f, 0.5f, 1).Tooltip("Glisser, ou double-clic pour taper")),
		f.Row().Gap(8.f).Align(ui::CrossAlign::Center).Children(
			f.Label("Quantité :").W(ui::Dimension::Px(120)), f.InputNumber(0.f, 10.f, 3.f, 1.f, 0)),
		f.Row().Gap(8.f).Align(ui::CrossAlign::Center).Children(f.Label("Couleur :").W(ui::Dimension::Px(120)),
																std::move(swatch)),
		f.Progress(0.f, 1.f, 0.65f).H(ui::Dimension::Px(10)));
	content.Spawn();

	// Câble le clic du swatch APRÈS le spawn (retrouvé par nom) : il n'existe
	// pas encore comme Entity tant que `content` (son parent) n'est pas
	// spawné — même contrainte que colorPicker()/inputNumber() en interne
	// (cf. Phase 3), ici gérée directement par l'appli plutôt que par la
	// factory puisque le popup à ouvrir (`pickerE`) est externe au widget.
	if (auto e = ui::FindByName(ar, "colorSwatch"); e.IsSome()) {
		ecs::Entity swatchE = e.Unwrap();
		if (auto cb = ar.GetOrAddComponent<ui::UiCallbacks>(swatchE); cb.IsSome())
			cb.Unwrap()->onClick = [&ar, &gui, swatchE, pickerE] { ui::OpenPopup(ar, gui.Layout(), pickerE, swatchE); };
	}

	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit()) {
				running = false;
				break;
			}
			if (e.IsKeyDown(SDLK_ESCAPE)) {
				if (gui.HasOpenModal())
					gui.HandleEvent(e);
				else
					running = false;
				break;
			}
			gui.HandleEvent(e);
		}
		gui.Tick(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		ren.SetDrawColor(sdl3::FColor::UI_WINDOW_BG());
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	return 0;
}
