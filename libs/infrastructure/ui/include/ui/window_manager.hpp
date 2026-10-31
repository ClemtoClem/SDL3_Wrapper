#pragma once
/**
 * ui::WindowManager — plusieurs fenêtres SDL3, chacune avec SA propre
 * interface.
 *
 * Chaque fenêtre (`WindowContext`) possède :
 *  - sa fenêtre SDL et son renderer (`Present()` par fenêtre) ;
 *  - son monde ECS (`ecs::ArchetypeRegistry`) et son `ui::Ui` : un monde PAR
 *    fenêtre, jamais un monde partagé (sinon les requêtes des systèmes ui
 *    traverseraient les fenêtres) ;
 *  - ses feuilles CSS (isolées : une feuille chargée dans le studio n'atteint
 *    pas la fenêtre de jeu) ;
 *  - son échelle de contenu (HiDPI par écran : `rem` mis à l'échelle) ;
 *  - son focus clavier, sa capture du pointeur et du clavier.
 *
 * GPU : le gestionnaire POSSÈDE un seul `SDL_GPUDevice` ; chaque fenêtre
 * « gpu » reçoit un renderer sur ce device (SDL_CreateGPURenderer : une
 * swapchain par fenêtre). Un seul `render3d::Canvas` (créé sur le renderer
 * de n'importe quelle fenêtre) sert alors toutes les fenêtres, et une vue
 * 3D DIRECT (texture GPU importée) peut s'afficher dans l'une ou l'autre.
 * Le device est détruit en DERNIER (après toutes les fenêtres) : un Canvas
 * de l'application doit être détruit avant le gestionnaire.
 *
 * Évènements : `Dispatch` route chaque évènement vers la fenêtre désignée
 * par son `windowID` (souris, clavier, texte, fenêtre…), à défaut vers la
 * fenêtre active (manettes, joysticks). Les raccourcis GLOBAUX (Ctrl+Q,
 * Ctrl+Shift+R, F3, F9…) sont essayés avant l'interface, dans TOUTES les
 * fenêtres, et reçoivent la fenêtre d'origine — routage explicite, jamais de
 * variable globale. Quand un champ de texte a le focus, seuls passent les
 * raccourcis que `DefaultKeyPassThrough` laisse passer (F1–F24, Ctrl/Alt
 * hors édition).
 *
 * Fermeture : une demande de fermeture (croix, Alt+F4) est notée sur la
 * fenêtre ; `HandleCloseRequests` ferme les fenêtres secondaires (en
 * appelant d'abord leur `onClose` : l'application y détache ce qui y était
 * attaché) et demande l'arrêt si la fenêtre PRINCIPALE ou la dernière est
 * fermée. Rien ne quitte de soi-même : l'hôte lit `QuitRequested()`.
 *
 * Sans écran : `WindowSpec::headless` crée un contexte sans fenêtre SDL
 * (interface sans rendu) — mêmes règles de routage, utile aux tests.
 *
 * @code{.cpp}
 * ui::WindowManager windows;
 * auto studio = windows.Create({.title = "Studio", .role = "studio", .primary = true});
 * auto game = windows.CreateOnDisplay(1, {.title = "Jeu", .role = "game"});
 * windows.AddGlobalShortcut({SDLK_Q, SDL_KMOD_CTRL, "quit", [&](ui::WindowContext &) { windows.RequestQuit(); }});
 * while (!windows.QuitRequested()) {
 *     while (auto ev = sdl3::PollEvent())
 *         windows.Dispatch(ev.Value());
 *     windows.HandleCloseRequests();
 *     windows.Update(dt);
 *     windows.Render();
 * }
 * @endcode
 */
#include <functional>
#include <memory>
#include <vector>

#include "ui.hpp"

