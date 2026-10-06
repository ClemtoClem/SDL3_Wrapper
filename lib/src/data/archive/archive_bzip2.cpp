// Définitions de data/archive/archive_bzip2.hpp
#include "data/archive/archive_bzip2.hpp"

namespace data::archive {

namespace detail::bzip2 {

uint32_t CrcUpdate(uint32_t crc, uint8_t byte) noexcept {
	static const std::array<uint32_t, 256> TABLE = [] {
		std::array<uint32_t, 256> table{};
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t value = i << 24;
			for (int bit = 0; bit < 8; ++bit)
				value = (value & 0x80000000u) ? (value << 1) ^ 0x04C11DB7u : value << 1;
			table[i] = value;
		}
		return table;
	}();
	return (crc << 8) ^ TABLE[((crc >> 24) ^ byte) & 0xFF];
}

// ── BitReader ────────────────────────────────────────────────────────────────

bool BitReader::Bits(int count, uint32_t& out) {
	while (m_count < count) {
		uint8_t byte = 0;
		if (!m_input->Byte(byte))
			return false;
		m_buffer = (m_buffer << 8) | byte;
		m_count += 8;
	}
	m_count -= count;
	out = uint32_t((m_buffer >> m_count) & ((uint64_t(1) << count) - 1));
	return true;
}

void BitReader::Reset() noexcept {
	m_buffer = 0;
	m_count = 0;
}

// ── DecodeTable ──────────────────────────────────────────────────────────────

void DecodeTable::Build(const uint8_t* lengths, int alphaSize) {
	minLength = 32;
	maxLength = 0;
	for (int i = 0; i < alphaSize; ++i) {
		minLength = std::min<int>(minLength, lengths[i]);
		maxLength = std::max<int>(maxLength, lengths[i]);
	}
	int pp = 0;
	for (int length = minLength; length <= maxLength; ++length)
		for (int symbol = 0; symbol < alphaSize; ++symbol)
			if (lengths[symbol] == length)
				perm[size_t(pp++)] = uint16_t(symbol);
	std::array<int32_t, MAX_CODE_LENGTH + 3> count{};
	for (int i = 0; i < alphaSize; ++i)
		++count[size_t(lengths[i]) + 1];
	for (size_t i = 1; i < count.size(); ++i)
		count[i] += count[i - 1];
	limit.fill(0);
	base.fill(0);
	int32_t vector = 0;
	for (int length = minLength; length <= maxLength; ++length) {
		vector += count[size_t(length) + 1] - count[size_t(length)];
		limit[size_t(length)] = vector - 1;
		vector <<= 1;
	}
	for (int length = minLength; length <= maxLength; ++length)
		base[size_t(length)] = count[size_t(length)];
	for (int length = minLength + 1; length <= maxLength; ++length)
		base[size_t(length)] = ((limit[size_t(length) - 1] + 1) << 1) - count[size_t(length)];
	if (minLength <= maxLength)
		base[size_t(minLength)] = count[size_t(minLength)];
}

// ── Decoder ──────────────────────────────────────────────────────────────────

void Decoder::Reset() {
	m_bits.Reset();
	m_mode = Mode::STREAM_HEADER;
	m_streams = 0;
}

Result<size_t, String> Decoder::Decode(uint8_t* out, size_t max) {
	size_t produced = 0;
	while (produced < max) {
		if (m_mode == Mode::OUTPUT) {
			produced += Output(out + produced, max - produced);
			if (m_mode == Mode::OUTPUT)
				continue;
			if ((m_blockCrc ^ 0xFFFFFFFFu) != m_storedBlockCrc)
				return Err(String("bzip2 : CRC du bloc incorrect (données corrompues)"));
			m_combinedCrc = ((m_combinedCrc << 1) | (m_combinedCrc >> 31)) ^ m_storedBlockCrc;
			m_mode = Mode::BLOCK;
			continue;
		}
		if (m_mode == Mode::DONE)
			break;
		if (auto error = Step(); error.IsSome())
			return Err(error.Unwrap());
	}
	return Ok(produced);
}

Option<String> Decoder::Step() {
	if (m_mode == Mode::STREAM_HEADER) {
		uint32_t b = 0, z = 0, h = 0, level = 0;
		if (!m_bits.Bits(8, b)) {
			if (m_streams > 0) {
				m_mode = Mode::DONE;
				return NONE;
			}
			return Some(String("bzip2 : fichier vide"));
		}
		if (!m_bits.Bits(8, z) || !m_bits.Bits(8, h) || !m_bits.Bits(8, level) || b != 'B' ||
			z != 'Z' || h != 'h' || level < '1' || level > '9') {
			// Après un flux complet, des octets nuls de remplissage sont tolérés.
			if (m_streams > 0 && b == 0) {
				m_mode = Mode::DONE;
				return NONE;
			}
			return Some(String(m_streams > 0 ? "bzip2 : données inattendues après le flux"
											 : "bzip2 : signature « BZh » absente"));
		}
		m_blockSize100k = int(level - '0');
		m_combinedCrc = 0;
		m_mode = Mode::BLOCK;
		return NONE;
	}
	// Mode::BLOCK
	uint32_t high = 0, low = 0;
	if (!m_bits.Bits(24, high) || !m_bits.Bits(24, low))
		return Some(String("bzip2 : flux tronqué"));
	const uint64_t magic = (uint64_t(high) << 24) | low;
	uint32_t crc = 0;
	if (!m_bits.Bits(32, crc))
		return Some(String("bzip2 : flux tronqué"));
	if (magic == END_MAGIC) {
		if (crc != m_combinedCrc)
			return Some(String("bzip2 : CRC combiné du flux incorrect"));
		m_bits.AlignToByte();
		++m_streams;
		m_mode = Mode::STREAM_HEADER;
		return NONE;
	}
	if (magic != BLOCK_MAGIC)
		return Some(String("bzip2 : signature de bloc invalide"));
	m_storedBlockCrc = crc;
	return ReadBlock();
}

Option<String> Decoder::ReadBlock() {
	uint32_t randomised = 0, origin = 0;
	if (!m_bits.Bit(randomised) || !m_bits.Bits(24, origin))
		return Some(String("bzip2 : en-tête de bloc tronqué"));
	if (randomised)
		return Some(String("bzip2 : bloc « randomisé » (bzip2 < 0.9.5) non géré"));
	// Octets présents.
	uint32_t used16 = 0;
	if (!m_bits.Bits(16, used16))
		return Some(String("bzip2 : table des symboles tronquée"));
	std::array<uint8_t, 256> seqToUnseq{};
	int inUse = 0;
	for (int i = 0; i < 16; ++i) {
		if (!(used16 & (0x8000u >> i)))
			continue;
		uint32_t bits = 0;
		if (!m_bits.Bits(16, bits))
			return Some(String("bzip2 : table des symboles tronquée"));
		for (int j = 0; j < 16; ++j)
			if (bits & (0x8000u >> j))
				seqToUnseq[size_t(inUse++)] = uint8_t(i * 16 + j);
	}
	if (inUse == 0)
		return Some(String("bzip2 : aucun symbole utilisé"));
	const int alphaSize = inUse + 2;
	uint32_t groups = 0, selectorCount = 0;
	if (!m_bits.Bits(3, groups) || !m_bits.Bits(15, selectorCount) || groups < 2 ||
		groups > MAX_GROUPS || selectorCount < 1)
		return Some(String("bzip2 : tables de Huffman invalides"));
	// Sélecteurs (MTF, unaire) — au-delà de 18 002 : lus mais ignorés
	// (bzip2 1.0.8).
	std::vector<uint8_t> selectors;
	selectors.reserve(std::min<uint32_t>(selectorCount, MAX_SELECTORS));
	std::array<uint8_t, MAX_GROUPS> mtf{0, 1, 2, 3, 4, 5};
	for (uint32_t i = 0; i < selectorCount; ++i) {
		uint32_t j = 0;
		for (;;) {
			uint32_t bit = 0;
			if (!m_bits.Bit(bit))
				return Some(String("bzip2 : sélecteurs tronqués"));
			if (!bit)
				break;
			if (++j >= groups)
				return Some(String("bzip2 : sélecteur invalide"));
		}
		const uint8_t value = mtf[j];
		for (; j > 0; --j)
			mtf[j] = mtf[j - 1];
		mtf[0] = value;
		if (i < uint32_t(MAX_SELECTORS))
			selectors.push_back(value);
	}
	// Longueurs de code, codées en différences.
	std::array<DecodeTable, MAX_GROUPS> tables;
	for (uint32_t t = 0; t < groups; ++t) {
		uint8_t lengths[MAX_ALPHA];
		uint32_t current = 0;
		if (!m_bits.Bits(5, current))
			return Some(String("bzip2 : longueurs de code tronquées"));
		for (int symbol = 0; symbol < alphaSize; ++symbol) {
			for (;;) {
				if (current < 1 || current > uint32_t(MAX_CODE_LENGTH))
					return Some(String("bzip2 : longueur de code invalide"));
				uint32_t bit = 0;
				if (!m_bits.Bit(bit))
					return Some(String("bzip2 : longueurs de code tronquées"));
				if (!bit)
					break;
				if (!m_bits.Bit(bit))
					return Some(String("bzip2 : longueurs de code tronquées"));
				current = bit ? current - 1 : current + 1;
			}
			lengths[symbol] = uint8_t(current);
		}
		tables[t].Build(lengths, alphaSize);
	}

	// Symboles MTF/RLE2 → octets de la BWT.
	const uint32_t blockMax = uint32_t(m_blockSize100k) * 100000u;
	m_tt.assign(blockMax, 0);
	std::array<uint32_t, 256> counts{};
	std::array<uint8_t, 256> list{};
	for (int i = 0; i < 256; ++i)
		list[size_t(i)] = uint8_t(i);
	const int endOfBlock = inUse + 1;
	uint32_t length = 0;
	size_t groupIndex = 0;
	int groupLeft = 0;
	const DecodeTable* table = nullptr;
	auto nextSymbol = [&](int& symbol) -> Option<String> {
		if (groupLeft == 0) {
			if (groupIndex >= selectors.size())
				return Some(String("bzip2 : plus de groupes que de sélecteurs"));
			table = &tables[selectors[groupIndex++]];
			groupLeft = GROUP_SIZE;
		}
		--groupLeft;
		int n = table->minLength;
		uint32_t code = 0;
		if (!m_bits.Bits(n, code))
			return Some(String("bzip2 : données tronquées"));
		while (n <= MAX_CODE_LENGTH && int32_t(code) > table->limit[size_t(n)]) {
			uint32_t bit = 0;
			if (!m_bits.Bit(bit))
				return Some(String("bzip2 : données tronquées"));
			code = (code << 1) | bit;
			++n;
		}
		if (n > MAX_CODE_LENGTH)
			return Some(String("bzip2 : code de Huffman invalide"));
		const int32_t index = int32_t(code) - table->base[size_t(n)];
		if (index < 0 || index >= MAX_ALPHA)
			return Some(String("bzip2 : code de Huffman invalide"));
		symbol = table->perm[size_t(index)];
		return NONE;
	};
	int symbol = 0;
	if (auto error = nextSymbol(symbol); error.IsSome())
		return error;
	for (;;) {
		if (symbol == endOfBlock)
			break;
		if (symbol <= 1) { // RUNA / RUNB : série de l'octet en tête de liste
			uint32_t run = 0, weight = 1;
			while (symbol <= 1) {
				run += weight << symbol;
				weight <<= 1;
				if (weight >= (uint32_t(1) << 21))
					return Some(String("bzip2 : série trop longue"));
				if (auto error = nextSymbol(symbol); error.IsSome())
					return error;
			}
			const uint8_t byte = seqToUnseq[list[0]];
			if (run > blockMax - length)
				return Some(String("bzip2 : bloc plus grand que annoncé"));
			counts[byte] += run;
			for (uint32_t k = 0; k < run; ++k)
				m_tt[length++] = byte;
			continue;
		}
		if (symbol >= alphaSize || length >= blockMax)
			return Some(String("bzip2 : symbole ou bloc invalide"));
		const size_t position = size_t(symbol - 1);
		const uint8_t value = list[position];
		std::memmove(list.data() + 1, list.data(), position);
		list[0] = value;
		const uint8_t byte = seqToUnseq[value];
		++counts[byte];
		m_tt[length++] = byte;
		if (auto error = nextSymbol(symbol); error.IsSome())
			return error;
	}
	if (origin >= length)
		return Some(String("bzip2 : origine de la BWT hors du bloc"));

	// BWT inverse (vecteur T de bzip2 : octet dans les 8 bits bas, lien
	// au-dessus).
	std::array<uint32_t, 256> start{};
	uint32_t sum = 0;
	for (int i = 0; i < 256; ++i) {
		start[size_t(i)] = sum;
		sum += counts[size_t(i)];
	}
	for (uint32_t i = 0; i < length; ++i) {
		const uint8_t byte = uint8_t(m_tt[i]);
		m_tt[start[byte]++] |= i << 8;
	}
	m_position = m_tt[origin] >> 8;
	m_left = length;
	m_runLength = 0;
	m_last = -1;
	m_repeat = 0;
	m_blockCrc = 0xFFFFFFFFu;
	m_mode = Mode::OUTPUT;
	return NONE;
}

size_t Decoder::Output(uint8_t* out, size_t max) {
	size_t produced = 0;
	while (produced < max) {
		if (m_repeat > 0) {
			const uint8_t byte = uint8_t(m_last);
			out[produced++] = byte;
			m_blockCrc = CrcUpdate(m_blockCrc, byte);
			--m_repeat;
			continue;
		}
		if (m_left == 0) {
			m_mode = Mode::BLOCK;
			break;
		}
		const uint32_t entry = m_tt[m_position];
		const uint8_t byte = uint8_t(entry);
		m_position = entry >> 8;
		--m_left;
		if (m_runLength == 4) {
			m_repeat = byte;
			m_runLength = 0;
			continue;
		}
		if (int(byte) == m_last) {
			++m_runLength;
		} else {
			m_runLength = 1;
			m_last = byte;
		}
		out[produced++] = byte;
		m_blockCrc = CrcUpdate(m_blockCrc, byte);
	}
	return produced;
}

// ── DecoderStreamImpl ────────────────────────────────────────────────────────

Result<size_t, ArchiveError> DecoderStreamImpl::Produce(uint8_t* out, size_t max) {
	auto produced = m_decoder.Decode(out, max);
	if (produced.IsError())
		return Err(m_buffer.Failed() ? m_buffer.Failure("bzip2")
									 : MakeError(ErrorKind::CORRUPT, produced.Error()));
	return Ok(produced.Value());
}

bool DecoderStreamImpl::Restart() {
	if (!m_buffer.Rewind())
		return false;
	m_decoder.Reset();
	return true;
}

// ── BitWriter ────────────────────────────────────────────────────────────────

void BitWriter::Bits(int count, uint32_t value) {
	m_buffer = (m_buffer << count) | (uint64_t(value) & ((uint64_t(1) << count) - 1));
	m_count += count;
	while (m_count >= 8) {
		m_count -= 8;
		m_bytes.push_back(uint8_t(m_buffer >> m_count));
	}
}

void BitWriter::Bits48(uint64_t value) {
	Bits(24, uint32_t(value >> 24));
	Bits(24, uint32_t(value & 0xFFFFFF));
}

void BitWriter::Flush() {
	if (m_count > 0)
		Bits(8 - m_count, 0);
}

Bytes BitWriter::Take() {
	Bytes out = std::move(m_bytes);
	m_bytes.clear();
	return out;
}

std::vector<uint32_t> SortRotations(const Bytes& block) {
	const uint32_t n = uint32_t(block.size());
	std::vector<uint32_t> order(n), rank(n), next(n), temp(n);
	std::vector<uint32_t> count(std::max<uint32_t>(n, 256) + 1);
	// Passe initiale : par octet.
	std::fill(count.begin(), count.begin() + 257, 0);
	for (uint32_t i = 0; i < n; ++i)
		++count[size_t(block[i]) + 1];
	for (size_t i = 1; i <= 256; ++i)
		count[i] += count[i - 1];
	for (uint32_t i = 0; i < n; ++i)
		order[count[block[i]]++] = i;
	uint32_t classes = 1;
	rank[order[0]] = 0;
	for (uint32_t i = 1; i < n; ++i) {
		if (block[order[i]] != block[order[i - 1]])
			++classes;
		rank[order[i]] = classes - 1;
	}
	for (uint32_t k = 1; k < n && classes < n; k <<= 1) {
		// Ordre selon la seconde moitié : décaler l'ordre courant de -k.
		for (uint32_t i = 0; i < n; ++i)
			temp[i] = order[i] >= k ? order[i] - k : order[i] + n - k;
		// Tri stable par la première moitié (rang).
		std::fill(count.begin(), count.begin() + classes + 1, 0);
		for (uint32_t i = 0; i < n; ++i)
			++count[size_t(rank[temp[i]]) + 1];
		for (uint32_t i = 1; i <= classes; ++i)
			count[i] += count[i - 1];
		for (uint32_t i = 0; i < n; ++i)
			order[count[rank[temp[i]]]++] = temp[i];
		// Nouveaux rangs.
		next[order[0]] = 0;
		uint32_t fresh = 1;
		for (uint32_t i = 1; i < n; ++i) {
			const uint32_t a = order[i], b = order[i - 1];
			const uint32_t a2 = a + k < n ? a + k : a + k - n;
			const uint32_t b2 = b + k < n ? b + k : b + k - n;
			if (rank[a] != rank[b] || rank[a2] != rank[b2])
				++fresh;
			next[a] = fresh - 1;
		}
		rank.swap(next);
		classes = fresh;
	}
	return order;
}

void EncodeBlock(const Bytes& block, uint32_t blockCrc, BitWriter& writer) {
	const uint32_t n = uint32_t(block.size());
	const std::vector<uint32_t> order = SortRotations(block);
	uint32_t origin = 0;
	std::array<bool, 256> used{};
	for (uint8_t byte : block)
		used[byte] = true;
	std::array<uint8_t, 256> unseqToSeq{};
	int inUse = 0;
	for (int i = 0; i < 256; ++i)
		if (used[size_t(i)])
			unseqToSeq[size_t(i)] = uint8_t(inUse++);
	const int alphaSize = inUse + 2;
	const int endOfBlock = inUse + 1;

	// MTF + séries de zéros (RUNA/RUNB).
	std::vector<uint16_t> symbols;
	symbols.reserve(n + 1);
	std::array<uint8_t, 256> list{};
	for (int i = 0; i < inUse; ++i)
		list[size_t(i)] = uint8_t(i);
	uint32_t zeros = 0;
	auto flushZeros = [&]() {
		if (zeros == 0)
			return;
		--zeros;
		for (;;) {
			symbols.push_back(uint16_t(zeros & 1)); // RUNB si impair, RUNA sinon
			if (zeros < 2)
				break;
			zeros = (zeros - 2) / 2;
		}
		zeros = 0;
	};
	for (uint32_t i = 0; i < n; ++i) {
		if (order[i] == 0)
			origin = i;
		const uint8_t byte = block[order[i] == 0 ? n - 1 : order[i] - 1];
		const uint8_t value = unseqToSeq[byte];
		if (list[0] == value) {
			++zeros;
			continue;
		}
		flushZeros();
		size_t position = 1;
		while (list[position] != value)
			++position;
		std::memmove(list.data() + 1, list.data(), position);
		list[0] = value;
		symbols.push_back(uint16_t(position + 1));
	}
	flushZeros();
	symbols.push_back(uint16_t(endOfBlock));

	// Tables de Huffman : partition initiale puis 4 passes d'affinage.
	std::array<uint32_t, MAX_ALPHA> frequency{};
	for (uint16_t symbol : symbols)
		++frequency[symbol];
	const size_t count = symbols.size();
	const int groups = count < 200 ? 2 : count < 600 ? 3 : count < 1200 ? 4 : count < 2400 ? 5 : 6;
	std::array<std::array<uint8_t, MAX_ALPHA>, MAX_GROUPS> lengths{};
	{
		int parts = groups;
		size_t remaining = count;
		int groupStart = 0;
		while (parts > 0) {
			const size_t target = remaining / size_t(parts);
			int groupEnd = groupStart - 1;
			size_t accumulated = 0;
			while (accumulated < target && groupEnd < alphaSize - 1)
				accumulated += frequency[size_t(++groupEnd)];
			if (groupEnd > groupStart && parts != groups && parts != 1 &&
				((groups - parts) % 2 == 1))
				accumulated -= frequency[size_t(groupEnd--)];
			for (int v = 0; v < alphaSize; ++v)
				lengths[size_t(parts - 1)][size_t(v)] = (v >= groupStart && v <= groupEnd) ? 0 : 15;
			--parts;
			groupStart = groupEnd + 1;
			remaining -= accumulated;
		}
	}
	std::vector<uint8_t> selectors((count + GROUP_SIZE - 1) / GROUP_SIZE);
	for (int iteration = 0; iteration < 4; ++iteration) {
		std::array<std::array<uint32_t, MAX_ALPHA>, MAX_GROUPS> groupFrequency{};
		for (size_t g = 0; g < selectors.size(); ++g) {
			const size_t from = g * GROUP_SIZE, to = std::min(count, from + GROUP_SIZE);
			int best = 0;
			uint32_t bestCost = UINT32_MAX;
			for (int t = 0; t < groups; ++t) {
				uint32_t cost = 0;
				for (size_t i = from; i < to; ++i)
					cost += lengths[size_t(t)][symbols[i]];
				if (cost < bestCost) {
					bestCost = cost;
					best = t;
				}
			}
			selectors[g] = uint8_t(best);
			for (size_t i = from; i < to; ++i)
				++groupFrequency[size_t(best)][symbols[i]];
		}
		for (int t = 0; t < groups; ++t) {
			std::array<uint32_t, MAX_ALPHA> weights{};
			for (int v = 0; v < alphaSize; ++v)
				weights[size_t(v)] = std::max<uint32_t>(groupFrequency[size_t(t)][size_t(v)],
														1); // tout symbole a un code
			deflate::BuildLengths(std::span<const uint32_t>(weights.data(), size_t(alphaSize)),
								  ENCODER_MAX_LENGTH,
								  std::span<uint8_t>(lengths[size_t(t)].data(), size_t(alphaSize)));
		}
	}

	// Codes canoniques (bits de poids fort d'abord, par longueur puis symbole).
	std::array<std::array<uint32_t, MAX_ALPHA>, MAX_GROUPS> codes{};
	for (int t = 0; t < groups; ++t) {
		uint32_t code = 0;
		for (int length = 1; length <= ENCODER_MAX_LENGTH; ++length) {
			for (int v = 0; v < alphaSize; ++v)
				if (lengths[size_t(t)][size_t(v)] == length)
					codes[size_t(t)][size_t(v)] = code++;
			code <<= 1;
		}
	}

	// Écriture du bloc.
	writer.Bits48(BLOCK_MAGIC);
	writer.Bits(32, blockCrc);
	writer.Bits(1, 0);
	writer.Bits(24, origin);
	uint32_t used16 = 0;
	for (int i = 0; i < 16; ++i)
		for (int j = 0; j < 16; ++j)
			if (used[size_t(i * 16 + j)])
				used16 |= 0x8000u >> i;
	writer.Bits(16, used16);
	for (int i = 0; i < 16; ++i) {
		if (!(used16 & (0x8000u >> i)))
			continue;
		uint32_t bits = 0;
		for (int j = 0; j < 16; ++j)
			if (used[size_t(i * 16 + j)])
				bits |= 0x8000u >> j;
		writer.Bits(16, bits);
	}
	writer.Bits(3, uint32_t(groups));
	writer.Bits(15, uint32_t(selectors.size()));
	std::array<uint8_t, MAX_GROUPS> mtf{0, 1, 2, 3, 4, 5};
	for (uint8_t selector : selectors) {
		int j = 0;
		while (mtf[size_t(j)] != selector)
			++j;
		for (int k = j; k > 0; --k)
			mtf[size_t(k)] = mtf[size_t(k - 1)];
		mtf[0] = selector;
		for (int k = 0; k < j; ++k)
			writer.Bits(1, 1);
		writer.Bits(1, 0);
	}
	for (int t = 0; t < groups; ++t) {
		int current = lengths[size_t(t)][0];
		writer.Bits(5, uint32_t(current));
		for (int v = 0; v < alphaSize; ++v) {
			const int target = lengths[size_t(t)][size_t(v)];
			while (current < target) {
				writer.Bits(2, 2);
				++current;
			}
			while (current > target) {
				writer.Bits(2, 3);
				--current;
			}
			writer.Bits(1, 0);
		}
	}
	for (size_t i = 0; i < count; ++i) {
		const int t = selectors[i / GROUP_SIZE];
		writer.Bits(lengths[size_t(t)][symbols[i]], codes[size_t(t)][symbols[i]]);
	}
}

// ── EncoderStreamImpl ────────────────────────────────────────────────────────

Result<bool, ArchiveError> EncoderStreamImpl::Consume(const uint8_t* data, size_t size) {
	if (!m_headerWritten) {
		const uint8_t header[4] = {'B', 'Z', 'h', uint8_t('0' + m_level)};
		auto emitted = Emit(header, 4);
		if (emitted.IsError())
			return emitted;
		m_headerWritten = true;
	}
	for (size_t i = 0; i < size; ++i) {
		const uint8_t byte = data[i];
		if (m_runLength > 0 && byte == m_runByte && m_runLength < 255) {
			++m_runLength;
		} else {
			if (m_runLength > 0) {
				auto flushed = FlushRun();
				if (flushed.IsError())
					return flushed;
			}
			m_runByte = byte;
			m_runLength = 1;
		}
	}
	return Ok(true);
}

Result<bool, ArchiveError> EncoderStreamImpl::Finish() {
	auto started = Consume(nullptr, 0);
	if (started.IsError())
		return started;
	if (m_runLength > 0) {
		auto flushed = FlushRun();
		if (flushed.IsError())
			return flushed;
	}
	if (!m_block.empty()) {
		auto written = WriteBlock();
		if (written.IsError())
			return written;
	}
	m_writer.Bits48(END_MAGIC);
	m_writer.Bits(32, m_combinedCrc);
	m_writer.Flush();
	Bytes tail = m_writer.Take();
	return Emit(tail.data(), tail.size());
}

Result<bool, ArchiveError> EncoderStreamImpl::FlushRun() {
	// Le CRC d'un bloc couvre ses octets d'origine : une série commencée
	// ne peut pas être coupée entre deux blocs.
	if (m_block.size() >= m_blockMax) {
		auto written = WriteBlock();
		if (written.IsError())
			return written;
	}
	const uint32_t direct = std::min<uint32_t>(m_runLength, 4);
	for (uint32_t k = 0; k < direct; ++k)
		m_block.push_back(m_runByte);
	if (m_runLength >= 4)
		m_block.push_back(uint8_t(m_runLength - 4));
	m_runOriginal.push_back(m_runLength);
	m_runLength = 0;
	return Ok(true);
}

Result<bool, ArchiveError> EncoderStreamImpl::WriteBlock() {
	// CRC des octets d'origine du bloc : recalculé depuis les séries.
	uint32_t crc = 0xFFFFFFFFu;
	size_t at = 0;
	for (uint32_t run : m_runOriginal) {
		const uint8_t byte = m_block[at];
		for (uint32_t k = 0; k < run; ++k)
			crc = CrcUpdate(crc, byte);
		at += std::min<uint32_t>(run, 4) + (run >= 4 ? 1 : 0);
	}
	crc ^= 0xFFFFFFFFu;
	EncodeBlock(m_block, crc, m_writer);
	m_combinedCrc = ((m_combinedCrc << 1) | (m_combinedCrc >> 31)) ^ crc;
	m_block.clear();
	m_runOriginal.clear();
	Bytes bytes = m_writer.Take();
	return Emit(bytes.data(), bytes.size());
}

} // namespace detail::bzip2

