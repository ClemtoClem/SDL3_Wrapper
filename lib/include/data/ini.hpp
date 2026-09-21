#pragma once
/**
 * data::IniDocument — codec INI (2 niveaux : sections -> clé=valeur).
 *
 * Les clés situées avant la première ligne `[section]` sont placées
 * directement à la racine (section implicite / globale). Commentaires `;`
 * et `#` supportés. Les valeurs sont auto-typées (bool/int/float/string) via
 * `scalar::ParseNode`, comme pour les autres formats texte de ce module.
 *
 * Aucune exception, aucun std::istringstream : découpage en lignes via
 * String::Lines(), erreurs propagées via Option<ParseError>.
 */
#include "document.hpp"

namespace data {

class IniDocument final : public Document {
public:
	IniDocument() = default;

	[[nodiscard]] String EncodeStr() const override {
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

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override {
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
};

DATA_REGISTER_FORMAT("ini", IniDocument, ".ini", ".cfg")

} // namespace data