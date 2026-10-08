#pragma once
/**
 * ui::components — composants ECS de l'interface utilisateur.
 *
 * Chaque widget est une entité ECS ordinaire (ecs::ecs::Entity) portant un UiRect
 * plus un composant de widget (UiPanel, UiLabel, UiButton, ...). Les éléments
 * forment un arbre via UiParent/UiChildren ; la passe de layout résout les
 * rectangles absolus et le clipping récursif (les enfants scrollent dans —
 * et sont découpés par — leurs parents).
 *
 * Inspiré de voxel/crates/engine_ui (components.rs) et SDL3pp_ui_v2
 * (UIComponents.h), adapté à l'ECS archétype de ce projet et rendu via
 * sdl3::Renderer.
 */
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <span>
#include <vector>

#include "../core/core.hpp"
#include "../ecs/ecs.hpp"
#include "../math/math.hpp"
#include "../sdl3/sdl3.hpp"

namespace sdl3 {
	class Renderer;
} // namespace sdl3

namespace ui {

// ============================================================================
// Dimension — longueur résoluble (px, % parent, % racine, auto, grow)
// ============================================================================

enum class DimUnit : uint8_t {
	PX,   ///< pixels absolus
	PCT,  ///< % de la taille de contenu du parent sur cet axe
	RPCT, ///< % de la taille de la racine/fenêtre sur cet axe
	EM,   ///< multiple de la taille de police effective du widget lui-même
	PEM,  ///< multiple de la taille de police effective du parent
	REM,  ///< multiple de la taille de police racine (LayoutSystem::rootFontSize)
	AUTO, ///< rétrécit à la taille intrinsèque du contenu
	GROW, ///< prend une part de l'espace restant sur l'axe principal (poids)
};

/// Bases de résolution des unités typographiques (Em/Pem/Rem). La taille
/// "effective" d'un widget est sa fontSize propre (UiLabel/UiButton/UiInput)
/// ou, à défaut, celle héritée de son parent — la racine héritant de
/// LayoutSystem::rootFontSize.
struct EmBases {
	float em = 0.f;  ///< police effective du widget
	float pem = 0.f; ///< police effective du parent
	float rem = 0.f; ///< police racine
	/// Échelle de contenu composée (produit multiplicatif de tous les
	/// ancêtres, cf. `prop::InnerZoom` — styles.hpp) — DOIT valoir 1.f par
	/// défaut : c'est le facteur par lequel `Dimension::Resolve()` multiplie
	/// CHAQUE dimension résolue (cf. plus bas), donc 0.f réduirait tout le
	/// layout à rien. Sert le node-graph editor (contenu d'un nœud zoomable
	/// indépendamment de la boîte du nœud) mais générique : n'importe quel
	/// sous-arbre peut poser `prop::InnerZoom` pour se redimensionner en bloc
	/// (paddings/gaps/tailles fixes compris, pas seulement le texte).
	float scale = 1.f;
};

struct Dimension {
	float value = 0.f;
	DimUnit unit = DimUnit::AUTO;
	float off = 0.f; ///< décalage fixe en pixels ajouté après résolution

	[[nodiscard]] static constexpr Dimension Px(float v) { return {v, DimUnit::PX, 0.f}; }
	[[nodiscard]] static constexpr Dimension Pct(float v) { return {v, DimUnit::PCT, 0.f}; }
	[[nodiscard]] static constexpr Dimension Rpct(float v) { return {v, DimUnit::RPCT, 0.f}; }
	[[nodiscard]] static constexpr Dimension Em(float v) { return {v, DimUnit::EM, 0.f}; }
	[[nodiscard]] static constexpr Dimension Pem(float v) { return {v, DimUnit::PEM, 0.f}; }
	[[nodiscard]] static constexpr Dimension Rem(float v) { return {v, DimUnit::REM, 0.f}; }
	[[nodiscard]] static constexpr Dimension Auto() { return {0.f, DimUnit::AUTO, 0.f}; }
	[[nodiscard]] static constexpr Dimension Grow(float weight = 1.f) { return {weight, DimUnit::GROW, 0.f}; }

	/// Ajoute un décalage fixe en pixels (ex: Dimension::Pct(50).Plus(-10)).
	[[nodiscard]] constexpr Dimension Plus(float o) const { return {value, unit, o}; }

	[[nodiscard]] constexpr bool IsAuto() const noexcept { return unit == DimUnit::AUTO; }
	[[nodiscard]] constexpr bool IsGrow() const noexcept { return unit == DimUnit::GROW; }

	/// Résout en pixels. `parent` et `root` sont les dimensions de l'axe
	/// concerné ; `fonts` porte les bases Em/Pem/Rem ET l'échelle de contenu
	/// composée (`fonts.scale`, cf. EmBases — node-graph inner-zoom et tout
	/// futur sous-arbre mis à l'échelle) — appliquée en dernier, sur le
	/// résultat COMPLET (base + off), pour qu'un `.Plus(-32.f)` reste
	/// proportionnellement cohérent avec le reste du sous-arbre plutôt que
	/// de rester à taille fixe pendant que tout l'entoure change d'échelle.
	/// `fonts.scale` vaut 1.f par défaut (EmBases{}) : aucun changement de
	/// comportement pour un sous-arbre qui ne pose jamais `prop::InnerZoom`.
	[[nodiscard]] constexpr float Resolve(float parent, float root, EmBases fonts = {}) const noexcept {
		float base = 0.f;
		switch (unit) {
		case DimUnit::PX:
			base = value;
			break;
		case DimUnit::PCT:
			base = value / 100.f * parent;
			break;
		case DimUnit::RPCT:
			base = value / 100.f * root;
			break;
		case DimUnit::EM:
			base = value * fonts.em;
			break;
		case DimUnit::PEM:
			base = value * fonts.pem;
			break;
		case DimUnit::REM:
			base = value * fonts.rem;
			break;
		case DimUnit::AUTO:
		case DimUnit::GROW:
			base = 0.f;
			break;
		}
		return (base + off) * fonts.scale;
	}
};

// ============================================================================
// Anchor — point de référence pour le positionnement absolu
// ============================================================================

enum class Anchor : uint8_t {
	TopLeft,
	Top, // Top Middle
	TopRight,
	CenterLeft,
	Center,
	CenterRight,
	BottomLeft,
	Bottom, // Bottom Middle
	BottomRight,
};

[[nodiscard]] constexpr sdl3::FPoint AnchorFactors(Anchor a) noexcept {
	switch (a) {
		case Anchor::TopLeft:     return {0.f, 0.f};
		case Anchor::Top:         return {0.5f, 0.f};
		case Anchor::TopRight:    return {1.f, 0.f};
		case Anchor::CenterLeft:  return {0.f, 0.5f};
		case Anchor::Center:      return {0.5f, 0.5f};
		case Anchor::CenterRight: return {1.f, 0.5f};
		case Anchor::BottomLeft:  return {0.f, 1.f};
		case Anchor::Bottom:      return {0.5f, 1.f};
		case Anchor::BottomRight: return {1.f, 1.f};
	}
	return {0.f, 0.f};
}

// ============================================================================
// Enums de layout
// ============================================================================

enum class LayoutDir : uint8_t { Row, Column };
enum class Justify : uint8_t { Start, Center, End, SpaceBetween };
enum class CrossAlign : uint8_t { Start, Center, End, Stretch };
/// Alignement du texte dans sa boîte. `Justify` n'a de sens qu'avec le retour
/// automatique à la ligne (cf. TextOverflow::WRAP) : l'espace restant y est
/// réparti ENTRE LES MOTS de chaque ligne, sauf la dernière — la justifier
/// aussi étirerait un mot isolé sur toute la largeur.
enum class TextAlign : uint8_t { Left, Center, Right, Justify };
enum class Orientation : uint8_t { Horizontal, Vertical };

/// Mode d'attache au parent (cf. CSS position). Contrôle à la fois le calcul
/// de layout (participation au flow) et — pour Fixed — le passage de dessin :
/// Relative : participe au flow normal du parent (comportement par défaut).
/// Absolute : sorti du flow, positionné (ancre + offset) dans la boîte de
///   contenu du parent ; dessiné à sa place normale dans l'arbre (toujours
///   sous les frères/soeurs dessinés après lui).
/// Fixed    : comme Absolute pour le calcul de position, mais sorti de
///   l'arbre de dessin normal — RenderSystem le reporte dans une passe
///   dédiée après tout le reste (clip réinitialisé à la fenêtre entière),
///   pour flotter au premier plan indépendamment de sa position dans la
///   hiérarchie (menus déroulants, HUD, panneaux flottants...).
enum class AttachLayout : uint8_t { RELATIVE, ABSOLUTE, FIXED };

// ============================================================================
// UiRect — boîte d'un élément (ancre + offset + taille + scroll)
// ============================================================================

struct UiRect {
	Anchor anchor = Anchor::TopLeft;
	sdl3::FPoint offset{0.f, 0.f};
	sdl3::FPoint size{0.f, 0.f};
	/// Taille logique du contenu (0 sur un axe = identique à size, non scrollable).
	sdl3::FPoint content{0.f, 0.f};
	/// Décalage de scroll courant, borné à [0, maxScroll].
	sdl3::FPoint scroll{0.f, 0.f};
	/// Si vrai, les enfants sont découpés à la boîte visible de cet élément.
	bool clipContent = false;
	/// Place réservée par les barres de défilement automatiques : `x` =
	/// largeur de la barre verticale (bord droit), `y` = hauteur de la barre
	/// horizontale (bord bas). Tenue à jour par UpdateGutter() — la barre
	/// occupe alors sa propre bande, comme un widget à part entière, au lieu
	/// de recouvrir le contenu.
	sdl3::FPoint gutter{0.f, 0.f};

