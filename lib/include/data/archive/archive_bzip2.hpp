#pragma once
/**
 * data::archive — bzip2 : compression et décompression.
 *
 * Format (Julian Seward, bzip2 1.0) : « BZh » + taille de bloc (1 à 9 × 100
 * 000), puis des blocs — RLE des séries de 4 à 255 octets identiques,
 * transformée de Burrows-Wheeler (BWT), déplacement en tête (MTF) avec séries
 * de zéros codées RUNA/RUNB, et 2 à 6 tables de Huffman choisies par groupes de
 * 50 symboles — chacun protégé par un CRC-32 (polynôme 0x04C11DB7, bits de
 * poids fort d'abord), le flux par un CRC combiné. Flux concaténés acceptés
 * (pbzip2, `cat a.bz2 b.bz2`).
 *
 *  - `OpenBzip2Stream` : décompression en flux, bloc par bloc ; la sortie
 *    d'un bloc (BWT inverse puis RLE) est produite à la demande — un bloc très
 *    répétitif peut s'étendre à plusieurs dizaines de Mio ;
 *  - `OpenBzip2Encoder` : compression en flux (blocs de `level` × 100 000
 *    octets) : tri des rotations par doublement de préfixe (tris par
 *    comptage, O(n log n)), 4 passes d'affinage des tables comme bzip2 ;
 *    sortie décodable par bzip2(1), 7-Zip, libarchive (vérifié) ;
 *  - `Bzip2Decompress`, `Bzip2Compress` : tampons complets.
 *
 * Les blocs « randomisés » (bzip2 < 0.9.5, 1999) sont refusés (`UNSUPPORTED`).
 */
#include "../../core/core.hpp"
#include "archive_deflate.hpp"
#include "archive_io.hpp"
#include "archive_stream.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace data::archive {

namespace detail::bzip2 {

inline constexpr uint64_t BLOCK_MAGIC = 0x314159265359ull;
inline constexpr uint64_t END_MAGIC = 0x177245385090ull;
inline constexpr int MAX_GROUPS = 6;
inline constexpr int GROUP_SIZE = 50;
inline constexpr int MAX_ALPHA = 258;
inline constexpr int MAX_CODE_LENGTH = 20;
inline constexpr int ENCODER_MAX_LENGTH = 17;
inline constexpr int MAX_SELECTORS = 18002;

/// CRC-32 « MSB d'abord » de bzip2 (polynôme 0x04C11DB7, non réfléchi).
[[nodiscard]] uint32_t CrcUpdate(uint32_t crc, uint8_t byte) noexcept;

/// Lecteur de bits MSB d'abord.
class BitReader {
public:
	explicit BitReader(InputBuffer& input) noexcept : m_input(&input) {}
	[[nodiscard]] bool Bits(int count, uint32_t& out);
	[[nodiscard]] bool Bit(uint32_t& out) { return Bits(1, out); }
	void AlignToByte() noexcept { m_count -= m_count % 8; }
	[[nodiscard]] size_t ReservedBytes() const noexcept { return size_t(m_count / 8); }
	void Reset() noexcept;

private:
	InputBuffer* m_input;
	uint64_t m_buffer = 0;
	int m_count = 0;
};

/// Table de décodage de Huffman à la manière de bzip2 (limit/base/perm).
struct DecodeTable {
	std::array<int32_t, MAX_CODE_LENGTH + 2> limit{}, base{};
	std::array<uint16_t, MAX_ALPHA> perm{};
	int minLength = 0, maxLength = 0;

	void Build(const uint8_t* lengths, int alphaSize);
};

/// Décodeur d'un flux bzip2 (flux concaténés compris).
class Decoder {
public:
	explicit Decoder(InputBuffer& input) : m_bits(input) {}

	void Reset();

	/// 0 = fin de tous les flux.
	[[nodiscard]] Result<size_t, String> Decode(uint8_t* out, size_t max);

private:
	enum class Mode : uint8_t { STREAM_HEADER, BLOCK, OUTPUT, DONE };

