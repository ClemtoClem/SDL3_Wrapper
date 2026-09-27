// Définitions de generators/markov.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "generators/markov.hpp"

namespace generators {

// ── MarkovNames ──────────────────────────────────────────────────────────────

MarkovNames::MarkovNames(const std::vector<String> &examples, int order) : m_order(std::clamp(order, 1, 8)) {
	for (const String &example : examples) {
		if (example.IsEmpty())
			continue;
		m_examples.emplace_back(example.CStr(), example.GetSize());
		const std::string padded = std::string(size_t(m_order), START) + m_examples.back() + END;
		for (size_t i = 0; i + size_t(m_order) < padded.size(); ++i)
			m_table[padded.substr(i, size_t(m_order))].push_back(padded[i + size_t(m_order)]);
	}
}

String MarkovNames::Generate(Rng &rng, int minLength, int maxLength, int attempts) const {
	if (m_table.empty())
		return String();
	std::string best;
	for (int attempt = 0; attempt < attempts; ++attempt) {
		std::string name, context(size_t(m_order), START);
		while (int(name.size()) < maxLength + 1) {
			auto it = m_table.find(context);
			if (it == m_table.end())
				break;
			const char next = it->second[size_t(rng.Int(0, int64_t(it->second.size()) - 1))];
			if (next == END)
				break;
			name.push_back(next);
			context = context.substr(1) + next;
		}
		if (int(name.size()) < minLength || int(name.size()) > maxLength)
			continue;
		best = name;
		if (std::find(m_examples.begin(), m_examples.end(), name) == m_examples.end())
			break;
	}
	if (!best.empty())
		best[0] = char(std::toupper(static_cast<unsigned char>(best[0])));
	return String(best.data(), best.size());
}

} // namespace generators
