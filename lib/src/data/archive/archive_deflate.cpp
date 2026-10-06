// Définitions de data/archive/archive_deflate.hpp
#include "data/archive/archive_deflate.hpp"

namespace data::archive {

namespace detail::deflate {

// ── BitReader ────────────────────────────────────────────────────────────────

void BitReader::Refill(int count) {
	while (m_bitCount < count && m_bitCount <= 56) {
		uint8_t byte = 0;
		if (!m_input->Byte(byte))
			return;
		m_buffer |= uint64_t(byte) << m_bitCount;
		m_bitCount += 8;
	}
}

bool BitReader::Take(int count, uint32_t& out) {
	if (count == 0) {
		out = 0;
		return true;
	}
	Refill(count);
	if (m_bitCount < count)
		return false;
	out = uint32_t(m_buffer & ((uint64_t(1) << count) - 1));
	m_buffer >>= count;
	m_bitCount -= count;
	return true;
}

void BitReader::AlignToByte() noexcept {
	const int drop = m_bitCount % 8;
	m_buffer >>= drop;
	m_bitCount -= drop;
}

bool BitReader::TakeByte(uint8_t& out) {
	if (m_bitCount >= 8) {
		out = uint8_t(m_buffer);
		m_buffer >>= 8;
		m_bitCount -= 8;
		return true;
	}
	return m_input->Byte(out);
}

void BitReader::Consume(int count) noexcept {
	m_buffer >>= count;
	m_bitCount -= count;
}

uint64_t BitReader::Consumed() const noexcept {
	return m_input->Consumed() - uint64_t(m_bitCount / 8);
}

void BitReader::Reset() noexcept {
	m_buffer = 0;
	m_bitCount = 0;
}

// ── Huffman ──────────────────────────────────────────────────────────────────

int Huffman::Build(const uint8_t* lengths, int symbolCount) {
	m_count.fill(0);
	m_symbols.assign(size_t(symbolCount), 0);
	for (int symbol = 0; symbol < symbolCount; ++symbol)
		++m_count[lengths[symbol]];
	if (m_count[0] == symbolCount) {
		m_fast.fill(0);
		return 0; // aucun code : permis (flux sans distance, par exemple)
	}
	int left = 1;
	for (int length = 1; length <= MAX_BITS; ++length) {
		left <<= 1;
		left -= m_count[length];
		if (left < 0)
			return left;
	}
	std::array<uint16_t, MAX_BITS + 1> offsets{};
	for (int length = 1; length < MAX_BITS; ++length)
		offsets[length + 1] = uint16_t(offsets[length] + m_count[length]);
	for (int symbol = 0; symbol < symbolCount; ++symbol)
		if (lengths[symbol] != 0)
			m_symbols[offsets[lengths[symbol]]++] = uint16_t(symbol);

	// Table rapide : pour chaque code de longueur <= FAST_BITS, toutes les
	// entrées dont les bits bas (ordre de lecture) valent le code inversé.
	m_fast.fill(0);
	int code = 0;
	int index = 0;
	for (int length = 1; length <= MAX_BITS; ++length) {
		for (int n = 0; n < m_count[length]; ++n, ++code, ++index) {
			if (length > FAST_BITS)
				continue;
			int reversed = 0;
			for (int bit = 0; bit < length; ++bit)
				reversed |= ((code >> bit) & 1) << (length - 1 - bit);
			uint16_t entry = uint16_t((m_symbols[size_t(index)] << 4) | length);
			for (int fill = reversed; fill < (1 << FAST_BITS); fill += (1 << length))
				m_fast[size_t(fill)] = entry;
		}
		code <<= 1;
	}
	return left;
}

int Huffman::Decode(BitReader& reader) const noexcept {
	reader.Refill(MAX_BITS);
	uint16_t entry = m_fast[size_t(reader.Buffer() & ((1u << FAST_BITS) - 1))];
	int length = entry & 0xF;
	if (length != 0 && length <= reader.BitCount()) {
		reader.Consume(length);
		return entry >> 4;
	}
	// Code long (ou fin de flux) : parcours canonique bit à bit.
	int code = 0;
	int first = 0;
	int index = 0;
	uint64_t bits = reader.Buffer();
	for (length = 1; length <= MAX_BITS && length <= reader.BitCount(); ++length) {
		code |= int((bits >> (length - 1)) & 1u);
		int count = m_count[length];
		if (code - count < first) {
			reader.Consume(length);
			return m_symbols[size_t(index + (code - first))];
		}
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

// ── Inflater ─────────────────────────────────────────────────────────────────

void Inflater::Reset() {
	m_bits.Reset();
	m_mode = Mode::HEADER;
	m_last = false;
	m_total = 0;
	m_copyLength = 0;
	m_storedRemaining = 0;
}

bool Inflater::TakeByteAfterEnd(uint8_t& out) {
	m_bits.AlignToByte();
	return m_bits.TakeByte(out);
}

Result<size_t, String> Inflater::Decode(uint8_t* out, size_t max) {
	size_t produced = 0;
	while (produced < max) {
		if (m_copyLength > 0) {
			produced += CopyMatch(out + produced, max - produced);
			continue;
		}
		switch (m_mode) {
		case Mode::DONE:
			return Ok(produced);
		case Mode::HEADER:
			if (auto error = ReadBlockHeader(); error.IsSome())
				return Err(error.Unwrap());
			break;
		case Mode::STORED: {
			if (m_storedRemaining == 0) {
				m_mode = m_last ? Mode::DONE : Mode::HEADER;
				break;
			}
			uint8_t byte = 0;
			if (!m_bits.TakeByte(byte))
				return Err(String("deflate : bloc stocké tronqué"));
			Emit(out, produced, byte);
			--m_storedRemaining;
			break;
		}
		case Mode::CODES: {
			if (auto error = DecodeSymbol(out, produced); error.IsSome())
				return Err(error.Unwrap());
			break;
		}
		}
	}
	return Ok(produced);
}

void Inflater::Emit(uint8_t* out, size_t& produced, uint8_t byte) {
	m_window[size_t(m_total) & (WINDOW - 1)] = byte;
	++m_total;
	out[produced++] = byte;
}

size_t Inflater::CopyMatch(uint8_t* out, size_t max) {
	size_t produced = 0;
	while (m_copyLength > 0 && produced < max) {
		const uint8_t byte = m_window[size_t(m_total - m_copyDistance) & (WINDOW - 1)];
		Emit(out, produced, byte);
		--m_copyLength;
	}
	return produced;
}

Option<String> Inflater::DecodeSymbol(uint8_t* out, size_t& produced) {
	int symbol = m_literals->Decode(m_bits);
	if (symbol < 0)
		return Some(String("deflate : code littéral invalide ou flux tronqué"));
	if (symbol < 256) {
		Emit(out, produced, uint8_t(symbol));
		return NONE;
	}
	if (symbol == 256) {
		m_mode = m_last ? Mode::DONE : Mode::HEADER;
		return NONE;
	}
	symbol -= 257;
	if (symbol >= 29)
		return Some(String("deflate : symbole de longueur invalide"));
	uint32_t extra = 0;
	const bool longLength = m_deflate64 && symbol == 28; // Deflate64 : 3 + 16 bits
	if (!m_bits.Take(longLength ? 16 : LENGTH_EXTRA[symbol], extra))
		return Some(String("deflate : flux tronqué (longueur)"));
	const size_t length = (longLength ? 3 : size_t(LENGTH_BASE[symbol])) + extra;
	const int distanceSymbol = m_distances->Decode(m_bits);
	if (distanceSymbol < 0 || distanceSymbol >= (m_deflate64 ? 32 : 30))
		return Some(String("deflate : code de distance invalide ou flux tronqué"));
	if (!m_bits.Take(DISTANCE_EXTRA[distanceSymbol], extra))
		return Some(String("deflate : flux tronqué (distance)"));
	const uint64_t distance = uint64_t(DISTANCE_BASE[distanceSymbol]) + extra;
	if (distance > m_total)
		return Some(String("deflate : distance au-delà du début des données"));
	m_copyLength = length;
	m_copyDistance = distance;
	return NONE;
}

Option<String> Inflater::ReadBlockHeader() {
	uint32_t last = 0, type = 0;
	if (!m_bits.Take(1, last) || !m_bits.Take(2, type))
		return Some(String("deflate : en-tête de bloc tronqué"));
	m_last = last != 0;
	if (type == 0) {
		m_bits.AlignToByte();
		uint8_t bytes[4];
		for (uint8_t& byte : bytes)
			if (!m_bits.TakeByte(byte))
				return Some(String("deflate : bloc stocké tronqué"));
		const uint16_t length = uint16_t(bytes[0] | (bytes[1] << 8));
		const uint16_t complement = uint16_t(bytes[2] | (bytes[3] << 8));
		if (uint16_t(~complement) != length)
			return Some(String("deflate : longueur de bloc stocké incohérente"));
		m_storedRemaining = length;
		m_mode = Mode::STORED;
		return NONE;
	}
	if (type == 1) {
		if (!m_fixedBuilt) {
			uint8_t lengths[288];
			for (int i = 0; i < 144; ++i)
				lengths[i] = 8;
			for (int i = 144; i < 256; ++i)
				lengths[i] = 9;
			for (int i = 256; i < 280; ++i)
				lengths[i] = 7;
			for (int i = 280; i < 288; ++i)
				lengths[i] = 8;
			(void)m_fixedLiterals.Build(lengths, 288);
			uint8_t distanceLengths[32];
			std::memset(distanceLengths, 5, sizeof(distanceLengths));
			(void)m_fixedDistances.Build(distanceLengths, 32);
			m_fixedBuilt = true;
		}
		m_literals = &m_fixedLiterals;
		m_distances = &m_fixedDistances;
		m_mode = Mode::CODES;
		return NONE;
	}
	if (type != 2)
		return Some(String("deflate : type de bloc réservé (3)"));

	uint32_t literalCount = 0, distanceCount = 0, codeLengthCount = 0;
	if (!m_bits.Take(5, literalCount) || !m_bits.Take(5, distanceCount) ||
		!m_bits.Take(4, codeLengthCount))
		return Some(String("deflate : en-tête dynamique tronqué"));
	literalCount += 257;
	distanceCount += 1;
	codeLengthCount += 4;
	if (literalCount > 286 || distanceCount > (m_deflate64 ? 32u : 30u))
		return Some(String("deflate : trop de codes dans l'en-tête dynamique"));
	static constexpr uint8_t ORDER[19] = {16, 17, 18, 0, 8,	 7, 9,	6, 10, 5,
										  11, 4,  12, 3, 13, 2, 14, 1, 15};
	uint8_t codeLengths[19] = {};
	for (uint32_t i = 0; i < codeLengthCount; ++i) {
		uint32_t value = 0;
		if (!m_bits.Take(3, value))
			return Some(String("deflate : en-tête dynamique tronqué"));
		codeLengths[ORDER[i]] = uint8_t(value);
	}
	Huffman codeLengthCode;
	if (codeLengthCode.Build(codeLengths, 19) != 0)
		return Some(String("deflate : code des longueurs incomplet"));
	uint8_t lengths[286 + 32] = {};
	uint32_t index = 0;
	while (index < literalCount + distanceCount) {
		const int symbol = codeLengthCode.Decode(m_bits);
		if (symbol < 0)
			return Some(String("deflate : longueur de code invalide"));
		if (symbol < 16) {
			lengths[index++] = uint8_t(symbol);
			continue;
		}
		uint8_t value = 0;
		uint32_t repeat = 0;
		if (symbol == 16) {
			if (index == 0)
				return Some(String("deflate : répétition sans longueur précédente"));
			value = lengths[index - 1];
			if (!m_bits.Take(2, repeat))
				return Some(String("deflate : flux tronqué"));
			repeat += 3;
		} else if (symbol == 17) {
			if (!m_bits.Take(3, repeat))
				return Some(String("deflate : flux tronqué"));
			repeat += 3;
		} else {
			if (!m_bits.Take(7, repeat))
				return Some(String("deflate : flux tronqué"));
			repeat += 11;
		}
		if (index + repeat > literalCount + distanceCount)
			return Some(String("deflate : trop de longueurs de code"));
		while (repeat-- > 0)
			lengths[index++] = value;
	}
	if (lengths[256] == 0)
		return Some(String("deflate : code de fin de bloc absent"));
	// Un code incomplet n'est admis que s'il se réduit à UN code de
	// longueur 1 (règle de zlib/puff) ; sur-souscrit : toujours invalide.
	auto acceptable = [](const Huffman& code, int missing, uint32_t symbolCount) {
		return missing == 0 ||
			   (missing > 0 && int(symbolCount) == code.CodeCount(0) + code.CodeCount(1));
	};
	if (!acceptable(m_dynamicLiterals, m_dynamicLiterals.Build(lengths, int(literalCount)),
					literalCount))
		return Some(String("deflate : code des littéraux invalide"));
	if (!acceptable(m_dynamicDistances,
					m_dynamicDistances.Build(lengths + literalCount, int(distanceCount)),
					distanceCount))
		return Some(String("deflate : code des distances invalide"));
	m_literals = &m_dynamicLiterals;
	m_distances = &m_dynamicDistances;
	m_mode = Mode::CODES;
	return NONE;
}

// ── InflateStreamImpl ────────────────────────────────────────────────────────

Result<size_t, ArchiveError> InflateStreamImpl::Produce(uint8_t* out, size_t max) {
	auto produced = m_inflater.Decode(out, max);
	if (produced.IsError()) {
		if (m_buffer.Failed())
			return Err(m_buffer.Failure("deflate"));
		return Err(MakeError(ErrorKind::CORRUPT, produced.Error()));
	}
	return Ok(produced.Value());
}

bool InflateStreamImpl::Restart() {
	if (!m_buffer.Rewind())
		return false;
	m_inflater.Reset();
	return true;
}

} // namespace detail::deflate

Result<ArchiveStream, ArchiveError> OpenInflateStream(ArchiveStream input, bool deflate64, Option<uint64_t> size) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::deflate::InflateStreamImpl>(std::move(input),
																		   deflate64, size, state),
					  state);
}

Result<InflateResult, String> Inflate(std::span<const uint8_t> input, size_t expectedSize, bool deflate64) {
	auto stream = ViewStream(input);
	if (stream.IsError())
		return Err(stream.Error());
	InputBuffer buffer(stream.Value());
	detail::deflate::Inflater inflater(buffer, deflate64);
	InflateResult result;
	const size_t limit = expectedSize > 0 ? expectedSize : size_t(-1);
	if (expectedSize > 0)
		result.bytes.reserve(
			std::min(expectedSize, input.size() * 16 + 4096)); // taille annoncée non fiable
	uint8_t chunk[1 << 15];
	for (;;) {
		auto produced = inflater.Decode(chunk, sizeof(chunk));
		if (produced.IsError())
			return Err(produced.Error());
		if (produced.Value() == 0)
			break;
		if (produced.Value() > limit - result.bytes.size())
			return Err(String("deflate : données plus longues que la taille annoncée"));
		result.bytes.insert(result.bytes.end(), chunk, chunk + produced.Value());
	}
	result.consumed = size_t(inflater.Consumed());
	return Ok(std::move(result));
}

namespace detail::deflate {

// ── BitWriter ────────────────────────────────────────────────────────────────

void BitWriter::Bits(uint32_t value, int count) {
	m_buffer |= uint64_t(value) << m_count;
	m_count += count;
	while (m_count >= 8) {
		m_out.push_back(uint8_t(m_buffer));
		m_buffer >>= 8;
		m_count -= 8;
	}
}

void BitWriter::AlignToByte() {
	if (m_count > 0)
		Bits(0, 8 - m_count);
}

void BitWriter::Bytes(std::span<const uint8_t> bytes) {
	AlignToByte();
	m_out.insert(m_out.end(), bytes.begin(), bytes.end());
}

archive::Bytes BitWriter::Take() {
	AlignToByte();
	return std::move(m_out);
}

archive::Bytes BitWriter::TakeComplete() {
	archive::Bytes out = std::move(m_out);
	m_out.clear();
	return out;
}

void BuildLengths(std::span<const uint32_t> frequencies, int maxBits, std::span<uint8_t> lengths) {
	std::fill(lengths.begin(), lengths.end(), uint8_t(0));
	struct Leaf {
		uint32_t frequency;
		uint16_t symbol;
	};
	std::vector<Leaf> leaves;
	for (size_t s = 0; s < frequencies.size(); ++s)
		if (frequencies[s] != 0)
			leaves.push_back(Leaf{frequencies[s], uint16_t(s)});
	if (leaves.empty())
		return;
	if (leaves.size() == 1) {
		lengths[leaves[0].symbol] = 1;
		return;
	}
	std::sort(leaves.begin(), leaves.end(), [](const Leaf& a, const Leaf& b) {
		return a.frequency != b.frequency ? a.frequency < b.frequency : a.symbol < b.symbol;
	});

	// Nœuds : feuilles [0, n), internes [n, 2n-1) ; `parent` pour les
	// profondeurs.
	const size_t n = leaves.size();
	std::vector<uint64_t> weight(2 * n - 1);
	std::vector<size_t> parent(2 * n - 1, 0);
	for (size_t i = 0; i < n; ++i)
		weight[i] = leaves[i].frequency;
	size_t nextLeaf = 0, nextInternal = n, created = n;
	auto takeSmallest = [&]() {
		if (nextLeaf < n && (nextInternal >= created || weight[nextLeaf] <= weight[nextInternal]))
			return nextLeaf++;
		return nextInternal++;
	};
	while (created < 2 * n - 1) {
		size_t a = takeSmallest();
		size_t b = takeSmallest();
		weight[created] = weight[a] + weight[b];
		parent[a] = created;
		parent[b] = created;
		++created;
	}
	std::vector<int> depth(2 * n - 1, 0);
	for (size_t i = 2 * n - 1; i-- > 0;)
		if (i != 2 * n - 2)
			depth[i] = depth[parent[i]] + 1;

	std::array<uint32_t, 64> count{};
	for (size_t i = 0; i < n; ++i)
		++count[size_t(std::min(depth[i], 63))];
	for (int length = maxBits + 1; length < 64; ++length) {
		count[size_t(maxBits)] += count[size_t(length)];
		count[size_t(length)] = 0;
	}
	uint64_t total = 0;
	for (int length = maxBits; length > 0; --length)
		total += uint64_t(count[size_t(length)]) << (maxBits - length);
	while (total != (uint64_t(1) << maxBits)) {
		--count[size_t(maxBits)];
		for (int length = maxBits - 1; length > 0; --length) {
			if (count[size_t(length)] != 0) {
				--count[size_t(length)];
				count[size_t(length + 1)] += 2;
				break;
			}
		}
		--total;
	}
	// Les moins fréquents reçoivent les codes les plus longs.
	size_t leaf = 0;
	for (int length = maxBits; length > 0; --length)
		for (uint32_t k = 0; k < count[size_t(length)]; ++k)
			lengths[leaves[leaf++].symbol] = uint8_t(length);
}

void BuildCodes(std::span<const uint8_t> lengths, std::span<uint16_t> codes) {
	std::array<uint16_t, 16> count{}, next{};
	for (uint8_t length : lengths)
		++count[length];
	count[0] = 0;
	uint16_t code = 0;
	for (int bits = 1; bits < 16; ++bits) {
		code = uint16_t((code + count[size_t(bits - 1)]) << 1);
		next[size_t(bits)] = code;
	}
	for (size_t s = 0; s < lengths.size(); ++s) {
		int length = lengths[s];
		if (length == 0) {
			codes[s] = 0;
			continue;
		}
		uint16_t value = next[size_t(length)]++;
		uint16_t reversed = 0;
		for (int bit = 0; bit < length; ++bit)
			reversed = uint16_t(reversed | (((value >> bit) & 1) << (length - 1 - bit)));
		codes[s] = reversed;
	}
}

int LengthSymbol(int length) noexcept {
	int symbol = 28;
	while (LENGTH_BASE[symbol] > length)
		--symbol;
	return symbol;
}

int DistanceSymbol(int distance) noexcept {
	int symbol = 29;
	while (DISTANCE_BASE[symbol] > distance)
		--symbol;
	return symbol;
}

LevelParameters ParametersFor(int level) noexcept {
	static constexpr LevelParameters TABLE[10] = {
		{0, 0, false},	{4, 8, false},	  {8, 16, false},	{16, 32, false},   {16, 16, true},
		{32, 32, true}, {128, 128, true}, {256, 258, true}, {1024, 258, true}, {4096, 258, true}};
	return TABLE[level < 0 ? 0 : (level > 9 ? 9 : level)];
}

// ── Compressor ───────────────────────────────────────────────────────────────

Compressor::Compressor(int level) : m_params(ParametersFor(level)) {
	m_head.assign(size_t(1) << HASH_BITS, -1);
	m_previous.assign(WINDOW, -1);
}

void Compressor::Feed(std::span<const uint8_t> data) {
	m_input.insert(m_input.end(), data.begin(), data.end());
}

void Compressor::Compress(bool final) {
	const size_t end = End();
	if (m_params.maxChain == 0) {
		// Niveau 0 : blocs stockés de 65 535 octets au fil de l'eau.
		while (end - m_position >= 65535 || (final && !m_finished)) {
			const size_t chunk = std::min<size_t>(65535, end - m_position);
			const bool last = final && m_position + chunk == end;
			WriteStoredBlocks(m_position, m_position + chunk, last);
			m_position += chunk;
			m_blockStart = m_position;
			if (last)
				m_finished = true;
		}
		Slide();
		return;
	}
	if (final && end == 0 && !m_finished) {
		// Bloc fixe final réduit au code de fin (7 bits à zéro) : 2 octets.
		m_writer.Bits(1, 1);
		m_writer.Bits(1, 2);
		m_writer.Bits(0, 7);
		m_finished = true;
		return;
	}
	const size_t stop = final ? end : (end > LOOKAHEAD ? end - LOOKAHEAD : 0);
	while (m_position < stop) {
		const size_t position = m_position;
		int length = 0, distance = 0;
		FindMatch(position, length, distance);
		if (m_params.lazy && length >= MIN_MATCH && length < m_params.niceLength &&
			position + 1 < end) {
			Insert(position);
			int nextLength = 0, nextDistance = 0;
			FindMatch(position + 1, nextLength, nextDistance);
			if (nextLength > length) {
				// Mieux vaut un littéral ici puis la correspondance suivante.
				m_tokens.push_back(Token{0, At(position)});
				++m_position;
				length = nextLength;
				distance = nextDistance;
			} else {
				m_previousInserted = true;
			}
		}
		if (length >= MIN_MATCH) {
			m_tokens.push_back(Token{uint16_t(length), uint16_t(distance)});
			for (int i = 0; i < length; ++i) {
				if (i == 0 && m_previousInserted) {
					m_previousInserted = false;
					continue;
				}
				Insert(m_position + size_t(i));
			}
			m_previousInserted = false;
			m_position += size_t(length);
		} else {
			if (!m_previousInserted)
				Insert(m_position);
			m_previousInserted = false;
			m_tokens.push_back(Token{0, At(m_position)});
			++m_position;
		}
		if (m_tokens.size() >= BLOCK_TOKENS) {
			FlushBlock(m_blockStart, m_position, false);
			m_blockStart = m_position;
		}
	}
	if (final && !m_finished) {
		FlushBlock(m_blockStart, end, true);
		m_blockStart = end;
		m_finished = true;
	}
	Slide();
}

archive::Bytes Compressor::TakeOutput() {
	return m_finished ? m_writer.Take() : m_writer.TakeComplete();
}

archive::Bytes Compressor::Run(std::span<const uint8_t> input) {
	Feed(input);
	Compress(true);
	return TakeOutput();
}

void Compressor::Slide() {
	const size_t keep =
		std::min(m_blockStart, m_position > size_t(WINDOW) ? m_position - WINDOW : 0);
	if (keep > m_base && keep - m_base >= (size_t(1) << 20)) {
		m_input.erase(m_input.begin(), m_input.begin() + std::ptrdiff_t(keep - m_base));
		m_base = keep;
	}
}

uint32_t Compressor::Hash(size_t position) const noexcept {
	uint32_t value =
		(uint32_t(At(position)) << 16) | (uint32_t(At(position + 1)) << 8) | At(position + 2);
	return (value * 2654435761u) >> (32 - HASH_BITS);
}

void Compressor::Insert(size_t position) {
	if (position + MIN_MATCH > End())
		return;
	uint32_t hash = Hash(position);
	m_previous[position & (WINDOW - 1)] = m_head[hash];
	m_head[hash] = int64_t(position);
}

void Compressor::FindMatch(size_t position, int& bestLength, int& bestDistance) const {
	bestLength = 0;
	bestDistance = 0;
	const size_t size = End();
	if (position + MIN_MATCH > size)
		return;
	const int maxLength = int(std::min<size_t>(MAX_MATCH, size - position));
	int64_t candidate = m_head[Hash(position)];
	int chain = m_params.maxChain;
	while (candidate >= 0 && chain-- > 0 && bestLength < maxLength) {
		const size_t from = size_t(candidate);
		if (from >= position)
			break;
		const size_t distance = position - from;
		if (distance > WINDOW)
			break;
		if (from < m_base)
			break; // sorti du tampon (déjà glissé)
		if (At(from + size_t(bestLength)) == At(position + size_t(bestLength)) ||
			bestLength == 0) {
			int length = 0;
			while (length < maxLength &&
				   At(from + size_t(length)) == At(position + size_t(length)))
				++length;
			if (length > bestLength) {
				bestLength = length;
				bestDistance = int(distance);
				if (length >= m_params.niceLength)
					break;
			}
		}
		int64_t next = m_previous[from & (WINDOW - 1)];
		if (next >= candidate)
			break; // entrée écrasée par une position plus récente de la fenêtre
				   // circulaire
		candidate = next;
	}
	if (bestLength < MIN_MATCH)
		bestLength = 0;
}

void Compressor::WriteStoredBlocks(size_t start, size_t end, bool final) {
	if (start == end) {
		m_writer.Bits(final ? 1 : 0, 3);
		m_writer.AlignToByte();
		static constexpr uint8_t EMPTY[4] = {0x00, 0x00, 0xFF, 0xFF};
		m_writer.Bytes(EMPTY);
		return;
	}
	while (start < end) {
		size_t chunk = std::min<size_t>(65535, end - start);
		bool last = final && start + chunk == end;
		m_writer.Bits(last ? 1 : 0, 3);
		m_writer.AlignToByte();
		uint8_t header[4] = {uint8_t(chunk), uint8_t(chunk >> 8), uint8_t(~chunk),
							 uint8_t(~chunk >> 8)};
		m_writer.Bytes(header);
		m_writer.Bytes(std::span<const uint8_t>(m_input.data() + (start - m_base), chunk));
		start += chunk;
	}
}

void Compressor::FlushBlock(size_t start, size_t end, bool final) {
	std::array<uint32_t, 286> literalFrequency{};
	std::array<uint32_t, 30> distanceFrequency{};
	uint64_t extraBits = 0;
	for (const Token& token : m_tokens) {
		if (token.length == 0) {
			++literalFrequency[token.distanceOrLiteral];
		} else {
			int ls = LengthSymbol(token.length);
			int ds = DistanceSymbol(token.distanceOrLiteral);
			++literalFrequency[size_t(257 + ls)];
			++distanceFrequency[size_t(ds)];
			extraBits += uint64_t(LENGTH_EXTRA[ls]) + DISTANCE_EXTRA[ds];
		}
	}
	literalFrequency[256] = 1;
	// Au moins deux codes dans chaque arbre (certains décodeurs refusent
	// les codes réduits à un seul symbole).
	if (std::count_if(literalFrequency.begin(), literalFrequency.end(),
					  [](uint32_t f) { return f != 0; }) < 2)
		literalFrequency[0] = std::max<uint32_t>(literalFrequency[0], 1);
	int usedDistances = int(std::count_if(distanceFrequency.begin(), distanceFrequency.end(),
										  [](uint32_t f) { return f != 0; }));
	if (usedDistances < 2) {
		distanceFrequency[0] = std::max<uint32_t>(distanceFrequency[0], 1);
		distanceFrequency[1] = std::max<uint32_t>(distanceFrequency[1], 1);
	}

	std::array<uint8_t, 286> literalLengths{};
	std::array<uint8_t, 30> distanceLengths{};
	BuildLengths(literalFrequency, 15, literalLengths);
	BuildLengths(distanceFrequency, 15, distanceLengths);

	int literalCount = 286;
	while (literalCount > 257 && literalLengths[size_t(literalCount - 1)] == 0)
		--literalCount;
	int distanceCount = 30;
	while (distanceCount > 1 && distanceLengths[size_t(distanceCount - 1)] == 0)
		--distanceCount;

	// Code des longueurs : RLE sur la suite littéraux + distances.
	std::vector<uint8_t> sequence(literalLengths.begin(),
								  literalLengths.begin() + literalCount);
	sequence.insert(sequence.end(), distanceLengths.begin(),
					distanceLengths.begin() + distanceCount);
	struct Rle {
		uint8_t symbol;
		uint8_t extra;
	};
	std::vector<Rle> rle;
	for (size_t i = 0; i < sequence.size();) {
		uint8_t value = sequence[i];
		size_t run = 1;
		while (i + run < sequence.size() && sequence[i + run] == value)
			++run;
		size_t remaining = run;
		if (value == 0) {
			while (remaining >= 11) {
				size_t take = std::min<size_t>(138, remaining);
				rle.push_back(Rle{18, uint8_t(take - 11)});
				remaining -= take;
			}
			if (remaining >= 3) {
				rle.push_back(Rle{17, uint8_t(remaining - 3)});
				remaining = 0;
			}
		} else {
			rle.push_back(Rle{value, 0});
			--remaining;
			while (remaining >= 3) {
				size_t take = std::min<size_t>(6, remaining);
				rle.push_back(Rle{16, uint8_t(take - 3)});
				remaining -= take;
			}
		}
		while (remaining-- > 0)
			rle.push_back(Rle{value, 0});
		i += run;
	}
	std::array<uint32_t, 19> codeLengthFrequency{};
	for (const Rle& item : rle)
		++codeLengthFrequency[item.symbol];
	std::array<uint8_t, 19> codeLengthLengths{};
	BuildLengths(codeLengthFrequency, 7, codeLengthLengths);
	static constexpr uint8_t ORDER[19] = {16, 17, 18, 0, 8,	 7, 9,	6, 10, 5,
										  11, 4,  12, 3, 13, 2, 14, 1, 15};
	int codeLengthCount = 19;
	while (codeLengthCount > 4 && codeLengthLengths[ORDER[codeLengthCount - 1]] == 0)
		--codeLengthCount;

	// Coût de chaque variante, en bits.
	uint64_t dynamicBits = 3 + 14 + 3 * uint64_t(codeLengthCount) + extraBits;
	for (const Rle& item : rle)
		dynamicBits += codeLengthLengths[item.symbol] + (item.symbol == 16	 ? 2
														 : item.symbol == 17 ? 3
														 : item.symbol == 18 ? 7
																			 : 0);
	uint64_t fixedBits = 3 + extraBits;
	for (size_t s = 0; s < 286; ++s) {
		dynamicBits += uint64_t(literalFrequency[s]) * literalLengths[s];
		fixedBits += uint64_t(literalFrequency[s]) * (s < 144	? 8
													  : s < 256 ? 9
													  : s < 280 ? 7
																: 8);
	}
	for (size_t s = 0; s < 30; ++s) {
		dynamicBits += uint64_t(distanceFrequency[s]) * distanceLengths[s];
		fixedBits += uint64_t(distanceFrequency[s]) * 5;
	}
	uint64_t storedBits = 8 * uint64_t(end - start) + 40 * ((end - start) / 65535 + 1);

	if (storedBits <= dynamicBits && storedBits <= fixedBits) {
		WriteStoredBlocks(start, end, final);
	} else if (fixedBits <= dynamicBits) {
		std::array<uint8_t, 288> fixedLiteral{};
		for (size_t s = 0; s < 288; ++s)
			fixedLiteral[s] = uint8_t(s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8);
		std::array<uint8_t, 30> fixedDistance{};
		fixedDistance.fill(5);
		m_writer.Bits(final ? 1 : 0, 1);
		m_writer.Bits(1, 2);
		WriteTokens(fixedLiteral, fixedDistance);
	} else {
		m_writer.Bits(final ? 1 : 0, 1);
		m_writer.Bits(2, 2);
		m_writer.Bits(uint32_t(literalCount - 257), 5);
		m_writer.Bits(uint32_t(distanceCount - 1), 5);
		m_writer.Bits(uint32_t(codeLengthCount - 4), 4);
		for (int i = 0; i < codeLengthCount; ++i)
			m_writer.Bits(codeLengthLengths[ORDER[i]], 3);
		std::array<uint16_t, 19> codeLengthCodes{};
		BuildCodes(codeLengthLengths, codeLengthCodes);
		for (const Rle& item : rle) {
			m_writer.Bits(codeLengthCodes[item.symbol], codeLengthLengths[item.symbol]);
			if (item.symbol == 16)
				m_writer.Bits(item.extra, 2);
			else if (item.symbol == 17)
				m_writer.Bits(item.extra, 3);
			else if (item.symbol == 18)
				m_writer.Bits(item.extra, 7);
		}
		WriteTokens(literalLengths, distanceLengths);
	}
	m_tokens.clear();
}

void Compressor::WriteTokens(std::span<const uint8_t> literalLengths, std::span<const uint8_t> distanceLengths) {
	std::vector<uint16_t> literalCodes(literalLengths.size());
	std::vector<uint16_t> distanceCodes(distanceLengths.size());
	BuildCodes(literalLengths, literalCodes);
	BuildCodes(distanceLengths, distanceCodes);
	for (const Token& token : m_tokens) {
		if (token.length == 0) {
			m_writer.Bits(literalCodes[token.distanceOrLiteral],
						  literalLengths[token.distanceOrLiteral]);
			continue;
		}
		int ls = LengthSymbol(token.length);
		m_writer.Bits(literalCodes[size_t(257 + ls)], literalLengths[size_t(257 + ls)]);
		m_writer.Bits(uint32_t(token.length - LENGTH_BASE[ls]), LENGTH_EXTRA[ls]);
		int ds = DistanceSymbol(token.distanceOrLiteral);
		m_writer.Bits(distanceCodes[size_t(ds)], distanceLengths[size_t(ds)]);
		m_writer.Bits(uint32_t(token.distanceOrLiteral - DISTANCE_BASE[ds]),
					  DISTANCE_EXTRA[ds]);
	}
	m_writer.Bits(literalCodes[256], literalLengths[256]);
}

} // namespace detail::deflate

Bytes Deflate(std::span<const uint8_t> input, int level) {
	detail::deflate::Compressor compressor(level);
	return compressor.Run(input);
}

namespace detail::deflate {

// ── DeflateEncoderImpl ───────────────────────────────────────────────────────

Result<bool, ArchiveError> DeflateEncoderImpl::Consume(const uint8_t* data, size_t size) {
	m_compressor.Feed(std::span<const uint8_t>(data, size));
	if (++m_calls % 16 == 0 || size >= (size_t(1) << 16))
		return Drain(false);
	return Ok(true);
}

Result<bool, ArchiveError> DeflateEncoderImpl::Drain(bool final) {
	m_compressor.Compress(final);
	archive::Bytes out = m_compressor.TakeOutput();
	return Emit(out.data(), out.size());
}

} // namespace detail::deflate

Result<ArchiveStream, ArchiveError> OpenDeflateEncoder(ArchiveStream sink, int level) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(
		std::make_unique<detail::deflate::DeflateEncoderImpl>(std::move(sink), level, state),
		state);
}

} // namespace data::archive
