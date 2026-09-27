#pragma once
/**
 * data::archive — PPMd (Dmitry Shkarin), variantes H et I.
 *
 * PPMd prédit chaque octet d'après les contextes des octets précédents
 * (ordre 2 à 64) et code la prédiction avec un codeur d'intervalle. Le modèle
 * vit dans un bloc mémoire de taille FIXÉE par les propriétés du flux ;
 * quand ce bloc est plein, le modèle repart de zéro. Pour décoder ce qu'un
 * autre programme a codé, il faut donc reproduire EXACTEMENT sa gestion de la
 * mémoire : ce fichier suit octet pour octet l'allocateur de 7-Zip (unités de
 * 12 octets, listes libres par taille, recollage des blocs libres), avec des
 * références 32 bits dans le bloc et des accès petit-boutistes explicites.
 *
 *  - `Ppmd7Model` : variante H (7z, coder 030401 ; RAR 3). Deux codeurs
 *    d'intervalle : celui de 7z (`Ppmd7zRangeDecoder`/`Encoder`) et celui de
 *    RAR 3 (`Ppmd7RarRangeDecoder`, sans retenue, de Dmitry Subbotin) ;
 *  - `OpenPpmd7Stream`, `OpenPpmd7Encoder` : flux 7z (propriétés : ordre sur
 *    1 octet + taille mémoire U32 LE) ;
 *  - `Ppmd8Model` : variante I rév. 1 (zip, méthode 98 ; voir plus bas).
 */
#include "../../core/core.hpp"
#include "archive_io.hpp"
#include "archive_stream.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace data::archive {

namespace detail::ppmd {

inline constexpr int INT_BITS = 7;
inline constexpr int PERIOD_BITS = 7;
inline constexpr uint32_t BIN_SCALE = 1u << (INT_BITS + PERIOD_BITS);
inline constexpr int NUM_INDEXES = 4 + 4 + 4 + 26;
inline constexpr uint32_t UNIT_SIZE = 12;
inline constexpr uint32_t TOP = 1u << 24;
inline constexpr uint32_t BOT = 1u << 15;

[[nodiscard]] uint32_t GetMean(uint32_t prob) noexcept;
[[nodiscard]] uint16_t UpdateProb0(uint32_t prob) noexcept;
[[nodiscard]] uint16_t UpdateProb1(uint32_t prob) noexcept;

/// Estimateur d'échappement secondaire (SEE).
struct See {
	uint16_t summ = 0;
	uint8_t shift = 0;
	uint8_t count = 0;

	void Update() noexcept;
};

/// Bloc mémoire du modèle : références = décalages depuis le début.
class Memory {
public:
	void Allocate(size_t size) { m_bytes.assign(size, 0); }
	[[nodiscard]] uint8_t U8(uint32_t at) const noexcept { return m_bytes[at]; }
	void SetU8(uint32_t at, uint8_t v) noexcept { m_bytes[at] = v; }
	[[nodiscard]] uint16_t U16(uint32_t at) const noexcept;
	void SetU16(uint32_t at, uint16_t v) noexcept;
	[[nodiscard]] uint32_t U32(uint32_t at) const noexcept;
	void SetU32(uint32_t at, uint32_t v) noexcept;
	void Copy(uint32_t to, uint32_t from, size_t size) noexcept;
	[[nodiscard]] size_t Size() const noexcept { return m_bytes.size(); }

private:
	std::vector<uint8_t> m_bytes;
};

// Champs d'un contexte (12 octets) et d'un état (6 octets).
//   contexte : NumStats u16 @0, SummFreq u16 @2, Stats u32 @4, Suffix u32 @8
//   état     : Symbol u8 @0, Freq u8 @1, Successor u16+u16 @2
// Un contexte à UN état range cet état dans SummFreq/Stats (OneState = ctx+2).

} // namespace detail::ppmd

// ============================================================================
// PPMd variante H (Ppmd7)
// ============================================================================

class Ppmd7Model {
public:
	static constexpr int MAX_ORDER = 64;
	static constexpr uint32_t MAX_FREQ = 124;

	Ppmd7Model();

