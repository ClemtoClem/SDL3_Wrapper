// Définitions de data/archive/archive_lzma.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/archive/archive_lzma.hpp"

namespace data::archive {

// ── LzmaProperties ───────────────────────────────────────────────────────────

uint8_t LzmaProperties::PropertiesByte() const noexcept {
	return uint8_t((pb * 5 + lp) * 9 + lc);
}

std::array<uint8_t, 5> LzmaProperties::Encode() const noexcept {
	return {PropertiesByte(), uint8_t(dictionarySize), uint8_t(dictionarySize >> 8),
			uint8_t(dictionarySize >> 16), uint8_t(dictionarySize >> 24)};
}

Result<LzmaProperties, String> LzmaProperties::FromByte(uint8_t byte) {
	if (byte >= 9 * 5 * 5)
		return Err(String("LZMA : octet de propriétés invalide"));
	LzmaProperties props;
	props.lc = uint8_t(byte % 9);
	byte = uint8_t(byte / 9);
	props.lp = uint8_t(byte % 5);
	props.pb = uint8_t(byte / 5);
	return Ok(props);
}

Result<LzmaProperties, String> LzmaProperties::Decode(std::span<const uint8_t> bytes) {
	if (bytes.size() < 5)
		return Err(String("LZMA : propriétés tronquées (5 octets attendus)"));
	auto props = FromByte(bytes[0]);
	if (props.IsError())
		return props;
	props.Value().dictionarySize = uint32_t(bytes[1]) | (uint32_t(bytes[2]) << 8) |
								   (uint32_t(bytes[3]) << 16) | (uint32_t(bytes[4]) << 24);
	return props;
}

uint32_t Lzma2DictionarySize(uint8_t byte) noexcept {
	if (byte >= 40)
		return 0xFFFFFFFFu;
	return (2u | (byte & 1u)) << (byte / 2 + 11);
}

uint8_t Lzma2DictionaryByte(uint32_t size) noexcept {
	for (uint8_t byte = 0; byte < 40; ++byte)
		if (Lzma2DictionarySize(byte) >= size)
			return byte;
	return 40;
}

namespace detail::lzma {

// ── Model::Length ────────────────────────────────────────────────────────────

void Model::Length::Reset() noexcept {
	choice = choice2 = PROB_INIT;
	for (auto& table : low)
		table.fill(PROB_INIT);
	for (auto& table : mid)
		table.fill(PROB_INIT);
	high.fill(PROB_INIT);
}

// ── Model ────────────────────────────────────────────────────────────────────

void Model::Reset(const LzmaProperties& props) {
	literal.assign(size_t(0x300) << (props.lc + props.lp), PROB_INIT);
	isMatch.fill(PROB_INIT);
	isRep.fill(PROB_INIT);
	isRepG0.fill(PROB_INIT);
	isRepG1.fill(PROB_INIT);
	isRepG2.fill(PROB_INIT);
	isRep0Long.fill(PROB_INIT);
	for (auto& table : posSlot)
		table.fill(PROB_INIT);
	posSpecial.fill(PROB_INIT);
	align.fill(PROB_INIT);
	length.Reset();
	repLength.Reset();
}

// ── RangeDecoder ─────────────────────────────────────────────────────────────

bool RangeDecoder::Init(InputBuffer& input) {
	m_input = &input;
	m_range = 0xFFFFFFFFu;
	m_code = 0;
	m_corrupted = m_overrun = false;
	m_start = input.Consumed();
	uint8_t first = 0;
	if (!input.Byte(first) || first != 0)
		return false;
	for (int i = 0; i < 4; ++i) {
		uint8_t byte = 0;
		if (!input.Byte(byte))
			return false;
		m_code = (m_code << 8) | byte;
	}
	return m_code != m_range;
}

int RangeDecoder::Bit(uint16_t& prob) {
	const uint32_t bound = (m_range >> NUM_BIT_MODEL_TOTAL_BITS) * prob;
	int symbol;
	if (m_code < bound) {
		prob = uint16_t(prob + ((BIT_MODEL_TOTAL - prob) >> NUM_MOVE_BITS));
		m_range = bound;
		symbol = 0;
	} else {
		prob = uint16_t(prob - (prob >> NUM_MOVE_BITS));
		m_code -= bound;
		m_range -= bound;
		symbol = 1;
	}
	Normalize();
	return symbol;
}

uint32_t RangeDecoder::DirectBits(int count) {
	uint32_t result = 0;
	do {
		m_range >>= 1;
		m_code -= m_range;
		const uint32_t t = 0u - (m_code >> 31);
		m_code += m_range & t;
		if (m_code == m_range)
			m_corrupted = true;
		Normalize();
		result = (result << 1) + (t + 1);
	} while (--count > 0);
	return result;
}

uint32_t RangeDecoder::BitTree(std::span<uint16_t> probs, int bits) {
	uint32_t m = 1;
	for (int i = 0; i < bits; ++i)
		m = (m << 1) + uint32_t(Bit(probs[m]));
	return m - (1u << bits);
}

uint32_t RangeDecoder::ReverseBitTree(uint16_t* probs, int bits) {
	uint32_t m = 1, symbol = 0;
	for (int i = 0; i < bits; ++i) {
		const uint32_t bit = uint32_t(Bit(probs[m]));
		m = (m << 1) + bit;
		symbol |= bit << i;
	}
	return symbol;
}

void RangeDecoder::Normalize() {
	if (m_range < TOP_VALUE) {
		m_range <<= 8;
		uint8_t next = 0;
		if (!m_input->Byte(next))
			m_overrun = true;
		m_code = (m_code << 8) | next;
	}
}

// ── Decoder ──────────────────────────────────────────────────────────────────

void Decoder::SetProperties(const LzmaProperties& props) {
	m_props = props;
	ResetState();
}

void Decoder::ResetState() {
	m_model.Reset(m_props);
	m_state = 0;
	m_reps = {0, 0, 0, 0};
	m_pending = 0;
}

void Decoder::ResetDictionary(uint64_t capacity) {
	m_window.Reset(capacity);
	m_pending = 0;
}

Result<Decoder::Stop, String> Decoder::Decode(RangeDecoder& rc, uint8_t* out, size_t max,
		size_t& produced, uint64_t& remaining, bool sizeKnown,
		bool markerAllowed) {
	const uint32_t pbMask = (1u << m_props.pb) - 1;
	const uint32_t lpMask = (1u << m_props.lp) - 1;
	for (;;) {
		// Suite d'une correspondance interrompue par un tampon plein.
		while (m_pending > 0) {
			if (produced == max)
				return Ok(Stop::OUTPUT_FULL);
			if (sizeKnown && remaining == 0)
				return Err(String("LZMA : correspondance au-delà de la taille annoncée"));
			const uint8_t byte = m_window.Get(uint64_t(m_reps[0]) + 1);
			m_window.Put(byte);
			out[produced++] = byte;
			--m_pending;
			--remaining;
		}
		if (rc.Overrun())
			return Err(String("LZMA : flux tronqué"));
		if (rc.Corrupted())
			return Err(String("LZMA : bits directs corrompus"));
		if (sizeKnown && remaining == 0 && (!markerAllowed || rc.IsFinishedOk()))
			return Ok(Stop::SIZE_REACHED);
		if (produced == max)
			return Ok(Stop::OUTPUT_FULL);

		const uint64_t position = m_window.Total();
		const uint32_t posState = uint32_t(position) & pbMask;
		if (rc.Bit(m_model.isMatch[size_t((m_state << NUM_POS_BITS_MAX) + int(posState))]) ==
			0) {
			if (sizeKnown && remaining == 0)
				return Err(String("LZMA : données au-delà de la taille annoncée"));
			const uint8_t previous = position > 0 ? m_window.Get(1) : 0;
			const size_t context =
				((uint32_t(position) & lpMask) << m_props.lc) + (previous >> (8 - m_props.lc));
			uint16_t* probs = m_model.literal.data() + 0x300 * context;
			uint32_t symbol = 1;
			if (m_state >= 7) {
				uint32_t matchByte = m_window.Has(uint64_t(m_reps[0]) + 1)
										 ? m_window.Get(uint64_t(m_reps[0]) + 1)
										 : 0;
				do {
					const uint32_t matchBit = (matchByte >> 7) & 1;
					matchByte <<= 1;
					const uint32_t bit =
						uint32_t(rc.Bit(probs[((1 + matchBit) << 8) + symbol]));
					symbol = (symbol << 1) | bit;
					if (matchBit != bit)
						break;
				} while (symbol < 0x100);
			}
			while (symbol < 0x100)
				symbol = (symbol << 1) | uint32_t(rc.Bit(probs[symbol]));
			const uint8_t byte = uint8_t(symbol - 0x100);
			m_window.Put(byte);
			out[produced++] = byte;
			m_state = StateAfterLiteral(m_state);
			--remaining;
			continue;
		}

		uint32_t length;
		if (rc.Bit(m_model.isRep[size_t(m_state)]) != 0) {
			if (sizeKnown && remaining == 0)
				return Err(String("LZMA : données au-delà de la taille annoncée"));
			if (position == 0)
				return Err(String("LZMA : répétition dans un dictionnaire vide"));
			if (rc.Bit(m_model.isRepG0[size_t(m_state)]) == 0) {
				if (rc.Bit(m_model.isRep0Long[size_t((m_state << NUM_POS_BITS_MAX) +
													 int(posState))]) == 0) {
					m_state = StateAfterShortRep(m_state);
					if (!m_window.Has(uint64_t(m_reps[0]) + 1))
						return Err(String("LZMA : distance hors du dictionnaire"));
					const uint8_t byte = m_window.Get(uint64_t(m_reps[0]) + 1);
					m_window.Put(byte);
					out[produced++] = byte;
					--remaining;
					continue;
				}
			} else {
				uint32_t distance;
				if (rc.Bit(m_model.isRepG1[size_t(m_state)]) == 0) {
					distance = m_reps[1];
				} else {
					if (rc.Bit(m_model.isRepG2[size_t(m_state)]) == 0) {
						distance = m_reps[2];
					} else {
						distance = m_reps[3];
						m_reps[3] = m_reps[2];
					}
					m_reps[2] = m_reps[1];
				}
				m_reps[1] = m_reps[0];
				m_reps[0] = distance;
			}
			length = DecodeLength(rc, m_model.repLength, posState);
			m_state = StateAfterRep(m_state);
		} else {
			m_reps[3] = m_reps[2];
			m_reps[2] = m_reps[1];
			m_reps[1] = m_reps[0];
			length = DecodeLength(rc, m_model.length, posState);
			m_state = StateAfterMatch(m_state);
			m_reps[0] = DecodeDistance(rc, length);
			if (m_reps[0] == 0xFFFFFFFFu) {
				if (!markerAllowed)
					return Err(String("LZMA : marqueur de fin inattendu"));
				if (!rc.IsFinishedOk() || rc.Overrun())
					return Err(String("LZMA : marqueur de fin corrompu"));
				if (sizeKnown && remaining != 0)
					return Err(String("LZMA : fin de flux avant la taille annoncée"));
				return Ok(Stop::END_MARKER);
			}
			if (sizeKnown && remaining == 0)
				return Err(String("LZMA : données au-delà de la taille annoncée"));
			if (m_reps[0] >= m_props.dictionarySize)
				return Err(String("LZMA : distance hors du dictionnaire"));
		}
		if (!m_window.Has(uint64_t(m_reps[0]) + 1))
			return Err(String("LZMA : distance hors du dictionnaire"));
		m_pending = length + MATCH_MIN_LEN;
	}
}

uint32_t Decoder::DecodeLength(RangeDecoder& rc, Model::Length& model, uint32_t posState) {
	if (rc.Bit(model.choice) == 0)
		return rc.BitTree(model.low[posState], 3);
	if (rc.Bit(model.choice2) == 0)
		return 8 + rc.BitTree(model.mid[posState], 3);
	return 16 + rc.BitTree(model.high, 8);
}

uint32_t Decoder::DecodeDistance(RangeDecoder& rc, uint32_t length) {
	const uint32_t lengthState = std::min<uint32_t>(length, NUM_LEN_TO_POS_STATES - 1);
	const uint32_t slot = rc.BitTree(m_model.posSlot[lengthState], 6);
	if (slot < 4)
		return slot;
	const int directBits = int((slot >> 1) - 1);
	uint32_t distance = (2 | (slot & 1)) << directBits;
	if (slot < uint32_t(END_POS_MODEL_INDEX)) {
		distance += rc.ReverseBitTree(m_model.posSpecial.data() + distance - slot, directBits);
	} else {
		distance += rc.DirectBits(directBits - NUM_ALIGN_BITS) << NUM_ALIGN_BITS;
		distance += rc.ReverseBitTree(m_model.align.data(), NUM_ALIGN_BITS);
	}
	return distance;
}

// ── LzmaStreamImpl ───────────────────────────────────────────────────────────

Result<size_t, ArchiveError> LzmaStreamImpl::Produce(uint8_t* out, size_t max) {
	if (m_done)
		return Ok(size_t(0));
	if (!m_started) {
		if (!m_buffer.Skip(m_headerSkip))
			return Err(MakeError(ErrorKind::CORRUPT, String("LZMA : en-tête tronqué")));
		m_decoder.SetProperties(m_props);
		m_decoder.ResetDictionary(
			m_unpackSize == UINT64_MAX
				? m_props.dictionarySize
				: std::min<uint64_t>(m_props.dictionarySize, m_unpackSize));
		m_remaining = m_unpackSize;
		if (!m_rc.Init(m_buffer))
			return Err(Failure(String("LZMA : début de flux invalide")));
		m_started = true;
	}
	size_t produced = 0;
	auto stop = m_decoder.Decode(m_rc, out, max, produced, m_remaining,
								 m_unpackSize != UINT64_MAX, m_markerAllowed);
	if (stop.IsError())
		return Err(Failure(stop.Error()));
	if (stop.Value() != Decoder::Stop::OUTPUT_FULL)
		m_done = true;
	return Ok(produced);
}

bool LzmaStreamImpl::Restart() {
	if (!m_buffer.Rewind())
		return false;
	m_started = m_done = false;
	return true;
}

ArchiveError LzmaStreamImpl::Failure(const String& message) const {
	if (m_buffer.Failed())
		return m_buffer.Failure("LZMA");
	return MakeError(ErrorKind::CORRUPT, message);
}

// ── Lzma2Decoder ─────────────────────────────────────────────────────────────

void Lzma2Decoder::Reset(uint32_t dictionarySize) {
	m_dictionarySize = dictionarySize == 0 ? 0xFFFFFFFFu : dictionarySize;
	m_props = LzmaProperties{};
	m_props.dictionarySize = m_dictionarySize;
	m_mode = Mode::CONTROL;
	m_needDictionaryReset = m_needProperties = true;
	m_haveProperties = false;
	m_decoder.ResetDictionary(m_dictionarySize);
}

Result<size_t, String> Lzma2Decoder::Decode(InputBuffer& input, uint8_t* out, size_t max) {
	size_t produced = 0;
	while (produced < max) {
		switch (m_mode) {
		case Mode::DONE:
			return Ok(produced);
		case Mode::CONTROL: {
			if (auto error = ReadChunkHeader(input); error.IsSome())
				return Err(error.Unwrap());
			break;
		}
		case Mode::UNCOMPRESSED: {
			while (m_chunkRemaining > 0 && produced < max) {
				uint8_t byte = 0;
				if (!input.Byte(byte))
					return Err(String("LZMA2 : morceau non compressé tronqué"));
				m_decoder.PutRaw(byte);
				out[produced++] = byte;
				--m_chunkRemaining;
			}
			if (m_chunkRemaining == 0)
				m_mode = Mode::CONTROL;
			break;
		}
		case Mode::LZMA: {
			auto stop =
				m_decoder.Decode(m_rc, out, max, produced, m_chunkRemaining, true, false);
			if (stop.IsError())
				return Err(stop.Error());
			if (stop.Value() == Decoder::Stop::SIZE_REACHED) {
				if (m_rc.Consumed() != m_chunkPacked)
					return Err(String("LZMA2 : taille compressée du morceau incohérente"));
				m_mode = Mode::CONTROL;
			}
			break;
		}
		}
	}
	return Ok(produced);
}

Option<String> Lzma2Decoder::ReadChunkHeader(InputBuffer& input) {
	uint8_t control = 0;
	if (!input.Byte(control))
		return Some(String("LZMA2 : flux tronqué (octet de contrôle)"));
	if (control == 0x00) {
		m_mode = Mode::DONE;
		return NONE;
	}
	if (control >= 0xE0 || control == 0x01) {
		m_needProperties = true;
		m_needDictionaryReset = false;
		m_decoder.ResetDictionary(std::min<uint64_t>(m_dictionarySize, m_capacityHint));
	} else if (m_needDictionaryReset) {
		return Some(String("LZMA2 : réinitialisation du dictionnaire manquante"));
	}
	uint8_t header[4];
	if (control >= 0x80) {
		if (input.ReadRaw(header, 4) != 4)
			return Some(String("LZMA2 : en-tête de morceau tronqué"));
		m_chunkRemaining =
			((uint64_t(control & 0x1F) << 16) | (uint64_t(header[0]) << 8) | header[1]) + 1;
		m_chunkPacked = ((uint64_t(header[2]) << 8) | header[3]) + 1;
		if (control >= 0xC0) {
			uint8_t propsByte = 0;
			if (!input.Byte(propsByte))
				return Some(String("LZMA2 : propriétés tronquées"));
			auto parsed = LzmaProperties::FromByte(propsByte);
			if (parsed.IsError() || parsed.Value().lc + parsed.Value().lp > 4)
				return Some(String("LZMA2 : propriétés invalides"));
			parsed.Value().dictionarySize = m_dictionarySize;
			m_props = parsed.Value();
			m_needProperties = false;
			m_haveProperties = true;
			// Changer les propriétés réinitialise l'état, pas le dictionnaire.
			m_decoder.SetProperties(m_props);
		} else if (m_needProperties || !m_haveProperties) {
			return Some(String("LZMA2 : propriétés manquantes"));
		} else if (control >= 0xA0) {
			m_decoder.ResetState();
		}
		if (!m_rc.Init(input))
			return Some(String("LZMA2 : début de morceau invalide"));
		m_mode = Mode::LZMA;
		return NONE;
	}
	if (control > 0x02)
		return Some(String("LZMA2 : octet de contrôle invalide"));
	if (input.ReadRaw(header, 2) != 2)
		return Some(String("LZMA2 : en-tête de morceau tronqué"));
	m_chunkRemaining = ((uint64_t(header[0]) << 8) | header[1]) + 1;
	m_mode = Mode::UNCOMPRESSED;
	return NONE;
}

// ── Lzma2StreamImpl ──────────────────────────────────────────────────────────

Lzma2StreamImpl::Lzma2StreamImpl(ArchiveStream input, uint32_t dictionarySize, Option<uint64_t> size,
		StreamStatePtr state)
	: DecoderImpl(std::move(state), size), m_input(std::move(input)),
	  m_buffer(m_input.io, m_input.state), m_dictionarySize(dictionarySize), m_size(size) {
	Reset();
}

Result<size_t, ArchiveError> Lzma2StreamImpl::Produce(uint8_t* out, size_t max) {
	auto produced = m_decoder.Decode(m_buffer, out, max);
	if (produced.IsError())
		return Err(m_buffer.Failed() ? m_buffer.Failure("LZMA2")
									 : MakeError(ErrorKind::CORRUPT, produced.Error()));
	return Ok(produced.Value());
}

bool Lzma2StreamImpl::Restart() {
	if (!m_buffer.Rewind())
		return false;
	Reset();
	return true;
}

void Lzma2StreamImpl::Reset() {
	m_decoder.Reset(m_dictionarySize);
	if (m_size.IsSome())
		m_decoder.LimitDictionary(m_size.Value());
}

// ── RangeEncoder ─────────────────────────────────────────────────────────────

void RangeEncoder::Bit(uint16_t& prob, uint32_t bit) {
	uint32_t bound = (m_range >> NUM_BIT_MODEL_TOTAL_BITS) * prob;
	if (bit == 0) {
		m_range = bound;
		prob = uint16_t(prob + ((BIT_MODEL_TOTAL - prob) >> NUM_MOVE_BITS));
	} else {
		m_low += bound;
		m_range -= bound;
		prob = uint16_t(prob - (prob >> NUM_MOVE_BITS));
	}
	while (m_range < TOP_VALUE) {
		m_range <<= 8;
		ShiftLow();
	}
}

void RangeEncoder::DirectBits(uint32_t value, int count) {
	do {
		m_range >>= 1;
		m_low += m_range & (0u - ((value >> --count) & 1u));
		if (m_range < TOP_VALUE) {
			m_range <<= 8;
			ShiftLow();
		}
	} while (count > 0);
}

void RangeEncoder::BitTree(std::span<uint16_t> probs, int bits, uint32_t symbol) {
	uint32_t m = 1;
	for (int i = bits; i-- > 0;) {
		uint32_t bit = (symbol >> i) & 1;
		Bit(probs[m], bit);
		m = (m << 1) | bit;
	}
}

void RangeEncoder::ReverseBitTree(uint16_t* probs, int bits, uint32_t symbol) {
	uint32_t m = 1;
	for (int i = 0; i < bits; ++i) {
		uint32_t bit = symbol & 1;
		Bit(probs[m], bit);
		m = (m << 1) | bit;
		symbol >>= 1;
	}
}

void RangeEncoder::Flush() {
	for (int i = 0; i < 5; ++i)
		ShiftLow();
}

size_t RangeEncoder::PendingSize() const noexcept {
	return m_out.size() + size_t(m_cacheSize) + 5;
}

void RangeEncoder::ShiftLow() {
	if (uint32_t(m_low) < 0xFF000000u || (m_low >> 32) != 0) {
		uint8_t temp = m_cache;
		do {
			m_out.push_back(uint8_t(temp + uint8_t(m_low >> 32)));
			temp = 0xFF;
		} while (--m_cacheSize != 0);
		m_cache = uint8_t(uint32_t(m_low) >> 24);
	}
	++m_cacheSize;
	m_low = uint64_t(uint32_t(m_low) << 8);
}

EncoderParameters EncoderParametersFor(int level) noexcept {
	static constexpr EncoderParameters TABLE[10] = {
		{4, 16, false}, {8, 24, false},	 {16, 32, false},  {24, 48, false},	 {32, 64, false},
		{48, 96, true}, {96, 128, true}, {192, 192, true}, {512, 273, true}, {1024, 273, true}};
	return TABLE[level < 0 ? 0 : (level > 9 ? 9 : level)];
}

// ── Encoder ──────────────────────────────────────────────────────────────────

Encoder::Encoder(const LzmaProperties& props, int level, uint64_t sizeHint)
	: m_props(props), m_params(EncoderParametersFor(level)) {
	uint64_t window =
		std::min<uint64_t>(std::max<uint64_t>(props.dictionarySize, 4096), uint64_t(1) << 22);
	window = std::min<uint64_t>(window, std::max<uint64_t>(sizeHint, 4096));
	m_windowMask = 1;
	while (m_windowMask < window)
		m_windowMask <<= 1;
	m_windowMask -= 1;
	m_maxDistance = uint32_t(std::min<uint64_t>(m_windowMask, props.dictionarySize - 1));
	m_head.assign(size_t(1) << HASH_BITS, UINT32_MAX);
	m_previous.assign(size_t(m_windowMask) + 1, UINT32_MAX);
	m_model.Reset(props);
}

void Encoder::ResetState() {
	m_model.Reset(m_props);
	m_state = 0;
	m_reps = {0, 0, 0, 0};
}

void Encoder::Feed(std::span<const uint8_t> data) {
	m_input.insert(m_input.end(), data.begin(), data.end());
}

void Encoder::Slide(size_t keep) {
	const size_t floor = m_input.empty() ? m_base : std::min(keep, End());
	if (floor <= m_base || floor - m_base < (size_t(1) << 22))
		return;
	const size_t delta = floor - m_base;
	m_input.erase(m_input.begin(), m_input.begin() + std::ptrdiff_t(delta));
	m_base = floor;
	const size_t tableDelta = m_base - m_tableBase;
	auto rebase = [tableDelta](uint32_t& entry) {
		if (entry == UINT32_MAX)
			return;
		entry = entry < tableDelta ? UINT32_MAX : uint32_t(entry - tableDelta);
	};
	for (uint32_t& entry : m_head)
		rebase(entry);
	for (uint32_t& entry : m_previous)
		rebase(entry);
	m_tableBase = m_base;
}

size_t Encoder::EncodeRange(RangeEncoder& rc, size_t position, size_t end, size_t outputLimit) {
	const uint32_t pbMask = (1u << m_props.pb) - 1;
	const size_t dataEnd = End();
	while (position < end && rc.PendingSize() + 32 < outputLimit) {
		const uint32_t posState = uint32_t(position) & pbMask;
		const size_t available = std::min<size_t>(MATCH_MAX_LEN, dataEnd - position);

		// Meilleure des 4 distances répétées.
		uint32_t repLength = 0, repIndex = 0;
		for (uint32_t i = 0; i < 4; ++i) {
			uint32_t length = MatchLength(position, m_reps[i], available);
			if (length > repLength) {
				repLength = length;
				repIndex = i;
			}
		}
		uint32_t mainLength = 0, mainDistance = 0;
		FindMatch(position, available, mainLength, mainDistance);

		enum class Choice { LITERAL, SHORT_REP, REP, MATCH } choice = Choice::LITERAL;
		uint32_t length = 1;
		if (repLength >= uint32_t(m_params.niceLength) ||
			(repLength >= 2 && repLength + 1 >= mainLength) ||
			(repLength >= 2 && repLength + 2 >= mainLength && mainDistance >= (1u << 9)) ||
			(repLength >= 2 && repLength + 3 >= mainLength && mainDistance >= (1u << 15))) {
			choice = Choice::REP;
			length = repLength;
		} else if (mainLength >= 3 || (mainLength == 2 && mainDistance < 128)) {
			choice = Choice::MATCH;
			length = mainLength;
			if (m_params.lazy && length < uint32_t(m_params.niceLength) &&
				position + 1 < dataEnd) {
				uint32_t nextLength = 0, nextDistance = 0;
				Insert(position);
				m_skipInsert = true;
				FindMatch(position + 1, std::min<size_t>(MATCH_MAX_LEN, dataEnd - position - 1),
						  nextLength, nextDistance);
				if (nextLength > length + 1 ||
					(nextLength == length + 1 && nextDistance <= mainDistance)) {
					choice = Choice::LITERAL;
					length = 1;
				}
			}
		}
		if (choice == Choice::LITERAL && position > 0 && m_reps[0] < position &&
			position - m_reps[0] - 1 >= m_base &&
			At(position) == At(position - m_reps[0] - 1) && m_state >= 7) {
			choice = Choice::SHORT_REP;
		}

		switch (choice) {
		case Choice::LITERAL:
			EncodeLiteral(rc, position, posState);
			break;
		case Choice::SHORT_REP:
			rc.Bit(m_model.isMatch[size_t((m_state << NUM_POS_BITS_MAX) + int(posState))], 1);
			rc.Bit(m_model.isRep[size_t(m_state)], 1);
			rc.Bit(m_model.isRepG0[size_t(m_state)], 0);
			rc.Bit(m_model.isRep0Long[size_t((m_state << NUM_POS_BITS_MAX) + int(posState))],
				   0);
			m_state = StateAfterShortRep(m_state);
			break;
		case Choice::REP:
			EncodeRep(rc, repIndex, length, posState);
			break;
		case Choice::MATCH:
			EncodeMatch(rc, mainDistance, length, posState);
			break;
		}
		for (uint32_t i = 0; i < length; ++i) {
			if (i == 0 && m_skipInsert) {
				m_skipInsert = false;
				continue;
			}
			Insert(position + i);
		}
		m_skipInsert = false;
		position += length;
	}
	return position;
}

void Encoder::EncodeEndMarker(RangeEncoder& rc, size_t position) {
	const uint32_t posState = uint32_t(position) & ((1u << m_props.pb) - 1);
	EncodeMatch(rc, 0xFFFFFFFFu, MATCH_MIN_LEN, posState);
}

uint32_t Encoder::MatchLength(size_t position, uint32_t rep, size_t available) const noexcept {
	if (uint64_t(rep) + 1 > position || position - rep - 1 < m_base)
		return 0;
	const size_t from = position - rep - 1;
	uint32_t length = 0;
	while (length < available && At(from + length) == At(position + length))
		++length;
	return length;
}

uint32_t Encoder::Hash(size_t position) const noexcept {
	uint32_t value =
		(uint32_t(At(position)) << 16) | (uint32_t(At(position + 1)) << 8) | At(position + 2);
	return (value * 2654435761u) >> (32 - HASH_BITS);
}

void Encoder::Insert(size_t position) {
	if (position + 3 > End())
		return;
	uint32_t hash = Hash(position);
	m_previous[position & m_windowMask] = m_head[hash];
	m_head[hash] = uint32_t(position - m_tableBase);
}

void Encoder::FindMatch(size_t position, size_t available, uint32_t& bestLength, uint32_t& bestDistance) const {
	bestLength = 0;
	bestDistance = 0;
	if (available < 3 || position + 3 > End())
		return;
	uint32_t stored = m_head[Hash(position)];
	int chain = m_params.maxChain;
	while (stored != UINT32_MAX && chain-- > 0 && bestLength < available) {
		const size_t candidate = m_tableBase + stored;
		if (candidate >= position || candidate < m_base)
			break;
		const size_t distance = position - candidate - 1;
		if (distance > m_maxDistance)
			break;
		if (At(candidate + bestLength) == At(position + bestLength)) {
			uint32_t length = 0;
			while (length < available && At(candidate + length) == At(position + length))
				++length;
			if (length > bestLength) {
				bestLength = length;
				bestDistance = uint32_t(distance);
				if (length >= uint32_t(m_params.niceLength))
					break;
			}
		}
		const uint32_t next = m_previous[candidate & m_windowMask];
		if (next == UINT32_MAX || m_tableBase + next >= candidate)
			break;
		stored = next;
	}
}

void Encoder::EncodeLiteral(RangeEncoder& rc, size_t position, uint32_t posState) {
	rc.Bit(m_model.isMatch[size_t((m_state << NUM_POS_BITS_MAX) + int(posState))], 0);
	const uint8_t previous = position > m_base ? At(position - 1) : 0;
	const uint32_t lpMask = (1u << m_props.lp) - 1;
	size_t context =
		((uint32_t(position) & lpMask) << m_props.lc) + (previous >> (8 - m_props.lc));
	uint16_t* probs = m_model.literal.data() + 0x300 * context;
	const uint8_t byte = At(position);
	uint32_t symbol = 1;
	bool matched = m_state >= 7;
	uint8_t matchByte =
		(matched && uint64_t(m_reps[0]) + 1 <= position && position - m_reps[0] - 1 >= m_base)
			? At(position - m_reps[0] - 1)
			: 0;
	for (int bitIndex = 7; bitIndex >= 0; --bitIndex) {
		uint32_t bit = (byte >> bitIndex) & 1;
		if (matched) {
			uint32_t matchBit = (matchByte >> bitIndex) & 1;
			rc.Bit(probs[((1 + matchBit) << 8) + symbol], bit);
			matched = matchBit == bit;
		} else {
			rc.Bit(probs[symbol], bit);
		}
		symbol = (symbol << 1) | bit;
	}
	m_state = StateAfterLiteral(m_state);
}

void Encoder::EncodeLength(RangeEncoder& rc, Model::Length& model, uint32_t length, uint32_t posState) {
	length -= MATCH_MIN_LEN;
	if (length < 8) {
		rc.Bit(model.choice, 0);
		rc.BitTree(model.low[posState], 3, length);
	} else if (length < 16) {
		rc.Bit(model.choice, 1);
		rc.Bit(model.choice2, 0);
		rc.BitTree(model.mid[posState], 3, length - 8);
	} else {
		rc.Bit(model.choice, 1);
		rc.Bit(model.choice2, 1);
		rc.BitTree(model.high, 8, length - 16);
	}
}

void Encoder::EncodeRep(RangeEncoder& rc, uint32_t repIndex, uint32_t length, uint32_t posState) {
	rc.Bit(m_model.isMatch[size_t((m_state << NUM_POS_BITS_MAX) + int(posState))], 1);
	rc.Bit(m_model.isRep[size_t(m_state)], 1);
	if (repIndex == 0) {
		rc.Bit(m_model.isRepG0[size_t(m_state)], 0);
		rc.Bit(m_model.isRep0Long[size_t((m_state << NUM_POS_BITS_MAX) + int(posState))], 1);
	} else {
		rc.Bit(m_model.isRepG0[size_t(m_state)], 1);
		if (repIndex == 1) {
			rc.Bit(m_model.isRepG1[size_t(m_state)], 0);
		} else {
			rc.Bit(m_model.isRepG1[size_t(m_state)], 1);
			rc.Bit(m_model.isRepG2[size_t(m_state)], repIndex - 2);
		}
		uint32_t distance = m_reps[repIndex];
		for (uint32_t i = repIndex; i > 0; --i)
			m_reps[i] = m_reps[i - 1];
		m_reps[0] = distance;
	}
	EncodeLength(rc, m_model.repLength, length, posState);
	m_state = StateAfterRep(m_state);
}

void Encoder::EncodeMatch(RangeEncoder& rc, uint32_t distance, uint32_t length, uint32_t posState) {
	rc.Bit(m_model.isMatch[size_t((m_state << NUM_POS_BITS_MAX) + int(posState))], 1);
	rc.Bit(m_model.isRep[size_t(m_state)], 0);
	EncodeLength(rc, m_model.length, length, posState);
	uint32_t lengthState =
		std::min<uint32_t>(length - MATCH_MIN_LEN, NUM_LEN_TO_POS_STATES - 1);
	uint32_t slot;
	if (distance < 4) {
		slot = distance;
	} else {
		int highest = 31;
		while (((distance >> highest) & 1) == 0)
			--highest;
		slot = (uint32_t(highest) << 1) | ((distance >> (highest - 1)) & 1);
	}
	rc.BitTree(m_model.posSlot[lengthState], 6, slot);
	if (slot >= 4) {
		int footerBits = int((slot >> 1) - 1);
		uint32_t base = (2 | (slot & 1)) << footerBits;
		uint32_t reduced = distance - base;
		if (slot < uint32_t(END_POS_MODEL_INDEX)) {
			rc.ReverseBitTree(m_model.posSpecial.data() + base - slot, footerBits, reduced);
		} else {
			rc.DirectBits(reduced >> NUM_ALIGN_BITS, footerBits - NUM_ALIGN_BITS);
			rc.ReverseBitTree(m_model.align.data(), NUM_ALIGN_BITS,
							  reduced & ((1u << NUM_ALIGN_BITS) - 1));
		}
	}
	m_reps[3] = m_reps[2];
	m_reps[2] = m_reps[1];
	m_reps[1] = m_reps[0];
	m_reps[0] = distance;
	m_state = StateAfterMatch(m_state);
}

} // namespace detail::lzma

Result<ArchiveStream, ArchiveError> OpenLzmaStream(ArchiveStream input, const LzmaProperties& props, uint64_t unpackSize,
		bool endMarkerAllowed) {
	if (props.lc > 8 || props.lp > 4 || props.pb > 4)
		return Err(MakeError(ErrorKind::CORRUPT, String("LZMA : propriétés hors bornes")));
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::lzma::LzmaStreamImpl>(
						  std::move(input), props, unpackSize, endMarkerAllowed, 0, state),
					  state);
}

