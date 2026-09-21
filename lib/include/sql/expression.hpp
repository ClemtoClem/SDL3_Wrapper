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

[[nodiscard]] inline bool IsLogicalKeyword(const Token &t, const char *word) noexcept {
	return t.type == TokenType::KEYWORD && t.text.ToUpper() == word;
}

/// -1 for a token that is not an operator at all (identifiers/literals/
/// parens use this as a sentinel, never actually compared with it).
[[nodiscard]] inline int Precedence(const Token &t) noexcept {
	if (IsComparisonOperator(t))
		return 3;
	if (IsLogicalKeyword(t, "NOT"))
		return 2;
	if (IsLogicalKeyword(t, "AND"))
		return 1;
	if (IsLogicalKeyword(t, "OR"))
		return 0;
	return -1;
}

[[nodiscard]] inline bool IsOperatorToken(const Token &t) noexcept { return Precedence(t) >= 0; }

[[nodiscard]] inline bool IsRightAssociative(const Token &t) noexcept { return IsLogicalKeyword(t, "NOT"); }

[[nodiscard]] inline bool IsOpenParen(const Token &t) noexcept {
	return t.type == TokenType::PUNCTUATION && t.text == "(";
}

[[nodiscard]] inline bool IsCloseParen(const Token &t) noexcept {
	return t.type == TokenType::PUNCTUATION && t.text == ")";
}

[[nodiscard]] inline bool IsOperandToken(const Token &t) noexcept {
	return t.type == TokenType::IDENTIFIER || t.type == TokenType::STRING_LITERAL ||
		   t.type == TokenType::INT_LITERAL || t.type == TokenType::FLOAT_LITERAL;
}

} // namespace detail

/// Shunting-yard parse of a comparison/logical expression starting at
/// `tokens[pos]`. Advances `pos` to just past the last consumed token, so
/// the caller (parser.hpp) knows exactly where the expression ended — e.g.
/// at a trailing ';', a following ORDER BY, or end-of-input.
[[nodiscard]] inline Result<Expression, String> ParseExpression(const std::vector<Token> &tokens, size_t &pos) {
	std::vector<Token> output;
	std::vector<Token> opStack;
	bool expectOperand = true;
	size_t parenDepth = 0;

	while (pos < tokens.size()) {
		const Token &tok = tokens[pos];

		if (expectOperand) {
			if (detail::IsOperandToken(tok)) {
				output.push_back(tok);
				++pos;
				expectOperand = false;
				continue;
			}
			if (detail::IsLogicalKeyword(tok, "NOT")) {
				opStack.push_back(tok);
				++pos;
				continue; // still expecting an operand after NOT
			}
			if (detail::IsOpenParen(tok)) {
				opStack.push_back(tok);
				++parenDepth;
				++pos;
				continue;
			}
			return Err(String("sql: expected an operand in expression, got '") + tok.text + "'");
		}

		// Not expecting an operand: an operator, ')' or the natural end of
		// the expression (';', ORDER BY, end-of-input, ...) is valid here.
		if (detail::IsOperatorToken(tok)) {
			int newPrec = detail::Precedence(tok);
			while (!opStack.empty() && !detail::IsOpenParen(opStack.back())) {
				int topPrec = detail::Precedence(opStack.back());
				if (topPrec > newPrec || (topPrec == newPrec && !detail::IsRightAssociative(tok))) {
					output.push_back(opStack.back());
					opStack.pop_back();
				} else {
					break;
				}
			}
			opStack.push_back(tok);
			++pos;
			expectOperand = true;
			continue;
		}

		if (detail::IsCloseParen(tok)) {
			if (parenDepth == 0)
				break; // this ')' belongs to an outer context, not ours
			bool foundOpen = false;
			while (!opStack.empty()) {
				if (detail::IsOpenParen(opStack.back())) {
					opStack.pop_back();
					foundOpen = true;
					break;
				}
				output.push_back(opStack.back());
				opStack.pop_back();
			}
			if (!foundOpen)
				return Err(String("sql: mismatched parentheses in expression"));
			--parenDepth;
			++pos;
			continue;
		}

		break; // natural end of the expression
	}

	if (expectOperand)
		return Err(String("sql: expression ended unexpectedly, expected an operand"));

	while (!opStack.empty()) {
		if (detail::IsOpenParen(opStack.back()))
			return Err(String("sql: mismatched parentheses in expression"));
		output.push_back(opStack.back());
		opStack.pop_back();
	}

	if (output.empty())
		return Err(String("sql: empty expression"));

	return Ok(Expression{std::move(output)});
}

