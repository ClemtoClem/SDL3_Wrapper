#pragma once
/**
 * script — base de données embarquée (`sql::`) vue depuis le langage
 * `Script`, espace de noms `sql`.
 *
 *   let db = sql.database()             # ou sql.host() si l'hôte en fournit une
 *   db.execute("CREATE TABLE t (id INT PRIMARY KEY, nom STRING)")
 *   db.insert("t", {id: 1, nom: "a"})
 *   db.upsert("t", {id: 1, nom: "b"})
 *   for (row in db.select("t")) { print(row.id, row.nom) }
 *   print(db.find("t", 1))              # { id: 1, nom: "b" }
 *   db.delete("t", 1)                   # clé primaire
 *   db.execute("DELETE FROM t WHERE id > 5")   # SQL brut
 *
 * Une ligne (`sql.row`) est une table Lua-like dont les champs sont les
 * noms de colonnes. `sql.database()` crée une base possédée par le script ;
 * `sql.host()` renvoie la base de l'hôte si l'installation en a une.
 */
#include <sql/database.hpp>
#include <script/script_std.hpp>

namespace script {

/// Installe `sql.database` (toujours) et `sql.host` si `hostDatabase` est
/// non nul. Une base créée par `sql.database()` est possédée par
/// l'interpréteur ; la base de l'hôte doit survivre au script.
void InstallSqlLibrary(Interpreter &vm, sql::Database *hostDatabase = nullptr);

} // namespace script