	/// Épaisseur d'une barre de défilement automatique (px).
	static constexpr float SCROLLBAR_THICKNESS = 8.f;
	/// Débordement en deçà duquel aucune barre n'apparaît : les arrondis de
	/// placement (fractions de pixel) ne doivent pas faire surgir une barre
	/// sur un conteneur qui, visuellement, ne déborde pas.
	static constexpr float OVERFLOW_EPSILON = 1.f;

	/// Partie visible de la boîte, barres de défilement exclues.
	[[nodiscard]] sdl3::FPoint ViewSize() const noexcept;
	[[nodiscard]] sdl3::FPoint ContentSize() const noexcept;
	[[nodiscard]] sdl3::FPoint MaxScroll() const noexcept;
	/// Barres nécessaires pour un contenu `c` dans une boîte `box` : une barre
	/// verticale réduit la largeur visible, ce qui peut à son tour rendre la
	/// barre horizontale nécessaire (et réciproquement).
	[[nodiscard]] static sdl3::FPoint GutterFor(sdl3::FPoint c, sdl3::FPoint box) noexcept;
	/// Recalcule `gutter` depuis `content`/`size` ; vrai s'il a changé.
	bool UpdateGutter() noexcept;
	void ClampScroll() noexcept;

	/// Résout en rectangle absolu dans la boîte [origin, origin+(w,h)] : le coin
	/// correspondant de l'élément s'aligne sur le point d'ancrage.
	[[nodiscard]] sdl3::FRect ResolveIn(sdl3::FPoint origin, float w, float h) const noexcept;
};

// ============================================================================
// Hiérarchie
// ============================================================================

/// Lie un élément à son parent. Sans lui, l'élément est une racine positionnée
/// contre la fenêtre.
struct UiParent {
	ecs::Entity parent{};
};

/// Liste ordonnée des enfants — maintenue par la factory (setParent). Évite de
/// reconstruire l'arbre à chaque frame (clé de la performance du layout).
struct UiChildren {
	std::vector<ecs::Entity> list;
};

/// Marqueur : l'élément (et son sous-arbre) est ignoré par layout/input/render.
struct UiHidden {};

/// Marqueur : l'élément (et son sous-arbre) est rendu grisé et ignore l'input.
struct UiDisabled {};

/// Marqueur : l'élément est TRANSPARENT au pointeur — équivalent du
/// `pointer-events: none` de CSS. Il est dessiné normalement, mais le
/// hit-test de premier plan (cf. HitTestIndex, plus bas) ne le retient
/// jamais comme cible : c'est le widget qui se trouve DERRIÈRE lui qui reçoit
/// survol et clics. Contrairement à UiHidden/UiDisabled, le marqueur n'est PAS
/// récursif — ses enfants restent cliquables (poser une étiquette décorative
/// par-dessus un bouton sans lui voler ses évènements, laisser un voile ou un
/// cadre purement visuel au-dessus d'un panneau interactif...).
struct UiPointerThrough {};

/// Nom de débogage / recherche (optionnel).
struct UiName {
	String name;
};

// ============================================================================
// UiComputed — résultats de la passe de layout
// ============================================================================

struct UiComputed {
	sdl3::FRect screen{};         ///< rectangle absolu à l'écran
	sdl3::FRect clip{};           ///< région de dessin (intersection des clips ancêtres)
	sdl3::FRect childClip{};      ///< région passée aux enfants
	sdl3::FPoint contentOrigin{}; ///< coin haut-gauche de la zone de contenu (écran - scroll)
	sdl3::FPoint measured{};      ///< taille intrinsèque issue de la passe de mesure
};

// ============================================================================
// UiFlow / UiItem — layout automatique (flexbox-like)
// ============================================================================

/// Attaché à un conteneur : dispose ses enfants automatiquement.
struct UiFlow {
	LayoutDir dir = LayoutDir::Column;
	float gap = 6.f;
	math::Sides padding = math::Sides(8.f);
	Justify justify = Justify::Start;
	CrossAlign align = CrossAlign::Stretch;
};

/// Propriétés de layout par-enfant. Sans UiItem, l'enfant est positionné en
/// absolu via son UiRect (ancre + offset).
struct UiItem {
	AttachLayout attach = AttachLayout::RELATIVE;
	Dimension width = Dimension::Auto();
	Dimension height = Dimension::Auto();
	math::Sides margin{};
	Option<CrossAlign> alignSelf = NONE; ///< surcharge l'alignement du parent
	Option<float> minWidth = NONE, maxWidth = NONE;
	Option<float> minHeight = NONE, maxHeight = NONE;
};

// ============================================================================
// WidgetColors — couleurs d'un widget selon son état interactif
// ============================================================================

struct WidgetColors {
	sdl3::FColor bgNormal;			///< fond au repos / piste
	sdl3::FColor bgHovered;			///< fond survolé ; inputs : couleur du placeholder
	sdl3::FColor bgPressed;			///< fond pressé/drag ; inputs : bordure de focus
	sdl3::FColor bgChecked;			///< couleur coché / actif / rempli
	sdl3::FColor bgFocus;			///< fond survolé / focus

	sdl3::FColor borderNormal;		///< contour au repos
	sdl3::FColor borderHovered;
	sdl3::FColor borderPressed;
	sdl3::FColor borderChecked;
	sdl3::FColor borderFocus;		///< contour survolé / focus

