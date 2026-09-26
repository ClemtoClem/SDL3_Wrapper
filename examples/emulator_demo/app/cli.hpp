#pragma once
/**
 * emulator_demo::CommandLine — options de ligne de commande de la démo.
 *
 * Même raison d'être que game_editor::CommandLine : un émulateur graphique
 * n'est pas vérifiable « en vrai » sans écran ni humain devant la manette.
 * Ces options rendent la démo PILOTABLE (ROM, appuis de boutons scriptés à
 * une image précise, sauvegardes/chargements d'état, fenêtre ouverte au
 * démarrage) et OBSERVABLE (captures d'écran, rapport d'exécution texte ou
 * JSON, code de sortie non nul si une vérification échoue).
 *
 * Les numéros d'image désignent des images ÉMULÉES (1 = première image
 * produite par la console), pas des images de la fenêtre : c'est ce qui rend
 * un scénario identique en mode fenêtré et en `--headless`, quelle que soit
 * la cadence d'affichage de la machine. Sans ROM, seule l'interface tourne et
 * les numéros d'image désignent alors les images de l'interface.
 *
 * Analyse sans exception : `Parse()` rend un `Result` dont l'erreur est déjà
 * un message affichable.
 */
#include <utility>
#include <vector>

#include "core/core.hpp"

namespace emulator_demo {

/// Boutons de la console, dans l'ordre attendu par `Input::pressKey` (NDS/GBA)
/// — et, pour les 8 premiers, par `gbc::GbcButton`.
enum ConsoleButton : int {
	BTN_A = 0,
	BTN_B = 1,
	BTN_SELECT = 2,
	BTN_START = 3,
	BTN_RIGHT = 4,
	BTN_LEFT = 5,
	BTN_UP = 6,
	BTN_DOWN = 7,
	BTN_R = 8,
	BTN_L = 9,
	BTN_X = 10,
	BTN_Y = 11,
	BTN_COUNT = 12
};

[[nodiscard]] inline const char *ButtonName(int button) {
	static constexpr const char *NAMES[BTN_COUNT] = {"A",	 "B",	 "SELECT", "START", "RIGHT", "LEFT",
													 "UP", "DOWN", "R",		 "L",	  "X",	   "Y"};
	return (button >= 0 && button < BTN_COUNT) ? NAMES[button] : "?";
}

/// Nom de bouton (insensible à la casse) -> index. NONE si inconnu.
[[nodiscard]] inline Option<int> ParseButton(const String &name) {
	String upper = name.ToUpper();
	for (int button = 0; button < BTN_COUNT; ++button)
		if (upper == ButtonName(button))
			return Some(button);
	return NONE;
}

/// « À l'image N, appuyer sur BOUTON pendant `holdFrames` images. »
struct ScriptedPress {
	long frame = 0;
	int button = BTN_A;
	long holdFrames = 6;
};

/// Opération d'état planifiée à une image donnée.
enum class StateActionKind : uint8_t { SAVE, LOAD };

struct ScriptedStateAction {
	long frame = 0;
	StateActionKind kind = StateActionKind::SAVE;
};

/// Une capture d'écran demandée : « à l'image N, écrire ce fichier ».
struct ScreenshotRequest {
	long frame = 0;
	String path;
};

enum class ReportFormat : uint8_t { TEXT, JSON };

struct CommandLine {
	// ── ROM et réglages de la console ────────────────────────────────────────
	String romPath;
	String romEntry;   ///< entrée à lancer quand --rom est une archive (vide = première ROM)
	String extractDir; ///< cache des ROMs extraites d'archives (vide = dossier de préférences)
	String archivePassword; ///< mot de passe des archives chiffrées (zip AES, 7z AES)
	/// Fichier de réglages. Vide = celui choisi dans la fenêtre de configuration,
	/// sinon ./saves/emulator_demo/config.ini (cf. config_location.hpp).
	String configPath;
	bool saveConfig = true;
	String biosDir = "./assets/bios-firmware/";	
	Option<bool> directBoot = NONE;
	Option<bool> arm7Hle = NONE;
	Option<bool> threaded2D = NONE;
	Option<bool> threaded3D = NONE;
	Option<int> screenLayout = NONE; ///< 0 = horizontal, 1 = vertical
	/// Fichier d'état imposé. Vide = <dossier des états>/<rom>.state0.
	String statePath;
	/// Dossier des états, pour cette exécution seulement (non enregistré).
	/// Vide = celui des réglages (défaut ./saves/emulator_demo/states).
	String stateDir;
	String saveDir; ///< vide = sauvegardes de cartouche à côté de la ROM

