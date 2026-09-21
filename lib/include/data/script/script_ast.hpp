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

/// Paramètres + corps d'une fonction, partagés entre l'AST et la valeur
/// `Function` produite à l'exécution (cf. en-tête du fichier).
struct FunctionDef {
	String name; ///< vide pour une lambda
	std::vector<String> params;
	std::vector<StmtPtr> body;
	int line = 0;
	int column = 0;

	FunctionDef() = default;
	FunctionDef(const FunctionDef &) = delete;
	FunctionDef &operator=(const FunctionDef &) = delete;
	~FunctionDef();
};

using FunctionDefPtr = std::shared_ptr<FunctionDef>;

struct NumberExpr : Expr {
	double value = 0;
	NumberExpr(double v, int lineNo, int columnNo) : Expr(ExprKind::NUMBER, lineNo, columnNo), value(v) {}
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
	RETURN,
	BREAK,
	CONTINUE,
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

struct LetStmt : Stmt {
	String name;
	ExprPtr initializer; ///< nullptr => `nil`
	LetStmt(String n, ExprPtr init, int lineNo, int columnNo)
		: Stmt(StmtKind::LET, lineNo, columnNo), name(std::move(n)), initializer(std::move(init)) {}
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
	WhileStmt(int lineNo, int columnNo) : Stmt(StmtKind::WHILE, lineNo, columnNo) {}
};

/// `for <name> in <iterable> { ... }` — itère une liste, les clés d'une
/// table, ou les caractères d'une chaîne.
struct ForInStmt : Stmt {
	String variable;
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
// Program
// ============================================================================

/// Une unité compilée : la suite d'instructions de plus haut niveau.
struct Program {
	std::vector<StmtPtr> statements;
};

} // namespace data::script
