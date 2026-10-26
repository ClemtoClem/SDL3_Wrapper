#pragma once
/**
 * ui::theme — feuilles de style du module.
 *
 *  - `StructureCss()` : mise en page par défaut de chaque type de widget
 *    (direction, alignements, débordement, curseurs) — indépendante du thème ;
 *  - `ThemeCss(palette)` : apparence (couleurs, bordures, rayons, tailles)
 *    écrite avec des propriétés personnalisées (`var(--accent)`…) posées sur
 *    la racine : une application peut ne redéfinir que la palette dans sa
 *    propre feuille (`root { --accent: #e0a030; }`).
 *
 * Ces deux feuilles ont la priorité la plus basse de la cascade : n'importe
 * quelle feuille de l'application, ou n'importe quel style posé par un
 * builder, les emporte.
 */
#include "css.hpp"

namespace ui {

enum class Theme : uint8_t {
	Dark,	///< bleuté, contrasté (applications)
	Light,	///< clair
	Studio, ///< gris neutres d'éditeur de jeu (Godot, Unity…)
	Aero,	///< verre translucide façon Windows 7
};

/// Palette d'un thème (toutes les couleurs que la feuille du thème utilise).
struct Palette {
	Color appBg, panel, surface, field, hover, pressed;
	Color border, borderStrong;
	Color text, muted, onAccent;
	Color accent, accentHover, accentPressed;
	Color danger, success, warning;
	Color scrollThumb, scrollTrack;
	float radius = 4.f;
	float fontSize = 14.f;
	bool glass = false;

	[[nodiscard]] static Palette Of(Theme theme);
};

[[nodiscard]] String StructureCss();
[[nodiscard]] String ThemeCss(const Palette &palette);
[[nodiscard]] inline String ThemeCss(Theme theme) { return ThemeCss(Palette::Of(theme)); }

/// Texte CSS d'une couleur (`#rrggbbaa`).
[[nodiscard]] String CssColor(Color c);

} // namespace ui
