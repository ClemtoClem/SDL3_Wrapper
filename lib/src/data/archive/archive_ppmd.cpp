// Définitions de data/archive/archive_ppmd.hpp
#include "data/archive/archive_ppmd.hpp"

namespace data::archive {

namespace detail::ppmd {

uint32_t GetMean(uint32_t prob) noexcept {
	return (prob + (1u << (PERIOD_BITS - 2))) >> PERIOD_BITS;
}

uint16_t UpdateProb0(uint32_t prob) noexcept {
	return uint16_t(prob + (1u << INT_BITS) - GetMean(prob));
}

uint16_t UpdateProb1(uint32_t prob) noexcept {
	return uint16_t(prob - GetMean(prob));
}

// ── See ──────────────────────────────────────────────────────────────────────

void See::Update() noexcept {
	if (shift < PERIOD_BITS && --count == 0) {
		summ = uint16_t(summ << 1);
		count = uint8_t(3 << shift++);
	}
}

// ── Memory ───────────────────────────────────────────────────────────────────

uint16_t Memory::U16(uint32_t at) const noexcept {
	return uint16_t(m_bytes[at] | (m_bytes[at + 1] << 8));
}

void Memory::SetU16(uint32_t at, uint16_t v) noexcept {
	m_bytes[at] = uint8_t(v);
	m_bytes[at + 1] = uint8_t(v >> 8);
}

uint32_t Memory::U32(uint32_t at) const noexcept {
	return uint32_t(m_bytes[at]) | (uint32_t(m_bytes[at + 1]) << 8) |
		   (uint32_t(m_bytes[at + 2]) << 16) | (uint32_t(m_bytes[at + 3]) << 24);
}

void Memory::SetU32(uint32_t at, uint32_t v) noexcept {
	for (int i = 0; i < 4; ++i)
		m_bytes[at + uint32_t(i)] = uint8_t(v >> (8 * i));
}

void Memory::Copy(uint32_t to, uint32_t from, size_t size) noexcept {
	std::memmove(m_bytes.data() + to, m_bytes.data() + from, size);
}

} // namespace detail::ppmd

// ── Ppmd7Model ───────────────────────────────────────────────────────────────

Ppmd7Model::Ppmd7Model() {
	using namespace detail::ppmd;
	for (unsigned i = 0, k = 0; i < unsigned(NUM_INDEXES); ++i) {
		unsigned step = i >= 12 ? 4 : (i >> 2) + 1;
		do {
			m_units2Index[k++] = uint8_t(i);
		} while (--step);
		m_index2Units[i] = uint8_t(k);
	}
	m_ns2bsIndex[0] = 0 << 1;
	m_ns2bsIndex[1] = 1 << 1;
	std::fill(m_ns2bsIndex.begin() + 2, m_ns2bsIndex.begin() + 11, uint8_t(2 << 1));
	std::fill(m_ns2bsIndex.begin() + 11, m_ns2bsIndex.end(), uint8_t(3 << 1));
	unsigned i = 0;
	for (; i < 3; ++i)
		m_ns2Index[i] = uint8_t(i);
	for (unsigned m = i, k = 1; i < 256; ++i) {
		m_ns2Index[i] = uint8_t(m);
		if (--k == 0)
			k = (++m) - 2;
	}
	std::fill(m_hb2Flag.begin(), m_hb2Flag.begin() + 0x40, uint8_t(0));
	std::fill(m_hb2Flag.begin() + 0x40, m_hb2Flag.end(), uint8_t(8));
}

void Ppmd7Model::Init(uint32_t size, unsigned maxOrder) {
	if (m_mem.Size() == 0 || m_size != size) {
		m_alignOffset = 4 - (size & 3);
		m_mem.Allocate(size_t(m_alignOffset) + size + detail::ppmd::UNIT_SIZE);
		m_size = size;
	}
	m_maxOrder = maxOrder;
	RestartModel();
	m_dummySee.shift = detail::ppmd::PERIOD_BITS;
	m_dummySee.summ = 0;
	m_dummySee.count = 64;
}

uint32_t Ppmd7Model::Successor(uint32_t s) const {
	return uint32_t(m_mem.U16(s + 2)) | (uint32_t(m_mem.U16(s + 4)) << 16);
}

void Ppmd7Model::SetSuccessor(uint32_t s, uint32_t v) {
	m_mem.SetU16(s + 2, uint16_t(v & 0xFFFF));
	m_mem.SetU16(s + 4, uint16_t(v >> 16));
}

void Ppmd7Model::SwapStates(uint32_t a, uint32_t b) {
	uint8_t tmp[6];
	for (int i = 0; i < 6; ++i)
		tmp[i] = m_mem.U8(a + uint32_t(i));
	m_mem.Copy(a, b, 6);
	for (int i = 0; i < 6; ++i)
		m_mem.SetU8(b + uint32_t(i), tmp[i]);
}

uint32_t Ppmd7Model::U2B(unsigned nu) noexcept {
	return uint32_t(nu) * detail::ppmd::UNIT_SIZE;
}

void Ppmd7Model::InsertNode(uint32_t node, unsigned index) {
	m_mem.SetU32(node, m_freeList[index]);
	m_freeList[index] = node;
}

uint32_t Ppmd7Model::RemoveNode(unsigned index) {
	const uint32_t node = m_freeList[index];
	m_freeList[index] = m_mem.U32(node);
	return node;
}

void Ppmd7Model::SplitBlock(uint32_t ptr, unsigned oldIndex, unsigned newIndex) {
	const unsigned nu = I2U(oldIndex) - I2U(newIndex);
	ptr += U2B(I2U(newIndex));
	unsigned i = U2I(nu);
	if (I2U(i) != nu) {
		const unsigned k = I2U(--i);
		InsertNode(ptr + U2B(k), nu - k - 1);
	}
	InsertNode(ptr, i);
}

void Ppmd7Model::GlueFreeBlocks() {
	using namespace detail::ppmd;
	const uint32_t head = m_alignOffset + m_size; // nœud sentinelle, après le bloc
	uint32_t n = head;
	m_glueCount = 255;
	// Liste doublement chaînée de tous les blocs libres.
	for (unsigned i = 0; i < unsigned(NUM_INDEXES); ++i) {
		const uint16_t nu = uint16_t(I2U(i));
		uint32_t next = m_freeList[i];
		m_freeList[i] = 0;
		while (next != 0) {
			const uint32_t node = next;
			m_mem.SetU32(node + 4, n); // Next
			m_mem.SetU32(n + 8, next); // Prev de n
			n = next;
			next = m_mem.U32(node); // ancien lien de liste libre
			m_mem.SetU16(node, 0);	// Stamp
			m_mem.SetU16(node + 2, nu);
		}
	}
	m_mem.SetU16(head, 1);
	m_mem.SetU32(head + 4, n);
	m_mem.SetU32(n + 8, head);
	if (m_loUnit != m_hiUnit)
		m_mem.SetU16(m_loUnit, 1);
	// Recollage des blocs libres adjacents.
	while (n != head) {
		const uint32_t node = n;
		uint32_t nu = m_mem.U16(node + 2);
		for (;;) {
			const uint32_t node2 = node + nu * UNIT_SIZE;
			nu += m_mem.U16(node2 + 2);
			if (m_mem.U16(node2) != 0 || nu >= 0x10000)
				break;
			const uint32_t prev2 = m_mem.U32(node2 + 8), next2 = m_mem.U32(node2 + 4);
			m_mem.SetU32(prev2 + 4, next2);
			m_mem.SetU32(next2 + 8, prev2);
			m_mem.SetU16(node + 2, uint16_t(nu));
		}
		n = m_mem.U32(node + 4);
	}
	// Redistribution dans les listes libres.
	for (n = m_mem.U32(head + 4); n != head;) {
		uint32_t node = n;
		const uint32_t next = m_mem.U32(node + 4);
		unsigned nu = m_mem.U16(node + 2);
		for (; nu > 128; nu -= 128, node += 128 * UNIT_SIZE)
			InsertNode(node, NUM_INDEXES - 1);
		unsigned i = U2I(nu);
		if (I2U(i) != nu) {
			const unsigned k = I2U(--i);
			InsertNode(node + k * UNIT_SIZE, nu - k - 1);
		}
		InsertNode(node, i);
		n = next;
	}
}

uint32_t Ppmd7Model::AllocUnitsRare(unsigned index) {
	using namespace detail::ppmd;
	if (m_glueCount == 0) {
		GlueFreeBlocks();
		if (m_freeList[index] != 0)
			return RemoveNode(index);
	}
	unsigned i = index;
	do {
		if (++i == unsigned(NUM_INDEXES)) {
			const uint32_t numBytes = U2B(I2U(index));
			--m_glueCount;
			if (m_unitsStart - m_text > numBytes) {
				m_unitsStart -= numBytes;
				return m_unitsStart;
			}
			return 0;
		}
	} while (m_freeList[i] == 0);
	const uint32_t block = RemoveNode(i);
	SplitBlock(block, i, index);
	return block;
}

uint32_t Ppmd7Model::AllocUnits(unsigned index) {
	if (m_freeList[index] != 0)
		return RemoveNode(index);
	const uint32_t numBytes = U2B(I2U(index));
	if (numBytes <= m_hiUnit - m_loUnit) {
		const uint32_t block = m_loUnit;
		m_loUnit += numBytes;
		return block;
	}
	return AllocUnitsRare(index);
}

uint32_t Ppmd7Model::ShrinkUnits(uint32_t oldPtr, unsigned oldNu, unsigned newNu) {
	const unsigned i0 = U2I(oldNu), i1 = U2I(newNu);
	if (i0 == i1)
		return oldPtr;
	if (m_freeList[i1] != 0) {
		const uint32_t ptr = RemoveNode(i1);
		m_mem.Copy(ptr, oldPtr, U2B(newNu));
		InsertNode(oldPtr, i0);
		return ptr;
	}
	SplitBlock(oldPtr, i0, i1);
	return oldPtr;
}

void Ppmd7Model::RestartModel() {
	using namespace detail::ppmd;
	m_freeList.fill(0);
	m_text = m_alignOffset;
	m_hiUnit = m_text + m_size;
	m_loUnit = m_unitsStart = m_hiUnit - m_size / 8 / UNIT_SIZE * 7 * UNIT_SIZE;
	m_glueCount = 0;
	m_orderFall = m_maxOrder;
	m_runLength = m_initRl = -int32_t(m_maxOrder < 12 ? m_maxOrder : 12) - 1;
	m_prevSuccess = 0;

	m_hiUnit -= UNIT_SIZE;
	m_minContext = m_maxContext = m_hiUnit;
	SetSuffix(m_minContext, 0);
	SetNumStats(m_minContext, 256);
	SetSummFreq(m_minContext, 256 + 1);
	m_foundState = m_loUnit;
	SetStats(m_minContext, m_foundState);
	m_loUnit += U2B(256 / 2);
	for (unsigned i = 0; i < 256; ++i) {
		const uint32_t s = m_foundState + i * 6;
		SetSymbol(s, uint8_t(i));
		SetFreq(s, 1);
		SetSuccessor(s, 0);
	}
	for (unsigned i = 0; i < 128; ++i)
		for (unsigned k = 0; k < 8; ++k) {
			const uint16_t value = uint16_t(BIN_SCALE - INIT_BIN_ESC[k] / (i + 2));
			for (unsigned m = 0; m < 64; m += 8)
				m_binSumm[i * 64 + k + m] = value;
		}
	for (unsigned i = 0; i < 25; ++i)
		for (unsigned k = 0; k < 16; ++k) {
			See& s = m_see[i][k];
			s.shift = PERIOD_BITS - 4;
			s.summ = uint16_t((5 * i + 10) << s.shift);
			s.count = 4;
		}
}

uint32_t Ppmd7Model::CreateSuccessors(bool skip) {
	uint32_t c = m_minContext;
	const uint32_t upBranch = Successor(m_foundState);
	std::array<uint32_t, MAX_ORDER> ps{};
	unsigned numPs = 0;
	if (!skip)
		ps[numPs++] = m_foundState;
	while (Suffix(c)) {
		c = Suffix(c);
		uint32_t s;
		if (NumStats(c) != 1) {
			for (s = Stats(c); Symbol(s) != Symbol(m_foundState); s += 6) {
			}
		} else {
			s = OneState(c);
		}
		const uint32_t successor = Successor(s);
		if (successor != upBranch) {
			c = successor;
			if (numPs == 0)
				return c;
			break;
		}
		ps[numPs++] = s;
	}
	const uint8_t upSymbol = m_mem.U8(upBranch);
	const uint32_t upSuccessor = upBranch + 1;
	uint32_t upFreq;
	if (NumStats(c) == 1) {
		upFreq = Freq(OneState(c));
	} else {
		uint32_t s;
		for (s = Stats(c); Symbol(s) != upSymbol; s += 6) {
		}
		const uint32_t cf = Freq(s) - 1;
		const uint32_t s0 = SummFreq(c) - NumStats(c) - cf;
		upFreq =
			1 + ((2 * cf <= s0) ? (5 * cf > s0 ? 1 : 0) : ((2 * cf + 3 * s0 - 1) / (2 * s0)));
	}
	do {
		uint32_t c1;
		if (m_hiUnit != m_loUnit) {
			m_hiUnit -= detail::ppmd::UNIT_SIZE;
			c1 = m_hiUnit;
		} else if (m_freeList[0] != 0) {
			c1 = RemoveNode(0);
		} else {
			c1 = AllocUnitsRare(0);
			if (c1 == 0)
				return 0;
		}
		SetNumStats(c1, 1);
		const uint32_t one = OneState(c1);
		SetSymbol(one, upSymbol);
		SetFreq(one, upFreq);
		SetSuccessor(one, upSuccessor);
		SetSuffix(c1, c);
		SetSuccessor(ps[--numPs], c1);
		c = c1;
	} while (numPs != 0);
	return c;
}

void Ppmd7Model::UpdateModel() {
	const uint32_t foundSymbol = Symbol(m_foundState);
	const uint32_t foundFreq = Freq(m_foundState);
	uint32_t fSuccessor = Successor(m_foundState);
	if (foundFreq < MAX_FREQ / 4 && Suffix(m_minContext) != 0) {
		const uint32_t c = Suffix(m_minContext);
		if (NumStats(c) == 1) {
			const uint32_t s = OneState(c);
			if (Freq(s) < 32)
				SetFreq(s, Freq(s) + 1);
		} else {
			uint32_t s = Stats(c);
			if (Symbol(s) != foundSymbol) {
				do {
					s += 6;
				} while (Symbol(s) != foundSymbol);
				if (Freq(s) >= Freq(s - 6)) {
					SwapStates(s, s - 6);
					s -= 6;
				}
			}
			if (Freq(s) < MAX_FREQ - 9) {
				SetFreq(s, Freq(s) + 2);
				SetSummFreq(c, SummFreq(c) + 2);
			}
		}
	}
	if (m_orderFall == 0) {
		m_minContext = m_maxContext = CreateSuccessors(true);
		if (m_minContext == 0) {
			RestartModel();
			return;
		}
		SetSuccessor(m_foundState, m_minContext);
		return;
	}
	m_mem.SetU8(m_text++, uint8_t(foundSymbol));
	uint32_t successor = m_text;
	if (m_text >= m_unitsStart) {
		RestartModel();
		return;
	}
	if (fSuccessor) {
		if (fSuccessor <= successor) {
			const uint32_t cs = CreateSuccessors(false);
			if (cs == 0) {
				RestartModel();
				return;
			}
			fSuccessor = cs;
		}
		if (--m_orderFall == 0) {
			successor = fSuccessor;
			m_text -= (m_maxContext != m_minContext) ? 1 : 0;
		}
	} else {
		SetSuccessor(m_foundState, successor);
		fSuccessor = m_minContext;
	}
	const unsigned ns = NumStats(m_minContext);
	const uint32_t s0 = SummFreq(m_minContext) - ns - (Freq(m_foundState) - 1);
	for (uint32_t c = m_maxContext; c != m_minContext; c = Suffix(c)) {
		const unsigned ns1 = NumStats(c);
		if (ns1 != 1) {
			if ((ns1 & 1) == 0) {
				const unsigned oldNu = ns1 >> 1;
				const unsigned i = U2I(oldNu);
				if (i != U2I(oldNu + 1)) {
					const uint32_t ptr = AllocUnits(i + 1);
					if (ptr == 0) {
						RestartModel();
						return;
					}
					const uint32_t oldPtr = Stats(c);
					m_mem.Copy(ptr, oldPtr, U2B(oldNu));
					InsertNode(oldPtr, i);
					SetStats(c, ptr);
				}
			}
			SetSummFreq(c,
						SummFreq(c) + (2 * ns1 < ns ? 1 : 0) +
							2 * ((4 * ns1 <= ns ? 1 : 0) & (SummFreq(c) <= 8 * ns1 ? 1 : 0)));
		} else {
			const uint32_t s = AllocUnits(0);
			if (s == 0) {
				RestartModel();
				return;
			}
			CopyState(s, OneState(c));
			SetStats(c, s);
			if (Freq(s) < MAX_FREQ / 4 - 1)
				SetFreq(s, Freq(s) << 1);
			else
				SetFreq(s, MAX_FREQ - 4);
			SetSummFreq(c, Freq(s) + m_initEsc + (ns > 3 ? 1 : 0));
		}
		uint32_t cf = 2 * Freq(m_foundState) * (SummFreq(c) + 6);
		const uint32_t sf = s0 + SummFreq(c);
		if (cf < 6 * sf) {
			cf = 1 + (cf > sf ? 1 : 0) + (cf >= 4 * sf ? 1 : 0);
			SetSummFreq(c, SummFreq(c) + 3);
		} else {
			cf = 4 + (cf >= 9 * sf ? 1 : 0) + (cf >= 12 * sf ? 1 : 0) + (cf >= 15 * sf ? 1 : 0);
			SetSummFreq(c, SummFreq(c) + cf);
		}
		const uint32_t s = Stats(c) + ns1 * 6;
		SetSuccessor(s, successor);
		SetSymbol(s, uint8_t(foundSymbol));
		SetFreq(s, cf);
		SetNumStats(c, ns1 + 1);
	}
	m_maxContext = m_minContext = fSuccessor;
}

void Ppmd7Model::Rescale() {
	const uint32_t stats = Stats(m_minContext);
	uint32_t s = m_foundState;
	{
		uint8_t tmp[6];
		for (int k = 0; k < 6; ++k)
			tmp[k] = m_mem.U8(s + uint32_t(k));
		for (; s != stats; s -= 6)
			CopyState(s, s - 6);
		for (int k = 0; k < 6; ++k)
			m_mem.SetU8(s + uint32_t(k), tmp[k]);
	}
	uint32_t escFreq = SummFreq(m_minContext) - Freq(s);
	SetFreq(s, Freq(s) + 4);
	const uint32_t adder = m_orderFall != 0 ? 1 : 0;
	SetFreq(s, (Freq(s) + adder) >> 1);
	uint32_t sumFreq = Freq(s);
	unsigned i = NumStats(m_minContext) - 1;
	do {
		s += 6;
		escFreq -= Freq(s);
		SetFreq(s, (Freq(s) + adder) >> 1);
		sumFreq += Freq(s);
		if (Freq(s) > Freq(s - 6)) {
			uint32_t s1 = s;
			uint8_t tmp[6];
			for (int k = 0; k < 6; ++k)
				tmp[k] = m_mem.U8(s1 + uint32_t(k));
			do {
				CopyState(s1, s1 - 6);
				s1 -= 6;
			} while (s1 != stats && tmp[1] > Freq(s1 - 6));
			for (int k = 0; k < 6; ++k)
				m_mem.SetU8(s1 + uint32_t(k), tmp[k]);
		}
	} while (--i);
	if (Freq(s) == 0) {
		const unsigned numStats = NumStats(m_minContext);
		do {
			++i;
			s -= 6;
		} while (Freq(s) == 0);
		escFreq += i;
		SetNumStats(m_minContext, NumStats(m_minContext) - i);
		if (NumStats(m_minContext) == 1) {
			uint8_t tmp[6];
			for (int k = 0; k < 6; ++k)
				tmp[k] = m_mem.U8(stats + uint32_t(k));
			do {
				tmp[1] = uint8_t(tmp[1] - (tmp[1] >> 1));
				escFreq >>= 1;
			} while (escFreq > 1);
			InsertNode(stats, U2I((numStats + 1) >> 1));
			m_foundState = OneState(m_minContext);
			for (int k = 0; k < 6; ++k)
				m_mem.SetU8(m_foundState + uint32_t(k), tmp[k]);
			return;
		}
		const unsigned n0 = (numStats + 1) >> 1, n1 = (NumStats(m_minContext) + 1) >> 1;
		if (n0 != n1)
			SetStats(m_minContext, ShrinkUnits(stats, n0, n1));
	}
	SetSummFreq(m_minContext, sumFreq + escFreq - (escFreq >> 1));
	m_foundState = Stats(m_minContext);
}

detail::ppmd::See* Ppmd7Model::MakeEscFreq(unsigned numMasked, uint32_t& escFreq) {
	const unsigned nonMasked = NumStats(m_minContext) - numMasked;
	if (NumStats(m_minContext) != 256) {
		detail::ppmd::See* see =
			&m_see[m_ns2Index[nonMasked - 1]]
				  [(nonMasked < NumStats(Suffix(m_minContext)) - NumStats(m_minContext) ? 1
																						: 0) +
				   2 * (SummFreq(m_minContext) < 11 * NumStats(m_minContext) ? 1 : 0) +
				   4 * (numMasked > nonMasked ? 1 : 0) + m_hiBitsFlag];
		const unsigned r = see->summ >> see->shift;
		see->summ = uint16_t(see->summ - r);
		escFreq = r + (r == 0 ? 1 : 0);
		return see;
	}
	escFreq = 1;
	return &m_dummySee;
}

void Ppmd7Model::NextContext() {
	const uint32_t c = Successor(m_foundState);
	if (m_orderFall == 0 && c > m_text)
		m_minContext = m_maxContext = c;
	else
		UpdateModel();
}

void Ppmd7Model::Update1() {
	uint32_t s = m_foundState;
	SetFreq(s, Freq(s) + 4);
	SetSummFreq(m_minContext, SummFreq(m_minContext) + 4);
	if (Freq(s) > Freq(s - 6)) {
		SwapStates(s, s - 6);
		m_foundState = s -= 6;
		if (Freq(s) > MAX_FREQ)
			Rescale();
	}
	NextContext();
}

void Ppmd7Model::Update1_0() {
	m_prevSuccess = (2 * Freq(m_foundState) > SummFreq(m_minContext)) ? 1 : 0;
	m_runLength += int32_t(m_prevSuccess);
	SetSummFreq(m_minContext, SummFreq(m_minContext) + 4);
	SetFreq(m_foundState, Freq(m_foundState) + 4);
	if (Freq(m_foundState) > MAX_FREQ)
		Rescale();
	NextContext();
}

void Ppmd7Model::UpdateBin() {
	SetFreq(m_foundState, Freq(m_foundState) + (Freq(m_foundState) < 128 ? 1 : 0));
	m_prevSuccess = 1;
	++m_runLength;
	NextContext();
}

void Ppmd7Model::Update2() {
	SetFreq(m_foundState, Freq(m_foundState) + 4);
	SetSummFreq(m_minContext, SummFreq(m_minContext) + 4);
	if (Freq(m_foundState) > MAX_FREQ)
		Rescale();
	m_runLength = m_initRl;
	UpdateModel();
}

uint32_t Ppmd7Model::BinSummIndex() {
	const uint32_t one = OneState(m_minContext);
	m_hiBitsFlag = m_hb2Flag[Symbol(m_foundState)];
	const uint32_t row = Freq(one) - 1;
	const uint32_t column = m_prevSuccess + m_ns2bsIndex[NumStats(Suffix(m_minContext)) - 1] +
							m_hiBitsFlag + 2 * m_hb2Flag[Symbol(one)] +
							((uint32_t(m_runLength) >> 26) & 0x20);
	return row * 64 + column;
}

// ── Ppmd7zRangeDecoder ───────────────────────────────────────────────────────

bool Ppmd7zRangeDecoder::Init() {
	m_code = 0;
	m_range = 0xFFFFFFFFu;
	uint8_t first = 0;
	if (!m_input->Byte(first) || first != 0)
		return false;
	for (int i = 0; i < 4; ++i)
		m_code = (m_code << 8) | Next();
	return m_code < 0xFFFFFFFFu;
}

uint32_t Ppmd7zRangeDecoder::GetThreshold(uint32_t total) {
	m_range /= total;
	return m_code / m_range;
}

void Ppmd7zRangeDecoder::Decode(uint32_t start, uint32_t size) {
	m_code -= start * m_range;
	m_range *= size;
	Normalize();
}

uint32_t Ppmd7zRangeDecoder::DecodeBit(uint32_t size0, uint32_t) {
	const uint32_t bound = (m_range >> 14) * size0;
	uint32_t symbol;
	if (m_code < bound) {
		symbol = 0;
		m_range = bound;
	} else {
		symbol = 1;
		m_code -= bound;
		m_range -= bound;
	}
	Normalize();
	return symbol;
}

uint32_t Ppmd7zRangeDecoder::Next() {
	uint8_t byte = 0;
	if (!m_input->Byte(byte))
		m_overrun = true;
	return byte;
}

void Ppmd7zRangeDecoder::Normalize() {
	if (m_range < detail::ppmd::TOP) {
		m_code = (m_code << 8) | Next();
		m_range <<= 8;
		if (m_range < detail::ppmd::TOP) {
			m_code = (m_code << 8) | Next();
			m_range <<= 8;
		}
	}
}

// ── Ppmd7RarRangeDecoder ─────────────────────────────────────────────────────

bool Ppmd7RarRangeDecoder::Init() {
	m_code = 0;
	m_low = 0;
	m_range = 0xFFFFFFFFu;
	for (int i = 0; i < 4; ++i)
		m_code = (m_code << 8) | Next();
	return m_code < 0xFFFFFFFFu;
}

uint32_t Ppmd7RarRangeDecoder::GetThreshold(uint32_t total) {
	m_range /= total;
	return (m_code - m_low) / m_range;
}

void Ppmd7RarRangeDecoder::Decode(uint32_t start, uint32_t size) {
	m_low += start * m_range;
	m_range *= size;
	Normalize();
}

uint32_t Ppmd7RarRangeDecoder::DecodeBit(uint32_t size0, uint32_t) {
	const uint32_t bound = (m_range >> 14) * size0;
	uint32_t symbol;
	if (m_code - m_low < bound) {
		symbol = 0;
		m_range = bound;
	} else {
		symbol = 1;
		m_low += bound;
		m_range = (m_range >> 14) * ((1u << 14) - size0);
	}
	Normalize();
	return symbol;
}

uint32_t Ppmd7RarRangeDecoder::Next() {
	uint8_t byte = 0;
	if (!m_input->Byte(byte))
		m_overrun = true;
	return byte;
}

void Ppmd7RarRangeDecoder::Normalize() {
	using detail::ppmd::BOT;
	using detail::ppmd::TOP;
	for (;;) {
		if ((m_low ^ (m_low + m_range)) >= TOP) {
			if (m_range >= BOT)
				break;
			m_range = (0u - m_low) & (BOT - 1);
		}
		m_code = (m_code << 8) | Next();
		m_range <<= 8;
		m_low <<= 8;
	}
}

// ── Ppmd7zRangeEncoder ───────────────────────────────────────────────────────

void Ppmd7zRangeEncoder::Encode(uint32_t start, uint32_t size, uint32_t total) {
	m_range /= total;
	m_low += uint64_t(start) * m_range;
	m_range *= size;
	while (m_range < detail::ppmd::TOP) {
		m_range <<= 8;
		ShiftLow();
	}
}

void Ppmd7zRangeEncoder::EncodeBit0(uint32_t size0) {
	m_range = (m_range >> 14) * size0;
	while (m_range < detail::ppmd::TOP) {
		m_range <<= 8;
		ShiftLow();
	}
}

void Ppmd7zRangeEncoder::EncodeBit1(uint32_t size0) {
	const uint32_t bound = (m_range >> 14) * size0;
	m_low += bound;
	m_range -= bound;
	while (m_range < detail::ppmd::TOP) {
		m_range <<= 8;
		ShiftLow();
	}
}

void Ppmd7zRangeEncoder::Flush() {
	for (int i = 0; i < 5; ++i)
		ShiftLow();
}

void Ppmd7zRangeEncoder::ShiftLow() {
	if (uint32_t(m_low) < 0xFF000000u || (m_low >> 32) != 0) {
		uint8_t temp = m_cache;
		do {
			m_out.push_back(uint8_t(temp + uint8_t(m_low >> 32)));
			temp = 0xFF;
		} while (--m_cacheSize != 0);
		m_cache = uint8_t(uint32_t(m_low) >> 24);
	}
	++m_cacheSize;
	m_low = uint32_t(m_low) << 8;
}

namespace detail::ppmd {

// ── Ppmd7StreamImpl ──────────────────────────────────────────────────────────

Result<size_t, ArchiveError> Ppmd7StreamImpl::Produce(uint8_t* out, size_t max) {
	if (!m_started) {
		m_model = std::make_unique<Ppmd7Model>();
		m_model->Init(m_memory, m_order);
		if (!m_rc.Init())
			return Err(MakeError(ErrorKind::CORRUPT, String("PPMd : début de flux invalide")));
		m_started = true;
	}
	size_t produced = 0;
	while (produced < max && m_done < m_size) {
		const int symbol = m_model->DecodeSymbol(m_rc);
		if (symbol < 0 || m_rc.Overrun())
			return Err(m_buffer.Failed()
						   ? m_buffer.Failure("PPMd")
						   : MakeError(ErrorKind::CORRUPT,
									   String(symbol == -1 ? "PPMd : fin de flux prématurée"
														   : "PPMd : données corrompues")));
		out[produced++] = uint8_t(symbol);
		++m_done;
	}
	return Ok(produced);
}

bool Ppmd7StreamImpl::Restart() {
	if (!m_buffer.Rewind())
		return false;
	m_started = false;
	m_done = 0;
	m_rc = Ppmd7zRangeDecoder(m_buffer);
	return true;
}

// ── Ppmd7EncoderImpl ─────────────────────────────────────────────────────────

Ppmd7EncoderImpl::Ppmd7EncoderImpl(ArchiveStream sink, unsigned order, uint32_t memory, bool endMarker,
		StreamStatePtr state)
	: EncoderImpl(std::move(sink), std::move(state)), m_endMarker(endMarker) {
	m_model = std::make_unique<Ppmd7Model>();
	m_model->Init(memory, order);
}

Result<bool, ArchiveError> Ppmd7EncoderImpl::Consume(const uint8_t* data, size_t size) {
	for (size_t i = 0; i < size; ++i)
		m_model->EncodeSymbol(m_rc, data[i]);
	return Drain();
}

Result<bool, ArchiveError> Ppmd7EncoderImpl::Finish() {
	if (m_endMarker)
		m_model->EncodeSymbol(m_rc, -1);
	m_rc.Flush();
	return Drain();
}

Result<bool, ArchiveError> Ppmd7EncoderImpl::Drain() {
	Bytes& out = m_rc.Output();
	auto emitted = Emit(out.data(), out.size());
	out.clear();
	return emitted;
}

} // namespace detail::ppmd

Result<ArchiveStream, ArchiveError> OpenPpmd7Stream(ArchiveStream input, unsigned order, uint32_t memory, uint64_t size) {
	if (order < 2 || order > 64 || memory < (1u << 11) || memory > 0xFFFFFFFFu - 12 * 3)
		return Err(MakeError(ErrorKind::CORRUPT, String("PPMd : propriétés invalides")));
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::ppmd::Ppmd7StreamImpl>(std::move(input), order,
																	  memory, size, state),
					  state);
}

