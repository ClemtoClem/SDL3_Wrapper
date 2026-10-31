#include <script/script.hpp>
#include <script_bindings/script_sql.hpp>

#include <memory>
#include <unordered_map>

namespace script {
namespace {

using lib::Args;
using lib::As;
using lib::Fail;
using lib::TypeBuilder;

// ── Types ────────────────────────────────────────────────────────────────

/// Table ordonnée index → nom de colonne (pour peupler les `row`).
struct DatabaseObj : HostObject {
    // owned : possédée par l'interpréteur (sql.database()) ; nullptr : base
    // de l'hôte (sql.host()), qui doit survivre au script.
    std::shared_ptr<sql::Database> owned;
    sql::Database *db = nullptr;
    mutable std::unordered_map<String, std::vector<String>> columnsCache;
};

struct RowObj : HostObject {
    DatabaseObj *parent = nullptr;
    sql::Row row;
    std::vector<String> columns;
};

// ── Conversions ─────────────────────────────────────────────────────────

Result<dataio::NodePtr, ScriptError> ValueToNode(const Value &v, const char *fn) {
    switch (v.GetKind()) {
        case Value::Kind::NIL:      return Ok(dataio::Node::MakeNone());
        case Value::Kind::BOOLEAN:  return Ok(dataio::Node::MakeBool(v.AsBoolean()));
        case Value::Kind::STRING:   return Ok(dataio::Node::MakeString(v.AsString()));
        case Value::Kind::NUMBER:
            if (v.IsInteger() && (IsSignedType(v.GetNumberType()) || v.AsUInt64() <= uint64_t(INT64_MAX)))
                return Ok(dataio::Node::MakeInt(v.AsInt64()));
            return Ok(dataio::Node::MakeFloat(v.AsNumber()));
        default:
            return Err(Fail(String::Format("`%s` : valeur scalaire attendue (STRING/BOOL/INT/FLOAT), trouvé `%s`",
                                            fn, v.TypeName())));
    }
}

Value NodeToValue(const dataio::NodePtr &node) {
    if (!node) return Value::Nil();
    switch (node->type) {
        case dataio::NodeType::STRING: return Value::Str(node->stringValue);
        case dataio::NodeType::BOOL:   return Value::Boolean(node->boolValue);
        case dataio::NodeType::INT:    return Value::Int(node->intValue);
        case dataio::NodeType::FLOAT:  return Value::Number(node->floatValue);
        default:                       return Value::Nil();
    }
}

/// Transforme une table `{col: valeur, …}` en `sql::Row`, dans l'ordre des
/// colonnes du schéma (requis par `InsertRow`). Toute colonne inconnue est
/// une erreur — pas de perte silencieuse.
Result<sql::Row, ScriptError> TableToRow(const std::shared_ptr<MapObject> &map,
                                          const std::vector<String> &columns, const char *fn) {
    if (!map) return Err(Fail(String::Format("`%s` : table attendue", fn)));
    sql::Row row;
    row.reserve(columns.size());
    for (const String &name : columns) {
        Option<Value> cell = map->Get(name);
        if (cell.IsNone())
            return Err(Fail(String::Format("`%s` : colonne `%s` absente", fn, name.CStr())));
        auto node = ValueToNode(cell.Unwrap(), fn);
        if (node.IsError()) return Err(node.Error());
        row.push_back(std::move(node).Unwrap());
    }
    return Ok(std::move(row));
}

/// Table de tous les champs d'une ligne, dans l'ordre des colonnes.
Value RowToTable(const sql::Row &row, const std::vector<String> &columns) {
    auto map = std::make_shared<MapObject>();
    for (size_t i = 0; i < row.size() && i < columns.size(); ++i)
        map->SetKey(columns[i], NodeToValue(row[i]));
    return Value::Map(std::move(map));
}

// ── Row ────────────────────────────────────────────────────────────────

void DefineRow(TypeBuilder &b) {
    b.type->get = [](const HostObject &self, const String &name) -> Option<Value> {
        const RowObj &r = As<RowObj>(self);
        for (size_t i = 0; i < r.columns.size() && i < r.row.size(); ++i)
            if (r.columns[i] == name)
                return Some(NodeToValue(r.row[i]));
        return NONE;
    };
    b.type->display = [](const HostObject &self) {
        const RowObj &r = As<RowObj>(self);
        String out("{");
        for (size_t i = 0; i < r.columns.size() && i < r.row.size(); ++i) {
            if (i > 0) out.Concat(", ");
            out.Concat(r.columns[i]); out.Concat(": ");
            out.Concat(NodeToValue(r.row[i]).ToDisplayString());
        }
        out.Concat("}");
        return out;
    };
}

// ── Database ────────────────────────────────────────────────────────────

const std::vector<String> &ColumnsOf(DatabaseObj &db, const String &table, const char * /*fn*/) {
    auto it = db.columnsCache.find(table);
    if (it != db.columnsCache.end()) return it->second;
    auto schema = db.db->GetSchema(table);
    if (schema.IsNone())
        return db.columnsCache.emplace(table, std::vector<String>{}).first->second;
    return db.columnsCache.emplace(table, schema.Unwrap()->ColumnNames()).first->second;
}

void DefineDatabase(TypeBuilder &b) {
    b.Method("execute", 1, 1, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        DatabaseObj &o = As<DatabaseObj>(self);
        auto r = o.db->Execute(a[0].ToDisplayString());
        if (r.IsError()) return Err(Fail(r.Error()));
        auto map = std::make_shared<MapObject>();
        map->SetKey(String("message"), Value::Str(r.Value().message));
        auto rows = std::make_shared<ListObject>();
        const std::vector<String> cols = r.Value().schema.ColumnNames();
        for (const sql::Row &row : r.Value().rows)
            rows->items.push_back(RowToTable(row, cols));
        map->SetKey(String("rows"), Value::List(std::move(rows)));
        return Ok(Value::Map(std::move(map)));
    });
    b.Method("tables", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
        DatabaseObj &o = As<DatabaseObj>(self);
        auto list = std::make_shared<ListObject>();
        for (const String &n : o.db->TableNames()) list->items.push_back(Value::Str(n));
        return Ok(Value::List(std::move(list)));
    });
    b.Method("columns", 1, 1, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        DatabaseObj &o = As<DatabaseObj>(self);
        const String t = a[0].ToDisplayString();
        const auto &cols = ColumnsOf(o, t, "sql.columns");
        auto list = std::make_shared<ListObject>();
        for (const String &n : cols) list->items.push_back(Value::Str(n));
        return Ok(Value::List(std::move(list)));
    });
    b.Method("insert", 2, 2, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        DatabaseObj &o = As<DatabaseObj>(self);
        const String t = a[0].ToDisplayString();
        const auto &cols = ColumnsOf(o, t, "sql.insert");
        if (!a[1].IsMap()) return Err(Fail(String("`sql.insert` : table de colonnes attendue")));
        auto row = TableToRow(a[1].AsMap(), cols, "sql.insert");
        if (row.IsError()) return Err(row.Error());
        auto r = o.db->InsertRow(t, std::move(row).Unwrap());
        if (r.IsError()) return Err(Fail(r.Error()));
        return Ok(Value::Str(r.Value().message));
    });
    b.Method("upsert", 2, 2, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        DatabaseObj &o = As<DatabaseObj>(self);
        const String t = a[0].ToDisplayString();
        const auto &cols = ColumnsOf(o, t, "sql.upsert");
        if (!a[1].IsMap()) return Err(Fail(String("`sql.upsert` : table de colonnes attendue")));
        auto row = TableToRow(a[1].AsMap(), cols, "sql.upsert");
        if (row.IsError()) return Err(row.Error());
        auto r = o.db->Upsert(t, std::move(row).Unwrap());
        if (r.IsError()) return Err(Fail(r.Error()));
        return Ok(Value::Str(r.Value().message));
    });
    b.Method("find", 2, 2, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        DatabaseObj &o = As<DatabaseObj>(self);
        const String t = a[0].ToDisplayString();
        const auto &cols = ColumnsOf(o, t, "sql.find");
        auto key = ValueToNode(a[1], "sql.find");
        if (key.IsError()) return Err(key.Error());
        auto r = o.db->FindByKey(t, key.Value());
        if (r.IsError()) return Err(Fail(r.Error()));
        if (r.Value().IsNone()) return Ok(Value::Nil());
        return Ok(RowToTable(r.Value().Value(), cols));
    });
    b.Method("delete", 2, 2, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        DatabaseObj &o = As<DatabaseObj>(self);
        auto key = ValueToNode(a[1], "sql.delete");
        if (key.IsError()) return Err(key.Error());
        auto r = o.db->DeleteByKey(a[0].ToDisplayString(), key.Value());
        if (r.IsError()) return Err(Fail(r.Error()));
        return Ok(Value::Boolean(r.Value()));
    });
    b.Method("select", 1, 1, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        DatabaseObj &o = As<DatabaseObj>(self);
        const String t = a[0].ToDisplayString();
        const auto &cols = ColumnsOf(o, t, "sql.select");
        auto list = std::make_shared<ListObject>();
        auto r = o.db->ForEachRow(t, [&](const sql::Row &row) {
            list->items.push_back(RowToTable(row, cols));
        });
        if (r.IsError()) return Err(Fail(r.Error()));
        return Ok(Value::List(std::move(list)));
    });
    b.Method("index", 2, 2, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        DatabaseObj &o = As<DatabaseObj>(self);
        const String t = a[0].ToDisplayString();
        auto r = o.db->CreateIndex(t, a[1].ToDisplayString());
        if (r.IsError()) return Err(Fail(r.Error()));
        return Ok(Value::Str(r.Value().message));
    });
    b.Method("lookup", 3, 3, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        DatabaseObj &o = As<DatabaseObj>(self);
        const String t = a[0].ToDisplayString();
        const auto &cols = ColumnsOf(o, t, "sql.lookup");
        auto key = ValueToNode(a[2], "sql.lookup");
        if (key.IsError()) return Err(key.Error());
        auto r = o.db->LookupByIndex(t, a[1].ToDisplayString(), key.Value());
        if (r.IsError()) return Err(Fail(r.Error()));
        auto list = std::make_shared<ListObject>();
        for (const sql::Row &row : r.Value()) list->items.push_back(RowToTable(row, cols));
        return Ok(Value::List(std::move(list)));
    });
    b.type->display = [](const HostObject &) { return String("<sql.database>"); };
}

} // namespace

