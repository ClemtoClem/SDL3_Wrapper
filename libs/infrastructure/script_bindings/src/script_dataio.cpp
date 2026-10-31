#include <script_bindings/script_dataio.hpp>

#include <cmath>
#include <memory>
#include <utility>

#include <dataio/dataio.hpp>
#include <dataio/documents/gltf.hpp>
#include <dataio/mesh_export.hpp>
#include <sdl3/iostream.hpp>

namespace script {

namespace {

using lib::Args;
using lib::As;
using lib::Fail;
using lib::TypeBuilder;

// ============================================================================
// Poignées
// ============================================================================

struct DocObject : HostObject {
    dataio::DocumentPtr doc;
    String format;      ///< nom sous lequel le codec a été créé
    String lastError;   ///< dernier message d'erreur (vide = succès)
};

struct GltfObject : HostObject {
    dataio::gltf::Document doc;
};

// ============================================================================
// Fabrique de codec — accepte nom ET extension
// ============================================================================

dataio::DocumentPtr MakeDocFor(const String &format) {
    const String key = format.ToLower();
    auto &factory = dataio::DocumentFactory::instance();
    if (auto doc = factory.CreateByName(key))
        return doc;
    // Alias d'extension : "yml" -> .yml, "cfg" -> .cfg, "htm" -> .htm…
    if (auto doc = factory.CreateByFilename(String("file.") + key))
        return doc;
    return nullptr;
}

// ============================================================================
// Clonage profond d'une Value (aucune dépendance à dataio::Node)
// ============================================================================

Value CloneValue(const Value &v, int depth = 0) {
    if (depth > 64)
        return v;
    switch (v.GetKind()) {
        case Value::Kind::LIST: {
            auto items = v.AsList()->Snapshot();
            std::vector<Value> cloned;
            cloned.reserve(items.size());
            for (const Value &item : items)
                cloned.push_back(CloneValue(item, depth + 1));
            return Value::List(std::make_shared<ListObject>(std::move(cloned)));
        }
        case Value::Kind::MAP: {
            auto copy = std::make_shared<MapObject>();
            for (auto &entry : v.AsMap()->Snapshot())
                copy->SetKey(entry.first, CloneValue(entry.second, depth + 1));
            return Value::Map(std::move(copy));
        }
        default:
            return v;
    }
}

// ============================================================================
// Conversions utilitaires (valeurs numériques ↔ listes)
// ============================================================================

Value FloatsToValue(const std::vector<float> &v) {
    auto list = std::make_shared<ListObject>();
    list->items.reserve(v.size());
    for (float x : v)
        list->items.push_back(Value::Number(double(x)));
    return Value::List(std::move(list));
}

Value IndicesToValue(const std::vector<uint32_t> &v) {
    auto list = std::make_shared<ListObject>();
    list->items.reserve(v.size());
    for (uint32_t x : v)
        list->items.push_back(Value::UInt(uint64_t(x), NumberType::U32));
    return Value::List(std::move(list));
}

// ============================================================================
// Conversion glTF → Value (structure, pas les données binaires)
// ============================================================================

Value GltfMeshesToValue(const std::vector<dataio::gltf::Mesh> &meshes) {
    auto list = std::make_shared<ListObject>();
    for (const auto &mesh : meshes) {
        auto m = std::make_shared<MapObject>();
        m->SetKey(String("name"), Value::Str(mesh.name));
        auto prims = std::make_shared<ListObject>();
        for (const auto &prim : mesh.primitives) {
            auto p = std::make_shared<MapObject>();
            auto attrs = std::make_shared<MapObject>();
            for (const auto &a : prim.attributes)
                if (a.accessor >= 0)
                    attrs->SetKey(a.name, Value::Int(a.accessor));
            p->SetKey(String("attributes"), Value::Map(attrs));
            p->SetKey(String("indices"), Value::Int(prim.indices));
            p->SetKey(String("material"), Value::Int(prim.material));
            p->SetKey(String("mode"), Value::Int(prim.mode));
            prims->items.push_back(Value::Map(p));
        }
        m->SetKey(String("primitives"), Value::List(prims));
        list->items.push_back(Value::Map(m));
    }
    return Value::List(std::move(list));
}

Value GltfNodesToValue(const std::vector<dataio::gltf::Node> &nodes) {
    auto list = std::make_shared<ListObject>();
    for (const auto &node : nodes) {
        auto m = std::make_shared<MapObject>();
        m->SetKey(String("name"), Value::Str(node.name));
        m->SetKey(String("mesh"), Value::Int(node.mesh));
        auto kids = std::make_shared<ListObject>();
        for (int c : node.children)
            kids->items.push_back(Value::Int(c));
        m->SetKey(String("children"), Value::List(kids));
        auto t = std::make_shared<ListObject>();
        for (float x : node.translation)
            t->items.push_back(Value::Number(double(x)));
        m->SetKey(String("translation"), Value::List(t));
        auto r = std::make_shared<ListObject>();
        for (float x : node.rotation)
            r->items.push_back(Value::Number(double(x)));
        m->SetKey(String("rotation"), Value::List(r));
        auto s = std::make_shared<ListObject>();
        for (float x : node.scale)
            s->items.push_back(Value::Number(double(x)));
        m->SetKey(String("scale"), Value::List(s));
        list->items.push_back(Value::Map(m));
    }
    return Value::List(std::move(list));
}

Value GltfScenesToValue(const std::vector<dataio::gltf::Scene> &scenes) {
    auto list = std::make_shared<ListObject>();
    for (const auto &scene : scenes) {
        auto m = std::make_shared<MapObject>();
        m->SetKey(String("name"), Value::Str(scene.name));
        auto nodes = std::make_shared<ListObject>();
        for (int n : scene.nodes)
            nodes->items.push_back(Value::Int(n));
        m->SetKey(String("nodes"), Value::List(nodes));
        list->items.push_back(Value::Map(m));
    }
    return Value::List(std::move(list));
}

Value GltfMaterialsToValue(const std::vector<dataio::gltf::Material> &materials) {
    auto list = std::make_shared<ListObject>();
    for (const auto &mat : materials) {
        auto m = std::make_shared<MapObject>();
        m->SetKey(String("name"), Value::Str(mat.name));
        auto bc = std::make_shared<ListObject>();
        for (float x : mat.baseColor)
            bc->items.push_back(Value::Number(double(x)));
        m->SetKey(String("base_color"), Value::List(bc));
        m->SetKey(String("metallic"), Value::Number(double(mat.metallic)));
        m->SetKey(String("roughness"), Value::Number(double(mat.roughness)));
        m->SetKey(String("double_sided"), Value::Boolean(mat.doubleSided));
        m->SetKey(String("base_color_texture"), Value::Int(mat.baseColorTexture.index));
        m->SetKey(String("metallic_roughness_texture"), Value::Int(mat.metallicRoughnessTexture.index));
        m->SetKey(String("normal_texture"), Value::Int(mat.normalTexture.index));
        list->items.push_back(Value::Map(m));
    }
    return Value::List(std::move(list));
}

Value GltfImagesToValue(const std::vector<dataio::gltf::Image> &images) {
    auto list = std::make_shared<ListObject>();
    for (const auto &img : images) {
        auto m = std::make_shared<MapObject>();
        m->SetKey(String("name"), Value::Str(img.name));
        m->SetKey(String("uri"), Value::Str(img.uri));
        list->items.push_back(Value::Map(m));
    }
    return Value::List(std::move(list));
}

Value GltfTexturesToValue(const std::vector<dataio::gltf::Texture> &textures) {
    auto list = std::make_shared<ListObject>();
    for (const auto &tex : textures) {
        auto m = std::make_shared<MapObject>();
        m->SetKey(String("source"), Value::Int(tex.source));
        m->SetKey(String("sampler"), Value::Int(tex.sampler));
        list->items.push_back(Value::Map(m));
    }
    return Value::List(std::move(list));
}

// ============================================================================
// dataio.doc — type et méthodes
// ============================================================================

void DefineDoc(TypeBuilder &b) {
    b.Method("decode", 1, 1, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        DocObject &o = As<DocObject>(self);
        if (!o.doc)
            return Err(Fail(String("`doc.decode` : aucun document associé")));
        auto text = lib::ArgText(args, 0, "doc.decode");
        if (text.IsError())
            return Err(text.Error());
        auto err = o.doc->DecodeStr(text.Value());
        if (err.IsSome()) {
            o.lastError = err.Value().Format();
            return Ok(Value::Boolean(false));
        }
        o.lastError = String();
        return Ok(Value::Boolean(true));
    });

