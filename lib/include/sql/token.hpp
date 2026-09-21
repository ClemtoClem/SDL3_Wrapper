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
	[[nodiscard]] static Result<std::vector<Token>, String> Tokenize(StringView sql) {
		std::vector<Token> tokens;
		size_t i = 0;
		size_t size = sql.GetSize();

		while (i < size) {
			char c = sql[i];

			if (IsSpace(c)) {
				++i;
				continue;
			}

			if (c == '\'') {
				auto lit = ScanStringLiteral(sql, i);
				if (lit.IsError())
					return Err(lit.Error());
				tokens.push_back(Token{TokenType::STRING_LITERAL, lit.Value()});
				continue;
			}

			if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
				(c == '-' && i + 1 < size && std::isdigit(static_cast<unsigned char>(sql[i + 1])) != 0)) {
				tokens.push_back(ScanNumber(sql, i));
				continue;
			}

			if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
				tokens.push_back(ScanWord(sql, i));
				continue;
			}

			auto op = ScanOperator(sql, i);
			if (op.IsSome()) {
				tokens.push_back(Token{TokenType::OPERATOR, *op});
				continue;
			}

			if (c == '(' || c == ')' || c == ',' || c == ';') {
				tokens.push_back(Token{TokenType::PUNCTUATION, String(c)});
				++i;
				continue;
			}

			return Err(String::Format("sql: unrecognized character '%c' at position %zu", c, i));
		}

		tokens.push_back(Token{TokenType::END, String()});
		return Ok(std::move(tokens));
	}

private:
	[[nodiscard]] static bool IsSpace(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

	[[nodiscard]] static bool IsKeywordText(const String &upper) noexcept {
		static const std::array<const char *, 17> KEYWORDS = {"CREATE", "TABLE", "INSERT", "INTO", "VALUES",
																"SELECT", "FROM",  "WHERE",  "DELETE", "DROP",
																"AND",    "OR",    "NOT",    "ORDER", "BY",
																"ASC",    "DESC"};
		for (auto *kw : KEYWORDS)
			if (upper == kw)
				return true;
		return false;
	}

	/// `sql[i]` is the opening quote. Consumes through the matching closing
	/// quote — `''` is an escaped literal quote inside the string, mirroring
	/// data::CsvDocument::ParseRows's doubled-quote escaping for `"..."`
	/// fields (csv.hpp) — and advances `i` past the closing quote.
	[[nodiscard]] static Result<String, String> ScanStringLiteral(StringView sql, size_t &i) {
		size_t size = sql.GetSize();
		++i; // skip opening quote
		String text;
		while (i < size) {
			char c = sql[i];
			if (c == '\'') {
				if (i + 1 < size && sql[i + 1] == '\'') {
					text += '\'';
					i += 2;
					continue;
				}
				++i; // skip closing quote
				return Ok(text);
			}
			text += c;
			++i;
		}
		return Err(String("sql: unterminated string literal"));
	}

	/// `sql[i]` is a digit, or a '-' immediately followed by a digit.
	/// Consumes digits, an optional single '.', and more digits.
	[[nodiscard]] static Token ScanNumber(StringView sql, size_t &i) {
		size_t size = sql.GetSize();
		size_t start = i;
		if (sql[i] == '-')
			++i;
		while (i < size && std::isdigit(static_cast<unsigned char>(sql[i])) != 0)
			++i;
		bool isFloat = false;
		if (i < size && sql[i] == '.') {
			isFloat = true;
			++i;
			while (i < size && std::isdigit(static_cast<unsigned char>(sql[i])) != 0)
				++i;
		}
		String text(sql.GetData() + start, i - start);
		return Token{isFloat ? TokenType::FLOAT_LITERAL : TokenType::INT_LITERAL, text};
	}

	/// `sql[i]` is an alpha char or '_'. Consumes an identifier-shaped word
	/// and classifies it as KEYWORD (case-insensitive match) or IDENTIFIER,
	/// preserving the original-case text either way.
	[[nodiscard]] static Token ScanWord(StringView sql, size_t &i) {
		size_t size = sql.GetSize();
		size_t start = i;
		while (i < size && (std::isalnum(static_cast<unsigned char>(sql[i])) != 0 || sql[i] == '_'))
			++i;
		String text(sql.GetData() + start, i - start);
		String upper = text.ToUpper();
		if (IsKeywordText(upper))
			return Token{TokenType::KEYWORD, text};
		return Token{TokenType::IDENTIFIER, text};
	}

	/// Recognizes the 2-char operators (`<>` `!=` `<=` `>=`) before falling
	/// back to the 1-char ones (`=` `<` `>` `*`), so they aren't mis-split.
	/// Returns NONE (without advancing `i`) if `sql[i]` starts no operator.
	[[nodiscard]] static Option<String> ScanOperator(StringView sql, size_t &i) {
		size_t size = sql.GetSize();
		char c = sql[i];
		if (i + 1 < size) {
			char c2 = sql[i + 1];
			if ((c == '<' && c2 == '>') || (c == '!' && c2 == '=') || (c == '<' && c2 == '=') ||
				(c == '>' && c2 == '=')) {
				String op(sql.GetData() + i, size_t(2));
				i += 2;
				return Some(op);
			}
		}
		if (c == '=' || c == '<' || c == '>' || c == '*') {
			String op(sql.GetData() + i, size_t(1));
			++i;
			return Some(op);
		}
		return NONE;
	}
};

} // namespace sql
