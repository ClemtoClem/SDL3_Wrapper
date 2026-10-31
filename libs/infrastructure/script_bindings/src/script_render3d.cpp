#include <script/script.hpp>
#include <script_bindings/script_render3d.hpp>
#include <script_bindings/script_common.hpp>
#include <script_bindings/script_color.hpp>

#include <algorithm>
#include <cmath>

#include <render3d/camera.hpp>
#include <render3d/shape.hpp>

namespace script {
namespace {

using lib::Args;
using lib::As;
using lib::Fail;
using lib::TypeBuilder;

// ── Types exposés au script ────────────────────────────────────────────────

struct MeshObj : HostObject {
    std::shared_ptr<render3d::Mesh> mesh;
};

struct MaterialObj : HostObject {
    render3d::Material material;
};

struct CameraObj : HostObject {
    render3d::Camera camera;
};

struct SceneObj : HostObject {
    render3d::Object3D *root = nullptr;
    render3d::Canvas *canvas = nullptr;  // facultatif, pour render()
    // Objets créés par le script, pas encore attachés à un parent.
    std::vector<std::unique_ptr<render3d::Object3D>> pending;
};

struct NodeObj : HostObject {
    render3d::Object3D *node = nullptr;
    render3d::Shape *asShape = nullptr;  // non-null si le nœud EST un Shape
    SceneObj *scene = nullptr;           // scène propriétaire (validation)
};

// (Vecteurs et couleurs : scriptlib::Vec3Of / Vec3List / QuatList — cf.
// script_common.hpp — et scriptlib::ByteColor, cf. script_color.hpp.)

// ── Définitions de types ───────────────────────────────────────────────────

void DefineMesh(TypeBuilder &builder) {
    std::weak_ptr<const HostType> wt = builder.type;
    auto make = [wt](std::shared_ptr<render3d::Mesh> mesh) -> Result<Value, ScriptError> {
        auto object = std::make_shared<MeshObj>();
        object->type = wt.lock();
        object->mesh = std::move(mesh);
        return Ok(Value::Host(std::move(object)));
    };
    builder.StaticFn("cube", 0, 1, [make](Interpreter &, Args &args) -> Result<Value, ScriptError> {
        const float s = args.empty() ? 1.f : args[0].AsFloat();
        return make(std::make_shared<render3d::Mesh>(render3d::Mesh::Cube(s)));
    });
    builder.StaticFn("box", 3, 3, [make](Interpreter &, Args &args) -> Result<Value, ScriptError> {
        return make(std::make_shared<render3d::Mesh>(
            render3d::Mesh::Box(args[0].AsFloat(), args[1].AsFloat(), args[2].AsFloat())));
    });
    builder.StaticFn("sphere", 1, 3, [make](Interpreter &, Args &args) -> Result<Value, ScriptError> {
        const float r = args[0].AsFloat();
        const int seg = args.size() > 1 ? int(args[1].AsInt64()) : 24;
        const int rings = args.size() > 2 ? int(args[2].AsInt64()) : 16;
        return make(std::make_shared<render3d::Mesh>(render3d::Mesh::Sphere(r, seg, rings)));
    });
    builder.StaticFn("plane", 0, 4, [make](Interpreter &, Args &args) -> Result<Value, ScriptError> {
        const float w = args.empty() ? 1.f : args[0].AsFloat();
        const float h = args.size() > 1 ? args[1].AsFloat() : 1.f;
        const int sx = args.size() > 2 ? int(args[2].AsInt64()) : 1;
        const int sy = args.size() > 3 ? int(args[3].AsInt64()) : 1;
        return make(std::make_shared<render3d::Mesh>(render3d::Mesh::Plane(w, h, sx, sy)));
    });
    builder.StaticFn("cylinder", 0, 5, [make](Interpreter &, Args &args) -> Result<Value, ScriptError> {
        const float rt = args.size() > 0 ? args[0].AsFloat() : 1.f;
        const float rb = args.size() > 1 ? args[1].AsFloat() : 1.f;
        const float h = args.size() > 2 ? args[2].AsFloat() : 1.f;
        const int seg = args.size() > 3 ? int(args[3].AsInt64()) : 32;
        return make(std::make_shared<render3d::Mesh>(render3d::Mesh::Cylinder(rt, rb, h, seg)));
    });
    builder.StaticFn("cone", 1, 3, [make](Interpreter &, Args &args) -> Result<Value, ScriptError> {
        const float r = args[0].AsFloat();
        const float h = args.size() > 1 ? args[1].AsFloat() : 1.f;
        const int seg = args.size() > 2 ? int(args[2].AsInt64()) : 32;
        return make(std::make_shared<render3d::Mesh>(render3d::Mesh::Cone(r, h, seg)));
    });
    builder.StaticFn("torus", 1, 4, [make](Interpreter &, Args &args) -> Result<Value, ScriptError> {
        const float r = args[0].AsFloat();
        const float tube = args.size() > 1 ? args[1].AsFloat() : 0.4f;
        const int rs = args.size() > 2 ? int(args[2].AsInt64()) : 12;
        const int ts = args.size() > 3 ? int(args[3].AsInt64()) : 48;
        return make(std::make_shared<render3d::Mesh>(render3d::Mesh::Torus(r, tube, rs, ts)));
    });
    builder.StaticFn("circle", 1, 2, [make](Interpreter &, Args &args) -> Result<Value, ScriptError> {
        const float r = args[0].AsFloat();
        const int seg = args.size() > 1 ? int(args[1].AsInt64()) : 32;
        return make(std::make_shared<render3d::Mesh>(render3d::Mesh::Circle(r, seg)));
    });
    builder.StaticFn("ring", 1, 4, [make](Interpreter &, Args &args) -> Result<Value, ScriptError> {
        const float ir = args.size() > 0 ? args[0].AsFloat() : 0.5f;
        const float or_ = args.size() > 1 ? args[1].AsFloat() : 1.f;
        const int ts = args.size() > 2 ? int(args[2].AsInt64()) : 32;
        const int ps = args.size() > 3 ? int(args[3].AsInt64()) : 1;
        return make(std::make_shared<render3d::Mesh>(render3d::Mesh::Ring(ir, or_, ts, ps)));
    });
    builder.type->display = [](const HostObject &) { return String("<render3d.mesh>"); };
}

void DefineMaterial(TypeBuilder &builder) {
    std::weak_ptr<const HostType> wt = builder.type;
    builder.Construct(
        0, 1, [wt](Interpreter &, Args &args, const Args &) -> Result<Value, ScriptError> {
            render3d::Material m;
            if (!args.empty()) {
                if (!args[0].IsMap())
                    return Err(Fail(String::Format("`render3d.material` : table d'options attendue, trouvé `%s`",
                                                   args[0].TypeName())));
                auto opts = args[0].AsMap();
                auto colorV = opts->Get(String("color"));
                if (colorV.IsSome()) {
                    auto c = scriptlib::ByteColor(colorV.Unwrap(), "render3d.material");
                    if (c.IsError())
                        return Err(c.Error());
                    m.baseColor = c.Value();
                }
                if (auto v = opts->Get(String("roughness")); v.IsSome())
                    m.roughness = float(v.Unwrap().AsFloat());
                if (auto v = opts->Get(String("metallic")); v.IsSome())
                    m.metallic = float(v.Unwrap().AsFloat());
                if (auto v = opts->Get(String("lit")); v.IsSome())
                    m.lit = v.Unwrap().IsTruthy();
                if (auto v = opts->Get(String("pbr")); v.IsSome())
                    m.pbr = v.Unwrap().IsTruthy();
                if (auto v = opts->Get(String("double_sided")); v.IsSome())
                    m.doubleSided = v.Unwrap().IsTruthy();
                if (auto v = opts->Get(String("wireframe")); v.IsSome())
                    m.wireframe = v.Unwrap().IsTruthy();
                if (auto v = opts->Get(String("transparent")); v.IsSome())
                    m.blend = v.Unwrap().IsTruthy();
            }
            auto object = std::make_shared<MaterialObj>();
            object->type = wt.lock();
            object->material = std::move(m);
            return Ok(Value::Host(std::move(object)));
        });
    builder.type->display = [](const HostObject &self) {
        const auto &m = As<MaterialObj>(self).material;
        return String::Format("<render3d.material r=%g g=%g b=%g rough=%g metal=%g>",
                              m.baseColor.r / 255.0, m.baseColor.g / 255.0, m.baseColor.b / 255.0,
                              m.roughness, m.metallic);
    };
}

void DefineCamera(TypeBuilder &builder) {
    std::weak_ptr<const HostType> wt = builder.type;
    builder.Construct(
        0, 1, [wt](Interpreter &, Args &args, const Args &) -> Result<Value, ScriptError> {
            auto object = std::make_shared<CameraObj>();
            object->type = wt.lock();
            if (!args.empty()) {
                if (!args[0].IsMap())
                    return Err(Fail(String::Format("`render3d.camera` : table d'options attendue, trouvé `%s`",
                                                   args[0].TypeName())));
                auto opts = args[0].AsMap();
                if (auto v = opts->Get(String("position")); v.IsSome()) {
                    auto p = scriptlib::Vec3Of(v.Unwrap(), "render3d.camera");
                    if (p.IsError())
                        return Err(p.Error());
                    object->camera.position = p.Value();
                }
                if (auto v = opts->Get(String("target")); v.IsSome()) {
                    auto t = scriptlib::Vec3Of(v.Unwrap(), "render3d.camera");
                    if (t.IsError())
                        return Err(t.Error());
                    object->camera.target = t.Value();
                }
                if (auto v = opts->Get(String("up")); v.IsSome()) {
                    auto u = scriptlib::Vec3Of(v.Unwrap(), "render3d.camera");
                    if (u.IsError())
                        return Err(u.Error());
                    object->camera.up = u.Value();
                }
                if (auto v = opts->Get(String("fov")); v.IsSome())
                    object->camera.fovYRadians = float(v.Unwrap().AsFloat());
                if (auto v = opts->Get(String("near")); v.IsSome())
                    object->camera.nearPlane = float(v.Unwrap().AsFloat());
                if (auto v = opts->Get(String("far")); v.IsSome())
                    object->camera.farPlane = float(v.Unwrap().AsFloat());
                if (auto v = opts->Get(String("aspect")); v.IsSome())
                    object->camera.aspect = float(v.Unwrap().AsFloat());
            }
            return Ok(Value::Host(std::move(object)));
        });
    builder.type->get = [](const HostObject &self, const String &name) -> Option<Value> {
        const auto &c = As<CameraObj>(self).camera;
        if (name == "position") return Some(scriptlib::Vec3List(c.position));
        if (name == "target") return Some(scriptlib::Vec3List(c.target));
        if (name == "up") return Some(scriptlib::Vec3List(c.up));
        if (name == "fov") return Some(Value::Number(double(c.fovYRadians)));
        if (name == "near") return Some(Value::Number(double(c.nearPlane)));
        if (name == "far") return Some(Value::Number(double(c.farPlane)));
        if (name == "aspect") return Some(Value::Number(double(c.aspect)));
        return NONE;
    };
    builder.type->set = [](Interpreter &, const HostRef &self, const String &name,
                            const Value &v) -> Result<bool, ScriptError> {
        auto &c = As<CameraObj>(self).camera;
        if (name == "position") { auto p = scriptlib::Vec3Of(v, "camera.position"); if (p.IsError()) return Err(p.Error()); c.position = p.Value(); return Ok(true); }
        if (name == "target")   { auto p = scriptlib::Vec3Of(v, "camera.target");   if (p.IsError()) return Err(p.Error()); c.target = p.Value();   return Ok(true); }
        if (name == "up")       { auto p = scriptlib::Vec3Of(v, "camera.up");       if (p.IsError()) return Err(p.Error()); c.up = p.Value();       return Ok(true); }
        if (name == "fov" && v.IsNumber())   { c.fovYRadians = float(v.AsFloat()); return Ok(true); }
        if (name == "near" && v.IsNumber())  { c.nearPlane = float(v.AsFloat());   return Ok(true); }
        if (name == "far" && v.IsNumber())   { c.farPlane = float(v.AsFloat());    return Ok(true); }
        return Ok(false);
    };
    builder.type->display = [](const HostObject &) { return String("<render3d.camera>"); };
}

void DefineNode(TypeBuilder &builder) {
    auto nodeOf = [](const HostObject &self) -> render3d::Object3D * { return As<NodeObj>(self).node; };
    builder.Method("find", 1, 1, [nodeOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        render3d::Object3D *n = nodeOf(*self);
        if (!n) return Ok(Value::Nil());
        render3d::Object3D *found = n->FindByName(args[0].ToDisplayString().View());
        if (!found) return Ok(Value::Nil());
        auto handle = std::make_shared<NodeObj>();
        handle->type = self->type;
        handle->node = found;
        handle->asShape = dynamic_cast<render3d::Shape *>(found);
        handle->scene = As<NodeObj>(self).scene;
        return Ok(Value::Host(std::move(handle)));
    });
    builder.Method("children", 0, 0, [nodeOf](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
        auto list = std::make_shared<ListObject>();
        render3d::Object3D *n = nodeOf(*self);
        if (n) {
            for (const auto &child : n->Children()) {
                auto handle = std::make_shared<NodeObj>();
                handle->type = self->type;
                handle->node = child.get();
                handle->asShape = dynamic_cast<render3d::Shape *>(child.get());
                handle->scene = As<NodeObj>(self).scene;
                list->items.push_back(Value::Host(std::move(handle)));
            }
        }
        return Ok(Value::List(std::move(list)));
    });
    builder.Method("parent", 0, 0, [nodeOf](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
        render3d::Object3D *n = nodeOf(*self);
        if (!n || !n->Parent()) return Ok(Value::Nil());
        auto handle = std::make_shared<NodeObj>();
        handle->type = self->type;
        handle->node = n->Parent();
        handle->asShape = dynamic_cast<render3d::Shape *>(n->Parent());
        handle->scene = As<NodeObj>(self).scene;
        return Ok(Value::Host(std::move(handle)));
    });
    builder.Method("set_position", 1, 1, [nodeOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        auto v = scriptlib::Vec3Of(args[0], "node.set_position");
        if (v.IsError()) return Err(v.Error());
        if (render3d::Object3D *n = nodeOf(*self)) n->SetPosition(v.Value());
        return Ok(Value::Host(self));
    });
    builder.Method("set_euler", 3, 3, [nodeOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        if (render3d::Object3D *n = nodeOf(*self))
            n->SetRotation(math::FQuaternion::FromEuler(float(args[0].AsFloat()), float(args[1].AsFloat()), float(args[2].AsFloat())));
        return Ok(Value::Host(self));
    });
    builder.Method("set_scale", 1, 1, [nodeOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        auto v = scriptlib::Vec3Of(args[0], "node.set_scale");
        if (v.IsError()) return Err(v.Error());
        if (render3d::Object3D *n = nodeOf(*self)) n->SetScale(v.Value());
        return Ok(Value::Host(self));
    });
    builder.Method("set_material", 1, 1, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        NodeObj &o = As<NodeObj>(self);
        if (!o.asShape || !o.asShape->Materials().size() || !args[0].IsHost())
            return Err(Fail(String("`node.set_material` : ce nœud n'est pas un Shape ou le matériau est absent")));
        const auto *mat = dynamic_cast<const MaterialObj *>(args[0].AsHost().get());
        if (!mat) return Err(Fail(String("`node.set_material` : render3d.material attendu")));
        o.asShape->Materials()[0] = mat->material;
        return Ok(Value::Host(self));
    });
    // Propriétés directes (getter + setter)
    builder.type->get = [nodeOf](const HostObject &self, const String &name) -> Option<Value> {
        render3d::Object3D *n = nodeOf(self);
        if (!n) return NONE;
        if (name == "position") return Some(scriptlib::Vec3List(n->Position()));
        if (name == "rotation") return Some(scriptlib::QuatList(n->Rotation()));
        if (name == "scale")    return Some(scriptlib::Vec3List(n->Scale()));
        if (name == "visible")  return Some(Value::Boolean(n->IsVisible()));
        if (name == "name")     return Some(Value::Str(n->Name()));
        return NONE;
    };
    builder.type->set = [nodeOf](Interpreter &, const HostRef &self, const String &name,
                                   const Value &v) -> Result<bool, ScriptError> {
        render3d::Object3D *n = nodeOf(*self);
        if (!n) return Ok(false);
        if (name == "position") { auto x = scriptlib::Vec3Of(v, "node.position"); if (x.IsError()) return Err(x.Error()); n->SetPosition(x.Value()); return Ok(true); }
        if (name == "scale")    { auto x = scriptlib::Vec3Of(v, "node.scale");    if (x.IsError()) return Err(x.Error()); n->SetScale(x.Value());    return Ok(true); }
        if (name == "visible")  { n->SetVisible(v.IsTruthy()); return Ok(true); }
        if (name == "name")     { n->SetName(v.ToDisplayString()); return Ok(true); }
        return Ok(false);
    };
    builder.type->display = [nodeOf](const HostObject &self) {
        render3d::Object3D *n = nodeOf(self);
        return n ? String::Format("<render3d.node %s>", n->Name().CStr()) : String("<render3d.node>");
    };
}

/// Poignée `render3d.node` — définie avant DefineScene, qui l'utilise.
Value MakeRawNodeHandle(render3d::Object3D *node, render3d::Shape *asShape, SceneObj *scene,
                        std::shared_ptr<const HostType> nodeType) {
    auto handle = std::make_shared<NodeObj>();
    handle->type = std::move(nodeType);
    handle->node = node;
    handle->asShape = asShape;
    handle->scene = scene;
    return Value::Host(std::move(handle));
}

void DefineScene(TypeBuilder &builder) {
    // create_object() : crée un Object3D vide détenu par la scène
    builder.Method("create_object", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
        SceneObj &o = As<SceneObj>(self);
        auto node = std::make_unique<render3d::Object3D>();
        render3d::Object3D *raw = node.get();
        o.pending.push_back(std::move(node));
        return Ok(MakeRawNodeHandle(raw, nullptr, &o, self->type));
    });
    builder.Method("create_shape", 1, 2, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        SceneObj &o = As<SceneObj>(self);
        if (!args[0].IsHost())
            return Err(Fail(String("`scene.create_shape` : mesh attendu")));
        const auto *mesh = dynamic_cast<const MeshObj *>(args[0].AsHost().get());
        if (!mesh || !mesh->mesh)
            return Err(Fail(String("`scene.create_shape` : mesh invalide")));
        render3d::Material mat;
        if (args.size() > 1) {
            if (!args[1].IsHost())
                return Err(Fail(String("`scene.create_shape` : matériau attendu")));
            const auto *m = dynamic_cast<const MaterialObj *>(args[1].AsHost().get());
            if (!m) return Err(Fail(String("`scene.create_shape` : matériau invalide")));
            mat = m->material;
        }
        auto shape = std::make_unique<render3d::Shape>(mesh->mesh, mat);
        render3d::Shape *raw = shape.get();
        o.pending.push_back(std::move(shape));
        return Ok(MakeRawNodeHandle(raw, raw, &o, self->type));
    });
    builder.Method("add", 1, 2, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        SceneObj &o = As<SceneObj>(self);
        if (!args[0].IsHost()) return Err(Fail(String("`scene.add` : nœud attendu")));
        auto *handle = dynamic_cast<NodeObj *>(args[0].AsHost().get());
        if (!handle || !handle->node) return Err(Fail(String("`scene.add` : nœud invalide")));
        render3d::Object3D *parent = o.root;
        if (args.size() > 1) {
            if (!args[1].IsHost()) return Err(Fail(String("`scene.add` : parent invalide")));
            auto *p = dynamic_cast<NodeObj *>(args[1].AsHost().get());
            if (!p || !p->node) return Err(Fail(String("`scene.add` : parent invalide")));
            parent = p->node;
        }
        // Retrouve le unique_ptr dans `pending`
        auto it = std::find_if(o.pending.begin(), o.pending.end(),
                                [node = handle->node](const auto &p) { return p.get() == node; });
        if (it == o.pending.end())
            return Err(Fail(String("`scene.add` : ce nœud n'appartient pas à cette scène")));
        parent->Add(std::move(*it));  // déplace la propriété (raw reste valide)
        o.pending.erase(it);
        return Ok(args[0]);
    });
    builder.Method("root", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
        SceneObj &o = As<SceneObj>(self);
        if (!o.root) return Ok(Value::Nil());
        return Ok(MakeRawNodeHandle(o.root, dynamic_cast<render3d::Shape *>(o.root), &o, self->type));
    });
    builder.Method("render", 1, 1, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
        SceneObj &o = As<SceneObj>(self);
        if (!o.canvas) return Err(Fail(String("`scene.render` : pas de Canvas associé")));
        if (!args[0].IsHost()) return Err(Fail(String("`scene.render` : caméra attendue")));
        const auto *cam = dynamic_cast<const CameraObj *>(args[0].AsHost().get());
        if (!cam) return Err(Fail(String("`scene.render` : render3d.camera attendue")));
        if (!o.root) return Ok(Value::Nil());
        o.canvas->SetCamera(cam->camera);
        o.canvas->DrawObject(*o.root);
        return Ok(Value::Nil());
    });
    builder.type->display = [](const HostObject &) { return String("<render3d.scene>"); };
}

} // namespace

