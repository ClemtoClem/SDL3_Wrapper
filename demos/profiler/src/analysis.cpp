// Définitions de profiler/analysis.hpp
#include <profiler/analysis.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace profiler {

namespace {

double Ms(int64_t ns) { return double(ns) / 1e6; }

double Percentile(std::vector<double> sorted, double p) {
	if (sorted.empty())
		return 0.0;
	std::sort(sorted.begin(), sorted.end());
	const size_t i = std::min(sorted.size() - 1, size_t(p * double(sorted.size() - 1) + 0.5));
	return sorted[i];
}

/// Fil principal : celui qui porte les images (le numéro du processus).
long MainThread(const Capture &c) { return c.pid; }

/// Temps propre de chaque zone (sa durée moins celle de ses zones filles),
/// zones triées par fil puis début.
std::vector<int64_t> SelfTimes(const Capture &c) {
	std::vector<int64_t> self(c.zones.size());
	std::vector<size_t> stack;
	long tid = -1;
	for (size_t i = 0; i < c.zones.size(); ++i) {
		const ZoneEvent &z = c.zones[i];
		self[i] = z.Duration();
		if (z.tid != tid) {
			stack.clear();
			tid = z.tid;
		}
		while (!stack.empty() && c.zones[stack.back()].end <= z.start)
			stack.pop_back();
		if (!stack.empty() && z.end <= c.zones[stack.back()].end)
			self[stack.back()] -= z.Duration();
		stack.push_back(i);
	}
	return self;
}

struct TreeBuilder {
	uint32_t function = 0;
	uint64_t inclusive = 0, self = 0;
	std::vector<TreeBuilder> children;
	std::unordered_map<uint32_t, size_t> index;

	TreeBuilder &Child(uint32_t f) {
		if (auto it = index.find(f); it != index.end())
			return children[it->second];
		index.emplace(f, children.size());
		children.push_back(TreeBuilder{f, 0, 0, {}, {}});
		return children.back();
	}

	CallNode Finish() const {
		CallNode node{function, inclusive, self, {}};
		node.children.reserve(children.size());
		for (const TreeBuilder &c : children)
			node.children.push_back(c.Finish());
		std::sort(node.children.begin(), node.children.end(), [](const CallNode &a, const CallNode &b) { return a.inclusive > b.inclusive; });
		return node;
	}
};

void AddFinding(Report &r, Severity s, const char *category, String title, String detail) {
	r.findings.push_back({s, String(category), std::move(title), std::move(detail)});
}

} // namespace

int64_t TimeRange::Length(const Capture &c) const noexcept {
	const int64_t b = std::max<int64_t>(0, begin), e = std::min(end, c.durationNs);
	return std::max<int64_t>(1, e - b);
}

const char *SeverityLabel(Severity s) noexcept {
	switch (s) {
	case Severity::Critical: return "CRITIQUE";
	case Severity::Warning: return "attention";
	default: return "info";
	}
}