namespace detail::lzma {

Result<Bytes, String> DrainStream(Result<ArchiveStream, ArchiveError> stream, uint64_t limit, uint64_t sizeHint) {
	if (stream.IsError())
		return Err(stream.Error().message);
	auto bytes = ReadStreamToEnd(stream.Value(), limit, sizeHint);
	if (bytes.IsError())
		return Err(bytes.Error().message);
	return Ok(std::move(bytes).Unwrap());
}

ArchiveStream MemoryInput(std::span<const uint8_t> input) {
	return OpenMemoryStream(Bytes(input.begin(), input.end())).Unwrap();
}

} // namespace detail::lzma

Result<Bytes, String> LzmaDecompress(std::span<const uint8_t> input,
		const LzmaProperties& props,
		uint64_t unpackSize,
		bool endMarkerAllowed) {
	return detail::lzma::DrainStream(
		OpenLzmaStream(detail::lzma::MemoryInput(input), props, unpackSize, endMarkerAllowed),
		UINT64_MAX,
		unpackSize == UINT64_MAX
			? 0
			: std::min<uint64_t>(unpackSize, uint64_t(input.size()) * 16 + 4096));
}

Bytes LzmaCompress(std::span<const uint8_t> input, const LzmaProperties& props, int level, bool writeEndMarker) {
	detail::lzma::Encoder encoder(props, level, input.size());
	encoder.Feed(input);
	detail::lzma::RangeEncoder rc;
	(void)encoder.EncodeRange(rc, 0, input.size(), SIZE_MAX);
	if (writeEndMarker)
		encoder.EncodeEndMarker(rc, input.size());
	rc.Flush();
	return rc.Take();
}

