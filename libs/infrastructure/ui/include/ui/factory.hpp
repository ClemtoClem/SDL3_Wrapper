#pragma once
/**
 * ui::Factory — UiTheme (palette + tables de couleurs par widget/état),
 * WidgetBuilder (DSL fluent) et UiFactory (fabrique à thème).
 *
 * Usage :
 * @code{.cpp}
 * ui::LayoutSystem layout;
 * ui::UiFactory f(world, layout);          // thème sombre par défaut
 *
 * f.Panel().Size(300, 0).HAuto().Pad(16).Gap(10)
 *   .Children(
 *       f.Label("Options").FontSize(20),
 *       f.Row().Gap(8).Children(
 *           f.Label("Volume").GrowW(),
 *           f.Slider(0.f, 1.f, 0.8f).W(Dimension::Px(140))
 *               .OnChange([](float v) { ... })),
 *       f.Button("OK").OnClick([] { ... }))
 *   .Spawn();
 * @endcode
 */
#include <memory>

#include "components.hpp"
#include "glyphs.hpp"
#include "interaction.hpp"
#include "plot.hpp"
#include "styles.hpp"
#include "systems.hpp"
#include "viewport3d.hpp"

namespace ui {

// ============================================================================
// UiTheme
// ============================================================================

/// Construit une WidgetColors en ne renseignant que les 7 champs
/// effectivement lus par StyleFromColors() (bgNormal/bgHovered/bgPressed/
/// bgChecked/borderNormal/borderFocus/textNormal) — les autres champs
/// (bgFocus, border{Hovered,Pressed,Checked}, text{Hovered,Pressed,
/// Highlighted}) restent à leur valeur par défaut (transparent), réservés à
/// un usage futur. Volontairement une fonction (affectations de champs) et
/// non un agrégat désigné : GCC (-Wextra) avertit (-Werror en fait une
/// erreur) sur un agrégat désigné qui ne couvre pas tous les champs, même en
/// C++20.
[[nodiscard]] WidgetColors MakeWidgetColors(sdl3::FColor bgNormal, sdl3::FColor bgHovered,
													sdl3::FColor bgPressed, sdl3::FColor bgChecked,
													sdl3::FColor borderNormal, sdl3::FColor borderFocus,
													sdl3::FColor textNormal);

struct UiTheme {
	float fontSize = 14.f;

	/// Si vrai, `UiFactory::Panel()`/`button()` (et les widgets composites
	/// des phases suivantes) attachent automatiquement un style « verre »
	/// (cf. UiStyle::glassPanel/glassButton, styles.hpp) au lieu de leur
	/// remplissage plat habituel — activé par `UiTheme::Aero()`.
	bool glassDefault = false;

	// Palette de base
	sdl3::FColor panelBg{30 / 255.f, 32 / 255.f, 44 / 255.f, 235 / 255.f};
	sdl3::FColor text = sdl3::FColor::UI_TEXT_PRIMARY();
	sdl3::FColor muted{150 / 255.f, 156 / 255.f, 178 / 255.f, 1.f};
	sdl3::FColor track = sdl3::FColor::UI_PANEL_DARK();
	sdl3::FColor fill = sdl3::FColor::UI_ACCENT_BLUE();
	sdl3::FColor thumb = sdl3::FColor::UI_ACCENT_BLUE_LIGHT();
	sdl3::FColor border = sdl3::FColor::UI_BORDER_MUTED();
	sdl3::FColor fieldBg{20 / 255.f, 24 / 255.f, 36 / 255.f, 220 / 255.f};
	sdl3::FColor accent = sdl3::FColor::UI_ACCENT_BLUE_BRIGHT();

	// Tables par widget / état
	WidgetColors button{};
	WidgetColors toggle{};
	WidgetColors checkbox{};
	WidgetColors slider{};
	WidgetColors input{};
	WidgetColors dragValue{}; ///< normal=fond, checked=bande de remplissage, borderFocus=édition
	WidgetColors radio{};
	WidgetColors scrollbar{};
	WidgetColors knob{};
	WidgetColors combo{};    ///< pressed = inutilisé ; checked = item sélectionné
	WidgetColors listbox{};  ///< pressed = pouce de la barre inline
	WidgetColors expander{}; ///< normal/hovered = fond d'en-tête
	WidgetColors tabs{};     ///< checked = onglet actif ; borderFocus = soulignement
	sdl3::FColor spinner{};
	sdl3::FColor badgeBg{};
	sdl3::FColor badgeText{};

	[[nodiscard]] static UiTheme Dark();

	[[nodiscard]] static UiTheme Light();

	/// Thème « atelier » des éditeurs de jeu (Godot, Unity, Unreal) : gris
	/// neutres sans teinte, contraste modéré, un seul bleu pour la sélection
	/// et le focus. Là où `Dark()` est bleuté et contrasté pour une
	/// application, `Studio()` s'efface derrière le contenu — une vue 3D, du
	/// code — qui doit rester ce que l'œil voit en premier.
	[[nodiscard]] static UiTheme Studio();

	/// Thème « verre » Frutiger Aero (Windows 7) : `glassDefault=true` fait
	/// que `panel()`/`button()`/`popup()` (cf. leurs corps) attachent un
	/// style `UiStyle::glassPanel()`/`glassButton()` inline au lieu de leur
	/// remplissage plat habituel — palette bleutée translucide, pas de rendu
	/// spécifique en plus de ce mécanisme déjà en place depuis la Phase 0.
	[[nodiscard]] static UiTheme Aero();
};

/// Convertit une table WidgetColors en style résolvable (normal→Bg,
/// hovered→BgHovered, pressed→BgPressed, checked→BgChecked, text→TextColor,
/// border→BorderColor, borderFocus→FocusedBorderColor). Base des classes
/// "root-<kind>" enregistrées par UiFactory (cf. registerRootClasses()).
[[nodiscard]] UiStyle StyleFromColors(const WidgetColors &c);

// ============================================================================
// WidgetBuilder — accumule la configuration, Spawn() crée l'entité
// ============================================================================


class UiFactory;

class WidgetBuilder {
public:
	WidgetBuilder(ecs::ArchetypeRegistry &world, LayoutSystem &layout) : world(&world), layout(&layout) {}

	WidgetBuilder(WidgetBuilder &&) = default;
	WidgetBuilder &operator=(WidgetBuilder &&) = default;
	WidgetBuilder(const WidgetBuilder &) = delete;
	WidgetBuilder &operator=(const WidgetBuilder &) = delete;

	// ── Placement / taille ───────────────────────────────────────────────────

	WidgetBuilder &W(Dimension v);
	WidgetBuilder &H(Dimension v);
	WidgetBuilder &Size(float pxW, float pxH) { return W(Dimension::Px(pxW)).H(Dimension::Px(pxH)); }
	WidgetBuilder &WAuto() { return W(Dimension::Auto()); }
	WidgetBuilder &HAuto() { return H(Dimension::Auto()); }
	WidgetBuilder &GrowW(float weight = 1.f) { return W(Dimension::Grow(weight)); }
	WidgetBuilder &GrowH(float weight = 1.f) { return H(Dimension::Grow(weight)); }

	WidgetBuilder &Anchor(Anchor a) {
		rect.anchor = a;
		return *this;
	}
	WidgetBuilder &Offset(float x, float y);
	WidgetBuilder &Clip();