void InstallSqlLibrary(Interpreter &vm, sql::Database *hostDatabase) {
    TypeBuilder row("sql.row"), db("sql.database");
    DefineRow(row);
    DefineDatabase(db);
    vm.RegisterHostType(String("sql"), String("row"), row.type);
    auto dbType = vm.RegisterHostType(String("sql"), String("database"), db.type);

    // sql.database() : base possédée par l'interpréteur
    vm.RegisterNamespacedNative(
        String("sql"), String("database"), 0, 0,
        [dbType](Interpreter &, Args &) -> Result<Value, ScriptError> {
            auto o = std::make_shared<DatabaseObj>();
            o->type = dbType;
            o->owned = std::make_shared<sql::Database>();
            o->db = o->owned.get();
            return Ok(Value::Host(std::move(o)));
        });
    // sql.host() : base de l'hôte si l'installation en a fourni une
    if (hostDatabase) {
        vm.RegisterNamespacedNative(
            String("sql"), String("host"), 0, 0,
            [hostDatabase, dbType](Interpreter &, Args &) -> Result<Value, ScriptError> {
                auto o = std::make_shared<DatabaseObj>();
                o->type = dbType;
                o->db = hostDatabase;
                return Ok(Value::Host(std::move(o)));
            });
    }
}

} // namespace script