// Smoke test : sql::BTreeIndex (lib/include/sql/btree_index.hpp) — a
// self-contained in-memory B+Tree, tested in complete isolation from
// sql::Database. Proves it actually splits under load, handles duplicate
// keys, and — the piece the reference project this module is modeled after
// (github.com/czLad/cs8_finalProject_SQL_Database) left as a known-broken
// stub — that Remove() correctly rebalances (borrow/merge) and the tree
// stays fully queryable afterward, not just on the happy path. CPU-only, no
// GPU/window involved.
#define USE_TEST
#include "core/test.hpp"
#include "sql/btree_index.hpp"

#include <algorithm>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────
// 1. Enough inserts (t=4 -> splits at 7 keys per node) to force multiple
//    node splits, then confirm every single key is still findable with the
//    exact right row id afterward.
// ─────────────────────────────────────────────────────────────────────────
TEST(BTreeIndex, InsertManyKeysForcesSplitsAndStaysQueryable) {
	sql::BTreeIndex index(4);

	const int COUNT = 50;
	for (int i = 0; i < COUNT; ++i)
		index.Insert(data::Node::MakeInt(i), static_cast<size_t>(i));

	for (int i = 0; i < COUNT; ++i) {
		auto ids = index.Find(data::Node::MakeInt(i));
		ASSERT_EQ(ids.size(), 1u);
		EXPECT_EQ(ids[0], static_cast<size_t>(i));
	}

	// A key that was never inserted.
	EXPECT_TRUE(index.Find(data::Node::MakeInt(9999)).empty());
}

// Same, but with keys inserted in descending order — exercises splitting on
// the "always insert at the front" path too, not just ascending.
TEST(BTreeIndex, InsertDescendingOrderStaysQueryable) {
	sql::BTreeIndex index(4);

	const int COUNT = 40;
	for (int i = COUNT - 1; i >= 0; --i)
		index.Insert(data::Node::MakeInt(i), static_cast<size_t>(i) + 1000);

	for (int i = 0; i < COUNT; ++i) {
		auto ids = index.Find(data::Node::MakeInt(i));
		ASSERT_EQ(ids.size(), 1u);
		EXPECT_EQ(ids[0], static_cast<size_t>(i) + 1000);
	}
}

// ─────────────────────────────────────────────────────────────────────────
// 2. Duplicate keys: several different row ids under the exact same key
//    value must all come back from Find.
// ─────────────────────────────────────────────────────────────────────────
TEST(BTreeIndex, DuplicateKeysReturnAllMatchingRowIds) {
	sql::BTreeIndex index(4);

	index.Insert(data::Node::MakeString("alice"), 1);
	index.Insert(data::Node::MakeString("bob"), 2);
	index.Insert(data::Node::MakeString("alice"), 3);
	index.Insert(data::Node::MakeString("alice"), 4);
	index.Insert(data::Node::MakeString("carol"), 5);

	auto aliceIds = index.Find(data::Node::MakeString("alice"));
	std::sort(aliceIds.begin(), aliceIds.end());
	ASSERT_EQ(aliceIds.size(), 3u);
	EXPECT_EQ(aliceIds[0], 1u);
	EXPECT_EQ(aliceIds[1], 3u);
	EXPECT_EQ(aliceIds[2], 4u);

	auto bobIds = index.Find(data::Node::MakeString("bob"));
	ASSERT_EQ(bobIds.size(), 1u);
	EXPECT_EQ(bobIds[0], 2u);

	EXPECT_TRUE(index.Find(data::Node::MakeString("dave")).empty());
}

// ─────────────────────────────────────────────────────────────────────────
// 3. Remove one (key, rowId) pair out of a duplicate-key group: the other
//    row id under that same key must remain findable; the removed one must
//    be gone. Removing a non-existent (key, rowId) pair returns false and
//    changes nothing.
// ─────────────────────────────────────────────────────────────────────────
TEST(BTreeIndex, RemoveOnePairFromDuplicateGroupKeepsTheOther) {
	sql::BTreeIndex index(4);
	index.Insert(data::Node::MakeInt(7), 100);
	index.Insert(data::Node::MakeInt(7), 200);

	EXPECT_FALSE(index.Remove(data::Node::MakeInt(7), 999)); // wrong rowId for this key
	EXPECT_FALSE(index.Remove(data::Node::MakeInt(42), 1));  // key never existed

	ASSERT_TRUE(index.Remove(data::Node::MakeInt(7), 100));

	auto ids = index.Find(data::Node::MakeInt(7));
	ASSERT_EQ(ids.size(), 1u);
	EXPECT_EQ(ids[0], 200u);

	// Removing the last row id under a key removes the key entry entirely.
	ASSERT_TRUE(index.Remove(data::Node::MakeInt(7), 200));
	EXPECT_TRUE(index.Find(data::Node::MakeInt(7)).empty());
}

