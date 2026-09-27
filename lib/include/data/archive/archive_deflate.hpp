#pragma once
/**
 * data::archive — DEFLATE (RFC 1951) : décompression et compression.
 *
 * `Inflate` : décompresseur complet (blocs stockés, Huffman fixe et
 * dynamique ; table rapide de 10 bits + parcours canonique à la « puff »),
 * toutes les entrées bornées, plafond de taille contre les bombes de
 * décompression. Validé octet pour octet contre zlib. Variante Deflate64
 * (« enhanced deflate » de PKWARE, méthode zip 9) : fenêtre de 64 Kio,
 * symbole de longueur 285 = 3 + 16 bits supplémentaires, codes de distance
 * 30 et 31 (32 769 et 49 153 + 14 bits).
 *
 * `Deflate` : compresseur. LZ77 sur une fenêtre de 32 Kio (chaînes de
 * hachage sur 3 octets, évaluation paresseuse à partir du niveau 4), puis pour
 * chaque bloc le codage le moins coûteux parmi : stocké, Huffman fixe,
 * Huffman dynamique (longueurs limitées à 15 bits par la redistribution de
 * miniz, code des longueurs RLE 16/17/18). Niveau 0 = stocké uniquement ;
 * 1–9 = effort croissant. Le flux produit est décodable par zlib, 7-Zip,
 * Info-ZIP… (vérifié par les tests).
 */
#include "../../core/core.hpp"
#include "archive_io.hpp"
#include "archive_stream.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace data::archive {

struct InflateResult {
	std::vector<uint8_t> bytes;
	size_t consumed = 0; ///< octets d'entrée utilisés (le flux peut être suivi d'autre chose)
};

namespace detail::deflate {

/// Lecteur de bits LSB d'abord sur un `InputBuffer`, réserve de 64 bits.
class BitReader {
public:
	explicit BitReader(InputBuffer& input) noexcept : m_input(&input) {}

	/// Garantit au moins `count` bits en réserve si l'entrée le permet.
	void Refill(int count);

	[[nodiscard]] bool Take(int count, uint32_t& out);

	/// Abandonne les bits jusqu'à la frontière d'octet (bloc stocké).
	void AlignToByte() noexcept;

	/// Octet brut (après AlignToByte) : réserve d'abord, puis l'entrée.
	[[nodiscard]] bool TakeByte(uint8_t& out);

	[[nodiscard]] uint64_t Buffer() const noexcept { return m_buffer; }
	[[nodiscard]] int BitCount() const noexcept { return m_bitCount; }
	void Consume(int count) noexcept;
	/// Octets d'entrée réellement consommés (les octets entiers encore en
	/// réserve ne comptent pas).
	[[nodiscard]] uint64_t Consumed() const noexcept;
	[[nodiscard]] size_t ReservedBytes() const noexcept { return size_t(m_bitCount / 8); }
	void Reset() noexcept;

private:
	InputBuffer* m_input;
	uint64_t m_buffer = 0;
	int m_bitCount = 0;
};

/// Code de Huffman canonique + table de décodage rapide.
class Huffman {
public:
	static constexpr int MAX_BITS = 15;
	static constexpr int FAST_BITS = 10;

	/// Construit le code depuis les longueurs. Rend le nombre de codes
	/// « manquants » (0 = code complet, >0 = incomplet, <0 = sur-souscrit).
	int Build(const uint8_t* lengths, int symbolCount);

	[[nodiscard]] int CodeCount(int length) const noexcept { return m_count[size_t(length)]; }

	/// Décode un symbole ; -1 si le flux est tronqué ou le code invalide.
	[[nodiscard]] int Decode(BitReader& reader) const noexcept;

private:
	std::array<uint16_t, MAX_BITS + 1> m_count{};
	std::vector<uint16_t> m_symbols;
	std::array<uint16_t, 1u << FAST_BITS> m_fast{};
};

inline constexpr uint16_t LENGTH_BASE[29] = {3,	 4,	 5,	 6,	  7,   8,	9,	 10,  11, 13,
											 15, 17, 19, 23,  27,  31,	35,	 43,  51, 59,
											 67, 83, 99, 115, 131, 163, 195, 227, 258};
inline constexpr uint8_t LENGTH_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
											 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
inline constexpr uint16_t DISTANCE_BASE[32] = {
	1,	  2,	3,	  4,	5,	  7,	 9,		13,	   17,	  25,	33,
	49,	  65,	97,	  129,	193,  257,	 385,	513,   769,	  1025, 1537,
	2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 32769, 49153}; // 30 et 31 : Deflate64
																	  // uniquement
inline constexpr uint8_t DISTANCE_EXTRA[32] = {0,  0,  0,  0,  1,  1,  2,  2,  3,  3, 4,
											   4,  5,  5,  6,  6,  7,  7,  8,  8,  9, 9,
											   10, 10, 11, 11, 12, 12, 13, 13, 14, 14};

/// Décodeur DEFLATE / Deflate64 incrémental : `Decode` produit au plus
/// `max` octets et reprend exactement là où il s'était arrêté (au milieu d'un
/// bloc stocké ou d'une copie de correspondance). Fenêtre circulaire de 64 Kio.
class Inflater {
public:
	static constexpr size_t WINDOW = size_t(1) << 16;

