// cpp-maker.cpp — Moteur de production unifié pour projets C et C++ (C++23)
//
// Compilation de l'outil :
/*   g++ -std=c++23 -O2 -pthread -Wall -Wextra -Wpedantic -Wformat=2 \
		-Wshadow -Wconversion -Wsign-conversion -Wdouble-promotion -Wundef \
		-Wcast-align -Wcast-qual -Woverloaded-virtual -Wnon-virtual-dtor \
		cpp-maker.cpp -o cpp-maker
	 clang++ -std=c++23 -O2 -pthread -Wall -Wextra -Wpedantic -Wformat=2 \
		-Wshadow -Wconversion -Wsign-conversion -Wdouble-promotion -Wundef \
		-Wcast-align -Wcast-qual -Woverloaded-virtual -Wnon-virtual-dtor \
		cpp-maker.cpp -o cpp-maker
*/
// Fichier unique, aucune dépendance externe obligatoire.
//
// ============================================================================
// Moteur de production unifié pour C et C++ — v0.6
// ============================================================================
//
// cpp-maker remplace Makefile, CMake, Meson et Ninja par un unique outil :
// configuration déclarative (TOML), graphe de tâches parallèle et incrémental,
// toolchains natives et croisées, tests, packaging et déploiement intégrés.
// Un seul binaire, un seul fichier de configuration, zéro dépendance externe
// obligatoire (Ninja reste utilisable comme backend d'exécution optionnel).
//
// Projets et modules
//   - projet mono-module (cpp-maker.toml) ou workspace multi-modules
//     ([workspace] modules = [...] + un module.toml par module) ;
//   - types de modules : executable | static | shared | header-only ;
//   - dépendances transitives entre modules : en-têtes publics, defines
//     publics, bibliothèques pkg-config et système propagés aux dépendants,
//     bibliothèques statiques/partagées liées automatiquement (ordre de
//     la fermeture topologique) ; détection des cycles ;
//   - chaque module se compile seul : `build -m <module>`, ou automatiquement
//     depuis son dossier (le module courant est détecté ; --all force tout
//     le workspace) ;
//   - surcharges par plate-forme cible : [platform.linux/windows/macos/...]
//     sources, defines, system, cflags, cxxflags, ldflags ;
//   - squelettes : `new` (mono-module ou --workspace, C ou C++, épinglage du
//     compilateur détecté dans cpp-maker.toml), `add` / `module add`
//     (module.toml + sources immédiatement compilables), `module rename`,
//     `module remove` (TOML réécrit puis VALIDÉ avant remplacement atomique).
//
// Compilation
//   - langages C, C++ et assembleur (.c / .cpp .cc .cxx .c++ / .S .s .sx) ;
//   - graphe de tâches (CC/CXX/AS, AR, LINK/SOLINK) exécuté en parallèle,
//     incrémental par empreinte (commande complète + version de l'outil +
//     dépendances issues des fichiers .d de -MMD) ;
//   - modes : release, debug (défaut), relwithdebinfo, minsize, asan, tsan,
//     ubsan, valgrind — redéfinissables ou étendus (base = "<mode>") ;
//   - arborescence de build : build/<toolchain>/<mode>/{bin,lib,obj} ;
//   - --keep-going, --dry-run, --verbose, --jobs N, --clean ;
//   - génération de build.ninja (--backend ninja ou `generate ninja`) et de
//     compile_commands.json (`generate compdb`).
//   - `size` : contenu détaillé des binaires construits — text/data/bss/dec/hex
//     (sémantique Berkeley de `size`), en-tête ELF (classe, endianness,
//     machine, ABI, type, point d'entrée), archives .a sommées, -v : plus
//     grandes sections ; lecture autonome, sans dépendance à binutils.
//
// Toolchains et compilation croisée
//   - toolchains nommées [toolchains.<nom>] : préfixe de triplet
//     (arm-linux-gnueabihf-...), clang --target, --sysroot, ar/strip/
//     pkg_config dédiés, flags par toolchain ;
//   - -t <nom>[,<nom>…] ou -t all ; tout préfixe de triplet présent dans le
//     PATH fonctionne sans configuration (arm-linux-gnueabihf) ;
//   - `run` / `test` exécutent les binaires croisés via l'émulateur détecté
//     (qemu-user avec -L sysroot, wine) ;
//   - toolchain list | show | which | use | install | remove : détection des
//     compilateurs croisés installés, installation gcc-N ou croisé (apt) ;
//   - doctor : diagnostic de l'environnement (compilateurs, sanitizers,
//     pkg-config, qemu, ninja, git...).
//
// Tests
//   - par module : sources de tests (défaut tests/), exécutables séparés,
//     délai maximal par exécutable, code de sortie 77 = test ignoré ;
//   - `test --filter X`, `check --modes debug,asan,tsan` ;
//   - rapport JUnit (--junit rapport.xml).
//
// Packaging, installation, déploiement
//   - `package` : mise en scène dist/<nom>-<version>-<os>-<arch>/
//     (bin, lib, include, fichiers .pc), strip, formats tar.gz | tar.xz |
//     zip | deb (métadonnées deb_depends, maintainer, license...),
//     checksums SHA256SUMS ;
//   - `install --prefix /usr/local [--destdir D]` ;
//   - `deploy <cible>` : [deploy.<cible>] rsync | scp | copie locale,
//     toolchain/mode/artifact au choix, commandes before/after,
//     ssh_options, --delete ; `deploy --list`.
//
// Configuration
//   - config show | validate | set <clé> <valeur> (validation avant
//     remplacement atomique) ;
//   - minimum_cpp_maker_version : refus poliment si l'outil est trop ancien.
//
// ============================================================================
// v0.6 — ajouté ou modifié par rapport à v0.5
// ============================================================================
//   - couches d'architecture : [layers] order = ["foundation", ...] dans
//     cpp-maker.toml et `layer = "<nom>"` dans le [module] de chaque
//     module.toml. Un module ne peut dépendre que de modules de sa couche
//     ou d'une couche inférieure (vérifié à chaque chargement : build,
//     test, doctor, config validate...). [layers.<nom>] forbid_pkg_config =
//     [...] interdit des paquets pkg-config dans la fermeture transitive
//     des modules d'une couche (ex. aucune dépendance à SDL en Foundation) ;
//     `modules` et `graph` affichent la couche, `graph --dot` regroupe les
//     modules par couche ;
//   - main() détecté automatiquement : à défaut de `main = "..."` et de
//     fichier main.*, la source d'un exécutable qui définit `int main(`
//     est exclue de l'exécutable de tests (v0.5 : seul main.* l'était,
//     d'où des « définitions multiples de main » à l'édition de liens) ;
//   - [tests] env = { NOM = "valeur" } dans cpp-maker.toml : variables
//     d'environnement des exécutables de tests ({root} = racine du projet),
//     ex. LSAN_OPTIONS = "suppressions={root}/tests/lsan.supp" ;
//   - étapes de génération [generate.<nom>] dans cpp-maker.toml : command
//     (sh -c, depuis la racine), inputs et outputs (fichiers ou dossiers).
//     Exécutées avant chaque build si une entrée est plus récente que la
//     dernière exécution réussie ou si une sortie manque (ex. compilation
//     des shaders GLSL → SPIR-V/MSL) ; échec = build en échec ;
//   - gabarit de tests (test.hpp) étendu : TEST(Nom) ou TEST(Suite, Nom),
//     CHECK/CHECK_EQ/CHECK_NE/CHECK_NEAR, REQUIRE/REQUIRE_EQ (arrêtent le
//     test), SKIP("raison") ; les macros EXPECT_*/ASSERT_* de style GTest
//     sont acceptées ; CHECK_EQ ne copie plus ses opérandes (types non
//     copiables) ; tous les tests ignorés → code de sortie 77.
//
// ============================================================================
// v0.5 — ajouté ou modifié par rapport à v0.4
// ============================================================================
//   - `new --type modular` : squelette de projet modulaire (workspace avec
//     un module `core` par défaut dans libs/core et une application dans
//     apps/<nom>) ; équivalent explicite de `new --workspace` ;
//   - `--type normal` remplace `--type executable` (l'alias `executable`
//     reste accepté pour la compatibilité) : c'est le type par défaut d'un
//     projet mono-module exécutable.
//   - workspaces : un module.toml par module ([workspace] modules = [...])
//     remplace les [modules.<nom>] centralisés — les projets v0.3 ET v0.4
//     restent constructibles tels quels (les [modules.<nom>] v0.4 sont
//     convertis à la volée, type "interface" → header-only, "library" →
//     static ; le [deploy] unique v0.4 devient deploy par défaut) ;
//   - modules executable : une cible peut désormais être un module du
//     workspace ; les bibliothèques des dépendances sont liées
//     automatiquement aux dépendants (v0.4 exigeait des ldflags manuels) ;
//   - header-only (ex-"interface") : validation des en-têtes par les
//     modules dépendants, aucun artefact produit ;
//   - assembleur .S/.s reconnu et compilé ;
//   - graphe de tâches unifié (compilation, archive, édition de liens)
//     avec empreinte par tâche incluant la commande ET le contenu des
//     dépendances (.d), reprise -k/--keep-going, affichage [n/N] ;
//   - répertoire de build par toolchain ET par mode :
//     build/<toolchain>/<mode> (v0.4 : build/<mode>) ;
//   - toolchains : clang --target et sysroot, émulateur automatique
//     (qemu-*/wine avec -L <sysroot>) pour run/test croisés, -t multi
//     ou all, sous-commandes toolchain use/install/remove (apt),
//     --backend ninja, --cc/--cxx ad hoc ;
//   - surcharges par plate-forme cible par module ([platform.<os>]) ;
//   - tests par module : timeout par exécutable, code 77 = ignoré,
//     rapport JUnit, `check` multi-modes ;
//   - packaging : tar.xz et deb, fichiers .pc, strip, SHA256SUMS,
//     métadonnées Debian ; install --destdir ;
//   - deploy multi-cibles ([deploy.<nom>]) avec toolchain/mode/artifact,
//     before/after, ssh_options, --delete ; v0.4 n'avait qu'un [deploy] ;
//   - generate ninja | compdb (build.ninja, compile_commands.json) ;
//   - size : nouvelle commande — inspection des binaires construits
//     (text/data/bss/dec/hex, en-tête ELF, archives .a), sans binutils ;
//   - config set + validate ; doctor ; info refondu ;
//   - new --workspace (squelette libs/<lib> + apps/<app>), épinglage du
//     compilateur détecté, --c-standard ; add/module add (nouveau module
//     compilable immédiatement) ; --no-scaffold ;
//   - identifiants C/C++ systématiquement sanitarisés pour les
//     squelettes (cpp-maker new myproj-sdl3 → namespace myproj_sdl3) ;
//   - graph --dot (v0.4 : --mermaid) ;
//   - lanceur de processus posix_spawn sans shell (groupe de processus
//     dédié pour les délais), fichiers écrits de façon atomique partout.

#include <algorithm>
#include <regex>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#ifdef __unix__
#  include <fcntl.h>
#  include <poll.h>
#  include <signal.h>
#  include <spawn.h>
#  include <sys/utsname.h>
#  include <sys/wait.h>
#  include <sys/file.h>
#  include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

// -Wnull-dereference produit des faux positifs dans les en-têtes de la
// libstdc++ (egptr()/gptr() de <streambuf> via istreambuf_iterator,
// std::vector::size() sur conteneur vide). Aucun des avertissements
// rapportés ne concerne du code de cpp-maker : on désactive cette option
// (GCC uniquement) le temps de la compilation, sans toucher aux autres.
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

static constexpr std::string_view CPP_MAKER_VERSION = "0.6.0";

// ============================================================================
// util
// ============================================================================
namespace util {

// Couleurs : activées uniquement sur terminal interactif, désactivables
// via NO_COLOR=1. Toutes les sorties passent par out/outln/errln, qui
// retirent les séquences ANSI lorsque les couleurs sont coupées.
inline bool color() {
#ifdef __unix__
	static bool c = isatty(STDOUT_FILENO) && std::getenv("NO_COLOR") == nullptr;
	return c;
#else
	return false;
#endif
}

// Retire les séquences ANSI "\033[…m" d'une chaîne.
inline std::string strip_ansi(std::string_view s) {
	std::string r;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
			size_t j = i + 2;
			while (j < s.size() && s[j] != 'm') ++j;
			i = j; // 'm' consommé (ou fin de chaîne) par le ++i du for
		} else {
			r += s[i];
		}
	}
	return r;
}

template <class... Args>
void out(std::format_string<Args...> f, Args&&... a) {
	std::string s = std::format(f, std::forward<Args>(a)...);
	std::cout << (color() ? s : strip_ansi(s));
}
template <class... Args>
void outln(std::format_string<Args...> f, Args&&... a) {
	std::string s = std::format(f, std::forward<Args>(a)...);
	std::cout << (color() ? s : strip_ansi(s)) << '\n';
}
template <class... Args>
void errln(std::format_string<Args...> f, Args&&... a) {
	std::string s = std::format(f, std::forward<Args>(a)...);
	std::cout.flush();
	std::cerr << (color() ? s : strip_ansi(s)) << '\n';
}
template <class... Args>
void warn(std::format_string<Args...> f, Args&&... a) {
	errln("\033[1;33mwarning:\033[0m {}", std::format(f, std::forward<Args>(a)...));
}

[[noreturn]] inline void die(std::string_view m) {
	errln("\033[1;31merror:\033[0m {}", m);
	std::exit(1);
}

// FNV-1a 64 bits
struct Hasher {
	uint64_t h = 1469598103934665603ULL;
	void feed(std::string_view s) {
		for (char ch : s) {
			h ^= static_cast<unsigned char>(ch);
			h *= 1099511628211ULL;
		}
		h ^= 0xff; h *= 1099511628211ULL;
	}
	void feed_u64(uint64_t v) {
		for (int i = 0; i < 8; ++i) { h ^= (v & 0xff); h *= 1099511628211ULL; v >>= 8; }
	}
	std::string hex() const { return std::format("{:016x}", h); }
};

inline std::string join(const std::vector<std::string>& v, std::string_view sep) {
	std::string r;
	for (size_t i = 0; i < v.size(); ++i) { if (i) r += sep; r += v[i]; }
	return r;
}

inline std::string trim(std::string_view s) {
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string_view::npos) return {};
	size_t e = s.find_last_not_of(" \t\r\n");
	return std::string(s.substr(b, e - b + 1));
}

// Découpe sur les blancs (sortie de pkg-config, etc.).
inline std::vector<std::string> split_ws(std::string_view s) {
	std::vector<std::string> r;
	std::string cur;
	for (char c : s) {
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			if (!cur.empty()) { r.push_back(cur); cur.clear(); }
		} else {
			cur += c;
		}
	}
	if (!cur.empty()) r.push_back(cur);
	return r;
}

// Découpe "a,b , c" → {"a","b","c"} (éléments vides ignorés).
inline std::vector<std::string> split_list(std::string_view s, char sep = ',') {
	std::vector<std::string> r;
	size_t start = 0;
	while (start <= s.size()) {
		size_t end = s.find(sep, start);
		if (end == std::string_view::npos) end = s.size();
		auto t = trim(s.substr(start, end - start));
		if (!t.empty()) r.push_back(t);
		start = end + 1;
	}
	return r;
}

inline std::string replace_all(std::string s, std::string_view from, std::string_view to) {
	if (from.empty()) return s;
	size_t pos = 0;
	while ((pos = s.find(from, pos)) != std::string::npos) {
		s.replace(pos, from.size(), to);
		pos += to.size();
	}
	return s;
}

inline std::string lower(std::string s) {
	for (auto& c : s) c = (char)std::tolower((unsigned char)c);
	return s;
}

// Nom utilisable comme composant de chemin : [A-Za-z0-9_.+-], non vide,
// sans "." ni "..".
inline bool is_safe_name(std::string_view s) {
	if (s.empty() || s == "." || s == "..") return false;
	return std::all_of(s.begin(), s.end(), [](unsigned char c) {
		return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '+';
	});
}

// Recherche d'un exécutable dans le PATH (sans lancer `which`).
inline std::optional<fs::path> find_in_path(const std::string& name) {
	if (name.empty()) return std::nullopt;
	auto executable = [](const fs::path& p) {
		std::error_code ec;
		if (!fs::is_regular_file(p, ec)) return false;
#ifdef __unix__
		return ::access(p.c_str(), X_OK) == 0;
#else
		return true;
#endif
	};
	if (name.find('/') != std::string::npos) {
		if (executable(name)) return fs::path(name);
		return std::nullopt;
	}
	const char* env = std::getenv("PATH");
	std::string path = env ? env : "/usr/local/bin:/usr/bin:/bin";
	for (auto& dir : split_list(path, ':')) {
		fs::path cand = fs::path(dir) / name;
		if (executable(cand)) return cand;
	}
	return std::nullopt;
}
inline bool have(const std::string& name) { return find_in_path(name).has_value(); }

// Échappement POSIX sh (pour build.ninja et l'affichage des commandes).
inline std::string shell_quote(const std::string& a) {
	if (a.empty()) return "''";
	bool plain = std::all_of(a.begin(), a.end(), [](unsigned char c) {
		return std::isalnum(c) || std::strchr("_-+=/.,:@%^", c) != nullptr;
	});
	if (plain) return a;
	return "'" + replace_all(a, "'", "'\\''") + "'";
}
inline std::string shell_join(const std::vector<std::string>& v) {
	std::string r;
	for (size_t i = 0; i < v.size(); ++i) { if (i) r += ' '; r += shell_quote(v[i]); }
	return r;
}

inline std::string json_escape(std::string_view s) {
	std::string r;
	for (char ch : s) {
		auto c = static_cast<unsigned char>(ch);
		switch (c) {
			case '"':  r += "\\\""; break;
			case '\\': r += "\\\\"; break;
			case '\n': r += "\\n"; break;
			case '\r': r += "\\r"; break;
			case '\t': r += "\\t"; break;
			default:
				if (c < 0x20) r += std::format("\\u{:04x}", c);
				else r += (char)c;
		}
	}
	return r;
}

inline std::string xml_escape(std::string_view s) {
	std::string r;
	for (char ch : s) {
		auto c = static_cast<unsigned char>(ch);
		switch (c) {
			case '<': r += "&lt;"; break;
			case '>': r += "&gt;"; break;
			case '&': r += "&amp;"; break;
			case '"': r += "&quot;"; break;
			default:
				if (c < 0x20 && c != '\n' && c != '\t' && c != '\r') r += '?';
				else r += (char)c;
		}
	}
	return r;
}

inline bool read_file(const fs::path& p, std::string& out) {
	std::ifstream f(p, std::ios::binary);
	if (!f) return false;
	out.assign((std::istreambuf_iterator<char>(f)), {});
	return true;
}

inline bool write_file_atomic(const fs::path& p, const std::string& content) {
	fs::path tmp = p;
	tmp += ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f) return false;
		f.write(content.data(), (std::streamsize)content.size());
		if (!f) return false;
	}
	std::error_code ec;
	fs::rename(tmp, p, ec);
	if (ec) { fs::remove(tmp, ec); return false; }
	return true;
}

// Écrit un fichier (répertoires parents créés) ; die() en cas d'échec.
inline void write_file(const fs::path& p, const std::string& content) {
	std::error_code ec;
	if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
	std::ofstream f(p, std::ios::binary | std::ios::trunc);
	if (!f) die("impossible d'écrire " + p.string());
	f << content;
	if (!f) die("échec d'écriture de " + p.string());
}

// Joint un chemin relatif à un répertoire de module ("." ou "" = racine).
inline fs::path under(const fs::path& dir, const fs::path& p) {
	if (p.is_absolute()) return p.lexically_normal();
	if (dir.empty() || dir == ".") return p.lexically_normal();
	return (dir / p).lexically_normal();
}

inline std::string elapsed(std::chrono::steady_clock::time_point t0) {
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - t0).count();
	return std::format("{}.{:02}s", ms / 1000, (ms % 1000) / 10);
}

// ---------------------------------------------------------------------------
// Verrou inter-processus (flock) : empêche deux cpp-maker de modifier
// simultanément le même projet. Le fichier .cpp-maker.lock est placé à la
// racine (jamais touché par `clean`) ; flock est libéré automatiquement par
// le noyau si le processus meurt. Best-effort : si le fichier ne peut pas
// être ouvert (FS en lecture seule), on continue sans verrou.
// ---------------------------------------------------------------------------
#ifdef __unix__
class FileLock {
	int fd_ = -1;
public:
	FileLock() = default;
	FileLock(const FileLock&) = delete;
	FileLock& operator=(const FileLock&) = delete;
	FileLock(FileLock&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
	FileLock& operator=(FileLock&& o) noexcept {
		if (this != &o) { release(); fd_ = o.fd_; o.fd_ = -1; }
		return *this;
	}
	~FileLock() { release(); }

	void release() {
		if (fd_ >= 0) { ::flock(fd_, LOCK_UN); ::close(fd_); fd_ = -1; }
	}
	bool held() const { return fd_ >= 0; }

	// Acquiert un verrou exclusif. En cas de contention, affiche
	// `waiting_message` (avec le PID du détenteur si lisible), puis attend.
	// Retourne false uniquement sur erreur d'E/S (le build continue alors
	// sans verrou : c'est du best-effort).
	bool acquire(const fs::path& p, std::string_view waiting_message = {}) {
		release();
		std::error_code ec;
		if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
		// O_CLOEXEC : les sous-processus (gcc, ninja, rsync…) n'héritent pas
		// du verrou.
		int fd = ::open(p.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
		if (fd < 0) return false;
		if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
			if (errno != EWOULDBLOCK) { ::close(fd); return false; }
			std::string holder;
			char buf[32] = {};
			ssize_t n = ::pread(fd, buf, sizeof buf - 1, 0);
			if (n > 0) {
				int pid = std::atoi(buf);
				if (pid > 0) holder = std::format(" (PID {})", pid);
			}
			if (!waiting_message.empty())
				outln("\033[1;33m{}\033[0m{}", waiting_message, holder);
			if (::flock(fd, LOCK_EX) != 0) { ::close(fd); return false; }
			outln("\033[1;32mVerrou obtenu, poursuite.\033[0m");
		}
		(void)!::ftruncate(fd, 0);
		auto s = std::format("{}\n", (int)::getpid());
		(void)!::write(fd, s.data(), s.size());
		fd_ = fd;
		return true;
	}
};
#else
class FileLock {  // pas de verrou inter-processus hors POSIX
public:
	FileLock() = default;
	FileLock(const FileLock&) = delete;
	FileLock& operator=(const FileLock&) = delete;
	bool acquire(const fs::path&, std::string_view = {}) { return true; }
	void release() {}
	bool held() const { return false; }
};
#endif

} // namespace util

