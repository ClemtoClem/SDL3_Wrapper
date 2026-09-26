// Tests unitaires — data::script (langage de script embarqué).
//
// Couvre le lexeur, le parseur, l'interpréteur, la bibliothèque standard, les
// garde-fous (budget d'instructions / profondeur d'appel), le pont
// Value <-> data::Node et la couche codecs (`parse`/`encode`).
//
// Aucune fenêtre, aucun GPU : 100% CPU, exécutable partout.
#define USE_TEST

#include "core/test.hpp"
#include "data/script.hpp"

using namespace data::script;

namespace {

/// Exécute une source et rend la valeur produite par son `return` de plus
/// haut niveau — ou fait échouer le test avec le message d'erreur formaté.
Value Eval(const char *source) {
	Interpreter vm;
	auto result = vm.Run(StringView(source));
	if (result.IsError()) {
		test::ReportFailure(__FILE__, __LINE__, result.Error().Format().CStr());
		return Value::Nil();
	}
	return result.Value();
}

/// Variante pour les cas d'erreur attendue : rend le message formaté.
String EvalError(const char *source) {
	Interpreter vm;
	auto result = vm.Run(StringView(source));
	if (result.IsOk())
		return String();
	return result.Error().Format();
}

double Number(const char *source) { return Eval(source).AsNumber(); }

} // namespace

// ============================================================================
// Lexeur
// ============================================================================

TEST(ScriptLexer, TokenizesLiteralsKeywordsAndOperators) {
	Lexer lexer(StringView("let x = 1.5e2 + \"a\\nb\" # commentaire\n"));
	auto tokens = lexer.Tokenize();
	ASSERT_TRUE(tokens.IsOk());

	// 6 tokens utiles + le token END_OF_FILE terminal.
	const std::vector<Token> &t = tokens.Value();
	ASSERT_TRUE(t.size() == 7);
	EXPECT_TRUE(t[6].type == TokenType::END_OF_FILE);
	EXPECT_TRUE(t[0].type == TokenType::KW_LET);
	EXPECT_TRUE(t[1].type == TokenType::IDENTIFIER);
	EXPECT_EQ(t[1].text, "x");
	EXPECT_TRUE(t[2].type == TokenType::ASSIGN);
	EXPECT_TRUE(t[3].type == TokenType::NUMBER);
	EXPECT_EQ(t[3].number, 150.0);
	EXPECT_TRUE(t[4].type == TokenType::PLUS);
	EXPECT_TRUE(t[5].type == TokenType::STRING_LITERAL);
	EXPECT_EQ(t[5].text, "a\nb");
}

TEST(ScriptLexer, ReportsUnterminatedStringWithPosition) {
	Lexer lexer(StringView("let a = 1\nlet b = \"oops"));
	auto tokens = lexer.Tokenize();
	ASSERT_TRUE(tokens.IsError());
	EXPECT_EQ(tokens.Error().line, 2);
}

TEST(ScriptLexer, RewindsWhenEIsNotAnExponent) {
	// `2ex` doit se lire NOMBRE(2) puis IDENTIFIANT(ex), pas un exposant
	// tronqué : c'est le chemin de rembobinage de LexNumber.
	Lexer lexer(StringView("2ex"));
	auto tokens = lexer.Tokenize();
	ASSERT_TRUE(tokens.IsOk());
	ASSERT_TRUE(tokens.Value().size() == 3);
	EXPECT_TRUE(tokens.Value()[0].type == TokenType::NUMBER);
	EXPECT_EQ(tokens.Value()[0].number, 2.0);
	EXPECT_TRUE(tokens.Value()[1].type == TokenType::IDENTIFIER);
	EXPECT_EQ(tokens.Value()[1].text, "ex");
}

// ============================================================================
// Parseur — précédences et messages
// ============================================================================

TEST(ScriptParser, RespectsArithmeticPrecedence) {
	EXPECT_EQ(Number("return 2 + 3 * 4"), 14.0);
	EXPECT_EQ(Number("return (2 + 3) * 4"), 20.0);
	EXPECT_EQ(Number("return 10 - 2 - 3"), 5.0); // associativité à gauche
	EXPECT_EQ(Number("return -2 + 5"), 3.0);
}

TEST(ScriptParser, ConcatBindsTighterThanComparison) {
	// `..` plus serré que `<` (convention Lua) : sans ça, `"a" .. "b" == "ab"`
	// se lirait `"a" .. ("b" == "ab")`.
	EXPECT_TRUE(Eval("return \"a\" .. \"b\" == \"ab\"").AsBoolean());
}

