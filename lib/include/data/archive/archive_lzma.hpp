#pragma once
/**
 * data::archive — LZMA, LZMA2 et conteneur .xz.
 *
 *  - LZMA (« LZMA1 ») : codeur de plage binaire adaptatif + LZ77 à grande
 *    fenêtre. Décodeur conforme à la spécification de référence d'Igor Pavlov
 *    (LzmaSpec.cpp) ; encodeur « rapide » : recherche gloutonne (chaînes de
 *    hachage sur 3 octets, 4 distances répétées, correspondances courtes
 *    « short rep »), avec évaluation paresseuse à partir du niveau 5 — sans
 *    l'analyse optimale de 7-Zip, donc un peu moins compact, mais un flux
 *    100 % standard (vérifié contre liblzma et 7-Zip par les tests).
 *    Utilisé par zip (méthode 14) et 7z (coder 03 01 01), et `.lzma`.
 *  - LZMA2 : suite de morceaux LZMA ou non compressés (≤ 2 Mio décompressés,
 *    ≤ 64 Kio compressés chacun) avec réinitialisations explicites d'état, de
 *    propriétés et de dictionnaire. Utilisé par 7z (coder 21) et xz.
 *  - xz : conteneur (en-tête de flux, blocs, index, pied) autour d'un filtre
 *    LZMA2, contrôle CRC-32, CRC-64 ou SHA-256. Les CHAMPS du conteneur sont
 *    lus et écrits par `sdl3::IOStream` (varints, U32 LE…) ; seules les données
 *    compressées sont décodées en mémoire. Filtres BCJ/delta non gérés
 *    (erreur explicite).
 */
#include "../../core/core.hpp"
#include "archive_crc.hpp"
#include "archive_crypto.hpp"
#include "archive_filters.hpp"
#include "archive_io.hpp"
#include "archive_stream.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace data::archive {

/// Propriétés lc/lp/pb + taille de dictionnaire.
struct LzmaProperties {
	uint8_t lc = 3; ///< bits de contexte littéral (octet précédent)
	uint8_t lp = 0; ///< bits de position littérale
	uint8_t pb = 2; ///< bits de position
	uint32_t dictionarySize = 1u << 23;

	[[nodiscard]] uint8_t PropertiesByte() const noexcept;

	/// Les 5 octets d'en-tête LZMA : propriétés puis dictionnaire (U32 LE).
	[[nodiscard]] std::array<uint8_t, 5> Encode() const noexcept;

	[[nodiscard]] static Result<LzmaProperties, String> FromByte(uint8_t byte);

	[[nodiscard]] static Result<LzmaProperties, String> Decode(std::span<const uint8_t> bytes);
};

/// Octet de dictionnaire LZMA2 (xz, 7z) : taille = (2 | (b & 1)) << (b / 2 +
/// 11).
[[nodiscard]] uint32_t Lzma2DictionarySize(uint8_t byte) noexcept;
[[nodiscard]] uint8_t Lzma2DictionaryByte(uint32_t size) noexcept;

