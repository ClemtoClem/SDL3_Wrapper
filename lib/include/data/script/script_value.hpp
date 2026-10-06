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
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "../../core/core.hpp"
#include "../node.hpp"
#include "script_ast.hpp"

namespace data::script {

class Value;
class Interpreter;
class Environment;

struct OwnerObject; // data/script/script_owners.hpp

// ── Données partagées entre fils d'exécution ────────────────────────────────
//
// Une fonction `async` s'exécute sur son propre fil (cf. Interpreter) : une
// liste, une table ou un objet peut donc être lu et modifié par plusieurs
// fils à la fois. Chacun porte son `mutex`, et TOUT accès de l'interpréteur
// passe par les méthodes ci-dessous (verrou `lock_guard` le temps d'une
// copie) — aucune référence vers l'intérieur ne sort jamais du verrou.
//
// Les champs (`items`, `entries`) restent publics pour l'HÔTE C++ qui les
// remplit ou les lit hors concurrence (sur le fil principal, arguments d'une
// native de l'hôte — copiés pour elle quand l'appel vient d'un autre fil).

struct ListObject {
	mutable std::mutex mutex;
	std::vector<Value> items;

	ListObject() = default;
	explicit ListObject(std::vector<Value> values) : items(std::move(values)) {}

	[[nodiscard]] size_t Size() const;
	/// Élément `index`, NONE hors bornes.
	[[nodiscard]] Option<Value> At(size_t index) const;
	/// Faux hors bornes.
	bool Set(size_t index, Value value);
	void Push(Value value);
	/// Copie du contenu : ce qu'on parcourt sans garder le verrou.
	[[nodiscard]] std::vector<Value> Snapshot() const;
	void Replace(std::vector<Value> values);
	/// Écrit `value` seulement si l'élément vaut encore `expected` (cf.
	/// Interpreter : `l[i] += 1` sans perte de mise à jour).
	bool CompareAndSet(size_t index, const Value &expected, Value value);
};

struct MapObject {
	mutable std::mutex mutex;
	std::vector<std::pair<String, Value>> entries;

