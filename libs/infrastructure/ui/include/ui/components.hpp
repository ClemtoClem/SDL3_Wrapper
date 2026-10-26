#pragma once
/**
 * ui::components — briques ATOMIQUES de l'interface (composants ECS).
 *
 * Chaque fonctionnalité élémentaire est une structure purement déclarative
 * (valeur/POD) ; un widget n'est qu'une entité (ou un petit arbre d'entités)
 * qui combine ces briques. Il n'existe pas de composant « Bouton » ou
 * « Curseur » : un bouton est un `Element` de type "button" + `TextContent` +
 * `Pressable` + `Focusable` ; un curseur est un `Range` + `RangeInput` dont
 * les enfants portent un `RangeView` (piste remplie, pouce) ; une case à
 * cocher est un `Check` + `Checkable` dont un enfant porte un `Shape` coche
 * visible par `ShowWhen` ; un menu déroulant est un `Selection` + un
 * `PopupTrigger` dont le `PopupState` contient des `SelectItem`…
 *
 * Les briques se trouvent les unes les autres PAR L'ARBRE : un `RangeView`
 * suit le `Range` de son plus proche ancêtre, un `SelectItem` sélectionne
 * dans le `Selection` de son plus proche ancêtre, un `ShowWhen` lit l'état de
 * son plus proche ancêtre… Aucune entité n'a donc besoin de connaître les
 * autres par leur identifiant : un widget composé se décrit entièrement par
 * sa structure, comme un arbre de widgets Flutter.
 *
 * La hiérarchie elle-même est celle du module ecs (`ecs::ChildOf` /
 * `ecs::Children`, relations.hpp).
 */
#include <cmath>
#include <functional>
#include <vector>

#include <ecs/ecs.hpp>
#include <ecs/relations.hpp>

#include "css.hpp"
#include "events.hpp"
#include "style.hpp"

namespace ui {

class Painter;

// ============================================================================
// Identité et style
// ============================================================================

/// Identité d'un widget pour les sélecteurs CSS : type (`button`), id
/// (`#quit`), classes (`.sidebar-icon`).
struct Element {
	String type;
	String id;
	std::vector<String> classes;

	[[nodiscard]] bool HasClass(StringView name) const noexcept {
		for (const String &c : classes)
			if (c.View() == name)
				return true;
		return false;
	}
};

/// Style posé par le code (builder fluent ou Widget::Style) :
///  - `own`    : privé (comme `#id`/`this`), non hérité par les enfants ;
///  - `shared` : partagé (comme une classe), hérité par les enfants ;
///  - `states` : variantes appliquées dans certains états (:hover…).
struct InlineStyle {
	StyleProps own;
	StyleProps shared;
	std::vector<StateStyle> states;
};

/// Feuilles de style importées PAR ce widget : elles s'appliquent à lui
/// (`this`) et à sa descendance, avec priorité sur celles de ses ancêtres.
struct StyleScope {
	std::vector<StyleSheetPtr> sheets;
};

/// États dynamiques (pseudo-classes :hover, :active, :focus…).
struct WidgetState {
	State flags = State::None;
};

/// Valeurs partagées transmises aux enfants (propriétés héritables et
/// propriétés personnalisées) — tenu par le StyleSystem.
struct SharedStyle {
	StyleProps props;
};

// ============================================================================
// Géométrie (résultat du Layout System)
// ============================================================================

/// Rectangle calculé par le Layout System (coordonnées fenêtre).
struct Rect {
	sdl3::FRect box{};	   ///< boîte de bordure
	sdl3::FRect content{}; ///< boîte de contenu (bordure, padding et barres de défilement exclus)
	sdl3::FRect clip{};	   ///< zone de dessin autorisée (boîte ∩ clips des ancêtres)
	bool visible = false;  ///< placé et non entièrement rogné cette passe
};

/// État de défilement d'un conteneur à `overflow: auto|scroll` (ou d'un texte
/// à `text-overflow: scroll|wrap` borné).
struct Scroll {
	sdl3::FPoint offset{};	///< décalage courant, borné à [0, content - view]
	sdl3::FPoint content{}; ///< taille du contenu
	sdl3::FPoint view{};	///< taille visible
	sdl3::FPoint gutter{};	///< place des barres (x : verticale, y : horizontale)

