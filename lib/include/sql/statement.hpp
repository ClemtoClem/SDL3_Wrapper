#pragma once
/**
 * sql:: parsed statement AST — pure data, no validation logic (validation
 * against a live Database's schemas happens in database.hpp's execution
 * methods, not here).
 */
#include "../core/core.hpp"
#include "../data/node.hpp"
#include "expression.hpp"
#include "schema.hpp"

#include <variant>
#include <vector>

namespace sql {

struct CreateTableStatement {
	String tableName;
	Schema schema;
};

/// Positional values — must match the target table's schema column
/// count/order, validated at execution time (database.hpp).
struct InsertStatement {
	String tableName;
	std::vector<data::NodePtr> values;
};

struct SelectStatement {
	String tableName;
	std::vector<String> columns; // empty = SELECT *
	Option<Expression> where;
};

struct DeleteStatement {
	String tableName;
	Option<Expression> where; // NONE = delete all rows
};

struct DropTableStatement {
	String tableName;
};

using Statement = std::variant<CreateTableStatement, InsertStatement, SelectStatement, DeleteStatement,
								DropTableStatement>;

} // namespace sql
