// Définitions de core/string.hpp
#include "core/string.hpp"

namespace detail {

size_t UintToBase(unsigned long long uval, int base, char *out) {
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

size_t IntToBase(long long value, int base, char *out) {
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

size_t FloatToStr(double value, int decimals, char *out, size_t outCap) {
	int n = std::snprintf(out, outCap, "%.*f", decimals, value);
	return n > 0 ? static_cast<size_t>(n) : 0;
}

} // namespace detail

// ── String ───────────────────────────────────────────────────────────────────

String::String(size_t count, char c) {
	EnsureCapacity(count);
	if (count)
		std::memset(m_data, static_cast<unsigned char>(c), count);
	SetLength(count);
}

String String::From(long long value, int base) {
	if (base < 2 || base > 36)
		return {};
	char buf[72];
	size_t n = detail::IntToBase(value, base, buf);
	return String(buf, n);
}

String String::From(unsigned long long v, int base) {
	if (base < 2 || base > 36)
		return {};
	char buf[72];
	size_t n = detail::UintToBase(v, base, buf);
	return String(buf, n);
}

String String::From(double value, int decimals) {
	char buf[64];
	size_t n = detail::FloatToStr(value, decimals, buf, sizeof(buf));
	return String(buf, n);
}

std::reverse_iterator<const char *> String::Rbegin() const {
	return std::reverse_iterator<const char *>(End());
}

std::reverse_iterator<const char *> String::Rend() const {
	return std::reverse_iterator<const char *>(Begin());
}

void String::SetCharAt(size_t i, char c) {
	if (i < m_size)
		m_data[i] = c;
}

int String::IndexOf(char c, size_t from) const {
	size_t p = View().Find(c, from);
	return (p == NPOS) ? -1 : static_cast<int>(p);
}

int String::LastIndexOf(char c, size_t from) const {
	size_t p = View().Rfind(c, from);
	return (p == NPOS) ? -1 : static_cast<int>(p);
}

size_t String::Count(StringView needle) const {
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

size_t String::Count(char c) const {
	size_t n = 0;
	for (size_t i = 0; i < m_size; ++i)
		if (m_data[i] == c)
			++n;
	return n;
}

String String::Substring(size_t beginIdx, size_t endIdx) const {
	if (endIdx == NPOS)
		endIdx = m_size;
	if (beginIdx > endIdx)
		std::swap(beginIdx, endIdx);
	return Substr(beginIdx, endIdx - beginIdx);
}

Option<String> String::GetBetween(StringView open, StringView close) const {
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

String & String::Append(const char *data, size_t len) {
	if (!data || !len)
		return *this;
	EnsureCapacity(m_size + len);
	std::memcpy(m_data + m_size, data, len);
	SetLength(m_size + len);
	return *this;
}

String & String::Append(size_t n, char c) {
	if (!n)
		return *this;
	EnsureCapacity(m_size + n);
	std::memset(m_data + m_size, static_cast<unsigned char>(c), n);
	SetLength(m_size + n);
	return *this;
}

bool String::Concat(const char *s) {
	Append(s);
	return true;
}

bool String::Concat(char c) {
	Append(c);
	return true;
}

bool String::Concat(int v) {
	Append(v);
	return true;
}

bool String::Concat(unsigned int v) {
	Append(v);
	return true;
}

bool String::Concat(long v) {
	Append(v);
	return true;
}

bool String::Concat(unsigned long v) {
	Append(v);
	return true;
}

bool String::Concat(float v) {
	Append(v);
	return true;
}

bool String::Concat(double v) {
	Append(v);
	return true;
}

String String::ToLower() const {
	String r(*this);
	for (char &c : r)
		c = static_cast<char>(std::tolower(static_cast<uint8_t>(c)));
	return r;
}

String String::ToUpper() const {
	String r(*this);
	for (char &c : r)
		c = static_cast<char>(std::toupper(static_cast<uint8_t>(c)));
	return r;
}

String String::UToLower() const {
	String result;
	result.Reserve(m_size);
	const auto *p = reinterpret_cast<const uint8_t *>(CStr());
	const auto *end = p + m_size;
	while (p < end)
		unicode::EncodeUtf8(unicode::ToLowerCp(unicode::DecodeNext(p, end)), result);
	return result;
}

String String::UToUpper() const {
	String result;
	result.Reserve(m_size);
	const auto *p = reinterpret_cast<const uint8_t *>(CStr());
	const auto *end = p + m_size;
	while (p < end)
		unicode::EncodeUtf8(unicode::ToUpperCp(unicode::DecodeNext(p, end)), result);
	return result;
}

String String::Trim() const {
	auto v = View();
	size_t b = v.GetSize();
	while (b > 0 && IsTrimSpace(v[b - 1]))
		--b;
	size_t a = 0;
	while (a < b && IsTrimSpace(v[a]))
		++a;
	return String(v.GetData() + a, b - a);
}

String String::TrimLeft() const {
	size_t i = 0;
	while (i < m_size && IsTrimSpace(m_data[i]))
		++i;
	return Substr(i);
}

String String::TrimRight() const {
	size_t i = m_size;
	while (i > 0 && IsTrimSpace(m_data[i - 1]))
		--i;
	return Substr(0, i);
}

String String::UTrim() const {
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

String String::Replace(StringView find, StringView replacement) const {
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

String String::Replace(char find, char replacement) const {
	String r(*this);
	for (char &c : r)
		if (c == find)
			c = replacement;
	return r;
}

void String::ReplaceInplace(char find, char rep) {
	for (char &c : *this)
		if (c == find)
			c = rep;
}

String String::Remove(size_t index, size_t count) const {
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

void String::RemoveInplace(size_t index, size_t count) {
	if (index >= m_size)
		return;
	if (count > m_size - index)
		count = m_size - index;
	std::memmove(m_data + index, m_data + index + count, m_size - index - count);
	SetLength(m_size - count);
}

String String::Insert(size_t pos, char c) const {
	if (pos > m_size)
		pos = m_size;
	String r;
	r.Reserve(m_size + 1);
	r.Append(m_data, pos);
	r.Append(c);
	r.Append(m_data + pos, m_size - pos);
	return r;
}

String String::Repeat(size_t n) const {
	String result;
	result.Reserve(m_size * n);
	for (size_t i = 0; i < n; ++i)
		result.Append(m_data, m_size);
	return result;
}

String String::ReverseBytes() const {
	String r(*this);
	std::reverse(r.Begin(), r.End());
	return r;
}

String String::UReverse() const {
	auto cps = unicode::ToUtf32(View());
	std::reverse(cps.begin(), cps.end());
	return String(unicode::FromUtf32(cps));
}

String String::PadLeft(size_t totalWidth, char pad) const {
	if (m_size >= totalWidth)
		return *this;
	String r;
	r.Reserve(totalWidth);
	r.Append(totalWidth - m_size, pad);
	r.Append(m_data, m_size);
	return r;
}

String String::PadRight(size_t totalWidth, char pad) const {
	if (m_size >= totalWidth)
		return *this;
	String r(*this);
	r.Append(totalWidth - m_size, pad);
	return r;
}

String String::PadCenter(size_t totalWidth, char pad) const {
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

String String::Truncate(size_t maxLen) const {
	return (m_size <= maxLen) ? *this : String(m_data, maxLen);
}

std::vector<String> String::Split(StringView delim, size_t maxSplits) const {
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

std::vector<String> String::Split(char delim, size_t maxSplits) const {
	return Split(StringView(&delim, 1), maxSplits);
}

std::vector<String> String::SplitAny(StringView chars) const {
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

std::vector<String> String::Lines() const {
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

std::vector<String> String::Chars() const {
	std::vector<String> result;
	result.reserve(m_size);
	for (size_t i = 0; i < m_size; ++i)
		result.emplace_back(m_data + i, size_t(1));
	return result;
}

std::vector<String> String::UChars() const {
	std::vector<String> result;
	for (auto cp : Codepoints()) {
		String s;
		unicode::EncodeUtf8(cp, s);
		result.emplace_back(std::move(s));
	}
	return result;
}

String String::Join(const std::vector<String> &parts, StringView sep) {
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

String String::Join(std::initializer_list<String> parts, StringView sep) {
	return Join(std::vector<String>(parts), sep);
}

bool String::IsAscii() const noexcept {
	for (uint8_t c : *this)
		if (c > 127)
			return false;
	return true;
}

bool String::IsNumeric() const noexcept {
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

bool String::IsAlpha() const noexcept {
	for (char c : *this)
		if (!std::isalpha(static_cast<uint8_t>(c)))
			return false;
	return !IsEmpty();
}

bool String::IsAlnum() const noexcept {
	for (char c : *this)
		if (!std::isalnum(static_cast<uint8_t>(c)))
			return false;
	return !IsEmpty();
}

bool String::IsWhitespace() const noexcept {
	for (char c : *this)
		if (!std::isspace(static_cast<uint8_t>(c)))
			return false;
	return !IsEmpty();
}

Option<int64_t> String::TryParseInt(int base) const {
	if (IsEmpty())
		return NONE;
	char *end;
	int64_t v = std::strtoll(CStr(), &end, base);
	if (end == CStr() || *end != '\0')
		return NONE;
	return Some(v);
}

Option<double> String::TryParseDouble() const {
	if (IsEmpty())
		return NONE;
	char *end;
	double v = std::strtod(CStr(), &end);
	if (end == CStr() || *end != '\0')
		return NONE;
	return Some(v);
}

size_t String::UByteOffset(size_t codepointIndex) const {
	return unicode::ByteOffsetOf(View(), codepointIndex);
}

std::vector<CodepointT> String::ToCodepoints() const {
	std::vector<CodepointT> result;
	for (auto cp : Codepoints())
		result.push_back(cp);
	return result;
}

void String::ToCharArray(char *buf, size_t bufsize, size_t offset) const {
	if (!buf || bufsize == 0)
		return;
	size_t avail = (offset < m_size) ? (m_size - offset) : 0;
	size_t len = std::min(avail, bufsize - 1);
	if (len)
		std::memcpy(buf, m_data + offset, len);
	buf[len] = '\0';
}

void String::GetBytes(uint8_t *buf, size_t bufsize, size_t offset) const {
	ToCharArray(reinterpret_cast<char *>(buf), bufsize, offset);
}

bool String::IsTrimSpace(char c) noexcept {
	return static_cast<uint8_t>(c) <= 0x20 || unicode::IsUnicodeSpace(static_cast<uint8_t>(c));
}

char * String::EmptySentinel() noexcept {
	static char empty = '\0';
	return &empty;
}

void String::EnsureCapacity(size_t minCap) {
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

void String::SetLength(size_t n) noexcept {
	m_size = n;
	if (m_data)
		m_data[n] = '\0';
}

void String::Assign(const char *data, size_t len) {
	EnsureCapacity(len);
	if (len && m_data)
		std::memcpy(m_data, data, len);
	SetLength(len);
}
