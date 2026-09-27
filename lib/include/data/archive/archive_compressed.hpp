#pragma once
/**
 * data::archive — fichiers compressés seuls : .gz, .xz, .bz2, .zst.
 *
 *  - `DetectCompression` : format d'après les octets de tête ;
 *  - `OpenDecompressor` / `OpenCompressor` : flux de (dé)compression d'un
 *    format donné, pour les fichiers seuls et pour les tar compressés ;
 *  - `SingleFileReader` : lecteur à une entrée (le contenu décompressé),
 *    en flux : rien n'est décompressé avant qu'on lise ;
 *  - `SingleFileWriter` : écrivain d'un arbre contenant UN fichier.
 */
#include "../../core/core.hpp"
#include "archive_bzip2.hpp"
#include "archive_fs.hpp"
#include "archive_gzip.hpp"
#include "archive_io.hpp"
#include "archive_lzma.hpp"
#include "archive_stream.hpp"
#include "archive_types.hpp"
#include "archive_zstd.hpp"

#include <memory>
#include <vector>

namespace data::archive {

[[nodiscard]] bool IsXz(std::span<const uint8_t> bytes) noexcept;

/// Format de compression d'après la signature (GZIP, XZ, BZIP2, ZSTD).
[[nodiscard]] Option<Format> DetectCompression(std::span<const uint8_t> head) noexcept;

/// Format de compression d'un tar compressé (TAR_GZIP → GZIP…).
[[nodiscard]] Format CompressionOf(Format format) noexcept;

/// Tar compressé correspondant à un format de compression (GZIP → TAR_GZIP…).
[[nodiscard]] Format TarFormatOf(Format compression) noexcept;

/// Flux décompressé de `input` (position courante) dans le format donné.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenDecompressor(ArchiveStream input, Format compression, Option<uint64_t> size = NONE);

/// Flux en écriture compressant vers `sink` (fermer avec `FinishStream`).
/// `name`/`modifiedTime` : inscrits dans l'en-tête gzip.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenCompressor(ArchiveStream sink, Format compression, const WriteOptions& options,
			   const String& name = "", int64_t modifiedTime = 0, uint64_t sizeHint = UINT64_MAX);

/// Nom d'une entrée d'après celui de l'archive : « rom.gba.gz » → « rom.gba ».
[[nodiscard]] String StripCompressionExtension(const String& fileName);

/// Lecteur « une seule entrée » (fichier compressé seul).
class SingleFileReader final : public ArchiveReader {
public:
	/// `source` : le fichier compressé ; `nameHint` : son nom (sert à nommer
	/// l'entrée quand le format ne garde pas le nom d'origine).
	[[nodiscard]] static Result<std::unique_ptr<SingleFileReader>, ArchiveError>
	Open(ArchiveSource source, Format format, const ReadOptions& options,
		 const String& nameHint = "");

	[[nodiscard]] Format GetFormat() const noexcept override { return m_format; }
	[[nodiscard]] const std::vector<EntryInfo>& Entries() const noexcept override;
	void SetPassword(const String&) override {}
	[[nodiscard]] uint64_t EntryLimit() const noexcept override { return m_options.maxEntrySize; }

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenEntry(size_t index) override;

private:
	SingleFileReader(ArchiveSource source, Format format, ReadOptions options)
		: m_source(std::move(source)), m_format(format), m_options(std::move(options)) {}

	uint64_t ProbeSize(Format format);

	ArchiveSource m_source;
	Format m_format;
	ReadOptions m_options;
	std::vector<EntryInfo> m_entries;
};

/// Écrivain gzip / xz / bzip2 / zstd : l'arbre doit contenir exactement UN
/// fichier.
class SingleFileWriter final : public ArchiveWriter {
public:
	explicit SingleFileWriter(Format format) noexcept : m_format(format) {}
	[[nodiscard]] Format GetFormat() const noexcept override { return m_format; }

	[[nodiscard]] Result<bool, ArchiveError> Write(const VirtualFs& fs, sdl3::IOStream& out,
												   const WriteOptions& options) override;

private:
	Format m_format;
};

} // namespace data::archive
