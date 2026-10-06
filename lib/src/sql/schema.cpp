// Définitions de sql/schema.hpp
#include "sql/schema.hpp"

namespace sql {

// ── Schema ───────────────────────────────────────────────────────────────────

Option<size_t> Schema::ColumnIndex(StringView name) const {
	for (size_t i = 0; i < columns.size(); ++i)
		if (columns[i].name.View() == name)
			return Some(i);
	return NONE;
}

std::vector<String> Schema::ColumnNames() const {
	std::vector<String> names;
	names.reserve(columns.size());
	for (auto &col : columns)
		names.push_back(col.name);
	return names;
}

String NodeTypeName(data::NodeType type) {
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
