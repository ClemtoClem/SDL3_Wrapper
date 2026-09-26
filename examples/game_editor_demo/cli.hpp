#pragma once
/**
 * game_editor::CommandLine — options de ligne de commande de la démo.
 *
 * Raison d'être : une application graphique n'est pas testable « en vrai »
 * sans écran ni humain. Toutes les options ci-dessous existent pour rendre
 * CETTE démo pilotable et observable depuis un terminal — exécuter un
 * scénario scripté, capturer des images à des instants précis, et produire un
 * rapport d'exécution lisible par une machine.
 *
 * Analyse volontairement simple et sans dépendance (pas de getopt : il mute
 * un état global et ne sait pas parler `--clef=valeur`), et surtout SANS
 * EXCEPTION : `Parse()` rend un `Result` dont l'erreur est déjà un message
 * affichable.
 */
#include <vector>

#include "core/core.hpp"

namespace game_editor {

/// Une capture d'écran demandée : « à l'image N, écrire ce fichier ».
struct ScreenshotRequest {
	long frame = 0;
	String path;
};

/// Format du rapport d'exécution (cf. report.hpp).
enum class ReportFormat : uint8_t { TEXT, JSON };

struct CommandLine {
	// ── Fenêtre / rendu ──────────────────────────────────────────────────────
	int windowWidth = 1440;
	int windowHeight = 860;
	String theme = "studio"; ///< studio | dark | light | aero

	// ── Pilotage ─────────────────────────────────────────────────────────────
	String scene;             ///< scène active au démarrage (vide = celle du projet)
	String scenario;          ///< scénario scripté intégré (cf. scenarios.hpp)
	String scriptPath;        ///< script utilisateur supplémentaire à charger
	String projectPath;       ///< projet JSON à charger au démarrage
	long frames = 0;          ///< >0 : quitte proprement après N images
	uint64_t randomSeed = 20260912; ///< graine partagée démo + scripts (exécutions rejouables)
	/// Fils du vivier de tâches : -1 = automatique (cœurs - 1), 0 = tout sur
	/// le fil principal. Sert à comparer série et parallèle sur une même
	/// scène, et à revenir au comportement d'origine si besoin.
	int jobWorkers = -1;

	// ── Observation ──────────────────────────────────────────────────────────
	String reportPath;
	ReportFormat reportFormat = ReportFormat::TEXT;
	String screenshotDir = "captures";
	String assetsDir     = "assets";                 ///< ressources (lecture seule)
	String savesDir      = "saves/game_editor_demo"; ///< projets, scènes, scripts enregistrés
	std::vector<ScreenshotRequest> screenshots;
	bool verbose = false;

	// ── Modes ────────────────────────────────────────────────────────────────
	/// Aucune fenêtre, aucun GPU : la simulation, les scripts et le rapport
	/// tournent seuls. C'est le mode utilisable sur une machine sans écran
	/// (intégration continue) et celui que les tests exercent.
	bool headless = false;
	bool showHelp = false;
	bool listScenarios = false;
	bool listScenes = false;

