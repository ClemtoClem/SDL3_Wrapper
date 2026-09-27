// Définitions de sql/expression.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sql/expression.hpp"

namespace sql {

namespace detail {

bool IsLogicalKeyword(const Token &t, const char *word) noexcept {
	return t.type == TokenType::KEYWORD && t.text.ToUpper() == word;
}

int Precedence(const Token &t) noexcept {
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

bool IsOpenParen(const Token &t) noexcept {
	return t.type == TokenType::PUNCTUATION && t.text == "(";
}

bool IsCloseParen(const Token &t) noexcept {
	return t.type == TokenType::PUNCTUATION && t.text == ")";
}

bool IsOperandToken(const Token &t) noexcept {
	return t.type == TokenType::IDENTIFIER || t.type == TokenType::STRING_LITERAL ||
		   t.type == TokenType::INT_LITERAL || t.type == TokenType::FLOAT_LITERAL;
}

} // namespace detail

Result<Expression, String> ParseExpression(const std::vector<Token> &tokens, size_t &pos) {
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

Result<int, String> Compare3Way(const data::NodePtr &a, const data::NodePtr &b) {
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

Result<data::NodePtr, String> ApplyComparison(const String &op, const data::NodePtr &a, const data::NodePtr &b) {
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

Result<data::NodePtr, String> ResolveOperand(const Token &tok,
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

Result<data::NodePtr, String> Evaluate(const Expression &expr,
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
