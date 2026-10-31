// Définitions de voxelia/studio/report.hpp
#include <voxelia/studio/report.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include <voxel/json.hpp>

namespace voxelia::studio {

namespace json = voxel::json;

const char *FeatureStatusId(FeatureStatus status) noexcept {
	switch (status) {
	case FeatureStatus::Ok:
		return "ok";
	case FeatureStatus::Warning:
		return "warning";
	case FeatureStatus::Error:
		return "error";
	}
	return "ok";
}

void StudioReport::Visit(const String &feature, FeatureStatus status, String detail) {
	FeatureVisit &visit = m_features[feature];
	++visit.visits;
	if (status >= visit.status) {
		visit.status = status;
		if (!detail.IsEmpty())
			visit.detail = std::move(detail);
	}
}

long long StudioReport::Counter(const String &name) const {
	auto it = m_counters.find(name);
	return it == m_counters.end() ? 0 : it->second;
}

FrameSummary StudioReport::Frames() const {
	FrameSummary s;
	if (m_frames.empty())
		return s;
	std::vector<double> sorted = m_frames;
	std::sort(sorted.begin(), sorted.end());
	const size_t n = sorted.size();
	double sum = 0.0;
	for (double f : sorted)
		sum += f;
	auto at = [&](double q) { return sorted[std::min(n - 1, size_t(double(n) * q))]; };
	s.frames = n;
	s.minMs = sorted.front();
	s.maxMs = sorted.back();
	s.avgMs = sum / double(n);
	s.p50Ms = at(0.50);
	s.p95Ms = at(0.95);
	s.p99Ms = at(0.99);
	return s;
}

bool StudioReport::Passed() const {
	if (!m_errors.empty())
		return false;
	for (const auto &[name, visit] : m_features)
		if (visit.status == FeatureStatus::Error)
			return false;
	return true;
}

String StudioReport::ToText() const {
	String out("Voxelia Studio — rapport d'exécution\n");
	out += String(Passed() ? "Résultat : succès\n" : "Résultat : ÉCHEC\n");
	for (const auto &[key, value] : m_info)
		out += key + String(" : ") + value + String("\n");
	const FrameSummary f = Frames();
	out += String::Format("\nCadence : %zu images, min %.2f ms, moy %.2f ms, p50 %.2f ms, p95 %.2f ms, p99 %.2f ms, max %.2f ms\n",
						  f.frames, f.minMs, f.avgMs, f.p50Ms, f.p95Ms, f.p99Ms, f.maxMs);
	out += String::Format("Fils simultanés (max) : %d\n", m_maxThreads);
	out += String::Format("\nFonctionnalités visitées (%zu) :\n", m_features.size());
	for (const auto &[name, visit] : m_features) {
		out += String::Format("  [%-7s] %s ×%zu", FeatureStatusId(visit.status), name.CStr(), visit.visits);
		if (!visit.detail.IsEmpty())
			out += String(" — ") + visit.detail;
		out += String("\n");
	}
	if (!m_counters.empty()) {
		out += String("\nCompteurs :\n");
		for (const auto &[name, value] : m_counters)
			out += String::Format("  %s : %lld\n", name.CStr(), value);
	}
	out += String::Format("\nCaptures écrites (%zu) :\n", m_captures.size());
	for (const String &c : m_captures)
		out += String("  ") + c + String("\n");
	out += String::Format("\nAvertissements (%zu) :\n", m_warnings.size());
	for (const String &w : m_warnings)
		out += String("  ") + w + String("\n");
	out += String::Format("\nErreurs (%zu) :\n", m_errors.size());
	for (const String &e : m_errors)
		out += String("  ") + e + String("\n");
	return out;
}

dataio::NodePtr StudioReport::ToJson() const {
	dataio::NodePtr root = dataio::Node::MakeObject();
	root->Set("passed", json::MakeBool(Passed()));
	dataio::NodePtr info = dataio::Node::MakeObject();
	for (const auto &[key, value] : m_info)
		info->Set(key, json::MakeStr(value));
	root->Set("info", info);
	const FrameSummary f = Frames();
	dataio::NodePtr frames = dataio::Node::MakeObject();
	frames->Set("count", json::MakeInt(int64_t(f.frames)));
	frames->Set("minMs", json::MakeNum(f.minMs));
	frames->Set("avgMs", json::MakeNum(f.avgMs));
	frames->Set("p50Ms", json::MakeNum(f.p50Ms));
	frames->Set("p95Ms", json::MakeNum(f.p95Ms));
	frames->Set("p99Ms", json::MakeNum(f.p99Ms));
	frames->Set("maxMs", json::MakeNum(f.maxMs));
	root->Set("frames", frames);
	root->Set("maxThreads", json::MakeInt(m_maxThreads));
	dataio::NodePtr features = dataio::Node::MakeObject();
	for (const auto &[name, visit] : m_features) {
		dataio::NodePtr v = dataio::Node::MakeObject();
		v->Set("status", json::MakeStr(String(FeatureStatusId(visit.status))));
		v->Set("visits", json::MakeInt(int64_t(visit.visits)));
		v->Set("detail", json::MakeStr(visit.detail));
		features->Set(name, v);
	}
	root->Set("features", features);
	dataio::NodePtr counters = dataio::Node::MakeObject();
	for (const auto &[name, value] : m_counters)
		counters->Set(name, json::MakeInt(value));
	root->Set("counters", counters);
	auto list = [](const std::vector<String> &items) {
		dataio::NodePtr a = dataio::Node::MakeArray();
		for (const String &s : items)
			a->Push(json::MakeStr(s));
		return a;
	};
	root->Set("captures", list(m_captures));
	root->Set("warnings", list(m_warnings));
	root->Set("errors", list(m_errors));
	return root;
}

Result<bool, String> StudioReport::Write(const String &path) const {
	std::error_code ec;
	const std::filesystem::path parent = std::filesystem::path(path.CStr()).parent_path();
	if (!parent.empty())
		std::filesystem::create_directories(parent, ec);
	FILE *file = std::fopen(path.CStr(), "wb");
	if (!file)
		return Err(String("rapport : impossible d'écrire ") + path);
	const String text = ToText();
	const bool ok = std::fwrite(text.CStr(), 1, text.GetSize(), file) == text.GetSize();
	std::fclose(file);
	if (!ok)
		return Err(String("rapport : écriture incomplète de ") + path);
	return json::WriteFile(path + String(".json"), ToJson());
}

} // namespace voxelia::studio
