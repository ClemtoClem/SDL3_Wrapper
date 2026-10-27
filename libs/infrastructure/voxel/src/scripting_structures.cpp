// Définitions de voxel/scripting.hpp : API des structures (presse-papiers,
// fichiers, formules), remplissages de zones, surlignages et visée.
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/scripting.hpp>

#include <cmath>

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

SE Error(const char *fn, const String &message) { return SE(String::Format("%s : %s", fn, message.CStr()), 0, 0); }

Result<BlockPos, SE> ArgPos(std::vector<Value> &args, size_t i, const char *fn) {
	TRY(x, sd::ArgNumber(args, i, fn));
	TRY(y, sd::ArgNumber(args, i + 1, fn));
	TRY(z, sd::ArgNumber(args, i + 2, fn));
	return Ok(BlockPos{int(std::floor(x)), int(std::floor(y)), int(std::floor(z))});
}

Value Pos(const BlockPos &p) {
	auto map = std::make_shared<script::MapObject>();
	map->SetKey("x", Value::Int(p.x));
	map->SetKey("y", Value::Int(p.y));
	map->SetKey("z", Value::Int(p.z));
	return Value::Map(map);
}

Value Info(const String &name, const Schematic &s) {
	auto map = std::make_shared<script::MapObject>();
	map->SetKey("name", Value::Str(name));
	map->SetKey("title", Value::Str(s.name));
	map->SetKey("size", Pos(s.Size()));
	map->SetKey("anchor", Pos(s.anchor));
	map->SetKey("volume", Value::Int(s.Volume()));
	map->SetKey("cells", Value::Int(s.Filled()));
	map->SetKey("blocks", Value::Int(s.Solid()));
	map->SetKey("containers", Value::Int(int64_t(s.containers.size())));
	auto counts = std::make_shared<script::MapObject>();
	for (const auto &[id, n] : s.Counts())
		counts->SetKey(id, Value::Int(n));
	map->SetKey("counts", Value::Map(counts));
	return Value::Map(map);
}

/// Résultat d'une opération qui peut échouer pour une raison de l'utilisateur.
Value Failure(const String &message) {
	auto map = std::make_shared<script::MapObject>();
	map->SetKey("ok", Value::Boolean(false));
	map->SetKey("error", Value::Str(message));
	return Value::Map(map);
}

Value Success(Value info) {
	if (info.IsMap())
		info.AsMap()->SetKey("ok", Value::Boolean(true));
	return info;
}

/// Argument booléen facultatif.
bool Flag(const std::vector<Value> &args, size_t i, bool fallback = false) {
	return i < args.size() && !args[i].IsNil() ? args[i].IsTruthy() : fallback;
}

} // namespace