namespace ui {

/// Identifiant d'une fenêtre du gestionnaire (jamais réutilisé ; 0 = aucune).
using WindowId = uint32_t;

struct WindowSpec {
	String title = "Fenêtre";
	int width = 1280, height = 720;
	/// Position dans le bureau virtuel ; NONE : centrée sur l'écran cible.
	Option<sdl3::Point> position = NONE;
	/// Indice dans sdl3::GetDisplays() ; NONE : écran principal.
	Option<size_t> display = NONE;
	/// RESIZABLE, BORDERLESS, FULLSCREEN, HIGH_PIXEL_DENSITY, ALWAYS_ON_TOP…
	sdl3::WindowFlags flags = sdl3::window_flags::RESIZABLE;
	/// "gpu" : renderer sur le device partagé du gestionnaire ; autre nom
	/// (« software », « opengl »…) : renderer indépendant, sans vue 3D.
	String renderDriver = "gpu";
	/// Synchronisation verticale. Une seule fenêtre devrait attendre le
	/// balayage (la principale) : N fenêtres synchronisées attendraient N fois.
	bool vsync = true;
	Theme theme = Theme::Dark;
	/// Feuilles propres à la fenêtre (chemins), puis CSS en ligne.
	std::vector<String> styleSheets{};
	String css{};
	/// Rôle libre (« studio », « game », « inspector »…) : rapport, raccourcis.
	String role{};
	/// Fermer la fenêtre principale arrête l'application.
	bool primary = false;
	/// Suit l'échelle de l'écran (changement d'écran, réglage système).
	bool followDisplayScale = true;
	/// Sans fenêtre SDL : interface sans rendu (tests, mode sans écran).
	bool headless = false;
};

class WindowManager;

class WindowContext {
public:
	WindowContext(const WindowContext &) = delete;
	WindowContext &operator=(const WindowContext &) = delete;
	~WindowContext();

	[[nodiscard]] WindowId Id() const noexcept { return m_id; }
	[[nodiscard]] const WindowSpec &Spec() const noexcept { return m_spec; }
	[[nodiscard]] const String &Role() const noexcept { return m_spec.role; }
	[[nodiscard]] bool IsPrimary() const noexcept { return m_spec.primary; }
	[[nodiscard]] bool IsHeadless() const noexcept { return !m_window; }
	/// Identifiant porté par les évènements destinés à cette fenêtre
	/// (SDL_WindowID ; synthétique pour un contexte sans fenêtre).
	[[nodiscard]] SDL_WindowID RoutingId() const noexcept { return m_routingId; }

	/// nullptr sans fenêtre SDL.
	[[nodiscard]] sdl3::Window *Window() noexcept { return m_window.get(); }
	[[nodiscard]] sdl3::Renderer *Renderer() noexcept { return m_renderer.get(); }
	[[nodiscard]] ecs::ArchetypeRegistry &World() noexcept { return m_world; }
	[[nodiscard]] Ui &Gui() noexcept { return *m_gui; }
	[[nodiscard]] Widget Root() { return m_gui->Root(); }
	/// Position dans le bureau virtuel : celle demandée à la création, puis
	/// celle des évènements WINDOW_MOVED (un pilote qui ne déplace pas ses
	/// fenêtres — offscreen — garde la disposition voulue par l'application).
	[[nodiscard]] sdl3::Point DesktopPosition() const noexcept { return m_desktopPosition; }
	/// Renderer sur le device GPU partagé du gestionnaire.
	[[nodiscard]] bool SharesGpuDevice() const noexcept { return m_sharesGpu; }

	// ── Échelle (HiDPI) ─────────────────────────────────────────────────────
	[[nodiscard]] float ContentScale() const noexcept { return m_contentScale; }
	/// Met `rem` à l'échelle (taille de police racine × échelle) et restyle.
	void SetContentScale(float scale);

