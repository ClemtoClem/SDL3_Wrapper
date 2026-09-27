#pragma once
/**
 * sql::ParseStatement — recursive-descent parser over a Tokenizer::Tokenize
 * token stream, dispatching on the leading keyword to one of 5 statement
 * parsers (CREATE TABLE / INSERT INTO / SELECT / DELETE / DROP TABLE).
 * Every error path returns a specific Err(String) naming what was expected
 * vs. found — mirrors data::ParseError's tone (document.hpp) — never a bare
 * "parse error".
 */
#include "../core/core.hpp"
#include "../data/node.hpp"
#include "expression.hpp"
#include "schema.hpp"
#include "statement.hpp"
#include "token.hpp"

#include <vector>

namespace sql {
namespace detail {

[[nodiscard]] String TokenTextOrEnd(const std::vector<Token> &tokens, size_t pos);

/// Consumes a specific keyword (case-insensitive) at `pos`, or Errs.
[[nodiscard]] Result<size_t, String> ExpectKeyword(const std::vector<Token> &tokens, size_t pos,
															const char *keyword);

/// Consumes a specific punctuation mark at `pos`, or Errs.
[[nodiscard]] Result<size_t, String> ExpectPunctuation(const std::vector<Token> &tokens, size_t pos,
																const char *punct);

/// Consumes an identifier (table/column name) at `pos`, or Errs.
[[nodiscard]] Result<String, String> ExpectIdentifier(const std::vector<Token> &tokens, size_t pos,
															   const char *what);

[[nodiscard]] bool IsPunct(const Token &t, const char *punct) noexcept;

[[nodiscard]] bool IsKeyword(const Token &t, const char *keyword) noexcept;

/// Maps a CREATE TABLE column-type spelling (case-insensitive) to one of
/// the 4 data::NodeType scalars — NONE means "not a recognized type name".
[[nodiscard]] Option<data::NodeType> ParseColumnType(const String &text);

/// Consumes one VALUES-list literal token (string/int/float literal, or the
/// bool-literal identifiers TRUE/FALSE) at `pos`, or Errs.
[[nodiscard]] Result<data::NodePtr, String> ParseValueLiteral(const std::vector<Token> &tokens, size_t pos);

/// Requires the statement to be over at `pos`: an optional trailing ';'
/// then TokenType::End. Anything else (in particular an unsupported
/// trailing ORDER BY) is a clear Err naming the extra input, rather than
/// silently ignoring it.
[[nodiscard]] Result<size_t, String> ExpectStatementEnd(const std::vector<Token> &tokens, size_t pos);

[[nodiscard]] Result<Statement, String> ParseCreateTable(const std::vector<Token> &tokens, size_t pos);

[[nodiscard]] Result<Statement, String> ParseInsert(const std::vector<Token> &tokens, size_t pos);

[[nodiscard]] Result<Statement, String> ParseSelect(const std::vector<Token> &tokens, size_t pos);

[[nodiscard]] Result<Statement, String> ParseDelete(const std::vector<Token> &tokens, size_t pos);

[[nodiscard]] Result<Statement, String> ParseDropTable(const std::vector<Token> &tokens, size_t pos);

} // namespace detail

/// Parses one complete statement from a Tokenizer::Tokenize token stream,
/// dispatching on the leading keyword. Exactly one statement per call — no
/// ';'-separated multi-statement batches (this is a library API, not a
/// REPL).
[[nodiscard]] Result<Statement, String> ParseStatement(const std::vector<Token> &tokens);

} // namespace sql
