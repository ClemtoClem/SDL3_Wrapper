// Tests du profiler : lecture des captures, symbolisation ELF, analyse,
// arbres d'appels — sur une VRAIE capture écrite par diagnostics::trace.
#include "test.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>

#include <diagnostics/trace.hpp>
#include <profiler/analysis.hpp>
#include <profiler/capture.hpp>
#include <profiler/symbols.hpp>

using namespace profiler;
namespace trace = diagnostics::trace;

namespace {

volatile double g_sink = 0.0;

/// Calcul assez long pour être échantillonné ; non inline, nom stable.
__attribute__((noinline)) void ProfilerTestHotLoop(int milliseconds) {
	const uint64_t until = trace::NowNs() + uint64_t(milliseconds) * uint64_t(1000000);
	double x = 1.0;
	while (trace::NowNs() < until)
		for (int i = 0; i < 2000; ++i)
			x = x * 1.0000001 + 0.25;
	g_sink = x;
}

String TempPath(const char *name) { return String((std::filesystem::temp_directory_path() / name).string().c_str()); }

/// Capture réelle : 10 images, zones imbriquées, un fil ouvrier, valeurs.
Option<Capture> RecordSample() {
	trace::Options options;
	options.samplingHz = 2000;
	options.processHz = 200;
	if (!trace::Start(options))
		return NONE;
	trace::SetThreadName("principal-test");
	for (int frame = 0; frame < 10; ++frame) {
		DIAG_ZONE("frame");
		{
			DIAG_ZONE("update");
			ProfilerTestHotLoop(frame == 7 ? 30 : 6); // l'image 7 : une saccade
		}
		DIAG_VALUE("compteur", frame);
		DIAG_FRAME();
	}
	std::thread worker([] {
		trace::SetThreadName("ouvrier-test");
		DIAG_ZONE("travail");
		ProfilerTestHotLoop(20);
	});
	worker.join();
	DIAG_FRAME();
	if (!trace::Stop())
		return NONE;
	const String path = TempPath("profiler_test.vcap");
	if (trace::Save(path).IsError())
		return NONE;
	Result<Capture, String> loaded = LoadCapture(path);
	std::filesystem::remove(path.CStr());
	if (loaded.IsError())
		return NONE;
	return Some(std::move(loaded.Value()));
}

} // namespace

TEST(Capture, ParsesASyntheticCapture) {
	const char *text = R"({"format": "diagnostics-capture", "version": 1, "pid": 7, "duration_ns": 3000000,
		"threads": [{"tid": 7, "name": "main"}], "modules": [],
		"zones": [[7,0,0,1000000,0],[7,1,100000,400000,1],[7,1,500000,700000,1],[7,0,1000000,3000000,0]],
		"values": [[7,2,10,1.5]], "async": [], "frames": [1000000,2000000,3000000],
		"samples": [[7,50,4096,8192]], "tasks": [[7,0,0,"R"],[7,1000000,900000,"S"]],
		"process": [[0,1048576,0,0,0,0,0,0,0]], "hardware": {"available": false, "reason": "test", "samples": []},
		"names": ["frame","update","valeur"]})";
	Result<Capture, String> c = ParseCapture(StringView(text));
	REQUIRE(c.IsOk());
	CHECK_EQ(c.Value().zones.size(), size_t(4));
	CHECK(c.Value().Name(1) == "update");
	CHECK_EQ(c.Value().samples.size(), size_t(1));
	CHECK_EQ(c.Value().samples[0].depth, uint32_t(2));
	CHECK(c.Value().hardwareReason == "test");
	CHECK(c.Value().ThreadName(7) == "main");
	Symbolizer symbols(c.Value());
	const Report r = Analyze(c.Value(), symbols);
	// Temps propre : 1 ms - (0,3 + 0,2) ms d'« update ».
	for (const ZoneStats &z : r.zones)
		if (z.label == "frame") {
			CHECK_EQ(z.count, uint64_t(2));
			CHECK_EQ(z.selfNs, int64_t(500000 + 2000000));
		}
	CHECK_EQ(r.frames.count, size_t(2));
	CHECK(std::fabs(r.frames.averageMs - 1.0) < 1e-6);
	CHECK(ParseCapture(StringView("{\"format\": \"autre\"}")).IsError());
	CHECK(ParseCapture(StringView("{\"format\": ")).IsError());
}

TEST(Symbols, ReadsTheElfOfThisTestAndDemangles) {
	Result<ElfSymbols, String> elf = ElfSymbols::Load(String("/proc/self/exe"));
	REQUIRE(elf.IsOk());
	CHECK(elf.Value().Count() > 100);
	CHECK(Demangle("_ZN8profiler7AnalyzeERKNS_7CaptureERNS_10SymbolizerERKNS_15AnalysisOptionsE").Contains("profiler::Analyze"));
	CHECK(Demangle("main") == "main");
	CHECK(ElfSymbols::Load(String("/inexistant")).IsError());
}

