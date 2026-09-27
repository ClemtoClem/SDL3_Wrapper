#pragma once
/**
 * scene::PropertyValue / PropertyMap — les propriétés ÉDITABLES d'un nœud.
 *
 * Un nœud doit pouvoir porter des données que personne n'a déclarées en C++
 * (`max_speed = 180`, `team = "blue"`), éditables dans l'inspecteur, lisibles
 * depuis un script, et conservées par la sauvegarde. C'est ce que ce fichier
 * fournit : une valeur TYPÉE (pas un simple `data::Node` non typé) et une
 * table ordonnée de telles valeurs.
 *
 * ── Pourquoi un type dédié plutôt que `data::Node` directement ───────────
 * `data::Node` sait déjà représenter booléens, nombres, chaînes, tableaux et
 * objets, et reste le format de FICHIER (cf. `ToJson`/`FromJson` plus bas —
 * aucune deuxième implémentation de JSON n'est introduite). Mais deux besoins
 * du graphe de nœuds ne se disent pas dans un arbre non typé :
 *
 *  1. une RÉFÉRENCE À UN NŒUD (`NodeRef`) doit être reconnaissable pour être
 *     re-câblée lorsqu'un sous-arbre est dupliqué (cf. NodeTree::Duplicate) ;
 *     noyée dans un objet JSON quelconque, elle serait indétectable ;
 *  2. un vecteur, un quaternion ou une couleur doivent garder leur SENS pour
 *     que l'inspecteur affiche trois champs numériques ou un sélecteur de
 *     couleur, et non « un tableau de trois nombres ».
 *
 * Le type est donc porté par la valeur elle-même, et la conversion vers
 * `data::Node` n'intervient qu'aux frontières (fichier, script).
 *
 * Aucune exception (cf. memory/feedback_no_exceptions.md) : une lecture de
 * mauvais type rend la valeur de repli demandée, jamais d'échec silencieux
 * qui corromprait le document.
 */
#include "../core/core.hpp"
#include "../data/node.hpp"
#include "../math/math.hpp"
#include "../sdl3/structs.hpp"

#include <vector>

namespace scene {

/// Identifiant stable d'un nœud. Même forme que `ecs::Entity` (indice +
/// génération) et pour la même raison : un indice seul serait réattribué
/// après suppression, et une référence conservée ailleurs pointerait alors
/// silencieusement sur un AUTRE nœud. Le nom, lui, n'est qu'une propriété
/// éditoriale (cf. Node::name) — jamais une identité.
struct NodeId {
	static constexpr uint32_t INVALID = ~0u;
	uint32_t index = INVALID;
	uint32_t generation = 0;

	[[nodiscard]] bool Valid() const noexcept { return index != INVALID; }
	[[nodiscard]] explicit operator bool() const noexcept { return Valid(); }
	[[nodiscard]] bool operator==(const NodeId& o) const noexcept {
		return index == o.index && generation == o.generation;
	}
	[[nodiscard]] bool operator!=(const NodeId& o) const noexcept { return !(*this == o); }
	[[nodiscard]] bool operator<(const NodeId& o) const noexcept {
		return index != o.index ? index < o.index : generation < o.generation;
	}

	/// Forme textuelle « indice:génération » — c'est ce qui part dans le
	/// fichier de scène, et ce que lit `FromText`.
	[[nodiscard]] String ToText() const;

	[[nodiscard]] static NodeId FromText(const String& text);
};

} // namespace scene

template <> struct std::hash<scene::NodeId> {
	size_t operator()(const scene::NodeId& id) const noexcept {
		size_t h = id.index;
		h ^= size_t(id.generation) + 0x9e3779b9ull + (h << 6) + (h >> 2);
		return h;
	}
};

namespace scene {

/// Familles de valeurs éditables (cf. en-tête). `RESOURCE` est un chemin de
/// fichier (modèle, texture, script…) et `NODE_REF` une référence vers un
/// autre nœud de la même scène.
enum class PropertyType : uint8_t {
	NIL,
	BOOL,
	INT,
	FLOAT,
	STRING,
	VEC2,
	VEC3,
	QUAT,
	COLOR,
	RESOURCE,
	NODE_REF,
	ARRAY,
	DICT,
};

[[nodiscard]] const char* PropertyTypeName(PropertyType type) noexcept;

[[nodiscard]] Option<PropertyType> PropertyTypeFromName(const String& name);

class PropertyValue;

/// Table ORDONNÉE clé → valeur. L'ordre est celui de l'insertion : c'est
/// l'ordre d'affichage dans l'inspecteur et celui du fichier, donc rouvrir un
/// projet ne doit pas mélanger les lignes (une `std::unordered_map` le ferait
/// à chaque sauvegarde, et le diff du fichier deviendrait illisible).
class PropertyMap {
public:
	[[nodiscard]] bool IsEmpty() const noexcept { return m_entries.empty(); }
	[[nodiscard]] size_t Size() const noexcept { return m_entries.size(); }
	void Clear() { m_entries.clear(); }

