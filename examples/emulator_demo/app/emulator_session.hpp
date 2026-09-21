#pragma once
/**
 * EmulatorSession — UNE partie en cours : le cœur (NDS/GBA `Core` ou GBC
 * `GbcCore`), son fil d'émulation, ses entrées (manette de l'hôte + boutons
 * pilotés par script), ses sauvegardes d'état et la dernière image produite.
 *
 * Extrait de l'ancien `GameView`, qui mêlait tout cela au rendu : la session
 * ne connaît NI fenêtre NI renderer. C'est ce qui permet au mode `--headless`
 * de faire tourner exactement le même code que la fenêtre, et à `GameView` de
 * n'être plus qu'une vue.
 *
 * Deux façons de la faire avancer :
 *  - `Start()` : un fil dédié enchaîne les images (mode fenêtré). La cadence
 *    est tenue par l'audio (le SPU attend que la pompe audio ait consommé le
 *    bloc précédent) ou, sans audio, par une horloge interne ;
 *  - `RunFrame()` : une image, sur le fil appelant (mode sans écran,
 *    déterministe et à cadence libre).
 *
 * ── Discipline de fils ───────────────────────────────────────────────────
 * Tout ce qui touche le cœur s'exécute sur le fil d'émulation, ENTRE deux
 * images : entrées, trames réseau, sauvegardes/chargements demandés par
 * l'interface (mis en file par `RequestQuickSave/Load`). L'ancien code
 * appliquait les boutons depuis le fil principal et verrouillait un mutex
 * pendant toute l'image pour sauvegarder — ce qui pouvait affamer le fil
 * principal, `std::mutex` n'étant pas équitable. Le seul échange sous verrou
 * est la COPIE de l'image terminée (quelques centaines de Ko).
 */
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/core.hpp"
#include "sdl3/sdl3.hpp"

#include "audio_output.hpp"
#include "cli.hpp"
#include "net_bridge.hpp"
#include "report.hpp"
#include "rom_metadata.hpp"
#include "rom_source.hpp"

namespace emulator_demo {
class ActionReplay;
class Core;
class GbcCore;
class SaveStates;
} // namespace emulator_demo

namespace emulator_demo::app {

/// Image émulée prête à afficher ou à capturer (valeurs 0xAABBGGRR).
///  - NDS : 256x384, écran du haut puis écran tactile ;
///  - GBA : 240x160 ;
///  - GBC : 160x144.
struct FrameSnapshot {
	std::vector<uint32_t> pixels;
	int width = 0;
	int height = 0;
	long frame = 0; ///< numéro de l'image émulée (0 = aucune encore)
};

struct SessionOptions {
	String statePath; ///< vide = <chemin logique>.state0
	String romEntry;  ///< entrée à lancer si la ROM est une archive (vide = première ROM)
	String extractDirectory; ///< cache des ROMs extraites (vide = DefaultExtractDirectory())
	bool threaded = true;
	bool unlimitedSpeed = false;
	sdl3::Mixer *mixer = nullptr; ///< nullptr = pas d'audio
	NetBridge *netBridge = nullptr;
	ThreadTracker *threads = nullptr;
	std::vector<ScriptedPress> presses;
	std::vector<ScriptedStateAction> stateActions;
};

class EmulatorSession {
public:
	/// Démarre une partie. Err porte un message lisible (ROM illisible, format
	/// inconnu, BIOS/firmware manquant…). Le fil n'est PAS lancé : `Start()`.
	[[nodiscard]] static Result<std::unique_ptr<EmulatorSession>, String> Create(const String &romPath,
																				SessionOptions options);

	~EmulatorSession();
	EmulatorSession(const EmulatorSession &) = delete;
	EmulatorSession &operator=(const EmulatorSession &) = delete;

	// ── Avancement ───────────────────────────────────────────────────────────
	void Start();
	void Stop();
	/// Une image sur le fil appelant (interdit pendant que le fil tourne).
	void RunFrame();

	[[nodiscard]] bool IsPaused() const { return m_paused.load(std::memory_order_relaxed); }
	void SetPaused(bool paused);

	// ── Entrées ──────────────────────────────────────────────────────────────
	/// Boutons tenus par l'hôte (clavier/manette), bit i = bouton i.
	void SetHostButtons(uint32_t mask) { m_hostButtons.store(mask, std::memory_order_relaxed); }
	/// Écran tactile NDS, coordonnées écran (0..255, 0..191).
	void SetTouch(int x, int y, bool pressed);

	// ── Sauvegardes d'état ───────────────────────────────────────────────────
	/// Mis en file et exécutés entre deux images (immédiatement sans fil).
	void RequestQuickSave();
	void RequestQuickLoad();
	[[nodiscard]] bool HasStateFile() const;
	[[nodiscard]] const String &StatePath() const { return m_statePath; }