    b.Method("encode", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
        DocObject &o = As<DocObject>(self);
        if (!o.doc)
            return Err(Fail(String("`doc.encode` : aucun document associé")));
        return Ok(Value::Str(o.doc->EncodeStr()));
    });

    b.Method("load", 1, 1, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        DocObject &o = As<DocObject>(self);
        if (!o.doc)
            return Err(Fail(String("`doc.load` : aucun document associé")));
        auto path = lib::ArgText(args, 0, "doc.load");
        if (path.IsError())
            return Err(path.Error());
        auto bytes = sdl3::ReadFile(path.Value());
        if (!bytes) {
            o.lastError = String(bytes.Error());
            return Ok(Value::Boolean(false));
        }
        String text(reinterpret_cast<const char *>(bytes.Value().data()), bytes.Value().size());
        auto err = o.doc->DecodeStr(text);
        if (err.IsSome()) {
            o.lastError = err.Value().Format();
            return Ok(Value::Boolean(false));
        }
        o.lastError = String();
        return Ok(Value::Boolean(true));
    });

    b.Method("save", 1, 1, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        DocObject &o = As<DocObject>(self);
        if (!o.doc)
            return Err(Fail(String("`doc.save` : aucun document associé")));
        auto path = lib::ArgText(args, 0, "doc.save");
        if (path.IsError())
            return Err(path.Error());
        const String text = o.doc->EncodeStr();
        if (!sdl3::WriteFile(path.Value(), text.CStr(), text.GetSize())) {
            o.lastError = String::Format("écriture impossible : %s", path.Value().CStr());
            return Ok(Value::Boolean(false));
        }
        o.lastError = String();
        return Ok(Value::Boolean(true));
    });

    b.Method("clear", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
        DocObject &o = As<DocObject>(self);
        if (auto fresh = MakeDocFor(o.format)) {
            o.doc = std::move(fresh);
            o.lastError = String();
        }
        return Ok(Value::Nil());
    });

    b.type->get = [](const HostObject &self, const String &name) -> Option<Value> {
        const DocObject &o = As<DocObject>(self);
        if (name == "root")
            return Some(o.doc ? ValueFromNode(o.doc->GetRoot()) : Value::Nil());
        if (name == "error")
            return Some(Value::Str(o.lastError));
        if (name == "format")
            return Some(Value::Str(o.format));
        return NONE;
    };

    b.type->set = [](Interpreter &, const HostRef &self, const String &name,
                     const Value &v) -> Result<bool, ScriptError> {
        DocObject &o = As<DocObject>(self);
        if (name == "root") {
            if (!o.doc)
                return Ok(false);
            o.doc->SetRoot(NodeFromValue(v));
            return Ok(true);
        }
        return Ok(false);
    };

    b.type->display = [](const HostObject &self) {
        const DocObject &o = As<DocObject>(self);
        return String::Format("<dataio.doc %s%s>", o.format.CStr(),
                              o.lastError.IsEmpty() ? "" : " (dernière opération en erreur)");
    };
}

