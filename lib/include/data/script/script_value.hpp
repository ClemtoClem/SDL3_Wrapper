#pragma once
/**
 * data::script — valeurs manipulées à l'exécution + pont vers `data::Node`.
 *
 * Typage dynamique à 8 formes : nil, booléen, nombre (double unique, comme
 * Lua 5.1/JavaScript), chaîne, liste, table, fonction utilisateur, fonction
 * native. Listes et tables ont une SÉMANTIQUE DE RÉFÉRENCE (`shared_ptr`) :
 * passer une liste à une fonction puis la modifier dedans se voit à
 * l'extérieur — indispensable pour qu'un script d'éditeur puisse remplir une
 * structure que l'hôte C++ relira ensuite.
 *
 * Table = `std::vector<std::pair<String, Value>>` à recherche linéaire, PAS
 * une table de hachage : l'ordre d'insertion est préservé (comme
 * `data::Node`, pour un aller-retour JSON stable) et `String` n'a pas de
 * `std::hash` dans ce dépôt. Les tables d'un script d'éditeur comptent
 * quelques dizaines de clés — le coût linéaire est sans objet ici, et c'est
 * une limite documentée plutôt qu'un oubli.
 */
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "../../core/core.hpp"
#include "../node.hpp"
#include "script_ast.hpp"

namespace data::script {

class Value;
class Interpreter;
class Environment;

struct ListObject {
	std::vector<Value> items;
};

struct MapObject {
	std::vector<std::pair<String, Value>> entries;

	[[nodiscard]] Value *Find(const String &key) noexcept;
	[[nodiscard]] const Value *Find(const String &key) const noexcept;
	void SetKey(const String &key, Value value);
	bool RemoveKey(const String &key);
};

/// Fonction définie dans le script : sa définition partagée (cf.
/// script_ast.hpp) + l'environnement capturé à la création (fermeture).
struct FunctionObject {
	FunctionDefPtr def;
	std::shared_ptr<Environment> closure;
};

/// Fonction fournie par l'hôte C++. Reçoit l'interpréteur (pour rappeler du
/// script, lever une erreur positionnée, lire l'état) et les arguments déjà
/// évalués. Retourne un `Result` : une native qui échoue interrompt le script
/// proprement, exactement comme une erreur d'exécution interne.
using NativeFn = std::function<Result<Value, ScriptError>(Interpreter &, std::vector<Value> &)>;

struct NativeObject {
	String name;
	NativeFn fn;
	int minArity = 0;
	int maxArity = -1; ///< -1 = variadique
};

/// Espace de noms (`math`, `editor`, ou déclaré par `namespace geo { … }`) :
/// un nom et la PORTÉE qui porte ses membres. Les membres sont lus à travers
/// cette portée — liaisons VIVANTES : si une fonction de l'espace de noms
/// modifie un de ses `let`, `geo.membre` le voit aussitôt — et ne s'écrivent
/// pas du dehors (cf. Interpreter).
struct NamespaceObject {
	String name;
	std::shared_ptr<Environment> scope;
};

// ============================================================================
// Value
// ============================================================================

class Value {
public:
	enum class Kind : uint8_t { NIL, BOOLEAN, NUMBER, STRING, LIST, MAP, FUNCTION, NATIVE, NAMESPACE };

	Value() = default;

