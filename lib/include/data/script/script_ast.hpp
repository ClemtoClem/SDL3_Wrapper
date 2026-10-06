#pragma once
/**
 * data::script — arbre syntaxique abstrait du langage embarqué.
 *
 * Hiérarchie polymorphe classique (base + `kind` explicite) plutôt qu'un
 * `std::variant` : l'interpréteur fait un `switch` sur `kind` puis un
 * `static_cast` — `std::get<T>` sur un variant LÈVE `std::bad_variant_access`
 * en cas de mauvais type, ce que ce dépôt interdit (cf.
 * memory/feedback_no_exceptions.md).
 *
 * Le corps d'une fonction vit dans un `FunctionDef` partagé (`shared_ptr`)
 * entre le nœud d'AST et la valeur `Function` créée à l'exécution : une
 * fermeture reste donc valide même si le `Program` qui l'a produite est
 * détruit — pas de pointeur pendouillant vers l'AST, sans avoir à faire
 * survivre tout le programme artificiellement.
 */
#include <memory>
#include <cstdint>
#include <mutex>
#include <vector>

#include "../../core/core.hpp"
#include "script_lexer.hpp"

namespace data::script {

// ============================================================================
// Expressions
// ============================================================================

enum class ExprKind : uint8_t {
	NUMBER,
	STRING_LITERAL,
	BOOLEAN,
	NIL,
	IDENTIFIER,
	LIST,
	MAP,
	UNARY,
	BINARY,
	LOGICAL,
	CALL,
	INDEX,
	MEMBER,
	FUNCTION,
	THIS,
	SUPER,
	AWAIT,
	IMPORT
};

struct Expr {
	ExprKind kind;
	int line = 0;
	int column = 0;

	Expr(ExprKind k, int lineNo, int columnNo) : kind(k), line(lineNo), column(columnNo) {}
	Expr(const Expr &) = delete;
	Expr &operator=(const Expr &) = delete;
	virtual ~Expr() = default;
};

using ExprPtr = std::unique_ptr<Expr>;

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

/// Annotation de type (`x:i32`, `fn f():list<i32>`), cf. TypeExpr plus bas.
struct TypeExpr;
using TypeRef = std::shared_ptr<const TypeExpr>;

/// Paramètre de type d'une fonction ou d'une classe générique : `T`, ou
/// `T extends Forme` (toute valeur liée à `T` doit alors être une `Forme`).
struct TypeParam {
	String name;
	TypeRef bound; ///< nullptr : sans contrainte
};

/// Paramètres + corps d'une fonction, partagés entre l'AST et la valeur
/// `Function` produite à l'exécution (cf. en-tête du fichier).
struct FunctionDef {
	String name; ///< vide pour une lambda
	/// `fn max<T>(a:T, b:T):T` — liés à l'appel (`max<i32>(…)`), ou déduits
	/// de la première valeur rencontrée.
	std::vector<TypeParam> typeParams;
	std::vector<String> params;
	/// `async fn` : chaque appel s'exécute sur son propre fil et rend
	/// aussitôt un `Future` (cf. Interpreter).
	bool isAsync = false;
	/// Types annoncés des paramètres (`nullptr` = libre), un par paramètre,
	/// et du résultat — vérifiés à chaque appel.
	std::vector<TypeRef> paramTypes;
	TypeRef returnType;
	std::vector<StmtPtr> body;
	int line = 0;
	int column = 0;
	/// Noms des `var` du corps (blocs imbriqués compris), calculés au premier
	/// appel puis gardés : le hissage se fait à CHAQUE appel, et un script
	/// appelé à chaque image ne doit pas reparcourir son corps pour ça.
	mutable std::vector<String> hoistedVars;
	mutable std::once_flag hoistOnce;