Result<ArchiveStream, ArchiveError> OpenPpmd7Encoder(ArchiveStream sink, unsigned order, uint32_t memory, bool endMarker) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::ppmd::Ppmd7EncoderImpl>(std::move(sink), order,
																	   memory, endMarker, state),
					  state);
}

// ── Ppmd8Model ───────────────────────────────────────────────────────────────

Ppmd8Model::Ppmd8Model() {
	using namespace detail::ppmd;
	for (unsigned i = 0, k = 0; i < unsigned(NUM_INDEXES); ++i) {
		unsigned step = i >= 12 ? 4 : (i >> 2) + 1;
		do {
			m_units2Index[k++] = uint8_t(i);
		} while (--step);
		m_index2Units[i] = uint8_t(k);
	}
	m_ns2bsIndex[0] = 0 << 1;
	m_ns2bsIndex[1] = 1 << 1;
	std::fill(m_ns2bsIndex.begin() + 2, m_ns2bsIndex.begin() + 11, uint8_t(2 << 1));
	std::fill(m_ns2bsIndex.begin() + 11, m_ns2bsIndex.end(), uint8_t(3 << 1));
	unsigned i = 0;
	for (; i < 5; ++i)
		m_ns2Index[i] = uint8_t(i);
	for (unsigned m = i, k = 1; i < 260; ++i) {
		m_ns2Index[i] = uint8_t(m);
		if (--k == 0)
			k = (++m) - 4;
	}
}