	/// Accès DIRECT sans verrou — pour l'hôte hors concurrence seulement.
	[[nodiscard]] Value *Find(const String &key) noexcept;
	[[nodiscard]] const Value *Find(const String &key) const noexcept;
	/// Lecture verrouillée (copie), NONE si la clé est absente.
	[[nodiscard]] Option<Value> Get(const String &key) const;
	void SetKey(const String &key, Value value);
	bool RemoveKey(const String &key);
	[[nodiscard]] size_t Size() const;
	[[nodiscard]] std::vector<std::pair<String, Value>> Snapshot() const;
	bool CompareAndSet(const String &key, const Value &expected, Value value);
};

/// Fonction définie dans le script : sa définition partagée (cf.
/// script_ast.hpp) + l'environnement capturé à la création (fermeture).
struct FunctionObject {
	FunctionDefPtr def;
	std::shared_ptr<Environment> closure;
	/// Arguments de type d'une fonction générique spécialisée (`max<i32>`) :
	/// des valeurs `type`, une par paramètre de type.
	std::vector<Value> typeArgs;
};

/// Fonction fournie par l'hôte C++. Reçoit l'interpréteur (pour rappeler du
/// script, lever une erreur positionnée, lire l'état) et les arguments déjà
/// évalués. Retourne un `Result` : une native qui échoue interrompt le script
/// proprement, exactement comme une erreur d'exécution interne.
using NativeFn = std::function<Result<Value, ScriptError>(Interpreter &, std::vector<Value> &)>;

struct HostType;

struct NativeObject {
	String name;
	/// Constructeur d'un type de l'hôte (`std.vector`, `math.vec3`) : c'est
	/// par lui que le type est nommé dans le script (`x is std.vector`,
	/// `let v:std.vector<i32>`, `std.vector.from(…)`).
	std::shared_ptr<const HostType> hostType;
	NativeFn fn;
	int minArity = 0;
	int maxArity = -1; ///< -1 = variadique
	/// Appelable depuis n'importe quel fil (bibliothèque standard, calcul
	/// pur). Faux pour l'API de l'HÔTE : appelée depuis une fonction `async`,
	/// elle est exécutée sur le fil principal (cf. Interpreter::PumpMainThread).
	bool anyThread = false;
};

/// Classe ou interface (cf. ClassDef, script_ast.hpp) et instance — définies
/// APRÈS Value, qu'elles contiennent (cf. plus bas). Une méthode LIÉE à son
/// instance n'a pas de forme propre : c'est une fonction ordinaire dont la
/// fermeture porte `this` (cf. Interpreter::BindMethod).
struct ClassObject;
struct InstanceObject;
struct FutureObject;
struct HostObject;
struct TypeObject;
[[nodiscard]] String DescribeFuture(const FutureObject &future);
[[nodiscard]] String DescribeClass(const ClassObject &klass);
[[nodiscard]] String DescribeInstance(const InstanceObject &instance);
[[nodiscard]] bool IsInterfaceClass(const ClassObject &klass) noexcept;
[[nodiscard]] const char *HostTypeName(const HostObject *object) noexcept;
[[nodiscard]] bool HostEquals(const HostObject *a, const HostObject *b);
[[nodiscard]] String DescribeHost(const HostObject *object);
[[nodiscard]] String DescribeType(const TypeObject &type);

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
// Types numériques
// ============================================================================

/// Type d'un nombre : entiers signés/non signés de 1 à 8 octets, flottants de
/// 4 et 8 octets. Un littéral entier est un `i64`, un littéral à virgule (ou
/// à exposant) un `f64` ; une variable typée (`let x:u8`) CONVERTIT la valeur
/// reçue vers son type (cf. Interpreter).
enum class NumberType : uint8_t { I8, I16, I32, I64, U8, U16, U32, U64, F32, F64 };

[[nodiscard]] const char *NumberTypeName(NumberType t) noexcept;
[[nodiscard]] constexpr bool IsIntegerType(NumberType t) noexcept { return t <= NumberType::U64; }
[[nodiscard]] constexpr bool IsSignedType(NumberType t) noexcept { return t <= NumberType::I64 || t >= NumberType::F32; }
/// Largeur en bits.
[[nodiscard]] constexpr int BitsOf(NumberType t) noexcept {
	switch (t) {
		case NumberType::I8:
		case NumberType::U8:
			return 8;
		case NumberType::I16:
		case NumberType::U16:
			return 16;
		case NumberType::I32:
		case NumberType::U32:
		case NumberType::F32:
			return 32;
		default:
			return 64;
	}
}
/// Type numérique d'un nom de type (`i32`…), NONE sinon.
[[nodiscard]] Option<NumberType> NumberTypeFromName(StringView name) noexcept;

// ============================================================================
// Value
// ============================================================================

class Value {
public:
	/// HOST : objet de l'hôte C++ (conteneur de `std`, fichier, vecteur de
	/// `math`…) ; TYPE : un type lié à un paramètre générique (`T`).
	enum class Kind : uint8_t {
		NIL,
		BOOLEAN,
		NUMBER,
		STRING,
		LIST,
		MAP,
		FUNCTION,
		NATIVE,
		NAMESPACE,
		CLASS,
		INSTANCE,
		FUTURE,
		HOST,
		TYPE
	};

	Value() = default;

