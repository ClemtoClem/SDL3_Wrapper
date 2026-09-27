#pragma once
/**
 * game_editor::App — la coquille applicative : ligne de commande, fenêtre,
 * boucle d'images, captures d'écran, rapport d'exécution.
 *
 * Deux modes, UN SEUL chemin de logique :
 *
 *  - **fenêtré** : SDL + `render3d::Canvas` + interface `ui::`. Sous
 *    `xvfb-run`, ce mode tourne sans écran physique tout en produisant de
 *    VRAIES captures d'écran — c'est ainsi que la démo se vérifie.
 *  - **`--headless`** : ni fenêtre, ni GPU. Le document, la physique, les
 *    scripts, le rapport et le scénario tournent à l'identique ; seules les
 *    captures d'écran sont impossibles (et signalées comme telles dans le
 *    rapport plutôt que tues). Utile là où aucun serveur d'affichage n'est
 *    disponible du tout.
 *
 * Le SCÉNARIO est le même dans les deux cas : c'est ce qui garantit qu'une
 * vérification sans écran teste bien le même enchaînement que ce qu'un humain
 * verrait à l'écran.
 *
 * ── Écran de chargement asynchrone ──────────────────────────────────────
 * Chaque matériau distinct fait compiler son pipeline GPU au PREMIER rendu.
 * Sans précaution, la fenêtre reste figée plusieurs secondes à l'ouverture.
 * Un fil secondaire effectue donc un rendu hors écran jetable de la scène
 * entière (`RenderObjectToTexture` — tampons de commandes locaux, jamais la
 * chaîne d'échange de la fenêtre, donc licite hors du fil principal d'après
 * SDL_gpu.h) pendant que le fil principal continue de pomper les évènements
 * et d'afficher une barre de progression. Même raisonnement et même structure
 * que examples/ui_viewport3d_demo.cpp, dont ce fichier reprend le motif.
 */
#include <cstdio>
#include <vector>

#include "core/core.hpp"
#include "data/script.hpp"
#include "ecs/ecs.hpp"
#include "render3d/canvas.hpp"
#include "render3d/offscreen.hpp"
#include "sdl3/filesystem.hpp"
#include "sdl3/image.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

#include "cli.hpp"
#include "report.hpp"
#include "scenarios.hpp"
#include "../editor/panels.hpp"
#include "../engine/runtime.hpp"

namespace game_editor {

class App {
public:
	App(CommandLine options, String commandLine)
		: m_options(std::move(options)), m_commandLine(std::move(commandLine)) {}

	App(const App &) = delete;
	App &operator=(const App &) = delete;

	/// Point d'entrée unique. Rend le code de sortie du processus : 0 si tout
	/// s'est bien passé, 1 si une étape a échoué OU si un script a signalé une
	/// erreur (un scénario qui échoue doit faire échouer la commande, sinon
	/// il ne vérifie rien).
	[[nodiscard]] int Run();

private:
	// ── Sorties d'information ────────────────────────────────────────────────

	void PrintScenarios() const;

	void PrintScenes();

	/// `--project` tel que donné s'il existe ; sinon relatif au dossier des
	/// projets (`--project=Donjon` → saves/game_editor_demo/projects/Donjon),
	/// puis au dossier des sauvegardes.
	[[nodiscard]] String ResolveProjectPath(const String &path) const;

	/// Ouvre `--project` s'il est donné. Sans lui, AUCUN projet n'est ouvert :
	/// l'interface l'annonce et propose d'en créer ou d'en ouvrir un. Un
	/// projet illisible est signalé, sans faire échouer le démarrage.
	void OpenInitialProject(Runtime &runtime);

	/// Nombre d'images à jouer : `--frames` prime, sinon la durée conseillée
	/// du scénario, sinon 0 (boucle jusqu'à fermeture par l'utilisateur).
	[[nodiscard]] long FrameBudget() const;

	// ── Scripts et scénarios ─────────────────────────────────────────────────

	/// Installe le scénario (intégré ou fichier) dans l'interpréteur outil.
	/// `false` si le nom/chemin demandé n'existe pas ou ne compile pas — le
	/// processus s'arrête alors, car poursuivre exécuterait un scénario vide
	/// en prétendant avoir testé quelque chose.
	[[nodiscard]] bool InstallScenario(Runtime &runtime);