bool IsBzip2(std::span<const uint8_t> bytes) noexcept {
	return bytes.size() >= 4 && bytes[0] == 'B' && bytes[1] == 'Z' && bytes[2] == 'h' &&
		   bytes[3] >= '1' && bytes[3] <= '9';
}

Result<ArchiveStream, ArchiveError> OpenBzip2Stream(ArchiveStream input, Option<uint64_t> size) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(
		std::make_unique<detail::bzip2::DecoderStreamImpl>(std::move(input), size, state), state);
}

Result<ArchiveStream, ArchiveError> OpenBzip2Encoder(ArchiveStream sink, int level) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(
		std::make_unique<detail::bzip2::EncoderStreamImpl>(std::move(sink), level, state), state);
}

Result<Bytes, ArchiveError> Bzip2Decompress(std::span<const uint8_t> input, uint64_t sizeLimit) {
	auto stream = OpenBzip2Stream(OpenMemoryStream(Bytes(input.begin(), input.end())).Unwrap());
	if (stream.IsError())
		return Err(stream.Error());
	return ReadStreamToEnd(stream.Value(), sizeLimit);
}

Result<Bytes, ArchiveError> Bzip2Compress(std::span<const uint8_t> input, int level) {
	auto target = std::make_shared<Bytes>();
	auto encoder = OpenBzip2Encoder(OpenMemorySink(target).Unwrap(), level);
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
