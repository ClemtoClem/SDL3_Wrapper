/**
 * ui_aero_windows — thème Frutiger Aero, fenêtre sans décoration : « bureau
 * simulé ». Barre de menus, panneaux flottants déplaçables /
 * redimensionnables (MoveHandle), menu contextuel (clic droit sur le
 * bureau), boîte de dialogue modale, splitter, sélecteur de date.
 *
 *   cpp-maker run ui_aero_windows
 */
#include <iostream>

#include <core/core.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 900;
static constexpr int WIN_H = 640;

static constexpr const char *AERO_CSS = R"css(
window-frame { background-image: linear-gradient(to bottom, #142d50, #3764a0); }
#desktop { flex-grow: 1; }
.floating { padding: 0; gap: 0; }
.floating-header { height: 26px; padding: 0 8px; background-color: #5aa0e6b0; cursor: move;
                   border-radius: 6px 6px 0 0; }
.floating-body { flex-grow: 1; padding: 8px; gap: 8px; }
.floating-grip { position: absolute; anchor: bottom-right; right: 2px; bottom: 2px; cursor: nwse-resize; }
#about-text { color: #d2dcea; }
)css";

/// Panneau flottant : en-tête qui déplace, poignée qui redimensionne.
static ui::WidgetBuilder FloatingPanel(String title, float x, float y, float w, float h, ui::AnyWidget body) {
	return ui::Panel()
		.Class("floating")
		.Absolute()
		.Left(x)
		.Top(y)
		.Size(w, h)
		.Children({
			ui::Row().Class("floating-header").With(ui::MoveHandle{false, {160.f, 100.f}}).Children({ui::Label(title)}),
			ui::Column().Class("floating-body").Children({std::move(body)}),
			ui::Icon(ui::MaterialIcons::SOUTH_EAST, 14.f).Class("floating-grip").With(ui::MoveHandle{true, {160.f, 100.f}}),
		});
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
	auto winRes = sdl3::Window::Create(u8"ui:: Aero - bureau", WIN_W, WIN_H,
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
				{.title = "Bureau Aero", .appIcon = Some(ui::MaterialIcons::DESKTOP_WINDOWS),
				 .status = "Panneaux flottants : glisser leur en-tête. Clic droit : menu.", .fillTitleBar = false});

	ui::Widget about;
	frame.Content().Append(ui::MenuBar().FillWidth().Children({
		ui::Menu("Fichier").Children({
			ui::MenuItem("Nouveau", "Ctrl+N"),
			ui::MenuItem("Ouvrir", "Ctrl+O"),
			ui::MenuSeparator(),
			ui::MenuItem("Quitter").OnClick([&frame] { frame.RequestClose(); }),
		}),
		ui::Menu("Aide").Children({ui::MenuItem("À propos").OnClick([&about] { about.Open(); })}),
	}));

	frame.Content().Append(
		ui::Box("desktop")
			.Id("desktop")
			.ContextMenu(ui::ContextMenu().Children({
				ui::MenuItem("Actualiser").OnClick([&frame] { frame.SetStatus("Bureau actualisé."); }),
				ui::MenuItem("À propos").OnClick([&about] { about.Open(); }),
			}))
			.Children({
				FloatingPanel("Agenda", 30.f, 20.f, 260.f, 170.f,
							  ui::Column().Children({
								  ui::Label("Date du rendez-vous :"),
								  ui::DatePicker(2026, 8, 10, [&frame](int y, int m, int d) {
									  frame.SetStatus(String("Rendez-vous le ") + String::From(d) + "/" +
													  String::From(m) + "/" + String::From(y));
								  }),
							  })),
				FloatingPanel("Panneau divisé", 340.f, 20.f, 420.f, 220.f,
							  ui::Splitter(ui::Panel().Padding(8.f).Children({ui::Label("Gauche")}),
										   ui::Panel().Padding(8.f).Children({ui::Label("Droite")}),
										   ui::Orientation::Horizontal, 180.f)
								  .Grow()),
			}));

	gui.Mount(ui::Modal(ui::Panel().Width(320.f).Gap(10.f).Padding(16.f).Children({
						  ui::Label("ui:: Aero Desktop").FontSize(18.f),
						  ui::Label("Démo du thème Frutiger Aero et du bureau simulé.").Id("about-text").TextWrap(),
						  ui::Button("Fermer").AlignSelf(ui::Align::End).OnClick([&about] { about.Close(); }),
					  }))
				  .Ref(about));

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
