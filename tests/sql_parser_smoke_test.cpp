// Smoke test : sql:: SQL text parser + factory/builder (lib/include/sql/
// {token,expression,schema,statement,parser,database,query_builder}.hpp) —
// real end-to-end pipelines through sql::Database::Execute (tokenize ->
// parse -> execute against Table<Row>-backed storage), proving actual rows
// come back with correct values, not just Ok() with empty/wrong data.
// Entirely CPU-only, no GPU/window involved.
#define USE_TEST
#include "core/test.hpp"
#include "sql/sql.hpp"

#include <algorithm>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────
// Small helper: build a users table and populate it with 3 known rows,
// shared by several tests below.
// ─────────────────────────────────────────────────────────────────────────
static void PopulateUsers(sql::Database &db) {
	auto created = db.Execute("CREATE TABLE users (id INT, name STRING, active BOOL)");
	ASSERT_TRUE(created.IsOk());

	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (1, 'Alice', true)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (2, 'Bob', false)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (3, 'Carol', true)").IsOk());
}

// ─────────────────────────────────────────────────────────────────────────
// 1. CREATE TABLE via Database::Execute — TableNames()/GetSchema() report
//    the table and its exact columns/types back.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlParser, CreateTableRegistersSchema) {
	sql::Database db;
	auto result = db.Execute("CREATE TABLE users (id INT, name STRING, active BOOL)");
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().message.Contains("created"));

	auto names = db.TableNames();
	ASSERT_EQ(names.size(), 1u);
	EXPECT_TRUE(names[0] == "users");

	auto schemaOpt = db.GetSchema("users");
	ASSERT_TRUE(schemaOpt.IsSome());
	const sql::Schema &schema = *schemaOpt.Value();
	ASSERT_EQ(schema.columns.size(), 3u);
	EXPECT_TRUE(schema.columns[0].name == "id");
	EXPECT_TRUE(schema.columns[0].type == data::NodeType::INT);
	EXPECT_TRUE(schema.columns[1].name == "name");
	EXPECT_TRUE(schema.columns[1].type == data::NodeType::STRING);
	EXPECT_TRUE(schema.columns[2].name == "active");
	EXPECT_TRUE(schema.columns[2].type == data::NodeType::BOOL);
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Several INSERTs then SELECT * — confirms the exact expected row
//    values come back (not just a row count).
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlParser, InsertThenSelectStarReturnsExactRows) {
	sql::Database db;
	PopulateUsers(db);

	auto result = db.Execute("SELECT * FROM users");
	ASSERT_TRUE(result.IsOk());
	sql::QueryResult &qr = result.Value();
	ASSERT_EQ(qr.rows.size(), 3u);
	ASSERT_EQ(qr.schema.columns.size(), 3u);

	// Rows come back in insertion order (Table<Row>::ForEachLive walks ids
	// in ascending order).
	EXPECT_EQ(qr.rows[0][0]->intValue, int64_t(1));
	EXPECT_TRUE(qr.rows[0][1]->stringValue == "Alice");
	EXPECT_TRUE(qr.rows[0][2]->boolValue == true);

	EXPECT_EQ(qr.rows[1][0]->intValue, int64_t(2));
	EXPECT_TRUE(qr.rows[1][1]->stringValue == "Bob");
	EXPECT_TRUE(qr.rows[1][2]->boolValue == false);

	EXPECT_EQ(qr.rows[2][0]->intValue, int64_t(3));
	EXPECT_TRUE(qr.rows[2][1]->stringValue == "Carol");
	EXPECT_TRUE(qr.rows[2][2]->boolValue == true);
}