	Option<String> Step();

	Option<String> ReadBlock();

	/// Sortie du bloc : parcours de la BWT inverse + décodage RLE (4 octets
	/// identiques suivis d'un compteur de répétitions).
	size_t Output(uint8_t* out, size_t max);

	BitReader m_bits;
	Mode m_mode = Mode::STREAM_HEADER;
	int m_streams = 0;
	int m_blockSize100k = 9;
	uint32_t m_combinedCrc = 0, m_storedBlockCrc = 0, m_blockCrc = 0;
	std::vector<uint32_t> m_tt;
	uint32_t m_position = 0, m_left = 0;
	int m_runLength = 0, m_last = -1;
	uint32_t m_repeat = 0;
};

class DecoderStreamImpl : public DecoderImpl {
public:
	DecoderStreamImpl(ArchiveStream input, Option<uint64_t> size, StreamStatePtr state)
		: DecoderImpl(std::move(state), size), m_input(std::move(input)),
		  m_buffer(m_input.io, m_input.state), m_decoder(m_buffer) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_input;
	InputBuffer m_buffer;
	Decoder m_decoder;
};

// ── Compression ─────────────────────────────────────────────────────────────

/// Écrivain de bits MSB d'abord.
class BitWriter {
public:
	void Bits(int count, uint32_t value);
	void Bits48(uint64_t value);
	void Flush();
	[[nodiscard]] Bytes Take();

private:
	uint64_t m_buffer = 0;
	int m_count = 0;
	Bytes m_bytes;
};

/// Tri des rotations cycliques de `block` (doublement de préfixe, tris par
/// comptage stables). Rend l'ordre des rotations.
[[nodiscard]] std::vector<uint32_t> SortRotations(const Bytes& block);

/// Compresse un bloc (déjà passé par le RLE initial) et l'ajoute au flux.
void EncodeBlock(const Bytes& block, uint32_t blockCrc, BitWriter& writer);

/// Compresseur en flux : RLE initial au fil de l'eau, un bloc compressé dès
/// qu'il est plein.
class EncoderStreamImpl : public EncoderImpl {
public:
	EncoderStreamImpl(ArchiveStream sink, int level, StreamStatePtr state)
		: EncoderImpl(std::move(sink), std::move(state)), m_level(std::clamp(level, 1, 9)),
		  m_blockMax(uint32_t(m_level) * 100000u - 19) {}

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override;

private:
	/// Ajoute la série courante au bloc (4 octets + compteur au-delà de 3).
	Result<bool, ArchiveError> FlushRun();

	Result<bool, ArchiveError> WriteBlock();

	int m_level;
	uint32_t m_blockMax;
	bool m_headerWritten = false;
	Bytes m_block;
	std::vector<uint32_t> m_runOriginal; ///< longueur d'origine de chaque série du bloc
	uint8_t m_runByte = 0;
	uint32_t m_runLength = 0;
	uint32_t m_combinedCrc = 0;
	BitWriter m_writer;
};

} // namespace detail::bzip2

[[nodiscard]] bool IsBzip2(std::span<const uint8_t> bytes) noexcept;

/// Flux décompressé d'un flux bzip2 lu depuis la position courante de `input`.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenBzip2Stream(ArchiveStream input, Option<uint64_t> size = NONE);

/// Flux en écriture : ce qu'on y écrit sort compressé en bzip2 dans `sink`.
/// `level` : taille des blocs (1 à 9 × 100 000 octets). Fermer avec
/// `FinishStream`.
[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenBzip2Encoder(ArchiveStream sink,
																		  int level = 9);

[[nodiscard]] Result<Bytes, ArchiveError> Bzip2Decompress(std::span<const uint8_t> input,
																 uint64_t sizeLimit = UINT64_MAX);

[[nodiscard]] Result<Bytes, ArchiveError> Bzip2Compress(std::span<const uint8_t> input,
															   int level = 9);

} // namespace data::archive