	Inflater(InputBuffer& input, bool deflate64)
		: m_bits(input), m_deflate64(deflate64), m_window(WINDOW) {}

	void Reset();

	[[nodiscard]] bool Finished() const noexcept { return m_mode == Mode::DONE; }
	/// Octets d'entrée utilisés (exact une fois le flux terminé).
	[[nodiscard]] uint64_t Consumed() const noexcept { return m_bits.Consumed(); }
	/// Octets lus d'avance mais non utilisés (à rendre au flux source).
	[[nodiscard]] size_t ReservedBytes() const noexcept { return m_bits.ReservedBytes(); }
	[[nodiscard]] uint64_t TotalOut() const noexcept { return m_total; }
	/// Après la fin du flux : octet suivant (réserve de bits, puis entrée),
	/// aligné sur l'octet — pour lire un pied (gzip).
	[[nodiscard]] bool TakeByteAfterEnd(uint8_t& out);

	/// 0 = fin du flux DEFLATE (dernier bloc terminé).
	[[nodiscard]] Result<size_t, String> Decode(uint8_t* out, size_t max);

private:
	enum class Mode : uint8_t { HEADER, STORED, CODES, DONE };

	void Emit(uint8_t* out, size_t& produced, uint8_t byte);

	size_t CopyMatch(uint8_t* out, size_t max);

	Option<String> DecodeSymbol(uint8_t* out, size_t& produced);

	Option<String> ReadBlockHeader();

	BitReader m_bits;
	bool m_deflate64;
	std::vector<uint8_t> m_window;
	Mode m_mode = Mode::HEADER;
	bool m_last = false;
	uint64_t m_total = 0;
	size_t m_copyLength = 0;
	uint64_t m_copyDistance = 0;
	uint32_t m_storedRemaining = 0;
	Huffman m_fixedLiterals, m_fixedDistances, m_dynamicLiterals, m_dynamicDistances;
	const Huffman* m_literals = nullptr;
	const Huffman* m_distances = nullptr;
	bool m_fixedBuilt = false;
};

/// Flux décompressé d'un flux DEFLATE brut (entrée possédée).
class InflateStreamImpl : public DecoderImpl {
public:
	InflateStreamImpl(ArchiveStream input, bool deflate64, Option<uint64_t> size,
					  StreamStatePtr state)
		: DecoderImpl(std::move(state), size), m_input(std::move(input)),
		  m_buffer(m_input.io, m_input.state), m_inflater(m_buffer, deflate64) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_input;
	InputBuffer m_buffer;
	Inflater m_inflater;
};

} // namespace detail::deflate

/// Flux décompressé d'un flux DEFLATE (ou Deflate64) brut lu dans `input`
/// depuis sa position courante. `size` : taille décompressée si connue.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenInflateStream(ArchiveStream input, bool deflate64 = false, Option<uint64_t> size = NONE);

/// Décompresse un flux DEFLATE brut en mémoire. `expectedSize` (si connue, 0
/// sinon) sert à réserver la mémoire ET de plafond : un flux qui produirait
/// davantage est rejeté (bombe de décompression, archive incohérente).
[[nodiscard]] Result<InflateResult, String>
Inflate(std::span<const uint8_t> input, size_t expectedSize = 0, bool deflate64 = false);

// ============================================================================
// Compression
// ============================================================================

