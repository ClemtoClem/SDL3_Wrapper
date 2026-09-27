#pragma once
/**
 * scene::Node — le nœud d'un arbre de composition générique (esprit Godot :
 * une scène est un ARBRE de nœuds, un objet complexe se construit en imbriquant
 * des nœuds simples).
 *
 *     Car                     (Node   — purement structurel)
 *     ├── Body                (Node3D — un transform, des composants)
 *     │   ├── Mesh            (MeshInstance)
 *     │   └── Collision       (Collider)
 *     ├── Wheels
 *     │   ├── FrontLeft …
 *     └── Camera
 *
 * ── Trois décisions qui expliquent la forme de ce fichier ────────────────
 *
 *  1. **Un nœud n'est PAS une classe polymorphe.** Pas de `class MeshInstance
 *     : public Node3D`. Le « type » est une chaîne (`Node::type`) et les
 *     capacités viennent de COMPOSANTS attachés. Raison : ce projet est déjà
 *     bâti sur un ECS et sur des descripteurs sérialisables ; une hiérarchie
 *     d'héritage y ajouterait un deuxième modèle d'objets, non sérialisable
 *     tel quel, et interdirait à l'utilisateur de déclarer ses propres types
 *     sans recompiler. Avec un type-chaîne + composants, un type inconnu se
 *     charge, se sauvegarde et s'affiche quand même (dégradé, jamais perdu).
 *
 *  2. **Le nœud est une VALEUR.** Copiable, comparable, sans pointeur vers
 *     ses voisins : la parenté est exprimée en `NodeId`. C'est ce qui rend
 *     l'annulation/rétablissement par instantané possible (copier l'arbre =
 *     copier un vecteur), la duplication de sous-arbre triviale, et les tests
 *     comparables champ à champ.
 *
 *  3. **Aucune dépendance render3d/physics/ui.** Ce fichier ne connaît que
 *     `core`, `math`, `data` et `sdl3::Color`. Le rendu et la physique sont
 *     PILOTÉS depuis l'arbre (cf. le runtime de l'éditeur), jamais l'inverse
 *     — même séparation document/runtime que celle déjà en place.
 */
#include "../core/core.hpp"
#include "../math/math.hpp"
#include "property.hpp"

#include <vector>

namespace scene {

// ============================================================================
// Transform
// ============================================================================

/// Transform LOCAL d'un nœud (relatif à son parent). La rotation est un
/// quaternion en mémoire — c'est la forme qui se compose et s'interpole sans
/// blocage de cardan — et des angles d'Euler en degrés dans le fichier, parce
/// que c'est ce qu'un humain relit et corrige à la main (`FromEuler`/`ToEuler`
/// sont des inverses exacts l'un de l'autre, cf. math.hpp).
struct Transform {
	math::FVector3 position;
	math::FQuaternion rotation = math::FQuaternion::Identity();
	math::FVector3 scale{1.f, 1.f, 1.f};

	static constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
	static constexpr float RAD2DEG = 180.f / 3.14159265358979323846f;

	[[nodiscard]] math::FMatrix4 Matrix() const noexcept;

	/// Transform correspondant à une matrice (cf. math::DecomposeTRS et ses
	/// limites : miroir porté sur X, cisaillement approché).
	[[nodiscard]] static Transform FromMatrix(const math::FMatrix4& m) noexcept;

	[[nodiscard]] math::FVector3 EulerDegrees() const noexcept;

	void SetEulerDegrees(const math::FVector3& degrees) noexcept;

	[[nodiscard]] bool operator==(const Transform& o) const noexcept {
		auto sameVec = [](const math::FVector3& a, const math::FVector3& b) {
			return a.x == b.x && a.y == b.y && a.z == b.z;
		};
		return sameVec(position, o.position) && sameVec(scale, o.scale) &&
			   rotation.x == o.rotation.x && rotation.y == o.rotation.y &&
			   rotation.z == o.rotation.z && rotation.w == o.rotation.w;
	}
	[[nodiscard]] bool operator!=(const Transform& o) const noexcept { return !(*this == o); }

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static Transform FromJson(const data::NodePtr& node);
};

/// Quel transform conserver lors d'un re-parentage (cf. NodeTree::Reparent).
enum class ReparentMode : uint8_t {
	/// Garde le transform LOCAL : le nœud « suit » son nouveau parent et
	/// saute donc visuellement. C'est ce qu'on veut en construisant un
	/// gabarit (poser une roue à (1,0,0) de CHAQUE voiture).
	KEEP_LOCAL,
	/// Garde la position MONDE : le nœud ne bouge pas à l'écran, son local
	/// est recalculé. C'est ce qu'on veut en réorganisant une scène existante
	/// à la souris — et le défaut de l'éditeur.
	KEEP_GLOBAL,
};

// ============================================================================
// Component
// ============================================================================

/// Capacité attachée à un nœud : « ce nœud a un maillage », « ce nœud est un
/// corps rigide », « ce nœud joue un son ». Le `type` est une chaîne et la
/// charge utile une table de propriétés typées — donc un composant défini par
/// l'application (ou par un futur greffon) traverse la sauvegarde, l'éditeur
/// et les scripts sans que la bibliothèque le connaisse.
struct Component {
	String type;
	PropertyMap props;
	/// Désactivé = conservé dans le document mais ignoré par le runtime
	/// (couper une lumière ou un corps sans perdre son réglage).
	bool enabled = true;