namespace detail::lzma {

// ── LzmaEncoderImpl ──────────────────────────────────────────────────────────

Result<bool, ArchiveError> LzmaEncoderImpl::Consume(const uint8_t* data, size_t size) {
	m_encoder.Feed(std::span<const uint8_t>(data, size));
	const size_t end = m_encoder.End();
	if (end - m_position > size_t(1) << 16) {
		m_position =
			m_encoder.EncodeRange(m_rc, m_position, end - (MATCH_MAX_LEN + 1), SIZE_MAX);
		m_encoder.Slide(m_position > size_t(m_encoder.MaxDistance()) + 1
							? m_position - m_encoder.MaxDistance() - 1
							: 0);
		return Drain();
	}
	return Ok(true);
}

Result<bool, ArchiveError> LzmaEncoderImpl::Finish() {
	m_position = m_encoder.EncodeRange(m_rc, m_position, m_encoder.End(), SIZE_MAX);
	if (m_endMarker)
		m_encoder.EncodeEndMarker(m_rc, m_position);
	m_rc.Flush();
	return Drain();
}

Result<bool, ArchiveError> LzmaEncoderImpl::Drain() {
	Bytes out = m_rc.Take();
	return Emit(out.data(), out.size());
}

} // namespace detail::lzma

Result<ArchiveStream, ArchiveError> OpenLzmaEncoder(ArchiveStream sink, const LzmaProperties& props, int level,
		bool endMarker, uint64_t sizeHint) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::lzma::LzmaEncoderImpl>(std::move(sink), props, level,
																	  endMarker, sizeHint, state),
					  state);
}

