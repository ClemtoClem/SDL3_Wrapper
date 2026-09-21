// Smoke test : sql:: binary Database persistence (lib/include/sql/
// binary_storage.hpp) + sql::Database::CreateIndex/LookupByIndex
// (lib/include/sql/database.hpp, wired on top of btree_index.hpp). CPU-only,
// no GPU/window involved.
#define USE_TEST
#include "core/test.hpp"
#include "sdl3/filesystem.hpp"
#include "sql/sql.hpp"

#include <algorithm>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────
// Helper: exact field-by-field row equality (not just "same row count") —
// two rows match if every cell has the same type and the same value.
// ─────────────────────────────────────────────────────────────────────────
static bool CellsEqual(const data::NodePtr &a, const data::NodePtr &b) {
	if (!a || !b)
		return a == b;
	if (a->type != b->type)
		return false;
	switch (a->type) {
		case data::NodeType::STRING:
			return a->stringValue == b->stringValue;
		case data::NodeType::BOOL:
			return a->boolValue == b->boolValue;
		case data::NodeType::INT:
			return a->intValue == b->intValue;
		case data::NodeType::FLOAT:
			return a->floatValue == b->floatValue;
		default:
			return false;
	}
}

static bool RowsEqual(const sql::Row &a, const sql::Row &b) {
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (!CellsEqual(a[i], b[i]))
			return false;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────
// 1. Full round-trip: two tables, a mix of column types (INT/FLOAT/STRING/
//    BOOL), several rows — including a STRING value containing a literal
//    single-quote character, the exact case Part 1's Database::CreateTable/
//    InsertRow extraction exists to handle correctly without needing SQL-
//    text re-escaping. Every table's schema and every row's every value
//    must match the original exactly after Save->Load.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlBinaryStorage, RoundTripPreservesSchemaAndEveryRowValueExactly) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE users (id INT, name STRING, active BOOL, rating FLOAT)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (1, 'Alice', true, 4.5)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (2, 'O''Brien', false, 2.25)").IsOk()); // literal ' via '' escape
	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (3, 'Carol', true, -1.0)").IsOk());

	ASSERT_TRUE(db.Execute("CREATE TABLE tags (label STRING, weight INT)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO tags VALUES ('vip', 10)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO tags VALUES ('flagged', 0)").IsOk());

	// Confirm the quote actually made it into the in-memory row before we
	// even touch the binary format — the round-trip claim below is only
	// meaningful if this string genuinely contains a raw '.
	auto preCheck = db.Execute("SELECT name FROM users WHERE id = 2");
	ASSERT_TRUE(preCheck.IsOk());
	ASSERT_EQ(preCheck.Value().rows.size(), 1u);
	EXPECT_TRUE(preCheck.Value().rows[0][0]->stringValue == "O'Brien");

	String path = "/tmp/sql_binary_storage_smoke_test.bin";
	ASSERT_TRUE(sql::SaveDatabaseBinary(path, db));

	auto loadedR = sql::LoadDatabaseBinary(path);
	ASSERT_TRUE(loadedR.IsOk());
	sql::Database &loaded = loadedR.Value();

	auto names = loaded.TableNames();
	std::sort(names.begin(), names.end());
	ASSERT_EQ(names.size(), 2u);
	EXPECT_TRUE(names[0] == "tags");
	EXPECT_TRUE(names[1] == "users");

	// Schema match, column-by-column, for both tables.
	auto usersSchemaOpt = loaded.GetSchema("users");
	ASSERT_TRUE(usersSchemaOpt.IsSome());
	const sql::Schema &usersSchema = *usersSchemaOpt.Value();
	ASSERT_EQ(usersSchema.columns.size(), 4u);
	EXPECT_TRUE(usersSchema.columns[0].name == "id");
	EXPECT_TRUE(usersSchema.columns[0].type == data::NodeType::INT);
	EXPECT_TRUE(usersSchema.columns[1].name == "name");
	EXPECT_TRUE(usersSchema.columns[1].type == data::NodeType::STRING);
	EXPECT_TRUE(usersSchema.columns[2].name == "active");
	EXPECT_TRUE(usersSchema.columns[2].type == data::NodeType::BOOL);
	EXPECT_TRUE(usersSchema.columns[3].name == "rating");
	EXPECT_TRUE(usersSchema.columns[3].type == data::NodeType::FLOAT);

	auto tagsSchemaOpt = loaded.GetSchema("tags");
	ASSERT_TRUE(tagsSchemaOpt.IsSome());
	const sql::Schema &tagsSchema = *tagsSchemaOpt.Value();
	ASSERT_EQ(tagsSchema.columns.size(), 2u);
	EXPECT_TRUE(tagsSchema.columns[0].name == "label");
	EXPECT_TRUE(tagsSchema.columns[0].type == data::NodeType::STRING);
	EXPECT_TRUE(tagsSchema.columns[1].name == "weight");
	EXPECT_TRUE(tagsSchema.columns[1].type == data::NodeType::INT);

	// Row-by-row, field-by-field comparison against the original data —
	// original rows are known in insertion order, and Table<Row>::ForEachLive
	// (which SELECT * walks) preserves that order, so a direct positional
	// comparison is valid here (no sorting needed).
	auto origUsers = db.Execute("SELECT * FROM users");
	auto loadedUsers = loaded.Execute("SELECT * FROM users");
	ASSERT_TRUE(origUsers.IsOk());
	ASSERT_TRUE(loadedUsers.IsOk());
	ASSERT_EQ(origUsers.Value().rows.size(), loadedUsers.Value().rows.size());
	ASSERT_EQ(origUsers.Value().rows.size(), 3u);
	for (size_t i = 0; i < origUsers.Value().rows.size(); ++i)
		EXPECT_TRUE(RowsEqual(origUsers.Value().rows[i], loadedUsers.Value().rows[i]));

	// Explicitly re-check the quoted row survived the actual binary
	// round-trip (not just the pre-save in-memory state checked above).
	ASSERT_EQ(loadedUsers.Value().rows[1].size(), 4u);
	EXPECT_TRUE(loadedUsers.Value().rows[1][1]->stringValue == "O'Brien");

	auto origTags = db.Execute("SELECT * FROM tags");
	auto loadedTags = loaded.Execute("SELECT * FROM tags");
	ASSERT_TRUE(origTags.IsOk());
	ASSERT_TRUE(loadedTags.IsOk());
	ASSERT_EQ(origTags.Value().rows.size(), loadedTags.Value().rows.size());
	ASSERT_EQ(origTags.Value().rows.size(), 2u);
	for (size_t i = 0; i < origTags.Value().rows.size(); ++i)
		EXPECT_TRUE(RowsEqual(origTags.Value().rows[i], loadedTags.Value().rows[i]));

	sdl3::filesystem::Remove(path);
}

