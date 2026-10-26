// Définitions de ui/plot.hpp
#include <ui/plot.hpp>
#include <ui/systems.hpp>

namespace ui {

// ── PlotSeries ───────────────────────────────────────────────────────────────

void PlotSeries::SetXy(std::span<const float> xs, std::span<const float> ys) {
	x.assign(xs.begin(), xs.end());
	y.assign(ys.begin(), ys.end());
}

void PlotSeries::SetY(std::span<const float> ys) {
	y.assign(ys.begin(), ys.end());
	if (x.size() != y.size()) {
		x.resize(y.size());
		for (size_t i = 0; i < x.size(); ++i)
			x[i] = float(i);
	}
}

namespace detail {

PlotEdge LegendMarginSide(LegendPosition pos) noexcept {
	switch (pos) {
		case LegendPosition::Left:
		case LegendPosition::TopLeft:
		case LegendPosition::BottomLeft:
			return PlotEdge::Left;
		case LegendPosition::Right:
		case LegendPosition::TopRight:
		case LegendPosition::BottomRight:
			return PlotEdge::Right;
		case LegendPosition::Top:
			return PlotEdge::Top;
		case LegendPosition::Bottom:
		default:
			return PlotEdge::Bottom;
	}
}

} // namespace detail

bool PlotHasSecondaryY(const PlotModel &plot) noexcept {
    for (const PlotSeries &s : plot.series)
        if (s.useSecondaryY)
            return true;
    return false;
}

size_t PlotLegendItemCount(const PlotModel &plot) noexcept {
    switch (plot.mode) {
    case PlotMode::PIE:
        return plot.pieSlices.size();
    case PlotMode::HEATMAP:
    case PlotMode::CANDLE:
        return 0;
    case PlotMode::XY:
    default:
        return plot.series.size();
    }
}

sdl3::FColor PlotPaletteColor(size_t seriesIndex) noexcept {
	return detail::K_PLOT_PALETTE[seriesIndex % detail::K_PLOT_PALETTE.size()];
}

float ToAxisSpace(const PlotAxis &axis, float value) noexcept {
	return axis.logScale ? std::log10(sdl3::Max(value, 1e-6f)) : value;
}

float FromAxisSpace(const PlotAxis &axis, float value) noexcept {
	return axis.logScale ? std::pow(10.f, value) : value;
}

float DataToScreen(const PlotAxis &axis, float screenMin, float screenMax, float value) noexcept {
	float lo = ToAxisSpace(axis, axis.min), hi = ToAxisSpace(axis, axis.max);
	float t = (ToAxisSpace(axis, value) - lo) / sdl3::Max(1e-6f, hi - lo);
	return screenMin + t * (screenMax - screenMin);
}

float SafeScreenSpan(float span) noexcept {
    return sdl3::Abs(span) < 1e-6f ? (span >= 0.f ? 1e-6f : -1e-6f) : span;
}

float ScreenToData(const PlotAxis &axis, float screenMin, float screenMax, float screenValue) noexcept {
    float lo = ToAxisSpace(axis, axis.min), hi = ToAxisSpace(axis, axis.max);
    float t = (screenValue - screenMin) / SafeScreenSpan(screenMax - screenMin);
    return FromAxisSpace(axis, lo + t * (hi - lo));
}

PlotMargins ComputePlotMargins(const PlotModel &plot) noexcept {
	PlotMargins m;
	// Graduations d'axes — XY ET Candle (Phase 6 : les bougies réutilisent
	// xAxis/yAxis pour leurs graduations, cf. resolveCandleAxis()) ; sans
	// objet en Pie/Heatmap (pas d'axes X/Y).
	if (plot.mode == PlotMode::XY || plot.mode == PlotMode::CANDLE) {
		if (!plot.xAxis.tickOverlay) {
			if (plot.xAxis.tickPosition == PlotEdge::Top)
				m.top += K_PLOT_TICK_MARGIN;
			else
				m.bottom += K_PLOT_TICK_MARGIN;
		}
		if (!plot.yAxis.tickOverlay) {
			if (plot.yAxis.tickPosition == PlotEdge::Right)
				m.right += K_PLOT_TICK_MARGIN;
			else
				m.left += K_PLOT_TICK_MARGIN;
		}
		if (PlotHasSecondaryY(plot) && !plot.yAxis2.tickOverlay) { // toujours faux en Candle (pas de series)
			if (plot.yAxis2.tickPosition == PlotEdge::Right)
				m.right += K_PLOT_TICK_MARGIN;
			else
				m.left += K_PLOT_TICK_MARGIN;
		}
	}
	if (plot.showLegend && !plot.legendOverlay) {
		float w = K_PLOT_LEGEND_WIDTH + 8.f;
		float h = float(PlotLegendItemCount(plot)) * K_PLOT_LEGEND_ROW_H + 14.f;
		switch (detail::LegendMarginSide(plot.legendPosition)) {
		case PlotEdge::Left:
			m.left += w;
			break;
		case PlotEdge::Right:
			m.right += w;
			break;
		case PlotEdge::Top:
			m.top += h;
			break;
		case PlotEdge::Bottom:
			m.bottom += h;
			break;
		}
	}
	return m;
}

sdl3::FRect ComputePlotRect(const PlotModel &plot, const sdl3::FRect &widgetRect) noexcept {
	PlotMargins m = ComputePlotMargins(plot);
	constexpr float K_BASE_PAD = 4.f;
	float x = widgetRect.x + K_BASE_PAD + m.left;
	float y = widgetRect.y + K_BASE_PAD + m.top;
	float w = sdl3::Max(0.f, widgetRect.w - 2.f * K_BASE_PAD - m.left - m.right);
	float h = sdl3::Max(0.f, widgetRect.h - 2.f * K_BASE_PAD - m.top - m.bottom);
	return {x, y, w, h};
}

sdl3::FRect AnchorRectIn(LegendPosition pos, const sdl3::FRect &area, float w, float h, float pad) noexcept {
	float x, y;
	switch (pos) {
	case LegendPosition::TopLeft:
		x = area.x + pad;
		y = area.y + pad;
		break;
	case LegendPosition::TopRight:
		x = area.x + area.w - w - pad;
		y = area.y + pad;
		break;
	case LegendPosition::BottomLeft:
		x = area.x + pad;
		y = area.y + area.h - h - pad;
		break;
	case LegendPosition::BottomRight:
		x = area.x + area.w - w - pad;
		y = area.y + area.h - h - pad;
		break;
	case LegendPosition::Left:
		x = area.x + pad;
		y = area.y + (area.h - h) * 0.5f;
		break;
	case LegendPosition::Right:
		x = area.x + area.w - w - pad;
		y = area.y + (area.h - h) * 0.5f;
		break;
	case LegendPosition::Top:
		x = area.x + (area.w - w) * 0.5f;
		y = area.y + pad;
		break;
	case LegendPosition::Bottom:
	default:
		x = area.x + (area.w - w) * 0.5f;
		y = area.y + area.h - h - pad;
		break;
	}
	return {x, y, w, h};
}

sdl3::FRect LegendReservedStrip(LegendPosition pos, const sdl3::FRect &widgetRect, const sdl3::FRect &plotRect) noexcept {
	switch (detail::LegendMarginSide(pos)) {
	case PlotEdge::Left:
		return {widgetRect.x, plotRect.y, sdl3::Max(0.f, plotRect.x - widgetRect.x), plotRect.h};
	case PlotEdge::Right:
		return {plotRect.x + plotRect.w, plotRect.y,
			   sdl3::Max(0.f, (widgetRect.x + widgetRect.w) - (plotRect.x + plotRect.w)), plotRect.h};
	case PlotEdge::Top:
		return {plotRect.x, widgetRect.y, plotRect.w, sdl3::Max(0.f, plotRect.y - widgetRect.y)};
	case PlotEdge::Bottom:
	default:
		return {plotRect.x, plotRect.y + plotRect.h, plotRect.w,
			   sdl3::Max(0.f, (widgetRect.y + widgetRect.h) - (plotRect.y + plotRect.h))};
	}
}

sdl3::FRect LegendBoxRect(const PlotModel &plot, const sdl3::FRect &widgetRect, const sdl3::FRect &plotRect) noexcept {
	float w = K_PLOT_LEGEND_WIDTH;
	float h = float(PlotLegendItemCount(plot)) * K_PLOT_LEGEND_ROW_H + 6.f;
	sdl3::FRect area = plot.legendOverlay ? plotRect : LegendReservedStrip(plot.legendPosition, widgetRect, plotRect);
	return AnchorRectIn(plot.legendPosition, area, w, h, 4.f);
}

sdl3::FRect LegendRowRect(const sdl3::FRect &legendBox, size_t index) noexcept {
	return {legendBox.x, legendBox.y + 3.f + float(index) * K_PLOT_LEGEND_ROW_H, legendBox.w, K_PLOT_LEGEND_ROW_H};
}

sdl3::FRect LegendSwatchRect(const sdl3::FRect &row) noexcept {
	return {row.x + 4.f, row.y + (row.h - 10.f) * 0.5f, 10.f, 10.f};
}

sdl3::FRect LegendLabelRect(const sdl3::FRect &row) noexcept {
	return {row.x + 18.f, row.y, sdl3::Max(0.f, row.w - 20.f), row.h};
}

int TickDecimals(float step) noexcept {
	if (!(step > 0.f) || !std::isfinite(step))
		return 2;
	if (step >= 0.999f)
		return 0;
	return sdl3::Clamp(int(std::ceil(-std::log10(double(step)))), 0, 4);
}

std::pair<float, float> ComputeAutoFit(const std::vector<PlotSeries> &series, bool forX,
		bool logScale,
		bool forSecondaryY) noexcept {
	bool any = false;
	float mn = 0.f, mx = 0.f;
	for (const PlotSeries &s : series) {
		if (!s.visible)
			continue;
		if (!forX && s.useSecondaryY != forSecondaryY)
			continue;
		const std::vector<float> &vals = forX ? s.x : s.y;
		// Barres, tiges et aires partent de 0 sur leur axe de valeur.
		const bool valueAxis = s.kind == PlotSeriesKind::BAR_H ? forX : !forX;
		if (valueAxis && !logScale && !vals.empty() &&
			(s.kind == PlotSeriesKind::BAR || s.kind == PlotSeriesKind::BAR_H || s.kind == PlotSeriesKind::HISTOGRAM ||
			 s.kind == PlotSeriesKind::STEM || s.kind == PlotSeriesKind::AREA)) {
			mn = any ? sdl3::Min(mn, 0.f) : 0.f;
			mx = any ? sdl3::Max(mx, 0.f) : 0.f;
			any = true;
		}
		// Les barres débordent de leur abscisse d'une demi-largeur.
		const bool barExtent = !valueAxis && (s.kind == PlotSeriesKind::BAR || s.kind == PlotSeriesKind::BAR_H ||
											  s.kind == PlotSeriesKind::HISTOGRAM);
		const float half = barExtent ? s.barWidth * 0.5f : 0.f;
		for (float v : vals) {
			if (logScale && v <= 0.f)
				continue;
			if (half > 0.f) {
				mn = any ? sdl3::Min(mn, v - half) : v - half;
				mx = any ? sdl3::Max(mx, v + half) : v + half;
				any = true;
				continue;
			}
			if (!any) {
				mn = mx = v;
				any = true;
			} else {
				mn = sdl3::Min(mn, v);
				mx = sdl3::Max(mx, v);
			}
		}
	}
	if (!any)
		return logScale ? std::pair{0.1f, 100.f} : std::pair{0.f, 1.f};
	if (logScale) {
		float lmn = std::log10(sdl3::Max(mn, 1e-6f)), lmx = std::log10(sdl3::Max(mx, 1e-6f));
		if (lmx - lmn < 1e-4f) {
			lmn -= 0.5f;
			lmx += 0.5f;
		}
		float pad = (lmx - lmn) * 0.05f;
		return {std::pow(10.f, lmn - pad), std::pow(10.f, lmx + pad)};
	}
	float range = mx - mn;
	if (range < 1e-6f) {
		mn -= 0.5f;
		mx += 0.5f;
		range = mx - mn;
	}
	float pad = range * 0.05f;
	return {mn - pad, mx + pad};
}

PlotAxis ResolveAxis(const PlotAxis &axis, const std::vector<PlotSeries> &series, bool forX, bool forSecondaryY) noexcept {
	PlotAxis resolved = axis;
	if (axis.autoFit) {
		auto [mn, mx] = ComputeAutoFit(series, forX, axis.logScale, forSecondaryY);
		resolved.min = mn;
		resolved.max = mx;
	}
	return resolved;
}

std::vector<float> GenerateLogTicks(float min, float max) {
	std::vector<float> ticks;
	if (!(max > min) || !(min > 0.f))
		return ticks;
	int lo = int(std::floor(std::log10(double(min))));
	int hi = int(std::ceil(std::log10(double(max))));
	for (int e = lo; e <= hi; ++e) {
		for (float m : {1.f, 2.f, 5.f}) {
			float v = m * std::pow(10.f, float(e));
			if (v >= min * (1.f - 1e-4f) && v <= max * (1.f + 1e-4f))
				ticks.push_back(v);
		}
	}
	return ticks;
}

std::vector<float> GenerateTicks(float min, float max, int targetCount) {
	std::vector<float> ticks;
	if (!(max > min) || targetCount <= 0)
		return ticks;
	float rawStep = (max - min) / float(targetCount);
	float mag = std::pow(10.f, std::floor(std::log10(rawStep)));
	float norm = rawStep / mag; // dans [1,10)
	float niceNorm = norm < 1.5f ? 1.f : (norm < 3.f ? 2.f : (norm < 7.f ? 5.f : 10.f));
	float step = niceNorm * mag;
	if (step <= 0.f || !std::isfinite(step))
		return ticks;
	float first = std::ceil(min / step) * step;
	for (float v = first; v <= max + step * 1e-4f; v += step)
		ticks.push_back(v);
	return ticks;
}

void DrawPlotGrid(PlotPen &ren, const sdl3::FRect &plotRect, const PlotAxis &xAxis, const PlotAxis &yAxis,
		sdl3::FColor gridColor) {
	ren.SetDrawColor(gridColor);
	if (xAxis.showGrid) {
		auto ticks = xAxis.logScale ? GenerateLogTicks(xAxis.min, xAxis.max) : GenerateTicks(xAxis.min, xAxis.max);
		for (float t : ticks) {
			float sx = DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, t);
			ren.DrawLine(sx, plotRect.y, sx, plotRect.y + plotRect.h);
		}
	}
	if (yAxis.showGrid) {
		auto ticks = yAxis.logScale ? GenerateLogTicks(yAxis.min, yAxis.max) : GenerateTicks(yAxis.min, yAxis.max);
		for (float t : ticks) {
			float sy = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, t);
			ren.DrawLine(plotRect.x, sy, plotRect.x + plotRect.w, sy);
		}
	}
}

