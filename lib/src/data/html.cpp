// Définitions de data/html.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/html.hpp"

namespace data {

// ── HtmlDocument ─────────────────────────────────────────────────────────────

String HtmlDocument::EncodeStr() const {
    if (!GetRoot())
        return "";
    String out;

    // Écriture du Doctype si présent
    if (GetRoot()->Has("@doctype")) {
        out.Append("<!DOCTYPE ");
        out.Append(scalar::ToString(GetRoot()->Get("@doctype")));
        out.Append(">\n");
    }

    for (auto &key : GetRoot()->Keys()) {
        if (key == "@doctype") continue;
        EncodeNode(out, key, GetRoot()->Get(key), 0);
    }
    return out;
}

Option<ParseError> HtmlDocument::DecodeImpl(const String &content) {
    auto root = Node::MakeObject();
    Reader r(content);

    while (!r.Eof()) {
        r.SkipWs();
        if (r.Eof()) break;

        if (r.PeekIs('<')) {
            r.Get();
            if (r.PeekIs('!')) {
                // Doctype ou Commentaire
                if (r.PeekIs({'D', 'd'})) { // DOCTYPE
                    auto docTypeVal = ReadDoctype(r);
                    root->Set("@doctype", Node::MakeString(docTypeVal));
                } else {
                    auto err = SkipComment(r);
                    if (err.IsSome()) return err;
                }
                continue;
            }
            if (r.PeekIs('/')) { // Balise fermante orpheline, on l'ignore
                SkipUntilGt(r);
                continue;
            }
            r.Putback();
            String tag;
            auto elemRes = ParseElement(r, tag, "");
            if (elemRes.IsError()) return Some(elemRes.Error());
            MergeChild(root, tag, elemRes.Unwrap());
        } else {
            String ignored = ReadText(r); // Texte hors racine
            (void)ignored;
        }
    }
    SetRoot(root);
    return NONE;
}

String HtmlDocument::ToLower(const String& s) {
    String res = s;
    for (size_t i = 0; i < res.GetSize(); ++i) res[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(res[i])));
    return res;
}

bool HtmlDocument::IsVoidElement(const String& tag) {
    String t = ToLower(tag);
    return t == "area" || t == "base" || t == "br" || t == "col" || t == "embed" ||
           t == "hr" || t == "img" || t == "input" || t == "link" || t == "meta" ||
           t == "param" || t == "source" || t == "track" || t == "wbr";
}

bool HtmlDocument::IsRawTextElement(const String& tag) {
    String t = ToLower(tag);
    return t == "script" || t == "style";
}

bool HtmlDocument::DoesImplicitClose(const String& parentTag, const String& childTag) {
    String p = ToLower(parentTag), c = ToLower(childTag);
    if (p == "p" && (c == "p" || c == "div" || c == "h1" || c == "h2" || c == "h3" || c == "h4" || c == "h5" || c == "h6" || c == "ul" || c == "ol" || c == "table" || c == "hr")) return true;
    if (p == "li" && c == "li") return true;
    if (p == "td" && (c == "td" || c == "th")) return true;
    if (p == "th" && (c == "td" || c == "th")) return true;
    if (p == "tr" && c == "tr") return true;
    if (p == "dt" && (c == "dt" || c == "dd")) return true;
    if (p == "dd" && (c == "dt" || c == "dd")) return true;
    return false;
}

void HtmlDocument::WriteIndent(String &out, int indent) {
    for (int i = 0; i < indent; ++i) out.Append("  ");
}

void HtmlDocument::EncodeAttributes(String &out, const NodePtr &attrs) {
    for (auto &name : attrs->Keys()) {
        out.Append(' ');
        out.Append(name);
        auto val = scalar::ToString(attrs->Get(name));
        // Attribut booléen : <input disabled>
        if (val.IsEmpty()) {
            continue; 
        }
        out.Append("=\"");
        out.Append(val);
        out.Append('"');
    }
}

