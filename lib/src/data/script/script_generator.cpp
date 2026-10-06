// Définitions de data/script/script_generator.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "data/script.hpp"
#include "data/script/script_generator.hpp"

namespace data::script {

namespace genlib {

// ── Options ──────────────────────────────────────────────────────────────────

Options::Options(const Value &v, const char *fn) : m_fn(fn) {
	if (v.IsMap() && v.AsMap())
		m_entries = v.AsMap()->Snapshot();
	else if (!v.IsNil())
		m_error = Some(Fail(String::Format("`%s` : table d'options attendue, trouvé `%s`", fn, v.TypeName())));
}

Option<Value> Options::Get(const char *key) {
	m_known.push_back(String(key));
	for (const auto &entry : m_entries)
		if (entry.first == key)
			return Some(entry.second);
	return NONE;
}

double Options::Number(const char *key, double fallback) {
	Option<Value> v = Get(key);
	if (v.IsNone())
		return fallback;
	if (!v.Value().IsNumber()) {
		Fault(String::Format("`%s` : option `%s` : nombre attendu", m_fn, key));
		return fallback;
	}
	return v.Value().AsNumber();
}

bool Options::Bool(const char *key, bool fallback) {
	Option<Value> v = Get(key);
	return v.IsNone() ? fallback : v.Value().IsTruthy();
}

String Options::Text(const char *key, const char *fallback) {
	Option<Value> v = Get(key);
	return v.IsNone() ? String(fallback) : v.Value().ToDisplayString();
}

void Options::Fault(String message) {
	if (m_error.IsNone())
		m_error = Some(Fail(std::move(message)));
}

Option<ScriptError> Options::Finish() {
	if (m_error.IsSome())
		return m_error;
	for (const auto &entry : m_entries)
		if (std::find(m_known.begin(), m_known.end(), entry.first) == m_known.end())
			return Some(Fail(String::Format("`%s` : option `%s` inconnue", m_fn, entry.first.CStr())));
	return NONE;
}

Result<generators::NoiseSettings, ScriptError> ReadNoise(Options &o, const char *fn) {
	using namespace generators;
	NoiseSettings s;
	if (Option<NoiseType> t = Named<NoiseType>(o.Text("type", "perlin"), {{"perlin", NoiseType::PERLIN},
																		   {"simplex", NoiseType::SIMPLEX},
																		   {"value", NoiseType::VALUE},
																		   {"worley", NoiseType::WORLEY},
																		   {"cellular", NoiseType::WORLEY}});
		t.IsSome())
		s.type = t.Unwrap();
	else
		o.Fault(String::Format("`%s` : type de bruit inconnu (perlin, simplex, value, worley)", fn));
	if (Option<FractalType> f = Named<FractalType>(o.Text("fractal", "fbm"), {{"none", FractalType::NONE},
																			   {"fbm", FractalType::FBM},
																			   {"ridged", FractalType::RIDGED},
																			   {"billow", FractalType::BILLOW},
																			   {"ping_pong", FractalType::PING_PONG}});
		f.IsSome())
		s.fractal = f.Unwrap();
	else
		o.Fault(String::Format("`%s` : fractale inconnue (none, fbm, ridged, billow, ping_pong)", fn));
	if (Option<CellularReturn> c = Named<CellularReturn>(o.Text("cellular", "f1"),
														 {{"f1", CellularReturn::F1},
														  {"f2", CellularReturn::F2},
														  {"f2_minus_f1", CellularReturn::F2_MINUS_F1},
														  {"cell_value", CellularReturn::CELL_VALUE}});
		c.IsSome())
		s.cellular = c.Unwrap();
	else
		o.Fault(String::Format("`%s` : sortie cellulaire inconnue (f1, f2, f2_minus_f1, cell_value)", fn));
	if (Option<DistanceMetric> m = Named<DistanceMetric>(o.Text("metric", "euclidean"),
														 {{"euclidean", DistanceMetric::EUCLIDEAN},
														  {"manhattan", DistanceMetric::MANHATTAN},
														  {"chebyshev", DistanceMetric::CHEBYSHEV}});
		m.IsSome())
		s.metric = m.Unwrap();
	else
		o.Fault(String::Format("`%s` : distance inconnue (euclidean, manhattan, chebyshev)", fn));
	s.seed = uint32_t(o.Number("seed", 1337));
	s.frequency = float(o.Number("frequency", 0.01));
	s.octaves = std::clamp(o.Int("octaves", 5), 1, 16);
	s.lacunarity = float(o.Number("lacunarity", 2.0));
	s.gain = float(o.Number("gain", 0.5));
	s.weightedStrength = float(o.Number("weighted_strength", 0.0));
	s.pingPongStrength = float(o.Number("ping_pong_strength", 2.0));
	s.jitter = float(o.Number("jitter", 1.0));
	s.warpAmplitude = float(o.Number("warp", 0.0));
	s.warpFrequency = float(o.Number("warp_frequency", 0.01));
	if (Option<Value> offset = o.Get("offset"); offset.IsSome() && offset.Value().IsList()) {
		const std::vector<Value> items = offset.Value().AsList()->Snapshot();
		if (items.size() > 0)
			s.offsetX = items[0].AsFloat();
		if (items.size() > 1)
			s.offsetY = items[1].AsFloat();
		if (items.size() > 2)
			s.offsetZ = items[2].AsFloat();
	}
	return Ok(s);
}

Option<generators::BlendMode> ReadBlend(const String &name) {
	using generators::BlendMode;
	return Named<BlendMode>(name, {{"set", BlendMode::SET},
								   {"add", BlendMode::ADD},
								   {"subtract", BlendMode::SUBTRACT},
								   {"multiply", BlendMode::MULTIPLY},
								   {"min", BlendMode::MIN},
								   {"max", BlendMode::MAX},
								   {"average", BlendMode::AVERAGE},
								   {"screen", BlendMode::SCREEN},
								   {"difference", BlendMode::DIFFERENCE},
								   {"overlay", BlendMode::OVERLAY}});
}

generators::HydraulicErosion ReadErosion(Options &o) {
	generators::HydraulicErosion p;
	p.droplets = std::clamp(o.Int("droplets", p.droplets), 0, 5000000);
	p.maxLifetime = std::clamp(o.Int("lifetime", p.maxLifetime), 1, 500);
	p.inertia = float(o.Number("inertia", p.inertia));
	p.sedimentCapacity = float(o.Number("capacity", p.sedimentCapacity));
	p.erodeSpeed = float(o.Number("erode_speed", p.erodeSpeed));
	p.depositSpeed = float(o.Number("deposit_speed", p.depositSpeed));
	p.evaporateSpeed = float(o.Number("evaporate_speed", p.evaporateSpeed));
	p.gravity = float(o.Number("gravity", p.gravity));
	p.radius = std::clamp(o.Int("radius", p.radius), 1, 16);
	p.seed = uint64_t(o.Number("seed", 1));
	return p;
}

generators::ThermalErosion ReadThermal(Options &o) {
	generators::ThermalErosion p;
	p.iterations = std::clamp(o.Int("iterations", p.iterations), 0, 10000);
	p.talus = float(o.Number("talus", p.talus));
	p.amount = float(o.Number("amount", p.amount));
	return p;
}

GenTypes TypesOf(Interpreter &vm) {
	return {lib::HostTypeOf(vm, "gen", "noise"), lib::HostTypeOf(vm, "gen", "heightmap"),
			lib::HostTypeOf(vm, "gen", "tilemap"), lib::HostTypeOf(vm, "gen", "names")};
}

Value MakeHeightMap(Interpreter &vm, generators::HeightMap map) {
	auto object = std::make_shared<HeightMapObject>();
	object->type = TypesOf(vm).heightmap;
	object->map = std::move(map);
	return Value::Host(std::move(object));
}

const generators::Noise * AsNoise(const Value &v) {
	if (!v.IsHost() || !v.AsHost())
		return nullptr;
	const auto *noise = dynamic_cast<const NoiseObject *>(v.AsHost().get());
	return noise ? &noise->noise : nullptr;
}

HeightMapObject * AsMap(const Value &v) {
	if (!v.IsHost() || !v.AsHost())
		return nullptr;
	return dynamic_cast<HeightMapObject *>(v.AsHost().get());
}

Value Pair(double a, double b) {
	return Value::List(std::make_shared<ListObject>(std::vector<Value>{Value::Number(a), Value::Number(b)}));
}

Value IntPair(int a, int b) {
	return Value::List(std::make_shared<ListObject>(std::vector<Value>{Value::Int(a), Value::Int(b)}));
}

void DefineNoise(TypeBuilder &builder) {
	std::weak_ptr<const HostType> weak = builder.type;
	builder.Construct(0, 1, [weak](Interpreter &, Args &args, const Args &) -> Result<Value, ScriptError> {
		Options o(args.empty() ? Value::Nil() : args[0], "gen.noise");
		auto settings = ReadNoise(o, "gen.noise");
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		auto object = std::make_shared<NoiseObject>();
		object->type = weak.lock();
		object->noise = generators::Noise(settings.Unwrap());
		return Ok(Value::Host(std::move(object)));
	});
	builder.Method("sample", 2, 3, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		float c[3] = {0, 0, 0};
		for (size_t i = 0; i < args.size(); ++i) {
			auto v = lib::ArgFloat(args, i, "gen.noise.sample");
			if (v.IsError())
				return Err(v.Error());
			c[i] = float(v.Value());
		}
		const generators::Noise &n = As<NoiseObject>(self).noise;
		return Ok(Value::Number(args.size() == 3 ? n.Sample(c[0], c[1], c[2]) : n.Sample(c[0], c[1])));
	});
	builder.Method("sample01", 2, 2, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		auto x = lib::ArgFloat(args, 0, "gen.noise.sample01");
		auto y = lib::ArgFloat(args, 1, "gen.noise.sample01");
		if (x.IsError())
			return Err(x.Error());
		if (y.IsError())
			return Err(y.Error());
		return Ok(Value::Number(As<NoiseObject>(self).noise.Sample01(float(x.Value()), float(y.Value()))));
	});
	builder.type->display = [](const HostObject &self) {
		const generators::NoiseSettings &s = As<NoiseObject>(self).noise.Settings();
		static const char *TYPES[] = {"perlin", "simplex", "value", "worley"};
		static const char *FRACTALS[] = {"none", "fbm", "ridged", "billow", "ping_pong"};
		return String::Format("<gen.noise %s/%s, graine %u, fréquence %g, %d octave(s)>", TYPES[int(s.type)],
							  FRACTALS[int(s.fractal)], s.seed, double(s.frequency), s.octaves);
	};
}

