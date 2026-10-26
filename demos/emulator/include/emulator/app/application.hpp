#pragma once
/**
 * Application — la coquille fenêtrée « EmulOS » : fenêtre sans bordure et
 * barre de titre maison, barre de menus, panneau d'état, zone de jeu,
 * journal repliable, bandeau de performances et boîtes de dialogue (ROMs,
 * états sauvegardés, configuration, codes de triche, réseau, à propos).
 *
 * Un seul `sdl3::Renderer` pour toute l'application : l'interface `ui::` ET
 * le jeu (dessiné par `GameView` depuis un widget Canvas de la zone centrale)
 * passent par lui dans la même image.
 *
 * Pilotage et observation (cf. CommandLine) : la boucle d'images prend les
 * captures d'écran demandées, ouvre la boîte `--open`, s'arrête après
 * `--frames` images et alimente le rapport d'exécution.
 */
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <ecs/ecs.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

#include "cli.hpp"
#include "emulator_session.hpp"
#include "game_view.hpp"
#include "gamepad_manager.hpp"
#include "log_console.hpp"
#include "net_bridge.hpp"
#include "report.hpp"

namespace emulator::app {

class Application {
public:
	Application(const CommandLine &options, RunReport &report, ThreadTracker &threads, LogConsole &log);
	~Application();

	Application(const Application &) = delete;
	Application &operator=(const Application &) = delete;

	/// SDL, fenêtre, renderer, polices, audio, interface. Err = message lisible.
	[[nodiscard]] Result<bool, String> Initialize();

	/// Boucle principale, jusqu'à la fermeture ou la fin du budget d'images.
	void Run();

	/// Relève dans le rapport ce que la session en cours a produit.
	void CollectReport();

	// ── Actions (menus, raccourcis, boîtes de dialogue) ──────────────────────
	/// Lance `romPath` à la place de la partie en cours. `fromCommandLine` :
	/// la session reçoit alors les appuis et opérations d'état scriptés.
	/// `romPath` peut être une archive : `romEntry` choisit alors l'entrée
	/// (vide = première ROM de l'archive).
	bool SwitchToGame(const String &romPath, const String &romEntry = "", bool fromCommandLine = false);
	void StopGame();
	void ResetGame();
	void TogglePause();
	void QuickSave();
	void QuickLoad();
	void RequestQuit() { m_running = false; }
	void RequestScreenshot(String path);

	/// Exécute `action` sur le fil principal à la prochaine image. Sûr depuis
	/// n'importe quel fil (rappels des boîtes de fichiers natives de SDL).
	void Post(std::function<void()> action) {
		std::lock_guard<std::mutex> lock(m_postMutex);
		m_posted.push_back(std::move(action));
	}

	/// roms | saves | config | cheats | network | about
	void OpenModal(const String &name);
	void CloseModal();

	// ── Accès pour les constructeurs de boîtes de dialogue ───────────────────
	[[nodiscard]] ui::Ui &Gui() { return *m_ui; }
	[[nodiscard]] ecs::ArchetypeRegistry &World() { return m_world; }
	[[nodiscard]] sdl3::Window &Window() { return m_window.Value(); }
	[[nodiscard]] sdl3::Renderer &Renderer() { return m_renderer.Value(); }
	[[nodiscard]] GamepadManager &Gamepads() { return m_gamepads; }
	[[nodiscard]] NetBridge &Net() { return m_net; }
	[[nodiscard]] EmulatorSession *Session() { return m_session.get(); }
	[[nodiscard]] bool HudVisible() const { return m_hudVisible; }
	void SetHudVisible(bool visible);

	/// Rappels liés à la boîte ouverte : effacés à sa fermeture.
	void AddModalTick(std::function<void()> fn) { m_modalTicks.push_back(std::move(fn)); }
	void AddModalEventHandler(std::function<bool(const sdl3::Event &)> fn) {
		m_modalEventHandlers.push_back(std::move(fn));
	}

