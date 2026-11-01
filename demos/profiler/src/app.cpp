// Définitions de profiler/app.hpp (ligne de commande)
#include <profiler/app.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include <profiler/analysis.hpp>
#include <profiler/capture.hpp>
#include <profiler/recorder.hpp>
#include <profiler/settings.hpp>
#include <profiler/symbols.hpp>

namespace profiler {

namespace {

/// Options « --nom valeur » après la commande ; les autres sont positionnelles.
struct Args {
	std::vector<String> positional;
	std::vector<std::pair<String, String>> named;
	std::vector<String> command; ///< après « -- »
	bool Has(const char *name) const {
		for (const auto &[k, v] : named)
			if (k == name)
				return true;
		return false;
	}
	String Get(const char *name, const String &fallback = String()) const {
		for (const auto &[k, v] : named)
			if (k == name)
				return v;
		return fallback;
	}
	double Number(const char *name, double fallback) const {
		const String v = Get(name);
		return v.IsEmpty() ? fallback : std::atof(v.CStr());
	}
};

/// Options sans valeur.
bool IsFlag(const String &name) { return name == "--bottom-up" || name == "--keep-running"; }

Args Parse(const std::vector<String> &raw, size_t from) {
	Args a;
	for (size_t i = from; i < raw.size(); ++i) {
		if (raw[i] == "--") {
			a.command.assign(raw.begin() + long(i) + 1, raw.end());
			break;
		}
		if (raw[i].StartsWith("--")) {
			const int eq = raw[i].IndexOf('=');
			if (eq > 0)
				a.named.push_back({raw[i].Substr(0, size_t(eq)), raw[i].Substr(size_t(eq) + 1)});
			else if (IsFlag(raw[i]) || i + 1 >= raw.size())
				a.named.push_back({raw[i], String()});
			else {
				a.named.push_back({raw[i], raw[i + 1]});
				++i;
			}
		} else {
			a.positional.push_back(raw[i]);
		}
	}
	return a;
}

/// Fil désigné par son nom (ou une partie) ou son numéro ; 0 : tous.
long ThreadOf(const Capture &c, const String &which) {
	if (which.IsEmpty())
		return 0;
	if (const long tid = std::atol(which.CStr()); tid > 0)
		return tid;
	for (long tid : c.ThreadIds())
		if (c.ThreadName(tid).ToLower().Contains(which.ToLower()))
			return tid;
	std::fprintf(stderr, "profiler : fil « %s » introuvable — tous les fils\n", which.CStr());
	return 0;
}

/// Réglages de la commande en cours (cf. Run).
Settings g_settings;

/// Capture désignée, sinon la plus récente du dossier des captures.
String CaptureArg(const Args &a, size_t index = 0) {
	if (a.positional.size() > index)
		return a.positional[index];
	const std::vector<String> captures = g_settings.Captures();
	if (captures.empty())
		return String();
	std::printf("(capture la plus récente : %s)\n", captures.front().CStr());
	return captures.front();
}

AnalysisOptions OptionsOf(const Capture &c, const Args &a) {
	AnalysisOptions o;
	if (a.Has("--from"))
		o.range.begin = int64_t(a.Number("--from", 0) * 1e6);
	if (a.Has("--to"))
		o.range.end = int64_t(a.Number("--to", 0) * 1e6);
	o.targetFps = a.Number("--target-fps", g_settings.targetFps);
	o.thread = ThreadOf(c, a.Get("--thread"));
	return o;
}

Option<Capture> Load(const String &path) {
	Result<Capture, String> c = LoadCapture(path);
	if (c.IsError()) {
		std::fprintf(stderr, "profiler : %s\n", c.Error().CStr());
		return NONE;
	}
	return Some(std::move(c.Value()));
}

int CommandReport(const Args &a) {
	const String path = CaptureArg(a);
	if (path.IsEmpty()) {
		std::fprintf(stderr, "profiler report : capture attendue (aucune dans %s)\n", g_settings.capturesDirectory.CStr());
		return 2;
	}
	Option<Capture> c = Load(path);
	if (c.IsNone())
		return 1;
	Symbolizer symbols(c.Value());
	const Report r = Analyze(c.Value(), symbols, OptionsOf(c.Value(), a));
	std::printf("Capture %s — %s (pid %ld), %.2f s\n\n", path.CStr(), c.Value().executable.CStr(), c.Value().pid,
				double(c.Value().durationNs) / 1e9);
	std::printf("%s", FormatReport(r, size_t(a.Number("--top", double(g_settings.top)))).CStr());
	for (const String &w : symbols.Warnings())
		std::printf("symboles : %s\n", w.CStr());
	return 0;
}

int CommandTree(const Args &a) {
	const String path = CaptureArg(a);
	if (path.IsEmpty()) {
		std::fprintf(stderr, "profiler tree : capture attendue\n");
		return 2;
	}
	Option<Capture> c = Load(path);
	if (c.IsNone())
		return 1;
	Symbolizer symbols(c.Value());
	const CallNode root = BuildCallTree(c.Value(), symbols, OptionsOf(c.Value(), a), a.Has("--bottom-up"));
	std::printf("%s", FormatCallTree(root, symbols, size_t(a.Number("--depth", 14)), a.Number("--min", 1.0) / 100.0).CStr());
	return 0;
}

int CommandCompare(const Args &a) {
	if (a.positional.size() < 2) {
		std::fprintf(stderr, "profiler compare : deux captures attendues\n");
		return 2;
	}
	Option<Capture> before = Load(a.positional[0]), after = Load(a.positional[1]);
	if (before.IsNone() || after.IsNone())
		return 1;
	Symbolizer sb(before.Value()), sa(after.Value());
	const Report rb = Analyze(before.Value(), sb, OptionsOf(before.Value(), a));
	const Report ra = Analyze(after.Value(), sa, OptionsOf(after.Value(), a));
	std::printf("%s", CompareReports(rb, ra).CStr());
	return 0;
}

int CommandExport(const Args &a) {
	const String path = CaptureArg(a);
	if (path.IsEmpty() || a.Get("--chrome").IsEmpty()) {
		std::fprintf(stderr, "profiler export : profiler export [F] --chrome SORTIE.json\n");
		return 2;
	}
	Option<Capture> c = Load(path);
	if (c.IsNone())
		return 1;
	if (Result<bool, String> r = ExportChromeTrace(c.Value(), a.Get("--chrome")); r.IsError()) {
		std::fprintf(stderr, "profiler : %s\n", r.Error().CStr());
		return 1;
	}
	std::printf("%s écrit (chrome://tracing, ui.perfetto.dev)\n", a.Get("--chrome").CStr());
	return 0;
}

int CommandList() {
	const std::vector<String> captures = g_settings.Captures();
	std::printf("%s : %zu capture(s)\n", g_settings.capturesDirectory.CStr(), captures.size());
	for (const String &path : captures) {
		std::error_code ec;
		const uintmax_t size = std::filesystem::file_size(path.CStr(), ec);
		std::printf("  %8.1f Mo  %s\n", ec ? 0.0 : double(size) / 1048576.0, path.CStr());
	}
	return 0;
}

/// Ramène une capture reçue dans le dossier des captures.
String Collect(const String &path, const String &program) {
	const String target = g_settings.NewCapturePath(program);
	std::error_code ec;
	std::filesystem::rename(path.CStr(), target.CStr(), ec);
	if (ec) { // autre système de fichiers : copie
		std::filesystem::copy_file(path.CStr(), target.CStr(), ec);
		if (ec)
			return path;
		std::filesystem::remove(path.CStr(), ec);
	}
	return target;
}

void Remember(const String &path) {
	g_settings.Remember(path);
	(void)g_settings.Save(g_settings.file);
}

} // namespace

const char *Usage() noexcept {
	return "profiler — profileur de programmes instrumentés (diagnostics::trace), inspiré de Google Orbit\n"
		   "\n"
		   "  profiler record [--out F] [--hz N] [--delay S --duration S] [--keep-running] -- programme args…\n"
		   "  profiler list\n"
		   "  profiler attach PID --duration S\n"
		   "  profiler report F [--from MS --to MS] [--thread NOM|TID] [--target-fps N] [--top N]\n"
		   "  profiler tree F [--bottom-up] [--thread NOM|TID] [--depth N] [--min POURCENT]\n"
		   "  profiler compare AVANT.vcap APRES.vcap\n"
		   "  profiler export F --chrome SORTIE.json\n"
		   "  profiler view [F] [--frames N --screenshot P.png]   (aussi : « profiler », « profiler F.vcap »)\n"
		   "\n"
		   "Captures : data/profiler/captures/ (sans fichier, les commandes prennent la plus récente).\n"
		   "Réglages : data/profiler/settings.json (--settings F pour un autre fichier).\n"
		   "\n"
		   "Le programme profilé est instrumenté avec DIAG_ZONE / DIAG_FRAME / DIAG_VALUE\n"
		   "et diagnostics::trace::StartFromEnvironment (cf. libs/foundation/diagnostics/trace.hpp).\n";
}

int Run(const std::vector<String> &args) {
	// Sans argument : le visualiseur (« profiler » équivaut à « profiler view »).
	if (args.empty())
		return Run({String("view")});
	if (args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
		std::printf("%s", Usage());
		return 0;
	}
	// « profiler capture.vcap » ou « profiler --settings F » : le visualiseur aussi.
	if (args[0].StartsWith("--") || args[0].EndsWith(".vcap")) {
		std::vector<String> view{String("view")};
		view.insert(view.end(), args.begin(), args.end());
		return Run(view);
	}
	String command = args[0];
	// Commande par défaut
	if (command.IsEmpty()) {
		command = "command";
	}

	const Args a = Parse(args, 1);
	g_settings = Settings::Load(a.Get("--settings", String(DEFAULT_SETTINGS)));
	g_settings.file = a.Get("--settings", String(DEFAULT_SETTINGS));
	if (command == "record") {
		RecordOptions o;
		o.command = a.command.empty() ? a.positional : a.command;
		if (o.command.empty()) {
			std::fprintf(stderr, "profiler record : commande attendue après « -- »\n");
			return 2;
		}
		o.output = a.Get("--out", g_settings.NewCapturePath(o.command[0]));
		o.samplingHz = int(a.Number("--hz", g_settings.samplingHz));
		o.delaySeconds = a.Number("--delay", g_settings.delaySeconds);
		o.durationSeconds = a.Number("--duration", g_settings.durationSeconds);
		o.stopAfterCapture = !a.Has("--keep-running") && g_settings.stopAfterCapture;
		Result<String, String> written = Record(o);
		if (written.IsError()) {
			std::fprintf(stderr, "profiler : %s\n", written.Error().CStr());
			return 1;
		}
		// Capture pilotée : « <sortie>.1.vcap » ramenée au nom choisi.
		String path = written.Value();
		if (!a.Has("--out") && path != o.output) {
			std::error_code ec;
			std::filesystem::rename(path.CStr(), o.output.CStr(), ec);
			if (!ec)
				path = o.output;
		}
		Remember(path);
		std::printf("capture : %s\n  profiler report %s\n  profiler view %s\n", path.CStr(), path.CStr(), path.CStr());
		return 0;
	}
	if (command == "list")
		return CommandList();
	if (command == "attach") {
		if (a.positional.empty()) {
			std::fprintf(stderr, "profiler attach : numéro de processus attendu\n");
			return 2;
		}
		const long pid = std::atol(a.positional[0].CStr());
		Result<String, String> written = Attach(pid, a.Number("--duration", g_settings.durationSeconds > 0 ? g_settings.durationSeconds : 5));
		if (written.IsError()) {
			std::fprintf(stderr, "profiler : %s\n", written.Error().CStr());
			return 1;
		}
		// Nom du programme : /proc/<pid>/exe.
		std::error_code ec;
		const auto exe = std::filesystem::read_symlink(String::Format("/proc/%ld/exe", pid).CStr(), ec);
		const String path = Collect(written.Value(), ec ? String("processus") : String(exe.filename().string().c_str()));
		Remember(path);
		std::printf("capture : %s\n", path.CStr());
		return 0;
	}
	if (command == "report")
		return CommandReport(a);
	if (command == "tree")
		return CommandTree(a);
	if (command == "compare")
		return CommandCompare(a);
	if (command == "export")
		return CommandExport(a);
	if (command == "view")
		return RunViewer(a.positional.empty() ? String() : a.positional[0], long(a.Number("--frames", 0)), a.Get("--screenshot"),
						 g_settings.file);
	std::fprintf(stderr, "profiler : commande inconnue « %s »\n\n%s", command.CStr(), Usage());
	return 2;
}

} // namespace profiler