TEST(SqlBinaryStorage, LoadReportsErrorForMissingFile) {
	auto loaded = sql::LoadDatabaseBinary(String("/tmp/sql_binary_storage_smoke_test_does_not_exist.bin"));
	EXPECT_TRUE(loaded.IsError());
}

TEST(SqlBinaryStorage, LoadReportsErrorForCorruptMagicBytes) {
	String path = "/tmp/sql_binary_storage_smoke_test_bad_magic.bin";
	const char BOGUS[] = "NOTASQLBFILE_TOO_SHORT_OR_WRONG";
	ASSERT_TRUE(sdl3::WriteFile(path, BOGUS, sizeof(BOGUS)));

	auto loaded = sql::LoadDatabaseBinary(path);
	ASSERT_TRUE(loaded.IsError());
	EXPECT_FALSE(loaded.Error().IsEmpty());

	sdl3::filesystem::Remove(path);
}

TEST(SqlBinaryStorage, LoadReportsErrorForTruncatedBuffer) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE t (a INT)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO t VALUES (42)").IsOk());

	String path = "/tmp/sql_binary_storage_smoke_test_truncated.bin";
	ASSERT_TRUE(sql::SaveDatabaseBinary(path, db));

	// Truncate the saved file to just its header — the table/row body is
	// gone, so loading it must Err (not read out of bounds, not silently
	// produce a wrong/empty Database).
	auto bytes = sdl3::ReadFile(path);
	ASSERT_TRUE(bytes.IsOk());
	ASSERT_TRUE(bytes.Value().size() > 6u);
	String truncPath = "/tmp/sql_binary_storage_smoke_test_truncated_short.bin";
	ASSERT_TRUE(sdl3::WriteFile(truncPath, bytes.Value().data(), 6));

	auto loaded = sql::LoadDatabaseBinary(truncPath);
	ASSERT_TRUE(loaded.IsError());
	EXPECT_FALSE(loaded.Error().IsEmpty());

	sdl3::filesystem::Remove(path);
	sdl3::filesystem::Remove(truncPath);
}

