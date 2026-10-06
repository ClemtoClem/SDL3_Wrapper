// Définitions de sql/parser.hpp
#include "sql/parser.hpp"

namespace sql {

namespace detail {

String TokenTextOrEnd(const std::vector<Token> &tokens, size_t pos) {
	return pos < tokens.size() ? tokens[pos].text : String("<end of input>");
}

Result<size_t, String> ExpectKeyword(const std::vector<Token> &tokens, size_t pos, const char *keyword) {
	if (pos >= tokens.size() || tokens[pos].type != TokenType::KEYWORD || tokens[pos].text.ToUpper() != keyword)
		return Err(String("sql: expected '") + keyword + "', got '" + TokenTextOrEnd(tokens, pos) + "'");
	return Ok(pos + 1);
}

Result<size_t, String> ExpectPunctuation(const std::vector<Token> &tokens, size_t pos, const char *punct) {
	if (pos >= tokens.size() || tokens[pos].type != TokenType::PUNCTUATION || tokens[pos].text != punct)
		return Err(String("sql: expected '") + punct + "', got '" + TokenTextOrEnd(tokens, pos) + "'");
	return Ok(pos + 1);
}

Result<String, String> ExpectIdentifier(const std::vector<Token> &tokens, size_t pos, const char *what) {
	if (pos >= tokens.size() || tokens[pos].type != TokenType::IDENTIFIER)
		return Err(String("sql: expected ") + what + ", got '" + TokenTextOrEnd(tokens, pos) + "'");
	return Ok(tokens[pos].text);
}

bool IsPunct(const Token &t, const char *punct) noexcept {
	return t.type == TokenType::PUNCTUATION && t.text == punct;
}

bool IsKeyword(const Token &t, const char *keyword) noexcept {
	return t.type == TokenType::KEYWORD && t.text.ToUpper() == keyword;
}

Option<data::NodeType> ParseColumnType(const String &text) {
	String up = text.ToUpper();
	if (up == "INT" || up == "INTEGER")
		return Some(data::NodeType::INT);
	if (up == "FLOAT" || up == "REAL" || up == "DOUBLE")
		return Some(data::NodeType::FLOAT);
	if (up == "STRING" || up == "TEXT" || up == "VARCHAR")
		return Some(data::NodeType::STRING);
	if (up == "BOOL" || up == "BOOLEAN")
		return Some(data::NodeType::BOOL);
	return NONE;
}

Result<data::NodePtr, String> ParseValueLiteral(const std::vector<Token> &tokens, size_t pos) {
	if (pos >= tokens.size())
		return Err(String("sql: expected a value, got '<end of input>'"));
	const Token &tok = tokens[pos];
	switch (tok.type) {
		case TokenType::STRING_LITERAL:
			return Ok(data::Node::MakeString(tok.text));
		case TokenType::INT_LITERAL: {
			auto v = tok.text.TryParseInt();
			if (v.IsNone())
				return Err(String("sql: invalid integer literal '") + tok.text + "'");
			return Ok(data::Node::MakeInt(*v));
		}
		case TokenType::FLOAT_LITERAL: {
			auto v = tok.text.TryParseDouble();
			if (v.IsNone())
				return Err(String("sql: invalid float literal '") + tok.text + "'");
			return Ok(data::Node::MakeFloat(*v));
		}
		case TokenType::IDENTIFIER: {
			String up = tok.text.ToUpper();
			if (up == "TRUE")
				return Ok(data::Node::MakeBool(true));
			if (up == "FALSE")
				return Ok(data::Node::MakeBool(false));
			return Err(String("sql: expected a literal value, got identifier '") + tok.text + "'");
		}
		default:
			return Err(String("sql: expected a literal value, got '") + tok.text + "'");
	}
}

Result<size_t, String> ExpectStatementEnd(const std::vector<Token> &tokens, size_t pos) {
	if (pos < tokens.size() && IsPunct(tokens[pos], ";"))
		++pos;
	if (pos < tokens.size() && tokens[pos].type != TokenType::END) {
		if (IsKeyword(tokens[pos], "ORDER"))
			return Err(String("sql: ORDER BY is not yet supported"));
		return Err(String("sql: unexpected trailing input '") + tokens[pos].text + "'");
	}
	return Ok(pos);
}

Result<Statement, String> ParseCreateTable(const std::vector<Token> &tokens, size_t pos) {
	auto r1 = ExpectKeyword(tokens, pos, "CREATE");
	if (r1.IsError())
		return Err(r1.Error());
	auto r2 = ExpectKeyword(tokens, r1.Value(), "TABLE");
	if (r2.IsError())
		return Err(r2.Error());
	pos = r2.Value();

	auto nameR = ExpectIdentifier(tokens, pos, "a table name");
	if (nameR.IsError())
		return Err(nameR.Error());
	String tableName = nameR.Value();
	++pos;

	auto openR = ExpectPunctuation(tokens, pos, "(");
	if (openR.IsError())
		return Err(openR.Error());
	pos = openR.Value();

	Schema schema;
	bool first = true;
	while (true) {
		if (pos < tokens.size() && IsPunct(tokens[pos], ")")) {
			++pos;
			break;
		}
		if (!first) {
			auto commaR = ExpectPunctuation(tokens, pos, ",");
			if (commaR.IsError())
				return Err(commaR.Error());
			pos = commaR.Value();
		}
		auto colNameR = ExpectIdentifier(tokens, pos, "a column name");
		if (colNameR.IsError())
			return Err(colNameR.Error());
		String colName = colNameR.Value();
		++pos;

		auto typeNameR = ExpectIdentifier(tokens, pos, "a column type");
		if (typeNameR.IsError())
			return Err(String("sql: expected a column type for column '") + colName + "', got '" +
						TokenTextOrEnd(tokens, pos) + "'");
		auto colType = ParseColumnType(typeNameR.Value());
		if (colType.IsNone())
			return Err(String("sql: unknown column type '") + typeNameR.Value() + "' for column '" + colName + "'");
		++pos;

		schema.columns.push_back(ColumnDef{colName, *colType});
		first = false;
	}

	if (schema.columns.empty())
		return Err(String("sql: CREATE TABLE requires at least one column"));

	auto endR = ExpectStatementEnd(tokens, pos);
	if (endR.IsError())
		return Err(endR.Error());

	return Ok(Statement(CreateTableStatement{tableName, schema}));
}

Result<Statement, String> ParseInsert(const std::vector<Token> &tokens, size_t pos) {
	auto r1 = ExpectKeyword(tokens, pos, "INSERT");
	if (r1.IsError())
		return Err(r1.Error());
	auto r2 = ExpectKeyword(tokens, r1.Value(), "INTO");
	if (r2.IsError())
		return Err(r2.Error());
	pos = r2.Value();

	auto nameR = ExpectIdentifier(tokens, pos, "a table name");
	if (nameR.IsError())
		return Err(nameR.Error());
	String tableName = nameR.Value();
	++pos;

	auto valuesKw = ExpectKeyword(tokens, pos, "VALUES");
	if (valuesKw.IsError())
		return Err(valuesKw.Error());
	pos = valuesKw.Value();

	auto openR = ExpectPunctuation(tokens, pos, "(");
	if (openR.IsError())
		return Err(openR.Error());
	pos = openR.Value();

	std::vector<data::NodePtr> values;
	bool first = true;
	while (true) {
		if (pos < tokens.size() && IsPunct(tokens[pos], ")")) {
			++pos;
			break;
		}
		if (!first) {
			auto commaR = ExpectPunctuation(tokens, pos, ",");
			if (commaR.IsError())
				return Err(commaR.Error());
			pos = commaR.Value();
		}
		auto valueR = ParseValueLiteral(tokens, pos);
		if (valueR.IsError())
			return Err(valueR.Error());
		values.push_back(valueR.Value());
		++pos;
		first = false;
	}

	if (values.empty())
		return Err(String("sql: INSERT INTO requires at least one value"));

	auto endR = ExpectStatementEnd(tokens, pos);
	if (endR.IsError())
		return Err(endR.Error());

	return Ok(Statement(InsertStatement{tableName, values}));
}

Result<Statement, String> ParseSelect(const std::vector<Token> &tokens, size_t pos) {
	auto r1 = ExpectKeyword(tokens, pos, "SELECT");
	if (r1.IsError())
		return Err(r1.Error());
	pos = r1.Value();

	std::vector<String> columns;
	if (pos < tokens.size() && tokens[pos].type == TokenType::OPERATOR && tokens[pos].text == "*") {
		++pos;
	} else {
		bool first = true;
		while (true) {
			if (!first) {
				if (pos < tokens.size() && IsPunct(tokens[pos], ",")) {
					++pos;
				} else {
					break;
				}
			}
			auto colR = ExpectIdentifier(tokens, pos, "a column name or '*'");
			if (colR.IsError())
				return Err(colR.Error());
			columns.push_back(colR.Value());
			++pos;
			first = false;
		}
	}

	auto fromR = ExpectKeyword(tokens, pos, "FROM");
	if (fromR.IsError())
		return Err(fromR.Error());
	pos = fromR.Value();

	auto nameR = ExpectIdentifier(tokens, pos, "a table name");
	if (nameR.IsError())
		return Err(nameR.Error());
	String tableName = nameR.Value();
	++pos;

	Option<Expression> where = NONE;
	if (pos < tokens.size() && IsKeyword(tokens[pos], "WHERE")) {
		++pos;
		auto exprR = ParseExpression(tokens, pos);
		if (exprR.IsError())
			return Err(exprR.Error());
		where = Some(exprR.Value());
	}

	auto endR = ExpectStatementEnd(tokens, pos);
	if (endR.IsError())
		return Err(endR.Error());

	return Ok(Statement(SelectStatement{tableName, columns, where}));
}

Result<Statement, String> ParseDelete(const std::vector<Token> &tokens, size_t pos) {
	auto r1 = ExpectKeyword(tokens, pos, "DELETE");
	if (r1.IsError())
		return Err(r1.Error());
	auto r2 = ExpectKeyword(tokens, r1.Value(), "FROM");
	if (r2.IsError())
		return Err(r2.Error());
	pos = r2.Value();

	auto nameR = ExpectIdentifier(tokens, pos, "a table name");
	if (nameR.IsError())
		return Err(nameR.Error());
	String tableName = nameR.Value();
	++pos;

	Option<Expression> where = NONE;
	if (pos < tokens.size() && IsKeyword(tokens[pos], "WHERE")) {
		++pos;
		auto exprR = ParseExpression(tokens, pos);
		if (exprR.IsError())
			return Err(exprR.Error());
		where = Some(exprR.Value());
	}

	auto endR = ExpectStatementEnd(tokens, pos);
	if (endR.IsError())
		return Err(endR.Error());

	return Ok(Statement(DeleteStatement{tableName, where}));
}

Result<Statement, String> ParseDropTable(const std::vector<Token> &tokens, size_t pos) {
	auto r1 = ExpectKeyword(tokens, pos, "DROP");
	if (r1.IsError())
		return Err(r1.Error());
	auto r2 = ExpectKeyword(tokens, r1.Value(), "TABLE");
	if (r2.IsError())
		return Err(r2.Error());
	pos = r2.Value();

	auto nameR = ExpectIdentifier(tokens, pos, "a table name");
	if (nameR.IsError())
		return Err(nameR.Error());
	String tableName = nameR.Value();
	++pos;

	auto endR = ExpectStatementEnd(tokens, pos);
	if (endR.IsError())
		return Err(endR.Error());

	return Ok(Statement(DropTableStatement{tableName}));
}

} // namespace detail

Result<Statement, String> ParseStatement(const std::vector<Token> &tokens) {
	if (tokens.empty() || tokens[0].type == TokenType::END)
		return Err(String("sql: empty statement"));

	const Token &first = tokens[0];
	if (first.type != TokenType::KEYWORD)
		return Err(String("sql: expected a statement keyword (CREATE/INSERT/SELECT/DELETE/DROP), got '") +
					first.text + "'");

	String kw = first.text.ToUpper();
	if (kw == "CREATE")
		return detail::ParseCreateTable(tokens, 0);
	if (kw == "INSERT")
		return detail::ParseInsert(tokens, 0);
	if (kw == "SELECT")
		return detail::ParseSelect(tokens, 0);
	if (kw == "DELETE")
		return detail::ParseDelete(tokens, 0);
	if (kw == "DROP")
		return detail::ParseDropTable(tokens, 0);

	return Err(String("sql: unknown statement keyword '") + first.text + "'");
}

} // namespace sql
