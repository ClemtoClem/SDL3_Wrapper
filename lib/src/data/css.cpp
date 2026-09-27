// Définitions de data/css.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/css.hpp"

namespace data {

// ── CssDocument ──────────────────────────────────────────────────────────────

String CssDocument::EncodeStr() const {
	if (!GetRoot())
		return "";
	String out;
	for (size_t i = 0; i < GetRoot()->GetSize(); ++i)
		EncodeRule(out, GetRoot()->At(i), 0);
	return out;
}

Option<ParseError> CssDocument::DecodeImpl(const String &content) {
	String clean = StripComments(content);
	Reader r(clean);
	auto res = ParseRuleList(r);
	if (res.IsError())
		return Some(res.Error());
	r.SkipWs();
	if (r.PeekIs('}'))
		return Some(ParseError("CSS: '}' inattendu (accolade fermante en trop)", r.Line()));
	SetRoot(res.Unwrap());
	return NONE;
}

void CssDocument::WriteIndent(String &out, int indent) {
	for (int i = 0; i < indent; ++i)
		out.Append("  ");
}

void CssDocument::EncodeDeclarations(String &out, const NodePtr &decls, int indent) {
	if (!decls)
		return;
	for (auto &key : decls->Keys()) {
		WriteIndent(out, indent);
		out.Append(key);
		out.Append(": ");
		out.Append(scalar::ToString(decls->Get(key)));
		out.Append(";\n");
	}
}

void CssDocument::EncodeRule(String &out, const NodePtr &rule, int indent) {
	if (!rule)
		return;
	WriteIndent(out, indent);

	if (rule->Has("selector")) {
		out.Append(rule->Get("selector")->stringValue);
		out.Append(" {\n");
		EncodeDeclarations(out, rule->Get("declarations"), indent + 1);
		WriteIndent(out, indent);
		out.Append("}\n");
		return;
	}

	String header = rule->Get("at")->stringValue;
	if (rule->Has("params")) {
		header.Append(' ');
		header.Append(rule->Get("params")->stringValue);
	}

	if (rule->Has("body")) {
		out.Append(header);
		out.Append(" {\n");
		auto body = rule->Get("body");
		for (size_t i = 0; i < body->GetSize(); ++i)
			EncodeRule(out, body->At(i), indent + 1);
		WriteIndent(out, indent);
		out.Append("}\n");
	} else if (rule->Has("declarations")) {
		out.Append(header);
		out.Append(" {\n");
		EncodeDeclarations(out, rule->Get("declarations"), indent + 1);
		WriteIndent(out, indent);
		out.Append("}\n");
	} else {
		out.Append(header);
		out.Append(";\n");
	}
}

String CssDocument::CollapseWs(const String &s) {
	String out;
	bool inWord = false;
	for (char c : s) {
		if (std::isspace(static_cast<unsigned char>(c))) {
			inWord = false;
		} else {
			if (!inWord && !out.IsEmpty())
				out.Append(' ');
			out.Append(c);
			inWord = true;
		}
	}
	return out;
}

String CssDocument::ReadName(Reader &r) {
	String name;
	while (true) {
		auto oc = r.Peek();
		if (!oc)
			break;
		char c = *oc;
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
			name += *r.Get();
		else
			break;
	}
	return name;
}

String CssDocument::StripComments(const String &content) {
	String out;
	out.Reserve(content.GetSize());
	bool inSingle = false, inDouble = false;
	size_t i = 0, size = content.GetSize();
	while (i < size) {
		if (!inSingle && !inDouble && i + 1 < size && content[i] == '/' && content[i + 1] == '*') {
			auto end = content.Find("*/", i + 2);
			i = (end == String::NPOS) ? size : end + 2;
			continue;
		}
		char c = content[i];
		if (c == '\'' && !inDouble)
			inSingle = !inSingle;
		else if (c == '"' && !inSingle)
			inDouble = !inDouble;
		out.Append(c);
		++i;
	}
	return out;
}

Result<CssDocument::ScanResult, ParseError> CssDocument::ReadUntilAny(Reader &r, const String &terminators) {
	ScanResult res;
	bool inSingle = false, inDouble = false;
	int depth = 0;
	while (true) {
		auto oc = r.Peek();
		if (!oc)
			return Result<ScanResult, ParseError>(
				Err<ParseError>(ParseError("CSS: fin de fichier inattendue", r.Line())));
		char c = *oc;
		if (inSingle || inDouble) {
			res.text += *r.Get();
			if ((inSingle && c == '\'') || (inDouble && c == '"'))
				inSingle = inDouble = false;
			continue;
		}
		if (c == '\'') {
			inSingle = true;
			res.text += *r.Get();
			continue;
		}
		if (c == '"') {
			inDouble = true;
			res.text += *r.Get();
			continue;
		}
		if (c == '(') {
			++depth;
			res.text += *r.Get();
			continue;
		}
		if (c == ')') {
			if (depth > 0)
				--depth;
			res.text += *r.Get();
			continue;
		}
		if (depth == 0 && terminators.Contains(c)) {
			res.terminator = c;
			return Ok<ScanResult>(res);
		}
		res.text += *r.Get();
	}
}

bool CssDocument::IsDeclarationAtRule(const String &name) {
	static const std::vector<String> NAMES = {
		"@font-face", "@page", "@property", "@counter-style", "@viewport", "@font-palette-values",
		"@font-feature-values",
	};
	for (auto &n : NAMES)
		if (n == name)
			return true;
	return false;
}

Result<NodePtr, ParseError> CssDocument::ParseDeclarations(Reader &r) {
	auto obj = Node::MakeObject();
	while (true) {
		r.SkipWs();
		auto oc = r.Peek();
		if (!oc)
			return Result<NodePtr, ParseError>(
				Err<ParseError>(ParseError("CSS: '}' manquant (bloc de déclarations non terminé)", r.Line())));
		if (*oc == '}') {
			r.Get();
			break;
		}
		if (*oc == ';') {
			r.Get();
			continue;
		}

		auto propR = ReadUntilAny(r, ":;}");
		if (propR.IsError())
			return Result<NodePtr, ParseError>(Err<ParseError>(propR.Error()));
		auto propScan = propR.Unwrap();
		if (propScan.terminator != ':')
			return Result<NodePtr, ParseError>(Err<ParseError>(
				ParseError("CSS: ':' attendu après la propriété '" + propScan.text.Trim() + "'", r.Line())));
		r.Get(); // ':'
		String prop = propScan.text.Trim();
		if (prop.IsEmpty())
			return Result<NodePtr, ParseError>(Err<ParseError>(ParseError("CSS: nom de propriété vide", r.Line())));

		auto valR = ReadUntilAny(r, ";}");
		if (valR.IsError())
			return Result<NodePtr, ParseError>(Err<ParseError>(valR.Error()));
		auto valScan = valR.Unwrap();
		obj->Set(prop, scalar::ParseNode(valScan.text.Trim().View()));
		if (valScan.terminator == ';')
			r.Get();
		// sinon '}' : laissé pour la prochaine itération, qui le consommera.
	}
	return Ok<NodePtr>(obj);
}

Result<NodePtr, ParseError> CssDocument::ParseStyleRule(Reader &r) {
	auto sr = ReadUntilAny(r, "{");
	if (sr.IsError())
		return Result<NodePtr, ParseError>(Err<ParseError>(sr.Error()));
	r.Get(); // '{'
	String selector = CollapseWs(sr.Unwrap().text.Trim());
	if (selector.IsEmpty())
		return Result<NodePtr, ParseError>(Err<ParseError>(ParseError("CSS: sélecteur vide", r.Line())));

	auto rule = Node::MakeObject();
	rule->Set("selector", Node::MakeString(selector));
	auto declsRes = ParseDeclarations(r);
	if (declsRes.IsError())
		return declsRes;
	rule->Set("declarations", declsRes.Unwrap());
	return Ok<NodePtr>(rule);
}

Result<NodePtr, ParseError> CssDocument::ParseAtRule(Reader &r) {
	r.Get(); // '@'
	String name = "@" + ReadName(r);
	if (name.GetSize() <= 1)
		return Result<NodePtr, ParseError>(
			Err<ParseError>(ParseError("CSS: nom de règle-@ manquant après '@'", r.Line())));
	r.SkipWs();

	auto pr = ReadUntilAny(r, "{;");
	if (pr.IsError())
		return Result<NodePtr, ParseError>(Err<ParseError>(pr.Error()));
	auto scan = pr.Unwrap();
	String params = CollapseWs(scan.text.Trim());

	auto rule = Node::MakeObject();
	rule->Set("at", Node::MakeString(name));
	if (!params.IsEmpty())
		rule->Set("params", Node::MakeString(params));

	if (scan.terminator == ';') {
		r.Get();
		return Ok<NodePtr>(rule);
	}

	r.Get(); // '{'
	if (IsDeclarationAtRule(name)) {
		auto declsRes = ParseDeclarations(r);
		if (declsRes.IsError())
			return declsRes;
		rule->Set("declarations", declsRes.Unwrap());
	} else {
		auto bodyRes = ParseRuleList(r);
		if (bodyRes.IsError())
			return bodyRes;
		r.SkipWs();
		if (!r.PeekIs('}'))
			return Result<NodePtr, ParseError>(
				Err<ParseError>(ParseError("CSS: '}' attendu pour fermer " + name, r.Line())));
		r.Get();
		rule->Set("body", bodyRes.Unwrap());
	}
	return Ok<NodePtr>(rule);
}

Result<NodePtr, ParseError> CssDocument::ParseRuleList(Reader &r) {
	auto arr = Node::MakeArray();
	while (true) {
		r.SkipWs();
		auto oc = r.Peek();
		if (!oc || *oc == '}')
			break;
		auto ruleRes = (*oc == '@') ? ParseAtRule(r) : ParseStyleRule(r);
		if (ruleRes.IsError())
			return ruleRes;
		arr->Push(ruleRes.Unwrap());
	}
	return Ok<NodePtr>(arr);
}

} // namespace data
