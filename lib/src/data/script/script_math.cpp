// Définitions de data/script/script_math.hpp
#include "data/script/script_math.hpp"

namespace data::script {

namespace mathlib {

Value FloatLike(double v, const Value& model) {
	return Value::Float(v, model.GetNumberType());
}

Result<Value, ScriptError> RequireInteger(const Args& args, size_t i, const char* fn) {
	if (i >= args.size() || !args[i].IsInteger())
		return Err(Fail(String::Format("`%s` : argument %d doit être un entier (i8 … u64), trouvé `%s`", fn, int(i + 1),
									   i < args.size() ? args[i].TypeName() : "rien")));
	return Ok(args[i]);
}

uint64_t RawBits(const Value& v) {
	const int bits = BitsOf(v.GetNumberType());
	const uint64_t mask = bits == 64 ? ~uint64_t(0) : (uint64_t(1) << bits) - 1;
	const uint64_t raw = IsSignedType(v.GetNumberType()) ? static_cast<uint64_t>(v.AsInt64()) : v.AsUInt64();
	return raw & mask;
}

Value FromBits(uint64_t raw, const Value& model) {
	return numeric::ConvertWrapping(Value::UInt(raw), model.GetNumberType()).Unwrap();
}

Value FromWideInt(Wide v) {
	if (numeric::Fits(v, NumberType::I64))
		return Value::Int(static_cast<int64_t>(v));
	return Value::UInt(static_cast<uint64_t>(v));
}

Wide Gcd(Wide a, Wide b) {
	if (a < 0)
		a = -a;
	if (b < 0)
		b = -b;
	while (b != 0) {
		const Wide t = a % b;
		a = b;
		b = t;
	}
	return a;
}

void InstallFunctions(Interpreter& vm) {
	const String ns("math");
	constexpr auto ANY = Interpreter::NativeThread::ANY;
	auto unary = [&vm, ns](const char* name, double (*fn)(double)) {
		const String qualified = String::Format("math.%s", name);
		vm.RegisterNamespacedNative(
			ns, String(name), 1, 1,
			[qualified, fn](Interpreter&, Args& args) -> Result<Value, ScriptError> {
				auto x = lib::ArgFloat(args, 0, qualified.CStr());
				if (x.IsError())
					return Err(x.Error());
				return Ok(FloatLike(fn(x.Value()), args[0]));
			},
			ANY);
	};
	unary("sinh", [](double v) { return std::sinh(v); });
	unary("cosh", [](double v) { return std::cosh(v); });
	unary("tanh", [](double v) { return std::tanh(v); });
	unary("asinh", [](double v) { return std::asinh(v); });
	unary("acosh", [](double v) { return std::acosh(v); });
	unary("atanh", [](double v) { return std::atanh(v); });
	unary("exp2", [](double v) { return std::exp2(v); });
	unary("expm1", [](double v) { return std::expm1(v); });
	unary("log2", [](double v) { return std::log2(v); });
	unary("log10", [](double v) { return std::log10(v); });
	unary("log1p", [](double v) { return std::log1p(v); });
	unary("logb", [](double v) { return std::logb(v); });
	unary("cbrt", [](double v) { return std::cbrt(v); });
	unary("trunc", [](double v) { return std::trunc(v); });
	unary("nearbyint", [](double v) { return std::nearbyint(v); });
	unary("erf", [](double v) { return std::erf(v); });
	unary("erfc", [](double v) { return std::erfc(v); });
	unary("tgamma", [](double v) { return std::tgamma(v); });
	unary("lgamma", [](double v) { return std::lgamma(v); });
	unary("fract", [](double v) { return v - std::floor(v); });

	auto binary = [&vm, ns](const char* name, double (*fn)(double, double)) {
		const String qualified = String::Format("math.%s", name);
		vm.RegisterNamespacedNative(
			ns, String(name), 2, 2,
			[qualified, fn](Interpreter&, Args& args) -> Result<Value, ScriptError> {
				auto x = lib::ArgFloat(args, 0, qualified.CStr());
				auto y = lib::ArgFloat(args, 1, qualified.CStr());
				if (x.IsError())
					return Err(x.Error());
				if (y.IsError())
					return Err(y.Error());
				const bool f32 =
					args[0].GetNumberType() == NumberType::F32 && args[1].GetNumberType() != NumberType::F64;
				return Ok(Value::Float(fn(x.Value(), y.Value()), f32 ? NumberType::F32 : NumberType::F64));
			},
			ANY);
	};
	binary("fmod", [](double a, double b) { return std::fmod(a, b); });
	binary("remainder", [](double a, double b) { return std::remainder(a, b); });
	binary("fmin", [](double a, double b) { return std::fmin(a, b); });
	binary("fmax", [](double a, double b) { return std::fmax(a, b); });
	binary("fdim", [](double a, double b) { return std::fdim(a, b); });
	binary("copysign", [](double a, double b) { return std::copysign(a, b); });
	binary("nextafter", [](double a, double b) { return std::nextafter(a, b); });

	auto predicate = [&vm, ns](const char* name, bool (*test)(double)) {
		const String qualified = String::Format("math.%s", name);
		vm.RegisterNamespacedNative(
			ns, String(name), 1, 1,
			[qualified, test](Interpreter&, Args& args) -> Result<Value, ScriptError> {
				auto x = lib::ArgFloat(args, 0, qualified.CStr());
				if (x.IsError())
					return Err(x.Error());
				return Ok(Value::Boolean(test(x.Value())));
			},
			ANY);
	};
	predicate("isnan", [](double v) { return std::isnan(v); });
	predicate("isinf", [](double v) { return std::isinf(v); });
	predicate("isfinite", [](double v) { return std::isfinite(v); });
	predicate("isnormal", [](double v) { return std::isnormal(v); });
	predicate("signbit", [](double v) { return std::signbit(v); });

	// hypot(x, y[, z]), fma(x, y, z), ldexp(x, e), frexp(x) -> [m, e], modf(x) -> [entier, fraction]
	vm.RegisterNamespacedNative(
		ns, String("hypot"), 2, 3,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			double v[3] = {0, 0, 0};
			for (size_t i = 0; i < args.size(); ++i) {
				auto x = lib::ArgFloat(args, i, "math.hypot");
				if (x.IsError())
					return Err(x.Error());
				v[i] = x.Value();
			}
			return Ok(Value::Number(args.size() == 3 ? std::hypot(v[0], v[1], v[2]) : std::hypot(v[0], v[1])));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("fma"), 3, 3,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			double v[3];
			for (size_t i = 0; i < 3; ++i) {
				auto x = lib::ArgFloat(args, i, "math.fma");
				if (x.IsError())
					return Err(x.Error());
				v[i] = x.Value();
			}
			return Ok(Value::Number(std::fma(v[0], v[1], v[2])));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("ldexp"), 2, 2,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			auto x = lib::ArgFloat(args, 0, "math.ldexp");
			auto e = lib::ArgInt(args, 1, "math.ldexp");
			if (x.IsError())
				return Err(x.Error());
			if (e.IsError())
				return Err(e.Error());
			return Ok(Value::Number(std::ldexp(x.Value(), int(std::clamp<int64_t>(e.Value(), -100000, 100000)))));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("frexp"), 1, 1,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			auto x = lib::ArgFloat(args, 0, "math.frexp");
			if (x.IsError())
				return Err(x.Error());
			int exponent = 0;
			const double mantissa = std::frexp(x.Value(), &exponent);
			return Ok(lib::Pair(Value::Number(mantissa), Value::Int(exponent)));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("modf"), 1, 1,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			auto x = lib::ArgFloat(args, 0, "math.modf");
			if (x.IsError())
				return Err(x.Error());
			double integral = 0.0;
			const double fraction = std::modf(x.Value(), &integral);
			return Ok(lib::Pair(Value::Number(integral), Value::Number(fraction)));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("ilogb"), 1, 1,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			auto x = lib::ArgFloat(args, 0, "math.ilogb");
			if (x.IsError())
				return Err(x.Error());
			return Ok(Value::Int(std::ilogb(x.Value())));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("smoothstep"), 3, 3,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			double v[3];
			for (size_t i = 0; i < 3; ++i) {
				auto x = lib::ArgFloat(args, i, "math.smoothstep");
				if (x.IsError())
					return Err(x.Error());
				v[i] = x.Value();
			}
			const double t = v[1] == v[0] ? 0.0 : std::clamp((v[2] - v[0]) / (v[1] - v[0]), 0.0, 1.0);
			return Ok(Value::Number(t * t * (3.0 - 2.0 * t)));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("midpoint"), 2, 2,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			if (args[0].IsInteger() && args[1].IsInteger()) {
				const Wide a = numeric::ToWide(args[0]), b = numeric::ToWide(args[1]);
				return Ok(FromWideInt(a + (b - a) / 2)); // arrondi vers `a`, comme std::midpoint
			}
			auto a = lib::ArgFloat(args, 0, "math.midpoint");
			auto b = lib::ArgFloat(args, 1, "math.midpoint");
			if (a.IsError())
				return Err(a.Error());
			if (b.IsError())
				return Err(b.Error());
			return Ok(Value::Number(std::midpoint(a.Value(), b.Value())));
		},
		ANY);