	sdl3::FColor textNormal;		///< couleur du texte
	sdl3::FColor textHovered;
	sdl3::FColor textPressed;
	sdl3::FColor textHighlighted;
};

// ============================================================================
// Composants de widgets
// ============================================================================

/// Rectangle rempli, optionnellement bordé/arrondi, avec dégradé vertical
/// optionnel. Marqueur pur : couleur/bordure/radius/dégradé viennent tous du
/// style résolu (cf. styles.hpp — classes "root-panel" + cascade).
struct UiPanel {};

/// Alignement du texte piloté par le style résolu (text-align, héritable) —
/// cf. styles.hpp `prop::TextAlignProp`.
struct UiLabel {
	String text;
};

// ============================================================================
// Débordement du texte
// ============================================================================

/**
 * Que faire d'une chaîne plus large que le widget qui l'affiche.
 *
 * Dans tous les modes, un widget dont la largeur n'est PAS contrainte
 * (ni `W(...)`, ni `MaxSize`, ni étirement par son parent) s'élargit jusqu'au
 * texte : le débordement ne se pose que lorsqu'une contrainte existe.
 */
enum class TextOverflow : uint8_t {
	/// Rogné net au bord du widget (comportement historique).
	CLIP,
	/// Rogné, avec « … » collé à la fin de ce qui tient.
	ELLIPSIS,
	/// Barre de défilement horizontale (la machinerie existante d'
	/// UiRect::content/scroll, donc molette et pouce inclus).
	SCROLL,
	/// Retour automatique à la ligne. Le widget GRANDIT en hauteur ; si sa
	/// hauteur est contrainte (`H(...)`/`MaxSize`), une barre verticale
	/// apparaît, là encore par la machinerie existante.
	WRAP,
	/// Le texte défile tout seul, aller-retour, avec une pause aux extrémités.
	MARQUEE,
};

/// Mode de débordement d'un widget textuel (absent = TextOverflow::CLIP) et
/// état d'animation du mode MARQUEE.
struct UiTextOverflow {
	TextOverflow mode = TextOverflow::CLIP;
	/// MARQUEE : vitesse en pixels par seconde.
	float speed = 40.f;
	/// MARQUEE : temps d'arrêt à chaque extrémité, en secondes.
	float pause = 1.f;
	/// Largeur du texte, en pixels — écrite par LayoutSystem, qui dispose de
	/// la VRAIE mesure (police comprise). `InputSystem::Tick` en a besoin pour
	/// connaître l'amplitude du défilement, et n'a, lui, aucune police sous la
	/// main : la recalculer là donnerait une amplitude fausse dès que la
	/// mesure réelle s'écarte de l'heuristique.
	float textWidth = 0.f;
	// ── État, entretenu par InputSystem::Tick ────────────────────────────────
	float offset = 0.f;  ///< décalage courant du texte, en pixels (>= 0)
	float hold = 0.f;    ///< temps de pause restant
	bool forward = true; ///< sens de défilement courant
};

/// Mode de débordement de `e` (CLIP par défaut).
[[nodiscard]] TextOverflow TextOverflowOf(const ecs::ArchetypeRegistry &world, ecs::Entity e) noexcept;

struct UiButton {
	String text;
	// État (géré par InputSystem)
	bool hovered = false;
	bool pressed = false;
	bool clicked = false; ///< vrai pendant la frame du clic (event flag)
};

struct UiToggle {
	bool checked = false;
	float animT = 0.f; ///< position animée du bouton [0,1] (rendu uniquement)
	bool hovered = false;
};

struct UiCheckbox {
	bool checked = false;
	bool hovered = false;
};

struct UiSlider {
	float min = 0.f, max = 1.f, value = 0.f;
	float step = 0.f; ///< pas de quantification (0 = continu)
	Orientation orient = Orientation::Horizontal;
	bool dragging = false;
	bool hovered = false;

	[[nodiscard]] float Normalized() const noexcept { return (max > min) ? (value - min) / (max - min) : 0.f; }
	/// Quantifie `v` sur `step` puis borne à [min, max].
	[[nodiscard]] float Snap(float v) const noexcept;
};

/// `min/max/value` structurels ; track/fill/radius viennent du style résolu
/// ("root-progress" mappe Bg→track, BgChecked→fill — cf. UiFactory).
struct UiProgress {
	float min = 0.f, max = 1.f, value = 0.f;

	[[nodiscard]] float Normalized() const noexcept;
};

struct UiSeparator {
	Orientation orient = Orientation::Horizontal;
	float thickness = 1.f;
};

/// Contrôle les interactions permises sur UiInput/UiInputArea. Le texte est
/// TOUJOURS affiché (il n'y a pas de mode "invisible") ; chaque niveau
/// ajoute une capacité par rapport à ReadOnly : édition (Write), copie
/// (Copy, indépendante de Write — utile pour une visionneuse en lecture
/// seule mais copiable, ex. le panneau de logs), puis collage (Paste,
/// implique toujours Write). Voir ioCanWrite/ioCanCopy/ioCanPaste.
enum class IOMode : uint8_t {
	READ_ONLY,              ///< affichage seul : ni édition, ni copie
	READ_AND_COPY_ONLY,       ///< lecture seule + copie (Ctrl+C) — ex : visionneuse de logs
	READ_AND_WRITE_ONLY,      ///< édition possible, copier/coller désactivés
	READ_WRITE_AND_COPY_ONLY,  ///< édition + copie, coller désactivé
	READ_WRITE_COPY_AND_PASTE, ///< tout autorisé — comportement d'un champ de saisie classique
};
[[nodiscard]] constexpr bool IoCanWrite(IOMode m) noexcept {
	return m == IOMode::READ_AND_WRITE_ONLY || m == IOMode::READ_WRITE_AND_COPY_ONLY || m == IOMode::READ_WRITE_COPY_AND_PASTE;
}
[[nodiscard]] constexpr bool IoCanCopy(IOMode m) noexcept {
	return m == IOMode::READ_AND_COPY_ONLY || m == IOMode::READ_WRITE_AND_COPY_ONLY || m == IOMode::READ_WRITE_COPY_AND_PASTE;
}
[[nodiscard]] constexpr bool IoCanPaste(IOMode m) noexcept { return m == IOMode::READ_WRITE_COPY_AND_PASTE; }

/// Champ de saisie mono-ligne (logique) : Entrée soumet (onSubmit), jamais
/// n'insère de retour à la ligne. Le texte est enveloppé (word-wrap) dans la
/// largeur du widget ; si le nombre de lignes enveloppées dépasse sa hauteur,
/// un défilement vertical automatique apparaît (réutilise le mécanisme
/// générique de UiRect.content/scroll/clipContent — cf. InputSystem::Tick()
/// pour le calcul de contenu et RenderSystem pour le rendu enveloppé).
/// Curseur déplaçable (flèches/Origine/Fin, souris) et sélection (Maj+flèche,
/// glisser-souris) — voir InputSystem::moveCursor/hitTestOffset.
struct UiInput {
	String text;
	String placeholder;
	IOMode ioMode = IOMode::READ_WRITE_COPY_AND_PASTE;
	size_t maxLen = 256;
	// État (géré par InputSystem)
	bool focused = false;
	bool hovered = false;
	float blink = 0.f;
	bool changed = false;   ///< event flag : texte modifié cette frame
	bool submitted = false; ///< event flag : Entrée pressée cette frame
	/// Position du curseur (index en octets dans `text`, toujours sur une
	/// frontière de point de code UTF-8).
	size_t cursor = 0;
	/// Point fixe de la sélection ; sélection vide quand == cursor.
	size_t selectionAnchor = 0;
};

/// Éditeur de texte multi-ligne : Entrée insère un retour à la ligne (si
/// l'édition est permise) plutôt que de soumettre — pas de word-wrap (les
/// lignes sont préservées telles quelles), défilement automatique vertical
/// ET horizontal si le contenu déborde. Curseur déplaçable et sélection
/// (comme UiInput, y compris Haut/Bas pour naviguer entre lignes réelles) —
/// utile pour saisir un texte multi-ligne aussi bien que pour parcourir et
/// copier une portion d'une visionneuse de logs en lecture seule.
/// Morceau coloré d'une ligne de texte : octets `[begin, end)` de la ligne
/// TELLE QU'AFFICHÉE (tabulations développées en espaces, cf.
/// UiInputArea::highlighter), dessinés en `color`. Les octets non couverts
/// gardent la couleur de texte du widget.
struct UiTextSpan {
	size_t begin = 0;
	size_t end = 0;
	sdl3::FColor color = sdl3::FColor::WHITE();
};

/// Coloration syntaxique d'UNE ligne : remplit `out` (vidé avant l'appel)
/// de morceaux triés, sans chevauchement. Sans état d'une ligne à l'autre —
/// suffisant pour des langages dont les jetons ne traversent pas les lignes
/// (script de l'éditeur, JSON, journaux), et c'est ce qui permet de ne
/// colorer QUE les lignes visibles.
using UiSyntaxHighlighter = std::function<void(const String &line, std::vector<UiTextSpan> &out)>;

struct UiInputArea {
	String text;
	String placeholder;
	IOMode ioMode = IOMode::READ_WRITE_COPY_AND_PASTE;
	size_t maxLen = 1 << 20; // 1 Mio de texte, largement au-dessus d'un usage journal

