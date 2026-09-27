// Définitions de sql/token.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sql/token.hpp"

namespace sql {

// ── Tokenizer ────────────────────────────────────────────────────────────────

Result<std::vector<Token>, String> Tokenizer::Tokenize(StringView sql) {
	std::vector<Token> tokens;
	size_t i = 0;
	size_t size = sql.GetSize();

	while (i < size) {
		char c = sql[i];

		if (IsSpace(c)) {
			++i;
			continue;
		}

		if (c == '\'') {
			auto lit = ScanStringLiteral(sql, i);
			if (lit.IsError())
				return Err(lit.Error());
			tokens.push_back(Token{TokenType::STRING_LITERAL, lit.Value()});
			continue;
		}

		if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
			(c == '-' && i + 1 < size && std::isdigit(static_cast<unsigned char>(sql[i + 1])) != 0)) {
			tokens.push_back(ScanNumber(sql, i));
			continue;
		}

		if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
			tokens.push_back(ScanWord(sql, i));
			continue;
		}

		auto op = ScanOperator(sql, i);
		if (op.IsSome()) {
			tokens.push_back(Token{TokenType::OPERATOR, *op});
			continue;
		}

		if (c == '(' || c == ')' || c == ',' || c == ';') {
			tokens.push_back(Token{TokenType::PUNCTUATION, String(c)});
			++i;
			continue;
		}

		return Err(String::Format("sql: unrecognized character '%c' at position %zu", c, i));
	}

	tokens.push_back(Token{TokenType::END, String()});
	return Ok(std::move(tokens));
}

bool Tokenizer::IsKeywordText(const String &upper) noexcept {
	static const std::array<const char *, 17> KEYWORDS = {"CREATE", "TABLE", "INSERT", "INTO", "VALUES",
															"SELECT", "FROM",  "WHERE",  "DELETE", "DROP",
															"AND",    "OR",    "NOT",    "ORDER", "BY",
															"ASC",    "DESC"};
	for (auto *kw : KEYWORDS)
		if (upper == kw)
			return true;
	return false;
}

Result<String, String> Tokenizer::ScanStringLiteral(StringView sql, size_t &i) {
	size_t size = sql.GetSize();
	++i; // skip opening quote
	String text;
	while (i < size) {
		char c = sql[i];
		if (c == '\'') {
			if (i + 1 < size && sql[i + 1] == '\'') {
				text += '\'';
				i += 2;
				continue;
			}
			++i; // skip closing quote
			return Ok(text);
		}
		text += c;
		++i;
	}
	return Err(String("sql: unterminated string literal"));
}

Token Tokenizer::ScanNumber(StringView sql, size_t &i) {
	size_t size = sql.GetSize();
	size_t start = i;
	if (sql[i] == '-')
		++i;
	while (i < size && std::isdigit(static_cast<unsigned char>(sql[i])) != 0)
		++i;
	bool isFloat = false;
	if (i < size && sql[i] == '.') {
		isFloat = true;
		++i;
		while (i < size && std::isdigit(static_cast<unsigned char>(sql[i])) != 0)
			++i;
	}
	String text(sql.GetData() + start, i - start);
	return Token{isFloat ? TokenType::FLOAT_LITERAL : TokenType::INT_LITERAL, text};
}

Token Tokenizer::ScanWord(StringView sql, size_t &i) {
	size_t size = sql.GetSize();
	size_t start = i;
	while (i < size && (std::isalnum(static_cast<unsigned char>(sql[i])) != 0 || sql[i] == '_'))
		++i;
	String text(sql.GetData() + start, i - start);
	String upper = text.ToUpper();
	if (IsKeywordText(upper))
		return Token{TokenType::KEYWORD, text};
	return Token{TokenType::IDENTIFIER, text};
}

Option<String> Tokenizer::ScanOperator(StringView sql, size_t &i) {
	size_t size = sql.GetSize();
	char c = sql[i];
	if (i + 1 < size) {
		char c2 = sql[i + 1];
		if ((c == '<' && c2 == '>') || (c == '!' && c2 == '=') || (c == '<' && c2 == '=') ||
			(c == '>' && c2 == '=')) {
			String op(sql.GetData() + i, size_t(2));
			i += 2;
			return Some(op);
		}
	}
	if (c == '=' || c == '<' || c == '>' || c == '*') {
		String op(sql.GetData() + i, size_t(1));
		++i;
		return Some(op);
	}
	return NONE;
}

} // namespace sql