	/// Alloue le bloc mémoire (`size` octets, comme 7-Zip) et initialise.
	void Init(uint32_t size, unsigned maxOrder);

	/// Décode un symbole ; -1 = marqueur de fin, -2 = données corrompues.
	template <typename Decoder> [[nodiscard]] int DecodeSymbol(Decoder& rc) {
		using namespace detail::ppmd;
		std::array<int8_t, 256> mask;
		if (NumStats(m_minContext) != 1) {
			uint32_t s = Stats(m_minContext);
			uint32_t count = rc.GetThreshold(SummFreq(m_minContext));
			uint32_t hiCount = Freq(s);
			if (count < hiCount) {
				rc.Decode(0, Freq(s));
				m_foundState = s;
				const uint8_t symbol = Symbol(s);
				Update1_0();
				return symbol;
			}
			m_prevSuccess = 0;
			unsigned i = NumStats(m_minContext) - 1;
			do {
				s += 6;
				if ((hiCount += Freq(s)) > count) {
					rc.Decode(hiCount - Freq(s), Freq(s));
					m_foundState = s;
					const uint8_t symbol = Symbol(s);
					Update1();
					return symbol;
				}
			} while (--i);
			if (count >= SummFreq(m_minContext))
				return -2;
			m_hiBitsFlag = m_hb2Flag[Symbol(m_foundState)];
			rc.Decode(hiCount, SummFreq(m_minContext) - hiCount);
			mask.fill(-1);
			mask[Symbol(s)] = 0;
			i = NumStats(m_minContext) - 1;
			do {
				s -= 6;
				mask[Symbol(s)] = 0;
			} while (--i);
		} else {
			const uint32_t probAt = BinSummIndex();
			uint16_t& prob = m_binSumm[probAt];
			if (rc.DecodeBit(prob, BIN_SCALE) == 0) {
				prob = UpdateProb0(prob);
				m_foundState = OneState(m_minContext);
				const uint8_t symbol = Symbol(m_foundState);
				UpdateBin();
				return symbol;
			}
			prob = UpdateProb1(prob);
			m_initEsc = EXP_ESCAPE[prob >> 10];
			mask.fill(-1);
			mask[Symbol(OneState(m_minContext))] = 0;
			m_prevSuccess = 0;
		}
		for (;;) {
			std::array<uint32_t, 256> states;
			const unsigned numMasked = NumStats(m_minContext);
			do {
				++m_orderFall;
				if (Suffix(m_minContext) == 0)
					return -1;
				m_minContext = Suffix(m_minContext);
			} while (NumStats(m_minContext) == numMasked);
			uint32_t hiCount = 0;
			uint32_t s = Stats(m_minContext);
			unsigned i = 0;
			const unsigned num = NumStats(m_minContext) - numMasked;
			do {
				const int k = mask[Symbol(s)];
				hiCount += Freq(s) & uint32_t(k);
				states[i] = s;
				s += 6;
				i -= unsigned(k);
			} while (i != num);
			uint32_t freqSum = 0;
			See* see = MakeEscFreq(numMasked, freqSum);
			freqSum += hiCount;
			const uint32_t count = rc.GetThreshold(freqSum);
			if (count < hiCount) {
				size_t k = 0;
				hiCount = 0;
				while ((hiCount += Freq(states[k])) <= count)
					++k;
				s = states[k];
				rc.Decode(hiCount - Freq(s), Freq(s));
				see->Update();
				m_foundState = s;
				const uint8_t symbol = Symbol(s);
				Update2();
				return symbol;
			}
			if (count >= freqSum)
				return -2;
			rc.Decode(hiCount, freqSum - hiCount);
			see->summ = uint16_t(see->summ + freqSum);
			do {
				mask[Symbol(states[--i])] = 0;
			} while (i != 0);
		}
	}

