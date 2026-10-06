#pragma once
/**
 * data::script — bibliothèque standard du langage (« Script ») : les
 * fonctions globales historiques (`print`, `len`, `range`…) et l'espace de
 * noms `std`, qui enveloppe la bibliothèque standard C++ :
 *
 *   conteneurs   std.vector  std.list (chaînée)  std.deque  std.queue
 *                std.stack  std.priority_queue  std.map (ordonnée)
 *                std.unordered_map  std.set  std.unordered_set
 *   texte        std.string (modifiable)  std.stream (flux de texte)
 *   flux         std.pipe (canal entre fils, nœud des opérateurs -> <- <->)
 *   système      std.file (RAII)  std.filesystem  std.os
 *   temps        std.datetime  std.date  std.time
 *   valeurs      std.option  std.result (`std.result.of(fn)` : le `try` sans exception)
 *   concurrence  std.future (lance une fonction sur son fil)  std.mutex  std.lock_guard (RAII)
 *   divers       std.sort, et toutes les fonctions globales (`std.print`…)
 *
 * Chaque type est un « type de l'hôte » (cf. HostType, script_value.hpp) :
 * `std.vector<i32>()` vérifie ses éléments, `v is std.vector` le teste,
 * `for (x in v)` le parcourt, `v -> print` émet ses éléments. Chaque
 * objet porte son verrou : il se partage entre fils `async` sans précaution.
 *
 * Aucune exception : std::filesystem est appelé avec `std::error_code`, les
 * flux sans masque d'exceptions, et les tris avec un comparateur du script
 * passent par un tri fusion maison (std::sort exige un ordre strict faible
 * qu'un comparateur quelconque ne garantit pas). std::regex, qui LÈVE sur un
 * motif invalide, n'est volontairement pas enveloppé.
 */
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "script_interpreter.hpp"

namespace data::script {

/// Fonctions globales historiques (print, len, range, list, map…).
void InstallCoreGlobals(Interpreter& vm);

// ============================================================================
// Outils communs aux types de l'hôte (std et math)
// ============================================================================

namespace lib {

using Args = std::vector<Value>;
using MethodFn = std::function<Result<Value, ScriptError>(Interpreter&, const HostRef&, Args&)>;

[[nodiscard]] ScriptError Fail(String message);

template <class T> [[nodiscard]] T& As(const HostRef& ref) {
	return static_cast<T&>(*ref);
}
template <class T> [[nodiscard]] const T& As(const HostObject& object) {
	return static_cast<const T&>(object);
}

/// Construction pas à pas d'un type de l'hôte (cf. HostType).
struct TypeBuilder {

	std::shared_ptr<HostType> type = std::make_shared<HostType>();

	explicit TypeBuilder(const char* qualifiedName);

	TypeBuilder& Method(const char* name, int minArity, int maxArity, MethodFn fn);

	/// Même méthode sous un autre nom (`push` / `push_back` / `append`).
	TypeBuilder& Alias(const char* alias, const char* target);

	TypeBuilder& Static(const char* name, Value value);

	TypeBuilder& StaticFn(const char* name, int minArity, int maxArity, NativeFn fn);

	TypeBuilder& Construct(int minArity, int maxArity,
						   std::function<Result<Value, ScriptError>(Interpreter&, Args&, const Args&)> fn);
};

/// Native autonome (fonction d'un espace de noms construit à la main).
[[nodiscard]] Value MakeFunction(const String& name, int minArity, int maxArity, NativeFn fn);

// ── Arguments ───────────────────────────────────────────────────────────────

[[nodiscard]] Result<double, ScriptError> ArgFloat(const Args& args, size_t i, const char* fn);

/// Entier (un flottant entier est accepté : `v.at(2.0)`).
[[nodiscard]] Result<int64_t, ScriptError> ArgInt(const Args& args, size_t i, const char* fn);

[[nodiscard]] Result<String, ScriptError> ArgText(const Args& args, size_t i, const char* fn);

[[nodiscard]] Option<ScriptError> ArgCallable(Interpreter& vm, const Args& args, size_t i, const char* fn);

/// Indice dans [0, size) (ou [0, size] si `allowEnd`) ; les négatifs
/// comptent depuis la fin (`-1` : le dernier), comme en Python.
[[nodiscard]] Result<size_t, ScriptError> ToIndex(int64_t raw, size_t size, const char* fn,
														 bool allowEnd = false);

/// Type de l'hôte `ns.name` de CET interpréteur (pour fabriquer ses objets).
[[nodiscard]] std::shared_ptr<const HostType> HostTypeOf(Interpreter& vm, const char* ns, const char* name);

/// Valeur destinée à l'argument de type `i` d'un conteneur typé.
[[nodiscard]] Result<Value, ScriptError> Conform(Interpreter& vm, const HostObject& object, size_t i,
														Value value);

/// « a avant b ? » : comparateur du script (booléen « a < b », ou nombre
/// négatif), sinon l'ordre naturel (cf. Interpreter::CompareValues).
[[nodiscard]] Result<bool, ScriptError> Less(Interpreter& vm, const Value& comparator, const Value& a,
													const Value& b);

/**
 * Tri STABLE par fusion : sûr avec n'importe quel comparateur du script —
 * std::sort exige un ordre strict faible, et un comparateur incohérent y
 * est un comportement indéfini ; ici, au pire, l'ordre est arbitraire.
 */
[[nodiscard]] Option<ScriptError> SortValues(Interpreter& vm, std::vector<Value>& items,
													const Value& comparator);

[[nodiscard]] String JoinDisplay(const char* prefix, const std::vector<Value>& items, const char* open = "[",
										const char* close = "]");

/// Éléments d'une valeur parcourable (liste, conteneur, chaîne…).
[[nodiscard]] Result<std::vector<Value>, ScriptError> ItemsOf(Interpreter& vm, const Value& source);

// ── Clés de tables (std.map, std.unordered_map, std.set…) ──────────────────

[[nodiscard]] const void* IdentityOf(const Value& v) noexcept;

[[nodiscard]] int KeyRank(const Value& v) noexcept;

/// Ordre total des clés : nil < booléens < nombres < chaînes < objets
/// (par identité). Les nombres NaN sont refusés à l'insertion.
[[nodiscard]] int KeyCompare(const Value& a, const Value& b) noexcept;

struct KeyLess {

	bool operator()(const Value& a, const Value& b) const noexcept {
	return KeyCompare(a, b) < 0;
}
};

/// Hachage cohérent avec l'égalité numérique (`1 == 1.0` : même case).
struct KeyHash {

	size_t operator()(const Value& v) const noexcept {
	switch (v.GetKind()) {
		case Value::Kind::BOOLEAN:
			return std::hash<bool>()(v.AsBoolean());
		case Value::Kind::NUMBER: {
			if (v.IsInteger()) {
				if (IsSignedType(v.GetNumberType()) && v.AsInt64() < 0)
					return std::hash<int64_t>()(v.AsInt64());
				return std::hash<uint64_t>()(v.AsUInt64());
			}
			const double d = v.AsNumber();
			if (d == std::floor(d) && d >= 0.0 && d < 1.8446744073709552e19)
				return std::hash<uint64_t>()(static_cast<uint64_t>(d));
			if (d == std::floor(d) && d < 0.0 && d >= -9.2233720368547758e18)
				return std::hash<int64_t>()(static_cast<int64_t>(d));
			return std::hash<double>()(d);
		}
		case Value::Kind::STRING:
			return std::hash<std::string_view>()(std::string_view(v.AsString().CStr(), v.AsString().GetSize()));
		case Value::Kind::NIL:
			return 0;
		default:
			return std::hash<const void*>()(IdentityOf(v));
	}
}
};

struct KeyEqual {