// ─────────────────────────────────────────────────────────────────────────
// 3. SELECT with a single WHERE comparison — confirms it actually narrows
//    results, both a matching and non-matching id.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlParser, SelectWithWhereFiltersRows) {
	sql::Database db;
	PopulateUsers(db);

	auto matchResult = db.Execute("SELECT name FROM users WHERE id = 2");
	ASSERT_TRUE(matchResult.IsOk());
	sql::QueryResult &matchQr = matchResult.Value();
	ASSERT_EQ(matchQr.rows.size(), 1u);
	ASSERT_EQ(matchQr.rows[0].size(), 1u);
	EXPECT_TRUE(matchQr.rows[0][0]->stringValue == "Bob");

	auto noMatchResult = db.Execute("SELECT name FROM users WHERE id = 999");
	ASSERT_TRUE(noMatchResult.IsOk());
	EXPECT_EQ(noMatchResult.Value().rows.size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────
// 4. WHERE with AND/OR combined — confirms expression.hpp's shunting-yard
//    handles operator precedence/multiple operators correctly, not just a
//    single bare comparison.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlParser, WhereWithAndOperatorPrecedence) {
	sql::Database db;
	PopulateUsers(db);

	// id > 1 AND active = true -> only Carol (id=3, active=true); Bob
	// (id=2) fails active=true, Alice (id=1) fails id>1.
	auto result = db.Execute("SELECT name FROM users WHERE id > 1 AND active = true");
	ASSERT_TRUE(result.IsOk());
	sql::QueryResult &qr = result.Value();
	ASSERT_EQ(qr.rows.size(), 1u);
	EXPECT_TRUE(qr.rows[0][0]->stringValue == "Carol");
}

TEST(SqlParser, WhereWithOrOperatorPrecedence) {
	sql::Database db;
	PopulateUsers(db);

	// id = 1 OR id = 3 -> Alice and Carol, not Bob.
	auto result = db.Execute("SELECT name FROM users WHERE id = 1 OR id = 3");
	ASSERT_TRUE(result.IsOk());
	sql::QueryResult &qr = result.Value();
	ASSERT_EQ(qr.rows.size(), 2u);
	std::vector<String> names;
	for (auto &row : qr.rows)
		names.push_back(row[0]->stringValue);
	std::sort(names.begin(), names.end());
	EXPECT_TRUE(names[0] == "Alice");
	EXPECT_TRUE(names[1] == "Carol");
}

// ─────────────────────────────────────────────────────────────────────────
// 5. DELETE ... WHERE ... then re-SELECT * — confirms the matching row is
//    gone and the others remain untouched.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlParser, DeleteWithWhereRemovesOnlyMatchingRows) {
	sql::Database db;
	PopulateUsers(db);

	auto deleteResult = db.Execute("DELETE FROM users WHERE active = false");
	ASSERT_TRUE(deleteResult.IsOk());
	EXPECT_TRUE(deleteResult.Value().message.Contains("1"));

	auto afterResult = db.Execute("SELECT * FROM users");
	ASSERT_TRUE(afterResult.IsOk());
	sql::QueryResult &qr = afterResult.Value();
	ASSERT_EQ(qr.rows.size(), 2u);
	for (auto &row : qr.rows)
		EXPECT_TRUE(row[1]->stringValue != "Bob"); // Bob (active=false) was deleted
}

// ─────────────────────────────────────────────────────────────────────────
// 6. DROP TABLE — table disappears from TableNames(), and a subsequent
//    SELECT * against it returns a clear Err, not a crash.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlParser, DropTableRemovesTableAndBlocksFurtherQueries) {
	sql::Database db;
	PopulateUsers(db);

	auto dropResult = db.Execute("DROP TABLE users");
	ASSERT_TRUE(dropResult.IsOk());

	auto names = db.TableNames();
	EXPECT_EQ(names.size(), 0u);

	auto selectResult = db.Execute("SELECT * FROM users");
	ASSERT_TRUE(selectResult.IsError());
	EXPECT_FALSE(selectResult.Error().IsEmpty());
}

// ─────────────────────────────────────────────────────────────────────────
// 7. Deliberately malformed inputs — each must return Err with a non-empty
//    message, not a crash or a silently-wrong Ok.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlParser, MalformedSyntaxReturnsError) {
	sql::Database db;
	auto result = db.Execute("CREATE TABLE users id INT, name STRING)"); // missing '('
	ASSERT_TRUE(result.IsError());
	EXPECT_FALSE(result.Error().IsEmpty());
}