void Ppmd8Model::Init(uint32_t size, unsigned maxOrder, Ppmd8Restore restore) {
	if (m_mem.Size() == 0 || m_size != size) {
		m_alignOffset = 4 - (size & 3);
		m_mem.Allocate(size_t(m_alignOffset) + size);
		m_size = size;
	}
	m_maxOrder = maxOrder;
	m_restore = restore;
	RestartModel();
	m_dummySee.shift = detail::ppmd::PERIOD_BITS;
	m_dummySee.summ = 0;
	m_dummySee.count = 64;
}

bool Ppmd8Model::DecoderInit(InputBuffer& input) {
	m_input = &input;
	m_low = 0;
	m_range = 0xFFFFFFFFu;
	m_code = 0;
	for (int i = 0; i < 4; ++i)
		m_code = (m_code << 8) | NextByte();
	return m_code < 0xFFFFFFFFu;
}

void Ppmd8Model::EncoderInit() {
	m_low = 0;
	m_range = 0xFFFFFFFFu;
	m_out.clear();
}

void Ppmd8Model::EncoderFlush() {
	for (int i = 0; i < 4; ++i, m_low <<= 8)
		m_out.push_back(uint8_t(m_low >> 24));
}

int Ppmd8Model::DecodeSymbol() {
	using namespace detail::ppmd;
	std::array<int8_t, 256> mask;
	if (NumStats(m_minContext) != 0) {
		uint32_t s = Stats(m_minContext);
		uint32_t count = DecThreshold(SummFreq(m_minContext));
		uint32_t hiCount = Freq(s);
		if (count < hiCount) {
			DecDecode(0, Freq(s));
			m_foundState = s;
			const uint8_t symbol = Symbol(s);
			Update1_0();
			return symbol;
		}
		m_prevSuccess = 0;
		unsigned i = NumStats(m_minContext);
		do {
			s += 6;
			if ((hiCount += Freq(s)) > count) {
				DecDecode(hiCount - Freq(s), Freq(s));
				m_foundState = s;
				const uint8_t symbol = Symbol(s);
				Update1();
				return symbol;
			}
		} while (--i);
		if (count >= SummFreq(m_minContext))
			return -2;
		DecDecode(hiCount, SummFreq(m_minContext) - hiCount);
		mask.fill(-1);
		mask[Symbol(s)] = 0;
		i = NumStats(m_minContext);
		do {
			s -= 6;
			mask[Symbol(s)] = 0;
		} while (--i);
	} else {
		uint16_t& prob = m_binSumm[BinSummIndex()];
		m_range >>= 14;
		if (m_code / m_range < prob) {
			DecDecode(0, prob);
			prob = UpdateProb0(prob);
			m_foundState = OneState(m_minContext);
			const uint8_t symbol = Symbol(m_foundState);
			UpdateBin();
			return symbol;
		}
		DecDecode(prob, (1u << 14) - prob);
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
		detail::ppmd::See* see = MakeEscFreq(numMasked, freqSum);
		freqSum += hiCount;
		const uint32_t count = DecThreshold(freqSum);
		if (count < hiCount) {
			size_t k = 0;
			hiCount = 0;
			while ((hiCount += Freq(states[k])) <= count)
				++k;
			s = states[k];
			DecDecode(hiCount - Freq(s), Freq(s));
			see->Update();
			m_foundState = s;
			const uint8_t symbol = Symbol(s);
			Update2();
			return symbol;
		}
		if (count >= freqSum)
			return -2;
		DecDecode(hiCount, freqSum - hiCount);
		see->summ = uint16_t(see->summ + freqSum);
		do {
			mask[Symbol(states[--i])] = 0;
		} while (i != 0);
	}
}

