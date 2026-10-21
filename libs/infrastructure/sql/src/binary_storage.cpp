// Définitions de sql/binary_storage.hpp
#include <sql/binary_storage.hpp>

#include <array>
#include <filesystem>

namespace sql {

namespace detail {

void PushU32Le(std::vector<uint8_t> &buf, uint32_t v) {
	for (int i = 0; i < 4; ++i)
		buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void PushI64Le(std::vector<uint8_t> &buf, int64_t v) {
	uint64_t u;
	std::memcpy(&u, &v, sizeof(u));
	for (int i = 0; i < 8; ++i)
		buf.push_back(static_cast<uint8_t>((u >> (8 * i)) & 0xFF));
}

void PushF64Le(std::vector<uint8_t> &buf, double v) {
	uint64_t u;
	std::memcpy(&u, &v, sizeof(u));
	for (int i = 0; i < 8; ++i)
		buf.push_back(static_cast<uint8_t>((u >> (8 * i)) & 0xFF));
}

void PushString(std::vector<uint8_t> &buf, const String &s) {
	PushU32Le(buf, static_cast<uint32_t>(s.GetSize()));
	for (size_t i = 0; i < s.GetSize(); ++i)
		buf.push_back(static_cast<uint8_t>(s[i]));
}

uint8_t TypeToTag(dataio::NodeType type) {
	switch (type) {
		case dataio::NodeType::STRING:
			return 0;
		case dataio::NodeType::BOOL:
			return 1;
		case dataio::NodeType::INT:
			return 2;
		case dataio::NodeType::FLOAT:
			return 3;
		default:
			return 0xFF; // caught by the caller before this can escape into the buffer
	}
}

// ── Reader ───────────────────────────────────────────────────────────────────

Result<uint8_t, String> Reader::ReadU8() {
	if (pos + 1 > buf.size())
		return Err(String("sql: binary_storage: truncated buffer reading a byte"));
	return Ok(buf[pos++]);
}

Result<uint32_t, String> Reader::ReadU32Le() {
	if (pos + 4 > buf.size())
		return Err(String("sql: binary_storage: truncated buffer reading a u32"));
	uint32_t v = 0;
	for (int i = 0; i < 4; ++i)
		v |= static_cast<uint32_t>(buf[pos + static_cast<size_t>(i)]) << (8 * i);
	pos += 4;
	return Ok(v);
}

Result<int64_t, String> Reader::ReadI64Le() {
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

Result<double, String> Reader::ReadF64Le() {
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

Result<String, String> Reader::ReadString() {
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

Result<dataio::NodeType, String> Reader::ReadTypeTag() {
	auto tagR = ReadU8();
	if (tagR.IsError())
		return Err(tagR.Error());
	switch (tagR.Value()) {
		case 0:
			return Ok(dataio::NodeType::STRING);
		case 1:
			return Ok(dataio::NodeType::BOOL);
		case 2:
			return Ok(dataio::NodeType::INT);
		case 3:
			return Ok(dataio::NodeType::FLOAT);
		default:
			return Err(String::Format("sql: binary_storage: unrecognized column type tag %u",
									   static_cast<unsigned>(tagR.Value())));
	}
}

} // namespace detail

uint32_t detail::Crc32(const uint8_t *data, size_t size) noexcept {
	static const auto TABLE = [] {
		std::array<uint32_t, 256> t{};
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t c = i;
			for (int k = 0; k < 8; ++k)
				c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			t[i] = c;
		}
		return t;
	}();
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < size; ++i)
		crc = TABLE[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

bool SaveDatabaseBinary(const String &path, Database &db) {
	std::vector<uint8_t> buf;
	buf.reserve(256);

	buf.push_back('S');
	buf.push_back('Q');
	buf.push_back('L');
	buf.push_back('B');
	detail::PushU8(buf, BINARY_FORMAT_VERSION);

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
		detail::PushString(buf, schema.primaryKey); // v2: empty = no key

		detail::PushU32Le(buf, static_cast<uint32_t>(db.RowCount(tableName)));
		bool rowsOk = true;
		auto visited = db.ForEachRow(tableName, [&](const Row &row) {
			if (!rowsOk)
				return;
			if (row.size() != schema.columns.size()) {
				rowsOk = false;
				return;
			}
			for (size_t c = 0; c < row.size(); ++c) {
				const dataio::NodePtr &cell = row[c];
				switch (schema.columns[c].type) {
					case dataio::NodeType::INT:
						detail::PushI64Le(buf, cell->intValue);
						break;
					case dataio::NodeType::FLOAT:
						detail::PushF64Le(buf, cell->floatValue);
						break;
					case dataio::NodeType::BOOL:
						detail::PushU8(buf, cell->boolValue ? 1 : 0);
						break;
					case dataio::NodeType::STRING:
						detail::PushString(buf, cell->stringValue);
						break;
					default:
						rowsOk = false; // schema.hpp restricts columns to these 4 scalars
						return;
				}
			}
		});
		if (visited.IsError() || !rowsOk)
			return false;
	}

	// v2: CRC-32 of everything before it, so a torn or bit-rotted file is
	// reported instead of silently loading wrong data.
	detail::PushU32Le(buf, detail::Crc32(buf.data(), buf.size()));

	// Atomic replace: write a sibling temp file, then rename it over the
	// target — a crash mid-write leaves the previous file intact.
	const std::string target(path.CStr(), path.GetSize());
	const std::string temp = target + ".tmp";
	if (!sdl3::WriteFile(String(temp.c_str()), buf.data(), buf.size()))
		return false;
	std::error_code ec;
	std::filesystem::rename(temp, target, ec);
	if (ec) {
		std::filesystem::remove(temp, ec);
		return false;
	}
	return true;
}

Result<Database, String> LoadDatabaseBinary(const String &path) {
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
	const uint8_t version = versionR.Value();
	if (version < 1 || version > BINARY_FORMAT_VERSION)
		return Err(String::Format("sql: binary_storage: unsupported format version %u",
								   static_cast<unsigned>(version)));
	if (version >= 2) {
		if (buf.size() < 9)
			return Err(String("sql: binary_storage: truncated buffer (no checksum)"));
		const size_t body = buf.size() - 4;
		const uint32_t stored = uint32_t(buf[body]) | (uint32_t(buf[body + 1]) << 8) | (uint32_t(buf[body + 2]) << 16) |
								(uint32_t(buf[body + 3]) << 24);
		if (stored != detail::Crc32(buf.data(), body))
			return Err(String("sql: binary_storage: checksum mismatch (corrupt or truncated file)"));
	}

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
		if (version >= 2) {
			auto keyR = r.ReadString();
			if (keyR.IsError())
				return Err(keyR.Error());
			schema.primaryKey = keyR.Value();
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
					case dataio::NodeType::INT: {
						auto vR = r.ReadI64Le();
						if (vR.IsError())
							return Err(vR.Error());
						row.push_back(dataio::Node::MakeInt(vR.Value()));
						break;
					}
					case dataio::NodeType::FLOAT: {
						auto vR = r.ReadF64Le();
						if (vR.IsError())
							return Err(vR.Error());
						row.push_back(dataio::Node::MakeFloat(vR.Value()));
						break;
					}
					case dataio::NodeType::BOOL: {
						auto vR = r.ReadU8();
						if (vR.IsError())
							return Err(vR.Error());
						row.push_back(dataio::Node::MakeBool(vR.Value() != 0));
						break;
					}
					case dataio::NodeType::STRING: {
						auto vR = r.ReadString();
						if (vR.IsError())
							return Err(vR.Error());
						row.push_back(dataio::Node::MakeString(vR.Value()));
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
