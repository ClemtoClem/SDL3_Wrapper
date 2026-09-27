#pragma once
/**
 * generators — noms procéduraux par chaîne de Markov de caractères : on
 * apprend, sur une liste d'exemples (villes, héros, planètes), quelle lettre
 * suit quelles `order` lettres ; on génère ensuite des noms qui « sonnent »
 * pareil sans en être des copies. Octets (les accents UTF-8 restent groupés
 * tant que l'ordre est ≥ 2).
 */
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

#include "../core/core.hpp"
#include "random.hpp"

namespace generators {

class MarkovNames {
public:
	MarkovNames(const std::vector<String> &examples, int order = 3);

	[[nodiscard]] bool Empty() const noexcept { return m_table.empty(); }

	/// Un nom de `minLength` à `maxLength` octets, différent des exemples
	/// (si possible, en `attempts` essais). Vide si rien n'a été appris.
	[[nodiscard]] String Generate(Rng &rng, int minLength = 4, int maxLength = 12, int attempts = 100) const;

private:
	static constexpr char START = '\x02';
	static constexpr char END = '\x03';
	int m_order;
	std::map<std::string, std::vector<char>> m_table;
	std::vector<std::string> m_examples;
};

} // namespace generators