void Ppmd8Model::EncodeSymbol(int symbol) {
	using namespace detail::ppmd;
	std::array<int8_t, 256> mask;
	if (NumStats(m_minContext) != 0) {
		uint32_t s = Stats(m_minContext);
		if (Symbol(s) == symbol) {
			EncEncode(0, Freq(s), SummFreq(m_minContext));
			m_foundState = s;
			Update1_0();
			return;
		}
		m_prevSuccess = 0;
		uint32_t sum = Freq(s);
		unsigned i = NumStats(m_minContext);
		do {
			s += 6;
			if (Symbol(s) == symbol) {
				EncEncode(sum, Freq(s), SummFreq(m_minContext));
				m_foundState = s;
				Update1();
				return;
			}
			sum += Freq(s);
		} while (--i);
		mask.fill(-1);
		mask[Symbol(s)] = 0;
		i = NumStats(m_minContext);
		do {
			s -= 6;
			mask[Symbol(s)] = 0;
		} while (--i);
		EncEncode(sum, SummFreq(m_minContext) - sum, SummFreq(m_minContext));
	} else {
		uint16_t& prob = m_binSumm[BinSummIndex()];
		const uint32_t s = OneState(m_minContext);
		if (Symbol(s) == symbol) {
			m_range = (m_range >> 14) * prob;
			EncNormalize();
			prob = UpdateProb0(prob);
			m_foundState = s;
			UpdateBin();
			return;
		}
		m_range >>= 14;
		m_low += prob * m_range;
		m_range *= (1u << 14) - prob;
		EncNormalize();
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
				return;
			m_minContext = Suffix(m_minContext);
		} while (NumStats(m_minContext) == numMasked);
		uint32_t escFreq = 0;
		detail::ppmd::See* see = MakeEscFreq(numMasked, escFreq);
		uint32_t s = Stats(m_minContext);
		uint32_t sum = 0;
		unsigned i = NumStats(m_minContext) + 1;
		do {
			const int current = Symbol(s);
			if (current == symbol) {
				const uint32_t low = sum;
				const uint32_t found = s;
				do {
					sum += Freq(s) & uint32_t(int32_t(mask[Symbol(s)]));
					s += 6;
				} while (--i);
				EncEncode(low, Freq(found), sum + escFreq);
				see->Update();
				m_foundState = found;
				Update2();
				return;
			}
			sum += Freq(s) & uint32_t(int32_t(mask[size_t(current)]));
			mask[size_t(current)] = 0;
			s += 6;
		} while (--i);
		EncEncode(sum, escFreq, sum + escFreq);
		see->summ = uint16_t(see->summ + sum + escFreq);
	}
}