void HtmlDocument::EncodeNode(String &out, const String &tag, const NodePtr &node, int indent) {
    if (!node) return;
    if (node->IsArray()) {
        for (size_t i = 0; i < node->GetSize(); ++i) EncodeNode(out, tag, node->At(i), indent);
        return;
    }
    if (!node->IsObject()) {
        WriteIndent(out, indent);
        out.Append('<'); out.Append(tag); out.Append('>');
        out.Append(scalar::ToString(node));
        out.Append("</"); out.Append(tag); out.Append(">\n");
        return;
    }

    WriteIndent(out, indent);
    out.Append('<'); out.Append(tag);
    if (node->Has(ATTR_KEY)) EncodeAttributes(out, node->Get(ATTR_KEY));

    if (IsVoidElement(tag)) {
        out.Append(">\n");
        return;
    }

    auto &keys = node->Keys();
    bool hasContent = false;
    for (auto &k : keys) if (k != ATTR_KEY) { hasContent = true; break; }

    if (!hasContent) {
        out.Append("></"); out.Append(tag); out.Append(">\n");
        return;
    }

    out.Append('>');
    bool onlyText = (keys.size() == (node->Has(ATTR_KEY) ? 2u : 1u)) && node->Has(TEXT_KEY);
    if (!onlyText) out.Append('\n');

    for (auto &k : keys) {
        if (k == ATTR_KEY) continue;
        if (k == TEXT_KEY) {
            out.Append(scalar::ToString(node->Get(TEXT_KEY)));
            if (!onlyText) out.Append('\n');
        } else {
            EncodeNode(out, k, node->Get(k), indent + 1);
        }
    }

    if (!onlyText) WriteIndent(out, indent);
    out.Append("</"); out.Append(tag); out.Append(">\n");
}

Option<ParseError> HtmlDocument::Fail(const Reader &r, const String &msg) {
    return Some(ParseError(String("HTML: ") + msg, r.Line()));
}

Result<NodePtr, ParseError> HtmlDocument::FailR(const Reader &r, const String &msg) {
    return Result<NodePtr, ParseError>(Err<ParseError>(ParseError(String("HTML: ") + msg, r.Line())));
}

void HtmlDocument::SkipUntilGt(Reader &r) {
    while (!r.Eof()) { if (r.Get() == Some('>')) break; }
}

Option<ParseError> HtmlDocument::SkipComment(Reader &r) {
    r.Get(); // '!'
    auto a = r.Get(); auto b = r.Get();
    if (!a || !b || *a != '-' || *b != '-') return Fail(r, "commentaire malformé");
    int dashes = 0;
    while (!r.Eof()) {
        char c = *r.Get();
        if (c == '-') ++dashes;
        else if (c == '>' && dashes >= 2) return NONE;
        else dashes = 0;
    }
    return Fail(r, "commentaire non terminé");
}

String HtmlDocument::ReadDoctype(Reader &r) {
    // On a lu '<!', on s'attend à DOCTYPE
    String content;
    while (!r.Eof()) {
        auto c = r.Peek();
        if (c == Some('>')) { r.Get(); break; }
        content += *r.Get();
    }
    // Ex: "DOCTYPE html"
    if (content.GetSize() > 8 && ToLower(content.Substr(0, 7)) == "doctype") {
        return content.Substr(8).Trim();
    }
    return content;
}

String HtmlDocument::ReadName(Reader &r) {
    String name;
    while (true) {
        auto oc = r.Peek();
        if (!oc) break;
        char c = *oc;
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.')
            name += *r.Get();
        else break;
    }
    return name;
}

String HtmlDocument::ReadAttrValue(Reader &r) {
    r.SkipWs();
    auto oq = r.Peek();
    if (!oq) return "";

    if (*oq == '"' || *oq == '\'') { // Quoté
        char q = *r.Get();
        String v;
        while (true) {
            auto oc = r.Peek();
            if (!oc || *oc == q) break;
            v += *r.Get();
        }
        if (r.PeekIs(q)) r.Get(); // consomme guillemet fermant
        return v;
    } else { // Non quoté (HTML5 valide)
        String v;
        while (true) {
            auto oc = r.Peek();
            if (!oc || std::isspace(static_cast<unsigned char>(*oc)) || *oc == '>' || *oc == '/') break;
            v += *r.Get();
        }
        return v;
    }
}

String HtmlDocument::ReadText(Reader &r) {
    String t;
    while (true) {
        auto oc = r.Peek();
        if (!oc || *oc == '<') break;
        t += *r.Get();
    }
    return t;
}

String HtmlDocument::ReadRawContent(Reader &r, const String &endTag) {
    String t;
    String closing = "</" + ToLower(endTag);
    while (!r.Eof()) {
        if (r.PeekIs('<')) {
            // Vérifie si on est sur la balise fermante attendue
            size_t remaining = r.Remaining().GetSize();
            if (remaining >= closing.GetSize() + 1) {
                StringView next = r.Remaining().Substr(0, closing.GetSize());
                if (ToLower(String(next)) == closing) {
                    break; // On s'arrête avant la balise fermante
                }
            }
        }
        t += *r.Get();
    }
    return t;
}

