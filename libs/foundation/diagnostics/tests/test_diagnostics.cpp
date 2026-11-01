#include "test.hpp"
#include <diagnostics/diagnostics.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace diagnostics;

namespace {
/// Journal isolé du journal global, nettoyé en fin de test.
struct CapturedLog {
	Logger logger;
	MemorySink memory{16};
	CapturedLog() { logger.AddSink(memory.AsSink()); }
};
} // namespace

// ── Logger ───────────────────────────────────────────────────────────────────

TEST(Logger, DeliversToEverySinkAboveTheLevel) {
	CapturedLog log;
	int other = 0;
	log.logger.AddSink([&](const Record &) { ++other; });
	log.logger.Write(Level::Info, "app", "bonjour");
	log.logger.Write(Level::Debug, "app", "ignoré : sous le niveau Info par défaut");
	auto entries = log.memory.Entries();
	REQUIRE_EQ(entries.size(), 1u);
	CHECK_EQ(entries[0].category, "app");
	CHECK_EQ(entries[0].message, "bonjour");
	CHECK_EQ(other, 1);
}

TEST(Logger, CategoryLevelOverridesTheGlobalLevel) {
	CapturedLog log;
	log.logger.SetLevel(Level::Warn);
	log.logger.SetCategoryLevel("physics", Level::Trace);
	log.logger.Log(Level::Info, "render", "filtré");
	log.logger.Log(Level::Trace, "physics", "pas {} contact(s)", 3);
	auto entries = log.memory.Entries();
	REQUIRE_EQ(entries.size(), 1u);
	CHECK_EQ(entries[0].message, "pas 3 contact(s)");
	CHECK(!log.logger.IsEnabled(Level::Off, "physics"));
}

TEST(Logger, RemovedSinkReceivesNothing) {
	Logger logger;
	int n = 0;
	size_t id = logger.AddSink([&](const Record &) { ++n; });
	logger.Write(Level::Error, "x", "1");
	logger.RemoveSink(id);
	logger.Write(Level::Error, "x", "2");
	CHECK_EQ(n, 1);
	CHECK_EQ(logger.AddSink(nullptr), 0u);
}

TEST(MemorySink, KeepsOnlyTheLastEntries) {
	Logger logger;
	MemorySink memory(3);
	logger.AddSink(memory.AsSink());
	for (int i = 0; i < 5; ++i)
		logger.Log(i % 2 ? Level::Error : Level::Info, "x", "{}", i);
	auto entries = memory.Entries();
	REQUIRE_EQ(entries.size(), 3u);
	CHECK_EQ(entries.front().message, "2");
	CHECK_EQ(memory.Count(Level::Error), 1u); // seul "3" parmi 2,3,4
	memory.Clear();
	CHECK_EQ(memory.Count(), 0u);
}

TEST(Logger, IsSafeFromSeveralThreads) {
	Logger logger;
	MemorySink memory(10000);
	logger.AddSink(memory.AsSink());
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; ++t)
		threads.emplace_back([&] {
			for (int i = 0; i < 500; ++i)
				logger.Write(Level::Info, "mt", "m");
		});
	for (auto &t : threads)
		t.join();
	CHECK_EQ(memory.Count(), 2000u);
}

TEST(FileSink, AppendsFormattedLines) {
	const std::string path = (std::filesystem::temp_directory_path() / "diagnostics_file_sink.log").string();
	std::filesystem::remove(path);
	{
		Logger logger;
		logger.AddSink(FileSink(path));
		logger.Write(Level::Error, "io", "disque plein");
	}
	std::ifstream in(path);
	std::string line;
	std::getline(in, line);
	CHECK(line.find("[Error   ] io: disque plein") != std::string::npos);
	CHECK(!FileSink("/dossier/inexistant/x.log"));
}

TEST(Logger, MacrosUseTheGlobalLogger) {
	MemorySink memory;
	size_t id = Logger::Global().AddSink(memory.AsSink());
	DIAG_WARN("macro", "valeur = {}", 42);
	DIAG_TRACE("macro", "filtré par le niveau global");
	Logger::Global().RemoveSink(id);
	auto entries = memory.Entries();
	REQUIRE_EQ(entries.size(), 1u);
	CHECK_EQ(entries[0].message, "valeur = 42");
	CHECK(entries[0].level == Level::Warn);
}

// ── Assertions ───────────────────────────────────────────────────────────────

TEST(Assert, VerifyCallsTheHandlerAndReturnsFalse) {
	std::vector<std::string> failures;
	auto previous = SetAssertHandler([&](const AssertionFailure &f) {
		failures.push_back(std::string(f.expression) + " | " + std::string(f.message));
	});
	int value = 3;
	CHECK(DIAG_VERIFY(value == 3, "jamais affiché"));
	CHECK(!DIAG_VERIFY(value == 4, "attendu {}", 4));
	CHECK(!DIAG_VERIFY(value > 10));
	SetAssertHandler(previous);
	REQUIRE_EQ(failures.size(), 2u);
	CHECK_EQ(failures[0], "value == 4 | attendu 4");
	CHECK_EQ(failures[1], "value > 10 | ");
}