	// ── Mode éditeur de code (tout désactivé par défaut) ─────────────────────
	/// Gouttière de numéros de ligne à gauche du texte.
	bool lineNumbers = false;
	/// Police à chasse fixe (cf. RenderSystem::RegisterMonospaceFont) : sans
	/// elle, les colonnes d'un code aligné ne tombent pas les unes sous les
	/// autres, et le curseur (placé sur une grille de cellules) dérive du
	/// texte dessiné. Sans police enregistrée, retombe sur la police normale.
	bool monospace = false;
	/// Surligne la ligne du curseur quand le champ a le focus.
	bool highlightCurrentLine = false;
	/// Suivre la FIN du texte quand de nouvelles lignes arrivent (journal,
	/// console), tant que l'utilisateur n'est pas remonté le consulter. Un
	/// document de code, lui, s'ouvre en haut et ne bouge pas tout seul.
	bool followTail = true;
	/// Coloration syntaxique (cf. UiSyntaxHighlighter) — vide = une couleur.
	UiSyntaxHighlighter highlighter;
	/// Largeur d'une cellule de caractère MESURÉE sur la police réellement
	/// dessinée (écrite par RenderSystem, lue par la saisie et la mise en
	/// page) ; 0 = pas encore dessiné, approximation CharWidthApprox.
	float cellWidth = 0.f;
	// État (géré par InputSystem)
	bool focused = false;
	bool hovered = false;
	float blink = 0.f;
	bool changed = false;
	/// Vrai si le défilement vertical était déjà au maximum (bas) avant la
	/// dernière mise à jour de contenu — permet à InputSystem de ne "suivre"
	/// automatiquement le nouveau contenu (ex. nouvelles lignes de log) que
	/// si l'utilisateur n'a pas remonté manuellement consulter l'historique.
	bool stickToBottom = true;
	size_t cursor = 0;
	size_t selectionAnchor = 0;
};

/// Ajustement d'une image dans son rectangle (cf. CSS object-fit).
enum class ImageFit : uint8_t {
	FILL,    ///< étire pour couvrir tout le rectangle (ratio ignoré)
	CONTAIN, ///< tient entièrement dans le rectangle (ratio conservé)
	COVER,   ///< couvre tout le rectangle en rognant (ratio conservé)
	NONE,    ///< taille d'origine, centrée, rognée si plus grande
};

/// Image texturée — `textureKey` est résolue dans le pool du RenderSystem.
struct UiImage {
	String textureKey;
	ImageFit fit = ImageFit::FILL;
};

/// Glyphe d'icône (Cupertino/Material...) : un seul point de code affiché
/// avec une police d'icônes dédiée, distincte de la police de texte
/// courante. `glyph` est la séquence UTF-8 déjà encodée du point de code
/// (cf. ui::Glyphs::ToUtf8) ; `font` est le nom de famille sous lequel la
/// police correspondante a été enregistrée via RenderSystem::RegisterFont
/// (cf. ui::Glyphs::FontFamily). Volontairement découplé de glyphs.hpp : ce
/// composant ECS ne connaît que des chaînes déjà résolues — le typage fort
/// par `enum class` (CupertinoIcons/MaterialIcons) vit dans UiFactory::Icon.
/// La teinte vient du style résolu (text-color, cf. "root-icon").
struct UiIcon {
	String glyph;
	String font;
	float size = 16.f;
};

/// Bouton radio : un seul coché à la fois parmi ceux qui partagent `group`.
struct UiRadio {
	String group;
	String text; ///< libellé affiché à droite du cercle
	bool checked = false;
	bool hovered = false;
};

/// Barre de défilement autonome (cf. UiRect::clipContent pour le scroll
/// intégré des conteneurs) : représente `viewSize` visible sur `contentSize`
/// total, `offset` dans [0, contentSize - viewSize].
struct UiScrollBar {
	float contentSize = 100.f;
	float viewSize = 50.f;
	float offset = 0.f;
	Orientation orient = Orientation::Horizontal;
	bool dragging = false;
	bool hovered = false;

	[[nodiscard]] float MaxOffset() const noexcept { return sdl3::Max(0.f, contentSize - viewSize); }
	/// Fraction visible [0,1] (taille relative du pouce).
	[[nodiscard]] float ThumbRatio() const noexcept;
	[[nodiscard]] float Normalized() const noexcept;
};

/// Potentiomètre rotatif : drag vertical (ou molette) pour régler la valeur
/// dans [min, max] (par défaut [0,1], continu).
struct UiKnob {
	float min = 0.f, max = 1.f, value = 0.f;
	float step = 0.f; ///< pas de quantification (0 = continu)
	bool dragging = false;
	bool hovered = false;
	// État interne du drag (géré par InputSystem)
	float dragStartY = 0.f;
	float dragStartValue = 0.f;

