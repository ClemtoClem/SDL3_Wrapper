// Définitions de data/archive/archive_stream.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/archive/archive_stream.hpp"

namespace data::archive {

// ── StreamState ──────────────────────────────────────────────────────────────

void StreamState::Fail(ArchiveError failure) {
	if (error.IsNone())
		error = Some(std::move(failure));
}

// ── ArchiveStream ────────────────────────────────────────────────────────────

ArchiveError ArchiveStream::Failure(const char* context) const {
	if (state && state->error.IsSome())
		return state->error.Value();
	return MakeError(ErrorKind::IO, String::Format("%s : erreur de lecture", context));
}

Result<ArchiveStream, ArchiveError> MakeStream(std::unique_ptr<sdl3::IOStreamImpl> impl, StreamStatePtr state) {
	auto io = sdl3::IOStream::FromImpl(std::move(impl));
	if (io.IsError())
		return Err(MakeError(ErrorKind::IO,
							 String::Format("flux impossible : %s", String(io.Error()).CStr())));
	return Ok(ArchiveStream{std::move(io).Unwrap(), std::move(state)});
}

Result<size_t, ArchiveError> StreamRead(ArchiveStream& stream, void* buffer, size_t size) {
	if (size == 0)
		return Ok(size_t(0));
	auto* bytes = static_cast<uint8_t*>(buffer);
	size_t total = 0;
	while (total < size) {
		const size_t done = stream.io.Read(bytes + total, size - total);
		if (done == 0) {
			if (stream.io.Status() == sdl3::IOStatus::ERROR ||
				(stream.state && stream.state->error.IsSome()))
				return Err(stream.Failure());
			break;
		}
		total += done;
	}
	return Ok(total);
}

Result<Bytes, ArchiveError> ReadStreamToEnd(ArchiveStream& stream, uint64_t limit, uint64_t sizeHint) {
	Bytes out;
	if (sizeHint > 0)
		out.reserve(size_t(std::min<uint64_t>({sizeHint, limit, uint64_t(1) << 26})));
	uint8_t chunk[1 << 16];
	for (;;) {
		auto done = StreamRead(stream, chunk, sizeof(chunk));
		if (done.IsError())
			return Err(done.Error());
		if (done.Value() == 0)
			break;
		if (out.size() + done.Value() > limit)
			return Err(MakeError(ErrorKind::LIMIT,
								 String("données plus grandes que la limite autorisée")));
		out.insert(out.end(), chunk, chunk + done.Value());
	}
	return Ok(std::move(out));
}

Result<uint64_t, ArchiveError> CopyStream(ArchiveStream& in, sdl3::IOStream& out, uint64_t limit) {
	std::vector<uint8_t> chunk(1 << 16);
	uint64_t total = 0;
	for (;;) {
		auto done = StreamRead(in, chunk.data(), chunk.size());
		if (done.IsError())
			return Err(done.Error());
		if (done.Value() == 0)
			break;
		total += done.Value();
		if (total > limit)
			return Err(MakeError(ErrorKind::LIMIT,
								 String("données plus grandes que la limite autorisée")));
		if (!out.WriteExact(chunk.data(), done.Value()))
			return Err(MakeError(ErrorKind::IO, String("écriture impossible")));
	}
	return Ok(total);
}

// ── InputBuffer ──────────────────────────────────────────────────────────────

InputBuffer::InputBuffer(sdl3::IOStream& source, StreamStatePtr sourceState, uint64_t limit, size_t capacity)
	: m_source(&source), m_sourceState(std::move(sourceState)), m_limit(limit),
	  m_buffer(capacity) {
	const Sint64 start = source.Tell();
	m_start = start < 0 ? 0 : uint64_t(start);
}

bool InputBuffer::Byte(uint8_t& out) {
	if (m_position == m_end && !Fill())
		return false;
	out = m_buffer[m_position++];
	return true;
}

bool InputBuffer::Fill() {
	if (m_position < m_end)
		return true;
	m_consumedBefore += m_end;
	m_position = m_end = 0;
	const uint64_t remaining = m_limit - m_fetched;
	if (remaining == 0 || m_failed)
		return false;
	const size_t want = size_t(std::min<uint64_t>(remaining, m_buffer.size()));
	const size_t got = m_source->Read(m_buffer.data(), want);
	if (got == 0) {
		if (m_source->Status() == sdl3::IOStatus::ERROR ||
			(m_sourceState && m_sourceState->error.IsSome()))
			m_failed = true;
		return false;
	}
	m_end = got;
	m_fetched += got;
	return true;
}

size_t InputBuffer::ReadRaw(uint8_t* out, size_t size) {
	size_t total = 0;
	while (total < size) {
		if (Available() == 0 && !Fill())
			break;
		const size_t take = std::min(size - total, Available());
		std::memcpy(out + total, Data(), take);
		m_position += take;
		total += take;
	}
	return total;
}

bool InputBuffer::Skip(uint64_t count) {
	while (count > 0) {
		if (Available() == 0 && !Fill())
			return false;
		const size_t take = size_t(std::min<uint64_t>(count, Available()));
		m_position += take;
		count -= take;
	}
	return true;
}

ArchiveError InputBuffer::Failure(const char* context) const {
	if (m_sourceState && m_sourceState->error.IsSome())
		return m_sourceState->error.Value();
	return MakeError(
		m_failed ? ErrorKind::IO : ErrorKind::CORRUPT,
		String::Format(m_failed ? "%s : erreur de lecture" : "%s : données tronquées",
					   context));
}

bool InputBuffer::Rewind() {
	if (m_source->Seek(Sint64(m_start), SDL_IO_SEEK_SET) < 0)
		return false;
	m_position = m_end = 0;
	m_consumedBefore = m_fetched = 0;
	m_failed = false;
	return true;
}

bool InputBuffer::GiveBack(uint64_t unusedBytes) {
	const uint64_t logical = m_start + Consumed() - unusedBytes;
	return m_source->Seek(Sint64(logical), SDL_IO_SEEK_SET) >= 0;
}

// ── SlidingWindow ────────────────────────────────────────────────────────────

void SlidingWindow::Reset(uint64_t capacity) {
	m_capacity = size_t(std::clamp<uint64_t>(capacity, 4096, uint64_t(SIZE_MAX / 2)));
	m_buffer.clear();
	m_position = 0;
	m_total = 0;
}

void SlidingWindow::Put(uint8_t byte) {
	if (m_position == m_buffer.size())
		m_buffer.resize(std::min(m_capacity, std::max(m_buffer.size() * 2, size_t(1) << 16)));
	m_buffer[m_position] = byte;
	if (++m_position == m_capacity)
		m_position = 0;
	++m_total;
}

uint8_t SlidingWindow::Get(uint64_t distance) const {
	return m_buffer[m_position >= distance ? m_position - size_t(distance)
										   : m_position + m_capacity - size_t(distance)];
}

bool SlidingWindow::Has(uint64_t distance) const noexcept {
	return distance >= 1 && distance <= std::min<uint64_t>(m_total, m_capacity);
}

// ── DecoderImpl ──────────────────────────────────────────────────────────────

size_t DecoderImpl::Read(void* buffer, size_t size, sdl3::IOStatus& status) {
	auto* out = static_cast<uint8_t*>(buffer);
	size_t total = 0;
	while (total < size && !m_ended) {
		if (m_state->error.IsSome())
			break;
		auto produced = Produce(out + total, size - total);
		if (produced.IsError()) {
			m_state->Fail(produced.Error());
			break;
		}
		if (produced.Value() == 0) {
			m_ended = true;
			if (m_size.IsNone())
				m_size = Some(m_position + total); // taille connue une fois le flux lu
			break;
		}
		total += produced.Value();
	}
	m_position += total;
	if (total == 0)
		status = m_state->error.IsSome() ? sdl3::IOStatus::ERROR : sdl3::IOStatus::END;
	return total;
}

Sint64 DecoderImpl::Seek(Sint64 offset, sdl3::IOWhence whence) {
	Sint64 target = offset;
	if (whence == sdl3::IOWhence::SEEK_CURRENT)
		target = Sint64(m_position) + offset;
	else if (whence == sdl3::IOWhence::SeekEnd) {
		if (m_size.IsNone())
			return -1;
		target = Sint64(m_size.Value()) + offset;
	}
	if (target < 0)
		return -1;
	if (uint64_t(target) < m_position) {
		if (!Restart())
			return -1;
		m_position = 0;
		m_ended = false;
		if (m_state->error.IsSome())
			return -1;
	}
	// Avancer : décoder et jeter.
	uint8_t scratch[1 << 14];
	while (m_position < uint64_t(target)) {
		sdl3::IOStatus status = sdl3::IOStatus::READY;
		const size_t want =
			size_t(std::min<uint64_t>(sizeof(scratch), uint64_t(target) - m_position));
		if (Read(scratch, want, status) == 0)
			return -1;
	}
	return Sint64(m_position);
}

// ── SubStreamImpl ────────────────────────────────────────────────────────────

Sint64 SubStreamImpl::Seek(Sint64 offset, sdl3::IOWhence whence) {
	Sint64 target = offset;
	if (whence == sdl3::IOWhence::SEEK_CURRENT)
		target += Sint64(m_position);
	else if (whence == sdl3::IOWhence::SeekEnd)
		target += Sint64(m_length);
	if (target < 0)
		return -1;
	m_position = uint64_t(target);
	return target;
}

size_t SubStreamImpl::Read(void* buffer, size_t size, sdl3::IOStatus& status) {
	if (m_position >= m_length) {
		status = sdl3::IOStatus::END;
		return 0;
	}
	const size_t want = size_t(std::min<uint64_t>(size, m_length - m_position));
	if (m_parent->Seek(Sint64(m_base + m_position), SDL_IO_SEEK_SET) < 0 ||
		!m_parent->ReadExact(buffer, want)) {
		// Parent décodé (bloc solide) : son erreur est la vraie cause.
		if (m_keepAlive && m_keepAlive->state && m_keepAlive->state->error.IsSome())
			m_state->Fail(m_keepAlive->state->error.Value());
		else
			m_state->Fail(MakeError(ErrorKind::CORRUPT,
									String("données hors du fichier (archive tronquée ?)")));
		status = sdl3::IOStatus::ERROR;
		return 0;
	}
	m_position += want;
	return want;
}

// ── MemoryStreamImpl ─────────────────────────────────────────────────────────

Sint64 MemoryStreamImpl::Seek(Sint64 offset, sdl3::IOWhence whence) {
	Sint64 target = offset;
	if (whence == sdl3::IOWhence::SEEK_CURRENT)
		target += Sint64(m_position);
	else if (whence == sdl3::IOWhence::SeekEnd)
		target += Sint64(m_bytes.size());
	if (target < 0)
		return -1;
	m_position = size_t(target);
	return target;
}

size_t MemoryStreamImpl::Read(void* buffer, size_t size, sdl3::IOStatus& status) {
	if (m_position >= m_bytes.size()) {
		status = sdl3::IOStatus::END;
		return 0;
	}
	const size_t take = std::min(size, m_bytes.size() - m_position);
	std::memcpy(buffer, m_bytes.data() + m_position, take);
	m_position += take;
	return take;
}

Result<ArchiveStream, ArchiveError> OpenSubStream(sdl3::IOStream& parent, uint64_t offset, uint64_t length) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<SubStreamImpl>(parent, offset, length, state), state);
}

