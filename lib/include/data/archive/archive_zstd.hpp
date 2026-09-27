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

[[nodiscard]] int HighBit(uint32_t value) noexcept;

/// Lecture de bits À REBOURS : le dernier octet porte un bit marqueur, les
/// bits sont consommés du plus haut vers le plus bas.
class BackwardBits {
public:
	[[nodiscard]] bool Init(const uint8_t* data, size_t size) noexcept;
	/// Lit `count` bits (≤ 32) ; au-delà du début, des zéros entrent par la
	/// droite.
	[[nodiscard]] uint32_t Read(int count) noexcept;
	[[nodiscard]] uint32_t Peek(int count) const noexcept;
	void Skip(int count) noexcept { m_position -= count; }
	/// Bits restants (négatif si on a lu au-delà du début).
	[[nodiscard]] int64_t Remaining() const noexcept { return m_position; }

private:
	[[nodiscard]] uint32_t Extract(uint64_t start, int count) const noexcept;

	const uint8_t* m_data = nullptr;
	size_t m_size = 0;
	int64_t m_position = 0;
};

/// Lecture de bits vers l'avant (LSB d'abord), zéros au-delà de la fin.
class ForwardBits {
public:
	ForwardBits(const uint8_t* data, size_t size) noexcept : m_data(data), m_size(size) {}
	[[nodiscard]] uint32_t Peek(int count) const noexcept;
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
	[[nodiscard]] bool Build(std::span<const int16_t> normalized, int log);

	/// Table « RLE » : un seul symbole, aucun bit.
	void BuildRle(uint16_t symbol);
};

/// Lit une description de table FSE (probabilités normalisées) ; rend le
/// nombre d'octets consommés.
[[nodiscard]] Result<size_t, String>
ReadFseDescription(const uint8_t* data, size_t size, int maxLog, int maxSymbol, FseTable& table);

/// Table de décodage de Huffman (index = `maxBits` bits lus d'avance).
struct HuffmanTable {
	struct Entry {
		uint8_t symbol = 0;
		uint8_t bits = 0;
	};
	std::vector<Entry> entries;
	int maxBits = 0;

	[[nodiscard]] bool BuildFromWeights(const std::vector<uint8_t>& givenWeights);
};

/// Décodeur de trames zstd.
class Decoder {
public:
	explicit Decoder(InputBuffer& input, uint64_t sizeLimit)
		: m_input(&input), m_sizeLimit(sizeLimit) {}

	void Reset();

	/// 0 = fin de toutes les trames.
	[[nodiscard]] Result<size_t, String> Decode(uint8_t* out, size_t max);

private:
	enum class Mode : uint8_t { FRAME, BLOCK, DONE };

	bool ReadExact(uint8_t* out, size_t size) { return m_input->ReadRaw(out, size) == size; }

	Option<String> ReadFrameHeader();

	Option<String> ReadBlock();

	// ── Bloc compressé ─────────────────────────────────────────────────────────

	Option<String> DecodeCompressedBlock();

	Option<String> DecodeLiterals(const uint8_t* data, size_t size, size_t& at);

	Result<size_t, String> ReadHuffmanTable(const uint8_t* data, size_t size);

	Option<String> DecodeHuffmanStream(const uint8_t* data, size_t size, uint8_t* out,
									   size_t count);

	Option<String> ExecuteSequences(const uint8_t* data, size_t size, uint32_t count);

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
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

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
	void Bits(uint64_t value, int count);
	void Close();
	/// Vide les bits en attente au prochain octet (description FSE).
	void AlignToByte();
	[[nodiscard]] Bytes& Data() noexcept { return m_bytes; }

private:
	uint64_t m_buffer = 0;
	int m_count = 0;
	Bytes m_bytes;
};

/// Probabilités normalisées à 2^log, chaque symbole présent ≥ 1.
[[nodiscard]] std::vector<int16_t> Normalize(std::span<const uint32_t> counts, int log);

/// Description d'une table FSE (inverse de `ReadFseDescription`).
void WriteFseDescription(ForwardWriter& writer, std::span<const int16_t> normalized,
								int log);

/// Table d'encodage FSE (tANS), construite comme la table de décodage.
class FseEncoder {
public:
	void Build(std::span<const int16_t> normalized, int log);
	/// État initial à partir du dernier symbole encodé (aucun bit écrit).
	[[nodiscard]] uint32_t Init(size_t symbol) const;
	void Encode(ForwardWriter& writer, uint32_t& state, size_t symbol) const;
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

[[nodiscard]] uint32_t LiteralLengthCode(uint32_t value) noexcept;
[[nodiscard]] uint32_t MatchLengthCode(uint32_t length) noexcept;

struct Sequence {
	uint32_t literalLength;
	uint32_t matchLength;
	uint32_t offset;
};

/// Écrit la section des littéraux : brute, RLE ou Huffman (1 ou 4 flux).
void WriteLiterals(const Bytes& literals, Bytes& out);

/// Écrit la section des séquences.
void WriteSequences(const std::vector<Sequence>& sequences, Bytes& out);

struct LevelParameters {
	int windowLog, chain, lazy;
};
[[nodiscard]] LevelParameters ParametersFor(int level) noexcept;

/// Compresseur zstd en flux : fenêtre glissante, chaînes de hachage sur 4
/// octets, un bloc de 128 Kio à la fois.
class EncoderStreamImpl : public EncoderImpl {
public:
	EncoderStreamImpl(ArchiveStream sink, int level, Option<uint64_t> pledgedSize,
					  StreamStatePtr state);

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override;

private:
	static constexpr int HASH_LOG = 17;
	static constexpr size_t MIN_MATCH = 4;

	Result<bool, ArchiveError> WriteHeader();

	[[nodiscard]] static uint32_t Hash(const uint8_t* p) noexcept;
	void Insert(size_t position);
	/// Meilleure correspondance en `position` (ne dépasse pas `limit`).
	void FindMatch(size_t position, size_t limit, size_t& bestLength, size_t& bestDistance) const;

	Result<bool, ArchiveError> CompressBlock(size_t end, bool last);

	/// Garde la fenêtre utile ; décale positions, tête et chaînes.
	void Slide();

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

[[nodiscard]] bool IsZstd(std::span<const uint8_t> bytes) noexcept;

/// Flux décompressé d'un flux zstd (trames concaténées) lu dans `input`.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenZstdStream(ArchiveStream input, Option<uint64_t> size = NONE, uint64_t sizeLimit = UINT64_MAX);

[[nodiscard]] Result<Bytes, ArchiveError> ZstdDecompress(std::span<const uint8_t> input,
																uint64_t sizeLimit = UINT64_MAX);

/// Flux en écriture : ce qu'on y écrit sort compressé en zstd dans `sink`.
/// `pledgedSize` : taille totale annoncée (inscrite dans l'en-tête de trame).
/// Fermer avec `FinishStream`.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenZstdEncoder(ArchiveStream sink, int level = 3, Option<uint64_t> pledgedSize = NONE);

[[nodiscard]] Result<Bytes, ArchiveError> ZstdCompress(std::span<const uint8_t> input,
															  int level = 3);

} // namespace data::archive
