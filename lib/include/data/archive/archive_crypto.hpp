#pragma once
/**
 * data::archive — cryptographie des archives chiffrées.
 *
 *  - `Aes` : AES (FIPS-197), clés de 128, 192 ou 256 bits — AES-256 est ce
 *    qu'utilisent zip (WinZip AE-2) et 7z. Modes : `AesCtrWinZip` (compteur
 *    128 bits PETIT-boutiste commençant à 1, convention de Brian Gladman
 *    reprise par WinZip) et `AesCbcEncrypt/Decrypt` (7z, sans remplissage :
 *    7-Zip complète à un multiple de 16 et tronque à la taille décompressée).
 *  - `Sha1`, `Sha256`, `Hmac<H>`, `Pbkdf2Hmac<H>` : dérivation des clés
 *    WinZip (PBKDF2-HMAC-SHA1, 1000 itérations), RAR5 (PBKDF2-HMAC-SHA256)
 *    et authentification des données chiffrées ; SHA-256 itéré pour 7z,
 *    SHA-1 itéré pour RAR 3. Sur x86-64, les instructions SHA-NI sont
 *    utilisées quand le processeur les a (détection à l'exécution, repli
 *    logiciel sinon) : les dérivations de 7z (2^19 tours) et RAR (2^18)
 *    passent d'environ une seconde à quelques dizaines de millisecondes.
 *  - `SecureRandomBytes` : sels et vecteurs d'initialisation, tirés du
 *    générateur du système (getrandom, arc4random_buf, BCryptGenRandom —
 *    SDL refuse d'ouvrir /dev/urandom, qui n'est pas un fichier régulier).
 *    Pas de repli sur un générateur non cryptographique : sans source sûre,
 *    le chiffrement ÉCHOUE.
 *  - `ZipCrypto` : chiffrement « traditionnel » PKWARE, uniquement pour
 *    OUVRIR les anciens zip protégés (il est cassé : ne pas l'utiliser pour
 *    protéger quoi que ce soit).
 *
 * Mise en garde : implémentations de référence, en temps non constant ; elles
 * conviennent pour lire et produire des archives, pas pour un serveur exposé
 * à des attaques par canaux auxiliaires.
 */
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#define DATA_ARCHIVE_SHA_NI 1
#include <immintrin.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define DATA_ARCHIVE_SHA_TARGET
#else
#include <cpuid.h>
#define DATA_ARCHIVE_SHA_TARGET __attribute__((target("sha,sse4.1,ssse3")))
#endif
#endif

#if defined(_WIN32)
#include <bcrypt.h>
#include <windows.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <stdlib.h>
#elif defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#endif

#include "archive_crc.hpp"
#include "archive_io.hpp"
#include "archive_stream.hpp"

namespace data::archive {

// ============================================================================
// AES
// ============================================================================

class Aes {
public:
	static constexpr size_t BLOCK_SIZE = 16;

	/// Clé de 16, 24 ou 32 octets.
	[[nodiscard]] static Result<Aes, String> Create(std::span<const uint8_t> key) {
		if (key.size() != 16 && key.size() != 24 && key.size() != 32)
			return Err(
				String::Format("AES : clé de %zu octets (16, 24 ou 32 attendus)", key.size()));
		Aes aes;
		aes.Expand(key);
		return Ok(std::move(aes));
	}

	void EncryptBlock(const uint8_t* in, uint8_t* out) const noexcept {
		uint8_t s[16];
		std::memcpy(s, in, 16);
		AddRoundKey(s, 0);
		for (int round = 1; round < m_rounds; ++round) {
			SubBytes(s, SBox());
			ShiftRows(s);
			MixColumns(s);
			AddRoundKey(s, round);
		}
		SubBytes(s, SBox());
		ShiftRows(s);
		AddRoundKey(s, m_rounds);
		std::memcpy(out, s, 16);
	}

	void DecryptBlock(const uint8_t* in, uint8_t* out) const noexcept {
		uint8_t s[16];
		std::memcpy(s, in, 16);
		AddRoundKey(s, m_rounds);
		for (int round = m_rounds - 1; round >= 1; --round) {
			InvShiftRows(s);
			SubBytes(s, InvSBox());
			AddRoundKey(s, round);
			InvMixColumns(s);
		}
		InvShiftRows(s);
		SubBytes(s, InvSBox());
		AddRoundKey(s, 0);
		std::memcpy(out, s, 16);
	}