Result<ArchiveStream, ArchiveError> OpenSharedSubStream(std::shared_ptr<ArchiveStream> parent, uint64_t offset, uint64_t length) {
	auto state = std::make_shared<StreamState>();
	sdl3::IOStream& io = parent->io;
	return MakeStream(std::make_unique<SubStreamImpl>(io, offset, length, state, std::move(parent)),
					  state);
}

Result<ArchiveStream, ArchiveError> OpenMemoryStream(Bytes bytes) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<MemoryStreamImpl>(std::move(bytes)), state);
}

Result<ArchiveStream, ArchiveError> OpenSharedMemoryStream(std::shared_ptr<const Bytes> bytes, uint64_t offset, uint64_t length) {
	if (offset > bytes->size() || length > bytes->size() - offset)
		return Err(MakeError(ErrorKind::CORRUPT, String("fenêtre hors du bloc décodé")));
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<MemoryStreamImpl>(std::move(bytes), offset, length), state);
}

// ── CheckedStreamImpl ────────────────────────────────────────────────────────

Result<size_t, ArchiveError> CheckedStreamImpl::Produce(uint8_t* out, size_t max) {
	if (m_expectedSize.IsSome())
		max = size_t(std::min<uint64_t>(
			max, m_expectedSize.Value() - std::min(m_count, m_expectedSize.Value()) + 1));
	auto done = StreamRead(m_inner, out, max);
	if (done.IsError()) {
		ArchiveError error = done.Error();
		// Données chiffrées indécodables : presque toujours la clé.
		if (m_mismatch == ErrorKind::WRONG_PASSWORD && error.kind == ErrorKind::CORRUPT) {
			error.kind = ErrorKind::WRONG_PASSWORD;
			error.message =
				String::Format("%s (mot de passe incorrect ?)", error.message.CStr());
		}
		if (!error.message.StartsWith(m_name + " :"))
			error.message = String::Format("%s : %s", m_name.CStr(), error.message.CStr());
		return Err(error);
	}
	if (done.Value() == 0) {
		if (m_expectedSize.IsSome() && m_count != m_expectedSize.Value())
			return Err(MakeError(
				ErrorKind::CORRUPT,
				String::Format("%s : %llu octets au lieu de %llu", m_name.CStr(),
							   static_cast<unsigned long long>(m_count),
							   static_cast<unsigned long long>(m_expectedSize.Value()))));
		if (m_expectedCrc.IsSome() && m_crc != m_expectedCrc.Value())
			return Err(
				MakeError(m_mismatch, String::Format("%s : CRC-32 incorrect%s", m_name.CStr(),
													 m_mismatch == ErrorKind::WRONG_PASSWORD
														 ? " (mot de passe incorrect ?)"
														 : "")));
		return Ok(size_t(0));
	}
	m_count += done.Value();
	if (m_expectedSize.IsSome() && m_count > m_expectedSize.Value())
		return Err(MakeError(
			ErrorKind::CORRUPT,
			String::Format("%s : plus de données que la taille annoncée", m_name.CStr())));
	m_crc = Crc32(std::span<const uint8_t>(out, done.Value()), m_crc);
	return done;
}

