// Définitions de data/ini.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/ini.hpp"

namespace data {

// ── IniDocument ──────────────────────────────────────────────────────────────

String IniDocument::EncodeStr() const {
	if (!GetRoot())
		return "";
	String out;

	// 1. Clés scalaires de la section globale (implicite) d'abord.
	for (auto &key : GetRoot()->Keys()) {
		auto val = GetRoot()->Get(key);
		if (val && !val->IsObject()) {
			out.Append(key);
			out.Append(" = ");
			out.Append(scalar::ToString(val));
			out.Append('\n');
		}
	}

	// 2. Puis chaque section (une clé Object = une section).
	for (auto &key : GetRoot()->Keys()) {
		auto val = GetRoot()->Get(key);
		if (!val || !val->IsObject())
			continue;
		out.Append('[');
		out.Append(key);
		out.Append("]\n");
		for (auto &subKey : val->Keys()) {
			out.Append(subKey);
			out.Append(" = ");
			out.Append(scalar::ToString(val->Get(subKey)));
			out.Append('\n');
		}
	}
	return out;
}

Option<ParseError> IniDocument::DecodeImpl(const String &content) {
	auto root = Node::MakeObject();
	NodePtr currentSection = root;
	int lineNo = 0;

	for (auto &raw : content.Lines()) {
		++lineNo;
		String trimmed = raw.Trim();
		if (trimmed.IsEmpty() || trimmed.Front() == ';' || trimmed.Front() == '#')
			continue;

		if (trimmed.Front() == '[') {
			auto close = trimmed.Find(']');
			if (close == String::NPOS)
				return Some(ParseError("INI: ']' manquant après un nom de section", lineNo));
			String sectionName = trimmed.Substr(1, close - 1).Trim();
			if (!root->Has(sectionName))
				root->Set(sectionName, Node::MakeObject());
			currentSection = root->Get(sectionName);
			continue;
		}

		auto eq = trimmed.Find('=');
		if (eq == String::NPOS)
			return Some(ParseError("INI: '=' attendu dans une affectation clé=valeur", lineNo));

		String key = trimmed.Substr(0, eq).Trim();
		String value = trimmed.Substr(eq + 1).Trim();
		if (key.IsEmpty())
			return Some(ParseError("INI: nom de clé vide", lineNo));

		currentSection->Set(key, scalar::ParseNode(value.View()));
	}

	SetRoot(root);
	return NONE;
}

} // namespace data