TEST(ScriptParser, BraceInConditionOpensBlockNotMapLiteral) {
	// Le cœur de l'ambiguïté `{` : `if flag { ... }` ne doit pas lire `{ ... }`
	// comme une table littérale.
	EXPECT_EQ(Number("let flag = true\nlet r = 0\nif flag { r = 7 }\nreturn r"), 7.0);
	// ... et une table reste écrivable entre parenthèses dans une condition.
	EXPECT_EQ(Number("if ({\"a\": 1})[\"a\"] == 1 { return 5 }\nreturn 0"), 5.0);
}

TEST(ScriptParser, RejectsInvalidAssignmentTarget) {
	String error = EvalError("1 + 2 = 3");
	EXPECT_TRUE(error.Contains("cible d'affectation invalide"));
}

TEST(ScriptParser, ErrorCarriesLineAndColumn) {
	Interpreter vm;
	auto result = vm.Run(StringView("let a = 1\nlet b = (2 + \n"));
	ASSERT_TRUE(result.IsError());
	EXPECT_EQ(result.Error().line, 3);
}

// ============================================================================
// Interpréteur — flot de contrôle
// ============================================================================

TEST(ScriptInterpreter, IfElseIfElseChain) {
	const char *source = "fn classify(n) {\n"
						 "    if n < 0 { return \"negatif\" }\n"
						 "    else if n == 0 { return \"zero\" }\n"
						 "    else { return \"positif\" }\n"
						 "}\n"
						 "return classify(-3) .. \",\" .. classify(0) .. \",\" .. classify(9)";
	EXPECT_EQ(Eval(source).AsString(), "negatif,zero,positif");
}

TEST(ScriptInterpreter, WhileWithBreakAndContinue) {
	const char *source = "let i = 0\n"
						 "let sum = 0\n"
						 "while true {\n"
						 "    i += 1\n"
						 "    if i > 10 { break }\n"
						 "    if i % 2 == 0 { continue }\n"
						 "    sum += i\n"
						 "}\n"
						 "return sum"; // 1+3+5+7+9
	EXPECT_EQ(Number(source), 25.0);
}

TEST(ScriptInterpreter, ForInOverListMapAndString) {
	EXPECT_EQ(Number("let s = 0\nfor v in [1, 2, 3, 4] { s += v }\nreturn s"), 10.0);
	// Table littérale itérée : parenthèses obligatoires (cf. ParsePrimary —
	// dans un `for ... in`, `{` ouvre le bloc).
	EXPECT_EQ(Eval("let out = \"\"\nfor k in ({a: 1, b: 2}) { out = out .. k }\nreturn out").AsString(), "ab");
	EXPECT_TRUE(EvalError("for k in {a: 1} { }").Contains("entourez-la de parenthèses"));
	EXPECT_EQ(Number("let n = 0\nfor c in \"abcde\" { n += 1 }\nreturn n"), 5.0);
}

TEST(ScriptInterpreter, ForInSnapshotsTheSequence) {
	// Le corps modifie la liste itérée : l'itération doit rester bornée par
	// l'état INITIAL (matérialisation avant boucle) et ne jamais boucler.
	const char *source = "let items = [1, 2, 3]\n"
						 "let count = 0\n"
						 "for v in items { count += 1; push(items, v) }\n"
						 "return count";
	EXPECT_EQ(Number(source), 3.0);
}

TEST(ScriptInterpreter, LogicalOperatorsShortCircuitAndReturnValues) {
	EXPECT_EQ(Number("return nil or 42"), 42.0);
	EXPECT_EQ(Number("return 7 and 8"), 8.0);
	EXPECT_TRUE(Eval("return nil and boom()").IsNil()); // `boom` n'existe pas : preuve du court-circuit
	EXPECT_TRUE(Eval("return not nil").AsBoolean());
}

TEST(ScriptInterpreter, ZeroAndEmptyStringAreTruthy) {
	// Convention Lua assumée (cf. Value::IsTruthy) — vérifiée explicitement
	// parce que c'est le piège n°1 pour qui vient de Python.
	EXPECT_EQ(Number("if 0 { return 1 }\nreturn 0"), 1.0);
	EXPECT_EQ(Number("if \"\" { return 1 }\nreturn 0"), 1.0);
}

// ============================================================================
// Interpréteur — fonctions et fermetures
// ============================================================================

TEST(ScriptInterpreter, RecursionAndArgumentPassing) {
	const char *source = "fn fact(n) {\n"
						 "    if n <= 1 { return 1 }\n"
						 "    return n * fact(n - 1)\n"
						 "}\n"
						 "return fact(6)";
	EXPECT_EQ(Number(source), 720.0);
}

