// Définitions de sql/btree_index.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sql/btree_index.hpp"

namespace sql {

namespace detail {

int CompareKeys(const data::NodePtr &a, const data::NodePtr &b) {
	auto cmp = Compare3Way(a, b);
	if (cmp.IsOk())
		return cmp.Value();
	return (a->type < b->type) ? -1 : (a->type > b->type ? 1 : 0);
}

} // namespace detail

// ── BTreeIndex ───────────────────────────────────────────────────────────────

void BTreeIndex::Insert(const data::NodePtr &key, size_t rowId) {
	auto split = InsertRecursive(m_root.get(), key, rowId);
	if (!split.IsSome())
		return;
	auto newRoot = std::make_unique<Node>(/*isLeaf=*/false);
	newRoot->keys.push_back(split->sepKey);
	newRoot->children.push_back(std::move(m_root));
	newRoot->children.push_back(std::move(split->rightChild));
	m_root = std::move(newRoot);
}

std::vector<size_t> BTreeIndex::Find(const data::NodePtr &key) const {
	const Node *node = m_root.get();
	while (!node->isLeaf)
		node = node->children[ChildIndexFor(node, key)].get();
	for (size_t i = 0; i < node->keys.size(); ++i)
		if (detail::CompareKeys(node->keys[i], key) == 0)
			return node->rowIds[i];
	return {};
}

bool BTreeIndex::Remove(const data::NodePtr &key, size_t rowId) {
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

size_t BTreeIndex::ChildIndexFor(const Node *node, const data::NodePtr &key) const {
	size_t idx = 0;
	while (idx < node->keys.size() && detail::CompareKeys(node->keys[idx], key) <= 0)
		++idx;
	return idx;
}

Option<BTreeIndex::SplitResult> BTreeIndex::InsertRecursive(Node *node, const data::NodePtr &key, size_t rowId) {
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

BTreeIndex::SplitResult BTreeIndex::SplitLeaf(Node *node) {
	size_t mid = node->keys.size() / 2;
	auto right = std::make_unique<Node>(/*isLeaf=*/true);
	right->keys.assign(node->keys.begin() + static_cast<ptrdiff_t>(mid), node->keys.end());
	right->rowIds.assign(node->rowIds.begin() + static_cast<ptrdiff_t>(mid), node->rowIds.end());
	node->keys.resize(mid);
	node->rowIds.resize(mid);

	data::NodePtr sepKey = right->keys.front();
	return SplitResult{std::move(sepKey), std::move(right)};
}

BTreeIndex::SplitResult BTreeIndex::SplitInternal(Node *node) {
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

bool BTreeIndex::RemoveFromNode(Node *node, const data::NodePtr &key, size_t rowId) {
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

void BTreeIndex::RebalanceChildIfUnderflowing(Node *node, size_t idx) {
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

void BTreeIndex::BorrowFromLeft(Node *node, size_t idx) {
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

void BTreeIndex::BorrowFromRight(Node *node, size_t idx) {
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

void BTreeIndex::MergeChildren(Node *node, size_t leftIdx) {
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

} // namespace sql