void DrawPlotLegendBackground(PlotPen &ren, const PlotModel &plot, const sdl3::FRect &legendBox) {
	ren.SetDrawColor(plot.legendBoxColor);
	ren.FillRoundedRect(legendBox, math::Corners(3.f));
	size_t n = PlotLegendItemCount(plot);
	for (size_t i = 0; i < n; ++i) {
		sdl3::FRect row = LegendRowRect(legendBox, i);
		if (int(i) == plot.legendHover) {
			ren.SetDrawColor(sdl3::FColor{255/255.f, 255/255.f, 255/255.f, 30/255.f});
			ren.FillRect(row);
		}
		sdl3::FRect swatch = LegendSwatchRect(row);
		sdl3::FColor c = plot.mode == PlotMode::PIE ? plot.pieSlices[i].color : plot.series[i].color;
		bool visible = plot.mode == PlotMode::PIE ? plot.pieSlices[i].visible : plot.series[i].visible;
		if (!visible)
			c.a = c.a * 0.3f;
		ren.SetDrawColor(c);
		ren.FillRect(swatch);
	}
}

namespace detail {

void AppendThickSegment(std::vector<sdl3::Vertex> &verts, sdl3::FPoint a, sdl3::FPoint b, float halfW, sdl3::FColor color) {
	sdl3::FPoint dir{b.x - a.x, b.y - a.y};
	float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
	if (len < 0.0001f)
		return;
	sdl3::FPoint n{-dir.y / len * halfW, dir.x / len * halfW};
	sdl3::Vertex a0{{a.x + n.x, a.y + n.y}, color, {0.f, 0.f}};
	sdl3::Vertex a1{{a.x - n.x, a.y - n.y}, color, {0.f, 0.f}};
	sdl3::Vertex b0{{b.x + n.x, b.y + n.y}, color, {0.f, 0.f}};
	sdl3::Vertex b1{{b.x - n.x, b.y - n.y}, color, {0.f, 0.f}};
	verts.insert(verts.end(), {a0, a1, b0, a1, b1, b0});
}

void AppendFillQuad(std::vector<sdl3::Vertex> &verts, sdl3::FPoint a, sdl3::FPoint b, sdl3::FPoint baseA, sdl3::FPoint baseB,
		sdl3::FColor color) {
	sdl3::Vertex va{{a.x, a.y}, color, {0.f, 0.f}};
	sdl3::Vertex vb{{b.x, b.y}, color, {0.f, 0.f}};
	sdl3::Vertex vBaseA{{baseA.x, baseA.y}, color, {0.f, 0.f}};
	sdl3::Vertex vBaseB{{baseB.x, baseB.y}, color, {0.f, 0.f}};
	verts.insert(verts.end(), {va, vb, vBaseB, va, vBaseB, vBaseA});
}

void DrawMarker(PlotPen &ren, sdl3::FPoint center, PlotMarker marker, float size) {
	float r = size * 0.5f;
	switch (marker) {
	case PlotMarker::NONE:
		return;
	case PlotMarker::CIRCLE:
		ren.FillCircle(center, r);
		return;
	case PlotMarker::SQUARE:
		ren.FillRect({center.x - r, center.y - r, size, size});
		return;
	case PlotMarker::TRIANGLE: {
		std::array<sdl3::FPoint, 3> tri{sdl3::FPoint{center.x, center.y - r}, sdl3::FPoint{center.x + r, center.y + r},
								  sdl3::FPoint{center.x - r, center.y + r}};
		ren.FillPolygon(tri);
		return;
	}
	}
}

void DrawThickPolyline(PlotPen &ren, std::span<const sdl3::FPoint> pts, float lineWidth, sdl3::FColor color) {
	if (pts.size() < 2)
		return;
	ren.SetDrawColor(color);
	sdl3::FColor fcol = ren.GetDrawColorFloat();
	float halfW = sdl3::Max(0.5f, lineWidth * 0.5f);
	std::vector<sdl3::Vertex> verts;
	verts.reserve((pts.size() - 1) * 6);
	for (size_t i = 0; i + 1 < pts.size(); ++i)
		AppendThickSegment(verts, pts[i], pts[i + 1], halfW, fcol);
	ren.RenderGeometry(verts);
	if (pts.size() > 2) {
		ren.SetDrawColor(color);
		for (size_t i = 1; i + 1 < pts.size(); ++i)
			ren.FillCircle(pts[i], halfW);
	}
}

std::vector<sdl3::FPoint> StepPoints(std::span<const sdl3::FPoint> pts) {
	std::vector<sdl3::FPoint> out;
	if (pts.empty())
		return out;
	out.reserve(pts.size() * 2 - 1);
	out.push_back(pts[0]);
	for (size_t i = 1; i < pts.size(); ++i) {
		out.push_back({pts[i].x, pts[i - 1].y});
		out.push_back(pts[i]);
	}
	return out;
}

} // namespace detail

