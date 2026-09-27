// Définitions de scene/tree.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "scene/tree.hpp"

namespace scene {

// ── NodePath ─────────────────────────────────────────────────────────────────

NodePath NodePath::Parse(const String& text) {
	NodePath path;
	path.m_absolute = text.StartsWith("/");
	for (const String& segment : text.Split('/'))
		if (!segment.IsEmpty())
			path.m_segments.push_back(segment);
	return path;
}

String NodePath::ToString() const {
	String out(m_absolute ? "/" : "");
	for (size_t i = 0; i < m_segments.size(); ++i) {
		if (i > 0)
			out.Concat("/");
		out.Concat(m_segments[i]);
	}
	return out;
}

// ── ValidationReport ─────────────────────────────────────────────────────────

bool ValidationReport::Ok() const noexcept {
	for (const ValidationIssue& issue : issues)
		if (issue.level == IssueLevel::ERROR_LEVEL)
			return false;
	return true;
}

size_t ValidationReport::ErrorCount() const noexcept {
	size_t count = 0;
	for (const ValidationIssue& issue : issues)
		if (issue.level == IssueLevel::ERROR_LEVEL)
			++count;
	return count;
}

String ValidationReport::Format() const {
	String out;
	for (const ValidationIssue& issue : issues) {
		out.Concat(issue.level == IssueLevel::ERROR_LEVEL ? "[erreur] " : "[attention] ");
		out.Concat(issue.node.Valid() ? String::Format("%s : ", issue.node.ToText().CStr())
									  : String());
		out.Concat(issue.message);
		out.Concat("\n");
	}
	return out;
}

// ── NodeTree ─────────────────────────────────────────────────────────────────

NodeTree::NodeTree(String rootName, String rootType) {
	m_root = AllocateSlot();
	Node& node = SlotOf(m_root)->node;
	node.id = m_root;
	node.name = std::move(rootName);
	node.type = std::move(rootType);
}

Node* NodeTree::Get(NodeId id) noexcept {
	Slot* slot = SlotOf(id);
	return slot ? &slot->node : nullptr;
}

const Node* NodeTree::Get(NodeId id) const noexcept {
	const Slot* slot = SlotOf(id);
	return slot ? &slot->node : nullptr;
}

NodeId NodeTree::ParentOf(NodeId id) const noexcept {
	const Node* node = Get(id);
	return node ? node->parent : NodeId{};
}

const std::vector<NodeId>& NodeTree::ChildrenOf(NodeId id) const noexcept {
	const Node* node = Get(id);
	return node ? node->children : m_noChildren;
}

std::vector<NodeId> NodeTree::AllNodes() const {
	std::vector<NodeId> out;
	out.reserve(m_aliveCount);
	Traverse(m_root, [&](NodeId id, const Node&) { out.push_back(id); });
	return out;
}

NodeId NodeTree::Create(NodeId parent, String name, String type, size_t index) {
	Node node;
	node.name = std::move(name);
	node.type = std::move(type);
	return Add(parent, std::move(node), index);
}

NodeId NodeTree::Add(NodeId parent, Node node, size_t index) {
	Slot* parentSlot = SlotOf(parent);
	if (!parentSlot)
		return NodeId{};
	NodeId id = AllocateSlot();
	parentSlot = SlotOf(parent); // AllocateSlot a pu réallouer le vecteur
	Slot* slot = SlotOf(id);
	node.id = id;
	node.parent = parent;
	node.children.clear();
	slot->node = std::move(node);
	InsertChild(parentSlot->node.children, id, index);
	Touch(id);
	TouchStructure();
	return id;
}

bool NodeTree::Remove(NodeId id) {
	if (!Contains(id) || id == m_root)
		return false;
	NodeId parent = ParentOf(id);
	if (Slot* parentSlot = SlotOf(parent))
		EraseChild(parentSlot->node.children, id);
	std::vector<NodeId> doomed;
	Traverse(id, [&](NodeId child, const Node&) { doomed.push_back(child); });
	for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
		FreeSlot(*it);
	TouchStructure();
	return true;
}

bool NodeTree::RemoveChildren(NodeId id) {
	Slot* slot = SlotOf(id);
	if (!slot)
		return false;
	std::vector<NodeId> children = slot->node.children;
	for (NodeId child : children)
		(void)Remove(child);
	return true;
}

bool NodeTree::IsAncestorOf(NodeId ancestor, NodeId node) const noexcept {
	if (!ancestor.Valid())
		return false;
	for (NodeId cur = node; cur.Valid(); cur = ParentOf(cur))
		if (cur == ancestor)
			return true;
	return false;
}

size_t NodeTree::DepthOf(NodeId id) const noexcept {
	size_t depth = 0;
	for (NodeId cur = ParentOf(id); cur.Valid(); cur = ParentOf(cur))
		++depth;
	return depth;
}

bool NodeTree::Reparent(NodeId child, NodeId newParent, ReparentMode mode, size_t index) {
	if (child == m_root || !Contains(child) || !Contains(newParent))
		return false;
	if (child == newParent || IsAncestorOf(child, newParent))
		return false;

	const math::FMatrix4 worldBefore = GlobalMatrix(child);
	NodeId oldParent = ParentOf(child);
	if (Slot* oldSlot = SlotOf(oldParent))
		EraseChild(oldSlot->node.children, child);

	Slot* newSlot = SlotOf(newParent);
	InsertChild(newSlot->node.children, child, index);
	SlotOf(child)->node.parent = newParent;
	Touch(child);
	TouchStructure();

	if (mode == ReparentMode::KEEP_GLOBAL) {
		const math::FMatrix4 parentWorld = GlobalMatrix(newParent);
		SlotOf(child)->node.transform =
			Transform::FromMatrix(parentWorld.Inverse() * worldBefore);
		Touch(child);
	}
	return true;
}

bool NodeTree::MoveChild(NodeId child, size_t index) {
	NodeId parent = ParentOf(child);
	Slot* parentSlot = SlotOf(parent);
	if (!parentSlot)
		return false;
	std::vector<NodeId>& list = parentSlot->node.children;
	EraseChild(list, child);
	InsertChild(list, child, index);
	TouchStructure();
	return true;
}

bool NodeTree::Rename(NodeId id, String name) {
	Slot* slot = SlotOf(id);
	if (!slot)
		return false;
	slot->node.name = std::move(name);
	TouchStructure();
	return true;
}

String NodeTree::UniqueChildName(NodeId parent, const String& base) const {
	String wanted = base.IsEmpty() ? String("Node") : base;
	if (!FindChild(parent, wanted).Valid())
		return wanted;
	for (int suffix = 2; suffix < 100000; ++suffix) {
		String candidate = String::Format("%s %d", wanted.CStr(), suffix);
		if (!FindChild(parent, candidate).Valid())
			return candidate;
	}
	return wanted;
}

NodeId NodeTree::FindChild(NodeId parent, const String& name) const noexcept {
	for (NodeId child : ChildrenOf(parent))
		if (const Node* node = Get(child); node && node->name == name)
			return child;
	return NodeId{};
}

NodeId NodeTree::FindByName(const String& name, NodeId from) const {
	NodeId start = from.Valid() ? from : m_root;
	NodeId found;
	Traverse(start, [&](NodeId id, const Node& node) {
		if (!found.Valid() && node.name == name)
			found = id;
	});
	return found;
}

NodeId NodeTree::Resolve(const NodePath& path, NodeId base) const {
	NodeId current = path.IsAbsolute() ? m_root : (base.Valid() ? base : m_root);
	if (!Contains(current))
		return NodeId{};
	const std::vector<String>& segments = path.Segments();
	size_t first = 0;
	// `/Scene/Car` comme `/Car` : le premier segment d'un chemin absolu
	// peut nommer la racine elle-même.
	if (path.IsAbsolute() && !segments.empty())
		if (const Node* root = Get(m_root); root && root->name == segments[0])
			first = 1;
	for (size_t i = first; i < segments.size(); ++i) {
		const String& segment = segments[i];
		if (segment == ".")
			continue;
		if (segment == "..") {
			NodeId parent = ParentOf(current);
			current =
				parent.Valid() ? parent : current; // remonter au-delà de la racine y reste
			continue;
		}
		current = FindChild(current, segment);
		if (!current.Valid())
			return NodeId{};
	}
	return current;
}

NodeId NodeTree::Resolve(const String& path, NodeId base) const {
	return Resolve(NodePath::Parse(path), base);
}

String NodeTree::PathOf(NodeId id) const {
	if (!Contains(id))
		return String();
	std::vector<const Node*> chain;
	for (NodeId cur = id; cur.Valid(); cur = ParentOf(cur))
		chain.push_back(Get(cur));
	String out;
	for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
		out.Concat("/");
		out.Concat((*it)->name);
	}
	return out;
}

