// Définitions de data/json.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/json.hpp"

namespace data {

// ── JsonDocument ─────────────────────────────────────────────────────────────

String JsonDocument::EncodeStr() const {
	if (!GetRoot())
		return "";
	String out;
	EncodeNode(out, GetRoot(), 0);
	return out;
}

Option<ParseError> JsonDocument::DecodeImpl(const String &content) {
	Reader r(content);
	auto res = ParseNode(r);
	if (res.IsError())
		return Some(res.Error());
	SetRoot(res.Unwrap());
	return NONE;
}

void JsonDocument::WriteIndent(String &out, int indent) {
	for (int i = 0; i < indent; ++i)
		out.Append("  ");
}

void JsonDocument::EncodeString(String &out, const String &s) {
	out.Append('"');
	for (unsigned char c : s) {
		switch (c) {
			case '"':
				out.Append("\\\"");
				break;
			case '\\':
				out.Append("\\\\");
				break;
			case '\b':
				out.Append("\\b");
				break;
			case '\f':
				out.Append("\\f");
				break;
			case '\n':
				out.Append("\\n");
				break;
			case '\r':
				out.Append("\\r");
				break;
			case '\t':
				out.Append("\\t");
				break;
			default:
				if (c < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", int(c));
					out.Append(buf);
				} else {
					out.Append(static_cast<char>(c));
				}
		}
	}
	out.Append('"');
}

void JsonDocument::EncodeNode(String &out, const NodePtr &node, int indent) {
	if (!node || node->IsNone()) {
		out.Append("null");
		return;
	}
	switch (node->type) {
		case NodeType::OBJECT: {
			out.Append('{');
			auto &keys = node->Keys();
			if (!keys.empty())
				out.Append('\n');
			for (size_t i = 0; i < keys.size(); ++i) {
				WriteIndent(out, indent + 1);
				EncodeString(out, keys[i]);
				out.Append(": ");
				EncodeNode(out, node->Get(keys[i]), indent + 1);
				if (i + 1 < keys.size())
					out.Append(',');
				out.Append('\n');
			}
			if (!keys.empty())
				WriteIndent(out, indent);
			out.Append('}');
			return;
		}
		case NodeType::ARRAY: {
			out.Append('[');
			size_t n = node->GetSize();
			if (n > 0)
				out.Append('\n');
			for (size_t i = 0; i < n; ++i) {
				WriteIndent(out, indent + 1);
				EncodeNode(out, node->At(i), indent + 1);
				if (i + 1 < n)
					out.Append(',');
				out.Append('\n');
			}
			if (n > 0)
				WriteIndent(out, indent);
			out.Append(']');
			return;
		}
		case NodeType::STRING:
			EncodeString(out, node->stringValue);
			return;
		case NodeType::BOOL:
			out.Append(node->boolValue ? "true" : "false");
			return;
		case NodeType::INT:
			out.Append(String::From(node->intValue));
			return;
		case NodeType::FLOAT: {
			// Représentation la PLUS COURTE qui se relit à l'identique
			// (std::to_chars sans précision) : l'aller-retour reste exact,
			// comme avec max_digits10, mais 1.65 s'écrit `1.65` et non
			// `1.6499999999999999` — un projet reste lisible et ses
			// différences de version, courtes.
			char buffer[64];
			const auto written = std::to_chars(buffer, buffer + sizeof(buffer), node->floatValue);
			String text = written.ec == std::errc{}
							  ? String(StringView(buffer, size_t(written.ptr - buffer)))
							  : String::FromStream(std::setprecision(std::numeric_limits<double>::max_digits10),
												   node->floatValue);
			// ... mais un FLOAT de valeur entière sort alors comme `5`,
			// que le DÉCODEUR relit ensuite en nœud INT : le type se
			// perd silencieusement à l'aller-retour, et tout lecteur qui
			// ne consulte que `floatValue` récupère 0 (bug réellement
			// rencontré sur des coordonnées de scène entières, cf.
			// memory/project_game_editor.md, contourné à l'époque côté
			// LECTURE seulement). Forcer le point décimal — licite en
			// JSON, sans effet sur la valeur — rend le type stable à
			// l'aller-retour. `inf`/`nan` (déjà hors JSON strict) et la
			// notation exponentielle sont laissés intacts.
			bool needsDecimalPoint = true;
			for (size_t i = 0; i < text.GetSize(); ++i) {
				char c = text.CharAt(i);
				if (c == '.' || c == 'e' || c == 'E' || c == 'n' || c == 'i') {
					needsDecimalPoint = false;
					break;
				}
			}
			if (needsDecimalPoint)
				text.Concat(".0");
			out.Append(text);
			return;
		}
		default:
			out.Append("null");
			return;
	}
}

Result<NodePtr, ParseError> JsonDocument::FailNode(const Reader &r, const String &msg) {
	return Result<NodePtr, ParseError>(Err<ParseError>(ParseError(String("JSON: ") + msg, r.Line())));
}

Result<String, ParseError> JsonDocument::FailStr(const Reader &r, const String &msg) {
	return Result<String, ParseError>(Err<ParseError>(ParseError(String("JSON: ") + msg, r.Line())));
}

Result<String, ParseError> JsonDocument::ParseStringLiteral(Reader &r) {
	r.Get(); // consomme le '"' ouvrant
	String value;
	while (true) {
		auto oc = r.Peek();
		if (!oc)
			return FailStr(r, "chaîne non terminée");
		if (*oc == '"')
			break;
		char c = *r.Get();
		if (c == '\\') {
			auto on = r.Get();
			if (!on)
				return FailStr(r, "échappement non terminé");
			char nx = *on;
			switch (nx) {
				case '"':
					value += '"';
					break;
				case '\\':
					value += '\\';
					break;
				case '/':
					value += '/';
					break;
				case 'b':
					value += '\b';
					break;
				case 'f':
					value += '\f';
					break;
				case 'n':
					value += '\n';
					break;
				case 'r':
					value += '\r';
					break;
				case 't':
					value += '\t';
					break;
				case 'u':
					for (int i = 0; i < 4 && !r.Eof(); ++i)
						r.Get(); // \uXXXX non décodé en UTF-8 ici
					break;
				default:
					value += nx;
			}
		} else {
			value += c;
		}
	}
	r.Get(); // '"' fermant
	return Ok<String>(value);
}

Result<NodePtr, ParseError> JsonDocument::ParseLiteral(Reader &r) {
	String token;
	while (true) {
		auto oc = r.Peek();
		if (!oc)
			break;
		char c = *oc;
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.')
			token += *r.Get();
		else
			break;
	}
	if (token.IsEmpty())
		return FailNode(r, "valeur attendue");
	if (token == "true")
		return Ok<NodePtr>(Node::MakeBool(true));
	if (token == "false")
		return Ok<NodePtr>(Node::MakeBool(false));
	if (token == "null")
		return Ok<NodePtr>(Node::MakeNone());
	if (auto i = token.TryParseInt())
		return Ok<NodePtr>(Node::MakeInt(*i));
	if (auto f = token.TryParseDouble())
		return Ok<NodePtr>(Node::MakeFloat(*f));
	return FailNode(r, "littéral invalide: " + token);
}

Result<NodePtr, ParseError> JsonDocument::ParseArray(Reader &r) {
	r.Get(); // '['
	auto arr = Node::MakeArray();
	r.SkipWs();
	if (r.PeekIs(']')) {
		r.Get();
		return Ok<NodePtr>(arr);
	}
	while (true) {
		auto item = ParseNode(r);
		if (item.IsError())
			return item;
		arr->Push(item.Unwrap());
		r.SkipWs();
		auto oc = r.Get();
		if (!oc)
			return FailNode(r, "',' ou ']' attendu dans un tableau");
		if (*oc == ']')
			break;
		if (*oc != ',')
			return FailNode(r, "',' ou ']' attendu dans un tableau");
		r.SkipWs();
	}
	return Ok<NodePtr>(arr);
}

Result<NodePtr, ParseError> JsonDocument::ParseObject(Reader &r) {
	r.Get(); // '{'
	auto obj = Node::MakeObject();
	r.SkipWs();
	if (r.PeekIs('}')) {
		r.Get();
		return Ok<NodePtr>(obj);
	}
	while (true) {
		r.SkipWs();
		if (!r.PeekIs('"'))
			return FailNode(r, "clé de type chaîne attendue");
		auto keyRes = ParseStringLiteral(r);
		if (keyRes.IsError())
			return Result<NodePtr, ParseError>(Err<ParseError>(keyRes.Error()));
		String key = keyRes.Unwrap();
		r.SkipWs();
		auto colon = r.Get();
		if (!colon || *colon != ':')
			return FailNode(r, "':' attendu après la clé");
		r.SkipWs();
		auto val = ParseNode(r);
		if (val.IsError())
			return val;
		obj->Set(key, val.Unwrap());
		r.SkipWs();
		auto oc = r.Get();
		if (!oc)
			return FailNode(r, "',' ou '}' attendu dans un objet");
		if (*oc == '}')
			break;
		if (*oc != ',')
			return FailNode(r, "',' ou '}' attendu dans un objet");
	}
	return Ok<NodePtr>(obj);
}

Result<NodePtr, ParseError> JsonDocument::ParseNode(Reader &r) {
	r.SkipWs();
	auto oc = r.Peek();
	if (!oc)
		return FailNode(r, "valeur attendue (fin de fichier)");
	switch (*oc) {
		case '{':
			return ParseObject(r);
		case '[':
			return ParseArray(r);
		case '"': {
			auto sres = ParseStringLiteral(r);
			if (sres.IsError())
				return Result<NodePtr, ParseError>(Err<ParseError>(sres.Error()));
			return Ok<NodePtr>(Node::MakeString(sres.Unwrap()));
		}
		default:
			return ParseLiteral(r);
	}
}

} // namespace data