	[[nodiscard]] float Normalized() const noexcept;
	[[nodiscard]] float Snap(float v) const noexcept;
};

/// Zone de dessin libre : le callback reçoit le Renderer et le rect écran
/// (le clip des ancêtres est déjà appliqué).
struct UiCanvas {
	std::function<void(sdl3::Renderer &, sdl3::FRect)> onDraw;
};

/// Preset shader appliqué au SOUS-ARBRE d'un widget (M23, cf.
/// ui/shader_effect.hpp pour le pipeline complet). Seuls 2 presets sont
/// fournis (BLUR volontairement écarté — un vrai flou multi-échantillons
/// n'a pas de valeur hand-computable simple à vérifier par pixel-readback
/// sans inspection visuelle, cf. rapport de tâche M23) :
///   - TINT_SHIFT : outColor.rgb = lerp(src.rgb, src.rgb * color.rgb, param)
///     (param = force [0,1]).
///   - GLOW       : halo radial — outColor.rgb = lerp(src.rgb, color.rgb,
///     smoothstep(param, 0.5, distance(uv, (0.5,0.5)))) (param = rayon
///     intérieur en UV, où le halo commence).
enum class UiShaderEffectKind : uint8_t { TINT_SHIFT, GLOW };

/// Composant PUREMENT DE DONNÉES — contrairement à UiViewport3D (M22, qui
/// porte directement son OffscreenTarget/displayTexture), AUCUN état GPU ne
/// vit ici : components.hpp ne dépend d'aucun header render3d::/sdl3::gpu/
/// sdl3::render (seule sdl3::FColor, déjà utilisée par UiColorSwatch ci-
/// dessus, est nécessaire), et cette entête doit le rester — c'est la base
/// de tout le graphe d'inclusion ui::. Tout l'état GPU-adjacent par widget
/// (texture cible sdl3::Renderer, GpuTexture source/sortie, texture affichée
/// finale) est donc centralisé PAR ENTITÉ dans ui::ShaderEffectSystem
/// (shader_effect.hpp), pas ici — divergence délibérée du précédent M22,
/// documentée dans le rapport de tâche M23.
struct UiShaderEffect {
	UiShaderEffectKind kind = UiShaderEffectKind::TINT_SHIFT;
	/// tintColor (TINT_SHIFT) ou glowColor (GLOW).
	sdl3::FColor color = sdl3::FColor::WHITE();
	/// force [0,1] (TINT_SHIFT) ou rayon intérieur UV (GLOW) — cf. l'énum ci-dessus.
	float param = 1.f;
};

/// Une catégorie à donner à une UiComboBox (UiFactory::Combo(categories),
/// UiComboBox::SetCategories) : un titre (vide = éléments au premier niveau
/// de la liste) et ses éléments.
struct ComboCategory {
	String title;
	std::vector<String> items;
};

/// Catégorie d'une UiComboBox : les éléments `items[first .. first+count)`.
struct UiComboGroup {
	String title;
	int first = 0;
	int count = 0;
};

/// Liste déroulante : la boîte affiche l'élément sélectionné, le clic ouvre
/// la liste en OVERLAY (dessinée après tout le reste, sans clip). onChange
/// reçoit l'index sélectionné (dans `items`).
///
/// Avec des catégories (`groups`, cf. UiFactory::Combo(categories)), la
/// liste déroulée montre d'abord les éléments hors catégorie, puis une ligne
/// par catégorie (titre + ▸) ; survoler une catégorie ouvre ses éléments à
/// DROITE, comme le sous-menu d'un Menu. Cliquer une catégorie ne ferme pas
/// la liste.
struct UiComboBox {
	std::vector<String> items;
	std::vector<UiComboGroup> groups; ///< vide : liste plate
	int selected = -1;
	float itemHeight = 26.f; ///< métrique de layout (dérivée de theme.fontSize à la construction)
	// État (géré par InputSystem)
	bool open = false;
	int hoveredItem = -1;
	int hoveredGroup = -1; ///< ligne de catégorie survolée
	int openGroup = -1;	   ///< catégorie dont le sous-menu est ouvert
	bool hovered = false;
	/// Bas de la zone de dessin (posé par RenderSystem) : un sous-menu qui
	/// déborderait remonte au-dessus. 0 = inconnu.
	float screenBottom = 0.f;

	/// Remplace éléments et catégories (index : catégories bout à bout) ;
	/// garde `selected` s'il reste valide, referme la liste.
	void SetCategories(std::vector<ComboCategory> categories);
	[[nodiscard]] bool Grouped() const noexcept { return !groups.empty(); }
	/// Éléments du premier niveau : tous (liste plate), ou ceux hors catégorie.
	[[nodiscard]] std::vector<int> TopItems() const;
	/// Lignes du premier niveau : éléments hors catégorie puis catégories.
	[[nodiscard]] int TopRows() const;
	/// Catégorie contenant l'élément `item`, ou -1.
	[[nodiscard]] int GroupOf(int item) const noexcept;
	/// Rectangle de la liste déroulée, sous la boîte `screen`.
	[[nodiscard]] sdl3::FRect DropdownRect(const sdl3::FRect &screen) const;
	/// Sous-menu de la catégorie `group`, à droite de sa ligne (vide si
	/// `group` n'est pas une catégorie).
	[[nodiscard]] sdl3::FRect SubmenuRect(const sdl3::FRect &screen, int group) const;
	/// Le point `p` est-il sur la liste déroulée ou le sous-menu ouvert ?
	[[nodiscard]] bool OverlayContains(const sdl3::FRect &screen, sdl3::FPoint p) const;
};

/// Liste sélectionnable à scroll interne (leaf : pas d'entités enfants).
/// onChange reçoit l'index sélectionné.
struct ListBox {
	std::vector<String> items;
	int selected = -1;
	float itemHeight = 24.f; ///< métrique de layout (dérivée de theme.fontSize à la construction)
	// État (géré par InputSystem)
	float scroll = 0.f;
	int hoveredItem = -1;
	bool hovered = false;

	[[nodiscard]] float ContentHeight() const noexcept { return itemHeight * float(items.size()); }
	[[nodiscard]] float MaxScroll(float viewH) const noexcept { return sdl3::Max(0.f, ContentHeight() - viewH); }
};

/// Section repliable : l'en-tête (titre + flèche) replie/déplie les enfants.
/// La factory réserve la hauteur d'en-tête via le padding haut du UiFlow.
/// onToggle reçoit le nouvel état `expanded`.
struct UiExpander {
	String title;
	bool expanded = true;
	float headerHeight = 28.f;
	bool hovered = false; ///< survol de l'en-tête uniquement
};

/// Conteneur à onglets : un enfant par onglet, seul l'actif est visible.
/// La barre d'onglets occupe le padding haut du UiFlow. onChange reçoit
/// l'index de l'onglet activé.
struct UiTabView {
	std::vector<String> tabs;
	int active = 0;
	float tabHeight = 30.f;
	int hoveredTab = -1;
};

/// Indicateur de chargement : arc tournant, animé par InputSystem. La
/// couleur du trait vient du style résolu (background-color).
struct UiSpinner {
	float speed = 360.f; ///< degrés/seconde
	float thickness = 3.f;
	float angle = 0.f; ///< état d'animation
};

/// Pastille de notification (pilule colorée + texte court).
struct UiBadge {
	String text;
};

/// Infobulle affichée près de la souris après un court délai de survol.
struct UiTooltip {
	String text;
};

/// Ordre de tri du pass overlay `AttachLayout::Fixed` (RenderSystem::Run()) —
/// par défaut 0 (ordre d'itération brut, comportement historique quand rien
/// ne l'utilise). Popups/sous-menus/modales l'utilisent pour s'empiler
/// correctement (un sous-menu doit se dessiner au-dessus de son menu parent).
struct UiOverlayLayer {
	int order = 0;
};

/// État d'un popup/modale (posé sur la racine de son contenu Fixed, cf.
/// UiFactory::Popup()/modal()). `trigger` : l'entité qui a ouvert ce popup
/// (bouton, item de menu...), mémorisée pour la fermeture au clic extérieur.
/// `modal` : bloque tout le reste de l'arbre tant qu'ouvert (cf. InputSystem
/// ::OpenModal/closeModal) — un popup non-modal se ferme juste au clic
/// extérieur ou à un nouveau clic sur son déclencheur.
struct UiPopupState {
	bool open = false;
	bool modal = false;
	ecs::Entity trigger{};
};

// ============================================================================
// Phase 3 — édition de valeurs (DragValue, ColorSwatch/ColorPicker)
// ============================================================================

/// Valeur numérique éditable façon ImGui::DragFloat : glisser horizontalement
/// modifie `value` proportionnellement à `speed` (px → valeur) ; un
/// DOUBLE-clic bascule en édition texte directe — saisie minimale (chiffres/
/// point/signe, retour arrière, Entrée valide, Échap annule), le curseur
/// restant toujours en fin de saisie (pas de navigation/sélection : un champ
/// numérique court n'en a pas l'usage, contrairement à UiInput qui reste la
/// brique générique pour du texte quelconque).
struct UiDragValue {
	float min = 0.f, max = 1.f, value = 0.f;
	float step = 0.f;  ///< pas de quantification (0 = continu)
	float speed = 1.f; ///< valeur ajoutée par pixel de glissement horizontal
	int decimals = 2;  ///< décimales affichées (cf. formatted())
	// État (géré par InputSystem)
	bool dragging = false;
	bool dragMoved = false;        ///< un vrai glisser a eu lieu cette pression
	bool dragWasDoubleClick = false;
	bool hovered = false;
	bool editing = false;
	String editText;
	float blink = 0.f;
	float dragStartX = 0.f;
	float dragStartValue = 0.f;

