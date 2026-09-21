#pragma once
/**
 * sql:: binary database persistence — a custom binary on-disk format for a
 * whole Database (this is what makes it "une base de données binaire" per
 * the module's original request, distinct from storage.hpp's existing
 * JSON/CSV *text* persistence).
 *
 * Format (all multi-byte integers little-endian):
 *   magic bytes "SQLB" (4 bytes) + version byte (1 byte, currently 1)
 *   table count (u32)
 *   per table:
 *     table name (u32 length-prefix + UTF8 bytes)
 *     column count (u32)
 *     per column:
 *       column name (u32 length-prefix + UTF8 bytes)
 *       type tag (1 byte: 0=STRING, 1=BOOL, 2=INT, 3=FLOAT)
 *     row count (u32)
 *     per row:
 *       per column, type-tagged per that column's schema type:
 *         INT   -> 8 bytes little-endian int64
 *         FLOAT -> 8 bytes little-endian double bit-pattern
 *         BOOL  -> 1 byte (0/1)
 *         STRING -> u32 length-prefix + UTF8 bytes
 *
 * The whole buffer is built in memory (std::vector<uint8_t>) with small
 * local pack/unpack helpers, then written/read as one blob via
 * sdl3::WriteFile/sdl3::ReadFile (lib/include/sdl3/iostream.hpp) — the same
 * idiom storage.hpp's SaveProjectJson/LoadProjectJson already use. Every
 * Read* helper is bounds-checked against the buffer and a cursor position;
 * a truncated or corrupt buffer produces an Err, never an out-of-bounds
 * read or a silently-wrong Database.
 *
 * LoadDatabaseBinary reconstructs tables via Database::CreateTable /
 * Database::InsertRow (database.hpp, Part 1's extraction) directly from
 * already-typed values — deliberately NOT by generating and re-parsing SQL
 * text, since a STRING cell containing a literal ' would need re-escaping
 * to survive a text round-trip. Row ids are not required to round-trip
 * (matches storage.hpp's LoadTableCsv precedent) — a freshly-loaded table
 * reassigns fresh sequential ids.
 */
#include "../core/core.hpp"
#include "../data/node.hpp"
#include "../sdl3/iostream.hpp"
#include "database.hpp"
#include "schema.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace sql {