	// ── Arithmétique entière (<numeric>) ──
	auto gcdLike = [&vm, ns](const char* name, bool lcm) {
		vm.RegisterNamespacedNative(
			ns, String(name), 2, 2,
			[name, lcm](Interpreter&, Args& args) -> Result<Value, ScriptError> {
				auto a = RequireInteger(args, 0, name);
				auto b = RequireInteger(args, 1, name);
				if (a.IsError())
					return a;
				if (b.IsError())
					return b;
				const Wide x = numeric::ToWide(a.Value()), y = numeric::ToWide(b.Value());
				const Wide g = Gcd(x, y);
				if (!lcm)
					return Ok(FromWideInt(g));
				if (g == 0)
					return Ok(Value::Int(0));
				const Wide l = (x < 0 ? -x : x) / g * (y < 0 ? -y : y);
				if (!numeric::Fits(l, NumberType::U64))
					return Err(Fail(String::Format("`%s` : dépassement de capacité", name)));
				return Ok(FromWideInt(l));
			},
			ANY);
	};
	gcdLike("gcd", false);
	gcdLike("lcm", true);
	vm.RegisterNamespacedNative(
		ns, String("factorial"), 1, 1,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			auto n = lib::ArgInt(args, 0, "math.factorial");
			if (n.IsError())
				return Err(n.Error());
			if (n.Value() < 0 || n.Value() > 20)
				return Err(Fail(String::Format("`math.factorial(%lld)` : défini de 0 à 20 sur u64 (au-delà : "
											   "math.tgamma(n + 1))",
											   static_cast<long long>(n.Value()))));
			uint64_t out = 1;
			for (int64_t i = 2; i <= n.Value(); ++i)
				out *= uint64_t(i);
			return Ok(Value::UInt(out));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("binomial"), 2, 2,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			auto n = lib::ArgInt(args, 0, "math.binomial");
			auto k = lib::ArgInt(args, 1, "math.binomial");
			if (n.IsError())
				return Err(n.Error());
			if (k.IsError())
				return Err(k.Error());
			if (n.Value() < 0 || k.Value() < 0 || k.Value() > n.Value())
				return Ok(Value::UInt(0));
			const int64_t kk = std::min(k.Value(), n.Value() - k.Value());
			Wide out = 1;
			for (int64_t i = 1; i <= kk; ++i) {
				out = out * Wide(n.Value() - kk + i) / Wide(i);
				if (out > Wide(UINT64_MAX))
					return Err(Fail(String("`math.binomial` : dépassement de u64")));
			}
			return Ok(Value::UInt(static_cast<uint64_t>(out)));
		},
		ANY);
	vm.RegisterNamespacedNative(
		ns, String("is_prime"), 1, 1,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			auto n = RequireInteger(args, 0, "math.is_prime");
			if (n.IsError())
				return n;
			const Wide v = numeric::ToWide(n.Value());
			if (v < 2)
				return Ok(Value::Boolean(false));
			for (Wide d = 2; d * d <= v; ++d)
				if (v % d == 0)
					return Ok(Value::Boolean(false));
			return Ok(Value::Boolean(true));
		},
		ANY);
	// Somme, produit (typés, dépassement vérifié) et moyenne d'une suite.
	auto fold = [&vm, ns](const char* name, numeric::Op op, int64_t start) {
		vm.RegisterNamespacedNative(
			ns, String(name), 1, 1,
			[name, op, start](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
				auto items = lib::ItemsOf(vm, args[0]);
				if (items.IsError())
					return Err(items.Error());
				Value total = Value::Int(start);
				for (const Value& item : items.Value()) {
					if (!item.IsNumber())
						return Err(Fail(String::Format("`%s` : élément `%s` non numérique", name, item.TypeName())));
					auto next = numeric::Apply(op, total, item);
					if (next.IsError())
						return Err(Fail(String::Format("`%s` : %s", name, next.Error().CStr())));
					total = next.Unwrap();
				}
				return Ok(total);
			},
			ANY);
	};
	fold("sum", numeric::Op::ADD, 0);
	fold("product", numeric::Op::MULTIPLY, 1);
	vm.RegisterNamespacedNative(
		ns, String("mean"), 1, 1,
		[](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto items = lib::ItemsOf(vm, args[0]);
			if (items.IsError())
				return Err(items.Error());
			if (items.Value().empty())
				return Ok(Value::Nil());
			double total = 0.0;
			for (const Value& item : items.Value()) {
				if (!item.IsNumber())
					return Err(Fail(String::Format("`math.mean` : élément `%s` non numérique", item.TypeName())));
				total += item.AsNumber();
			}
			return Ok(Value::Number(total / double(items.Value().size())));
		},
		ANY);

	// ── Manipulation de bits (<bit>) : sur la largeur du type de l'entier ──
	auto bitCount = [&vm, ns](const char* name, int (*count)(uint64_t, int)) {
		vm.RegisterNamespacedNative(
			ns, String(name), 1, 1,
			[name, count](Interpreter&, Args& args) -> Result<Value, ScriptError> {
				auto v = RequireInteger(args, 0, name);
				if (v.IsError())
					return v;
				return Ok(Value::Int(count(RawBits(v.Value()), BitsOf(v.Value().GetNumberType()))));
			},
			ANY);
	};
	bitCount("popcount", [](uint64_t raw, int) { return std::popcount(raw); });
	bitCount("countl_zero", [](uint64_t raw, int bits) { return std::countl_zero(raw) - (64 - bits); });
	bitCount("countr_zero", [](uint64_t raw, int bits) { return raw == 0 ? bits : std::countr_zero(raw); });
	bitCount("bit_width", [](uint64_t raw, int) { return int(std::bit_width(raw)); });
	vm.RegisterNamespacedNative(
		ns, String("has_single_bit"), 1, 1,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			auto v = RequireInteger(args, 0, "math.has_single_bit");
			if (v.IsError())
				return v;
			return Ok(Value::Boolean(std::has_single_bit(RawBits(v.Value()))));
		},
		ANY);
	auto rotate = [&vm, ns](const char* name, bool left) {
		vm.RegisterNamespacedNative(
			ns, String(name), 2, 2,
			[name, left](Interpreter&, Args& args) -> Result<Value, ScriptError> {
				auto v = RequireInteger(args, 0, name);
				auto s = lib::ArgInt(args, 1, name);
				if (v.IsError())
					return v;
				if (s.IsError())
					return Err(s.Error());
				const int bits = BitsOf(v.Value().GetNumberType());
				const uint64_t raw = RawBits(v.Value());
				const int shift = int(((s.Value() % bits) + bits) % bits);
				const int by = left ? shift : (bits - shift) % bits;
				const uint64_t mask = bits == 64 ? ~uint64_t(0) : (uint64_t(1) << bits) - 1;
				const uint64_t out = by == 0 ? raw : ((raw << by) | (raw >> (bits - by))) & mask;
				return Ok(FromBits(out, v.Value()));
			},
			ANY);
	};
	rotate("rotl", true);
	rotate("rotr", false);
	vm.RegisterNamespacedNative(
		ns, String("byteswap"), 1, 1,
		[](Interpreter&, Args& args) -> Result<Value, ScriptError> {
			auto v = RequireInteger(args, 0, "math.byteswap");
			if (v.IsError())
				return v;
			const int bytes = BitsOf(v.Value().GetNumberType()) / 8;
			const uint64_t raw = RawBits(v.Value());
			uint64_t out = 0;
			for (int i = 0; i < bytes; ++i)
				out |= ((raw >> (8 * i)) & 0xFF) << (8 * (bytes - 1 - i));
			return Ok(FromBits(out, v.Value()));
		},
		ANY);
	auto bitRound = [&vm, ns](const char* name, bool ceil) {
		vm.RegisterNamespacedNative(
			ns, String(name), 1, 1,
			[name, ceil](Interpreter&, Args& args) -> Result<Value, ScriptError> {
				auto v = RequireInteger(args, 0, name);
				if (v.IsError())
					return v;
				const uint64_t raw = RawBits(v.Value());
				if (ceil && raw > (uint64_t(1) << 63))
					return Err(Fail(String::Format("`%s` : dépassement", name)));
				const uint64_t out = ceil ? std::bit_ceil(raw) : std::bit_floor(raw);
				auto converted = numeric::ConvertExact(Value::UInt(out), v.Value().GetNumberType());
				if (converted.IsError())
					return Err(Fail(String::Format("`%s` : %s", name, converted.Error().CStr())));
				return Ok(converted.Unwrap());
			},
			ANY);
	};
	bitRound("bit_ceil", true);
	bitRound("bit_floor", false);

	// ── Fonctions spéciales (C++17) — arguments vérifiés AVANT l'appel :
	// hors domaine, la bibliothèque C++ lèverait std::domain_error. ──
	auto special = [&vm, ns](const char* name, int arity, bool (*valid)(const double*), double (*fn)(const double*)) {
		vm.RegisterNamespacedNative(
			ns, String(name), arity, arity,
			[name, arity, valid, fn](Interpreter&, Args& args) -> Result<Value, ScriptError> {
				double v[3] = {0, 0, 0};
				for (int i = 0; i < arity; ++i) {
					auto x = lib::ArgFloat(args, size_t(i), name);
					if (x.IsError())
						return Err(x.Error());
					v[i] = x.Value();
					if (std::isnan(v[i]))
						return Ok(Value::Number(std::nan("")));
				}
				if (!valid(v))
					return Err(Fail(String::Format("`math.%s` : argument hors du domaine", name)));
				return Ok(Value::Number(fn(v)));
			},
			ANY);
	};
	special(
		"beta", 2, [](const double* v) { return v[0] > 0.0 && v[1] > 0.0; },
		[](const double* v) { return std::beta(v[0], v[1]); });
	special(
		"riemann_zeta", 1, [](const double* v) { return v[0] != 1.0; },
		[](const double* v) { return std::riemann_zeta(v[0]); });
	special(
		"expint", 1, [](const double* v) { return v[0] != 0.0; }, [](const double* v) { return std::expint(v[0]); });
	special(
		"legendre", 2,
		[](const double* v) { return v[0] >= 0.0 && v[0] == std::floor(v[0]) && v[0] < 1e4 && std::fabs(v[1]) <= 1.0; },
		[](const double* v) { return std::legendre(unsigned(v[0]), v[1]); });
	special(
		"hermite", 2, [](const double* v) { return v[0] >= 0.0 && v[0] == std::floor(v[0]) && v[0] < 1e4; },
		[](const double* v) { return std::hermite(unsigned(v[0]), v[1]); });
	special(
		"laguerre", 2,
		[](const double* v) { return v[0] >= 0.0 && v[0] == std::floor(v[0]) && v[0] < 1e4 && v[1] >= 0.0; },
		[](const double* v) { return std::laguerre(unsigned(v[0]), v[1]); });
	special(
		"cyl_bessel_j", 2, [](const double* v) { return v[0] >= 0.0 && v[1] >= 0.0; },
		[](const double* v) { return std::cyl_bessel_j(v[0], v[1]); });
	special(
		"sph_bessel", 2,
		[](const double* v) { return v[0] >= 0.0 && v[0] == std::floor(v[0]) && v[0] < 1e4 && v[1] >= 0.0; },
		[](const double* v) { return std::sph_bessel(unsigned(v[0]), v[1]); });

	// ── Constantes supplémentaires et bornes des types ──
	const std::pair<const char*, double> constants[] = {
		{"log2e", 1.44269504088896340736},		{"log10e", 0.43429448190325182765},
		{"inv_sqrtpi", 0.56418958354775628695}, {"inv_sqrt2", 0.70710678118654752440},
		{"egamma", 0.57721566490153286061},		{"nan", std::nan("")},
		{"epsilon", 2.220446049250313e-16},
	};
	for (const auto& [name, value] : constants)
		vm.RegisterNamespaceConstant(ns, String(name), Value::Number(value));
	// math.max_i8 … math.max_u64, math.min_i8 …, math.max_f32, math.max_f64…
	for (int i = 0; i <= static_cast<int>(NumberType::U64); ++i) {
		const NumberType t = static_cast<NumberType>(i);
		vm.RegisterNamespaceConstant(ns, String::Format("min_%s", NumberTypeName(t)),
									 numeric::FromWide(numeric::LowOf(t), t));
		vm.RegisterNamespaceConstant(ns, String::Format("max_%s", NumberTypeName(t)),
									 numeric::FromWide(numeric::HighOf(t), t));
	}
	vm.RegisterNamespaceConstant(ns, String("max_f32"), Value::Float(3.4028234663852886e38, NumberType::F32));
	vm.RegisterNamespaceConstant(ns, String("min_f32"), Value::Float(-3.4028234663852886e38, NumberType::F32));
	vm.RegisterNamespaceConstant(ns, String("max_f64"), Value::Number(1.7976931348623157e308));
	vm.RegisterNamespaceConstant(ns, String("min_f64"), Value::Number(-1.7976931348623157e308));
}

} // namespace mathlib

