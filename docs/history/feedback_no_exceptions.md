---
name: feedback-no-exceptions
description: "Never use C++ exceptions anywhere in this repo (lib/, tests/, examples/) — every fallible operation returns core::Result<T,E> or Option<T>."
metadata:
  node_type: memory
  type: feedback
---

Aucune exception, jamais : ni `throw`, ni `try`/`catch`, ni appel à une API
standard qui lève (`std::stod`, `std::stoi`, `vector::at`, `std::get` sur un
`variant` du mauvais type, `make_shared` compris lorsqu'un chemin d'erreur
doit rester observable). Toute opération faillible retourne
`Result<T, E>` (core/result.hpp) ou `Option<T>` (core/option.hpp).

**Why:** rappel explicite de l'utilisateur pendant l'implémentation de
l'éditeur de niveau (2026-09-12), alors que j'attaquais un interpréteur de
script — un domaine où le réflexe habituel est de propager les erreurs de
parsing/exécution par exception. C'est aussi la règle déjà écrite en tête de
`lib/include/data/data.hpp` ("Aucune exception n'est levée par ce module") et
suivie par tout le reste de `lib/`.

**How to apply:**
- Parsing/exécution : un type d'erreur porteur de contexte (ligne/colonne/
  message) + `Result<T, ScriptError>` ; jamais de `throw` pour dérouler la
  pile — on remonte le `Result` explicitement (voir
  `data::script::Interpreter`, [[project-script-language]]).
- Conversion de chaînes : `String::TryParseInt` / `String::TryParseDouble`
  (retournent `Option`), jamais `std::stod`/`std::stoll`.
- Accès indexé : tester la taille puis indexer, jamais `.at()`.
- `std::variant` : `std::holds_alternative` + `std::get_if`, jamais
  `std::get<T>` (qui lève `bad_variant_access`).
- Ce dépôt compile déjà sans `-fexceptions` désactivé, mais la règle est
  sémantique, pas une question de drapeau de compilation.

Related: [[feedback-wrap-c-pointers]] (l'autre règle de style imposée par
l'utilisateur sur ce dépôt).