	[[nodiscard]] static Value Nil() { return {}; }
	[[nodiscard]] static Value Boolean(bool v);
	/// Un `f64` (la forme de l'hôte : `Value::Number(2.5)`).
	[[nodiscard]] static Value Number(double v) { return Float(v, NumberType::F64); }
	/// Un flottant ; `f32` est arrondi à la précision simple.
	[[nodiscard]] static Value Float(double v, NumberType type = NumberType::F64);
	/// Un entier signé (`i64` par défaut) — `v` doit tenir dans `type`.
	[[nodiscard]] static Value Int(int64_t v, NumberType type = NumberType::I64);
	/// Un entier non signé (`u64` par défaut) — `v` doit tenir dans `type`.
	[[nodiscard]] static Value UInt(uint64_t v, NumberType type = NumberType::U64);
	[[nodiscard]] static Value Str(String v);
	[[nodiscard]] static Value List(std::shared_ptr<ListObject> v);
	[[nodiscard]] static Value EmptyList() { return List(std::make_shared<ListObject>()); }
	[[nodiscard]] static Value Map(std::shared_ptr<MapObject> v);
	[[nodiscard]] static Value EmptyMap() { return Map(std::make_shared<MapObject>()); }
	[[nodiscard]] static Value Function(std::shared_ptr<FunctionObject> v);
	[[nodiscard]] static Value Native(std::shared_ptr<NativeObject> v);
	[[nodiscard]] static Value Namespace(std::shared_ptr<NamespaceObject> v);
	[[nodiscard]] static Value Class(std::shared_ptr<ClassObject> v);
	[[nodiscard]] static Value Instance(std::shared_ptr<InstanceObject> v);
	[[nodiscard]] static Value Future(std::shared_ptr<FutureObject> v);
	[[nodiscard]] static Value Host(std::shared_ptr<HostObject> v);
	[[nodiscard]] static Value Type(std::shared_ptr<TypeObject> v);

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
	[[nodiscard]] bool IsClass() const noexcept { return m_kind == Kind::CLASS; }
	[[nodiscard]] bool IsInstance() const noexcept { return m_kind == Kind::INSTANCE; }
	[[nodiscard]] bool IsFuture() const noexcept { return m_kind == Kind::FUTURE; }
	[[nodiscard]] bool IsHost() const noexcept { return m_kind == Kind::HOST; }
	[[nodiscard]] bool IsType() const noexcept { return m_kind == Kind::TYPE; }
	[[nodiscard]] bool IsCallable() const noexcept { return IsFunction() || IsNative(); }

	[[nodiscard]] bool AsBoolean() const noexcept { return m_boolean; }
	/// Valeur numérique (quel que soit son type), en `double`.
	[[nodiscard]] double AsNumber() const noexcept;
	[[nodiscard]] float AsFloat() const noexcept { return float(AsNumber()); }
	[[nodiscard]] NumberType GetNumberType() const noexcept { return m_numberType; }
	[[nodiscard]] bool IsInteger() const noexcept { return m_kind == Kind::NUMBER && IsIntegerType(m_numberType); }
	[[nodiscard]] bool IsFloat() const noexcept { return m_kind == Kind::NUMBER && !IsIntegerType(m_numberType); }
	/// Entier signé (un non signé au-delà de INT64_MAX est saturé ; un
	/// flottant est tronqué).
	[[nodiscard]] int64_t AsInt64() const noexcept;
	/// Entier non signé (un négatif vaut 0 ; un flottant est tronqué).
	[[nodiscard]] uint64_t AsUInt64() const noexcept;
	[[nodiscard]] const String &AsString() const noexcept { return m_text; }
	[[nodiscard]] const std::shared_ptr<ListObject> &AsList() const noexcept { return m_list; }
	[[nodiscard]] const std::shared_ptr<MapObject> &AsMap() const noexcept { return m_map; }
	[[nodiscard]] const std::shared_ptr<FunctionObject> &AsFunction() const noexcept { return m_function; }
	[[nodiscard]] const std::shared_ptr<NativeObject> &AsNative() const noexcept { return m_native; }
	[[nodiscard]] const std::shared_ptr<NamespaceObject> &AsNamespace() const noexcept { return m_namespace; }
	[[nodiscard]] const std::shared_ptr<ClassObject> &AsClass() const noexcept { return m_class; }
	[[nodiscard]] const std::shared_ptr<InstanceObject> &AsInstance() const noexcept { return m_instance; }
	[[nodiscard]] const std::shared_ptr<FutureObject> &AsFuture() const noexcept { return m_future; }
	[[nodiscard]] const std::shared_ptr<HostObject> &AsHost() const noexcept { return m_host; }
	[[nodiscard]] const std::shared_ptr<TypeObject> &AsType() const noexcept { return m_type; }

