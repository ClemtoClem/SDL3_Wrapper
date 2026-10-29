#pragma once
/**
 * Coquille de l'application : écrans (pile de navigation), fenêtres
 * modales, notifications, monde de fond des menus, session de jeu.
 *
 * Couches (de bas en haut) : vue 3D (monde des menus ou de la partie), HUD
 * de la partie, écrans, notifications. Chaque écran est reconstruit quand
 * il revient au premier plan : il reflète toujours les données courantes
 * (mondes, mods, réglages).
 */
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include <audio/engine.hpp>
#include <render3d/canvas.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

#include "app.hpp"
#include "catalog.hpp"
#include "settings.hpp"
#include "ui/kit.hpp"

namespace voxel {
class Game;
}

namespace voxelia {

class Shell;
class Session;
class WorldRenderer;

class Screen {
public:
	explicit Screen(Shell &shell);
	virtual ~Screen() = default;
	[[nodiscard]] virtual String Id() const = 0;
	/// Crée les widgets sous `root` (couche plein écran).
	virtual void Build(ui::Widget root) = 0;
	virtual void Update(float /*dt*/) {}
	/// Touche pressée (scancode) ; vrai si traitée.
	virtual bool OnKey(int /*scancode*/) { return false; }
	/// Échap / retour : vrai si l'écran l'a traité lui-même (sinon : Pop()).
	virtual bool OnBack() { return false; }
	/// Vrai : se superpose à l'écran précédent, qui reste affiché dessous.
	[[nodiscard]] virtual bool Overlay() const { return false; }
	/// Vrai : laisse la souris au jeu (aucun écran « bloquant »).
	[[nodiscard]] virtual bool PassThrough() const { return false; }

protected:
	Shell &shell;
	Kit &kit;
};

/// Demande de confirmation (suppression, sortie, changement important).
struct ConfirmSpec {
	String title, message;
	std::vector<String> details;     ///< liste à puces (conséquences)
	String confirmText = "Confirmer", cancelText = "Annuler";
	Tone tone = Tone::DANGER;
	ui::MaterialIcons icon = ui::MaterialIcons::WARNING;
	std::function<void()> onConfirm, onCancel;
	/// Case à cocher facultative (« Créer une sauvegarde de sécurité »).
	String checkboxText;
	bool checkboxValue = true;
	std::function<void(bool)> onCheckbox;
};

class Shell {
public:
	Shell(Options options, sdl3::Window &window, sdl3::Renderer &renderer, render3d::Canvas &canvas, ui::Ui &gui);
	~Shell();

	Result<bool, String> Initialize();
	[[nodiscard]] bool Running() const noexcept { return m_running; }
	void HandleEvent(sdl3::Event &e);
	void Update(float dt, long frame);
	/// Pas de simulation de la partie sur son fil (cf. Session::BeginSimulation).
	void BeginSimulation();
	/// Attend ce pas : à appeler avant tout accès au jeu (début d'image, sortie).
	void EndSimulation();
	/// Commande d'automatisation (--exec=N:!…) : « !screen nom », « !click texte »…
	void AppCommand(const String &line);
	/// Exécute une commande de jeu (console) si une partie est en cours.
	void GameCommand(const String &line);

	// ── Accès ───────────────────────────────────────────────────────────────
	[[nodiscard]] Settings &Prefs() noexcept { return m_settings; }
	[[nodiscard]] Kit &K() noexcept { return *m_kit; }
	[[nodiscard]] ui::Ui &Gui() noexcept { return m_gui; }
	[[nodiscard]] WorldCatalog &Worlds() noexcept { return m_worlds; }
	[[nodiscard]] ModCatalog &Mods() noexcept { return m_mods; }
	[[nodiscard]] Session *Game() noexcept { return m_session.get(); }
	[[nodiscard]] sdl3::Window &Window() noexcept { return m_window; }
	[[nodiscard]] sdl3::Renderer &Renderer() noexcept { return m_renderer; }
	[[nodiscard]] render3d::Canvas &Canvas() noexcept { return m_canvas; }
	[[nodiscard]] const Options &Opts() const noexcept { return m_options; }
	/// Jeu des menus (contenu de tous les mods installés : icônes, sons, skins).
	[[nodiscard]] voxel::Game *MenuGame() noexcept;
	[[nodiscard]] WorldRenderer *MenuRenderer() noexcept;
	[[nodiscard]] String Version() const { return String("0.6.0"); }
	[[nodiscard]] const std::vector<String> &RecentLog() const noexcept { return m_log; }
	void Log(const String &line);

