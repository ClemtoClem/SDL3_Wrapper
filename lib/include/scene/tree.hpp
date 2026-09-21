#pragma once
/**
 * scene::NodeTree — l'arbre de nœuds lui-même : stockage, hiérarchie,
 * transforms hérités, chemins, duplication, validation, sérialisation.
 *
 * ── Stockage : des emplacements à génération, pas des pointeurs ──────────
 * Les nœuds vivent dans un `std::vector` d'emplacements réutilisables, et un
 * `NodeId` est (indice, génération) — exactement la forme d'`ecs::Entity`.
 * Conséquences voulues :
 *  - accès en O(1) par identifiant, sans table de hachage ;
 *  - un identifiant conservé après suppression est DÉTECTÉ comme périmé (la
 *    génération ne correspond plus) au lieu de désigner le nœud qui a repris
 *    l'emplacement ;
 *  - l'arbre entier se copie en copiant un vecteur — c'est ce qui rend
 *    l'annulation par instantané et la duplication de sous-arbre simples.
 *
 * ── Transforms : cache invalidé en O(1), recalculé en O(profondeur) ──────
 * Écrire un transform ne parcourt PAS le sous-arbre pour le marquer sale
 * (déplacer la racine de 100 000 nœuds coûterait 100 000 écritures par image
 * de glissé). Chaque nœud retient une « époque » qui change quand son monde
 * change, et l'époque de son parent au moment où il a calculé le sien : à la
 * lecture, un désaccord suffit à savoir qu'il faut recalculer. Lire un
 * transform monde coûte donc la profondeur du nœud, écrire coûte une
 * affectation, et rien n'est jamais recalculé inutilement.
 */
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../core/core.hpp"
#include "../data/json.hpp"
#include "node.hpp"

namespace scene {

/// Position d'insertion « à la fin » pour les fonctions qui prennent un rang
/// dans la fratrie.
inline constexpr size_t NODE_APPEND = ~size_t(0);

// ============================================================================
// NodePath
// ============================================================================

/**
 * Chemin hiérarchique, absolu (`/Car/Wheels/FrontLeft`) ou relatif
 * (`../RearLeft`, `Wheels/FrontLeft`, `.`).
 *
 * Règles de résolution, énoncées ici une fois pour toutes :
 *  - un chemin commençant par `/` part de la RACINE de l'arbre ; le segment
 *    initial doit alors être le nom de la racine elle-même (`/Scene/...`), ou
 *    être omis (`/Car` cherche un enfant de la racine nommé `Car` si la
 *    racine ne s'appelle pas `Car`) — les deux écritures sont acceptées parce
 *    que les deux se lisent naturellement ;
 *  - un chemin relatif part du nœud `base` fourni à Resolve() ;
 *  - `.` désigne le nœud courant, `..` son parent (`..` à la racine reste la
 *    racine, plutôt que d'échouer : remonter trop haut est une erreur
 *    bénigne dans une hiérarchie qu'on réorganise) ;
 *  - un segment est comparé au NOM des enfants ; si plusieurs enfants
 *    portent le même nom, le PREMIER dans l'ordre de la fratrie gagne. Les
 *    noms ne sont donc pas des identifiants (cf. NodeId) et l'éditeur
 *    propose, sans l'imposer, un nom unique entre frères.
 */
class NodePath {
public:
	NodePath() = default;

	[[nodiscard]] static NodePath Parse(const String &text) {
		NodePath path;
		path.m_absolute = text.StartsWith("/");
		for (const String &segment : text.Split('/'))
			if (!segment.IsEmpty())
				path.m_segments.push_back(segment);
		return path;
	}

	[[nodiscard]] bool IsAbsolute() const noexcept { return m_absolute; }
	[[nodiscard]] bool IsEmpty() const noexcept { return m_segments.empty(); }
	[[nodiscard]] const std::vector<String> &Segments() const noexcept { return m_segments; }

	[[nodiscard]] String ToString() const {
		String out(m_absolute ? "/" : "");
		for (size_t i = 0; i < m_segments.size(); ++i) {
			if (i > 0)
				out.Concat("/");
			out.Concat(m_segments[i]);
		}
		return out;
	}

private:
	bool m_absolute = false;
	std::vector<String> m_segments;
};

// ============================================================================
// Validation
// ============================================================================

enum class IssueLevel : uint8_t { WARNING, ERROR_LEVEL };

struct ValidationIssue {
	IssueLevel level = IssueLevel::ERROR_LEVEL;
	NodeId node;
	String message;
};

struct ValidationReport {
	std::vector<ValidationIssue> issues;

