#pragma once
/**
 * diagnostics::trace — capture de performances d'un programme, à la manière
 * de Google Orbit : ce qu'il faut pour savoir OÙ part le temps (CPU, fils,
 * mémoire, cache, GPU), enregistré dans un fichier de capture que le
 * profiler (demos/profiler) analyse et affiche.
 *
 *  - Instrumentation manuelle (portées, images, valeurs, intervalles
 *    asynchrones) : coût quasi nul hors capture (un booléen atomique lu).
 *  - Échantillonnage des piles d'appels : minuterie SIGPROF (temps CPU du
 *    processus) — chaque échantillon est la pile du fil qui consommait le
 *    processeur ; symbolisation hors ligne (carte des modules jointe).
 *  - Échantillonneur du processus (100 Hz par défaut) : TOUS les fils (y
 *    compris ceux de SDL et du pilote GPU), leur temps CPU et leur état
 *    (en cours, prêt, en attente…), mémoire résidente et tas, défauts de
 *    page, changements de contexte, compteurs matériels (cycles,
 *    instructions, défauts de cache, mauvaises prédictions) si le noyau le
 *    permet (perf_event_paranoid ≤ 2).
 *
 * @code{.cpp}
 * void Physics::Step() {
 *     DIAG_ZONE("physics.step");          // portée chronométrée
 *     DIAG_VALUE("physics.bodies", n);    // piste de valeurs
 * }
 * while (running) { ...; DIAG_FRAME(); }  // une image
 * @endcode
 *
 * Pilotage par l'environnement (cf. StartFromEnvironment) :
 *   DIAG_TRACE=capture.vcap   capture dès le démarrage, écrite à la sortie ;
 *   DIAG_TRACE_HZ=1000        échantillonnage des piles (0 : aucun) ;
 *   DIAG_TRACE_CONTROL=1      SIGUSR2 démarre / arrête une capture (le
 *                             profiler s'en sert pour capturer un programme
 *                             déjà lancé) ; chaque arrêt écrit un fichier.
 */
#include <cstdint>
#include <vector>

#include <core/core.hpp>

namespace diagnostics::trace {

struct Options {
	size_t maxZonesPerThread = size_t(1) << 20;
	/// Échantillons de pile par seconde de temps CPU (0 : aucun).
	int samplingHz = 1000;
	size_t maxSamples = 200000;
	/// Échantillons du processus et des fils par seconde (0 : aucun).
	int processHz = 100;
	bool hardwareCounters = true;
};

/// Démarre une capture (efface la précédente) ; faux si déjà en cours.
bool Start(const Options &options = Options{});
/// Arrête la capture (les données restent jusqu'au prochain Start).
bool Stop();
[[nodiscard]] bool Active() noexcept;
/// Écrit la dernière capture (après Stop) au format « vcap » (JSON).
Result<bool, String> Save(const String &path);

/// DIAG_TRACE / DIAG_TRACE_HZ / DIAG_TRACE_CONTROL (cf. en-tête) ; vrai si
/// une capture a démarré.
bool StartFromEnvironment();
/// À appeler à chaque image : traite les demandes de SIGUSR2 (démarrer, ou
/// arrêter et écrire `<DIAG_TRACE>.<n>.vcap`).
void PollControl();
/// Fin du programme : arrête et écrit la capture lancée par l'environnement.
void FinishFromEnvironment();

/// Nom du fil courant dans les captures (et pour le système).
void SetThreadName(StringView name);
/// Nom durable (les zones gardent un pointeur) : chaîne de caractères copiée
/// une fois dans une table globale.
[[nodiscard]] const char *Intern(StringView name);

/// Fin d'une image (fil principal).
void Frame();
/// Valeur d'une piste (mémoire, nombre d'objets…), horodatée.
void Value(const char *name, double value);
/// Intervalle qui ne suit pas la pile (chargement, requête…) : `id` relie
/// début et fin, éventuellement sur deux fils différents.
void AsyncBegin(const char *name, uint64_t id);
void AsyncEnd(const char *name, uint64_t id);

/// Horloge des captures (CLOCK_MONOTONIC, ns).
[[nodiscard]] uint64_t NowNs() noexcept;

/// Portée chronométrée (cf. DIAG_ZONE). `name` doit vivre jusqu'à Save :
/// littéral ou Intern().
class Zone {
public:
	explicit Zone(const char *name) noexcept;
	~Zone();
	Zone(const Zone &) = delete;
	Zone &operator=(const Zone &) = delete;

private:
	const char *m_name = nullptr;
	uint64_t m_start = 0;
	uint32_t m_depth = 0;
};

} // namespace diagnostics::trace

#define DIAG_TRACE_CAT_(a, b) a##b
#define DIAG_TRACE_CAT(a, b) DIAG_TRACE_CAT_(a, b)
#define DIAG_ZONE(name) ::diagnostics::trace::Zone DIAG_TRACE_CAT(diag_zone_, __LINE__)(name)
#define DIAG_FRAME() ::diagnostics::trace::Frame()
#define DIAG_VALUE(name, value) ::diagnostics::trace::Value(name, double(value))