	[[nodiscard]] sdl3::FPoint MaxOffset() const noexcept {
		return {math::Max(0.f, content.x - view.x), math::Max(0.f, content.y - view.y)};
	}
	void Clamp() noexcept {
		const sdl3::FPoint m = MaxOffset();
		offset.x = math::Clamp(offset.x, 0.f, m.x);
		offset.y = math::Clamp(offset.y, 0.f, m.y);
	}
};

// ============================================================================
// Contenus
// ============================================================================

struct TextContent {
	String value;
};

/// Libellé interne d'un widget composé (bouton, case à cocher…) : cible de
/// `Widget::Text` / `SetText` quand le widget lui-même n'a pas de texte.
struct TextSlot {};

/// Corps d'un widget composé (expander, nœud d'arbre…) : cible de
/// `Widget::Append` / `Insert` / `Clear`, l'en-tête restant intact.
struct ContentSlot {};

/// Indicateur de tri d'une colonne de table (texte ▲ / ▼ mis à jour au tri).
struct SortIndicator {
	int column = 0;
};

/// Image : clé du cache de textures ou chemin de fichier.
struct ImageContent {
	String source;
};

/// Glyphe d'une police d'icônes (cf. glyphs.hpp) ; taille = font-size,
/// teinte = color.
struct IconContent {
	String glyph;  ///< UTF-8
	String family; ///< famille de police enregistrée
};

enum class ShapeKind : uint8_t {
	Circle,
	Ring,
	Arc,	  ///< arc de `sweep` degrés depuis `start` (+ Spin)
	Check,	  ///< coche ✓
	Dash,	  ///< tiret (case indéterminée)
	Cross,	  ///< croix ×
	Plus,
	Minus,
	ChevronRight,
	ChevronDown,
	ChevronLeft,
	ChevronUp,
	TriangleRight,
	TriangleDown,
	Dot,
	Grip, ///< poignée de redimensionnement ↘
	Line, ///< diagonale (aiguille d'un potentiomètre : cf. RangeView Angle)
};

/// Forme vectorielle dessinée dans la boîte de contenu, de la couleur `color`.
struct Shape {
	ShapeKind kind = ShapeKind::Circle;
	float start = 0.f;
	float sweep = 270.f;
	float thickness = 2.f;
};

/// Rendu libre : le rappel reçoit le peintre (clip déjà posé) et la boîte.
struct CanvasPainter {
	std::function<void(Painter &, const sdl3::FRect &)> draw;
};

// ============================================================================
// Visibilité, activation, interaction
// ============================================================================

/// Masqué par le code (et toute sa descendance) : ni place, ni dessin, ni saisie.
struct Hidden {};
/// Désactivé (et toute sa descendance) : grisé par le style (:disabled), inerte.
struct Disabled {};

/// Accepte le focus clavier/manette (navigation Tab, flèches, croix directionnelle).
struct Focusable {
	int tabIndex = 0;
};

/// Réagit à l'activation : état :active à l'appui, clic au clavier (Entrée,
/// Espace) et à la manette (bouton SOUTH) quand il a le focus.
struct Pressable {};

/// Infobulle affichée après un court survol.
struct Tooltip {
	String text;
};

// ============================================================================
// Valeurs et comportements
// ============================================================================

/// État booléen (coché, activé, déplié). `anim` suit `on` en douceur (rendu).
struct Check {
	bool on = false;
	float anim = 0.f;
};

enum class CheckMode : uint8_t { Toggle, Radio };

/// Un clic bascule le `Check` du widget (Radio : le coche et décoche les
/// autres du même groupe ; groupe vide = frères de même parent).
struct Checkable {
	CheckMode mode = CheckMode::Toggle;
	String group;
};

/// Un clic bascule le `Check` du plus proche ancêtre (en-tête d'un panneau
/// repliable, flèche d'un nœud d'arbre).
struct ToggleParent {};

/// Valeur numérique bornée. `page` : portion visible (barre de défilement).
struct Range {
	float min = 0.f, max = 1.f, value = 0.f;
	float step = 0.f;
	float page = 0.f;