// ============================================================================
// proc — lancement sécurisé de processus externes (sans shell)
// ============================================================================
namespace proc {

struct Result {
	int exit_code = 0;
	bool signaled = false;
	int signal = 0;
	bool timed_out = false;
	std::string output;
};

struct Options {
	bool capture = false;             // stdout+stderr capturés dans output
	std::optional<fs::path> cwd;      // répertoire de travail du fils
	int timeout_sec = 0;              // 0 = illimité
	std::vector<std::pair<std::string, std::string>> env{}; // ajouté à l'environnement hérité
};

#ifdef __unix__
inline Result run(const std::vector<std::string>& args, const Options& o = {}) {
	Result r;
	if (args.empty()) { r.exit_code = 127; r.output = "empty command"; return r; }
	// Le fils écrit directement sur les descripteurs : vider nos tampons d'abord.
	if (!o.capture) { std::cout.flush(); std::cerr.flush(); }

	std::vector<char*> argv;
	argv.reserve(args.size() + 1);
	for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
	argv.push_back(nullptr);

	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	auto cleanup = [&] {
		posix_spawn_file_actions_destroy(&fa);
		posix_spawnattr_destroy(&attr);
	};

	if (o.timeout_sec > 0) {
		// Groupe de processus dédié : le délai tue aussi les petits-enfants.
		posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
		posix_spawnattr_setpgroup(&attr, 0);
	}
	if (o.cwd) {
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
		posix_spawn_file_actions_addchdir_np(&fa, o.cwd->c_str());
#else
		cleanup();
		r.exit_code = 127; r.output = "changement de répertoire non supporté"; return r;
#endif
	}

	int pfd[2] = {-1, -1};
	if (o.capture) {
		if (pipe(pfd) != 0) {
			cleanup();
			r.exit_code = 127; r.output = "pipe failed"; return r;
		}
		posix_spawn_file_actions_addclose(&fa, pfd[0]);
		posix_spawn_file_actions_adddup2(&fa, pfd[1], STDOUT_FILENO);
		posix_spawn_file_actions_adddup2(&fa, pfd[1], STDERR_FILENO);
		posix_spawn_file_actions_addclose(&fa, pfd[1]);
	}

	// Environnement : celui du parent, surchargé par o.env.
	std::vector<std::string> env_storage;
	std::vector<char*> envp;
	if (!o.env.empty()) {
		for (char** e = environ; *e; ++e) {
			std::string_view kv(*e);
			auto eq = kv.find('=');
			std::string_view key = kv.substr(0, eq);
			bool overridden = std::any_of(o.env.begin(), o.env.end(),
										  [&](auto& p) { return p.first == key; });
			if (!overridden) env_storage.emplace_back(kv);
		}
		for (auto& [k, v] : o.env) env_storage.push_back(k + "=" + v);
		for (auto& s : env_storage) envp.push_back(s.data());
		envp.push_back(nullptr);
	}

	pid_t pid = -1;
	int rc = posix_spawnp(&pid, args[0].c_str(), &fa, &attr, argv.data(),
						  o.env.empty() ? environ : envp.data());
	cleanup();
	if (o.capture) close(pfd[1]);

	if (rc != 0) {
		if (pfd[0] >= 0) close(pfd[0]);
		r.exit_code = 127;
		r.output = std::format("spawn '{}' failed: {}", args[0], std::strerror(rc));
		return r;
	}

	using clock = std::chrono::steady_clock;
	auto deadline = clock::now() + std::chrono::seconds(o.timeout_sec);
	bool eof = !o.capture;
	bool exited = false;
	int status = 0;
	char buf[8192];

	auto read_some = [&](int wait_ms) {
		pollfd p{pfd[0], POLLIN, 0};
		int pr = poll(&p, 1, wait_ms);
		if (pr <= 0) return false;
		ssize_t n = ::read(pfd[0], buf, sizeof buf);
		if (n > 0) { r.output.append(buf, (size_t)n); return true; }
		if (n == 0 || (errno != EINTR && errno != EAGAIN)) eof = true;
		return false;
	};

	while (!(exited && eof)) {
		if (!eof && !exited) read_some(50);
		if (!exited) {
			bool block = eof && o.timeout_sec <= 0;
			pid_t w = waitpid(pid, &status, block ? 0 : WNOHANG);
			if (w == pid) exited = true;
			else if (w < 0 && errno != EINTR) { exited = true; status = 0; }
		}
		if (exited && !eof) {
			// Vidange : un petit-enfant peut garder le tube ouvert.
			while (!eof && read_some(0)) {}
			eof = true;
		}
		if (!exited && o.timeout_sec > 0 && clock::now() > deadline) {
			kill(-pid, SIGKILL);
			kill(pid, SIGKILL);
			while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
			r.timed_out = true;
			exited = true;
		}
		if (!exited && eof && o.timeout_sec > 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	if (pfd[0] >= 0) close(pfd[0]);

	if (WIFEXITED(status)) r.exit_code = WEXITSTATUS(status);
	else if (WIFSIGNALED(status)) {
		r.signaled = true; r.signal = WTERMSIG(status);
		r.exit_code = 128 + r.signal;
	}
	if (r.timed_out) r.exit_code = 124;
	return r;
}
#else
inline Result run(const std::vector<std::string>& args, const Options& o = {}) {
	Result r;
	if (args.empty()) { r.exit_code = 127; return r; }
	// Repli non-POSIX : échappement minimal, sans délai ni cwd.
	std::string cmd;
	for (size_t i = 0; i < args.size(); ++i) {
		if (i) cmd += ' ';
		cmd += '"' + args[i] + '"';
	}
	if (o.capture) cmd += " 2>&1";
	FILE* p = _popen(cmd.c_str(), "r");
	if (!p) { r.exit_code = 127; return r; }
	char buf[4096];
	while (fgets(buf, sizeof buf, p)) { if (o.capture) r.output += buf; else std::fputs(buf, stdout); }
	r.exit_code = _pclose(p);
	return r;
}
#endif

inline Result capture(const std::vector<std::string>& args) {
	Options o; o.capture = true;
	return run(args, o);
}

} // namespace proc

// ============================================================================
// toml — parseur du sous-ensemble utilisé par cpp-maker.toml / module.toml
// Support : tables [a.b], clés nues, "…" ou pointées (a.b = …), chaînes
//           "…"/'…', entiers, booléens, tableaux [v1, v2, …] multi-lignes,
//           tables inline { k = v, … }.
// ============================================================================
namespace toml {

struct Value;
using Array = std::vector<Value>;
using Table = std::map<std::string, Value>;

struct Value {
	std::variant<std::monostate, std::string, int64_t, bool, Array, Table> v;

	const Table* table() const { return std::get_if<Table>(&v); }
	Table* table() { return std::get_if<Table>(&v); }
	const Array* array() const { return std::get_if<Array>(&v); }
	const std::string* str() const { return std::get_if<std::string>(&v); }
	std::optional<int64_t> integer() const {
		if (auto p = std::get_if<int64_t>(&v)) return *p;
		return std::nullopt;
	}
	std::optional<bool> boolean() const {
		if (auto p = std::get_if<bool>(&v)) return *p;
		return std::nullopt;
	}
};

struct ParseError : std::runtime_error {
	ParseError(std::string m) : std::runtime_error(std::move(m)) {}
};

class Parser {
public:
	Parser(std::string text, std::string file)
		: text_(std::move(text)), file_(std::move(file)) {}

	Table parse() {
		Table root;
		Table* cur = &root;
		std::set<std::string> defined_tables;
		while (!eof()) {
			skip_ws_and_comments();
			if (eof()) break;
			if (peek() == '[') {
				next(); // [
				if (peek() == '[') fail("les tableaux de tables [[...]] ne sont pas supportés");
				skip_ws();
				auto path = parse_key_path();
				if (next() != ']') fail("']' attendu après le nom de table");
				std::string full = util::join(path, ".");
				if (!defined_tables.insert(full).second)
					fail(std::format("table [{}] définie deux fois", full));
				cur = &ensure_table(root, path);
				expect_eol();
			} else {
				auto path = parse_key_path();
				std::string key = path.back();
				path.pop_back();
				Table& dst = path.empty() ? *cur : ensure_table(*cur, path);
				skip_ws();
				if (next() != '=') fail("'=' attendu");
				skip_ws();
				Value v = parse_value();
				if (dst.count(key)) fail(std::format("clé '{}' déjà définie", key));
				dst[key] = std::move(v);
				expect_eol();
			}
		}
		return root;
	}

private:
	std::string text_;
	std::string file_;
	size_t i_ = 0;
	int line_ = 1;

	[[noreturn]] void fail(const std::string& m) {
		throw ParseError(std::format("{}:{}: {}", file_, line_, m));
	}
	bool eof() const { return i_ >= text_.size(); }
	char peek() const { return eof() ? '\0' : text_[i_]; }
	char next() {
		char c = peek();
		if (c == '\n') ++line_;
		if (!eof()) ++i_;
		return c;
	}
	void skip_ws() {
		while (!eof() && (peek() == ' ' || peek() == '\t')) next();
	}
	void skip_ws_and_comments() {
		for (;;) {
			char c = peek();
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { next(); continue; }
			if (c == '#') { while (!eof() && peek() != '\n') next(); continue; }
			break;
		}
	}
	void expect_eol() {
		skip_ws();
		if (eof()) return;
		if (peek() == '#') { while (!eof() && peek() != '\n') next(); }
		if (eof()) return;
		if (peek() != '\n' && peek() != '\r') fail("fin de ligne attendue");
		if (peek() == '\r') next();
		if (peek() == '\n') next();
	}
	bool is_bare_key_char(char c) const {
		return std::isalnum((unsigned char)c) || c == '_' || c == '-';
	}
	std::string parse_key() {
		skip_ws();
		if (peek() == '"' || peek() == '\'') return parse_string();
		std::string k;
		while (!eof() && is_bare_key_char(peek())) k += next();
		if (k.empty()) fail("clé attendue");
		return k;
	}
	std::vector<std::string> parse_key_path() {
		std::vector<std::string> path;
		path.push_back(parse_key());
		skip_ws();
		while (peek() == '.') {
			next(); skip_ws();
			path.push_back(parse_key());
			skip_ws();
		}
		return path;
	}
	Table& ensure_table(Table& root, const std::vector<std::string>& path) {
		Table* cur = &root;
		for (auto& p : path) {
			auto& slot = (*cur)[p];
			if (std::holds_alternative<std::monostate>(slot.v)) slot.v = Table{};
			if (!slot.table()) fail(std::format("'{}' n'est pas une table", p));
			cur = slot.table();
		}
		return *cur;
	}
	std::string parse_string() {
		char q = next();
		std::string s;
		while (!eof()) {
			char c = next();
			if (c == q) return s;
			if (c == '\n') fail("chaîne non terminée en fin de ligne");
			if (q == '"' && c == '\\') {
				char e = next();
				switch (e) {
					case 'n': s += '\n'; break;
					case 't': s += '\t'; break;
					case 'r': s += '\r'; break;
					case '"': s += '"'; break;
					case '\\': s += '\\'; break;
					default: s += e;
				}
			} else {
				s += c;
			}
		}
		fail("chaîne non terminée");
	}
	Value parse_value() {
		char c = peek();
		if (c == '"' || c == '\'') return Value{parse_string()};
		if (c == '[') return Value{parse_array()};
		if (c == '{') return Value{parse_inline_table()};
		if (c == 't' || c == 'f') return Value{parse_bool()};
		if (c == '-' || c == '+' || std::isdigit((unsigned char)c)) return Value{parse_int()};
		fail(std::format("valeur invalide (caractère '{}')", c));
	}
	Array parse_array() {
		next(); // [
		Array a;
		for (;;) {
			skip_ws_and_comments();
			if (peek() == ']') { next(); return a; }
			a.push_back(parse_value());
			skip_ws_and_comments();
			if (peek() == ',') { next(); continue; }
			if (peek() == ']') { next(); return a; }
			fail("',' ou ']' attendu dans un tableau");
		}
	}
	Table parse_inline_table() {
		next(); // {
		Table t;
		skip_ws();
		if (peek() == '}') { next(); return t; }
		for (;;) {
			skip_ws();
			auto path = parse_key_path();
			std::string key = path.back();
			path.pop_back();
			Table& dst = path.empty() ? t : ensure_table(t, path);
			skip_ws();
			if (next() != '=') fail("'=' attendu dans une table inline");
			skip_ws();
			if (dst.count(key)) fail(std::format("clé '{}' déjà définie", key));
			dst[key] = parse_value();
			skip_ws();
			char c = next();
			if (c == ',') continue;
			if (c == '}') return t;
			fail("',' ou '}' attendu dans une table inline");
		}
	}
	bool parse_bool() {
		if (text_.compare(i_, 4, "true") == 0) { i_ += 4; return true; }
		if (text_.compare(i_, 5, "false") == 0) { i_ += 5; return false; }
		fail("booléen attendu");
	}
	int64_t parse_int() {
		bool neg = false;
		if (peek() == '-' || peek() == '+') { neg = peek() == '-'; next(); }
		int64_t v = 0;
		bool any = false;
		while (!eof() && (std::isdigit((unsigned char)peek()) || peek() == '_')) {
			char c = next();
			if (c == '_') continue;
			v = v * 10 + (c - '0');
			any = true;
		}
		if (!any) fail("entier attendu");
		return neg ? -v : v;
	}
};

inline Table parse_file(const fs::path& p) {
	std::string content;
	if (!util::read_file(p, content))
		throw ParseError(std::format("{}: impossible d'ouvrir le fichier", p.string()));
	Parser parser(std::move(content), p.string());
	return parser.parse();
}

// --- accès typés : lèvent ParseError si le type ne correspond pas ----------
inline const Value* find(const Table& t, const std::string& k) {
	auto it = t.find(k);
	return it == t.end() ? nullptr : &it->second;
}
inline const Table* get_table(const Table& t, const std::string& k) {
	auto v = find(t, k);
	if (!v) return nullptr;
	if (!v->table()) throw ParseError(std::format("'{}' doit être une table", k));
	return v->table();
}
inline std::optional<std::string> get_str(const Table& t, const std::string& k) {
	auto v = find(t, k);
	if (!v) return std::nullopt;
	if (!v->str()) throw ParseError(std::format("'{}' doit être une chaîne", k));
	return *v->str();
}
// Tableau de chaînes ; une chaîne seule est acceptée comme tableau à 1 élément.
inline std::optional<std::vector<std::string>> get_strs(const Table& t, const std::string& k) {
	auto v = find(t, k);
	if (!v) return std::nullopt;
	if (auto s = v->str()) return std::vector<std::string>{*s};
	auto a = v->array();
	if (!a) throw ParseError(std::format("'{}' doit être un tableau de chaînes", k));
	std::vector<std::string> r;
	for (auto& x : *a) {
		if (!x.str()) throw ParseError(std::format("'{}' doit être un tableau de chaînes", k));
		r.push_back(*x.str());
	}
	return r;
}
inline std::optional<int64_t> get_int(const Table& t, const std::string& k) {
	auto v = find(t, k);
	if (!v) return std::nullopt;
	if (!v->integer()) throw ParseError(std::format("'{}' doit être un entier", k));
	return *v->integer();
}
inline std::optional<bool> get_bool(const Table& t, const std::string& k) {
	auto v = find(t, k);
	if (!v) return std::nullopt;
	if (!v->boolean()) throw ParseError(std::format("'{}' doit être un booléen", k));
	return *v->boolean();
}

template <class T>
inline void set_if(T& dst, const std::optional<T>& v) { if (v) dst = *v; }

} // namespace toml

// ============================================================================
// config — modèle de cpp-maker.toml (projet / workspace) et module.toml
// ============================================================================
struct ModeConfig {
	std::string name;
	std::vector<std::string> flags;
	std::vector<std::string> link_flags;
	std::string sanitizer;   // "" | "address" | "thread" | "undefined"
	std::string runner;      // "" | "valgrind"
	std::vector<std::string> runner_args;
	std::vector<std::string> defines;
};

// Profil de toolchain : [toolchains.<nom>]
struct ToolchainConfig {
	std::string name;
	std::string prefix;              // "arm-linux-gnueabihf-" ⇒ gcc, g++, ar, strip…
	std::string cc = "auto";
	std::string cxx = "auto";
	std::string ar, strip, pkg_config;
	std::string family = "auto";     // auto | gcc | clang (choix automatique)
	std::string target;              // clang --target=<triplet>
	std::string sysroot;             // --sysroot=<dir>
	std::string os;                  // "" = déduit du triplet
	std::vector<std::string> cflags, cxxflags, ldflags;
	std::vector<std::string> runner; // émulateur pour run/test
	bool runner_set = false;         // runner = [] désactive l'auto-détection
	bool from_config = false;
};

struct PlatformOverride {
	std::vector<std::string> sources, defines, system_libs, cflags, cxxflags, ldflags;
};

struct ModuleConfig {
	std::string name;
	std::string type = "executable"; // executable | static | shared | header-only
	std::string version;             // "" = version du projet
	fs::path dir;                    // relatif à la racine ("." = racine)
	fs::path file;                   // module.toml (ou cpp-maker.toml)

	std::vector<std::string> source_dirs = {"src"};
	std::vector<std::string> source_files;
	std::vector<std::string> exclude;
	std::string main;                // fichier contenant main() (exclu des tests)
	std::string layer;               // couche d'architecture ([layers] order), "" = aucune

	std::vector<std::string> public_includes = {"include"};
	std::vector<std::string> private_includes; // défaut : source_dirs

	std::vector<std::string> deps;         // autres modules
	std::vector<std::string> pkg_config;   // paquets pkg-config
	std::vector<std::string> system_libs;  // -l<x>

	std::vector<std::string> defines, public_defines;
	std::vector<std::string> cflags, cxxflags, ldflags;

	std::vector<std::string> test_dirs = {"tests"};
	std::string c_standard, cxx_standard;  // "" = hérité du projet
	std::map<std::string, PlatformOverride> platforms;
	bool install = true;

	bool is_library() const { return type == "static" || type == "shared" || type == "header-only"; }
};

struct PackageConfig {
	std::vector<std::string> formats = {"tar.gz"};
	bool strip = true;
	std::string maintainer = "unknown <unknown@example.com>";
	std::string description;
	std::string homepage;
	std::string license;
	std::vector<std::string> deb_depends;
	std::string deb_prefix = "/usr";
	std::vector<std::string> extra_files;  // copiés dans share/<nom>/
	bool checksums = true;
};

struct DeployConfig {
	std::string name;
	std::string method = "rsync";     // rsync | scp | copy
	std::string host, user, path;
	int port = 0;
	std::string toolchain;            // "" = toolchain par défaut
	std::string mode = "release";
	std::string artifact = "stage";   // stage | tar.gz | tar.xz | zip | deb
	std::vector<std::string> modules; // vide = tous
	std::vector<std::string> ssh_options;
	std::vector<std::string> before;  // commandes locales (sh -c) avant transfert
	std::vector<std::string> after;   // commandes sur la cible après transfert
	std::vector<std::string> after_local; // commandes locales (sh -c) après transfert
	std::string script;               // method = script : script shell local (v0.4)
	bool delete_extra = false;        // rsync --delete
};

struct Project {
	fs::path root;                    // absolu
	fs::path file;                    // cpp-maker.toml
	bool has_file = false;
	std::string name = "project";
	std::string version = "0.1.0";
	std::string description;
	std::string cxx_standard = "c++23";
	std::string c_standard = "c17";
	std::string min_version;

	std::string default_mode = "debug";
	std::string default_toolchain = "host";
	int jobs = 0;
	int test_timeout = 300;
	std::vector<std::pair<std::string, std::string>> test_env; // [tests] env
	std::vector<std::string> layer_order;                       // [layers] order (bas → haut)
	struct GenerateStep { std::string name, command; std::vector<std::string> inputs, outputs; };
	std::vector<GenerateStep> generate_steps;                   // [generate.<nom>]
	std::map<std::string, std::vector<std::string>> layer_forbid_pkg; // [layers.<n>] forbid_pkg_config
	bool static_pic = true;
	std::vector<std::string> warnings = {
		"-Wall",
		"-Wextra",
		"-Wpedantic",
		"-Wformat=2",
		"-Wshadow",
		"-Wconversion",
		"-Wsign-conversion",
		"-Wdouble-promotion",
		"-Wundef",
		"-Wcast-align",
		"-Wcast-qual",
		"-Woverloaded-virtual",
		"-Wnon-virtual-dtor"
	};
	std::vector<std::string> cflags, cxxflags, ldflags, defines; // globaux

	bool workspace = false;
	std::vector<std::string> module_paths;
	std::vector<ModeConfig> modes;
	std::map<std::string, ToolchainConfig> toolchains;
	std::vector<ModuleConfig> modules;
	PackageConfig package;
	std::map<std::string, DeployConfig> deploys;

	const ModeConfig* find_mode(const std::string& n) const {
		for (auto& m : modes) if (m.name == n) return &m;
		return nullptr;
	}
	const ModuleConfig* find_module(const std::string& n) const {
		for (auto& m : modules) if (m.name == n) return &m;
		return nullptr;
	}
};

namespace cfg {

struct ConfigError : std::runtime_error {
	ConfigError(std::string m) : std::runtime_error(std::move(m)) {}
};

inline std::vector<ModeConfig> default_modes() {
	return {
		{"release",  {"-O2"}, {}, {}, {}, {}, {"NDEBUG"}},
		{"debug",    {"-O0", "-g3"}, {}, {}, {}, {}, {"DEBUG"}},
		{"relwithdebinfo", {"-O2", "-g"}, {}, {}, {}, {}, {"NDEBUG"}},
		{"minsize",  {"-Os"}, {}, {}, {}, {}, {"NDEBUG"}},
		{"asan",     {"-O1", "-g3", "-fsanitize=address", "-fno-omit-frame-pointer"},
					 {"-fsanitize=address"}, "address", "", {}, {"DEBUG"}},
		{"tsan",     {"-O1", "-g3", "-fsanitize=thread", "-fno-omit-frame-pointer"},
					 {"-fsanitize=thread"}, "thread", "", {}, {"DEBUG"}},
		{"ubsan",    {"-O1", "-g3", "-fsanitize=undefined", "-fno-omit-frame-pointer"},
					 {"-fsanitize=undefined"}, "undefined", "", {}, {"DEBUG"}},
		{"valgrind", {"-O0", "-g3"}, {}, {}, "valgrind",
					 {"--leak-check=full", "--show-leak-kinds=all",
					  "--track-origins=yes", "--error-exitcode=1"}, {"DEBUG"}},
	};
}

inline const std::set<std::string>& cxx_standards() {
	static const std::set<std::string> s = {
		"c++98", "c++03", "c++11", "c++14", "c++17", "c++20", "c++23", "c++26",
		"gnu++98", "gnu++03", "gnu++11", "gnu++14", "gnu++17", "gnu++20", "gnu++23", "gnu++26"};
	return s;
}
inline const std::set<std::string>& c_standards() {
	static const std::set<std::string> s = {
		"c89", "c90", "c99", "c11", "c17", "c18", "c23", "c2x",
		"gnu89", "gnu90", "gnu99", "gnu11", "gnu17", "gnu18", "gnu23", "gnu2x"};
	return s;
}

// Les modes "-DXXX" dans flags (format v0.3) sont convertis en defines.
inline void normalize_mode(ModeConfig& m) {
	std::vector<std::string> flags;
	for (auto& f : m.flags) {
		if (f.starts_with("-D") && f.size() > 2) {
			if (std::find(m.defines.begin(), m.defines.end(), f.substr(2)) == m.defines.end())
				m.defines.push_back(f.substr(2));
		} else {
			flags.push_back(f);
		}
	}
	m.flags = std::move(flags);
	if (!m.sanitizer.empty()) {
		auto has = [](const std::vector<std::string>& v) {
			return std::any_of(v.begin(), v.end(),
							   [](auto& f) { return f.starts_with("-fsanitize="); });
		};
		if (!has(m.flags)) {
			m.flags.push_back("-fsanitize=" + m.sanitizer);
			m.flags.push_back("-fno-omit-frame-pointer");
		}
		if (!has(m.link_flags)) m.link_flags.push_back("-fsanitize=" + m.sanitizer);
	}
}

inline void parse_modes(const toml::Table& build, Project& p) {
	auto mt = toml::get_table(build, "modes");
	if (!mt) return;
	auto defaults = default_modes();
	// Ordre de traitement : un mode passe après le mode utilisateur dont il hérite.
	std::vector<std::string> order, pending;
	for (auto& [mn, mv] : *mt) pending.push_back(mn);
	while (!pending.empty()) {
		bool progress = false;
		for (auto it = pending.begin(); it != pending.end();) {
			auto t = mt->at(*it).table();
			std::optional<std::string> base = t ? toml::get_str(*t, "base") : std::nullopt;
			bool waits = base && *base != *it &&
						 std::find(pending.begin(), pending.end(), *base) != pending.end();
			if (waits) { ++it; continue; }
			order.push_back(*it);
			it = pending.erase(it);
			progress = true;
		}
		if (!progress)
			throw ConfigError("build.modes : héritage circulaire entre " + util::join(pending, ", "));
	}
	for (auto& mn : order) {
		auto t = mt->at(mn).table();
		if (!t) throw ConfigError(std::format("build.modes.{} doit être une table", mn));
		ModeConfig m;
		m.name = mn;
		auto base = toml::get_str(*t, "base");
		auto flags = toml::get_strs(*t, "flags");
		auto lflags = toml::get_strs(*t, "link_flags");
		auto defs = toml::get_strs(*t, "defines");
		toml::set_if(m.sanitizer, toml::get_str(*t, "sanitizer"));
		toml::set_if(m.runner, toml::get_str(*t, "runner"));
		auto rargs = toml::get_strs(*t, "runner_args");
		if (base) {
			const ModeConfig* b = nullptr;
			for (auto& d : p.modes) if (d.name == *base) b = &d;
			if (!b) for (auto& d : defaults) if (d.name == *base) b = &d;
			if (!b) throw ConfigError(std::format("build.modes.{} : base '{}' inconnue", mn, *base));
			ModeConfig bb = *b;
			if (m.sanitizer.empty()) m.sanitizer = bb.sanitizer;
			if (m.runner.empty()) { m.runner = bb.runner; if (!rargs) m.runner_args = bb.runner_args; }
			m.flags = bb.flags; m.link_flags = bb.link_flags; m.defines = bb.defines;
			// les sanitizers de la base ne s'appliquent que si conservés
			if (m.sanitizer != bb.sanitizer) {
				std::erase_if(m.flags, [](auto& f) { return f.starts_with("-fsanitize="); });
				std::erase_if(m.link_flags, [](auto& f) { return f.starts_with("-fsanitize="); });
			}
		}
		if (flags) m.flags = *flags;
		if (lflags) m.link_flags = *lflags;
		if (defs) m.defines = *defs;
		if (rargs) m.runner_args = *rargs;
		normalize_mode(m);
		p.modes.push_back(std::move(m));
	}
}

inline ToolchainConfig parse_toolchain(const std::string& name, const toml::Table& t) {
	ToolchainConfig c;
	c.name = name;
	c.from_config = true;
	toml::set_if(c.prefix, toml::get_str(t, "prefix"));
	toml::set_if(c.cc, toml::get_str(t, "cc"));
	toml::set_if(c.cxx, toml::get_str(t, "cxx"));
	toml::set_if(c.ar, toml::get_str(t, "ar"));
	toml::set_if(c.strip, toml::get_str(t, "strip"));
	toml::set_if(c.pkg_config, toml::get_str(t, "pkg_config"));
	toml::set_if(c.family, toml::get_str(t, "family"));
	toml::set_if(c.target, toml::get_str(t, "target"));
	toml::set_if(c.sysroot, toml::get_str(t, "sysroot"));
	toml::set_if(c.os, toml::get_str(t, "os"));
	toml::set_if(c.cflags, toml::get_strs(t, "cflags"));
	toml::set_if(c.cxxflags, toml::get_strs(t, "cxxflags"));
	toml::set_if(c.ldflags, toml::get_strs(t, "ldflags"));
	if (auto r = toml::get_strs(t, "runner")) { c.runner = *r; c.runner_set = true; }
	// v0.4 : extra_flags = options de compilation communes C et C++.
	if (auto f = toml::get_strs(t, "extra_flags")) {
		c.cflags.insert(c.cflags.end(), f->begin(), f->end());
		c.cxxflags.insert(c.cxxflags.end(), f->begin(), f->end());
	}
	return c;
}

inline PlatformOverride parse_platform(const toml::Table& t) {
	PlatformOverride o;
	toml::set_if(o.sources, toml::get_strs(t, "sources"));
	toml::set_if(o.defines, toml::get_strs(t, "defines"));
	toml::set_if(o.system_libs, toml::get_strs(t, "system"));
	toml::set_if(o.cflags, toml::get_strs(t, "cflags"));
	toml::set_if(o.cxxflags, toml::get_strs(t, "cxxflags"));
	toml::set_if(o.ldflags, toml::get_strs(t, "ldflags"));
	return o;
}

// Lit un module (module.toml, ou cpp-maker.toml pour un projet mono-module).
// `implicit` : accepte aussi les clés v0.3 ([project].type, [layout], [compiler]).
inline ModuleConfig parse_module(const toml::Table& root, const fs::path& dir,
								 const fs::path& file, const std::string& default_name,
								 bool implicit) {
	ModuleConfig m;
	m.dir = dir;
	m.file = file;
	m.name = default_name;
	bool priv_set = false;

	if (implicit) {
		if (auto pt = toml::get_table(root, "project"))
			if (auto t = toml::get_str(*pt, "type")) m.type = *t;
		if (auto lt = toml::get_table(root, "layout")) {
			toml::set_if(m.source_dirs, toml::get_strs(*lt, "source_dirs"));
			toml::set_if(m.test_dirs, toml::get_strs(*lt, "test_dirs"));
		}
		if (auto ct = toml::get_table(root, "compiler"))
			toml::set_if(m.public_includes, toml::get_strs(*ct, "include_dirs"));
	}
	if (auto t = toml::get_table(root, "module")) {
		toml::set_if(m.name, toml::get_str(*t, "name"));
		toml::set_if(m.type, toml::get_str(*t, "type"));
		toml::set_if(m.version, toml::get_str(*t, "version"));
		toml::set_if(m.main, toml::get_str(*t, "main"));
		toml::set_if(m.layer, toml::get_str(*t, "layer"));
		toml::set_if(m.install, toml::get_bool(*t, "install"));
		toml::set_if(m.c_standard, toml::get_str(*t, "c_standard"));
		toml::set_if(m.cxx_standard, toml::get_str(*t, "standard"));
		toml::set_if(m.cxx_standard, toml::get_str(*t, "cxx_standard"));
	}
	if (auto t = toml::get_table(root, "sources")) {
		toml::set_if(m.source_dirs, toml::get_strs(*t, "dirs"));
		toml::set_if(m.source_files, toml::get_strs(*t, "files"));
		toml::set_if(m.exclude, toml::get_strs(*t, "exclude"));
		toml::set_if(m.main, toml::get_str(*t, "main"));
	}
	if (auto t = toml::get_table(root, "include")) {
		toml::set_if(m.public_includes, toml::get_strs(*t, "public"));
		if (auto pv = toml::get_strs(*t, "private")) { m.private_includes = *pv; priv_set = true; }
	}
	if (auto t = toml::get_table(root, "dependencies")) {
		toml::set_if(m.deps, toml::get_strs(*t, "modules"));
		toml::set_if(m.pkg_config, toml::get_strs(*t, "pkg_config"));
		toml::set_if(m.system_libs, toml::get_strs(*t, "system"));
	}
	if (auto t = toml::get_table(root, "flags")) {
		toml::set_if(m.defines, toml::get_strs(*t, "defines"));
		toml::set_if(m.public_defines, toml::get_strs(*t, "public_defines"));
		toml::set_if(m.cflags, toml::get_strs(*t, "cflags"));
		toml::set_if(m.cxxflags, toml::get_strs(*t, "cxxflags"));
		toml::set_if(m.ldflags, toml::get_strs(*t, "ldflags"));
	}
	if (auto t = toml::get_table(root, "tests"))
		toml::set_if(m.test_dirs, toml::get_strs(*t, "dirs"));
	if (auto t = toml::get_table(root, "platform")) {
		for (auto& [os, v] : *t) {
			if (!v.table()) throw ConfigError(std::format("platform.{} doit être une table", os));
			m.platforms[os] = parse_platform(*v.table());
		}
	}
	if (m.type == "library") m.type = "static";        // alias v0.3
	if (m.type == "interface") m.type = "header-only";
	if (!priv_set) m.private_includes = m.source_dirs;
	return m;
}

// Charge cpp-maker.toml et, pour un workspace, chaque module.toml.
inline Project load(const fs::path& root_dir, const fs::path& file) {
	Project p;
	p.root = root_dir;
	p.file = file;
	p.name = root_dir.filename().string();
	std::error_code ec;
	p.has_file = fs::exists(file, ec);

	toml::Table root;
	if (p.has_file) root = toml::parse_file(file);

	try {
		if (auto t = toml::get_table(root, "project")) {
			toml::set_if(p.name, toml::get_str(*t, "name"));
			toml::set_if(p.version, toml::get_str(*t, "version"));
			toml::set_if(p.description, toml::get_str(*t, "description"));
			toml::set_if(p.cxx_standard, toml::get_str(*t, "standard"));
			toml::set_if(p.cxx_standard, toml::get_str(*t, "cxx_standard"));
			toml::set_if(p.c_standard, toml::get_str(*t, "c_standard"));
			toml::set_if(p.min_version, toml::get_str(*t, "minimum_cpp_maker_version"));
		}
		if (auto t = toml::get_table(root, "build")) {
			toml::set_if(p.default_mode, toml::get_str(*t, "default_mode"));
			toml::set_if(p.default_toolchain, toml::get_str(*t, "default_toolchain"));
			if (auto j = toml::get_int(*t, "jobs")) p.jobs = (int)*j;
			toml::set_if(p.warnings, toml::get_strs(*t, "warnings"));
			toml::set_if(p.static_pic, toml::get_bool(*t, "static_pic"));
			toml::set_if(p.cflags, toml::get_strs(*t, "cflags"));
			toml::set_if(p.cxxflags, toml::get_strs(*t, "cxxflags"));
			toml::set_if(p.ldflags, toml::get_strs(*t, "ldflags"));
			toml::set_if(p.defines, toml::get_strs(*t, "defines"));
			parse_modes(*t, p);
		}
		// Modes standard non redéfinis : conservés.
		for (auto& d : default_modes())
			if (!p.find_mode(d.name)) p.modes.push_back(d);

		if (auto t = toml::get_table(root, "toolchains")) {
			for (auto& [n, v] : *t) {
				if (!v.table()) throw ConfigError(std::format("toolchains.{} doit être une table", n));
				p.toolchains[n] = parse_toolchain(n, *v.table());
			}
		}
		// [compiler] v0.3 : compilateur hôte + options globales.
		if (auto t = toml::get_table(root, "compiler")) {
			auto& host = p.toolchains["host"];
			host.name = "host";
			toml::set_if(host.cxx, toml::get_str(*t, "cxx"));
			toml::set_if(host.cc, toml::get_str(*t, "cc"));
			toml::set_if(host.family, toml::get_str(*t, "family"));
			if (auto f = toml::get_strs(*t, "cxxflags"))
				p.cxxflags.insert(p.cxxflags.end(), f->begin(), f->end());
			if (auto f = toml::get_strs(*t, "cflags"))
				p.cflags.insert(p.cflags.end(), f->begin(), f->end());
			if (auto f = toml::get_strs(*t, "ldflags"))
				p.ldflags.insert(p.ldflags.end(), f->begin(), f->end());
		}
		if (auto t = toml::get_table(root, "tests")) {
			if (auto to = toml::get_int(*t, "timeout")) p.test_timeout = (int)*to;
			if (auto env = toml::get_table(*t, "env")) {
				for (auto& [k, v] : *env) {
					if (!v.str()) throw ConfigError(std::format("tests.env.{} doit être une chaîne", k));
					p.test_env.emplace_back(k, *v.str());
				}
			}
		}
		if (auto t = toml::get_table(root, "generate")) {
			for (auto& [k, v] : *t) {
				if (!v.table()) throw ConfigError(std::format("generate.{} doit être une table", k));
				Project::GenerateStep g;
				g.name = k;
				toml::set_if(g.command, toml::get_str(*v.table(), "command"));
				toml::set_if(g.inputs, toml::get_strs(*v.table(), "inputs"));
				toml::set_if(g.outputs, toml::get_strs(*v.table(), "outputs"));
				if (g.command.empty()) throw ConfigError(std::format("generate.{}.command requis", k));
				p.generate_steps.push_back(std::move(g));
			}
		}
		if (auto t = toml::get_table(root, "layers")) {
			toml::set_if(p.layer_order, toml::get_strs(*t, "order"));
			for (auto& [k, v] : *t) {
				if (k == "order") continue;
				if (!v.table()) throw ConfigError(std::format("layers.{} doit être une table", k));
				if (auto f = toml::get_strs(*v.table(), "forbid_pkg_config")) p.layer_forbid_pkg[k] = *f;
			}
		}

		if (auto t = toml::get_table(root, "package")) {
			auto& k = p.package;
			toml::set_if(k.formats, toml::get_strs(*t, "formats"));
			toml::set_if(k.strip, toml::get_bool(*t, "strip"));
			toml::set_if(k.maintainer, toml::get_str(*t, "maintainer"));
			toml::set_if(k.description, toml::get_str(*t, "description"));
			toml::set_if(k.homepage, toml::get_str(*t, "homepage"));
			toml::set_if(k.license, toml::get_str(*t, "license"));
			toml::set_if(k.deb_depends, toml::get_strs(*t, "deb_depends"));
			toml::set_if(k.deb_prefix, toml::get_str(*t, "deb_prefix"));
			toml::set_if(k.extra_files, toml::get_strs(*t, "extra_files"));
			toml::set_if(k.checksums, toml::get_bool(*t, "checksums"));
		}
		if (p.package.description.empty()) p.package.description = p.description;

		if (auto t = toml::get_table(root, "deploy")) {
			if (toml::get_str(*t, "method").has_value() ||
				toml::find(*t, "target_dir")) {
				// [deploy] unique v0.4 : method = rsync | scp | local,
				// host = "user@machine", target_dir, pre/post → cible "default".
				DeployConfig d;
				d.name = "default";
				if (auto m = toml::get_str(*t, "method")) {
					if (*m == "local") d.method = "copy";
					else d.method = *m;
				}
				if (auto h = toml::get_str(*t, "host")) {
					auto at = h->find('@');
					if (at != std::string::npos) {
						d.user = h->substr(0, at);
						d.host = h->substr(at + 1);
					} else {
						d.host = *h;
					}
				}
				toml::set_if(d.path, toml::get_str(*t, "target_dir"));
				toml::set_if(d.before, toml::get_strs(*t, "pre"));
				// v0.4 : 'post' s'exécutait localement, jamais sur la cible.
				toml::set_if(d.after_local, toml::get_strs(*t, "post"));
				toml::set_if(d.script, toml::get_str(*t, "script"));
				p.deploys["default"] = std::move(d);
			} else {
			for (auto& [n, v] : *t) {
				auto dt = v.table();
				if (!dt) throw ConfigError(std::format("deploy.{} doit être une table", n));
				DeployConfig d;
				d.name = n;
				toml::set_if(d.method, toml::get_str(*dt, "method"));
				toml::set_if(d.host, toml::get_str(*dt, "host"));
				toml::set_if(d.user, toml::get_str(*dt, "user"));
				toml::set_if(d.path, toml::get_str(*dt, "path"));
				if (auto port = toml::get_int(*dt, "port")) d.port = (int)*port;
				toml::set_if(d.toolchain, toml::get_str(*dt, "toolchain"));
				toml::set_if(d.mode, toml::get_str(*dt, "mode"));
				toml::set_if(d.artifact, toml::get_str(*dt, "artifact"));
				toml::set_if(d.modules, toml::get_strs(*dt, "modules"));
				toml::set_if(d.ssh_options, toml::get_strs(*dt, "ssh_options"));
				toml::set_if(d.before, toml::get_strs(*dt, "before"));
				toml::set_if(d.after, toml::get_strs(*dt, "after"));
				toml::set_if(d.after_local, toml::get_strs(*dt, "after_local"));
				toml::set_if(d.script, toml::get_str(*dt, "script"));
				toml::set_if(d.delete_extra, toml::get_bool(*dt, "delete"));
				p.deploys[n] = std::move(d);
			}
			} // fin du format v0.5 [deploy.<nom>]
		}

		if (auto t = toml::get_table(root, "workspace")) {
			p.workspace = true;
			toml::set_if(p.module_paths, toml::get_strs(*t, "modules"));
		}
	} catch (const toml::ParseError& e) {
		throw ConfigError(std::format("{}: {}", file.string(), e.what()));
	}

	if (p.workspace) {
		for (auto& mp : p.module_paths) {
			fs::path dir = fs::path(mp).lexically_normal();
			if (dir.is_absolute() || (!dir.empty() && *dir.begin() == ".."))
				throw ConfigError(std::format("{}: module '{}' hors du workspace", file.string(), mp));
			fs::path mf = root_dir / dir / "module.toml";
			if (!fs::exists(mf, ec))
				throw ConfigError(std::format("{}: module '{}' : {} introuvable",
											  file.string(), mp, (dir / "module.toml").string()));
			auto mt = toml::parse_file(mf);
			try {
				p.modules.push_back(parse_module(mt, dir, dir / "module.toml",
												 dir.filename().string(), false));
			} catch (const toml::ParseError& e) {
				throw ConfigError(std::format("{}: {}", (dir / "module.toml").string(), e.what()));
			}
		}
	} else if (toml::get_table(root, "modules")) {
		// Rétro-compatibilité v0.4 : [modules.<nom>] dans cpp-maker.toml.
		// Chaque module reste à la racine du projet (dir = "."), les
		// source_dirs/include_dirs v0.4 étant déjà relatifs à la racine.
		auto mt = toml::get_table(root, "modules");
		try {
			for (auto& [mn, mv] : *mt) {
				auto t = mv.table();
				if (!t) throw toml::ParseError(std::format("modules.{} doit être une table", mn));
				ModuleConfig m;
				m.name = mn;
				m.dir = ".";
				m.file = file.filename();
				// Défauts v0.4 : type static, aucun dossier implicite. Les tests
				// v0.4 (tests/) appartiennent à l'exécutable principal seul.
				m.type = "static";
				m.source_dirs.clear();
				m.public_includes.clear();
				m.test_dirs.clear();
				toml::set_if(m.type, toml::get_str(*t, "type"));
				toml::set_if(m.source_dirs, toml::get_strs(*t, "source_dirs"));
				toml::set_if(m.public_includes, toml::get_strs(*t, "include_dirs"));
				toml::set_if(m.deps, toml::get_strs(*t, "deps"));
				toml::set_if(m.defines, toml::get_strs(*t, "defines"));
				if (auto f = toml::get_strs(*t, "flags")) {
					m.cflags = *f;
					m.cxxflags = std::move(*f);
				}
				toml::set_if(m.ldflags, toml::get_strs(*t, "link_flags"));
				if (m.type == "interface") m.type = "header-only"; // alias v0.4
				if (m.type == "library")   m.type = "static";      // alias v0.3
				m.private_includes = m.source_dirs;
				p.modules.push_back(std::move(m));
			}
			// L'exécutable global v0.4 (sources du projet hors modules, p.ex.
			// src/main.cpp) reste la cible principale : il est chargé comme
			// un module de plus, dépendant de tous les [modules.*] — qui sont
			// désormais liés automatiquement (plus de ldflags manuels).
			{
				std::vector<std::string> dirs = {"src"};
				if (auto lt = toml::get_table(root, "layout"))
					if (auto d = toml::get_strs(*lt, "source_dirs")) dirs = *d;
				auto has_source = [&](const std::string& d) {
					std::error_code dir_ec;
					if (!fs::is_directory(d, dir_ec)) return false;
					fs::recursive_directory_iterator it(d,
						fs::directory_options::skip_permission_denied, dir_ec);
					if (dir_ec) return false;
					for (; it != fs::recursive_directory_iterator(); it.increment(dir_ec)) {
						if (dir_ec) break;
						if (!it->is_regular_file(dir_ec) || dir_ec) continue;
						auto e = it->path().extension().string();
						for (auto* x : {".c", ".cpp", ".cc", ".cxx", ".c++", ".C", ".S", ".s"})
							if (e == x) return true;
					}
					return false;
				};
				bool any = false;
				for (auto& d : dirs) if (has_source(d)) { any = true; break; }
				if (any) {
					try {
						auto m = parse_module(root, ".", file.filename(), p.name, true);
						for (auto& lm : p.modules) m.deps.push_back(lm.name);
						p.modules.push_back(std::move(m));
					} catch (const toml::ParseError& e) {
						throw ConfigError(std::format("{}: {}", file.string(), e.what()));
					}
				}
			}
		} catch (const toml::ParseError& e) {
			throw ConfigError(std::format("{}: {}", file.string(), e.what()));
		}
	} else {
		try {
			p.modules.push_back(parse_module(root, ".", file.filename(), p.name, true));
		} catch (const toml::ParseError& e) {
			throw ConfigError(std::format("{}: {}", file.string(), e.what()));
		}
	}
	return p;
}

// Ordre topologique (dépendances d'abord) de `roots` et de leur fermeture.
// Lève ConfigError en cas de cycle ou de module inconnu.
inline std::vector<std::string> topo_order(const Project& p, const std::vector<std::string>& roots) {
	std::vector<std::string> order;
	std::map<std::string, int> state; // 0 = blanc, 1 = en cours, 2 = fini
	std::vector<std::string> stack;
	std::function<void(const std::string&)> visit = [&](const std::string& n) {
		auto m = p.find_module(n);
		if (!m) throw ConfigError(std::format("module inconnu : '{}'", n));
		if (state[n] == 2) return;
		if (state[n] == 1) {
			auto it = std::find(stack.begin(), stack.end(), n);
			std::vector<std::string> cyc(it, stack.end());
			cyc.push_back(n);
			throw ConfigError("dépendance circulaire : " + util::join(cyc, " → "));
		}
		state[n] = 1;
		stack.push_back(n);
		for (auto& d : m->deps) visit(d);
		stack.pop_back();
		state[n] = 2;
		order.push_back(n);
	};
	for (auto& r : roots) visit(r);
	return order;
}

// Retourne une chaîne vide si la configuration est valide, sinon un
// diagnostic complet (fichier + champ + nature de l'erreur).
// Couches d'architecture : un module ne dépend que de sa couche ou d'une
// couche inférieure ; les paquets interdits d'une couche ne doivent pas
// apparaître dans la fermeture transitive de ses modules.
inline std::string validate_layers(const Project& p, const std::string& file) {
	auto rank = [&](const std::string& l) -> int {
		for (size_t i = 0; i < p.layer_order.size(); ++i) if (p.layer_order[i] == l) return (int)i;
		return -1;
	};
	for (auto& [l, f] : p.layer_forbid_pkg) {
		(void)f;
		if (rank(l) < 0)
			return std::format("{}: [layers.{}] : couche absente de layers.order", file, l);
	}
	for (auto& m : p.modules) {
		if (m.layer.empty()) continue;
		if (rank(m.layer) < 0)
			return std::format("{}: couche inconnue '{}' (layers.order : {})", m.file.string(), m.layer,
							   p.layer_order.empty() ? "vide" : util::join(p.layer_order, ", "));
	}
	for (auto& m : p.modules) {
		if (m.layer.empty()) continue;
		for (auto& d : m.deps) {
			auto dm = p.find_module(d);
			if (!dm || dm->layer.empty()) continue;
			if (rank(dm->layer) > rank(m.layer))
				return std::format("{}: '{}' (couche {}) ne peut pas dépendre de '{}' (couche {}, "
								   "supérieure) — inversez la dépendance ou déplacez le code",
								   m.file.string(), m.name, m.layer, d, dm->layer);
		}
		auto it = p.layer_forbid_pkg.find(m.layer);
		if (it == p.layer_forbid_pkg.end()) continue;
		// Fermeture transitive des dépendances de modules.
		std::vector<const ModuleConfig*> stack{&m};
		std::set<std::string> seen{m.name};
		while (!stack.empty()) {
			const ModuleConfig* cur = stack.back();
			stack.pop_back();
			for (auto& pkg : cur->pkg_config)
				if (std::find(it->second.begin(), it->second.end(), pkg) != it->second.end())
					return std::format("{}: le paquet '{}' est interdit dans la couche {} ; "
									   "'{}' en dépend{}", m.file.string(), pkg, m.layer, m.name,
									   cur == &m ? "" : std::format(" via '{}'", cur->name));
			for (auto& d : cur->deps)
				if (auto dm = p.find_module(d); dm && seen.insert(d).second) stack.push_back(dm);
		}
	}
	return {};
}

inline std::string validate_errors(const Project& p) {
	std::string file = p.file.string();
	if (!cfg::cxx_standards().count(p.cxx_standard))
		return std::format("{}: champ 'project.standard' invalide : '{}'", file, p.cxx_standard);
	if (!cfg::c_standards().count(p.c_standard))
		return std::format("{}: champ 'project.c_standard' invalide : '{}'", file, p.c_standard);
	if (!util::is_safe_name(p.name))
		return std::format("{}: champ 'project.name' invalide : '{}'", file, p.name);
	if (p.jobs < 0) return std::format("{}: 'build.jobs' doit être ≥ 0", file);
	if (!p.find_mode(p.default_mode))
		return std::format("{}: 'build.default_mode' : mode inconnu '{}'", file, p.default_mode);
	for (auto& m : p.modes) {
		if (!util::is_safe_name(m.name))
			return std::format("{}: nom de mode invalide : '{}'", file, m.name);
		if (!m.sanitizer.empty() && m.sanitizer != "address" && m.sanitizer != "thread" &&
			m.sanitizer != "undefined" && m.sanitizer != "leak")
			return std::format("{}: sanitizer '{}' non supporté (address|thread|undefined|leak)",
							   file, m.sanitizer);
		if (!m.runner.empty() && m.runner != "valgrind")
			return std::format("{}: runner '{}' non supporté (valgrind)", file, m.runner);
	}
	for (auto& [n, t] : p.toolchains) {
		if (!util::is_safe_name(n))
			return std::format("{}: nom de toolchain invalide : '{}'", file, n);
		if (t.family != "auto" && t.family != "gcc" && t.family != "clang")
			return std::format("{}: toolchains.{}.family invalide : '{}'", file, n, t.family);
		if (!t.os.empty() && t.os != "linux" && t.os != "windows" && t.os != "macos" &&
			t.os != "android" && t.os != "freebsd" && t.os != "baremetal")
			return std::format("{}: toolchains.{}.os invalide : '{}'", file, n, t.os);
	}
	std::set<std::string> names;
	for (auto& m : p.modules) {
		std::string mf = m.file.string();
		if (!util::is_safe_name(m.name))
			return std::format("{}: nom de module invalide : '{}'", mf, m.name);
		if (!names.insert(m.name).second)
			return std::format("{}: module '{}' défini deux fois", mf, m.name);
		static const std::set<std::string> types = {"executable", "static", "shared", "header-only"};
		if (!types.count(m.type))
			return std::format("{}: type de module invalide : '{}' "
							   "(executable|static|shared|header-only)", mf, m.type);
		if (!m.cxx_standard.empty() && !cfg::cxx_standards().count(m.cxx_standard))
			return std::format("{}: standard C++ invalide : '{}'", mf, m.cxx_standard);
		if (!m.c_standard.empty() && !cfg::c_standards().count(m.c_standard))
			return std::format("{}: standard C invalide : '{}'", mf, m.c_standard);
		for (auto& d : m.deps) {
			if (d == m.name) return std::format("{}: le module '{}' dépend de lui-même", mf, d);
		}
		for (auto& [os, o] : m.platforms) {
			(void)o;
			static const std::set<std::string> oses = {"linux", "windows", "macos", "android",
													   "freebsd", "baremetal", "unix"};
			if (!oses.count(os))
				return std::format("{}: plate-forme inconnue [platform.{}]", mf, os);
		}
	}
	for (auto& m : p.modules) {
		for (auto& d : m.deps) {
			auto dm = p.find_module(d);
			if (!dm)
				return std::format("{}: dépendance inconnue '{}' (modules : {})", m.file.string(), d,
								   [&] { std::vector<std::string> v; for (auto& x : p.modules) v.push_back(x.name); return util::join(v, ", "); }());
			if (dm->type == "executable")
				return std::format("{}: '{}' dépend de l'exécutable '{}' (seules les bibliothèques "
								   "peuvent être des dépendances)", m.file.string(), m.name, d);
		}
	}
	try {
		std::vector<std::string> all;
		for (auto& m : p.modules) all.push_back(m.name);
		topo_order(p, all);
	} catch (const ConfigError& e) {
		return std::format("{}: {}", file, e.what());
	}
	if (auto err = validate_layers(p, file); !err.empty()) return err;
	for (auto& [n, d] : p.deploys) {
		if (d.method != "rsync" && d.method != "scp" && d.method != "copy" && d.method != "script")
			return std::format("{}: deploy.{}.method invalide : '{}' (rsync|scp|copy|script)", file, n, d.method);
		if (d.method == "script") {
			if (d.script.empty())
				return std::format("{}: deploy.{}.script requis pour la méthode 'script'", file, n);
			continue;
		}
		if (d.method != "copy" && d.host.empty())
			return std::format("{}: deploy.{}.host requis pour la méthode '{}'", file, n, d.method);
		if (d.path.empty())
			return std::format("{}: deploy.{}.path requis", file, n);
		static const std::set<std::string> arts = {"stage", "tar.gz", "tar.xz", "zip", "deb"};
		if (!arts.count(d.artifact))
			return std::format("{}: deploy.{}.artifact invalide : '{}'", file, n, d.artifact);
	}
	for (auto& f : p.package.formats) {
		static const std::set<std::string> fmts = {"tar.gz", "tar.xz", "zip", "deb"};
		if (!fmts.count(f))
			return std::format("{}: package.formats : format inconnu '{}' (tar.gz|tar.xz|zip|deb)", file, f);
	}
	return {};
}

} // namespace cfg

// ============================================================================
// toolchain — résolution des compilateurs C/C++ et de la plate-forme cible
// ============================================================================
namespace toolchain {

struct Tool {
	std::string exe;                    // nom ou chemin
	std::vector<std::string> pre_args;  // --target=…, --sysroot=…
	std::string family;                 // gcc | clang | unknown
	std::string version;                // 1re ligne de --version
};

struct Toolchain {
	std::string name;                   // nom utilisé pour build/<nom>/
	ToolchainConfig cfg;
	std::optional<Tool> cc, cxx;
	std::string ar, strip, pkg_config;
	std::string triple, arch, os;       // os : linux | windows | macos | android | baremetal…
	bool cross = false;
	std::vector<std::string> runner;    // émulateur (vide = exécution native)
	std::string runner_note;            // explication si exécution impossible

	bool can_execute() const { return !cross || !runner.empty(); }
	std::string exe_suffix() const { return os == "windows" ? ".exe" : ""; }

	const Tool& c_tool() const {
		if (!cc) util::die(std::format("toolchain '{}' : aucun compilateur C trouvé{}", name,
									   cfg.cc != "auto" ? " ('" + cfg.cc + "')" : std::string{}));
		return *cc;
	}
	const Tool& cxx_tool() const {
		if (!cxx) util::die(std::format("toolchain '{}' : aucun compilateur C++ trouvé{}", name,
										cfg.cxx != "auto" ? " ('" + cfg.cxx + "')" : std::string{}));
		return *cxx;
	}
};

inline std::optional<Tool> probe(const std::string& exe, const std::vector<std::string>& pre = {}) {
	if (!util::have(exe)) return std::nullopt;
	auto r = proc::capture({exe, "--version"});
	if (r.exit_code != 0) return std::nullopt;
	Tool t;
	t.exe = exe;
	t.pre_args = pre;
	const std::string& v = r.output;
	if (v.find("clang") != std::string::npos) t.family = "clang";
	else if (v.find("gcc") != std::string::npos || v.find("GCC") != std::string::npos ||
			 v.find("Free Software Foundation") != std::string::npos) t.family = "gcc";
	else t.family = "unknown";
	auto nl = v.find('\n');
	t.version = v.substr(0, nl == std::string::npos ? v.size() : nl);
	return t;
}

inline std::string dumpmachine(const Tool& t) {
	std::vector<std::string> cmd = {t.exe};
	cmd.insert(cmd.end(), t.pre_args.begin(), t.pre_args.end());
	cmd.push_back("-dumpmachine");
	auto r = proc::capture(cmd);
	if (r.exit_code != 0) return {};
	return util::trim(r.output);
}

// Architecture normalisée : x86_64, x86, arm, aarch64, riscv64, mips…
inline std::string normalize_arch(std::string a) {
	if (a == "amd64") return "x86_64";
	if (a == "i386" || a == "i486" || a == "i586" || a == "i686") return "x86";
	if (a == "arm64") return "aarch64";
	if (a.starts_with("armv") || a == "armhf" || a == "armel" || a == "arm") return "arm";
	return a;
}

inline std::string os_from_triple(const std::string& t) {
	if (t.find("mingw") != std::string::npos || t.find("windows") != std::string::npos ||
		t.find("cygwin") != std::string::npos || t.find("msvc") != std::string::npos) return "windows";
	if (t.find("apple") != std::string::npos || t.find("darwin") != std::string::npos) return "macos";
	if (t.find("android") != std::string::npos) return "android";
	if (t.find("freebsd") != std::string::npos) return "freebsd";
	if (t.find("linux") != std::string::npos) return "linux";
	if (t.find("none") != std::string::npos || t.find("elf") != std::string::npos ||
		t.ends_with("eabi") || t.ends_with("eabihf")) return "baremetal";
	return "linux";
}

inline std::string host_arch() {
#ifdef __unix__
	utsname u{};
	if (uname(&u) == 0) return normalize_arch(u.machine);
#endif
	return "unknown";
}
inline std::string host_os() {
#if defined(_WIN32)
	return "windows";
#elif defined(__APPLE__)
	return "macos";
#elif defined(__FreeBSD__)
	return "freebsd";
#else
	return "linux";
#endif
}

// Architecture Debian (pour les paquets .deb).
inline std::string deb_arch(const Toolchain& t) {
	auto a = t.arch;
	if (a == "x86_64") return "amd64";
	if (a == "x86") return "i386";
	if (a == "aarch64") return "arm64";
	if (a == "arm") return t.triple.find("hf") != std::string::npos ? "armhf" : "armel";
	if (a == "riscv64") return "riscv64";
	if (a.starts_with("powerpc64le") || a == "ppc64le") return "ppc64el";
	return a;
}

// Émulateur utilisable pour exécuter un binaire de la cible sur l'hôte.
inline std::vector<std::string> auto_runner(const Toolchain& t, std::string& note) {
	if (t.os == "windows") {
		for (auto w : {"wine64", "wine"})
			if (util::have(w)) return {w};
		note = "cible Windows : installez wine pour exécuter les binaires";
		return {};
	}
	if (t.os == "baremetal") { note = "cible bare-metal : exécution impossible sur l'hôte"; return {}; }
	if (t.os == "macos") { note = "cible macOS : exécution impossible sur l'hôte"; return {}; }
	static const std::map<std::string, std::string> qemu = {
		{"arm", "qemu-arm"}, {"aarch64", "qemu-aarch64"}, {"x86", "qemu-i386"},
		{"x86_64", "qemu-x86_64"}, {"riscv64", "qemu-riscv64"}, {"riscv32", "qemu-riscv32"},
		{"mips", "qemu-mips"}, {"mipsel", "qemu-mipsel"}, {"mips64", "qemu-mips64"},
		{"mips64el", "qemu-mips64el"}, {"powerpc", "qemu-ppc"}, {"powerpc64", "qemu-ppc64"},
		{"powerpc64le", "qemu-ppc64le"}, {"s390x", "qemu-s390x"},
	};
	auto it = qemu.find(t.arch);
	if (it == qemu.end()) { note = "aucun émulateur connu pour l'architecture " + t.arch; return {}; }
	std::string exe;
	for (auto cand : {it->second, it->second + "-static"})
		if (util::have(cand)) { exe = cand; break; }
	if (exe.empty()) {
		note = std::format("installez {} (paquet qemu-user) pour exécuter les binaires {}",
						   it->second, t.arch);
		return {};
	}
	std::vector<std::string> r = {exe};
	std::string sysroot = t.cfg.sysroot;
	std::error_code ec;
	if (sysroot.empty() && !t.triple.empty() && fs::is_directory("/usr/" + t.triple, ec))
		sysroot = "/usr/" + t.triple;
	if (!sysroot.empty()) { r.push_back("-L"); r.push_back(sysroot); }
	return r;
}

// Candidats « auto » pour l'hôte.
inline std::vector<std::string> host_candidates(bool cxx, const std::string& family) {
	std::vector<std::string> r;
	if (const char* e = std::getenv(cxx ? "CXX" : "CC"); e && *e) r.push_back(e);
	auto gcc = cxx ? std::vector<std::string>{"g++", "g++-16", "g++-15", "g++-14", "g++-13", "g++-12"}
				   : std::vector<std::string>{"gcc", "gcc-16", "gcc-15", "gcc-14", "gcc-13", "gcc-12"};
	auto clang = cxx ? std::vector<std::string>{"clang++", "clang++-20", "clang++-19", "clang++-18", "clang++-17"}
					 : std::vector<std::string>{"clang", "clang-20", "clang-19", "clang-18", "clang-17"};
	if (family == "clang") { r.insert(r.end(), clang.begin(), clang.end()); return r; }
	r.insert(r.end(), gcc.begin(), gcc.end());
	if (family == "auto") {
		r.insert(r.end(), clang.begin(), clang.end());
		r.push_back(cxx ? "c++" : "cc");
	}
	return r;
}

// Les préfixes de triplets installés (arm-linux-gnueabihf-, x86_64-w64-mingw32-…).
inline std::vector<std::string> detect_prefixes() {
	std::set<std::string> out;
	const char* env = std::getenv("PATH");
	for (auto& dir : util::split_list(env ? env : "/usr/bin", ':')) {
		std::error_code ec;
		fs::directory_iterator it(dir, ec);
		if (ec) continue;
		for (; it != fs::directory_iterator(); it.increment(ec)) {
			if (ec) break;
			auto n = it->path().filename().string();
			for (std::string suf : {"-gcc", "-g++"}) {
				if (n.size() > suf.size() && n.ends_with(suf)) {
					auto pre = n.substr(0, n.size() - suf.size() + 1); // garde le '-'
					if (std::count(pre.begin(), pre.end(), '-') >= 3) out.insert(pre);
				}
			}
		}
	}
	return {out.begin(), out.end()};
}

// Construit la configuration d'une toolchain à partir de son nom :
//   - profil [toolchains.<nom>] du projet ;
//   - "host" | "gcc" | "clang" intégrés ;
//   - préfixe de triplet présent dans le PATH : "arm-linux-gnueabihf" ;
//   - nom d'un compilateur : "arm-linux-gnueabihf-g++", "clang++-18".
inline std::optional<ToolchainConfig> config_for(const Project& p, const std::string& name) {
	if (auto it = p.toolchains.find(name); it != p.toolchains.end()) {
		auto c = it->second;
		c.name = name;
		return c;
	}
	ToolchainConfig c;
	c.name = name;
	if (name == "host") return c;
	if (name == "gcc") { c.cc = "gcc"; c.cxx = "g++"; return c; }
	if (name == "clang") { c.cc = "clang"; c.cxx = "clang++"; return c; }
	// Nom de compilateur explicite.
	for (auto [suf, csuf, is_cxx] : {std::tuple{"clang++", "clang", true}, {"clang", "clang", false},
									 {"g++", "gcc", true}, {"gcc", "gcc", false}}) {
		std::string s = suf;
		auto pos = name.rfind(s);
		if (pos == std::string::npos) continue;
		if (pos != 0 && name[pos - 1] != '-' && name[pos - 1] != '/') continue;
		std::string tail = name.substr(pos + s.size());
		if (!tail.empty() && !(tail[0] == '-' && std::all_of(tail.begin() + 1, tail.end(), ::isdigit)))
			continue;
		if (!util::have(name)) continue;
		std::string prefix = name.substr(0, pos);
		std::string other = prefix + (is_cxx ? std::string(csuf) : (s == "gcc" ? "g++" : "clang++")) + tail;
		if (is_cxx) { c.cxx = name; c.cc = util::have(other) ? other : "none"; }
		else        { c.cc = name; c.cxx = util::have(other) ? other : "none"; }
		if (!prefix.empty()) c.prefix = prefix;
		return c;
	}
	// Préfixe de triplet.
	std::string prefix = name.ends_with("-") ? name : name + "-";
	if (util::have(prefix + "gcc") || util::have(prefix + "g++")) {
		c.prefix = prefix;
		return c;
	}
	return std::nullopt;
}

inline Toolchain resolve(const Project& p, const std::string& name,
						 const std::string& cc_override = {}, const std::string& cxx_override = {}) {
	auto oc = config_for(p, name);
	if (!oc) {
		std::vector<std::string> known = {"host", "gcc", "clang"};
		for (auto& [n, t] : p.toolchains)
			if (std::find(known.begin(), known.end(), n) == known.end()) known.push_back(n);
		for (auto& pre : detect_prefixes()) known.push_back(pre.substr(0, pre.size() - 1));
		throw std::runtime_error(std::format("toolchain inconnue : '{}'\n  disponibles : {}", name,
											 util::join(known, ", ")));
	}
	Toolchain t;
	t.cfg = *oc;
	auto& c = t.cfg;
	if (!cc_override.empty()) c.cc = cc_override;
	if (!cxx_override.empty()) c.cxx = cxx_override;
	t.name = name;
	if (!cc_override.empty() || !cxx_override.empty()) {
		std::string base = fs::path(!cxx_override.empty() ? cxx_override : cc_override).filename().string();
		t.name = util::is_safe_name(base) ? base : "custom";
	}
	if (!util::is_safe_name(t.name)) {
		std::string s;
		for (char ch : t.name) s += (std::isalnum((unsigned char)ch) || ch == '-' || ch == '_') ? ch : '_';
		t.name = s;
	}

	std::vector<std::string> pre;
	if (!c.target.empty()) pre.push_back("--target=" + c.target);
	if (!c.sysroot.empty()) pre.push_back("--sysroot=" + c.sysroot);

	auto pick = [&](bool cxx) -> std::optional<Tool> {
		std::string spec = cxx ? c.cxx : c.cc;
		if (spec == "none") return std::nullopt;
		if (spec != "auto") return probe(spec, pre);
		if (!c.prefix.empty()) {
			if (c.family == "clang") {
				// clang + préfixe : la cible est déduite du triplet.
				auto v = pre;
				if (c.target.empty())
					v.insert(v.begin(), "--target=" + c.prefix.substr(0, c.prefix.size() - 1));
				return probe(cxx ? "clang++" : "clang", v);
			}
			return probe(c.prefix + (cxx ? "g++" : "gcc"), pre);
		}
		for (auto& cand : host_candidates(cxx, c.family))
			if (auto r = probe(cand, pre)) return r;
		return std::nullopt;
	};
	t.cc = pick(false);
	t.cxx = pick(true);
	if (!t.cc && !t.cxx)
		throw std::runtime_error(std::format("toolchain '{}' : aucun compilateur C ou C++ utilisable "
							  "(cc = \"{}\", cxx = \"{}\"{})", name, c.cc, c.cxx,
							  c.prefix.empty() ? "" : ", prefix = \"" + c.prefix + "\""));

	// Outils binutils : préfixe, sinon versions llvm pour clang, sinon défaut.
	auto bin = [&](const std::string& explicit_, const std::string& tool) {
		if (!explicit_.empty()) return explicit_;
		if (!c.prefix.empty() && util::have(c.prefix + tool)) return c.prefix + tool;
		const Tool& any = t.cxx ? *t.cxx : *t.cc;
		if (any.family == "clang" && !c.target.empty() && util::have("llvm-" + tool)) return "llvm-" + tool;
		if (any.family == "gcc" && tool == "ar") {
			// gcc-ar est requis pour -flto ; on l'utilise s'il est appairé.
			auto exe = fs::path(any.exe).filename().string();
			auto pos = exe.find("g++");
			if (pos == std::string::npos) pos = exe.find("gcc");
			if (pos != std::string::npos) {
				std::string gar = exe.substr(0, pos) + "gcc-ar" + exe.substr(pos + 3);
				if (util::have(gar)) return gar;
			}
		}
		return tool;
	};
	t.ar = bin(c.ar, "ar");
	t.strip = bin(c.strip, "strip");
	if (!c.pkg_config.empty()) t.pkg_config = c.pkg_config;
	else if (!c.prefix.empty() && util::have(c.prefix + "pkg-config")) t.pkg_config = c.prefix + "pkg-config";
	else t.pkg_config = "pkg-config";

	t.triple = dumpmachine(t.cxx ? *t.cxx : *t.cc);
	if (t.triple.empty() && !c.target.empty()) t.triple = c.target;
	if (t.triple.empty() && !c.prefix.empty()) t.triple = c.prefix.substr(0, c.prefix.size() - 1);
	t.arch = normalize_arch(t.triple.empty() ? host_arch() : t.triple.substr(0, t.triple.find('-')));
	t.os = !c.os.empty() ? c.os : (t.triple.empty() ? host_os() : os_from_triple(t.triple));
	t.cross = t.arch != host_arch() || t.os != host_os();

	if (c.runner_set) {
		t.runner = c.runner;
		if (t.runner.empty() && t.cross) t.runner_note = "runner = [] : exécution désactivée";
	} else if (t.cross) {
		t.runner = auto_runner(t, t.runner_note);
	}
	return t;
}

// Compile un fichier trivial avec le compilateur et le standard demandés.
inline bool smoke_compile(const Tool& tool, const std::string& lang, const std::string& standard,
						  std::string* err_out = nullptr, bool link = true) {
	std::error_code ec;
	auto tmp = fs::temp_directory_path(ec);
	if (ec) return true; // on ne peut pas tester, on suppose que c'est OK
#ifdef __unix__
	auto dir = tmp / std::format("cpp_maker_smoke_{}_{}", ::getpid(),
								 std::hash<std::thread::id>{}(std::this_thread::get_id()));
#else
	auto dir = tmp / "cpp_maker_smoke";
#endif
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
	if (ec) return true;
	bool c = lang == "c";
	auto src = dir / (c ? "smoke.c" : "smoke.cpp");
	auto bin = dir / "smoke.bin";
	{
		std::ofstream f(src);
		if (c) f << "#include <stdio.h>\nint main(void){ printf(\"0\\n\"); return 0; }\n";
		else   f << "#include <iostream>\nint main(){ std::cout << 0 << '\\n'; return 0; }\n";
	}
	std::vector<std::string> cmd = {tool.exe};
	cmd.insert(cmd.end(), tool.pre_args.begin(), tool.pre_args.end());
	cmd.push_back("-std=" + standard);
	if (!link) cmd.push_back("-c");
	cmd.insert(cmd.end(), {src.string(), "-o", bin.string()});
	auto r = proc::capture(cmd);
	fs::remove_all(dir, ec);
	if (r.exit_code == 0) return true;
	if (err_out) *err_out = r.output;
	return false;
}

// Tronque un message de compilateur à ses N premières lignes.
inline std::string truncate(const std::string& s, size_t max_lines = 5) {
	size_t lines = 0;
	for (size_t i = 0; i < s.size(); ++i)
		if (s[i] == '\n' && ++lines >= max_lines) return s.substr(0, i);
	return s;
}

// Détection robuste pour `cpp-maker new` : le premier candidat hôte qui
// compile réellement un fichier trivial.
struct TrialResult { std::string name; std::string status; };
inline std::optional<Tool> detect_working(const std::string& lang, const std::string& family,
										  const std::string& forced, const std::string& standard,
										  std::vector<TrialResult>* log = nullptr) {
	std::vector<std::string> cands = forced.empty() || forced == "auto"
		? host_candidates(lang != "c", family) : std::vector<std::string>{forced};
	for (auto& cand : cands) {
		auto i = probe(cand);
		if (!i) { if (log) log->push_back({cand, "non trouvé"}); continue; }
		std::string err;
		if (smoke_compile(*i, lang, standard, &err)) {
			if (log) log->push_back({cand, "OK — " + i->version});
			return i;
		}
		if (log) log->push_back({cand, "compilation triviale en échec"});
	}
	return std::nullopt;
}

} // namespace toolchain

// ============================================================================
// build — graphe de tâches, exécution parallèle et incrémentale
// ============================================================================
namespace build {

enum class Lang { C, CXX, ASM };

inline std::optional<Lang> lang_of(const fs::path& p) {
	auto e = p.extension().string();
	if (e == ".c") return Lang::C;
	if (e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".c++" || e == ".C" || e == ".cp")
		return Lang::CXX;
	if (e == ".S" || e == ".s" || e == ".sx") return Lang::ASM;
	return std::nullopt;
}

struct Task {
	std::string kind;               // CC | CXX | AS | AR | LINK | SOLINK
	std::string label;              // texte affiché
	std::vector<std::string> cmd;
	std::vector<fs::path> outputs;
	std::vector<fs::path> inputs;   // entrées prises en compte dans l'empreinte
	fs::path source;                // tâches de compilation
	fs::path depfile;
	fs::path stamp;
	std::vector<size_t> deps;
	std::string tool_version;
	bool remove_outputs_first = false;
	bool is_compile() const { return !source.empty(); }
};

// Parser de fichier .d généré par -MMD
inline std::vector<fs::path> parse_depfile(const fs::path& p) {
	std::vector<fs::path> deps;
	std::string all;
	if (!util::read_file(p, all)) return deps;
	std::string joined;
	for (size_t i = 0; i < all.size(); ++i) {
		if (all[i] == '\\' && i + 1 < all.size() && all[i + 1] == '\n') { ++i; continue; }
		if (all[i] == '\\' && i + 1 < all.size() && all[i + 1] == '\r') {
			++i;
			if (i + 1 < all.size() && all[i + 1] == '\n') ++i;
			continue;
		}
		joined += all[i];
	}
	// Seule la première règle compte (-MP ajoute des cibles vides ensuite).
	auto nl = joined.find('\n');
	std::string first = joined.substr(0, nl);
	auto colon = first.find(": ");
	if (colon == std::string::npos) colon = first.find(':');
	if (colon == std::string::npos) return deps;
	// Tokenisation respectant les échappements '\'x du makefile : un chemin
	// contenant des espaces est écrit "a\ b/c.cpp" et ne doit pas être coupé.
	std::string body = first.substr(colon + 1);
	std::string tok;
	auto flush = [&]() { if (!tok.empty()) { deps.push_back(tok); tok.clear(); } };
	for (size_t i = 0; i < body.size(); ++i) {
		char c = body[i];
		if (c == '\\' && i + 1 < body.size()) { tok += body[++i]; continue; }
		if (c == '$' && i + 1 < body.size() && body[i + 1] == '$') { tok += '$'; ++i; continue; }
		if (c == ' ' || c == '\t' || c == '\r') { flush(); continue; }
		tok += c;
	}
	flush();
	return deps;
}

inline std::string fingerprint(const Task& t) {
	util::Hasher h;
	h.feed(t.tool_version);
	for (auto& a : t.cmd) h.feed(a);
	std::vector<fs::path> files = t.inputs;
	if (!t.depfile.empty()) {
		auto d = parse_depfile(t.depfile);
		if (!d.empty()) files = std::move(d);
	}
	for (auto& f : files) {
		h.feed(f.string());
		std::error_code ec;
		auto sz = fs::file_size(f, ec);
		if (ec) { h.feed("<absent>"); continue; }
		h.feed_u64((uint64_t)sz);
		auto mt = fs::last_write_time(f, ec);
		if (!ec) h.feed_u64((uint64_t)mt.time_since_epoch().count());
	}
	return h.hex();
}

inline bool up_to_date(const Task& t) {
	std::error_code ec;
	for (auto& o : t.outputs) if (!fs::exists(o, ec)) return false;
	std::string old;
	if (!util::read_file(t.stamp, old)) return false;
	return old == fingerprint(t);
}

// Tâches qui seraient exécutées (périmées, ou dépendant d'une tâche périmée).
inline std::vector<char> predict_dirty(const std::vector<Task>& tasks) {
	std::vector<char> dirty(tasks.size(), 0);
	// Les tâches sont créées dépendances d'abord : un seul passage suffit.
	for (size_t i = 0; i < tasks.size(); ++i) {
		bool d = !up_to_date(tasks[i]);
		for (auto dep : tasks[i].deps) if (dirty[dep]) d = true;
		dirty[i] = d;
	}
	return dirty;
}

struct ExecOptions {
	int jobs = 0;
	bool verbose = false;
	bool keep_going = false;
};
struct ExecStats { int ran = 0, uptodate = 0, failed = 0, skipped = 0; };

inline std::string kind_color(const std::string& k) {
	if (k == "CC" || k == "CXX" || k == "AS") return "\033[1;33m";
	if (k == "AR") return "\033[1;36m";
	return "\033[1;35m";
}

// Nombre de workers de compilation à utiliser. cpp-maker occupe déjà un
// cœur (thread principal), donc au plus ncpus - 1 workers, avec un plancher
// de 1 (mono-cœur : impossible de ne rien lancer). Si `requested > 0`
// dépasse ce budget, un avertissement est émis (une seule fois par
// exécution, pour ne pas spammer en mode `check` multi-modes) et la valeur
// est plafonnée. Un `requested` à 0 signifie « auto ».
inline int parallel_jobs(int requested = 0) {
	unsigned hc = std::thread::hardware_concurrency();
	int ncpus = hc == 0 ? 1 : (int)hc; // 0 = inconnu
	int budget = std::max(1, ncpus - 1);
	if (requested > budget) {
		static bool warned = false;
		if (!warned) {
			warned = true;
			util::warn("parallélisme demandé ({}) supérieur au budget CPU ({} cœurs au total, "
						"1 réservé à cpp-maker) : plafonné à {}", requested, ncpus, budget);
		}
		return budget;
	}
	return requested > 0 ? requested : budget;
}

inline ExecStats execute(const std::vector<Task>& tasks, const ExecOptions& o) {
	ExecStats st;
	const size_t n = tasks.size();
	if (n == 0) return st;

	auto dirty = predict_dirty(tasks);
	size_t total = (size_t)std::count(dirty.begin(), dirty.end(), 1);

	std::vector<int> pending(n, 0);
	std::vector<std::vector<size_t>> rdeps(n);
	for (size_t i = 0; i < n; ++i) {
		pending[i] = (int)tasks[i].deps.size();
		for (auto d : tasks[i].deps) rdeps[d].push_back(i);
	}
	std::deque<size_t> ready;
	for (size_t i = 0; i < n; ++i) if (pending[i] == 0) ready.push_back(i);

	std::vector<char> state(n, 0); // 0 = en attente, 1 = ok, 2 = échec/ignorée
	std::mutex mu, print_mu;
	std::condition_variable cv;
	size_t finished = 0, counter = 0;
	bool stop = false;

	int jobs = parallel_jobs(o.jobs);
	jobs = std::max(1, std::min<int>(jobs, (int)n));

	auto run_one = [&](const Task& t) -> bool {
		if (up_to_date(t)) {
			std::lock_guard lk(mu);
			++st.uptodate;
			return true;
		}
		std::error_code ec;
		for (auto& out : t.outputs) {
			if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
			if (t.remove_outputs_first) fs::remove(out, ec);
		}
		if (!t.stamp.empty() && t.stamp.has_parent_path())
			fs::create_directories(t.stamp.parent_path(), ec);
		{
			std::lock_guard lk(print_mu);
			size_t idx;
			{ std::lock_guard lk2(mu); idx = ++counter; }
			util::outln("[{}/{}] {}{:<6}\033[0m {}", idx, std::max(idx, total),
						kind_color(t.kind), t.kind, t.label);
			if (o.verbose) util::outln("      $ {}", util::shell_join(t.cmd));
		}
		auto r = proc::run(t.cmd, {.capture = true, .cwd = std::nullopt});
		bool ok = r.exit_code == 0;
		for (auto& out : t.outputs) if (ok && !fs::exists(out, ec)) ok = false;
		{
			std::lock_guard lk(print_mu);
			if (!ok) {
				util::errln("\033[1;31mÉCHEC :\033[0m {} {} (code {})", t.kind, t.label, r.exit_code);
				if (!o.verbose) util::errln("      $ {}", util::shell_join(t.cmd));
			}
			if (!r.output.empty()) {
				std::cout.flush();
				(ok ? std::cout : std::cerr) << r.output;
				if (r.output.back() != '\n') (ok ? std::cout : std::cerr) << '\n';
			}
		}
		if (!ok) { std::error_code ec2; fs::remove(t.stamp, ec2); return false; }
		if (!t.stamp.empty()) util::write_file_atomic(t.stamp, fingerprint(t));
		std::lock_guard lk(mu);
		++st.ran;
		return true;
	};

	auto worker = [&] {
		std::unique_lock lk(mu);
		for (;;) {
			cv.wait(lk, [&] { return !ready.empty() || finished == n; });
			if (ready.empty()) return;
			size_t i = ready.front();
			ready.pop_front();
			bool dep_failed = std::any_of(tasks[i].deps.begin(), tasks[i].deps.end(),
										  [&](size_t d) { return state[d] == 2; });
			bool abort = stop;
			lk.unlock();
			bool ok = false;
			if (!dep_failed && !abort) ok = run_one(tasks[i]);
			lk.lock();
			if (dep_failed || abort) ++st.skipped;
			else if (!ok) { ++st.failed; if (!o.keep_going) stop = true; }
			state[i] = ok ? 1 : 2;
			++finished;
			for (auto r : rdeps[i]) if (--pending[r] == 0) ready.push_back(r);
			cv.notify_all();
		}
	};

	std::vector<std::thread> pool;
	for (int i = 0; i < jobs; ++i) pool.emplace_back(worker);
	for (auto& th : pool) th.join();
	return st;
}

// ---------------------------------------------------------------------------
// Planification : modules → tâches
// ---------------------------------------------------------------------------
struct Source { fs::path path; Lang lang; };

struct ModulePlan {
	const ModuleConfig* cfg = nullptr;
	std::vector<Source> sources;          // hors main
	std::optional<Source> main;
	std::vector<Source> tests;
	std::vector<std::string> closure;     // dépendances transitives (dépendances d'abord)
	std::vector<std::string> pc_cflags, pc_libs;
	bool has_cxx = false, has_c = false;

	fs::path artifact, import_lib;        // exécutable / bibliothèque
	std::optional<size_t> link_task;
	std::vector<fs::path> objects;        // hors main
	std::vector<size_t> object_tasks;
	std::optional<size_t> main_task;
	fs::path main_object;
	fs::path test_exe;
	std::optional<size_t> test_task;
};

struct Plan {
	const Project* p = nullptr;
	const toolchain::Toolchain* t = nullptr;
	const ModeConfig* mode = nullptr;
	fs::path build_dir;
	std::vector<Task> tasks;
	std::vector<std::string> order;       // modules, dépendances d'abord
	std::vector<std::string> selected;
	std::map<std::string, ModulePlan> mods;
	bool with_tests = false;
};

inline fs::path build_dir_for(const std::string& tc, const std::string& mode) {
	return fs::path("build") / tc / mode;
}

// Motif simple : '*' (hors '/'), '**' (tout), '?'.
inline bool glob_match(std::string_view pat, std::string_view s) {
	if (pat.empty()) return s.empty();
	if (pat.starts_with("**")) {
		auto rest = pat.substr(2);
		if (rest.starts_with("/")) rest.remove_prefix(1);
		for (size_t i = 0; i <= s.size(); ++i) if (glob_match(rest, s.substr(i))) return true;
		return false;
	}
	if (pat[0] == '*') {
		for (size_t i = 0; i <= s.size(); ++i) {
			if (glob_match(pat.substr(1), s.substr(i))) return true;
			if (i < s.size() && s[i] == '/') break;
		}
		return false;
	}
	if (s.empty()) return false;
	if (pat[0] == '?' || pat[0] == s[0]) return glob_match(pat.substr(1), s.substr(1));
	return false;
}

inline bool excluded(const ModuleConfig& m, const fs::path& p) {
	std::string rel = (m.dir == "." ? p : p.lexically_relative(m.dir)).generic_string();
	for (auto& e : m.exclude) {
		std::string ex = fs::path(e).lexically_normal().generic_string();
		if (rel == ex || rel.starts_with(ex + "/") || glob_match(ex, rel)) return true;
	}
	return false;
}

inline std::vector<Source> discover(const ModuleConfig& m, const std::vector<std::string>& dirs) {
	std::vector<Source> out;
	for (auto& d : dirs) {
		fs::path dir = util::under(m.dir, d);
		std::error_code ec;
		if (!fs::is_directory(dir, ec)) continue;
		fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
		if (ec) continue;
		for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
			if (ec) break;
			if (!it->is_regular_file(ec) || ec) continue;
			auto l = lang_of(it->path());
			if (!l) continue;
			fs::path p = it->path().lexically_normal();
			if (!excluded(m, p)) out.push_back({p, *l});
		}
	}
	std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.path < b.path; });
	out.erase(std::unique(out.begin(), out.end(), [](auto& a, auto& b) { return a.path == b.path; }),
			  out.end());
	return out;
}