	[[nodiscard]] bool Ok() const noexcept {
		for (const ValidationIssue &issue : issues)
			if (issue.level == IssueLevel::ERROR_LEVEL)
				return false;
		return true;
	}
	[[nodiscard]] size_t ErrorCount() const noexcept {
		size_t count = 0;
		for (const ValidationIssue &issue : issues)
			if (issue.level == IssueLevel::ERROR_LEVEL)
				++count;
		return count;
	}
	[[nodiscard]] size_t WarningCount() const noexcept { return issues.size() - ErrorCount(); }

	[[nodiscard]] String Format() const {
		String out;
		for (const ValidationIssue &issue : issues) {
			out.Concat(issue.level == IssueLevel::ERROR_LEVEL ? "[erreur] " : "[attention] ");
			out.Concat(issue.node.Valid() ? String::Format("%s : ", issue.node.ToText().CStr()) : String());
			out.Concat(issue.message);
			out.Concat("\n");
		}
		return out;
	}
};

// ============================================================================
// NodeTree
// ============================================================================

class NodeTree {
public:
	/// Un arbre neuf a TOUJOURS une racine (nommée `rootName`) : un arbre
	/// sans racine obligerait chaque appelant à traiter le cas « scène vide »
	/// séparément, pour aucun gain.
	explicit NodeTree(String rootName = String("Scene"), String rootType = String(node_type::NODE)) {
		m_root = AllocateSlot();
		Node &node = SlotOf(m_root)->node;
		node.id = m_root;
		node.name = std::move(rootName);
		node.type = std::move(rootType);
	}

	// ── Accès ────────────────────────────────────────────────────────────────

	[[nodiscard]] NodeId Root() const noexcept { return m_root; }
	[[nodiscard]] size_t Size() const noexcept { return m_aliveCount; }

	[[nodiscard]] bool Contains(NodeId id) const noexcept { return SlotOf(id) != nullptr; }

	[[nodiscard]] Node *Get(NodeId id) noexcept {
		Slot *slot = SlotOf(id);
		return slot ? &slot->node : nullptr;
	}
	[[nodiscard]] const Node *Get(NodeId id) const noexcept {
		const Slot *slot = SlotOf(id);
		return slot ? &slot->node : nullptr;
	}

	[[nodiscard]] NodeId ParentOf(NodeId id) const noexcept {
		const Node *node = Get(id);
		return node ? node->parent : NodeId{};
	}

	[[nodiscard]] const std::vector<NodeId> &ChildrenOf(NodeId id) const noexcept {
		const Node *node = Get(id);
		return node ? node->children : m_noChildren;
	}

	/// Tous les identifiants vivants, dans l'ordre de PARCOURS de l'arbre
	/// (préfixe) — l'ordre de l'outliner et du fichier.
	[[nodiscard]] std::vector<NodeId> AllNodes() const {
		std::vector<NodeId> out;
		out.reserve(m_aliveCount);
		Traverse(m_root, [&](NodeId id, const Node &) { out.push_back(id); });
		return out;
	}

	// ── Création / destruction ───────────────────────────────────────────────

	/// Crée un nœud enfant de `parent`. `index` choisit son rang dans la
	/// fratrie (NODE_APPEND = à la fin).
	NodeId Create(NodeId parent, String name, String type = String(node_type::NODE3D),
				  size_t index = NODE_APPEND) {
		Node node;
		node.name = std::move(name);
		node.type = std::move(type);
		return Add(parent, std::move(node), index);
	}

	/// Insère un nœud déjà rempli (ses `id`/`parent`/`children` sont
	/// ignorés et réécrits — l'arbre est seul maître des liens).
	NodeId Add(NodeId parent, Node node, size_t index = NODE_APPEND) {
		Slot *parentSlot = SlotOf(parent);
		if (!parentSlot)
			return NodeId{};
		NodeId id = AllocateSlot();
		parentSlot = SlotOf(parent); // AllocateSlot a pu réallouer le vecteur
		Slot *slot = SlotOf(id);
		node.id = id;
		node.parent = parent;
		node.children.clear();
		slot->node = std::move(node);
		InsertChild(parentSlot->node.children, id, index);
		Touch(id);
		return id;
	}

	/// Supprime `id` ET tout son sous-arbre. La racine ne peut pas être
	/// supprimée (l'arbre resterait sans point d'entrée) — videz-la plutôt.
	bool Remove(NodeId id) {
		if (!Contains(id) || id == m_root)
			return false;
		NodeId parent = ParentOf(id);
		if (Slot *parentSlot = SlotOf(parent))
			EraseChild(parentSlot->node.children, id);
		std::vector<NodeId> doomed;
		Traverse(id, [&](NodeId child, const Node &) { doomed.push_back(child); });
		for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
			FreeSlot(*it);
		return true;
	}

