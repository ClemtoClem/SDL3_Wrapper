#pragma once
/**
 * game_editor::kit — les briques d'interface partagées par tous les panneaux.
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
 * Règle de mise en page de tout ce dossier : un `UiFlow` (Row/Column) a 8 px
 * de marge PAR DÉFAUT, et un conteneur dont le contenu déborde affiche une
 * barre de défilement sans qu'on la demande. Chaque conteneur pose donc sa
 * marge explicitement (souvent `Pad(0.f)`) : un filet de 1 px ou un carré de
 * 8 px avec 16 px de marge déborderait, et dessinerait des barres parasites.
 *
 * Aucune de ces briques ne connaît le document : elles reçoivent des valeurs
 * et rendent des rappels. C'est ce qui les rend réutilisables — et ce qui
 * garde la logique d'édition dans `Runtime`, seul endroit où elle est testée.
 */
#include <functional>
#include <memory>
#include <vector>

#include "core/core.hpp"
#include "ui/ui.hpp"

#include "../document/project.hpp"
#include "../engine/runtime.hpp"

namespace game_editor {

/// Les quatre références qu'utilise chaque panneau.
struct UiContext {
	Runtime &runtime;
	ui::Ui &gui;
	ecs::ArchetypeRegistry &registry;
	ui::UiFactory &factory;
	/// Renderer de la fenêtre, pour charger des aperçus d'images — nul tant
	/// que l'application ne l'a pas fourni (tests, mode sans écran).
	sdl3::Renderer *renderer = nullptr;

