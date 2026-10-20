#pragma once
/**
 * ui::WindowFrame — encadrement de fenêtre complet en un appel : barre de
 * titre (icône d'application, titre, poignée de déplacement, réduire /
 * agrandir-restaurer / fermer), zone de contenu, barre d'état avec poignée de
 * redimensionnement, hit-test système (déplacer par la barre, redimensionner
 * par les bords et la poignée) et police d'icônes MaterialIcons.
 *
 * La fenêtre doit être créée SANS décoration (`WindowFrame::WINDOW_FLAGS`) :
 *
 * @code{.cpp}
 * auto window = sdl3::Window::Create("Titre", 1280, 720, ui::WindowFrame::WINDOW_FLAGS);
 * ...
 * ui::WindowFrame frame;
 * frame.Build(gui, window, {.title = "Titre", .appIcon = Some(ui::MaterialIcons::DASHBOARD)});
 * f.Panel().Parent(frame.Content())...;      // le contenu de l'application
 * while (running) {
 *     ...
 *     frame.Update();                         // hit-test, icône agrandir
 *     if (frame.CloseRequested()) running = false;
 * }
 * @endcode
 *
 * Le contenu est une colonne qui occupe tout l'espace entre les deux barres
 * (`ContentRect()` donne son rectangle à l'écran — pour un rendu dessiné à la
 * main, via SDL_SetRenderViewport par exemple).
 *
 * Deux constructions : à partir de la façade `ui::Ui`, ou des systèmes pris
 * séparément (UiFactory + LayoutSystem + RenderSystem) pour les applications
 * qui assemblent leur pipeline elles-mêmes.
 */
#include "chrome.hpp"
#include "systems.hpp"

#include <functional>

namespace ui {

class Ui;

struct WindowFrameOptions {
	String title{};
	/// Icône d'application avant le titre (NONE : aucune).
	Option<MaterialIcons> appIcon = NONE;
	/// Message initial de la barre d'état.
	String status{};
	bool statusBar = true;
	bool moveHandle = true;
	bool minimizeButton = true;
	bool maximizeButton = true;
	/// Fermer : appelé en plus de CloseRequested() (NONE : rien d'autre).
	std::function<void()> onClose{};
	/// Fond de toute la fenêtre (alpha nul : aucun — l'application efface
	/// elle-même, ou un thème verre reste visible).
	sdl3::FColor background{};
	/// Fond de la barre de titre (alpha nul : `panelBg` du thème, opaque).
	sdl3::FColor titleBackground{};
	/// Fond de la barre d'état (alpha nul : aucun).
	sdl3::FColor statusBackground{};
	/// false : barre de titre sans fond (fenêtres « verre » Aero).
	bool fillTitleBar = true;
	float titleHeight = 36.f;
	float statusHeight = 24.f;
	/// Marge intérieure de la zone de contenu.
	float contentPadding = 0.f;
	/// Écart entre les enfants de la zone de contenu.
	float contentGap = 0.f;
};

class WindowFrame {
public:
	/// Drapeaux de création de la fenêtre : sans décoration système,
	/// redimensionnable.
	static constexpr sdl3::WindowFlags WINDOW_FLAGS =
		sdl3::window_flags::BORDERLESS | sdl3::window_flags::RESIZABLE;

	WindowFrame() = default;
	WindowFrame(const WindowFrame&) = delete;
	WindowFrame& operator=(const WindowFrame&) = delete;
	~WindowFrame();

	/// Construit l'encadrement (racine plein écran) et branche le hit-test.
	/// Charge et enregistre la police MaterialIcons si elle ne l'est pas déjà
	/// (repli sur des glyphes texte si elle est introuvable).
	void Build(Ui& gui, sdl3::Window& window, WindowFrameOptions options);
	void Build(UiFactory& factory, LayoutSystem& layout, RenderSystem& render, sdl3::Window& window,
			   WindowFrameOptions options);

	/// À appeler à chaque image : hit-test (après layout) et icône
	/// agrandir / restaurer selon l'état réel de la fenêtre.
	void Update();

	/// Le bouton Fermer a été cliqué.
	[[nodiscard]] bool CloseRequested() const noexcept { return m_closeRequested; }
	void ClearCloseRequest() noexcept { m_closeRequested = false; }
	/// Demande la fermeture comme le bouton × (menu « Quitter »…).
	void RequestClose() noexcept { m_closeRequested = true; }

	[[nodiscard]] ecs::Entity Root() const noexcept { return m_root; }
	/// Parent du contenu de l'application.
	[[nodiscard]] ecs::Entity Content() const noexcept { return m_content; }
	/// Rectangle de la zone de contenu à l'écran (après le dernier layout).
	[[nodiscard]] sdl3::FRect ContentRect() const;

	[[nodiscard]] const TitleBarWidgets& TitleBar() const noexcept { return m_titleBar; }
	[[nodiscard]] const StatusBarWidgets& StatusBar() const noexcept { return m_statusBar; }
	[[nodiscard]] WindowChrome& Chrome() noexcept { return m_chrome; }
	/// La police d'icônes est disponible (enregistrée par l'application ou
	/// chargée par Build).
	[[nodiscard]] bool HasIcons() const noexcept { return m_icons; }

	void SetTitle(const String& text);
	/// Message de la barre d'état ; couleur alpha nul : `muted` du thème.
	void SetStatus(const String& text, sdl3::FColor color = {});

private:
	ecs::ArchetypeRegistry* m_world = nullptr;
	LayoutSystem* m_layout = nullptr;
	sdl3::Window* m_window = nullptr;
	Option<sdl3::Font> m_iconFont; ///< police chargée par Build (sinon celle de l'application)
	WindowChrome m_chrome;
	TitleBarWidgets m_titleBar;
	StatusBarWidgets m_statusBar;
	ecs::Entity m_root{};
	ecs::Entity m_content{};
	bool m_icons = false;
	bool m_closeRequested = false;
};

} // namespace ui