	// ── Navigation ──────────────────────────────────────────────────────────
	void Push(std::unique_ptr<Screen> screen);
	void Pop();
	void Replace(std::unique_ptr<Screen> screen);
	/// Vide la pile (en jeu : retour à la partie ; sinon : menu principal).
	void ClearScreens();
	void Home();
	[[nodiscard]] Screen *Top() noexcept { return m_stack.empty() ? nullptr : m_stack.back().screen.get(); }
	[[nodiscard]] bool HasScreen(const String &id) const;
	/// Reconstruit l'écran du dessus (après un changement de données).
	void Rebuild();
	/// Reconstruit tous les écrans (thème ou échelle changés).
	void RebuildAll();

	// ── Dialogues et notifications ──────────────────────────────────────────
	void Confirm(ConfirmSpec spec);
	/// `important` : affichée même si l'utilisateur limite les notifications.
	void Notify(Tone tone, const String &title, const String &text = String(), bool important = false);
	void ShowError(const String &title, const String &message, const String &details);

	// ── Partie ──────────────────────────────────────────────────────────────
	void PlayWorld(const String &folder);
	void LeaveWorld(bool save);
	[[nodiscard]] bool InGame() const noexcept { return m_session != nullptr; }

	// ── Préférences ─────────────────────────────────────────────────────────
	void SaveSettings();
	/// Applique les préférences (thème, fenêtre, rendu, audio) et les enregistre.
	void ApplySettings(bool rebuild = true);
	/// Son d'interface (« ui_click », « ui_confirm »…).
	void UiSound(const char *id);
	void Quit() { m_running = false; }
	/// Capture d'écran (dossier data/voxelia/screenshots).
	void Screenshot(const String &path = String());

	/// Monde dont la vignette doit être prise à la prochaine image (quitter une partie).
	String pendingThumbnail;

private:
	struct Layer {
		std::unique_ptr<Screen> screen;
		ui::Widget root;
	};
	struct Toast {
		ui::Widget widget;
		float ttl = 0.f;
	};
	class Backdrop;

	void BuildLayers();
	void ShowTop();
	void DestroyLayer(Layer &layer);
	ui::Widget MakeLayerRoot(bool overlay);
	/// Couche du HUD (recréée à chaque partie), sous les écrans.
	void MakeHudLayer();
	void UpdateToasts(float dt);
	void UpdateViewport();
	void UpdateMenuAudio(float dt);
	/// Arrête la musique du menu (entrée dans une partie).
	void StopMenuMusic();
	/// Monde à ouvrir après l'écran de chargement (une image de délai).
	void StartLoading();

	Options m_options;
	sdl3::Window &m_window;
	sdl3::Renderer &m_renderer;
	render3d::Canvas &m_canvas;
	ui::Ui &m_gui;
	Settings m_settings;
	std::unique_ptr<Kit> m_kit;
	WorldCatalog m_worlds;
	ModCatalog m_mods;
	std::unique_ptr<Backdrop> m_backdrop;
	std::unique_ptr<Session> m_session;
	std::vector<Layer> m_stack;
	std::deque<Toast> m_toasts;
	ui::Widget m_viewport, m_hudLayer, m_screenLayer, m_toastLayer;
	std::vector<String> m_log;
	String m_loadingWorld;
	int m_loadingDelay = 0;
	bool m_running = true;
	bool m_rebuildAll = false;
	float m_autosaveTimer = 0.f;
	audio::AudioEngine::VoiceId m_menuMusic = audio::AudioEngine::NO_VOICE;
};

} // namespace voxelia
