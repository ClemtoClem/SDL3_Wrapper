// Définitions de cli.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "cli.hpp"

namespace game_editor {

// ── CommandLine ──────────────────────────────────────────────────────────────

const char * CommandLine::HelpText() {
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
		"  --script=CHEMIN         charge un script .script supplémentaire\n"
		"  --project=CHEMIN        ouvre un projet : son dossier, son .json, ou son nom dans\n"
		"                          DIR/projects (sans cette option : « Aucun projet ouvert »)\n"
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
		"  --saves-dir=DIR         sauvegardes (défaut saves/game_editor_demo) : un dossier\n"
		"                          par projet dans DIR/projects (.json, assets, scenes, scripts)\n"
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

Result<CommandLine, String> CommandLine::Parse(int argc, char **argv) {
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

String CommandLine::Rebuild(int argc, char **argv) {
	String out;
	for (int i = 0; i < argc; ++i) {
		if (i > 0)
			out.Concat(" ");
		out.Concat(argv[i] ? argv[i] : "");
	}
	return out;
}

Option<std::pair<String, String>> CommandLine::SplitValue(const String &arg) {
	size_t equals = arg.Find('=');
	if (equals == String::NPOS || !arg.StartsWith("--"))
		return NONE;
	return Some(std::pair<String, String>(arg.Substr(0, equals), arg.Substr(equals + 1)));
}

Result<int64_t, String> CommandLine::ParsePositiveInt(const String &raw, const String &key) {
	Option<int64_t> parsed = raw.TryParseInt();
	if (parsed.IsNone() || parsed.Unwrap() <= 0)
		return Err(String::Format("%s : entier strictement positif attendu, trouvé `%s`", key.CStr(),
								  raw.CStr()));
	return Ok(parsed.Unwrap());
}

Result<ScreenshotRequest, String> CommandLine::ParseScreenshot(const String &raw) {
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

} // namespace game_editor
