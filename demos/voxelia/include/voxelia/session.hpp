#pragma once
/**
 * Partie en cours : jeu (voxel::Game), rendu du monde, son, commandes du
 * joueur, HUD et journal des messages. Les panneaux (inventaire,
 * fabrication, discussion, terminal…) sont des écrans du Shell qui
 * s'appuient sur cette session.
 */
#include <deque>
#include <memory>

#include <audio/engine.hpp>
#include <render3d/camera.hpp>
#include <ui/widget.hpp>
#include <voxel/game.hpp>

#include "catalog.hpp"
#include "worker.hpp"
#include "world_renderer.hpp"

namespace voxelia {

class Shell;

/// Ligne du journal (discussion, système, événements, commandes).
struct LogLine {
	String text;
	String channel; ///< « chat », « system », « event », « command », « result », « error »
	String author;  ///< joueur (discussion)
	double time = 0.0; ///< heure système (s) de réception
	float shown = 8.f; ///< secondes d'affichage restantes sur le HUD
};

/// Statistiques de la partie (écran d'informations du monde).
struct SessionStats {
	int blocksBroken = 0, blocksPlaced = 0, creaturesKilled = 0, deaths = 0, itemsCrafted = 0;
	double distance = 0.0;
	double sessionTime = 0.0;
};

class Session {
public:
	Session(Shell &shell, String folder);
	~Session();

	/// Étapes de chargement (une par image) : faux tant que ce n'est pas prêt.
	bool Prepare(float &progress, String &status);
	[[nodiscard]] bool Ready() const noexcept { return m_ready; }
	[[nodiscard]] const String &Error() const noexcept { return m_error; }

	void HandleEvent(sdl3::Event &e, bool captureAllowed);
	/// Commandes, actions, caméra, scène du monde, sons, HUD. Avec le fil de
	/// mise à jour, la simulation n'y avance pas : elle est réservée pour
	/// BeginSimulation (sinon elle avance ici, avant le reste).
	void Update(float dt, bool paused, bool panelOpen);
	/// Fil principal : lance sur le fil « voxel-update » le pas de simulation
	/// réservé par Update (voxel::Game::Tick : l'ECS, le monde, les scripts).
	/// Rien ne doit toucher au jeu avant EndSimulation.
	void BeginSimulation();
	/// Fil principal : attend la fin du pas de simulation en cours.
	void EndSimulation();
	/// Rechargement à chaud d'une définition (Voxelia Studio), sans quitter le
	/// monde. Ordre imposé par les fils : pas de simulation en cours (attendu
	/// au besoin), fils de maillage attendus, contenu rechargé
	/// (Game::ReloadDefinition), atlas reconstruit s'il manque une texture,
	/// mailleurs refaits et chunks remaillés.
	struct ContentReload {
		std::vector<String> blocks; ///< blocs (re)définis, états compris
		std::vector<String> errors; ///< définitions refusées (lot)
		bool atlasRebuilt = false;
		double milliseconds = 0.0; ///< attente des fils comprise
	};
	Result<ContentReload, String> ReloadDefinition(const dataio::NodePtr &definition, const voxel::ContentRegistry::LoadContext &context);
	/// Un LOT de définitions (un mod entier) : fils attendus, atlas et
	/// mailleurs refaits UNE fois. Les définitions refusées sont listées dans
	/// `errors`, les autres rechargées.
	struct Definition {
		dataio::NodePtr node;
		voxel::ContentRegistry::LoadContext context;
	};
	ContentReload ReloadDefinitions(const std::vector<Definition> &definitions);
	/// Durée du dernier pas de simulation (ms, moyenne glissante).
	[[nodiscard]] float TickMilliseconds() const noexcept { return m_tickMs; }
	void BuildHud(ui::Widget layer);
	void SetHudVisible(bool visible);
	/// Remet les réglages (distance d'affichage, ombres…) au moteur.
	void ApplySettings();
	void Save(bool notify);

	// ── Accès ───────────────────────────────────────────────────────────────
	[[nodiscard]] voxel::Game &Game() noexcept { return *m_game; }
	[[nodiscard]] WorldRenderer &Renderer() noexcept { return *m_world; }
	[[nodiscard]] ecs::Entity Player() const noexcept { return m_player; }
	[[nodiscard]] voxel::Inventory *Inventory();
	[[nodiscard]] const String &Folder() const noexcept { return m_folder; }
	[[nodiscard]] const render3d::Camera &Camera() const noexcept { return m_camera; }
	[[nodiscard]] audio::AudioEngine &Audio() noexcept { return *m_audio; }
	[[nodiscard]] const std::deque<LogLine> &Log() const noexcept { return m_logLines; }
	[[nodiscard]] const SessionStats &Stats() const noexcept { return m_stats; }
	[[nodiscard]] std::vector<String> &History() noexcept { return m_history; }
	[[nodiscard]] float Fps() const noexcept { return m_fps; }
	[[nodiscard]] bool ThirdPerson() const noexcept { return m_thirdPerson; }
	/// Gravité inversée (le joueur marche au plafond) : vue et commandes retournées.
	[[nodiscard]] bool Inverted() const {
		if (!m_game)
			return false;
		auto body = m_game->Registry().GetComponent<voxel::PhysicsBody>(m_player);
		return body.IsSome() && body.Unwrap()->gravityDirection < 0.f;
	}