	FunctionDef() = default;
	FunctionDef(const FunctionDef &) = delete;
	FunctionDef &operator=(const FunctionDef &) = delete;
	~FunctionDef();
};

using FunctionDefPtr = std::shared_ptr<FunctionDef>;

/// Littéral numérique : entier (`42` — i64, ou u64 au-delà de INT64_MAX) ou
/// flottant (`4.2`, `1e3` — f64).
struct NumberExpr : Expr {
	double value = 0;
	bool isInteger = false;
	uint64_t integer = 0;
	NumberExpr(double v, int lineNo, int columnNo) : Expr(ExprKind::NUMBER, lineNo, columnNo), value(v) {}
	NumberExpr(uint64_t v, int lineNo, int columnNo)
		: Expr(ExprKind::NUMBER, lineNo, columnNo), value(double(v)), isInteger(true), integer(v) {}
};

struct StringExpr : Expr {
	String value;
	StringExpr(String v, int lineNo, int columnNo)
		: Expr(ExprKind::STRING_LITERAL, lineNo, columnNo), value(std::move(v)) {}
};

struct BooleanExpr : Expr {
	bool value = false;
	BooleanExpr(bool v, int lineNo, int columnNo) : Expr(ExprKind::BOOLEAN, lineNo, columnNo), value(v) {}
};

struct NilExpr : Expr {
	NilExpr(int lineNo, int columnNo) : Expr(ExprKind::NIL, lineNo, columnNo) {}
};

struct IdentifierExpr : Expr {
	String name;
	IdentifierExpr(String n, int lineNo, int columnNo)
		: Expr(ExprKind::IDENTIFIER, lineNo, columnNo), name(std::move(n)) {}
};

struct ListExpr : Expr {
	std::vector<ExprPtr> elements;
	ListExpr(int lineNo, int columnNo) : Expr(ExprKind::LIST, lineNo, columnNo) {}
};

struct MapEntry {
	ExprPtr key;
	ExprPtr value;
};

struct MapExpr : Expr {
	std::vector<MapEntry> entries;
	MapExpr(int lineNo, int columnNo) : Expr(ExprKind::MAP, lineNo, columnNo) {}
};

enum class UnaryOp : uint8_t { NEGATE, NOT };

struct UnaryExpr : Expr {
	UnaryOp op;
	ExprPtr operand;
	UnaryExpr(UnaryOp o, ExprPtr e, int lineNo, int columnNo)
		: Expr(ExprKind::UNARY, lineNo, columnNo), op(o), operand(std::move(e)) {}
};

/// `this` : l'instance sur laquelle la méthode courante a été appelée.
struct ThisExpr : Expr {
	ThisExpr(int lineNo, int columnNo) : Expr(ExprKind::THIS, lineNo, columnNo) {}
};

/// `super.méthode` (ou `super(…)`, abrégé de `super.init(…)`) : la méthode de
/// la classe PARENTE de celle qui définit la méthode courante, liée au même
/// `this`.
struct SuperExpr : Expr {
	String method;
	SuperExpr(String m, int lineNo, int columnNo) : Expr(ExprKind::SUPER, lineNo, columnNo), method(std::move(m)) {}
};

/// `await expr` : attend le `Future` que rend `expr` (une autre valeur est
/// rendue telle quelle, comme en Dart).
struct AwaitExpr : Expr {
	ExprPtr operand;
	AwaitExpr(ExprPtr e, int lineNo, int columnNo) : Expr(ExprKind::AWAIT, lineNo, columnNo), operand(std::move(e)) {}
};

enum class BinaryOp : uint8_t {
	ADD,
	SUBTRACT,
	MULTIPLY,
	DIVIDE,
	MODULO,
	CONCAT,
	EQUAL,
	NOT_EQUAL,
	LESS,
	LESS_EQUAL,
	GREATER,
	GREATER_EQUAL,
	IS, ///< `valeur is Classe` : instance de la classe (ou d'une dérivée), ou de l'interface
	AS, ///< `valeur as Classe` : la valeur elle-même si elle `is Classe`, sinon une erreur (transtypage vérifié)
	FLOW_RIGHT, ///< `a -> b` : a s'écoule dans b (appel, envoi dans un pipe, connexion)
	FLOW_LEFT,  ///< `a <- b` : b s'écoule dans a
	FLOW_BOTH,  ///< `a <-> b` : liaison dans les deux sens
};

struct BinaryExpr : Expr {
	BinaryOp op;
	ExprPtr left;
	ExprPtr right;
	BinaryExpr(BinaryOp o, ExprPtr l, ExprPtr r, int lineNo, int columnNo)
		: Expr(ExprKind::BINARY, lineNo, columnNo), op(o), left(std::move(l)), right(std::move(r)) {}
};

enum class LogicalOp : uint8_t { AND, OR };

struct LogicalExpr : Expr {
	LogicalOp op;
	ExprPtr left;
	ExprPtr right;
	LogicalExpr(LogicalOp o, ExprPtr l, ExprPtr r, int lineNo, int columnNo)
		: Expr(ExprKind::LOGICAL, lineNo, columnNo), op(o), left(std::move(l)), right(std::move(r)) {}
};

struct CallExpr : Expr {
	ExprPtr callee;
	std::vector<ExprPtr> args;
	/// Écrit `new Classe(…)` : l'appelé DOIT être une classe (sans `new`,
	/// appeler une classe l'instancie aussi, façon Python/Dart).
	bool isNew = false;
	/// Arguments de type explicites : `max<i32>(1, 2)`, `new Boîte<f32>(…)`.
	std::vector<TypeRef> typeArgs;
	CallExpr(ExprPtr c, int lineNo, int columnNo)
		: Expr(ExprKind::CALL, lineNo, columnNo), callee(std::move(c)) {}
};

struct IndexExpr : Expr {
	ExprPtr object;
	ExprPtr index;
	IndexExpr(ExprPtr o, ExprPtr i, int lineNo, int columnNo)
		: Expr(ExprKind::INDEX, lineNo, columnNo), object(std::move(o)), index(std::move(i)) {}
};

/// `a.b` — sucre syntaxique pour `a["b"]`, conservé comme nœud distinct pour
/// produire un message d'erreur qui parle de « champ » et non d'« index ».
struct MemberExpr : Expr {
	ExprPtr object;
	String name;
	MemberExpr(ExprPtr o, String n, int lineNo, int columnNo)
		: Expr(ExprKind::MEMBER, lineNo, columnNo), object(std::move(o)), name(std::move(n)) {}
};

struct FunctionExpr : Expr {
	FunctionDefPtr def;
	FunctionExpr(FunctionDefPtr d, int lineNo, int columnNo)
		: Expr(ExprKind::FUNCTION, lineNo, columnNo), def(std::move(d)) {}
};

/// `import "chemin"` : exécute UNE SEULE FOIS le module nommé et rend sa
/// valeur — son `return` de plus haut niveau, ou, à défaut, un `namespace`
/// de ses déclarations de premier niveau. Le fichier est cherché (dans
/// l'ordre) : chemin absolu, répertoire du script importateur, répertoires
/// enregistrés par l'hôte (`Interpreter::AddImportPath`) ; extension
/// `.script` ajoutée si absente. Un module déjà chargé rend la
/// MÊME valeur (cache par chemin canonique). Les imports cycliques sont une
/// erreur. Le module s'exécute dans sa propre portée de FONCTION (hissage
/// des `var` compris), dont le parent est la portée globale de
/// l'interpréteur — les natives (`print`, `std`, `math`…) restent visibles.
struct ImportExpr : Expr {
	ExprPtr specifier;
	ImportExpr(ExprPtr e, int lineNo, int columnNo)
		: Expr(ExprKind::IMPORT, lineNo, columnNo), specifier(std::move(e)) {}
};

// ============================================================================
// Instructions
// ============================================================================

enum class StmtKind : uint8_t {
	EXPRESSION,
	LET,
	ASSIGN,
	BLOCK,
	IF,
	WHILE,
	FOR_IN,
	FUNCTION,
	CLASS,
	RETURN,
	BREAK,
	CONTINUE,
	NAMESPACE,
};

struct Stmt {
	StmtKind kind;
	int line = 0;
	int column = 0;