	/// Véracité façon Lua : SEULS `nil` et `false` sont faux. `0` et `""`
	/// sont VRAIS — choix explicite (un script d'éditeur teste surtout des
	/// présences/absences, et `if count` sur un compteur à zéro serait un
	/// piège silencieux dans l'autre convention).
	[[nodiscard]] bool IsTruthy() const noexcept;

	[[nodiscard]] const char *TypeName() const noexcept;

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
				return NumbersEqual(*this, other);
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
			case Kind::CLASS:
				return m_class == other.m_class;
			case Kind::INSTANCE:
				return m_instance == other.m_instance;
			case Kind::FUTURE:
				return m_future == other.m_future;
			case Kind::HOST:
				return m_host == other.m_host || HostEquals(m_host.get(), other.m_host.get());
			case Kind::TYPE:
				return m_type == other.m_type;
		}
		return false;
	}

	/// Représentation textuelle (ce qu'affiche `print`). Un nombre entier
	/// s'écrit sans décimale (`3`, pas `3.000000`) — seule façon d'obtenir un
	/// `"objet " .. i` lisible dans un script.
	[[nodiscard]] String ToDisplayString() const;

	/// Formatage numérique partagé (cf. ToDisplayString) — exposé car l'hôte
	/// en a besoin pour composer les mêmes chaînes côté C++ (rapports, log).
	[[nodiscard]] static String NumberToString(double v);

private:
	Kind m_kind = Kind::NIL;
	bool m_boolean = false;
	/// Égalité NUMÉRIQUE entre types : `1 == 1.0`, `u8(3) == 3`.
	[[nodiscard]] static bool NumbersEqual(const Value &a, const Value &b) noexcept {
		if (a.IsInteger() && b.IsInteger()) {
			const bool as = IsSignedType(a.m_numberType), bs = IsSignedType(b.m_numberType);
			if (as && bs)
				return a.m_int == b.m_int;
			if (!as && !bs)
				return a.m_uint == b.m_uint;
			const int64_t s = as ? a.m_int : b.m_int;
			const uint64_t u = as ? b.m_uint : a.m_uint;
			return s >= 0 && static_cast<uint64_t>(s) == u;
		}
		return a.AsNumber() == b.AsNumber();
	}

	NumberType m_numberType = NumberType::F64;
	double m_number = 0.0;
	int64_t m_int = 0;
	uint64_t m_uint = 0;
	String m_text;
	std::shared_ptr<ListObject> m_list;
	std::shared_ptr<MapObject> m_map;
	std::shared_ptr<FunctionObject> m_function;
	std::shared_ptr<NativeObject> m_native;
	std::shared_ptr<NamespaceObject> m_namespace;
	std::shared_ptr<ClassObject> m_class;
	std::shared_ptr<InstanceObject> m_instance;
	std::shared_ptr<FutureObject> m_future;
	std::shared_ptr<HostObject> m_host;
	std::shared_ptr<TypeObject> m_type;
};

// ============================================================================
// Classes et instances (définies après Value, qu'elles contiennent)
// ============================================================================

/// Un champ vivant — d'instance ou de classe (`static`). `constant` : déclaré
/// `const`, il ne se réaffecte plus après son initialisation.
struct FieldSlot {
	String name;
	Value value;
	bool constant = false;
	TypeRef type; ///< `let x:i32` dans la classe — vérifié à chaque affectation
};

/// Recherche linéaire (même raison que MapObject : quelques champs).
[[nodiscard]] FieldSlot *FindField(std::vector<FieldSlot> &fields, const String &name) noexcept;

/// Champs d'un objet ou d'une classe, protégés par leur verrou (cf. « Données
/// partagées entre fils d'exécution » plus haut).
struct FieldSet {
	enum class WriteResult : uint8_t { OK, CREATED, CONSTANT, MISSING };

