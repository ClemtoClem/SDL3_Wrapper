#pragma once
/**
 * data::XmlDocument — codec XML 1.0 (sous-ensemble : éléments, attributs,
 * texte, commentaires — pas de DTD/CDATA/namespaces/entités custom).
 *
 * Convention de correspondance XML <-> arbre commun (identique à
 * SDL3pp_dataScripts.h) : les attributs d'un élément sont regroupés sous la
 * clé "@attributes" (un Object), le texte sous "#text". Deux enfants avec le
 * même nom de balise sont fusionnés en un Array — c'est l'écart XML/arbre
 * habituel : cela préserve le regroupement par nom de balise, mais PAS
 * l'ordre d'entrelacement entre balises de noms différents
 * (`<a/><b/><a/>` redevient `{a:[...,...], b:[...]}`, l'ordre relatif de
 * `b` par rapport aux deux `a` est perdu). Un lecteur XML "événementiel"
 * séparé serait nécessaire pour préserver un ordre de document strict ;
 * hors de portée de ce module dont le but est l'interopérabilité entre
 * formats via un arbre commun, pas la fidélité XML à 100%.
 *
 * Aucune exception : toute erreur de syntaxe est propagée via
 * Option<ParseError>/Result<NodePtr,ParseError>.
 */
#include "document.hpp"

namespace data {

class XmlDocument final : public Document {
public:
	XmlDocument() = default;

	[[nodiscard]] String EncodeStr() const override {
		if (!GetRoot())
			return "";
		String out;
		out.Append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
		for (auto &key : GetRoot()->Keys())
			EncodeNode(out, key, GetRoot()->Get(key), 0);
		return out;
	}

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override {
		auto root = Node::MakeObject();
		Reader r(content);

		r.SkipWs();
		if (r.PeekIs('<')) {
			r.Get();
			if (r.PeekIs('?')) { // <?xml ... ?>
				r.Get();
				while (!r.Eof()) {
					auto c = r.Get();
					if (c && *c == '?' && r.PeekIs('>'))
						break;
				}
				r.Get(); // '>'
			} else {
				r.Putback();
			}
		}
		while (!r.Eof()) {
			r.SkipWs();
			if (r.Eof())
				break;
			if (r.PeekIs('<')) {
				r.Get();
				if (r.PeekIs('!')) {
					auto err = SkipComment(r);
					if (err.IsSome())
						return err;
					continue;
				}
				r.Putback();
				String tag;
				auto elemRes = ParseElement(r, tag);
				if (elemRes.IsError())
					return Some(elemRes.Error());
				MergeChild(root, tag, elemRes.Unwrap());
			} else {
				String ignored = ReadText(r); // texte hors élément racine — ignoré
				(void)ignored;
			}
		}
		SetRoot(root);
		return NONE;
	}

private:
	static constexpr const char *ATTR_KEY = "@attributes";
	static constexpr const char *TEXT_KEY = "#text";

	// ── Encodage ─────────────────────────────────────────────────────────────

	static void WriteIndent(String &out, int indent) {
		for (int i = 0; i < indent; ++i)
			out.Append("  ");
	}

	static void EncodeAttributes(String &out, const NodePtr &attrs) {
		for (auto &name : attrs->Keys()) {
			out.Append(' ');
			out.Append(name);
			out.Append("=\"");
			out.Append(scalar::ToString(attrs->Get(name)));
			out.Append('"');
		}
	}