	// ── Fenêtre ──────────────────────────────────────────────────────────────
	int windowWidth = 1200;
	int windowHeight = 760;
	bool audio = true;
	bool unlimitedSpeed = false;
	bool showHud = true;
	String openModal; ///< roms | saves | config | cheats | network | about

	// ── Pilotage ─────────────────────────────────────────────────────────────
	long frames = 0; ///< >0 : quitte proprement après N images
	std::vector<ScriptedPress> presses;
	std::vector<ScriptedStateAction> stateActions;

	// ── Observation ──────────────────────────────────────────────────────────
	String reportPath;
	ReportFormat reportFormat = ReportFormat::TEXT;
	std::vector<ScreenshotRequest> screenshots;
	String screenshotDir = "captures";
	long screenshotEvery = 0;
	bool requireVideo = false;
	bool verbose = false;

	// ── Modes ────────────────────────────────────────────────────────────────
	bool headless = false;
	bool showHelp = false;
	bool romInfo = false;
	bool listButtons = false;
	Option<String> listRomsDir = NONE;

	/// Nombre d'images par défaut en mode sans écran quand `--frames` manque :
	/// 10 secondes de console, assez pour sortir de la plupart des écrans
	/// d'amorçage.
	static constexpr long DEFAULT_HEADLESS_FRAMES = 600;