	bool operator()(const Value& a, const Value& b) const noexcept {
	if (KeyRank(a) != KeyRank(b))
		return false;
	if (KeyRank(a) == 4)
		return IdentityOf(a) == IdentityOf(b);
	return a.Equals(b);
}
};

[[nodiscard]] Option<ScriptError> CheckKey(const Value& key, const char* fn);

// ── Flux sortants des conteneurs ────────────────────────────────────────────

[[nodiscard]] Result<Value, ScriptError> NewVector(Interpreter& vm, std::vector<Value> items);

/**
 * `conteneur -> puits` : chaque élément s'écoule dans le puits (`-> print`,
 * `-> pipe`, `-> liste`) ; face à une FONCTION, c'est une transformation :
 * un nouveau `std.vector` de ses résultats (`nil` écarté : un filtre).
 */
[[nodiscard]] Result<Value, ScriptError> EmitItems(Interpreter& vm, const std::vector<Value>& items,
														  const Value& sink);

} // namespace lib

// ============================================================================
// Conteneurs séquentiels : std.vector, std.list (chaînée), std.deque
// ============================================================================

namespace lib {

/// Conteneur séquentiel de la bibliothèque C++ derrière son verrou.
template <class Container> struct SequenceObject : HostObject {
	mutable std::mutex mutex;
	Container items;

	[[nodiscard]] std::vector<Value> Snapshot() const {
		std::lock_guard<std::mutex> lock(mutex);
		return std::vector<Value>(items.begin(), items.end());
	}
	void Assign(std::vector<Value> values) {
		Container next(std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
		std::lock_guard<std::mutex> lock(mutex);
		items.swap(next); // l'ancien contenu est détruit hors du verrou (fin de portée de `next`)
	}
	[[nodiscard]] size_t Size() const {
		std::lock_guard<std::mutex> lock(mutex);
		return items.size();
	}
};

using VectorObject = SequenceObject<std::vector<Value>>;
using LinkedListObject = SequenceObject<std::list<Value>>;
using DequeObject = SequenceObject<std::deque<Value>>;

template <class Container> [[nodiscard]] typename Container::iterator NthOf(Container& c, size_t i) {
	auto it = c.begin();
	std::advance(it, static_cast<ptrdiff_t>(i));
	return it;
}

/// Nouvel objet du type `type` (vector, list, deque) rempli de `values`.
template <class Container>
[[nodiscard]] Result<Value, ScriptError> MakeSequence(const std::shared_ptr<const HostType>& type,
													  std::vector<Value> values, std::vector<Value> typeArgs = {}) {
	if (!type)
		return Err(Fail(String("type de conteneur indisponible")));
	auto object = std::make_shared<SequenceObject<Container>>();
	object->type = type;
	object->typeArgs = std::move(typeArgs);
	object->items = Container(std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
	return Ok(Value::Host(std::move(object)));
}

Result<Value, ScriptError> NewVector(Interpreter& vm, std::vector<Value> items);

/// Éléments conformes aux arguments de type de l'objet (insertion typée).
[[nodiscard]] Result<std::vector<Value>, ScriptError> ConformAll(Interpreter& vm, const HostObject& object,
																		std::vector<Value> values);

/**
 * Méthodes communes aux conteneurs séquentiels (`if constexpr` pour ce qui
 * n'existe que sur certains : `push_front` sur list/deque, `reserve` sur
 * vector…). Indices négatifs : depuis la fin.
 */
template <class Container> void DefineSequence(TypeBuilder& builder, const char* label) {
	using Object = SequenceObject<Container>;
	constexpr bool IS_VECTOR = std::is_same_v<Container, std::vector<Value>>;
	constexpr bool IS_LIST = std::is_same_v<Container, std::list<Value>>;
	constexpr bool HAS_FRONT = !IS_VECTOR;
	const String name(label);
	auto fnName = [name](const char* method) { return String::Format("%s.%s", name.CStr(), method); };
	std::shared_ptr<HostType> type = builder.type;
	std::weak_ptr<const HostType> weakType = type;

	builder.Construct(
		0, 2, [weakType, fnName](Interpreter& vm, Args& args, const Args& typeArgs) -> Result<Value, ScriptError> {
			std::vector<Value> values;
			if (args.size() == 1 && !args[0].IsNumber()) {
				auto items = ItemsOf(vm, args[0]);
				if (items.IsError())
					return Err(items.Error());
				values = items.Unwrap();
			} else if (!args.empty()) {
				auto count = ArgInt(args, 0, fnName("new").CStr());
				if (count.IsError())
					return Err(count.Error());
				if (count.Value() < 0 || count.Value() > 100000000)
					return Err(Fail(String::Format("`%s` : taille %lld invalide", fnName("new").CStr(),
												   static_cast<long long>(count.Value()))));
				values.assign(size_t(count.Value()), args.size() > 1 ? args[1] : Value::Nil());
			}
			auto made = MakeSequence<Container>(weakType.lock(), {}, typeArgs);
			if (made.IsError())
				return made;
			auto conformed = ConformAll(vm, *made.Value().AsHost(), std::move(values));
			if (conformed.IsError())
				return Err(conformed.Error());
			As<Object>(made.Value().AsHost()).Assign(conformed.Unwrap());
			return made;
		});

	builder.Method("push", 1, -1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto values = ConformAll(vm, *self, args);
		if (values.IsError())
			return Err(values.Error());
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		for (Value& v : values.Value())
			o.items.push_back(std::move(v));
		return Ok(Value::Host(self));
	});
	builder.Alias("push_back", "push").Alias("append", "push");
	builder.Method("pop", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		if (o.items.empty())
			return Ok(Value::Nil());
		Value last = o.items.back();
		o.items.pop_back();
		return Ok(last);
	});
	builder.Alias("pop_back", "pop");
	if constexpr (HAS_FRONT) {
		builder.Method("push_front", 1, 1,
					   [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   auto value = Conform(vm, *self, 0, args[0]);
						   if (value.IsError())
							   return value;
						   Object& o = As<Object>(self);
						   std::lock_guard<std::mutex> lock(o.mutex);
						   o.items.push_front(value.Unwrap());
						   return Ok(Value::Host(self));
					   });
		builder.Method("pop_front", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			Object& o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			if (o.items.empty())
				return Ok(Value::Nil());
			Value first = o.items.front();
			o.items.pop_front();
			return Ok(first);
		});
	}
	builder.Method("size", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Int(int64_t(As<Object>(self).Size())));
	});
	builder.Alias("len", "size");
	builder.Method("empty", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Boolean(As<Object>(self).Size() == 0));
	});
	builder.Alias("is_empty", "empty");
	builder.Method("clear", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		As<Object>(self).Assign({});
		return Ok(Value::Host(self));
	});
	builder.Method("front", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(o.items.empty() ? Value::Nil() : o.items.front());
	});
	builder.Method("back", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(o.items.empty() ? Value::Nil() : o.items.back());
	});
	// Lecture : `at` refuse un indice hors bornes, `get` rend nil.
	builder.Method("at", 1, 1, [fnName](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto raw = ArgInt(args, 0, fnName("at").CStr());
		if (raw.IsError())
			return Err(raw.Error());
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		auto index = ToIndex(raw.Value(), o.items.size(), fnName("at").CStr());
		if (index.IsError())
			return Err(index.Error());
		return Ok(*NthOf(o.items, index.Value()));
	});
	builder.Method("get", 1, 2, [fnName](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto raw = ArgInt(args, 0, fnName("get").CStr());
		if (raw.IsError())
			return Err(raw.Error());
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		auto index = ToIndex(raw.Value(), o.items.size(), "get");
		if (index.IsError())
			return Ok(args.size() > 1 ? args[1] : Value::Nil());
		return Ok(*NthOf(o.items, index.Value()));
	});
	builder.Method("set", 2, 2,
				   [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   auto raw = ArgInt(args, 0, fnName("set").CStr());
					   if (raw.IsError())
						   return Err(raw.Error());
					   auto value = Conform(vm, *self, 0, args[1]);
					   if (value.IsError())
						   return value;
					   Object& o = As<Object>(self);
					   Value old;
					   std::lock_guard<std::mutex> lock(o.mutex);
					   auto index = ToIndex(raw.Value(), o.items.size(), fnName("set").CStr());
					   if (index.IsError())
						   return Err(index.Error());
					   std::swap(*NthOf(o.items, index.Value()), old);
					   *NthOf(o.items, index.Value()) = value.Unwrap();
					   return Ok(Value::Host(self));
				   });
	builder.Method("insert", 2, 2,
				   [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   auto raw = ArgInt(args, 0, fnName("insert").CStr());
					   if (raw.IsError())
						   return Err(raw.Error());
					   auto value = Conform(vm, *self, 0, args[1]);
					   if (value.IsError())
						   return value;
					   Object& o = As<Object>(self);
					   std::lock_guard<std::mutex> lock(o.mutex);
					   auto index = ToIndex(raw.Value(), o.items.size(), fnName("insert").CStr(), true);
					   if (index.IsError())
						   return Err(index.Error());
					   o.items.insert(NthOf(o.items, index.Value()), value.Unwrap());
					   return Ok(Value::Host(self));
				   });
	builder.Method("erase", 1, 1,
				   [fnName](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   auto raw = ArgInt(args, 0, fnName("erase").CStr());
					   if (raw.IsError())
						   return Err(raw.Error());
					   Object& o = As<Object>(self);
					   std::lock_guard<std::mutex> lock(o.mutex);
					   auto index = ToIndex(raw.Value(), o.items.size(), fnName("erase").CStr());
					   if (index.IsError())
						   return Err(index.Error());
					   auto it = NthOf(o.items, index.Value());
					   Value removed = *it;
					   o.items.erase(it);
					   return Ok(removed);
				   });
	// Retire TOUTES les valeurs égales ; rend leur nombre.
	builder.Method("remove", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::vector<Value> kept, removed;
		for (Value& item : o.Snapshot())
			(item.Equals(args[0]) ? removed : kept).push_back(std::move(item));
		o.Assign(std::move(kept));
		return Ok(Value::Int(int64_t(removed.size())));
	});
	builder.Method("contains", 1, 1,
				   [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   for (const Value& item : As<Object>(self).Snapshot()) {
						   auto equal = vm.ValuesEqual(item, args[0]);
						   if (equal.IsError())
							   return Err(equal.Error());
						   if (equal.Value())
							   return Ok(Value::Boolean(true));
					   }
					   return Ok(Value::Boolean(false));
				   });
	builder.Method("index_of", 1, 1,
				   [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   const std::vector<Value> items = As<Object>(self).Snapshot();
					   for (size_t i = 0; i < items.size(); ++i) {
						   auto equal = vm.ValuesEqual(items[i], args[0]);
						   if (equal.IsError())
							   return Err(equal.Error());
						   if (equal.Value())
							   return Ok(Value::Int(int64_t(i)));
					   }
					   return Ok(Value::Int(-1));
				   });
	builder.Alias("find", "index_of");
	builder.Method("to_list", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::List(std::make_shared<ListObject>(As<Object>(self).Snapshot())));
	});
	builder.Method("copy", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return MakeSequence<Container>(self->type, As<Object>(self).Snapshot(), self->typeArgs);
	});
	builder.Method("reverse", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		std::reverse(o.items.begin(), o.items.end());
		return Ok(Value::Host(self));
	});
	// `sort()` : ordre naturel ; `sort(fn(a, b) { return a > b })` : le
	// comparateur « a avant b ». Tri stable.
	builder.Method("sort", 0, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::vector<Value> items = o.Snapshot();
		if (auto error = SortValues(vm, items, args.empty() ? Value::Nil() : args[0]); error.IsSome())
			return Err(error.Unwrap());
		o.Assign(std::move(items));
		return Ok(Value::Host(self));
	});
	builder.Method("extend", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto items = ItemsOf(vm, args[0]);
		if (items.IsError())
			return Err(items.Error());
		auto values = ConformAll(vm, *self, items.Unwrap());
		if (values.IsError())
			return Err(values.Error());
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		for (Value& v : values.Value())
			o.items.push_back(std::move(v));
		return Ok(Value::Host(self));
	});
	builder.Method("for_each", 1, 1,
				   [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   if (auto error = ArgCallable(vm, args, 0, fnName("for_each").CStr()); error.IsSome())
						   return Err(error.Unwrap());
					   for (const Value& item : As<Object>(self).Snapshot()) {
						   auto result = vm.CallValue(args[0], {item}, 0, 0);
						   if (result.IsError())
							   return result;
					   }
					   return Ok(Value::Nil());
				   });
	builder.Method("map", 1, 1,
				   [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   if (auto error = ArgCallable(vm, args, 0, fnName("map").CStr()); error.IsSome())
						   return Err(error.Unwrap());
					   std::vector<Value> results;
					   for (const Value& item : As<Object>(self).Snapshot()) {
						   auto result = vm.CallValue(args[0], {item}, 0, 0);
						   if (result.IsError())
							   return result;
						   results.push_back(result.Unwrap());
					   }
					   return MakeSequence<Container>(self->type, std::move(results));
				   });
	builder.Method("filter", 1, 1,
				   [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   if (auto error = ArgCallable(vm, args, 0, fnName("filter").CStr()); error.IsSome())
						   return Err(error.Unwrap());
					   std::vector<Value> kept;
					   for (const Value& item : As<Object>(self).Snapshot()) {
						   auto keep = vm.CallValue(args[0], {item}, 0, 0);
						   if (keep.IsError())
							   return keep;
						   if (keep.Value().IsTruthy())
							   kept.push_back(item);
					   }
					   return MakeSequence<Container>(self->type, std::move(kept), self->typeArgs);
				   });
	builder.Method("reduce", 2, 2,
				   [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   if (auto error = ArgCallable(vm, args, 0, fnName("reduce").CStr()); error.IsSome())
						   return Err(error.Unwrap());
					   Value accumulator = args[1];
					   for (const Value& item : As<Object>(self).Snapshot()) {
						   auto next = vm.CallValue(args[0], {accumulator, item}, 0, 0);
						   if (next.IsError())
							   return next;
						   accumulator = next.Unwrap();
					   }
					   return Ok(accumulator);
				   });
	builder.Method("join", 0, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		const String separator = args.empty() ? String(", ") : args[0].ToDisplayString();
		String out;
		const std::vector<Value> items = As<Object>(self).Snapshot();
		for (size_t i = 0; i < items.size(); ++i) {
			auto text = vm.Stringify(items[i]);
			if (text.IsError())
				return Err(text.Error());
			if (i > 0)
				out.Concat(separator);
			out.Concat(text.Value());
		}
		return Ok(Value::Str(std::move(out)));
	});
	if constexpr (IS_VECTOR) {
		builder.Method("reserve", 1, 1,
					   [fnName](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   auto n = ArgInt(args, 0, fnName("reserve").CStr());
						   if (n.IsError())
							   return Err(n.Error());
						   if (n.Value() < 0 || n.Value() > 100000000)
							   return Err(Fail(String("`vector.reserve` : capacité invalide")));
						   Object& o = As<Object>(self);
						   std::lock_guard<std::mutex> lock(o.mutex);
						   o.items.reserve(size_t(n.Value()));
						   return Ok(Value::Host(self));
					   });
		builder.Method("capacity", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			Object& o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			return Ok(Value::Int(int64_t(o.items.capacity())));
		});
		builder.Method("shrink_to_fit", 0, 0,
					   [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
						   Object& o = As<Object>(self);
						   std::lock_guard<std::mutex> lock(o.mutex);
						   o.items.shrink_to_fit();
						   return Ok(Value::Host(self));
					   });
		builder.Method("resize", 1, 2,
					   [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   auto n = ArgInt(args, 0, fnName("resize").CStr());
						   if (n.IsError())
							   return Err(n.Error());
						   if (n.Value() < 0 || n.Value() > 100000000)
							   return Err(Fail(String("`vector.resize` : taille invalide")));
						   Value fill = args.size() > 1 ? args[1] : Value::Nil();
						   Object& o = As<Object>(self);
						   if (size_t(n.Value()) > o.Size()) {
							   auto conformed = Conform(vm, *self, 0, fill);
							   if (conformed.IsError())
								   return conformed;
							   fill = conformed.Unwrap();
						   }
						   std::vector<Value> items = o.Snapshot();
						   items.resize(size_t(n.Value()), fill);
						   o.Assign(std::move(items));
						   return Ok(Value::Host(self));
					   });
		builder.Method("swap", 2, 2,
					   [fnName](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   auto a = ArgInt(args, 0, fnName("swap").CStr());
						   auto b = ArgInt(args, 1, fnName("swap").CStr());
						   if (a.IsError())
							   return Err(a.Error());
						   if (b.IsError())
							   return Err(b.Error());
						   Object& o = As<Object>(self);
						   std::lock_guard<std::mutex> lock(o.mutex);
						   auto i = ToIndex(a.Value(), o.items.size(), fnName("swap").CStr());
						   auto j = ToIndex(b.Value(), o.items.size(), fnName("swap").CStr());
						   if (i.IsError())
							   return Err(i.Error());
						   if (j.IsError())
							   return Err(j.Error());
						   std::swap(o.items[i.Value()], o.items[j.Value()]);
						   return Ok(Value::Host(self));
					   });
		builder.Method("slice", 1, 2,
					   [fnName](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   const std::vector<Value> items = As<Object>(self).Snapshot();
						   auto from = ArgInt(args, 0, fnName("slice").CStr());
						   if (from.IsError())
							   return Err(from.Error());
						   int64_t to = int64_t(items.size());
						   if (args.size() > 1) {
							   auto end = ArgInt(args, 1, fnName("slice").CStr());
							   if (end.IsError())
								   return Err(end.Error());
							   to = end.Value();
						   }
						   const int64_t size = int64_t(items.size());
						   int64_t a = from.Value() < 0 ? from.Value() + size : from.Value();
						   int64_t b = to < 0 ? to + size : to;
						   a = std::clamp<int64_t>(a, 0, size);
						   b = std::clamp<int64_t>(b, a, size);
						   return MakeSequence<Container>(
							   self->type, std::vector<Value>(items.begin() + a, items.begin() + b), self->typeArgs);
					   });
	}
	if constexpr (IS_LIST) {
		// Retire les doublons CONSÉCUTIFS (std::list::unique).
		builder.Method("unique", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			Object& o = As<Object>(self);
			std::vector<Value> items = o.Snapshot();
			std::vector<Value> kept;
			for (Value& item : items)
				if (kept.empty() || !kept.back().Equals(item))
					kept.push_back(std::move(item));
			o.Assign(std::move(kept));
			return Ok(Value::Host(self));
		});
	}

	// ── Crochets ──
	type->index = [fnName](Interpreter&, const HostRef& self, const Value& key) -> Result<Value, ScriptError> {
		if (!key.IsNumber())
			return Err(Fail(
				String::Format("`%s[…]` : indice numérique attendu, trouvé `%s`", fnName("").CStr(), key.TypeName())));
		Args args{key};
		auto raw = ArgInt(args, 0, fnName("[]").CStr());
		if (raw.IsError())
			return Err(raw.Error());
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		auto index = ToIndex(raw.Value(), o.items.size(), fnName("[]").CStr());
		if (index.IsError())
			return Err(index.Error());
		return Ok(*NthOf(o.items, index.Value()));
	};
	type->setIndex = [fnName](Interpreter& vm, const HostRef& self, const Value& key,
							  Value value) -> Option<ScriptError> {
		Args args{key};
		auto raw = ArgInt(args, 0, fnName("[]").CStr());
		if (raw.IsError())
			return Some(raw.Error());
		auto conformed = Conform(vm, *self, 0, std::move(value));
		if (conformed.IsError())
			return Some(conformed.Error());
		Object& o = As<Object>(self);
		Value old;
		std::lock_guard<std::mutex> lock(o.mutex);
		auto index = ToIndex(raw.Value(), o.items.size(), fnName("[]").CStr());
		if (index.IsError())
			return Some(index.Error());
		auto it = NthOf(o.items, index.Value());
		std::swap(*it, old);
		*it = conformed.Unwrap();
		return NONE;
	};
	type->items = [](const HostObject& self) { return As<Object>(self).Snapshot(); };
	type->size = [](const HostObject& self) { return As<Object>(self).Size(); };
	const String prefix = String(label);
	type->display = [prefix](const HostObject& self) {
		return JoinDisplay(prefix.CStr(), As<Object>(self).Snapshot());
	};
	type->equals = [](const HostObject& a, const HostObject& b) {
		const std::vector<Value> x = As<Object>(a).Snapshot(), y = As<Object>(b).Snapshot();
		if (x.size() != y.size())
			return false;
		for (size_t i = 0; i < x.size(); ++i)
			if (!x[i].Equals(y[i]))
				return false;
		return true;
	};
	type->flowIn = [](Interpreter& vm, const HostRef& self, const Value& value) -> Result<Value, ScriptError> {
		auto conformed = Conform(vm, *self, 0, value);
		if (conformed.IsError())
			return conformed;
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		o.items.push_back(conformed.Unwrap());
		return Ok(Value::Host(self));
	};
	type->flowOut = [](Interpreter& vm, const HostRef& self, const Value& sink) -> Result<Value, ScriptError> {
		return EmitItems(vm, As<Object>(self).Snapshot(), sink);
	};
	// `a + b` : concaténation de deux conteneurs du même type.
	type->binary = [](Interpreter&, BinaryOp op, const Value& a, const Value& b) -> Option<Result<Value, ScriptError>> {
		if (op != BinaryOp::ADD || !a.IsHost() || !b.IsHost() || a.AsHost()->type != b.AsHost()->type)
			return NONE;
		std::vector<Value> items = As<Object>(*a.AsHost()).Snapshot();
		for (Value& item : As<Object>(*b.AsHost()).Snapshot())
			items.push_back(std::move(item));
		return Some(MakeSequence<Container>(a.AsHost()->type, std::move(items), a.AsHost()->typeArgs));
	};
}