	[[nodiscard]] float Snap(float v) const noexcept;
	[[nodiscard]] String Formatted() const { return String::From(value, decimals); }
};

/// Pastille de couleur cliquable — affiche `color` (damier de transparence
/// sous l'alpha < 255), interaction identique à UiButton (hovered/pressed/
/// clicked → onClick) mais sans texte. Sert typiquement de déclencheur pour
/// un UiColorPicker en popup (cf. UiFactory::ColorSwatch/colorPicker).
struct UiColorSwatch {
	sdl3::FColor color = sdl3::FColor::WHITE();
	bool hovered = false;
	bool pressed = false;
	bool clicked = false;
};

/// Carré saturation/valeur d'un sélecteur HSV (cf. UiFactory::ColorPicker) :
/// glisser dedans modifie `s` (axe X) et `v` (axe Y, inversé — haut = 1). La
/// teinte `h` est réglée séparément par UiHueSlider et seulement LUE ici pour
/// teinter le dégradé au dessin (RenderSystem, 4 coins via renderGeometry —
/// pas de texture).
struct UiSVSquare {
	float h = 0.f, s = 1.f, v = 1.f;
	bool dragging = false;
	bool hovered = false;
};

/// Glissière de teinte [0,360) — piste arc-en-ciel à 6 segments dessinée par
/// RenderSystem (pas un UiSlider générique : dégradé multi-stops, pas une
/// simple interpolation à 2 couleurs).
struct UiHueSlider {
	float hue = 0.f;
	bool dragging = false;
	bool hovered = false;
};

/// Glissière alpha [0,1] — piste damier (transparence) + dégradé de
/// `baseColor` opaque → transparente, pilotée par le RGB courant du picker.
struct UiAlphaSlider {
	float alpha = 1.f;
	sdl3::FColor baseColor = sdl3::FColor::WHITE(); ///< RGB affiché (alpha du champ ignoré ici)
	bool dragging = false;
	bool hovered = false;
};

/// Composante Teinte/Saturation/Valeur d'une sdl3::FColor (cf. colorToHsv/hsvToColor
/// ci-dessous) — h en degrés [0,360), s/v normalisés [0,1].
struct HSV {
	float h = 0.f, s = 0.f, v = 0.f;
};

/// Convertit HSV → sdl3::FColor (composantes flottantes 0-1), `a` passé tel quel.
[[nodiscard]] sdl3::FColor HsvToColor(float h, float s, float v, float a = 1.f) noexcept;

/// Convertit sdl3::FColor (RGB) → HSV. L'alpha n'est pas représenté (cf. UiAlphaSlider).
[[nodiscard]] HSV ColorToHsv(sdl3::FColor c) noexcept;

/// Formate en "#RRGGBBAA" (majuscules).
[[nodiscard]] String ColorToHex(sdl3::FColor c);

/// Parse "#RRGGBB"/"#RRGGBBAA" (le '#' est optionnel) — `fallback` si la
/// longueur ne correspond à aucun des deux formats.
[[nodiscard]] sdl3::FColor HexToColor(const String &hex, sdl3::FColor fallback = sdl3::FColor::WHITE());

// ============================================================================
// Phase 4 — Selectable & TreeNode (sélection : cf. UiSelection/
// applySelectionClick/nearestSelectionAncestor, interaction.hpp)
// ============================================================================

/// Élément sélectionnable façon ImGui::Selectable — surbrillance PERSISTANTE
/// (contrairement à UiButton.pressed, momentané) pilotée par le UiSelection
/// du plus proche ANCÊTRE qui en porte un (cf. interaction.hpp
/// nearestSelectionAncestor) : ce widget n'a pas son propre état de
/// sélection, seulement un reflet en lecture pour le rendu (`selected`,
/// recalculé par InputSystem après un clic sur lui OU un de ses frères —
/// même motif deux-passes que les UiRadio d'un groupe). Conteneur de flow
/// (cf. UiFactory::Selectable()) : le contenu (texte, icône...) est un
/// enfant normal, pas un champ figé sur ce composant.
struct UiSelectable {
	int index = 0; ///< position dans le UiSelection de l'ancêtre (assignée par l'appelant, doit être unique)
	bool selected = false;
	bool hovered = false;
	/// Enfoncé sur la ligne : un relâchement dessus appelle `onClick` (comme
	/// un bouton) — c'est le geste d'une ligne d'arbre, de liste de scènes ou
	/// de dossiers, même hors de tout UiSelection.
	bool pressed = false;
};

/// Nœud d'arbre repliable ET sélectionnable (fusion de UiExpander +
/// UiSelectable) : l'EN-TÊTE (flèche + titre, hauteur `headerHeight`)
/// replie/déplie les enfants (typiquement d'autres UiTreeNode/UiSelectable,
/// formant l'arbre via UiChildren — cf. UiFactory::TreeNode()) ; un clic sur
/// l'en-tête HORS flèche applique la sélection comme UiSelectable, contre le
/// UiSelection du plus proche ancêtre. `depth` est informatif seulement (pas
/// utilisé pour l'indentation, gérée par le padding cumulé de la cascade de
/// conteneurs — cf. treeNode()) : utile à l'appelant pour son propre rendu.
struct UiTreeNode {
	String title;
	int index = 0; ///< position dans le UiSelection de l'ancêtre (feuilles ET nœuds partagent l'espace d'index)
	int depth = 0;
	bool expanded = true;
	bool selected = false;
	bool hovered = false;      ///< survol de l'en-tête (hors flèche)
	bool hoveredArrow = false; ///< survol de la flèche seule
	float headerHeight = 26.f;
	bool pressed = false; ///< enfoncé sur l'en-tête hors flèche (cf. UiSelectable::pressed)
};

// ============================================================================
// Phase 5 — MenuBar / Menu / MenuItem (popups Phase 1, positionnement
// dynamique via positionPopupBelow/positionPopupRightOf, systems.hpp)
// ============================================================================

/// Entrée de barre de menu (ex: "Fichier", "Édition") — cf.
/// UiFactory::MenuBar()/menu(). Le clic bascule l'ouverture de `menuPopup`
/// (positionné SOUS l'item à l'ouverture, cf. positionPopupBelow) ; la
/// fermeture au clic extérieur réutilise le mécanisme popup non-modal
/// générique de Phase 1, sans code supplémentaire.
struct UiMenuBarItem {
	String text;
	ecs::Entity menuPopup{}; ///< posé par UiFactory::Menu() à la construction
	bool hovered = false;
};

/// Entrée d'un menu déroulant (cf. UiFactory::MenuItem()/subMenu()). SANS
/// sous-menu (`hasSubmenu=false`) : le clic déclenche onClick puis ferme
/// toute la chaîne de popups ouverte (cf. closeMenuChain, systems.hpp).
/// AVEC sous-menu (`submenuPopup` valide) : le clic bascule l'ouverture de
/// celui-ci (positionné à DROITE de cet item, cf. positionPopupRightOf) au
/// lieu d'exécuter/fermer.
struct UiMenuItem {
	String text;
	String shortcut; ///< texte informatif aligné à droite (ex: "Ctrl+S") — pas de vrai raccourci clavier géré
	bool hasSubmenu = false;
	ecs::Entity submenuPopup{}; ///< posé par UiFactory::subMenu() à la construction
	bool hovered = false;
};

// ============================================================================
// Phase 6 — Table (colonnes redimensionnables/triables, multi-sélection,
// réordonnancement de lignes)
// ============================================================================

/// Colonne d'une UiTable — largeur en pixels (pas de layout auto : une table
/// gère ses colonnes elle-même, cf. UiTable ci-dessous, plutôt que de
/// composer un widget par cellule comme le reste du module).
struct UiTableColumn {
	String title;
	float width = 100.f;
	float minWidth = 40.f;
	bool sortable = true;
};

enum class SortOrder : uint8_t { NONE, ASCENDING, DESCENDING };

/// Table façon ImGui::Table — volontairement un widget AUTONOME qui dessine
/// ses propres en-têtes/cellules (comme ListBox/UiComboBox), PAS une
/// composition d'entités par cellule : une grille de 100 lignes × 5 colonnes
/// composée à l'ECS ferait 500+ entités pour un widget qui n'a besoin
/// d'aucune interactivité par cellule au-delà du texte — cf. la note Phase 6
/// dans project_ui_ecs_module.md pour la discussion complète. Réutilise
/// néanmoins les PRIMITIVES de Phase 2/4 quand c'est un vrai composant
/// partageable : `UiSelection` (posée en composant SÉPARÉ sur la même
/// entité, cf. UiFactory::Table()) pilote la sélection multiple exactement
/// comme Selectable/TreeNode ; `UiCallbacks::onReorder` notifie le
/// réordonnancement de lignes, même signature que UiReorderable. Le
/// redimensionnement de colonne et le glisser de ligne, en revanche,
/// réimplémentent l'ALGORITHME (pas le composant) de UiResizeHandle/
/// UiReorderable, adapté à des colonnes/lignes internes plutôt qu'à des
/// entités ECS indépendantes.
struct UiTable {
	std::vector<UiTableColumn> columns;
	std::vector<std::vector<String>> rows; ///< rows[i][col] — texte de cellule (Phase 6 : cellules texte seulement)
	float rowHeight = 26.f;
	float headerHeight = 30.f;
	bool reorderable = false; ///< glisser une ligne pour la déplacer (cf. UiCallbacks::onReorder)
	int sortColumn = -1;
	SortOrder sortOrder = SortOrder::NONE;
	// État (géré par InputSystem)
	float scroll = 0.f;
	int hoveredRow = -1;
	int hoveredColumn = -1;  ///< survol d'en-tête (tri)
	int resizingColumn = -1; ///< -1 = aucun redimensionnement en cours
	float resizeStartMouseX = 0.f;
	float resizeStartWidth = 0.f;
	int reorderRow = -1; ///< -1 = aucun glisser de ligne en cours
	float reorderStartMouseY = 0.f;
	bool reorderMoved = false;
	int reorderTargetRow = -1;

