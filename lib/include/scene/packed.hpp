#pragma once
/**
 * scene::PackedScene — une hiérarchie RÉUTILISABLE, et son instanciation.
 *
 *     Car.tscene                     RaceTrack
 *     ├── Body                       ├── Car01   ─┐
 *     ├── Wheels                     ├── Car02   ─┼─ trois instances de
 *     │   ├── FrontLeft              └── Car03   ─┘  la MÊME définition
 *     │   └── …
 *     └── Camera
 *
 * ── Ce qu'une instance est, et ce qu'elle n'est pas ──────────────────────
 * Instancier COPIE l'arbre de la scène source dans l'arbre cible, avec des
 * identifiants neufs et les références internes re-câblées (exactement
 * NodeTree::Duplicate, déjà éprouvé). Ce n'est donc pas un lien vivant : une
 * modification ultérieure du fichier source ne se propage pas d'elle-même aux
 * instances déjà posées.
 *
 * C'est un choix, pas un oubli. Un lien vivant impose de savoir, pour chaque
 * valeur d'une instance, si elle vient de la source ou si l'utilisateur l'a
 * redéfinie localement — c'est le « système d'override » que la spécification
 * range elle-même dans les suites possibles. La racine de chaque instance
 * garde donc une trace de sa provenance (composant `SceneInstance`, ci-dessous)
 * pour que l'éditeur l'affiche, et pour qu'un tel système puisse être ajouté
 * plus tard sans changer le format : les instances existantes sauront déjà
 * d'où elles viennent.
 */
#include "tree.hpp"

namespace scene {

/// Types de composants connus de la bibliothèque elle-même (tous les autres
/// sont déclarés par l'application, cf. type_registry.hpp).
namespace component_type {
/// Posé sur la RACINE d'une instance : mémorise la scène d'origine.
/// Propriétés : `source` (RESOURCE — chemin du fichier .tscene).
inline constexpr const char* SCENE_INSTANCE = "SceneInstance";
} // namespace component_type

class PackedScene {
public:
	PackedScene() = default;

	/// Emballe le sous-arbre `root` (de `tree`) en scène réutilisable. La
	/// racine de la scène emballée est une COPIE de `root` : son transform
	/// local est conservé et sert de transform par défaut à chaque instance.
	[[nodiscard]] static PackedScene FromSubtree(const NodeTree& tree, NodeId root) {
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

	[[nodiscard]] bool IsEmpty() const noexcept {
		return m_tree.Size() <= 1 && m_tree.ChildrenOf(m_tree.Root()).empty();
	}
	[[nodiscard]] const NodeTree& Tree() const noexcept { return m_tree; }
	[[nodiscard]] NodeTree& Tree() noexcept { return m_tree; }

	/// Chemin d'où vient cette scène (posé par l'application au chargement) —
	/// recopié dans le composant `SceneInstance` de chaque instance.
	[[nodiscard]] const String& Source() const noexcept { return m_source; }
	void SetSource(String source) { m_source = std::move(source); }

	/**
	 * Instancie la scène sous `parent` dans `target`. Le nœud racine créé
	 * reçoit `name` (ou le nom de la scène si `name` est vide), un nom rendu
	 * unique entre ses frères, et le composant `SceneInstance`.
	 *
	 * Rend l'identifiant de la racine de l'instance, invalide si `parent`
	 * n'existe pas.
	 */
	NodeId InstantiateInto(NodeTree& target, NodeId parent, const String& name = String(),
						   size_t index = NODE_APPEND) const {
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

	/// Vrai si ce nœud est la racine d'une instance de scène.
	[[nodiscard]] static bool IsInstanceRoot(const Node& node) noexcept {
		return node.HasComponent(String(component_type::SCENE_INSTANCE));
	}

	/// Chemin de la scène dont ce nœud est une instance (vide sinon).
	[[nodiscard]] static String InstanceSource(const Node& node) {
		const PropertyValue* source =
			node.ComponentProperty(String(component_type::SCENE_INSTANCE), String("source"));
		return source ? source->AsString() : String();
	}

	// ── Sérialisation ────────────────────────────────────────────────────────

	[[nodiscard]] data::NodePtr ToJson() const {
		auto json = m_tree.ToJson();
		json->Set("format", data::Node::MakeString(String("scene.packed")));
		if (!m_source.IsEmpty())
			json->Set("source", data::Node::MakeString(m_source));
		return json;
	}

	[[nodiscard]] static Result<PackedScene, String> FromJson(const data::NodePtr& json) {
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

	[[nodiscard]] String EncodeJson() const {
		data::JsonDocument document;
		document.SetRoot(ToJson());
		return document.EncodeStr();
	}

	[[nodiscard]] static Result<PackedScene, String> DecodeJson(const String& text) {
		data::JsonDocument document;
		auto error = document.DecodeStr(text);
		if (error.IsSome())
			return Err(error.Unwrap().Format());
		return FromJson(document.GetRoot());
	}

private:
	static void CopyChildren(const NodeTree& from, NodeId fromNode, NodeTree& to, NodeId toNode) {
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

	/// Après emballage, les références internes pointent encore sur les
	/// identifiants de l'arbre D'ORIGINE : on les redirige vers les copies.
	/// Une référence qui sortait du sous-arbre est effacée (elle désignerait
	/// un nœud absent de la scène emballée, donc une référence cassée dès la
	/// première instanciation).
	void RemapInternalRefs(const NodeTree& from, NodeId fromRoot) {
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

	NodeTree m_tree{String("Scene")};
	String m_source;
};

} // namespace scene