// ============================================================================
// Adaptateurs : std.queue (FIFO), std.stack (LIFO), std.priority_queue
// ============================================================================
//
// Comme en C++, on n'y lit que l'extrémité utile. `q -> puits` les VIDE
// (chaque élément sort dans l'ordre où `pop` le rendrait).

struct AdapterObject : HostObject {

	enum class Kind : uint8_t { QUEUE, STACK, PRIORITY };
	mutable std::mutex mutex;
	Kind kind = Kind::QUEUE;
	std::deque<Value> items; ///< file : avant = sortie ; pile : arrière = sortie ; priorité : tas (max en tête)
	Value comparator; ///< priorité : « a avant b » du script (nil : ordre naturel, le plus GRAND sort d'abord)
};

/// Tas binaire à la main : un comparateur du script peut échouer ou être
/// incohérent, ce que std::push_heap ne tolère pas.
[[nodiscard]] Option<ScriptError> HeapPush(Interpreter& vm, std::vector<Value>& heap, Value value,
												  const Value& comparator);

[[nodiscard]] Result<Value, ScriptError> HeapPop(Interpreter& vm, std::vector<Value>& heap,
														const Value& comparator);

void DefineAdapter(TypeBuilder& builder, AdapterObject::Kind kind);

} // namespace lib