	[[nodiscard]] static const char *HelpText() {
		return "emulator_demo — émulateur NDS / GBA / GBC bâti sur le wrapper SDL3/C++23\n"
			   "\n"
			   "USAGE\n"
			   "  emulator_demo [options] [ROM]\n"
			   "\n"
			   "ROM ET CONSOLE\n"
			   "  --rom=CHEMIN            ROM à lancer (.nds .gba .gbc .gb), aussi en argument positionnel ;\n"
			   "                          une archive .zip .tar .tar.gz .tgz .gz est décompressée à la volée\n"
			   "  --rom-entry=NOM         entrée de l'archive à lancer (défaut : première ROM trouvée)\n"
			   "  --extract-dir=DIR       cache des ROMs extraites (défaut : dossier de préférences SDL)\n"
			   "  --archive-password=MOT  mot de passe des archives chiffrées (zip AES/ZipCrypto, 7z AES)\n"
			   "  --config=CHEMIN         fichier de réglages (défaut : celui choisi dans la configuration,\n"
			   "                          sinon ./saves/emulator_demo/config.ini)\n"
			   "  --no-save-config        ne réécrit pas le fichier de réglages en quittant\n"
			   "  --bios-dir=DIR          dossier de ndsBios9.bin, ndsBios7.bin, firmware.bin, gbaBios.bin\n"
			   "  --boot=MODE             direct | bios (défaut : config, direct à défaut)\n"
			   "  --arm7-hle              ARM7 en HLE (NDS sans BIOS ni firmware)\n"
			   "  --threaded-2d / --threaded-3d  rendu 2D / 3D de la console sur un fil dédié\n"
			   "  --layout=DISPOSITION    horizontal | vertical (écrans NDS)\n"
			   "  --state-dir=DIR         dossier des états sauvegardés (défaut : réglages, sinon\n"
			   "                          ./saves/emulator_demo/states)\n"
			   "  --state-path=CHEMIN     fichier de la sauvegarde rapide (défaut <dossier des états>/<rom>.state0)\n"
			   "  --save-dir=DIR          sauvegardes de cartouche (.sav) dans DIR plutôt qu'à côté de la ROM\n"
			   "                          (recommandé pour les tests : le jeu y écrit pendant l'exécution)\n"
			   "\n"
			   "FENÊTRE\n"
			   "  --width=N / --height=N  taille de la fenêtre (défaut 1200x760)\n"
			   "  --no-audio              pas de sortie audio (cadence tenue par une horloge)\n"
			   "  --speed=VITESSE         normal | unlimited\n"
			   "  --hide-hud              masque le bandeau de performances\n"
			   "  --open=BOÎTE            ouvre une boîte au démarrage : roms | saves | config | cheats | network | about\n"
			   "\n"
			   "PILOTAGE (numéros d'image = images émulées)\n"
			   "  --frames=N              quitte proprement après N images\n"
			   "  --press=N:BOUTON[:D]    appuie BOUTON à l'image N pendant D images (défaut 6), répétable\n"
			   "  --save-state=N          sauvegarde rapide à l'image N, répétable\n"
			   "  --load-state=N          chargement rapide à l'image N, répétable\n"
			   "\n"
			   "OBSERVATION\n"
			   "  --report=CHEMIN         écrit le rapport d'exécution\n"
			   "  --report-format=FORMAT  text | json (défaut text)\n"
			   "  --screenshot=N:CHEMIN   capture l'image N dans CHEMIN, répétable\n"
			   "  --screenshot-every=K    capture toutes les K images dans --screenshot-dir\n"
			   "  --screenshot-dir=DIR    dossier des captures automatiques (défaut captures)\n"
			   "  --require-video         échoue si la dernière image émulée est uniforme\n"
			   "  --verbose               trace détaillée sur la sortie standard\n"
			   "\n"
			   "MODES\n"
			   "  --headless              sans fenêtre, sans GPU ni audio : cœur seul, cadence libre\n"
			   "                          (les captures sont alors celles du framebuffer émulé)\n"
			   "  --rom-info              affiche les métadonnées de la ROM (ou de l'archive) puis quitte\n"
			   "  --list-roms[=DIR]       liste les ROMs d'un dossier, archives comprises (défaut assets/roms)\n"
			   "  --list-buttons          liste les noms de boutons de --press puis quitte\n"
			   "  -h, --help              affiche cette aide\n"
			   "\n"
			   "EXEMPLES\n"
			   "  # Vérification sans écran : 20 s de jeu, START à 3 s, capture et rapport JSON\n"
			   "  emulator_demo --headless --rom=jeu.gba --frames=1200 --press=180:START --save-dir=/tmp/saves \\\n"
			   "      --screenshot=1200:captures/jeu.png --report=rapport.json --report-format=json\n"
			   "\n"
			   "  # ROM lancée directement depuis son archive\n"
			   "  emulator_demo --headless --rom=jeux.zip --rom-entry=jeu.gbc --frames=600 --report=/dev/stdout\n"
			   "\n"
			   "  # Aller-retour d'état : sauvegarde à 300, chargement à 600\n"
			   "  emulator_demo --headless --rom=jeu.gbc --frames=900 --save-state=300 --load-state=600 \\\n"
			   "      --state-path=/tmp/jeu.state0 --report=/dev/stdout\n"
			   "\n"
			   "  # Capture de l'interface avec la boîte de configuration ouverte\n"
			   "  emulator_demo --frames=90 --open=config --screenshot=80:captures/config.png\n";
	}

