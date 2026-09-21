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
#include <vector>

#include "../core/core.hpp"
#include "../math/math.hpp"
#include "property.hpp"

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

	[[nodiscard]] math::FMatrix4 Matrix() const noexcept { return math::ComposeTRS(position, rotation, scale); }

	/// Transform correspondant à une matrice (cf. math::DecomposeTRS et ses
	/// limites : miroir porté sur X, cisaillement approché).
	[[nodiscard]] static Transform FromMatrix(const math::FMatrix4 &m) noexcept {
		math::TRS trs = math::DecomposeTRS(m);
		return Transform{trs.translation, trs.rotation, trs.scale};
	}

	[[nodiscard]] math::FVector3 EulerDegrees() const noexcept {
		math::FVector3 e = rotation.ToEuler();
		return {e.x * RAD2DEG, e.y * RAD2DEG, e.z * RAD2DEG};
	}

	void SetEulerDegrees(const math::FVector3 &degrees) noexcept {
		rotation = math::FQuaternion::FromEuler(degrees.x * DEG2RAD, degrees.y * DEG2RAD, degrees.z * DEG2RAD);
	}

	[[nodiscard]] bool operator==(const Transform &o) const noexcept {
		auto sameVec = [](const math::FVector3 &a, const math::FVector3 &b) {
			return a.x == b.x && a.y == b.y && a.z == b.z;
		};
		return sameVec(position, o.position) && sameVec(scale, o.scale) && rotation.x == o.rotation.x &&
			   rotation.y == o.rotation.y && rotation.z == o.rotation.z && rotation.w == o.rotation.w;
	}
	[[nodiscard]] bool operator!=(const Transform &o) const noexcept { return !(*this == o); }

	[[nodiscard]] data::NodePtr ToJson() const {
		auto vec3 = [](const math::FVector3 &v) {
			auto array = data::Node::MakeArray();
			array->Push(data::Node::MakeFloat(double(v.x)));
			array->Push(data::Node::MakeFloat(double(v.y)));
			array->Push(data::Node::MakeFloat(double(v.z)));
			return array;
		};
		auto node = data::Node::MakeObject();
		node->Set("position", vec3(position));
		node->Set("rotation", vec3(EulerDegrees()));
		node->Set("scale", vec3(scale));
		return node;
	}

	[[nodiscard]] static Transform FromJson(const data::NodePtr &node) {
		Transform transform;
		if (!node || !node->IsObject())
			return transform;
		auto number = [](const data::NodePtr &n, float fallback) -> float {
			if (!n)
				return fallback;
			if (n->IsInt())
				return float(n->intValue);
			if (n->IsFloat())
				return float(n->floatValue);
			return fallback;
		};
		auto vec3 = [&](const data::NodePtr &n, math::FVector3 fallback) -> math::FVector3 {
			if (!n || !n->IsArray() || n->GetSize() < 3)
				return fallback;
			return {number(n->At(0), fallback.x), number(n->At(1), fallback.y), number(n->At(2), fallback.z)};
		};
		transform.position = vec3(node->Get("position"), {});
		transform.SetEulerDegrees(vec3(node->Get("rotation"), {}));
		transform.scale = vec3(node->Get("scale"), {1.f, 1.f, 1.f});
		return transform;
	}
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

	[[nodiscard]] bool operator==(const Component &o) const {
		return type == o.type && enabled == o.enabled && props == o.props;
	}
	[[nodiscard]] bool operator!=(const Component &o) const { return !(*this == o); }

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("type", data::Node::MakeString(type));
		if (!enabled)
			node->Set("enabled", data::Node::MakeBool(false));
		node->Set("props", props.ToJson());
		return node;
	}

	[[nodiscard]] static Component FromJson(const data::NodePtr &node) {
		Component component;
		if (!node || !node->IsObject())
			return component;
		if (auto type = node->Get("type"); type && type->IsString())
			component.type = type->stringValue;
		if (auto enabled = node->Get("enabled"); enabled && enabled->IsBool())
			component.enabled = enabled->boolValue;
		component.props = PropertyMap::FromJson(node->Get("props"));
		return component;
	}
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
inline constexpr const char *NODE = "Node";
/// Nœud spatial explicite — même structure, nom qui dit l'intention.
inline constexpr const char *NODE3D = "Node3D";
} // namespace node_type

/// Un nœud de l'arbre. Valeur pure : la parenté est en `NodeId`, jamais en
/// pointeur (cf. en-tête, décision 2).
struct Node {
	NodeId id;
	NodeId parent;                  ///< invalide pour la racine
	std::vector<NodeId> children;   ///< ORDRE stable : c'est celui de l'outliner et du fichier
	String name;                    ///< libellé éditorial, NON unique globalement (cf. NodeTree::UniqueChildName)
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