std::vector<NodeId> NodeTree::Descendants(NodeId from) const {
	std::vector<NodeId> out;
	Traverse(from, [&](NodeId id, const Node&) {
		if (id != from)
			out.push_back(id);
	});
	return out;
}

Transform NodeTree::LocalTransform(NodeId id) const {
	const Node* node = Get(id);
	return node ? node->transform : Transform{};
}

bool NodeTree::SetLocalTransform(NodeId id, const Transform& transform) {
	Slot* slot = SlotOf(id);
	if (!slot)
		return false;
	slot->node.transform = transform;
	Touch(id);
	return true;
}

bool NodeTree::SetLocalPosition(NodeId id, const math::FVector3& position) {
	Slot* slot = SlotOf(id);
	if (!slot)
		return false;
	slot->node.transform.position = position;
	Touch(id);
	return true;
}

bool NodeTree::SetLocalRotation(NodeId id, const math::FQuaternion& rotation) {
	Slot* slot = SlotOf(id);
	if (!slot)
		return false;
	slot->node.transform.rotation = rotation;
	Touch(id);
	return true;
}

bool NodeTree::SetLocalScale(NodeId id, const math::FVector3& scale) {
	Slot* slot = SlotOf(id);
	if (!slot)
		return false;
	slot->node.transform.scale = scale;
	Touch(id);
	return true;
}