	/// Squelette commun des boîtes : panneau centré, en-tête titré avec
	/// bouton de fermeture, corps en colonne (rendu dans `body`).
	struct ModalFrame {
		ui::Widget root;
		ui::Widget body;
	};
	[[nodiscard]] ModalFrame BeginModal(const char *title, float width, float height);

	void SetLabel(ui::Widget widget, const String &text);
	void SetHidden(ui::Widget widget, bool hidden);

	/// Fichier journal (vide = désactivé) et écho terminal.
	void SetLogFilePath(const String &path);

	/// Palette « EmulOS » (maquettes d'origine) : feuille de style de la coquille.
	static const char *ShellCss();

private:
	void BuildShell();
	[[nodiscard]] ui::AnyWidget MenuBar();
	[[nodiscard]] ui::AnyWidget Sidebar();
	[[nodiscard]] ui::AnyWidget Viewport();
	[[nodiscard]] ui::AnyWidget LogPanel();

	void DoCloseModal();
	void HandleEvent(const sdl3::Event &event);
	bool HandleShortcut(const sdl3::Event &event);
	void UpdateWidgets();
	void CaptureScreenshots(long displayedFrame);
	[[nodiscard]] long DisplayedFrame() const;
	void DestroySession();

	const CommandLine &m_options;
	RunReport &m_report;
	ThreadTracker &m_threads;
	LogConsole &m_log;

	// Ordre de déclaration = ordre inverse de destruction : les contextes
	// SDL survivent à tout ce qui dépend d'eux, la session (fils, piste
	// audio) meurt avant le mixeur, l'interface avant la police.
	Option<sdl3::SdlContext> m_sdl = NONE;
	Option<sdl3::MixerContext> m_mixerContext = NONE;
	Option<sdl3::Mixer> m_mixer = NONE;
	Option<sdl3::Window> m_window = NONE;
	Option<sdl3::Renderer> m_renderer = NONE;
	Option<sdl3::TtfContext> m_ttf = NONE;
	std::unique_ptr<sdl3::FileLogSink> m_fileLogSink;
	size_t m_fileSinkId = 0;

	ecs::ArchetypeRegistry m_world;
	std::unique_ptr<ui::Ui> m_ui;
	GamepadManager m_gamepads;
	NetBridge m_net;
	std::unique_ptr<EmulatorSession> m_session;
	std::unique_ptr<GameView> m_gameView;

	// Widgets de la coquille.
	ui::Widget m_placeholder, m_hud, m_hudLabel, m_pauseLabel;
	ui::Widget m_romTitle, m_romDetails, m_logBody, m_logView;
	std::vector<std::pair<RomSystem, ui::Widget>> m_systemStatus;
	// Encadrement de la fenêtre sans décoration (module ui) : barre de titre,
	// barre d'état et hit-test système (déplacer, redimensionner) — déclaré
	// après la fenêtre, détruit avant elle.
	ui::WindowFrame m_frame;
	uint64_t m_logRevision = ~uint64_t(0);

	// Boîte de dialogue ouverte.
	ui::Widget m_modalRoot;
	String m_modalName;
	std::vector<std::function<void()>> m_modalTicks;
	std::vector<std::function<bool(const sdl3::Event &)>> m_modalEventHandlers;

	/// Actions différées à la fin du traitement des évènements (cf. OpenModal).
	std::vector<std::function<void()>> m_deferred;
	std::mutex m_postMutex;
	std::vector<std::function<void()>> m_posted;

	bool m_running = true;
	uint64_t m_sessionStartTicks = 0;
	bool m_hudVisible = true;
	long m_uiFrame = 0;
	long m_lastCoreFpsSample = 0;
	std::vector<bool> m_screenshotDone;
	long m_lastAutoScreenshot = 0;
	std::vector<String> m_pendingScreenshots;
	uint64_t m_lastFrameHash = 0;
	long m_lastHashedFrame = 0;
	long m_distinctFrames = 0;
};

} // namespace emulator::app
