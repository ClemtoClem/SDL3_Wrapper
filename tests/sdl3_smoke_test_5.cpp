// Smoke test : SDL3_ttf (Font, TextEngine, Text), SDL3_image (imgLoadTexture),
// et misc (Properties déjà couvert par smoke_test_2 — ici clipboard/system).
// Driver vidéo "dummy" : pas de fenêtre réellement affichée, pas de clipboard système touché.
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

	auto winRes = Window::Create("smoke-test-5", 200, 100, window_flags::HIDDEN);
	if (!winRes) {
		std::cerr << "window failed: " << winRes.Error().CStr() << "\n";
		return 1;
	}
	auto &win = winRes.Value();

	auto renRes = Renderer::Create(win);
	if (!renRes) {
		std::cerr << "renderer failed: " << renRes.Error().CStr() << "\n";
		return 1;
	}
	auto &ren = renRes.Value();

	// --- SDL3_image ---
	auto texRes = ImgLoadTexture(ren, "assets/textures/backrooms_concrete.png");
	if (!texRes) {
		std::cerr << "imgLoadTexture failed: " << texRes.Error().CStr() << "\n";
		return 1;
	}
	auto &tex = texRes.Value();
	std::cout << "loaded texture " << tex.GetWidth() << "x" << tex.GetHeight() << "\n";

	auto surfRes = ImgLoad("assets/textures/backrooms_concrete.png");
	if (surfRes)
		std::cout << "loaded surface " << surfRes.Value().GetWidth() << "x" << surfRes.Value().GetHeight() << "\n";

	// --- SDL3_ttf ---
	auto ttf = TtfContext::Create();
	if (!ttf) {
		std::cerr << "ttf init failed: " << ttf.Error().CStr() << "\n";
		return 1;
	}

	auto fontRes = Font::FindLocal({"DejaVuSans-Bold", "DejaVuSans", "FreeSans", "Arial"}, 14.f);
	if (!fontRes) {
		std::cerr << "font find failed: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto &font = fontRes.Value();

	auto engRes = TextEngine::Create(ren);
	if (!engRes) {
		std::cerr << "text engine failed: " << engRes.Error().CStr() << "\n";
		return 1;
	}
	auto &eng = engRes.Value();

	auto textRes = Text::Create(eng, font, String("Smoke test 5"));
	if (!textRes) {
		std::cerr << "text create failed: " << textRes.Error().CStr() << "\n";
		return 1;
	}
	auto &text = textRes.Value();
	text.Draw(0.f, 0.f);
	ren.Present();
	std::cout << "text drawn ok\n";

	// --- Clipboard (safe: dummy driver, does not touch the real desktop clipboard) ---
	clipboard::SetText("smoke-test-5");
	std::cout << "clipboard hasText=" << clipboard::HasText() << " text=" << clipboard::GetText().CStr() << "\n";

	std::cout << "smoke test 5 done\n";
	return 0;
}
