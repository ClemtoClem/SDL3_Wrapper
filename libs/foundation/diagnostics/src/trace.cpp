// Définitions de diagnostics/trace.hpp
#include <diagnostics/trace.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#ifdef __linux__
#include <csignal>
#include <dirent.h>
#include <execinfo.h>
#include <linux/perf_event.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace diagnostics::trace {

namespace {

struct ZoneRecord {
	const char *name;
	uint64_t start, end;
	uint32_t depth;
};

struct ValueRecord {
	const char *name;
	uint64_t time;
	double value;
};

struct AsyncRecord {
	const char *name;
	uint64_t id, time;
	long tid;
	bool begin;
};

/// Données d'un fil instrumenté : jamais libérées (le pointeur de fil reste
/// valide d'une capture à l'autre, même après la fin du fil).
struct ThreadData {
	long tid = 0;
	String name;
	std::mutex mutex;
	std::vector<ZoneRecord> zones;
	std::vector<ValueRecord> values;
	uint32_t depth = 0;
	size_t dropped = 0;
};

constexpr int MAX_FRAMES = 48;

/// Écrit depuis le gestionnaire de SIGPROF : aucune allocation.
struct StackSample {
	uint64_t time;
	long tid;
	int depth;
	void *frames[MAX_FRAMES];
	std::atomic<bool> ready;
};

struct TaskSample {
	long tid;
	uint64_t time;
	uint64_t cpuNs;
	char state;
};

struct ProcessSample {
	uint64_t time;
	uint64_t rss, heap;
	long minflt, majflt, nvcsw, nivcsw;
	uint64_t userNs, sysNs;
};

constexpr int HW_COUNTERS = 5;
struct HardwareSample {
	uint64_t time;
	uint64_t values[HW_COUNTERS];
};

struct Module {
	uint64_t start, end, offset;
	String path;
};

struct State {
	std::atomic<bool> active{false};
	std::mutex mutex; // fils, images, asynchrones
	std::vector<std::unique_ptr<ThreadData>> threads;
	std::vector<uint64_t> frames;
	std::vector<AsyncRecord> async;
	Options options;
	uint64_t start = 0, end = 0;
	bool captured = false;

	// Échantillonnage des piles.
	std::unique_ptr<StackSample[]> samples;
	size_t sampleCapacity = 0;
	std::atomic<size_t> sampleCursor{0};
	std::atomic<size_t> samplesDropped{0};
	std::atomic<bool> sampling{false};

	// Échantillonneur du processus.
	std::thread sampler;
	std::atomic<bool> samplerRunning{false};
	std::mutex samplerMutex;
	std::vector<TaskSample> tasks;
	std::unordered_map<long, String> taskNames;
	std::vector<ProcessSample> process;
	std::vector<HardwareSample> hardware;
	bool hardwareAvailable = false;
	String hardwareReason;
	std::vector<int> hardwareFds; // HW_COUNTERS par fil ouvert

	std::vector<Module> modules;

	// Noms durables.
	std::mutex internMutex;
	std::unordered_map<String, std::unique_ptr<char[]>> interned;