	static void EncodeNode(String &out, const String &tag, const NodePtr &node, int indent) {
		if (!node)
			return;
		if (node->IsArray()) {
			for (size_t i = 0; i < node->GetSize(); ++i)
				EncodeNode(out, tag, node->At(i), indent);
			return;
		}
		if (!node->IsObject()) {
			WriteIndent(out, indent);
			out.Append('<');
			out.Append(tag);
			out.Append('>');
			out.Append(scalar::ToString(node));
			out.Append("</");
			out.Append(tag);
			out.Append(">\n");
			return;
		}
		WriteIndent(out, indent);
		out.Append('<');
		out.Append(tag);
		if (node->Has(ATTR_KEY))
			EncodeAttributes(out, node->Get(ATTR_KEY));

		auto &keys = node->Keys();
		bool hasContent = false;
		for (auto &k : keys)
			if (k != ATTR_KEY) {
				hasContent = true;
				break;
			}
		if (!hasContent) {
			out.Append(" />\n");
			return;
		}

		out.Append('>');
		bool onlyText = (keys.size() == (node->Has(ATTR_KEY) ? 2u : 1u)) && node->Has(TEXT_KEY);
		if (!onlyText)
			out.Append('\n');
		for (auto &k : keys) {
			if (k == ATTR_KEY)
				continue;
			if (k == TEXT_KEY) {
				out.Append(scalar::ToString(node->Get(TEXT_KEY)));
				if (!onlyText)
					out.Append('\n');
			} else {
				EncodeNode(out, k, node->Get(k), indent + 1);
			}
		}
		if (!onlyText)
			WriteIndent(out, indent);
		out.Append("</");
		out.Append(tag);
		out.Append(">\n");
	}

	// ── Décodage ─────────────────────────────────────────────────────────────

	[[nodiscard]] static Option<ParseError> Fail(const Reader &r, const String &msg) {
		return Some(ParseError(String("XML: ") + msg, r.Line()));
	}
	[[nodiscard]] static Result<NodePtr, ParseError> FailR(const Reader &r, const String &msg) {
		return Result<NodePtr, ParseError>(Err<ParseError>(ParseError(String("XML: ") + msg, r.Line())));
	}
	[[nodiscard]] static Result<String, ParseError> FailS(const Reader &r, const String &msg) {
		return Result<String, ParseError>(Err<ParseError>(ParseError(String("XML: ") + msg, r.Line())));
	}

	[[nodiscard]] static Option<ParseError> SkipComment(Reader &r) {
		r.Get(); // '!'
		auto a = r.Get();
		auto b = r.Get();
		if (!a || !b || *a != '-' || *b != '-')
			return Fail(r, "commentaire XML malformé (<!-- attendu)");
		int dashes = 0;
		while (!r.Eof()) {
			char c = *r.Get();
			if (c == '-')
				++dashes;
			else if (c == '>' && dashes >= 2)
				return NONE;
			else
				dashes = 0;
		}
		return Fail(r, "commentaire XML non terminé");
	}

	[[nodiscard]] static String ReadName(Reader &r) {
		String name;
		while (true) {
			auto oc = r.Peek();
			if (!oc)
				break;
			char c = *oc;
			if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.')
				name += *r.Get();
			else
				break;
		}
		return name;
	}

	[[nodiscard]] static Result<String, ParseError> ReadAttrValue(Reader &r) {
		r.SkipWs();
		auto oq = r.Get();
		if (!oq || (*oq != '"' && *oq != '\''))
			return FailS(r, "guillemet attendu pour la valeur d'attribut");
		char q = *oq;
		String v;
		while (true) {
			auto oc = r.Peek();
			if (!oc)
				return FailS(r, "valeur d'attribut non terminée");
			if (*oc == q)
				break;
			v += *r.Get();
		}
		r.Get(); // guillemet fermant
		return Ok<String>(v);
	}

	[[nodiscard]] static String ReadText(Reader &r) {
		String t;
		while (true) {
			auto oc = r.Peek();
			if (!oc || *oc == '<')
				break;
			t += *r.Get();
		}
		return t;
	}

	[[nodiscard]] static Result<NodePtr, ParseError> ParseAttributes(Reader &r) {
		auto attrs = Node::MakeObject();
		while (true) {
			r.SkipWs();
			auto oc = r.Peek();
			if (!oc || *oc == '>' || *oc == '/')
				break;
			String name = ReadName(r);
			if (name.IsEmpty())
				break;
			r.SkipWs();
			auto eq = r.Get();
			if (!eq || *eq != '=')
				return FailR(r, "'=' attendu après l'attribut: " + name);
			auto valRes = ReadAttrValue(r);
			if (valRes.IsError())
				return Result<NodePtr, ParseError>(Err<ParseError>(valRes.Error()));
			attrs->Set(name, Node::MakeString(valRes.Unwrap()));
		}
		return Ok<NodePtr>(attrs);
	}