Report Analyze(const Capture &c, Symbolizer &symbols, const AnalysisOptions &options) {
	Report r;
	r.range = options.range;
	r.targetFps = options.targetFps;
	const int64_t length = options.range.Length(c);
	r.durationMs = Ms(length);
	auto inThread = [&](long tid) { return options.thread == 0 || options.thread == tid; };

	// ── Images ──────────────────────────────────────────────────────────────
	for (size_t i = 1; i < c.frames.size(); ++i)
		if (options.range.Contains(c.frames[i]) && options.range.Contains(c.frames[i - 1])) {
			r.frames.frameMs.push_back(Ms(c.frames[i] - c.frames[i - 1]));
			r.frames.frameStart.push_back(c.frames[i - 1]);
		}
	FrameStats &f = r.frames;
	f.count = f.frameMs.size();
	if (f.count > 0) {
		double sum = 0;
		for (double ms : f.frameMs)
			sum += ms;
		f.averageMs = sum / double(f.count);
		f.fps = f.averageMs > 0 ? 1000.0 / f.averageMs : 0;
		f.p50Ms = Percentile(f.frameMs, 0.50);
		f.p95Ms = Percentile(f.frameMs, 0.95);
		f.p99Ms = Percentile(f.frameMs, 0.99);
		f.maxMs = *std::max_element(f.frameMs.begin(), f.frameMs.end());
	}
	const double frames = std::max<double>(1.0, double(f.count));

	// ── Zones ───────────────────────────────────────────────────────────────
	const std::vector<int64_t> self = SelfTimes(c);
	struct Accumulator {
		ZoneStats stats;
		double mean = 0, m2 = 0;
	};
	std::unordered_map<uint32_t, Accumulator> zones;
	for (size_t i = 0; i < c.zones.size(); ++i) {
		const ZoneEvent &z = c.zones[i];
		if (!options.range.Contains(z.start) || !inThread(z.tid))
			continue;
		Accumulator &a = zones[z.name];
		ZoneStats &s = a.stats;
		const int64_t d = z.Duration();
		if (s.count == 0) {
			s.name = z.name;
			s.label = c.Name(z.name);
			s.minNs = d;
		}
		++s.count;
		s.totalNs += d;
		s.selfNs += self[i];
		s.minNs = std::min(s.minNs, d);
		s.maxNs = std::max(s.maxNs, d);
		const double delta = double(d) - a.mean;
		a.mean += delta / double(s.count);
		a.m2 += delta * (double(d) - a.mean);
		if (std::find(s.threads.begin(), s.threads.end(), z.tid) == s.threads.end())
			s.threads.push_back(z.tid);
	}
	for (auto &[name, a] : zones) {
		a.stats.meanNs = a.mean;
		a.stats.stddevNs = a.stats.count > 1 ? std::sqrt(a.m2 / double(a.stats.count - 1)) : 0.0;
		a.stats.perFrameMs = Ms(a.stats.totalNs) / frames;
		r.zones.push_back(std::move(a.stats));
	}
	std::sort(r.zones.begin(), r.zones.end(), [](const ZoneStats &a, const ZoneStats &b) { return a.selfNs > b.selfNs; });

	// ── Fils ────────────────────────────────────────────────────────────────
	for (long tid : c.ThreadIds()) {
		if (!inThread(tid))
			continue;
		ThreadStats t;
		t.tid = tid;
		t.name = c.ThreadName(tid);
		const TaskSample *first = nullptr, *last = nullptr;
		size_t states = 0, running = 0, sleeping = 0, io = 0;
		for (const TaskSample &s : c.tasks) {
			if (s.tid != tid || !options.range.Contains(s.time))
				continue;
			if (!first)
				first = &s;
			last = &s;
			++states;
			running += s.state == 'R' ? 1 : 0;
			sleeping += s.state == 'S' ? 1 : 0;
			io += s.state == 'D' ? 1 : 0;
		}
		if (first && last && last->cpuNs >= first->cpuNs)
			t.cpuNs = int64_t(last->cpuNs - first->cpuNs);
		t.cpuShare = double(t.cpuNs) / double(length);
		if (states > 0) {
			t.running = double(running) / double(states);
			t.sleeping = double(sleeping) / double(states);
			t.waitingIo = double(io) / double(states);
		}
		for (const ZoneEvent &z : c.zones)
			if (z.tid == tid && z.depth == 0 && options.range.Contains(z.start))
				t.instrumentedNs += z.Duration();
		for (const StackSample &s : c.samples)
			if (s.tid == tid && options.range.Contains(s.time))
				++t.samples;
		if (t.cpuNs > 0 || t.samples > 0 || t.instrumentedNs > 0)
			r.threads.push_back(std::move(t));
	}
	std::sort(r.threads.begin(), r.threads.end(), [](const ThreadStats &a, const ThreadStats &b) { return a.cpuNs > b.cpuNs; });

	// ── Échantillons : fonctions ────────────────────────────────────────────
	std::unordered_map<uint32_t, FunctionStats> functions;
	std::unordered_set<uint32_t> seen;
	for (const StackSample &s : c.samples) {
		if (!options.range.Contains(s.time) || !inThread(s.tid) || s.depth == 0)
			continue;
		++r.samples;
		seen.clear();
		for (uint32_t d = 0; d < s.depth; ++d) {
			const Symbolizer::Location &loc = symbols.Resolve(c.stackFrames[s.first + d]);
			FunctionStats &fs = functions[loc.function];
			if (fs.name.IsEmpty()) {
				fs.function = loc.function;
				fs.name = symbols.Function(loc.function);
				fs.module = loc.module;
			}
			if (d == 0)
				++fs.selfSamples;
			if (seen.insert(loc.function).second)
				++fs.inclusiveSamples;
		}
	}
	for (auto &[index, fs] : functions) {
		fs.selfShare = r.samples ? double(fs.selfSamples) / double(r.samples) : 0.0;
		fs.inclusiveShare = r.samples ? double(fs.inclusiveSamples) / double(r.samples) : 0.0;
		r.functions.push_back(std::move(fs));
	}
	std::sort(r.functions.begin(), r.functions.end(), [](const FunctionStats &a, const FunctionStats &b) {
		return a.selfSamples != b.selfSamples ? a.selfSamples > b.selfSamples : a.inclusiveSamples > b.inclusiveSamples;
	});

	// ── Mémoire, processus ──────────────────────────────────────────────────
	const ProcessSample *pFirst = nullptr, *pLast = nullptr;
	for (const ProcessSample &p : c.process) {
		if (!options.range.Contains(p.time))
			continue;
		if (!pFirst)
			pFirst = &p;
		pLast = &p;
		r.memory.peakMb = std::max(r.memory.peakMb, double(p.rss) / 1048576.0);
		r.memory.heapPeakMb = std::max(r.memory.heapPeakMb, double(p.heap) / 1048576.0);
	}
	if (pFirst && pLast && pLast != pFirst) {
		MemoryStats &m = r.memory;
		m.available = true;
		const double seconds = std::max(1e-9, double(pLast->time - pFirst->time) / 1e9);
		m.startMb = double(pFirst->rss) / 1048576.0;
		m.endMb = double(pLast->rss) / 1048576.0;
		// Croissance APRÈS la mise en route (chargement) : dernier tiers de la plage.
		const int64_t settled = pFirst->time + (pLast->time - pFirst->time) * 2 / 3;
		const ProcessSample *pSettled = pLast;
		for (const ProcessSample &p : c.process)
			if (p.time >= settled && options.range.Contains(p.time)) {
				pSettled = &p;
				break;
			}
		const double settledSeconds = double(pLast->time - pSettled->time) / 1e9;
		m.growthMbPerMinute = settledSeconds > 0.5 ? (double(pLast->rss) - double(pSettled->rss)) / 1048576.0 / settledSeconds * 60.0 : 0.0;
		m.minorFaultsPerSecond = double(pLast->minorFaults - pFirst->minorFaults) / seconds;
		m.majorFaults = double(pLast->majorFaults - pFirst->majorFaults);
		m.voluntarySwitchesPerSecond = double(pLast->voluntarySwitches - pFirst->voluntarySwitches) / seconds;
		m.involuntarySwitchesPerSecond = double(pLast->involuntarySwitches - pFirst->involuntarySwitches) / seconds;
		m.processCpuShare = double((pLast->userNs + pLast->systemNs) - (pFirst->userNs + pFirst->systemNs)) / (seconds * 1e9);
	}

	// ── Cache (compteurs matériels) ─────────────────────────────────────────
	r.cache.reason = c.hardwareReason;
	const HardwareSample *hFirst = nullptr, *hLast = nullptr;
	for (const HardwareSample &h : c.hardware)
		if (options.range.Contains(h.time)) {
			if (!hFirst)
				hFirst = &h;
			hLast = &h;
		}
	if (c.hardwareAvailable && hFirst && hLast && hLast != hFirst) {
		CacheStats &k = r.cache;
		k.available = true;
		const double cycles = double(hLast->cycles - hFirst->cycles), instr = double(hLast->instructions - hFirst->instructions);
		const double refs = double(hLast->cacheReferences - hFirst->cacheReferences), misses = double(hLast->cacheMisses - hFirst->cacheMisses);
		const double branches = double(hLast->branchMisses - hFirst->branchMisses);
		k.ipc = cycles > 0 ? instr / cycles : 0;
		k.cacheMissRate = refs > 0 ? misses / refs : 0;
		k.cacheMissesPerKiloInstruction = instr > 0 ? misses / (instr / 1000.0) : 0;
		k.branchMissesPerKiloInstruction = instr > 0 ? branches / (instr / 1000.0) : 0;
	}

	// ── GPU ─────────────────────────────────────────────────────────────────
	for (const ZoneStats &z : r.zones) {
		if (z.label == "gpu.present")
			r.gpu.presentMsPerFrame = z.perFrameMs;
		if (z.label == "render3d.offscreen")
			r.gpu.offscreenMsPerFrame = z.perFrameMs;
	}
	r.gpu.presentShare = f.averageMs > 0 ? r.gpu.presentMsPerFrame / f.averageMs : 0;

	// ── Diagnostic ──────────────────────────────────────────────────────────
	const double targetMs = 1000.0 / std::max(1.0, options.targetFps);
	if (f.count > 0) {
		if (f.fps < options.targetFps)
			AddFinding(r, f.fps < 0.6 * options.targetFps ? Severity::Critical : Severity::Warning, "Images",
					   String::Format("%.0f img/s : sous l'objectif de %.0f img/s", f.fps, options.targetFps),
					   String::Format("image moyenne %.2f ms pour un budget de %.2f ms (médiane %.2f, 99 %% : %.2f, pire %.2f ms). "
									  "Les constats suivants disent où part le temps.",
									  f.averageMs, targetMs, f.p50Ms, f.p99Ms, f.maxMs));
		if (f.p99Ms > 2.0 * f.p50Ms && f.p99Ms > targetMs) {
			// Pire image : zones du fil principal qui l'occupent.
			const size_t worst = size_t(std::max_element(f.frameMs.begin(), f.frameMs.end()) - f.frameMs.begin());
			const int64_t ws = f.frameStart[worst], we = ws + int64_t(f.frameMs[worst] * 1e6);
			std::map<uint32_t, int64_t> inWorst;
			for (size_t i = 0; i < c.zones.size(); ++i) {
				const ZoneEvent &z = c.zones[i];
				if (z.tid == MainThread(c) && z.start >= ws && z.start < we)
					inWorst[z.name] += self[i];
			}
			std::vector<std::pair<int64_t, uint32_t>> ranked;
			for (const auto &[name, ns] : inWorst)
				ranked.push_back({ns, name});
			std::sort(ranked.rbegin(), ranked.rend());
			String causes;
			for (size_t i = 0; i < ranked.size() && i < 3; ++i)
				causes += String::Format("%s%s %.2f ms", i ? ", " : "", c.Name(ranked[i].second).CStr(), Ms(ranked[i].first));
			size_t spikes = 0;
			for (double ms : f.frameMs)
				spikes += ms > 2.0 * f.p50Ms ? 1 : 0;
			AddFinding(r, Severity::Warning, "Images", String::Format("Saccades : %zu image(s) à plus du double de la médiane", spikes),
					   String::Format("pire image %.2f ms à %.3f s%s%s.", f.maxMs, double(ws) / 1e9,
									  causes.IsEmpty() ? "" : " — temps propre dominant : ", causes.CStr()));
		}
	}
	// CPU : le fil principal.
	for (const ThreadStats &t : r.threads)
		if (t.tid == MainThread(c)) {
			String top;
			size_t shown = 0;
			for (const ZoneStats &z : r.zones) {
				if (std::find(z.threads.begin(), z.threads.end(), t.tid) == z.threads.end() || z.label == "frame")
					continue;
				top += String::Format("%s%s %.2f ms/image", shown ? ", " : "", z.label.CStr(), Ms(z.selfNs) / frames);
				if (++shown == 5)
					break;
			}
			// Sans plafond de cadence, un fil principal plein est normal : grave
			// seulement si l'objectif n'est pas tenu.
			const bool belowTarget = f.count > 0 && f.fps < options.targetFps;
			if (t.cpuShare > 0.85 && belowTarget)
				AddFinding(r, Severity::Critical, "CPU", String::Format("Fil principal saturé : %.0f %% d'un cœur", t.cpuShare * 100.0),
						   String("Le processeur limite la cadence. Temps propre dominant : ") + top + String("."));
			else if (t.cpuShare > 0.85)
				AddFinding(r, Severity::Info, "CPU", String::Format("Fil principal plein (%.0f %% d'un cœur), objectif tenu", t.cpuShare * 100.0),
						   String("Cadence non plafonnée : le processeur fixe le maximum. Temps propre dominant : ") + top + String("."));
			else if (!top.IsEmpty())
				AddFinding(r, Severity::Info, "CPU", String::Format("Fil principal : %.0f %% d'un cœur", t.cpuShare * 100.0),
						   String("Temps propre dominant : ") + top + String("."));
		}
	if (!r.functions.empty()) {
		String hot;
		for (size_t i = 0; i < r.functions.size() && i < 5; ++i)
			hot += String::Format("%s%s (%.1f %%)", i ? ", " : "", r.functions[i].name.CStr(), r.functions[i].selfShare * 100.0);
		AddFinding(r, r.functions[0].selfShare > 0.15 ? Severity::Warning : Severity::Info, "CPU", String("Fonctions les plus échantillonnées"),
				   hot + String(" — temps propre, tous fils (cf. arbres d'appels)."));
	}
	// GPU.
	if (r.gpu.presentShare > 0.25)
		AddFinding(r, r.gpu.presentShare > 0.45 ? Severity::Critical : Severity::Warning, "GPU",
				   String::Format("Présentation : %.0f %% de l'image (%.2f ms)", r.gpu.presentShare * 100.0, r.gpu.presentMsPerFrame),
				   String("Le fil principal attend le GPU (ou la synchronisation verticale) : alléger le rendu (moins de dessins, "
						  "de vues, de résolution) ou désactiver la synchronisation pour mesurer."));
	if (r.gpu.offscreenMsPerFrame > 0.25 * std::max(targetMs, 1e-3))
		AddFinding(r, Severity::Warning, "GPU", String::Format("Rendus 3D hors écran : %.2f ms par image", r.gpu.offscreenMsPerFrame),
				   String("Préparation des dessins (CPU) de chaque vue 3D : plafonner les vues secondaires, réduire les objets."));
	// Fils.
	for (const ZoneStats &z : r.zones)
		if (z.label.StartsWith("frame.wait") && f.averageMs > 0 && z.perFrameMs > 0.1 * f.averageMs)
			AddFinding(r, Severity::Warning, "Fils", String::Format("Attente : %s %.2f ms par image", z.label.CStr(), z.perFrameMs),
					   String("Le fil principal attend un autre fil : regarder le fil attendu dans la chronologie."));
	const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
	for (const ThreadStats &t : r.threads) {
		if (t.tid != MainThread(c) && t.cpuShare > 0.9)
			AddFinding(r, Severity::Warning, "Fils", String::Format("%s saturé : %.0f %% d'un cœur", t.name.CStr(), t.cpuShare * 100.0),
					   String("Ce fil ne suit plus : son travail en attente retarde les autres."));
		if (t.waitingIo > 0.05)
			AddFinding(r, Severity::Warning, "Fils", String::Format("%s en entrées/sorties %.0f %% du temps", t.name.CStr(), t.waitingIo * 100.0),
					   String("Attente non interruptible (disque, pilote) : lectures de fichiers sur le chemin critique ?"));
	}
	if (r.memory.available && r.memory.processCpuShare > 0.8 * double(cores))
		AddFinding(r, Severity::Warning, "Fils", String::Format("Processus : %.1f cœurs sur %u", r.memory.processCpuShare, cores),
				   String("Tous les cœurs sont occupés : trop de fils de travail, ou du travail en double."));
	if (r.memory.available && r.memory.involuntarySwitchesPerSecond > 2000)
		AddFinding(r, Severity::Info, "Fils", String::Format("%.0f préemptions par seconde", r.memory.involuntarySwitchesPerSecond),
				   String("Plus de fils prêts que de cœurs : les fils se volent le processeur."));
	// Mémoire.
	if (r.memory.available) {
		if (r.memory.growthMbPerMinute > 10.0)
			AddFinding(r, Severity::Warning, "Mémoire", String::Format("Mémoire résidente : +%.1f Mo/min après la mise en route", r.memory.growthMbPerMinute),
					   String::Format("de %.0f à %.0f Mo sur la plage (pic %.0f Mo) ; croissance mesurée sur son dernier tiers : fuite, ou "
									  "cache qui ne se vide pas.",
									  r.memory.startMb, r.memory.endMb, r.memory.peakMb));
		if (r.memory.minorFaultsPerSecond > 50000)
			AddFinding(r, Severity::Warning, "Mémoire", String::Format("%.0f défauts de page par seconde", r.memory.minorFaultsPerSecond),
					   String("Beaucoup de mémoire neuve touchée à chaque image : allocations et libérations de gros blocs."));
		if (r.memory.majorFaults > 0)
			AddFinding(r, Severity::Warning, "Mémoire", String::Format("%.0f défaut(s) de page majeur(s)", r.memory.majorFaults),
					   String("Pages relues sur le disque : mémoire insuffisante ou fichiers projetés froids."));
		AddFinding(r, Severity::Info, "Mémoire",
				   String::Format("Mémoire résidente %.0f Mo (pic %.0f Mo), tas %.0f Mo", r.memory.endMb, r.memory.peakMb, r.memory.heapPeakMb),
				   String::Format("%.0f défauts de page mineurs par seconde.", r.memory.minorFaultsPerSecond));
	}
	// Cache.
	if (r.cache.available) {
		if (r.cache.cacheMissesPerKiloInstruction > 10.0 || r.cache.ipc < 0.7)
			AddFinding(r, Severity::Warning, "Cache",
					   String::Format("Accès mémoire coûteux : IPC %.2f, %.1f défauts de cache / 1000 instructions", r.cache.ipc,
									  r.cache.cacheMissesPerKiloInstruction),
					   String("Données éparpillées (pointeurs, tables de hachage) : regrouper ce qui est lu ensemble."));
		else
			AddFinding(r, Severity::Info, "Cache",
					   String::Format("IPC %.2f, défauts de cache %.1f %%, %.1f / 1000 instructions", r.cache.ipc,
									  r.cache.cacheMissRate * 100.0, r.cache.cacheMissesPerKiloInstruction),
					   String::Format("%.1f mauvaises prédictions de branchement / 1000 instructions.", r.cache.branchMissesPerKiloInstruction));
	} else {
		AddFinding(r, Severity::Info, "Cache", String("Compteurs matériels indisponibles"),
				   c.hardwareReason.IsEmpty() ? String("capture sans compteurs matériels") : c.hardwareReason);
	}
	if (c.samplesDropped > 0 || c.zonesDropped > 0)
		AddFinding(r, Severity::Info, "Capture", String("Capture tronquée"),
				   String::Format("%zu échantillon(s) et %zu zone(s) perdus (tampons pleins) : capture plus courte ou tampons plus grands.",
								  c.samplesDropped, c.zonesDropped));
	std::stable_sort(r.findings.begin(), r.findings.end(), [](const Finding &a, const Finding &b) { return a.severity > b.severity; });
	return r;
}