// ============================================================================
// dataio.gltf_doc — type et méthodes
// ============================================================================

void DefineGltf(TypeBuilder &b) {
    b.Method("read_floats", 1, 1, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        auto idx = lib::ArgInt(args, 0, "gltf_doc.read_floats");
        if (idx.IsError())
            return Err(idx.Error());
        auto result = As<GltfObject>(self).doc.ReadFloats(int(idx.Value()));
        if (result.IsError())
            return Err(Fail(result.Error()));
        return Ok(FloatsToValue(result.Value()));
    });

    b.Method("read_indices", 1, 1, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        auto idx = lib::ArgInt(args, 0, "gltf_doc.read_indices");
        if (idx.IsError())
            return Err(idx.Error());
        auto result = As<GltfObject>(self).doc.ReadIndices(int(idx.Value()));
        if (result.IsError())
            return Err(Fail(result.Error()));
        return Ok(IndicesToValue(result.Value()));
    });

    b.Method("load_buffers", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
        auto err = dataio::gltf::LoadBuffers(As<GltfObject>(self).doc);
        return Ok(Value::Boolean(err.IsNone()));
    });

    b.Method("resolve_uri", 1, 1, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        auto uri = lib::ArgText(args, 0, "gltf_doc.resolve_uri");
        if (uri.IsError())
            return Err(uri.Error());
        return Ok(Value::Str(As<GltfObject>(self).doc.ResolveUri(uri.Value())));
    });

    b.type->get = [](const HostObject &self, const String &name) -> Option<Value> {
        const auto &doc = As<GltfObject>(self).doc;
        if (name == "version")       return Some(Value::Str(doc.version));
        if (name == "generator")     return Some(Value::Str(doc.generator));
        if (name == "default_scene") return Some(Value::Int(doc.defaultScene));
        if (name == "meshes")        return Some(GltfMeshesToValue(doc.meshes));
        if (name == "nodes")         return Some(GltfNodesToValue(doc.nodes));
        if (name == "scenes")        return Some(GltfScenesToValue(doc.scenes));
        if (name == "materials")     return Some(GltfMaterialsToValue(doc.materials));
        if (name == "images")        return Some(GltfImagesToValue(doc.images));
        if (name == "textures")      return Some(GltfTexturesToValue(doc.textures));
        return NONE;
    };

    b.type->display = [](const HostObject &self) {
        const auto &d = As<GltfObject>(self).doc;
        return String::Format("<dataio.gltf_doc v%s, %d maillage(s), %d nœud(s)>",
                              d.version.CStr(), int(d.meshes.size()), int(d.nodes.size()));
    };
}