Result<NodePtr, ParseError> HtmlDocument::ParseAttributes(Reader &r) {
    auto attrs = Node::MakeObject();
    while (true) {
        r.SkipWs();
        auto oc = r.Peek();
        if (!oc || *oc == '>' || *oc == '/') break;

        String name = ReadName(r);
        if (name.IsEmpty()) break;

        r.SkipWs();
        if (r.PeekIs('=')) {
            r.Get(); // consomme '='
            String val = ReadAttrValue(r);
            attrs->Set(name, Node::MakeString(val));
        } else {
            // Attribut booléen (ex: disabled, checked)
            attrs->Set(name, Node::MakeString("")); // Valeur vide pour signaler un booléen
        }
    }
    return Ok<NodePtr>(attrs);
}

void HtmlDocument::MergeChild(NodePtr parent, const String &key, NodePtr child) {
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

Option<ParseError> HtmlDocument::ParseContent(Reader &r, NodePtr parent, const String &parentTag) {
    while (true) {
        r.SkipWs();
        auto oc = r.Peek();
        if (!oc) return NONE; // Fin de fichier, ferme implicitement

        if (*oc == '<') {
            r.Get();
            if (r.PeekIs('/')) { // Balise fermante explicite
                r.Putback();
                return NONE;
            }
            if (r.PeekIs('!')) {
                auto err = SkipComment(r);
                if (err.IsSome()) return err;
                continue;
            }

            // Vérification de la fermeture implicite
            r.Putback(); // on remet '<'
            String nextTag = ReadNameAt(r); // Lit le nom sans consommer la balise

            if (!parentTag.IsEmpty() && DoesImplicitClose(parentTag, nextTag)) {
                return NONE; // Signale au parent de se fermer
            }

            String childTag;
            auto childRes = ParseElement(r, childTag, parentTag);
            if (childRes.IsError()) return Some(childRes.Error());
            MergeChild(parent, childTag, childRes.Unwrap());
        } else {
            String text = ReadText(r);
            bool blank = true;
            for (char c : text) if (!std::isspace(static_cast<unsigned char>(c))) { blank = false; break; }
            if (!blank) parent->Set(TEXT_KEY, Node::MakeString(text));
        }
    }
}

String HtmlDocument::ReadNameAt(Reader &r) {
    size_t startPos = r.Pos();
    if (!r.PeekIs('<')) return "";
    r.Get(); // '<'
    String name = ReadName(r);
    r.SetPos(startPos); // Rollback
    return name;
}

Result<NodePtr, ParseError> HtmlDocument::ParseElement(Reader &r, String &outTag, const String &parentTag) {
    r.SkipWs();
    auto lt = r.Get();
    if (!lt || *lt != '<') return FailR(r, "'<' attendu");

    outTag = ReadName(r);
    if (outTag.IsEmpty()) return FailR(r, "nom de balise manquant");

    auto elem = Node::MakeObject();
    auto attrsRes = ParseAttributes(r);
    if (attrsRes.IsError()) return Result<NodePtr, ParseError>(Err<ParseError>(attrsRes.Error()));
    auto attrs = attrsRes.Unwrap();
    if (!attrs->Keys().empty()) elem->Set(ATTR_KEY, attrs);

    r.SkipWs();
    auto oc = r.Peek();
    if (!oc) return FailR(r, "balise malformée");

    // Gestion de la fermeture de la balise ouvrante ( /> ou > )
    if (*oc == '/') r.Get(); // Auto-fermant XHTML ignoré

    if (!r.PeekIs('>')) return FailR(r, "'>' attendu");
    r.Get(); // Consomme '>'

    if (IsVoidElement(outTag)) {
        return Ok<NodePtr>(elem); // Pas de contenu pour les void elements
    }

    // Gestion des raw text elements (script, style)
    if (IsRawTextElement(outTag)) {
        String raw = ReadRawContent(r, outTag);
        if (!raw.IsEmpty()) elem->Set(TEXT_KEY, Node::MakeString(raw));
        // On s'attend à la balise fermante
        SkipClosingTag(r, outTag);
        return Ok<NodePtr>(elem);
    }

    // Parsing classique du contenu
    auto err = ParseContent(r, elem, outTag);
    if (err.IsSome()) return Result<NodePtr, ParseError>(Err<ParseError>(err.Value()));

    // Consommer la balise fermante explicite si présente
    SkipClosingTag(r, outTag);

    return Ok<NodePtr>(elem);
}

void HtmlDocument::SkipClosingTag(Reader &r, const String &tag) {
    r.SkipWs();
    if (!r.PeekIs('<')) return;

    size_t startPos = r.Pos();
    r.Get(); // '<'
    if (!r.PeekIs('/')) { r.SetPos(startPos); return; }
    r.Get(); // '/'

    String closing = ReadName(r);
    if (ToLower(closing) != ToLower(tag)) {
        r.SetPos(startPos); // Ce n'est pas la bonne balise fermante, on rollback
        return;
    }

    r.SkipWs();
    if (r.PeekIs('>')) r.Get(); // '>'
}

} // namespace data
