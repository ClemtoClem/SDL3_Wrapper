// Définitions de profiler/recorder.hpp
#include <profiler/recorder.hpp>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace profiler {

namespace {

using Clock = std::chrono::steady_clock;

String WithoutExtension(const String &path) { return path.EndsWith(".vcap") ? path.Substr(0, path.Length() - 5) : path; }

/// Fichier `<base>.<n>.vcap` le plus récent (captures pilotées par SIGUSR2).
Option<String> NewestCapture(const String &base, std::filesystem::file_time_type after) {
	namespace fs = std::filesystem;
	const fs::path b(base.CStr());
	const fs::path dir = b.has_parent_path() ? b.parent_path() : fs::path(".");
	const String prefix = String(b.filename().string().c_str()) + String(".");
	Option<String> best = NONE;
	fs::file_time_type bestTime{};
	std::error_code ec;
	for (const fs::directory_entry &e : fs::directory_iterator(dir, ec)) {
		const String name(e.path().filename().string().c_str());
		if (!name.StartsWith(prefix) || e.path().extension() != ".vcap")
			continue;
		const auto t = fs::last_write_time(e.path(), ec);
		if (ec || t < after || (best.IsSome() && t <= bestTime))
			continue;
		best = Some(String(e.path().string().c_str()));
		bestTime = t;
	}
	return best;
}

/// Attend un fichier de capture complet (taille stable).
Option<String> WaitForCapture(const String &base, std::filesystem::file_time_type after, double timeoutSeconds) {
	const auto deadline = Clock::now() + std::chrono::duration<double>(timeoutSeconds);
	uintmax_t lastSize = ~uintmax_t(0);
	while (Clock::now() < deadline) {
		if (Option<String> found = NewestCapture(base, after); found.IsSome()) {
			std::error_code ec;
			const uintmax_t size = std::filesystem::file_size(found.Value().CStr(), ec);
			if (!ec && size > 0 && size == lastSize)
				return found;
			lastSize = ec ? ~uintmax_t(0) : size;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
	return NONE;
}

void Sleep(double seconds) { std::this_thread::sleep_for(std::chrono::duration<double>(seconds)); }

} // namespace

Result<String, String> Record(const RecordOptions &o) {
	if (o.command.empty())
		return Err(String("aucune commande à lancer"));
	const bool controlled = o.delaySeconds > 0.0 || o.durationSeconds > 0.0;
	const String absolute(std::filesystem::absolute(o.output.CStr()).string().c_str());
	std::error_code ec;
	std::filesystem::remove(absolute.CStr(), ec);
	const auto launched = std::filesystem::file_time_type::clock::now();
	const pid_t child = fork();
	if (child < 0)
		return Err(String::Format("fork : %s", std::strerror(errno)));
	if (child == 0) {
		setenv("DIAG_TRACE", absolute.CStr(), 1);
		setenv("DIAG_TRACE_HZ", String::Format("%d", o.samplingHz).CStr(), 1);
		if (controlled)
			setenv("DIAG_TRACE_CONTROL", "wait", 1);
		std::vector<char *> argv;
		for (const String &a : o.command)
			argv.push_back(const_cast<char *>(a.CStr()));
		argv.push_back(nullptr);
		execvp(argv[0], argv.data());
		std::fprintf(stderr, "profiler : %s : %s\n", argv[0], std::strerror(errno));
		_exit(127);
	}
	std::printf("profiler : %s lancé (pid %d)\n", o.command[0].CStr(), int(child));
	if (controlled) {
		Sleep(std::max(0.0, o.delaySeconds));
		kill(child, SIGUSR2); // démarre
		std::printf("profiler : capture démarrée pour %.1f s\n", o.durationSeconds);
		Sleep(std::max(0.1, o.durationSeconds));
		kill(child, SIGUSR2); // arrête et écrit <base>.<n>.vcap
		const Option<String> written = WaitForCapture(WithoutExtension(absolute), launched, 30.0);
		if (o.stopAfterCapture) {
			kill(child, SIGTERM);
			int status = 0;
			for (int i = 0; i < 50 && waitpid(child, &status, WNOHANG) == 0; ++i)
				Sleep(0.1);
			if (waitpid(child, &status, WNOHANG) == 0) {
				kill(child, SIGKILL);
				(void)waitpid(child, &status, 0);
			}
		}
		if (written.IsNone())
			return Err(String("aucune capture reçue : le programme appelle-t-il diagnostics::trace::PollControl() à chaque image ?"));
		return Ok(written.Value());
	}
	int status = 0;
	(void)waitpid(child, &status, 0);
	if (!std::filesystem::exists(absolute.CStr()))
		return Err(String::Format("le programme s'est terminé (code %d) sans écrire de capture : est-il instrumenté "
								  "(diagnostics::trace::StartFromEnvironment / FinishFromEnvironment) ?",
								  WIFEXITED(status) ? WEXITSTATUS(status) : -1));
	return Ok(absolute);
}

Result<String, String> Attach(long pid, double durationSeconds) {
	// Chemin de capture : variable DIAG_TRACE de l'environnement de la cible.
	const String environ = String::Format("/proc/%ld/environ", pid);
	FILE *f = std::fopen(environ.CStr(), "rb");
	if (!f)
		return Err(String::Format("processus %ld : %s", pid, std::strerror(errno)));
	String trace;
	bool control = false;
	String entry;
	for (int c = std::fgetc(f);; c = std::fgetc(f)) {
		if (c == '\0' || c == EOF) {
			if (entry.StartsWith("DIAG_TRACE="))
				trace = entry.Substr(11);
			if (entry.StartsWith("DIAG_TRACE_CONTROL="))
				control = entry != "DIAG_TRACE_CONTROL=0";
			entry = String();
			if (c == EOF)
				break;
		} else {
			entry += char(c);
		}
	}
	std::fclose(f);
	if (trace.IsEmpty() || !control)
		return Err(String::Format("processus %ld : lancé sans DIAG_TRACE=… et DIAG_TRACE_CONTROL=1 — il ne peut pas être capturé à chaud", pid));
	const auto before = std::filesystem::file_time_type::clock::now();
	if (kill(pid_t(pid), SIGUSR2) != 0)
		return Err(String::Format("SIGUSR2 : %s", std::strerror(errno)));
	Sleep(std::max(0.1, durationSeconds));
	(void)kill(pid_t(pid), SIGUSR2);
	const Option<String> written = WaitForCapture(WithoutExtension(trace), before, 30.0);
	if (written.IsNone())
		return Err(String("aucune capture reçue"));
	return Ok(written.Value());
}

} // namespace profiler
