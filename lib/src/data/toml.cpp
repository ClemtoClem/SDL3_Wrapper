// Définitions de data/toml.hpp
#include "data/toml.hpp"

namespace data {

// ── TomlDocument ─────────────────────────────────────────────────────────────

String TomlDocument::EncodeStr() const {
	if (!GetRoot())
		return "";
	String out;
	EncodeScalarsOf(out, GetRoot());
	EncodeTablesOf(out, GetRoot(), "");
	return out;
}

Option<ParseError> TomlDocument::DecodeImpl(const String &content) {
	auto root = Node::MakeObject();
	NodePtr currentTable = root;
	int lineNo = 0;

	for (auto &rawLine : content.Lines()) {
		++lineNo;
		String trimmed = StripComment(rawLine).Trim();
		if (trimmed.IsEmpty())
			continue;

		if (trimmed.Front() == '[') {
			if (trimmed.GetSize() < 2 || trimmed.Back() != ']')
				return Some(ParseError("TOML: en-tête de table malformé", lineNo));
			if (trimmed[1] == '[')
				return Some(ParseError("TOML: [[array-of-tables]] non supporté par ce sous-ensemble", lineNo));

			String path = trimmed.Substr(1, trimmed.GetSize() - 2).Trim();
			currentTable = root;
			for (auto &part : SplitDotted(path)) {
				if (!currentTable->Has(part))
					currentTable->Set(part, Node::MakeObject());
				auto next = currentTable->Get(part);
				if (!next->IsObject())
					return Some(ParseError("TOML: '" + part + "' redéfinit une clé non-table", lineNo));
				currentTable = next;
			}
			continue;
		}

		auto eq = FindTopLevelEquals(trimmed);
		if (eq == String::NPOS)
			return Some(ParseError("TOML: '=' attendu dans une assignation", lineNo));

		String key = trimmed.Substr(0, eq).Trim();
		String valueText = trimmed.Substr(eq + 1).Trim();
		if (key.IsEmpty())
			return Some(ParseError("TOML: nom de clé vide", lineNo));

		auto value = ParseValue(valueText);
		if (!value)
			return Some(ParseError("TOML: valeur invalide pour '" + key + "'", lineNo));
		currentTable->Set(key, value);
	}

	SetRoot(root);
	return NONE;
}

String TomlDocument::StripComment(const String &line) {
	bool inSingle = false, inDouble = false;
	for (size_t i = 0; i < line.GetSize(); ++i) {
		char c = line[i];
		if (c == '\'' && !inDouble)
			inSingle = !inSingle;
		else if (c == '"' && !inSingle)
			inDouble = !inDouble;
		else if (c == '#' && !inSingle && !inDouble)
			return line.Substr(0, i);
	}
	return line;
}

std::vector<String> TomlDocument::SplitDotted(const String &path) {
	std::vector<String> parts;
	String cur;
	for (char c : path) {
		if (c == '.') {
			parts.push_back(cur.Trim());
			cur.Clear();
		} else
			cur += c;
	}
	parts.push_back(cur.Trim());
	return parts;
}

size_t TomlDocument::FindTopLevelEquals(const String &s) {
	bool inSingle = false, inDouble = false;
	for (size_t i = 0; i < s.GetSize(); ++i) {
		char c = s[i];
		if (c == '\'' && !inDouble)
			inSingle = !inSingle;
		else if (c == '"' && !inSingle)
			inDouble = !inDouble;
		else if (c == '=' && !inSingle && !inDouble)
			return i;
	}
	return String::NPOS;
}

std::vector<String> TomlDocument::SplitTopLevelCommas(const String &s) {
	std::vector<String> parts;
	String cur;
	bool inSingle = false, inDouble = false;
	int depth = 0;
	for (char c : s) {
		if (c == '\'' && !inDouble)
			inSingle = !inSingle;
		else if (c == '"' && !inSingle)
			inDouble = !inDouble;
		else if (!inSingle && !inDouble) {
			if (c == '[')
				++depth;
			else if (c == ']')
				--depth;
			else if (c == ',' && depth == 0) {
				parts.push_back(cur);
				cur.Clear();
				continue;
			}
		}
		cur += c;
	}
	if (!cur.Trim().IsEmpty())
		parts.push_back(cur);
	return parts;
}

NodePtr TomlDocument::ParseValue(const String &raw) {
	String s = raw.Trim();
	if (s.IsEmpty())
		return nullptr;

	if (s.Front() == '"' && s.Back() == '"' && s.GetSize() >= 2)
		return Node::MakeString(Unescape(s.Substr(1, s.GetSize() - 2)));
	if (s.Front() == '\'' && s.Back() == '\'' && s.GetSize() >= 2)
		return Node::MakeString(s.Substr(1, s.GetSize() - 2));

	if (s.Front() == '[' && s.Back() == ']') {
		auto arr = Node::MakeArray();
		String inner = s.Substr(1, s.GetSize() - 2);
		for (auto &part : SplitTopLevelCommas(inner)) {
			auto v = ParseValue(part.Trim());
			if (!v)
				return nullptr;
			arr->Push(v);
		}
		return arr;
	}

	if (auto b = scalar::ParseBool(s.View()))
		return Node::MakeBool(*b);
	if (auto i = scalar::ParseInt(s.View()))
		return Node::MakeInt(*i);
	if (auto f = scalar::ParseFloat(s.View()))
		return Node::MakeFloat(*f);

	// Non reconnu comme scalaire TOML canonique (ex: date/heure) : conservé
	// tel quel en chaîne plutôt que de rejeter le document.
	return Node::MakeString(s);
}

String TomlDocument::Unescape(const String &s) {
	String out;
	out.Reserve(s.GetSize());
	for (size_t i = 0; i < s.GetSize(); ++i) {
		if (s[i] == '\\' && i + 1 < s.GetSize()) {
			char n = s[++i];
			switch (n) {
				case 'n':
					out += '\n';
					break;
				case 't':
					out += '\t';
					break;
				case 'r':
					out += '\r';
					break;
				case '"':
					out += '"';
					break;
				case '\\':
					out += '\\';
					break;
				default:
					out += n;
			}
		} else {
			out += s[i];
		}
	}
	return out;
}

String TomlDocument::Escape(const String &s) {
	String out;
	for (char c : s) {
		if (c == '"')
			out.Append("\\\"");
		else if (c == '\\')
			out.Append("\\\\");
		else if (c == '\n')
			out.Append("\\n");
		else
			out.Append(c);
	}
	return out;
}

String TomlDocument::EncodeScalar(const NodePtr &node) {
	switch (node->type) {
		case NodeType::BOOL:
			return node->boolValue ? "true" : "false";
		case NodeType::INT:
			return String::From(node->intValue);
		case NodeType::FLOAT:
			return String::FromStream(node->floatValue);
		case NodeType::STRING:
			return String("\"") + Escape(node->stringValue) + "\""; // TOML : les chaînes sont toujours citées
		default:
			return "\"\"";
	}
}

String TomlDocument::EncodeArrayInline(const NodePtr &arr) {
	String out;
	out.Append('[');
	for (size_t i = 0; i < arr->GetSize(); ++i) {
		if (i)
			out.Append(", ");
		auto item = arr->At(i);
		out.Append(item->IsArray() ? EncodeArrayInline(item) : EncodeScalar(item));
	}
	out.Append(']');
	return out;
}

void TomlDocument::EncodeScalarsOf(String &out, const NodePtr &table) {
	for (auto &key : table->Keys()) {
		auto val = table->Get(key);
		if (val->IsObject())
			continue; // écrit dans EncodeTablesOf
		out.Append(key);
		out.Append(" = ");
		out.Append(val->IsArray() ? EncodeArrayInline(val) : EncodeScalar(val));
		out.Append('\n');
	}
}

void TomlDocument::EncodeTablesOf(String &out, const NodePtr &table, const String &prefix) {
	for (auto &key : table->Keys()) {
		auto val = table->Get(key);
		if (!val->IsObject())
			continue;
		String path = prefix.IsEmpty() ? key : (prefix + "." + key);
		out.Append('\n');
		out.Append('[');
		out.Append(path);
		out.Append("]\n");
		EncodeScalarsOf(out, val);
		EncodeTablesOf(out, val, path);
	}
}

} // namespace data