uint32_t Ppmd8Model::NextByte() {
	uint8_t byte = 0;
	if (!m_input->Byte(byte))
		m_overrun = true;
	return byte;
}

uint32_t Ppmd8Model::DecThreshold(uint32_t total) {
	m_range /= total;
	return m_code / m_range;
}

void Ppmd8Model::DecDecode(uint32_t start, uint32_t size) {
	using detail::ppmd::BOT;
	using detail::ppmd::TOP;
	start *= m_range;
	m_low += start;
	m_code -= start;
	m_range *= size;
	for (;;) {
		if ((m_low ^ (m_low + m_range)) >= TOP) {
			if (m_range >= BOT)
				break;
			m_range = (0u - m_low) & (BOT - 1);
		}
		m_code = (m_code << 8) | NextByte();
		m_range <<= 8;
		m_low <<= 8;
	}
}

void Ppmd8Model::EncNormalize() {
	using detail::ppmd::BOT;
	using detail::ppmd::TOP;
	for (;;) {
		if ((m_low ^ (m_low + m_range)) >= TOP) {
			if (m_range >= BOT)
				break;
			m_range = (0u - m_low) & (BOT - 1);
		}
		m_out.push_back(uint8_t(m_low >> 24));
		m_range <<= 8;
		m_low <<= 8;
	}
}

void Ppmd8Model::EncEncode(uint32_t start, uint32_t size, uint32_t total) {
	m_range /= total;
	m_low += start * m_range;
	m_range *= size;
	EncNormalize();
}

uint32_t Ppmd8Model::Successor(uint32_t s) const {
	return uint32_t(m_mem.U16(s + 2)) | (uint32_t(m_mem.U16(s + 4)) << 16);
}

void Ppmd8Model::SetSuccessor(uint32_t s, uint32_t v) {
	m_mem.SetU16(s + 2, uint16_t(v & 0xFFFF));
	m_mem.SetU16(s + 4, uint16_t(v >> 16));
}

void Ppmd8Model::SwapStates(uint32_t a, uint32_t b) {
	uint8_t tmp[6];
	for (int i = 0; i < 6; ++i)
		tmp[i] = m_mem.U8(a + uint32_t(i));
	m_mem.Copy(a, b, 6);
	for (int i = 0; i < 6; ++i)
		m_mem.SetU8(b + uint32_t(i), tmp[i]);
}

uint32_t Ppmd8Model::U2B(unsigned nu) noexcept {
	return uint32_t(nu) * detail::ppmd::UNIT_SIZE;
}

void Ppmd8Model::InsertNode(uint32_t node, unsigned index) {
	m_mem.SetU32(node, EMPTY_NODE);
	m_mem.SetU32(node + 4, m_freeList[index]);
	m_mem.SetU32(node + 8, I2U(index));
	m_freeList[index] = node;
	++m_stamps[index];
}

uint32_t Ppmd8Model::RemoveNode(unsigned index) {
	const uint32_t node = m_freeList[index];
	m_freeList[index] = m_mem.U32(node + 4);
	--m_stamps[index];
	return node;
}

void Ppmd8Model::SplitBlock(uint32_t ptr, unsigned oldIndex, unsigned newIndex) {
	const unsigned nu = I2U(oldIndex) - I2U(newIndex);
	ptr += U2B(I2U(newIndex));
	unsigned i = U2I(nu);
	if (I2U(i) != nu) {
		const unsigned k = I2U(--i);
		InsertNode(ptr + U2B(k), nu - k - 1);
	}
	InsertNode(ptr, i);
}

void Ppmd8Model::GlueFreeBlocks() {
	using namespace detail::ppmd;
	uint32_t head = 0;
	// `prev` : adresse d'un champ Next à renseigner (0 = la tête elle-même).
	uint32_t prevField = 0;
	bool prevIsHead = true;
	m_glueCount = 1 << 13;
	m_stamps.fill(0);
	if (m_loUnit != m_hiUnit)
		m_mem.SetU32(m_loUnit, 0);
	for (unsigned i = 0; i < unsigned(NUM_INDEXES); ++i) {
		uint32_t next = m_freeList[i];
		m_freeList[i] = 0;
		while (next != 0) {
			const uint32_t node = next;
			if (m_mem.U32(node + 8) != 0) {
				if (prevIsHead)
					head = next;
				else
					m_mem.SetU32(prevField, next);
				prevIsHead = false;
				prevField = node + 4;
				for (;;) {
					const uint32_t node2 = node + m_mem.U32(node + 8) * UNIT_SIZE;
					if (m_mem.U32(node2) != EMPTY_NODE)
						break;
					m_mem.SetU32(node + 8, m_mem.U32(node + 8) + m_mem.U32(node2 + 8));
					m_mem.SetU32(node2 + 8, 0);
				}
			}
			next = m_mem.U32(node + 4);
		}
	}
	if (prevIsHead)
		head = 0;
	else
		m_mem.SetU32(prevField, 0);
	while (head != 0) {
		uint32_t node = head;
		head = m_mem.U32(node + 4);
		unsigned nu = m_mem.U32(node + 8);
		if (nu == 0)
			continue;
		for (; nu > 128; nu -= 128, node += 128 * UNIT_SIZE)
			InsertNode(node, NUM_INDEXES - 1);
		unsigned i = U2I(nu);
		if (I2U(i) != nu) {
			const unsigned k = I2U(--i);
			InsertNode(node + k * UNIT_SIZE, nu - k - 1);
		}
		InsertNode(node, i);
	}
}

uint32_t Ppmd8Model::AllocUnitsRare(unsigned index) {
	using namespace detail::ppmd;
	if (m_glueCount == 0) {
		GlueFreeBlocks();
		if (m_freeList[index] != 0)
			return RemoveNode(index);
	}
	unsigned i = index;
	do {
		if (++i == unsigned(NUM_INDEXES)) {
			const uint32_t numBytes = U2B(I2U(index));
			--m_glueCount;
			if (m_unitsStart - m_text > numBytes) {
				m_unitsStart -= numBytes;
				return m_unitsStart;
			}
			return 0;
		}
	} while (m_freeList[i] == 0);
	const uint32_t block = RemoveNode(i);
	SplitBlock(block, i, index);
	return block;
}

uint32_t Ppmd8Model::AllocUnits(unsigned index) {
	if (m_freeList[index] != 0)
		return RemoveNode(index);
	const uint32_t numBytes = U2B(I2U(index));
	if (numBytes <= m_hiUnit - m_loUnit) {
		const uint32_t block = m_loUnit;
		m_loUnit += numBytes;
		return block;
	}
	return AllocUnitsRare(index);
}

uint32_t Ppmd8Model::ShrinkUnits(uint32_t oldPtr, unsigned oldNu, unsigned newNu) {
	const unsigned i0 = U2I(oldNu), i1 = U2I(newNu);
	if (i0 == i1)
		return oldPtr;
	if (m_freeList[i1] != 0) {
		const uint32_t ptr = RemoveNode(i1);
		m_mem.Copy(ptr, oldPtr, U2B(newNu));
		InsertNode(oldPtr, i0);
		return ptr;
	}
	SplitBlock(oldPtr, i0, i1);
	return oldPtr;
}

void Ppmd8Model::SpecialFreeUnit(uint32_t ptr) {
	if (ptr != m_unitsStart)
		InsertNode(ptr, 0);
	else
		m_unitsStart += detail::ppmd::UNIT_SIZE;
}

uint32_t Ppmd8Model::MoveUnitsUp(uint32_t oldPtr, unsigned nu) {
	const unsigned index = U2I(nu);
	if (oldPtr > m_unitsStart + 16 * 1024 || oldPtr > m_freeList[index])
		return oldPtr;
	const uint32_t ptr = RemoveNode(index);
	m_mem.Copy(ptr, oldPtr, U2B(nu));
	if (oldPtr != m_unitsStart)
		InsertNode(oldPtr, index);
	else
		m_unitsStart += U2B(I2U(index));
	return ptr;
}