	/// Analyse `argv`. Une option inconnue ou une valeur invalide est une
	/// ERREUR : une faute de frappe ne doit pas être ignorée silencieusement
	/// dans une commande censée vérifier quelque chose.
	[[nodiscard]] static Result<CommandLine, String> Parse(int argc, char **argv) {
		CommandLine options;
		for (int i = 1; i < argc; ++i) {
			String arg(argv[i] ? argv[i] : "");

			if (arg == "-h" || arg == "--help") {
				options.showHelp = true;
			} else if (arg == "--headless") {
				options.headless = true;
			} else if (arg == "--verbose") {
				options.verbose = true;
			} else if (arg == "--no-audio") {
				options.audio = false;
			} else if (arg == "--no-save-config") {
				options.saveConfig = false;
			} else if (arg == "--arm7-hle") {
				options.arm7Hle = Some(true);
			} else if (arg == "--threaded-2d") {
				options.threaded2D = Some(true);
			} else if (arg == "--threaded-3d") {
				options.threaded3D = Some(true);
			} else if (arg == "--hide-hud") {
				options.showHud = false;
			} else if (arg == "--require-video") {
				options.requireVideo = true;
			} else if (arg == "--rom-info") {
				options.romInfo = true;
			} else if (arg == "--list-buttons") {
				options.listButtons = true;
			} else if (arg == "--list-roms") {
				options.listRomsDir = Some(String("assets/roms"));
			} else if (!arg.StartsWith("-")) {
				if (!options.romPath.IsEmpty())
					return Err(String::Format("une seule ROM à la fois (déjà : %s, trouvé : %s)",
											  options.romPath.CStr(), arg.CStr()));
				options.romPath = arg;
			} else {
				auto parsed = options.ParseValueOption(arg);
				if (parsed.IsError())
					return Err(parsed.Error());
			}
		}
		return Ok(std::move(options));
	}

	/// Ligne de commande reconstituée — reproduite dans le rapport pour qu'une
	/// exécution soit rejouable à l'identique.
	[[nodiscard]] static String Rebuild(int argc, char **argv) {
		String out;
		for (int i = 0; i < argc; ++i) {
			if (i > 0)
				out.Concat(" ");
			// Le mot de passe d'archive ne doit pas finir dans le rapport.
			const String argument(argv[i] ? argv[i] : "");
			out.Concat(argument.StartsWith("--archive-password=") ? String("--archive-password=***") : argument);
		}
		return out;
	}

private:
	[[nodiscard]] Result<bool, String> ParseValueOption(const String &arg) {
		size_t equals = arg.Find('=');
		if (equals == String::NPOS || !arg.StartsWith("--"))
			return Err(String::Format("option inconnue : %s (essayez --help)", arg.CStr()));
		String key = arg.Substr(0, equals);
		String raw = arg.Substr(equals + 1);

		if (key == "--rom") {
			romPath = raw;
		} else if (key == "--rom-entry") {
			romEntry = raw;
		} else if (key == "--extract-dir") {
			extractDir = raw;
		} else if (key == "--archive-password") {
			archivePassword = raw;
		} else if (key == "--config") {
			configPath = raw;
		} else if (key == "--bios-dir") {
			biosDir = raw;
		} else if (key == "--state-path") {
			statePath = raw;
		} else if (key == "--state-dir") {
			stateDir = raw;
		} else if (key == "--save-dir") {
			saveDir = raw;
		} else if (key == "--boot") {
			if (raw != "direct" && raw != "bios")
				return Err(String::Format("--boot : valeur inconnue `%s` (direct | bios)", raw.CStr()));
			directBoot = Some(raw == "direct");
		} else if (key == "--layout") {
			if (raw != "horizontal" && raw != "vertical")
				return Err(String::Format("--layout : valeur inconnue `%s` (horizontal | vertical)", raw.CStr()));
			screenLayout = Some(raw == "vertical" ? 1 : 0);
		} else if (key == "--speed") {
			if (raw != "normal" && raw != "unlimited")
				return Err(String::Format("--speed : valeur inconnue `%s` (normal | unlimited)", raw.CStr()));
			unlimitedSpeed = raw == "unlimited";
		} else if (key == "--open") {
			if (raw != "roms" && raw != "saves" && raw != "config" && raw != "cheats" && raw != "network" &&
				raw != "about")
				return Err(String::Format(
					"--open : boîte inconnue `%s` (roms | saves | config | cheats | network | about)", raw.CStr()));
			openModal = raw;
		} else if (key == "--width" || key == "--height") {
			auto value = PositiveInt(raw, key);
			if (value.IsError())
				return Err(value.Error());
			(key == "--width" ? windowWidth : windowHeight) = int(value.Value());
		} else if (key == "--frames") {
			auto value = PositiveInt(raw, key);
			if (value.IsError())
				return Err(value.Error());
			frames = long(value.Value());
		} else if (key == "--screenshot-every") {
			auto value = PositiveInt(raw, key);
			if (value.IsError())
				return Err(value.Error());
			screenshotEvery = long(value.Value());
		} else if (key == "--screenshot-dir") {
			screenshotDir = raw;
		} else if (key == "--screenshot") {
			auto request = ParseScreenshot(raw);
			if (request.IsError())
				return Err(request.Error());
			screenshots.push_back(request.Value());
		} else if (key == "--press") {
			auto press = ParsePress(raw);
			if (press.IsError())
				return Err(press.Error());
			presses.push_back(press.Value());
		} else if (key == "--save-state" || key == "--load-state") {
			auto value = PositiveInt(raw, key);
			if (value.IsError())
				return Err(value.Error());
			stateActions.push_back(ScriptedStateAction{
				long(value.Value()), key == "--save-state" ? StateActionKind::SAVE : StateActionKind::LOAD});
		} else if (key == "--report") {
			reportPath = raw;
		} else if (key == "--report-format") {
			if (raw == "text")
				reportFormat = ReportFormat::TEXT;
			else if (raw == "json")
				reportFormat = ReportFormat::JSON;
			else
				return Err(String::Format("--report-format : valeur inconnue `%s` (text | json)", raw.CStr()));
		} else if (key == "--list-roms") {
			listRomsDir = Some(raw);
		} else {
			return Err(String::Format("option inconnue : %s (essayez --help)", key.CStr()));
		}
		return Ok(true);
	}

