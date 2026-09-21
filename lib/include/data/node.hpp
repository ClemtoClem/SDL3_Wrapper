#pragma once
/**
 * data::Node — arbre de données typé, indépendant du format, utilisé comme
 * représentation intermédiaire commune pour tous les codecs (JSON, XML, INI,
 * YAML, TOML, CSV, CSS) — inspiré de SDL3pp_dataScripts.h, mais les nœuds
 * Object et Array sont réellement *stockés* dans un sdl3::Properties (chaque
 * enfant est une propriété "pointer" avec cleanup) au lieu d'un simple
 * unordered_map/LinkedMap : la durée de vie et le stockage reposent sur
 * Properties, comme demandé.
 *
 * Les clés et les valeurs scalaires de type chaîne utilisent `String` (cf.
 * string.hpp) plutôt que `std::string`, en cohérence avec le reste du
 * module data::.
 *
 * SDL_Properties étant une table de hachage (l'énumération ne respecte PAS
 * l'ordre d'insertion), un vecteur de clés `order` est maintenu à côté pour
 * préserver l'ordre — indispensable pour un round-trip stable (XML où
 * l'ordre des enfants est sémantique, JSON/YAML où on veut un ordre humain
 * cohérent, CSV où l'ordre des colonnes compte).
 */
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "../core/core.hpp"
#include "../sdl3/misc.hpp"

namespace data {

// ============================================================================
// NodeType
// ============================================================================

enum class NodeType : uint8_t {
	NONE,
	OBJECT,
	ARRAY,
	STRING,
	BOOL,
	INT,
	FLOAT,
};

class Node;
using NodePtr = std::shared_ptr<Node>;

// ============================================================================
// Node
// ============================================================================

class Node : public std::enable_shared_from_this<Node> {
public:
	NodeType type = NodeType::NONE;

	// --- Stockage scalaire ---
	String stringValue;
	bool boolValue = false;
	int64_t intValue = 0;
	double floatValue = 0.0;

	// --- Stockage Object/Array : chaque enfant est une propriété "pointer"
	// avec cleanup dans `props` (la vraie source de vérité pour la durée de
	// vie) ; `order` ne fait que fixer l'ordre d'énumération de `props`.
	sdl3::Properties props;
	std::vector<String> order;

	Node() = default;
	explicit Node(NodeType t) : type(t) {
		if (t == NodeType::OBJECT || t == NodeType::ARRAY)
			props = sdl3::Properties::Create();
	}

	// ── Fabriques ─────────────────────────────────────────────────────────────

	[[nodiscard]] static NodePtr MakeNone() { return std::make_shared<Node>(NodeType::NONE); }
	[[nodiscard]] static NodePtr MakeObject() { return std::make_shared<Node>(NodeType::OBJECT); }
	[[nodiscard]] static NodePtr MakeArray() { return std::make_shared<Node>(NodeType::ARRAY); }
	[[nodiscard]] static NodePtr MakeString(String v) {
		auto n = std::make_shared<Node>(NodeType::STRING);
		n->stringValue = std::move(v);
		return n;
	}
	[[nodiscard]] static NodePtr MakeBool(bool v) {
		auto n = std::make_shared<Node>(NodeType::BOOL);
		n->boolValue = v;
		return n;
	}
	[[nodiscard]] static NodePtr MakeInt(int64_t v) {
		auto n = std::make_shared<Node>(NodeType::INT);
		n->intValue = v;
		return n;
	}
	[[nodiscard]] static NodePtr MakeFloat(double v) {
		auto n = std::make_shared<Node>(NodeType::FLOAT);
		n->floatValue = v;
		return n;
	}

	// ── Prédicats ─────────────────────────────────────────────────────────────

	[[nodiscard]] bool IsNone() const noexcept { return type == NodeType::NONE; }
	[[nodiscard]] bool IsObject() const noexcept { return type == NodeType::OBJECT; }
	[[nodiscard]] bool IsArray() const noexcept { return type == NodeType::ARRAY; }
	[[nodiscard]] bool IsString() const noexcept { return type == NodeType::STRING; }
	[[nodiscard]] bool IsBool() const noexcept { return type == NodeType::BOOL; }
	[[nodiscard]] bool IsInt() const noexcept { return type == NodeType::INT; }
	[[nodiscard]] bool IsFloat() const noexcept { return type == NodeType::FLOAT; }
	[[nodiscard]] bool IsNumber() const noexcept { return IsInt() || IsFloat(); }
	[[nodiscard]] bool IsScalar() const noexcept { return !IsObject() && !IsArray() && !IsNone(); }

	// ── Accès Object (clés arbitraires) ────────────────────────────────────────

	void Set(const String &key, NodePtr child) {
		bool isNewKey = !props.HasProperty(key.CStr());
		auto *heapPtr = new NodePtr(std::move(child));
		props.SetPointerWithCleanup(key.CStr(), heapPtr, [](void *p) { delete static_cast<NodePtr *>(p); });
		if (isNewKey)
			order.push_back(key);
	}

	[[nodiscard]] NodePtr Get(const String &key) const {
		auto *p = props.GetTypedPointer<NodePtr>(key.CStr());
		return p ? *p : nullptr;
	}

	[[nodiscard]] bool Has(const String &key) const { return props.HasProperty(key.CStr()); }

	void Remove(const String &key) {
		props.ClearProperty(key.CStr());
		order.erase(std::remove(order.begin(), order.end(), key), order.end());
	}

	[[nodiscard]] const std::vector<String> &Keys() const noexcept { return order; }

	// ── Accès Array (indices séquentiels 0..N-1, mêmes fondations que Object) ──

	void Push(NodePtr child) { Set(String::From(static_cast<long long>(order.size())), std::move(child)); }
	[[nodiscard]] size_t GetSize() const noexcept { return order.size(); }
	[[nodiscard]] NodePtr At(size_t i) const { return i < order.size() ? Get(order[i]) : nullptr; }

	// ── Copie profonde ────────────────────────────────────────────────────────

	[[nodiscard]] NodePtr Clone() const {
		switch (type) {
			case NodeType::NONE:
				return MakeNone();
			case NodeType::STRING:
				return MakeString(stringValue);
			case NodeType::BOOL:
				return MakeBool(boolValue);
			case NodeType::INT:
				return MakeInt(intValue);
			case NodeType::FLOAT:
				return MakeFloat(floatValue);
			case NodeType::OBJECT: {
				auto n = MakeObject();
				for (auto &k : order)
					n->Set(k, Get(k)->Clone());
				return n;
			}
			case NodeType::ARRAY: {
				auto n = MakeArray();
				for (auto &k : order)
					n->Push(Get(k)->Clone());
				return n;
			}
		}
		return MakeNone();
	}
};

} // namespace data