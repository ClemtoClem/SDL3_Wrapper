#pragma once
/**
 * data::JsonDocument — codec JSON (RFC 8259) sur l'arbre data::Node commun.
 * Aucune exception : le décodage retourne Result<NodePtr,ParseError> en
 * interne, converti en Option<ParseError> par DecodeImpl.
 */
#include <charconv>
#include <cstdio>
#include <iomanip>
#include <limits>

#include "document.hpp"

namespace data {

class JsonDocument final : public Document {
public:
	JsonDocument() = default;

	[[nodiscard]] String EncodeStr() const override;

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override;

private:
	// ── Encodage ─────────────────────────────────────────────────────────────

	static void WriteIndent(String &out, int indent);

	static void EncodeString(String &out, const String &s);

	static void EncodeNode(String &out, const NodePtr &node, int indent);

	// ── Décodage ─────────────────────────────────────────────────────────────

	[[nodiscard]] static Result<NodePtr, ParseError> FailNode(const Reader &r, const String &msg);
	[[nodiscard]] static Result<String, ParseError> FailStr(const Reader &r, const String &msg);

	[[nodiscard]] static Result<String, ParseError> ParseStringLiteral(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseLiteral(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseArray(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseObject(Reader &r);

	[[nodiscard]] static Result<NodePtr, ParseError> ParseNode(Reader &r);
};

DATA_REGISTER_FORMAT("json", JsonDocument, ".json")

} // namespace data