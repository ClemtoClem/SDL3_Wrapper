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
	String projectPath;       ///< projet à ouvrir au démarrage (vide = « Aucun projet ouvert »)
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
	[[nodiscard]] static const char *HelpText();

	/// Analyse `argv`. Les erreurs (option inconnue, valeur invalide) sont
	/// remontées telles quelles : une faute de frappe ne doit PAS être
	/// ignorée silencieusement dans une commande censée produire un rapport.
	[[nodiscard]] static Result<CommandLine, String> Parse(int argc, char **argv);

	/// Ligne de commande reconstituée — reproduite telle quelle dans le
	/// rapport pour qu'un run soit rejouable à l'identique.
	[[nodiscard]] static String Rebuild(int argc, char **argv);

private:
	/// `--clef=valeur` -> (`--clef`, `valeur`). NONE si l'argument n'a pas
	/// cette forme (donc ni un drapeau connu, ni une option à valeur).
	[[nodiscard]] static Option<std::pair<String, String>> SplitValue(const String &arg);

	[[nodiscard]] static Result<int64_t, String> ParsePositiveInt(const String &raw, const String &key);

	/// `N:chemin` — le chemin peut contenir des `:` (cas Windows `C:\...`),
	/// donc seule la PREMIÈRE occurrence sépare.
	[[nodiscard]] static Result<ScreenshotRequest, String> ParseScreenshot(const String &raw);
};

} // namespace game_editor
