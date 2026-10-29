#pragma once
/**
 * ui::chrome — barres de fenêtre dessinées par l'interface (fenêtres sans
 * décoration système) :
 *
 *  - `TitleBar` (`titlebar`) : icône `.titlebar-icon`, titre `.titlebar-title`,
 *    poignée `.titlebar-move`, boutons `.titlebar-button` (`#titlebar-minimize`,
 *    `#titlebar-maximize`, `#titlebar-close`) ;
 *  - `StatusBar` (`statusbar`) : message `.statusbar-text`, poignée
 *    `.statusbar-grip` ;
 *  - `WindowChrome` : hit-test système (déplacer par la barre de titre,
 *    redimensionner par les bords et les poignées) d'après la mise en page ;
 *  - `MakeFloatingPanel` : panneau flottant DANS la fenêtre (bureau simulé),
 *    déplacé / redimensionné par l'interface (MoveHandle).
 *
 * Tout se stylise en CSS (`titlebar { height: 40px; }`,
 * `#titlebar-close:hover { background-color: #c42b1c; }`).
 */
#include <functional>
#include <vector>

#include <sdl3/render.hpp>

#include "glyphs.hpp"
#include "widgets.hpp"

namespace ui {

// ============================================================================
// Barre de titre
// ============================================================================

struct TitleBarOptions {
	String title{};
	/// Icône d'application avant le titre (NONE : aucune).
	Option<MaterialIcons> appIcon = NONE;
	/// Poignée « déplacer » (indicative : toute la barre hors boutons déplace).
	bool moveHandle = true;
	bool minimizeButton = true;
	bool maximizeButton = true;
	/// Police MaterialIcons disponible ; sinon glyphes texte (—, □, ×, ✥).
	bool icons = true;
	/// Fermer : `onClose` si fourni, sinon `window.Hide()`.
	std::function<void()> onClose{};
	/// false : barre sans fond (classe `transparent` — verre Aero).
	bool fillBackground = true;
};

/// Barre de titre de `window` (`titlebar`, id `titlebar`). Agrandir bascule
/// selon l'état réel de la fenêtre.
[[nodiscard]] WidgetBuilder TitleBar(sdl3::Window &window, TitleBarOptions options);

/// Widgets d'une barre de titre matérialisée.
struct TitleBarWidgets {
	Widget root, appIcon, title, moveHandle, minimizeBtn, maximizeBtn, closeBtn;

	/// Retrouve les parties d'une barre (racine `titlebar` ou un ancêtre).
	[[nodiscard]] static TitleBarWidgets From(Widget bar);
	/// Boutons à exclure de la bande déplaçable.
	[[nodiscard]] std::vector<Widget> Buttons() const;
	/// Icône agrandir / restaurer selon l'état réel de la fenêtre (chaque image).
	void Update(const sdl3::Window &window) const;
	void SetTitle(String text) const;
};

// ============================================================================
// Barre d'état
// ============================================================================

struct StatusBarOptions {
	String text{};
	bool resizeGrip = true; ///< poignée ↘ du coin bas-droit
	bool icons = true;		///< glyphe MaterialIcons SOUTH_EAST, sinon ⇲
};

/// Barre d'état (`statusbar`, id `statusbar`).
[[nodiscard]] WidgetBuilder StatusBar(StatusBarOptions options = {});

struct StatusBarWidgets {
	Widget root, label, grip;

	[[nodiscard]] static StatusBarWidgets From(Widget bar);
	/// Message (et couleur ; NONE : celle des feuilles).
	void SetText(String text, Option<Color> color = NONE) const;
};

// ============================================================================
// WindowChrome — hit-test système d'une fenêtre sans décoration
// ============================================================================

/// Rectangles relevés après mise en page ; le rappel de hit-test (appelé par
/// SDL hors de la boucle) ne lit que ce cache, jamais l'ECS.
struct HitTestCache {
	sdl3::FRect titleBar{};
	std::vector<sdl3::FRect> excluded; ///< boutons : cliquables normalement
	std::vector<sdl3::FRect> grips;	   ///< poignées : coin bas-droit
	float resizeBorder = 6.f;
	float w = 0.f, h = 0.f;
	bool resizable = true;
};

class WindowChrome {
public:
	WindowChrome() = default;
	WindowChrome(const WindowChrome &) = delete;
	WindowChrome &operator=(const WindowChrome &) = delete;
	/// Retire le hit-test : la fenêtre doit encore exister.
	~WindowChrome() { Detach(); }

	/// `titleBar` : bande déplaçable ; `buttons` : exclus (restent cliquables).
	void Attach(sdl3::Window &window, Widget titleBar, std::vector<Widget> buttons = {}, bool resizable = true);
	void Attach(sdl3::Window &window, const TitleBarWidgets &titleBar, const StatusBarWidgets *statusBar = nullptr,
				bool resizable = true);
	/// Poignée visible : la saisir redimensionne par le coin bas-droit.
	void AddResizeGrip(Widget grip);
	void Detach();
	/// Chaque image : relève les rectangles si la mise en page a tourné.
	void Update();

	[[nodiscard]] const HitTestCache &Cache() const noexcept { return m_cache; }
	/// Résultat du hit-test pour un point de la fenêtre.
	[[nodiscard]] SDL_HitTestResult HitTest(const SDL_Point &p) const;

private:
	void Rebuild(bool force);

	sdl3::Window *m_window = nullptr;
	Widget m_titleBar;
	std::vector<Widget> m_buttons;
	std::vector<Widget> m_grips;
	HitTestCache m_cache;
	uint64_t m_lastPass = ~uint64_t(0);
};

// ============================================================================
// Panneau flottant (bureau simulé dans une seule fenêtre)
// ============================================================================

/// `panel` doit être positionné (absolute / fixed) avec une taille en
/// pixels ; glisser `handle` le déplace, glisser `grip` le redimensionne.
void MakeFloatingPanel(Widget handle, Option<Widget> grip = NONE, sdl3::FPoint minSize = {160.f, 100.f});

/// Bouton à icône MaterialIcons, ou texte `fallback` sans police d'icônes.
[[nodiscard]] WidgetBuilder ChromeButton(MaterialIcons glyph, String fallback, bool icons, String tooltip = String());

} // namespace ui