void Ppmd8Model::ExpandTextArea() {
	using namespace detail::ppmd;
	std::array<uint32_t, NUM_INDEXES> count{};
	if (m_loUnit != m_hiUnit)
		m_mem.SetU32(m_loUnit, 0);
	uint32_t node = m_unitsStart;
	for (; m_mem.U32(node) == EMPTY_NODE; node += m_mem.U32(node + 8) * UNIT_SIZE) {
		m_mem.SetU32(node, 0);
		++count[U2I(m_mem.U32(node + 8))];
	}
	m_unitsStart = node;
	for (unsigned i = 0; i < unsigned(NUM_INDEXES); ++i) {
		// `link` : champ contenant le lien courant (liste libre ou Next d'un
		// nœud).
		bool linkIsHead = true;
		uint32_t linkField = 0;
		auto link = [&]() { return linkIsHead ? m_freeList[i] : m_mem.U32(linkField); };
		auto setLink = [&](uint32_t v) {
			if (linkIsHead)
				m_freeList[i] = v;
			else
				m_mem.SetU32(linkField, v);
		};
		while (count[i] != 0) {
			uint32_t n = link();
			while (m_mem.U32(n) == 0) {
				setLink(m_mem.U32(n + 4));
				n = link();
				--m_stamps[i];
				if (--count[i] == 0)
					break;
			}
			linkIsHead = false;
			linkField = n + 4;
		}
	}
}

void Ppmd8Model::RestartModel() {
	using namespace detail::ppmd;
	m_freeList.fill(0);
	m_stamps.fill(0);
	m_text = m_alignOffset;
	m_hiUnit = m_text + m_size;
	m_loUnit = m_unitsStart = m_hiUnit - m_size / 8 / UNIT_SIZE * 7 * UNIT_SIZE;
	m_glueCount = 0;
	m_orderFall = m_maxOrder;
	m_runLength = m_initRl = -int32_t(m_maxOrder < 12 ? m_maxOrder : 12) - 1;
	m_prevSuccess = 0;
	m_hiUnit -= UNIT_SIZE;
	m_minContext = m_maxContext = m_hiUnit;
	SetSuffix(m_minContext, 0);
	SetNumStats(m_minContext, 255);
	SetFlags(m_minContext, 0);
	SetSummFreq(m_minContext, 256 + 1);
	m_foundState = m_loUnit;
	SetStats(m_minContext, m_foundState);
	m_loUnit += U2B(256 / 2);
	for (unsigned i = 0; i < 256; ++i) {
		const uint32_t s = m_foundState + i * 6;
		SetSymbol(s, uint8_t(i));
		SetFreq(s, 1);
		SetSuccessor(s, 0);
	}
	for (unsigned i = 0, m = 0; m < 25; ++m) {
		while (m_ns2Index[i] == m)
			++i;
		for (unsigned k = 0; k < 8; ++k) {
			const uint16_t value = uint16_t(BIN_SCALE - INIT_BIN_ESC[k] / (i + 1));
			for (unsigned r = 0; r < 64; r += 8)
				m_binSumm[m * 64 + k + r] = value;
		}
	}
	for (unsigned i = 0, m = 0; m < 24; ++m) {
		while (m_ns2Index[i + 3] == m + 3)
			++i;
		for (unsigned k = 0; k < 32; ++k) {
			See& s = m_see[m][k];
			s.shift = PERIOD_BITS - 4;
			s.summ = uint16_t((2 * i + 5) << s.shift);
			s.count = 7;
		}
	}
}

void Ppmd8Model::Refresh(uint32_t ctx, unsigned oldNu, unsigned scale) {
	unsigned i = NumStats(ctx);
	uint32_t s = ShrinkUnits(Stats(ctx), oldNu, (i + 2) >> 1);
	SetStats(ctx, s);
	unsigned flags = (Flags(ctx) & (0x10 + 0x04 * scale)) + 0x08 * (Symbol(s) >= 0x40 ? 1 : 0);
	uint32_t escFreq = SummFreq(ctx) - Freq(s);
	SetFreq(s, (Freq(s) + scale) >> scale);
	uint32_t sumFreq = Freq(s);
	do {
		s += 6;
		escFreq -= Freq(s);
		SetFreq(s, (Freq(s) + scale) >> scale);
		sumFreq += Freq(s);
		flags |= 0x08 * (Symbol(s) >= 0x40 ? 1 : 0);
	} while (--i);
	SetSummFreq(ctx, sumFreq + ((escFreq + scale) >> scale));
	SetFlags(ctx, flags);
}

uint32_t Ppmd8Model::CutOff(uint32_t ctx, unsigned order) {
	if (NumStats(ctx) == 0) {
		const uint32_t s = OneState(ctx);
		if (Successor(s) >= m_unitsStart) {
			if (order < m_maxOrder)
				SetSuccessor(s, CutOff(Successor(s), order + 1));
			else
				SetSuccessor(s, 0);
			if (Successor(s) || order <= 9)
				return ctx;
		}
		SpecialFreeUnit(ctx);
		return 0;
	}
	const unsigned tmp = (NumStats(ctx) + 2) >> 1;
	SetStats(ctx, MoveUnitsUp(Stats(ctx), tmp));
	int i = int(NumStats(ctx));
	for (uint32_t s = Stats(ctx) + uint32_t(i) * 6;; s -= 6) {
		if (Successor(s) < m_unitsStart) {
			const uint32_t s2 = Stats(ctx) + uint32_t(i--) * 6;
			SetSuccessor(s, 0);
			SwapStates(s, s2);
		} else if (order < m_maxOrder) {
			SetSuccessor(s, CutOff(Successor(s), order + 1));
		} else {
			SetSuccessor(s, 0);
		}
		if (s == Stats(ctx))
			break;
	}
	if (i != int(NumStats(ctx)) && order) {
		SetNumStats(ctx, unsigned(i));
		const uint32_t s = Stats(ctx);
		if (i < 0) {
			FreeUnits(s, tmp);
			SpecialFreeUnit(ctx);
			return 0;
		}
		if (i == 0) {
			SetFlags(ctx, (Flags(ctx) & 0x10) + 0x08 * (Symbol(s) >= 0x40 ? 1 : 0));
			CopyState(OneState(ctx), s);
			FreeUnits(s, tmp);
			SetFreq(OneState(ctx), (Freq(OneState(ctx)) + 11) >> 3);
		} else {
			Refresh(ctx, tmp, SummFreq(ctx) > 16 * unsigned(i) ? 1 : 0);
		}
	}
	return ctx;
}

uint32_t Ppmd8Model::GetUsedMemory() const {
	uint32_t v = 0;
	for (unsigned i = 0; i < unsigned(detail::ppmd::NUM_INDEXES); ++i)
		v += m_stamps[i] * I2U(i);
	return m_size - (m_hiUnit - m_loUnit) - (m_unitsStart - m_text) - U2B(v);
}

void Ppmd8Model::RestoreModel(uint32_t c1) {
	m_text = m_alignOffset;
	uint32_t c = m_maxContext;
	for (; c != c1; c = Suffix(c)) {
		SetNumStats(c, NumStats(c) - 1);
		if (NumStats(c) == 0) {
			const uint32_t s = Stats(c);
			SetFlags(c, (Flags(c) & 0x10) + 0x08 * (Symbol(s) >= 0x40 ? 1 : 0));
			CopyState(OneState(c), s);
			SpecialFreeUnit(s);
			SetFreq(OneState(c), (Freq(OneState(c)) + 11) >> 3);
		} else {
			Refresh(c, (NumStats(c) + 3) >> 1, 0);
		}
	}
	for (; c != m_minContext; c = Suffix(c)) {
		if (NumStats(c) == 0) {
			SetFreq(OneState(c), Freq(OneState(c)) - (Freq(OneState(c)) >> 1));
		} else {
			SetSummFreq(c, SummFreq(c) + 4);
			if (SummFreq(c) > 128 + 4 * NumStats(c))
				Refresh(c, (NumStats(c) + 2) >> 1, 1);
		}
	}
	if (m_restore == Ppmd8Restore::RESTART || GetUsedMemory() < (m_size >> 1)) {
		RestartModel();
	} else {
		while (Suffix(m_maxContext))
			m_maxContext = Suffix(m_maxContext);
		do {
			CutOff(m_maxContext, 0);
			ExpandTextArea();
		} while (GetUsedMemory() > 3 * (m_size >> 2));
		m_glueCount = 0;
		m_orderFall = m_maxOrder;
	}
}

