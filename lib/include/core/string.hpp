#pragma once
/**
 * @file string.hpp
 * @brief Classe String unifiée — UTF-8 natif, API Arduino + std::string + fonctionnel.
 *
 * @details
 * Stockage interne : buffer d'octets maison (`char*`/taille/capacité), pas
 * `std::string` — voir string_view.hpp pour la vue non-possédante associée
 * (`StringView`, équivalent maison de std::string_view). `String` ne
 * conserve donc aucune dépendance de stockage à std::string/std::string_view ;
 * elle garde uniquement un constructeur d'interop `String(const std::string&)`
 * pour accepter le résultat d'API std:: tierces (ex. les conversions
 * UTF-16/32 ci-dessous, qui restent en std::u16string/std::u32string, hors
 * périmètre de cette réécriture).
 *
 * Toutes les méthodes travaillent en **octets** par défaut pour la performance.
 * Les méthodes Unicode (codepoints) sont préfixées `u_` ou accessibles via `codepoints()`.
 *
 * ## Compatibilité
 * | Source          | Méthodes disponibles                                      |
 * |-----------------|-----------------------------------------------------------|
 * | Arduino String  | concat, indexOf, startsWith, endsWith, trim, replace, ... |
 * | std::string     | find, rfind, substr, begin/end, c_str, size, ...          |
 * | Propre          | map, filter, split, join, pad, repeat, pipe (`|`)         |
 * | Unicode         | u_length, u_char_at, codepoints(), to_utf16, to_utf32     |
 *
 * ## Exemple
 * @code{.cpp}
 * String s = "  Héllo, Wörld!  ";
 *
 * // Chaînage fluent
 * String r = s.trim().toLower().replace("ö", "o");
 *
 * // Opérateur pipe (fonctionnel)
 * auto shout = [](String s){ return s.toUpper().Append("!"); };
 * String r2 = String("hello") | shout;
 *
 * // Split / join
 * auto parts = String("a,b,c").split(",");
 * String back = String::join(parts, "-");  // "a-b-c"
 *
 * // Conversion de types
 * String n = String::From(3.14f, 4);  // "3.1400"
 * int    i = String("42").toInt();
 *
 * // Unicode
 * String emoji = u8"Héllo 🌍";
 * size_t cp_len = emoji.u_length();  // en codepoints
 * for (auto cp : emoji.codepoints()) { ... }
 * auto utf16 = emoji.ToUtf16();
 * @endcode
 */

#include "option.hpp"
#include "string_unicode.hpp"
#include "string_view.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers internes
// ---------------------------------------------------------------------------