Result<ArchiveStream, ArchiveError> OpenLzmaAloneStream(ArchiveStream input) {
	uint8_t header[13];
	auto got = StreamRead(input, header, sizeof(header));
	if (got.IsError())
		return Err(got.Error());
	if (got.Value() != sizeof(header))
		return Err(MakeError(ErrorKind::FORMAT, String(".lzma : en-tête tronqué")));
	auto props = LzmaProperties::Decode(std::span<const uint8_t>(header, 5));
	if (props.IsError())
		return Err(MakeError(ErrorKind::FORMAT, props.Error()));
	uint64_t size = 0;
	for (int i = 0; i < 8; ++i)
		size |= uint64_t(header[5 + i]) << (8 * i);
	if (props.Value().lc > 8 || props.Value().lp > 4 || props.Value().pb > 4)
		return Err(MakeError(ErrorKind::FORMAT, String(".lzma : propriétés hors bornes")));
	// L'en-tête est relu (et sauté) par le décodeur : il peut ainsi repartir du
	// début.
	if (input.io.Seek(-13, SDL_IO_SEEK_CUR) < 0)
		return Err(MakeError(ErrorKind::IO, String(".lzma : flux non repositionnable")));
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::lzma::LzmaStreamImpl>(
						  std::move(input), props.Value(), size, true, 13, state),
					  state);
}

