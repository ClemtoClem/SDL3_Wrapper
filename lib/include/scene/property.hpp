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
#include <vector>

#include "../core/core.hpp"
#include "../data/node.hpp"
#include "../math/math.hpp"
#include "../sdl3/structs.hpp"

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
	[[nodiscard]] bool operator==(const NodeId &o) const noexcept {
		return index == o.index && generation == o.generation;
	}
	[[nodiscard]] bool operator!=(const NodeId &o) const noexcept { return !(*this == o); }
	[[nodiscard]] bool operator<(const NodeId &o) const noexcept {
		return index != o.index ? index < o.index : generation < o.generation;
	}

	/// Forme textuelle « indice:génération » — c'est ce qui part dans le
	/// fichier de scène, et ce que lit `FromText`.
	[[nodiscard]] String ToText() const {
		return Valid() ? String::Format("%u:%u", index, generation) : String("-");
	}

	[[nodiscard]] static NodeId FromText(const String &text) {
		int colon = text.IndexOf(':');
		if (colon <= 0)
			return NodeId{};
		NodeId id;
		id.index = uint32_t(text.Substr(0, size_t(colon)).TryParseInt().UnwrapOr(int64_t(INVALID)));
		id.generation = uint32_t(text.Substr(size_t(colon) + 1).TryParseInt().UnwrapOr(0));
		return id;
	}
};

} // namespace scene

