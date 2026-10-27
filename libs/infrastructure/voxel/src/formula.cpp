#include <voxel/formula.hpp>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace voxel {

enum class Formula::Op : uint8_t {
	CONST, VAR,
	NEG, NOT,
	ADD, SUB, MUL, DIV, MOD, POW,
	LT, LE, GT, GE, EQ, NE, AND, OR,
	SIN, COS, TAN, ASIN, ACOS, ATAN, ATAN2, SINH, COSH, TANH, SQRT, CBRT, ABS, FLOOR, CEIL, ROUND, TRUNC, FRAC, SIGN,
	EXP, LOG, LOG2, LOG10, MIN, MAX, HYPOT, CLAMP, IF,
};

namespace {

constexpr size_t MAX_STACK = 64;
constexpr double EPSILON = 1e-9;

struct Function {
	const char *name;
	Formula::Op op;
	int minArgs, maxArgs; ///< maxArgs < 0 : sans limite (min, max)
};

constexpr Function FUNCTIONS[] = {
	{"sin", Formula::Op::SIN, 1, 1},	 {"cos", Formula::Op::COS, 1, 1},	  {"tan", Formula::Op::TAN, 1, 1},
	{"asin", Formula::Op::ASIN, 1, 1},	 {"acos", Formula::Op::ACOS, 1, 1},	  {"atan", Formula::Op::ATAN, 1, 1},
	{"atan2", Formula::Op::ATAN2, 2, 2}, {"sinh", Formula::Op::SINH, 1, 1},	  {"cosh", Formula::Op::COSH, 1, 1},
	{"tanh", Formula::Op::TANH, 1, 1},	 {"sqrt", Formula::Op::SQRT, 1, 1},	  {"cbrt", Formula::Op::CBRT, 1, 1},
	{"abs", Formula::Op::ABS, 1, 1},	 {"floor", Formula::Op::FLOOR, 1, 1}, {"ceil", Formula::Op::CEIL, 1, 1},
	{"round", Formula::Op::ROUND, 1, 1}, {"trunc", Formula::Op::TRUNC, 1, 1}, {"frac", Formula::Op::FRAC, 1, 1},
	{"sign", Formula::Op::SIGN, 1, 1},	 {"exp", Formula::Op::EXP, 1, 1},	  {"log", Formula::Op::LOG, 1, 1},
	{"ln", Formula::Op::LOG, 1, 1},		 {"log2", Formula::Op::LOG2, 1, 1},	  {"log10", Formula::Op::LOG10, 1, 1},
	{"min", Formula::Op::MIN, 1, -1},	 {"max", Formula::Op::MAX, 1, -1},	  {"pow", Formula::Op::POW, 2, 2},
	{"hypot", Formula::Op::HYPOT, 2, 2}, {"clamp", Formula::Op::CLAMP, 3, 3}, {"mod", Formula::Op::MOD, 2, 2},
	{"if", Formula::Op::IF, 3, 3},
};

/// Analyse descendante → code en notation polonaise inverse.
class Parser {
public:
	Parser(const String &text, const std::vector<String> &variables) : m_text(text.CStr()), m_variables(variables) {}

	Result<bool, String> Run(std::vector<Formula::Instr> &code, bool &condition) {
		m_code = &code;
		Next();
		bool cond = false;
		if (Result<bool, String> r = Or(cond); r.IsError())
			return r;
		if (m_kind != Kind::END)
			return Fail("opérateur ou fin de formule attendu");
		condition = cond;
		return Ok(true);
	}

private:
	enum class Kind { END, NUMBER, NAME, SYMBOL };

	Result<bool, String> Fail(const char *what) const {
		return Err(String::Format("position %d : %s%s%s", int(m_start) + 1, what, m_token.empty() ? "" : " (près de « ",
								  m_token.empty() ? "" : (m_token + " »)").c_str()));
	}

	void Next() {
		while (m_text[m_pos] && std::isspace(uint8_t(m_text[m_pos])))
			++m_pos;
		m_start = m_pos;
		m_token.clear();
		const char c = m_text[m_pos];
		if (!c) {
			m_kind = Kind::END;
			return;
		}
		if (std::isdigit(uint8_t(c)) || (c == '.' && std::isdigit(uint8_t(m_text[m_pos + 1])))) {
			char *end = nullptr;
			m_number = std::strtod(m_text + m_pos, &end);
			m_token.assign(m_text + m_pos, size_t(end - (m_text + m_pos)));
			m_pos = size_t(end - m_text);
			m_kind = Kind::NUMBER;
			return;
		}
		if (std::isalpha(uint8_t(c)) || c == '_') {
			while (std::isalnum(uint8_t(m_text[m_pos])) || m_text[m_pos] == '_')
				m_token.push_back(m_text[m_pos++]);
			m_kind = Kind::NAME;
			return;
		}
		static const char *TWO[] = {"<=", ">=", "==", "!=", "&&", "||", "**"};
		for (const char *two : TWO)
			if (std::strncmp(m_text + m_pos, two, 2) == 0) {
				m_token = two;
				m_pos += 2;
				m_kind = Kind::SYMBOL;
				return;
			}
		m_token = std::string(1, c);
		++m_pos;
		m_kind = Kind::SYMBOL;
	}