CallNode BuildCallTree(const Capture &c, Symbolizer &symbols, const AnalysisOptions &options, bool bottomUp) {
	TreeBuilder root;
	std::vector<uint32_t> path;
	for (const StackSample &s : c.samples) {
		if (!options.range.Contains(s.time) || (options.thread != 0 && options.thread != s.tid) || s.depth == 0)
			continue;
		path.clear();
		for (uint32_t d = 0; d < s.depth; ++d)
			path.push_back(symbols.Resolve(c.stackFrames[s.first + d]).function);
		// Feuille d'abord : descendant = de la fin (main) vers la feuille.
		if (!bottomUp)
			std::reverse(path.begin(), path.end());
		++root.inclusive;
		TreeBuilder *node = &root;
		for (size_t i = 0; i < path.size(); ++i) {
			node = &node->Child(path[i]);
			++node->inclusive;
		}
		if (bottomUp) {
			if (!root.children.empty())
				++root.Child(path.front()).self;
		} else {
			++node->self;
		}
	}
	return root.Finish();
}

// ── Textes ───────────────────────────────────────────────────────────────────

String FormatReport(const Report &r, size_t top) {
	String out;
	out += String::Format("Plage : %.1f ms — %zu image(s), %zu échantillon(s) de pile\n\n", r.durationMs, r.frames.count, r.samples);
	out += String("DIAGNOSTIC\n");
	for (const Finding &f : r.findings)
		out += String::Format("  [%-9s] %-8s %s\n              %s\n", SeverityLabel(f.severity), f.category.CStr(), f.title.CStr(), f.detail.CStr());
	if (r.frames.count > 0)
		out += String::Format("\nIMAGES : %.1f img/s — moyenne %.2f ms, médiane %.2f, 95 %% %.2f, 99 %% %.2f, pire %.2f ms\n", r.frames.fps,
							  r.frames.averageMs, r.frames.p50Ms, r.frames.p95Ms, r.frames.p99Ms, r.frames.maxMs);
	out += String("\nFONCTIONS INSTRUMENTÉES (temps propre décroissant)\n");
	out += String::Format("  %-28s %8s %10s %10s %9s %9s %9s %9s %10s\n", "zone", "nombre", "total ms", "propre ms", "moy. ms", "min ms",
						  "max ms", "σ ms", "ms/image");
	for (size_t i = 0; i < r.zones.size() && i < top; ++i) {
		const ZoneStats &z = r.zones[i];
		out += String::Format("  %-28s %8llu %10.2f %10.2f %9.3f %9.3f %9.3f %9.3f %10.3f\n", z.label.CStr(), (unsigned long long)z.count,
							  Ms(z.totalNs), Ms(z.selfNs), z.meanNs / 1e6, Ms(z.minNs), Ms(z.maxNs), z.stddevNs / 1e6, z.perFrameMs);
	}
	out += String("\nFILS (temps CPU de l'ordonnanceur)\n");
	out += String::Format("  %-22s %8s %8s %8s %8s %8s %9s\n", "fil", "CPU ms", "cœur %", "actif %", "attente %", "E/S %", "échant.");
	for (size_t i = 0; i < r.threads.size() && i < top; ++i) {
		const ThreadStats &t = r.threads[i];
		out += String::Format("  %-22s %8.1f %8.1f %8.1f %8.1f %8.1f %9zu\n", t.name.CStr(), Ms(t.cpuNs), t.cpuShare * 100.0,
							  t.running * 100.0, t.sleeping * 100.0, t.waitingIo * 100.0, t.samples);
	}
	out += String("\nÉCHANTILLONNAGE (fonctions, temps propre)\n");
	out += String::Format("  %7s %7s  %s\n", "propre", "inclus", "fonction [module]");
	for (size_t i = 0; i < r.functions.size() && i < top; ++i) {
		const FunctionStats &fs = r.functions[i];
		out += String::Format("  %6.2f%% %6.2f%%  %s [%s]\n", fs.selfShare * 100.0, fs.inclusiveShare * 100.0, fs.name.CStr(), fs.module.CStr());
	}
	if (r.memory.available)
		out += String::Format("\nMÉMOIRE : résidente %.0f → %.0f Mo (pic %.0f, %+.1f Mo/min), tas pic %.0f Mo, %.0f défauts mineurs/s, "
							  "%.0f majeurs ; processus %.2f cœurs ; %.0f changements volontaires/s, %.0f préemptions/s\n",
							  r.memory.startMb, r.memory.endMb, r.memory.peakMb, r.memory.growthMbPerMinute, r.memory.heapPeakMb,
							  r.memory.minorFaultsPerSecond, r.memory.majorFaults, r.memory.processCpuShare,
							  r.memory.voluntarySwitchesPerSecond, r.memory.involuntarySwitchesPerSecond);
	out += r.cache.available ? String::Format("CACHE : IPC %.2f, défauts %.1f %% (%.1f / 1000 instr.), branchements %.1f / 1000 instr.\n",
											  r.cache.ipc, r.cache.cacheMissRate * 100.0, r.cache.cacheMissesPerKiloInstruction,
											  r.cache.branchMissesPerKiloInstruction)
							 : String("CACHE : indisponible — ") + r.cache.reason + String("\n");
	out += String::Format("GPU : présentation %.2f ms/image (%.0f %%), rendus hors écran %.2f ms/image\n", r.gpu.presentMsPerFrame,
						  r.gpu.presentShare * 100.0, r.gpu.offscreenMsPerFrame);
	return out;
}