	// ── Saisie locale ───────────────────────────────────────────────────────
	/// Un champ de texte de CETTE fenêtre a le focus clavier.
	[[nodiscard]] bool TextFieldHasFocus() const;
	/// Un glisser ou un appui de CETTE fenêtre tient le pointeur.
	[[nodiscard]] bool PointerCapture() const;
	/// Le clavier est réservé à cette fenêtre (champ de texte, ou réservation
	/// explicite de l'application : vol libre, peinture…).
	[[nodiscard]] bool KeyboardCapture() const { return m_keyboardCapture || TextFieldHasFocus(); }
	void SetKeyboardCapture(bool captured) noexcept { m_keyboardCapture = captured; }

	// ── Fermeture ───────────────────────────────────────────────────────────
	[[nodiscard]] bool CloseRequested() const noexcept { return m_closeRequested; }
	void RequestClose() noexcept { m_closeRequested = true; }
	void ClearCloseRequest() noexcept { m_closeRequested = false; }
	/// Appelé juste avant la destruction : détacher ce que l'application y a
	/// attaché (vue 3D, panneaux, références au monde ECS de la fenêtre).
	std::function<void(WindowContext &)> onClose{};

private:
	friend class WindowManager;
	WindowContext(WindowId id, WindowSpec spec) : m_id(id), m_spec(std::move(spec)) {}

	WindowId m_id;
	WindowSpec m_spec;
	SDL_WindowID m_routingId = 0;
	// Ordre de destruction (inverse de la déclaration) : interface, monde,
	// renderer, fenêtre.
	std::unique_ptr<sdl3::Window> m_window;
	std::unique_ptr<sdl3::Renderer> m_renderer;
	ecs::ArchetypeRegistry m_world;
	std::unique_ptr<Ui> m_gui;
	sdl3::Point m_desktopPosition{0, 0};
	float m_contentScale = 1.f;
	bool m_sharesGpu = false;
	bool m_keyboardCapture = false;
	bool m_closeRequested = false;
};

/// Raccourci valable dans toutes les fenêtres.
struct GlobalShortcut {
	SDL_Keycode key = SDLK_UNKNOWN;
	/// Modificateurs exigés (Ctrl/Maj/Alt/GUI, gauche ou droite indifférent) ;
	/// les modificateurs non cités doivent être relâchés.
	SDL_Keymod mods = SDL_KMOD_NONE;
	String name{};
	std::function<void(WindowContext &origin)> action{};
};

/// Résultat du routage d'un évènement.
struct DispatchResult {
	WindowContext *target = nullptr; ///< fenêtre destinataire (nullptr : aucune)
	bool consumed = false;            ///< pris par un raccourci ou l'interface
	String shortcut{};                ///< nom du raccourci global déclenché
};

class WindowManager {
public:
	WindowManager() = default;
	WindowManager(const WindowManager &) = delete;
	WindowManager &operator=(const WindowManager &) = delete;
	~WindowManager();

	// ── Fenêtres ────────────────────────────────────────────────────────────
	/// Crée une fenêtre (SDL vidéo initialisé, sauf `headless`).
	Result<WindowContext *, String> Create(WindowSpec spec);
	/// Crée la fenêtre sur l'écran `displayIndex` (sdl3::GetDisplays()),
	/// centrée sur lui ; écran inexistant : écran principal.
	Result<WindowContext *, String> CreateOnDisplay(size_t displayIndex, WindowSpec spec);
	/// Ferme et détruit (onClose d'abord). Faux si la fenêtre n'existe pas.
	bool Close(WindowId id);
	/// Donne le focus (et passe au premier plan).
	bool Focus(WindowId id);

	[[nodiscard]] WindowContext *Find(WindowId id) noexcept;
	[[nodiscard]] WindowContext *FindByRole(StringView role) noexcept;
	[[nodiscard]] WindowContext *FindByRoutingId(SDL_WindowID routingId) noexcept;
	/// Fenêtres ouvertes, dans l'ordre de création.
	[[nodiscard]] std::vector<WindowContext *> Windows();
	[[nodiscard]] size_t Count() const noexcept { return m_windows.size(); }
	/// Fenêtre qui a le focus (à défaut : la principale, puis la première).
	[[nodiscard]] WindowContext *ActiveWindow() noexcept;