TEST(Analysis, RealCaptureSymbolizesZonesThreadsAndFindings) {
	const Option<Capture> recorded = RecordSample();
	REQUIRE(recorded.IsSome());
	const Capture &c = recorded.Value();
	CHECK(c.zones.size() >= 21);
	CHECK(c.frames.size() == 11);
	CHECK(!c.samples.empty());
	CHECK(!c.modules.empty());
	Symbolizer symbols(c);
	AnalysisOptions options;
	options.targetFps = 144; // images de 6 ms : sous l'objectif
	const Report r = Analyze(c, symbols, options);
	// Zones : « update » domine le temps propre.
	bool update = false;
	for (const ZoneStats &z : r.zones)
		if (z.label == "update") {
			update = true;
			CHECK_EQ(z.count, uint64_t(10));
			CHECK(z.maxNs > 25000000); // la saccade
			CHECK(z.stddevNs > 0.0);
		}
	CHECK(update);
	// Fils nommés.
	bool principal = false, ouvrier = false;
	for (const ThreadStats &t : r.threads) {
		principal = principal || t.name == "principal-test";
		ouvrier = ouvrier || t.name == "ouvrier-test";
	}
	CHECK(principal);
	CHECK(ouvrier);
	// Les échantillons tombent dans la boucle chaude, retrouvée par son nom.
	bool hot = false;
	for (const FunctionStats &fn : r.functions)
		hot = hot || fn.name.Contains("ProfilerTestHotLoop");
	CHECK(hot);
	// Diagnostic : cadence sous l'objectif, saccade signalée.
	bool fps = false, spikes = false;
	for (const Finding &f : r.findings) {
		fps = fps || (f.category == "Images" && f.title.Contains("objectif"));
		spikes = spikes || f.title.Contains("Saccades");
	}
	CHECK(fps);
	CHECK(spikes);
	CHECK(FormatReport(r).Contains("DIAGNOSTIC"));
	// Arbres : la boucle chaude est une feuille de l'arbre ascendant.
	const CallNode down = BuildCallTree(c, symbols, options, false);
	const CallNode up = BuildCallTree(c, symbols, options, true);
	CHECK_EQ(down.inclusive, up.inclusive);
	REQUIRE(!up.children.empty());
	// Sous les sanitizers, la feuille peut être leur bibliothèque d'exécution
	// (appelée par la boucle) : on vérifie la part INCLUSIVE de la boucle.
	bool inclusive = false;
	for (const FunctionStats &fn : r.functions)
		inclusive = inclusive || (fn.name.Contains("ProfilerTestHotLoop") && fn.inclusiveShare > 0.2);
	CHECK(inclusive);
	CHECK(FormatCallTree(down, symbols).Length() > 0);
	// Plage : seules les 3 premières images.
	AnalysisOptions first = options;
	first.range.end = c.frames[3];
	CHECK(Analyze(c, symbols, first).frames.count <= 3);
	// Comparaison et export.
	CHECK(CompareReports(r, r).Contains("IMAGES"));
	const String chrome = TempPath("profiler_test.json");
	CHECK(ExportChromeTrace(c, chrome).IsOk());
	CHECK(std::filesystem::file_size(chrome.CStr()) > 100);
	std::filesystem::remove(chrome.CStr());
}

#include <profiler/settings.hpp>

TEST(Settings, CreatedOnFirstLaunchRoundTripAndCapturePaths) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "profiler_settings_test";
	std::filesystem::remove_all(dir);
	const String file((dir / "settings.json").string().c_str());
	// Premier lancement : valeurs par défaut, fichier créé.
	Settings s = Settings::Load(file);
	CHECK(std::filesystem::exists(file.CStr()));
	CHECK(s.capturesDirectory == DEFAULT_CAPTURES);
	CHECK_EQ(s.samplingHz, 1000);
	// Aller-retour.
	s.capturesDirectory = String((dir / "captures").string().c_str());
	s.samplingHz = 250;
	s.targetFps = 60;
	s.durationSeconds = 4;
	s.lastCommand = String("prog --option \"x\"");
	for (int i = 0; i < 12; ++i)
		s.Remember(String::Format("c%d.vcap", i));
	CHECK_EQ(s.recentCaptures.size(), size_t(10));
	CHECK(s.recentCaptures.front() == "c11.vcap");
	CHECK(s.Save(file).IsOk());
	const Settings back = Settings::Load(file);
	CHECK(back.capturesDirectory == s.capturesDirectory);
	CHECK_EQ(back.samplingHz, 250);
	CHECK(std::fabs(back.targetFps - 60.0) < 1e-9);
	CHECK(std::fabs(back.durationSeconds - 4.0) < 1e-9);
	CHECK(back.lastCommand == s.lastCommand);
	CHECK_EQ(back.recentCaptures.size(), size_t(10));
	// Chemins de capture : dans le dossier, nom du programme, jamais écrasés.
	const String a = back.NewCapturePath(String("/usr/bin/voxelia"));
	CHECK(a.StartsWith(back.capturesDirectory + String("/voxelia-")));
	CHECK(a.EndsWith(".vcap"));
	{
		FILE *f = std::fopen(a.CStr(), "w");
		REQUIRE(f != nullptr);
		std::fputs("{}", f);
		std::fclose(f);
	}
	const String b = back.NewCapturePath(String("voxelia"));
	CHECK(b != a);
	CHECK_EQ(back.Captures().size(), size_t(1));
	std::filesystem::remove_all(dir);
}
