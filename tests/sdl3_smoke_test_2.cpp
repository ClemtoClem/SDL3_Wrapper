// NOTE : les fabriques de couleurs sont en MAJUSCULES (`Color::RED()`,
// convention de nommage des constantes du dépôt) ; ce test employait encore
// l'ancienne casse `Color::Red()` et ne compilait plus.
// Smoke test : coeur du wrapper — Sdl, Window, Renderer, Surface, Texture, Properties, system info.
// Utilise le driver vidéo "dummy" + une fenêtre cachée pour ne rien afficher à l'écran.
#include "sdl3/sdl3.hpp"
#include <cstdlib>
#include <iostream>

int main() {
	setenv("SDL_VIDEODRIVER", "dummy", 0);

	using namespace sdl3;

	auto sdl = sdl3::SdlContext::Create(init_flags::VIDEO);
	if (!sdl) {
		std::cerr << "sdl init failed: " << sdl3::GetError().CStr() << "\n";
		return 1;
	}

	auto winRes = Window::Create("smoke-test-2", 320, 240, window_flags::HIDDEN);
	if (!winRes) {
		std::cerr << "window failed: " << winRes.Error().CStr() << "\n";
		return 1;
	}
	auto &win = winRes.Value();
	std::cout << "window size=" << win.GetWidth() << "x" << win.GetHeight() << "\n";

	auto renRes = Renderer::Create(win);
	if (!renRes) {
		std::cerr << "renderer failed: " << renRes.Error().CStr() << "\n";
		return 1;
	}
	auto &ren = renRes.Value();

	ren.SetDrawColor(Color::BLUE());
	ren.Clear();
	ren.SetDrawColor(Color::RED());
	ren.FillRect(FRect{10, 10, 50, 50});
	ren.DrawRect(FRect{70, 10, 50, 50});
	ren.DrawLine(0.f, 0.f, 100.f, 100.f);
	ren.Present();
	std::cout << "renderer output size=" << ren.OutputSize().x << "x" << ren.OutputSize().y << "\n";

	auto surfRes = Surface::Create(16, 16);
	if (!surfRes) {
		std::cerr << "surface failed: " << surfRes.Error().CStr() << "\n";
		return 1;
	}
	auto &surf = surfRes.Value();
	surf.Fill(Color::GREEN());
	std::cout << "surface size=" << surf.GetWidth() << "x" << surf.GetHeight() << "\n";

	auto texRes = ren.CreateTextureFromSurface(surf);
	if (!texRes) {
		std::cerr << "texture failed: " << texRes.Error().CStr() << "\n";
		return 1;
	}
	auto &tex = texRes.Value();
	std::cout << "texture size=" << tex.GetWidth() << "x" << tex.GetHeight() << "\n";
	ren.Render(tex, 0.f, 0.f);
	ren.Present();

	// --- Properties ---
	auto props = Properties::Create();
	props["answer"] = Sint64(42);
	props["label"] = "hello";
	std::cout << "prop answer=" << props["answer"].Get<Sint64>() << " label=" << props["label"].Get<const char *>()
			  << "\n";

	// --- System info ---
	std::cout << "cpu=" << system::CpuCount() << " ram=" << system::SystemRam() << "MB platform=" << system::Platform()
			  << "\n";

	// --- Power ---
	auto pw = power::Info();
	std::cout << "power state=" << int(pw.state) << " secondsLeft=" << pw.secondsLeft.UnwrapOr(-1)
			  << " percent=" << pw.percent.UnwrapOr(-1) << "\n";

	std::cout << "smoke test 2 done\n";
	return 0;
}
