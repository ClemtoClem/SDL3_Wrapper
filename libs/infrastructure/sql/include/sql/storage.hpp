#pragma once
/**
 * sql:: persistence — two independent halves.
 *
 * Half 1 (generic, tree-shaped project files): SaveProjectJson/LoadProjectJson
 * are thin wrappers around dataio::JsonDocument — they save/load an arbitrary
 * dataio::Node tree as-is, no Table<Row>/CSV involvement.
 *
 * Half 2 (row-shaped metadata store): SaveTableCsv/LoadTableCsv persist a
 * Table<Row> via dataio::CsvDocument. Table<Row> knows nothing about Row's
 * field layout (see table.hpp), so the caller supplies explicit conversion
 * functions — same customization-point pattern as dataio::DocumentFactory's
 * Creator (document.hpp).
 *
 * IMPORTANT — CSV's Node root shape is COLUMNAR, not row-shaped (see
 * data/csv.hpp's own doc comment): the root is an Object whose every value
 * is an Array — one named column. Row N's value for column C is
 * `root->Get(C)->At(N)`, the opposite of the more intuitive "Array of
 * row-Objects" shape. Both functions below build/read that exact shape.
 */
#include <dataio/documents/csv.hpp>
#include <dataio/document.hpp>
#include <dataio/documents/json.hpp>
#include <sdl3/iostream.hpp>
#include "table.hpp"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace sql {

// ============================================================================
// Half 1 — generic project (tree-shaped) persistence
// ============================================================================

/// Encodes `root` as JSON and writes it to `path`. `root` is used as-is (not
/// cloned) as the JsonDocument's root — the caller keeps ownership via the
/// shared_ptr.
[[nodiscard]] bool SaveProjectJson(const String &path, const dataio::NodePtr &root);

/// Reads `path` and decodes it as JSON. Err on I/O failure or a JSON parse
/// error (formatted via dataio::ParseError::Format()).
[[nodiscard]] Result<dataio::NodePtr, String> LoadProjectJson(const String &path);

// ============================================================================
// Half 2 — Table<Row> CSV persistence
// ============================================================================

/// Writes every live row of `table` to `path` as CSV, one named column per
/// entry of `columns`. `getField(row, column)` supplies the cell value for
/// each (row, column) pair — Table<Row>/this function never need to know
/// Row's actual field layout.
template <typename Row>
[[nodiscard]] bool SaveTableCsv(const String &path, const Table<Row> &table, const std::vector<String> &columns,
								 const std::function<dataio::NodePtr(const Row &, const String &)> &getField) {
	auto root = dataio::Node::MakeObject();
	for (auto &col : columns)
		root->Set(col, dataio::Node::MakeArray());

	table.ForEachLive([&](size_t /*id*/, const Row &row) {
		for (auto &col : columns)
			root->Get(col)->Push(getField(row, col));
	});

	dataio::CsvDocument doc;
	doc.SetRoot(root);
	String text = doc.EncodeStr();
	return sdl3::WriteFile(path, text.CStr(), text.GetSize());
}

/// Reads `path` as CSV and rebuilds a fresh Table<Row>. `makeRow(cells)` is
/// called once per CSV data row with that row's column->cell pairs (in
/// `columns` order) and returns Some(row) to insert it, or NONE to skip that
/// row. Ids are NOT preserved from any earlier save — the returned table
/// assigns fresh sequential ids starting from 0, matching how a metadata
/// store (e.g. a recent-files list) is actually used.
template <typename Row>
[[nodiscard]] Result<Table<Row>, String> LoadTableCsv(
	const String &path, const std::vector<String> &columns,
	const std::function<Option<Row>(const std::vector<std::pair<String, dataio::NodePtr>> &)> &makeRow) {
	auto bytes = sdl3::ReadFile(path);
	if (!bytes)
		return Err(String(bytes.Error()));

	String content(reinterpret_cast<const char *>(bytes.Value().data()), bytes.Value().size());

	dataio::CsvDocument doc;
	auto err = doc.DecodeStr(content);
	if (err.IsSome())
		return Err(err->Format());

	Table<Row> table;
	auto root = doc.GetRoot();
	if (!root)
		return Ok(std::move(table));

	size_t rowCount = 0;
	for (auto &col : columns) {
		if (auto colNode = root->Get(col))
			rowCount = std::max(rowCount, colNode->GetSize());
	}

	for (size_t r = 0; r < rowCount; ++r) {
		std::vector<std::pair<String, dataio::NodePtr>> cells;
		cells.reserve(columns.size());
		for (auto &col : columns) {
			auto colNode = root->Get(col);
			dataio::NodePtr cell = (colNode && r < colNode->GetSize()) ? colNode->At(r) : nullptr;
			cells.emplace_back(col, cell);
		}
		auto row = makeRow(cells);
		if (row.IsSome())
			table.Insert(std::move(*row));
	}

	return Ok(std::move(table));
}

} // namespace sql
