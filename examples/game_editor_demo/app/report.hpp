#pragma once
/**
 * game_editor — rapport d'exécution : ce que la démo a réellement fait.
 *
 * Toute la raison d'être de ce fichier : une application graphique qui tourne
 * quelques secondes puis se ferme ne laisse RIEN derrière elle. Un rapport la
 * rend vérifiable — on peut relire, après coup et sans écran, quelles scènes
 * ont été chargées, quels objets existaient, combien de fils d'exécution ont
 * tourné en parallèle, à quelle cadence les images sont sorties, quels
 * scripts ont été exécutés et quelles captures ont été écrites.
 *
 * Deux formats, même contenu : `text` pour un humain dans un terminal, `json`
 * (produit avec `data::JsonDocument`, comme le reste du dépôt) pour une
 * machine — un test d'intégration lit le JSON et vérifie des seuils.
 *
 * `FrameStats` mérite un mot : la cadence est mesurée en écartant la PREMIÈRE
 * image. Elle contient la compilation des pipelines GPU et la construction
 * de l'interface, et son temps (souvent des centaines de millisecondes)
 * écraserait le minimum mesuré au point de le rendre inutile.
 */
#include <vector>

#include "core/core.hpp"
#include "data/json.hpp"
#include "sdl3/thread.hpp"
#include "sdl3/time.hpp"

#include "cli.hpp"
#include "../document/project.hpp"
#include "../engine/runtime.hpp"

namespace game_editor {

// ============================================================================
// Suivi des fils d'exécution
// ============================================================================

/// Compte les fils d'exécution vivants et retient le maximum atteint
/// SIMULTANÉMENT — c'est cette valeur-là que le rapport publie (un simple
/// total cumulé ne dirait rien du parallélisme réel).
///
/// Le fil principal est compté dès la construction : « 1 » signifie donc
/// « aucun fil secondaire », ce qui est la lecture attendue.
class ThreadTracker {
public:
	ThreadTracker();

	/// Garde RAII : incrémente à la construction, décrémente à la
	/// destruction. À placer en tête du corps de chaque fil créé, pour que le
	/// comptage suive la vraie durée de vie même en cas de sortie anticipée.
	class Scope {
	public:
		explicit Scope(ThreadTracker &tracker) noexcept : m_tracker(&tracker) { m_tracker->Enter(); }
		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;
		~Scope() { m_tracker->Leave(); }

	private:
		ThreadTracker *m_tracker;
	};

	[[nodiscard]] int Peak() const noexcept { return m_peak.Load(); }
	[[nodiscard]] int Live() const noexcept { return m_live.Load(); }
	[[nodiscard]] int TotalStarted() const noexcept { return m_started.Load(); }

private:
	void Enter() noexcept;

	void Leave() noexcept { m_live.FetchAdd(-1); }

	sdl3::AtomicInt m_live;
	sdl3::AtomicInt m_peak;
	sdl3::AtomicInt m_started;
};

// ============================================================================
// Statistiques d'images
// ============================================================================

class FrameStats {
public:
	/// Enregistre la durée d'une image, en secondes.
	void Push(double seconds);

	[[nodiscard]] long Count() const noexcept { return m_count; }
	[[nodiscard]] double FirstFrameSeconds() const noexcept { return m_firstFrameSeconds; }
	[[nodiscard]] double TotalSeconds() const noexcept { return m_totalSeconds; }

	/// Les extrêmes sont en IMAGES PAR SECONDE : une image LONGUE donne le fps
	/// MINIMAL, d'où l'inversion croisée entre min/max de durée et de cadence.
	[[nodiscard]] double MinFps() const noexcept { return m_maxSeconds > 0.0 ? 1.0 / m_maxSeconds : 0.0; }
	[[nodiscard]] double MaxFps() const noexcept { return m_minSeconds > 0.0 ? 1.0 / m_minSeconds : 0.0; }
	[[nodiscard]] double AverageFps() const noexcept;

	/// Cadence instantanée lissée sur les dernières images — c'est ce que
	/// l'interface affiche et ce que `editor.fps()` rend (une moyenne sur
	/// toute la session sauterait aux yeux comme fausse dès que la charge
	/// change).
	[[nodiscard]] double RecentFps(size_t window = 30) const;