	[[nodiscard]] static Value Nil() { return {}; }
	[[nodiscard]] static Value Boolean(bool v) {
		Value value;
		value.m_kind = Kind::BOOLEAN;
		value.m_boolean = v;
		return value;
	}
	[[nodiscard]] static Value Number(double v) {
		Value value;
		value.m_kind = Kind::NUMBER;
		value.m_number = v;
		return value;
	}
	[[nodiscard]] static Value Str(String v) {
		Value value;
		value.m_kind = Kind::STRING;
		value.m_text = std::move(v);
		return value;
	}
	[[nodiscard]] static Value List(std::shared_ptr<ListObject> v) {
		Value value;
		value.m_kind = Kind::LIST;
		value.m_list = std::move(v);
		return value;
	}
	[[nodiscard]] static Value EmptyList() { return List(std::make_shared<ListObject>()); }
	[[nodiscard]] static Value Map(std::shared_ptr<MapObject> v) {
		Value value;
		value.m_kind = Kind::MAP;
		value.m_map = std::move(v);
		return value;
	}
	[[nodiscard]] static Value EmptyMap() { return Map(std::make_shared<MapObject>()); }
	[[nodiscard]] static Value Function(std::shared_ptr<FunctionObject> v) {
		Value value;
		value.m_kind = Kind::FUNCTION;
		value.m_function = std::move(v);
		return value;
	}
	[[nodiscard]] static Value Native(std::shared_ptr<NativeObject> v) {
		Value value;
		value.m_kind = Kind::NATIVE;
		value.m_native = std::move(v);
		return value;
	}
	[[nodiscard]] static Value Namespace(std::shared_ptr<NamespaceObject> v) {
		Value value;
		value.m_kind = Kind::NAMESPACE;
		value.m_namespace = std::move(v);
		return value;
	}

	// ── Inspection ───────────────────────────────────────────────────────────

	[[nodiscard]] Kind GetKind() const noexcept { return m_kind; }
	[[nodiscard]] bool IsNil() const noexcept { return m_kind == Kind::NIL; }
	[[nodiscard]] bool IsBoolean() const noexcept { return m_kind == Kind::BOOLEAN; }
	[[nodiscard]] bool IsNumber() const noexcept { return m_kind == Kind::NUMBER; }
	[[nodiscard]] bool IsString() const noexcept { return m_kind == Kind::STRING; }
	[[nodiscard]] bool IsList() const noexcept { return m_kind == Kind::LIST; }
	[[nodiscard]] bool IsMap() const noexcept { return m_kind == Kind::MAP; }
	[[nodiscard]] bool IsFunction() const noexcept { return m_kind == Kind::FUNCTION; }
	[[nodiscard]] bool IsNative() const noexcept { return m_kind == Kind::NATIVE; }
	[[nodiscard]] bool IsNamespace() const noexcept { return m_kind == Kind::NAMESPACE; }
	[[nodiscard]] bool IsCallable() const noexcept { return IsFunction() || IsNative(); }

	[[nodiscard]] bool AsBoolean() const noexcept { return m_boolean; }
	[[nodiscard]] double AsNumber() const noexcept { return m_number; }
	[[nodiscard]] float AsFloat() const noexcept { return float(m_number); }
	[[nodiscard]] const String &AsString() const noexcept { return m_text; }
	[[nodiscard]] const std::shared_ptr<ListObject> &AsList() const noexcept { return m_list; }
	[[nodiscard]] const std::shared_ptr<MapObject> &AsMap() const noexcept { return m_map; }
	[[nodiscard]] const std::shared_ptr<FunctionObject> &AsFunction() const noexcept { return m_function; }
	[[nodiscard]] const std::shared_ptr<NativeObject> &AsNative() const noexcept { return m_native; }
	[[nodiscard]] const std::shared_ptr<NamespaceObject> &AsNamespace() const noexcept { return m_namespace; }

	/// Véracité façon Lua : SEULS `nil` et `false` sont faux. `0` et `""`
	/// sont VRAIS — choix explicite (un script d'éditeur teste surtout des
	/// présences/absences, et `if count` sur un compteur à zéro serait un
	/// piège silencieux dans l'autre convention).
	[[nodiscard]] bool IsTruthy() const noexcept {
		if (m_kind == Kind::NIL)
			return false;
		if (m_kind == Kind::BOOLEAN)
			return m_boolean;
		return true;
	}

	[[nodiscard]] const char *TypeName() const noexcept {
		switch (m_kind) {
			case Kind::NIL:
				return "nil";
			case Kind::BOOLEAN:
				return "bool";
			case Kind::NUMBER:
				return "number";
			case Kind::STRING:
				return "string";
			case Kind::LIST:
				return "list";
			case Kind::MAP:
				return "map";
			case Kind::FUNCTION:
				return "function";
			case Kind::NATIVE:
				return "native";
			case Kind::NAMESPACE:
				return "namespace";
		}
		return "?";
	}