	Stmt(StmtKind k, int lineNo, int columnNo) : kind(k), line(lineNo), column(columnNo) {}
	Stmt(const Stmt &) = delete;
	Stmt &operator=(const Stmt &) = delete;
	virtual ~Stmt() = default;
};

inline FunctionDef::~FunctionDef() = default;

struct ExpressionStmt : Stmt {
	ExprPtr expression;
	ExpressionStmt(ExprPtr e, int lineNo, int columnNo)
		: Stmt(StmtKind::EXPRESSION, lineNo, columnNo), expression(std::move(e)) {}
};

/// Forme d'une déclaration de variable — elle décide de la portée et des
/// droits (cf. memory/project_script_language.md) :
///
///   | forme   | portée              | réaffectation | redéclaration (même portée) |
///   |---------|---------------------|---------------|-----------------------------|
///   | `var`   | fonction ou globale | oui           | oui                         |
///   | `let`   | bloc `{}`           | oui           | non                         |
///   | `const` | bloc `{}`           | NON           | non                         |
///
/// Toutes sont hissées au début de leur portée : `var` y vaut `nil` avant sa
/// ligne ; `let`/`const` y sont en « zone morte » (lecture = erreur).
enum class DeclKind : uint8_t { LET, VAR, CONST };

[[nodiscard]] const char *DeclKindName(DeclKind kind) noexcept;

struct LetStmt : Stmt {
	String name;
	ExprPtr initializer; ///< nullptr => `nil` (interdit pour `const`), ou la valeur par défaut du type
	DeclKind declKind = DeclKind::LET;
	TypeRef type;        ///< `let x:i32` — nullptr si non annoncé
	LetStmt(String n, ExprPtr init, int lineNo, int columnNo, DeclKind kind = DeclKind::LET)
		: Stmt(StmtKind::LET, lineNo, columnNo), name(std::move(n)), initializer(std::move(init)), declKind(kind) {}
};

/// Affectation à une cible assignable : identifiant, `a[i]` ou `a.b`.
/// `op` porte les formes composées (`+=`…), déroulées par l'interpréteur.
struct AssignStmt : Stmt {
	ExprPtr target;
	ExprPtr value;
	Option<BinaryOp> compound = NONE;
	AssignStmt(ExprPtr t, ExprPtr v, Option<BinaryOp> c, int lineNo, int columnNo)
		: Stmt(StmtKind::ASSIGN, lineNo, columnNo), target(std::move(t)), value(std::move(v)), compound(c) {}
};

struct BlockStmt : Stmt {
	std::vector<StmtPtr> statements;
	BlockStmt(int lineNo, int columnNo) : Stmt(StmtKind::BLOCK, lineNo, columnNo) {}
};

struct IfStmt : Stmt {
	ExprPtr condition;
	StmtPtr thenBranch;
	StmtPtr elseBranch; ///< nullptr si absent
	IfStmt(int lineNo, int columnNo) : Stmt(StmtKind::IF, lineNo, columnNo) {}
};

struct WhileStmt : Stmt {
	ExprPtr condition;
	StmtPtr body;
	bool checkAfter = false; ///< `do { … } while (…)` : le corps passe au moins une fois
	WhileStmt(int lineNo, int columnNo) : Stmt(StmtKind::WHILE, lineNo, columnNo) {}
};

/// `for <name> in <iterable> { ... }` — itère une liste, les clés d'une
/// table, ou les caractères d'une chaîne.
struct ForInStmt : Stmt {
	String variable;
	TypeRef type; ///< `for (i:i32 in …)` — nullptr si non annoncé
	ExprPtr iterable;
	StmtPtr body;
	ForInStmt(int lineNo, int columnNo) : Stmt(StmtKind::FOR_IN, lineNo, columnNo) {}
};

struct FunctionStmt : Stmt {
	FunctionDefPtr def;
	FunctionStmt(FunctionDefPtr d, int lineNo, int columnNo)
		: Stmt(StmtKind::FUNCTION, lineNo, columnNo), def(std::move(d)) {}
};

struct ReturnStmt : Stmt {
	ExprPtr value; ///< nullptr => `nil`
	ReturnStmt(ExprPtr v, int lineNo, int columnNo)
		: Stmt(StmtKind::RETURN, lineNo, columnNo), value(std::move(v)) {}
};

struct BreakStmt : Stmt {
	BreakStmt(int lineNo, int columnNo) : Stmt(StmtKind::BREAK, lineNo, columnNo) {}
};

struct ContinueStmt : Stmt {
	ContinueStmt(int lineNo, int columnNo) : Stmt(StmtKind::CONTINUE, lineNo, columnNo) {}
};

// ============================================================================
// Classes et interfaces
// ============================================================================
//
//   interface Forme { fn aire() }
//   abstract class Base implements Forme {
//       let nom = "base"                 # champ d'instance (initialisé à chaque `new`)
//       static let creates = 0           # champ de classe
//       fn init(nom) { this.nom = nom }  # constructeur
//       abstract fn aire()
//   }
//   class Carré extends Base {
//       let côté = 1
//       fn init(c) { super.init("carré"); this.côté = c }
//       override fn aire() { return this.côté * this.côté }
//       factory fn unité() { return new Carré(1) }
//   }
//
// La définition est PARTAGÉE (comme FunctionDef) entre l'AST et la valeur
// `Class` produite à l'exécution : la classe survit au programme qui l'a
// déclarée.

/// Chemin d'un nom de classe : `Forme`, ou `geo.Forme` dans un espace de noms.
using TypePath = std::vector<String>;

[[nodiscard]] String JoinTypePath(const TypePath &path);

/**
 * Type annoncé : nom (ou chemin de classe) + paramètres + `?` :
 *   i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 — nombres (entiers bornés, flottants)
 *   bool, string, any, fn, nil
 *   list<T>, map<V> (clés : chaînes), future<T>
 *   Forme, geo.Forme — une classe ou une interface
 *   T?  — la même chose, ou nil
 */
struct TypeExpr {
	TypePath path;
	std::vector<TypeRef> args;
	bool nullable = false;
	int line = 0;
	int column = 0;