	[[nodiscard]] float Span() const noexcept { return max - min; }
	[[nodiscard]] float Normalized() const noexcept {
		const float span = math::Max(0.f, Span() - page);
		return span > 0.f ? math::Clamp((value - min) / span, 0.f, 1.f) : 0.f;
	}
	[[nodiscard]] float Snap(float v) const noexcept {
		if (step > 0.f)
			v = min + std::round((v - min) / step) * step;
		return math::Clamp(v, min, math::Max(min, max - page));
	}
};

enum class RangeMode : uint8_t {
	Track,	  ///< la position du pointeur le long de la piste donne la valeur (curseur, barre)
	Relative, ///< le déplacement du pointeur ajoute `speed` × pixels (potentiomètre, valeur glissée)
};

/// Saisie d'un `Range` : glisser, molette, flèches du clavier.
struct RangeInput {
	RangeMode mode = RangeMode::Track;
	Orientation axis = Orientation::Horizontal;
	float speed = 0.01f;	///< Relative : valeur par pixel
	bool wheel = true;
	bool invert = false;	///< vertical : le haut = max
	float thumbSize = 0.f;	///< Track : taille du pouce (la course est la piste moins le pouce)
};

enum class RangePart : uint8_t {
	Fill,		///< longueur proportionnelle à la valeur (piste remplie, barre de progression)
	Thumb,		///< position proportionnelle à la valeur (pouce)
	Angle,		///< rotation (aiguille / arc d'un potentiomètre)
	CheckThumb, ///< position suivant le `Check` animé (interrupteur)
	Thumb2D,	///< position 2D suivant un `Range2D`
};

/// Partie visuelle liée à la valeur du plus proche ancêtre (lui compris).
struct RangeView {
	RangePart part = RangePart::Fill;
	Orientation axis = Orientation::Horizontal;
	bool invert = false;
};

/// Texte formaté de la valeur du plus proche `Range` (lui compris).
struct RangeLabel {
	int decimals = 2;
	String prefix, suffix;
	std::function<String(float)> format;
};

/// Un clic ajoute `step × direction` au plus proche `Range` (boutons +/-).
struct RangeStep {
	float direction = 1.f;
};

/// Valeur 2D dans [0,1]² (carré saturation/valeur d'un sélecteur de couleur).
struct Range2D {
	sdl3::FPoint value{};
};

/// Couleur éditée (pastille, sélecteur de couleur).
struct ColorModel {
	Color value{1.f, 1.f, 1.f, 1.f};
};

/// Sélection d'indices (listes, onglets, menus déroulants, tables).
struct Selection {
	std::vector<int> selected; ///< triés
	int anchor = -1;		   ///< départ d'une extension Maj+clic
	int current = -1;		   ///< dernier élément cliqué / courant au clavier
	bool multiple = false;
	bool toggleable = false; ///< un second clic désélectionne (sinon : reste sélectionné)

