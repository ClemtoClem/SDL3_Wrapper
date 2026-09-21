#pragma once
/**
 * sql::QueryBuilder — the "builder": a fluent C++ API for SELECT queries
 * that needs no SQL text at all, chaining by returning `*this` (this
 * codebase's established Builder convention — cf. ui::WidgetBuilder,
 * ui/factory.hpp; render3d::ShaderBuilder, render3d/shader_builder.hpp).
 *
 * `.Where(...)` calls are ANDed together — kept deliberately simple, no
 * OR/parenthesization in the fluent API itself (a caller who genuinely
 * needs that can write raw SQL text through Database::Execute directly,
 * which is more expressive by construction).
 *
 * Execute() builds a SelectStatement's Expression RPN directly from the
 * accumulated conditions — no round-trip through token.hpp/parser.hpp,
 * since the builder already has structured data — then hands it to
 * Database::ExecuteSelectStatement, reusing the exact same per-row/WHERE-
 * evaluation logic Execute(String)'s SELECT path uses internally.
 */
#include "../core/core.hpp"
#include "../data/node.hpp"
#include "database.hpp"
#include "expression.hpp"
#include "statement.hpp"
#include "token.hpp"

#include <array>
#include <vector>

namespace sql {

class QueryBuilder {
public:
	QueryBuilder() = default;

	/// Empty/unset (the default) means `SELECT *`.
	QueryBuilder &Select(std::vector<String> columns) {
		m_columns = std::move(columns);
		return *this;
	}

	QueryBuilder &From(String tableName) {
		m_tableName = std::move(tableName);
		return *this;
	}

	/// Repeated calls AND together. `op` must be one of the comparison
	/// operators expression.hpp understands: = <> != < <= > >=.
	QueryBuilder &Where(String column, String op, data::NodePtr value) {
		m_conditions.push_back(Condition{std::move(column), std::move(op), std::move(value)});
		return *this;
	}

	/// Builds and runs the SELECT against `db`. Err if From(...) was never
	/// called, an unsupported Where(...) operator was used, a Where(...)
	/// value wasn't a scalar (STRING/BOOL/INT/FLOAT), or the underlying
	/// SELECT execution itself errors (unknown table/column, ...).
	[[nodiscard]] Result<QueryResult, String> Execute(Database &db) const {
		if (m_tableName.IsEmpty())
			return Err(String("sql: QueryBuilder.From(...) was not set"));

		SelectStatement stmt;
		stmt.tableName = m_tableName;
		stmt.columns = m_columns;

		if (!m_conditions.empty()) {
			auto rpnR = BuildWhereRpn();
			if (rpnR.IsError())
				return Err(rpnR.Error());
			stmt.where = Some(Expression{rpnR.Value()});
		}

		return db.ExecuteSelectStatement(stmt);
	}

private:
	struct Condition {
		String column;
		String op;
		data::NodePtr value;
	};

	[[nodiscard]] static bool IsSupportedOperator(const String &op) noexcept {
		static const std::array<const char *, 7> OPS = {"=", "<>", "!=", "<", "<=", ">", ">="};
		for (auto *o : OPS)
			if (op == o)
				return true;
		return false;
	}

	/// Converts one Where(...) value into the RPN operand token
	/// expression.hpp::Evaluate expects — Err for anything that isn't a
	/// scalar (OBJECT/ARRAY/NONE are data::'s tree-shaped document concern,
	/// never a SQL cell value).
	[[nodiscard]] static Result<Token, String> ValueToLiteralToken(const data::NodePtr &value) {
		if (!value)
			return Err(String("sql: QueryBuilder.Where(...) value must not be null"));
		switch (value->type) {
			case data::NodeType::STRING:
				return Ok(Token{TokenType::STRING_LITERAL, value->stringValue});
			case data::NodeType::INT:
				return Ok(Token{TokenType::INT_LITERAL, String::From(value->intValue)});
			case data::NodeType::FLOAT:
				return Ok(Token{TokenType::FLOAT_LITERAL, String::From(value->floatValue, 6)});
			case data::NodeType::BOOL:
				return Ok(Token{TokenType::IDENTIFIER, value->boolValue ? String("TRUE") : String("FALSE")});
			default:
				return Err(String("sql: QueryBuilder.Where(...) value must be a scalar (STRING/BOOL/INT/FLOAT)"));
		}
	}

	/// Directly emits the RPN token sequence for "cond1 AND cond2 AND ...":
	/// each condition contributes `column value op`, and every condition
	/// after the first is followed by AND — no shunting-yard needed since a
	/// flat AND-chain has no precedence ambiguity to resolve.
	[[nodiscard]] Result<std::vector<Token>, String> BuildWhereRpn() const {
		std::vector<Token> rpn;
		for (size_t i = 0; i < m_conditions.size(); ++i) {
			const Condition &cond = m_conditions[i];
			if (!IsSupportedOperator(cond.op))
				return Err(String("sql: QueryBuilder.Where(...) unsupported operator '") + cond.op + "'");

			auto litR = ValueToLiteralToken(cond.value);
			if (litR.IsError())
				return Err(litR.Error());

			rpn.push_back(Token{TokenType::IDENTIFIER, cond.column});
			rpn.push_back(litR.Value());
			rpn.push_back(Token{TokenType::OPERATOR, cond.op});
			if (i > 0)
				rpn.push_back(Token{TokenType::KEYWORD, String("AND")});
		}
		return Ok(std::move(rpn));
	}

	String m_tableName;
	std::vector<String> m_columns;
	std::vector<Condition> m_conditions;
};

} // namespace sql
