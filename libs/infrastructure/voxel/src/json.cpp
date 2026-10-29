// Définitions de voxel/json.hpp
#include <voxel/json.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <dataio/documents/json.hpp>

namespace voxel::json {

namespace {

bool IsIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
bool IsIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '-'; }

} // namespace

String Normalize(const String &text) {
	const std::string in(text.CStr(), text.GetSize());
	std::string out;
	out.reserve(in.size() + 16);
	// Dernier caractère significatif écrit (hors blancs) : décide si un
	// identifiant nu est une CLÉ (après « { » ou « , ») et repère les
	// virgules finales.
	auto lastSignificant = [&]() -> char {
		for (size_t k = out.size(); k > 0; --k)
			if (!std::isspace(static_cast<unsigned char>(out[k - 1])))
				return out[k - 1];
		return '\0';
	};
	for (size_t i = 0; i < in.size();) {
		const char c = in[i];
		// Chaînes : recopiées telles quelles ('…' devient "…").
		if (c == '"' || c == '\'') {
			const char quote = c;
			out += '"';
			++i;
			while (i < in.size() && in[i] != quote) {
				if (in[i] == '\\' && i + 1 < in.size()) {
					out += in[i];
					out += in[i + 1];
					i += 2;
					continue;
				}
				if (quote == '\'' && in[i] == '"')
					out += '\\';
				out += in[i++];
			}
			out += '"';
			++i;
			continue;
		}
		// Commentaires.
		if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
			while (i < in.size() && in[i] != '\n')
				++i;
			continue;
		}
		if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
			i += 2;
			while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/'))
				++i;
			i += 2;
			continue;
		}
		// Virgule finale : « , » suivi (blancs/commentaires exclus) de } ou ].
		if (c == ',') {
			size_t j = i + 1;
			while (j < in.size()) {
				if (std::isspace(static_cast<unsigned char>(in[j]))) {
					++j;
				} else if (in[j] == '/' && j + 1 < in.size() && in[j + 1] == '/') {
					while (j < in.size() && in[j] != '\n')
						++j;
				} else if (in[j] == '/' && j + 1 < in.size() && in[j + 1] == '*') {
					j += 2;
					while (j + 1 < in.size() && !(in[j] == '*' && in[j + 1] == '/'))
						++j;
					j += 2;
				} else {
					break;
				}
			}
			if (j < in.size() && (in[j] == '}' || in[j] == ']')) {
				i = j;
				continue;
			}
			out += c;
			++i;
			continue;
		}
		// Clé sans guillemets : identifiant après { ou , et suivi de « : ».
		if (IsIdentStart(c) && (lastSignificant() == '{' || lastSignificant() == ',')) {
			size_t j = i;
			while (j < in.size() && IsIdentChar(in[j]))
				++j;
			size_t k = j;
			while (k < in.size() && std::isspace(static_cast<unsigned char>(in[k])))
				++k;
			if (k < in.size() && in[k] == ':') {
				out += '"';
				out.append(in, i, j - i);
				out += '"';
				i = j;
				continue;
			}
		}
		out += c;
		++i;
	}
	return String(out.c_str(), out.size());
}

Result<NodePtr, String> Parse(const String &text) {
	dataio::JsonDocument document;
	if (auto error = document.DecodeStr(Normalize(text)); error.IsSome())
		return Err(error.Unwrap().Format());
	return Ok(document.GetRoot());
}

Result<NodePtr, String> ReadFile(const String &path) {
	std::ifstream file(path.CStr(), std::ios::binary);
	if (!file)
		return Err(String::Format("%s : fichier illisible", path.CStr()));
	std::stringstream buffer;
	buffer << file.rdbuf();
	const std::string text = buffer.str();
	Result<NodePtr, String> parsed = Parse(String(text.c_str(), text.size()));
	if (parsed.IsError())
		return Err(String::Format("%s : %s", path.CStr(), parsed.Error().CStr()));
	return parsed;
}

String Encode(const NodePtr &node) {
	dataio::JsonDocument document;
	document.SetRoot(node);
	return document.EncodeStr();
}

Result<bool, String> WriteFile(const String &path, const NodePtr &node) {
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(path.CStr()).parent_path(), ec);
	std::ofstream file(path.CStr(), std::ios::binary | std::ios::trunc);
	if (!file)
		return Err(String::Format("%s : écriture impossible", path.CStr()));
	const String text = Encode(node);
	file.write(text.CStr(), std::streamsize(text.GetSize()));
	return Ok(bool(file));
}

