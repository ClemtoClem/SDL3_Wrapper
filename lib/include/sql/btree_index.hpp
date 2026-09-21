#pragma once
/**
 * sql::BTreeIndex — an in-memory B+Tree keyed by a scalar data::NodePtr
 * value (INT/FLOAT/STRING/BOOL), mapping each key to the list of row ids
 * that hold it. This is the piece the reference project this module is
 * modeled after (github.com/czLad/cs8_finalProject_SQL_Database) itself
 * admits is incomplete ("remove operation prepared but lacking disk
 * management for DELETE") — this implementation's Remove() is a real,
 * complete B+Tree deletion (borrow-from-sibling / merge-with-sibling,
 * propagated up through parents), not a stub.
 *
 * Self-contained and independently unit-testable: no Database/Table
 * dependency here (database.hpp wires this up as an optional secondary
 * index on top of its own storage — see CreateIndex/LookupByIndex there).
 *
 * Key comparison reuses sql::detail::Compare3Way (expression.hpp) directly
 * rather than re-deriving 3-way scalar comparison logic — it already
 * handles numeric INT/FLOAT cross-comparison and STRING lexicographic
 * comparison correctly. Compare3Way Err's on an incompatible type pairing
 * (e.g. STRING vs INT); BTreeIndex still needs *some* total order to stay a
 * valid tree in that case, so CompareKeys below falls back to ordering by
 * NodeType as a last resort — arbitrary but deterministic, and never
 * actually exercised by Database::CreateIndex, which only ever inserts
 * values already typed by that column's Schema (a single NodeType per
 * index).
 *
 * Node shape (classic B+Tree, minimum degree `t`, configurable): internal
 * nodes hold `keys.size()` separator keys and `keys.size()+1` child
 * pointers, never data; leaf nodes hold keys with a parallel row-id list
 * per key (duplicate keys are allowed — a real indexed column may repeat a
 * value, so each leaf entry maps one key to potentially several row ids).
 * A node splits once insertion would leave it holding `2t-1` keys, and
 * splitting promotes the median key to the parent (for a leaf split, that
 * key is *copied* up and remains present in the right leaf too, since leaf
 * data must stay in leaves; for an internal split, the median key is moved
 * up, since internal separators carry no data of their own).
 */
#include "../core/core.hpp"
#include "../data/node.hpp"
#include "expression.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace sql {

namespace detail {

/// Total order over two scalar keys for BTreeIndex's internal use. Defers
/// to Compare3Way for everything it can compare; on Compare3Way's Err (an
/// incompatible type pairing) falls back to ordering by NodeType so the
/// tree stays internally consistent rather than misbehaving on a mixed-type
/// index (see this file's header doc comment for why that fallback is safe
/// in practice).
[[nodiscard]] inline int CompareKeys(const data::NodePtr &a, const data::NodePtr &b) {
	auto cmp = Compare3Way(a, b);
	if (cmp.IsOk())
		return cmp.Value();
	return (a->type < b->type) ? -1 : (a->type > b->type ? 1 : 0);
}

} // namespace detail

class BTreeIndex {
public:
	/// `t` is the tree's minimum degree — a node holds at most `2t-1` keys
	/// (splits once it would hold that many) and, other than the root, at
	/// least `t-1` keys. Must be >= 2.
	explicit BTreeIndex(size_t t = 4) : m_t(t < 2 ? 2 : t), m_root(std::make_unique<Node>(/*isLeaf=*/true)) {}

	/// Inserts one (key, rowId) pair. A key already present gets `rowId`
	/// appended to its existing row-id list (duplicate keys are allowed);
	/// a brand-new key is inserted in sorted order, splitting nodes on
	/// overflow as needed (possibly growing the tree's height by one).
	void Insert(const data::NodePtr &key, size_t rowId) {
		auto split = InsertRecursive(m_root.get(), key, rowId);
		if (!split.IsSome())
			return;
		auto newRoot = std::make_unique<Node>(/*isLeaf=*/false);
		newRoot->keys.push_back(split->sepKey);
		newRoot->children.push_back(std::move(m_root));
		newRoot->children.push_back(std::move(split->rightChild));
		m_root = std::move(newRoot);
	}

	/// All row ids stored under an exact match of `key`, in insertion order;
	/// empty if `key` isn't present at all.
	[[nodiscard]] std::vector<size_t> Find(const data::NodePtr &key) const {
		const Node *node = m_root.get();
		while (!node->isLeaf)
			node = node->children[ChildIndexFor(node, key)].get();
		for (size_t i = 0; i < node->keys.size(); ++i)
			if (detail::CompareKeys(node->keys[i], key) == 0)
				return node->rowIds[i];
		return {};
	}

