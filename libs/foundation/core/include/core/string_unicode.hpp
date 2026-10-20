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
[[nodiscard]] bool IsValidUtf8(StringView sv) noexcept;

/// Compte le nombre de codepoints Unicode (pas d'octets) dans une séquence UTF-8 valide.
[[nodiscard]] size_t CodepointCount(StringView sv) noexcept;

// Surcharges C++20 : const char8_t* ne se convertit plus implicitement en const char*
[[nodiscard]] bool IsValidUtf8(const char8_t *s) noexcept;
[[nodiscard]] size_t CodepointCount(const char8_t *s) noexcept;

// ---------------------------------------------------------------------------
// Itérateur codepoint UTF-8
// ---------------------------------------------------------------------------

/// Décode un codepoint à la position `p`, avance `p`.
/// Retourne UNICODE_REPLACEMENT si invalide.
[[nodiscard]] CodepointT DecodeNext(const uint8_t *&p, const uint8_t *end) noexcept;

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

	explicit CodepointIterator(const char *p, const char *end);

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
[[nodiscard]] std::u16string ToUtf16(StringView utf8);

/// Convertit une chaîne UTF-8 en UTF-32.
[[nodiscard]] std::u32string ToUtf32(StringView utf8);

/// Convertit une chaîne UTF-16 en UTF-8.
[[nodiscard]] std::string FromUtf16(std::u16string_view utf16);

/// Convertit une chaîne UTF-32 en UTF-8.
[[nodiscard]] std::string FromUtf32(std::u32string_view utf32);

/// Retourne le codepoint à l'index `n` (O(n) — parcours séquentiel).
[[nodiscard]] CodepointT CodepointAt(StringView sv, size_t n);

/// Retourne la position en octets du n-ième codepoint.
[[nodiscard]] size_t ByteOffsetOf(StringView sv, size_t n);

// ---------------------------------------------------------------------------
// Transformations case-folding Unicode (BMP — plage Latin/Latin-Extended)
// ---------------------------------------------------------------------------

/// Convertit un codepoint en minuscule (couverture basique : ASCII + Latin-1).
[[nodiscard]] CodepointT ToLowerCp(CodepointT cp) noexcept;

/// Convertit un codepoint en majuscule (couverture basique : ASCII + Latin-1).
[[nodiscard]] CodepointT ToUpperCp(CodepointT cp) noexcept;

/// Retourne true si le codepoint est considéré comme un espace Unicode.
[[nodiscard]] bool IsUnicodeSpace(CodepointT cp) noexcept;

} // namespace unicode