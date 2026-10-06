/**
 * ui_plot_demo — galerie interactive du widget de plots (ui/plot.hpp),
 * un widget par famille de graphique proposée par le chantier :
 *
 *   Rangée 1 : Ligne + Aire + barres d'erreur · Barres/Histogramme/Lollipop
 *              (Bar/BarH/Histogram/Stem) · Nuage de points + axe Y secondaire
 *              · Camembert (PlotMode::PIE)
 *   Rangée 2 : Heatmap (PlotMode::HEATMAP) · Chandelier OHLC
 *              (PlotMode::CANDLE) · onCustomDraw (échappatoire de rendu
 *              100% personnalisé, dégradé de couleur par segment)
 *
 * Interactif sur les plots XY/Candle : glisser = pan, molette = zoom (par
 * axe si survolée sur sa bande de graduation, combiné sinon), clic-droit
 * glissé = zoom-rectangle, double-clic = retour à l'ajustement automatique,
 * survol/clic sur une entrée de légende = surbrillance/bascule visible.
 *
 *   make examples && ./build/examples/ui_plot_demo
 */
#include <cmath>
#include <format>
#include <iostream>
#include <vector>

#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 1320;
static constexpr int WIN_H = 700; // 640 de contenu + barres de titre et d'état
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
	auto engRes = sdl3::TextEngine::Create(ren);
	if (!engRes) {
		std::cerr << "TextEngine: " << engRes.Error().CStr() << "\n";
		return 1;
	}
	auto &eng = engRes.Value();

	ecs::ArchetypeRegistry ar;
	ui::Ui gui(ar, window, ren);
	ui::UiFactory &f = gui.Factory();

	gui.SetTextEngine(eng, font);
	gui.Layout().measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};

	constexpr float K_PLOT_W = 300.f, K_PLOT_H = 220.f;

	// ── 1. Ligne + Aire + barres d'erreur Y ─────────────────────────────────
	float lx[] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f};
	float ly[] = {2.f, 3.f, 2.5f, 4.f, 3.5f, 5.f, 4.5f, 6.f};
	float lerr[] = {0.3f, 0.4f, 0.2f, 0.5f, 0.3f, 0.4f, 0.3f, 0.5f};
	float ay[] = {1.f, 1.5f, 1.2f, 2.f, 1.8f, 2.5f, 2.2f, 3.f};
	auto plot1 = f.Plot("Ligne + Aire + Erreurs");
	plot1.AddLineSeriesXy(lx, ly, "mesures", Some(sdl3::FColor::UI_ACCENT_BLUE_BRIGHT())).SetYError(lerr);
	plot1.AddAreaSeries(lx, ay, "tendance", Some(sdl3::FColor{80/255.f, 200/255.f, 120/255.f, 255/255.f}), 0.3f);
	plot1.Size(K_PLOT_W, K_PLOT_H);

	// ── 2. Barres + Lollipop (Stem) ──────────────────────────────────────────
	// BarH/Histogram ne sont pas mélangés ici : BarH a une convention d'axes
	// différente (x[i]=longueur, y[i]=catégorie), Histogram est visuellement
	// presque identique à Bar (juste jointif) — les deux déjà démontrés
	// isolément lors de la Phase 1 (tests), pas répétés ici.
	float bx[] = {0.f, 1.f, 2.f, 3.f, 4.f};
	float by[] = {3.f, 5.f, 2.f, 6.f, 4.f};
	float hy[] = {2.f, 4.f, 1.f, 3.f, 5.f};
	auto plot2 = f.Plot("Bar + Stem");
	plot2.AddBarSeries(bx, by, "bar", Some(sdl3::FColor{235/255.f, 155/255.f, 60/255.f, 255/255.f}));
	plot2.AddStemSeries(bx, hy, "stem", Some(sdl3::FColor::UI_ACCENT_BLUE_BRIGHT()));
	plot2.Size(K_PLOT_W, K_PLOT_H);

	// ── 3. Nuage de points + axe Y secondaire ───────────────────────────────
	// Formes délibérément DIVERGENTES (primaire monte, secondaire descend) et
	// non proportionnelles entre elles — sinon les deux axes auto-ajustés
	// finissent proportionnellement identiques et le rendu de l'axe
	// secondaire, bien que correct, devient visuellement indiscernable du
	// primaire (piège rencontré en écrivant cet exemple).
	float sx[] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f};
	float sy1[] = {1.f, 3.f, 2.f, 4.f, 3.f, 5.f};
	float sy2[] = {480.f, 220.f, 350.f, 150.f, 300.f, 90.f};
	auto plot3 = f.Plot("Nuage + axe Y secondaire");
	plot3.AddScatterSeries(sx, sy1, "primaire", Some(sdl3::FColor::UI_ACCENT_BLUE_BRIGHT()));
	plot3.AddScatterSeries(sx, sy2, "secondaire", Some(sdl3::FColor{225/255.f, 90/255.f, 80/255.f, 255/255.f})).UseSecondaryY();
	plot3.Size(K_PLOT_W, K_PLOT_H);

	// ── 4. Camembert ─────────────────────────────────────────────────────────
	// Légende à droite en espace RÉSERVÉ (pas le TopRight superposé par
	// défaut) : sur un plot presque carré, la légende par défaut chevauche le
	// titre centré en haut — piège de mise en page rencontré en écrivant cet
	// exemple, pas un bug du widget (déjà son comportement documenté depuis
	// la Phase 2 : superposition = choix explicite, pas toujours adapté).
	auto plot4 = f.Pie("Répartition");
	plot4.AddPieSlice(35.f, "Rouge", Some(sdl3::FColor{220/255.f, 50/255.f, 50/255.f, 255/255.f}));
	plot4.AddPieSlice(25.f, "Vert", Some(sdl3::FColor{50/255.f, 200/255.f, 80/255.f, 255/255.f}));
	plot4.AddPieSlice(20.f, "Bleu", Some(sdl3::FColor{60/255.f, 100/255.f, 220/255.f, 255/255.f}));
	plot4.AddPieSlice(20.f, "Jaune", Some(sdl3::FColor{230/255.f, 200/255.f, 50/255.f, 255/255.f}));
	plot4.Legend(ui::LegendPosition::Right, false);
	plot4.Size(K_PLOT_W, K_PLOT_H);

	// ── 5. Heatmap ───────────────────────────────────────────────────────────
	float hvals[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.2f, 0.4f, 0.6f, 0.8f, 0.9f, 0.6f, 0.3f, 0.1f};
	auto plot5 = f.Heatmap("Heatmap (Viridis)");
	plot5.SetHeatmapData(3, 4, hvals, ui::Colormap::VIRIDIS);
	plot5.Size(K_PLOT_W, K_PLOT_H);

	// ── 6. Chandelier OHLC ───────────────────────────────────────────────────
	auto plot6 = f.Candlestick("Chandelier");
	plot6.AddOhlcBar(0.f, 100.f, 110.f, 95.f, 108.f);
	plot6.AddOhlcBar(1.f, 108.f, 112.f, 100.f, 102.f);
	plot6.AddOhlcBar(2.f, 102.f, 120.f, 101.f, 118.f);
	plot6.AddOhlcBar(3.f, 118.f, 122.f, 110.f, 112.f);
	plot6.AddOhlcBar(4.f, 112.f, 130.f, 111.f, 128.f);
	plot6.Size(K_PLOT_W, K_PLOT_H);

	// ── 7. onCustomDraw : dégradé de couleur par segment ────────────────────
	// L'échappatoire reçoit le rectangle de tracé ET les axes résolus — libre
	// à l'appelant de convertir données→écran lui-même via dataToScreen(),
	// exactement comme le ferait RenderSystem::drawWidget en interne.
	static std::vector<float> gradX, gradY;
	gradX.resize(40);
	gradY.resize(40);
	for (int i = 0; i < 40; ++i) {
		float t = float(i) / 39.f;
		gradX[i] = t * 10.f;
		gradY[i] = std::sin(t * 6.2831853f * 1.5f) * 2.f + std::cos(t * 6.2831853f * 0.5f);
	}
	auto plot7 = f.Plot("onCustomDraw (degrade)");
	plot7.AddLineSeriesXy(gradX, gradY, "degrade");
	plot7.SetOnCustomDraw([](sdl3::Renderer &r, const sdl3::FRect &plotRect, const ui::PlotSeries &s,
							 const ui::PlotAxis &xAxis, const ui::PlotAxis &yAxis) {
		size_t n = sdl3::Min(s.x.size(), s.y.size());
		for (size_t i = 0; i + 1 < n; ++i) {
			float t = float(i) / float(n > 1 ? n - 1 : 1);
			sdl3::FColor c(60.f + t * 180.f, 120.f - t * 80.f, 220.f - t * 160.f, 1.0f);
			float x0 = ui::DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, s.x[i]);
			float y0 = ui::DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, s.y[i]);
			float x1 = ui::DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, s.x[i + 1]);
			float y1 = ui::DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, s.y[i + 1]);
			r.SetDrawColor(c);
			r.DrawLine(x0, y0, x1, y1);
			r.FillCircle({x0, y0}, 2.5f);
		}
	});
	plot7.Size(K_PLOT_W, K_PLOT_H);

	// ── Encadrement de fenêtre (barre de titre, barre d'état) ────────────────
	ui::WindowFrame frame;
	frame.Build(gui, window, {.title = "ui:: - galerie de plots", .appIcon = Some(ui::MaterialIcons::SHOW_CHART),
							  .status = "7 graphiques interactifs."});

	// ── Mise en page : titre + instructions, puis 2 rangées ─────────────────
	auto root = f.Column();
	root.Gap(12.f).Pad(16.f).GrowW().GrowH().Parent(frame.Content());
	root.Children(
		f.Label("ui:: — galerie de plots 2D").FontSize(20.f),
		f.Label("Pan : glisser · Zoom : molette (par axe sur la bande de graduation, combiné sinon) · "
				 "Zoom-rectangle : clic-droit glissé · Reset : double-clic · Légende : survol/clic")
			.FontSize(12.f)
			.TextColor(sdl3::Color{150, 156, 178}),
		f.Separator(),
		f.Row().Gap(14.f).Children(std::move(plot1), std::move(plot2), std::move(plot3), std::move(plot4)),
		f.Row().Gap(14.f).Children(std::move(plot5), std::move(plot6), std::move(plot7)));
	root.Spawn();

	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}
			gui.HandleEvent(e);
		}
		gui.Tick(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		ren.SetDrawColor(sdl3::FColor::UI_APP_BG());
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	return 0;
}