	/// Centile des DURÉES d'image, en millisecondes (p99 = « les pires 1% »),
	/// bien plus parlant qu'une moyenne pour juger des à-coups.
	[[nodiscard]] double PercentileMs(double percentile) const;

private:
	long m_count = 0;
	size_t m_sampleCount = 0;
	double m_totalSeconds = 0.0;
	double m_minSeconds = 0.0;
	double m_maxSeconds = 0.0;
	double m_firstFrameSeconds = 0.0;
	std::vector<double> m_samples;
};

// ============================================================================
// Coût par phase
// ============================================================================

/// Moyenne glissante du temps passé dans chaque étage d'une image.
///
/// Sans cette ventilation, « 46 ms par image » ne dit pas s'il faut optimiser
/// la physique, la mise en page, la scène 3D ou le dessin 2D — et on optimise
/// alors au hasard. C'est cette mesure qui a montré que la simulation coûtait
/// 0,01 ms quand le rendu de la scène 3D en coûtait 40.
class PhaseStats {
public:
	/// Une phase = un étage de la boucle d'image.
	enum Phase : int {
		SCENARIO,    ///< rappel `on_frame` du scénario
		SIMULATION,  ///< scripts de jeu + physique + animation + synchro ECS→Object3D
		PORTALS,     ///< rendu récursif des portails
		UI_STYLE,    ///< cascade de styles
		UI_LAYOUT,   ///< mise en page
		UI_VIEWPORT, ///< rendu de la scène 3D dans les widgets Viewport3D
		UI_EFFECTS,  ///< post-traitement des widgets à effet
		UI_DRAW,     ///< dessin 2D
		PRESENT,     ///< présentation de l'image
		COUNT,
	};

	[[nodiscard]] static const char *Name(int phase) noexcept;

	void Add(int phase, double milliseconds);

	void EndFrame() { ++m_frames; }

	[[nodiscard]] long Frames() const noexcept { return m_frames; }
	[[nodiscard]] double AverageMs(int phase) const noexcept;
	[[nodiscard]] double PeakMs(int phase) const noexcept;
	[[nodiscard]] double TotalAverageMs() const noexcept;

private:
	double m_total[COUNT] = {};
	double m_peak[COUNT] = {};
	long m_frames = 0;
};

// ============================================================================
// Rapport
// ============================================================================

/// Trace d'une capture d'écran demandée.
struct ScreenshotRecord {
	long frame = 0;
	String path;
	bool written = false;
	String error;
};

/// Trace d'une session de mode Jeu.
struct PlaySessionRecord {
	String scene;
	long startFrame = 0;
	long endFrame = 0;
	double duration = 0.0;
};

class RunReport {
public:
	// ── Entrées collectées pendant l'exécution ───────────────────────────────
	String commandLine;
	String mode = "fenêtré";
	String theme;
	String scenario;
	uint64_t randomSeed = 0;
	double wallClockSeconds = 0.0;
	bool completed = false;
	String failure; ///< vide si tout s'est bien passé

	FrameStats frames;
	PhaseStats phases;
	int peakThreads = 1;
	int totalThreads = 1;
	int jobWorkers = 0;          ///< fils du vivier de tâches
	int jobPeakConcurrent = 0;   ///< tâches exécutées SIMULTANÉMENT au maximum

	std::vector<ScreenshotRecord> screenshots;
	std::vector<PlaySessionRecord> playSessions;
	std::vector<String> scriptsExecuted;
	size_t scriptRuns = 0;
	size_t scriptCalls = 0;
	size_t scriptErrors = 0;

	std::vector<String> warnings;
	std::vector<String> errors;

	/// Compteurs de gameplay relevés à la fin (tours bouclés, meilleur temps…).
	std::vector<std::pair<String, String>> gameplayCounters;

	// ── Rendu ────────────────────────────────────────────────────────────────

	/// Rapport lisible. Les nombres sont volontairement peu décimaux : ce
	/// document se lit, il ne se compare pas au bit près (c'est le rôle du
	/// JSON).
	[[nodiscard]] String ToText(const Project &project) const;

	/// Même contenu, en JSON — c'est cette forme que lit un test
	/// d'intégration (seuils de cadence, présence des captures, absence
	/// d'erreur de script).
	[[nodiscard]] String ToJson(const Project &project) const;

	[[nodiscard]] String Render(const Project &project, ReportFormat format) const;

	/// Écrit le rapport. Err porte un message déjà lisible (le chemin
	/// fautif), jamais une exception.
	[[nodiscard]] Result<bool, String> Write(const Project &project, const String &path, ReportFormat format) const;

	/// Recopie dans le rapport ce que le journal de l'éditeur a retenu
	/// d'anormal — un rapport « terminé normalement » alors que la console
	/// est pleine d'erreurs de script serait trompeur.
	void CollectDiagnostics(const Runtime &runtime);
};

} // namespace game_editor