	/// Message d'aide — imprimé tel quel par `--help`.
	[[nodiscard]] static const char *HelpText() {
		return
			"game_editor_demo — éditeur de niveau 3D bâti sur le wrapper SDL3/C++23\n"
			"\n"
			"USAGE\n"
			"  game_editor_demo [options]\n"
			"\n"
			"FENÊTRE\n"
			"  --width=N               largeur de la fenêtre (défaut 1440)\n"
			"  --height=N              hauteur de la fenêtre (défaut 860)\n"
			"  --theme=NOM             studio | dark | light | aero (défaut studio)\n"
			"\n"
			"PILOTAGE\n"
			"  --scene=NOM             scène active au démarrage\n"
			"  --scenario=NOM          joue un scénario scripté intégré (voir --list-scenarios)\n"
			"  --script=CHEMIN         charge un script .sled supplémentaire\n"
			"  --project=CHEMIN        charge un projet JSON au démarrage\n"
			"  --frames=N              quitte proprement après N images\n"
			"  --seed=N                graine aléatoire (exécutions reproductibles)\n"
			"  --jobs=N                fils du vivier de tâches (0 = tout sur le fil principal)\n"
			"\n"
			"OBSERVATION\n"
			"  --report=CHEMIN         écrit le rapport d'exécution\n"
			"  --report-format=FORMAT  text | json (défaut text)\n"
			"  --screenshot=N:CHEMIN   capture l'image N dans CHEMIN (répétable)\n"
			"  --screenshot-dir=DIR    dossier des captures automatiques (défaut captures)\n"
			"  --assets-dir=DIR        racine des ressources (défaut assets) : navigateur,\n"
			"                          et lecture des scénarios (jamais d'écriture)\n"
			"  --saves-dir=DIR         sauvegardes (défaut saves/game_editor_demo) :\n"
			"                          DIR/projects, DIR/scenes, DIR/scripts\n"
			"  --verbose               trace détaillée sur la sortie standard\n"
			"\n"
			"MODES\n"
			"  --headless              sans fenêtre ni GPU : simulation + scripts + rapport\n"
			"  --list-scenarios        liste les scénarios intégrés puis quitte\n"
			"  --list-scenes           liste les scènes du projet puis quitte\n"
			"  -h, --help              affiche cette aide\n"
			"\n"
			"EXEMPLES\n"
			"  # Visite guidée complète, captures à chaque étape, rapport JSON\n"
			"  game_editor_demo --scenario=tour --frames=600 --report=rapport.json --report-format=json\n"
			"\n"
			"  # Circuit de voiture jouable, vérifié sans écran\n"
			"  game_editor_demo --headless --scenario=circuit --frames=1800 --report=circuit.txt\n"
			"\n"
			"  # Capture ponctuelle de l'interface\n"
			"  game_editor_demo --frames=120 --screenshot=100:captures/ui.png\n";
	}

	/// Analyse `argv`. Les erreurs (option inconnue, valeur invalide) sont
	/// remontées telles quelles : une faute de frappe ne doit PAS être
	/// ignorée silencieusement dans une commande censée produire un rapport.
	[[nodiscard]] static Result<CommandLine, String> Parse(int argc, char **argv) {
		CommandLine options;
		for (int i = 1; i < argc; ++i) {
			String arg(argv[i] ? argv[i] : "");
			if (arg == "-h" || arg == "--help") {
				options.showHelp = true;
				continue;
			}
			if (arg == "--headless") {
				options.headless = true;
				continue;
			}
			if (arg == "--verbose") {
				options.verbose = true;
				continue;
			}
			if (arg == "--list-scenarios") {
				options.listScenarios = true;
				continue;
			}
			if (arg == "--list-scenes") {
				options.listScenes = true;
				continue;
			}

			auto value = SplitValue(arg);
			if (value.IsNone())
				return Err(String::Format("option inconnue : %s (essayez --help)", arg.CStr()));
			const String &key = value.Unwrap().first;
			const String &raw = value.Unwrap().second;

			if (key == "--width" || key == "--height") {
				auto parsed = ParsePositiveInt(raw, key);
				if (parsed.IsError())
					return Err(parsed.Error());
				(key == "--width" ? options.windowWidth : options.windowHeight) = int(parsed.Value());
			} else if (key == "--frames") {
				auto parsed = ParsePositiveInt(raw, key);
				if (parsed.IsError())
					return Err(parsed.Error());
				options.frames = long(parsed.Value());
			} else if (key == "--jobs") {
				Option<int64_t> parsed = raw.TryParseInt();
				if (parsed.IsNone() || parsed.Unwrap() < 0 || parsed.Unwrap() > 64)
					return Err(String::Format("--jobs : entier de 0 à 64 attendu, trouvé `%s`", raw.CStr()));
				options.jobWorkers = int(parsed.Unwrap());
			} else if (key == "--seed") {
				auto parsed = ParsePositiveInt(raw, key);
				if (parsed.IsError())
					return Err(parsed.Error());
				options.randomSeed = uint64_t(parsed.Value());
			} else if (key == "--theme") {
				if (raw != "studio" && raw != "dark" && raw != "light" && raw != "aero")
					return Err(String::Format("--theme : valeur inconnue `%s` (studio | dark | light | aero)", raw.CStr()));
				options.theme = raw;
			} else if (key == "--scene") {
				options.scene = raw;
			} else if (key == "--scenario") {
				options.scenario = raw;
			} else if (key == "--script") {
				options.scriptPath = raw;
			} else if (key == "--project") {
				options.projectPath = raw;
			} else if (key == "--assets-dir") {
				if (raw.IsEmpty())
					return Err(String("--assets-dir : chemin vide"));
				options.assetsDir = raw;
			} else if (key == "--saves-dir") {
				if (raw.IsEmpty())
					return Err(String("--saves-dir : chemin vide"));
				options.savesDir = raw;
			} else if (key == "--report") {
				options.reportPath = raw;
			} else if (key == "--report-format") {
				if (raw == "text")
					options.reportFormat = ReportFormat::TEXT;
				else if (raw == "json")
					options.reportFormat = ReportFormat::JSON;
				else
					return Err(String::Format("--report-format : valeur inconnue `%s` (text | json)", raw.CStr()));
			} else if (key == "--screenshot-dir") {
				options.screenshotDir = raw;
			} else if (key == "--screenshot") {
				auto request = ParseScreenshot(raw);
				if (request.IsError())
					return Err(request.Error());
				options.screenshots.push_back(request.Value());
			} else {
				return Err(String::Format("option inconnue : %s (essayez --help)", key.CStr()));
			}
		}
		return Ok(std::move(options));
	}