	/// Code un symbole (0..255) ou le marqueur de fin (-1).
	template <typename Encoder> void EncodeSymbol(Encoder& rc, int symbol) {
		using namespace detail::ppmd;
		std::array<int8_t, 256> mask;
		if (NumStats(m_minContext) != 1) {
			uint32_t s = Stats(m_minContext);
			if (Symbol(s) == symbol) {
				rc.Encode(0, Freq(s), SummFreq(m_minContext));
				m_foundState = s;
				Update1_0();
				return;
			}
			m_prevSuccess = 0;
			uint32_t sum = Freq(s);
			unsigned i = NumStats(m_minContext) - 1;
			do {
				s += 6;
				if (Symbol(s) == symbol) {
					rc.Encode(sum, Freq(s), SummFreq(m_minContext));
					m_foundState = s;
					Update1();
					return;
				}
				sum += Freq(s);
			} while (--i);
			m_hiBitsFlag = m_hb2Flag[Symbol(m_foundState)];
			mask.fill(-1);
			mask[Symbol(s)] = 0;
			i = NumStats(m_minContext) - 1;
			do {
				s -= 6;
				mask[Symbol(s)] = 0;
			} while (--i);
			rc.Encode(sum, SummFreq(m_minContext) - sum, SummFreq(m_minContext));
		} else {
			uint16_t& prob = m_binSumm[BinSummIndex()];
			const uint32_t s = OneState(m_minContext);
			if (Symbol(s) == symbol) {
				rc.EncodeBit0(prob);
				prob = UpdateProb0(prob);
				m_foundState = s;
				UpdateBin();
				return;
			}
			rc.EncodeBit1(prob);
			prob = UpdateProb1(prob);
			m_initEsc = EXP_ESCAPE[prob >> 10];
			mask.fill(-1);
			mask[Symbol(s)] = 0;
			m_prevSuccess = 0;
		}
		for (;;) {
			const unsigned numMasked = NumStats(m_minContext);
			do {
				++m_orderFall;
				if (Suffix(m_minContext) == 0)
					return; // marqueur de fin codé
				m_minContext = Suffix(m_minContext);
			} while (NumStats(m_minContext) == numMasked);
			uint32_t escFreq = 0;
			See* see = MakeEscFreq(numMasked, escFreq);
			uint32_t s = Stats(m_minContext);
			uint32_t sum = 0;
			unsigned i = NumStats(m_minContext);
			do {
				const int current = Symbol(s);
				if (current == symbol) {
					const uint32_t low = sum;
					const uint32_t found = s;
					do {
						sum += Freq(s) & uint32_t(int32_t(mask[Symbol(s)]));
						s += 6;
					} while (--i);
					rc.Encode(low, Freq(found), sum + escFreq);
					see->Update();
					m_foundState = found;
					Update2();
					return;
				}
				sum += Freq(s) & uint32_t(int32_t(mask[size_t(current)]));
				mask[size_t(current)] = 0;
				s += 6;
			} while (--i);
			rc.Encode(sum, escFreq, sum + escFreq);
			see->summ = uint16_t(see->summ + sum + escFreq);
		}
	}

private:
	static constexpr uint8_t EXP_ESCAPE[16] = {25, 14, 9, 7, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2};
	static constexpr uint16_t INIT_BIN_ESC[8] = {0x3CDD, 0x1F3F, 0x59BF, 0x48F3,
												 0x64A1, 0x5ABC, 0x6632, 0x6051};

	// ── Accès aux structures ────────────────────────────────────────────────
	[[nodiscard]] unsigned NumStats(uint32_t c) const { return m_mem.U16(c); }
	void SetNumStats(uint32_t c, unsigned v) { m_mem.SetU16(c, uint16_t(v)); }
	[[nodiscard]] uint32_t SummFreq(uint32_t c) const { return m_mem.U16(c + 2); }
	void SetSummFreq(uint32_t c, uint32_t v) { m_mem.SetU16(c + 2, uint16_t(v)); }
	[[nodiscard]] uint32_t Stats(uint32_t c) const { return m_mem.U32(c + 4); }
	void SetStats(uint32_t c, uint32_t v) { m_mem.SetU32(c + 4, v); }
	[[nodiscard]] uint32_t Suffix(uint32_t c) const { return m_mem.U32(c + 8); }
	void SetSuffix(uint32_t c, uint32_t v) { m_mem.SetU32(c + 8, v); }
	[[nodiscard]] static uint32_t OneState(uint32_t c) noexcept { return c + 2; }

