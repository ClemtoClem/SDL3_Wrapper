#pragma once
/**
 * emulator_demo — rapport d'exécution : ce que la démo a réellement fait.
 *
 * Un émulateur qui tourne quelques secondes puis se ferme ne laisse rien
 * derrière lui. Le rapport le rend vérifiable après coup et sans écran :
 * quelle ROM a démarré (ou pourquoi elle n'a pas démarré), combien d'images
 * la console a produites et à quelle vitesse, combien de fils d'exécution ont
 * tourné en même temps, quels boutons ont été pressés, quelles sauvegardes
 * d'état ont réussi, quelles captures ont été écrites, et si l'image finale
 * contient autre chose qu'un aplat (`--require-video`).
 *
 * Deux formats, même contenu : `text` pour un humain, `json` (via
 * `data::JsonDocument`) pour un test d'intégration qui vérifie des seuils.
 *
 * Structure reprise de examples/game_editor/report.hpp (ThreadTracker,
 * FrameStats) : mêmes mesures, mêmes pièges — la première image est exclue
 * des extrêmes, et le parallélisme publié est un maximum SIMULTANÉ, pas un
 * total cumulé.
 */
#include <algorithm>
#include <cstdio>
#include <vector>

#include "core/core.hpp"
#include "data/json.hpp"
#include "sdl3/iostream.hpp"
#include "sdl3/thread.hpp"

#include "cli.hpp"

namespace emulator_demo {

// ============================================================================
// Suivi des fils d'exécution
// ============================================================================

/// Compte les fils vivants CRÉÉS PAR LA DÉMO (émulation, pompe audio,
/// acceptation réseau) et retient le maximum atteint simultanément. Le fil
/// principal est compté dès la construction.
class ThreadTracker {
public:
	ThreadTracker() {
		m_live.Store(1);
		m_peak.Store(1);
		m_started.Store(1);
	}

	/// Garde RAII à placer en tête du corps de chaque fil créé.
	class Scope {
	public:
		explicit Scope(ThreadTracker *tracker) noexcept : m_tracker(tracker) {
			if (m_tracker)
				m_tracker->Enter();
		}
		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;
		~Scope() {
			if (m_tracker)
				m_tracker->Leave();
		}

	private:
		ThreadTracker *m_tracker;
	};

	[[nodiscard]] int Peak() const noexcept { return m_peak.Load(); }
	[[nodiscard]] int Live() const noexcept { return m_live.Load(); }
	[[nodiscard]] int TotalStarted() const noexcept { return m_started.Load(); }

private:
	void Enter() noexcept {
		int live = m_live.FetchAdd(1) + 1;
		m_started.FetchAdd(1);
		for (;;) {
			int peak = m_peak.Load();
			if (live <= peak || m_peak.CompareExchange(peak, live))
				break;
		}
	}
	void Leave() noexcept { m_live.FetchAdd(-1); }

	sdl3::AtomicInt m_live;
	sdl3::AtomicInt m_peak;
	sdl3::AtomicInt m_started;
};

/// Nombre de fils du PROCESSUS entier (SDL, pilote audio, Mesa/Vulkan…),
/// lu dans /proc/self/status. NONE hors Linux : le rapport le dit plutôt que
/// d'inventer un chiffre.
[[nodiscard]] inline Option<int> ReadProcessThreadCount() {
	std::FILE *status = std::fopen("/proc/self/status", "r");
	if (!status)
		return NONE;
	char line[256];
	Option<int> count = NONE;
	while (std::fgets(line, sizeof(line), status)) {
		int value = 0;
		if (std::sscanf(line, "Threads: %d", &value) == 1) {
			count = Some(value);
			break;
		}
	}
	std::fclose(status);
	return count;
}

// ============================================================================
// Statistiques d'images
// ============================================================================

class FrameStats {
public:
	/// Durée d'une image en secondes. La première est mesurée mais exclue des
	/// extrêmes : elle contient l'initialisation (textures, pipelines GPU).
	void Push(double seconds) {
		++m_count;
		if (m_count == 1) {
			m_firstFrameSeconds = seconds;
			return;
		}
		if (seconds <= 0.0)
			return;
		m_totalSeconds += seconds;
		++m_sampleCount;
		if (m_sampleCount == 1 || seconds < m_minSeconds)
			m_minSeconds = seconds;
		if (m_sampleCount == 1 || seconds > m_maxSeconds)
			m_maxSeconds = seconds;
		m_samples.push_back(seconds);
	}

