#pragma once
/**
 * data::script — analyse syntaxique (descente récursive + niveaux de
 * précédence explicites) produisant un `Program` (cf. script_ast.hpp).
 *
 * Précédences, du plus lâche au plus serré (calquées sur Lua, qui place la
 * concaténation entre les comparaisons et l'addition) :
 *
 *   or → and → == != → < <= > >= → .. → + - → * / % → unaire → postfixe
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
	[[nodiscard]] static Result<Program, ScriptError> Compile(StringView source) {
		Lexer lexer(source);
		auto tokens = lexer.Tokenize();
		if (tokens.IsError())
			return Err(tokens.Error());
		Parser parser(tokens.Unwrap());
		return parser.ParseProgram();
	}

	[[nodiscard]] Result<Program, ScriptError> ParseProgram() {
		Program program;
		while (!Check(TokenType::END_OF_FILE)) {
			auto stmt = ParseStatement();
			if (stmt.IsError())
				return Err(stmt.Error());
			program.statements.push_back(std::move(stmt).Unwrap());
		}
		return Ok(std::move(program));
	}

private:
	// ── Curseur ──────────────────────────────────────────────────────────────

	[[nodiscard]] const Token &Peek(size_t offset = 0) const noexcept {
		size_t i = m_pos + offset;
		return i < m_tokens.size() ? m_tokens[i] : m_tokens.back();
	}
	[[nodiscard]] const Token &Previous() const noexcept { return m_tokens[m_pos > 0 ? m_pos - 1 : 0]; }
	[[nodiscard]] bool Check(TokenType t) const noexcept { return Peek().type == t; }

	const Token &Advance() noexcept {
		if (Peek().type != TokenType::END_OF_FILE)
			++m_pos;
		return Previous();
	}

	bool Match(TokenType t) noexcept {
		if (!Check(t))
			return false;
		Advance();
		return true;
	}

	[[nodiscard]] ScriptError ErrorAt(const Token &tok, String message) const {
		return ScriptError(std::move(message), tok.line, tok.column);
	}

	[[nodiscard]] Result<Token, ScriptError> Consume(TokenType t, const char *what) {
		if (Check(t))
			return Ok(Advance());
		return Err(ErrorAt(Peek(), String::Format("attendu %s, trouvé `%s`", what, TokenTypeName(Peek().type))));
	}

	/// `;` et retours à la ligne sont facultatifs : on absorbe simplement un
	/// `;` s'il est là (cf. script_lexer.hpp — aucun token de fin de ligne).
	void SkipOptionalSemicolons() {
		while (Match(TokenType::SEMICOLON)) {
		}
	}

	// ── Instructions ─────────────────────────────────────────────────────────

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseStatement() {
		SkipOptionalSemicolons();
		const Token &tok = Peek();
		switch (tok.type) {
			case TokenType::KW_LET:
				return ParseLet();
			case TokenType::KW_FN:
				return ParseFunctionStatement();
			case TokenType::KW_IF:
				return ParseIf();
			case TokenType::KW_WHILE:
				return ParseWhile();
			case TokenType::KW_FOR:
				return ParseForIn();
			case TokenType::KW_RETURN:
				return ParseReturn();
			case TokenType::KW_BREAK:
				Advance();
				SkipOptionalSemicolons();
				return Ok(StmtPtr(new BreakStmt(tok.line, tok.column)));
			case TokenType::KW_CONTINUE:
				Advance();
				SkipOptionalSemicolons();
				return Ok(StmtPtr(new ContinueStmt(tok.line, tok.column)));
			case TokenType::LEFT_BRACE:
				return ParseBlock();
			default:
				return ParseExpressionOrAssignment();
		}
	}

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseBlock() {
		auto open = Consume(TokenType::LEFT_BRACE, "`{`");
		if (open.IsError())
			return Err(open.Error());
		const Token openTok = open.Unwrap();

		auto block = std::make_unique<BlockStmt>(openTok.line, openTok.column);
		while (!Check(TokenType::RIGHT_BRACE) && !Check(TokenType::END_OF_FILE)) {
			auto stmt = ParseStatement();
			if (stmt.IsError())
				return Err(stmt.Error());
			block->statements.push_back(std::move(stmt).Unwrap());
		}
		auto close = Consume(TokenType::RIGHT_BRACE, "`}`");
		if (close.IsError())
			return Err(close.Error());
		return Ok(StmtPtr(std::move(block)));
	}

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseLet() {
		const Token letTok = Advance();
		auto name = Consume(TokenType::IDENTIFIER, "un nom de variable");
		if (name.IsError())
			return Err(name.Error());

		ExprPtr init;
		if (Match(TokenType::ASSIGN)) {
			auto value = ParseExpression();
			if (value.IsError())
				return Err(value.Error());
			init = std::move(value).Unwrap();
		}
		SkipOptionalSemicolons();
		return Ok(StmtPtr(new LetStmt(name.Unwrap().text, std::move(init), letTok.line, letTok.column)));
	}

	[[nodiscard]] Result<FunctionDefPtr, ScriptError> ParseFunctionRest(const Token &fnTok, String name) {
		auto def = std::make_shared<FunctionDef>();
		def->name = std::move(name);
		def->line = fnTok.line;
		def->column = fnTok.column;

		auto open = Consume(TokenType::LEFT_PAREN, "`(`");
		if (open.IsError())
			return Err(open.Error());

		if (!Check(TokenType::RIGHT_PAREN)) {
			for (;;) {
				auto param = Consume(TokenType::IDENTIFIER, "un nom de paramètre");
				if (param.IsError())
					return Err(param.Error());
				def->params.push_back(param.Unwrap().text);
				if (!Match(TokenType::COMMA))
					break;
			}
		}
		auto close = Consume(TokenType::RIGHT_PAREN, "`)`");
		if (close.IsError())
			return Err(close.Error());

		// Le corps est parsé ici plutôt que délégué à ParseBlock() pour le
		// stocker à plat dans FunctionDef::body (l'interpréteur crée déjà sa
		// propre portée d'appel : un BlockStmt supplémentaire serait une
		// portée imbriquée inutile à chaque appel).
		bool savedAllowMap = m_allowMapLiteral;
		m_allowMapLiteral = true;
		auto openBrace = Consume(TokenType::LEFT_BRACE, "`{`");
		if (openBrace.IsError()) {
			m_allowMapLiteral = savedAllowMap;
			return Err(openBrace.Error());
		}
		while (!Check(TokenType::RIGHT_BRACE) && !Check(TokenType::END_OF_FILE)) {
			auto stmt = ParseStatement();
			if (stmt.IsError()) {
				m_allowMapLiteral = savedAllowMap;
				return Err(stmt.Error());
			}
			def->body.push_back(std::move(stmt).Unwrap());
		}
		auto closeBrace = Consume(TokenType::RIGHT_BRACE, "`}`");
		m_allowMapLiteral = savedAllowMap;
		if (closeBrace.IsError())
			return Err(closeBrace.Error());

		return Ok(std::move(def));
	}

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseFunctionStatement() {
		const Token fnTok = Advance();
		auto name = Consume(TokenType::IDENTIFIER, "un nom de fonction");
		if (name.IsError())
			return Err(name.Error());
		auto def = ParseFunctionRest(fnTok, name.Unwrap().text);
		if (def.IsError())
			return Err(def.Error());
		return Ok(StmtPtr(new FunctionStmt(std::move(def).Unwrap(), fnTok.line, fnTok.column)));
	}

	/// Condition de structure de contrôle : `{` y ouvre toujours le bloc
	/// (cf. en-tête du fichier).
	[[nodiscard]] Result<ExprPtr, ScriptError> ParseControlCondition() {
		bool saved = m_allowMapLiteral;
		m_allowMapLiteral = false;
		auto expr = ParseExpression();
		m_allowMapLiteral = saved;
		return expr;
	}

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseIf() {
		const Token ifTok = Advance();
		auto stmt = std::make_unique<IfStmt>(ifTok.line, ifTok.column);

		auto cond = ParseControlCondition();
		if (cond.IsError())
			return Err(cond.Error());
		stmt->condition = std::move(cond).Unwrap();

		auto thenBranch = ParseBlock();
		if (thenBranch.IsError())
			return Err(thenBranch.Error());
		stmt->thenBranch = std::move(thenBranch).Unwrap();

		if (Match(TokenType::KW_ELSE)) {
			// `else if` s'enchaîne récursivement ; `else { }` est un bloc.
			auto elseBranch = Check(TokenType::KW_IF) ? ParseIf() : ParseBlock();
			if (elseBranch.IsError())
				return Err(elseBranch.Error());
			stmt->elseBranch = std::move(elseBranch).Unwrap();
		}
		return Ok(StmtPtr(std::move(stmt)));
	}

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseWhile() {
		const Token whileTok = Advance();
		auto stmt = std::make_unique<WhileStmt>(whileTok.line, whileTok.column);

		auto cond = ParseControlCondition();
		if (cond.IsError())
			return Err(cond.Error());
		stmt->condition = std::move(cond).Unwrap();

		auto body = ParseBlock();
		if (body.IsError())
			return Err(body.Error());
		stmt->body = std::move(body).Unwrap();
		return Ok(StmtPtr(std::move(stmt)));
	}

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseForIn() {
		const Token forTok = Advance();
		auto stmt = std::make_unique<ForInStmt>(forTok.line, forTok.column);

		auto name = Consume(TokenType::IDENTIFIER, "un nom de variable de boucle");
		if (name.IsError())
			return Err(name.Error());
		stmt->variable = name.Unwrap().text;

		auto in = Consume(TokenType::KW_IN, "`in`");
		if (in.IsError())
			return Err(in.Error());

		auto iterable = ParseControlCondition();
		if (iterable.IsError())
			return Err(iterable.Error());
		stmt->iterable = std::move(iterable).Unwrap();

		auto body = ParseBlock();
		if (body.IsError())
			return Err(body.Error());
		stmt->body = std::move(body).Unwrap();
		return Ok(StmtPtr(std::move(stmt)));
	}

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseReturn() {
		const Token returnTok = Advance();
		ExprPtr value;
		// `return` nu (fin de bloc ou `;`) rend `nil`.
		if (!Check(TokenType::RIGHT_BRACE) && !Check(TokenType::SEMICOLON) && !Check(TokenType::END_OF_FILE)) {
			auto expr = ParseExpression();
			if (expr.IsError())
				return Err(expr.Error());
			value = std::move(expr).Unwrap();
		}
		SkipOptionalSemicolons();
		return Ok(StmtPtr(new ReturnStmt(std::move(value), returnTok.line, returnTok.column)));
	}

	[[nodiscard]] static Option<BinaryOp> CompoundOpFor(TokenType t) noexcept {
		switch (t) {
			case TokenType::PLUS_ASSIGN:
				return Some(BinaryOp::ADD);
			case TokenType::MINUS_ASSIGN:
				return Some(BinaryOp::SUBTRACT);
			case TokenType::STAR_ASSIGN:
				return Some(BinaryOp::MULTIPLY);
			case TokenType::SLASH_ASSIGN:
				return Some(BinaryOp::DIVIDE);
			default:
				return NONE;
		}
	}

	[[nodiscard]] static bool IsAssignable(const Expr &e) noexcept {
		return e.kind == ExprKind::IDENTIFIER || e.kind == ExprKind::INDEX || e.kind == ExprKind::MEMBER;
	}

	[[nodiscard]] Result<StmtPtr, ScriptError> ParseExpressionOrAssignment() {
		const Token startTok = Peek();
		auto lhs = ParseExpression();
		if (lhs.IsError())
			return Err(lhs.Error());
		ExprPtr target = std::move(lhs).Unwrap();

		Option<BinaryOp> compound = CompoundOpFor(Peek().type);
		if (Check(TokenType::ASSIGN) || compound.IsSome()) {
			const Token opTok = Advance();
			if (!IsAssignable(*target))
				return Err(ErrorAt(opTok, String("cible d'affectation invalide (attendu une variable, `a[i]` ou "
												 "`a.b`)")));
			auto value = ParseExpression();
			if (value.IsError())
				return Err(value.Error());
			SkipOptionalSemicolons();
			return Ok(StmtPtr(new AssignStmt(std::move(target), std::move(value).Unwrap(), compound, opTok.line,
											 opTok.column)));
		}

		SkipOptionalSemicolons();
		return Ok(StmtPtr(new ExpressionStmt(std::move(target), startTok.line, startTok.column)));
	}

	// ── Expressions (précédence croissante) ──────────────────────────────────

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseExpression() { return ParseOr(); }

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseOr() {
		auto left = ParseAnd();
		if (left.IsError())
			return left;
		ExprPtr expr = std::move(left).Unwrap();
		while (Check(TokenType::KW_OR)) {
			const Token opTok = Advance();
			auto right = ParseAnd();
			if (right.IsError())
				return right;
			expr = ExprPtr(new LogicalExpr(LogicalOp::OR, std::move(expr), std::move(right).Unwrap(), opTok.line,
										   opTok.column));
		}
		return Ok(std::move(expr));
	}

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseAnd() {
		auto left = ParseEquality();
		if (left.IsError())
			return left;
		ExprPtr expr = std::move(left).Unwrap();
		while (Check(TokenType::KW_AND)) {
			const Token opTok = Advance();
			auto right = ParseEquality();
			if (right.IsError())
				return right;
			expr = ExprPtr(new LogicalExpr(LogicalOp::AND, std::move(expr), std::move(right).Unwrap(), opTok.line,
										   opTok.column));
		}
		return Ok(std::move(expr));
	}

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseEquality() {
		auto left = ParseComparison();
		if (left.IsError())
			return left;
		ExprPtr expr = std::move(left).Unwrap();
		for (;;) {
			BinaryOp op;
			if (Check(TokenType::EQUAL))
				op = BinaryOp::EQUAL;
			else if (Check(TokenType::NOT_EQUAL))
				op = BinaryOp::NOT_EQUAL;
			else
				return Ok(std::move(expr));
			const Token opTok = Advance();
			auto right = ParseComparison();
			if (right.IsError())
				return right;
			expr = ExprPtr(new BinaryExpr(op, std::move(expr), std::move(right).Unwrap(), opTok.line, opTok.column));
		}
	}

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseComparison() {
		auto left = ParseConcat();
		if (left.IsError())
			return left;
		ExprPtr expr = std::move(left).Unwrap();
		for (;;) {
			BinaryOp op;
			if (Check(TokenType::LESS))
				op = BinaryOp::LESS;
			else if (Check(TokenType::LESS_EQUAL))
				op = BinaryOp::LESS_EQUAL;
			else if (Check(TokenType::GREATER))
				op = BinaryOp::GREATER;
			else if (Check(TokenType::GREATER_EQUAL))
				op = BinaryOp::GREATER_EQUAL;
			else
				return Ok(std::move(expr));
			const Token opTok = Advance();
			auto right = ParseConcat();
			if (right.IsError())
				return right;
			expr = ExprPtr(new BinaryExpr(op, std::move(expr), std::move(right).Unwrap(), opTok.line, opTok.column));
		}
	}

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseConcat() {
		auto left = ParseAdditive();
		if (left.IsError())
			return left;
		ExprPtr expr = std::move(left).Unwrap();
		while (Check(TokenType::CONCAT)) {
			const Token opTok = Advance();
			auto right = ParseAdditive();
			if (right.IsError())
				return right;
			expr = ExprPtr(
				new BinaryExpr(BinaryOp::CONCAT, std::move(expr), std::move(right).Unwrap(), opTok.line, opTok.column));
		}
		return Ok(std::move(expr));
	}

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseAdditive() {
		auto left = ParseMultiplicative();
		if (left.IsError())
			return left;
		ExprPtr expr = std::move(left).Unwrap();
		for (;;) {
			BinaryOp op;
			if (Check(TokenType::PLUS))
				op = BinaryOp::ADD;
			else if (Check(TokenType::MINUS))
				op = BinaryOp::SUBTRACT;
			else
				return Ok(std::move(expr));
			const Token opTok = Advance();
			auto right = ParseMultiplicative();
			if (right.IsError())
				return right;
			expr = ExprPtr(new BinaryExpr(op, std::move(expr), std::move(right).Unwrap(), opTok.line, opTok.column));
		}
	}

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseMultiplicative() {
		auto left = ParseUnary();
		if (left.IsError())
			return left;
		ExprPtr expr = std::move(left).Unwrap();
		for (;;) {
			BinaryOp op;
			if (Check(TokenType::STAR))
				op = BinaryOp::MULTIPLY;
			else if (Check(TokenType::SLASH))
				op = BinaryOp::DIVIDE;
			else if (Check(TokenType::PERCENT))
				op = BinaryOp::MODULO;
			else
				return Ok(std::move(expr));
			const Token opTok = Advance();
			auto right = ParseUnary();
			if (right.IsError())
				return right;
			expr = ExprPtr(new BinaryExpr(op, std::move(expr), std::move(right).Unwrap(), opTok.line, opTok.column));
		}
	}

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseUnary() {
		if (Check(TokenType::MINUS) || Check(TokenType::KW_NOT)) {
			const Token opTok = Advance();
			UnaryOp op = opTok.type == TokenType::MINUS ? UnaryOp::NEGATE : UnaryOp::NOT;
			auto operand = ParseUnary();
			if (operand.IsError())
				return operand;
			return Ok(ExprPtr(new UnaryExpr(op, std::move(operand).Unwrap(), opTok.line, opTok.column)));
		}
		return ParsePostfix();
	}

	[[nodiscard]] Result<ExprPtr, ScriptError> ParsePostfix() {
		auto primary = ParsePrimary();
		if (primary.IsError())
			return primary;
		ExprPtr expr = std::move(primary).Unwrap();

		for (;;) {
			if (Check(TokenType::LEFT_PAREN)) {
				const Token openTok = Advance();
				auto call = std::make_unique<CallExpr>(std::move(expr), openTok.line, openTok.column);
				// Les arguments sont entre parenthèses : une table littérale
				// y redevient licite même dans une condition de contrôle.
				bool saved = m_allowMapLiteral;
				m_allowMapLiteral = true;
				if (!Check(TokenType::RIGHT_PAREN)) {
					for (;;) {
						auto arg = ParseExpression();
						if (arg.IsError()) {
							m_allowMapLiteral = saved;
							return arg;
						}
						call->args.push_back(std::move(arg).Unwrap());
						if (!Match(TokenType::COMMA))
							break;
					}
				}
				auto close = Consume(TokenType::RIGHT_PAREN, "`)`");
				m_allowMapLiteral = saved;
				if (close.IsError())
					return Err(close.Error());
				expr = ExprPtr(std::move(call));
				continue;
			}
			if (Check(TokenType::LEFT_BRACKET)) {
				const Token openTok = Advance();
				bool saved = m_allowMapLiteral;
				m_allowMapLiteral = true;
				auto index = ParseExpression();
				if (index.IsError()) {
					m_allowMapLiteral = saved;
					return index;
				}
				auto close = Consume(TokenType::RIGHT_BRACKET, "`]`");
				m_allowMapLiteral = saved;
				if (close.IsError())
					return Err(close.Error());
				expr = ExprPtr(new IndexExpr(std::move(expr), std::move(index).Unwrap(), openTok.line, openTok.column));
				continue;
			}
			if (Check(TokenType::DOT)) {
				const Token dotTok = Advance();
				auto name = Consume(TokenType::IDENTIFIER, "un nom de champ");
				if (name.IsError())
					return Err(name.Error());
				expr = ExprPtr(new MemberExpr(std::move(expr), name.Unwrap().text, dotTok.line, dotTok.column));
				continue;
			}
			return Ok(std::move(expr));
		}
	}

	[[nodiscard]] Result<ExprPtr, ScriptError> ParsePrimary() {
		const Token &tok = Peek();
		switch (tok.type) {
			case TokenType::NUMBER: {
				const Token t = Advance();
				return Ok(ExprPtr(new NumberExpr(t.number, t.line, t.column)));
			}
			case TokenType::STRING_LITERAL: {
				const Token t = Advance();
				return Ok(ExprPtr(new StringExpr(t.text, t.line, t.column)));
			}
			case TokenType::KW_TRUE: {
				const Token t = Advance();
				return Ok(ExprPtr(new BooleanExpr(true, t.line, t.column)));
			}
			case TokenType::KW_FALSE: {
				const Token t = Advance();
				return Ok(ExprPtr(new BooleanExpr(false, t.line, t.column)));
			}
			case TokenType::KW_NIL: {
				const Token t = Advance();
				return Ok(ExprPtr(new NilExpr(t.line, t.column)));
			}
			case TokenType::IDENTIFIER: {
				const Token t = Advance();
				return Ok(ExprPtr(new IdentifierExpr(t.text, t.line, t.column)));
			}
			case TokenType::KW_FN: {
				const Token t = Advance();
				auto def = ParseFunctionRest(t, String());
				if (def.IsError())
					return Err(def.Error());
				return Ok(ExprPtr(new FunctionExpr(std::move(def).Unwrap(), t.line, t.column)));
			}
			case TokenType::LEFT_PAREN: {
				Advance();
				bool saved = m_allowMapLiteral;
				m_allowMapLiteral = true;
				auto inner = ParseExpression();
				if (inner.IsError()) {
					m_allowMapLiteral = saved;
					return inner;
				}
				auto close = Consume(TokenType::RIGHT_PAREN, "`)`");
				m_allowMapLiteral = saved;
				if (close.IsError())
					return Err(close.Error());
				return inner;
			}
			case TokenType::LEFT_BRACKET:
				return ParseListLiteral();
			case TokenType::LEFT_BRACE:
				if (!m_allowMapLiteral)
					return Err(ErrorAt(tok, String("ici `{` ouvre le bloc de la structure de contrôle — pour une "
												   "table littérale, entourez-la de parenthèses")));
				return ParseMapLiteral();
			default:
				break;
		}
		return Err(ErrorAt(tok, String::Format("expression attendue, trouvé `%s`", TokenTypeName(tok.type))));
	}

	[[nodiscard]] Result<ExprPtr, ScriptError> ParseListLiteral() {
		const Token openTok = Advance();
		auto list = std::make_unique<ListExpr>(openTok.line, openTok.column);
		bool saved = m_allowMapLiteral;
		m_allowMapLiteral = true;
		if (!Check(TokenType::RIGHT_BRACKET)) {
			for (;;) {
				auto element = ParseExpression();
				if (element.IsError()) {
					m_allowMapLiteral = saved;
					return element;
				}
				list->elements.push_back(std::move(element).Unwrap());
				if (!Match(TokenType::COMMA))
					break;
				// Virgule finale tolérée : `[1, 2, 3,]`.
				if (Check(TokenType::RIGHT_BRACKET))
					break;
			}
		}
		auto close = Consume(TokenType::RIGHT_BRACKET, "`]`");
		m_allowMapLiteral = saved;
		if (close.IsError())
			return Err(close.Error());
		return Ok(ExprPtr(std::move(list)));
	}

	/// `{ "a": 1, b: 2 }` — une clé identifiant nu vaut la chaîne du même nom
	/// (comme en JavaScript/YAML), ce qui rend les tables de configuration
	/// bien plus lisibles dans un script d'éditeur.
	[[nodiscard]] Result<ExprPtr, ScriptError> ParseMapLiteral() {
		const Token openTok = Advance();
		auto map = std::make_unique<MapExpr>(openTok.line, openTok.column);
		bool saved = m_allowMapLiteral;
		m_allowMapLiteral = true;

		auto fail = [&](ScriptError e) {
			m_allowMapLiteral = saved;
			return Result<ExprPtr, ScriptError>(Err(std::move(e)));
		};

		if (!Check(TokenType::RIGHT_BRACE)) {
			for (;;) {
				MapEntry entry;
				if (Check(TokenType::IDENTIFIER) && Peek(1).type == TokenType::COLON) {
					const Token keyTok = Advance();
					entry.key = ExprPtr(new StringExpr(keyTok.text, keyTok.line, keyTok.column));
				} else {
					auto key = ParseExpression();
					if (key.IsError())
						return fail(key.Error());
					entry.key = std::move(key).Unwrap();
				}
				auto colon = Consume(TokenType::COLON, "`:`");
				if (colon.IsError())
					return fail(colon.Error());
				auto value = ParseExpression();
				if (value.IsError())
					return fail(value.Error());
				entry.value = std::move(value).Unwrap();
				map->entries.push_back(std::move(entry));

				if (!Match(TokenType::COMMA))
					break;
				if (Check(TokenType::RIGHT_BRACE))
					break;
			}
		}
		auto close = Consume(TokenType::RIGHT_BRACE, "`}`");
		m_allowMapLiteral = saved;
		if (close.IsError())
			return Err(close.Error());
		return Ok(ExprPtr(std::move(map)));
	}

	std::vector<Token> m_tokens;
	size_t m_pos = 0;
	bool m_allowMapLiteral = true;
};

} // namespace data::script
