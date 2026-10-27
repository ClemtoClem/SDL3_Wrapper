#pragma once
/**
 * Formula — expressions mathématiques saisies par le joueur (constructions
 * à partir d'une formule), compilées une fois puis évaluées très vite, sans
 * passer par le langage de script (aucun accès au jeu : sûr par nature).
 *
 *   nombres        12  3.5  1e-3        constantes   pi  e  tau
 *   variables      celles données à la compilation (x, y, z, u, v, t…)
 *   opérateurs     + - * / % ^ (puissance, associative à droite), - unaire
 *   comparaisons   < <= > >= == != (1 ou 0)
 *   logique        and or not (ou && || !) — 0 est faux, le reste vrai
 *   fonctions      sin cos tan asin acos atan atan2 sinh cosh tanh sqrt cbrt
 *                  abs floor ceil round trunc frac sign exp log ln log2 log10
 *                  min max pow hypot clamp mod if(cond, a, b)
 *
 * Exemples : « x^2 + y^2 + z^2 <= 20^2 », « sqrt(x^2+y^2) - 10 »,
 * « abs(x) + abs(y) <= 10 - z and z >= 0 ».
 */
#include <vector>

#include <core/core.hpp>

namespace voxel {

class Formula {
public:
	/// Compile `text` ; `variables` : noms acceptés, dans l'ordre des valeurs
	/// passées à Eval. Erreur lisible (« position 7 : “)” attendu »).
	[[nodiscard]] static Result<Formula, String> Compile(const String &text, const std::vector<String> &variables);

	/// Valeur pour ces variables (autant que de noms à la compilation).
	/// Une comparaison vaut 1 ou 0 ; NaN si le calcul n'a pas de sens.
	[[nodiscard]] double Eval(const double *values) const;
	[[nodiscard]] double Eval(std::initializer_list<double> values) const { return Eval(values.begin()); }

	/// Vrai si l'expression est une condition (comparaison, and/or/not) :
	/// « région pleine » plutôt que « surface où l'expression s'annule ».
	[[nodiscard]] bool IsCondition() const noexcept { return m_condition; }
	[[nodiscard]] const String &Text() const noexcept { return m_text; }

	enum class Op : uint8_t;
	struct Instr {
		Op op;
		uint8_t arity = 0;
		uint16_t index = 0; ///< variable
		double value = 0.0; ///< constante
	};

private:
	std::vector<Instr> m_code; ///< notation polonaise inverse
	String m_text;
	bool m_condition = false;
	size_t m_stack = 1;
};

} // namespace voxel