	// Pilotage par l'environnement.
	String envPath;
	Options envOptions;
	bool envStarted = false;
	std::atomic<bool> toggleRequested{false};
	int captureIndex = 0;
};

State &G() {
	// Jamais détruit : le gestionnaire de signal et les fils détachés peuvent
	// y accéder jusqu'à la toute fin du processus.
	static State *state = new State;
	return *state;
}

thread_local ThreadData *t_thread = nullptr;

long CurrentTid() noexcept {
#ifdef __linux__
	return long(syscall(SYS_gettid));
#else
	return 0;
#endif
}

ThreadData *Current() {
	if (t_thread)
		return t_thread;
	auto data = std::make_unique<ThreadData>();
	data->tid = CurrentTid();
	data->zones.reserve(4096);
	ThreadData *raw = data.get();
	{
		std::lock_guard<std::mutex> lock(G().mutex);
		G().threads.push_back(std::move(data));
	}
	t_thread = raw;
	return raw;
}

// ── Lecture de /proc ─────────────────────────────────────────────────────────

#ifdef __linux__
bool ReadFile(const char *path, char *buffer, size_t size) {
	FILE *f = std::fopen(path, "r");
	if (!f)
		return false;
	const size_t n = std::fread(buffer, 1, size - 1, f);
	std::fclose(f);
	buffer[n] = '\0';
	return n > 0;
}

/// Nom, état et temps CPU (ns) d'un fil.
bool ReadTask(long tid, String &name, char &state, uint64_t &cpuNs) {
	char path[96], buffer[1024];
	std::snprintf(path, sizeof path, "/proc/self/task/%ld/stat", tid);
	if (!ReadFile(path, buffer, sizeof buffer))
		return false;
	const char *open = std::strchr(buffer, '(');
	const char *close = std::strrchr(buffer, ')');
	if (!open || !close || close < open)
		return false;
	name = String(StringView(open + 1, size_t(close - open - 1)));
	state = close[2];
	// schedstat : temps passé sur le processeur, en ns (précis, contrairement
	// aux tics d'horloge de stat).
	std::snprintf(path, sizeof path, "/proc/self/task/%ld/schedstat", tid);
	if (ReadFile(path, buffer, sizeof buffer)) {
		cpuNs = std::strtoull(buffer, nullptr, 10);
		return true;
	}
	// Repli : utime et stime (champs 14 et 15) en tics.
	const char *p = close + 2;
	unsigned long long values[13] = {};
	int field = 3;
	while (*p && field <= 15) {
		while (*p == ' ')
			++p;
		const char *start = p;
		while (*p && *p != ' ')
			++p;
		if (field >= 14)
			values[field - 14] = std::strtoull(start, nullptr, 10);
		++field;
	}
	const long ticks = sysconf(_SC_CLK_TCK);
	cpuNs = (values[0] + values[1]) * 1000000000ull / uint64_t(ticks > 0 ? ticks : 100);
	return true;
}

std::vector<long> ListTasks() {
	std::vector<long> out;
	if (DIR *dir = opendir("/proc/self/task")) {
		while (dirent *entry = readdir(dir))
			if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9')
				out.push_back(std::strtol(entry->d_name, nullptr, 10));
		closedir(dir);
	}
	return out;
}

std::vector<Module> ReadModules() {
	std::vector<Module> out;
	FILE *f = std::fopen("/proc/self/maps", "r");
	if (!f)
		return out;
	char line[4096];
	while (std::fgets(line, sizeof line, f)) {
		unsigned long long start = 0, end = 0, offset = 0;
		char perms[8] = {}, path[3000] = {};
		const int n = std::sscanf(line, "%llx-%llx %7s %llx %*s %*s %2999[^\n]", &start, &end, perms, &offset, path);
		if (n < 5 || !std::strchr(perms, 'x') || path[0] != '/')
			continue;
		out.push_back({start, end, offset, String(path)});
	}
	std::fclose(f);
	return out;
}

// ── Compteurs matériels ─────────────────────────────────────────────────────

int PerfOpen(perf_event_attr &attr, long tid) {
	return int(syscall(SYS_perf_event_open, &attr, pid_t(tid), -1, -1, 0));
}

void OpenHardwareCounters(State &g) {
	g.hardwareFds.clear();
	g.hardwareAvailable = false;
	g.hardwareReason = String();
	if (!g.options.hardwareCounters) {
		g.hardwareReason = String("désactivés");
		return;
	}
	static const uint64_t CONFIG[HW_COUNTERS] = {PERF_COUNT_HW_CPU_CYCLES, PERF_COUNT_HW_INSTRUCTIONS,
												 PERF_COUNT_HW_CACHE_REFERENCES, PERF_COUNT_HW_CACHE_MISSES,
												 PERF_COUNT_HW_BRANCH_MISSES};
	for (long tid : ListTasks())
		for (int i = 0; i < HW_COUNTERS; ++i) {
			perf_event_attr attr{};
			attr.size = sizeof attr;
			attr.type = PERF_TYPE_HARDWARE;
			attr.config = CONFIG[i];
			attr.exclude_kernel = 1;
			attr.exclude_hv = 1;
			attr.inherit = 1; // fils créés ensuite par ce fil
			const int fd = PerfOpen(attr, tid);
			if (fd < 0) {
				const int error = errno;
				for (int open : g.hardwareFds)
					close(open);
				g.hardwareFds.clear();
				char paranoid[16] = "?";
				(void)ReadFile("/proc/sys/kernel/perf_event_paranoid", paranoid, sizeof paranoid);
				for (char *c = paranoid; *c; ++c)
					if (*c == '\n')
						*c = '\0';
				g.hardwareReason = String::Format("perf_event_open : %s (perf_event_paranoid = %s ; "
												  "« sudo sysctl kernel.perf_event_paranoid=1 » les autorise)",
												  std::strerror(error), paranoid);
				return;
			}
			g.hardwareFds.push_back(fd);
		}
	g.hardwareAvailable = !g.hardwareFds.empty();
}

void CloseHardwareCounters(State &g) {
	for (int fd : g.hardwareFds)
		close(fd);
	g.hardwareFds.clear();
}

void SampleHardware(State &g, uint64_t now) {
	if (!g.hardwareAvailable)
		return;
	HardwareSample s{now, {}};
	for (size_t i = 0; i < g.hardwareFds.size(); ++i) {
		uint64_t value = 0;
		if (read(g.hardwareFds[i], &value, sizeof value) == ssize_t(sizeof value))
			s.values[i % HW_COUNTERS] += value;
	}
	g.hardware.push_back(s);
}

// ── Échantillonnage des piles (SIGPROF) ─────────────────────────────────────

struct sigaction g_previousProf {};

void OnProf(int, siginfo_t *, void *) {
	State &g = G();
	if (!g.sampling.load(std::memory_order_relaxed))
		return;
	const int savedErrno = errno;
	const size_t i = g.sampleCursor.fetch_add(1, std::memory_order_relaxed);
	if (i >= g.sampleCapacity) {
		g.samplesDropped.fetch_add(1, std::memory_order_relaxed);
		errno = savedErrno;
		return;
	}
	StackSample &s = g.samples[i];
	s.time = NowNs();
	s.tid = CurrentTid();
	s.depth = backtrace(s.frames, MAX_FRAMES);
	s.ready.store(true, std::memory_order_release);
	errno = savedErrno;
}

void StartSampling(State &g) {
	if (g.options.samplingHz <= 0 || g.options.maxSamples == 0)
		return;
	if (g.sampleCapacity != g.options.maxSamples) {
		g.samples.reset(new StackSample[g.options.maxSamples]);
		g.sampleCapacity = g.options.maxSamples;
	}
	for (size_t i = 0; i < g.sampleCapacity; ++i)
		g.samples[i].ready.store(false, std::memory_order_relaxed);
	g.sampleCursor.store(0);
	g.samplesDropped.store(0);
	// Premier appel hors signal : backtrace charge libgcc (allocation).
	void *warm[4];
	(void)backtrace(warm, 4);
	struct sigaction action {};
	action.sa_sigaction = &OnProf;
	action.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPROF, &action, &g_previousProf);
	g.sampling.store(true);
	const long usec = std::max(100L, 1000000L / long(g.options.samplingHz));
	itimerval timer{};
	timer.it_interval.tv_usec = usec % 1000000;
	timer.it_interval.tv_sec = usec / 1000000;
	timer.it_value = timer.it_interval;
	setitimer(ITIMER_PROF, &timer, nullptr);
}

