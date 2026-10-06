/**
 * ui_aero_windows — thème Frutiger Aero, fenêtre borderless : « bureau
 * simulé ». Barre de menu (Phase 5), panneaux flottants déplaçables/
 * redimensionnables (ui::PanelChrome, Phase 9), popup contextuel (Phase 1),
 * boîte de dialogue modale (Phase 1), splitter (Phase 8), sélecteur de date
 * en popup (Phase 8).
 *
 *   make examples && ./build/examples/ui_aero_windows
 */
#include <iostream>

#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 900;
static constexpr int WIN_H = 640;
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

	auto winRes = sdl3::Window::Create(u8"ui:: Aero - bureau", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
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
	ui::LayoutSystem &layout = gui.Layout();

	gui.SetTextEngine(eng, font);
	gui.Layout().measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};

	// ── Fond « bureau » ──────────────────────────────────────────────────────
	auto desktop = f.Panel();
	desktop.Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Rpct(100)).H(ui::Dimension::Rpct(100));
	desktop.Style(ui::UiStyle{}.SetBg(sdl3::FColor{20 / 255.f, 45 / 255.f, 80 / 255.f, 1.f}).SetBgGradient(sdl3::FColor{55 / 255.f, 100 / 255.f, 160 / 255.f, 1.f}));
	desktop.Spawn();

	// Encadrement de fenêtre « verre » : barre de titre sans fond (le dégradé
	// du bureau reste visible), barre d'état et poignée de redimensionnement.
	ui::WindowFrame frame;
	frame.Build(gui, window, {.title = u8"Bureau Aero", .appIcon = Some(ui::MaterialIcons::DESKTOP_WINDOWS),
							  .status = "Panneaux flottants : glisser leur en-tête.", .fillTitleBar = false});

	// ── Barre de menu (sous la barre de titre OS) ───────────────────────────
	auto menuBarRow = f.MenuBar();
	menuBarRow.GrowW().Parent(frame.Content());
	ecs::Entity menuBarE = menuBarRow.Spawn();

	// Boîte de dialogue modale ("À propos"), construite avant les menus qui
	// l'ouvrent (openModal a besoin de son Entity déjà spawnée).
	auto aboutContent = f.Panel();
	aboutContent.Size(320.f, 160.f).Gap(10.f).Pad(16.f);
	aboutContent.Children(f.Label("ui:: Aero Desktop").FontSize(18.f),
						  f.Label("Démo du thème Frutiger Aero et du bureau simulé.").TextColor(sdl3::Color{210, 220, 235}),
						  f.Button("Fermer").AlignSelf(ui::CrossAlign::End));
	auto aboutModal = f.Modal(std::move(aboutContent));
	ecs::Entity aboutModalE = aboutModal.Spawn();
	if (auto ch = ar.GetComponent<ui::UiChildren>(aboutModalE); ch.IsSome() && ch.Unwrap()->list.size() == 2) {
		ecs::Entity dialogPanel = ch.Unwrap()->list[1];
		if (auto ch2 = ar.GetComponent<ui::UiChildren>(dialogPanel); ch2.IsSome() && !ch2.Unwrap()->list.empty()) {
			ecs::Entity closeBtn = ch2.Unwrap()->list.back();
			if (auto cb = ar.GetOrAddComponent<ui::UiCallbacks>(closeBtn); cb.IsSome())
				cb.Unwrap()->onClick = [&gui, aboutModalE] { gui.CloseModal(aboutModalE); };
		}
	}

	ecs::Entity fileMenu = f.Menu(menuBarE, "Fichier", f.MenuItem("Nouveau"), f.MenuItem("Ouvrir"),
								 f.MenuItem("Quitter").OnClick([&frame] { frame.RequestClose(); }));
	(void)fileMenu;
	ecs::Entity aideMenu = f.Menu(menuBarE, "Aide", f.MenuItem("À propos"));
	if (auto ch = ar.GetComponent<ui::UiChildren>(aideMenu); ch.IsSome() && !ch.Unwrap()->list.empty()) {
		ecs::Entity aproposItem = ch.Unwrap()->list.front();
		if (auto cb = ar.GetOrAddComponent<ui::UiCallbacks>(aproposItem); cb.IsSome())
			cb.Unwrap()->onClick = [&gui, aboutModalE] { gui.OpenModal(aboutModalE); };
	}

	// ── Zone de contenu : 2 panneaux flottants (PanelChrome) + splitter ─────
	auto contentArea = f.Column();
	contentArea.GrowW().GrowH().Parent(frame.Content());
	ecs::Entity contentAreaE = contentArea.Spawn();

	// Panneau flottant n°1 : sélecteur de date (déclenché par un bouton).
	auto floatPanel = f.Panel();
	floatPanel.Absolute().Anchor(ui::Anchor::TopLeft).Offset(30.f, 20.f).Size(260.f, 170.f).Gap(0.f).Parent(contentAreaE);
	auto floatHeader = f.Row();
	floatHeader.Size(260.f, 24.f).Pad(math::Sides{8.f, 4.f}).Style(ui::UiStyle{}.SetBg(sdl3::FColor::UI_AERO_GLASS_BLUE()));
	floatHeader.Children(f.Label("Agenda"));
	auto dateBtn = f.Button("Choisir une date...");
	dateBtn.GrowW();
	floatPanel.Children(std::move(floatHeader), std::move(dateBtn));
	ecs::Entity floatPanelE = floatPanel.Spawn();
	ecs::Entity floatHeaderE{};
	if (auto ch = ar.GetComponent<ui::UiChildren>(floatPanelE); ch.IsSome() && ch.Unwrap()->list.size() == 2) {
		floatHeaderE = ch.Unwrap()->list[0];
		ecs::Entity dateBtnE = ch.Unwrap()->list[1];
		auto picker = f.DatePicker(2026, 8, 10);
		ecs::Entity pickerE = picker.Spawn();
		if (auto cb = ar.GetOrAddComponent<ui::UiCallbacks>(dateBtnE); cb.IsSome())
			cb.Unwrap()->onClick = [&ar, &layout, pickerE, dateBtnE] { ui::OpenPopup(ar, layout, pickerE, dateBtnE); };
	}
	ui::PanelChrome::Attach(ar, floatPanelE, floatHeaderE);

	// Panneau flottant n°2 : splitter horizontal.
	auto floatPanel2 = f.Panel();
	floatPanel2.Absolute()
		.Anchor(ui::Anchor::TopLeft)
		.Offset(340.f, 20.f)
		.Size(420.f, 220.f)
		.Gap(0.f)
		.Parent(contentAreaE);
	auto floatHeader2 = f.Row();
	floatHeader2.Size(420.f, 24.f).Pad(math::Sides{8.f, 4.f}).Style(ui::UiStyle{}.SetBg(sdl3::FColor::UI_AERO_GLASS_BLUE()));
	floatHeader2.Children(f.Label("Panneau divisé"));
	floatPanel2.Children(std::move(floatHeader2));
	ecs::Entity floatPanel2E = floatPanel2.Spawn();
	ecs::Entity floatHeader2E{};
	if (auto ch = ar.GetComponent<ui::UiChildren>(floatPanel2E); ch.IsSome() && !ch.Unwrap()->list.empty())
		floatHeader2E = ch.Unwrap()->list.front();
	ui::PanelChrome::Attach(ar, floatPanel2E, floatHeader2E);

	auto splitLeft = f.Panel();
	splitLeft.Pad(8.f).Children(f.Label("Gauche"));
	auto splitRight = f.Panel();
	splitRight.Pad(8.f).Children(f.Label("Droite"));
	ecs::Entity splitContainerE =
		f.Splitter(std::move(splitLeft), std::move(splitRight), ui::Orientation::Horizontal, 180.f);
	ar.AddComponent(splitContainerE, ui::UiParent{floatPanel2E});
	if (auto ch = ar.GetOrAddComponent<ui::UiChildren>(floatPanel2E); ch.IsSome())
		ch.Unwrap()->list.push_back(splitContainerE);
	if (auto it = ar.GetOrAddComponent<ui::UiItem>(splitContainerE); it.IsSome())
		it.Unwrap()->height = ui::Dimension::Grow();

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
