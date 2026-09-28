---
name: project-script-language
description: "data::script — langage de script interprété embarqué (lexeur/parseur/interpréteur + pont data::Node + bibliothèque de codecs), écrit pour piloter animations, gameplay et scénarios de l'éditeur de niveau. Aucune exception : tout passe par Result/Option."
metadata:
  type: project
---

Nouveau sous-module de `data::`, ajouté le 2026-09-12 pour l'éditeur de niveau
([[project-level-editor-app]]). Le module `data::` savait transformer du TEXTE
en arbre `data::Node` pour sept formats de DONNÉES ; il sait maintenant aussi
compiler et exécuter un langage de PROGRAMMATION.

## Fichiers

| Fichier | Rôle |
|---|---|
| `lib/include/data/script_lexer.hpp` | `ScriptError`, `Token`, `Lexer` |
| `lib/include/data/script_ast.hpp` | `Expr`/`Stmt` polymorphes, `FunctionDef`, `Program` |
| `lib/include/data/script_parser.hpp` | descente récursive + niveaux de précédence |
| `lib/include/data/script_value.hpp` | `Value` (8 formes), `ListObject`/`MapObject`, pont `data::Node` |
| `lib/include/data/script_interpreter.hpp` | `Environment`, `Interpreter`, bibliothèque standard (~50 natives) |
| `lib/include/data/script.hpp` | ombrelle + `InstallDataLibrary` (parse/encode/read_file/write_file/load) |
| `tests/script_smoke_test.cpp` | 40 tests, 100 % CPU |

## Le langage (« Sled »)

Typage dynamique façon Lua/Python, syntaxe à ACCOLADES :
`let`/`var`, `fn`/`func`, `if`/`else if`/`else`, `while`, `for x in`,
`return`/`break`/`continue`, listes `[…]`, tables `{clef: valeur}`, fonctions
anonymes et fermetures, `and`/`or`/`not`, `..` (concaténation), `+= -= *= /=`,
commentaires `#`, `//`, `/* */`.

## Décisions à retenir

- **Aucune exception, y compris pour le flot de contrôle.** L'implémentation
  « naturelle » propage `return`/`break`/`continue` et les erreurs par
  exception ; interdit ici ([[feedback-no-exceptions]]). Chaque instruction
  rend `Result<ExecOutcome, ScriptError>` où `ExecOutcome::flow` porte
  NORMAL/BREAK/CONTINUE/RETURN ; chaque expression rend
  `Result<Value, ScriptError>`. Verbeux, mais chaque point de propagation est
  visible.
- **AST polymorphe plutôt que `std::variant`** : `std::get<T>` lève
  `bad_variant_access`. `kind` + `static_cast` est la seule forme sans
  exception.
- **Accolades, pas d'indentation significative** : un lexeur sans pile
  d'indentation reste trivialement réentrant et testable.
- **Ambiguïté `{`** tranchée comme en Rust/Go : dans une condition de
  `if`/`while` et dans l'itérable d'un `for`, `{` ouvre TOUJOURS le bloc. Une
  table littérale y reste écrivable entre parenthèses, et le message d'erreur
  le dit explicitement.