	[[nodiscard]] int Rounds() const noexcept { return m_rounds; }

private:
	Aes() = default;

	[[nodiscard]] static const std::array<uint8_t, 256>& SBox() noexcept {
		static constexpr std::array<uint8_t, 256> BOX = {
			0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7,
			0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf,
			0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5,
			0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
			0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e,
			0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
			0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef,
			0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
			0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff,
			0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d,
			0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee,
			0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
			0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5,
			0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e,
			0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e,
			0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
			0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55,
			0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
			0xb0, 0x54, 0xbb, 0x16};
		return BOX;
	}
	[[nodiscard]] static const std::array<uint8_t, 256>& InvSBox() noexcept {
		static const std::array<uint8_t, 256> INVERSE = [] {
			std::array<uint8_t, 256> inverse{};
			for (int i = 0; i < 256; ++i)
				inverse[SBox()[size_t(i)]] = uint8_t(i);
			return inverse;
		}();
		return INVERSE;
	}

	[[nodiscard]] static constexpr uint8_t Xtime(uint8_t x) noexcept {
		return uint8_t((x << 1) ^ ((x & 0x80) ? 0x1B : 0));
	}
	[[nodiscard]] static constexpr uint8_t Mul(uint8_t x, uint8_t y) noexcept {
		uint8_t result = 0;
		while (y) {
			if (y & 1)
				result ^= x;
			x = Xtime(x);
			y >>= 1;
		}
		return result;
	}

	void Expand(std::span<const uint8_t> key) noexcept {
		const int nk = int(key.size() / 4);
		m_rounds = nk + 6;
		const int total = 4 * (m_rounds + 1);
		std::memcpy(m_roundKeys.data(), key.data(), key.size());
		uint8_t rcon = 1;
		for (int i = nk; i < total; ++i) {
			uint8_t temp[4];
			std::memcpy(temp, &m_roundKeys[size_t((i - 1) * 4)], 4);
			if (i % nk == 0) {
				uint8_t first = temp[0];
				temp[0] = uint8_t(SBox()[temp[1]] ^ rcon);
				temp[1] = SBox()[temp[2]];
				temp[2] = SBox()[temp[3]];
				temp[3] = SBox()[first];
				rcon = Xtime(rcon);
			} else if (nk > 6 && i % nk == 4) {
				for (uint8_t& byte : temp)
					byte = SBox()[byte];
			}
			for (int b = 0; b < 4; ++b)
				m_roundKeys[size_t(i * 4 + b)] =
					uint8_t(m_roundKeys[size_t((i - nk) * 4 + b)] ^ temp[b]);
		}
	}

	void AddRoundKey(uint8_t* s, int round) const noexcept {
		for (int i = 0; i < 16; ++i)
			s[i] ^= m_roundKeys[size_t(round * 16 + i)];
	}
	static void SubBytes(uint8_t* s, const std::array<uint8_t, 256>& box) noexcept {
		for (int i = 0; i < 16; ++i)
			s[i] = box[s[i]];
	}
	// État en colonnes : s[colonne * 4 + ligne].
	static void ShiftRows(uint8_t* s) noexcept {
		uint8_t t[16];
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				t[c * 4 + r] = s[((c + r) % 4) * 4 + r];
		std::memcpy(s, t, 16);
	}
	static void InvShiftRows(uint8_t* s) noexcept {
		uint8_t t[16];
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				t[((c + r) % 4) * 4 + r] = s[c * 4 + r];
		std::memcpy(s, t, 16);
	}
	static void MixColumns(uint8_t* s) noexcept {
		for (int c = 0; c < 4; ++c) {
			uint8_t* a = s + c * 4;
			uint8_t a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
			a[0] = uint8_t(Xtime(a0) ^ Xtime(a1) ^ a1 ^ a2 ^ a3);
			a[1] = uint8_t(a0 ^ Xtime(a1) ^ Xtime(a2) ^ a2 ^ a3);
			a[2] = uint8_t(a0 ^ a1 ^ Xtime(a2) ^ Xtime(a3) ^ a3);
			a[3] = uint8_t(Xtime(a0) ^ a0 ^ a1 ^ a2 ^ Xtime(a3));
		}
	}
	static void InvMixColumns(uint8_t* s) noexcept {
		for (int c = 0; c < 4; ++c) {
			uint8_t* a = s + c * 4;
			uint8_t a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
			a[0] = uint8_t(Mul(a0, 14) ^ Mul(a1, 11) ^ Mul(a2, 13) ^ Mul(a3, 9));
			a[1] = uint8_t(Mul(a0, 9) ^ Mul(a1, 14) ^ Mul(a2, 11) ^ Mul(a3, 13));
			a[2] = uint8_t(Mul(a0, 13) ^ Mul(a1, 9) ^ Mul(a2, 14) ^ Mul(a3, 11));
			a[3] = uint8_t(Mul(a0, 11) ^ Mul(a1, 13) ^ Mul(a2, 9) ^ Mul(a3, 14));
		}
	}