namespace mathlib {

// ── VecObject ────────────────────────────────────────────────────────────────

void VecObject::Get(float out[4]) const {
std::lock_guard<std::mutex> lock(mutex);
std::copy(c, c + 4, out);
}

MathTypes TypesOf(Interpreter& vm) {
	MathTypes t;
	t.vec[2] = lib::HostTypeOf(vm, "math", "vec2");
	t.vec[3] = lib::HostTypeOf(vm, "math", "vec3");
	t.vec[4] = lib::HostTypeOf(vm, "math", "vec4");
	t.mat4 = lib::HostTypeOf(vm, "math", "mat4");
	t.quat = lib::HostTypeOf(vm, "math", "quat");
	t.aabb = lib::HostTypeOf(vm, "math", "aabb");
	t.plane = lib::HostTypeOf(vm, "math", "plane");
	t.ray = lib::HostTypeOf(vm, "math", "ray");
	t.complex = lib::HostTypeOf(vm, "math", "complex");
	return t;
}

Value MakeVec(Interpreter& vm, int size, const float* c) {
	auto object = std::make_shared<VecObject>();
	object->type = TypesOf(vm).vec[size];
	object->size = size;
	std::copy(c, c + size, object->c);
	return Value::Host(std::move(object));
}

Value MakeVec3(Interpreter& vm, const math::FVector3& v) {
	const float c[3] = {v.x, v.y, v.z};
	return MakeVec(vm, 3, c);
}

Value MakeVec4(Interpreter& vm, const math::FVector4& v) {
	const float c[4] = {v.x, v.y, v.z, v.w};
	return MakeVec(vm, 4, c);
}

const VecObject* AsVec(const Value& v, int size) {
	if (!v.IsHost() || !v.AsHost())
		return nullptr;
	const auto* vec = dynamic_cast<const VecObject*>(v.AsHost().get());
	return vec && (size == 0 || vec->size == size) ? vec : nullptr;
}

Result<std::array<float, 4>, ScriptError> Components(const Value& v, int size, const char* fn) {
	std::array<float, 4> out{0, 0, 0, 0};
	if (const VecObject* vec = AsVec(v)) {
		if (vec->size != size)
			return Err(Fail(String::Format("`%s` : vec%d attendu, reçu vec%d", fn, size, vec->size)));
		vec->Get(out.data());
		return Ok(out);
	}
	if (v.IsList() && v.AsList() && int(v.AsList()->Size()) == size) {
		const std::vector<Value> items = v.AsList()->Snapshot();
		for (int i = 0; i < size; ++i) {
			if (!items[size_t(i)].IsNumber())
				return Err(Fail(String::Format("`%s` : composante %d non numérique", fn, i)));
			out[size_t(i)] = items[size_t(i)].AsFloat();
		}
		return Ok(out);
	}
	if (v.IsMap() && v.AsMap()) {
		static const char* NAMES[] = {"x", "y", "z", "w"};
		for (int i = 0; i < size; ++i) {
			Option<Value> item = v.AsMap()->Get(String(NAMES[i]));
			if (item.IsNone() || !item.Value().IsNumber())
				return Err(Fail(String::Format("`%s` : table sans composante numérique `%s`", fn, NAMES[i])));
			out[size_t(i)] = item.Value().AsFloat();
		}
		return Ok(out);
	}
	return Err(Fail(
		String::Format("`%s` : vec%d (ou liste de %d nombres) attendu, trouvé `%s`", fn, size, size, v.TypeName())));
}

Result<math::FVector3, ScriptError> Vec3Of(const Value& v, const char* fn) {
	auto c = Components(v, 3, fn);
	if (c.IsError())
		return Err(c.Error());
	return Ok(math::FVector3{c.Value()[0], c.Value()[1], c.Value()[2]});
}

void DefineVector(TypeBuilder& builder, int size) {
	static const char* NAMES[] = {"x", "y", "z", "w"};
	const String typeName = builder.type->name;
	std::weak_ptr<const HostType> weakType = builder.type;
	auto make = [size](const std::shared_ptr<const HostType>& type, const float* c) {
		auto object = std::make_shared<VecObject>();
		object->type = type;
		object->size = size;
		std::copy(c, c + size, object->c);
		return Value::Host(std::move(object));
	};
	// `vec3()`, `vec3(s)`, `vec3(x, y, z)`, `vec3([x, y, z])`, `vec3({x, y, z})`
	builder.Construct(
		0, size, [weakType, size, make, typeName](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
			float c[4] = {0, 0, 0, 0};
			if (args.size() == 1 && !args[0].IsNumber()) {
				auto parts = Components(args[0], size, typeName.CStr());
				if (parts.IsError())
					return Err(parts.Error());
				std::copy(parts.Value().begin(), parts.Value().begin() + size, c);
			} else if (args.size() == 1) {
				std::fill(c, c + size, args[0].AsFloat());
			} else if (!args.empty()) {
				if (int(args.size()) != size)
					return Err(Fail(String::Format("`%s` : 0, 1 ou %d composantes attendues", typeName.CStr(), size)));
				for (int i = 0; i < size; ++i) {
					auto x = lib::ArgFloat(args, size_t(i), typeName.CStr());
					if (x.IsError())
						return Err(x.Error());
					c[i] = float(x.Value());
				}
			}
			return Ok(make(weakType.lock(), c));
		});
	auto constant = [weakType, make](int axis, float fill) {
		return [weakType, make, axis, fill](Interpreter&, Args&) -> Result<Value, ScriptError> {
			float c[4] = {fill, fill, fill, fill};
			if (axis >= 0) {
				std::fill(c, c + 4, 0.f);
				c[axis] = 1.f;
			}
			return Ok(make(weakType.lock(), c));
		};
	};
	builder.StaticFn("zero", 0, 0, constant(-1, 0.f));
	builder.StaticFn("one", 0, 0, constant(-1, 1.f));
	for (int axis = 0; axis < size; ++axis)
		builder.StaticFn(String::Format("unit_%s", NAMES[axis]).CStr(), 0, 0, constant(axis, 0.f));

	auto read = [](const HostRef& self, float out[4]) { As<VecObject>(self).Get(out); };
	auto withOther = [size, typeName](const char* method,
									  std::function<Value(Interpreter&, const float*, const float*, int)> fn) {
		const String name = String::Format("%s.%s", typeName.CStr(), method);
		return [size, name, fn](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			auto other = Components(args[0], size, name.CStr());
			if (other.IsError())
				return Err(other.Error());
			float c[4];
			As<VecObject>(self).Get(c);
			return Ok(fn(vm, c, other.Value().data(), size));
		};
	};
	auto dot = [](const float* a, const float* b, int n) {
		double s = 0;
		for (int i = 0; i < n; ++i)
			s += double(a[i]) * double(b[i]);
		return s;
	};
	builder.Method("dot", 1, 1, withOther("dot", [dot](Interpreter&, const float* a, const float* b, int n) {
					   return Value::Float(dot(a, b, n), NumberType::F32);
				   }));
	builder.Method("length_sq", 0, 0,
				   [dot, size, read](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
					   float c[4];
					   read(self, c);
					   return Ok(Value::Float(dot(c, c, size), NumberType::F32));
				   });
	builder.Method("length", 0, 0,
				   [dot, size, read](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
					   float c[4];
					   read(self, c);
					   return Ok(Value::Float(std::sqrt(dot(c, c, size)), NumberType::F32));
				   });
	builder.Method("normalize", 0, 0,
				   [dot, size, read](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
					   float c[4];
					   read(self, c);
					   const double len = std::sqrt(dot(c, c, size));
					   float out[4] = {0, 0, 0, 0};
					   if (len > 1e-8)
						   for (int i = 0; i < size; ++i)
							   out[i] = float(c[i] / len);
					   return Ok(MakeVec(vm, size, out));
				   });
	builder.Method("distance", 1, 1, withOther("distance", [](Interpreter&, const float* a, const float* b, int n) {
					   double s = 0;
					   for (int i = 0; i < n; ++i)
						   s += double(a[i] - b[i]) * double(a[i] - b[i]);
					   return Value::Float(std::sqrt(s), NumberType::F32);
				   }));
	builder.Method("distance_sq", 1, 1,
				   withOther("distance_sq", [](Interpreter&, const float* a, const float* b, int n) {
					   double s = 0;
					   for (int i = 0; i < n; ++i)
						   s += double(a[i] - b[i]) * double(a[i] - b[i]);
					   return Value::Float(s, NumberType::F32);
				   }));
	builder.Method("angle", 1, 1, withOther("angle", [dot](Interpreter&, const float* a, const float* b, int n) {
					   const double la = std::sqrt(dot(a, a, n)), lb = std::sqrt(dot(b, b, n));
					   if (la < 1e-12 || lb < 1e-12)
						   return Value::Float(0.0, NumberType::F32);
					   return Value::Float(std::acos(std::clamp(dot(a, b, n) / (la * lb), -1.0, 1.0)), NumberType::F32);
				   }));
	auto componentwise = [](float (*op)(float, float)) {
		return [op](Interpreter& vm, const float* a, const float* b, int n) {
			float out[4] = {0, 0, 0, 0};
			for (int i = 0; i < n; ++i)
				out[i] = op(a[i], b[i]);
			return MakeVec(vm, n, out);
		};
	};
	builder.Method("min", 1, 1, withOther("min", componentwise([](float a, float b) { return std::min(a, b); })));
	builder.Method("max", 1, 1, withOther("max", componentwise([](float a, float b) { return std::max(a, b); })));
	builder.Method("lerp", 2, 2,
				   [size, typeName](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					   auto other = Components(args[0], size, typeName.CStr());
					   if (other.IsError())
						   return Err(other.Error());
					   auto t = lib::ArgFloat(args, 1, "lerp");
					   if (t.IsError())
						   return Err(t.Error());
					   float c[4], out[4] = {0, 0, 0, 0};
					   As<VecObject>(self).Get(c);
					   for (int i = 0; i < size; ++i)
						   out[i] = float(c[i] + (other.Value()[size_t(i)] - c[i]) * t.Value());
					   return Ok(MakeVec(vm, size, out));
				   });
	builder.Method("abs", 0, 0, [size](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		float c[4];
		As<VecObject>(self).Get(c);
		for (int i = 0; i < size; ++i)
			c[i] = std::fabs(c[i]);
		return Ok(MakeVec(vm, size, c));
	});
	builder.Method("copy", 0, 0, [size](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		float c[4];
		As<VecObject>(self).Get(c);
		return Ok(MakeVec(vm, size, c));
	});
	builder.Method("to_list", 0, 0, [size](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		float c[4];
		As<VecObject>(self).Get(c);
		auto list = std::make_shared<ListObject>();
		for (int i = 0; i < size; ++i)
			list->items.push_back(Value::Float(c[i], NumberType::F32));
		return Ok(Value::List(std::move(list)));
	});
	if (size == 3) {
		builder.Method("cross", 1, 1, withOther("cross", [](Interpreter& vm, const float* a, const float* b, int) {
						   return MakeVec3(vm,
										   math::FVector3{a[0], a[1], a[2]}.Cross(math::FVector3{b[0], b[1], b[2]}));
					   }));
		builder.Method("reflect", 1, 1, withOther("reflect", [](Interpreter& vm, const float* a, const float* b, int) {
						   return MakeVec3(vm,
										   math::FVector3{a[0], a[1], a[2]}.Reflect(math::FVector3{b[0], b[1], b[2]}));
					   }));
	}
	if (size >= 3)
		builder.Method("xy", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			float c[4];
			As<VecObject>(self).Get(c);
			return Ok(MakeVec(vm, 2, c));
		});
	if (size == 4)
		builder.Method("xyz", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			float c[4];
			As<VecObject>(self).Get(c);
			return Ok(MakeVec(vm, 3, c));
		});

	HostType& type = *builder.type;
	type.get = [size](const HostObject& self, const String& name) -> Option<Value> {
		for (int i = 0; i < size; ++i)
			if (name == NAMES[i]) {
				float c[4];
				As<VecObject>(self).Get(c);
				return Some(Value::Float(c[i], NumberType::F32));
			}
		return NONE;
	};
	type.set = [size](Interpreter&, const HostRef& self, const String& name,
					  const Value& value) -> Result<bool, ScriptError> {
		for (int i = 0; i < size; ++i)
			if (name == NAMES[i]) {
				if (!value.IsNumber())
					return Err(Fail(
						String::Format("composante `%s` : nombre attendu, trouvé `%s`", NAMES[i], value.TypeName())));
				VecObject& o = As<VecObject>(self);
				std::lock_guard<std::mutex> lock(o.mutex);
				o.c[i] = value.AsFloat();
				return Ok(true);
			}
		return Ok(false);
	};
	type.index = [size](Interpreter&, const HostRef& self, const Value& key) -> Result<Value, ScriptError> {
		Args args{key};
		auto raw = lib::ArgInt(args, 0, "vec[]");
		if (raw.IsError())
			return Err(raw.Error());
		auto index = lib::ToIndex(raw.Value(), size_t(size), "vec[]");
		if (index.IsError())
			return Err(index.Error());
		float c[4];
		As<VecObject>(self).Get(c);
		return Ok(Value::Float(c[index.Value()], NumberType::F32));
	};
	type.setIndex = [size](Interpreter&, const HostRef& self, const Value& key, Value value) -> Option<ScriptError> {
		Args args{key};
		auto raw = lib::ArgInt(args, 0, "vec[]");
		if (raw.IsError())
			return Some(raw.Error());
		auto index = lib::ToIndex(raw.Value(), size_t(size), "vec[]");
		if (index.IsError())
			return Some(index.Error());
		if (!value.IsNumber())
			return Some(Fail(String("vec[] : nombre attendu")));
		VecObject& o = As<VecObject>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		o.c[index.Value()] = value.AsFloat();
		return NONE;
	};
	type.items = [size](const HostObject& self) {
		float c[4];
		As<VecObject>(self).Get(c);
		std::vector<Value> out;
		for (int i = 0; i < size; ++i)
			out.push_back(Value::Float(c[i], NumberType::F32));
		return out;
	};
	type.size = [size](const HostObject&) { return size_t(size); };
	type.display = [size](const HostObject& self) {
		float c[4];
		As<VecObject>(self).Get(c);
		String out = String::Format("vec%d(", size);
		for (int i = 0; i < size; ++i) {
			if (i > 0)
				out.Concat(", ");
			out.Concat(Value::NumberToString(double(c[i])));
		}
		out.Concat(")");
		return out;
	};
	type.equals = [size](const HostObject& a, const HostObject& b) {
		float x[4], y[4];
		As<VecObject>(a).Get(x);
		As<VecObject>(b).Get(y);
		return std::equal(x, x + size, y);
	};
	type.negate = [size](Interpreter& vm, const HostRef& self) -> Option<Result<Value, ScriptError>> {
		float c[4];
		As<VecObject>(self).Get(c);
		for (int i = 0; i < size; ++i)
			c[i] = -c[i];
		return Some(Result<Value, ScriptError>(Ok(MakeVec(vm, size, c))));
	};
	// vec ± vec, vec * / vec (composante par composante), vec * / nombre, nombre * vec.
	type.binary = [size](Interpreter& vm, BinaryOp op, const Value& a,
						 const Value& b) -> Option<Result<Value, ScriptError>> {
		using R = Result<Value, ScriptError>;
		const VecObject *va = AsVec(a, size), *vb = AsVec(b, size);
		float x[4] = {0, 0, 0, 0}, y[4] = {0, 0, 0, 0}, out[4] = {0, 0, 0, 0};
		if (va && vb) {
			va->Get(x);
			vb->Get(y);
			for (int i = 0; i < size; ++i) {
				switch (op) {
					case BinaryOp::ADD:
						out[i] = x[i] + y[i];
						break;
					case BinaryOp::SUBTRACT:
						out[i] = x[i] - y[i];
						break;
					case BinaryOp::MULTIPLY:
						out[i] = x[i] * y[i];
						break;
					case BinaryOp::DIVIDE:
						out[i] = x[i] / y[i];
						break;
					case BinaryOp::EQUAL:
						return Some(R(Ok(Value::Boolean(std::equal(x, x + size, y)))));
					case BinaryOp::NOT_EQUAL:
						return Some(R(Ok(Value::Boolean(!std::equal(x, x + size, y)))));
					default:
						return NONE;
				}
			}
			return Some(R(Ok(MakeVec(vm, size, out))));
		}
		if (va && b.IsNumber() && (op == BinaryOp::MULTIPLY || op == BinaryOp::DIVIDE)) {
			va->Get(x);
			const float s = b.AsFloat();
			for (int i = 0; i < size; ++i)
				out[i] = op == BinaryOp::MULTIPLY ? x[i] * s : x[i] / s;
			return Some(R(Ok(MakeVec(vm, size, out))));
		}
		if (vb && a.IsNumber() && op == BinaryOp::MULTIPLY) {
			vb->Get(y);
			for (int i = 0; i < size; ++i)
				out[i] = y[i] * a.AsFloat();
			return Some(R(Ok(MakeVec(vm, size, out))));
		}
		return NONE;
	};
}

