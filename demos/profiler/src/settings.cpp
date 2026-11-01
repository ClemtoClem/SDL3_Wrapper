// Définitions de profiler/settings.hpp
#include <profiler/settings.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <dataio/documents/json.hpp>

namespace profiler {

namespace {

namespace fs = std::filesystem;

dataio::NodePtr Field(const dataio::NodePtr &root, const char *key) { return root ? root->Get(String(key)) : nullptr; }

double Number(const dataio::NodePtr &root, const char *key, double fallback) {
	const dataio::NodePtr n = Field(root, key);
	if (!n || !n->IsNumber())
		return fallback;
	return n->IsInt() ? double(n->intValue) : n->floatValue;
}

String Text(const dataio::NodePtr &root, const char *key, const String &fallback) {
	const dataio::NodePtr n = Field(root, key);
	return n && n->IsString() ? n->stringValue : fallback;
}

bool Flag(const dataio::NodePtr &root, const char *key, bool fallback) {
	const dataio::NodePtr n = Field(root, key);
	return n && n->IsBool() ? n->boolValue : fallback;
}

bool ReadText(const String &path, String &out) {
	FILE *f = std::fopen(path.CStr(), "rb");
	if (!f)
		return false;
	char buffer[4096];
	size_t n = 0;
	while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0)
		out += String(StringView(buffer, n));
	std::fclose(f);
	return true;
}

} // namespace

Settings Settings::Load(const String &path) {
	Settings s;
	s.file = path;
	String text;
	if (!ReadText(path, text)) {
		(void)s.Save(path); // premier lancement : le fichier documente les réglages
		return s;
	}
	dataio::JsonDocument doc;
	if (const Option<dataio::ParseError> error = doc.DecodeStr(text); error.IsSome()) {
		std::fprintf(stderr, "profiler : %s illisible (%s) — réglages par défaut\n", path.CStr(), error.Value().Format().CStr());
		return s;
	}
	const dataio::NodePtr root = doc.GetRoot();
	s.capturesDirectory = Text(root, "capturesDirectory", s.capturesDirectory);
	s.samplingHz = std::max(0, int(Number(root, "samplingHz", s.samplingHz)));
	s.targetFps = std::max(1.0, Number(root, "targetFps", s.targetFps));
	s.top = size_t(std::max(1.0, Number(root, "top", double(s.top))));
	s.delaySeconds = std::max(0.0, Number(root, "delaySeconds", s.delaySeconds));
	s.durationSeconds = std::max(0.0, Number(root, "durationSeconds", s.durationSeconds));
	s.stopAfterCapture = Flag(root, "stopAfterCapture", s.stopAfterCapture);
	s.lastCommand = Text(root, "lastCommand", s.lastCommand);
	if (const dataio::NodePtr recent = Field(root, "recentCaptures"); recent && recent->IsArray())
		for (size_t i = 0; i < recent->GetSize(); ++i)
			if (const dataio::NodePtr item = recent->At(i); item && item->IsString())
				s.recentCaptures.push_back(item->stringValue);
	return s;
}

Result<bool, String> Settings::Save(const String &path) const {
	dataio::NodePtr root = dataio::Node::MakeObject();
	root->Set(String("capturesDirectory"), dataio::Node::MakeString(capturesDirectory));
	root->Set(String("samplingHz"), dataio::Node::MakeInt(samplingHz));
	root->Set(String("targetFps"), dataio::Node::MakeFloat(targetFps));
	root->Set(String("top"), dataio::Node::MakeInt(int64_t(top)));
	root->Set(String("delaySeconds"), dataio::Node::MakeFloat(delaySeconds));
	root->Set(String("durationSeconds"), dataio::Node::MakeFloat(durationSeconds));
	root->Set(String("stopAfterCapture"), dataio::Node::MakeBool(stopAfterCapture));
	root->Set(String("lastCommand"), dataio::Node::MakeString(lastCommand));
	dataio::NodePtr recent = dataio::Node::MakeArray();
	for (const String &r : recentCaptures)
		recent->Push(dataio::Node::MakeString(r));
	root->Set(String("recentCaptures"), recent);
	dataio::JsonDocument doc;
	doc.SetRoot(root);
	std::error_code ec;
	const fs::path target(path.CStr());
	if (target.has_parent_path())
		fs::create_directories(target.parent_path(), ec);
	FILE *f = std::fopen(path.CStr(), "w");
	if (!f)
		return Err(String::Format("%s : %s", path.CStr(), std::strerror(errno)));
	const String text = doc.EncodeStr();
	const bool ok = std::fwrite(text.CStr(), 1, text.Length(), f) == text.Length();
	std::fclose(f);
	return ok ? Result<bool, String>(Ok(true)) : Result<bool, String>(Err(path + String(" : écriture incomplète")));
}

String Settings::NewCapturePath(const String &program) const {
	std::error_code ec;
	fs::create_directories(capturesDirectory.CStr(), ec);
	String name(fs::path(program.CStr()).filename().string().c_str());
	if (name.IsEmpty())
		name = String("capture");
	const std::time_t now = std::time(nullptr);
	std::tm local{};
	localtime_r(&now, &local);
	char stamp[32];
	std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
	String path = String::Format("%s/%s-%s.vcap", capturesDirectory.CStr(), name.CStr(), stamp);
	for (int i = 2; fs::exists(path.CStr()); ++i) // deux captures dans la même seconde
		path = String::Format("%s/%s-%s-%d.vcap", capturesDirectory.CStr(), name.CStr(), stamp, i);
	return path;
}

std::vector<String> Settings::Captures() const {
	std::vector<std::pair<fs::file_time_type, String>> found;
	std::error_code ec;
	for (const fs::directory_entry &e : fs::directory_iterator(capturesDirectory.CStr(), ec))
		if (e.path().extension() == ".vcap")
			found.push_back({fs::last_write_time(e.path(), ec), String(e.path().string().c_str())});
	std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	std::vector<String> out;
	for (auto &[time, path] : found)
		out.push_back(std::move(path));
	return out;
}

void Settings::Remember(const String &path) {
	std::erase(recentCaptures, path);
	recentCaptures.insert(recentCaptures.begin(), path);
	if (recentCaptures.size() > 10)
		recentCaptures.resize(10);
}

} // namespace profiler