	[[nodiscard]] const PropertyValue* Find(const String& key) const noexcept;
	[[nodiscard]] PropertyValue* Find(const String& key) noexcept;
	[[nodiscard]] bool Has(const String& key) const noexcept { return Find(key) != nullptr; }

	void Set(const String& key, PropertyValue value);
	bool Remove(const String& key);
	bool Rename(const String& key, const String& newKey);

	[[nodiscard]] std::vector<String> Keys() const;

	/// Accès séquentiel (inspecteur, sérialisation, comparaison).
	[[nodiscard]] const std::vector<std::pair<String, PropertyValue>>& Entries() const noexcept;
	[[nodiscard]] std::vector<std::pair<String, PropertyValue>>& Entries() noexcept;

	[[nodiscard]] bool operator==(const PropertyMap& other) const;
	[[nodiscard]] bool operator!=(const PropertyMap& other) const { return !(*this == other); }

	[[nodiscard]] data::NodePtr ToJson() const;
	[[nodiscard]] static PropertyMap FromJson(const data::NodePtr& node);

private:
	std::vector<std::pair<String, PropertyValue>> m_entries;
};

/// Une valeur éditable. Structure « étiquetée » plutôt que `std::variant` :
/// `std::variant::get` lance une exception sur mauvais type, ce que ce projet
/// s'interdit, et les accesseurs ci-dessous rendent une valeur de repli —
/// forme déjà employée partout ailleurs (`Option`/`Result`).
class PropertyValue {
public:
	PropertyValue() = default;

	// ── Fabriques ────────────────────────────────────────────────────────────

	[[nodiscard]] static PropertyValue Nil() { return PropertyValue{}; }
	[[nodiscard]] static PropertyValue Bool(bool v);
	[[nodiscard]] static PropertyValue Int(int64_t v);
	[[nodiscard]] static PropertyValue Float(double v);
	[[nodiscard]] static PropertyValue Str(String v);
	[[nodiscard]] static PropertyValue Vec2(const math::FVector2& v);
	[[nodiscard]] static PropertyValue Vec3(const math::FVector3& v);
	[[nodiscard]] static PropertyValue Quat(const math::FQuaternion& q);
	[[nodiscard]] static PropertyValue Color(const sdl3::Color& c) {
		PropertyValue p;
		p.m_type = PropertyType::COLOR;
		p.m_vec = {float(c.r), float(c.g), float(c.b), float(c.a)};
		return p;
	}
	/// Chemin de ressource (modèle, texture, script…). Volontairement
	/// distinct de STRING : l'inspecteur y propose un sélecteur de fichier et
	/// le validateur vérifie l'existence du fichier (cf. NodeTree::Validate).
	[[nodiscard]] static PropertyValue Resource(String path);
	/// Référence vers un autre nœud. Sérialisée par IDENTIFIANT (et non par
	/// nom ni par pointeur) : elle survit donc au renommage, au déplacement
	/// dans l'arbre, et à l'aller-retour fichier.
	[[nodiscard]] static PropertyValue NodeRef(NodeId id);
	[[nodiscard]] static PropertyValue Array(std::vector<PropertyValue> items);
	[[nodiscard]] static PropertyValue Dict(PropertyMap map);

	// ── Interrogation ────────────────────────────────────────────────────────

	[[nodiscard]] PropertyType Type() const noexcept { return m_type; }
	[[nodiscard]] bool IsNil() const noexcept { return m_type == PropertyType::NIL; }
	[[nodiscard]] bool IsNodeRef() const noexcept { return m_type == PropertyType::NODE_REF; }
	[[nodiscard]] bool IsNumber() const noexcept;

	/// Les accesseurs convertissent quand la conversion est SÛRE (int↔float,
	/// bool→int) et rendent `fallback` sinon — une propriété saisie comme
	/// `2` reste lisible en float, ce qui évite de casser un document écrit
	/// à la main.
	[[nodiscard]] bool AsBool(bool fallback = false) const noexcept;
	[[nodiscard]] int64_t AsInt(int64_t fallback = 0) const noexcept;
	[[nodiscard]] float AsFloat(float fallback = 0.f) const noexcept;
	[[nodiscard]] String AsString(const char* fallback = "") const;
	[[nodiscard]] math::FVector2 AsVec2(math::FVector2 fallback = {}) const noexcept;
	[[nodiscard]] math::FVector3 AsVec3(math::FVector3 fallback = {}) const noexcept;
	[[nodiscard]] math::FQuaternion
	AsQuat(math::FQuaternion fallback = math::FQuaternion::Identity()) const noexcept;
	[[nodiscard]] sdl3::Color AsColor(sdl3::Color fallback = {255, 255, 255, 255}) const noexcept {
		if (m_type != PropertyType::COLOR)
			return fallback;
		auto channel = [](float v) -> uint8_t {
			return uint8_t(v < 0.f ? 0.f : (v > 255.f ? 255.f : v));
		};
		return sdl3::Color{channel(m_vec.x), channel(m_vec.y), channel(m_vec.z), channel(m_vec.w)};
	}
	[[nodiscard]] NodeId AsNodeRef() const noexcept;
	[[nodiscard]] const std::vector<PropertyValue>& AsArray() const noexcept { return m_array; }
	[[nodiscard]] std::vector<PropertyValue>& AsArray() noexcept { return m_array; }
	[[nodiscard]] const PropertyMap* AsDict() const noexcept;
	[[nodiscard]] PropertyMap* AsDict() noexcept { return m_dict ? m_dict.get() : nullptr; }