inline bool platform_matches(const std::string& key, const std::string& os) {
	if (key == os) return true;
	if (key == "unix") return os != "windows" && os != "baremetal";
	return false;
}

inline fs::path object_path(const Plan& pl, const ModuleConfig& m, const fs::path& src) {
	fs::path rel = m.dir == "." ? src : src.lexically_relative(m.dir);
	std::string s = rel.generic_string();
	if (rel.empty() || s.starts_with("..")) s = util::replace_all(src.generic_string(), "../", "__/");
	return pl.build_dir / "obj" / m.name / (s + ".o");
}

inline std::vector<std::string> pkg_query(const toolchain::Toolchain& t, const std::string& pkg,
										  const std::string& what) {
	std::vector<std::string> cmd = {t.pkg_config, what, pkg};
	bool restricted = t.cross && t.cfg.pkg_config.empty() && t.pkg_config == "pkg-config";
	if (restricted) {
		// Pas de pkg-config dédié à la cible : on limite la recherche aux .pc
		// de la cible (sysroot, multiarch Debian). Sans cela, pkg-config
		// renverrait les en-têtes et bibliothèques de l'hôte.
		std::vector<std::string> dirs;
		auto add = [&](const std::string& d) {
			std::error_code ec;
			if (fs::is_directory(d, ec)) dirs.push_back(d);
		};
		const std::string& sr = t.cfg.sysroot;
		if (!sr.empty())
			for (auto sub : {std::string("/usr/lib/pkgconfig"), "/usr/lib/" + t.triple + "/pkgconfig",
							 std::string("/usr/local/lib/pkgconfig"), std::string("/usr/share/pkgconfig")})
				add(sr + sub);
		if (!t.triple.empty()) {
			add("/usr/lib/" + t.triple + "/pkgconfig");
			add("/usr/" + t.triple + "/lib/pkgconfig");
		}
		add("/usr/share/pkgconfig");
		cmd = {"env", "PKG_CONFIG_LIBDIR=" + util::join(dirs, ":"), "PKG_CONFIG_PATH="};
		if (!sr.empty()) cmd.push_back("PKG_CONFIG_SYSROOT_DIR=" + sr);
		cmd.insert(cmd.end(), {t.pkg_config, what, pkg});
	}
	auto r = proc::capture(cmd);
	if (r.exit_code != 0) {
		std::string hint;
		if (restricted)
			hint = std::format("\n  cible {} : paquet de développement de la cible introuvable "
							   "(.pc cherchés hors de l'hôte). Installez-le pour {} (multiarch ou "
							   "sysroot), ou définissez toolchains.{}.sysroot / pkg_config.",
							   t.triple, t.arch, t.name);
		util::die(std::format("pkg-config : paquet '{}' introuvable ({} {} {}){}\n{}", pkg,
							  t.pkg_config, what, pkg, hint, util::trim(r.output)));
	}
	return util::split_ws(r.output);
}