namespace detail::deflate {

/// Écrivain de bits LSB d'abord (ordre DEFLATE).
class BitWriter {
public:
	void Bits(uint32_t value, int count);
	void AlignToByte();
	void Bytes(std::span<const uint8_t> bytes);
	[[nodiscard]] archive::Bytes Take();
	/// Octets complets produits jusqu'ici (les bits en attente restent).
	[[nodiscard]] archive::Bytes TakeComplete();

private:
	archive::Bytes m_out;
	uint64_t m_buffer = 0;
	int m_count = 0;
};

/// Longueurs de code de Huffman optimales, limitées à `maxBits`.
/// Construction à deux files sur les feuilles triées, puis redistribution de
/// miniz (`tdefl_huffman_enforce_max_code_size`) : chaque itération retire un
/// code de longueur maximale ou en scinde un plus court, jusqu'à ce que la
/// somme de Kraft soit exactement 1 — la convergence est garantie.
void BuildLengths(std::span<const uint32_t> frequencies, int maxBits,
						 std::span<uint8_t> lengths);

/// Codes canoniques, bits INVERSÉS (DEFLATE écrit les codes MSB d'abord dans
/// un flux LSB d'abord).
void BuildCodes(std::span<const uint8_t> lengths, std::span<uint16_t> codes);

[[nodiscard]] int LengthSymbol(int length) noexcept;
[[nodiscard]] int DistanceSymbol(int distance) noexcept;

struct Token {
	uint16_t length; ///< 0 = littéral
	uint16_t distanceOrLiteral;
};

struct LevelParameters {
	int maxChain;
	int niceLength;
	bool lazy;
};

[[nodiscard]] LevelParameters ParametersFor(int level) noexcept;

class Compressor {
public:
	static constexpr int WINDOW = 32768;
	static constexpr int MIN_MATCH = 3;
	static constexpr int MAX_MATCH = 258;
	static constexpr int HASH_BITS = 15;
	static constexpr size_t BLOCK_TOKENS = 1u << 15;
	/// Données gardées en avance : une correspondance n'est jamais coupée par
	/// la fin d'un morceau (sauf à la vraie fin du flux).
	static constexpr size_t LOOKAHEAD = MAX_MATCH + 1;

	explicit Compressor(int level);

	/// Ajoute des données à compresser.
	void Feed(std::span<const uint8_t> data);

	/// Compresse ce qui peut l'être ; `final` : plus rien ne suivra (dernier
	/// bloc émis). Les octets produits se récupèrent avec `TakeOutput`.
	void Compress(bool final);

	/// Octets complets produits (en fin de flux, tout est aligné et rendu).
	[[nodiscard]] archive::Bytes TakeOutput();

	/// Compression d'un tampon complet.
	[[nodiscard]] archive::Bytes Run(std::span<const uint8_t> input);

private:
	/// Octet à la position ABSOLUE `position` (le tampon a glissé de m_base).
	[[nodiscard]] uint8_t At(size_t position) const noexcept { return m_input[position - m_base]; }
	[[nodiscard]] size_t End() const noexcept { return m_base + m_input.size(); }

	/// Oublie ce qui précède la fenêtre (et le bloc en cours).
	void Slide();

	[[nodiscard]] uint32_t Hash(size_t position) const noexcept;

	void Insert(size_t position);

	void FindMatch(size_t position, int& bestLength, int& bestDistance) const;

	void WriteStoredBlocks(size_t start, size_t end, bool final);

	void FlushBlock(size_t start, size_t end, bool final);

	void WriteTokens(std::span<const uint8_t> literalLengths,
					 std::span<const uint8_t> distanceLengths);

	archive::Bytes m_input;
	size_t m_base = 0;		 ///< position absolue de m_input[0]
	size_t m_position = 0;	 ///< prochaine position à coder
	size_t m_blockStart = 0; ///< début du bloc en cours
	bool m_finished = false;
	LevelParameters m_params;
	BitWriter m_writer;
	std::vector<Token> m_tokens;
	std::vector<int64_t> m_head;
	std::vector<int64_t> m_previous;
	bool m_previousInserted = false;
};

} // namespace detail::deflate

/// Compresse `input` en un flux DEFLATE brut. `level` : 0 (stocké) à 9.
[[nodiscard]] Bytes Deflate(std::span<const uint8_t> input, int level = 6);

namespace detail::deflate {

/// Compresseur DEFLATE en flux.
class DeflateEncoderImpl : public EncoderImpl {
public:
	DeflateEncoderImpl(ArchiveStream sink, int level, StreamStatePtr state)
		: EncoderImpl(std::move(sink), std::move(state)), m_compressor(level) {}

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override { return Drain(true); }

private:
	Result<bool, ArchiveError> Drain(bool final);

	Compressor m_compressor;
	size_t m_calls = 0;
};

} // namespace detail::deflate

/// Flux en écriture : compression DEFLATE brute vers `sink`. Fermer avec
/// `FinishStream`.
[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenDeflateEncoder(ArchiveStream sink,
																			int level = 6);

} // namespace data::archive
