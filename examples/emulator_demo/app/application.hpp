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

#include "ecs/ecs.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include "cli.hpp"
#include "emulator_session.hpp"
#include "game_view.hpp"
#include "gamepad_manager.hpp"
#include "log_console.hpp"
#include "net_bridge.hpp"
#include "report.hpp"

namespace emulator_demo::app {

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
	[[nodiscard]] ui::UiFactory &Factory() { return m_ui->Factory(); }
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
		ecs::Entity root{};
		ecs::Entity body{};
	};
	[[nodiscard]] ModalFrame BeginModal(const char *title, float width, float height);

	void SetLabel(ecs::Entity entity, const String &text);
	void SetHidden(ecs::Entity entity, bool hidden);

	/// Fichier journal (vide = désactivé) et écho terminal.
	void SetLogFilePath(const String &path);

	// Palette « EmulOS » (maquettes d'origine).
	static sdl3::FColor BG_DARK() { return sdl3::FColor(sdl3::Color{13, 17, 23, 255}); }
	static sdl3::FColor SIDEBAR_BG() { return sdl3::FColor(sdl3::Color{30, 33, 40, 255}); }
	static sdl3::FColor HEADER_BLUE() { return sdl3::FColor(sdl3::Color{37, 99, 235, 255}); }
	static sdl3::FColor ROW_BG() { return sdl3::FColor(sdl3::Color{42, 46, 54, 255}); }
	static sdl3::FColor VIEWPORT_BG() { return sdl3::FColor(sdl3::Color{58, 61, 66, 255}); }
	static sdl3::FColor TEXT_PRIMARY() { return sdl3::FColor(sdl3::Color{230, 230, 235, 255}); }
	static sdl3::FColor TEXT_MUTED() { return sdl3::FColor(sdl3::Color{150, 155, 165, 255}); }
	static sdl3::FColor DIALOG_BG() { return sdl3::FColor(sdl3::Color{24, 27, 33, 255}); }
	static sdl3::FColor HUD_BG() { return sdl3::FColor(sdl3::Color{10, 12, 15, 200}); }

private:
	void BuildShell();
	void BuildTitleBar(ecs::Entity parent);
	void BuildMenuBar(ecs::Entity parent);
	void BuildSidebar(ecs::Entity parent);
	void BuildViewport(ecs::Entity parent);
	void BuildLogPanel(ecs::Entity parent);

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
	Option<sdl3::Font> m_font = NONE;
	Option<sdl3::TextEngine> m_textEngine = NONE;
	std::unique_ptr<sdl3::FileLogSink> m_fileLogSink;
	size_t m_fileSinkId = 0;

	ecs::ArchetypeRegistry m_world;
	std::unique_ptr<ui::Ui> m_ui;
	GamepadManager m_gamepads;
	NetBridge m_net;
	std::unique_ptr<EmulatorSession> m_session;
	std::unique_ptr<GameView> m_gameView;

	// Entités de la coquille.
	ecs::Entity m_shellRoot{};
	std::vector<ecs::Entity> m_menuPopups;
	ecs::Entity m_placeholder{};
	ecs::Entity m_hud{};
	ecs::Entity m_hudLabel{};
	ecs::Entity m_pauseLabel{};
	ecs::Entity m_titleLabel{};
	std::vector<std::pair<RomSystem, ecs::Entity>> m_systemStatus;
	ecs::Entity m_romTitle{};
	ecs::Entity m_romDetails{};
	ecs::Entity m_logBody{};
	ecs::Entity m_logView{};
	uint64_t m_logRevision = ~uint64_t(0);

	// Boîte de dialogue ouverte.
	ecs::Entity m_modalRoot{};
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

} // namespace emulator_demo::app
