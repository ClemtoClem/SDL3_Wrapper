# core — types de base

`libs/foundation/core` · couche **foundation** · bibliothèque statique `libcore.a`

## Présentation

`core` fournit les types sur lesquels repose tout le reste du dépôt. Le module
n'a aucune dépendance : ni SDL, ni autre module.

- `String` / `StringView` : chaîne UTF-8 possédante, avec son propre tampon (elle ne repose pas sur `std::string`), et vue non possédante.
- `Option<T>` et `Result<T, E>` : gestion des absences et des erreurs **sans exception**. C'est la convention de tout le dépôt.
- `Ref<T>` / `RefMut<T>` : emprunts non propriétaires, explicites dans les signatures.
- `Wrapper<T, Deleter>`, `DeviceWrapper`, `Borrowed<T>` : enveloppes RAII de pointeurs C, base de tous les wrappers SDL de `sdl3`.
- `Uuid` : identifiant stable de 128 bits (aléatoire ou dérivé d'un nom).
- `unicode::` : validation, décodage et conversions UTF-8, UTF-16 et UTF-32.

> **Espace de noms** : ces types sont déclarés dans l'espace de noms
> **global** (`String`, `Option`, `Result`, `Uuid`…). L'espace `core` existe
> (`core.hpp`) mais il est vide. Les fonctions Unicode sont dans `unicode::`.

## Architecture interne

| Fichier | Contenu |
|---|---|
| `include/core/core.hpp` | En-tête parapluie (inclut tous les autres) |
| `string.hpp`, `src/string.cpp` | `String`, littéral `"…"_s`, `std::hash<String>` |
| `string_view.hpp`, `src/string_view.cpp` | `StringView` |
| `string_unicode.hpp`, `src/string_unicode.cpp` | `unicode::` (validation, itération par codepoint, UTF-16/32, casse) |
| `option.hpp` | `NoneType`/`NONE`, `Some<T>`, `Option<T>`, `BadOptionAccess` |
| `result.hpp` | `Ok<T>`, `Err<E>`, `Result<T, E>`, `Overloaded` |
| `interop.hpp` | Conversions croisées `Option::OkOr` / `Result::Ok()` / `ErrorOption()` |
| `ref.hpp` | `Ref<T>`, `RefMut<T>` |
| `wrapper.hpp` | `Wrapper`, `DeviceWrapper`, `Borrowed`, `detail::ToRawHandles` |
| `uuid.hpp`, `src/uuid.cpp` | `Uuid` |

Dépendances : uniquement la bibliothèque standard C++23.

## API publique

### `String`

Les méthodes travaillent en **octets** par défaut. Les variantes Unicode
portent le préfixe `U` : `ULength`, `UCharAt`, `UToLower`, `UReverse`,
`UTrim`, `UChars`. On peut aussi itérer sur `Codepoints()`.

| Catégorie | Méthodes principales |
|---|---|
| Construction | `String()`, depuis `const char*`, `char8_t*` (`u8"…"`), `StringView`, `char`, `(count, c)`, `std::string` (interop) ; littéral `"abc"_s` |
| Fabriques | `From(entier, base)`, `From(double, décimales)`, `From(bool)`, `FromStream(args…)` (format d'un `ostringstream`), `Format(fmt, args…)` (style `printf`), `FromUtf16`, `FromUtf32` |
| Fichiers | `static Option<String> ReadFile(path)` (`NONE` si illisible), `bool WriteFile(path) const` |
| Accès | `CStr()`, `View()`, `GetSize()`/`Length()`, `IsEmpty()`, `CharAt`, `Front`, `Back`, itérateurs |
| Recherche | `Find`, `Rfind` (rendent `NPOS`), `IndexOf`/`LastIndexOf` (rendent `-1`), `Contains`, `StartsWith`, `EndsWith`, `Count`, `GetBetween(open, close)` → `Option<String>` |
| Extraction | `Substr(pos, len)`, `Substring(début, fin)`, `Split(délim, maxSplits)`, `SplitAny`, `Lines`, `Chars`, `static Join(parts, sep)` |
| Transformation (copie) | `ToLower`/`ToUpper` (ASCII), `UToLower`/`UToUpper`, `Trim`/`TrimLeft`/`TrimRight`, `Replace`, `Remove`, `Insert`, `Repeat`, `PadLeft`/`PadRight`/`PadCenter`, `Truncate`, `IndentLines` |
| Transformation (en place) | `Append`, `+=`, `Concat`, `PushBack`, `Clear`, `TrimInplace`, `ReplaceInplace`, `RemoveInplace`, `ToLowerCase`/`ToUpperCase`, `Reserve` |
| Fonctionnel (codepoints) | `Map`, `Filter`, `FlatMap`, `Reduce`, `Any`, `All`, opérateur `|` (`s | fn` ≡ `fn(s)`) |
| Tests | `IsAscii`, `IsValidUtf8`, `IsNumeric`, `IsAlpha`, `IsAlnum`, `IsWhitespace`, `Equals`, `EqualsIgnoreCase`, `Compare` |
| Conversion | `ToInt32`, `ToInt64`, `ToUint64`, `ToFloat`, `ToDouble` (sans contrôle), `TryParseInt(base)` → `Option<int64_t>`, `TryParseDouble()` → `Option<double>` |
| Unicode | `ULength`, `UCharAt`, `UByteOffset`, `Codepoints()`, `ToCodepoints`, `ToUtf16`, `ToUtf32` |

`String::Hash` et la spécialisation `std::hash<String>` permettent d'utiliser
`String` comme clé de `std::unordered_map`.

> Le commentaire d'en-tête de `string.hpp` montre des noms en minuscules
> (`trim()`, `toLower()`). L'API réelle est en *PascalCase* : `Trim()`,
> `ToLower()`.

### `StringView`

Vue `(const char*, taille)` constexpr : `GetData`, `GetSize`, `IsEmpty`,
`Substr`, `RemovePrefix`/`RemoveSuffix`, `Find`/`Rfind`, `StartsWith`,
`EndsWith`, `Contains`, `Compare`. Une `String` se convertit en vue par
`View()`.

### `Option<T>`

- Construction : `Some(valeur)`, `NONE` ou `Option<T>(std::in_place, args…)`.
  Il n'y a **pas** de conversion implicite depuis une valeur nue : on écrit
  `return Some(42);`, ou `Option<uint32_t>(Some(1234u))` pour un argument.
- Tests : `IsSome()`, `IsNone()`, `explicit operator bool`.
- Accès :
  - `*opt` et `->` sont `noexcept` et appellent `std::abort()` si l'option est
    vide ;
  - `Value()` lève `BadOptionAccess`, la seule exception du module ;
  - `ValueUnchecked()` ne vérifie rien ;
  - `Unwrap()`, `UnwrapOr(défaut)` et `UnwrapOrElse(fn)` rendent la valeur.
- Combinateurs :
  - `Map(fn)`, `AndThen(fn)` ;
  - `Match(siPrésent, siAbsent)` ;
  - `OkOr(err)` et `OkOrElse(fn)` convertissent en `Result`.

### `Result<T, E>`

- Construction : `return Ok(v);` ou `return Err(e);`. Les guides de déduction
  rendent ces deux formes implicites. Fabriques : `Result::MakeOk(args…)` et
  `MakeErr(args…)`.
- Tests : `IsOk()`, `IsError()`, `explicit operator bool`.
- Accès :
  - **par référence** : `Value()` et `Error()`. Ils sont `noexcept` et
    appellent `std::abort()` si le résultat n'est pas dans le bon état. C'est
    l'idiome du dépôt : `auto &win = winRes.Value();`.
  - **par extraction** : `Unwrap()`, `UnwrapOr(défaut)` et `UnwrapOrElse(fn)` ;
    `UnwrapError()` pour l'erreur, qui n'existe que sur une rvalue ;
  - **conversion en option** : `Ok()` → `Option<T>`, `ErrorOption()` →
    `Option<E>`.

> **Types non copiables.** `Unwrap()` appelé sur une lvalue *copie* la
> valeur, et la compilation échoue pour un type déplaçable seulement : une
> `sdl3::Window`, une `Texture`, tous les `Wrapper`. Il faut alors écrire
> `std::move(res).Unwrap()`, ou garder le `Result` et utiliser
> `res.Value()`.
- Combinateurs :
  - `Map`, `MapErr` ;
  - `AndThen`, `OrElse` : l'erreur se propage sans branchement manuel ;
  - `Match(siOk, siErr)` : le traitement doit couvrir les deux cas.

`using Error = StringView;` (dans `interop.hpp`) est le type d'erreur léger
utilisé par certains modules.

### `Ref<T>`, `RefMut<T>`

Ce sont des emprunts non propriétaires, plus explicites dans une signature
qu'un `const T*` ou un `T&`.

- On y accède par `->`, `*` ou `Get()`.
- La construction depuis une valeur temporaire est interdite à la
  compilation, pour éviter les références pendantes.
- `Ref<T*>` est refusé par `static_assert`.

### `Wrapper<T, Deleter>`, `DeviceWrapper<D, T, Deleter>`, `Borrowed<T>`

- **`Wrapper<T, Deleter>`** : possède un `T*` C et appelle `Deleter` à sa
  destruction. Il est déplaçable mais pas copiable. Méthodes : `Get()`,
  `Release()`, `Reset()`, `operator bool`.
- **`DeviceWrapper<D, T, Deleter>`** : même chose pour une ressource qui doit
  être libérée avec son périphérique (par exemple les objets SDL_GPU).
- **`Borrowed<T>`** : vue non possédante d'une ressource C, qu'elle ne libère
  jamais.

Ces trois classes servent de base aux classes de `sdl3`. Ainsi, aucun pointeur
C brut n'apparaît dans les signatures publiques.

### `Uuid`

| Membre | Rôle |
|---|---|
| `static Uuid Nil()` | L'identifiant nul (« aucun ») |
| `static Uuid Generate()` | Aléatoire (version 4) |
| `static Uuid FromName(espace, nom)` | Déterministe : même espace et même nom → même Uuid (version 5, hachage 128 bits non cryptographique) |
| `static Option<Uuid> Parse(texte)` | Forme canonique, tirets facultatifs, casse indifférente |
| `String ToString() const` | `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` en minuscules |
| `IsNil()`, `Version()`, `==`, `<`, `std::hash<Uuid>` | Comparaisons et utilisation comme clé |

### `unicode::`

Fonctions principales :

- `IsValidUtf8`, `CodepointCount`, `DecodeNext`, `EncodeUtf8` ;
- `CodepointIterator`, `CodepointView` ;
- `FromUtf16`, `FromUtf32`, `ToUtf16`, `ToUtf32` ;
- `CodepointAt`, `ByteOffsetOf` ;
- `ToLowerCp`, `ToUpperCp`, `IsUnicodeSpace`.

## Fonctionnement

- **Sans exception** : les modules du dépôt signalent leurs erreurs par
  `Result<T, String>`, et les absences par `Option<T>`. Les seules exceptions
  possibles viennent de `Option::Value()` sur une option vide et de la
  bibliothèque standard.
- **Octets ou codepoints** : `GetSize()`, `Substr()` et `Find()` comptent en
  octets. Pour un texte non ASCII, il faut passer par les variantes `U…` ou par
  `Codepoints()`.
- **Interopérabilité** : `String(const std::string&)` copie les octets. Cela
  permet d'accepter le résultat d'une API standard sans garder de dépendance
  de stockage.

## Exemple

```cpp
#include <cstdio>
#include <core/core.hpp>

Result<int, String> ParsePort(const String &text) {
	Option<int64_t> n = text.Trim().TryParseInt();
	if (n.IsNone())
		return Err(String("port invalide : ") + text);
	if (*n < 1 || *n > 65535)
		return Err(String::Format("port hors limites : %lld", static_cast<long long>(*n)));
	return Ok(static_cast<int>(*n));
}

int main() {
	String line = "  host=example.org ; port=8080  ";
	for (const String &part : line.Trim().Split(';')) {
		std::vector<String> kv = part.Trim().Split('=', 1);
		if (kv.size() == 2)
			std::printf("%s -> %s\n", kv[0].CStr(), kv[1].CStr());
	}

	Result<int, String> port = ParsePort(" 8080 ");
	String msg = port.Match([](int p) { return String::From(p); }, [](const String &e) { return e; });
	std::printf("port : %s\n", msg.CStr());
	std::printf("défaut : %d\n", ParsePort("abc").UnwrapOr(80));

	String accents = u8"Héllo 🌍";
	std::printf("%zu octets, %zu codepoints\n", accents.GetSize(), accents.ULength()); // 11, 7

	Uuid space = Uuid::Generate();
	Uuid a = Uuid::FromName(space, "assets/textures/stone.png");
	Uuid b = Uuid::FromName(space, "assets/textures/stone.png");
	std::printf("%s (stable : %d)\n", a.ToString().CStr(), a == b); // stable : 1

	Option<String> file = String::ReadFile("cpp-maker.toml");
	std::printf("%zu lignes\n", file.Map([](const String &s) { return s.Lines().size(); }).UnwrapOr(0));
}
```

## Intégration

Dans le `module.toml` d'un module :

```toml
[dependencies]
modules = ["core"]
```

Puis `#include <core/core.hpp>` (ou l'en-tête précis). Le module n'a rien à
initialiser.

## Tests

Les tests sont dans `libs/foundation/core/tests/` : `test_core.cpp` (chaînes,
`Option`, `Result`) et `test_uuid.cpp`. On les lance par :

```sh
cpp-maker test -m core
```

Le gabarit de test `tests/test.hpp`, généré par cpp-maker, est décrit dans
[compilation.md](compilation.md#tests).
