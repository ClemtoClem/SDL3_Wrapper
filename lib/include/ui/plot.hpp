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
	void SetXy(std::span<const float> xs, std::span<const float> ys);
	/// Remplace uniquement y — régénère un x implicite (indices 0..N-1) si
	/// la taille a changé, même sémantique que l'ancien `UiPlot::setValues()`.
	void SetY(std::span<const float> ys);
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
[[nodiscard]] PlotEdge LegendMarginSide(LegendPosition pos) noexcept;
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
[[nodiscard]] bool PlotHasSecondaryY(const UiPlot &plot) noexcept;

/// Nombre d'entrées de légende — `series` en `PlotMode::XY`, `pieSlices` en
/// `PlotMode::PIE` (Phase 5), toujours 0 en `PlotMode::HEATMAP`/`Candle`
/// (Phase 6 : pas de notion d'"entrée" pour une grille de couleur ou une
/// série de bougies) : les fonctions de mise en page de la légende (marges,
/// boîte) n'ont besoin QUE de ce compte, pas de savoir laquelle des
/// collections l'a produit.
[[nodiscard]] size_t PlotLegendItemCount(const UiPlot &plot) noexcept;

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
[[nodiscard]] sdl3::FColor PlotPaletteColor(size_t seriesIndex) noexcept;

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
[[nodiscard]] float ToAxisSpace(const PlotAxis &axis, float value) noexcept;
[[nodiscard]] float FromAxisSpace(const PlotAxis &axis, float value) noexcept;

[[nodiscard]] float DataToScreen(const PlotAxis &axis, float screenMin, float screenMax,
										float value) noexcept;

/// Clampe un span ÉCRAN pour la division `t = (v - screenMin) / span` en
/// évitant la division par ~0, SANS EN INVERSER LE SIGNE — `screenMax -
/// screenMin` est négatif pour un axe Y (screenMin = bas de l'écran =
/// axis.min, screenMax = haut = axis.max, cf. tous les appels de ce fichier).
/// `sdl3::Max(1e-6f, span)` seul (comme le clamp de `dataToScreen` sur le
/// span en ESPACE DONNÉES, toujours positif par construction) inverserait
/// silencieusement l'axe Y en un span minuscule mais POSITIF — bogué une
/// première fois ici, une deuxième fois dans `applyPlotAxisZoom` (même
/// erreur dupliquée) avant d'être unifié dans ce seul helper.
[[nodiscard]] float SafeScreenSpan(float span) noexcept;

[[nodiscard]] float ScreenToData(const PlotAxis &axis, float screenMin, float screenMax,
                                        float screenValue) noexcept;

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
[[nodiscard]] PlotMargins ComputePlotMargins(const UiPlot &plot) noexcept;

/// Rectangle de TRACÉ (données) à partir de la boîte écran du widget —
/// applique `computePlotMargins()` puis un petit inset fixe de base.
[[nodiscard]] sdl3::FRect ComputePlotRect(const UiPlot &plot, const sdl3::FRect &widgetRect) noexcept;

/// Position/taille finale d'une zone ancrée (légende ou bande de
/// graduations superposée) à l'une des 8 points cardinaux/coins de `area`.
[[nodiscard]] sdl3::FRect AnchorRectIn(LegendPosition pos, const sdl3::FRect &area, float w, float h,
										float pad) noexcept;

/// Bande réservée à la légende (mode NON superposé) — ce qu'il reste de
/// `widgetRect` une fois `plotRect` retiré, du côté déterminé par
/// `legendMarginSide(pos)`.
[[nodiscard]] sdl3::FRect LegendReservedStrip(LegendPosition pos, const sdl3::FRect &widgetRect,
											   const sdl3::FRect &plotRect) noexcept;

/// Boîte de légende complète (position + taille), superposée à `plotRect`
/// ou dans sa bande réservée de `widgetRect`, selon `plot.legendOverlay`.
[[nodiscard]] sdl3::FRect LegendBoxRect(const UiPlot &plot, const sdl3::FRect &widgetRect, const sdl3::FRect &plotRect) noexcept;

/// Rectangle-ligne de la Nième entrée de légende, à l'intérieur de `legendBox`.
[[nodiscard]] sdl3::FRect LegendRowRect(const sdl3::FRect &legendBox, size_t index) noexcept;