	/// Mode d'attache au parent (cf. AttachLayout : Relative/Absolute/Fixed).
	WidgetBuilder &AttachLayout(AttachLayout a) {
		Item().attach = a;
		return *this;
	}
	/// Positionné en absolu dans le parent (ancre + offset, hors flow), mais
	/// toujours dessiné à sa place normale dans l'arbre (sous les widgets
	/// dessinés après lui — peut être recouvert par eux).
	WidgetBuilder &Absolute() { return AttachLayout(AttachLayout::ABSOLUTE); }
	/// Comme absolute(), mais en plus détaché de l'arbre de DESSIN : reporté
	/// par RenderSystem dans une passe overlay après tout le reste, pour
	/// flotter au premier plan quel que soit son parent logique (menus
	/// déroulants, HUD, panneaux flottants...).
	WidgetBuilder &Fixed() { return AttachLayout(AttachLayout::FIXED); }

	WidgetBuilder &Margin(math::Sides m);
	/// Même marge sur les quatre côtés — symétrique de `Pad(float)`, qui
	/// existait déjà ; son absence obligeait à écrire `Margin(math::Sides{0})`
	/// pour la valeur la plus courante.
	WidgetBuilder &Margin(float m) { return Margin(math::Sides(m)); }
	WidgetBuilder &AlignSelf(CrossAlign a);
	WidgetBuilder &MinSize(float mw, float mh);
	WidgetBuilder &MaxSize(float mw, float mh);

	WidgetBuilder &Hidden();
	/// Widget inerte et grisé (UiDisabled, récursif sur le sous-arbre).
	WidgetBuilder &Disabled();
	/// Widget transparent au pointeur (UiPointerThrough, NON récursif) : il
	/// est dessiné, mais c'est le widget situé DERRIÈRE lui qui reçoit survol
	/// et clics — cf. HitTestIndex (components.hpp). Pour une décoration posée
	/// par-dessus une zone interactive (étiquette, voile, cadre).
	WidgetBuilder &PointerThrough();
	/// Infobulle affichée près de la souris après un court survol.
	WidgetBuilder &Tooltip(String text);
	WidgetBuilder &Name(String n);
	WidgetBuilder &Parent(ecs::Entity p);

	// ── Conteneur flow ───────────────────────────────────────────────────────

	WidgetBuilder &Column();
	WidgetBuilder &Row();
	WidgetBuilder &Gap(float g);
	WidgetBuilder &Pad(float p);
	WidgetBuilder &Pad(math::Sides p);
	WidgetBuilder &Justify(Justify j) {
		Flow().justify = j;
		return *this;
	}
	WidgetBuilder &Align(CrossAlign a);
	/// Effet shader post-traitement du sous-arbre de ce widget (M23,
	/// shader_effect.hpp) — bas niveau, cf. `.TintShiftEffect()`/
	/// `.GlowEffect()` ci-dessous pour les deux presets tout faits. Composant
	/// pur (aucun état GPU dessus, cf. UiShaderEffect) : le pipeline/les
	/// textures réels sont gérés par ui::ShaderEffectSystem, appelé par
	/// ui::Ui::Render() si un render3d::Canvas a été enregistré
	/// (Ui::Initialize(render3d::Canvas&)) — no-op sinon, comme Viewport3D.
	WidgetBuilder &Shader(UiShaderEffect effect);
	/// Préréglage : virage de teinte — outColor.rgb = lerp(src.rgb, src.rgb *
	/// `color`.rgb, `strength`). `strength`=0 laisse le contenu inchangé,
	/// `strength`=1 applique le virage plein.
	WidgetBuilder &TintShiftEffect(sdl3::FColor color, float strength = 1.f);
	/// Préréglage : halo radial — outColor.rgb = lerp(src.rgb, `color`.rgb,
	/// smoothstep(`innerRadius`, 0.5, distance(uv, (0.5,0.5)))). `innerRadius`
	/// dans [0, 0.5) : plus petit = halo plus large (commence plus près du
	/// centre).
	WidgetBuilder &GlowEffect(sdl3::FColor color, float innerRadius = 0.3f);

	/// Conteneur scrollable (clip + molette gérée par InputSystem).
	WidgetBuilder &Scrollable();

	// ── Style ────────────────────────────────────────────────────────────────
	// Toutes ces méthodes posent des propriétés dans le style INLINE de
	// l'entité (priorité maximale de la cascade, cf. styles.hpp) — elles
	// s'appliquent quel que soit le type de widget (un widget sans fond
	// propre, ex. UiLabel, ignore juste bg/border puisque RenderSystem ne
	// les lit que pour les widgets qui dessinent un fond).

	/// Pose une couleur de fond ET s'assure qu'un fond sera dessiné (ajoute
	/// le marqueur UiPanel si absent — utilisable sur n'importe quel widget,
	/// pas seulement `panel()`, ex: colorer le fond d'une liste scrollable).
	WidgetBuilder &Bg(sdl3::FColor c);
	WidgetBuilder &Gradient(sdl3::FColor bottom);
	WidgetBuilder &BorderColor(sdl3::FColor c, float width = 1.f);
	WidgetBuilder &Radius(float r);
	WidgetBuilder &FontSize(float fs);
	/// Échelle de contenu du sous-arbre (cf. prop::InnerZoom, styles.hpp —
	/// EmBases::scale/Dimension::Resolve()) : 1.0 = normal, 0.5 = deux fois
	/// plus petit, 2.0 = deux fois plus grand — composé multiplicativement
	/// avec les ancêtres. Générique (node-graph inner-zoom en est le premier
	/// usage, cf. plan), pas de dépendance au node-graph ici.
	WidgetBuilder &InnerZoom(float v);
	WidgetBuilder &TextColor(sdl3::FColor c);
	// ── Débordement du texte (cf. TextOverflow, components.hpp) ─────────────
	//
	// Ces cinq méthodes décrivent CE QUI ARRIVE quand la chaîne est plus large
	// que le widget. Aucune ne contraint la largeur : un widget libre s'étend
	// toujours jusqu'à son texte. Elles n'agissent qu'une fois la largeur
	// bornée — par `W(...)`, par `MaxSize(...)` ou par l'étirement du parent.

	/// Mode générique (les quatre raccourcis ci-dessous sont plus lisibles).
	WidgetBuilder &TextOverflowMode(ui::TextOverflow mode);

	/// Rogne au bord du widget — le comportement par défaut, explicité.
	WidgetBuilder &TextClip() { return TextOverflowMode(ui::TextOverflow::CLIP); }

	/// Rogne en terminant par « … ».
	WidgetBuilder &TextEllipsis() { return TextOverflowMode(ui::TextOverflow::ELLIPSIS); }

	/// Barre de défilement horizontale quand le texte ne tient pas.
	WidgetBuilder &TextScroll() { return TextOverflowMode(ui::TextOverflow::SCROLL); }

	/// Retour automatique à la ligne, avec l'alignement demandé
	/// (`TextAlign::Justify` répartit l'espace entre les mots). Le widget
	/// grandit en hauteur ; bornez-la (`H(...)`, `MaxSize`) pour obtenir à la
	/// place une barre de défilement verticale.
	WidgetBuilder &TextWrap(ui::TextAlign align = ui::TextAlign::Left) {
		StyleMut().SetTextAlign(align);
		return TextOverflowMode(ui::TextOverflow::WRAP);
	}