	[[nodiscard]] bool operator==(const Component& o) const {
		return type == o.type && enabled == o.enabled && props == o.props;
	}
	[[nodiscard]] bool operator!=(const Component& o) const { return !(*this == o); }

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static Component FromJson(const data::NodePtr& node);
};

// ============================================================================
// Node
// ============================================================================

/// Types de nœuds fournis par la bibliothèque. Tout le reste est déclaré par
/// l'application (cf. NodeTypeRegistry, type_registry.hpp) — ces deux-là
/// suffisent à décrire n'importe quel arbre, les autres n'ajoutent que des
/// composants par défaut et une icône.
namespace node_type {
/// Nœud purement structurel : un groupe, un dossier. Il a quand même un
/// transform (un groupe qu'on déplace déplace ses enfants) — c'est le choix
/// inverse de Godot, où `Node` n'a pas de transform et `Node3D` en ajoute un.
/// Raison : un arbre où seuls certains niveaux portent un transform oblige
/// tout le code de parcours à traiter le cas « trou dans la chaîne », pour un
/// gain mémoire dérisoire (10 flottants).
inline constexpr const char* NODE = "Node";
/// Nœud spatial explicite — même structure, nom qui dit l'intention.
inline constexpr const char* NODE3D = "Node3D";
} // namespace node_type

/// Un nœud de l'arbre. Valeur pure : la parenté est en `NodeId`, jamais en
/// pointeur (cf. en-tête, décision 2).
struct Node {
	NodeId id;
	NodeId parent;				  ///< invalide pour la racine
	std::vector<NodeId> children; ///< ORDRE stable : c'est celui de l'outliner et du fichier
	String name; ///< libellé éditorial, NON unique globalement (cf. NodeTree::UniqueChildName)
	String type = node_type::NODE3D;
	Transform transform;
	/// Visibilité PROPRE. La visibilité effective est héritée : un enfant
	/// visible d'un parent caché reste caché (cf. NodeTree::IsVisibleInTree).
	bool visible = true;
	/// Verrouillé : l'éditeur refuse de le sélectionner au clic et de le
	/// déplacer au manipulateur (le décor d'un niveau qu'on ne veut plus
	/// bouger par accident).
	bool locked = false;
	std::vector<Component> components;
	PropertyMap properties;

	// ── Composants ───────────────────────────────────────────────────────────

	[[nodiscard]] const Component* FindComponent(const String& componentType) const noexcept;
	[[nodiscard]] Component* FindComponent(const String& componentType) noexcept;
	[[nodiscard]] bool HasComponent(const String& componentType) const noexcept;

	/// Ajoute OU remplace le composant de ce type (un nœud ne porte qu'un
	/// composant par type : deux maillages sur le même nœud décriraient deux
	/// objets, c'est-à-dire deux nœuds).
	Component& SetComponent(Component component);

	bool RemoveComponent(const String& componentType);

	/// Raccourci de lecture d'une propriété de composant.
	[[nodiscard]] const PropertyValue* ComponentProperty(const String& componentType,
														 const String& key) const noexcept;

	// ── Propriétés libres ────────────────────────────────────────────────────

	[[nodiscard]] const PropertyValue* Get(const String& key) const noexcept;
	void Set(const String& key, PropertyValue value) { properties.Set(key, std::move(value)); }

	// ── Comparaison ──────────────────────────────────────────────────────────
	// Compare le CONTENU, identifiants et parenté inclus — c'est ce dont les
	// tests d'aller-retour (sauver/recharger) et l'annulation ont besoin.
	[[nodiscard]] bool operator==(const Node& o) const {
		return id == o.id && parent == o.parent && children == o.children && name == o.name &&
			   type == o.type && transform == o.transform && visible == o.visible &&
			   locked == o.locked && components == o.components && properties == o.properties;
	}
	[[nodiscard]] bool operator!=(const Node& o) const { return !(*this == o); }

	// ── Sérialisation ────────────────────────────────────────────────────────
	// Les ENFANTS ne sont pas sérialisés ici : l'arbre l'est sous forme
	// imbriquée par NodeTree (cf. tree.hpp), ce qui donne un fichier qui se
	// lit comme l'arbre qu'il décrit.

	[[nodiscard]] data::NodePtr ToJson() const;

	/// Lit un nœud SANS ses enfants (cf. ToJson) — `children` reste vide, la
	/// reconstruction de l'arbre est le travail de NodeTree::FromJson.
	[[nodiscard]] static Node FromJson(const data::NodePtr& json);
};

} // namespace scene
