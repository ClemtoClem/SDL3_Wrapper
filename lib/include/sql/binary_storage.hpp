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

void PushU32Le(std::vector<uint8_t> &buf, uint32_t v);

void PushI64Le(std::vector<uint8_t> &buf, int64_t v);

void PushF64Le(std::vector<uint8_t> &buf, double v);

void PushString(std::vector<uint8_t> &buf, const String &s);

/// STRING/BOOL/INT/FLOAT -> a 1-byte tag. Any other data::NodeType (a SQL
/// cell is never OBJECT/ARRAY/NONE) is a programmer error, not a runtime
/// Err — asserted rather than silently mis-tagged.
[[nodiscard]] uint8_t TypeToTag(data::NodeType type);

// ============================================================================
// Unpack helpers — a cursor position into an immutable byte buffer, Err on
// any read that would run past the end (truncated/corrupt buffer).
// ============================================================================

struct Reader {
	const std::vector<uint8_t> &buf;
	size_t pos = 0;

	[[nodiscard]] Result<uint8_t, String> ReadU8();

	[[nodiscard]] Result<uint32_t, String> ReadU32Le();

	[[nodiscard]] Result<int64_t, String> ReadI64Le();

	[[nodiscard]] Result<double, String> ReadF64Le();

	[[nodiscard]] Result<String, String> ReadString();

	[[nodiscard]] Result<data::NodeType, String> ReadTypeTag();
};

} // namespace detail

/// Serializes every table in `db` (schema + all live rows, via db.Execute's
/// public "SELECT * FROM <table>" — no need for new raw-iteration access)
/// into the binary format above and writes it to `path`. Returns false on
/// any I/O failure.
[[nodiscard]] bool SaveDatabaseBinary(const String &path, Database &db);

/// Reads `path` and reconstructs a fresh Database from the binary format
/// above, via Database::CreateTable/Database::InsertRow directly (see this
/// file's header doc comment for why — no SQL-text round-trip). Err with a
/// specific message on any I/O failure or format violation (truncated
/// buffer, bad magic bytes/version, unrecognized type tag) — never reads
/// out of bounds or silently produces a wrong Database.
[[nodiscard]] Result<Database, String> LoadDatabaseBinary(const String &path);

} // namespace sql