// ── MatObject ────────────────────────────────────────────────────────────────

math::FMatrix4 MatObject::Get() const {
std::lock_guard<std::mutex> lock(mutex);
return m;
}

// ── QuatObject ───────────────────────────────────────────────────────────────

math::FQuaternion QuatObject::Get() const {
std::lock_guard<std::mutex> lock(mutex);
return q;
}

Value MakeMat(Interpreter& vm, const math::FMatrix4& m) {
	auto object = std::make_shared<MatObject>();
	object->type = TypesOf(vm).mat4;
	object->m = m;
	return Value::Host(std::move(object));
}

Value MakeQuat(Interpreter& vm, const math::FQuaternion& q) {
	auto object = std::make_shared<QuatObject>();
	object->type = TypesOf(vm).quat;
	object->q = q;
	return Value::Host(std::move(object));
}

const MatObject* AsMat(const Value& v) {
	return v.IsHost() && v.AsHost() ? dynamic_cast<const MatObject*>(v.AsHost().get()) : nullptr;
}

const QuatObject* AsQuat(const Value& v) {
	return v.IsHost() && v.AsHost() ? dynamic_cast<const QuatObject*>(v.AsHost().get()) : nullptr;
}

Result<math::FMatrix4, ScriptError> MatOf(const Value& v, const char* fn) {
	if (const MatObject* m = AsMat(v))
		return Ok(m->Get());
	return Err(Fail(String::Format("`%s` : math.mat4 attendu, trouvé `%s`", fn, v.TypeName())));
}

Result<math::FQuaternion, ScriptError> QuatOf(const Value& v, const char* fn) {
	if (const QuatObject* q = AsQuat(v))
		return Ok(q->Get());
	return Err(Fail(String::Format("`%s` : math.quat attendu, trouvé `%s`", fn, v.TypeName())));
}

Result<math::FVector3, ScriptError> Vec3Args(const Args& args, const char* fn) {
	if (args.size() == 1 && args[0].IsNumber()) {
		const float s = args[0].AsFloat();
		return Ok(math::FVector3{s, s, s});
	}
	if (args.size() == 1)
		return Vec3Of(args[0], fn);
	if (args.size() != 3)
		return Err(Fail(String::Format("`%s` : (x, y, z) ou un vec3 attendu", fn)));
	float c[3];
	for (size_t i = 0; i < 3; ++i) {
		auto x = lib::ArgFloat(args, i, fn);
		if (x.IsError())
			return Err(x.Error());
		c[i] = float(x.Value());
	}
	return Ok(math::FVector3{c[0], c[1], c[2]});
}