std::vector<sdl3::FPoint> PlotSeriesScreenPoints(const PlotSeries &s, const sdl3::FRect &plotRect,
		const PlotAxis &xAxis, const PlotAxis &yAxis) {
	std::vector<sdl3::FPoint> pts;
	size_t n = sdl3::Min(s.x.size(), s.y.size());
	pts.reserve(n);
	for (size_t i = 0; i < n; ++i)
		pts.push_back({DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, s.x[i]),
					   DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, s.y[i])});
	return pts;
}

void DrawPlotSeries(PlotPen &ren, const sdl3::FRect &plotRect, const PlotSeries &s, const PlotAxis &xAxis,
		const PlotAxis &yAxis, float dimFactor) {
	if (!s.visible)
		return;
	if (s.onCustomDraw) {
		s.onCustomDraw(ren.Paint(), plotRect, s, xAxis, yAxis);
		return;
	}
	std::vector<sdl3::FPoint> pts = PlotSeriesScreenPoints(s, plotRect, xAxis, yAxis);
	if (pts.empty())
		return;

	sdl3::FColor drawColor = s.color;
	drawColor.a = sdl3::Clamp(drawColor.a * dimFactor, 0.f, 1.f);

	switch (s.kind) {
	case PlotSeriesKind::LINE:
		detail::DrawThickPolyline(ren, pts, s.lineWidth, drawColor);
		break;
	case PlotSeriesKind::STEP:
		detail::DrawThickPolyline(ren, detail::StepPoints(pts), s.lineWidth, drawColor);
		break;
	case PlotSeriesKind::AREA: {
		// Remplissage sous la courbe, quad par quad (cf. appendFillQuad —
		// évite les artefacts d'un unique polygone en éventail sur une
		// courbe non convexe), PUIS la ligne elle-même par-dessus.
		float baseY =
			sdl3::Clamp(DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, 0.f), plotRect.y, plotRect.y + plotRect.h);
		sdl3::FColor fillCol = drawColor;
		fillCol.a = sdl3::Clamp(fillCol.a * s.fillOpacity, 0.f, 1.f);
		ren.SetDrawColor(fillCol);
		sdl3::FColor ffcol = ren.GetDrawColorFloat();
		std::vector<sdl3::Vertex> verts;
		verts.reserve((pts.size() - 1) * 6);
		for (size_t i = 0; i + 1 < pts.size(); ++i)
			detail::AppendFillQuad(verts, pts[i], pts[i + 1], {pts[i].x, baseY}, {pts[i + 1].x, baseY}, ffcol);
		ren.RenderGeometry(verts);
		detail::DrawThickPolyline(ren, pts, s.lineWidth, drawColor);
		break;
	}
	case PlotSeriesKind::BAR:
	case PlotSeriesKind::HISTOGRAM: {
		// Histogram = Bar sans espacement entre barres (jointives) — pas
		// d'autre différence de rendu, cf. doc de PlotSeriesKind.
		float baseY =
			sdl3::Clamp(DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, 0.f), plotRect.y, plotRect.y + plotRect.h);
		float halfWData = s.barWidth * 0.5f * (s.kind == PlotSeriesKind::HISTOGRAM ? 1.f : 0.8f);
		ren.SetDrawColor(drawColor);
		for (size_t i = 0; i < pts.size(); ++i) {
			float x0 = DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, s.x[i] - halfWData);
			float x1 = DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, s.x[i] + halfWData);
			float top = sdl3::Min(pts[i].y, baseY), bottom = sdl3::Max(pts[i].y, baseY);
			ren.FillRect({sdl3::Min(x0, x1), top, sdl3::Max(1.f, sdl3::Abs(x1 - x0)), sdl3::Max(1.f, bottom - top)});
		}
		break;
	}
	case PlotSeriesKind::BAR_H: {
		// x[i] = longueur de la barre (depuis la ligne de base 0), y[i] =
		// position/catégorie — cf. doc de PlotSeriesKind.
		float baseX =
			sdl3::Clamp(DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, 0.f), plotRect.x, plotRect.x + plotRect.w);
		float halfHData = s.barWidth * 0.5f * 0.8f;
		ren.SetDrawColor(drawColor);
		for (size_t i = 0; i < pts.size(); ++i) {
			float y0 = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, s.y[i] - halfHData);
			float y1 = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, s.y[i] + halfHData);
			float left = sdl3::Min(pts[i].x, baseX), right = sdl3::Max(pts[i].x, baseX);
			ren.FillRect({left, sdl3::Min(y0, y1), sdl3::Max(1.f, right - left), sdl3::Max(1.f, sdl3::Abs(y1 - y0))});
		}
		break;
	}
	case PlotSeriesKind::STEM: {
		float baseY =
			sdl3::Clamp(DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, 0.f), plotRect.y, plotRect.y + plotRect.h);
		ren.SetDrawColor(drawColor);
		for (sdl3::FPoint p : pts)
			ren.DrawLine(p.x, baseY, p.x, p.y);
		break; // le marqueur de pointe est dessiné par le bloc générique ci-dessous
	}
	case PlotSeriesKind::SCATTER:
	default:
		break;
	}

	// Barres d'erreur (Phase 5) — sous les marqueurs (dessinés juste après),
	// pour que le marqueur du point reste visible par-dessus sa propre barre.
	if (!s.yError.empty()) {
		ren.SetDrawColor(drawColor);
		size_t n = sdl3::Min(pts.size(), s.yError.size());
		constexpr float K_CAP_HALF_W = 4.f; ///< demi-largeur des petites moustaches horizontales
		for (size_t i = 0; i < n; ++i) {
			float err = sdl3::Max(0.f, s.yError[i]);
			if (err <= 0.f)
				continue;
			float yLo = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, s.y[i] - err);
			float yHi = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, s.y[i] + err);
			ren.DrawLine(pts[i].x, yLo, pts[i].x, yHi);
			ren.DrawLine(pts[i].x - K_CAP_HALF_W, yLo, pts[i].x + K_CAP_HALF_W, yLo);
			ren.DrawLine(pts[i].x - K_CAP_HALF_W, yHi, pts[i].x + K_CAP_HALF_W, yHi);
		}
	}

	// Marqueurs : toujours pour Scatter/Stem (look par défaut), sinon
	// seulement si explicitement posés (`.marker`) — un Bar/Area avec un
	// marqueur explicite reste possible (pas interdit), juste pas le défaut.
	if (s.marker != PlotMarker::NONE || s.kind == PlotSeriesKind::SCATTER || s.kind == PlotSeriesKind::STEM) {
		PlotMarker m = s.marker == PlotMarker::NONE ? PlotMarker::CIRCLE : s.marker;
		ren.SetDrawColor(drawColor);
		for (sdl3::FPoint p : pts)
			detail::DrawMarker(ren, p, m, s.markerSize);
	}
}

std::pair<int, int> PlotNearestPoint(const PlotModel &plot, const sdl3::FRect &plotRect,
		const PlotAxis &xAxis, const PlotAxis &yAxis,
		const PlotAxis &yAxis2, sdl3::FPoint mouseScreen,
		float maxDist) {
	int bestSeries = -1, bestIndex = -1;
	float bestDist = maxDist;
	for (size_t si = 0; si < plot.series.size(); ++si) {
		const PlotSeries &s = plot.series[si];
		if (!s.visible)
			continue;
		auto pts = PlotSeriesScreenPoints(s, plotRect, xAxis, s.useSecondaryY ? yAxis2 : yAxis);
		for (size_t i = 0; i < pts.size(); ++i) {
			float dx = pts[i].x - mouseScreen.x, dy = pts[i].y - mouseScreen.y;
			float d = std::sqrt(dx * dx + dy * dy);
			if (d < bestDist) {
				bestDist = d;
				bestSeries = int(si);
				bestIndex = int(i);
			}
		}
	}
	return {bestSeries, bestIndex};
}

std::vector<PieSliceAngles> ComputePieAngles(const std::vector<PieSlice> &slices) {
	std::vector<PieSliceAngles> out(slices.size());
	float total = 0.f;
	for (const PieSlice &s : slices)
		if (s.visible)
			total += sdl3::Max(0.f, s.value);
	if (total <= 0.f)
		return out;
	float angle = -90.f;
	for (size_t i = 0; i < slices.size(); ++i) {
		if (!slices[i].visible)
			continue;
		float sweep = sdl3::Max(0.f, slices[i].value) / total * 360.f;
		out[i] = {angle, angle + sweep};
		angle += sweep;
	}
	return out;
}