	[[nodiscard]] uint8_t Symbol(uint32_t s) const { return m_mem.U8(s); }
	void SetSymbol(uint32_t s, uint8_t v) { m_mem.SetU8(s, v); }
	[[nodiscard]] uint32_t Freq(uint32_t s) const { return m_mem.U8(s + 1); }
	void SetFreq(uint32_t s, uint32_t v) { m_mem.SetU8(s + 1, uint8_t(v)); }
	[[nodiscard]] uint32_t Successor(uint32_t s) const;
	void SetSuccessor(uint32_t s, uint32_t v);
	void CopyState(uint32_t to, uint32_t from) { m_mem.Copy(to, from, 6); }
	void SwapStates(uint32_t a, uint32_t b);

	// Nœud libre (même disposition qu'un contexte) : Stamp u16 @0, NU u16 @2,
	// Next u32 @4, Prev u32 @8.
	[[nodiscard]] unsigned I2U(unsigned index) const { return m_index2Units[index]; }
	[[nodiscard]] unsigned U2I(unsigned nu) const { return m_units2Index[nu - 1]; }
	[[nodiscard]] static uint32_t U2B(unsigned nu) noexcept;

	void InsertNode(uint32_t node, unsigned index);
	uint32_t RemoveNode(unsigned index);
	void SplitBlock(uint32_t ptr, unsigned oldIndex, unsigned newIndex);

	void GlueFreeBlocks();

	uint32_t AllocUnitsRare(unsigned index);

	uint32_t AllocUnits(unsigned index);

	uint32_t ShrinkUnits(uint32_t oldPtr, unsigned oldNu, unsigned newNu);

	void RestartModel();

	/// Crée les contextes successeurs ; 0 si la mémoire manque.
	uint32_t CreateSuccessors(bool skip);

	void UpdateModel();

	void Rescale();

	detail::ppmd::See* MakeEscFreq(unsigned numMasked, uint32_t& escFreq);

	void NextContext();

	void Update1();
	void Update1_0();
	void UpdateBin();
	void Update2();

	[[nodiscard]] uint32_t BinSummIndex();

	detail::ppmd::Memory m_mem;
	uint32_t m_size = 0, m_alignOffset = 0;
	uint32_t m_minContext = 0, m_maxContext = 0, m_foundState = 0;
	unsigned m_orderFall = 0, m_initEsc = 0, m_prevSuccess = 0, m_maxOrder = 0, m_hiBitsFlag = 0;
	int32_t m_runLength = 0, m_initRl = 0;
	uint32_t m_glueCount = 0;
	uint32_t m_loUnit = 0, m_hiUnit = 0, m_text = 0, m_unitsStart = 0;
	std::array<uint8_t, detail::ppmd::NUM_INDEXES> m_index2Units{};
	std::array<uint8_t, 128> m_units2Index{};
	std::array<uint32_t, detail::ppmd::NUM_INDEXES> m_freeList{};
	std::array<uint8_t, 256> m_ns2Index{}, m_ns2bsIndex{}, m_hb2Flag{};
	detail::ppmd::See m_dummySee;
	std::array<std::array<detail::ppmd::See, 16>, 25> m_see{};
	std::array<uint16_t, 128 * 64> m_binSumm{};
};

// ── Codeurs d'intervalle ────────────────────────────────────────────────────

/// Décodeur d'intervalle de 7z pour PPMd (5 octets d'amorce, le premier nul).
class Ppmd7zRangeDecoder {
public:
	explicit Ppmd7zRangeDecoder(InputBuffer& input) : m_input(&input) {}
	[[nodiscard]] bool Init();
	[[nodiscard]] uint32_t GetThreshold(uint32_t total);
	void Decode(uint32_t start, uint32_t size);
	[[nodiscard]] uint32_t DecodeBit(uint32_t size0, uint32_t);
	/// Vrai si le flux s'est terminé proprement (code nul).
	[[nodiscard]] bool IsFinishedOk() const noexcept { return m_code == 0; }
	[[nodiscard]] bool Overrun() const noexcept { return m_overrun; }

private:
	uint32_t Next();
	void Normalize();

	InputBuffer* m_input;
	uint32_t m_range = 0, m_code = 0;
	bool m_overrun = false;
};