	[[nodiscard]] long Count() const noexcept { return m_count; }
	[[nodiscard]] double FirstFrameSeconds() const noexcept { return m_firstFrameSeconds; }
	[[nodiscard]] double MinFps() const noexcept { return m_maxSeconds > 0.0 ? 1.0 / m_maxSeconds : 0.0; }
	[[nodiscard]] double MaxFps() const noexcept { return m_minSeconds > 0.0 ? 1.0 / m_minSeconds : 0.0; }
	[[nodiscard]] double AverageFps() const noexcept {
		return m_totalSeconds > 0.0 ? double(m_sampleCount) / m_totalSeconds : 0.0;
	}

	/// Cadence lissée sur les dernières images (affichage du HUD).
	[[nodiscard]] double RecentFps(size_t window = 30) const {
		if (m_samples.empty())
			return 0.0;
		size_t take = m_samples.size() < window ? m_samples.size() : window;
		double total = 0.0;
		for (size_t i = m_samples.size() - take; i < m_samples.size(); ++i)
			total += m_samples[i];
		return total > 0.0 ? double(take) / total : 0.0;
	}

	/// Centile des DURÉES d'image, en millisecondes.
	[[nodiscard]] double PercentileMs(double percentile) const {
		if (m_samples.empty())
			return 0.0;
		std::vector<double> sorted = m_samples;
		for (size_t i = 1; i < sorted.size(); ++i) {
			double key = sorted[i];
			size_t j = i;
			while (j > 0 && sorted[j - 1] > key) {
				sorted[j] = sorted[j - 1];
				--j;
			}
			sorted[j] = key;
		}
		double rank = percentile * double(sorted.size() - 1) / 100.0;
		size_t index = size_t(rank < 0.0 ? 0.0 : rank);
		if (index >= sorted.size())
			index = sorted.size() - 1;
		return sorted[index] * 1000.0;
	}

private:
	long m_count = 0;
	size_t m_sampleCount = 0;
	double m_totalSeconds = 0.0;
	double m_minSeconds = 0.0;
	double m_maxSeconds = 0.0;
	double m_firstFrameSeconds = 0.0;
	std::vector<double> m_samples;
};

// ============================================================================
// Enregistrements
// ============================================================================

/// Front d'un bouton piloté par `--press` (appui ou relâchement).
struct InputEdgeRecord {
	long frame = 0;
	int button = BTN_A;
	bool pressed = false;
};

/// Sauvegarde ou chargement d'état, planifié (`--save-state`) ou demandé
/// depuis l'interface (F5/F9, menu).
struct StateOperationRecord {
	long frame = 0;
	StateActionKind kind = StateActionKind::SAVE;
	bool ok = false;
	String origin; ///< "script" | "interface"
	String detail;
};

struct ScreenshotRecord {
	long frame = 0;
	String path;
	String source; ///< "fenêtre" | "framebuffer"
	bool written = false;
	String error;
};

/// Empreinte de la dernière image émulée : de quoi affirmer « le jeu affiche
/// quelque chose » sans comparer des pixels à une référence.
struct VideoRecord {
	long frame = 0;
	int width = 0;
	int height = 0;
	uint64_t checksum = 0; ///< FNV-1a 64 bits des pixels
	int distinctColors = 0; ///< plafonné à DISTINCT_COLOR_CAP
	double nonBlackRatio = 0.0;
	long distinctFrameCount = 0; ///< images dont l'empreinte diffère de la précédente

	static constexpr int DISTINCT_COLOR_CAP = 4096;

