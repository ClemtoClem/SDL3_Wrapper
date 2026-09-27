// Définitions de generators/lsystem.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "generators/lsystem.hpp"

namespace generators {

String ExpandLSystem(const String &axiom, const std::vector<LRule> &rules, int iterations,
		uint64_t seed, size_t maxLength) {
	Rng rng(seed);
	String current = axiom;
	for (int it = 0; it < iterations; ++it) {
		String next;
		for (size_t i = 0; i < current.GetSize(); ++i) {
			const char c = current.CharAt(i);
			std::vector<const LRule *> matches;
			std::vector<double> weights;
			for (const LRule &rule : rules)
				if (rule.symbol == c) {
					matches.push_back(&rule);
					weights.push_back(rule.weight);
				}
			if (matches.empty()) {
				next.PushBack(c);
			} else {
				const int64_t pick = matches.size() == 1 ? 0 : rng.Weighted(weights);
				next.Concat(matches[size_t(pick < 0 ? 0 : pick)]->replacement);
			}
			if (next.GetSize() > maxLength)
				return current; // la génération suivante serait trop grande
		}
		current = std::move(next);
	}
	return current;
}

std::vector<TurtleSegment> InterpretTurtle(const String &program, const TurtleSettings &s) {
	struct State {
		float px = 0, py = 0, pz = 0;
		float hx = 0, hy = 1, hz = 0; ///< direction (heading)
		float lx = 1, ly = 0, lz = 0; ///< gauche
		float ux = 0, uy = 0, uz = 1; ///< dessus
		float thickness, step;
		int depth = 0;
	};
	Rng rng(s.seed);
	std::vector<TurtleSegment> out;
	std::vector<State> stack;
	State t;
	t.thickness = s.thickness;
	t.step = s.step;
	auto rotate = [](float &ax, float &ay, float &az, float &bx, float &by, float &bz, float angle) {
		// Tourne le couple (a, b) dans son plan.
		const float c = std::cos(angle), sn = std::sin(angle);
		const float nax = ax * c + bx * sn, nay = ay * c + by * sn, naz = az * c + bz * sn;
		const float nbx = -ax * sn + bx * c, nby = -ay * sn + by * c, nbz = -az * sn + bz * c;
		ax = nax, ay = nay, az = naz, bx = nbx, by = nby, bz = nbz;
	};
	for (size_t i = 0; i < program.GetSize(); ++i) {
		const char c = program.CharAt(i);
		float angle = s.angleDegrees * 0.017453292f;
		if (s.angleJitter > 0.f)
			angle += float(rng.Range(-s.angleJitter, s.angleJitter)) * 0.017453292f;
		switch (c) {
			case 'F':
			case 'G':
			case 'f': {
				const float nx = t.px + t.hx * t.step, ny = t.py + t.hy * t.step, nz = t.pz + t.hz * t.step;
				if (c != 'f')
					out.push_back({t.px, t.py, t.pz, nx, ny, nz, t.thickness, t.depth});
				t.px = nx, t.py = ny, t.pz = nz;
				break;
			}
			case '+':
				rotate(t.hx, t.hy, t.hz, t.lx, t.ly, t.lz, angle);
				break;
			case '-':
				rotate(t.hx, t.hy, t.hz, t.lx, t.ly, t.lz, -angle);
				break;
			case '&':
				rotate(t.hx, t.hy, t.hz, t.ux, t.uy, t.uz, angle);
				break;
			case '^':
				rotate(t.hx, t.hy, t.hz, t.ux, t.uy, t.uz, -angle);
				break;
			case '\\':
				rotate(t.lx, t.ly, t.lz, t.ux, t.uy, t.uz, angle);
				break;
			case '/':
				rotate(t.lx, t.ly, t.lz, t.ux, t.uy, t.uz, -angle);
				break;
			case '|':
				rotate(t.hx, t.hy, t.hz, t.lx, t.ly, t.lz, 3.14159265f);
				break;
			case '!':
				t.thickness *= s.thinning;
				break;
			case '[':
				stack.push_back(t);
				t.thickness *= s.thinning;
				t.step *= s.stepScale;
				++t.depth;
				break;
			case ']':
				if (!stack.empty()) {
					t = stack.back();
					stack.pop_back();
				}
				break;
			default:
				break;
		}
	}
	return out;
}

} // namespace generators
