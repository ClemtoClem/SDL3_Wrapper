#pragma once
/**
 * data::YamlDocument — codec YAML : SOUS-ENSEMBLE "block style" volontairement
 * limité (YAML complet est un des formats les plus complexes qui existent —
 * réimplémenter tout le spec 1.2 est hors de portée raisonnable ici).
 *
 * Supporté : mappings et séquences en style bloc (indentation), imbrication
 * arbitraire, scalaires auto-typés (bool/int/float/string), chaînes citées
 * '...'/"...", commentaires `# ...`, `-` seul suivi d'un bloc imbriqué pour
 * les éléments de séquence composites (objet/tableau).
 *
 * NON supporté (limitations documentées, pas des bugs) : style flow inline
 * (`[a, b]`, `{k: v}`), ancres/alias (`&`/`*`), tags (`!!str`), scalaires
 * bloc littéral/plié (`|`, `>`), clés complexes, multi-documents (`---`
 * séparateurs simplement ignorés, traités comme un seul document), la forme
 * compacte `- key: value` (le premier champ d'un mapping directement sur la
 * ligne du tiret) — cette dernière EST cependant supportée en lecture, mais
 * l'encodeur produit systématiquement la forme développée (`-` seul puis
 * mapping indenté dessous) pour rester simple et non ambigu.
 *
 * Aucune exception, aucun std::istringstream : le fichier est découpé en
 * lignes via String::Lines().
 */
#include <utility>

#include "document.hpp"

namespace data {

class YamlDocument final : public Document {
public:
	YamlDocument() = default;

	[[nodiscard]] String EncodeStr() const override {
		if (!GetRoot())
			return "";
		String out;
		EncodeMapping(out, GetRoot(), 0);
		return out;
	}

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override {
		auto lines = Preprocess(content);
		size_t idx = 0;
		NodePtr root = lines.empty() ? Node::MakeObject() : ParseBlock(lines, idx, lines[0].indent);
		if (!root->IsObject() && !root->IsArray()) {
			// Document YAML réduit à un unique scalaire — on l'enveloppe
			// pour rester cohérent avec le contrat "racine = Object" des
			// autres formats de ce module.
			auto wrapper = Node::MakeObject();
			wrapper->Set("value", root);
			root = wrapper;
		}
		SetRoot(root);
		return NONE;
	}

private:
	// ── Prétraitement : une ligne logique = (indentation, contenu, est-un-item-de-séquence) ──

	struct PLine {
		int indent;
		String text;
		bool isListItem;
		int lineNo;
	};

	[[nodiscard]] static std::vector<PLine> Preprocess(const String &content) {
		std::vector<PLine> out;
		int lineNo = 0;
		for (auto &raw : content.Lines()) {
			++lineNo;

			size_t firstNonSpace = 0;
			while (firstNonSpace < raw.GetSize() && raw[firstNonSpace] == ' ')
				++firstNonSpace;
			if (firstNonSpace == raw.GetSize())
				continue; // ligne vide
			String text = raw.Substr(firstNonSpace);
			if (text[0] == '#')
				continue; // commentaire pleine ligne
			if (text == "---" || text == "...")
				continue; // séparateur de document — ignoré

			int indent = int(firstNonSpace);
			bool isListItem = false;
			if (text[0] == '-' && (text.GetSize() == 1 || text[1] == ' ')) {
				isListItem = true;
				size_t p = firstNonSpace + 1;
				while (p < raw.GetSize() && raw[p] == ' ')
					++p;
				if (p >= raw.GetSize()) {
					// "-" seul (rien après, pas même un espace) : le contenu
					// imbriqué est conventionnellement indenté de 2 colonnes
					// de plus que le tiret lui-même (cf. EncodeSequence()).
					indent = int(firstNonSpace) + 2;
					out.push_back({indent, String(), isListItem, lineNo});
					continue;
				}
				indent = int(p);
				text = raw.Substr(p);
			}
			out.push_back({indent, text, isListItem, lineNo});
		}
		return out;
	}