	/// Défilement automatique aller-retour. `speed` en pixels par seconde,
	/// `pause` en secondes à chaque extrémité.
	WidgetBuilder &TextMarquee(float speed = 40.f, float pause = 1.f);

	WidgetBuilder &TextAlign(TextAlign a) {
		StyleMut().SetTextAlign(a);
		return *this;
	}
	/// Bascule vers la police italique enregistrée (cf.
	/// RenderSystem::RegisterItalicFont()) — sans effet si aucune n'est
	/// enregistrée (retombe silencieusement sur la police normale).
	WidgetBuilder &Italic(bool v = true);
	/// Bascule vers la police grasse enregistrée (cf.
	/// RenderSystem::RegisterBoldFont()) — même comportement de repli
	/// silencieux que italic().
	WidgetBuilder &Bold(bool v = true);
	/// Bande soulignée sous le texte (dessinée, pas un effet de police —
	/// fonctionne avec n'importe quelle combinaison bold()/italic()).
	WidgetBuilder &Underline(bool v = true);
	/// Bande barrant le texte (dessinée, cf. underline()).
	WidgetBuilder &Strikethrough(bool v = true);
	/// Fond plein derrière le texte façon surligneur — couleur via
	/// `.highlightColor(...)` (défaut jaune translucide si non posée).
	WidgetBuilder &Highlight(bool v = true);
	WidgetBuilder &HighlightColor(sdl3::FColor c);
	/// Effet « verre » (thème Aero) — bande de reflet + lueur de bordure,
	/// superposées au fond normal (cf. UiStyle::glass()/RenderSystem).
	WidgetBuilder &Glass(float gloss = 0.35f, float glow = 0.45f);
	/// Mode d'ajustement d'une image (Fill/Contain/Cover/NONE).
	WidgetBuilder &Fit(ImageFit mode);
	/// Restreint édition/copie/collage sur un UiInput ou UiInputArea (défaut :
	/// ReadWriteCopyAndPaste — tout autorisé). Voir IOMode.
	WidgetBuilder &IoMode(IOMode mode);
	/// Longueur maximale du texte (en octets) accepté par un UiInput/UiInputArea.
	WidgetBuilder &MaxLen(size_t n);

	// ── UiInputArea en éditeur de code ───────────────────────────────────────

	/// Mode « éditeur de code » d'un UiInputArea en un appel : chasse fixe,
	/// numéros de ligne, ligne du curseur surlignée, et coloration si
	/// `highlighter` est fourni. Sans effet sur les autres widgets.
	WidgetBuilder &CodeEditor(UiSyntaxHighlighter highlighter = nullptr);
	/// Gouttière de numéros de ligne (UiInputArea).
	WidgetBuilder &LineNumbers(bool enabled = true);
	/// Police à chasse fixe (UiInputArea, cf. Ui::RegisterMonospaceFont).
	WidgetBuilder &Monospace(bool enabled = true);
	/// Coloration syntaxique ligne par ligne (UiInputArea).
	WidgetBuilder &Highlighter(UiSyntaxHighlighter highlighter);

	// ── Callbacks ────────────────────────────────────────────────────────────

	WidgetBuilder &OnClick(std::function<void()> fn);
	WidgetBuilder &OnChange(std::function<void(float)> fn);
	WidgetBuilder &OnToggle(std::function<void(bool)> fn);
	WidgetBuilder &onScroll(std::function<void(float)> fn);
	WidgetBuilder &onTextChange(std::function<void(const String &)> fn);
	/// Notifié à CHAQUE modification du texte d'un UiInput/UiInputArea
	/// (frappe, collage, effacement) — un filtre de recherche « en direct ».
	WidgetBuilder &OnTextChange(std::function<void(const String &)> fn);
	WidgetBuilder &OnSubmit(std::function<void(const String &)> fn);

	// ── Enfants (DSL imbriqué) ───────────────────────────────────────────────

	template <typename... Builders> WidgetBuilder &Children(Builders &&...bs) {
		(kids.push_back(std::make_unique<WidgetBuilder>(std::move(bs))), ...);
		return *this;
	}

	// ── Spawn ────────────────────────────────────────────────────────────────

	ecs::Entity Spawn();

	// ── Styles dynamiques ────────────────────────────────────────────────

	/// Pose un style inline (priorité maximale dans la cascade).
	WidgetBuilder &Style(UiStyle s);

	/// Ajoute une classe CSS (peut être appelé plusieurs fois).
	WidgetBuilder &ClassName(String name);

	/// Pose la liste complète de classes (remplace les appels précédents à className).
	WidgetBuilder &ClassNames(std::vector<String> names);

	/// Fait de cette entité un conteneur de sélection (cf. UiSelection,
	/// interaction.hpp) : les UiSelectable/UiTreeNode descendants (n'importe
	/// où dans le sous-arbre, pas seulement enfants directs — cf.
	/// nearestSelectionAncestor) appliquent leurs clics contre l'état de
	/// sélection posé ICI. `multiSelect=false` restreint ctrl/shift à un
	/// remplacement pur (comme une ListBox).
	WidgetBuilder &SelectionRoot(bool multiSelect = true);

	/// Rend cette entité déplaçable par glisser vertical parmi ses frères
	/// directs (cf. UiReorderable, interaction.hpp) — réordonne réellement
	/// UiChildren du parent. Combinable avec `selectable()`/`treeNode()`
	/// (Phase 4) ou n'importe quelle ligne d'une liste custom ; le PARENT
	/// reçoit la notification `onReorder(fromIndex, toIndex)` (cf.
	/// UiCallbacks — à poser sur le conteneur via `.onReorder()`, pas sur la
	/// ligne elle-même).
	/// Fait de ce widget une SOURCE de glisser-déposer transportant `id`
	/// (cf. UiDragPayload, interaction.hpp) ; `label` non vide : fantôme
	/// dessiné près du pointeur pendant le geste.
	WidgetBuilder &DragPayload(String kind, int64_t id, String label = String());

	/// Fait de ce widget une CIBLE de dépôt. `accepts` vide = tout accepter ;
	/// `highlight` : cadre d'accent dessiné quand un glissé compatible la survole.
	WidgetBuilder &DropTarget(String accepts = String(), bool highlight = true);

	/// Double clic sur un bouton ou une ligne sélectionnable.
	WidgetBuilder &OnDoubleClick(std::function<void()> fn);

	/// Début d'un glissé depuis cette source (cf. UiCallbacks::onDragStart).
	WidgetBuilder &OnDragStart(std::function<void()> fn);

	/// Notifié au dépôt sur CE widget, avec l'identifiant de la source.
	WidgetBuilder &OnDrop(std::function<void(int64_t)> fn);

	/// Notifié au clic DROIT sur ce widget (position écran du pointeur) —
	/// typiquement pour ouvrir un menu contextuel via `Ui::OpenPopupAt`.
	WidgetBuilder &OnContextMenu(std::function<void(float, float)> fn);

	WidgetBuilder &Reorderable();

	WidgetBuilder &OnReorder(std::function<void(int, int)> fn);

