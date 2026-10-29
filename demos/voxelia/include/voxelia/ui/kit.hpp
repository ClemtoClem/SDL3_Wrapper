#pragma once
/**
 * Système de design de l'interface (« Voxelia ») : palette, typographie,
 * espacements, composants. Tous les écrans le partagent.
 *
 * Langage graphique : panneaux sombres légèrement bleutés, coins arrondis
 * modérés (6 px les commandes, 10 px les cartes), liseré inférieur plus
 * sombre sous les boutons (relief « bloc »), un seul vert d'herbe pour
 * l'action principale, l'ambre pour la progression, le rouge redstone pour
 * le danger. Titres en police pixel (police bitmap du pack), texte courant en
 * DejaVu Sans. Le HUD reprend la palette en plus discret (fonds translucides,
 * pas de cadres).
 *
 * Grille : contenu centré de 1080 px au plus, colonnes de 12 ; espacements
 * de 4 / 8 / 12 / 16 / 24 / 32 px.
 *
 * Le système est une FEUILLE CSS générée depuis la palette et les échelles
 * (Kit::Css) : variables `--vx-*`, classes des composants (`.vx-button`,
 * `.vx-card`, `.vx-menu-button`…), tons (`.tone-primary`…), rôles
 * typographiques (`.text-h1`…) et états (`:hover`, `:active`, `.on`). Les
 * builders ci-dessous ne font que poser ces classes : changer de thème
 * (contraste, daltonisme, échelle) régénère la feuille, sans reconstruire.
 */
#include <functional>
#include <vector>

#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

#include "../settings.hpp"

namespace voxelia {

struct Palette {
	sdl3::FColor bg0, bg1, bg2, bg3;        ///< fond, panneau, surélevé, survol
	sdl3::FColor line, lineStrong;          ///< séparateurs, bordures
	sdl3::FColor text, textMuted, textDim;
	sdl3::FColor primary, primaryHover, primaryPressed, onPrimary; ///< vert d'herbe
	sdl3::FColor secondary;                 ///< bleu ciel (information, liens)
	sdl3::FColor accent;                    ///< ambre (progression, expérience)
	sdl3::FColor danger, warning, success, info;
	sdl3::FColor scrim;                     ///< voile derrière les fenêtres modales
	// HUD
	sdl3::FColor health, hunger, stamina, xp, air, armor;
};

/// Rôles typographiques (tailles avant l'échelle de texte de l'accessibilité).
enum class Text { DISPLAY, H1, H2, H3, BODY, SMALL, CAPTION };

enum class Tone { NEUTRAL, PRIMARY, INFO, SUCCESS, WARNING, DANGER, ACCENT };

/// Police bitmap 16×16 caractères (textures/fonts/ascii.png du pack), à
/// largeurs proportionnelles : logo, titres, chiffres du HUD.
class PixelFont {
public:
	bool Load(sdl3::Renderer &renderer, const String &path);
	[[nodiscard]] bool Ready() const noexcept { return m_texture.IsSome(); }
	/// Largeur d'un texte à l'échelle `scale` (1 : 8 px de haut).
	[[nodiscard]] float Measure(const String &text, float scale) const;
	/// Dessine avec une ombre portée (style jeu) ; `shadow` = 0 : sans ombre.
	void Draw(sdl3::Renderer &renderer, const String &text, float x, float y, float scale, sdl3::FColor color,
			  float shadow = 1.f) const;

private:
	Option<sdl3::Texture> m_texture = NONE;
	int m_cell = 8;
	std::array<int, 256> m_width{};
};

class Kit {
public:
	Kit(ui::Ui &ui, const Settings &settings);

	/// Reconstruit la palette et la feuille de style (contraste, échelles).
	void Apply();
	[[nodiscard]] const Palette &Colors() const noexcept { return m_palette; }
	[[nodiscard]] float Size(Text role) const;
	[[nodiscard]] float Scale() const noexcept { return m_settings.uiScale; }
	[[nodiscard]] ui::Ui &Ui() noexcept { return m_ui; }
	[[nodiscard]] PixelFont &Pixel() noexcept { return m_pixel; }
	[[nodiscard]] sdl3::FColor ToneColor(Tone tone) const;
	/// Feuille de style du système de design (régénérée par Apply).
	[[nodiscard]] String Css() const;

	// ── Mise en page ────────────────────────────────────────────────────────
	ui::WidgetBuilder Column(float gap = 8.f);
	ui::WidgetBuilder Row(float gap = 8.f);
	ui::WidgetBuilder Spacer();					 ///< espace extensible
	/// Carte : panneau arrondi (fond surélevé, bordure discrète).
	ui::WidgetBuilder Card(float pad = 16.f);
	/// Panneau d'écran plein (fond translucide sur le monde de fond).
	ui::WidgetBuilder Surface(float alpha = 0.92f);
	ui::WidgetBuilder Divider();
	/// Défilement vertical.
	ui::WidgetBuilder Scroll(float gap = 8.f);