	/// Device GPU partagé (nullptr tant qu'aucune fenêtre « gpu » n'existe).
	[[nodiscard]] SDL_GPUDevice *GpuDevice() const noexcept;

	// ── Évènements ──────────────────────────────────────────────────────────
	void AddGlobalShortcut(GlobalShortcut shortcut);
	bool RemoveGlobalShortcut(StringView name);
	/// Route l'évènement : raccourcis globaux, puis interface de la fenêtre.
	DispatchResult Dispatch(sdl3::Event &ev);
	/// Tenue du registre seule (focus, déplacement, échelle d'écran, demande
	/// de fermeture, SDL_EVENT_QUIT), sans raccourci ni transmission — pour une
	/// fenêtre dont l'application traite elle-même les évènements. Rend la
	/// fenêtre destinataire (nullptr : aucune).
	WindowContext *Observe(const sdl3::Event &ev);
	/// Fenêtre destinataire, sans rien transmettre (une fenêtre dont
	/// l'application traite elle-même les évènements : jeu, console…).
	[[nodiscard]] WindowContext *Target(const sdl3::Event &ev);
	/// Essaie les raccourcis globaux pour `origin` ; vrai si l'un a été déclenché.
	bool TriggerShortcut(sdl3::Event &ev, WindowContext &origin, DispatchResult *out = nullptr);

	/// Ferme les fenêtres secondaires qui l'ont demandé ; principale ou
	/// dernière fenêtre : arrêt demandé. Rend le nombre de fenêtres fermées.
	size_t HandleCloseRequests();
	[[nodiscard]] bool QuitRequested() const noexcept { return m_quit; }
	void RequestQuit() noexcept { m_quit = true; }

	// ── Image ───────────────────────────────────────────────────────────────
	void Update(float dt);
	/// Pour chaque fenêtre rendue : effacement, `draw` (avant l'interface :
	/// fond, scène), interface, captures demandées, `Present()`.
	void Render(sdl3::FColor clear = {0.f, 0.f, 0.f, 1.f},
				const std::function<void(WindowContext &)> &draw = {});
	/// Render en deux temps : dessin et captures de toutes les fenêtres, puis
	/// présentation — l'hôte glisse entre les deux ce qui doit précéder
	/// l'attente du balayage (lancer le pas de simulation suivant…).
	void Draw(sdl3::FColor clear = {0.f, 0.f, 0.f, 1.f}, const std::function<void(WindowContext &)> &draw = {});
	void Present();

	// ── Captures (prises dans Render, avant Present) ────────────────────────
	void RequestCapture(WindowId id, String path);
	/// Bureau entier : toutes les fenêtres composées à leur position.
	void RequestDesktopCapture(String path);
	/// Erreurs des captures de la dernière image (vide : tout est écrit).
	[[nodiscard]] const std::vector<String> &CaptureErrors() const noexcept { return m_captureErrors; }

private:
	Result<bool, String> EnsureGpuDevice();
	void ComposeDesktop();

	std::vector<std::unique_ptr<WindowContext>> m_windows;
	std::vector<GlobalShortcut> m_shortcuts;
	SDL_GPUDevice *m_gpu = nullptr; ///< possédé, détruit après les fenêtres
	WindowId m_nextId = 1;
	WindowId m_active = 0;
	bool m_quit = false;

	struct PendingCapture {
		WindowId id;
		String path;
	};
	std::vector<PendingCapture> m_captures;
	std::vector<String> m_desktopCaptures;
	struct Shot {
		SDL_Surface *pixels;
		sdl3::Point position;
	};
	std::vector<Shot> m_desktopShots; ///< entre Draw et Present
	std::vector<String> m_captureErrors;
};

/// Identifiant de fenêtre porté par un évènement SDL (0 : aucun).
[[nodiscard]] SDL_WindowID EventWindowId(const sdl3::Event &ev) noexcept;

} // namespace ui
