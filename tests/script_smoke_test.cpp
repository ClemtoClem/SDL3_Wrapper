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
		auto result = vm.Run(StringView("return random() + random() * 100"));
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

int main() { return RUN_ALL_TESTS(); }