	/// Vide le sous-arbre de `id` sans supprimer `id`.
	bool RemoveChildren(NodeId id) {
		Slot *slot = SlotOf(id);
		if (!slot)
			return false;
		std::vector<NodeId> children = slot->node.children;
		for (NodeId child : children)
			(void)Remove(child);
		return true;
	}

	// ── Hiérarchie ───────────────────────────────────────────────────────────

	[[nodiscard]] bool IsAncestorOf(NodeId ancestor, NodeId node) const noexcept {
		if (!ancestor.Valid())
			return false;
		for (NodeId cur = node; cur.Valid(); cur = ParentOf(cur))
			if (cur == ancestor)
				return true;
		return false;
	}

	[[nodiscard]] size_t DepthOf(NodeId id) const noexcept {
		size_t depth = 0;
		for (NodeId cur = ParentOf(id); cur.Valid(); cur = ParentOf(cur))
			++depth;
		return depth;
	}

	/**
	 * Change le parent de `child`. Refusé — sans rien modifier — si le
	 * nouveau parent est `child` lui-même ou l'un de ses descendants : ce
	 * serait un cycle, c'est-à-dire un arbre qui n'en est plus un, et le
	 * détecter ici est la seule façon d'empêcher un glisser-déposer
	 * d'outliner de casser la scène.
	 *
	 * `mode` décide de ce qui est conservé (cf. ReparentMode) : KEEP_GLOBAL
	 * recalcule le transform local pour que le nœud ne bouge pas à l'écran.
	 */
	bool Reparent(NodeId child, NodeId newParent, ReparentMode mode = ReparentMode::KEEP_GLOBAL,
				  size_t index = NODE_APPEND) {
		if (child == m_root || !Contains(child) || !Contains(newParent))
			return false;
		if (child == newParent || IsAncestorOf(child, newParent))
			return false;

		const math::FMatrix4 worldBefore = GlobalMatrix(child);
		NodeId oldParent = ParentOf(child);
		if (Slot *oldSlot = SlotOf(oldParent))
			EraseChild(oldSlot->node.children, child);

		Slot *newSlot = SlotOf(newParent);
		InsertChild(newSlot->node.children, child, index);
		SlotOf(child)->node.parent = newParent;
		Touch(child);

		if (mode == ReparentMode::KEEP_GLOBAL) {
			const math::FMatrix4 parentWorld = GlobalMatrix(newParent);
			SlotOf(child)->node.transform = Transform::FromMatrix(parentWorld.Inverse() * worldBefore);
			Touch(child);
		}
		return true;
	}

	/// Déplace `child` au rang `index` dans SA fratrie (réordonnancement pur).
	bool MoveChild(NodeId child, size_t index) {
		NodeId parent = ParentOf(child);
		Slot *parentSlot = SlotOf(parent);
		if (!parentSlot)
			return false;
		std::vector<NodeId> &list = parentSlot->node.children;
		EraseChild(list, child);
		InsertChild(list, child, index);
		return true;
	}

	// ── Noms et chemins ──────────────────────────────────────────────────────

	bool Rename(NodeId id, String name) {
		Slot *slot = SlotOf(id);
		if (!slot)
			return false;
		slot->node.name = std::move(name);
		return true;
	}

