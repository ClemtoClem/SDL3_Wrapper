// Définitions de data/node.hpp
#include "data/node.hpp"

namespace data {

// ── Node ─────────────────────────────────────────────────────────────────────

Node::Node(NodeType t) : type(t) {
	if (t == NodeType::OBJECT || t == NodeType::ARRAY)
		props = sdl3::Properties::Create();
}

NodePtr Node::MakeString(String v) {
	auto n = std::make_shared<Node>(NodeType::STRING);
	n->stringValue = std::move(v);
	return n;
}

NodePtr Node::MakeBool(bool v) {
	auto n = std::make_shared<Node>(NodeType::BOOL);
	n->boolValue = v;
	return n;
}

NodePtr Node::MakeInt(int64_t v) {
	auto n = std::make_shared<Node>(NodeType::INT);
	n->intValue = v;
	return n;
}

NodePtr Node::MakeFloat(double v) {
	auto n = std::make_shared<Node>(NodeType::FLOAT);
	n->floatValue = v;
	return n;
}

void Node::Set(const String &key, NodePtr child) {
	bool isNewKey = !props.HasProperty(key.CStr());
	auto *heapPtr = new NodePtr(std::move(child));
	props.SetPointerWithCleanup(key.CStr(), heapPtr, [](void *p) { delete static_cast<NodePtr *>(p); });
	if (isNewKey)
		order.push_back(key);
}

NodePtr Node::Get(const String &key) const {
	auto *p = props.GetTypedPointer<NodePtr>(key.CStr());
	return p ? *p : nullptr;
}

void Node::Remove(const String &key) {
	props.ClearProperty(key.CStr());
	order.erase(std::remove(order.begin(), order.end(), key), order.end());
}

NodePtr Node::Clone() const {
	switch (type) {
		case NodeType::NONE:
			return MakeNone();
		case NodeType::STRING:
			return MakeString(stringValue);
		case NodeType::BOOL:
			return MakeBool(boolValue);
		case NodeType::INT:
			return MakeInt(intValue);
		case NodeType::FLOAT:
			return MakeFloat(floatValue);
		case NodeType::OBJECT: {
			auto n = MakeObject();
			for (auto &k : order)
				n->Set(k, Get(k)->Clone());
			return n;
		}
		case NodeType::ARRAY: {
			auto n = MakeArray();
			for (auto &k : order)
				n->Push(Get(k)->Clone());
			return n;
		}
	}
	return MakeNone();
}

} // namespace data