	/// Ajoute une série à ce plot (cf. ui/plot.hpp — `.Plot()` doit avoir été
	/// appelé avant, sinon un `Plot` vide est créé au vol). Couleur
	/// résolue depuis la palette catégorielle par défaut (round-robin) si
	/// `color` n'est pas précisé.
	WidgetBuilder &AddSeries(PlotSeriesKind kind, std::span<const float> x, std::span<const float> y,
							 String label = "", Option<sdl3::FColor> color = NONE);
	/// Ajoute une part à ce camembert (cf. ui/plot.hpp — `.PIE()` doit avoir
	/// été appelé avant, sinon un `Plot` vide en `PlotMode::XY` est créé au
	/// vol, ce qui n'a pas de sens pour une part : préférez toujours
	/// `.PIE().addPieSlice(...)`). Couleur résolue depuis la palette
	/// catégorielle par défaut (round-robin) si `color` n'est pas précisé.
	WidgetBuilder &AddPieSlice(float value, String label = "", Option<sdl3::FColor> color = NONE);
	/// Pose la grille de valeurs du heatmap (Phase 6, cf. ui/plot.hpp
	/// `HeatmapData`) — `.heatmap()` doit avoir été appelé avant. `values`
	/// (ordre ligne principale) doit avoir au moins `rows*cols` éléments,
	/// sinon `drawHeatmap()` ne dessine rien plutôt que de risquer un accès
	/// hors bornes.
	WidgetBuilder &SetHeatmapData(int rows, int cols, std::span<const float> values,
								  Colormap cm = Colormap::VIRIDIS);
	/// Ajoute une bougie à ce chandelier (Phase 6, cf. ui/plot.hpp
	/// `OhlcBar`) — `.Candlestick()` doit avoir été appelé avant.
	WidgetBuilder &AddOhlcBar(float x, float open, float high, float low, float close);
	/// Pose des barres d'erreur Y (Phase 5, cf. `PlotSeries::yError`) sur la
	/// DERNIÈRE série ajoutée — même pattern que `useSecondaryY()`, à
	/// appeler juste après un `addXSeries(...)`. `err` doit avoir la même
	/// taille que les `y[]` de cette série (sinon les indices en trop sont
	/// simplement ignorés au rendu, cf. `drawPlotSeries`).
	WidgetBuilder &SetYError(std::span<const float> err);
	/// Pose l'échappatoire de rendu personnalisé (Phase 7, cf.
	/// `PlotSeries::onCustomDraw`) sur la DERNIÈRE série ajoutée — même
	/// pattern que `useSecondaryY()`/`setYError()`. Remplace ENTIÈREMENT le
	/// rendu par défaut de cette série (`kind`/couleur/marqueur/etc. ne sont
	/// alors utilisés que par la légende, pas par le tracé).
	WidgetBuilder &SetOnCustomDraw(std::function<void(sdl3::Renderer &, const sdl3::FRect &, const PlotSeries &,
													   const PlotAxis &, const PlotAxis &)>
										fn);
	/// Série Line, X implicite (indices 0..N-1).
	WidgetBuilder &AddLineSeries(std::span<const float> y, String label = "", Option<sdl3::FColor> color = NONE);
	/// Série Line, X explicite.
	WidgetBuilder &AddLineSeriesXy(std::span<const float> x, std::span<const float> y, String label = "",
								   Option<sdl3::FColor> color = NONE);
	/// Série Scatter (nuage de points), X explicite.
	WidgetBuilder &AddScatterSeries(std::span<const float> x, std::span<const float> y, String label = "",
									Option<sdl3::FColor> color = NONE);
	/// Série Step (« escalier », valeur maintenue jusqu'au x suivant), X explicite.
	WidgetBuilder &AddStepSeries(std::span<const float> x, std::span<const float> y, String label = "",
								 Option<sdl3::FColor> color = NONE);
	/// Série Area (courbe + remplissage jusqu'à la ligne de base 0), X
	/// explicite — `fillOpacity` dans [0,1].
	WidgetBuilder &AddAreaSeries(std::span<const float> x, std::span<const float> y, String label = "",
								 Option<sdl3::FColor> color = NONE, float fillOpacity = 0.35f);
	/// Série Stem (« lollipop » : ligne verticale depuis 0 + marqueur), X explicite.
	WidgetBuilder &AddStemSeries(std::span<const float> x, std::span<const float> y, String label = "",
								 Option<sdl3::FColor> color = NONE);
	/// Série Bar (barres verticales depuis la ligne de base 0), X explicite
	/// — `barWidth` en unités DONNÉES (cf. PlotSeries::barWidth).
	WidgetBuilder &AddBarSeries(std::span<const float> x, std::span<const float> y, String label = "",
								Option<sdl3::FColor> color = NONE, float barWidth = 0.67f);
	/// Série BarH (barres horizontales) — `x[i]` = longueur, `y[i]` =
	/// position/catégorie (cf. doc de PlotSeriesKind::BAR_H).
	WidgetBuilder &AddBarHSeries(std::span<const float> x, std::span<const float> y, String label = "",
								 Option<sdl3::FColor> color = NONE, float barWidth = 0.67f);
	/// Série Histogram (barres jointives) — `x[]` = centres de classe, `y[]`
	/// = effectifs (pas de binning automatique, cf. doc de PlotSeriesKind).
	WidgetBuilder &AddHistogramSeries(std::span<const float> x, std::span<const float> y, String label = "",
									  Option<sdl3::FColor> color = NONE, float barWidth = 1.f);

	/// Style des graduations de l'axe X (cf. ui/plot.hpp `PlotAxis`) —
	/// `position` doit être `Top`/`Bottom` (`Left`/`Right` n'a pas de sens
	/// pour un axe X, non validé). `overlay=true` fait flotter les
	/// graduations par-dessus la zone de tracé (avec `boxColor` en fond) au
	/// lieu de réserver un espace dédié en permanence.
	WidgetBuilder &XTicks(PlotEdge position, bool overlay = false, Option<sdl3::FColor> boxColor = NONE);
	/// Style des graduations de l'axe Y — `position` doit être `Left`/`Right`.
	WidgetBuilder &YTicks(PlotEdge position, bool overlay = false, Option<sdl3::FColor> boxColor = NONE);
	/// Position/mode de la légende (cf. ui/plot.hpp `LegendPosition`) —
	/// `overlay=true` (défaut) la fait flotter par-dessus la zone de tracé
	/// (avec `boxColor` en fond), `false` réserve un espace dédié sur le
	/// bord correspondant.
	WidgetBuilder &Legend(LegendPosition position, bool overlay = true, Option<sdl3::FColor> boxColor = NONE);
	/// Affiche/masque entièrement la légende (visible par défaut dès qu'une
	/// série porte un `label`).
	WidgetBuilder &ShowLegend(bool show);
	/// Échelle logarithmique (base 10, Phase 3) par axe — cf. doc
	/// `PlotAxis::logScale` (données strictement positives requises).
	WidgetBuilder &LogScaleX(bool enable = true);
	WidgetBuilder &LogScaleY(bool enable = true);
	/// Échelle logarithmique de l'axe Y SECONDAIRE (Phase 4, cf. `yAxis2`).
	WidgetBuilder &LogScaleY2(bool enable = true);
	/// Bascule la DERNIÈRE série ajoutée (via `addLineSeries()`/etc.) sur
	/// l'axe Y secondaire `yAxis2` (Phase 4) — à appeler juste après l'ajout
	/// de la série concernée. `yAxis2` n'est dessiné/résolu que si au moins
	/// une série l'utilise (cf. `plotHasSecondaryY()`), pas de builder séparé
	/// pour "activer" l'axe lui-même.
	WidgetBuilder &UseSecondaryY(bool enable = true);
	/// Style des graduations de l'axe Y SECONDAIRE (Phase 4) — mêmes règles
	/// que `yGetTicks()`, `position` doit être `Left`/`Right`. Sans effet visible
	/// si aucune série n'a `useSecondaryY=true`.
	WidgetBuilder &YTicks2(PlotEdge position, bool overlay = false, Option<sdl3::FColor> boxColor = NONE);

