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
#include "../core/core.hpp"
#include "../data/json.hpp"
#include "node.hpp"

#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

	[[nodiscard]] static NodePath Parse(const String& text);

	[[nodiscard]] bool IsAbsolute() const noexcept { return m_absolute; }
	[[nodiscard]] bool IsEmpty() const noexcept { return m_segments.empty(); }
	[[nodiscard]] const std::vector<String>& Segments() const noexcept { return m_segments; }

	[[nodiscard]] String ToString() const;

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

	[[nodiscard]] bool Ok() const noexcept;
	[[nodiscard]] size_t ErrorCount() const noexcept;
	[[nodiscard]] size_t WarningCount() const noexcept { return issues.size() - ErrorCount(); }

	[[nodiscard]] String Format() const;
};

// ============================================================================
// NodeTree
// ============================================================================

class NodeTree {
public:
	/// Un arbre neuf a TOUJOURS une racine (nommée `rootName`) : un arbre
	/// sans racine obligerait chaque appelant à traiter le cas « scène vide »
	/// séparément, pour aucun gain.
	explicit NodeTree(String rootName = String("Scene"),
					  String rootType = String(node_type::NODE));

	// ── Accès ────────────────────────────────────────────────────────────────

	[[nodiscard]] NodeId Root() const noexcept { return m_root; }
	[[nodiscard]] size_t Size() const noexcept { return m_aliveCount; }

	[[nodiscard]] bool Contains(NodeId id) const noexcept { return SlotOf(id) != nullptr; }

	[[nodiscard]] Node* Get(NodeId id) noexcept;
	[[nodiscard]] const Node* Get(NodeId id) const noexcept;

	[[nodiscard]] NodeId ParentOf(NodeId id) const noexcept;

	[[nodiscard]] const std::vector<NodeId>& ChildrenOf(NodeId id) const noexcept;

	/// Tous les identifiants vivants, dans l'ordre de PARCOURS de l'arbre
	/// (préfixe) — l'ordre de l'outliner et du fichier.
	[[nodiscard]] std::vector<NodeId> AllNodes() const;

	// ── Création / destruction ───────────────────────────────────────────────

	/// Crée un nœud enfant de `parent`. `index` choisit son rang dans la
	/// fratrie (NODE_APPEND = à la fin).
	NodeId Create(NodeId parent, String name, String type = String(node_type::NODE3D),
				  size_t index = NODE_APPEND);

	/// Insère un nœud déjà rempli (ses `id`/`parent`/`children` sont
	/// ignorés et réécrits — l'arbre est seul maître des liens).
	NodeId Add(NodeId parent, Node node, size_t index = NODE_APPEND);

	/// Supprime `id` ET tout son sous-arbre. La racine ne peut pas être
	/// supprimée (l'arbre resterait sans point d'entrée) — videz-la plutôt.
	bool Remove(NodeId id);

	/// Vide le sous-arbre de `id` sans supprimer `id`.
	bool RemoveChildren(NodeId id);

	// ── Hiérarchie ───────────────────────────────────────────────────────────

	[[nodiscard]] bool IsAncestorOf(NodeId ancestor, NodeId node) const noexcept;

