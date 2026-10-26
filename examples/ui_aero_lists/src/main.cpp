/**
 * ui_aero_lists — thème Frutiger Aero, fenêtre sans décoration : listes,
 * arbres, tableaux et graphes. Selectable multi-sélection (Ctrl/Maj+clic),
 * TreeNode imbriqué, Table triable / redimensionnable / réordonnable,
 * PlotLines.
 *
 *   cpp-maker run ui_aero_lists
 */
#include <iostream>

#include <core/core.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 820;
static constexpr int WIN_H = 620;

static constexpr const char *AERO_CSS = R"css(
window-frame { background-image: linear-gradient(to bottom, #1d4f8a, #6fa8dc); }
#content { padding: 16px; gap: 12px; align-items: stretch; }
#content > column { gap: 10px; }
selection-list { background-color: #ffffff26; border-radius: 8px; padding: 6px; gap: 2px; }
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
	auto winRes = sdl3::Window::Create(u8"ui:: Aero - listes", WIN_W, WIN_H,
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

	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "Listes Aero", .appIcon = Some(ui::MaterialIcons::VIEW_LIST),
				 .status = "Listes sélectionnables et arbre.", .fillTitleBar = false});

	const float samples[] = {3.f, 5.f, 2.f, 8.f, 6.f, 9.f, 4.f, 7.f};
	frame.Content().Append(ui::Row().Id("content").Grow().FillWidth().Children({
		// ── Colonne gauche : liste sélectionnable + arbre ──
		ui::Column().Width(220.f).Children({
			ui::Label("Fruits (Ctrl/Maj+clic) :"),
			ui::SelectionList(true).FillWidth().Height(160.f).OnSelect([&frame](int i) {
				frame.SetStatus(String("Fruit sélectionné : ") + String::From(i));
			}).Children({
				ui::Selectable("Pomme", 0),
				ui::Selectable("Banane", 1),
				ui::Selectable("Cerise", 2),
				ui::Selectable("Datte", 3),
				ui::Selectable("Figue", 4),
			}),
			ui::Label("Arborescence :"),
			ui::SelectionList(false).FillWidth().Grow().Children({
				ui::TreeNode("Documents", 0).Children({ui::TreeNode("rapport.pdf", 1, false),
													   ui::TreeNode("notes.txt", 2, false)}),
				ui::TreeNode("Images", 3).Children({ui::TreeNode("photo1.png", 4, false),
													ui::TreeNode("photo2.png", 5, false)}),
			}),
		}),
		// ── Colonne droite : table + graphe ──
		ui::Column().Grow().Children({
			ui::Label("Table (tri / redim. / glisser une ligne) :"),
			ui::Table({{"Nom", 140.f, 60.f, true}, {"Score", 90.f, 50.f, true}, {"Niveau", 90.f, 50.f, true}},
					  {{"Alice", "87", "12"}, {"Bob", "64", "9"}, {"Chloé", "95", "15"}, {"David", "72", "10"}},
					  true, true)
				.FillWidth()
				.Height(180.f),
			ui::Label("Graphe :"),
			ui::PlotLines(samples, "Progression").FillWidth().Height(140.f),
		}),
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
