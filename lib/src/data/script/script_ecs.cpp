// Définitions de data/script/script_ecs.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "data/script.hpp"
#include "data/script/script_ecs.hpp"

namespace data::script {

// ── ScriptComponents ─────────────────────────────────────────────────────────

Value * ScriptComponents::Find(const String &name) noexcept {
	for (auto &item : items)
		if (item.first == name)
			return &item.second;
	return nullptr;
}

// ── EcsWorld ─────────────────────────────────────────────────────────────────

const EcsWorld::HostComponent * EcsWorld::FindHost(const String &name) const noexcept {
	for (const HostComponent &component : m_hostComponents)
		if (component.name == name)
			return &component;
	return nullptr;
}

namespace ecslib {

ecs::Entity UnpackEntity(uint64_t id) {
	return ecs::Entity{uint32_t(id & 0xFFFFFFFFu), uint32_t(id >> 32)};
}

std::shared_ptr<const HostType> EntityType(Interpreter &vm) {
	return lib::HostTypeOf(vm, "ecs", "entity");
}

Value MakeEntity(Interpreter &vm, const std::shared_ptr<EcsWorld> &world, ecs::Entity entity) {
	auto object = std::make_shared<EntityObject>();
	object->type = EntityType(vm);
	object->world = world;
	object->entity = entity;
	return Value::Host(std::move(object));
}

bool HasComponent(EcsWorld &world, ecs::Entity e, const String &name) {
	ecs::ArchetypeRegistry &r = world.Registry();
	if (!r.IsAlive(e))
		return false;
	if (const EcsWorld::HostComponent *host = world.FindHost(name))
		return host->has(r, e);
	auto script = r.GetComponent<ScriptComponents>(e);
	return script.IsSome() && script.Unwrap()->Find(name) != nullptr;
}

Value GetComponent(EcsWorld &world, ecs::Entity e, const String &name) {
	ecs::ArchetypeRegistry &r = world.Registry();
	if (!r.IsAlive(e))
		return Value::Nil();
	if (const EcsWorld::HostComponent *host = world.FindHost(name))
		return host->has(r, e) ? host->get(r, e) : Value::Nil();
	auto script = r.GetComponent<ScriptComponents>(e);
	if (script.IsNone())
		return Value::Nil();
	const Value *found = script.Unwrap()->Find(name);
	return found ? *found : Value::Nil();
}

Option<ScriptError> SetComponent(EcsWorld &world, ecs::Entity e, const String &name, Value value) {
	ecs::ArchetypeRegistry &r = world.Registry();
	if (!r.IsAlive(e))
		return Some(Fail(String::Format("`ecs` : entité morte (composant `%s`)", name.CStr())));
	if (const EcsWorld::HostComponent *host = world.FindHost(name)) {
		if (!host->set)
			return Some(Fail(String::Format("`ecs` : le composant `%s` de l'hôte est en lecture seule", name.CStr())));
		if (Option<String> bad = host->set(r, e, value); bad.IsSome())
			return Some(Fail(String::Format("`ecs` : composant `%s` : %s", name.CStr(), bad.Value().CStr())));
		return NONE;
	}
	auto script = r.GetOrAddComponent<ScriptComponents>(e);
	if (script.IsNone())
		return Some(Fail(String("`ecs` : entité inaccessible")));
	if (Value *existing = script.Unwrap()->Find(name))
		*existing = std::move(value);
	else
		script.Unwrap()->items.emplace_back(name, std::move(value));
	return NONE;
}

bool RemoveComponent(EcsWorld &world, ecs::Entity e, const String &name) {
	ecs::ArchetypeRegistry &r = world.Registry();
	if (!r.IsAlive(e))
		return false;
	if (const EcsWorld::HostComponent *host = world.FindHost(name))
		return host->remove && host->has(r, e) && host->remove(r, e);
	auto script = r.GetComponent<ScriptComponents>(e);
	if (script.IsNone())
		return false;
	auto &items = script.Unwrap()->items;
	for (size_t i = 0; i < items.size(); ++i)
		if (items[i].first == name) {
			items.erase(items.begin() + ptrdiff_t(i));
			return true;
		}
	return false;
}

std::vector<String> ComponentNames(EcsWorld &world, ecs::Entity e) {
	std::vector<String> names;
	ecs::ArchetypeRegistry &r = world.Registry();
	if (!r.IsAlive(e))
		return names;
	for (const EcsWorld::HostComponent &host : world.HostComponents())
		if (host.has(r, e))
			names.push_back(host.name);
	if (auto script = r.GetComponent<ScriptComponents>(e); script.IsSome())
		for (const auto &item : script.Unwrap()->items)
			names.push_back(item.first);
	return names;
}

std::vector<ecs::Entity> AllEntities(EcsWorld &world) {
	ecs::ArchetypeRegistry &r = world.Registry();
	std::vector<ecs::Entity> out = r.EntitiesWith<ScriptComponents>();
	for (const EcsWorld::HostComponent &host : world.HostComponents())
		for (ecs::Entity e : host.entities(r))
			out.push_back(e);
	// Ordre total (index, génération) : Entity::operator< ne compare que des
	// entités de même génération, ce qui n'est pas un ordre strict faible.
	std::sort(out.begin(), out.end(), [](ecs::Entity a, ecs::Entity b) {
		return a.id != b.id ? a.id < b.id : a.generation < b.generation;
	});
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}

std::vector<ecs::Entity> Query(EcsWorld &world, const std::vector<String> &names) {
	std::vector<ecs::Entity> out;
	for (ecs::Entity e : AllEntities(world)) {
		bool all = true;
		for (const String &name : names)
			if (!HasComponent(world, e, name)) {
				all = false;
				break;
			}
		if (all)
			out.push_back(e);
	}
	return out;
}

Result<ecs::Entity, ScriptError> ArgEntity(const Args &args, size_t i, const EcsWorld &world, const char *fn) {
	if (i < args.size() && args[i].IsHost() && args[i].AsHost()) {
		if (const auto *entity = dynamic_cast<const EntityObject *>(args[i].AsHost().get())) {
			if (entity->world.get() != &world)
				return Err(Fail(String::Format("`%s` : cette entité appartient à un autre monde", fn)));
			return Ok(entity->entity);
		}
	}
	if (i < args.size() && args[i].IsInteger())
		return Ok(UnpackEntity(args[i].AsUInt64()));
	return Err(Fail(String::Format("`%s` : entité attendue, trouvé `%s`", fn,
								   i < args.size() ? args[i].TypeName() : "rien")));
}

Result<std::vector<String>, ScriptError> ArgNames(const Args &args, size_t from, size_t to, const char *fn) {
	std::vector<String> names;
	for (size_t i = from; i < to && i < args.size(); ++i) {
		if (args[i].IsString()) {
			names.push_back(args[i].AsString());
		} else if (args[i].IsList() && args[i].AsList()) {
			for (const Value &item : args[i].AsList()->Snapshot()) {
				if (!item.IsString())
					return Err(Fail(String::Format("`%s` : nom de composant attendu, trouvé `%s`", fn, item.TypeName())));
				names.push_back(item.AsString());
			}
		} else {
			return Err(Fail(String::Format("`%s` : nom(s) de composant attendu(s), trouvé `%s`", fn, args[i].TypeName())));
		}
	}
	return Ok(std::move(names));
}

Result<int64_t, ScriptError> RunQuery(Interpreter &vm, const std::shared_ptr<EcsWorld> &world,
		const std::vector<String> &names, const Value &fn,
		const Option<Value> &extra) {
	std::vector<ecs::Entity> entities;
	{
		std::lock_guard<std::mutex> lock(world->Mutex());
		entities = Query(*world, names);
	}
	int64_t calls = 0;
	for (ecs::Entity e : entities) {
		Args args;
		{
			std::lock_guard<std::mutex> lock(world->Mutex());
			if (!world->Registry().IsAlive(e))
				continue; // détruite par un appel précédent
			bool complete = true;
			for (const String &name : names) {
				if (!HasComponent(*world, e, name)) {
					complete = false;
					break;
				}
				args.push_back(GetComponent(*world, e, name));
			}
			if (!complete)
				continue;
		}
		args.insert(args.begin(), MakeEntity(vm, world, e));
		if (extra.IsSome())
			args.push_back(extra.Value());
		auto result = vm.CallValue(fn, std::move(args), 0, 0);
		if (result.IsError())
			return Err(result.Error());
		++calls;
	}
	return Ok(calls);
}

void DefineWorld(TypeBuilder &builder, bool owned) {
	using Object = WorldObject;
	auto worldOf = [](const HostRef &self) -> std::shared_ptr<EcsWorld> { return As<Object>(self).world; };
	if (owned)
		builder.Construct(0, 0, [weak = std::weak_ptr<const HostType>(builder.type)](Interpreter &, Args &,
																					  const Args &) -> Result<Value, ScriptError> {
			auto object = std::make_shared<Object>();
			object->type = weak.lock();
			object->world = std::make_shared<EcsWorld>();
			return Ok(Value::Host(std::move(object)));
		});

	// `spawn()` ou `spawn({nom: valeur, …})`.
	builder.Method("spawn", 0, 1, [worldOf](Interpreter &vm, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		if (!args.empty() && !(args[0].IsMap() && args[0].AsMap()))
			return Err(Fail(String::Format("`ecs.world.spawn` : table de composants attendue, trouvé `%s`", args[0].TypeName())));
		std::lock_guard<std::mutex> lock(world->Mutex());
		const ecs::Entity e = world->Registry().Spawn();
		world->Registry().AddComponent(e, ScriptComponents{});
		if (!args.empty())
			for (auto &entry : args[0].AsMap()->Snapshot())
				if (auto error = SetComponent(*world, e, entry.first, entry.second); error.IsSome())
					return Err(error.Unwrap());
		return Ok(MakeEntity(vm, world, e));
	});
	builder.Method("despawn", 1, 1, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto e = ArgEntity(args, 0, *world, "ecs.world.despawn");
		if (e.IsError())
			return Err(e.Error());
		ScriptComponents released; // valeurs détruites hors du verrou
		std::lock_guard<std::mutex> lock(world->Mutex());
		if (auto script = world->Registry().GetComponent<ScriptComponents>(e.Value()); script.IsSome())
			released.items.swap(script.Unwrap()->items);
		return Ok(Value::Boolean(world->Registry().Despawn(e.Value())));
	});
	builder.Method("alive", 1, 1, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto e = ArgEntity(args, 0, *world, "ecs.world.alive");
		if (e.IsError())
			return Err(e.Error());
		std::lock_guard<std::mutex> lock(world->Mutex());
		return Ok(Value::Boolean(world->Registry().IsAlive(e.Value())));
	});
	builder.Method("entity", 1, 1, [worldOf](Interpreter &vm, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto e = ArgEntity(args, 0, *world, "ecs.world.entity");
		if (e.IsError())
			return Err(e.Error());
		std::lock_guard<std::mutex> lock(world->Mutex());
		if (!world->Registry().IsAlive(e.Value()))
			return Ok(Value::Nil());
		return Ok(MakeEntity(vm, world, e.Value()));
	});
	builder.Method("set", 3, 3, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto e = ArgEntity(args, 0, *world, "ecs.world.set");
		auto name = lib::ArgText(args, 1, "ecs.world.set");
		if (e.IsError())
			return Err(e.Error());
		if (name.IsError())
			return Err(name.Error());
		std::lock_guard<std::mutex> lock(world->Mutex());
		if (auto error = SetComponent(*world, e.Value(), name.Value(), args[2]); error.IsSome())
			return Err(error.Unwrap());
		return Ok(args[2]);
	});
	builder.Method("get", 2, 3, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto e = ArgEntity(args, 0, *world, "ecs.world.get");
		auto name = lib::ArgText(args, 1, "ecs.world.get");
		if (e.IsError())
			return Err(e.Error());
		if (name.IsError())
			return Err(name.Error());
		std::lock_guard<std::mutex> lock(world->Mutex());
		if (!HasComponent(*world, e.Value(), name.Value()))
			return Ok(args.size() > 2 ? args[2] : Value::Nil());
		return Ok(GetComponent(*world, e.Value(), name.Value()));
	});
	builder.Method("has", 2, -1, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto e = ArgEntity(args, 0, *world, "ecs.world.has");
		if (e.IsError())
			return Err(e.Error());
		auto names = ArgNames(args, 1, args.size(), "ecs.world.has");
		if (names.IsError())
			return Err(names.Error());
		std::lock_guard<std::mutex> lock(world->Mutex());
		for (const String &name : names.Value())
			if (!HasComponent(*world, e.Value(), name))
				return Ok(Value::Boolean(false));
		return Ok(Value::Boolean(true));
	});
	builder.Method("remove", 2, 2, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto e = ArgEntity(args, 0, *world, "ecs.world.remove");
		auto name = lib::ArgText(args, 1, "ecs.world.remove");
		if (e.IsError())
			return Err(e.Error());
		if (name.IsError())
			return Err(name.Error());
		std::lock_guard<std::mutex> lock(world->Mutex());
		return Ok(Value::Boolean(RemoveComponent(*world, e.Value(), name.Value())));
	});
	builder.Method("components", 1, 1, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto e = ArgEntity(args, 0, *world, "ecs.world.components");
		if (e.IsError())
			return Err(e.Error());
		auto list = std::make_shared<ListObject>();
		std::lock_guard<std::mutex> lock(world->Mutex());
		for (const String &name : ComponentNames(*world, e.Value()))
			list->items.push_back(Value::Str(name));
		return Ok(Value::List(std::move(list)));
	});
	// `query("pos", "vit")` : les entités ; `query(["pos", "vit"], fn(e, pos, vit) { … })` :
	// appelle `fn` pour chacune et rend leur nombre.
	builder.Method("query", 0, -1, [worldOf](Interpreter &vm, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		const bool withFn = !args.empty() && vm.IsCallableValue(args.back()) && !args.back().IsString();
		auto names = ArgNames(args, 0, withFn ? args.size() - 1 : args.size(), "ecs.world.query");
		if (names.IsError())
			return Err(names.Error());
		if (withFn) {
			auto calls = RunQuery(vm, world, names.Value(), args.back(), NONE);
			if (calls.IsError())
				return Err(calls.Error());
			return Ok(Value::Int(calls.Value()));
		}
		std::vector<ecs::Entity> entities;
		{
			std::lock_guard<std::mutex> lock(world->Mutex());
			entities = Query(*world, names.Value());
		}
		auto list = std::make_shared<ListObject>();
		for (ecs::Entity e : entities)
			list->items.push_back(MakeEntity(vm, world, e));
		return Ok(Value::List(std::move(list)));
	});
	builder.Alias("each", "query");
	builder.Method("count", 0, -1, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto names = ArgNames(args, 0, args.size(), "ecs.world.count");
		if (names.IsError())
			return Err(names.Error());
		std::lock_guard<std::mutex> lock(world->Mutex());
		return Ok(Value::Int(int64_t(Query(*world, names.Value()).size())));
	});
	// Systèmes : `system(nom, [composants], fn(e, …composants, dt))`, exécutés
	// dans leur ordre d'enregistrement par `run(dt)`.
	builder.Method("system", 3, 3, [worldOf](Interpreter &vm, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto name = lib::ArgText(args, 0, "ecs.world.system");
		if (name.IsError())
			return Err(name.Error());
		auto names = ArgNames(args, 1, 2, "ecs.world.system");
		if (names.IsError())
			return Err(names.Error());
		if (auto error = lib::ArgCallable(vm, args, 2, "ecs.world.system"); error.IsSome())
			return Err(error.Unwrap());
		std::lock_guard<std::mutex> lock(world->Mutex());
		for (EcsWorld::System &system : world->Systems())
			if (system.name == name.Value()) {
				system.components = names.Unwrap();
				system.fn = args[2];
				return Ok(Value::Host(self));
			}
		world->Systems().push_back(EcsWorld::System{name.Unwrap(), names.Unwrap(), args[2]});
		return Ok(Value::Host(self));
	});
	builder.Method("remove_system", 1, 1, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto name = lib::ArgText(args, 0, "ecs.world.remove_system");
		if (name.IsError())
			return Err(name.Error());
		std::lock_guard<std::mutex> lock(world->Mutex());
		auto &systems = world->Systems();
		for (size_t i = 0; i < systems.size(); ++i)
			if (systems[i].name == name.Value()) {
				systems.erase(systems.begin() + ptrdiff_t(i));
				return Ok(Value::Boolean(true));
			}
		return Ok(Value::Boolean(false));
	});
	builder.Method("systems", 0, 0, [worldOf](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto list = std::make_shared<ListObject>();
		std::lock_guard<std::mutex> lock(world->Mutex());
		for (const EcsWorld::System &system : world->Systems())
			list->items.push_back(Value::Str(system.name));
		return Ok(Value::List(std::move(list)));
	});
	builder.Method("run", 0, 1, [worldOf](Interpreter &vm, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		const Value dt = args.empty() ? Value::Number(0.0) : args[0];
		std::vector<EcsWorld::System> systems;
		{
			std::lock_guard<std::mutex> lock(world->Mutex());
			systems = world->Systems();
		}
		int64_t calls = 0;
		for (const EcsWorld::System &system : systems) {
			auto ran = RunQuery(vm, world, system.components, system.fn, Some(dt));
			if (ran.IsError()) {
				ScriptError error = ran.Error();
				error.message = String::Format("système `%s` : %s", system.name.CStr(), error.message.CStr());
				return Err(std::move(error));
			}
			calls += ran.Value();
		}
		return Ok(Value::Int(calls));
	});
	// Ressources : des singletons nommés du monde.
	builder.Method("set_resource", 2, 2, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto name = lib::ArgText(args, 0, "ecs.world.set_resource");
		if (name.IsError())
			return Err(name.Error());
		std::lock_guard<std::mutex> lock(world->Mutex());
		ecs::Resources &resources = world->Registry().resources;
		if (!resources.Contains<ScriptResources>())
			resources.Insert(ScriptResources{});
		auto store = resources.Get<ScriptResources>();
		for (auto &item : store.Unwrap()->items)
			if (item.first == name.Value()) {
				item.second = args[1];
				return Ok(args[1]);
			}
		store.Unwrap()->items.emplace_back(name.Unwrap(), args[1]);
		return Ok(args[1]);
	});
	builder.Method("resource", 1, 2, [worldOf](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto name = lib::ArgText(args, 0, "ecs.world.resource");
		if (name.IsError())
			return Err(name.Error());
		std::lock_guard<std::mutex> lock(world->Mutex());
		auto store = world->Registry().resources.Get<ScriptResources>();
		if (store.IsSome())
			for (const auto &item : store.Unwrap()->items)
				if (item.first == name.Value())
					return Ok(item.second);
		return Ok(args.size() > 1 ? args[1] : Value::Nil());
	});
	builder.Method("entities", 0, 0, [worldOf](Interpreter &vm, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		std::vector<ecs::Entity> entities;
		{
			std::lock_guard<std::mutex> lock(world->Mutex());
			entities = AllEntities(*world);
		}
		auto list = std::make_shared<ListObject>();
		for (ecs::Entity e : entities)
			list->items.push_back(MakeEntity(vm, world, e));
		return Ok(Value::List(std::move(list)));
	});
	// Détruit toutes les entités du script (monde possédé : TOUTES les entités).
	builder.Method("clear", 0, 0, [worldOf](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		std::vector<ScriptComponents> released;
		std::lock_guard<std::mutex> lock(world->Mutex());
		ecs::ArchetypeRegistry &r = world->Registry();
		for (ecs::Entity e : r.EntitiesWith<ScriptComponents>()) {
			if (auto script = r.GetComponent<ScriptComponents>(e); script.IsSome()) {
				released.emplace_back();
				released.back().items.swap(script.Unwrap()->items);
			}
			if (world->IsOwned())
				(void)r.Despawn(e);
			else
				(void)r.RemoveComponent<ScriptComponents>(e);
		}
		return Ok(Value::Int(int64_t(released.size())));
	});
	builder.Method("host_components", 0, 0, [worldOf](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		std::shared_ptr<EcsWorld> world = worldOf(self);
		auto list = std::make_shared<ListObject>();
		for (const EcsWorld::HostComponent &component : world->HostComponents())
			list->items.push_back(Value::Str(component.name));
		return Ok(Value::List(std::move(list)));
	});

	HostType &type = *builder.type;
	type.mainThreadOnly = !owned;
	type.size = [](const HostObject &self) {
		const std::shared_ptr<EcsWorld> &world = As<Object>(self).world;
		std::lock_guard<std::mutex> lock(world->Mutex());
		return AllEntities(*world).size();
	};
	type.display = [owned](const HostObject &self) {
		const std::shared_ptr<EcsWorld> &world = As<Object>(self).world;
		std::lock_guard<std::mutex> lock(world->Mutex());
		return String::Format("<ecs.%s : %d entité(s), %d système(s)>", owned ? "world" : "host", int(AllEntities(*world).size()),
							  int(world->Systems().size()));
	};
	// `{pos: …} -> monde` : crée une entité avec ces composants.
	type.flowIn = [](Interpreter &vm, const HostRef &self, const Value &value) -> Result<Value, ScriptError> {
		const HostMethod *spawn = self->type->FindMethod(String("spawn"));
		Args args{value};
		return spawn->fn(vm, self, args);
	};
}