inline std::string artifact_name(const toolchain::Toolchain& t, const ModuleConfig& m) {
	if (m.type == "executable") return m.name + t.exe_suffix();
	if (m.type == "static") return "lib" + m.name + ".a";
	if (m.type == "shared") {
		if (t.os == "windows") return m.name + ".dll";
		if (t.os == "macos") return "lib" + m.name + ".dylib";
		return "lib" + m.name + ".so";
	}
	return {};
}

class Planner {
public:
	Planner(Plan& pl) : pl_(pl), p_(*pl.p), t_(*pl.t), mode_(*pl.mode) {}

	void run() {
		pl_.order = cfg::topo_order(p_, pl_.selected);
		for (auto& n : pl_.order) prepare(n);
		for (auto& n : pl_.order) emit_module(n);
		if (pl_.with_tests)
			for (auto& n : pl_.selected) emit_tests(n);
	}

private:
	Plan& pl_;
	const Project& p_;
	const toolchain::Toolchain& t_;
	const ModeConfig& mode_;

	std::vector<const PlatformOverride*> overrides(const ModuleConfig& m) const {
		std::vector<const PlatformOverride*> r;
		for (auto& [k, o] : m.platforms) if (platform_matches(k, t_.os)) r.push_back(&o);
		return r;
	}

	void prepare(const std::string& name) {
		auto& mp = pl_.mods[name];
		const ModuleConfig& m = *p_.find_module(name);
		mp.cfg = &m;
		auto deps = cfg::topo_order(p_, {name});
		deps.pop_back();
		mp.closure = deps;

		auto srcs = discover(m, m.source_dirs);
		std::vector<std::string> files = m.source_files;
		for (auto o : overrides(m)) files.insert(files.end(), o->sources.begin(), o->sources.end());
		for (auto& f : files) {
			fs::path p = util::under(m.dir, f);
			auto l = lang_of(p);
			std::error_code ec;
			if (!fs::is_regular_file(p, ec))
				util::die(std::format("{}: source introuvable : {}", m.file.string(), p.string()));
			if (!l) util::die(std::format("{}: extension non reconnue : {}", m.file.string(), p.string()));
			if (std::none_of(srcs.begin(), srcs.end(), [&](auto& s) { return s.path == p; }))
				srcs.push_back({p, *l});
		}
		if (m.type == "header-only" && !srcs.empty())
			util::die(std::format("module '{}' de type header-only mais {} source(s) trouvée(s) "
								  "dans {}", name, srcs.size(), util::join(m.source_dirs, ", ")));
		if (m.type != "header-only" && srcs.empty())
			util::die(std::format("module '{}' ({}) : aucune source C/C++ dans {}", name,
								  m.file.string(), util::join(m.source_dirs, ", ")));

		fs::path main_path;
		if (!m.main.empty()) {
			main_path = util::under(m.dir, m.main);
			if (std::none_of(srcs.begin(), srcs.end(), [&](auto& s) { return s.path == main_path; }))
				util::die(std::format("{}: main = '{}' ne fait pas partie des sources",
									  m.file.string(), m.main));
		} else if (m.type == "executable") {
			for (auto& s : srcs) if (s.path.stem() == "main") { main_path = s.path; break; }
			// v0.6 : sinon, la source qui définit main() (exclue des tests).
			if (main_path.empty()) {
				static const std::regex main_re(R"((^|\n)\s*int\s+main\s*\()");
				for (auto& s : srcs) {
					std::ifstream in(s.path);
					std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
					if (std::regex_search(text, main_re)) { main_path = s.path; break; }
				}
			}
		}
		for (auto& s : srcs) {
			if (s.lang == Lang::CXX) mp.has_cxx = true;
			if (s.lang == Lang::C) mp.has_c = true;
			if (!main_path.empty() && s.path == main_path) mp.main = s;
			else mp.sources.push_back(s);
		}
		if (pl_.with_tests && std::find(pl_.selected.begin(), pl_.selected.end(), name) != pl_.selected.end())
			mp.tests = discover(m, m.test_dirs);

		for (auto& pkg : m.pkg_config) {
			auto cf = pkg_query(t_, pkg, "--cflags");
			auto lf = pkg_query(t_, pkg, "--libs");
			mp.pc_cflags.insert(mp.pc_cflags.end(), cf.begin(), cf.end());
			mp.pc_libs.insert(mp.pc_libs.end(), lf.begin(), lf.end());
		}

		std::string an = artifact_name(t_, m);
		if (m.type == "executable") mp.artifact = pl_.build_dir / "bin" / an;
		else if (m.type == "static") mp.artifact = pl_.build_dir / "lib" / an;
		else if (m.type == "shared") {
			if (t_.os == "windows") {
				mp.artifact = pl_.build_dir / "bin" / an;
				mp.import_lib = pl_.build_dir / "lib" / ("lib" + m.name + ".dll.a");
			} else {
				mp.artifact = pl_.build_dir / "lib" / an;
			}
		}
	}

	bool closure_uses_cxx(const ModulePlan& mp, bool tests) const {
		if (mp.has_cxx) return true;
		if (tests && std::any_of(mp.tests.begin(), mp.tests.end(),
								 [](auto& s) { return s.lang == Lang::CXX; })) return true;
		for (auto& d : mp.closure) if (pl_.mods.at(d).has_cxx) return true;
		return false;
	}

	bool pic(const ModuleConfig& m) const {
		if (t_.os == "windows") return false;
		return m.type == "shared" || (m.type == "static" && p_.static_pic);
	}

	std::vector<std::string> compile_flags(const ModulePlan& mp, Lang lang, bool tests) const {
		const ModuleConfig& m = *mp.cfg;
		const auto& tool = lang == Lang::CXX ? t_.cxx_tool() : t_.c_tool();
		std::vector<std::string> c = {tool.exe};
		c.insert(c.end(), tool.pre_args.begin(), tool.pre_args.end());
		if (lang == Lang::CXX)
			c.push_back("-std=" + (m.cxx_standard.empty() ? p_.cxx_standard : m.cxx_standard));
		else if (lang == Lang::C)
			c.push_back("-std=" + (m.c_standard.empty() ? p_.c_standard : m.c_standard));
		auto add = [&](const std::vector<std::string>& v) { c.insert(c.end(), v.begin(), v.end()); };
		add(mode_.flags);
		if (lang != Lang::ASM) add(p_.warnings);
		add(lang == Lang::CXX ? t_.cfg.cxxflags : t_.cfg.cflags);
		add(lang == Lang::CXX ? p_.cxxflags : p_.cflags);
		if (lang != Lang::ASM) add(lang == Lang::CXX ? m.cxxflags : m.cflags);
		auto ovs = overrides(m);
		for (auto o : ovs) if (lang != Lang::ASM) add(lang == Lang::CXX ? o->cxxflags : o->cflags);
		if (pic(m)) c.push_back("-fPIC");

		auto def = [&](const std::vector<std::string>& v) { for (auto& d : v) c.push_back("-D" + d); };
		def(p_.defines);
		def(mode_.defines);
		def(m.defines);
		def(m.public_defines);
		for (auto o : ovs) def(o->defines);
		for (auto& d : mp.closure) def(p_.find_module(d)->public_defines);

		auto inc = [&](const ModuleConfig& mm, const std::vector<std::string>& v) {
			for (auto& i : v) c.push_back("-I" + util::under(mm.dir, i).string());
		};
		inc(m, m.public_includes);
		inc(m, m.private_includes);
		if (tests) inc(m, m.test_dirs);
		for (auto& d : mp.closure) inc(*p_.find_module(d), p_.find_module(d)->public_includes);
		add(mp.pc_cflags);
		for (auto& d : mp.closure) add(pl_.mods.at(d).pc_cflags);
		return c;
	}

	size_t add_compile(const ModulePlan& mp, const Source& s, bool tests, fs::path& obj_out) {
		Task t;
		t.source = s.path;
		obj_out = object_path(pl_, *mp.cfg, s.path);
		t.outputs = {obj_out};
		t.depfile = fs::path(obj_out.string() + ".d");
		t.stamp = fs::path(obj_out.string() + ".stamp");
		t.inputs = {s.path};
		t.cmd = compile_flags(mp, s.lang, tests);
		t.tool_version = (s.lang == Lang::CXX ? t_.cxx_tool() : t_.c_tool()).version;
		t.cmd.insert(t.cmd.end(), {"-MMD", "-MP", "-MF", t.depfile.string(),
								   "-c", s.path.string(), "-o", obj_out.string()});
		t.kind = s.lang == Lang::CXX ? "CXX" : s.lang == Lang::C ? "CC" : "AS";
		t.label = s.path.string();
		pl_.tasks.push_back(std::move(t));
		return pl_.tasks.size() - 1;
	}

	// Bibliothèques des dépendances, pkg-config et système, dans l'ordre de lien.
	void link_libs(const ModulePlan& mp, std::vector<std::string>& cmd, std::vector<fs::path>& inputs,
				   std::vector<size_t>& deps, bool& needs_rpath) const {
		const ModuleConfig& m = *mp.cfg;
		for (auto it = mp.closure.rbegin(); it != mp.closure.rend(); ++it) {
			auto& dp = pl_.mods.at(*it);
			if (dp.cfg->type == "header-only") continue;
			fs::path lib = dp.import_lib.empty() ? dp.artifact : dp.import_lib;
			cmd.push_back(lib.string());
			inputs.push_back(lib);
			if (dp.link_task) deps.push_back(*dp.link_task);
			if (dp.cfg->type == "shared") needs_rpath = true;
		}
		cmd.insert(cmd.end(), mp.pc_libs.begin(), mp.pc_libs.end());
		for (auto& d : mp.closure) {
			auto& dl = pl_.mods.at(d).pc_libs;
			cmd.insert(cmd.end(), dl.begin(), dl.end());
		}
		std::vector<std::string> sys = m.system_libs;
		for (auto o : overrides(m)) sys.insert(sys.end(), o->system_libs.begin(), o->system_libs.end());
		for (auto& d : mp.closure) {
			auto& dm = *p_.find_module(d);
			sys.insert(sys.end(), dm.system_libs.begin(), dm.system_libs.end());
			for (auto o : overrides(dm)) sys.insert(sys.end(), o->system_libs.begin(), o->system_libs.end());
		}
		std::set<std::string> seen;
		for (auto& l : sys) {
			if (!seen.insert(l).second) continue;
			cmd.push_back(l.starts_with("-") ? l : "-l" + l);
		}
	}

	std::vector<std::string> link_prefix(const ModulePlan& mp, bool tests) const {
		const auto& tool = closure_uses_cxx(mp, tests) ? t_.cxx_tool()
						 : (t_.cc ? *t_.cc : t_.cxx_tool());
		std::vector<std::string> c = {tool.exe};
		c.insert(c.end(), tool.pre_args.begin(), tool.pre_args.end());
		c.insert(c.end(), mode_.link_flags.begin(), mode_.link_flags.end());
		c.insert(c.end(), t_.cfg.ldflags.begin(), t_.cfg.ldflags.end());
		c.insert(c.end(), p_.ldflags.begin(), p_.ldflags.end());
		c.insert(c.end(), mp.cfg->ldflags.begin(), mp.cfg->ldflags.end());
		for (auto o : overrides(*mp.cfg)) c.insert(c.end(), o->ldflags.begin(), o->ldflags.end());
		return c;
	}

	void add_rpath(std::vector<std::string>& cmd) const {
		if (t_.os == "windows" || t_.os == "baremetal") return;
		if (t_.os == "macos") cmd.push_back("-Wl,-rpath,@loader_path/../lib");
		else cmd.push_back("-Wl,-rpath,$ORIGIN/../lib");
	}

	void emit_module(const std::string& name) {
		auto& mp = pl_.mods[name];
		const ModuleConfig& m = *mp.cfg;
		for (auto& s : mp.sources) {
			fs::path obj;
			mp.object_tasks.push_back(add_compile(mp, s, false, obj));
			mp.objects.push_back(obj);
		}
		if (mp.main) mp.main_task = add_compile(mp, *mp.main, false, mp.main_object);
		if (m.type == "header-only") return;

		Task t;
		t.outputs = {mp.artifact};
		t.stamp = pl_.build_dir / "stamps" / (m.name + ".link.stamp");
		t.label = mp.artifact.string();
		std::vector<fs::path> objs = mp.objects;
		std::vector<size_t> deps = mp.object_tasks;
		if (mp.main) { objs.push_back(mp.main_object); deps.push_back(*mp.main_task); }

		if (m.type == "static") {
			t.kind = "AR";
			t.cmd = {t_.ar, "rcs", mp.artifact.string()};
			for (auto& o : objs) t.cmd.push_back(o.string());
			t.inputs = objs;
			t.remove_outputs_first = true;
			t.tool_version = t_.ar;
		} else {
			t.kind = m.type == "shared" ? "SOLINK" : "LINK";
			t.cmd = link_prefix(mp, false);
			if (m.type == "shared") {
				t.cmd.push_back(t_.os == "macos" ? "-dynamiclib" : "-shared");
				if (t_.os == "windows") {
					t.cmd.push_back("-Wl,--out-implib," + mp.import_lib.string());
					t.outputs.push_back(mp.import_lib);
				} else if (t_.os == "macos") {
					t.cmd.push_back("-Wl,-install_name,@rpath/" + mp.artifact.filename().string());
				} else {
					t.cmd.push_back("-Wl,-soname," + mp.artifact.filename().string());
				}
			}
			for (auto& o : objs) t.cmd.push_back(o.string());
			t.inputs = objs;
			bool rpath = false;
			link_libs(mp, t.cmd, t.inputs, deps, rpath);
			if (rpath) add_rpath(t.cmd);
			t.cmd.push_back("-o");
			t.cmd.push_back(mp.artifact.string());
			t.tool_version = (closure_uses_cxx(mp, false) ? t_.cxx_tool() : t_.cc ? *t_.cc : t_.cxx_tool()).version;
		}
		t.deps = deps;
		pl_.tasks.push_back(std::move(t));
		mp.link_task = pl_.tasks.size() - 1;
	}

	void emit_tests(const std::string& name) {
		auto& mp = pl_.mods[name];
		const ModuleConfig& m = *mp.cfg;
		if (mp.tests.empty()) return;
		std::vector<fs::path> objs;
		std::vector<size_t> deps;
		for (auto& s : mp.tests) {
			fs::path obj;
			deps.push_back(add_compile(mp, s, true, obj));
			objs.push_back(obj);
		}
		Task t;
		t.kind = "LINK";
		mp.test_exe = pl_.build_dir / "tests" / (m.name + "-tests" + t_.exe_suffix());
		t.outputs = {mp.test_exe};
		t.stamp = pl_.build_dir / "stamps" / (m.name + ".tests.stamp");
		t.label = mp.test_exe.string();
		t.cmd = link_prefix(mp, true);
		for (auto& o : objs) t.cmd.push_back(o.string());
		t.inputs = objs;
		bool rpath = false;
		if (m.type == "executable") {
			// Les objets du programme, sauf celui qui contient main().
			for (auto& o : mp.objects) { t.cmd.push_back(o.string()); t.inputs.push_back(o); }
			deps.insert(deps.end(), mp.object_tasks.begin(), mp.object_tasks.end());
		} else if (m.type != "header-only") {
			fs::path lib = mp.import_lib.empty() ? mp.artifact : mp.import_lib;
			t.cmd.push_back(lib.string());
			t.inputs.push_back(lib);
			deps.push_back(*mp.link_task);
			if (m.type == "shared") rpath = true;
		}
		link_libs(mp, t.cmd, t.inputs, deps, rpath);
		if (rpath) add_rpath(t.cmd);
		t.cmd.push_back("-o");
		t.cmd.push_back(mp.test_exe.string());
		t.tool_version = (closure_uses_cxx(mp, true) ? t_.cxx_tool() : t_.cc ? *t_.cc : t_.cxx_tool()).version;
		t.deps = deps;
		pl_.tasks.push_back(std::move(t));
		mp.test_task = pl_.tasks.size() - 1;
	}
};

inline Plan make_plan(const Project& p, const toolchain::Toolchain& t, const ModeConfig& mode,
					  const std::vector<std::string>& selected, bool with_tests) {
	Plan pl;
	pl.p = &p;
	pl.t = &t;
	pl.mode = &mode;
	pl.build_dir = build_dir_for(t.name, mode.name);
	pl.selected = selected;
	pl.with_tests = with_tests;
	Planner(pl).run();
	return pl;
}

// ---------------------------------------------------------------------------
// compile_commands.json
// ---------------------------------------------------------------------------
inline void write_compdb(const Plan& pl, const fs::path& out) {
	std::string root = fs::current_path().string();
	std::string s = "[\n";
	bool first = true;
	for (auto& t : pl.tasks) {
		if (!t.is_compile()) continue;
		if (!first) s += ",\n";
		first = false;
		s += "  {\n    \"directory\": \"" + util::json_escape(root) + "\",\n    \"arguments\": [";
		for (size_t i = 0; i < t.cmd.size(); ++i) {
			if (i) s += ", ";
			s += "\"" + util::json_escape(t.cmd[i]) + "\"";
		}
		s += "],\n    \"file\": \"" + util::json_escape(t.source.string()) + "\",\n";
		s += "    \"output\": \"" + util::json_escape(t.outputs[0].string()) + "\"\n  }";
	}
	s += "\n]\n";
	std::error_code ec;
	fs::create_directories(out.parent_path(), ec);
	util::write_file_atomic(out, s);
}

// ---------------------------------------------------------------------------
// build.ninja (exécuté depuis la racine : ninja -f build/<tc>/<mode>/build.ninja)
// ---------------------------------------------------------------------------
inline std::string ninja_path(const std::string& s) {
	std::string r;
	for (char c : s) {
		if (c == '$' || c == ' ' || c == ':') r += '$';
		if (c == '\n') continue;
		r += c;
	}
	return r;
}
inline std::string ninja_value(const std::string& s) {
	return util::replace_all(util::replace_all(s, "$", "$$"), "\n", " ");
}

inline fs::path write_ninja(const Plan& pl) {
	std::string s;
	s += "# Généré par cpp-maker " + std::string(CPP_MAKER_VERSION) + " — ne pas modifier.\n";
	s += std::format("# Toolchain : {}   Mode : {}\n", pl.t->name, pl.mode->name);
	s += "# Usage (depuis la racine du projet) : ninja -f " + (pl.build_dir / "build.ninja").string() + "\n\n";
	s += "ninja_required_version = 1.3\n";
	s += "builddir = " + ninja_path(pl.build_dir.string()) + "\n\n";
	s += "rule compile\n  command = $cmd\n  description = $kind $label\n  depfile = $depfile\n  deps = gcc\n\n";
	s += "rule run\n  command = $cmd\n  description = $kind $label\n\n";
	std::vector<std::string> all;
	for (auto& t : pl.tasks) {
		std::string outs, ins, implicit;
		for (auto& o : t.outputs) outs += " " + ninja_path(o.string());
		for (auto& i : t.inputs) ins += " " + ninja_path(i.string());
		std::set<std::string> in_set;
		for (auto& i : t.inputs) in_set.insert(i.string());
		for (auto d : t.deps)
			for (auto& o : pl.tasks[d].outputs)
				if (!in_set.count(o.string())) implicit += " " + ninja_path(o.string());
		std::string cmd = util::shell_join(t.cmd);
		if (t.remove_outputs_first) cmd = "rm -f " + util::shell_quote(t.outputs[0].string()) + " && " + cmd;
		s += std::format("build{}: {}{}{}\n", outs, t.is_compile() ? "compile" : "run", ins,
						 implicit.empty() ? "" : " |" + implicit);
		s += "  cmd = " + ninja_value(cmd) + "\n";
		s += "  kind = " + t.kind + "\n";
		s += "  label = " + ninja_value(t.label) + "\n";
		if (t.is_compile()) s += "  depfile = " + ninja_value(t.depfile.string()) + "\n";
		s += "\n";
		if (!t.is_compile()) for (auto& o : t.outputs) all.push_back(ninja_path(o.string()));
	}
	if (all.empty())
		for (auto& t : pl.tasks) for (auto& o : t.outputs) all.push_back(ninja_path(o.string()));
	s += "build all: phony " + util::join(all, " ") + "\n";
	s += "default all\n";
	fs::path out = pl.build_dir / "build.ninja";
	std::error_code ec;
	fs::create_directories(pl.build_dir, ec);
	util::write_file_atomic(out, s);
	return out;
}

} // namespace build

// ============================================================================
// elf — lecture autonome des binaires : en-tête ELF + tailles Berkeley
// (équivalent de `size`, sans dépendance à binutils : les binaires croisés
// sont analysables même sans binutils de la cible installé)
// ============================================================================
namespace elf {

struct Section {
	std::string name;
	uint64_t size = 0;
	uint64_t flags = 0;
};

struct Info {
	bool ok = false;              // analysé (ELF ou archive d'objets)
	bool elf = false;             // un seul fichier ELF
	std::string note;             // raison si non analysable
	std::string cls;              // ELF32 / ELF64
	std::string endian;           // little / big endian
	std::string type;             // REL / EXEC / DYN / CORE
	std::string machine;          // x86-64, ARM, AArch64…
	std::string abi;              // OS/ABI
	uint64_t entry = 0;
	uint64_t text = 0, data = 0, bss = 0;
	uint64_t file_size = 0;
	int members = 0;              // archive ar : membres ELF sommés
	std::vector<Section> sections;
	uint64_t dec() const { return text + data + bss; }
};

inline uint16_t rd16(const std::string& b, size_t o, bool be) {
	if (o + 2 > b.size()) return 0;
	auto u = (unsigned char)b[o];
	auto v = (unsigned char)b[o + 1];
	return be ? (uint16_t)((u << 8) | v) : (uint16_t)((v << 8) | u);
}
inline uint32_t rd32(const std::string& b, size_t o, bool be) {
	if (o + 4 > b.size()) return 0;
	uint32_t r = 0;
	for (size_t i = 0; i < 4; ++i)
		r = (r << 8) | (unsigned char)b[o + (be ? i : size_t(3) - i)];
	return r;
}
inline uint64_t rd64(const std::string& b, size_t o, bool be) {
	if (o + 8 > b.size()) return 0;
	uint64_t r = 0;
	for (size_t i = 0; i < 8; ++i)
		r = (r << 8) | (unsigned char)b[o + (be ? i : size_t(7) - i)];
	return r;
}

inline const char* machine_name(uint16_t m) {
	switch (m) {
		case 3:   return "x86";
		case 8:   return "MIPS";
		case 20:  return "PowerPC";
		case 21:  return "PowerPC64";
		case 40:  return "ARM";
		case 50:  return "IA-64";
		case 62:  return "x86-64";
		case 183: return "AArch64";
		case 243: return "RISC-V";
		case 247: return "LoongArch";
		default:  return nullptr;
	}
}

inline const char* abi_name(unsigned char a) {
	switch (a) {
		case 0:  return "UNIX System V";
		case 2:  return "HP-UX";
		case 3:  return "Linux";
		case 6:  return "Solaris";
		case 9:  return "FreeBSD";
		case 12: return "OpenBSD";
		default: return "inconnu";
	}
}

// Analyse un fichier ELF dans buf à l'offset `off` (début d'en-tête).
// Remplit in ; retourne false si ce n'est pas un ELF.
inline bool parse_at(const std::string& buf, size_t off, Info& in) {
	if (off + 16 > buf.size()) return false;
	if ((unsigned char)buf[off] != 0x7f || buf[off + 1] != 'E' ||
		buf[off + 2] != 'L' || buf[off + 3] != 'F') return false;
	bool is64 = (unsigned char)buf[off + 4] == 2;
	bool be = (unsigned char)buf[off + 5] == 2;
	in.elf = true;
	in.cls = is64 ? "ELF64" : "ELF32";
	in.endian = be ? "big endian" : "little endian";
	in.abi = abi_name((unsigned char)buf[off + 7]);

	uint16_t e_type = rd16(buf, off + 16, be);
	uint16_t e_machine = rd16(buf, off + 18, be);
	switch (e_type) {
		case 1: in.type = "REL (relocalisable)"; break;
		case 2: in.type = "EXEC (exécutable)"; break;
		case 3: in.type = "DYN (position indépendante — PIE ou .so)"; break;
		case 4: in.type = "CORE (core dump)"; break;
		default: in.type = std::format("type {}", e_type); break;
	}
	if (auto* n = machine_name(e_machine)) in.machine = n;
	else in.machine = std::format("machine {}", e_machine);

	uint64_t shoff;
	uint16_t shentsize, shnum, shstrndx;
	if (is64) {
		in.entry = rd64(buf, off + 24, be);
		shoff = rd64(buf, off + 40, be);
		shentsize = rd16(buf, off + 58, be);
		shnum = rd16(buf, off + 60, be);
		shstrndx = rd16(buf, off + 62, be);
	} else {
		in.entry = rd32(buf, off + 24, be);
		shoff = rd32(buf, off + 32, be);
		shentsize = rd16(buf, off + 46, be);
		shnum = rd16(buf, off + 48, be);
		shstrndx = rd16(buf, off + 50, be);
	}
	in.ok = true;
	// Les offsets de l'en-tête sont relatifs au début de CE fichier ELF : pour
	// un membre d'archive ar, il faut leur ajouter `off`.
	if (shoff == 0 || shnum == 0 || shentsize == 0 || off + shoff > buf.size()) return true;

	// En-tête de section i → type, drapeaux, offset, taille. Retourne false
	// hors bornes ; sinon l'offset du nom (0 = sans nom).
	auto section_at = [&](uint16_t i, uint32_t& name_off, uint32_t& sh_type,
						  uint64_t& sh_flags, uint64_t& sh_offset, uint64_t& sh_size) {
		size_t h = off + (size_t)shoff + (size_t)i * shentsize;
		if (h + shentsize > buf.size()) return false;
		name_off = rd32(buf, h, be);
		sh_type = rd32(buf, h + 4, be);
		sh_flags = is64 ? rd64(buf, h + 8, be) : rd32(buf, h + 8, be);
		sh_offset = is64 ? rd64(buf, h + 24, be) : rd32(buf, h + 16, be);
		sh_size = is64 ? rd64(buf, h + 32, be) : rd32(buf, h + 20, be);
		return true;
	};
	auto section_name = [&](uint64_t strtab_off, uint32_t name_off) {
		size_t p = (size_t)strtab_off + name_off;
		if (p >= buf.size()) return std::string{};
		size_t e = buf.find('\0', p);
		if (e == std::string::npos) e = buf.size();
		return buf.substr(p, e - p);
	};

	// Table des noms de sections (section shstrndx).
	uint64_t strtab = 0;
	{
		uint32_t n, t; uint64_t f, o, s;
		if (shstrndx < shnum && section_at(shstrndx, n, t, f, o, s)) strtab = off + o;
	}

	constexpr uint32_t SHT_NOBITS = 8;
	constexpr uint64_t SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXECINSTR = 4;
	for (uint16_t i = 0; i < shnum; ++i) {
		uint32_t name_off = 0, sh_type = 0;
		uint64_t sh_flags = 0, sh_offset = 0, sh_size = 0;
		if (!section_at(i, name_off, sh_type, sh_flags, sh_offset, sh_size)) continue;
		(void)sh_offset;
		if (!(sh_flags & SHF_ALLOC)) continue;
		// Tailles « Berkeley » (comme `size`) : .rodata compte dans text.
		if (sh_type == SHT_NOBITS) in.bss += sh_size;
		else if (sh_flags & SHF_EXECINSTR || !(sh_flags & SHF_WRITE)) in.text += sh_size;
		else in.data += sh_size;
		in.sections.push_back({section_name(strtab, name_off), sh_size, sh_flags});
	}
	return true;
}

// Nom de la section de sortie qu'utilisera l'éditeur de liens pour une
// section d'objet : .text._Z… (-ffunction-sections, COMDAT) → .text, etc.
inline std::string output_section(const std::string& n) {
	for (auto base : {".text", ".rodata", ".data.rel.ro", ".data", ".bss", ".tbss", ".tdata",
					  ".init_array", ".fini_array", ".gcc_except_table", ".ARM.extab", ".ARM.exidx"})
		if (n == base || n.starts_with(std::string(base) + "."))
			return base;
	return n;
}

// Analyse un fichier : ELF direct, ou archive ar (.a) dont les objets sont
// sommés (text/data/bss de l'ensemble des membres ELF).
inline Info read(const fs::path& p) {
	Info in;
	std::string buf;
	if (!util::read_file(p, buf)) { in.note = "illisible"; return in; }
	in.file_size = buf.size();
	if (parse_at(buf, 0, in)) return in;   // ELF simple
	if (buf.starts_with("!<arch>\n")) {
		Info acc;
		acc.file_size = in.file_size;
		size_t pos = 8;
		while (pos + 60 <= buf.size()) {
			if (buf[pos + 58] != '`' || buf[pos + 59] != '\n') break;   // fin d'en-tête
			long sz = 0;
			bool digits = false;
			for (size_t k = 48; k < 58; ++k) {
				char ch = buf[pos + k];
				if (ch == ' ') continue;
				if (ch < '0' || ch > '9') break;
				sz = sz * 10 + (ch - '0');
				digits = true;
			}
			if (!digits) break;
			size_t data = pos + 60;
			if (sz < 0 || data + (size_t)sz > buf.size()) break;
			Info m;
			if (parse_at(buf, data, m)) {
				++acc.members;
				acc.text += m.text;
				acc.data += m.data;
				acc.bss += m.bss;
				// Sections cumulées par nom sur l'ensemble des membres (-v).
				for (auto sec : m.sections) {
					sec.name = output_section(sec.name);
					auto it = std::find_if(acc.sections.begin(), acc.sections.end(),
										   [&](auto& x) { return x.name == sec.name; });
					if (it == acc.sections.end()) acc.sections.push_back(sec);
					else { it->size += sec.size; it->flags |= sec.flags; }
				}
			}
			pos = data + (size_t)sz + ((size_t)sz & 1);   // alignement pair
		}
		if (acc.members > 0) { acc.ok = true; return acc; }
		in.note = "archive ar sans membre ELF";
		return in;
	}
	in.note = "pas un fichier ELF (PE/Windows, binaire brut…)";
	return in;
}

} // namespace elf

// ============================================================================
// commandes
// ============================================================================
namespace commands {

// ---------------------------------------------------------------------------
// Contexte : racine du projet, configuration, module courant
// ---------------------------------------------------------------------------
struct Ctx {
	Project p;
	std::string cwd_module;   // module dont le dossier contient le répertoire courant
};

inline std::vector<int> parse_semver(const std::string& s) {
	std::vector<int> v;
	for (auto& part : util::split_list(s, '.')) {
		int x = 0;
		for (char c : part) { if (!std::isdigit((unsigned char)c)) break; x = x * 10 + (c - '0'); }
		v.push_back(x);
	}
	while (v.size() < 3) v.push_back(0);
	return v;
}

inline std::optional<fs::path> find_root(const fs::path& from) {
	fs::path p = from;
	for (int i = 0; i < 64; ++i) {
		std::error_code ec;
		if (fs::exists(p / "cpp-maker.toml", ec)) return p;
		if (!p.has_parent_path() || p.parent_path() == p) break;
		p = p.parent_path();
	}
	return std::nullopt;
}

inline Ctx load_ctx(bool validate = true) {
	fs::path cwd = fs::current_path();
	fs::path root;
	if (auto r = find_root(cwd)) root = *r;
	else if (fs::exists(cwd / "src")) root = cwd; // projet v0.3 sans cpp-maker.toml
	else util::die(std::format("aucun cpp-maker.toml dans {} ni ses parents "
							   "(cpp-maker new <nom> pour créer un projet)", cwd.string()));
	// Tous les chemins internes sont relatifs à la racine.
	fs::current_path(root);
	Ctx c;
	try {
		c.p = cfg::load(root, "cpp-maker.toml");
	} catch (const std::exception& e) {
		util::die(e.what());
	}
	if (!c.p.min_version.empty() &&
		parse_semver(c.p.min_version) > parse_semver(std::string(CPP_MAKER_VERSION)))
		util::die(std::format("ce projet requiert cpp-maker ≥ {} (version actuelle {})",
							  c.p.min_version, CPP_MAKER_VERSION));
	if (validate) {
		auto e = cfg::validate_errors(c.p);
		if (!e.empty()) util::die(e);
	}
	if (c.p.workspace) {
		std::string rel = cwd.lexically_relative(root).generic_string();
		size_t best = 0;
		for (auto& m : c.p.modules) {
			std::string d = m.dir.generic_string();
			if ((rel == d || rel.starts_with(d + "/")) && d.size() > best) {
				best = d.size();
				c.cwd_module = m.name;
			}
		}
	}
	return c;
}

// ---------------------------------------------------------------------------
// Options communes
// ---------------------------------------------------------------------------
struct Opts {
	std::string mode;
	std::vector<std::string> modules, toolchains;
	std::string cc, cxx;
	int jobs = 0;
	bool verbose = false, dry_run = false, keep_going = false, all = false, clean = false;
	std::vector<std::string> positional, passthrough;
	// passthrough : arguments bruts situés après `--args` (ou `--`).
	// Transmis à l'exécutable par run/test/check ; ignorés ailleurs.
	std::map<std::string, std::string> values;  // options spécifiques à valeur
	std::set<std::string> flags;                 // options spécifiques booléennes