// ============================================================================
// Tables associatives : std.map (ordonnée), std.unordered_map, std.set,
// std.unordered_set
// ============================================================================
//
// Clés : nombres, chaînes, booléens, ou objets (par identité). `std.map<K, V>`
// vérifie les clés contre K et les valeurs contre V ; `std.set<T>` ses
// éléments contre T.

namespace lib {

template <class Table> struct TableObject : HostObject {
	mutable std::mutex mutex;
	Table entries;
};

using OrderedMap = std::map<Value, Value, KeyLess>;
using HashMap = std::unordered_map<Value, Value, KeyHash, KeyEqual>;
using OrderedSet = std::set<Value, KeyLess>;
using HashSet = std::unordered_set<Value, KeyHash, KeyEqual>;

template <class Table> constexpr bool IS_SET = std::is_same_v<Table, OrderedSet> || std::is_same_v<Table, HashSet>;
template <class Table>
constexpr bool IS_ORDERED = std::is_same_v<Table, OrderedMap> || std::is_same_v<Table, OrderedSet>;

/// Clé (et valeur) conformes aux arguments de type : `<K, V>` pour une
/// table, `<T>` pour un ensemble.
[[nodiscard]] Result<Value, ScriptError> ConformKey(Interpreter& vm, const HostObject& object, Value key,
														   const char* fn);

template <class Table> [[nodiscard]] std::vector<Value> KeysOf(const TableObject<Table>& o) {
	std::lock_guard<std::mutex> lock(o.mutex);
	std::vector<Value> out;
	out.reserve(o.entries.size());
	for (const auto& entry : o.entries) {
		if constexpr (IS_SET<Table>)
			out.push_back(entry);
		else
			out.push_back(entry.first);
	}
	return out;
}

template <class Table> [[nodiscard]] std::vector<std::pair<Value, Value>> PairsOf(const TableObject<Table>& o) {
	std::lock_guard<std::mutex> lock(o.mutex);
	std::vector<std::pair<Value, Value>> out;
	if constexpr (!IS_SET<Table>)
		for (const auto& entry : o.entries)
			out.emplace_back(entry.first, entry.second);
	return out;
}

[[nodiscard]] Value Pair(const Value& a, const Value& b);

template <class Table>
[[nodiscard]] Result<Value, ScriptError> MakeTable(const std::shared_ptr<const HostType>& type, Args typeArgs) {
	auto object = std::make_shared<TableObject<Table>>();
	object->type = type;
	object->typeArgs = std::move(typeArgs);
	return Ok(Value::Host(std::move(object)));
}

/// Ajoute `[clé, valeur]` (table) ou `élément` (ensemble) — ce que reçoit
/// l'opérateur de flux, et ce que parcourt le constructeur.
template <class Table>
[[nodiscard]] Option<ScriptError> InsertItem(Interpreter& vm, const HostRef& self, const Value& item, bool replace,
											 const char* fn) {
	using Object = TableObject<Table>;
	Object& o = As<Object>(self);
	if constexpr (IS_SET<Table>) {
		auto key = ConformKey(vm, o, item, fn);
		if (key.IsError())
			return Some(key.Error());
		std::lock_guard<std::mutex> lock(o.mutex);
		o.entries.insert(key.Unwrap());
		return NONE;
	} else {
		Value key, value;
		if (item.IsList() && item.AsList() && item.AsList()->Size() == 2) {
			key = item.AsList()->At(0).Unwrap();
			value = item.AsList()->At(1).Unwrap();
		} else {
			return Some(
				Fail(String::Format("`%s` : attendu une paire [clé, valeur], trouvé `%s`", fn, item.TypeName())));
		}
		auto k = ConformKey(vm, o, key, fn);
		if (k.IsError())
			return Some(k.Error());
		auto v = Conform(vm, o, 1, value);
		if (v.IsError())
			return Some(v.Error());
		Value old;
		std::lock_guard<std::mutex> lock(o.mutex);
		auto it = o.entries.find(k.Value());
		if (it == o.entries.end())
			o.entries.emplace(k.Unwrap(), v.Unwrap());
		else if (replace) {
			std::swap(old, it->second);
			it->second = v.Unwrap();
		}
		return NONE;
	}
}

template <class Table> void DefineTable(TypeBuilder& builder) {
	using Object = TableObject<Table>;
	constexpr bool SET = IS_SET<Table>;
	std::weak_ptr<const HostType> weakType = builder.type;
	const String name = builder.type->name;
	auto fnName = [name](const char* method) { return String::Format("%s.%s", name.CStr(), method); };

	// `std.map()`, `std.map({a: 1})` (table du script), `std.map([[k, v], …])`,
	// `std.set([1, 2, 3])`.
	builder.Construct(
		0, 1, [weakType, fnName](Interpreter& vm, Args& args, const Args& typeArgs) -> Result<Value, ScriptError> {
			auto made = MakeTable<Table>(weakType.lock(), typeArgs);
			if (made.IsError() || args.empty())
				return made;
			const HostRef& self = made.Value().AsHost();
			if constexpr (!SET) {
				if (args[0].IsMap() && args[0].AsMap()) {
					for (const auto& entry : args[0].AsMap()->Snapshot())
						if (auto error = InsertItem<Table>(vm, self, Pair(Value::Str(entry.first), entry.second), true,
														   fnName("new").CStr());
							error.IsSome())
							return Err(error.Unwrap());
					return made;
				}
			}
			auto items = ItemsOf(vm, args[0]);
			if (items.IsError())
				return Err(items.Error());
			for (const Value& item : items.Value())
				if (auto error = InsertItem<Table>(vm, self, item, true, fnName("new").CStr()); error.IsSome())
					return Err(error.Unwrap());
			return made;
		});

	if constexpr (SET) {
		builder.Method(
			"insert", 1, -1, [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
				for (const Value& item : args)
					if (auto error = InsertItem<Table>(vm, self, item, true, fnName("insert").CStr()); error.IsSome())
						return Err(error.Unwrap());
				return Ok(Value::Host(self));
			});
		builder.Alias("add", "insert");
	} else {
		builder.Method(
			"set", 2, 2, [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
				if (auto error = InsertItem<Table>(vm, self, Pair(args[0], args[1]), true, fnName("set").CStr());
					error.IsSome())
					return Err(error.Unwrap());
				return Ok(Value::Host(self));
			});
		// `insert` n'écrase pas une clé existante (comme std::map::insert) ;
		// rend vrai si la paire a été ajoutée.
		builder.Method(
			"insert", 2, 2, [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
				Object& o = As<Object>(self);
				bool existed = false;
				{
					std::lock_guard<std::mutex> lock(o.mutex);
					existed = o.entries.find(args[0]) != o.entries.end();
				}
				if (existed)
					return Ok(Value::Boolean(false));
				if (auto error = InsertItem<Table>(vm, self, Pair(args[0], args[1]), false, fnName("insert").CStr());
					error.IsSome())
					return Err(error.Unwrap());
				return Ok(Value::Boolean(true));
			});
		builder.Method("get", 1, 2, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			Object& o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			auto it = o.entries.find(args[0]);
			if (it == o.entries.end())
				return Ok(args.size() > 1 ? args[1] : Value::Nil());
			return Ok(it->second);
		});
		builder.Method("at", 1, 1,
					   [fnName](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   Object& o = As<Object>(self);
						   std::lock_guard<std::mutex> lock(o.mutex);
						   auto it = o.entries.find(args[0]);
						   if (it == o.entries.end())
							   return Err(Fail(String::Format("`%s` : clé absente `%s`", fnName("at").CStr(),
															  args[0].ToDisplayString().CStr())));
						   return Ok(it->second);
					   });
		builder.Method("values", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			std::vector<Value> out;
			for (auto& entry : PairsOf(As<Object>(self)))
				out.push_back(std::move(entry.second));
			return Ok(Value::List(std::make_shared<ListObject>(std::move(out))));
		});
		builder.Method("items", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			std::vector<Value> out;
			for (auto& entry : PairsOf(As<Object>(self)))
				out.push_back(Pair(entry.first, entry.second));
			return Ok(Value::List(std::make_shared<ListObject>(std::move(out))));
		});
		// Table du script (clés converties en texte).
		builder.Method("to_map", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			auto map = std::make_shared<MapObject>();
			for (auto& entry : PairsOf(As<Object>(self)))
				map->SetKey(entry.first.ToDisplayString(), std::move(entry.second));
			return Ok(Value::Map(std::move(map)));
		});
		builder.Method("for_each", 1, 1,
					   [fnName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   if (auto error = ArgCallable(vm, args, 0, fnName("for_each").CStr()); error.IsSome())
							   return Err(error.Unwrap());
						   for (auto& entry : PairsOf(As<Object>(self))) {
							   auto result = vm.CallValue(args[0], {entry.first, entry.second}, 0, 0);
							   if (result.IsError())
								   return result;
						   }
						   return Ok(Value::Nil());
					   });
	}
	builder.Method("has", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Boolean(o.entries.find(args[0]) != o.entries.end()));
	});
	builder.Alias("contains", "has");
	builder.Method("erase", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		Table released;
		std::lock_guard<std::mutex> lock(o.mutex);
		auto it = o.entries.find(args[0]);
		if (it == o.entries.end())
			return Ok(Value::Boolean(false));
		released.insert(o.entries.extract(it));
		return Ok(Value::Boolean(true));
	});
	builder.Alias("remove", "erase");
	builder.Method("keys", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::List(std::make_shared<ListObject>(KeysOf(As<Object>(self)))));
	});
	builder.Alias("to_list", "keys");
	builder.Method("size", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Int(int64_t(o.entries.size())));
	});
	builder.Alias("len", "size");
	builder.Method("empty", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(Value::Boolean(o.entries.empty()));
	});
	builder.Alias("is_empty", "empty");
	builder.Method("clear", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		Object& o = As<Object>(self);
		Table released;
		std::lock_guard<std::mutex> lock(o.mutex);
		released.swap(o.entries);
		return Ok(Value::Host(self));
	});
	if constexpr (IS_ORDERED<Table>) {
		// Première / dernière clé, et bornes (`lower_bound` : première clé ≥ k).
		auto edge = [](bool first) {
			return [first](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
				std::vector<Value> keys = KeysOf(As<Object>(self));
				if (keys.empty())
					return Ok(Value::Nil());
				return Ok(first ? keys.front() : keys.back());
			};
		};
		builder.Method("first", 0, 0, edge(true));
		builder.Method("last", 0, 0, edge(false));
		builder.Method("lower_bound", 1, 1,
					   [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   Object& o = As<Object>(self);
						   std::lock_guard<std::mutex> lock(o.mutex);
						   auto it = o.entries.lower_bound(args[0]);
						   if (it == o.entries.end())
							   return Ok(Value::Nil());
						   if constexpr (SET)
							   return Ok(*it);
						   else
							   return Ok(it->first);
					   });
		builder.Method("upper_bound", 1, 1,
					   [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
						   Object& o = As<Object>(self);
						   std::lock_guard<std::mutex> lock(o.mutex);
						   auto it = o.entries.upper_bound(args[0]);
						   if (it == o.entries.end())
							   return Ok(Value::Nil());
						   if constexpr (SET)
							   return Ok(*it);
						   else
							   return Ok(it->first);
					   });
	} else {
		builder.Method("bucket_count", 0, 0,
					   [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
						   Object& o = As<Object>(self);
						   std::lock_guard<std::mutex> lock(o.mutex);
						   return Ok(Value::Int(int64_t(o.entries.bucket_count())));
					   });
		builder.Method("load_factor", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			Object& o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			return Ok(Value::Number(double(o.entries.load_factor())));
		});
	}
	if constexpr (SET) {
		// Opérations ensemblistes : un nouvel ensemble du même type.
		auto combine = [](int mode) {
			return [mode](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
				auto other = ItemsOf(vm, args[0]);
				if (other.IsError())
					return Err(other.Error());
				Table theirs(other.Value().begin(), other.Value().end());
				std::vector<Value> mine = KeysOf(As<Object>(self));
				auto made = MakeTable<Table>(self->type, self->typeArgs);
				Object& out = As<Object>(made.Value().AsHost());
				for (const Value& v : mine) {
					const bool inOther = theirs.find(v) != theirs.end();
					if (mode == 0 || (mode == 1 && inOther) || (mode == 2 && !inOther))
						out.entries.insert(v);
				}
				if (mode == 0)
					for (const Value& v : theirs)
						out.entries.insert(v);
				return made;
			};
		};
		builder.Method("union", 1, 1, combine(0));
		builder.Method("intersection", 1, 1, combine(1));
		builder.Method("difference", 1, 1, combine(2));
	}

	HostType& type = *builder.type;
	if constexpr (!SET) {
		type.index = [](Interpreter&, const HostRef& self, const Value& key) -> Result<Value, ScriptError> {
			Object& o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			auto it = o.entries.find(key);
			return Ok(it == o.entries.end() ? Value::Nil() : it->second);
		};
		type.setIndex = [fnName](Interpreter& vm, const HostRef& self, const Value& key,
								 Value value) -> Option<ScriptError> {
			return InsertItem<Table>(vm, self, Pair(key, value), true, fnName("[]").CStr());
		};
	}
	type.items = [](const HostObject& self) { return KeysOf(As<Object>(self)); };
	type.size = [](const HostObject& self) {
		std::lock_guard<std::mutex> lock(As<Object>(self).mutex);
		return As<Object>(self).entries.size();
	};
	type.display = [name](const HostObject& self) {
		if constexpr (SET) {
			return JoinDisplay(name.CStr(), KeysOf(As<Object>(self)), "{", "}");
		} else {
			String out = name + String("{");
			bool first = true;
			for (const auto& entry : PairsOf(As<Object>(self))) {
				if (!first)
					out.Concat(", ");
				first = false;
				out.Concat(entry.first.IsString() ? String::Format("\"%s\"", entry.first.AsString().CStr())
												  : entry.first.ToDisplayString());
				out.Concat(": ");
				out.Concat(entry.second.ToDisplayString());
			}
			out.Concat("}");
			return out;
		}
	};
	type.flowIn = [fnName](Interpreter& vm, const HostRef& self, const Value& item) -> Result<Value, ScriptError> {
		if (auto error = InsertItem<Table>(vm, self, item, true, fnName("<-").CStr()); error.IsSome())
			return Err(error.Unwrap());
		return Ok(Value::Host(self));
	};
	// Émet ses éléments : les clés d'un ensemble, les paires [k, v] d'une table.
	type.flowOut = [](Interpreter& vm, const HostRef& self, const Value& sink) -> Result<Value, ScriptError> {
		std::vector<Value> items;
		if constexpr (SET)
			items = KeysOf(As<Object>(self));
		else
			for (auto& entry : PairsOf(As<Object>(self)))
				items.push_back(Pair(entry.first, entry.second));
		return EmitItems(vm, items, sink);
	};
}

} // namespace lib

