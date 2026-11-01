# script — langage de script embarqué

`libs/infrastructure/script` · couche **infrastructure** · `libscript.a` · espace de noms `script::`

## Présentation

`script` implémente « Script », un langage **interprété**, dynamiquement typé,
à syntaxe à accolades. Le module contient tout le nécessaire :

- un lexeur, puis un parseur par descente récursive qui produit un AST ;
- un interpréteur par parcours d'arbre ;
- une bibliothèque standard, et des liaisons vers l'hôte C++.

Il sert de langage de mods à [voxelia](demo_voxelia.md) (`mods/*/init.script`)
et de langage de jeu à la [sandbox](demo_sandbox.md). Les liaisons vers les
autres modules du dépôt sont dans
[`script_bindings`](libs_infrastructure_script_bindings.md).

**Conception :**

- **Aucune exception**, même pour le flot de contrôle.
  - Chaque instruction rend `Result<ExecOutcome, ScriptError>`, et
    `ExecOutcome` porte NORMAL, BREAK, CONTINUE ou RETURN.
  - Chaque expression rend `Result<Value, ScriptError>`.
- **Garde-fous** : `maxSteps` (budget d'instructions par exécution) et
  `maxCallDepth`. Une boucle infinie ou une récursion sans fin devient une
  erreur ordinaire, au lieu de geler l'application.
- **Reproductible** : `SetRandomSeed` fixe le générateur interne
  (xorshift64\*).
- **Véracité façon Lua** : seuls `nil` et `false` sont faux ; `0` et `""`
  sont vrais.
- **Variables** : affecter une variable non déclarée est une **erreur**. Les
  portées `var` (fonction, hissée), `let` et `const` (bloc, avec une zone
  morte avant la déclaration) se comportent comme en JavaScript.
- **Références** : listes et tables ont une sémantique de référence. L'hôte
  peut donc donner une liste à un script, la faire remplir et la relire.

## Le langage en bref

Résumé ci-dessous. Le guide complet (syntaxe, bibliothèque standard,
intégration C++) est dans [script_language](script_language.md).

```
// commentaires : //  /* … */  #
let x = 1            var y = 2            const PI2 = math.pi * 2
fn carre(n) { return n * n }              // `func` est un synonyme de `fn`
let f = fn(a) { return a + 1 }            // fonction anonyme, fermetures

if (x > 0) { … } else { … }
while (y < 10) { y += 1 }
for (v in [1, 2, 3]) { … }                for (k in {a: 1, b: 2}) { … }
for (i in range(0, 5)) { … }              for (c in "texte") { … }

let liste = [1, 2, 3]                     let table = {nom: "Ada", age: 36}
"a" .. "b"                                // concaténation
x += 1   x -= 1   x *= 2   x /= 2

class Animal {
    let nom = "?"                         // champ (par instance)
    fn init(nom) { this.nom = nom }       // constructeur
    fn cri() { return "..." }
}
class Chien extends Animal {
    override fn cri() { return "wouf" }   // `override` obligatoire
}
let c = new Chien("Rex")                  // `new` facultatif
c is Animal                               // vrai

namespace geo { fn aire(r) { return math.pi * r * r } }
source -> filtre -> puits                 // opérateurs de flux (std.pipe)
```

Mots-clés :

- `and`, `or`, `not`, `in`, `is`, `as`, `nil` (ou `null`), `true`, `false` ;
- `let`, `var`, `const`, `fn`/`func`, `return`, `if`, `else`, `while`,
  `do`, `for`, `break`, `continue` ;
- `class`, `extends`, `implements`, `interface`, `abstract`, `override`,
  `static`, `factory`, `operator`, `super`, `this`, `new`, `enum`,
  `namespace`, `import` ;
- `async`, `await`.

Précédence, de la plus lâche à la plus serrée :

| Niveau | Opérateurs |
|---|---|
| Flux | `->`, `<-`, `<->` |
| Logique | `or`, puis `and` |
| Égalité | `==`, `!=` |
| Comparaison | `<`, `<=`, `>`, `>=` |
| Concaténation | `..` |
| Addition | `+`, `-` |
| Multiplication | `*`, `/`, `%` |
| — | unaires, puis postfixés |

Dans la condition d'un `if` ou d'un `while`, et dans l'itérable d'un `for`,
`{` ouvre **toujours** le bloc. Une table littérale s'y écrit entre
parenthèses.

**Bibliothèque** :

- fonctions globales : `print`, `len`, `range`, `keys`, `push`, `type`… ;
- espace `math` : toute la bibliothèque `<cmath>`, plus les types de
  [math](libs_foundation_math.md) ;
- espace `std` :
  - conteneurs : `vector`, `list`, `deque`, `queue`, `stack`,
    `priority_queue`, `map`, `unordered_map`, `set`, `unordered_set` ;
  - texte et flux : `string`, `stream`, `pipe` ;
  - fichiers et système : `file`, `filesystem`, `os` ;
  - temps : `datetime`, `date`, `time` ;
  - valeurs : `option`, `result` ;
  - concurrence : `future`, `mutex`, `lock_guard` ;
  - tri : `sort`.

## Architecture interne

| Fichier | Contenu |
|---|---|
| `script.hpp` | Point d'entrée : `LoadScriptFile`, `RunScriptFile`, `InstallDataLibrary` |
| `script_lexer.hpp` | `ScriptError {message, line, column}` (`Format()` → `"3:17: message"`), `Token`, `Lexer` |
| `script_ast.hpp` | AST polymorphe (`Expr`/`Stmt` avec un `kind`, sans `std::variant` qui lèverait des exceptions), `FunctionDef`, `Program` |
| `script_parser.hpp` | Parseur à niveaux de précédence |
| `script_value.hpp` | `Value` (nil, booléen, nombre, chaîne, liste, table, fonction, native, instance…), `NativeFn`, `HostType`, pont `ValueFromNode`/`NodeFromValue` vers `dataio::Node` |
| `script_interpreter.hpp` | `Environment`, `Interpreter` |
| `script_std.hpp`, `script_math.hpp` | Bibliothèque standard, espaces `std` et `math` |
| `script_owners.hpp` | « Owners » : types de base fournis par l'hôte, dont les classes de script dérivent, avec un cycle de vie RAII (`init`, `on_destroy`, `deinit`, `destroy()`) |

Dépendances : `core`, `math`, `sdl3` (lecture de fichiers, fils), `dataio`.

## API publique (hôte C++)

| Élément | Rôle |
|---|---|
| `Interpreter` | Machine virtuelle. Ses champs publics : `maxSteps` (20 000 000), `maxCallDepth` (200), `maxAsyncTasks` (256), `onPrint` (destination de `print` ; sinon `Output()`), `onAsyncError` |
| `Result<Value, ScriptError> Run(source)` / `RunProgram(program)` | Compile et exécute |
| `RegisterNative(nom, aritéMin, aritéMax, fn)` | Fonction C++ ; l'arité est vérifiée **avant** l'appel ; `aritéMax < 0` = variadique |
| `RegisterNamespacedNative(espace, nom, …)` | `scene.spawn(…)` ; les membres d'un espace de l'hôte sont constants |
| `SetGlobal`, `GetGlobal`, `HasGlobal` | Globales de l'hôte |
| `CallGlobalIfPresent(nom, args)` | Appelle un rappel du script s'il existe (`NONE` sinon) : c'est le mécanisme `on_start`, `on_update`… |
| `SetRandomSeed`, `Output()`, `ClearOutput()` | Reproductibilité et sortie |
| `InstallDataLibrary(vm)` | Ajoute `parse(format, texte)`, `encode(format, valeur)`, `read_file`, `write_file` et `load(format, chemin)`, pour JSON, YAML, TOML, XML, HTML, INI et CSV |
| `RunScriptFile(vm, chemin)` | Charge puis exécute ; une erreur d'E/S a `line == 0` |

`using NativeFn = std::function<Result<Value, ScriptError>(Interpreter&, std::vector<Value>&)>`.

## Exemple

Script `demo.script` :

```
// Données et fonctions
let scores = {ada: 12, bob: 7}
scores.cyd = 21

fn best(table) {
    var name = nil
    var top = -1
    for (key in keys(table)) {
        if (table[key] > top) { top = table[key]; name = key }
    }
    return name
}

// Classes, héritage, redéfinition
class Animal {
    let nom = "?"
    fn init(nom) { this.nom = nom }
    fn cri() { return "..." }
    fn presente() { return this.nom .. " : " .. this.cri() }
}
class Chien extends Animal {
    override fn cri() { return "wouf" }
}

// Espace de noms
namespace geo {
    fn aire(r) { return math.pi * r * r }
}

let rex = new Chien("Rex")
print(rex.presente())
print("meilleur : " .. best(scores))
spawn(1, 2, 3)                       // fonction fournie par l'hôte C++
let doc = parse("json", "{\"level\": 3, \"tags\": [\"a\", \"b\"]}")
print("niveau " .. doc.level .. ", " .. len(doc.tags) .. " tags, aire " .. geo.aire(2))
return len(keys(scores))
```

Programme hôte :

```cpp
#include <cstdio>
#include <script/script.hpp>

int main(int argc, char **argv) {
	script::Interpreter vm;
	vm.maxSteps = 1'000'000; // une boucle infinie devient une erreur ordinaire
	script::InstallDataLibrary(vm); // parse / encode / read_file / write_file / load
	vm.onPrint = [](const String &line) { std::printf("[script] %s\n", line.CStr()); };

	// Fonction de l'hôte : l'arité est vérifiée avant l'appel
	vm.RegisterNative("spawn", 3, 3, [](script::Interpreter &, std::vector<script::Value> &args) -> Result<script::Value, script::ScriptError> {
		std::printf("spawn(%g, %g, %g)\n", args[0].AsNumber(), args[1].AsNumber(), args[2].AsNumber());
		return Ok(script::Value::Nil());
	});

	auto result = script::RunScriptFile(vm, argc > 1 ? argv[1] : "demo.script");
	if (result.IsError()) {
		std::fprintf(stderr, "%s\n", result.Error().Format().CStr()); // message positionné (ligne, colonne)
		return 1;
	}
	std::printf("valeur rendue : %g\n", result.Value().AsNumber());

	// Rappel optionnel : NONE si le script ne définit pas « on_update »
	if (auto r = vm.CallGlobalIfPresent("on_update", {script::Value::Number(0.016)}); r.IsNone())
		std::printf("pas de on_update\n");
	return 0;
}
```

Sortie :

```
[script] Rex : wouf
[script] meilleur : cyd
spawn(1, 2, 3)
[script] niveau 3, 2 tags, aire 12.5664
valeur rendue : 3
pas de on_update
```

## Intégration

`modules = ["script"]`, puis `#include <script/script.hpp>`.

- Pour exposer les modules du dépôt (ECS, UI, physique, audio, SQL…),
  utilisez [script_bindings](libs_infrastructure_script_bindings.md).
- Pour l'édition des fichiers `.script`, une extension VS Code est fournie :
  voir [Extension VS Code](tools_script_vscode.md).

**Fermetures et mémoire.** Une fermeture capture son environnement par
`shared_ptr`, et cet environnement contient la fonction dès qu'elle y est
nommée : c'est un cycle. L'interpréteur le casse dans son destructeur
(`BreakEnvironmentCycles`). Par conséquent, la mémoire d'une fermeture
cyclique créée en boucle n'est rendue qu'à la destruction de l'interpréteur.

## Tests

```sh
cpp-maker test -m script
```

`tests/test_script.cpp` couvre :

- le lexeur, le parseur et les portées ;
- les classes et l'héritage, les espaces de noms ;
- les garde-fous ;
- la bibliothèque standard et le pont `dataio`.
