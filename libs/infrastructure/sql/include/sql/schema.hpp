#pragma once
/**
 * sql::Schema — a table's column list: name + scalar type, in declaration
 * order (also the positional order InsertStatement's values and a stored
 * row's std::vector<dataio::NodePtr> follow — see table.hpp's Table<Row>
 * instantiated as Table<std::vector<dataio::NodePtr>> in database.hpp).
 *
 * `type` is restricted to dataio::NodeType's 4 scalars (STRING/BOOL/INT/
 * FLOAT) — a SQL cell is never an OBJECT/ARRAY, those are data::'s own
 * tree-shaped document concern.
 */
#include <core/core.hpp>
#include <dataio/node.hpp>

#include <vector>

namespace sql {

struct ColumnDef {
	String name;
	dataio::NodeType type = dataio::NodeType::STRING;
};

struct Schema {
	std::vector<ColumnDef> columns;

	/// Index of the column named `name`, or NONE if no such column exists.
	[[nodiscard]] Option<size_t> ColumnIndex(StringView name) const;

	/// Column names in declaration order.
	[[nodiscard]] std::vector<String> ColumnNames() const;
};

/// Human-readable name of a scalar dataio::NodeType, for error messages
/// (type-mismatch reports in database.hpp / expression.hpp).
[[nodiscard]] String NodeTypeName(dataio::NodeType type);

} // namespace sql
