// Smoke test : formes complexes du Renderer (cercle, ellipse, arc, pie,
// rectangles arrondis, polygones, géométrie, bézier) — inspiré de
// SDL3pp_render.h. Driver vidéo "dummy" : pas de fenêtre réellement affichée,
// on vérifie juste que chaque appel réussit (renvoie true) sans planter.
#include "sdl3/sdl3.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
	setenv("SDL_VIDEODRIVER", "dummy", 0);

	using namespace sdl3;

	auto sdl = SdlContext::Create(init_flags::VIDEO);
	if (!sdl) {
		std::cerr << "sdl init failed: " << GetError().CStr() << "\n";
		return 1;
	}

	auto winRes = Window::Create("smoke-test-12", 400, 300, window_flags::HIDDEN);
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

	ren.SetDrawColor(Color::BLUE());
	ren.Clear();

	// --- getDrawColor / getDrawColorFloat ---
	ren.SetDrawColor(Color{10, 20, 30, 255});
	Color got = ren.GetDrawColor();
	std::cout << "getDrawColor=" << int(got.r) << "," << int(got.g) << "," << int(got.b) << "\n";
	if (got.r != 10 || got.g != 20 || got.b != 30) {
		std::cerr << "getDrawColor mismatch\n";
		return 1;
	}

	ren.SetDrawColor(Color::RED());

	// --- Circle / Ellipse / Arc / Pie ---
	bool ok = true;
	ok = ren.DrawCircle({100, 100}, 30) && ok;
	ok = ren.FillCircle({100, 100}, 20) && ok;
	ok = ren.DrawEllipse({200, 100}, 40, 20, 15.f) && ok;
	ok = ren.FillEllipse({200, 100}, 30, 15, 0.f) && ok;
	ok = ren.DrawArc({100, 200}, 40, 0.f, 180.f) && ok;
	ok = ren.DrawPie({100, 200}, 40, 0.f, 90.f) && ok;
	std::cout << "circle/ellipse/arc/pie ok=" << ok << "\n";
	if (!ok) {
		std::cerr << "circle/ellipse/arc/pie failed\n";
		return 1;
	}

	// --- Rounded rects ---
	ok = true;
	ok = ren.DrawRoundedRect(FRect{20, 20, 100, 60}, Corners(10.f)) && ok;
	ok = ren.FillRoundedRect(FRect{20, 100, 100, 60}, Corners(5.f, 10.f, 15.f, 20.f)) && ok;
	ok = ren.DrawRoundedBorderedRect(FRect{150, 20, 100, 60}, Sides(3.f), Corners(8.f)) && ok;
	std::cout << "rounded rects ok=" << ok << "\n";
	if (!ok) {
		std::cerr << "rounded rects failed\n";
		return 1;
	}

	// --- Polygon ---
	std::vector<FPoint> triangle = {{300, 20}, {350, 20}, {325, 60}};
	ok = ren.DrawPolygon(triangle);
	ok = ren.FillPolygon(triangle) && ok;
	std::cout << "polygon ok=" << ok << "\n";
	if (!ok) {
		std::cerr << "polygon failed\n";
		return 1;
	}

	// --- Geometry (raw triangle) ---
	FColor c = ren.GetDrawColorFloat();
	// `sdl3::Vertex` et non `SDL_Vertex` : `RenderGeometry` prend le type
	// enveloppé (le wrapper n'expose aucune structure C brute dans ses
	// signatures), et ce test ne compilait plus depuis ce durcissement.
	std::array<Vertex, 3> verts{{
		{{10.f, 10.f}, c, {0.f, 0.f}},
		{{40.f, 10.f}, c, {0.f, 0.f}},
		{{25.f, 40.f}, c, {0.f, 0.f}},
	}};
	ok = ren.RenderGeometry(verts);
	std::cout << "renderGeometry ok=" << ok << "\n";
	if (!ok) {
		std::cerr << "renderGeometry failed\n";
		return 1;
	}

	// --- Bezier ---
	std::vector<FPoint> ctrl = {{10, 250}, {100, 200}, {200, 300}, {300, 250}};
	ok = ren.DrawBezier(ctrl, 0.05f);
	std::cout << "bezier ok=" << ok << "\n";
	if (!ok) {
		std::cerr << "bezier failed\n";
		return 1;
	}

	// --- shapes:: point generators (usage direct, sans Renderer) ---
	auto circlePts = shapes::GenerateCirclePoints({0, 0}, 10.f);
	auto ellipsePts = shapes::GenerateEllipsePoints({0, 0}, 10.f, 5.f, 0.f);
	auto arcPts = shapes::GenerateArcPoints({0, 0}, 10.f, 0.f, 90.f);
	std::cout << "circlePts=" << circlePts.size() << " ellipsePts=" << ellipsePts.size() << " arcPts=" << arcPts.size()
			  << "\n";
	if (circlePts.empty() || ellipsePts.empty() || arcPts.empty()) {
		std::cerr << "point generators returned empty\n";
		return 1;
	}

	ren.Present();
	std::cout << "smoke test 12 done\n";
	return 0;
}