TEST(ScriptInterpreter, ClosuresCaptureTheirEnvironment) {
	const char *source = "fn make_counter(start) {\n"
						 "    let n = start\n"
						 "    return fn() { n += 1; return n }\n"
						 "}\n"
						 "let c = make_counter(10)\n"
						 "c()\n"
						 "c()\n"
						 "return c()";
	EXPECT_EQ(Number(source), 13.0);
}

TEST(ScriptInterpreter, ArityMismatchIsAnError) {
	EXPECT_TRUE(EvalError("fn f(a, b) { return a }\nreturn f(1)").Contains("attend 2 argument"));
}

TEST(ScriptInterpreter, HostCanCallScriptFunctionsAfterRun) {
	// Le mode d'emploi de l'éditeur : le script déclare `on_update`, l'hôte
	// l'appelle à chaque image.
	Interpreter vm;
	auto loaded = vm.Run(StringView("let total = 0\nfn on_update(dt) { total += dt; return total }"));
	ASSERT_TRUE(loaded.IsOk());

	for (int i = 0; i < 3; ++i) {
		auto called = vm.CallGlobalIfPresent(String("on_update"), {Value::Number(0.5)});
		ASSERT_TRUE(called.IsSome());
		ASSERT_TRUE(called.Unwrap().IsOk());
	}
	Option<Value> total = vm.GetGlobal(String("total"));
	ASSERT_TRUE(total.IsSome());
	EXPECT_EQ(total.Unwrap().AsNumber(), 1.5);

	EXPECT_TRUE(vm.CallGlobalIfPresent(String("on_absent"), {}).IsNone());
}

// ============================================================================
// Garde-fous — un script fautif ne doit JAMAIS figer l'hôte
// ============================================================================

TEST(ScriptGuards, InfiniteLoopHitsStepBudget) {
	Interpreter vm;
	vm.maxSteps = 5000;
	auto result = vm.Run(StringView("while true { }"));
	ASSERT_TRUE(result.IsError());
	EXPECT_TRUE(result.Error().message.Contains("budget d'exécution"));
}

TEST(ScriptGuards, InfiniteRecursionHitsCallDepth) {
	Interpreter vm;
	vm.maxCallDepth = 32;
	auto result = vm.Run(StringView("fn boom() { return boom() }\nreturn boom()"));
	ASSERT_TRUE(result.IsError());
	EXPECT_TRUE(result.Error().message.Contains("profondeur d'appel"));
}

TEST(ScriptGuards, DivisionByZeroIsAnErrorNotInfinity) {
	EXPECT_TRUE(EvalError("return 1 / 0").Contains("division par zéro"));
	EXPECT_TRUE(EvalError("return 1 % 0").Contains("modulo par zéro"));
}

TEST(ScriptGuards, AssigningToAnUndeclaredVariableIsAnError) {
	// Le piège des globales implicites de Lua, refusé ici.
	EXPECT_TRUE(EvalError("undeclared = 3").Contains("variable inconnue"));
}

// ============================================================================
// Bibliothèque standard
// ============================================================================

TEST(ScriptStdlib, ListOperations) {
	EXPECT_EQ(Number("let l = [1, 2, 3]\npush(l, 4)\nreturn len(l)"), 4.0);
	EXPECT_EQ(Number("let l = [1, 2, 3]\nreturn pop(l)"), 3.0);
	EXPECT_EQ(Number("let l = [1, 2, 3]\nremove(l, 0)\nreturn l[0]"), 2.0);
	EXPECT_EQ(Number("let l = [5, 1, 4]\nsort_numbers(l)\nreturn l[0] * 100 + l[2]"), 105.0);
	EXPECT_EQ(Number("return index_of([\"a\", \"b\"], \"b\")"), 1.0);
	EXPECT_EQ(Number("let s = 0\nfor v in range(1, 5) { s += v }\nreturn s"), 10.0);
	// Lecture hors bornes = nil (documenté), écriture hors bornes = erreur.
	EXPECT_TRUE(Eval("return [1, 2][9]").IsNil());
	EXPECT_TRUE(EvalError("let l = [1]\nl[9] = 3").Contains("hors bornes"));
}

TEST(ScriptStdlib, MapOperations) {
	EXPECT_EQ(Number("let m = {a: 1}\nm.b = 2\nreturn len(m)"), 2.0);
	EXPECT_EQ(Eval("let m = {a: 1, b: 2}\nreturn join(keys(m), \"-\")").AsString(), "a-b");
	EXPECT_TRUE(Eval("return has({a: 1}, \"a\")").AsBoolean());
	EXPECT_TRUE(Eval("let m = {a: 1}\nerase(m, \"a\")\nreturn has(m, \"a\")").AsBoolean() == false);
	// Champ absent = nil, jamais une erreur.
	EXPECT_TRUE(Eval("return {a: 1}.zzz").IsNil());
}

