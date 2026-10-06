# Script — le langage de script embarqué de `data::script`

Ce document explique comment **écrire des scripts Script** et comment **intégrer
l'interpréteur depuis du code C++ hôte** (le moteur/l'application qui embarque
`data::script`). Il couvre la syntaxe du langage, sa bibliothèque standard
(`std`, `math`, `ecs`, `ui`, `gen`), et l'API C++ d'intégration.

Script est un langage à typage **dynamique**, syntaxe à **accolades** (pas
d'indentation significative), très proche dans l'esprit d'un mélange
Python/Lua/TypeScript : `let`/`var`/`const`, `fn`, `for x in ...`, listes et
tables littérales, `and`/`or`/`not`, classes avec héritage simple et
interfaces, `async`/`await`, et des **opérateurs de flux** (`->`, `<-`,
`<->`) inspirés des pipes.

Aucune exception n'est levée dans tout le module (côté C++ comme côté
script) : chaque erreur remonte comme une valeur (`Result<T, ScriptError>`
côté C++, message d'erreur positionné côté script). Ce choix se répercute sur
toute l'organisation du code.

---

## Table des matières

1. [Démarrage rapide](#démarrage-rapide)
2. [Syntaxe du langage](#syntaxe-du-langage)
   - [Commentaires et lexique](#commentaires-et-lexique)
   - [Variables et types](#variables-et-types)
   - [Opérateurs](#opérateurs)
   - [Structures de contrôle](#structures-de-contrôle)
   - [Fonctions](#fonctions)
   - [Classes, interfaces, énumérations](#classes-interfaces-énumérations)
   - [Bases fournies par l'hôte (owners)](#bases-fournies-par-lhôte-owners)
   - [Modules : `import`](#modules--import)
   - [Surcharge d'opérateurs](#surcharge-dopérateurs)
   - [Génériques](#génériques)
   - [Espaces de noms](#espaces-de-noms)
   - [Asynchrone : `async`/`await`](#asynchrone-asyncawait)
   - [Opérateurs de flux (`->`, `<-`, `<->`)](#opérateurs-de-flux---)
3. [Bibliothèque standard](#bibliothèque-standard)
   - [Fonctions globales](#fonctions-globales)
   - [`std` — conteneurs, texte, fichiers, temps, concurrence](#std--conteneurs-texte-fichiers-temps-concurrence)
   - [`math` — mathématiques et algèbre linéaire](#math--mathématiques-et-algèbre-linéaire)
   - [`ecs` — entités et composants](#ecs--entités-et-composants)
   - [`ui` — interface utilisateur](#ui--interface-utilisateur)
   - [`gen` — génération procédurale](#gen--génération-procédurale)
4. [Gestion des erreurs](#gestion-des-erreurs)
5. [Intégration côté C++ (l'hôte)](#intégration-côté-c-lhôte)
6. [Modèle mémoire et limites connues](#modèle-mémoire-et-limites-connues)
7. [Organisation des fichiers du module](#organisation-des-fichiers-du-module)

---

## Démarrage rapide

Un script minimal :

```script
# commentaire (ou // ... ou /* ... */)
let v = 0.0
fn step(dt) {
    v = v + 9.81 * dt
    if v > 10 { v = 10 }
    return v
}

print(step(0.5))
```

Depuis l'hôte C++ :

```cpp
#include "data/script/script_std.hpp"
#include "data/script/script_math.hpp"

data::script::Interpreter vm; // installe std + math automatiquement

vm.onPrint = [](const data::script::String &line) {
    std::printf("%s\n", line.CStr());
};

if (auto result = vm.Run("print('bonjour depuis Script')"); result.IsError())
    std::printf("erreur : %s\n", result.Error().Format().CStr());
```

---

## Syntaxe du langage

### Commentaires et lexique

- Commentaires : `# ...`, `// ...`, `/* ... */`.
- Les retours à la ligne ne sont pas significatifs ; `;` est optionnel.
- Les identifiants acceptent les lettres UTF-8 non-ASCII (`départ`, `créés`
  sont valides).
- Chaînes : `"..."` ou `'...'`, échappements usuels ; un échappement inconnu
  est une **erreur** (pas d'ignorance silencieuse).
- Nombres : entiers (`42`, en `i64`, ou `u64` au-delà de `INT64_MAX`) ou
  flottants (`4.2`, `1e3`, en `f64`).

### Variables et types

Trois formes de déclaration, avec des règles de portée et de droits
différentes :

| forme   | portée              | réaffectation | redéclaration (même portée) |
|---------|---------------------|----------------|------------------------------|
| `var`   | fonction ou globale | oui            | oui                          |
| `let`   | bloc `{}`           | oui            | non                          |
| `const` | bloc `{}`           | **non**        | non                          |

Toutes sont **hissées** en tête de leur portée : un `var` y vaut `nil` avant
sa ligne de déclaration ; un `let`/`const` est en « zone morte » (le lire
avant sa déclaration est une erreur).

```script
var compteur = 0
let nom = "Ada"
const PI_APPROX = 3.14159
```

Annotation de type optionnelle après un nom (variable, paramètre, champ,
type de retour) :

```script
let x: i32 = 3
fn f(a: i32, b: i32): i32 { return a + b }
```

Types disponibles dans les annotations :

- **Nombres entiers bornés** : `i8 i16 i32 i64 u8 u16 u32 u64`
- **Flottants** : `f32 f64`
- **Autres primitifs** : `bool`, `string`, `any`, `fn`, `nil`
- **Génériques** : `list<T>`, `map<V>` (les clés d'une `map` littérale sont
  toujours des chaînes), `future<T>`
- **Classes/interfaces** : `Forme`, ou `geo.Forme` dans un espace de noms
- **Nullable** : `T?` — la même chose, ou `nil`

Valeurs littérales : nombres, chaînes, `true`/`false`, `nil`, listes
(`[1, 2, 3]`), tables (`{clé: valeur, ...}`).

Arithmétique typée : un calcul entier ⊕ entier se fait **exactement** sur
128 bits, puis le résultat doit tenir dans le type commun des deux opérandes
(le plus large ; à largeur égale et signes mêlés, le non signé si le
résultat est positif) — sinon c'est une erreur de dépassement de capacité
(`u8(200) + u8(100)` échoue). Dès qu'un flottant entre dans le calcul, le
résultat est flottant. `/` rend toujours un flottant.

### Opérateurs

```
+  -  *  /  %  ..(concaténation de chaînes, à la Lua)
== != < <= > >=
and  or  not
is    # `valeur is Classe` : instance de la classe (ou dérivée), ou d'une interface
as    # `valeur as Classe` : transtypage vérifié (erreur si le test `is` échoue)
=  +=  -=  *=  /=
```

Opérateurs de flux (voir la [section dédiée](#opérateurs-de-flux---)) :

```
->   # a -> b : a s'écoule dans b
<-   # a <- b : b s'écoule dans a (écrivez `x < -1` avec un espace pour comparer à un négatif)
<->  # liaison dans les deux sens
```

### Structures de contrôle

```script
if condition {
    ...
} else if autre {
    ...
} else {
    ...
}

while condition {
    ...
}

do {
    ...
} while condition   # le corps s'exécute au moins une fois

for x in iterable {
    ...
}
# `iterable` : liste, clés d'une table, caractères d'une chaîne, valeurs
# d'un énuméré, éléments d'un conteneur `std.*`, ou ce que rend un
# `operator iter` défini par une classe.

for i: i32 in liste {   # type annoncé sur la variable de boucle, optionnel
    ...
}

break
continue
return valeur   # `return` sans expression rend `nil`
```

### Fonctions

```script
fn carré(x) { return x * x }

fn max<T>(a: T, b: T): T {     # fonction générique, T lié à l'appel (max<i32>(1,2))
    if a > b { return a }
    return b
}

let lambda = fn(x, y) { return x + y }   # fonction anonyme, valeur de première classe
```

- Une fonction peut être `async` (voir plus bas).
- Les paramètres et le type de retour peuvent être annotés ; ils sont
  **vérifiés à chaque appel**.

### Classes, interfaces, énumérations

```script
interface Forme { fn aire() }

abstract class Base implements Forme {
    let nom = "base"                # champ d'instance (initialisé à chaque `new`)
    static let creates = 0          # champ de classe
    fn init(nom) { this.nom = nom } # constructeur
    abstract fn aire()
}

class Carré extends Base {
    let côté = 1
    fn init(c) { super.init("carré"); this.côté = c }
    override fn aire() { return this.côté * this.côté }
    factory fn unité() { return new Carré(1) }   # constructeur nommé
}

let c = new Carré(3)
print(c.aire())          # 9
print(c is Forme)        # true
let f = c as Forme       # transtypage vérifié
```

Points clés :

- `extends` = héritage simple d'une classe ; `implements` = une ou plusieurs
  interfaces.
- `super.methode(...)` appelle la méthode de la classe **parente** ;
  `super(...)` est un raccourci pour `super.init(...)`.
- `this` désigne l'instance courante.
- `factory fn` : méthode de classe qui **doit** rendre une instance — sert de
  constructeur nommé.
- Une classe (comme une fonction) est une valeur **partagée** entre l'AST et
  l'exécution : elle survit au programme qui l'a déclarée.

Énumérations — une classe spéciale dont les seules instances sont ses
valeurs :

```script
enum Couleur {
    ROUGE
    VERT = 5      # sans valeur explicite : la précédente + 1 (0 par défaut)
    BLEU

    fn nom_fr() { return this.name }
}
```

Générique : `class Boîte<T> { ... }`, instancié `Boîte<i32>(...)`.

### Bases fournies par l'hôte (owners)

L'hôte peut exposer des **types de base** écrits en C++ (un nœud de scène, un
corps physique, un abonnement à des évènements…). Une classe en dérive avec
`extends`, à côté d'**au plus une** classe parente de script, dans n'importe
quel ordre :

```script
class Ennemi extends Acteur, Mesh3D, PhysicsBody, Gameplay {
    fn init(nom) {
        super.init(nom)                              # Acteur.init, puis chaque base
        this.set_body({kind: "dynamic", mass: 80})   # méthode de PhysicsBody
        this.on("hit", fn(dégâts) { this.pv -= dégâts })   # Gameplay
    }
    fn on_destroy() { print("adieu " .. this.name) }
    fn deinit()     { print("deinit") }
}

let e = Ennemi("gobelin")   # construit, enregistré, actif
e is Mesh3D                 # true
e.destroy()                 # séquence RAII ci-dessous ; rend false la 2e fois
e.is_destroyed()            # true
```

Règles :

- **Construction.** `super.init(args)` construit la classe parente de script
  puis les bases de l'hôte déclarées par la classe, dans l'ordre de
  `extends`, avec les **mêmes** arguments. Une base que `init` n'a pas
  construite l'est à la sortie de `init` (sans argument) — et, sans `init`,
  avec les arguments de l'appel : `class Caisse extends Mesh3D {}` puis
  `Caisse("c1")`. Appeler une méthode d'une base pas encore construite est
  une erreur explicite. Une base refusée par l'hôte annule la construction.
- **Méthodes.** `this.x` cherche d'abord les champs, puis les méthodes du
  script, puis celles des bases de l'hôte : une classe peut donc
  **redéfinir** une méthode d'une base et appeler l'originale par
  `super.x(…)`. Les propriétés de l'hôte se lisent (et, si l'hôte le
  permet, s'écrivent) comme des champs.
- **Vie.** Dès qu'une base est construite, l'instance est enregistrée dans le
  registre de son interpréteur, qui la **garde en vie** : une ressource ne
  disparaît pas parce qu'une variable est perdue. Elle vit jusqu'à
  `destroy()` — ou jusqu'à ce que l'hôte la détruise (fin de scène,
  destruction de l'interpréteur).
- **Destruction** (idempotente) : `on_destroy()` → `deinit()` de chaque
  classe, la plus dérivée d'abord → libération C++ de chaque base en ordre
  **inverse** de construction → retrait du registre. Ensuite l'objet existe
  encore (ses champs restent lisibles) mais les méthodes de ses bases
  rendent une erreur « détruite ».
- Les erreurs de déclaration sont explicites : deux parents de script, une
  base citée deux fois ou déjà héritée, un type de l'hôte non dérivable, des
  arguments de type `<…>` donnés à une base de l'hôte.

Évitez les cycles de références entre objets (A tient B qui tient A) : le
langage compte les références, il ne ramasse pas les cycles. Les abonnements
de `Gameplay` sont oubliés à la destruction pour cette raison.

### Modules : `import`

`import "chemin"` exécute **une seule fois** un autre script et rend sa
valeur : son `return` de plus haut niveau, ou à défaut un espace de noms de
ses déclarations. Le fichier est cherché par le résolveur de l'hôte, puis en
chemin absolu, dans le dossier du script importateur et dans les dossiers
enregistrés par l'hôte ; l'extension `.script` est ajoutée si absente. Les
imports cycliques sont une erreur. `require("chemin")` en est la forme
fonction.

```script
const demo = import "demo"          # demo.script définit `abstract class SceneDemo`
class Physique extends demo.SceneDemo { … }
```

### Surcharge d'opérateurs

```script
class Vecteur2 {
    let x = 0
    let y = 0
    fn init(x, y) { this.x = x; this.y = y }

    operator +(o) { return new Vecteur2(this.x + o.x, this.y + o.y) }  # instance, this à gauche
    static operator ==(a, b) { return a.x == a.x and a.y == b.y }      # symétrique
    operator str() { return "(" .. this.x .. ", " .. this.y .. ")" }   # conversion texte
    operator [](i) { return i == 0 ? this.x : this.y }                 # lecture indexée (si le langage a un opérateur ternaire ; sinon un if)
}
```

Symboles surchargeables après `operator` :
`+ - * / % .. == != < <= > >= -> <- <->`, ainsi que `[]` (lecture indexée),
`[]=` (écriture indexée), `()` (appel), `str` (conversion en texte affiché
par `print`) et `iter` (ce que parcourt `for ... in`).

Ordre de résolution d'un opérateur binaire : méthode d'instance de
l'opérande gauche → `static operator` de la classe de gauche → `static
operator` de la classe de droite → crochet `binary` d'un objet natif de
l'hôte. À défaut de `!=`, `>`, `<=`, `>=` définis explicitement, ils sont
**dérivés** de `==` et `<`.

### Génériques

Fonctions (`fn max<T>(...)`) et classes (`class Boîte<T>`) acceptent des
paramètres de type, éventuellement contraints :

```script
fn plusGrand<T extends Comparable>(a: T, b: T): T { ... }
```

`T` est lié explicitement à l'appel (`max<i32>(1, 2)`) ou déduit de la
première valeur rencontrée.

### Espaces de noms

```script
namespace geo {
    class Forme { fn aire() { return 0 } }
    let PI = 3.14159
}

let f = new geo.Forme()
print(geo.PI)
```

Le corps d'un `namespace` est une portée de **fonction** (un `var` n'en sort
pas) ; ses déclarations de premier niveau en deviennent les membres, lus
depuis l'extérieur par `nom.membre`. Rouvrir un espace de noms existant
**l'étend** (comme en TypeScript) au lieu de le remplacer.

### Asynchrone : `async`/`await`

```script
async fn charger(url) {
    let données = http.get(url)   # supposé fourni par l'hôte
    return données.length
}

let f = charger("...")     # rend immédiatement un Future<i32>
let taille = await f       # attend son résultat (bloquant pour ce fil)
f.then(fn(n) { print(n) }) # ou : callback à la complétion, sans bloquer
```

- Chaque appel d'une fonction `async` s'exécute sur son **propre fil** et
  rend aussitôt un `Future` (à la façon de Dart/Flutter).
- `await expr` attend le `Future` rendu par `expr` — appliqué à une valeur
  qui n'est pas un `Future`, `await` la rend telle quelle.
- Un `Future` non observé (jamais `await`é ni `.then()`é) dont l'exécution a
  échoué signale son erreur via `onAsyncError` côté hôte (jamais avalée
  silencieusement).
- Sur le fil principal, `Interpreter::PumpMainThread()` (côté hôte) traite
  les appels en attente des fils `async` — notamment tout ce qui touche à
  l'interface (`ui.*`) ou aux objets marqués `mainThreadOnly` côté hôte.

### Opérateurs de flux (`->`, `<-`, `<->`)

Trois opérateurs empruntés à l'idée de *pipes*, utilisables entre pipes,
fonctions, conteneurs, fichiers, ou une classe qui définit
`operator -> / <- / <->` :

```script
entrée -> fn(x) { return x * 2 } -> sortie   # une transformation insérée au milieu d'un flux
entrée <- 21                                 # sortie.receive() vaut alors 42
a <-> b                                      # liaison dans les deux sens
```

- `a -> b` : `a` s'écoule dans `b` (émission, appel de fonction en chaîne,
  connexion à un pipe).
- `a <- b` : `b` s'écoule dans `a` (attention à l'espace : `x < -1` compare à
  un négatif, `x<-1` est un flux).
- `a <-> b` : lien bidirectionnel.
- Les cycles (`a <-> b`, `a -> b -> a`) sont automatiquement coupés : une
  valeur ne repasse jamais par un pipe qu'elle a déjà traversé.

Exemple avec un conteneur : `conteneur -> fn` transforme chaque élément en un
nouveau `std.vector` de résultats (`nil` rendu par la fonction = élément
filtré) ; `conteneur -> print` affiche chaque élément.

---

## Bibliothèque standard

### Fonctions globales

`print`, `len`, `range`, `list`, `map`, et les autres fonctions historiques
sont installées par `InstallCoreGlobals` (appelée automatiquement par le
constructeur de `Interpreter` avec `InstallStdLibrary`).

### `std` — conteneurs, texte, fichiers, temps, concurrence

Chaque type `std.*` est un **type natif de l'hôte** (`HostType`) : typé
(`std.vector<i32>()` vérifie ses éléments), testable par `is`
(`v is std.vector`), parcourable (`for x in v`), et branchable sur un flux
(`v -> print`). Chaque objet porte son propre verrou : il se partage entre
fils `async` sans précaution particulière.

| Catégorie      | Types                                                                 |
|----------------|------------------------------------------------------------------------|
| Conteneurs     | `std.vector`, `std.list` (chaînée), `std.deque`                       |
| Adaptateurs    | `std.queue` (FIFO), `std.stack` (LIFO), `std.priority_queue`           |
| Tables         | `std.map` (ordonnée), `std.unordered_map`, `std.set`, `std.unordered_set` |
| Texte          | `std.string` (chaîne modifiable), `std.stream` (flux de texte)        |
| Flux           | `std.pipe` (canal entre fils, nœud des opérateurs `-> <- <->`)         |
| Système        | `std.file` (RAII), `std.filesystem`, `std.os`                         |
| Temps          | `std.datetime`, `std.date`, `std.time`                                |
| Valeurs        | `std.option`, `std.result` (`std.result.of(fn)` : le « try » sans exception) |
| Concurrence    | `std.future` (lance une fonction sur son propre fil), `std.mutex`, `std.lock_guard` (RAII) |
| Divers         | `std.sort`, et toutes les fonctions globales exportées (`std.print`…) |

Exemples caractéristiques :

```script
let v = std.vector<i32>()
v.push(1); v.push(2)
v -> print                       # émet chaque élément vers print

let f = std.file("notes.txt", "w")   # modes : r w a r+ w+ a+ (et b)
f.write_line("bonjour")
f <- "suite"                      # écrit via le flux

let d = std.date(2026, 9, 27)
d.add_days(10)
let n = std.datetime.now()        # heure locale ; std.datetime.utc_now() pour l'UTC
print(n.format("%d/%m/%Y %H:%M")) # strftime : %Y %m %d %H %M %S %A %B ...
print(std.date(2026, 12, 25) - d) # différence en jours

let r = std.result.of(fn() { return 10 / 0 })   # capture l'erreur plutôt que d'interrompre
if r.is_err() { print(r.unwrap_err()) }

let fut = std.future(fn() { return calcul_long() })  # lancé sur son propre fil
print(await fut)

let m = std.mutex()
m.with(fn() { /* section critique */ })
```

Remarques d'implémentation utiles à connaître :

- Aucune exception : `std.filesystem` s'appuie sur `std::error_code`, les
  flux tournent sans masque d'exceptions, et les tris avec un comparateur du
  script utilisent un **tri fusion maison** (un comparateur de script
  quelconque ne garantit pas l'ordre strict faible qu'exige `std::sort`).
- `std::regex` (qui lève sur un motif invalide) n'est volontairement **pas**
  enveloppé.
- `std.string`/`std.stream` travaillent en octets UTF-8 : `size()` compte des
  octets, `s[i]` rend un octet (sous forme de chaîne d'un caractère).

### `math` — mathématiques et algèbre linéaire

Enveloppe complète de `<cmath>`, `<numeric>`, `<bit>`, des fonctions
spéciales, et de la bibliothèque `math::` du dépôt :

```
<cmath>     sin cos tan asin acos atan atan2 sinh … atanh exp exp2 expm1
            log log2 log10 log1p logb ilogb pow sqrt cbrt hypot fma
            floor ceil round trunc nearbyint fract fmod remainder fmin fmax
            fdim copysign nextafter ldexp frexp modf erf erfc tgamma lgamma
            isnan isinf isfinite isnormal signbit abs sign
spéciales   beta riemann_zeta expint legendre hermite laguerre
            cyl_bessel_j sph_bessel (domaine vérifié : aucune exception)
<numeric>   gcd lcm midpoint sum product mean (+ factorial binomial is_prime)
<bit>       popcount countl_zero countr_zero bit_width has_single_bit
            rotl rotr byteswap bit_ceil bit_floor (sur la largeur du type)
utilitaires min max clamp lerp smoothstep rad deg random random_int
constantes  pi tau e phi sqrt2 … inf nan epsilon, min_i8 … max_u64, min_f32 …
classes     vec2 vec3 vec4 mat4 quat aabb plane ray, complex, random_engine
```

```script
let v = math.vec3(1, 2, 3)
v.x; v[0]; v + w; 2 * v; -v; v.dot(w); v.cross(w)

let m = math.mat4.translate(1, 0, 0) * math.mat4.rotate_y(math.pi_2)
let p = m * v                       # point transformé (vec3)

let q = math.quat.from_axis_angle(math.vec3(0, 1, 0), math.pi)
let z = math.complex(3, 4)          # z.abs() == 5
let r = math.random_engine(42)      # séquence reproductible, indépendante de math.random()
r.int(1, 6); r.normal(0, 1); r.shuffle(liste)
```

Les composantes des vecteurs/matrices sont en `f32` ; ces objets ont une
**sémantique de référence** (comme les listes) : `copy()` avant de modifier
si on veut préserver l'original.

### `ecs` — entités et composants

Vue depuis le script du système d'entités/composants du moteur
(`ecs::ArchetypeRegistry`). Un composant C++ est un type (qu'un script ne
peut pas déclarer) ; le script manipule donc soit ses propres composants
(valeur quelconque, rangée dans `ScriptComponents`), soit des composants C++
que l'hôte a explicitement exposés sous un nom (`EcsWorld::BindComponent`).

```script
let monde = ecs.world()                        # monde propre au script
let e = monde.spawn({pos: [0, 0], vit: [1, 0]})
monde.system("mouvement", ["pos", "vit"], fn(e, pos, vit, dt) {
    pos[0] += vit[0] * dt                       # listes/tables : passées par référence
})
monde.run(0.016)                                # exécute les systèmes, dans l'ordre déclaré
for e in monde.query("pos") { print(e.get("pos")) }

let scène = ecs.host()                          # le monde de l'hôte, s'il l'expose
```

Points d'attention :

- Les requêtes **matérialisent** la liste des entités avant d'appeler le
  script : un système peut créer ou détruire des entités sans invalider
  l'itération en cours.
- Un monde créé par le script a son propre verrou et se partage entre fils
  `async` ; le monde de l'**hôte**, lui, n'est manipulé que sur le fil
  principal.

### `ui` — interface utilisateur

Vue depuis le script du système d'interface du moteur (`ui::`). C'est
l'hôte qui décide où l'interface du script s'affiche (`UiBinding`).

```script
let menu = ui.column({anchor: "center", gap: 12, pad: 24, bg: "#101418e0", radius: 10})
ui.label("MON JEU", {parent: menu, font_size: 36, bold: true})
ui.button("Jouer", fn(b) { game.load_scene("Niveau 1") }, {parent: menu, w: 220})
let barre = ui.progress(0, 1, {parent: menu, w: 300})
barre.value = 0.5                     # ou barre.set_value(0.5)
```

Options communes (dernier argument, table facultative) : `parent`, `name`,
`w`/`h` (pixels, `"auto"`, `"grow"`, `"50%"`), `grow`, `pad`, `gap`, `bg`,
`color`, `border`, `radius`, `font_size`, `bold`, `align`
(`start|center|end|stretch`), `justify` (`start|center|end|between`),
`text_align` (`left|center|right`), `anchor` (`top_left`, `top`, …,
`center`, …, `bottom_right`), `offset` `[x, y]`, `absolute`, `fixed`,
`hidden`, `pointer_through`. Couleurs : `"#rrggbb"`, `"#rrggbbaa"` ou
`[r, g, b(, a)]` (0–1, ou 0–255 si une composante dépasse 1).

- **Sans écran** (hôte sans fenêtre, tests) : les widgets restent des objets
  ordinaires — texte, valeur et rappels fonctionnent, `w.click()` simule un
  clic. Le **même script** tourne donc en mode graphique et en mode
  « headless ».
- Les rappels déclenchés par l'utilisateur (clic, changement) sont **mis en
  file** puis exécutés par `UiBinding::Flush()`, appelée par l'hôte une fois
  par image, hors de la boucle d'évènements : un rappel peut détruire
  l'interface qui l'a appelé sans invalider ce qui était en cours de
  parcours.
- Toutes les fonctions `ui.*` s'exécutent sur le fil **principal** (un appel
  depuis un fil `async` y est automatiquement renvoyé).

### `gen` — génération procédurale

Vue depuis le script du module de génération procédurale (`generators::`).

```script
let bruit = gen.noise({type: "simplex", fractal: "ridged", octaves: 6, frequency: 0.01, seed: 7})
let carte = gen.heightmap(128, 128).fill(bruit).normalize().terrace(6, 0.5)
carte.erode({droplets: 20000}).save_pgm("relief.pgm", true)

let terrain = gen.terrain({width: 129, height: 129, seed: 3, island: 2, sea_level: 0.3})
let donjon = gen.dungeon({width: 41, height: 41, seed: 9})    # salles et labyrinthes
for ligne in donjon.rows() { print(ligne) }

let arbres = gen.poisson(100, 100, 4, 1, fn(x, y) { return carte.sample_uv(x / 100, y / 100) })
let plante = gen.lsystem("X", {X: "F[+X][-X]FX", F: "FF"}, 5)
let noms = gen.names(["aldoria", "belmora", "cendrial"], 2)
```

- Les options passées aux générateurs sont des tables `{clé: valeur}`,
  toutes facultatives ; une clé **inconnue** est une erreur (protection
  contre les fautes de frappe).
- Les objets générés (`gen.noise`, `gen.heightmap`, `gen.tilemap` — rendu
  par `dungeon`/`maze`/`bsp`/`caves` —, `gen.names`) portent leur propre
  verrou : on peut générer dans une fonction `async` pendant qu'un écran de
  chargement s'affiche.

---

## Gestion des erreurs

Script n'a **aucune exception**, ni côté langage ni côté implémentation C++ :

- Une erreur d'exécution interrompt normalement le script courant et remonte
  à l'hôte sous la forme d'un `ScriptError` (message + ligne + colonne),
  formaté par `ScriptError::Format()` (`"3:17: message"`).
- Pour capturer une erreur **sans** interrompre le script — l'équivalent
  d'un `try`/`catch` — utilisez `std.result.of(fn, arguments...)` : la
  fonction s'exécute, et son échec devient un `std.result` en erreur au lieu
  de se propager.
- Deux garde-fous protègent l'hôte d'un script qui boucle ou récurse sans
  fin : `Interpreter::maxSteps` (budget d'instructions par exécution) et
  `Interpreter::maxCallDepth` (profondeur d'appel). Les dépasser produit une
  **erreur d'exécution ordinaire**, jamais un gel de l'application ni un
  débordement de pile. Ils sont remis à zéro à chaque `Run()`.

---

## Intégration côté C++ (l'hôte)

### Créer l'interpréteur

```cpp
#include "data/script/script_std.hpp"    // std + fonctions globales
#include "data/script/script_math.hpp"   // math (optionnel)
#include "data/script/script_ecs.hpp"    // ecs (optionnel)
#include "data/script/script_ui.hpp"     // ui (optionnel)

data::script::Interpreter vm;
data::script::InstallStdLibrary(vm);   // fait par le constructeur si inclus
data::script::InstallMathLibrary(vm);  // idem
```

### Exécuter du code

```cpp
// Compile ET exécute ; les `let`/`fn` de premier niveau restent en globales,
// consultables/rappelables par la suite (utile pour on_update(dt) etc).
data::script::Result<data::script::Value, data::script::ScriptError> result = vm.Run(source);
if (result.IsError()) {
    std::printf("%s\n", result.Error().Format().CStr());
}

// Rappelle une fonction déclarée par le script si elle existe ; NONE si
// absente (distinct d'une erreur d'exécution réelle).
if (auto call = vm.CallGlobalIfPresent("on_update", {data::script::Value::Float(dt, ...)}); call.IsSome()) {
    if (call.Value().IsError()) { /* ... */ }
}
```

### Exposer des fonctions natives

```cpp
vm.RegisterNative("clamp01", /*minArity=*/1, /*maxArity=*/1,
    [](data::script::Interpreter &vm, std::vector<data::script::Value> &args)
        -> data::script::Result<data::script::Value, data::script::ScriptError> {
        double x = args[0].AsNumber();
        return data::script::Ok(data::script::Value::Number(std::clamp(x, 0.0, 1.0)));
    });
```

L'arité (`minArity`/`maxArity`, `maxArity < 0` = variadique) est vérifiée
**avant** l'appel : une fonction native n'a jamais à valider elle-même le
nombre de ses arguments.

### Organiser l'API par domaine (espaces de noms de l'hôte)

```cpp
vm.RegisterNamespacedNative("editor", "log", 1, 1, MaFonctionLog);
vm.RegisterNamespaceConstant("editor", "version", data::script::Value::Int(3));
```

Les membres d'un espace de noms de l'hôte sont des **constantes** : un
script ne peut pas réaffecter `editor.log` par accident.

### Déclarer un type natif complet (`HostType`)

Un `HostType` décrit un type de l'hôte comme `std.vector` : ses méthodes
(`methods`), ses membres de classe (`statics`), son constructeur
(`construct`), et des **crochets** optionnels par lesquels l'interpréteur
lui délègue les opérateurs, l'indexation, le parcours et les flux
(`get`/`set`, `binary`, `negate`, `index`/`setIndex`, `items`/`size`,
`display`, `equals`, `flowIn`/`flowOut`/`link`, `call`). Un crochet absent
signifie que l'opération correspondante n'est pas supportée (erreur
explicite, jamais un plantage).

```cpp
using namespace data::script;

lib::TypeBuilder builder("mon_jeu.entité");
builder.Method("nom", 0, 0, [](Interpreter&, const HostRef& self, lib::Args&) {
    return Ok(Value::String(lib::As<MonEntiteObject>(self).nom));
});
vm.RegisterHostType("mon_jeu", "entité", builder.type);
```

### Bases dérivables par les scripts (owners)

Une base dont les scripts **dérivent** (cf. [Bases fournies par l'hôte](#bases-fournies-par-lhôte-owners))
est un `HostType` marqué `isOwner`, dont chaque instance de script reçoit un
état C++ : une classe dérivée d'`OwnerObject`
(`data/script/script_owners.hpp`). Ses `OnInit` / `OnDeinit` sont le
constructeur et le destructeur de la base ; `OwnerTypeBuilder<T>` câble les
méthodes, qui reçoivent directement `T&` (déjà vérifié construit et non
détruit) :

```cpp
struct CompteurOwner : data::script::OwnerObject {
    int valeur = 0;
    Option<ScriptError> OnInit(Interpreter&, std::vector<Value>& args) override {
        if (!args.empty() && args[0].IsNumber()) valeur = int(args[0].AsNumber());
        return NONE;                          // Some(erreur) annule la construction
    }
    void OnDeinit(Interpreter&) override { /* libérer la ressource */ }
};

data::script::OwnerTypeBuilder<CompteurOwner> compteur;   // fabrique par défaut : make_shared<T>()
compteur.Method("incrementer", 0, 0, [](Interpreter&, CompteurOwner& self, std::vector<Value>&) {
    return Result<Value, ScriptError>(Ok(Value::Number(++self.valeur)));
});
compteur.Property("valeur", [](const CompteurOwner& self) { return Value::Number(self.valeur); });
vm.RegisterHostType("jeu", "Compteur", compteur.Type());  // `class X extends jeu.Compteur {}`
```

Côté hôte :

- `vm.Owners()` est le registre (`Snapshot`, `Count`, `Describe` pour un
  affichage qui n'exécute aucun script, `DestroyAll`) ;
  `vm.DestroyInstance(instance)` joue la séquence RAII d'une instance ;
  le destructeur de l'interpréteur appelle `DestroyAll` tant qu'il fonctionne
  encore.
- `OwnerObject::Sibling<T>()` donne une autre base de la même instance (un
  corps physique retrouve le nœud créé par la base « maillage »).
- `vm.ClassesDerivedFrom("jeu.Compteur")` liste les classes **feuilles**
  (ni abstraites, ni parentes d'une autre) déclarées par l'interpréteur qui
  dérivent de la base — c'est ainsi qu'un moteur trouve « la » classe qu'un
  script définit ; `vm.CallMethodIfPresent(objet, "on_update", args)`
  appelle un rappel facultatif.
- Une base est `mainThreadOnly` : sa construction depuis un fil `async` est
  refusée, ses méthodes y sont renvoyées au fil principal.

### Composants ECS et widgets d'interface exposés par l'hôte

```cpp
data::script::EcsWorld hostWorld(monRegistre);
hostWorld.BindComponent<Transform>("transform",
    [](const Transform &t) { return MakeVec3(vm, t.position); },
    [](const Value &v, Transform &t) -> Option<String> { /* ... */ return NONE; });

data::script::InstallEcsLibrary(vm, std::shared_ptr<EcsWorld>(&hostWorld, [](EcsWorld*){}));
```

### Threads et concurrence

- `Interpreter::NativeThread::DEFAULT` : l'API de l'hôte tourne sur le fil
  principal ; la bibliothèque standard tourne sur n'importe quel fil.
- `MAIN` : toujours sur le fil principal ; appelée depuis un fil `async`,
  elle y est **mise en file**.
- `ANY` : exécutée sur le fil de l'appelant (la native doit être sûre entre
  fils).
- L'hôte doit appeler régulièrement `Interpreter::PumpMainThread()` pour
  traiter les appels des fils `async` en attente sur le fil principal
  (notamment tout ce qui touche à `ui.*`).

### Pont avec les données (`data::Node`)

```cpp
data::script::Value valeurScript = data::script::ValueFromNode(nodeJsonDéjàChargé);
data::script::NodePtr nodeReencodable = data::script::NodeFromValue(valeurScript);
```

Tout document JSON/YAML/TOML/XML déjà chargé par les codecs `data::`
devient directement lisible/manipulable depuis un script, et l'inverse,
sans couche d'adaptation. Une fonction (utilisateur ou native) n'a pas de
représentation de données et devient `NONE` (comme `JSON.stringify` ignore
une fonction) ; un nombre entier repart en `INT` (pas de perte
d'information à l'aller-retour script → JSON → script).

---

## Modèle mémoire et limites connues

- **Pas de ramasse-miettes.** Les valeurs du script sont gérées par
  `shared_ptr`/`unique_ptr` classiques ; une fonction capture son
  environnement d'appel par `shared_ptr`, qui contient à son tour la
  fonction dès qu'elle s'y nomme — un **cycle** de références. L'interpréteur
  garde un registre faible de tous les environnements créés et les vide à sa
  destruction, ce qui casse tous les cycles d'un coup.
- **Limite assumée** : la mémoire d'une fermeture cyclique créée dans une
  boucle n'est récupérée qu'à la destruction de l'interpréteur, pas avant
  que le programme ne se termine ou ne relance un nouveau `Run()`.
- Chaque objet natif de l'hôte (`std.vector`, `math.vec3`, un fifo...) porte
  son propre verrou : il se partage donc entre fils `async` sans précaution
  supplémentaire côté script.
- Les erreurs de compilation/exécution portent toujours leur position source
  (`ligne:colonne`) pour rester exploitables telles quelles dans un éditeur.

---

## Organisation des fichiers du module

| Fichier                    | Rôle                                                                 |
|-----------------------------|-----------------------------------------------------------------------|
| `script_lexer.hpp/.cpp`     | Analyse lexicale (`Lexer`, `Token`, `ScriptError`)                    |
| `script_ast.hpp/.cpp`       | Arbre syntaxique (`Expr`, `Stmt`, `ClassDef`, `FunctionDef`...)       |
| `script_parser.hpp/.cpp`    | Analyse syntaxique : source → `Program`                               |
| `script_value.hpp/.cpp`     | Représentation des valeurs à l'exécution (`Value`, `HostType`, `Future`, arithmétique typée) |
| `script_interpreter.hpp/.cpp` | Interpréteur (parcours d'arbre), `Environment`, API `Interpreter` |
| `script_owners.hpp/.cpp`    | Bases fournies par l'hôte : `OwnerObject`, `OwnerTypeBuilder`, registre, séquence RAII |
| `script_std.hpp/.cpp`       | Fonctions globales et espace de noms `std`                            |
| `script_math.hpp/.cpp`      | Espace de noms `math`                                                 |
| `script_ecs.hpp/.cpp`       | Espace de noms `ecs`                                                  |
| `script_ui.hpp/.cpp`        | Espace de noms `ui`                                                   |
| `script_generator.hpp/.cpp` | Espace de noms `gen`                                                  |

Chaque `.cpp` est généré à partir de l'en-tête correspondant par
`splitter.py` : le code des méthodes définies inline dans le `.hpp` est
déplacé tel quel dans le `.cpp` (seules les signatures sont réécrites). Les
deux fichiers sont donc à considérer comme un **seul module** par
sous-domaine.

---

## Résumé express (aide-mémoire)

```script
# variables
let x = 1; var y = 2; const Z = 3

# fonctions
fn f(a, b) { return a + b }
async fn g() { return await std.future(fn() { return 42 }) }

# contrôle
if x > 0 { } else { }
while x < 10 { x += 1 }
for i in range(10) { }

# classes
interface I { fn m() }
class C implements I {
    let champ = 0
    fn init(v) { this.champ = v }
    override fn m() { return this.champ }
    operator +(o) { return new C(this.champ + o.champ) }
}

# flux
pipe_a -> fn(x) { return x * 2 } -> pipe_b
pipe_a <- 21

# collections
let v = std.vector<i32>(); v.push(1)
let t = {clé: "valeur"}

# erreurs sans exception
let r = std.result.of(fn() { return risqué() })
if r.is_err() { print(r.unwrap_err()) }
```