	[[nodiscard]] const String &Name() const noexcept { return path.back(); }
	[[nodiscard]] bool IsSimple(const char *name) const noexcept { return path.size() == 1 && path[0] == name; }
	/// Forme écrite : `list<i32>?`.
	[[nodiscard]] String ToString() const;
};

/// Champ déclaré dans le corps : `let x = 0`, `const n = 3`, `static let c = 0`.
struct FieldDef {
	String name;
	DeclKind declKind = DeclKind::LET;
	ExprPtr initializer; ///< nullptr => `nil`, ou la valeur par défaut du type
	TypeRef type;        ///< `let x:i32` — nullptr si non annoncé
	bool isStatic = false;
	int line = 0;
	int column = 0;
};

/// Méthode : corps (sauf `abstract` et signatures d'interface) + modificateurs.
struct MethodDef {
	FunctionDefPtr def;
	bool isStatic = false;
	bool isAbstract = false; ///< sans corps : à fournir par une classe dérivée
	bool isOverride = false; ///< doit remplacer une méthode héritée (vérifié)
	bool isFactory = false;  ///< constructeur nommé : méthode de classe qui DOIT rendre une instance
};

/// Valeur d'un type énuméré : `ROUGE`, `VERT = 5` (sans valeur : la
/// précédente + 1, en partant de 0).
struct EnumEntry {
	String name;
	ExprPtr value; ///< nullptr : valeur automatique
	int line = 0;
	int column = 0;
};

struct ClassDef {
	String name;
	bool isAbstract = false;
	bool isInterface = false;
	/// `enum Nom { A, B = 5; fn m() { … } }` : une classe dont les SEULES
	/// instances sont ses valeurs (champs constants `name` et `value`).
	bool isEnum = false;
	std::vector<EnumEntry> enumEntries;
	/// `class Boîte<T>` — cf. FunctionDef::typeParams.
	std::vector<TypeParam> typeParams;
	/// `extends A, B, C` — chaque base est résolue à l'exécution de la
	/// déclaration : au plus UNE classe de script, et zéro ou plusieurs bases
	/// fournies par l'hôte (« owners » : `Mesh3D`, `Gameplay`…), cf.
	/// Interpreter::ExecClass et data/script/script_owners.hpp.
	std::vector<TypePath> bases;
	std::vector<TypeRef> superTypeArgs; ///< `extends Boîte<i32>`
	size_t superTypeArgsBase = 0;		///< index, dans `bases`, de la base qui reçoit `superTypeArgs`
	std::vector<TypePath> interfaces; ///< `implements` (classe) ou `extends` (interface)
	std::vector<FieldDef> fields;
	std::vector<MethodDef> methods;
	int line = 0;
	int column = 0;