/// Décodeur d'intervalle « sans retenue » de RAR 3 (Subbotin).
class Ppmd7RarRangeDecoder {
public:
	explicit Ppmd7RarRangeDecoder(InputBuffer& input) : m_input(&input) {}
	[[nodiscard]] bool Init();
	[[nodiscard]] uint32_t GetThreshold(uint32_t total);
	void Decode(uint32_t start, uint32_t size);
	[[nodiscard]] uint32_t DecodeBit(uint32_t size0, uint32_t);
	[[nodiscard]] bool Overrun() const noexcept { return m_overrun; }

private:
	uint32_t Next();
	void Normalize();

	InputBuffer* m_input;
	uint32_t m_range = 0, m_code = 0, m_low = 0;
	bool m_overrun = false;
};

/// Codeur d'intervalle de 7z pour PPMd.
class Ppmd7zRangeEncoder {
public:
	void Encode(uint32_t start, uint32_t size, uint32_t total);
	void EncodeBit0(uint32_t size0);
	void EncodeBit1(uint32_t size0);
	void Flush();
	[[nodiscard]] Bytes& Output() noexcept { return m_out; }

private:
	void ShiftLow();

	uint64_t m_low = 0;
	uint32_t m_range = 0xFFFFFFFFu;
	uint8_t m_cache = 0;
	uint64_t m_cacheSize = 1;
	Bytes m_out;
};

namespace detail::ppmd {

/// Flux décodé d'un flux PPMd de 7z (taille connue).
class Ppmd7StreamImpl : public DecoderImpl {
public:
	Ppmd7StreamImpl(ArchiveStream input, unsigned order, uint32_t memory, uint64_t size,
					StreamStatePtr state)
		: DecoderImpl(std::move(state), Some(size)), m_input(std::move(input)),
		  m_buffer(m_input.io, m_input.state), m_rc(m_buffer), m_order(order), m_memory(memory),
		  m_size(size) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_input;
	InputBuffer m_buffer;
	Ppmd7zRangeDecoder m_rc;
	unsigned m_order;
	uint32_t m_memory;
	uint64_t m_size;
	uint64_t m_done = 0;
	bool m_started = false;
	std::unique_ptr<Ppmd7Model> m_model;
};

/// Compresseur PPMd (variante H, codeur de 7z) en flux.
class Ppmd7EncoderImpl : public EncoderImpl {
public:
	Ppmd7EncoderImpl(ArchiveStream sink, unsigned order, uint32_t memory, bool endMarker,
					 StreamStatePtr state);

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override;

private:
	Result<bool, ArchiveError> Drain();

	std::unique_ptr<Ppmd7Model> m_model;
	Ppmd7zRangeEncoder m_rc;
	bool m_endMarker;
};

} // namespace detail::ppmd

/// Flux décompressé d'un flux PPMd de 7z (`order` 2..64, `memory` en octets).
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenPpmd7Stream(ArchiveStream input, unsigned order, uint32_t memory, uint64_t size);

/// Flux en écriture : compression PPMd (variante H, 7z).
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenPpmd7Encoder(ArchiveStream sink, unsigned order, uint32_t memory, bool endMarker = false);

// ============================================================================
// PPMd variante I révision 1 (Ppmd8) — zip, méthode 98
// ============================================================================

/// Méthode de restauration quand la mémoire du modèle est pleine.
enum class Ppmd8Restore : uint8_t { RESTART = 0, CUT_OFF = 1 };

class Ppmd8Model {
public:
	static constexpr int MAX_ORDER = 16;
	static constexpr uint32_t MAX_FREQ = 124;
	static constexpr uint32_t EMPTY_NODE = 0xFFFFFFFFu;

	Ppmd8Model();

	void Init(uint32_t size, unsigned maxOrder, Ppmd8Restore restore);

	// ── Codeur d'intervalle intégré (Subbotin, sans retenue) ────────────────

	[[nodiscard]] bool DecoderInit(InputBuffer& input);
	[[nodiscard]] bool Overrun() const noexcept { return m_overrun; }

	void EncoderInit();
	void EncoderFlush();
	[[nodiscard]] Bytes& EncoderOutput() noexcept { return m_out; }