// ============================================================================
// Texte : std.string (chaîne modifiable, std::string), std.stream (flux de
// texte, std::stringstream)
// ============================================================================
//
// Octets UTF-8, comme std::string : `size()` compte des octets, `[i]` rend
// un octet (sous forme de chaîne d'un caractère).

namespace lib {

struct StringObject : HostObject {

	mutable std::mutex mutex;
	std::string text;

	[[nodiscard]] std::string Get() const;
};

[[nodiscard]] String ToText(const std::string& s);

[[nodiscard]] std::string ToStd(const String& s);

/// Texte d'un argument : chaîne du script, `std.string`, ou tout autre valeur
/// (sa forme affichée, `operator str` compris).
[[nodiscard]] Result<std::string, ScriptError> TextOf(Interpreter& vm, const Value& value);

[[nodiscard]] Result<Value, ScriptError> MakeStringObject(const std::shared_ptr<const HostType>& type,
																 std::string text);

/// Encode un point de code en UTF-8.
void AppendUtf8(std::string& out, uint32_t cp);

/// Fonctions de texte partagées par `std.string.xxx(s, …)` (sur une chaîne
/// du script) et les méthodes de l'objet `std.string` (sur son contenu).
struct TextFn {

	const char* name;
	int minArity, maxArity; ///< SANS compter le texte lui-même
	std::function<Result<Value, ScriptError>(Interpreter&, const std::string&, Args&)> fn;
};

[[nodiscard]] std::vector<TextFn> TextFunctions();

void DefineString(TypeBuilder& builder);

// ── std.stream ──────────────────────────────────────────────────────────────

struct StreamObject : HostObject {