	std::array<uint8_t, 16 * 15> m_roundKeys{};
	int m_rounds = 0;
};

/// AES-CTR façon WinZip : bloc compteur de 16 octets incrémenté en
/// petit-boutiste AVANT chaque bloc (premier compteur = 1). Chiffrer et
/// déchiffrer sont la même opération (XOR du flux de clé), appliquée ici en
/// place et par morceaux successifs.
class AesCtrWinZip {
public:
	explicit AesCtrWinZip(Aes aes) noexcept : m_aes(std::move(aes)) {}

	void Apply(std::span<uint8_t> data) noexcept {
		for (uint8_t& byte : data) {
			if (m_used == Aes::BLOCK_SIZE) {
				for (uint8_t& counterByte : m_counter)
					if (++counterByte != 0)
						break;
				m_aes.EncryptBlock(m_counter.data(), m_keystream.data());
				m_used = 0;
			}
			byte ^= m_keystream[m_used++];
		}
	}

private:
	Aes m_aes;
	std::array<uint8_t, 16> m_counter{};
	std::array<uint8_t, 16> m_keystream{};
	size_t m_used = Aes::BLOCK_SIZE;
};

/// AES-CBC sans remplissage ; la taille doit être un multiple de 16.
[[nodiscard]] inline Result<bool, String>
AesCbcDecrypt(const Aes& aes, std::span<const uint8_t, 16> iv, std::span<uint8_t> data) {
	if (data.size() % Aes::BLOCK_SIZE != 0)
		return Err(String("AES-CBC : données de taille non multiple de 16"));
	uint8_t previous[16], current[16], plain[16];
	std::memcpy(previous, iv.data(), 16);
	for (size_t at = 0; at < data.size(); at += 16) {
		std::memcpy(current, data.data() + at, 16);
		aes.DecryptBlock(current, plain);
		for (size_t i = 0; i < 16; ++i)
			data[at + i] = uint8_t(plain[i] ^ previous[i]);
		std::memcpy(previous, current, 16);
	}
	return Ok(true);
}

[[nodiscard]] inline Result<bool, String>
AesCbcEncrypt(const Aes& aes, std::span<const uint8_t, 16> iv, std::span<uint8_t> data) {
	if (data.size() % Aes::BLOCK_SIZE != 0)
		return Err(String("AES-CBC : données de taille non multiple de 16"));
	uint8_t previous[16], block[16];
	std::memcpy(previous, iv.data(), 16);
	for (size_t at = 0; at < data.size(); at += 16) {
		for (size_t i = 0; i < 16; ++i)
			block[i] = uint8_t(data[at + i] ^ previous[i]);
		aes.EncryptBlock(block, data.data() + at);
		std::memcpy(previous, data.data() + at, 16);
	}
	return Ok(true);
}

// ============================================================================
// SHA-1 / SHA-256
// ============================================================================

namespace detail {

[[nodiscard]] constexpr uint32_t Rotl(uint32_t x, int n) noexcept {
	return (x << n) | (x >> (32 - n));
}
[[nodiscard]] constexpr uint32_t Rotr(uint32_t x, int n) noexcept {
	return (x >> n) | (x << (32 - n));
}
[[nodiscard]] inline uint32_t LoadBe32(const uint8_t* p) noexcept {
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline void StoreBe32(uint8_t* p, uint32_t v) noexcept {
	p[0] = uint8_t(v >> 24);
	p[1] = uint8_t(v >> 16);
	p[2] = uint8_t(v >> 8);
	p[3] = uint8_t(v);
}

#if defined(DATA_ARCHIVE_SHA_NI)
/// Le processeur a-t-il les instructions SHA (et SSSE3/SSE4.1) ?
[[nodiscard]] inline bool CpuHasShaNi() noexcept {
	static const bool HAS = [] {
#if defined(_MSC_VER) && !defined(__clang__)
		int info[4];
		__cpuid(info, 0);
		if (info[0] < 7)
			return false;
		__cpuid(info, 1);
		const bool sse = (info[2] & (1 << 9)) && (info[2] & (1 << 19));
		__cpuidex(info, 7, 0);
		return sse && (info[1] & (1 << 29)) != 0;
#else
		unsigned a = 0, b = 0, c = 0, d = 0;
		if (!__get_cpuid(1, &a, &b, &c, &d))
			return false;
		const bool sse = (c & (1u << 9)) && (c & (1u << 19)); // SSSE3, SSE4.1
		if (!__get_cpuid_count(7, 0, &a, &b, &c, &d))
			return false;
		return sse && (b & (1u << 29)) != 0; // SHA
#endif
	}();
	return HAS;
}

/// SHA-256 matériel : 4 tours par paire d'instructions sha256rnds2, message
/// étendu par sha256msg1/msg2. `state` : H0..H7 dans l'ordre standard.
DATA_ARCHIVE_SHA_TARGET inline void Sha256BlocksShaNi(uint32_t* state, const uint8_t* data,
													  size_t blocks, const uint32_t* k) noexcept {
	const __m128i mask = _mm_set_epi64x(0x0c0d0e0f08090a0bLL, 0x0405060700010203LL);
	__m128i tmp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(state));
	__m128i state1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(state + 4));
	tmp = _mm_shuffle_epi32(tmp, 0xB1);				  // CDAB
	state1 = _mm_shuffle_epi32(state1, 0x1B);		  // EFGH
	__m128i state0 = _mm_alignr_epi8(tmp, state1, 8); // ABEF
	state1 = _mm_blend_epi16(state1, tmp, 0xF0);	  // CDGH
	for (; blocks > 0; --blocks, data += 64) {
		const __m128i abefSave = state0, cdghSave = state1;
		__m128i message[4];
		for (int i = 0; i < 4; ++i)
			message[i] = _mm_shuffle_epi8(
				_mm_loadu_si128(reinterpret_cast<const __m128i*>(data + 16 * i)), mask);
		for (int group = 0; group < 16; ++group) {
			__m128i words =
				_mm_add_epi32(message[group & 3],
							  _mm_loadu_si128(reinterpret_cast<const __m128i*>(k + 4 * group)));
			if (group < 12) {
				// W[t..t+3] = msg2(msg1(W[t-16], W[t-12]) + W[t-7..t-4], W[t-4])
				__m128i next = _mm_sha256msg1_epu32(message[group & 3], message[(group + 1) & 3]);
				next = _mm_add_epi32(
					next, _mm_alignr_epi8(message[(group + 3) & 3], message[(group + 2) & 3], 4));
				message[group & 3] = _mm_sha256msg2_epu32(next, message[(group + 3) & 3]);
			}
			state1 = _mm_sha256rnds2_epu32(state1, state0, words);
			words = _mm_shuffle_epi32(words, 0x0E);
			state0 = _mm_sha256rnds2_epu32(state0, state1, words);
		}
		state0 = _mm_add_epi32(state0, abefSave);
		state1 = _mm_add_epi32(state1, cdghSave);
	}
	tmp = _mm_shuffle_epi32(state0, 0x1B);		 // FEBA
	state1 = _mm_shuffle_epi32(state1, 0xB1);	 // DCHG
	state0 = _mm_blend_epi16(tmp, state1, 0xF0); // DCBA
	state1 = _mm_alignr_epi8(state1, tmp, 8);	 // HGFE
	_mm_storeu_si128(reinterpret_cast<__m128i*>(state), state0);
	_mm_storeu_si128(reinterpret_cast<__m128i*>(state + 4), state1);
}

/// SHA-1 matériel : 4 tours par sha1rnds4, E porté par sha1nexte.
DATA_ARCHIVE_SHA_TARGET inline void Sha1BlocksShaNi(uint32_t* state, const uint8_t* data,
													size_t blocks) noexcept {
	const __m128i mask = _mm_set_epi64x(0x0001020304050607LL, 0x08090a0b0c0d0e0fLL);
	__m128i abcd =
		_mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(state)), 0x1B);
	__m128i e0 = _mm_set_epi32(int(state[4]), 0, 0, 0);
	for (; blocks > 0; --blocks, data += 64) {
		const __m128i abcdSave = abcd, e0Save = e0;
		__m128i message[4];
		for (int i = 0; i < 4; ++i)
			message[i] = _mm_shuffle_epi8(
				_mm_loadu_si128(reinterpret_cast<const __m128i*>(data + 16 * i)), mask);
		__m128i e1 = abcd;
		e0 = _mm_add_epi32(e0, message[0]);
		abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
		for (int group = 1; group < 20; ++group) {
			if (group >= 4) {
				// W[t..t+3] = msg2(msg1(W[t-16], W[t-12]) ^ W[t-8], W[t-4])
				__m128i next = _mm_sha1msg1_epu32(message[group & 3], message[(group + 1) & 3]);
				next = _mm_xor_si128(next, message[(group + 2) & 3]);
				message[group & 3] = _mm_sha1msg2_epu32(next, message[(group + 3) & 3]);
			}
			// Les tours se jouent sur 4 immédiats constants (fonctions f0..f3).
			if (group & 1) {
				e1 = _mm_sha1nexte_epu32(e1, message[group & 3]);
				e0 = abcd;
				switch (group / 5) {
				case 0:
					abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
					break;
				case 1:
					abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
					break;
				case 2:
					abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
					break;
				default:
					abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
					break;
				}
			} else {
				e0 = _mm_sha1nexte_epu32(e0, message[group & 3]);
				e1 = abcd;
				switch (group / 5) {
				case 0:
					abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
					break;
				case 1:
					abcd = _mm_sha1rnds4_epu32(abcd, e0, 1);
					break;
				case 2:
					abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
					break;
				default:
					abcd = _mm_sha1rnds4_epu32(abcd, e0, 3);
					break;
				}
			}
		}
		e0 = _mm_sha1nexte_epu32(e0, e0Save);
		abcd = _mm_add_epi32(abcd, abcdSave);
	}
	abcd = _mm_shuffle_epi32(abcd, 0x1B);
	_mm_storeu_si128(reinterpret_cast<__m128i*>(state), abcd);
	state[4] = uint32_t(_mm_extract_epi32(e0, 3));
}
#endif