	bool Is(const char *symbol) const { return m_kind == Kind::SYMBOL && m_token == symbol; }
	bool IsWord(const char *word) const { return m_kind == Kind::NAME && m_token == word; }

	void Emit(Formula::Op op, uint8_t arity = 0, uint16_t index = 0, double value = 0.0) {
		m_code->push_back(Formula::Instr{op, arity, index, value});
	}

	Result<bool, String> Or(bool &cond) {
		if (Result<bool, String> r = And(cond); r.IsError())
			return r;
		while (Is("||") || IsWord("or")) {
			Next();
			bool other = false;
			if (Result<bool, String> r = And(other); r.IsError())
				return r;
			Emit(Formula::Op::OR);
			cond = true;
		}
		return Ok(true);
	}
	Result<bool, String> And(bool &cond) {
		if (Result<bool, String> r = Not(cond); r.IsError())
			return r;
		while (Is("&&") || IsWord("and")) {
			Next();
			bool other = false;
			if (Result<bool, String> r = Not(other); r.IsError())
				return r;
			Emit(Formula::Op::AND);
			cond = true;
		}
		return Ok(true);
	}
	Result<bool, String> Not(bool &cond) {
		if (Is("!") || IsWord("not")) {
			Next();
			if (Result<bool, String> r = Not(cond); r.IsError())
				return r;
			Emit(Formula::Op::NOT);
			cond = true;
			return Ok(true);
		}
		return Compare(cond);
	}
	Result<bool, String> Compare(bool &cond) {
		if (Result<bool, String> r = Sum(); r.IsError())
			return r;
		static const std::pair<const char *, Formula::Op> OPS[] = {{"<=", Formula::Op::LE}, {">=", Formula::Op::GE}, {"<", Formula::Op::LT},
																	{">", Formula::Op::GT},	 {"==", Formula::Op::EQ}, {"=", Formula::Op::EQ},
																	{"!=", Formula::Op::NE}};
		// Comparaisons enchaînées (« 0 <= z <= 10 ») : chaque paire, liées par « and ».
		bool chained = false;
		for (;;) {
			const Formula::Op *op = nullptr;
			for (const auto &[symbol, code] : OPS)
				if (Is(symbol))
					op = &code;
			if (!op)
				break;
			const Formula::Op compare = *op;
			Next();
			if (chained)
				return Fail("comparaisons enchaînées : écrire « a <= b and b <= c »");
			if (Result<bool, String> r = Sum(); r.IsError())
				return r;
			Emit(compare);
			cond = chained = true;
		}
		return Ok(true);
	}
	Result<bool, String> Sum() {
		if (Result<bool, String> r = Product(); r.IsError())
			return r;
		while (Is("+") || Is("-")) {
			const Formula::Op op = Is("+") ? Formula::Op::ADD : Formula::Op::SUB;
			Next();
			if (Result<bool, String> r = Product(); r.IsError())
				return r;
			Emit(op);
		}
		return Ok(true);
	}
	Result<bool, String> Product() {
		if (Result<bool, String> r = Unary(); r.IsError())
			return r;
		for (;;) {
			Formula::Op op;
			bool consume = true;
			if (Is("*"))
				op = Formula::Op::MUL;
			else if (Is("/"))
				op = Formula::Op::DIV;
			else if (Is("%"))
				op = Formula::Op::MOD;
			else if (m_implicit && ((m_kind == Kind::NAME && !IsWord("and") && !IsWord("or") && !IsWord("not")) || Is("("))) {
				op = Formula::Op::MUL; // « 2x », « 3(x+1) », « u cos(v) » : produit implicite
				consume = false;
			} else {
				break;
			}
			m_implicit = false;
			if (consume)
				Next();
			if (Result<bool, String> r = Unary(); r.IsError())
				return r;
			Emit(op);
		}
		return Ok(true);
	}
	Result<bool, String> Unary() {
		if (Is("-")) {
			Next();
			if (Result<bool, String> r = Unary(); r.IsError())
				return r;
			Emit(Formula::Op::NEG);
			return Ok(true);
		}
		if (Is("+")) {
			Next();
			return Unary();
		}
		return Power();
	}
	Result<bool, String> Power() {
		if (Result<bool, String> r = Primary(); r.IsError())
			return r;
		if (Is("^") || Is("**")) {
			m_implicit = false;
			Next();
			if (Result<bool, String> r = Unary(); r.IsError()) // à droite : 2^-x, 2^3^2 = 2^9
				return r;
			Emit(Formula::Op::POW);
		}
		return Ok(true);
	}
	Result<bool, String> Primary() {
		m_implicit = false;
		if (m_kind == Kind::NUMBER) {
			Emit(Formula::Op::CONST, 0, 0, m_number);
			Next();
			m_implicit = true; // une valeur suivie d'un nom ou d'une parenthèse : produit
			return Ok(true);
		}
		if (Is("(")) {
			Next();
			bool inner = false;
			if (Result<bool, String> r = Or(inner); r.IsError())
				return r;
			if (!Is(")"))
				return Fail("« ) » attendu");
			Next();
			m_implicit = true; // « (x+1)(x-1) »
			return Ok(true);
		}
		if (m_kind == Kind::NAME) {
			const std::string name = m_token;
			Next();
			if (Is("(")) {
				const Function *fn = nullptr;
				for (const Function &f : FUNCTIONS)
					if (name == f.name)
						fn = &f;
				if (!fn)
					return Err(String::Format("fonction inconnue « %s »", name.c_str()));
				Next();
				int args = 0;
				if (!Is(")"))
					for (;;) {
						bool inner = false;
						if (Result<bool, String> r = Or(inner); r.IsError())
							return r;
						++args;
						if (Is(",")) {
							Next();
							continue;
						}
						break;
					}
				if (!Is(")"))
					return Fail("« ) » attendu après les arguments");
				Next();
				if (args < fn->minArgs || (fn->maxArgs >= 0 && args > fn->maxArgs) || args > 255)
					return Err(String::Format("%s : %d argument(s) donné(s)", name.c_str(), args));
				Emit(fn->op, uint8_t(args));
				m_implicit = true; // « 2 sin(x) cos(y) »
				return Ok(true);
			}
			for (size_t i = 0; i < m_variables.size(); ++i)
				if (m_variables[i] == name.c_str()) {
					Emit(Formula::Op::VAR, 0, uint16_t(i));
					m_implicit = true; // « u cos(v) », « x y »
					return Ok(true);
				}
			if (name == "pi")
				Emit(Formula::Op::CONST, 0, 0, M_PI);
			else if (name == "tau")
				Emit(Formula::Op::CONST, 0, 0, 2.0 * M_PI);
			else if (name == "e")
				Emit(Formula::Op::CONST, 0, 0, M_E);
			else
				return Err(String::Format("nom inconnu « %s »", name.c_str()));
			m_implicit = true;
			return Ok(true);
		}
		if (m_kind == Kind::END)
			return Fail("formule incomplète");
		return Fail("valeur attendue");
	}