sdl3::FRect ComputePieCircleRect(const sdl3::FRect &plotRect) noexcept {
	float d = sdl3::Min(plotRect.w, plotRect.h);
	float cx = plotRect.x + plotRect.w * 0.5f, cy = plotRect.y + plotRect.h * 0.5f;
	return {cx - d * 0.5f, cy - d * 0.5f, d, d};
}

int PieHitTest(const std::vector<PieSlice> &slices, const std::vector<PieSliceAngles> &angles,
		sdl3::FPoint center, float radius, sdl3::FPoint mouseScreen) noexcept {
	float dx = mouseScreen.x - center.x, dy = mouseScreen.y - center.y;
	if (std::sqrt(dx * dx + dy * dy) > radius)
		return -1;
	float ang = std::atan2(dy, dx) * (180.f / sdl3::PI_F); // même convention que generateArcPoints (0=droite, horaire)
	for (size_t i = 0; i < slices.size(); ++i) {
		if (!slices[i].visible || angles[i].endDeg <= angles[i].startDeg)
			continue;
		float rel = ang - angles[i].startDeg;
		while (rel < 0.f)
			rel += 360.f;
		while (rel >= 360.f)
			rel -= 360.f;
		if (rel <= angles[i].endDeg - angles[i].startDeg)
			return int(i);
	}
	return -1;
}

namespace detail {

void AppendPieSliceFan(std::vector<sdl3::Vertex> &verts, sdl3::FPoint center, std::span<const sdl3::FPoint> arcPoints,
		sdl3::FColor color) {
	if (arcPoints.size() < 2)
		return;
	sdl3::Vertex centerV{{center.x, center.y}, color, {0.f, 0.f}};
	for (size_t i = 0; i + 1 < arcPoints.size(); ++i) {
		verts.push_back(centerV);
		verts.push_back({{arcPoints[i].x, arcPoints[i].y}, color, {0.f, 0.f}});
		verts.push_back({{arcPoints[i + 1].x, arcPoints[i + 1].y}, color, {0.f, 0.f}});
	}
}

} // namespace detail

void DrawPieChart(PlotPen &ren, const std::vector<PieSlice> &slices,
		const std::vector<PieSliceAngles> &angles, sdl3::FPoint center, float radius, int hoveredSlice,
		sdl3::FColor borderColor) {
	for (size_t i = 0; i < slices.size(); ++i) {
		if (!slices[i].visible || angles[i].endDeg <= angles[i].startDeg)
			continue;
		float dim = (hoveredSlice < 0 || int(i) == hoveredSlice) ? 1.f : 0.6f;
		sdl3::FColor c = slices[i].color;
		c.a = sdl3::Clamp(c.a * dim, 0.f, 1.f);
		ren.SetDrawColor(c);
		sdl3::FColor fcol = ren.GetDrawColorFloat();
		auto arc = sdl3::shapes::GenerateArcPoints(center, radius, angles[i].startDeg, angles[i].endDeg);
		std::vector<sdl3::Vertex> verts;
		detail::AppendPieSliceFan(verts, center, arc, fcol);
		ren.RenderGeometry(verts);
	}
	ren.SetDrawColor(borderColor);
	for (size_t i = 0; i < slices.size(); ++i) {
		if (!slices[i].visible || angles[i].endDeg <= angles[i].startDeg)
			continue;
		ren.DrawPie(center, radius, angles[i].startDeg, angles[i].endDeg);
	}
}

sdl3::FColor HeatmapColor(float t, Colormap cm) noexcept {
	t = sdl3::Clamp(t, 0.f, 1.f);
	switch (cm) {
	case Colormap::GRAYSCALE: {
		return {t, t, t, 1.0f};
	}
	case Colormap::COOL_WARM:
		if (t < 0.5f)
			return sdl3::FColor::Lerp(sdl3::FColor{40/255.0f, 60/255.0f, 200/255.0f, 1.0f}, sdl3::FColor{245/255.0f, 245/255.0f, 245/255.0f, 1.0f}, t / 0.5f, 1.f);
		return sdl3::FColor::Lerp(sdl3::FColor{245/255.0f, 245/255.0f, 245/255.0f, 1.0f}, sdl3::FColor{210/255.0f, 40/255.0f, 40/255.0f, 1.0f}, (t - 0.5f) / 0.5f);
	case Colormap::VIRIDIS:
	default:
		if (t < 0.5f)
			return sdl3::FColor::Lerp(sdl3::FColor{68/255.0f, 1/255.0f, 84/255.0f, 1.f}, sdl3::FColor{33/255.0f, 145/255.0f, 140/255.0f, 1.f}, t / 0.5f, 1.f);
		return sdl3::FColor::Lerp(sdl3::FColor{33/255.0f, 145/255.0f, 140/255.0f, 1.f}, sdl3::FColor{253/255.0f, 231/255.0f, 37/255.0f, 1.f}, (t - 0.5f) / 0.5f, 1.f);
	}
}

std::pair<float, float> ResolveHeatmapRange(const HeatmapData &hm) noexcept {
	if (!hm.autoRange)
		return {hm.minValue, hm.maxValue};
	bool any = false;
	float mn = 0.f, mx = 0.f;
	for (float v : hm.values) {
		if (!any) {
			mn = mx = v;
			any = true;
		} else {
			mn = sdl3::Min(mn, v);
			mx = sdl3::Max(mx, v);
		}
	}
	if (!any)
		return {0.f, 1.f};
	if (mx - mn < 1e-6f) {
		mn -= 0.5f;
		mx += 0.5f;
	}
	return {mn, mx};
}

void DrawHeatmap(PlotPen &ren, const sdl3::FRect &plotRect, const HeatmapData &hm) {
	if (hm.rows <= 0 || hm.cols <= 0 || int64_t(hm.values.size()) < int64_t(hm.rows) * int64_t(hm.cols))
		return;
	auto [mn, mx] = ResolveHeatmapRange(hm);
	float span = sdl3::Max(1e-6f, mx - mn);
	float cellW = plotRect.w / float(hm.cols);
	float cellH = plotRect.h / float(hm.rows);
	for (int r = 0; r < hm.rows; ++r) {
		for (int c = 0; c < hm.cols; ++c) {
			float v = hm.values[size_t(r) * size_t(hm.cols) + size_t(c)];
			float t = (v - mn) / span;
			ren.SetDrawColor(HeatmapColor(t, hm.colormap));
			ren.FillRect({plotRect.x + float(c) * cellW, plotRect.y + float(r) * cellH, cellW + 0.5f, cellH + 0.5f});
		}
	}
}

int HeatmapHitTest(const HeatmapData &hm, const sdl3::FRect &plotRect, sdl3::FPoint mouseScreen) noexcept {
	if (hm.rows <= 0 || hm.cols <= 0 || !plotRect.Contains(mouseScreen))
		return -1;
	int c = int((mouseScreen.x - plotRect.x) / (plotRect.w / float(hm.cols)));
	int r = int((mouseScreen.y - plotRect.y) / (plotRect.h / float(hm.rows)));
	if (r < 0 || r >= hm.rows || c < 0 || c >= hm.cols)
		return -1;
	return r * hm.cols + c;
}

std::pair<float, float> ComputeCandleAutoFit(const std::vector<OhlcBar> &bars, bool forX) noexcept {
	bool any = false;
	float mn = 0.f, mx = 0.f;
	for (const OhlcBar &b : bars) {
		if (forX) {
			if (!any) {
				mn = mx = b.x;
				any = true;
			} else {
				mn = sdl3::Min(mn, b.x);
				mx = sdl3::Max(mx, b.x);
			}
		} else {
			if (!any) {
				mn = b.low;
				mx = b.high;
				any = true;
			} else {
				mn = sdl3::Min(mn, b.low);
				mx = sdl3::Max(mx, b.high);
			}
		}
	}
	if (!any)
		return {0.f, 1.f};
	float range = mx - mn;
	if (range < 1e-6f) {
		mn -= 0.5f;
		mx += 0.5f;
		range = mx - mn;
	}
	float pad = range * 0.08f;
	return {mn - pad, mx + pad};
}

PlotAxis ResolveCandleAxis(const PlotAxis &axis, const std::vector<OhlcBar> &bars, bool forX) noexcept {
	PlotAxis resolved = axis;
	auto [mn, mx] = ComputeCandleAutoFit(bars, forX);
	resolved.min = mn;
	resolved.max = mx;
	return resolved;
}

sdl3::FRect ComputeCandleBodyRect(const sdl3::FRect &plotRect, const PlotAxis &xAxis, const PlotAxis &yAxis,
		const OhlcBar &bar, float candleWidth) noexcept {
	float halfW = candleWidth * 0.5f;
	float x0 = DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, bar.x - halfW);
	float x1 = DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, bar.x + halfW);
	float yOpen = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, bar.open);
	float yClose = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, bar.close);
	float top = sdl3::Min(yOpen, yClose), bottom = sdl3::Max(yOpen, yClose);
	return {sdl3::Min(x0, x1), top, sdl3::Max(1.f, sdl3::Abs(x1 - x0)), sdl3::Max(1.f, bottom - top)};
}

