#pragma once
/**
 * data::HtmlDocument — codec HTML/HTM (sous-ensemble HTML5 pratique).
 *
 * Hérite de la logique de XmlDocument mais y apporte les assouplissements
 * indispensables du HTML : balises auto-fermantes (void), attributs booléens
 * et non quotés, fermeture implicite des balises (ex: <p> ouvert, un
 * nouveau <p> le ferme), et contenu brut pour <script>/<style>.
 *
 * Le Doctype est préservé via la clé spéciale "@doctype" à la racine.
 */
#include "document.hpp"

namespace data {

class HtmlDocument final : public Document {
public:
    HtmlDocument() = default;

    [[nodiscard]] String EncodeStr() const override;

protected:
    [[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override;

private:
    static constexpr const char *ATTR_KEY = "@attributes";
    static constexpr const char *TEXT_KEY = "#text";

    // ── Prédicats HTML ────────────────────────────────────────────────────────

    static String ToLower(const String& s);

    static bool IsVoidElement(const String& tag);

    static bool IsRawTextElement(const String& tag);

    // Vrai si l'ouverture de `childTag` ferme implicitement `parentTag`
    static bool DoesImplicitClose(const String& parentTag, const String& childTag);

    // ── Encodage ─────────────────────────────────────────────────────────────

    static void WriteIndent(String &out, int indent);

    static void EncodeAttributes(String &out, const NodePtr &attrs);

    static void EncodeNode(String &out, const String &tag, const NodePtr &node, int indent);

    // ── Décodage ─────────────────────────────────────────────────────────────

    [[nodiscard]] static Option<ParseError> Fail(const Reader &r, const String &msg);
    [[nodiscard]] static Result<NodePtr, ParseError> FailR(const Reader &r, const String &msg);

    static void SkipUntilGt(Reader &r);

    [[nodiscard]] static Option<ParseError> SkipComment(Reader &r);

    [[nodiscard]] static String ReadDoctype(Reader &r);

    [[nodiscard]] static String ReadName(Reader &r);

    [[nodiscard]] static String ReadAttrValue(Reader &r);

    [[nodiscard]] static String ReadText(Reader &r);

    [[nodiscard]] static String ReadRawContent(Reader &r, const String &endTag);

    [[nodiscard]] static Result<NodePtr, ParseError> ParseAttributes(Reader &r);

    static void MergeChild(NodePtr parent, const String &key, NodePtr child);

    [[nodiscard]] static Option<ParseError> ParseContent(Reader &r, NodePtr parent, const String &parentTag);

    // Utilitaire pour lire le nom de la prochaine balise sans avancer le curseur principal
    static String ReadNameAt(Reader &r);

    [[nodiscard]] static Result<NodePtr, ParseError> ParseElement(Reader &r, String &outTag, const String &parentTag);

    static void SkipClosingTag(Reader &r, const String &tag);
};

DATA_REGISTER_FORMAT("html", HtmlDocument, ".html", ".htm")

} // namespace data