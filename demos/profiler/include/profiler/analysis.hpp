#pragma once
/**
 * Analyse d'une capture : ce qu'Orbit montre dans ses rapports, plus un
 * diagnostic des goulots d'étranglement.
 *
 *  - images : cadence, percentiles, pics (et les zones qui les expliquent) ;
 *  - fonctions instrumentées : nombre, total, temps propre (sans les zones
 *    filles), min / max / moyenne / écart type, par image ;
 *  - fils : temps CPU (ordonnanceur), part du processeur, états (en cours,
 *    en attente, entrées/sorties), échantillons ;
 *  - échantillonnage : fonctions (temps propre et inclusif), arbres d'appels
 *    descendant (« top-down ») et ascendant (« bottom-up ») ;
 *  - mémoire (résidente, tas, croissance, défauts de page), cache (IPC,
 *    défauts de cache, mauvaises prédictions) si les compteurs matériels
 *    sont disponibles, GPU (présentation et rendus hors écran) ;
 *  - diagnostic : constats classés par gravité et par catégorie (Images,
 *    CPU, Fils, Mémoire, Cache, GPU), chacun avec ce qu'il faut regarder.
 *
 * Tout se calcule sur une plage de temps (sélection dans la chronologie).
 */
#include <cstdint>
#include <limits>
#include <vector>

#include <core/core.hpp>

#include "capture.hpp"
#include "symbols.hpp"

namespace profiler {

struct TimeRange {
	int64_t begin = 0;
	int64_t end = std::numeric_limits<int64_t>::max();
	[[nodiscard]] bool Contains(int64_t t) const noexcept { return t >= begin && t < end; }
	[[nodiscard]] int64_t Length(const Capture &c) const noexcept;
};

struct FrameStats {
	size_t count = 0;
	double averageMs = 0, p50Ms = 0, p95Ms = 0, p99Ms = 0, maxMs = 0, fps = 0;
	std::vector<double> frameMs;	///< durée de chaque image de la plage
	std::vector<int64_t> frameStart; ///< début de chaque image
};

struct ZoneStats {
	uint32_t name = 0;
	String label;
	uint64_t count = 0;
	int64_t totalNs = 0, selfNs = 0, minNs = 0, maxNs = 0;
	double meanNs = 0, stddevNs = 0;
	double perFrameMs = 0; ///< total / nombre d'images
	std::vector<long> threads;
};

struct ThreadStats {
	long tid = 0;
	String name;
	int64_t cpuNs = 0;		///< temps CPU dans la plage (ordonnanceur)
	double cpuShare = 0;	///< cpuNs / durée de la plage (1 = un cœur plein)
	double running = 0, sleeping = 0, waitingIo = 0; ///< parts des états observés
	int64_t instrumentedNs = 0; ///< zones de premier niveau
	size_t samples = 0;
};

struct FunctionStats {
	uint32_t function = 0;
	String name, module;
	uint64_t selfSamples = 0, inclusiveSamples = 0;
	double selfShare = 0, inclusiveShare = 0;
};

/// Nœud d'arbre d'appels (échantillons).
struct CallNode {
	uint32_t function = 0;
	uint64_t inclusive = 0, self = 0;
	std::vector<CallNode> children; ///< triés par `inclusive` décroissant
};

struct MemoryStats {
	bool available = false;
	double startMb = 0, endMb = 0, peakMb = 0, heapPeakMb = 0, growthMbPerMinute = 0;
	double minorFaultsPerSecond = 0, majorFaults = 0;
	double voluntarySwitchesPerSecond = 0, involuntarySwitchesPerSecond = 0;
	double processCpuShare = 0; ///< utilisateur + système / durée (cœurs)
};

struct CacheStats {
	bool available = false;
	String reason;
	double ipc = 0, cacheMissRate = 0, cacheMissesPerKiloInstruction = 0, branchMissesPerKiloInstruction = 0;
};

struct GpuStats {
	double presentMsPerFrame = 0, presentShare = 0;
	double offscreenMsPerFrame = 0;
};

enum class Severity : uint8_t { Info, Warning, Critical };
[[nodiscard]] const char *SeverityLabel(Severity s) noexcept;

struct Finding {
	Severity severity = Severity::Info;
	String category; ///< « Images », « CPU », « Fils », « Mémoire », « Cache », « GPU »
	String title, detail;
};

struct Report {
	TimeRange range;
	double durationMs = 0;
	double targetFps = 144;
	FrameStats frames;
	std::vector<ZoneStats> zones;		  ///< temps propre décroissant
	std::vector<ThreadStats> threads;	  ///< temps CPU décroissant
	std::vector<FunctionStats> functions; ///< temps propre décroissant
	size_t samples = 0;
	MemoryStats memory;
	CacheStats cache;
	GpuStats gpu;
	std::vector<Finding> findings;		  ///< gravité décroissante
};

struct AnalysisOptions {
	TimeRange range{};
	double targetFps = 144;
	/// 0 : tous les fils ; sinon, échantillons et zones de ce fil seulement.
	long thread = 0;
};

[[nodiscard]] Report Analyze(const Capture &capture, Symbolizer &symbols, const AnalysisOptions &options = {});

/// Arbre d'appels des échantillons : descendant (de main vers les feuilles)
/// ou ascendant (des fonctions qui consomment vers leurs appelants).
[[nodiscard]] CallNode BuildCallTree(const Capture &capture, Symbolizer &symbols, const AnalysisOptions &options, bool bottomUp);

/// Rapport texte (console, fichier).
[[nodiscard]] String FormatReport(const Report &report, size_t top = 20);
[[nodiscard]] String FormatCallTree(const CallNode &root, const Symbolizer &symbols, size_t maxDepth = 12, double minShare = 0.01);
/// Écarts entre deux captures (avant / après une optimisation).
[[nodiscard]] String CompareReports(const Report &before, const Report &after, size_t top = 15);
/// Export au format Chrome Trace Event (chrome://tracing, ui.perfetto.dev).
Result<bool, String> ExportChromeTrace(const Capture &capture, const String &path);

} // namespace profiler