	[[nodiscard]] const ui::UiTheme &Theme() const noexcept { return gui.Theme(); }
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

[[nodiscard]] Palette PaletteOf(const ui::UiTheme &theme);

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
// Colorations syntaxiques (fonctions pures, cf. ui::UiSyntaxHighlighter)
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
void HighlightSled(const String &line, std::vector<ui::UiTextSpan> &out);

/// JSON : clés (chaîne suivie de `:`), chaînes, nombres, littéraux, accolades.
void HighlightJson(const String &line, std::vector<ui::UiTextSpan> &out);

/// Console : le niveau entre crochets donne la couleur (`[err   ]` en rouge,
/// `[warn  ]` en orange, `[ok    ]` en vert, `[script]` en cyan) — une erreur
/// est colorée sur toute sa ligne, c'est elle qu'on cherche des yeux.
void HighlightLog(const String &line, std::vector<ui::UiTextSpan> &out);

/// Coloration adaptée à un nom de fichier (ou de document) : `.json` et
/// `.gltf` en JSON, tout le reste dans le langage de l'éditeur.
[[nodiscard]] ui::UiSyntaxHighlighter ForName(const String &name);

} // namespace syntax

// ============================================================================
// Petites aides d'arbre d'interface
// ============================================================================

/// Vide un conteneur. Les entités sont COLLECTÉES avant destruction :
/// `DespawnTree` modifie la liste d'enfants que l'on parcourrait.
void ClearChildren(UiContext &ctx, ecs::Entity parent);

/// Change le fond d'un widget déjà construit (style inline) et relance la
/// cascade de styles sur lui.
void SetBackground(UiContext &ctx, ecs::Entity entity, sdl3::FColor color);

void SetLabelText(UiContext &ctx, ecs::Entity entity, const String &text);

void SetHidden(UiContext &ctx, ecs::Entity entity, bool hidden);

/// Libellé discret (titre de section, légende).
ecs::Entity Caption(UiContext &ctx, ecs::Entity parent, const String &text, float size = 12.f);

/// Élément extensible qui pousse la suite d'une rangée vers la droite.
ecs::Entity Spacer(UiContext &ctx, ecs::Entity parent);

/// Icône seule, sans interaction (repère visuel d'une ligne).
ecs::Entity Glyph(UiContext &ctx, ecs::Entity parent, ui::MaterialIcons icon, sdl3::FColor color,
						 float size = 16.f);

/// Bouton d'icône plat (≡, +, ⋮, ←…) : fond transparent au repos, survol
/// visible. `outIcon` rend l'entité de l'icône pour les boutons dont le
/// glyphe change (lecture/arrêt).
ecs::Entity IconButton(UiContext &ctx, ecs::Entity parent, ui::MaterialIcons icon, const String &tooltip,
							  std::function<void()> action, float size = 24.f, ecs::Entity *outIcon = nullptr,
							  sdl3::FColor tint = sdl3::FColor{});

/// Champ de recherche avec loupe, rapporte chaque frappe.
ecs::Entity SearchField(UiContext &ctx, ecs::Entity parent, const String &placeholder,
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
	ecs::Entity Vector3(ecs::Entity page, const char *label, math::FVector3 value, float speed,
						std::function<void(math::FVector3)> onChange, float minValue = -1.0e5f,
						float maxValue = 1.0e5f);

	/// Vecteur à deux composantes (2D : X, Y).
	ecs::Entity Vector2(ecs::Entity page, const char *label, math::FVector2 value, float speed,
						std::function<void(math::FVector2)> onChange, float minValue = -1.0e5f,
						float maxValue = 1.0e5f);

	ecs::Entity Number(ecs::Entity page, const char *label, float value, float minValue, float maxValue, float speed,
					   std::function<void(float)> onChange, int decimals = 2);

	ecs::Entity Slider(ecs::Entity page, const char *label, float value, float minValue, float maxValue,
					   std::function<void(float)> onChange);

	ecs::Entity Check(ecs::Entity page, const char *label, bool value, std::function<void(bool)> onToggle);

	ecs::Entity Choice(ecs::Entity page, const char *label, std::vector<String> items, int selected,
					   std::function<void(int)> onChange);

	/// Texte : validé à l'Entrée (un renommage ne doit pas partir à chaque
	/// frappe).
	ecs::Entity Text(ecs::Entity page, const char *label, const String &value, std::function<void(const String &)> onSubmit);

	ecs::Entity ReadOnly(ecs::Entity page, const char *label, const String &value);

	/**
	 * Couleur : une barre de la couleur (comme dans les maquettes) + la
	 * pipette qui ouvre le sélecteur complet (teinte, saturation, hexa).
	 * Le sélecteur est un popup créé à la demande et retenu dans `popups`
	 * pour être détruit avec la page (c'est une racine indépendante, hors du
	 * sous-arbre de la page).
	 */
	ecs::Entity Color(ecs::Entity page, const char *label, sdl3::Color value, std::function<void(sdl3::Color)> onChange,
					  std::vector<ecs::Entity> &popups) {
		ecs::Entity row = Row(page, label);
		ui::WidgetBuilder bar = m_ctx.factory.Button(String());
		bar.GrowW().H(ui::Dimension::Px(20.f)).Radius(3.f).Bg(FromColor(value)).Tooltip(String("Choisir la couleur"));
		bar.Parent(row);
		ecs::Entity barEntity = bar.Spawn();

		UiContext *ctx = &m_ctx;
		ui::WidgetBuilder picker = m_ctx.factory.ColorPicker(FromColor(value), [ctx, barEntity, onChange](sdl3::FColor c) {
			c.a = 1.f;
			SetBackground(*ctx, barEntity, c);
			onChange(ToColor(c));
		});
		picker.OverlayOrder(20);
		ecs::Entity pickerEntity = picker.Spawn();
		popups.push_back(pickerEntity);

		auto open = [ctx, barEntity, pickerEntity] {
			if (auto computed = ctx->registry.GetComponent<ui::UiComputed>(barEntity); computed.IsSome()) {
				const sdl3::FRect r = computed.Unwrap()->screen;
				ctx->gui.OpenPopupAt(pickerEntity, sdl3::FPoint{r.x, r.y + r.h + 2.f});
			}
		};
		if (auto callbacks = m_ctx.registry.GetComponent<ui::UiCallbacks>(barEntity); callbacks.IsSome())
			callbacks.Unwrap()->onClick = open;
		(void)IconButton(m_ctx, row, ui::MaterialIcons::COLORIZE, String("Sélecteur de couleur"), open, 22.f);
		return row;
	}

	/// Rangée nue « libellé | … » dans laquelle l'appelant place ses champs.
	ecs::Entity Row(ecs::Entity page, const char *label);

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
 * Pourquoi pas `ui::UiTabView` : il dessine ses onglets lui-même, sans place
 * pour un × par onglet, un menu ≡ ou des boutons dans le bandeau, qui sont
 * justement ce qui fait un panneau d'éditeur (cf. les maquettes).
 */
class DockPanel {
public:
	explicit DockPanel(UiContext &ctx) : m_ctx(ctx) {}

	/// Construit le cadre sous `parent`. `size` pose les dimensions (le
	/// panneau ne décide pas de sa place dans la mise en page).
	ecs::Entity Build(ecs::Entity parent, const std::function<void(ui::WidgetBuilder &)> &size);

	/// Ajoute une page ; rend le conteneur où placer son contenu. `scroll` :
	/// contenu défilable (listes, inspecteur) ; sinon la page remplit la zone
	/// (vue 3D, éditeur de code, qui gèrent leur défilement eux-mêmes).
	ecs::Entity AddPage(const String &title, bool scroll, bool closable = false, float pad = 6.f);

	/// Retire une page (et son contenu).
	void RemovePage(int index);

	void SetActive(int index);

	void SetTitle(int index, const String &title);

	[[nodiscard]] int Active() const noexcept { return m_active; }
	[[nodiscard]] int Count() const noexcept { return int(m_pages.size()); }
	[[nodiscard]] ecs::Entity Page(int index) const;
	[[nodiscard]] ecs::Entity Frame() const noexcept { return m_frame; }
	/// Zone du bandeau, à droite des onglets, pour les boutons du panneau.
	[[nodiscard]] ecs::Entity HeaderExtra() const noexcept { return m_extra; }

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
		ecs::Entity content{};
		bool closable = false;
	};

	void RebuildTabs();

	UiContext &m_ctx;
	ecs::Entity m_frame{}, m_header{}, m_tabs{}, m_extra{}, m_menuButton{}, m_body{};
	std::vector<PageInfo> m_pages;
	int m_active = -1;
};

} // namespace kit
} // namespace game_editor