	[[nodiscard]] float TotalWidth() const noexcept;
	/// Décalage X cumulé (depuis la gauche de la table) du début de la colonne `col`.
	[[nodiscard]] float ColumnX(int col) const noexcept;
	[[nodiscard]] float ContentHeight() const noexcept { return rowHeight * float(rows.size()); }
	[[nodiscard]] float MaxScroll(float bodyH) const noexcept { return sdl3::Max(0.f, ContentHeight() - bodyH); }
};

// ============================================================================
// Phase 8 — Splitter (cf. interaction.hpp UiResizeHandle, réutilisé tel
// quel par UiFactory::Splitter() — pas de nouveau composant ici) & DatePicker
// ============================================================================

[[nodiscard]] constexpr bool IsLeapYear(int y) noexcept { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

[[nodiscard]] constexpr int DaysInMonth(int year, int month) noexcept {
	constexpr int K_DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int m = ((month - 1) % 12 + 12) % 12;
	if (m == 1 && IsLeapYear(year))
		return 29;
	return K_DAYS[m];
}

/// Jour de la semaine (0=Lundi..6=Dimanche) du 1er jour de `month`/`year` —
/// congruence de Zeller, reconvertie de sa base native (0=Samedi) vers une
/// semaine ISO commençant le lundi.
[[nodiscard]] constexpr int FirstWeekdayOfMonth(int year, int month) noexcept {
	int y = year, m = month;
	if (m < 3) {
		m += 12;
		y -= 1;
	}
	int k = y % 100, j = y / 100;
	int h = (1 + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
	return (h + 5) % 7;
}

[[nodiscard]] constexpr const char *MonthNameFr(int month) noexcept {
	constexpr const char *K_NAMES[12] = {"Janvier", "Février",   "Mars",      "Avril",   "Mai",      "Juin",
										"Juillet", "Août",      "Septembre", "Octobre", "Novembre", "Décembre"};
	if (month < 1 || month > 12)
		return "";
	return K_NAMES[month - 1];
}

/// Grille calendrier mensuel (widget AUTONOME, même philosophie que
/// UiTable/Plot : pas d'entité par case). L'en-tête (flèches précédent/
/// suivant + "Mois Année") change de mois ; un clic sur une case pose
/// `selectedDay` et notifie `onDaySelected` DIRECTEMENT (pas via
/// UiCallbacks::onChange, dont la signature `void(float)` ne porterait
/// qu'une seule valeur — même idiome que UiCanvas::onDraw, un callback posé
/// sur le composant lui-même quand la signature générique ne convient pas).
struct UiCalendar {
	int year = 2026, month = 1; ///< month : 1-12
	int selectedDay = -1;       ///< -1 = aucune sélection
	std::function<void(int year, int month, int day)> onDaySelected;
	// État (géré par InputSystem)
	int hoveredDay = -1;
	bool hoveredPrevArrow = false, hoveredNextArrow = false;
};

// ============================================================================
// UiCallbacks — réactions applicatives aux événements
// ============================================================================

struct UiCallbacks {
	std::function<void()> onClick;
	std::function<void(float)> onChange; ///< sliders / knobs
	std::function<void(bool)> onToggle;  ///< toggle / checkbox / radio
	std::function<void(float)> onScroll; ///< scrollbars : nouvel offset
	std::function<void(const String &)> onTextChange;
	std::function<void(const String &)> onSubmit;       ///< input : Entrée
	std::function<void(int, int)> onReorder;             ///< UiReorderable (posé sur le CONTENEUR) : (fromIndex, toIndex)
	/// UiDropTarget (posé sur la CIBLE) : appelé au relâchement d'un glissé
	/// compatible, avec l'identifiant transporté par la source (cf.
	/// UiDragPayload, interaction.hpp).
	std::function<void(int64_t)> onDrop;
	/// Bouton ou ligne sélectionnable : DOUBLE clic (second appui rapproché,
	/// selon le système). Le premier appui a déjà donné son `onClick`.
	std::function<void()> onDoubleClick;
	/// UiDragPayload (posé sur la SOURCE) : le glissé vient de commencer (seuil
	/// de déplacement franchi) — moment d'ajuster `label`/`count` du fantôme
	/// selon ce qui part réellement (la sélection entière, par exemple).
	std::function<void()> onDragStart;
	/// Clic DROIT sur ce widget (ou sur un descendant qui n'a pas le sien) :
	/// position écran du pointeur. C'est le point d'entrée d'un menu
	/// contextuel (cf. UiFactory::ContextMenu / ui::OpenPopupAt) — seul le
	/// plus proche porteur de la chaîne de premier plan est notifié, comme
	/// dans tout gestionnaire de fenêtres.
	std::function<void(float, float)> onContextMenu;
};

// ============================================================================
// Helpers de hiérarchie
// ============================================================================

/// Fait de `child` un enfant de `parent` (maintient UiParent + UiChildren).
void SetParent(ecs::ArchetypeRegistry &world, ecs::Entity child, ecs::Entity parent);

/// Détruit `e` et tout son sous-arbre.
void DespawnTree(ecs::ArchetypeRegistry &world, ecs::Entity e);

/// Recherche la première entité UI portant ce nom (UiName).
[[nodiscard]] Option<ecs::Entity> FindByName(ecs::ArchetypeRegistry &world, const String &name);

/// Vrai si l'élément ou l'un de ses ancêtres porte UiHidden.
[[nodiscard]] bool IsHiddenRecursive(ecs::ArchetypeRegistry &world, ecs::Entity e);

/// Vrai si l'élément ou l'un de ses ancêtres porte UiDisabled.
[[nodiscard]] bool IsDisabledRecursive(ecs::ArchetypeRegistry &world, ecs::Entity e);

/// Vrai si `e` est `ancestor` lui-même ou un de ses descendants (remonte via
/// UiParent). Utilisé pour bloquer l'interaction en dehors d'une modale
/// ouverte (cf. InputSystem::dispatch) — un seul test par widget candidat.
[[nodiscard]] bool IsDescendantOrSelf(ecs::ArchetypeRegistry &world, ecs::Entity e, ecs::Entity ancestor);

// ============================================================================
// Hit-test de PREMIER PLAN (z-order)
// ============================================================================
//
// Quand plusieurs widgets se superposent (item de menu au-dessus d'un bouton
// de barre d'outils, popup par-dessus un panneau, HUD flottant au-dessus
// d'une liste...), un seul doit réagir : celui que l'utilisateur VOIT, donc
// le dernier dessiné. HitTestIndex donne cette entité en rejouant EXACTEMENT
// l'ordre de dessin de RenderSystem::Run :
//
//   1. les listes déroulantes des UiComboBox ouverts (dessinées en dernier,
//      après tout le reste — leur rect n'est PAS celui du widget, cf.
//      UiComboBox::DropdownRect),
//   2. les overlays AttachLayout::FIXED (popups/menus/modales/HUD), triés par
//      UiOverlayLayer::order,
//   3. les arbres normaux (racines sans UiParent), un enfant étant dessiné
//      PAR-DESSUS son parent.
//
// Les mêmes sorties anticipées que DrawTree sont reproduites (absence de
// UiComputed, clip dégénéré, sous-arbre FIXED sauté pendant la descente
// normale) pour que "ce qui est cliquable" colle à "ce qui est visible".
//
// PERFORMANCE — pourquoi un index et pas une descente d'arbre à chaque appel :
// parcourir l'arbre coûte, par entité, plusieurs recherches de composants
// (donc de tables de hachage) et une allocation par conteneur ; mesuré à 5 à
// 7 ms par appel sur 1200 widgets en build de debug (-O0 + ASan), soit un
// budget d'image entier perdu dès quelques appels par frame. L'ordre de
// dessin, lui, ne change QUE lorsque le layout re-tourne : on le calcule donc
// une fois par passe de layout (LayoutSystem::PassCount() sert de version) et
// chaque test n'est plus qu'un parcours arrière d'un vecteur contigu de
// rectangles — sans allocation ni recherche de composant tant qu'aucun
// candidat ne contient le point. Les états DYNAMIQUES (UiHidden,
// UiPointerThrough, combo ouvert) restent lus en direct sur les rares
// candidats retenus, donc les afficher/masquer ne demande aucune
// reconstruction.
class HitTestIndex {
public:
	/// Reconstruit l'index si la géométrie a bougé. `layoutPass` :
	/// LayoutSystem::PassCount() (une passe de layout = une géométrie neuve).
	/// Le nombre d'entités couvre les créations/destructions survenues sans
	/// passe de layout.
	void Refresh(ecs::ArchetypeRegistry &world, uint64_t layoutPass);

	/// Force une reconstruction au prochain Refresh (changement structurel
	/// qui ne passe ni par le layout ni par le nombre d'entités — reparentage
	/// manuel, bascule d'un AttachLayout, UiOverlayLayer modifié à la volée).
	void Invalidate() noexcept { m_built = false; }

	/// Change à chaque reconstruction — permet à un appelant de savoir que
	/// ses propres résultats mémorisés ne valent plus rien.
	[[nodiscard]] uint64_t Revision() const noexcept { return m_revision; }

	/// Nombre de widgets indexés (diagnostic).
	[[nodiscard]] size_t Size() const noexcept { return m_nodes.size(); }

	/// Widget le plus en avant sous `p`, entité invalide si le point ne
	/// touche aucun widget dessiné. `world` n'est lu que pour les états
	/// dynamiques des rares candidats géométriquement retenus.
	[[nodiscard]] ecs::Entity TopMostAt(ecs::ArchetypeRegistry &world, sdl3::FPoint p) const;

private:
	struct Node {
		ecs::Entity entity;
		sdl3::FRect screen;
		sdl3::FRect clip;
	};

	void Rebuild(ecs::ArchetypeRegistry &world);

	/// Ajoute `e` puis son sous-arbre, dans l'ordre de dessin. UiHidden n'est
	/// PAS filtré ici : c'est un état dynamique, relu à chaque test (sinon
	/// afficher un popup demanderait une reconstruction).
	void Append(ecs::ArchetypeRegistry &world, ecs::Entity e, bool overlayEntry = false);

	std::vector<Node> m_nodes;  ///< tous les widgets, du premier au dernier dessiné
	std::vector<Node> m_combos; ///< sous-ensemble portant UiComboBox (liste déroulante)
	uint64_t m_layoutPass = 0;
	uint64_t m_revision = 0;
	size_t m_entityCount = 0;
	bool m_built = false;
};

/// Widget le plus en avant sous le point `p` (cf. HitTestIndex) — version
/// ponctuelle, qui reconstruit l'index à chaque appel. Pratique pour un test
/// ou un appel isolé ; tout code appelé à chaque évènement doit garder un
/// HitTestIndex et le rafraîchir (cf. InputSystem::FrontMostAt).
[[nodiscard]] ecs::Entity HitTestTopMost(ecs::ArchetypeRegistry &world, sdl3::FPoint p);

/// Active/désactive un widget (ajoute ou retire UiDisabled).
void SetEnabled(ecs::ArchetypeRegistry &world, ecs::Entity e, bool enabled);

} // namespace ui