void DefineHeightMap(TypeBuilder &builder) {
	using Object = HeightMapObject;
	std::weak_ptr<const HostType> weak = builder.type;
	builder.Construct(2, 3, [weak](Interpreter &, Args &args, const Args &) -> Result<Value, ScriptError> {
		auto w = lib::ArgInt(args, 0, "gen.heightmap");
		auto h = lib::ArgInt(args, 1, "gen.heightmap");
		if (w.IsError())
			return Err(w.Error());
		if (h.IsError())
			return Err(h.Error());
		if (w.Value() < 1 || h.Value() < 1 || w.Value() * h.Value() > 64000000)
			return Err(Fail(String("`gen.heightmap` : taille invalide (au plus 64 millions de cases)")));
		const float fill = args.size() > 2 ? args[2].AsFloat() : 0.f;
		auto object = std::make_shared<Object>();
		object->type = weak.lock();
		object->map = generators::HeightMap(int(w.Value()), int(h.Value()), fill);
		return Ok(Value::Host(std::move(object)));
	});
	// Transformation en place, sous le verrou ; rend la carte (chaînage).
	auto edit = [](std::function<Option<ScriptError>(generators::HeightMap &, Args &)> fn) {
		return [fn](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
			Object &o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			if (auto error = fn(o.map, args); error.IsSome())
				return Err(error.Unwrap());
			return Ok(Value::Host(self));
		};
	};
	auto read = [](std::function<Result<Value, ScriptError>(const generators::HeightMap &, Args &)> fn) {
		return [fn](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
			Object &o = As<Object>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			return fn(o.map, args);
		};
	};
	auto num = [](const Args &args, size_t i, double fallback) {
		return i < args.size() && args[i].IsNumber() ? args[i].AsNumber() : fallback;
	};
	builder.Method("get", 2, 2, read([](const generators::HeightMap &m, Args &a) -> Result<Value, ScriptError> {
		return Ok(Value::Number(m.At(int(a[0].AsInt64()), int(a[1].AsInt64()))));
	}));
	builder.Method("set", 3, 3, edit([](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.Set(int(a[0].AsInt64()), int(a[1].AsInt64()), a[2].AsFloat());
		return NONE;
	}));
	builder.Method("sample", 2, 2, read([](const generators::HeightMap &m, Args &a) -> Result<Value, ScriptError> {
		return Ok(Value::Number(m.Sample(a[0].AsFloat(), a[1].AsFloat())));
	}));
	builder.Method("sample_uv", 2, 2, read([](const generators::HeightMap &m, Args &a) -> Result<Value, ScriptError> {
		return Ok(Value::Number(m.SampleUV(a[0].AsFloat(), a[1].AsFloat())));
	}));
	builder.Method("fill", 1, 2, edit([num](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		const generators::Noise *noise = AsNoise(a[0]);
		if (!noise)
			return Some(Fail(String("`gen.heightmap.fill` : un gen.noise attendu")));
		m.Fill(*noise, float(num(a, 1, 1.0)));
		return NONE;
	}));
	// add_noise(bruit, {blend, strength, scale, mask})
	builder.Method("add_noise", 1, 2, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		const generators::Noise *noise = AsNoise(args[0]);
		if (!noise)
			return Err(Fail(String("`gen.heightmap.add_noise` : un gen.noise attendu")));
		Options o(args.size() > 1 ? args[1] : Value::Nil(), "gen.heightmap.add_noise");
		Option<generators::BlendMode> blend = ReadBlend(o.Text("blend", "add"));
		const float strength = float(o.Number("strength", 1.0)), scale = float(o.Number("scale", 1.0));
		Option<Value> mask = o.Get("mask");
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		if (blend.IsNone())
			return Err(Fail(String("`gen.heightmap.add_noise` : mélange inconnu")));
		Object &target = As<Object>(self);
		generators::HeightMap maskCopy;
		const bool masked = mask.IsSome() && AsMap(mask.Value());
		if (masked) {
			HeightMapObject *m = AsMap(mask.Value());
			std::lock_guard<std::mutex> lock(m->mutex);
			maskCopy = m->map;
		}
		std::lock_guard<std::mutex> lock(target.mutex);
		target.map.AddNoise(*noise, blend.Unwrap(), strength, scale, masked ? &maskCopy : nullptr);
		return Ok(Value::Host(self));
	});
	// combine(autre, mélange, intensité[, masque])
	builder.Method("combine", 1, 4, [](Interpreter &, const HostRef &self, Args &args) -> Result<Value, ScriptError> {
		HeightMapObject *other = AsMap(args[0]);
		if (!other)
			return Err(Fail(String("`gen.heightmap.combine` : une gen.heightmap attendue")));
		Option<generators::BlendMode> blend = ReadBlend(args.size() > 1 ? args[1].ToDisplayString() : String("add"));
		if (blend.IsNone())
			return Err(Fail(String("`gen.heightmap.combine` : mélange inconnu (set, add, subtract, multiply, min, max, "
								   "average, screen, difference, overlay)")));
		const float strength = args.size() > 2 ? args[2].AsFloat() : 1.f;
		generators::HeightMap source, maskCopy;
		{
			std::lock_guard<std::mutex> lock(other->mutex);
			source = other->map;
		}
		HeightMapObject *mask = args.size() > 3 ? AsMap(args[3]) : nullptr;
		if (mask) {
			std::lock_guard<std::mutex> lock(mask->mutex);
			maskCopy = mask->map;
		}
		Object &target = As<Object>(self);
		std::lock_guard<std::mutex> lock(target.mutex);
		target.map.Combine(source, blend.Unwrap(), strength, mask ? &maskCopy : nullptr);
		return Ok(Value::Host(self));
	});
	builder.Method("scale", 1, 2, edit([num](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.Scale(float(num(a, 0, 1.0)), float(num(a, 1, 0.0)));
		return NONE;
	}));
	builder.Method("clamp", 0, 2, edit([num](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.Clamp(float(num(a, 0, 0.0)), float(num(a, 1, 1.0)));
		return NONE;
	}));
	builder.Method("normalize", 0, 2, edit([num](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.Normalize(float(num(a, 0, 0.0)), float(num(a, 1, 1.0)));
		return NONE;
	}));
	builder.Method("remap", 4, 4, edit([](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.Remap(a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), a[3].AsFloat());
		return NONE;
	}));
	builder.Method("invert", 0, 0, edit([](generators::HeightMap &m, Args &) -> Option<ScriptError> {
		m.Invert();
		return NONE;
	}));
	builder.Method("abs", 0, 0, edit([](generators::HeightMap &m, Args &) -> Option<ScriptError> {
		m.Abs();
		return NONE;
	}));
	builder.Method("power", 1, 1, edit([](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.Power(a[0].AsFloat());
		return NONE;
	}));
	// curve([[x, y], …]) — points x croissants.
	builder.Method("curve", 1, 1, edit([](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		if (!a[0].IsList())
			return Some(Fail(String("`gen.heightmap.curve` : liste de points [x, y] attendue")));
		std::vector<std::pair<float, float>> points;
		for (const Value &p : a[0].AsList()->Snapshot()) {
			if (!p.IsList() || p.AsList()->Size() != 2)
				return Some(Fail(String("`gen.heightmap.curve` : chaque point est [x, y]")));
			points.emplace_back(p.AsList()->At(0).Unwrap().AsFloat(), p.AsList()->At(1).Unwrap().AsFloat());
		}
		std::sort(points.begin(), points.end());
		m.Curve(points);
		return NONE;
	}));
	builder.Method("terrace", 1, 2, edit([num](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.Terrace(int(a[0].AsInt64()), float(num(a, 1, 0.5)));
		return NONE;
	}));
	builder.Method("sea_level", 1, 1, edit([](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.SeaLevel(a[0].AsFloat());
		return NONE;
	}));
	builder.Method("blur", 1, 2, edit([num](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.BoxBlur(std::clamp(int(a[0].AsInt64()), 0, 64), std::clamp(int(num(a, 1, 1.0)), 1, 8));
		return NONE;
	}));
	builder.Method("smooth", 1, 1, edit([](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.Smooth(std::clamp(a[0].AsFloat(), 0.f, 64.f));
		return NONE;
	}));
	builder.Method("sharpen", 1, 2, edit([num](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.Sharpen(a[0].AsFloat(), std::clamp(int(num(a, 1, 1.0)), 1, 16));
		return NONE;
	}));
	builder.Method("island", 0, 2, edit([num](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		m.IslandFalloff(float(num(a, 0, 2.0)), float(num(a, 1, 1.0)));
		return NONE;
	}));
	builder.Method("erode", 0, 1, edit([](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		Options o(a.empty() ? Value::Nil() : a[0], "gen.heightmap.erode");
		const generators::HydraulicErosion p = ReadErosion(o);
		if (auto error = o.Finish(); error.IsSome())
			return error;
		m.ErodeHydraulic(p);
		return NONE;
	}));
	builder.Method("erode_thermal", 0, 1, edit([](generators::HeightMap &m, Args &a) -> Option<ScriptError> {
		Options o(a.empty() ? Value::Nil() : a[0], "gen.heightmap.erode_thermal");
		const generators::ThermalErosion p = ReadThermal(o);
		if (auto error = o.Finish(); error.IsSome())
			return error;
		m.ErodeThermal(p);
		return NONE;
	}));
	// Cartes dérivées : de NOUVELLES cartes.
	builder.Method("slope_map", 0, 2, [num](Interpreter &vm, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
		Object &o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(MakeHeightMap(vm, o.map.SlopeMap(float(num(a, 0, 1.0)), float(num(a, 1, 1.0)))));
	});
	builder.Method("height_mask", 2, 3, [num](Interpreter &vm, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
		Object &o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(MakeHeightMap(vm, o.map.HeightMask(a[0].AsFloat(), a[1].AsFloat(), float(num(a, 2, 0.0)))));
	});
	builder.Method("slope_mask", 2, 5, [num](Interpreter &vm, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
		Object &o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(MakeHeightMap(vm, o.map.SlopeMask(a[0].AsFloat(), a[1].AsFloat(), float(num(a, 2, 0.0)),
													float(num(a, 3, 1.0)), float(num(a, 4, 1.0)))));
	});
	builder.Method("resized", 2, 2, [](Interpreter &vm, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
		const int w = int(a[0].AsInt64()), h = int(a[1].AsInt64());
		if (w < 1 || h < 1 || int64_t(w) * h > 64000000)
			return Err(Fail(String("`gen.heightmap.resized` : taille invalide")));
		Object &o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(MakeHeightMap(vm, o.map.Resized(w, h)));
	});
	builder.Method("copy", 0, 0, [](Interpreter &vm, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		Object &o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return Ok(MakeHeightMap(vm, o.map));
	});
	builder.Method("min", 0, 0, read([](const generators::HeightMap &m, Args &) -> Result<Value, ScriptError> {
		return Ok(Value::Number(m.Min()));
	}));
	builder.Method("max", 0, 0, read([](const generators::HeightMap &m, Args &) -> Result<Value, ScriptError> {
		return Ok(Value::Number(m.Max()));
	}));
	builder.Method("mean", 0, 0, read([](const generators::HeightMap &m, Args &) -> Result<Value, ScriptError> {
		return Ok(Value::Number(m.Mean()));
	}));
	// Lignes de la carte (liste de listes) — pour un petit aperçu.
	builder.Method("to_list", 0, 0, read([](const generators::HeightMap &m, Args &) -> Result<Value, ScriptError> {
		auto rows = std::make_shared<ListObject>();
		for (int y = 0; y < m.Height(); ++y) {
			std::vector<float> row(m.Data().begin() + ptrdiff_t(y) * m.Width(),
								   m.Data().begin() + ptrdiff_t(y + 1) * m.Width());
			rows->items.push_back(NumberList(row));
		}
		return Ok(Value::List(std::move(rows)));
	}));
	auto saved = [](Option<String> error) -> Result<Value, ScriptError> {
		if (error.IsSome())
			return Err(Fail(error.Unwrap()));
		return Ok(Value::Boolean(true));
	};
	builder.Method("save_pgm", 1, 2, read([saved](const generators::HeightMap &m, Args &a) -> Result<Value, ScriptError> {
		return saved(m.SavePgm(a[0].ToDisplayString(), a.size() > 1 && a[1].IsTruthy()));
	}));
	builder.Method("save_raw16", 1, 1, read([saved](const generators::HeightMap &m, Args &a) -> Result<Value, ScriptError> {
		return saved(m.SaveRaw16(a[0].ToDisplayString()));
	}));
	// Image en couleurs de biomes (PPM) : save_colored(chemin[, niveau_de_la_mer]).
	builder.Method("save_colored", 1, 2,
				   read([saved](const generators::HeightMap &m, Args &a) -> Result<Value, ScriptError> {
					   const auto rules = generators::DefaultBiomes(a.size() > 1 ? a[1].AsFloat() : 0.3f);
					   const generators::HeightMap slope = m.SlopeMap(1.f, float(m.Width()) * 0.35f);
					   return saved(m.SavePpm(a[0].ToDisplayString(), [&](float v, int x, int y) {
						   return generators::BiomeColor(rules, v, slope.At(x, y));
					   }));
				   }));
	// Maillage : mesh({cell_size, height_scale, step, biomes: niveau_de_la_mer})
	// → {positions, normals, uvs, colors, indices, vertex_count, triangle_count}.
	auto meshOf = [](const generators::HeightMap &m, const Value &opts,
					 generators::TerrainMesh &mesh) -> Option<ScriptError> {
		Options o(opts, "gen.heightmap.mesh");
		generators::TerrainMeshSettings s;
		s.cellSize = float(o.Number("cell_size", 1.0));
		s.heightScale = float(o.Number("height_scale", 50.0));
		s.step = std::clamp(o.Int("step", 1), 1, 64);
		s.centered = o.Bool("centered", true);
		Option<Value> biomes = o.Get("biomes");
		if (auto error = o.Finish(); error.IsSome())
			return error;
		const std::vector<generators::BiomeRule> rules =
			biomes.IsSome() && biomes.Value().IsNumber() ? generators::DefaultBiomes(biomes.Value().AsFloat())
														 : std::vector<generators::BiomeRule>{};
		mesh = generators::BuildTerrainMesh(m, s, rules);
		return NONE;
	};
	builder.Method("mesh", 0, 1, read([meshOf](const generators::HeightMap &m, Args &a) -> Result<Value, ScriptError> {
		generators::TerrainMesh mesh;
		if (auto error = meshOf(m, a.empty() ? Value::Nil() : a[0], mesh); error.IsSome())
			return Err(error.Unwrap());
		auto map = std::make_shared<MapObject>();
		map->SetKey(String("positions"), NumberList(mesh.positions));
		map->SetKey(String("normals"), NumberList(mesh.normals));
		map->SetKey(String("uvs"), NumberList(mesh.uvs));
		map->SetKey(String("colors"), NumberList(mesh.colors));
		map->SetKey(String("indices"), NumberList(mesh.indices));
		map->SetKey(String("vertex_count"), Value::Int(int64_t(mesh.VertexCount())));
		map->SetKey(String("triangle_count"), Value::Int(int64_t(mesh.TriangleCount())));
		return Ok(Value::Map(std::move(map)));
	}));
	builder.Method("save_obj", 1, 2,
				   read([meshOf, saved](const generators::HeightMap &m, Args &a) -> Result<Value, ScriptError> {
					   generators::TerrainMesh mesh;
					   if (auto error = meshOf(m, a.size() > 1 ? a[1] : Value::Nil(), mesh); error.IsSome())
						   return Err(error.Unwrap());
					   return saved(mesh.SaveObj(a[0].ToDisplayString()));
				   }));
	HostType &type = *builder.type;
	type.get = [](const HostObject &self, const String &name) -> Option<Value> {
		const Object &o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		if (name == "width")
			return Some(Value::Int(o.map.Width()));
		if (name == "height")
			return Some(Value::Int(o.map.Height()));
		return NONE;
	};
	type.display = [](const HostObject &self) {
		const Object &o = As<Object>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		return String::Format("<gen.heightmap %d×%d, [%g, %g]>", o.map.Width(), o.map.Height(), double(o.map.Min()),
							  double(o.map.Max()));
	};
	type.size = [](const HostObject &self) { return As<Object>(self).map.Size(); };
	// Opérateurs : carte ± / × carte ou nombre → nouvelle carte.
	type.binary = [](Interpreter &vm, BinaryOp op, const Value &a, const Value &b) -> Option<Result<Value, ScriptError>> {
		using R = Result<Value, ScriptError>;
		generators::BlendMode mode;
		switch (op) {
			case BinaryOp::ADD:
				mode = generators::BlendMode::ADD;
				break;
			case BinaryOp::SUBTRACT:
				mode = generators::BlendMode::SUBTRACT;
				break;
			case BinaryOp::MULTIPLY:
				mode = generators::BlendMode::MULTIPLY;
				break;
			default:
				return NONE;
		}
		HeightMapObject *left = AsMap(a);
		HeightMapObject *right = AsMap(b);
		generators::HeightMap result;
		if (left) {
			std::lock_guard<std::mutex> lock(left->mutex);
			result = left->map;
		} else if (right && a.IsNumber() && op != BinaryOp::SUBTRACT) {
			std::lock_guard<std::mutex> lock(right->mutex);
			result = right->map;
			return Some(R(Ok(MakeHeightMap(vm, op == BinaryOp::ADD ? result.Scale(1.f, a.AsFloat())
																	: result.Scale(a.AsFloat(), 0.f)))));
		} else {
			return NONE;
		}
		if (right) {
			generators::HeightMap other;
			{
				std::lock_guard<std::mutex> lock(right->mutex);
				other = right->map;
			}
			result.Combine(other, mode, 1.f);
		} else if (b.IsNumber()) {
			const float n = b.AsFloat();
			result.Apply([&](float v) { return generators::Blend(mode, v, n); });
		} else {
			return NONE;
		}
		return Some(R(Ok(MakeHeightMap(vm, std::move(result)))));
	};
}