bool CheckedStreamImpl::Restart() {
	if (m_inner.io.Seek(0, SDL_IO_SEEK_SET) != 0)
		return false;
	m_count = 0;
	m_crc = 0;
	return true;
}

Result<ArchiveStream, ArchiveError> OpenCheckedStream(ArchiveStream inner, Option<uint64_t> size, Option<uint32_t> crc,
		const String& name, ErrorKind mismatch) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(
		std::make_unique<CheckedStreamImpl>(std::move(inner), size, crc, name, mismatch, state),
		state);
}

Result<Bytes, ArchiveError> StreamReadExact(ArchiveStream& stream, uint64_t size) {
	Bytes bytes(static_cast<size_t>(size));
	auto done = StreamRead(stream, bytes.data(), bytes.size());
	if (done.IsError())
		return Err(done.Error());
	if (done.Value() != size)
		return Err(MakeError(ErrorKind::CORRUPT, String("données tronquées")));
	return Ok(std::move(bytes));
}

Result<ArchiveStream, ArchiveError> OpenFileStream(const String& path) {
	auto io = sdl3::IOStream::FromFile(path, "rb");
	if (io.IsError())
		return Err(
			MakeError(ErrorKind::IO, String::Format("ouverture de %s impossible : %s", path.CStr(),
													String(io.Error()).CStr())));
	return Ok(ArchiveStream{std::move(io).Unwrap(), std::make_shared<StreamState>()});
}

