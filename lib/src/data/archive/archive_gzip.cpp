// Définitions de data/archive/archive_gzip.hpp
#include "data/archive/archive_gzip.hpp"

namespace data::archive {

bool IsGzip(std::span<const uint8_t> bytes) noexcept {
	return bytes.size() >= 2 && bytes[0] == 0x1F && bytes[1] == 0x8B;
}

namespace detail::gzip {

Result<Option<GzipHeader>, ArchiveError> ReadMemberHeader(InputBuffer& input, bool afterMember) {
	uint8_t fixed[10];
	const size_t got = input.ReadRaw(fixed, 1);
	if (got == 0) {
		if (afterMember)
			return Ok(Option<GzipHeader>(NONE));
		return Err(MakeError(ErrorKind::FORMAT, String("gzip : fichier vide")));
	}
	// Remplissage nul après un membre (bandes, blocs d'archivage).
	if (afterMember && fixed[0] == 0) {
		uint8_t byte = 0;
		while (input.Byte(byte))
			if (byte != 0)
				return Err(MakeError(ErrorKind::CORRUPT,
									 String("gzip : données inattendues après le membre")));
		return Ok(Option<GzipHeader>(NONE));
	}
	if (input.ReadRaw(fixed + 1, 9) != 9 || fixed[0] != 0x1F || fixed[1] != 0x8B)
		return Err(MakeError(afterMember ? ErrorKind::CORRUPT : ErrorKind::FORMAT,
							 String("gzip : signature absente")));
	uint32_t crc = Crc32(std::span<const uint8_t>(fixed, 10));
	const uint8_t method = fixed[2], flags = fixed[3];
	if (method != 8)
		return Err(MakeError(ErrorKind::UNSUPPORTED,
							 String::Format("gzip : méthode %u non gérée", unsigned(method))));
	if (flags & 0xE0)
		return Err(MakeError(ErrorKind::CORRUPT, String("gzip : drapeaux réservés positionnés")));
	GzipHeader header;
	header.modifiedTime = int64_t(uint32_t(fixed[4]) | (uint32_t(fixed[5]) << 8) |
								  (uint32_t(fixed[6]) << 16) | (uint32_t(fixed[7]) << 24));
	auto take = [&](uint8_t& byte) {
		if (!input.Byte(byte))
			return false;
		crc = Crc32(std::span<const uint8_t>(&byte, 1), crc);
		return true;
	};
	if (flags & 0x04) { // FEXTRA
		uint8_t lengthBytes[2];
		if (!take(lengthBytes[0]) || !take(lengthBytes[1]))
			return Err(MakeError(ErrorKind::CORRUPT, String("gzip : en-tête tronqué")));
		for (unsigned i = 0, n = unsigned(lengthBytes[0] | (lengthBytes[1] << 8)); i < n; ++i) {
			uint8_t byte = 0;
			if (!take(byte))
				return Err(MakeError(ErrorKind::CORRUPT, String("gzip : en-tête tronqué")));
		}
	}
	auto zeroTerminated = [&](String* out) {
		for (;;) {
			uint8_t byte = 0;
			if (!take(byte))
				return false;
			if (byte == 0)
				return true;
			if (out)
				out->PushBack(char(byte));
		}
	};
	if ((flags & 0x08) && !zeroTerminated(&header.originalName)) // FNAME (ISO 8859-1)
		return Err(MakeError(ErrorKind::CORRUPT, String("gzip : en-tête tronqué")));
	if ((flags & 0x10) && !zeroTerminated(nullptr)) // FCOMMENT
		return Err(MakeError(ErrorKind::CORRUPT, String("gzip : en-tête tronqué")));
	if (flags & 0x02) { // FHCRC : 16 bits bas du CRC-32 de l'en-tête
		uint8_t stored[2];
		if (input.ReadRaw(stored, 2) != 2)
			return Err(MakeError(ErrorKind::CORRUPT, String("gzip : en-tête tronqué")));
		if (uint16_t(stored[0] | (stored[1] << 8)) != uint16_t(crc))
			return Err(MakeError(ErrorKind::CORRUPT, String("gzip : CRC de l'en-tête incorrect")));
	}
	if (!IsAscii(header.originalName))
		header.originalName = Latin1ToUtf8(header.originalName);
	return Ok(Option<GzipHeader>(Some(std::move(header))));
}

// ── GzipStreamImpl ───────────────────────────────────────────────────────────

GzipStreamImpl::GzipStreamImpl(ArchiveStream input, Option<uint64_t> size, uint64_t sizeLimit, StreamStatePtr state)
	: DecoderImpl(std::move(state), size), m_input(std::move(input)),
	  m_buffer(m_input.io, m_input.state), m_inflater(m_buffer, false), m_sizeLimit(sizeLimit) {
}

Result<size_t, ArchiveError> GzipStreamImpl::Produce(uint8_t* out, size_t max) {
	for (;;) {
		if (m_done)
			return Ok(size_t(0));
		if (!m_inMember) {
			auto header = ReadMemberHeader(m_buffer, m_members > 0);
			if (header.IsError())
				return Err(m_buffer.Failed() ? m_buffer.Failure("gzip") : header.Error());
			if (header.Value().IsNone()) {
				m_done = true;
				return Ok(size_t(0));
			}
			m_inflater.Reset();
			m_crc = 0;
			m_memberSize = 0;
			m_inMember = true;
		}
		auto produced = m_inflater.Decode(out, max);
		if (produced.IsError())
			return Err(m_buffer.Failed()
						   ? m_buffer.Failure("gzip")
						   : MakeError(ErrorKind::CORRUPT,
									   String::Format("gzip : %s", produced.Error().CStr())));
		if (produced.Value() > 0) {
			m_crc = Crc32(std::span<const uint8_t>(out, produced.Value()), m_crc);
			m_memberSize += produced.Value();
			m_total += produced.Value();
			if (m_total > m_sizeLimit)
				return Err(
					MakeError(ErrorKind::LIMIT,
							  String("gzip : taille décompressée au-delà de la limite")));
			return Ok(produced.Value());
		}
		// Fin du flux DEFLATE : pied du membre (octets en réserve d'abord).
		uint8_t trailer[8];
		if (!ReadTrailer(trailer))
			return Err(MakeError(ErrorKind::CORRUPT, String("gzip : pied de membre tronqué")));
		const uint32_t storedCrc = uint32_t(trailer[0]) | (uint32_t(trailer[1]) << 8) |
								   (uint32_t(trailer[2]) << 16) | (uint32_t(trailer[3]) << 24);
		const uint32_t storedSize = uint32_t(trailer[4]) | (uint32_t(trailer[5]) << 8) |
									(uint32_t(trailer[6]) << 16) | (uint32_t(trailer[7]) << 24);
		if (storedCrc != m_crc)
			return Err(MakeError(ErrorKind::CORRUPT,
								 String("gzip : CRC-32 incorrect (données corrompues)")));
		if (storedSize != uint32_t(m_memberSize))
			return Err(
				MakeError(ErrorKind::CORRUPT, String("gzip : taille décompressée incorrecte")));
		++m_members;
		m_inMember = false;
	}
}

bool GzipStreamImpl::Restart() {
	if (!m_buffer.Rewind())
		return false;
	m_inflater.Reset();
	m_inMember = m_done = false;
	m_members = 0;
	m_total = 0;
	return true;
}

bool GzipStreamImpl::ReadTrailer(uint8_t* trailer) {
	for (int i = 0; i < 8; ++i)
		if (!m_inflater.TakeByteAfterEnd(trailer[i]))
			return false;
	return true;
}

// ── GzipEncoderImpl ──────────────────────────────────────────────────────────

Result<bool, ArchiveError> GzipEncoderImpl::Consume(const uint8_t* data, size_t size) {
	if (auto header = WriteHeader(); header.IsError())
		return header;
	const std::span<const uint8_t> bytes(data, size);
	m_crc = Crc32(bytes, m_crc);
	m_size += size;
	m_compressor.Feed(bytes);
	if (++m_calls % 16 == 0 || size >= (size_t(1) << 16)) {
		m_compressor.Compress(false);
		Bytes out = m_compressor.TakeOutput();
		return Emit(out.data(), out.size());
	}
	return Ok(true);
}

Result<bool, ArchiveError> GzipEncoderImpl::Finish() {
	if (auto header = WriteHeader(); header.IsError())
		return header;
	m_compressor.Compress(true);
	Bytes out = m_compressor.TakeOutput();
	for (int i = 0; i < 4; ++i)
		out.push_back(uint8_t(m_crc >> (8 * i)));
	for (int i = 0; i < 4; ++i)
		out.push_back(uint8_t(m_size >> (8 * i)));
	return Emit(out.data(), out.size());
}

Result<bool, ArchiveError> GzipEncoderImpl::WriteHeader() {
	if (m_headerWritten)
		return Ok(true);
	m_headerWritten = true;
	Bytes header = {0x1F, 0x8B, 8, uint8_t(m_name.IsEmpty() ? 0x00 : 0x08)};
	const uint32_t mtime = uint32_t(std::clamp<int64_t>(m_modifiedTime, 0, 0xFFFFFFFFll));
	for (int i = 0; i < 4; ++i)
		header.push_back(uint8_t(mtime >> (8 * i)));
	header.push_back(m_level >= 9 ? 2 : (m_level <= 1 ? 4 : 0));
	header.push_back(3); // Unix
	if (!m_name.IsEmpty()) {
		header.insert(header.end(), m_name.CStr(), m_name.CStr() + m_name.GetSize());
		header.push_back(0);
	}
	return Emit(header.data(), header.size());
}

} // namespace detail::gzip

Result<ArchiveStream, ArchiveError> OpenGzipStream(ArchiveStream input, Option<uint64_t> size, uint64_t sizeLimit) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(
		std::make_unique<detail::gzip::GzipStreamImpl>(std::move(input), size, sizeLimit, state),
		state);
}

