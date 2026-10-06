/**
 * ui_aero_lists — thème Frutiger Aero, fenêtre borderless : listes/arbres/
 * tableaux/graphes (Phases 4, 6, 7). Selectable multi-sélection, TreeNode
 * imbriqué, Table triable/redimensionnable/réordonnable, PlotLines.
 *
 *   make examples && ./build/examples/ui_aero_lists
 */
#include <iostream>

#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 820;
static constexpr int WIN_H = 620;
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

	auto winRes = sdl3::Window::Create(u8"ui:: Aero - listes", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
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

	auto desktop = f.Panel();
	desktop.Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Rpct(100)).H(ui::Dimension::Rpct(100));
	desktop.Style(ui::UiStyle{}.SetBg(sdl3::FColor::UI_AERO_TITLE_DARK()).SetBgGradient(sdl3::FColor::UI_AERO_TITLE_LIGHT()));
	desktop.Spawn();

	// Encadrement de fenêtre « verre » : barre de titre sans fond (le dégradé
	// du bureau reste visible), barre d'état et poignée de redimensionnement.
	ui::WindowFrame frame;
	frame.Build(gui, window, {.title = u8"Listes Aero", .appIcon = Some(ui::MaterialIcons::VIEW_LIST),
							  .status = "Listes sélectionnables et arbre.", .fillTitleBar = false});

	auto content = f.Row();
	content.Gap(12.f).Pad(16.f).GrowW().GrowH().Parent(frame.Content());

	// ── Colonne gauche : Selectable list + TreeNode ─────────────────────────
	auto leftCol = f.Column();
	leftCol.Gap(10.f).W(ui::Dimension::Px(220.f)).GrowH();

	auto selList = f.Column();
	selList.SelectionRoot(true).Gap(2.f).Pad(6.f).GrowW().H(ui::Dimension::Px(160.f));
	selList.Children(f.Selectable("Pomme", 0), f.Selectable("Banane", 1), f.Selectable("Cerise", 2),
					 f.Selectable("Datte", 3), f.Selectable("Figue", 4));

	auto tree = f.Column();
	tree.SelectionRoot(true).Gap(2.f).Pad(6.f).GrowW().GrowH();
	auto docs = f.TreeNode("Documents", 0, 0);
	docs.Children(f.Selectable("rapport.pdf", 1), f.Selectable("notes.txt", 2));
	auto imgs = f.TreeNode("Images", 3, 0);
	imgs.Children(f.Selectable("photo1.png", 4), f.Selectable("photo2.png", 5));
	tree.Children(std::move(docs), std::move(imgs));

	leftCol.Children(f.Label("Fruits (Ctrl/Maj+clic) :"), std::move(selList), f.Label("Arborescence :"),
					 std::move(tree));

	// ── Colonne droite : Table + Plot ───────────────────────────────────────
	auto rightCol = f.Column();
	rightCol.Gap(10.f).GrowW().GrowH();

	std::vector<ui::UiTableColumn> cols = {{"Nom", 140.f, 60.f, true}, {"Score", 90.f, 50.f, true},
										   {"Niveau", 90.f, 50.f, true}};
	std::vector<std::vector<String>> rows = {{"Alice", "87", "12"}, {"Bob", "64", "9"}, {"Chloé", "95", "15"},
											 {"David", "72", "10"}};
	auto table = f.Table(cols, rows, true, true);
	table.GrowW().H(ui::Dimension::Px(180.f));

	float samples[] = {3.f, 5.f, 2.f, 8.f, 6.f, 9.f, 4.f, 7.f};
	auto plot = f.PlotLines(samples, "Progression");
	plot.GrowW().H(ui::Dimension::Px(100.f));

	rightCol.Children(f.Label("Table (tri/redim./glisser une ligne) :"), std::move(table), f.Label("Graphe :"),
					  std::move(plot));

	content.Children(std::move(leftCol), std::move(rightCol));
	content.Spawn();

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