const char * TileName(generators::Tile t) {
	switch (t) {
		case generators::Tile::WALL:
			return "wall";
		case generators::Tile::FLOOR:
			return "floor";
		case generators::Tile::CORRIDOR:
			return "corridor";
		case generators::Tile::DOOR:
			return "door";
		case generators::Tile::ENTRANCE:
			return "entrance";
		case generators::Tile::EXIT:
			return "exit";
	}
	return "?";
}

Value MakeDungeon(Interpreter &vm, generators::Dungeon dungeon) {
	auto object = std::make_shared<DungeonObject>();
	object->type = TypesOf(vm).dungeon;
	object->dungeon = std::move(dungeon);
	return Value::Host(std::move(object));
}

void DefineDungeon(TypeBuilder &builder) {
	using Object = DungeonObject;
	auto cell = [](const Args &a, size_t i) { return int(a[i].AsInt64()); };
	builder.Method("get", 2, 2, [cell](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
		return Ok(Value::Str(String(TileName(As<Object>(self).dungeon.tiles.At(cell(a, 0), cell(a, 1))))));
	});
	builder.Method("walkable", 2, 2, [cell](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
		return Ok(Value::Boolean(generators::IsWalkable(As<Object>(self).dungeon.tiles.At(cell(a, 0), cell(a, 1)))));
	});
	builder.Method("rows", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		auto list = std::make_shared<ListObject>();
		for (String &row : As<Object>(self).dungeon.Rows())
			list->items.push_back(Value::Str(std::move(row)));
		return Ok(Value::List(std::move(list)));
	});
	builder.Method("rooms", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		auto list = std::make_shared<ListObject>();
		for (const generators::Rect &r : As<Object>(self).dungeon.rooms) {
			auto room = std::make_shared<MapObject>();
			room->SetKey(String("x"), Value::Int(r.x));
			room->SetKey(String("y"), Value::Int(r.y));
			room->SetKey(String("w"), Value::Int(r.w));
			room->SetKey(String("h"), Value::Int(r.h));
			room->SetKey(String("cx"), Value::Int(r.CenterX()));
			room->SetKey(String("cy"), Value::Int(r.CenterY()));
			list->items.push_back(Value::Map(std::move(room)));
		}
		return Ok(Value::List(std::move(list)));
	});
	builder.Method("doors", 0, 0, [](Interpreter &, const HostRef &self, Args &) -> Result<Value, ScriptError> {
		auto list = std::make_shared<ListObject>();
		for (const generators::Cell &c : As<Object>(self).dungeon.doors)
			list->items.push_back(IntPair(c.x, c.y));
		return Ok(Value::List(std::move(list)));
	});
	// Cases d'un type : cells("floor") → [[x, y], …]
	builder.Method("cells", 1, 1, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
		const String wanted = a[0].ToDisplayString();
		const generators::Dungeon &d = As<Object>(self).dungeon;
		auto list = std::make_shared<ListObject>();
		for (int y = 0; y < d.tiles.Height(); ++y)
			for (int x = 0; x < d.tiles.Width(); ++x)
				if (wanted == TileName(d.tiles.At(x, y)) ||
					(wanted == "walkable" && generators::IsWalkable(d.tiles.At(x, y))))
					list->items.push_back(IntPair(x, y));
		return Ok(Value::List(std::move(list)));
	});
	builder.Method("count", 1, 1, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
		const String wanted = a[0].ToDisplayString();
		int64_t n = 0;
		for (generators::Tile t : As<Object>(self).dungeon.tiles.Cells())
			n += wanted == TileName(t) ? 1 : 0;
		return Ok(Value::Int(n));
	});
	// Distance à pied entre deux cases (-1 : inaccessible).
	builder.Method("distance", 4, 4, [cell](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
		const generators::Dungeon &d = As<Object>(self).dungeon;
		const generators::Grid<int> field = generators::DistanceField(d.tiles, {cell(a, 0), cell(a, 1)});
		return Ok(Value::Int(field.At(cell(a, 2), cell(a, 3), -1)));
	});
	HostType &type = *builder.type;
	type.get = [](const HostObject &self, const String &name) -> Option<Value> {
		const generators::Dungeon &d = As<Object>(self).dungeon;
		if (name == "width")
			return Some(Value::Int(d.tiles.Width()));
		if (name == "height")
			return Some(Value::Int(d.tiles.Height()));
		if (name == "entrance")
			return Some(IntPair(d.entrance.x, d.entrance.y));
		if (name == "exit")
			return Some(IntPair(d.exit.x, d.exit.y));
		if (name == "entrance_room")
			return Some(Value::Int(d.entranceRoom));
		if (name == "exit_room")
			return Some(Value::Int(d.exitRoom));
		return NONE;
	};
	type.items = [](const HostObject &self) {
		std::vector<Value> rows;
		for (String &row : As<Object>(self).dungeon.Rows())
			rows.push_back(Value::Str(std::move(row)));
		return rows;
	};
	type.display = [](const HostObject &self) {
		const generators::Dungeon &d = As<Object>(self).dungeon;
		return String::Format("<gen.tilemap %d×%d, %d salle(s), %d porte(s)>", d.tiles.Width(), d.tiles.Height(),
							  int(d.rooms.size()), int(d.doors.size()));
	};
}

