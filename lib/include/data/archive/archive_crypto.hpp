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
	[[nodiscard]] static Result<Aes, String> Create(std::span<const uint8_t> key);

	void EncryptBlock(const uint8_t* in, uint8_t* out) const noexcept;

	void DecryptBlock(const uint8_t* in, uint8_t* out) const noexcept;

	[[nodiscard]] int Rounds() const noexcept { return m_rounds; }

private:
	Aes() = default;

	[[nodiscard]] static const std::array<uint8_t, 256>& SBox() noexcept;
	[[nodiscard]] static const std::array<uint8_t, 256>& InvSBox() noexcept;

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

	void Expand(std::span<const uint8_t> key) noexcept;

	void AddRoundKey(uint8_t* s, int round) const noexcept;
	static void SubBytes(uint8_t* s, const std::array<uint8_t, 256>& box) noexcept;
	// État en colonnes : s[colonne * 4 + ligne].
	static void ShiftRows(uint8_t* s) noexcept;
	static void InvShiftRows(uint8_t* s) noexcept;
	static void MixColumns(uint8_t* s) noexcept;
	static void InvMixColumns(uint8_t* s) noexcept;

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

	void Apply(std::span<uint8_t> data) noexcept;

private:
	Aes m_aes;
	std::array<uint8_t, 16> m_counter{};
	std::array<uint8_t, 16> m_keystream{};
	size_t m_used = Aes::BLOCK_SIZE;
};

/// AES-CBC sans remplissage ; la taille doit être un multiple de 16.
[[nodiscard]] Result<bool, String>
AesCbcDecrypt(const Aes& aes, std::span<const uint8_t, 16> iv, std::span<uint8_t> data);

[[nodiscard]] Result<bool, String>
AesCbcEncrypt(const Aes& aes, std::span<const uint8_t, 16> iv, std::span<uint8_t> data);

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
[[nodiscard]] uint32_t LoadBe32(const uint8_t* p) noexcept;
void StoreBe32(uint8_t* p, uint32_t v) noexcept;

#if defined(DATA_ARCHIVE_SHA_NI)
/// Le processeur a-t-il les instructions SHA (et SSSE3/SSE4.1) ?
[[nodiscard]] bool CpuHasShaNi() noexcept;

/// SHA-256 matériel : 4 tours par paire d'instructions sha256rnds2, message
/// étendu par sha256msg1/msg2. `state` : H0..H7 dans l'ordre standard.
DATA_ARCHIVE_SHA_TARGET void Sha256BlocksShaNi(uint32_t* state, const uint8_t* data,
													  size_t blocks, const uint32_t* k) noexcept;

/// SHA-1 matériel : 4 tours par sha1rnds4, E porté par sha1nexte.
DATA_ARCHIVE_SHA_TARGET void Sha1BlocksShaNi(uint32_t* state, const uint8_t* data,
													size_t blocks) noexcept;
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
	void CompressBlocks(const uint8_t* data, size_t blocks) noexcept;
	void Compress(const uint8_t* block) noexcept;
	void Output(uint8_t* out) const noexcept;

private:
	uint32_t m_h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
};

class Sha256 : public detail::Md64<Sha256, 32> {
public:
	void CompressBlocks(const uint8_t* data, size_t blocks) noexcept;
	void Compress(const uint8_t* block) noexcept;
	void Output(uint8_t* out) const noexcept;

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

[[nodiscard]] Bytes Pbkdf2HmacSha1(std::span<const uint8_t> password,
										  std::span<const uint8_t> salt, uint32_t iterations,
										  size_t length);

// ============================================================================
// Aléa
// ============================================================================

/// Octets aléatoires de qualité cryptographique (générateur du système).
[[nodiscard]] Result<Bytes, String> SecureRandomBytes(size_t count);

// ============================================================================
// Chiffrement zip traditionnel (PKWARE)
// ============================================================================

class ZipCrypto {
public:
	explicit ZipCrypto(const String& password) noexcept;

	void Decrypt(std::span<uint8_t> data) noexcept;
	void Encrypt(std::span<uint8_t> data) noexcept;

private:
	[[nodiscard]] uint8_t KeystreamByte() const noexcept;
	void UpdateKeys(uint8_t byte) noexcept;

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
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

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
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override;

private:
	Result<bool, ArchiveError> Flush(size_t size);

	Aes m_aes;
	std::array<uint8_t, 16> m_previous;
	uint8_t m_buffer[1 << 14];
	size_t m_fill = 0;
};

} // namespace data::archive
