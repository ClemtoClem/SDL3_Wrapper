#pragma once
/**
 * sandbox::kit — les briques d'interface partagées par tous les panneaux.
 *
 * Tout ce qui se répète d'un panneau à l'autre vit ici, une fois :
 *
 *  - `UiContext`       : les quatre références dont tout panneau a besoin ;
 *  - `NodeLook`        : icône, couleur et libellé d'un type de nœud (l'arbre,
 *                        l'inspecteur et le navigateur de ressources doivent
 *                        montrer une lumière de la MÊME façon) ;
 *  - les colorations syntaxiques (script, JSON, journal), fonctions pures
 *    testées sans fenêtre ;
 *  - `PropertyRows`    : les lignes « libellé + champ » de l'inspecteur ;
 *  - `DockPanel`       : un panneau à onglets façon éditeur de jeu (bandeau
 *                        d'onglets, bouton ≡, pages empilées).
 *
 * Les couleurs du cadre de l'éditeur (Palette) découlent de la palette du
 * thème ui:: ; la feuille de style de l'éditeur (EditorCss) en fait des
 * classes (`.editor-header`, `.editor-toolbar`…), si bien que changer de
 * thème repeint l'application sans rien reconstruire à la main.
 *
 * Aucune de ces briques ne connaît le document : elles reçoivent des valeurs
 * et rendent des rappels. C'est ce qui les rend réutilisables — et ce qui
 * garde la logique d'édition dans `Runtime`, seul endroit où elle est testée.
 */
#include <functional>
#include <memory>
#include <vector>

#include <core/core.hpp>
#include <ui/ui.hpp>

#include <sandbox/document/project.hpp>
#include <sandbox/engine/runtime.hpp>

namespace sandbox {

class ModelingSession;

/// Les références qu'utilise chaque panneau.
struct UiContext {
	Runtime &runtime;
	ui::Ui &gui;
	ecs::ArchetypeRegistry &registry;
	/// Renderer de la fenêtre, pour charger des aperçus d'images — nul tant
	/// que l'application ne l'a pas fourni (tests, mode sans écran).
	sdl3::Renderer *renderer = nullptr;
	/// Session de modélisation de la vue (cf. modeling_session.hpp) — nulle
	/// tant que l'éditeur ne l'a pas fournie.
	ModelingSession *modeling = nullptr;