Result<ArchiveStream, ArchiveError> OpenViewStream(std::span<const uint8_t> bytes) {
	auto io = ViewStream(bytes);
	if (io.IsError())
		return Err(MakeError(ErrorKind::IO, io.Error()));
	return Ok(ArchiveStream{std::move(io).Unwrap(), std::make_shared<StreamState>()});
}

// ── BorrowedStreamImpl ───────────────────────────────────────────────────────

Sint64 BorrowedStreamImpl::Seek(Sint64 offset, sdl3::IOWhence whence) {
	return m_target->Seek(offset, whence);
}

size_t BorrowedStreamImpl::Read(void* buffer, size_t size, sdl3::IOStatus& status) {
	const size_t done = m_target->Read(buffer, size);
	if (done == 0)
		status = m_target->Status();
	return done;
}

size_t BorrowedStreamImpl::Write(const void* buffer, size_t size, sdl3::IOStatus& status) {
	if (!m_target->WriteExact(buffer, size)) {
		status = sdl3::IOStatus::ERROR;
		return 0;
	}
	return size;
}

Result<ArchiveStream, ArchiveError> OpenBorrowedStream(sdl3::IOStream& target) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<BorrowedStreamImpl>(target), state);
}

// ── EncoderImpl ──────────────────────────────────────────────────────────────