	/// Égalité structurelle pour les scalaires, IDENTITÉ pour les types à
	/// sémantique de référence (listes, tables, fonctions) — même règle que
	/// Python/Lua pour les tables.
	[[nodiscard]] bool Equals(const Value &other) const noexcept {
		if (m_kind != other.m_kind)
			return false;
		switch (m_kind) {
			case Kind::NIL:
				return true;
			case Kind::BOOLEAN:
				return m_boolean == other.m_boolean;
			case Kind::NUMBER:
				return m_number == other.m_number;
			case Kind::STRING:
				return m_text == other.m_text;
			case Kind::LIST:
				return m_list == other.m_list;
			case Kind::MAP:
				return m_map == other.m_map;
			case Kind::FUNCTION:
				return m_function == other.m_function;
			case Kind::NATIVE:
				return m_native == other.m_native;
			case Kind::NAMESPACE:
				return m_namespace == other.m_namespace;
		}
		return false;
	}

	/// Représentation textuelle (ce qu'affiche `print`). Un nombre entier
	/// s'écrit sans décimale (`3`, pas `3.000000`) — seule façon d'obtenir un
	/// `"objet " .. i` lisible dans un script.
	[[nodiscard]] String ToDisplayString() const {
		switch (m_kind) {
			case Kind::NIL:
				return String("nil");
			case Kind::BOOLEAN:
				return String(m_boolean ? "true" : "false");
			case Kind::NUMBER:
				return NumberToString(m_number);
			case Kind::STRING:
				return m_text;
			case Kind::LIST: {
				String out("[");
				if (m_list) {
					for (size_t i = 0; i < m_list->items.size(); ++i) {
						if (i > 0)
							out.Concat(", ");
						out.Concat(m_list->items[i].ToDisplayString());
					}
				}
				out.Concat("]");
				return out;
			}
			case Kind::MAP: {
				String out("{");
				if (m_map) {
					for (size_t i = 0; i < m_map->entries.size(); ++i) {
						if (i > 0)
							out.Concat(", ");
						out.Concat(m_map->entries[i].first);
						out.Concat(": ");
						out.Concat(m_map->entries[i].second.ToDisplayString());
					}
				}
				out.Concat("}");
				return out;
			}
			case Kind::FUNCTION:
				return String::Format("<fn %s>", (m_function && !m_function->def->name.IsEmpty())
													  ? m_function->def->name.CStr()
													  : "anonyme");
			case Kind::NATIVE:
				return String::Format("<native %s>", m_native ? m_native->name.CStr() : "?");
			case Kind::NAMESPACE:
				return String::Format("<namespace %s>", m_namespace ? m_namespace->name.CStr() : "?");
		}
		return String("?");
	}

	/// Formatage numérique partagé (cf. ToDisplayString) — exposé car l'hôte
	/// en a besoin pour composer les mêmes chaînes côté C++ (rapports, log).
	[[nodiscard]] static String NumberToString(double v) {
		if (std::isnan(v))
			return String("nan");
		if (std::isinf(v))
			return String(v > 0 ? "inf" : "-inf");
		if (v == std::floor(v) && std::fabs(v) < 1e15)
			return String::From(static_cast<long long>(v));
		return String::Format("%.6g", v);
	}

private:
	Kind m_kind = Kind::NIL;
	bool m_boolean = false;
	double m_number = 0.0;
	String m_text;
	std::shared_ptr<ListObject> m_list;
	std::shared_ptr<MapObject> m_map;
	std::shared_ptr<FunctionObject> m_function;
	std::shared_ptr<NativeObject> m_native;
	std::shared_ptr<NamespaceObject> m_namespace;
};

// ── MapObject (défini après Value : ses entrées SONT des Value) ─────────────

inline Value *MapObject::Find(const String &key) noexcept {
	for (auto &entry : entries)
		if (entry.first == key)
			return &entry.second;
	return nullptr;
}

inline const Value *MapObject::Find(const String &key) const noexcept {
	for (const auto &entry : entries)
		if (entry.first == key)
			return &entry.second;
	return nullptr;
}

inline void MapObject::SetKey(const String &key, Value value) {
	if (Value *existing = Find(key)) {
		*existing = std::move(value);
		return;
	}
	entries.emplace_back(key, std::move(value));
}

inline bool MapObject::RemoveKey(const String &key) {
	for (size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].first == key) {
			entries.erase(entries.begin() + static_cast<ptrdiff_t>(i));
			return true;
		}
	}
	return false;
}