	[[nodiscard]] const ui::Palette &Theme() const noexcept { return gui.CurrentPalette(); }
};

namespace kit {

// ============================================================================
// Couleurs fixes de l'interface (indépendantes du thème : ce sont des CODES,
// pas de la décoration — l'axe X est rouge dans tous les éditeurs du monde).
// ============================================================================

inline constexpr sdl3::FColor AXIS_X{0.90f, 0.33f, 0.30f, 1.f};
inline constexpr sdl3::FColor AXIS_Y{0.45f, 0.78f, 0.32f, 1.f};
inline constexpr sdl3::FColor AXIS_Z{0.30f, 0.52f, 0.95f, 1.f};

[[nodiscard]] sdl3::FColor Rgb(int r, int g, int b, int a = 255) noexcept;

[[nodiscard]] inline sdl3::FColor FromColor(const sdl3::Color &c) noexcept { return Rgb(c.r, c.g, c.b, c.a); }

[[nodiscard]] sdl3::Color ToColor(const sdl3::FColor &c) noexcept;

// ============================================================================
// Palette du cadre de l'éditeur, DÉRIVÉE du thème
// ============================================================================

/// Couleurs du « chrome » de l'éditeur (fond de fenêtre, bandeaux, barres
/// d'outils, sélection…) : aucune n'est codée en dur, toutes découlent du
/// thème actif — changer de thème repeint donc TOUTE l'application, pas
/// seulement les widgets que la fabrique colore d'elle-même.
struct Palette {
	sdl3::FColor base;		///< fond de l'application, gouttières, poignées de redimensionnement
	sdl3::FColor header;	///< bandeaux de docks, barre de menus, barre d'état
	sdl3::FColor toolbar;	///< barres d'outils, fond de l'arborescence des ressources
	sdl3::FColor well;		///< cadre des vignettes, fond des aperçus
	sdl3::FColor selection; ///< élément sélectionné, bouton d'outil actif
	sdl3::FColor separator; ///< filets verticaux et horizontaux
	sdl3::FColor ok, warning, error;
	bool light = false; ///< fond clair : textes et icônes foncés
};

/// Luminance relative (Rec. 709) — tranche clair/sombre.
[[nodiscard]] float Luminance(const sdl3::FColor &c) noexcept;

/// `a` vers `b` d'une fraction `t`, rendue opaque (le chrome ne doit pas
/// laisser voir ce qu'il y a derrière la fenêtre).
[[nodiscard]] sdl3::FColor Mix(const sdl3::FColor &a, const sdl3::FColor &b, float t) noexcept;

[[nodiscard]] Palette PaletteOf(const ui::Palette &theme);

/// Feuille de style de l'éditeur, dérivée de la palette (cf. Palette).
[[nodiscard]] String EditorCss(const ui::Palette &theme);

[[nodiscard]] inline Palette PaletteOf(const UiContext &ctx) { return PaletteOf(ctx.Theme()); }

/// Ce que l'interface montre d'un script analysé (cf. ScriptOutline) :
/// erreur de syntaxe ou refus du moteur en rouge/orange, comportement ou
/// scène valide en vert, module (à importer) en gris.
struct ScriptBadge {
	ui::MaterialIcons icon;
	sdl3::FColor color;
	String text;	 ///< ScriptOutline::Summary
	bool broken = false; ///< syntaxe invalide, ou refusé au lancement du mode Jeu
};

[[nodiscard]] ScriptBadge BadgeOf(const UiContext &ctx, const ScriptOutline &outline);

/// Teinte d'icône lisible sur le fond du thème : les couleurs de code des
/// nœuds et des fichiers sont des pastels pensés pour un fond sombre ; sur un
/// thème clair, on les fonce d'autant pour garder le contraste.
[[nodiscard]] sdl3::FColor Readable(const UiContext &ctx, const sdl3::FColor &color);

// ============================================================================
// Apparence d'un type de nœud
// ============================================================================

/// Icône, teinte et libellé d'un nœud — le « (Light) » gris de l'arbre.
struct NodeLook {
	ui::MaterialIcons icon = ui::MaterialIcons::ACCOUNT_TREE;
	sdl3::FColor color = Rgb(150, 170, 200);
	const char *label = "Nœud";
};

/// Déduit l'apparence d'un nœud de son TYPE, puis de ses composants : un
/// maillage qui porte une lumière (une lanterne) reste un maillage à l'œil,
/// mais un groupe qui porte une lumière est une lumière.
[[nodiscard]] NodeLook LookOf(const scene::Node &node);

// ============================================================================
// Colorations syntaxiques (fonctions pures, cf. ui::SyntaxHighlighter)
// ============================================================================

namespace syntax {

/// Couleurs de coloration syntaxique — deux jeux, pour un fond sombre ou
/// clair (un jaune pâle lisible sur gris foncé disparaît sur du blanc).
struct Scheme {
	sdl3::FColor keyword, string, number, comment, function, nameSpace, key, brace, literal;
	sdl3::FColor error, warning, ok, dim;
	sdl3::FColor type; ///< bases du moteur (`Scene`, `Mesh3D`…) dans le code

