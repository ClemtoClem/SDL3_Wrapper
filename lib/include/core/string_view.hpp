#pragma once
/**
 * @file string_view.hpp
 * @brief StringView — vue non-possédante sur une séquence d'octets (char),
 * équivalent maison de std::string_view.
 *
 * @details
 * Ne fait aucune allocation et ne possède pas les données référencées : la
 * durée de vie du buffer sous-jacent doit couvrir celle de la vue (mêmes
 * règles que std::string_view). Sert de type de vue unique pour `String`
 * (string.hpp) et pour le module Unicode (string_unicode.hpp), afin que ce
 * sous-système ne dépende ni de std::string ni de std::string_view.
 */
#include <cstddef>
#include <cstring>
#include <ostream>

class StringView {
public:
	static constexpr size_t NPOS = static_cast<size_t>(-1);

	constexpr StringView() noexcept = default;
	constexpr StringView(const char *data, size_t size) noexcept : m_data(data), m_size(size) {}

	// Non-constexpr : repose sur strlen (chaîne de longueur dynamique, non connue à la compilation).
	StringView(const char *cstr) noexcept : m_data(cstr), m_size(cstr ? std::strlen(cstr) : 0) {}

	[[nodiscard]] constexpr const char *GetData() const noexcept { return m_data; }
	[[nodiscard]] constexpr const char *CStr() const noexcept { return m_data; }
	[[nodiscard]] constexpr size_t GetSize() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool IsEmpty() const noexcept { return m_size == 0; }

	[[nodiscard]] constexpr char operator[](size_t i) const noexcept { return m_data[i]; }
	[[nodiscard]] constexpr char Front() const noexcept { return m_data[0]; }
	[[nodiscard]] constexpr char Back() const noexcept { return m_data[m_size - 1]; }

	[[nodiscard]] constexpr const char *Begin() const noexcept { return m_data; }
	[[nodiscard]] constexpr const char *End() const noexcept { return m_data + m_size; }

	constexpr void RemovePrefix(size_t n) noexcept {
		m_data += n;
		m_size -= n;
	}
	constexpr void RemoveSuffix(size_t n) noexcept { m_size -= n; }

	[[nodiscard]] constexpr StringView Substr(size_t pos, size_t len = NPOS) const noexcept {
		if (pos > m_size)
			pos = m_size;
		size_t avail = m_size - pos;
		return StringView(m_data + pos, len < avail ? len : avail);
	}

	[[nodiscard]] int Compare(StringView o) const noexcept {
		size_t n = m_size < o.m_size ? m_size : o.m_size;
		int r = n ? std::memcmp(m_data, o.m_data, n) : 0;
		if (r != 0)
			return r;
		if (m_size == o.m_size)
			return 0;
		return m_size < o.m_size ? -1 : 1;
	}

	[[nodiscard]] size_t Find(StringView needle, size_t from = 0) const noexcept {
		if (from > m_size)
			return NPOS;
		if (needle.IsEmpty())
			return from;
		if (needle.m_size > m_size - from)
			return NPOS;
		size_t last = m_size - needle.m_size;
		for (size_t i = from; i <= last; ++i)
			if (std::memcmp(m_data + i, needle.m_data, needle.m_size) == 0)
				return i;
		return NPOS;
	}
	[[nodiscard]] size_t Find(char c, size_t from = 0) const noexcept {
		for (size_t i = from; i < m_size; ++i)
			if (m_data[i] == c)
				return i;
		return NPOS;
	}

	[[nodiscard]] size_t Rfind(StringView needle, size_t from = NPOS) const noexcept {
		if (needle.m_size > m_size)
			return NPOS;
		size_t last = m_size - needle.m_size;
		if (from < last)
			last = from;
		for (size_t i = last;; --i) {
			if (std::memcmp(m_data + i, needle.m_data, needle.m_size) == 0)
				return i;
			if (i == 0)
				break;
		}
		return NPOS;
	}
	[[nodiscard]] size_t Rfind(char c, size_t from = NPOS) const noexcept {
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

	[[nodiscard]] bool StartsWith(StringView p) const noexcept {
		return m_size >= p.m_size && std::memcmp(m_data, p.m_data, p.m_size) == 0;
	}
	[[nodiscard]] bool EndsWith(StringView s) const noexcept {
		return m_size >= s.m_size && std::memcmp(m_data + (m_size - s.m_size), s.m_data, s.m_size) == 0;
	}
	[[nodiscard]] bool Contains(StringView s) const noexcept { return Find(s) != NPOS; }
	[[nodiscard]] bool Contains(char c) const noexcept { return Find(c) != NPOS; }

	friend bool operator==(StringView a, StringView b) noexcept { return a.Compare(b) == 0; }
	friend bool operator!=(StringView a, StringView b) noexcept { return a.Compare(b) != 0; }
	friend bool operator<(StringView a, StringView b) noexcept { return a.Compare(b) < 0; }
	friend bool operator<=(StringView a, StringView b) noexcept { return a.Compare(b) <= 0; }
	friend bool operator>(StringView a, StringView b) noexcept { return a.Compare(b) > 0; }
	friend bool operator>=(StringView a, StringView b) noexcept { return a.Compare(b) >= 0; }

	friend std::ostream &operator<<(std::ostream &os, StringView sv) {
		return os.write(sv.GetData(), static_cast<std::streamsize>(sv.GetSize()));
	}

private:
	const char *m_data = nullptr;
	size_t m_size = 0;
};
