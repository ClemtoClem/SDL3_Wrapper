#pragma once
/**
 * data::script — analyse lexicale du langage de script embarqué ("Sled").
 *
 * Le module `data::` savait déjà transformer du TEXTE en arbre `data::Node`
 * pour sept formats de DONNÉES (JSON/XML/YAML/INI/TOML/CSV/CSS) ; ce
 * sous-module étend la même idée à un langage de PROGRAMMATION : même
 * discipline d'erreurs (aucune exception — cf. l'en-tête de data.hpp), mêmes
 * types de chaînes (`String`/`StringView`), et un pont explicite vers
 * `data::Node` (cf. script_value.hpp) pour que n'importe quel document
 * JSON/YAML déjà chargé soit directement manipulable depuis un script.
 *
 * Syntaxe volontairement à ACCOLADES (ni indentation significative façon
 * Python, ni `end` façon Lua) : un lexer sans état de pile d'indentation
 * reste trivialement réentrant et testable ligne à ligne, ce qui est le seul
 * point qui compte ici — la « saveur » Python/Lua vient du reste (typage
 * dynamique, `fn`, `for x in ...`, listes/tables littérales, `and`/`or`/`not`).
 *
 * @code
 * # commentaire (ou // ... ou des blocs)
 * let v = 0.0
 * fn step(dt) {
 *     v = v + 9.81 * dt
 *     if v > 10 { v = 10 }
 *     return v
 * }
 * @endcode
 */
#include <cstdint>
#include <vector>

#include "../../core/core.hpp"

namespace data::script {

// ============================================================================
// ScriptError — le SEUL canal d'erreur du module (aucune exception, jamais)
// ============================================================================

/// Erreur de compilation (lexeur/parseur) ou d'exécution (interpréteur),
/// toujours porteuse de sa position source pour un message exploitable.
///
/// Faux positif de GCC en -O3 : dans un `Option<Result<Value, ScriptError>>`
/// construit sur la branche Ok, il croit lire `line`/`column` non
/// initialisés en déplaçant la variante Err (qui n'est jamais active). Les
/// champs SONT initialisés (ci-dessous) ; l'avertissement, rattaché aux
/// constructeurs implicites de ce type, est donc coupé ici seulement.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
struct ScriptError {
	String message;
	int line = 0;
	int column = 0;

	ScriptError() = default;
	ScriptError(String msg, int lineNo, int columnNo) : message(std::move(msg)), line(lineNo), column(columnNo) {}

	/// "3:17: message" — même forme que les codecs data:: existants.
	[[nodiscard]] String Format() const;
};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// ============================================================================
// Token
// ============================================================================

enum class TokenType : uint8_t {
	END_OF_FILE,

	// Littéraux
	NUMBER,
	STRING_LITERAL,
	IDENTIFIER,

	// Mots-clés (key word)
	KW_LET,
	KW_VAR,
	KW_CONST,
	KW_STATIC,

	KW_NAMESPACE,
	KW_FN,
	KW_IF,
	KW_ELSE,
	KW_WHILE,
	KW_DO,
	KW_FOR,
	KW_IN,
	KW_RETURN,
	KW_BREAK,
	KW_CONTINUE,
	KW_TRUE,
	KW_FALSE,
	KW_NIL,
	KW_AND,
	KW_OR,
	KW_NOT,

	// OOP Keywords
	KW_CLASS,
	KW_INTERFACE,
	KW_ABSTRACT,
	KW_FACTORY,
	KW_EXTENDS,
	KW_IMPLEMENTS,
	KW_OVERRIDE,
	KW_NEW,
	KW_THIS,
	KW_SUPER,
	KW_IS,
	KW_AS,

	// Async keywords
	KW_ASYNC,
	KW_AWAIT,

	// Types énumérés, surcharge d'opérateurs
	KW_ENUM,
	KW_OPERATOR,

	// Ponctuation
	LEFT_PAREN,
	RIGHT_PAREN,
	LEFT_BRACE,
	RIGHT_BRACE,
	LEFT_BRACKET,
	RIGHT_BRACKET,
	COMMA,
	COLON,
	QUESTION, ///< `?` — type pouvant valoir nil (`i32?`)
	SEMICOLON,
	DOT,

