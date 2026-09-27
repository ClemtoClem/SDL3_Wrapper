// Définitions de resources/bytes_resource.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "resources/bytes_resource.hpp"

namespace resources {

// ── BytesResource ────────────────────────────────────────────────────────────

Result<bool, String> BytesResource::DoLoad() {
	auto result = sdl3::ReadFile(m_path);
	if (!result.IsOk())
		return Err(String(result.Error()));
	m_bytes = std::move(result.Value());
	return Ok(true);
}

} // namespace resources
