// Définitions de data/archive/archive_compressed.hpp
#include "data/archive/archive_compressed.hpp"

namespace data::archive {

bool IsXz(std::span<const uint8_t> bytes) noexcept {
	return bytes.size() >= 6 && std::memcmp(bytes.data(), detail::xz::STREAM_MAGIC, 6) == 0;
}

Option<Format> DetectCompression(std::span<const uint8_t> head) noexcept {
	if (IsGzip(head))
		return Some(Format::GZIP);
	if (IsXz(head))
		return Some(Format::XZ);
	if (IsBzip2(head))
		return Some(Format::BZIP2);
	if (IsZstd(head))
		return Some(Format::ZSTD);
	return NONE;
}

Format CompressionOf(Format format) noexcept {
	switch (format) {
	case Format::TAR_GZIP:
		return Format::GZIP;
	case Format::TAR_XZ:
		return Format::XZ;
	case Format::TAR_BZIP2:
		return Format::BZIP2;
	case Format::TAR_ZSTD:
		return Format::ZSTD;
	default:
		return format;
	}
}

Format TarFormatOf(Format compression) noexcept {
	switch (compression) {
	case Format::GZIP:
		return Format::TAR_GZIP;
	case Format::XZ:
		return Format::TAR_XZ;
	case Format::BZIP2:
		return Format::TAR_BZIP2;
	case Format::ZSTD:
		return Format::TAR_ZSTD;
	default:
		return Format::TAR;
	}
}

Result<ArchiveStream, ArchiveError> OpenDecompressor(ArchiveStream input, Format compression, Option<uint64_t> size) {
	switch (CompressionOf(compression)) {
	case Format::GZIP:
		return OpenGzipStream(std::move(input), size);
	case Format::XZ:
		return OpenXzStream(std::move(input));
	case Format::BZIP2:
		return OpenBzip2Stream(std::move(input), size);
	case Format::ZSTD:
		return OpenZstdStream(std::move(input), size);
	default:
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("format de compression inconnu")));
	}
}

Result<ArchiveStream, ArchiveError> OpenCompressor(ArchiveStream sink, Format compression, const WriteOptions& options,
		const String& name, int64_t modifiedTime, uint64_t sizeHint) {
	switch (CompressionOf(compression)) {
	case Format::GZIP:
		return OpenGzipEncoder(std::move(sink), options.level, name, modifiedTime);
	case Format::XZ:
		return OpenXzEncoder(std::move(sink), options.level, options.dictionarySize, XzCheck::CRC64,
							 sizeHint);
	case Format::BZIP2:
		return OpenBzip2Encoder(std::move(sink), std::clamp(options.level, 1, 9));
	case Format::ZSTD:
		// Pas de taille annoncée : un fichier qui change pendant l'écriture
		// rendrait la trame invalide.
		(void)sizeHint;
		return OpenZstdEncoder(std::move(sink), std::max(options.level, 1));
	default:
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("format de compression inconnu")));
	}
}

String StripCompressionExtension(const String& fileName) {
	const String base = BaseName(fileName);
	const String lower = base.ToLower();
	for (const char* extension : {".gz", ".xz", ".bz2", ".zst", ".lzma", ".z"})
		if (lower.EndsWith(extension) && lower.GetSize() > std::strlen(extension))
			return base.Substr(0, base.GetSize() - std::strlen(extension));
	return base.IsEmpty() ? String("contenu") : base + ".out";
}

// ── SingleFileReader ─────────────────────────────────────────────────────────

Result<std::unique_ptr<SingleFileReader>, ArchiveError> SingleFileReader::Open(ArchiveSource source, Format format, const ReadOptions& options,
		const String& nameHint) {
	std::unique_ptr<SingleFileReader> reader(
		new SingleFileReader(std::move(source), format, options));
	EntryInfo entry;
	entry.type = EntryType::FILE;
	entry.method = String(FormatName(format));
	entry.packedSize = reader->m_source.Size();
	if (format == Format::GZIP) {
		(void)reader->m_source.Stream().Seek(0, SDL_IO_SEEK_SET);
		auto header = ReadGzipHeader(reader->m_source.Stream());
		if (header.IsError())
			return Err(header.Error());
		entry.modifiedTime = header.Value().modifiedTime;
		auto normalized = NormalizePath(header.Value().originalName);
		if (normalized.IsOk() && !normalized.Value().IsEmpty())
			entry.path = BaseName(normalized.Value());
	}
	if (entry.path.IsEmpty())
		entry.path = StripCompressionExtension(nameHint);
	// Taille décompressée : connue sans tout décoder pour gzip (ISIZE du
	// dernier membre, modulo 4 Gio) ; sinon calculée à la première lecture.
	entry.size = reader->ProbeSize(format);
	reader->m_entries.push_back(std::move(entry));
	return Ok(std::move(reader));
}

