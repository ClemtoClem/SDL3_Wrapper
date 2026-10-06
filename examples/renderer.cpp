#include <format>
#include <iostream>
#include <vector>

#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

// ============================================================================
// Constantes
// ============================================================================

static constexpr int WIN_W = 900;
static constexpr int WIN_H = 600; ///< zone de dessin (sous la barre de titre)
/// Barres de titre et d'état de ui::WindowFrame, ajoutées autour de la zone.
static constexpr int FRAME_H = 36 + 24;
static constexpr float SPEED = 250.f; // pixels/seconde pour le carré
static constexpr float FONT_PT = 14.f;

// ============================================================================
// AppState
// ============================================================================

struct ClickPoint {
	float x, y;
	sdl3::Color col;
};

struct AppState {
	// Carré mobile (clavier)
	float sx = WIN_W / 2.f - 30.f;
	float sy = WIN_H / 2.f - 30.f;
	float sw = 60.f, sh = 60.f;
	sdl3::Color sqColor = sdl3::Color::RED();

	// Points de clic (souris)
	std::vector<ClickPoint> clicks;

	// Compteur d'événements (EventWatch)
	int watchCount = 0;

	// Couleur de fond
	sdl3::Color bg = {30, 30, 40};

	// FPS
	uint64_t lastTick = 0;
	float fps = 0.f;
	int frames = 0;

	// Infos clavier actuelles
	String lastKey = "none";
	String lastEvent = "none";
};

// ============================================================================
// Rendu du texte via TTF (bloc de HUD)
// ============================================================================

static void DrawHud(sdl3::Renderer &ren, sdl3::Font &font, const AppState &s, sdl3::TextEngine &eng) {
	std::vector<std::string> lines = {
		std::format("FPS:        {:.1f}", s.fps),
		std::format("Carré:      ({:.0f}, {:.0f})", s.sx, s.sy),
		std::format("Clics:      {}", s.clicks.size()),
		std::format("EventWatch: {} events", s.watchCount),
		std::format("Dernière touche: {}", s.lastKey.c_str()),
		std::format("Dernier event:   {}", s.lastEvent.c_str()),
		"",
		"Contrôles:",
		"  Flèches / WASD : déplacer le carré",
		"  R/G/B/Y        : couleur du carré",
		"  Clic gauche    : dessiner un cercle",
		"  Clic droit     : effacer les cercles",
		"  Espace         : couleur fond aléatoire",
		"  Échap          : quitter",
	};

	float y = 10.f;
	for (auto &line : lines) {
		if (!line.empty()) {
			if (auto txt = sdl3::Text::Create(eng, font, String(line.c_str()))) {
				ren.SetDrawColor(sdl3::Color{220, 220, 220});
				txt.Value().Draw(10.f, y);
			}
		}
		y += 20.f;
	}
}

// ============================================================================
// Test des Properties (exécuté une seule fois)
// ============================================================================

static void TestProperties() {
	using namespace sdl3;
	auto props = Properties::Create();
	props["title"] = "SDL3 Wrapper Test";
	props["version"] = Sint64(3);
	props["scale"] = 2.5f;
	props["debug"] = true;

	auto title = props["title"].Get<const char *>();
	auto version = props["version"].Get<Sint64>();
	auto scale = props["scale"].Get<float>();
	auto debug = props["debug"].Get<bool>();

	std::cout << "[Properties] title=" << title << " version=" << version << " scale=" << scale << " debug=" << debug
			  << "\n";

	// Enumération
	std::cout << "[Properties] clés: ";
	props.Enumerate([](const char *name) { std::cout << name << " "; });
	std::cout << "\n";

	// Clone
	auto copy = props.Clone();
	std::cout << "[Properties] clone title=" << copy["title"].Get<const char *>() << "\n";
}

// ============================================================================
// main
// ============================================================================

