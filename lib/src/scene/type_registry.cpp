// Définitions de scene/type_registry.hpp
#include "scene/type_registry.hpp"

namespace scene {

// ── NodeTypeRegistry ─────────────────────────────────────────────────────────

NodeTypeRegistry NodeTypeRegistry::WithBuiltins() {
	NodeTypeRegistry registry;
	NodeTypeInfo node;
	node.name = String(node_type::NODE);
	node.label = String("Groupe");
	node.category = String("Structure");
	node.icon = String("▤");
	registry.Register(std::move(node));

	NodeTypeInfo node3d;
	node3d.name = String(node_type::NODE3D);
	node3d.label = String("Nœud 3D");
	node3d.category = String("Structure");
	node3d.icon = String("◇");
	registry.Register(std::move(node3d));
	return registry;
}

void NodeTypeRegistry::Register(NodeTypeInfo info) {
	for (NodeTypeInfo& existing : m_types)
		if (existing.name == info.name) {
			existing = std::move(info);
			return;
		}
	m_types.push_back(std::move(info));
}

const NodeTypeInfo* NodeTypeRegistry::Find(const String& name) const noexcept {
	for (const NodeTypeInfo& info : m_types)
		if (info.name == name)
			return &info;
	return nullptr;
}

String NodeTypeRegistry::LabelOf(const String& name) const {
	const NodeTypeInfo* info = Find(name);
	return info && !info->label.IsEmpty() ? info->label : name;
}

String NodeTypeRegistry::IconOf(const String& name) const {
	const NodeTypeInfo* info = Find(name);
	return info ? info->icon : String("?");
}

Node NodeTypeRegistry::Make(const String& type, String name) const {
	Node node;
	node.type = type;
	node.name = std::move(name);
	if (const NodeTypeInfo* info = Find(type)) {
		node.components = info->defaultComponents;
		node.properties = info->defaultProperties;
	}
	return node;
}

NodeId NodeTypeRegistry::Create(NodeTree& tree, NodeId parent, const String& type, const String& name) const {
	return tree.Add(parent, Make(type, tree.UniqueChildName(parent, name)));
}

std::function<bool(const String&)> NodeTypeRegistry::TypeChecker() const {
	return [this](const String& type) { return Knows(type); };
}

} // namespace scene