	// Sépare "key: value" au premier ':' top-level (hors guillemets) suivi
	// d'un espace ou de la fin de ligne. Retourne NONE si la ligne n'a pas
	// cette forme (ex: un item de séquence scalaire brut).
	[[nodiscard]] static Option<std::pair<String, String>> SplitKeyValue(const String &text) {
		bool inSingle = false, inDouble = false;
		for (size_t i = 0; i < text.GetSize(); ++i) {
			char c = text[i];
			if (c == '\'' && !inDouble)
				inSingle = !inSingle;
			else if (c == '"' && !inSingle)
				inDouble = !inDouble;
			else if (c == ':' && !inSingle && !inDouble && (i + 1 == text.GetSize() || text[i + 1] == ' ')) {
				return Some(std::make_pair(text.Substr(0, i).Trim(), text.Substr(i + 1).Trim()));
			}
		}
		return NONE;
	}

	/// Retire un commentaire de fin de ligne en respectant les guillemets :
	/// le `#` de `"a # b"` fait partie de la valeur, pas d'un commentaire.
	[[nodiscard]] static String StripTrailingComment(const String &text) {
		bool inSingle = false, inDouble = false;
		for (size_t i = 0; i < text.GetSize(); ++i) {
			char c = text[i];
			if (c == '\'' && !inDouble)
				inSingle = !inSingle;
			else if (c == '"' && !inSingle)
				inDouble = !inDouble;
			else if (c == '#' && !inSingle && !inDouble && (i == 0 || text[i - 1] == ' ' || text[i - 1] == '\t'))
				return text.Substr(0, i).TrimRight();
		}
		return text;
	}

	[[nodiscard]] static NodePtr ParseScalar(const String &raw) {
		// Le commentaire est retiré AVANT l'examen des guillemets : sinon un
		// scalaire cité SUIVI d'un commentaire (`"Hello: World"  # note`) ne
		// se termine plus par un guillemet, la branche de dé-citation est
		// sautée, et la valeur garde ses guillemets (bug réel, attrapé par
		// tests/data_smoke_test_5.cpp::Yaml::QuotedStringsAndComments).
		// L'ancien découpage sur `" #"` mordait de surcroît à l'intérieur
		// d'une chaîne citée contenant cette séquence.
		String s = StripTrailingComment(raw).Trim();
		if (s.GetSize() >= 2 && s.Front() == '"' && s.Back() == '"')
			return Node::MakeString(s.Substr(1, s.GetSize() - 2));
		if (s.GetSize() >= 2 && s.Front() == '\'' && s.Back() == '\'')
			return Node::MakeString(s.Substr(1, s.GetSize() - 2));

		if (s.IsEmpty() || s == "~" || s == "null" || s == "Null" || s == "NULL")
			return Node::MakeNone();
		return scalar::ParseNode(s.View());
	}

	// Consomme les champs d'un mapping (éventuellement débuté par un premier
	// champ déjà extrait de la ligne du tiret, cf. forme compacte "- key: value").
	static void ContinueMapping(const std::vector<PLine> &lines, size_t &idx, NodePtr obj, int fieldIndent) {
		while (idx < lines.size() && lines[idx].indent == fieldIndent && !lines[idx].isListItem) {
			auto kv = SplitKeyValue(lines[idx].text);
			if (!kv)
				break;
			++idx;
			if (kv->second.IsEmpty()) {
				if (idx < lines.size() && lines[idx].indent > fieldIndent)
					obj->Set(kv->first, ParseBlock(lines, idx, lines[idx].indent));
				else
					obj->Set(kv->first, Node::MakeNone());
			} else {
				obj->Set(kv->first, ParseScalar(kv->second));
			}
		}
	}

