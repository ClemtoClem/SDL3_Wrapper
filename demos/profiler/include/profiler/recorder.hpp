#pragma once
/**
 * Lancement et capture d'un programme instrumenté (diagnostics::trace),
 * comme le service d'Orbit côté machine cible :
 *
 *  - Record : lance la commande avec DIAG_TRACE ; sans durée, la capture
 *    couvre toute l'exécution (écrite à la sortie) ; avec un délai et une
 *    durée, le programme démarre sans capturer (DIAG_TRACE_CONTROL=wait),
 *    puis SIGUSR2 démarre et arrête la capture — la mise en route (chargement)
 *    n'y figure pas ;
 *  - Attach : capture pendant `duration` secondes un programme DÉJÀ lancé
 *    avec DIAG_TRACE_CONTROL=1 (chemin de capture lu dans son environnement).
 */
#include <vector>

#include <core/core.hpp>

namespace profiler {

struct RecordOptions {
	std::vector<String> command; ///< programme et arguments
	String output = "capture.vcap";
	int samplingHz = 1000;
	double delaySeconds = 0.0;	  ///< > 0 ou durée > 0 : capture pilotée
	double durationSeconds = 0.0; ///< 0 : jusqu'à la sortie du programme
	bool stopAfterCapture = true; ///< capture pilotée : termine le programme ensuite
};

/// Rend le chemin de la capture écrite.
[[nodiscard]] Result<String, String> Record(const RecordOptions &options);
[[nodiscard]] Result<String, String> Attach(long pid, double durationSeconds);

} // namespace profiler