namespace detail::lzma {

inline constexpr int NUM_BIT_MODEL_TOTAL_BITS = 11;
inline constexpr uint32_t BIT_MODEL_TOTAL = 1u << NUM_BIT_MODEL_TOTAL_BITS;
inline constexpr int NUM_MOVE_BITS = 5;
inline constexpr uint32_t TOP_VALUE = 1u << 24;
inline constexpr uint16_t PROB_INIT = BIT_MODEL_TOTAL / 2;
inline constexpr int NUM_STATES = 12;
inline constexpr int NUM_POS_BITS_MAX = 4;
inline constexpr int NUM_LEN_TO_POS_STATES = 4;
inline constexpr int NUM_ALIGN_BITS = 4;
inline constexpr int START_POS_MODEL_INDEX = 4;
inline constexpr int END_POS_MODEL_INDEX = 14;
inline constexpr int NUM_FULL_DISTANCES = 1 << (END_POS_MODEL_INDEX >> 1);
inline constexpr int MATCH_MIN_LEN = 2;
inline constexpr int MATCH_MAX_LEN = 273;

[[nodiscard]] constexpr int StateAfterLiteral(int s) noexcept {
	return s < 4 ? 0 : (s < 10 ? s - 3 : s - 6);
}
[[nodiscard]] constexpr int StateAfterMatch(int s) noexcept {
	return s < 7 ? 7 : 10;
}
[[nodiscard]] constexpr int StateAfterRep(int s) noexcept {
	return s < 7 ? 8 : 11;
}
[[nodiscard]] constexpr int StateAfterShortRep(int s) noexcept {
	return s < 7 ? 9 : 11;
}

/// Toutes les probabilités d'un modèle LZMA, partagées par décodeur et
/// encodeur.
struct Model {
	std::vector<uint16_t> literal;
	std::array<uint16_t, NUM_STATES << NUM_POS_BITS_MAX> isMatch{};
	std::array<uint16_t, NUM_STATES> isRep{}, isRepG0{}, isRepG1{}, isRepG2{};
	std::array<uint16_t, NUM_STATES << NUM_POS_BITS_MAX> isRep0Long{};
	std::array<std::array<uint16_t, 1 << 6>, NUM_LEN_TO_POS_STATES> posSlot{};
	std::array<uint16_t, 1 + NUM_FULL_DISTANCES - END_POS_MODEL_INDEX> posSpecial{};
	std::array<uint16_t, 1 << NUM_ALIGN_BITS> align{};
	struct Length {
		uint16_t choice = PROB_INIT, choice2 = PROB_INIT;
		std::array<std::array<uint16_t, 1 << 3>, 1 << NUM_POS_BITS_MAX> low{}, mid{};
		std::array<uint16_t, 1 << 8> high{};
		void Reset() noexcept;
	} length, repLength;

	void Reset(const LzmaProperties& props);
};

// ── Décodage ────────────────────────────────────────────────────────────────

/// Codeur de plage en lecture, alimenté par un `InputBuffer`.
class RangeDecoder {
public:
	/// Lit les 5 octets d'amorce (le premier doit être nul).
	[[nodiscard]] bool Init(InputBuffer& input);

	[[nodiscard]] int Bit(uint16_t& prob);

	[[nodiscard]] uint32_t DirectBits(int count);

	[[nodiscard]] uint32_t BitTree(std::span<uint16_t> probs, int bits);
	[[nodiscard]] uint32_t ReverseBitTree(uint16_t* probs, int bits);

	[[nodiscard]] bool IsFinishedOk() const noexcept { return m_code == 0; }
	/// Lecture au-delà de l'entrée : flux tronqué.
	[[nodiscard]] bool Overrun() const noexcept { return m_overrun; }
	[[nodiscard]] bool Corrupted() const noexcept { return m_corrupted; }
	/// Octets consommés depuis `Init`.
	[[nodiscard]] uint64_t Consumed() const noexcept { return m_input->Consumed() - m_start; }

private:
	void Normalize();

	InputBuffer* m_input = nullptr;
	uint64_t m_start = 0;
	uint32_t m_range = 0, m_code = 0;
	bool m_corrupted = false, m_overrun = false;
};

using Window = SlidingWindow;

/// Décodeur LZMA à état persistant (morceaux LZMA2) et à sortie incrémentale.
class Decoder {
public:
	void SetProperties(const LzmaProperties& props);
	void ResetState();
	void ResetDictionary(uint64_t capacity);
	[[nodiscard]] const LzmaProperties& Properties() const noexcept { return m_props; }
	[[nodiscard]] Window& Dictionary() noexcept { return m_window; }

	/// Octet non compressé (morceau LZMA2 stocké) : entre dans le dictionnaire.
	void PutRaw(uint8_t byte) { m_window.Put(byte); }

	enum class Stop : uint8_t { OUTPUT_FULL, SIZE_REACHED, END_MARKER };

	/// Décode au plus `max` octets vers `out` (`produced` incrémenté).
	/// `remaining` : octets encore attendus si `sizeKnown`.
	[[nodiscard]] Result<Stop, String> Decode(RangeDecoder& rc, uint8_t* out, size_t max,
											  size_t& produced, uint64_t& remaining, bool sizeKnown,
											  bool markerAllowed);

private:
	[[nodiscard]] static uint32_t DecodeLength(RangeDecoder& rc, Model::Length& model,
											   uint32_t posState);

	[[nodiscard]] uint32_t DecodeDistance(RangeDecoder& rc, uint32_t length);