	[[nodiscard]] static NodePtr ParseBlock(const std::vector<PLine> &lines, size_t &idx, int indent) {
		if (idx >= lines.size() || lines[idx].indent < indent)
			return Node::MakeNone();

		if (lines[idx].isListItem) {
			auto arr = Node::MakeArray();
			while (idx < lines.size() && lines[idx].indent >= indent && lines[idx].isListItem) {
				int itemIndent = lines[idx].indent;
				String text = lines[idx].text;

				if (text.IsEmpty()) {
					++idx;
					arr->Push(ParseBlock(lines, idx, itemIndent));
					continue;
				}
				auto kv = SplitKeyValue(text);
				if (kv) {
					auto obj = Node::MakeObject();
					++idx;
					if (kv->second.IsEmpty()) {
						if (idx < lines.size() && lines[idx].indent > itemIndent)
							obj->Set(kv->first, ParseBlock(lines, idx, lines[idx].indent));
						else
							obj->Set(kv->first, Node::MakeNone());
					} else {
						obj->Set(kv->first, ParseScalar(kv->second));
					}
					ContinueMapping(lines, idx, obj, itemIndent);
					arr->Push(obj);
				} else {
					arr->Push(ParseScalar(text));
					++idx;
				}
			}
			return arr;
		}

		auto obj = Node::MakeObject();
		ContinueMapping(lines, idx, obj, indent);
		return obj;
	}

	// ── Encodage ─────────────────────────────────────────────────────────────

	[[nodiscard]] static String IndentStr(int n) { return String(size_t(n) * 2, ' '); }

	[[nodiscard]] static bool NeedsQuoting(const String &s) {
		if (s.IsEmpty())
			return true;
		if (scalar::ParseBool(s.View()) || scalar::ParseInt(s.View()) || scalar::ParseFloat(s.View()))
			return true;
		if (s == "~" || s == "null" || s == "Null" || s == "NULL")
			return true;
		if (s.Contains(':') || s.Contains('#'))
			return true;
		if (s.Front() == ' ' || s.Back() == ' ')
			return true;
		char c0 = s.Front();
		if (c0 == '-' || c0 == '[' || c0 == '{' || c0 == '&' || c0 == '*' || c0 == '"' || c0 == '\'' || c0 == '|' ||
			c0 == '>')
			return true;
		return false;
	}

	[[nodiscard]] static String EncodeScalar(const NodePtr &node) {
		if (!node || node->IsNone())
			return "null";
		switch (node->type) {
		case NodeType::BOOL:
			return node->boolValue ? "true" : "false";
		case NodeType::INT:
			return String::From(node->intValue);
		case NodeType::FLOAT:
			return String::FromStream(node->floatValue);
		case NodeType::STRING:
			return NeedsQuoting(node->stringValue) ? (String("\"") + node->stringValue + "\"") : node->stringValue;
		default:
			return "";
		}
	}

	static void EncodeMapping(String &out, const NodePtr &obj, int indent) {
		for (auto &key : obj->Keys()) {
			auto val = obj->Get(key);
			if (val->IsObject()) {
				out.Append(IndentStr(indent));
				out.Append(key);
				out.Append(":\n");
				EncodeMapping(out, val, indent + 1);
			} else if (val->IsArray()) {
				out.Append(IndentStr(indent));
				out.Append(key);
				out.Append(":\n");
				EncodeSequence(out, val, indent);
			} else {
				out.Append(IndentStr(indent));
				out.Append(key);
				out.Append(": ");
				out.Append(EncodeScalar(val));
				out.Append('\n');
			}
		}
	}

	static void EncodeSequence(String &out, const NodePtr &arr, int indent) {
		for (size_t i = 0; i < arr->GetSize(); ++i) {
			auto item = arr->At(i);
			if (item->IsObject()) {
				out.Append(IndentStr(indent));
				out.Append("-\n");
				EncodeMapping(out, item, indent + 1);
			} else if (item->IsArray()) {
				out.Append(IndentStr(indent));
				out.Append("-\n");
				EncodeSequence(out, item, indent + 1);
			} else {
				out.Append(IndentStr(indent));
				out.Append("- ");
				out.Append(EncodeScalar(item));
				out.Append('\n');
			}
		}
	}
};

DATA_REGISTER_FORMAT("yaml", YamlDocument, ".yaml", ".yml")

} // namespace data