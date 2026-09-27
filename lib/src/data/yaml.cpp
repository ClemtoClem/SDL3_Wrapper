// Définitions de data/yaml.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/yaml.hpp"

namespace data {

// ── YamlDocument ─────────────────────────────────────────────────────────────

String YamlDocument::EncodeStr() const {
	if (!GetRoot())
		return "";
	String out;
	EncodeMapping(out, GetRoot(), 0);
	return out;
}

Option<ParseError> YamlDocument::DecodeImpl(const String &content) {
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

std::vector<YamlDocument::PLine> YamlDocument::Preprocess(const String &content) {
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

Option<std::pair<String, String>> YamlDocument::SplitKeyValue(const String &text) {
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

String YamlDocument::StripTrailingComment(const String &text) {
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

NodePtr YamlDocument::ParseScalar(const String &raw) {
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

void YamlDocument::ContinueMapping(const std::vector<PLine> &lines, size_t &idx, NodePtr obj, int fieldIndent) {
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

NodePtr YamlDocument::ParseBlock(const std::vector<PLine> &lines, size_t &idx, int indent) {
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

bool YamlDocument::NeedsQuoting(const String &s) {
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

String YamlDocument::EncodeScalar(const NodePtr &node) {
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

void YamlDocument::EncodeMapping(String &out, const NodePtr &obj, int indent) {
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

void YamlDocument::EncodeSequence(String &out, const NodePtr &arr, int indent) {
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

} // namespace data
