#pragma once
/**
 * data::archive — sommes de contrôle.
 *
 *  - `Crc32` : CRC-32 IEEE 802.3 (zip, gzip, 7z, xz, PNG). Tables « slicing by
 *    8 » : ~8 fois plus rapide que la version octet par octet sur de gros
 *    fichiers, même résultat.
 *  - `Crc16Arc` : CRC-16/ARC (polynôme réfléchi 0xA001, init 0x0000) et
 *    `Crc16Modbus` (même polynôme, init 0xFFFF — en-tête de ROM NDS).
 *  - `Crc16Ccitt` : CRC-16/CCITT-FALSE (0x1021 non réfléchi, init 0xFFFF).
 *  - `Crc64Xz` : CRC-64/XZ (polynôme réfléchi 0xC96C5795D7870F42), contrôle
 *    d'intégrité par défaut des fichiers .xz.
 *  - `Xxh64` : XXH64 (Yann Collet), incrémental ; zstd en garde les 32 bits
 *    bas comme contrôle de trame. XXH64("", 0) = EF46DB3751D8E999.
 *
 * Chaque fonction accepte une valeur précédente pour chaîner des blocs :
 * `Crc32(b, Crc32(a)) == Crc32(a + b)`.
 * Valeurs de contrôle sur « 123456789 » (vérifiées par les tests) : CRC-32
 * CBF43926, CRC-16/ARC BB3D, CRC-16/MODBUS 4B37, CRC-16/CCITT-FALSE 29B1,
 * CRC-64/XZ 995DC9BBDF1939FA.
 */
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace data::archive {

namespace detail {

[[nodiscard]] const std::array<std::array<uint32_t, 256>, 8>& Crc32Tables() noexcept;

} // namespace detail

/// Mise à jour d'un octet SANS inversion initiale/finale (utilisée par le
/// chiffrement zip traditionnel, qui manipule l'état brut).
[[nodiscard]] uint32_t Crc32UpdateRaw(uint32_t state, uint8_t byte) noexcept;

[[nodiscard]] uint32_t Crc32(std::span<const uint8_t> bytes, uint32_t crc = 0) noexcept;

namespace detail {

[[nodiscard]] uint16_t Crc16Reflected(std::span<const uint8_t> bytes,
											 uint16_t crc) noexcept;

} // namespace detail

/// CRC-16/ARC (aussi appelé CRC-16/IBM, LHA, ARJ).
[[nodiscard]] uint16_t Crc16Arc(std::span<const uint8_t> bytes,
									   uint16_t crc = 0x0000) noexcept;

/// CRC-16/MODBUS : même polynôme, valeur initiale 0xFFFF.
[[nodiscard]] uint16_t Crc16Modbus(std::span<const uint8_t> bytes,
										  uint16_t crc = 0xFFFF) noexcept;

/// CRC-16/CCITT-FALSE (MSB d'abord).
[[nodiscard]] uint16_t Crc16Ccitt(std::span<const uint8_t> bytes,
										 uint16_t crc = 0xFFFF) noexcept;

[[nodiscard]] uint64_t Crc64Xz(std::span<const uint8_t> bytes, uint64_t crc = 0) noexcept;

/// XXH64 incrémental (graine 0 par défaut).
class Xxh64 {
public:
	explicit Xxh64(uint64_t seed = 0) noexcept;

	void Update(std::span<const uint8_t> bytes) noexcept;

	[[nodiscard]] uint64_t Digest() const noexcept;

private:
	static constexpr uint64_t P1 = 0x9E3779B185EBCA87ull, P2 = 0xC2B2AE3D27D4EB4Full,
							  P3 = 0x165667B19E3779F9ull, P4 = 0x85EBCA77C2B2AE63ull,
							  P5 = 0x27D4EB2F165667C5ull;
	[[nodiscard]] static constexpr uint64_t Rotl(uint64_t x, int r) noexcept {
		return (x << r) | (x >> (64 - r));
	}
	[[nodiscard]] static constexpr uint64_t Round(uint64_t acc, uint64_t lane) noexcept {
		return Rotl(acc + lane * P2, 31) * P1;
	}
	[[nodiscard]] static uint64_t Load64(const uint8_t* p) noexcept;
	[[nodiscard]] static uint32_t Load32(const uint8_t* p) noexcept;
	void Stripe(const uint8_t* p) noexcept;

	std::array<uint64_t, 4> m_lanes{};
	std::array<uint8_t, 32> m_stripe{};
	size_t m_used = 0;
	uint64_t m_total = 0;
	uint64_t m_seed = 0;
};

} // namespace data::archive