	[[nodiscard]] static Result<int64_t, String> PositiveInt(const String &raw, const String &key) {
		Option<int64_t> parsed = raw.TryParseInt();
		if (parsed.IsNone() || parsed.Unwrap() <= 0)
			return Err(String::Format("%s : entier strictement positif attendu, trouvé `%s`", key.CStr(), raw.CStr()));
		return Ok(parsed.Unwrap());
	}

	/// `N:chemin` — seule la PREMIÈRE occurrence de `:` sépare (chemins
	/// Windows `C:\...`).
	[[nodiscard]] static Result<ScreenshotRequest, String> ParseScreenshot(const String &raw) {
		size_t colon = raw.Find(':');
		if (colon == String::NPOS)
			return Err(String::Format("--screenshot : forme attendue N:CHEMIN, trouvé `%s`", raw.CStr()));
		Option<int64_t> frame = raw.Substr(0, colon).TryParseInt();
		if (frame.IsNone() || frame.Unwrap() <= 0)
			return Err(String::Format("--screenshot : numéro d'image invalide dans `%s`", raw.CStr()));
		String path = raw.Substr(colon + 1);
		if (path.IsEmpty())
			return Err(String::Format("--screenshot : chemin vide dans `%s`", raw.CStr()));
		return Ok(ScreenshotRequest{long(frame.Unwrap()), std::move(path)});
	}

	/// `N:BOUTON` ou `N:BOUTON:DURÉE`.
	[[nodiscard]] static Result<ScriptedPress, String> ParsePress(const String &raw) {
		std::vector<String> parts = raw.Split(':');
		if (parts.size() < 2 || parts.size() > 3)
			return Err(String::Format("--press : forme attendue N:BOUTON[:DURÉE], trouvé `%s`", raw.CStr()));
		Option<int64_t> frame = parts[0].TryParseInt();
		if (frame.IsNone() || frame.Unwrap() <= 0)
			return Err(String::Format("--press : numéro d'image invalide dans `%s`", raw.CStr()));
		Option<int> button = ParseButton(parts[1]);
		if (button.IsNone())
			return Err(String::Format("--press : bouton inconnu `%s` (voir --list-buttons)", parts[1].CStr()));
		ScriptedPress press;
		press.frame = long(frame.Unwrap());
		press.button = button.Unwrap();
		if (parts.size() == 3) {
			Option<int64_t> hold = parts[2].TryParseInt();
			if (hold.IsNone() || hold.Unwrap() <= 0)
				return Err(String::Format("--press : durée invalide dans `%s`", raw.CStr()));
			press.holdFrames = long(hold.Unwrap());
		}
		return Ok(press);
	}
};

} // namespace emulator_demo