TEST(Assert, AssertIsActiveInDebugBuilds) {
	int calls = 0;
	auto previous = SetAssertHandler([&](const AssertionFailure &) { ++calls; });
	DIAG_ASSERT(1 + 1 == 3, "arithmétique");
	DIAG_ASSERT(true);
	SetAssertHandler(previous);
#if DIAG_ENABLE_ASSERTS
	CHECK_EQ(calls, 1);
#else
	CHECK_EQ(calls, 0);
#endif
}

// ── Profiler ─────────────────────────────────────────────────────────────────

TEST(Profiler, AccumulatesTimingStats) {
	Profiler p;
	p.Record("step", std::chrono::milliseconds(2));
	p.Record("step", std::chrono::milliseconds(4));
	TimingStats s = p.Timing("step");
	CHECK_EQ(s.count, 2u);
	CHECK_NEAR(s.MeanMs(), 3.0, 1e-9);
	CHECK_NEAR(s.MinMs(), 2.0, 1e-9);
	CHECK_NEAR(s.MaxMs(), 4.0, 1e-9);
	CHECK_EQ(p.Timing("absent").count, 0u);
	CHECK_NEAR(p.Timing("absent").MeanMs(), 0.0, 1e-12);
}

TEST(Profiler, ScopedTimerRecordsItsLifetime) {
	Profiler p;
	{
		ScopedTimer t("sleep", p);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	TimingStats s = p.Timing("sleep");
	CHECK_EQ(s.count, 1u);
	CHECK(s.MeanMs() >= 4.0);
}

TEST(Profiler, CountersAndReset) {
	Profiler p;
	p.Increment("draw_calls", 10);
	p.Increment("draw_calls");
	p.Increment("frames");
	CHECK_EQ(p.Counter("draw_calls"), 11);
	CHECK_EQ(p.Counters().size(), 2u);
	p.Reset();
	CHECK_EQ(p.Counter("draw_calls"), 0);
	CHECK(p.Timings().empty());
}

TEST(Profiler, ProfileScopeMacroUsesTheGlobalProfiler) {
	Profiler::Global().Reset();
	{ DIAG_PROFILE_SCOPE("macro.scope"); }
	CHECK_EQ(Profiler::Global().Timing("macro.scope").count, 1u);
}

// ── Capture (trace) ──────────────────────────────────────────────────────────

#include <diagnostics/trace.hpp>

namespace {
/// Travail CPU mesurable (non optimisé hors du programme).
volatile double g_sink = 0.0;
void Burn(int milliseconds) {
	const uint64_t until = trace::NowNs() + uint64_t(milliseconds) * 1000000ull;
	double x = 1.0;
	while (trace::NowNs() < until)
		for (int i = 0; i < 1000; ++i)
			x = x * 1.0000001 + 0.5;
	g_sink = x;
}

std::string ReadAll(const std::string &path) {
	std::ifstream in(path);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
} // namespace

TEST(Trace, ZonesFramesValuesThreadsAndSamplesAreWritten) {
	CHECK(!trace::Active());
	{
		DIAG_ZONE("hors.capture"); // aucun effet hors capture
	}
	trace::Options options;
	options.samplingHz = 2000;
	options.processHz = 200;
	CHECK(trace::Start(options));
	CHECK(!trace::Start(options)); // déjà en cours
	trace::SetThreadName("principal");
	{
		DIAG_ZONE("test.outer");
		{
			DIAG_ZONE("test.inner");
			Burn(40);
		}
		DIAG_VALUE("test.value", 42);
		DIAG_FRAME();
	}
	std::thread worker([] {
		trace::SetThreadName("ouvrier");
		DIAG_ZONE(trace::Intern("test.dynamique"));
		Burn(30);
	});
	worker.join();
	trace::AsyncBegin("test.async", 7);
	trace::AsyncEnd("test.async", 7);
	DIAG_FRAME();
	CHECK(trace::Stop());
	CHECK(!trace::Stop());
	const std::string path = (std::filesystem::temp_directory_path() / "diag_trace_test.vcap").string();
	CHECK(trace::Save(String(path.c_str())).IsOk());
	const std::string text = ReadAll(path);
	std::filesystem::remove(path);
	for (const char *needle : {"\"diagnostics-capture\"", "\"test.outer\"", "\"test.inner\"", "\"test.dynamique\"", "\"test.value\"",
							   "\"test.async\"", "\"principal\"", "\"ouvrier\"", "\"modules\"", "\"tasks\"", "\"process\""})
		CHECK(text.find(needle) != std::string::npos);
	// Au moins un échantillon de pile (70 ms de calcul à 2 kHz).
	const size_t samples = text.find("\"samples\": [\n[");
	CHECK(samples != std::string::npos);
	{
		DIAG_ZONE("apres.capture"); // de nouveau sans effet
	}
	CHECK(trace::Save(String(path.c_str())).IsOk()); // la capture reste lisible
	CHECK(ReadAll(path).find("apres.capture") == std::string::npos);
	std::filesystem::remove(path);
}