	mutable std::mutex mutex;
	std::vector<FieldSlot> slots;

	[[nodiscard]] Option<Value> Get(const String &name) const;
	/// Écrit un champ existant (refus s'il est constant) ; sinon le crée si
	/// `create`, ou rend MISSING.
	WriteResult Set(const String &name, Value value, bool create);
	/// Pose un champ À LA CONSTRUCTION (constant ou non, écrase l'existant).
	void Initialize(const String &name, Value value, bool constant, TypeRef type = nullptr);
	/// Type annoncé du champ `name` (nullptr : libre ou absent).
	[[nodiscard]] TypeRef TypeOf(const String &name) const;
	[[nodiscard]] bool Has(const String &name) const;
	[[nodiscard]] std::vector<FieldSlot> Snapshot() const;
	/// `champ += …` sans perte de mise à jour : écrit seulement si la valeur
	/// n'a pas changé depuis sa lecture.
	WriteResult CompareAndSet(const String &name, const Value &expected, Value value);
};

/// Classe (ou interface) à l'exécution : sa définition, ses parents RÉSOLUS,
/// la portée où elle a été déclarée (fermeture de ses méthodes) et ses
/// champs de classe.
struct ClassObject {
	ClassDefPtr def;
	std::shared_ptr<ClassObject> superclass;
	std::vector<std::shared_ptr<ClassObject>> interfaces;
	std::shared_ptr<Environment> closure;
	FieldSet statics;
	/// Valeurs d'un type énuméré, dans l'ordre de déclaration.
	std::vector<Value> enumValues;
	/// La classe ou un ancêtre déclare `deinit` : ses instances passent par
	/// la file de destruction (cf. InstanceObject) de l'interpréteur `owner`.
	bool hasDeinit = false;
	const Interpreter *owner = nullptr;
	std::shared_ptr<std::atomic<bool>> ownerAlive;
	/// Bases fournies par l'hôte (owners) : celles des ancêtres d'abord, puis
	/// celles que la classe déclare, dans l'ordre de `extends`. L'instance
	/// reçoit un `OwnerObject` par entrée (cf. script_owners.hpp).
	std::vector<std::shared_ptr<const HostType>> owners;

	[[nodiscard]] const String &Name() const noexcept { return def->name; }
	[[nodiscard]] bool IsInterface() const noexcept { return def->isInterface; }
	[[nodiscard]] bool IsAbstract() const noexcept { return def->isAbstract; }

	/// Vrai pour la classe elle-même, ses dérivées et — pour une interface —
	/// toute classe qui l'implémente, directement, par une interface dérivée
	/// ou par héritage.
	[[nodiscard]] bool IsSubtypeOf(const ClassObject &other) const noexcept;
};

struct InstanceObject {
	std::shared_ptr<ClassObject> klass;
	FieldSet fields;
	/// Classe générique : ses arguments de type (valeurs `type`), pour la
	/// classe de l'instance ET chacun de ses ancêtres génériques.
	std::vector<std::pair<const ClassObject *, std::vector<Value>>> typeArgs;
	/// `deinit` déjà passé (ou en cours) : ne pas le rappeler.
	bool finalized = false;
	/// Un par entrée de `klass->owners` (créés par Instantiate).
	std::vector<std::shared_ptr<OwnerObject>> owners;
	/// `destroy()` a commencé : la seconde tentative (appel manuel, puis fin
	/// de scène) ne fait rien.
	bool destroyed = false;

	InstanceObject() = default;
	InstanceObject(const InstanceObject &) = delete;
	InstanceObject &operator=(const InstanceObject &) = delete;
	/// RAII : la DERNIÈRE référence disparaît — si la classe a un `deinit`,
	/// les champs passent à un « fantôme » mis en file ; l'interpréteur
	/// exécute son `deinit` à la prochaine instruction (jamais ici même :
	/// une destruction peut survenir sous le verrou d'une portée).
	~InstanceObject();

