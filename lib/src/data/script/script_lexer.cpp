// Définitions de data/script/script_lexer.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/script/script_lexer.hpp"

namespace data::script {

#if defined(__GNUC__) && !defined(__clang__)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

// ── ScriptError ──────────────────────────────────────────────────────────────

String ScriptError::Format() const {
	return String::Format("%d:%d: %s", line, column, message.CStr());
}

const char * TokenTypeName(TokenType t) noexcept {
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
		case TokenType::KW_STATIC:
			return "static";
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
		case TokenType::KW_DO:
			return "do";
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
		case TokenType::KW_CLASS:
			return "class";
		case TokenType::KW_INTERFACE:
			return "interface";
		case TokenType::KW_ABSTRACT:
			return "abstract";
		case TokenType::KW_FACTORY:
			return "factory";
		case TokenType::KW_EXTENDS:
			return "extends";
		case TokenType::KW_IMPLEMENTS:
			return "implements";
		case TokenType::KW_OVERRIDE:
			return "override";
		case TokenType::KW_NEW:
			return "new";
		case TokenType::KW_THIS:
			return "this";
		case TokenType::KW_SUPER:
			return "super";
		case TokenType::KW_IS:
			return "is";
		case TokenType::KW_AS:
			return "as";
		case TokenType::KW_ASYNC:
			return "async";
		case TokenType::KW_AWAIT:
			return "await";
		case TokenType::KW_ENUM:
			return "enum";
		case TokenType::KW_OPERATOR:
			return "operator";
		case TokenType::ARROW_RIGHT:
			return "->";
		case TokenType::ARROW_LEFT:
			return "<-";
		case TokenType::ARROW_BOTH:
			return "<->";
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
		case TokenType::QUESTION:
			return "?";
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

// ── Lexer ────────────────────────────────────────────────────────────────────

Result<std::vector<Token>, ScriptError> Lexer::Tokenize() {
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

char Lexer::Peek(size_t offset) const noexcept {
	size_t i = m_pos + offset;
	return i < m_source.GetSize() ? m_source.GetData()[i] : '\0';
}

char Lexer::Advance() noexcept {
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

bool Lexer::Match(char expected) noexcept {
	if (Peek() != expected)
		return false;
	Advance();
	return true;
}

Token Lexer::Make(TokenType type) const {
	Token t;
	t.type = type;
	t.line = m_tokenLine;
	t.column = m_tokenColumn;
	return t;
}

Lexer::SkipTriviaResult Lexer::SkipTrivia() {
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

bool Lexer::IsAlpha(char c) noexcept {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

Result<Token, ScriptError> Lexer::NextToken() {
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
		case '?':
			return Ok(Make(TokenType::QUESTION));
		case ';':
			return Ok(Make(TokenType::SEMICOLON));
		case '.':
			return Ok(Make(Match('.') ? TokenType::CONCAT : TokenType::DOT));
		case '+':
			return Ok(Make(Match('=') ? TokenType::PLUS_ASSIGN : TokenType::PLUS));
		case '-':
			if (Match('>'))
				return Ok(Make(TokenType::ARROW_RIGHT));
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
			// `<-` et `<->` sont des opérateurs de flux : une comparaison à
			// un nombre négatif s'écrit avec une espace (`x < -1`).
			if (Match('-'))
				return Ok(Make(Match('>') ? TokenType::ARROW_BOTH : TokenType::ARROW_LEFT));
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

Result<Token, ScriptError> Lexer::LexNumber() {
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
	bool integral = true;
	uint64_t exact = 0;
	for (size_t i = 0; i < literal.GetSize() && integral; ++i) {
		const char digit = literal.CStr()[i];
		if (!IsDigit(digit))
			integral = false;
		else if (exact > (UINT64_MAX - uint64_t(digit - '0')) / 10)
			return Err(ScriptError(String::Format("littéral entier trop grand `%s` (u64 au plus)", literal.CStr()),
								   m_tokenLine, m_tokenColumn));
		else
			exact = exact * 10 + uint64_t(digit - '0');
	}
	t.isInteger = integral;
	t.integer = exact;
	t.text = std::move(literal);
	return Ok(std::move(t));
}

Token Lexer::LexIdentifier() {
	size_t start = m_pos;
	while (IsAlnum(Peek()))
		Advance();
	String word(m_source.GetData() + start, m_pos - start);

	Token t = Make(KeywordType(word.View()));
	t.text = std::move(word);
	return t;
}

TokenType Lexer::KeywordType(StringView word) noexcept {
	struct Entry {
		const char *name;
		TokenType type;
	};
	static constexpr Entry KEYWORDS[] = {
		{"const", TokenType::KW_CONST},       {"let", TokenType::KW_LET},
		{"var", TokenType::KW_VAR},           {"static", TokenType::KW_STATIC},

		{"namespace", TokenType::KW_NAMESPACE},
		{"fn", TokenType::KW_FN},             {"func", TokenType::KW_FN},
		{"if", TokenType::KW_IF},             {"else", TokenType::KW_ELSE},
		{"while", TokenType::KW_WHILE},       {"do", TokenType::KW_DO},       {"for", TokenType::KW_FOR},
		{"in", TokenType::KW_IN},             {"return", TokenType::KW_RETURN},
		{"break", TokenType::KW_BREAK},       {"continue", TokenType::KW_CONTINUE},
		{"true", TokenType::KW_TRUE},         {"false", TokenType::KW_FALSE},
		{"nil", TokenType::KW_NIL},           {"null", TokenType::KW_NIL},
		{"and", TokenType::KW_AND},           {"or", TokenType::KW_OR},
		{"not", TokenType::KW_NOT},

		{"class", TokenType::KW_CLASS},       {"interface", TokenType::KW_INTERFACE},
		{"abstract", TokenType::KW_ABSTRACT}, {"factory", TokenType::KW_FACTORY},
		{"extends", TokenType::KW_EXTENDS},   {"implements", TokenType::KW_IMPLEMENTS},
		{"override", TokenType::KW_OVERRIDE}, {"new", TokenType::KW_NEW},
		{"this", TokenType::KW_THIS},         {"super", TokenType::KW_SUPER},
		{"is", TokenType::KW_IS},             {"as", TokenType::KW_AS},
		{"async", TokenType::KW_ASYNC},       {"await", TokenType::KW_AWAIT},
		{"enum", TokenType::KW_ENUM},         {"operator", TokenType::KW_OPERATOR},
	};

	for (const Entry &e : KEYWORDS) {
		if (word == StringView(e.name))
			return e.type;
	}
	return TokenType::IDENTIFIER;
}

Result<Token, ScriptError> Lexer::LexString() {
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
#pragma GCC diagnostic pop
#endif

} // namespace data::script