const std::vector<EntryInfo>& SingleFileReader::Entries() const noexcept {
	return m_entries;
}

Result<ArchiveStream, ArchiveError> SingleFileReader::OpenEntry(size_t index) {
	if (index != 0)
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("entrée inexistante")));
	auto window = OpenSubStream(m_source.Stream(), 0, m_source.Size());
	if (window.IsError())
		return Err(window.Error());
	return OpenDecompressor(std::move(window).Unwrap(), m_format);
}

uint64_t SingleFileReader::ProbeSize(Format format) {
	const uint64_t size = m_source.Size();
	if (format == Format::GZIP && size >= 18) {
		auto tail = ReadRange(m_source.Stream(), size - 4, 4);
		if (tail.IsOk())
			return uint64_t(tail.Value()[0]) | (uint64_t(tail.Value()[1]) << 8) |
				   (uint64_t(tail.Value()[2]) << 16) | (uint64_t(tail.Value()[3]) << 24);
	}
	if (format == Format::ZSTD && size >= 6) {
		auto head = ReadRange(m_source.Stream(), 0, std::min<uint64_t>(size, 18));
		if (head.IsOk() && IsZstd(head.Value())) {
			const Bytes& h = head.Value();
			const uint8_t descriptor = h[4];
			const int fcsFlag = descriptor >> 6;
			const bool single = descriptor & 0x20;
			static constexpr int DICTIONARY[4] = {0, 1, 2, 4};
			static constexpr int FCS[4] = {0, 2, 4, 8};
			const size_t at = 5 + (single ? 0 : 1) + size_t(DICTIONARY[descriptor & 3]);
			const int fcsSize = fcsFlag == 0 && single ? 1 : FCS[fcsFlag];
			if (fcsSize > 0 && at + size_t(fcsSize) <= h.size()) {
				uint64_t value = 0;
				for (int i = 0; i < fcsSize; ++i)
					value |= uint64_t(h[at + size_t(i)]) << (8 * i);
				return fcsSize == 2 ? value + 256 : value;
			}
		}
	}
	if (format == Format::XZ && size >= 32) {
		// Pied : taille de l'index ; index : tailles décompressées des blocs.
		auto footer = ReadRange(m_source.Stream(), size - 12, 12);
		if (footer.IsOk() && footer.Value()[10] == 'Y' && footer.Value()[11] == 'Z') {
			const Bytes& f = footer.Value();
			const uint64_t backward = (uint64_t(f[4]) | (uint64_t(f[5]) << 8) |
									   (uint64_t(f[6]) << 16) | (uint64_t(f[7]) << 24)) *
										  4 +
									  4;
			if (backward + 12 <= size) {
				auto index = ReadRange(m_source.Stream(), size - 12 - backward, backward);
				if (index.IsOk()) {
					auto view = ViewStream(index.Value());
					if (view.IsOk()) {
						BinaryReader reader(view.Value());
						(void)reader.U8();
						const uint64_t count = detail::xz::ReadVarint(reader);
						uint64_t total = 0;
						for (uint64_t i = 0; i < count && reader.Ok(); ++i) {
							(void)detail::xz::ReadVarint(reader);
							total += detail::xz::ReadVarint(reader);
						}
						if (reader.Ok())
							return total; // (dernier flux seulement s'il y en a plusieurs)
					}
				}
			}
		}
	}
	return 0; // inconnue avant lecture
}

// ── SingleFileWriter ─────────────────────────────────────────────────────────

Result<bool, ArchiveError> SingleFileWriter::Write(const VirtualFs& fs, sdl3::IOStream& out,
		const WriteOptions& options) {
	std::vector<VirtualFs::NodeId> files;
	for (VirtualFs::NodeId id : fs.Flatten())
		if (fs.Get(id).IsFile())
			files.push_back(id);
	if (files.size() != 1)
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT,
							 String::Format("%s ne contient qu'un seul fichier (%d "
											"fournis) : utilisez tar.%s",
											FormatName(m_format), int(files.size()),
											FormatName(m_format))));
	if (options.encryption != Encryption::NONE)
		return Err(
			MakeError(ErrorKind::UNSUPPORTED,
					  String::Format("%s ne gère pas le chiffrement", FormatName(m_format))));
	const EntryInfo& info = fs.Get(files[0]).info;
	auto content = fs.OpenContent(files[0]);
	if (content.IsError())
		return Err(content.Error());
	auto sink = OpenBorrowedStream(out);
	if (sink.IsError())
		return Err(sink.Error());
	auto encoder = OpenCompressor(std::move(sink).Unwrap(), m_format, options, info.BaseName(),
								  info.modifiedTime, info.size);
	if (encoder.IsError())
		return Err(encoder.Error());
	if (auto copied = CopyToStream(content.Value(), encoder.Value()); copied.IsError())
		return Err(copied.Error());
	return FinishStream(encoder.Value());
}

} // namespace data::archive