int main() {
	// ── Init ─────────────────────────────────────────────────────────────────

	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::AUDIO | sdl3::init_flags::EVENTS);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}

	// ── TTF ──────────────────────────────────────────────────────────────────

	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::cerr << "TTF init: " << ttf.Error().CStr() << "\n";
		return 1;
	}

	auto fontRes = sdl3::Font::FindLocal({"DejaVuSans-Bold", "DejaVuSans", "FreeSans", "Arial", "Helvetica"}, FONT_PT);
	if (!fontRes) {
		std::cerr << "Font load: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto &font = fontRes.Value();

	// ── Window & Renderer ─────────────────────────────────────────────────────

	auto winRes = sdl3::Window::Create(u8"SDL3 Wrapper Demo", WIN_W, WIN_H + FRAME_H, ui::WindowFrame::WINDOW_FLAGS);
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

	// ── TextEngine ────────────────────────────────────────────────────────────

	auto engRes = sdl3::TextEngine::Create(ren);
	if (!engRes) {
		std::cerr << "TextEngine: " << engRes.Error().CStr() << "\n";
		return 1;
	}
	auto &eng = engRes.Value();

	// ── Encadrement de fenêtre (ui::) ─────────────────────────────────────────
	// La scène est dessinée à la main dans la zone de contenu (viewport) ; la
	// barre de titre et la barre d'état sont des widgets ui:: par-dessus.
	ecs::ArchetypeRegistry ar;
	ui::Ui gui(ar, window, ren);
	gui.SetTextEngine(eng, font);
	gui.Layout().measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};
	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "SDL3 Wrapper - rendu 2D", .appIcon = Some(ui::MaterialIcons::BRUSH),
				 .status = "Flèches/WASD : déplacer · R G B Y M : couleur · Espace : fond · Clic : cercle · Molette : taille"});
	auto contentOrigin = [&frame] {
		sdl3::FRect r = frame.ContentRect();
		return sdl3::FPoint{r.x, r.y};
	};

	// ── Properties test ───────────────────────────────────────────────────────

	TestProperties();

	// ── EventWatch ───────────────────────────────────────────────────────────

	AppState state;
	state.lastTick = sdl3::GetTicksMS();

	sdl3::EventWatch watch([&state](const sdl3::Event &) {
		state.watchCount++;
		return true; // ne filtre rien
	});

	// ── Enregistrement d'un type utilisateur ─────────────────────────────────

	uint32_t evCustom = sdl3::RegisterEvents(1);
	std::cout << "[Events] Custom event type id = " << evCustom << "\n";

	// ── Boucle principale ─────────────────────────────────────────────────────

	bool running = true;

	while (running) {
		// -- Temps ---------------------------------------------------------
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - state.lastTick) / 1000.f;
		state.lastTick = now;
		state.frames++;

		// FPS toutes les secondes
		static uint64_t fpsTimer = 0;
		static int fpsCnt = 0;
		fpsTimer += uint64_t(dt * 1000.f);
		fpsCnt++;
		if (fpsTimer >= 1000) {
			state.fps = float(fpsCnt);
			fpsCnt = 0;
			fpsTimer = 0;
		}

		// -- Événements -------------------------------------------------------
		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			state.lastEvent = e.Describe();

			// Quit
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}

			// Clavier — couleur du carré
			if (e.IsKeyDown()) {
				state.lastKey = String(SDL_GetKeyName(e.Keycode()));

				switch (e.Keycode()) {
				case SDLK_R:
					state.sqColor = sdl3::Color::RED();
					break;
				case SDLK_G:
					state.sqColor = sdl3::Color::GREEN();
					break;
				case SDLK_B:
					state.sqColor = sdl3::Color::BLUE();
					break;
				case SDLK_Y:
					state.sqColor = sdl3::Color::YELLOW();
					break;
				case SDLK_M:
					state.sqColor = sdl3::Color::MAGENTA();
					break;
				case SDLK_SPACE: {
					// Couleur de fond pseudo-aléatoire
					uint64_t t = sdl3::GetTicksNS();
					state.bg = {uint8_t((t >> 8) & 0x7F), uint8_t((t >> 16) & 0x7F), uint8_t((t >> 24) & 0x7F)};
					break;
				}
				default:
					break;
				}
			}

			// Fenêtre
			if (e.IsWindowResized()) {
				std::cout << "[Window] Redimensionné: " << e.Window().data1 << "×" << e.Window().data2 << "\n";
			}
			if (e.IsWindowFocusGained())
				std::cout << "[Window] Focus obtenu\n";
			if (e.IsWindowFocusLost())
				std::cout << "[Window] Focus perdu\n";

			// Barre de titre / d'état (boutons, infobulles)
			gui.HandleEvent(e);

			// Souris — cercles sur clic gauche (dans la zone de dessin, en
			// coordonnées de la zone), effacer sur clic droit
			if (e.IsMouseDown(SDL_BUTTON_LEFT)) {
				sdl3::FPoint o = contentOrigin();
				float mx = e.MouseButton().x - o.x, my = e.MouseButton().y - o.y;
				if (mx >= 0.f && my >= 0.f && mx < float(WIN_W) && my < float(WIN_H)) {
					uint8_t r = uint8_t((mx * 255) / WIN_W);
					uint8_t b = uint8_t((my * 255) / WIN_H);
					state.clicks.push_back({mx, my, sdl3::Color{r, 100, b}});
				}
			}
			if (e.IsMouseDown(SDL_BUTTON_RIGHT))
				state.clicks.clear();

			// Molette — taille du carré
			if (e.IsMouseWheel()) {
				state.sw = sdl3::Clamp(state.sw + e.MouseWheel().y * 5.f, 10.f, 200.f);
				state.sh = state.sw;
			}

			// Gamepad / joystick info
			if (e.IsGamepadAdded())
				std::cout << "[Gamepad] Connecté (id=" << e.GamepadDevice().which << ")\n";
			if (e.IsGamepadRemoved())
				std::cout << "[Gamepad] Déconnecté\n";

			// Drop
			if (e.IsDropFile())
				std::cout << "[Drop] Fichier: " << e.Drop().data << "\n";
		}

		// -- Mouvement du carré (clavier d'état) ------------------------------
		auto kb = sdl3::keyboard::State();
		if (kb[SDL_SCANCODE_LEFT] || kb[SDL_SCANCODE_A])
			state.sx -= SPEED * dt;
		if (kb[SDL_SCANCODE_RIGHT] || kb[SDL_SCANCODE_D])
			state.sx += SPEED * dt;
		if (kb[SDL_SCANCODE_UP] || kb[SDL_SCANCODE_W])
			state.sy -= SPEED * dt;
		if (kb[SDL_SCANCODE_DOWN] || kb[SDL_SCANCODE_S])
			state.sy += SPEED * dt;

		// Garder dans la fenêtre
		state.sx = sdl3::Clamp(state.sx, 0.f, float(WIN_W) - state.sw);
		state.sy = sdl3::Clamp(state.sy, 0.f, float(WIN_H) - state.sh);

		gui.Tick(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		// -- Rendu ------------------------------------------------------------
		ren.SetDrawColor(sdl3::FColor::UI_WINDOW_BG());
		ren.Clear();
		// La scène dans la zone de contenu : (0, 0) = son coin haut-gauche.
		sdl3::FPoint o = contentOrigin();
		ren.SetViewport(sdl3::FRect{o.x, o.y, float(WIN_W), float(WIN_H)});
		ren.SetDrawColor(state.bg);
		ren.FillRect(sdl3::FRect{0.f, 0.f, float(WIN_W), float(WIN_H)});

		// Cercles de clic
		for (auto &cp : state.clicks) {
			ren.SetDrawColor(cp.col);
			ren.DrawCircle(sdl3::FPoint{cp.x, cp.y}, 20.f);
		}

		// Grille légère
		ren.SetDrawColor(sdl3::Color{50, 50, 60});
		for (int x = 0; x < WIN_W; x += 50) {
			std::array<sdl3::FPoint, 2> seg{{{float(x), 0.f}, {float(x), float(WIN_H)}}};
			ren.DrawLines(seg);
		}
		for (int y = 0; y < WIN_H; y += 50) {
			std::array<sdl3::FPoint, 2> seg{{{0.f, float(y)}, {float(WIN_W), float(y)}}};
			ren.DrawLines(seg);
		}

		// Carré mobile
		ren.SetDrawColor(state.sqColor);
		ren.FillRect(sdl3::FRect{state.sx, state.sy, state.sw, state.sh});

		// Contour blanc
		ren.SetDrawColor(sdl3::Color::WHITE_SMOKE());
		ren.DrawRect(sdl3::FRect{state.sx, state.sy, state.sw, state.sh});

		// Croix au centre
		ren.SetDrawColor(sdl3::Color{255, 255, 0});
		float cx = state.sx + state.sw / 2.f;
		float cy = state.sy + state.sh / 2.f;
		std::array<sdl3::FPoint, 2> hline{{{cx - 10.f, cy}, {cx + 10.f, cy}}};
		std::array<sdl3::FPoint, 2> vline{{{cx, cy - 10.f}, {cx, cy + 10.f}}};
		ren.DrawLines(hline);
		ren.DrawLines(vline);

		// HUD texte
		DrawHud(ren, font, state, eng);

		ren.ClearViewport();
		gui.Render();
		ren.Present();
	}

	std::cout << "[Info] Terminé — " << state.frames << " frames, " << state.watchCount << " événements surveillés\n";
	return 0;
}
