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

#include <chrono>
#include <memory>
#include <thread>

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
	EXPECT_EQ(Number("let flag = true\nlet r = 0\nif (flag) { r = 7 }\nreturn r"), 7.0);
	// ... et une table reste écrivable entre parenthèses dans une condition.
	EXPECT_EQ(Number("if (({\"a\": 1})[\"a\"] == 1) { return 5 }\nreturn 0"), 5.0);
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
						 "    if (n < 0) { return \"negatif\" }\n"
						 "    else if (n == 0) { return \"zero\" }\n"
						 "    else { return \"positif\" }\n"
						 "}\n"
						 "return classify(-3) .. \",\" .. classify(0) .. \",\" .. classify(9)";
	EXPECT_EQ(Eval(source).AsString(), "negatif,zero,positif");
}

TEST(ScriptInterpreter, WhileWithBreakAndContinue) {
	const char *source = "let i = 0\n"
						 "let sum = 0\n"
						 "while (true) {\n"
						 "    i += 1\n"
						 "    if (i > 10) { break }\n"
						 "    if (i % 2 == 0) { continue }\n"
						 "    sum += i\n"
						 "}\n"
						 "return sum"; // 1+3+5+7+9
	EXPECT_EQ(Number(source), 25.0);
}

TEST(ScriptInterpreter, ForInOverListMapAndString) {
	EXPECT_EQ(Number("let s = 0\nfor (v in [1, 2, 3, 4]) { s += v }\nreturn s"), 10.0);
	// Table littérale itérée : parenthèses obligatoires (cf. ParsePrimary —
	// dans un `for ... in`, `{` ouvre le bloc).
	EXPECT_EQ(Eval("let out = \"\"\nfor (k in ({a: 1, b: 2})) { out = out .. k }\nreturn out").AsString(), "ab");
	// Entre les parenthèses de l'en-tête, une table littérale est permise.
	EXPECT_EQ(Number("let n = 0\nfor (k in {a: 1, b: 2}) { n += 1 }\nreturn n"), 2.0);
	EXPECT_EQ(Number("let n = 0\nfor (c in \"abcde\") { n += 1 }\nreturn n"), 5.0);
}

TEST(ScriptInterpreter, ForInSnapshotsTheSequence) {
	// Le corps modifie la liste itérée : l'itération doit rester bornée par
	// l'état INITIAL (matérialisation avant boucle) et ne jamais boucler.
	const char *source = "let items = [1, 2, 3]\n"
						 "let count = 0\n"
						 "for (v in items) { count += 1; push(items, v) }\n"
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
	EXPECT_EQ(Number("if (0) { return 1 }\nreturn 0"), 1.0);
	EXPECT_EQ(Number("if (\"\") { return 1 }\nreturn 0"), 1.0);
}

// ============================================================================
// Interpréteur — fonctions et fermetures
// ============================================================================

TEST(ScriptInterpreter, RecursionAndArgumentPassing) {
	const char *source = "fn fact(n) {\n"
						 "    if (n <= 1) { return 1 }\n"
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
	auto result = vm.Run(StringView("while (true) { }"));
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
	EXPECT_EQ(Number("let s = 0\nfor (v in range(1, 5)) { s += v }\nreturn s"), 10.0);
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

	auto result = vm.Run(StringView("for (i in range(0, 4)) { push(out, i * i) }"));
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
	auto result = vm.Run(StringView("return read_file(\"/chemin/qui/n/existe/pas.script\")"));
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
	EXPECT_EQ(Number("fn f() { if (true) { var x = 7 } return x }\nreturn f()"), 7.0);
	EXPECT_EQ(Number("if (true) { var y = 3 }\nreturn y"), 3.0); // … ou globale
	// … `let` et `const`, non.
	EXPECT_TRUE(EvalError("fn f() { if (true) { let x = 7 } return x }\nreturn f()").Contains("variable inconnue `x`"));
	EXPECT_TRUE(EvalError("if (true) { const k = 1 }\nreturn k").Contains("variable inconnue `k`"));
	// Un bloc masque sans écraser.
	EXPECT_EQ(Number("let a = 1\nif (true) { let a = 2 }\nreturn a"), 1.0);
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
	EXPECT_TRUE(EvalError("fn f() { let x = 1\n if (true) { var x = 2 } }").Contains("déjà déclarée"));
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
	EXPECT_TRUE(EvalError("let x = 1\nif (true) { let y = x\n let x = 2 }").Contains("avant sa déclaration"));
	// Une fonction est hissée ENTIÈRE : appelable avant sa ligne.
	EXPECT_EQ(Number("return later()\nfn later() { return 42 }"), 42.0);
	// Une fermeture peut viser un `let` déclaré plus loin, s'il l'est à l'appel.
	EXPECT_EQ(Number("fn g() { return late }\nlet late = 9\nreturn g()"), 9.0);
}

