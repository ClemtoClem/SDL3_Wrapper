// Définitions de voxel/scripting.hpp
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/scripting.hpp>

#include <cmath>

#include <ecs/relations.hpp>

namespace voxel {

using script::Value;
using SE = script::ScriptError;
using NativeResult = Result<Value, SE>;
namespace sd = script::detail;

#define TRY(var, expr)                                                                                                         \
	auto var##_result = (expr);                                                                                                \
	if (var##_result.IsError())                                                                                                \
		return Err(var##_result.Error());                                                                                      \
	auto var = var##_result.Unwrap()

namespace {

Value Vec(const math::FVector3 &v) {
	auto map = std::make_shared<script::MapObject>();
	map->SetKey("x", Value::Number(v.x));
	map->SetKey("y", Value::Number(v.y));
	map->SetKey("z", Value::Number(v.z));
	return Value::Map(map);
}

Value PosValue(const BlockPos &p) {
	auto map = std::make_shared<script::MapObject>();
	map->SetKey("x", Value::Int(p.x));
	map->SetKey("y", Value::Int(p.y));
	map->SetKey("z", Value::Int(p.z));
	return Value::Map(map);
}

SE Error(const char *fn, const String &message) { return SE(String::Format("%s : %s", fn, message.CStr()), 0, 0); }

Result<BlockPos, SE> ArgPos(std::vector<Value> &args, size_t i, const char *fn) {
	TRY(x, sd::ArgNumber(args, i, fn));
	TRY(y, sd::ArgNumber(args, i + 1, fn));
	TRY(z, sd::ArgNumber(args, i + 2, fn));
	return Ok(BlockPos{int(std::floor(x)), int(std::floor(y)), int(std::floor(z))});
}

Result<math::FVector3, SE> ArgVec(std::vector<Value> &args, size_t i, const char *fn) {
	TRY(x, sd::ArgNumber(args, i, fn));
	TRY(y, sd::ArgNumber(args, i + 1, fn));
	TRY(z, sd::ArgNumber(args, i + 2, fn));
	return Ok(math::FVector3{float(x), float(y), float(z)});
}

/// Id de contenu écrit par un script : qualifié par le mod courant.
String Qualified(const String &id, const String &mod) { return ContentRegistry::Qualify(id, mod); }

} // namespace

ScriptHost::ScriptHost(Game &game) : m_game(game) { InstallApi(); }

ScriptHost::~ScriptHost() = default;

Value ScriptHost::EntityValue(ecs::Entity e) const {
	return e.Valid() ? Value::Int(int64_t(uint64_t(e.id) | (uint64_t(e.generation) << 32))) : Value::Nil();
}

ecs::Entity ScriptHost::EntityOf(const Value &v) const {
	if (!v.IsNumber())
		return ecs::Entity{};
	const uint64_t raw = uint64_t(v.AsInt64());
	ecs::Entity e;
	e.id = uint32_t(raw & 0xFFFFFFFFu);
	e.generation = uint32_t(raw >> 32);
	return m_game.Registry().IsAlive(e) ? e : ecs::Entity{};
}

void ScriptHost::Report(const String &where, const SE &error) {
	m_game.LogLine(String::Format("[script %s] %s", where.CStr(), error.Format().CStr()));
}

Result<Value, SE> ScriptHost::Call(const Value &fn, std::vector<Value> args) { return m_vm.CallValue(fn, std::move(args), 0, 0); }

Result<Value, SE> ScriptHost::Call(const Owned &callback, std::vector<Value> args) {
	const String previous = m_currentMod;
	m_currentMod = callback.mod;
	Result<Value, SE> r = m_vm.CallValue(callback.fn, std::move(args), 0, 0);
	m_currentMod = previous;
	return r;
}

Option<String> ScriptHost::Validate(const String &source, const String &name) const {
	Result<script::Program, SE> program = script::Parser::Compile(source.View());
	if (program.IsError())
		return Some(String::Format("%s:%s", name.CStr(), program.Error().Format().CStr()));
	return NONE;
}

Result<bool, String> ScriptHost::RunFile(const String &path, const String &mod) {
	Result<String, String> source = script::LoadScriptFile(path);
	if (source.IsError())
		return Err(source.Error());
	if (Option<String> invalid = Validate(source.Value(), path); invalid.IsSome())
		return Err(invalid.Unwrap());
	const String previous = m_currentMod;
	m_currentMod = mod;
	Result<Value, SE> run = m_vm.Run(source.Value().View());
	m_currentMod = previous;
	if (run.IsError())
		return Err(String::Format("%s:%s", path.CStr(), run.Error().Format().CStr()));
	return Ok(true);
}

Result<String, String> ScriptHost::RunSource(const String &source, const String &name) {
	if (Option<String> invalid = Validate(source, name); invalid.IsSome())
		return Err(invalid.Unwrap());
	Result<Value, SE> run = m_vm.Run(source.View());
	if (run.IsError())
		return Err(run.Error().Format());
	return Ok(run.Value().IsNil() ? String() : run.Value().ToDisplayString());
}

bool ScriptHost::HasHandlers(const String &event) const {
	auto it = m_handlers.find(event.CStr());
	return it != m_handlers.end() && !it->second.empty();
}

bool ScriptHost::Emit(const String &event, std::vector<Value> args) {
	auto it = m_handlers.find(event.CStr());
	if (it == m_handlers.end())
		return true;
	bool allowed = true;
	const std::vector<Owned> handlers = it->second; // un gestionnaire peut en ajouter
	for (const Owned &fn : handlers) {
		Result<Value, SE> r = Call(fn, args);
		if (r.IsError())
			Report(event, r.Error());
		else if (r.Value().IsBoolean() && !r.Value().AsBoolean())
			allowed = false;
	}
	return allowed;
}

bool ScriptHost::RunBehavior(ecs::Entity entity, BehaviorState &state, float dt) {
	Owned fn;
	if (auto it = m_behaviors.find(state.id.CStr()); it != m_behaviors.end()) {
		fn = it->second;
	} else if (const BehaviorDef *def = m_game.Content().FindBehavior(state.id); def && !def->script.IsEmpty()) {
		Option<Value> global = m_vm.GetGlobal(def->script);
		if (global.IsNone())
			return false;
		fn = Owned{global.Unwrap(), def->mod};
		if (!state.params && def->params)
			state.params = def->params;
	} else {
		return false;
	}
	Value params = state.params ? script::ValueFromNode(state.params) : Value::EmptyMap();
	Result<Value, SE> r = Call(fn, {EntityValue(entity), params, Value::Number(dt)});
	if (r.IsError()) {
		Report(state.id, r.Error());
		return false;
	}
	return true;
}

TerrainGenerator::ScriptFn ScriptHost::Generator() {
	return [this](Chunk &chunk, World &world, const GeneratorDef &def) {
		Owned fn;
		if (auto it = m_generators.find(def.id.CStr()); it != m_generators.end())
			fn = it->second;
		else if (Option<Value> global = m_vm.GetGlobal(def.script); global.IsSome())
			fn = Owned{global.Unwrap(), def.mod};
		else
			return;
		m_genChunk = &chunk;
		m_genWorld = &world;
		const BlockPos o = chunk.Pos().Origin();
		Result<Value, SE> r = Call(fn, {Value::Int(o.x), Value::Int(o.y), Value::Int(o.z), Value::Int(world.Settings().seed)});
		if (r.IsError())
			Report(def.id, r.Error());
		m_genChunk = nullptr;
		m_genWorld = nullptr;
	};
}

void ScriptHost::RunSystems(float dt) {
	for (const auto &[name, fn] : m_systems) {
		Result<Value, SE> r = Call(fn, {Value::Number(dt)});
		if (r.IsError())
			Report(name, r.Error());
	}
}

// ── API ──────────────────────────────────────────────────────────────────────

void ScriptHost::InstallApi() {
	script::Interpreter &vm = m_vm;

	vm.RegisterNamespacedNative("voxel", "register", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(map, sd::ArgMap(args, 0, "voxel.register"));
		(void)map;
		ContentRegistry::LoadContext ctx{m_currentMod, String(), String::Format("script de %s", m_currentMod.CStr())};
		// Le chemin du mod sert aux fichiers relatifs (textures, modèles).
		for (const ModInfo &mod : m_game.Mods().mods)
			if (mod.id == m_currentMod)
				ctx.baseDir = mod.path;
		Result<String, String> id = m_game.Content().Load(script::NodeFromValue(args[0]), ctx);
		if (id.IsError())
			return Err(Error("voxel.register", id.Error()));
		return Ok(Value::Str(id.Unwrap()));
	});
	vm.RegisterNamespacedNative("voxel", "on", 2, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(event, sd::ArgString(args, 0, "voxel.on"));
		if (!args[1].IsCallable())
			return Err(Error("voxel.on", String("fonction attendue")));
		m_handlers[event.CStr()].push_back(Owned{args[1], m_currentMod});
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("voxel", "command", 4, 4, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "voxel.command"));
		TRY(usage, sd::ArgString(args, 1, "voxel.command"));
		TRY(help, sd::ArgString(args, 2, "voxel.command"));
		if (!args[3].IsCallable())
			return Err(Error("voxel.command", String("fonction attendue")));
		const Owned fn{args[3], m_currentMod};
		m_game.RegisterCommand(name, usage, help, [this, fn](Game &, ecs::Entity sender, const std::vector<String> &argv) -> Result<String, String> {
			auto list = std::make_shared<script::ListObject>();
			for (const String &a : argv)
				list->Push(Value::Str(a));
			Result<Value, SE> r = Call(fn, {EntityValue(sender), Value::List(list)});
			if (r.IsError())
				return Err(r.Error().Format());
			return Ok(r.Value().IsNil() ? String() : r.Value().ToDisplayString());
		});
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("voxel", "behavior", 2, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(id, sd::ArgString(args, 0, "voxel.behavior"));
		if (!args[1].IsCallable())
			return Err(Error("voxel.behavior", String("fonction attendue")));
		const String qualified = Qualified(id, m_currentMod);
		m_behaviors[qualified.CStr()] = Owned{args[1], m_currentMod};
		// Déclaré aussi au registre : les définitions qui l'utilisent sont valides.
		if (!m_game.Content().FindBehavior(qualified)) {
			NodePtr def = dataio::Node::MakeObject();
			def->Set("type", json::MakeStr("behavior"));
			def->Set("id", json::MakeStr(qualified));
			def->Set("script", json::MakeStr(qualified));
			(void)m_game.Content().Load(def, ContentRegistry::LoadContext{m_currentMod, String(), String("voxel.behavior")});
		}
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("voxel", "generator", 2, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(id, sd::ArgString(args, 0, "voxel.generator"));
		if (!args[1].IsCallable())
			return Err(Error("voxel.generator", String("fonction attendue")));
		const String qualified = Qualified(id, m_currentMod);
		m_generators[qualified.CStr()] = Owned{args[1], m_currentMod};
		// Déclare aussi le GeneratorDef correspondant s'il n'existe pas.
		if (!m_game.Content().FindGenerator(qualified)) {
			NodePtr def = dataio::Node::MakeObject();
			def->Set("type", json::MakeStr("generator"));
			def->Set("id", json::MakeStr(qualified));
			def->Set("kind", json::MakeStr("script"));
			(void)m_game.Content().Load(def, ContentRegistry::LoadContext{m_currentMod, String(), String("voxel.generator")});
		}
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("voxel", "system", 2, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "voxel.system"));
		if (!args[1].IsCallable())
			return Err(Error("voxel.system", String("fonction attendue")));
		m_systems.push_back({name, Owned{args[1], m_currentMod}});
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("voxel", "mod", 0, 0, [this](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		return Ok(Value::Str(m_currentMod));
	});

	// Générateur scripté : écriture dans le chunk en cours.
	vm.RegisterNamespacedNative("gen", "set", 4, 4, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		if (!m_genChunk)
			return Err(Error("gen.set", String("seulement pendant la génération d'un chunk")));
		TRY(p, ArgPos(args, 0, "gen.set"));
		TRY(id, sd::ArgString(args, 3, "gen.set"));
		const BlockId block = m_game.Content().BlockIdOf(Qualified(id, m_currentMod));
		const BlockPos o = m_genChunk->Pos().Origin();
		const BlockPos l = p - o;
		if (l.x >= 0 && l.y >= 0 && l.z >= 0 && l.x < CHUNK_SIZE && l.y < CHUNK_SIZE && l.z < CHUNK_SIZE)
			m_genChunk->Set(l.x, l.y, l.z, block);
		else if (m_genWorld)
			m_genWorld->GenerationWrite(p, block);
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("gen", "noise", 2, 4, [](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(x, sd::ArgNumber(args, 0, "gen.noise"));
		TRY(z, sd::ArgNumber(args, 1, "gen.noise"));
		double frequency = 0.02, seed = 1;
		if (args.size() > 2) {
			TRY(f, sd::ArgNumber(args, 2, "gen.noise"));
			frequency = f;
		}
		if (args.size() > 3) {
			TRY(s, sd::ArgNumber(args, 3, "gen.noise"));
			seed = s;
		}
		generators::NoiseSettings settings;
		settings.type = generators::NoiseType::SIMPLEX;
		settings.seed = uint32_t(seed);
		settings.frequency = float(frequency);
		settings.octaves = 4;
		static thread_local std::map<std::pair<uint32_t, float>, generators::Noise> cache;
		auto key = std::make_pair(settings.seed, settings.frequency);
		auto it = cache.find(key);
		if (it == cache.end())
			it = cache.emplace(key, generators::Noise(settings)).first;
		return Ok(Value::Number(it->second.Sample(float(x), float(z))));
	});
	vm.RegisterNamespacedNative("gen", "random", 3, 4, [](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(p, ArgPos(args, 0, "gen.random"));
		uint32_t salt = 0;
		if (args.size() > 3) {
			TRY(s, sd::ArgNumber(args, 3, "gen.random"));
			salt = uint32_t(s);
		}
		return Ok(Value::Number(RandomAt(1, p.x, p.y, p.z, salt)));
	});

	InstallWorldApi();
	InstallEntityApi();
	InstallContentApi();
	InstallStructureApi();
}

void ScriptHost::InstallWorldApi() {
	script::Interpreter &vm = m_vm;
	vm.RegisterNamespacedNative("world", "get", 3, 3, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(p, ArgPos(args, 0, "world.get"));
		if (!m_game.HasWorld())
			return Ok(Value::Nil());
		return Ok(Value::Str(m_game.GetWorld().BlockAt(p).id));
	});
	vm.RegisterNamespacedNative("world", "param", 3, 3, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(p, ArgPos(args, 0, "world.param"));
		return Ok(Value::Int(m_game.HasWorld() ? m_game.GetWorld().GetParam(p) : 0));
	});
	vm.RegisterNamespacedNative("world", "set", 4, 5, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(p, ArgPos(args, 0, "world.set"));
		TRY(id, sd::ArgString(args, 3, "world.set"));
		if (!m_game.HasWorld())
			return Ok(Value::Boolean(false));
		uint8_t param = 0;
		if (args.size() > 4) {
			TRY(pa, sd::ArgNumber(args, 4, "world.set"));
			param = uint8_t(pa);
		}
		const String qualified = Qualified(id, m_currentMod);
		const BlockDef *def = m_game.Content().FindBlock(qualified);
		if (!def)
			return Err(Error("world.set", String::Format("bloc inconnu « %s »", qualified.CStr())));
		return Ok(Value::Boolean(m_game.GetWorld().SetBlock(p, def->numeric, param, ChangeCause::SCRIPT)));
	});
	// Rangements (coffres…) et blocs à bascule (portes).
	vm.RegisterNamespacedNative("world", "container", 3, 3, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(p, ArgPos(args, 0, "world.container"));
		Inventory *inventory = m_game.HasWorld() ? m_game.BlockInventory(p) : nullptr;
		if (!inventory)
			return Ok(Value::Nil());
		auto list = std::make_shared<script::ListObject>();
		for (int i = 0; i < inventory->Size(); ++i) {
			const ItemStack &stack = inventory->Slot(i);
			if (stack.IsEmpty()) {
				list->Push(Value::Nil());
				continue;
			}
			auto map = std::make_shared<script::MapObject>();
			map->SetKey("item", Value::Str(stack.item));
			map->SetKey("count", Value::Int(stack.count));
			list->Push(Value::Map(map));
		}
		return Ok(Value::List(list));
	});
	vm.RegisterNamespacedNative("world", "container_add", 5, 5, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(p, ArgPos(args, 0, "world.container_add"));
		TRY(item, sd::ArgString(args, 3, "world.container_add"));
		TRY(count, sd::ArgNumber(args, 4, "world.container_add"));
		Inventory *inventory = m_game.HasWorld() ? m_game.BlockInventory(p) : nullptr;
		if (!inventory)
			return Err(Error("world.container_add", String("pas de rangement à cette position")));
		const String qualified = Qualified(item, m_currentMod);
		if (!m_game.Content().FindItem(qualified))
			return Err(Error("world.container_add", String::Format("objet inconnu « %s »", qualified.CStr())));
		return Ok(Value::Int(inventory->Add(m_game.Content(), qualified, int(count)))); // reste qui n'a pas trouvé de place
	});
	vm.RegisterNamespacedNative("world", "toggle", 3, 3, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(p, ArgPos(args, 0, "world.toggle"));
		return Ok(Value::Boolean(m_game.HasWorld() && m_game.ToggleBlock(p)));
	});
	// États d'un bloc (« states ») : world.state(x, y, z) → nom ou nil ;
	// world.set_state(x, y, z, nom) → vrai si le bloc a cet état.
	vm.RegisterNamespacedNative("world", "state", 3, 3, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(p, ArgPos(args, 0, "world.state"));
		if (!m_game.HasWorld() || !m_game.GetWorld().IsLoaded(p))
			return Ok(Value::Nil());
		const BlockDef &def = m_game.GetWorld().BlockAt(p);
		return Ok(def.stateName.IsEmpty() ? Value::Nil() : Value::Str(def.stateName));
	});
	vm.RegisterNamespacedNative("world", "set_state", 4, 4, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(p, ArgPos(args, 0, "world.set_state"));
		TRY(state, sd::ArgString(args, 3, "world.set_state"));
		return Ok(Value::Boolean(m_game.HasWorld() && m_game.SetBlockState(p, state)));
	});
	vm.RegisterNamespacedNative("world", "use", 3, 4, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(p, ArgPos(args, 0, "world.use"));
		const ecs::Entity actor = args.size() > 3 ? EntityOf(args[3]) : m_game.LocalPlayer();
		return Ok(Value::Boolean(m_game.HasWorld() && m_game.UseBlock(actor, p)));
	});
	vm.RegisterNamespacedNative("world", "surface", 2, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(x, sd::ArgNumber(args, 0, "world.surface"));
		TRY(z, sd::ArgNumber(args, 1, "world.surface"));
		if (!m_game.HasWorld() || !m_game.GetWorld().Generator())
			return Ok(Value::Nil());
		return Ok(Value::Int(m_game.GetWorld().Generator()->SurfaceHeight(int(std::floor(x)), int(std::floor(z)))));
	});
	vm.RegisterNamespacedNative("world", "biome", 2, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(x, sd::ArgNumber(args, 0, "world.biome"));
		TRY(z, sd::ArgNumber(args, 1, "world.biome"));
		if (!m_game.HasWorld() || !m_game.GetWorld().Generator())
			return Ok(Value::Nil());
		const BiomeDef *b = m_game.GetWorld().Generator()->BiomeAt(int(std::floor(x)), int(std::floor(z)));
		return Ok(b ? Value::Str(b->id) : Value::Nil());
	});
	vm.RegisterNamespacedNative("world", "structure", 4, 5, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(id, sd::ArgString(args, 0, "world.structure"));
		TRY(p, ArgPos(args, 1, "world.structure"));
		int rotation = 0;
		if (args.size() > 4) {
			TRY(r, sd::ArgNumber(args, 4, "world.structure"));
			rotation = int(r);
		}
		const StructureDef *s = m_game.Content().FindStructure(Qualified(id, m_currentMod));
		if (!s || !m_game.HasWorld())
			return Ok(Value::Boolean(false));
		m_game.GetWorld().PlaceStructure(*s, p, rotation, false);
		return Ok(Value::Boolean(true));
	});
	vm.RegisterNamespacedNative("world", "spawn", 4, 4, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(id, sd::ArgString(args, 0, "world.spawn"));
		TRY(p, ArgVec(args, 1, "world.spawn"));
		return Ok(EntityValue(m_game.SpawnEntity(Qualified(id, m_currentMod), p)));
	});
	vm.RegisterNamespacedNative("world", "drop", 5, 5, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(item, sd::ArgString(args, 0, "world.drop"));
		TRY(count, sd::ArgNumber(args, 1, "world.drop"));
		TRY(p, ArgVec(args, 2, "world.drop"));
		return Ok(EntityValue(m_game.DropItem(ItemStack{Qualified(item, m_currentMod), int(count), 0, nullptr}, p)));
	});
	vm.RegisterNamespacedNative("world", "entities", 4, 5, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(center, ArgVec(args, 0, "world.entities"));
		TRY(radius, sd::ArgNumber(args, 3, "world.entities"));
		String kind;
		if (args.size() > 4) {
			TRY(k, sd::ArgString(args, 4, "world.entities"));
			kind = k;
		}
		auto list = std::make_shared<script::ListObject>();
		m_game.Registry().Query<Transform, EntityKind>([&](ecs::Entity e, Transform &t, EntityKind &k) {
			const math::FVector3 d = t.position - center;
			if (d.Dot(d) <= float(radius * radius) && (kind.IsEmpty() || k.id == kind || k.kind == kind))
				list->Push(EntityValue(e));
		});
		return Ok(Value::List(list));
	});
	vm.RegisterNamespacedNative("world", "raycast", 7, 7, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(o, ArgVec(args, 0, "world.raycast"));
		TRY(d, ArgVec(args, 3, "world.raycast"));
		TRY(dist, sd::ArgNumber(args, 6, "world.raycast"));
		if (!m_game.HasWorld())
			return Ok(Value::Nil());
		Option<BlockHit> hit = RaycastBlocks(m_game.GetWorld(), o, d, float(dist));
		if (hit.IsNone())
			return Ok(Value::Nil());
		Value result = PosValue(hit.Value().block);
		result.AsMap()->SetKey("block", Value::Str(m_game.GetWorld().BlockAt(hit.Value().block).id));
		result.AsMap()->SetKey("distance", Value::Number(hit.Value().distance));
		return Ok(result);
	});
	// Sol où se tenir dans la colonne (chunks chargés seulement) : nil s'il n'y en a pas.
	vm.RegisterNamespacedNative("world", "ground", 4, 4, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(x, sd::ArgNumber(args, 0, "world.ground"));
		TRY(z, sd::ArgNumber(args, 1, "world.ground"));
		TRY(top, sd::ArgNumber(args, 2, "world.ground"));
		TRY(bottom, sd::ArgNumber(args, 3, "world.ground"));
		Option<int> y = m_game.LoadedGround(int(std::floor(x)), int(std::floor(z)), int(std::floor(top)), int(std::floor(bottom)));
		return Ok(y.IsSome() ? Value::Int(y.Unwrap()) : Value::Nil());
	});
	// ── Dimensions ──────────────────────────────────────────────────────────
	vm.RegisterNamespacedNative("world", "dimension", 0, 0, [this](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		return Ok(Value::Str(m_game.CurrentDimension()));
	});
	vm.RegisterNamespacedNative("world", "default_dimension", 0, 0, [this](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		return Ok(Value::Str(m_game.Content().DefaultDimension()));
	});
	vm.RegisterNamespacedNative("world", "dimensions", 0, 0, [this](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		auto list = std::make_shared<script::ListObject>();
		for (const DimensionDef *d : m_game.Content().Dimensions())
			list->Push(Value::Str(d->id));
		return Ok(Value::List(list));
	});
	// Temps passé dans la dimension courante depuis l'arrivée (s).
	vm.RegisterNamespacedNative("world", "time_here", 0, 0, [this](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		return Ok(Value::Number(m_game.TimeInDimension()));
	});
	// Données persistantes de la dimension courante (ou de `dimension`).
	vm.RegisterNamespacedNative("world", "data", 1, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(key, sd::ArgString(args, 0, "world.data"));
		String dimension;
		if (args.size() > 1) {
			TRY(d, sd::ArgString(args, 1, "world.data"));
			dimension = Qualified(d, m_currentMod);
		}
		if (!m_game.HasWorld())
			return Ok(Value::Nil());
		NodePtr v = json::At(json::At(m_game.DimensionData(dimension), "state"), key.CStr());
		return Ok(v ? script::ValueFromNode(v) : Value::Nil());
	});
	vm.RegisterNamespacedNative("world", "set_data", 2, 3, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(key, sd::ArgString(args, 0, "world.set_data"));
		String dimension;
		if (args.size() > 2) {
			TRY(d, sd::ArgString(args, 2, "world.set_data"));
			dimension = Qualified(d, m_currentMod);
		}
		if (!m_game.HasWorld())
			return Ok(Value::Nil());
		NodePtr data = m_game.DimensionData(dimension);
		NodePtr state = json::At(data, "state");
		if (!state || !state->IsObject()) {
			state = dataio::Node::MakeObject();
			data->Set("state", state);
		}
		state->Set(key, script::NodeFromValue(args[1]));
		return Ok(Value::Nil());
	});
}