namespace {
void FormatNode(String &out, const CallNode &n, const Symbolizer &symbols, uint64_t total, size_t depth, size_t maxDepth, double minShare) {
	for (const CallNode &child : n.children) {
		const double share = total ? double(child.inclusive) / double(total) : 0.0;
		if (share < minShare)
			continue;
		out += String::Format("%6.2f%% %6.2f%%  ", share * 100.0, total ? double(child.self) / double(total) * 100.0 : 0.0);
		for (size_t i = 0; i < depth; ++i)
			out += String("  ");
		out += symbols.Function(child.function) + String("\n");
		if (depth + 1 < maxDepth)
			FormatNode(out, child, symbols, total, depth + 1, maxDepth, minShare);
	}
}
} // namespace

String FormatCallTree(const CallNode &root, const Symbolizer &symbols, size_t maxDepth, double minShare) {
	String out = String::Format("%7s %7s  fonction (%llu échantillons)\n", "inclus", "propre", (unsigned long long)root.inclusive);
	FormatNode(out, root, symbols, root.inclusive, 0, maxDepth, minShare);
	return out;
}

String CompareReports(const Report &a, const Report &b, size_t top) {
	String out;
	auto delta = [](double before, double after) { return before > 0 ? (after - before) / before * 100.0 : 0.0; };
	out += String::Format("IMAGES : %.1f → %.1f img/s (%+.1f %%), moyenne %.2f → %.2f ms, 99 %% %.2f → %.2f ms\n\n", a.frames.fps,
						  b.frames.fps, delta(a.frames.fps, b.frames.fps), a.frames.averageMs, b.frames.averageMs, a.frames.p99Ms,
						  b.frames.p99Ms);
	struct Row {
		String label;
		double before = 0, after = 0;
	};
	std::map<String, Row> rows;
	for (const ZoneStats &z : a.zones)
		rows[z.label] = {z.label, Ms(z.selfNs) / std::max<double>(1.0, double(a.frames.count)), 0};
	for (const ZoneStats &z : b.zones) {
		Row &row = rows[z.label];
		row.label = z.label;
		row.after = Ms(z.selfNs) / std::max<double>(1.0, double(b.frames.count));
	}
	std::vector<Row> sorted;
	for (auto &[k, v] : rows)
		sorted.push_back(v);
	std::sort(sorted.begin(), sorted.end(), [](const Row &x, const Row &y) { return std::fabs(x.after - x.before) > std::fabs(y.after - y.before); });
	out += String::Format("ZONES (temps propre par image, plus grands écarts)\n  %-28s %10s %10s %10s\n", "zone", "avant ms", "après ms", "écart");
	for (size_t i = 0; i < sorted.size() && i < top; ++i)
		out += String::Format("  %-28s %10.3f %10.3f %+9.1f%%\n", sorted[i].label.CStr(), sorted[i].before, sorted[i].after,
							  delta(sorted[i].before, sorted[i].after));
	std::map<String, std::pair<double, double>> fns;
	for (const FunctionStats &f : a.functions)
		fns[f.name].first = f.selfShare;
	for (const FunctionStats &f : b.functions)
		fns[f.name].second = f.selfShare;
	std::vector<std::pair<String, std::pair<double, double>>> fsorted(fns.begin(), fns.end());
	std::sort(fsorted.begin(), fsorted.end(), [](const auto &x, const auto &y) {
		return std::fabs(x.second.second - x.second.first) > std::fabs(y.second.second - y.second.first);
	});
	out += String("\nFONCTIONS ÉCHANTILLONNÉES (part du temps propre, plus grands écarts)\n");
	for (size_t i = 0; i < fsorted.size() && i < top; ++i)
		out += String::Format("  %6.2f%% → %6.2f%%  %s\n", fsorted[i].second.first * 100.0, fsorted[i].second.second * 100.0,
							  fsorted[i].first.CStr());
	return out;
}