/// Sous-rectangle de la pastille de couleur d'une ligne de légende.
[[nodiscard]] sdl3::FRect LegendSwatchRect(const sdl3::FRect &row) noexcept;

/// Sous-rectangle du LABEL TEXTE d'une ligne de légende (à droite de la
/// pastille) — la LARGEUR réelle du texte n'est pas connue ici (mesure de
/// texte indisponible dans ce fichier, cf. en-tête) ; ce rectangle borne
/// juste la zone où `RenderSystem::drawWidget` doit le dessiner.
[[nodiscard]] sdl3::FRect LegendLabelRect(const sdl3::FRect &row) noexcept;

/// Nombre de décimales "raisonnable" pour un pas de graduation donné (0 si
/// le pas est >= 1, sinon assez de décimales pour distinguer deux
/// graduations consécutives).
[[nodiscard]] int TickDecimals(float step) noexcept;

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
[[nodiscard]] std::pair<float, float> ComputeAutoFit(const std::vector<PlotSeries> &series, bool forX,
															 bool logScale = false,
															 bool forSecondaryY = false) noexcept;

/// Résout un axe pour LA frame courante : si `autoFit`, recalcule min/max
/// depuis les séries visibles (fresh, jamais mis en cache dans le
/// composant — mêmes valeurs recalculées indépendamment par le survol
/// (InputSystem::dispatch) et le rendu (RenderSystem::drawWidget), pas de
/// risque de désynchronisation d'une frame à l'autre) ; sinon renvoie une
/// copie telle quelle. Le pan/zoom (Phase 3) bascule `autoFit=false` au
/// premier geste, comme la plupart des bibliothèques de graphes — sans quoi
/// le recalcul auto-fit de LA frame suivante écraserait le pan/zoom manuel.
[[nodiscard]] PlotAxis ResolveAxis(const PlotAxis &axis, const std::vector<PlotSeries> &series, bool forX,
										  bool forSecondaryY = false) noexcept;

/// Graduations pour un axe LOGARITHMIQUE — le sous-ensemble de {1,2,5}×10^n
/// tombant dans [min,max], toutes les décades couvertes (convention standard
/// des bibliothèques de graphes — un espacement linéaire naïf n'aurait aucun
/// sens en log). `min` doit être strictement positif (sinon liste vide, cf.
/// doc `PlotAxis::logScale`).
[[nodiscard]] std::vector<float> GenerateLogTicks(float min, float max);

/// Graduations "rondes" pour un axe (pas d'implémentation naïve à espacement
/// fixe — arrondit le pas à 1/2/5 x une puissance de 10, comme la plupart
/// des bibliothèques de graphes) — `targetCount` est indicatif (le nombre
/// réel dépend de où tombent les multiples du pas dans [min,max]).
[[nodiscard]] std::vector<float> GenerateTicks(float min, float max, int targetCount = 5);

// ============================================================================
// Rendu géométrique (pas de texte — cf. en-tête du fichier) : appelé depuis
// RenderSystem::drawWidget (systems.hpp), qui dessine les labels/légende
// lui-même via ses propres méthodes de texte.
// ============================================================================

/// Grille de fond (lignes verticales aux graduations X, horizontales aux
/// graduations Y) — n'affiche rien si les graduations sont trop rapprochées
/// à l'écran (même garde que NodeGraphSystem::RenderGrid).
void DrawPlotGrid(IUiRenderBackend &ren, const sdl3::FRect &plotRect, const PlotAxis &xAxis, const PlotAxis &yAxis,
						 sdl3::FColor gridColor);

/// Fond + pastilles de couleur de la légende (PAS le texte — cf. en-tête du
/// fichier, à la charge de RenderSystem::drawWidget) : boîte translucide
/// (`plot.legendBoxColor`), bande de surbrillance sur la ligne survolée,
/// pastille pleine par entrée (atténuée si masquée, pour montrer visuellement
/// que l'entrée est masquée sans la faire disparaître de la liste). Itère
/// `series` en `PlotMode::XY`, `pieSlices` en `PlotMode::PIE` (Phase 5) — cf.
/// `plotLegendItemCount()`, même compte utilisé pour la mise en page.
void DrawPlotLegendBackground(IUiRenderBackend &ren, const UiPlot &plot, const sdl3::FRect &legendBox);