	/// `base`, `base 2`, `base 3`… : le premier nom libre PARMI LES FRÈRES.
	/// L'unicité n'est imposée qu'entre frères, parce que c'est tout ce dont
	/// un chemin a besoin pour être sans ambiguïté.
	[[nodiscard]] String UniqueChildName(NodeId parent, const String &base) const {
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

	[[nodiscard]] NodeId FindChild(NodeId parent, const String &name) const noexcept {
		for (NodeId child : ChildrenOf(parent))
			if (const Node *node = Get(child); node && node->name == name)
				return child;
		return NodeId{};
	}

	/// Recherche récursive par nom dans le sous-arbre de `from` (parcours
	/// préfixe, premier trouvé).
	[[nodiscard]] NodeId FindByName(const String &name, NodeId from = NodeId{}) const {
		NodeId start = from.Valid() ? from : m_root;
		NodeId found;
		Traverse(start, [&](NodeId id, const Node &node) {
			if (!found.Valid() && node.name == name)
				found = id;
		});
		return found;
	}

	/// Résout un chemin (cf. NodePath pour les règles). `base` ne sert qu'aux
	/// chemins relatifs.
	[[nodiscard]] NodeId Resolve(const NodePath &path, NodeId base = NodeId{}) const {
		NodeId current = path.IsAbsolute() ? m_root : (base.Valid() ? base : m_root);
		if (!Contains(current))
			return NodeId{};
		const std::vector<String> &segments = path.Segments();
		size_t first = 0;
		// `/Scene/Car` comme `/Car` : le premier segment d'un chemin absolu
		// peut nommer la racine elle-même.
		if (path.IsAbsolute() && !segments.empty())
			if (const Node *root = Get(m_root); root && root->name == segments[0])
				first = 1;
		for (size_t i = first; i < segments.size(); ++i) {
			const String &segment = segments[i];
			if (segment == ".")
				continue;
			if (segment == "..") {
				NodeId parent = ParentOf(current);
				current = parent.Valid() ? parent : current; // remonter au-delà de la racine y reste
				continue;
			}
			current = FindChild(current, segment);
			if (!current.Valid())
				return NodeId{};
		}
		return current;
	}

	[[nodiscard]] NodeId Resolve(const String &path, NodeId base = NodeId{}) const {
		return Resolve(NodePath::Parse(path), base);
	}

	/// Chemin absolu de `id`, racine comprise (`/Scene/Car/Wheels`).
	[[nodiscard]] String PathOf(NodeId id) const {
		if (!Contains(id))
			return String();
		std::vector<const Node *> chain;
		for (NodeId cur = id; cur.Valid(); cur = ParentOf(cur))
			chain.push_back(Get(cur));
		String out;
		for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
			out.Concat("/");
			out.Concat((*it)->name);
		}
		return out;
	}

	// ── Parcours ─────────────────────────────────────────────────────────────

	/// Parcours préfixe (parent avant enfants, enfants dans l'ordre) —
	/// l'ordre d'affichage et de sérialisation. Itératif : un arbre profond
	/// ne doit pas dépendre de la pile d'appels.
	template <typename Visitor> void Traverse(NodeId from, const Visitor &visit) const {
		if (!Contains(from))
			return;
		std::vector<NodeId> stack{from};
		while (!stack.empty()) {
			NodeId id = stack.back();
			stack.pop_back();
			const Node *node = Get(id);
			if (!node)
				continue;
			visit(id, *node);
			for (auto it = node->children.rbegin(); it != node->children.rend(); ++it)
				stack.push_back(*it);
		}
	}

	[[nodiscard]] std::vector<NodeId> Descendants(NodeId from) const {
		std::vector<NodeId> out;
		Traverse(from, [&](NodeId id, const Node &) {
			if (id != from)
				out.push_back(id);
		});
		return out;
	}

	// ── Transforms ───────────────────────────────────────────────────────────

	[[nodiscard]] Transform LocalTransform(NodeId id) const {
		const Node *node = Get(id);
		return node ? node->transform : Transform{};
	}

	bool SetLocalTransform(NodeId id, const Transform &transform) {
		Slot *slot = SlotOf(id);
		if (!slot)
			return false;
		slot->node.transform = transform;
		Touch(id);
		return true;
	}

	bool SetLocalPosition(NodeId id, const math::FVector3 &position) {
		Slot *slot = SlotOf(id);
		if (!slot)
			return false;
		slot->node.transform.position = position;
		Touch(id);
		return true;
	}

	bool SetLocalRotation(NodeId id, const math::FQuaternion &rotation) {
		Slot *slot = SlotOf(id);
		if (!slot)
			return false;
		slot->node.transform.rotation = rotation;
		Touch(id);
		return true;
	}

	bool SetLocalScale(NodeId id, const math::FVector3 &scale) {
		Slot *slot = SlotOf(id);
		if (!slot)
			return false;
		slot->node.transform.scale = scale;
		Touch(id);
		return true;
	}

	/// Matrice monde : composition de toute la chaîne des parents, mise en
	/// cache (cf. en-tête du fichier).
	[[nodiscard]] const math::FMatrix4 &GlobalMatrix(NodeId id) const {
		const Slot *slot = SlotOf(id);
		if (!slot)
			return m_identity;

		// Chaîne du nœud jusqu'au premier ancêtre, recalculée de haut en bas :
		// itératif plutôt que récursif pour ne rien devoir à la profondeur.
		m_chain.clear();
		for (NodeId cur = id; cur.Valid(); cur = ParentOf(cur))
			m_chain.push_back(cur);

		for (auto it = m_chain.rbegin(); it != m_chain.rend(); ++it) {
			const Node &node = SlotOf(*it)->node;
			Cache &cache = m_cache[it->index];
			if (!node.parent.Valid()) {
				if (cache.localDirty) {
					cache.world = node.transform.Matrix();
					cache.localDirty = false;
					++cache.worldEpoch;
				}
				continue;
			}
			const Cache &parentCache = m_cache[node.parent.index];
			if (cache.localDirty || cache.parentEpochSeen != parentCache.worldEpoch) {
				cache.world = parentCache.world * node.transform.Matrix();
				cache.parentEpochSeen = parentCache.worldEpoch;
				cache.localDirty = false;
				++cache.worldEpoch;
			}
		}
		return m_cache[id.index].world;
	}