const math::FMatrix4& NodeTree::GlobalMatrix(NodeId id) const {
	const Slot* slot = SlotOf(id);
	if (!slot)
		return m_identity;

	// Chaîne du nœud jusqu'au premier ancêtre, recalculée de haut en bas :
	// itératif plutôt que récursif pour ne rien devoir à la profondeur.
	m_chain.clear();
	for (NodeId cur = id; cur.Valid(); cur = ParentOf(cur))
		m_chain.push_back(cur);

	for (auto it = m_chain.rbegin(); it != m_chain.rend(); ++it) {
		const Node& node = SlotOf(*it)->node;
		Cache& cache = m_cache[it->index];
		if (!node.parent.Valid()) {
			if (cache.localDirty) {
				cache.world = node.transform.Matrix();
				cache.localDirty = false;
				++cache.worldEpoch;
			}
			continue;
		}
		const Cache& parentCache = m_cache[node.parent.index];
		if (cache.localDirty || cache.parentEpochSeen != parentCache.worldEpoch) {
			cache.world = parentCache.world * node.transform.Matrix();
			cache.parentEpochSeen = parentCache.worldEpoch;
			cache.localDirty = false;
			++cache.worldEpoch;
		}
	}
	return m_cache[id.index].world;
}

Transform NodeTree::GlobalTransform(NodeId id) const {
	return Transform::FromMatrix(GlobalMatrix(id));
}

math::FVector3 NodeTree::GlobalPosition(NodeId id) const {
	const math::FMatrix4& m = GlobalMatrix(id);
	return {m.m[12], m.m[13], m.m[14]};
}

