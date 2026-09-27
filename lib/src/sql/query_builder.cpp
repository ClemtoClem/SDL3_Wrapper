// Définitions de sql/query_builder.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sql/query_builder.hpp"

namespace sql {

// ── QueryBuilder ─────────────────────────────────────────────────────────────

QueryBuilder & QueryBuilder::Select(std::vector<String> columns) {
	m_columns = std::move(columns);
	return *this;
}

QueryBuilder & QueryBuilder::From(String tableName) {
	m_tableName = std::move(tableName);
	return *this;
}

QueryBuilder & QueryBuilder::Where(String column, String op, data::NodePtr value) {
	m_conditions.push_back(Condition{std::move(column), std::move(op), std::move(value)});
	return *this;
}

Result<QueryResult, String> QueryBuilder::Execute(Database &db) const {
	if (m_tableName.IsEmpty())
		return Err(String("sql: QueryBuilder.From(...) was not set"));

	SelectStatement stmt;
	stmt.tableName = m_tableName;
	stmt.columns = m_columns;

	if (!m_conditions.empty()) {
		auto rpnR = BuildWhereRpn();
		if (rpnR.IsError())
			return Err(rpnR.Error());
		stmt.where = Some(Expression{rpnR.Value()});
	}

	return db.ExecuteSelectStatement(stmt);
}

bool QueryBuilder::IsSupportedOperator(const String &op) noexcept {
	static const std::array<const char *, 7> OPS = {"=", "<>", "!=", "<", "<=", ">", ">="};
	for (auto *o : OPS)
		if (op == o)
			return true;
	return false;
}

Result<Token, String> QueryBuilder::ValueToLiteralToken(const data::NodePtr &value) {
	if (!value)
		return Err(String("sql: QueryBuilder.Where(...) value must not be null"));
	switch (value->type) {
		case data::NodeType::STRING:
			return Ok(Token{TokenType::STRING_LITERAL, value->stringValue});
		case data::NodeType::INT:
			return Ok(Token{TokenType::INT_LITERAL, String::From(value->intValue)});
		case data::NodeType::FLOAT:
			return Ok(Token{TokenType::FLOAT_LITERAL, String::From(value->floatValue, 6)});
		case data::NodeType::BOOL:
			return Ok(Token{TokenType::IDENTIFIER, value->boolValue ? String("TRUE") : String("FALSE")});
		default:
			return Err(String("sql: QueryBuilder.Where(...) value must be a scalar (STRING/BOOL/INT/FLOAT)"));
	}
}

Result<std::vector<Token>, String> QueryBuilder::BuildWhereRpn() const {
	std::vector<Token> rpn;
	for (size_t i = 0; i < m_conditions.size(); ++i) {
		const Condition &cond = m_conditions[i];
		if (!IsSupportedOperator(cond.op))
			return Err(String("sql: QueryBuilder.Where(...) unsupported operator '") + cond.op + "'");

		auto litR = ValueToLiteralToken(cond.value);
		if (litR.IsError())
			return Err(litR.Error());

		rpn.push_back(Token{TokenType::IDENTIFIER, cond.column});
		rpn.push_back(litR.Value());
		rpn.push_back(Token{TokenType::OPERATOR, cond.op});
		if (i > 0)
			rpn.push_back(Token{TokenType::KEYWORD, String("AND")});
	}
	return Ok(std::move(rpn));
}

} // namespace sql