void DrawCandleChart(PlotPen &ren, const sdl3::FRect &plotRect, const std::vector<OhlcBar> &bars,
		const PlotAxis &xAxis, const PlotAxis &yAxis, float candleWidth, sdl3::FColor bullColor,
		sdl3::FColor bearColor, int hoveredBar) {
	for (size_t i = 0; i < bars.size(); ++i) {
		const OhlcBar &bar = bars[i];
		bool bull = bar.close >= bar.open;
		sdl3::FColor c = bull ? bullColor : bearColor;
		float dim = (hoveredBar < 0 || int(i) == hoveredBar) ? 1.f : 0.6f;
		c.a = sdl3::Clamp(c.a * dim, 0.f, 1.f);
		ren.SetDrawColor(c);
		float cx = DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, bar.x);
		float yHigh = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, bar.high);
		float yLow = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, bar.low);
		ren.DrawLine(cx, yHigh, cx, yLow);
		ren.FillRect(ComputeCandleBodyRect(plotRect, xAxis, yAxis, bar, candleWidth));
	}
}

int CandleNearestBar(const std::vector<OhlcBar> &bars, const sdl3::FRect &plotRect,
		const PlotAxis &xAxis, sdl3::FPoint mouseScreen, float maxDist) noexcept {
	int best = -1;
	float bestDist = maxDist;
	for (size_t i = 0; i < bars.size(); ++i) {
		float cx = DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, bars[i].x);
		float d = sdl3::Abs(cx - mouseScreen.x);
		if (d < bestDist) {
			bestDist = d;
			best = int(i);
		}
	}
	return best;
}

void ApplyPlotPan(PlotModel &plot, const sdl3::FRect &plotRect, sdl3::FPoint startMouse, sdl3::FPoint currentMouse,
		const PlotAxis &startXAxis, const PlotAxis &startYAxis,
		const PlotAxis &startYAxis2) noexcept {
	float dx = currentMouse.x - startMouse.x;
	float dy = currentMouse.y - startMouse.y;
	{
		float lo = ToAxisSpace(startXAxis, startXAxis.min), hi = ToAxisSpace(startXAxis, startXAxis.max);
		float delta = dx * (hi - lo) / sdl3::Max(1.f, plotRect.w);
		plot.xAxis.min = FromAxisSpace(startXAxis, lo - delta);
		plot.xAxis.max = FromAxisSpace(startXAxis, hi - delta);
		plot.xAxis.autoFit = false;
	}
	{
		float lo = ToAxisSpace(startYAxis, startYAxis.min), hi = ToAxisSpace(startYAxis, startYAxis.max);
		float delta = dy * (hi - lo) / sdl3::Max(1.f, plotRect.h);
		plot.yAxis.min = FromAxisSpace(startYAxis, lo + delta);
		plot.yAxis.max = FromAxisSpace(startYAxis, hi + delta);
		plot.yAxis.autoFit = false;
	}
	{
		float lo = ToAxisSpace(startYAxis2, startYAxis2.min), hi = ToAxisSpace(startYAxis2, startYAxis2.max);
		float delta = dy * (hi - lo) / sdl3::Max(1.f, plotRect.h);
		plot.yAxis2.min = FromAxisSpace(startYAxis2, lo + delta);
		plot.yAxis2.max = FromAxisSpace(startYAxis2, hi + delta);
		plot.yAxis2.autoFit = false;
	}
}

void ApplyPlotAxisZoom(PlotAxis &axis, const std::vector<PlotSeries> &series, bool forX, float screenMin,
		float screenMax, float mouseScreen, float wheelY,
		bool forSecondaryY) noexcept {
    PlotAxis resolved = ResolveAxis(axis, series, forX, forSecondaryY);
    float lo = ToAxisSpace(resolved, resolved.min), hi = ToAxisSpace(resolved, resolved.max);
    float t = (mouseScreen - screenMin) / SafeScreenSpan(screenMax - screenMin);
    float pivot = lo + t * (hi - lo);
	float factor = std::pow(0.9f, wheelY);
	float newLo = pivot - (pivot - lo) * factor;
	float newHi = pivot + (hi - pivot) * factor;
	axis.min = FromAxisSpace(resolved, newLo);
	axis.max = FromAxisSpace(resolved, newHi);
	axis.autoFit = false;
}

void ApplyPlotBoxZoom(PlotModel &plot, const sdl3::FRect &plotRect, sdl3::FPoint corner0, sdl3::FPoint corner1,
		const PlotAxis &startXAxis, const PlotAxis &startYAxis,
		const PlotAxis &startYAxis2) noexcept {
	float dx0 = ScreenToData(startXAxis, plotRect.x, plotRect.x + plotRect.w, corner0.x);
	float dx1 = ScreenToData(startXAxis, plotRect.x, plotRect.x + plotRect.w, corner1.x);
	plot.xAxis.min = sdl3::Min(dx0, dx1);
	plot.xAxis.max = sdl3::Max(dx0, dx1);
	plot.xAxis.autoFit = false;

	float dy0 = ScreenToData(startYAxis, plotRect.y + plotRect.h, plotRect.y, corner0.y);
	float dy1 = ScreenToData(startYAxis, plotRect.y + plotRect.h, plotRect.y, corner1.y);
	plot.yAxis.min = sdl3::Min(dy0, dy1);
	plot.yAxis.max = sdl3::Max(dy0, dy1);
	plot.yAxis.autoFit = false;

	float dy0b = ScreenToData(startYAxis2, plotRect.y + plotRect.h, plotRect.y, corner0.y);
	float dy1b = ScreenToData(startYAxis2, plotRect.y + plotRect.h, plotRect.y, corner1.y);
	plot.yAxis2.min = sdl3::Min(dy0b, dy1b);
	plot.yAxis2.max = sdl3::Max(dy0b, dy1b);
	plot.yAxis2.autoFit = false;
}

void ResetPlotToAutoFit(PlotModel &plot) noexcept {
	plot.xAxis.autoFit = true;
	plot.yAxis.autoFit = true;
	plot.yAxis2.autoFit = true;
}

void DrawPlotBoxZoomRect(PlotPen &ren, const PlotModel &plot) {
	if (!plot.boxZooming)
		return;
	float x0 = sdl3::Min(plot.dragStartMouse.x, plot.dragCurrentMouse.x);
	float x1 = sdl3::Max(plot.dragStartMouse.x, plot.dragCurrentMouse.x);
	float y0 = sdl3::Min(plot.dragStartMouse.y, plot.dragCurrentMouse.y);
	float y1 = sdl3::Max(plot.dragStartMouse.y, plot.dragCurrentMouse.y);
	sdl3::FRect r{x0, y0, sdl3::Max(1.f, x1 - x0), sdl3::Max(1.f, y1 - y0)};
	ren.SetDrawColor(sdl3::FColor{1.f, 1.f, 1.f, 40/255.f});
	ren.FillRect(r);
	ren.SetDrawColor(sdl3::FColor{1.f, 1.f, 1.f, 160/255.f});
	ren.DrawRect(r);
}

} // namespace ui

// ============================================================================
// PlotPen
// ============================================================================

