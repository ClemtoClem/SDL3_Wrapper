#pragma once
/**
 * profiler — profileur de programmes C++ instrumentés avec
 * diagnostics::trace, inspiré de Google Orbit.
 *
 *   profiler record [--out F] [--hz N] [--delay S --duration S] -- prog args…
 *       (par défaut : data/profiler/captures/<programme>-<date>-<heure>.vcap ;
 *       réglages : data/profiler/settings.json, cf. settings.hpp)
 *   profiler list
 *       captures du dossier, la plus récente d'abord
 *       lance le programme et capture (toute l'exécution, ou une fenêtre de
 *       `duration` secondes après `delay` secondes)
 *   profiler attach PID --duration S
 *       capture un programme déjà lancé avec DIAG_TRACE_CONTROL=1
 *   profiler report F [--from MS --to MS] [--thread NOM|TID] [--target-fps N] [--top N]
 *       diagnostic des goulots d'étranglement + statistiques
 *   profiler tree F [--bottom-up] [--thread …] [--depth N] [--min %]
 *       arbre d'appels des échantillons
 *   profiler compare AVANT APRES
 *       écarts entre deux captures
 *   profiler export F --chrome SORTIE.json
 *       pour chrome://tracing ou ui.perfetto.dev
 *   profiler view [F] [--frames N --screenshot P.png]
 *       visualiseur (chronologie des fils, fonctions, arbres, diagnostic) ;
 *       « profiler » sans argument l'ouvre aussi
 *
 * Instrumenter un programme : DIAG_ZONE / DIAG_FRAME / DIAG_VALUE et
 * diagnostics::trace::StartFromEnvironment (cf. diagnostics/trace.hpp).
 */
#include <vector>

#include <core/core.hpp>

namespace profiler {

[[nodiscard]] const char *Usage() noexcept;

/// Point d'entrée (arguments sans le nom du programme) ; rend le code de sortie.
int Run(const std::vector<String> &args);

/// Visualiseur (fenêtre) ; `capture` vide : rien d'ouvert. `frames` > 0 :
/// quitte après ce nombre d'images, en écrivant `screenshot` (PNG) s'il est donné.
int RunViewer(const String &capture, long frames = 0, const String &screenshot = String(),
			  const String &settingsPath = String("data/profiler/settings.json"));

} // namespace profiler