namespace detail {

/// Convertit une magnitude non-signée en base donnée (2…36) dans `out` (non
/// null-terminé). Retourne la longueur écrite.
inline size_t UintToBase(unsigned long long uval, int base, char *out) {
	if (uval == 0) {
		out[0] = '0';
		return 1;
	}
	char tmp[70];
	int n = 0;
	while (uval > 0) {
		int d = static_cast<int>(uval % static_cast<unsigned long long>(base));
		tmp[n++] = (d < 10) ? static_cast<char>('0' + d) : static_cast<char>('a' + d - 10);
		uval /= static_cast<unsigned long long>(base);
	}
	size_t len = 0;
	while (n > 0)
		out[len++] = tmp[--n];
	return len;
}

/// Convertit un entier signé en base donnée (2…36) dans `out` (au moins 72
/// octets). Retourne la longueur écrite (0 si base invalide).
inline size_t IntToBase(long long value, int base, char *out) {
	if (base < 2 || base > 36)
		return 0;
	if (value < 0) {
		out[0] = '-';
		// Évite l'UB de -LLONG_MIN : (value+1) est représentable, puis on
		// ré-ajoute 1 après négation/conversion en non-signé.
		unsigned long long uval = static_cast<unsigned long long>(-(value + 1)) + 1;
		return 1 + UintToBase(uval, base, out + 1);
	}
	return UintToBase(static_cast<unsigned long long>(value), base, out);
}

/// Convertit un flottant en chaîne avec `decimals` décimales dans `out`.
inline size_t FloatToStr(double value, int decimals, char *out, size_t outCap) {
	int n = std::snprintf(out, outCap, "%.*f", decimals, value);
	return n > 0 ? static_cast<size_t>(n) : 0;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Classe String
// ---------------------------------------------------------------------------

class String {
public:
	// =========================================================================
	// Constructeurs & assignation
	// =========================================================================

	String() noexcept = default;

	String(const char *cstr) { Assign(cstr, cstr ? std::strlen(cstr) : 0); }
	String(const char *m_data, size_t len) { Assign(m_data, len); }
	String(const char8_t *cstr) : String(cstr ? reinterpret_cast<const char *>(cstr) : "") {}
	String(StringView sv) { Assign(sv.GetData(), sv.GetSize()); }
	String(char c) { Assign(&c, 1); }
	String(size_t count, char c) {
		EnsureCapacity(count);
		if (count)
			std::memset(m_data, static_cast<unsigned char>(c), count);
		SetLength(count);
	}

	/// Interop : accepte un std::string externe (ex. valeurs renvoyées par
	/// des API std:: tierces comme unicode::FromUtf16, ou par du code
	/// C++ standard). Copie immédiatement les octets — ne conserve aucune
	/// dépendance de stockage à std::string ensuite.
	String(const std::string &s) { Assign(s.data(), s.size()); }

	String(const String &other) { Assign(other.m_data, other.m_size); }
	String(String &&other) noexcept : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity) {
		other.m_data = nullptr;
		other.m_size = 0;
		other.m_capacity = 0;
	}

	~String() { delete[] m_data;}

	String &operator=(const String &other) {
		if (this != &other)
			Assign(other.m_data, other.m_size);
		return *this;
	}
	String &operator=(String &&other) noexcept {
		if (this != &other) {
			delete[] m_data;
			m_data = other.m_data;
			m_size = other.m_size;
			m_capacity = other.m_capacity;
			other.m_data = nullptr;
			other.m_size = 0;
			other.m_capacity = 0;
		}
		return *this;
	}

	String &operator=(const char *cstr) {
		Assign(cstr, cstr ? std::strlen(cstr) : 0);
		return *this;
	}
	String &operator=(StringView sv) {
		Assign(sv.GetData(), sv.GetSize());
		return *this;
	}
	String &operator=(char c) {
		Assign(&c, 1);
		return *this;
	}

	// =========================================================================
	// Factories — conversions depuis types numériques
	// =========================================================================

	/// Construit depuis un entier, dans la base donnée (2-36).
	static String From(long long value, int base = 10) {
		if (base < 2 || base > 36)
			return {};
		char buf[72];
		size_t n = detail::IntToBase(value, base, buf);
		return String(buf, n);
	}
	static String From(int value, int base = 10) { return From(static_cast<long long>(value), base); }
	static String From(long value, int base = 10) { return From(static_cast<long long>(value), base); }
	static String From(unsigned long long v, int base = 10) {
		if (base < 2 || base > 36)
			return {};
		char buf[72];
		size_t n = detail::UintToBase(v, base, buf);
		return String(buf, n);
	}
	static String From(unsigned int v, int base = 10) { return From(static_cast<unsigned long long>(v), base); }
	static String From(unsigned long v, int base = 10) { return From(static_cast<unsigned long long>(v), base); }

	/// Construit depuis un flottant avec un nombre de décimales.
	static String From(double value, int decimals = 2) {
		char buf[64];
		size_t n = detail::FloatToStr(value, decimals, buf, sizeof(buf));
		return String(buf, n);
	}
	static String From(float value, int decimals = 2) { return From(static_cast<double>(value), decimals); }

	/// Construit depuis un booléen.
	static String From(bool value) { return String(value ? "true" : "false"); }

	/// Construit depuis le contenu déjà accumulé d'un flux std:: (ostringstream
	/// / stringstream) — permet de continuer à utiliser <sstream> ponctuellement
	/// (ex. formatage de flottant identique à std::ostream) tout en ramenant
	/// le résultat dans une String, sans que String ne dépende autrement de
	/// std::string en tant que représentation de stockage.
	static String From(const std::ostringstream &ss) { return String(ss.str()); }
	static String From(const std::stringstream &ss) { return String(ss.str()); }

	/// Sérialise `value` via operator<<(std::ostream&, T) — pratique pour
	/// obtenir exactement le même format qu'un std::ostringstream (ex. la
	/// précision par défaut d'un double, ou un manipulateur std:: comme
	/// std::setprecision/std::hex) sans écrire le flux à la main à chaque
	/// site d'appel.
	/// @code{.cpp}
	/// String s = String::FromStream(3.14159);                     // "3.14159"
	/// String h = String::FromStream(std::hex, 255);                // "ff"
	/// @endcode
	template <typename... Args> static String FromStream(Args &&...args) {
		std::ostringstream oss;
		(oss << ... << std::forward<Args>(args));
		return String(oss.str());
	}

	/// Construit depuis des codepoints UTF-32.
	static String FromUtf32(std::u32string_view utf32) { return String(unicode::FromUtf32(utf32)); }

	/// Construit depuis une chaîne UTF-16.
	static String FromUtf16(std::u16string_view utf16) { return String(unicode::FromUtf16(utf16)); }

	/// Format printf-style.
	template <typename... Args> static String Format(const char *fmt, Args... args) {
		int len = std::snprintf(nullptr, 0, fmt, args...);
		if (len <= 0)
			return {};
		String result;
		result.EnsureCapacity(static_cast<size_t>(len));
		std::snprintf(result.m_data, static_cast<size_t>(len) + 1, fmt, args...);
		result.SetLength(static_cast<size_t>(len));
		return result;
	}

	// =========================================================================
	// Accès au stockage sous-jacent
	// =========================================================================

	/// Construit un std::istringstream lisant une copie du contenu — utile
	/// pour réutiliser ponctuellement les extracteurs std:: (`>>`,
	/// manipulateurs) sur le contenu d'une String. Copie les octets (un
	/// istringstream possède son propre buffer), donc reste valide même si
	/// la String source est ensuite modifiée ou détruite.
	[[nodiscard]] std::istringstream Stream() const { return std::istringstream(std::string(CStr(), m_size)); }

	[[nodiscard]] const char *CStr() const noexcept { return m_data ? m_data : ""; }
	/// Alias std::string-compat de CStr (cf. Begin/begin, End/end, PushBack/push_back).
	[[nodiscard]] const char *c_str() const noexcept { return CStr(); }
	[[nodiscard]] StringView View() const noexcept { return StringView(CStr(), m_size); }

	[[nodiscard]] size_t GetSize() const noexcept { return m_size; }
	/// Alias std::string-compat de Size.
	[[nodiscard]] size_t size() const noexcept { return m_size; }
	[[nodiscard]] size_t Length() const noexcept { return m_size; }
	[[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }

	// Compatibilité Arduino : conversion bool implicite
	[[nodiscard]] explicit operator bool() const noexcept { return m_size != 0; }

	// =========================================================================
	// Itérateurs (octets — compatibilité std::string)
	// =========================================================================

	[[nodiscard]] char *Begin() noexcept { return m_data ? m_data : EmptySentinel(); }
	[[nodiscard]] char *End() noexcept { return Begin() + m_size; }
	[[nodiscard]] const char *Begin() const noexcept { return CStr(); }
	[[nodiscard]] const char *End() const noexcept { return CStr() + m_size; }
	[[nodiscard]] const char *Cbegin() const noexcept { return Begin(); }
	[[nodiscard]] const char *Cend() const noexcept { return End(); }

	[[nodiscard]] std::reverse_iterator<char *> Rbegin() { return std::reverse_iterator<char *>(End()); }
	[[nodiscard]] std::reverse_iterator<char *> Rend() { return std::reverse_iterator<char *>(Begin()); }
	[[nodiscard]] std::reverse_iterator<const char *> Rbegin() const {
		return std::reverse_iterator<const char *>(End());
	}
	[[nodiscard]] std::reverse_iterator<const char *> Rend() const {
		return std::reverse_iterator<const char *>(Begin());
	}

	/// Alias std::string-compat de Begin/End, requis par le for-range C++
	/// (`for (auto &c : s)`), qui ne résout `begin`/`end` que par ce nom exact.
	[[nodiscard]] char *begin() noexcept { return Begin(); }
	[[nodiscard]] char *end() noexcept { return End(); }
	[[nodiscard]] const char *begin() const noexcept { return Begin(); }
	[[nodiscard]] const char *end() const noexcept { return End(); }

	// =========================================================================
	// Accès aux caractères (octets)
	// =========================================================================

	char operator[](size_t i) const noexcept { return m_data[i]; }
	char &operator[](size_t i) noexcept { return m_data[i]; }

	char CharAt(size_t i) const noexcept { return (i < m_size) ? m_data[i] : '\0'; }
	void SetCharAt(size_t i, char c) {
		if (i < m_size)
			m_data[i] = c;
	}

	[[nodiscard]] char Front() const noexcept { return m_data[0]; }
	[[nodiscard]] char Back() const noexcept { return m_data[m_size - 1]; }

	// =========================================================================
	// Comparaison
	// =========================================================================

	[[nodiscard]] int Compare(const String &other) const noexcept { return View().Compare(other.View()); }
	[[nodiscard]] int Compare(const char *cstr) const noexcept { return View().Compare(StringView(cstr)); }
	[[nodiscard]] bool Equals(const String &other) const noexcept { return View() == other.View(); }
	[[nodiscard]] bool Equals(const char *cstr) const noexcept { return View() == StringView(cstr); }

	[[nodiscard]] bool EqualsIgnoreCase(const String &other) const noexcept {
		if (m_size != other.m_size)
			return false;
		for (size_t i = 0; i < m_size; ++i)
			if (std::tolower(static_cast<uint8_t>(m_data[i])) != std::tolower(static_cast<uint8_t>(other.m_data[i])))
				return false;
		return true;
	}
	[[nodiscard]] int CompareTo(const String &other) const noexcept { return Compare(other); }
	[[nodiscard]] int CompareTo(const char *cstr) const noexcept { return Compare(cstr); }

	friend bool operator==(const String &a, const String &b) noexcept { return a.View() == b.View(); }
	friend bool operator!=(const String &a, const String &b) noexcept { return a.View() != b.View(); }
	friend bool operator<(const String &a, const String &b) noexcept { return a.View() < b.View(); }
	friend bool operator<=(const String &a, const String &b) noexcept { return a.View() <= b.View(); }
	friend bool operator>(const String &a, const String &b) noexcept { return a.View() > b.View(); }
	friend bool operator>=(const String &a, const String &b) noexcept { return a.View() >= b.View(); }

	friend bool operator==(const String &a, const char *b) noexcept { return a.View() == StringView(b); }
	friend bool operator!=(const String &a, const char *b) noexcept { return a.View() != StringView(b); }
	friend bool operator==(const char *a, const String &b) noexcept { return b == a; }
	friend bool operator!=(const char *a, const String &b) noexcept { return b != a; }

	// =========================================================================
	// Recherche (octets)
	// =========================================================================

	static constexpr size_t NPOS = StringView::NPOS;
	/// Alias std::string-compat de NPOS.
	static constexpr size_t npos = StringView::NPOS;

	[[nodiscard]] size_t Find(const String &s, size_t from = 0) const { return View().Find(s.View(), from); }
	[[nodiscard]] size_t Find(const char *s, size_t from = 0) const { return View().Find(StringView(s), from); }
	[[nodiscard]] size_t Find(char c, size_t from = 0) const { return View().Find(c, from); }
	[[nodiscard]] size_t Rfind(const String &s, size_t from = NPOS) const { return View().Rfind(s.View(), from); }
	[[nodiscard]] size_t Rfind(const char *s, size_t from = NPOS) const { return View().Rfind(StringView(s), from); }
	[[nodiscard]] size_t Rfind(char c, size_t from = NPOS) const { return View().Rfind(c, from); }

	// Compat Arduino
	[[nodiscard]] int IndexOf(char c, size_t from = 0) const {
		size_t p = View().Find(c, from);
		return (p == NPOS) ? -1 : static_cast<int>(p);
	}
	[[nodiscard]] int IndexOf(const String &s, size_t from = 0) const {
		size_t p = View().Find(s.View(), from);
		return (p == NPOS) ? -1 : static_cast<int>(p);
	}
	[[nodiscard]] int LastIndexOf(char c, size_t from = NPOS) const {
		size_t p = View().Rfind(c, from);
		return (p == NPOS) ? -1 : static_cast<int>(p);
	}
	[[nodiscard]] int LastIndexOf(const String &s, size_t from = NPOS) const {
		size_t p = View().Rfind(s.View(), from);
		return (p == NPOS) ? -1 : static_cast<int>(p);
	}

	[[nodiscard]] bool Contains(const String &s) const { return View().Contains(s.View()); }
	[[nodiscard]] bool Contains(const char *s) const { return View().Contains(StringView(s)); }
	[[nodiscard]] bool Contains(char c) const { return View().Contains(c); }

	[[nodiscard]] bool StartsWith(const char* prefix) const noexcept { return StartsWith(StringView(prefix)); }
	[[nodiscard]] bool StartsWith(StringView prefix) const noexcept { return View().StartsWith(prefix); }

	[[nodiscard]] bool EndsWith(const char* prefix) const noexcept { return EndsWith(StringView(prefix)); }
	[[nodiscard]] bool EndsWith(StringView suffix) const noexcept { return View().EndsWith(suffix); }
	
	// Compat Arduino
	[[nodiscard]] bool StartsWith(const String &prefix, size_t offset = 0) const {
		return View().Substr(offset).StartsWith(prefix.View());
	}
	[[nodiscard]] bool EndsWith(const String &suffix) const { return View().EndsWith(suffix.View()); }

	/// Nombre d'occurrences non-chevauchantes de `needle`.
	[[nodiscard]] size_t Count(StringView needle) const {
		if (needle.IsEmpty())
			return 0;
		size_t n = 0, pos = 0;
		auto v = View();
		while ((pos = v.Find(needle, pos)) != NPOS) {
			++n;
			pos += needle.GetSize();
		}
		return n;
	}
	[[nodiscard]] size_t Count(char c) const {
		size_t n = 0;
		for (size_t i = 0; i < m_size; ++i)
			if (m_data[i] == c)
				++n;
		return n;
	}

	// =========================================================================
	// Extraction (octets)
	// =========================================================================

	[[nodiscard]] String Substr(size_t pos, size_t len = NPOS) const { return String(View().Substr(pos, len)); }
	// Compat Arduino
	[[nodiscard]] String Substring(size_t beginIdx, size_t endIdx = NPOS) const {
		if (endIdx == NPOS)
			endIdx = m_size;
		if (beginIdx > endIdx)
			std::swap(beginIdx, endIdx);
		return Substr(beginIdx, endIdx - beginIdx);
	}

	[[nodiscard]] Option<String> GetBetween(StringView open, StringView close) const {
		auto v = View();
		size_t s = v.Find(open);
		if (s == NPOS)
			return NONE;
		s += open.GetSize();
		size_t e = v.Find(close, s);
		if (e == NPOS)
			return NONE;
		return Some(Substr(s, e - s));
	}

	// =========================================================================
	// Concaténation & append
	// =========================================================================

	String &Append(const char *data, size_t len) {
		if (!data || !len)
			return *this;
		EnsureCapacity(m_size + len);
		std::memcpy(m_data + m_size, data, len);
		SetLength(m_size + len);
		return *this;
	}
	String &Append(const String &s) { return Append(s.m_data, s.m_size); }
	String &Append(const char *s) { return s ? Append(s, std::strlen(s)) : *this; }
	String &Append(StringView sv) { return Append(sv.GetData(), sv.GetSize()); }
	String &Append(char c) { return Append(&c, 1); }
	String &Append(size_t n, char c) {
		if (!n)
			return *this;
		EnsureCapacity(m_size + n);
		std::memset(m_data + m_size, static_cast<unsigned char>(c), n);
		SetLength(m_size + n);
		return *this;
	}

	String &Append(long long v, int base = 10) { return Append(String::From(v, base)); }
	String &Append(int v, int base = 10) { return Append(String::From(v, base)); }
	String &Append(long v, int base = 10) { return Append(String::From(v, base)); }
	String &Append(unsigned long long v, int base = 10) { return Append(String::From(v, base)); }
	String &Append(unsigned int v, int base = 10) { return Append(String::From(v, base)); }
	String &Append(unsigned long v, int base = 10) { return Append(String::From(v, base)); }
	String &Append(double v, int decimals = 2) { return Append(String::From(v, decimals)); }
	String &Append(float v, int decimals = 2) { return Append(String::From(v, decimals)); }
	String &Append(bool v) { return Append(String::From(v)); }

	/// Ajoute un unique octet (nom std::string-compat, utilisé aussi comme
	/// "sink" générique par unicode::EncodeUtf8).
	void PushBack(char c) { Append(&c, 1); }
	/// Alias std::string-compat de PushBack, requis pour rester interchangeable
	/// avec std::string en tant que "sink" générique (cf. unicode::EncodeUtf8).
	void push_back(char c) { Append(&c, 1); }

	void Clear() noexcept { SetLength(0); }

	// Compat Arduino — bool retour indique toujours succès ici
	bool Concat(const String &s) {
		Append(s);
		return true;
	}
	bool Concat(const char *s) {
		Append(s);
		return true;
	}
	bool Concat(char c) {
		Append(c);
		return true;
	}
	bool Concat(int v) {
		Append(v);
		return true;
	}
	bool Concat(unsigned int v) {
		Append(v);
		return true;
	}
	bool Concat(long v) {
		Append(v);
		return true;
	}
	bool Concat(unsigned long v) {
		Append(v);
		return true;
	}
	bool Concat(float v) {
		Append(v);
		return true;
	}
	bool Concat(double v) {
		Append(v);
		return true;
	}

	// operator+= — retourne *this pour chaînage
	String &operator+=(const String &s) { return Append(s); }
	String &operator+=(const char *s) { return Append(s); }
	String &operator+=(StringView sv) { return Append(sv); }
	String &operator+=(char c) { return Append(c); }
	String &operator+=(long long v) { return Append(v); }
	String &operator+=(int v) { return Append(v); }
	String &operator+=(unsigned int v) { return Append(v); }
	String &operator+=(long v) { return Append(v); }
	String &operator+=(unsigned long v) { return Append(v); }
	String &operator+=(double v) { return Append(v); }
	String &operator+=(float v) { return Append(v); }
	String &operator+=(bool v) { return Append(v); }

	// operator+ — retourne une nouvelle String
	[[nodiscard]] friend String operator+(String lhs, const String &rhs) {
		lhs.Append(rhs);
		return lhs;
	}
	[[nodiscard]] friend String operator+(String lhs, const char *rhs) {
		lhs.Append(rhs);
		return lhs;
	}
	[[nodiscard]] friend String operator+(const char *lhs, const String &rhs) { return String(lhs) + rhs; }
	[[nodiscard]] friend String operator+(String lhs, char rhs) {
		lhs.Append(rhs);
		return lhs;
	}
	[[nodiscard]] friend String operator+(String lhs, long long rhs) {
		lhs.Append(rhs);
		return lhs;
	}
	[[nodiscard]] friend String operator+(String lhs, int rhs) {
		lhs.Append(rhs);
		return lhs;
	}
	[[nodiscard]] friend String operator+(String lhs, double rhs) {
		lhs.Append(rhs);
		return lhs;
	}

	// =========================================================================
	// Opérateur pipe (fonctionnel)
	// =========================================================================

	/// Applique une transformation : `s | fn` équivaut à `fn(s)`.
	/// @code
	/// auto upper = [](String s){ return s.toUpper(); };
	/// String r = String("hello") | upper | [](String s){ return s + "!"; };
	/// @endcode
	template <typename Fn> [[nodiscard]] auto operator|(Fn &&fn) const & {
		return std::invoke(std::forward<Fn>(fn), *this);
	}
	template <typename Fn> [[nodiscard]] auto operator|(Fn &&fn) && {
		return std::invoke(std::forward<Fn>(fn), std::move(*this));
	}

	// =========================================================================
	// Transformations — retournent une nouvelle String (chaînage fluent)
	// =========================================================================

	/// Minuscule ASCII (octets < 128).
	[[nodiscard]] String ToLower() const {
		String r(*this);
		for (char &c : r)
			c = static_cast<char>(std::tolower(static_cast<uint8_t>(c)));
		return r;
	}
	/// Majuscule ASCII.
	[[nodiscard]] String ToUpper() const {
		String r(*this);
		for (char &c : r)
			c = static_cast<char>(std::toupper(static_cast<uint8_t>(c)));
		return r;
	}
	// Compat Arduino (modifient en place)
	void ToLowerCase() { *this = ToLower(); }
	void ToUpperCase() { *this = ToUpper(); }

	/// Minuscule Unicode complet (codepoints).
	[[nodiscard]] String UToLower() const {
		String result;
		result.Reserve(m_size);
		const auto *p = reinterpret_cast<const uint8_t *>(CStr());
		const auto *end = p + m_size;
		while (p < end)
			unicode::EncodeUtf8(unicode::ToLowerCp(unicode::DecodeNext(p, end)), result);
		return result;
	}

	/// Majuscule Unicode complet.
	[[nodiscard]] String UToUpper() const {
		String result;
		result.Reserve(m_size);
		const auto *p = reinterpret_cast<const uint8_t *>(CStr());
		const auto *end = p + m_size;
		while (p < end)
			unicode::EncodeUtf8(unicode::ToUpperCp(unicode::DecodeNext(p, end)), result);
		return result;
	}

	/// Supprime les espaces en début et fin (ASCII).
	[[nodiscard]] String Trim() const {
		auto v = View();
		size_t b = v.GetSize();
		while (b > 0 && IsTrimSpace(v[b - 1]))
			--b;
		size_t a = 0;
		while (a < b && IsTrimSpace(v[a]))
			++a;
		return String(v.GetData() + a, b - a);
	}
	// Compat Arduino
	void TrimInplace() { *this = Trim(); }

	/// Supprime uniquement les espaces à gauche.
	[[nodiscard]] String TrimLeft() const {
		size_t i = 0;
		while (i < m_size && IsTrimSpace(m_data[i]))
			++i;
		return Substr(i);
	}

	/// Supprime uniquement les espaces à droite.
	[[nodiscard]] String TrimRight() const {
		size_t i = m_size;
		while (i > 0 && IsTrimSpace(m_data[i - 1]))
			--i;
		return Substr(0, i);
	}

	/// Supprime les espaces Unicode en début/fin.
	[[nodiscard]] String UTrim() const {
		auto cpv = Codepoints();
		auto it = cpv.Begin(), end = cpv.End();
		while (it != end && unicode::IsUnicodeSpace(*it))
			++it;
		std::vector<CodepointT> cps;
		while (it != end) {
			cps.push_back(*it);
			++it;
		}
		while (!cps.empty() && unicode::IsUnicodeSpace(cps.back()))
			cps.pop_back();
		String result;
		for (auto cp : cps)
			unicode::EncodeUtf8(cp, result);
		return result;
	}

	/// Remplace toutes les occurrences de `find` par `replacement`.
	[[nodiscard]] String Replace(StringView find, StringView replacement) const {
		if (find.IsEmpty())
			return *this;
		String result;
		result.Reserve(m_size);
		size_t pos = 0, prev = 0;
		auto v = View();
		while ((pos = v.Find(find, prev)) != NPOS) {
			result.Append(m_data + prev, pos - prev);
			result.Append(replacement.GetData(), replacement.GetSize());
			prev = pos + find.GetSize();
		}
		result.Append(m_data + prev, m_size - prev);
		return result;
	}

	[[nodiscard]] String Replace(char find, char replacement) const {
		String r(*this);
		for (char &c : r)
			if (c == find)
				c = replacement;
		return r;
	}

	// Compat Arduino (modifient en place)
	void ReplaceInplace(const String &find, const String &rep) { *this = Replace(find.View(), rep.View()); }
	void ReplaceInplace(char find, char rep) {
		for (char &c : *this)
			if (c == find)
				c = rep;
	}

	/// Supprime `count` octets à partir de `index`.
	[[nodiscard]] String Remove(size_t index, size_t count = NPOS) const {
		if (index >= m_size)
			return *this;
		if (count > m_size - index)
			count = m_size - index;
		String r;
		r.Reserve(m_size - count);
		r.Append(m_data, index);
		r.Append(m_data + index + count, m_size - index - count);
		return r;
	}
	// Compat Arduino (modifie en place)
	void RemoveInplace(size_t index, size_t count = NPOS) {
		if (index >= m_size)
			return;
		if (count > m_size - index)
			count = m_size - index;
		std::memmove(m_data + index, m_data + index + count, m_size - index - count);
		SetLength(m_size - count);
	}

	/// Insère `s` à la position `pos`.
	[[nodiscard]] String Insert(size_t pos, const String &s) const {
		if (pos > m_size)
			pos = m_size;
		String r;
		r.Reserve(m_size + s.m_size);
		r.Append(m_data, pos);
		r.Append(s.m_data, s.m_size);
		r.Append(m_data + pos, m_size - pos);
		return r;
	}
	[[nodiscard]] String Insert(size_t pos, char c) const {
		if (pos > m_size)
			pos = m_size;
		String r;
		r.Reserve(m_size + 1);
		r.Append(m_data, pos);
		r.Append(c);
		r.Append(m_data + pos, m_size - pos);
		return r;
	}

	/// Répète la chaîne `n` fois.
	[[nodiscard]] String Repeat(size_t n) const {
		String result;
		result.Reserve(m_size * n);
		for (size_t i = 0; i < n; ++i)
			result.Append(m_data, m_size);
		return result;
	}

	/// Inverse la chaîne (octets — n'est pas unicode-safe pour les multi-octets).
	[[nodiscard]] String ReverseBytes() const {
		String r(*this);
		std::reverse(r.Begin(), r.End());
		return r;
	}

	/// Inverse la séquence de codepoints (unicode-safe).
	[[nodiscard]] String UReverse() const {
		auto cps = unicode::ToUtf32(View());
		std::reverse(cps.begin(), cps.end());
		return String(unicode::FromUtf32(cps));
	}

	// =========================================================================
	// Padding
	// =========================================================================

	/// Complète à gauche jusqu'à `total_width` octets avec le caractère `pad`.
	[[nodiscard]] String PadLeft(size_t totalWidth, char pad = ' ') const {
		if (m_size >= totalWidth)
			return *this;
		String r;
		r.Reserve(totalWidth);
		r.Append(totalWidth - m_size, pad);
		r.Append(m_data, m_size);
		return r;
	}

	/// Complète à droite.
	[[nodiscard]] String PadRight(size_t totalWidth, char pad = ' ') const {
		if (m_size >= totalWidth)
			return *this;
		String r(*this);
		r.Append(totalWidth - m_size, pad);
		return r;
	}

	/// Centre avec padding des deux côtés.
	[[nodiscard]] String PadCenter(size_t totalWidth, char pad = ' ') const {
		if (m_size >= totalWidth)
			return *this;
		size_t totalPad = totalWidth - m_size;
		size_t leftPad = totalPad / 2;
		size_t rightPad = totalPad - leftPad;
		String r;
		r.Reserve(totalWidth);
		r.Append(leftPad, pad);
		r.Append(m_data, m_size);
		r.Append(rightPad, pad);
		return r;
	}

	/// Tronque à `max_len` octets.
	[[nodiscard]] String Truncate(size_t maxLen) const {
		return (m_size <= maxLen) ? *this : String(m_data, maxLen);
	}

	// =========================================================================
	// Split & join
	// =========================================================================

	/// Divise sur un délimiteur chaîne. `max_splits` = 0 → illimité.
	[[nodiscard]] std::vector<String> Split(StringView delim, size_t maxSplits = 0) const {
		std::vector<String> result;
		if (delim.IsEmpty()) {
			result.emplace_back(*this);
			return result;
		}
		auto v = View();
		size_t pos = 0, prev = 0, splits = 0;
		while ((pos = v.Find(delim, prev)) != NPOS) {
			result.emplace_back(m_data + prev, pos - prev);
			prev = pos + delim.GetSize();
			if (maxSplits > 0 && ++splits >= maxSplits)
				break;
		}
		result.emplace_back(m_data + prev, m_size - prev);
		return result;
	}

	/// Divise sur un caractère.
	[[nodiscard]] std::vector<String> Split(char delim, size_t maxSplits = 0) const {
		return Split(StringView(&delim, 1), maxSplits);
	}

	/// Divise sur n'importe quel caractère de `chars` (style strtok).
	[[nodiscard]] std::vector<String> SplitAny(StringView chars) const {
		std::vector<String> result;
		size_t prev = 0;
		for (size_t i = 0; i < m_size; ++i) {
			if (chars.Find(m_data[i]) != StringView::NPOS) {
				result.emplace_back(m_data + prev, i - prev);
				prev = i + 1;
			}
		}
		result.emplace_back(m_data + prev, m_size - prev);
		return result;
	}

	/// Divise sur les sauts de ligne (\n, \r\n, \r).
	[[nodiscard]] std::vector<String> Lines() const {
		std::vector<String> result;
		size_t prev = 0;
		for (size_t i = 0; i < m_size;) {
			if (m_data[i] == '\r') {
				result.emplace_back(m_data + prev, i - prev);
				prev = (i + 1 < m_size && m_data[i + 1] == '\n') ? i + 2 : i + 1;
				i = prev;
			} else if (m_data[i] == '\n') {
				result.emplace_back(m_data + prev, i - prev);
				prev = ++i;
			} else {
				++i;
			}
		}
		result.emplace_back(m_data + prev, m_size - prev);
		return result;
	}

	/// Divise en caractères individuels (octets).
	[[nodiscard]] std::vector<String> Chars() const {
		std::vector<String> result;
		result.reserve(m_size);
		for (size_t i = 0; i < m_size; ++i)
			result.emplace_back(m_data + i, size_t(1));
		return result;
	}

	/// Divise en codepoints Unicode individuels.
	[[nodiscard]] std::vector<String> UChars() const {
		std::vector<String> result;
		for (auto cp : Codepoints()) {
			String s;
			unicode::EncodeUtf8(cp, s);
			result.emplace_back(std::move(s));
		}
		return result;
	}

	/// Joint un vecteur de String avec un séparateur.
	[[nodiscard]] static String Join(const std::vector<String> &parts, StringView sep = "") {
		String result;
		size_t total = 0;
		for (auto &p : parts)
			total += p.GetSize();
		if (!parts.empty())
			total += sep.GetSize() * (parts.size() - 1);
		result.Reserve(total);
		for (size_t i = 0; i < parts.size(); ++i) {
			if (i > 0)
				result.Append(sep.GetData(), sep.GetSize());
			result.Append(parts[i].m_data, parts[i].m_size);
		}
		return result;
	}

	[[nodiscard]] static String Join(std::initializer_list<String> parts, StringView sep = "") {
		return Join(std::vector<String>(parts), sep);
	}

	// =========================================================================
	// Transformations fonctionnelles (codepoints)
	// =========================================================================

	/// Applique `fn` à chaque codepoint et retourne la chaîne transformée.
	template <typename Fn> [[nodiscard]] String Map(Fn &&fn) const {
		String result;
		result.Reserve(m_size);
		for (auto cp : Codepoints())
			unicode::EncodeUtf8(std::invoke(std::forward<Fn>(fn), cp), result);
		return result;
	}

	/// Conserve uniquement les codepoints pour lesquels `pred` retourne true.
	template <typename Pred> [[nodiscard]] String Filter(Pred &&pred) const {
		String result;
		result.Reserve(m_size);
		for (auto cp : Codepoints())
			if (std::invoke(std::forward<Pred>(pred), cp))
				unicode::EncodeUtf8(cp, result);
		return result;
	}

	/// Transforme chaque codepoint en String et concatène les résultats.
	template <typename Fn> [[nodiscard]] String FlatMap(Fn &&fn) const {
		String result;
		for (auto cp : Codepoints()) {
			String s = std::invoke(std::forward<Fn>(fn), cp);
			result.Append(s.m_data, s.m_size);
		}
		return result;
	}

	/// Réduit les codepoints à une valeur (fold gauche).
	template <typename T, typename Fn> [[nodiscard]] T Reduce(T init, Fn &&fn) const {
		for (auto cp : Codepoints())
			init = std::invoke(std::forward<Fn>(fn), std::move(init), cp);
		return init;
	}

	/// Retourne true si `pred` est vrai pour au moins un codepoint.
	template <typename Pred> [[nodiscard]] bool Any(Pred &&pred) const {
		for (auto cp : Codepoints())
			if (std::invoke(std::forward<Pred>(pred), cp))
				return true;
		return false;
	}

	/// Retourne true si `pred` est vrai pour tous les codepoints.
	template <typename Pred> [[nodiscard]] bool All(Pred &&pred) const {
		for (auto cp : Codepoints())
			if (!std::invoke(std::forward<Pred>(pred), cp))
				return false;
		return true;
	}

	// =========================================================================
	// Tests de contenu
	// =========================================================================

	[[nodiscard]] bool IsAscii() const noexcept {
		for (uint8_t c : *this)
			if (c > 127)
				return false;
		return true;
	}
	[[nodiscard]] bool IsValidUtf8() const noexcept { return unicode::IsValidUtf8(View()); }
	[[nodiscard]] bool IsNumeric() const noexcept {
		if (m_size == 0)
			return false;
		size_t i = 0;
		if (m_data[0] == '-' || m_data[0] == '+') {
			if (m_size == 1)
				return false;
			i = 1;
		}
		bool dot = false;
		for (; i < m_size; ++i) {
			if (m_data[i] == '.' && !dot) {
				dot = true;
				continue;
			}
			if (!std::isdigit(static_cast<uint8_t>(m_data[i])))
				return false;
		}
		return true;
	}
	[[nodiscard]] bool IsAlpha() const noexcept {
		for (char c : *this)
			if (!std::isalpha(static_cast<uint8_t>(c)))
				return false;
		return !IsEmpty();
	}
	[[nodiscard]] bool IsAlnum() const noexcept {
		for (char c : *this)
			if (!std::isalnum(static_cast<uint8_t>(c)))
				return false;
		return !IsEmpty();
	}
	[[nodiscard]] bool IsWhitespace() const noexcept {
		for (char c : *this)
			if (!std::isspace(static_cast<uint8_t>(c)))
				return false;
		return !IsEmpty();
	}

	// =========================================================================
	// Conversion vers types numériques
	// =========================================================================

	[[nodiscard]] int32_t ToInt32() const { return std::strtol(CStr(), nullptr, 10); }
	[[nodiscard]] int64_t ToInt64() const { return std::strtoll(CStr(), nullptr, 10); }
	[[nodiscard]] uint64_t ToUint64() const { return std::strtoull(CStr(), nullptr, 10); }
	[[nodiscard]] float ToFloat() const { return std::strtof(CStr(), nullptr); }
	[[nodiscard]] double ToDouble() const { return std::strtod(CStr(), nullptr); }

	/// Conversion avec détection d'erreur — retourne nullopt si invalide.
	[[nodiscard]] Option<int64_t> TryParseInt(int base = 10) const {
		if (IsEmpty())
			return NONE;
		char *end;
		int64_t v = std::strtoll(CStr(), &end, base);
		if (end == CStr() || *end != '\0')
			return NONE;
		return Some(v);
	}

	[[nodiscard]] Option<double> TryParseDouble() const {
		if (IsEmpty())
			return NONE;
		char *end;
		double v = std::strtod(CStr(), &end);
		if (end == CStr() || *end != '\0')
			return NONE;
		return Some(v);
	}

	// =========================================================================
	// Unicode — accès par codepoint
	// =========================================================================

	/// Nombre de codepoints (O(n) — parcours).
	[[nodiscard]] size_t ULength() const noexcept { return unicode::CodepointCount(View()); }

	/// Codepoint à l'index `n` (O(n)).
	[[nodiscard]] CodepointT UCharAt(size_t n) const { return unicode::CodepointAt(View(), n); }

	/// Position en octets du n-ième codepoint.
	[[nodiscard]] size_t UByteOffset(size_t codepointIndex) const {
		return unicode::ByteOffsetOf(View(), codepointIndex);
	}

	/// Vue itérable sur les codepoints.
	[[nodiscard]] unicode::CodepointView Codepoints() const noexcept { return unicode::CodepointView{View()}; }

	/// Retourne tous les codepoints dans un vecteur.
	[[nodiscard]] std::vector<CodepointT> ToCodepoints() const {
		std::vector<CodepointT> result;
		for (auto cp : Codepoints())
			result.push_back(cp);
		return result;
	}

	// =========================================================================
	// Conversions Unicode
	// =========================================================================

	[[nodiscard]] std::u16string ToUtf16() const { return unicode::ToUtf16(View()); }
	[[nodiscard]] std::u32string ToUtf32() const { return unicode::ToUtf32(View()); }

	// =========================================================================
	// Copie / buffer (compat Arduino)
	// =========================================================================

	void ToCharArray(char *buf, size_t bufsize, size_t offset = 0) const {
		if (!buf || bufsize == 0)
			return;
		size_t avail = (offset < m_size) ? (m_size - offset) : 0;
		size_t len = std::min(avail, bufsize - 1);
		if (len)
			std::memcpy(buf, m_data + offset, len);
		buf[len] = '\0';
	}

	void GetBytes(uint8_t *buf, size_t bufsize, size_t offset = 0) const {
		ToCharArray(reinterpret_cast<char *>(buf), bufsize, offset);
	}

	void Reserve(size_t m_capacity) { EnsureCapacity(m_capacity); }

	// =========================================================================
	// Flux
	// =========================================================================

	friend std::ostream &operator<<(std::ostream &os, const String &s) {
		return os.write(s.CStr(), static_cast<std::streamsize>(s.GetSize()));
	}
	friend std::istream &operator>>(std::istream &is, String &s) {
		s.Clear();
		is >> std::ws;
		char c;
		while (is.get(c)) {
			if (std::isspace(static_cast<unsigned char>(c))) {
				is.putback(c);
				break;
			}
			s.PushBack(c);
		}
		return is;
	}

	// std::getline compat
	friend std::istream &Getline(std::istream &is, String &s, char delim = '\n') {
		s.Clear();
		std::istream::sentry sentry(is, true);
		if (!sentry)
			return is;
		char c;
		bool extracted = false;
		while (is.get(c)) {
			extracted = true;
			if (c == delim)
				break;
			s.PushBack(c);
		}
		if (!extracted)
			is.setstate(std::ios::failbit);
		return is;
	}

	// =========================================================================
	// Hash (pour utilisation comme clé de map)
	// =========================================================================

	struct Hash {
		size_t operator()(const String &s) const noexcept {
			// FNV-1a 64 bits.
			size_t h = 14695981039346656037ull;
			for (size_t i = 0; i < s.m_size; ++i) {
				h ^= static_cast<unsigned char>(s.m_data[i]);
				h *= 1099511628211ull;
			}
			return h;
		}
	};

private:
	char *m_data = nullptr;
	size_t m_size = 0;
	size_t m_capacity = 0;

	[[nodiscard]] static bool IsTrimSpace(char c) noexcept {
		return static_cast<uint8_t>(c) <= 0x20 || unicode::IsUnicodeSpace(static_cast<uint8_t>(c));
	}

	/// Buffer statique non-const partagé pour begin()==end() sur une String
	/// vide (évite d'allouer, et d'exposer un char* vers un littéral const).
	static char *EmptySentinel() noexcept {
		static char empty = '\0';
		return &empty;
	}

	void EnsureCapacity(size_t minCap) {
		if (minCap <= m_capacity)
			return;
		size_t newCap = m_capacity ? m_capacity * 2 : 16;
		if (newCap < minCap)
			newCap = minCap;
		char *newData = new char[newCap + 1];
		if (m_data && m_size)
			std::memcpy(newData, m_data, m_size);
		delete[] m_data;
		m_data = newData;
		m_capacity = newCap;
		m_data[m_size] = '\0';
	}

	void SetLength(size_t n) noexcept {
		m_size = n;
		if (m_data)
			m_data[n] = '\0';
	}

	void Assign(const char *data, size_t len) {
		EnsureCapacity(len);
		if (len && m_data)
			std::memcpy(m_data, data, len);
		SetLength(len);
	}
};

// ---------------------------------------------------------------------------
// Littéraux utilisateur
// ---------------------------------------------------------------------------

inline namespace string_literals {

/// `"hello"_s` → String("hello")
inline String operator""_s(const char *str, size_t len) { return String(str, len); }

} // namespace string_literals

// ---------------------------------------------------------------------------
// Spécialisation std::hash
// ---------------------------------------------------------------------------

namespace std {
template <> struct hash<String> {
	size_t operator()(const String &s) const noexcept { return String::Hash{}(s); }
};
} // namespace std