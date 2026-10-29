#pragma once
/**
 * ui::widgets — catalogue des widgets, tous COMPOSÉS des briques atomiques
 * de components.hpp (aucun composant propre à un widget).
 *
 * Chaque fonction rend un `WidgetBuilder` (valeur) ; le type CSS du widget
 * est indiqué entre parenthèses, et ses parties internes portent des
 * classes (`slider-track`, `checkbox-box`…) que les feuilles de style ciblent :
 *
 * @code{.css}
 * slider .slider-thumb { background-color: #fff; border-radius: 8px; }
 * button.danger:hover  { background-color: #c03030; }
 * #quit                { color: #ff8080; }
 * @endcode
 *
 * Tout ce qu'une feuille configure se configure aussi par le builder
 * (`.Background(...)`, `.HoverStyle(...)`…), et inversement.
 */
#include <functional>
#include <vector>

#include "builder.hpp"
#include "glyphs.hpp"

namespace ui {

// ============================================================================
// Conteneurs
// ============================================================================

/// Boîte générique (`box`, ou le type donné).
[[nodiscard]] WidgetBuilder Box(String type = "box");
/// Colonne flexbox (`column`).
[[nodiscard]] WidgetBuilder Column();
/// Rangée flexbox (`row`).
[[nodiscard]] WidgetBuilder Row();
/// Rangée qui passe à la ligne (`wrap`).
[[nodiscard]] WidgetBuilder Wrap();
/// Enfants superposés (`stack`).
[[nodiscard]] WidgetBuilder Stack();
/// Grille à `columns` colonnes égales (`grid`).
[[nodiscard]] WidgetBuilder Grid(int columns);
/// Colonne défilante (`scroll-view`).
[[nodiscard]] WidgetBuilder ScrollView();
/// Panneau (carte) au fond du thème (`panel`).
[[nodiscard]] WidgetBuilder Panel();
/// Espace extensible (`spacer`).
[[nodiscard]] WidgetBuilder Spacer();
/// Filet de séparation (`separator`, classe `horizontal`/`vertical`).
[[nodiscard]] WidgetBuilder Separator(Orientation orientation = Orientation::Horizontal);

// ============================================================================
// Contenus
// ============================================================================

/// Texte (`text`).
[[nodiscard]] WidgetBuilder Text(String text);
/// Libellé (`label`) — un texte que les feuilles distinguent des autres.
[[nodiscard]] WidgetBuilder Label(String text);
/// Titre (`heading`, classe `h1`…`h6`).
[[nodiscard]] WidgetBuilder Heading(String text, int level = 1);
/// Pastille de statut (`badge`).
[[nodiscard]] WidgetBuilder Badge(String text);
/// Image (`image`) : chemin de fichier ou clé du cache de textures.
[[nodiscard]] WidgetBuilder Image(String source);
/// Glyphe d'une police d'icônes (`icon`) déjà résolu.
[[nodiscard]] WidgetBuilder Icon(String glyph, String family, float size = 16.f);
/// Icône typée (glyphs.hpp) : `ui::Icon(ui::MaterialIcons::SAVE, 18.f)`.
template <GlyphSet E> [[nodiscard]] WidgetBuilder Icon(E icon, float size = 16.f) {
	return Icon(Glyphs::ToUtf8(icon), String(Glyphs::FontFamily<E>()), size);
}
/// Forme vectorielle (`shape`).
[[nodiscard]] WidgetBuilder ShapeBox(ShapeKind kind, float thickness = 2.f);
/// Dessin libre (`canvas`).
[[nodiscard]] WidgetBuilder Canvas(std::function<void(Painter &, const sdl3::FRect &)> draw);

// ============================================================================
// Commandes
// ============================================================================

/// Bouton (`button`) ; libellé interne `.button-label`.
[[nodiscard]] WidgetBuilder Button(String text);
/// Bouton à icône seule (`button`, classe `icon-button`).
[[nodiscard]] WidgetBuilder IconButton(WidgetBuilder icon, String tooltip = String());
template <GlyphSet E> [[nodiscard]] WidgetBuilder IconButton(E icon, String tooltip = String(), float size = 18.f) {
	return IconButton(Icon(icon, size), std::move(tooltip));
}
/// Interrupteur (`switch`) ; pouce `.switch-thumb`.
[[nodiscard]] WidgetBuilder Switch(bool on = false);
/// Case à cocher (`checkbox`) ; `.checkbox-box`, `.checkbox-mark`, libellé.
[[nodiscard]] WidgetBuilder Checkbox(String label = String(), bool checked = false);
/// Bouton radio (`radio`) ; groupe vide = frères de même parent.
[[nodiscard]] WidgetBuilder Radio(String label, String group = String(), bool checked = false);
/// Curseur (`slider`) ; `.slider-track`, `.slider-fill`, `.slider-thumb`.
[[nodiscard]] WidgetBuilder Slider(float min, float max, float value, float step = 0.f,
								   Orientation orientation = Orientation::Horizontal);
/// Barre de progression (`progress`) ; `.progress-fill`.
[[nodiscard]] WidgetBuilder Progress(float min, float max, float value);
/// Indicateur de chargement (`spinner`).
[[nodiscard]] WidgetBuilder Spinner(float thickness = 3.f, float speedDegPerSecond = 360.f);
/// Potentiomètre (`knob`) : glisser verticalement, molette, flèches.
[[nodiscard]] WidgetBuilder Knob(float min, float max, float value, float step = 0.f);
/// Barre de défilement autonome (`scrollbar`) : `view` visible sur `content`.
[[nodiscard]] WidgetBuilder ScrollBar(float content, float view, Orientation orientation = Orientation::Horizontal);

// ============================================================================
// Saisie
// ============================================================================

/// Champ mono-ligne (`input`) : Entrée émet Submit.
[[nodiscard]] WidgetBuilder Input(String placeholder = String(), String value = String());
/// Mot de passe (`input`, classe `password`).
[[nodiscard]] WidgetBuilder Password(String placeholder = String());
/// Zone multi-ligne (`textarea`).
[[nodiscard]] WidgetBuilder TextArea(String placeholder = String(), String value = String());
/// Éditeur de code (`textarea`, classe `code`) : chasse fixe, numéros, ligne courante.
[[nodiscard]] WidgetBuilder CodeEditor(String value = String(), SyntaxHighlighter highlighter = nullptr);
/// Journal en lecture seule copiable, qui suit les nouvelles lignes (`textarea`, classe `log`).
[[nodiscard]] WidgetBuilder LogView(SyntaxHighlighter highlighter = nullptr);
/// Valeur glissée façon ImGui::DragFloat (`drag-value`) ; double clic : saisie.
[[nodiscard]] WidgetBuilder DragValue(float min, float max, float value, float step = 0.f, float speed = 0.f,
									  int decimals = 2);
/// Champ numérique à boutons −/+ (`number-input`).
[[nodiscard]] WidgetBuilder NumberInput(float min, float max, float value, float step = 1.f, int decimals = 2);

// ============================================================================
// Listes et sélection
// ============================================================================

/// Catégorie d'un menu déroulant (titre vide : éléments au premier niveau).
struct ComboCategory {
	String title;
	std::vector<String> items;
};

/// Menu déroulant (`combo`) ; liste `.combo-popup` > `.combo-item`. Select : index.
[[nodiscard]] WidgetBuilder Combo(std::vector<String> items, int selected = 0);
/// Menu déroulant par catégories (sous-menus) ; index comptés bout à bout.
[[nodiscard]] WidgetBuilder Combo(std::vector<ComboCategory> categories, int selected = 0);
/// Liste à défilement (`listbox`) ; Select : index.
[[nodiscard]] WidgetBuilder ListBox(std::vector<String> items, int selected = -1, bool multiple = false);
/// Élément sélectionnable (`selectable`) du plus proche Selection ancêtre.
[[nodiscard]] WidgetBuilder Selectable(String text, int index);
/// Fait d'un conteneur une racine de sélection (pour des Selectable/TreeNode).
[[nodiscard]] WidgetBuilder SelectionList(bool multiple = true);
/// Nœud d'arbre repliable et sélectionnable (`tree-node`) ; enfants = sous-nœuds.
[[nodiscard]] WidgetBuilder TreeNode(String title, int index, bool expanded = true);
/// Panneau repliable (`expander`) ; enfants = corps. Toggle : déplié.
[[nodiscard]] WidgetBuilder Expander(String title, bool expanded = true);
/// Onglets (`tabview`) : un enfant par onglet. Select : index.
[[nodiscard]] WidgetBuilder TabView(std::vector<String> tabs, int active = 0);

// ============================================================================
// Popups, menus, dialogues
// ============================================================================

/// Popup ancré (`popup`) : `Widget::Open/OpenAt/Close`.
[[nodiscard]] WidgetBuilder Popup(Placement placement = Placement::Below);
/// Dialogue modal (`modal`) : voile plein écran, contenu centré.
[[nodiscard]] WidgetBuilder Modal(AnyWidget content);
/// Barre de menus (`menubar`) ; enfants = Menu(...).
[[nodiscard]] WidgetBuilder MenuBar();
/// Entrée de barre de menus (`menu`) ; enfants = entrées du menu.
[[nodiscard]] WidgetBuilder Menu(String title);
/// Entrée de menu (`menu-item`).
[[nodiscard]] WidgetBuilder MenuItem(String text, String shortcut = String());
/// Entrée à sous-menu (`menu-item`, classe `submenu`) ; enfants = entrées.
[[nodiscard]] WidgetBuilder SubMenu(String text);
/// Filet entre entrées (`menu-separator`).
[[nodiscard]] WidgetBuilder MenuSeparator();
/// Menu contextuel (`context-menu`) ; à poser par `.ContextMenu(...)` sur un widget,
/// ou à monter seul et ouvrir par `Widget::OpenAt`.
[[nodiscard]] WidgetBuilder ContextMenu();

// ============================================================================
// Widgets complexes
// ============================================================================

/// Table (`table`) : en-têtes triables et redimensionnables, sélection,
/// lignes réordonnables. Change : colonne triée ; Select : ligne.
[[nodiscard]] WidgetBuilder Table(std::vector<TableColumn> columns, std::vector<std::vector<String>> rows,
								  bool multiple = true, bool reorderable = false);
/// Deux panneaux séparés par une poignée glissable (`splitter`).
[[nodiscard]] WidgetBuilder Splitter(AnyWidget before, AnyWidget after, Orientation orientation = Orientation::Horizontal,
									 float initialBefore = 200.f, float minBefore = 50.f, float minAfter = 50.f);
/// Pastille de couleur cliquable (`color-swatch`).
[[nodiscard]] WidgetBuilder ColorSwatch(Color color);
/// Sélecteur de couleur (`color-picker`) : carré SV, teinte, alpha, hexa.
[[nodiscard]] WidgetBuilder ColorPicker(Color initial, std::function<void(Color)> onChange = nullptr);
/// Calendrier mensuel (`calendar`).
[[nodiscard]] WidgetBuilder Calendar(int year, int month, int day = -1, std::function<void(int, int, int)> onSelect = nullptr);
/// Sélecteur de date (`date-picker`) : bouton + calendrier en popup.
[[nodiscard]] WidgetBuilder DatePicker(int year, int month, int day, std::function<void(int, int, int)> onSelect = nullptr);

// ── Dates ────────────────────────────────────────────────────────────────────

[[nodiscard]] constexpr bool IsLeapYear(int y) noexcept { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
[[nodiscard]] constexpr int DaysInMonth(int year, int month) noexcept {
	constexpr int K_DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	const int m = ((month - 1) % 12 + 12) % 12;
	return m == 1 && IsLeapYear(year) ? 29 : K_DAYS[m];
}
/// Jour de la semaine du 1er du mois (0 = lundi).
[[nodiscard]] constexpr int FirstWeekdayOfMonth(int year, int month) noexcept {
	int y = year, m = month;
	if (m < 3) {
		m += 12;
		y -= 1;
	}
	const int k = y % 100, j = y / 100;
	const int h = (1 + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
	return (h + 5) % 7;
}
[[nodiscard]] const char *MonthNameFr(int month) noexcept;

} // namespace ui