Result<Bytes, String> LzmaAloneDecompress(std::span<const uint8_t> input) {
	return detail::lzma::DrainStream(OpenLzmaAloneStream(detail::lzma::MemoryInput(input)));
}

Result<Bytes, String> LzmaAloneCompress(std::span<const uint8_t> input, int level, uint32_t dictionarySize) {
	LzmaProperties props;
	props.dictionarySize = dictionarySize;
	auto stream = CreateMemoryWriter();
	if (stream.IsError())
		return Err(stream.Error());
	BinaryWriter writer(stream.Value());
	writer.Write(props.Encode());
	writer.U64Le(uint64_t(input.size()));
	writer.Write(LzmaCompress(input, props, level, false));
	if (!writer.Ok())
		return Err(String(".lzma : écriture impossible"));
	return Ok(stream.Value().DynamicMemoryBytes());
}

Result<ArchiveStream, ArchiveError> OpenLzma2Stream(ArchiveStream input, uint32_t dictionarySize, Option<uint64_t> size) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::lzma::Lzma2StreamImpl>(std::move(input),
																	  dictionarySize, size, state),
					  state);
}

Result<Bytes, String> Lzma2Decompress(std::span<const uint8_t> input,
		uint32_t dictionarySize,
		size_t* consumed,
		uint64_t sizeLimit) {
	auto stream = ViewStream(input);
	if (stream.IsError())
		return Err(stream.Error());
	InputBuffer buffer(stream.Value());
	detail::lzma::Lzma2Decoder decoder;
	decoder.Reset(dictionarySize);
	Bytes out;
	uint8_t chunk[1 << 15];
	for (;;) {
		auto produced = decoder.Decode(buffer, chunk, sizeof(chunk));
		if (produced.IsError())
			return Err(produced.Error());
		if (produced.Value() == 0)
			break;
		if (out.size() + produced.Value() > sizeLimit)
			return Err(String("LZMA2 : données plus longues que la taille annoncée"));
		out.insert(out.end(), chunk, chunk + produced.Value());
	}
	if (consumed)
		*consumed = size_t(buffer.Consumed());
	return Ok(std::move(out));
}