namespace detail {
/// Ajoute un quad (ruban épais) pour un segment `a`-`b` — même technique que
/// nodegraph.hpp::appendThickSegment (aucune primitive "ligne épaisse"
/// native, cf. Décision #7 du plan).
void AppendThickSegment(std::vector<sdl3::Vertex> &verts, sdl3::FPoint a, sdl3::FPoint b, float halfW, sdl3::FColor color);

/// Ajoute un quad plein (2 triangles) entre deux points de courbe `a`-`b` et
/// leurs projetés respectifs sur la ligne de base `baseA`-`baseB` — remplage
/// « sous la courbe » (Area) segment par segment, plutôt qu'un unique
/// polygone en éventail (`fillPolygon`/`renderGeometryFromPoints`) qui
/// produirait des artefacts sur une courbe non convexe (zigzag).
void AppendFillQuad(std::vector<sdl3::Vertex> &verts, sdl3::FPoint a, sdl3::FPoint b, sdl3::FPoint baseA, sdl3::FPoint baseB,
						   sdl3::FColor color);

void DrawMarker(IUiRenderBackend &ren, sdl3::FPoint center, PlotMarker marker, float size);

/// Polyligne épaisse (segments consécutifs de `pts`) — factorisé car Line ET
/// Step le réutilisent (Step ne diffère que par la construction des points,
/// cf. stepPoints(), pas par le tracé lui-même).
void DrawThickPolyline(IUiRenderBackend &ren, std::span<const sdl3::FPoint> pts, float lineWidth, sdl3::FColor color);

/// Points en « escalier » à partir des points de courbe — convention
/// « post » : la valeur de gauche est maintenue jusqu'au x suivant, puis
/// saute (comme un signal numérique échantillonné-bloqué).
[[nodiscard]] std::vector<sdl3::FPoint> StepPoints(std::span<const sdl3::FPoint> pts);
} // namespace detail

/// Points écran d'une série (utilisé pour le tracé ET par le hit-test de
/// survol) — un point par échantillon `(x[i], y[i])`, dans l'ordre.
[[nodiscard]] std::vector<sdl3::FPoint> PlotSeriesScreenPoints(const PlotSeries &s, const sdl3::FRect &plotRect,
																const PlotAxis &xAxis, const PlotAxis &yAxis);

/// Dessine une série, selon `s.kind` — respecte `onCustomDraw` s'il est posé
/// (remplace ALORS entièrement le rendu par défaut, `dimFactor` ignoré dans
/// ce cas — c'est à l'appelant de le gérer s'il le souhaite). `dimFactor`
/// (dans [0,1]) atténue l'opacité de TOUT ce qui est dessiné — utilisé pour
/// estomper les séries non survolées quand une entrée de légende a le focus
/// (cf. Phase 2, `UiPlot::legendHover`), 1.0 = opacité normale.
void DrawPlotSeries(IUiRenderBackend &ren, const sdl3::FRect &plotRect, const PlotSeries &s, const PlotAxis &xAxis,
						   const PlotAxis &yAxis, float dimFactor = 1.f);

/// Point le plus proche de `mouseScreen` parmi toutes les séries VISIBLES,
/// dans un rayon de `maxDist` pixels — utilisé par le survol
/// (InputSystem::dispatch, systems.hpp). `xAxis`/`yAxis`/`yAxis2` doivent
/// être des axes déjà RÉSOLUS (cf. resolveAxis()) — pas `plot.xAxis`/
/// `plot.yAxis`/`plot.yAxis2` bruts, qui peuvent être en mode auto-fit
/// (min/max non à jour). Chaque série choisit `yAxis` ou `yAxis2` selon
/// `PlotSeries::useSecondaryY` (Phase 4). Retourne {-1,-1} si rien dans le
/// rayon.
[[nodiscard]] std::pair<int, int> PlotNearestPoint(const UiPlot &plot, const sdl3::FRect &plotRect,
														  const PlotAxis &xAxis, const PlotAxis &yAxis,
														  const PlotAxis &yAxis2, sdl3::FPoint mouseScreen,
														  float maxDist = 12.f);

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
[[nodiscard]] std::vector<PieSliceAngles> ComputePieAngles(const std::vector<PieSlice> &slices);

