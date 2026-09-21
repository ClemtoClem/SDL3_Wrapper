#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "string_view.hpp"

// ---------------------------------------------------------------------------
// Types Unicode de base
// ---------------------------------------------------------------------------

/// Un codepoint Unicode (U+0000 … U+10FFFF).
using CodepointT = uint32_t;

/// Constante représentant un codepoint invalide (U+FFFD = replacement character).
inline constexpr CodepointT UNICODE_REPLACEMENT = 0xFFFD;
inline constexpr CodepointT UNICODE_MAX = 0x10FFFF;

// ---------------------------------------------------------------------------
// Validation UTF-8
// ---------------------------------------------------------------------------

namespace unicode {

/// Retourne true si la séquence d'octets est du UTF-8 valide.
[[nodiscard]] inline bool IsValidUtf8(StringView sv) noexcept {
	const auto *p = reinterpret_cast<const uint8_t *>(sv.GetData());
	const auto *end = p + sv.GetSize();
	while (p < end) {
		uint8_t b = *p++;
		int extra = 0;
		uint32_t minCp = 0;
		if (b < 0x80) {
			continue;
		} else if ((b & 0xE0) == 0xC0) {
			extra = 1;
			minCp = 0x80;
		} else if ((b & 0xF0) == 0xE0) {
			extra = 2;
			minCp = 0x800;
		} else if ((b & 0xF8) == 0xF0) {
			extra = 3;
			minCp = 0x10000;
		} else {
			return false;
		} // octet de continuation orphelin ou > 4 octets

		uint32_t cp = b & (0x3F >> extra);
		for (int i = 0; i < extra; ++i) {
			if (p >= end || (*p & 0xC0) != 0x80)
				return false;
			cp = (cp << 6) | (*p++ & 0x3F);
		}
		if (cp < minCp)
			return false; // sur-encodage
		if (cp > UNICODE_MAX)
			return false;
		if (cp >= 0xD800 && cp <= 0xDFFF)
			return false; // surrogates
	}
	return true;
}

/// Compte le nombre de codepoints Unicode (pas d'octets) dans une séquence UTF-8 valide.
[[nodiscard]] inline size_t CodepointCount(StringView sv) noexcept {
	size_t count = 0;
	const auto *p = reinterpret_cast<const uint8_t *>(sv.GetData());
	const auto *end = p + sv.GetSize();
	while (p < end) {
		// Saute les octets de continuation (10xxxxxx)
		if ((*p & 0xC0) != 0x80)
			++count;
		++p;
	}
	return count;
}

// Surcharges C++20 : const char8_t* ne se convertit plus implicitement en const char*
[[nodiscard]] inline bool IsValidUtf8(const char8_t *s) noexcept {
	return IsValidUtf8(StringView(reinterpret_cast<const char *>(s)));
}
[[nodiscard]] inline size_t CodepointCount(const char8_t *s) noexcept {
	return CodepointCount(StringView(reinterpret_cast<const char *>(s)));
}

// ---------------------------------------------------------------------------
// Itérateur codepoint UTF-8
// ---------------------------------------------------------------------------

/// Décode un codepoint à la position `p`, avance `p`.
/// Retourne UNICODE_REPLACEMENT si invalide.
[[nodiscard]] inline CodepointT DecodeNext(const uint8_t *&p, const uint8_t *end) noexcept {
	if (p >= end)
		return UNICODE_REPLACEMENT;

	uint8_t b = *p++;
	if (b < 0x80)
		return b;

	int extra = 0;
	uint32_t cp = 0;
	uint32_t minCp = 0;

	if ((b & 0xE0) == 0xC0) {
		extra = 1;
		cp = b & 0x1F;
		minCp = 0x80;
	} else if ((b & 0xF0) == 0xE0) {
		extra = 2;
		cp = b & 0x0F;
		minCp = 0x800;
	} else if ((b & 0xF8) == 0xF0) {
		extra = 3;
		cp = b & 0x07;
		minCp = 0x10000;
	} else {
		return UNICODE_REPLACEMENT;
	}

	for (int i = 0; i < extra; ++i) {
		if (p >= end || (*p & 0xC0) != 0x80)
			return UNICODE_REPLACEMENT;
		cp = (cp << 6) | (*p++ & 0x3F);
	}
	if (cp < minCp || cp > UNICODE_MAX)
		return UNICODE_REPLACEMENT;
	if (cp >= 0xD800 && cp <= 0xDFFF)
		return UNICODE_REPLACEMENT;
	return cp;
}

/// Encode un codepoint en UTF-8 et l'ajoute à `out` via `out.push_back(char)`.
/// Générique (`Sink` = std::string, String, ...) afin que ce module bas
/// niveau n'ait besoin de connaître ni std::string ni String.
template <typename Sink> inline void EncodeUtf8(CodepointT cp, Sink &out) {
	if (cp < 0x80) {
		out.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

/// Itérateur forward sur les codepoints d'une chaîne UTF-8.
/// cur_start pointe vers le début du codepoint courant (clé de comparaison).
/// pos pointe après le codepoint courant (après decode_next).
/// La comparaison se fait sur cur_start : quand il atteint end, l'itérateur
/// est égal à end() — le codepoint courant a déjà été rendu lors du tour précédent.
class CodepointIterator {
	const uint8_t *curStart; // début du codepoint courant (clé d'égalité)
	const uint8_t *pos;       // position après decode du codepoint courant
	const uint8_t *end;
	CodepointT current;

public:
	using IteratorCategory = std::forward_iterator_tag;
	using ValueType = CodepointT;
	using DifferenceType = std::ptrdiff_t;
	using Pointer = const CodepointT *;
	using Reference = CodepointT;

	explicit CodepointIterator(const char *p, const char *end)
		: curStart(reinterpret_cast<const uint8_t *>(p)), pos(reinterpret_cast<const uint8_t *>(p)),
		  end(reinterpret_cast<const uint8_t *>(end)), current(0) {
		if (reinterpret_cast<const uint8_t *>(pos) < reinterpret_cast<const uint8_t *>(end))
			current = DecodeNext(pos, reinterpret_cast<const uint8_t *>(end));
	}

	// Constructeur de fin
	explicit CodepointIterator(const char *end)
		: curStart(reinterpret_cast<const uint8_t *>(end)), pos(reinterpret_cast<const uint8_t *>(end)),
		  end(reinterpret_cast<const uint8_t *>(end)), current(0) {}

	CodepointT operator*() const noexcept { return current; }

	CodepointIterator &operator++() {
		curStart = pos; // avance la clé vers le prochain codepoint
		if (pos < end)
			current = DecodeNext(pos, end);
		else
			current = 0;
		return *this;
	}
	CodepointIterator operator++(int) {
		auto tmp = *this;
		++(*this);
		return tmp;
	}

	bool operator==(const CodepointIterator &o) const noexcept { return curStart == o.curStart; }
	bool operator!=(const CodepointIterator &o) const noexcept { return !(*this == o); }
};

/// Vue itérable sur les codepoints d'une chaîne UTF-8.
struct CodepointView {
	StringView sv;
	CodepointIterator Begin() const { return CodepointIterator(sv.GetData(), sv.GetData() + sv.GetSize()); }
	CodepointIterator End() const { return CodepointIterator(sv.GetData() + sv.GetSize()); }
	/// Alias requis par le for-range C++, qui ne résout begin/end que par ce nom exact.
	CodepointIterator begin() const { return Begin(); }
	CodepointIterator end() const { return End(); }
};

// ---------------------------------------------------------------------------
// Conversions UTF-8 ↔ UTF-16 ↔ UTF-32
// ---------------------------------------------------------------------------

/// Convertit une chaîne UTF-8 en UTF-16 (little-endian, avec surrogates si nécessaire).
[[nodiscard]] inline std::u16string ToUtf16(StringView utf8) {
	std::u16string result;
	result.reserve(utf8.GetSize()); // estimation grossière
	const auto *p = reinterpret_cast<const uint8_t *>(utf8.GetData());
	const auto *end = p + utf8.GetSize();
	while (p < end) {
		CodepointT cp = DecodeNext(p, end);
		if (cp == UNICODE_REPLACEMENT) {
			result += static_cast<char16_t>(0xFFFD);
			continue;
		}
		if (cp < 0x10000) {
			result += static_cast<char16_t>(cp);
		} else {
			// Paire surrogate
			cp -= 0x10000;
			result += static_cast<char16_t>(0xD800 | (cp >> 10));
			result += static_cast<char16_t>(0xDC00 | (cp & 0x3FF));
		}
	}
	return result;
}

/// Convertit une chaîne UTF-8 en UTF-32.
[[nodiscard]] inline std::u32string ToUtf32(StringView utf8) {
	std::u32string result;
	result.reserve(utf8.GetSize());
	const auto *p = reinterpret_cast<const uint8_t *>(utf8.GetData());
	const auto *end = p + utf8.GetSize();
	while (p < end)
		result += static_cast<char32_t>(DecodeNext(p, end));
	return result;
}

/// Convertit une chaîne UTF-16 en UTF-8.
[[nodiscard]] inline std::string FromUtf16(std::u16string_view utf16) {
	std::string result;
	result.reserve(utf16.size() * 3 / 2);
	for (size_t i = 0; i < utf16.size();) {
		char16_t c = utf16[i++];
		CodepointT cp;
		if (c >= 0xD800 && c <= 0xDBFF) {
			// Surrogate haut — attend un surrogate bas
			if (i < utf16.size() && utf16[i] >= 0xDC00 && utf16[i] <= 0xDFFF) {
				cp = 0x10000 + ((CodepointT(c) - 0xD800) << 10) + (utf16[i++] - 0xDC00);
			} else {
				cp = UNICODE_REPLACEMENT;
			}
		} else if (c >= 0xDC00 && c <= 0xDFFF) {
			cp = UNICODE_REPLACEMENT; // surrogate bas orphelin
		} else {
			cp = static_cast<CodepointT>(c);
		}
		EncodeUtf8(cp, result);
	}
	return result;
}

/// Convertit une chaîne UTF-32 en UTF-8.
[[nodiscard]] inline std::string FromUtf32(std::u32string_view utf32) {
	std::string result;
	result.reserve(utf32.size() * 2);
	for (char32_t c : utf32)
		EncodeUtf8(static_cast<CodepointT>(c), result);
	return result;
}

/// Retourne le codepoint à l'index `n` (O(n) — parcours séquentiel).
[[nodiscard]] inline CodepointT CodepointAt(StringView sv, size_t n) {
	const auto *p = reinterpret_cast<const uint8_t *>(sv.GetData());
	const auto *end = p + sv.GetSize();
	for (size_t i = 0; i < n && p < end; ++i) {
		[[maybe_unused]] auto _ = DecodeNext(p, end);
	}
	if (p >= end)
		return UNICODE_REPLACEMENT;
	return DecodeNext(p, end);
}

/// Retourne la position en octets du n-ième codepoint.
[[nodiscard]] inline size_t ByteOffsetOf(StringView sv, size_t n) {
	const auto *start = reinterpret_cast<const uint8_t *>(sv.GetData());
	const auto *p = start;
	const auto *end = p + sv.GetSize();
	for (size_t i = 0; i < n && p < end; ++i) {
		[[maybe_unused]] auto _ = DecodeNext(p, end);
	}
	return static_cast<size_t>(p - start);
}

// ---------------------------------------------------------------------------
// Transformations case-folding Unicode (BMP — plage Latin/Latin-Extended)
// ---------------------------------------------------------------------------

/// Convertit un codepoint en minuscule (couverture basique : ASCII + Latin-1).
[[nodiscard]] inline CodepointT ToLowerCp(CodepointT cp) noexcept {
	// ASCII A-Z
	if (cp >= 0x41 && cp <= 0x5A)
		return cp + 0x20;
	// Latin-1 supplement (À-Ö, Ø-Þ)
	if (cp >= 0xC0 && cp <= 0xD6)
		return cp + 0x20;
	if (cp >= 0xD8 && cp <= 0xDE)
		return cp + 0x20;
	// Quelques cas Latin Extended-A courants
	if (cp >= 0x100 && cp <= 0x12E && (cp & 1) == 0)
		return cp + 1;
	return cp;
}

/// Convertit un codepoint en majuscule (couverture basique : ASCII + Latin-1).
[[nodiscard]] inline CodepointT ToUpperCp(CodepointT cp) noexcept {
	if (cp >= 0x61 && cp <= 0x7A)
		return cp - 0x20;
	if (cp >= 0xE0 && cp <= 0xF6)
		return cp - 0x20;
	if (cp >= 0xF8 && cp <= 0xFE)
		return cp - 0x20;
	if (cp >= 0x101 && cp <= 0x12F && (cp & 1) == 1)
		return cp - 1;
	return cp;
}

/// Retourne true si le codepoint est considéré comme un espace Unicode.
[[nodiscard]] inline bool IsUnicodeSpace(CodepointT cp) noexcept {
	switch (cp) {
	case 0x0009:
	case 0x000A:
	case 0x000B:
	case 0x000C:
	case 0x000D:
	case 0x0020:
	case 0x00A0:
	case 0x1680:
	case 0x2000:
	case 0x2001:
	case 0x2002:
	case 0x2003:
	case 0x2004:
	case 0x2005:
	case 0x2006:
	case 0x2007:
	case 0x2008:
	case 0x2009:
	case 0x200A:
	case 0x2028:
	case 0x2029:
	case 0x202F:
	case 0x205F:
	case 0x3000:
	case 0xFEFF: // BOM / zero-width no-break space
		return true;
	default:
		return false;
	}
}

} // namespace unicode