namespace detail::lzma {

// ── Lzma2ChunkWriter ─────────────────────────────────────────────────────────

void Lzma2ChunkWriter::Encode(bool final, Bytes& out) {
	for (;;) {
		const size_t available = m_encoder.End() - m_position;
		if (available == 0 || (!final && available < CHUNK_UNPACKED_MAX + MATCH_MAX_LEN + 1))
			break;
		m_encoder.ResetState();
		RangeEncoder rc;
		const size_t end = std::min(m_encoder.End(), m_position + CHUNK_UNPACKED_MAX);
		const size_t reached =
			m_encoder.EncodeRange(rc, m_position, end, CHUNK_PACKED_MAX - 16);
		rc.Flush();
		Bytes packed = rc.Take();
		const size_t unpacked = reached - m_position;
		if (packed.size() < unpacked && packed.size() <= CHUNK_PACKED_MAX) {
			const uint8_t control = m_first ? 0xE0 : (m_needProperties ? 0xC0 : 0xA0);
			out.push_back(uint8_t(control | ((unpacked - 1) >> 16)));
			out.push_back(uint8_t((unpacked - 1) >> 8));
			out.push_back(uint8_t(unpacked - 1));
			out.push_back(uint8_t((packed.size() - 1) >> 8));
			out.push_back(uint8_t(packed.size() - 1));
			if (control >= 0xC0)
				out.push_back(m_props.PropertiesByte());
			out.insert(out.end(), packed.begin(), packed.end());
			m_needProperties = false;
		} else {
			for (size_t at = m_position; at < reached;) {
				const size_t size = std::min<size_t>(CHUNK_PACKED_MAX, reached - at);
				out.push_back(m_first ? 0x01 : 0x02);
				out.push_back(uint8_t((size - 1) >> 8));
				out.push_back(uint8_t(size - 1));
				for (size_t k = 0; k < size; ++k)
					out.push_back(m_encoder.At(at + k));
				if (m_first)
					m_needProperties = true; // un morceau 0x01 réinitialise le dictionnaire
				m_first = false;
				at += size;
			}
		}
		m_first = false;
		m_position = reached;
		const size_t history = size_t(m_encoder.MaxDistance()) + 1;
		m_encoder.Slide(m_position > history ? m_position - history : 0);
	}
}

void Lzma2ChunkWriter::Finish(Bytes& out) {
	Encode(true, out);
	out.push_back(0x00);
}

LzmaProperties Lzma2ChunkWriter::MakeProperties(uint32_t dictionarySize) {
	LzmaProperties props;
	props.dictionarySize = Lzma2DictionarySize(Lzma2DictionaryByte(dictionarySize));
	return props;
}

// ── Lzma2EncoderImpl ─────────────────────────────────────────────────────────

Result<bool, ArchiveError> Lzma2EncoderImpl::Consume(const uint8_t* data, size_t size) {
	m_chunks.Feed(std::span<const uint8_t>(data, size));
	Bytes out;
	m_chunks.Encode(false, out);
	return Emit(out.data(), out.size());
}

Result<bool, ArchiveError> Lzma2EncoderImpl::Finish() {
	Bytes out;
	m_chunks.Finish(out);
	return Emit(out.data(), out.size());
}

} // namespace detail::lzma

Bytes Lzma2Compress(std::span<const uint8_t> input, uint32_t dictionarySize, int level) {
	detail::lzma::Lzma2ChunkWriter chunks(dictionarySize, level, input.size());
	chunks.Feed(input);
	Bytes out;
	chunks.Finish(out);
	return out;
}

Result<ArchiveStream, ArchiveError> OpenLzma2Encoder(ArchiveStream sink, uint32_t dictionarySize, int level,
		uint64_t sizeHint) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::lzma::Lzma2EncoderImpl>(
						  std::move(sink), dictionarySize, level, sizeHint, state),
					  state);
}

namespace detail::xz {

uint64_t ReadVarint(BinaryReader& reader) {
	uint64_t value = 0;
	for (int i = 0; i < 9; ++i) {
		uint8_t byte = reader.U8();
		value |= uint64_t(byte & 0x7F) << (7 * i);
		if ((byte & 0x80) == 0)
			return value;
	}
	reader.Fail();
	return 0;
}

void WriteVarint(BinaryWriter& writer, uint64_t value) {
	while (value >= 0x80) {
		writer.U8(uint8_t(value | 0x80));
		value >>= 7;
	}
	writer.U8(uint8_t(value));
}

size_t CheckSize(uint8_t check) noexcept {
	switch (check) {
	case 0x00:
		return 0;
	case 0x01:
		return 4;
	case 0x04:
		return 8;
	case 0x0A:
		return 32;
	default:
		return check == 0 ? 0 : size_t(4) << ((check - 1) / 3);
	}
}

} // namespace detail::xz

namespace detail::xz {

// ── XzStreamImpl ─────────────────────────────────────────────────────────────

Result<size_t, ArchiveError> XzStreamImpl::Produce(uint8_t* out, size_t max) {
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
		case Mode::STREAM_HEADER:
			error = ReadStreamHeader();
			break;
		case Mode::BLOCK_HEADER:
			error = ReadBlockHeader();
			break;
		case Mode::BLOCK_DATA:
			error = DecodeBlockData();
			break;
		case Mode::INDEX:
			error = ReadIndexAndFooter();
			break;
		}
		if (error.IsSome()) {
			if (m_buffer.Failed())
				return Err(m_buffer.Failure("xz"));
			return Err(MakeError(m_sawStream ? ErrorKind::CORRUPT : ErrorKind::FORMAT,
								 error.Unwrap()));
		}
	}
}

bool XzStreamImpl::Restart() {
	if (!m_buffer.Rewind())
		return false;
	m_mode = Mode::STREAM_HEADER;
	m_sawStream = false;
	m_total = 0;
	m_ready.clear();
	m_readyAt = 0;
	m_records.clear();
	return true;
}

Option<String> XzStreamImpl::ReadStreamHeader() {
	uint8_t header[12];
	// Remplissage entre flux : groupes de 4 octets nuls.
	for (;;) {
		const size_t got = m_buffer.ReadRaw(header, 4);
		if (got == 0 && m_sawStream) {
			m_mode = Mode::DONE;
			return NONE;
		}
		if (got != 4)
			return Some(String(m_sawStream ? "xz : données inattendues après le flux"
										   : "xz : signature absente"));
		if (m_sawStream && header[0] == 0 && header[1] == 0 && header[2] == 0 && header[3] == 0)
			continue;
		break;
	}
	if (!ReadExact(header + 4, 8) || std::memcmp(header, STREAM_MAGIC, 6) != 0)
		return Some(String(m_sawStream ? "xz : données inattendues après le flux"
									   : "xz : signature absente"));
	const uint32_t headerCrc = uint32_t(header[8]) | (uint32_t(header[9]) << 8) |
							   (uint32_t(header[10]) << 16) | (uint32_t(header[11]) << 24);
	if (Crc32(std::span<const uint8_t>(header + 6, 2)) != headerCrc || header[6] != 0 ||
		(header[7] & 0xF0) != 0)
		return Some(String("xz : en-tête de flux invalide"));
	m_flags[0] = header[6];
	m_flags[1] = header[7];
	m_check = header[7] & 0x0F;
	m_records.clear();
	m_sawStream = true;
	m_mode = Mode::BLOCK_HEADER;
	return NONE;
}