	const char *m_text;
	const std::vector<String> &m_variables;
	std::vector<Formula::Instr> *m_code = nullptr;
	size_t m_pos = 0, m_start = 0;
	Kind m_kind = Kind::END;
	std::string m_token;
	double m_number = 0.0;
	bool m_implicit = false;
};

bool Truth(double v) { return v != 0.0 && !std::isnan(v); }

} // namespace

Result<Formula, String> Formula::Compile(const String &text, const std::vector<String> &variables) {
	Formula f;
	f.m_text = text.Trim();
	if (f.m_text.IsEmpty())
		return Err(String("formule vide"));
	if (f.m_text.GetSize() > 4096)
		return Err(String("formule trop longue"));
	Parser parser(f.m_text, variables);
	if (Result<bool, String> r = parser.Run(f.m_code, f.m_condition); r.IsError())
		return Err(r.Error());
	// Profondeur de pile.
	int depth = 0, peak = 0;
	for (const Instr &i : f.m_code) {
		switch (i.op) {
		case Op::CONST:
		case Op::VAR: ++depth; break;
		case Op::NEG:
		case Op::NOT: break;
		default:
			if (i.arity > 0)
				depth -= int(i.arity) - 1;
			else
				depth -= 1; // opérateur binaire
		}
		peak = std::max(peak, depth);
	}
	if (size_t(peak) > MAX_STACK)
		return Err(String("formule trop imbriquée"));
	f.m_stack = size_t(std::max(1, peak));
	return Ok(std::move(f));
}