template <> struct std::hash<scene::NodeId> {
	size_t operator()(const scene::NodeId &id) const noexcept {
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

[[nodiscard]] inline const char *PropertyTypeName(PropertyType type) noexcept {
	switch (type) {
		case PropertyType::NIL:
			return "nil";
		case PropertyType::BOOL:
			return "bool";
		case PropertyType::INT:
			return "int";
		case PropertyType::FLOAT:
			return "float";
		case PropertyType::STRING:
			return "string";
		case PropertyType::VEC2:
			return "vec2";
		case PropertyType::VEC3:
			return "vec3";
		case PropertyType::QUAT:
			return "quat";
		case PropertyType::COLOR:
			return "color";
		case PropertyType::RESOURCE:
			return "resource";
		case PropertyType::NODE_REF:
			return "node";
		case PropertyType::ARRAY:
			return "array";
		case PropertyType::DICT:
			return "dict";
	}
	return "nil";
}

[[nodiscard]] inline Option<PropertyType> PropertyTypeFromName(const String &name) {
	struct Entry {
		const char *name;
		PropertyType type;
	};
	static constexpr Entry TABLE[] = {
		{"nil", PropertyType::NIL},           {"bool", PropertyType::BOOL},     {"int", PropertyType::INT},
		{"float", PropertyType::FLOAT},       {"string", PropertyType::STRING}, {"vec2", PropertyType::VEC2},
		{"vec3", PropertyType::VEC3},         {"quat", PropertyType::QUAT},     {"color", PropertyType::COLOR},
		{"resource", PropertyType::RESOURCE}, {"node", PropertyType::NODE_REF}, {"array", PropertyType::ARRAY},
		{"dict", PropertyType::DICT},
	};
	for (const Entry &entry : TABLE)
		if (name == entry.name)
			return Some(entry.type);
	return NONE;
}

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

	[[nodiscard]] const PropertyValue *Find(const String &key) const noexcept;
	[[nodiscard]] PropertyValue *Find(const String &key) noexcept;
	[[nodiscard]] bool Has(const String &key) const noexcept { return Find(key) != nullptr; }

	void Set(const String &key, PropertyValue value);
	bool Remove(const String &key);
	bool Rename(const String &key, const String &newKey);

	[[nodiscard]] std::vector<String> Keys() const;

	/// Accès séquentiel (inspecteur, sérialisation, comparaison).
	[[nodiscard]] const std::vector<std::pair<String, PropertyValue>> &Entries() const noexcept { return m_entries; }
	[[nodiscard]] std::vector<std::pair<String, PropertyValue>> &Entries() noexcept { return m_entries; }

	[[nodiscard]] bool operator==(const PropertyMap &other) const;
	[[nodiscard]] bool operator!=(const PropertyMap &other) const { return !(*this == other); }

	[[nodiscard]] data::NodePtr ToJson() const;
	[[nodiscard]] static PropertyMap FromJson(const data::NodePtr &node);

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
	[[nodiscard]] static PropertyValue Bool(bool v) {
		PropertyValue p;
		p.m_type = PropertyType::BOOL;
		p.m_bool = v;
		return p;
	}
	[[nodiscard]] static PropertyValue Int(int64_t v) {
		PropertyValue p;
		p.m_type = PropertyType::INT;
		p.m_int = v;
		return p;
	}
	[[nodiscard]] static PropertyValue Float(double v) {
		PropertyValue p;
		p.m_type = PropertyType::FLOAT;
		p.m_float = v;
		return p;
	}
	[[nodiscard]] static PropertyValue Str(String v) {
		PropertyValue p;
		p.m_type = PropertyType::STRING;
		p.m_string = std::move(v);
		return p;
	}
	[[nodiscard]] static PropertyValue Vec2(const math::FVector2 &v) {
		PropertyValue p;
		p.m_type = PropertyType::VEC2;
		p.m_vec = {v.x, v.y, 0.f, 0.f};
		return p;
	}
	[[nodiscard]] static PropertyValue Vec3(const math::FVector3 &v) {
		PropertyValue p;
		p.m_type = PropertyType::VEC3;
		p.m_vec = {v.x, v.y, v.z, 0.f};
		return p;
	}
	[[nodiscard]] static PropertyValue Quat(const math::FQuaternion &q) {
		PropertyValue p;
		p.m_type = PropertyType::QUAT;
		p.m_vec = {q.x, q.y, q.z, q.w};
		return p;
	}
	[[nodiscard]] static PropertyValue Color(const sdl3::Color &c) {
		PropertyValue p;
		p.m_type = PropertyType::COLOR;
		p.m_vec = {float(c.r), float(c.g), float(c.b), float(c.a)};
		return p;
	}
	/// Chemin de ressource (modèle, texture, script…). Volontairement
	/// distinct de STRING : l'inspecteur y propose un sélecteur de fichier et
	/// le validateur vérifie l'existence du fichier (cf. NodeTree::Validate).
	[[nodiscard]] static PropertyValue Resource(String path) {
		PropertyValue p;
		p.m_type = PropertyType::RESOURCE;
		p.m_string = std::move(path);
		return p;
	}
	/// Référence vers un autre nœud. Sérialisée par IDENTIFIANT (et non par
	/// nom ni par pointeur) : elle survit donc au renommage, au déplacement
	/// dans l'arbre, et à l'aller-retour fichier.
	[[nodiscard]] static PropertyValue NodeRef(NodeId id) {
		PropertyValue p;
		p.m_type = PropertyType::NODE_REF;
		p.m_node = id;
		return p;
	}
	[[nodiscard]] static PropertyValue Array(std::vector<PropertyValue> items) {
		PropertyValue p;
		p.m_type = PropertyType::ARRAY;
		p.m_array = std::move(items);
		return p;
	}
	[[nodiscard]] static PropertyValue Dict(PropertyMap map) {
		PropertyValue p;
		p.m_type = PropertyType::DICT;
		p.m_dict = std::make_shared<PropertyMap>(std::move(map));
		return p;
	}

	// ── Interrogation ────────────────────────────────────────────────────────

	[[nodiscard]] PropertyType Type() const noexcept { return m_type; }
	[[nodiscard]] bool IsNil() const noexcept { return m_type == PropertyType::NIL; }
	[[nodiscard]] bool IsNodeRef() const noexcept { return m_type == PropertyType::NODE_REF; }
	[[nodiscard]] bool IsNumber() const noexcept {
		return m_type == PropertyType::INT || m_type == PropertyType::FLOAT;
	}

	/// Les accesseurs convertissent quand la conversion est SÛRE (int↔float,
	/// bool→int) et rendent `fallback` sinon — une propriété saisie comme
	/// `2` reste lisible en float, ce qui évite de casser un document écrit
	/// à la main.
	[[nodiscard]] bool AsBool(bool fallback = false) const noexcept {
		switch (m_type) {
			case PropertyType::BOOL:
				return m_bool;
			case PropertyType::INT:
				return m_int != 0;
			case PropertyType::FLOAT:
				return m_float != 0.0;
			default:
				return fallback;
		}
	}
	[[nodiscard]] int64_t AsInt(int64_t fallback = 0) const noexcept {
		switch (m_type) {
			case PropertyType::INT:
				return m_int;
			case PropertyType::FLOAT:
				return int64_t(m_float);
			case PropertyType::BOOL:
				return m_bool ? 1 : 0;
			default:
				return fallback;
		}
	}
	[[nodiscard]] float AsFloat(float fallback = 0.f) const noexcept {
		switch (m_type) {
			case PropertyType::FLOAT:
				return float(m_float);
			case PropertyType::INT:
				return float(m_int);
			case PropertyType::BOOL:
				return m_bool ? 1.f : 0.f;
			default:
				return fallback;
		}
	}
	[[nodiscard]] String AsString(const char *fallback = "") const {
		if (m_type == PropertyType::STRING || m_type == PropertyType::RESOURCE)
			return m_string;
		return String(fallback);
	}
	[[nodiscard]] math::FVector2 AsVec2(math::FVector2 fallback = {}) const noexcept {
		return m_type == PropertyType::VEC2 ? math::FVector2{m_vec.x, m_vec.y} : fallback;
	}
	[[nodiscard]] math::FVector3 AsVec3(math::FVector3 fallback = {}) const noexcept {
		return m_type == PropertyType::VEC3 ? math::FVector3{m_vec.x, m_vec.y, m_vec.z} : fallback;
	}
	[[nodiscard]] math::FQuaternion AsQuat(math::FQuaternion fallback = math::FQuaternion::Identity()) const noexcept {
		return m_type == PropertyType::QUAT ? math::FQuaternion{m_vec.x, m_vec.y, m_vec.z, m_vec.w} : fallback;
	}
	[[nodiscard]] sdl3::Color AsColor(sdl3::Color fallback = {255, 255, 255, 255}) const noexcept {
		if (m_type != PropertyType::COLOR)
			return fallback;
		auto channel = [](float v) -> uint8_t { return uint8_t(v < 0.f ? 0.f : (v > 255.f ? 255.f : v)); };
		return sdl3::Color{channel(m_vec.x), channel(m_vec.y), channel(m_vec.z), channel(m_vec.w)};
	}
	[[nodiscard]] NodeId AsNodeRef() const noexcept { return m_type == PropertyType::NODE_REF ? m_node : NodeId{}; }
	[[nodiscard]] const std::vector<PropertyValue> &AsArray() const noexcept { return m_array; }
	[[nodiscard]] std::vector<PropertyValue> &AsArray() noexcept { return m_array; }
	[[nodiscard]] const PropertyMap *AsDict() const noexcept { return m_dict ? m_dict.get() : nullptr; }
	[[nodiscard]] PropertyMap *AsDict() noexcept { return m_dict ? m_dict.get() : nullptr; }

	/// Re-câble les références de nœud contenues (y compris au fond des
	/// tableaux et dictionnaires) selon `remap` — cœur de la duplication d'un
	/// sous-arbre : une référence INTERNE suit la copie, une référence
	/// EXTERNE reste sur sa cible d'origine (absente de `remap`).
	template <typename Lookup> void RemapNodeRefs(const Lookup &remap) {
		if (m_type == PropertyType::NODE_REF) {
			if (Option<NodeId> replacement = remap(m_node); replacement.IsSome())
				m_node = replacement.Unwrap();
			return;
		}
		if (m_type == PropertyType::ARRAY) {
			for (PropertyValue &item : m_array)
				item.RemapNodeRefs(remap);
			return;
		}
		if (m_type == PropertyType::DICT && m_dict) {
			// Copie sur écriture : un dictionnaire partagé entre deux valeurs
			// (cf. m_dict, shared_ptr) ne doit pas être remappé deux fois ni
			// polluer l'original.
			m_dict = std::make_shared<PropertyMap>(*m_dict);
			for (auto &[key, value] : m_dict->Entries())
				value.RemapNodeRefs(remap);
		}
	}

	/// Visite toutes les références de nœud contenues (validation, rapport).
	template <typename Visitor> void VisitNodeRefs(const Visitor &visit) const {
		if (m_type == PropertyType::NODE_REF) {
			visit(m_node);
			return;
		}
		if (m_type == PropertyType::ARRAY) {
			for (const PropertyValue &item : m_array)
				item.VisitNodeRefs(visit);
			return;
		}
		if (m_type == PropertyType::DICT && m_dict)
			for (const auto &[key, value] : m_dict->Entries())
				value.VisitNodeRefs(visit);
	}

	[[nodiscard]] bool operator==(const PropertyValue &o) const {
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
				return m_vec.x == o.m_vec.x && m_vec.y == o.m_vec.y && m_vec.z == o.m_vec.z && m_vec.w == o.m_vec.w;
			case PropertyType::NODE_REF:
				return m_node == o.m_node;
			case PropertyType::ARRAY:
				return m_array == o.m_array;
			case PropertyType::DICT:
				return (!m_dict && !o.m_dict) || (m_dict && o.m_dict && *m_dict == *o.m_dict);
		}
		return false;
	}
	[[nodiscard]] bool operator!=(const PropertyValue &o) const { return !(*this == o); }

	// ── Sérialisation ────────────────────────────────────────────────────────
	//
	// Forme retenue : `{"type": "...", "value": ...}`. Un tableau nu de trois
	// nombres serait plus court, mais ne dirait pas si c'est un vecteur, une
	// couleur ou trois réglages sans rapport — or c'est précisément ce que
	// l'inspecteur et le validateur ont besoin de savoir en relisant.

	[[nodiscard]] data::NodePtr ToJson() const {
		auto node = data::Node::MakeObject();
		node->Set("type", data::Node::MakeString(String(PropertyTypeName(m_type))));
		switch (m_type) {
			case PropertyType::NIL:
				break;
			case PropertyType::BOOL:
				node->Set("value", data::Node::MakeBool(m_bool));
				break;
			case PropertyType::INT:
				node->Set("value", data::Node::MakeInt(m_int));
				break;
			case PropertyType::FLOAT:
				node->Set("value", data::Node::MakeFloat(m_float));
				break;
			case PropertyType::STRING:
			case PropertyType::RESOURCE:
				node->Set("value", data::Node::MakeString(m_string));
				break;
			case PropertyType::VEC2:
			case PropertyType::VEC3:
			case PropertyType::QUAT:
			case PropertyType::COLOR: {
				auto array = data::Node::MakeArray();
				const int count = m_type == PropertyType::VEC2 ? 2 : (m_type == PropertyType::VEC3 ? 3 : 4);
				const float components[4] = {m_vec.x, m_vec.y, m_vec.z, m_vec.w};
				for (int i = 0; i < count; ++i)
					array->Push(data::Node::MakeFloat(double(components[i])));
				node->Set("value", array);
				break;
			}
			case PropertyType::NODE_REF:
				node->Set("value", data::Node::MakeString(m_node.ToText()));
				break;
			case PropertyType::ARRAY: {
				auto array = data::Node::MakeArray();
				for (const PropertyValue &item : m_array)
					array->Push(item.ToJson());
				node->Set("value", array);
				break;
			}
			case PropertyType::DICT:
				node->Set("value", m_dict ? m_dict->ToJson() : data::Node::MakeObject());
				break;
		}
		return node;
	}

	[[nodiscard]] static PropertyValue FromJson(const data::NodePtr &node) {
		if (!node || !node->IsObject())
			return PropertyValue{};
		auto typeNode = node->Get("type");
		PropertyType type = PropertyTypeFromName(typeNode && typeNode->IsString() ? typeNode->stringValue : String())
								.UnwrapOr(PropertyType::NIL);
		auto value = node->Get("value");
		auto number = [](const data::NodePtr &n, float fallback = 0.f) -> float {
			if (!n)
				return fallback;
			if (n->IsInt())
				return float(n->intValue);
			if (n->IsFloat())
				return float(n->floatValue);
			return fallback;
		};
		switch (type) {
			case PropertyType::NIL:
				return PropertyValue{};
			case PropertyType::BOOL:
				return Bool(value && value->IsBool() ? value->boolValue : false);
			case PropertyType::INT:
				return Int(value && value->IsInt() ? value->intValue : int64_t(number(value)));
			case PropertyType::FLOAT:
				return Float(double(number(value)));
			case PropertyType::STRING:
				return Str(value && value->IsString() ? value->stringValue : String());
			case PropertyType::RESOURCE:
				return Resource(value && value->IsString() ? value->stringValue : String());
			case PropertyType::VEC2:
			case PropertyType::VEC3:
			case PropertyType::QUAT:
			case PropertyType::COLOR: {
				float c[4] = {0.f, 0.f, 0.f, type == PropertyType::QUAT ? 1.f : 255.f};
				if (value && value->IsArray())
					for (size_t i = 0; i < value->GetSize() && i < 4; ++i)
						c[i] = number(value->At(i));
				if (type == PropertyType::VEC2)
					return Vec2({c[0], c[1]});
				if (type == PropertyType::VEC3)
					return Vec3({c[0], c[1], c[2]});
				if (type == PropertyType::QUAT)
					return Quat(math::FQuaternion{c[0], c[1], c[2], c[3]});
				auto channel = [](float v) -> uint8_t { return uint8_t(v < 0.f ? 0.f : (v > 255.f ? 255.f : v)); };
				return Color(sdl3::Color{channel(c[0]), channel(c[1]), channel(c[2]), channel(c[3])});
			}
			case PropertyType::NODE_REF:
				return NodeRef(NodeId::FromText(value && value->IsString() ? value->stringValue : String()));
			case PropertyType::ARRAY: {
				std::vector<PropertyValue> items;
				if (value && value->IsArray())
					for (size_t i = 0; i < value->GetSize(); ++i)
						items.push_back(FromJson(value->At(i)));
				return Array(std::move(items));
			}
			case PropertyType::DICT:
				return Dict(PropertyMap::FromJson(value));
		}
		return PropertyValue{};
	}

	/// Forme courte pour la console et les rapports.
	[[nodiscard]] String ToDisplayString() const {
		switch (m_type) {
			case PropertyType::NIL:
				return String("nil");
			case PropertyType::BOOL:
				return String(m_bool ? "true" : "false");
			case PropertyType::INT:
				return String::From(static_cast<long long>(m_int));
			case PropertyType::FLOAT:
				return String::From(m_float, 3);
			case PropertyType::STRING:
			case PropertyType::RESOURCE:
				return m_string;
			case PropertyType::VEC2:
				return String::Format("(%.3f, %.3f)", double(m_vec.x), double(m_vec.y));
			case PropertyType::VEC3:
				return String::Format("(%.3f, %.3f, %.3f)", double(m_vec.x), double(m_vec.y), double(m_vec.z));
			case PropertyType::QUAT:
				return String::Format("(%.3f, %.3f, %.3f, %.3f)", double(m_vec.x), double(m_vec.y), double(m_vec.z),
									  double(m_vec.w));
			case PropertyType::COLOR:
				return String::Format("#%02X%02X%02X", int(m_vec.x), int(m_vec.y), int(m_vec.z));
			case PropertyType::NODE_REF:
				return String::Format("node(%s)", m_node.ToText().CStr());
			case PropertyType::ARRAY:
				return String::Format("[%zu]", m_array.size());
			case PropertyType::DICT:
				return String::Format("{%zu}", m_dict ? m_dict->Size() : size_t(0));
		}
		return String("nil");
	}

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

inline const PropertyValue *PropertyMap::Find(const String &key) const noexcept {
	for (const auto &entry : m_entries)
		if (entry.first == key)
			return &entry.second;
	return nullptr;
}

inline PropertyValue *PropertyMap::Find(const String &key) noexcept {
	for (auto &entry : m_entries)
		if (entry.first == key)
			return &entry.second;
	return nullptr;
}

inline void PropertyMap::Set(const String &key, PropertyValue value) {
	if (PropertyValue *found = Find(key)) {
		*found = std::move(value);
		return;
	}
	m_entries.push_back({key, std::move(value)});
}

inline bool PropertyMap::Remove(const String &key) {
	for (size_t i = 0; i < m_entries.size(); ++i)
		if (m_entries[i].first == key) {
			m_entries.erase(m_entries.begin() + ptrdiff_t(i));
			return true;
		}
	return false;
}

inline bool PropertyMap::Rename(const String &key, const String &newKey) {
	if (key == newKey || Find(newKey))
		return false;
	if (PropertyValue *found = Find(key)) {
		for (auto &entry : m_entries)
			if (entry.first == key) {
				entry.first = newKey;
				return true;
			}
		(void)found;
	}
	return false;
}

inline std::vector<String> PropertyMap::Keys() const {
	std::vector<String> keys;
	keys.reserve(m_entries.size());
	for (const auto &entry : m_entries)
		keys.push_back(entry.first);
	return keys;
}

inline bool PropertyMap::operator==(const PropertyMap &other) const {
	if (m_entries.size() != other.m_entries.size())
		return false;
	for (size_t i = 0; i < m_entries.size(); ++i)
		if (m_entries[i].first != other.m_entries[i].first || m_entries[i].second != other.m_entries[i].second)
			return false;
	return true;
}

inline data::NodePtr PropertyMap::ToJson() const {
	auto node = data::Node::MakeObject();
	for (const auto &[key, value] : m_entries)
		node->Set(key, value.ToJson());
	return node;
}

inline PropertyMap PropertyMap::FromJson(const data::NodePtr &node) {
	PropertyMap map;
	if (!node || !node->IsObject())
		return map;
	for (const String &key : node->Keys())
		map.Set(key, PropertyValue::FromJson(node->Get(key)));
	return map;
}

} // namespace scene