	/// -1 = marqueur de fin, -2 = données corrompues.
	[[nodiscard]] int DecodeSymbol();

	void EncodeSymbol(int symbol);

private:
	static constexpr uint8_t EXP_ESCAPE[16] = {25, 14, 9, 7, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2};
	static constexpr uint16_t INIT_BIN_ESC[8] = {0x3CDD, 0x1F3F, 0x59BF, 0x48F3,
												 0x64A1, 0x5ABC, 0x6632, 0x6051};

	uint32_t NextByte();
	[[nodiscard]] uint32_t DecThreshold(uint32_t total);
	void DecDecode(uint32_t start, uint32_t size);
	void EncNormalize();
	void EncEncode(uint32_t start, uint32_t size, uint32_t total);

	// ── Structures : contexte NumStats u8 @0 (nombre - 1), Flags u8 @1,
	// SummFreq u16 @2, Stats u32 @4, Suffix u32 @8 ; nœud libre Stamp u32 @0,
	// Next u32 @4, NU u32 @8.
	[[nodiscard]] unsigned NumStats(uint32_t c) const { return m_mem.U8(c); }
	void SetNumStats(uint32_t c, unsigned v) { m_mem.SetU8(c, uint8_t(v)); }
	[[nodiscard]] unsigned Flags(uint32_t c) const { return m_mem.U8(c + 1); }
	void SetFlags(uint32_t c, unsigned v) { m_mem.SetU8(c + 1, uint8_t(v)); }
	[[nodiscard]] uint32_t SummFreq(uint32_t c) const { return m_mem.U16(c + 2); }
	void SetSummFreq(uint32_t c, uint32_t v) { m_mem.SetU16(c + 2, uint16_t(v)); }
	[[nodiscard]] uint32_t Stats(uint32_t c) const { return m_mem.U32(c + 4); }
	void SetStats(uint32_t c, uint32_t v) { m_mem.SetU32(c + 4, v); }
	[[nodiscard]] uint32_t Suffix(uint32_t c) const { return m_mem.U32(c + 8); }
	void SetSuffix(uint32_t c, uint32_t v) { m_mem.SetU32(c + 8, v); }
	[[nodiscard]] static uint32_t OneState(uint32_t c) noexcept { return c + 2; }

	[[nodiscard]] uint8_t Symbol(uint32_t s) const { return m_mem.U8(s); }
	void SetSymbol(uint32_t s, uint8_t v) { m_mem.SetU8(s, v); }
	[[nodiscard]] uint32_t Freq(uint32_t s) const { return m_mem.U8(s + 1); }
	void SetFreq(uint32_t s, uint32_t v) { m_mem.SetU8(s + 1, uint8_t(v)); }
	[[nodiscard]] uint32_t Successor(uint32_t s) const;
	void SetSuccessor(uint32_t s, uint32_t v);
	void CopyState(uint32_t to, uint32_t from) { m_mem.Copy(to, from, 6); }
	void SwapStates(uint32_t a, uint32_t b);
	/// Pointeur « au sens du modèle » : un successeur est un contexte s'il est
	/// au-delà de UnitsStart (sinon c'est une position dans le texte).
	[[nodiscard]] uint32_t Pointer(uint32_t ref) const noexcept { return ref; }

	[[nodiscard]] unsigned I2U(unsigned index) const { return m_index2Units[index]; }
	[[nodiscard]] unsigned U2I(unsigned nu) const { return m_units2Index[nu - 1]; }
	[[nodiscard]] static uint32_t U2B(unsigned nu) noexcept;

	void InsertNode(uint32_t node, unsigned index);
	uint32_t RemoveNode(unsigned index);
	void SplitBlock(uint32_t ptr, unsigned oldIndex, unsigned newIndex);

	void GlueFreeBlocks();

	uint32_t AllocUnitsRare(unsigned index);

	uint32_t AllocUnits(unsigned index);

	uint32_t ShrinkUnits(uint32_t oldPtr, unsigned oldNu, unsigned newNu);

	void FreeUnits(uint32_t ptr, unsigned nu) { InsertNode(ptr, U2I(nu)); }

	void SpecialFreeUnit(uint32_t ptr);