/// Squelette Merkle–Damgård commun (blocs de 64 octets, longueur 64 bits BE).
template <typename Derived, size_t DIGEST> class Md64 {
public:
	static constexpr size_t BLOCK_SIZE = 64;
	static constexpr size_t DIGEST_SIZE = DIGEST;

	void Update(std::span<const uint8_t> bytes) noexcept {
		if (bytes.empty())
			return;
		m_length += bytes.size();
		size_t at = 0;
		if (m_blockUsed > 0) {
			const size_t take = std::min(BLOCK_SIZE - m_blockUsed, bytes.size());
			std::memcpy(m_block.data() + m_blockUsed, bytes.data(), take);
			m_blockUsed += take;
			at = take;
			if (m_blockUsed < BLOCK_SIZE)
				return;
			static_cast<Derived*>(this)->CompressBlocks(m_block.data(), 1);
			m_blockUsed = 0;
		}
		// Blocs entiers compressés directement depuis l'entrée, sans copie.
		const size_t blocks = (bytes.size() - at) / BLOCK_SIZE;
		if (blocks > 0) {
			static_cast<Derived*>(this)->CompressBlocks(bytes.data() + at, blocks);
			at += blocks * BLOCK_SIZE;
		}
		if (at < bytes.size()) {
			std::memcpy(m_block.data(), bytes.data() + at, bytes.size() - at);
			m_blockUsed = bytes.size() - at;
		}
	}

	[[nodiscard]] std::array<uint8_t, DIGEST> Final() noexcept {
		const uint64_t bits = m_length * 8;
		// 0x80, zéros jusqu'à 56 mod 64, longueur en bits (64 bits BE) : un seul
		// Update.
		uint8_t padding[BLOCK_SIZE + 8] = {0x80};
		const size_t zeros = (m_blockUsed < 56 ? 56 - m_blockUsed : 120 - m_blockUsed) - 1;
		for (int i = 0; i < 8; ++i)
			padding[1 + zeros + size_t(i)] = uint8_t(bits >> (56 - 8 * i));
		Update(std::span<const uint8_t>(padding, 1 + zeros + 8));
		std::array<uint8_t, DIGEST> digest{};
		static_cast<Derived*>(this)->Output(digest.data());
		return digest;
	}

protected:
	std::array<uint8_t, BLOCK_SIZE> m_block{};
	size_t m_blockUsed = 0;
	uint64_t m_length = 0;
};

} // namespace detail

