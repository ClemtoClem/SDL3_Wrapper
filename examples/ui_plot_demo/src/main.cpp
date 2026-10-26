/**
 * ui_plot_demo — galerie interactive du widget de graphiques (ui/plot.hpp),
 * un widget par famille :
 *
 *   Rangée 1 : Ligne + Aire + barres d'erreur · Barres + Lollipop (Stem) ·
 *              Nuage de points + axe Y secondaire · Camembert
 *   Rangée 2 : Heatmap · Chandelier OHLC · dessin personnalisé (dégradé de
 *              couleur par segment, via Painter)
 *
 * Interactions sur les graphiques XY / Candle : glisser = pan, molette = zoom
 * (par axe sur sa bande de graduation, combiné sinon), clic droit glissé =
 * zoom rectangle, double clic = ajustement automatique, survol / clic sur la
 * légende = surbrillance / bascule de visibilité.
 *
 *   cpp-maker run ui_plot_demo
 */
#include <cmath>
#include <iostream>
#include <vector>

#include <core/core.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 1320;
static constexpr int WIN_H = 700;

static constexpr const char *PLOT_CSS = R"css(
#gallery { padding: 16px; gap: 12px; }
#gallery > row { gap: 14px; }
plot { width: 300px; height: 220px; }
#hint { font-size: 12px; color: var(--muted); }
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
	auto winRes = sdl3::Window::Create(u8"ui:: - galerie de plots", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
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
	ui::Ui gui(world, window, ren);
	gui.AddStyleSheet(PLOT_CSS);

	const ui::Color blue = ui::Rgba(90, 160, 255), green = ui::Rgba(80, 200, 120), orange = ui::Rgba(235, 155, 60),
					red = ui::Rgba(225, 90, 80);

	// ── 1. Ligne + Aire + barres d'erreur Y ─────────────────────────────────
	const float lx[] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f};
	const float ly[] = {2.f, 3.f, 2.5f, 4.f, 3.5f, 5.f, 4.5f, 6.f};
	const float lerr[] = {0.3f, 0.4f, 0.2f, 0.5f, 0.3f, 0.4f, 0.3f, 0.5f};
	const float ay[] = {1.f, 1.5f, 1.2f, 2.f, 1.8f, 2.5f, 2.2f, 3.f};
	ui::PlotBuilder plot1 = ui::Plot("Ligne + Aire + Erreurs");
	plot1.AddLineSeriesXy(lx, ly, "mesures", Some(blue)).SetYError(lerr);
	plot1.AddAreaSeries(lx, ay, "tendance", Some(green), 0.3f);

	// ── 2. Barres + Lollipop (Stem) ──────────────────────────────────────────
	const float bx[] = {0.f, 1.f, 2.f, 3.f, 4.f};
	const float by[] = {3.f, 5.f, 2.f, 6.f, 4.f};
	const float hy[] = {2.f, 4.f, 1.f, 3.f, 5.f};
	ui::PlotBuilder plot2 = ui::Plot("Bar + Stem");
	plot2.AddBarSeries(bx, by, "bar", Some(orange)).AddStemSeries(bx, hy, "stem", Some(blue));

	// ── 3. Nuage de points + axe Y secondaire (formes divergentes) ──────────
	const float sx[] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f};
	const float sy1[] = {1.f, 3.f, 2.f, 4.f, 3.f, 5.f};
	const float sy2[] = {480.f, 220.f, 350.f, 150.f, 300.f, 90.f};
	ui::PlotBuilder plot3 = ui::Plot("Nuage + axe Y secondaire");
	plot3.AddScatterSeries(sx, sy1, "primaire", Some(blue));
	plot3.AddScatterSeries(sx, sy2, "secondaire", Some(red)).UseSecondaryY();

	// ── 4. Camembert (légende à droite, en espace réservé) ──────────────────
	ui::PlotBuilder plot4 = ui::Pie("Répartition");
	plot4.AddPieSlice(35.f, "Rouge", Some(ui::Rgba(220, 50, 50)))
		.AddPieSlice(25.f, "Vert", Some(ui::Rgba(50, 200, 80)))
		.AddPieSlice(20.f, "Bleu", Some(ui::Rgba(60, 100, 220)))
		.AddPieSlice(20.f, "Jaune", Some(ui::Rgba(230, 200, 50)))
		.Legend(ui::LegendPosition::Right, false);

	// ── 5. Heatmap ───────────────────────────────────────────────────────────
	const float hvals[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.2f, 0.4f, 0.6f, 0.8f, 0.9f, 0.6f, 0.3f, 0.1f};
	ui::PlotBuilder plot5 = ui::Heatmap("Heatmap (Viridis)");
	plot5.SetHeatmapData(3, 4, hvals, ui::Colormap::VIRIDIS);

	// ── 6. Chandelier OHLC ───────────────────────────────────────────────────
	ui::PlotBuilder plot6 = ui::Candlestick("Chandelier");
	plot6.AddOhlcBar(0.f, 100.f, 110.f, 95.f, 108.f)
		.AddOhlcBar(1.f, 108.f, 112.f, 100.f, 102.f)
		.AddOhlcBar(2.f, 102.f, 120.f, 101.f, 118.f)
		.AddOhlcBar(3.f, 118.f, 122.f, 110.f, 112.f)
		.AddOhlcBar(4.f, 112.f, 130.f, 111.f, 128.f);

	// ── 7. Dessin personnalisé : dégradé de couleur par segment ─────────────
	std::vector<float> gradX(40), gradY(40);
	for (size_t i = 0; i < 40; ++i) {
		const float t = float(i) / 39.f;
		gradX[i] = t * 10.f;
		gradY[i] = std::sin(t * 6.2831853f * 1.5f) * 2.f + std::cos(t * 6.2831853f * 0.5f);
	}
	ui::PlotBuilder plot7 = ui::Plot("Dessin personnalisé (dégradé)");
	plot7.AddLineSeriesXy(gradX, gradY, "dégradé")
		.SetOnCustomDraw([](ui::Painter &p, const sdl3::FRect &r, const ui::PlotSeries &s, const ui::PlotAxis &xAxis,
							const ui::PlotAxis &yAxis) {
			const size_t n = std::min(s.x.size(), s.y.size());
			for (size_t i = 0; i + 1 < n; ++i) {
				const float t = float(i) / float(n > 1 ? n - 1 : 1);
				const ui::Color c{(60.f + t * 180.f) / 255.f, (120.f - t * 80.f) / 255.f, (220.f - t * 160.f) / 255.f,
								  1.f};
				const sdl3::FPoint a{ui::DataToScreen(xAxis, r.x, r.x + r.w, s.x[i]),
									 ui::DataToScreen(yAxis, r.y + r.h, r.y, s.y[i])};
				const sdl3::FPoint b{ui::DataToScreen(xAxis, r.x, r.x + r.w, s.x[i + 1]),
									 ui::DataToScreen(yAxis, r.y + r.h, r.y, s.y[i + 1])};
				p.Line(a, b, 2.f, c);
				p.FillCircle(a, 2.5f, c);
			}
		});

	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "ui:: - galerie de plots", .appIcon = Some(ui::MaterialIcons::SHOW_CHART),
				 .status = "7 graphiques interactifs."});
	frame.Content().Append(ui::Column().Id("gallery").Grow().FillWidth().Children({
		ui::Label("ui:: — galerie de plots 2D").FontSize(20.f),
		ui::Label("Pan : glisser · Zoom : molette (par axe sur la bande de graduation, combiné sinon) · "
				  "Zoom rectangle : clic droit glissé · Reset : double clic · Légende : survol / clic")
			.Id("hint"),
		ui::Separator(),
		ui::Row().Children({plot1, plot2, plot3, plot4}),
		ui::Row().Children({plot5, plot6, plot7}),
	}));

	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		const uint64_t now = sdl3::GetTicksMS();
		const float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}
			gui.HandleEvent(e);
		}
		gui.Update(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		ren.SetDrawColor(gui.CurrentPalette().appBg);
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	return 0;
}