namespace ui {

bool PlotPen::FillRect(const sdl3::FRect &r) {
	m_painter.FillRect(r, m_color);
	return true;
}

bool PlotPen::DrawRect(const sdl3::FRect &r) {
	m_painter.StrokeRoundedRect(r, math::Corners(), math::Sides(1.f), m_color);
	return true;
}

bool PlotPen::DrawLine(float x1, float y1, float x2, float y2) {
	m_painter.Line({x1, y1}, {x2, y2}, 1.f, m_color);
	return true;
}

bool PlotPen::FillRoundedRect(const sdl3::FRect &r, const math::Corners &c) {
	m_painter.FillRoundedRect(r, c, m_color);
	return true;
}

bool PlotPen::DrawRoundedRect(const sdl3::FRect &r, const math::Corners &c) {
	m_painter.StrokeRoundedRect(r, c, math::Sides(1.f), m_color);
	return true;
}

bool PlotPen::FillCircle(sdl3::FPoint center, float radius) {
	m_painter.FillCircle(center, radius, m_color);
	return true;
}

bool PlotPen::DrawCircle(sdl3::FPoint center, float radius) {
	m_painter.StrokeCircle(center, radius, 1.f, m_color);
	return true;
}

bool PlotPen::FillPolygon(std::span<const sdl3::FPoint> pts) {
	m_painter.FillPolygon(pts, m_color);
	return true;
}

bool PlotPen::DrawPolygon(std::span<const sdl3::FPoint> pts) {
	m_painter.Polyline(pts, 1.f, m_color, true);
	return true;
}

bool PlotPen::DrawPie(sdl3::FPoint center, float radius, float startDeg, float endDeg) {
	m_painter.Arc(center, radius, startDeg, endDeg - startDeg, 1.f, m_color);
	const float a0 = startDeg * math::PI / 180.f, a1 = endDeg * math::PI / 180.f;
	m_painter.Line(center, {center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius}, 1.f, m_color);
	m_painter.Line(center, {center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius}, 1.f, m_color);
	return true;
}

bool PlotPen::RenderGeometry(std::span<const sdl3::Vertex> vertices) {
	m_painter.Triangles(vertices);
	return true;
}

// ============================================================================
// PlotBuilder
// ============================================================================

PlotBuilder::PlotBuilder(PlotMode mode, String title) : Builder("plot") {
	m_model.mode = mode;
	m_model.title = std::move(title);
	// Axe Y principal à gauche, secondaire en face.
	m_model.yAxis.tickPosition = PlotEdge::Left;
	m_model.yAxis2.tickPosition = PlotEdge::Right;
	if (mode == PlotMode::PIE)
		spec.element.classes.push_back("pie");
	else if (mode == PlotMode::HEATMAP)
		spec.element.classes.push_back("heatmap");
	else if (mode == PlotMode::CANDLE)
		spec.element.classes.push_back("candlestick");
}

void PlotBuilder::Expand(WidgetSpec &s) const {
	s.components.push_back([model = m_model](ecs::ArchetypeRegistry &w, ecs::Entity e) { w.AddComponent(e, model); });
}

PlotBuilder &PlotBuilder::AddSeries(PlotSeriesKind kind, std::span<const float> x, std::span<const float> y, String label,
									Option<ui::Color> color) {
	PlotSeries s;
	s.kind = kind;
	s.SetXy(x, y);
	s.label = std::move(label);
	s.color = color.IsSome() ? color.Unwrap() : PlotPaletteColor(m_model.series.size());
	if (kind == PlotSeriesKind::SCATTER)
		s.marker = PlotMarker::CIRCLE;
	m_model.series.push_back(std::move(s));
	return *this;
}

PlotBuilder &PlotBuilder::AddLineSeries(std::span<const float> y, String label, Option<ui::Color> color) {
	std::vector<float> x(y.size());
	for (size_t i = 0; i < x.size(); ++i)
		x[i] = float(i);
	return AddSeries(PlotSeriesKind::LINE, x, y, std::move(label), color);
}

PlotBuilder &PlotBuilder::AddLineSeriesXy(std::span<const float> x, std::span<const float> y, String label,
										  Option<ui::Color> color) {
	return AddSeries(PlotSeriesKind::LINE, x, y, std::move(label), color);
}

PlotBuilder &PlotBuilder::AddScatterSeries(std::span<const float> x, std::span<const float> y, String label,
										   Option<ui::Color> color) {
	return AddSeries(PlotSeriesKind::SCATTER, x, y, std::move(label), color);
}

PlotBuilder &PlotBuilder::AddStepSeries(std::span<const float> x, std::span<const float> y, String label,
										Option<ui::Color> color) {
	return AddSeries(PlotSeriesKind::STEP, x, y, std::move(label), color);
}

PlotBuilder &PlotBuilder::AddAreaSeries(std::span<const float> x, std::span<const float> y, String label,
										Option<ui::Color> color, float fillOpacity) {
	AddSeries(PlotSeriesKind::AREA, x, y, std::move(label), color);
	m_model.series.back().fillOpacity = fillOpacity;
	return *this;
}

PlotBuilder &PlotBuilder::AddStemSeries(std::span<const float> x, std::span<const float> y, String label,
										Option<ui::Color> color) {
	AddSeries(PlotSeriesKind::STEM, x, y, std::move(label), color);
	m_model.series.back().marker = PlotMarker::CIRCLE;
	return *this;
}

PlotBuilder &PlotBuilder::AddBarSeries(std::span<const float> x, std::span<const float> y, String label,
									   Option<ui::Color> color, float barWidth) {
	AddSeries(PlotSeriesKind::BAR, x, y, std::move(label), color);
	m_model.series.back().barWidth = barWidth;
	return *this;
}

PlotBuilder &PlotBuilder::AddBarHSeries(std::span<const float> x, std::span<const float> y, String label,
										Option<ui::Color> color, float barWidth) {
	AddSeries(PlotSeriesKind::BAR_H, x, y, std::move(label), color);
	m_model.series.back().barWidth = barWidth;
	return *this;
}

PlotBuilder &PlotBuilder::AddHistogramSeries(std::span<const float> x, std::span<const float> y, String label,
											 Option<ui::Color> color, float barWidth) {
	AddSeries(PlotSeriesKind::HISTOGRAM, x, y, std::move(label), color);
	m_model.series.back().barWidth = barWidth;
	return *this;
}

PlotBuilder &PlotBuilder::SetYError(std::span<const float> err) {
	if (!m_model.series.empty())
		m_model.series.back().yError.assign(err.begin(), err.end());
	return *this;
}

PlotBuilder &PlotBuilder::UseSecondaryY(bool enable) {
	if (!m_model.series.empty())
		m_model.series.back().useSecondaryY = enable;
	return *this;
}

PlotBuilder &PlotBuilder::SetMarker(PlotMarker marker, float size) {
	if (!m_model.series.empty()) {
		m_model.series.back().marker = marker;
		m_model.series.back().markerSize = size;
	}
	return *this;
}

PlotBuilder &PlotBuilder::SetLineWidth(float width) {
	if (!m_model.series.empty())
		m_model.series.back().lineWidth = width;
	return *this;
}

PlotBuilder &PlotBuilder::SetOnCustomDraw(
	std::function<void(Painter &, const sdl3::FRect &, const PlotSeries &, const PlotAxis &, const PlotAxis &)> fn) {
	if (!m_model.series.empty())
		m_model.series.back().onCustomDraw = std::move(fn);
	return *this;
}

PlotBuilder &PlotBuilder::AddPieSlice(float value, String label, Option<ui::Color> color) {
	PieSlice s;
	s.value = value;
	s.label = std::move(label);
	s.color = color.IsSome() ? color.Unwrap() : PlotPaletteColor(m_model.pieSlices.size());
	m_model.pieSlices.push_back(std::move(s));
	return *this;
}

PlotBuilder &PlotBuilder::SetHeatmapData(int rows, int cols, std::span<const float> values, Colormap cm) {
	m_model.heatmap.rows = rows;
	m_model.heatmap.cols = cols;
	m_model.heatmap.values.assign(values.begin(), values.end());
	m_model.heatmap.colormap = cm;
	return *this;
}

PlotBuilder &PlotBuilder::AddOhlcBar(float x, float open, float high, float low, float close) {
	m_model.candleBars.push_back({x, open, high, low, close});
	return *this;
}

PlotBuilder &PlotBuilder::XTicks(PlotEdge position, bool overlay, Option<ui::Color> boxColor) {
	m_model.xAxis.tickPosition = position;
	m_model.xAxis.tickOverlay = overlay;
	if (boxColor.IsSome())
		m_model.xAxis.tickBoxColor = boxColor.Unwrap();
	return *this;
}

PlotBuilder &PlotBuilder::YTicks(PlotEdge position, bool overlay, Option<ui::Color> boxColor) {
	m_model.yAxis.tickPosition = position;
	m_model.yAxis.tickOverlay = overlay;
	if (boxColor.IsSome())
		m_model.yAxis.tickBoxColor = boxColor.Unwrap();
	return *this;
}

PlotBuilder &PlotBuilder::YTicks2(PlotEdge position, bool overlay, Option<ui::Color> boxColor) {
	m_model.yAxis2.tickPosition = position;
	m_model.yAxis2.tickOverlay = overlay;
	if (boxColor.IsSome())
		m_model.yAxis2.tickBoxColor = boxColor.Unwrap();
	return *this;
}

PlotBuilder &PlotBuilder::Legend(LegendPosition position, bool overlay, Option<ui::Color> boxColor) {
	m_model.legendPosition = position;
	m_model.legendOverlay = overlay;
	m_model.showLegend = true;
	if (boxColor.IsSome())
		m_model.legendBoxColor = boxColor.Unwrap();
	return *this;
}

PlotBuilder &PlotBuilder::ShowLegend(bool show) {
	m_model.showLegend = show;
	return *this;
}

PlotBuilder &PlotBuilder::LogScaleX(bool enable) {
	m_model.xAxis.logScale = enable;
	return *this;
}

PlotBuilder &PlotBuilder::LogScaleY(bool enable) {
	m_model.yAxis.logScale = enable;
	return *this;
}

PlotBuilder &PlotBuilder::LogScaleY2(bool enable) {
	m_model.yAxis2.logScale = enable;
	return *this;
}

PlotBuilder &PlotBuilder::XRange(float min, float max) {
	m_model.xAxis.min = min;
	m_model.xAxis.max = max;
	m_model.xAxis.autoFit = false;
	return *this;
}

PlotBuilder &PlotBuilder::YRange(float min, float max) {
	m_model.yAxis.min = min;
	m_model.yAxis.max = max;
	m_model.yAxis.autoFit = false;
	return *this;
}

PlotBuilder Plot(String title) { return PlotBuilder(PlotMode::XY, std::move(title)); }

PlotBuilder PlotLines(std::span<const float> values, String title, Option<ui::Color> color) {
	PlotBuilder b(PlotMode::XY, std::move(title));
	b.AddLineSeries(values, String(), color);
	b.ShowLegend(false);
	return b;
}

PlotBuilder Pie(String title) { return PlotBuilder(PlotMode::PIE, std::move(title)); }
PlotBuilder Heatmap(String title) { return PlotBuilder(PlotMode::HEATMAP, std::move(title)); }
PlotBuilder Candlestick(String title) { return PlotBuilder(PlotMode::CANDLE, std::move(title)); }

// ============================================================================
// Extension : dessin et navigation
// ============================================================================

namespace {

struct PlotText {
	Context &ctx;
	const ComputedStyle &cs;