namespace detail {

// ============================================================================
// Pack helpers — append to a growing byte buffer.
// ============================================================================

inline void PushU8(std::vector<uint8_t> &buf, uint8_t v) { buf.push_back(v); }

inline void PushU32Le(std::vector<uint8_t> &buf, uint32_t v) {
	for (int i = 0; i < 4; ++i)
		buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

inline void PushI64Le(std::vector<uint8_t> &buf, int64_t v) {
	uint64_t u;
	std::memcpy(&u, &v, sizeof(u));
	for (int i = 0; i < 8; ++i)
		buf.push_back(static_cast<uint8_t>((u >> (8 * i)) & 0xFF));
}

inline void PushF64Le(std::vector<uint8_t> &buf, double v) {
	uint64_t u;
	std::memcpy(&u, &v, sizeof(u));
	for (int i = 0; i < 8; ++i)
		buf.push_back(static_cast<uint8_t>((u >> (8 * i)) & 0xFF));
}

inline void PushString(std::vector<uint8_t> &buf, const String &s) {
	PushU32Le(buf, static_cast<uint32_t>(s.GetSize()));
	for (size_t i = 0; i < s.GetSize(); ++i)
		buf.push_back(static_cast<uint8_t>(s[i]));
}

/// STRING/BOOL/INT/FLOAT -> a 1-byte tag. Any other data::NodeType (a SQL
/// cell is never OBJECT/ARRAY/NONE) is a programmer error, not a runtime
/// Err — asserted rather than silently mis-tagged.
[[nodiscard]] inline uint8_t TypeToTag(data::NodeType type) {
	switch (type) {
		case data::NodeType::STRING:
			return 0;
		case data::NodeType::BOOL:
			return 1;
		case data::NodeType::INT:
			return 2;
		case data::NodeType::FLOAT:
			return 3;
		default:
			return 0xFF; // caught by the caller before this can escape into the buffer
	}
}

// ============================================================================
// Unpack helpers — a cursor position into an immutable byte buffer, Err on
// any read that would run past the end (truncated/corrupt buffer).
// ============================================================================

struct Reader {
	const std::vector<uint8_t> &buf;
	size_t pos = 0;

	[[nodiscard]] Result<uint8_t, String> ReadU8() {
		if (pos + 1 > buf.size())
			return Err(String("sql: binary_storage: truncated buffer reading a byte"));
		return Ok(buf[pos++]);
	}

	[[nodiscard]] Result<uint32_t, String> ReadU32Le() {
		if (pos + 4 > buf.size())
			return Err(String("sql: binary_storage: truncated buffer reading a u32"));
		uint32_t v = 0;
		for (int i = 0; i < 4; ++i)
			v |= static_cast<uint32_t>(buf[pos + static_cast<size_t>(i)]) << (8 * i);
		pos += 4;
		return Ok(v);
	}

	[[nodiscard]] Result<int64_t, String> ReadI64Le() {
		if (pos + 8 > buf.size())
			return Err(String("sql: binary_storage: truncated buffer reading an i64"));
		uint64_t u = 0;
		for (int i = 0; i < 8; ++i)
			u |= static_cast<uint64_t>(buf[pos + static_cast<size_t>(i)]) << (8 * i);
		pos += 8;
		int64_t v;
		std::memcpy(&v, &u, sizeof(v));
		return Ok(v);
	}

	[[nodiscard]] Result<double, String> ReadF64Le() {
		if (pos + 8 > buf.size())
			return Err(String("sql: binary_storage: truncated buffer reading an f64"));
		uint64_t u = 0;
		for (int i = 0; i < 8; ++i)
			u |= static_cast<uint64_t>(buf[pos + static_cast<size_t>(i)]) << (8 * i);
		pos += 8;
		double v;
		std::memcpy(&v, &u, sizeof(v));
		return Ok(v);
	}

	[[nodiscard]] Result<String, String> ReadString() {
		auto lenR = ReadU32Le();
		if (lenR.IsError())
			return Err(lenR.Error());
		size_t len = lenR.Value();
		if (pos + len > buf.size())
			return Err(String("sql: binary_storage: truncated buffer reading a string"));
		String s(reinterpret_cast<const char *>(buf.data() + pos), len);
		pos += len;
		return Ok(std::move(s));
	}

	[[nodiscard]] Result<data::NodeType, String> ReadTypeTag() {
		auto tagR = ReadU8();
		if (tagR.IsError())
			return Err(tagR.Error());
		switch (tagR.Value()) {
			case 0:
				return Ok(data::NodeType::STRING);
			case 1:
				return Ok(data::NodeType::BOOL);
			case 2:
				return Ok(data::NodeType::INT);
			case 3:
				return Ok(data::NodeType::FLOAT);
			default:
				return Err(String::Format("sql: binary_storage: unrecognized column type tag %u",
										   static_cast<unsigned>(tagR.Value())));
		}
	}
};

} // namespace detail

/// Serializes every table in `db` (schema + all live rows, via db.Execute's
/// public "SELECT * FROM <table>" — no need for new raw-iteration access)
/// into the binary format above and writes it to `path`. Returns false on
/// any I/O failure.
[[nodiscard]] inline bool SaveDatabaseBinary(const String &path, Database &db) {
	std::vector<uint8_t> buf;
	buf.reserve(256);

	buf.push_back('S');
	buf.push_back('Q');
	buf.push_back('L');
	buf.push_back('B');
	detail::PushU8(buf, 1); // version

	std::vector<String> tableNames = db.TableNames();
	detail::PushU32Le(buf, static_cast<uint32_t>(tableNames.size()));

	for (const String &tableName : tableNames) {
		auto schemaOpt = db.GetSchema(tableName);
		if (schemaOpt.IsNone())
			return false; // TableNames()/GetSchema() disagreeing would be an internal inconsistency
		const Schema &schema = *schemaOpt.Value();

		detail::PushString(buf, tableName);
		detail::PushU32Le(buf, static_cast<uint32_t>(schema.columns.size()));
		for (const ColumnDef &col : schema.columns) {
			detail::PushString(buf, col.name);
			uint8_t tag = detail::TypeToTag(col.type);
			if (tag == 0xFF)
				return false; // a SQL column can only ever be one of the 4 scalar types
			detail::PushU8(buf, tag);
		}

		auto selectR = db.Execute(String("SELECT * FROM ") + tableName);
		if (selectR.IsError())
			return false;
		const std::vector<Row> &rows = selectR.Value().rows;

		detail::PushU32Le(buf, static_cast<uint32_t>(rows.size()));
		for (const Row &row : rows) {
			if (row.size() != schema.columns.size())
				return false; // SELECT * always matches schema column order/count
			for (size_t c = 0; c < row.size(); ++c) {
				const data::NodePtr &cell = row[c];
				switch (schema.columns[c].type) {
					case data::NodeType::INT:
						detail::PushI64Le(buf, cell->intValue);
						break;
					case data::NodeType::FLOAT:
						detail::PushF64Le(buf, cell->floatValue);
						break;
					case data::NodeType::BOOL:
						detail::PushU8(buf, cell->boolValue ? 1 : 0);
						break;
					case data::NodeType::STRING:
						detail::PushString(buf, cell->stringValue);
						break;
					default:
						return false; // schema.hpp restricts columns to these 4 scalars
				}
			}
		}
	}

	return sdl3::WriteFile(path, buf.data(), buf.size());
}

/// Reads `path` and reconstructs a fresh Database from the binary format
/// above, via Database::CreateTable/Database::InsertRow directly (see this
/// file's header doc comment for why — no SQL-text round-trip). Err with a
/// specific message on any I/O failure or format violation (truncated
/// buffer, bad magic bytes/version, unrecognized type tag) — never reads
/// out of bounds or silently produces a wrong Database.
[[nodiscard]] inline Result<Database, String> LoadDatabaseBinary(const String &path) {
	auto bytesR = sdl3::ReadFile(path);
	if (!bytesR)
		return Err(String(bytesR.Error()));
	const std::vector<uint8_t> &buf = bytesR.Value();

	detail::Reader r{buf, 0};

	if (buf.size() < 5)
		return Err(String("sql: binary_storage: buffer too small to contain a header"));
	if (buf[0] != 'S' || buf[1] != 'Q' || buf[2] != 'L' || buf[3] != 'B')
		return Err(String("sql: binary_storage: bad magic bytes (not a sql:: binary database file)"));
	r.pos = 4;

	auto versionR = r.ReadU8();
	if (versionR.IsError())
		return Err(versionR.Error());
	if (versionR.Value() != 1)
		return Err(String::Format("sql: binary_storage: unsupported format version %u",
								   static_cast<unsigned>(versionR.Value())));

	auto tableCountR = r.ReadU32Le();
	if (tableCountR.IsError())
		return Err(tableCountR.Error());

	Database db;

	for (uint32_t t = 0; t < tableCountR.Value(); ++t) {
		auto nameR = r.ReadString();
		if (nameR.IsError())
			return Err(nameR.Error());

		auto colCountR = r.ReadU32Le();
		if (colCountR.IsError())
			return Err(colCountR.Error());

		Schema schema;
		schema.columns.reserve(colCountR.Value());
		for (uint32_t c = 0; c < colCountR.Value(); ++c) {
			auto colNameR = r.ReadString();
			if (colNameR.IsError())
				return Err(colNameR.Error());
			auto typeR = r.ReadTypeTag();
			if (typeR.IsError())
				return Err(typeR.Error());
			schema.columns.push_back(ColumnDef{colNameR.Value(), typeR.Value()});
		}

		auto createR = db.CreateTable(nameR.Value(), schema);
		if (createR.IsError())
			return Err(createR.Error());

		auto rowCountR = r.ReadU32Le();
		if (rowCountR.IsError())
			return Err(rowCountR.Error());

		for (uint32_t rowIdx = 0; rowIdx < rowCountR.Value(); ++rowIdx) {
			Row row;
			row.reserve(schema.columns.size());
			for (const ColumnDef &col : schema.columns) {
				switch (col.type) {
					case data::NodeType::INT: {
						auto vR = r.ReadI64Le();
						if (vR.IsError())
							return Err(vR.Error());
						row.push_back(data::Node::MakeInt(vR.Value()));
						break;
					}
					case data::NodeType::FLOAT: {
						auto vR = r.ReadF64Le();
						if (vR.IsError())
							return Err(vR.Error());
						row.push_back(data::Node::MakeFloat(vR.Value()));
						break;
					}
					case data::NodeType::BOOL: {
						auto vR = r.ReadU8();
						if (vR.IsError())
							return Err(vR.Error());
						row.push_back(data::Node::MakeBool(vR.Value() != 0));
						break;
					}
					case data::NodeType::STRING: {
						auto vR = r.ReadString();
						if (vR.IsError())
							return Err(vR.Error());
						row.push_back(data::Node::MakeString(vR.Value()));
						break;
					}
					default:
						return Err(String("sql: binary_storage: schema column has a non-scalar type"));
				}
			}

			auto insertR = db.InsertRow(nameR.Value(), std::move(row));
			if (insertR.IsError())
				return Err(insertR.Error());
		}
	}

	return Ok(std::move(db));
}

} // namespace sql