	[[nodiscard]] size_t DepthOf(NodeId id) const noexcept;

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
				  size_t index = NODE_APPEND);

	/// Déplace `child` au rang `index` dans SA fratrie (réordonnancement pur).
	bool MoveChild(NodeId child, size_t index);

	// ── Noms et chemins ──────────────────────────────────────────────────────

	bool Rename(NodeId id, String name);

	/**
	 * Tampon de STRUCTURE : change à chaque ajout, suppression, reparentage,
	 * réordonnancement ou renommage (via cette classe). Il est tiré d'un
	 * compteur global au processus, si bien que deux arbres — ou un arbre et
	 * une copie — n'ont le même tampon que s'ils sont dans le même état
	 * structurel : un index externe (noms → identifiants, lignes d'un
	 * outliner…) se valide donc par une simple comparaison, sans que chaque
	 * site de mutation ait à penser à l'invalider — et reste juste après une
	 * annulation qui recopie un ancien arbre.
	 */
	[[nodiscard]] uint64_t StructureStamp() const noexcept { return m_structureStamp; }

	/// `base`, `base 2`, `base 3`… : le premier nom libre PARMI LES FRÈRES.
	/// L'unicité n'est imposée qu'entre frères, parce que c'est tout ce dont
	/// un chemin a besoin pour être sans ambiguïté.
	[[nodiscard]] String UniqueChildName(NodeId parent, const String& base) const;

	[[nodiscard]] NodeId FindChild(NodeId parent, const String& name) const noexcept;

	/// Recherche récursive par nom dans le sous-arbre de `from` (parcours
	/// préfixe, premier trouvé).
	[[nodiscard]] NodeId FindByName(const String& name, NodeId from = NodeId{}) const;

	/// Résout un chemin (cf. NodePath pour les règles). `base` ne sert qu'aux
	/// chemins relatifs.
	[[nodiscard]] NodeId Resolve(const NodePath& path, NodeId base = NodeId{}) const;

	[[nodiscard]] NodeId Resolve(const String& path, NodeId base = NodeId{}) const;

	/// Chemin absolu de `id`, racine comprise (`/Scene/Car/Wheels`).
	[[nodiscard]] String PathOf(NodeId id) const;

	// ── Parcours ─────────────────────────────────────────────────────────────

	/// Parcours préfixe (parent avant enfants, enfants dans l'ordre) —
	/// l'ordre d'affichage et de sérialisation. Itératif : un arbre profond
	/// ne doit pas dépendre de la pile d'appels.
	template <typename Visitor> void Traverse(NodeId from, const Visitor& visit) const {
		if (!Contains(from))
			return;
		std::vector<NodeId> stack{from};
		while (!stack.empty()) {
			NodeId id = stack.back();
			stack.pop_back();
			const Node* node = Get(id);
			if (!node)
				continue;
			visit(id, *node);
			for (auto it = node->children.rbegin(); it != node->children.rend(); ++it)
				stack.push_back(*it);
		}
	}

	[[nodiscard]] std::vector<NodeId> Descendants(NodeId from) const;

	// ── Transforms ───────────────────────────────────────────────────────────

	[[nodiscard]] Transform LocalTransform(NodeId id) const;

	bool SetLocalTransform(NodeId id, const Transform& transform);

	bool SetLocalPosition(NodeId id, const math::FVector3& position);

	bool SetLocalRotation(NodeId id, const math::FQuaternion& rotation);

	bool SetLocalScale(NodeId id, const math::FVector3& scale);

	/// Matrice monde : composition de toute la chaîne des parents, mise en
	/// cache (cf. en-tête du fichier).
	[[nodiscard]] const math::FMatrix4& GlobalMatrix(NodeId id) const;

	[[nodiscard]] Transform GlobalTransform(NodeId id) const;

	[[nodiscard]] math::FVector3 GlobalPosition(NodeId id) const;

	/// Pose le transform MONDE : le local est recalculé à partir de celui du
	/// parent. C'est ce dont se servent le manipulateur 3D (qui raisonne en
	/// monde) et la synchronisation depuis la physique (qui ne connaît que le
	/// monde).
	bool SetGlobalTransform(NodeId id, const Transform& world);

	bool SetGlobalPosition(NodeId id, const math::FVector3& position);

	// ── Visibilité ───────────────────────────────────────────────────────────

	bool SetVisible(NodeId id, bool visible);

	/// Visibilité EFFECTIVE : un nœud visible sous un parent caché ne se voit
	/// pas. Même règle que le rendu (render3d::Object3D::Traverse saute les
	/// sous-arbres invisibles).
	[[nodiscard]] bool IsVisibleInTree(NodeId id) const noexcept;

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
	NodeId Duplicate(NodeId source, NodeId parent = NodeId{}, size_t index = NODE_APPEND);

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
	[[nodiscard]] ValidationReport
	Validate(const std::function<bool(const String&)>& resourceExists = {},
			 const std::function<bool(const String&)>& typeIsKnown = {}) const;

	// ── Sérialisation ────────────────────────────────────────────────────────

	/// Forme IMBRIQUÉE : un nœud porte ses enfants. Un fichier de scène se lit
	/// alors comme l'arbre qu'il décrit, et une branche se copie-colle à la
	/// main d'un fichier à l'autre.
	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static Result<NodeTree, String> FromJson(const data::NodePtr& json);

	[[nodiscard]] String EncodeJson() const;

	[[nodiscard]] static Result<NodeTree, String> DecodeJson(const String& text);

	static constexpr int FORMAT_VERSION = 1;

	/// Égalité STRUCTURELLE : mêmes nœuds, mêmes liens, mêmes contenus, dans
	/// le même ordre. Utilisé par les tests d'aller-retour.
	[[nodiscard]] bool operator==(const NodeTree& other) const {
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
	[[nodiscard]] bool operator!=(const NodeTree& other) const { return !(*this == other); }

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

	[[nodiscard]] Slot* SlotOf(NodeId id) noexcept;
	[[nodiscard]] const Slot* SlotOf(NodeId id) const noexcept;

	NodeId AllocateSlot();

	/// Alloue un emplacement PRÉCIS (lecture d'un fichier qui conserve les
	/// identifiants) — échoue si l'emplacement est déjà pris.
	NodeId AllocateSlotAt(NodeId wanted);

	NodeId AddAt(NodeId parent, Node node, NodeId wanted);

	/// Déplace la racine fraîchement créée vers l'emplacement voulu par un
	/// fichier (cf. FromJson) — l'arbre est vide à cet instant, donc rien
	/// d'autre n'y renvoie.
	NodeId RelocateRoot(NodeId wanted);

	/// Agrandit le tableau d'emplacements SANS chaîner les nouveaux dans la
	/// liste des libres. C'est réservé à la lecture d'un fichier qui conserve
	/// les identifiants : les chaîner obligerait AllocateSlotAt à parcourir
	/// cette liste pour en retirer l'emplacement voulu, ce qui rend le
	/// chargement quadratique (mesuré : 157 s pour 100 000 nœuds). La liste
	/// est reconstruite une fois à la fin, par RebuildFreeList().
	void ReserveSlots(size_t count);

	/// Re-chaîne tous les emplacements morts, des indices hauts vers les bas
	/// (l'allocation repart donc du plus petit indice libre, ce qui garde le
	/// tableau compact).
	void RebuildFreeList();

	void DetachFromFreeList(uint32_t index);

	void FreeSlot(NodeId id);

	/// Marque le transform monde de `id` à recalculer. O(1) : les descendants
	/// s'en aperçoivent par comparaison d'époque (cf. en-tête).
	/// Nouveau tampon de structure, unique au processus (cf. StructureStamp).
	[[nodiscard]] static uint64_t NextStructureStamp() noexcept;
	void TouchStructure() noexcept { m_structureStamp = NextStructureStamp(); }

	void Touch(NodeId id) const;

	static void InsertChild(std::vector<NodeId>& list, NodeId child, size_t index);

	static void EraseChild(std::vector<NodeId>& list, NodeId child);

	[[nodiscard]] data::NodePtr NodeToJson(NodeId id) const;

	/// Aplatit l'arbre JSON en (nœud, parent d'origine) dans l'ordre préfixe.
	static void CollectJson(const data::NodePtr& json, NodeId parent, std::vector<Node>& flat,
							std::vector<NodeId>& parents);

	std::vector<Slot> m_slots;
	mutable std::vector<Cache> m_cache;
	mutable std::vector<NodeId>
		m_chain; ///< tampon de GlobalMatrix (évite une allocation par appel)
	uint32_t m_freeHead = ~0u;
	size_t m_aliveCount = 0;
	NodeId m_root;
	uint64_t m_structureStamp = NextStructureStamp();
	std::vector<NodeId> m_noChildren; ///< renvoyé par ChildrenOf pour un identifiant inconnu
	math::FMatrix4 m_identity = math::FMatrix4::Identity();
};

} // namespace scene