Option<String> XzStreamImpl::ReadBlockHeader() {
	uint8_t sizeByte = 0;
	if (!m_buffer.Byte(sizeByte))
		return Some(String("xz : bloc tronqué"));
	if (sizeByte == 0) {
		m_mode = Mode::INDEX;
		return NONE;
	}
	const size_t headerSize = (size_t(sizeByte) + 1) * 4;
	Bytes header(headerSize);
	header[0] = sizeByte;
	if (!ReadExact(header.data() + 1, headerSize - 1))
		return Some(String("xz : en-tête de bloc tronqué"));
	const uint32_t storedCrc =
		uint32_t(header[headerSize - 4]) | (uint32_t(header[headerSize - 3]) << 8) |
		(uint32_t(header[headerSize - 2]) << 16) | (uint32_t(header[headerSize - 1]) << 24);
	if (Crc32(std::span<const uint8_t>(header).subspan(0, headerSize - 4)) != storedCrc)
		return Some(String("xz : CRC de l'en-tête de bloc incorrect"));
	auto headerStream = ViewStream(std::span<const uint8_t>(header).subspan(1, headerSize - 5));
	if (headerStream.IsError())
		return Some(String("xz : flux mémoire impossible"));
	BinaryReader fields(headerStream.Value());
	const uint8_t blockFlags = fields.U8();
	if (blockFlags & 0x3C)
		return Some(String("xz : drapeaux de bloc réservés"));
	m_blockCompressed = (blockFlags & 0x40) ? Some(ReadVarint(fields)) : Option<uint64_t>(NONE);
	m_blockUncompressed =
		(blockFlags & 0x80) ? Some(ReadVarint(fields)) : Option<uint64_t>(NONE);
	const int filterCount = (blockFlags & 0x03) + 1;
	m_stages.clear();
	uint32_t dictionarySize = 0;
	for (int f = 0; f < filterCount; ++f) {
		const uint64_t id = ReadVarint(fields);
		const uint64_t propertiesSize = ReadVarint(fields);
		if (!fields.Ok() || propertiesSize > fields.Remaining())
			return Some(String("xz : filtre mal formé"));
		Bytes properties = fields.ReadBytes(propertiesSize);
		const bool last = f == filterCount - 1;
		if (id == 0x21) {
			if (!last || propertiesSize != 1 || properties[0] > 40)
				return Some(String("xz : propriétés LZMA2 invalides"));
			dictionarySize = Lzma2DictionarySize(properties[0]);
			continue;
		}
		if (last)
			return Some(String("xz : le dernier filtre doit être LZMA2"));
		FilterSpec spec;
		if (id == 0x03) {
			if (propertiesSize != 1)
				return Some(String("xz : propriétés delta invalides"));
			spec.type = FilterSpec::Type::DELTA;
			spec.deltaDistance = size_t(properties[0]) + 1;
		} else if (id >= 0x04 && id <= 0x0A) {
			static constexpr BranchKind KINDS[7] = {
				BranchKind::X86,	   BranchKind::POWERPC, BranchKind::IA64, BranchKind::ARM,
				BranchKind::ARM_THUMB, BranchKind::SPARC,	BranchKind::ARM64};
			spec.branch = KINDS[id - 0x04];
			if (propertiesSize == 4)
				spec.startOffset = uint32_t(properties[0]) | (uint32_t(properties[1]) << 8) |
								   (uint32_t(properties[2]) << 16) |
								   (uint32_t(properties[3]) << 24);
			else if (propertiesSize != 0)
				return Some(String("xz : propriétés de filtre invalides"));
		} else {
			return Some(String::Format("xz : filtre 0x%llX non géré",
									   static_cast<unsigned long long>(id)));
		}
		m_stages.emplace_back(spec, false);
	}
	if (!fields.Ok() || dictionarySize == 0)
		return Some(String("xz : en-tête de bloc invalide"));
	for (size_t i = 0; i < fields.Remaining(); ++i)
		if (fields.U8() != 0)
			return Some(String("xz : remplissage d'en-tête de bloc non nul"));
	m_blockHeaderSize = headerSize;
	m_blockStart = m_buffer.Consumed();
	m_blockOut = 0;
	m_lzma2.Reset(dictionarySize);
	if (m_blockUncompressed.IsSome())
		m_lzma2.LimitDictionary(m_blockUncompressed.Value());
	m_crc32 = 0;
	m_crc64 = 0;
	m_sha = Sha256();
	m_mode = Mode::BLOCK_DATA;
	return NONE;
}

Option<String> XzStreamImpl::DecodeBlockData() {
	uint8_t chunk[1 << 15];
	auto produced = m_lzma2.Decode(m_buffer, chunk, sizeof(chunk));
	if (produced.IsError())
		return Some(String::Format("xz : %s", produced.Error().CStr()));
	const bool finished = produced.Value() == 0;
	// Défiltrage : du dernier filtre (le plus proche de LZMA2) au premier.
	Bytes data(chunk, chunk + produced.Value());
	for (size_t i = m_stages.size(); i-- > 0;) {
		Bytes next;
		m_stages[i].Push(data, next, finished);
		data = std::move(next);
	}
	m_blockOut += data.size();
	m_total += data.size();
	if (m_total > m_sizeLimit)
		return Some(String("xz : taille décompressée au-delà de la limite"));
	if (m_blockUncompressed.IsSome() && m_blockOut > m_blockUncompressed.Value())
		return Some(String("xz : taille décompressée du bloc incohérente"));
	UpdateCheck(data);
	m_ready = std::move(data);
	if (finished)
		return FinishBlock();
	return NONE;
}

void XzStreamImpl::UpdateCheck(std::span<const uint8_t> data) {
	if (m_check == 0x01)
		m_crc32 = Crc32(data, m_crc32);
	else if (m_check == 0x04)
		m_crc64 = Crc64Xz(data, m_crc64);
	else if (m_check == 0x0A)
		m_sha.Update(data);
}

Option<String> XzStreamImpl::FinishBlock() {
	const uint64_t compressed = m_buffer.Consumed() - m_blockStart;
	if (m_blockCompressed.IsSome() && compressed != m_blockCompressed.Value())
		return Some(String("xz : taille compressée du bloc incohérente"));
	if (m_blockUncompressed.IsSome() && m_blockOut != m_blockUncompressed.Value())
		return Some(String("xz : taille décompressée du bloc incohérente"));
	for (uint64_t padding = compressed; padding % 4 != 0; ++padding) {
		uint8_t zero = 0;
		if (!m_buffer.Byte(zero) || zero != 0)
			return Some(String("xz : remplissage de bloc non nul"));
	}
	const size_t checkSize = CheckSize(m_check);
	uint8_t checkValue[64];
	if (!ReadExact(checkValue, checkSize))
		return Some(String("xz : contrôle de bloc tronqué"));
	if (m_check == 0x01) {
		for (int i = 0; i < 4; ++i)
			if (checkValue[i] != uint8_t(m_crc32 >> (8 * i)))
				return Some(String("xz : CRC-32 des données incorrect"));
	} else if (m_check == 0x04) {
		for (int i = 0; i < 8; ++i)
			if (checkValue[i] != uint8_t(m_crc64 >> (8 * i)))
				return Some(String("xz : CRC-64 des données incorrect"));
	} else if (m_check == 0x0A) {
		const auto digest = m_sha.Final();
		if (std::memcmp(digest.data(), checkValue, 32) != 0)
			return Some(String("xz : SHA-256 des données incorrect"));
	}
	m_records.push_back(Record{m_blockHeaderSize + compressed + checkSize, m_blockOut});
	m_mode = Mode::BLOCK_HEADER;
	return NONE;
}