bool NodeTree::SetGlobalTransform(NodeId id, const Transform& world) {
	const Node* node = Get(id);
	if (!node)
		return false;
	if (!node->parent.Valid())
		return SetLocalTransform(id, world);
	const math::FMatrix4 parentWorld = GlobalMatrix(node->parent);
	return SetLocalTransform(id, Transform::FromMatrix(parentWorld.Inverse() * world.Matrix()));
}

bool NodeTree::SetGlobalPosition(NodeId id, const math::FVector3& position) {
	Transform world = GlobalTransform(id);
	world.position = position;
	return SetGlobalTransform(id, world);
}

bool NodeTree::SetVisible(NodeId id, bool visible) {
	Slot* slot = SlotOf(id);
	if (!slot)
		return false;
	slot->node.visible = visible;
	return true;
}

bool NodeTree::IsVisibleInTree(NodeId id) const noexcept {
	for (NodeId cur = id; cur.Valid(); cur = ParentOf(cur))
		if (const Node* node = Get(cur); !node || !node->visible)
			return false;
	return true;
}

NodeId NodeTree::Duplicate(NodeId source, NodeId parent, size_t index) {
	if (!Contains(source) || source == m_root)
		return NodeId{};
	NodeId destination = parent.Valid() ? parent : ParentOf(source);
	if (!Contains(destination) || IsAncestorOf(source, destination))
		return NodeId{};

	std::vector<NodeId> originals;
	Traverse(source, [&](NodeId id, const Node&) { originals.push_back(id); });

	// Table de correspondance ancien -> neuf. Une recherche linéaire dans
	// un vecteur suffirait pour une poignée de nœuds, mais dupliquer un
	// sous-arbre de 10 000 nœuds la rendrait quadratique — et dupliquer
	// un gros gabarit est justement une opération courante.
	std::unordered_map<NodeId, NodeId> remap;
	remap.reserve(originals.size());
	NodeId newRoot;
	for (NodeId original : originals) {
		const Node* node = Get(original);
		Node copy = *node;
		copy.children.clear();
		NodeId newParent = destination;
		size_t insertAt = index;
		if (original != source) {
			auto found = remap.find(node->parent);
			if (found == remap.end())
				continue; // parent hors copie : impossible en parcours préfixe
			newParent = found->second;
			insertAt = NODE_APPEND;
		}
		NodeId created = Add(newParent, std::move(copy), insertAt);
		remap.emplace(original, created);
		if (original == source)
			newRoot = created;
	}

	auto lookup = [&remap](NodeId id) -> Option<NodeId> {
		auto found = remap.find(id);
		if (found == remap.end())
			return NONE;
		return Some(found->second);
	};
	for (const auto& [from, to] : remap) {
		Node* copy = Get(to);
		for (auto& [key, value] : copy->properties.Entries())
			value.RemapNodeRefs(lookup);
		for (Component& component : copy->components)
			for (auto& [key, value] : component.props.Entries())
				value.RemapNodeRefs(lookup);
	}
	return newRoot;
}

