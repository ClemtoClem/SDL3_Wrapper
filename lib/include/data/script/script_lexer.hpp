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
struct ScriptError {
	String message;
	int line = 0;
	int column = 0;

	ScriptError() = default;
	ScriptError(String msg, int lineNo, int columnNo) : message(std::move(msg)), line(lineNo), column(columnNo) {}

	/// "3:17: message" — même forme que les codecs data:: existants.
	[[nodiscard]] String Format() const {
		return String::Format("%d:%d: %s", line, column, message.CStr());
	}
};

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
	KW_NAMESPACE,
	KW_FN,
	KW_IF,
	KW_ELSE,
	KW_WHILE,
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

	// Ponctuation
	LEFT_PAREN,
	RIGHT_PAREN,
	LEFT_BRACE,
	RIGHT_BRACE,
	LEFT_BRACKET,
	RIGHT_BRACKET,
	COMMA,
	COLON,
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
};

struct Token {
	TokenType type = TokenType::END_OF_FILE;
	String text;       ///< lexème brut (identifiant, ou contenu décodé d'une chaîne)
	double number = 0; ///< valeur numérique si type == NUMBER
	int line = 1;
	int column = 1;
};

/// Nom lisible d'un type de token — uniquement pour les messages d'erreur du
/// parseur ("attendu `)` , trouvé `nombre`").
[[nodiscard]] inline const char *TokenTypeName(TokenType t) noexcept {
	switch (t) {
		case TokenType::END_OF_FILE:
			return "fin de fichier";
		case TokenType::NUMBER:
			return "nombre";
		case TokenType::STRING_LITERAL:
			return "chaîne";
		case TokenType::IDENTIFIER:
			return "identifiant";
		case TokenType::KW_CONST:
			return "const";
		case TokenType::KW_VAR:
			return "var";
		case TokenType::KW_NAMESPACE:
			return "namespace";
		case TokenType::KW_LET:
			return "let";
		case TokenType::KW_FN:
			return "fn";
		case TokenType::KW_IF:
			return "if";
		case TokenType::KW_ELSE:
			return "else";
		case TokenType::KW_WHILE:
			return "while";
		case TokenType::KW_FOR:
			return "for";
		case TokenType::KW_IN:
			return "in";
		case TokenType::KW_RETURN:
			return "return";
		case TokenType::KW_BREAK:
			return "break";
		case TokenType::KW_CONTINUE:
			return "continue";
		case TokenType::KW_TRUE:
			return "true";
		case TokenType::KW_FALSE:
			return "false";
		case TokenType::KW_NIL:
			return "nil";
		case TokenType::KW_AND:
			return "and";
		case TokenType::KW_OR:
			return "or";
		case TokenType::KW_NOT:
			return "not";
		case TokenType::LEFT_PAREN:
			return "(";
		case TokenType::RIGHT_PAREN:
			return ")";
		case TokenType::LEFT_BRACE:
			return "{";
		case TokenType::RIGHT_BRACE:
			return "}";
		case TokenType::LEFT_BRACKET:
			return "[";
		case TokenType::RIGHT_BRACKET:
			return "]";
		case TokenType::COMMA:
			return ",";
		case TokenType::COLON:
			return ":";
		case TokenType::SEMICOLON:
			return ";";
		case TokenType::DOT:
			return ".";
		case TokenType::PLUS:
			return "+";
		case TokenType::MINUS:
			return "-";
		case TokenType::STAR:
			return "*";
		case TokenType::SLASH:
			return "/";
		case TokenType::PERCENT:
			return "%";
		case TokenType::ASSIGN:
			return "=";
		case TokenType::PLUS_ASSIGN:
			return "+=";
		case TokenType::MINUS_ASSIGN:
			return "-=";
		case TokenType::STAR_ASSIGN:
			return "*=";
		case TokenType::SLASH_ASSIGN:
			return "/=";
		case TokenType::EQUAL:
			return "==";
		case TokenType::NOT_EQUAL:
			return "!=";
		case TokenType::LESS:
			return "<";
		case TokenType::LESS_EQUAL:
			return "<=";
		case TokenType::GREATER:
			return ">";
		case TokenType::GREATER_EQUAL:
			return ">=";
		case TokenType::CONCAT:
			return "..";
	}
	return "?";
}

// ============================================================================
// Lexer
// ============================================================================

/// Transforme une source en suite de `Token`. Ne lève jamais : toute erreur
/// (chaîne non terminée, caractère inconnu, échappement invalide) remonte
/// comme `Err(ScriptError)` positionnée sur le caractère fautif.
class Lexer {
public:
	explicit Lexer(StringView source) noexcept : m_source(source) {}

