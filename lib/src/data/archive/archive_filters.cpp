// Définitions de data/archive/archive_filters.hpp
#include "data/archive/archive_filters.hpp"

namespace data::archive {

const char* BranchKindName(BranchKind kind) noexcept {
	switch (kind) {
	case BranchKind::X86:
		return "bcj";
	case BranchKind::POWERPC:
		return "ppc";
	case BranchKind::IA64:
		return "ia64";
	case BranchKind::ARM:
		return "arm";
	case BranchKind::ARM_THUMB:
		return "armt";
	case BranchKind::SPARC:
		return "sparc";
	case BranchKind::ARM64:
		return "arm64";
	}
	return "?";
}

// ── BranchConverter ──────────────────────────────────────────────────────────

size_t BranchConverter::Convert(uint8_t* data, size_t size) noexcept {
	size_t processed = 0;
	switch (m_kind) {
	case BranchKind::X86:
		processed = X86(data, size);
		break;
	case BranchKind::POWERPC:
		processed = PowerPc(data, size);
		break;
	case BranchKind::IA64:
		processed = Ia64(data, size);
		break;
	case BranchKind::ARM:
		processed = Arm(data, size);
		break;
	case BranchKind::ARM_THUMB:
		processed = ArmThumb(data, size);
		break;
	case BranchKind::SPARC:
		processed = Sparc(data, size);
		break;
	case BranchKind::ARM64:
		processed = Arm64(data, size);
		break;
	}
	m_ip += uint32_t(processed);
	return processed;
}

uint32_t BranchConverter::Destination(uint32_t source, uint32_t pc) const noexcept {
	return m_encoding ? pc + source : source - pc;
}

size_t BranchConverter::X86(uint8_t* data, size_t size) noexcept {
	static constexpr uint8_t ALLOWED[8] = {1, 1, 1, 0, 1, 0, 0, 0};
	static constexpr uint8_t BIT_NUMBER[8] = {0, 1, 2, 2, 3, 3, 3, 3};
	auto isMsByte = [](uint8_t b) { return b == 0 || b == 0xFF; };
	if (size < 5)
		return 0;
	const uint32_t ip = m_ip + 5;
	uint32_t previousMask = m_x86State & 0x7;
	size_t position = 0;
	size_t previousPosition = size_t(0) - 1;
	for (;;) {
		while (position < size - 4 && (data[position] & 0xFE) != 0xE8)
			++position;
		if (position >= size - 4)
			break;
		const size_t gap = position - previousPosition;
		if (gap > 3) {
			previousMask = 0;
		} else {
			previousMask = (previousMask << (gap - 1)) & 0x7;
			if (previousMask != 0) {
				const uint8_t b = data[position + 4 - BIT_NUMBER[previousMask]];
				if (!ALLOWED[previousMask] || isMsByte(b)) {
					previousPosition = position;
					previousMask = ((previousMask << 1) & 0x7) | 1;
					++position;
					continue;
				}
			}
		}
		previousPosition = position;
		if (isMsByte(data[position + 4])) {
			uint32_t source =
				uint32_t(data[position + 1]) | (uint32_t(data[position + 2]) << 8) |
				(uint32_t(data[position + 3]) << 16) | (uint32_t(data[position + 4]) << 24);
			uint32_t destination = 0;
			for (;;) {
				destination = Destination(source, ip + uint32_t(position));
				if (previousMask == 0)
					break;
				const unsigned index = BIT_NUMBER[previousMask] * 8u;
				const uint8_t b = uint8_t(destination >> (24 - index));
				if (!isMsByte(b))
					break;
				source = destination ^ ((uint32_t(1) << (32 - index)) - 1);
			}
			data[position + 4] = uint8_t(~(((destination >> 24) & 1) - 1));
			data[position + 3] = uint8_t(destination >> 16);
			data[position + 2] = uint8_t(destination >> 8);
			data[position + 1] = uint8_t(destination);
			position += 5;
		} else {
			previousMask = ((previousMask << 1) & 0x7) | 1;
			++position;
		}
	}
	const size_t gap = position - previousPosition;
	m_x86State = gap > 3 ? 0 : ((previousMask << (gap - 1)) & 0x7);
	return position;
}

size_t BranchConverter::PowerPc(uint8_t* data, size_t size) noexcept {
	size_t i = 0;
	for (; i + 4 <= size; i += 4) {
		if ((data[i] >> 2) != 0x12 || (data[i + 3] & 3) != 1)
			continue;
		const uint32_t source = (uint32_t(data[i] & 3) << 24) | (uint32_t(data[i + 1]) << 16) |
								(uint32_t(data[i + 2]) << 8) | uint32_t(data[i + 3] & ~3u);
		const uint32_t destination = Destination(source, m_ip + uint32_t(i));
		data[i] = uint8_t(0x48 | ((destination >> 24) & 0x3));
		data[i + 1] = uint8_t(destination >> 16);
		data[i + 2] = uint8_t(destination >> 8);
		data[i + 3] = uint8_t((data[i + 3] & 0x3) | (destination & ~3u));
	}
	return i;
}

size_t BranchConverter::Ia64(uint8_t* data, size_t size) noexcept {
	static constexpr uint8_t BRANCH_TABLE[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
												 0, 0, 0, 0, 0, 4, 4, 6, 6, 0, 0,
												 7, 7, 4, 4, 0, 0, 4, 4, 0, 0};
	size_t i = 0;
	for (; i + 16 <= size; i += 16) {
		const uint32_t mask = BRANCH_TABLE[data[i] & 0x1F];
		uint32_t bitPosition = 5;
		for (int slot = 0; slot < 3; ++slot, bitPosition += 41) {
			if (((mask >> slot) & 1) == 0)
				continue;
			const uint32_t bytePosition = bitPosition >> 3;
			const uint32_t bitRest = bitPosition & 0x7;
			uint64_t instruction = 0;
			for (int j = 0; j < 6; ++j)
				instruction |= uint64_t(data[i + size_t(j) + bytePosition]) << (8 * j);
			uint64_t normalized = instruction >> bitRest;
			if (((normalized >> 37) & 0xF) != 0x5 || ((normalized >> 9) & 0x7) != 0)
				continue;
			uint32_t source = uint32_t((normalized >> 13) & 0xFFFFF);
			source |= (uint32_t(normalized >> 36) & 1) << 20;
			source <<= 4;
			uint32_t destination = Destination(source, m_ip + uint32_t(i)) >> 4;
			normalized &= ~(uint64_t(0x8FFFFF) << 13);
			normalized |= uint64_t(destination & 0xFFFFF) << 13;
			normalized |= uint64_t(destination & 0x100000) << (36 - 20);
			instruction &= (uint64_t(1) << bitRest) - 1;
			instruction |= normalized << bitRest;
			for (int j = 0; j < 6; ++j)
				data[i + size_t(j) + bytePosition] = uint8_t(instruction >> (8 * j));
		}
	}
	return i;
}

size_t BranchConverter::Arm(uint8_t* data, size_t size) noexcept {
	size_t i = 0;
	for (; i + 4 <= size; i += 4) {
		if (data[i + 3] != 0xEB)
			continue;
		uint32_t source =
			(uint32_t(data[i + 2]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i];
		source <<= 2;
		const uint32_t destination = Destination(source, m_ip + uint32_t(i) + 8) >> 2;
		data[i + 2] = uint8_t(destination >> 16);
		data[i + 1] = uint8_t(destination >> 8);
		data[i] = uint8_t(destination);
	}
	return i;
}

size_t BranchConverter::ArmThumb(uint8_t* data, size_t size) noexcept {
	size_t i = 0;
	for (; i + 4 <= size; i += 2) {
		if ((data[i + 1] & 0xF8) != 0xF0 || (data[i + 3] & 0xF8) != 0xF8)
			continue;
		uint32_t source = ((uint32_t(data[i + 1]) & 7) << 19) | (uint32_t(data[i]) << 11) |
						  ((uint32_t(data[i + 3]) & 7) << 8) | data[i + 2];
		source <<= 1;
		const uint32_t destination = Destination(source, m_ip + uint32_t(i) + 4) >> 1;
		data[i + 1] = uint8_t(0xF0 | ((destination >> 19) & 0x7));
		data[i] = uint8_t(destination >> 11);
		data[i + 3] = uint8_t(0xF8 | ((destination >> 8) & 0x7));
		data[i + 2] = uint8_t(destination);
		i += 2;
	}
	return i;
}

size_t BranchConverter::Sparc(uint8_t* data, size_t size) noexcept {
	size_t i = 0;
	for (; i + 4 <= size; i += 4) {
		if (!((data[i] == 0x40 && (data[i + 1] & 0xC0) == 0x00) ||
			  (data[i] == 0x7F && (data[i + 1] & 0xC0) == 0xC0)))
			continue;
		uint32_t source = (uint32_t(data[i]) << 24) | (uint32_t(data[i + 1]) << 16) |
						  (uint32_t(data[i + 2]) << 8) | uint32_t(data[i + 3]);
		source <<= 2;
		uint32_t destination = Destination(source, m_ip + uint32_t(i)) >> 2;
		destination = (((0u - ((destination >> 22) & 1)) << 22) & 0x3FFFFFFFu) |
					  (destination & 0x3FFFFFu) | 0x40000000u;
		data[i] = uint8_t(destination >> 24);
		data[i + 1] = uint8_t(destination >> 16);
		data[i + 2] = uint8_t(destination >> 8);
		data[i + 3] = uint8_t(destination);
	}
	return i;
}

size_t BranchConverter::Arm64(uint8_t* data, size_t size) noexcept {
	size_t i = 0;
	for (; i + 4 <= size; i += 4) {
		uint32_t pc = m_ip + uint32_t(i);
		uint32_t instruction = uint32_t(data[i]) | (uint32_t(data[i + 1]) << 8) |
							   (uint32_t(data[i + 2]) << 16) | (uint32_t(data[i + 3]) << 24);
		if ((instruction >> 26) == 0x25) { // BL
			const uint32_t source = instruction;
			pc >>= 2;
			if (!m_encoding)
				pc = 0u - pc;
			instruction = 0x94000000u | ((source + pc) & 0x03FFFFFFu);
		} else if ((instruction & 0x9F000000u) == 0x90000000u) { // ADRP
			const uint32_t source =
				((instruction >> 29) & 3) | ((instruction >> 3) & 0x001FFFFCu);
			if ((source + 0x00020000u) & 0x001C0000u)
				continue;
			instruction &= 0x9000001Fu;
			pc >>= 12;
			if (!m_encoding)
				pc = 0u - pc;
			const uint32_t destination = source + pc;
			instruction |= (destination & 3) << 29;
			instruction |= (destination & 0x0003FFFCu) << 3;
			instruction |= (0u - (destination & 0x00020000u)) & 0x00E00000u;
		} else {
			continue;
		}
		data[i] = uint8_t(instruction);
		data[i + 1] = uint8_t(instruction >> 8);
		data[i + 2] = uint8_t(instruction >> 16);
		data[i + 3] = uint8_t(instruction >> 24);
	}
	return i;
}

// ── DeltaConverter ───────────────────────────────────────────────────────────

void DeltaConverter::Convert(uint8_t* data, size_t size) noexcept {
	for (size_t i = 0; i < size; ++i) {
		uint8_t& previous = m_history[(m_position - m_distance) & 0xFF];
		const uint8_t original = data[i];
		if (m_encoding) {
			data[i] = uint8_t(original - previous);
			m_history[m_position & 0xFF] = original;
		} else {
			data[i] = uint8_t(original + previous);
			m_history[m_position & 0xFF] = data[i];
		}
		++m_position;
	}
}

void BcjX86(std::span<uint8_t> data, bool encoding) noexcept {
	BranchConverter(BranchKind::X86, encoding).Convert(data.data(), data.size());
}

void BcjArm(std::span<uint8_t> data, bool encoding) noexcept {
	BranchConverter(BranchKind::ARM, encoding).Convert(data.data(), data.size());
}

void BcjArmThumb(std::span<uint8_t> data, bool encoding) noexcept {
	BranchConverter(BranchKind::ARM_THUMB, encoding).Convert(data.data(), data.size());
}

void BcjArm64(std::span<uint8_t> data, bool encoding) noexcept {
	BranchConverter(BranchKind::ARM64, encoding).Convert(data.data(), data.size());
}

void DeltaDecode(std::span<uint8_t> data, size_t distance) noexcept {
	DeltaConverter(distance, false).Convert(data.data(), data.size());
}

void DeltaEncode(std::span<uint8_t> data, size_t distance) noexcept {
	DeltaConverter(distance, true).Convert(data.data(), data.size());
}

// ── FilterStage ──────────────────────────────────────────────────────────────

void FilterStage::Push(std::span<const uint8_t> input, Bytes& output, bool final) {
	if (m_spec.type == FilterSpec::Type::DELTA) {
		const size_t at = output.size();
		output.insert(output.end(), input.begin(), input.end());
		m_delta.Convert(output.data() + at, input.size());
		return;
	}
	m_pending.insert(m_pending.end(), input.begin(), input.end());
	const size_t processed = m_branch.Convert(m_pending.data(), m_pending.size());
	const size_t release = final ? m_pending.size() : processed;
	output.insert(output.end(), m_pending.begin(), m_pending.begin() + std::ptrdiff_t(release));
	m_pending.erase(m_pending.begin(), m_pending.begin() + std::ptrdiff_t(release));
}

void FilterStage::Reset() {
	m_branch = BranchConverter(m_spec.branch, m_encoding, m_spec.startOffset);
	m_delta = DeltaConverter(m_spec.deltaDistance, m_encoding);
	m_pending.clear();
}

namespace detail::filters {

// ── FilterStreamImpl ─────────────────────────────────────────────────────────

Result<size_t, ArchiveError> FilterStreamImpl::Produce(uint8_t* out, size_t max) {
	while (m_ready.size() - m_readyAt == 0) {
		if (m_inputDone)
			return Ok(size_t(0));
		m_ready.clear();
		m_readyAt = 0;
		uint8_t chunk[1 << 15];
		auto got = StreamRead(m_input, chunk, sizeof(chunk));
		if (got.IsError())
			return Err(got.Error());
		m_inputDone = got.Value() == 0;
		m_stage.Push(std::span<const uint8_t>(chunk, got.Value()), m_ready, m_inputDone);
	}
	const size_t take = std::min(max, m_ready.size() - m_readyAt);
	std::memcpy(out, m_ready.data() + m_readyAt, take);
	m_readyAt += take;
	return Ok(take);
}

bool FilterStreamImpl::Restart() {
	if (m_input.io.Seek(0, SDL_IO_SEEK_SET) != 0)
		return false;
	m_stage.Reset();
	m_ready.clear();
	m_readyAt = 0;
	m_inputDone = false;
	return true;
}

// ── Bcj2StreamImpl ───────────────────────────────────────────────────────────

Bcj2StreamImpl::Bcj2StreamImpl(ArchiveStream main, ArchiveStream call, ArchiveStream jump, ArchiveStream rc,
		uint64_t outSize, StreamStatePtr state)
	: DecoderImpl(std::move(state), Some(outSize)), m_main(std::move(main)),
	  m_call(std::move(call)), m_jump(std::move(jump)), m_rc(std::move(rc)),
	  m_mainIn(m_main.io, m_main.state), m_callIn(m_call.io, m_call.state),
	  m_jumpIn(m_jump.io, m_jump.state), m_rcIn(m_rc.io, m_rc.state), m_outSize(outSize) {
	for (uint16_t& p : m_probabilities)
		p = uint16_t(MODEL_TOTAL >> 1);
}

Result<size_t, ArchiveError> Bcj2StreamImpl::Produce(uint8_t* out, size_t max) {
	if (!m_started) {
		for (int i = 0; i < 5; ++i) {
			uint8_t byte = 0;
			if (!m_rcIn.Byte(byte))
				return Err(MakeError(ErrorKind::CORRUPT,
									 String("BCJ2 : flux du codeur d'intervalle tronqué")));
			m_code = (m_code << 8) | byte;
		}
		m_started = true;
	}
	size_t produced = 0;
	while (produced < max && m_total < m_outSize) {
		// Octets d'une adresse convertie encore à rendre.
		if (m_addressLeft > 0) {
			out[produced++] = uint8_t(m_address >> (8 * (4 - m_addressLeft)));
			--m_addressLeft;
			++m_total;
			continue;
		}
		uint8_t b = 0;
		if (!m_mainIn.Byte(b))
			return Err(MakeError(ErrorKind::CORRUPT, String("BCJ2 : flux principal tronqué")));
		out[produced++] = b;
		++m_total;
		const bool jump = (b & 0xFE) == 0xE8 || (m_previous == 0x0F && (b & 0xF0) == 0x80);
		if (!jump) {
			m_previous = b;
			continue;
		}
		if (m_total == m_outSize)
			break;
		uint16_t& probability =
			m_probabilities[b == 0xE8 ? size_t(m_previous) : (b == 0xE9 ? 256 : 257)];
		const uint32_t bound = (m_range >> 11) * probability;
		bool converted;
		if (m_code < bound) {
			m_range = bound;
			probability = uint16_t(probability + ((MODEL_TOTAL - probability) >> MOVE_BITS));
			converted = false;
		} else {
			m_range -= bound;
			m_code -= bound;
			probability = uint16_t(probability - (probability >> MOVE_BITS));
			converted = true;
		}
		if (m_range < TOP) {
			uint8_t next = 0;
			if (!m_rcIn.Byte(next))
				return Err(MakeError(ErrorKind::CORRUPT,
									 String("BCJ2 : flux du codeur d'intervalle tronqué")));
			m_range <<= 8;
			m_code = (m_code << 8) | next;
		}
		if (!converted) {
			m_previous = b;
			continue;
		}
		InputBuffer& source = b == 0xE8 ? m_callIn : m_jumpIn;
		uint8_t bytes[4];
		if (source.ReadRaw(bytes, 4) != 4)
			return Err(MakeError(ErrorKind::CORRUPT, String("BCJ2 : flux d'adresses tronqué")));
		const uint32_t absolute = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
								  (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
		m_address = absolute - uint32_t(m_total + 4);
		m_addressLeft = 4;
		m_previous = uint8_t(m_address >> 24);
	}
	return Ok(produced);
}

} // namespace detail::filters

Result<ArchiveStream, ArchiveError> OpenFilterStream(ArchiveStream input, const FilterSpec& spec, bool encoding,
		Option<uint64_t> size) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::filters::FilterStreamImpl>(std::move(input), spec,
																		  encoding, size, state),
					  state);
}

Result<ArchiveStream, ArchiveError> OpenBcj2Stream(ArchiveStream main, ArchiveStream call, ArchiveStream jump, ArchiveStream rc,
		uint64_t outSize) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(
		std::make_unique<detail::filters::Bcj2StreamImpl>(
			std::move(main), std::move(call), std::move(jump), std::move(rc), outSize, state),
		state);
}

Result<Bytes, String> Bcj2Decode(std::span<const uint8_t> main, std::span<const uint8_t> call,
		std::span<const uint8_t> jump, std::span<const uint8_t> rc, uint64_t outSize) {
	auto open = [](std::span<const uint8_t> bytes) {
		return OpenMemoryStream(Bytes(bytes.begin(), bytes.end()));
	};
	auto stream = OpenBcj2Stream(open(main).Unwrap(), open(call).Unwrap(), open(jump).Unwrap(),
								 open(rc).Unwrap(), outSize);
	if (stream.IsError())
		return Err(stream.Error().message);
	auto bytes = ReadStreamToEnd(stream.Value(), outSize);
	if (bytes.IsError())
		return Err(bytes.Error().message);
	if (bytes.Value().size() != outSize)
		return Err(String("BCJ2 : flux principal tronqué"));
	return Ok(std::move(bytes).Unwrap());
}

} // namespace data::archive