void ScriptHost::InstallEntityApi() {
	script::Interpreter &vm = m_vm;
	auto entityArg = [this](std::vector<Value> &args, const char *fn) -> Result<ecs::Entity, SE> {
		const ecs::Entity e = EntityOf(args[0]);
		if (!e.Valid())
			return Err(Error(fn, String("entité inexistante")));
		return Ok(e);
	};
	ecs::ArchetypeRegistry &reg = m_game.Registry();

	vm.RegisterNamespacedNative("entity", "exists", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		return Ok(Value::Boolean(EntityOf(args[0]).Valid()));
	});
	vm.RegisterNamespacedNative("entity", "kind", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.kind"));
		auto k = reg.GetComponent<EntityKind>(e);
		return Ok(k.IsSome() ? Value::Str(k.Unwrap()->id) : Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "pos", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.pos"));
		auto t = reg.GetComponent<Transform>(e);
		return Ok(t.IsSome() ? Vec(t.Unwrap()->position) : Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "set_pos", 4, 4, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.set_pos"));
		TRY(p, ArgVec(args, 1, "entity.set_pos"));
		if (auto t = reg.GetComponent<Transform>(e); t.IsSome())
			t.Unwrap()->position = t.Unwrap()->previous = p;
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "forward", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.forward"));
		auto t = reg.GetComponent<Transform>(e);
		return Ok(t.IsSome() ? Vec(t.Unwrap()->Forward()) : Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "velocity", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.velocity"));
		auto v = reg.GetComponent<Velocity>(e);
		return Ok(v.IsSome() ? Vec(v.Unwrap()->linear) : Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "set_velocity", 4, 4, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.set_velocity"));
		TRY(v, ArgVec(args, 1, "entity.set_velocity"));
		if (auto vel = reg.GetComponent<Velocity>(e); vel.IsSome())
			vel.Unwrap()->linear = v;
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "impulse", 4, 4, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.impulse"));
		TRY(v, ArgVec(args, 1, "entity.impulse"));
		if (auto vel = reg.GetComponent<Velocity>(e); vel.IsSome())
			vel.Unwrap()->linear = vel.Unwrap()->linear + v;
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "move", 3, 4, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.move"));
		TRY(dx, sd::ArgNumber(args, 1, "entity.move"));
		TRY(dz, sd::ArgNumber(args, 2, "entity.move"));
		if (auto intent = reg.GetComponent<MoveIntent>(e); intent.IsSome()) {
			intent.Unwrap()->wish = {float(dx), 0.f, float(dz)};
			if (args.size() > 3) {
				TRY(speed, sd::ArgNumber(args, 3, "entity.move"));
				intent.Unwrap()->speed = float(speed);
			}
			if (std::fabs(dx) + std::fabs(dz) > 1e-4)
				if (auto t = reg.GetComponent<Transform>(e); t.IsSome())
					t.Unwrap()->yaw = std::atan2(float(dx), float(dz));
		}
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "jump", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.jump"));
		if (auto intent = reg.GetComponent<MoveIntent>(e); intent.IsSome())
			intent.Unwrap()->jump = true;
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "look", 2, 2, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.look"));
		TRY(yaw, sd::ArgNumber(args, 1, "entity.look"));
		if (auto t = reg.GetComponent<Transform>(e); t.IsSome())
			t.Unwrap()->yaw = float(yaw);
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "health", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.health"));
		auto h = reg.GetComponent<Health>(e);
		return Ok(h.IsSome() ? Value::Number(h.Unwrap()->current) : Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "damage", 2, 3, [this, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.damage"));
		TRY(amount, sd::ArgNumber(args, 1, "entity.damage"));
		const ecs::Entity source = args.size() > 2 ? EntityOf(args[2]) : ecs::Entity{};
		m_game.Damage(e, float(amount), source);
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "heal", 2, 2, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.heal"));
		TRY(amount, sd::ArgNumber(args, 1, "entity.heal"));
		if (auto h = reg.GetComponent<Health>(e); h.IsSome())
			h.Unwrap()->current = std::min(h.Unwrap()->max, h.Unwrap()->current + float(amount));
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "remove", 1, 1, [&reg, this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		const ecs::Entity e = EntityOf(args[0]);
		if (e.Valid() && e != m_game.LocalPlayer())
			(void)ecs::DespawnRecursive(reg, e);
		return Ok(Value::Nil());
	});
	// Apparence, survie et effets.
	vm.RegisterNamespacedNative("entity", "skin", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.skin"));
		auto v = reg.GetComponent<Visual>(e);
		return Ok(v.IsSome() ? Value::Str(v.Unwrap()->skin) : Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "set_skin", 2, 3, [this, &reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.set_skin"));
		TRY(skin, sd::ArgString(args, 1, "entity.set_skin"));
		auto v = reg.GetComponent<Visual>(e);
		if (v.IsNone())
			return Ok(Value::Boolean(false));
		v.Unwrap()->skin = Qualified(skin, m_currentMod);
		if (args.size() > 2) { // modèle articulé facultatif
			TRY(model, sd::ArgString(args, 2, "entity.set_skin"));
			v.Unwrap()->rig.model = Qualified(model, m_currentMod);
		}
		return Ok(Value::Boolean(true));
	});
	vm.RegisterNamespacedNative("entity", "tags", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.tags"));
		Value list = Value::EmptyList();
		if (auto kind = reg.GetComponent<EntityKind>(e); kind.IsSome() && kind.Unwrap()->def)
			for (const String &tag : kind.Unwrap()->def->tags)
				list.AsList()->items.push_back(Value::Str(tag));
		return Ok(list);
	});
	vm.RegisterNamespacedNative("entity", "effect", 3, 4, [this, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.effect"));
		TRY(id, sd::ArgString(args, 1, "entity.effect"));
		TRY(seconds, sd::ArgNumber(args, 2, "entity.effect"));
		int level = 1;
		if (args.size() > 3) {
			TRY(l, sd::ArgNumber(args, 3, "entity.effect"));
			level = int(l);
		}
		m_game.AddEffect(e, id, float(seconds), level);
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "add_xp", 2, 2, [this, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.add_xp"));
		TRY(amount, sd::ArgNumber(args, 1, "entity.add_xp"));
		m_game.AddXp(e, int(amount));
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "hunger", 1, 2, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.hunger"));
		auto sv = reg.GetComponent<Survival>(e);
		if (sv.IsNone())
			return Ok(Value::Nil());
		if (args.size() > 1) {
			TRY(h, sd::ArgNumber(args, 1, "entity.hunger"));
			sv.Unwrap()->hunger = std::clamp(float(h), 0.f, 20.f);
		}
		return Ok(Value::Number(sv.Unwrap()->hunger));
	});
	vm.RegisterNamespacedNative("entity", "prop", 2, 2, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.prop"));
		TRY(key, sd::ArgString(args, 1, "entity.prop"));
		auto props = reg.GetComponent<Properties>(e);
		if (props.IsNone() || !props.Unwrap()->values)
			return Ok(Value::Nil());
		// Propriétés d'état (« state.x ») d'abord, puis celles de la définition.
		if (NodePtr v = json::At(json::At(props.Unwrap()->values, "state"), key.CStr()))
			return Ok(script::ValueFromNode(v));
		NodePtr v = json::At(props.Unwrap()->values, key.CStr());
		return Ok(v ? script::ValueFromNode(v) : Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "set_prop", 3, 3, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.set_prop"));
		TRY(key, sd::ArgString(args, 1, "entity.set_prop"));
		auto props = reg.GetComponent<Properties>(e);
		if (props.IsNone()) {
			reg.AddComponent(e, Properties{dataio::Node::MakeObject()});
			props = reg.GetComponent<Properties>(e);
		}
		if (!props.Unwrap()->values)
			props.Unwrap()->values = dataio::Node::MakeObject();
		NodePtr state = json::At(props.Unwrap()->values, "state");
		if (!state) {
			state = dataio::Node::MakeObject();
			props.Unwrap()->values->Set("state", state);
		}
		state->Set(key, script::NodeFromValue(args[2]));
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "give", 3, 3, [this, &reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.give"));
		TRY(item, sd::ArgString(args, 1, "entity.give"));
		TRY(count, sd::ArgNumber(args, 2, "entity.give"));
		auto inv = reg.GetComponent<InventoryComponent>(e);
		if (inv.IsNone())
			return Ok(Value::Int(int64_t(count)));
		return Ok(Value::Int(inv.Unwrap()->inventory.Add(m_game.Content(), Qualified(item, m_currentMod), int(count))));
	});
	vm.RegisterNamespacedNative("entity", "take", 3, 3, [this, &reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.take"));
		TRY(item, sd::ArgString(args, 1, "entity.take"));
		TRY(count, sd::ArgNumber(args, 2, "entity.take"));
		auto inv = reg.GetComponent<InventoryComponent>(e);
		return Ok(Value::Boolean(inv.IsSome() && inv.Unwrap()->inventory.Remove(Qualified(item, m_currentMod), int(count))));
	});
	vm.RegisterNamespacedNative("entity", "count", 2, 2, [this, &reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.count"));
		TRY(item, sd::ArgString(args, 1, "entity.count"));
		auto inv = reg.GetComponent<InventoryComponent>(e);
		return Ok(Value::Int(inv.IsSome() ? inv.Unwrap()->inventory.Count(Qualified(item, m_currentMod)) : 0));
	});
	vm.RegisterNamespacedNative("entity", "held", 1, 1, [this, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.held"));
		const ItemStack *held = m_game.HeldItem(e);
		return Ok(held && !held->IsEmpty() ? Value::Str(held->item) : Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "nearest_player", 2, 2, [this, &reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.nearest_player"));
		TRY(range, sd::ArgNumber(args, 1, "entity.nearest_player"));
		auto t = reg.GetComponent<Transform>(e);
		if (t.IsNone())
			return Ok(Value::Nil());
		Option<ecs::Entity> p = NearestPlayer(reg, t.Unwrap()->position, float(range));
		return Ok(p.IsSome() ? EntityValue(p.Unwrap()) : Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "players", 0, 0, [this, &reg](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		auto list = std::make_shared<script::ListObject>();
		reg.Query<Player>([&](ecs::Entity e, Player &) { list->Push(EntityValue(e)); });
		return Ok(Value::List(list));
	});
	// Voyage entre dimensions : entity.travel(joueur, "core:inverted" [, x, y, z]).
	vm.RegisterNamespacedNative("entity", "travel", 2, 5, [this, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.travel"));
		TRY(id, sd::ArgString(args, 1, "entity.travel"));
		Option<math::FVector3> at = NONE;
		if (args.size() >= 5) {
			TRY(p, ArgVec(args, 2, "entity.travel"));
			at = Some(p);
		}
		Result<bool, String> r = m_game.TravelTo(e, id.IsEmpty() ? id : Qualified(id, m_currentMod), at);
		if (r.IsError())
			return Err(Error("entity.travel", r.Error()));
		return Ok(Value::Boolean(true));
	});
	vm.RegisterNamespacedNative("entity", "dimension", 1, 1, [this, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.dimension"));
		(void)e; // un seul monde actif : celui du joueur local
		return Ok(Value::Str(m_game.CurrentDimension()));
	});
	// Gravité : 1 vers le bas, -1 vers le haut (marcher au plafond).
	vm.RegisterNamespacedNative("entity", "gravity", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.gravity"));
		auto body = reg.GetComponent<PhysicsBody>(e);
		return Ok(Value::Int(body.IsSome() && body.Unwrap()->gravityDirection < 0.f ? -1 : 1));
	});
	vm.RegisterNamespacedNative("entity", "set_gravity", 2, 2, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.set_gravity"));
		TRY(dir, sd::ArgNumber(args, 1, "entity.set_gravity"));
		if (auto body = reg.GetComponent<PhysicsBody>(e); body.IsSome()) {
			const float before = body.Unwrap()->gravityDirection;
			body.Unwrap()->gravityDirection = dir < 0.0 ? -1.f : 1.f;
			if (before != body.Unwrap()->gravityDirection)
				body.Unwrap()->onGround = false;
		}
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "armor", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.armor"));
		Value list = Value::EmptyList();
		if (auto eq = reg.GetComponent<Equipment>(e); eq.IsSome())
			for (const ItemStack &piece : eq.Unwrap()->armor)
				if (!piece.IsEmpty())
					list.AsList()->items.push_back(Value::Str(piece.item));
		return Ok(list);
	});
	vm.RegisterNamespacedNative("entity", "sneaking", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.sneaking"));
		auto intent = reg.GetComponent<MoveIntent>(e);
		return Ok(Value::Boolean(intent.IsSome() && intent.Unwrap()->sneak));
	});
	vm.RegisterNamespacedNative("entity", "on_ground", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.on_ground"));
		auto body = reg.GetComponent<PhysicsBody>(e);
		return Ok(Value::Boolean(body.IsSome() && body.Unwrap()->onGround));
	});
	// État physique : {on_ground, hit_ceiling, hit_wall, in_fluid, sinking, trapped, jumping, sneaking, gravity, fall}.
	vm.RegisterNamespacedNative("entity", "body", 1, 1, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.body"));
		auto map = std::make_shared<script::MapObject>();
		if (auto body = reg.GetComponent<PhysicsBody>(e); body.IsSome()) {
			const PhysicsBody &b = *body.Unwrap();
			map->SetKey("on_ground", Value::Boolean(b.onGround));
			map->SetKey("hit_ceiling", Value::Boolean(b.hitCeiling));
			map->SetKey("hit_wall", Value::Boolean(b.hitWall));
			map->SetKey("in_fluid", Value::Boolean(b.inFluid));
			map->SetKey("sinking", Value::Boolean(b.inSinking));
			map->SetKey("trapped", Value::Boolean(b.trapped));
			map->SetKey("flies", Value::Boolean(b.flies));
			map->SetKey("gravity", Value::Int(b.gravityDirection < 0.f ? -1 : 1));
			map->SetKey("fall", Value::Number(b.fallDistance));
		}
		if (auto intent = reg.GetComponent<MoveIntent>(e); intent.IsSome()) {
			map->SetKey("jumping", Value::Boolean(intent.Unwrap()->jump));
			map->SetKey("sneaking", Value::Boolean(intent.Unwrap()->sneak));
			map->SetKey("moving", Value::Boolean(intent.Unwrap()->wish.Length() > 0.1f));
		}
		return Ok(Value::Map(map));
	});
	vm.RegisterNamespacedNative("entity", "has_effect", 2, 2, [&reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.has_effect"));
		TRY(id, sd::ArgString(args, 1, "entity.has_effect"));
		auto fx = reg.GetComponent<StatusEffects>(e);
		return Ok(Value::Int(fx.IsSome() ? fx.Unwrap()->Level(id) : 0));
	});
	vm.RegisterNamespacedNative("entity", "sound", 2, 2, [this, &reg, entityArg](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(e, entityArg(args, "entity.sound"));
		TRY(event, sd::ArgString(args, 1, "entity.sound"));
		auto emitter = reg.GetComponent<SoundEmitter>(e);
		auto t = reg.GetComponent<Transform>(e);
		if (emitter.IsSome() && t.IsSome())
			if (const String sound = emitter.Unwrap()->sounds.Get(event.CStr()); !sound.IsEmpty())
				m_game.PlaySound(sound, t.Unwrap()->position);
		return Ok(Value::Nil());
	});
}

void ScriptHost::InstallContentApi() {
	script::Interpreter &vm = m_vm;
	vm.RegisterNamespacedNative("content", "get", 2, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(kind, sd::ArgString(args, 0, "content.get"));
		TRY(rawId, sd::ArgString(args, 1, "content.get"));
		const String id = Qualified(rawId, m_currentMod);
		const ContentRegistry &c = m_game.Content();
		NodePtr raw;
		if (kind == "block") {
			if (const BlockDef *d = c.FindBlock(id))
				raw = d->raw;
		} else if (kind == "item" || kind == "tool") {
			if (const ItemDef *d = c.FindItem(id))
				raw = d->raw;
		} else if (kind == "entity" || kind == "creature") {
			if (const EntityDef *d = c.FindEntity(id))
				raw = d->raw;
		} else if (kind == "recipe") {
			if (const RecipeDef *d = c.FindRecipe(id))
				raw = d->raw;
		} else if (kind == "biome") {
			if (const BiomeDef *d = c.FindBiome(id))
				raw = d->raw;
		} else if (kind == "structure") {
			if (const StructureDef *d = c.FindStructure(id))
				raw = d->raw;
		}
		return Ok(raw ? script::ValueFromNode(raw) : Value::Nil());
	});
	vm.RegisterNamespacedNative("content", "list", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(kind, sd::ArgString(args, 0, "content.list"));
		auto list = std::make_shared<script::ListObject>();
		const ContentRegistry &c = m_game.Content();
		if (kind == "block")
			for (const BlockDef &b : c.Blocks())
				list->Push(Value::Str(b.id));
		else if (kind == "item")
			for (const ItemDef *i : c.Items())
				list->Push(Value::Str(i->id));
		else if (kind == "entity" || kind == "creature")
			for (const EntityDef *e : c.Entities())
				list->Push(Value::Str(e->id));
		else if (kind == "recipe")
			for (const RecipeDef *r : c.Recipes())
				list->Push(Value::Str(r->id));
		else if (kind == "biome")
			for (const BiomeDef *b : c.Biomes())
				list->Push(Value::Str(b->id));
		return Ok(Value::List(list));
	});
	vm.RegisterNamespacedNative("content", "config", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(path, sd::ArgString(args, 0, "content.config"));
		NodePtr v = json::At(m_game.Content().Config(), path.CStr());
		return Ok(v ? script::ValueFromNode(v) : Value::Nil());
	});

	vm.RegisterNamespacedNative("game", "log", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		m_game.LogLine(String::Format("[%s] %s", m_currentMod.CStr(), args[0].ToDisplayString().CStr()));
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("game", "say", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		m_game.Message(args[0].ToDisplayString(), String("chat"));
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("game", "time", 0, 0, [this](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		return Ok(Value::Number(m_game.DayFraction()));
	});
	vm.RegisterNamespacedNative("game", "set_time", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(t, sd::ArgNumber(args, 0, "game.set_time"));
		m_game.SetGameTime(t * double(m_game.Options().dayLength));
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("game", "sound", 4, 4, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(id, sd::ArgString(args, 0, "game.sound"));
		TRY(p, ArgVec(args, 1, "game.sound"));
		m_game.PlaySound(Qualified(id, m_currentMod), p);
		return Ok(Value::Nil());
	});
	// Son d'ambiance ou musique (non spatialisé) : game.ambient(id [, volume]
	// [, options]) ; options {volume, channel, fade_in, fade_out, loop} — sur
	// un canal, un seul son à la fois : l'ancien s'éteint en fondu, le nouveau
	// monte en fondu, le même son déjà en cours continue.
	vm.RegisterNamespacedNative("game", "ambient", 1, 3, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(id, sd::ArgString(args, 0, "game.ambient"));
		float volume = 1.f;
		Game::AmbientOptions options;
		for (size_t i = 1; i < args.size(); ++i) {
			if (args[i].IsNumber()) {
				volume = float(args[i].AsNumber());
			} else if (args[i].IsMap() && args[i].AsMap()) {
				for (const auto &[key, value] : args[i].AsMap()->Snapshot()) {
					const bool number = value.IsNumber();
					if (key == "volume" && number)
						volume = float(value.AsNumber());
					else if (key == "channel" && value.IsString())
						options.channel = value.AsString();
					else if (key == "fade_in" && number)
						options.fadeIn = float(value.AsNumber());
					else if (key == "fade_out" && number)
						options.fadeOut = float(value.AsNumber());
					else if (key == "loop")
						options.loop = value.IsTruthy();
					else
						return Err(script::Interpreter::MakeError(
							String::Format("`game.ambient` : option `%s` inconnue ou mal typée", key.CStr())));
				}
			} else if (!args[i].IsNil()) {
				return Err(script::Interpreter::MakeError(String("`game.ambient(id [, volume] [, options])` : volume ou table attendus")));
			}
		}
		m_game.PlayAmbient(Qualified(id, m_currentMod), volume, options);
		return Ok(Value::Nil());
	});
	// game.stop_ambient(canal [, fondu_de_sortie]).
	vm.RegisterNamespacedNative("game", "stop_ambient", 1, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(channel, sd::ArgString(args, 0, "game.stop_ambient"));
		float fadeOut = 0.f;
		if (args.size() > 1) {
			TRY(f, sd::ArgNumber(args, 1, "game.stop_ambient"));
			fadeOut = float(f);
		}
		m_game.StopAmbient(channel, fadeOut);
		return Ok(Value::Nil());
	});
	// Réglage d'un mod (mod.json « settings » : valeur par défaut, surchargée
	// par l'utilisateur) : voxel.setting("nom") dans les scripts du mod.
	vm.RegisterNamespacedNative("voxel", "setting", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "voxel.setting"));
		if (NodePtr v = json::At(m_game.Options().modSettings, (m_currentMod + String(".") + name).CStr()))
			return Ok(script::ValueFromNode(v));
		for (const ModInfo &mod : m_game.Mods().mods)
			if (mod.id == m_currentMod)
				if (NodePtr list = json::At(mod.manifest, "settings"); list && list->IsArray())
					for (size_t i = 0; i < list->GetSize(); ++i)
						if (json::Str(list->At(i), "id") == name)
							if (NodePtr def = json::At(list->At(i), "default"))
								return Ok(script::ValueFromNode(def));
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("game", "rule", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "game.rule"));
		return Ok(Value::Boolean(m_game.Rule(name.CStr())));
	});
	vm.RegisterNamespacedNative("game", "mode", 0, 0, [this](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		return Ok(Value::Str(String(GameModeId(m_game.Mode()))));
	});
	vm.RegisterNamespacedNative("game", "difficulty", 0, 0, [this](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		return Ok(Value::Str(String(DifficultyId(m_game.GetDifficulty()))));
	});
	// Interface côté application : game.open_ui(joueur, "trade", {title: …, offers: […]}, source)
	vm.RegisterNamespacedNative("game", "open_ui", 3, 4, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		const ecs::Entity player = EntityOf(args[0]);
		TRY(kind, sd::ArgString(args, 1, "game.open_ui"));
		UiRequest request{kind, script::NodeFromValue(args[2]), player, args.size() > 3 ? EntityOf(args[3]) : ecs::Entity{}};
		m_game.RequestUi(std::move(request));
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("game", "seed", 0, 0, [this](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		return Ok(Value::Int(m_game.HasWorld() ? m_game.GetWorld().Settings().seed : 0));
	});
	vm.RegisterNamespacedNative("game", "execute", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(line, sd::ArgString(args, 0, "game.execute"));
		Result<String, String> r = m_game.Execute(line, m_game.LocalPlayer());
		return Ok(Value::Str(r.IsOk() ? r.Value() : r.Error()));
	});
}

} // namespace voxel