void DefineNames(TypeBuilder &builder) {
	using Object = NamesObject;
	builder.Method("generate", 0, 2, [](Interpreter &, const HostRef &self, Args &a) -> Result<Value, ScriptError> {
		Object &o = As<Object>(self);
		const int lo = a.size() > 0 ? int(a[0].AsInt64()) : 4, hi = a.size() > 1 ? int(a[1].AsInt64()) : 12;
		std::lock_guard<std::mutex> lock(o.mutex);
		String name = o.names.Generate(o.rng, std::max(1, lo), std::max(lo, hi));
		return Ok(name.IsEmpty() ? Value::Nil() : Value::Str(std::move(name)));
	});
	builder.type->display = [](const HostObject &) { return String("<gen.names>"); };
}

Result<generators::TerrainSettings, ScriptError> ReadTerrain(Options &o) {
	generators::TerrainSettings t;
	const uint32_t seed = uint32_t(o.Number("seed", 1));
	t = generators::DefaultTerrain(seed);
	t.width = std::clamp(o.Int("width", t.width), 2, 4097);
	t.height = std::clamp(o.Int("height", t.height), 2, 4097);
	t.scale = float(o.Number("scale", 1.0));
	if (Option<Value> layers = o.Get("layers"); layers.IsSome()) {
		if (!layers.Value().IsList())
			return Err(Fail(String("`gen.terrain` : `layers` doit être une liste de tables")));
		t.layers.clear();
		for (const Value &item : layers.Value().AsList()->Snapshot()) {
			Options layer(item, "gen.terrain (couche)");
			auto noise = ReadNoise(layer, "gen.terrain");
			generators::TerrainLayer l;
			l.noise = noise.Unwrap();
			Option<generators::BlendMode> blend = ReadBlend(layer.Text("blend", t.layers.empty() ? "set" : "add"));
			if (blend.IsNone())
				return Err(Fail(String("`gen.terrain` : mélange de couche inconnu")));
			l.blend = blend.Unwrap();
			l.strength = float(layer.Number("strength", 1.0));
			l.maskLayer = layer.Int("mask", -1);
			l.maskLow = float(layer.Number("mask_low", 0.0));
			l.maskHigh = float(layer.Number("mask_high", 1.0));
			l.maskFeather = float(layer.Number("mask_feather", 0.1));
			l.enabled = layer.Bool("enabled", true);
			if (auto error = layer.Finish(); error.IsSome())
				return Err(error.Unwrap());
			t.layers.push_back(l);
		}
	}
	t.normalize = o.Bool("normalize", true);
	t.islandFalloff = float(o.Number("island", 0.0));
	t.terraces = o.Int("terraces", 0);
	t.terraceSharpness = float(o.Number("terrace_sharpness", 0.5));
	t.smooth = float(o.Number("smooth", 0.0));
	t.seaLevel = float(o.Number("sea_level", -1.0));
	if (Option<Value> curve = o.Get("curve"); curve.IsSome() && curve.Value().IsList())
		for (const Value &p : curve.Value().AsList()->Snapshot())
			if (p.IsList() && p.AsList()->Size() == 2)
				t.curve.emplace_back(p.AsList()->At(0).Unwrap().AsFloat(), p.AsList()->At(1).Unwrap().AsFloat());
	if (Option<Value> erosion = o.Get("erosion"); erosion.IsSome() && erosion.Value().IsTruthy()) {
		Options e(erosion.Value().IsMap() ? erosion.Value() : Value::Nil(), "gen.terrain (erosion)");
		t.erosion = Some(ReadErosion(e));
		if (auto error = e.Finish(); error.IsSome())
			return Err(error.Unwrap());
	}
	if (Option<Value> thermal = o.Get("thermal"); thermal.IsSome() && thermal.Value().IsTruthy()) {
		Options e(thermal.Value().IsMap() ? thermal.Value() : Value::Nil(), "gen.terrain (thermal)");
		t.thermal = Some(ReadThermal(e));
		if (auto error = e.Finish(); error.IsSome())
			return Err(error.Unwrap());
	}
	return Ok(std::move(t));
}