	[[nodiscard]] Result<std::vector<Token>, ScriptError> Tokenize() {
		std::vector<Token> tokens;
		for (;;) {
			SkipTriviaResult skipped = SkipTrivia();
			if (skipped.error.IsSome())
				return Err(skipped.error.Unwrap());
			if (AtEnd()) {
				// Le token EOF porte la position de FIN RÉELLE de la source
				// (et non celle du dernier token lu) : c'est elle que le
				// parseur cite quand une construction reste inachevée.
				m_tokenLine = m_line;
				m_tokenColumn = m_column;
				tokens.push_back(Make(TokenType::END_OF_FILE));
				return Ok(std::move(tokens));
			}
			auto tok = NextToken();
			if (tok.IsError())
				return Err(tok.Error());
			tokens.push_back(tok.Unwrap());
		}
	}

private:
	struct SkipTriviaResult {
		Option<ScriptError> error = NONE;
	};

	[[nodiscard]] bool AtEnd() const noexcept { return m_pos >= m_source.GetSize(); }
	[[nodiscard]] char Peek(size_t offset = 0) const noexcept {
		size_t i = m_pos + offset;
		return i < m_source.GetSize() ? m_source.GetData()[i] : '\0';
	}

	char Advance() noexcept {
		char c = Peek();
		++m_pos;
		if (c == '\n') {
			++m_line;
			m_column = 1;
		} else {
			++m_column;
		}
		return c;
	}

	bool Match(char expected) noexcept {
		if (Peek() != expected)
			return false;
		Advance();
		return true;
	}

	[[nodiscard]] Token Make(TokenType type) const {
		Token t;
		t.type = type;
		t.line = m_tokenLine;
		t.column = m_tokenColumn;
		return t;
	}

