# sql — base de données embarquée

`libs/infrastructure/sql` · couche **infrastructure** · `libsql.a` · espace de noms `sql::`

## Présentation

`sql` est une petite base de données en mémoire, construite sur l'arbre
[`dataio::Node`](libs_infrastructure_dataio.md), sans aucune dépendance
externe. Elle se compose de quatre parties :

1. **Persistance de projets** : enregistrer et recharger un arbre
   `dataio::Node` en JSON (`SaveProjectJson`, `LoadProjectJson`).
2. **Magasin de lignes** `Table<Row>` : `Insert`, `Get`, `Delete` et
   `Query(prédicat)`, avec des identifiants stables malgré les suppressions,
   et une persistance en CSV. On s'en sert pour des listes de fichiers
   récents ou un catalogue.
3. **Moteur SQL**, sur ce même magasin :
   - un lexeur écrit à la main et un parseur par descente récursive ;
   - les expressions `WHERE`, évaluées par shunting-yard puis notation
     polonaise inverse ;
   - une classe `Database` qui exécute le SQL ;
   - un `QueryBuilder` fluent qui évite d'écrire du texte SQL.
4. **Stockage binaire** de toute une base (contrôle CRC-32) et **index
   B+arbre** secondaires (`CreateIndex`, `LookupByIndex`).

Le module est utilisé par [voxel](libs_infrastructure_voxel.md) (sauvegardes)
et par les scripts (`sql.*` dans
[script_bindings](libs_infrastructure_script_bindings.md)).

### Sous-ensemble SQL accepté

| Instruction | Forme |
|---|---|
| `CREATE TABLE t (col TYPE [PRIMARY KEY], …)` | Types : `INT`/`INTEGER`, `FLOAT`/`REAL`/`DOUBLE`, `STRING`/`TEXT`/`VARCHAR`, `BOOL`/`BOOLEAN` ; une seule clé primaire |
| `INSERT INTO t VALUES (…)` | **Une ligne par instruction** (pas de `VALUES (…), (…)`) ; un INT est converti en FLOAT si la colonne l'exige |
| `SELECT * | col, … FROM t [WHERE …]` | `WHERE` : `=`, `<>` / `!=`, `<`, `<=`, `>`, `>=`, `AND`, `OR`, `NOT`, parenthèses |
| `DELETE FROM t [WHERE …]` | |
| `DROP TABLE t` | |

Ne sont **pas** pris en charge : `UPDATE`, `ORDER BY` (refusé avec un
message explicite), les jointures et les agrégats. `Upsert`, `FindByKey` et
`DeleteByKey` remplacent `UPDATE` pour les tables à clé primaire.

## Architecture interne

| En-tête | Contenu |
|---|---|
| `sql.hpp` | En-tête parapluie et présentation |
| `token.hpp`, `parser.hpp`, `statement.hpp`, `expression.hpp` | Lexeur, parseur, AST (`std::variant`), expressions `WHERE` |
| `schema.hpp` | `ColumnDef`, `Schema` |
| `database.hpp` | `QueryResult {schema, rows, message}`, `Database` |
| `query_builder.hpp` | `QueryBuilder` |
| `table.hpp`, `query.hpp` | `Table<Row>`, combinateurs de prédicats |
| `storage.hpp` | JSON de projet, CSV de tables |
| `binary_storage.hpp` | `SaveDatabaseBinary`, `LoadDatabaseBinary`, `Crc32` |
| `btree_index.hpp` | `BTreeIndex` (B+arbre clé scalaire → identifiants de lignes) |

Dépendances : `core`, `sdl3` (fichiers), `dataio`.

## API publique

### `Database`