	[[nodiscard]] const std::vector<Value> *TypeArgsOf(const ClassObject *level) const noexcept;
};

/// File de destruction du fil courant (cf. ~InstanceObject). Fermée à la
/// fin du fil : ce qui meurt ensuite n'a plus d'interpréteur pour `deinit`.
struct FinalizerQueue {
	std::vector<std::shared_ptr<InstanceObject>> pending;
	bool closed = false;
	~FinalizerQueue();
};

[[nodiscard]] FinalizerQueue &PendingFinalizers() noexcept;



// ============================================================================
// Types génériques et objets de l'hôte
// ============================================================================

/// Le type lié à un paramètre générique (`T`) pendant un appel, ou pour une
/// instance : donné explicitement (`max<i32>`, `Boîte<f32>()`), ou DÉDUIT de
/// la première valeur rencontrée (`max(1, 2)` : T = i64). Tant qu'il n'est
/// ni donné ni déduit, `T` accepte tout.
struct TypeObject {
	String name;
	mutable std::mutex mutex;
	TypeRef type;                             ///< type donné (ou déduit d'une valeur simple)
	std::shared_ptr<Environment> scope;       ///< où résoudre `type`
	std::shared_ptr<ClassObject> klass;       ///< déduit d'un objet du script
	std::shared_ptr<const HostType> hostType; ///< déduit d'un objet de l'hôte
	TypeRef bound;                            ///< `T extends Forme`
	std::shared_ptr<Environment> boundScope;

	[[nodiscard]] bool IsBound() const;
};

using HostRef = std::shared_ptr<HostObject>;

/// Méthode d'un type de l'hôte : reçoit l'objet (`self`) et les arguments,
/// arité vérifiée avant l'appel.
struct HostMethod {
	String name;
	int minArity = 0;
	int maxArity = 0; ///< -1 = variadique
	std::function<Result<Value, ScriptError>(Interpreter &, const HostRef &, std::vector<Value> &)> fn;
};

/**
 * Description d'un type de l'hôte (`std.vector`, `std.file`, `math.vec3`…) :
 * ses méthodes, ses membres de classe, son constructeur et les CROCHETS par
 * lesquels l'interpréteur lui délègue opérateurs, index, parcours et flux.
 * Un crochet absent = l'opération n'est pas supportée (erreur explicite).
 * Tout objet de l'hôte porte son propre verrou : les crochets sont appelés
 * depuis n'importe quel fil.
 */
struct HostType {
	String name; ///< nom qualifié : `std.vector`
	/// Objets NON sûrs entre fils (interface, monde ECS de l'hôte) : leurs
	/// méthodes, appelées depuis un fil `async`, s'exécutent sur le fil
	/// principal (cf. Interpreter::PumpMainThread), comme l'API de l'hôte.
	bool mainThreadOnly = false;
	std::vector<HostMethod> methods;
	/// Membres de classe (`std.vector.from`, `math.vec3.zero`).
	std::vector<std::pair<String, Value>> statics;
	int minArity = 0, maxArity = -1; ///< du constructeur
	/// Peut servir de BASE à une classe de script (`extends Mesh3D`) : un
	/// owner ne s'instancie pas directement (pas de `construct`), il naît
	/// avec l'instance qui en dérive (cf. script_owners.hpp).
	bool isOwner = false;

