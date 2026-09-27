#pragma once
/**
 * data::archive — gzip (RFC 1952).
 *
 *  - `OpenGzipStream` : décompression en flux ; membres concaténés (comme
 *    `gzip -d`), remplissage nul final toléré ; en-tête (FEXTRA, FNAME,
 *    FCOMMENT, FHCRC vérifié), CRC-32 et taille de chaque membre vérifiés ;
 *  - `ReadGzipHeader` : nom d'origine et date du premier membre ;
 *  - `OpenGzipEncoder` : compression en flux (nom d'origine, date, XFL,
 * OS=Unix) ;
 *  - `GzipDecompress`, `GzipCompress` : tampons complets.
 *
 * Les champs d'en-tête et de pied sont lus et écrits octet par octet,
 * petit-boutistes (MTIME, CRC32, ISIZE en U32 LE).
 */
#include "../../core/core.hpp"
#include "archive_crc.hpp"
#include "archive_deflate.hpp"
#include "archive_io.hpp"
#include "archive_stream.hpp"

#include <memory>
#include <vector>

namespace data::archive {

struct GzipHeader {
	String originalName;
	int64_t modifiedTime = 0;
};

struct GzipContent {
	Bytes bytes;
	String originalName;
	int64_t modifiedTime = 0;
};

[[nodiscard]] bool IsGzip(std::span<const uint8_t> bytes) noexcept;

namespace detail::gzip {

/// Lit un en-tête de membre ; `Some(header)`, NONE si l'entrée est finie
/// (pas même un octet), Err si l'en-tête est invalide.
[[nodiscard]] Result<Option<GzipHeader>, ArchiveError> ReadMemberHeader(InputBuffer& input,
																			   bool afterMember);

class GzipStreamImpl : public DecoderImpl {
public:
	GzipStreamImpl(ArchiveStream input, Option<uint64_t> size, uint64_t sizeLimit,
				   StreamStatePtr state);

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	/// Pied de 8 octets : d'abord les octets entiers restés dans la réserve
	/// de bits de l'inflate, puis l'entrée.
	bool ReadTrailer(uint8_t* trailer);

	ArchiveStream m_input;
	InputBuffer m_buffer;
	deflate::Inflater m_inflater;
	uint64_t m_sizeLimit;
	bool m_inMember = false, m_done = false;
	uint32_t m_crc = 0;
	uint64_t m_memberSize = 0, m_total = 0;
	int m_members = 0;
};

class GzipEncoderImpl : public EncoderImpl {
public:
	GzipEncoderImpl(ArchiveStream sink, int level, String name, int64_t modifiedTime,
					StreamStatePtr state)
		: EncoderImpl(std::move(sink), std::move(state)), m_compressor(level), m_level(level),
		  m_name(std::move(name)), m_modifiedTime(modifiedTime) {}

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override;

private:
	Result<bool, ArchiveError> WriteHeader();

	deflate::Compressor m_compressor;
	int m_level;
	String m_name;
	int64_t m_modifiedTime;
	bool m_headerWritten = false;
	uint32_t m_crc = 0;
	uint64_t m_size = 0;
	size_t m_calls = 0;
};

} // namespace detail::gzip

/// Flux décompressé d'un fichier gzip (tous ses membres).
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenGzipStream(ArchiveStream input, Option<uint64_t> size = NONE, uint64_t sizeLimit = UINT64_MAX);

/// En-tête du premier membre (nom d'origine, date) ; la position du flux est
/// restaurée.
[[nodiscard]] Result<GzipHeader, ArchiveError> ReadGzipHeader(sdl3::IOStream& stream);

/// Flux en écriture : fichier gzip complet vers `sink` (fermer avec
/// `FinishStream`).
[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenGzipEncoder(ArchiveStream sink,
																		 int level = 6,
																		 const String& name = "",
																		 int64_t modifiedTime = 0);

/// Décompresse un fichier gzip (tous ses membres) en mémoire.
[[nodiscard]] Result<GzipContent, ArchiveError>
GzipDecompress(std::span<const uint8_t> input, uint64_t sizeLimit = UINT64_MAX);

[[nodiscard]] Result<Bytes, ArchiveError> GzipCompress(std::span<const uint8_t> input,
															  const String& name = "",
															  int64_t modifiedTime = 0,
															  int level = 6);

} // namespace data::archive
