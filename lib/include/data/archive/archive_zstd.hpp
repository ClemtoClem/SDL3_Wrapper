#pragma once
/**
 * data::archive — Zstandard (RFC 8878) : décompression et compression.
 *
 * Décodage complet du format :
 *  - trames zstd (magic 0xFD2FB528) et trames ignorables (0x184D2A5x),
 *    concaténées ; taille de fenêtre (jusqu'à 2 Gio, mémoire allouée au fil
 *    des données), taille de contenu, contrôle XXH64 ;
 *  - blocs bruts, RLE et compressés (≤ 128 Kio) ;
 *  - littéraux bruts, RLE, Huffman sur 1 ou 4 flux (poids directs ou
 *    compressés FSE, tables réutilisées d'un bloc à l'autre) ;
 *  - séquences FSE (tables prédéfinies, RLE, décrites, répétées), offsets
 *    répétés, correspondances qui remontent dans les blocs précédents.
 * Les dictionnaires externes (identifiant de dictionnaire non nul) sont
 * refusés (`UNSUPPORTED`).
 *
 * Compression (`OpenZstdEncoder`) : LZ77 à chaînes de hachage sur une fenêtre
 * glissante, littéraux Huffman (poids directs ou FSE), séquences FSE
 * (tables prédéfinies, RLE ou normalisées selon les fréquences), contrôle
 * XXH64 ; décodable par zstd(1) (vérifié).
 *
 * Les champs de trame et de bloc sont lus par un `InputBuffer` ; les flux de
 * bits (lus À REBOURS, particularité de zstd) sont décodés en mémoire, un
 * bloc (≤ 128 Kio) à la fois.
 */
#include "../../core/core.hpp"
#include "archive_crc.hpp"
#include "archive_deflate.hpp"
#include "archive_io.hpp"
#include "archive_stream.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