	[[nodiscard]] bool HasPicture() const noexcept { return width > 0 && height > 0; }
	[[nodiscard]] bool IsUniform() const noexcept { return HasPicture() && distinctColors <= 1; }
};

/// FNV-1a 64 bits des pixels — assez bon marché pour être calculé à CHAQUE
/// image (détection des images distinctes).
[[nodiscard]] inline uint64_t HashPixels(const std::vector<uint32_t> &pixels) {
	uint64_t hash = 1469598103934665603ull;
	for (uint32_t pixel : pixels) {
		for (int shift = 0; shift < 32; shift += 8) {
			hash ^= uint64_t((pixel >> shift) & 0xFF);
			hash *= 1099511628211ull;
		}
	}
	return hash;
}

/// Empreinte complète d'une image RGBA (valeurs 0xAABBGGRR) — calculée une
/// seule fois, en fin d'exécution (tri des couleurs en O(n log n)).
[[nodiscard]] inline VideoRecord FingerprintFrame(const std::vector<uint32_t> &pixels, int width, int height,
												  long frame) {
	VideoRecord record;
	record.frame = frame;
	record.width = width;
	record.height = height;
	if (pixels.empty() || width <= 0 || height <= 0)
		return record;

	std::vector<uint32_t> colors;
	colors.reserve(pixels.size());
	size_t nonBlack = 0;
	for (uint32_t pixel : pixels) {
		uint32_t rgb = pixel & 0x00FFFFFF;
		if (rgb != 0)
			++nonBlack;
		colors.push_back(rgb);
	}
	std::sort(colors.begin(), colors.end());
	size_t distinct = size_t(std::unique(colors.begin(), colors.end()) - colors.begin());

	record.checksum = HashPixels(pixels);
	record.distinctColors = int(distinct < size_t(VideoRecord::DISTINCT_COLOR_CAP) ? distinct
																				   : size_t(VideoRecord::DISTINCT_COLOR_CAP));
	record.nonBlackRatio = double(nonBlack) / double(pixels.size());
	return record;
}

// ============================================================================
// Rapport
// ============================================================================

class RunReport {
public:
	// ── Exécution ────────────────────────────────────────────────────────────
	String commandLine;
	String mode = "fenêtré";
	String configPath;
	double wallClockSeconds = 0.0;
	bool completed = false;
	String failure; ///< vide si tout s'est bien passé
	std::vector<String> failedChecks;

	// ── ROM ──────────────────────────────────────────────────────────────────
	String romPath;
	String system; ///< NDS | GBA | GBC | (aucune)
	int64_t romSizeBytes = 0;
	String romTitle;
	String romPublisher;
	bool romHasIcon = false;
	bool booted = false;
	String bootError;
	String romGameCode;
	String romRegion;
	int romVersion = 0;
	String romHardware;
	String romCartridge;
	Option<bool> romHeaderChecksumOk = NONE;
	uint32_t romCrc32 = 0;
	// Origine archive (vide/0 pour une ROM simple)
	String archiveFormat;
	String archiveEntry;
	uint64_t archiveBytes = 0;
	uint64_t archiveStoredBytes = 0;
	String archiveMethod;
	String extractedPath;

	// ── Réglages effectifs ───────────────────────────────────────────────────
	bool directBoot = true;
	bool arm7Hle = false;
	bool threaded2D = false;
	bool threaded3D = false;
	int fpsLimiter = 1;
	String screenLayout;
	String saveDirectory;
	std::vector<std::pair<String, bool>> firmwareFiles; ///< (chemin, présent)

	// ── Émulation ────────────────────────────────────────────────────────────
	long emulatedFrames = 0;
	double emulatedSeconds = 0.0;
	double emulationWallSeconds = 0.0;
	int coreFpsMin = 0;
	int coreFpsMax = 0;
	bool audioEnabled = false;
	long audioChunks = 0;
	std::vector<InputEdgeRecord> inputEdges;
	std::vector<StateOperationRecord> stateOperations;
	VideoRecord video;

	// ── Performances ─────────────────────────────────────────────────────────
	String frameStatsLabel = "images de la fenêtre";
	FrameStats frames;
	int peakThreads = 1;
	int totalThreads = 1;
	Option<int> processThreadsPeak = NONE;

	// ── Interface ────────────────────────────────────────────────────────────
	std::vector<String> modalsOpened;
	std::vector<ScreenshotRecord> screenshots;

	// ── Journal ──────────────────────────────────────────────────────────────
	long logWarnings = 0;
	long logErrors = 0;
	std::vector<String> logExcerpt; ///< derniers avertissements/erreurs SDL
	std::vector<String> warnings;	///< avertissements de la démo elle-même

	/// Vitesse d'émulation relative au temps réel (100 % = vitesse console).
	[[nodiscard]] double SpeedPercent() const noexcept {
		return emulationWallSeconds > 0.0 ? emulatedSeconds * 100.0 / emulationWallSeconds : 0.0;
	}