TEST(ScriptStdlib, StringOperations) {
	EXPECT_EQ(Eval("return upper(\"abc\")").AsString(), "ABC");
	EXPECT_EQ(Eval("return sub(\"bonjour\", 0, 3)").AsString(), "bon");
	EXPECT_EQ(Number("return find(\"bonjour\", \"jour\")"), 3.0);
	EXPECT_EQ(Number("return find(\"bonjour\", \"zzz\")"), -1.0);
	EXPECT_EQ(Eval("return join(split(\"a,b,c\", \",\"), \"|\")").AsString(), "a|b|c");
	EXPECT_EQ(Eval("return format(\"{} sur {}\", 3, 10)").AsString(), "3 sur 10");
	EXPECT_EQ(Eval("return replace(\"aXbXc\", \"X\", \"-\")").AsString(), "a-b-c");
}

TEST(ScriptStdlib, NumbersPrintWithoutTrailingDecimals) {
	// `"objet " .. i` doit donner "objet 3", pas "objet 3.000000".
	EXPECT_EQ(Eval("return \"objet \" .. 3").AsString(), "objet 3");
	EXPECT_EQ(Eval("return str(2.5)").AsString(), "2.5");
}

TEST(ScriptStdlib, NumConversionReturnsNilRatherThanFailing) {
	EXPECT_EQ(Number("return num(\"12.5\")"), 12.5);
	EXPECT_TRUE(Eval("return num(\"pas un nombre\")").IsNil());
	EXPECT_EQ(Number("return num(\"zzz\") or -1"), -1.0);
}

TEST(ScriptStdlib, PrintIsCapturedWhenNoSinkIsInstalled) {
	Interpreter vm;
	auto result = vm.Run(StringView("print(\"a\", 1)\nprint(\"b\")"));
	ASSERT_TRUE(result.IsOk());
	ASSERT_TRUE(vm.Output().size() == 2);
	EXPECT_EQ(vm.Output()[0], "a 1");
	EXPECT_EQ(vm.Output()[1], "b");
}

TEST(ScriptStdlib, PrintGoesToTheHostSinkWhenInstalled) {
	Interpreter vm;
	std::vector<String> captured;
	vm.onPrint = [&captured](const String &line) { captured.push_back(line); };
	auto result = vm.Run(StringView("print(\"vers la console\")"));
	ASSERT_TRUE(result.IsOk());
	ASSERT_TRUE(captured.size() == 1);
	EXPECT_EQ(captured[0], "vers la console");
	EXPECT_TRUE(vm.Output().empty());
}

TEST(ScriptStdlib, AssertFailsWithItsOwnMessage) {
	EXPECT_TRUE(EvalError("assert(1 == 2, \"les tours ne collent pas\")").Contains("les tours ne collent pas"));
	EXPECT_TRUE(EvalError("assert(true)").IsEmpty());
}

TEST(ScriptStdlib, RandomIsReproducibleForAGivenSeed) {
	// Indispensable pour que deux exécutions de la démo produisent le même
	// rapport et les mêmes captures d'écran.
	auto roll = [](uint64_t seed) {
		Interpreter vm;
		vm.SetRandomSeed(seed);
		auto result = vm.Run(StringView("return math.random() + math.random() * 100"));
		return result.IsOk() ? result.Value().AsNumber() : -1.0;
	};
	EXPECT_EQ(roll(1234), roll(1234));
	EXPECT_TRUE(roll(1234) != roll(9999));
	EXPECT_TRUE(roll(1234) >= 0.0);
}

// ============================================================================
// Hôte C++ <-> script
// ============================================================================