	LzmaProperties m_props;
	Model m_model;
	Window m_window;
	int m_state = 0;
	std::array<uint32_t, 4> m_reps{};
	uint32_t m_pending = 0;
};

/// Flux LZMA brut (propriétés connues, taille connue ou marqueur de fin).
class LzmaStreamImpl : public DecoderImpl {
public:
	LzmaStreamImpl(ArchiveStream input, const LzmaProperties& props, uint64_t unpackSize,
				   bool markerAllowed, uint64_t headerSkip, StreamStatePtr state)
		: DecoderImpl(std::move(state),
					  unpackSize == UINT64_MAX ? Option<uint64_t>(NONE) : Some(unpackSize)),
		  m_input(std::move(input)), m_buffer(m_input.io, m_input.state), m_props(props),
		  m_unpackSize(unpackSize), m_markerAllowed(markerAllowed || unpackSize == UINT64_MAX),
		  m_headerSkip(headerSkip) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveError Failure(const String& message) const;

	ArchiveStream m_input;
	InputBuffer m_buffer;
	LzmaProperties m_props;
	uint64_t m_unpackSize;
	bool m_markerAllowed;
	uint64_t m_headerSkip;
	Decoder m_decoder;
	RangeDecoder m_rc;
	uint64_t m_remaining = 0;
	bool m_started = false, m_done = false;
};

/// Décodeur LZMA2 incrémental sur un `InputBuffer` (utilisé seul ou dans xz).
class Lzma2Decoder {
public:
	void Reset(uint32_t dictionarySize);
	void LimitDictionary(uint64_t size) { m_capacityHint = size; }
	[[nodiscard]] bool Finished() const noexcept { return m_mode == Mode::DONE; }

	/// 0 = fin du flux LZMA2 (octet de contrôle 0x00).
	[[nodiscard]] Result<size_t, String> Decode(InputBuffer& input, uint8_t* out, size_t max);

private:
	enum class Mode : uint8_t { CONTROL, LZMA, UNCOMPRESSED, DONE };

	Option<String> ReadChunkHeader(InputBuffer& input);

	Decoder m_decoder;
	RangeDecoder m_rc;
	LzmaProperties m_props;
	uint32_t m_dictionarySize = 0;
	uint64_t m_capacityHint = UINT64_MAX;
	Mode m_mode = Mode::CONTROL;
	uint64_t m_chunkRemaining = 0, m_chunkPacked = 0;
	bool m_needDictionaryReset = true, m_needProperties = true, m_haveProperties = false;
};

class Lzma2StreamImpl : public DecoderImpl {
public:
	Lzma2StreamImpl(ArchiveStream input, uint32_t dictionarySize, Option<uint64_t> size,
					StreamStatePtr state);

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	void Reset();

	ArchiveStream m_input;
	InputBuffer m_buffer;
	uint32_t m_dictionarySize;
	Option<uint64_t> m_size;
	Lzma2Decoder m_decoder;
};

// ── Encodage ────────────────────────────────────────────────────────────────

class RangeEncoder {
public:
	void Bit(uint16_t& prob, uint32_t bit);
	void DirectBits(uint32_t value, int count);
	void BitTree(std::span<uint16_t> probs, int bits, uint32_t symbol);
	void ReverseBitTree(uint16_t* probs, int bits, uint32_t symbol);
	void Flush();
	/// Majorant de la taille finale si l'on vidait maintenant.
	[[nodiscard]] size_t PendingSize() const noexcept;
	[[nodiscard]] Bytes Take() { return std::move(m_out); }

private:
	void ShiftLow();

	Bytes m_out;
	uint64_t m_low = 0;
	uint32_t m_range = 0xFFFFFFFFu;
	uint8_t m_cache = 0;
	uint64_t m_cacheSize = 1;
};

struct EncoderParameters {
	int maxChain;
	int niceLength;
	bool lazy;
};

[[nodiscard]] EncoderParameters EncoderParametersFor(int level) noexcept;

class Encoder {
public:
	/// `sizeHint` : taille totale si connue (réduit la mémoire des tables).
	Encoder(const LzmaProperties& props, int level, uint64_t sizeHint = UINT64_MAX);

	void ResetState();
	/// Distance maximale utilisée : ce qui précède (position - distance - 1)
	/// peut être oublié.
	[[nodiscard]] uint32_t MaxDistance() const noexcept { return m_maxDistance; }

