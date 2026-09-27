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
	[[nodiscard]] Result<QueryResult, String> Execute(const String &sql);

	/// Executes an already-built SelectStatement directly, bypassing the
	/// tokenizer/parser — used by query_builder.hpp's fluent QueryBuilder,
	/// which already has structured data. Reuses the exact same per-row /
	/// WHERE-evaluation logic as Execute(String)'s SELECT path.
	[[nodiscard]] Result<QueryResult, String> ExecuteSelectStatement(const SelectStatement &stmt);

	/// The reference project's "SHOW TABLES" equivalent, as a plain C++
	/// method rather than another parsed statement kind.
	[[nodiscard]] std::vector<String> TableNames() const;

	[[nodiscard]] Option<Ref<Schema>> GetSchema(const String &tableName) const;

	/// Registers a new table named `tableName` with `schema`. Err if a table
	/// with that name already exists. Extracted out of ExecuteCreateTable so
	/// binary_storage.hpp's LoadDatabaseBinary can rebuild a table directly
	/// from an already-typed Schema without round-tripping through SQL text
	/// (a CREATE TABLE statement string).
	[[nodiscard]] Result<QueryResult, String> CreateTable(const String &tableName, Schema schema);

	/// Validates `values` against `tableName`'s schema (unknown table, wrong
	/// value count, INT->FLOAT coercion via CoerceInsertValues) and appends
	/// them as a new row. Extracted out of ExecuteInsert so
	/// binary_storage.hpp's LoadDatabaseBinary can insert already-typed Row
	/// data directly — deliberately NOT by generating and re-parsing SQL
	/// text, since a STRING cell containing a literal ' would need
	/// re-escaping to survive a text round-trip, needless risk for
	/// structured data that was never user-typed SQL in the first place.
	[[nodiscard]] Result<QueryResult, String> InsertRow(const String &tableName, Row values);

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
	[[nodiscard]] Result<QueryResult, String> CreateIndex(const String &tableName, const String &columnName);

	/// Resolves `key` to its matching rows via a previously-CreateIndex'd
	/// (tableName, columnName) index. Err if no such index was ever created
	/// (never silently falls back to a full scan — a caller opts into
	/// indexed lookup explicitly via CreateIndex). See CreateIndex's doc
	/// comment for the snapshot-freshness caveat this inherits.
	[[nodiscard]] Result<std::vector<Row>, String> LookupByIndex(const String &tableName, const String &columnName,
																   const data::NodePtr &key) const;

private:
	std::unordered_map<String, Schema> m_schemas;
	std::unordered_map<String, Table<Row>> m_tables;
	std::map<std::pair<String, String>, BTreeIndex> m_indexes;

	[[nodiscard]] Result<QueryResult, String> ExecuteCreateTable(const CreateTableStatement &stmt);

	/// Value/column-type compatibility, with one convenience coercion: an
	/// INT literal into a FLOAT column is promoted rather than rejected
	/// (writing "3" instead of "3.0" is a very common SQL ergonomics case).
	/// Any other mismatch (e.g. a string into an INT column) is a clear Err.
	[[nodiscard]] static Result<Row, String> CoerceInsertValues(const std::vector<data::NodePtr> &values,
																   const Schema &schema);

	[[nodiscard]] Result<QueryResult, String> ExecuteInsert(const InsertStatement &stmt);

	[[nodiscard]] Result<QueryResult, String> ExecuteSelect(const SelectStatement &stmt);

	[[nodiscard]] Result<QueryResult, String> ExecuteDelete(const DeleteStatement &stmt);

	[[nodiscard]] Result<QueryResult, String> ExecuteDropTable(const DropTableStatement &stmt);
};

} // namespace sql