namespace detail {

/// 3-way compare of two scalar values for use by the comparison operators.
/// Numeric (INT/FLOAT, in any combination) compares by value; STRING
/// compares lexicographically; BOOL compares false < true. Any other
/// pairing (e.g. STRING vs INT) is a clear Err, never a silent false.
[[nodiscard]] inline Result<int, String> Compare3Way(const data::NodePtr &a, const data::NodePtr &b) {
	if (a->IsNumber() && b->IsNumber()) {
		double av = a->IsInt() ? static_cast<double>(a->intValue) : a->floatValue;
		double bv = b->IsInt() ? static_cast<double>(b->intValue) : b->floatValue;
		return Ok((av < bv) ? -1 : (av > bv) ? 1 : 0);
	}
	if (a->type == b->type) {
		if (a->IsString())
			return Ok(a->stringValue.Compare(b->stringValue));
		if (a->IsBool())
			return Ok((a->boolValue ? 1 : 0) - (b->boolValue ? 1 : 0));
	}
	return Err(String("sql: type mismatch in comparison ('") + NodeTypeName(a->type) + "' vs '" +
					NodeTypeName(b->type) + "')");
}

[[nodiscard]] inline Result<data::NodePtr, String> ApplyComparison(const String &op, const data::NodePtr &a,
																	  const data::NodePtr &b) {
	auto cmp = Compare3Way(a, b);
	if (cmp.IsError())
		return Err(cmp.Error());
	int c = cmp.Value();
	if (op == "=")
		return Ok(data::Node::MakeBool(c == 0));
	if (op == "<>" || op == "!=")
		return Ok(data::Node::MakeBool(c != 0));
	if (op == "<")
		return Ok(data::Node::MakeBool(c < 0));
	if (op == "<=")
		return Ok(data::Node::MakeBool(c <= 0));
	if (op == ">")
		return Ok(data::Node::MakeBool(c > 0));
	if (op == ">=")
		return Ok(data::Node::MakeBool(c >= 0));
	return Err(String("sql: unknown comparison operator '") + op + "'");
}

/// Resolves one RPN operand token (identifier or literal) to a runtime
/// value. An identifier whose text is `TRUE`/`FALSE` (case-insensitive) is
/// a bool literal, not a column reference — the tokenizer has no separate
/// boolean-literal token type, so this is where that distinction is made.
[[nodiscard]] inline Result<data::NodePtr, String> ResolveOperand(const Token &tok,
																	 const std::vector<data::NodePtr> &row,
																	 const std::vector<String> &columnNames) {
	switch (tok.type) {
		case TokenType::STRING_LITERAL:
			return Ok(data::Node::MakeString(tok.text));
		case TokenType::INT_LITERAL: {
			auto v = tok.text.TryParseInt();
			if (v.IsNone())
				return Err(String("sql: invalid integer literal '") + tok.text + "'");
			return Ok(data::Node::MakeInt(*v));
		}
		case TokenType::FLOAT_LITERAL: {
			auto v = tok.text.TryParseDouble();
			if (v.IsNone())
				return Err(String("sql: invalid float literal '") + tok.text + "'");
			return Ok(data::Node::MakeFloat(*v));
		}
		case TokenType::IDENTIFIER: {
			String upper = tok.text.ToUpper();
			if (upper == "TRUE")
				return Ok(data::Node::MakeBool(true));
			if (upper == "FALSE")
				return Ok(data::Node::MakeBool(false));
			for (size_t i = 0; i < columnNames.size(); ++i) {
				if (columnNames[i] == tok.text)
					return Ok(row[i]);
			}
			return Err(String("sql: unknown column '") + tok.text + "' in expression");
		}
		default:
			return Err(String("sql: unexpected token '") + tok.text + "' in expression");
	}
}

} // namespace detail

/// Evaluates `expr`'s RPN token list left to right against one `row`
/// (`columnNames` gives that row's schema column order — see
/// Schema::ColumnNames). The final (and only) value left on the stack is
/// the result; for a WHERE clause this must be a bool, so a non-bool result
/// (or a malformed RPN sequence — stack underflow / leftover operands) is
/// an Err, never a silently-wrong answer.
[[nodiscard]] inline Result<data::NodePtr, String> Evaluate(const Expression &expr,
															   const std::vector<data::NodePtr> &row,
															   const std::vector<String> &columnNames) {
	std::vector<data::NodePtr> stack;

	for (const Token &tok : expr.rpn) {
		if (detail::IsOperandToken(tok)) {
			auto v = detail::ResolveOperand(tok, row, columnNames);
			if (v.IsError())
				return Err(v.Error());
			stack.push_back(v.Value());
			continue;
		}

		if (detail::IsLogicalKeyword(tok, "NOT")) {
			if (stack.empty())
				return Err(String("sql: malformed expression (stack underflow at NOT)"));
			data::NodePtr a = stack.back();
			stack.pop_back();
			if (!a->IsBool())
				return Err(String("sql: NOT requires a boolean operand, got ") + NodeTypeName(a->type));
			stack.push_back(data::Node::MakeBool(!a->boolValue));
			continue;
		}

		if (stack.size() < 2)
			return Err(String("sql: malformed expression (stack underflow at '") + tok.text + "')");
		data::NodePtr b = stack.back();
		stack.pop_back();
		data::NodePtr a = stack.back();
		stack.pop_back();

		if (detail::IsLogicalKeyword(tok, "AND") || detail::IsLogicalKeyword(tok, "OR")) {
			if (!a->IsBool() || !b->IsBool())
				return Err(String("sql: '") + tok.text + "' requires boolean operands");
			bool result = detail::IsLogicalKeyword(tok, "AND") ? (a->boolValue && b->boolValue)
																 : (a->boolValue || b->boolValue);
			stack.push_back(data::Node::MakeBool(result));
			continue;
		}

		if (detail::IsComparisonOperator(tok)) {
			auto r = detail::ApplyComparison(tok.text, a, b);
			if (r.IsError())
				return Err(r.Error());
			stack.push_back(r.Value());
			continue;
		}

		return Err(String("sql: unexpected token '") + tok.text + "' in expression");
	}

	if (stack.size() != 1)
		return Err(String("sql: malformed expression (expected exactly one result)"));
	if (!stack.back()->IsBool())
		return Err(String("sql: expression must evaluate to a boolean, got ") + NodeTypeName(stack.back()->type));
	return Ok(stack.back());
}

} // namespace sql