	[[nodiscard]] static Scheme Dark() noexcept;
	[[nodiscard]] static Scheme Light() noexcept;
};

/// Jeu actif — choisi par le thème (cf. EditorUi::SetTheme).
[[nodiscard]] Scheme &Colors() noexcept;

[[nodiscard]] bool IsIdentStart(char c) noexcept;
[[nodiscard]] inline bool IsIdentChar(char c) noexcept { return IsIdentStart(c) || (c >= '0' && c <= '9'); }
[[nodiscard]] inline bool IsDigit(char c) noexcept { return c >= '0' && c <= '9'; }

/// Fin d'une chaîne entre guillemets commençant en `i` (guillemet inclus),
/// échappements `\"` respectés ; une chaîne non terminée court jusqu'au bout
/// de la ligne — comme le lexeur, qui la signalera.
[[nodiscard]] size_t StringEnd(const String &line, size_t i) noexcept;

[[nodiscard]] size_t NumberEnd(const String &line, size_t i) noexcept;

/// Langage de script de l'éditeur (« Script ») : mots-clés, chaînes, nombres,
/// commentaires (`#`, `//`), appels de fonction et tables d'API
/// (`editor.`, `node.`…).
void HighlightSled(const String &line, std::vector<ui::TextSpan> &out);

/// JSON : clés (chaîne suivie de `:`), chaînes, nombres, littéraux, accolades.
void HighlightJson(const String &line, std::vector<ui::TextSpan> &out);

/// Console : le niveau entre crochets donne la couleur (`[err   ]` en rouge,
/// `[warn  ]` en orange, `[ok    ]` en vert, `[script]` en cyan) — une erreur
/// est colorée sur toute sa ligne, c'est elle qu'on cherche des yeux.
void HighlightLog(const String &line, std::vector<ui::TextSpan> &out);

/// Coloration adaptée à un nom de fichier (ou de document) : `.json` et
/// `.gltf` en JSON, tout le reste dans le langage de l'éditeur.
[[nodiscard]] ui::SyntaxHighlighter ForName(const String &name);

} // namespace syntax

// ============================================================================
// Petites aides d'arbre d'interface
// ============================================================================

/// Vide un conteneur (et oublie sa ligne sélectionnée, cf. MarkSelected).
void ClearChildren(UiContext &ctx, ui::Widget parent);

/// Marque la ligne `index` (ui::Selectable) de la liste `list` comme
/// sélectionnée : état `:selected` de la ligne, sans évènement.
void MarkSelected(ui::Widget list, int index);

/// Change le fond d'un widget déjà construit (style inline).
void SetBackground(UiContext &ctx, ui::Widget widget, sdl3::FColor color);

void SetLabelText(UiContext &ctx, ui::Widget widget, const String &text);

void SetHidden(UiContext &ctx, ui::Widget widget, bool hidden);

/// Libellé discret (titre de section, légende).
ui::Widget Caption(UiContext &ctx, ui::Widget parent, const String &text, float size = 12.f);

/// Élément extensible qui pousse la suite d'une rangée vers la droite.
ui::Widget Spacer(UiContext &ctx, ui::Widget parent);

/// Icône seule, sans interaction (repère visuel d'une ligne).
ui::Widget Glyph(UiContext &ctx, ui::Widget parent, ui::MaterialIcons icon, sdl3::FColor color, float size = 16.f);

/// Bouton d'icône plat (≡, +, ⋮, ←…) : fond transparent au repos, survol
/// visible. `outIcon` rend l'icône pour les boutons dont le glyphe change
/// (lecture/arrêt).
ui::Widget IconButton(UiContext &ctx, ui::Widget parent, ui::MaterialIcons icon, const String &tooltip,
					  std::function<void()> action, float size = 24.f, ui::Widget *outIcon = nullptr,
					  sdl3::FColor tint = sdl3::FColor{});

/// Champ de recherche avec loupe, rapporte chaque frappe.
ui::Widget SearchField(UiContext &ctx, ui::Widget parent, const String &placeholder,
					   std::function<void(const String &)> onChange, float width = -1.f);

// ============================================================================
// PropertyRows — les lignes de l'inspecteur
// ============================================================================

/**
 * Fabrique de lignes « libellé | champ(s) ». Chaque ligne rend son entité ;
 * les valeurs repartent par rappel, avec la valeur COMPLÈTE (un vecteur
 * entier, une couleur entière) pour que l'appelant n'ait jamais à recoller
 * des composantes.
 */
class PropertyRows {
public:
	PropertyRows(UiContext &ctx, float labelWidth = 104.f) : m_ctx(ctx), m_labelWidth(labelWidth) {}

	/// Vecteur à trois composantes, X/Y/Z colorées comme les axes.
	ui::Widget Vector3(ui::Widget page, const char *label, math::FVector3 value, float speed,
					   std::function<void(math::FVector3)> onChange, float minValue = -1.0e5f, float maxValue = 1.0e5f);

	/// Vecteur à deux composantes (2D : X, Y).
	ui::Widget Vector2(ui::Widget page, const char *label, math::FVector2 value, float speed,
					   std::function<void(math::FVector2)> onChange, float minValue = -1.0e5f, float maxValue = 1.0e5f);

	ui::Widget Number(ui::Widget page, const char *label, float value, float minValue, float maxValue, float speed,
					  std::function<void(float)> onChange, int decimals = 2);