ValidationReport NodeTree::Validate(const std::function<bool(const String&)>& resourceExists,
		const std::function<bool(const String&)>& typeIsKnown) const {
	ValidationReport report;
	auto error = [&](NodeId id, String message) {
		report.issues.push_back({IssueLevel::ERROR_LEVEL, id, std::move(message)});
	};
	auto warn = [&](NodeId id, String message) {
		report.issues.push_back({IssueLevel::WARNING, id, std::move(message)});
	};

	// 1. Atteignabilité depuis la racine + cycles. Un cycle rend un nœud
	//    inatteignable ET fait boucler tout parcours : on borne donc le
	//    nombre de pas par le nombre de nœuds vivants.
	std::vector<bool> seen(m_slots.size(), false);
	size_t visited = 0;
	std::vector<NodeId> stack{m_root};
	while (!stack.empty() && visited <= m_aliveCount) {
		NodeId id = stack.back();
		stack.pop_back();
		const Slot* slot = SlotOf(id);
		if (!slot)
			continue;
		if (seen[id.index]) {
			error(
				id,
				String(
					"nœud atteint deux fois : l'arbre contient un cycle ou un enfant partagé"));
			continue;
		}
		seen[id.index] = true;
		++visited;
		for (NodeId child : slot->node.children)
			stack.push_back(child);
	}

	for (size_t i = 0; i < m_slots.size(); ++i) {
		const Slot& slot = m_slots[i];
		if (!slot.alive)
			continue;
		NodeId id = slot.node.id;
		if (!seen[i])
			error(id, String::Format("nœud « %s » inatteignable depuis la racine",
									 slot.node.name.CStr()));

		// 2. Parent existant et cohérent avec la liste d'enfants.
		if (id != m_root) {
			const Slot* parent = SlotOf(slot.node.parent);
			if (!parent)
				error(id,
					  String::Format("nœud « %s » : parent inexistant", slot.node.name.CStr()));
			else {
				size_t count = 0;
				for (NodeId child : parent->node.children)
					if (child == id)
						++count;
				if (count == 0)
					error(id, String::Format(
								  "nœud « %s » : absent de la liste d'enfants de son parent",
								  slot.node.name.CStr()));
				else if (count > 1)
					error(id, String::Format("nœud « %s » : listé %zu fois chez son parent",
											 slot.node.name.CStr(), count));
			}
		} else if (slot.node.parent.Valid()) {
			error(id, String("la racine ne doit pas avoir de parent"));
		}

		// 3. Enfants : existants, et se réclamant bien de ce parent.
		for (NodeId child : slot.node.children) {
			const Slot* childSlot = SlotOf(child);
			if (!childSlot)
				error(id, String::Format("nœud « %s » : enfant inexistant %s",
										 slot.node.name.CStr(), child.ToText().CStr()));
			else if (childSlot->node.parent != id)
				error(child,
					  String::Format(
						  "nœud « %s » : son parent déclaré n'est pas celui qui le liste",
						  childSlot->node.name.CStr()));
		}

		// 4. Transform exploitable (un NaN se propage à toute la scène).
		const Transform& transform = slot.node.transform;
		auto finite = [](float v) { return v == v && v < 1e30f && v > -1e30f; };
		if (!finite(transform.position.x) || !finite(transform.position.y) ||
			!finite(transform.position.z) || !finite(transform.scale.x) ||
			!finite(transform.scale.y) || !finite(transform.scale.z) ||
			!finite(transform.rotation.x) || !finite(transform.rotation.y) ||
			!finite(transform.rotation.z) || !finite(transform.rotation.w))
			error(id,
				  String::Format("nœud « %s » : transform non fini", slot.node.name.CStr()));
		if (transform.scale.x == 0.f || transform.scale.y == 0.f || transform.scale.z == 0.f)
			warn(id, String::Format("nœud « %s » : échelle nulle sur un axe",
									slot.node.name.CStr()));

		// 5. Nom vide (le doublon entre frères est vérifié plus bas, une
		//    seule fois par PARENT : le faire ici comparerait chaque nœud
		//    à toute sa fratrie, donc 100 millions de comparaisons pour
		//    un parent de 10 000 enfants — mesuré à 39 s avant).
		if (slot.node.name.IsEmpty())
			warn(id, String("nœud sans nom"));

		// 6. Type connu.
		if (typeIsKnown && !typeIsKnown(slot.node.type))
			warn(id, String::Format("type de nœud inconnu « %s » (conservé tel quel)",
									slot.node.type.CStr()));

		// 7. Références et ressources, propriétés du nœud comme des
		//    composants.
		auto checkValue = [&](const PropertyValue& value) {
			value.VisitNodeRefs([&](NodeId target) {
				if (target.Valid() && !Contains(target))
					error(id,
						  String::Format("nœud « %s » : référence vers un nœud disparu (%s)",
										 slot.node.name.CStr(), target.ToText().CStr()));
			});
			if (resourceExists && value.Type() == PropertyType::RESOURCE) {
				String path = value.AsString();
				if (!path.IsEmpty() && !resourceExists(path))
					error(id, String::Format("nœud « %s » : ressource introuvable « %s »",
											 slot.node.name.CStr(), path.CStr()));
			}
		};
		for (const auto& [key, value] : slot.node.properties.Entries())
			checkValue(value);
		for (const Component& component : slot.node.components)
			for (const auto& [key, value] : component.props.Entries())
				checkValue(value);
	}

	// 8. Noms partagés entre frères : une passe par parent.
	for (const Slot& slot : m_slots) {
		if (!slot.alive || slot.node.children.empty())
			continue;
		std::unordered_set<String> names;
		names.reserve(slot.node.children.size());
		for (NodeId child : slot.node.children) {
			const Node* node = Get(child);
			if (!node || node->name.IsEmpty())
				continue;
			if (!names.insert(node->name).second)
				warn(child,
					 String::Format(
						 "nom « %s » partagé avec un frère : les chemins deviennent ambigus",
						 node->name.CStr()));
		}
	}
	return report;
}