void StopSampling(State &g) {
	if (!g.sampling.load())
		return;
	itimerval off{};
	setitimer(ITIMER_PROF, &off, nullptr);
	g.sampling.store(false);
	sigaction(SIGPROF, &g_previousProf, nullptr);
}

// ── Échantillonneur du processus ────────────────────────────────────────────

void SampleProcess(State &g) {
	const uint64_t now = NowNs();
	std::lock_guard<std::mutex> lock(g.samplerMutex);
	for (long tid : ListTasks()) {
		String name;
		char state = '?';
		uint64_t cpu = 0;
		if (!ReadTask(tid, name, state, cpu))
			continue;
		g.taskNames[tid] = name;
		g.tasks.push_back({tid, now, cpu, state});
	}
	ProcessSample p{};
	p.time = now;
	char buffer[256];
	if (ReadFile("/proc/self/statm", buffer, sizeof buffer)) {
		unsigned long long size = 0, resident = 0;
		if (std::sscanf(buffer, "%llu %llu", &size, &resident) == 2)
			p.rss = resident * uint64_t(sysconf(_SC_PAGESIZE));
	}
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
	// mallinfo2 parcourt tout le tas (et verrouille ses arènes) : 10 fois
	// moins souvent que le reste, la valeur précédente entre-temps.
	if (g.process.empty() || g.process.size() % 10 == 0)
		p.heap = uint64_t(mallinfo2().uordblks);
	else
		p.heap = g.process.back().heap;
#endif
	rusage usage{};
	if (getrusage(RUSAGE_SELF, &usage) == 0) {
		p.minflt = usage.ru_minflt;
		p.majflt = usage.ru_majflt;
		p.nvcsw = usage.ru_nvcsw;
		p.nivcsw = usage.ru_nivcsw;
		p.userNs = uint64_t(usage.ru_utime.tv_sec) * 1000000000ull + uint64_t(usage.ru_utime.tv_usec) * 1000ull;
		p.sysNs = uint64_t(usage.ru_stime.tv_sec) * 1000000000ull + uint64_t(usage.ru_stime.tv_usec) * 1000ull;
	}
	g.process.push_back(p);
	SampleHardware(g, now);
}