// ============================================================================
// dataio.mesh.save
// ============================================================================

Result<bool, ScriptError> ReadFloatList(const Value &v, const char *name, std::vector<float> &out) {
    if (!v.IsList() || !v.AsList())
        return Err(Fail(String::Format("`dataio.mesh.save` : `%s` doit être une liste de nombres", name)));
    for (const Value &x : v.AsList()->Snapshot()) {
        if (!x.IsNumber())
            return Err(Fail(String::Format("`dataio.mesh.save` : `%s` contient une valeur non numérique", name)));
        out.push_back(x.AsFloat());
    }
    return Ok(true);
}

Result<Value, ScriptError> MeshSave(const Args &args) {
    if (args.size() < 2)
        return Err(Fail(String("`dataio.mesh.save(chemin, options)` : deux arguments attendus")));
    auto path = lib::ArgText(args, 0, "dataio.mesh.save");
    if (path.IsError())
        return Err(path.Error());
    if (!args[1].IsMap() || !args[1].AsMap())
        return Err(Fail(String("`dataio.mesh.save` : table d'options attendue")));
    const auto opts = args[1].AsMap()->Snapshot();

    auto get = [&](const char *key) -> Option<Value> {
        for (auto &e : opts)
            if (e.first == key)
                return Some(e.second);
        return NONE;
    };

    std::vector<float> positions, normals, uvs;
    std::vector<uint8_t> colors;
    std::vector<uint32_t> indices;
    std::vector<dataio::MeshMaterialGroup> materials;
    String comment = String("Exporté par dataio.mesh");

    if (auto v = get("positions"); v.IsSome()) {
        auto r = ReadFloatList(v.Unwrap(), "positions", positions);
        if (r.IsError())
            return Err(r.Error());
    }
    if (positions.empty() || positions.size() % 3 != 0)
        return Err(Fail(String("`dataio.mesh.save` : `positions` doit contenir un multiple de 3 flottants")));

    if (auto v = get("normals"); v.IsSome()) {
        auto r = ReadFloatList(v.Unwrap(), "normals", normals);
        if (r.IsError())
            return Err(r.Error());
        if (normals.size() != positions.size())
            return Err(Fail(String("`dataio.mesh.save` : `normals` doit avoir la même longueur que `positions`")));
    }

    if (auto v = get("uvs"); v.IsSome()) {
        auto r = ReadFloatList(v.Unwrap(), "uvs", uvs);
        if (r.IsError())
            return Err(r.Error());
        if (uvs.size() != positions.size() / 3 * 2)
            return Err(Fail(String("`dataio.mesh.save` : `uvs` doit contenir 2 flottants par sommet")));
    }

    // `colors` : pas de ReadFloatList (le conteneur final est
    // `std::vector<uint8_t>`) — lecture explicite.
    colors.clear();
    if (auto v = get("colors"); v.IsSome()) {
        if (!v.Value().IsList() || !v.Value().AsList())
            return Err(Fail(String("`dataio.mesh.save` : `colors` doit être une liste de nombres")));
        for (const Value &x : v.Value().AsList()->Snapshot()) {
            if (!x.IsNumber())
                return Err(Fail(String("`dataio.mesh.save` : `colors` contient une valeur non numérique")));
            colors.push_back(uint8_t(std::lround(math::Clamp(x.AsFloat(), 0.f, 1.f) * 255.f)));
        }
        if (colors.size() != positions.size())
            return Err(Fail(String("`dataio.mesh.save` : `colors` doit contenir 3 octets (r,g,b) par sommet")));
    }

    if (auto v = get("indices"); v.IsSome()) {
        if (!v.Value().IsList() || !v.Value().AsList())
            return Err(Fail(String("`dataio.mesh.save` : `indices` doit être une liste d'entiers")));
        for (const Value &x : v.Value().AsList()->Snapshot()) {
            if (!x.IsInteger() || x.AsInt64() < 0)
                return Err(Fail(String("`dataio.mesh.save` : `indices` contient une valeur non entière ou négative")));
            indices.push_back(uint32_t(x.AsUInt64()));
        }
    } else {
        // Non indexé : indices identité.
        indices.resize(positions.size() / 3);
        for (size_t i = 0; i < indices.size(); ++i)
            indices[i] = uint32_t(i);
    }

    if (auto v = get("materials"); v.IsSome() && v.Value().IsList()) {
        for (const Value &m : v.Value().AsList()->Snapshot()) {
            if (!m.IsMap() || !m.AsMap())
                return Err(Fail(String("`dataio.mesh.save` : chaque matériau doit être une table")));
            dataio::MeshMaterialGroup g;
            for (auto &entry : m.AsMap()->Snapshot()) {
                const String &k = entry.first;
                const Value &x = entry.second;
                if (k == "name" && x.IsString())                     g.name = x.AsString();
                else if (k == "r" && x.IsNumber())                   g.r = x.AsFloat();
                else if (k == "g" && x.IsNumber())                   g.g = x.AsFloat();
                else if (k == "b" && x.IsNumber())                   g.b = x.AsFloat();
                else if (k == "a" && x.IsNumber())                   g.a = x.AsFloat();
                else if (k == "first_index" && x.IsInteger())        g.firstIndex = uint32_t(x.AsUInt64());
                else if (k == "index_count" && x.IsInteger())        g.indexCount = uint32_t(x.AsUInt64());
            }
            materials.push_back(std::move(g));
        }
    }

    if (auto v = get("comment"); v.IsSome() && v.Value().IsString())
        comment = v.Value().AsString();

    dataio::TriangleMeshView view;
    view.positions = positions;
    view.normals = normals;
    view.uvs = uvs;
    view.colors = colors;
    view.indices = indices;
    view.materials = materials;

    auto err = dataio::SaveMesh(path.Value(), view, comment);
    if (err.IsSome())
        return Err(Fail(err.Value()));
    return Ok(Value::Boolean(true));
}

} // namespace

