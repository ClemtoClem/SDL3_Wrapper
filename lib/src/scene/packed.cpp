// Définitions de scene/packed.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "scene/packed.hpp"

namespace scene {

// ── PackedScene ──────────────────────────────────────────────────────────────

PackedScene PackedScene::FromSubtree(const NodeTree& tree, NodeId root) {
	PackedScene packed;
	const Node* source = tree.Get(root);
	if (!source)
		return packed;
	packed.m_tree = NodeTree(source->name, source->type);
	Node& packedRoot = *packed.m_tree.Get(packed.m_tree.Root());
	const NodeId keepId = packedRoot.id;
	packedRoot = *source;
	packedRoot.id = keepId;
	packedRoot.parent = NodeId{};
	packedRoot.children.clear();
	// Les enfants sont recopiés un à un : Duplicate() copie DANS un arbre,
	// ici on recopie d'un arbre vers un autre.
	CopyChildren(tree, root, packed.m_tree, packed.m_tree.Root());
	packed.RemapInternalRefs(tree, root);
	return packed;
}

bool PackedScene::IsEmpty() const noexcept {
	return m_tree.Size() <= 1 && m_tree.ChildrenOf(m_tree.Root()).empty();
}

NodeId PackedScene::InstantiateInto(NodeTree& target, NodeId parent, const String& name, size_t index) const {
	if (!target.Contains(parent) || m_tree.Size() == 0)
		return NodeId{};

	std::unordered_map<NodeId, NodeId> remap;
	remap.reserve(m_tree.Size());

	NodeId instanceRoot;
	m_tree.Traverse(m_tree.Root(), [&](NodeId id, const Node& node) {
		Node copy = node;
		copy.children.clear();
		NodeId destination = parent;
		size_t insertAt = index;
		if (id != m_tree.Root()) {
			auto found = remap.find(node.parent);
			if (found == remap.end())
				return;
			destination = found->second;
			insertAt = NODE_APPEND;
		} else {
			String wanted = name.IsEmpty() ? node.name : name;
			copy.name = target.UniqueChildName(parent, wanted);
			Component instance;
			instance.type = String(component_type::SCENE_INSTANCE);
			instance.props.Set(String("source"), PropertyValue::Resource(m_source));
			copy.SetComponent(std::move(instance));
		}
		NodeId created = target.Add(destination, std::move(copy), insertAt);
		remap.emplace(id, created);
		if (id == m_tree.Root())
			instanceRoot = created;
	});

	auto lookup = [&remap](NodeId id) -> Option<NodeId> {
		auto found = remap.find(id);
		if (found == remap.end())
			return NONE;
		return Some(found->second);
	};
	for (const auto& [from, to] : remap) {
		Node* node = target.Get(to);
		for (auto& [key, value] : node->properties.Entries())
			value.RemapNodeRefs(lookup);
		for (Component& component : node->components)
			for (auto& [key, value] : component.props.Entries())
				value.RemapNodeRefs(lookup);
	}
	return instanceRoot;
}

bool PackedScene::IsInstanceRoot(const Node& node) noexcept {
	return node.HasComponent(String(component_type::SCENE_INSTANCE));
}

String PackedScene::InstanceSource(const Node& node) {
	const PropertyValue* source =
		node.ComponentProperty(String(component_type::SCENE_INSTANCE), String("source"));
	return source ? source->AsString() : String();
}

data::NodePtr PackedScene::ToJson() const {
	auto json = m_tree.ToJson();
	json->Set("format", data::Node::MakeString(String("scene.packed")));
	if (!m_source.IsEmpty())
		json->Set("source", data::Node::MakeString(m_source));
	return json;
}

Result<PackedScene, String> PackedScene::FromJson(const data::NodePtr& json) {
	auto tree = NodeTree::FromJson(json);
	if (tree.IsError())
		return Err(tree.Error());
	PackedScene packed;
	packed.m_tree = std::move(tree).Unwrap();
	if (json)
		if (auto source = json->Get("source"); source && source->IsString())
			packed.m_source = source->stringValue;
	return Ok(std::move(packed));
}

String PackedScene::EncodeJson() const {
	data::JsonDocument document;
	document.SetRoot(ToJson());
	return document.EncodeStr();
}

Result<PackedScene, String> PackedScene::DecodeJson(const String& text) {
	data::JsonDocument document;
	auto error = document.DecodeStr(text);
	if (error.IsSome())
		return Err(error.Unwrap().Format());
	return FromJson(document.GetRoot());
}

void PackedScene::CopyChildren(const NodeTree& from, NodeId fromNode, NodeTree& to, NodeId toNode) {
	for (NodeId child : from.ChildrenOf(fromNode)) {
		const Node* node = from.Get(child);
		if (!node)
			continue;
		Node copy = *node;
		copy.children.clear();
		NodeId created = to.Add(toNode, std::move(copy));
		CopyChildren(from, child, to, created);
	}
}

void PackedScene::RemapInternalRefs(const NodeTree& from, NodeId fromRoot) {
	std::unordered_map<NodeId, NodeId> remap;
	std::vector<NodeId> sources;
	from.Traverse(fromRoot, [&](NodeId id, const Node&) { sources.push_back(id); });
	std::vector<NodeId> copies = m_tree.AllNodes();
	for (size_t i = 0; i < sources.size() && i < copies.size(); ++i)
		remap.emplace(sources[i], copies[i]);

	auto lookup = [&remap](NodeId id) -> Option<NodeId> {
		auto found = remap.find(id);
		if (found == remap.end())
			return Some(NodeId{}); // référence sortante : neutralisée
		return Some(found->second);
	};
	for (NodeId id : copies) {
		Node* node = m_tree.Get(id);
		for (auto& [key, value] : node->properties.Entries())
			value.RemapNodeRefs(lookup);
		for (Component& component : node->components)
			for (auto& [key, value] : component.props.Entries())
				value.RemapNodeRefs(lookup);
	}
}

} // namespace scene