	/// Espaces, retours à la ligne et commentaires (`#`, `//`, `/* ... */`).
	/// Les retours à la ligne ne produisent PAS de token : les instructions
	/// sont délimitées par la grammaire elle-même (cf. script_parser.hpp),
	/// `;` restant optionnel.
	SkipTriviaResult SkipTrivia() {
		for (;;) {
			char c = Peek();
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
				Advance();
				continue;
			}
			if (c == '#') {
				while (!AtEnd() && Peek() != '\n')
					Advance();
				continue;
			}
			if (c == '/' && Peek(1) == '/') {
				while (!AtEnd() && Peek() != '\n')
					Advance();
				continue;
			}
			if (c == '/' && Peek(1) == '*') {
				int startLine = m_line, startColumn = m_column;
				Advance();
				Advance();
				for (;;) {
					if (AtEnd())
						return {Some(ScriptError(String("commentaire de bloc non terminé"), startLine, startColumn))};
					if (Peek() == '*' && Peek(1) == '/') {
						Advance();
						Advance();
						break;
					}
					Advance();
				}
				continue;
			}
			return {};
		}
	}

	[[nodiscard]] static bool IsDigit(char c) noexcept { return c >= '0' && c <= '9'; }
	[[nodiscard]] static bool IsAlpha(char c) noexcept {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
	}
	[[nodiscard]] static bool IsAlnum(char c) noexcept { return IsAlpha(c) || IsDigit(c); }

	[[nodiscard]] Result<Token, ScriptError> NextToken() {
		m_tokenLine = m_line;
		m_tokenColumn = m_column;

		char c = Peek();
		if (IsDigit(c) || (c == '.' && IsDigit(Peek(1))))
			return LexNumber();
		if (IsAlpha(c))
			return Ok(LexIdentifier());
		if (c == '"' || c == '\'')
			return LexString();

		Advance();
		switch (c) {
			case '(':
				return Ok(Make(TokenType::LEFT_PAREN));
			case ')':
				return Ok(Make(TokenType::RIGHT_PAREN));
			case '{':
				return Ok(Make(TokenType::LEFT_BRACE));
			case '}':
				return Ok(Make(TokenType::RIGHT_BRACE));
			case '[':
				return Ok(Make(TokenType::LEFT_BRACKET));
			case ']':
				return Ok(Make(TokenType::RIGHT_BRACKET));
			case ',':
				return Ok(Make(TokenType::COMMA));
			case ':':
				return Ok(Make(TokenType::COLON));
			case ';':
				return Ok(Make(TokenType::SEMICOLON));
			case '.':
				return Ok(Make(Match('.') ? TokenType::CONCAT : TokenType::DOT));
			case '+':
				return Ok(Make(Match('=') ? TokenType::PLUS_ASSIGN : TokenType::PLUS));
			case '-':
				return Ok(Make(Match('=') ? TokenType::MINUS_ASSIGN : TokenType::MINUS));
			case '*':
				return Ok(Make(Match('=') ? TokenType::STAR_ASSIGN : TokenType::STAR));
			case '/':
				return Ok(Make(Match('=') ? TokenType::SLASH_ASSIGN : TokenType::SLASH));
			case '%':
				return Ok(Make(TokenType::PERCENT));
			case '=':
				return Ok(Make(Match('=') ? TokenType::EQUAL : TokenType::ASSIGN));
			case '<':
				return Ok(Make(Match('=') ? TokenType::LESS_EQUAL : TokenType::LESS));
			case '>':
				return Ok(Make(Match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER));
			case '!':
				if (Match('='))
					return Ok(Make(TokenType::NOT_EQUAL));
				return Err(ScriptError(String("`!` seul n'est pas un opérateur (utilisez `not`)"), m_tokenLine,
									   m_tokenColumn));
			default:
				break;
		}
		return Err(ScriptError(String::Format("caractère inattendu `%c`", c), m_tokenLine, m_tokenColumn));
	}

	[[nodiscard]] Result<Token, ScriptError> LexNumber() {
		size_t start = m_pos;
		while (IsDigit(Peek()))
			Advance();
		if (Peek() == '.' && IsDigit(Peek(1))) {
			Advance();
			while (IsDigit(Peek()))
				Advance();
		}
		if (Peek() == 'e' || Peek() == 'E') {
			size_t save = m_pos;
			int saveLine = m_line, saveColumn = m_column;
			Advance();
			if (Peek() == '+' || Peek() == '-')
				Advance();
			if (IsDigit(Peek())) {
				while (IsDigit(Peek()))
					Advance();
			} else {
				// Pas un exposant (ex: `2eux`) — on rembobine proprement.
				m_pos = save;
				m_line = saveLine;
				m_column = saveColumn;
			}
		}

		String literal(m_source.GetData() + start, m_pos - start);
		Option<double> parsed = literal.TryParseDouble();
		if (parsed.IsNone())
			return Err(ScriptError(String::Format("littéral numérique invalide `%s`", literal.CStr()), m_tokenLine,
								   m_tokenColumn));
		Token t = Make(TokenType::NUMBER);
		t.number = parsed.Unwrap();
		t.text = std::move(literal);
		return Ok(std::move(t));
	}

	[[nodiscard]] Token LexIdentifier() {
		size_t start = m_pos;
		while (IsAlnum(Peek()))
			Advance();
		String word(m_source.GetData() + start, m_pos - start);

		Token t = Make(KeywordType(word.View()));
		t.text = std::move(word);
		return t;
	}

	[[nodiscard]] static TokenType KeywordType(StringView word) noexcept {
		struct Entry {
			const char *name;
			TokenType type;
		};
		static constexpr Entry KEYWORDS[] = {
			{"const", TokenType::KW_CONST},       {"namespace", TokenType::KW_NAMESPACE},
			{"let", TokenType::KW_LET},           {"var", TokenType::KW_VAR},
			{"fn", TokenType::KW_FN},             {"func", TokenType::KW_FN},
			{"if", TokenType::KW_IF},             {"else", TokenType::KW_ELSE},
			{"while", TokenType::KW_WHILE},       {"for", TokenType::KW_FOR},
			{"in", TokenType::KW_IN},             {"return", TokenType::KW_RETURN},
			{"break", TokenType::KW_BREAK},       {"continue", TokenType::KW_CONTINUE},
			{"true", TokenType::KW_TRUE},         {"false", TokenType::KW_FALSE},
			{"nil", TokenType::KW_NIL},           {"null", TokenType::KW_NIL},
			{"and", TokenType::KW_AND},           {"or", TokenType::KW_OR},
			{"not", TokenType::KW_NOT},
		};
		for (const Entry &e : KEYWORDS)
			if (word == StringView(e.name))
				return e.type;
		return TokenType::IDENTIFIER;
	}

	/// Chaîne `"..."` ou `'...'`, avec les échappements usuels. Les
	/// échappements inconnus sont une ERREUR (pas un passage silencieux) :
	/// c'est la seule façon de repérer une faute de frappe dans un script.
	[[nodiscard]] Result<Token, ScriptError> LexString() {
		char quote = Advance();
		String out;
		for (;;) {
			if (AtEnd())
				return Err(ScriptError(String("chaîne non terminée"), m_tokenLine, m_tokenColumn));
			char c = Advance();
			if (c == quote)
				break;
			if (c != '\\') {
				out.PushBack(c);
				continue;
			}
			if (AtEnd())
				return Err(ScriptError(String("chaîne non terminée"), m_tokenLine, m_tokenColumn));
			int escLine = m_line, escColumn = m_column;
			char esc = Advance();
			switch (esc) {
				case 'n':
					out.PushBack('\n');
					break;
				case 't':
					out.PushBack('\t');
					break;
				case 'r':
					out.PushBack('\r');
					break;
				case '0':
					out.PushBack('\0');
					break;
				case '\\':
					out.PushBack('\\');
					break;
				case '"':
					out.PushBack('"');
					break;
				case '\'':
					out.PushBack('\'');
					break;
				default:
					return Err(ScriptError(String::Format("échappement inconnu `\\%c`", esc), escLine, escColumn));
			}
		}
		Token t = Make(TokenType::STRING_LITERAL);
		t.text = std::move(out);
		return Ok(std::move(t));
	}

	StringView m_source;
	size_t m_pos = 0;
	int m_line = 1;
	int m_column = 1;
	int m_tokenLine = 1;
	int m_tokenColumn = 1;
};

} // namespace data::script