// ============================================================================
// Point d'entrée
// ============================================================================

void InstallDataioLibrary(Interpreter &vm) {
    TypeBuilder docBuilder("dataio.doc");
    TypeBuilder gltfDoc("dataio.gltf_doc");
    DefineDoc(docBuilder);
    DefineGltf(gltfDoc);

    auto docType = vm.RegisterHostType(String("dataio"), String("doc"), docBuilder.type);
    auto gltfType = vm.RegisterHostType(String("dataio"), String("gltf_doc"), gltfDoc.type);

    constexpr auto ANY = Interpreter::NativeThread::ANY;

    // ── dataio.formats() ────────────────────────────────────────────────────
    vm.RegisterNamespacedNative(
        String("dataio"), String("formats"), 0, 0,
        [](Interpreter &, Args &) -> Result<Value, ScriptError> {
            auto list = std::make_shared<ListObject>();
            for (const String &name : dataio::DocumentFactory::instance().RegisteredFormats())
                list->items.push_back(Value::Str(name));
            return Ok(Value::List(std::move(list)));
        },
        ANY);

    // ── dataio.clone(v) ─────────────────────────────────────────────────────
    vm.RegisterNamespacedNative(
        String("dataio"), String("clone"), 1, 1,
        [](Interpreter &, Args &args) -> Result<Value, ScriptError> {
            return Ok(CloneValue(args[0]));
        },
        ANY);

    // ── dataio.parse(fmt, texte) ────────────────────────────────────────────
    vm.RegisterNamespacedNative(
        String("dataio"), String("parse"), 2, 2,
        [](Interpreter &, Args &args) -> Result<Value, ScriptError> {
            auto format = lib::ArgText(args, 0, "dataio.parse");
            if (format.IsError())
                return Err(format.Error());
            auto text = lib::ArgText(args, 1, "dataio.parse");
            if (text.IsError())
                return Err(text.Error());
            auto doc = MakeDocFor(format.Value());
            if (!doc)
                return Err(Fail(String::Format(
                    "`dataio.parse` : format `%s` inconnu (voir `dataio.formats()`)", format.Value().CStr())));
            auto err = doc->DecodeStr(text.Value());
            if (err.IsSome())
                return Err(Fail(String::Format("`dataio.parse` (%s) : %s",
                                               format.Value().CStr(), err.Value().Format().CStr())));
            return Ok(ValueFromNode(doc->GetRoot()));
        });

    // ── dataio.encode(fmt, valeur) ──────────────────────────────────────────
    vm.RegisterNamespacedNative(
        String("dataio"), String("encode"), 2, 2,
        [](Interpreter &, Args &args) -> Result<Value, ScriptError> {
            auto format = lib::ArgText(args, 0, "dataio.encode");
            if (format.IsError())
                return Err(format.Error());
            auto doc = MakeDocFor(format.Value());
            if (!doc)
                return Err(Fail(String::Format(
                    "`dataio.encode` : format `%s` inconnu (voir `dataio.formats()`)", format.Value().CStr())));
            doc->SetRoot(NodeFromValue(args[1]));
            return Ok(Value::Str(doc->EncodeStr()));
        });

    // ── dataio.document(fmt) ────────────────────────────────────────────────
    vm.RegisterNamespacedNative(
        String("dataio"), String("document"), 1, 1,
        [docType](Interpreter &, Args &args) -> Result<Value, ScriptError> {
            auto format = lib::ArgText(args, 0, "dataio.document");
            if (format.IsError())
                return Err(format.Error());
            auto doc = MakeDocFor(format.Value());
            if (!doc)
                return Err(Fail(String::Format(
                    "`dataio.document` : format `%s` inconnu (voir `dataio.formats()`)", format.Value().CStr())));
            auto object = std::make_shared<DocObject>();
            object->type = docType;
            object->doc = std::move(doc);
            object->format = format.Value();
            return Ok(Value::Host(std::move(object)));
        });

    // ── dataio.gltf.load(chemin, [charger_tampons=true]) ────────────────────
    vm.RegisterNamespacedNative(
        String("dataio.gltf"), String("load"), 1, 2,
        [gltfType](Interpreter &, Args &args) -> Result<Value, ScriptError> {
            auto path = lib::ArgText(args, 0, "dataio.gltf.load");
            if (path.IsError())
                return Err(path.Error());
            const bool loadBuffers = args.size() < 2 || args[1].IsTruthy();
            auto result = dataio::gltf::LoadFile(path.Value(), loadBuffers);
            if (result.IsError())
                return Err(Fail(String::Format("`dataio.gltf.load` : %s", result.Error().CStr())));
            auto object = std::make_shared<GltfObject>();
            object->type = gltfType;
            object->doc = std::move(result).Unwrap();
            return Ok(Value::Host(std::move(object)));
        },
        ANY);

    // ── dataio.mesh.save(chemin, options) ───────────────────────────────────
    vm.RegisterNamespacedNative(
        String("dataio.mesh"), String("save"), 2, 2,
        [](Interpreter &, Args &args) -> Result<Value, ScriptError> { return MeshSave(args); },
        ANY);
}

} // namespace script