	/// Ligne de commande reconstituée — reproduite telle quelle dans le
	/// rapport pour qu'un run soit rejouable à l'identique.
	[[nodiscard]] static String Rebuild(int argc, char **argv) {
		String out;
		for (int i = 0; i < argc; ++i) {
			if (i > 0)
				out.Concat(" ");
			out.Concat(argv[i] ? argv[i] : "");
		}
		return out;
	}

private:
	/// `--clef=valeur` -> (`--clef`, `valeur`). NONE si l'argument n'a pas
	/// cette forme (donc ni un drapeau connu, ni une option à valeur).
	[[nodiscard]] static Option<std::pair<String, String>> SplitValue(const String &arg) {
		size_t equals = arg.Find('=');
		if (equals == String::NPOS || !arg.StartsWith("--"))
			return NONE;
		return Some(std::pair<String, String>(arg.Substr(0, equals), arg.Substr(equals + 1)));
	}

	[[nodiscard]] static Result<int64_t, String> ParsePositiveInt(const String &raw, const String &key) {
		Option<int64_t> parsed = raw.TryParseInt();
		if (parsed.IsNone() || parsed.Unwrap() <= 0)
			return Err(String::Format("%s : entier strictement positif attendu, trouvé `%s`", key.CStr(),
									  raw.CStr()));
		return Ok(parsed.Unwrap());
	}

	/// `N:chemin` — le chemin peut contenir des `:` (cas Windows `C:\...`),
	/// donc seule la PREMIÈRE occurrence sépare.
	[[nodiscard]] static Result<ScreenshotRequest, String> ParseScreenshot(const String &raw) {
		size_t colon = raw.Find(':');
		if (colon == String::NPOS)
			return Err(String::Format("--screenshot : forme attendue N:CHEMIN, trouvé `%s`", raw.CStr()));
		Option<int64_t> frame = raw.Substr(0, colon).TryParseInt();
		if (frame.IsNone() || frame.Unwrap() < 0)
			return Err(String::Format("--screenshot : numéro d'image invalide dans `%s`", raw.CStr()));
		String path = raw.Substr(colon + 1);
		if (path.IsEmpty())
			return Err(String::Format("--screenshot : chemin vide dans `%s`", raw.CStr()));
		return Ok(ScreenshotRequest{long(frame.Unwrap()), std::move(path)});
	}
};

} // namespace game_editor