	/// Re-câble les références de nœud contenues (y compris au fond des
	/// tableaux et dictionnaires) selon `remap` — cœur de la duplication d'un
	/// sous-arbre : une référence INTERNE suit la copie, une référence
	/// EXTERNE reste sur sa cible d'origine (absente de `remap`).
	template <typename Lookup> void RemapNodeRefs(const Lookup& remap) {
		if (m_type == PropertyType::NODE_REF) {
			if (Option<NodeId> replacement = remap(m_node); replacement.IsSome())
				m_node = replacement.Unwrap();
			return;
		}
		if (m_type == PropertyType::ARRAY) {
			for (PropertyValue& item : m_array)
				item.RemapNodeRefs(remap);
			return;
		}
		if (m_type == PropertyType::DICT && m_dict) {
			// Copie sur écriture : un dictionnaire partagé entre deux valeurs
			// (cf. m_dict, shared_ptr) ne doit pas être remappé deux fois ni
			// polluer l'original.
			m_dict = std::make_shared<PropertyMap>(*m_dict);
			for (auto& [key, value] : m_dict->Entries())
				value.RemapNodeRefs(remap);
		}
	}

	/// Visite toutes les références de nœud contenues (validation, rapport).
	template <typename Visitor> void VisitNodeRefs(const Visitor& visit) const {
		if (m_type == PropertyType::NODE_REF) {
			visit(m_node);
			return;
		}
		if (m_type == PropertyType::ARRAY) {
			for (const PropertyValue& item : m_array)
				item.VisitNodeRefs(visit);
			return;
		}
		if (m_type == PropertyType::DICT && m_dict)
			for (const auto& [key, value] : m_dict->Entries())
				value.VisitNodeRefs(visit);
	}

	[[nodiscard]] bool operator==(const PropertyValue& o) const {
		if (m_type != o.m_type)
			return false;
		switch (m_type) {
		case PropertyType::NIL:
			return true;
		case PropertyType::BOOL:
			return m_bool == o.m_bool;
		case PropertyType::INT:
			return m_int == o.m_int;
		case PropertyType::FLOAT:
			return m_float == o.m_float;
		case PropertyType::STRING:
		case PropertyType::RESOURCE:
			return m_string == o.m_string;
		case PropertyType::VEC2:
		case PropertyType::VEC3:
		case PropertyType::QUAT:
		case PropertyType::COLOR:
			return m_vec.x == o.m_vec.x && m_vec.y == o.m_vec.y && m_vec.z == o.m_vec.z &&
				   m_vec.w == o.m_vec.w;
		case PropertyType::NODE_REF:
			return m_node == o.m_node;
		case PropertyType::ARRAY:
			return m_array == o.m_array;
		case PropertyType::DICT:
			return (!m_dict && !o.m_dict) || (m_dict && o.m_dict && *m_dict == *o.m_dict);
		}
		return false;
	}
	[[nodiscard]] bool operator!=(const PropertyValue& o) const { return !(*this == o); }

	// ── Sérialisation ────────────────────────────────────────────────────────
	//
	// Forme retenue : `{"type": "...", "value": ...}`. Un tableau nu de trois
	// nombres serait plus court, mais ne dirait pas si c'est un vecteur, une
	// couleur ou trois réglages sans rapport — or c'est précisément ce que
	// l'inspecteur et le validateur ont besoin de savoir en relisant.

	[[nodiscard]] data::NodePtr ToJson() const;

	[[nodiscard]] static PropertyValue FromJson(const data::NodePtr& node);

	/// Forme courte pour la console et les rapports.
	[[nodiscard]] String ToDisplayString() const;

private:
	PropertyType m_type = PropertyType::NIL;
	bool m_bool = false;
	int64_t m_int = 0;
	double m_float = 0.0;
	String m_string;
	math::FVector4 m_vec{};
	NodeId m_node{};
	std::vector<PropertyValue> m_array;
	/// Indirection pour le seul cas récursif : un `PropertyMap` membre ferait
	/// un type incomplet à cet endroit (PropertyMap contient des
	/// PropertyValue). Partagé, donc copié à l'écriture dans RemapNodeRefs.
	std::shared_ptr<PropertyMap> m_dict;
};

// ── PropertyMap : définitions différées (PropertyValue complet ici) ─────────













inline bool PropertyMap::operator==(const PropertyMap& other) const {
	if (m_entries.size() != other.m_entries.size())
		return false;
	for (size_t i = 0; i < m_entries.size(); ++i)
		if (m_entries[i].first != other.m_entries[i].first ||
			m_entries[i].second != other.m_entries[i].second)
			return false;
	return true;
}





} // namespace scene