void ScriptHost::InstallStructureApi() {
	script::Interpreter &vm = m_vm;
	// Id de bloc écrit par un joueur : complet, « core:nom », ou « nom » d'un mod chargé.
	auto block = [this](const String &raw) -> String {
		const String id = raw.Trim();
		if (id.IsEmpty() || m_game.Content().FindBlock(id))
			return id;
		if (id == "air")
			return String(AIR_ID);
		if (!id.Contains(":")) {
			if (m_game.Content().FindBlock(String("core:") + id))
				return String("core:") + id;
			const String suffix = String(":") + id;
			for (const BlockDef &b : m_game.Content().Blocks())
				if (b.id.EndsWith(suffix.CStr()))
					return b.id;
		}
		return id;
	};
	auto clipboard = [this](const String &name, const char *fn) -> Result<Schematic *, SE> {
		Schematic *s = m_game.Clipboard(name);
		if (!s)
			return Err(Error(fn, String::Format("presse-papiers « %s » vide", name.CStr())));
		return Ok(s);
	};

	// ── Presse-papiers ──────────────────────────────────────────────────────
	vm.RegisterNamespacedNative("structure", "copy", 7, 10, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.copy"));
		TRY(a, ArgPos(args, 1, "structure.copy"));
		TRY(b, ArgPos(args, 4, "structure.copy"));
		if (!m_game.HasWorld())
			return Ok(Value::Nil());
		Option<BlockPos> anchor = NONE;
		if (args.size() >= 10) {
			TRY(at, ArgPos(args, 7, "structure.copy"));
			anchor = Some(at);
		}
		m_game.SetClipboard(name, m_game.CaptureRegion(a, b, anchor));
		return Ok(Info(name, *m_game.Clipboard(name)));
	});
	vm.RegisterNamespacedNative("structure", "paste", 4, 5, [this, clipboard](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.paste"));
		TRY(at, ArgPos(args, 1, "structure.paste"));
		TRY(s, clipboard(name, "structure.paste"));
		Schematic::PasteOptions options;
		if (args.size() > 4 && args[4].IsMap()) {
			const NodePtr o = script::NodeFromValue(args[4]);
			options.rotation = json::Int(o, "rotation", 0);
			options.keepAir = json::Bool(o, "keep_air", true);
			options.onlyAir = json::Bool(o, "only_air", false);
		}
		return Ok(Value::Int(m_game.HasWorld() ? m_game.PasteSchematic(*s, at, options) : 0));
	});
	vm.RegisterNamespacedNative("structure", "bounds", 4, 5, [clipboard](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.bounds"));
		TRY(at, ArgPos(args, 1, "structure.bounds"));
		TRY(s, clipboard(name, "structure.bounds"));
		int rotation = 0;
		if (args.size() > 4) {
			TRY(r, sd::ArgNumber(args, 4, "structure.bounds"));
			rotation = int(r);
		}
		const auto [lo, hi] = s->Bounds(at, rotation);
		auto map = std::make_shared<script::MapObject>();
		map->SetKey("min", Pos(lo));
		map->SetKey("max", Pos(hi));
		return Ok(Value::Map(map));
	});
	vm.RegisterNamespacedNative("structure", "info", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.info"));
		const Schematic *s = m_game.Clipboard(name);
		return Ok(s ? Info(name, *s) : Value::Nil());
	});
	vm.RegisterNamespacedNative("structure", "exists", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.exists"));
		return Ok(Value::Boolean(m_game.Clipboard(name) != nullptr));
	});
	vm.RegisterNamespacedNative("structure", "forget", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.forget"));
		return Ok(Value::Boolean(m_game.ForgetClipboard(name)));
	});
	vm.RegisterNamespacedNative("structure", "clipboards", 0, 0, [this](script::Interpreter &, std::vector<Value> &) -> NativeResult {
		auto list = std::make_shared<script::ListObject>();
		for (const String &n : m_game.ClipboardNames())
			list->Push(Value::Str(n));
		return Ok(Value::List(list));
	});
	vm.RegisterNamespacedNative("structure", "duplicate", 2, 2, [this, clipboard](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(from, sd::ArgString(args, 0, "structure.duplicate"));
		TRY(to, sd::ArgString(args, 1, "structure.duplicate"));
		TRY(s, clipboard(from, "structure.duplicate"));
		Schematic copy = *s;
		m_game.SetClipboard(to, std::move(copy));
		return Ok(Info(to, *m_game.Clipboard(to)));
	});
	vm.RegisterNamespacedNative("structure", "rotate", 2, 2, [clipboard](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.rotate"));
		TRY(q, sd::ArgNumber(args, 1, "structure.rotate"));
		TRY(s, clipboard(name, "structure.rotate"));
		*s = s->Rotated(int(q));
		return Ok(Info(name, *s));
	});
	vm.RegisterNamespacedNative("structure", "mirror", 2, 2, [clipboard](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.mirror"));
		TRY(axis, sd::ArgString(args, 1, "structure.mirror"));
		TRY(s, clipboard(name, "structure.mirror"));
		if (axis != "x" && axis != "y" && axis != "z")
			return Err(Error("structure.mirror", String("axe « x », « y » ou « z » attendu")));
		*s = s->Mirrored(axis == "x" ? 0 : axis == "y" ? 1 : 2);
		return Ok(Info(name, *s));
	});
	vm.RegisterNamespacedNative("structure", "hollow", 1, 2, [clipboard](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.hollow"));
		TRY(s, clipboard(name, "structure.hollow"));
		int flat = -1; // forme plate : « x », « y » ou « z » (axe ignoré)
		if (args.size() > 1 && args[1].IsString())
			flat = args[1].AsString() == "x" ? 0 : args[1].AsString() == "y" ? 1 : args[1].AsString() == "z" ? 2 : -1;
		*s = s->Hollow(flat);
		return Ok(Info(name, *s));
	});
	vm.RegisterNamespacedNative("structure", "replace", 3, 3, [clipboard, block](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.replace"));
		TRY(from, sd::ArgString(args, 1, "structure.replace"));
		TRY(to, sd::ArgString(args, 2, "structure.replace"));
		TRY(s, clipboard(name, "structure.replace"));
		return Ok(Value::Int(s->Replace(block(from), block(to))));
	});
	vm.RegisterNamespacedNative("structure", "set_anchor", 4, 4, [clipboard](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.set_anchor"));
		TRY(p, ArgPos(args, 1, "structure.set_anchor"));
		TRY(s, clipboard(name, "structure.set_anchor"));
		s->anchor = p;
		return Ok(Info(name, *s));
	});
	vm.RegisterNamespacedNative("structure", "box", 5, 5, [this, block](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.box"));
		TRY(size, ArgPos(args, 1, "structure.box"));
		TRY(id, sd::ArgString(args, 4, "structure.box"));
		const String b = block(id);
		if (!m_game.Content().FindBlock(b))
			return Ok(Failure(String::Format("bloc inconnu « %s »", id.CStr())));
		if (size.x <= 0 || size.y <= 0 || size.z <= 0 || int64_t(size.x) * size.y * size.z > 16ll * 1024 * 1024)
			return Ok(Failure(String("taille invalide")));
		Schematic s(size);
		for (int y = 0; y < size.y; ++y)
			for (int z = 0; z < size.z; ++z)
				for (int x = 0; x < size.x; ++x)
					s.Set({x, y, z}, b);
		m_game.SetClipboard(name, std::move(s));
		return Ok(Success(Info(name, *m_game.Clipboard(name))));
	});
	vm.RegisterNamespacedNative("structure", "from_content", 2, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.from_content"));
		TRY(id, sd::ArgString(args, 1, "structure.from_content"));
		const StructureDef *def = m_game.Content().FindStructure(m_game.ResolveShortId(id));
		if (!def)
			return Ok(Failure(String::Format("structure inconnue « %s »", id.CStr())));
		Schematic s(def->size);
		for (size_t i = 0; i < def->blocks.size(); ++i)
			s.Set(def->blocks[i].first, def->blocks[i].second, i < def->params.size() ? def->params[i] : 0);
		s.anchor = def->anchor;
		s.name = def->id;
		m_game.SetClipboard(name, std::move(s));
		return Ok(Success(Info(name, *m_game.Clipboard(name))));
	});

	// ── Fichiers (monde ou échange) ─────────────────────────────────────────
	vm.RegisterNamespacedNative("structure", "save", 2, 3, [this, clipboard](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.save"));
		TRY(file, sd::ArgString(args, 1, "structure.save"));
		TRY(s, clipboard(name, "structure.save"));
		Result<String, String> path = m_game.SaveStructureFile(*s, file, Flag(args, 2));
		if (path.IsError())
			return Ok(Failure(path.Error()));
		auto map = std::make_shared<script::MapObject>();
		map->SetKey("ok", Value::Boolean(true));
		map->SetKey("path", Value::Str(path.Value()));
		return Ok(Value::Map(map));
	});
	vm.RegisterNamespacedNative("structure", "load", 2, 3, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(file, sd::ArgString(args, 0, "structure.load"));
		TRY(name, sd::ArgString(args, 1, "structure.load"));
		std::vector<String> unknown;
		Result<Schematic, String> s = m_game.LoadStructureFile(file, Flag(args, 2), &unknown);
		if (s.IsError())
			return Ok(Failure(s.Error()));
		m_game.SetClipboard(name, std::move(s.Value()));
		Value info = Success(Info(name, *m_game.Clipboard(name)));
		auto list = std::make_shared<script::ListObject>();
		for (const String &u : unknown)
			list->Push(Value::Str(u));
		info.AsMap()->SetKey("unknown", Value::List(list));
		return Ok(info);
	});
	vm.RegisterNamespacedNative("structure", "list", 0, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		auto list = std::make_shared<script::ListObject>();
		for (const String &f : m_game.StructureFiles(Flag(args, 0)))
			list->Push(Value::Str(f));
		return Ok(Value::List(list));
	});
	vm.RegisterNamespacedNative("structure", "delete", 1, 2, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(file, sd::ArgString(args, 0, "structure.delete"));
		Result<bool, String> r = m_game.DeleteStructureFile(file, Flag(args, 1));
		return Ok(r.IsError() ? Failure(r.Error()) : Success(Value::EmptyMap()));
	});
	vm.RegisterNamespacedNative("structure", "directory", 0, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		return Ok(Value::Str(m_game.StructuresDirectory(Flag(args, 0))));
	});

	// ── Formules ────────────────────────────────────────────────────────────
	vm.RegisterNamespacedNative("structure", "plot", 2, 2, [this, block](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(name, sd::ArgString(args, 0, "structure.plot"));
		if (!args[1].IsMap())
			return Err(Error("structure.plot", String("table de paramètres attendue")));
		const NodePtr o = script::NodeFromValue(args[1]);
		PlotSpec spec;
		const String kind = json::Str(o, "kind", "implicit");
		if (kind == "implicit")
			spec.kind = PlotSpec::Kind::IMPLICIT;
		else if (kind == "curve")
			spec.kind = PlotSpec::Kind::CURVE;
		else if (kind == "surface")
			spec.kind = PlotSpec::Kind::SURFACE;
		else if (kind == "solid")
			spec.kind = PlotSpec::Kind::SOLID;
		else
			return Ok(Failure(String::Format("genre inconnu « %s » (implicit, curve, surface, solid)", kind.CStr())));
		spec.expression = json::Str(o, "expression");
		spec.fx = json::Str(o, "x");
		spec.fy = json::Str(o, "y");
		spec.fz = json::Str(o, "z");
		if (NodePtr box = json::At(o, "box"); box && box->IsArray() && box->GetSize() >= 6) {
			spec.min = {json::Int(box->At(0), "", 0), json::Int(box->At(1), "", 0), json::Int(box->At(2), "", 0)};
			spec.max = {json::Int(box->At(3), "", 0), json::Int(box->At(4), "", 0), json::Int(box->At(5), "", 0)};
		}
		const char *params[3] = {"u", "v", "w"};
		for (int i = 0; i < 3; ++i)
			if (NodePtr r = json::At(o, i == 0 && json::Has(o, "t") ? "t" : params[i]); r && r->IsArray() && r->GetSize() >= 2) {
				spec.range[i][0] = json::Num(r->At(0), "", 0.0);
				spec.range[i][1] = json::Num(r->At(1), "", 1.0);
				if (r->GetSize() >= 3)
					spec.samples[i] = json::Int(r->At(2), "", spec.samples[i]);
			}
		spec.zUp = json::Bool(o, "z_up", true);
		spec.scale = json::Num(o, "scale", 1.0);
		spec.hollow = json::Bool(o, "hollow", false);
		spec.timeLimit = json::Num(o, "time_limit", 10.0);
		if (NodePtr m = json::At(o, "max_size"); m && m->IsArray() && m->GetSize() >= 3)
			spec.maxSize = {json::Int(m->At(0), "", 64), json::Int(m->At(1), "", 64), json::Int(m->At(2), "", 64)};
		spec.block = block(json::Str(o, "block"));
		if (!m_game.Content().FindBlock(spec.block))
			return Ok(Failure(String::Format("bloc inconnu « %s »", json::Str(o, "block").CStr())));
		Result<Schematic, String> s = Plot(spec);
		if (s.IsError())
			return Ok(Failure(s.Error()));
		s.Value().name = kind;
		m_game.SetClipboard(name, std::move(s.Value()));
		return Ok(Success(Info(name, *m_game.Clipboard(name))));
	});
	vm.RegisterNamespacedNative("formula", "check", 1, 2, [](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(text, sd::ArgString(args, 0, "formula.check"));
		std::vector<String> vars{"x", "y", "z"};
		if (args.size() > 1 && args[1].IsList()) {
			vars.clear();
			for (const Value &v : args[1].AsList()->items)
				if (v.IsString())
					vars.push_back(v.AsString());
		}
		Result<Formula, String> f = Formula::Compile(text, vars);
		return Ok(f.IsError() ? Value::Str(f.Error()) : Value::Nil());
	});
	vm.RegisterNamespacedNative("formula", "eval", 1, 2, [](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(text, sd::ArgString(args, 0, "formula.eval"));
		std::vector<String> names;
		std::vector<double> values;
		if (args.size() > 1 && args[1].IsMap()) {
			const NodePtr vars = script::NodeFromValue(args[1]);
			for (const String &key : vars->Keys())
				if (NodePtr value = vars->Get(key); value && value->IsNumber()) {
					names.push_back(key);
					values.push_back(json::Num(value, "", 0.0));
				}
		}
		Result<Formula, String> f = Formula::Compile(text, names);
		if (f.IsError())
			return Err(Error("formula.eval", f.Error()));
		const double v = f.Value().Eval(values.data());
		return Ok(std::isnan(v) ? Value::Nil() : Value::Number(v));
	});

	// ── Zones du monde ──────────────────────────────────────────────────────
	vm.RegisterNamespacedNative("world", "fill", 7, 8, [this, block](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(a, ArgPos(args, 0, "world.fill"));
		TRY(b, ArgPos(args, 3, "world.fill"));
		TRY(id, sd::ArgString(args, 6, "world.fill"));
		String mode("all");
		if (args.size() > 7) {
			TRY(m, sd::ArgString(args, 7, "world.fill"));
			mode = m;
		}
		Result<int64_t, String> r = m_game.FillRegion(a, b, block(id), mode);
		if (r.IsError())
			return Err(Error("world.fill", r.Error()));
		return Ok(Value::Int(r.Value()));
	});
	vm.RegisterNamespacedNative("world", "replace", 8, 8, [this, block](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(a, ArgPos(args, 0, "world.replace"));
		TRY(b, ArgPos(args, 3, "world.replace"));
		TRY(from, sd::ArgString(args, 6, "world.replace"));
		TRY(to, sd::ArgString(args, 7, "world.replace"));
		Result<int64_t, String> r = m_game.ReplaceRegion(a, b, from.IsEmpty() ? String() : block(from), block(to));
		if (r.IsError())
			return Err(Error("world.replace", r.Error()));
		return Ok(Value::Int(r.Value()));
	});
	vm.RegisterNamespacedNative("world", "count", 6, 6, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(a, ArgPos(args, 0, "world.count"));
		TRY(b, ArgPos(args, 3, "world.count"));
		if (!m_game.HasWorld())
			return Ok(Value::Nil());
		const Schematic s = Schematic::Capture(m_game.GetWorld(), a, b);
		auto map = std::make_shared<script::MapObject>();
		for (const auto &[id, n] : s.Counts())
			map->SetKey(id, Value::Int(n));
		return Ok(Value::Map(map));
	});

	// ── Surlignages et visée ────────────────────────────────────────────────
	vm.RegisterNamespacedNative("game", "highlight", 7, 8, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(id, sd::ArgString(args, 0, "game.highlight"));
		TRY(a, ArgPos(args, 1, "game.highlight"));
		TRY(b, ArgPos(args, 4, "game.highlight"));
		std::array<uint8_t, 4> color{255, 255, 255, 255};
		if (args.size() > 7 && args[7].IsList()) {
			const auto &items = args[7].AsList()->items;
			for (size_t i = 0; i < 4 && i < items.size(); ++i)
				if (items[i].IsNumber())
					color[i] = uint8_t(std::clamp(items[i].AsNumber(), 0.0, 255.0));
		}
		m_game.SetHighlight(id, a, b, color);
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("game", "clear_highlight", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		TRY(id, sd::ArgString(args, 0, "game.clear_highlight"));
		m_game.ClearHighlight(id);
		return Ok(Value::Nil());
	});
	vm.RegisterNamespacedNative("entity", "target", 1, 1, [this](script::Interpreter &, std::vector<Value> &args) -> NativeResult {
		const ecs::Entity e = EntityOf(args[0]);
		if (!e.Valid() || !m_game.HasWorld())
			return Ok(Value::Nil());
		Option<BlockHit> hit = m_game.TargetBlock(e);
		if (hit.IsNone())
			return Ok(Value::Nil());
		auto map = std::make_shared<script::MapObject>();
		const BlockHit &h = hit.Value();
		map->SetKey("x", Value::Int(h.block.x));
		map->SetKey("y", Value::Int(h.block.y));
		map->SetKey("z", Value::Int(h.block.z));
		map->SetKey("ax", Value::Int(h.previous.x)); // cellule devant la face visée
		map->SetKey("ay", Value::Int(h.previous.y));
		map->SetKey("az", Value::Int(h.previous.z));
		map->SetKey("block", Value::Str(m_game.GetWorld().BlockAt(h.block).id));
		return Ok(Value::Map(map));
	});
}

} // namespace voxel