void InstallFunctions(Interpreter &vm) {
	const String ns("gen");
	constexpr auto ANY = Interpreter::NativeThread::ANY;
	vm.RegisterNamespacedNative(ns, String("terrain"), 0, 1, [](Interpreter &vm, Args &args) -> Result<Value, ScriptError> {
		Options o(args.empty() ? Value::Nil() : args[0], "gen.terrain");
		auto settings = ReadTerrain(o);
		if (settings.IsError())
			return Err(settings.Error());
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		return Ok(MakeHeightMap(vm, generators::GenerateTerrain(settings.Value())));
	}, ANY);
	// Biome d'un point : gen.biome(hauteur, pente[, niveau_de_la_mer]).
	vm.RegisterNamespacedNative(ns, String("biome"), 2, 3, [](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		const auto rules = generators::DefaultBiomes(args.size() > 2 ? args[2].AsFloat() : 0.3f);
		const int index = generators::ClassifyBiome(rules, args[0].AsFloat(), args[1].AsFloat());
		return Ok(index < 0 ? Value::Nil() : Value::Str(rules[size_t(index)].name));
	}, ANY);
	// Points de Poisson : gen.poisson(largeur, hauteur, rayon, graine[, densité(x, y)]).
	vm.RegisterNamespacedNative(ns, String("poisson"), 4, 5, [](Interpreter &vm, Args &args) -> Result<Value, ScriptError> {
		for (size_t i = 0; i < 4; ++i)
			if (!args[i].IsNumber())
				return Err(Fail(String("`gen.poisson(largeur, hauteur, rayon, graine[, densité])` : nombres attendus")));
		Option<ScriptError> failure = NONE;
		std::function<float(float, float)> density;
		if (args.size() > 4 && !args[4].IsNil()) {
			if (!vm.IsCallableValue(args[4]))
				return Err(Fail(String("`gen.poisson` : la densité doit être une fonction (x, y)")));
			Value fn = args[4];
			density = [&vm, fn, &failure](float x, float y) -> float {
				if (failure.IsSome())
					return 0.f;
				auto v = vm.CallValue(fn, {Value::Number(x), Value::Number(y)}, 0, 0);
				if (v.IsError()) {
					failure = Some(v.Error());
					return 0.f;
				}
				return v.Value().IsNumber() ? v.Value().AsFloat() : (v.Value().IsTruthy() ? 1.f : 0.f);
			};
		}
		const auto points = generators::PoissonDisk(args[0].AsFloat(), args[1].AsFloat(), args[2].AsFloat(),
													args[3].AsUInt64(), density);
		if (failure.IsSome())
			return Err(failure.Unwrap());
		auto list = std::make_shared<ListObject>();
		for (const generators::Point2 &p : points)
			list->items.push_back(Pair(p.x, p.y));
		return Ok(Value::List(std::move(list)));
	}, ANY);
	vm.RegisterNamespacedNative(ns, String("jittered_grid"), 5, 5, [](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		auto list = std::make_shared<ListObject>();
		for (const generators::Point2 &p : generators::JitteredGrid(args[0].AsFloat(), args[1].AsFloat(),
																	args[2].AsFloat(), args[3].AsFloat(), args[4].AsUInt64()))
			list->items.push_back(Pair(p.x, p.y));
		return Ok(Value::List(std::move(list)));
	}, ANY);
	// Donjons.
	vm.RegisterNamespacedNative(ns, String("maze"), 2, 3, [](Interpreter &vm, Args &args) -> Result<Value, ScriptError> {
		Options o(args.size() > 2 ? args[2] : Value::Nil(), "gen.maze");
		const uint64_t seed = uint64_t(o.Number("seed", 1));
		const float wiggle = float(o.Number("wiggle", 0.5));
		Option<generators::MazeAlgorithm> algo = Named<generators::MazeAlgorithm>(
			o.Text("algorithm", "backtracker"), {{"backtracker", generators::MazeAlgorithm::BACKTRACKER},
												 {"prim", generators::MazeAlgorithm::PRIM},
												 {"binary_tree", generators::MazeAlgorithm::BINARY_TREE}});
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		if (algo.IsNone())
			return Err(Fail(String("`gen.maze` : algorithme inconnu (backtracker, prim, binary_tree)")));
		const int w = std::clamp(int(args[0].AsInt64()), 5, 2001), h = std::clamp(int(args[1].AsInt64()), 5, 2001);
		return Ok(MakeDungeon(vm, generators::GenerateMaze(w, h, seed, algo.Unwrap(), wiggle)));
	}, ANY);
	vm.RegisterNamespacedNative(ns, String("dungeon"), 0, 1, [](Interpreter &vm, Args &args) -> Result<Value, ScriptError> {
		Options o(args.empty() ? Value::Nil() : args[0], "gen.dungeon");
		generators::RoomsAndMazesConfig c;
		c.width = std::clamp(o.Int("width", c.width), 9, 2001);
		c.height = std::clamp(o.Int("height", c.height), 9, 2001);
		c.roomAttempts = std::clamp(o.Int("rooms", c.roomAttempts), 0, 10000);
		c.roomSizeMin = o.Int("room_min", c.roomSizeMin);
		c.roomSizeMax = o.Int("room_max", c.roomSizeMax);
		c.wiggle = float(o.Number("wiggle", c.wiggle));
		c.extraConnectionChance = float(o.Number("extra_doors", c.extraConnectionChance));
		c.deadendKeepChance = float(o.Number("keep_dead_ends", c.deadendKeepChance));
		c.seed = uint64_t(o.Number("seed", 1));
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		return Ok(MakeDungeon(vm, generators::GenerateRoomsAndMazes(c)));
	}, ANY);
	vm.RegisterNamespacedNative(ns, String("bsp"), 0, 1, [](Interpreter &vm, Args &args) -> Result<Value, ScriptError> {
		Options o(args.empty() ? Value::Nil() : args[0], "gen.bsp");
		generators::BspConfig c;
		c.width = std::clamp(o.Int("width", c.width), 8, 2000);
		c.height = std::clamp(o.Int("height", c.height), 8, 2000);
		c.minLeaf = std::clamp(o.Int("min_leaf", c.minLeaf), 5, 1000);
		c.roomMargin = std::clamp(o.Int("margin", c.roomMargin), 1, 20);
		c.fillMin = float(o.Number("fill_min", c.fillMin));
		c.seed = uint64_t(o.Number("seed", 1));
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		return Ok(MakeDungeon(vm, generators::GenerateBsp(c)));
	}, ANY);
	vm.RegisterNamespacedNative(ns, String("caves"), 0, 1, [](Interpreter &vm, Args &args) -> Result<Value, ScriptError> {
		Options o(args.empty() ? Value::Nil() : args[0], "gen.caves");
		generators::CaveConfig c;
		c.width = std::clamp(o.Int("width", c.width), 8, 2000);
		c.height = std::clamp(o.Int("height", c.height), 8, 2000);
		c.fill = float(o.Number("fill", c.fill));
		c.steps = std::clamp(o.Int("steps", c.steps), 0, 50);
		c.birth = o.Int("birth", c.birth);
		c.survival = o.Int("survival", c.survival);
		c.keepLargest = o.Bool("keep_largest", c.keepLargest);
		c.seed = uint64_t(o.Number("seed", 1));
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		return Ok(MakeDungeon(vm, generators::GenerateCaves(c)));
	}, ANY);
	// L-systèmes : gen.lsystem(axiome, règles, itérations[, graine]) ; règles :
	// {F: "FF", X: "F[+X]"} ou [["F", "F[+F]", 1], ["F", "F[-F]", 1]] (stochastiques).
	vm.RegisterNamespacedNative(ns, String("lsystem"), 3, 4, [](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		std::vector<generators::LRule> rules;
		auto symbolOf = [](const String &s) -> Option<char> {
			return s.GetSize() == 1 ? Option<char>(Some(s.CharAt(0))) : Option<char>(NONE);
		};
		if (args[1].IsMap() && args[1].AsMap()) {
			for (const auto &[key, value] : args[1].AsMap()->Snapshot()) {
				Option<char> symbol = symbolOf(key);
				if (symbol.IsNone())
					return Err(Fail(String::Format("`gen.lsystem` : symbole `%s` : un seul caractère attendu", key.CStr())));
				rules.push_back({symbol.Unwrap(), value.ToDisplayString(), 1.f});
			}
		} else if (args[1].IsList() && args[1].AsList()) {
			for (const Value &rule : args[1].AsList()->Snapshot()) {
				if (!rule.IsList() || rule.AsList()->Size() < 2)
					return Err(Fail(String("`gen.lsystem` : règle [symbole, remplacement(, poids)] attendue")));
				const std::vector<Value> r = rule.AsList()->Snapshot();
				Option<char> symbol = symbolOf(r[0].ToDisplayString());
				if (symbol.IsNone())
					return Err(Fail(String("`gen.lsystem` : symbole d'un seul caractère attendu")));
				rules.push_back({symbol.Unwrap(), r[1].ToDisplayString(), r.size() > 2 ? r[2].AsFloat() : 1.f});
			}
		} else {
			return Err(Fail(String("`gen.lsystem` : règles attendues (table ou liste)")));
		}
		const int iterations = std::clamp(int(args[2].AsInt64()), 0, 20);
		const uint64_t seed = args.size() > 3 ? args[3].AsUInt64() : 1;
		return Ok(Value::Str(generators::ExpandLSystem(args[0].ToDisplayString(), rules, iterations, seed)));
	}, ANY);
	// Tortue : gen.turtle(programme, {step, angle, thickness, thinning, step_scale, jitter, seed})
	// → [{a: [x, y, z], b: [x, y, z], thickness, depth}, …]
	vm.RegisterNamespacedNative(ns, String("turtle"), 1, 2, [](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		Options o(args.size() > 1 ? args[1] : Value::Nil(), "gen.turtle");
		generators::TurtleSettings s;
		s.step = float(o.Number("step", s.step));
		s.angleDegrees = float(o.Number("angle", s.angleDegrees));
		s.thickness = float(o.Number("thickness", s.thickness));
		s.thinning = float(o.Number("thinning", s.thinning));
		s.stepScale = float(o.Number("step_scale", s.stepScale));
		s.angleJitter = float(o.Number("jitter", s.angleJitter));
		s.seed = uint64_t(o.Number("seed", 1));
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		auto list = std::make_shared<ListObject>();
		for (const generators::TurtleSegment &seg : generators::InterpretTurtle(args[0].ToDisplayString(), s)) {
			auto segment = std::make_shared<MapObject>();
			segment->SetKey(String("a"), NumberList(std::vector<float>{seg.ax, seg.ay, seg.az}));
			segment->SetKey(String("b"), NumberList(std::vector<float>{seg.bx, seg.by, seg.bz}));
			segment->SetKey(String("thickness"), Value::Number(seg.thickness));
			segment->SetKey(String("depth"), Value::Int(seg.depth));
			list->items.push_back(Value::Map(std::move(segment)));
		}
		return Ok(Value::List(std::move(list)));
	}, ANY);
	// WFC : gen.wfc(tuiles, largeur, hauteur[, {seed, attempts, fixed: [[x, y, indice]]}])
	// tuiles : [{name, sides: [haut, droite, bas, gauche], weight}] → lignes de noms.
	vm.RegisterNamespacedNative(ns, String("wfc"), 3, 4, [](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		if (!args[0].IsList())
			return Err(Fail(String("`gen.wfc` : liste de tuiles attendue")));
		std::vector<generators::WfcTile> tiles;
		for (const Value &item : args[0].AsList()->Snapshot()) {
			Options t(item, "gen.wfc (tuile)");
			generators::WfcTile tile;
			tile.name = t.Text("name", "?");
			tile.weight = float(t.Number("weight", 1.0));
			Option<Value> sides = t.Get("sides");
			if (auto error = t.Finish(); error.IsSome())
				return Err(error.Unwrap());
			if (sides.IsNone() || !sides.Value().IsList() || sides.Value().AsList()->Size() != 4)
				return Err(Fail(String::Format("`gen.wfc` : tuile `%s` : `sides` = [haut, droite, bas, gauche]", tile.name.CStr())));
			const std::vector<Value> s = sides.Value().AsList()->Snapshot();
			for (size_t k = 0; k < 4; ++k)
				tile.sides[k] = s[k].ToDisplayString();
			tiles.push_back(std::move(tile));
		}
		Options o(args.size() > 3 ? args[3] : Value::Nil(), "gen.wfc");
		const uint64_t seed = uint64_t(o.Number("seed", 1));
		const int attempts = std::clamp(o.Int("attempts", 10), 1, 1000);
		Option<Value> fixed = o.Get("fixed");
		if (auto error = o.Finish(); error.IsSome())
			return Err(error.Unwrap());
		generators::WaveFunctionCollapse wfc(tiles);
		if (fixed.IsSome() && fixed.Value().IsList())
			for (const Value &f : fixed.Value().AsList()->Snapshot())
				if (f.IsList() && f.AsList()->Size() == 3)
					wfc.Constrain(int(f.AsList()->At(0).Unwrap().AsInt64()), int(f.AsList()->At(1).Unwrap().AsInt64()),
								  int(f.AsList()->At(2).Unwrap().AsInt64()));
		auto result = wfc.Run(int(args[1].AsInt64()), int(args[2].AsInt64()), seed, attempts);
		if (result.IsError())
			return Err(Fail(result.Error()));
		auto rows = std::make_shared<ListObject>();
		for (int y = 0; y < result.Value().height; ++y) {
			auto row = std::make_shared<ListObject>();
			for (int x = 0; x < result.Value().width; ++x)
				row->items.push_back(Value::Str(tiles[size_t(result.Value().At(x, y))].name));
			rows->items.push_back(Value::List(std::move(row)));
		}
		return Ok(Value::List(std::move(rows)));
	}, ANY);
	vm.RegisterNamespacedNative(ns, String("hash"), 2, 3, [](Interpreter &, Args &args) -> Result<Value, ScriptError> {
		return Ok(Value::Number(generators::HashFloat2(int32_t(args[0].AsInt64()), int32_t(args[1].AsInt64()),
													   args.size() > 2 ? uint32_t(args[2].AsUInt64()) : 0u)));
	}, ANY);
}

} // namespace genlib

