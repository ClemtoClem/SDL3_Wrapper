#pragma once
/**
 * sql:: WHERE-clause expressions — shunting-yard parsing into RPN
 * (Expression::rpn) plus a direct left-to-right stack evaluator (Evaluate).
 * This is the one genuinely useful *algorithmic* idea worth porting from
 * the reference project's shunting_yard/rpn modules — a well-known,
 * legitimate technique, distinct from that project's hand-rolled STL
 * substitutes (stack/queue/etc.) which this codebase does NOT port (see
 * sql.hpp's module doc comment).
 *
 * Precedence (tightest to loosest), matching standard SQL: comparison
 * operators (= <> != < <= > >=) bind tighter than NOT, which binds tighter
 * than AND, which binds tighter than OR. Parentheses override.
 */
#include "../core/core.hpp"
#include "../data/node.hpp"
#include "schema.hpp"
#include "token.hpp"

#include <vector>

namespace sql {

/// Shunting-yard output: an already-reordered token sequence in postfix
/// (RPN) form, ready for direct left-to-right stack evaluation — no
/// expression tree needed.
struct Expression {
	std::vector<Token> rpn;
};

namespace detail {

[[nodiscard]] inline bool IsComparisonOperator(const Token &t) noexcept { return t.type == TokenType::OPERATOR; }

[[nodiscard]] bool IsLogicalKeyword(const Token &t, const char *word) noexcept;

/// -1 for a token that is not an operator at all (identifiers/literals/
/// parens use this as a sentinel, never actually compared with it).
[[nodiscard]] int Precedence(const Token &t) noexcept;

[[nodiscard]] inline bool IsOperatorToken(const Token &t) noexcept { return Precedence(t) >= 0; }

[[nodiscard]] inline bool IsRightAssociative(const Token &t) noexcept { return IsLogicalKeyword(t, "NOT"); }

[[nodiscard]] bool IsOpenParen(const Token &t) noexcept;

[[nodiscard]] bool IsCloseParen(const Token &t) noexcept;

[[nodiscard]] bool IsOperandToken(const Token &t) noexcept;

} // namespace detail

/// Shunting-yard parse of a comparison/logical expression starting at
/// `tokens[pos]`. Advances `pos` to just past the last consumed token, so
/// the caller (parser.hpp) knows exactly where the expression ended — e.g.
/// at a trailing ';', a following ORDER BY, or end-of-input.
[[nodiscard]] Result<Expression, String> ParseExpression(const std::vector<Token> &tokens, size_t &pos);

namespace detail {

/// 3-way compare of two scalar values for use by the comparison operators.
/// Numeric (INT/FLOAT, in any combination) compares by value; STRING
/// compares lexicographically; BOOL compares false < true. Any other
/// pairing (e.g. STRING vs INT) is a clear Err, never a silent false.
[[nodiscard]] Result<int, String> Compare3Way(const data::NodePtr &a, const data::NodePtr &b);

[[nodiscard]] Result<data::NodePtr, String> ApplyComparison(const String &op, const data::NodePtr &a,
																	  const data::NodePtr &b);

/// Resolves one RPN operand token (identifier or literal) to a runtime
/// value. An identifier whose text is `TRUE`/`FALSE` (case-insensitive) is
/// a bool literal, not a column reference — the tokenizer has no separate
/// boolean-literal token type, so this is where that distinction is made.
[[nodiscard]] Result<data::NodePtr, String> ResolveOperand(const Token &tok,
																	 const std::vector<data::NodePtr> &row,
																	 const std::vector<String> &columnNames);

} // namespace detail

/// Evaluates `expr`'s RPN token list left to right against one `row`
/// (`columnNames` gives that row's schema column order — see
/// Schema::ColumnNames). The final (and only) value left on the stack is
/// the result; for a WHERE clause this must be a bool, so a non-bool result
/// (or a malformed RPN sequence — stack underflow / leftover operands) is
/// an Err, never a silently-wrong answer.
[[nodiscard]] Result<data::NodePtr, String> Evaluate(const Expression &expr,
															   const std::vector<data::NodePtr> &row,
															   const std::vector<String> &columnNames);

} // namespace sql