	/// Ordre de tri du pass overlay Fixed (cf. UiOverlayLayer, systems.hpp) —
	/// utile pour empiler un sous-menu au-dessus de son menu parent, ou une
	/// modale au-dessus d'un popup déjà ouvert. Défaut 0 si jamais posé.
	WidgetBuilder &OverlayOrder(int order);

private:
	friend class UiFactory;

	ecs::ArchetypeRegistry *world;
	LayoutSystem *layout;

	UiRect rect{};
	Option<UiFlow> flow = NONE;
	Option<UiItem> item = NONE;
	Option<UiPanel> panel = NONE;
	Option<UiLabel> label = NONE;
	Option<UiButton> button = NONE;
	Option<UiToggle> toggle = NONE;
	Option<UiCheckbox> checkbox = NONE;
	Option<UiSlider> slider = NONE;
	Option<UiProgress> progress = NONE;
	Option<UiSeparator> separator = NONE;
	Option<UiInput> input = NONE;
	Option<UiInputArea> inputArea = NONE;
	Option<UiDragValue> dragValue = NONE;
	Option<UiColorSwatch> colorSwatch = NONE;
	Option<UiSVSquare> svSquare = NONE;
	Option<UiHueSlider> hueSlider = NONE;
	Option<UiAlphaSlider> alphaSlider = NONE;
	Option<UiSelectable> selectable = NONE;
	Option<UiTreeNode> treeNode = NONE;
	Option<UiReorderable> reorderable = NONE;
	Option<UiTextOverflow> textOverflow = NONE;
	Option<UiDragPayload> dragPayload = NONE;
	Option<UiDropTarget> dropTarget = NONE;
	Option<UiSelection> selection = NONE;
	Option<UiMenuBarItem> menuBarItem = NONE;
	Option<UiMenuItem> menuItem = NONE;
	Option<UiTable> table = NONE;
	Option<UiPlot> plot = NONE;
	Option<UiCalendar> calendar = NONE;
	Option<UiResizeHandle> resizeHandle = NONE;
	Option<UiImage> image = NONE;
	Option<UiViewport3D> viewport3d = NONE;
	Option<UiShaderEffect> shaderEffect = NONE;
	Option<UiIcon> icon = NONE;
	Option<UiRadio> radio = NONE;
	Option<UiScrollBar> scrollbar = NONE;
	Option<UiKnob> knob = NONE;
	Option<UiCanvas> canvas = NONE;
	Option<UiComboBox> combo = NONE;
	Option<ListBox> listbox = NONE;
	Option<UiExpander> expander = NONE;
	Option<UiTabView> tabview = NONE;
	Option<UiSpinner> spinner = NONE;
	Option<UiBadge> badge = NONE;
	Option<UiTooltip> tooltip = NONE;
	Option<UiPopupState> popupState = NONE;
	Option<UiOverlayLayer> overlayLayer = NONE;
	Option<UiCallbacks> callbacks = NONE;
	Option<UiName> name = NONE;
	Option<ecs::Entity> parent = NONE;
	Option<UiStyle> style = NONE;
	std::vector<String> classNames;
	bool hidden = false;
	bool disabled = false;
	bool pointerThrough = false;
	std::vector<std::unique_ptr<WidgetBuilder>> kids;

	// NB : Option<T>::Unwrap() renvoie une COPIE — pour muter en place il faut
	// passer par operator*/operator-> qui renvoient bien une référence.
	UiFlow &Flow();
	UiItem &Item();
	UiPanel &Panel();
	UiCallbacks &Callbacks();
	UiStyle &StyleMut();

	UiTextOverflow &OverflowMut();

	/// Nom de la classe de défauts thémés correspondant au type de widget
	/// déjà posé sur ce builder (ex: "root-button"), ou NONE si ce type n'a
	/// pas de défauts spécifiques (image/canvas/tooltip...). Utilisé par
	/// Spawn() pour préfixer la UiClassList — cf. UiFactory::registerRootClasses.
	[[nodiscard]] Option<String> KindClass() const;
};

// ============================================================================
// UiFactory
// ============================================================================

class UiFactory {
public:
	UiTheme theme;

	/// Classes CSS-like partagées par tous les widgets de cette factory
	/// (`sheet.define("danger", UiStyle{}.SetBg(...))`, puis `.className
	/// ("danger")` sur un WidgetBuilder). Résolu par le StyleSystem de la
	/// façade `Ui` (cf. ui.hpp) — voir styles.hpp pour la cascade complète.
	UiStyleSheet sheet;

	UiFactory(ecs::ArchetypeRegistry &world, LayoutSystem &layout, UiTheme t = UiTheme::Dark());

	[[nodiscard]] ecs::ArchetypeRegistry &World() noexcept { return *world; }
	[[nodiscard]] LayoutSystem &Layout() noexcept { return *layout; }

	/// Remplace le thème À CHAUD : réenregistre les classes « root-* » avec
	/// la nouvelle palette et force la cascade à se re-résoudre sur tous les
	/// widgets déjà construits.
	///
	/// Sans cette méthode, le thème n'était modifiable qu'à la construction
	/// de `UiFactory` — écrire dans `theme` après coup ne touchait que les
	/// widgets créés ENSUITE, laissant l'interface à moitié repeinte.
	/// Manque relevé en implémentant le sélecteur de thème de l'éditeur de
	/// niveau, où l'utilisateur bascule dark/light/aero à tout moment.
	void SetTheme(UiTheme newTheme);

	// ── Conteneurs ───────────────────────────────────────────────────────────

	[[nodiscard]] WidgetBuilder Column();
	[[nodiscard]] WidgetBuilder Row();

	/// Colonne avec fond de panneau thémé (carte) — cf. classe "root-panel"
	/// pour les défauts (fond/bordure/radius) ; en thème verre (Aero), un
	/// style glassPanel inline remplace le fond plat.
	[[nodiscard]] WidgetBuilder Panel();

	// ── Widgets ──────────────────────────────────────────────────────────────
	// Toutes les couleurs/tailles de police par défaut viennent désormais des
	// classes "root"/"root-<type>" (cf. registerRootClasses()) résolues par
	// le style, pas de champs sur les composants — cf. components.hpp.

	[[nodiscard]] WidgetBuilder Label(String text);

	[[nodiscard]] WidgetBuilder Button(String text);

	[[nodiscard]] WidgetBuilder Toggle(bool checked = false);