	// ── Texte ───────────────────────────────────────────────────────────────
	ui::WidgetBuilder Label(String text, Text role = Text::BODY);
	ui::WidgetBuilder Muted(String text, Text role = Text::SMALL);
	ui::WidgetBuilder Heading(String text, Text role = Text::H2);
	/// Titre en police pixel (dessiné), hauteur en px.
	ui::WidgetBuilder PixelTitle(String text, float height, sdl3::FColor color, bool centered = false);
	ui::WidgetBuilder Paragraph(String text, Text role = Text::BODY);

	// ── Commandes ───────────────────────────────────────────────────────────
	ui::WidgetBuilder Button(String text, std::function<void()> fn, Tone tone = Tone::NEUTRAL);
	ui::WidgetBuilder PrimaryButton(String text, std::function<void()> fn) {
		return Button(std::move(text), std::move(fn), Tone::PRIMARY);
	}
	ui::WidgetBuilder DangerButton(String text, std::function<void()> fn) {
		return Button(std::move(text), std::move(fn), Tone::DANGER);
	}
	ui::WidgetBuilder GhostButton(String text, std::function<void()> fn);
	/// Bouton icône + texte (menus principaux), large.
	ui::WidgetBuilder MenuButton(ui::MaterialIcons icon, const String &text, const String &hint, std::function<void()> fn,
								 bool primary = false);
	ui::Widget MenuButton(ui::Widget parent, ui::MaterialIcons icon, const String &text, const String &hint,
						  std::function<void()> fn, bool primary = false) {
		return parent.Append(MenuButton(icon, text, hint, std::move(fn), primary));
	}
	/// Bouton carré à icône seule (barres d'outils).
	ui::WidgetBuilder IconButton(ui::MaterialIcons icon, String tooltip, std::function<void()> fn,
								 Tone tone = Tone::NEUTRAL);
	ui::WidgetBuilder Icon(ui::MaterialIcons icon, float size = 18.f, Option<sdl3::FColor> color = NONE);
	ui::WidgetBuilder Input(String placeholder, String value = String());
	ui::WidgetBuilder Toggle(bool value, std::function<void(bool)> fn);
	ui::WidgetBuilder Check(bool value, std::function<void(bool)> fn);
	ui::WidgetBuilder Slider(float min, float max, float value, float step, std::function<void(float)> fn);
	ui::WidgetBuilder Combo(std::vector<String> items, int selected, std::function<void(int)> fn);
	/// Pastille de statut (« Installé », « Incompatible »…).
	ui::WidgetBuilder Chip(String text, Tone tone);
	/// Onglets horizontaux (barre) : rend la rangée ; `onSelect(index)`.
	ui::Widget Tabs(ui::Widget parent, const std::vector<String> &tabs, int active, std::function<void(int)> onSelect);

	// ── Lignes de réglages ──────────────────────────────────────────────────
	/// Ligne « libellé + description | commande » ; `locked` : verrouillée (cadenas).
	ui::Widget SettingRow(ui::Widget parent, const String &label, const String &description, ui::WidgetBuilder control,
						  bool locked = false);
	ui::Widget ToggleRow(ui::Widget parent, const String &label, const String &description, bool value,
						 std::function<void(bool)> fn, bool locked = false);
	ui::Widget SliderRow(ui::Widget parent, const String &label, const String &description, float min, float max,
						 float value, float step, std::function<String(float)> format, std::function<void(float)> fn);
	ui::Widget ComboRow(ui::Widget parent, const String &label, const String &description, std::vector<String> items,
						int selected, std::function<void(int)> fn, bool locked = false);
	/// Commande créée par le dernier SettingRow (pour y ajouter des éléments).
	[[nodiscard]] ui::Widget LastControl() const noexcept { return m_lastControl; }
	/// Titre de section (petites capitales colorées + filet).
	ui::Widget SectionTitle(ui::Widget parent, const String &title, Option<ui::MaterialIcons> icon = NONE);

	// ── Fenêtres ────────────────────────────────────────────────────────────
	/// En-tête d'écran : bouton retour, titre, sous-titre, actions à droite.
	ui::Widget ScreenHeader(ui::Widget parent, const String &title, const String &subtitle, std::function<void()> back);
	/// Pied d'écran : boutons alignés à droite (le premier est le plus à gauche).
	ui::Widget Footer(ui::Widget parent);

	// ── Utilitaires ─────────────────────────────────────────────────────────
	void SetText(ui::Widget label, const String &text);
	void SetHidden(ui::Widget w, bool hidden);
	[[nodiscard]] String InputText(ui::Widget input) { return input.Text(); }
	void SetInputText(ui::Widget input, const String &text) { input.SetText(text); }
	void Destroy(ui::Widget w) { w.Destroy(); }
	/// Détruit les enfants d'un conteneur (listes rafraîchies sur place).
	void ClearChildren(ui::Widget w) { w.Clear(); }
	/// Couleur du texte d'un libellé existant.
	void SetColor(ui::Widget w, sdl3::FColor color) { w.Color(color); }

private:
	ui::Ui &m_ui;
	const Settings &m_settings;
	Palette m_palette{};
	PixelFont m_pixel;
	ui::StyleSheetPtr m_sheet;
	ui::Widget m_lastControl;
};

/// Couleur 0..255.
[[nodiscard]] sdl3::FColor Rgb(int r, int g, int b, int a = 255);
[[nodiscard]] sdl3::FColor WithAlpha(sdl3::FColor c, float a);

} // namespace voxelia