	sdl3::Font *Font(float size) const { return ctx.fonts.Resolve(cs.fontFamily.View(), size); }
	sdl3::FPoint Measure(const String &s, float size) const { return ctx.painter.MeasureText(s, Font(size), size); }
	void Draw(const String &s, float size, Color c, float x, float y) const { ctx.painter.Text(s, Font(size), c, x, y); }
};

/// Libellé d'une graduation (jamais « -0 » : arrondi à `dec` décimales).
String TickLabel(float v, int dec) {
	return String::From(std::fabs(v) < 0.5f * std::pow(10.f, -float(dec)) ? 0.f : v, dec);
}

void DrawAxisTicks(const PlotText &t, PlotPen &pen, const sdl3::FRect &plotRect, const PlotAxis &axis,
				   const PlotAxis &resolved, bool isX, float tickFs, Color tickColor) {
	auto ticks = resolved.logScale ? GenerateLogTicks(resolved.min, resolved.max) : GenerateTicks(resolved.min, resolved.max);
	const int dec = ticks.size() >= 2 ? TickDecimals(ticks[1] - ticks[0]) : 1;
	if (isX) {
		const bool top = axis.tickPosition == PlotEdge::Top;
		const float ly = axis.tickOverlay ? (top ? plotRect.y + 2.f : plotRect.y + plotRect.h - tickFs - 2.f)
										  : (top ? plotRect.y - K_PLOT_TICK_MARGIN + 2.f : plotRect.y + plotRect.h + 4.f);
		if (axis.tickOverlay && !ticks.empty()) {
			pen.SetDrawColor(axis.tickBoxColor);
			pen.FillRect({plotRect.x, ly - 1.f, plotRect.w, tickFs + 2.f});
		}
		for (float v : ticks) {
			const float sx = DataToScreen(resolved, plotRect.x, plotRect.x + plotRect.w, v);
			const String label = TickLabel(v, dec);
			const sdl3::FPoint sz = t.Measure(label, tickFs);
			t.Draw(label, tickFs, tickColor, sx - sz.x * 0.5f, ly);
		}
		return;
	}
	const bool right = axis.tickPosition == PlotEdge::Right;
	const float boxW = K_PLOT_TICK_MARGIN - 2.f;
	if (axis.tickOverlay && !ticks.empty()) {
		pen.SetDrawColor(axis.tickBoxColor);
		pen.FillRect({right ? plotRect.x + plotRect.w - boxW : plotRect.x, plotRect.y, boxW, plotRect.h});
	}
	for (float v : ticks) {
		const float sy = DataToScreen(resolved, plotRect.y + plotRect.h, plotRect.y, v);
		const String label = TickLabel(v, dec);
		const sdl3::FPoint sz = t.Measure(label, tickFs);
		const float lx = axis.tickOverlay ? (right ? plotRect.x + plotRect.w - sz.x - 3.f : plotRect.x + 3.f)
										  : (right ? plotRect.x + plotRect.w + 3.f : plotRect.x - sz.x - 3.f);
		t.Draw(label, tickFs, tickColor, lx, sy - sz.y * 0.5f);
	}
}

void DrawTopLabel(const PlotText &t, const sdl3::FRect &s, float centerX, const String &text, Color c) {
	const float fs = 12.f * t.cs.scale;
	const sdl3::FPoint sz = t.Measure(text, fs);
	const float tx = math::Clamp(centerX - sz.x * 0.5f, s.x, math::Max(s.x, s.x + s.w - sz.x));
	t.Draw(text, fs, c, tx, s.y + 2.f);
}

void DrawLegendLabels(const PlotText &t, const PlotModel &p, const sdl3::FRect &legendBox, Color tickColor) {
	const float fs = 11.f * t.cs.scale;
	const size_t n = p.mode == PlotMode::PIE ? p.pieSlices.size() : p.series.size();
	for (size_t i = 0; i < n; ++i) {
		const String &label = p.mode == PlotMode::PIE ? p.pieSlices[i].label : p.series[i].label;
		const bool visible = p.mode == PlotMode::PIE ? p.pieSlices[i].visible : p.series[i].visible;
		if (label.IsEmpty())
			continue;
		const sdl3::FRect labelRect = LegendLabelRect(LegendRowRect(legendBox, i));
		Color lc = tickColor;
		if (!visible)
			lc.a *= 0.5f;
		const sdl3::FPoint sz = t.Measure(label, fs);
		t.Draw(label, fs, lc, labelRect.x, labelRect.y + (labelRect.h - sz.y) * 0.5f);
	}
}

void DrawPlot(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e, const Rect &rect, const ComputedStyle &cs) {
	auto model = world.GetComponent<PlotModel>(e);
	if (model.IsNone())
		return;
	const PlotModel &p = *model.Unwrap();
	Painter &painter = ctx.painter;
	PlotPen pen(painter);
	const sdl3::FRect s = rect.box;
	const PlotText text{ctx, cs};
	const Color textColor = cs.color;
	const Color tickColor = WithAlpha(cs.color, cs.color.a * 0.8f);
	const float tickFs = 11.f * cs.scale;
	painter.PushClip(s);
	const sdl3::FRect plotRect = ComputePlotRect(p, s);

	if (p.mode == PlotMode::PIE) {
		const sdl3::FRect circleRect = ComputePieCircleRect(plotRect);
		const sdl3::FPoint center{circleRect.x + circleRect.w * 0.5f, circleRect.y + circleRect.h * 0.5f};
		const auto angles = ComputePieAngles(p.pieSlices);
		const int hovered = p.legendHover >= 0 ? p.legendHover : p.hoveredSeries;
		DrawPieChart(pen, p.pieSlices, angles, center, circleRect.w * 0.5f, hovered, cs.borderColor);
		if (hovered >= 0 && size_t(hovered) < p.pieSlices.size()) {
			const PieSlice &hs = p.pieSlices[size_t(hovered)];
			float total = 0.f;
			for (const PieSlice &sl : p.pieSlices)
				if (sl.visible)
					total += math::Max(0.f, sl.value);
			const float pct = total > 0.f ? math::Max(0.f, hs.value) / total * 100.f : 0.f;
			String full = hs.label.IsEmpty() ? String::From(hs.value, 1) : hs.label;
			full += String(" (") + String::From(pct, 1) + "%)";
			DrawTopLabel(text, s, s.x + s.w * 0.5f, full, textColor);
		}
		if (p.showLegend && !p.pieSlices.empty()) {
			const sdl3::FRect legendBox = LegendBoxRect(p, s, plotRect);
			DrawPlotLegendBackground(pen, p, legendBox);
			DrawLegendLabels(text, p, legendBox, tickColor);
		}
	} else if (p.mode == PlotMode::HEATMAP) {
		DrawHeatmap(pen, plotRect, p.heatmap);
		if (p.hoveredSeries >= 0 && size_t(p.hoveredSeries) < p.heatmap.values.size())
			DrawTopLabel(text, s, s.x + s.w * 0.5f, String::From(p.heatmap.values[size_t(p.hoveredSeries)], 2), textColor);
	} else if (p.mode == PlotMode::CANDLE) {
		const PlotAxis xr = ResolveCandleAxis(p.xAxis, p.candleBars, true);
		const PlotAxis yr = ResolveCandleAxis(p.yAxis, p.candleBars, false);
		DrawPlotGrid(pen, plotRect, xr, yr, WithAlpha(cs.color, 0.08f));
		DrawCandleChart(pen, plotRect, p.candleBars, xr, yr, p.candleWidth, p.candleBullColor, p.candleBearColor,
						p.hoveredSeries);
		DrawAxisTicks(text, pen, plotRect, p.xAxis, xr, true, tickFs, tickColor);
		DrawAxisTicks(text, pen, plotRect, p.yAxis, yr, false, tickFs, tickColor);
		if (p.hoveredSeries >= 0 && size_t(p.hoveredSeries) < p.candleBars.size()) {
			const OhlcBar &b = p.candleBars[size_t(p.hoveredSeries)];
			DrawTopLabel(text, s, s.x + s.w * 0.5f,
						 String("O:") + String::From(b.open, 2) + " H:" + String::From(b.high, 2) + " L:" +
							 String::From(b.low, 2) + " C:" + String::From(b.close, 2),
						 textColor);
		}
	} else {
		const PlotAxis xr = ResolveAxis(p.xAxis, p.series, true);
		const PlotAxis yr = ResolveAxis(p.yAxis, p.series, false);
		const bool hasY2 = PlotHasSecondaryY(p);
		const PlotAxis yr2 = hasY2 ? ResolveAxis(p.yAxis2, p.series, false, true) : PlotAxis{};
		DrawPlotGrid(pen, plotRect, xr, yr, WithAlpha(cs.color, 0.08f));
		painter.PushClip(plotRect);
		for (size_t i = 0; i < p.series.size(); ++i) {
			const float dim = (p.legendHover < 0 || int(i) == p.legendHover) ? 1.f : 0.25f;
			DrawPlotSeries(pen, plotRect, p.series[i], xr, p.series[i].useSecondaryY ? yr2 : yr, dim);
		}
		painter.PopClip();
		DrawAxisTicks(text, pen, plotRect, p.xAxis, xr, true, tickFs, tickColor);
		DrawAxisTicks(text, pen, plotRect, p.yAxis, yr, false, tickFs, tickColor);
		if (hasY2)
			DrawAxisTicks(text, pen, plotRect, p.yAxis2, yr2, false, tickFs, tickColor);
		if (p.hoveredSeries >= 0 && size_t(p.hoveredSeries) < p.series.size()) {
			const PlotSeries &hs = p.series[size_t(p.hoveredSeries)];
			if (size_t(p.hoveredIndex) < hs.x.size() && size_t(p.hoveredIndex) < hs.y.size()) {
				const float hx = DataToScreen(xr, plotRect.x, plotRect.x + plotRect.w, hs.x[size_t(p.hoveredIndex)]);
				pen.SetDrawColor(WithAlpha(cs.color, 0.4f));
				pen.DrawLine(hx, plotRect.y, hx, plotRect.y + plotRect.h);
				DrawTopLabel(text, s, hx, String::From(hs.y[size_t(p.hoveredIndex)], 2), textColor);
			}
		}
		if (p.showLegend && !p.series.empty()) {
			const sdl3::FRect legendBox = LegendBoxRect(p, s, plotRect);
			DrawPlotLegendBackground(pen, p, legendBox);
			DrawLegendLabels(text, p, legendBox, tickColor);
		}
		DrawPlotBoxZoomRect(pen, p);
	}
	if (!p.title.IsEmpty()) {
		const float fs = 13.f * cs.scale;
		const sdl3::FPoint sz = text.Measure(p.title, fs);
		text.Draw(p.title, fs, textColor, s.x + (s.w - sz.x) * 0.5f, s.y + 2.f);
	}
	painter.PopClip();
}

ecs::Entity PlotUnder(ecs::ArchetypeRegistry &world, sdl3::FPoint p) {
	return NearestWith<PlotModel>(world, HitTest(world, p));
}

void UpdatePlotHover(ecs::ArchetypeRegistry &world, ecs::Entity e, PlotModel &p, sdl3::FPoint mouse) {
	const sdl3::FRect s = Widget(world, e).Bounds();
	const sdl3::FRect plotRect = ComputePlotRect(p, s);
	p.hoveredSeries = p.hoveredIndex = -1;
	p.legendHover = -1;
	if (!p.hovered)
		return;
	const size_t legendItems = p.mode == PlotMode::PIE ? p.pieSlices.size() : p.series.size();
	if (p.showLegend && (p.mode == PlotMode::XY || p.mode == PlotMode::PIE)) {
		const sdl3::FRect legendBox = LegendBoxRect(p, s, plotRect);
		for (size_t i = 0; i < legendItems; ++i)
			if (LegendRowRect(legendBox, i).Contains(mouse)) {
				p.legendHover = int(i);
				return;
			}
	}
	switch (p.mode) {
	case PlotMode::PIE: {
		const sdl3::FRect circle = ComputePieCircleRect(plotRect);
		p.hoveredSeries = PieHitTest(p.pieSlices, ComputePieAngles(p.pieSlices),
									 {circle.x + circle.w * 0.5f, circle.y + circle.h * 0.5f}, circle.w * 0.5f, mouse);
		break;
	}
	case PlotMode::HEATMAP:
		p.hoveredSeries = HeatmapHitTest(p.heatmap, plotRect, mouse);
		break;
	case PlotMode::CANDLE:
		p.hoveredSeries = CandleNearestBar(p.candleBars, plotRect, ResolveCandleAxis(p.xAxis, p.candleBars, true), mouse);
		break;
	case PlotMode::XY: {
		const PlotAxis xr = ResolveAxis(p.xAxis, p.series, true);
		const PlotAxis yr = ResolveAxis(p.yAxis, p.series, false);
		const PlotAxis yr2 = ResolveAxis(p.yAxis2, p.series, false, true);
		auto [si, idx] = PlotNearestPoint(p, plotRect, xr, yr, yr2, mouse);
		p.hoveredSeries = si;
		p.hoveredIndex = idx;
		break;
	}
	}
}

bool PlotEvent(ecs::ArchetypeRegistry &world, Context &ctx, const sdl3::Event &ev) {
	(void)ctx;
	if (ev.IsMouseMotion()) {
		const sdl3::FPoint m{ev.MouseMotion().x, ev.MouseMotion().y};
		const ecs::Entity under = PlotUnder(world, m);
		bool gesture = false;
		world.Query<PlotModel>([&](ecs::Entity e, PlotModel &p) {
			p.hovered = e == under;
			const sdl3::FRect plotRect = ComputePlotRect(p, Widget(world, e).Bounds());
			if (p.panning) {
				p.dragCurrentMouse = m;
				ApplyPlotPan(p, plotRect, p.dragStartMouse, p.dragCurrentMouse, p.dragStartXAxis, p.dragStartYAxis,
							 p.dragStartYAxis2);
				gesture = true;
			} else if (p.boxZooming) {
				p.dragCurrentMouse = m;
				gesture = true;
			} else {
				UpdatePlotHover(world, e, p, m);
			}
		});
		return gesture;
	}
	if (ev.IsMouseDown() || ev.IsMouseUp()) {
		const sdl3::FPoint m{ev.MouseButton().x, ev.MouseButton().y};
		const bool left = ev.MouseButton().button == SDL_BUTTON_LEFT;
		const bool right = ev.MouseButton().button == SDL_BUTTON_RIGHT;
		if (ev.IsMouseUp()) {
			bool ended = false;
			world.Query<PlotModel>([&](ecs::Entity e, PlotModel &p) {
				if (left && p.panning) {
					p.panning = false;
					ended = true;
				}
				if (right && p.boxZooming) {
					p.boxZooming = false;
					ended = true;
					const float dw = std::fabs(m.x - p.dragStartMouse.x), dh = std::fabs(m.y - p.dragStartMouse.y);
					if (dw > 4.f && dh > 4.f)
						ApplyPlotBoxZoom(p, ComputePlotRect(p, Widget(world, e).Bounds()), p.dragStartMouse, m,
										 p.dragStartXAxis, p.dragStartYAxis, p.dragStartYAxis2);
				}
			});
			return ended;
		}
		const ecs::Entity e = PlotUnder(world, m);
		if (!e.Valid() || (!left && !right))
			return false;
		PlotModel &p = *world.GetComponent<PlotModel>(e).Unwrap();
		p.hovered = true;
		UpdatePlotHover(world, e, p, m);
		if (p.legendHover >= 0) {
			if (left) {
				if (p.mode == PlotMode::PIE)
					p.pieSlices[size_t(p.legendHover)].visible = !p.pieSlices[size_t(p.legendHover)].visible;
				else
					p.series[size_t(p.legendHover)].visible = !p.series[size_t(p.legendHover)].visible;
			}
			return true;
		}
		if (p.mode != PlotMode::XY)
			return false;
		if (left && ev.MouseButton().clicks >= 2) {
			ResetPlotToAutoFit(p);
			return true;
		}
		p.dragStartMouse = p.dragCurrentMouse = m;
		p.dragStartXAxis = ResolveAxis(p.xAxis, p.series, true);
		p.dragStartYAxis = ResolveAxis(p.yAxis, p.series, false);
		p.dragStartYAxis2 = ResolveAxis(p.yAxis2, p.series, false, true);
		p.hoveredSeries = p.hoveredIndex = -1;
		if (left)
			p.panning = true;
		else
			p.boxZooming = true;
		return true;
	}
	if (ev.IsMouseWheel()) {
		const sdl3::FPoint m{ev.MouseWheel().mouse_x, ev.MouseWheel().mouse_y};
		const ecs::Entity e = PlotUnder(world, m);
		if (!e.Valid())
			return false;
		PlotModel &p = *world.GetComponent<PlotModel>(e).Unwrap();
		if (p.mode != PlotMode::XY)
			return false;
		const sdl3::FRect plotRect = ComputePlotRect(p, Widget(world, e).Bounds());
		const float wheel = ev.MouseWheel().y;
		const bool overX = !p.xAxis.tickOverlay && ((p.xAxis.tickPosition == PlotEdge::Bottom && m.y > plotRect.y + plotRect.h) ||
													(p.xAxis.tickPosition == PlotEdge::Top && m.y < plotRect.y));
		const bool overY = !p.yAxis.tickOverlay && ((p.yAxis.tickPosition == PlotEdge::Right && m.x > plotRect.x + plotRect.w) ||
													(p.yAxis.tickPosition == PlotEdge::Left && m.x < plotRect.x));
		const bool overY2 = PlotHasSecondaryY(p) && !p.yAxis2.tickOverlay &&
							((p.yAxis2.tickPosition == PlotEdge::Right && m.x > plotRect.x + plotRect.w) ||
							 (p.yAxis2.tickPosition == PlotEdge::Left && m.x < plotRect.x));
		if (overX) {
			ApplyPlotAxisZoom(p.xAxis, p.series, true, plotRect.x, plotRect.x + plotRect.w, m.x, wheel);
		} else if (overY) {
			ApplyPlotAxisZoom(p.yAxis, p.series, false, plotRect.y + plotRect.h, plotRect.y, m.y, wheel);
		} else if (overY2) {
			ApplyPlotAxisZoom(p.yAxis2, p.series, false, plotRect.y + plotRect.h, plotRect.y, m.y, wheel, true);
		} else {
			ApplyPlotAxisZoom(p.xAxis, p.series, true, plotRect.x, plotRect.x + plotRect.w, m.x, wheel);
			ApplyPlotAxisZoom(p.yAxis, p.series, false, plotRect.y + plotRect.h, plotRect.y, m.y, wheel);
			if (PlotHasSecondaryY(p))
				ApplyPlotAxisZoom(p.yAxis2, p.series, false, plotRect.y + plotRect.h, plotRect.y, m.y, wheel, true);
		}
		return true;
	}
	return false;
}

} // namespace

void RegisterPlotExtension(Context &ctx) {
	Extension ext;
	ext.name = "plot";
	ext.draw = DrawPlot;
	ext.event = PlotEvent;
	ext.measure = [](ecs::ArchetypeRegistry &world, Context &, ecs::Entity e,
					 const ComputedStyle &) -> Option<sdl3::FPoint> {
		if (world.HasComponent<PlotModel>(e))
			return Some(sdl3::FPoint{200.f, 120.f});
		return NONE;
	};
	RegisterExtension(ctx, std::move(ext));
}

} // namespace ui