	/// Construction ; `typeArgs` : `std.vector<i32>()` (valeurs `type`).
	std::function<Result<Value, ScriptError>(Interpreter &, std::vector<Value> &, const std::vector<Value> &typeArgs)>
		construct;
	/// Propriété `obj.nom` (NONE : inconnue) ; écriture (Ok(false) : inconnue).
	std::function<Option<Value>(const HostObject &, const String &)> get;
	std::function<Result<bool, ScriptError>(Interpreter &, const HostRef &, const String &, const Value &)> set;
	/// Opérateur binaire où l'un des deux opérandes est de ce type (NONE :
	/// non supporté — l'interpréteur rend alors son erreur habituelle).
	std::function<Option<Result<Value, ScriptError>>(Interpreter &, BinaryOp, const Value &, const Value &)> binary;
	std::function<Option<Result<Value, ScriptError>>(Interpreter &, const HostRef &)> negate;
	std::function<Result<Value, ScriptError>(Interpreter &, const HostRef &, const Value &)> index;
	std::function<Option<ScriptError>(Interpreter &, const HostRef &, const Value &, Value)> setIndex;
	/// Ce que parcourt `for … in` (copie prise sous le verrou), et la taille.
	std::function<std::vector<Value>(const HostObject &)> items;
	std::function<size_t(const HostObject &)> size;
	std::function<String(const HostObject &)> display;
	std::function<bool(const HostObject &, const HostObject &)> equals;
	/// Flux : `valeur -> objet` (reçoit), `objet -> puits` (émet vers /
	/// se connecte à), `objet <-> autre` (liaison dans les deux sens).
	std::function<Result<Value, ScriptError>(Interpreter &, const HostRef &, const Value &)> flowIn;
	std::function<Result<Value, ScriptError>(Interpreter &, const HostRef &, const Value &)> flowOut;
	std::function<Result<Value, ScriptError>(Interpreter &, const HostRef &, const Value &)> link;
	/// `objet(args)`.
	std::function<Result<Value, ScriptError>(Interpreter &, const HostRef &, std::vector<Value> &)> call;
	/// Owner (`isOwner`) : fabrique l'état C++ de la base pour une nouvelle
	/// instance — un dérivé d'`OwnerObject` dont `OnInit`/`OnDeinit` sont le
	/// constructeur et le destructeur (cf. OwnerTypeBuilder).
	std::function<std::shared_ptr<OwnerObject>()> createOwner;

	[[nodiscard]] const HostMethod *FindMethod(const String &methodName) const noexcept;
	[[nodiscard]] const Value *FindStatic(const String &memberName) const noexcept;
};

/// Objet de l'hôte : dérivez-en (données + verrou), le type décrit le reste.
struct HostObject : std::enable_shared_from_this<HostObject> {
	std::shared_ptr<const HostType> type;
	/// `std.vector<i32>()` : arguments de type (valeurs `type`), vérifiés par
	/// les conteneurs à chaque insertion (cf. Interpreter::Conform).
	std::vector<Value> typeArgs;

	HostObject() = default;
	HostObject(const HostObject &) = delete;
	HostObject &operator=(const HostObject &) = delete;
	virtual ~HostObject() = default;
};

[[nodiscard]] const char *HostTypeName(const HostObject *object) noexcept;

[[nodiscard]] bool HostEquals(const HostObject *a, const HostObject *b);

[[nodiscard]] String DescribeHost(const HostObject *object);

[[nodiscard]] String DescribeType(const TypeObject &type);

/// Résultat à venir d'une fonction `async` (façon `Future` de Dart/Flutter) :
/// le fil qui l'exécute y dépose sa valeur ou son erreur puis réveille, par
/// la variable de condition, ceux qui l'attendent (`await`).
struct FutureObject {
	enum class State : uint8_t { PENDING, DONE, FAILED };

	mutable std::mutex mutex;
	std::condition_variable ready;
	State state = State::PENDING;
	Value value;
	ScriptError error;
	bool observed = false; ///< un `await` (ou `.then`) l'a lu : son erreur n'est pas orpheline

	void Complete(Result<Value, ScriptError> result);
	[[nodiscard]] State GetState() const;
};

[[nodiscard]] String DescribeFuture(const FutureObject &future);

[[nodiscard]] inline bool IsInterfaceClass(const ClassObject &klass) noexcept { return klass.IsInterface(); }

[[nodiscard]] String DescribeClass(const ClassObject &klass);

/// `Carré{côté: 2, nom: carré}` — ce qu'affiche `print(instance)`. Une
/// instance contenue s'abrège (`…`) : deux objets qui se citent ne
/// feraient pas boucler l'affichage.
[[nodiscard]] String DescribeInstance(const InstanceObject &instance);