	[[nodiscard]] WidgetBuilder Checkbox(bool checked = false);

	[[nodiscard]] WidgetBuilder Slider(float min, float max, float value, float step = 0.f,
									   Orientation orient = Orientation::Horizontal);

	[[nodiscard]] WidgetBuilder Progress(float min, float max, float value);

	[[nodiscard]] WidgetBuilder Separator(Orientation orient = Orientation::Horizontal);

	/// Champ de saisie mono-ligne (word-wrap + défilement vertical
	/// automatique si le texte enveloppé dépasse la hauteur du widget —
	/// cf. UiInput). `.ioMode()` pour restreindre édition/copie/collage.
	[[nodiscard]] WidgetBuilder Input(String placeholder = "");

	/// Éditeur/visionneuse multi-ligne, sans word-wrap, défilement vertical
	/// ET horizontal automatique (cf. UiInputArea). Usage typique : `.ioMode
	/// (IOMode::ReadAndCopyOnly)` pour une visionneuse de logs copiable.
	[[nodiscard]] WidgetBuilder InputArea(String placeholder = "");

	/// Valeur numérique façon ImGui::DragFloat — glisser change `value`
	/// (`speed` unités/pixel), DOUBLE-clic bascule en saisie texte directe
	/// (cf. UiDragValue). `step` : quantification (0 = continu).
	[[nodiscard]] WidgetBuilder DragValue(float min, float max, float value, float step = 0.f, float speed = 1.f,
										  int decimals = 2);

	/// Champ numérique façon `<input type=number>` : boutons +/- de part et
	/// d'autre d'un UiInput (COMPOSITE — pas de nouveau composant leaf, cf.
	/// plan Phase 3). `onChange` est notifié par les boutons ET par la saisie
	/// directe (Entrée dans le champ) ; jamais pendant la frappe elle-même.
	[[nodiscard]] WidgetBuilder InputNumber(float min, float max, float value, float step = 1.f, int decimals = 2,
											std::function<void(float)> onChange = nullptr);

	/// Pastille de couleur cliquable (cf. UiColorSwatch) — typiquement
	/// déclencheur d'un `colorPicker()` voisin via `.OnClick([&]{
	/// ui::OpenPopup(world, layout, pickerEntity, swatchEntity); })`.
	[[nodiscard]] WidgetBuilder ColorSwatch(sdl3::FColor initial = sdl3::FColor::WHITE());

	/// Carré saturation/valeur brut (cf. UiSVSquare) — brique interne de
	/// `colorPicker()`, exposée pour composer un sélecteur sur mesure.
	[[nodiscard]] WidgetBuilder SvSquare(float h = 0.f, float s = 1.f, float v = 1.f);

	/// Glissière de teinte brute [0,360) (cf. UiHueSlider) — brique interne
	/// de `colorPicker()`.
	[[nodiscard]] WidgetBuilder HueSlider(float hue = 0.f);

	/// Glissière alpha brute [0,1] (cf. UiAlphaSlider) — brique interne de
	/// `colorPicker()`.
	[[nodiscard]] WidgetBuilder AlphaSlider(float alpha = 1.f, sdl3::FColor baseColor = sdl3::FColor::WHITE());

	/// Sélecteur de couleur complet, en popup (cf. UiFactory::Popup(), Phase
	/// 1) : carré SV + glissière de teinte + glissière alpha + champ hex +
	/// aperçu, tous synchronisés entre eux. À ouvrir/fermer via
	/// `ui::OpenPopup`/`closePopup` (typiquement déclenché par un
	/// `colorSwatch()` voisin). `onChange` reçoit la sdl3::FColor complète (RGBA) à
	/// chaque ajustement — jamais pendant la frappe dans le champ hex.
	[[nodiscard]] WidgetBuilder ColorPicker(sdl3::FColor initial, std::function<void(sdl3::FColor)> onChange = nullptr);

	/// Ligne sélectionnable façon ImGui::Selectable (cf. UiSelectable) —
	/// surbrillance persistante pilotée par le UiSelection du plus proche
	/// ancêtre (posez `.SelectionRoot()` sur le conteneur racine de la
	/// liste). `index` doit être unique dans ce conteneur (position
	/// logique — PAS forcément l'ordre d'ajout si des lignes sont filtrées).
	/// Combinez avec `.reorderable()` pour le glisser-déposer (Phase 4).
	[[nodiscard]] WidgetBuilder Selectable(String text, int index);

	/// Nœud d'arbre repliable ET sélectionnable (cf. UiTreeNode) — l'en-tête
	/// replie/déplie les enfants ajoutés via `.Children(...)` (typiquement
	/// d'autres `treeNode()`/`selectable()`, formant l'arbre récursivement) ;
	/// un clic ailleurs sur l'en-tête sélectionne, comme `selectable()`
	/// (même UiSelection ancêtre requis). `depth` est purement informatif —
	/// l'indentation vient du padding cumulé de chaque niveau imbriqué, pas
	/// d'un calcul dépendant de `depth`.
	[[nodiscard]] WidgetBuilder TreeNode(String title, int index, int depth = 0, bool expanded = true);

	/// Barre de menu horizontale (ex: menu principal d'une fenêtre) —
	/// conteneur simple, les entrées s'ajoutent via `menu()` APRÈS l'avoir
	/// spawnée (cf. menu() : chaque entrée a besoin de l'ecs::Entity de la barre
	/// déjà existante pour s'y attacher, comme son propre popup déroulant).
	[[nodiscard]] WidgetBuilder MenuBar();

	/// Entrée de barre de menu SIMPLE, sans son propre menu déroulant — texte
	/// cliquable façon UiButton (peu utilisé directement ; préférez
	/// `menu()` pour une vraie entrée de barre de menu AVEC déroulant).
	[[nodiscard]] WidgetBuilder MenuItem(String text, String shortcut = "");

	/// Entrée de barre de menu + son menu déroulant, en un seul appel : spawn
	/// l'item comme enfant de `bar` (déjà spawnée, cf. menuBar()) et son
	/// popup comme racine INDÉPENDANTE (nécessaire pour que son ancre/offset
	/// se résolvent en coordonnées fenêtre absolues via positionPopupBelow,
	/// cf. systems.hpp — un popup Fixed enfant se positionnerait relativement
	/// à la boîte de contenu de SON parent, pas de la fenêtre). `items...`
	/// sont typiquement des `menuItem(...)` — pour une entrée AVEC sous-menu,
	/// utilisez `subMenu()` séparément avec le popup RETOURNÉ ici comme
	/// `parentPopup` (les entrées avec sous-menu atterrissent alors APRÈS
	/// celles passées ici, cf. subMenu()). Retourne l'ecs::Entity du POPUP (pas de
	/// l'item) — utile pour `subMenu()` ou une fermeture programmatique via
	/// `ui::closePopup`.
	template <typename... Builders> ecs::Entity Menu(ecs::Entity bar, String text, Builders &&...items) {
		WidgetBuilder itemBuilder(*world, *layout);
		itemBuilder.menuBarItem = Some(UiMenuBarItem{std::move(text), ecs::Entity{}, false});
		itemBuilder.WAuto().HAuto();
		itemBuilder.Parent(bar);
		ecs::Entity itemEntity = itemBuilder.Spawn();

		WidgetBuilder popupBuilder = MenuPopup(10);
		popupBuilder.Children(std::forward<Builders>(items)...);
		ecs::Entity popupEntity = popupBuilder.Spawn();

		if (auto mb = world->GetComponent<UiMenuBarItem>(itemEntity); mb.IsSome())
			mb.Unwrap()->menuPopup = popupEntity;
		return popupEntity;
	}

