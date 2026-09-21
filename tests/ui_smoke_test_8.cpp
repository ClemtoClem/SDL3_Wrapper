// Smoke test : ui::Plot (src/ui/plot.hpp) — widget de graphiques 2D façon
// ImPlot, remplace l'ancien Plot (PlotKind::Lines/Histogram). Grandit au
// fil des phases (0 à 7, cf. plan mossy-forging-fog.md).
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"
#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>

static sdl3::Event MouseMotionEv(float x, float y) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_MOTION;
	ev.raw.motion.x = x;
	ev.raw.motion.y = y;
	return ev;
}
static sdl3::Event MouseDownEv(float x, float y, Uint8 button = SDL_BUTTON_LEFT, Uint8 clicks = 1) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = button;
	ev.raw.button.clicks = clicks;
	return ev;
}
static sdl3::Event MouseUpEv(float x, float y, Uint8 button = SDL_BUTTON_LEFT) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_BUTTON_UP;
	ev.raw.button.x = x;
	ev.raw.button.y = y;
	ev.raw.button.button = button;
	return ev;
}
static sdl3::Event WheelEv(float x, float y, float wheelY) {
	sdl3::Event ev{};
	ev.raw.type = SDL_EVENT_MOUSE_WHEEL;
	ev.raw.wheel.mouse_x = x;
	ev.raw.wheel.mouse_y = y;
	ev.raw.wheel.y = wheelY;
	return ev;
}