void SamplerLoop(State *g) {
#ifdef __linux__
	pthread_setname_np(pthread_self(), "diag-sampler");
#endif
	const auto period = std::chrono::nanoseconds(1000000000LL / std::max(1, g->options.processHz));
	auto next = std::chrono::steady_clock::now();
	while (g->samplerRunning.load()) {
		SampleProcess(*g);
		next += period;
		std::this_thread::sleep_until(next);
	}
}

void OnToggle(int) { G().toggleRequested.store(true); }
#endif // __linux__

String Escape(const char *text) {
	String out;
	for (const char *c = text; c && *c; ++c) {
		if (*c == '"' || *c == '\\') {
			out += '\\';
			out += *c;
		} else if (uint8_t(*c) < 0x20) {
			out += ' ';
		} else {
			out += *c;
		}
	}
	return out;
}

} // namespace

// ── API ──────────────────────────────────────────────────────────────────────

uint64_t NowNs() noexcept {
	timespec ts{};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

bool Active() noexcept { return G().active.load(std::memory_order_relaxed); }

const char *Intern(StringView name) {
	State &g = G();
	std::lock_guard<std::mutex> lock(g.internMutex);
	const String key(name);
	if (auto it = g.interned.find(key); it != g.interned.end())
		return it->second.get();
	auto copy = std::make_unique<char[]>(key.Length() + 1);
	std::memcpy(copy.get(), key.CStr(), key.Length() + 1);
	const char *raw = copy.get();
	g.interned.emplace(key, std::move(copy));
	return raw;
}

void SetThreadName(StringView name) {
	ThreadData *t = Current();
	{
		std::lock_guard<std::mutex> lock(t->mutex);
		t->name = String(name);
	}
#ifdef __linux__
	char shortName[16] = {};
	std::memcpy(shortName, name.GetData(), std::min<size_t>(15, name.GetSize()));
	pthread_setname_np(pthread_self(), shortName);
#endif
}

bool Start(const Options &options) {
	State &g = G();
	if (g.active.load())
		return false;
	{
		std::lock_guard<std::mutex> lock(g.mutex);
		g.options = options;
		g.frames.clear();
		g.async.clear();
		for (auto &t : g.threads) {
			std::lock_guard<std::mutex> tl(t->mutex);
			t->zones.clear();
			t->values.clear();
			t->dropped = 0;
		}
	}
	{
		std::lock_guard<std::mutex> lock(g.samplerMutex);
		g.tasks.clear();
		g.process.clear();
		g.hardware.clear();
	}
	g.modules.clear();
	g.start = NowNs();
	g.end = g.start;
	g.captured = false;
	g.active.store(true);
#ifdef __linux__
	OpenHardwareCounters(g);
	StartSampling(g);
	if (options.processHz > 0) {
		g.samplerRunning.store(true);
		g.sampler = std::thread(SamplerLoop, &g);
	}
#endif
	return true;
}

bool Stop() {
	State &g = G();
	if (!g.active.load())
		return false;
	g.active.store(false);
#ifdef __linux__
	StopSampling(g);
	if (g.sampler.joinable()) {
		g.samplerRunning.store(false);
		g.sampler.join();
	}
	SampleProcess(g); // état final
	CloseHardwareCounters(g);
	g.modules = ReadModules();
#endif
	g.end = NowNs();
	g.captured = true;
	return true;
}

void Frame() {
	if (!Active())
		return;
	State &g = G();
	std::lock_guard<std::mutex> lock(g.mutex);
	g.frames.push_back(NowNs());
}

void Value(const char *name, double value) {
	if (!Active())
		return;
	ThreadData *t = Current();
	std::lock_guard<std::mutex> lock(t->mutex);
	if (t->values.size() < G().options.maxZonesPerThread)
		t->values.push_back({name, NowNs(), value});
}

void AsyncBegin(const char *name, uint64_t id) {
	if (!Active())
		return;
	State &g = G();
	std::lock_guard<std::mutex> lock(g.mutex);
	g.async.push_back({name, id, NowNs(), CurrentTid(), true});
}

void AsyncEnd(const char *name, uint64_t id) {
	if (!Active())
		return;
	State &g = G();
	std::lock_guard<std::mutex> lock(g.mutex);
	g.async.push_back({name, id, NowNs(), CurrentTid(), false});
}

Zone::Zone(const char *name) noexcept {
	if (!Active() || !name)
		return;
	ThreadData *t = Current();
	m_name = name;
	m_depth = t->depth++;
	m_start = NowNs();
}

Zone::~Zone() {
	if (!m_name)
		return;
	const uint64_t end = NowNs();
	ThreadData *t = t_thread;
	if (!t)
		return;
	t->depth = m_depth;
	if (!Active())
		return;
	std::lock_guard<std::mutex> lock(t->mutex);
	if (t->zones.size() < G().options.maxZonesPerThread)
		t->zones.push_back({m_name, m_start, end, m_depth});
	else
		++t->dropped;
}

// ── Écriture ─────────────────────────────────────────────────────────────────

Result<bool, String> Save(const String &path) {
	State &g = G();
	if (g.active.load())
		return Err(String("capture en cours : Stop() d'abord"));
	if (!g.captured)
		return Err(String("aucune capture"));
	FILE *f = std::fopen(path.CStr(), "w");
	if (!f)
		return Err(String::Format("%s : %s", path.CStr(), std::strerror(errno)));
	const uint64_t t0 = g.start;
	auto rel = [t0](uint64_t t) { return t >= t0 ? (long long)(t - t0) : 0LL; };

	// Noms des zones et valeurs : une table, les enregistrements y renvoient.
	std::unordered_map<const char *, size_t> nameIndex;
	std::vector<const char *> names;
	auto nameOf = [&](const char *n) {
		auto [it, inserted] = nameIndex.emplace(n, names.size());
		if (inserted)
			names.push_back(n);
		return it->second;
	};

	std::lock_guard<std::mutex> lock(g.mutex);
	std::lock_guard<std::mutex> samplerLock(g.samplerMutex);
	std::fprintf(f, "{\n\"format\": \"diagnostics-capture\", \"version\": 1,\n");
	std::fprintf(f, "\"pid\": %ld, \"duration_ns\": %lld, \"sampling_hz\": %d, \"process_hz\": %d,\n",
#ifdef __linux__
				 long(getpid()),
#else
				 0L,
#endif
				 rel(g.end), g.options.samplingHz, g.options.processHz);
	char exe[2048] = {};
#ifdef __linux__
	const ssize_t exeLength = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (exeLength > 0)
		exe[exeLength] = '\0';
#endif
	std::fprintf(f, "\"executable\": \"%s\",\n", Escape(exe).CStr());

	// Fils : instrumentés (nommés par le programme) et vus par /proc.
	std::unordered_map<long, String> threadNames = g.taskNames;
	for (const auto &t : g.threads)
		if (!t->name.IsEmpty())
			threadNames[t->tid] = t->name;
	std::fprintf(f, "\"threads\": [");
	bool first = true;
	for (const auto &[tid, name] : threadNames) {
		std::fprintf(f, "%s\n  {\"tid\": %ld, \"name\": \"%s\"}", first ? "" : ",", tid, Escape(name.CStr()).CStr());
		first = false;
	}
	std::fprintf(f, "],\n");

	std::fprintf(f, "\"modules\": [");
	first = true;
	for (const Module &m : g.modules) {
		std::fprintf(f, "%s\n  {\"start\": %llu, \"end\": %llu, \"offset\": %llu, \"path\": \"%s\"}", first ? "" : ",",
					 (unsigned long long)m.start, (unsigned long long)m.end, (unsigned long long)m.offset, Escape(m.path.CStr()).CStr());
		first = false;
	}
	std::fprintf(f, "],\n");

	// Zones : [tid, nom, début, fin, profondeur].
	std::fprintf(f, "\"zones\": [");
	first = true;
	size_t dropped = 0;
	for (const auto &t : g.threads) {
		std::lock_guard<std::mutex> tl(t->mutex);
		dropped += t->dropped;
		for (const ZoneRecord &z : t->zones) {
			std::fprintf(f, "%s[%ld,%zu,%lld,%lld,%u]", first ? "\n" : ",\n", t->tid, nameOf(z.name), rel(z.start), rel(z.end), z.depth);
			first = false;
		}
	}
	std::fprintf(f, "],\n\"zones_dropped\": %zu,\n", dropped);

	std::fprintf(f, "\"values\": [");
	first = true;
	for (const auto &t : g.threads) {
		std::lock_guard<std::mutex> tl(t->mutex);
		for (const ValueRecord &v : t->values) {
			std::fprintf(f, "%s[%ld,%zu,%lld,%.9g]", first ? "\n" : ",\n", t->tid, nameOf(v.name), rel(v.time), v.value);
			first = false;
		}
	}
	std::fprintf(f, "],\n");

	std::fprintf(f, "\"async\": [");
	first = true;
	for (const AsyncRecord &a : g.async) {
		std::fprintf(f, "%s[%ld,%zu,%llu,%lld,%d]", first ? "\n" : ",\n", a.tid, nameOf(a.name), (unsigned long long)a.id, rel(a.time),
					 a.begin ? 1 : 0);
		first = false;
	}
	std::fprintf(f, "],\n");

	std::fprintf(f, "\"frames\": [");
	for (size_t i = 0; i < g.frames.size(); ++i)
		std::fprintf(f, "%s%lld", i ? "," : "", rel(g.frames[i]));
	std::fprintf(f, "],\n");

	// Échantillons de pile : [tid, temps, adresse…] (feuille d'abord ; les
	// deux premiers cadres — le gestionnaire et le trampoline du signal — sont
	// retirés).
	std::fprintf(f, "\"samples\": [");
	first = true;
	const size_t sampleCount = std::min(g.sampleCursor.load(), g.sampleCapacity);
	for (size_t i = 0; i < sampleCount; ++i) {
		const StackSample &s = g.samples[i];
		if (!s.ready.load(std::memory_order_acquire))
			continue;
		std::fprintf(f, "%s[%ld,%lld", first ? "\n" : ",\n", s.tid, rel(s.time));
		for (int d = 2; d < s.depth; ++d)
			std::fprintf(f, ",%llu", (unsigned long long)uintptr_t(s.frames[d]));
		std::fprintf(f, "]");
		first = false;
	}
	std::fprintf(f, "],\n\"samples_dropped\": %zu,\n", g.samplesDropped.load());

	// États des fils : [tid, temps, CPU ns, état].
	std::fprintf(f, "\"tasks\": [");
	first = true;
	for (const TaskSample &t : g.tasks) {
		std::fprintf(f, "%s[%ld,%lld,%llu,\"%c\"]", first ? "\n" : ",\n", t.tid, rel(t.time), (unsigned long long)t.cpuNs,
					 t.state >= 'A' && t.state <= 'z' ? t.state : '?');
		first = false;
	}
	std::fprintf(f, "],\n");

	// Processus : [temps, RSS, tas, défauts mineurs, majeurs, changements
	// volontaires, involontaires, CPU utilisateur ns, système ns].
	std::fprintf(f, "\"process\": [");
	first = true;
	for (const ProcessSample &p : g.process) {
		std::fprintf(f, "%s[%lld,%llu,%llu,%ld,%ld,%ld,%ld,%llu,%llu]", first ? "\n" : ",\n", rel(p.time), (unsigned long long)p.rss,
					 (unsigned long long)p.heap, p.minflt, p.majflt, p.nvcsw, p.nivcsw, (unsigned long long)p.userNs,
					 (unsigned long long)p.sysNs);
		first = false;
	}
	std::fprintf(f, "],\n");

	// Compteurs matériels : [temps, cycles, instructions, références cache,
	// défauts cache, mauvaises prédictions de branchement] (cumulés).
	std::fprintf(f, "\"hardware\": {\"available\": %s, \"reason\": \"%s\", \"samples\": [", g.hardwareAvailable ? "true" : "false",
				 Escape(g.hardwareReason.CStr()).CStr());
	first = true;
	for (const HardwareSample &h : g.hardware) {
		std::fprintf(f, "%s[%lld,%llu,%llu,%llu,%llu,%llu]", first ? "\n" : ",\n", rel(h.time), (unsigned long long)h.values[0],
					 (unsigned long long)h.values[1], (unsigned long long)h.values[2], (unsigned long long)h.values[3],
					 (unsigned long long)h.values[4]);
		first = false;
	}
	std::fprintf(f, "]},\n");

	std::fprintf(f, "\"names\": [");
	for (size_t i = 0; i < names.size(); ++i)
		std::fprintf(f, "%s\"%s\"", i ? "," : "", Escape(names[i]).CStr());
	std::fprintf(f, "]\n}\n");
	const bool ok = std::fclose(f) == 0;
	if (!ok)
		return Err(String::Format("%s : écriture incomplète", path.CStr()));
	return Ok(true);
}

// ── Environnement ────────────────────────────────────────────────────────────

bool StartFromEnvironment() {
	State &g = G();
	const char *path = std::getenv("DIAG_TRACE");
	if (!path || !*path)
		return false;
	g.envPath = String(path);
	if (const char *hz = std::getenv("DIAG_TRACE_HZ"))
		g.envOptions.samplingHz = std::max(0, std::atoi(hz));
	const char *control = std::getenv("DIAG_TRACE_CONTROL");
#ifdef __linux__
	if (control && *control && *control != '0') {
		struct sigaction action {};
		action.sa_handler = &OnToggle;
		action.sa_flags = SA_RESTART;
		sigemptyset(&action.sa_mask);
		sigaction(SIGUSR2, &action, nullptr);
	}
#endif
	// « wait » : le profiler démarrera la capture lui-même (SIGUSR2).
	if (control && String(control) == "wait")
		return false;
	g.envStarted = Start(g.envOptions);
	return g.envStarted;
}

void PollControl() {
	State &g = G();
	if (!g.toggleRequested.exchange(false))
		return;
	if (Active()) {
		(void)Stop();
		String base = g.envPath;
		if (base.EndsWith(".vcap"))
			base = base.Substr(0, base.Length() - 5);
		(void)Save(String::Format("%s.%d.vcap", base.CStr(), ++g.captureIndex));
	} else {
		(void)Start(g.envOptions);
	}
}

void FinishFromEnvironment() {
	State &g = G();
	if (g.envPath.IsEmpty() || !Active())
		return;
	(void)Stop();
	(void)Save(g.envPath);
}

} // namespace diagnostics::trace