	std::string value(const std::string& k, const std::string& def = {}) const {
		auto it = values.find(k);
		return it == values.end() ? def : it->second;
	}
	bool flag(const std::string& k) const { return flags.count(k) > 0; }
};

// Analyse robuste d'un entier passé en option (die() avec message clair).
inline int parse_int_arg(const std::string& opt, const std::string& s) {
	try {
		size_t pos = 0;
		int v = std::stoi(s, &pos);
		if (pos != s.size()) throw std::invalid_argument(s);
		return v;
	} catch (const std::exception&) {
		util::die(std::format("valeur invalide pour {} : '{}'", opt, s));
	}
}

inline Opts parse_opts(const std::vector<std::string>& args,
					   const std::set<std::string>& value_opts = {},
					   const std::set<std::string>& bool_opts = {}) {
	Opts o;
	for (size_t i = 0; i < args.size(); ++i) {
		std::string a = args[i];
		// « --args » et « -- » sont deux séparateurs équivalents : tout ce qui les
		// suit est transmis tel quel à l'exécutable (run/test/check), sans être
		// interprété par cpp-maker. C'est la seule façon de passer des options qui
		// ressemblent à celles de cpp-maker (--world=iles, --verbose…).
		if (a == "--" || a == "--args") {
			o.passthrough.assign(args.begin() + (long)i + 1, args.end());
			break;
		}
		std::string val;
		bool has_eq = false;
		if (a.starts_with("--") && a.find('=') != std::string::npos) {
			val = a.substr(a.find('=') + 1);
			a = a.substr(0, a.find('='));
			has_eq = true;
		}
		auto take = [&]() -> std::string {
			if (has_eq) return val;
			if (i + 1 >= args.size()) util::die("valeur manquante pour " + a);
			return args[++i];
		};
		if (a == "-m" || a == "--module") {
			for (auto& m : util::split_list(take())) o.modules.push_back(m);
		} else if (a == "-t" || a == "--toolchain") {
			for (auto& t : util::split_list(take())) o.toolchains.push_back(t);
		} else if (a == "--cc") o.cc = take();
		else if (a == "--cxx") o.cxx = take();
		else if (a == "--mode") o.mode = take();
		else if (a == "-j" || a == "--jobs") o.jobs = parse_int_arg("--jobs", take());
		else if (a.starts_with("-j") && a.size() > 2 && std::isdigit((unsigned char)a[2]))
			o.jobs = parse_int_arg("-j", a.substr(2));
		else if (a == "-v" || a == "--verbose") o.verbose = true;
		else if (a == "-n" || a == "--dry-run") o.dry_run = true;
		else if (a == "-k" || a == "--keep-going") o.keep_going = true;
		else if (a == "-a" || a == "--all") o.all = true;
		else if (a == "--clean") o.clean = true;
		else if (value_opts.count(a)) o.values[a] = take();
		else if (bool_opts.count(a)) o.flags.insert(a);
		else if (a.starts_with("-") && a != "-") util::die("option inconnue : " + a);
		else o.positional.push_back(a);
	}
	if (o.jobs < 0) util::die("--jobs doit être ≥ 0");
	return o;
}

inline std::string pick_mode(const Ctx& c, const Opts& o, const std::string& def = {}) {
	std::string m = !o.mode.empty() ? o.mode : !def.empty() ? def : c.p.default_mode;
	if (!c.p.find_mode(m)) {
		std::vector<std::string> names;
		for (auto& x : c.p.modes) names.push_back(x.name);
		util::die(std::format("mode inconnu : '{}' (modes : {})", m, util::join(names, ", ")));
	}
	return m;
}

// Mode passé en premier argument positionnel : "cpp-maker build release".
inline void mode_from_positional(Opts& o) {
	if (o.mode.empty() && !o.positional.empty()) {
		o.mode = o.positional.front();
		o.positional.erase(o.positional.begin());
	}
}

inline std::vector<std::string> pick_toolchains(const Ctx& c, const Opts& o, const std::string& def = {}) {
	std::vector<std::string> r;
	for (auto& t : o.toolchains) {
		if (t == "all") {
			for (auto& [n, tc] : c.p.toolchains) if (tc.from_config || n == "host") r.push_back(n);
			if (r.empty()) r.push_back("host");
		} else {
			r.push_back(t);
		}
	}
	if (r.empty()) r.push_back(!def.empty() ? def : c.p.default_toolchain);
	std::vector<std::string> uniq;
	for (auto& x : r) if (std::find(uniq.begin(), uniq.end(), x) == uniq.end()) uniq.push_back(x);
	return uniq;
}

inline std::vector<std::string> select_modules(const Ctx& c, const Opts& o) {
	std::vector<std::string> r;
	if (!o.modules.empty()) {
		for (auto& m : o.modules) {
			if (!c.p.find_module(m)) {
				std::vector<std::string> names;
				for (auto& x : c.p.modules) names.push_back(x.name);
				util::die(std::format("module inconnu : '{}' (modules : {})", m, util::join(names, ", ")));
			}
			r.push_back(m);
		}
		return r;
	}
	if (!o.all && !c.cwd_module.empty()) {
		util::outln("\033[2m(module courant : {} — utilisez --all pour tout le workspace)\033[0m",
					c.cwd_module);
		return {c.cwd_module};
	}
	for (auto& m : c.p.modules) r.push_back(m.name);
	return r;
}

// ---------------------------------------------------------------------------
// Session de build : toolchain résolue + plan
// ---------------------------------------------------------------------------
struct Session {
	std::unique_ptr<toolchain::Toolchain> tc;
	std::unique_ptr<build::Plan> plan;
	util::FileLock lock;
	const ModeConfig* mode = nullptr;
};

inline Session prepare(const Ctx& c, const Opts& o, const std::string& tc_name,
					   const std::string& mode, const std::vector<std::string>& modules,
					   bool tests, bool lock = true) {           // ← param `lock`
	Session s;
	s.tc = std::make_unique<toolchain::Toolchain>(toolchain::resolve(c.p, tc_name, o.cc, o.cxx));
	s.mode = c.p.find_mode(mode);
	if (!s.mode) util::die("mode inconnu : " + mode);
	try {
		s.plan = std::make_unique<build::Plan>(build::make_plan(c.p, *s.tc, *s.mode, modules, tests));
	} catch (const cfg::ConfigError& e) {
		util::die(e.what());
	}
	// Sérialise avec toute autre instance cpp-maker sur le même projet.
	// Le verrou est tenu tant que la Session est vivante : pendant toute la
	// phase de compilation / édition de liens / packaging / staging.
	if (lock)
		s.lock.acquire(c.p.root / ".cpp-maker.lock",
					   "Processus de compilation déjà lancé, Veuillez attendre...");
	return s;
}

inline void print_header(const Ctx& c, const Session& s) {
	util::outln("\033[1;34m[cpp-maker]\033[0m {} · toolchain \033[1m{}\033[0m ({}{}) · mode \033[1m{}\033[0m",
				c.p.name, s.tc->name, s.tc->triple.empty() ? "?" : s.tc->triple,
				s.tc->cross ? ", croisée" : "", s.mode->name);
	std::vector<std::string> mods;
	for (auto& m : s.plan->order) {
		bool sel = std::find(s.plan->selected.begin(), s.plan->selected.end(), m) != s.plan->selected.end();
		mods.push_back(sel ? m : m + "*");
	}
	util::outln("\033[2m            modules : {}{}\033[0m", util::join(mods, ", "),
				mods.size() != s.plan->selected.size() ? "   (* = dépendance)" : "");
}

// [generate.<nom>] : exécute les étapes dont une entrée a changé.
inline bool run_generate_steps(const Project& p, bool verbose) {
	namespace chr = std::chrono;
	auto newest = [&](const std::vector<std::string>& paths, bool& missing) {
		fs::file_time_type t = fs::file_time_type::min();
		std::error_code ec;
		for (auto& rel : paths) {
			fs::path path = p.root / rel;
			if (!fs::exists(path, ec)) { missing = true; continue; }
			if (fs::is_directory(path, ec)) {
				for (auto& e : fs::recursive_directory_iterator(path, ec))
					if (e.is_regular_file(ec)) t = std::max(t, e.last_write_time(ec));
			} else {
				t = std::max(t, fs::last_write_time(path, ec));
			}
		}
		return t;
	};
	for (auto& g : p.generate_steps) {
		fs::path stamp = p.root / "build" / "generate" / (g.name + ".stamp");
		std::error_code ec;
		bool missing_in = false, missing_out = false;
		auto in = newest(g.inputs, missing_in);
		(void)newest(g.outputs, missing_out);
		bool stale = !fs::exists(stamp, ec) || missing_out || g.inputs.empty() ||
					 in > fs::last_write_time(stamp, ec);
		if (!stale) continue;
		util::outln("\033[1;35m[generate]\033[0m {} : {}", g.name, g.command);
		auto r = proc::run({"sh", "-c", g.command}, {.capture = !verbose, .cwd = p.root});
		if (r.exit_code != 0) {
			if (!verbose) std::cerr << r.output;
			util::errln("\033[1;31mÉCHEC\033[0m : generate.{} (code {})", g.name, r.exit_code);
			return false;
		}
		fs::create_directories(stamp.parent_path(), ec);
		util::write_file(stamp, std::format("{}\n", g.command));
	}
	return true;
}

inline int run_plan(const Ctx& c, Session& s, const Opts& o) {
	print_header(c, s);
	auto& pl = *s.plan;
	if (o.dry_run) {
		auto dirty = build::predict_dirty(pl.tasks);
		size_t n = 0;
		for (size_t i = 0; i < pl.tasks.size(); ++i) {
			if (!dirty[i]) continue;
			++n;
			util::outln("  \033[1;35m[dry-run]\033[0m {:<6} {}", pl.tasks[i].kind, pl.tasks[i].label);
			if (o.verbose) util::outln("      $ {}", util::shell_join(pl.tasks[i].cmd));
		}
		util::outln("{} tâche(s) à exécuter sur {}.", n, pl.tasks.size());
		return 0;
	}
	if (o.clean) {
		std::error_code ec;
		fs::remove_all(pl.build_dir, ec);
		util::outln("\033[1;35m[cpp-maker]\033[0m nettoyage de {}", pl.build_dir.string());
	}
	build::write_compdb(pl, pl.build_dir / "compile_commands.json");
	if (!run_generate_steps(c.p, o.verbose)) return 1;

	auto t0 = std::chrono::steady_clock::now();
	build::ExecOptions eo;
	eo.jobs = o.jobs > 0 ? o.jobs : c.p.jobs;
	eo.verbose = o.verbose;
	eo.keep_going = o.keep_going;
	auto st = build::execute(pl.tasks, eo);
	if (st.failed > 0) {
		util::errln("\033[1;31m✗ {} tâche(s) en échec, {} non exécutée(s)\033[0m ({})",
					st.failed, st.skipped, util::elapsed(t0));
		return 1;
	}
	if (st.ran == 0) util::outln("\033[1;32m✓ rien à faire\033[0m, tout est à jour ({} tâches)", st.uptodate);
	else util::outln("\033[1;32m✓ {} tâche(s) exécutée(s)\033[0m, {} à jour — {}", st.ran, st.uptodate,
					 util::elapsed(t0));
	return 0;
}

inline int run_ninja(const Ctx& c, Session& s, const Opts& o) {
	print_header(c, s);
	if (!util::have("ninja")) util::die("ninja introuvable dans le PATH (backend ninja)");
	auto file = build::write_ninja(*s.plan);
	build::write_compdb(*s.plan, s.plan->build_dir / "compile_commands.json");
	std::vector<std::string> cmd = {"ninja", "-f", file.string()};
	// --jobs (CLI) prioritaire sur [build].jobs ; dans les deux cas le
	// budget ncpus - 1 s'applique. 0 = auto → budget complet.
	int requested = o.jobs > 0 ? o.jobs : c.p.jobs;
	int jobs = build::parallel_jobs(requested);
	cmd.push_back("-j");
	cmd.push_back(std::to_string(jobs));
	if (o.verbose) cmd.push_back("-v");
	if (o.keep_going) { cmd.push_back("-k"); cmd.push_back("0"); }
	if (o.dry_run) cmd.push_back("-n");
	return proc::run(cmd).exit_code;
}

// Chaîne de lancement : [valgrind …] [émulateur …] programme
inline std::optional<std::vector<std::string>> launcher(const Session& s, std::string& why) {
	std::vector<std::string> cmd;
	if (!s.tc->can_execute()) {
		why = s.tc->runner_note.empty() ? "exécution impossible pour cette cible" : s.tc->runner_note;
		return std::nullopt;
	}
	if (!s.mode->runner.empty()) {
		if (!s.tc->runner.empty()) {
			why = std::format("le mode '{}' ({}) n'est pas compatible avec un émulateur",
							  s.mode->name, s.mode->runner);
			return std::nullopt;
		}
		if (!util::have(s.mode->runner)) {
			why = std::format("{} introuvable — mode '{}' indisponible", s.mode->runner, s.mode->name);
			return std::nullopt;
		}
		cmd.push_back(s.mode->runner);
		cmd.insert(cmd.end(), s.mode->runner_args.begin(), s.mode->runner_args.end());
	}
	cmd.insert(cmd.end(), s.tc->runner.begin(), s.tc->runner.end());
	return cmd;
}

// ---------------------------------------------------------------------------
// build
// ---------------------------------------------------------------------------
inline int cmd_build(const std::vector<std::string>& args) {
	auto o = parse_opts(args, {"--backend"}, {"--tests"});
	mode_from_positional(o);
	if (!o.positional.empty()) util::die("argument inattendu : " + o.positional[0]);
	auto c = load_ctx();
	std::string mode = pick_mode(c, o);
	std::string backend = o.value("--backend", "native");
	if (backend != "native" && backend != "ninja") util::die("backend inconnu : " + backend + " (native|ninja)");
	auto mods = select_modules(c, o);
	std::vector<std::string> failed;
	for (auto& tcn : pick_toolchains(c, o)) {
		auto s = prepare(c, o, tcn, mode, mods, o.flag("--tests"));
		int rc = backend == "ninja" ? run_ninja(c, s, o) : run_plan(c, s, o);
		if (rc != 0) { failed.push_back(s.tc->name); continue; }
		if (o.dry_run) continue;
		for (auto& m : s.plan->selected) {
			auto& mp = s.plan->mods.at(m);
			if (!mp.artifact.empty()) util::outln("  → {:<14} {}", m, mp.artifact.string());
			if (!mp.test_exe.empty()) util::outln("  → {:<14} {}", m + " (tests)", mp.test_exe.string());
		}
	}
	if (!failed.empty()) {
		util::errln("\033[1;31mbuild en échec pour : {}\033[0m", util::join(failed, ", "));
		return 1;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------
inline int cmd_run(const std::vector<std::string>& args) {
	auto o = parse_opts(args);
	mode_from_positional(o);
	auto c = load_ctx();
	std::string mode = pick_mode(c, o);
	auto tcs = pick_toolchains(c, o);
	if (tcs.size() != 1) util::die("run : une seule toolchain à la fois");

	std::string target;
	if (!o.modules.empty()) {
		if (o.modules.size() != 1) util::die("run : un seul module à la fois");
		target = o.modules[0];
	} else if (!c.cwd_module.empty() && c.p.find_module(c.cwd_module)->type == "executable") {
		target = c.cwd_module;
	} else {
		std::vector<std::string> exes;
		for (auto& m : c.p.modules) if (m.type == "executable") exes.push_back(m.name);
		if (exes.empty())
			util::die("aucun module exécutable dans ce projet (bibliothèques uniquement : "
					  "utilisez 'cpp-maker build' ou 'cpp-maker test')");
		if (exes.size() > 1)
			util::die(std::format("plusieurs exécutables ({}) : précisez -m <module>", util::join(exes, ", ")));
		target = exes[0];
	}
	auto m = c.p.find_module(target);
	if (!m) util::die("module inconnu : " + target);
	if (m->type != "executable")
		util::die(std::format("le module '{}' est de type '{}' : rien à exécuter", target, m->type));

	auto s = prepare(c, o, tcs[0], mode, {target}, false);
	int rc = run_plan(c, s, o);
	if (rc != 0 || o.dry_run) return rc;
	s.lock.release();
	std::string why;
	auto l = launcher(s, why);
	if (!l) { util::errln("\033[1;31m{}\033[0m", why); return 2; }
	
	std::vector<std::string> cmd = *l;
	cmd.push_back(s.plan->mods.at(target).artifact.string());
	// Positional résiduel (compatibilité : `cpp-maker run release foo`) puis
	// arguments explicites passés après `--args` (ou `--`).
	cmd.insert(cmd.end(), o.positional.begin(), o.positional.end());
	cmd.insert(cmd.end(), o.passthrough.begin(), o.passthrough.end());
	if (o.verbose) util::outln("      $ {}", util::shell_join(cmd));
	util::outln("\033[1;34m[cpp-maker]\033[0m exécution de {}{}",
				target, o.passthrough.empty() ? ""
					 : std::format(" ({} argument(s) transmis)", o.passthrough.size()));
	return proc::run(cmd).exit_code;
}

// ---------------------------------------------------------------------------
// test
// ---------------------------------------------------------------------------
struct TestCaseResult {
	std::string suite, name, status; // pass | fail | skip | timeout
	double seconds = 0;
	std::string output;
	int code = 0;
};

inline void write_junit(const fs::path& file, const std::vector<TestCaseResult>& results) {
	std::map<std::string, std::vector<const TestCaseResult*>> suites;
	for (auto& r : results) suites[r.suite].push_back(&r);
	std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuites>\n";
	for (auto& [name, rs] : suites) {
		int fail = 0, skip = 0;
		double t = 0;
		for (auto r : rs) { fail += r->status == "fail" || r->status == "timeout"; skip += r->status == "skip"; t += r->seconds; }
		s += std::format("  <testsuite name=\"{}\" tests=\"{}\" failures=\"{}\" skipped=\"{}\" time=\"{:.3f}\">\n",
						 util::xml_escape(name), rs.size(), fail, skip, t);
		for (auto r : rs) {
			s += std::format("    <testcase classname=\"{}\" name=\"{}\" time=\"{:.3f}\">\n",
							 util::xml_escape(name), util::xml_escape(r->name), r->seconds);
			if (r->status == "fail" || r->status == "timeout")
				s += std::format("      <failure message=\"{}\"/>\n",
								 r->status == "timeout" ? "délai dépassé" : std::format("code de sortie {}", r->code));
			if (r->status == "skip") s += "      <skipped/>\n";
			s += "      <system-out>" + util::xml_escape(r->output) + "</system-out>\n";
			s += "    </testcase>\n";
		}
		s += "  </testsuite>\n";
	}
	s += "</testsuites>\n";
	util::write_file(file, s);
}

// Compile et exécute les tests d'un mode ; ajoute les résultats à `results`.
inline int do_test(const Ctx& c, const Opts& o, const std::string& mode,
				   std::vector<TestCaseResult>& results) {
	auto mods = select_modules(c, o);
	int timeout = o.values.count("--timeout") ? parse_int_arg("--timeout", o.value("--timeout"))
											  : c.p.test_timeout;
	std::string filter = o.value("--filter");
	bool capture = o.values.count("--junit") > 0;
	int failures = 0;

	for (auto& tcn : pick_toolchains(c, o)) {
		auto s = prepare(c, o, tcn, mode, mods, true);
		if (run_plan(c, s, o) != 0) {
			results.push_back({std::format("{}/{}", s.tc->name, mode), "build", "fail", 0, "échec de compilation", 1});
			++failures;
			continue;
		}
		if (o.dry_run) continue;
		std::string why;
		auto l = launcher(s, why);
		bool any = false;
		for (auto& m : s.plan->selected) {
			auto& mp = s.plan->mods.at(m);
			if (mp.test_exe.empty()) continue;
			any = true;
			TestCaseResult r;
			r.suite = std::format("{}/{}", s.tc->name, mode);
			r.name = m;
			if (!l) {
				r.status = "skip";
				r.output = why;
				util::outln("  \033[1;33m[ SKIP ]\033[0m {} — {}", m, why);
				results.push_back(r);
				continue;
			}
			std::vector<std::string> cmd = *l;
			cmd.push_back(mp.test_exe.string());
			if (!filter.empty()) { cmd.push_back("--filter"); cmd.push_back(filter); }
			cmd.insert(cmd.end(), o.passthrough.begin(), o.passthrough.end());
			util::outln("\n\033[1;36m── tests : {} ({} · {}) ──\033[0m", m, s.tc->name, mode);
			if (o.verbose) util::outln("      $ {}", util::shell_join(cmd));
			auto t0 = std::chrono::steady_clock::now();
			std::vector<std::pair<std::string, std::string>> env;
			for (auto& [k, v] : c.p.test_env) {
				std::string val = v;
				for (size_t pos; (pos = val.find("{root}")) != std::string::npos;)
					val.replace(pos, 6, c.p.root.string());
				env.emplace_back(k, val);
			}
			auto pr = proc::run(cmd, {.capture = capture, .cwd = std::nullopt, .timeout_sec = timeout,
									  .env = env});
			r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			r.output = pr.output;
			r.code = pr.exit_code;
			if (capture && !pr.output.empty()) std::cout << pr.output;
			if (pr.timed_out) r.status = "timeout";
			else if (pr.exit_code == 0) r.status = "pass";
			else if (pr.exit_code == 77) r.status = "skip";
			else r.status = "fail";
			if (r.status == "fail" || r.status == "timeout") ++failures;
			results.push_back(r);
		}
		if (!any) util::outln("  (aucun test trouvé pour {})", util::join(s.plan->selected, ", "));
	}
	return failures;
}

inline int print_test_summary(const std::vector<TestCaseResult>& results) {
	if (results.empty()) return 0;
	util::outln("\n\033[1;36mRésumé des tests\033[0m");
	int fail = 0, pass = 0, skip = 0;
	for (auto& r : results) {
		std::string tag = r.status == "pass" ? "\033[1;32m[ PASS ]\033[0m"
						: r.status == "skip" ? "\033[1;33m[ SKIP ]\033[0m"
						: r.status == "timeout" ? "\033[1;31m[TMOUT ]\033[0m"
												: "\033[1;31m[ FAIL ]\033[0m";
		util::outln("  {} {:<20} {:<24} {:.2f}s", tag, r.name, r.suite, r.seconds);
		if (r.status == "pass") ++pass;
		else if (r.status == "skip") ++skip;
		else ++fail;
	}
	util::outln("  {} réussi(s), {} échec(s), {} ignoré(s)", pass, fail, skip);
	return fail;
}

inline int cmd_test(const std::vector<std::string>& args) {
	auto o = parse_opts(args, {"--filter", "--junit", "--timeout"});
	mode_from_positional(o);
	if (!o.positional.empty()) util::die("argument inattendu : " + o.positional[0]);
	auto c = load_ctx();
	std::string mode = pick_mode(c, o);
	std::vector<TestCaseResult> results;
	do_test(c, o, mode, results);
	if (o.values.count("--junit")) {
		fs::path f = o.value("--junit");
		write_junit(f, results);
		util::outln("Rapport JUnit : {}", f.string());
	}
	return print_test_summary(results) > 0 ? 1 : 0;
}

inline int cmd_check(const std::vector<std::string>& args) {
	auto o = parse_opts(args, {"--modes", "--filter", "--junit", "--timeout"});
	auto c = load_ctx();
	std::vector<std::string> modes = o.values.count("--modes") ? util::split_list(o.value("--modes"))
															   : std::vector<std::string>{"debug", "asan", "tsan"};
	std::vector<TestCaseResult> results;
	for (auto& m : modes) {
		Opts om = o;
		om.mode = m;
		pick_mode(c, om);
		util::outln("\n\033[1;35m===== tests :: {} =====\033[0m", m);
		do_test(c, om, m, results);
	}
	if (o.values.count("--junit")) write_junit(o.value("--junit"), results);
	return print_test_summary(results) > 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// generate ninja | compdb
// ---------------------------------------------------------------------------
inline int cmd_generate(const std::vector<std::string>& args) {
	auto o = parse_opts(args);
	if (o.positional.empty()) util::die("usage : cpp-maker generate <ninja|compdb> [mode] [-t toolchain]");
	std::string what = o.positional[0];
	o.positional.erase(o.positional.begin());
	mode_from_positional(o);
	auto c = load_ctx();
	std::string mode = pick_mode(c, o);
	auto mods = select_modules(c, o);
	for (auto& tcn : pick_toolchains(c, o)) {
		auto s = prepare(c, o, tcn, mode, mods, true);
		if (what == "ninja") {
			auto f = build::write_ninja(*s.plan);
			util::outln("\033[1;32m{}\033[0m écrit — depuis {} : ninja -f {}", f.string(),
						c.p.root.string(), f.string());
		} else if (what == "compdb") {
			build::write_compdb(*s.plan, "compile_commands.json");
			util::outln("\033[1;32mcompile_commands.json\033[0m écrit ({} · {})", s.tc->name, mode);
		} else {
			util::die("generate : cible inconnue '" + what + "' (ninja|compdb)");
		}
	}
	return 0;
}

// ---------------------------------------------------------------------------
// graph / modules / info
// ---------------------------------------------------------------------------
inline int cmd_graph(const std::vector<std::string>& args) {
	auto o = parse_opts(args, {}, {"--dot"});
	auto c = load_ctx();
	if (o.flag("--dot")) {
		util::outln("digraph \"{}\" {{", c.p.name);
		util::outln("  rankdir=LR; node [shape=box, fontname=\"Helvetica\"];");
		auto node = [&](const ModuleConfig& m, const char* indent) {
			std::string shape = m.type == "executable" ? "box3d" : m.type == "header-only" ? "note" : "box";
			util::outln("{}\"{}\" [shape={}, label=\"{}\\n({})\"];", indent, m.name, shape, m.name, m.type);
		};
		// v0.6 : un sous-graphe par couche, de la plus basse à la plus haute.
		for (auto& l : c.p.layer_order) {
			util::outln("  subgraph \"cluster_{}\" {{ label=\"{}\"; style=rounded;", l, l);
			for (auto& m : c.p.modules) if (m.layer == l) node(m, "    ");
			util::outln("  }}");
		}
		for (auto& m : c.p.modules) {
			if (m.layer.empty()) node(m, "  ");
			for (auto& d : m.deps) util::outln("  \"{}\" -> \"{}\";", m.name, d);
			for (auto& d : m.pkg_config) util::outln("  \"{}\" -> \"pkg:{}\" [style=dashed];", m.name, d);
		}
		util::outln("}}");
		return 0;
	}
	std::function<void(const std::string&, const std::string&, int)> show =
		[&](const std::string& n, const std::string& indent, int depth) {
			auto m = c.p.find_module(n);
			for (size_t i = 0; i < m->deps.size(); ++i) {
				bool last = i + 1 == m->deps.size();
				auto d = c.p.find_module(m->deps[i]);
				util::outln("{}{}{} \033[2m({})\033[0m", indent, last ? "└─ " : "├─ ", d->name, d->type);
				if (depth < 32) show(d->name, indent + (last ? "   " : "│  "), depth + 1);
			}
		};
	std::set<std::string> is_dep;
	for (auto& m : c.p.modules) for (auto& d : m.deps) is_dep.insert(d);
	for (auto& m : c.p.modules) {
		if (is_dep.count(m.name) && !o.all) continue;
		util::outln("\033[1m{}\033[0m \033[2m({}{})\033[0m", m.name, m.type,
					m.layer.empty() ? "" : ", " + m.layer);
		show(m.name, "", 0);
	}
	return 0;
}

inline int cmd_modules(const std::vector<std::string>& args) {
	(void)parse_opts(args);
	auto c = load_ctx();
	util::outln("\033[1;36m{:<16} {:<12} {:<15} {:<34} {}\033[0m", "MODULE", "TYPE", "COUCHE", "DOSSIER",
				"DÉPENDANCES");
	for (auto& m : c.p.modules) {
		std::vector<std::string> deps = m.deps;
		for (auto& x : m.pkg_config) deps.push_back("pkg:" + x);
		for (auto& x : m.system_libs) deps.push_back("-l" + x);
		util::outln("{:<16} {:<12} {:<15} {:<34} {}", m.name, m.type, m.layer.empty() ? "—" : m.layer,
					m.dir.generic_string(), deps.empty() ? "—" : util::join(deps, ", "));
	}
	return 0;
}

inline void print_toolchain(const toolchain::Toolchain& t, const std::string& indent = "  ") {
	util::outln("{}cc        : {}", indent, t.cc ? t.cc->exe + "  (" + t.cc->version + ")" : "—");
	util::outln("{}cxx       : {}", indent, t.cxx ? t.cxx->exe + "  (" + t.cxx->version + ")" : "—");
	util::outln("{}ar/strip  : {} / {}", indent, t.ar, t.strip);
	util::outln("{}cible     : {} (arch {}, os {}){}", indent, t.triple.empty() ? "?" : t.triple, t.arch, t.os,
				t.cross ? " — compilation croisée" : "");
	if (t.cross)
		util::outln("{}exécution : {}", indent,
					t.runner.empty() ? "impossible — " + t.runner_note : util::join(t.runner, " "));
}

inline int cmd_info() {
	auto c = load_ctx(false);
	auto& p = c.p;
	util::outln("\033[1;36mcpp-maker {} — info\033[0m", CPP_MAKER_VERSION);
	util::outln("  racine     : {}", p.root.string());
	util::outln("  projet     : {} {}{}", p.name, p.version, p.workspace ? "  (workspace)" : "");
	util::outln("  standards  : {} / {}", p.cxx_standard, p.c_standard);
	util::outln("  défauts    : mode {}, toolchain {}", p.default_mode, p.default_toolchain);
	std::vector<std::string> modes;
	for (auto& m : p.modes) modes.push_back(m.name);
	util::outln("  modes      : {}", util::join(modes, ", "));
	std::vector<std::string> tcs;
	for (auto& [n, t] : p.toolchains) tcs.push_back(n);
	util::outln("  toolchains : {}", tcs.empty() ? "(host)" : util::join(tcs, ", "));
	util::outln("  modules    :");
	for (auto& m : p.modules)
		util::outln("    - {:<14} {:<12} {}{}", m.name, m.type, m.dir.generic_string(),
					m.deps.empty() ? "" : "  ← " + util::join(m.deps, ", "));
	if (!p.deploys.empty()) {
		std::vector<std::string> d;
		for (auto& [n, x] : p.deploys) d.push_back(n);
		util::outln("  déploiements : {}", util::join(d, ", "));
	}
	util::outln("\n\033[1;36mtoolchain par défaut : {}\033[0m", p.default_toolchain);
	auto t = toolchain::resolve(p, p.default_toolchain);
	print_toolchain(t);
	return 0;
}

// ---------------------------------------------------------------------------
// size : contenu détaillé des binaires construits
//   text/data/bss/dec/hex (sémantique Berkeley de `size`), en-tête ELF
//   (classe, endianness, machine, ABI, type, point d'entrée), archives .a
//   sommées. Sans dépendance à binutils : les binaires croisés sont lisibles
//   même sans binutils de la cible.
// ---------------------------------------------------------------------------
inline int cmd_size(const std::vector<std::string>& args) {
	auto o = parse_opts(args);
	mode_from_positional(o);
	auto c = load_ctx(false);
	std::string mode = pick_mode(c, o);
	std::vector<std::string> tcs = pick_toolchains(c, o);

	// Filtre -m <module> : exécutable <nom>[.exe], bibliothèque lib<nom>.*,
	// exécutable de tests <nom>-tests.
	auto wanted = [&](const std::string& file) {
		if (o.modules.empty()) return true;
		std::string stem = fs::path(file).stem().string();
		for (auto& m : o.modules) {
			if (stem == m || stem == m + "-tests" || stem == m + ".dll") return true;
			if (stem == "lib" + m || stem.starts_with("lib" + m + ".")) return true;
		}
		return false;
	};
	auto row_header = [] {
		util::outln("    {:>10} {:>10} {:>10} {:>10} {:>10}", "text", "data", "bss", "dec", "hex");
	};
	auto row = [](const elf::Info& in) {
		util::outln("    {:>10} {:>10} {:>10} {:>10} {:>10x}",
					in.text, in.data, in.bss, in.dec(), in.dec());
	};

	size_t listed = 0;
	for (auto& tc : tcs) {
		fs::path dir = build::build_dir_for(tc, mode);
		std::error_code ec;
		if (!fs::is_directory(dir, ec)) {
			util::warn("rien de construit pour la toolchain '{}' en mode {} ({})",
					   tc, mode, dir.generic_string());
			continue;
		}
		util::outln("\033[1;36m{} — toolchain {} · mode {}\033[0m",
					dir.generic_string(), tc, mode);
		for (auto* sub : {"bin", "lib", "tests"}) {
			fs::path d = dir / sub;
			if (!fs::is_directory(d, ec)) continue;
			std::vector<fs::path> files;
			for (fs::directory_iterator it(d, ec), end; it != end && !ec; it.increment(ec))
				if (it->is_regular_file(ec) && it->path().extension() != ".tmp")
					files.push_back(it->path());
			std::sort(files.begin(), files.end());
			for (auto& f : files) {
				if (!wanted(f.filename().string())) continue;
				++listed;
				auto in = elf::read(f);
				std::string rel = f.lexically_relative(dir).generic_string();
				if (!in.ok) {
					util::outln("  \033[1m{}\033[0m — {} ({:L} octets)", rel, in.note, in.file_size);
					continue;
				}
				if (in.members > 0) {
					util::outln("  \033[1m{}\033[0m — archive statique, {} objet{} ELF ({:L} octets)",
								rel, in.members, in.members > 1 ? "s" : "", in.file_size);
				} else {
				util::outln("  \033[1m{}\033[0m — {} {}, machine {}, ABI {},\n"
							"                {}, entrée 0x{:x} — {:L} octets",
							rel, in.cls, in.endian, in.machine, in.abi, in.type,
							in.entry, in.file_size);
				}
				row_header();
				row(in);
				if (o.verbose) {
					auto secs = in.sections;
					std::sort(secs.begin(), secs.end(),
							  [](auto& a, auto& b) { return a.size > b.size; });
					size_t n = std::min<size_t>(secs.size(), 10);
					for (size_t i = 0; i < n; ++i)
						util::outln("      {:<20} {:>10} octets   [{}{}]",
									secs[i].name.empty() ? "«sans nom»" : secs[i].name,
									secs[i].size,
									secs[i].flags & 4 ? "X" : "-",
									secs[i].flags & 1 ? "W" : "R");
				}
			}
		}
	}
	if (listed == 0)
		util::die(std::format("aucun binaire à analyser — lancez 'cpp-maker build' d'abord "
							  "(mode {}) ou vérifiez -t/-m", mode));
	return 0;
}

// ---------------------------------------------------------------------------
// clean
// ---------------------------------------------------------------------------
inline int cmd_clean(const std::vector<std::string>& args) {
	auto o = parse_opts(args);
	mode_from_positional(o);
	auto c = load_ctx(false);
	util::FileLock lock;
	lock.acquire(c.p.root / ".cpp-maker.lock", "Processus de compilation déjà lancé, Veuillez attendre...");
	std::error_code ec;
	auto rm = [&](const fs::path& p) {
		if (!fs::exists(p, ec)) return;
		fs::remove_all(p, ec);
		util::outln("  supprimé : {}", p.string());
	};
	if (!o.mode.empty() || !o.toolchains.empty()) {
		std::vector<fs::path> tcdirs;
		if (!o.toolchains.empty()) for (auto& t : o.toolchains) tcdirs.push_back(fs::path("build") / t);
		else if (fs::is_directory("build", ec))
			for (auto& e : fs::directory_iterator("build", ec)) tcdirs.push_back(e.path());
		for (auto& d : tcdirs) rm(o.mode.empty() ? d : d / o.mode);
		return 0;
	}
	if (!fs::exists("build", ec) && !(o.all && fs::exists("dist", ec))) {
		util::outln("Rien à nettoyer.");
		return 0;
	}
	rm("build");
	if (o.all) rm("dist");
	return 0;
}

// ---------------------------------------------------------------------------
// package / install / deploy
// ---------------------------------------------------------------------------

// Le mode produit-il des informations de débogage ? (strip inutile sinon)
inline bool mode_has_debug(const ModeConfig& m) {
	return std::any_of(m.flags.begin(), m.flags.end(), [](auto& f) { return f.starts_with("-g"); });
}

inline void copy_into(const fs::path& src, const fs::path& dst_dir, std::vector<fs::path>& installed) {
	std::error_code ec;
	fs::create_directories(dst_dir, ec);
	if (ec) util::die(std::format("impossible de créer {} : {}", dst_dir.string(), ec.message()));
	fs::path dst = dst_dir / src.filename();
	if (fs::is_directory(src, ec)) {
		fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
		if (ec) util::die(std::format("copie de {} → {} : {}", src.string(), dst.string(), ec.message()));
		for (auto& e : fs::recursive_directory_iterator(dst, ec))
			if (e.is_regular_file()) installed.push_back(e.path());
	} else {
		fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
		if (ec) util::die(std::format("copie de {} → {} : {}", src.string(), dst.string(), ec.message()));
		installed.push_back(dst);
	}
}

// Contenu d'un répertoire d'en-têtes copié dans include/.
inline void copy_headers(const fs::path& inc_dir, const fs::path& dst, std::vector<fs::path>& installed) {
	std::error_code ec;
	if (!fs::is_directory(inc_dir, ec)) return;
	for (auto& e : fs::directory_iterator(inc_dir, ec)) copy_into(e.path(), dst, installed);
}

inline std::string pc_file(const Ctx& c, const ModuleConfig& m, const build::ModulePlan& mp) {
	std::string s = "prefix=${pcfiledir}/../..\nexec_prefix=${prefix}\n"
					"libdir=${prefix}/lib\nincludedir=${prefix}/include\n\n";
	s += "Name: " + m.name + "\n";
	s += "Description: " + (c.p.package.description.empty() ? m.name + " (" + c.p.name + ")"
															 : c.p.package.description) + "\n";
	s += "Version: " + (m.version.empty() ? c.p.version : m.version) + "\n";
	std::vector<std::string> req = m.pkg_config;
	for (auto& d : m.deps) req.push_back(d);
	if (!req.empty()) s += "Requires: " + util::join(req, ", ") + "\n";
	if (m.type != "header-only") s += "Libs: -L${libdir} -l" + m.name + "\n";
	std::vector<std::string> priv;
	for (auto& l : m.system_libs) priv.push_back(l.starts_with("-") ? l : "-l" + l);
	if (!priv.empty()) s += "Libs.private: " + util::join(priv, " ") + "\n";
	std::string cflags = "-I${includedir}";
	for (auto& d : m.public_defines) cflags += " -D" + d;
	s += "Cflags: " + cflags + "\n";
	(void)mp;
	return s;
}

// Copie les artefacts installables dans dest (bin/ lib/ include/ share/).
inline std::vector<fs::path> stage(const Ctx& c, const Session& s, const fs::path& dest, bool strip) {
	std::vector<fs::path> installed;
	const auto& t = *s.tc;
	std::vector<fs::path> to_strip;
	for (auto& n : s.plan->order) {
		auto& mp = s.plan->mods.at(n);
		const ModuleConfig& m = *mp.cfg;
		if (!m.install) continue;
		bool selected = std::find(s.plan->selected.begin(), s.plan->selected.end(), n) != s.plan->selected.end();
		if (!selected) {
			// Dépendance tirée par un module demandé : seul le runtime est utile.
			if (m.type == "shared") {
				copy_into(mp.artifact, dest / (t.os == "windows" ? "bin" : "lib"), installed);
				to_strip.push_back(installed.back());
			}
			continue;
		}
		if (m.type == "executable") {
			copy_into(mp.artifact, dest / "bin", installed);
			to_strip.push_back(installed.back());
			continue;
		}
		if (m.type == "shared") {
			copy_into(mp.artifact, dest / (t.os == "windows" ? "bin" : "lib"), installed);
			to_strip.push_back(installed.back());
			if (!mp.import_lib.empty()) copy_into(mp.import_lib, dest / "lib", installed);
		} else if (m.type == "static") {
			copy_into(mp.artifact, dest / "lib", installed);
		}
		for (auto& inc : m.public_includes) copy_headers(util::under(m.dir, inc), dest / "include", installed);
		fs::path pc = dest / "lib" / "pkgconfig" / (m.name + ".pc");
		util::write_file(pc, pc_file(c, m, mp));
		installed.push_back(pc);
	}
	std::error_code ec;
	for (auto& e : fs::directory_iterator(".", ec)) {
		auto fn = e.path().filename().string();
		auto up = fn;
		for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
		if (e.is_regular_file() && (up.starts_with("README") || up.starts_with("LICENSE") ||
									up.starts_with("COPYING") || up.starts_with("CHANGELOG")))
			copy_into(e.path(), dest / "share" / "doc" / c.p.name, installed);
	}
	for (auto& f : c.p.package.extra_files) {
		if (!fs::exists(f, ec)) util::die("package.extra_files : introuvable : " + f);
		copy_into(f, dest / "share" / c.p.name, installed);
	}
	if (strip && !to_strip.empty()) {
		if (!util::have(t.strip)) {
			util::warn("{} introuvable : binaires non strippés", t.strip);
		} else {
			for (auto& f : to_strip) {
				bool lib = f.extension() == ".so" || f.extension() == ".dll" || f.extension() == ".dylib";
				std::vector<std::string> cmd = {t.strip};
				if (lib) cmd.push_back("--strip-unneeded");
				cmd.push_back(f.string());
				auto r = proc::capture(cmd);
				if (r.exit_code != 0) util::warn("strip {} : {}", f.string(), util::trim(r.output));
			}
		}
	}
	return installed;
}

inline std::string package_basename(const Ctx& c, const toolchain::Toolchain& t) {
	return std::format("{}-{}-{}-{}", c.p.name, c.p.version, t.os, t.arch);
}

inline void run_checked(const std::vector<std::string>& cmd, bool verbose,
						const std::optional<fs::path>& cwd = std::nullopt) {
	if (verbose) util::outln("      $ {}{}", cwd ? "(cd " + cwd->string() + ") " : "", util::shell_join(cmd));
	auto r = proc::run(cmd, {.capture = true, .cwd = cwd});
	if (r.exit_code != 0)
		util::die(std::format("commande en échec (code {}) : {}\n{}", r.exit_code,
							  util::shell_join(cmd), util::trim(r.output)));
}

inline std::string human_size(uintmax_t n) {
	if (n >= 1024 * 1024) return std::format("{:.1f} Mio", (double)n / (1024.0 * 1024.0));
	if (n >= 1024) return std::format("{:.1f} Kio", (double)n / 1024.0);
	return std::format("{} o", n);
}

inline std::string deb_name(const Project& p) {
	std::string pkg;
	for (char ch : util::lower(p.name)) pkg += (std::isalnum((unsigned char)ch) || ch == '.' || ch == '+') ? ch : '-';
	return pkg;
}
inline std::string deb_filename(const Project& p, const toolchain::Toolchain& t) {
	return std::format("{}_{}_{}.deb", deb_name(p), p.version, toolchain::deb_arch(t));
}

// Construit un paquet .deb ; retourne son chemin.
inline fs::path make_deb(const Ctx& c, const Session& s, const fs::path& dist, bool strip, bool verbose) {
	if (!util::have("dpkg-deb")) util::die("dpkg-deb introuvable : format deb indisponible");
	auto& p = c.p;
	if (p.version.empty() || !std::isdigit((unsigned char)p.version[0]))
		util::die("format deb : project.version doit commencer par un chiffre");
	std::string pkg = deb_name(p);
	std::string arch = toolchain::deb_arch(*s.tc);
	fs::path root = s.plan->build_dir / "deb" / std::format("{}_{}_{}", pkg, p.version, arch);
	std::error_code ec;
	fs::remove_all(root, ec);
	fs::path prefix = fs::path(p.package.deb_prefix).relative_path();
	auto files = stage(c, s, root / prefix, strip);
	uintmax_t kb = 0;
	for (auto& f : files) kb += fs::file_size(f, ec) / 1024 + 1;
	std::string control = std::format(
		"Package: {}\nVersion: {}\nArchitecture: {}\nMaintainer: {}\nInstalled-Size: {}\n"
		"Section: misc\nPriority: optional\n", pkg, p.version, arch, p.package.maintainer, kb);
	if (!p.package.deb_depends.empty()) control += "Depends: " + util::join(p.package.deb_depends, ", ") + "\n";
	if (!p.package.homepage.empty()) control += "Homepage: " + p.package.homepage + "\n";
	std::string desc = p.package.description.empty() ? p.name : p.package.description;
	control += "Description: " + desc + "\n";
	util::write_file(root / "DEBIAN" / "control", control);
	fs::permissions(root / "DEBIAN", fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
					fs::perms::others_read | fs::perms::others_exec, ec);
	fs::path out = fs::absolute(dist / deb_filename(p, *s.tc));
	run_checked({"dpkg-deb", "--build", "--root-owner-group", root.string(), out.string()}, verbose);
	return out;
}

// Produit les archives demandées ; retourne les fichiers créés.
inline std::vector<fs::path> make_packages(const Ctx& c, const Session& s,
										   const std::vector<std::string>& formats,
										   bool strip, bool verbose, fs::path* stage_out = nullptr) {
	std::string base = package_basename(c, *s.tc);
	fs::path stage_parent = s.plan->build_dir / "stage";
	fs::path stage_dir = stage_parent / base;
	std::error_code ec;
	fs::remove_all(stage_dir, ec);
	fs::create_directories(stage_dir, ec);
	auto files = stage(c, s, stage_dir, strip);
	util::outln("\033[1;34m[cpp-maker]\033[0m staging : {} ({} fichier(s))", stage_dir.string(), files.size());
	if (stage_out) *stage_out = stage_dir;

	fs::path dist = fs::absolute("dist");
	fs::create_directories(dist, ec);
	std::vector<fs::path> out;
	for (auto& f : formats) {
		if (f == "tar.gz" || f == "tar.xz") {
			if (!util::have("tar")) util::die("tar introuvable");
			fs::path a = dist / (base + "." + f);
			run_checked({"tar", "-C", stage_parent.string(), f == "tar.gz" ? "-czf" : "-cJf",
						 a.string(), base}, verbose);
			out.push_back(a);
		} else if (f == "zip") {
			if (!util::have("zip")) util::die("zip introuvable : format zip indisponible");
			fs::path a = dist / (base + ".zip");
			fs::remove(a, ec);
			run_checked({"zip", "-qry", a.string(), base}, verbose, fs::absolute(stage_parent));
			out.push_back(a);
		} else if (f == "deb") {
			out.push_back(make_deb(c, s, dist, strip, verbose));
		} else if (f != "stage") {
			util::die("format de paquet inconnu : " + f);
		}
	}
	if (c.p.package.checksums && !out.empty() && util::have("sha256sum")) {
		std::vector<std::string> cmd = {"sha256sum"};
		for (auto& f : out) cmd.push_back(f.filename().string());
		auto r = proc::run(cmd, {.capture = true, .cwd = dist});
		if (r.exit_code == 0) {
			fs::path sums = dist / (base + ".sha256");
			util::write_file(sums, r.output);
			out.push_back(sums);
		}
	}
	return out;
}

inline int cmd_package(const std::vector<std::string>& args) {
	auto o = parse_opts(args, {"--format"}, {"--no-strip"});
	mode_from_positional(o);
	auto c = load_ctx();
	std::string mode = pick_mode(c, o, "release");
	auto formats = o.values.count("--format") ? util::split_list(o.value("--format")) : c.p.package.formats;
	for (auto& f : formats) {
		static const std::set<std::string> ok = {"tar.gz", "tar.xz", "zip", "deb", "stage"};
		if (!ok.count(f)) util::die("format inconnu : " + f + " (tar.gz|tar.xz|zip|deb|stage)");
	}
	auto mods = select_modules(c, o);
	for (auto& tcn : pick_toolchains(c, o)) {
		auto s = prepare(c, o, tcn, mode, mods, false);
		if (run_plan(c, s, o) != 0) return 1;
		if (o.dry_run) continue;
		bool strip = c.p.package.strip && !o.flag("--no-strip") && !mode_has_debug(*s.mode);
		auto files = make_packages(c, s, formats, strip, o.verbose);
		for (auto& f : files) {
			std::error_code ec;
			util::outln("  \033[1;32m📦\033[0m {:<56} {}", fs::relative(f, c.p.root, ec).string(),
						human_size(fs::file_size(f, ec)));
		}
	}
	return 0;
}

inline int cmd_install(const std::vector<std::string>& args) {
	auto o = parse_opts(args, {"--prefix", "--destdir"}, {"--no-strip"});
	mode_from_positional(o);
	auto c = load_ctx();
	std::string mode = pick_mode(c, o, "release");
	auto tcs = pick_toolchains(c, o);
	if (tcs.size() != 1) util::die("install : une seule toolchain à la fois");
	fs::path prefix = o.value("--prefix", "/usr/local");
	fs::path target = prefix;
	if (o.values.count("--destdir")) target = fs::path(o.value("--destdir")) / prefix.relative_path();
	auto s = prepare(c, o, tcs[0], mode, select_modules(c, o), false);
	if (run_plan(c, s, o) != 0) return 1;
	if (o.dry_run) { util::outln("[dry-run] installation dans {}", target.string()); return 0; }
	bool strip = c.p.package.strip && !o.flag("--no-strip") && !mode_has_debug(*s.mode);
	auto files = stage(c, s, target, strip);
	std::string manifest;
	for (auto& f : files) { util::outln("  installé : {}", f.string()); manifest += f.string() + "\n"; }
	util::write_file(s.plan->build_dir / "install_manifest.txt", manifest);
	util::outln("\033[1;32m{} fichier(s) installé(s) dans {}\033[0m", files.size(), target.string());
	return 0;
}

inline std::string expand(std::string s, const std::map<std::string, std::string>& vars) {
	for (auto& [k, v] : vars) s = util::replace_all(s, "{" + k + "}", v);
	return s;
}

inline int cmd_deploy(const std::vector<std::string>& args) {
	auto o = parse_opts(args, {}, {"--list"});
	auto c = load_ctx();
	if (o.flag("--list") || o.positional.empty()) {
		if (c.p.deploys.empty()) {
			util::outln("Aucune cible de déploiement : ajoutez une table [deploy.<nom>] dans cpp-maker.toml");
			return o.positional.empty() && !o.flag("--list") ? 1 : 0;
		}
		util::outln("\033[1;36mCibles de déploiement\033[0m");
		for (auto& [n, d] : c.p.deploys)
			util::outln("  {:<12} {} → {}{}{} ({} · {} · {})", n, d.method,
						d.user.empty() ? "" : d.user + "@", d.host.empty() ? "" : d.host + ":", d.path,
						d.toolchain.empty() ? c.p.default_toolchain : d.toolchain, d.mode, d.artifact);
		return 0;
	}
	std::string name = o.positional[0];
	auto it = c.p.deploys.find(name);
	if (it == c.p.deploys.end()) util::die("cible de déploiement inconnue : " + name);
	DeployConfig d = it->second;
	std::string mode = pick_mode(c, o, d.mode);
	auto tcs = pick_toolchains(c, o, d.toolchain);
	if (tcs.size() != 1) util::die("deploy : une seule toolchain à la fois");
	Opts so = o;
	if (so.modules.empty()) { so.modules = d.modules; if (so.modules.empty()) so.all = true; }
	auto s = prepare(c, so, tcs[0], mode, select_modules(c, so), false);

	if (run_plan(c, s, o) != 0) return 1;

	std::string target = (d.user.empty() ? "" : d.user + "@") + d.host;
	std::vector<std::string> ssh = {"ssh"};
	if (d.port) { ssh.push_back("-p"); ssh.push_back(std::to_string(d.port)); }
	ssh.insert(ssh.end(), d.ssh_options.begin(), d.ssh_options.end());

	fs::path stage_dir;
	std::vector<fs::path> packages;
	std::map<std::string, std::string> vars = {
		{"name", c.p.name}, {"version", c.p.version},
		{"toolchain", s.tc->name}, {"mode", mode}, {"host", d.host}, {"artifact", ""}};
	d.path = expand(d.path, vars);
	vars["path"] = d.path;

	auto exec = [&](const std::vector<std::string>& cmd) {
		util::outln("  \033[1;35m$\033[0m {}", util::shell_join(cmd));
		if (o.dry_run) return;
		auto r = proc::run(cmd);
		if (r.exit_code != 0) util::die(std::format("déploiement interrompu (code {})", r.exit_code));
	};

	util::outln("\033[1;34m[cpp-maker]\033[0m déploiement '{}' : {} → {}{}", name, d.method,
				target.empty() ? "" : target + ":", d.path);
	bool strip = c.p.package.strip && !mode_has_debug(*s.mode);
	if (!o.dry_run) {
		std::vector<std::string> fmts;
		if (d.artifact != "stage") fmts.push_back(d.artifact);
		packages = make_packages(c, s, fmts, strip, o.verbose, &stage_dir);
		std::erase_if(packages, [](auto& f) { return f.extension() == ".sha256"; });
	} else {
		stage_dir = s.plan->build_dir / "stage" / package_basename(c, *s.tc);
		if (d.artifact == "deb") packages.push_back("dist/" + deb_filename(c.p, *s.tc));
		else if (d.artifact != "stage") packages.push_back("dist/" + package_basename(c, *s.tc) + "." + d.artifact);
	}
	if (!packages.empty()) vars["artifact"] = d.path + "/" + packages[0].filename().string();

	for (auto& b : d.before) exec({"sh", "-c", expand(b, vars)});

	// Éléments à transférer : contenu du staging, ou le paquet.
	std::vector<fs::path> items;
	if (d.artifact == "stage") {
		std::error_code ec;
		if (!o.dry_run)
			for (auto& e : fs::directory_iterator(stage_dir, ec)) items.push_back(e.path());
		else items.push_back(stage_dir / "*");
	} else {
		items = packages;
	}

	if (d.method == "script") {
		// v0.4 : script local recevant le staging dans $CPP_MAKER_DIST.
		exec({"sh", "-c", "CPP_MAKER_DIST=" + util::shell_quote(fs::absolute(stage_dir).string()) +
						  "; export CPP_MAKER_DIST; " + expand(d.script, vars)});
	} else if (d.method == "copy") {
		if (!o.dry_run) {
			std::vector<fs::path> done;
			for (auto& i : items) copy_into(i, d.path, done);
			util::outln("  {} fichier(s) copié(s) dans {}", done.size(), d.path);
		} else {
			for (auto& i : items) util::outln("  [dry-run] copie {} → {}", i.string(), d.path);
		}
		for (auto& a : d.after) exec({"sh", "-c", expand(a, vars)});
	} else {
		auto mk = ssh;
		mk.push_back(target);
		mk.push_back("mkdir -p " + util::shell_quote(d.path));
		exec(mk);
		if (d.method == "rsync") {
			std::vector<std::string> cmd = {"rsync", "-az"};
			if (d.delete_extra) cmd.push_back("--delete");
			if (o.verbose) cmd.push_back("-v");
			cmd.push_back("-e");
			cmd.push_back(util::shell_join(ssh));
			if (d.artifact == "stage") cmd.push_back(stage_dir.string() + "/");
			else for (auto& i : items) cmd.push_back(i.string());
			cmd.push_back(target + ":" + d.path + "/");
			exec(cmd);
		} else { // scp
			std::vector<std::string> cmd = {"scp", "-r"};
			if (d.port) { cmd.push_back("-P"); cmd.push_back(std::to_string(d.port)); }
			cmd.insert(cmd.end(), d.ssh_options.begin(), d.ssh_options.end());
			for (auto& i : items) cmd.push_back(i.string());
			cmd.push_back(target + ":" + d.path + "/");
			exec(cmd);
		}
		for (auto& a : d.after) {
			auto cmd = ssh;
			cmd.push_back(target);
			cmd.push_back(expand(a, vars));
			exec(cmd);
		}
	}
	for (auto& a : d.after_local) exec({"sh", "-c", expand(a, vars)});
	util::outln("\033[1;32m✓ déploiement '{}' {}\033[0m", name, o.dry_run ? "simulé (dry-run)" : "terminé");
	return 0;
}

// ---------------------------------------------------------------------------
// Analyse statique des #include pour la détection des dépendances implicites
// ---------------------------------------------------------------------------

// Extrait les chemins des directives #include "…" / <…> d'un fichier source.
inline std::vector<std::string> scan_includes(const fs::path& file) {
	std::vector<std::string> out;
	std::string content;
	if (!util::read_file(file, content)) return out;
	size_t i = 0;
	while ((i = content.find("#include", i)) != std::string::npos) {
		size_t j = i + 8;
		while (j < content.size() && (content[j] == ' ' || content[j] == '\t')) ++j;
		if (j >= content.size()) break;
		char open = content[j];
		if (open != '"' && open != '<') { i += 8; continue; }
		char close = open == '"' ? '"' : '>';
		auto end = content.find(close, j + 1);
		if (end == std::string::npos || end > content.find('\n', j)) { i = j + 1; continue; }
		out.push_back(content.substr(j + 1, end - j - 1));
		i = end + 1;
	}
	return out;
}

// Indexe chaque en-tête public du workspace : "chemin/relatif.hpp" → module.
inline std::map<std::string, std::string> public_header_index(const Project& p) {
	std::map<std::string, std::string> idx;
	std::error_code ec;
	for (auto& m : p.modules) {
		for (auto& inc_dir : m.public_includes) {
			fs::path root = util::under(m.dir, inc_dir);
			if (!fs::is_directory(root, ec)) continue;
			for (auto& e : fs::recursive_directory_iterator(root,
					 fs::directory_options::skip_permission_denied, ec)) {
				if (ec) break;
				if (!e.is_regular_file(ec)) continue;
				auto rel = e.path().lexically_relative(root).generic_string();
				idx[rel] = m.name;
			}
		}
	}
	return idx;
}

// En-tête C/C++ (analysé pour les #include comme les sources).
inline bool is_header_file(const std::string& e) {
	return e == ".h" || e == ".hpp" || e == ".hh" || e == ".hxx" || e == ".h++" ||
		   e == ".ipp" || e == ".tpp" || e == ".inl";
}

// Retourne les couples (module, dépendance_manquante) détectés à partir des
// #include qui pointent vers les en-têtes publics d'un autre module non
// joignable par la fermeture transitive des dépendances déclarées.
inline std::vector<std::pair<std::string, std::string>>
detect_missing_deps(const Project& p) {
	std::set<std::pair<std::string, std::string>> missing;
	auto idx = public_header_index(p);
	for (auto& m : p.modules) {
		// Fermeture transitive des dépendances déjà déclarées.
		std::set<std::string> reachable;
		std::vector<std::string> stack = m.deps;
		while (!stack.empty()) {
			auto n = stack.back(); stack.pop_back();
			if (!reachable.insert(n).second) continue;
			if (auto dm = p.find_module(n))
				for (auto& d : dm->deps) stack.push_back(d);
		}
		// On ignore tests/ (ils ne créent pas de dépendance de compilation).
		// Les en-têtes comptent : un #include dans un en-tête public impose
		// la dépendance à tous les modules qui l'incluent.
		std::vector<fs::path> files;
		for (auto& src : build::discover(m, m.source_dirs)) files.push_back(src.path);
		std::vector<std::string> hdr_dirs = m.public_includes;
		hdr_dirs.insert(hdr_dirs.end(), m.private_includes.begin(), m.private_includes.end());
		for (auto& d : hdr_dirs) {
			fs::path dir = util::under(m.dir, d);
			std::error_code ec;
			if (!fs::is_directory(dir, ec)) continue;
			for (auto& e : fs::recursive_directory_iterator(dir,
					 fs::directory_options::skip_permission_denied, ec)) {
				if (ec) break;
				auto ext = e.path().extension().string();
				if (e.is_regular_file(ec) && is_header_file(ext))
					files.push_back(e.path());
			}
		}
		std::sort(files.begin(), files.end());
		files.erase(std::unique(files.begin(), files.end()), files.end());
		for (auto& file : files) {
			for (auto& inc : scan_includes(file)) {
				auto it = idx.find(inc);
				if (it == idx.end() || it->second == m.name) continue;
				if (reachable.count(it->second)) continue;
				missing.insert({m.name, it->second});
			}
		}
	}
	return {missing.begin(), missing.end()};
}

// Définies plus bas (section module rename/remove).
inline std::string replace_toml_array(const std::string& text, const std::string& section,
									  const std::string& key, const std::vector<std::string>& values);
inline fs::path editable_module_file(const Project& p, const ModuleConfig& m);

// Applique les dépendances manquantes : pour chaque module.toml concerné,
// ajoute les nouvelles entrées au tableau [dependencies].modules, valide
// la configuration globale puis remplace le fichier atomiquement (rollback
// en cas d'erreur). Retourne le nombre de modules effectivement modifiés.
inline int apply_missing_deps(const Project& p,
		const std::vector<std::pair<std::string, std::string>>& missing) {
	std::map<std::string, std::vector<std::string>> to_add;
	for (auto& [mn, dn] : missing) to_add[mn].push_back(dn);
	int fixed = 0;
	for (auto& [mn, adds] : to_add) {
		auto m = p.find_module(mn);
		if (!m) continue;
		fs::path file = editable_module_file(p, *m);
		std::string text;
		if (!util::read_file(file, text)) {
			util::warn("impossible de lire {}", file.string());
			continue;
		}
		std::vector<std::string> deps = m->deps;
		for (auto& d : adds)
			if (std::find(deps.begin(), deps.end(), d) == deps.end())
				deps.push_back(d);
		std::string new_text = replace_toml_array(text, "dependencies", "modules", deps);
		if (new_text == text) continue;
		util::write_file_atomic(file, new_text);
		try {
			auto np = cfg::load(p.root, p.file);
			auto e = cfg::validate_errors(np);
			if (!e.empty()) throw std::runtime_error(e);
		} catch (const std::exception& e) {
			util::write_file_atomic(file, text);
			util::warn("édition annulée pour '{}' : {}", mn, e.what());
			continue;
		}
		util::outln("  \033[1;32m+\033[0m {:<14} ← {}", mn, util::join(adds, ", "));
		++fixed;
	}
	return fixed;
}

// ---------------------------------------------------------------------------
// doctor
// ---------------------------------------------------------------------------
inline int cmd_doctor(const std::vector<std::string>& args = {}) {
	auto o = parse_opts(args, {}, {"--no-fix", "--check-deps"});
	auto c = load_ctx(false);
	int problems = 0;
	auto mark = [&](bool ok, const std::string& msg, bool fatal = true) {
		if (ok) util::outln("  \033[1;32m[ ok ]\033[0m {}", msg);
		else if (fatal) { util::outln("  \033[1;31m[fail]\033[0m {}", msg); ++problems; }
		else util::outln("  \033[1;33m[ -- ]\033[0m {}", msg);
	};

	util::outln("\033[1;36mcpp-maker doctor\033[0m (v{})\n", CPP_MAKER_VERSION);
	util::outln("\033[1mConfiguration\033[0m");
	mark(c.p.has_file, "cpp-maker.toml présent");
	auto err = cfg::validate_errors(c.p);
	mark(err.empty(), err.empty() ? std::format("configuration valide ({} module(s))", c.p.modules.size())
								  : "configuration invalide : " + err);

	util::outln("\n\033[1mOutils\033[0m");
	for (auto [tool, use] : std::initializer_list<std::pair<const char*, const char*>>{
			 {"ar", "bibliothèques statiques"}, {"pkg-config", "dépendances pkg-config"},
			 {"ninja", "--backend ninja"}, {"tar", "paquets tar.gz/tar.xz"}, {"zip", "paquets zip"},
			 {"dpkg-deb", "paquets deb"}, {"sha256sum", "sommes de contrôle"},
			 {"rsync", "deploy rsync"}, {"ssh", "deploy rsync/scp"}, {"valgrind", "mode valgrind"},
			 {"qemu-arm", "tests ARM 32 bits"}, {"qemu-aarch64", "tests ARM 64 bits"},
			 {"wine", "tests Windows"}}) {
		bool ok = util::have(tool);
		mark(ok, std::format("{:<12} {}", tool, ok ? use : std::string(use) + " — indisponible"),
			 std::string(tool) == "ar");
	}

	std::vector<std::string> names = {c.p.default_toolchain};
	for (auto& [n, t] : c.p.toolchains) if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
	for (auto& n : names) {
		util::outln("\n\033[1mToolchain {}\033[0m", n);
		try {
			auto t = toolchain::resolve(c.p, n);
			print_toolchain(t, "         ");
			if (t.cc) {
				std::string e;
				bool ok = toolchain::smoke_compile(*t.cc, "c", c.p.c_standard, &e);
				mark(ok, std::format("C   -std={} : compilation + édition de liens", c.p.c_standard));
				if (!ok) util::outln("           {}", toolchain::truncate(e, 3));
			}
			if (t.cxx) {
				std::string e;
				bool ok = toolchain::smoke_compile(*t.cxx, "c++", c.p.cxx_standard, &e);
				mark(ok, std::format("C++ -std={} : compilation + édition de liens", c.p.cxx_standard));
				if (!ok) util::outln("           {}", toolchain::truncate(e, 3));
			}
			if (!t.cross && t.cxx) {
				for (auto san : {"address", "thread", "undefined"}) {
					toolchain::Tool tool = *t.cxx;
					tool.pre_args.push_back(std::string("-fsanitize=") + san);
					bool ok = toolchain::smoke_compile(tool, "c++", c.p.cxx_standard);
					mark(ok, std::format("sanitizer {}", san), false);
				}
			}
			if (t.cross) mark(t.can_execute(), t.can_execute() ? "exécution via " + util::join(t.runner, " ")
																: "exécution impossible : " + t.runner_note, false);
		} catch (const std::exception& e) {
			mark(false, e.what());
		}
	}
	auto prefixes = toolchain::detect_prefixes();
	if (!prefixes.empty()) {
		util::outln("\n\033[1mCompilateurs croisés détectés dans le PATH\033[0m (utilisables avec -t <triplet>)");
		for (auto& p : prefixes) util::outln("  {}", p.substr(0, p.size() - 1));
	}
	
	util::outln("\n\033[1mDépendances entre modules\033[0m");
	auto missing = detect_missing_deps(c.p);
	if (missing.empty()) {
		util::outln("  \033[1;32m[ ok ]\033[0m toutes les dépendances déclarées couvrent les #include");
	} else {
		util::outln("  {} dépendance(s) implicite(s) détectée(s) :", missing.size());
		for (auto& [mn, dn] : missing)
			util::outln("    \033[1;33m·\033[0m {:<14} → {}", mn, dn);
		if (o.flag("--check-deps")) {
			util::outln("  \033[2m(--check-deps : rien n'a été modifié)\033[0m");
			problems += (int)missing.size();
		} else if (o.flag("--no-fix")) {
			util::outln("  \033[2m(--no-fix : utilisez 'cpp-maker doctor' pour enregistrer)\033[0m");
			problems += (int)missing.size();
		} else {
			util::outln("  Enregistrement dans les module.toml…");
			int fixed = apply_missing_deps(c.p, missing);
			if (fixed == 0) {
				util::outln("  aucune modification (fichiers non éditables ou déjà à jour)");
				problems += (int)missing.size();
			} else {
				util::outln("  \033[1;32m{} module(s) mis à jour\033[0m "
							"— relancez 'cpp-maker doctor' pour confirmer", fixed);
			}
		}
	}

	if (problems == 0) util::outln("\033[1;32mAucun problème détecté.\033[0m");
	else util::outln("\033[1;31m{} problème(s) détecté(s).\033[0m", problems);
	return problems == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// config show | validate | set
// ---------------------------------------------------------------------------
// cpp-maker config set <clé> <valeur>
// Modifie cpp-maker.toml de manière contrôlée : la clé doit exister, la
// nouvelle configuration est validée AVANT un remplacement atomique du
// fichier (écriture temporaire + rename), donc une valeur invalide ne
// peut jamais corrompre le fichier existant.
inline int cmd_config_set(const std::vector<std::string>& args) {
	if (args.size() != 3) util::die("usage : cpp-maker config set <clé> <valeur>");
	const std::string& key = args[1];
	const std::string& val = args[2];

	static const std::set<std::string> allowed = {
		"project.name", "project.version", "project.type", "project.standard", "project.c_standard",
		"project.description", "module.type", "compiler.cxx", "compiler.cc", "compiler.family",
		"build.default_mode", "build.default_toolchain", "build.jobs",
	};
	static const std::set<std::string> integers = {"build.jobs"};
	if (!allowed.count(key)) {
		std::vector<std::string> v(allowed.begin(), allowed.end());
		util::die(std::format("clé '{}' non modifiable via 'config set' (autorisées : {})",
							  key, util::join(v, ", ")));
	}

	auto root = find_root(fs::current_path());
	if (!root) util::die("cpp-maker.toml introuvable : lancez 'cpp-maker new' d'abord");
	fs::path file = *root / "cpp-maker.toml";

	std::ifstream in(file);
	if (!in) util::die("impossible de lire " + file.string());
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(in, line)) lines.push_back(line);

	bool replaced = false;
	std::string cur_table;
	for (auto& ln : lines) {
		std::string t = util::trim(ln);
		if (t.empty() || t[0] == '#') continue;
		if (t.front() == '[' && t.find(']') != std::string::npos) {
			cur_table = util::trim(t.substr(1, t.find(']') - 1));
			continue;
		}
		auto eq = t.find('=');
		if (eq == std::string::npos) continue;
		std::string k = util::trim(t.substr(0, eq));
		if (cur_table + "." + k != key) continue;

		std::string newval;
		if (integers.count(key)) {
			if (val.empty() || !std::all_of(val.begin(), val.end(),
											[](unsigned char ch) { return std::isdigit(ch); }))
				util::die(std::format("valeur invalide pour {} : '{}'", key, val));
			newval = val;
		} else {
			std::string esc;
			for (char ch : val) {
				if (ch == '"' || ch == '\\') esc += '\\';
				esc += ch;
			}
			newval = std::format("\"{}\"", esc);
		}
		size_t indent = ln.find_first_not_of(" \t");
		ln = std::format("{}{} = {}", std::string(indent, ' '), k, newval);
		replaced = true;
	}
	if (!replaced) util::die(std::format("clé '{}' introuvable dans {}", key, file.string()));

	std::string content;
	for (auto& l : lines) content += l + "\n";
	fs::path tmp = file;
	tmp += ".tmp";
	util::write_file(tmp, content);
	std::error_code ec;
	try {
		auto p = cfg::load(*root, tmp);
		p.file = file;
		std::string e = cfg::validate_errors(p);
		if (!e.empty()) throw std::runtime_error(e);
	} catch (const std::exception& e) {
		fs::remove(tmp, ec);
		util::die(std::format("valeur refusée, fichier inchangé : {}", e.what()));
	}
	fs::rename(tmp, file, ec);
	if (ec) { fs::remove(tmp, ec); util::die("échec du remplacement de " + file.string()); }
	util::outln("\033[1;32m{} = {}\033[0m (configuration validée)", key, val);
	return 0;
}

inline int cmd_config_show(const std::vector<std::string>& filters) {
	auto c = load_ctx(false);
	auto& p = c.p;

	// Chaque entrée est (clé pointée, valeur formatée). Scalaires entre
	// guillemets (comme dans cpp-maker.toml), tableaux entre crochets.
	// Les champs optionnels vides sont omis ; les champs structurants
	// (project.*, build.warnings…) sont toujours présents.
	std::vector<std::pair<std::string, std::string>> entries;
	auto add = [&](std::string k, std::string v) {
		entries.emplace_back(std::move(k), std::move(v));
	};
	auto s_ = [](std::string_view x) { return std::format("\"{}\"", x); };
	auto l_ = [](const std::vector<std::string>& v) {
		std::string r = "[";
		for (size_t i = 0; i < v.size(); ++i) r += (i ? ", \"" : "\"") + v[i] + "\"";
		return r + "]";
	};
	auto b_ = [](bool x) { return std::string(x ? "true" : "false"); };

	add("project.name", s_(p.name));
	add("project.version", s_(p.version));
	add("project.description", s_(p.description));
	add("project.standard", s_(p.cxx_standard));
	add("project.c_standard", s_(p.c_standard));
	if (!p.min_version.empty()) add("project.minimum_cpp_maker_version", s_(p.min_version));

	add("build.default_mode", s_(p.default_mode));
	add("build.default_toolchain", s_(p.default_toolchain));
	add("build.jobs", std::to_string(p.jobs));
	add("build.static_pic", b_(p.static_pic));
	add("build.warnings", l_(p.warnings));
	if (!p.cflags.empty())   add("build.cflags", l_(p.cflags));
	if (!p.cxxflags.empty()) add("build.cxxflags", l_(p.cxxflags));
	if (!p.ldflags.empty())  add("build.ldflags", l_(p.ldflags));
	if (!p.defines.empty())  add("build.defines", l_(p.defines));

	for (auto& [n, t] : p.toolchains) {
		const std::string pre = "toolchains." + n + ".";
		if (!t.prefix.empty())     add(pre + "prefix", s_(t.prefix));
		add(pre + "cc", s_(t.cc));
		add(pre + "cxx", s_(t.cxx));
		if (!t.ar.empty())         add(pre + "ar", s_(t.ar));
		if (!t.strip.empty())      add(pre + "strip", s_(t.strip));
		if (!t.pkg_config.empty()) add(pre + "pkg_config", s_(t.pkg_config));
		add(pre + "family", s_(t.family));
		if (!t.target.empty())     add(pre + "target", s_(t.target));
		if (!t.sysroot.empty())    add(pre + "sysroot", s_(t.sysroot));
		if (!t.os.empty())         add(pre + "os", s_(t.os));
		if (!t.cflags.empty())     add(pre + "cflags", l_(t.cflags));
		if (!t.cxxflags.empty())   add(pre + "cxxflags", l_(t.cxxflags));
		if (!t.ldflags.empty())    add(pre + "ldflags", l_(t.ldflags));
		if (t.runner_set)          add(pre + "runner", l_(t.runner));
	}

	for (auto& m : p.modes) {
		const std::string pre = "modes." + m.name + ".";
		add(pre + "flags", l_(m.flags));
		if (!m.link_flags.empty())  add(pre + "link_flags", l_(m.link_flags));
		if (!m.defines.empty())     add(pre + "defines", l_(m.defines));
		if (!m.sanitizer.empty())   add(pre + "sanitizer", s_(m.sanitizer));
		if (!m.runner.empty())      add(pre + "runner", s_(m.runner));
		if (!m.runner_args.empty()) add(pre + "runner_args", l_(m.runner_args));
	}

	add("tests.timeout", std::to_string(p.test_timeout));
	for (auto& [k, v] : p.test_env) add("tests.env." + k, s_(v));

	for (auto& g : p.generate_steps) {
		const std::string pre = "generate." + g.name + ".";
		add(pre + "command", s_(g.command));
		if (!g.inputs.empty())  add(pre + "inputs", l_(g.inputs));
		if (!g.outputs.empty()) add(pre + "outputs", l_(g.outputs));
	}

	if (!p.layer_order.empty()) add("layers.order", l_(p.layer_order));
	for (auto& [layer, forbid] : p.layer_forbid_pkg)
		add("layers." + layer + ".forbid_pkg_config", l_(forbid));

	auto& pk = p.package;
	add("package.formats", l_(pk.formats));
	add("package.strip", b_(pk.strip));
	add("package.maintainer", s_(pk.maintainer));
	if (!pk.description.empty())  add("package.description", s_(pk.description));
	if (!pk.homepage.empty())     add("package.homepage", s_(pk.homepage));
	if (!pk.license.empty())      add("package.license", s_(pk.license));
	if (!pk.deb_depends.empty())  add("package.deb_depends", l_(pk.deb_depends));
	add("package.deb_prefix", s_(pk.deb_prefix));
	if (!pk.extra_files.empty())  add("package.extra_files", l_(pk.extra_files));
	add("package.checksums", b_(pk.checksums));

	for (auto& [n, d] : p.deploys) {
		const std::string pre = "deploy." + n + ".";
		add(pre + "method", s_(d.method));
		if (!d.host.empty())        add(pre + "host", s_(d.host));
		if (!d.user.empty())        add(pre + "user", s_(d.user));
		if (!d.path.empty())        add(pre + "path", s_(d.path));
		if (d.port)                 add(pre + "port", std::to_string(d.port));
		if (!d.toolchain.empty())   add(pre + "toolchain", s_(d.toolchain));
		add(pre + "mode", s_(d.mode));
		add(pre + "artifact", s_(d.artifact));
		if (!d.modules.empty())     add(pre + "modules", l_(d.modules));
		if (!d.ssh_options.empty()) add(pre + "ssh_options", l_(d.ssh_options));
		if (!d.before.empty())      add(pre + "before", l_(d.before));
		if (!d.after.empty())       add(pre + "after", l_(d.after));
		if (!d.after_local.empty()) add(pre + "after_local", l_(d.after_local));
		if (!d.script.empty())      add(pre + "script", s_(d.script));
		if (d.delete_extra)         add(pre + "delete", "true");
	}

	if (p.workspace) add("workspace.modules", l_(p.module_paths));

	for (auto& m : p.modules) {
		const std::string pre = "module." + m.name + ".";
		add(pre + "type", s_(m.type));
		if (!m.version.empty()) add(pre + "version", s_(m.version));
		add(pre + "dir", s_(m.dir.generic_string()));
		add(pre + "source_dirs", l_(m.source_dirs));
		if (!m.source_files.empty()) add(pre + "source_files", l_(m.source_files));
		if (!m.exclude.empty())      add(pre + "exclude", l_(m.exclude));
		if (!m.main.empty())         add(pre + "main", s_(m.main));
		if (!m.layer.empty())        add(pre + "layer", s_(m.layer));
		add(pre + "public_includes", l_(m.public_includes));
		if (m.private_includes != m.source_dirs)
			add(pre + "private_includes", l_(m.private_includes));
		if (!m.deps.empty())         add(pre + "dependencies.modules", l_(m.deps));
		if (!m.pkg_config.empty())   add(pre + "dependencies.pkg_config", l_(m.pkg_config));
		if (!m.system_libs.empty())  add(pre + "dependencies.system", l_(m.system_libs));
		if (!m.defines.empty())         add(pre + "flags.defines", l_(m.defines));
		if (!m.public_defines.empty())  add(pre + "flags.public_defines", l_(m.public_defines));
		if (!m.cflags.empty())          add(pre + "flags.cflags", l_(m.cflags));
		if (!m.cxxflags.empty())        add(pre + "flags.cxxflags", l_(m.cxxflags));
		if (!m.ldflags.empty())         add(pre + "flags.ldflags", l_(m.ldflags));
		add(pre + "install", b_(m.install));
	}

	// Sélection : chaque filtre matche la clé exacte et/ou son préfixe
	// suivi de '.' (build.warnings, toolchains.host, modes.release…).
	std::vector<std::pair<std::string, std::string>> selected;
	std::vector<std::string> unknown;
	if (filters.empty()) {
		selected = entries;
	} else {
		std::set<std::pair<std::string, std::string>> seen;
		for (auto& f : filters) {
			std::string prefix = f.empty() ? f : f + ".";
			bool any = false;
			for (auto& e : entries) {
				if (e.first == f || e.first.starts_with(prefix)) {
					if (seen.insert(e).second) selected.push_back(e);
					any = true;
				}
			}
			if (!any) unknown.push_back(f);
		}
		if (filters.size() > 1)
			std::sort(selected.begin(), selected.end(),
					  [](auto& a, auto& b) { return a.first < b.first; });
	}

	// Suggestion pour les clés inconnues (« nam » → « project.name »).
	for (auto& f : unknown) {
		std::vector<std::string> sugg;
		for (auto& e : entries)
			if (e.first.size() > f.size() && e.first.ends_with("." + f))
				sugg.push_back(e.first);
		util::errln("\033[1;33mwarning:\033[0m clé inconnue : '{}'{}", f,
					sugg.empty() ? "" : " (essayez : " + util::join(sugg, ", ") + ")");
	}

	if (selected.empty()) {
		if (filters.empty()) util::outln("(configuration vide)");
		return filters.empty() ? 0 : 1;
	}

	size_t w = 0;
	for (auto& [k, v] : selected) w = std::max(w, k.size());
	for (auto& [k, v] : selected) util::outln("{:<{}} = {}", k, w, v);

	return unknown.empty() ? 0 : 1;
}

inline int cmd_config(const std::vector<std::string>& args) {
	if (args.empty()) util::die("usage : cpp-maker config <show [clé…]|validate|set>");
	if (args[0] == "show")
		return cmd_config_show({args.begin() + 1, args.end()});
	if (args[0] == "validate") {
		auto c = load_ctx(false);
		auto e = cfg::validate_errors(c.p);
		if (!e.empty()) { util::errln("\033[1;31m{}\033[0m", e); return 1; }
		util::outln("\033[1;32mConfiguration valide.\033[0m ({} module(s))", c.p.modules.size());
		return 0;
	}
	if (args[0] == "set") return cmd_config_set(args);
	util::die("sous-commande config inconnue : " + args[0]);
}

// ---------------------------------------------------------------------------
// toolchain : profils, détection, gestion des versions gcc/g++
// ---------------------------------------------------------------------------
inline bool is_debian_like() {
	return fs::exists("/etc/debian_version") && fs::exists("/usr/bin/apt-get");
}

inline std::vector<int> list_installed_gcc_versions() {
	std::set<int> versions;
	std::vector<fs::path> dirs;
	if (const char* path_env = std::getenv("PATH"))
		for (auto& d : util::split_list(path_env, ':')) dirs.emplace_back(d);
	dirs.emplace_back("/usr/bin");
	for (auto& dir : dirs) {
		std::error_code ec;
		if (!fs::is_directory(dir, ec)) continue;
		for (auto& e : fs::directory_iterator(dir, ec)) {
			const auto name = e.path().filename().string();
			auto parse = [&](std::string_view prefix) -> int {
				if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) return -1;
				int v = 0;
				for (char ch : name.substr(prefix.size())) {
					if (!std::isdigit((unsigned char)ch)) return -1;
					v = v * 10 + (ch - '0');
				}
				return v;
			};
			int v = parse("gcc-");
			if (v <= 0) v = parse("g++-");
			if (v > 0) versions.insert(v);
		}
	}
	return {versions.begin(), versions.end()};
}

// Résout /usr/bin/gcc → … → /usr/bin/gcc-N, en suivant les symlinks.
inline std::optional<int> resolve_gcc_version(const fs::path& link) {
	std::error_code ec;
	if (!fs::exists(link, ec)) return std::nullopt;
	auto canon = fs::canonical(link, ec);
	if (ec) return std::nullopt;
	auto name = canon.filename().string();
	for (auto prefix : {std::string_view("gcc-"), std::string_view("g++-")}) {
		if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
			int v = 0;
			bool ok = true;
			for (char ch : name.substr(prefix.size())) {
				if (!std::isdigit((unsigned char)ch)) { ok = false; break; }
				v = v * 10 + (ch - '0');
			}
			if (ok && v > 0) return v;
		}
	}
	return std::nullopt;
}

inline int run_as_root_rc(const std::vector<std::string>& argv) {
#ifdef __unix__
	bool is_root = (::geteuid() == 0);
#else
	bool is_root = false;
#endif
	std::vector<std::string> full;
	if (!is_root) {
		if (!util::have("sudo")) {
			util::errln("privilèges root ou 'sudo' requis pour : {}", util::join(argv, " "));
			return 127;
		}
		full.push_back("sudo");
	}
	full.insert(full.end(), argv.begin(), argv.end());
	auto r = proc::capture(full);
	if (r.exit_code != 0 && !r.output.empty()) std::cerr << r.output << '\n';
	return r.exit_code;
}

inline void run_as_root(const std::vector<std::string>& argv) {
	int rc = run_as_root_rc(argv);
	if (rc != 0) util::die(std::format("commande échouée (code {}) : {}", rc, util::join(argv, " ")));
}

inline bool confirm(const std::string& question, bool auto_yes) {
	if (auto_yes) return true;
	util::out("{} [y/N] ", question);
	std::cout.flush();
	std::string line;
	if (!std::getline(std::cin, line)) return false;
	return !line.empty() && (line[0] == 'y' || line[0] == 'Y');
}

inline int parse_version(const std::string& s) {
	int v = parse_int_arg("version", s);
	if (v <= 0 || v > 99) util::die(std::format("version invalide : '{}'", s));
	return v;
}

// Lit la version majeure réellement utilisée par un compilateur.
inline std::optional<int> gcc_major_version(const std::string& cxx) {
	auto r = proc::capture({cxx, "-dumpversion"});
	if (r.exit_code != 0) return std::nullopt;
	auto out = util::trim(r.output);
	if (out.empty()) return std::nullopt;
	try { return std::stoi(out); } catch (...) { return std::nullopt; }
}

inline int cmd_toolchain(const std::vector<std::string>& args) {
	if (args.empty() || args[0] == "list" || args[0] == "ls") {
		std::optional<Project> p;
		if (auto root = find_root(fs::current_path())) {
			try { p = cfg::load(*root, *root / "cpp-maker.toml"); } catch (...) {}
		}
		util::outln("\033[1;36mToolchains du projet\033[0m");
		if (!p || p->toolchains.empty()) util::outln("  (aucun profil — 'host' implicite)");
		else for (auto& [n, t] : p->toolchains) {
			std::string what = !t.prefix.empty() ? "prefix " + t.prefix
							 : std::format("cc {} · cxx {}", t.cc, t.cxx);
			if (!t.target.empty()) what += " · target " + t.target;
			util::outln("  {:<12} {}{}", n, what, p->default_toolchain == n ? "  \033[1;32m(défaut)\033[0m" : "");
		}
		util::outln("\033[1;36mToolchains intégrées\033[0m");
		util::outln("  host         compilateur natif (CC/CXX, gcc, clang…)");
		util::outln("  gcc          gcc / g++");
		util::outln("  clang        clang / clang++");
		auto pre = toolchain::detect_prefixes();
		util::outln("\033[1;36mCompilateurs croisés détectés\033[0m (-t <triplet>)");
		if (pre.empty()) util::outln("  (aucun)");
		for (auto& x : pre) {
			std::string tools;
			for (auto t : {"gcc", "g++"}) if (util::have(x + t)) tools += std::string(tools.empty() ? "" : ", ") + x + t;
			util::outln("  {:<28} {}", x.substr(0, x.size() - 1), tools);
		}

		auto versions = list_installed_gcc_versions();
		auto cur_gcc = resolve_gcc_version("/usr/bin/gcc");
		auto cur_gxx = resolve_gcc_version("/usr/bin/g++");
		util::outln("\033[1;36mVersions gcc/g++ de l'hôte\033[0m");
		if (versions.empty()) util::outln("  (aucune)");
		for (int v : versions)
			util::outln("  gcc/g++-{}{}", v, (cur_gcc == v || cur_gxx == v) ? "  \033[1;32m(défaut)\033[0m" : "");
		if (cur_gcc) util::outln("  /usr/bin/gcc → gcc-{}", *cur_gcc);
		if (cur_gxx) util::outln("  /usr/bin/g++ → g++-{}", *cur_gxx);
		return 0;
	}

	const std::string& sub = args[0];

	if (sub == "show") {
		auto c = load_ctx(false);
		std::string n = args.size() > 1 ? args[1] : c.p.default_toolchain;
		auto t = toolchain::resolve(c.p, n);
		util::outln("\033[1;36mToolchain {}\033[0m", t.name);
		print_toolchain(t);
		util::outln("  pkg-config: {}", t.pkg_config);
		util::outln("  build dir : build/{}/<mode>", t.name);
		return 0;
	}

	if (sub == "which") {
		auto c = load_ctx(false);
		auto t = toolchain::resolve(c.p, args.size() > 1 ? args[1] : c.p.default_toolchain);
		if (t.cxx) util::outln("{}", t.cxx->exe);
		if (t.cc) util::outln("{}", t.cc->exe);
		return 0;
	}

	if (sub == "use") {
		if (args.size() < 2) util::die("usage : cpp-maker toolchain use <version>");
		int v = parse_version(args[1]);
		fs::path gcc = std::format("/usr/bin/gcc-{}", v);
		fs::path gxx = std::format("/usr/bin/g++-{}", v);
		if (!fs::exists(gcc) || !fs::exists(gxx))
			util::die(std::format("gcc-{0}/g++-{0} introuvables — "
								  "installez-les avec `cpp-maker toolchain install {0}`", v));
		util::outln("Configuration de gcc-{0}/g++-{0} comme version par défaut…", v);
		int rc_install = run_as_root_rc({"update-alternatives", "--install", "/usr/bin/gcc", "gcc",
										 gcc.string(), "200", "--slave", "/usr/bin/g++", "g++", gxx.string()});
		if (rc_install != 0) {
			run_as_root({"update-alternatives", "--install", "/usr/bin/gcc", "gcc", gcc.string(), "200"});
			(void)run_as_root_rc({"update-alternatives", "--install", "/usr/bin/g++", "g++", gxx.string(), "200"});
		}
		int rc_set_gcc = run_as_root_rc({"update-alternatives", "--set", "gcc", gcc.string()});
		int rc_set_gxx = run_as_root_rc({"update-alternatives", "--set", "g++", gxx.string()});
		auto used = gcc_major_version("g++");
		if (!used || *used != v) {
			util::errln("\033[1;31mÉchec : la version active reste g++-{}.\033[0m",
						used ? std::to_string(*used) : std::string("?"));
			util::errln("  --set gcc rc = {}, --set g++ rc = {}", rc_set_gcc, rc_set_gxx);
			auto q = proc::capture({"update-alternatives", "--display", "gcc"});
			if (!q.output.empty()) std::cerr << q.output;
			return 1;
		}
		util::outln("\033[1;32mDéfaut mis à jour.\033[0m g++ → g++-{}", v);
		return 0;
	}

	if (sub == "install") {
		if (args.size() < 2) util::die("usage : cpp-maker toolchain install <version|triplet>");
		bool yes = std::any_of(args.begin(), args.end(), [](auto& a) { return a == "-y" || a == "--yes"; });
		if (!is_debian_like())
			util::die("l'installation automatique n'est supportée que sur Debian/Ubuntu (apt)");
		// Compilateur croisé : cpp-maker toolchain install arm-linux-gnueabihf
		if (!std::isdigit((unsigned char)args[1][0])) {
			std::string triple = args[1];
			if (!util::is_safe_name(triple)) util::die("triplet invalide : " + triple);
			std::vector<std::string> pkgs = {"gcc-" + triple, "g++-" + triple};
			if (triple.starts_with("x86_64-w64-mingw32") || triple.starts_with("i686-w64-mingw32"))
				pkgs = {"mingw-w64"};
			if (!confirm(std::format("Installer {} (+ qemu-user) ?", util::join(pkgs, " ")), yes)) return 1;
			run_as_root({"apt-get", "update"});
			std::vector<std::string> cmd = {"apt-get", "install", "-y"};
			cmd.insert(cmd.end(), pkgs.begin(), pkgs.end());
			if (!triple.starts_with("x86_64-w64")) cmd.push_back("qemu-user");
			run_as_root(cmd);
			util::outln("\033[1;32m{} installé.\033[0m Utilisation : cpp-maker build -t {}", triple, triple);
			return 0;
		}
		int v = parse_version(args[1]);
		if (!confirm(std::format("Ajouter le PPA ubuntu-toolchain-r/test (si nécessaire) et installer "
								 "gcc-{0} g++-{0} ?", v), yes))
			return 1;
		if (!util::have("add-apt-repository")) {
			util::outln("Installation de software-properties-common…");
			run_as_root({"apt-get", "install", "-y", "software-properties-common"});
		}
		util::outln("Ajout du PPA ubuntu-toolchain-r/test…");
		run_as_root({"add-apt-repository", "ppa:ubuntu-toolchain-r/test", "-y"});
		util::outln("Mise à jour de l'index apt…");
		run_as_root({"apt-get", "update"});
		util::outln("Installation de gcc-{0} et g++-{0}…", v);
		run_as_root({"apt-get", "install", "-y", std::format("gcc-{}", v), std::format("g++-{}", v)});
		util::outln("\033[1;32mgcc-{0}/g++-{0} installés.\033[0m", v);
		util::outln("Pour les activer par défaut : cpp-maker toolchain use {}", v);
		return 0;
	}

	if (sub == "remove" || sub == "uninstall" || sub == "purge") {
		if (args.size() < 2) util::die("usage : cpp-maker toolchain remove <version>");
		int v = parse_version(args[1]);
		if (!is_debian_like())
			util::die("la suppression automatique n'est supportée que sur Debian/Ubuntu (apt)");
		auto cur_gcc = resolve_gcc_version("/usr/bin/gcc");
		auto cur_gxx = resolve_gcc_version("/usr/bin/g++");
		if (cur_gcc == v || cur_gxx == v)
			util::die(std::format("gcc-{0}/g++-{0} est actuellement le défaut. "
								  "Changez d'abord avec `cpp-maker toolchain use <autre>`", v));
		bool yes = std::any_of(args.begin(), args.end(), [](auto& a) { return a == "-y" || a == "--yes"; });
		if (!confirm(std::format("Purger gcc-{0} et g++-{0} (avec --autoremove) ?", v), yes)) return 1;
		run_as_root({"apt-get", "purge", "--autoremove", "-y", std::format("gcc-{}", v), std::format("g++-{}", v)});
		util::outln("\033[1;32mgcc-{0}/g++-{0} supprimés.\033[0m", v);
		return 0;
	}

	util::die("sous-commande toolchain inconnue : " + sub +
			  " (attendu : list | show | which | use | install | remove)");
}

// ---------------------------------------------------------------------------
// new / add : génération de projets et de modules
// ---------------------------------------------------------------------------
inline std::string ident(const std::string& name) {
	std::string r;
	for (char ch : name) r += std::isalnum((unsigned char)ch) ? ch : '_';
	if (r.empty() || std::isdigit((unsigned char)r[0])) r = "_" + r;
	return r;
}
inline std::string upper(std::string s) {
	for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch);
	return s;
}

inline std::string toml_array(const std::vector<std::string>& v) {
	std::string r = "[";
	for (size_t i = 0; i < v.size(); ++i) r += (i ? ", \"" : "\"") + v[i] + "\"";
	return r + "]";
}

inline const char* TEST_HPP = R"TPL(#pragma once
// Mini-framework de tests généré par cpp-maker.
//
// Deux styles acceptés :
//   TEST(Nom)          { CHECK(x); CHECK_EQ(a, b); REQUIRE(ptr); }
//   TEST(Suite, Nom)   { EXPECT_TRUE(x); EXPECT_EQ(a, b); ASSERT_TRUE(ptr); }
// CHECK/EXPECT_* continuent le test en cas d'échec ; REQUIRE/ASSERT_*
// l'arrêtent (return). SKIP("raison") marque le test comme ignoré (ex. pas
// d'affichage ou de GPU disponible) ; si tous les tests exécutés sont
// ignorés, l'exécutable sort avec le code 77 (test ignoré pour cpp-maker).
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase { std::string name; std::function<void()> fn; };

inline std::vector<TestCase>& test_registry() {
	static std::vector<TestCase> r;
	return r;
}
inline int& test_failures() {
	static int f = 0;
	return f;
}
inline bool& test_skipped() {
	static bool s = false;
	return s;
}

struct TestRegistrar {
	TestRegistrar(const std::string& n, std::function<void()> f) {
		test_registry().push_back({n, std::move(f)});
	}
};

inline void test_report(const char* kind, const char* expr, const char* file, int line) {
	std::fprintf(stderr, "\n  %s failed: %s (%s:%d)", kind, expr, file, line);
	++test_failures();
}

namespace test {
/// Signale un échec depuis du code de test (compatibilité anciens tests).
inline void ReportFailure(const char* file, int line, const char* cond) {
	test_report("Failure", cond, file, line);
}
} // namespace test

#define TEST_CAT_(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT_(a, b)
#define TEST_1_(name) \
	static void test_fn_##name(); \
	static TestRegistrar test_reg_##name(#name, test_fn_##name); \
	static void test_fn_##name()
#define TEST_2_(suite, name) \
	static void test_fn_##suite##_##name(); \
	static TestRegistrar test_reg_##suite##_##name(#suite "." #name, test_fn_##suite##_##name); \
	static void test_fn_##suite##_##name()
#define TEST_PICK_(_1, _2, M, ...) M
#define TEST(...) TEST_PICK_(__VA_ARGS__, TEST_2_, TEST_1_, unused)(__VA_ARGS__)

#define CHECK(cond) do { \
	if (!(cond)) test_report("CHECK", #cond, __FILE__, __LINE__); \
} while (0)

#define CHECK_EQ(a, b) do { \
	if (!((a) == (b))) test_report("CHECK_EQ", #a " == " #b, __FILE__, __LINE__); \
} while (0)

#define CHECK_NE(a, b) do { \
	if ((a) == (b)) test_report("CHECK_NE", #a " != " #b, __FILE__, __LINE__); \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
	if (!(std::fabs(double(a) - double(b)) <= double(eps))) \
		test_report("CHECK_NEAR", #a " ~ " #b, __FILE__, __LINE__); \
} while (0)

#define REQUIRE(cond) do { \
	if (!(cond)) { test_report("REQUIRE", #cond, __FILE__, __LINE__); return; } \
} while (0)

#define REQUIRE_EQ(a, b) do { \
	if (!((a) == (b))) { test_report("REQUIRE_EQ", #a " == " #b, __FILE__, __LINE__); return; } \
} while (0)

#define SKIP(reason) do { \
	std::fprintf(stderr, "\n  skipped: %s", reason); \
	test_skipped() = true; \
	return; \
} while (0)

// Style GTest (compatibilité avec les anciens tests du projet).
#define EXPECT_TRUE(c)   CHECK(c)
#define EXPECT_FALSE(c)  CHECK(!(c))
#define EXPECT_EQ(a, b)  CHECK_EQ(a, b)
#define EXPECT_NE(a, b)  CHECK_NE(a, b)
#define ASSERT_TRUE(c)   REQUIRE(c)
#define ASSERT_FALSE(c)  REQUIRE(!(c))
#define ASSERT_EQ(a, b)  REQUIRE_EQ(a, b)
)TPL";

inline const char* TEST_RUNNER_CPP = R"TPL(#include "test.hpp"

int main(int argc, char** argv) {
	std::string filter;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a == "--filter" && i + 1 < argc) filter = argv[++i];
	}
	int failed = 0, skipped = 0, total = 0;
	for (auto& tc : test_registry()) {
		if (!filter.empty() && tc.name.find(filter) == std::string::npos) continue;
		++total;
		int before = test_failures();
		test_skipped() = false;
		std::printf("  \033[0;33m[ RUN  ]\033[0m %s", tc.name.c_str());
		std::fflush(stdout);
		tc.fn();
		if (test_failures() > before) {
			std::printf("\r  \033[1;31m[ FAIL ]\033[0m %s\n", tc.name.c_str());
			++failed;
		} else if (test_skipped()) {
			std::printf("\r  \033[1;33m[ SKIP ]\033[0m %s\n", tc.name.c_str());
			++skipped;
		} else {
			std::printf("\r  \033[1;32m[ PASS ]\033[0m %s\n", tc.name.c_str());
		}
	}
	std::printf("\n%d/%d tests passed", total - failed - skipped, total);
	if (skipped) std::printf(", %d skipped", skipped);
	std::printf("\n");
	if (failed > 0) return 1;
	return (total > 0 && skipped == total) ? 77 : 0;
}
)TPL";