	/// Removes just the one `(key, rowId)` pair — not every row under that
	/// key. If that leaf entry's row-id list becomes empty the key entry
	/// itself is removed; if the leaf (or an internal node touched while
	/// unwinding) then underflows below `t-1` keys, it's rebalanced via
	/// borrow-from-sibling or merge-with-sibling, propagated up through
	/// parents as needed. Returns false if `(key, rowId)` wasn't present.
	bool Remove(const data::NodePtr &key, size_t rowId) {
		bool removed = RemoveFromNode(m_root.get(), key, rowId);
		if (!removed)
			return false;
		// The root has no minimum-key constraint, but if it's an internal
		// node that a merge emptied down to zero keys (a single remaining
		// child), the tree's height shrinks by adopting that child as the
		// new root.
		if (!m_root->isLeaf && m_root->keys.empty())
			m_root = std::move(m_root->children.front());
		return true;
	}

private:
	struct Node {
		bool isLeaf;
		std::vector<data::NodePtr> keys;
		std::vector<std::vector<size_t>> rowIds;			// leaf-only, parallel to keys
		std::vector<std::unique_ptr<Node>> children;		// internal-only, keys.size()+1 entries

		explicit Node(bool leaf) : isLeaf(leaf) {}
	};

	/// What a child split hands back to its parent: the separator key to
	/// insert, and the new right-hand sibling node to link in beside it.
	struct SplitResult {
		data::NodePtr sepKey;
		std::unique_ptr<Node> rightChild;
	};

	size_t m_t;
	std::unique_ptr<Node> m_root;

	[[nodiscard]] size_t MaxKeys() const noexcept { return 2 * m_t - 1; }
	[[nodiscard]] size_t MinKeys() const noexcept { return m_t - 1; }

	/// Index of the child to descend into while searching/inserting for
	/// `key`: the count of separator keys <= key. Matches the leaf-split
	/// convention below, where a promoted separator is also the smallest
	/// key of the right-hand side — so a search for exactly that key must
	/// land on the right.
	[[nodiscard]] size_t ChildIndexFor(const Node *node, const data::NodePtr &key) const {
		size_t idx = 0;
		while (idx < node->keys.size() && detail::CompareKeys(node->keys[idx], key) <= 0)
			++idx;
		return idx;
	}

	/// Recursively inserts (key, rowId) under `node`. Returns Some(split) if
	/// `node` overflowed and had to split — the caller links the returned
	/// right sibling in beside `node` and inserts the separator key.
	[[nodiscard]] Option<SplitResult> InsertRecursive(Node *node, const data::NodePtr &key, size_t rowId) {
		if (node->isLeaf) {
			size_t idx = 0;
			while (idx < node->keys.size() && detail::CompareKeys(node->keys[idx], key) < 0)
				++idx;
			if (idx < node->keys.size() && detail::CompareKeys(node->keys[idx], key) == 0) {
				node->rowIds[idx].push_back(rowId);
				return NONE; // existing key — no new key inserted, so no possible overflow
			}
			node->keys.insert(node->keys.begin() + static_cast<ptrdiff_t>(idx), key);
			node->rowIds.insert(node->rowIds.begin() + static_cast<ptrdiff_t>(idx), std::vector<size_t>{rowId});
			if (node->keys.size() < MaxKeys())
				return NONE;
			return Some(SplitLeaf(node));
		}

		size_t idx = ChildIndexFor(node, key);
		auto childSplit = InsertRecursive(node->children[idx].get(), key, rowId);
		if (childSplit.IsNone())
			return NONE;

		node->keys.insert(node->keys.begin() + static_cast<ptrdiff_t>(idx), childSplit->sepKey);
		node->children.insert(node->children.begin() + static_cast<ptrdiff_t>(idx) + 1,
							   std::move(childSplit->rightChild));
		if (node->keys.size() < MaxKeys())
			return NONE;
		return Some(SplitInternal(node));
	}

	/// Splits an overflowing leaf in half. The right half's first key is
	/// copied up as the separator (it stays in the right leaf too — leaf
	/// data must remain in leaves).
	[[nodiscard]] SplitResult SplitLeaf(Node *node) {
		size_t mid = node->keys.size() / 2;
		auto right = std::make_unique<Node>(/*isLeaf=*/true);
		right->keys.assign(node->keys.begin() + static_cast<ptrdiff_t>(mid), node->keys.end());
		right->rowIds.assign(node->rowIds.begin() + static_cast<ptrdiff_t>(mid), node->rowIds.end());
		node->keys.resize(mid);
		node->rowIds.resize(mid);

		data::NodePtr sepKey = right->keys.front();
		return SplitResult{std::move(sepKey), std::move(right)};
	}

	/// Splits an overflowing internal node in half. The median key is moved
	/// (not copied) up to the parent, since an internal separator carries no
	/// data of its own — see this file's header doc comment for why that
	/// preserves the B+Tree invariant across the split.
	[[nodiscard]] SplitResult SplitInternal(Node *node) {
		size_t mid = node->keys.size() / 2;
		data::NodePtr promoted = node->keys[mid];

		auto right = std::make_unique<Node>(/*isLeaf=*/false);
		right->keys.assign(node->keys.begin() + static_cast<ptrdiff_t>(mid) + 1, node->keys.end());
		right->children.assign(std::make_move_iterator(node->children.begin() + static_cast<ptrdiff_t>(mid) + 1),
								std::make_move_iterator(node->children.end()));

		node->keys.resize(mid);
		node->children.resize(mid + 1);

		return SplitResult{std::move(promoted), std::move(right)};
	}

