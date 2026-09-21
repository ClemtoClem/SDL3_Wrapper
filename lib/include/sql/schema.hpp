#pragma once
/**
 * sql::Schema — a table's column list: name + scalar type, in declaration
 * order (also the positional order InsertStatement's values and a stored
 * row's std::vector<data::NodePtr> follow — see table.hpp's Table<Row>
 * instantiated as Table<std::vector<data::NodePtr>> in database.hpp).
 *
 * `type` is restricted to data::NodeType's 4 scalars (STRING/BOOL/INT/
 * FLOAT) — a SQL cell is never an OBJECT/ARRAY, those are data::'s own
 * tree-shaped document concern.
 */
#include "../core/core.hpp"
#include "../data/node.hpp"

#include <vector>

namespace sql {

struct ColumnDef {
	String name;
	data::NodeType type = data::NodeType::STRING;
};

struct Schema {
	std::vector<ColumnDef> columns;

	/// Index of the column named `name`, or NONE if no such column exists.
	[[nodiscard]] Option<size_t> ColumnIndex(StringView name) const {
		for (size_t i = 0; i < columns.size(); ++i)
			if (columns[i].name.View() == name)
				return Some(i);
		return NONE;
	}

	/// Column names in declaration order.
	[[nodiscard]] std::vector<String> ColumnNames() const {
		std::vector<String> names;
		names.reserve(columns.size());
		for (auto &col : columns)
			names.push_back(col.name);
		return names;
	}
};

/// Human-readable name of a scalar data::NodeType, for error messages
/// (type-mismatch reports in database.hpp / expression.hpp).
[[nodiscard]] inline String NodeTypeName(data::NodeType type) {
	switch (type) {
		case data::NodeType::STRING:
			return "STRING";
		case data::NodeType::BOOL:
			return "BOOL";
		case data::NodeType::INT:
			return "INT";
		case data::NodeType::FLOAT:
			return "FLOAT";
		case data::NodeType::OBJECT:
			return "OBJECT";
		case data::NodeType::ARRAY:
			return "ARRAY";
		case data::NodeType::NONE:
		default:
			return "NONE";
	}
}

} // namespace sql