void DefineMat4(TypeBuilder& builder) {
	std::weak_ptr<const HostType> weakType = builder.type;
	builder.Construct(0, 1, [weakType](Interpreter& vm, Args& args, const Args&) -> Result<Value, ScriptError> {
		math::FMatrix4 m = math::FMatrix4::Identity();
		if (!args.empty()) {
			auto items = lib::ItemsOf(vm, args[0]);
			if (items.IsError())
				return Err(items.Error());
			if (items.Value().size() != 16)
				return Err(Fail(String("`math.mat4(liste)` : 16 nombres attendus (colonnes d'abord)")));
			for (size_t i = 0; i < 16; ++i)
				m.m[i] = items.Value()[i].AsFloat();
		}
		auto object = std::make_shared<MatObject>();
		object->type = weakType.lock();
		object->m = m;
		return Ok(Value::Host(std::move(object)));
	});
	builder.StaticFn("identity", 0, 0, [](Interpreter& vm, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeMat(vm, math::FMatrix4::Identity()));
	});
	builder.StaticFn("translate", 1, 3, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto t = Vec3Args(args, "math.mat4.translate");
		if (t.IsError())
			return Err(t.Error());
		return Ok(MakeMat(vm, math::FMatrix4::Translate(t.Value())));
	});
	builder.StaticFn("scale", 1, 3, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto s = Vec3Args(args, "math.mat4.scale");
		if (s.IsError())
			return Err(s.Error());
		return Ok(MakeMat(vm, math::FMatrix4::Scale(s.Value())));
	});
	auto rotation = [](math::FMatrix4 (*make)(float), const char* name) {
		return [make, name](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
			auto a = lib::ArgFloat(args, 0, name);
			if (a.IsError())
				return Err(a.Error());
			return Ok(MakeMat(vm, make(float(a.Value()))));
		};
	};
	builder.StaticFn("rotate_x", 1, 1,
					 rotation([](float a) { return math::FMatrix4::RotateX(a); }, "math.mat4.rotate_x"));
	builder.StaticFn("rotate_y", 1, 1,
					 rotation([](float a) { return math::FMatrix4::RotateY(a); }, "math.mat4.rotate_y"));
	builder.StaticFn("rotate_z", 1, 1,
					 rotation([](float a) { return math::FMatrix4::RotateZ(a); }, "math.mat4.rotate_z"));
	builder.StaticFn("rotate", 2, 2, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto axis = Vec3Of(args[0], "math.mat4.rotate");
		auto angle = lib::ArgFloat(args, 1, "math.mat4.rotate");
		if (axis.IsError())
			return Err(axis.Error());
		if (angle.IsError())
			return Err(angle.Error());
		return Ok(MakeMat(vm, math::FMatrix4::Rotate(axis.Value().Normalize(), float(angle.Value()))));
	});
	builder.StaticFn("perspective", 4, 4, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		float v[4];
		for (size_t i = 0; i < 4; ++i) {
			auto x = lib::ArgFloat(args, i, "math.mat4.perspective");
			if (x.IsError())
				return Err(x.Error());
			v[i] = float(x.Value());
		}
		return Ok(MakeMat(vm, math::FMatrix4::Perspective(v[0], v[1], v[2], v[3])));
	});
	builder.StaticFn("ortho", 6, 6, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		float v[6];
		for (size_t i = 0; i < 6; ++i) {
			auto x = lib::ArgFloat(args, i, "math.mat4.ortho");
			if (x.IsError())
				return Err(x.Error());
			v[i] = float(x.Value());
		}
		return Ok(MakeMat(vm, math::FMatrix4::Ortho(v[0], v[1], v[2], v[3], v[4], v[5])));
	});
	builder.StaticFn("look_at", 3, 3, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		math::FVector3 v[3];
		for (size_t i = 0; i < 3; ++i) {
			auto x = Vec3Of(args[i], "math.mat4.look_at");
			if (x.IsError())
				return Err(x.Error());
			v[i] = x.Value();
		}
		return Ok(MakeMat(vm, math::FMatrix4::LookAt(v[0], v[1], v[2])));
	});
	builder.StaticFn("compose", 3, 3, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto t = Vec3Of(args[0], "math.mat4.compose");
		auto r = QuatOf(args[1], "math.mat4.compose");
		auto s = Vec3Of(args[2], "math.mat4.compose");
		if (t.IsError())
			return Err(t.Error());
		if (r.IsError())
			return Err(r.Error());
		if (s.IsError())
			return Err(s.Error());
		return Ok(MakeMat(vm, math::ComposeTRS(t.Value(), r.Value(), s.Value())));
	});
	auto transformed = [](bool point) {
		return [point](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			auto v = Vec3Of(args[0], point ? "mat4.transform_point" : "mat4.transform_dir");
			if (v.IsError())
				return Err(v.Error());
			const math::FMatrix4 m = As<MatObject>(self).Get();
			return Ok(MakeVec3(vm, point ? m.TransformPoint(v.Value()) : m.TransformDir(v.Value())));
		};
	};
	builder.Method("transform_point", 1, 1, transformed(true));
	builder.Method("transform_dir", 1, 1, transformed(false));
	builder.Method("transpose", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeMat(vm, As<MatObject>(self).Get().Transpose()));
	});
	builder.Method("inverse", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeMat(vm, As<MatObject>(self).Get().Inverse()));
	});
	builder.Method("at", 2, 2, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto r = lib::ArgInt(args, 0, "mat4.at");
		auto c = lib::ArgInt(args, 1, "mat4.at");
		if (r.IsError())
			return Err(r.Error());
		if (c.IsError())
			return Err(c.Error());
		if (r.Value() < 0 || r.Value() > 3 || c.Value() < 0 || c.Value() > 3)
			return Err(Fail(String("`mat4.at(ligne, colonne)` : indices de 0 à 3")));
		return Ok(Value::Float(As<MatObject>(self).Get().At(int(r.Value()), int(c.Value())), NumberType::F32));
	});
	builder.Method("set", 3, 3, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto r = lib::ArgInt(args, 0, "mat4.set");
		auto c = lib::ArgInt(args, 1, "mat4.set");
		auto v = lib::ArgFloat(args, 2, "mat4.set");
		if (r.IsError())
			return Err(r.Error());
		if (c.IsError())
			return Err(c.Error());
		if (v.IsError())
			return Err(v.Error());
		if (r.Value() < 0 || r.Value() > 3 || c.Value() < 0 || c.Value() > 3)
			return Err(Fail(String("`mat4.set(ligne, colonne, valeur)` : indices de 0 à 3")));
		MatObject& o = As<MatObject>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		o.m.At(int(r.Value()), int(c.Value())) = float(v.Value());
		return Ok(Value::Host(self));
	});
	builder.Method("to_list", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		const math::FMatrix4 m = As<MatObject>(self).Get();
		auto list = std::make_shared<ListObject>();
		for (float v : m.m)
			list->items.push_back(Value::Float(v, NumberType::F32));
		return Ok(Value::List(std::move(list)));
	});
	builder.Method("copy", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeMat(vm, As<MatObject>(self).Get()));
	});
	// { translation: vec3, rotation: quat, scale: vec3, sheared: bool }
	builder.Method("decompose", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		const math::TRS trs = math::DecomposeTRS(As<MatObject>(self).Get());
		auto map = std::make_shared<MapObject>();
		map->SetKey(String("translation"), MakeVec3(vm, trs.translation));
		map->SetKey(String("rotation"), MakeQuat(vm, trs.rotation));
		map->SetKey(String("scale"), MakeVec3(vm, trs.scale));
		map->SetKey(String("sheared"), Value::Boolean(trs.sheared));
		return Ok(Value::Map(std::move(map)));
	});
	HostType& type = *builder.type;
	type.index = [](Interpreter&, const HostRef& self, const Value& key) -> Result<Value, ScriptError> {
		Args args{key};
		auto raw = lib::ArgInt(args, 0, "mat4[]");
		if (raw.IsError())
			return Err(raw.Error());
		auto index = lib::ToIndex(raw.Value(), 16, "mat4[]");
		if (index.IsError())
			return Err(index.Error());
		return Ok(Value::Float(As<MatObject>(self).Get().m[index.Value()], NumberType::F32));
	};
	type.items = [](const HostObject& self) {
		std::vector<Value> out;
		for (float v : As<MatObject>(self).Get().m)
			out.push_back(Value::Float(v, NumberType::F32));
		return out;
	};
	type.display = [](const HostObject& self) {
		const math::FMatrix4 m = As<MatObject>(self).Get();
		String out("mat4(");
		for (int r = 0; r < 4; ++r) {
			out.Concat(r ? "; " : "");
			for (int c = 0; c < 4; ++c) {
				out.Concat(c ? " " : "");
				out.Concat(Value::NumberToString(double(m.At(r, c))));
			}
		}
		out.Concat(")");
		return out;
	};
	type.equals = [](const HostObject& a, const HostObject& b) {
		const math::FMatrix4 x = As<MatObject>(a).Get(), y = As<MatObject>(b).Get();
		return std::equal(std::begin(x.m), std::end(x.m), std::begin(y.m));
	};
	// mat * mat, mat * vec4, mat * vec3 (un POINT : w = 1).
	type.binary = [](Interpreter& vm, BinaryOp op, const Value& a,
					 const Value& b) -> Option<Result<Value, ScriptError>> {
		using R = Result<Value, ScriptError>;
		const MatObject* ma = AsMat(a);
		if (!ma || op != BinaryOp::MULTIPLY)
			return NONE;
		const math::FMatrix4 m = ma->Get();
		if (const MatObject* mb = AsMat(b))
			return Some(R(Ok(MakeMat(vm, m * mb->Get()))));
		if (const VecObject* v = AsVec(b, 4)) {
			float c[4];
			v->Get(c);
			return Some(R(Ok(MakeVec4(vm, m * math::FVector4{c[0], c[1], c[2], c[3]}))));
		}
		if (const VecObject* v = AsVec(b, 3)) {
			float c[4];
			v->Get(c);
			return Some(R(Ok(MakeVec3(vm, m.TransformPoint(math::FVector3{c[0], c[1], c[2]})))));
		}
		return NONE;
	};
}

void DefineQuat(TypeBuilder& builder) {
	std::weak_ptr<const HostType> weakType = builder.type;
	builder.Construct(0, 4, [weakType](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
		math::FQuaternion q = math::FQuaternion::Identity();
		if (!args.empty()) {
			if (args.size() != 4)
				return Err(Fail(String("`math.quat(x, y, z, w)` (ou sans argument : l'identité)")));
			float c[4];
			for (size_t i = 0; i < 4; ++i) {
				auto x = lib::ArgFloat(args, i, "math.quat");
				if (x.IsError())
					return Err(x.Error());
				c[i] = float(x.Value());
			}
			q = math::FQuaternion(c[0], c[1], c[2], c[3]);
		}
		auto object = std::make_shared<QuatObject>();
		object->type = weakType.lock();
		object->q = q;
		return Ok(Value::Host(std::move(object)));
	});
	builder.StaticFn("identity", 0, 0, [](Interpreter& vm, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeQuat(vm, math::FQuaternion::Identity()));
	});
	builder.StaticFn("from_axis_angle", 2, 2, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto axis = Vec3Of(args[0], "math.quat.from_axis_angle");
		auto angle = lib::ArgFloat(args, 1, "math.quat.from_axis_angle");
		if (axis.IsError())
			return Err(axis.Error());
		if (angle.IsError())
			return Err(angle.Error());
		return Ok(MakeQuat(vm, math::FQuaternion::FromAxisAngle(axis.Value().Normalize(), float(angle.Value()))));
	});
	builder.StaticFn("from_euler", 3, 3, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		float v[3];
		for (size_t i = 0; i < 3; ++i) {
			auto x = lib::ArgFloat(args, i, "math.quat.from_euler");
			if (x.IsError())
				return Err(x.Error());
			v[i] = float(x.Value());
		}
		return Ok(MakeQuat(vm, math::FQuaternion::FromEuler(v[0], v[1], v[2])));
	});
	builder.StaticFn("from_to", 2, 2, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto a = Vec3Of(args[0], "math.quat.from_to");
		auto b = Vec3Of(args[1], "math.quat.from_to");
		if (a.IsError())
			return Err(a.Error());
		if (b.IsError())
			return Err(b.Error());
		return Ok(MakeQuat(vm, math::FQuaternion::FromTo(a.Value().Normalize(), b.Value().Normalize())));
	});
	builder.StaticFn("from_matrix", 1, 1, [](Interpreter& vm, Args& args) -> Result<Value, ScriptError> {
		auto m = MatOf(args[0], "math.quat.from_matrix");
		if (m.IsError())
			return Err(m.Error());
		return Ok(MakeQuat(vm, math::FQuaternion::FromMatrix(m.Value())));
	});
	auto unaryQ = [](math::FQuaternion (*fn)(const math::FQuaternion&)) {
		return [fn](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			return Ok(MakeQuat(vm, fn(As<QuatObject>(self).Get())));
		};
	};
	builder.Method("conjugate", 0, 0, unaryQ([](const math::FQuaternion& q) { return q.Conjugate(); }));
	builder.Method("normalize", 0, 0, unaryQ([](const math::FQuaternion& q) { return q.Normalize(); }));
	builder.Method("norm", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Float(As<QuatObject>(self).Get().Norm(), NumberType::F32));
	});
	builder.Method("rotate", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto v = Vec3Of(args[0], "quat.rotate");
		if (v.IsError())
			return Err(v.Error());
		return Ok(MakeVec3(vm, As<QuatObject>(self).Get().Rotate(v.Value())));
	});
	builder.Method("to_euler", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeVec3(vm, As<QuatObject>(self).Get().ToEuler()));
	});
	builder.Method("to_mat4", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeMat(vm, As<QuatObject>(self).Get().ToMat4()));
	});
	builder.Method("slerp", 2, 2, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto to = QuatOf(args[0], "quat.slerp");
		auto t = lib::ArgFloat(args, 1, "quat.slerp");
		if (to.IsError())
			return Err(to.Error());
		if (t.IsError())
			return Err(t.Error());
		return Ok(MakeQuat(vm, As<QuatObject>(self).Get().Slerp(to.Value(), float(t.Value()))));
	});
	builder.Method("copy", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeQuat(vm, As<QuatObject>(self).Get()));
	});
	HostType& type = *builder.type;
	type.get = [](const HostObject& self, const String& name) -> Option<Value> {
		const math::FQuaternion q = As<QuatObject>(self).Get();
		if (name == "x")
			return Some(Value::Float(q.x, NumberType::F32));
		if (name == "y")
			return Some(Value::Float(q.y, NumberType::F32));
		if (name == "z")
			return Some(Value::Float(q.z, NumberType::F32));
		if (name == "w")
			return Some(Value::Float(q.w, NumberType::F32));
		return NONE;
	};
	type.display = [](const HostObject& self) {
		const math::FQuaternion q = As<QuatObject>(self).Get();
		return String::Format("quat(%s, %s, %s, %s)", Value::NumberToString(q.x).CStr(),
							  Value::NumberToString(q.y).CStr(), Value::NumberToString(q.z).CStr(),
							  Value::NumberToString(q.w).CStr());
	};
	type.equals = [](const HostObject& a, const HostObject& b) {
		const math::FQuaternion x = As<QuatObject>(a).Get(), y = As<QuatObject>(b).Get();
		return x.x == y.x && x.y == y.y && x.z == y.z && x.w == y.w;
	};
	// q * q (composition), q * vec3 (rotation).
	type.binary = [](Interpreter& vm, BinaryOp op, const Value& a,
					 const Value& b) -> Option<Result<Value, ScriptError>> {
		using R = Result<Value, ScriptError>;
		const QuatObject* qa = AsQuat(a);
		if (!qa || op != BinaryOp::MULTIPLY)
			return NONE;
		if (const QuatObject* qb = AsQuat(b))
			return Some(R(Ok(MakeQuat(vm, qa->Get() * qb->Get()))));
		if (const VecObject* v = AsVec(b, 3)) {
			float c[4];
			v->Get(c);
			return Some(R(Ok(MakeVec3(vm, qa->Get().Rotate(math::FVector3{c[0], c[1], c[2]})))));
		}
		return NONE;
	};
}