Result<GzipHeader, ArchiveError> ReadGzipHeader(sdl3::IOStream& stream) {
	const Sint64 start = stream.Tell();
	InputBuffer buffer(stream, {}, 64 * 1024, 4096);
	auto header = detail::gzip::ReadMemberHeader(buffer, false);
	(void)stream.Seek(start < 0 ? 0 : start, SDL_IO_SEEK_SET);
	if (header.IsError())
		return Err(header.Error());
	return Ok(header.Value().UnwrapOr(GzipHeader{}));
}

Result<ArchiveStream, ArchiveError> OpenGzipEncoder(ArchiveStream sink,
		int level,
		const String& name,
		int64_t modifiedTime) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<detail::gzip::GzipEncoderImpl>(std::move(sink), level, name,
																	  modifiedTime, state),
					  state);
}

Result<GzipContent, ArchiveError> GzipDecompress(std::span<const uint8_t> input, uint64_t sizeLimit) {
	auto memory = OpenViewStream(input);
	if (memory.IsError())
		return Err(memory.Error());
	GzipContent content;
	auto header = ReadGzipHeader(memory.Value().io);
	if (header.IsError())
		return Err(header.Error());
	content.originalName = header.Value().originalName;
	content.modifiedTime = header.Value().modifiedTime;
	auto stream = OpenGzipStream(std::move(memory).Unwrap(), NONE, sizeLimit);
	if (stream.IsError())
		return Err(stream.Error());
	auto bytes = ReadStreamToEnd(stream.Value(), sizeLimit);
	if (bytes.IsError())
		return Err(bytes.Error());
	content.bytes = std::move(bytes).Unwrap();
	return Ok(std::move(content));
}

Result<Bytes, ArchiveError> GzipCompress(std::span<const uint8_t> input,
		const String& name,
		int64_t modifiedTime,
		int level) {
	auto target = std::make_shared<Bytes>();
	auto encoder = OpenGzipEncoder(OpenMemorySink(target).Unwrap(), level, name, modifiedTime);
	if (encoder.IsError())
		return Err(encoder.Error());
	if (auto written = StreamWrite(encoder.Value(), input); written.IsError())
		return Err(written.Error());
	if (auto finished = FinishStream(encoder.Value()); finished.IsError())
		return Err(finished.Error());
	return Ok(std::move(*target));
}

} // namespace data::archive
