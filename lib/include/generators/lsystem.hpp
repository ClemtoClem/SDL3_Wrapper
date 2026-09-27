#pragma once
/**
 * generators — L-systèmes (Lindenmayer) : réécriture de chaînes, puis
 * interprétation « tortue » en segments 3D — plantes, arbres, fractales.
 *
 *   axiome "F", règle F → "F[+F]F[-F]F", angle 25°, 4 itérations : une herbe.
 *
 * Règles STOCHASTIQUES : plusieurs productions pour un même symbole, avec
 * des poids (tirage reproductible par graine).
 *
 * Alphabet de la tortue :
 *   F G  avancer en traçant      f  avancer sans tracer
 *   + -  tourner autour de Y (lacet)       & ^  tanguer (autour de X)
 *   \ /  rouler (autour de Z)              |    demi-tour
 *   [ ]  empiler / dépiler l'état          !    affiner le trait (× thinning)
 *   Les autres symboles ne dessinent rien (variables de réécriture).
 */
#include <cmath>
#include <vector>

#include "../core/core.hpp"
#include "random.hpp"

namespace generators {

struct LRule {
	char symbol = 'F';
	String replacement;
	float weight = 1.f;
};

/// Applique `iterations` fois les règles ; `maxLength` borne la chaîne
/// (croissance exponentielle : on s'arrête AVANT de l'atteindre).
[[nodiscard]] String ExpandLSystem(const String &axiom, const std::vector<LRule> &rules, int iterations,
										  uint64_t seed = 1, size_t maxLength = 2000000);

struct TurtleSettings {
	float step = 1.f;
	float angleDegrees = 25.f;
	float thickness = 0.1f;
	float thinning = 0.7f;   ///< facteur appliqué par `!` et à chaque `[`
	float stepScale = 1.f;   ///< facteur de longueur à chaque `[` (branches plus courtes)
	float angleJitter = 0.f; ///< variation aléatoire des angles (degrés)
	uint64_t seed = 1;
};

/// Segment tracé : de `a` à `b`, épaisseur, profondeur de branche.
struct TurtleSegment {
	float ax, ay, az, bx, by, bz;
	float thickness;
	int depth;
};

/// Interprète la chaîne : la tortue part de l'origine, vers +Y (le haut).
[[nodiscard]] std::vector<TurtleSegment> InterpretTurtle(const String &program, const TurtleSettings &s = {});

} // namespace generators