class Sha1 : public detail::Md64<Sha1, 20> {
public:
	void CompressBlocks(const uint8_t* data, size_t blocks) noexcept {
#if defined(DATA_ARCHIVE_SHA_NI)
		if (detail::CpuHasShaNi()) {
			detail::Sha1BlocksShaNi(m_h, data, blocks);
			return;
		}
#endif
		for (size_t i = 0; i < blocks; ++i)
			Compress(data + 64 * i);
	}
	void Compress(const uint8_t* block) noexcept {
		uint32_t w[80];
		for (int i = 0; i < 16; ++i)
			w[i] = detail::LoadBe32(block + i * 4);
		for (int i = 16; i < 80; ++i)
			w[i] = detail::Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4];
		for (int i = 0; i < 80; ++i) {
			uint32_t f, k;
			if (i < 20) {
				f = (b & c) | (~b & d);
				k = 0x5A827999u;
			} else if (i < 40) {
				f = b ^ c ^ d;
				k = 0x6ED9EBA1u;
			} else if (i < 60) {
				f = (b & c) | (b & d) | (c & d);
				k = 0x8F1BBCDCu;
			} else {
				f = b ^ c ^ d;
				k = 0xCA62C1D6u;
			}
			uint32_t temp = detail::Rotl(a, 5) + f + e + k + w[i];
			e = d;
			d = c;
			c = detail::Rotl(b, 30);
			b = a;
			a = temp;
		}
		m_h[0] += a;
		m_h[1] += b;
		m_h[2] += c;
		m_h[3] += d;
		m_h[4] += e;
	}
	void Output(uint8_t* out) const noexcept {
		for (int i = 0; i < 5; ++i)
			detail::StoreBe32(out + i * 4, m_h[i]);
	}