TEST(ScriptScope, EachLoopTurnGetsFreshBindings) {
	// Chaque tour a sa propre variable : les fermetures ne voient pas toutes
	// la dernière valeur.
	EXPECT_EQ(Number("let fs = []\nfor (i in [1, 2, 3]) { push(fs, fn() { return i }) }\nreturn fs[0]() + fs[2]()"), 4.0);
	// `let` dans le corps : une nouvelle liaison à chaque tour, pas une redéclaration.
	EXPECT_EQ(Number("let total = 0\nfor (i in [1, 2, 3]) { let doubled = i * 2\n total += doubled }\nreturn total"), 12.0);
	EXPECT_EQ(Number("var n = 0\nwhile (n < 3) { const step = 1\n n += step }\nreturn n"), 3.0);
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

// ─────────────────────────────────────────────────────────────────────────
// Programmation objet : classes, héritage, interfaces, abstract, factory
// ─────────────────────────────────────────────────────────────────────────

TEST(ScriptClass, FieldsConstructorAndMethods) {
	const char *source = "class Compteur {\n"
						 "    let valeur = 0\n"
						 "    const pas = 1\n"
						 "    fn init(départ) { this.valeur = départ }\n"
						 "    fn avance() { this.valeur += this.pas; return this }\n"
						 "}\n"
						 "let c = new Compteur(10)\n"
						 "c.avance().avance()\n"
						 "let d = Compteur(1)\n" // `new` facultatif
						 "return [c.valeur, d.valeur, c is Compteur, type(c), type(Compteur)]";
	Value result = Eval(source);
	ASSERT_TRUE(result.IsList());
	const auto &items = result.AsList()->items;
	EXPECT_EQ(items[0].AsNumber(), 12.0);
	EXPECT_EQ(items[1].AsNumber(), 1.0);
	EXPECT_TRUE(items[2].AsBoolean());
	EXPECT_EQ(items[3].AsString(), "instance");
	EXPECT_EQ(items[4].AsString(), "class");
	// Deux instances ne partagent pas leurs champs.
	EXPECT_EQ(Number("class P { let l = [] }\nlet a = P()\nlet b = P()\npush(a.l, 1)\nreturn len(b.l)"), 0.0);
	// Un champ `const` ne se réaffecte pas ; un champ nouveau se crée.
	EXPECT_TRUE(EvalError("class P { const n = 1 }\nlet p = P()\np.n = 2").Contains("constant"));
	EXPECT_EQ(Number("class P { }\nlet p = P()\np.extra = 4\nreturn p.extra"), 4.0);
	// Membre inconnu : erreur (pas un `nil` silencieux).
	EXPECT_TRUE(EvalError("class P { }\nreturn P().absent").Contains("ni champ ni méthode"));
	// Arguments sans constructeur.
	EXPECT_TRUE(EvalError("class P { }\nP(1)").Contains("constructeur"));
}

TEST(ScriptClass, InheritanceOverrideAndSuper) {
	const char *source = "class Animal {\n"
						 "    let nom = \"?\"\n"
						 "    fn init(nom) { this.nom = nom }\n"
						 "    fn cri() { return \"...\" }\n"
						 "    fn présente() { return this.nom .. \" : \" .. this.cri() }\n"
						 "}\n"
						 "class Chien extends Animal {\n"
						 "    fn init(nom) { super(nom .. \" le chien\") }\n"
						 "    override fn cri() { return \"wouf\" }\n"
						 "}\n"
						 "class Chiot extends Chien {\n"
						 "    override fn cri() { return super.cri() .. \" (petit)\" }\n"
						 "}\n"
						 "let c = Chiot(\"Rex\")\n"
						 "return [c.présente(), c is Chiot, c is Chien, c is Animal, Animal(\"a\") is Chien]";
	Value result = Eval(source);
	ASSERT_TRUE(result.IsList());
	const auto &items = result.AsList()->items;
	// Liaison dynamique (this.cri() appelle la version la plus dérivée) et
	// `super` part de la classe qui DÉFINIT la méthode (pas de récursion).
	EXPECT_EQ(items[0].AsString(), "Rex le chien : wouf (petit)");
	EXPECT_TRUE(items[1].AsBoolean() && items[2].AsBoolean() && items[3].AsBoolean());
	EXPECT_FALSE(items[4].AsBoolean());
	// `override` qui ne remplace rien.
	EXPECT_TRUE(EvalError("class A { }\nclass B extends A { override fn f() { } }").Contains("override"));
	// `super` hors d'une classe dérivée : refusé dès la compilation.
	EXPECT_TRUE(EvalError("class A { fn f() { return super.f() } }").Contains("super"));
	EXPECT_TRUE(EvalError("fn f() { return this }").Contains("this"));
	// Hériter d'autre chose qu'une classe.
	EXPECT_TRUE(EvalError("let A = 1\nclass B extends A { }").Contains("classe"));
}

TEST(ScriptClass, AbstractClassesAndInterfaces) {
	const char *source = "interface Forme {\n"
						 "    fn aire()\n"
						 "    fn nom()\n"
						 "}\n"
						 "interface Solide extends Forme { fn volume(h) }\n"
						 "abstract class Base implements Forme {\n"
						 "    fn nom() { return \"forme\" }\n"
						 "    abstract fn aire()\n"
						 "    fn décrit() { return this.nom() .. \" \" .. this.aire() }\n"
						 "}\n"
						 "class Carré extends Base implements Solide {\n"
						 "    let c = 0\n"
						 "    fn init(c) { this.c = c }\n"
						 "    override fn aire() { return this.c * this.c }\n"
						 "    fn volume(h) { return this.aire() * h }\n"
						 "}\n"
						 "let q = Carré(3)\n"
						 "return [q.décrit(), q.volume(2), q is Forme, q is Solide, q is Base]";
	Value result = Eval(source);
	ASSERT_TRUE(result.IsList());
	const auto &items = result.AsList()->items;
	EXPECT_EQ(items[0].AsString(), "forme 9");
	EXPECT_EQ(items[1].AsNumber(), 18.0);
	EXPECT_TRUE(items[2].AsBoolean() && items[3].AsBoolean() && items[4].AsBoolean());

	// Ni classe abstraite ni interface ne s'instancient.
	EXPECT_TRUE(EvalError("abstract class A { }\nA()").Contains("abstraite"));
	EXPECT_TRUE(EvalError("interface I { fn f() }\nI()").Contains("interface"));
	// Classe concrète incomplète : refusée à sa DÉCLARATION.
	EXPECT_TRUE(EvalError("abstract class A { abstract fn f() }\nclass B extends A { }").Contains("doit implémenter"));
	EXPECT_TRUE(EvalError("interface I { fn f(a) }\nclass B implements I { }").Contains("doit implémenter"));
	// Signature différente de celle de l'interface.
	EXPECT_TRUE(EvalError("interface I { fn f(a, b) }\nclass B implements I { fn f(a) { } }").Contains("paramètre"));
	// Méthode abstraite dans une classe concrète : erreur de compilation.
	EXPECT_TRUE(EvalError("class A { abstract fn f() }").Contains("abstract class"));
	// `implements` une classe / `extends` une interface.
	EXPECT_TRUE(EvalError("class A { }\nclass B implements A { }").Contains("extends"));
	EXPECT_TRUE(EvalError("interface I { }\nclass B extends I { }").Contains("implements"));
}

TEST(ScriptClass, StaticMembersAndFactories) {
	const char *source = "class Point {\n"
						 "    static let créés = 0\n"
						 "    static const ORIGINE_X = 0\n"
						 "    let x = 0\n"
						 "    let y = 0\n"
						 "    fn init(x, y) { this.x = x; this.y = y; Point.créés += 1 }\n"
						 "    static fn somme(a, b) { return Point(a.x + b.x, a.y + b.y) }\n"
						 "    factory fn origine() { return new Point(Point.ORIGINE_X, 0) }\n"
						 "    factory fn cassé() { return 42 }\n"
						 "}\n"
						 "let p = Point.somme(Point(1, 2), Point.origine())\n"
						 "return [p.x, p.y, Point.créés, Point.name, str(p)]";
	Value result = Eval(source);
	ASSERT_TRUE(result.IsList());
	const auto &items = result.AsList()->items;
	EXPECT_EQ(items[0].AsNumber(), 1.0);
	EXPECT_EQ(items[1].AsNumber(), 2.0);
	EXPECT_EQ(items[2].AsNumber(), 3.0);
	EXPECT_EQ(items[3].AsString(), "Point");
	EXPECT_EQ(items[4].AsString(), "Point{x: 1, y: 2}");
	// Un constructeur `factory` qui ne rend pas une instance : erreur.
	EXPECT_TRUE(EvalError("class P { factory fn f() { return 1 } }\nP.f()").Contains("factory"));
	// Champ de classe constant.
	EXPECT_TRUE(EvalError("class P { static const K = 1 }\nP.K = 2").Contains("constant"));
	// `this` interdit dans une méthode de classe.
	EXPECT_TRUE(EvalError("class P { static fn f() { return this } }").Contains("this"));
}

TEST(ScriptClass, ClassesFollowScopingRules) {
	// Constante : pas de réaffectation, zone morte avant la déclaration.
	EXPECT_TRUE(EvalError("class A { }\nA = 1").Contains("constante"));
	EXPECT_TRUE(EvalError("let a = A()\nclass A { }").Contains("zone morte"));
	// Dans un espace de noms, et désignée par son chemin.
	EXPECT_EQ(Number("namespace geo { class Forme { fn aire() { return 2 } } }\n"
					 "class Carré extends geo.Forme { }\n"
					 "return new geo.Forme().aire() + Carré().aire()"),
			  4.0);
	// Une fermeture capturée dans une méthode garde son `this`.
	EXPECT_EQ(Number("class B { let v = 5\n fn getter() { return fn() { return this.v } } }\n"
					 "let g = B().getter()\nreturn g()"),
			  5.0);
	// keys / has / JSON d'un objet.
	EXPECT_EQ(Number("class P { let a = 1\n let b = 2\n fn m() { } }\n"
					 "let p = P()\nreturn len(keys(p)) + (has(p, \"m\") and 10 or 0)"),
			  12.0);
	// Mots réservés : interdits comme noms de variables, permis comme noms
	// de champs et clés de table (leur place les désambiguïse).
	EXPECT_TRUE(EvalError("let static = 1").Contains("nom de variable"));
	EXPECT_EQ(Number("let t = {class: 3, new: 4, static: 1}\nreturn t.class + t.new + t.static"), 8.0);
}

TEST(ScriptClass, CheckedCastWithAs) {
	const char *source = "interface Nommé { fn nom() }\n"
						 "class Chat implements Nommé { fn nom() { return \"chat\" } }\n"
						 "class Pierre { }\n"
						 "let bête = Chat() as Nommé\n"
						 "return bête.nom()";
	EXPECT_EQ(Eval(source).AsString(), "chat");
	EXPECT_TRUE(EvalError("class A { }\nclass B { }\nreturn A() as B").Contains("transtypage"));
	EXPECT_TRUE(EvalError("return 1 as 2").Contains("classe"));
}

// ─────────────────────────────────────────────────────────────────────────
// Exécution asynchrone : async / await / Future, un fil par appel
// ─────────────────────────────────────────────────────────────────────────

TEST(ScriptAsync, AsyncFunctionsReturnFuturesThatAwaitResolves) {
	const char *source = "async fn carré(x) { sleep(5)\n return x * x }\n"
						 "let f = carré(7)\n"
						 "let avant = f.done\n"      // lancé sur un autre fil : pas encore fini
						 "let v = await f\n"
						 "return [type(f), avant, v, f.done, f.value]";
	Value result = Eval(source);
	ASSERT_TRUE(result.IsList());
	const auto &items = result.AsList()->items;
	EXPECT_EQ(items[0].AsString(), "future");
	EXPECT_FALSE(items[1].AsBoolean());
	EXPECT_EQ(items[2].AsNumber(), 49.0);
	EXPECT_TRUE(items[3].AsBoolean());
	EXPECT_EQ(items[4].AsNumber(), 49.0);
	// `await` sur une valeur ordinaire la rend telle quelle (comme Dart).
	EXPECT_EQ(Number("return await 3"), 3.0);
	// `await` hors d'une fonction `async` : refusé à la compilation.
	EXPECT_TRUE(EvalError("fn f() { return await 1 }").Contains("async"));
	// Lambdas et méthodes async.
	EXPECT_EQ(Number("let f = async fn(a) { return a + 1 }\nreturn await f(1)"), 2.0);
	EXPECT_EQ(Number("class C { let n = 4\n async fn double() { return this.n * 2 } }\nreturn await C().double()"), 8.0);
}

TEST(ScriptAsync, SharedVariablesAreProtectedAcrossThreads) {
	// Quatre fils incrémentent la même variable, la même liste, le même champ
	// d'objet et la même clé de table : aucune mise à jour ne se perd.
	const char *source = "var total = 0\n"
						 "let journal = []\n"
						 "class Compteur { let n = 0 }\n"
						 "let objet = Compteur()\n"
						 "let table = {n: 0}\n"
						 "async fn travaille(k) {\n"
						 "    for (i in range(0, 500)) {\n"
						 "        total += 1\n"
						 "        objet.n += 1\n"
						 "        table.n += 1\n"
						 "    }\n"
						 "    push(journal, k)\n"
						 "    return k\n"
						 "}\n"
						 "let tâches = [travaille(1), travaille(2), travaille(3), travaille(4)]\n"
						 "let résultats = await future.all(tâches)\n"
						 "return [total, objet.n, table.n, len(journal), résultats[3]]";
	Value result = Eval(source);
	ASSERT_TRUE(result.IsList());
	const auto &items = result.AsList()->items;
	EXPECT_EQ(items[0].AsNumber(), 2000.0);
	EXPECT_EQ(items[1].AsNumber(), 2000.0);
	EXPECT_EQ(items[2].AsNumber(), 2000.0);
	EXPECT_EQ(items[3].AsNumber(), 4.0);
	EXPECT_EQ(items[4].AsNumber(), 4.0);
}

TEST(ScriptAsync, ErrorsPropagateThroughAwaitAndAreNeverSwallowed) {
	EXPECT_TRUE(EvalError("async fn casse() { return 1 / 0 }\nawait casse()").Contains("division par zéro"));
	// `.failed` / `.error` sans lever l'erreur.
	const char *source = "async fn casse() { return nil + 1 }\n"
						 "let f = casse()\n"
						 "f.wait\n"
						 "while (not f.done) { sleep(1) }\n"
						 "return [f.failed, f.error != nil]";
	Value result = Eval(source);
	ASSERT_TRUE(result.IsList());
	EXPECT_TRUE(result.AsList()->items[0].AsBoolean());
	EXPECT_TRUE(result.AsList()->items[1].AsBoolean());

	// Une erreur que personne n'attend est signalée au passage suivant.
	Interpreter vm;
	std::vector<String> reported;
	vm.onAsyncError = [&reported](const ScriptError &error) { reported.push_back(error.message); };
	ASSERT_TRUE(vm.Run(StringView("async fn casse() { return [] + 1 }\ncasse()")).IsOk());
	for (int i = 0; i < 200 && reported.empty(); ++i) {
		vm.PumpMainThread();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	ASSERT_TRUE(reported.size() == 1u);
	EXPECT_TRUE(reported[0].Contains("inapplicable"));
}

TEST(ScriptAsync, ThenAllAndDelayedComposeFutures) {
	const char *source = "async fn lent(x) { sleep(3)\n return x }\n"
						 "let chaîne = lent(2).then(fn(v) { return v * 10 })\n"
						 "let tous = future.all([lent(1), future.value(5), lent(3)])\n"
						 "let tard = future.delayed(5, fn() { return \"fini\" })\n"
						 "return [await chaîne, await tous, await tard]";
	Value result = Eval(source);
	ASSERT_TRUE(result.IsList());
	const auto &items = result.AsList()->items;
	EXPECT_EQ(items[0].AsNumber(), 20.0);
	EXPECT_EQ(items[1].ToDisplayString(), "[1, 5, 3]");
	EXPECT_EQ(items[2].AsString(), "fini");
	EXPECT_TRUE(EvalError("await future.error(\"raté\")").Contains("raté"));
}

TEST(ScriptAsync, HostFunctionsRunOnTheMainThread) {
	Interpreter vm;
	std::vector<std::thread::id> seen;
	vm.RegisterNative(String("hote"), 1, 1, [&seen](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
		seen.push_back(std::this_thread::get_id());
		// Les listes reçues d'un autre fil sont des COPIES.
		if (args[0].IsList())
			args[0].AsList()->items.push_back(Value::Number(99));
		return Ok(Value::Number(double(args[0].IsList() ? args[0].AsList()->items.size() : 0)));
	});
	auto result = vm.Run(StringView("let l = [1, 2]\n"
									"async fn appelle() { return hote(l) }\n"
									"let n = await appelle()\n"
									"return [n, len(l)]"));
	ASSERT_TRUE(result.IsOk());
	ASSERT_TRUE(seen.size() == 1u);
	EXPECT_TRUE(seen[0] == std::this_thread::get_id()); // exécutée sur CE fil (le principal)
	EXPECT_EQ(result.Value().AsList()->items[0].AsNumber(), 3.0);
	EXPECT_EQ(result.Value().AsList()->items[1].AsNumber(), 2.0); // l'original n'a pas bougé

	// `print` depuis un fil async arrive dans la sortie.
	Interpreter printer;
	ASSERT_TRUE(printer.Run(StringView("async fn dit() { print(\"depuis un fil\") }\nawait dit()")).IsOk());
	ASSERT_TRUE(printer.Output().size() == 1u);
	EXPECT_EQ(printer.Output()[0], "depuis un fil");
}

TEST(ScriptAsync, DestroyingTheInterpreterStopsRunningTasks) {
	auto vm = std::make_unique<Interpreter>();
	ASSERT_TRUE(vm->Run(StringView("async fn sans_fin() { while (true) { } }\nsans_fin()\nsans_fin()")).IsOk());
	EXPECT_EQ(vm->RunningTasks(), size_t(2));
	vm.reset(); // doit rendre la main : les boucles s'arrêtent, les fils sont joints
	EXPECT_TRUE(true);
}

// ─────────────────────────────────────────────────────────────────────────
// Syntaxe : parenthèses obligatoires, do … while, types annoncés
// ─────────────────────────────────────────────────────────────────────────

TEST(ScriptSyntax, ControlStructuresRequireParentheses) {
	EXPECT_TRUE(EvalError("if true { }").Contains("parenthèses"));
	EXPECT_TRUE(EvalError("while false { }").Contains("parenthèses"));
	EXPECT_TRUE(EvalError("for i in [1] { }").Contains("for (x in liste)"));
	EXPECT_TRUE(EvalError("do { } while true").Contains("parenthèses"));
	// Une table littérale entre les parenthèses d'une condition.
	EXPECT_EQ(Number("if ({a: 1}.a == 1) { return 3 }\nreturn 0"), 3.0);
}

TEST(ScriptSyntax, DoWhileRunsItsBodyAtLeastOnce) {
	EXPECT_EQ(Number("let n = 0\ndo { n += 1 } while (false)\nreturn n"), 1.0);
	EXPECT_EQ(Number("let n = 0\ndo { n += 1 } while (n < 5)\nreturn n"), 5.0);
	// `continue` passe par le test ; `break` sort.
	EXPECT_EQ(Number("let n = 0\nlet pairs = 0\ndo {\n n += 1\n if (n % 2 == 1) { continue }\n pairs += 1\n} while (n < 6)\nreturn pairs"),
			  3.0);
	EXPECT_EQ(Number("let n = 0\ndo { n += 1\n if (n == 3) { break } } while (true)\nreturn n"), 3.0);
}

TEST(ScriptTypes, TheExampleOfTheRequest) {
	const char *source = "fn create_list(length:u64):list<i32> {\n"
						 "    let array:list<i32>\n"
						 "    for (i in range(0, length)) {\n"
						 "        array.append(i)\n"
						 "    }\n"
						 "    return array\n"
						 "}\n"
						 "return create_list(8)";
	EXPECT_EQ(Eval(source).ToDisplayString(), "[0, 1, 2, 3, 4, 5, 6, 7]");
	// Le paramètre est vérifié : u64 n'accepte ni négatif ni fraction.
	EXPECT_TRUE(EvalError("fn f(n:u64) { }\nf(-1)").Contains("hors des bornes de u64"));
	EXPECT_TRUE(EvalError("fn f(n:u64) { }\nf(1.5)").Contains("entier"));
	// Le résultat aussi : list<i32> refuse une liste de chaînes.
	EXPECT_TRUE(EvalError("fn f():list<i32> { return [\"a\"] }\nf()").Contains("doit rendre `list<i32>`"));
}

TEST(ScriptTypes, VariablesKeepTheirDeclaredType) {
	// Valeurs par défaut d'une variable typée sans valeur.
	EXPECT_EQ(Eval("let a:i32\nlet b:f64\nlet s:string\nlet f:bool\nlet l:list<u8>\nlet m:map<f32>\nlet o:i32?\n"
				   "return [a, b, s, f, len(l), len(m), o]")
				  .ToDisplayString(),
			  "[0, 0, , false, 0, 0, nil]");
	// Chaque affectation est vérifiée — y compris les formes composées.
	EXPECT_TRUE(EvalError("let x:u8 = 250\nx += 10").Contains("hors des bornes de u8"));
	EXPECT_TRUE(EvalError("let x:i32 = 1\nx = \"texte\"").Contains("attendu i32"));
	EXPECT_TRUE(EvalError("let x:i16 = 1\nx = 2.5").Contains("entier"));
	EXPECT_TRUE(EvalError("let s:string = 3").Contains("attendu string"));
	EXPECT_EQ(Number("let x:f32 = 1\nx = 2.5\nreturn x"), 2.5);
	EXPECT_EQ(Number("var v:i64 = 3\nv *= 4\nreturn v"), 12.0);
	// `?` : nil permis.
	EXPECT_EQ(Eval("let x:i32? = nil\nreturn x").IsNil(), true);
	EXPECT_TRUE(EvalError("let x:i32 = nil").Contains("attendu i32"));
	// Types de classes et d'interfaces.
	EXPECT_EQ(Number("interface Forme { fn aire() }\nclass Carré implements Forme { fn aire() { return 4 } }\n"
					 "let f:Forme = Carré()\nreturn f.aire()"),
			  4.0);
	EXPECT_TRUE(EvalError("class A { }\nclass B { }\nlet a:A = B()").Contains("objet `A`"));
	EXPECT_TRUE(EvalError("let x:Inconnu = 1").Contains("type inconnu"));
	// Boucle et champs typés.
	EXPECT_TRUE(EvalError("for (i:u8 in [1, 300]) { }").Contains("hors des bornes de u8"));
	EXPECT_TRUE(EvalError("class P { let n:i32 = 0 }\nlet p = P()\np.n = \"x\"").Contains("attendu i32"));
	EXPECT_EQ(Number("class P { let n:i32\n let l:list<f64> }\nlet p = P()\np.l.append(1.5)\nreturn p.n + p.l[0]"), 1.5);
	// map<string, V> : les clés sont des chaînes, seul V est vérifié.
	EXPECT_TRUE(EvalError("let m:map<string, i32> = {a: 1, b: \"x\"}").Contains("clé `b`"));
}

TEST(ScriptTypes, NumericConversionsReplaceInt) {
	EXPECT_EQ(Number("return i32(3.9)"), 3.0);
	EXPECT_EQ(Number("return i32(-3.9)"), -3.0);
	EXPECT_EQ(Number("return u8(300)"), 44.0);   // repliement, comme un transtypage C
	EXPECT_EQ(Number("return i8(200)"), -56.0);
	EXPECT_EQ(Number("return u16(-1)"), 65535.0);
	EXPECT_EQ(Number("return i64(\"42\")"), 42.0);
	EXPECT_EQ(Number("return f64(true)"), 1.0);
	EXPECT_EQ(Number("return f32(0.1)"), double(0.1f));
	EXPECT_TRUE(EvalError("return i32(\"abc\")").Contains("conversion impossible"));
	EXPECT_TRUE(EvalError("return int(3)").Contains("variable inconnue `int`"));
}

TEST(ScriptTypes, ListsAndStringsHaveMethods) {
	EXPECT_EQ(Eval("let l = [3, 1, 2]\nl.append(5)\nl.sort()\nreturn [l, l.len(), l.contains(2), l.index_of(5), l.pop(), l]")
				  .ToDisplayString(),
			  "[[1, 2, 3], 4, true, 3, 5, [1, 2, 3]]");
	EXPECT_EQ(Eval("let s = \"  Bonjour  \"\nreturn [s.trim().upper(), s.len(), s.contains(\"jour\"), \"a,b\".split(\",\")]")
				  .ToDisplayString(),
			  "[BONJOUR, 11, true, [a, b]]");
	EXPECT_TRUE(Eval("return [].is_empty()").AsBoolean());
	EXPECT_TRUE(EvalError("return [1].voler()").Contains("pas de méthode `voler`"));
}

/// Type (i8 … f64) et texte d'une valeur : `"u8 44"`.
String Typed(const char *source) {
	const Value v = Eval(source);
	return String(v.TypeName()) + String(" ") + v.ToDisplayString();
}

TEST(ScriptNumbers, LiteralsAreI64OrF64) {
	EXPECT_EQ(Typed("return 42"), "i64 42");
	EXPECT_EQ(Typed("return 4.5"), "f64 4.5");
	EXPECT_EQ(Typed("return 1e3"), "f64 1000");
	EXPECT_EQ(Typed("return 18446744073709551615"), "u64 18446744073709551615"); // au-delà d'INT64_MAX
	EXPECT_EQ(Typed("return -9223372036854775808"), "i64 -9223372036854775808");
	EXPECT_TRUE(EvalError("return 18446744073709551616").Contains("trop grand"));
	// Exact sur 64 bits (un double n'a que 53 bits de mantisse).
	EXPECT_EQ(Typed("return 9007199254740993 + 0"), "i64 9007199254740993");
}

TEST(ScriptNumbers, EverySizeHasItsType) {
	EXPECT_EQ(Typed("let x:u8 = 200\nreturn x"), "u8 200");
	EXPECT_EQ(Typed("let x:i16 = -3\nreturn x"), "i16 -3");
	EXPECT_EQ(Typed("let x:u32\nreturn x"), "u32 0");
	EXPECT_EQ(Typed("let x:f32 = 0.1\nreturn x"), "f32 " + Value::NumberToString(double(0.1f)));
	EXPECT_EQ(Typed("let x:f64 = 3\nreturn x"), "f64 3"); // un entier converti en flottant
	EXPECT_EQ(Typed("let x:i32 = 4.0\nreturn x"), "i32 4");  // un flottant entier, sans perte
	EXPECT_TRUE(EvalError("let x:i32 = 4.5").Contains("attendu un entier i32"));
	EXPECT_TRUE(EvalError("let x:u8 = 256").Contains("hors des bornes de u8"));
	EXPECT_TRUE(EvalError("let x:u8 = -1").Contains("hors des bornes de u8"));
	// Chaque affectation convertit (et vérifie) à nouveau.
	EXPECT_EQ(Typed("let x:u16 = 1\nx = 7\nreturn x"), "u16 7");
	EXPECT_EQ(Typed("let x:u16 = 1\nx += 7\nreturn x"), "u16 8");
	// Paramètres, résultat, champs.
	EXPECT_EQ(Typed("fn f(a:i8):u64 { return a * 2 }\nreturn f(5)"), "u64 10");
	EXPECT_EQ(Typed("class P { let x:f32 = 1 }\nreturn new P().x"), "f32 1");
	EXPECT_EQ(Typed("for (i:u8 in range(3)) { return i }"), "u8 0");
}

TEST(ScriptNumbers, ArithmeticKeepsTheWidestTypeAndChecksOverflow) {
	EXPECT_EQ(Typed("return u8(200) + u8(55)"), "u8 255");
	EXPECT_TRUE(EvalError("return u8(200) + u8(56)").Contains("dépassement de capacité : 256 ne tient pas dans u8"));
	EXPECT_TRUE(EvalError("return u8(1) - u8(2)").Contains("ne tient pas dans u8"));
	EXPECT_TRUE(EvalError("return 9223372036854775807 + 1").Contains("ne tient pas dans i64"));
	EXPECT_EQ(Typed("return u8(200) + 100"), "i64 300");              // i64 est plus large
	EXPECT_EQ(Typed("return i16(3) * u32(4)"), "u32 12");            // u32 est plus large
	EXPECT_EQ(Typed("return 18446744073709551615 - 1"), "u64 18446744073709551614"); // signé positif : u64
	EXPECT_EQ(Typed("return u64(5) - 6"), "i64 -1");                 // résultat négatif : i64
	EXPECT_EQ(Typed("return u64(5) - 1"), "u64 4");                  // positif : u64
	EXPECT_EQ(Typed("return -u8(3)"), "i64 -3");
	EXPECT_EQ(Typed("return i32(7) % 3"), "i64 1");
	EXPECT_EQ(Typed("return -7 % 3"), "i64 -1"); // signe du dividende, comme en C
	// `/` rend un flottant ; un flottant rend l'opération flottante.
	EXPECT_EQ(Typed("return 7 / 2"), "f64 3.5");
	EXPECT_EQ(Typed("return f32(1.5) * 2"), "f32 3");
	EXPECT_EQ(Typed("return f32(1.5) + 0.5"), "f64 2");
	EXPECT_TRUE(EvalError("return 1 % 0").Contains("modulo par zéro"));
}

TEST(ScriptNumbers, ComparisonsAndEqualityAreNumeric) {
	EXPECT_TRUE(Eval("return 1 == 1.0").AsBoolean());
	EXPECT_TRUE(Eval("return u8(3) == i64(3)").AsBoolean());
	EXPECT_FALSE(Eval("return i8(-1) == u64(18446744073709551615)").AsBoolean());
	EXPECT_TRUE(Eval("return i8(-1) < u64(0)").AsBoolean());
	EXPECT_TRUE(Eval("return 9007199254740993 > 9007199254740992").AsBoolean()); // exact, pas via double
	EXPECT_EQ(Eval("let m = {}\nm[u8(1)] = \"a\"\nreturn m[1]").ToDisplayString(), "a");
}

TEST(ScriptNumbers, TheStandardLibraryReturnsIntegers) {
	EXPECT_EQ(Typed("return len([1, 2])"), "i64 2");
	EXPECT_EQ(Typed("return range(3)[2]"), "i64 2");
	EXPECT_EQ(Typed("return range(0.5, 2)[1]"), "f64 1.5");
	EXPECT_EQ(Typed("return [1, 2].index_of(2)"), "i64 1");
	EXPECT_EQ(Typed("return num(\"12\")"), "i64 12");
	EXPECT_EQ(Typed("return num(\"1.5\")"), "f64 1.5");
	EXPECT_EQ(Typed("return u64(\"18446744073709551615\")"), "u64 18446744073709551615");
	EXPECT_EQ(Typed("return u8(300)"), "u8 44");
	EXPECT_EQ(Typed("return i64(u64(18446744073709551615))"), "i64 -1"); // repliement exact
	EXPECT_EQ(Typed("return math.abs(-3)"), "i64 3");
	EXPECT_EQ(Typed("return math.abs(-2.5)"), "f64 2.5");
	EXPECT_EQ(Typed("return math.max(u8(3), 2.5)"), "u8 3");
	EXPECT_EQ(Typed("return math.floor(f32(2.5))"), "f32 2");
	EXPECT_EQ(Typed("return math.sqrt(4)"), "f64 2");
}

TEST(ScriptNumbers, JsonKeepsIntegersAndFloatsApart) {
	Interpreter vm;
	InstallDataLibrary(vm);
	auto result = vm.Run(StringView("let doc = parse(\"json\", \"{\\\"a\\\": 3, \\\"b\\\": 3.0}\")\n"
									"return [type(doc.a), type(doc.b), encode(\"json\", {x: u8(3), y: f32(3)})]"));
	ASSERT_TRUE(result.IsOk());
	const String shown = result.Value().ToDisplayString();
	EXPECT_TRUE(shown.StartsWith("[i64, f64, "));
	EXPECT_TRUE(shown.Contains("\"x\": 3,") && shown.Contains("\"y\": 3.0")); // u8 -> entier, f32 -> flottant
	EXPECT_TRUE(NodeFromValue(Value::Int(3, NumberType::U8))->type == data::NodeType::INT);
	EXPECT_TRUE(NodeFromValue(Value::Float(3.0, NumberType::F32))->type == data::NodeType::FLOAT);
}

// ============================================================================
// Opérateurs de flux, surcharge, énumérés, génériques, RAII
// ============================================================================

/// Exécute puis rend les lignes affichées, jointes par `|`.
String Printed(const char *source) {
	Interpreter vm;
	auto result = vm.Run(StringView(source));
	if (result.IsError()) {
		test::ReportFailure(__FILE__, __LINE__, result.Error().Format().CStr());
		return String();
	}
	String out;
	for (const String &line : vm.Output()) {
		if (!out.IsEmpty())
			out.Concat("|");
		out.Concat(line);
	}
	return out;
}

TEST(ScriptFlow, ArrowsChainCallsAndFillSinks) {
	EXPECT_EQ(Number("fn double(x) { return x * 2 }\nfn inc(x) { return x + 1 }\nreturn 5 -> double -> inc"), 11.0);
	EXPECT_EQ(Number("fn double(x) { return x * 2 }\nfn inc(x) { return x + 1 }\nreturn inc <- double <- 5"), 11.0);
	EXPECT_EQ(Eval("let l = []\n1 -> l\nl <- 2\nreturn l").ToDisplayString(), "[1, 2]");
	EXPECT_EQ(Printed("\"bonjour\" -> print"), "bonjour");
	EXPECT_TRUE(EvalError("let x = 3\nreturn x<-1").Contains("x < -1"));
	EXPECT_TRUE(EvalError("return 1 <-> 2").Contains("`<->` relie deux pipes"));
}

TEST(ScriptOperators, ClassesOverloadOperators) {
	const char *vec = "class V {\n"
					  "  let x = 0\n  let y = 0\n"
					  "  fn init(x, y) { this.x = x; this.y = y }\n"
					  "  operator +(o) { return V(this.x + o.x, this.y + o.y) }\n"
					  "  operator -() { return V(-this.x, -this.y) }\n"
					  "  operator ==(o) { return this.x == o.x and this.y == o.y }\n"
					  "  operator <(o) { return this.x < o.x }\n"
					  "  operator [](i) { if (i == 0) { return this.x } return this.y }\n"
					  "  operator []=(i, v) { if (i == 0) { this.x = v } else { this.y = v } }\n"
					  "  operator ()(k) { return this.x * k }\n"
					  "  operator str() { return \"(\" .. this.x .. \", \" .. this.y .. \")\" }\n"
					  "  static operator *(a, b) { if (a is V) { return V(a.x * b, a.y * b) } return V(b.x * a, b.y * a) }\n"
					  "}\n";
	auto run = [&](const char *tail) { return Eval((String(vec) + tail).CStr()).ToDisplayString(); };
	EXPECT_EQ(run("return str(V(1, 2) + V(3, 4))"), "(4, 6)");
	EXPECT_EQ(run("return \"\" + (-V(1, 2))"), "(-1, -2)");
	EXPECT_EQ(run("return [V(1, 2) == V(1, 2), V(1, 2) != V(1, 3), V(1, 0) < V(2, 0), V(3, 0) > V(2, 0), V(2, 0) >= V(2, 0)]"),
			  "[true, true, true, true, true]");
	EXPECT_EQ(run("let v = V(1, 2)\nv[1] = 9\nreturn [v[0], v[1], v(10)]"), "[1, 9, 10]");
	EXPECT_EQ(run("return \"\" .. (2 * V(1, 2)) .. (V(1, 2) * 3)"), "(2, 4)(3, 6)");
	EXPECT_EQ(Printed((String(vec) + "print(V(7, 8))").CStr()), "(7, 8)");
	EXPECT_TRUE(EvalError("class A { operator +(a, b) { return 0 } }").Contains("1 paramètre"));
}

TEST(ScriptOperators, FlowOperatorsCanBeOverloaded) {
	const char *source = "class Tuyau {\n"
						 "  let reçu = []\n"
						 "  operator <-(v) { this.reçu.append(v); return this }\n"
						 "  operator ->(cible) { for (v in this.reçu) { v -> cible } return cible }\n"
						 "  operator <->(autre) { return \"lié\" }\n"
						 "}\n"
						 "let a = Tuyau()\nlet b = Tuyau()\n"
						 "1 -> a\na <- 2\na -> b\n"
						 "return [b.reçu, a <-> b]";
	EXPECT_EQ(Eval(source).ToDisplayString(), "[[1, 2], lié]");
}

TEST(ScriptEnums, EnumsAreSingletonsWithNameAndValue) {
	const char *decl = "enum Couleur { ROUGE, VERT = 5, BLEU; fn chaude() { return this == Couleur.ROUGE } static fn défaut() { return Couleur.BLEU } }\n";
	auto run = [&](const char *tail) { return Eval((String(decl) + tail).CStr()).ToDisplayString(); };
	EXPECT_EQ(run("return [Couleur.ROUGE.value, Couleur.VERT.value, Couleur.BLEU.value, Couleur.BLEU.name]"), "[0, 5, 6, BLEU]");
	EXPECT_EQ(run("return [Couleur.ROUGE, Couleur.from(5), Couleur.from_name(\"BLEU\"), Couleur.from(42)]"),
			  "[Couleur.ROUGE, Couleur.VERT, Couleur.BLEU, nil]");
	EXPECT_EQ(run("return [Couleur.ROUGE.chaude(), Couleur.défaut(), Couleur.ROUGE is Couleur, Couleur.ROUGE < Couleur.BLEU]"),
			  "[true, Couleur.BLEU, true, true]");
	EXPECT_EQ(run("let n = []\nfor (c in Couleur) { n.append(c.name) }\nreturn [n, len(Couleur.values())]"),
			  "[[ROUGE, VERT, BLEU], 3]");
	EXPECT_EQ(run("fn f(c:Couleur) { return c.name }\nreturn f(Couleur.VERT)"), "VERT");
	EXPECT_TRUE(EvalError((String(decl) + "return Couleur()").CStr()).Contains("type énuméré"));
	EXPECT_TRUE(EvalError((String(decl) + "Couleur.ROUGE.value = 3").CStr()).Contains("constant"));
	EXPECT_EQ(Eval("enum E { A = \"a\", B = \"b\" }\nreturn E.B.value").ToDisplayString(), "b");
}

TEST(ScriptGenerics, FunctionsBindOrInferTheirTypeParameters) {
	const char *decl = "fn premier<T>(a:T, b:T):T { return a }\n";
	auto run = [&](const char *tail) { return (String(decl) + tail); };
	EXPECT_EQ(Typed(run("return premier<u8>(3, 4)").CStr()), "u8 3");
	EXPECT_EQ(Typed(run("return premier(3, 4)").CStr()), "i64 3");
	EXPECT_TRUE(EvalError(run("return premier(3, \"x\")").CStr()).Contains("attendu i64"));
	EXPECT_TRUE(EvalError(run("return premier<u8>(300, 4)").CStr()).Contains("hors des bornes de u8"));
	EXPECT_EQ(Eval(run("return premier(\"a\", \"b\")").CStr()).ToDisplayString(), "a");
	// Contrainte `extends`.
	EXPECT_TRUE(EvalError("class F {}\nfn g<T extends F>(x:T) { return x }\nreturn g(3)").Contains("T extends F"));
	// `a < b > (c)` reste une comparaison quand ce n'en est pas un appel générique.
	EXPECT_TRUE(Eval("let a = 1\nlet b = 2\nreturn a < b").AsBoolean());
}

TEST(ScriptGenerics, ClassesCarryTheirTypeArguments) {
	const char *decl = "class Boîte<T> {\n  let valeur:T\n  fn init(v:T) { this.valeur = v }\n  fn prendre():T { return this.valeur }\n}\n"
					   "class Entiers extends Boîte<i32> {}\n";
	auto run = [&](const char *tail) { return (String(decl) + tail); };
	EXPECT_EQ(Typed(run("return new Boîte<u16>(7).prendre()").CStr()), "u16 7");
	EXPECT_TRUE(EvalError(run("let b = new Boîte<u8>(1)\nb.valeur = 999").CStr()).Contains("hors des bornes de u8"));
	EXPECT_TRUE(EvalError(run("let b = Boîte(1)\nb.valeur = \"x\"").CStr()).Contains("attendu i64")); // déduit
	EXPECT_EQ(Typed(run("return Entiers(5).prendre()").CStr()), "i32 5");
	EXPECT_TRUE(EvalError(run("let b:Boîte<string> = new Boîte<i32>(1)").CStr()).Contains("argument de type 1"));
	EXPECT_EQ(Eval(run("let b:Boîte<i32> = new Boîte<i32>(1)\nreturn b.valeur").CStr()).ToDisplayString(), "1");
}

TEST(ScriptRaii, DeinitRunsWhenTheLastReferenceDisappears) {
	const char *decl = "class Ressource {\n  let nom = \"\"\n  fn init(n) { this.nom = n; print(\"ouvre \" .. n) }\n"
					   "  fn deinit() { print(\"ferme \" .. this.nom) }\n}\n"
					   "class Fichier extends Ressource { fn deinit() { print(\"vide \" .. this.nom) } }\n";
	auto run = [&](const char *tail) { return Printed((String(decl) + tail).CStr()); };
	// Sortie de bloc : ordre LIFO des portées, déterministe.
	EXPECT_EQ(run("{\n  let a = Ressource(\"a\")\n  print(\"dedans\")\n}\nprint(\"après\")"), "ouvre a|dedans|ferme a|après");
	// Réaffectation : l'ancienne valeur est détruite.
	EXPECT_EQ(run("let r = Ressource(\"x\")\nr = Ressource(\"y\")\nprint(\"fin\")"), "ouvre x|ouvre y|ferme x|fin");
	// Fin de fonction, et chaînage dérivée -> parent (comme en C++).
	EXPECT_EQ(run("fn f() { let f = Fichier(\"z\") }\nf()\nprint(\"retour\")"), "ouvre z|vide z|ferme z|retour");
	// Une référence gardée ailleurs retarde la destruction.
	EXPECT_EQ(run("let garde = []\n{ let a = Ressource(\"g\"); garde.append(a) }\nprint(\"toujours là\")\ngarde.pop()\nprint(\"fin\")"),
			  "ouvre g|toujours là|ferme g|fin");
	EXPECT_TRUE(EvalError("class A { fn deinit(x) { } }").Contains("aucun paramètre"));
	// Destruction de l'interpréteur : les objets encore vivants passent aussi.
	Interpreter vm;
	{
		Interpreter inner;
		ASSERT_TRUE(inner.Run(StringView((String(decl) + "let r = Ressource(\"fin\")").CStr())).IsOk());
	}
}

// ============================================================================
// Espace de noms std (script_std.hpp)
// ============================================================================

TEST(ScriptStd, VectorListAndDeque) {
	EXPECT_EQ(Eval("let v = std.vector<i32>()\nv.push(3, 1, 2)\nv.sort()\nreturn [v, v.size(), v[0], v[-1], v.contains(2)]").ToDisplayString(),
			  "[std.vector[1, 2, 3], 3, 1, 3, true]");
	EXPECT_EQ(Typed("let v = std.vector<u8>()\nv.push(7)\nreturn v[0]"), "u8 7");
	EXPECT_TRUE(EvalError("let v = std.vector<u8>()\nv.push(300)").Contains("hors des bornes de u8"));
	EXPECT_TRUE(EvalError("let v = std.vector([1])\nreturn v.at(5)").Contains("hors bornes"));
	EXPECT_EQ(Eval("let v = std.vector([1, 2, 3, 4])\nreturn [v.map(fn(x) { return x * 10 }), v.filter(fn(x) { return x % 2 == 0 }), "
				   "v.reduce(fn(a, x) { return a + x }, 0), v.slice(1, 3)]")
				  .ToDisplayString(),
			  "[std.vector[10, 20, 30, 40], std.vector[2, 4], 10, std.vector[2, 3]]");
	EXPECT_EQ(Eval("let v = std.vector([3, 1, 2])\nv.sort(fn(a, b) { return a > b })\nreturn v").ToDisplayString(),
			  "std.vector[3, 2, 1]");
	EXPECT_EQ(Eval("let l = std.list([2, 3])\nl.push_front(1)\nl.push_back(4)\nl.insert(2, 9)\nl.erase(0)\n"
				   "return [l, l.pop_front(), l.front(), l.back()]")
				  .ToDisplayString(),
			  "[std.list[9, 3, 4], 2, 9, 4]"); // `l` est une référence : affichée après `pop_front`
	EXPECT_EQ(Eval("let d = std.deque()\nd.push_front(1)\nd.push_back(2)\nlet t = 0\nfor (x in d) { t += x }\nreturn [d, t, len(d)]")
				  .ToDisplayString(),
			  "[std.deque[1, 2], 3, 2]");
	EXPECT_TRUE(Eval("return std.vector([1, 2]) == std.vector([1, 2])").AsBoolean());
	EXPECT_EQ(Eval("return std.vector([1]) + std.vector([2])").ToDisplayString(), "std.vector[1, 2]");
	EXPECT_TRUE(Eval("let v = std.vector()\nreturn v is std.vector and not (v is std.list)").AsBoolean());
	EXPECT_TRUE(EvalError("let v:std.vector<i32> = std.vector<string>()").Contains("argument de type 1"));
}

TEST(ScriptStd, QueueStackAndPriorityQueue) {
	EXPECT_EQ(Eval("let q = std.queue()\nq.push(1)\nq.push(2)\nreturn [q.pop(), q.front(), q.size()]").ToDisplayString(), "[1, 2, 1]");
	EXPECT_EQ(Eval("let s = std.stack([1, 2, 3])\nreturn [s.pop(), s.top(), s.size()]").ToDisplayString(), "[3, 2, 2]");
	EXPECT_EQ(Eval("let p = std.priority_queue([5, 1, 8, 3])\nlet out = []\nwhile (not p.empty()) { out.append(p.pop()) }\nreturn out")
				  .ToDisplayString(),
			  "[8, 5, 3, 1]");
	EXPECT_EQ(Eval("let p = std.priority_queue(fn(a, b) { return a > b }, [5, 1, 8])\nreturn [p.pop(), p.pop()]").ToDisplayString(),
			  "[1, 5]");
	// `file -> puits` vide la file dans l'ordre de sortie.
	EXPECT_EQ(Eval("let q = std.queue([1, 2, 3])\nlet l = []\nq -> l\nreturn [l, q.size()]").ToDisplayString(), "[[1, 2, 3], 0]");
}

TEST(ScriptStd, MapsAndSets) {
	EXPECT_EQ(Eval("let m = std.map()\nm[\"b\"] = 2\nm[\"a\"] = 1\nm[3] = \"trois\"\nreturn [m.keys(), m[\"a\"], m.get(\"z\", 0), m.has(3), m.first()]")
				  .ToDisplayString(),
			  "[[3, a, b], 1, 0, true, 3]");
	EXPECT_EQ(Eval("let m = std.unordered_map({x: 1})\nm.set(1, \"un\")\nreturn [m[1.0], m.size(), m.erase(\"x\"), m.size()]").ToDisplayString(),
			  "[un, 2, true, 1]");
	EXPECT_TRUE(EvalError("let m = std.map<string, i32>()\nm[\"a\"] = \"x\"").Contains("attendu i32"));
	EXPECT_EQ(Eval("let s = std.set([3, 1, 3, 2])\nreturn [s, s.union([5]).keys(), s.intersection([1, 9]).keys(), s.lower_bound(2)]")
				  .ToDisplayString(),
			  "[std.set{1, 2, 3}, [1, 2, 3, 5], [1], 2]");
	EXPECT_EQ(Eval("let s = std.unordered_set()\ns.insert(1, 2, 2)\nreturn len(s)").ToDisplayString(), "2");
	// (`;` : sans lui, un `[` en début de ligne indexerait l'expression précédente.)
	EXPECT_EQ(Eval("let m = std.map();\n[\"k\", 42] -> m\nreturn m.items()").ToDisplayString(), "[[k, 42]]");
}

TEST(ScriptStd, StringsAndStreams) {
	EXPECT_EQ(Eval("let s = std.string(\"Bon\")\ns.append(\"jour\", \" !\")\ns[0] = \"b\"\nreturn [s, s.size(), s.upper(), s.find(\"jour\"), s.substr(3, 4)]")
				  .ToDisplayString(),
			  "[bonjour !, 9, BONJOUR !, 3, jour]");
	EXPECT_EQ(Eval("return [std.string.pad_left(\"7\", 3, \"0\"), std.string.join([1, 2, 3], \"-\"), std.string.count(\"banana\", \"a\"), "
				   "std.string.split(\" a  b \"), std.string.from_char_code(233)]")
				  .ToDisplayString(),
			  "[007, 1-2-3, 3, [a, b], é]");
	EXPECT_TRUE(Eval("return std.string(\"a\") == \"a\"").AsBoolean());
	EXPECT_EQ(Eval("let f = std.stream()\nf.write_line(\"un\")\nf <- \"deux\\ntrois\"\nreturn [f.read_line(), f.lines(), f.eof()]").ToDisplayString(),
			  "[un, [deux, trois], true]");
	EXPECT_EQ(Printed("let f = std.stream(\"a\\nb\")\nf -> print"), "a|b");
}

TEST(ScriptStd, PipesConnectTransformAndLink) {
	EXPECT_EQ(Eval("let a = std.pipe()\nlet b = std.pipe()\na -> fn(x) { return x * 2 } -> b\na <- 21\na <- 4\nreturn [b.receive(), b.try_receive(), b.try_receive()]")
				  .ToDisplayString(),
			  "[42, 8, nil]");
	// Filtre (nil écarté) et puits liste.
	EXPECT_EQ(Eval("let p = std.pipe()\nlet l = []\np -> fn(x) { if (x > 2) { return x } } -> l\nfor (i in range(5)) { i -> p }\nreturn l")
				  .ToDisplayString(),
			  "[3, 4]");
	// Liaison dans les deux sens : ce qui entre d'un côté ressort de l'autre.
	EXPECT_EQ(Eval("let a = std.pipe()\nlet b = std.pipe()\na <-> b\na.send(1)\nb.send(2)\nreturn [b.drain(), a.drain()]").ToDisplayString(),
			  "[[1], [2]]");
	// Ce qu'un pipe gardait s'écoule à la connexion.
	EXPECT_EQ(Eval("let p = std.pipe()\np.send(1, 2)\nlet l = []\np -> l\nreturn l").ToDisplayString(), "[1, 2]");
	// Producteur / consommateur entre fils.
	EXPECT_EQ(Number("let p = std.pipe()\nasync fn produire() { for (i in range(1, 11)) { p <- i } p.close() }\nproduire()\n"
					 "let total = 0\nlet v = p.receive()\nwhile (v != nil) { total += v; v = p.receive() }\nreturn total"),
			  55.0);
}

TEST(ScriptStd, FilesFilesystemAndOs) {
	const char *source =
		"let dir = \"build/tests/tmp/script_std\"\n"
		"std.filesystem.remove_all(dir)\n"
		"std.filesystem.mkdir(dir)\n"
		"let path = std.filesystem.join(dir, \"notes.txt\")\n"
		"{\n  let f = std.file(path, \"w\")\n  f.write_line(\"un\")\n  f <- \"deux\\n\"\n}\n" // fermé en sortie de bloc (RAII)
		"let lignes = []\n"
		"std.file(path) -> lignes\n"
		"std.file.append(path, \"trois\\n\")\n"
		"return [lignes, std.file.lines(path), std.filesystem.list(dir), std.filesystem.size(path), "
		"std.filesystem.extension(path), std.filesystem.exists(path), std.file.exists(dir)]";
	EXPECT_EQ(Eval(source).ToDisplayString(), "[[un, deux], [un, deux, trois], [notes.txt], 14, .txt, true, false]");
	EXPECT_TRUE(EvalError("std.file(\"build/tests/tmp/absent/x.txt\")").Contains("impossible d'ouvrir"));
	EXPECT_TRUE(Eval("return std.os.cpu_count() >= 1 and len(std.os.platform) > 0 and std.os.env(\"PAS_DE_VARIABLE_XYZ\") == nil").AsBoolean());
}

TEST(ScriptStd, DatesAndTimes) {
	EXPECT_EQ(Eval("let d = std.date(2026, 9, 27)\nreturn [d, d.weekday, d.add_days(10), d.add_months(5), std.date(2026, 12, 25) - d, d.format(\"%d/%m/%Y\")]")
				  .ToDisplayString(),
			  "[2026-09-27, 7, 2026-10-07, 2027-02-27, 89, 27/09/2026]");
	EXPECT_EQ(Eval("return [std.date(2024, 1, 31).add_months(1), std.date.is_leap(2024), std.date.days_in_month(2023, 2)]").ToDisplayString(),
			  "[2024-02-29, true, 28]");
	EXPECT_TRUE(EvalError("std.date(2026, 2, 30)").Contains("date invalide"));
	EXPECT_EQ(Eval("let t = std.time(23, 59, 30)\nreturn [t, t.add_seconds(45), t.hour, std.time(1, 0) - std.time(0, 30)]").ToDisplayString(),
			  "[23:59:30, 00:00:15, 23, 1800]");
	EXPECT_EQ(Eval("let a = std.datetime(2026, 9, 27, 12, 0, 0)\nlet b = a.add_hours(2)\nreturn [b - a, b.hour, a < b, a.date(), b.time()]")
				  .ToDisplayString(),
			  "[7200, 14, true, 2026-09-27, 14:00:00]");
	EXPECT_EQ(Eval("return std.datetime.parse(\"2026-01-02 03:04:05\").format(\"%H:%M\")").ToDisplayString(), "03:04");
}

TEST(ScriptStd, OptionResultFutureAndMutex) {
	EXPECT_EQ(Eval("let a = std.option.some(3)\nlet b = std.option(nil)\nreturn [a, b, a.map(fn(x) { return x + 1 }), b.unwrap_or(7), a.is_some()]")
				  .ToDisplayString(),
			  "[Some(3), None, Some(4), 7, true]");
	EXPECT_TRUE(EvalError("return std.option.none().unwrap()").Contains("None"));
	// `std.result.of` capture l'erreur au lieu d'interrompre le script.
	EXPECT_EQ(Eval("let r = std.result.of(fn() { return 1 / 0 })\nlet ok = std.result.of(fn(x) { return x * 2 }, 4)\n"
				   "return [r.is_err(), r.unwrap_err(), ok, ok.map(fn(x) { return x + 1 }).unwrap()]")
				  .ToDisplayString(),
			  "[true, division par zéro, Ok(8), 9]");
	EXPECT_EQ(Number("let f = std.future(fn(a, b) { return a * b }, 6, 7)\nreturn await f"), 42.0);
	// Verrou RAII : la garde libère le mutex à la sortie du bloc.
	EXPECT_EQ(Eval("let m = std.mutex()\n{ let g = std.lock_guard(m)\n  assert(m.locked) }\nlet libre = not m.locked\n"
				   "return [libre, m.with(fn() { return m.locked }), m.locked]")
				  .ToDisplayString(),
			  "[true, true, false]");
	EXPECT_EQ(Number("let m = std.mutex()\nvar total = 0\nasync fn ajoute() { for (i in range(200)) { m.with(fn() { total = total + 1 }) } }\n"
					 "let a = ajoute()\nlet b = ajoute()\nawait a\nawait b\nreturn total"),
			  400.0);
}

TEST(ScriptStd, GlobalsAreAlsoInStd) {
	EXPECT_EQ(Printed("std.print(std.len([1, 2]), std.str(3))"), "2 3");
	EXPECT_EQ(Eval("let l = [3, 1, 2]\nstd.sort(l)\nreturn l").ToDisplayString(), "[1, 2, 3]");
	EXPECT_TRUE(Eval("return std.list != list").AsBoolean()); // std.list : la liste chaînée
}

// ============================================================================
// Espace de noms math (script_math.hpp)
// ============================================================================

TEST(ScriptMath, CmathNumericAndBits) {
	EXPECT_EQ(Eval("return [math.hypot(3, 4), math.cbrt(27), math.gcd(12, 18), math.lcm(4, 6), math.factorial(5), math.binomial(5, 2)]")
				  .ToDisplayString(),
			  "[5, 3, 6, 12, 120, 10]");
	EXPECT_EQ(Eval("return [math.popcount(u8(255)), math.popcount(i8(-1)), math.countl_zero(u8(1)), math.rotl(u8(129), 1), math.byteswap(u16(258))]")
				  .ToDisplayString(),
			  "[8, 8, 7, 3, 513]");
	EXPECT_EQ(Typed("return math.rotl(u8(129), 1)"), "u8 3");
	EXPECT_EQ(Eval("return [math.sum([1, 2, 3]), math.mean([1, 2, 3, 4]), math.frexp(8), math.modf(2.5), math.is_prime(97)]").ToDisplayString(),
			  "[6, 2.5, [0.5, 4], [2, 0.5], true]");
	EXPECT_EQ(Typed("return math.max_u8"), "u8 255");
	EXPECT_EQ(Typed("return math.min_i64"), "i64 -9223372036854775808");
	EXPECT_TRUE(EvalError("return math.legendre(2, 3)").Contains("hors du domaine"));
	EXPECT_TRUE(EvalError("return math.factorial(30)").Contains("tgamma"));
	EXPECT_TRUE(Eval("return math.isnan(math.nan) and math.isinf(math.inf) and math.signbit(-1.0)").AsBoolean());
}

TEST(ScriptMath, VectorsMatricesAndQuaternions) {
	EXPECT_EQ(Eval("let a = math.vec3(1, 2, 3)\nlet b = math.vec3(4, 5, 6)\nreturn [a + b, b - a, a * 2, 2 * a, -a, a.dot(b), a.cross(b), a[1], a.x]")
				  .ToDisplayString(),
			  "[vec3(5, 7, 9), vec3(3, 3, 3), vec3(2, 4, 6), vec3(2, 4, 6), vec3(-1, -2, -3), 32, vec3(-3, 6, -3), 2, 1]");
	EXPECT_EQ(Eval("let v = math.vec2(3, 4)\nv.x = 6\nv[1] = 8\nreturn [v, v.length(), v.normalize(), math.vec3([1, 2, 3]), math.vec4(1)]")
				  .ToDisplayString(),
			  "[vec2(6, 8), 10, vec2(0.6, 0.8), vec3(1, 2, 3), vec4(1, 1, 1, 1)]");
	EXPECT_TRUE(Eval("return math.vec3(1, 2, 3) == math.vec3(1, 2, 3)").AsBoolean());
	EXPECT_EQ(Eval("let m = math.mat4.translate(1, 2, 3)\nreturn [m * math.vec3(1, 1, 1), m.inverse() * math.vec3(1, 1, 1), m.at(0, 3)]")
				  .ToDisplayString(),
			  "[vec3(2, 3, 4), vec3(0, -1, -2), 1]");
	const Value rotated = Eval("let q = math.quat.from_axis_angle(math.vec3(0, 0, 1), math.pi_2)\nreturn q * math.vec3(1, 0, 0)");
	ASSERT_TRUE(rotated.IsHost());
	const std::vector<Value> c = rotated.AsHost()->type->items(*rotated.AsHost());
	EXPECT_TRUE(std::fabs(c[0].AsNumber()) < 1e-6 && std::fabs(c[1].AsNumber() - 1.0) < 1e-6);
	EXPECT_EQ(Eval("let d = math.mat4.compose(math.vec3(1, 2, 3), math.quat.identity(), math.vec3(2, 2, 2)).decompose()\nreturn [d.translation, d.scale]")
				  .ToDisplayString(),
			  "[vec3(1, 2, 3), vec3(2, 2, 2)]");
	EXPECT_EQ(Eval("let b = math.aabb(math.vec3(0, 0, 0), math.vec3(2, 2, 2))\nlet r = math.ray(math.vec3(1, 1, -5), math.vec3(0, 0, 1))\n"
				   "return [b.center(), b.contains(math.vec3(1, 1, 1)), r.intersect_aabb(b), r.at(2)]")
				  .ToDisplayString(),
			  "[vec3(1, 1, 1), true, [5, 7], vec3(1, 1, -3)]");
	EXPECT_TRUE(EvalError("return math.vec3(1, 2)").Contains("0, 1 ou 3 composantes"));
}

TEST(ScriptMath, ComplexAndRandomEngine) {
	EXPECT_EQ(Eval("let z = math.complex(3, 4)\nreturn [z, z.abs(), z * math.complex(0, 1), z + 1, z.conj(), -z]").ToDisplayString(),
			  "[3+4i, 5, -4+3i, 4+4i, 3-4i, -3-4i]");
	EXPECT_EQ(Eval("let a = math.random_engine(7)\nlet b = math.random_engine(7)\nreturn [a.int(1, 100) == b.int(1, 100), a.next() == b.next()]")
				  .ToDisplayString(),
			  "[true, true]");
	EXPECT_TRUE(Eval("let r = math.random_engine(1)\nlet l = range(10)\nr.shuffle(l)\nstd.sort(l)\nreturn l == l and len(l) == 10 and r.choice([5]) == 5")
					.AsBoolean());
	EXPECT_TRUE(EvalError("return math.random_engine().normal(0, -1)").Contains("écart type"));
}

int main() { return RUN_ALL_TESTS(); }
