#include <script_bindings/script_physics.hpp>

namespace script {
namespace {

using lib::Args;
using lib::As;
using lib::Fail;
using lib::TypeBuilder;

struct BodyObj : HostObject {
    physics::World *world = nullptr;
    ecs::Entity entity{};
};

struct ShapeObj : HostObject {
    physics::Shape shape;
};

struct WorldObj : HostObject {
    physics::World *world = nullptr;
};

Result<math::FVector3, ScriptError> Vec3Of(const Value &v, const char *fn) {
    if (v.IsList() && v.AsList() && v.AsList()->Size() == 3) {
        const std::vector<Value> it = v.AsList()->Snapshot();
        return Ok(math::FVector3{it[0].AsFloat(), it[1].AsFloat(), it[2].AsFloat()});
    }
    if (v.IsMap() && v.AsMap()) {
        auto g = [&](const char *k) { return v.AsMap()->Get(String(k)).UnwrapOr(Value::Number(0.0)).AsFloat(); };
        return Ok(math::FVector3{g("x"), g("y"), g("z")});
    }
    return Err(Fail(String::Format("`%s` : vec3 attendu", fn)));
}

Value Vec3Value(const math::FVector3 &v) {
    auto l = std::make_shared<ListObject>();
    l->items = {Value::Number(v.x), Value::Number(v.y), Value::Number(v.z)};
    return Value::List(std::move(l));
}

/// Fabrique une poignée `physics.body` — factorisé pour les sites `spawn`.
Value MakeBodyHandle(physics::World &world, ecs::Entity e) {
    auto o = std::make_shared<BodyObj>();
    o->world = &world;
    o->entity = e;
    return Value::Host(std::move(o));
}

// ── Shape ────────────────────────────────────────────────────────────────

void DefineShape(TypeBuilder &b) {
    std::weak_ptr<const HostType> wt = b.type;
    auto make = [wt](physics::Shape s) {
        auto o = std::make_shared<ShapeObj>();
        o->type = wt.lock();
        o->shape = std::move(s);
        return Value::Host(std::move(o));
    };
    b.StaticFn("sphere", 1, 1, [make](Interpreter &, Args &a) {
        return Ok(make(physics::Sphere{{0, 0, 0}, float(a[0].AsFloat())}));
    });
    // Box : demi-étendues comme `physics::Box`.
    b.StaticFn("box", 1, 1, [make](Interpreter &, Args &a) -> Result<Value, ScriptError> {
        auto v = Vec3Of(a[0], "physics.box");
        if (v.IsError()) return Err(v.Error());
        return Ok(make(physics::Box{{0, 0, 0}, v.Value()}));
    });
    b.StaticFn("capsule", 2, 2, [make](Interpreter &, Args &a) {
        return Ok(make(physics::Capsule{{0, 0, 0}, float(a[0].AsFloat()), float(a[1].AsFloat())}));
    });
    b.type->display = [](const HostObject &) { return String("<physics.shape>"); };
}

// ── Body ────────────────────────────────────────────────────────────────

void DefineBody(TypeBuilder &b) {
    auto pair = [](const HostRef &self) -> std::pair<physics::World *, ecs::Entity> {
        BodyObj &o = As<BodyObj>(self);
        return {o.world, o.entity};
    };
    b.Method("apply_impulse", 1, 1, [pair](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        auto v = Vec3Of(a[0], "body.apply_impulse");
        if (v.IsError()) return Err(v.Error());
        auto [w, e] = pair(self);
        if (auto rb = w->Registry().GetComponent<physics::RigidBody>(e); rb.IsSome())
            rb.Unwrap()->linearVelocity += v.Value() * rb.Unwrap()->invMass;
        return Ok(Value::Host(self));
    });
    b.Method("apply_force", 1, 1, [pair](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        auto v = Vec3Of(a[0], "body.apply_force");
        if (v.IsError()) return Err(v.Error());
        auto [w, e] = pair(self);
        if (auto rb = w->Registry().GetComponent<physics::RigidBody>(e); rb.IsSome())
            rb.Unwrap()->linearVelocity += v.Value() * rb.Unwrap()->invMass * (1.f / 60.f);
        return Ok(Value::Host(self));
    });
    b.type->get = [](const HostObject &self, const String &name) -> Option<Value> {
        const BodyObj &o = As<BodyObj>(self);
        if (!o.world) return NONE;
        auto rb = o.world->Registry().GetComponent<physics::RigidBody>(o.entity);
        if (rb.IsNone()) return NONE;
        const auto &r = *rb.Unwrap();
        if (name == "position") return Some(Vec3Value(r.position));
        if (name == "velocity") return Some(Vec3Value(r.linearVelocity));
        if (name == "angular_velocity") return Some(Vec3Value(r.angularVelocity));
        if (name == "mass") return Some(Value::Number(double(r.mass)));
        if (name == "restitution") return Some(Value::Number(double(r.restitution)));
        if (name == "friction") return Some(Value::Number(double(r.friction)));
        if (name == "is_static") return Some(Value::Boolean(r.invMass <= 0.f));
        if (name == "entity") return Some(Value::UInt(uint64_t(o.entity.id) | (uint64_t(o.entity.generation) << 32)));
        return NONE;
    };
    b.type->set = [](Interpreter &, const HostRef &self, const String &name,
                     const Value &v) -> Result<bool, ScriptError> {
        BodyObj &o = As<BodyObj>(self);
        if (!o.world) return Ok(false);
        auto rb = o.world->Registry().GetComponent<physics::RigidBody>(o.entity);
        if (rb.IsNone()) return Ok(false);
        auto &r = *rb.Unwrap();
        if (name == "position") { auto p = Vec3Of(v, "body.position"); if (p.IsError()) return Err(p.Error()); r.position = p.Value(); return Ok(true); }
        if (name == "velocity") { auto p = Vec3Of(v, "body.velocity"); if (p.IsError()) return Err(p.Error()); r.linearVelocity = p.Value(); return Ok(true); }
        if (name == "angular_velocity") { auto p = Vec3Of(v, "body.angular_velocity"); if (p.IsError()) return Err(p.Error()); r.angularVelocity = p.Value(); return Ok(true); }
        if (name == "restitution" && v.IsNumber()) { r.restitution = float(v.AsFloat()); return Ok(true); }
        if (name == "friction" && v.IsNumber())    { r.friction = float(v.AsFloat());    return Ok(true); }
        return Ok(false);
    };
    b.type->display = [](const HostObject &) { return String("<physics.body>"); };
}

// ── World ────────────────────────────────────────────────────────────────

void DefineWorld(TypeBuilder &b) {
    b.Method("step", 1, 1, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        As<WorldObj>(self).world->Step(float(a[0].AsFloat()));
        return Ok(Value::Nil());
    });
    b.Method("spawn", 2, 3, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        WorldObj &o = As<WorldObj>(self);
        if (!a[0].IsHost()) return Err(Fail(String("`spawn` : physics.shape attendu")));
        const auto *shape = dynamic_cast<const ShapeObj *>(a[0].AsHost().get());
        if (!shape) return Err(Fail(String("`spawn` : physics.shape attendu")));
        auto pos = Vec3Of(a[1], "spawn");
        if (pos.IsError()) return Err(pos.Error());
        const float mass = a.size() > 2 ? float(a[2].AsFloat()) : 1.f;
        const ecs::Entity e = o.world->Registry().Spawn();
        o.world->Registry().AddComponent(e, physics::RigidBody::MakeDynamic(shape->shape, pos.Value(), mass));
        return Ok(MakeBodyHandle(*o.world, e));
    });
    b.Method("spawn_static", 2, 2, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        WorldObj &o = As<WorldObj>(self);
        if (!a[0].IsHost()) return Err(Fail(String("`spawn_static` : physics.shape attendu")));
        const auto *shape = dynamic_cast<const ShapeObj *>(a[0].AsHost().get());
        if (!shape) return Err(Fail(String("`spawn_static` : physics.shape attendu")));
        auto pos = Vec3Of(a[1], "spawn_static");
        if (pos.IsError()) return Err(pos.Error());
        const ecs::Entity e = o.world->Registry().Spawn();
        o.world->Registry().AddComponent(e, physics::RigidBody::MakeStatic(shape->shape, pos.Value()));
        return Ok(MakeBodyHandle(*o.world, e));
    });
    b.Method("despawn", 1, 1, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
        WorldObj &o = As<WorldObj>(self);
        if (!a[0].IsHost()) return Ok(Value::Boolean(false));
        const auto *body = dynamic_cast<const BodyObj *>(a[0].AsHost().get());
        if (!body) return Ok(Value::Boolean(false));
        return Ok(Value::Boolean(o.world->Registry().Despawn(body->entity)));
    });
    b.type->get = [](const HostObject &self, const String &name) -> Option<Value> {
        const WorldObj &o = As<WorldObj>(self);
        if (name == "gravity") return Some(Vec3Value(o.world->config.gravity));
        if (name == "solver_iterations") return Some(Value::Int(o.world->config.solverIterations));
        if (name == "joint_iterations") return Some(Value::Int(o.world->config.jointIterations));
        return NONE;
    };
    b.type->set = [](Interpreter &, const HostRef &self, const String &name,
                     const Value &v) -> Result<bool, ScriptError> {
        WorldObj &o = As<WorldObj>(self);
        if (name == "gravity") { auto g = Vec3Of(v, "gravity"); if (g.IsError()) return Err(g.Error()); o.world->config.gravity = g.Value(); return Ok(true); }
        if (name == "solver_iterations" && v.IsNumber()) { o.world->config.solverIterations = int(v.AsInt64()); return Ok(true); }
        if (name == "joint_iterations" && v.IsNumber())  { o.world->config.jointIterations = int(v.AsInt64());  return Ok(true); }
        return Ok(false);
    };
    b.type->display = [](const HostObject &) { return String("<physics.world>"); };
}

} // namespace

void InstallPhysicsLibrary(Interpreter &vm, physics::World &world) {
    TypeBuilder shape("physics.shape"), body("physics.body"), worldB("physics.world");
    DefineShape(shape);
    DefineBody(body);
    DefineWorld(worldB);
    vm.RegisterHostType(String("physics"), String("shape"), shape.type);
    vm.RegisterHostType(String("physics"), String("body"), body.type);
    auto worldType = vm.RegisterHostType(String("physics"), String("world"), worldB.type);

    vm.RegisterNamespacedNative(
        String("physics"), String("world"), 0, 0,
        [&world, worldType](Interpreter &, Args &) -> Result<Value, ScriptError> {
            auto o = std::make_shared<WorldObj>();
            o->type = worldType;
            o->world = &world;
            return Ok(Value::Host(std::move(o)));
        });
}

} // namespace script