	ClassDef() = default;
	ClassDef(const ClassDef &) = delete;
	ClassDef &operator=(const ClassDef &) = delete;

	[[nodiscard]] const MethodDef *FindMethod(const String &methodName, bool wantStatic) const noexcept;
};

using ClassDefPtr = std::shared_ptr<ClassDef>;

struct ClassStmt : Stmt {
	ClassDefPtr def;
	ClassStmt(ClassDefPtr d, int lineNo, int columnNo) : Stmt(StmtKind::CLASS, lineNo, columnNo), def(std::move(d)) {}
};

// ============================================================================
// Program
// ============================================================================

/// Une unité compilée : la suite d'instructions de plus haut niveau.
/// `namespace nom { … }` : un espace de noms. Son corps est une portée de
/// FONCTION (un `var` n'en sort pas) ; ses déclarations de premier niveau en
/// sont les MEMBRES, lus du dehors par `nom.membre`. Rouvrir un espace de
/// noms existant l'étend (comme en TypeScript).
struct NamespaceStmt : Stmt {
	String name;
	std::vector<StmtPtr> body;
	NamespaceStmt(String n, int lineNo, int columnNo) : Stmt(StmtKind::NAMESPACE, lineNo, columnNo), name(std::move(n)) {}
};

struct Program {
	std::vector<StmtPtr> statements;
};

} // namespace data::script