	// ── Observation ──────────────────────────────────────────────────────────
	[[nodiscard]] RomSystem System() const { return m_system; }
	/// Fichier donné par l'utilisateur : la ROM, ou l'archive qui la contient.
	[[nodiscard]] const String &RomPath() const { return m_romPath; }
	/// Entrée d'archive lancée (vide pour une ROM simple).
	[[nodiscard]] String RomEntry() const { return m_archive.IsSome() ? m_archive.Value().entryName : String(); }
	/// Fichier réellement lu par le cœur (copie extraite pour une archive).
	[[nodiscard]] const String &BootPath() const { return m_bootPath; }
	/// « archive.zip › jeu.gba » ou le chemin de la ROM.
	[[nodiscard]] String DisplayName() const;
	[[nodiscard]] const Option<ArchiveOrigin> &Archive() const { return m_archive; }
	/// Métadonnées lues dans l'en-tête au démarrage.
	[[nodiscard]] const Option<RomMetadata> &Metadata() const { return m_metadata; }
	[[nodiscard]] long EmulatedFrames() const { return m_frames.load(std::memory_order_relaxed); }
	/// Cadence mesurée par le cœur lui-même (images par seconde réelle).
	[[nodiscard]] int CoreFps() const;
	/// Fréquence d'image de la console réelle.
	[[nodiscard]] double NativeRefreshRate() const;
	/// Copie la dernière image si elle est plus récente que `out.frame`.
	bool CopyLatestFrame(FrameSnapshot &out) const;
	[[nodiscard]] long AudioChunks() const { return m_audio ? m_audio->ChunksSent() : 0; }
	[[nodiscard]] bool HasAudio() const { return m_audio != nullptr; }

	[[nodiscard]] std::vector<StateOperationRecord> StateOperations() const;
	[[nodiscard]] std::vector<InputEdgeRecord> InputEdges() const;

	/// Moteur Action Replay (NDS uniquement), nullptr sinon.
	[[nodiscard]] ActionReplay *Cheats() const;

private:
	EmulatorSession(RomSystem system, SessionOptions options);

	[[nodiscard]] Result<bool, String> Boot();
	void EmulationLoop(std::stop_token stop);
	void StepOneFrame();
	void ApplyButtons(long frame);
	void ApplyScriptedStateActions(long frame);
	void DrainRequests();
	void PublishFrame(long frame);
	bool SaveStateNow(String &detail);
	bool LoadStateNow(String &detail);
	void RecordStateOperation(long frame, StateActionKind kind, bool ok, const char *origin, String detail);

	String m_romPath;	  ///< source (ROM ou archive)
	String m_bootPath;	  ///< fichier lu par le cœur
	String m_logicalPath; ///< base des sauvegardes/états (à côté de l'archive)
	String m_statePath;
	Option<ArchiveOrigin> m_archive = NONE;
	Option<RomMetadata> m_metadata = NONE;
	RomSystem m_system;
	SessionOptions m_options;

	std::unique_ptr<Core> m_core;
	std::unique_ptr<GbcCore> m_gbcCore;
	std::unique_ptr<SaveStates> m_saveStates;
	std::unique_ptr<AudioOutput> m_audio;

	std::atomic<long> m_frames{0};
	std::atomic<bool> m_paused{false};
	std::atomic<uint32_t> m_hostButtons{0};
	uint32_t m_appliedButtons = 0;
	uint32_t m_scriptedButtons = 0;
	std::atomic<int> m_touchX{0};
	std::atomic<int> m_touchY{0};
	std::atomic<bool> m_touchPressed{false};
	bool m_appliedTouch = false;

	enum class Request : uint8_t { SAVE, LOAD };
	std::mutex m_requestMutex;
	std::vector<Request> m_requests;

	mutable std::mutex m_recordMutex;
	std::vector<StateOperationRecord> m_stateOperations;
	std::vector<InputEdgeRecord> m_inputEdges;

	mutable std::mutex m_frameMutex;
	FrameSnapshot m_latest;
	std::vector<uint32_t> m_scratch;

	std::chrono::steady_clock::time_point m_nextDeadline;
	// Déclaré en DERNIER : arrêté avant la destruction du reste.
	std::jthread m_thread;
};

/// Recopie dans le rapport ce que l'en-tête de la ROM annonce.
inline void DescribeRomInReport(RunReport &report, const RomMetadata &metadata) {
	report.system = RomSystemName(metadata.system);
	report.romSizeBytes = metadata.sizeBytes;
	report.romTitle = metadata.title;
	report.romPublisher = metadata.publisher;
	report.romHasIcon = metadata.icon.IsSome();
	report.romGameCode = metadata.gameCode;
	report.romRegion = metadata.region;
	report.romVersion = metadata.version;
	report.romHardware = metadata.hardware;
	report.romCartridge = metadata.cartridge;
	report.romHeaderChecksumOk = metadata.headerChecksumOk;
	report.romCrc32 = metadata.crc32;
}

/// Métadonnées, origine archive et copie extraite d'une session démarrée.
inline void DescribeSessionInReport(RunReport &report, const EmulatorSession &session) {
	report.romPath = session.RomPath();
	report.system = RomSystemName(session.System());
	if (session.Metadata().IsSome())
		DescribeRomInReport(report, session.Metadata().Value());
	report.archiveFormat.Clear();
	report.archiveEntry.Clear();
	report.archiveBytes = 0;
	report.archiveStoredBytes = 0;
	report.archiveMethod.Clear();
	report.extractedPath.Clear();
	if (session.Archive().IsSome()) {
		const ArchiveOrigin &origin = session.Archive().Value();
		report.archiveFormat = data::archive::FormatName(origin.format);
		report.archiveEntry = origin.entryName;
		report.archiveBytes = origin.archiveBytes;
		report.archiveStoredBytes = origin.storedBytes;
		report.archiveMethod = origin.method;
		report.extractedPath = session.BootPath();
	}
}

} // namespace emulator_demo::app