// ─────────────────────────────────────────────────────────────────────────
// 2. CreateIndex / LookupByIndex — populated table, a lookup that matches
//    (right rows come back), one that doesn't (empty), and unknown-table /
//    no-index-yet error cases. Also confirms the documented snapshot
//    staleness: a mutation made AFTER CreateIndex is NOT reflected in a
//    subsequent LookupByIndex, exactly as database.hpp's CreateIndex doc
//    comment promises.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlBinaryStorage, CreateIndexAndLookupByIndexFindExactMatches) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE users (id INT, name STRING, dept STRING)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (1, 'Alice', 'eng')").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (2, 'Bob', 'sales')").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (3, 'Carol', 'eng')").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (4, 'Dave', 'eng')").IsOk());

	auto createR = db.CreateIndex("users", "dept");
	ASSERT_TRUE(createR.IsOk());

	// "eng" matches 3 rows (Alice, Carol, Dave) — confirms duplicate-key
	// handling flows correctly all the way from BTreeIndex through
	// LookupByIndex.
	auto engR = db.LookupByIndex("users", "dept", data::Node::MakeString("eng"));
	ASSERT_TRUE(engR.IsOk());
	std::vector<String> engNames;
	for (auto &row : engR.Value())
		engNames.push_back(row[1]->stringValue);
	std::sort(engNames.begin(), engNames.end());
	ASSERT_EQ(engNames.size(), 3u);
	EXPECT_TRUE(engNames[0] == "Alice");
	EXPECT_TRUE(engNames[1] == "Carol");
	EXPECT_TRUE(engNames[2] == "Dave");

	auto salesR = db.LookupByIndex("users", "dept", data::Node::MakeString("sales"));
	ASSERT_TRUE(salesR.IsOk());
	ASSERT_EQ(salesR.Value().size(), 1u);
	EXPECT_TRUE(salesR.Value()[0][1]->stringValue == "Bob");

	// A value that doesn't exist -> empty, not an error.
	auto missingR = db.LookupByIndex("users", "dept", data::Node::MakeString("marketing"));
	ASSERT_TRUE(missingR.IsOk());
	EXPECT_TRUE(missingR.Value().empty());

	// No index was ever created on "name" -> Err, not a silent full scan.
	auto noIndexR = db.LookupByIndex("users", "name", data::Node::MakeString("Alice"));
	EXPECT_TRUE(noIndexR.IsError());

	// CreateIndex on an unknown table/column -> Err.
	EXPECT_TRUE(db.CreateIndex("does_not_exist", "dept").IsError());
	EXPECT_TRUE(db.CreateIndex("users", "does_not_exist").IsError());
}

TEST(SqlBinaryStorage, CreateIndexIsASnapshotNotLiveMaintained) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE items (id INT, category STRING)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO items VALUES (1, 'books')").IsOk());

	ASSERT_TRUE(db.CreateIndex("items", "category").IsOk());

	// Confirmed present at snapshot time.
	auto before = db.LookupByIndex("items", "category", data::Node::MakeString("books"));
	ASSERT_TRUE(before.IsOk());
	EXPECT_EQ(before.Value().size(), 1u);

	// Mutate the table AFTER CreateIndex: insert a new "toys" row and
	// delete the original "books" row.
	ASSERT_TRUE(db.Execute("INSERT INTO items VALUES (2, 'toys')").IsOk());
	ASSERT_TRUE(db.Execute("DELETE FROM items WHERE id = 1").IsOk());

	// Per CreateIndex's documented snapshot semantics: the new "toys" row
	// is invisible (it was never scanned into the index). The deleted
	// "books" row's id IS still listed in the stale index, but
	// LookupByIndex resolves it back through the live table — where
	// Table<Row>::Delete already tombstoned and discarded it — so it comes
	// back empty too, never resurrected with stale data. Staleness here
	// only ever manifests as a missing row, not a wrong one.
	auto toysLookup = db.LookupByIndex("items", "category", data::Node::MakeString("toys"));
	ASSERT_TRUE(toysLookup.IsOk());
	EXPECT_TRUE(toysLookup.Value().empty()); // inserted after CreateIndex -> not indexed

	auto booksLookup = db.LookupByIndex("items", "category", data::Node::MakeString("books"));
	ASSERT_TRUE(booksLookup.IsOk());
	EXPECT_TRUE(booksLookup.Value().empty()); // deleted after CreateIndex -> stale id resolves to nothing live

	// Re-creating the index picks up the current, post-mutation state.
	ASSERT_TRUE(db.CreateIndex("items", "category").IsOk());
	auto toysAfterRebuild = db.LookupByIndex("items", "category", data::Node::MakeString("toys"));
	ASSERT_TRUE(toysAfterRebuild.IsOk());
	EXPECT_EQ(toysAfterRebuild.Value().size(), 1u);

	auto booksAfterRebuild = db.LookupByIndex("items", "category", data::Node::MakeString("books"));
	ASSERT_TRUE(booksAfterRebuild.IsOk());
	EXPECT_TRUE(booksAfterRebuild.Value().empty());
}

int main() {
	return RUN_ALL_TESTS();
}