TEST(ScriptHost, NativeFunctionsReceiveArgumentsAndReportErrors) {
	Interpreter vm;
	double accumulated = 0.0;
	vm.RegisterNative(String("add_force"), 2, 2,
					  [&accumulated](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
						  if (!args[0].IsNumber() || !args[1].IsNumber())
							  return Err(Interpreter::MakeError(String("add_force attend deux nombres")));
						  accumulated += args[0].AsNumber() * args[1].AsNumber();
						  return Ok(Value::Number(accumulated));
					  });

	auto ok = vm.Run(StringView("add_force(2, 3)\nreturn add_force(4, 5)"));
	ASSERT_TRUE(ok.IsOk());
	EXPECT_EQ(ok.Value().AsNumber(), 26.0);
	EXPECT_EQ(accumulated, 26.0);

	// Arité vérifiée par l'interpréteur AVANT d'entrer dans la native.
	auto badArity = vm.Run(StringView("return add_force(1)"));
	ASSERT_TRUE(badArity.IsError());
	EXPECT_TRUE(badArity.Error().message.Contains("attend 2 argument"));

	// Erreur remontée PAR la native.
	auto badType = vm.Run(StringView("return add_force(\"x\", 1)"));
	ASSERT_TRUE(badType.IsError());
	EXPECT_TRUE(badType.Error().message.Contains("deux nombres"));
}

TEST(ScriptHost, NamespacedNativesGroupTheHostApi) {
	Interpreter vm;
	int spawned = 0;
	vm.RegisterNamespacedNative(String("scene"), String("spawn"), 1, 1,
							   [&spawned](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   spawned += int(args[0].AsNumber());
								   return Ok(Value::Nil());
							   });
	vm.RegisterNamespacedNative(String("scene"), String("count"), 0, 0,
							   [&spawned](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Number(double(spawned)));
							   });

	auto result = vm.Run(StringView("scene.spawn(2)\nscene.spawn(3)\nreturn scene.count()"));
	ASSERT_TRUE(result.IsOk());
	EXPECT_EQ(result.Value().AsNumber(), 5.0);
}

TEST(ScriptHost, ListsAndMapsHaveReferenceSemanticsAcrossTheBoundary) {
	// L'hôte donne une liste au script, le script la remplit, l'hôte la relit.
	Interpreter vm;
	auto shared = std::make_shared<ListObject>();
	vm.SetGlobal(String("out"), Value::List(shared));

	auto result = vm.Run(StringView("for i in range(0, 4) { push(out, i * i) }"));
	ASSERT_TRUE(result.IsOk());
	ASSERT_TRUE(shared->items.size() == 4);
	EXPECT_EQ(shared->items[3].AsNumber(), 9.0);
}

// ============================================================================
// Pont data::Node <-> Value
// ============================================================================

TEST(ScriptNodeBridge, NodeToValueAndBack) {
	auto root = data::Node::MakeObject();
	root->Set("name", data::Node::MakeString("piste"));
	root->Set("laps", data::Node::MakeInt(3));
	root->Set("gravity", data::Node::MakeFloat(-9.81));
	auto checkpoints = data::Node::MakeArray();
	checkpoints->Push(data::Node::MakeInt(10));
	checkpoints->Push(data::Node::MakeInt(20));
	root->Set("checkpoints", checkpoints);

	Value value = ValueFromNode(root);
	ASSERT_TRUE(value.IsMap());

	Interpreter vm;
	vm.SetGlobal(String("track"), value);
	auto result = vm.Run(StringView("return track.name .. \":\" .. track.laps .. \":\" .. len(track.checkpoints)"));
	ASSERT_TRUE(result.IsOk());
	EXPECT_EQ(result.Value().AsString(), "piste:3:2");

	// Aller-retour : Value -> Node conserve types et ordre.
	data::NodePtr back = NodeFromValue(value);
	ASSERT_TRUE(back->IsObject());
	ASSERT_TRUE(back->Keys().size() == 4);
	EXPECT_EQ(back->Keys()[0], "name");
	EXPECT_TRUE(back->Get("laps")->IsInt());
	EXPECT_EQ(back->Get("laps")->intValue, int64_t(3));
	EXPECT_TRUE(back->Get("gravity")->IsFloat());
	EXPECT_TRUE(back->Get("checkpoints")->IsArray());
}

TEST(ScriptNodeBridge, FunctionsBecomeNoneRatherThanFailing) {
	Interpreter vm;
	auto result = vm.Run(StringView("return {cb: fn() { return 1 }, n: 2}"));
	ASSERT_TRUE(result.IsOk());
	data::NodePtr node = NodeFromValue(result.Value());
	ASSERT_TRUE(node->IsObject());
	EXPECT_TRUE(node->Get("cb")->IsNone());
	EXPECT_TRUE(node->Get("n")->IsInt());
}

// ============================================================================
// Couche codecs (InstallDataLibrary) — ce qui relie le langage au module data
// ============================================================================