Option<String> XzStreamImpl::ReadIndexAndFooter() {
	// L'indicateur 0x00 est déjà lu : il entre dans le CRC de l'index.
	const uint8_t indicator = 0;
	uint32_t crc = Crc32(std::span<const uint8_t>(&indicator, 1));
	uint64_t length = 1;
	auto byte = [&](uint8_t& value) {
		if (!m_buffer.Byte(value))
			return false;
		crc = Crc32(std::span<const uint8_t>(&value, 1), crc);
		++length;
		return true;
	};
	auto varint = [&](uint64_t& value) {
		value = 0;
		for (int i = 0; i < 9; ++i) {
			uint8_t b = 0;
			if (!byte(b))
				return false;
			value |= uint64_t(b & 0x7F) << (7 * i);
			if ((b & 0x80) == 0)
				return true;
		}
		return false;
	};
	uint64_t count = 0;
	if (!varint(count) || count != m_records.size())
		return Some(String("xz : nombre d'enregistrements d'index incohérent"));
	for (const Record& record : m_records) {
		uint64_t unpadded = 0, uncompressed = 0;
		if (!varint(unpadded) || !varint(uncompressed))
			return Some(String("xz : index tronqué"));
		if (unpadded != record.unpadded || uncompressed != record.uncompressed)
			return Some(String("xz : index incohérent avec les blocs"));
	}
	while (length % 4 != 0) {
		uint8_t zero = 0;
		if (!byte(zero) || zero != 0)
			return Some(String("xz : remplissage d'index non nul"));
	}
	uint8_t tail[16];
	if (!ReadExact(tail, 16))
		return Some(String("xz : index tronqué"));
	const uint32_t indexCrc = uint32_t(tail[0]) | (uint32_t(tail[1]) << 8) |
							  (uint32_t(tail[2]) << 16) | (uint32_t(tail[3]) << 24);
	if (indexCrc != crc)
		return Some(String("xz : CRC de l'index incorrect"));
	const uint32_t footerCrc = uint32_t(tail[4]) | (uint32_t(tail[5]) << 8) |
							   (uint32_t(tail[6]) << 16) | (uint32_t(tail[7]) << 24);
	const uint32_t backwardSize = uint32_t(tail[8]) | (uint32_t(tail[9]) << 8) |
								  (uint32_t(tail[10]) << 16) | (uint32_t(tail[11]) << 24);
	if (tail[14] != 'Y' || tail[15] != 'Z' ||
		Crc32(std::span<const uint8_t>(tail + 8, 6)) != footerCrc || tail[12] != m_flags[0] ||
		tail[13] != m_flags[1] || (uint64_t(backwardSize) + 1) * 4 != length + 4)
		return Some(String("xz : pied de flux invalide"));
	m_mode = Mode::STREAM_HEADER;
	return NONE;
}

} // namespace detail::xz

Result<ArchiveStream, ArchiveError> OpenXzStream(ArchiveStream input, uint64_t sizeLimit) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(
		std::make_unique<detail::xz::XzStreamImpl>(std::move(input), sizeLimit, state), state);
}

Result<Bytes, String> XzDecompress(std::span<const uint8_t> input, uint64_t sizeLimit) {
	return detail::lzma::DrainStream(OpenXzStream(detail::lzma::MemoryInput(input), sizeLimit));
}

namespace detail::xz {

// ── XzEncoderImpl ────────────────────────────────────────────────────────────

Result<bool, ArchiveError> XzEncoderImpl::Consume(const uint8_t* data, size_t size) {
	if (auto header = WriteStreamHeader(); header.IsError())
		return header;
	if (size == 0)
		return Ok(true);
	if (auto block = WriteBlockHeader(); block.IsError())
		return block;
	const std::span<const uint8_t> bytes(data, size);
	UpdateCheck(bytes);
	m_uncompressed += size;
	m_chunks.Feed(bytes);
	Bytes out;
	m_chunks.Encode(false, out);
	m_compressed += out.size();
	return Emit(out.data(), out.size());
}

Result<bool, ArchiveError> XzEncoderImpl::Finish() {
	if (auto header = WriteStreamHeader(); header.IsError())
		return header;
	Bytes out;
	std::vector<std::pair<uint64_t, uint64_t>> records;
	if (m_blockStarted) {
		m_chunks.Finish(out);
		m_compressed += out.size();
		const uint64_t unpaddedCompressed = m_compressed;
		for (uint64_t padded = m_compressed; padded % 4 != 0; ++padded)
			out.push_back(0); // remplissage de bloc (hors taille compressée)
		// Contrôle.
		switch (m_check) {
		case XzCheck::NONE:
			break;
		case XzCheck::CRC32:
			for (int i = 0; i < 4; ++i)
				out.push_back(uint8_t(m_crc32 >> (8 * i)));
			break;
		case XzCheck::CRC64:
			for (int i = 0; i < 8; ++i)
				out.push_back(uint8_t(m_crc64 >> (8 * i)));
			break;
		case XzCheck::SHA256: {
			const auto digest = m_sha.Final();
			out.insert(out.end(), digest.begin(), digest.end());
			break;
		}
		}
		records.emplace_back(m_blockHeaderSize + unpaddedCompressed +
								 CheckSize(uint8_t(m_check)),
							 m_uncompressed);
	}
	// Index : indicateur, nombre, enregistrements, remplissage, CRC-32.
	auto indexStream = CreateMemoryWriter();
	if (indexStream.IsError())
		return Err(MakeError(ErrorKind::IO, indexStream.Error()));
	BinaryWriter index(indexStream.Value());
	index.U8(0x00);
	WriteVarint(index, records.size());
	for (const auto& [unpadded, uncompressed] : records) {
		WriteVarint(index, unpadded);
		WriteVarint(index, uncompressed);
	}
	index.AlignTo(4);
	const Bytes indexBytes = indexStream.Value().DynamicMemoryBytes();
	out.insert(out.end(), indexBytes.begin(), indexBytes.end());
	const uint32_t indexCrc = Crc32(indexBytes);
	for (int i = 0; i < 4; ++i)
		out.push_back(uint8_t(indexCrc >> (8 * i)));
	// Pied : CRC-32 de (taille arrière, drapeaux), ces champs, « YZ ».
	const uint32_t backwardSize = uint32_t((indexBytes.size() + 4) / 4 - 1);
	const uint8_t fields[6] = {uint8_t(backwardSize),
							   uint8_t(backwardSize >> 8),
							   uint8_t(backwardSize >> 16),
							   uint8_t(backwardSize >> 24),
							   0x00,
							   uint8_t(m_check)};
	const uint32_t footerCrc = Crc32(fields);
	for (int i = 0; i < 4; ++i)
		out.push_back(uint8_t(footerCrc >> (8 * i)));
	out.insert(out.end(), fields, fields + 6);
	out.push_back('Y');
	out.push_back('Z');
	return Emit(out.data(), out.size());
}

Result<bool, ArchiveError> XzEncoderImpl::WriteStreamHeader() {
	if (m_headerWritten)
		return Ok(true);
	m_headerWritten = true;
	Bytes header(STREAM_MAGIC, STREAM_MAGIC + 6);
	const uint8_t flags[2] = {0x00, uint8_t(m_check)};
	header.push_back(flags[0]);
	header.push_back(flags[1]);
	const uint32_t crc = Crc32(flags);
	for (int i = 0; i < 4; ++i)
		header.push_back(uint8_t(crc >> (8 * i)));
	return Emit(header.data(), header.size());
}

Result<bool, ArchiveError> XzEncoderImpl::WriteBlockHeader() {
	if (m_blockStarted)
		return Ok(true);
	m_blockStarted = true;
	// Taille (en multiples de 4, moins 1), drapeaux : 1 filtre, sans tailles.
	Bytes header = {0x00, 0x00, 0x21, 0x01, m_dictionaryByte};
	while ((header.size() + 4) % 4 != 0)
		header.push_back(0);
	header[0] = uint8_t((header.size() + 4) / 4 - 1);
	const uint32_t crc = Crc32(header);
	for (int i = 0; i < 4; ++i)
		header.push_back(uint8_t(crc >> (8 * i)));
	m_blockHeaderSize = header.size();
	return Emit(header.data(), header.size());
}

void XzEncoderImpl::UpdateCheck(std::span<const uint8_t> data) {
	if (m_check == XzCheck::CRC32)
		m_crc32 = Crc32(data, m_crc32);
	else if (m_check == XzCheck::CRC64)
		m_crc64 = Crc64Xz(data, m_crc64);
	else if (m_check == XzCheck::SHA256)
		m_sha.Update(data);
}

} // namespace detail::xz

Result<ArchiveStream, ArchiveError> OpenXzEncoder(ArchiveStream sink, int level, uint32_t dictionarySize,
		XzCheck check, uint64_t sizeHint) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::xz::XzEncoderImpl>(
						  std::move(sink), level, dictionarySize, check, sizeHint, state),
					  state);
}

Result<Bytes, String> XzCompress(std::span<const uint8_t> input, int level, uint32_t dictionarySize, XzCheck check) {
	auto target = std::make_shared<Bytes>();
	auto encoder =
		OpenXzEncoder(OpenMemorySink(target).Unwrap(), level, dictionarySize, check, input.size());
	if (encoder.IsError())
		return Err(encoder.Error().message);
	if (auto written = StreamWrite(encoder.Value(), input); written.IsError())
		return Err(written.Error().message);
	if (auto finished = FinishStream(encoder.Value()); finished.IsError())
		return Err(finished.Error().message);
	return Ok(std::move(*target));
}

} // namespace data::archive
