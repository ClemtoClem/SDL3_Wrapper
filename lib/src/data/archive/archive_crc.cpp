// Définitions de data/archive/archive_crc.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/archive/archive_crc.hpp"

namespace data::archive {

namespace detail {

const std::array<std::array<uint32_t, 256>, 8>& Crc32Tables() noexcept {
	static const std::array<std::array<uint32_t, 256>, 8> TABLES = [] {
		std::array<std::array<uint32_t, 256>, 8> tables{};
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t value = i;
			for (int bit = 0; bit < 8; ++bit)
				value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
			tables[0][i] = value;
		}
		for (uint32_t i = 0; i < 256; ++i)
			for (size_t t = 1; t < 8; ++t)
				tables[t][i] = (tables[t - 1][i] >> 8) ^ tables[0][tables[t - 1][i] & 0xFF];
		return tables;
	}();
	return TABLES;
}

} // namespace detail

uint32_t Crc32UpdateRaw(uint32_t state, uint8_t byte) noexcept {
	return detail::Crc32Tables()[0][(state ^ byte) & 0xFF] ^ (state >> 8);
}

uint32_t Crc32(std::span<const uint8_t> bytes, uint32_t crc) noexcept {
	const auto& t = detail::Crc32Tables();
	crc = ~crc;
	const uint8_t* p = bytes.data();
	size_t n = bytes.size();
	while (n >= 8) {
		uint32_t low = crc ^ (uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
							  (uint32_t(p[3]) << 24));
		crc = t[7][low & 0xFF] ^ t[6][(low >> 8) & 0xFF] ^ t[5][(low >> 16) & 0xFF] ^
			  t[4][low >> 24] ^ t[3][p[4]] ^ t[2][p[5]] ^ t[1][p[6]] ^ t[0][p[7]];
		p += 8;
		n -= 8;
	}
	while (n-- > 0)
		crc = t[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

namespace detail {

uint16_t Crc16Reflected(std::span<const uint8_t> bytes, uint16_t crc) noexcept {
	static const std::array<uint16_t, 256> TABLE = [] {
		std::array<uint16_t, 256> table{};
		for (uint32_t i = 0; i < 256; ++i) {
			uint16_t value = uint16_t(i);
			for (int bit = 0; bit < 8; ++bit)
				value = (value & 1u) ? uint16_t((value >> 1) ^ 0xA001u) : uint16_t(value >> 1);
			table[i] = value;
		}
		return table;
	}();
	for (uint8_t byte : bytes)
		crc = uint16_t(TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8));
	return crc;
}

} // namespace detail

uint16_t Crc16Arc(std::span<const uint8_t> bytes, uint16_t crc) noexcept {
	return detail::Crc16Reflected(bytes, crc);
}

uint16_t Crc16Modbus(std::span<const uint8_t> bytes, uint16_t crc) noexcept {
	return detail::Crc16Reflected(bytes, crc);
}

uint16_t Crc16Ccitt(std::span<const uint8_t> bytes, uint16_t crc) noexcept {
	static const std::array<uint16_t, 256> TABLE = [] {
		std::array<uint16_t, 256> table{};
		for (uint32_t i = 0; i < 256; ++i) {
			uint16_t value = uint16_t(i << 8);
			for (int bit = 0; bit < 8; ++bit)
				value = (value & 0x8000u) ? uint16_t((value << 1) ^ 0x1021u) : uint16_t(value << 1);
			table[i] = value;
		}
		return table;
	}();
	for (uint8_t byte : bytes)
		crc = uint16_t((crc << 8) ^ TABLE[((crc >> 8) ^ byte) & 0xFF]);
	return crc;
}

uint64_t Crc64Xz(std::span<const uint8_t> bytes, uint64_t crc) noexcept {
	static const std::array<uint64_t, 256> TABLE = [] {
		std::array<uint64_t, 256> table{};
		for (uint64_t i = 0; i < 256; ++i) {
			uint64_t value = i;
			for (int bit = 0; bit < 8; ++bit)
				value = (value & 1u) ? (0xC96C5795D7870F42ull ^ (value >> 1)) : (value >> 1);
			table[i] = value;
		}
		return table;
	}();
	crc = ~crc;
	for (uint8_t byte : bytes)
		crc = TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

// ── Xxh64 ────────────────────────────────────────────────────────────────────

Xxh64::Xxh64(uint64_t seed) noexcept {
	m_lanes = {seed + P1 + P2, seed + P2, seed, seed - P1};
	m_seed = seed;
}

void Xxh64::Update(std::span<const uint8_t> bytes) noexcept {
	size_t at = 0;
	m_total += bytes.size();
	if (m_used > 0) {
		const size_t take = std::min(bytes.size(), size_t(32) - m_used);
		std::memcpy(m_stripe.data() + m_used, bytes.data(), take);
		m_used += take;
		at = take;
		if (m_used < 32)
			return;
		Stripe(m_stripe.data());
		m_used = 0;
	}
	for (; at + 32 <= bytes.size(); at += 32)
		Stripe(bytes.data() + at);
	if (at < bytes.size()) {
		std::memcpy(m_stripe.data(), bytes.data() + at, bytes.size() - at);
		m_used = bytes.size() - at;
	}
}

uint64_t Xxh64::Digest() const noexcept {
	uint64_t h;
	if (m_total >= 32) {
		h = Rotl(m_lanes[0], 1) + Rotl(m_lanes[1], 7) + Rotl(m_lanes[2], 12) +
			Rotl(m_lanes[3], 18);
		for (uint64_t lane : m_lanes)
			h = (h ^ Round(0, lane)) * P1 + P4;
	} else {
		h = m_seed + P5;
	}
	h += m_total;
	size_t at = 0;
	for (; at + 8 <= m_used; at += 8) {
		h ^= Round(0, Load64(m_stripe.data() + at));
		h = Rotl(h, 27) * P1 + P4;
	}
	if (at + 4 <= m_used) {
		h ^= uint64_t(Load32(m_stripe.data() + at)) * P1;
		h = Rotl(h, 23) * P2 + P3;
		at += 4;
	}
	for (; at < m_used; ++at) {
		h ^= uint64_t(m_stripe[at]) * P5;
		h = Rotl(h, 11) * P1;
	}
	h ^= h >> 33;
	h *= P2;
	h ^= h >> 29;
	h *= P3;
	h ^= h >> 32;
	return h;
}

uint64_t Xxh64::Load64(const uint8_t* p) noexcept {
	uint64_t v = 0;
	for (int i = 0; i < 8; ++i)
		v |= uint64_t(p[i]) << (8 * i);
	return v;
}

uint32_t Xxh64::Load32(const uint8_t* p) noexcept {
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
		   (uint32_t(p[3]) << 24);
}

void Xxh64::Stripe(const uint8_t* p) noexcept {
	for (int i = 0; i < 4; ++i)
		m_lanes[size_t(i)] = Round(m_lanes[size_t(i)], Load64(p + 8 * i));
}

} // namespace data::archive
