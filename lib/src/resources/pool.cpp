// Définitions de resources/pool.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "resources/pool.hpp"

namespace resources {

// ── Registry ─────────────────────────────────────────────────────────────────

void Registry::ClearAll() {
	std::scoped_lock lk(mu);
	for (auto &[_, s] : slots)
		s->Clear();
}

} // namespace resources