// ── AabbObject ───────────────────────────────────────────────────────────────

math::FAABB AabbObject::Get() const {
std::lock_guard<std::mutex> lock(mutex);
return box;
}

Value MakeAabb(Interpreter& vm, const math::FAABB& box) {
	auto object = std::make_shared<AabbObject>();
	object->type = TypesOf(vm).aabb;
	object->box = box;
	return Value::Host(std::move(object));
}

const AabbObject* AsAabb(const Value& v) {
	return v.IsHost() && v.AsHost() ? dynamic_cast<const AabbObject*>(v.AsHost().get()) : nullptr;
}

void DefineGeometry(TypeBuilder& aabb, TypeBuilder& plane, TypeBuilder& ray) {
	std::weak_ptr<const HostType> aabbType = aabb.type, planeType = plane.type, rayType = ray.type;
	aabb.Construct(0, 2, [aabbType](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
		math::FAABB box;
		if (args.size() == 2) {
			auto lo = Vec3Of(args[0], "math.aabb");
			auto hi = Vec3Of(args[1], "math.aabb");
			if (lo.IsError())
				return Err(lo.Error());
			if (hi.IsError())
				return Err(hi.Error());
			box = math::FAABB(lo.Value(), hi.Value());
		} else if (!args.empty()) {
			return Err(Fail(String("`math.aabb()` (vide) ou `math.aabb(min, max)`")));
		}
		auto object = std::make_shared<AabbObject>();
		object->type = aabbType.lock();
		object->box = box;
		return Ok(Value::Host(std::move(object)));
	});
	auto vecQuery = [](math::FVector3 (math::FAABB::*query)() const) {
		return [query](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			const math::FAABB box = As<AabbObject>(self).Get();
			return Ok(MakeVec3(vm, (box.*query)()));
		};
	};
	aabb.Method("center", 0, 0, vecQuery(&math::FAABB::Center));
	aabb.Method("half_extents", 0, 0, vecQuery(&math::FAABB::HalfExtents));
	aabb.Method("size", 0, 0, vecQuery(&math::FAABB::Size));
	aabb.Method("is_valid", 0, 0, [](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(Value::Boolean(As<AabbObject>(self).Get().IsValid()));
	});
	aabb.Method("contains", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		const math::FAABB box = As<AabbObject>(self).Get();
		if (const AabbObject* other = AsAabb(args[0]))
			return Ok(Value::Boolean(box.Contains(other->Get())));
		auto p = Vec3Of(args[0], "aabb.contains");
		if (p.IsError())
			return Err(p.Error());
		return Ok(Value::Boolean(box.Contains(p.Value())));
	});
	aabb.Method("intersects", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		const AabbObject* other = AsAabb(args[0]);
		if (!other)
			return Err(Fail(String("`aabb.intersects` : math.aabb attendu")));
		return Ok(Value::Boolean(As<AabbObject>(self).Get().Intersects(other->Get())));
	});
	aabb.Method("expand", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		AabbObject& o = As<AabbObject>(self);
		if (const AabbObject* other = AsAabb(args[0])) {
			const math::FAABB box = other->Get();
			std::lock_guard<std::mutex> lock(o.mutex);
			o.box.Expand(box);
			return Ok(Value::Host(self));
		}
		auto p = Vec3Of(args[0], "aabb.expand");
		if (p.IsError())
			return Err(p.Error());
		std::lock_guard<std::mutex> lock(o.mutex);
		o.box.Expand(p.Value());
		return Ok(Value::Host(self));
	});
	aabb.Method("translated", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto t = Vec3Of(args[0], "aabb.translated");
		if (t.IsError())
			return Err(t.Error());
		return Ok(MakeAabb(vm, As<AabbObject>(self).Get().Translated(t.Value())));
	});
	aabb.Method("transformed", 1, 1,
				[](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
					auto m = MatOf(args[0], "aabb.transformed");
					if (m.IsError())
						return Err(m.Error());
					return Ok(MakeAabb(vm, As<AabbObject>(self).Get().Transformed(m.Value())));
				});
	aabb.Method("min", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeVec3(vm, As<AabbObject>(self).Get().min));
	});
	aabb.Method("max", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeVec3(vm, As<AabbObject>(self).Get().max));
	});
	aabb.type->display = [](const HostObject& self) {
		const math::FAABB box = As<AabbObject>(self).Get();
		return String::Format("aabb(min (%s, %s, %s), max (%s, %s, %s))", Value::NumberToString(box.min.x).CStr(),
							  Value::NumberToString(box.min.y).CStr(), Value::NumberToString(box.min.z).CStr(),
							  Value::NumberToString(box.max.x).CStr(), Value::NumberToString(box.max.y).CStr(),
							  Value::NumberToString(box.max.z).CStr());
	};

	// Plan : `math.plane(normale, d)` ou `math.plane(normale, point)`.
	plane.Construct(2, 2, [planeType](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
		auto n = Vec3Of(args[0], "math.plane");
		if (n.IsError())
			return Err(n.Error());
		auto object = std::make_shared<PlaneObject>();
		object->type = planeType.lock();
		if (args[1].IsNumber()) {
			object->plane = math::FPlane(n.Value(), args[1].AsFloat());
		} else {
			auto point = Vec3Of(args[1], "math.plane");
			if (point.IsError())
				return Err(point.Error());
			object->plane = math::FPlane(n.Value(), point.Value());
		}
		return Ok(Value::Host(std::move(object)));
	});
	plane.StaticFn("from_triangle", 3, 3, [planeType](Interpreter&, Args& args) -> Result<Value, ScriptError> {
		math::FVector3 v[3];
		for (size_t i = 0; i < 3; ++i) {
			auto x = Vec3Of(args[i], "math.plane.from_triangle");
			if (x.IsError())
				return Err(x.Error());
			v[i] = x.Value();
		}
		auto object = std::make_shared<PlaneObject>();
		object->type = planeType.lock();
		object->plane = math::FPlane::fromTriangle(v[0], v[1], v[2]);
		return Ok(Value::Host(std::move(object)));
	});
	plane.Method("distance", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto p = Vec3Of(args[0], "plane.distance");
		if (p.IsError())
			return Err(p.Error());
		return Ok(Value::Float(As<PlaneObject>(self).plane.Distance(p.Value()), NumberType::F32));
	});
	plane.type->get = [](const HostObject& self, const String& name) -> Option<Value> {
		const math::FPlane& p = As<PlaneObject>(self).plane;
		if (name == "d")
			return Some(Value::Float(p.d, NumberType::F32));
		if (name == "normal")
			return Some(Value::List(std::make_shared<ListObject>(
				std::vector<Value>{Value::Float(p.normal.x, NumberType::F32), Value::Float(p.normal.y, NumberType::F32),
								   Value::Float(p.normal.z, NumberType::F32)})));
		return NONE;
	};
	plane.type->display = [](const HostObject& self) {
		const math::FPlane& p = As<PlaneObject>(self).plane;
		return String::Format("plane(n (%s, %s, %s), d %s)", Value::NumberToString(p.normal.x).CStr(),
							  Value::NumberToString(p.normal.y).CStr(), Value::NumberToString(p.normal.z).CStr(),
							  Value::NumberToString(p.d).CStr());
	};

	// Rayon : `math.ray(origine, direction)` (direction normalisée).
	ray.Construct(2, 2, [rayType](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
		auto origin = Vec3Of(args[0], "math.ray");
		auto direction = Vec3Of(args[1], "math.ray");
		if (origin.IsError())
			return Err(origin.Error());
		if (direction.IsError())
			return Err(direction.Error());
		auto object = std::make_shared<RayObject>();
		object->type = rayType.lock();
		object->ray = math::FRay(origin.Value(), direction.Value());
		return Ok(Value::Host(std::move(object)));
	});
	ray.Method("at", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto t = lib::ArgFloat(args, 0, "ray.at");
		if (t.IsError())
			return Err(t.Error());
		return Ok(MakeVec3(vm, As<RayObject>(self).ray.At(float(t.Value()))));
	});
	ray.Method("distance_to", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto p = Vec3Of(args[0], "ray.distance_to");
		if (p.IsError())
			return Err(p.Error());
		return Ok(Value::Float(As<RayObject>(self).ray.DistanceTo(p.Value()), NumberType::F32));
	});
	// Intersections : nil si le rayon manque sa cible.
	ray.Method("intersect_aabb", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		const AabbObject* box = AsAabb(args[0]);
		if (!box)
			return Err(Fail(String("`ray.intersect_aabb` : math.aabb attendu")));
		float tMin = 0, tMax = 0;
		if (!As<RayObject>(self).ray.Intersects(box->Get(), tMin, tMax))
			return Ok(Value::Nil());
		return Ok(lib::Pair(Value::Float(tMin, NumberType::F32), Value::Float(tMax, NumberType::F32)));
	});
	ray.Method("intersect_plane", 1, 1,
			   [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
				   if (!args[0].IsHost() || !dynamic_cast<const PlaneObject*>(args[0].AsHost().get()))
					   return Err(Fail(String("`ray.intersect_plane` : math.plane attendu")));
				   float t = 0;
				   if (!As<RayObject>(self).ray.Intersects(As<PlaneObject>(*args[0].AsHost()).plane, t))
					   return Ok(Value::Nil());
				   return Ok(Value::Float(t, NumberType::F32));
			   });
	ray.Method("intersect_triangle", 3, 3,
			   [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
				   math::FVector3 v[3];
				   for (size_t i = 0; i < 3; ++i) {
					   auto x = Vec3Of(args[i], "ray.intersect_triangle");
					   if (x.IsError())
						   return Err(x.Error());
					   v[i] = x.Value();
				   }
				   float t = 0, u = 0, w = 0;
				   if (!As<RayObject>(self).ray.Intersects(v[0], v[1], v[2], t, u, w))
					   return Ok(Value::Nil());
				   return Ok(Value::List(std::make_shared<ListObject>(
					   std::vector<Value>{Value::Float(t, NumberType::F32), Value::Float(u, NumberType::F32),
										  Value::Float(w, NumberType::F32)})));
			   });
	ray.Method("transformed", 1, 1,
			   [rayType](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
				   auto m = MatOf(args[0], "ray.transformed");
				   if (m.IsError())
					   return Err(m.Error());
				   auto object = std::make_shared<RayObject>();
				   object->type = rayType.lock();
				   object->ray = As<RayObject>(self).ray.Transformed(m.Value());
				   return Ok(Value::Host(std::move(object)));
			   });
	ray.Method("origin", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeVec3(vm, As<RayObject>(self).ray.origin));
	});
	ray.Method("direction", 0, 0, [](Interpreter& vm, const HostRef& self, Args&) -> Result<Value, ScriptError> {
		return Ok(MakeVec3(vm, As<RayObject>(self).ray.direction));
	});
	ray.type->display = [](const HostObject& self) {
		const math::FRay& r = As<RayObject>(self).ray;
		return String::Format("ray(o (%s, %s, %s), d (%s, %s, %s))", Value::NumberToString(r.origin.x).CStr(),
							  Value::NumberToString(r.origin.y).CStr(), Value::NumberToString(r.origin.z).CStr(),
							  Value::NumberToString(r.direction.x).CStr(), Value::NumberToString(r.direction.y).CStr(),
							  Value::NumberToString(r.direction.z).CStr());
	};
}