// ─────────────────────────────────────────────────────────────────────────
// 4. The high-risk part: remove enough keys to force at least one
//    underflow/rebalance (borrow or merge), then confirm the tree is STILL
//    correctly queryable for every remaining key — this is exactly the
//    scenario the reference project's own delete path left broken.
// ─────────────────────────────────────────────────────────────────────────
TEST(BTreeIndex, RemoveManyKeysForcesRebalanceAndStaysCorrect) {
	sql::BTreeIndex index(4);

	const int COUNT = 60;
	for (int i = 0; i < COUNT; ++i)
		index.Insert(data::Node::MakeInt(i), static_cast<size_t>(i));

	// Remove every 3rd key (0, 3, 6, ...) — spread across the whole
	// keyspace, guaranteeing multiple leaves underflow and need rebalancing
	// (borrow or merge), possibly cascading into their parents too.
	std::vector<int> removed;
	for (int i = 0; i < COUNT; i += 3) {
		ASSERT_TRUE(index.Remove(data::Node::MakeInt(i), static_cast<size_t>(i)));
		removed.push_back(i);
	}

	// Every removed key is really gone.
	for (int i : removed)
		EXPECT_TRUE(index.Find(data::Node::MakeInt(i)).empty());

	// Every surviving key is still findable with its correct row id — the
	// real proof the rebalance didn't corrupt the tree.
	for (int i = 0; i < COUNT; ++i) {
		if (i % 3 == 0)
			continue;
		auto ids = index.Find(data::Node::MakeInt(i));
		ASSERT_EQ(ids.size(), 1u);
		EXPECT_EQ(ids[0], static_cast<size_t>(i));
	}

	// Now remove almost everything else too, down to a handful of keys —
	// forces repeated merges all the way up toward (and including) the
	// root shrinking.
	// Survivors after this pass are exactly {i : i%3!=0 && i%7==0} — remove
	// everything else that's still present (i%3!=0 && i%7!=0).
	for (int i = 0; i < COUNT; ++i) {
		if (i % 3 == 0)
			continue;
		if (i % 7 != 0)
			ASSERT_TRUE(index.Remove(data::Node::MakeInt(i), static_cast<size_t>(i)));
	}

	for (int i = 0; i < COUNT; ++i) {
		auto ids = index.Find(data::Node::MakeInt(i));
		bool shouldSurvive = (i % 3 != 0) && (i % 7 == 0);
		if (shouldSurvive) {
			ASSERT_EQ(ids.size(), 1u);
			EXPECT_EQ(ids[0], static_cast<size_t>(i));
		} else {
			EXPECT_TRUE(ids.empty());
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────
// 5. Mixed key types across separate indexes (INT-keyed and STRING-keyed) —
//    Compare3Way (reused from expression.hpp) supports both.
// ─────────────────────────────────────────────────────────────────────────
TEST(BTreeIndex, IntKeyedIndexOrdersAndFindsCorrectly) {
	sql::BTreeIndex index(4);
	std::vector<int> values = {50, 10, 40, 20, 30, 5, 45, 25, 35, 15};
	for (size_t i = 0; i < values.size(); ++i)
		index.Insert(data::Node::MakeInt(values[i]), i);

	for (size_t i = 0; i < values.size(); ++i) {
		auto ids = index.Find(data::Node::MakeInt(values[i]));
		ASSERT_EQ(ids.size(), 1u);
		EXPECT_EQ(ids[0], i);
	}
	EXPECT_TRUE(index.Find(data::Node::MakeInt(1000)).empty());
}

TEST(BTreeIndex, StringKeyedIndexOrdersAndFindsCorrectly) {
	sql::BTreeIndex index(4);
	std::vector<String> values = {"mango", "apple", "kiwi", "banana", "date",
								   "fig",   "grape", "lime", "pear",   "cherry"};
	for (size_t i = 0; i < values.size(); ++i)
		index.Insert(data::Node::MakeString(values[i]), i);

	for (size_t i = 0; i < values.size(); ++i) {
		auto ids = index.Find(data::Node::MakeString(values[i]));
		ASSERT_EQ(ids.size(), 1u);
		EXPECT_EQ(ids[0], i);
	}
	EXPECT_TRUE(index.Find(data::Node::MakeString("zzz_not_present")).empty());
}

int main() {
	return RUN_ALL_TESTS();
}