// Écrit les sources d'un module. `uses` : bibliothèque dont main() appelle add().
inline void write_module_sources(const fs::path& dir, const std::string& name, const std::string& type,
								 bool c_lang, const std::string& uses = {}) {
	std::string id = ident(name);
	std::string guard = upper(id) + "_H";
	if (c_lang) {
		util::write_file(dir / "include" / name / (name + ".h"), std::format(
R"(#ifndef {0}
#define {0}

#ifdef __cplusplus
extern "C" {{
#endif

{1}int {2}_add(int a, int b){3}

#ifdef __cplusplus
}}
#endif

#endif /* {0} */
)", guard, type == "header-only" ? "static inline " : "", id,
			type == "header-only" ? " { return a + b; }" : ";"));
		if (type != "header-only")
			util::write_file(dir / "src" / (name + ".c"), std::format(
R"(#include "{0}/{0}.h"

int {1}_add(int a, int b) {{
	return a + b;
}}
)", name, id));
		if (type == "executable") {
			std::string inc = uses.empty() ? name : uses;
			util::write_file(dir / "src" / "main.c", std::format(
R"(#include <stdio.h>
#include "{0}/{0}.h"

int main(void) {{
	printf("Hello from {1} — 2 + 3 = %d\n", {2}_add(2, 3));
	return 0;
}}
)", inc, name, ident(inc)));
		}
		if (type != "executable" || uses.empty())
			util::write_file(dir / "tests" / ("test_" + name + ".c"), std::format(
R"(#include <stdio.h>
#include "{0}/{0}.h"

static int failures = 0;
#define CHECK(c) do {{ if (!(c)) {{ \
	fprintf(stderr, "  CHECK failed: %s (%s:%d)\n", #c, __FILE__, __LINE__); ++failures; }} }} while (0)

int main(void) {{
	CHECK({1}_add(2, 3) == 5);
	CHECK({1}_add(-1, 1) == 0);
	printf("{0} : %s\n", failures ? "FAIL" : "OK");
	return failures ? 1 : 0;
}}
)", name, id));
		return;
	}
	util::write_file(dir / "include" / name / (name + ".hpp"), std::format(
R"(#pragma once

namespace {0} {{

{1}int add(int a, int b){2}

}} // namespace {0}
)", id, type == "header-only" ? "inline " : "", type == "header-only" ? " { return a + b; }" : ";"));
	if (type != "header-only")
		util::write_file(dir / "src" / (name + ".cpp"), std::format(
R"(#include "{0}/{0}.hpp"

namespace {1} {{

int add(int a, int b) {{
	return a + b;
}}

}} // namespace {1}
)", name, id));
	if (type == "executable") {
		std::string inc = uses.empty() ? name : uses;
		util::write_file(dir / "src" / "main.cpp", std::format(
R"(#include <iostream>
#include "{0}/{0}.hpp"

int main() {{
	std::cout << "Hello from {1} — 2 + 3 = " << {2}::add(2, 3) << '\n';
	return 0;
}}
)", inc, name, ident(inc)));
	}
	if (type != "executable" || uses.empty()) {
		util::write_file(dir / "tests" / "test.hpp", TEST_HPP);
		util::write_file(dir / "tests" / "test_runner.cpp", TEST_RUNNER_CPP);
		util::write_file(dir / "tests" / ("test_" + name + ".cpp"), std::format(
R"(#include "test.hpp"
#include "{0}/{0}.hpp"

TEST(add_works) {{ CHECK_EQ({1}::add(2, 3), 5); }}
TEST(add_neg)   {{ CHECK_EQ({1}::add(-1, 1), 0); }}
)", name, id));
	}
}

inline std::string module_toml(const std::string& name, const std::string& type,
							   const std::vector<std::string>& deps) {
	return std::format(R"([module]
name = "{}"
type = "{}"              # executable | static | shared | header-only

[sources]
dirs = ["src"]
# exclude = ["src/experimental/**"]

[include]
public = ["include"]      # propagé aux modules dépendants
# private = ["src"]

[dependencies]
modules = {}
pkg_config = []           # ex. ["zlib", "libcurl"]
system = []               # ex. ["m", "pthread"]

[flags]
defines = []
public_defines = []
cflags = []
cxxflags = []

[tests]
dirs = ["tests"]

# Surcharges par plate-forme cible (linux | windows | macos | android | unix…)
# [platform.windows]
# system = ["ws2_32"]
)", name, type, toml_array(deps));
}

inline std::string toolchains_toml() {
	std::string s = R"(# Toolchains : cpp-maker build -t <nom>[,<nom>…] (ou -t all).
# Sans profil, tout préfixe de triplet du PATH fonctionne : -t arm-linux-gnueabihf
[toolchains.host]
cc = "auto"
cxx = "auto"
)";
	struct Known { const char* name; const char* prefix; const char* runner; const char* comment; };
	for (auto k : {Known{"armhf", "arm-linux-gnueabihf-", "", "ARM 32 bits (Raspberry Pi…)"},
				   Known{"aarch64", "aarch64-linux-gnu-", "", "ARM 64 bits"},
				   Known{"win64", "x86_64-w64-mingw32-", "", "Windows 64 bits (MinGW-w64)"}}) {
		bool found = util::have(std::string(k.prefix) + "gcc") || util::have(std::string(k.prefix) + "g++");
		std::string c = found ? "" : "# ";
		s += std::format("\n# {}{}\n{}[toolchains.{}]\n{}prefix = \"{}\"\n", k.comment,
						 found ? " — détectée" : "", c, k.name, c, k.prefix);
		if (std::string(k.name) != "win64")
			s += "# sysroot = \"/usr/" + std::string(k.prefix).substr(0, std::strlen(k.prefix) - 1) + "\"\n";
		s += "# runner = []          # émulateur (auto : qemu-*/wine), [] = désactivé\n";
	}
	s += R"(
# Clang en compilation croisée :
# [toolchains.clang-arm64]
# cc = "clang"
# cxx = "clang++"
# target = "aarch64-linux-gnu"
)";
	return s;
}

inline std::string common_toml_tail() {
	return R"(
[build]
default_mode = "debug"
default_toolchain = "host"
jobs = 0  # 0 = cœurs - 1 (cpp-maker en réserve un)
warnings = [
	"-Wall",
	"-Wextra",
	"-Wpedantic",
	"-Wformat=2",
	"-Wshadow",
	"-Wconversion",
	"-Wsign-conversion",
	"-Wdouble-promotion",
	"-Wundef",
	"-Wcast-align",
	"-Wcast-qual",
	"-Woverloaded-virtual",
	"-Wnon-virtual-dtor"
]

# Modes : release, debug, relwithdebinfo, minsize, asan, tsan, ubsan, valgrind
# sont prédéfinis ; on peut les redéfinir ou en ajouter (base = "<mode>").
[build.modes.release]
flags = ["-O2"]
defines = ["NDEBUG"]

[build.modes.debug]
flags = ["-O0", "-g3"]
defines = ["DEBUG"]

[build.modes.asan]
base = "debug"
sanitizer = "address"

[tests]
timeout = 300                  # secondes par exécutable de test

[package]
formats = ["tar.gz"]           # tar.gz | tar.xz | zip | deb
strip = true
maintainer = "Nom <email@example.com>"
description = ""
# deb_depends = ["libc6"]

# Déploiement : cpp-maker deploy <nom>
# [deploy.rpi]
# method = "rsync"             # rsync | scp | copy
# host = "raspberrypi.local"
# user = "pi"
# path = "/home/pi/{name}"
# toolchain = "armhf"
# mode = "release"
# artifact = "stage"           # stage | tar.gz | tar.xz | zip | deb
# after = ["{path}/bin/{name} --version"]
)";
}

inline void cmd_new(const std::vector<std::string>& rest) {
	if (rest.empty()) util::die("usage : cpp-maker new <nom> [--lang c|c++] "
								"[--type normal|static|shared|header-only|modular] "
								"[--workspace] [--standard ...] [--compiler ...]");
	auto o = parse_opts(rest, {"--type", "--lang", "--standard", "--compiler", "--c-standard"},
						{"--workspace", "--modules"});
	if (o.positional.size() != 1) util::die("cpp-maker new : un seul nom de projet attendu");
	std::string name = o.positional[0];

	// --- Type de projet -----------------------------------------------------
	// `normal`   : exécutable mono-module (remplace `executable`).
	// `modular`  : workspace avec un module `core` par défaut — équivaut à
	//              `--workspace` (conservé pour la rétrocompatibilité).
	// `static` | `shared` | `header-only` : bibliothèque (mono-module).
	std::string cli_type = o.value("--type", "normal");
	std::string lang = o.value("--lang", "c++");
	bool ws = o.flag("--workspace") || o.flag("--modules");
	if (cli_type == "modular") { ws = true; cli_type = "normal"; }
	if (cli_type == "executable") cli_type = "normal";  // alias rétrocompatible
	if (cli_type == "library") cli_type = "static";     // alias rétrocompatible
	static const std::set<std::string> cli_types = {"normal", "static", "shared", "header-only"};
	if (!cli_types.count(cli_type))
		util::die("type invalide : " + cli_type +
				  " (normal|static|shared|header-only|modular)");
	// Type interne utilisé par la suite (write_module_sources, module_toml).
	std::string type = (cli_type == "normal") ? "executable" : cli_type;

	if (lang == "cpp" || lang == "cxx") lang = "c++";
	if (lang != "c" && lang != "c++") util::die("--lang : c ou c++");
	bool c_lang = lang == "c";
	std::string cxx_std = o.value("--standard", "c++23");
	std::string c_std = o.value("--c-standard", "c17");
	if (c_lang && o.values.count("--standard") && cfg::c_standards().count(cxx_std)) {
		c_std = cxx_std;
		cxx_std = "c++23";
	}

	fs::path root = name;
	std::string pname = root.filename().string();
	if (!util::is_safe_name(pname)) util::die("nom de projet invalide : '" + pname + "'");
	if (fs::exists(root)) util::die("le dossier existe déjà : " + root.string());
	if (!cfg::cxx_standards().count(cxx_std)) util::die("standard C++ invalide : " + cxx_std);
	if (!cfg::c_standards().count(c_std)) util::die("standard C invalide : " + c_std);
	
	// Vérifier un compilateur hôte AVANT de créer quoi que ce soit.
	std::string forced = o.value("--compiler", "auto");
	std::vector<toolchain::TrialResult> log;
	auto chosen = toolchain::detect_working(lang, "auto", forced, c_lang ? c_std : cxx_std, &log);
	if (!chosen) {
		for (auto& l : log) util::errln("  {:<14} {}", l.name, l.status);
		util::die(std::format("aucun compilateur {} ne compile un fichier trivial en -std={}\n"
							  "  Pistes : cpp-maker toolchain list | cpp-maker toolchain install 14",
							  lang, c_lang ? c_std : cxx_std));
	}
	bool pin = forced != "auto";
	if (!pin) {
		auto first = toolchain::host_candidates(!c_lang, "auto");
		if (!first.empty() && first[0] != chosen->exe) pin = true;
	}
	util::outln("\033[1;34m[cpp-maker]\033[0m compilateur hôte : {} ({})", chosen->exe, chosen->version);

	std::string tc = toolchains_toml();
	if (pin) {
		tc = util::replace_all(tc, std::format("{} = \"auto\"\n", c_lang ? "cc" : "cxx"),
							   std::format("{} = \"{}\"     # épinglé par cpp-maker new\n",
										   c_lang ? "cc" : "cxx", chosen->exe));
		util::outln("  → inscrit dans cpp-maker.toml : {} = \"{}\"", c_lang ? "cc" : "cxx", chosen->exe);
	}

	std::string head = std::format(R"([project]
name = "{}"
version = "0.1.0"
description = ""
standard = "{}"              # standard C++
c_standard = "{}"               # standard C
minimum_cpp_maker_version = "{}"
)", pname, cxx_std, c_std, CPP_MAKER_VERSION);

	fs::create_directories(root);
	if (ws) {
		std::string app = pname;
		std::string lib = "core";
		if (app == lib) lib = "corelib";
		std::string toml = head + std::format(R"(
[workspace]
modules = [
	"libs/{}",
	"apps/{}",
]
)", lib, app) + "\n" + tc + common_toml_tail();
		util::write_file(root / "cpp-maker.toml", toml);
		fs::path libdir = root / "libs" / lib;
		util::write_file(libdir / "module.toml", module_toml(lib, "static", {}));
		write_module_sources(libdir, lib, "static", c_lang);
		fs::path appdir = root / "apps" / app;
		util::write_file(appdir / "module.toml", module_toml(app, "executable", {lib}));
		write_module_sources(appdir, app, "executable", c_lang, lib);
		// l'application n'a pas d'en-tête propre : main() utilise la bibliothèque
		std::error_code ec;
		fs::remove_all(appdir / "include", ec);
		fs::remove(appdir / "src" / (app + (c_lang ? ".c" : ".cpp")), ec);
	} else {
		std::string toml = head + std::format(R"(
[module]
type = "{}"            # executable | static | shared | header-only

[sources]
dirs = ["src"]

[include]
public = ["include"]

[dependencies]
pkg_config = []                # ex. ["zlib"]
system = []                    # ex. ["m", "pthread"]

[tests]
dirs = ["tests"]
timeout = 300
)", type) + "\n" + tc + util::replace_all(common_toml_tail(), "\n[tests]\ntimeout = 300                  # secondes par exécutable de test\n", "");
		// [tests] est déjà défini par la section module
		util::write_file(root / "cpp-maker.toml", toml);
		write_module_sources(root, pname, type, c_lang);
	}

	util::write_file(root / ".gitignore", "build/\ndist/\ncompile_commands.json\n.cpp-maker.lock\n");
	util::write_file(root / "README.md", std::format(
		"# {0}\n\nProjet {1} généré par cpp-maker {2}.\n\n## Utilisation\n\n```sh\n"
		"cpp-maker build                 # debug, toolchain hôte\n"
		"cpp-maker build release -t armhf # compilation croisée\n"
		"cpp-maker test                  # compile et lance les tests\n"
		"cpp-maker package               # dist/{0}-<version>-<os>-<arch>.tar.gz\n"
		"cpp-maker graph                 # dépendances entre modules\n```\n",
		pname, c_lang ? "C" : "C++", CPP_MAKER_VERSION));

	util::outln("\033[1;32mProjet '{}' créé{}.\033[0m\n", pname, ws ? " (workspace multi-modules)" : "");
	util::outln("  cd {}", root.string());
	util::outln("  cpp-maker build                  # compile (debug)");
	if (type == "executable" || ws) util::outln("  cpp-maker run                    # compile et exécute");
	util::outln("  cpp-maker test                   # tests");
	util::outln("  cpp-maker build release -t all   # toutes les toolchains configurées");
	if (ws) util::outln("  cpp-maker add libs/net --type static   # nouveau module");
	util::outln("  cpp-maker doctor                 # diagnostic de l'environnement");
}

// Remplace le tableau [workspace].modules dans le texte de cpp-maker.toml.
inline std::string set_workspace_modules(const std::string& text, const std::vector<std::string>& mods) {
	std::string arr = "modules = [\n";
	for (auto& m : mods) arr += "    \"" + m + "\",\n";
	arr += "]";
	auto ws = text.find("[workspace]");
	if (ws == std::string::npos) util::die("table [workspace] introuvable dans cpp-maker.toml");
	size_t pos = ws;
	for (;;) {
		pos = text.find("modules", pos);
		if (pos == std::string::npos) {
			auto eol = text.find('\n', ws);
			return text.substr(0, eol + 1) + arr + "\n" + text.substr(eol + 1);
		}
		size_t bol = text.rfind('\n', pos);
		bol = bol == std::string::npos ? 0 : bol + 1;
		if (util::trim(text.substr(bol, pos - bol)).empty()) break;
		pos += 7;
	}
	auto lb = text.find('[', pos);
	if (lb == std::string::npos) util::die("tableau modules mal formé");
	size_t rb = lb + 1;
	bool in_str = false;
	for (; rb < text.size(); ++rb) {
		char ch = text[rb];
		if (ch == '"') in_str = !in_str;
		else if (ch == '#' && !in_str) { while (rb < text.size() && text[rb] != '\n') ++rb; }
		else if (ch == ']' && !in_str) break;
	}
	if (rb >= text.size()) util::die("tableau modules non terminé");
	return text.substr(0, pos) + arr + text.substr(rb + 1);
}

inline int cmd_add(const std::vector<std::string>& args) {
	auto o = parse_opts(args, {"--type", "--lang", "--deps", "--name", "--dir"},
						{"--no-scaffold"});
	if (o.positional.size() != 1)
		util::die("usage : cpp-maker add <nom|chemin> [--type T] [--lang c|c++] "
				  "[--deps a,b] [--name nom] [--dir libs|apps|<chemin>] [--no-scaffold]");
	fs::path invoked = fs::current_path();
	auto c = load_ctx();
	if (!c.p.workspace)
		util::die("ce projet n'est pas un workspace : ajoutez une table [workspace] avec "
				  "modules = [...] (ou créez-le avec 'cpp-maker new <nom> --workspace')");

	// --- Détermination du chemin du module ---------------------------------
	//   - nom simple ("core")            → <dir>/core, dir = "libs" par défaut
	//   - chemin relatif ("libs/x", "apps/y")
	//                                    → interprété depuis le répertoire
	//                                      d'invocation de la commande, puis
	//                                      ramené relatif à la racine
	//   - chemin absolu                  → tel quel
	// L'option --dir déplace la destination par défaut ("--dir apps" place
	// le module dans apps/<nom>).
	fs::path arg = o.positional[0];
	fs::path rel;
	if (arg.is_absolute()) {
		rel = arg.lexically_normal().lexically_relative(c.p.root);
	} else {
		std::string s = arg.generic_string();
		bool has_sep = s.find('/') != std::string::npos ||
					   s.find('\\') != std::string::npos;
		if (!has_sep) {
			std::string dir = o.value("--dir", "libs");
			if (dir.empty() || dir == "." || dir == "/")
				util::die("--dir invalide : '" + dir + "'");
			// On normalise et on retire un éventuel séparateur final.
			fs::path d = fs::path(dir).lexically_normal();
			if (d.empty() || *d.begin() == "..")
				util::die("--dir doit rester dans le workspace : '" + dir + "'");
			rel = d / arg;
		} else {
			rel = fs::absolute(invoked / arg).lexically_normal().lexically_relative(c.p.root);
		}
	}
	rel = rel.lexically_normal();

	std::string name = o.value("--name", rel.filename().string());
	std::string type = o.value("--type", "static");
	if (type == "library") type = "static";
	if (type == "normal") type = "executable";            // alias de --type normal
	std::string lang = o.value("--lang", "c++");
	if (lang == "cpp" || lang == "cxx") lang = "c++";
	if (lang != "c" && lang != "c++") util::die("--lang : c ou c++");
	auto deps = util::split_list(o.value("--deps"));
	if (c.p.find_module(name)) util::die("un module nommé '" + name + "' existe déjà");
	for (auto& d : deps) if (!c.p.find_module(d)) util::die("dépendance inconnue : " + d);
	if (fs::exists(rel)) util::die("le dossier existe déjà : " + rel.string());
	if (!util::is_safe_name(name)) util::die("nom de module invalide : " + name);

	// --- Enregistrement dans [workspace].modules ---------------------------
	std::string text;
	util::read_file("cpp-maker.toml", text);
	auto mods = c.p.module_paths;
	mods.push_back(rel.generic_string());
	std::string new_text = set_workspace_modules(text, mods);

	util::write_file(rel / "module.toml", module_toml(name, type, deps));
	if (!o.flag("--no-scaffold")) {
		// main() appelle <dep>::add() uniquement si l'en-tête de squelette de
		// <dep> la déclare ; sinon le programme généré n'utilise que lui-même.
		std::string uses;
		for (auto& d : deps) {
			auto dm = c.p.find_module(d);
			if (dm->type == "executable") continue;
			for (auto ext : {".hpp", ".h"}) {
				std::string hdr;
				if (util::read_file(util::under(dm->dir, fs::path("include") / d / (d + ext)), hdr) &&
					hdr.find("add(int a, int b)") != std::string::npos) { uses = d; break; }
			}
			if (!uses.empty()) break;
		}
		write_module_sources(rel, name, type, lang == "c", type == "executable" ? uses : "");
	}
	util::write_file_atomic("cpp-maker.toml", new_text);
	try {
		auto p = cfg::load(c.p.root, "cpp-maker.toml");
		auto e = cfg::validate_errors(p);
		if (!e.empty()) throw std::runtime_error(e);
	} catch (const std::exception& e) {
		util::write_file_atomic("cpp-maker.toml", text);
		util::die(std::format("ajout annulé (cpp-maker.toml restauré) : {}", e.what()));
	}
	util::outln("\033[1;32mModule '{}' ({}) ajouté dans {}\033[0m", name, type, rel.generic_string());
	util::outln("  cpp-maker build -m {}", name);
	return 0;
}

// ---------------------------------------------------------------------------
// module rename / module remove : gestion des modules du workspace
// ---------------------------------------------------------------------------
// Remplace la valeur "key = <chaîne>" de la table [section] d'un texte TOML ;
// la clé est insérée sous l'en-tête si elle est absente.
inline std::string replace_toml_string(const std::string& text, const std::string& section,
									   const std::string& key, const std::string& value) {
	auto sec = text.find("[" + section + "]");
	if (sec == std::string::npos) return text;
	size_t bol = text.find('\n', sec);
	if (bol == std::string::npos) return text;
	++bol;
	for (;;) {
		size_t eol = text.find('\n', bol);
		if (eol == std::string::npos) eol = text.size();
		size_t b = text.find_first_not_of(" \t", bol);
		if (b < eol && text[b] == '[') break;                    // section suivante
		if (b < eol && text.compare(b, key.size(), key) == 0 &&
			(b + key.size() >= eol || text[b + key.size()] == ' ' ||
			 text[b + key.size()] == '\t' || text[b + key.size()] == '=')) {
			auto eq = text.find('=', b);
			if (eq == std::string::npos || eq > eol) break;
			auto q1 = text.find('"', eq);
			if (q1 == std::string::npos || q1 > eol) break;
			auto q2 = text.find('"', q1 + 1);
			if (q2 == std::string::npos) break;
			return text.substr(0, q1 + 1) + value + text.substr(q2);
		}
		if (eol == text.size()) break;
		bol = eol + 1;
	}
	auto eol = text.find('\n', sec);
	if (eol == std::string::npos) return text + "\n" + key + " = \"" + value + "\"\n";
	return text.substr(0, eol + 1) + key + " = \"" + value + "\"\n" + text.substr(eol + 1);
}

// Remplace la valeur "key = [...]" de la table [section] par un tableau de
// chaînes ; insère la section puis la clé si l'une des deux est absente. Le
// tableau multi-lignes existant (commentaires compris) est intégralement
// remplacé, ce qui rend la fonction idempotente.
inline std::string replace_toml_array(const std::string& text, const std::string& section,
									  const std::string& key, const std::vector<std::string>& values) {
	std::string new_arr = key + " = " + toml_array(values);
	auto sec = text.find("[" + section + "]");
	if (sec == std::string::npos) {
		std::string s = text;
		if (!s.empty() && s.back() != '\n') s += '\n';
		s += "\n[" + section + "]\n" + new_arr + "\n";
		return s;
	}
	size_t bol = text.find('\n', sec);
	if (bol == std::string::npos) return text + "\n" + new_arr + "\n";
	++bol;
	for (;;) {
		size_t eol = text.find('\n', bol);
		if (eol == std::string::npos) eol = text.size();
		size_t b = text.find_first_not_of(" \t", bol);
		// Section suivante : la clé est absente, on l'insère juste avant.
		if (b < eol && text[b] == '[')
			return text.substr(0, bol) + new_arr + "\n" + text.substr(bol);
		if (b < eol && text.compare(b, key.size(), key) == 0 &&
			(b + key.size() >= eol || text[b + key.size()] == ' ' ||
			 text[b + key.size()] == '\t' || text[b + key.size()] == '=')) {
			auto eq = text.find('=', b);
			if (eq == std::string::npos || eq > eol) return text;
			auto lb = text.find('[', eq);
			if (lb == std::string::npos) return text;
			size_t rb = lb + 1;
			bool in_str = false;
			for (; rb < text.size(); ++rb) {
				char ch = text[rb];
				if (ch == '"') in_str = !in_str;
				else if (ch == '#' && !in_str) { while (rb < text.size() && text[rb] != '\n') ++rb; }
				else if (ch == ']' && !in_str) break;
			}
			if (rb >= text.size()) return text;
			size_t indent = b - bol;
			return text.substr(0, b) + key + " = " + toml_array(values) + text.substr(rb + 1);
			(void)indent;
		}
		if (eol == text.size()) {
			// Fin de fichier : append dans la section courante.
			std::string s = text;
			if (s.back() != '\n') s += '\n';
			s += new_arr + "\n";
			return s;
		}
		bol = eol + 1;
	}
}

// Détermine si le module peut être édité automatiquement (module.toml dédié,
// ou cpp-maker.toml d'un projet mono-module) — refuse les configs v0.4 avec
// plusieurs modules partageant le même fichier.
inline fs::path editable_module_file(const Project& p, const ModuleConfig& m) {
	if (m.file.filename() == "module.toml") return m.file;
	int shared = 0;
	for (auto& x : p.modules) if (x.file == m.file) ++shared;
	if (shared > 1)
		util::die(std::format("édition impossible : configuration v0.4 (plusieurs modules dans "
							  "{}) — convertissez en workspace (un module.toml par module)",
							  m.file.string()));
	return m.file;
}

// cpp-maker module add <chemin> [--type …] [--lang …] [--deps a,b] [--no-scaffold]
// cpp-maker module rename <ancien> <nouveau>
// cpp-maker module remove <nom> [--force] [--purge]
//
// Les fichiers TOML sont modifiés puis la configuration entière est
// revalidée ; en cas d'échec, tout est restauré (remplacement atomique).
inline int cmd_module(const std::vector<std::string>& args) {
	if (args.empty())
		util::die("usage : cpp-maker module <deps|add|rename|remove> … (voir 'cpp-maker help')");
	const std::string& sub = args[0];
	if (sub == "add") {
		std::vector<std::string> rest(args.begin() + 1, args.end());
		return cmd_add(rest);
	}

	auto c = load_ctx();
	if (!c.p.workspace && sub != "deps")
		util::die("la gestion des modules (rename/remove) requiert un workspace "
				  "([workspace] modules = […] ; ou 'cpp-maker new <nom> --workspace')");

	struct Edit { fs::path file; std::string before, after; };

		if (sub == "deps") {
		auto o = parse_opts(args.size() > 1
							? std::vector<std::string>(args.begin() + 1, args.end())
							: std::vector<std::string>{},
							{}, {"--pkg-config", "--system", "--module"});
		if (o.positional.size() < 2)
			util::die("usage : cpp-maker module deps <list|add|remove> <module> [<nom>] "
					  "[--pkg-config|--system|--module]");
		const std::string action = o.positional[0];
		const std::string mname  = o.positional[1];
		const ModuleConfig* m = c.p.find_module(mname);
		if (!m) util::die("module inconnu : " + mname);

		if (action == "list") {
			util::outln("\033[1;36mDépendances de '{}'\033[0m ({})", mname, m->file.generic_string());
			util::outln("  modules    : {}", m->deps.empty() ? "—" : util::join(m->deps, ", "));
			util::outln("  pkg-config : {}", m->pkg_config.empty() ? "—" : util::join(m->pkg_config, ", "));
			util::outln("  système    : {}", m->system_libs.empty() ? "—" : util::join(m->system_libs, ", "));
			return 0;
		}
		if (action != "add" && action != "remove")
			util::die("action inconnue : " + action + " (list|add|remove)");
		if (o.positional.size() < 3)
			util::die("cpp-maker module deps " + action + " <module> <nom>");

		std::string name = o.positional[2];
		if (name.starts_with("-l")) name = name.substr(2);  // -lz → z
		if (name.empty() || !util::is_safe_name(name))
			util::die("nom de dépendance invalide : '" + o.positional[2] + "'");

		enum Kind { KModule, KPkgConfig, KSystem };
		int nflags = (int)o.flag("--pkg-config") + (int)o.flag("--system") + (int)o.flag("--module");
		if (nflags > 1)
			util::die("--pkg-config, --system et --module sont mutuellement exclusifs");
		auto has = [&](const std::vector<std::string>& v) {
			return std::find(v.begin(), v.end(), name) != v.end();
		};
		Kind kind;
		if (o.flag("--pkg-config")) kind = KPkgConfig;
		else if (o.flag("--system")) kind = KSystem;
		else if (o.flag("--module")) kind = KModule;
		// remove : le type est celui de la liste où le nom figure déjà.
		else if (action == "remove" && has(m->deps)) kind = KModule;
		else if (action == "remove" && has(m->pkg_config)) kind = KPkgConfig;
		else if (action == "remove" && has(m->system_libs)) kind = KSystem;
		else if (c.p.find_module(name)) kind = KModule;
		else if (util::have("pkg-config") &&
				 proc::capture({"pkg-config", "--exists", name}).exit_code == 0)
			kind = KPkgConfig;
		else kind = KSystem;
		const char* label = kind == KModule ? "module" : kind == KPkgConfig ? "pkg-config" : "système";

		std::vector<std::string> mods = m->deps, pcs = m->pkg_config, sys = m->system_libs;
		std::vector<std::string>& L = kind == KModule ? mods : kind == KPkgConfig ? pcs : sys;

		if (action == "add") {
			if (kind == KModule && !c.p.find_module(name))
				util::die("module inconnu : " + name);
			if (kind == KModule && name == mname)
				util::die("un module ne peut pas dépendre de lui-même");
			if (std::find(L.begin(), L.end(), name) != L.end()) {
				util::outln("'{}' est déjà une dépendance ({}) de '{}'.", name, label, mname);
				return 0;
			}
			if (kind == KPkgConfig && util::have("pkg-config") &&
				proc::capture({"pkg-config", "--exists", name}).exit_code != 0)
				util::warn("pkg-config ne connaît pas '{}' sur l'hôte : le build échouera tant "
						   "que son paquet de développement n'est pas installé", name);
			L.push_back(name);
		} else {
			auto it = std::find(L.begin(), L.end(), name);
			if (it == L.end())
				util::die(std::format("'{}' n'est pas une dépendance ({}) de '{}'",
									  name, label, mname));
			L.erase(it);
		}

		fs::path file = editable_module_file(c.p, *m);
		std::string text;
		if (!util::read_file(file, text))
			util::die("impossible de lire " + file.string());
		std::string new_text = text;
		new_text = replace_toml_array(new_text, "dependencies", "modules",    mods);
		new_text = replace_toml_array(new_text, "dependencies", "pkg_config", pcs);
		new_text = replace_toml_array(new_text, "dependencies", "system",     sys);
		util::write_file_atomic(file, new_text);
		try {
			auto np = cfg::load(c.p.root, c.p.file);
			auto e = cfg::validate_errors(np);
			if (!e.empty()) throw std::runtime_error(e);
		} catch (const std::exception& e) {
			util::write_file_atomic(file, text);
			util::die(std::format("opération annulée ({} restauré) : {}", file.string(), e.what()));
		}
		util::outln("\033[1;32m{}\033[0m : '{}' {} ({})", mname, name,
					action == "add" ? "ajouté" : "retiré", label);
		return 0;
	}
	
	else if (sub == "rename") {
		if (args.size() != 3)
			util::die("usage : cpp-maker module rename <ancien> <nouveau>");
		const std::string& from = args[1];
		const std::string& to   = args[2];
		const ModuleConfig* m = c.p.find_module(from);
		if (!m) {
			std::vector<std::string> names;
			for (auto& x : c.p.modules) names.push_back(x.name);
			util::die(std::format("module inconnu : {} (modules : {})", from, util::join(names, ", ")));
		}
		if (from == to) { util::outln("rien à faire."); return 0; }
		if (!util::is_safe_name(to)) util::die("nom de module invalide : " + to);
		if (c.p.find_module(to)) util::die("le nom est déjà pris : " + to);

		std::vector<Edit> edits;
		// 1) module.toml : name = "<nouveau>".
		{
			std::string text;
			if (!util::read_file(m->file, text))
				util::die("impossible de lire " + m->file.string());
			edits.push_back({m->file, text, replace_toml_string(text, "module", "name", to)});
		}
		// 2) deps des autres modules.
		for (auto& o : c.p.modules) {
			if (o.name == from ||
				std::find(o.deps.begin(), o.deps.end(), from) == o.deps.end()) continue;
			std::string text;
			if (!util::read_file(o.file, text))
				util::die("impossible de lire " + o.file.string());
			std::vector<std::string> deps = o.deps;
			for (auto& d : deps) if (d == from) d = to;
			edits.push_back({o.file, text,
							 replace_toml_array(text, "dependencies", "modules", deps)});
		}
		// 3) Répertoire conventionnel (<ancien> comme nom de dossier) :
		//    renommé sur le disque, chemin mis à jour dans [workspace].
		fs::path old_dir = m->dir, new_dir;
		bool dir_rename = m->dir.filename().string() == from;
		if (dir_rename) {
			std::string text;
			if (!util::read_file(c.p.file, text))
				util::die("impossible de lire " + c.p.file.string());
			std::vector<std::string> paths = c.p.module_paths;
			for (auto& mp : paths)
				if (fs::path(mp).lexically_normal() == old_dir)
					mp = (old_dir.parent_path() / to).generic_string();
			edits.push_back({c.p.file, text, set_workspace_modules(text, paths)});
			new_dir = old_dir.parent_path() / to;
		}

		for (auto& e : edits) util::write_file_atomic(e.file, e.after);
		std::error_code ec;
		if (dir_rename) {
			fs::rename(old_dir, new_dir, ec);
			if (ec) util::warn("renommage du répertoire impossible : {} → {}",
							   old_dir.string(), new_dir.string());
		}
		try {
			auto p = cfg::load(c.p.root, c.p.file);
			auto e = cfg::validate_errors(p);
			if (!e.empty()) throw std::runtime_error(e);
		} catch (const std::exception& e) {
			if (dir_rename && !ec) { std::error_code ec2; fs::rename(new_dir, old_dir, ec2); }
			for (auto& ed : edits) util::write_file_atomic(ed.file, ed.before);
			util::die(std::format("renommage annulé (fichiers restaurés) : {}", e.what()));
		}
		util::outln("\033[1;32mmodule '{}' renommé en '{}'.\033[0m", from, to);
		if (dir_rename && !ec) util::outln("  répertoire : {}", new_dir.generic_string());
		return 0;
	}

	else if (sub == "remove" || sub == "rm") {
		auto o = parse_opts(std::vector<std::string>(args.begin() + 1, args.end()),
							{}, {"--force", "--purge"});
		if (o.positional.size() != 1)
			util::die("usage : cpp-maker module remove <nom> [--force] [--purge]");
		const std::string& name = o.positional[0];
		const ModuleConfig* m = c.p.find_module(name);
		if (!m) util::die("module inconnu : " + name);

		std::vector<std::string> dependents;
		for (auto& x : c.p.modules)
			if (x.name != name &&
				std::find(x.deps.begin(), x.deps.end(), name) != x.deps.end())
				dependents.push_back(x.name);
		if (!dependents.empty() && !o.flag("--force"))
			util::die(std::format("des modules dépendent de '{}' : {} — retirez d'abord la "
								  "dépendance ou réessayez avec --force",
								  name, util::join(dependents, ", ")));

		std::vector<Edit> edits;
		for (auto& x : c.p.modules) {
			if (x.name == name ||
				std::find(x.deps.begin(), x.deps.end(), name) == x.deps.end()) continue;
			std::string text;
			if (!util::read_file(x.file, text))
				util::die("impossible de lire " + x.file.string());
			std::vector<std::string> deps;
			for (auto& d : x.deps) if (d != name) deps.push_back(d);
			edits.push_back({x.file, text,
							 replace_toml_array(text, "dependencies", "modules", deps)});
		}
		{
			std::string text;
			if (!util::read_file(c.p.file, text))
				util::die("impossible de lire " + c.p.file.string());
			std::vector<std::string> paths;
			for (auto& mp : c.p.module_paths)
				if (fs::path(mp).lexically_normal() != m->dir) paths.push_back(mp);
			edits.push_back({c.p.file, text, set_workspace_modules(text, paths)});
		}
		for (auto& e : edits) util::write_file_atomic(e.file, e.after);
		try {
			auto p = cfg::load(c.p.root, c.p.file);
			auto e = cfg::validate_errors(p);
			if (!e.empty()) throw std::runtime_error(e);
		} catch (const std::exception& e) {
			for (auto& ed : edits) util::write_file_atomic(ed.file, ed.before);
			util::die(std::format("suppression annulée (fichiers restaurés) : {}", e.what()));
		}
		if (o.flag("--purge")) {
			std::error_code ec;
			if (fs::exists(m->dir, ec)) {
				fs::remove_all(m->dir, ec);
				util::outln("  répertoire supprimé : {}", m->dir.generic_string());
			}
		}
		util::outln("\033[1;32mmodule '{}' retiré du workspace.\033[0m", name);
		if (!o.flag("--purge"))
			util::outln("  (les sources de {} restent sur le disque ; --purge pour les supprimer)",
						m->dir.generic_string());
		return 0;
	}

	util::die("sous-commande module inconnue : " + sub + " (attendu : deps | add | rename | remove)");
}

} // namespace commands

// ============================================================================
// main
// ============================================================================
static void print_help() {
    util::outln(R"(cpp-maker {} — moteur de production unifié pour C et C++

USAGE
    cpp-maker <commande> [options]

PROJET
    new <nom> [--lang c|c++] [--type normal|static|shared|header-only|modular]
              [--workspace] [--standard c++NN] [--c-standard cNN] [--compiler cxx]
                            Crée un projet.
                              --type normal        exécutable mono-module (défaut)
                              --type static        bibliothèque statique
                              --type shared        bibliothèque partagée
                              --type header-only   bibliothèque d'en-têtes seuls
                              --type modular       workspace modulaire : un
                                                   module `core` par défaut
                                                   dans libs/core, et une
                                                   application dans apps/<nom>
                                                   (équivaut à --workspace)
    add <nom|chemin> [--type T] [--lang c|c++] [--deps a,b]
                     [--dir libs|apps|<chemin>] [--no-scaffold]
                            Ajoute un module au workspace. Un nom simple est
                            placé dans libs/<nom> (ou apps/<nom> avec
                            --dir apps) ; un chemin explicite (libs/x,
                            apps/y, /abs/path) est utilisé tel quel.
    module deps <list|add|remove> <module> [<nom>]
                     [--pkg-config|--system|--module]
                            Configure les bibliothèques d'un module :
                            liste, ajoute ou retire une dépendance.
                            Sans option, le type est déduit (module connu,
                            pkg-config si disponible, sinon bibliothèque
                            système).
    module rename <ancien> <nouveau>
                            Renomme un module (module.toml, deps des autres,
                            répertoire si conventionnel) avec validation
    module remove <nom> [--force] [--purge]
                            Retire un module du workspace (--force : retire
                            aussi les deps des dépendants ; --purge : supprime
                            les sources)
    info | modules | graph [--dot]
                            Projet, liste des modules, graphe de dépendances
    size    [mode] [-m a,b] [-t tc]
                            Contenu détaillé des binaires construits :
                            text/data/bss/dec/hex (Berkeley), en-tête ELF
                            (classe, machine, ABI, type, entrée), archives .a
                            sommées ; -v : plus grandes sections
    config show [clé…] | validate | set <clé> <valeur>
                            show : toute la configuration, ou seulement
                            les clés demandées (exactes ou par préfixe :
                            build.warnings, toolchains.host, modes.release…)
                            set : validation avant remplacement atomique
    doctor [--no-fix|--check-deps]
                            Diagnostic : outils, dépendances, toolchains, sanitizers
                            Enregistre automatiquement dans [dependencies].modules
                            les dépendances implicites détectées à partir
                            des #include (--no-fix / --check-deps pour
                            seulement les lister).

CYCLE DE VIE
    build   [mode]          Compile (incrémental, parallèle) ; --tests inclut les tests
    run     [mode] [--args arg…] [-- arg…]
                            Compile et exécute ; tout ce qui suit --args (ou --)
                            est passé tel quel au programme
    test    [mode] [--filter X] [--junit rapport.xml] [--timeout s]
            [--args arg…]   arguments transmis à chaque exécutable de test
    check   [--modes debug,asan,tsan] [--args arg…]
                            Tests dans plusieurs modes
    package [mode] [--format tar.gz,tar.xz,zip,deb] [--no-strip]
                            Archives dans dist/ (défaut : release)
    install [mode] [--prefix /usr/local] [--destdir D]
    deploy  <cible> | --list
                            Build + package + transfert ([deploy.<cible>])
    generate ninja|compdb   build.ninja / compile_commands.json
    clean   [mode] [-t tc] [--all]
                            Supprime build/ (et dist/ avec --all)

TOOLCHAINS
    toolchain list          Profils, compilateurs croisés détectés, versions gcc
    toolchain show [nom]    Détail d'une toolchain (triplet, émulateur…)
    toolchain install <N|triplet>
                            Installe gcc-N ou un compilateur croisé (apt)
    toolchain use <N> | remove <N>

OPTIONS COMMUNES
    -m, --module a,b        Modules à traiter (et leurs dépendances)
    -a, --all               Tout le workspace (même depuis le dossier d'un module)
    -t, --toolchain a,b     Toolchain(s) : host, gcc, clang, profil [toolchains.x],
                            triplet du PATH (arm-linux-gnueabihf), ou 'all'
    --cc X / --cxx Y        Compilateurs ad hoc (ex. --cxx clang++-18)
    --mode M                Mode (aussi en premier argument)
    -j, --jobs N            Parallélisme (0 = auto : ncpus - 1 workers,
                            un cœur restant dédié à cpp-maker)
    -k, --keep-going        Continue après une erreur
    -n, --dry-run           Affiche ce qui serait fait
    -v, --verbose           Affiche les commandes
    --clean                 Repart d'un dossier de build vide
    --backend ninja         (build) délègue l'exécution à ninja

MODES
    release  debug (défaut)  relwithdebinfo  minsize  asan  tsan  ubsan  valgrind

EXEMPLES
    cpp-maker new hello --workspace
    cd hello
    cpp-maker add core                         # crée libs/core/
    cpp-maker add gui --dir apps --deps core   # crée apps/gui/
    cpp-maker build && cpp-maker run
    cpp-maker build release -t host,armhf
    cpp-maker test -t arm-linux-gnueabihf          # exécution via qemu-arm
    cpp-maker build -m core                        # un module seul
    cpp-maker package --format tar.gz,deb -t armhf
    cpp-maker deploy rpi
    cpp-maker module deps list core
    cpp-maker module deps add core zlib              # pkg-config
    cpp-maker module deps add core m --system        # -lm
    cpp-maker module deps add app1 core --module     # dépendance de module
    cpp-maker doctor                                 # + auto-fix deps
    cpp-maker run release -m voxelgame --args --world=iles --generator=core:islands
                        # cpp-maker parse "release" et "-m voxelgame",
                        # le jeu reçoit "--world=iles" et "--generator=core:islands"
)", CPP_MAKER_VERSION);
}

int main(int argc, char** argv) {
	std::vector<std::string> args(argv + 1, argv + argc);
	if (args.empty() || args[0] == "help" || args[0] == "-h" || args[0] == "--help") {
		print_help();
		return 0;
	}
	if (args[0] == "--version" || args[0] == "version") {
		util::outln("cpp-maker {}", CPP_MAKER_VERSION);
		return 0;
	}
	const std::string cmd = args[0];
	std::vector<std::string> rest(args.begin() + 1, args.end());

	try {
		using namespace commands;
		if (cmd == "new")       { cmd_new(rest); return 0; }
		if (cmd == "add")       return cmd_add(rest);
		if (cmd == "module")    return cmd_module(rest);
		if (cmd == "build")     return cmd_build(rest);
		if (cmd == "run")       return cmd_run(rest);
		if (cmd == "test")      return cmd_test(rest);
		if (cmd == "check")     return cmd_check(rest);
		if (cmd == "package")   return cmd_package(rest);
		if (cmd == "install")   return cmd_install(rest);
		if (cmd == "deploy")    return cmd_deploy(rest);
		if (cmd == "generate")  return cmd_generate(rest);
		if (cmd == "graph")     return cmd_graph(rest);
		if (cmd == "modules")   return cmd_modules(rest);
		if (cmd == "info")      return cmd_info();
		if (cmd == "size")      return cmd_size(rest);
		if (cmd == "doctor")    return cmd_doctor(rest);
		if (cmd == "clean")     return cmd_clean(rest);
		if (cmd == "config")    return cmd_config(rest);
		if (cmd == "toolchain") return cmd_toolchain(rest);
		if (cmd == "valgrind") {
			std::vector<std::string> a = {"--mode", "valgrind", "--"};
			a.insert(a.end(), rest.begin(), rest.end());
			return cmd_run(a);
		}
		util::die("commande inconnue : " + cmd + " (cpp-maker help)");
	} catch (const std::exception& e) {
		util::die(e.what());
	}
}