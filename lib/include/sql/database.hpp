#pragma once
/**
 * sql::Database — the "factory": a named-table registry that tokenizes,
 * parses, and executes SQL text against in-memory tables. Instance-based (a
 * user creates and owns one Database), not a global singleton like
 * data::DocumentFactory, since a database is inherently stateful per
 * instance.
 *
 * Storage reuses sql::Table<Row> (table.hpp) directly, instantiated as
 * Table<std::vector<data::NodePtr>> — one row is a positional vector of
 * cell values, ordered per that table's Schema's column order. No new
 * insert/delete/stable-id logic here; that's already correct in table.hpp.
 *
 * Table storage stays purely in-memory here — binary_storage.hpp layers
 * whole-Database persistence to a custom binary file format on top (see
 * sql.hpp's module doc comment), reusing this file's CreateTable/InsertRow
 * to reconstruct a loaded Database without a SQL-text round-trip.
 *
 * CreateIndex/LookupByIndex below layer an optional secondary BTreeIndex
 * (btree_index.hpp) per (table, column) on top of the same Table<Row>
 * storage — a SNAPSHOT built by scanning the table's live rows at
 * CreateIndex-call time, not a live-maintained structure (see CreateIndex's
 * own doc comment for the rationale).
 */
#include "../core/core.hpp"
#include "../data/node.hpp"
#include "btree_index.hpp"
#include "expression.hpp"
#include "parser.hpp"
#include "schema.hpp"
#include "statement.hpp"
#include "table.hpp"
#include "token.hpp"

#include <map>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace sql {

using Row = std::vector<data::NodePtr>;

/// A SELECT populates `schema` + `rows`; CREATE TABLE / INSERT / DELETE /
/// DROP TABLE leave `rows` empty and set a human-readable `message` (e.g.
/// "1 row inserted", "table dropped").
struct QueryResult {
	Schema schema;
	std::vector<Row> rows;
	String message;
};

class Database {
public:
	Database() = default;