/// Rectangle carré inscrit maximal, centré dans `plotRect` — le cercle du
/// camembert y est inscrit (même esprit que `computePlotRect()` pour XY :
/// une seule source de vérité pour le survol ET le rendu).
[[nodiscard]] sdl3::FRect ComputePieCircleRect(const sdl3::FRect &plotRect) noexcept;

/// Index de la part sous `mouseScreen` (distance <= rayon ET angle dans
/// [start,end) d'une part visible), -1 si hors cercle ou aucune part avec une
/// valeur strictement positive à cet angle.
[[nodiscard]] int PieHitTest(const std::vector<PieSlice> &slices, const std::vector<PieSliceAngles> &angles,
									sdl3::FPoint center, float radius, sdl3::FPoint mouseScreen) noexcept;

namespace detail {
/// Éventail de triangles EXACT depuis `center` (pas l'approximation par
/// centroïde de `fillPolygon`/`renderGeometryFromPoints` — correcte ici
/// seulement parce qu'une part de camembert est un vrai éventail géométrique
/// depuis son centre, contrairement à l'aire sous une courbe zigzag qui a
/// motivé `appendFillQuad` plus haut).
void AppendPieSliceFan(std::vector<sdl3::Vertex> &verts, sdl3::FPoint center, std::span<const sdl3::FPoint> arcPoints,
							  sdl3::FColor color);
} // namespace detail

/// Dessine le camembert (parts pleines PUIS contours dans une passe séparée,
/// pour qu'aucun contour ne soit recouvert par le remplissage d'une part
/// voisine) — `hoveredSlice` (survol direct OU `UiPlot::legendHover`, à
/// l'appelant de choisir lequel prime) atténue les autres parts, même esprit
/// que `dimFactor` de `drawPlotSeries()`.
void DrawPieChart(IUiRenderBackend &ren, const std::vector<PieSlice> &slices,
						 const std::vector<PieSliceAngles> &angles, sdl3::FPoint center, float radius, int hoveredSlice,
						 sdl3::FColor borderColor);

// ============================================================================
// Heatmap (Phase 6) — géométrie/rendu, pas de texte (cf. en-tête du fichier).
// ============================================================================

/// Convertit une valeur normalisée `t` dans [0,1] en couleur selon `cm` —
/// interpolation RGB linéaire par morceaux entre 2-3 couleurs ancrées (cf.
/// doc de `Colormap`).
[[nodiscard]] sdl3::FColor HeatmapColor(float t, Colormap cm) noexcept;

/// Résout la plage [min,max] de normalisation — recalculée À CHAQUE FRAME
/// depuis `values` si `autoRange` (même esprit que `resolveAxis()`, jamais
/// mise en cache), sinon `minValue`/`maxValue` tels quels.
[[nodiscard]] std::pair<float, float> ResolveHeatmapRange(const HeatmapData &hm) noexcept;

/// Dessine la grille colorée — ne fait RIEN si `values` n'a pas au moins
/// `rows*cols` éléments (plutôt que de risquer un accès hors bornes) ; un
/// léger surplomb de 0.5px entre cellules absorbe l'arrondi pixel (évite un
/// liseré de fond visible entre deux cellules adjacentes).
void DrawHeatmap(IUiRenderBackend &ren, const sdl3::FRect &plotRect, const HeatmapData &hm);

/// Index de cellule (`r*cols+c`) sous `mouseScreen`, -1 si hors grille.
[[nodiscard]] int HeatmapHitTest(const HeatmapData &hm, const sdl3::FRect &plotRect, sdl3::FPoint mouseScreen) noexcept;

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
[[nodiscard]] std::pair<float, float> ComputeCandleAutoFit(const std::vector<OhlcBar> &bars,
																   bool forX) noexcept;

/// Comme `resolveAxis()`, mais TOUJOURS résolu depuis `bars` (pas de
/// `PlotAxis::autoFit` à consulter — un chandelier n'a pas de navigation,
/// cf. en-tête de section) ; `min`/`max` de `axis` sont donc ignorés, seuls
/// ses réglages d'affichage (`tickPosition`/`tickOverlay`/`showGrid`/...)
/// sont conservés.
[[nodiscard]] PlotAxis ResolveCandleAxis(const PlotAxis &axis, const std::vector<OhlcBar> &bars,
												bool forX) noexcept;

