#pragma once
/**
 * editor — briques communes des éditeurs 3D du dépôt (sandbox, Voxelia
 * Studio) : le CADRE de l'éditeur (palette dérivée du thème ui::, feuille de
 * style `.editor-*` / `.dock-*`, boutons d'icône plats, champ de recherche,
 * poignées de redimensionnement), les docks à onglets (dock.hpp), la
 * navigation orbitale (navigation.hpp), le gizmo de transformation
 * (gizmo.hpp) et la palette de commandes (command_palette.hpp).
 *
 * Les couleurs du cadre ne sont jamais codées en dur : elles découlent du
 * thème actif ; changer de thème repeint donc toute l'application.
 */
#include <functional>

#include <core/core.hpp>
#include <ui/ui.hpp>

namespace editor {

// Couleurs de CODE (indépendantes du thème : l'axe X est rouge partout).
inline constexpr sdl3::FColor AXIS_X{0.90f, 0.33f, 0.30f, 1.f};
inline constexpr sdl3::FColor AXIS_Y{0.45f, 0.78f, 0.32f, 1.f};
inline constexpr sdl3::FColor AXIS_Z{0.30f, 0.52f, 0.95f, 1.f};
/// Poignée survolée ou saisie (gizmo).
inline constexpr sdl3::FColor HIGHLIGHT{1.f, 0.85f, 0.25f, 1.f};

[[nodiscard]] sdl3::FColor Rgb(int r, int g, int b, int a = 255) noexcept;
[[nodiscard]] inline sdl3::FColor FromColor(const sdl3::Color &c) noexcept { return Rgb(c.r, c.g, c.b, c.a); }
[[nodiscard]] sdl3::Color ToColor(const sdl3::FColor &c) noexcept;
/// Luminance relative (Rec. 709) — tranche clair/sombre.
[[nodiscard]] float Luminance(const sdl3::FColor &c) noexcept;
/// `a` vers `b` d'une fraction `t`, rendue opaque.
[[nodiscard]] sdl3::FColor Mix(const sdl3::FColor &a, const sdl3::FColor &b, float t) noexcept;

/// Couleurs du « chrome » de l'éditeur, toutes dérivées du thème.
struct Palette {
	sdl3::FColor base;		///< fond de l'application, gouttières, poignées
	sdl3::FColor header;	///< bandeaux de docks, barre de menus, barre d'état
	sdl3::FColor toolbar;	///< barres d'outils
	sdl3::FColor well;		///< cadre des vignettes, fond des aperçus
	sdl3::FColor selection; ///< élément sélectionné, outil actif
	sdl3::FColor separator; ///< filets
	sdl3::FColor ok, warning, error;
	bool light = false; ///< fond clair : textes et icônes foncés
};

[[nodiscard]] Palette PaletteOf(const ui::Palette &theme);
/// Feuille de style de l'éditeur (classes `.editor-*`, `.dock-*`,
/// `.flat-icon`, `.prop-*`, `.search-field`, `.command-palette*`).
[[nodiscard]] String EditorCss(const ui::Palette &theme);
/// Teinte d'icône lisible sur le fond du thème (pastels foncés sur fond clair).
[[nodiscard]] sdl3::FColor Readable(const ui::Palette &theme, const sdl3::FColor &color);

// ── Petites briques ─────────────────────────────────────────────────────────

/// Montre / cache un widget (sans effet s'il est déjà dans cet état).
void SetHidden(ui::Widget widget, bool hidden);

/// Bouton d'icône plat (≡, +, ×…) : transparent au repos, survol visible.
/// `outIcon` rend l'icône (glyphe changeant : lecture/arrêt).
ui::Widget IconButton(const ui::Palette &theme, ui::Widget parent, ui::MaterialIcons icon, const String &tooltip,
					  std::function<void()> action, float size = 24.f, ui::Widget *outIcon = nullptr,
					  sdl3::FColor tint = sdl3::FColor{});

/// Champ de recherche avec loupe ; rend le champ, rapporte chaque frappe.
ui::Widget SearchField(const ui::Palette &theme, ui::Widget parent, const String &placeholder,
					   std::function<void(const String &)> onChange, float width = -1.f);

/// Poignée de redimensionnement (5 px) entre deux panneaux d'une rangée
/// (`Horizontal`) ou d'une colonne (`Vertical`). `before` : elle SUIT le
/// panneau `target` (ancré à gauche / en haut) ; sinon elle le PRÉCÈDE
/// (panneau ancré à droite / en bas).
ui::Widget ResizeHandle(const ui::Palette &theme, ui::Widget parent, ui::Orientation orientation, ui::Widget target,
						bool before, float minSize, float maxSize);

} // namespace editor
