#pragma once
/**
 * data::archive — vocabulaire commun à tous les formats.
 *
 * Formats, méthodes de compression et de chiffrement, options de lecture et
 * d'écriture, description d'une entrée, erreurs typées, et les deux
 * interfaces que chaque format implémente :
 *
 *  - `ArchiveReader` / `ArchiveWriter` : définis dans archive_stream.hpp
 *    (la lecture d'une entrée rend un flux).
 *
 * Les erreurs sont TYPÉES (`ErrorKind`) : une interface peut ainsi demander un
 * mot de passe sur `PASSWORD_REQUIRED`/`WRONG_PASSWORD` au lieu d'analyser un
 * message.
 */
#include "../../core/core.hpp"
#include "../../sdl3/iostream.hpp"
#include "archive_io.hpp"

#include <cstdint>
#include <vector>

namespace data::archive {

class VirtualFs;

enum class Format : uint8_t {
	ZIP,
	GZIP,
	XZ,
	BZIP2,
	ZSTD,
	TAR,
	TAR_GZIP,
	TAR_XZ,
	TAR_BZIP2,
	TAR_ZSTD,
	SEVEN_ZIP,
	ISO9660,
	RAR, ///< lecture seule (format propriétaire)
};

[[nodiscard]] inline const char* FormatName(Format format) noexcept {
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

/// Formats « un seul fichier compressé » (.gz, .xz, .bz2, .zst).
[[nodiscard]] inline bool IsSingleFileFormat(Format format) noexcept {
	return format == Format::GZIP || format == Format::XZ || format == Format::BZIP2 ||
		   format == Format::ZSTD;
}

enum class Compression : uint8_t { STORE, DEFLATE, LZMA, LZMA2, BZIP2, ZSTD, PPMD };

[[nodiscard]] inline const char* CompressionName(Compression compression) noexcept {
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

/// AES256 : zip WinZip AE-2 et 7z. ZIP_CRYPTO : chiffrement zip historique,
/// FAIBLE — à réserver à la compatibilité avec de vieux outils.
enum class Encryption : uint8_t { NONE, AES256, ZIP_CRYPTO };

enum class EntryType : uint8_t { FILE, DIRECTORY, SYMLINK };

/// Description d'une entrée, commune à tous les formats.
struct EntryInfo {
	String path; ///< normalisé : séparateur '/', ni '/' initial ni final
	EntryType type = EntryType::FILE;
	uint64_t size = 0;		 ///< taille décompressée
	uint64_t packedSize = 0; ///< taille dans l'archive (0 si inconnue : solide 7z, tar.gz…)
	Option<uint32_t> crc32 = NONE;
	String method; ///< « deflate », « lzma2 + aes-256 », « stockée »…
	bool encrypted = false;
	int64_t modifiedTime = 0; ///< secondes Unix, 0 si inconnue
	uint32_t mode = 0;		  ///< permissions Unix (0 si inconnues)
	String linkTarget;		  ///< cible d'un lien symbolique

	[[nodiscard]] String BaseName() const {
		size_t slash = path.Rfind('/');
		return slash == String::NPOS ? path : path.Substr(slash + 1);
	}
	[[nodiscard]] bool IsDirectory() const noexcept { return type == EntryType::DIRECTORY; }
};

struct ReadOptions {
	String password;
	bool verifyChecksums = true;
	/// Taille décompressée maximale acceptée pour une entrée (garde-fou contre
	/// les bombes de décompression) : 4 Gio par défaut.
	uint64_t maxEntrySize = uint64_t(1) << 32;
	/// Accès aléatoire dans un flux compressé d'un seul tenant (tar.gz, tar.xz,
	/// tar.bz2, tar.zst, bloc solide 7z, RAR solide) : s'il décompresse en
	/// moins de `memoryCacheLimit` octets, il est gardé en mémoire ; sinon il
	/// est relu depuis le début quand on recule (mémoire bornée, plus lent).
	/// L'extraction complète (`ExtractAll`) le lit toujours en un seul passage.
	uint64_t memoryCacheLimit = uint64_t(64) << 20;
};

struct WriteOptions {
	Compression compression = Compression::DEFLATE;
	int level = 6; ///< 0 (aucun effort) à 9
	Encryption encryption = Encryption::NONE;
	String password;
	bool encryptHeaders = false; ///< 7z : chiffre aussi la liste des fichiers
	bool solid = true;			 ///< 7z : un seul flux compressé pour tous les fichiers
	uint32_t dictionarySize = uint32_t(1) << 22;
	unsigned ppmdOrder = 6;		///< PPMd : ordre du modèle (2 à 16 pour zip, 64 pour 7z)
	uint32_t ppmdMemoryMb = 16; ///< PPMd : mémoire du modèle, en Mio
	/// 7z solide : taille maximale (décompressée) d'un bloc ; au-delà, un
	/// nouveau bloc commence (mémoire bornée à l'écriture et à la lecture).
	uint64_t solidBlockSize = uint64_t(64) << 20;
	String volumeLabel = "ARCHIVE"; ///< ISO 9660
	int64_t defaultTime = 0;		///< date des entrées sans date (0 = maintenant)
};

enum class ErrorKind : uint8_t {
	IO,				   ///< lecture/écriture impossible
	FORMAT,			   ///< ce n'est pas (ou plus) une archive de ce format
	CORRUPT,		   ///< données incohérentes, CRC faux
	UNSUPPORTED,	   ///< variante non gérée (méthode, ZIP64 multi-volumes…)
	PASSWORD_REQUIRED, ///< entrée ou en-têtes chiffrés, aucun mot de passe fourni
	WRONG_PASSWORD,	   ///< mot de passe refusé (vérificateur ou authentification)
	UNSAFE_PATH,	   ///< chemin sortant de la racine (« zip slip »)
	LIMIT,			   ///< garde-fou de taille dépassé
	INVALID_ARGUMENT,  ///< appel incorrect (entrée inexistante, options
					   ///< incompatibles)
};

[[nodiscard]] inline const char* ErrorKindName(ErrorKind kind) noexcept {
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

struct ArchiveError {
	ErrorKind kind = ErrorKind::FORMAT;
	String message;

	[[nodiscard]] String Describe() const {
		return String::Format("%s : %s", ErrorKindName(kind), message.CStr());
	}
	[[nodiscard]] bool NeedsPassword() const noexcept {
		return kind == ErrorKind::PASSWORD_REQUIRED || kind == ErrorKind::WRONG_PASSWORD;
	}
};

[[nodiscard]] inline ArchiveError MakeError(ErrorKind kind, String message) {
	return ArchiveError{kind, std::move(message)};
}

} // namespace data::archive