	[[nodiscard]] const Component *FindComponent(const String &componentType) const noexcept {
		for (const Component &component : components)
			if (component.type == componentType)
				return &component;
		return nullptr;
	}
	[[nodiscard]] Component *FindComponent(const String &componentType) noexcept {
		for (Component &component : components)
			if (component.type == componentType)
				return &component;
		return nullptr;
	}
	[[nodiscard]] bool HasComponent(const String &componentType) const noexcept {
		return FindComponent(componentType) != nullptr;
	}

	/// Ajoute OU remplace le composant de ce type (un nœud ne porte qu'un
	/// composant par type : deux maillages sur le même nœud décriraient deux
	/// objets, c'est-à-dire deux nœuds).
	Component &SetComponent(Component component) {
		if (Component *existing = FindComponent(component.type)) {
			*existing = std::move(component);
			return *existing;
		}
		components.push_back(std::move(component));
		return components.back();
	}

	bool RemoveComponent(const String &componentType) {
		for (size_t i = 0; i < components.size(); ++i)
			if (components[i].type == componentType) {
				components.erase(components.begin() + ptrdiff_t(i));
				return true;
			}
		return false;
	}

	/// Raccourci de lecture d'une propriété de composant.
	[[nodiscard]] const PropertyValue *ComponentProperty(const String &componentType, const String &key) const noexcept {
		const Component *component = FindComponent(componentType);
		return component ? component->props.Find(key) : nullptr;
	}

	// ── Propriétés libres ────────────────────────────────────────────────────

	[[nodiscard]] const PropertyValue *Get(const String &key) const noexcept { return properties.Find(key); }
	void Set(const String &key, PropertyValue value) { properties.Set(key, std::move(value)); }

	// ── Comparaison ──────────────────────────────────────────────────────────
	// Compare le CONTENU, identifiants et parenté inclus — c'est ce dont les
	// tests d'aller-retour (sauver/recharger) et l'annulation ont besoin.
	[[nodiscard]] bool operator==(const Node &o) const {
		return id == o.id && parent == o.parent && children == o.children && name == o.name && type == o.type &&
			   transform == o.transform && visible == o.visible && locked == o.locked &&
			   components == o.components && properties == o.properties;
	}
	[[nodiscard]] bool operator!=(const Node &o) const { return !(*this == o); }

	// ── Sérialisation ────────────────────────────────────────────────────────
	// Les ENFANTS ne sont pas sérialisés ici : l'arbre l'est sous forme
	// imbriquée par NodeTree (cf. tree.hpp), ce qui donne un fichier qui se
	// lit comme l'arbre qu'il décrit.

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("id", data::Node::MakeString(id.ToText()));
		node->Set("type", data::Node::MakeString(type));
		node->Set("name", data::Node::MakeString(name));
		node->Set("transform", transform.ToJson());
		if (!visible)
			node->Set("visible", data::Node::MakeBool(false));
		if (locked)
			node->Set("locked", data::Node::MakeBool(true));
		if (!components.empty()) {
			auto array = data::Node::MakeArray();
			for (const Component &component : components)
				array->Push(component.ToJson());
			node->Set("components", array);
		}
		if (!properties.IsEmpty())
			node->Set("properties", properties.ToJson());
		return node;
	}

	/// Lit un nœud SANS ses enfants (cf. ToJson) — `children` reste vide, la
	/// reconstruction de l'arbre est le travail de NodeTree::FromJson.
	[[nodiscard]] static Node FromJson(const data::NodePtr &json) {
		Node node;
		if (!json || !json->IsObject())
			return node;
		if (auto id = json->Get("id"); id && id->IsString())
			node.id = NodeId::FromText(id->stringValue);
		if (auto type = json->Get("type"); type && type->IsString() && !type->stringValue.IsEmpty())
			node.type = type->stringValue;
		if (auto name = json->Get("name"); name && name->IsString())
			node.name = name->stringValue;
		node.transform = Transform::FromJson(json->Get("transform"));
		if (auto visible = json->Get("visible"); visible && visible->IsBool())
			node.visible = visible->boolValue;
		if (auto locked = json->Get("locked"); locked && locked->IsBool())
			node.locked = locked->boolValue;
		if (auto array = json->Get("components"); array && array->IsArray())
			for (size_t i = 0; i < array->GetSize(); ++i)
				node.components.push_back(Component::FromJson(array->At(i)));
		node.properties = PropertyMap::FromJson(json->Get("properties"));
		return node;
	}
};

} // namespace scene
