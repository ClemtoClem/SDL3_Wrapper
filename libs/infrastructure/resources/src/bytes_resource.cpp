// Définitions de resources/bytes_resource.hpp
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
