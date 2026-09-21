// Smoke test : sql:: (lib/include/sql/sql.hpp) — Table<Row> core
// (Insert/Get/Delete/Query with stable ids surviving deletion), query.hpp's
// And/Or combinators, and storage.hpp's two persistence halves (JSON project
// tree round-trip, CSV table round-trip). Entirely CPU-only, no GPU/window
// involved.
#define USE_TEST
#include "core/test.hpp"
#include "sdl3/filesystem.hpp"
#include "sql/sql.hpp"

#include <algorithm>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────
// Shared row type for the Table<Row> tests below.
// ─────────────────────────────────────────────────────────────────────────
struct Item {
	String name;
	int value = 0;
};

// ─────────────────────────────────────────────────────────────────────────
// 1. Table core: Insert returns stable monotonic ids; Delete tombstones a
//    slot without shifting/invalidating the other ids; Query only ever
//    matches live rows — a deleted row's stale in-memory data (value=10,
//    which would still satisfy `value > 3`) must never come back.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlTable, InsertGetDeleteTombstoneAndQuery) {
	sql::Table<Item> table;

	size_t id0 = table.Insert(Item{"alpha", 5});
	size_t id1 = table.Insert(Item{"beta", 10});
	size_t id2 = table.Insert(Item{"gamma", 1});

	EXPECT_EQ(id0, 0u);
	EXPECT_EQ(id1, 1u);
	EXPECT_EQ(id2, 2u);

	ASSERT_TRUE(table.Get(id0).IsSome());
	EXPECT_TRUE(table.Get(id0).Value()->name == "alpha");
	EXPECT_EQ(table.Get(id0).Value()->value, 5);

	ASSERT_TRUE(table.Get(id1).IsSome());
	EXPECT_EQ(table.Get(id1).Value()->value, 10);

	// Mutable Get() actually mutates the stored row.
	table.Get(id0).Value()->value = 99;
	EXPECT_EQ(table.Get(id0).Value()->value, 99);

	// Query before deletion: value > 3 matches id0 (99) and id1 (10), not id2 (1).
	auto before = table.Query([](const Item &item) { return item.value > 3; });
	EXPECT_EQ(before.size(), 2u);

	ASSERT_TRUE(table.Delete(id1));
	EXPECT_FALSE(table.Delete(id1)); // already deleted -> false

	// id1 is gone, but id0/id2 are unaffected and keep their original ids.
	EXPECT_TRUE(table.Get(id1).IsNone());
	ASSERT_TRUE(table.Get(id0).IsSome());
	EXPECT_EQ(table.Get(id0).Value()->value, 99);
	ASSERT_TRUE(table.Get(id2).IsSome());
	EXPECT_TRUE(table.Get(id2).Value()->name == "gamma");

	// Query after deletion: id1's stale value (10) would still satisfy
	// `value > 3`, but a tombstoned row must never be matched.
	auto after = table.Query([](const Item &item) { return item.value > 3; });
	ASSERT_EQ(after.size(), 1u);
	EXPECT_EQ(after[0], id0);

	EXPECT_EQ(table.Size(), 2u);

	// Out-of-range access is a clean NONE/false, not UB.
	EXPECT_TRUE(table.Get(999).IsNone());
	EXPECT_FALSE(table.Delete(999));
}

// ─────────────────────────────────────────────────────────────────────────
// 2. query.hpp combinators (All/And/Or) against four rows engineered to hit
//    every true/false combination of two predicates.
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlQuery, AllAndOrCombinators) {
	sql::Table<Item> table;
	size_t id0 = table.Insert(Item{"apple", 20});  // starts-with-a: T, value>10: T
	size_t id1 = table.Insert(Item{"apricot", 5}); // T, F
	size_t id2 = table.Insert(Item{"berry", 20});  // F, T
	size_t id3 = table.Insert(Item{"berry", 5});   // F, F

	auto startsWithA = [](const Item &item) { return item.name.StartsWith("a"); };
	auto valueOver10 = [](const Item &item) { return item.value > 10; };

	auto all = table.Query(sql::All<Item>());
	EXPECT_EQ(all.size(), 4u);

	auto andIds = table.Query(sql::And(startsWithA, valueOver10));
	ASSERT_EQ(andIds.size(), 1u);
	EXPECT_EQ(andIds[0], id0);

	auto orIds = table.Query(sql::Or(startsWithA, valueOver10));
	EXPECT_EQ(orIds.size(), 3u); // id0, id1, id2 — id3 matches neither

	bool orHasId3 = false;
	for (auto id : orIds)
		if (id == id3)
			orHasId3 = true;
	EXPECT_FALSE(orHasId3);
	(void) id1;
	(void) id2;
}