private:
	uint32_t m_h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
};

class Sha256 : public detail::Md64<Sha256, 32> {
public:
	void CompressBlocks(const uint8_t* data, size_t blocks) noexcept {
#if defined(DATA_ARCHIVE_SHA_NI)
		if (detail::CpuHasShaNi()) {
			detail::Sha256BlocksShaNi(m_h, data, blocks, K);
			return;
		}
#endif
		for (size_t i = 0; i < blocks; ++i)
			Compress(data + 64 * i);
	}
	void Compress(const uint8_t* block) noexcept {
		uint32_t w[64];
		for (int i = 0; i < 16; ++i)
			w[i] = detail::LoadBe32(block + i * 4);
		for (int i = 16; i < 64; ++i) {
			uint32_t s0 =
				detail::Rotr(w[i - 15], 7) ^ detail::Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			uint32_t s1 =
				detail::Rotr(w[i - 2], 17) ^ detail::Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4], f = m_h[5], g = m_h[6],
				 h = m_h[7];
		for (int i = 0; i < 64; ++i) {
			uint32_t t1 = h + (detail::Rotr(e, 6) ^ detail::Rotr(e, 11) ^ detail::Rotr(e, 25)) +
						  ((e & f) ^ (~e & g)) + K[i] + w[i];
			uint32_t t2 = (detail::Rotr(a, 2) ^ detail::Rotr(a, 13) ^ detail::Rotr(a, 22)) +
						  ((a & b) ^ (a & c) ^ (b & c));
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		m_h[0] += a;
		m_h[1] += b;
		m_h[2] += c;
		m_h[3] += d;
		m_h[4] += e;
		m_h[5] += f;
		m_h[6] += g;
		m_h[7] += h;
	}
	void Output(uint8_t* out) const noexcept {
		for (int i = 0; i < 8; ++i)
			detail::StoreBe32(out + i * 4, m_h[i]);
	}

private:
	static constexpr uint32_t K[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
		0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
		0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
		0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
		0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
		0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
		0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
		0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
		0xc67178f2};
	uint32_t m_h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
					   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
};

/// HMAC (RFC 2104) sur une fonction de hachage `H` (Sha1, Sha256).
template <typename H> class Hmac {
public:
	explicit Hmac(std::span<const uint8_t> key) noexcept {
		std::array<uint8_t, H::BLOCK_SIZE> block{};
		if (key.size() > H::BLOCK_SIZE) {
			H hash;
			hash.Update(key);
			auto digest = hash.Final();
			std::memcpy(block.data(), digest.data(), digest.size());
		} else if (!key.empty()) {
			std::memcpy(block.data(), key.data(), key.size());
		}
		std::array<uint8_t, H::BLOCK_SIZE> inner{}, outer{};
		for (size_t i = 0; i < H::BLOCK_SIZE; ++i) {
			inner[i] = uint8_t(block[i] ^ 0x36);
			outer[i] = uint8_t(block[i] ^ 0x5C);
		}
		m_inner.Update(inner);
		m_outer.Update(outer);
	}
	void Update(std::span<const uint8_t> bytes) noexcept { m_inner.Update(bytes); }
	[[nodiscard]] std::array<uint8_t, H::DIGEST_SIZE> Final() noexcept {
		auto innerDigest = m_inner.Final();
		m_outer.Update(innerDigest);
		return m_outer.Final();
	}

private:
	H m_inner;
	H m_outer;
};

/// PBKDF2-HMAC (RFC 8018) sur `H` (Sha1, Sha256). Les états HMAC (clé déjà
/// absorbée) sont recopiés à chaque itération plutôt que recalculés.
template <typename H>
[[nodiscard]] inline Bytes Pbkdf2Hmac(std::span<const uint8_t> password,
									  std::span<const uint8_t> salt, uint32_t iterations,
									  size_t length) {
	Bytes output;
	output.reserve(length);
	const Hmac<H> keyed(password);
	for (uint32_t blockIndex = 1; output.size() < length; ++blockIndex) {
		uint8_t counter[4];
		detail::StoreBe32(counter, blockIndex);
		Hmac<H> first = keyed;
		first.Update(salt);
		first.Update(counter);
		auto u = first.Final();
		auto t = u;
		for (uint32_t i = 1; i < iterations; ++i) {
			Hmac<H> next = keyed;
			next.Update(u);
			u = next.Final();
			for (size_t b = 0; b < t.size(); ++b)
				t[b] ^= u[b];
		}
		for (size_t b = 0; b < t.size() && output.size() < length; ++b)
			output.push_back(t[b]);
	}
	return output;
}

[[nodiscard]] inline Bytes Pbkdf2HmacSha1(std::span<const uint8_t> password,
										  std::span<const uint8_t> salt, uint32_t iterations,
										  size_t length) {
	return Pbkdf2Hmac<Sha1>(password, salt, iterations, length);
}

// ============================================================================
// Aléa
// ============================================================================

/// Octets aléatoires de qualité cryptographique (générateur du système).
[[nodiscard]] inline Result<Bytes, String> SecureRandomBytes(size_t count) {
	Bytes bytes(count);
#if defined(_WIN32)
	if (count > 0 && !BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes.data(), ULONG(count),
													 BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
		return Err(String("BCryptGenRandom a échoué : chiffrement impossible"));
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
	arc4random_buf(bytes.data(), count);
#elif defined(__linux__)
	size_t filled = 0;
	while (filled < count) {
		ssize_t got = getrandom(bytes.data() + filled, count - filled, 0);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			return Err(String("getrandom a échoué : chiffrement impossible"));
		}
		filled += size_t(got);
	}
#else
	(void)bytes;
	return Err(String("aucune source d'aléa cryptographique sur cette "
					  "plate-forme : chiffrement impossible"));
#endif
	return Ok(std::move(bytes));
}

// ============================================================================
// Chiffrement zip traditionnel (PKWARE)
// ============================================================================

class ZipCrypto {
public:
	explicit ZipCrypto(const String& password) noexcept {
		for (size_t i = 0; i < password.GetSize(); ++i)
			UpdateKeys(uint8_t(password.CharAt(i)));
	}