uint32_t Ppmd8Model::CreateSuccessors(bool skip, uint32_t s1, uint32_t c) {
	const uint32_t upBranch = Successor(m_foundState);
	std::array<uint32_t, MAX_ORDER + 1> ps{};
	unsigned numPs = 0;
	if (!skip)
		ps[numPs++] = m_foundState;
	while (Suffix(c)) {
		c = Suffix(c);
		uint32_t s;
		if (s1) {
			s = s1;
			s1 = 0;
		} else if (NumStats(c) != 0) {
			for (s = Stats(c); Symbol(s) != Symbol(m_foundState); s += 6) {
			}
			if (Freq(s) < MAX_FREQ - 9) {
				SetFreq(s, Freq(s) + 1);
				SetSummFreq(c, SummFreq(c) + 1);
			}
		} else {
			s = OneState(c);
			SetFreq(s, Freq(s) +
						   ((NumStats(Suffix(c)) == 0 ? 1u : 0u) & (Freq(s) < 24 ? 1u : 0u)));
		}
		const uint32_t successor = Successor(s);
		if (successor != upBranch) {
			c = successor;
			if (numPs == 0)
				return c;
			break;
		}
		ps[numPs++] = s;
	}
	const uint8_t upSymbol = m_mem.U8(upBranch);
	const uint32_t upSuccessor = upBranch + 1;
	const unsigned flags =
		0x10 * (Symbol(m_foundState) >= 0x40 ? 1 : 0) + 0x08 * (upSymbol >= 0x40 ? 1 : 0);
	uint32_t upFreq;
	if (NumStats(c) == 0) {
		upFreq = Freq(OneState(c));
	} else {
		uint32_t s;
		for (s = Stats(c); Symbol(s) != upSymbol; s += 6) {
		}
		const uint32_t cf = Freq(s) - 1;
		const uint32_t s0 = SummFreq(c) - NumStats(c) - cf;
		upFreq = 1 + ((2 * cf <= s0) ? (5 * cf > s0 ? 1 : 0) : ((cf + 2 * s0 - 3) / s0));
	}
	do {
		uint32_t c1;
		if (m_hiUnit != m_loUnit) {
			m_hiUnit -= detail::ppmd::UNIT_SIZE;
			c1 = m_hiUnit;
		} else if (m_freeList[0] != 0) {
			c1 = RemoveNode(0);
		} else {
			c1 = AllocUnitsRare(0);
			if (c1 == 0)
				return 0;
		}
		SetNumStats(c1, 0);
		SetFlags(c1, flags);
		const uint32_t one = OneState(c1);
		SetSymbol(one, upSymbol);
		SetFreq(one, upFreq);
		SetSuccessor(one, upSuccessor);
		SetSuffix(c1, c);
		SetSuccessor(ps[--numPs], c1);
		c = c1;
	} while (numPs != 0);
	return c;
}

uint32_t Ppmd8Model::ReduceOrder(uint32_t s1, uint32_t c) {
	uint32_t s = 0;
	const uint32_t c1 = c;
	const uint32_t upBranch = m_text;
	SetSuccessor(m_foundState, upBranch);
	++m_orderFall;
	for (;;) {
		if (s1) {
			c = Suffix(c);
			s = s1;
			s1 = 0;
		} else {
			if (!Suffix(c))
				return c;
			c = Suffix(c);
			if (NumStats(c)) {
				s = Stats(c);
				if (Symbol(s) != Symbol(m_foundState))
					do {
						s += 6;
					} while (Symbol(s) != Symbol(m_foundState));
				if (Freq(s) < MAX_FREQ - 9) {
					SetFreq(s, Freq(s) + 2);
					SetSummFreq(c, SummFreq(c) + 2);
				}
			} else {
				s = OneState(c);
				SetFreq(s, Freq(s) + (Freq(s) < 32 ? 1 : 0));
			}
		}
		if (Successor(s))
			break;
		SetSuccessor(s, upBranch);
		++m_orderFall;
	}
	if (Successor(s) <= upBranch) {
		const uint32_t saved = m_foundState;
		m_foundState = s;
		const uint32_t successor = CreateSuccessors(false, 0, c);
		SetSuccessor(s, successor);
		m_foundState = saved;
	}
	if (m_orderFall == 1 && c1 == m_maxContext) {
		SetSuccessor(m_foundState, Successor(s));
		--m_text;
	}
	return Successor(s);
}

void Ppmd8Model::UpdateModel() {
	uint32_t fSuccessor = Successor(m_foundState);
	const uint32_t fFreq = Freq(m_foundState);
	const uint8_t fSymbol = Symbol(m_foundState);
	uint32_t s = 0;
	if (fFreq < MAX_FREQ / 4 && Suffix(m_minContext) != 0) {
		const uint32_t c = Suffix(m_minContext);
		if (NumStats(c) == 0) {
			s = OneState(c);
			if (Freq(s) < 32)
				SetFreq(s, Freq(s) + 1);
		} else {
			s = Stats(c);
			if (Symbol(s) != fSymbol) {
				do {
					s += 6;
				} while (Symbol(s) != fSymbol);
				if (Freq(s) >= Freq(s - 6)) {
					SwapStates(s, s - 6);
					s -= 6;
				}
			}
			if (Freq(s) < MAX_FREQ - 9) {
				SetFreq(s, Freq(s) + 2);
				SetSummFreq(c, SummFreq(c) + 2);
			}
		}
	}
	uint32_t c = m_maxContext;
	if (m_orderFall == 0 && fSuccessor) {
		const uint32_t cs = CreateSuccessors(true, s, m_minContext);
		if (cs == 0) {
			SetSuccessor(m_foundState, 0);
			RestoreModel(c);
		} else {
			SetSuccessor(m_foundState, cs);
			m_maxContext = cs;
		}
		return;
	}
	m_mem.SetU8(m_text++, fSymbol);
	uint32_t successor = m_text;
	if (m_text >= m_unitsStart) {
		RestoreModel(c);
		return;
	}
	if (!fSuccessor) {
		const uint32_t cs = ReduceOrder(s, m_minContext);
		if (cs == 0) {
			RestoreModel(c);
			return;
		}
		fSuccessor = cs;
	} else if (fSuccessor < m_unitsStart) {
		const uint32_t cs = CreateSuccessors(false, s, m_minContext);
		if (cs == 0) {
			RestoreModel(c);
			return;
		}
		fSuccessor = cs;
	}
	if (--m_orderFall == 0) {
		successor = fSuccessor;
		m_text -= (m_maxContext != m_minContext) ? 1 : 0;
	}
	const unsigned ns = NumStats(m_minContext);
	const uint32_t s0 = SummFreq(m_minContext) - ns - fFreq;
	const unsigned flag = 0x08 * (fSymbol >= 0x40 ? 1 : 0);
	for (; c != m_minContext; c = Suffix(c)) {
		const unsigned ns1 = NumStats(c);
		if (ns1 != 0) {
			if ((ns1 & 1) != 0) {
				const unsigned oldNu = (ns1 + 1) >> 1;
				const unsigned i = U2I(oldNu);
				if (i != U2I(oldNu + 1)) {
					const uint32_t ptr = AllocUnits(i + 1);
					if (ptr == 0) {
						RestoreModel(c);
						return;
					}
					const uint32_t oldPtr = Stats(c);
					m_mem.Copy(ptr, oldPtr, U2B(oldNu));
					InsertNode(oldPtr, i);
					SetStats(c, ptr);
				}
			}
			SetSummFreq(c, SummFreq(c) + (3 * ns1 + 1 < ns ? 1 : 0));
		} else {
			const uint32_t s2 = AllocUnits(0);
			if (s2 == 0) {
				RestoreModel(c);
				return;
			}
			CopyState(s2, OneState(c));
			SetStats(c, s2);
			if (Freq(s2) < MAX_FREQ / 4 - 1)
				SetFreq(s2, Freq(s2) << 1);
			else
				SetFreq(s2, MAX_FREQ - 4);
			SetSummFreq(c, Freq(s2) + m_initEsc + (ns > 2 ? 1 : 0));
		}
		uint32_t cf = 2 * fFreq * (SummFreq(c) + 6);
		const uint32_t sf = s0 + SummFreq(c);
		if (cf < 6 * sf) {
			cf = 1 + (cf > sf ? 1 : 0) + (cf >= 4 * sf ? 1 : 0);
			SetSummFreq(c, SummFreq(c) + 4);
		} else {
			cf = 4 + (cf > 9 * sf ? 1 : 0) + (cf > 12 * sf ? 1 : 0) + (cf > 15 * sf ? 1 : 0);
			SetSummFreq(c, SummFreq(c) + cf);
		}
		const uint32_t s2 = Stats(c) + (ns1 + 1) * 6;
		SetSuccessor(s2, successor);
		SetSymbol(s2, fSymbol);
		SetFreq(s2, cf);
		SetFlags(c, Flags(c) | flag);
		SetNumStats(c, ns1 + 1);
	}
	m_maxContext = m_minContext = fSuccessor;
}