void InstallGeneratorLibrary(Interpreter &vm) {
	using namespace genlib;
	// Type des grilles de niveau : `gen.tilemap` (`gen.dungeon` est la fonction
	// qui en crée une — un même nom écraserait le type).
	TypeBuilder noise("gen.noise"), heightmap("gen.heightmap"), dungeon("gen.tilemap"), names("gen.names");
	DefineNoise(noise);
	DefineHeightMap(heightmap);
	DefineDungeon(dungeon);
	DefineNames(names);
	// `gen.names(exemples, ordre[, graine])`
	std::weak_ptr<const HostType> weakNames = names.type;
	names.Construct(1, 3, [weakNames](Interpreter &vm, Args &args, const Args &) -> Result<Value, ScriptError> {
		auto items = lib::ItemsOf(vm, args[0]);
		if (items.IsError())
			return Err(items.Error());
		std::vector<String> examples;
		for (const Value &v : items.Value())
			examples.push_back(v.ToDisplayString().ToLower());
		auto object = std::make_shared<NamesObject>();
		object->type = weakNames.lock();
		object->names = generators::MarkovNames(examples, args.size() > 1 ? int(args[1].AsInt64()) : 3);
		object->rng.Seed(args.size() > 2 ? args[2].AsUInt64() : 1);
		return Ok(Value::Host(std::move(object)));
	});
	vm.RegisterHostType(String("gen"), String("noise"), noise.type);
	vm.RegisterHostType(String("gen"), String("heightmap"), heightmap.type);
	vm.RegisterHostType(String("gen"), String("tilemap"), dungeon.type);
	vm.RegisterHostType(String("gen"), String("names"), names.type);
	InstallFunctions(vm);
}

} // namespace data::script
