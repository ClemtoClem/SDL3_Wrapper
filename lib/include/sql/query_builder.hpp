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
	QueryBuilder &Select(std::vector<String> columns);

	QueryBuilder &From(String tableName);

	/// Repeated calls AND together. `op` must be one of the comparison
	/// operators expression.hpp understands: = <> != < <= > >=.
	QueryBuilder &Where(String column, String op, data::NodePtr value);

	/// Builds and runs the SELECT against `db`. Err if From(...) was never
	/// called, an unsupported Where(...) operator was used, a Where(...)
	/// value wasn't a scalar (STRING/BOOL/INT/FLOAT), or the underlying
	/// SELECT execution itself errors (unknown table/column, ...).
	[[nodiscard]] Result<QueryResult, String> Execute(Database &db) const;

private:
	struct Condition {
		String column;
		String op;
		data::NodePtr value;
	};

	[[nodiscard]] static bool IsSupportedOperator(const String &op) noexcept;

	/// Converts one Where(...) value into the RPN operand token
	/// expression.hpp::Evaluate expects — Err for anything that isn't a
	/// scalar (OBJECT/ARRAY/NONE are data::'s tree-shaped document concern,
	/// never a SQL cell value).
	[[nodiscard]] static Result<Token, String> ValueToLiteralToken(const data::NodePtr &value);

	/// Directly emits the RPN token sequence for "cond1 AND cond2 AND ...":
	/// each condition contributes `column value op`, and every condition
	/// after the first is followed by AND — no shunting-yard needed since a
	/// flat AND-chain has no precedence ambiguity to resolve.
	[[nodiscard]] Result<std::vector<Token>, String> BuildWhereRpn() const;

	String m_tableName;
	std::vector<String> m_columns;
	std::vector<Condition> m_conditions;
};

} // namespace sql