	/// Ajoute des données (positions absolues : la première donnée reçue est
	/// à la position 0).
	void Feed(std::span<const uint8_t> data);
	/// Fin des données reçues (position absolue).
	[[nodiscard]] size_t End() const noexcept { return m_base + m_input.size(); }
	/// Octet à une position absolue encore dans le tampon.
	[[nodiscard]] uint8_t At(size_t position) const noexcept { return m_input[position - m_base]; }

	/// Oublie les données antérieures à `keep` (au-delà du dictionnaire) et
	/// recale les tables de hachage.
	void Slide(size_t keep);

	/// Encode [position, stop) et rend la position atteinte : l'arrêt anticipé
	/// survient si la sortie estimée dépasse `outputLimit` octets. Les
	/// correspondances peuvent lire jusqu'à la fin des données reçues.
	size_t EncodeRange(RangeEncoder& rc, size_t position, size_t end, size_t outputLimit);

	void EncodeEndMarker(RangeEncoder& rc, size_t position);

private:
	static constexpr int HASH_BITS = 18;

	[[nodiscard]] uint32_t MatchLength(size_t position, uint32_t rep,
									   size_t available) const noexcept;

	[[nodiscard]] uint32_t Hash(size_t position) const noexcept;

	void Insert(size_t position);

	void FindMatch(size_t position, size_t available, uint32_t& bestLength,
				   uint32_t& bestDistance) const;

	void EncodeLiteral(RangeEncoder& rc, size_t position, uint32_t posState);

	static void EncodeLength(RangeEncoder& rc, Model::Length& model, uint32_t length,
							 uint32_t posState);

	void EncodeRep(RangeEncoder& rc, uint32_t repIndex, uint32_t length, uint32_t posState);

	void EncodeMatch(RangeEncoder& rc, uint32_t distance, uint32_t length, uint32_t posState);

	Bytes m_input;
	size_t m_base = 0;		///< position absolue de m_input[0]
	size_t m_tableBase = 0; ///< origine des positions stockées dans les tables
	LzmaProperties m_props;
	EncoderParameters m_params;
	Model m_model;
	int m_state = 0;
	std::array<uint32_t, 4> m_reps{};
	std::vector<uint32_t> m_head;
	std::vector<uint32_t> m_previous;
	uint64_t m_windowMask = 0;
	uint32_t m_maxDistance = 0;
	bool m_skipInsert = false;
};

} // namespace detail::lzma

// ============================================================================
// LZMA brut et .lzma
// ============================================================================

/// Flux décompressé d'un flux LZMA brut lu depuis la position courante de
/// `input`. `unpackSize` : UINT64_MAX = inconnue (marqueur de fin obligatoire).
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenLzmaStream(ArchiveStream input, const LzmaProperties& props, uint64_t unpackSize,
			   bool endMarkerAllowed = true);

namespace detail::lzma {
/// Lit un flux mémoire jusqu'au bout (fonctions « tout en mémoire »).
[[nodiscard]] Result<Bytes, String> DrainStream(Result<ArchiveStream, ArchiveError> stream,
													   uint64_t limit = UINT64_MAX,
													   uint64_t sizeHint = 0);
[[nodiscard]] ArchiveStream MemoryInput(std::span<const uint8_t> input);
} // namespace detail::lzma

[[nodiscard]] Result<Bytes, String> LzmaDecompress(std::span<const uint8_t> input,
														  const LzmaProperties& props,
														  uint64_t unpackSize,
														  bool endMarkerAllowed = true);

/// Compresse en LZMA brut (sans en-tête).
[[nodiscard]] Bytes LzmaCompress(std::span<const uint8_t> input, const LzmaProperties& props,
										int level = 6, bool writeEndMarker = false);

namespace detail::lzma {

/// Compresseur LZMA brut en flux (7z coder 030101, zip méthode 14).
class LzmaEncoderImpl : public EncoderImpl {
public:
	LzmaEncoderImpl(ArchiveStream sink, const LzmaProperties& props, int level, bool endMarker,
					uint64_t sizeHint, StreamStatePtr state)
		: EncoderImpl(std::move(sink), std::move(state)), m_encoder(props, level, sizeHint),
		  m_endMarker(endMarker) {}

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override;

private:
	Result<bool, ArchiveError> Drain();

	Encoder m_encoder;
	RangeEncoder m_rc;
	size_t m_position = 0;
	bool m_endMarker;
};

} // namespace detail::lzma

/// Flux en écriture : LZMA brut (sans en-tête) vers `sink`.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenLzmaEncoder(ArchiveStream sink, const LzmaProperties& props, int level = 6,
				bool endMarker = false, uint64_t sizeHint = UINT64_MAX);