void InstallRender3DLibrary(Interpreter &vm, render3d::Canvas *canvas, render3d::Object3D *sceneRoot) {
    TypeBuilder mesh("render3d.mesh"), material("render3d.material"), camera("render3d.camera"),
        node("render3d.node"), scene("render3d.scene");
    DefineMesh(mesh);
    DefineMaterial(material);
    DefineCamera(camera);
    DefineNode(node);
    DefineScene(scene);
    vm.RegisterHostType(String("render3d"), String("mesh"), mesh.type);
    vm.RegisterHostType(String("render3d"), String("material"), material.type);
    vm.RegisterHostType(String("render3d"), String("camera"), camera.type);
    vm.RegisterHostType(String("render3d"), String("node"), node.type);
    auto sceneType = vm.RegisterHostType(String("render3d"), String("scene"), scene.type);
    // `render3d.scene()` sans argument : utilise la racine fournie à l'install.
    vm.RegisterNamespacedNative(
        String("render3d"), String("scene"), 0, 0,
        [sceneRoot, canvas, sceneType](Interpreter &, Args &) -> Result<Value, ScriptError> {
            auto object = std::make_shared<SceneObj>();
            object->type = sceneType;
            object->root = sceneRoot;
            object->canvas = canvas;
            return Ok(Value::Host(std::move(object)));
        });
    // Expose `math.FVector3`/`math.FQuaternion` en entrée de `set_*`.
    // (Déjà fournis par le sous-module `math` de l'interpréteur.)
}

} // namespace script