NodePtr At(const NodePtr &node, const char *path) {
	NodePtr current = node;
	const char *p = path;
	while (current && *p) {
		const char *dot = p;
		while (*dot && *dot != '.')
			++dot;
		const String key(p, size_t(dot - p));
		if (!current->IsObject() && !current->IsArray())
			return nullptr;
		current = current->Has(key) ? current->Get(key) : nullptr;
		p = *dot ? dot + 1 : dot;
	}
	return current;
}

String Str(const NodePtr &node, const char *path, const String &fallback) {
	NodePtr n = At(node, path);
	return n && n->IsString() ? n->stringValue : fallback;
}

double Num(const NodePtr &node, const char *path, double fallback) {
	NodePtr n = At(node, path);
	if (!n)
		return fallback;
	if (n->IsFloat())
		return n->floatValue;
	if (n->IsInt())
		return double(n->intValue);
	if (n->IsBool())
		return n->boolValue ? 1.0 : 0.0;
	return fallback;
}

int Int(const NodePtr &node, const char *path, int fallback) { return int(std::lround(Num(node, path, fallback))); }

bool Bool(const NodePtr &node, const char *path, bool fallback) {
	NodePtr n = At(node, path);
	if (!n)
		return fallback;
	if (n->IsBool())
		return n->boolValue;
	if (n->IsNumber())
		return Num(n, "", 0.0) != 0.0;
	return fallback;
}

bool Has(const NodePtr &node, const char *path) { return At(node, path) != nullptr; }

math::FVector3 Vec3(const NodePtr &node, const char *path, math::FVector3 fallback) {
	NodePtr n = At(node, path);
	if (!n)
		return fallback;
	if (n->IsArray() && n->GetSize() >= 3)
		return {float(Num(n->At(0), "", fallback.x)), float(Num(n->At(1), "", fallback.y)), float(Num(n->At(2), "", fallback.z))};
	if (n->IsObject())
		return {float(Num(n, "x", fallback.x)), float(Num(n, "y", fallback.y)), float(Num(n, "z", fallback.z))};
	if (n->IsNumber()) {
		const float v = float(Num(n, "", 0.0));
		return {v, v, v};
	}
	return fallback;
}

std::vector<String> StrList(const NodePtr &node, const char *path) {
	std::vector<String> out;
	NodePtr n = At(node, path);
	if (!n)
		return out;
	if (n->IsString()) {
		out.push_back(n->stringValue);
		return out;
	}
	if (n->IsArray())
		for (size_t i = 0; i < n->GetSize(); ++i)
			if (NodePtr item = n->At(i); item && item->IsString())
				out.push_back(item->stringValue);
	return out;
}

std::vector<std::vector<double>> NumRows(const NodePtr &node, const char *path) {
	std::vector<std::vector<double>> rows;
	NodePtr n = At(node, path);
	if (!n || !n->IsArray())
		return rows;
	// Un seul tableau de nombres compte comme une ligne.
	if (n->GetSize() > 0 && n->At(0) && n->At(0)->IsNumber()) {
		std::vector<double> row;
		for (size_t i = 0; i < n->GetSize(); ++i)
			row.push_back(Num(n->At(i), "", 0.0));
		rows.push_back(std::move(row));
		return rows;
	}
	for (size_t r = 0; r < n->GetSize(); ++r) {
		NodePtr line = n->At(r);
		if (!line || !line->IsArray())
			continue;
		std::vector<double> row;
		for (size_t i = 0; i < line->GetSize(); ++i)
			row.push_back(Num(line->At(i), "", 0.0));
		rows.push_back(std::move(row));
	}
	return rows;
}

NodePtr MakeStr(const String &v) { return dataio::Node::MakeString(v); }
NodePtr MakeNum(double v) { return dataio::Node::MakeFloat(v); }
NodePtr MakeInt(int64_t v) { return dataio::Node::MakeInt(v); }
NodePtr MakeBool(bool v) { return dataio::Node::MakeBool(v); }
NodePtr MakeVec3(const math::FVector3 &v) {
	NodePtr n = dataio::Node::MakeObject();
	n->Set("x", MakeNum(v.x));
	n->Set("y", MakeNum(v.y));
	n->Set("z", MakeNum(v.z));
	return n;
}

} // namespace voxel::json