	// ── Actions ─────────────────────────────────────────────────────────────
	/// Message de discussion du joueur, ou commande (« / »).
	void Submit(const String &text);
	/// Exécute une commande ; rend le résultat (et l'ajoute au journal).
	Result<String, String> Command(const String &line);
	void Craft(const String &recipe, int times);
	void SelectSlot(int slot);
	[[nodiscard]] int SelectedSlot();
	void DropSelected(bool all = false);
	void SetCaptured(bool captured);
	[[nodiscard]] bool Captured() const noexcept { return m_captured; }
	/// Vignette du monde (capture de la vue 3D) : demandée à la sortie.
	void RequestThumbnail() { m_wantThumbnail = true; }
	[[nodiscard]] bool ThumbnailRequested() const noexcept { return m_wantThumbnail; }
	void ThumbnailTaken() { m_wantThumbnail = false; }
	/// Icône d'objet (case de l'atlas, en pixels de l'atlas) ; texture partagée.
	void DrawItem(sdl3::Renderer &ren, const voxel::ItemStack &stack, sdl3::FRect cell, bool count = true);
	[[nodiscard]] String ItemName(const String &id);

private:
	/// Un pas de simulation, chronométré (fil de mise à jour, ou sur place).
	void RunTick(float dt);
	void UpdateControls();
	void UpdateActions(float dt, const Option<voxel::BlockHit> &target);
	void UpdateSound();
	void UpdateMessages(float dt);
	void UpdateStats(float dt);
	void HandleUiRequests();
	void ResetBreaking();
	void PickBlock();
	render3d::Camera CameraFor();
	void DrawHud(sdl3::Renderer &ren, sdl3::FRect r);
	void DrawBars(sdl3::Renderer &ren, float x, float y, float width);
	void EnsureIcons(sdl3::Renderer &ren);
	void UpdateHudLabels();
	void AddLog(const String &text, const String &channel, const String &author = String());
	[[nodiscard]] float CategoryVolume(const String &sound) const;

	Shell &m_shell;
	String m_folder;
	std::unique_ptr<voxel::Game> m_game;
	std::unique_ptr<WorldRenderer> m_world;
	std::unique_ptr<audio::AudioEngine> m_audio;
	ecs::Entity m_player{};
	render3d::Camera m_camera;
	int m_stage = 0;
	size_t m_targetChunks = 1;
	bool m_ready = false;
	String m_error;

	// HUD
	ui::Widget m_hud, m_hudCanvas, m_infoPanel, m_targetPanel, m_targetName, m_targetDetail, m_hintPanel;
	ui::Widget m_chatPanel, m_debugPanel;
	std::array<ui::Widget, 3> m_infoLines{};
	std::array<ui::Widget, 8> m_chatLines{};
	std::array<ui::Widget, 8> m_chatRows{}; ///< fond de chaque ligne de discussion
	std::array<ui::Widget, 5> m_debugLines{};
	std::array<ui::Widget, 3> m_hintLines{};
	Option<sdl3::Texture> m_icons = NONE;
	bool m_hudVisible = true, m_debug = false, m_thirdPerson = false;

	// Commandes du joueur
	bool m_captured = false, m_leftDown = false, m_rightDown = false;
	bool m_sprintToggled = false, m_sneakToggled = false;
	float m_breakProgress = 0.f, m_hitTimer = 0.f, m_attackTimer = 0.f, m_useTimer = 0.f, m_breakRepeat = 0.f;
	voxel::BlockPos m_breaking{0, -100000, 0};
	float m_lastHealth = 1e9f, m_hurtFlash = 0.f, m_swing = 0.f, m_bob = 0.f;

	// Journal
	std::deque<LogLine> m_logLines;
	std::vector<String> m_history;
	uint64_t m_soundCursor = 0, m_messageCursor = 0, m_diedCursor = 0;
	SessionStats m_stats;
	math::FVector3 m_lastPosition{0.f, 0.f, 0.f};
	float m_fps = 0.f;
	long m_fpsFrames = 0;
	uint64_t m_fpsTicks = 0;
	bool m_wantThumbnail = false;
	audio::AudioEngine::VoiceId m_ambientVoice = audio::AudioEngine::NO_VOICE;

	// Fil de mise à jour (simulation).
	float m_pendingTick = -1.f; ///< pas réservé par Update (< 0 : aucun)
	float m_tickMs = 0.f;       ///< écrit par le fil de mise à jour, lu après EndSimulation
	bool m_ticked = false;      ///< un pas a été lancé depuis le dernier EndSimulation
	/// Dernier membre : détruit (joint) avant le jeu qu'il fait avancer.
	std::unique_ptr<Worker> m_updateThread;
};

} // namespace voxelia