TEST(ScriptDataLibrary, ParsesJsonIntoScriptValues) {
	Interpreter vm;
	InstallDataLibrary(vm);
	const char *source = "let doc = parse(\"json\", \"{\\\"a\\\": [1, 2, 3], \\\"b\\\": {\\\"c\\\": true}}\")\n"
						 "return len(doc.a) + doc.a[2] + (doc.b.c and 10 or 0)";
	auto result = vm.Run(StringView(source));
	ASSERT_TRUE(result.IsOk());
	EXPECT_EQ(result.Value().AsNumber(), 16.0); // 3 + 3 + 10
}

TEST(ScriptDataLibrary, EncodeThenParseRoundTrips) {
	Interpreter vm;
	InstallDataLibrary(vm);
	const char *source = "let original = {name: \"circuit\", laps: 3, checkpoints: [1, 2]}\n"
						 "let text = encode(\"json\", original)\n"
						 "let back = parse(\"json\", text)\n"
						 "return back.name .. \"/\" .. back.laps .. \"/\" .. len(back.checkpoints)";
	auto result = vm.Run(StringView(source));
	ASSERT_TRUE(result.IsOk());
	EXPECT_EQ(result.Value().AsString(), "circuit/3/2");
}

TEST(ScriptDataLibrary, UnknownFormatAndMalformedTextAreErrors) {
	Interpreter vm;
	InstallDataLibrary(vm);
	auto unknown = vm.Run(StringView("return parse(\"brainfuck\", \"x\")"));
	ASSERT_TRUE(unknown.IsError());
	EXPECT_TRUE(unknown.Error().message.Contains("format inconnu"));

	auto malformed = vm.Run(StringView("return parse(\"json\", \"{ pas du json\")"));
	EXPECT_TRUE(malformed.IsError());
}

TEST(ScriptDataLibrary, ReadFileReturnsNilForAMissingPath) {
	Interpreter vm;
	InstallDataLibrary(vm);
	auto result = vm.Run(StringView("return read_file(\"/chemin/qui/n/existe/pas.sled\")"));
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().IsNil());
}

// ============================================================================
// Portées : var / let / const
// ============================================================================
//
// | forme   | portée              | réaffectation | redéclaration | hissage              |
// |---------|---------------------|---------------|---------------|----------------------|
// | `var`   | fonction ou globale | oui           | oui           | oui, vaut nil        |
// | `let`   | bloc                | oui           | non           | oui, zone morte      |
// | `const` | bloc                | NON           | non           | oui, zone morte      |

TEST(ScriptScope, VarBelongsToTheFunctionLetAndConstToTheBlock) {
	// `var` sort du bloc (portée de fonction)…
	EXPECT_EQ(Number("fn f() { if true { var x = 7 } return x }\nreturn f()"), 7.0);
	EXPECT_EQ(Number("if true { var y = 3 }\nreturn y"), 3.0); // … ou globale
	// … `let` et `const`, non.
	EXPECT_TRUE(EvalError("fn f() { if true { let x = 7 } return x }\nreturn f()").Contains("variable inconnue `x`"));
	EXPECT_TRUE(EvalError("if true { const k = 1 }\nreturn k").Contains("variable inconnue `k`"));
	// Un bloc masque sans écraser.
	EXPECT_EQ(Number("let a = 1\nif true { let a = 2 }\nreturn a"), 1.0);
	// `var` ne sort pas d'une fonction.
	EXPECT_TRUE(EvalError("fn f() { var inner = 1 }\nf()\nreturn inner").Contains("variable inconnue `inner`"));
}

TEST(ScriptScope, OnlyConstRefusesReassignment) {
	EXPECT_EQ(Number("var a = 1\na = 2\nreturn a"), 2.0);
	EXPECT_EQ(Number("let b = 1\nb += 4\nreturn b"), 5.0);
	// Détectée DÈS la compilation, avec la ligne de la déclaration.
	const String error = EvalError("const c = 1\n\nc = 2");
	EXPECT_TRUE(error.Contains("`c` est une constante (déclarée ligne 1)"));
	EXPECT_TRUE(error.StartsWith("3:"));
	EXPECT_TRUE(EvalError("const c = 1\nc += 1").Contains("constante"));
	// Et à l'exécution pour ce que l'analyse ne peut pas voir (fermeture).
	EXPECT_TRUE(EvalError("const k = 1\nfn f() { k = 2 }\nf()").Contains("constante"));
	// Une constante qui désigne une table : la LIAISON est figée, pas le contenu.
	EXPECT_EQ(Number("const t = {n: 1}\nt.n = 5\nreturn t.n"), 5.0);
	// `const` doit être initialisée.
	EXPECT_TRUE(EvalError("const z").Contains("doit être initialisée"));
}