Result<bool, String> ExportChromeTrace(const Capture &c, const String &path) {
	FILE *f = std::fopen(path.CStr(), "w");
	if (!f)
		return Err(String::Format("%s : %s", path.CStr(), std::strerror(errno)));
	auto escape = [](const String &s) {
		String out;
		for (size_t i = 0; i < s.Length(); ++i) {
			if (s[i] == '"' || s[i] == '\\')
				out += '\\';
			out += s[i];
		}
		return out;
	};
	std::fprintf(f, "{\"traceEvents\":[\n");
	bool first = true;
	auto sep = [&] {
		std::fprintf(f, first ? "" : ",\n");
		first = false;
	};
	for (long tid : c.ThreadIds()) {
		sep();
		std::fprintf(f, "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":%ld,\"tid\":%ld,\"args\":{\"name\":\"%s\"}}", c.pid, tid,
					 escape(c.ThreadName(tid)).CStr());
	}
	for (const ZoneEvent &z : c.zones) {
		sep();
		std::fprintf(f, "{\"name\":\"%s\",\"ph\":\"X\",\"pid\":%ld,\"tid\":%ld,\"ts\":%.3f,\"dur\":%.3f}", escape(c.Name(z.name)).CStr(), c.pid,
					 z.tid, double(z.start) / 1e3, double(z.Duration()) / 1e3);
	}
	for (const ValueEvent &v : c.values) {
		sep();
		std::fprintf(f, "{\"name\":\"%s\",\"ph\":\"C\",\"pid\":%ld,\"ts\":%.3f,\"args\":{\"valeur\":%.9g}}", escape(c.Name(v.name)).CStr(), c.pid,
					 double(v.time) / 1e3, v.value);
	}
	for (int64_t t : c.frames) {
		sep();
		std::fprintf(f, "{\"name\":\"image\",\"ph\":\"i\",\"s\":\"g\",\"pid\":%ld,\"ts\":%.3f}", c.pid, double(t) / 1e3);
	}
	for (const ProcessSample &p : c.process) {
		sep();
		std::fprintf(f, "{\"name\":\"mémoire (Mo)\",\"ph\":\"C\",\"pid\":%ld,\"ts\":%.3f,\"args\":{\"résidente\":%.2f,\"tas\":%.2f}}", c.pid,
					 double(p.time) / 1e3, double(p.rss) / 1048576.0, double(p.heap) / 1048576.0);
	}
	std::fprintf(f, "\n]}\n");
	if (std::fclose(f) != 0)
		return Err(path + String(" : écriture incomplète"));
	return Ok(true);
}

} // namespace profiler