TEST(SqlParser, UnknownTableReturnsError) {
	sql::Database db;
	auto result = db.Execute("SELECT * FROM does_not_exist");
	ASSERT_TRUE(result.IsError());
	EXPECT_FALSE(result.Error().IsEmpty());
}

TEST(SqlParser, WrongInsertValueCountReturnsError) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE users (id INT, name STRING, active BOOL)").IsOk());

	auto result = db.Execute("INSERT INTO users VALUES (1, 'Alice')"); // missing 3rd value
	ASSERT_TRUE(result.IsError());
	EXPECT_FALSE(result.Error().IsEmpty());
}

TEST(SqlParser, UnknownColumnInWhereReturnsError) {
	sql::Database db;
	ASSERT_TRUE(db.Execute("CREATE TABLE users (id INT, name STRING, active BOOL)").IsOk());
	ASSERT_TRUE(db.Execute("INSERT INTO users VALUES (1, 'Alice', true)").IsOk());

	auto result = db.Execute("SELECT * FROM users WHERE nonexistent_column = 1");
	ASSERT_TRUE(result.IsError());
	EXPECT_FALSE(result.Error().IsEmpty());
}

// ─────────────────────────────────────────────────────────────────────────
// 8. QueryBuilder end-to-end — confirms it returns the same result a
//    hand-written equivalent SQL string via Execute() would.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlParser, QueryBuilderMatchesEquivalentSqlText) {
	sql::Database db;
	PopulateUsers(db);

	sql::QueryBuilder builder;
	auto builderResult = builder.Select({"name"}).From("users").Where("id", ">", data::Node::MakeInt(1)).Execute(db);
	ASSERT_TRUE(builderResult.IsOk());

	auto textResult = db.Execute("SELECT name FROM users WHERE id > 1");
	ASSERT_TRUE(textResult.IsOk());

	sql::QueryResult &builderQr = builderResult.Value();
	sql::QueryResult &textQr = textResult.Value();
	ASSERT_EQ(builderQr.rows.size(), textQr.rows.size());
	ASSERT_EQ(builderQr.rows.size(), 2u); // Bob (id=2) and Carol (id=3)

	std::vector<String> builderNames;
	std::vector<String> textNames;
	for (auto &row : builderQr.rows)
		builderNames.push_back(row[0]->stringValue);
	for (auto &row : textQr.rows)
		textNames.push_back(row[0]->stringValue);
	std::sort(builderNames.begin(), builderNames.end());
	std::sort(textNames.begin(), textNames.end());
	ASSERT_EQ(builderNames.size(), textNames.size());
	for (size_t i = 0; i < builderNames.size(); ++i)
		EXPECT_TRUE(builderNames[i] == textNames[i]);
}

// QueryBuilder with multiple Where(...) calls ANDed together, and an empty
// Select({}) meaning SELECT *.
TEST(SqlParser, QueryBuilderMultipleWhereConditionsAreAnded) {
	sql::Database db;
	PopulateUsers(db);

	sql::QueryBuilder builder;
	auto result = builder.From("users")
					  .Where("id", ">", data::Node::MakeInt(1))
					  .Where("active", "=", data::Node::MakeBool(true))
					  .Execute(db);
	ASSERT_TRUE(result.IsOk());
	sql::QueryResult &qr = result.Value();
	ASSERT_EQ(qr.rows.size(), 1u); // only Carol: id=3 (>1) and active=true
	EXPECT_TRUE(qr.schema.columns.size() == 3u); // SELECT * (unset Select())

	size_t nameIdx = qr.schema.ColumnIndex("name").Value();
	EXPECT_TRUE(qr.rows[0][nameIdx]->stringValue == "Carol");
}

int main() {
	return RUN_ALL_TESTS();
}