	// Opérateurs
	PLUS,
	MINUS,
	STAR,
	SLASH,
	PERCENT,
	ASSIGN,
	PLUS_ASSIGN,
	MINUS_ASSIGN,
	STAR_ASSIGN,
	SLASH_ASSIGN,
	EQUAL,
	NOT_EQUAL,
	LESS,
	LESS_EQUAL,
	GREATER,
	GREATER_EQUAL,
	CONCAT, ///< `..` — concaténation de chaînes explicite (emprunt à Lua)

	// Opérateurs de flux (interconnexion de pipes, chaînes d'appels)
	ARROW_RIGHT, ///< `->` — `a -> b` : a s'écoule dans b
	ARROW_LEFT,  ///< `<-` — `a <- b` : b s'écoule dans a (écrire `x < -1` pour comparer à un négatif)
	ARROW_BOTH,  ///< `<->` — liaison dans les deux sens
};

struct Token {
	TokenType type = TokenType::END_OF_FILE;
	String text;       ///< lexème brut (identifiant, ou contenu décodé d'une chaîne)
	double number = 0; ///< valeur numérique si type == NUMBER
	/// NUMBER sans virgule ni exposant : un entier, valeur exacte dans
	/// `integer` (jusqu'à UINT64_MAX).
	bool isInteger = false;
	uint64_t integer = 0;
	int line = 1;
	int column = 1;
};

/// Nom lisible d'un type de token — uniquement pour les messages d'erreur du
/// parseur ("attendu `)` , trouvé `nombre`").
[[nodiscard]] const char *TokenTypeName(TokenType t) noexcept;

// ============================================================================
// Lexer
// ============================================================================

/// Transforme une source en suite de `Token`. Ne lève jamais : toute erreur
/// (chaîne non terminée, caractère inconnu, échappement invalide) remonte
/// comme `Err(ScriptError)` positionnée sur le caractère fautif.
class Lexer {
public:
	explicit Lexer(StringView source) noexcept : m_source(source) {}

	[[nodiscard]] Result<std::vector<Token>, ScriptError> Tokenize();

private:
	struct SkipTriviaResult {
		Option<ScriptError> error = NONE;
	};

	[[nodiscard]] bool AtEnd() const noexcept { return m_pos >= m_source.GetSize(); }
	[[nodiscard]] char Peek(size_t offset = 0) const noexcept;

	char Advance() noexcept;

	bool Match(char expected) noexcept;

	[[nodiscard]] Token Make(TokenType type) const;

	/// Espaces, retours à la ligne et commentaires (`#`, `//`, `/* ... */`).
	/// Les retours à la ligne ne produisent PAS de token : les instructions
	/// sont délimitées par la grammaire elle-même (cf. script_parser.hpp),
	/// `;` restant optionnel.
	SkipTriviaResult SkipTrivia();

	[[nodiscard]] static bool IsDigit(char c) noexcept { return c >= '0' && c <= '9'; }
	/// Lettre d'identifiant : ASCII, `_`, et tout octet d'un caractère UTF-8
	/// non ASCII (≥ 0x80) — `départ`, `créés` sont des noms valides, comme
	/// en Python.
	[[nodiscard]] static bool IsAlpha(char c) noexcept;
	[[nodiscard]] static bool IsAlnum(char c) noexcept { return IsAlpha(c) || IsDigit(c); }

	[[nodiscard]] Result<Token, ScriptError> NextToken();

	[[nodiscard]] Result<Token, ScriptError> LexNumber();

	[[nodiscard]] Token LexIdentifier();

	[[nodiscard]] static TokenType KeywordType(StringView word) noexcept;

	/// Chaîne `"..."` ou `'...'`, avec les échappements usuels. Les
	/// échappements inconnus sont une ERREUR (pas un passage silencieux) :
	/// c'est la seule façon de repérer une faute de frappe dans un script.
	[[nodiscard]] Result<Token, ScriptError> LexString();

	StringView m_source;
	size_t m_pos = 0;
	int m_line = 1;
	int m_column = 1;
	int m_tokenLine = 1;
	int m_tokenColumn = 1;
};

} // namespace data::script