	ui::Widget Slider(ui::Widget page, const char *label, float value, float minValue, float maxValue,
					  std::function<void(float)> onChange);

	ui::Widget Check(ui::Widget page, const char *label, bool value, std::function<void(bool)> onToggle);

	ui::Widget Choice(ui::Widget page, const char *label, std::vector<String> items, int selected,
					  std::function<void(int)> onChange);

	/// Texte : validé à l'Entrée (un renommage ne doit pas partir à chaque
	/// frappe).
	ui::Widget Text(ui::Widget page, const char *label, const String &value, std::function<void(const String &)> onSubmit);

	ui::Widget ReadOnly(ui::Widget page, const char *label, const String &value);

	/**
	 * Couleur : une barre de la couleur (comme dans les maquettes) + la
	 * pipette ; l'une et l'autre ouvrent le sélecteur complet (teinte,
	 * saturation, hexa) dans un popup ancré sous la barre. Le popup fait
	 * partie de la rangée : il disparaît avec la page.
	 */
	ui::Widget Color(ui::Widget page, const char *label, sdl3::Color value, std::function<void(sdl3::Color)> onChange);

	/// Rangée nue « libellé | … » dans laquelle l'appelant place ses champs.
	ui::Widget Row(ui::Widget page, const char *label);

private:
	UiContext &m_ctx;
	float m_labelWidth;
};

// ============================================================================
// DockPanel — panneau à onglets façon éditeur de jeu
// ============================================================================

/**
 * Un panneau d'éditeur : un bandeau d'onglets (titre, × facultatif), une zone
 * libre à droite du bandeau (boutons propres au panneau), le bouton ≡ du menu
 * du panneau, puis les pages empilées — seule l'active est visible.
 *
 * Pourquoi pas `ui::TabView` : il dessine ses onglets lui-même, sans place
 * pour un × par onglet, un menu ≡ ou des boutons dans le bandeau, qui sont
 * justement ce qui fait un panneau d'éditeur (cf. les maquettes).
 */
class DockPanel {
public:
	explicit DockPanel(UiContext &ctx) : m_ctx(ctx) {}

	/// Construit le cadre sous `parent`. `size` pose les dimensions (le
	/// panneau ne décide pas de sa place dans la mise en page).
	ui::Widget Build(ui::Widget parent, const std::function<void(ui::WidgetBuilder &)> &size);

	/// Ajoute une page ; rend le conteneur où placer son contenu. `scroll` :
	/// contenu défilable (listes, inspecteur) ; sinon la page remplit la zone
	/// (vue 3D, éditeur de code, qui gèrent leur défilement eux-mêmes).
	ui::Widget AddPage(const String &title, bool scroll, bool closable = false, float pad = 6.f);

	/// Retire une page (et son contenu).
	void RemovePage(int index);

	void SetActive(int index);

	void SetTitle(int index, const String &title);

	[[nodiscard]] int Active() const noexcept { return m_active; }
	[[nodiscard]] int Count() const noexcept { return int(m_pages.size()); }
	[[nodiscard]] ui::Widget Page(int index) const;
	[[nodiscard]] ui::Widget Frame() const noexcept { return m_frame; }
	/// Zone du bandeau, à droite des onglets, pour les boutons du panneau.
	[[nodiscard]] ui::Widget HeaderExtra() const noexcept { return m_extra; }

	/// Appelé quand une page devient active (clic d'onglet ou programme).
	std::function<void(int)> onActivate;
	/// Appelé au clic sur le × d'un onglet fermable (la fermeture elle-même
	/// revient à l'appelant : un document modifié peut vouloir la refuser).
	std::function<void(int)> onClose;
	/// Appelé au clic sur ≡, avec le coin bas-gauche du bouton.
	std::function<void(float, float)> onMenu;

	static constexpr float HEADER_HEIGHT = 30.f;

private:
	struct PageInfo {
		String title;
		ui::Widget content;
		bool closable = false;
	};

	void RebuildTabs();

	UiContext &m_ctx;
	ui::Widget m_frame, m_header, m_tabs, m_extra, m_menuButton, m_body;
	std::vector<PageInfo> m_pages;
	int m_active = -1;
};

} // namespace kit
} // namespace sandbox