	/// Tokenizes -> parses -> executes one SQL statement. Err on any lexer,
	/// parser, or schema-validation failure (unknown table, wrong value
	/// count, unknown column, type mismatch, malformed WHERE, ...).
	[[nodiscard]] Result<QueryResult, String> Execute(const String &sql) {
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

	/// Executes an already-built SelectStatement directly, bypassing the
	/// tokenizer/parser — used by query_builder.hpp's fluent QueryBuilder,
	/// which already has structured data. Reuses the exact same per-row /
	/// WHERE-evaluation logic as Execute(String)'s SELECT path.
	[[nodiscard]] Result<QueryResult, String> ExecuteSelectStatement(const SelectStatement &stmt) {
		return ExecuteSelect(stmt);
	}

	/// The reference project's "SHOW TABLES" equivalent, as a plain C++
	/// method rather than another parsed statement kind.
	[[nodiscard]] std::vector<String> TableNames() const {
		std::vector<String> names;
		names.reserve(m_schemas.size());
		for (auto &entry : m_schemas)
			names.push_back(entry.first);
		return names;
	}

	[[nodiscard]] Option<Ref<Schema>> GetSchema(const String &tableName) const {
		auto it = m_schemas.find(tableName);
		if (it == m_schemas.end())
			return NONE;
		return Some(Ref<Schema>(it->second));
	}

	/// Registers a new table named `tableName` with `schema`. Err if a table
	/// with that name already exists. Extracted out of ExecuteCreateTable so
	/// binary_storage.hpp's LoadDatabaseBinary can rebuild a table directly
	/// from an already-typed Schema without round-tripping through SQL text
	/// (a CREATE TABLE statement string).
	[[nodiscard]] Result<QueryResult, String> CreateTable(const String &tableName, Schema schema) {
		if (m_schemas.find(tableName) != m_schemas.end())
			return Err(String("sql: table '") + tableName + "' already exists");

		m_schemas[tableName] = std::move(schema);
		m_tables[tableName] = Table<Row>();

		QueryResult result;
		result.message = String("table '") + tableName + "' created";
		return Ok(result);
	}

	/// Validates `values` against `tableName`'s schema (unknown table, wrong
	/// value count, INT->FLOAT coercion via CoerceInsertValues) and appends
	/// them as a new row. Extracted out of ExecuteInsert so
	/// binary_storage.hpp's LoadDatabaseBinary can insert already-typed Row
	/// data directly — deliberately NOT by generating and re-parsing SQL
	/// text, since a STRING cell containing a literal ' would need
	/// re-escaping to survive a text round-trip, needless risk for
	/// structured data that was never user-typed SQL in the first place.
	[[nodiscard]] Result<QueryResult, String> InsertRow(const String &tableName, Row values) {
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

	/// Builds a fresh BTreeIndex over `columnName` in `tableName` by
	/// scanning that table's current live rows, and stores it keyed by
	/// (tableName, columnName) for LookupByIndex. Err if the table or
	/// column doesn't exist. Re-calling CreateIndex for the same
	/// (tableName, columnName) rebuilds it from scratch, replacing whatever
	/// was stored before.
	///
	/// SNAPSHOT semantics, deliberately not live-maintained: the set of
	/// (key -> row id) entries reflects `tableName`'s state AT THE TIME
	/// CreateIndex was called — a row InsertRow'd afterward is invisible to
	/// LookupByIndex even if its value matches, until CreateIndex is
	/// re-called to rebuild the index. Concretely, staleness only ever
	/// shows up as a false NEGATIVE (a real, live row silently missing from
	/// results), never as returning wrong/stale data for a row deleted
	/// since: LookupByIndex resolves each stored row id back through the
	/// table itself, and a row ExecuteDelete'd since CreateIndex resolves
	/// to NONE there (Table<Row>::Delete tombstones and discards the row's
	/// storage), so it's transparently dropped from the results rather than
	/// resurrected with stale values. This is a deliberate scope choice,
	/// not an oversight: keeping every created index live-synced on every
	/// subsequent mutation is real added complexity, and this snapshot
	/// design gets the safer failure mode (missing new data, never wrong
	/// data) without it.
	[[nodiscard]] Result<QueryResult, String> CreateIndex(const String &tableName, const String &columnName) {
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

	/// Resolves `key` to its matching rows via a previously-CreateIndex'd
	/// (tableName, columnName) index. Err if no such index was ever created
	/// (never silently falls back to a full scan — a caller opts into
	/// indexed lookup explicitly via CreateIndex). See CreateIndex's doc
	/// comment for the snapshot-freshness caveat this inherits.
	[[nodiscard]] Result<std::vector<Row>, String> LookupByIndex(const String &tableName, const String &columnName,
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

private:
	std::unordered_map<String, Schema> m_schemas;
	std::unordered_map<String, Table<Row>> m_tables;
	std::map<std::pair<String, String>, BTreeIndex> m_indexes;

	[[nodiscard]] Result<QueryResult, String> ExecuteCreateTable(const CreateTableStatement &stmt) {
		return CreateTable(stmt.tableName, stmt.schema);
	}

	/// Value/column-type compatibility, with one convenience coercion: an
	/// INT literal into a FLOAT column is promoted rather than rejected
	/// (writing "3" instead of "3.0" is a very common SQL ergonomics case).
	/// Any other mismatch (e.g. a string into an INT column) is a clear Err.
	[[nodiscard]] static Result<Row, String> CoerceInsertValues(const std::vector<data::NodePtr> &values,
																   const Schema &schema) {
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

	[[nodiscard]] Result<QueryResult, String> ExecuteInsert(const InsertStatement &stmt) {
		return InsertRow(stmt.tableName, stmt.values);
	}

	[[nodiscard]] Result<QueryResult, String> ExecuteSelect(const SelectStatement &stmt) {
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

	[[nodiscard]] Result<QueryResult, String> ExecuteDelete(const DeleteStatement &stmt) {
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

	[[nodiscard]] Result<QueryResult, String> ExecuteDropTable(const DropTableStatement &stmt) {
		auto schemaIt = m_schemas.find(stmt.tableName);
		if (schemaIt == m_schemas.end())
			return Err(String("sql: unknown table '") + stmt.tableName + "'");

		m_schemas.erase(schemaIt);
		m_tables.erase(stmt.tableName);

		QueryResult result;
		result.message = String("table '") + stmt.tableName + "' dropped";
		return Ok(result);
	}
};

} // namespace sql
