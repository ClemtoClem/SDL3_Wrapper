#pragma once
/**
 * ui::UiPlot — widget de graphiques 2D façon ImPlot (epezent/implot), adapté à
 * l'architecture ECS + factory/builder de ce module (cf. plan
 * mossy-forging-fog.md, section "Widgets de plots/graphiques 2D").
 *
 * Contrairement au node-graph (ui/nodegraph.hpp), un plot n'a besoin
 * D'AUCUNE entité par point/série : c'est un widget UNIQUE, auto-dessiné,
 * exactement comme `UiTable`/`UiCalendar` — pas de classe système séparée,
 * pas de `prepass()`, pas d'appel supplémentaire dans la boucle applicative.
 * `Plot` se construit et s'utilise comme n'importe quel autre widget
 * (`f.Plot()...Spawn()`), et passe par les points d'extension EXISTANTS
 * (`RenderSystem::drawWidget`, `InputSystem::dispatch`, cf. systems.hpp).
 *
 * Ce fichier fournit les DONNÉES (composant `UiPlot` + `PlotSeries`/
 * `PlotAxis`) et les FONCTIONS LIBRES de transform/rendu-géométrie/
 * génération de graduations — le dessin de TEXTE (labels d'axes, légende)
 * reste dans `systems.hpp` (`RenderSystem::drawTextRaw`/`measureCached` sont
 * des méthodes privées de `RenderSystem`, pas accessibles d'ici).
 *
 * Remplace l'ancien widget `UiPlot` (Phase 7 de l'initiative précédente,
 * PlotKind::Lines/Histogram, une seule série Y implicite) — un seul « le »
 * widget de plot, pas deux mécanismes parallèles.
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "components.hpp"
#include "render_backend.hpp"
#include "../sdl3/structs.hpp"

namespace ui {

// ============================================================================
// Séries
// ============================================================================

/// Type de tracé d'une série. `Bar`/`BarH`/`Histogram` dessinent depuis une
/// ligne de base à 0 (barres pleines) ; `BarH` échange le rôle des axes
/// (`x[i]` = longueur de la barre, `y[i]` = position/catégorie) — même
/// convention que `ImPlot::PlotBarsH`. `Histogram` est visuellement
/// identique à `Bar` mais sans espacement entre barres (barres jointives,
/// look histogramme classique) — pas de binning automatique de valeurs
/// brutes : l'appelant fournit déjà `x[]`=centres de classe, `y[]`=effectifs
/// (même choix que faisait déjà l'ancien widget).
enum class PlotSeriesKind : uint8_t { LINE, SCATTER, BAR, BAR_H, HISTOGRAM, AREA, STEM, STEP };

/// Forme de marqueur pour Scatter (et les points de Line, si posée).
enum class PlotMarker : uint8_t { NONE, CIRCLE, SQUARE, TRIANGLE };

/// Une série de données — DONNÉE PURE dans `UiPlot::series` (pas d'entité,
/// même idiome que `GraphConnection`/`UiTable::rows` : un plot peut avoir
/// des milliers de points, une entité par point serait absurde). `x`/`y`
/// sont COPIÉS (même raison que l'ancien `UiPlot::values` : un composant ECS
/// retenu doit posséder ses données d'une frame à l'autre).
struct PlotSeries {
	PlotSeriesKind kind = PlotSeriesKind::LINE;
	std::vector<float> x, y;
	sdl3::FColor color = sdl3::FColor::WHITE(); ///< résolu depuis la palette par défaut si non précisé à l'ajout
	float lineWidth = 2.f;
	PlotMarker marker = PlotMarker::NONE;
	float markerSize = 4.f;
	/// Largeur de barre en unités DONNÉES (pas pixels — même convention que
	/// `ImPlot::PlotBars`) — Bar/BarH/Histogram uniquement. 0.67 par défaut
	/// (adapté à des x régulièrement espacés de 1, comme des indices).
	float barWidth = 0.67f;
	/// Opacité du remplissage sous la courbe — Area uniquement, dans [0,1].
	float fillOpacity = 0.35f;
	String label; ///< nom affiché dans la légende (Phase 2)
	bool visible = true;
	/// Si vrai, la série se réfère à `UiPlot::yAxis2` (axe Y secondaire,
	/// Phase 4) au lieu de `UiPlot::yAxis` — `UiPlot::yAxis2` n'est affiché
	/// que si AU MOINS une série l'utilise (cf. `plotHasSecondaryY()`), pas
	/// de flag séparé à synchroniser sur `Plot` lui-même.
	bool useSecondaryY = false;
	/// Barres d'erreur Y (Phase 5) — écart symétrique ± autour de `y[i]`,
	/// même taille que `x`/`y` si posé ; vide (défaut) = pas de barres.
	/// Prévu pour Line/Scatter (pas de garde technique empêchant un autre
	/// type, mais visuellement peu utile sur Bar/Area par exemple).
	std::vector<float> yError;

	/// Échappatoire de rendu personnalisé (Phase 7, spec : « widget
	/// configurable ») — si posé, REMPLACE le rendu par défaut de cette
	/// série ; reçoit le rectangle de tracé écran et les axes courants (pour
	/// que l'appelant puisse lui-même convertir données→écran via
	/// `dataToScreen()` s'il en a besoin).
	std::function<void(sdl3::Renderer &, const sdl3::FRect &plotRect, const PlotSeries &, const struct PlotAxis &xAxis,
					   const struct PlotAxis &yAxis)>
		onCustomDraw;

	/// Remplace x ET y (tailles doivent correspondre).
	void SetXy(std::span<const float> xs, std::span<const float> ys) {
		x.assign(xs.begin(), xs.end());
		y.assign(ys.begin(), ys.end());
	}
	/// Remplace uniquement y — régénère un x implicite (indices 0..N-1) si
	/// la taille a changé, même sémantique que l'ancien `UiPlot::setValues()`.
	void SetY(std::span<const float> ys) {
		y.assign(ys.begin(), ys.end());
		if (x.size() != y.size()) {
			x.resize(y.size());
			for (size_t i = 0; i < x.size(); ++i)
				x[i] = float(i);
		}
	}
};

// ============================================================================
// Axes
// ============================================================================

/// Un axe (X, Y principal, ou Y secondaire — Phase 4). `min`/`max` sont la
/// SOURCE DE VÉRITÉ de la plage affichée : le pan/zoom (Phase 3) les mute
/// directement, aucune réécriture de UiRect/UiItem (cf. Décision #3 du plan).
/// Bord d'ancrage — pour un axe X seuls `Top`/`Bottom` ont un sens, pour un
/// axe Y seuls `Left`/`Right` (pas de validation forcée, cf. doc des champs
/// qui l'utilisent).
enum class PlotEdge : uint8_t { Left, Right, Top, Bottom };

struct PlotAxis {
	float min = 0.f, max = 1.f;
	bool autoFit = true; ///< recalculé depuis les séries visibles chaque frame si vrai
	bool showGrid = true;
	String label;
	/// Bord où dessiner les graduations — `Top`/`Bottom` pour un axe X,
	/// `Left`/`Right` pour un axe Y (`UiFactory::Plot()` initialise
	/// `yAxis.tickPosition = Left` ; `xAxis` garde le défaut `Bottom`).
	PlotEdge tickPosition = PlotEdge::Bottom;
	/// Si vrai, les graduations flottent PAR-DESSUS la zone de tracé (avec
	/// `tickBoxColor` en fond, pour la lisibilité) au lieu de réserver un
	/// espace dédié en permanence (comportement par défaut).
	bool tickOverlay = false;
	sdl3::FColor tickBoxColor = sdl3::FColor::UI_OVERLAY_DARK();
	/// Échelle logarithmique (base 10, Phase 3) — transform et génération de
	/// graduations adaptées (cf. `toAxisSpace()`/`generateLogGetTicks()`).
	/// Nécessite des données STRICTEMENT POSITIVES : les valeurs <= 0 sont
	/// ignorées par `computeAutoFit()` et bornées à un epsilon dans les
	/// transforms (jamais de log(0)/négatif).
	bool logScale = false;
};

// ============================================================================
// Légende
// ============================================================================

/// Position d'ancrage de la légende — 8 positions (4 bords + 4 coins), même
/// idée que les axes : combinable avec `legendOverlay` (superposée à la zone
/// de tracé, comportement par défaut d'ImPlot) ou un espace réservé en
/// permanence sur le bord correspondant (les positions de coin réservent sur
/// leur bord "principal" — Left pour TopLeft/BottomLeft, Right pour
/// TopRight/BottomRight — un coin n'a pas de bord dédié à lui seul).
enum class LegendPosition : uint8_t { Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

namespace detail {
[[nodiscard]] inline PlotEdge LegendMarginSide(LegendPosition pos) noexcept {
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

// ============================================================================
// Camembert (Phase 5, cf. Décision #5 — forme de données distincte, pas un
// simple x[]/y[]) — vit à côté de PlotSeries plutôt qu'imbriqué dans Plot,
// même raison que PlotSeries lui-même : pas d'entité par part, un plot peut
// en avoir des dizaines.
// ============================================================================

/// Une part de camembert — DONNÉE PURE dans `UiPlot::pieSlices`, actif
/// seulement si `UiPlot::mode == PlotMode::PIE`. `visible` permet le même
/// bascule-au-clic-de-légende que `PlotSeries::visible` (Phase 2) : une part
/// masquée est exclue du total ET recalculée hors des angles (cf.
/// `computePieAngles()`), pas juste rendue invisible à angle inchangé.
struct PieSlice {
	float value = 0.f; ///< négatif traité comme 0 (pas d'aire négative)
	String label;      ///< nom affiché dans la légende
	sdl3::FColor color = sdl3::FColor::WHITE(); ///< résolu depuis la palette par défaut si non précisé à l'ajout
	bool visible = true;
};

// ============================================================================
// Heatmap (Phase 6, cf. Décision #5 — grille 2D de valeurs, encore une forme
// de données distincte de x[]/y[]).
// ============================================================================

/// Dégradé de couleurs utilisé pour convertir une valeur normalisée [0,1] en
/// couleur — approximations à 3 points ancrés (interpolation RGB linéaire
/// par morceaux, pas une vraie carte perceptuellement uniforme : suffisant
/// pour une lecture visuelle, pas pour une reproduction scientifique exacte).
enum class Colormap : uint8_t {
	VIRIDIS,  ///< violet foncé -> teal -> jaune (défaut, standard de facto matplotlib)
	GRAYSCALE, ///< noir -> blanc
	COOL_WARM, ///< bleu -> blanc -> rouge (divergent, utile pour des données signées)
};

/// Grille 2D de valeurs — DONNÉE PURE dans `UiPlot::heatmap`, actif seulement
/// si `UiPlot::mode == PlotMode::HEATMAP`. `values` est en ordre LIGNE
/// PRINCIPALE (`values[r*cols+c]`), taille attendue `rows*cols` (sinon
/// `drawHeatmap()` ne dessine rien plutôt que de risquer un accès hors
/// bornes — cf. son garde en tête de fonction).
struct HeatmapData {
	int rows = 0, cols = 0;
	std::vector<float> values;
	Colormap colormap = Colormap::VIRIDIS;
	/// Si vrai (défaut), `minValue`/`maxValue` sont recalculés À CHAQUE FRAME
	/// depuis `values` (même esprit que `PlotAxis::autoFit` — jamais mis en
	/// cache, cf. `resolveHeatmapRange()`) ; sinon la plage manuelle ci-dessous
	/// est utilisée telle quelle (utile pour comparer plusieurs heatmaps sur
	/// une échelle de couleur commune).
	bool autoRange = true;
	float minValue = 0.f, maxValue = 1.f;
};

// ============================================================================
// Candlestick/OHLC (Phase 6, cf. Décision #5 — encore une forme de données
// distincte : 4 valeurs Y par point, ne rentre pas dans le moule x[]/y[]).
// ============================================================================

/// Une bougie — DONNÉE PURE dans `UiPlot::candleBars`, actif seulement si
/// `UiPlot::mode == PlotMode::CANDLE`. `x` partage l'échelle de `xAxis`
/// (typiquement un indice de temps/session), `open`/`close` déterminent la
/// couleur (hausse si `close >= open`, cf. `UiPlot::candleBullColor`/
/// `candleBearColor`), `high`/`low` la mèche.
struct OhlcBar {
	float x = 0.f;
	float open = 0.f, high = 0.f, low = 0.f, close = 0.f;
};

// ============================================================================
// Plot
// ============================================================================

/// Widget de plot — auto-dessiné (comme `UiTable`), pas d'entité par
/// série/point. `mode` distingue les familles de données : XY (séries
/// x[]/y[] partageant les mêmes axes), Pie (parts, Phase 5), Heatmap (grille
/// 2D, Phase 6), Candle (bougies OHLC, Phase 6).
enum class PlotMode : uint8_t { XY, PIE, HEATMAP, CANDLE };

struct UiPlot {
	PlotMode mode = PlotMode::XY;
	PlotAxis xAxis, yAxis;
	/// Axe Y secondaire (Phase 4) — bord par défaut `Right` (posé par
	/// `UiFactory::Plot()`, opposé au bord par défaut de `yAxis`). N'est
	/// dessiné/résolu que si au moins une série a `useSecondaryY=true` (cf.
	/// `plotHasSecondaryY()`) ; sinon simplement ignoré. Sans effet en
	/// `PlotMode::PIE` (pas d'axes).
	PlotAxis yAxis2;
	std::vector<PlotSeries> series; ///< actif seulement en `PlotMode::XY`
	std::vector<PieSlice> pieSlices; ///< actif seulement en `PlotMode::PIE`
	HeatmapData heatmap;             ///< actif seulement en `PlotMode::HEATMAP`
	std::vector<OhlcBar> candleBars; ///< actif seulement en `PlotMode::CANDLE`
	/// Largeur du corps de bougie en unités DONNÉES (même convention que
	/// `PlotSeries::barWidth`) — `PlotMode::CANDLE` uniquement.
	float candleWidth = 0.6f;
	sdl3::FColor candleBullColor{60/255.f, 180/255.f, 90/255.f, 255/255.f}; ///< hausse (close >= open)
	sdl3::FColor candleBearColor{220/255.f, 70/255.f, 70/255.f, 255/255.f}; ///< baisse (close < open)
	String title; ///< texte optionnel superposé en haut, centré (même rôle que l'ancien overlayText)

	bool showLegend = true;
	LegendPosition legendPosition = LegendPosition::TopRight;
	/// Superposée à la zone de tracé (défaut) ou espace réservé en
	/// permanence sur le bord correspondant à `legendPosition`.
	bool legendOverlay = true;
	sdl3::FColor legendBoxColor = sdl3::FColor::UI_OVERLAY_DARK();

	// État de survol (géré par InputSystem::dispatch, comme l'ancien Plot) —
	// le point le plus proche, TOUTES séries visibles confondues.
	bool hovered = false;
	int hoveredSeries = -1;
	int hoveredIndex = -1;
	/// Ligne de légende survolée (-1 = aucune) — met en surbrillance la
	/// série correspondante dans le tracé (cf. `dimFactor`, drawPlotSeries()).
	int legendHover = -1;

	// ── Navigation (Phase 3) : pan (glisser gauche), zoom-rectangle
	// (glisser droit), état stocké directement sur le composant — comme
	// `legendHover` ci-dessus, jamais côté InputSystem (Décision #4 du plan).
	// Pan et zoom-rectangle ne sont jamais actifs simultanément : ils
	// partagent les mêmes champs `dragStart*`/`dragCurrentMouse`.
	bool panning = false;
	bool boxZooming = false;
	sdl3::FPoint dragStartMouse{};
	/// Mis à jour en continu pendant panning/boxZooming : pan = position
	/// courante de la souris (pour le delta) ; box-zoom = second coin du
	/// rectangle (pour son rendu ET le calcul final au relâchement).
	sdl3::FPoint dragCurrentMouse{};
	/// Axes RÉSOLUS au moment où le geste a commencé (cf. `resolveAxis()`) —
	/// évite tout glissement si les données/l'auto-fit changent en cours de
	/// geste (un plot avec des données live pourrait sinon "sauter").
	PlotAxis dragStartXAxis, dragStartYAxis;
	/// Snapshot équivalent pour `yAxis2` (Phase 4) — toujours pris, même sans
	/// axe secondaire actif (cf. doc de `applyPlotPan()`).
	PlotAxis dragStartYAxis2;
};

/// Vrai si au moins une série se réfère à `yAxis2` — seule condition
/// d'activation de l'axe Y secondaire (Phase 4) : pas de flag dédié sur
/// `Plot` à garder synchronisé avec l'état des séries.
[[nodiscard]] inline bool PlotHasSecondaryY(const UiPlot &plot) noexcept {
    for (const PlotSeries &s : plot.series)
        if (s.useSecondaryY)
            return true;
    return false;
}

/// Nombre d'entrées de légende — `series` en `PlotMode::XY`, `pieSlices` en
/// `PlotMode::PIE` (Phase 5), toujours 0 en `PlotMode::HEATMAP`/`Candle`
/// (Phase 6 : pas de notion d'"entrée" pour une grille de couleur ou une
/// série de bougies) : les fonctions de mise en page de la légende (marges,
/// boîte) n'ont besoin QUE de ce compte, pas de savoir laquelle des
/// collections l'a produit.
[[nodiscard]] inline size_t PlotLegendItemCount(const UiPlot &plot) noexcept {
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

// ============================================================================
// Palette catégorielle par défaut (Décision #6 — autonome, pas d'ajout à UiTheme)
// ============================================================================

namespace detail {
/// 10 couleurs distinctes (palette "tab10", un standard de facto pour les
/// séries catégorielles — matplotlib/D3/Vega l'utilisent tous).
inline constexpr std::array<sdl3::FColor, 10> K_PLOT_PALETTE{{
	{31, 119, 180, 255}, {255, 127, 14, 255}, {44, 160, 44, 255}, {214, 39, 40, 255}, {148, 103, 189, 255},
	{140, 86, 75, 255}, {227, 119, 194, 255}, {127, 127, 127, 255}, {188, 189, 34, 255}, {23, 190, 207, 255},
}};
} // namespace detail

/// Couleur de palette pour la Nième série (round-robin) — utilisée par les
/// builders `addLineSeries()`/`addScatterSeries()` quand aucune couleur
/// explicite n'est fournie.
[[nodiscard]] inline sdl3::FColor PlotPaletteColor(size_t seriesIndex) noexcept {
	return detail::K_PLOT_PALETTE[seriesIndex % detail::K_PLOT_PALETTE.size()];
}

// ============================================================================
// Transform données <-> écran (Décision #3) — symétrie 1D de
// canvasToScreen/screenToCanvas (nodegraph.hpp), sans pan+zoom séparés :
// juste axis.min/max, mutés directement par les interactions (Phase 3).
// ============================================================================

/// Convertit une valeur d'axe vers/depuis son "espace de travail" — l'espace
/// linéaire (identité) ou logarithmique (log10, borné à un epsilon positif)
/// selon `axis.logScale`. `dataToScreen`/`screenToData` ET le pan/zoom
/// (Phase 3, plus bas) passent tous par ces deux fonctions : c'est la SEULE
/// chose à changer pour que l'échelle log se comporte correctement partout.
[[nodiscard]] inline float ToAxisSpace(const PlotAxis &axis, float value) noexcept {
	return axis.logScale ? std::log10(sdl3::Max(value, 1e-6f)) : value;
}
[[nodiscard]] inline float FromAxisSpace(const PlotAxis &axis, float value) noexcept {
	return axis.logScale ? std::pow(10.f, value) : value;
}

[[nodiscard]] inline float DataToScreen(const PlotAxis &axis, float screenMin, float screenMax,
										float value) noexcept {
	float lo = ToAxisSpace(axis, axis.min), hi = ToAxisSpace(axis, axis.max);
	float t = (ToAxisSpace(axis, value) - lo) / sdl3::Max(1e-6f, hi - lo);
	return screenMin + t * (screenMax - screenMin);
}

/// Clampe un span ÉCRAN pour la division `t = (v - screenMin) / span` en
/// évitant la division par ~0, SANS EN INVERSER LE SIGNE — `screenMax -
/// screenMin` est négatif pour un axe Y (screenMin = bas de l'écran =
/// axis.min, screenMax = haut = axis.max, cf. tous les appels de ce fichier).
/// `sdl3::Max(1e-6f, span)` seul (comme le clamp de `dataToScreen` sur le
/// span en ESPACE DONNÉES, toujours positif par construction) inverserait
/// silencieusement l'axe Y en un span minuscule mais POSITIF — bogué une
/// première fois ici, une deuxième fois dans `applyPlotAxisZoom` (même
/// erreur dupliquée) avant d'être unifié dans ce seul helper.
[[nodiscard]] inline float SafeScreenSpan(float span) noexcept {
    return sdl3::Abs(span) < 1e-6f ? (span >= 0.f ? 1e-6f : -1e-6f) : span;
}

[[nodiscard]] inline float ScreenToData(const PlotAxis &axis, float screenMin, float screenMax,
                                        float screenValue) noexcept {
    float lo = ToAxisSpace(axis, axis.min), hi = ToAxisSpace(axis, axis.max);
    float t = (screenValue - screenMin) / SafeScreenSpan(screenMax - screenMin);
    return FromAxisSpace(axis, lo + t * (hi - lo));
}

// ============================================================================
// Marges (graduations réservées + légende réservée) et rectangle de tracé —
// UNIQUE source de vérité, réutilisée par le survol (InputSystem::dispatch)
// ET le rendu (RenderSystem::drawWidget) pour qu'ils restent en accord.
// ============================================================================

inline constexpr float K_PLOT_TICK_MARGIN = 32.f;   ///< espace réservé par axe si tickOverlay=false
													  ///< (assez large pour un label négatif "-XX.X" au
													  ///< tickFs par défaut — 22px clipait le signe "-"
													  ///< des labels négatifs, cf. l'axe Y d'une forme
													  ///< d'onde en [-1,1])
inline constexpr float K_PLOT_LEGEND_WIDTH = 110.f; ///< largeur de la boîte de légende
inline constexpr float K_PLOT_LEGEND_ROW_H = 16.f;   ///< hauteur d'une ligne de légende

struct PlotMargins {
	float left = 0.f, right = 0.f, top = 0.f, bottom = 0.f;
};

/// Marges réservées par les graduations (si `!tickOverlay`) et la légende
/// (si `showLegend && !legendOverlay`) — AVANT tout calcul de rectangle,
/// pour éviter toute circularité (la position finale de la légende dépend du
/// rectangle de tracé, qui dépend lui-même de cette marge).
[[nodiscard]] inline PlotMargins ComputePlotMargins(const UiPlot &plot) noexcept {
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

/// Rectangle de TRACÉ (données) à partir de la boîte écran du widget —
/// applique `computePlotMargins()` puis un petit inset fixe de base.
[[nodiscard]] inline sdl3::FRect ComputePlotRect(const UiPlot &plot, const sdl3::FRect &widgetRect) noexcept {
	PlotMargins m = ComputePlotMargins(plot);
	constexpr float K_BASE_PAD = 4.f;
	float x = widgetRect.x + K_BASE_PAD + m.left;
	float y = widgetRect.y + K_BASE_PAD + m.top;
	float w = sdl3::Max(0.f, widgetRect.w - 2.f * K_BASE_PAD - m.left - m.right);
	float h = sdl3::Max(0.f, widgetRect.h - 2.f * K_BASE_PAD - m.top - m.bottom);
	return {x, y, w, h};
}

/// Position/taille finale d'une zone ancrée (légende ou bande de
/// graduations superposée) à l'une des 8 points cardinaux/coins de `area`.
[[nodiscard]] inline sdl3::FRect AnchorRectIn(LegendPosition pos, const sdl3::FRect &area, float w, float h,
										float pad) noexcept {
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

/// Bande réservée à la légende (mode NON superposé) — ce qu'il reste de
/// `widgetRect` une fois `plotRect` retiré, du côté déterminé par
/// `legendMarginSide(pos)`.
[[nodiscard]] inline sdl3::FRect LegendReservedStrip(LegendPosition pos, const sdl3::FRect &widgetRect,
											   const sdl3::FRect &plotRect) noexcept {
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

/// Boîte de légende complète (position + taille), superposée à `plotRect`
/// ou dans sa bande réservée de `widgetRect`, selon `plot.legendOverlay`.
[[nodiscard]] inline sdl3::FRect LegendBoxRect(const UiPlot &plot, const sdl3::FRect &widgetRect, const sdl3::FRect &plotRect) noexcept {
	float w = K_PLOT_LEGEND_WIDTH;
	float h = float(PlotLegendItemCount(plot)) * K_PLOT_LEGEND_ROW_H + 6.f;
	sdl3::FRect area = plot.legendOverlay ? plotRect : LegendReservedStrip(plot.legendPosition, widgetRect, plotRect);
	return AnchorRectIn(plot.legendPosition, area, w, h, 4.f);
}

/// Rectangle-ligne de la Nième entrée de légende, à l'intérieur de `legendBox`.
[[nodiscard]] inline sdl3::FRect LegendRowRect(const sdl3::FRect &legendBox, size_t index) noexcept {
	return {legendBox.x, legendBox.y + 3.f + float(index) * K_PLOT_LEGEND_ROW_H, legendBox.w, K_PLOT_LEGEND_ROW_H};
}

/// Sous-rectangle de la pastille de couleur d'une ligne de légende.
[[nodiscard]] inline sdl3::FRect LegendSwatchRect(const sdl3::FRect &row) noexcept {
	return {row.x + 4.f, row.y + (row.h - 10.f) * 0.5f, 10.f, 10.f};
}

/// Sous-rectangle du LABEL TEXTE d'une ligne de légende (à droite de la
/// pastille) — la LARGEUR réelle du texte n'est pas connue ici (mesure de
/// texte indisponible dans ce fichier, cf. en-tête) ; ce rectangle borne
/// juste la zone où `RenderSystem::drawWidget` doit le dessiner.
[[nodiscard]] inline sdl3::FRect LegendLabelRect(const sdl3::FRect &row) noexcept {
	return {row.x + 18.f, row.y, sdl3::Max(0.f, row.w - 20.f), row.h};
}

/// Nombre de décimales "raisonnable" pour un pas de graduation donné (0 si
/// le pas est >= 1, sinon assez de décimales pour distinguer deux
/// graduations consécutives).
[[nodiscard]] inline int TickDecimals(float step) noexcept {
	if (!(step > 0.f) || !std::isfinite(step))
		return 2;
	if (step >= 0.999f)
		return 0;
	return sdl3::Clamp(int(std::ceil(-std::log10(double(step)))), 0, 4);
}

/// Recalcule min/max d'un axe depuis les séries VISIBLES (ignore les
/// invisibles, cf. Phase 2) — `forX` sélectionne x[] ou y[]. Ajoute une
/// marge de 5% de chaque côté pour qu'aucun point ne tombe exactement sur le
/// bord du rectangle de tracé. Retourne {0,1} si aucune donnée (évite une
/// plage nulle/NaN).
/// `logScale` : ignore les valeurs <= 0 (échelle log, cf. doc
/// `PlotAxis::logScale`) et calcule la marge de 5% dans l'ESPACE LOG (une
/// marge additive linéaire n'aurait aucun sens sur une échelle log).
/// `forSecondaryY` (Phase 4) : IGNORÉ quand `forX=true` (l'axe X est partagé
/// par toutes les séries, quel que soit leur axe Y) ; quand `forX=false`,
/// filtre les séries dont `useSecondaryY` correspond — permet d'ajuster
/// `yAxis`/`yAxis2` indépendamment l'un de l'autre.
[[nodiscard]] inline std::pair<float, float> ComputeAutoFit(const std::vector<PlotSeries> &series, bool forX,
															 bool logScale = false,
															 bool forSecondaryY = false) noexcept {
	bool any = false;
	float mn = 0.f, mx = 0.f;
	for (const PlotSeries &s : series) {
		if (!s.visible)
			continue;
		if (!forX && s.useSecondaryY != forSecondaryY)
			continue;
		const std::vector<float> &vals = forX ? s.x : s.y;
		for (float v : vals) {
			if (logScale && v <= 0.f)
				continue;
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

/// Résout un axe pour LA frame courante : si `autoFit`, recalcule min/max
/// depuis les séries visibles (fresh, jamais mis en cache dans le
/// composant — mêmes valeurs recalculées indépendamment par le survol
/// (InputSystem::dispatch) et le rendu (RenderSystem::drawWidget), pas de
/// risque de désynchronisation d'une frame à l'autre) ; sinon renvoie une
/// copie telle quelle. Le pan/zoom (Phase 3) bascule `autoFit=false` au
/// premier geste, comme la plupart des bibliothèques de graphes — sans quoi
/// le recalcul auto-fit de LA frame suivante écraserait le pan/zoom manuel.
[[nodiscard]] inline PlotAxis ResolveAxis(const PlotAxis &axis, const std::vector<PlotSeries> &series, bool forX,
										  bool forSecondaryY = false) noexcept {
	PlotAxis resolved = axis;
	if (axis.autoFit) {
		auto [mn, mx] = ComputeAutoFit(series, forX, axis.logScale, forSecondaryY);
		resolved.min = mn;
		resolved.max = mx;
	}
	return resolved;
}

/// Graduations pour un axe LOGARITHMIQUE — le sous-ensemble de {1,2,5}×10^n
/// tombant dans [min,max], toutes les décades couvertes (convention standard
/// des bibliothèques de graphes — un espacement linéaire naïf n'aurait aucun
/// sens en log). `min` doit être strictement positif (sinon liste vide, cf.
/// doc `PlotAxis::logScale`).
[[nodiscard]] inline std::vector<float> GenerateLogTicks(float min, float max) {
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

/// Graduations "rondes" pour un axe (pas d'implémentation naïve à espacement
/// fixe — arrondit le pas à 1/2/5 x une puissance de 10, comme la plupart
/// des bibliothèques de graphes) — `targetCount` est indicatif (le nombre
/// réel dépend de où tombent les multiples du pas dans [min,max]).
[[nodiscard]] inline std::vector<float> GenerateTicks(float min, float max, int targetCount = 5) {
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

// ============================================================================
// Rendu géométrique (pas de texte — cf. en-tête du fichier) : appelé depuis
// RenderSystem::drawWidget (systems.hpp), qui dessine les labels/légende
// lui-même via ses propres méthodes de texte.
// ============================================================================

/// Grille de fond (lignes verticales aux graduations X, horizontales aux
/// graduations Y) — n'affiche rien si les graduations sont trop rapprochées
/// à l'écran (même garde que NodeGraphSystem::RenderGrid).
inline void DrawPlotGrid(IUiRenderBackend &ren, const sdl3::FRect &plotRect, const PlotAxis &xAxis, const PlotAxis &yAxis,
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

/// Fond + pastilles de couleur de la légende (PAS le texte — cf. en-tête du
/// fichier, à la charge de RenderSystem::drawWidget) : boîte translucide
/// (`plot.legendBoxColor`), bande de surbrillance sur la ligne survolée,
/// pastille pleine par entrée (atténuée si masquée, pour montrer visuellement
/// que l'entrée est masquée sans la faire disparaître de la liste). Itère
/// `series` en `PlotMode::XY`, `pieSlices` en `PlotMode::PIE` (Phase 5) — cf.
/// `plotLegendItemCount()`, même compte utilisé pour la mise en page.
inline void DrawPlotLegendBackground(IUiRenderBackend &ren, const UiPlot &plot, const sdl3::FRect &legendBox) {
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
/// Ajoute un quad (ruban épais) pour un segment `a`-`b` — même technique que
/// nodegraph.hpp::appendThickSegment (aucune primitive "ligne épaisse"
/// native, cf. Décision #7 du plan).
inline void AppendThickSegment(std::vector<sdl3::Vertex> &verts, sdl3::FPoint a, sdl3::FPoint b, float halfW, sdl3::FColor color) {
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

/// Ajoute un quad plein (2 triangles) entre deux points de courbe `a`-`b` et
/// leurs projetés respectifs sur la ligne de base `baseA`-`baseB` — remplage
/// « sous la courbe » (Area) segment par segment, plutôt qu'un unique
/// polygone en éventail (`fillPolygon`/`renderGeometryFromPoints`) qui
/// produirait des artefacts sur une courbe non convexe (zigzag).
inline void AppendFillQuad(std::vector<sdl3::Vertex> &verts, sdl3::FPoint a, sdl3::FPoint b, sdl3::FPoint baseA, sdl3::FPoint baseB,
						   sdl3::FColor color) {
	sdl3::Vertex va{{a.x, a.y}, color, {0.f, 0.f}};
	sdl3::Vertex vb{{b.x, b.y}, color, {0.f, 0.f}};
	sdl3::Vertex vBaseA{{baseA.x, baseA.y}, color, {0.f, 0.f}};
	sdl3::Vertex vBaseB{{baseB.x, baseB.y}, color, {0.f, 0.f}};
	verts.insert(verts.end(), {va, vb, vBaseB, va, vBaseB, vBaseA});
}

inline void DrawMarker(IUiRenderBackend &ren, sdl3::FPoint center, PlotMarker marker, float size) {
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

/// Polyligne épaisse (segments consécutifs de `pts`) — factorisé car Line ET
/// Step le réutilisent (Step ne diffère que par la construction des points,
/// cf. stepPoints(), pas par le tracé lui-même).
inline void DrawThickPolyline(IUiRenderBackend &ren, std::span<const sdl3::FPoint> pts, float lineWidth, sdl3::FColor color) {
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

/// Points en « escalier » à partir des points de courbe — convention
/// « post » : la valeur de gauche est maintenue jusqu'au x suivant, puis
/// saute (comme un signal numérique échantillonné-bloqué).
[[nodiscard]] inline std::vector<sdl3::FPoint> StepPoints(std::span<const sdl3::FPoint> pts) {
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

/// Points écran d'une série (utilisé pour le tracé ET par le hit-test de
/// survol) — un point par échantillon `(x[i], y[i])`, dans l'ordre.
[[nodiscard]] inline std::vector<sdl3::FPoint> PlotSeriesScreenPoints(const PlotSeries &s, const sdl3::FRect &plotRect,
																const PlotAxis &xAxis, const PlotAxis &yAxis) {
	std::vector<sdl3::FPoint> pts;
	size_t n = sdl3::Min(s.x.size(), s.y.size());
	pts.reserve(n);
	for (size_t i = 0; i < n; ++i)
		pts.push_back({DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, s.x[i]),
					   DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, s.y[i])});
	return pts;
}

/// Dessine une série, selon `s.kind` — respecte `onCustomDraw` s'il est posé
/// (remplace ALORS entièrement le rendu par défaut, `dimFactor` ignoré dans
/// ce cas — c'est à l'appelant de le gérer s'il le souhaite). `dimFactor`
/// (dans [0,1]) atténue l'opacité de TOUT ce qui est dessiné — utilisé pour
/// estomper les séries non survolées quand une entrée de légende a le focus
/// (cf. Phase 2, `UiPlot::legendHover`), 1.0 = opacité normale.
inline void DrawPlotSeries(IUiRenderBackend &ren, const sdl3::FRect &plotRect, const PlotSeries &s, const PlotAxis &xAxis,
						   const PlotAxis &yAxis, float dimFactor = 1.f) {
	if (!s.visible)
		return;
	if (s.onCustomDraw) {
		if (auto *nr = ren.NativeRenderer())
			s.onCustomDraw(*nr, plotRect, s, xAxis, yAxis);
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

/// Point le plus proche de `mouseScreen` parmi toutes les séries VISIBLES,
/// dans un rayon de `maxDist` pixels — utilisé par le survol
/// (InputSystem::dispatch, systems.hpp). `xAxis`/`yAxis`/`yAxis2` doivent
/// être des axes déjà RÉSOLUS (cf. resolveAxis()) — pas `plot.xAxis`/
/// `plot.yAxis`/`plot.yAxis2` bruts, qui peuvent être en mode auto-fit
/// (min/max non à jour). Chaque série choisit `yAxis` ou `yAxis2` selon
/// `PlotSeries::useSecondaryY` (Phase 4). Retourne {-1,-1} si rien dans le
/// rayon.
[[nodiscard]] inline std::pair<int, int> PlotNearestPoint(const UiPlot &plot, const sdl3::FRect &plotRect,
														  const PlotAxis &xAxis, const PlotAxis &yAxis,
														  const PlotAxis &yAxis2, sdl3::FPoint mouseScreen,
														  float maxDist = 12.f) {
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

// ============================================================================
// Camembert (Phase 5) — géométrie/rendu, pas de texte (cf. en-tête du
// fichier) : RenderSystem::drawWidget dessine les labels/pourcentages.
// ============================================================================

/// Angles [startDeg,endDeg) d'une part, dans la même convention que
/// `sdl3::Renderer::DrawPie`/`generateArcPoints` (0°=droite, sens horaire).
struct PieSliceAngles {
	float startDeg = 0.f, endDeg = 0.f;
};

/// Calcule les angles de chaque part VISIBLE, en partant de -90° (haut, cf.
/// convention horaire ci-dessus) — recalculé À CHAQUE FRAME depuis les
/// valeurs courantes (même esprit que `resolveAxis()` : jamais mis en cache
/// dans le composant, pas de risque de désynchronisation survol/rendu).
/// Parts invisibles (bascule de légende) exclues du total ET de la sortie
/// (angle {0,0} — donc `endDeg <= startDeg`, cf. `drawPieChart`/`pieHitTest`
/// qui sautent ce cas). Valeurs négatives traitées comme 0 (pas d'aire
/// négative).
[[nodiscard]] inline std::vector<PieSliceAngles> ComputePieAngles(const std::vector<PieSlice> &slices) {
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

/// Rectangle carré inscrit maximal, centré dans `plotRect` — le cercle du
/// camembert y est inscrit (même esprit que `computePlotRect()` pour XY :
/// une seule source de vérité pour le survol ET le rendu).
[[nodiscard]] inline sdl3::FRect ComputePieCircleRect(const sdl3::FRect &plotRect) noexcept {
	float d = sdl3::Min(plotRect.w, plotRect.h);
	float cx = plotRect.x + plotRect.w * 0.5f, cy = plotRect.y + plotRect.h * 0.5f;
	return {cx - d * 0.5f, cy - d * 0.5f, d, d};
}

/// Index de la part sous `mouseScreen` (distance <= rayon ET angle dans
/// [start,end) d'une part visible), -1 si hors cercle ou aucune part avec une
/// valeur strictement positive à cet angle.
[[nodiscard]] inline int PieHitTest(const std::vector<PieSlice> &slices, const std::vector<PieSliceAngles> &angles,
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
/// Éventail de triangles EXACT depuis `center` (pas l'approximation par
/// centroïde de `fillPolygon`/`renderGeometryFromPoints` — correcte ici
/// seulement parce qu'une part de camembert est un vrai éventail géométrique
/// depuis son centre, contrairement à l'aire sous une courbe zigzag qui a
/// motivé `appendFillQuad` plus haut).
inline void AppendPieSliceFan(std::vector<sdl3::Vertex> &verts, sdl3::FPoint center, std::span<const sdl3::FPoint> arcPoints,
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

/// Dessine le camembert (parts pleines PUIS contours dans une passe séparée,
/// pour qu'aucun contour ne soit recouvert par le remplissage d'une part
/// voisine) — `hoveredSlice` (survol direct OU `UiPlot::legendHover`, à
/// l'appelant de choisir lequel prime) atténue les autres parts, même esprit
/// que `dimFactor` de `drawPlotSeries()`.
inline void DrawPieChart(IUiRenderBackend &ren, const std::vector<PieSlice> &slices,
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

// ============================================================================
// Heatmap (Phase 6) — géométrie/rendu, pas de texte (cf. en-tête du fichier).
// ============================================================================

/// Convertit une valeur normalisée `t` dans [0,1] en couleur selon `cm` —
/// interpolation RGB linéaire par morceaux entre 2-3 couleurs ancrées (cf.
/// doc de `Colormap`).
[[nodiscard]] inline sdl3::FColor HeatmapColor(float t, Colormap cm) noexcept {
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

/// Résout la plage [min,max] de normalisation — recalculée À CHAQUE FRAME
/// depuis `values` si `autoRange` (même esprit que `resolveAxis()`, jamais
/// mise en cache), sinon `minValue`/`maxValue` tels quels.
[[nodiscard]] inline std::pair<float, float> ResolveHeatmapRange(const HeatmapData &hm) noexcept {
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

/// Dessine la grille colorée — ne fait RIEN si `values` n'a pas au moins
/// `rows*cols` éléments (plutôt que de risquer un accès hors bornes) ; un
/// léger surplomb de 0.5px entre cellules absorbe l'arrondi pixel (évite un
/// liseré de fond visible entre deux cellules adjacentes).
inline void DrawHeatmap(IUiRenderBackend &ren, const sdl3::FRect &plotRect, const HeatmapData &hm) {
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

/// Index de cellule (`r*cols+c`) sous `mouseScreen`, -1 si hors grille.
[[nodiscard]] inline int HeatmapHitTest(const HeatmapData &hm, const sdl3::FRect &plotRect, sdl3::FPoint mouseScreen) noexcept {
	if (hm.rows <= 0 || hm.cols <= 0 || !plotRect.Contains(mouseScreen))
		return -1;
	int c = int((mouseScreen.x - plotRect.x) / (plotRect.w / float(hm.cols)));
	int r = int((mouseScreen.y - plotRect.y) / (plotRect.h / float(hm.rows)));
	if (r < 0 || r >= hm.rows || c < 0 || c >= hm.cols)
		return -1;
	return r * hm.cols + c;
}

// ============================================================================
// Candlestick/OHLC (Phase 6) — géométrie/rendu, pas de texte. Contrairement à
// Pie/Heatmap, un chandelier a un vrai axe X (temps) et Y (prix) continus —
// réutilise `xAxis`/`yAxis` de `Plot` pour leurs réglages d'affichage
// (position/style des graduations), mais `min`/`max` sont TOUJOURS résolus
// depuis `candleBars` (cf. `resolveCandleAxis()`, jamais depuis
// `PlotAxis::autoFit`/navigation — pas de pan/zoom en `PlotMode::CANDLE`,
// cf. Décision de portée documentée dans systems.hpp).
// ============================================================================

/// Min/max le long d'un axe depuis les bougies — `forX` utilise `bar.x`,
/// sinon `bar.low`/`bar.high` (la mèche complète, pas juste corps
/// ouverture/fermeture). Marge de 8% de chaque côté (même esprit que
/// `computeAutoFit()`, proportion différente : les bougies ont une largeur
/// visible qui a besoin d'un peu plus d'air en X qu'un simple point XY).
[[nodiscard]] inline std::pair<float, float> ComputeCandleAutoFit(const std::vector<OhlcBar> &bars,
																   bool forX) noexcept {
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

/// Comme `resolveAxis()`, mais TOUJOURS résolu depuis `bars` (pas de
/// `PlotAxis::autoFit` à consulter — un chandelier n'a pas de navigation,
/// cf. en-tête de section) ; `min`/`max` de `axis` sont donc ignorés, seuls
/// ses réglages d'affichage (`tickPosition`/`tickOverlay`/`showGrid`/...)
/// sont conservés.
[[nodiscard]] inline PlotAxis ResolveCandleAxis(const PlotAxis &axis, const std::vector<OhlcBar> &bars,
												bool forX) noexcept {
	PlotAxis resolved = axis;
	auto [mn, mx] = ComputeCandleAutoFit(bars, forX);
	resolved.min = mn;
	resolved.max = mx;
	return resolved;
}

/// Rectangle écran du CORPS d'une bougie (entre `open` et `close`).
[[nodiscard]] inline sdl3::FRect ComputeCandleBodyRect(const sdl3::FRect &plotRect, const PlotAxis &xAxis, const PlotAxis &yAxis,
												 const OhlcBar &bar, float candleWidth) noexcept {
	float halfW = candleWidth * 0.5f;
	float x0 = DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, bar.x - halfW);
	float x1 = DataToScreen(xAxis, plotRect.x, plotRect.x + plotRect.w, bar.x + halfW);
	float yOpen = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, bar.open);
	float yClose = DataToScreen(yAxis, plotRect.y + plotRect.h, plotRect.y, bar.close);
	float top = sdl3::Min(yOpen, yClose), bottom = sdl3::Max(yOpen, yClose);
	return {sdl3::Min(x0, x1), top, sdl3::Max(1.f, sdl3::Abs(x1 - x0)), sdl3::Max(1.f, bottom - top)};
}

/// Dessine toutes les bougies (mèche + corps) — couleur hausse/baisse selon
/// `close` vs `open` (cf. doc de `OhlcBar`). `dimFactor` estompe une bougie
/// donnée (survol, même esprit que `drawPlotSeries()`), -1 = aucune.
inline void DrawCandleChart(IUiRenderBackend &ren, const sdl3::FRect &plotRect, const std::vector<OhlcBar> &bars,
							const PlotAxis &xAxis, const PlotAxis &yAxis, float candleWidth, sdl3::FColor bullColor,
							sdl3::FColor bearColor, int hoveredBar = -1) {
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

/// Bougie la plus proche de `mouseScreen` EN X (une bougie couvre toute sa
/// hauteur de mèche, donc le survol n'a de sens qu'en distance horizontale,
/// contrairement à `plotNearestPoint()` qui teste des points ponctuels) —
/// -1 si aucune bougie dans `maxDist` pixels.
[[nodiscard]] inline int CandleNearestBar(const std::vector<OhlcBar> &bars, const sdl3::FRect &plotRect,
										  const PlotAxis &xAxis, sdl3::FPoint mouseScreen, float maxDist = 10.f) noexcept {
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

// ============================================================================
// Navigation (Phase 3) : pan, zoom molette, zoom-rectangle, retour à
// l'ajustement automatique — fonctions libres appelées depuis
// InputSystem::dispatch (systems.hpp), état stocké sur Plot lui-même (cf.
// Décision #4). Toutes mutent directement PlotAxis::min/max (Décision #3),
// jamais UiRect/UiItem — aucun markDirty nécessaire.
// ============================================================================

/// Décale `plot.xAxis`/`plot.yAxis`/`plot.yAxis2` pour que le point de
/// données sous `startMouse` (résolu via les axes SNAPSHOT `startXAxis`/
/// `startYAxis`/`startYAxis2`, pris au début du geste) se retrouve sous
/// `currentMouse` — fonctionne aussi bien en échelle linéaire que
/// logarithmique (tout passe par `toAxisSpace()`). `startYAxis2` est
/// toujours appliqué à `plot.yAxis2`, même si aucune série ne l'utilise
/// (Phase 4) : mutation inconditionnelle mais sans effet visible dans ce
/// cas (axe jamais dessiné/résolu ailleurs), plus simple qu'un paramètre
/// optionnel. Bascule `autoFit=false` sur les trois axes (sans quoi le
/// recalcul auto-fit de la frame suivante écraserait le pan).
inline void ApplyPlotPan(UiPlot &plot, const sdl3::FRect &plotRect, sdl3::FPoint startMouse, sdl3::FPoint currentMouse,
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

/// Zoom molette centré sur `mouseScreen` : garde la valeur de données sous le
/// curseur FIXE (pivot), réduit/agrandit la plage autour de ce pivot.
/// `wheelY` positif = zoom avant (réduit la plage). `screenMin`/`screenMax` :
/// bornes écran de CET axe, mêmes conventions que `dataToScreen` (inversées
/// pour Y — `screenMin` = bas = `axis.min`, `screenMax` = haut = `axis.max`).
/// Résout l'axe (`resolveAxis`) avant de zoomer pour partir d'une plage à
/// jour même si l'axe est encore en auto-fit (premier geste de l'utilisateur).
/// `forSecondaryY` (Phase 4) : transmis tel quel à `resolveAxis()` — sans
/// effet quand `forX=true` (X est partagé, jamais primaire/secondaire).
inline void ApplyPlotAxisZoom(PlotAxis &axis, const std::vector<PlotSeries> &series, bool forX, float screenMin,
							  float screenMax, float mouseScreen, float wheelY,
							  bool forSecondaryY = false) noexcept {
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

/// Calcule et applique le résultat d'un zoom-rectangle (clic-droit glissé) :
/// les deux coins écran sont convertis en données via les axes SNAPSHOT
/// `startXAxis`/`startYAxis`/`startYAxis2` (pris au début du geste), puis
/// min/max sont TRIÉS — gère les deux sens de glisser ET l'inversion
/// écran/données de l'axe Y sans cas particulier. `startYAxis2` toujours
/// appliqué à `plot.yAxis2` (même remarque que `applyPlotPan`, Phase 4).
inline void ApplyPlotBoxZoom(UiPlot &plot, const sdl3::FRect &plotRect, sdl3::FPoint corner0, sdl3::FPoint corner1,
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

/// Retour à l'ajustement automatique (double-clic) — repasse simplement les
/// trois axes en `autoFit=true`, `resolveAxis()` recalcule leur plage depuis
/// les séries visibles dès la frame suivante (aucune valeur à recalculer ici).
inline void ResetPlotToAutoFit(UiPlot &plot) noexcept {
	plot.xAxis.autoFit = true;
	plot.yAxis.autoFit = true;
	plot.yAxis2.autoFit = true;
}

/// Rectangle de sélection du zoom-rectangle, pendant le geste uniquement
/// (`plot.boxZooming`) — fond translucide + contour, coins normalisés (pas
/// d'hypothèse sur le sens de glisser).
inline void DrawPlotBoxZoomRect(IUiRenderBackend &ren, const UiPlot &plot) {
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
