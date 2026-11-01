#pragma once
/**
 * profiler::Capture — une capture de performances (fichier « .vcap » écrit
 * par diagnostics::trace) chargée en mémoire : zones instrumentées, images,
 * pistes de valeurs, intervalles asynchrones, échantillons de pile, états et
 * temps CPU de chaque fil, mémoire du processus, compteurs matériels, carte
 * des modules (pour la symbolisation).
 *
 * Lecture dédiée (pas de DOM JSON générique) : une capture de quelques
 * secondes compte des centaines de milliers de zones et d'échantillons.
 * Temps en ns depuis le début de la capture.
 */
#include <cstdint>
#include <vector>

#include <core/core.hpp>

namespace profiler {

struct ThreadInfo {
	long tid = 0;
	String name;
};

struct Module {
	uint64_t start = 0, end = 0, offset = 0;
	String path;
};

struct ZoneEvent {
	long tid = 0;
	uint32_t name = 0; ///< indice dans Capture::names
	int64_t start = 0, end = 0;
	uint32_t depth = 0;
	[[nodiscard]] int64_t Duration() const noexcept { return end - start; }
};

struct ValueEvent {
	long tid = 0;
	uint32_t name = 0;
	int64_t time = 0;
	double value = 0.0;
};

struct AsyncEvent {
	long tid = 0;
	uint32_t name = 0;
	uint64_t id = 0;
	int64_t time = 0;
	bool begin = true;
};

/// Pile d'appels échantillonnée : `frames[first .. first + depth[` dans
/// Capture::stackFrames, feuille d'abord.
struct StackSample {
	long tid = 0;
	int64_t time = 0;
	uint32_t first = 0, depth = 0;
};

/// État d'un fil à un instant (R en cours ou prêt, S en attente, D attente
/// non interruptible — entrées/sorties —, T arrêté, Z terminé).
struct TaskSample {
	long tid = 0;
	int64_t time = 0;
	uint64_t cpuNs = 0; ///< temps CPU cumulé du fil
	char state = '?';
};

struct ProcessSample {
	int64_t time = 0;
	uint64_t rss = 0, heap = 0;
	long minorFaults = 0, majorFaults = 0, voluntarySwitches = 0, involuntarySwitches = 0;
	uint64_t userNs = 0, systemNs = 0;
};

/// Compteurs matériels cumulés.
struct HardwareSample {
	int64_t time = 0;
	uint64_t cycles = 0, instructions = 0, cacheReferences = 0, cacheMisses = 0, branchMisses = 0;
};

struct Capture {
	String path, executable;
	long pid = 0;
	int64_t durationNs = 0;
	int samplingHz = 0, processHz = 0;
	std::vector<ThreadInfo> threads;
	std::vector<Module> modules;
	std::vector<String> names;
	std::vector<ZoneEvent> zones;	 ///< triées par fil puis début
	std::vector<ValueEvent> values;
	std::vector<AsyncEvent> async;
	std::vector<int64_t> frames;
	std::vector<StackSample> samples;
	std::vector<uint64_t> stackFrames;
	std::vector<TaskSample> tasks;
	std::vector<ProcessSample> process;
	bool hardwareAvailable = false;
	String hardwareReason;
	std::vector<HardwareSample> hardware;
	size_t zonesDropped = 0, samplesDropped = 0;

	[[nodiscard]] String ThreadName(long tid) const;
	[[nodiscard]] const String &Name(uint32_t index) const;
	/// Fils présents (déclarés, ou vus dans les zones, échantillons, états).
	[[nodiscard]] std::vector<long> ThreadIds() const;
};

[[nodiscard]] Result<Capture, String> LoadCapture(const String &path);
/// Analyse un texte de capture (tests, flux).
[[nodiscard]] Result<Capture, String> ParseCapture(StringView text, const String &path = String());

} // namespace profiler