/// Flux décompressé d'un fichier `.lzma` (« LZMA alone » : 5 octets de
/// propriétés, taille U64 LE ou -1, puis le flux LZMA).
[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenLzmaAloneStream(ArchiveStream input);

[[nodiscard]] Result<Bytes, String> LzmaAloneDecompress(std::span<const uint8_t> input);

[[nodiscard]] Result<Bytes, String> LzmaAloneCompress(std::span<const uint8_t> input,
															 int level = 6,
															 uint32_t dictionarySize = 1u << 23);

// ============================================================================
// LZMA2
// ============================================================================

/// Flux décompressé d'un flux LZMA2 brut (7z : coder 21).
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenLzma2Stream(ArchiveStream input, uint32_t dictionarySize, Option<uint64_t> size = NONE);

[[nodiscard]] Result<Bytes, String> Lzma2Decompress(std::span<const uint8_t> input,
														   uint32_t dictionarySize,
														   size_t* consumed = nullptr,
														   uint64_t sizeLimit = UINT64_MAX);

namespace detail::lzma {

/// Morceaux LZMA2 produits au fil de l'eau : morceaux LZMA (≤ 2 Mio
/// décompressés, ≤ 64 Kio compressés) avec dictionnaire conservé d'un morceau
/// à l'autre ; un morceau que LZMA n'arrive pas à réduire est stocké tel quel.
class Lzma2ChunkWriter {
public:
	static constexpr size_t CHUNK_UNPACKED_MAX = (size_t(1) << 21) - MATCH_MAX_LEN;
	static constexpr size_t CHUNK_PACKED_MAX = size_t(1) << 16;

	Lzma2ChunkWriter(uint32_t dictionarySize, int level, uint64_t sizeHint)
		: m_props(MakeProperties(dictionarySize)), m_encoder(m_props, level, sizeHint) {}

	void Feed(std::span<const uint8_t> data) { m_encoder.Feed(data); }

	/// Émet les morceaux complets (tous si `final`) à la suite de `out`.
	void Encode(bool final, Bytes& out);

	/// Derniers morceaux et octet de fin (0x00).
	void Finish(Bytes& out);

	[[nodiscard]] const LzmaProperties& Properties() const noexcept { return m_props; }

private:
	[[nodiscard]] static LzmaProperties MakeProperties(uint32_t dictionarySize);

	LzmaProperties m_props;
	Encoder m_encoder;
	size_t m_position = 0;
	bool m_first = true, m_needProperties = true;
};

/// Compresseur LZMA2 brut en flux (7z coder 21).
class Lzma2EncoderImpl : public EncoderImpl {
public:
	Lzma2EncoderImpl(ArchiveStream sink, uint32_t dictionarySize, int level, uint64_t sizeHint,
					 StreamStatePtr state)
		: EncoderImpl(std::move(sink), std::move(state)),
		  m_chunks(dictionarySize, level, sizeHint) {}

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override;

private:
	Lzma2ChunkWriter m_chunks;
};

} // namespace detail::lzma

/// Compresse en LZMA2 (tampon complet).
[[nodiscard]] Bytes Lzma2Compress(std::span<const uint8_t> input,
										 uint32_t dictionarySize = 1u << 23, int level = 6);

/// Flux en écriture : LZMA2 brut vers `sink`.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenLzma2Encoder(ArchiveStream sink, uint32_t dictionarySize = 1u << 23, int level = 6,
				 uint64_t sizeHint = UINT64_MAX);

// ============================================================================
// .xz
// ============================================================================

enum class XzCheck : uint8_t { NONE = 0x00, CRC32 = 0x01, CRC64 = 0x04, SHA256 = 0x0A };

namespace detail::xz {

inline constexpr uint8_t STREAM_MAGIC[6] = {0xFD, '7', 'z', 'X', 'Z', 0x00};

[[nodiscard]] uint64_t ReadVarint(BinaryReader& reader);

void WriteVarint(BinaryWriter& writer, uint64_t value);

[[nodiscard]] size_t CheckSize(uint8_t check) noexcept;

} // namespace detail::xz