void DefineEntity(TypeBuilder &builder) {
	using Object = EntityObject;
	auto call = [](const char *method) {
		// e.get(nom) … : la méthode du monde, l'entité en premier argument.
		return [method](Interpreter &vm, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
			const Object &o = As<Object>(self);
			auto world = std::make_shared<WorldObject>();
			world->world = o.world;
			world->type = lib::HostTypeOf(vm, "ecs", o.world->IsOwned() ? "world" : "host_world");
			if (!world->type)
				return Err(Fail(String("`ecs` : type de monde indisponible")));
			const HostMethod *target = world->type->FindMethod(String(method));
			Args full;
			full.push_back(Value::Host(self));
			for (Value &arg : args)
				full.push_back(std::move(arg));
			return target->fn(vm, world, full);
		};
	};
	builder.Method("get", 1, 2, call("get"));
	builder.Method("set", 2, 2, call("set"));
	builder.Method("has", 1, -1, call("has"));
	builder.Method("remove", 1, 1, call("remove"));
	builder.Method("components", 0, 0, call("components"));
	builder.Method("despawn", 0, 0, call("despawn"));
	builder.Method("alive", 0, 0, call("alive"));
	HostType &type = *builder.type;
	type.get = [](const HostObject &self, const String &name) -> Option<Value> {
		const Object &o = As<Object>(self);
		if (name == "id")
			return Some(Value::UInt(PackEntity(o.entity)));
		if (name == "index")
			return Some(Value::UInt(o.entity.id, NumberType::U32));
		if (name == "generation")
			return Some(Value::UInt(o.entity.generation, NumberType::U32));
		return NONE;
	};
	// `e["pos"]` / `e["pos"] = v` : raccourcis de get/set.
	type.index = [call](Interpreter &vm, const HostRef &self, const Value &key) -> Result<Value, ScriptError> {
		Args args{key};
		return call("get")(vm, self, args);
	};
	type.setIndex = [call](Interpreter &vm, const HostRef &self, const Value &key, Value value) -> Option<ScriptError> {
		Args args{key, std::move(value)};
		auto result = call("set")(vm, self, args);
		return result.IsError() ? Some(result.Error()) : Option<ScriptError>(NONE);
	};
	type.display = [](const HostObject &self) {
		const Object &o = As<Object>(self);
		return String::Format("Entity(%u v%u)", o.entity.id, o.entity.generation);
	};
	type.equals = [](const HostObject &a, const HostObject &b) {
		const Object &x = As<Object>(a), &y = As<Object>(b);
		return x.world == y.world && x.entity == y.entity;
	};
}

} // namespace ecslib

void InstallEcsLibrary(Interpreter &vm, std::shared_ptr<EcsWorld> hostWorld) {
	using namespace ecslib;
	TypeBuilder world("ecs.world"), host("ecs.host_world"), entity("ecs.entity");
	DefineWorld(world, true);
	DefineWorld(host, false);
	DefineEntity(entity);
	vm.RegisterHostType(String("ecs"), String("world"), world.type);
	vm.RegisterHostType(String("ecs"), String("host_world"), host.type);
	vm.RegisterHostType(String("ecs"), String("entity"), entity.type);
	std::shared_ptr<const HostType> hostType = host.type;
	vm.RegisterNamespacedNative(String("ecs"), String("host"), 0, 0,
								[hostWorld, hostType](Interpreter &, Args &) -> Result<Value, ScriptError> {
									if (!hostWorld)
										return Ok(Value::Nil());
									auto object = std::make_shared<WorldObject>();
									object->type = hostType;
									object->world = hostWorld;
									return Ok(Value::Host(std::move(object)));
								},
								Interpreter::NativeThread::ANY);
}

} // namespace data::script
