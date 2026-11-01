# Tests d'intégration et fichiers de suppression

`tests/` · module `integration` (type `header-only`, aucune source, seulement `tests/`)

## Tests d'intégration (`tests/integration`)

Chaque module a ses propres tests unitaires dans son dossier `tests/`. Le
module `integration` regroupe les tests qui **composent plusieurs modules**.
Ces tests ne peuvent pas vivre dans un module de bas niveau sans lui imposer
une dépendance.

Le premier cas couvert : un `resources::ResourceCache` et son
`AssetCatalog`, peuplés par les fabriques de ressources de
[render3d](libs_infrastructure_render3d.md) (maillages, textures, matériaux)
et d'[audio](libs_infrastructure_audio.md) (sons). C'est la composition que
fait une application, et c'est ainsi que
[resources](libs_infrastructure_resources.md) peut rester indépendant de
render3d et d'audio.

| Test | Vérifie |
|---|---|
| `AssetCatalog_JsonRoundTrip` | Le catalogue survit à un aller-retour JSON |
| `ResourceCache_PrimitivesAreSharedNotCopied` | Deux références à la même primitive partagent la même ressource |
| `ResourceCache_FilesLoadAsynchronously` | Les fichiers se chargent en arrière-plan |
| `ResourceCache_MissingReferencesAreReportedNotFatal` | Une référence inconnue est recensée, sans faire échouer le chargement |
| `ResourceCache_MaterialsAreSharedAndReloadFromTheCatalog` | Les matériaux sont partagés et recréés depuis le catalogue |

```sh
cpp-maker test -m integration
```

Dépendances déclarées : `resources`, `render3d`, `audio`.

## Fichiers de suppression des sanitizers

Les fuites et les courses de données **internes aux bibliothèques tierces**
(pilotes X11 et Mesa, dbus, SDL_net) ne viennent pas du code du dépôt et ne
peuvent pas y être corrigées. Elles sont ignorées par des fichiers de
suppression, que `cpp-maker.toml` transmet automatiquement aux exécutables
de test (`[tests.env]`, où `{root}` désigne la racine du dépôt) :

| Fichier | Variable | Outil |
|---|---|---|
| `tests/lsan_suppressions.txt` | `LSAN_OPTIONS=suppressions=…` | LeakSanitizer (modes `asan`, `lsan`, `--sanitize address`) |
| `tests/tsan_suppressions.txt` | `TSAN_OPTIONS=suppressions=…` | ThreadSanitizer (`tsan`) |
| `tests/valgrind.supp` | `VALGRIND_OPTS=--suppressions=…` | Valgrind (mode `valgrind`) |

Pour lancer un binaire hors de `cpp-maker test` avec les mêmes suppressions :

```sh
LSAN_OPTIONS=suppressions=tests/lsan_suppressions.txt ./build/host/asan/bin/<nom>
```

Voir aussi [compilation](compilation.md#sanitizers-et-vérifications).