double Formula::Eval(const double *values) const {
	double stack[MAX_STACK];
	size_t top = 0;
	for (const Instr &i : m_code) {
		switch (i.op) {
		case Op::CONST: stack[top++] = i.value; continue;
		case Op::VAR: stack[top++] = values[i.index]; continue;
		case Op::NEG: stack[top - 1] = -stack[top - 1]; continue;
		case Op::NOT: stack[top - 1] = Truth(stack[top - 1]) ? 0.0 : 1.0; continue;
		default: break;
		}
		if (i.arity == 0) { // opérateur binaire
			const double b = stack[--top];
			double &a = stack[top - 1];
			switch (i.op) {
			case Op::ADD: a += b; break;
			case Op::SUB: a -= b; break;
			case Op::MUL: a *= b; break;
			case Op::DIV: a /= b; break;
			case Op::MOD: a = b == 0.0 ? std::numeric_limits<double>::quiet_NaN() : a - b * std::floor(a / b); break;
			case Op::POW: a = std::pow(a, b); break;
			case Op::LT: a = a < b - EPSILON ? 1.0 : 0.0; break;
			case Op::LE: a = a <= b + EPSILON ? 1.0 : 0.0; break;
			case Op::GT: a = a > b + EPSILON ? 1.0 : 0.0; break;
			case Op::GE: a = a >= b - EPSILON ? 1.0 : 0.0; break;
			case Op::EQ: a = std::fabs(a - b) <= EPSILON ? 1.0 : 0.0; break;
			case Op::NE: a = std::fabs(a - b) > EPSILON ? 1.0 : 0.0; break;
			case Op::AND: a = Truth(a) && Truth(b) ? 1.0 : 0.0; break;
			case Op::OR: a = Truth(a) || Truth(b) ? 1.0 : 0.0; break;
			default: break;
			}
			continue;
		}
		// Fonction à `arity` arguments : stack[base .. top-1].
		const size_t base = top - i.arity;
		double *a = stack + base;
		double r = 0.0;
		switch (i.op) {
		case Op::SIN: r = std::sin(a[0]); break;
		case Op::COS: r = std::cos(a[0]); break;
		case Op::TAN: r = std::tan(a[0]); break;
		case Op::ASIN: r = std::asin(a[0]); break;
		case Op::ACOS: r = std::acos(a[0]); break;
		case Op::ATAN: r = std::atan(a[0]); break;
		case Op::ATAN2: r = std::atan2(a[0], a[1]); break;
		case Op::SINH: r = std::sinh(a[0]); break;
		case Op::COSH: r = std::cosh(a[0]); break;
		case Op::TANH: r = std::tanh(a[0]); break;
		case Op::SQRT: r = std::sqrt(a[0]); break;
		case Op::CBRT: r = std::cbrt(a[0]); break;
		case Op::ABS: r = std::fabs(a[0]); break;
		case Op::FLOOR: r = std::floor(a[0]); break;
		case Op::CEIL: r = std::ceil(a[0]); break;
		case Op::ROUND: r = std::round(a[0]); break;
		case Op::TRUNC: r = std::trunc(a[0]); break;
		case Op::FRAC: r = a[0] - std::floor(a[0]); break;
		case Op::SIGN: r = a[0] > 0.0 ? 1.0 : a[0] < 0.0 ? -1.0 : 0.0; break;
		case Op::EXP: r = std::exp(a[0]); break;
		case Op::LOG: r = std::log(a[0]); break;
		case Op::LOG2: r = std::log2(a[0]); break;
		case Op::LOG10: r = std::log10(a[0]); break;
		case Op::POW: r = std::pow(a[0], a[1]); break;
		case Op::HYPOT: r = std::hypot(a[0], a[1]); break;
		case Op::MOD: r = a[1] == 0.0 ? std::numeric_limits<double>::quiet_NaN() : a[0] - a[1] * std::floor(a[0] / a[1]); break;
		case Op::CLAMP: r = std::min(std::max(a[0], a[1]), a[2]); break;
		case Op::IF: r = Truth(a[0]) ? a[1] : a[2]; break;
		case Op::MIN:
		case Op::MAX:
			r = a[0];
			for (size_t k = 1; k < i.arity; ++k)
				r = i.op == Op::MIN ? std::min(r, a[k]) : std::max(r, a[k]);
			break;
		default: break;
		}
		top = base;
		stack[top++] = r;
	}
	return top > 0 ? stack[top - 1] : std::numeric_limits<double>::quiet_NaN();
}

} // namespace voxel