Sint64 EncoderImpl::Seek(Sint64 offset, sdl3::IOWhence whence) {
	return offset == 0 && whence == sdl3::IOWhence::SEEK_CURRENT ? Sint64(m_received) : -1;
}

size_t EncoderImpl::Write(const void* buffer, size_t size, sdl3::IOStatus& status) {
	if (m_state->error.IsSome() || m_finished) {
		status = sdl3::IOStatus::ERROR;
		return 0;
	}
	auto consumed = Consume(static_cast<const uint8_t*>(buffer), size);
	if (consumed.IsError()) {
		m_state->Fail(consumed.Error());
		status = sdl3::IOStatus::ERROR;
		return 0;
	}
	m_received += size;
	return size;
}

bool EncoderImpl::Close() {
	if (!m_finished && m_state->error.IsNone()) {
		m_finished = true;
		auto finished = Finish();
		if (finished.IsError())
			m_state->Fail(finished.Error());
	}
	if (!m_sink.io.Close() && m_state->error.IsNone())
		m_state->Fail(m_sink.Failure("écriture"));
	if (m_state->error.IsNone() && m_sink.state && m_sink.state->error.IsSome())
		m_state->Fail(m_sink.state->error.Value());
	return m_state->error.IsNone();
}

Result<bool, ArchiveError> EncoderImpl::Emit(const void* data, size_t size) {
	if (size == 0)
		return Ok(true);
	if (!m_sink.io.WriteExact(data, size))
		return Err(m_sink.Failure("écriture"));
	m_emitted += size;
	return Ok(true);
}

Result<bool, ArchiveError> FinishStream(ArchiveStream& stream) {
	const bool closed = stream.io.Close();
	if (stream.state && stream.state->error.IsSome())
		return Err(stream.state->error.Value());
	if (!closed)
		return Err(MakeError(ErrorKind::IO, String("fermeture du flux impossible")));
	return Ok(true);
}

Result<bool, ArchiveError> StreamWrite(ArchiveStream& stream, std::span<const uint8_t> data) {
	if (data.empty())
		return Ok(true);
	if (!stream.io.WriteExact(data.data(), data.size()))
		return Err(stream.Failure("écriture"));
	return Ok(true);
}

Result<uint64_t, ArchiveError> CopyToStream(ArchiveStream& in, ArchiveStream& out) {
	std::vector<uint8_t> chunk(1 << 16);
	uint64_t total = 0;
	for (;;) {
		auto done = StreamRead(in, chunk.data(), chunk.size());
		if (done.IsError())
			return Err(done.Error());
		if (done.Value() == 0)
			return Ok(total);
		auto written = StreamWrite(out, std::span<const uint8_t>(chunk.data(), done.Value()));
		if (written.IsError())
			return Err(written.Error());
		total += done.Value();
	}
}

// ── DrainImpl ────────────────────────────────────────────────────────────────

Result<size_t, ArchiveError> DrainImpl::Produce(uint8_t* out, size_t max) {
	auto got = StreamRead(m_inner, out, max);
	if (got.IsError() || got.Value() > 0)
		return got;
	uint8_t scratch[1 << 12];
	for (;;) {
		auto drained = StreamRead(*m_tail, scratch, sizeof(scratch));
		if (drained.IsError())
			return drained;
		if (drained.Value() == 0)
			return Ok(size_t(0));
	}
}

Result<ArchiveStream, ArchiveError> OpenDrainingStream(ArchiveStream inner, std::shared_ptr<ArchiveStream> tail) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<DrainImpl>(std::move(inner), std::move(tail), state), state);
}

Result<bool, ArchiveError> CopyStreamExactly(ArchiveStream& content, sdl3::IOStream& out, uint64_t size, const String& path) {
	std::vector<uint8_t> chunk(1 << 16);
	uint64_t copied = 0;
	for (;;) {
		const size_t want = size_t(std::min<uint64_t>(chunk.size(), size - copied + 1));
		auto done = StreamRead(content, chunk.data(), want);
		if (done.IsError())
			return Err(done.Error());
		if (done.Value() == 0)
			break;
		if (copied + done.Value() > size)
			return Err(
				MakeError(ErrorKind::INVALID_ARGUMENT,
						  String::Format("%s est plus grand que sa taille annoncée", path.CStr())));
		if (!out.WriteExact(chunk.data(), done.Value()))
			return Err(MakeError(ErrorKind::IO, String("écriture impossible")));
		copied += done.Value();
	}
	if (copied != size)
		return Err(
			MakeError(ErrorKind::INVALID_ARGUMENT,
					  String::Format("%s est plus petit que sa taille annoncée", path.CStr())));
	return Ok(true);
}

