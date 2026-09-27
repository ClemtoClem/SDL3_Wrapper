#pragma once
/**
 * data::script — analyse syntaxique (descente récursive + niveaux de
 * précédence explicites) produisant un `Program` (cf. script_ast.hpp).
 *
 * Précédences, du plus lâche au plus serré (calquées sur Lua, qui place la
 * concaténation entre les comparaisons et l'addition) :
 *
 *   -> <- <-> → or → and → == != → < <= > >= → .. → + - → * / % → unaire → postfixe
 *
 * Les opérateurs de FLUX sont les plus lâches : `source -> filtre -> puits`
 * s'écrit sans parenthèses. `->` et `<->` associent à gauche, `<-` à droite
 * (`a <- b <- c` = `a <- (b <- c)`, comme `c -> b -> a`).
 *
 * Ambiguïté `{` volontairement tranchée comme en Rust/Go : dans la
 * CONDITION d'un `if`/`while` et dans l'itérable d'un `for`, une accolade
 * ouvre toujours le BLOC, jamais une table littérale (`m_allowMapLiteral`).
 * Une table y reste écrivable entre parenthèses : `if ({"a": 1})["a"] { }`.
 */
#include <utility>

#include "script_ast.hpp"

namespace data::script {

class Parser {

  public:
	explicit Parser(std::vector<Token> tokens) : m_tokens(std::move(tokens)) {}

	/// Compile une source complète. Aucune récupération d'erreur : la
	/// PREMIÈRE erreur arrête la compilation et remonte telle quelle —
	/// un script d'éditeur est court, un diagnostic précis vaut mieux
	/// qu'une cascade de diagnostics dérivés.
	[[nodiscard]] static Result<Program, ScriptError> Compile(StringView source);

	[[nodiscard]] Result<Program, ScriptError> ParseProgram();

  private:
	// ── Curseur ──────────────────────────────────────────────────────────────

	[[nodiscard]] const Token& Peek(size_t offset = 0) const noexcept;

	[[nodiscard]] const Token& Previous() const noexcept;

	[[nodiscard]] bool Check(TokenType t) const noexcept;

	const Token& Advance() noexcept;

	bool Match(TokenType t) noexcept;

	[[nodiscard]] ScriptError ErrorAt(const Token& tok, String message) const;

	[[nodiscard]] Result<Token, ScriptError> Consume(TokenType t, const char* what);

	/// `;` et retours à la ligne sont facultatifs : on absorbe simplement un
	/// `;` s'il est là (cf. script_lexer.hpp — aucun token de fin de ligne).
	void SkipOptionalSemicolons();

	// ── Instructions ─────────────────────────────────────────────────────────

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseStatement();

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseBlock();

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseLet(DeclKind kind);

	/// `namespace nom { déclarations }`
	[[nodiscard]] Result<StmtPtr, ScriptError> ParseNamespace();

	// ── Classes et interfaces ────────────────────────────────────────────────

	/// Identifiant, ou mot-clé employé comme NOM de champ (`obj.class`,
	/// `{ new: 1 }`) — sa place le désambiguïse.
	[[nodiscard]] static bool IsWord(TokenType t) noexcept;

	/// `:Type` facultatif après un nom (variable, paramètre, champ, résultat).
	[[nodiscard]] Result<TypeRef, ScriptError> ParseOptionalType();

	/// `i32`, `list<i32>`, `map<string, f64>`, `geo.Forme`, `Forme?`.
	[[nodiscard]] Result<TypeRef, ScriptError> ParseType();

	/// `Nom` ou `espace.Nom`.
	[[nodiscard]] Result<TypePath, ScriptError> ParseTypePath(const char* what);

	/// `(a, b)` seuls : signature d'une méthode `abstract` ou d'interface.
	[[nodiscard]] Result<FunctionDefPtr, ScriptError> ParseSignature(const Token& at, String name);

	/// `<T, U extends Forme>` (le `<` est le prochain token) — paramètres de
	/// type d'une fonction ou d'une classe générique.
	[[nodiscard]] Result<std::vector<TypeParam>, ScriptError> ParseTypeParams();

	/// `<i32, string>` (le `<` est le prochain token) : arguments de type.
	[[nodiscard]] Result<std::vector<TypeRef>, ScriptError> ParseTypeArgs();

	/// Appel générique `nom<T…>(` ? Essai SANS effet : le curseur revient à
	/// sa place si ce n'en est pas un (`a < b` reste une comparaison). Même
	/// règle que C# : `a<b>(c)` est un appel générique.
	[[nodiscard]] Option<std::vector<TypeRef>> TryParseCallTypeArgs();

	/**
	 * `enum Nom [implements I] { A, B = expr, C [; membres] }` : un type
	 * énuméré. Chaque valeur est une instance UNIQUE de la classe `Nom`
	 * (`Nom.A`), avec les champs constants `name` et `value` ; après un `;`,
	 * des méthodes (et champs `static`) comme dans une classe.
	 */
	[[nodiscard]] Result<StmtPtr, ScriptError> ParseEnum();

	/**
	 * `[abstract] class Nom [extends Parent] [implements I1, I2] { membres }`
	 * ou `interface Nom [extends I1, I2] { fn m(a, b) … }`.
	 *
	 * Membres : champs `let`/`var`/`const` (précédés de `static` pour un
	 * champ de classe) et méthodes `fn`, précédées des modificateurs
	 * `static`, `abstract`, `override`, `factory`. Le constructeur est la
	 * méthode `init` (`constructor` en est un synonyme).
	 */
	[[nodiscard]] Result<StmtPtr, ScriptError> ParseClass(bool isAbstract, bool isInterface);

	[[nodiscard]] Option<ScriptError> ParseClassMember(ClassDef& def);