data::NodePtr NodeTree::ToJson() const {
	auto root = data::Node::MakeObject();
	root->Set("format", data::Node::MakeString(String("scene.tree")));
	root->Set("version", data::Node::MakeInt(FORMAT_VERSION));
	root->Set("root", NodeToJson(m_root));
	return root;
}

Result<NodeTree, String> NodeTree::FromJson(const data::NodePtr& json) {
	if (!json || !json->IsObject())
		return Err(String("arbre : objet JSON attendu"));
	auto version = json->Get("version");
	if (version && version->IsInt() && version->intValue > FORMAT_VERSION)
		return Err(String::Format("arbre : version de format %lld trop récente (max %d)",
								  static_cast<long long>(version->intValue), FORMAT_VERSION));
	auto rootJson = json->Get("root");
	if (!rootJson || !rootJson->IsObject())
		return Err(String("arbre : nœud racine manquant"));

	NodeTree tree;
	// Les identifiants du FICHIER sont conservés quand c'est raisonnable
	// (les références entre nœuds restent alors valides à l'identique
	// après un aller-retour, et un identifiant noté ailleurs — sélection
	// de l'éditeur, variable de script — survit au rechargement). Sinon
	// (indices absurdes venant d'un fichier trafiqué, doublons), on
	// renumérote et on re-câble les références.
	std::vector<Node> flat;
	std::vector<NodeId> parents;
	CollectJson(rootJson, NodeId{}, flat, parents);
	if (flat.empty())
		return Err(String("arbre : racine illisible"));

	uint32_t maxIndex = 0;
	bool duplicates = false;
	std::unordered_set<uint32_t> seen;
	seen.reserve(flat.size());
	for (const Node& node : flat) {
		if (!node.id.Valid()) {
			duplicates = true; // identifiant absent : renumérotation
			break;
		}
		maxIndex = node.id.index > maxIndex ? node.id.index : maxIndex;
		if (!seen.insert(node.id.index).second) {
			duplicates = true;
			break;
		}
	}
	const bool keepIds = !duplicates && maxIndex < flat.size() * 4 + 1024;
	if (keepIds)
		tree.ReserveSlots(maxIndex + 1);

	// Même raison que dans Duplicate : une table, pas une recherche
	// linéaire — sinon charger un fichier de 100 000 nœuds est quadratique
	// (mesuré : 513 s avant, cf. tests/scene_node_smoke_test.cpp).
	std::unordered_map<NodeId, NodeId> remap;
	remap.reserve(flat.size());
	for (size_t i = 0; i < flat.size(); ++i) {
		NodeId wanted = keepIds ? flat[i].id : NodeId{};
		NodeId created;
		if (i == 0) {
			created = tree.m_root;
			NodeId original = tree.m_root;
			Node root = flat[0];
			root.id = keepIds ? wanted : original;
			root.parent = NodeId{};
			root.children.clear();
			if (keepIds) {
				// La racine occupe déjà l'emplacement 0 ; si le fichier lui
				// donne un autre indice, on la déplace pour que TOUS les
				// identifiants du fichier soient respectés.
				created = tree.RelocateRoot(wanted);
				root.id = created;
			}
			*tree.Get(created) = std::move(root);
			tree.Touch(created);
		} else {
			auto found = remap.find(parents[i]);
			if (found == remap.end())
				return Err(String("arbre : parent introuvable pendant la lecture"));
			NodeId parent = found->second;
			Node node = flat[i];
			created = keepIds ? tree.AddAt(parent, std::move(node), wanted)
							  : tree.Add(parent, std::move(node));
			if (!created.Valid())
				return Err(String("arbre : identifiant de fichier inutilisable"));
		}
		remap.emplace(flat[i].id, created);
	}

	if (!keepIds) {
		auto lookup = [&remap](NodeId id) -> Option<NodeId> {
			auto found = remap.find(id);
			if (found == remap.end())
				return NONE;
			return Some(found->second);
		};
		for (const auto& [from, to] : remap) {
			Node* node = tree.Get(to);
			for (auto& [key, value] : node->properties.Entries())
				value.RemapNodeRefs(lookup);
			for (Component& component : node->components)
				for (auto& [key, value] : component.props.Entries())
					value.RemapNodeRefs(lookup);
		}
	}
	tree.RebuildFreeList(); // cf. ReserveSlots : les trous n'étaient pas chaînés
	return Ok(std::move(tree));
}