	[[nodiscard]] String ToText() const {
		String out;
		auto line = [&out](const String &text) {
			out.Concat(text);
			out.Concat("\n");
		};
		auto yesNo = [](bool value) { return value ? "oui" : "non"; };

		line(String("================================================================"));
		line(String(" RAPPORT D'EXÉCUTION — emulator_demo"));
		line(String("================================================================"));
		line(String::Format("Commande        : %s", commandLine.CStr()));
		line(String::Format("Mode            : %s", mode.CStr()));
		line(String::Format("Réglages        : %s", configPath.CStr()));
		line(String::Format("Durée totale    : %.2f s", wallClockSeconds));
		line(String::Format("Issue           : %s", completed ? "terminé normalement" : failure.CStr()));
		for (const String &check : failedChecks)
			line(String::Format("  [ÉCHEC] %s", check.CStr()));
		line(String());

		line(String("--- ROM --------------------------------------------------------"));
		if (romPath.IsEmpty()) {
			line(String("  (aucune ROM : interface seule)"));
		} else {
			line(String::Format("Fichier         : %s", romPath.CStr()));
			line(String::Format("Système         : %s", system.CStr()));
			line(String::Format("Taille          : %lld octets", static_cast<long long>(romSizeBytes)));
			line(String::Format("Titre           : %s", romTitle.IsEmpty() ? "(illisible)" : romTitle.Replace('\n', ' ').CStr()));
			line(String::Format("Éditeur         : %s", romPublisher.IsEmpty() ? "(inconnu)" : romPublisher.CStr()));
			line(String::Format("Icône           : %s", yesNo(romHasIcon)));
			if (!romGameCode.IsEmpty())
				line(String::Format("Code produit    : %s (%s)", romGameCode.CStr(), romRegion.CStr()));
			else if (!romRegion.IsEmpty())
				line(String::Format("Région          : %s", romRegion.CStr()));
			line(String::Format("Version         : %d", romVersion));
			if (!romHardware.IsEmpty())
				line(String::Format("Matériel        : %s", romHardware.CStr()));
			if (!romCartridge.IsEmpty())
				line(String::Format("Cartouche       : %s", romCartridge.CStr()));
			if (romHeaderChecksumOk.IsSome())
				line(String::Format("En-tête         : somme de contrôle %s",
									romHeaderChecksumOk.Unwrap() ? "valide" : "INVALIDE"));
			line(String::Format("CRC-32          : %08X", romCrc32));
			if (!archiveFormat.IsEmpty()) {
				line(String::Format("Archive         : %s, entrée « %s »", archiveFormat.CStr(), archiveEntry.CStr()));
				// 0 : entrée d'un flux compressé partagé (tar compressé, bloc solide).
				const String stored = archiveStoredBytes == 0
										  ? String("bloc compressé partagé")
										  : String::Format("%llu octets stockés",
														   static_cast<unsigned long long>(archiveStoredBytes));
				line(String::Format("                  %llu octets d'archive · %s (%s)",
									static_cast<unsigned long long>(archiveBytes), stored.CStr(), archiveMethod.CStr()));
				line(String::Format("Extraite vers   : %s", extractedPath.CStr()));
			}
			line(String::Format("Démarrage       : %s", booted ? "réussi" : bootError.CStr()));
		}
		line(String());

		line(String("--- Réglages effectifs -----------------------------------------"));
		line(String::Format("Boot direct     : %s", yesNo(directBoot)));
		line(String::Format("ARM7 HLE        : %s", yesNo(arm7Hle)));
		line(String::Format("Rendu 2D/3D fil : %s / %s", yesNo(threaded2D), yesNo(threaded3D)));
		line(String::Format("Limiteur        : %d", fpsLimiter));
		line(String::Format("Écrans NDS      : %s", screenLayout.CStr()));
		line(String::Format("Sauvegardes     : %s", saveDirectory.CStr()));
		for (const auto &file : firmwareFiles)
			line(String::Format("  [%s] %s", file.second ? "présent" : "absent ", file.first.CStr()));
		line(String());

		line(String("--- Émulation --------------------------------------------------"));
		line(String::Format("Images émulées  : %ld (%.2f s de console)", emulatedFrames, emulatedSeconds));
		line(String::Format("Vitesse         : %.0f %% du temps réel", SpeedPercent()));
		line(String::Format("Cadence cœur    : min %d · max %d img/s (mesure interne, par seconde)", coreFpsMin,
							coreFpsMax));
		line(String::Format("Audio           : %s (%ld blocs envoyés)", audioEnabled ? "actif" : "inactif",
							audioChunks));
		if (video.HasPicture()) {
			line(String::Format("Dernière image  : n°%ld, %dx%d, empreinte %016llx", video.frame, video.width,
								video.height, static_cast<unsigned long long>(video.checksum)));
			line(String::Format("Contenu         : %d couleur(s)%s, %.1f %% de pixels non noirs, %ld image(s) distinctes",
								video.distinctColors,
								video.distinctColors >= VideoRecord::DISTINCT_COLOR_CAP ? "+" : "",
								video.nonBlackRatio * 100.0, video.distinctFrameCount));
		} else {
			line(String("Dernière image  : (aucune)"));
		}
		line(String::Format("Boutons pilotés : %d front(s)", int(inputEdges.size())));
		for (const InputEdgeRecord &edge : inputEdges)
			line(String::Format("  image %-6ld %-6s %s", edge.frame, ButtonName(edge.button),
								edge.pressed ? "appui" : "relâché"));
		line(String::Format("États           : %d opération(s)", int(stateOperations.size())));
		for (const StateOperationRecord &op : stateOperations)
			line(String::Format("  image %-6ld %-11s %-9s [%s] %s", op.frame,
								op.kind == StateActionKind::SAVE ? "sauvegarde" : "chargement", op.origin.CStr(),
								op.ok ? "ok" : "ÉCHEC", op.detail.CStr()));
		line(String());

		line(String("--- Performance ------------------------------------------------"));
		line(String::Format("Mesure          : %s", frameStatsLabel.CStr()));
		line(String::Format("Images          : %ld", frames.Count()));
		line(String::Format("Première image  : %.1f ms (exclue des extrêmes)", frames.FirstFrameSeconds() * 1000.0));
		line(String::Format("Cadence min     : %.1f img/s", frames.MinFps()));
		line(String::Format("Cadence max     : %.1f img/s", frames.MaxFps()));
		line(String::Format("Cadence moyenne : %.1f img/s", frames.AverageFps()));
		line(String::Format("Durée d'image   : médiane %.2f ms · p95 %.2f ms · p99 %.2f ms", frames.PercentileMs(50.0),
							frames.PercentileMs(95.0), frames.PercentileMs(99.0)));
		line(String::Format("Fils de la démo : %d simultanés (maximum) · %d démarrés au total", peakThreads,
							totalThreads));
		if (processThreadsPeak.IsSome())
			line(String::Format("Fils du process : %d simultanés (maximum, SDL et pilotes compris)",
								processThreadsPeak.Unwrap()));
		line(String());

		line(String("--- Interface et captures --------------------------------------"));
		if (modalsOpened.empty())
			line(String("  Boîtes ouvertes : (aucune)"));
		for (const String &modal : modalsOpened)
			line(String::Format("  Boîte ouverte : %s", modal.CStr()));
		if (screenshots.empty())
			line(String("  Captures : (aucune)"));
		for (const ScreenshotRecord &shot : screenshots)
			line(String::Format("  [%s] image %-6ld %-11s %s%s", shot.written ? "ok " : "ÉCHEC", shot.frame,
								shot.source.CStr(), shot.path.CStr(),
								shot.error.IsEmpty() ? "" : String::Format(" (%s)", shot.error.CStr()).CStr()));
		line(String());

		line(String("--- Journal ----------------------------------------------------"));
		line(String::Format("Avertissements  : %ld · erreurs : %ld", logWarnings, logErrors));
		for (const String &entry : logExcerpt)
			line(String::Format("  %s", entry.CStr()));
		for (const String &warning : warnings)
			line(String::Format("  [démo] %s", warning.CStr()));
		line(String());
		return out;
	}