TEST(ScriptScope, RedeclarationRulesFollowTheTable) {
	EXPECT_EQ(Number("var a = 1\nvar a = 2\nreturn a"), 2.0);  // var : permis
	EXPECT_EQ(Number("var a = 1\nvar a\nreturn a"), 1.0);      // sans valeur, il garde la sienne
	EXPECT_TRUE(EvalError("let a = 1\nlet a = 2").Contains("déjà déclarée dans cette portée (ligne 1)"));
	EXPECT_TRUE(EvalError("const a = 1\nconst a = 2").Contains("déjà déclarée"));
	EXPECT_TRUE(EvalError("let a = 1\nvar a = 2").Contains("déjà déclarée"));
	EXPECT_TRUE(EvalError("var a = 1\nlet a = 2").Contains("déjà déclarée"));
	// Un `var` d'un bloc heurte le `let` de la fonction qu'il traverse.
	EXPECT_TRUE(EvalError("fn f() { let x = 1\n if true { var x = 2 } }").Contains("déjà déclarée"));
	EXPECT_TRUE(EvalError("fn f(p) { let p = 1 }").Contains("déjà déclarée"));
	EXPECT_TRUE(EvalError("fn f(p, p) { }").Contains("en double"));
	// Dans des portées DIFFÉRENTES, aucun conflit.
	EXPECT_EQ(Number("let a = 1\nfn f() { let a = 2\n return a }\nreturn f() + a"), 3.0);
}

TEST(ScriptScope, HoistingGivesVarNilAndLetAConstDeadZone) {
	// `var` hissé : lisible avant sa ligne, et vaut nil.
	EXPECT_TRUE(Eval("fn f() { let before = v\n var v = 3\n return before }\nreturn f()").IsNil());
	// `let`/`const` hissés NON initialisés : zone morte.
	EXPECT_TRUE(EvalError("fn f() { let r = w\n let w = 1 }\nf()").Contains("avant sa déclaration"));
	EXPECT_TRUE(EvalError("fn f() { k = 2\n const k = 1 }\nf()").Contains("avant sa déclaration"));
	// Hissé, donc il MASQUE déjà la variable englobante : pas de lecture
	// silencieuse de l'autre `x`.
	EXPECT_TRUE(EvalError("let x = 1\nif true { let y = x\n let x = 2 }").Contains("avant sa déclaration"));
	// Une fonction est hissée ENTIÈRE : appelable avant sa ligne.
	EXPECT_EQ(Number("return later()\nfn later() { return 42 }"), 42.0);
	// Une fermeture peut viser un `let` déclaré plus loin, s'il l'est à l'appel.
	EXPECT_EQ(Number("fn g() { return late }\nlet late = 9\nreturn g()"), 9.0);
}

TEST(ScriptScope, EachLoopTurnGetsFreshBindings) {
	// Chaque tour a sa propre variable : les fermetures ne voient pas toutes
	// la dernière valeur.
	EXPECT_EQ(Number("let fs = []\nfor i in [1, 2, 3] { push(fs, fn() { return i }) }\nreturn fs[0]() + fs[2]()"), 4.0);
	// `let` dans le corps : une nouvelle liaison à chaque tour, pas une redéclaration.
	EXPECT_EQ(Number("let total = 0\nfor i in [1, 2, 3] { let doubled = i * 2\n total += doubled }\nreturn total"), 12.0);
	EXPECT_EQ(Number("var n = 0\nwhile n < 3 { const step = 1\n n += step }\nreturn n"), 3.0);
}

TEST(ScriptScope, AConsoleMayReRunTheSameScript) {
	// La console de l'éditeur rejoue un même extrait dans le même
	// interpréteur : ses `let`/`const` de premier niveau se redéclarent.
	Interpreter vm;
	ASSERT_TRUE(vm.Run(StringView("let count = 1\nconst step = 2")).IsOk());
	auto again = vm.Run(StringView("let count = 5\nconst step = 3\nreturn count + step"));
	ASSERT_TRUE(again.IsOk());
	EXPECT_EQ(again.Value().AsNumber(), 8.0);
	// Mais pas ce que l'application a posé.
	vm.SetGlobal(String("shot_dir"), Value::Str(String("/tmp")));
	auto clash = vm.Run(StringView("let shot_dir = 1"));
	ASSERT_TRUE(clash.IsError());
	EXPECT_TRUE(clash.Error().message.Contains("réservé"));
	EXPECT_TRUE(vm.Run(StringView("shot_dir = \"/autre\"")).IsOk()); // la réaffecter reste permis
}

// ============================================================================
// Espaces de noms
// ============================================================================