Option<std::complex<double>> ComplexOf(const Value& v) {
	if (v.IsNumber())
		return Some(std::complex<double>(v.AsNumber(), 0.0));
	if (v.IsHost() && v.AsHost())
		if (const auto* c = dynamic_cast<const ComplexObject*>(v.AsHost().get()))
			return Some(c->z);
	return NONE;
}

void DefineComplex(TypeBuilder& builder) {
	std::weak_ptr<const HostType> weakType = builder.type;
	auto make = [weakType](std::complex<double> z) {
		auto object = std::make_shared<ComplexObject>();
		object->type = weakType.lock();
		object->z = z;
		return Value::Host(std::move(object));
	};
	builder.Construct(0, 2, [make](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
		double re = 0, im = 0;
		if (!args.empty()) {
			auto r = lib::ArgFloat(args, 0, "math.complex");
			if (r.IsError())
				return Err(r.Error());
			re = r.Value();
		}
		if (args.size() > 1) {
			auto i = lib::ArgFloat(args, 1, "math.complex");
			if (i.IsError())
				return Err(i.Error());
			im = i.Value();
		}
		return Ok(make({re, im}));
	});
	builder.StaticFn("polar", 2, 2, [make](Interpreter&, Args& args) -> Result<Value, ScriptError> {
		auto r = lib::ArgFloat(args, 0, "math.complex.polar");
		auto theta = lib::ArgFloat(args, 1, "math.complex.polar");
		if (r.IsError())
			return Err(r.Error());
		if (theta.IsError())
			return Err(theta.Error());
		if (r.Value() < 0.0 || !std::isfinite(r.Value()))
			return Err(Fail(String("`math.complex.polar` : module négatif ou non fini")));
		return Ok(make(std::polar(r.Value(), theta.Value())));
	});
	auto real = [](double (*fn)(const std::complex<double>&)) {
		return [fn](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			return Ok(Value::Number(fn(As<ComplexObject>(self).z)));
		};
	};
	builder.Method("abs", 0, 0, real([](const std::complex<double>& z) { return std::abs(z); }));
	builder.Method("arg", 0, 0, real([](const std::complex<double>& z) { return std::arg(z); }));
	builder.Method("norm", 0, 0, real([](const std::complex<double>& z) { return std::norm(z); }));
	auto complexFn = [make](std::complex<double> (*fn)(const std::complex<double>&)) {
		return [make, fn](Interpreter&, const HostRef& self, Args&) -> Result<Value, ScriptError> {
			return Ok(make(fn(As<ComplexObject>(self).z)));
		};
	};
	builder.Method("conj", 0, 0, complexFn([](const std::complex<double>& z) { return std::conj(z); }));
	builder.Method("exp", 0, 0, complexFn([](const std::complex<double>& z) { return std::exp(z); }));
	builder.Method("log", 0, 0, complexFn([](const std::complex<double>& z) { return std::log(z); }));
	builder.Method("sqrt", 0, 0, complexFn([](const std::complex<double>& z) { return std::sqrt(z); }));
	builder.Method("sin", 0, 0, complexFn([](const std::complex<double>& z) { return std::sin(z); }));
	builder.Method("cos", 0, 0, complexFn([](const std::complex<double>& z) { return std::cos(z); }));
	builder.Method("pow", 1, 1, [make](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		Option<std::complex<double>> e = ComplexOf(args[0]);
		if (e.IsNone())
			return Err(Fail(String("`complex.pow` : nombre ou complexe attendu")));
		return Ok(make(std::pow(As<ComplexObject>(self).z, e.Value())));
	});
	HostType& type = *builder.type;
	type.get = [](const HostObject& self, const String& name) -> Option<Value> {
		const std::complex<double> z = As<ComplexObject>(self).z;
		if (name == "real" || name == "re")
			return Some(Value::Number(z.real()));
		if (name == "imag" || name == "im")
			return Some(Value::Number(z.imag()));
		return NONE;
	};
	type.display = [](const HostObject& self) {
		const std::complex<double> z = As<ComplexObject>(self).z;
		if (z.imag() == 0.0)
			return Value::NumberToString(z.real());
		const String im = Value::NumberToString(std::fabs(z.imag()));
		if (z.real() == 0.0)
			return String::Format("%s%si", z.imag() < 0 ? "-" : "", im.CStr());
		return String::Format("%s%s%si", Value::NumberToString(z.real()).CStr(), z.imag() < 0 ? "-" : "+", im.CStr());
	};
	type.equals = [](const HostObject& a, const HostObject& b) {
		return As<ComplexObject>(a).z == As<ComplexObject>(b).z;
	};
	type.negate = [make](Interpreter&, const HostRef& self) -> Option<Result<Value, ScriptError>> {
		return Some(Result<Value, ScriptError>(Ok(make(-As<ComplexObject>(self).z))));
	};
	type.binary = [make](Interpreter&, BinaryOp op, const Value& a,
						 const Value& b) -> Option<Result<Value, ScriptError>> {
		using R = Result<Value, ScriptError>;
		Option<std::complex<double>> x = ComplexOf(a), y = ComplexOf(b);
		if (x.IsNone() || y.IsNone())
			return NONE;
		switch (op) {
			case BinaryOp::ADD:
				return Some(R(Ok(make(x.Value() + y.Value()))));
			case BinaryOp::SUBTRACT:
				return Some(R(Ok(make(x.Value() - y.Value()))));
			case BinaryOp::MULTIPLY:
				return Some(R(Ok(make(x.Value() * y.Value()))));
			case BinaryOp::DIVIDE:
				if (y.Value() == std::complex<double>(0.0, 0.0))
					return Some(R(Err(Fail(String("division par zéro")))));
				return Some(R(Ok(make(x.Value() / y.Value()))));
			case BinaryOp::EQUAL:
				return Some(R(Ok(Value::Boolean(x.Value() == y.Value()))));
			case BinaryOp::NOT_EQUAL:
				return Some(R(Ok(Value::Boolean(x.Value() != y.Value()))));
			default:
				return NONE;
		}
	};
}

void DefineRandomEngine(TypeBuilder& builder) {
	std::weak_ptr<const HostType> weakType = builder.type;
	builder.Construct(0, 1, [weakType](Interpreter&, Args& args, const Args&) -> Result<Value, ScriptError> {
		auto object = std::make_shared<EngineObject>();
		object->type = weakType.lock();
		if (!args.empty()) {
			auto seed = lib::ArgInt(args, 0, "math.random_engine");
			if (seed.IsError())
				return Err(seed.Error());
			object->engine.seed(uint64_t(seed.Value()));
		}
		return Ok(Value::Host(std::move(object)));
	});
	auto with = [](auto fn) {
		return [fn](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
			EngineObject& o = As<EngineObject>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			return fn(vm, o.engine, args);
		};
	};
	builder.Method("seed", 1, 1,
				   with([](Interpreter&, std::mt19937_64& engine, Args& args) -> Result<Value, ScriptError> {
					   auto seed = lib::ArgInt(args, 0, "random_engine.seed");
					   if (seed.IsError())
						   return Err(seed.Error());
					   engine.seed(uint64_t(seed.Value()));
					   return Ok(Value::Nil());
				   }));
	builder.Method("next", 0, 0, with([](Interpreter&, std::mt19937_64& engine, Args&) -> Result<Value, ScriptError> {
					   return Ok(Value::Number(std::uniform_real_distribution<double>(0.0, 1.0)(engine)));
				   }));
	builder.Method("int", 2, 2,
				   with([](Interpreter&, std::mt19937_64& engine, Args& args) -> Result<Value, ScriptError> {
					   auto lo = lib::ArgInt(args, 0, "random_engine.int");
					   auto hi = lib::ArgInt(args, 1, "random_engine.int");
					   if (lo.IsError())
						   return Err(lo.Error());
					   if (hi.IsError())
						   return Err(hi.Error());
					   if (hi.Value() < lo.Value())
						   return Err(Fail(String("`random_engine.int(min, max)` : max < min")));
					   return Ok(Value::Int(std::uniform_int_distribution<int64_t>(lo.Value(), hi.Value())(engine)));
				   }));
	builder.Method("float", 2, 2,
				   with([](Interpreter&, std::mt19937_64& engine, Args& args) -> Result<Value, ScriptError> {
					   auto lo = lib::ArgFloat(args, 0, "random_engine.float");
					   auto hi = lib::ArgFloat(args, 1, "random_engine.float");
					   if (lo.IsError())
						   return Err(lo.Error());
					   if (hi.IsError())
						   return Err(hi.Error());
					   if (!(hi.Value() > lo.Value()) || !std::isfinite(hi.Value() - lo.Value()))
						   return Err(Fail(String("`random_engine.float(min, max)` : intervalle invalide")));
					   return Ok(Value::Number(std::uniform_real_distribution<double>(lo.Value(), hi.Value())(engine)));
				   }));
	builder.Method("normal", 0, 2,
				   with([](Interpreter&, std::mt19937_64& engine, Args& args) -> Result<Value, ScriptError> {
					   double mean = 0.0, stddev = 1.0;
					   if (!args.empty()) {
						   auto m = lib::ArgFloat(args, 0, "random_engine.normal");
						   if (m.IsError())
							   return Err(m.Error());
						   mean = m.Value();
					   }
					   if (args.size() > 1) {
						   auto s = lib::ArgFloat(args, 1, "random_engine.normal");
						   if (s.IsError())
							   return Err(s.Error());
						   stddev = s.Value();
					   }
					   if (!(stddev > 0.0) || !std::isfinite(stddev) || !std::isfinite(mean))
						   return Err(Fail(String("`random_engine.normal` : écart type strictement positif attendu")));
					   return Ok(Value::Number(std::normal_distribution<double>(mean, stddev)(engine)));
				   }));
	builder.Method("bool", 0, 1,
				   with([](Interpreter&, std::mt19937_64& engine, Args& args) -> Result<Value, ScriptError> {
					   double p = 0.5;
					   if (!args.empty()) {
						   auto x = lib::ArgFloat(args, 0, "random_engine.bool");
						   if (x.IsError())
							   return Err(x.Error());
						   p = x.Value();
					   }
					   if (!(p >= 0.0 && p <= 1.0))
						   return Err(Fail(String("`random_engine.bool(p)` : p dans [0, 1]")));
					   return Ok(Value::Boolean(std::bernoulli_distribution(p)(engine)));
				   }));
	builder.Method("choice", 1, 1, [](Interpreter& vm, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto items = lib::ItemsOf(vm, args[0]);
		if (items.IsError())
			return Err(items.Error());
		if (items.Value().empty())
			return Ok(Value::Nil());
		EngineObject& o = As<EngineObject>(self);
		std::lock_guard<std::mutex> lock(o.mutex);
		const size_t i = std::uniform_int_distribution<size_t>(0, items.Value().size() - 1)(o.engine);
		return Ok(items.Value()[i]);
	});
	// Mélange EN PLACE une liste du script (Fisher-Yates).
	builder.Method("shuffle", 1, 1, [](Interpreter&, const HostRef& self, Args& args) -> Result<Value, ScriptError> {
		auto list = detail::ArgList(args, 0, "random_engine.shuffle");
		if (list.IsError())
			return Err(list.Error());
		std::vector<Value> items = list.Value()->Snapshot();
		{
			EngineObject& o = As<EngineObject>(self);
			std::lock_guard<std::mutex> lock(o.mutex);
			for (size_t i = items.size(); i > 1; --i)
				std::swap(items[i - 1], items[std::uniform_int_distribution<size_t>(0, i - 1)(o.engine)]);
		}
		list.Value()->Replace(std::move(items));
		return Ok(args[0]);
	});
	builder.type->display = [](const HostObject&) { return String("<math.random_engine mt19937_64>"); };
}

