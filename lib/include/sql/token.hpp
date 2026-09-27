#pragma once
/**
 * sql::Tokenizer — hand-written lexer (no regex) for the SQL text subset
 * accepted by sql::ParseStatement (parser.hpp): CREATE TABLE / INSERT INTO /
 * SELECT / DELETE / DROP TABLE, plus WHERE-clause comparison/logical
 * expressions (expression.hpp). Every fallible operation returns
 * Result<T, E> — no exceptions, per this codebase's core:: convention.
 */
#include "../core/core.hpp"

#include <array>
#include <cctype>
#include <vector>

namespace sql {

enum class TokenType {
	KEYWORD,
	IDENTIFIER,
	STRING_LITERAL,
	INT_LITERAL,
	FLOAT_LITERAL,
	OPERATOR,
	PUNCTUATION,
	END,
};

/// `text` holds the literal's raw text for literal tokens (no surrounding
/// quotes for STRING_LITERAL, with `''` escapes already resolved) — parser
/// / expression code converts it via String::TryParseInt/TryParseDouble
/// (mirrors data::document.hpp's scalar::ParseInt/ParseFloat idiom).
struct Token {
	TokenType type = TokenType::END;
	String text;
};

class Tokenizer {
public:
	/// Scans `sql` into a flat token stream terminated by a single
	/// TokenType::END token. Err(message) on an unterminated string
	/// literal or an unrecognized character — never silently drops input.
	[[nodiscard]] static Result<std::vector<Token>, String> Tokenize(StringView sql);

private:
	[[nodiscard]] static bool IsSpace(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

	[[nodiscard]] static bool IsKeywordText(const String &upper) noexcept;

	/// `sql[i]` is the opening quote. Consumes through the matching closing
	/// quote — `''` is an escaped literal quote inside the string, mirroring
	/// data::CsvDocument::ParseRows's doubled-quote escaping for `"..."`
	/// fields (csv.hpp) — and advances `i` past the closing quote.
	[[nodiscard]] static Result<String, String> ScanStringLiteral(StringView sql, size_t &i);

	/// `sql[i]` is a digit, or a '-' immediately followed by a digit.
	/// Consumes digits, an optional single '.', and more digits.
	[[nodiscard]] static Token ScanNumber(StringView sql, size_t &i);

	/// `sql[i]` is an alpha char or '_'. Consumes an identifier-shaped word
	/// and classifies it as KEYWORD (case-insensitive match) or IDENTIFIER,
	/// preserving the original-case text either way.
	[[nodiscard]] static Token ScanWord(StringView sql, size_t &i);

	/// Recognizes the 2-char operators (`<>` `!=` `<=` `>=`) before falling
	/// back to the 1-char ones (`=` `<` `>` `*`), so they aren't mis-split.
	/// Returns NONE (without advancing `i`) if `sql[i]` starts no operator.
	[[nodiscard]] static Option<String> ScanOperator(StringView sql, size_t &i);
};

} // namespace sql
