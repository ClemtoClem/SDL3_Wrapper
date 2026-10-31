#pragma once
/**
 * Rapport d'exécution du studio (texte et JSON) : fonctionnalités visitées et
 * leur statut, cadence (min / moyenne / centiles), fils de travail, captures
 * écrites, avertissements et erreurs. Écrit à la sortie avec --report=FICHIER
 * (le JSON à côté : FICHIER.json).
 *
 * Une fonctionnalité visitée plusieurs fois garde son PIRE statut (une seule
 * erreur suffit à la marquer en échec) et compte ses visites.
 */
#include <algorithm>
#include <map>
#include <vector>

#include <core/core.hpp>
#include <dataio/node.hpp>

namespace voxelia::studio {

enum class FeatureStatus : uint8_t { Ok, Warning, Error };

struct FeatureVisit {
	FeatureStatus status = FeatureStatus::Ok;
	size_t visits = 0;
	String detail; ///< dernier détail (ou celui du pire statut)
};

struct FrameSummary {
	size_t frames = 0;
	double minMs = 0.0, avgMs = 0.0, p50Ms = 0.0, p95Ms = 0.0, p99Ms = 0.0, maxMs = 0.0;
};

class StudioReport {
public:
	void Visit(const String &feature, FeatureStatus status = FeatureStatus::Ok, String detail = String());
	void Frame(double milliseconds) { m_frames.push_back(milliseconds); }
	void Capture(const String &path) { m_captures.push_back(path); }
	void Warning(String text) { m_warnings.push_back(std::move(text)); }
	void Error(String text) { m_errors.push_back(std::move(text)); }
	/// Compteurs libres (entités listées, commandes exécutées…).
	void Count(const String &name, long long delta = 1) { m_counters[name] += delta; }
	/// Fils de travail observés (fil « voxel-update », fils « voxel-render-N »).
	void Threads(int simultaneous) { m_maxThreads = std::max(m_maxThreads, simultaneous); }
	void Set(const String &key, String value) { m_info[key] = std::move(value); }

	[[nodiscard]] const std::map<String, FeatureVisit> &Features() const noexcept { return m_features; }
	[[nodiscard]] const std::vector<String> &Errors() const noexcept { return m_errors; }
	[[nodiscard]] const std::vector<String> &Captures() const noexcept { return m_captures; }
	[[nodiscard]] long long Counter(const String &name) const;
	[[nodiscard]] int MaxThreads() const noexcept { return m_maxThreads; }
	[[nodiscard]] FrameSummary Frames() const;
	/// Aucune fonctionnalité en échec, aucune erreur.
	[[nodiscard]] bool Passed() const;

	[[nodiscard]] String ToText() const;
	[[nodiscard]] dataio::NodePtr ToJson() const;
	/// `path` (texte) et `path`.json.
	Result<bool, String> Write(const String &path) const;

private:
	std::map<String, FeatureVisit> m_features;
	std::vector<double> m_frames;
	std::vector<String> m_captures, m_warnings, m_errors;
	std::map<String, long long> m_counters;
	std::map<String, String> m_info;
	int m_maxThreads = 0;
};

[[nodiscard]] const char *FeatureStatusId(FeatureStatus status) noexcept; ///< "ok", "warning", "error"

} // namespace voxelia::studio
