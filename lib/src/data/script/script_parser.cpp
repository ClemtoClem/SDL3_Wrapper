// Définitions de data/script/script_parser.hpp
#include "data/script/script_parser.hpp"

namespace data::script {

// ── Parser ───────────────────────────────────────────────────────────────────

Result<Program, ScriptError> Parser::Compile(StringView source) {
	Lexer lexer(source);
	auto tokens = lexer.Tokenize();
	if (tokens.IsError())
		return Err(tokens.Error());
	Parser parser(tokens.Unwrap());
	return parser.ParseProgram();
	}

	Result<Program, ScriptError> Parser::ParseProgram() {
	Program program;
	m_scopes.clear();
	m_awaitAllowed = true; // niveau principal : `await` permis (comme un `main` async)
	PushScope(true);	   // portée du programme : une portée de FONCTION (les `var` y vont)
	while (!Check(TokenType::END_OF_FILE)) {
		auto stmt = ParseStatement();
		if (stmt.IsError())
			return Err(stmt.Error());
		program.statements.push_back(std::move(stmt).Unwrap());
	}
	return Ok(std::move(program));
}

const Token& Parser::Peek(size_t offset) const noexcept {
	size_t i = m_pos + offset;
	return i < m_tokens.size() ? m_tokens[i] : m_tokens.back();
}

const Token& Parser::Previous() const noexcept {
	return m_tokens[m_pos > 0 ? m_pos - 1 : 0];
}

bool Parser::Check(TokenType t) const noexcept {
	return Peek().type == t;
}

const Token& Parser::Advance() noexcept {
	if (Peek().type != TokenType::END_OF_FILE)
		++m_pos;
	return Previous();
}

bool Parser::Match(TokenType t) noexcept {
	if (!Check(t))
		return false;
	Advance();
	return true;
}

ScriptError Parser::ErrorAt(const Token& tok, String message) const {
	return ScriptError(std::move(message), tok.line, tok.column);
}

Result<Token, ScriptError> Parser::Consume(TokenType t, const char* what) {
	if (Check(t))
		return Ok(Advance());
	return Err(ErrorAt(Peek(), String::Format("attendu %s, trouvé `%s`", what, TokenTypeName(Peek().type))));
}

void Parser::SkipOptionalSemicolons() {
	while (Match(TokenType::SEMICOLON)) {
	}
}

Result<StmtPtr, ScriptError> Parser::ParseStatement() {
	SkipOptionalSemicolons();
	const Token& tok = Peek();
	switch (tok.type) {
		case TokenType::KW_LET:
			return ParseLet(DeclKind::LET);
		case TokenType::KW_VAR:
			return ParseLet(DeclKind::VAR);
		case TokenType::KW_CONST:
			return ParseLet(DeclKind::CONST);
		case TokenType::KW_NAMESPACE:
			return ParseNamespace();
		case TokenType::KW_CLASS:
			return ParseClass(false, false);
		case TokenType::KW_INTERFACE:
			return ParseClass(false, true);
		case TokenType::KW_ENUM:
			return ParseEnum();
		case TokenType::KW_ABSTRACT: {
			Advance();
			if (!Check(TokenType::KW_CLASS))
				return Err(
					ErrorAt(Peek(), String("`abstract` au niveau d'une instruction : attendu `abstract class`")));
			return ParseClass(true, false);
		}
		case TokenType::KW_FN:
			return ParseFunctionStatement();
		case TokenType::KW_ASYNC: {
			if (Peek(1).type != TokenType::KW_FN)
				return Err(ErrorAt(Peek(1), String("attendu `fn` après `async`")));
			Advance();
			return ParseFunctionStatement(true);
		}
		case TokenType::KW_IF:
			return ParseIf();
		case TokenType::KW_WHILE:
			return ParseWhile();
		case TokenType::KW_DO:
			return ParseDoWhile();
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

Result<StmtPtr, ScriptError> Parser::ParseBlock() {
	auto open = Consume(TokenType::LEFT_BRACE, "`{`");
	if (open.IsError())
		return Err(open.Error());
	const Token openTok = open.Unwrap();

	auto block = std::make_unique<BlockStmt>(openTok.line, openTok.column);
	ScopeGuard scope(*this, false);
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

Result<StmtPtr, ScriptError> Parser::ParseLet(DeclKind kind) {
	const Token letTok = Advance();
	auto name = Consume(TokenType::IDENTIFIER, "un nom de variable");
	if (name.IsError())
		return Err(name.Error());
	auto type = ParseOptionalType();
	if (type.IsError())
		return Err(type.Error());

	ExprPtr init;
	if (Match(TokenType::ASSIGN)) {
		auto value = ParseExpression();
		if (value.IsError())
			return Err(value.Error());
		init = std::move(value).Unwrap();
	} else if (kind == DeclKind::CONST) {
		return Err(ErrorAt(
			letTok, String::Format("`const %s` doit être initialisée à sa déclaration", name.Unwrap().text.CStr())));
	}
	// Déclarée APRÈS l'initialiseur : `let x = x + 1` lit donc le `x`
	// englobant… sauf que, hissée, la nouvelle `x` le masque dès le début
	// du bloc — l'interpréteur rendra l'erreur de zone morte, comme JS.
	const StaticDecl declared = kind == DeclKind::VAR	  ? StaticDecl::VAR
								: kind == DeclKind::CONST ? StaticDecl::CONST
														: StaticDecl::LET;
	if (auto error = Declare(name.Unwrap(), declared); error.IsSome())
		return Err(error.Unwrap());
	SkipOptionalSemicolons();
	auto stmt = std::make_unique<LetStmt>(name.Unwrap().text, std::move(init), letTok.line, letTok.column, kind);
	stmt->type = std::move(type).Unwrap();
	return Ok(StmtPtr(std::move(stmt)));
}

Result<StmtPtr, ScriptError> Parser::ParseNamespace() {
	const Token nsTok = Advance();
	auto name = Consume(TokenType::IDENTIFIER, "un nom d'espace de noms");
	if (name.IsError())
		return Err(name.Error());
	if (auto error = Declare(name.Unwrap(), StaticDecl::NAMESPACE); error.IsSome())
		return Err(error.Unwrap());
	auto open = Consume(TokenType::LEFT_BRACE, "`{`");
	if (open.IsError())
		return Err(open.Error());
	auto stmt = std::make_unique<NamespaceStmt>(name.Unwrap().text, nsTok.line, nsTok.column);
	ScopeGuard scope(*this, true);
	while (!Check(TokenType::RIGHT_BRACE) && !Check(TokenType::END_OF_FILE)) {
		const Token& at = Peek();
		if (at.type == TokenType::KW_RETURN || at.type == TokenType::KW_BREAK || at.type == TokenType::KW_CONTINUE)
			return Err(ErrorAt(at, String::Format("`%s` interdit dans un espace de noms", TokenTypeName(at.type))));
		auto child = ParseStatement();
		if (child.IsError())
			return Err(child.Error());
		stmt->body.push_back(std::move(child).Unwrap());
	}
	auto close = Consume(TokenType::RIGHT_BRACE, "`}`");
	if (close.IsError())
		return Err(close.Error());
	return Ok(StmtPtr(std::move(stmt)));
}

bool Parser::IsWord(TokenType t) noexcept {
	return t == TokenType::IDENTIFIER || (t >= TokenType::KW_LET && t < TokenType::LEFT_PAREN);
}

Result<TypeRef, ScriptError> Parser::ParseOptionalType() {
	if (!Match(TokenType::COLON))
		return Ok(TypeRef());
	return ParseType();
}

Result<TypeRef, ScriptError> Parser::ParseType() {
	const Token at = Peek();
	auto type = std::make_shared<TypeExpr>();
	type->line = at.line;
	type->column = at.column;
	// `fn` est aussi un nom de type (valeur appelable).
	if (Match(TokenType::KW_FN)) {
		type->path.push_back(String("fn"));
	} else if (Match(TokenType::KW_NIL)) {
		type->path.push_back(String("nil"));
	} else {
		auto path = ParseTypePath("un nom de type");
		if (path.IsError())
			return Err(path.Error());
		type->path = std::move(path).Unwrap();
	}
	if (Match(TokenType::LESS)) {
		for (;;) {
			auto arg = ParseType();
			if (arg.IsError())
				return arg;
			type->args.push_back(std::move(arg).Unwrap());
			if (!Match(TokenType::COMMA))
				break;
		}
		auto close = Consume(TokenType::GREATER, "`>` (fin des paramètres du type)");
		if (close.IsError())
			return Err(close.Error());
	}
	if (Match(TokenType::QUESTION))
		type->nullable = true;
	return Ok(TypeRef(std::move(type)));
}

Result<TypePath, ScriptError> Parser::ParseTypePath(const char* what) {
	TypePath path;
	auto first = Consume(TokenType::IDENTIFIER, what);
	if (first.IsError())
		return Err(first.Error());
	path.push_back(first.Unwrap().text);
	while (Match(TokenType::DOT)) {
		auto next = Consume(TokenType::IDENTIFIER, what);
		if (next.IsError())
			return Err(next.Error());
		path.push_back(next.Unwrap().text);
	}
	return Ok(std::move(path));
}

Result<FunctionDefPtr, ScriptError> Parser::ParseSignature(const Token& at, String name) {
	auto def = std::make_shared<FunctionDef>();
	def->name = std::move(name);
	def->line = at.line;
	def->column = at.column;
	auto open = Consume(TokenType::LEFT_PAREN, "`(`");
	if (open.IsError())
		return Err(open.Error());
	ScopeGuard scope(*this, true);
	if (!Check(TokenType::RIGHT_PAREN)) {
		for (;;) {
			auto param = Consume(TokenType::IDENTIFIER, "un nom de paramètre");
			if (param.IsError())
				return Err(param.Error());
			if (auto error = Declare(param.Unwrap(), StaticDecl::PARAM); error.IsSome())
				return Err(error.Unwrap());
			auto paramType = ParseOptionalType();
			if (paramType.IsError())
				return Err(paramType.Error());
			def->params.push_back(param.Unwrap().text);
			def->paramTypes.push_back(std::move(paramType).Unwrap());
			if (!Match(TokenType::COMMA))
				break;
		}
	}
	auto close = Consume(TokenType::RIGHT_PAREN, "`)`");
	if (close.IsError())
		return Err(close.Error());
	auto returnType = ParseOptionalType();
	if (returnType.IsError())
		return Err(returnType.Error());
	def->returnType = std::move(returnType).Unwrap();
	if (Check(TokenType::LEFT_BRACE))
		return Err(ErrorAt(Peek(), String::Format("`%s` est une signature sans corps (méthode abstraite ou "
												"d'interface) : retirez le bloc `{ … }`",
												def->name.CStr())));
	SkipOptionalSemicolons();
	return Ok(std::move(def));
}

Result<std::vector<TypeParam>, ScriptError> Parser::ParseTypeParams() {
	std::vector<TypeParam> params;
	if (!Match(TokenType::LESS))
		return Ok(std::move(params));
	for (;;) {
		auto name = Consume(TokenType::IDENTIFIER, "un nom de paramètre de type");
		if (name.IsError())
			return Err(name.Error());
		TypeParam param;
		param.name = name.Unwrap().text;
		for (const TypeParam& existing : params)
			if (existing.name == param.name)
				return Err(
					ErrorAt(name.Value(), String::Format("paramètre de type `%s` en double", param.name.CStr())));
		if (Match(TokenType::KW_EXTENDS)) {
			auto bound = ParseType();
			if (bound.IsError())
				return Err(bound.Error());
			param.bound = std::move(bound).Unwrap();
		}
		params.push_back(std::move(param));
		if (!Match(TokenType::COMMA))
			break;
	}
	auto close = Consume(TokenType::GREATER, "`>` (fin des paramètres de type)");
	if (close.IsError())
		return Err(close.Error());
	return Ok(std::move(params));
}

Result<std::vector<TypeRef>, ScriptError> Parser::ParseTypeArgs() {
	std::vector<TypeRef> args;
	auto open = Consume(TokenType::LESS, "`<`");
	if (open.IsError())
		return Err(open.Error());
	for (;;) {
		auto arg = ParseType();
		if (arg.IsError())
			return Err(arg.Error());
		args.push_back(std::move(arg).Unwrap());
		if (!Match(TokenType::COMMA))
			break;
	}
	auto close = Consume(TokenType::GREATER, "`>` (fin des arguments de type)");
	if (close.IsError())
		return Err(close.Error());
	return Ok(std::move(args));
}

Option<std::vector<TypeRef>> Parser::TryParseCallTypeArgs() {
	if (!Check(TokenType::LESS))
		return NONE;
	const size_t saved = m_pos;
	auto args = ParseTypeArgs();
	if (args.IsOk() && Check(TokenType::LEFT_PAREN))
		return Some(std::move(args).Unwrap());
	m_pos = saved;
	return NONE;
}

Result<StmtPtr, ScriptError> Parser::ParseEnum() {
	const Token enumTok = Advance();
	auto name = Consume(TokenType::IDENTIFIER, "un nom de type énuméré");
	if (name.IsError())
		return Err(name.Error());
	if (auto error = Declare(name.Unwrap(), StaticDecl::CONST); error.IsSome())
		return Err(error.Unwrap());
	auto def = std::make_shared<ClassDef>();
	def->name = name.Unwrap().text;
	def->isEnum = true;
	def->line = enumTok.line;
	def->column = enumTok.column;
	if (Match(TokenType::KW_IMPLEMENTS)) {
		do {
			auto path = ParseTypePath("un nom d'interface");
			if (path.IsError())
				return Err(path.Error());
			def->interfaces.push_back(std::move(path).Unwrap());
		} while (Match(TokenType::COMMA));
	}
	auto open = Consume(TokenType::LEFT_BRACE, "`{`");
	if (open.IsError())
		return Err(open.Error());
	const bool savedAllowMap = m_allowMapLiteral;
	m_allowMapLiteral = true;
	ScopeGuard scope(*this, true);
	auto fail = [&](ScriptError error) {
		m_allowMapLiteral = savedAllowMap;
		return Result<StmtPtr, ScriptError>(Err(std::move(error)));
	};
	while (Check(TokenType::IDENTIFIER)) {
		const Token entryTok = Advance();
		for (const EnumEntry& existing : def->enumEntries)
			if (existing.name == entryTok.text)
				return fail(ErrorAt(entryTok, String::Format("valeur `%s` déclarée deux fois", entryTok.text.CStr())));
		EnumEntry entry;
		entry.name = entryTok.text;
		entry.line = entryTok.line;
		entry.column = entryTok.column;
		if (Match(TokenType::ASSIGN)) {
			AwaitContext noAwait(*this, false);
			auto value = ParseExpression();
			if (value.IsError())
				return fail(value.Error());
			entry.value = std::move(value).Unwrap();
		}
		def->enumEntries.push_back(std::move(entry));
		if (!Match(TokenType::COMMA))
			break;
	}
	if (def->enumEntries.empty())
		return fail(ErrorAt(Peek(), String::Format("le type énuméré `%s` n'a aucune valeur", def->name.CStr())));
	SkipOptionalSemicolons();
	while (!Check(TokenType::RIGHT_BRACE) && !Check(TokenType::END_OF_FILE)) {
		const Token at = Peek();
		if ((at.type == TokenType::KW_LET || at.type == TokenType::KW_VAR || at.type == TokenType::KW_CONST))
			return fail(ErrorAt(at, String("un type énuméré n'a pas de champ d'instance (ses valeurs sont "
										"uniques) : déclarez-le `static`")));
		if (auto member = ParseClassMember(*def); member.IsSome())
			return fail(member.Unwrap());
		SkipOptionalSemicolons();
	}
	m_allowMapLiteral = savedAllowMap;
	auto close = Consume(TokenType::RIGHT_BRACE, "`}`");
	if (close.IsError())
		return Err(close.Error());
	for (const MethodDef& method : def->methods)
		if (method.def->name == "init" || method.def->name == "deinit")
			return Err(ErrorAt(enumTok, String::Format("un type énuméré n'a ni constructeur ni destructeur "
													"(`%s.%s`)",
													def->name.CStr(), method.def->name.CStr())));
	SkipOptionalSemicolons();
	return Ok(StmtPtr(new ClassStmt(std::move(def), enumTok.line, enumTok.column)));
}

Result<StmtPtr, ScriptError> Parser::ParseClass(bool isAbstract, bool isInterface) {
	const Token classTok = Advance();
	auto name = Consume(TokenType::IDENTIFIER, isInterface ? "un nom d'interface" : "un nom de classe");
	if (name.IsError())
		return Err(name.Error());
	// Une classe est une constante : ni redéclarable, ni réaffectable.
	if (auto error = Declare(name.Unwrap(), StaticDecl::CONST); error.IsSome())
		return Err(error.Unwrap());

	auto def = std::make_shared<ClassDef>();
	def->name = name.Unwrap().text;
	def->isAbstract = isAbstract || isInterface;
	def->isInterface = isInterface;
	def->line = classTok.line;
	def->column = classTok.column;
	auto typeParams = ParseTypeParams();
	if (typeParams.IsError())
		return Err(typeParams.Error());
	def->typeParams = std::move(typeParams).Unwrap();

	auto parseList = [this](std::vector<TypePath>& out, const char* what) -> Option<ScriptError> {
		do {
			auto path = ParseTypePath(what);
			if (path.IsError())
				return Some(path.Error());
			out.push_back(std::move(path).Unwrap());
		} while (Match(TokenType::COMMA));
		return NONE;
	};
	if (Check(TokenType::KW_EXTENDS)) {
		Advance();
		if (isInterface) {
			if (auto error = parseList(def->interfaces, "un nom d'interface"); error.IsSome())
				return Err(error.Unwrap());
		} else {
			// `extends A, B, C` : au plus UNE classe de script, et des bases
			// fournies par l'hôte (owners), dans n'importe quel ordre. Ce que
			// désigne chaque nom n'est connu qu'à l'exécution (ExecClass).
			do {
				auto path = ParseTypePath("un nom de classe ou de base");
				if (path.IsError())
					return Err(path.Error());
				def->bases.push_back(std::move(path).Unwrap());
				// `extends Boîte<i32>` : les arguments de type vont à la classe
				// parente de script (une base de l'hôte n'est pas générique).
				if (Check(TokenType::LESS)) {
					if (!def->superTypeArgs.empty())
						return Err(ErrorAt(Peek(), String("arguments de type `<…>` donnés à deux bases : seule la "
														  "classe parente de script est générique")));
					auto args = ParseTypeArgs();
					if (args.IsError())
						return Err(args.Error());
					def->superTypeArgs = std::move(args).Unwrap();
					def->superTypeArgsBase = def->bases.size() - 1;
				}
			} while (Match(TokenType::COMMA));
		}
	}
	if (Check(TokenType::KW_IMPLEMENTS)) {
		const Token implTok = Advance();
		if (isInterface)
			return Err(ErrorAt(implTok, String("une interface ÉTEND d'autres interfaces (`extends`), elle "
											"n'implémente rien")));
		if (auto error = parseList(def->interfaces, "un nom d'interface"); error.IsSome())
			return Err(error.Unwrap());
	}

	auto open = Consume(TokenType::LEFT_BRACE, "`{`");
	if (open.IsError())
		return Err(open.Error());
	const bool savedAllowMap = m_allowMapLiteral;
	m_allowMapLiteral = true;
	ScopeGuard scope(*this, true); // les noms des membres ne fuient pas au dehors
	while (!Check(TokenType::RIGHT_BRACE) && !Check(TokenType::END_OF_FILE)) {
		auto member = ParseClassMember(*def);
		if (member.IsSome()) {
			m_allowMapLiteral = savedAllowMap;
			return Err(member.Unwrap());
		}
		SkipOptionalSemicolons();
	}
	m_allowMapLiteral = savedAllowMap;
	auto close = Consume(TokenType::RIGHT_BRACE, "`}`");
	if (close.IsError())
		return Err(close.Error());
	SkipOptionalSemicolons();
	return Ok(StmtPtr(new ClassStmt(std::move(def), classTok.line, classTok.column)));
}

Option<ScriptError> Parser::ParseClassMember(ClassDef& def) {
	bool isStatic = false, isAbstract = false, isOverride = false, isFactory = false, isAsync = false;
	for (;;) {
		bool* flag = Check(TokenType::KW_STATIC)	 ? &isStatic
					: Check(TokenType::KW_ABSTRACT) ? &isAbstract
					: Check(TokenType::KW_OVERRIDE) ? &isOverride
					: Check(TokenType::KW_FACTORY)	 ? &isFactory
					: Check(TokenType::KW_ASYNC)	 ? &isAsync
													: nullptr;
		if (!flag)
			break;
		if (*flag)
			return Some(ErrorAt(Peek(), String::Format("modificateur `%s` en double", TokenTypeName(Peek().type))));
		*flag = true;
		Advance();
	}
	const Token at = Peek();

	// ── Champ ────────────────────────────────────────────────────────
	if (at.type == TokenType::KW_LET || at.type == TokenType::KW_VAR || at.type == TokenType::KW_CONST) {
		if (def.isInterface)
			return Some(ErrorAt(at, String("une interface ne déclare que des signatures de méthodes")));
		if (isAbstract || isOverride || isFactory || isAsync)
			return Some(ErrorAt(at, String("`abstract`, `override`, `factory` et `async` s'appliquent aux méthodes")));
		Advance();
		auto name = Consume(TokenType::IDENTIFIER, "un nom de champ");
		if (name.IsError())
			return Some(name.Error());
		FieldDef field;
		field.name = name.Unwrap().text;
		auto fieldType = ParseOptionalType();
		if (fieldType.IsError())
			return Some(fieldType.Error());
		field.type = std::move(fieldType).Unwrap();
		field.declKind = at.type == TokenType::KW_CONST ? DeclKind::CONST
						: at.type == TokenType::KW_VAR ? DeclKind::VAR
														: DeclKind::LET;
		field.isStatic = isStatic;
		field.line = at.line;
		field.column = at.column;
		for (const FieldDef& existing : def.fields)
			if (existing.name == field.name && existing.isStatic == isStatic)
				return Some(ErrorAt(at, String::Format("champ `%s` déclaré deux fois", field.name.CStr())));
		if (Match(TokenType::ASSIGN)) {
			// Un initialiseur d'instance voit `this` (les champs déjà
			// initialisés), pas un initialiseur de classe.
			MemberContext context(*this, !isStatic, false);
			AwaitContext noAwait(*this, false);
			auto value = ParseExpression();
			if (value.IsError())
				return Some(value.Error());
			field.initializer = std::move(value).Unwrap();
		} else if (field.declKind == DeclKind::CONST) {
			return Some(
				ErrorAt(at, String::Format("champ `const %s` : valeur initiale obligatoire", field.name.CStr())));
		}
		def.fields.push_back(std::move(field));
		return NONE;
	}

	// ── Méthode ──────────────────────────────────────────────────────
	// `fn nom(…)`, `operator +(…)` ou `fn operator +(…)`.
	Token fnToken = at;
	if (!Check(TokenType::KW_OPERATOR)) {
		auto fnTok = Consume(TokenType::KW_FN, "`fn`, `operator`, `let`, `var` ou `const` (membre de classe)");
		if (fnTok.IsError())
			return Some(fnTok.Error());
		fnToken = fnTok.Unwrap();
	}
	String methodName;
	if (Check(TokenType::KW_OPERATOR)) {
		auto symbol = ParseOperatorName();
		if (symbol.IsError())
			return Some(symbol.Error());
		methodName = String("operator") + symbol.Unwrap();
	} else {
		auto name = Consume(TokenType::IDENTIFIER, "un nom de méthode");
		if (name.IsError())
			return Some(name.Error());
		methodName = name.Unwrap().text == "constructor"  ? String("init")
					: name.Unwrap().text == "destructor" ? String("deinit")
														: name.Unwrap().text;
	}
	const bool isInit = methodName == "init";
	const bool isOperator = methodName.StartsWith("operator");
	if (methodName == "deinit" && (isStatic || isFactory || isAbstract || isAsync || isOverride))
		return Some(ErrorAt(at, String("le destructeur `deinit` est une méthode d'instance ordinaire (ni "
									"`static`, ni `factory`, ni `abstract`, ni `async`, ni `override` : "
									"ceux des parents sont TOUJOURS appelés après lui)")));
	if (isOperator && (isFactory || isAbstract || isAsync))
		return Some(ErrorAt(at, String("un opérateur n'est ni `factory`, ni `abstract`, ni `async`")));
	auto typeParams = ParseTypeParams();
	if (typeParams.IsError())
		return Some(typeParams.Error());

	if (def.isInterface && (isStatic || isAbstract || isOverride || isFactory))
		return Some(ErrorAt(at, String("une méthode d'interface est une simple signature, sans modificateur")));
	if (isAbstract && !def.isAbstract)
		return Some(ErrorAt(at, String::Format("méthode abstraite `%s` dans la classe concrète `%s` : déclarez "
											"`abstract class %s`",
											methodName.CStr(), def.name.CStr(), def.name.CStr())));
	if (isAbstract && (isStatic || isFactory))
		return Some(ErrorAt(at, String("une méthode abstraite n'est ni `static` ni `factory`")));
	if (isFactory && isOverride)
		return Some(ErrorAt(at, String("un constructeur `factory` n'est pas hérité : `override` sans objet")));
	if (isInit && (isStatic || isFactory || isAbstract || isAsync))
		return Some(ErrorAt(at, String("le constructeur `init` est une méthode d'instance ordinaire (ni "
									"`static`, ni `factory`, ni `abstract`, ni `async`)")));
	if (isFactory && isAsync)
		return Some(ErrorAt(at, String("un constructeur `factory` rend une instance, pas un `Future` : pas "
									"d'`async`")));
	if (!isOperator)
		for (const MethodDef& existing : def.methods)
			if (existing.def->name == methodName && existing.isStatic == (isStatic || isFactory))
				return Some(ErrorAt(at, String::Format("méthode `%s` déclarée deux fois", methodName.CStr())));

	MethodDef method;
	method.isStatic = isStatic || isFactory;
	method.isAbstract = isAbstract || def.isInterface;
	method.isOverride = isOverride;
	method.isFactory = isFactory;
	{
		MemberContext context(*this, !method.isStatic, !method.isStatic && !def.bases.empty());
		auto parsed =
			method.isAbstract ? ParseSignature(fnToken, methodName) : ParseFunctionRest(fnToken, methodName, isAsync);
		if (parsed.IsError())
			return Some(parsed.Error());
		method.def = std::move(parsed).Unwrap();
		method.def->isAsync = isAsync;
		method.def->typeParams = std::move(typeParams).Unwrap();
	}
	if (method.def->name == "deinit" && !method.def->params.empty())
		return Some(ErrorAt(at, String("le destructeur `deinit` ne prend aucun paramètre")));
	if (isOperator) {
		if (auto error = CheckOperatorArity(at, *method.def, method.isStatic); error.IsSome())
			return error;
		for (const MethodDef& existing : def.methods)
			if (existing.def->name == method.def->name && existing.isStatic == method.isStatic)
				return Some(ErrorAt(at, String::Format("opérateur `%s` déclaré deux fois", method.def->name.CStr())));
	}
	def.methods.push_back(std::move(method));
	return NONE;
}

Result<String, ScriptError> Parser::ParseOperatorName() {
	const Token opTok = Advance(); // `operator`
	const Token symbol = Peek();
	switch (symbol.type) {
		case TokenType::PLUS:
		case TokenType::MINUS:
		case TokenType::STAR:
		case TokenType::SLASH:
		case TokenType::PERCENT:
		case TokenType::CONCAT:
		case TokenType::EQUAL:
		case TokenType::NOT_EQUAL:
		case TokenType::LESS:
		case TokenType::LESS_EQUAL:
		case TokenType::GREATER:
		case TokenType::GREATER_EQUAL:
		case TokenType::ARROW_RIGHT:
		case TokenType::ARROW_LEFT:
		case TokenType::ARROW_BOTH:
			Advance();
			return Ok(String(TokenTypeName(symbol.type)));
		case TokenType::LEFT_BRACKET: {
			Advance();
			auto close = Consume(TokenType::RIGHT_BRACKET, "`]` (`operator []`)");
			if (close.IsError())
				return Err(close.Error());
			return Ok(String(Match(TokenType::ASSIGN) ? "[]=" : "[]"));
		}
		case TokenType::LEFT_PAREN: {
			// `operator ()(args)` : la première paire est le symbole.
			if (Peek(1).type != TokenType::RIGHT_PAREN)
				break;
			Advance();
			Advance();
			return Ok(String("()"));
		}
		case TokenType::IDENTIFIER:
			if (symbol.text == "str" || symbol.text == "iter") {
				Advance();
				return Ok(String(" ") + symbol.text);
			}
			break;
		default:
			break;
	}
	return Err(ErrorAt(opTok, String::Format("opérateur surchargeable attendu après `operator` (+ - * / % .. == != "
											"< <= > >= -> <- <-> [] []= () str iter), trouvé `%s`",
											TokenTypeName(symbol.type))));
}

Option<ScriptError> Parser::CheckOperatorArity(const Token& at, FunctionDef& def, bool isStatic) {
	const String symbol = def.name.Substr(8); // après "operator"
	const int count = int(def.params.size());
	const int self = isStatic ? 0 : 1; // opérandes fournis par `this`
	auto refuse = [&](const char* expected) {
		return Some(ScriptError(String::Format("`operator %s` %s : attendu %s", symbol.CStr(),
											isStatic ? "(static)" : "(d'instance)", expected),
								at.line, at.column));
	};
	if (symbol == "-" && count + self == 1) {
		def.name = String("operator neg");
		return NONE;
	}
	if (symbol == "()") {
		if (isStatic)
			return refuse("une méthode d'instance");
		return NONE;
	}
	if (symbol == "[]" || symbol == "[]=" || symbol == " str" || symbol == " iter") {
		if (isStatic)
			return refuse("une méthode d'instance");
		const int expected = symbol == "[]" ? 1 : symbol == "[]=" ? 2 : 0;
		if (count != expected)
			return refuse(expected == 0	  ? "aucun paramètre"
						: expected == 1 ? "1 paramètre (l'index)"
										: "2 paramètres (l'index, la valeur)");
		return NONE;
	}
	if (count + self != 2)
		return refuse(isStatic ? "2 paramètres (les deux opérandes)" : "1 paramètre (l'opérande de droite)");
	return NONE;
}

Result<FunctionDefPtr, ScriptError> Parser::ParseFunctionRest(const Token& fnTok, String name, bool isAsync) {
	auto def = std::make_shared<FunctionDef>();
	def->name = std::move(name);
	def->isAsync = isAsync;
	// `await` n'a de sens que dans une fonction `async` (règle de Dart) —
	// une fonction ordinaire imbriquée dans une `async` n'y a pas droit.
	AwaitContext awaitContext(*this, isAsync);
	def->line = fnTok.line;
	def->column = fnTok.column;

	auto open = Consume(TokenType::LEFT_PAREN, "`(`");
	if (open.IsError())
		return Err(open.Error());

	// Paramètres ET corps partagent la portée de la fonction (un `let`
	// du même nom qu'un paramètre est donc une redéclaration).
	ScopeGuard scope(*this, true);
	if (!Check(TokenType::RIGHT_PAREN)) {
		for (;;) {
			auto param = Consume(TokenType::IDENTIFIER, "un nom de paramètre");
			if (param.IsError())
				return Err(param.Error());
			if (auto error = Declare(param.Unwrap(), StaticDecl::PARAM); error.IsSome())
				return Err(error.Unwrap());
			auto paramType = ParseOptionalType();
			if (paramType.IsError())
				return Err(paramType.Error());
			def->params.push_back(param.Unwrap().text);
			def->paramTypes.push_back(std::move(paramType).Unwrap());
			if (!Match(TokenType::COMMA))
				break;
		}
	}
	auto close = Consume(TokenType::RIGHT_PAREN, "`)`");
	if (close.IsError())
		return Err(close.Error());
	auto returnType = ParseOptionalType();
	if (returnType.IsError())
		return Err(returnType.Error());
	def->returnType = std::move(returnType).Unwrap();

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

Result<StmtPtr, ScriptError> Parser::ParseFunctionStatement(bool isAsync) {
	const Token fnTok = Advance();
	auto name = Consume(TokenType::IDENTIFIER, "un nom de fonction");
	if (name.IsError())
		return Err(name.Error());
	if (auto error = Declare(name.Unwrap(), StaticDecl::FUNCTION); error.IsSome())
		return Err(error.Unwrap());
	auto typeParams = ParseTypeParams();
	if (typeParams.IsError())
		return Err(typeParams.Error());
	auto def = ParseFunctionRest(fnTok, name.Unwrap().text, isAsync);
	if (def.IsError())
		return Err(def.Error());
	def.Value()->typeParams = std::move(typeParams).Unwrap();
	return Ok(StmtPtr(new FunctionStmt(std::move(def).Unwrap(), fnTok.line, fnTok.column)));
}

Result<ExprPtr, ScriptError> Parser::ParseControlCondition(const char* keyword) {
	auto open = Consume(TokenType::LEFT_PAREN,
		String::Format("`(` après `%s` — la condition s'écrit entre parenthèses : `%s (condition) { … }`",
			keyword, keyword).CStr());
	if (open.IsError())
		return Err(open.Error());
	bool saved = m_allowMapLiteral;
	m_allowMapLiteral = true;
	auto expr = ParseExpression();
	m_allowMapLiteral = saved;
	if (expr.IsError())
		return expr;
	auto close = Consume(TokenType::RIGHT_PAREN, "`)` (fin de la condition)");
	if (close.IsError())
		return Err(close.Error());
	return expr;
}

Result<StmtPtr, ScriptError> Parser::ParseIf() {
	const Token ifTok = Advance();
	auto stmt = std::make_unique<IfStmt>(ifTok.line, ifTok.column);

	auto cond = ParseControlCondition("if");
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

Result<StmtPtr, ScriptError> Parser::ParseWhile() {
	const Token whileTok = Advance();
	auto stmt = std::make_unique<WhileStmt>(whileTok.line, whileTok.column);

	auto cond = ParseControlCondition("while");
	if (cond.IsError())
		return Err(cond.Error());
	stmt->condition = std::move(cond).Unwrap();

	auto body = ParseBlock();
	if (body.IsError())
		return Err(body.Error());
	stmt->body = std::move(body).Unwrap();
	return Ok(StmtPtr(std::move(stmt)));
}

Result<StmtPtr, ScriptError> Parser::ParseDoWhile() {
	const Token doTok = Advance();
	auto stmt = std::make_unique<WhileStmt>(doTok.line, doTok.column);
	stmt->checkAfter = true;
	auto body = ParseBlock();
	if (body.IsError())
		return Err(body.Error());
	stmt->body = std::move(body).Unwrap();
	auto whileTok = Consume(TokenType::KW_WHILE, "`while (condition)` après le bloc d'un `do`");
	if (whileTok.IsError())
		return Err(whileTok.Error());
	auto cond = ParseControlCondition("while");
	if (cond.IsError())
		return Err(cond.Error());
	stmt->condition = std::move(cond).Unwrap();
	SkipOptionalSemicolons();
	return Ok(StmtPtr(std::move(stmt)));
}

Result<StmtPtr, ScriptError> Parser::ParseForIn() {
	const Token forTok = Advance();
	auto stmt = std::make_unique<ForInStmt>(forTok.line, forTok.column);

	auto open = Consume(TokenType::LEFT_PAREN, "`(` après `for` — la boucle s'écrit `for (x in liste) { … }`");
	if (open.IsError())
		return Err(open.Error());
	auto name = Consume(TokenType::IDENTIFIER, "un nom de variable de boucle");
	if (name.IsError())
		return Err(name.Error());
	stmt->variable = name.Unwrap().text;
	auto loopType = ParseOptionalType();
	if (loopType.IsError())
		return Err(loopType.Error());
	stmt->type = std::move(loopType).Unwrap();

	auto in = Consume(TokenType::KW_IN, "`in`");
	if (in.IsError())
		return Err(in.Error());

	bool savedMap = m_allowMapLiteral;
	m_allowMapLiteral = true;
	auto iterable = ParseExpression();
	m_allowMapLiteral = savedMap;
	if (iterable.IsError())
		return Err(iterable.Error());
	stmt->iterable = std::move(iterable).Unwrap();
	auto close = Consume(TokenType::RIGHT_PAREN, "`)` (fin de l'en-tête du `for`)");
	if (close.IsError())
		return Err(close.Error());

	// La variable de boucle vit dans une portée propre à chaque tour
	// (sémantique `let`), qui englobe le bloc du corps.
	ScopeGuard loopScope(*this, false);
	if (auto error = Declare(name.Unwrap(), StaticDecl::LET); error.IsSome())
		return Err(error.Unwrap());
	auto body = ParseBlock();
	if (body.IsError())
		return Err(body.Error());
	stmt->body = std::move(body).Unwrap();
	return Ok(StmtPtr(std::move(stmt)));
}

Result<StmtPtr, ScriptError> Parser::ParseReturn() {
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

Option<BinaryOp> Parser::CompoundOpFor(TokenType t) noexcept {
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

bool Parser::IsAssignable(const Expr& e) noexcept {
return e.kind == ExprKind::IDENTIFIER || e.kind == ExprKind::INDEX || e.kind == ExprKind::MEMBER;
}

Result<StmtPtr, ScriptError> Parser::ParseExpressionOrAssignment() {
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
		if (target->kind == ExprKind::IDENTIFIER) {
			const String& name = static_cast<const IdentifierExpr&>(*target).name;
			if (const StaticBinding* binding = Resolve(name)) {
				if (binding->kind == StaticDecl::CONST)
					return Err(ErrorAt(opTok, String::Format("`%s` est une constante (déclarée ligne %d) : "
															"réaffectation impossible",
															name.CStr(), binding->line)));
				if (binding->kind == StaticDecl::NAMESPACE)
					return Err(ErrorAt(
						opTok, String::Format("`%s` est un espace de noms : réaffectation impossible", name.CStr())));
			}
		}
		auto value = ParseExpression();
		if (value.IsError())
			return Err(value.Error());
		SkipOptionalSemicolons();
		return Ok(
			StmtPtr(new AssignStmt(std::move(target), std::move(value).Unwrap(), compound, opTok.line, opTok.column)));
	}

	SkipOptionalSemicolons();
	return Ok(StmtPtr(new ExpressionStmt(std::move(target), startTok.line, startTok.column)));
}

Result<ExprPtr, ScriptError> Parser::ParseExpression() {
	return ParseFlow();
}

Result<ExprPtr, ScriptError> Parser::ParseFlow() {
	auto left = ParseOr();
	if (left.IsError())
		return left;
	ExprPtr expr = std::move(left).Unwrap();
	for (;;) {
		if (Check(TokenType::ARROW_LEFT)) {
			const Token opTok = Advance();
			auto right = ParseFlow();
			if (right.IsError())
				return right;
			return Ok(ExprPtr(new BinaryExpr(BinaryOp::FLOW_LEFT, std::move(expr), std::move(right).Unwrap(),
											opTok.line, opTok.column)));
		}
		BinaryOp op;
		if (Check(TokenType::ARROW_RIGHT))
			op = BinaryOp::FLOW_RIGHT;
		else if (Check(TokenType::ARROW_BOTH))
			op = BinaryOp::FLOW_BOTH;
		else
			return Ok(std::move(expr));
		const Token opTok = Advance();
		auto right = ParseOr();
		if (right.IsError())
			return right;
		expr = ExprPtr(new BinaryExpr(op, std::move(expr), std::move(right).Unwrap(), opTok.line, opTok.column));
	}
}

Result<ExprPtr, ScriptError> Parser::ParseOr() {
	auto left = ParseAnd();
	if (left.IsError())
		return left;
	ExprPtr expr = std::move(left).Unwrap();
	while (Check(TokenType::KW_OR)) {
		const Token opTok = Advance();
		auto right = ParseAnd();
		if (right.IsError())
			return right;
		expr = ExprPtr(
			new LogicalExpr(LogicalOp::OR, std::move(expr), std::move(right).Unwrap(), opTok.line, opTok.column));
	}
	return Ok(std::move(expr));
}

Result<ExprPtr, ScriptError> Parser::ParseAnd() {
	auto left = ParseEquality();
	if (left.IsError())
		return left;
	ExprPtr expr = std::move(left).Unwrap();
	while (Check(TokenType::KW_AND)) {
		const Token opTok = Advance();
		auto right = ParseEquality();
		if (right.IsError())
			return right;
		expr = ExprPtr(
			new LogicalExpr(LogicalOp::AND, std::move(expr), std::move(right).Unwrap(), opTok.line, opTok.column));
	}
	return Ok(std::move(expr));
}

Result<ExprPtr, ScriptError> Parser::ParseEquality() {
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

Result<ExprPtr, ScriptError> Parser::ParseComparison() {
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
		else if (Check(TokenType::KW_IS))
			op = BinaryOp::IS;
		else if (Check(TokenType::KW_AS))
			op = BinaryOp::AS;
		else
			return Ok(std::move(expr));
		const Token opTok = Advance();
		auto right = ParseConcat();
		if (right.IsError())
			return right;
		expr = ExprPtr(new BinaryExpr(op, std::move(expr), std::move(right).Unwrap(), opTok.line, opTok.column));
	}
}

Result<ExprPtr, ScriptError> Parser::ParseConcat() {
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

Result<ExprPtr, ScriptError> Parser::ParseAdditive() {
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

Result<ExprPtr, ScriptError> Parser::ParseMultiplicative() {
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

Result<ExprPtr, ScriptError> Parser::ParseUnary() {
	if (Check(TokenType::KW_IMPORT)) {
			const Token importTok = Advance();
			auto operand = ParseUnary();
			if (operand.IsError())
				return operand;
			return Ok(ExprPtr(new ImportExpr(std::move(operand).Unwrap(), importTok.line, importTok.column)));
		}
	else if (Check(TokenType::KW_AWAIT)) {
		const Token awaitTok = Advance();
		if (!m_awaitAllowed)
			return Err(ErrorAt(awaitTok, String("`await` n'est permis que dans une fonction `async` (ou au niveau "
												"principal du script)")));
		auto operand = ParseUnary();
		if (operand.IsError())
			return operand;
		return Ok(ExprPtr(new AwaitExpr(std::move(operand).Unwrap(), awaitTok.line, awaitTok.column)));
	}
	else if (Check(TokenType::MINUS) || Check(TokenType::KW_NOT)) {
		const Token opTok = Advance();
		UnaryOp op = opTok.type == TokenType::MINUS ? UnaryOp::NEGATE : UnaryOp::NOT;
		auto operand = ParseUnary();
		if (operand.IsError())
			return operand;
		return Ok(ExprPtr(new UnaryExpr(op, std::move(operand).Unwrap(), opTok.line, opTok.column)));
	}
	return ParsePostfix();
}

Option<ScriptError> Parser::ParseCallArguments(CallExpr& call) {
	// Les arguments sont entre parenthèses : une table littérale y
	// redevient licite même dans une condition de contrôle.
	bool saved = m_allowMapLiteral;
	m_allowMapLiteral = true;
	if (!Check(TokenType::RIGHT_PAREN)) {
		for (;;) {
			auto arg = ParseExpression();
			if (arg.IsError()) {
				m_allowMapLiteral = saved;
				return Some(arg.Error());
			}
			call.args.push_back(std::move(arg).Unwrap());
			if (!Match(TokenType::COMMA))
				break;
		}
	}
	auto close = Consume(TokenType::RIGHT_PAREN, "`)`");
	m_allowMapLiteral = saved;
	if (close.IsError())
		return Some(close.Error());
	return NONE;
}

Result<ExprPtr, ScriptError> Parser::ParseNew() {
	const Token newTok = Advance();
	auto path = ParseTypePath("un nom de classe après `new`");
	if (path.IsError())
		return Err(path.Error());
	const TypePath& names = path.Value();
	ExprPtr callee(new IdentifierExpr(names.front(), newTok.line, newTok.column));
	for (size_t i = 1; i < names.size(); ++i)
		callee = ExprPtr(new MemberExpr(std::move(callee), names[i], newTok.line, newTok.column));
	std::vector<TypeRef> typeArgs;
	if (Check(TokenType::LESS)) {
		auto args = ParseTypeArgs();
		if (args.IsError())
			return Err(args.Error());
		typeArgs = std::move(args).Unwrap();
	}
	auto open = Consume(TokenType::LEFT_PAREN, "`(` (arguments du constructeur)");
	if (open.IsError())
		return Err(open.Error());
	auto call = std::make_unique<CallExpr>(std::move(callee), newTok.line, newTok.column);
	call->isNew = true;
	call->typeArgs = std::move(typeArgs);
	if (auto error = ParseCallArguments(*call); error.IsSome())
		return Err(error.Unwrap());
	return Ok(ExprPtr(std::move(call)));
}

Result<ExprPtr, ScriptError> Parser::ParsePostfix() {
	auto primary = Check(TokenType::KW_NEW) ? ParseNew() : ParsePrimary();
	if (primary.IsError())
		return primary;
	ExprPtr expr = std::move(primary).Unwrap();

	for (;;) {
		// `f<i32>(…)` : appel générique (sinon `<` reste une comparaison).
		std::vector<TypeRef> typeArgs;
		if ((expr->kind == ExprKind::IDENTIFIER || expr->kind == ExprKind::MEMBER) && Check(TokenType::LESS))
			if (Option<std::vector<TypeRef>> args = TryParseCallTypeArgs(); args.IsSome())
				typeArgs = std::move(args).Unwrap();
		if (Check(TokenType::LEFT_PAREN)) {
			const Token openTok = Advance();
			auto call = std::make_unique<CallExpr>(std::move(expr), openTok.line, openTok.column);
			call->typeArgs = std::move(typeArgs);
			if (auto error = ParseCallArguments(*call); error.IsSome())
				return Err(error.Unwrap());
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
			if (!IsWord(Peek().type))
				return Err(ErrorAt(Peek(),
								String::Format("attendu un nom de champ, trouvé `%s`", TokenTypeName(Peek().type))));
			const Token name = Advance();
			expr = ExprPtr(new MemberExpr(std::move(expr), name.text, dotTok.line, dotTok.column));
			continue;
		}
		return Ok(std::move(expr));
	}
}

Result<ExprPtr, ScriptError> Parser::ParsePrimary() {
	const Token& tok = Peek();
	switch (tok.type) {
		case TokenType::NUMBER: {
			const Token t = Advance();
			if (t.isInteger)
				return Ok(ExprPtr(new NumberExpr(t.integer, t.line, t.column)));
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
		case TokenType::KW_THIS: {
			const Token t = Advance();
			if (!m_thisAllowed)
				return Err(ErrorAt(t, String("`this` n'existe que dans une méthode (ou un initialiseur de champ) "
											"d'instance")));
			return Ok(ExprPtr(new ThisExpr(t.line, t.column)));
		}
		case TokenType::KW_SUPER: {
			const Token t = Advance();
			if (!m_superAllowed)
				return Err(ErrorAt(t, String("`super` n'existe que dans une méthode d'instance d'une classe qui "
											"étend (`extends`) une autre classe")));
			// `super(…)` : abrégé de `super.init(…)` (l'appel suit).
			if (Check(TokenType::LEFT_PAREN))
				return Ok(ExprPtr(new SuperExpr(String("init"), t.line, t.column)));
			auto dot = Consume(TokenType::DOT, "`.` ou `(` après `super`");
			if (dot.IsError())
				return Err(dot.Error());
			if (!IsWord(Peek().type))
				return Err(ErrorAt(Peek(), String("attendu un nom de méthode après `super.`")));
			const Token name = Advance();
			return Ok(ExprPtr(new SuperExpr(name.text, t.line, t.column)));
		}
		case TokenType::KW_ASYNC: {
			Advance();
			if (!Check(TokenType::KW_FN))
				return Err(ErrorAt(Peek(), String("attendu `fn` après `async`")));
			const Token t = Advance();
			auto def = ParseFunctionRest(t, String(), true);
			if (def.IsError())
				return Err(def.Error());
			return Ok(ExprPtr(new FunctionExpr(std::move(def).Unwrap(), t.line, t.column)));
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

Result<ExprPtr, ScriptError> Parser::ParseListLiteral() {
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

Result<ExprPtr, ScriptError> Parser::ParseMapLiteral() {
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
			if (IsWord(Peek().type) && Peek(1).type == TokenType::COLON) {
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

// ── Parser::AwaitContext ─────────────────────────────────────────────────────

Parser::AwaitContext::AwaitContext(Parser& p, bool allowed) : parser(p), saved(p.m_awaitAllowed) {
	parser.m_awaitAllowed = allowed;
}

Parser::AwaitContext::~AwaitContext() {
	parser.m_awaitAllowed = saved;
}

// ── Parser::MemberContext ────────────────────────────────────────────────────

Parser::MemberContext::MemberContext(Parser& p, bool allowThis, bool allowSuper) : parser(p), savedThis(p.m_thisAllowed), savedSuper(p.m_superAllowed) {
	parser.m_thisAllowed = allowThis;
	parser.m_superAllowed = allowSuper;
}

Parser::MemberContext::~MemberContext() {
	parser.m_thisAllowed = savedThis;
	parser.m_superAllowed = savedSuper;
}

// ── Parser::ScopeGuard ───────────────────────────────────────────────────────

Parser::ScopeGuard::ScopeGuard(Parser& p, bool function) : parser(p) {
	parser.PushScope(function);
}

Parser::ScopeGuard::~ScopeGuard() {
	parser.PopScope();
}

// ── Parser ───────────────────────────────────────────────────────────────────

void Parser::PushScope(bool function) {
	m_scopes.push_back(StaticScope{function, {}});
}

void Parser::PopScope() {
	if (!m_scopes.empty())
		m_scopes.pop_back();
}

Parser::StaticBinding* Parser::FindIn(StaticScope& scope, const String& name) {
	for (StaticBinding& binding : scope.names)
		if (binding.name == name)
			return &binding;
	return nullptr;
}

const Parser::StaticBinding* Parser::Resolve(const String& name) {
	for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it)
		if (const StaticBinding* binding = FindIn(*it, name))
			return binding;
	return nullptr;
}

bool Parser::IsLexical(StaticDecl kind) noexcept {
	return kind == StaticDecl::LET || kind == StaticDecl::CONST || kind == StaticDecl::NAMESPACE;
}

Option<ScriptError> Parser::Declare(const Token& at, StaticDecl kind) {
	if (m_scopes.empty())
		PushScope(true);
	const String& name = at.text;
	auto conflict = [&](const StaticBinding& existing) {
		return ErrorAt(
			at, String::Format("`%s` est déjà déclarée dans cette portée (ligne %d)", name.CStr(), existing.line));
	};
	StaticScope& current = m_scopes.back();

	if (kind == StaticDecl::VAR) {
		// Un `var` appartient à la portée de fonction la plus proche : il
		// traverse les blocs intermédiaires, et heurte tout `let`/`const`
		// du même nom qu'il y rencontre.
		for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
			if (StaticBinding* existing = FindIn(*it, name)) {
				if (IsLexical(existing->kind))
					return Some(conflict(*existing));
			} else {
				it->names.push_back(StaticBinding{name, StaticDecl::VAR, at.line});
			}
			if (it->function)
				break;
		}
		return NONE;
	}

	StaticBinding* existing = FindIn(current, name);
	if (existing) {
		const bool reopen = kind == StaticDecl::NAMESPACE && existing->kind == StaticDecl::NAMESPACE;
		const bool redefine = !IsLexical(kind) && !IsLexical(existing->kind); // fn/param/var entre eux
		if (kind == StaticDecl::PARAM && existing->kind == StaticDecl::PARAM)
			return Some(ErrorAt(at, String::Format("paramètre `%s` en double", name.CStr())));
		if (!reopen && !redefine)
			return Some(conflict(*existing));
		return NONE;
	}
	current.names.push_back(StaticBinding{name, kind, at.line});
	return NONE;
}

} // namespace data::script
