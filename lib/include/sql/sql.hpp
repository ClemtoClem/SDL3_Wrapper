#pragma once
/**
 * sql:: — started as "NOT a SQL query engine" (a plain in-process
 * programmatic table store, no SQL text at all), then grew a second,
 * genuine half on top: a real SQL text parser and a fluent C++
 * query-builder "factory", both built entirely on the existing data::
 * module (no new external dependency). Three pieces now live here:
 *
 * 1. Project persistence (storage.hpp, half 1) — generic save/load of a
 *    data::Node tree to a JSON file (data::JsonDocument). Tree-shaped data.
 * 2. Metadata table store (table.hpp + query.hpp + storage.hpp, half 2) —
 *    sql::Table<Row>, a small in-process, row-shaped store with
 *    Insert/Get/Delete/Query only — no SQL syntax — for genuinely
 *    row-shaped data such as a recent-files list or an asset catalog.
 *    Persisted via data::CsvDocument.
 * 3. A real SQL pipeline on top of the same Table<Row> storage — no
 *    reimplementation of insert/delete/stable-id logic:
 *      - token.hpp — Tokenizer: hand-written lexer (no regex).
 *      - expression.hpp — shunting-yard -> RPN parsing and evaluation of
 *        WHERE-clause comparison/logical expressions.
 *      - schema.hpp — ColumnDef/Schema (the 4 data::NodeType scalars only).
 *      - statement.hpp — the parsed-statement AST (a std::variant).
 *      - parser.hpp — recursive-descent ParseStatement over token.hpp's
 *        stream, producing statement.hpp's AST.
 *      - database.hpp — Database: a named-table registry that executes
 *        CREATE TABLE / INSERT INTO / SELECT [WHERE ...] / DELETE
 *        [WHERE ...] / DROP TABLE, either from SQL text (Execute) or an
 *        already-built SelectStatement (ExecuteSelectStatement).
 *      - query_builder.hpp — QueryBuilder: a fluent C++ "factory/builder"
 *        alternative to writing SQL text, reusing Database's own
 *        WHERE-evaluation logic rather than duplicating it.
 * 4. Binary file storage + B+Tree indexing — the two pieces the reference
 *    project's own tokenizer/parser/shunting-yard/B+Tree/binary-storage
 *    pipeline modeled a genuine DBMS around, layered on top of the same
 *    in-memory Table<Row> core once it was verified:
 *      - btree_index.hpp — BTreeIndex: a real, self-contained in-memory
 *        B+Tree keyed by a scalar data::NodePtr (reusing expression.hpp's
 *        Compare3Way rather than re-deriving comparison logic), with
 *        node-splitting insert AND a complete borrow/merge-rebalancing
 *        Remove — the reference project's own README admits its remove
 *        was "prepared but lacking disk management"; this one doesn't
 *        leave that unfinished.
 *      - binary_storage.hpp — SaveDatabaseBinary/LoadDatabaseBinary: a
 *        custom binary on-disk format (magic + version, then per-table
 *        schema + type-tagged rows) for a whole Database, distinct from
 *        storage.hpp's JSON/CSV *text* persistence. Loading reconstructs
 *        tables via Database::CreateTable/InsertRow directly (typed data
 *        in, no SQL-text round-trip — see that file's doc comment for why).
 *      - database.hpp's CreateIndex/LookupByIndex wire a BTreeIndex on top
 *        of a table's existing storage as an optional secondary index, per
 *        (table, column). It's a SNAPSHOT built at CreateIndex-call time,
 *        not live-maintained against later InsertRow/ExecuteDelete calls —
 *        a deliberate, documented scope choice (see CreateIndex's own doc
 *        comment). Staleness only ever shows up as a false negative (a row
 *        inserted after CreateIndex is invisible to LookupByIndex until
 *        it's re-called): a row deleted since is transparently dropped
 *        from results rather than resurrected with stale data, since
 *        LookupByIndex resolves every stored row id back through the live
 *        table itself.
 *    No disk-backed/memory-mapped B+Tree (the reference project's own
 *    admitted limitation) — the tree lives in memory, built by scanning
 *    Database's in-memory rows; binary_storage.hpp's file format is a
 *    separate, simpler flat serialization of the tables themselves, not of
 *    any index structure. Table<Row>/Database also never switch to using
 *    BTreeIndex as their underlying storage — it stays a purely optional
 *    secondary index a caller builds on top via CreateIndex.
 *
 * Aucune exception : toute opération pouvant échouer retourne Option<T> ou
 * Result<T,E> (cf. core/option.hpp / core/result.hpp), comme dans data::.
 * Les chaînes utilisent String (cf. core/string.hpp).
 */
#include "table.hpp"
#include "query.hpp"
#include "storage.hpp"
#include "token.hpp"
#include "expression.hpp"
#include "schema.hpp"
#include "statement.hpp"
#include "parser.hpp"
#include "btree_index.hpp"
#include "database.hpp"
#include "binary_storage.hpp"
#include "query_builder.hpp"