	mutable std::mutex mutex;
	std::string buffer;
	size_t readPos = 0;
};

/// Ligne suivante (sans son `\n`) ; NONE en fin de flux.
[[nodiscard]] Option<std::string> ReadLine(StreamObject& o);

void DefineStream(TypeBuilder& builder);

} // namespace lib

// ============================================================================
// std.pipe — canal entre fils et nœud d'un réseau de flux
// ============================================================================
//
//   let entrée = std.pipe()
//   let sortie = std.pipe()
//   entrée -> fn(x) { return x * 2 } -> sortie   # un pipe « transformé » au milieu
//   entrée <- 21                                 # sortie.receive() == 42
//   a <-> b                                      # liaison dans les deux sens
//
// Une valeur envoyée dans un pipe CONNECTÉ s'écoule vers ses puits (autres
// pipes, fonctions, listes, conteneurs…) ; un pipe sans puits la GARDE pour
// `receive` (bloquant, réveillé par variable de condition), `try_receive`
// ou `drain`. Un pipe transformé (`p -> fn`) applique la fonction à chaque
// valeur (nil = écartée : un filtre). Les cycles (`a <-> b`) sont coupés :
// une valeur ne repasse jamais par un pipe qu'elle a déjà traversé — elle
// s'arrête dans le dernier pipe qui ne peut plus la transmettre.