// ── ConcatStreamImpl ─────────────────────────────────────────────────────────

Result<size_t, ArchiveError> ConcatStreamImpl::Produce(uint8_t* out, size_t max) {
	while (m_current < m_parts.size()) {
		auto got = StreamRead(m_parts[m_current], out, max);
		if (got.IsError() || got.Value() > 0)
			return got;
		++m_current;
	}
	return Ok(size_t(0));
}

bool ConcatStreamImpl::Restart() {
	for (ArchiveStream& part : m_parts)
		if (part.io.Seek(0, SDL_IO_SEEK_SET) != 0)
			return false;
	m_current = 0;
	return true;
}

Result<ArchiveStream, ArchiveError> OpenConcatStream(std::vector<ArchiveStream> parts, Option<uint64_t> size) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<ConcatStreamImpl>(std::move(parts), size, state), state);
}

// ── CountingSinkImpl ─────────────────────────────────────────────────────────

Sint64 CountingSinkImpl::Seek(Sint64 offset, sdl3::IOWhence whence) {
	return offset == 0 && whence == sdl3::IOWhence::SEEK_CURRENT ? Sint64(*m_counter) : -1;
}

size_t CountingSinkImpl::Write(const void* buffer, size_t size, sdl3::IOStatus& status) {
	if (!m_target->WriteExact(buffer, size)) {
		status = sdl3::IOStatus::ERROR;
		return 0;
	}
	*m_counter += size;
	return size;
}

Result<ArchiveStream, ArchiveError> OpenCountingSink(sdl3::IOStream& target, std::shared_ptr<uint64_t> counter) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<CountingSinkImpl>(target, std::move(counter)), state);
}

// ── MemorySinkImpl ───────────────────────────────────────────────────────────

Sint64 MemorySinkImpl::Seek(Sint64 offset, sdl3::IOWhence whence) {
	return offset == 0 && whence == sdl3::IOWhence::SEEK_CURRENT ? Sint64(m_target->size())
																 : -1;
}

size_t MemorySinkImpl::Write(const void* buffer, size_t size, sdl3::IOStatus&) {
	const auto* bytes = static_cast<const uint8_t*>(buffer);
	m_target->insert(m_target->end(), bytes, bytes + size);
	return size;
}

Result<ArchiveStream, ArchiveError> OpenMemorySink(std::shared_ptr<Bytes> target) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<MemorySinkImpl>(std::move(target)), state);
}

// ── ArchiveReader ────────────────────────────────────────────────────────────

Result<Bytes, ArchiveError> ArchiveReader::Extract(size_t index) {
	if (index >= Entries().size())
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("entrée inexistante")));
	const EntryInfo& entry = Entries()[index];
	if (entry.size > EntryLimit())
		return Err(
			MakeError(ErrorKind::LIMIT,
					  String::Format("%s : %llu octets dépassent la limite", entry.path.CStr(),
									 static_cast<unsigned long long>(entry.size))));
	auto stream = OpenEntry(index);
	if (stream.IsError())
		return Err(stream.Error());
	auto bytes = ReadStreamToEnd(stream.Value(), EntryLimit(), entry.size);
	if (bytes.IsError()) {
		ArchiveError error = bytes.Error();
		if (error.kind == ErrorKind::LIMIT)
			error.message = String::Format("%s : %s", entry.path.CStr(), error.message.CStr());
		return Err(error);
	}
	return bytes;
}

bool ArchiveReader::HasEncryptedEntries() const noexcept {
	for (const EntryInfo& entry : Entries())
		if (entry.encrypted)
			return true;
	return false;
}

Option<size_t> ArchiveReader::Find(const String& path) const {
	for (size_t i = 0; i < Entries().size(); ++i)
		if (Entries()[i].path == path)
			return Some(i);
	return NONE;
}

} // namespace data::archive