namespace detail::xz {

/// Décodeur .xz en flux : en-têtes de flux et de bloc, LZMA2 et filtres de
/// branchement/delta, contrôle (CRC-32, CRC-64, SHA-256) calculé au fil de
/// la lecture, index et pied vérifiés, flux concaténés et remplissage.
class XzStreamImpl : public DecoderImpl {
public:
	XzStreamImpl(ArchiveStream input, uint64_t sizeLimit, StreamStatePtr state)
		: DecoderImpl(std::move(state)), m_input(std::move(input)),
		  m_buffer(m_input.io, m_input.state), m_sizeLimit(sizeLimit) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	enum class Mode : uint8_t { STREAM_HEADER, BLOCK_HEADER, BLOCK_DATA, INDEX, DONE };
	struct Record {
		uint64_t unpadded, uncompressed;
	};

	bool ReadExact(uint8_t* out, size_t size) { return m_buffer.ReadRaw(out, size) == size; }

	Option<String> ReadStreamHeader();

	Option<String> ReadBlockHeader();

	Option<String> DecodeBlockData();

	void UpdateCheck(std::span<const uint8_t> data);

	Option<String> FinishBlock();

	Option<String> ReadIndexAndFooter();

	ArchiveStream m_input;
	InputBuffer m_buffer;
	uint64_t m_sizeLimit;
	Mode m_mode = Mode::STREAM_HEADER;
	bool m_sawStream = false;
	uint8_t m_flags[2] = {0, 0};
	uint8_t m_check = 0;
	std::vector<Record> m_records;
	Option<uint64_t> m_blockCompressed = NONE, m_blockUncompressed = NONE;
	uint64_t m_blockHeaderSize = 0, m_blockStart = 0, m_blockOut = 0, m_total = 0;
	std::vector<FilterStage> m_stages;
	detail::lzma::Lzma2Decoder m_lzma2;
	uint32_t m_crc32 = 0;
	uint64_t m_crc64 = 0;
	Sha256 m_sha;
	Bytes m_ready;
	size_t m_readyAt = 0;
};

} // namespace detail::xz

/// Flux décompressé d'un fichier .xz (flux concaténés, remplissage,
/// filtres BCJ/delta + LZMA2). `sizeLimit` plafonne la taille produite.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenXzStream(ArchiveStream input, uint64_t sizeLimit = UINT64_MAX);

/// Décompresse un fichier .xz en mémoire.
[[nodiscard]] Result<Bytes, String> XzDecompress(std::span<const uint8_t> input,
														uint64_t sizeLimit = UINT64_MAX);

namespace detail::xz {

/// Compresseur .xz en flux : un flux, un bloc LZMA2 (tailles absentes de
/// l'en-tête de bloc, elles figurent dans l'index écrit à la fin).
class XzEncoderImpl : public EncoderImpl {
public:
	XzEncoderImpl(ArchiveStream sink, int level, uint32_t dictionarySize, XzCheck check,
				  uint64_t sizeHint, StreamStatePtr state)
		: EncoderImpl(std::move(sink), std::move(state)), m_chunks(dictionarySize, level, sizeHint),
		  m_dictionaryByte(Lzma2DictionaryByte(dictionarySize)), m_check(check) {}

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;

	Result<bool, ArchiveError> Finish() override;

private:
	Result<bool, ArchiveError> WriteStreamHeader();

	Result<bool, ArchiveError> WriteBlockHeader();

	void UpdateCheck(std::span<const uint8_t> data);

	lzma::Lzma2ChunkWriter m_chunks;
	uint8_t m_dictionaryByte;
	XzCheck m_check;
	bool m_headerWritten = false, m_blockStarted = false;
	uint64_t m_blockHeaderSize = 0, m_compressed = 0, m_uncompressed = 0;
	uint32_t m_crc32 = 0;
	uint64_t m_crc64 = 0;
	Sha256 m_sha;
};

} // namespace detail::xz

/// Flux en écriture : fichier .xz complet vers `sink`. Fermer avec
/// `FinishStream`.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenXzEncoder(ArchiveStream sink, int level = 6, uint32_t dictionarySize = 1u << 23,
			  XzCheck check = XzCheck::CRC64, uint64_t sizeHint = UINT64_MAX);

/// Compresse en .xz (tampon complet) : un flux, un bloc LZMA2.
[[nodiscard]] Result<Bytes, String> XzCompress(std::span<const uint8_t> input, int level = 6,
													  uint32_t dictionarySize = 1u << 23,
													  XzCheck check = XzCheck::CRC64);

} // namespace data::archive