	/// Menu contextuel : un popup de menu SANS entrée de barre, à ouvrir au
	/// clic droit via `ui::OpenPopupAt` (cf. WidgetBuilder::OnContextMenu).
	/// Mêmes items que `Menu()` — `MenuItem(...)` pour une action, puis
	/// `SubMenu(popupRetourné, ...)` pour une entrée à sous-menu (qui se
	/// déploie à droite, et se referme avec toute la chaîne au clic d'une
	/// feuille). Retourne l'entité du POPUP, racine indépendante comme celle
	/// de `Menu()` : c'est ce qui lui permet d'apparaître par-dessus tout.
	template <typename... Builders> ecs::Entity ContextMenu(Builders &&...items) {
		WidgetBuilder popupBuilder = MenuPopup(10);
		popupBuilder.Children(std::forward<Builders>(items)...);
		return popupBuilder.Spawn();
	}

	/// Entrée de menu AVEC sous-menu : comme `menu()`, mais l'item déclencheur
	/// est un `UiMenuItem` (pas `UiMenuBarItem`) parenté sous `parentPopup`
	/// (l'ecs::Entity retournée par `menu()`/`subMenu()` du niveau parent — PAS
	/// l'entité d'un item). Le sous-menu s'ouvre au clic, positionné à
	/// DROITE de l'item (cf. positionPopupRightOf). Retourne l'ecs::Entity du
	/// popup du SOUS-menu (pour un niveau supplémentaire si besoin — le plan
	/// n'en requiert qu'un, mais le mécanisme est général).
	template <typename... Builders> ecs::Entity SubMenu(ecs::Entity parentPopup, String text, Builders &&...items) {
		WidgetBuilder itemBuilder(*world, *layout);
		UiMenuItem mi;
		mi.text = std::move(text);
		mi.hasSubmenu = true;
		itemBuilder.menuItem = Some(std::move(mi));
		itemBuilder.WAuto().HAuto();
		itemBuilder.Parent(parentPopup);
		ecs::Entity itemEntity = itemBuilder.Spawn();

		WidgetBuilder popupBuilder = MenuPopup(11);
		popupBuilder.Children(std::forward<Builders>(items)...);
		ecs::Entity popupEntity = popupBuilder.Spawn();

		if (auto mi2 = world->GetComponent<UiMenuItem>(itemEntity); mi2.IsSome())
			mi2.Unwrap()->submenuPopup = popupEntity;
		return popupEntity;
	}

	/// Table façon ImGui::Table (cf. UiTable, components.hpp) : `columns`
	/// définit l'en-tête (titre + largeur initiale + triable ou non),
	/// `rows[i]` doit avoir autant d'entrées que `columns` (cellules
	/// manquantes tolérées, affichées vides). Colonnes redimensionnables par
	/// glisser sur leur bordure droite, triables par clic d'en-tête (cycle
	/// Aucun→Croissant→Décroissant), multi-sélection de lignes (clic/Ctrl+
	/// clic/Maj+clic — cf. UiSelection, posée automatiquement) ;
	/// `reorderable=true` permet en plus de glisser une ligne pour la
	/// déplacer (notifie `.onReorder(fromIndex,toIndex)`). Le tri notifie
	/// `.OnChange(columnIndexAsFloat)` et VIDE la sélection courante (les
	/// index ne correspondraient plus aux bonnes lignes après un tri, cf.
	/// le composant pour la limitation).
	[[nodiscard]] WidgetBuilder Table(std::vector<UiTableColumn> columns, std::vector<std::vector<String>> rows,
									  bool multiSelect = true, bool reorderable = false);

	/// Plot vide (cf. ui/plot.hpp) — ajoutez des séries via
	/// `.AddLineSeries()`/`.AddScatterSeries()` (WidgetBuilder) puis
	/// `.Spawn()`. `title` est un texte optionnel superposé en haut, centré.
	[[nodiscard]] WidgetBuilder Plot(String title = "");

	/// Raccourci pour un plot à une seule série Line, X implicite (indices
	/// 0..N-1) — même forme d'appel que l'ancien `plotLines()`, reconstruit
	/// sur le nouveau système (`.Plot().AddLineSeries(...)`, cf. ci-dessous).
	[[nodiscard]] WidgetBuilder PlotLines(std::span<const float> values, String title = "",
										  Option<sdl3::FColor> color = NONE);

	/// Camembert vide (Phase 5, `PlotMode::PIE`) — ajoutez des parts via
	/// `.addPieSlice(...)` (WidgetBuilder) puis `.Spawn()`. Pas d'axes ni de
	/// navigation (pan/zoom/box-zoom, Phase 3) sur ce mode, cf. doc de
	/// `PlotMode`.
	[[nodiscard]] WidgetBuilder Pie(String title = "");

	/// Heatmap vide (Phase 6, `PlotMode::HEATMAP`) — posez la grille via
	/// `.setHeatmapData(...)` (WidgetBuilder) puis `.Spawn()`. Pas d'axes ni
	/// de légende sur ce mode (une grille de couleur n'a pas d'"entrée").
	[[nodiscard]] WidgetBuilder Heatmap(String title = "");

	/// Chandelier vide (Phase 6, `PlotMode::CANDLE`) — ajoutez des bougies
	/// via `.AddOhlcBar(...)` (WidgetBuilder) puis `.Spawn()`. Graduations/
	/// grille dans le style XY (réutilise `xAxis`/`yAxis`), mais toujours
	/// auto-ajustées (pas de pan/zoom/box-zoom sur ce mode, cf. doc de
	/// `resolveCandleAxis()`) et pas de légende (une seule série de bougies).
	[[nodiscard]] WidgetBuilder Candlestick(String title = "");

	/// Panneau redimensionnable en 2 (cf. UiResizeHandle, interaction.hpp —
	/// RÉUTILISE le composant/l'entité directement, pas juste l'algorithme,
	/// contrairement aux colonnes de UiTable qui le réimplémentent en
	/// interne). `before`/`after` sont deux builders NON encore spawnés
	/// (typiquement `panel()`/`column()` avec leur contenu) ; la poignée
	/// s'insère entre les deux, glissable, épinglant la taille de `before`
	/// en pixels pendant le drag (`after` en Grow absorbe le reste — cf.
	/// resolveResizeDrag()). Retourne l'ecs::Entity du CONTENEUR (row/column).
	[[nodiscard]] ecs::Entity Splitter(WidgetBuilder before, WidgetBuilder after,
								  Orientation orient = Orientation::Horizontal, float initialBeforeSize = 200.f,
								  float minBefore = 50.f, float minAfter = 50.f);