TEST(ScriptNamespace, MathIsANamespaceOfFunctionsAndConstants) {
	EXPECT_EQ(Number("return math.floor(math.pi * 100)"), 314.0);
	EXPECT_EQ(Number("return math.clamp(7, 0, 5) + math.max(1, 9, 3)"), 14.0);
	EXPECT_TRUE(Eval("return type(math)").AsString() == "namespace");
	EXPECT_TRUE(Eval("return has(math, \"tau\") and not has(math, \"nope\")").IsTruthy());
	EXPECT_EQ(Number("return physics.c"), 299792458.0);
	// Les anciens globaux n'encombrent plus l'espace global.
	EXPECT_TRUE(EvalError("return sin(1)").Contains("variable inconnue `sin`"));
	EXPECT_TRUE(EvalError("return pi()").Contains("variable inconnue `pi`"));
	// Un nom local court ne heurte plus une constante : `e`, `c`, `g`…
	EXPECT_EQ(Number("let e = 2\nlet c = 3\nreturn e * c"), 6.0);
	// Membres inconnus : une erreur qui dit où chercher, pas un nil muet.
	EXPECT_TRUE(EvalError("return math.nope").Contains("l'espace de noms `math` n'a pas de membre `nope`"));
}

TEST(ScriptNamespace, NamespacesAreReadOnlyFromOutside) {
	EXPECT_TRUE(EvalError("math.pi = 3").Contains("espace de noms"));
	EXPECT_TRUE(EvalError("math[\"sin\"] = 1").Contains("espace de noms"));
	EXPECT_TRUE(EvalError("math = 1").Contains("espace de noms"));
	EXPECT_TRUE(EvalError("let math = 1").Contains("réservé"));
}

TEST(ScriptNamespace, UserNamespacesGroupDeclarationsWithLiveMembers) {
	const char *source = "namespace geo {\n"
						 "    const unit = 2\n"
						 "    let calls = 0\n"
						 "    fn area(r) {\n"
						 "        calls += 1\n"
						 "        return square(r) * unit\n" // appel non qualifié : même portée
						 "    }\n"
						 "    fn square(v) { return v * v }\n"   // hissée : utilisable plus haut
						 "}\n"
						 "let a = geo.area(3)\n"
						 "geo.area(1)\n"
						 "return [a, geo.calls, geo.unit]";
	Value result = Eval(source);
	ASSERT_TRUE(result.IsList());
	EXPECT_EQ(result.AsList()->items[0].AsNumber(), 18.0);
	EXPECT_EQ(result.AsList()->items[1].AsNumber(), 2.0); // liaison VIVANTE
	EXPECT_EQ(result.AsList()->items[2].AsNumber(), 2.0);
	// Modifiable de l'intérieur seulement.
	EXPECT_TRUE(EvalError("namespace n { let v = 1 }\nn.v = 2").Contains("espace de noms"));
	// Un `var` reste DANS son espace de noms.
	EXPECT_TRUE(EvalError("namespace n { var hidden = 1 }\nreturn hidden").Contains("variable inconnue"));
	// Rouvrir un espace de noms l'étend.
	EXPECT_EQ(Number("namespace n { const a = 1 }\nnamespace n { const b = 2 }\nreturn n.a + n.b"), 3.0);
	// Imbriqués.
	EXPECT_EQ(Number("namespace outer { namespace inner { const x = 5 } }\nreturn outer.inner.x"), 5.0);
	// Pas de flot de contrôle au niveau d'un espace de noms.
	EXPECT_TRUE(EvalError("namespace n { return 1 }").Contains("interdit"));
	EXPECT_TRUE(Eval("namespace n { const a = 1 }\nreturn keys(n)").AsList()->items.size() == 1u);
}

TEST(ScriptNamespace, HostNamespacesExposeTheApplicationApi) {
	Interpreter vm;
	vm.RegisterNamespacedNative(String("game"), String("score"), 0, 0,
								[](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
									return Ok(Value::Number(12.0));
								});
	vm.RegisterNamespaceConstant(String("game"), String("version"), Value::Number(3.0));
	auto result = vm.Run(StringView("return game.score() + game.version"));
	ASSERT_TRUE(result.IsOk());
	EXPECT_EQ(result.Value().AsNumber(), 15.0);
	EXPECT_TRUE(vm.GetNamespaceMember(String("game"), String("version")).IsSome());
	EXPECT_TRUE(vm.GetNamespaceMember(String("game"), String("absent")).IsNone());
	EXPECT_TRUE(vm.Run(StringView("game.score = nil")).IsError()); // l'API ne s'écrase pas
}

int main() { return RUN_ALL_TESTS(); }
