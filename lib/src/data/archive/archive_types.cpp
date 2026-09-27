// Définitions de data/archive/archive_types.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "data/archive/archive_types.hpp"

namespace data::archive {

const char* FormatName(Format format) noexcept {
	switch (format) {
	case Format::ZIP:
		return "zip";
	case Format::GZIP:
		return "gzip";
	case Format::XZ:
		return "xz";
	case Format::BZIP2:
		return "bzip2";
	case Format::ZSTD:
		return "zstd";
	case Format::TAR:
		return "tar";
	case Format::TAR_GZIP:
		return "tar.gz";
	case Format::TAR_XZ:
		return "tar.xz";
	case Format::TAR_BZIP2:
		return "tar.bz2";
	case Format::TAR_ZSTD:
		return "tar.zst";
	case Format::SEVEN_ZIP:
		return "7z";
	case Format::ISO9660:
		return "iso";
	case Format::RAR:
		return "rar";
	}
	return "?";
}

bool IsSingleFileFormat(Format format) noexcept {
	return format == Format::GZIP || format == Format::XZ || format == Format::BZIP2 ||
		   format == Format::ZSTD;
}

const char* CompressionName(Compression compression) noexcept {
	switch (compression) {
	case Compression::STORE:
		return "stockée";
	case Compression::DEFLATE:
		return "deflate";
	case Compression::LZMA:
		return "lzma";
	case Compression::LZMA2:
		return "lzma2";
	case Compression::BZIP2:
		return "bzip2";
	case Compression::ZSTD:
		return "zstd";
	case Compression::PPMD:
		return "ppmd";
	}
	return "?";
}

// ── EntryInfo ────────────────────────────────────────────────────────────────

String EntryInfo::BaseName() const {
	size_t slash = path.Rfind('/');
	return slash == String::NPOS ? path : path.Substr(slash + 1);
}

const char* ErrorKindName(ErrorKind kind) noexcept {
	switch (kind) {
	case ErrorKind::IO:
		return "entrée/sortie";
	case ErrorKind::FORMAT:
		return "format";
	case ErrorKind::CORRUPT:
		return "archive corrompue";
	case ErrorKind::UNSUPPORTED:
		return "non géré";
	case ErrorKind::PASSWORD_REQUIRED:
		return "mot de passe requis";
	case ErrorKind::WRONG_PASSWORD:
		return "mot de passe incorrect";
	case ErrorKind::UNSAFE_PATH:
		return "chemin dangereux";
	case ErrorKind::LIMIT:
		return "limite dépassée";
	case ErrorKind::INVALID_ARGUMENT:
		return "argument invalide";
	}
	return "?";
}

// ── ArchiveError ─────────────────────────────────────────────────────────────

String ArchiveError::Describe() const {
	return String::Format("%s : %s", ErrorKindName(kind), message.CStr());
}

bool ArchiveError::NeedsPassword() const noexcept {
	return kind == ErrorKind::PASSWORD_REQUIRED || kind == ErrorKind::WRONG_PASSWORD;
}

ArchiveError MakeError(ErrorKind kind, String message) {
	return ArchiveError{kind, std::move(message)};
}

} // namespace data::archive
