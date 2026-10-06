// Définitions de core/string_view.hpp
#include "core/string_view.hpp"

// ── StringView ───────────────────────────────────────────────────────────────

size_t StringView::Find(char c, size_t from) const noexcept {
	for (size_t i = from; i < m_size; ++i)
		if (m_data[i] == c)
			return i;
	return NPOS;
}

size_t StringView::Rfind(char c, size_t from) const noexcept {
	if (m_size == 0)
		return NPOS;
	size_t start = (from < m_size) ? from : m_size - 1;
	for (size_t i = start;; --i) {
		if (m_data[i] == c)
			return i;
		if (i == 0)
			break;
	}
	return NPOS;
}