String NodeTree::EncodeJson() const {
	data::JsonDocument document;
	document.SetRoot(ToJson());
	return document.EncodeStr();
}

Result<NodeTree, String> NodeTree::DecodeJson(const String& text) {
	data::JsonDocument document;
	auto error = document.DecodeStr(text);
	if (error.IsSome())
		return Err(error.Unwrap().Format());
	return FromJson(document.GetRoot());
}

NodeTree::Slot* NodeTree::SlotOf(NodeId id) noexcept {
	if (!id.Valid() || id.index >= m_slots.size())
		return nullptr;
	Slot& slot = m_slots[id.index];
	return (slot.alive && slot.generation == id.generation) ? &slot : nullptr;
}

const NodeTree::Slot* NodeTree::SlotOf(NodeId id) const noexcept {
	if (!id.Valid() || id.index >= m_slots.size())
		return nullptr;
	const Slot& slot = m_slots[id.index];
	return (slot.alive && slot.generation == id.generation) ? &slot : nullptr;
}

NodeId NodeTree::AllocateSlot() {
	uint32_t index;
	if (m_freeHead != ~0u) {
		index = m_freeHead;
		m_freeHead = m_slots[index].nextFree;
	} else {
		index = uint32_t(m_slots.size());
		m_slots.push_back(Slot{});
		m_cache.push_back(Cache{});
	}
	Slot& slot = m_slots[index];
	slot.alive = true;
	slot.node = Node{};
	m_cache[index] = Cache{};
	++m_aliveCount;
	return NodeId{index, slot.generation};
}

NodeId NodeTree::AllocateSlotAt(NodeId wanted) {
	if (!wanted.Valid())
		return NodeId{};
	ReserveSlots(wanted.index + 1);
	Slot& slot = m_slots[wanted.index];
	if (slot.alive)
		return NodeId{};
	DetachFromFreeList(wanted.index);
	slot.alive = true;
	slot.generation = wanted.generation;
	slot.node = Node{};
	m_cache[wanted.index] = Cache{};
	++m_aliveCount;
	return NodeId{wanted.index, slot.generation};
}

