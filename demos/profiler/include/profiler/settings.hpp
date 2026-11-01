#pragma once
/**
 * Réglages du profiler : ./data/profiler/settings.json par défaut (créé avec
 * les valeurs par défaut au premier lancement), et les captures rangées dans
 * ./data/profiler/captures/ (« <programme>-<date>-<heure>.vcap »).
 *
 * Champs (tous facultatifs) :
 *   capturesDirectory   dossier des captures enregistrées
 *   samplingHz          échantillons de pile par seconde de temps CPU
 *   targetFps           objectif de cadence du diagnostic
 *   top                 lignes des tableaux du rapport texte
 *   delaySeconds, durationSeconds, stopAfterCapture   capture pilotée par défaut
 *   lastCommand         dernière commande lancée (visualiseur)
 *   recentCaptures      captures ouvertes récemment (visualiseur, 10 au plus)
 */
#include <vector>

#include <core/core.hpp>

namespace profiler {

inline constexpr const char *DEFAULT_SETTINGS = "data/profiler/settings.json";
inline constexpr const char *DEFAULT_CAPTURES = "data/profiler/captures";

struct Settings {
	String capturesDirectory = DEFAULT_CAPTURES;
	int samplingHz = 1000;
	double targetFps = 144.0;
	size_t top = 20;
	double delaySeconds = 0.0, durationSeconds = 0.0;
	bool stopAfterCapture = true;
	String lastCommand;
	std::vector<String> recentCaptures;
	/// Fichier d'où viennent les réglages (non enregistré).
	String file = DEFAULT_SETTINGS;

	/// Lit `path` ; absent : valeurs par défaut, et le fichier est créé.
	[[nodiscard]] static Settings Load(const String &path = String(DEFAULT_SETTINGS));
	Result<bool, String> Save(const String &path = String(DEFAULT_SETTINGS)) const;

	/// Chemin d'une nouvelle capture du programme `program` (dossier créé).
	[[nodiscard]] String NewCapturePath(const String &program) const;
	/// Captures du dossier, la plus récente d'abord.
	[[nodiscard]] std::vector<String> Captures() const;
	/// Place `path` en tête des captures récentes.
	void Remember(const String &path);
};

} // namespace profiler
