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
#include <editor/dock.hpp>
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
// Couleurs et cadre de l'éditeur : module partagé `editor` (theme.hpp) — la
// sandbox et Voxelia Studio ont le même cadre, les mêmes docks.
// ============================================================================

using editor::AXIS_X;
using editor::AXIS_Y;
using editor::AXIS_Z;
using editor::EditorCss;
using editor::FromColor;
using editor::Luminance;
using editor::Mix;
using editor::Palette;
using editor::PaletteOf;
using editor::Rgb;
using editor::ToColor;

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
/// Panneau à onglets du module `editor`, construit depuis le contexte.
class DockPanel : public editor::DockPanel {
public:
	explicit DockPanel(UiContext &ctx) : editor::DockPanel(ctx.gui) {}
};

} // namespace kit
} // namespace sandbox