	/// Sélecteur de date, en popup (cf. UiFactory::Popup(), Phase 1) : grille
	/// calendrier mensuel (cf. UiCalendar, components.hpp) avec navigation
	/// précédent/suivant. `onChange` reçoit (année, mois, jour) au clic sur
	/// une case — signature à 3 valeurs, donc posée DIRECTEMENT sur le
	/// composant (UiCalendar::onDaySelected) plutôt que via
	/// UiCallbacks::onChange (qui n'en porte qu'une).
	[[nodiscard]] WidgetBuilder DatePicker(int year, int month, int selectedDay = -1,
										   std::function<void(int, int, int)> onChange = nullptr);

	[[nodiscard]] WidgetBuilder Image(String textureKey, float w, float h);

	/// Widget affichant une scène render3d:: indépendante (M22, viewport3d.hpp)
	/// — `root` DOIT rester en vie au moins aussi longtemps que le widget (cf.
	/// UiViewport3D::root, non possédé : Object3D est non-copiable/non-
	/// déplaçable). Aucune taille par défaut posée ici (contrairement à
	/// `Image()`) : un Viewport3D n'a pas de dimension "intrinsèque" comme une
	/// texture chargée — l'appelant pose `.Size(...)`/`.GrowW()`/etc. comme pour
	/// n'importe quel autre widget ; Viewport3DSystem::Update lit simplement la
	/// taille résolue par LayoutSystem chaque frame (UiComputed.screen).
	[[nodiscard]] WidgetBuilder Viewport3D(render3d::Object3D &root, render3d::Camera camera = {});

	/// Icône glyphe (Cupertino/Material...) — `value` est contraint par le
	/// concept `GlyphSet` à une valeur d'un `enum class` d'icônes déclaré
	/// dans glyphs.hpp (jamais un entier ou un nom en chaîne). La police
	/// correspondante doit être chargée et enregistrée sous son nom de
	/// famille via `RenderSystem::RegisterFont(Glyphs::FontFamily<E>(), font)`
	/// pour que l'icône se dessine (cf. Glyphs::fontFile<E>() pour le fichier
	/// à charger, dans assets/fonts/). Teinte : text-color (classe "root-icon").
	template <GlyphSet E>
	[[nodiscard]] WidgetBuilder Icon(E value, float size = 16.f) {
		WidgetBuilder b(*world, *layout);
		UiIcon ic;
		ic.glyph = Glyphs::ToUtf8(value);
		ic.font = String(Glyphs::FontFamily<E>());
		ic.size = size;
		b.icon = Some(std::move(ic));
		b.Size(size, size);
		return b;
	}

	/// Bouton radio : un seul coché à la fois parmi ceux du même `group`.
	[[nodiscard]] WidgetBuilder Radio(String group, String text, bool checked = false);

	/// Barre de défilement autonome (viewSize visible sur contentSize total).
	[[nodiscard]] WidgetBuilder Scrollbar(float contentSize, float viewSize,
										  Orientation orient = Orientation::Horizontal);

	/// Potentiomètre rotatif, valeur normalisée [0,1].
	[[nodiscard]] WidgetBuilder Knob(float value = 0.f) { return Knob(0.f, 1.f, value, 0.f); }

	/// Potentiomètre rotatif à plage [min, max], quantifié par `step` (0 = continu).
	[[nodiscard]] WidgetBuilder Knob(float min, float max, float value, float step = 0.f);

	/// Zone de dessin libre — `fn(renderer, rectEcran)` appelée au rendu.
	[[nodiscard]] WidgetBuilder Canvas(std::function<void(sdl3::Renderer &, sdl3::FRect)> fn);

	/// Liste déroulante. `selected` : index initial (-1 = aucun). onChange
	/// reçoit l'index sélectionné (en float).
	[[nodiscard]] WidgetBuilder Combo(std::vector<String> items, int selected = 0);

	/// Liste déroulante par catégories : chaque catégorie titrée devient une
	/// ligne « Titre ▸ » dont les éléments s'ouvrent à droite au survol, comme
	/// un sous-menu de Menu ; une catégorie SANS titre met ses éléments au
	/// premier niveau. Les index (`selected`, onChange) comptent les éléments
	/// de toutes les catégories bout à bout, dans l'ordre :
	/// `Combo({{"Scènes", {"A", "B"}}, {"Objets", {"C"}}}, 2)` choisit « C ».
	[[nodiscard]] WidgetBuilder Combo(std::vector<ComboCategory> categories, int selected = 0);

	/// Liste sélectionnable à scroll interne. onChange reçoit l'index.
	[[nodiscard]] WidgetBuilder Listbox(std::vector<String> items, int selected = -1);

	/// Section repliable : les enfants forment le corps, l'en-tête les
	/// replie/déplie. onToggle reçoit le nouvel état.
	[[nodiscard]] WidgetBuilder Expander(String title, bool expanded = true);

	/// Conteneur à onglets : un enfant par entrée de `tabs`, seul l'actif est
	/// visible. onChange reçoit l'index de l'onglet activé.
	[[nodiscard]] WidgetBuilder Tabview(std::vector<String> tabs, int active = 0);

	/// Indicateur de chargement animé (arc tournant).
	[[nodiscard]] WidgetBuilder Spinner(float thickness = 3.f);

	/// Pastille de notification (compteur, statut...).
	[[nodiscard]] WidgetBuilder Badge(String text);

	/// Popup ancré (menu contextuel, liste de suggestions, dropdown d'un
	/// color picker...) : caché par défaut, à ouvrir/fermer via
	/// `ui::OpenPopup`/`ui::closePopup` (positionné au clic ouvrant — poser
	/// `.Anchor(...)`/`.Offset(...)` avant `openPopup` pour le placer près de
	/// son déclencheur). Pas de scrim, se ferme au clic extérieur ou Échap
	/// n'affecte QUE les modales — cf. InputSystem::dispatch. Composez son
	/// contenu via `.Children(...)` comme n'importe quel conteneur.
	[[nodiscard]] WidgetBuilder Popup();

	/// Popup de MENU (déroulant de barre, menu contextuel, sous-menu), dessiné
	/// comme la liste d'un combo ouvert : un seul fond (celui des champs),
	/// un liseré d'accent, et des entrées en lignes pleine largeur SANS
	/// espacement ni fond propre au repos — seule la ligne survolée se
	/// détache (cf. RenderSystem, UiMenuItem). Thème verre : le panneau
	/// « verre » habituel.
	[[nodiscard]] WidgetBuilder MenuPopup(int overlayOrder);

	/// Boîte de dialogue modale, centrée, avec fond assombri qui bloque tout
	/// le reste de l'interface tant qu'elle est ouverte (cf.
	/// InputSystem::OpenModal/closeModal — Échap la referme). `content` est
	/// le panneau du dialogue lui-même : dimensionnez-le normalement (ex:
	/// `f.Panel().Size(400, 240).Children(...)`), il est automatiquement
	/// enveloppé avec le scrim plein écran et centré.
	[[nodiscard]] WidgetBuilder Modal(WidgetBuilder content);

private:
	ecs::ArchetypeRegistry *world;
	LayoutSystem *layout;

	/// Enregistre "root" (défauts génériques : police/couleur de texte du
	/// thème) et "root-<type>" (une classe par type de widget doté d'une
	/// table WidgetColors dans UiTheme) — appelé une fois à la construction.
	/// Ces classes sont toujours en tête de UiClassList (cf. Spawn()) : la
	/// cascade les surcharge en priorité la plus basse.
	void RegisterRootClasses();
};

} // namespace ui
