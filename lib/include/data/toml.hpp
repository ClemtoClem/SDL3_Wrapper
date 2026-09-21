#pragma once
/**
 * data::TomlDocument — codec TOML : SOUS-ENSEMBLE volontairement limité
 * (comme pour YAML, le spec TOML complet — dotted keys, inline tables,
 * array-of-tables `[[x]]`, datetimes, chaînes multi-lignes — est hors de
 * portée raisonnable ici).
 *
 * Supporté : tables `[section]` et tables imbriquées `[section.sous]`
 * (créent une hiérarchie d'Object), assignations `clé = valeur`, chaînes
 * citées "..."/'...', bool/int/float auto-typés, tableaux "inline" d'une
 * seule ligne de scalaires `[1, 2, 3]`, commentaires `# ...`.
 *
 * NON supporté : `[[array_of_tables]]`, tables inline `{k = v}`, clés
 * pointées (`a.b = 1`), chaînes multi-lignes/littérales, dates/heures
 * (gardées telles quelles comme chaînes non citées si non numériques).
 *
 * Aucune exception, aucun std::istringstream : découpage en lignes via
 * String::Lines(). Les valeurs invalides sont signalées via `nullptr`
 * (NodePtr vide) et remontées en Option<ParseError> par DecodeImpl.
 */
#include "document.hpp"

namespace data {

class TomlDocument final : public Document {
public:
	TomlDocument() = default;

	[[nodiscard]] String EncodeStr() const override {
		if (!GetRoot())
			return "";
		String out;
		EncodeScalarsOf(out, GetRoot());
		EncodeTablesOf(out, GetRoot(), "");
		return out;
	}

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override {
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

private:
	// Retire un commentaire `# ...` en fin de ligne, en respectant les guillemets.
	[[nodiscard]] static String StripComment(const String &line) {
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

	[[nodiscard]] static std::vector<String> SplitDotted(const String &path) {
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

	[[nodiscard]] static size_t FindTopLevelEquals(const String &s) {
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

	// Découpe une liste séparée par des virgules en respectant les guillemets
	// et les crochets imbriqués (utilisé pour le contenu d'un tableau inline).
	[[nodiscard]] static std::vector<String> SplitTopLevelCommas(const String &s) {
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

	// Retourne nullptr en cas d'échec (pas d'exception).
	[[nodiscard]] static NodePtr ParseValue(const String &raw) {
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

	[[nodiscard]] static String Unescape(const String &s) {
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

	// ── Encodage ─────────────────────────────────────────────────────────────

	[[nodiscard]] static String Escape(const String &s) {
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

	[[nodiscard]] static String EncodeScalar(const NodePtr &node) {
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

	[[nodiscard]] static String EncodeArrayInline(const NodePtr &arr) {
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

	static void EncodeScalarsOf(String &out, const NodePtr &table) {
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

	static void EncodeTablesOf(String &out, const NodePtr &table, const String &prefix) {
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
};

DATA_REGISTER_FORMAT("toml", TomlDocument, ".toml")

} // namespace data