namespace data::archive {

namespace detail::zstd {

inline constexpr uint32_t FRAME_MAGIC = 0xFD2FB528u;
inline constexpr uint32_t SKIPPABLE_MASK = 0xFFFFFFF0u;
inline constexpr uint32_t SKIPPABLE_MAGIC = 0x184D2A50u;
inline constexpr size_t BLOCK_MAX = size_t(1) << 17;
inline constexpr uint64_t WINDOW_LIMIT = uint64_t(1) << 31;

// Codes de longueur de littéraux et de correspondance : base et bits en plus.
inline constexpr uint32_t LL_BASE[36] = {
	0,	1,	2,	3,	4,	5,	6,	7,	8,	 9,	  10,  11,	 12,   13,	 14,   15,	  16,	 18,
	20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
inline constexpr uint8_t LL_BITS[36] = {0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,
										0, 0, 0, 0, 1, 1,  1,  1,  2,  2,  3,  3,
										4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
inline constexpr uint32_t ML_BASE[53] = {
	3,	4,	5,	6,	7,	8,	9,	10,	 11,  12,  13,	 14,   15,	 16,   17,	  18,	 19,   20,
	21, 22, 23, 24, 25, 26, 27, 28,	 29,  30,  31,	 32,   33,	 34,   35,	  37,	 39,   41,
	43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};
inline constexpr uint8_t ML_BITS[53] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0, 0,
										0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  1,  1,  1, 1,
										2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

// Distributions prédéfinies (RFC 8878 §3.1.1.3.2.2).
inline constexpr int16_t LL_DEFAULT[36] = {4, 3, 2, 2, 2, 2, 2, 2, 2,  2,  2,  2,
										   2, 1, 1, 1, 2, 2, 2, 2, 2,  2,  2,  2,
										   2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
inline constexpr int16_t ML_DEFAULT[53] = {
	1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	 1,	 1,	 1,	 1,	 1,	 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
inline constexpr int16_t OF_DEFAULT[29] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1,  1,  1,  1,  1, 1,
										   1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};
inline constexpr int LL_DEFAULT_LOG = 6, ML_DEFAULT_LOG = 6, OF_DEFAULT_LOG = 5;
inline constexpr int LL_MAX_LOG = 9, ML_MAX_LOG = 9, OF_MAX_LOG = 8;
inline constexpr int HUFFMAN_MAX_BITS = 11;

[[nodiscard]] inline int HighBit(uint32_t value) noexcept {
	return 31 - std::countl_zero(value);
}

/// Lecture de bits À REBOURS : le dernier octet porte un bit marqueur, les
/// bits sont consommés du plus haut vers le plus bas.
class BackwardBits {
public:
	[[nodiscard]] bool Init(const uint8_t* data, size_t size) noexcept {
		m_data = data;
		m_size = size;
		if (size == 0 || data[size - 1] == 0)
			return false;
		m_position = int64_t(size - 1) * 8 + HighBit(data[size - 1]);
		return true;
	}
	/// Lit `count` bits (≤ 32) ; au-delà du début, des zéros entrent par la
	/// droite.
	[[nodiscard]] uint32_t Read(int count) noexcept {
		const uint32_t value = Peek(count);
		m_position -= count;
		return value;
	}
	[[nodiscard]] uint32_t Peek(int count) const noexcept {
		if (count == 0)
			return 0;
		const int64_t start = m_position - count;
		if (start >= 0)
			return Extract(uint64_t(start), count);
		if (m_position <= 0)
			return 0;
		return Extract(0, int(m_position)) << (count - m_position);
	}
	void Skip(int count) noexcept { m_position -= count; }
	/// Bits restants (négatif si on a lu au-delà du début).
	[[nodiscard]] int64_t Remaining() const noexcept { return m_position; }

private:
	[[nodiscard]] uint32_t Extract(uint64_t start, int count) const noexcept {
		const size_t first = size_t(start / 8);
		const size_t last = size_t((start + uint64_t(count) - 1) / 8);
		uint64_t window = 0;
		for (size_t i = last + 1; i-- > first;)
			window = (window << 8) | m_data[i];
		return uint32_t((window >> (start % 8)) & ((uint64_t(1) << count) - 1));
	}

	const uint8_t* m_data = nullptr;
	size_t m_size = 0;
	int64_t m_position = 0;
};

/// Lecture de bits vers l'avant (LSB d'abord), zéros au-delà de la fin.
class ForwardBits {
public:
	ForwardBits(const uint8_t* data, size_t size) noexcept : m_data(data), m_size(size) {}
	[[nodiscard]] uint32_t Peek(int count) const noexcept {
		uint32_t value = 0;
		for (int i = 0; i < count; ++i) {
			const uint64_t bit = m_position + uint64_t(i);
			const size_t byte = size_t(bit / 8);
			if (byte < m_size && ((m_data[byte] >> (bit % 8)) & 1))
				value |= uint32_t(1) << i;
		}
		return value;
	}
	void Skip(int count) noexcept { m_position += uint64_t(count); }
	[[nodiscard]] uint64_t BytesUsed() const noexcept { return (m_position + 7) / 8; }

private:
	const uint8_t* m_data;
	size_t m_size;
	uint64_t m_position = 0;
};

/// Table de décodage FSE.
struct FseTable {
	struct Entry {
		uint16_t symbol = 0;
		uint8_t bits = 0;
		uint16_t baseline = 0;
	};
	std::vector<Entry> entries;
	int accuracyLog = 0;

	/// Construit la table depuis les probabilités normalisées (-1 = « moins de 1
	/// »).
	[[nodiscard]] bool Build(std::span<const int16_t> normalized, int log) {
		accuracyLog = log;
		const uint32_t size = uint32_t(1) << log;
		entries.assign(size, Entry{});
		std::vector<uint32_t> next(normalized.size());
		int32_t high = int32_t(size) - 1;
		for (size_t s = 0; s < normalized.size(); ++s) {
			if (normalized[s] == -1) {
				if (high < 0)
					return false;
				entries[size_t(high--)].symbol = uint16_t(s);
				next[s] = 1;
			} else {
				next[s] = uint32_t(std::max<int16_t>(normalized[s], 0));
			}
		}
		const uint32_t step = (size >> 1) + (size >> 3) + 3;
		const uint32_t mask = size - 1;
		uint32_t position = 0;
		for (size_t s = 0; s < normalized.size(); ++s) {
			for (int16_t i = 0; i < normalized[s]; ++i) {
				entries[position].symbol = uint16_t(s);
				do {
					position = (position + step) & mask;
				} while (int32_t(position) > high);
			}
		}
		if (position != 0)
			return false;
		for (uint32_t u = 0; u < size; ++u) {
			const uint16_t s = entries[u].symbol;
			const uint32_t state = next[s]++;
			if (state == 0)
				return false;
			const int bits = log - HighBit(state);
			entries[u].bits = uint8_t(bits);
			entries[u].baseline = uint16_t((state << bits) - size);
		}
		return true;
	}

	/// Table « RLE » : un seul symbole, aucun bit.
	void BuildRle(uint16_t symbol) {
		accuracyLog = 0;
		entries.assign(1, Entry{symbol, 0, 0});
	}
};

/// Lit une description de table FSE (probabilités normalisées) ; rend le
/// nombre d'octets consommés.
[[nodiscard]] inline Result<size_t, String>
ReadFseDescription(const uint8_t* data, size_t size, int maxLog, int maxSymbol, FseTable& table) {
	ForwardBits bits(data, size);
	const int log = int(bits.Peek(4)) + 5;
	bits.Skip(4);
	if (log > maxLog)
		return Err(String("zstd : précision FSE trop grande"));
	int32_t remaining = (1 << log) + 1;
	int32_t threshold = 1 << log;
	int bitCount = log + 1;
	std::vector<int16_t> normalized;
	bool previousZero = false;
	while (remaining > 1 && int(normalized.size()) <= maxSymbol) {
		if (previousZero) {
			size_t target = normalized.size();
			while (bits.Peek(16) == 0xFFFF) {
				target += 24;
				bits.Skip(16);
			}
			while (bits.Peek(2) == 3) {
				target += 3;
				bits.Skip(2);
			}
			target += bits.Peek(2);
			bits.Skip(2);
			if (int(target) > maxSymbol + 1)
				return Err(String("zstd : trop de symboles FSE"));
			normalized.resize(target, 0);
			if (int(normalized.size()) > maxSymbol)
				break;
		}
		const int32_t max = (2 * threshold - 1) - remaining;
		int32_t count;
		const int32_t low = int32_t(bits.Peek(bitCount - 1));
		if (low < max) {
			count = low;
			bits.Skip(bitCount - 1);
		} else {
			count = int32_t(bits.Peek(bitCount));
			if (count >= threshold)
				count -= max;
			bits.Skip(bitCount);
		}
		--count; // -1 = probabilité « inférieure à 1 »
		remaining -= count < 0 ? -count : count;
		normalized.push_back(int16_t(count));
		previousZero = count == 0;
		while (remaining < threshold) {
			--bitCount;
			threshold >>= 1;
		}
	}
	if (remaining != 1 || int(normalized.size()) > maxSymbol + 1)
		return Err(String("zstd : description FSE invalide"));
	const uint64_t used = bits.BytesUsed();
	if (used > size)
		return Err(String("zstd : description FSE tronquée"));
	if (!table.Build(normalized, log))
		return Err(String("zstd : table FSE invalide"));
	return Ok(size_t(used));
}

/// Table de décodage de Huffman (index = `maxBits` bits lus d'avance).
struct HuffmanTable {
	struct Entry {
		uint8_t symbol = 0;
		uint8_t bits = 0;
	};
	std::vector<Entry> entries;
	int maxBits = 0;

	[[nodiscard]] bool BuildFromWeights(const std::vector<uint8_t>& givenWeights) {
		// Le poids du dernier symbole est déduit : il complète la somme à une
		// puissance de 2.
		uint32_t total = 0;
		for (uint8_t w : givenWeights) {
			if (w > HUFFMAN_MAX_BITS)
				return false;
			if (w > 0)
				total += uint32_t(1) << (w - 1);
		}
		if (total == 0)
			return false;
		maxBits = HighBit(total) + 1;
		const uint32_t rest = (uint32_t(1) << maxBits) - total;
		if (maxBits > HUFFMAN_MAX_BITS || (rest & (rest - 1)) != 0)
			return false;
		std::vector<uint8_t> weights = givenWeights;
		weights.push_back(uint8_t(HighBit(rest) + 1));
		if (weights.size() > 256)
			return false;
		std::array<uint32_t, HUFFMAN_MAX_BITS + 2> rankStart{};
		for (uint8_t w : weights)
			if (w > 0)
				rankStart[w] += uint32_t(1) << (w - 1);
		uint32_t next = 0;
		for (int w = 1; w <= maxBits; ++w) {
			const uint32_t current = next;
			next += rankStart[size_t(w)];
			rankStart[size_t(w)] = current;
		}
		entries.assign(size_t(1) << maxBits, Entry{});
		for (size_t symbol = 0; symbol < weights.size(); ++symbol) {
			const uint8_t w = weights[symbol];
			if (w == 0)
				continue;
			const uint32_t length = uint32_t(1) << (w - 1);
			for (uint32_t i = 0; i < length; ++i)
				entries[rankStart[w] + i] = Entry{uint8_t(symbol), uint8_t(maxBits + 1 - w)};
			rankStart[w] += length;
		}
		return true;
	}
};

/// Décodeur de trames zstd.
class Decoder {
public:
	explicit Decoder(InputBuffer& input, uint64_t sizeLimit)
		: m_input(&input), m_sizeLimit(sizeLimit) {}

	void Reset() {
		m_mode = Mode::FRAME;
		m_frames = 0;
		m_total = 0;
		m_ready.clear();
		m_readyAt = 0;
	}

	/// 0 = fin de toutes les trames.
	[[nodiscard]] Result<size_t, String> Decode(uint8_t* out, size_t max) {
		for (;;) {
			if (m_readyAt < m_ready.size()) {
				const size_t take = std::min(max, m_ready.size() - m_readyAt);
				std::memcpy(out, m_ready.data() + m_readyAt, take);
				m_readyAt += take;
				return Ok(take);
			}
			m_ready.clear();
			m_readyAt = 0;
			Option<String> error = NONE;
			switch (m_mode) {
			case Mode::DONE:
				return Ok(size_t(0));
			case Mode::FRAME:
				error = ReadFrameHeader();
				break;
			case Mode::BLOCK:
				error = ReadBlock();
				break;
			}
			if (error.IsSome())
				return Err(error.Unwrap());
		}
	}

private:
	enum class Mode : uint8_t { FRAME, BLOCK, DONE };

	bool ReadExact(uint8_t* out, size_t size) { return m_input->ReadRaw(out, size) == size; }

	Option<String> ReadFrameHeader() {
		uint8_t magicBytes[4];
		const size_t got = m_input->ReadRaw(magicBytes, 4);
		if (got == 0 && m_frames > 0) {
			m_mode = Mode::DONE;
			return NONE;
		}
		if (got != 4)
			return Some(String(m_frames > 0 ? "zstd : données inattendues après la trame"
											: "zstd : fichier vide"));
		const uint32_t magic = uint32_t(magicBytes[0]) | (uint32_t(magicBytes[1]) << 8) |
							   (uint32_t(magicBytes[2]) << 16) | (uint32_t(magicBytes[3]) << 24);
		if ((magic & SKIPPABLE_MASK) == SKIPPABLE_MAGIC) {
			uint8_t sizeBytes[4];
			if (!ReadExact(sizeBytes, 4))
				return Some(String("zstd : trame ignorable tronquée"));
			const uint32_t size = uint32_t(sizeBytes[0]) | (uint32_t(sizeBytes[1]) << 8) |
								  (uint32_t(sizeBytes[2]) << 16) | (uint32_t(sizeBytes[3]) << 24);
			if (!m_input->Skip(size))
				return Some(String("zstd : trame ignorable tronquée"));
			++m_frames;
			return NONE;
		}
		if (magic != FRAME_MAGIC)
			return Some(String(m_frames > 0 ? "zstd : données inattendues après la trame"
											: "zstd : signature absente"));
		uint8_t descriptor = 0;
		if (!m_input->Byte(descriptor))
			return Some(String("zstd : en-tête de trame tronqué"));
		if (descriptor & 0x08)
			return Some(String("zstd : bit réservé de l'en-tête de trame positionné"));
		const int fcsFlag = descriptor >> 6;
		const bool singleSegment = (descriptor & 0x20) != 0;
		m_checksum = (descriptor & 0x04) != 0;
		const int dictionaryFlag = descriptor & 0x03;
		uint64_t window = 0;
		if (!singleSegment) {
			uint8_t windowByte = 0;
			if (!m_input->Byte(windowByte))
				return Some(String("zstd : en-tête de trame tronqué"));
			const int exponent = windowByte >> 3, mantissa = windowByte & 7;
			const uint64_t base = uint64_t(1) << (10 + exponent);
			window = base + (base / 8) * uint64_t(mantissa);
		}
		static constexpr int DICTIONARY_SIZES[4] = {0, 1, 2, 4};
		uint8_t field[8] = {};
		if (!ReadExact(field, size_t(DICTIONARY_SIZES[dictionaryFlag])))
			return Some(String("zstd : en-tête de trame tronqué"));
		uint32_t dictionary = 0;
		for (int i = 0; i < DICTIONARY_SIZES[dictionaryFlag]; ++i)
			dictionary |= uint32_t(field[i]) << (8 * i);
		if (dictionary != 0)
			return Some(String("zstd : trame compressée avec un dictionnaire externe (non géré)"));
		static constexpr int FCS_SIZES[4] = {0, 2, 4, 8};
		const int fcsSize = fcsFlag == 0 && singleSegment ? 1 : FCS_SIZES[fcsFlag];
		if (!ReadExact(field, size_t(fcsSize)))
			return Some(String("zstd : en-tête de trame tronqué"));
		m_contentSize = NONE;
		if (fcsSize > 0) {
			uint64_t value = 0;
			for (int i = 0; i < fcsSize; ++i)
				value |= uint64_t(field[i]) << (8 * i);
			if (fcsSize == 2)
				value += 256;
			m_contentSize = Some(value);
		}
		if (singleSegment)
			window = m_contentSize.UnwrapOr(0);
		if (window > WINDOW_LIMIT)
			return Some(String("zstd : fenêtre supérieure à 2 Gio refusée"));
		m_windowSize = std::max<uint64_t>(window, 1);
		m_window.Reset(std::max<uint64_t>(window, 1024));
		m_blockMax = size_t(std::min<uint64_t>(m_windowSize, BLOCK_MAX));
		m_frameOut = 0;
		m_hash = Xxh64();
		m_repeats = {1, 4, 8};
		m_haveHuffman = false;
		m_haveTables = {false, false, false};
		m_mode = Mode::BLOCK;
		return NONE;
	}

	Option<String> ReadBlock() {
		uint8_t header[3];
		if (!ReadExact(header, 3))
			return Some(String("zstd : en-tête de bloc tronqué"));
		const uint32_t value =
			uint32_t(header[0]) | (uint32_t(header[1]) << 8) | (uint32_t(header[2]) << 16);
		const bool last = value & 1;
		const int type = int((value >> 1) & 3);
		const size_t size = value >> 3;
		if (type == 3)
			return Some(String("zstd : type de bloc réservé"));
		if (type == 0) { // brut
			if (size > m_blockMax)
				return Some(String("zstd : bloc brut trop grand"));
			m_ready.resize(size);
			if (!ReadExact(m_ready.data(), size))
				return Some(String("zstd : bloc brut tronqué"));
			for (uint8_t byte : m_ready)
				m_window.Put(byte);
		} else if (type == 1) { // RLE
			if (size > m_blockMax)
				return Some(String("zstd : bloc RLE trop grand"));
			uint8_t byte = 0;
			if (!m_input->Byte(byte))
				return Some(String("zstd : bloc RLE tronqué"));
			m_ready.assign(size, byte);
			for (size_t i = 0; i < size; ++i)
				m_window.Put(byte);
		} else {
			if (size > m_blockMax)
				return Some(String("zstd : bloc compressé trop grand"));
			m_block.resize(size);
			if (!ReadExact(m_block.data(), size))
				return Some(String("zstd : bloc compressé tronqué"));
			if (auto error = DecodeCompressedBlock(); error.IsSome())
				return error;
		}
		m_frameOut += m_ready.size();
		m_total += m_ready.size();
		if (m_total > m_sizeLimit)
			return Some(String("zstd : taille décompressée au-delà de la limite"));
		if (m_checksum)
			m_hash.Update(m_ready);
		if (last) {
			if (m_contentSize.IsSome() && m_frameOut != m_contentSize.Value())
				return Some(String("zstd : taille du contenu incohérente"));
			if (m_checksum) {
				uint8_t check[4];
				if (!ReadExact(check, 4))
					return Some(String("zstd : contrôle de trame tronqué"));
				const uint32_t stored = uint32_t(check[0]) | (uint32_t(check[1]) << 8) |
										(uint32_t(check[2]) << 16) | (uint32_t(check[3]) << 24);
				if (stored != uint32_t(m_hash.Digest()))
					return Some(String("zstd : contrôle XXH64 incorrect (données corrompues)"));
			}
			++m_frames;
			m_mode = Mode::FRAME;
		}
		return NONE;
	}

	// ── Bloc compressé ─────────────────────────────────────────────────────────

	Option<String> DecodeCompressedBlock() {
		const uint8_t* data = m_block.data();
		const size_t size = m_block.size();
		size_t at = 0;
		if (auto error = DecodeLiterals(data, size, at); error.IsSome())
			return error;
		// Nombre de séquences.
		if (at >= size)
			return Some(String("zstd : section des séquences absente"));
		uint32_t count = data[at++];
		if (count >= 128) {
			if (count < 255) {
				if (at >= size)
					return Some(String("zstd : section des séquences tronquée"));
				count = ((count - 128) << 8) + data[at++];
			} else {
				if (at + 2 > size)
					return Some(String("zstd : section des séquences tronquée"));
				count = uint32_t(data[at]) + (uint32_t(data[at + 1]) << 8) + 0x7F00;
				at += 2;
			}
		}
		m_ready.clear();
		m_ready.reserve(m_literals.size() + 1024);
		if (count == 0) {
			if (at != size)
				return Some(String("zstd : octets en trop après les littéraux"));
			for (uint8_t byte : m_literals) {
				m_window.Put(byte);
				m_ready.push_back(byte);
			}
			return NONE;
		}
		if (at >= size)
			return Some(String("zstd : section des séquences tronquée"));
		const uint8_t modes = data[at++];
		if (modes & 0x03)
			return Some(String("zstd : bits réservés des modes de séquences positionnés"));
		struct Kind {
			int mode, index, maxLog, maxSymbol, defaultLog;
			const int16_t* defaults;
			size_t defaultCount;
		};
		const Kind kinds[3] = {
			{(modes >> 6) & 3, 0, LL_MAX_LOG, 35, LL_DEFAULT_LOG, LL_DEFAULT, 36},
			{(modes >> 4) & 3, 1, OF_MAX_LOG, 31, OF_DEFAULT_LOG, OF_DEFAULT, 29},
			{(modes >> 2) & 3, 2, ML_MAX_LOG, 52, ML_DEFAULT_LOG, ML_DEFAULT, 53}};
		for (const Kind& kind : kinds) {
			FseTable& table = m_tables[size_t(kind.index)];
			switch (kind.mode) {
			case 0:
				(void)table.Build(std::span<const int16_t>(kind.defaults, kind.defaultCount),
								  kind.defaultLog);
				break;
			case 1:
				if (at >= size)
					return Some(String("zstd : symbole RLE de séquence manquant"));
				if (data[at] > kind.maxSymbol)
					return Some(String("zstd : symbole RLE de séquence invalide"));
				table.BuildRle(data[at++]);
				break;
			case 2: {
				auto used =
					ReadFseDescription(data + at, size - at, kind.maxLog, kind.maxSymbol, table);
				if (used.IsError())
					return Some(used.Error());
				at += used.Value();
				break;
			}
			default:
				if (!m_haveTables[size_t(kind.index)])
					return Some(String("zstd : table de séquences répétée sans table précédente"));
				break;
			}
			m_haveTables[size_t(kind.index)] = true;
		}
		return ExecuteSequences(data + at, size - at, count);
	}

	Option<String> DecodeLiterals(const uint8_t* data, size_t size, size_t& at) {
		if (size == 0)
			return Some(String("zstd : bloc compressé vide"));
		const uint8_t first = data[0];
		const int type = first & 3;
		const int sizeFormat = (first >> 2) & 3;
		size_t regenerated = 0, compressed = 0;
		int streams = 1;
		if (type <= 1) {
			if ((sizeFormat & 1) == 0) {
				regenerated = first >> 3;
				at = 1;
			} else if (sizeFormat == 1) {
				if (size < 2)
					return Some(String("zstd : en-tête de littéraux tronqué"));
				regenerated = (first >> 4) + (size_t(data[1]) << 4);
				at = 2;
			} else {
				if (size < 3)
					return Some(String("zstd : en-tête de littéraux tronqué"));
				regenerated = (first >> 4) + (size_t(data[1]) << 4) + (size_t(data[2]) << 12);
				at = 3;
			}
			if (regenerated > BLOCK_MAX)
				return Some(String("zstd : trop de littéraux"));
			if (type == 0) {
				if (regenerated > size - at)
					return Some(String("zstd : littéraux bruts tronqués"));
				m_literals.assign(data + at, data + at + regenerated);
				at += regenerated;
			} else {
				if (at >= size)
					return Some(String("zstd : littéraux RLE tronqués"));
				m_literals.assign(regenerated, data[at]);
				++at;
			}
			return NONE;
		}
		// Littéraux Huffman (2) ou Huffman avec l'arbre précédent (3).
		const size_t headerSize = sizeFormat <= 1 ? 3 : (sizeFormat == 2 ? 4 : 5);
		if (size < headerSize)
			return Some(String("zstd : en-tête de littéraux tronqué"));
		uint64_t value = 0;
		for (size_t i = 0; i < headerSize; ++i)
			value |= uint64_t(data[i]) << (8 * i);
		streams = sizeFormat == 0 ? 1 : 4;
		const int fieldBits = headerSize == 3 ? 10 : (headerSize == 4 ? 14 : 18);
		regenerated = size_t((value >> 4) & ((uint64_t(1) << fieldBits) - 1));
		compressed = size_t((value >> (4 + fieldBits)) & ((uint64_t(1) << fieldBits) - 1));
		at = headerSize;
		if (regenerated > BLOCK_MAX || compressed > size - at)
			return Some(String("zstd : tailles de littéraux invalides"));
		const uint8_t* payload = data + at;
		size_t payloadSize = compressed;
		at += compressed;
		if (type == 2) {
			auto used = ReadHuffmanTable(payload, payloadSize);
			if (used.IsError())
				return Some(used.Error());
			payload += used.Value();
			payloadSize -= used.Value();
			m_haveHuffman = true;
		} else if (!m_haveHuffman) {
			return Some(String("zstd : arbre de Huffman répété sans arbre précédent"));
		}
		m_literals.resize(regenerated);
		if (streams == 1)
			return DecodeHuffmanStream(payload, payloadSize, m_literals.data(), regenerated);
		if (payloadSize < 6)
			return Some(String("zstd : table de sauts des littéraux tronquée"));
		const size_t sizes[3] = {size_t(payload[0] | (payload[1] << 8)),
								 size_t(payload[2] | (payload[3] << 8)),
								 size_t(payload[4] | (payload[5] << 8))};
		size_t offset = 6;
		const size_t segment = (regenerated + 3) / 4;
		if (3 * segment > regenerated)
			return Some(String("zstd : trop peu de littéraux pour 4 flux"));
		size_t produced = 0;
		for (int s = 0; s < 4; ++s) {
			const size_t streamSize = s < 3 ? sizes[s] : payloadSize - offset;
			if (offset > payloadSize || streamSize > payloadSize - offset)
				return Some(String("zstd : flux de littéraux tronqué"));
			const size_t count = s < 3 ? segment : regenerated - 3 * segment;
			if (auto error = DecodeHuffmanStream(payload + offset, streamSize,
												 m_literals.data() + produced, count);
				error.IsSome())
				return error;
			produced += count;
			offset += streamSize;
		}
		return NONE;
	}

	Result<size_t, String> ReadHuffmanTable(const uint8_t* data, size_t size) {
		if (size == 0)
			return Err(String("zstd : arbre de Huffman absent"));
		const uint8_t header = data[0];
		std::vector<uint8_t> weights;
		size_t used = 1;
		if (header >= 128) {
			const size_t count = header - 127;
			const size_t bytes = (count + 1) / 2;
			if (bytes > size - 1)
				return Err(String("zstd : poids de Huffman tronqués"));
			for (size_t i = 0; i < count; ++i)
				weights.push_back(i % 2 == 0 ? uint8_t(data[1 + i / 2] >> 4)
											 : uint8_t(data[1 + i / 2] & 0xF));
			used += bytes;
		} else {
			if (header > size - 1)
				return Err(String("zstd : poids de Huffman tronqués"));
			const uint8_t* body = data + 1;
			FseTable table;
			auto description = ReadFseDescription(body, header, 6, 255, table);
			if (description.IsError())
				return Err(description.Error());
			BackwardBits bits;
			if (!bits.Init(body + description.Value(), header - description.Value()))
				return Err(String("zstd : poids de Huffman invalides"));
			uint32_t state1 = bits.Read(table.accuracyLog), state2 = bits.Read(table.accuracyLog);
			auto step = [&](uint32_t& state) {
				const FseTable::Entry& entry = table.entries[state];
				weights.push_back(uint8_t(entry.symbol));
				state = entry.baseline + bits.Read(entry.bits);
			};
			for (;;) {
				if (weights.size() > 254)
					return Err(String("zstd : trop de poids de Huffman"));
				step(state1);
				if (bits.Remaining() < 0) {
					weights.push_back(uint8_t(table.entries[state2].symbol));
					break;
				}
				step(state2);
				if (bits.Remaining() < 0) {
					weights.push_back(uint8_t(table.entries[state1].symbol));
					break;
				}
			}
			used += header;
		}
		if (!m_huffman.BuildFromWeights(weights))
			return Err(String("zstd : arbre de Huffman invalide"));
		return Ok(used);
	}

	Option<String> DecodeHuffmanStream(const uint8_t* data, size_t size, uint8_t* out,
									   size_t count) {
		BackwardBits bits;
		if (!bits.Init(data, size))
			return Some(String("zstd : flux de littéraux invalide"));
		const int maxBits = m_huffman.maxBits;
		for (size_t i = 0; i < count; ++i) {
			const HuffmanTable::Entry& entry = m_huffman.entries[bits.Peek(maxBits)];
			out[i] = entry.symbol;
			bits.Skip(entry.bits);
		}
		if (bits.Remaining() != 0)
			return Some(String("zstd : flux de littéraux mal terminé"));
		return NONE;
	}

	Option<String> ExecuteSequences(const uint8_t* data, size_t size, uint32_t count) {
		BackwardBits bits;
		if (!bits.Init(data, size))
			return Some(String("zstd : flux de séquences invalide"));
		FseTable& ll = m_tables[0];
		FseTable& of = m_tables[1];
		FseTable& ml = m_tables[2];
		uint32_t llState = bits.Read(ll.accuracyLog);
		uint32_t ofState = bits.Read(of.accuracyLog);
		uint32_t mlState = bits.Read(ml.accuracyLog);
		size_t literal = 0;
		for (uint32_t n = 0; n < count; ++n) {
			const FseTable::Entry& llEntry = ll.entries[llState];
			const FseTable::Entry& ofEntry = of.entries[ofState];
			const FseTable::Entry& mlEntry = ml.entries[mlState];
			const uint32_t ofCode = ofEntry.symbol, llCode = llEntry.symbol,
						   mlCode = mlEntry.symbol;
			if (ofCode > 31 || llCode > 35 || mlCode > 52)
				return Some(String("zstd : code de séquence invalide"));
			const uint64_t offsetValue = (uint64_t(1) << ofCode) + bits.Read(int(ofCode));
			const uint32_t matchLength = ML_BASE[mlCode] + bits.Read(ML_BITS[mlCode]);
			const uint32_t literalLength = LL_BASE[llCode] + bits.Read(LL_BITS[llCode]);
			uint64_t offset;
			if (offsetValue > 3) {
				offset = offsetValue - 3;
				m_repeats[2] = m_repeats[1];
				m_repeats[1] = m_repeats[0];
				m_repeats[0] = offset;
			} else {
				const uint64_t index = offsetValue - 1 + (literalLength == 0 ? 1 : 0);
				if (index == 0) {
					offset = m_repeats[0];
				} else {
					offset = index == 3 ? m_repeats[0] - 1 : m_repeats[size_t(index)];
					if (offset == 0)
						return Some(String("zstd : offset répété nul"));
					if (index != 1)
						m_repeats[2] = m_repeats[1];
					m_repeats[1] = m_repeats[0];
					m_repeats[0] = offset;
				}
			}
			if (n + 1 < count) {
				llState = llEntry.baseline + bits.Read(llEntry.bits);
				mlState = mlEntry.baseline + bits.Read(mlEntry.bits);
				ofState = ofEntry.baseline + bits.Read(ofEntry.bits);
			}
			if (bits.Remaining() < 0)
				return Some(String("zstd : flux de séquences tronqué"));
			// Littéraux puis correspondance.
			if (literalLength > m_literals.size() - literal)
				return Some(String("zstd : plus de littéraux que disponibles"));
			for (uint32_t i = 0; i < literalLength; ++i) {
				const uint8_t byte = m_literals[literal++];
				m_window.Put(byte);
				m_ready.push_back(byte);
			}
			if (offset > std::min<uint64_t>(m_window.Total(), m_windowSize) ||
				!m_window.Has(offset))
				return Some(String("zstd : offset hors de la fenêtre"));
			if (m_ready.size() + matchLength > BLOCK_MAX)
				return Some(String("zstd : bloc décompressé trop grand"));
			for (uint32_t i = 0; i < matchLength; ++i) {
				const uint8_t byte = m_window.Get(offset);
				m_window.Put(byte);
				m_ready.push_back(byte);
			}
		}
		if (bits.Remaining() != 0)
			return Some(String("zstd : flux de séquences mal terminé"));
		for (; literal < m_literals.size(); ++literal) {
			m_window.Put(m_literals[literal]);
			m_ready.push_back(m_literals[literal]);
		}
		if (m_ready.size() > BLOCK_MAX)
			return Some(String("zstd : bloc décompressé trop grand"));
		return NONE;
	}

	InputBuffer* m_input;
	uint64_t m_sizeLimit;
	Mode m_mode = Mode::FRAME;
	int m_frames = 0;
	uint64_t m_total = 0;
	// Trame courante.
	bool m_checksum = false;
	Option<uint64_t> m_contentSize = NONE;
	uint64_t m_windowSize = 0;
	size_t m_blockMax = BLOCK_MAX;
	uint64_t m_frameOut = 0;
	Xxh64 m_hash;
	SlidingWindow m_window;
	std::array<uint64_t, 3> m_repeats{1, 4, 8};
	HuffmanTable m_huffman;
	bool m_haveHuffman = false;
	std::array<FseTable, 3> m_tables;
	std::array<bool, 3> m_haveTables{};
	// Bloc courant.
	Bytes m_block, m_literals, m_ready;
	size_t m_readyAt = 0;
};

class DecoderStreamImpl : public DecoderImpl {
public:
	DecoderStreamImpl(ArchiveStream input, Option<uint64_t> size, uint64_t sizeLimit,
					  StreamStatePtr state)
		: DecoderImpl(std::move(state), size), m_input(std::move(input)),
		  m_buffer(m_input.io, m_input.state), m_decoder(m_buffer, sizeLimit) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override {
		auto produced = m_decoder.Decode(out, max);
		if (produced.IsError()) {
			if (m_buffer.Failed())
				return Err(m_buffer.Failure("zstd"));
			const bool unsupported = produced.Error().Contains("non géré");
			return Err(MakeError(unsupported ? ErrorKind::UNSUPPORTED : ErrorKind::CORRUPT,
								 produced.Error()));
		}
		return Ok(produced.Value());
	}
	bool Restart() override {
		if (!m_buffer.Rewind())
			return false;
		m_decoder.Reset();
		return true;
	}

private:
	ArchiveStream m_input;
	InputBuffer m_buffer;
	Decoder m_decoder;
};

// ── Compression ─────────────────────────────────────────────────────────────

/// Écrivain de bits vers l'avant (LSB d'abord) ; `Close` ajoute le bit
/// marqueur que le décodeur cherche en partant de la fin.
class ForwardWriter {
public:
	void Bits(uint64_t value, int count) {
		if (count == 0)
			return;
		m_buffer |= (value & ((uint64_t(1) << count) - 1)) << m_count;
		m_count += count;
		while (m_count >= 8) {
			m_bytes.push_back(uint8_t(m_buffer));
			m_buffer >>= 8;
			m_count -= 8;
		}
	}
	void Close() {
		Bits(1, 1);
		if (m_count > 0) {
			m_bytes.push_back(uint8_t(m_buffer));
			m_buffer = 0;
			m_count = 0;
		}
	}
	/// Vide les bits en attente au prochain octet (description FSE).
	void AlignToByte() {
		if (m_count > 0) {
			m_bytes.push_back(uint8_t(m_buffer));
			m_buffer = 0;
			m_count = 0;
		}
	}
	[[nodiscard]] Bytes& Data() noexcept { return m_bytes; }

private:
	uint64_t m_buffer = 0;
	int m_count = 0;
	Bytes m_bytes;
};

/// Probabilités normalisées à 2^log, chaque symbole présent ≥ 1.
[[nodiscard]] inline std::vector<int16_t> Normalize(std::span<const uint32_t> counts, int log) {
	uint64_t total = 0;
	for (uint32_t c : counts)
		total += c;
	const int32_t scale = int32_t(1) << log;
	std::vector<int16_t> normalized(counts.size(), 0);
	int32_t sum = 0;
	size_t largest = 0;
	for (size_t s = 0; s < counts.size(); ++s) {
		if (counts[s] == 0)
			continue;
		const int32_t value = std::max<int32_t>(
			1, int32_t((uint64_t(counts[s]) * uint64_t(scale) + total / 2) / total));
		normalized[s] = int16_t(value);
		sum += value;
		if (counts[s] > counts[largest])
			largest = s;
	}
	if (sum < scale) {
		normalized[largest] = int16_t(normalized[largest] + (scale - sum));
	} else {
		while (sum > scale) {
			size_t pick = 0;
			for (size_t s = 0; s < normalized.size(); ++s)
				if (normalized[s] > normalized[pick])
					pick = s;
			--normalized[pick];
			--sum;
		}
	}
	return normalized;
}

/// Description d'une table FSE (inverse de `ReadFseDescription`).
inline void WriteFseDescription(ForwardWriter& writer, std::span<const int16_t> normalized,
								int log) {
	writer.Bits(uint64_t(log - 5), 4);
	int32_t remaining = (1 << log) + 1;
	int32_t threshold = 1 << log;
	int bitCount = log + 1;
	size_t symbol = 0;
	bool previousZero = false;
	while (symbol < normalized.size() && remaining > 1) {
		if (previousZero) {
			size_t start = symbol;
			while (symbol < normalized.size() && normalized[symbol] == 0)
				++symbol;
			if (symbol == normalized.size())
				break;
			while (symbol >= start + 24) {
				start += 24;
				writer.Bits(0xFFFF, 16);
			}
			while (symbol >= start + 3) {
				start += 3;
				writer.Bits(3, 2);
			}
			writer.Bits(symbol - start, 2);
		}
		int32_t count = normalized[symbol++];
		const int32_t max = (2 * threshold - 1) - remaining;
		remaining -= count < 0 ? -count : count;
		++count;
		if (count >= threshold)
			count += max;
		writer.Bits(uint64_t(count), bitCount - (count < max ? 1 : 0));
		previousZero = count == 1;
		while (remaining < threshold) {
			--bitCount;
			threshold >>= 1;
		}
	}
	writer.AlignToByte();
}

/// Table d'encodage FSE (tANS), construite comme la table de décodage.
class FseEncoder {
public:
	void Build(std::span<const int16_t> normalized, int log) {
		m_log = log;
		const uint32_t size = uint32_t(1) << log;
		std::vector<uint16_t> spread(size);
		// Même répartition que le décodeur : symboles « -1 » en fin de table.
		int32_t high = int32_t(size) - 1;
		for (size_t s = 0; s < normalized.size(); ++s)
			if (normalized[s] == -1)
				spread[size_t(high--)] = uint16_t(s);
		const uint32_t step = (size >> 1) + (size >> 3) + 3;
		const uint32_t mask = size - 1;
		uint32_t position = 0;
		for (size_t s = 0; s < normalized.size(); ++s)
			for (int16_t i = 0; i < normalized[s]; ++i) {
				spread[position] = uint16_t(s);
				do {
					position = (position + step) & mask;
				} while (int32_t(position) > high);
			}
		std::vector<uint32_t> cumulative(normalized.size() + 1, 0);
		for (size_t s = 0; s < normalized.size(); ++s)
			cumulative[s + 1] =
				cumulative[s] +
				(normalized[s] == -1 ? 1u : uint32_t(std::max<int16_t>(normalized[s], 0)));
		m_states.assign(size, 0);
		std::vector<uint32_t> next(cumulative.begin(), cumulative.end() - 1);
		for (uint32_t u = 0; u < size; ++u)
			m_states[next[spread[u]]++] = uint16_t(size + u);
		m_symbols.assign(normalized.size(), Transform{});
		for (size_t s = 0; s < normalized.size(); ++s) {
			const int32_t n = normalized[s];
			if (n == 0)
				continue;
			if (n == 1 || n == -1) {
				m_symbols[s].deltaBits = (uint32_t(log) << 16) - size;
				m_symbols[s].deltaState = int32_t(cumulative[s]) - 1;
			} else {
				const int maxBitsOut = log - HighBit(uint32_t(n - 1));
				const uint32_t minStatePlus = uint32_t(n) << maxBitsOut;
				m_symbols[s].deltaBits = (uint32_t(maxBitsOut) << 16) - minStatePlus;
				m_symbols[s].deltaState = int32_t(cumulative[s]) - n;
			}
		}
	}
	/// État initial à partir du dernier symbole encodé (aucun bit écrit).
	[[nodiscard]] uint32_t Init(size_t symbol) const {
		const Transform& t = m_symbols[symbol];
		const uint32_t bits = (t.deltaBits + (1u << 15)) >> 16;
		const uint32_t value = (bits << 16) - t.deltaBits;
		return m_states[size_t(int32_t(value >> bits) + t.deltaState)];
	}
	void Encode(ForwardWriter& writer, uint32_t& state, size_t symbol) const {
		const Transform& t = m_symbols[symbol];
		const uint32_t bits = (state + t.deltaBits) >> 16;
		writer.Bits(state, int(bits));
		state = m_states[size_t(int32_t(state >> bits) + t.deltaState)];
	}
	void Flush(ForwardWriter& writer, uint32_t state) const { writer.Bits(state, m_log); }

private:
	struct Transform {
		uint32_t deltaBits = 0;
		int32_t deltaState = 0;
	};
	int m_log = 0;
	std::vector<uint16_t> m_states;
	std::vector<Transform> m_symbols;
};

[[nodiscard]] inline uint32_t LiteralLengthCode(uint32_t value) noexcept {
	uint32_t code = 35;
	while (LL_BASE[code] > value)
		--code;
	return code;
}
[[nodiscard]] inline uint32_t MatchLengthCode(uint32_t length) noexcept {
	uint32_t code = 52;
	while (ML_BASE[code] > length)
		--code;
	return code;
}

struct Sequence {
	uint32_t literalLength;
	uint32_t matchLength;
	uint32_t offset;
};

/// Écrit la section des littéraux : brute, RLE ou Huffman (1 ou 4 flux).
inline void WriteLiterals(const Bytes& literals, Bytes& out) {
	const size_t size = literals.size();
	auto writeRaw = [&](int type) {
		const size_t payload = type == 1 ? 1 : size;
		if (size <= 31) {
			out.push_back(uint8_t((size << 3) | uint32_t(type)));
		} else if (size <= 4095) {
			out.push_back(uint8_t(((size & 0xF) << 4) | (1 << 2) | uint32_t(type)));
			out.push_back(uint8_t(size >> 4));
		} else {
			out.push_back(uint8_t(((size & 0xF) << 4) | (3 << 2) | uint32_t(type)));
			out.push_back(uint8_t(size >> 4));
			out.push_back(uint8_t(size >> 12));
		}
		out.insert(out.end(), literals.begin(), literals.begin() + std::ptrdiff_t(payload));
	};
	std::array<uint32_t, 256> counts{};
	for (uint8_t byte : literals)
		++counts[byte];
	int distinct = 0, maxSymbol = 0;
	for (int s = 0; s < 256; ++s)
		if (counts[size_t(s)] > 0) {
			++distinct;
			maxSymbol = s;
		}
	if (size > 0 && distinct == 1) {
		writeRaw(1);
		return;
	}
	if (size < 64) {
		writeRaw(0);
		return;
	}
	// Longueurs de code (≤ 11 bits) → poids.
	std::array<uint8_t, 256> lengths{};
	deflate::BuildLengths(std::span<const uint32_t>(counts.data(), size_t(maxSymbol) + 1),
						  HUFFMAN_MAX_BITS,
						  std::span<uint8_t>(lengths.data(), size_t(maxSymbol) + 1));
	int maxBits = 0;
	for (int s = 0; s <= maxSymbol; ++s)
		maxBits = std::max<int>(maxBits, lengths[size_t(s)]);
	std::array<uint8_t, 256> weights{};
	for (int s = 0; s <= maxSymbol; ++s)
		weights[size_t(s)] = lengths[size_t(s)] ? uint8_t(maxBits + 1 - lengths[size_t(s)]) : 0;
	// Codes : même attribution que la table du décodeur (par poids croissant).
	std::array<uint32_t, HUFFMAN_MAX_BITS + 2> rankStart{};
	for (int s = 0; s <= maxSymbol; ++s)
		if (weights[size_t(s)] > 0)
			rankStart[weights[size_t(s)]] += uint32_t(1) << (weights[size_t(s)] - 1);
	uint32_t next = 0;
	for (int w = 1; w <= maxBits; ++w) {
		const uint32_t current = next;
		next += rankStart[size_t(w)];
		rankStart[size_t(w)] = current;
	}
	std::array<uint16_t, 256> codes{};
	for (int s = 0; s <= maxSymbol; ++s) {
		const uint8_t w = weights[size_t(s)];
		if (w == 0)
			continue;
		codes[size_t(s)] = uint16_t(rankStart[w] >> (w - 1));
		rankStart[w] += uint32_t(1) << (w - 1);
	}
	// Description de l'arbre : poids des symboles 0..maxSymbol-1.
	Bytes tree;
	const size_t weightCount = size_t(maxSymbol);
	if (weightCount <= 128) {
		tree.push_back(uint8_t(127 + weightCount));
		for (size_t i = 0; i < weightCount; i += 2)
			tree.push_back(uint8_t((weights[i] << 4) | (i + 1 < weightCount ? weights[i + 1] : 0)));
	} else {
		std::array<uint32_t, HUFFMAN_MAX_BITS + 1> weightCounts{};
		int maxWeight = 0;
		for (size_t i = 0; i < weightCount; ++i) {
			++weightCounts[weights[i]];
			maxWeight = std::max<int>(maxWeight, weights[i]);
		}
		const int log = 6;
		const std::vector<int16_t> normalized =
			Normalize(std::span<const uint32_t>(weightCounts.data(), size_t(maxWeight) + 1), log);
		FseEncoder fse;
		fse.Build(normalized, log);
		ForwardWriter description;
		WriteFseDescription(description, normalized, log);
		ForwardWriter stream;
		const uint8_t* ip = weights.data() + weightCount;
		uint32_t state1 = 0, state2 = 0;
		if (weightCount & 1) {
			state1 = fse.Init(*--ip);
			state2 = fse.Init(*--ip);
			fse.Encode(stream, state1, *--ip);
		} else {
			state2 = fse.Init(*--ip);
			state1 = fse.Init(*--ip);
		}
		while (ip > weights.data()) {
			fse.Encode(stream, state2, *--ip);
			fse.Encode(stream, state1, *--ip);
		}
		fse.Flush(stream, state2);
		fse.Flush(stream, state1);
		stream.Close();
		const size_t compressedSize = description.Data().size() + stream.Data().size();
		if (compressedSize >= 128) {
			writeRaw(0);
			return;
		}
		tree.push_back(uint8_t(compressedSize));
		tree.insert(tree.end(), description.Data().begin(), description.Data().end());
		tree.insert(tree.end(), stream.Data().begin(), stream.Data().end());
	}
	// Flux : 1 si peu de littéraux, 4 sinon (symboles écrits à rebours).
	auto encodeStream = [&](size_t from, size_t count) {
		ForwardWriter writer;
		for (size_t i = from + count; i-- > from;)
			writer.Bits(codes[literals[i]], maxBits + 1 - weights[literals[i]]);
		writer.Close();
		return std::move(writer.Data());
	};
	Bytes body = tree;
	const bool single = size < 256;
	if (single) {
		Bytes stream = encodeStream(0, size);
		body.insert(body.end(), stream.begin(), stream.end());
	} else {
		const size_t segment = (size + 3) / 4;
		Bytes streams[4];
		for (int s = 0; s < 4; ++s)
			streams[s] = encodeStream(size_t(s) * segment, s < 3 ? segment : size - 3 * segment);
		for (int s = 0; s < 3; ++s) {
			if (streams[s].size() > 0xFFFF) {
				writeRaw(0);
				return;
			}
			body.push_back(uint8_t(streams[s].size()));
			body.push_back(uint8_t(streams[s].size() >> 8));
		}
		for (const Bytes& stream : streams)
			body.insert(body.end(), stream.begin(), stream.end());
	}
	const size_t compressed = body.size();
	if (compressed + 5 >= size) {
		writeRaw(0);
		return;
	}
	const int format = single ? 0
							  : (size <= 1023 && compressed <= 1023
									 ? 1
									 : (size <= 16383 && compressed <= 16383 ? 2 : 3));
	const int fieldBits = format <= 1 ? 10 : (format == 2 ? 14 : 18);
	const int headerSize = format <= 1 ? 3 : (format == 2 ? 4 : 5);
	const uint64_t header = 2u | (uint64_t(format) << 2) | (uint64_t(size) << 4) |
							(uint64_t(compressed) << (4 + fieldBits));
	for (int i = 0; i < headerSize; ++i)
		out.push_back(uint8_t(header >> (8 * i)));
	out.insert(out.end(), body.begin(), body.end());
}

/// Écrit la section des séquences.
inline void WriteSequences(const std::vector<Sequence>& sequences, Bytes& out) {
	const size_t count = sequences.size();
	if (count < 128) {
		out.push_back(uint8_t(count));
	} else if (count < 0x7F00) {
		out.push_back(uint8_t((count >> 8) + 128));
		out.push_back(uint8_t(count));
	} else {
		out.push_back(255);
		out.push_back(uint8_t(count - 0x7F00));
		out.push_back(uint8_t((count - 0x7F00) >> 8));
	}
	if (count == 0)
		return;
	std::vector<uint8_t> llCodes(count), mlCodes(count), ofCodes(count);
	std::array<uint32_t, 36> llCounts{};
	std::array<uint32_t, 53> mlCounts{};
	std::array<uint32_t, 32> ofCounts{};
	for (size_t i = 0; i < count; ++i) {
		llCodes[i] = uint8_t(LiteralLengthCode(sequences[i].literalLength));
		mlCodes[i] = uint8_t(MatchLengthCode(sequences[i].matchLength));
		ofCodes[i] = uint8_t(HighBit(sequences[i].offset + 3));
		++llCounts[llCodes[i]];
		++mlCounts[mlCodes[i]];
		++ofCounts[ofCodes[i]];
	}
	// Mode de chaque table : RLE (un seul code), prédéfinie (peu de séquences
	// et codes couverts), sinon table décrite (probabilités normalisées).
	struct Choice {
		int mode = 0;
		FseEncoder encoder;
		Bytes header; ///< octet RLE ou description de la table
	};
	auto choose = [&](std::span<const uint32_t> counts, const int16_t* defaults,
					  size_t defaultCount, int defaultLog, int maxLog) {
		Choice choice;
		int distinct = 0, maxSymbol = 0;
		for (size_t s = 0; s < counts.size(); ++s)
			if (counts[s] > 0) {
				++distinct;
				maxSymbol = int(s);
			}
		if (distinct == 1 && count > 1) {
			choice.mode = 1;
			choice.header.push_back(uint8_t(maxSymbol));
			std::vector<int16_t> single(size_t(maxSymbol) + 1, 0);
			single[size_t(maxSymbol)] = 1;
			choice.encoder.Build(single, 0);
			return choice;
		}
		if (count < 32 && size_t(maxSymbol) < defaultCount) {
			choice.mode = 0;
			choice.encoder.Build(std::span<const int16_t>(defaults, defaultCount), defaultLog);
			return choice;
		}
		int log = std::clamp(HighBit(uint32_t(count)) - 1, 5, maxLog);
		while ((1 << log) < distinct)
			++log;
		const std::vector<int16_t> normalized =
			Normalize(std::span<const uint32_t>(counts.data(), size_t(maxSymbol) + 1), log);
		choice.mode = 2;
		choice.encoder.Build(normalized, log);
		ForwardWriter description;
		WriteFseDescription(description, normalized, log);
		choice.header = std::move(description.Data());
		return choice;
	};
	Choice ll = choose(llCounts, LL_DEFAULT, 36, LL_DEFAULT_LOG, LL_MAX_LOG);
	Choice of = choose(ofCounts, OF_DEFAULT, 29, OF_DEFAULT_LOG, OF_MAX_LOG);
	Choice ml = choose(mlCounts, ML_DEFAULT, 53, ML_DEFAULT_LOG, ML_MAX_LOG);
	out.push_back(uint8_t((ll.mode << 6) | (of.mode << 4) | (ml.mode << 2)));
	for (const Choice* choice : {&ll, &of, &ml})
		out.insert(out.end(), choice->header.begin(), choice->header.end());

	// Flux : séquences de la dernière à la première (le décodeur lit à rebours).
	ForwardWriter stream;
	const size_t last = count - 1;
	uint32_t mlState = ml.encoder.Init(mlCodes[last]);
	uint32_t ofState = of.encoder.Init(ofCodes[last]);
	uint32_t llState = ll.encoder.Init(llCodes[last]);
	auto extra = [&](size_t i) {
		const Sequence& seq = sequences[i];
		stream.Bits(seq.literalLength - LL_BASE[llCodes[i]], LL_BITS[llCodes[i]]);
		stream.Bits(seq.matchLength - ML_BASE[mlCodes[i]], ML_BITS[mlCodes[i]]);
		const uint64_t offsetValue = uint64_t(seq.offset) + 3;
		stream.Bits(offsetValue - (uint64_t(1) << ofCodes[i]), ofCodes[i]);
	};
	extra(last);
	for (size_t i = last; i-- > 0;) {
		of.encoder.Encode(stream, ofState, ofCodes[i]);
		ml.encoder.Encode(stream, mlState, mlCodes[i]);
		ll.encoder.Encode(stream, llState, llCodes[i]);
		extra(i);
	}
	ml.encoder.Flush(stream, mlState);
	of.encoder.Flush(stream, ofState);
	ll.encoder.Flush(stream, llState);
	stream.Close();
	out.insert(out.end(), stream.Data().begin(), stream.Data().end());
}

struct LevelParameters {
	int windowLog, chain, lazy;
};
[[nodiscard]] inline LevelParameters ParametersFor(int level) noexcept {
	if (level <= 1)
		return {19, 4, 0};
	if (level <= 3)
		return {20, 16, 0};
	if (level <= 6)
		return {21, 48, 1};
	if (level <= 9)
		return {22, 128, 1};
	if (level <= 15)
		return {23, 256, 1};
	return {23, 1024, 1};
}

/// Compresseur zstd en flux : fenêtre glissante, chaînes de hachage sur 4
/// octets, un bloc de 128 Kio à la fois.
class EncoderStreamImpl : public EncoderImpl {
public:
	EncoderStreamImpl(ArchiveStream sink, int level, Option<uint64_t> pledgedSize,
					  StreamStatePtr state)
		: EncoderImpl(std::move(sink), std::move(state)), m_params(ParametersFor(level)),
		  m_pledged(pledgedSize) {
		if (pledgedSize.IsSome())
			while (m_params.windowLog > 10 &&
				   (uint64_t(1) << (m_params.windowLog - 1)) >= pledgedSize.Value())
				--m_params.windowLog;
		m_window = size_t(1) << m_params.windowLog;
		m_head.assign(size_t(1) << HASH_LOG, 0);
		m_chain.assign(m_window, 0);
	}

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override {
		if (auto header = WriteHeader(); header.IsError())
			return header;
		m_hash.Update(std::span<const uint8_t>(data, size));
		m_buffer.insert(m_buffer.end(), data, data + size);
		// Garder au moins un octet : le dernier bloc porte le drapeau « fin ».
		while (m_buffer.size() - m_cursor > BLOCK_MAX) {
			auto block = CompressBlock(m_cursor + BLOCK_MAX, false);
			if (block.IsError())
				return block;
		}
		return Ok(true);
	}
	Result<bool, ArchiveError> Finish() override {
		if (auto header = WriteHeader(); header.IsError())
			return header;
		auto block = CompressBlock(m_buffer.size(), true);
		if (block.IsError())
			return block;
		const uint32_t digest = uint32_t(m_hash.Digest());
		const uint8_t check[4] = {uint8_t(digest), uint8_t(digest >> 8), uint8_t(digest >> 16),
								  uint8_t(digest >> 24)};
		return Emit(check, 4);
	}

private:
	static constexpr int HASH_LOG = 17;
	static constexpr size_t MIN_MATCH = 4;

	Result<bool, ArchiveError> WriteHeader() {
		if (m_headerWritten)
			return Ok(true);
		m_headerWritten = true;
		Bytes header = {0x28, 0xB5, 0x2F, 0xFD};
		if (m_pledged.IsSome()) {
			header.push_back(uint8_t(0xC0 | 0x04)); // taille sur 8 octets, contrôle
			header.push_back(uint8_t((m_params.windowLog - 10) << 3));
			for (int i = 0; i < 8; ++i)
				header.push_back(uint8_t(m_pledged.Value() >> (8 * i)));
		} else {
			header.push_back(0x04);
			header.push_back(uint8_t((m_params.windowLog - 10) << 3));
		}
		return Emit(header.data(), header.size());
	}

	[[nodiscard]] static uint32_t Hash(const uint8_t* p) noexcept {
		const uint32_t v = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
						   (uint32_t(p[3]) << 24);
		return (v * 2654435761u) >> (32 - HASH_LOG);
	}
	void Insert(size_t position) {
		if (position + MIN_MATCH > m_buffer.size())
			return;
		const uint32_t h = Hash(m_buffer.data() + position);
		m_chain[position & (m_window - 1)] = m_head[h];
		m_head[h] = uint32_t(position + 1);
	}
	/// Meilleure correspondance en `position` (ne dépasse pas `limit`).
	void FindMatch(size_t position, size_t limit, size_t& bestLength, size_t& bestDistance) const {
		bestLength = 0;
		if (position + MIN_MATCH > limit)
			return;
		uint32_t candidate = m_head[Hash(m_buffer.data() + position)];
		const size_t maxLength = std::min<size_t>(limit - position, 131074);
		for (int chain = 0; chain < m_params.chain && candidate != 0; ++chain) {
			const size_t from = candidate - 1;
			if (from >= position || position - from >= m_window)
				break;
			const uint8_t* a = m_buffer.data() + from;
			const uint8_t* b = m_buffer.data() + position;
			if (a[bestLength] == b[bestLength]) {
				size_t length = 0;
				while (length < maxLength && a[length] == b[length])
					++length;
				if (length > bestLength) {
					bestLength = length;
					bestDistance = position - from;
					if (length == maxLength)
						break;
				}
			}
			candidate = m_chain[from & (m_window - 1)];
		}
		if (bestLength < MIN_MATCH)
			bestLength = 0;
	}

	Result<bool, ArchiveError> CompressBlock(size_t end, bool last) {
		const size_t start = m_cursor;
		std::vector<Sequence> sequences;
		Bytes literals;
		size_t anchor = start, position = start;
		while (position + MIN_MATCH <= end) {
			size_t length = 0, distance = 0;
			FindMatch(position, end, length, distance);
			if (length != 0 && m_params.lazy && position + 1 + MIN_MATCH <= end) {
				Insert(position);
				size_t nextLength = 0, nextDistance = 0;
				FindMatch(position + 1, end, nextLength, nextDistance);
				if (nextLength > length + 1) {
					++position;
					length = nextLength;
					distance = nextDistance;
				} else {
					// `position` déjà insérée : la correspondance commence ici.
					literals.insert(literals.end(), m_buffer.begin() + std::ptrdiff_t(anchor),
									m_buffer.begin() + std::ptrdiff_t(position));
					sequences.push_back(Sequence{uint32_t(position - anchor), uint32_t(length),
												 uint32_t(distance)});
					for (size_t i = 1; i < length; ++i)
						Insert(position + i);
					position += length;
					anchor = position;
					continue;
				}
			}
			if (length == 0) {
				Insert(position);
				++position;
				continue;
			}
			literals.insert(literals.end(), m_buffer.begin() + std::ptrdiff_t(anchor),
							m_buffer.begin() + std::ptrdiff_t(position));
			sequences.push_back(
				Sequence{uint32_t(position - anchor), uint32_t(length), uint32_t(distance)});
			for (size_t i = 0; i < length; ++i)
				Insert(position + i);
			position += length;
			anchor = position;
		}
		literals.insert(literals.end(), m_buffer.begin() + std::ptrdiff_t(anchor),
						m_buffer.begin() + std::ptrdiff_t(end));

		Bytes body;
		WriteLiterals(literals, body);
		WriteSequences(sequences, body);
		const size_t rawSize = end - start;
		Bytes block;
		uint32_t header;
		if (body.size() >= rawSize || body.size() > BLOCK_MAX) {
			header = uint32_t(last) | (0u << 1) | uint32_t(rawSize << 3);
			block.assign(m_buffer.begin() + std::ptrdiff_t(start),
						 m_buffer.begin() + std::ptrdiff_t(end));
		} else {
			header = uint32_t(last) | (2u << 1) | uint32_t(body.size() << 3);
			block = std::move(body);
		}
		const uint8_t head[3] = {uint8_t(header), uint8_t(header >> 8), uint8_t(header >> 16)};
		if (auto emitted = Emit(head, 3); emitted.IsError())
			return emitted;
		if (auto emitted = Emit(block.data(), block.size()); emitted.IsError())
			return emitted;
		m_cursor = end;
		Slide();
		return Ok(true);
	}

	/// Garde la fenêtre utile ; décale positions, tête et chaînes.
	void Slide() {
		if (m_cursor < 2 * m_window + BLOCK_MAX)
			return;
		const size_t delta = ((m_cursor - m_window) / m_window) * m_window;
		m_buffer.erase(m_buffer.begin(), m_buffer.begin() + std::ptrdiff_t(delta));
		m_cursor -= delta;
		auto rebase = [delta](uint32_t& entry) {
			entry = entry > delta ? uint32_t(entry - delta) : 0;
		};
		for (uint32_t& entry : m_head)
			rebase(entry);
		for (uint32_t& entry : m_chain)
			rebase(entry);
	}

	LevelParameters m_params;
	Option<uint64_t> m_pledged;
	size_t m_window = 0;
	bool m_headerWritten = false;
	Bytes m_buffer;
	size_t m_cursor = 0;
	std::vector<uint32_t> m_head, m_chain;
	Xxh64 m_hash;
};

} // namespace detail::zstd

[[nodiscard]] inline bool IsZstd(std::span<const uint8_t> bytes) noexcept {
	return bytes.size() >= 4 && bytes[0] == 0x28 && bytes[1] == 0xB5 && bytes[2] == 0x2F &&
		   bytes[3] == 0xFD;
}

/// Flux décompressé d'un flux zstd (trames concaténées) lu dans `input`.
[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenZstdStream(ArchiveStream input, Option<uint64_t> size = NONE, uint64_t sizeLimit = UINT64_MAX) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(
		std::make_unique<detail::zstd::DecoderStreamImpl>(std::move(input), size, sizeLimit, state),
		state);
}

[[nodiscard]] inline Result<Bytes, ArchiveError> ZstdDecompress(std::span<const uint8_t> input,
																uint64_t sizeLimit = UINT64_MAX) {
	auto stream = OpenZstdStream(OpenMemoryStream(Bytes(input.begin(), input.end())).Unwrap(), NONE,
								 sizeLimit);
	if (stream.IsError())
		return Err(stream.Error());
	return ReadStreamToEnd(stream.Value(), sizeLimit);
}

/// Flux en écriture : ce qu'on y écrit sort compressé en zstd dans `sink`.
/// `pledgedSize` : taille totale annoncée (inscrite dans l'en-tête de trame).
/// Fermer avec `FinishStream`.
[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenZstdEncoder(ArchiveStream sink, int level = 3, Option<uint64_t> pledgedSize = NONE) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::zstd::EncoderStreamImpl>(std::move(sink), level,
																		pledgedSize, state),
					  state);
}

[[nodiscard]] inline Result<Bytes, ArchiveError> ZstdCompress(std::span<const uint8_t> input,
															  int level = 3) {
	auto target = std::make_shared<Bytes>();
	auto encoder =
		OpenZstdEncoder(OpenMemorySink(target).Unwrap(), level, Some(uint64_t(input.size())));
	if (encoder.IsError())
		return Err(encoder.Error());
	auto written = StreamWrite(encoder.Value(), input);
	if (written.IsError())
		return Err(written.Error());
	auto finished = FinishStream(encoder.Value());
	if (finished.IsError())
		return Err(finished.Error());
	return Ok(std::move(*target));
}

} // namespace data::archive