| Membre | Rôle |
|---|---|
| `Result<QueryResult, String> Execute(sql)` | Analyse et exécute une instruction ; l'erreur nomme ce qui était attendu |
| `ExecuteSelectStatement(stmt)` | SELECT déjà structuré (utilisé par `QueryBuilder`) |
| `TableNames()`, `GetSchema(table)` | Description |
| `CreateTable(table, Schema)`, `InsertRow(table, Row)` | Accès structuré, sans texte |
| `Upsert(table, Row)`, `FindByKey(table, clé)`, `DeleteByKey(table, clé)` | Par clé primaire |
| `CreateIndex(table, colonne)`, `LookupByIndex(table, colonne, valeur)` | Index B+arbre |

`Row` est un `std::vector<dataio::NodePtr>`, dans l'ordre des colonnes.

> **Index figés.** `CreateIndex` prend un **instantané** de la table : une
> ligne insérée ensuite n'apparaît pas dans `LookupByIndex` tant que l'index
> n'a pas été reconstruit (nouvel appel à `CreateIndex`).

### `QueryBuilder`

`QueryBuilder().Select({cols}).From(table).Where(col, op, valeur).Execute(db)`
réutilise exactement l'évaluation `WHERE` de `Database`.

### Persistance

- `SaveDatabaseBinary(chemin, db)` et `LoadDatabaseBinary(chemin)` →
  `Result<Database, String>` : format binaire propre au module, avec CRC.
- `SaveTableCsv` et `LoadTableCsv`, pour un `Table<Row>`.
- `SaveProjectJson` et `LoadProjectJson`, pour un arbre `dataio::Node`.

## Exemple

```cpp
#include <cstdio>
#include <dataio/document.hpp>
#include <sql/binary_storage.hpp>
#include <sql/database.hpp>
#include <sql/query_builder.hpp>

static void Print(const Result<sql::QueryResult, String> &r) {
	if (r.IsError()) {
		std::printf("erreur : %s\n", r.Error().CStr());
		return;
	}
	for (const sql::Row &row : r.Value().rows) {
		for (const dataio::NodePtr &cell : row)
			std::printf("%s  ", dataio::scalar::ToString(cell).CStr());
		std::printf("\n");
	}
}

int main() {
	sql::Database db;
	(void)db.Execute("CREATE TABLE joueur (id INT PRIMARY KEY, nom STRING, niveau INT, score FLOAT)");
	(void)db.Execute("INSERT INTO joueur VALUES (1, 'Ada', 12, 1520.5)");
	(void)db.Execute("INSERT INTO joueur VALUES (2, 'Bob', 7, 830.0)");
	(void)db.Execute("INSERT INTO joueur VALUES (3, 'Cyd', 15, 2210.0)");

	// SQL texte : WHERE avec opérateurs de comparaison et logiques
	Print(db.Execute("SELECT nom, score FROM joueur WHERE niveau >= 10 AND score > 1000"));

	// Même requête sans texte SQL : le QueryBuilder
	Print(sql::QueryBuilder().Select({"nom"}).From("joueur").Where("niveau", "<", dataio::Node::MakeInt(10)).Execute(db));

	// Erreur positionnée plutôt qu'exception
	Print(db.Execute("SELECT age FROM joueur"));

	// Persistance binaire (CRC vérifié au chargement)
	if (sql::SaveDatabaseBinary("joueurs.db", db)) {
		auto loaded = sql::LoadDatabaseBinary("joueurs.db");
		std::printf("rechargée : %zu table(s)\n", loaded ? loaded.Value().TableNames().size() : 0);
	}
	return 0;
}
```

Sortie :

```
Ada  1520.5
Cyd  2210
Bob
erreur : sql: unknown column 'age' in SELECT
rechargée : 1 table(s)
```

## Intégration

`modules = ["sql"]`, puis `#include <sql/database.hpp>`, ou `<sql/sql.hpp>`
pour tout le module.

La base vit en mémoire. Pour la conserver, appelez
`SaveDatabaseBinary`, par exemple à chaque sauvegarde de partie.

## Tests

```sh
cpp-maker test -m sql
```

Les tests couvrent le lexeur et le parseur, l'exécution, les clés primaires,
le stockage binaire et le B+arbre (y compris la suppression).