	static void MergeChild(NodePtr parent, const String &key, NodePtr child) {
		if (!parent->Has(key)) {
			parent->Set(key, child);
			return;
		}
		auto existing = parent->Get(key);
		if (existing->IsArray()) {
			existing->Push(child);
			return;
		}
		auto arr = Node::MakeArray();
		arr->Push(existing);
		arr->Push(child);
		parent->Set(key, arr);
	}

	[[nodiscard]] static Option<ParseError> ParseContent(Reader &r, NodePtr parent) {
		while (true) {
			r.SkipWs();
			auto oc = r.Peek();
			if (!oc)
				return Fail(r, "EOF inattendu (balise fermante manquante)");
			if (*oc == '<') {
				r.Get();
				if (r.PeekIs('/')) {
					r.Putback();
					return NONE;
				}
				if (r.PeekIs('!')) {
					auto err = SkipComment(r);
					if (err.IsSome())
						return err;
					continue;
				}
				r.Putback();
				String childTag;
				auto childRes = ParseElement(r, childTag);
				if (childRes.IsError())
					return Some(childRes.Error());
				MergeChild(parent, childTag, childRes.Unwrap());
			} else {
				String text = ReadText(r);
				bool blank = true;
				for (char c : text)
					if (!std::isspace(static_cast<unsigned char>(c))) {
						blank = false;
						break;
					}
				if (!blank)
					parent->Set(TEXT_KEY, Node::MakeString(text));
			}
		}
	}

	[[nodiscard]] static Result<NodePtr, ParseError> ParseElement(Reader &r, String &outTag) {
		r.SkipWs();
		auto lt = r.Get();
		if (!lt || *lt != '<')
			return FailR(r, "'<' attendu");
		outTag = ReadName(r);
		if (outTag.IsEmpty())
			return FailR(r, "nom de balise manquant");

		auto elem = Node::MakeObject();
		auto attrsRes = ParseAttributes(r);
		if (attrsRes.IsError())
			return Result<NodePtr, ParseError>(Err<ParseError>(attrsRes.Error()));
		auto attrs = attrsRes.Unwrap();
		if (!attrs->Keys().empty())
			elem->Set(ATTR_KEY, attrs);

		r.SkipWs();
		auto oc = r.Peek();
		if (!oc)
			return FailR(r, "balise malformée après les attributs");
		if (*oc == '/') {
			r.Get();
			auto gt = r.Get();
			if (!gt || *gt != '>')
				return FailR(r, "'>' attendu après '/'");
			return Ok<NodePtr>(elem);
		}
		if (*oc == '>') {
			r.Get();
			auto err = ParseContent(r, elem);
			if (err.IsSome())
				return Result<NodePtr, ParseError>(Err<ParseError>(err.Value()));
			r.SkipWs();
			auto lt2 = r.Get();
			if (!lt2 || *lt2 != '<')
				return FailR(r, "'<' attendu (balise fermante)");
			auto sl = r.Get();
			if (!sl || *sl != '/')
				return FailR(r, "'/' attendu (balise fermante)");
			String closing = ReadName(r);
			if (closing != outTag)
				return FailR(r, "balises non appariées: <" + outTag + "> fermée par </" + closing + ">");
			r.SkipWs();
			auto gt2 = r.Get();
			if (!gt2 || *gt2 != '>')
				return FailR(r, "'>' attendu en fin de balise fermante");
			return Ok<NodePtr>(elem);
		}
		return FailR(r, "balise malformée après les attributs");
	}
};

DATA_REGISTER_FORMAT("xml", XmlDocument, ".xml")

} // namespace data