// ── MapObject (défini après Value : ses entrées SONT des Value) ─────────────















// ── ListObject ──────────────────────────────────────────────────────────────

















// ============================================================================
// Pont data::Node <-> Value — ce qui raccroche le langage au reste de data::
// ============================================================================

/// Convertit un arbre `data::Node` (JSON/YAML/TOML/XML… déjà décodé) en
/// valeur de script : un document chargé par n'importe quel codec `data::`
/// devient directement lisible depuis un script, sans couche d'adaptation.
[[nodiscard]] Value ValueFromNode(const NodePtr &node);

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
[[nodiscard]] NodePtr NodeFromValue(const Value &value);


// ============================================================================
// Arithmétique typée
// ============================================================================
//
// Entier ⊕ entier : calcul EXACT sur 128 bits, puis le résultat doit tenir
// dans le type commun (le plus large ; à largeur égale et signes mêlés, le
// non signé si le résultat est positif, le signé sinon) — sinon erreur de
// dépassement (`u8(200) + u8(100)`). Un flottant dans l'opération : le
// résultat est flottant (f32 seulement si aucun f64 n'y entre). `/` rend
// toujours un flottant.

namespace numeric {

__extension__ typedef __int128 Wide; // GCC/Clang : exact pour tout couple d'entiers 64 bits

[[nodiscard]] Wide ToWide(const Value &v) noexcept;

/// Bornes (inclusives) d'un type entier.
[[nodiscard]] Wide LowOf(NumberType t) noexcept;
[[nodiscard]] Wide HighOf(NumberType t) noexcept;
[[nodiscard]] inline bool Fits(Wide v, NumberType t) noexcept { return v >= LowOf(t) && v <= HighOf(t); }

/// Un entier exact rangé dans `t` (qui doit le contenir).
[[nodiscard]] Value FromWide(Wide v, NumberType t);

[[nodiscard]] String WideToString(Wide v);

/// Type commun de deux entiers, vu le résultat `r` (cf. plus haut).
[[nodiscard]] NumberType CommonInteger(const Value &a, const Value &b, Wide r) noexcept;

/// Type flottant d'une opération où entre au moins un flottant.
[[nodiscard]] NumberType CommonFloat(const Value &a, const Value &b) noexcept;

/// Comparaison exacte : <0, 0, >0 (NaN : 2, jamais égal ni ordonné).
[[nodiscard]] int Compare(const Value &a, const Value &b) noexcept;

enum class Op : uint8_t { ADD, SUBTRACT, MULTIPLY, DIVIDE, MODULO };

/// `a op b` ; l'erreur est un message (division par zéro, dépassement).
[[nodiscard]] Result<Value, String> Apply(Op op, const Value &a, const Value &b);

/// `-v` : même type si possible ; un non signé devient un i64.
[[nodiscard]] Result<Value, String> Negate(const Value &v);

/// Conversion SANS PERTE vers `t` (variables, paramètres, champs typés) :
/// un entier doit tenir dans les bornes, un flottant vers un entier doit
/// être entier ; vers un flottant, l'arrondi est admis (f32 : pas au-delà
/// de sa plage). L'erreur décrit le refus.
[[nodiscard]] Result<Value, String> ConvertExact(const Value &v, NumberType t);

/// Transtypage façon C (`u8(300)` vaut 44) : partie entière, puis
/// repliement sur la largeur du type ; d'un flottant vers 64 bits,
/// saturation aux bornes. Vers f32 : arrondi à la précision simple.
[[nodiscard]] Result<Value, String> ConvertWrapping(const Value &v, NumberType t);

/// Nombre écrit dans une chaîne : entier exact si c'est un entier
/// (`"-12"`, `"18446744073709551615"`), sinon f64 ; NONE si ce n'en est pas un.
[[nodiscard]] Option<Value> Parse(const String &text);

} // namespace numeric

} // namespace data::script