	/// Removes (key, rowId) from the subtree rooted at `node`. Returns
	/// false if not found (in which case nothing was modified). On success,
	/// if `node` is internal, rebalances whichever child the removal
	/// descended into if that child now underflows — this is what makes the
	/// rebalance propagate one level at a time as the recursion unwinds.
	bool RemoveFromNode(Node *node, const data::NodePtr &key, size_t rowId) {
		if (node->isLeaf) {
			for (size_t i = 0; i < node->keys.size(); ++i) {
				if (detail::CompareKeys(node->keys[i], key) != 0)
					continue;
				auto &ids = node->rowIds[i];
				for (size_t j = 0; j < ids.size(); ++j) {
					if (ids[j] != rowId)
						continue;
					ids.erase(ids.begin() + static_cast<ptrdiff_t>(j));
					if (ids.empty()) {
						node->keys.erase(node->keys.begin() + static_cast<ptrdiff_t>(i));
						node->rowIds.erase(node->rowIds.begin() + static_cast<ptrdiff_t>(i));
					}
					return true;
				}
				return false; // key present, but not with this rowId
			}
			return false;
		}

		size_t idx = ChildIndexFor(node, key);
		bool removed = RemoveFromNode(node->children[idx].get(), key, rowId);
		if (!removed)
			return false;
		RebalanceChildIfUnderflowing(node, idx);
		return true;
	}

	/// If `node->children[idx]` holds fewer than MinKeys() keys, fixes it up
	/// via borrow-from-sibling (preferring the left sibling) or, when
	/// neither sibling has a spare key, merge-with-sibling.
	void RebalanceChildIfUnderflowing(Node *node, size_t idx) {
		Node *child = node->children[idx].get();
		if (child->keys.size() >= MinKeys())
			return;

		bool hasLeft = idx > 0;
		bool hasRight = idx + 1 < node->children.size();

		if (hasLeft && node->children[idx - 1]->keys.size() > MinKeys()) {
			BorrowFromLeft(node, idx);
			return;
		}
		if (hasRight && node->children[idx + 1]->keys.size() > MinKeys()) {
			BorrowFromRight(node, idx);
			return;
		}
		if (hasLeft)
			MergeChildren(node, idx - 1);
		else
			MergeChildren(node, idx);
	}

	void BorrowFromLeft(Node *node, size_t idx) {
		Node *child = node->children[idx].get();
		Node *left = node->children[idx - 1].get();

		if (child->isLeaf) {
			child->keys.insert(child->keys.begin(), left->keys.back());
			child->rowIds.insert(child->rowIds.begin(), std::move(left->rowIds.back()));
			left->keys.pop_back();
			left->rowIds.pop_back();
			node->keys[idx - 1] = child->keys.front();
		} else {
			child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
			child->children.insert(child->children.begin(), std::move(left->children.back()));
			left->children.pop_back();
			node->keys[idx - 1] = left->keys.back();
			left->keys.pop_back();
		}
	}

	void BorrowFromRight(Node *node, size_t idx) {
		Node *child = node->children[idx].get();
		Node *right = node->children[idx + 1].get();

		if (child->isLeaf) {
			child->keys.push_back(right->keys.front());
			child->rowIds.push_back(std::move(right->rowIds.front()));
			right->keys.erase(right->keys.begin());
			right->rowIds.erase(right->rowIds.begin());
			node->keys[idx] = right->keys.front();
		} else {
			child->keys.push_back(node->keys[idx]);
			child->children.push_back(std::move(right->children.front()));
			right->children.erase(right->children.begin());
			node->keys[idx] = right->keys.front();
			right->keys.erase(right->keys.begin());
		}
	}

	/// Merges node->children[leftIdx] and node->children[leftIdx+1] into a
	/// single node (kept at leftIdx), removing the separator key between
	/// them (node->keys[leftIdx]) and the now-absorbed right child.
	void MergeChildren(Node *node, size_t leftIdx) {
		Node *left = node->children[leftIdx].get();
		Node *right = node->children[leftIdx + 1].get();

		if (left->isLeaf) {
			// The leaf separator is already duplicated as right's first
			// key, so merging leaves needs no key pulled down from `node`.
			left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
			left->rowIds.insert(left->rowIds.end(), std::make_move_iterator(right->rowIds.begin()),
								 std::make_move_iterator(right->rowIds.end()));
		} else {
			left->keys.push_back(node->keys[leftIdx]);
			left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
			left->children.insert(left->children.end(), std::make_move_iterator(right->children.begin()),
								   std::make_move_iterator(right->children.end()));
		}

		node->keys.erase(node->keys.begin() + static_cast<ptrdiff_t>(leftIdx));
		node->children.erase(node->children.begin() + static_cast<ptrdiff_t>(leftIdx) + 1);
	}
};

} // namespace sql