namespace lib {

struct PipeObject : HostObject {

	mutable std::mutex mutex;
	std::condition_variable ready;
	std::deque<Value> buffer;
	std::vector<Value> sinks;
	/// Pipes en aval qui REMONTENT jusqu'à celui-ci (`a <-> b`, `a -> b -> a`) :
	/// tenus faiblement, sans quoi le cycle de `shared_ptr` ne serait jamais
	/// libéré.
	std::vector<std::weak_ptr<HostObject>> backLinks;

	/// Puits vivants (les liens faibles expirés sont ignorés).
	[[nodiscard]] std::vector<Value> Sinks() const;

	Value transform; ///< nil, ou la fonction appliquée à chaque valeur
	bool closed = false;
};

[[nodiscard]] bool IsPipe(const Value& value);

/// Pipes traversés par la valeur en cours de livraison (sur ce fil).
[[nodiscard]] std::vector<const PipeObject*>& PipePath();

/// Livre `value` dans `pipe` : rend faux si la valeur y est déjà passée
/// (cycle), vrai sinon (transmise, gardée ou écartée par un filtre).
[[nodiscard]] Result<bool, ScriptError> Deliver(Interpreter& vm, const HostRef& pipeRef, Value value);

/// Vrai si `from` (un pipe) mène à `target` par des liens FORTS.
[[nodiscard]] bool Reaches(const PipeObject& from, const PipeObject* target, int depth = 0);

/// Connecte `sink` en aval ; ce que le pipe gardait s'y écoule aussitôt.
[[nodiscard]] Result<Value, ScriptError> Connect(Interpreter& vm, const HostRef& pipeRef, const Value& sink);

[[nodiscard]] Result<Value, ScriptError> NewPipe(const std::shared_ptr<const HostType>& type,
														Value transform = Value());

/// Attend une valeur (`timeoutMs` < 0 : sans limite) ; nil si le pipe est
/// fermé et vide, si le délai expire ou si l'interpréteur s'arrête. Sur le
/// fil principal, les appels des fils `async` sont traités pendant l'attente.
[[nodiscard]] Result<Value, ScriptError> Receive(Interpreter& vm, PipeObject& pipe, double timeoutMs);

void DefinePipe(TypeBuilder& builder);

} // namespace lib