// ─────────────────────────────────────────────────────────────────────────
// Recursive deep-equality helper for the JSON round-trip test — compares
// type and, per type, the relevant scalar field, or recurses over
// Keys()/order for Object/Array. Mirrors data::Node::Clone()'s own
// switch-over-NodeType shape.
// ─────────────────────────────────────────────────────────────────────────
static bool NodesEqual(const data::NodePtr &a, const data::NodePtr &b) {
	if (!a || !b)
		return a == b;
	if (a->type != b->type)
		return false;
	switch (a->type) {
		case data::NodeType::NONE:
			return true;
		case data::NodeType::STRING:
			return a->stringValue == b->stringValue;
		case data::NodeType::BOOL:
			return a->boolValue == b->boolValue;
		case data::NodeType::INT:
			return a->intValue == b->intValue;
		case data::NodeType::FLOAT:
			return a->floatValue == b->floatValue;
		case data::NodeType::OBJECT:
		case data::NodeType::ARRAY: {
			if (a->Keys().size() != b->Keys().size())
				return false;
			for (size_t i = 0; i < a->Keys().size(); ++i) {
				if (a->Keys()[i] != b->Keys()[i])
					return false;
				if (!NodesEqual(a->Get(a->Keys()[i]), b->Get(b->Keys()[i])))
					return false;
			}
			return true;
		}
	}
	return false;
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Project JSON round-trip — a hand-built Node tree mixing nested Object,
//    Array, and every scalar type, saved then reloaded, compared field by
//    field (not just "didn't crash").
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlStorage, ProjectJsonRoundTripPreservesAllShapesAndTypes) {
	auto root = data::Node::MakeObject();
	root->Set("name", data::Node::MakeString("Test Project"));
	root->Set("enabled", data::Node::MakeBool(true));
	root->Set("count", data::Node::MakeInt(42));
	root->Set("ratio", data::Node::MakeFloat(3.5));

	auto meta = data::Node::MakeObject();
	meta->Set("author", data::Node::MakeString("Clement"));
	meta->Set("version", data::Node::MakeInt(2));
	meta->Set("published", data::Node::MakeBool(false));
	root->Set("meta", meta);

	auto items = data::Node::MakeArray();
	items->Push(data::Node::MakeInt(1));
	items->Push(data::Node::MakeString("two"));
	items->Push(data::Node::MakeBool(false));
	items->Push(data::Node::MakeFloat(4.25));
	root->Set("items", items);

	String path = "/tmp/sql_smoke_test_project.json";

	ASSERT_TRUE(sql::SaveProjectJson(path, root));

	auto loaded = sql::LoadProjectJson(path);
	ASSERT_TRUE(loaded.IsOk());
	EXPECT_TRUE(NodesEqual(root, loaded.Value()));

	sdl3::filesystem::Remove(path);
}

TEST(SqlStorage, LoadProjectJsonReportsErrorForMissingFile) {
	auto loaded = sql::LoadProjectJson(String("/tmp/sql_smoke_test_does_not_exist.json"));
	EXPECT_TRUE(loaded.IsError());
}

// ─────────────────────────────────────────────────────────────────────────
// 4. Table<Row> CSV round-trip — every original row's field values must be
//    recoverably present in the freshly-loaded table (ids/order are not
//    required to round-trip, per storage.hpp's design).
// ─────────────────────────────────────────────────────────────────────────
TEST(SqlStorage, TableCsvRoundTripPreservesRowData) {
	sql::Table<Item> table;
	table.Insert(Item{"recent_a.png", 10});
	table.Insert(Item{"recent_b.txt", 20});
	table.Insert(Item{"recent_c.json", 30});

	std::vector<String> columns = {"name", "value"};

	std::function<data::NodePtr(const Item &, const String &)> getField = [](const Item &item, const String &col) {
		if (col == "name")
			return data::Node::MakeString(item.name);
		return data::Node::MakeInt(item.value);
	};

	String path = "/tmp/sql_smoke_test_table.csv";
	ASSERT_TRUE(sql::SaveTableCsv(path, table, columns, getField));

	std::function<Option<Item>(const std::vector<std::pair<String, data::NodePtr>> &)> makeRow =
		[](const std::vector<std::pair<String, data::NodePtr>> &cells) -> Option<Item> {
		Item item;
		for (auto &[col, cell] : cells) {
			if (!cell)
				continue;
			if (col == "name")
				item.name = cell->stringValue;
			else if (col == "value")
				item.value = static_cast<int>(cell->intValue);
		}
		return Some(item);
	};

	auto loadedResult = sql::LoadTableCsv<Item>(path, columns, makeRow);
	ASSERT_TRUE(loadedResult.IsOk());
	sql::Table<Item> &loaded = loadedResult.Value();

	EXPECT_EQ(loaded.Size(), 3u);

	std::vector<std::pair<String, int>> originalRows = {
		{"recent_a.png", 10}, {"recent_b.txt", 20}, {"recent_c.json", 30}};
	std::vector<std::pair<String, int>> loadedRows;
	loaded.ForEachLive([&](size_t /*id*/, const Item &item) { loadedRows.emplace_back(item.name, item.value); });

	auto sortRows = [](std::vector<std::pair<String, int>> &rows) {
		std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	};
	sortRows(originalRows);
	sortRows(loadedRows);

	ASSERT_EQ(loadedRows.size(), originalRows.size());
	for (size_t i = 0; i < originalRows.size(); ++i) {
		EXPECT_TRUE(originalRows[i].first == loadedRows[i].first);
		EXPECT_EQ(originalRows[i].second, loadedRows[i].second);
	}

	sdl3::filesystem::Remove(path);
}

int main() {
	return RUN_ALL_TESTS();
}