void Ppmd8Model::Rescale() {
	const uint32_t stats = Stats(m_minContext);
	uint32_t s = m_foundState;
	{
		uint8_t tmp[6];
		for (int k = 0; k < 6; ++k)
			tmp[k] = m_mem.U8(s + uint32_t(k));
		for (; s != stats; s -= 6)
			CopyState(s, s - 6);
		for (int k = 0; k < 6; ++k)
			m_mem.SetU8(s + uint32_t(k), tmp[k]);
	}
	uint32_t escFreq = SummFreq(m_minContext) - Freq(s);
	SetFreq(s, Freq(s) + 4);
	const uint32_t adder = m_orderFall != 0 ? 1 : 0;
	SetFreq(s, (Freq(s) + adder) >> 1);
	uint32_t sumFreq = Freq(s);
	unsigned i = NumStats(m_minContext);
	do {
		s += 6;
		escFreq -= Freq(s);
		SetFreq(s, (Freq(s) + adder) >> 1);
		sumFreq += Freq(s);
		if (Freq(s) > Freq(s - 6)) {
			uint32_t s1 = s;
			uint8_t tmp[6];
			for (int k = 0; k < 6; ++k)
				tmp[k] = m_mem.U8(s1 + uint32_t(k));
			do {
				CopyState(s1, s1 - 6);
				s1 -= 6;
			} while (s1 != stats && tmp[1] > Freq(s1 - 6));
			for (int k = 0; k < 6; ++k)
				m_mem.SetU8(s1 + uint32_t(k), tmp[k]);
		}
	} while (--i);
	if (Freq(s) == 0) {
		const unsigned numStats = NumStats(m_minContext);
		do {
			++i;
			s -= 6;
		} while (Freq(s) == 0);
		escFreq += i;
		SetNumStats(m_minContext, NumStats(m_minContext) - i);
		if (NumStats(m_minContext) == 0) {
			uint8_t tmp[6];
			for (int k = 0; k < 6; ++k)
				tmp[k] = m_mem.U8(stats + uint32_t(k));
			tmp[1] = uint8_t((2 * tmp[1] + escFreq - 1) / escFreq);
			if (tmp[1] > MAX_FREQ / 3)
				tmp[1] = uint8_t(MAX_FREQ / 3);
			InsertNode(stats, U2I((numStats + 2) >> 1));
			SetFlags(m_minContext,
					 (Flags(m_minContext) & 0x10) + 0x08 * (tmp[0] >= 0x40 ? 1 : 0));
			m_foundState = OneState(m_minContext);
			for (int k = 0; k < 6; ++k)
				m_mem.SetU8(m_foundState + uint32_t(k), tmp[k]);
			return;
		}
		const unsigned n0 = (numStats + 2) >> 1, n1 = (NumStats(m_minContext) + 2) >> 1;
		if (n0 != n1)
			SetStats(m_minContext, ShrinkUnits(stats, n0, n1));
		SetFlags(m_minContext, Flags(m_minContext) & ~0x08u);
		s = Stats(m_minContext);
		SetFlags(m_minContext, Flags(m_minContext) | 0x08 * (Symbol(s) >= 0x40 ? 1 : 0));
		i = NumStats(m_minContext);
		do {
			s += 6;
			SetFlags(m_minContext, Flags(m_minContext) | 0x08 * (Symbol(s) >= 0x40 ? 1 : 0));
		} while (--i);
	}
	SetSummFreq(m_minContext, sumFreq + escFreq - (escFreq >> 1));
	SetFlags(m_minContext, Flags(m_minContext) | 0x4);
	m_foundState = Stats(m_minContext);
}

detail::ppmd::See* Ppmd8Model::MakeEscFreq(unsigned numMasked1, uint32_t& escFreq) {
	if (NumStats(m_minContext) != 0xFF) {
		detail::ppmd::See* see =
			&m_see[m_ns2Index[NumStats(m_minContext) + 2] - 3]
				  [(SummFreq(m_minContext) > 11 * (NumStats(m_minContext) + 1) ? 1 : 0) +
				   2 * (2 * NumStats(m_minContext) < NumStats(Suffix(m_minContext)) + numMasked1
							? 1
							: 0) +
				   Flags(m_minContext)];
		const unsigned r = see->summ >> see->shift;
		see->summ = uint16_t(see->summ - r);
		escFreq = r + (r == 0 ? 1 : 0);
		return see;
	}
	escFreq = 1;
	return &m_dummySee;
}

void Ppmd8Model::NextContext() {
	const uint32_t c = Successor(m_foundState);
	if (m_orderFall == 0 && c >= m_unitsStart) {
		m_minContext = m_maxContext = c;
	} else {
		UpdateModel();
		m_minContext = m_maxContext;
	}
}

void Ppmd8Model::Update1() {
	uint32_t s = m_foundState;
	SetFreq(s, Freq(s) + 4);
	SetSummFreq(m_minContext, SummFreq(m_minContext) + 4);
	if (Freq(s) > Freq(s - 6)) {
		SwapStates(s, s - 6);
		m_foundState = s -= 6;
		if (Freq(s) > MAX_FREQ)
			Rescale();
	}
	NextContext();
}

void Ppmd8Model::Update1_0() {
	m_prevSuccess = (2 * Freq(m_foundState) >= SummFreq(m_minContext)) ? 1 : 0;
	m_runLength += int32_t(m_prevSuccess);
	SetSummFreq(m_minContext, SummFreq(m_minContext) + 4);
	SetFreq(m_foundState, Freq(m_foundState) + 4);
	if (Freq(m_foundState) > MAX_FREQ)
		Rescale();
	NextContext();
}

void Ppmd8Model::UpdateBin() {
	SetFreq(m_foundState, Freq(m_foundState) + (Freq(m_foundState) < 196 ? 1 : 0));
	m_prevSuccess = 1;
	++m_runLength;
	NextContext();
}

void Ppmd8Model::Update2() {
	SetSummFreq(m_minContext, SummFreq(m_minContext) + 4);
	SetFreq(m_foundState, Freq(m_foundState) + 4);
	if (Freq(m_foundState) > MAX_FREQ)
		Rescale();
	m_runLength = m_initRl;
	UpdateModel();
	m_minContext = m_maxContext;
}

uint32_t Ppmd8Model::BinSummIndex() const {
	const uint32_t one = OneState(m_minContext);
	const uint32_t row = m_ns2Index[Freq(one) - 1];
	const uint32_t column = m_ns2bsIndex[NumStats(Suffix(m_minContext))] + m_prevSuccess +
							Flags(m_minContext) + ((uint32_t(m_runLength) >> 26) & 0x20);
	return row * 64 + column;
}

namespace detail::ppmd {

// ── Ppmd8StreamImpl ──────────────────────────────────────────────────────────

Result<size_t, ArchiveError> Ppmd8StreamImpl::Produce(uint8_t* out, size_t max) {
	if (!m_started) {
		uint8_t header[2];
		if (m_buffer.ReadRaw(header, 2) != 2)
			return Err(MakeError(ErrorKind::CORRUPT, String("PPMd (zip) : en-tête tronqué")));
		const unsigned value = unsigned(header[0]) | (unsigned(header[1]) << 8);
		const unsigned order = (value & 0xF) + 1;
		const uint32_t memory = (((value >> 4) & 0xFF) + 1) << 20;
		const unsigned restore = value >> 12;
		if (order < 2)
			return Err(MakeError(ErrorKind::CORRUPT, String("PPMd (zip) : ordre invalide")));
		if (restore > 1)
			return Err(
				MakeError(ErrorKind::UNSUPPORTED,
						  String("PPMd (zip) : méthode de restauration « freeze » non gérée")));
		m_model = std::make_unique<Ppmd8Model>();
		m_model->Init(memory, order,
					  restore == 1 ? Ppmd8Restore::CUT_OFF : Ppmd8Restore::RESTART);
		if (!m_model->DecoderInit(m_buffer))
			return Err(
				MakeError(ErrorKind::CORRUPT, String("PPMd (zip) : début de flux invalide")));
		m_started = true;
	}
	size_t produced = 0;
	while (produced < max && m_done < m_size) {
		const int symbol = m_model->DecodeSymbol();
		if (symbol < 0 || m_model->Overrun())
			return Err(m_buffer.Failed()
						   ? m_buffer.Failure("PPMd")
						   : MakeError(ErrorKind::CORRUPT,
									   String(symbol == -1 ? "PPMd : fin de flux prématurée"
														   : "PPMd : données corrompues")));
		out[produced++] = uint8_t(symbol);
		++m_done;
	}
	return Ok(produced);
}

bool Ppmd8StreamImpl::Restart() {
	if (!m_buffer.Rewind())
		return false;
	m_started = false;
	m_done = 0;
	return true;
}

// ── Ppmd8EncoderImpl ─────────────────────────────────────────────────────────

Ppmd8EncoderImpl::Ppmd8EncoderImpl(ArchiveStream sink, unsigned order, uint32_t memoryMb, Ppmd8Restore restore,
		StreamStatePtr state)
	: EncoderImpl(std::move(sink), std::move(state)) {
	m_header[0] = uint8_t((order - 1) | ((memoryMb - 1) << 4));
	m_header[1] = uint8_t(((memoryMb - 1) >> 4) | (unsigned(restore) << 4));
	m_model = std::make_unique<Ppmd8Model>();
	m_model->Init(memoryMb << 20, order, restore);
	m_model->EncoderInit();
}

Result<bool, ArchiveError> Ppmd8EncoderImpl::Consume(const uint8_t* data, size_t size) {
	if (auto header = WriteHeader(); header.IsError())
		return header;
	for (size_t i = 0; i < size; ++i)
		m_model->EncodeSymbol(data[i]);
	return Drain();
}

Result<bool, ArchiveError> Ppmd8EncoderImpl::Finish() {
	if (auto header = WriteHeader(); header.IsError())
		return header;
	m_model->EncodeSymbol(-1); // marqueur de fin, comme 7-Zip
	m_model->EncoderFlush();
	return Drain();
}

Result<bool, ArchiveError> Ppmd8EncoderImpl::WriteHeader() {
	if (m_headerWritten)
		return Ok(true);
	m_headerWritten = true;
	return Emit(m_header, 2);
}

Result<bool, ArchiveError> Ppmd8EncoderImpl::Drain() {
	Bytes& out = m_model->EncoderOutput();
	auto emitted = Emit(out.data(), out.size());
	out.clear();
	return emitted;
}

} // namespace detail::ppmd

Result<ArchiveStream, ArchiveError> OpenPpmd8ZipStream(ArchiveStream input, uint64_t size) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(
		std::make_unique<detail::ppmd::Ppmd8StreamImpl>(std::move(input), size, state), state);
}

Result<ArchiveStream, ArchiveError> OpenPpmd8ZipEncoder(ArchiveStream sink, unsigned order, uint32_t memoryMb,
		Ppmd8Restore restore) {
	if (order < 2 || order > 16 || memoryMb < 1 || memoryMb > 256)
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT,
							 String("PPMd (zip) : ordre ou mémoire hors bornes")));
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::ppmd::Ppmd8EncoderImpl>(std::move(sink), order,
																	   memoryMb, restore, state),
					  state);
}

} // namespace data::archive
