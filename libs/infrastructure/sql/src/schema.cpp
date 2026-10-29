// Définitions de sql/schema.hpp
#include <sql/schema.hpp>

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

String NodeTypeName(dataio::NodeType type) {
	switch (type) {
		case dataio::NodeType::STRING:
			return "STRING";
		case dataio::NodeType::BOOL:
			return "BOOL";
		case dataio::NodeType::INT:
			return "INT";
		case dataio::NodeType::FLOAT:
			return "FLOAT";
		case dataio::NodeType::OBJECT:
			return "OBJECT";
		case dataio::NodeType::ARRAY:
			return "ARRAY";
		case dataio::NodeType::NONE:
		default:
			return "NONE";
	}
}

} // namespace sql
