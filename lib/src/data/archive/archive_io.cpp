// Définitions de data/archive/archive_io.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/archive/archive_io.hpp"

namespace data::archive {

// ── BinaryReader ─────────────────────────────────────────────────────────────

uint16_t BinaryReader::U16LeBe() noexcept {
	uint16_t little = U16Le();
	uint16_t big = U16Be();
	if (little != big)
		m_ok = false;
	return little;
}

uint32_t BinaryReader::U32LeBe() noexcept {
	uint32_t little = U32Le();
	uint32_t big = U32Be();
	if (little != big)
		m_ok = false;
	return little;
}

bool BinaryReader::Read(void* out, size_t size) noexcept {
	if (!m_ok || !m_stream->ReadExact(out, size)) {
		m_ok = false;
		if (size > 0)
			std::memset(out, 0, size);
		return false;
	}
	return true;
}

Bytes BinaryReader::ReadBytes(uint64_t size) {
	// Taille du flux inconnue (flux décompressé) : plafond fixe.
	const uint64_t available = m_stream->GetSize() < 0 ? uint64_t(1) << 26 : Remaining();
	if (!m_ok || size > available) {
		m_ok = false;
		return {};
	}
	Bytes bytes(static_cast<size_t>(size));
	(void)Read(bytes.data(), bytes.size());
	return bytes;
}

String BinaryReader::ReadString(uint64_t size) {
	Bytes bytes = ReadBytes(size);
	return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool BinaryReader::Seek(uint64_t position) noexcept {
	if (!m_ok || (m_stream->GetSize() >= 0 && position > Size()) ||
		m_stream->Seek(Sint64(position), SDL_IO_SEEK_SET) < 0)
		m_ok = false;
	return m_ok;
}

uint64_t BinaryReader::Tell() const noexcept {
	Sint64 position = m_stream->Tell();
	return position < 0 ? 0 : uint64_t(position);
}

uint64_t BinaryReader::Size() const noexcept {
	Sint64 size = m_stream->GetSize();
	return size < 0 ? 0 : uint64_t(size);
}

uint64_t BinaryReader::Remaining() const noexcept {
	uint64_t size = Size();
	uint64_t position = Tell();
	return position < size ? size - position : 0;
}

// ── BinaryWriter ─────────────────────────────────────────────────────────────

void BinaryWriter::U16LeBe(uint16_t v) noexcept {
	U16Le(v);
	U16Be(v);
}

void BinaryWriter::U32LeBe(uint32_t v) noexcept {
	U32Le(v);
	U32Be(v);
}

void BinaryWriter::Write(std::span<const uint8_t> bytes) noexcept {
	Check(m_stream->WriteExact(bytes.data(), bytes.size()));
}

void BinaryWriter::WriteFixed(const String& text, size_t width, uint8_t pad) noexcept {
	size_t length = text.GetSize() < width ? text.GetSize() : width;
	Write(text.CStr(), length);
	Fill(width - length, pad);
}

void BinaryWriter::Fill(uint64_t count, uint8_t value) noexcept {
	uint8_t block[512];
	std::memset(block, value, sizeof(block));
	while (count > 0 && m_ok) {
		size_t chunk = count < sizeof(block) ? size_t(count) : sizeof(block);
		Write(block, chunk);
		count -= chunk;
	}
}

void BinaryWriter::AlignTo(uint64_t alignment, uint8_t value) noexcept {
	uint64_t remainder = Tell() % alignment;
	if (remainder != 0)
		Fill(alignment - remainder, value);
}

bool BinaryWriter::Seek(uint64_t position) noexcept {
	Check(m_stream->Seek(Sint64(position), SDL_IO_SEEK_SET) >= 0);
	return m_ok;
}

uint64_t BinaryWriter::Tell() const noexcept {
	Sint64 position = m_stream->Tell();
	return position < 0 ? 0 : uint64_t(position);
}

// ── MemoryStream ─────────────────────────────────────────────────────────────

Result<MemoryStream, String> MemoryStream::FromBytes(Bytes bytes) {
	MemoryStream memory;
	memory.m_bytes = std::make_unique<Bytes>(std::move(bytes));
	// FromConstMemory refuse un tampon nul : un octet factice pour le cas vide.
	static const uint8_t EMPTY = 0;
	const uint8_t* data = memory.m_bytes->empty() ? &EMPTY : memory.m_bytes->data();
	auto stream = sdl3::IOStream::FromConstMemory(data, memory.m_bytes->size());
	if (stream.IsError())
		return Err(
			String::Format("flux mémoire impossible : %s", String(stream.Error()).CStr()));
	memory.m_stream = std::move(stream).Unwrap();
	return Ok(std::move(memory));
}

// ── ArchiveSource ────────────────────────────────────────────────────────────

Result<ArchiveSource, String> ArchiveSource::FromFile(const String& path) {
	auto stream = sdl3::IOStream::FromFile(path, "rb");
	if (stream.IsError())
		return Err(String::Format("ouverture de %s impossible : %s", path.CStr(),
								  String(stream.Error()).CStr()));
	ArchiveSource source;
	source.m_file = Some(std::move(stream).Unwrap());
	return Ok(std::move(source));
}

Result<ArchiveSource, String> ArchiveSource::FromBytes(Bytes bytes) {
	auto memory = MemoryStream::FromBytes(std::move(bytes));
	if (memory.IsError())
		return Err(memory.Error());
	ArchiveSource source;
	source.m_memory = Some(std::move(memory).Unwrap());
	return Ok(std::move(source));
}

sdl3::IOStream& ArchiveSource::Stream() noexcept {
	return m_file.IsSome() ? m_file.Value() : m_memory.Value().Stream();
}

uint64_t ArchiveSource::Size() noexcept {
	Sint64 size = Stream().GetSize();
	return size < 0 ? 0 : uint64_t(size);
}

Result<sdl3::IOStream, String> ViewStream(std::span<const uint8_t> bytes) {
	static const uint8_t EMPTY = 0;
	auto stream =
		sdl3::IOStream::FromConstMemory(bytes.empty() ? &EMPTY : bytes.data(), bytes.size());
	if (stream.IsError())
		return Err(String::Format("flux mémoire impossible : %s", String(stream.Error()).CStr()));
	return Ok(std::move(stream).Unwrap());
}

Result<sdl3::IOStream, String> CreateMemoryWriter() {
	auto stream = sdl3::IOStream::FromDynamicMemory();
	if (stream.IsError())
		return Err(String::Format("flux mémoire impossible : %s", String(stream.Error()).CStr()));
	return Ok(std::move(stream).Unwrap());
}

Result<Bytes, String> ReadRange(sdl3::IOStream& stream, uint64_t offset, uint64_t size) {
	Sint64 total = stream.GetSize();
	if (total < 0 || offset > uint64_t(total) || size > uint64_t(total) - offset)
		return Err(
			String::Format("lecture hors du fichier (%llu octets à %llu, fichier de %lld octets)",
						   static_cast<unsigned long long>(size),
						   static_cast<unsigned long long>(offset), static_cast<long long>(total)));
	if (stream.Seek(Sint64(offset), SDL_IO_SEEK_SET) < 0)
		return Err(String("positionnement impossible dans le flux"));
	Bytes bytes(static_cast<size_t>(size));
	if (!stream.ReadExact(bytes.data(), bytes.size()))
		return Err(String("flux tronqué"));
	return Ok(std::move(bytes));
}

Bytes Utf8ToUtf16Le(const String& text, bool nullTerminated) {
	std::u16string units = unicode::ToUtf16(text.View());
	Bytes out;
	out.reserve((units.size() + 1) * 2);
	for (char16_t unit : units) {
		out.push_back(uint8_t(unit & 0xFF));
		out.push_back(uint8_t(unit >> 8));
	}
	if (nullTerminated) {
		out.push_back(0);
		out.push_back(0);
	}
	return out;
}

Bytes Utf8ToUtf16Be(const String& text) {
	std::u16string units = unicode::ToUtf16(text.View());
	Bytes out;
	out.reserve(units.size() * 2);
	for (char16_t unit : units) {
		out.push_back(uint8_t(unit >> 8));
		out.push_back(uint8_t(unit & 0xFF));
	}
	return out;
}

String Utf16ToUtf8(std::span<const uint8_t> bytes, bool bigEndian) {
	std::u16string units;
	units.reserve(bytes.size() / 2);
	for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
		char16_t unit = bigEndian ? char16_t((bytes[i] << 8) | bytes[i + 1])
								  : char16_t(bytes[i] | (bytes[i + 1] << 8));
		if (unit == 0)
			break;
		units.push_back(unit);
	}
	return String(unicode::FromUtf16(units));
}

String Latin1ToUtf8(const String& text) {
	String out;
	for (size_t i = 0; i < text.GetSize(); ++i) {
		const uint8_t c = uint8_t(text.CharAt(i));
		if (c < 0x80) {
			out.PushBack(char(c));
		} else {
			out.PushBack(char(0xC0 | (c >> 6)));
			out.PushBack(char(0x80 | (c & 0x3F)));
		}
	}
	return out;
}

String Cp437ToUtf8(std::span<const uint8_t> bytes) {
	static constexpr char16_t HIGH[128] = {
		0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8,
		0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2,
		0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1,
		0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD,
		0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562,
		0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534,
		0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560,
		0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
		0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF, 0x0393,
		0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6,
		0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0,
		0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};
	std::u16string units;
	units.reserve(bytes.size());
	for (uint8_t byte : bytes)
		units.push_back(byte < 0x80 ? char16_t(byte) : HIGH[byte - 0x80]);
	return String(unicode::FromUtf16(units));
}

bool IsAscii(const String& text) {
	for (size_t i = 0; i < text.GetSize(); ++i)
		if (uint8_t(text.CharAt(i)) >= 0x80)
			return false;
	return true;
}

int64_t CurrentUnixTime() noexcept {
	SDL_Time now = 0;
	if (!SDL_GetCurrentTime(&now))
		return 0;
	return int64_t(now / 1000000000LL);
}

} // namespace data::archive
