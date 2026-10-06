// Définitions de resources/pool.hpp
#include "resources/pool.hpp"

namespace resources {

// ── Registry ─────────────────────────────────────────────────────────────────

void Registry::ClearAll() {
	std::scoped_lock lk(mu);
	for (auto &[_, s] : slots)
		s->Clear();
}

} // namespace resources