- **Véracité façon Lua** : seuls `nil` et `false` sont faux ; `0` et `""` sont
  VRAIS. Choix assumé (un script d'éditeur teste des présences/absences).
- **Affecter une variable non déclarée est une ERREUR**, contrairement à Lua —
  une faute de frappe ne crée pas silencieusement un global.
- **Listes et tables ont une sémantique de RÉFÉRENCE** (`shared_ptr`) : l'hôte
  C++ peut donner une liste à un script, la faire remplir, et la relire.
- **Tables = vecteur trié par insertion, recherche linéaire** — `String` n'a
  pas de `std::hash` dans ce dépôt, et l'ordre d'insertion préservé rend
  l'aller-retour JSON stable. Limite documentée, pas un oubli.

## Deux garde-fous qui comptent vraiment

`maxSteps` (budget d'instructions) et `maxCallDepth` transforment une boucle
infinie ou une récursion sans fin en erreur d'exécution ordinaire, au lieu d'un
gel de l'application ou d'un débordement de pile. C'est ce qui rend acceptable
d'exécuter du script utilisateur à chaque image dans un éditeur.

## Fermetures et fuites (à relire avant de toucher `Environment`)

Une fonction capture son environnement par `shared_ptr`, et cet environnement
contient la fonction dès qu'elle y est nommée : **cycle de `shared_ptr`, donc
fuite** — visible sous LeakSanitizer, avec lequel ce dépôt compile. Il n'y a
pas de ramasse-miettes. L'interpréteur garde donc un registre `weak_ptr` de
tous les environnements créés et vide leurs liaisons dans son destructeur
(`BreakEnvironmentCycles`), ce qui casse tous les cycles d'un coup. Le registre
est purgé de ses entrées mortes de façon amortie, sinon il grossirait sans fin
pour un script appelé à chaque image. Limite assumée : la mémoire d'une
fermeture cyclique créée en boucle n'est récupérée qu'à la destruction de
l'interpréteur.

## Pont vers `data::`

`ValueFromNode` / `NodeFromValue` convertissent dans les deux sens. Un nombre
entier est réencodé en nœud `INT` — contrepartie du correctif apporté à
l'encodeur JSON (cf. [[project-level-editor-app]]), pour que l'aller-retour
script → JSON → script soit stable. Une fonction n'a pas de représentation de
données et devient `NONE`, sans erreur (comme `JSON.stringify` ignore une
fonction).

`InstallDataLibrary(vm)` — séparé du constructeur pour que le cœur du langage
n'entraîne AUCUNE dépendance vers les codecs ni vers `sdl3::` — ajoute
`parse(format, texte)`, `encode(format, valeur)`, `read_file`, `write_file`,
`load(format, chemin)`, pour les sept formats du module. C'est par là qu'un
script lit un `.gltf` (JSON) ou un projet d'éditeur.

## Intégration hôte

`RegisterNative(nom, minArité, maxArité, fn)` et
`RegisterNamespacedNative(table, nom, …)` (qui donne `scene.spawn(…)`).
L'ARITÉ EST VÉRIFIÉE AVANT L'APPEL : une native n'a jamais à valider le nombre
de ses arguments. `CallGlobalIfPresent` distingue « rappel non déclaré »
(NONE, normal) d'une vraie erreur d'exécution — c'est le mécanisme
`on_start`/`on_update`/`on_frame` de l'éditeur.

`SetRandomSeed` + générateur xorshift64\* interne : deux exécutions d'un même
script donnent exactement la même chose, ce qui rend rapports et captures
d'écran comparables d'un run à l'autre.

## Deux bugs trouvés par ses propres tests

1. Le token `END_OF_FILE` portait la position du DERNIER token lu, pas la fin
   réelle de la source : toute construction inachevée était signalée à la
   mauvaise ligne.
2. `sort_numbers` utilise un tri par insertion et non `std::sort` :
   volontaire, pour n'avoir jamais de comparateur scripté susceptible
   d'échouer au milieu d'un tri (ce qui casserait l'invariant sans moyen de
   remonter l'erreur proprement).

## Portées `var` / `let` / `const` et espaces de noms — 2026-09-26

Demande : un système de portée façon JavaScript et des espaces de noms (« math »
pour les fonctions et constantes mathématiques).

| forme   | portée              | réaffectation | redéclaration (même portée) | hissage                      |
|---------|---------------------|---------------|-----------------------------|------------------------------|
| `var`   | fonction ou globale | oui           | oui (sans valeur : garde la sienne) | oui, vaut `nil` (pas d'`undefined` dans le langage) |
| `let`   | bloc `{}`           | oui           | non                         | oui, non initialisée (zone morte) |
| `const` | bloc `{}`           | NON           | non                         | oui, non initialisée (zone morte) |

- **Deux niveaux de contrôle.** Le parseur suit des portées STATIQUES
  (`Parser::Declare`/`Resolve`, portée de fonction ≠ portée de bloc) et rend,
  AVANT exécution et avec la ligne de la déclaration : redéclaration d'un
  `let`/`const`, `var` qui traverse un bloc et heurte un `let` de la même
  fonction, paramètre en double, `const` sans valeur, réaffectation d'une
  constante ou d'un espace de noms. L'interpréteur refait ces contrôles à
  l'exécution (ce que l'analyse ne voit pas : valeurs de l'hôte, fermetures).
- **Environment** porte des `Binding{kind, initialized, host}` et sait s'il est
  une portée de FONCTION (corps de fonction, programme, espace de noms) ou de
  BLOC. `Hoist()` s'exécute à l'entrée de chaque portée : `let`/`const` du
  niveau → liaison non initialisée ; `fn` → définie aussitôt (appelable avant
  sa ligne) ; portée de fonction → tous les `var` du corps (blocs imbriqués
  compris, PAS les fonctions ni espaces de noms imbriqués) à `nil`. La liste
  des `var` d'une fonction est calculée une fois et gardée dans
  `FunctionDef::hoistedVars` (le hissage a lieu à CHAQUE appel ; un script
  appelé à chaque image ne doit pas reparcourir son corps).
- **Zone morte** : lire ou affecter un `let`/`const` avant sa ligne est une
  erreur (« utilisée avant sa déclaration »). Hissée, la liaison masque dès le
  début du bloc la variable englobante du même nom — pas de lecture muette de
  la mauvaise variable (comme JS).
- **Boucles** : chaque tour de `for x in` a sa propre liaison (sémantique
  `let`) — une fermeture créée au tour 1 voit toujours la valeur du tour 1.
- **Console de l'éditeur** : rejouer un programme dans le même interpréteur
  REDÉCLARE ses `let`/`const` de premier niveau (comme une console de
  navigateur) ; en revanche un nom posé par l'HÔTE (`SetGlobal`, espaces de
  noms de l'application) ne se redéclare pas (« nom réservé »), il se
  réaffecte seulement s'il n'est pas constant.

### Espaces de noms

- Nouveau type de valeur `NAMESPACE` (`NamespaceObject{name, scope}`) : les
  membres sont les liaisons de sa portée, lues VIVANTES (une fonction de
  l'espace qui modifie un de ses `let` est vue du dehors) et en LECTURE SEULE
  du dehors (`ns.x = …`, `ns["x"] = …`, `ns = …` sont refusés).
- `namespace geo { const unit = 2  fn area(r) { … } }` : corps = portée de
  FONCTION (un `var` n'en sort pas), membres = ses déclarations de premier
  niveau, rouvrable pour l'étendre (comme TypeScript), imbricable
  (`outer.inner.x`), sans `return`/`break`/`continue`.
- `math` : toutes les fonctions (`math.sin`, `math.atan2`, `math.clamp`,
  `math.lerp`, `math.min`/`max`, `math.random`/`random_int`…) et les
  constantes en VALEURS (`math.pi`, `math.tau`, `math.e`, `math.phi`…) ;
  `physics` : les constantes physiques (`physics.c`, `physics.g`, `physics.h`…).
  Ce n'étaient avant que des globales, dont des fonctions d'une lettre (`e()`,
  `c()`, `g()`, `h()`, `R()`) qui entraient en collision avec les variables
  des scripts — et `c` y figurait deux fois.
- L'API de l'hôte (`editor.`, `scene.`, `node.`, `light.`…) passe par
  `RegisterNamespacedNative`/`RegisterNamespaceConstant`/`HostNamespace` :
  ce sont désormais de vrais espaces de noms (un script ne peut plus écraser
  `editor.log` par accident). Lecture côté hôte : `GetNamespaceMember`.
- `keys(ns)` et `has(ns, "x")` acceptent un espace de noms ; `type(math)`
  vaut `"namespace"`.

**Rupture assumée** : les anciennes globales mathématiques (`sin(…)`, `pi()`…)
n'existent plus. Tous les scripts du dépôt ont été migrés (contenu du donjon,
circuit, scénarios, tests). Un projet ENREGISTRÉ avant ce changement garde des
scripts de scène qui appellent `sin(…)` : ils échouent en mode Jeu avec
« variable inconnue `sin` » — les mettre à jour en `math.sin(…)`.

Tests : `tests/script_smoke_test.cpp`, suites `ScriptScope` (6) et
`ScriptNamespace` (4) — une ligne par règle du tableau.