	[[nodiscard]] Transform GlobalTransform(NodeId id) const { return Transform::FromMatrix(GlobalMatrix(id)); }

	[[nodiscard]] math::FVector3 GlobalPosition(NodeId id) const {
		const math::FMatrix4 &m = GlobalMatrix(id);
		return {m.m[12], m.m[13], m.m[14]};
	}

	/// Pose le transform MONDE : le local est recalculé à partir de celui du
	/// parent. C'est ce dont se servent le manipulateur 3D (qui raisonne en
	/// monde) et la synchronisation depuis la physique (qui ne connaît que le
	/// monde).
	bool SetGlobalTransform(NodeId id, const Transform &world) {
		const Node *node = Get(id);
		if (!node)
			return false;
		if (!node->parent.Valid())
			return SetLocalTransform(id, world);
		const math::FMatrix4 parentWorld = GlobalMatrix(node->parent);
		return SetLocalTransform(id, Transform::FromMatrix(parentWorld.Inverse() * world.Matrix()));
	}

	bool SetGlobalPosition(NodeId id, const math::FVector3 &position) {
		Transform world = GlobalTransform(id);
		world.position = position;
		return SetGlobalTransform(id, world);
	}

	// ── Visibilité ───────────────────────────────────────────────────────────

	bool SetVisible(NodeId id, bool visible) {
		Slot *slot = SlotOf(id);
		if (!slot)
			return false;
		slot->node.visible = visible;
		return true;
	}

	/// Visibilité EFFECTIVE : un nœud visible sous un parent caché ne se voit
	/// pas. Même règle que le rendu (render3d::Object3D::Traverse saute les
	/// sous-arbres invisibles).
	[[nodiscard]] bool IsVisibleInTree(NodeId id) const noexcept {
		for (NodeId cur = id; cur.Valid(); cur = ParentOf(cur))
			if (const Node *node = Get(cur); !node || !node->visible)
				return false;
		return true;
	}

	// ── Duplication ──────────────────────────────────────────────────────────