// ============================================================================
// Pont data::Node <-> Value — ce qui raccroche le langage au reste de data::
// ============================================================================

/// Convertit un arbre `data::Node` (JSON/YAML/TOML/XML… déjà décodé) en
/// valeur de script : un document chargé par n'importe quel codec `data::`
/// devient directement lisible depuis un script, sans couche d'adaptation.
[[nodiscard]] inline Value ValueFromNode(const NodePtr &node) {
	if (!node)
		return Value::Nil();
	switch (node->type) {
		case NodeType::NONE:
			return Value::Nil();
		case NodeType::STRING:
			return Value::Str(node->stringValue);
		case NodeType::BOOL:
			return Value::Boolean(node->boolValue);
		case NodeType::INT:
			return Value::Number(double(node->intValue));
		case NodeType::FLOAT:
			return Value::Number(node->floatValue);
		case NodeType::ARRAY: {
			auto list = std::make_shared<ListObject>();
			list->items.reserve(node->GetSize());
			for (size_t i = 0; i < node->GetSize(); ++i)
				list->items.push_back(ValueFromNode(node->At(i)));
			return Value::List(std::move(list));
		}
		case NodeType::OBJECT: {
			auto map = std::make_shared<MapObject>();
			for (const String &key : node->Keys())
				map->entries.emplace_back(key, ValueFromNode(node->Get(key)));
			return Value::Map(std::move(map));
		}
	}
	return Value::Nil();
}

/// Inverse de `ValueFromNode` : une structure construite par un script
/// devient un `data::Node` réencodable en JSON/YAML/TOML par les codecs
/// existants. Les fonctions (utilisateur ou natives) n'ont pas de
/// représentation de données et deviennent `NONE` — sans erreur, comme
/// `JSON.stringify` ignore une fonction.
///
/// Un nombre entier est réencodé en `INT` : c'est la contrepartie de la
/// limite connue de l'encodeur JSON de ce module (`5.0` s'écrit `5` et se
/// relit en INT — cf. examples/game_editor/, où le bug avait été trouvé),
/// donc l'aller-retour script -> JSON -> script est stable.
[[nodiscard]] inline NodePtr NodeFromValue(const Value &value) {
	switch (value.GetKind()) {
		case Value::Kind::NIL:
			return Node::MakeNone();
		case Value::Kind::BOOLEAN:
			return Node::MakeBool(value.AsBoolean());
		case Value::Kind::NUMBER: {
			double n = value.AsNumber();
			if (n == std::floor(n) && std::fabs(n) < 9.0e15)
				return Node::MakeInt(static_cast<int64_t>(n));
			return Node::MakeFloat(n);
		}
		case Value::Kind::STRING:
			return Node::MakeString(value.AsString());
		case Value::Kind::LIST: {
			auto arr = Node::MakeArray();
			if (value.AsList())
				for (const Value &item : value.AsList()->items)
					arr->Push(NodeFromValue(item));
			return arr;
		}
		case Value::Kind::MAP: {
			auto obj = Node::MakeObject();
			if (value.AsMap())
				for (const auto &entry : value.AsMap()->entries)
					obj->Set(entry.first, NodeFromValue(entry.second));
			return obj;
		}
		case Value::Kind::FUNCTION:
		case Value::Kind::NATIVE:
		case Value::Kind::NAMESPACE:
			return Node::MakeNone();
	}
	return Node::MakeNone();
}

} // namespace data::script