	uint32_t MoveUnitsUp(uint32_t oldPtr, unsigned nu);

	void ExpandTextArea();

	void RestartModel();

	void Refresh(uint32_t ctx, unsigned oldNu, unsigned scale);

	uint32_t CutOff(uint32_t ctx, unsigned order);

	[[nodiscard]] uint32_t GetUsedMemory() const;

	void RestoreModel(uint32_t c1);

	uint32_t CreateSuccessors(bool skip, uint32_t s1, uint32_t c);

	uint32_t ReduceOrder(uint32_t s1, uint32_t c);

	void UpdateModel();

	void Rescale();

	detail::ppmd::See* MakeEscFreq(unsigned numMasked1, uint32_t& escFreq);

	void NextContext();
	void Update1();
	void Update1_0();
	void UpdateBin();
	void Update2();

	[[nodiscard]] uint32_t BinSummIndex() const;

	detail::ppmd::Memory m_mem;
	uint32_t m_size = 0, m_alignOffset = 0;
	Ppmd8Restore m_restore = Ppmd8Restore::RESTART;
	uint32_t m_minContext = 0, m_maxContext = 0, m_foundState = 0;
	unsigned m_orderFall = 0, m_initEsc = 0, m_prevSuccess = 0, m_maxOrder = 0;
	int32_t m_runLength = 0, m_initRl = 0;
	uint32_t m_glueCount = 0;
	uint32_t m_loUnit = 0, m_hiUnit = 0, m_text = 0, m_unitsStart = 0;
	std::array<uint8_t, detail::ppmd::NUM_INDEXES> m_index2Units{};
	std::array<uint8_t, 128> m_units2Index{};
	std::array<uint32_t, detail::ppmd::NUM_INDEXES> m_freeList{};
	std::array<uint32_t, detail::ppmd::NUM_INDEXES> m_stamps{};
	std::array<uint8_t, 256> m_ns2bsIndex{};
	std::array<uint8_t, 260> m_ns2Index{};
	detail::ppmd::See m_dummySee;
	std::array<std::array<detail::ppmd::See, 32>, 24> m_see{};
	std::array<uint16_t, 25 * 64> m_binSumm{};
	// Codeur d'intervalle.
	InputBuffer* m_input = nullptr;
	uint32_t m_low = 0, m_range = 0, m_code = 0;
	bool m_overrun = false;
	Bytes m_out;
};

namespace detail::ppmd {

/// Flux PPMd de zip (méthode 98) : en-tête de 2 octets (ordre, mémoire en
/// Mio, méthode de restauration) puis le flux Ppmd8.
class Ppmd8StreamImpl : public DecoderImpl {
public:
	Ppmd8StreamImpl(ArchiveStream input, uint64_t size, StreamStatePtr state)
		: DecoderImpl(std::move(state), Some(size)), m_input(std::move(input)),
		  m_buffer(m_input.io, m_input.state), m_size(size) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_input;
	InputBuffer m_buffer;
	uint64_t m_size;
	uint64_t m_done = 0;
	bool m_started = false;
	std::unique_ptr<Ppmd8Model> m_model;
};

class Ppmd8EncoderImpl : public EncoderImpl {
public:
	Ppmd8EncoderImpl(ArchiveStream sink, unsigned order, uint32_t memoryMb, Ppmd8Restore restore,
					 StreamStatePtr state);

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override;

private:
	Result<bool, ArchiveError> WriteHeader();
	Result<bool, ArchiveError> Drain();

	std::unique_ptr<Ppmd8Model> m_model;
	uint8_t m_header[2];
	bool m_headerWritten = false;
};

} // namespace detail::ppmd

/// Flux décompressé d'une entrée zip PPMd (méthode 98, en-tête compris).
[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenPpmd8ZipStream(ArchiveStream input,
																			uint64_t size);

/// Flux en écriture : compression PPMd zip (en-tête de 2 octets compris).
/// `order` 2..16, `memoryMb` 1..256.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenPpmd8ZipEncoder(ArchiveStream sink, unsigned order = 8, uint32_t memoryMb = 16,
					Ppmd8Restore restore = Ppmd8Restore::CUT_OFF);

} // namespace data::archive