	/**
	 * Copie `source` et tout son sous-arbre sous `parent` (par défaut : le
	 * parent de `source`, donc « dupliquer à côté »).
	 *
	 * Les identifiants sont NEUFS, et les références entre nœuds sont
	 * re-câblées selon une règle simple : une référence qui pointe DANS le
	 * sous-arbre copié suit la copie ; une référence qui en sort continue de
	 * désigner sa cible d'origine. C'est ce qui fait qu'un duplicata de
	 * voiture vise sa propre caméra, mais garde le même circuit comme cible.
	 */
	NodeId Duplicate(NodeId source, NodeId parent = NodeId{}, size_t index = NODE_APPEND) {
		if (!Contains(source) || source == m_root)
			return NodeId{};
		NodeId destination = parent.Valid() ? parent : ParentOf(source);
		if (!Contains(destination) || IsAncestorOf(source, destination))
			return NodeId{};

		std::vector<NodeId> originals;
		Traverse(source, [&](NodeId id, const Node &) { originals.push_back(id); });

		// Table de correspondance ancien -> neuf. Une recherche linéaire dans
		// un vecteur suffirait pour une poignée de nœuds, mais dupliquer un
		// sous-arbre de 10 000 nœuds la rendrait quadratique — et dupliquer
		// un gros gabarit est justement une opération courante.
		std::unordered_map<NodeId, NodeId> remap;
		remap.reserve(originals.size());
		NodeId newRoot;
		for (NodeId original : originals) {
			const Node *node = Get(original);
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
		for (const auto &[from, to] : remap) {
			Node *copy = Get(to);
			for (auto &[key, value] : copy->properties.Entries())
				value.RemapNodeRefs(lookup);
			for (Component &component : copy->components)
				for (auto &[key, value] : component.props.Entries())
					value.RemapNodeRefs(lookup);
		}
		return newRoot;
	}

	// ── Validation ───────────────────────────────────────────────────────────

	/**
	 * Vérifie l'intégrité de l'arbre. Ce qui est cherché, et pourquoi c'est
	 * cherché ICI plutôt que laissé aux invariants : un arbre peut arriver
	 * d'un FICHIER écrit à la main ou par un autre outil, où rien ne garantit
	 * ces propriétés.
	 *
	 * `resourceExists` (facultatif) permet de signaler une ressource
	 * manquante sans que ce fichier ait à connaître le système de fichiers ;
	 * `typeIsKnown` (facultatif) fait de même pour les types de nœuds, qui
	 * sont déclarés par l'application.
	 */
	[[nodiscard]] ValidationReport Validate(
		const std::function<bool(const String &)> &resourceExists = {},
		const std::function<bool(const String &)> &typeIsKnown = {}) const {
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
			const Slot *slot = SlotOf(id);
			if (!slot)
				continue;
			if (seen[id.index]) {
				error(id, String("nœud atteint deux fois : l'arbre contient un cycle ou un enfant partagé"));
				continue;
			}
			seen[id.index] = true;
			++visited;
			for (NodeId child : slot->node.children)
				stack.push_back(child);
		}

		for (size_t i = 0; i < m_slots.size(); ++i) {
			const Slot &slot = m_slots[i];
			if (!slot.alive)
				continue;
			NodeId id = slot.node.id;
			if (!seen[i])
				error(id, String::Format("nœud « %s » inatteignable depuis la racine", slot.node.name.CStr()));

			// 2. Parent existant et cohérent avec la liste d'enfants.
			if (id != m_root) {
				const Slot *parent = SlotOf(slot.node.parent);
				if (!parent)
					error(id, String::Format("nœud « %s » : parent inexistant", slot.node.name.CStr()));
				else {
					size_t count = 0;
					for (NodeId child : parent->node.children)
						if (child == id)
							++count;
					if (count == 0)
						error(id, String::Format("nœud « %s » : absent de la liste d'enfants de son parent",
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
				const Slot *childSlot = SlotOf(child);
				if (!childSlot)
					error(id, String::Format("nœud « %s » : enfant inexistant %s", slot.node.name.CStr(),
											 child.ToText().CStr()));
				else if (childSlot->node.parent != id)
					error(child, String::Format("nœud « %s » : son parent déclaré n'est pas celui qui le liste",
												childSlot->node.name.CStr()));
			}

			// 4. Transform exploitable (un NaN se propage à toute la scène).
			const Transform &transform = slot.node.transform;
			auto finite = [](float v) { return v == v && v < 1e30f && v > -1e30f; };
			if (!finite(transform.position.x) || !finite(transform.position.y) || !finite(transform.position.z) ||
				!finite(transform.scale.x) || !finite(transform.scale.y) || !finite(transform.scale.z) ||
				!finite(transform.rotation.x) || !finite(transform.rotation.y) || !finite(transform.rotation.z) ||
				!finite(transform.rotation.w))
				error(id, String::Format("nœud « %s » : transform non fini", slot.node.name.CStr()));
			if (transform.scale.x == 0.f || transform.scale.y == 0.f || transform.scale.z == 0.f)
				warn(id, String::Format("nœud « %s » : échelle nulle sur un axe", slot.node.name.CStr()));

			// 5. Nom vide (le doublon entre frères est vérifié plus bas, une
			//    seule fois par PARENT : le faire ici comparerait chaque nœud
			//    à toute sa fratrie, donc 100 millions de comparaisons pour
			//    un parent de 10 000 enfants — mesuré à 39 s avant).
			if (slot.node.name.IsEmpty())
				warn(id, String("nœud sans nom"));

			// 6. Type connu.
			if (typeIsKnown && !typeIsKnown(slot.node.type))
				warn(id, String::Format("type de nœud inconnu « %s » (conservé tel quel)", slot.node.type.CStr()));

			// 7. Références et ressources, propriétés du nœud comme des
			//    composants.
			auto checkValue = [&](const PropertyValue &value) {
				value.VisitNodeRefs([&](NodeId target) {
					if (target.Valid() && !Contains(target))
						error(id, String::Format("nœud « %s » : référence vers un nœud disparu (%s)",
												 slot.node.name.CStr(), target.ToText().CStr()));
				});
				if (resourceExists && value.Type() == PropertyType::RESOURCE) {
					String path = value.AsString();
					if (!path.IsEmpty() && !resourceExists(path))
						error(id, String::Format("nœud « %s » : ressource introuvable « %s »",
												 slot.node.name.CStr(), path.CStr()));
				}
			};
			for (const auto &[key, value] : slot.node.properties.Entries())
				checkValue(value);
			for (const Component &component : slot.node.components)
				for (const auto &[key, value] : component.props.Entries())
					checkValue(value);
		}

		// 8. Noms partagés entre frères : une passe par parent.
		for (const Slot &slot : m_slots) {
			if (!slot.alive || slot.node.children.empty())
				continue;
			std::unordered_set<String> names;
			names.reserve(slot.node.children.size());
			for (NodeId child : slot.node.children) {
				const Node *node = Get(child);
				if (!node || node->name.IsEmpty())
					continue;
				if (!names.insert(node->name).second)
					warn(child, String::Format("nom « %s » partagé avec un frère : les chemins deviennent ambigus",
											   node->name.CStr()));
			}
		}
		return report;
	}

	// ── Sérialisation ────────────────────────────────────────────────────────

	/// Forme IMBRIQUÉE : un nœud porte ses enfants. Un fichier de scène se lit
	/// alors comme l'arbre qu'il décrit, et une branche se copie-colle à la
	/// main d'un fichier à l'autre.
	[[nodiscard]] data::NodePtr ToJson() const {
		auto root = data::Node::MakeObject();
		root->Set("format", data::Node::MakeString(String("scene.tree")));
		root->Set("version", data::Node::MakeInt(FORMAT_VERSION));
		root->Set("root", NodeToJson(m_root));
		return root;
	}

	[[nodiscard]] static Result<NodeTree, String> FromJson(const data::NodePtr &json) {
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
		for (const Node &node : flat) {
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
				created = keepIds ? tree.AddAt(parent, std::move(node), wanted) : tree.Add(parent, std::move(node));
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
			for (const auto &[from, to] : remap) {
				Node *node = tree.Get(to);
				for (auto &[key, value] : node->properties.Entries())
					value.RemapNodeRefs(lookup);
				for (Component &component : node->components)
					for (auto &[key, value] : component.props.Entries())
						value.RemapNodeRefs(lookup);
			}
		}
		tree.RebuildFreeList(); // cf. ReserveSlots : les trous n'étaient pas chaînés
		return Ok(std::move(tree));
	}

	[[nodiscard]] String EncodeJson() const {
		data::JsonDocument document;
		document.SetRoot(ToJson());
		return document.EncodeStr();
	}

	[[nodiscard]] static Result<NodeTree, String> DecodeJson(const String &text) {
		data::JsonDocument document;
		auto error = document.DecodeStr(text);
		if (error.IsSome())
			return Err(error.Unwrap().Format());
		return FromJson(document.GetRoot());
	}

	static constexpr int FORMAT_VERSION = 1;

	/// Égalité STRUCTURELLE : mêmes nœuds, mêmes liens, mêmes contenus, dans
	/// le même ordre. Utilisé par les tests d'aller-retour.
	[[nodiscard]] bool operator==(const NodeTree &other) const {
		if (m_aliveCount != other.m_aliveCount)
			return false;
		std::vector<NodeId> mine = AllNodes();
		std::vector<NodeId> theirs = other.AllNodes();
		if (mine.size() != theirs.size())
			return false;
		for (size_t i = 0; i < mine.size(); ++i)
			if (*Get(mine[i]) != *other.Get(theirs[i]))
				return false;
		return true;
	}
	[[nodiscard]] bool operator!=(const NodeTree &other) const { return !(*this == other); }

private:
	struct Slot {
		Node node;
		uint32_t generation = 0;
		uint32_t nextFree = ~0u;
		bool alive = false;
	};

	/// Cache de transform monde (cf. en-tête). `mutable` : lire une matrice
	/// monde est conceptuellement une lecture, même si elle remplit le cache.
	struct Cache {
		math::FMatrix4 world = math::FMatrix4::Identity();
		uint64_t worldEpoch = 1;
		uint64_t parentEpochSeen = 0;
		bool localDirty = true;
	};

	[[nodiscard]] Slot *SlotOf(NodeId id) noexcept {
		if (!id.Valid() || id.index >= m_slots.size())
			return nullptr;
		Slot &slot = m_slots[id.index];
		return (slot.alive && slot.generation == id.generation) ? &slot : nullptr;
	}
	[[nodiscard]] const Slot *SlotOf(NodeId id) const noexcept {
		if (!id.Valid() || id.index >= m_slots.size())
			return nullptr;
		const Slot &slot = m_slots[id.index];
		return (slot.alive && slot.generation == id.generation) ? &slot : nullptr;
	}

	NodeId AllocateSlot() {
		uint32_t index;
		if (m_freeHead != ~0u) {
			index = m_freeHead;
			m_freeHead = m_slots[index].nextFree;
		} else {
			index = uint32_t(m_slots.size());
			m_slots.push_back(Slot{});
			m_cache.push_back(Cache{});
		}
		Slot &slot = m_slots[index];
		slot.alive = true;
		slot.node = Node{};
		m_cache[index] = Cache{};
		++m_aliveCount;
		return NodeId{index, slot.generation};
	}

	/// Alloue un emplacement PRÉCIS (lecture d'un fichier qui conserve les
	/// identifiants) — échoue si l'emplacement est déjà pris.
	NodeId AllocateSlotAt(NodeId wanted) {
		if (!wanted.Valid())
			return NodeId{};
		ReserveSlots(wanted.index + 1);
		Slot &slot = m_slots[wanted.index];
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

	NodeId AddAt(NodeId parent, Node node, NodeId wanted) {
		if (!SlotOf(parent))
			return NodeId{};
		NodeId id = AllocateSlotAt(wanted);
		if (!id.Valid())
			return NodeId{};
		Slot *slot = SlotOf(id);
		node.id = id;
		node.parent = parent;
		node.children.clear();
		slot->node = std::move(node);
		InsertChild(SlotOf(parent)->node.children, id, NODE_APPEND);
		Touch(id);
		return id;
	}

	/// Déplace la racine fraîchement créée vers l'emplacement voulu par un
	/// fichier (cf. FromJson) — l'arbre est vide à cet instant, donc rien
	/// d'autre n'y renvoie.
	NodeId RelocateRoot(NodeId wanted) {
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

	/// Agrandit le tableau d'emplacements SANS chaîner les nouveaux dans la
	/// liste des libres. C'est réservé à la lecture d'un fichier qui conserve
	/// les identifiants : les chaîner obligerait AllocateSlotAt à parcourir
	/// cette liste pour en retirer l'emplacement voulu, ce qui rend le
	/// chargement quadratique (mesuré : 157 s pour 100 000 nœuds). La liste
	/// est reconstruite une fois à la fin, par RebuildFreeList().
	void ReserveSlots(size_t count) {
		while (m_slots.size() < count) {
			Slot slot;
			slot.alive = false;
			slot.nextFree = ~0u;
			m_slots.push_back(std::move(slot));
			m_cache.push_back(Cache{});
		}
	}

	/// Re-chaîne tous les emplacements morts, des indices hauts vers les bas
	/// (l'allocation repart donc du plus petit indice libre, ce qui garde le
	/// tableau compact).
	void RebuildFreeList() {
		m_freeHead = ~0u;
		for (size_t i = m_slots.size(); i-- > 0;) {
			if (m_slots[i].alive)
				continue;
			m_slots[i].nextFree = m_freeHead;
			m_freeHead = uint32_t(i);
		}
	}

	void DetachFromFreeList(uint32_t index) {
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

	void FreeSlot(NodeId id) {
		Slot *slot = SlotOf(id);
		if (!slot)
			return;
		slot->alive = false;
		++slot->generation; // tout identifiant conservé ailleurs devient périmé
		slot->node = Node{};
		slot->nextFree = m_freeHead;
		m_freeHead = id.index;
		--m_aliveCount;
	}

	/// Marque le transform monde de `id` à recalculer. O(1) : les descendants
	/// s'en aperçoivent par comparaison d'époque (cf. en-tête).
	void Touch(NodeId id) const {
		if (id.Valid() && id.index < m_cache.size())
			m_cache[id.index].localDirty = true;
	}

	static void InsertChild(std::vector<NodeId> &list, NodeId child, size_t index) {
		if (index >= list.size())
			list.push_back(child);
		else
			list.insert(list.begin() + ptrdiff_t(index), child);
	}

	static void EraseChild(std::vector<NodeId> &list, NodeId child) {
		for (size_t i = 0; i < list.size(); ++i)
			if (list[i] == child) {
				list.erase(list.begin() + ptrdiff_t(i));
				return;
			}
	}

	[[nodiscard]] data::NodePtr NodeToJson(NodeId id) const {
		const Node *node = Get(id);
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

	/// Aplatit l'arbre JSON en (nœud, parent d'origine) dans l'ordre préfixe.
	static void CollectJson(const data::NodePtr &json, NodeId parent, std::vector<Node> &flat,
							std::vector<NodeId> &parents) {
		if (!json || !json->IsObject())
			return;
		Node node = Node::FromJson(json);
		flat.push_back(node);
		parents.push_back(parent);
		if (auto children = json->Get("children"); children && children->IsArray())
			for (size_t i = 0; i < children->GetSize(); ++i)
				CollectJson(children->At(i), node.id, flat, parents);
	}

	std::vector<Slot> m_slots;
	mutable std::vector<Cache> m_cache;
	mutable std::vector<NodeId> m_chain; ///< tampon de GlobalMatrix (évite une allocation par appel)
	uint32_t m_freeHead = ~0u;
	size_t m_aliveCount = 0;
	NodeId m_root;
	std::vector<NodeId> m_noChildren; ///< renvoyé par ChildrenOf pour un identifiant inconnu
	math::FMatrix4 m_identity = math::FMatrix4::Identity();
};

} // namespace scene
