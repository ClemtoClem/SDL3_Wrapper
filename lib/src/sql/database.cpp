// Définitions de sql/database.hpp
#include "sql/database.hpp"

namespace sql {

// ── Database ─────────────────────────────────────────────────────────────────

Result<QueryResult, String> Database::Execute(const String &sql) {
	auto tokensR = Tokenizer::Tokenize(sql.View());
	if (tokensR.IsError())
		return Err(tokensR.Error());

	auto stmtR = ParseStatement(tokensR.Value());
	if (stmtR.IsError())
		return Err(stmtR.Error());

	return std::visit(
		[this](auto &&stmt) -> Result<QueryResult, String> {
			using T = std::decay_t<decltype(stmt)>;
			if constexpr (std::is_same_v<T, CreateTableStatement>) {
				return ExecuteCreateTable(stmt);
			} else if constexpr (std::is_same_v<T, InsertStatement>) {
				return ExecuteInsert(stmt);
			} else if constexpr (std::is_same_v<T, SelectStatement>) {
				return ExecuteSelect(stmt);
			} else if constexpr (std::is_same_v<T, DeleteStatement>) {
				return ExecuteDelete(stmt);
			} else {
				static_assert(std::is_same_v<T, DropTableStatement>);
				return ExecuteDropTable(stmt);
			}
		},
		stmtR.Value());
}

Result<QueryResult, String> Database::ExecuteSelectStatement(const SelectStatement &stmt) {
	return ExecuteSelect(stmt);
}

std::vector<String> Database::TableNames() const {
	std::vector<String> names;
	names.reserve(m_schemas.size());
	for (auto &entry : m_schemas)
		names.push_back(entry.first);
	return names;
}

Option<Ref<Schema>> Database::GetSchema(const String &tableName) const {
	auto it = m_schemas.find(tableName);
	if (it == m_schemas.end())
		return NONE;
	return Some(Ref<Schema>(it->second));
}

Result<QueryResult, String> Database::CreateTable(const String &tableName, Schema schema) {
	if (m_schemas.find(tableName) != m_schemas.end())
		return Err(String("sql: table '") + tableName + "' already exists");

	m_schemas[tableName] = std::move(schema);
	m_tables[tableName] = Table<Row>();

	QueryResult result;
	result.message = String("table '") + tableName + "' created";
	return Ok(result);
}

Result<QueryResult, String> Database::InsertRow(const String &tableName, Row values) {
	auto schemaIt = m_schemas.find(tableName);
	if (schemaIt == m_schemas.end())
		return Err(String("sql: unknown table '") + tableName + "'");
	const Schema &schema = schemaIt->second;

	if (values.size() != schema.columns.size())
		return Err(String::Format("sql: INSERT INTO '%s' expects %zu value(s), got %zu", tableName.CStr(),
								   schema.columns.size(), values.size()));

	auto coercedR = CoerceInsertValues(values, schema);
	if (coercedR.IsError())
		return Err(coercedR.Error());

	m_tables[tableName].Insert(coercedR.Value());

	QueryResult result;
	result.message = "1 row inserted";
	return Ok(result);
}

Result<QueryResult, String> Database::CreateIndex(const String &tableName, const String &columnName) {
	auto schemaIt = m_schemas.find(tableName);
	if (schemaIt == m_schemas.end())
		return Err(String("sql: unknown table '") + tableName + "'");
	const Schema &schema = schemaIt->second;

	auto colIdxOpt = schema.ColumnIndex(columnName.View());
	if (colIdxOpt.IsNone())
		return Err(String("sql: unknown column '") + columnName + "' in table '" + tableName + "'");
	size_t colIdx = *colIdxOpt;

	BTreeIndex index;
	const Table<Row> &table = m_tables.at(tableName);
	table.ForEachLive([&](size_t id, const Row &row) { index.Insert(row[colIdx], id); });

	m_indexes.insert_or_assign(std::make_pair(tableName, columnName), std::move(index));

	QueryResult result;
	result.message = String("index created on '") + tableName + "." + columnName + "'";
	return Ok(result);
}

Result<std::vector<Row>, String> Database::LookupByIndex(const String &tableName, const String &columnName,
		const data::NodePtr &key) const {
	auto idxIt = m_indexes.find(std::make_pair(tableName, columnName));
	if (idxIt == m_indexes.end())
		return Err(String("sql: no index on '") + tableName + "." + columnName + "' (call CreateIndex first)");

	auto tableIt = m_tables.find(tableName);
	if (tableIt == m_tables.end())
		return Err(String("sql: unknown table '") + tableName + "'");

	std::vector<Row> results;
	for (size_t id : idxIt->second.Find(key)) {
		auto rowOpt = tableIt->second.Get(id);
		if (rowOpt.IsSome())
			results.push_back(*rowOpt.Value());
	}
	return Ok(std::move(results));
}

Result<QueryResult, String> Database::ExecuteCreateTable(const CreateTableStatement &stmt) {
	return CreateTable(stmt.tableName, stmt.schema);
}

Result<Row, String> Database::CoerceInsertValues(const std::vector<data::NodePtr> &values, const Schema &schema) {
	Row coerced;
	coerced.reserve(values.size());
	for (size_t i = 0; i < values.size(); ++i) {
		const data::NodePtr &v = values[i];
		data::NodeType expected = schema.columns[i].type;
		if (v->type == expected) {
			coerced.push_back(v);
			continue;
		}
		if (expected == data::NodeType::FLOAT && v->IsInt()) {
			coerced.push_back(data::Node::MakeFloat(static_cast<double>(v->intValue)));
			continue;
		}
		return Err(String("sql: value for column '") + schema.columns[i].name + "' has type " +
					NodeTypeName(v->type) + ", expected " + NodeTypeName(expected));
	}
	return Ok(std::move(coerced));
}

Result<QueryResult, String> Database::ExecuteInsert(const InsertStatement &stmt) {
	return InsertRow(stmt.tableName, stmt.values);
}

Result<QueryResult, String> Database::ExecuteSelect(const SelectStatement &stmt) {
	auto schemaIt = m_schemas.find(stmt.tableName);
	if (schemaIt == m_schemas.end())
		return Err(String("sql: unknown table '") + stmt.tableName + "'");
	const Schema &schema = schemaIt->second;
	const Table<Row> &table = m_tables.at(stmt.tableName);
	std::vector<String> allColumnNames = schema.ColumnNames();

	std::vector<size_t> selIndices;
	Schema outSchema;
	if (stmt.columns.empty()) {
		outSchema = schema;
		for (size_t i = 0; i < schema.columns.size(); ++i)
			selIndices.push_back(i);
	} else {
		for (const String &colName : stmt.columns) {
			auto idx = schema.ColumnIndex(colName.View());
			if (idx.IsNone())
				return Err(String("sql: unknown column '") + colName + "' in SELECT");
			selIndices.push_back(*idx);
			outSchema.columns.push_back(schema.columns[*idx]);
		}
	}

	QueryResult result;
	result.schema = outSchema;

	bool hadError = false;
	String errMsg;
	table.ForEachLive([&](size_t /*id*/, const Row &row) {
		if (hadError)
			return;
		if (stmt.where.IsSome()) {
			auto evalR = Evaluate(*stmt.where, row, allColumnNames);
			if (evalR.IsError()) {
				hadError = true;
				errMsg = evalR.Error();
				return;
			}
			if (!evalR.Value()->boolValue)
				return;
		}
		Row outRow;
		outRow.reserve(selIndices.size());
		for (size_t idx : selIndices)
			outRow.push_back(row[idx]);
		result.rows.push_back(std::move(outRow));
	});
	if (hadError)
		return Err(errMsg);

	result.message = String::Format("%zu row(s) selected", result.rows.size());
	return Ok(result);
}

Result<QueryResult, String> Database::ExecuteDelete(const DeleteStatement &stmt) {
	auto schemaIt = m_schemas.find(stmt.tableName);
	if (schemaIt == m_schemas.end())
		return Err(String("sql: unknown table '") + stmt.tableName + "'");
	std::vector<String> allColumnNames = schemaIt->second.ColumnNames();
	Table<Row> &table = m_tables.at(stmt.tableName);

	// Collect ids first — ForEachLive iterates const, and deleting
	// while iterating would invalidate it.
	std::vector<size_t> idsToDelete;
	bool hadError = false;
	String errMsg;
	table.ForEachLive([&](size_t id, const Row &row) {
		if (hadError)
			return;
		bool matches = true;
		if (stmt.where.IsSome()) {
			auto evalR = Evaluate(*stmt.where, row, allColumnNames);
			if (evalR.IsError()) {
				hadError = true;
				errMsg = evalR.Error();
				return;
			}
			matches = evalR.Value()->boolValue;
		}
		if (matches)
			idsToDelete.push_back(id);
	});
	if (hadError)
		return Err(errMsg);

	for (size_t id : idsToDelete)
		table.Delete(id);

	QueryResult result;
	result.message = String::Format("%zu row(s) deleted", idsToDelete.size());
	return Ok(result);
}

Result<QueryResult, String> Database::ExecuteDropTable(const DropTableStatement &stmt) {
	auto schemaIt = m_schemas.find(stmt.tableName);
	if (schemaIt == m_schemas.end())
		return Err(String("sql: unknown table '") + stmt.tableName + "'");

	m_schemas.erase(schemaIt);
	m_tables.erase(stmt.tableName);

	QueryResult result;
	result.message = String("table '") + stmt.tableName + "' dropped";
	return Ok(result);
}

} // namespace sql