	void Decrypt(std::span<uint8_t> data) noexcept {
		for (uint8_t& byte : data) {
			byte = uint8_t(byte ^ KeystreamByte());
			UpdateKeys(byte);
		}
	}
	void Encrypt(std::span<uint8_t> data) noexcept {
		for (uint8_t& byte : data) {
			uint8_t key = KeystreamByte();
			UpdateKeys(byte);
			byte = uint8_t(byte ^ key);
		}
	}

private:
	[[nodiscard]] uint8_t KeystreamByte() const noexcept {
		const uint32_t temp = (m_keys[2] & 0xFFFFu) | 2u; // produit en 32 bits non signés
		return uint8_t((temp * (temp ^ 1)) >> 8);
	}
	void UpdateKeys(uint8_t byte) noexcept {
		m_keys[0] = Crc32UpdateRaw(m_keys[0], byte);
		m_keys[1] = (m_keys[1] + (m_keys[0] & 0xFF)) * 134775813u + 1;
		m_keys[2] = Crc32UpdateRaw(m_keys[2], uint8_t(m_keys[1] >> 24));
	}

	uint32_t m_keys[3] = {0x12345678u, 0x23456789u, 0x34567890u};
};

// ============================================================================
// AES-CBC en flux
// ============================================================================

/// Déchiffrement AES-CBC en flux (7z, RAR) : blocs de 16 octets, sortie
/// tronquée à `outSize` (le chiffré est complété jusqu'à un multiple de 16).
class AesCbcDecryptImpl final : public DecoderImpl {
public:
	AesCbcDecryptImpl(ArchiveStream inner, const Aes& aes, const std::array<uint8_t, 16>& iv,
					  uint64_t outSize, StreamStatePtr state)
		: DecoderImpl(std::move(state), Some(outSize)), m_inner(std::move(inner)), m_aes(aes),
		  m_iv(iv), m_previous(iv), m_outSize(outSize) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override {
		if (m_produced >= m_outSize)
			return Ok(size_t(0));
		if (m_position == m_end) {
			auto got = StreamRead(m_inner, m_buffer, sizeof(m_buffer));
			if (got.IsError())
				return got;
			if (got.Value() == 0 || got.Value() % Aes::BLOCK_SIZE != 0)
				return Err(MakeError(ErrorKind::CORRUPT, String("données AES tronquées")));
			uint8_t plain[16];
			for (size_t at = 0; at < got.Value(); at += 16) {
				uint8_t* block = m_buffer + at;
				m_aes.DecryptBlock(block, plain);
				for (size_t i = 0; i < 16; ++i) {
					const uint8_t cipher = block[i];
					block[i] = uint8_t(plain[i] ^ m_previous[i]);
					m_previous[i] = cipher;
				}
			}
			m_position = 0;
			m_end = got.Value();
		}
		const size_t take =
			size_t(std::min<uint64_t>({max, m_end - m_position, m_outSize - m_produced}));
		std::memcpy(out, m_buffer + m_position, take);
		m_position += take;
		m_produced += take;
		return Ok(take);
	}
	bool Restart() override {
		if (m_inner.io.Seek(0, SDL_IO_SEEK_SET) != 0)
			return false;
		m_previous = m_iv;
		m_position = m_end = 0;
		m_produced = 0;
		return true;
	}

private:
	ArchiveStream m_inner;
	Aes m_aes;
	std::array<uint8_t, 16> m_iv, m_previous;
	uint64_t m_outSize, m_produced = 0;
	uint8_t m_buffer[1 << 14];
	size_t m_position = 0, m_end = 0;
};

/// Chiffrement AES-CBC en flux ; le dernier bloc est complété par des zéros.
class AesCbcEncryptImpl final : public EncoderImpl {
public:
	AesCbcEncryptImpl(ArchiveStream sink, const Aes& aes, const std::array<uint8_t, 16>& iv,
					  StreamStatePtr state)
		: EncoderImpl(std::move(sink), std::move(state)), m_aes(aes), m_previous(iv) {}

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override {
		while (size > 0) {
			const size_t take = std::min(size, sizeof(m_buffer) - m_fill);
			std::memcpy(m_buffer + m_fill, data, take);
			m_fill += take;
			data += take;
			size -= take;
			if (m_fill == sizeof(m_buffer))
				if (auto flushed = Flush(m_fill); flushed.IsError())
					return flushed;
		}
		return Ok(true);
	}
	Result<bool, ArchiveError> Finish() override {
		const size_t padded = (m_fill + 15) / 16 * 16;
		std::memset(m_buffer + m_fill, 0, padded - m_fill);
		return Flush(padded);
	}

private:
	Result<bool, ArchiveError> Flush(size_t size) {
		const size_t blocks = size / 16 * 16;
		for (size_t at = 0; at < blocks; at += 16) {
			uint8_t* block = m_buffer + at;
			for (size_t i = 0; i < 16; ++i)
				block[i] ^= m_previous[i];
			m_aes.EncryptBlock(block, block);
			std::memcpy(m_previous.data(), block, 16);
		}
		auto emitted = Emit(m_buffer, blocks);
		std::memmove(m_buffer, m_buffer + blocks, m_fill - std::min(m_fill, blocks));
		m_fill -= std::min(m_fill, blocks);
		return emitted;
	}

	Aes m_aes;
	std::array<uint8_t, 16> m_previous;
	uint8_t m_buffer[1 << 14];
	size_t m_fill = 0;
};

} // namespace data::archive