	[[nodiscard]] String ToJson() const {
		using data::Node;
		auto root = Node::MakeObject();
		root->Set("format", Node::MakeString("emulator_demo.report"));
		root->Set("version", Node::MakeInt(1));

		auto run = Node::MakeObject();
		run->Set("command_line", Node::MakeString(commandLine));
		run->Set("mode", Node::MakeString(mode));
		run->Set("config_path", Node::MakeString(configPath));
		run->Set("wall_clock_seconds", Node::MakeFloat(wallClockSeconds));
		run->Set("completed", Node::MakeBool(completed));
		run->Set("failure", Node::MakeString(failure));
		auto checks = Node::MakeArray();
		for (const String &check : failedChecks)
			checks->Push(Node::MakeString(check));
		run->Set("failed_checks", checks);
		root->Set("run", run);

		auto rom = Node::MakeObject();
		rom->Set("path", Node::MakeString(romPath));
		rom->Set("system", Node::MakeString(system));
		rom->Set("size_bytes", Node::MakeInt(romSizeBytes));
		rom->Set("title", Node::MakeString(romTitle));
		rom->Set("publisher", Node::MakeString(romPublisher));
		rom->Set("has_icon", Node::MakeBool(romHasIcon));
		rom->Set("booted", Node::MakeBool(booted));
		rom->Set("boot_error", Node::MakeString(bootError));
		rom->Set("game_code", Node::MakeString(romGameCode));
		rom->Set("region", Node::MakeString(romRegion));
		rom->Set("version", Node::MakeInt(romVersion));
		rom->Set("hardware", Node::MakeString(romHardware));
		rom->Set("cartridge", Node::MakeString(romCartridge));
		rom->Set("header_checksum_ok",
				 romHeaderChecksumOk.IsSome() ? Node::MakeBool(romHeaderChecksumOk.Unwrap()) : Node::MakeNone());
		rom->Set("crc32", Node::MakeString(String::Format("%08X", romCrc32)));
		auto archiveNode = Node::MakeObject();
		archiveNode->Set("format", Node::MakeString(archiveFormat));
		archiveNode->Set("entry", Node::MakeString(archiveEntry));
		archiveNode->Set("archive_bytes", Node::MakeInt(int64_t(archiveBytes)));
		archiveNode->Set("stored_bytes", Node::MakeInt(int64_t(archiveStoredBytes)));
		archiveNode->Set("method", Node::MakeString(archiveMethod));
		archiveNode->Set("extracted_path", Node::MakeString(extractedPath));
		rom->Set("archive", archiveNode);
		root->Set("rom", rom);

		auto settings = Node::MakeObject();
		settings->Set("direct_boot", Node::MakeBool(directBoot));
		settings->Set("arm7_hle", Node::MakeBool(arm7Hle));
		settings->Set("threaded_2d", Node::MakeBool(threaded2D));
		settings->Set("threaded_3d", Node::MakeBool(threaded3D));
		settings->Set("fps_limiter", Node::MakeInt(fpsLimiter));
		settings->Set("screen_layout", Node::MakeString(screenLayout));
		settings->Set("save_directory", Node::MakeString(saveDirectory));
		auto firmware = Node::MakeArray();
		for (const auto &file : firmwareFiles) {
			auto entry = Node::MakeObject();
			entry->Set("path", Node::MakeString(file.first));
			entry->Set("present", Node::MakeBool(file.second));
			firmware->Push(entry);
		}
		settings->Set("firmware_files", firmware);
		root->Set("settings", settings);

		auto emulation = Node::MakeObject();
		emulation->Set("frames", Node::MakeInt(emulatedFrames));
		emulation->Set("emulated_seconds", Node::MakeFloat(emulatedSeconds));
		emulation->Set("wall_seconds", Node::MakeFloat(emulationWallSeconds));
		emulation->Set("speed_percent", Node::MakeFloat(SpeedPercent()));
		emulation->Set("core_fps_min", Node::MakeInt(coreFpsMin));
		emulation->Set("core_fps_max", Node::MakeInt(coreFpsMax));
		emulation->Set("audio_enabled", Node::MakeBool(audioEnabled));
		emulation->Set("audio_chunks", Node::MakeInt(audioChunks));
		auto edges = Node::MakeArray();
		for (const InputEdgeRecord &edge : inputEdges) {
			auto entry = Node::MakeObject();
			entry->Set("frame", Node::MakeInt(edge.frame));
			entry->Set("button", Node::MakeString(ButtonName(edge.button)));
			entry->Set("pressed", Node::MakeBool(edge.pressed));
			edges->Push(entry);
		}
		emulation->Set("input_edges", edges);
		auto states = Node::MakeArray();
		for (const StateOperationRecord &op : stateOperations) {
			auto entry = Node::MakeObject();
			entry->Set("frame", Node::MakeInt(op.frame));
			entry->Set("kind", Node::MakeString(op.kind == StateActionKind::SAVE ? "save" : "load"));
			entry->Set("origin", Node::MakeString(op.origin));
			entry->Set("ok", Node::MakeBool(op.ok));
			entry->Set("detail", Node::MakeString(op.detail));
			states->Push(entry);
		}
		emulation->Set("state_operations", states);
		auto videoNode = Node::MakeObject();
		videoNode->Set("frame", Node::MakeInt(video.frame));
		videoNode->Set("width", Node::MakeInt(video.width));
		videoNode->Set("height", Node::MakeInt(video.height));
		videoNode->Set("checksum", Node::MakeString(String::Format("%016llx",
																	static_cast<unsigned long long>(video.checksum))));
		videoNode->Set("distinct_colors", Node::MakeInt(video.distinctColors));
		videoNode->Set("non_black_ratio", Node::MakeFloat(video.nonBlackRatio));
		videoNode->Set("distinct_frames", Node::MakeInt(video.distinctFrameCount));
		videoNode->Set("uniform", Node::MakeBool(video.IsUniform()));
		emulation->Set("last_frame", videoNode);
		root->Set("emulation", emulation);

		auto performance = Node::MakeObject();
		performance->Set("measured", Node::MakeString(frameStatsLabel));
		performance->Set("frames", Node::MakeInt(frames.Count()));
		performance->Set("first_frame_ms", Node::MakeFloat(frames.FirstFrameSeconds() * 1000.0));
		performance->Set("fps_min", Node::MakeFloat(frames.MinFps()));
		performance->Set("fps_max", Node::MakeFloat(frames.MaxFps()));
		performance->Set("fps_average", Node::MakeFloat(frames.AverageFps()));
		performance->Set("frame_ms_p50", Node::MakeFloat(frames.PercentileMs(50.0)));
		performance->Set("frame_ms_p95", Node::MakeFloat(frames.PercentileMs(95.0)));
		performance->Set("frame_ms_p99", Node::MakeFloat(frames.PercentileMs(99.0)));
		performance->Set("threads_peak_concurrent", Node::MakeInt(peakThreads));
		performance->Set("threads_total_started", Node::MakeInt(totalThreads));
		performance->Set("process_threads_peak",
						 processThreadsPeak.IsSome() ? Node::MakeInt(processThreadsPeak.Unwrap()) : Node::MakeNone());
		root->Set("performance", performance);

		auto interfaceNode = Node::MakeObject();
		auto modals = Node::MakeArray();
		for (const String &modal : modalsOpened)
			modals->Push(Node::MakeString(modal));
		interfaceNode->Set("modals_opened", modals);
		root->Set("interface", interfaceNode);

		auto shots = Node::MakeArray();
		for (const ScreenshotRecord &shot : screenshots) {
			auto entry = Node::MakeObject();
			entry->Set("frame", Node::MakeInt(shot.frame));
			entry->Set("path", Node::MakeString(shot.path));
			entry->Set("source", Node::MakeString(shot.source));
			entry->Set("written", Node::MakeBool(shot.written));
			entry->Set("error", Node::MakeString(shot.error));
			shots->Push(entry);
		}
		root->Set("screenshots", shots);

		auto log = Node::MakeObject();
		log->Set("warnings", Node::MakeInt(logWarnings));
		log->Set("errors", Node::MakeInt(logErrors));
		auto excerpt = Node::MakeArray();
		for (const String &entry : logExcerpt)
			excerpt->Push(Node::MakeString(entry));
		log->Set("excerpt", excerpt);
		auto demoWarnings = Node::MakeArray();
		for (const String &warning : warnings)
			demoWarnings->Push(Node::MakeString(warning));
		log->Set("demo_warnings", demoWarnings);
		root->Set("log", log);

		data::JsonDocument document;
		document.SetRoot(root);
		return document.EncodeStr();
	}

	[[nodiscard]] Result<bool, String> Write(const String &path, ReportFormat format) const {
		String text = format == ReportFormat::JSON ? ToJson() : ToText();
		if (!sdl3::WriteFile(path, text.CStr(), text.GetSize()))
			return Err(String::Format("écriture du rapport impossible : %s", path.CStr()));
		return Ok(true);
	}
};

} // namespace emulator_demo