/// Rectangle écran du CORPS d'une bougie (entre `open` et `close`).
[[nodiscard]] sdl3::FRect ComputeCandleBodyRect(const sdl3::FRect &plotRect, const PlotAxis &xAxis, const PlotAxis &yAxis,
												 const OhlcBar &bar, float candleWidth) noexcept;

/// Dessine toutes les bougies (mèche + corps) — couleur hausse/baisse selon
/// `close` vs `open` (cf. doc de `OhlcBar`). `dimFactor` estompe une bougie
/// donnée (survol, même esprit que `drawPlotSeries()`), -1 = aucune.
void DrawCandleChart(IUiRenderBackend &ren, const sdl3::FRect &plotRect, const std::vector<OhlcBar> &bars,
							const PlotAxis &xAxis, const PlotAxis &yAxis, float candleWidth, sdl3::FColor bullColor,
							sdl3::FColor bearColor, int hoveredBar = -1);

/// Bougie la plus proche de `mouseScreen` EN X (une bougie couvre toute sa
/// hauteur de mèche, donc le survol n'a de sens qu'en distance horizontale,
/// contrairement à `plotNearestPoint()` qui teste des points ponctuels) —
/// -1 si aucune bougie dans `maxDist` pixels.
[[nodiscard]] int CandleNearestBar(const std::vector<OhlcBar> &bars, const sdl3::FRect &plotRect,
										  const PlotAxis &xAxis, sdl3::FPoint mouseScreen, float maxDist = 10.f) noexcept;

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
void ApplyPlotPan(UiPlot &plot, const sdl3::FRect &plotRect, sdl3::FPoint startMouse, sdl3::FPoint currentMouse,
						 const PlotAxis &startXAxis, const PlotAxis &startYAxis,
						 const PlotAxis &startYAxis2) noexcept;

/// Zoom molette centré sur `mouseScreen` : garde la valeur de données sous le
/// curseur FIXE (pivot), réduit/agrandit la plage autour de ce pivot.
/// `wheelY` positif = zoom avant (réduit la plage). `screenMin`/`screenMax` :
/// bornes écran de CET axe, mêmes conventions que `dataToScreen` (inversées
/// pour Y — `screenMin` = bas = `axis.min`, `screenMax` = haut = `axis.max`).
/// Résout l'axe (`resolveAxis`) avant de zoomer pour partir d'une plage à
/// jour même si l'axe est encore en auto-fit (premier geste de l'utilisateur).
/// `forSecondaryY` (Phase 4) : transmis tel quel à `resolveAxis()` — sans
/// effet quand `forX=true` (X est partagé, jamais primaire/secondaire).
void ApplyPlotAxisZoom(PlotAxis &axis, const std::vector<PlotSeries> &series, bool forX, float screenMin,
							  float screenMax, float mouseScreen, float wheelY,
							  bool forSecondaryY = false) noexcept;

/// Calcule et applique le résultat d'un zoom-rectangle (clic-droit glissé) :
/// les deux coins écran sont convertis en données via les axes SNAPSHOT
/// `startXAxis`/`startYAxis`/`startYAxis2` (pris au début du geste), puis
/// min/max sont TRIÉS — gère les deux sens de glisser ET l'inversion
/// écran/données de l'axe Y sans cas particulier. `startYAxis2` toujours
/// appliqué à `plot.yAxis2` (même remarque que `applyPlotPan`, Phase 4).
void ApplyPlotBoxZoom(UiPlot &plot, const sdl3::FRect &plotRect, sdl3::FPoint corner0, sdl3::FPoint corner1,
							 const PlotAxis &startXAxis, const PlotAxis &startYAxis,
							 const PlotAxis &startYAxis2) noexcept;

/// Retour à l'ajustement automatique (double-clic) — repasse simplement les
/// trois axes en `autoFit=true`, `resolveAxis()` recalcule leur plage depuis
/// les séries visibles dès la frame suivante (aucune valeur à recalculer ici).
void ResetPlotToAutoFit(UiPlot &plot) noexcept;

/// Rectangle de sélection du zoom-rectangle, pendant le geste uniquement
/// (`plot.boxZooming`) — fond translucide + contour, coins normalisés (pas
/// d'hypothèse sur le sens de glisser).
void DrawPlotBoxZoomRect(IUiRenderBackend &ren, const UiPlot &plot);

} // namespace ui