	[[nodiscard]] bool IsSelected(int i) const noexcept;
	[[nodiscard]] int First() const noexcept { return selected.empty() ? -1 : selected.front(); }
	void SelectOnly(int i);
	/// Clic façon Explorateur (simple / Ctrl / Maj).
	void Click(int i, bool ctrl, bool shift);
};

/// Élément d'index `index` du plus proche `Selection` ancêtre.
struct SelectItem {
	int index = 0;
};

/// Texte = libellé de l'élément sélectionné du plus proche `Selection`.
struct SelectionText {
	std::vector<String> items;
	String placeholder;
};

enum class VisibleIf : uint8_t {
	Checked,   ///< le `Check` de l'ancêtre est vrai (panneau déplié)
	Unchecked, ///< … est faux
	Selected,  ///< l'élément `index` du `Selection` de l'ancêtre est sélectionné (page d'onglet)
	Editing,   ///< l'ancêtre est en cours d'édition texte (valeur glissée)
	NotEditing,
};

/// Visible seulement si la condition, évaluée sur le plus proche ancêtre qui
/// porte l'état concerné, est vraie.
struct ShowWhen {
	VisibleIf condition = VisibleIf::Checked;
	int index = 0;
};

// ============================================================================
// Texte éditable
// ============================================================================

enum class IOMode : uint8_t {
	ReadOnly,			   ///< affichage seul
	ReadAndCopy,		   ///< lecture + copie (journal)
	ReadWrite,			   ///< édition, ni copie ni collage
	ReadWriteCopy,		   ///< édition + copie
	ReadWriteCopyPaste,	   ///< tout (champ classique)
};
[[nodiscard]] constexpr bool IoCanWrite(IOMode m) noexcept { return m >= IOMode::ReadWrite; }
[[nodiscard]] constexpr bool IoCanCopy(IOMode m) noexcept {
	return m == IOMode::ReadAndCopy || m == IOMode::ReadWriteCopy || m == IOMode::ReadWriteCopyPaste;
}
[[nodiscard]] constexpr bool IoCanPaste(IOMode m) noexcept { return m == IOMode::ReadWriteCopyPaste; }

/// Morceau coloré d'une ligne (octets [begin, end) de la ligne AFFICHÉE).
struct TextSpan {
	size_t begin = 0;
	size_t end = 0;
	Color color{1.f, 1.f, 1.f, 1.f};
};
/// Coloration syntaxique d'une ligne (sans état d'une ligne à l'autre).
using SyntaxHighlighter = std::function<void(const String &line, std::vector<TextSpan> &out)>;

/// Édition du `TextContent` du widget (curseur, sélection, presse-papiers).
struct TextEditor {
	String placeholder;
	IOMode mode = IOMode::ReadWriteCopyPaste;
	size_t maxLength = 4096;
	bool multiline = false;
	bool password = false;
	bool submitOnEnter = true; ///< mono-ligne : Entrée émet Submit
	// Éditeur de code
	bool lineNumbers = false;
	bool monospace = false;
	bool highlightLine = false;
	bool followTail = false; ///< suit la fin du texte (journal) tant qu'on n'est pas remonté
	SyntaxHighlighter highlighter;
	// État
	size_t cursor = 0;
	size_t anchor = 0;
	float blink = 0.f;
	bool stickToBottom = true;
	float preferredX = -1.f; ///< colonne visée par Haut/Bas
};

/// Double-clic : passe en édition texte de la valeur (cf. DragValue) ;
/// Entrée valide, Échap annule.
struct EditOnDoubleClick {
	bool editing = false;
	String before;
	bool singleClick = false; ///< un simple clic suffit (champ numérique)
};

// ============================================================================
// Popups, menus
// ============================================================================

enum class Placement : uint8_t {
	Below,		///< sous l'ancre, aligné à gauche
	BelowRight, ///< sous l'ancre, aligné à droite
	Above,
	Right, ///< à droite (sous-menus)
	Left,
	Center, ///< centré dans la fenêtre (modales)
	Point,	///< au point `at` (menu contextuel)
};

/// Racine d'un popup (position: fixed). Ouvert/fermé par le contexte.
struct PopupState {
	bool open = false;
	bool modal = false;			   ///< bloque le reste de l'interface (voile)
	Placement placement = Placement::Below;
	ecs::Entity anchor{};		   ///< widget d'ancrage (sinon le parent)
	sdl3::FPoint at{};			   ///< Placement::Point
	bool closeOnOutside = true;	   ///< clic hors du popup
	bool closeOnEscape = true;
	bool closeOnItemClick = false; ///< menus : un clic sur une entrée ferme la chaîne
	bool matchAnchorWidth = false; ///< menus déroulants : au moins la largeur de l'ancre
};

enum class PopupOpen : uint8_t { Click, Hover, ContextMenu };

/// Ouvre le popup du premier descendant qui en porte un.
struct PopupTrigger {
	PopupOpen mode = PopupOpen::Click;
	Placement placement = Placement::Below;
};

/// Barre de menus : quand un de ses menus est ouvert, survoler une autre
/// entrée bascule sur son menu.
struct MenuStrip {};

// ============================================================================
// Glisser-déposer, redimensionnement
// ============================================================================

/// Source de glisser-déposer transportant un identifiant applicatif.
struct DragSource {
	String kind;
	int64_t id = 0;
	String label; ///< fantôme dessiné près du pointeur (vide : aucun)
	int count = 1;
};

/// Cible de dépôt (`accepts` vide = tout).
struct DropTarget {
	String accepts;
	bool highlight = true;
};

/// Déplaçable par glisser parmi ses frères (le parent reçoit Reorder).
struct Reorderable {};

/// Poignée entre deux frères : glisser redimensionne le frère précédent
/// (le suivant, en `flex-grow`, absorbe le reste) — ou le SUIVANT si
/// `after` (panneau ancré à droite / en bas).
struct ResizeHandle {
	Orientation axis = Orientation::Horizontal;
	float minBefore = 40.f;
	float minAfter = 40.f;
	bool after = false;
	float maxSize = 0.f; ///< taille maximale du frère redimensionné (0 : aucune)
};

/// Glisser déplace (ou redimensionne) le plus proche ancêtre positionné
/// (absolute/fixed) — barre de titre et coin d'un panneau flottant.
struct MoveHandle {
	bool resize = false;
	sdl3::FPoint minSize{120.f, 60.f};
};

// ============================================================================
// Animations
// ============================================================================

/// Rotation continue (indicateur de chargement).
struct Spin {
	float speed = 360.f; ///< degrés/seconde
	float angle = 0.f;
};

/// État du défilement automatique `text-overflow: marquee`.
struct Marquee {
	float offset = 0.f;
	float hold = 0.f;
	bool forward = true;
	float overflow = 0.f; ///< largeur qui dépasse (écrite par la mise en page)
};

// ============================================================================
// Tables
// ============================================================================

enum class SortOrder : uint8_t { None, Ascending, Descending };

struct TableColumn {
	String title;
	float width = 100.f;
	float minWidth = 40.f;
	bool sortable = true;
};

/// Colonnes d'une table (racine) — largeurs lues par les `TableCell`.
struct TableModel {
	std::vector<TableColumn> columns;
	int sortColumn = -1;
	SortOrder order = SortOrder::None;
};

/// Cellule de la colonne `column` du plus proche `TableModel` ancêtre.
struct TableCell {
	int column = 0;
};

/// En-tête de colonne : un clic trie la table.
struct TableHeader {
	int column = 0;
};

/// Bord droit d'un en-tête : glisser redimensionne la colonne.
struct ColumnResizer {
	int column = 0;
};

// ============================================================================
// Hiérarchie : aides
// ============================================================================

[[nodiscard]] inline Option<ecs::Entity> ParentOf(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	return ecs::ParentOf(world, e);
}

/// Enfants dans l'ordre (vide si aucun).
[[nodiscard]] const std::vector<ecs::Entity> &ChildList(const ecs::ArchetypeRegistry &world, ecs::Entity e);

/// `e` ou son plus proche ancêtre portant `T` (entité invalide sinon).
template <typename T> [[nodiscard]] ecs::Entity NearestWith(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	for (Option<ecs::Entity> cur = Some(e); cur.IsSome(); cur = ecs::ParentOf(world, cur.Unwrap()))
		if (world.HasComponent<T>(cur.Unwrap()))
			return cur.Unwrap();
	return ecs::Entity{};
}

/// Comme NearestWith, en partant du parent.
template <typename T> [[nodiscard]] ecs::Entity AncestorWith(const ecs::ArchetypeRegistry &world, ecs::Entity e) {
	auto parent = ecs::ParentOf(world, e);
	return parent.IsSome() ? NearestWith<T>(world, parent.Unwrap()) : ecs::Entity{};
}

/// Vrai si `e` est `ancestor` ou l'un de ses descendants.
[[nodiscard]] bool IsDescendantOrSelf(const ecs::ArchetypeRegistry &world, ecs::Entity e, ecs::Entity ancestor);
/// `e` ou un ancêtre porte `Hidden`, `Disabled`.
[[nodiscard]] bool IsHiddenRecursive(const ecs::ArchetypeRegistry &world, ecs::Entity e);
[[nodiscard]] bool IsDisabledRecursive(const ecs::ArchetypeRegistry &world, ecs::Entity e);

} // namespace ui
