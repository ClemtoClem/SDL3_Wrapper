// Smoke test : PRIMARY KEY tables (live keyed index, Upsert / FindByKey /
// DeleteByKey / ForEachRow) and the v2 binary format (key persisted, CRC-32,
// atomic write, v1 compatibility). CPU-only.
#include "test.hpp"
#include <sdl3/filesystem.hpp>
#include <sql/sql.hpp>

#include <filesystem>
#include <vector>

using dataio::Node;

static sql::Row KV(const char *key, int64_t value) { return {Node::MakeString(key), Node::MakeInt(value)}; }

TEST(SqlKeys, PrimaryKeyIsParsedAndRejectsDuplicates) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE kv (k STRING PRIMARY KEY, v INT)").IsOk());
	auto schema = db.GetSchema("kv");
	ASSERT_TRUE(schema.IsSome());
	EXPECT_TRUE(schema.Value()->primaryKey == "k");

	ASSERT_TRUE(db.Execute("INSERT INTO kv VALUES ('a', 1)").IsOk());
	EXPECT_TRUE(db.Execute("INSERT INTO kv VALUES ('a', 2)").IsError());
	EXPECT_EQ(db.RowCount("kv"), 1u);

	EXPECT_TRUE(db.Execute("CREATE TABLE bad (a INT PRIMARY KEY, b INT PRIMARY KEY)").IsError());
	// "key" stays usable as an ordinary column name.
	EXPECT_TRUE(db.Execute("CREATE TABLE plain (key STRING, value INT)").IsOk());
}

TEST(SqlKeys, UpsertFindAndDeleteByKey) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE kv (k STRING PRIMARY KEY, v INT)").IsOk());
	ASSERT_TRUE(db.Upsert("kv", KV("a", 1)).IsOk());
	ASSERT_TRUE(db.Upsert("kv", KV("b", 2)).IsOk());
	ASSERT_TRUE(db.Upsert("kv", KV("a", 10)).IsOk()); // replaced in place
	EXPECT_EQ(db.RowCount("kv"), 2u);

	auto a = db.FindByKey("kv", Node::MakeString("a"));
	ASSERT_TRUE(a.IsOk() && a.Value().IsSome());
	EXPECT_EQ(a.Value().Unwrap()[1]->intValue, 10);
	EXPECT_TRUE(db.FindByKey("kv", Node::MakeString("zzz")).Value().IsNone());

	auto removed = db.DeleteByKey("kv", Node::MakeString("a"));
	ASSERT_TRUE(removed.IsOk());
	EXPECT_TRUE(removed.Value());
	EXPECT_FALSE(db.DeleteByKey("kv", Node::MakeString("a")).Value());
	EXPECT_TRUE(db.FindByKey("kv", Node::MakeString("a")).Value().IsNone());

	// SQL DELETE keeps the key index in sync: the key can be inserted again.
	ASSERT_TRUE(db.Execute("DELETE FROM kv WHERE k = 'b'").IsOk());
	EXPECT_TRUE(db.FindByKey("kv", Node::MakeString("b")).Value().IsNone());
	EXPECT_TRUE(db.Execute("INSERT INTO kv VALUES ('b', 3)").IsOk());

	// Keyed calls on a table without a key are errors, not full scans.
	ASSERT_TRUE(db.Execute("CREATE TABLE plain (a INT)").IsOk());
	EXPECT_TRUE(db.FindByKey("plain", Node::MakeInt(1)).IsError());
	EXPECT_TRUE(db.Upsert("unknown", KV("x", 1)).IsError());
}

TEST(SqlKeys, ForEachRowVisitsLiveRowsOnly) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE kv (k STRING PRIMARY KEY, v INT)").IsOk());
	for (int i = 0; i < 5; ++i)
		ASSERT_TRUE(db.Upsert("kv", KV(String::Format("k%d", i).CStr(), i)).IsOk());
	ASSERT_TRUE(db.DeleteByKey("kv", Node::MakeString("k2")).IsOk());
	int64_t sum = 0;
	size_t n = 0;
	ASSERT_TRUE(db.ForEachRow("kv", [&](const sql::Row &row) {
		sum += row[1]->intValue;
		++n;
	}).IsOk());
	EXPECT_EQ(n, 4u);
	EXPECT_EQ(sum, 0 + 1 + 3 + 4);
}

TEST(SqlKeys, BinaryRoundTripKeepsKeyAndRawBytes) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE blobs (id INT PRIMARY KEY, data STRING)").IsOk());
	const char raw[] = {'\0', '\x01', '\xff', '\0', 'z'};
	ASSERT_TRUE(db.Upsert("blobs", {Node::MakeInt(7), Node::MakeString(String(raw, sizeof(raw)))}).IsOk());

	String path = "/tmp/sql_keys_roundtrip.bin";
	ASSERT_TRUE(sql::SaveDatabaseBinary(path, db));
	auto loaded = sql::LoadDatabaseBinary(path);
	ASSERT_TRUE(loaded.IsOk());
	EXPECT_TRUE(loaded.Value().GetSchema("blobs").Value()->primaryKey == "id");
	auto row = loaded.Value().FindByKey("blobs", Node::MakeInt(7));
	ASSERT_TRUE(row.IsOk() && row.Value().IsSome());
	const String &data = row.Value().Unwrap()[1]->stringValue;
	ASSERT_EQ(data.GetSize(), sizeof(raw));
	for (size_t i = 0; i < sizeof(raw); ++i)
		EXPECT_EQ(data[i], raw[i]);
	// No temp file left behind by the atomic write.
	EXPECT_FALSE(std::filesystem::exists("/tmp/sql_keys_roundtrip.bin.tmp"));
	sdl3::filesystem::Remove(path);
}

TEST(SqlKeys, ChecksumDetectsCorruption) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE t (a INT)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO t VALUES (42)").IsOk());
	String path = "/tmp/sql_keys_corrupt.bin";
	ASSERT_TRUE(sql::SaveDatabaseBinary(path, db));
	auto bytes = sdl3::ReadFile(path);
	ASSERT_TRUE(bytes.IsOk());
	std::vector<uint8_t> data = bytes.Value();
	data[data.size() - 6] ^= 0x10; // flip one bit of the stored value
	ASSERT_TRUE(sdl3::WriteFile(path, data.data(), data.size()));
	auto loaded = sql::LoadDatabaseBinary(path);
	ASSERT_TRUE(loaded.IsError());
	sdl3::filesystem::Remove(path);
}

TEST(SqlKeys, VersionOneFilesStillLoad) {
	// "SQLB", version 1, one table "t" with one INT column "a", one row 5.
	std::vector<uint8_t> v1 = {'S', 'Q', 'L', 'B', 1, 1, 0, 0, 0, 1, 0, 0, 0, 't', 1, 0, 0, 0, 1, 0, 0, 0, 'a', 2,
							   1, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0};
	String path = "/tmp/sql_keys_v1.bin";
	ASSERT_TRUE(sdl3::WriteFile(path, v1.data(), v1.size()));
	auto loaded = sql::LoadDatabaseBinary(path);
	ASSERT_TRUE(loaded.IsOk());
	EXPECT_EQ(loaded.Value().RowCount("t"), 1u);
	EXPECT_TRUE(loaded.Value().GetSchema("t").Value()->primaryKey.IsEmpty());
	sdl3::filesystem::Remove(path);
}