void InstallClasses(Interpreter& vm) {
	auto declare = [&vm](TypeBuilder& builder, const char* name) {
		vm.RegisterHostType(String("math"), String(name), builder.type);
	};
	TypeBuilder vec2("math.vec2"), vec3("math.vec3"), vec4("math.vec4");
	DefineVector(vec2, 2);
	DefineVector(vec3, 3);
	DefineVector(vec4, 4);
	declare(vec2, "vec2");
	declare(vec3, "vec3");
	declare(vec4, "vec4");
	TypeBuilder mat4("math.mat4"), quat("math.quat");
	DefineMat4(mat4);
	DefineQuat(quat);
	declare(mat4, "mat4");
	declare(quat, "quat");
	TypeBuilder aabb("math.aabb"), plane("math.plane"), ray("math.ray");
	DefineGeometry(aabb, plane, ray);
	declare(aabb, "aabb");
	declare(plane, "plane");
	declare(ray, "ray");
	TypeBuilder complex("math.complex"), engine("math.random_engine");
	DefineComplex(complex);
	DefineRandomEngine(engine);
	declare(complex, "complex");
	declare(engine, "random_engine");
}

} // namespace mathlib

void InstallMathLibrary(Interpreter& vm) {
	// ── Mathématiques : l'espace de noms `math` ────────────────────────────
	// Fonctions ET constantes rangées sous `math.` (`math.sin`, `math.pi`)
	// plutôt que dans les globales, où des noms d'une lettre (`e`, `c`, `g`)
	// entraient en collision avec les variables des scripts.

	auto unaryMath = [&vm](const char* name, double (*fn)(double)) {
		const String qualified = String::Format("math.%s", name);
		vm.RegisterNamespacedNative(
			String("math"), String(name), 1, 1,
			[qualified, fn](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
				auto n = detail::ArgNumber(args, 0, qualified.CStr());
				if (n.IsError())
					return Err(n.Error());
				// Un f32 reste un f32 ; un entier donne un f64.
				return Ok(Value::Float(fn(n.Unwrap()), args[0].GetNumberType()));
			});
	};
	// Fonctions qui gardent un entier ENTIER (et son type) : `math.abs(-3)`
	// vaut l'i64 3, `math.floor(u8(7))` l'u8 7.
	auto integerMath = [&vm](const char* name, double (*fn)(double), Result<Value, String> (*onInteger)(const Value&)) {
		const String qualified = String::Format("math.%s", name);
		vm.RegisterNamespacedNative(
			String("math"), String(name), 1, 1,
			[qualified, fn, onInteger](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
				auto n = detail::ArgNumber(args, 0, qualified.CStr());
				if (n.IsError())
					return Err(n.Error());
				if (!args[0].IsInteger())
					return Ok(Value::Float(fn(n.Unwrap()), args[0].GetNumberType()));
				auto result = onInteger(args[0]);
				if (result.IsError())
					return Err(
						Interpreter::MakeError(String::Format("`%s` : %s", qualified.CStr(), result.Error().CStr())));
				return Ok(std::move(result).Unwrap());
			});
	};
	auto identity = [](const Value& v) -> Result<Value, String> { return Ok(v); };
	integerMath(
		"abs", [](double v) { return std::fabs(v); },
		[](const Value& v) -> Result<Value, String> {
			return numeric::ToWide(v) < 0 ? numeric::Negate(v) : Result<Value, String>(Ok(v));
		});
	integerMath("floor", [](double v) { return std::floor(v); }, identity);
	integerMath("ceil", [](double v) { return std::ceil(v); }, identity);
	integerMath("round", [](double v) { return std::round(v); }, identity);
	integerMath(
		"sign", [](double v) { return v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0); },
		[](const Value& v) -> Result<Value, String> {
			const numeric::Wide w = numeric::ToWide(v);
			return Ok(Value::Int(w > 0 ? 1 : (w < 0 ? -1 : 0)));
		});
	unaryMath("sqrt", [](double v) { return v > 0.0 ? std::sqrt(v) : 0.0; });
	unaryMath("sin", [](double v) { return std::sin(v); });
	unaryMath("cos", [](double v) { return std::cos(v); });
	unaryMath("tan", [](double v) { return std::tan(v); });
	unaryMath("asin", [](double v) { return std::asin(v); });
	unaryMath("acos", [](double v) { return std::acos(v); });
	unaryMath("atan", [](double v) { return std::atan(v); });
	unaryMath("exp", [](double v) { return std::exp(v); });
	unaryMath("log", [](double v) { return v > 0.0 ? std::log(v) : 0.0; });
	unaryMath("rad", [](double v) { return v * 3.14159265358979323846 / 180.0; });
	unaryMath("deg", [](double v) { return v * 180.0 / 3.14159265358979323846; });

	// Constantes : des VALEURS (`math.pi`), non des fonctions à appeler.
	const std::pair<const char*, double> mathConstants[] = {
		{"pi", 3.14159265358979323846},
		{"e", 2.71828182845904523536},
		{"phi", 1.61803398874989484820},	// nombre d'or
		{"sqrt2", 1.41421356237309504880},	// racine de 2
		{"sqrt3", 1.73205080756887729352},	// racine de 3
		{"tau", 6.28318530717958647692},	// 2π (tour complet)
		{"pi_2", 1.57079632679489661923},	// π/2 (angle droit)
		{"pi_4", 0.78539816339744830962},	// π/4 (45°)
		{"inv_pi", 0.31830988618379067154}, // 1/π
		{"ln2", 0.69314718055994530941},
		{"ln10", 2.30258509299404568402},
		{"euler_mascheroni", 0.57721566490153286060}, // constante γ
		{"catalan", 0.91596559417721901505},
		{"inf", HUGE_VAL},
	};
	for (const auto& [name, value] : mathConstants)
		vm.RegisterNamespaceConstant(String("math"), String(name), Value::Number(value));

	// Constantes physiques : un espace de noms à part — ce ne sont pas des
	// mathématiques, et `physics.c` se lit mieux qu'un `c` global.
	const std::pair<const char*, double> physicsConstants[] = {
		{"c", 299792458.0},			   // vitesse de la lumière dans le vide (m/s)
		{"g", 9.80665},				   // pesanteur terrestre standard (m/s²)
		{"R", 8.314462618},			   // constante des gaz parfaits (J mol⁻¹ K⁻¹)
		{"N_A", 6.02214076e23},		   // nombre d'Avogadro (mol⁻¹)
		{"k_B", 1.380649e-23},		   // constante de Boltzmann (J/K)
		{"std_atm", 101325.0},		   // pression atmosphérique standard (Pa)
		{"h", 6.62607015e-34},		   // constante de Planck (J s)
		{"hbar", 1.054571817e-34},	   // constante de Planck réduite (h / 2π)
		{"e_charge", 1.602176634e-19}, // charge élémentaire (C)
		{"m_e", 9.1093837015e-31},	   // masse de l'électron au repos (kg)
	};
	for (const auto& [name, value] : physicsConstants)
		vm.RegisterNamespaceConstant(String("physics"), String(name), Value::Number(value));

	vm.RegisterNamespacedNative(String("math"), String("atan2"), 2, 2,
								[](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
									auto y = detail::ArgNumber(args, 0, "math.atan2");
									if (y.IsError())
										return Err(y.Error());
									auto x = detail::ArgNumber(args, 1, "math.atan2");
									if (x.IsError())
										return Err(x.Error());
									return Ok(Value::Number(std::atan2(y.Unwrap(), x.Unwrap())));
								});

	vm.RegisterNamespacedNative(String("math"), String("pow"), 2, 2,
								[](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
									auto base = detail::ArgNumber(args, 0, "math.pow");
									if (base.IsError())
										return Err(base.Error());
									auto exponent = detail::ArgNumber(args, 1, "math.pow");
									if (exponent.IsError())
										return Err(exponent.Error());
									return Ok(Value::Number(std::pow(base.Unwrap(), exponent.Unwrap())));
								});

	vm.RegisterNamespacedNative(String("math"), String("min"), 1, -1,
								[](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
									// L'argument retenu est rendu tel quel (avec son type).
									size_t best = 0;
									for (size_t i = 0; i < args.size(); ++i) {
										auto n = detail::ArgNumber(args, i, "math.min");
										if (n.IsError())
											return Err(n.Error());
										if (numeric::Compare(args[i], args[best]) == -1)
											best = i;
									}
									return Ok(args[best]);
								});

	vm.RegisterNamespacedNative(String("math"), String("max"), 1, -1,
								[](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
									// L'argument retenu est rendu tel quel (avec son type).
									size_t best = 0;
									for (size_t i = 0; i < args.size(); ++i) {
										auto n = detail::ArgNumber(args, i, "math.max");
										if (n.IsError())
											return Err(n.Error());
										if (numeric::Compare(args[i], args[best]) == 1)
											best = i;
									}
									return Ok(args[best]);
								});

	vm.RegisterNamespacedNative(String("math"), String("clamp"), 3, 3,
								[](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
									auto value = detail::ArgNumber(args, 0, "math.clamp");
									if (value.IsError())
										return Err(value.Error());
									auto low = detail::ArgNumber(args, 1, "math.clamp");
									if (low.IsError())
										return Err(low.Error());
									auto high = detail::ArgNumber(args, 2, "math.clamp");
									if (high.IsError())
										return Err(high.Error());
									// L'argument retenu est rendu tel quel (avec son type).
									if (numeric::Compare(args[0], args[1]) == -1)
										return Ok(args[1]);
									if (numeric::Compare(args[0], args[2]) == 1)
										return Ok(args[2]);
									return Ok(args[0]);
								});

	vm.RegisterNamespacedNative(String("math"), String("lerp"), 3, 3,
								[](Interpreter&, std::vector<Value>& args) -> Result<Value, ScriptError> {
									auto from = detail::ArgNumber(args, 0, "math.lerp");
									if (from.IsError())
										return Err(from.Error());
									auto to = detail::ArgNumber(args, 1, "math.lerp");
									if (to.IsError())
										return Err(to.Error());
									auto t = detail::ArgNumber(args, 2, "math.lerp");
									if (t.IsError())
										return Err(t.Error());
									double a = from.Unwrap(), b = to.Unwrap(), k = t.Unwrap();
									return Ok(Value::Number(a + (b - a) * k));
								});

	vm.RegisterNamespacedNative(String("math"), String("random"), 0, 0,
								[](Interpreter& vm, std::vector<Value>&) -> Result<Value, ScriptError> {
									return Ok(Value::Number(vm.NextRandom()));
								});

	vm.RegisterNamespacedNative(
		String("math"), String("random_int"), 2, 2,
		[](Interpreter& vm, std::vector<Value>& args) -> Result<Value, ScriptError> {
			auto low = detail::ArgNumber(args, 0, "math.random_int");
			if (low.IsError())
				return Err(low.Error());
			auto high = detail::ArgNumber(args, 1, "math.random_int");
			if (high.IsError())
				return Err(high.Error());
			double lo = std::floor(low.Unwrap()), hi = std::floor(high.Unwrap());
			if (hi < lo)
				return Err(
					Interpreter::MakeError(String("`math.random_int` : borne haute inférieure à la borne basse")));
			double span = hi - lo + 1.0;
			return Ok(Value::Int(static_cast<int64_t>(lo + std::floor(vm.NextRandom() * span))));
		});

	mathlib::InstallFunctions(vm);
	mathlib::InstallClasses(vm);
}

} // namespace data::script