	/**
	 * Symbole après `operator` : `+ - * / % .. == != < <= > >= -> <- <->`,
	 * `[]` (lecture), `[]=` (écriture), `()` (appel), `str` (conversion en
	 * texte) ou `iter` (ce que parcourt `for … in`).
	 */
	[[nodiscard]] Result<String, ScriptError> ParseOperatorName();

	/// Arité d'une surcharge : forme d'INSTANCE (`this` à gauche) ou `static`
	/// (les deux opérandes en paramètres). `-` sans opérande droit est la
	/// négation (renommée `operator neg`).
	[[nodiscard]] static Option<ScriptError> CheckOperatorArity(const Token& at, FunctionDef& def, bool isStatic);

	[[nodiscard]] Result<FunctionDefPtr, ScriptError> ParseFunctionRest(const Token& fnTok, String name,
																		bool isAsync = false);

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseFunctionStatement(bool isAsync = false);

	/// Condition de structure de contrôle : `{` y ouvre toujours le bloc
	/// (cf. en-tête du fichier).
	/// `( condition )` d'un `if`/`while` — parenthèses OBLIGATOIRES.
	[[nodiscard]] Result<ExprPtr, ScriptError> ParseControlCondition(const char* keyword);

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseIf();

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseWhile();

	/// `do { … } while (condition)` — le corps s'exécute au moins une fois.
	[[nodiscard]] Result<StmtPtr, ScriptError> ParseDoWhile();

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseForIn();

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseReturn();

	[[nodiscard]] static Option<BinaryOp> CompoundOpFor(TokenType t) noexcept;

	[[nodiscard]] static bool IsAssignable(const Expr& e) noexcept;

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseExpressionOrAssignment();

	// ── Expressions (précédence croissante) ──────────────────────────────────

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseExpression();

	/// `a -> b -> c`, `a <-> b` (à gauche) ; `a <- b <- c` (à droite).
	[[nodiscard]] Result<ExprPtr, ScriptError> ParseFlow();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseOr();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseAnd();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseEquality();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseComparison();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseConcat();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseAdditive();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseMultiplicative();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseUnary();

	/// Arguments d'un appel, `(` déjà consommée.
	[[nodiscard]] Option<ScriptError> ParseCallArguments(CallExpr& call);

	/// `new Classe(args)` / `new espace.Classe(args)`.
	[[nodiscard]] Result<ExprPtr, ScriptError> ParseNew();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParsePostfix();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParsePrimary();

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseListLiteral();

	/// `{ "a": 1, b: 2 }` — une clé identifiant nu vaut la chaîne du même nom
	/// (comme en JavaScript/YAML), ce qui rend les tables de configuration
	/// bien plus lisibles dans un script d'éditeur.
	[[nodiscard]] Result<ExprPtr, ScriptError> ParseMapLiteral();

	std::vector<Token> m_tokens;
	size_t m_pos = 0;
	bool m_allowMapLiteral = true;
	/// `this` / `super` licites ici (méthode d'instance, initialiseur de
	/// champ). Une fonction imbriquée en hérite : sa fermeture capture `this`.
	bool m_thisAllowed = false;
	bool m_superAllowed = false;
	/// `await` licite ici : corps d'une fonction `async`, ou niveau principal.
	bool m_awaitAllowed = true;

	struct AwaitContext {

		Parser& parser;
		bool saved;
		AwaitContext(Parser& p, bool allowed);

		~AwaitContext();

		AwaitContext(const AwaitContext&) = delete;
		AwaitContext& operator=(const AwaitContext&) = delete;
	};

	/// Contexte d'un membre de classe, rétabli à la sortie (RAII).
	struct MemberContext {

		Parser& parser;
		bool savedThis, savedSuper;
		MemberContext(Parser& p, bool allowThis, bool allowSuper);

		~MemberContext();

		MemberContext(const MemberContext&) = delete;
		MemberContext& operator=(const MemberContext&) = delete;
	};

	// ── Portées statiques ────────────────────────────────────────────────────
	// Suivies PENDANT l'analyse syntaxique pour signaler, ligne à l'appui et
	// avant toute exécution, ce que les règles de portée interdisent :
	// redéclarer un `let`/`const` dans sa portée, réaffecter une constante,
	// un `var` qui heurterait un `let` de la même fonction. L'interpréteur
	// refait ces contrôles à l'exécution (valeurs définies par l'hôte).

	enum class StaticDecl : uint8_t { VAR, LET, CONST, FUNCTION, PARAM, NAMESPACE };
	struct StaticBinding {

		String name;
		StaticDecl kind;
		int line = 0;
	};

	struct StaticScope {

		bool function = false;
		std::vector<StaticBinding> names;
	};

	std::vector<StaticScope> m_scopes;

	/// Portée ouverte pour la durée d'un bloc C++ (RAII : refermée sur tous
	/// les chemins de retour, erreurs comprises).
	struct ScopeGuard {

		Parser& parser;
		ScopeGuard(Parser& p, bool function);

		~ScopeGuard();

		ScopeGuard(const ScopeGuard&) = delete;
		ScopeGuard& operator=(const ScopeGuard&) = delete;
	};

	void PushScope(bool function);

	void PopScope();

	[[nodiscard]] static StaticBinding* FindIn(StaticScope& scope, const String& name);

	[[nodiscard]] const StaticBinding* Resolve(const String& name);

	[[nodiscard]] static bool IsLexical(StaticDecl kind) noexcept;

	/// Enregistre une déclaration et rend l'erreur si les règles l'interdisent.
	[[nodiscard]] Option<ScriptError> Declare(const Token& at, StaticDecl kind);
};

} // namespace data::script