NodeId NodeTree::AddAt(NodeId parent, Node node, NodeId wanted) {
	if (!SlotOf(parent))
		return NodeId{};
	NodeId id = AllocateSlotAt(wanted);
	if (!id.Valid())
		return NodeId{};
	Slot* slot = SlotOf(id);
	node.id = id;
	node.parent = parent;
	node.children.clear();
	slot->node = std::move(node);
	InsertChild(SlotOf(parent)->node.children, id, NODE_APPEND);
	Touch(id);
	TouchStructure();
	return id;
}

NodeId NodeTree::RelocateRoot(NodeId wanted) {
	if (!wanted.Valid() || wanted == m_root)
		return m_root;
	FreeSlot(m_root);
	NodeId placed = AllocateSlotAt(wanted);
	if (!placed.Valid()) {
		m_root = AllocateSlot();
		Get(m_root)->id = m_root;
		return m_root;
	}
	m_root = placed;
	Get(m_root)->id = placed;
	return placed;
}

void NodeTree::ReserveSlots(size_t count) {
	while (m_slots.size() < count) {
		Slot slot;
		slot.alive = false;
		slot.nextFree = ~0u;
		m_slots.push_back(std::move(slot));
		m_cache.push_back(Cache{});
	}
}

void NodeTree::RebuildFreeList() {
	m_freeHead = ~0u;
	for (size_t i = m_slots.size(); i-- > 0;) {
		if (m_slots[i].alive)
			continue;
		m_slots[i].nextFree = m_freeHead;
		m_freeHead = uint32_t(i);
	}
}

void NodeTree::DetachFromFreeList(uint32_t index) {
	if (m_freeHead == index) {
		m_freeHead = m_slots[index].nextFree;
		return;
	}
	for (uint32_t cur = m_freeHead; cur != ~0u; cur = m_slots[cur].nextFree)
		if (m_slots[cur].nextFree == index) {
			m_slots[cur].nextFree = m_slots[index].nextFree;
			return;
		}
}

void NodeTree::FreeSlot(NodeId id) {
	Slot* slot = SlotOf(id);
	if (!slot)
		return;
	slot->alive = false;
	++slot->generation; // tout identifiant conservé ailleurs devient périmé
	slot->node = Node{};
	slot->nextFree = m_freeHead;
	m_freeHead = id.index;
	--m_aliveCount;
}

uint64_t NodeTree::NextStructureStamp() noexcept {
	static std::atomic<uint64_t> counter{0};
	return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

void NodeTree::Touch(NodeId id) const {
	if (id.Valid() && id.index < m_cache.size())
		m_cache[id.index].localDirty = true;
}

void NodeTree::InsertChild(std::vector<NodeId>& list, NodeId child, size_t index) {
	if (index >= list.size())
		list.push_back(child);
	else
		list.insert(list.begin() + ptrdiff_t(index), child);
}

void NodeTree::EraseChild(std::vector<NodeId>& list, NodeId child) {
	for (size_t i = 0; i < list.size(); ++i)
		if (list[i] == child) {
			list.erase(list.begin() + ptrdiff_t(i));
			return;
		}
}

data::NodePtr NodeTree::NodeToJson(NodeId id) const {
	const Node* node = Get(id);
	if (!node)
		return data::Node::MakeObject();
	data::NodePtr json = node->ToJson();
	if (!node->children.empty()) {
		auto array = data::Node::MakeArray();
		for (NodeId child : node->children)
			array->Push(NodeToJson(child));
		json->Set("children", array);
	}
	return json;
}

void NodeTree::CollectJson(const data::NodePtr& json, NodeId parent, std::vector<Node>& flat,
		std::vector<NodeId>& parents) {
	if (!json || !json->IsObject())
		return;
	Node node = Node::FromJson(json);
	flat.push_back(node);
	parents.push_back(parent);
	if (auto children = json->Get("children"); children && children->IsArray())
		for (size_t i = 0; i < children->GetSize(); ++i)
			CollectJson(children->At(i), node.id, flat, parents);
}

} // namespace scene