// ============================================================================
// Fichiers et système : std.file (RAII), std.filesystem, std.os
// ============================================================================

namespace lib {

namespace fs = std::filesystem;

[[nodiscard]] String PathText(const fs::path& path);

[[nodiscard]] Result<fs::path, ScriptError> ArgPath(Interpreter& vm, const Args& args, size_t i, const char* fn);

[[nodiscard]] ScriptError FsError(const char* fn, const fs::path& path, const std::error_code& ec);

// ── std.file ────────────────────────────────────────────────────────────────
//
//   let f = std.file("notes.txt", "w")   # r, w, a, r+, w+, a+ (et `b`)
//   f.write_line("bonjour")
//   f <- "suite"                         # écrit
//   f -> print                           # (en lecture) émet chaque ligne
//
// RAII : le fichier se ferme (et se vide) quand le dernier accès au
// `std.file` disparaît — sortie de bloc comprise — ou par `close()`.

struct FileObject : HostObject {

	mutable std::mutex mutex;
	std::fstream stream;
	std::string path;
	std::string mode;
};

[[nodiscard]] Option<std::ios::openmode> ParseMode(const std::string& mode);

[[nodiscard]] Result<std::string, ScriptError> ReadWhole(const fs::path& path, const char* fn);

[[nodiscard]] Option<ScriptError> WriteWhole(const fs::path& path, const std::string& text, bool append,
													const char* fn);

void DefineFile(TypeBuilder& builder);

// ── std.filesystem ─────────────────────────────────────────────────────────

void InstallFilesystem(Interpreter& vm);

// ── std.os ──────────────────────────────────────────────────────────────────

void InstallOs(Interpreter& vm);

} // namespace lib

// ============================================================================
// Dates et heures : std.datetime, std.date, std.time (std::chrono)
// ============================================================================
//
//   let d = std.date(2026, 9, 27)       d.add_days(10), d.weekday (1 = lundi)
//   let t = std.time(12, 30)            t.add_seconds(90).format("%H:%M")
//   let n = std.datetime.now()          n.format("%d/%m/%Y %H:%M"), n.date()
//   std.date(2026, 12, 25) - d          # 89 (jours)
//
// Heure LOCALE par défaut (`std.datetime.utc_now()` pour UTC). `format`
// suit strftime (%Y %m %d %H %M %S %A %B…).

namespace lib {

using Millis = std::chrono::sys_time<std::chrono::milliseconds>;
constexpr int64_t MS_PER_DAY = 86400000;

[[nodiscard]] std::tm BreakDown(Millis at, bool utc);

[[nodiscard]] Millis Compose(std::tm fields, int64_t millis, bool utc);

[[nodiscard]] Result<String, ScriptError> FormatTm(const std::tm& fields, const std::string& format);

/// Lit `text` selon `format` (std::get_time) ; NONE s'il ne correspond pas.
[[nodiscard]] Option<std::tm> ParseTm(const std::string& text, const std::string& format);

/// Jour ISO de la semaine : 1 = lundi … 7 = dimanche.
[[nodiscard]] int64_t IsoWeekday(int tmWday);

struct DateTimeObject : HostObject {

	Millis at;
	bool utc = false;
};

struct DateObject : HostObject {

	std::chrono::sys_days day;
};

struct TimeObject : HostObject {

	int64_t millis = 0; ///< depuis minuit, dans [0, 24 h[
};

[[nodiscard]] Value MakeDateTime(const std::shared_ptr<const HostType>& type, Millis at, bool utc);

[[nodiscard]] Value MakeDate(const std::shared_ptr<const HostType>& type, std::chrono::sys_days day);

[[nodiscard]] Value MakeTime(const std::shared_ptr<const HostType>& type, int64_t millis);

[[nodiscard]] std::tm DateFields(std::chrono::sys_days day);

[[nodiscard]] std::tm TimeFields(int64_t millis);

/// Date validée ; `add_months` ramène le jour dans le mois (31 → 30…).
[[nodiscard]] Result<std::chrono::sys_days, ScriptError> MakeDay(int64_t y, int64_t m, int64_t d, bool clampDay,
																		const char* fn);

[[nodiscard]] Result<std::string, ScriptError> FormatArg(Interpreter& vm, const Args& args, size_t i,
																const char* fallback);

void DefineDateTime(TypeBuilder& builder, TypeBuilder& dateBuilder, TypeBuilder& timeBuilder);

} // namespace lib

// ============================================================================
// std.option, std.result, std.future, std.mutex / std.lock_guard
// ============================================================================

namespace lib {

// ── std.option : une valeur ou rien ─────────────────────────────────────────
//   std.option.some(3), std.option.none(), std.option(v) (nil -> none)
//   o.is_some() o.unwrap() o.unwrap_or(d) o.map(fn) o.and_then(fn) o.filter(fn)

struct OptionObject : HostObject {

	bool some = false;
	Value value;
};

[[nodiscard]] Value MakeOption(const std::shared_ptr<const HostType>& type, Option<Value> value);

void DefineOption(TypeBuilder& builder);

// ── std.result : une valeur ou une erreur ───────────────────────────────────
//   std.result.ok(v), std.result.err(e), std.result.of(fn, args…) — ce
//   dernier CAPTURE l'erreur d'exécution de `fn` au lieu de l'interrompre
//   (le `try` du langage, sans exception).

struct ResultObject : HostObject {

	bool ok = true;
	Value value;
};

[[nodiscard]] Value MakeResult(const std::shared_ptr<const HostType>& type, bool ok, Value value);

void DefineResult(TypeBuilder& builder, std::weak_ptr<const HostType> optionType);

// ── std.future : std.future(fn, args…) lance `fn` sur son propre fil ────────
// (comme std::async) ; les fonctions de classe sont celles de `future`.

void DefineFuture(Interpreter& vm, TypeBuilder& builder);

// ── std.mutex et std.lock_guard (RAII) ──────────────────────────────────────
//   let m = std.mutex()
//   { let garde = std.lock_guard(m) ; … }   # déverrouillé en sortie de bloc
//   m.with(fn() { … })                      # verrouillé le temps de l'appel
// Un verrou du script, pas un std::mutex brut : l'attente reste
// interruptible (arrêt de l'interpréteur) et, sur le fil principal, continue
// de servir les appels des fils `async`.

struct MutexObject : HostObject {

	std::mutex mutex;
	std::condition_variable released;
	bool locked = false;
};

[[nodiscard]] Option<ScriptError> LockMutex(Interpreter& vm, MutexObject& m);

void UnlockMutex(MutexObject& m);

struct LockGuardObject : HostObject {

	std::shared_ptr<HostObject> mutex; ///< un MutexObject
	bool held = false;
	~LockGuardObject() override;
};

void DefineMutex(TypeBuilder& builder, TypeBuilder& guardBuilder);

} // namespace lib

// ============================================================================
// Installation de l'espace de noms `std`
// ============================================================================

void InstallStdLibrary(Interpreter& vm);

} // namespace data::script