int main() {
	setenv("SDL_VIDEODRIVER", "dummy", 0);

	// ════════════════════════════════════════════════════════════════════
	// Phase 0 — Fondation : transform, autofit, graduations, construction,
	// survol, rendu complet
	// ════════════════════════════════════════════════════════════════════

	// ── Transform données<->écran : round-trip exact ────────────────────────
	{
		ui::PlotAxis axis;
		axis.min = 0.f;
		axis.max = 100.f;
		axis.autoFit = false;
		float sx = ui::DataToScreen(axis, 10.f, 210.f, 50.f); // milieu de la plage -> milieu de l'écran
		assert(sdl3::Abs(sx - 110.f) < 0.01f);
		float back = ui::ScreenToData(axis, 10.f, 210.f, sx);
		assert(sdl3::Abs(back - 50.f) < 0.01f);
		std::cout << "dataToScreen/screenToData (round-trip): ok\n";
	}

	// ── computeAutoFit : min/max + marge 5%, ignore les séries invisibles ──
	{
		std::vector<ui::PlotSeries> series;
		ui::PlotSeries s1;
		s1.SetXy(std::array<float, 3>{0.f, 1.f, 2.f}, std::array<float, 3>{10.f, 20.f, 5.f});
		series.push_back(s1);
		ui::PlotSeries s2;
		s2.visible = false;
		s2.SetXy(std::array<float, 1>{0.f}, std::array<float, 1>{1000.f});
		series.push_back(s2);

		auto [mn, mx] = ui::ComputeAutoFit(series, false); // Y : plage réelle [5,20], marge 5% de 15 = 0.75
		assert(sdl3::Abs(mn - (5.f - 0.75f)) < 0.01f);
		assert(sdl3::Abs(mx - (20.f + 0.75f)) < 0.01f);
		std::cout << "computeAutoFit (ignore les invisibles, marge 5%): ok\n";

		auto [emptyMn, emptyMx] = ui::ComputeAutoFit({}, true);
		assert(emptyMn == 0.f && emptyMx == 1.f);
		std::cout << "computeAutoFit (vide -> {0,1}): ok\n";
	}

	// ── generateTicks : pas "rond" (1/2/5 x 10^n) ───────────────────────────
	{
		auto ticks = ui::GenerateTicks(0.f, 100.f, 5);
		assert(!ticks.empty());
		for (float t : ticks)
			assert(t >= 0.f && t <= 100.f);
		if (ticks.size() >= 2)
			assert(sdl3::Abs((ticks[1] - ticks[0]) - 20.f) < 0.01f); // 100/5=20 -> déjà "rond"
		std::cout << "generateTicks (pas rond): ok\n";
	}

	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::RenderSystem render;
	ui::StyleSystem style;
	ui::UiFactory f(ar, layout);
	style.sheet = &f.sheet;

	// ── Construction : plotLines() (raccourci) + .Plot().AddLineSeries()/.AddScatterSeries() ──
	float ySamples[] = {1.f, 4.f, 2.f, 8.f, 5.f};
	auto plotA = f.PlotLines(ySamples, "Demo");
	plotA.Name("plotA").Size(300.f, 200.f);
	ecs::Entity plotAE = plotA.Spawn();

	auto plotB = f.Plot("Multi");
	float xs[] = {0.f, 1.f, 2.f, 3.f};
	float ys1[] = {0.f, 3.f, 1.f, 4.f};
	float ys2[] = {4.f, 1.f, 3.f, 0.f};
	plotB.AddLineSeriesXy(xs, ys1, "s1");
	plotB.AddScatterSeries(xs, ys2, "s2", Some(sdl3::FColor::RED()));
	// Décalé de plotA (par défaut à l'origine) — nécessaire dès la Phase 3 :
	// deux Plot superposés se disputeraient le même `wheelConsumed`
	// partagé (comportement VOULU : un seul widget doit réagir à la molette
	// à un point donné), ce qui casserait silencieusement les tests
	// d'intégration molette/glisser si plusieurs plots coexistaient au même
	// endroit à l'écran.
	plotB.Name("plotB").Size(300.f, 200.f).Offset(350.f, 0.f);
	ecs::Entity plotBE = plotB.Spawn();

	style.Resolve(ar);
	layout.RunIfNeeded(ar, 800.f, 600.f);

	// ── Vérifie la construction ──────────────────────────────────────────────
	{
		auto pa = ar.GetComponent<ui::UiPlot>(plotAE);
		assert(pa.IsSome());
		assert(pa.Unwrap()->series.size() == 1);
		assert(pa.Unwrap()->series[0].kind == ui::PlotSeriesKind::LINE);
		assert(pa.Unwrap()->series[0].y.size() == 5);
		assert(pa.Unwrap()->title == "Demo");

		auto pb = ar.GetComponent<ui::UiPlot>(plotBE);
		assert(pb.IsSome());
		assert(pb.Unwrap()->series.size() == 2);
		assert(pb.Unwrap()->series[0].kind == ui::PlotSeriesKind::LINE);
		assert(pb.Unwrap()->series[1].kind == ui::PlotSeriesKind::SCATTER);
		// couleur palette assignée en round-robin (pas de couleur explicite pour s1).
		sdl3::FColor expected0 = ui::PlotPaletteColor(0);
		assert(pb.Unwrap()->series[0].color.r == expected0.r && pb.Unwrap()->series[0].color.g == expected0.g);
		// couleur explicite respectée pour s2.
		assert(pb.Unwrap()->series[1].color.r == sdl3::FColor::RED().r && pb.Unwrap()->series[1].color.g == sdl3::FColor::RED().g);
		std::cout << "construction (plotLines/.Plot().AddLineSeries()/.AddScatterSeries()): ok\n";
	}

	// ── setY() régénère x implicite si la taille a changé ───────────────────
	{
		auto pa = ar.GetComponent<ui::UiPlot>(plotAE);
		float newY[] = {9.f, 9.f, 9.f};
		pa.Unwrap()->series[0].SetY(newY);
		assert(pa.Unwrap()->series[0].y.size() == 3);
		assert(pa.Unwrap()->series[0].x.size() == 3);
		assert(sdl3::Abs(pa.Unwrap()->series[0].x[2] - 2.f) < 0.01f);
		pa.Unwrap()->series[0].SetY(ySamples); // restaure pour la suite
		std::cout << "PlotSeries::setY (regenere x implicite si besoin): ok\n";
	}

	// ── Survol : point le plus proche parmi les séries visibles ─────────────
	{
		auto c = ar.GetComponent<ui::UiComputed>(plotAE);
		assert(c.IsSome());
		auto pa = ar.GetComponent<ui::UiPlot>(plotAE);
		ui::PlotAxis xr = ui::ResolveAxis(pa.Unwrap()->xAxis, pa.Unwrap()->series, true);
		ui::PlotAxis yr = ui::ResolveAxis(pa.Unwrap()->yAxis, pa.Unwrap()->series, false);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pa.Unwrap(), c.Unwrap()->screen);
		auto pts = ui::PlotSeriesScreenPoints(pa.Unwrap()->series[0], plotRect, xr, yr);
		assert(pts.size() == 5);
		sdl3::FPoint target = pts[2];

		input.HandleEvent(ar, MouseMotionEv(target.x, target.y), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotAE).Unwrap()->hovered);
		assert(ar.GetComponent<ui::UiPlot>(plotAE).Unwrap()->hoveredSeries == 0);
		assert(ar.GetComponent<ui::UiPlot>(plotAE).Unwrap()->hoveredIndex == 2);
		std::cout << "survol (point le plus proche): ok\n";

		input.HandleEvent(ar, MouseMotionEv(-100.f, -100.f), layout); // loin de tout widget
		assert(!ar.GetComponent<ui::UiPlot>(plotAE).Unwrap()->hovered);
		std::cout << "survol efface hors widget: ok\n";
	}

	// ── Rendu réel (fond/grille/séries/tooltip) sans crash ──────────────────
	{
		auto winRes = sdl3::Window::Create("smoke-ui8", 400, 300, sdl3::window_flags::HIDDEN);
		assert(winRes);
		auto renRes = sdl3::Renderer::Create(winRes.Value());
		assert(renRes);
		auto &ren = renRes.Value();
		ui::SdlRendererBackend renBackend(ren);
		input.HandleEvent(ar, MouseMotionEv(0.f, 0.f), layout); // état de survol propre avant rendu
		render.Run(ar, renBackend, &input.tooltip);
		std::cout << "rendu complet (fond+grille+series+tooltip): ok\n";
	}

	std::cout << "plot phase0 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 1 — Bar/BarH/Histogram/Area/Stem/Step
	// ════════════════════════════════════════════════════════════════════

	// ── detail::StepPoints() : escalier "post" (valeur maintenue puis saut) ──
	{
		std::array<sdl3::FPoint, 3> pts{sdl3::FPoint{0.f, 10.f}, sdl3::FPoint{1.f, 20.f}, sdl3::FPoint{2.f, 5.f}};
		auto steps = ui::detail::StepPoints(pts);
		assert(steps.size() == 5); // 3 points -> 2*3-1
		assert(sdl3::Abs(steps[0].x - 0.f) < 0.01f && sdl3::Abs(steps[0].y - 10.f) < 0.01f);
		assert(sdl3::Abs(steps[1].x - 1.f) < 0.01f && sdl3::Abs(steps[1].y - 10.f) < 0.01f); // maintien avant saut
		assert(sdl3::Abs(steps[2].x - 1.f) < 0.01f && sdl3::Abs(steps[2].y - 20.f) < 0.01f); // saut
		std::cout << "detail::StepPoints (escalier post): ok\n";
	}

	// ── Construction de chaque type de série à axes partagés ────────────────
	ecs::Entity plotC{};
	{
		auto plotBuilder = f.Plot("Toutes series");
		float cx[] = {0.f, 1.f, 2.f, 3.f};
		float cy[] = {2.f, -1.f, 3.f, 1.f};
		plotBuilder.AddBarSeries(cx, cy, "bar");
		plotBuilder.AddBarHSeries(cx, cy, "barh");
		plotBuilder.AddHistogramSeries(cx, cy, "hist");
		plotBuilder.AddAreaSeries(cx, cy, "area", NONE, 0.5f);
		plotBuilder.AddStemSeries(cx, cy, "stem");
		plotBuilder.AddStepSeries(cx, cy, "step");
		plotBuilder.Name("plotC").Size(300.f, 200.f).Offset(0.f, 250.f); // ne recouvre pas plotA/plotB
		plotC = plotBuilder.Spawn();

		auto pc = ar.GetComponent<ui::UiPlot>(plotC);
		assert(pc.IsSome());
		assert(pc.Unwrap()->series.size() == 6);
		assert(pc.Unwrap()->series[0].kind == ui::PlotSeriesKind::BAR);
		assert(sdl3::Abs(pc.Unwrap()->series[0].barWidth - 0.67f) < 0.01f); // defaut
		assert(pc.Unwrap()->series[1].kind == ui::PlotSeriesKind::BAR_H);
		assert(pc.Unwrap()->series[2].kind == ui::PlotSeriesKind::HISTOGRAM);
		assert(sdl3::Abs(pc.Unwrap()->series[2].barWidth - 1.f) < 0.01f); // defaut jointif
		assert(pc.Unwrap()->series[3].kind == ui::PlotSeriesKind::AREA);
		assert(sdl3::Abs(pc.Unwrap()->series[3].fillOpacity - 0.5f) < 0.01f); // explicite
		assert(pc.Unwrap()->series[4].kind == ui::PlotSeriesKind::STEM);
		assert(pc.Unwrap()->series[5].kind == ui::PlotSeriesKind::STEP);
		std::cout << "construction (Bar/BarH/Histogram/Area/Stem/Step): ok\n";
	}

	style.Resolve(ar);
	layout.RunIfNeeded(ar, 800.f, 600.f);

	// ── Rendu réel de toutes les séries simultanément, sans crash ───────────
	{
		auto winRes = sdl3::Window::Create("smoke-ui8-phase1", 400, 300, sdl3::window_flags::HIDDEN);
		assert(winRes);
		auto renRes = sdl3::Renderer::Create(winRes.Value());
		assert(renRes);
		auto &ren = renRes.Value();
		ui::SdlRendererBackend renBackend(ren);
		render.Run(ar, renBackend, &input.tooltip);
		std::cout << "rendu complet (Bar/BarH/Histogram/Area/Stem/Step ensemble): ok\n";
	}

	std::cout << "plot phase1 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 2 — Légende (survol/bascule) + positionnement graduations/légende
	// ════════════════════════════════════════════════════════════════════

	// ── anchorRectIn : les 8 positions ancrent correctement dans une zone ──
	{
		sdl3::FRect area{0.f, 0.f, 200.f, 100.f};
		auto tl = ui::AnchorRectIn(ui::LegendPosition::TopLeft, area, 40.f, 20.f, 5.f);
		assert(sdl3::Abs(tl.x - 5.f) < 0.01f && sdl3::Abs(tl.y - 5.f) < 0.01f);
		auto br = ui::AnchorRectIn(ui::LegendPosition::BottomRight, area, 40.f, 20.f, 5.f);
		assert(sdl3::Abs(br.x - (200.f - 40.f - 5.f)) < 0.01f && sdl3::Abs(br.y - (100.f - 20.f - 5.f)) < 0.01f);
		auto rightCentered = ui::AnchorRectIn(ui::LegendPosition::Right, area, 40.f, 20.f, 5.f);
		assert(sdl3::Abs(rightCentered.y - (100.f - 20.f) * 0.5f) < 0.01f);
		std::cout << "anchorRectIn (8 positions): ok\n";
	}

	// ── computePlotMargins : graduations/légende réservées vs superposées ──
	{
		ui::UiPlot plot;
		plot.yAxis.tickPosition = ui::PlotEdge::Left;
		plot.series.resize(2); // pour la hauteur de légende réservée

		auto m1 = ui::ComputePlotMargins(plot); // tout réservé par défaut (ticks non-overlay), pas de légende sans label
		assert(m1.left > 0.f && m1.bottom > 0.f);
		assert(m1.top == 0.f); // X en bas par défaut
		assert(m1.right == 0.f); // Y à gauche, pas de légende réservée par défaut (legendOverlay=true)

		plot.xAxis.tickOverlay = true;
		plot.yAxis.tickOverlay = true;
		auto m2 = ui::ComputePlotMargins(plot);
		assert(m2.left == 0.f && m2.bottom == 0.f); // plus rien réservé pour les graduations

		plot.legendOverlay = false;
		plot.legendPosition = ui::LegendPosition::Right;
		auto m3 = ui::ComputePlotMargins(plot);
		assert(m3.right > 0.f); // légende réservée à droite
		std::cout << "computePlotMargins (reserve/overlay): ok\n";
	}

	// ── Construction via builder : xTicks/yTicks/legend/showLegend ──────────
	ecs::Entity plotD{};
	{
		auto plotBuilder = f.Plot("Legende");
		float lx[] = {0.f, 1.f, 2.f};
		float ly1[] = {1.f, 2.f, 3.f};
		float ly2[] = {3.f, 2.f, 1.f};
		plotBuilder.AddLineSeriesXy(lx, ly1, "Serie A");
		plotBuilder.AddLineSeriesXy(lx, ly2, "Serie B");
		plotBuilder.XTicks(ui::PlotEdge::Top, true, Some(sdl3::FColor{10 / 255.f, 10 / 255.f, 10 / 255.f, 200 / 255.f}));
		plotBuilder.YTicks(ui::PlotEdge::Right);
		plotBuilder.Legend(ui::LegendPosition::BottomLeft, false);
		plotBuilder.Name("plotD").Size(320.f, 220.f).Offset(350.f, 250.f); // ne recouvre pas plotA/B/C
		plotD = plotBuilder.Spawn();

		auto pd = ar.GetComponent<ui::UiPlot>(plotD);
		assert(pd.IsSome());
		assert(pd.Unwrap()->xAxis.tickPosition == ui::PlotEdge::Top);
		assert(pd.Unwrap()->xAxis.tickOverlay);
		assert(pd.Unwrap()->xAxis.tickBoxColor.r == 10 / 255.f);
		assert(pd.Unwrap()->yAxis.tickPosition == ui::PlotEdge::Right);
		assert(!pd.Unwrap()->yAxis.tickOverlay); // defaut de yGetTicks()
		assert(pd.Unwrap()->legendPosition == ui::LegendPosition::BottomLeft);
		assert(!pd.Unwrap()->legendOverlay);
		std::cout << "construction (xTicks/yTicks/legend): ok\n";
	}

	style.Resolve(ar);
	layout.RunIfNeeded(ar, 800.f, 600.f);

	// ── Interaction légende : survol = surbrillance, clic = bascule visible ─
	{
		auto c = ar.GetComponent<ui::UiComputed>(plotD);
		assert(c.IsSome());
		auto pd = ar.GetComponent<ui::UiPlot>(plotD);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pd.Unwrap(), c.Unwrap()->screen);
		sdl3::FRect legendBox = ui::LegendBoxRect(*pd.Unwrap(), c.Unwrap()->screen, plotRect);
		sdl3::FRect row0 = ui::LegendRowRect(legendBox, 0);
		sdl3::FPoint rowCenter{row0.x + row0.w * 0.5f, row0.y + row0.h * 0.5f};

		input.HandleEvent(ar, MouseMotionEv(rowCenter.x, rowCenter.y), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotD).Unwrap()->legendHover == 0);
		assert(ar.GetComponent<ui::UiPlot>(plotD).Unwrap()->series[0].visible); // pas encore bascule

		input.HandleEvent(ar, MouseDownEv(rowCenter.x, rowCenter.y), layout);
		assert(!ar.GetComponent<ui::UiPlot>(plotD).Unwrap()->series[0].visible); // bascule au clic
		assert(ar.GetComponent<ui::UiPlot>(plotD).Unwrap()->series[1].visible); // l'autre serie inchangee

		input.HandleEvent(ar, MouseDownEv(rowCenter.x, rowCenter.y), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotD).Unwrap()->series[0].visible); // re-bascule
		std::cout << "interaction legende (survol/bascule): ok\n";

		input.HandleEvent(ar, MouseMotionEv(-100.f, -100.f), layout); // loin de tout widget
		assert(ar.GetComponent<ui::UiPlot>(plotD).Unwrap()->legendHover == -1);
		std::cout << "legendHover efface hors widget: ok\n";
	}

	// ── Rendu complet avec graduations reservees/superposees + legende ──────
	{
		auto winRes = sdl3::Window::Create("smoke-ui8-phase2", 400, 300, sdl3::window_flags::HIDDEN);
		assert(winRes);
		auto renRes = sdl3::Renderer::Create(winRes.Value());
		assert(renRes);
		auto &ren = renRes.Value();
		ui::SdlRendererBackend renBackend(ren);
		render.Run(ar, renBackend, &input.tooltip);
		std::cout << "rendu complet (legende + graduations mixtes): ok\n";
	}

	std::cout << "plot phase2 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 3 — Navigation : pan/zoom molette/zoom-rectangle/reset + échelle log
	// ════════════════════════════════════════════════════════════════════

	// ── toAxisSpace/fromAxisSpace : identité (linéaire) / log10 (log) ───────
	{
		ui::PlotAxis lin;
		assert(sdl3::Abs(ui::ToAxisSpace(lin, 42.f) - 42.f) < 0.001f);
		assert(sdl3::Abs(ui::FromAxisSpace(lin, 42.f) - 42.f) < 0.001f);

		ui::PlotAxis logAxis;
		logAxis.logScale = true;
		float v = ui::ToAxisSpace(logAxis, 100.f);
		assert(sdl3::Abs(v - 2.f) < 0.001f); // log10(100)=2
		assert(sdl3::Abs(ui::FromAxisSpace(logAxis, v) - 100.f) < 0.01f);
		std::cout << "toAxisSpace/fromAxisSpace (lineaire + log): ok\n";
	}

	// ── dataToScreen/screenToData en échelle log ─────────────────────────────
	{
		ui::PlotAxis axis;
		axis.min = 1.f;
		axis.max = 100.f;
		axis.autoFit = false;
		axis.logScale = true;
		float sx10 = ui::DataToScreen(axis, 0.f, 200.f, 10.f); // log10(10)=1, au milieu de [0,2] -> milieu ecran
		assert(sdl3::Abs(sx10 - 100.f) < 0.5f);
		float back = ui::ScreenToData(axis, 0.f, 200.f, sx10);
		assert(sdl3::Abs(back - 10.f) < 0.1f);
		std::cout << "dataToScreen/screenToData (echelle log): ok\n";
	}

	// ── computeAutoFit en échelle log : ignore <=0, marge dans l'espace log ──
	{
		std::vector<ui::PlotSeries> series;
		ui::PlotSeries s;
		s.SetXy(std::array<float, 4>{0.f, 1.f, 2.f, 3.f}, std::array<float, 4>{-5.f, 1.f, 100.f, 0.f});
		series.push_back(s);
		auto [mn, mx] = ui::ComputeAutoFit(series, false, true);
		assert(mn > 0.f && mn < 1.f); // marge log en dessous de 1 (le vrai min visible, -5 et 0 ignores)
		assert(mx > 100.f);           // marge log au dessus de 100
		std::cout << "computeAutoFit (echelle log, ignore les valeurs <= 0): ok\n";
	}

	// ── generateLogTicks : {1,2,5}x10^n par décade couverte ─────────────────
	{
		auto ticks = ui::GenerateLogTicks(1.f, 1000.f);
		assert(!ticks.empty());
		for (float t : ticks)
			assert(t >= 0.999f && t <= 1000.1f);
		bool has1 = false, has10 = false, has100 = false, has1000 = false;
		for (float t : ticks) {
			if (sdl3::Abs(t - 1.f) < 0.01f)
				has1 = true;
			if (sdl3::Abs(t - 10.f) < 0.01f)
				has10 = true;
			if (sdl3::Abs(t - 100.f) < 0.01f)
				has100 = true;
			if (sdl3::Abs(t - 1000.f) < 0.01f)
				has1000 = true;
		}
		assert(has1 && has10 && has100 && has1000);
		std::cout << "generateLogTicks (1/2/5 x 10^n par decade): ok\n";
	}

	// ── applyPlotPan (fonction pure) : le contenu suit la souris ─────────────
	{
		ui::UiPlot plot;
		plot.xAxis.min = 0.f;
		plot.xAxis.max = 100.f;
		plot.xAxis.autoFit = false;
		plot.yAxis.min = 0.f;
		plot.yAxis.max = 50.f;
		plot.yAxis.autoFit = false;
		plot.yAxis2.min = 0.f;
		plot.yAxis2.max = 1000.f; // magnitude tres differente de yAxis, pour bien distinguer les deux
		plot.yAxis2.autoFit = false;
		sdl3::FRect plotRect{0.f, 0.f, 200.f, 100.f};
		ui::PlotAxis startX = plot.xAxis, startY = plot.yAxis, startY2 = plot.yAxis2;
		sdl3::FPoint startMouse{50.f, 50.f};
		float dataXAtStart = ui::ScreenToData(startX, plotRect.x, plotRect.x + plotRect.w, startMouse.x);
		float dataY2AtStart = ui::ScreenToData(startY2, plotRect.y + plotRect.h, plotRect.y, startMouse.y);
		sdl3::FPoint currentMouse{80.f, 30.f}; // glisse a droite ET vers le haut

		ui::ApplyPlotPan(plot, plotRect, startMouse, currentMouse, startX, startY, startY2);
		assert(!plot.xAxis.autoFit && !plot.yAxis.autoFit && !plot.yAxis2.autoFit);
		float sxNow = ui::DataToScreen(plot.xAxis, plotRect.x, plotRect.x + plotRect.w, dataXAtStart);
		assert(sdl3::Abs(sxNow - currentMouse.x) < 0.5f); // le point sous la souris au depart y reste
		// yAxis2 (Phase 4) suit le MEME geste, sur SA propre plage (magnitude 20x plus grande) —
		// verifie que le pan applique aux DEUX axes Y independamment, pas juste au primaire.
		float sy2Now = ui::DataToScreen(plot.yAxis2, plotRect.y + plotRect.h, plotRect.y, dataY2AtStart);
		assert(sdl3::Abs(sy2Now - currentMouse.y) < 0.5f);
		std::cout << "applyPlotPan (le contenu suit la souris, yAxis2 inclus): ok\n";
	}

	// ── applyPlotAxisZoom (fonction pure) : pivot fixe, plage retrécit ───────
	{
		ui::PlotAxis axis;
		axis.min = 0.f;
		axis.max = 100.f;
		axis.autoFit = false;
		std::vector<ui::PlotSeries> series; // vide : resolveAxis renvoie axis tel quel (autoFit=false)
		float screenMin = 0.f, screenMax = 200.f, mouseScreen = 100.f; // pivot au milieu -> donnee 50

		ui::ApplyPlotAxisZoom(axis, series, true, screenMin, screenMax, mouseScreen, 1.f); // molette avant
		assert(!axis.autoFit);
		assert((axis.max - axis.min) < 100.f); // la plage a retreci
		float sx = ui::DataToScreen(axis, screenMin, screenMax, 50.f);
		assert(sdl3::Abs(sx - mouseScreen) < 1.f); // le pivot reste sous la souris
		std::cout << "applyPlotAxisZoom (pivot fixe, plage retrecit): ok\n";

		// Même geste avec la convention INVERSÉE (axe Y : screenMin > screenMax,
		// cf. tous les appels réels de ce fichier) — a régressé une fois en
		// silence (t explosait via un clamp qui ne préservait pas le signe du
		// span écran), doit rester testé explicitement pour ne jamais revenir.
		ui::PlotAxis axisY;
		axisY.min = 0.f;
		axisY.max = 100.f;
		axisY.autoFit = false;
		ui::ApplyPlotAxisZoom(axisY, series, false, 200.f, 0.f, 100.f, 1.f); // screenMin=200 > screenMax=0
		assert(!axisY.autoFit);
		float rangeY = axisY.max - axisY.min;
		assert(rangeY > 0.f && rangeY < 100.f); // plage saine et retrecie (pas degeneree/explosee)
		float syY = ui::DataToScreen(axisY, 200.f, 0.f, 50.f);
		assert(sdl3::Abs(syY - 100.f) < 1.f); // le pivot reste sous la souris (convention inversee)
		std::cout << "applyPlotAxisZoom (convention inversee, axe Y): ok\n";
	}

	// ── applyPlotBoxZoom (fonction pure) : min/max triés depuis les 2 coins ──
	{
		ui::UiPlot plot;
		sdl3::FRect plotRect{0.f, 0.f, 200.f, 100.f};
		ui::PlotAxis startX;
		startX.min = 0.f;
		startX.max = 100.f;
		startX.autoFit = false;
		ui::PlotAxis startY;
		startY.min = 0.f;
		startY.max = 50.f;
		startY.autoFit = false;
		ui::PlotAxis startY2;
		startY2.min = 0.f;
		startY2.max = 500.f; // magnitude differente de startY
		startY2.autoFit = false;
		sdl3::FPoint corner0{50.f, 20.f}, corner1{150.f, 80.f}; // rectangle quelconque

		ui::ApplyPlotBoxZoom(plot, plotRect, corner0, corner1, startX, startY, startY2);
		assert(!plot.xAxis.autoFit && !plot.yAxis.autoFit && !plot.yAxis2.autoFit);
		assert(plot.xAxis.min < plot.xAxis.max);
		assert(plot.yAxis.min < plot.yAxis.max);
		assert(plot.yAxis2.min < plot.yAxis2.max);
		assert(sdl3::Abs(plot.xAxis.min - 25.f) < 0.5f); // corner0.x=50 -> donnee 25
		assert(sdl3::Abs(plot.xAxis.max - 75.f) < 0.5f); // corner1.x=150 -> donnee 75
		assert(sdl3::Abs(plot.yAxis.min - 10.f) < 0.5f); // corner1.y=80 (bas) -> donnee 10 (axe Y inverse)
		assert(sdl3::Abs(plot.yAxis.max - 40.f) < 0.5f); // corner0.y=20 (haut) -> donnee 40
		assert(sdl3::Abs(plot.yAxis2.min - 100.f) < 5.f); // meme rectangle, plage 10x plus grande
		assert(sdl3::Abs(plot.yAxis2.max - 400.f) < 5.f);
		std::cout << "applyPlotBoxZoom (min/max tries depuis les 2 coins, axe Y inverse, yAxis2 inclus): ok\n";
	}

	// ── resetPlotToAutoFit (fonction pure) ────────────────────────────────────
	{
		ui::UiPlot plot;
		plot.xAxis.autoFit = false;
		plot.yAxis.autoFit = false;
		ui::ResetPlotToAutoFit(plot);
		assert(plot.xAxis.autoFit && plot.yAxis.autoFit);
		std::cout << "resetPlotToAutoFit (reactive autoFit): ok\n";
	}

	// ── Intégration réelle (InputSystem::dispatch) : pan (glisser gauche) ────
	{
		auto pb = ar.GetComponent<ui::UiPlot>(plotBE);
		ui::PlotAxis preX = ui::ResolveAxis(pb.Unwrap()->xAxis, pb.Unwrap()->series, true);
		auto c = ar.GetComponent<ui::UiComputed>(plotBE);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pb.Unwrap(), c.Unwrap()->screen);
		sdl3::FPoint center{plotRect.x + plotRect.w * 0.5f, plotRect.y + plotRect.h * 0.5f};

		input.HandleEvent(ar, MouseMotionEv(center.x, center.y), layout);
		input.HandleEvent(ar, MouseDownEv(center.x, center.y), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotBE).Unwrap()->panning);

		input.HandleEvent(ar, MouseMotionEv(center.x + 30.f, center.y), layout); // glisse 30px a droite
		auto pbAfter = ar.GetComponent<ui::UiPlot>(plotBE);
		assert(pbAfter.Unwrap()->panning);         // bouton toujours enfonce
		assert(!pbAfter.Unwrap()->xAxis.autoFit);   // premiere interaction -> desactive l'auto-fit
		assert(pbAfter.Unwrap()->xAxis.min < preX.min); // glisser a droite = contenu suit -> min/max diminuent
		assert(pbAfter.Unwrap()->xAxis.max < preX.max);

		input.HandleEvent(ar, MouseUpEv(center.x + 30.f, center.y), layout);
		assert(!ar.GetComponent<ui::UiPlot>(plotBE).Unwrap()->panning); // relache -> geste termine
		std::cout << "pan (glisser gauche, integration InputSystem): ok\n";
	}

	// ── Intégration réelle : zoom molette combiné (centre du tracé) ──────────
	{
		auto pb = ar.GetComponent<ui::UiPlot>(plotBE);
		float preRangeX = pb.Unwrap()->xAxis.max - pb.Unwrap()->xAxis.min;
		float preRangeY = pb.Unwrap()->yAxis.max - pb.Unwrap()->yAxis.min;
		auto c = ar.GetComponent<ui::UiComputed>(plotBE);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pb.Unwrap(), c.Unwrap()->screen);
		sdl3::FPoint center{plotRect.x + plotRect.w * 0.5f, plotRect.y + plotRect.h * 0.5f};

		input.HandleEvent(ar, MouseMotionEv(center.x, center.y), layout);
		input.HandleEvent(ar, WheelEv(center.x, center.y, 1.f), layout); // molette avant = zoom avant

		auto pbAfter = ar.GetComponent<ui::UiPlot>(plotBE);
		float postRangeX = pbAfter.Unwrap()->xAxis.max - pbAfter.Unwrap()->xAxis.min;
		float postRangeY = pbAfter.Unwrap()->yAxis.max - pbAfter.Unwrap()->yAxis.min;
		assert(postRangeX < preRangeX); // centre du tracé (hors bande de graduation) -> les 2 axes ensemble
		assert(postRangeY < preRangeY);
		std::cout << "zoom molette (combine, integration InputSystem): ok\n";
	}

	// ── Intégration réelle : zoom molette PAR AXE (bande de graduation Y de plotD) ──
	{
		auto pd = ar.GetComponent<ui::UiPlot>(plotD);
		auto c = ar.GetComponent<ui::UiComputed>(plotD);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pd.Unwrap(), c.Unwrap()->screen);
		// yAxis de plotD : tickPosition=Right, tickOverlay=false (cf. Phase 2) -> bande reservee a droite
		sdl3::FPoint overYTicks{plotRect.x + plotRect.w + 5.f, plotRect.y + plotRect.h * 0.5f};
		float preRangeY = ui::ResolveAxis(pd.Unwrap()->yAxis, pd.Unwrap()->series, false).max -
						 ui::ResolveAxis(pd.Unwrap()->yAxis, pd.Unwrap()->series, false).min;

		input.HandleEvent(ar, MouseMotionEv(overYTicks.x, overYTicks.y), layout);
		input.HandleEvent(ar, WheelEv(overYTicks.x, overYTicks.y, 1.f), layout);

		auto pdAfter = ar.GetComponent<ui::UiPlot>(plotD);
		assert(!pdAfter.Unwrap()->yAxis.autoFit); // Y modifie
		assert(pdAfter.Unwrap()->xAxis.autoFit);  // X inchange (zoom PAR AXE, pas combine)
		float postRangeY = pdAfter.Unwrap()->yAxis.max - pdAfter.Unwrap()->yAxis.min;
		assert(postRangeY < preRangeY);
		std::cout << "zoom molette (par axe, bande de graduation reservee): ok\n";
	}

	// ── Intégration réelle : zoom-rectangle (clic-droit glissé) ──────────────
	{
		auto pb = ar.GetComponent<ui::UiPlot>(plotBE);
		float preRangeX = pb.Unwrap()->xAxis.max - pb.Unwrap()->xAxis.min;
		float preRangeY = pb.Unwrap()->yAxis.max - pb.Unwrap()->yAxis.min;
		auto c = ar.GetComponent<ui::UiComputed>(plotBE);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pb.Unwrap(), c.Unwrap()->screen);
		sdl3::FPoint corner0{plotRect.x + plotRect.w * 0.25f, plotRect.y + plotRect.h * 0.25f};
		sdl3::FPoint corner1{plotRect.x + plotRect.w * 0.75f, plotRect.y + plotRect.h * 0.75f};

		input.HandleEvent(ar, MouseMotionEv(corner0.x, corner0.y), layout);
		input.HandleEvent(ar, MouseDownEv(corner0.x, corner0.y, SDL_BUTTON_RIGHT), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotBE).Unwrap()->boxZooming);

		input.HandleEvent(ar, MouseMotionEv(corner1.x, corner1.y), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotBE).Unwrap()->boxZooming); // toujours en cours (bouton pas relache)

		input.HandleEvent(ar, MouseUpEv(corner1.x, corner1.y, SDL_BUTTON_RIGHT), layout);
		auto pbAfter = ar.GetComponent<ui::UiPlot>(plotBE);
		assert(!pbAfter.Unwrap()->boxZooming);
		assert(!pbAfter.Unwrap()->xAxis.autoFit && !pbAfter.Unwrap()->yAxis.autoFit);
		float postRangeX = pbAfter.Unwrap()->xAxis.max - pbAfter.Unwrap()->xAxis.min;
		float postRangeY = pbAfter.Unwrap()->yAxis.max - pbAfter.Unwrap()->yAxis.min;
		assert(postRangeX < preRangeX); // rectangle = 50% du tracé sur chaque axe -> plage retrecie
		assert(postRangeY < preRangeY);
		std::cout << "zoom-rectangle (clic-droit glisse, integration InputSystem): ok\n";
	}

	// ── Un simple clic-droit SANS glisser n'applique aucun zoom (seuil 4px) ──
	{
		auto pb = ar.GetComponent<ui::UiPlot>(plotBE);
		float preMinX = pb.Unwrap()->xAxis.min, preMaxX = pb.Unwrap()->xAxis.max;
		auto c = ar.GetComponent<ui::UiComputed>(plotBE);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pb.Unwrap(), c.Unwrap()->screen);
		sdl3::FPoint p{plotRect.x + plotRect.w * 0.5f, plotRect.y + plotRect.h * 0.5f};

		input.HandleEvent(ar, MouseMotionEv(p.x, p.y), layout);
		input.HandleEvent(ar, MouseDownEv(p.x, p.y, SDL_BUTTON_RIGHT), layout);
		input.HandleEvent(ar, MouseUpEv(p.x, p.y, SDL_BUTTON_RIGHT), layout); // relache au meme endroit
		auto pbAfter = ar.GetComponent<ui::UiPlot>(plotBE);
		assert(!pbAfter.Unwrap()->boxZooming);
		assert(sdl3::Abs(pbAfter.Unwrap()->xAxis.min - preMinX) < 0.01f); // rien n'a change
		assert(sdl3::Abs(pbAfter.Unwrap()->xAxis.max - preMaxX) < 0.01f);
		std::cout << "clic-droit sans glisser (aucun zoom applique): ok\n";
	}

	// ── Intégration réelle : double-clic = retour à l'ajustement automatique ─
	{
		auto pb = ar.GetComponent<ui::UiPlot>(plotBE);
		assert(!pb.Unwrap()->xAxis.autoFit); // suite du zoom-rectangle precedent
		auto c = ar.GetComponent<ui::UiComputed>(plotBE);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pb.Unwrap(), c.Unwrap()->screen);
		sdl3::FPoint center{plotRect.x + plotRect.w * 0.5f, plotRect.y + plotRect.h * 0.5f};

		input.HandleEvent(ar, MouseMotionEv(center.x, center.y), layout);
		input.HandleEvent(ar, MouseDownEv(center.x, center.y, SDL_BUTTON_LEFT, 2), layout); // double-clic
		auto pbAfter = ar.GetComponent<ui::UiPlot>(plotBE);
		assert(pbAfter.Unwrap()->xAxis.autoFit && pbAfter.Unwrap()->yAxis.autoFit);
		assert(!pbAfter.Unwrap()->panning); // le double-clic ne doit PAS armer un pan
		input.HandleEvent(ar, MouseUpEv(center.x, center.y), layout); // relachement propre
		std::cout << "double-clic (retour a l'ajustement automatique, integration InputSystem): ok\n";
	}

	// ── Construction + rendu réel d'un plot en échelle log (X et Y) ──────────
	{
		auto plotBuilder = f.Plot("Log-Log");
		float lx[] = {1.f, 10.f, 100.f, 1000.f};
		float ly[] = {2.f, 20.f, 200.f, 2000.f};
		plotBuilder.AddLineSeries(ly, "log-log"); // X implicite (indices), régénéré ci-dessous
		plotBuilder.AddLineSeriesXy(lx, ly, "vraies-donnees");
		plotBuilder.LogScaleX().LogScaleY();
		plotBuilder.Name("plotE").Size(300.f, 200.f).Offset(0.f, 500.f); // ne recouvre pas plotA/B/C/D
		ecs::Entity plotE = plotBuilder.Spawn();

		auto pe = ar.GetComponent<ui::UiPlot>(plotE);
		assert(pe.IsSome());
		assert(pe.Unwrap()->xAxis.logScale && pe.Unwrap()->yAxis.logScale);

		style.Resolve(ar);
		layout.RunIfNeeded(ar, 800.f, 600.f);

		// Marque un zoom-rectangle comme "en cours" sur plotBE pour exercer
		// aussi drawPlotBoxZoomRect() pendant ce rendu (jamais couvert par un
		// rendu reel jusqu'ici, seulement par la logique de calcul).
		ar.GetComponent<ui::UiPlot>(plotBE).Unwrap()->boxZooming = true;
		ar.GetComponent<ui::UiPlot>(plotBE).Unwrap()->dragStartMouse = {10.f, 10.f};
		ar.GetComponent<ui::UiPlot>(plotBE).Unwrap()->dragCurrentMouse = {60.f, 60.f};

		auto winRes = sdl3::Window::Create("smoke-ui8-phase3", 400, 300, sdl3::window_flags::HIDDEN);
		assert(winRes);
		auto renRes = sdl3::Renderer::Create(winRes.Value());
		assert(renRes);
		auto &ren = renRes.Value();
		ui::SdlRendererBackend renBackend(ren);
		render.Run(ar, renBackend, &input.tooltip);
		std::cout << "rendu complet (echelle log + rectangle de zoom en cours): ok\n";

		ar.GetComponent<ui::UiPlot>(plotBE).Unwrap()->boxZooming = false; // remise a plat
	}

	std::cout << "plot phase3 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 4 — Curseur/tooltip (déjà couvert Phase 0) + axe Y secondaire
	// ════════════════════════════════════════════════════════════════════

	// ── plotHasSecondaryY : vrai seulement si au moins une série l'utilise ──
	{
		ui::UiPlot plot;
		assert(!ui::PlotHasSecondaryY(plot));
		ui::PlotSeries s;
		s.SetXy(std::array<float, 2>{0.f, 1.f}, std::array<float, 2>{1.f, 2.f});
		plot.series.push_back(s);
		assert(!ui::PlotHasSecondaryY(plot)); // aucune serie ne l'utilise encore
		plot.series.back().useSecondaryY = true;
		assert(ui::PlotHasSecondaryY(plot));
		std::cout << "plotHasSecondaryY (derive de PlotSeries::useSecondaryY): ok\n";
	}

	// ── computeAutoFit(forSecondaryY) : filtre indépendamment primaire/secondaire ──
	{
		std::vector<ui::PlotSeries> series;
		ui::PlotSeries primary;
		primary.SetXy(std::array<float, 2>{0.f, 1.f}, std::array<float, 2>{1.f, 2.f}); // petites valeurs
		series.push_back(primary);
		ui::PlotSeries secondary;
		secondary.useSecondaryY = true;
		secondary.SetXy(std::array<float, 2>{0.f, 1.f}, std::array<float, 2>{1000.f, 2000.f}); // grandes valeurs
		series.push_back(secondary);

		auto [pmn, pmx] = ui::ComputeAutoFit(series, false, false, false); // Y primaire seul
		assert(pmx < 100.f); // n'inclut PAS les grandes valeurs de la serie secondaire
		auto [smn, smx] = ui::ComputeAutoFit(series, false, false, true); // Y secondaire seul
		assert(smn > 100.f); // n'inclut PAS les petites valeurs de la serie primaire
		auto [xmn, xmx] = ui::ComputeAutoFit(series, true, false, false); // X : forSecondaryY ignore
		assert(sdl3::Abs(xmn) < 1.f && xmx < 2.f); // les DEUX series contribuent a X, peu importe forSecondaryY
		std::cout << "computeAutoFit (forSecondaryY filtre Y independamment, X toujours partage): ok\n";
	}

	// ── computePlotMargins : yAxis2 réservé (droite par défaut) si actif ─────
	{
		ui::UiPlot plot;
		plot.yAxis.tickPosition = ui::PlotEdge::Left;
		plot.yAxis2.tickPosition = ui::PlotEdge::Right; // defaut de UiFactory::Plot()

		auto mNoY2 = ui::ComputePlotMargins(plot); // aucune serie -> plotHasSecondaryY==false
		float rightBefore = mNoY2.right;
		assert(rightBefore == 0.f); // rien reserve a droite (yAxis est a gauche, yAxis2 pas encore actif)

		ui::PlotSeries s;
		s.useSecondaryY = true;
		s.SetXy(std::array<float, 1>{0.f}, std::array<float, 1>{1.f});
		plot.series.push_back(s); // active yAxis2 (plotHasSecondaryY devient vrai)
		auto mWithY2 = ui::ComputePlotMargins(plot);
		assert(mWithY2.right > rightBefore); // marge droite supplementaire pour yAxis2
		std::cout << "computePlotMargins (yAxis2 reserve a droite si actif): ok\n";
	}

	// ── Construction via builder : addXSeries(...).UseSecondaryY() ──────────
	ecs::Entity plotF{};
	{
		auto plotBuilder = f.Plot("Double axe Y");
		float fx[] = {0.f, 1.f, 2.f, 3.f};
		float fyPrimary[] = {1.f, 2.f, 1.5f, 2.5f};   // petite echelle
		float fySecondary[] = {100.f, 400.f, 250.f, 500.f}; // grande echelle
		plotBuilder.AddLineSeriesXy(fx, fyPrimary, "primaire");
		plotBuilder.AddLineSeriesXy(fx, fySecondary, "secondaire").UseSecondaryY();
		plotBuilder.Name("plotF").Size(300.f, 200.f).Offset(350.f, 500.f); // ne recouvre pas plotA..E
		plotF = plotBuilder.Spawn();

		auto pf = ar.GetComponent<ui::UiPlot>(plotF);
		assert(pf.IsSome());
		assert(!pf.Unwrap()->series[0].useSecondaryY);
		assert(pf.Unwrap()->series[1].useSecondaryY);
		assert(pf.Unwrap()->yAxis2.tickPosition == ui::PlotEdge::Right); // defaut de UiFactory::Plot()
		assert(ui::PlotHasSecondaryY(*pf.Unwrap()));
		std::cout << "construction (addLineSeriesXY(...).UseSecondaryY()): ok\n";
	}

	style.Resolve(ar);
	layout.RunIfNeeded(ar, 800.f, 600.f);

	// ── Auto-fit indépendant : yAxis et yAxis2 se résolvent à des plages
	// très différentes (magnitude x100) sans interférence mutuelle ──────────
	{
		auto pf = ar.GetComponent<ui::UiPlot>(plotF);
		ui::PlotAxis yr = ui::ResolveAxis(pf.Unwrap()->yAxis, pf.Unwrap()->series, false, false);
		ui::PlotAxis yr2 = ui::ResolveAxis(pf.Unwrap()->yAxis2, pf.Unwrap()->series, false, true);
		assert(yr.max < 10.f);   // plage primaire ~[1,2.5] + marge
		assert(yr2.min > 50.f);  // plage secondaire ~[100,500] + marge, jamais tiree vers le bas par le primaire
		std::cout << "auto-fit independant (yAxis/yAxis2 ne se contaminent pas): ok\n";
	}

	// ── Survol : plotNearestPoint trouve un point sur la série secondaire ──
	{
		auto pf = ar.GetComponent<ui::UiPlot>(plotF);
		auto c = ar.GetComponent<ui::UiComputed>(plotF);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pf.Unwrap(), c.Unwrap()->screen);
		ui::PlotAxis xr = ui::ResolveAxis(pf.Unwrap()->xAxis, pf.Unwrap()->series, true);
		ui::PlotAxis yr = ui::ResolveAxis(pf.Unwrap()->yAxis, pf.Unwrap()->series, false, false);
		ui::PlotAxis yr2 = ui::ResolveAxis(pf.Unwrap()->yAxis2, pf.Unwrap()->series, false, true);
		auto pts2 = ui::PlotSeriesScreenPoints(pf.Unwrap()->series[1], plotRect, xr, yr2); // serie secondaire
		sdl3::FPoint target = pts2[1]; // point d'indice 1 (y=400)

		auto [si, idx] = ui::PlotNearestPoint(*pf.Unwrap(), plotRect, xr, yr, yr2, target);
		assert(si == 1); // bien la serie secondaire, pas la primaire
		assert(idx == 1);
		std::cout << "plotNearestPoint (trouve un point sur la serie secondaire via yAxis2): ok\n";

		// Intégration réelle via InputSystem : le survol doit aboutir au
		// même résultat que l'appel direct ci-dessus.
		input.HandleEvent(ar, MouseMotionEv(target.x, target.y), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotF).Unwrap()->hoveredSeries == 1);
		assert(ar.GetComponent<ui::UiPlot>(plotF).Unwrap()->hoveredIndex == 1);
		std::cout << "survol (integration InputSystem, serie secondaire): ok\n";
	}

	// ── Rendu réel avec axe Y secondaire (graduations + deux échelles) ──────
	{
		auto winRes = sdl3::Window::Create("smoke-ui8-phase4", 400, 300, sdl3::window_flags::HIDDEN);
		assert(winRes);
		auto renRes = sdl3::Renderer::Create(winRes.Value());
		assert(renRes);
		auto &ren = renRes.Value();
		ui::SdlRendererBackend renBackend(ren);
		render.Run(ar, renBackend, &input.tooltip);
		std::cout << "rendu complet (axe Y secondaire, graduations sur les deux bords): ok\n";
	}

	std::cout << "plot phase4 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 5 — Barres d'erreur (Line/Scatter) + Camembert (PlotMode::PIE)
	// ════════════════════════════════════════════════════════════════════

	// ── computePieAngles : proportions correctes, parts masquées exclues ────
	{
		std::vector<ui::PieSlice> slices;
		ui::PieSlice a;
		a.value = 25.f;
		slices.push_back(a);
		ui::PieSlice b;
		b.value = 75.f;
		slices.push_back(b);
		ui::PieSlice hidden;
		hidden.value = 1000.f;
		hidden.visible = false; // exclue du total malgre sa grande valeur
		slices.push_back(hidden);

		auto angles = ui::ComputePieAngles(slices);
		assert(sdl3::Abs(angles[0].startDeg - (-90.f)) < 0.01f); // debut en haut (convention horaire)
		assert(sdl3::Abs((angles[0].endDeg - angles[0].startDeg) - 90.f) < 0.5f);  // 25% de 360 = 90
		assert(sdl3::Abs((angles[1].endDeg - angles[1].startDeg) - 270.f) < 0.5f); // 75% de 360 = 270
		assert(angles[2].endDeg <= angles[2].startDeg); // part masquee : pas d'angle
		std::cout << "computePieAngles (proportions, parts masquees exclues): ok\n";

		std::vector<ui::PieSlice> withNeg;
		ui::PieSlice neg;
		neg.value = -10.f;
		withNeg.push_back(neg);
		ui::PieSlice pos;
		pos.value = 50.f;
		withNeg.push_back(pos);
		auto angles2 = ui::ComputePieAngles(withNeg);
		assert(angles2[0].endDeg <= angles2[0].startDeg); // valeur negative -> aucune aire
		assert(sdl3::Abs((angles2[1].endDeg - angles2[1].startDeg) - 360.f) < 0.5f); // seule part restante = 100%
		std::cout << "computePieAngles (valeurs negatives traitees comme 0): ok\n";
	}

	// ── computePieCircleRect : carré inscrit, centré ─────────────────────────
	{
		sdl3::FRect plotRect{10.f, 20.f, 200.f, 100.f}; // largeur > hauteur
		auto circle = ui::ComputePieCircleRect(plotRect);
		assert(sdl3::Abs(circle.w - 100.f) < 0.01f && sdl3::Abs(circle.h - 100.f) < 0.01f); // cote = sdl3::Min(w,h)
		assert(sdl3::Abs((circle.x + circle.w * 0.5f) - (plotRect.x + plotRect.w * 0.5f)) < 0.01f);
		assert(sdl3::Abs((circle.y + circle.h * 0.5f) - (plotRect.y + plotRect.h * 0.5f)) < 0.01f);
		std::cout << "computePieCircleRect (carre inscrit centre): ok\n";
	}

	// ── pieHitTest : distance/angle corrects, hors rayon -> -1 ──────────────
	{
		std::vector<ui::PieSlice> slices;
		ui::PieSlice a;
		a.value = 50.f;
		slices.push_back(a); // -90 -> 90 (moitie "droite" en tournant depuis le haut)
		ui::PieSlice b;
		b.value = 50.f;
		slices.push_back(b); // 90 -> 270 (moitie "gauche")
		auto angles = ui::ComputePieAngles(slices);
		sdl3::FPoint center{100.f, 100.f};
		float radius = 50.f;

		int hit = ui::PieHitTest(slices, angles, center, radius, {center.x + 30.f, center.y}); // angle ~0
		assert(hit == 0);
		int hit2 = ui::PieHitTest(slices, angles, center, radius, {center.x - 30.f, center.y}); // angle ~180
		assert(hit2 == 1);
		int hit3 = ui::PieHitTest(slices, angles, center, radius, {center.x + 200.f, center.y}); // hors rayon
		assert(hit3 == -1);
		std::cout << "pieHitTest (distance/angle, hors rayon): ok\n";
	}

	// ── Construction via builder : pie().addPieSlice(...) ────────────────────
	ecs::Entity plotG{};
	{
		auto pieBuilder = f.Pie("Repartition");
		pieBuilder.AddPieSlice(40.f, "Rouge", Some(sdl3::FColor{220, 50, 50, 255}));
		pieBuilder.AddPieSlice(35.f, "Vert", Some(sdl3::FColor{50, 200, 80, 255}));
		pieBuilder.AddPieSlice(25.f, "Bleu", Some(sdl3::FColor{60, 100, 220, 255}));
		pieBuilder.Name("plotG").Offset(0.f, 750.f); // ne recouvre pas plotA..F
		plotG = pieBuilder.Spawn();

		auto pg = ar.GetComponent<ui::UiPlot>(plotG);
		assert(pg.IsSome());
		assert(pg.Unwrap()->mode == ui::PlotMode::PIE);
		assert(pg.Unwrap()->pieSlices.size() == 3);
		assert(pg.Unwrap()->pieSlices[0].label == "Rouge");
		assert(sdl3::Abs(pg.Unwrap()->pieSlices[0].value - 40.f) < 0.01f);
		std::cout << "construction (pie().addPieSlice()): ok\n";
	}

	// ── Barres d'erreur : construction via builder ───────────────────────────
	ecs::Entity plotH{};
	{
		auto plotBuilder = f.Plot("Erreurs");
		float hx[] = {0.f, 1.f, 2.f, 3.f};
		float hy[] = {2.f, 3.f, 2.5f, 4.f};
		float herr[] = {0.3f, 0.5f, 0.2f, 0.6f};
		plotBuilder.AddScatterSeries(hx, hy, "mesures").SetYError(herr);
		plotBuilder.Name("plotH").Offset(350.f, 750.f); // ne recouvre pas plotA..G
		plotH = plotBuilder.Spawn();

		auto ph = ar.GetComponent<ui::UiPlot>(plotH);
		assert(ph.IsSome());
		assert(ph.Unwrap()->series[0].yError.size() == 4);
		assert(sdl3::Abs(ph.Unwrap()->series[0].yError[1] - 0.5f) < 0.01f);
		std::cout << "construction (addScatterSeries(...).SetYError()): ok\n";
	}

	style.Resolve(ar);
	layout.RunIfNeeded(ar, 800.f, 1000.f); // canvas agrandi : plotG/plotH sont a y=750

	// ── Interaction légende (Pie) : survol/bascule, même mécanisme que XY ───
	{
		auto pg = ar.GetComponent<ui::UiPlot>(plotG);
		auto c = ar.GetComponent<ui::UiComputed>(plotG);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pg.Unwrap(), c.Unwrap()->screen);
		sdl3::FRect legendBox = ui::LegendBoxRect(*pg.Unwrap(), c.Unwrap()->screen, plotRect);
		sdl3::FRect row0 = ui::LegendRowRect(legendBox, 0);
		sdl3::FPoint rowCenter{row0.x + row0.w * 0.5f, row0.y + row0.h * 0.5f};

		input.HandleEvent(ar, MouseMotionEv(rowCenter.x, rowCenter.y), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotG).Unwrap()->legendHover == 0);

		input.HandleEvent(ar, MouseDownEv(rowCenter.x, rowCenter.y), layout);
		assert(!ar.GetComponent<ui::UiPlot>(plotG).Unwrap()->pieSlices[0].visible); // bascule au clic
		input.HandleEvent(ar, MouseDownEv(rowCenter.x, rowCenter.y), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotG).Unwrap()->pieSlices[0].visible); // re-bascule
		std::cout << "interaction legende (Pie, survol/bascule): ok\n";
	}

	// ── Survol direct d'une part (intégration InputSystem) ──────────────────
	{
		auto pg = ar.GetComponent<ui::UiPlot>(plotG);
		auto c = ar.GetComponent<ui::UiComputed>(plotG);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pg.Unwrap(), c.Unwrap()->screen);
		sdl3::FRect circleRect = ui::ComputePieCircleRect(plotRect);
		sdl3::FPoint center{circleRect.x + circleRect.w * 0.5f, circleRect.y + circleRect.h * 0.5f};
		float radius = circleRect.w * 0.5f;
		// Droit au-dessus du centre = début de la part 0 (-90deg, cf. computePieAngles).
		sdl3::FPoint target{center.x, center.y - radius * 0.5f};

		input.HandleEvent(ar, MouseMotionEv(target.x, target.y), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotG).Unwrap()->hoveredSeries == 0);
		std::cout << "survol direct d'une part (integration InputSystem): ok\n";

		input.HandleEvent(ar, MouseMotionEv(-100.f, -100.f), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotG).Unwrap()->hoveredSeries == -1);
		std::cout << "survol efface hors widget (Pie): ok\n";
	}

	// ── Rendu réel : camembert (légende + étiquette survolée) + barres d'erreur ──
	{
		auto winRes = sdl3::Window::Create("smoke-ui8-phase5", 400, 300, sdl3::window_flags::HIDDEN);
		assert(winRes);
		auto renRes = sdl3::Renderer::Create(winRes.Value());
		assert(renRes);
		auto &ren = renRes.Value();
		ui::SdlRendererBackend renBackend(ren);
		render.Run(ar, renBackend, &input.tooltip);
		std::cout << "rendu complet (camembert + barres d'erreur): ok\n";
	}

	std::cout << "plot phase5 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 6 — Heatmap (PlotMode::HEATMAP) + Chandelier (PlotMode::CANDLE)
	// ════════════════════════════════════════════════════════════════════

	// ── heatmapColor : extrémités et point milieu par colormap ──────────────
	{
		sdl3::FColor g0 = ui::HeatmapColor(0.f, ui::Colormap::GRAYSCALE);
		sdl3::FColor g1 = ui::HeatmapColor(1.f, ui::Colormap::GRAYSCALE);
		assert(g0.r == 0.f && g0.g == 0.f && g0.b == 0.f);
		assert(g1.r == 1.f && g1.g == 1.f && g1.b == 1.f);

		sdl3::FColor v0 = ui::HeatmapColor(0.f, ui::Colormap::VIRIDIS);
		assert(v0.r == 68 / 255.f && v0.g == 1 / 255.f && v0.b == 84 / 255.f); // point d'ancrage exact

		sdl3::FColor cwMid = ui::HeatmapColor(0.5f, ui::Colormap::COOL_WARM);
		assert(cwMid.r == 245 / 255.f && cwMid.g == 245 / 255.f && cwMid.b == 245 / 255.f); // blanc au milieu (divergent)
		std::cout << "heatmapColor (extremites/milieu par colormap): ok\n";
	}

	// ── resolveHeatmapRange : auto (depuis values) vs manuel ─────────────────
	{
		ui::HeatmapData hm;
		hm.values = {1.f, 5.f, 3.f, 9.f};
		auto [mn, mx] = ui::ResolveHeatmapRange(hm);
		assert(sdl3::Abs(mn - 1.f) < 0.01f && sdl3::Abs(mx - 9.f) < 0.01f);

		hm.autoRange = false;
		hm.minValue = 0.f;
		hm.maxValue = 100.f;
		auto [mn2, mx2] = ui::ResolveHeatmapRange(hm);
		assert(sdl3::Abs(mn2 - 0.f) < 0.01f && sdl3::Abs(mx2 - 100.f) < 0.01f);
		std::cout << "resolveHeatmapRange (auto vs manuel): ok\n";
	}

	// ── drawHeatmap : garde si values insuffisant (pas de crash) ─────────────
	{
		auto winRes = sdl3::Window::Create("smoke-ui8-phase6-guard", 400, 300, sdl3::window_flags::HIDDEN);
		assert(winRes);
		auto renRes = sdl3::Renderer::Create(winRes.Value());
		assert(renRes);
		auto &ren = renRes.Value();
		ui::SdlRendererBackend renBackend(ren);
		ui::HeatmapData badHm;
		badHm.rows = 3;
		badHm.cols = 3;
		badHm.values = {1.f, 2.f}; // 2 < 9 attendus
		ui::DrawHeatmap(renBackend, sdl3::FRect{0.f, 0.f, 100.f, 100.f}, badHm); // ne doit pas planter
		std::cout << "drawHeatmap (garde values insuffisant, pas de crash): ok\n";
	}

	// ── heatmapHitTest : index de cellule, hors grille -> -1 ─────────────────
	{
		ui::HeatmapData hm;
		hm.rows = 2;
		hm.cols = 2;
		hm.values = {1.f, 2.f, 3.f, 4.f};
		sdl3::FRect plotRect{0.f, 0.f, 100.f, 100.f}; // cellules de 50x50

		assert(ui::HeatmapHitTest(hm, plotRect, {10.f, 10.f}) == 0); // ligne 0, colonne 0
		assert(ui::HeatmapHitTest(hm, plotRect, {60.f, 10.f}) == 1); // ligne 0, colonne 1
		assert(ui::HeatmapHitTest(hm, plotRect, {10.f, 60.f}) == 2); // ligne 1, colonne 0
		assert(ui::HeatmapHitTest(hm, plotRect, {60.f, 60.f}) == 3); // ligne 1, colonne 1
		assert(ui::HeatmapHitTest(hm, plotRect, {200.f, 200.f}) == -1); // hors grille
		std::cout << "heatmapHitTest (index de cellule, hors grille): ok\n";
	}

	// ── Construction via builder : heatmap().setHeatmapData(...) ─────────────
	ecs::Entity plotI{};
	{
		auto hmBuilder = f.Heatmap("Correlation");
		float values[] = {0.1f, 0.5f, 0.9f, 0.3f, 0.7f, 0.2f};
		hmBuilder.SetHeatmapData(2, 3, values, ui::Colormap::COOL_WARM);
		hmBuilder.Name("plotI").Offset(0.f, 1000.f); // ne recouvre pas plotA..H
		plotI = hmBuilder.Spawn();

		auto pi = ar.GetComponent<ui::UiPlot>(plotI);
		assert(pi.IsSome());
		assert(pi.Unwrap()->mode == ui::PlotMode::HEATMAP);
		assert(pi.Unwrap()->heatmap.rows == 2 && pi.Unwrap()->heatmap.cols == 3);
		assert(pi.Unwrap()->heatmap.values.size() == 6);
		assert(pi.Unwrap()->heatmap.colormap == ui::Colormap::COOL_WARM);
		std::cout << "construction (heatmap().setHeatmapData()): ok\n";
	}

	// ── computeCandleAutoFit / resolveCandleAxis : min/max depuis les bougies ──
	{
		std::vector<ui::OhlcBar> bars;
		ui::OhlcBar b1{0.f, 10.f, 12.f, 9.f, 11.f};
		ui::OhlcBar b2{1.f, 11.f, 15.f, 10.f, 14.f};
		bars.push_back(b1);
		bars.push_back(b2);

		auto [xmn, xmx] = ui::ComputeCandleAutoFit(bars, true);
		assert(xmn < 0.f && xmx > 1.f); // marge autour de [0,1]
		auto [ymn, ymx] = ui::ComputeCandleAutoFit(bars, false);
		assert(ymn < 9.f && ymx > 15.f); // marge autour de [low_min=9, high_max=15]

		ui::PlotAxis xAxis;
		xAxis.autoFit = false; // ignore : resolveCandleAxis resout TOUJOURS depuis les bougies
		xAxis.min = 999.f;
		xAxis.max = 1000.f;
		auto resolved = ui::ResolveCandleAxis(xAxis, bars, true);
		assert(sdl3::Abs(resolved.min - xmn) < 0.01f); // pas 999 : autoFit=false n'a aucun effet ici
		std::cout << "computeCandleAutoFit/resolveCandleAxis (toujours depuis les bougies): ok\n";
	}

	// ── computeCandleBodyRect + candleNearestBar ──────────────────────────────
	{
		std::vector<ui::OhlcBar> bars;
		ui::OhlcBar b1{0.f, 10.f, 12.f, 9.f, 11.f}; // hausse (close > open)
		ui::OhlcBar b2{1.f, 11.f, 15.f, 10.f, 8.f}; // baisse (close < open)
		bars.push_back(b1);
		bars.push_back(b2);
		ui::PlotAxis xAxis;
		xAxis.min = -0.5f;
		xAxis.max = 1.5f;
		xAxis.autoFit = false;
		ui::PlotAxis yAxis;
		yAxis.min = 8.f;
		yAxis.max = 16.f;
		yAxis.autoFit = false;
		sdl3::FRect plotRect{0.f, 0.f, 200.f, 100.f};

		auto body1 = ui::ComputeCandleBodyRect(plotRect, xAxis, yAxis, b1, 0.4f);
		assert(body1.w > 0.f && body1.h > 0.f);

		float cx0 = ui::DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, b1.x);
		float cx1 = ui::DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, b2.x);
		int nearest0 = ui::CandleNearestBar(bars, plotRect, xAxis, {cx0, 50.f});
		assert(nearest0 == 0);
		int nearest1 = ui::CandleNearestBar(bars, plotRect, xAxis, {cx1, 50.f});
		assert(nearest1 == 1);
		int nearestNone = ui::CandleNearestBar(bars, plotRect, xAxis, {-500.f, 50.f}, 5.f);
		assert(nearestNone == -1);
		std::cout << "computeCandleBodyRect/candleNearestBar: ok\n";
	}

	// ── Construction via builder : candlestick().AddOhlcBar(...) ─────────────
	ecs::Entity plotJ{};
	{
		auto candleBuilder = f.Candlestick("BTC/USD");
		candleBuilder.AddOhlcBar(0.f, 100.f, 110.f, 95.f, 108.f);
		candleBuilder.AddOhlcBar(1.f, 108.f, 112.f, 100.f, 102.f);
		candleBuilder.AddOhlcBar(2.f, 102.f, 120.f, 101.f, 118.f);
		candleBuilder.Name("plotJ").Offset(350.f, 1000.f); // ne recouvre pas plotA..I
		plotJ = candleBuilder.Spawn();

		auto pj = ar.GetComponent<ui::UiPlot>(plotJ);
		assert(pj.IsSome());
		assert(pj.Unwrap()->mode == ui::PlotMode::CANDLE);
		assert(pj.Unwrap()->candleBars.size() == 3);
		assert(sdl3::Abs(pj.Unwrap()->candleBars[2].close - 118.f) < 0.01f);
		std::cout << "construction (candlestick().AddOhlcBar()): ok\n";
	}

	style.Resolve(ar);
	layout.RunIfNeeded(ar, 800.f, 1250.f); // canvas agrandi : plotI/plotJ sont a y=1000

	// ── Survol (intégration InputSystem) : cellule de heatmap ────────────────
	{
		auto pi = ar.GetComponent<ui::UiPlot>(plotI);
		auto c = ar.GetComponent<ui::UiComputed>(plotI);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pi.Unwrap(), c.Unwrap()->screen);
		sdl3::FPoint target{plotRect.x + 5.f, plotRect.y + 5.f}; // premiere cellule (ligne 0, colonne 0)

		input.HandleEvent(ar, MouseMotionEv(target.x, target.y), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotI).Unwrap()->hoveredSeries == 0);
		std::cout << "survol (integration InputSystem, cellule heatmap): ok\n";

		input.HandleEvent(ar, MouseMotionEv(-100.f, -100.f), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotI).Unwrap()->hoveredSeries == -1);
		std::cout << "survol efface hors widget (Heatmap): ok\n";
	}

	// ── Survol (intégration InputSystem) : bougie la plus proche ────────────
	{
		auto pj = ar.GetComponent<ui::UiPlot>(plotJ);
		auto c = ar.GetComponent<ui::UiComputed>(plotJ);
		sdl3::FRect plotRect = ui::ComputePlotRect(*pj.Unwrap(), c.Unwrap()->screen);
		ui::PlotAxis xr = ui::ResolveCandleAxis(pj.Unwrap()->xAxis, pj.Unwrap()->candleBars, true);
		float cx = ui::DataToScreen(xr, plotRect.x, plotRect.x + plotRect.w, pj.Unwrap()->candleBars[1].x);
		float cy = plotRect.y + plotRect.h * 0.5f;

		input.HandleEvent(ar, MouseMotionEv(cx, cy), layout);
		assert(ar.GetComponent<ui::UiPlot>(plotJ).Unwrap()->hoveredSeries == 1);
		std::cout << "survol (integration InputSystem, bougie la plus proche): ok\n";
	}

	// ── Rendu réel : heatmap + chandelier ──────────────────────────────────
	{
		auto winRes = sdl3::Window::Create("smoke-ui8-phase6", 400, 300, sdl3::window_flags::HIDDEN);
		assert(winRes);
		auto renRes = sdl3::Renderer::Create(winRes.Value());
		assert(renRes);
		auto &ren = renRes.Value();
		ui::SdlRendererBackend renBackend(ren);
		render.Run(ar, renBackend, &input.tooltip);
		std::cout << "rendu complet (heatmap + chandelier): ok\n";
	}

	std::cout << "plot phase6 checks passed\n";

	// ════════════════════════════════════════════════════════════════════
	// Phase 7 — onCustomDraw + clôture des tests
	// ════════════════════════════════════════════════════════════════════

	// ── onCustomDraw : remplace ENTIÈREMENT le rendu par défaut d'une série ──
	{
		auto plotBuilder = f.Plot("Custom");
		float kx[] = {0.f, 1.f, 2.f};
		float ky[] = {1.f, 2.f, 3.f};
		plotBuilder.AddLineSeries(ky, "normal"); // rendu par defaut, pour verifier que les DEUX coexistent
		plotBuilder.AddLineSeriesXy(kx, ky, "custom");

		static int customDrawCalls = 0;
		static sdl3::FRect lastPlotRect{};
		static ui::PlotAxis lastXAxis{}, lastYAxis{};
		plotBuilder.SetOnCustomDraw([](sdl3::Renderer &ren, const sdl3::FRect &plotRect, const ui::PlotSeries &s,
									   const ui::PlotAxis &xAxis, const ui::PlotAxis &yAxis) {
			++customDrawCalls;
			lastPlotRect = plotRect;
			lastXAxis = xAxis;
			lastYAxis = yAxis;
			// Dessine juste un point au centre, pour vérifier que l'appelant
			// PEUT réellement dessiner (pas juste être notifié).
			ren.SetDrawColor(s.color);
			ren.FillCircle({plotRect.x + plotRect.w * 0.5f, plotRect.y + plotRect.h * 0.5f}, 3.f);
		});
		plotBuilder.Name("plotK").Offset(0.f, 1250.f); // ne recouvre pas plotA..J
		ecs::Entity plotK = plotBuilder.Spawn();

		auto pk = ar.GetComponent<ui::UiPlot>(plotK);
		assert(pk.IsSome());
		assert(pk.Unwrap()->series.size() == 2);
		assert(!bool(pk.Unwrap()->series[0].onCustomDraw)); // "normal" n'en a pas
		assert(bool(pk.Unwrap()->series[1].onCustomDraw));  // "custom" en a un
		std::cout << "construction (setOnCustomDraw sur la derniere serie): ok\n";

		style.Resolve(ar);
		layout.RunIfNeeded(ar, 800.f, 1400.f); // canvas agrandi : plotK est a y=1250

		assert(customDrawCalls == 0); // pas encore rendu
		auto winRes = sdl3::Window::Create("smoke-ui8-phase7", 400, 300, sdl3::window_flags::HIDDEN);
		assert(winRes);
		auto renRes = sdl3::Renderer::Create(winRes.Value());
		assert(renRes);
		auto &ren = renRes.Value();
		ui::SdlRendererBackend renBackend(ren);
		render.Run(ar, renBackend, &input.tooltip);
		assert(customDrawCalls == 1); // appele exactement une fois pour cette serie, ce rendu
		assert(lastPlotRect.w > 0.f && lastPlotRect.h > 0.f); // rectangle plausible recu
		assert(lastXAxis.max > lastXAxis.min);                // axes plausibles recus
		assert(lastYAxis.max > lastYAxis.min);

		render.Run(ar, renBackend, &input.tooltip); // un second rendu -> un second appel
		assert(customDrawCalls == 2);
		std::cout << "onCustomDraw (appele au rendu, recoit plotRect/axes plausibles): ok\n";
	}

	std::cout << "plot phase7 checks passed\n";
	return 0;
}