	/// Branche les fonctions que les scripts appellent sur l'hôte. En mode
	/// sans écran, `onScreenshot`/`onThemeChange`/`onPanelFocus` restent non
	/// installés : les fonctions de script rendent alors `false` (cf.
	/// runtime.hpp), ce qui laisse un même scénario s'exécuter dans les deux
	/// modes sans branche conditionnelle dans le script.
	void InstallCommonHooks(Runtime &runtime);

	/// Suit les entrées/sorties du mode Jeu pour le rapport, sans que le
	/// runtime ait à connaître le rapport.
	void TrackPlayState(Runtime &runtime);

	/// Relève les compteurs déclarés par le script de gameplay, pour que le
	/// rapport puisse affirmer « la voiture a bouclé N tours » — c'est ce qui
	/// fait d'un essai de jouabilité une VÉRIFICATION et pas une démo.
	void CollectGameplayCounters(Runtime &runtime);

	void FinishReport(Runtime &runtime, uint64_t startTicks);

	/// Écrit le rapport si `--report` a été donné, et l'affiche toujours en
	/// mode bavard.
	void EmitReport(const Project &project);

	/// Code de sortie : un scénario dont un `assert` a échoué, ou un script
	/// en erreur, doit faire échouer la commande.
	[[nodiscard]] int ExitCode() const;

	// ── Mode sans écran ──────────────────────────────────────────────────────

	/// Boucle « simulation pure » à pas fixe. Le temps est SIMULÉ (1/60 s par
	/// image) et non mesuré : deux exécutions produisent alors exactement la
	/// même partie, ce qui est le seul moyen d'écrire une assertion stable
	/// sur un nombre de tours de circuit. La cadence mesurée reste, elle,
	/// bien réelle (c'est le temps de calcul de chaque pas).
	[[nodiscard]] int RunHeadless();

	// ── Mode fenêtré ─────────────────────────────────────────────────────────

	[[nodiscard]] int RunWindowed();

	void InstallWindowedHooks(Runtime &runtime, EditorUi &editorUi);

	/// Ouvre la police d'icônes. Elle est RENDUE à l'appelant sans être
	/// enregistrée, pour deux raisons distinctes qui se sont chacune payées
	/// d'un plantage :
	///
	///  - `RenderSystem::RegisterFont` mémorise un POINTEUR vers la police ;
	///    l'enregistrer ici viserait la variable locale de cette fonction,
	///    qui meurt dès le `return` (le déplacement vers l'appelant change
	///    l'adresse) — use-after-return à la première icône dessinée ;
	///  - une `sdl3::Font` doit être détruite AVANT le `TtfContext` qui l'a
	///    vue naître ; une variable statique (détruite à la toute fin du
	///    processus) ne l'assure pas, une locale de l'appelant déclarée après
	///    son `TtfContext`, si.
	///
	/// Chargement « au mieux » : sans police d'icônes, l'interface reste
	/// entièrement utilisable (les boutons d'outils perdent leur glyphe),
	/// donc un échec ne doit pas empêcher l'application de démarrer.
	[[nodiscard]] static Option<sdl3::Font> OpenIconFont();

	// ── Captures d'écran ─────────────────────────────────────────────────────

	/// Les demandes de `--screenshot=N:CHEMIN` dont l'image est arrivée.
	void QueueCommandLineScreenshots();

	void FlushScreenshots(sdl3::Renderer &renderer);

	void EnsureScreenshotDirectory();

	// ── Écran de chargement ──────────────────────────────────────────────────

	/// Affiche une barre de progression pendant qu'un fil secondaire force la
	/// compilation des pipelines GPU. Rend `false` si l'utilisateur a fermé la
	/// fenêtre entre-temps (sortie propre, sans construire l'interface).
	[[nodiscard]] bool RunLoadingScreen(ui::Ui &gui, ecs::ArchetypeRegistry &registry, sdl3::Renderer &renderer,
										render3d::Canvas &canvas, Runtime &runtime);

	[[nodiscard]] int Fail(String message);

	// ── Membres ──────────────────────────────────────────────────────────────

	static constexpr float FONT_POINT_SIZE = 14.f;
	static constexpr double MAX_STEP_SECONDS = 1.0 / 20.0;

	CommandLine m_options;
	String m_commandLine;
	RunReport m_report;
	ThreadTracker m_threads;
	EditorUi *m_ui = nullptr;
	bool m_quitRequested = false;

	long m_frame = 0;
	bool m_wasPlaying = false;
	bool m_screenshotDirectoryReady = false;
	std::vector<String> m_pendingScreenshots;
};

} // namespace game_editor
