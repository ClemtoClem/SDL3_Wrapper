#pragma once
/**
 * data::archive — archives et compression, sans dépendance externe.
 *
 * Façade du module : elle inclut tous les formats et offre une interface
 * UNIQUE pour lire (désarchiver / décompresser) et écrire (archiver /
 * compresser) n'importe lequel d'entre eux. Tout se fait EN FLUX : une entrée
 * de plusieurs gigaoctets s'extrait avec quelques mégaoctets de mémoire.
 *
 * ── Fichiers du module (data/archive/) ───────────────────────────────────
 *  archive_io.hpp         lecteur/écrivain binaires sur sdl3::IOStream (LE,
 *                         BE, « deux boutismes »), UTF-16, CP437, dates
 *  archive_stream.hpp     flux d'archive (`ArchiveStream`), décodeurs et
 *                         encodeurs en flux, fenêtres, tampons, interfaces
 *                         `ArchiveReader` / `ArchiveWriter`
 *  archive_crc.hpp        CRC-32, CRC-16, CRC-64 (xz), XXH64 (zstd)
 *  archive_crypto.hpp     AES (ECB, CBC, CTR WinZip, flux CBC), SHA-1,
 *                         SHA-256 (SHA-NI), HMAC, PBKDF2, aléa, ZipCrypto
 *  archive_deflate.hpp    DEFLATE / Deflate64
 *  archive_lzma.hpp       LZMA, LZMA2, .lzma, .xz
 *  archive_bzip2.hpp      bzip2
 *  archive_zstd.hpp       Zstandard
 *  archive_ppmd.hpp       PPMd variantes H (7z, RAR) et I (zip)
 *  archive_filters.hpp    filtres BCJ (x86, ARM, ARMT, ARM64, PPC, SPARC,
 *                         IA64), BCJ2, Delta
 *  archive_gzip.hpp       gzip (membres multiples)
 *  archive_compressed.hpp fichiers compressés seuls (.gz, .xz, .bz2, .zst)
 *  archive_types.hpp      formats, options, erreurs typées
 *  archive_fs.hpp         système de fichiers virtuel, chemins sûrs,
 *                         navigation (`VirtualFs`, `Navigator`, glob)
 *  archive_platform.hpp   liens symboliques sur disque (absents de SDL3)
 *  archive_zip.hpp        zip (ZIP64, AES, ZipCrypto, 7 méthodes)
 *  archive_tar.hpp        tar (ustar, pax, GNU, fichiers creux), compressé ou
 * non archive_7z.hpp         7z (solide, toutes méthodes courantes, AES-256)
 *  archive_iso.hpp        ISO 9660 + Joliet + Rock Ridge
 *  archive_rar.hpp        RAR 1.5 à 7 (lecture seule, volumes, chiffrement)
 *
 * ── Utilisation ──────────────────────────────────────────────────────────
 *
 *   // Lire : format détecté par les octets, jamais par l'extension.
 *   auto archive = data::archive::Archive::Open("jeux.7z");
 *   if (archive.IsError() && archive.Error().NeedsPassword()) …
 *   data::archive::Navigator &nav = archive.Value().Nav();
 *   nav.ChangeDirectory("roms/gba");
 *   for (auto id : nav.Glob("*.gba")) …
 *   auto bytes = archive.Value().Read("roms/gba/jeu.gba");   // en mémoire
 *   auto stream = archive.Value().OpenFile("roms/gba/jeu.gba"); // en flux
 *
 *   // Écrire : un VirtualFs vers n'importe quel format (sauf RAR).
 *   data::archive::VirtualFs fs;
 *   fs.AddDiskPath("sauvegardes");
 *   data::archive::WriteOptions options;
 *   options.compression = data::archive::Compression::LZMA2;
 *   options.encryption = data::archive::Encryption::AES256;
 *   options.password = "secret";
 *   data::archive::WriteArchiveFile("sauvegardes.7z",
 * data::archive::Format::SEVEN_ZIP, fs, options);
 *
 * Aucune exception : `Result<…, ArchiveError>` (cf.
 * memory/feedback_no_exceptions.md).
 */
#include "../core/core.hpp"
#include "../sdl3/filesystem.hpp"
#include "../sdl3/iostream.hpp"
#include "archive/archive_7z.hpp"
#include "archive/archive_bzip2.hpp"
#include "archive/archive_compressed.hpp"
#include "archive/archive_crc.hpp"
#include "archive/archive_crypto.hpp"
#include "archive/archive_deflate.hpp"
#include "archive/archive_filters.hpp"
#include "archive/archive_fs.hpp"
#include "archive/archive_gzip.hpp"
#include "archive/archive_io.hpp"
#include "archive/archive_iso.hpp"
#include "archive/archive_lzma.hpp"
#include "archive/archive_platform.hpp"
#include "archive/archive_ppmd.hpp"
#include "archive/archive_rar.hpp"
#include "archive/archive_stream.hpp"
#include "archive/archive_tar.hpp"
#include "archive/archive_types.hpp"
#include "archive/archive_zip.hpp"
#include "archive/archive_zstd.hpp"

#include <memory>
#include <vector>

namespace data::archive {

// ============================================================================
// Détection et extensions
// ============================================================================

/// Format d'après les octets de tête. Les fichiers compressés (gzip, xz,
/// bzip2, zstd) sont rendus tels quels : `OpenArchive` regarde ensuite s'ils
/// contiennent un tar.
[[nodiscard]] inline Option<Format> DetectFormat(sdl3::IOStream& stream) {
	const Sint64 size = stream.GetSize();
	if (size <= 0)
		return NONE;
	auto head = ReadRange(stream, 0, std::min<uint64_t>(uint64_t(size), detail::tar::BLOCK));
	if (head.IsError())
		return NONE;
	const Bytes& bytes = head.Value();
	auto starts = [&bytes](std::initializer_list<uint8_t> magic) {
		return bytes.size() >= magic.size() &&
			   std::equal(magic.begin(), magic.end(), bytes.begin());
	};
	if (starts({'P', 'K', 3, 4}) || starts({'P', 'K', 5, 6}) || starts({'P', 'K', 7, 8}))
		return Some(Format::ZIP);
	if (starts({'7', 'z', 0xBC, 0xAF, 0x27, 0x1C}))
		return Some(Format::SEVEN_ZIP);
	if (RarReader::LooksLikeRar(bytes))
		return Some(Format::RAR);
	if (Option<Format> compression = DetectCompression(bytes); compression.IsSome())
		return compression;
	if (IsoReader::LooksLikeIso(stream))
		return Some(Format::ISO9660);
	if (detail::tar::LooksLikeTar(bytes))
		return Some(Format::TAR);
	// Auto-extractible (exécutable Windows ou ELF) : zip (fin de répertoire
	// central à la fin du fichier) ou RAR (signature dans les premiers Mio).
	const bool executable = starts({'M', 'Z'}) || starts({0x7F, 'E', 'L', 'F'});
	const uint64_t tail = std::min<uint64_t>(uint64_t(size), 22 + 65535);
	auto end = ReadRange(stream, uint64_t(size) - tail, tail);
	if (end.IsOk()) {
		const Bytes& t = end.Value();
		for (size_t i = t.size() >= 22 ? t.size() - 22 + 1 : 0; i-- > 0;)
			if (t[i] == 'P' && t[i + 1] == 'K' && t[i + 2] == 5 && t[i + 3] == 6)
				return Some(Format::ZIP);
	}
	if (executable) {
		const uint64_t scan = std::min<uint64_t>(uint64_t(size), detail::rar::MAX_SFX_SIZE);
		for (uint64_t base = 0; base < scan; base += 1 << 16) {
			auto chunk =
				ReadRange(stream, base, std::min<uint64_t>((1 << 16) + 8, uint64_t(size) - base));
			if (chunk.IsError())
				break;
			const Bytes& c = chunk.Value();
			for (size_t i = 0; i + 7 <= c.size(); ++i)
				if (c[i] == 'R' && RarReader::LooksLikeRar(std::span<const uint8_t>(c).subspan(i)))
					return Some(Format::RAR);
		}
	}
	return NONE;
}

[[nodiscard]] inline Option<Format> FormatFromExtension(const String& path) {
	const String lower = path.ToLower();
	struct Suffix {
		const char* text;
		Format format;
	};
	// Du plus long au plus court : « .tar.gz » avant « .gz ».
	static constexpr Suffix SUFFIXES[] = {{".tar.gz", Format::TAR_GZIP},
										  {".tgz", Format::TAR_GZIP},
										  {".tar.xz", Format::TAR_XZ},
										  {".txz", Format::TAR_XZ},
										  {".tar.bz2", Format::TAR_BZIP2},
										  {".tbz2", Format::TAR_BZIP2},
										  {".tbz", Format::TAR_BZIP2},
										  {".tar.zst", Format::TAR_ZSTD},
										  {".tzst", Format::TAR_ZSTD},
										  {".zip", Format::ZIP},
										  {".jar", Format::ZIP},
										  {".cbz", Format::ZIP},
										  {".tar", Format::TAR},
										  {".cbt", Format::TAR},
										  {".gz", Format::GZIP},
										  {".xz", Format::XZ},
										  {".bz2", Format::BZIP2},
										  {".zst", Format::ZSTD},
										  {".7z", Format::SEVEN_ZIP},
										  {".cb7", Format::SEVEN_ZIP},
										  {".iso", Format::ISO9660},
										  {".rar", Format::RAR},
										  {".cbr", Format::RAR}};
	for (const Suffix& suffix : SUFFIXES)
		if (lower.EndsWith(suffix.text))
			return Some(suffix.format);
	return NONE;
}

[[nodiscard]] inline const char* FormatExtension(Format format) noexcept {
	switch (format) {
	case Format::ZIP:
		return ".zip";
	case Format::GZIP:
		return ".gz";
	case Format::XZ:
		return ".xz";
	case Format::BZIP2:
		return ".bz2";
	case Format::ZSTD:
		return ".zst";
	case Format::TAR:
		return ".tar";
	case Format::TAR_GZIP:
		return ".tar.gz";
	case Format::TAR_XZ:
		return ".tar.xz";
	case Format::TAR_BZIP2:
		return ".tar.bz2";
	case Format::TAR_ZSTD:
		return ".tar.zst";
	case Format::SEVEN_ZIP:
		return ".7z";
	case Format::ISO9660:
		return ".iso";
	case Format::RAR:
		return ".rar";
	}
	return "";
}

[[nodiscard]] inline bool HasArchiveExtension(const String& path) {
	return FormatFromExtension(path).IsSome();
}

/// Vrai si le format peut être écrit (RAR : lecture seule).
[[nodiscard]] inline bool IsWritableFormat(Format format) noexcept {
	return format != Format::RAR;
}

// ============================================================================
// Lecture générique
// ============================================================================

/// Ouvre une archive de n'importe quel format. `nameHint` (nom du fichier)
/// nomme l'entrée d'un fichier compressé seul qui ne porte pas son nom
/// d'origine ; `volumes` ouvre les volumes suivants d'un RAR multi-volumes.
[[nodiscard]] inline Result<std::unique_ptr<ArchiveReader>, ArchiveError>
OpenArchive(ArchiveSource source, const ReadOptions& options = {}, const String& nameHint = "",
			RarVolumeOpener volumes = {}) {
	auto upcast = [](auto result) -> Result<std::unique_ptr<ArchiveReader>, ArchiveError> {
		if (result.IsError())
			return Err(result.Error());
		return Ok(std::unique_ptr<ArchiveReader>(std::move(result).Unwrap()));
	};
	Option<Format> format = DetectFormat(source.Stream());
	if (format.IsNone())
		return Err(MakeError(ErrorKind::FORMAT,
							 String("format d'archive non reconnu (zip, 7z, rar, tar, iso, gz, "
									"xz, bz2 ou zst attendu)")));
	switch (format.Unwrap()) {
	case Format::ZIP:
		return upcast(ZipReader::Open(std::move(source), options));
	case Format::SEVEN_ZIP:
		return upcast(SevenZipReader::Open(std::move(source), options));
	case Format::ISO9660:
		return upcast(IsoReader::Open(std::move(source), options));
	case Format::TAR:
		return upcast(TarReader::Open(std::move(source), options));
	case Format::RAR:
		return upcast(RarReader::Open(std::move(source), options, std::move(volumes)));
	default:
		break;
	}
	// Fichier compressé : un tar s'il commence par un en-tête tar valide
	// (seul le premier bloc est décompressé pour le savoir).
	bool tar = false;
	{
		auto window = OpenSubStream(source.Stream(), 0, source.Size());
		if (window.IsError())
			return Err(window.Error());
		auto decoder = OpenDecompressor(std::move(window).Unwrap(), format.Unwrap());
		if (decoder.IsError())
			return Err(decoder.Error());
		uint8_t block[detail::tar::BLOCK];
		auto got = StreamRead(decoder.Value(), block, sizeof(block));
		if (got.IsError())
			return Err(got.Error());
		tar = detail::tar::LooksLikeTar(std::span<const uint8_t>(block, got.Value()));
	}
	if (tar)
		return upcast(TarReader::OpenCompressed(std::move(source), options));
	return upcast(SingleFileReader::Open(std::move(source), format.Unwrap(), options, nameHint));
}

/// Volumes suivants d'un RAR multi-volumes, cherchés à côté de `path`.
[[nodiscard]] inline RarVolumeOpener DiskVolumeOpener(const String& path) {
	return [path](uint32_t index, bool newNumbering) -> Option<ArchiveSource> {
		const String name = RarVolumeName(path, index, newNumbering);
		if (name.IsEmpty())
			return NONE;
		auto source = ArchiveSource::FromFile(name);
		if (source.IsError())
			return NONE;
		return Some(std::move(source).Unwrap());
	};
}

[[nodiscard]] inline Result<std::unique_ptr<ArchiveReader>, ArchiveError>
OpenArchiveFile(const String& path, const ReadOptions& options = {}) {
	auto source = ArchiveSource::FromFile(path);
	if (source.IsError())
		return Err(MakeError(ErrorKind::IO, source.Error()));
	return OpenArchive(std::move(source).Unwrap(), options, path, DiskVolumeOpener(path));
}

[[nodiscard]] inline Result<std::unique_ptr<ArchiveReader>, ArchiveError>
OpenArchiveBytes(Bytes bytes, const ReadOptions& options = {}, const String& nameHint = "") {
	auto source = ArchiveSource::FromBytes(std::move(bytes));
	if (source.IsError())
		return Err(MakeError(ErrorKind::IO, source.Error()));
	return OpenArchive(std::move(source).Unwrap(), options, nameHint);
}

// ============================================================================
// Écriture générique
// ============================================================================

[[nodiscard]] inline std::unique_ptr<ArchiveWriter> CreateWriter(Format format) {
	switch (format) {
	case Format::ZIP:
		return std::make_unique<ZipWriter>();
	case Format::GZIP:
	case Format::XZ:
	case Format::BZIP2:
	case Format::ZSTD:
		return std::make_unique<SingleFileWriter>(format);
	case Format::TAR:
	case Format::TAR_GZIP:
	case Format::TAR_XZ:
	case Format::TAR_BZIP2:
	case Format::TAR_ZSTD:
		return std::make_unique<TarWriter>(format);
	case Format::SEVEN_ZIP:
		return std::make_unique<SevenZipWriter>();
	case Format::ISO9660:
		return std::make_unique<IsoWriter>();
	case Format::RAR:
		return nullptr; // format propriétaire : lecture seule
	}
	return nullptr;
}

[[nodiscard]] inline Result<bool, ArchiveError> WriteArchive(Format format, const VirtualFs& fs,
															 sdl3::IOStream& out,
															 const WriteOptions& options = {}) {
	std::unique_ptr<ArchiveWriter> writer = CreateWriter(format);
	if (!writer)
		return Err(
			MakeError(ErrorKind::UNSUPPORTED,
					  String::Format("écriture au format %s non gérée%s", FormatName(format),
									 format == Format::RAR ? " (format propriétaire)" : "")));
	if (options.level < 0 || options.level > 9)
		return Err(
			MakeError(ErrorKind::INVALID_ARGUMENT, String("niveau de compression hors de 0..9")));
	return writer->Write(fs, out, options);
}

[[nodiscard]] inline Result<Bytes, ArchiveError>
WriteArchiveBytes(Format format, const VirtualFs& fs, const WriteOptions& options = {}) {
	auto stream = CreateMemoryWriter();
	if (stream.IsError())
		return Err(MakeError(ErrorKind::IO, stream.Error()));
	auto written = WriteArchive(format, fs, stream.Value(), options);
	if (written.IsError())
		return Err(written.Error());
	return Ok(stream.Value().DynamicMemoryBytes());
}

/// Écrit dans un fichier temporaire voisin puis le renomme : une archive
/// existante n'est jamais laissée à moitié écrite.
[[nodiscard]] inline Result<bool, ArchiveError> WriteArchiveFile(const String& path, Format format,
																 const VirtualFs& fs,
																 const WriteOptions& options = {}) {
	const String temporary = path + ".partial";
	{
		auto stream = sdl3::IOStream::FromFile(temporary, "wb");
		if (stream.IsError())
			return Err(MakeError(ErrorKind::IO,
								 String::Format("création de %s impossible : %s", temporary.CStr(),
												String(stream.Error()).CStr())));
		auto written = WriteArchive(format, fs, stream.Value(), options);
		if (written.IsError() || !stream.Value().Flush()) {
			stream.Value().Close();
			(void)sdl3::filesystem::Remove(temporary);
			return written.IsError()
					   ? written
					   : Result<bool, ArchiveError>(
							 Err(MakeError(ErrorKind::IO, String("écriture interrompue"))));
		}
		if (!stream.Value().Close()) {
			(void)sdl3::filesystem::Remove(temporary);
			return Err(MakeError(ErrorKind::IO,
								 String::Format("fermeture de %s impossible", temporary.CStr())));
		}
	}
	if (!sdl3::filesystem::Rename(temporary, path)) {
		(void)sdl3::filesystem::Remove(temporary);
		return Err(
			MakeError(ErrorKind::IO, String::Format("renommage vers %s impossible", path.CStr())));
	}
	return Ok(true);
}

/// Archive un fichier ou dossier du disque (format déduit de l'extension si
/// `format` est NONE).
[[nodiscard]] inline Result<size_t, ArchiveError> PackPath(const String& diskPath,
														   const String& archivePath,
														   Option<Format> format = NONE,
														   const WriteOptions& options = {}) {
	Option<Format> chosen = format.IsSome() ? format : FormatFromExtension(archivePath);
	if (chosen.IsNone())
		return Err(
			MakeError(ErrorKind::INVALID_ARGUMENT,
					  String::Format("%s : extension d'archive inconnue", archivePath.CStr())));
	VirtualFs fs;
	auto added = fs.AddDiskPath(diskPath);
	if (added.IsError())
		return Err(added.Error());
	auto written = WriteArchiveFile(archivePath, chosen.Unwrap(), fs, options);
	if (written.IsError())
		return Err(written.Error());
	return Ok(added.Value());
}

// ============================================================================
// Archive : lecteur + arborescence + navigation
// ============================================================================

struct ExtractOptions {
	/// Crée les liens symboliques (après tous les fichiers ; ceux dont la
	/// cible sortirait du dossier d'extraction sont refusés). Faux : ignorés.
	bool createSymlinks = true;
	/// Remplace les fichiers existants (sinon ils sont laissés et comptés
	/// dans `skippedFiles`).
	bool overwrite = true;
};

struct ExtractStats {
	size_t files = 0;
	size_t directories = 0;
	size_t links = 0;		 ///< liens symboliques créés
	size_t skippedLinks = 0; ///< liens non créés (désactivés, refusés ou impossibles)
	size_t skippedFiles = 0; ///< fichiers existants conservés (`overwrite` faux)
	uint64_t bytes = 0;
};

namespace detail {

/// Vrai si la cible `target` d'un lien placé en `linkPath` (chemins
/// d'archive normalisés) reste dans la racine d'extraction.
[[nodiscard]] inline bool LinkStaysInside(const String& linkPath, const String& target) {
	if (target.IsEmpty() || target.StartsWith("/") ||
		(target.GetSize() >= 2 && target.CharAt(1) == ':'))
		return false;
	const String parent = ParentPath(linkPath);
	return NormalizePath(parent.IsEmpty() ? target : parent + "/" + target).IsOk();
}

} // namespace detail

/// Extrait toutes les entrées sous `directory`, en flux (chaque fichier est
/// copié par morceaux ; un fichier dont l'extraction échoue est supprimé).
/// Tous les chemins sont vérifiés AVANT d'écrire quoi que ce soit : un seul
/// chemin sortant de la racine (« ../ », absolu, lecteur Windows) →
/// `UNSAFE_PATH`, rien n'est écrit. Les liens symboliques sont créés en
/// dernier : aucun fichier n'est écrit à travers un lien de l'archive.
[[nodiscard]] inline Result<ExtractStats, ArchiveError>
ExtractAll(ArchiveReader& reader, const String& directory, const ExtractOptions& options = {}) {
	const std::vector<EntryInfo>& entries = reader.Entries();
	std::vector<String> targets;
	targets.reserve(entries.size());
	for (const EntryInfo& entry : entries) {
		auto normalized = NormalizePath(entry.path);
		if (normalized.IsError())
			return Err(normalized.Error());
		targets.push_back(normalized.Value());
	}
	ExtractStats stats;
	if (!directory.IsEmpty() && !sdl3::filesystem::CreateDirectory(directory))
		return Err(MakeError(ErrorKind::IO,
							 String::Format("création de %s impossible", directory.CStr())));
	auto ensureParent = [&directory](const String& target) {
		const String parent = ParentPath(target);
		return parent.IsEmpty() || sdl3::filesystem::CreateDirectory(JoinPath(directory, parent));
	};
	std::vector<size_t> links;
	for (size_t i = 0; i < entries.size(); ++i) {
		if (targets[i].IsEmpty())
			continue;
		const String destination = JoinPath(directory, targets[i]);
		if (entries[i].type == EntryType::DIRECTORY) {
			if (!sdl3::filesystem::CreateDirectory(destination))
				return Err(MakeError(ErrorKind::IO, String::Format("création de %s impossible",
																   destination.CStr())));
			++stats.directories;
			continue;
		}
		if (entries[i].type == EntryType::SYMLINK) {
			links.push_back(i);
			continue;
		}
		if (!ensureParent(targets[i]))
			return Err(
				MakeError(ErrorKind::IO, String::Format("création du dossier de %s impossible",
														destination.CStr())));
		// Un lien préexistant à cet endroit n'est jamais suivi : il est remplacé.
		const bool isLink = platform::IsSymlink(destination);
		if (!options.overwrite && (isLink || sdl3::filesystem::PathInfo(destination).IsSome())) {
			++stats.skippedFiles;
			continue;
		}
		if (isLink)
			(void)sdl3::filesystem::Remove(destination);
		auto content = reader.OpenEntry(i);
		if (content.IsError())
			return Err(content.Error());
		auto file = sdl3::IOStream::FromFile(destination, "wb");
		if (file.IsError())
			return Err(MakeError(ErrorKind::IO,
								 String::Format("écriture de %s impossible : %s",
												destination.CStr(), String(file.Error()).CStr())));
		auto copied = CopyStream(content.Value(), file.Value(), reader.EntryLimit());
		const bool closed = file.Value().Close();
		if (copied.IsError() || !closed) {
			(void)sdl3::filesystem::Remove(destination); // pas de fichier à moitié écrit
			if (copied.IsError())
				return Err(copied.Error());
			return Err(MakeError(ErrorKind::IO,
								 String::Format("écriture de %s impossible", destination.CStr())));
		}
		++stats.files;
		stats.bytes += copied.Value();
	}
	for (size_t i : links) {
		if (!options.createSymlinks) {
			++stats.skippedLinks;
			continue;
		}
		String target = entries[i].linkTarget;
		if (target.IsEmpty()) {
			auto content = reader.Extract(i); // cible stockée comme contenu
			if (content.IsOk())
				target = String(reinterpret_cast<const char*>(content.Value().data()),
								content.Value().size());
		}
		if (!detail::LinkStaysInside(targets[i], target) || !ensureParent(targets[i])) {
			++stats.skippedLinks;
			continue;
		}
		const String destination = JoinPath(directory, targets[i]);
		// Indice « dossier » (Windows) : la cible est un dossier de l'archive.
		const String parent = ParentPath(targets[i]);
		auto resolved = NormalizePath(parent.IsEmpty() ? target : parent + "/" + target);
		bool directoryHint = false;
		if (resolved.IsOk())
			for (const EntryInfo& entry : entries)
				if (entry.IsDirectory() && entry.path == resolved.Value())
					directoryHint = true;
		if (!options.overwrite && (platform::IsSymlink(destination) ||
								   sdl3::filesystem::PathInfo(destination).IsSome())) {
			++stats.skippedFiles;
			continue;
		}
		if (platform::CreateSymlink(target, destination, directoryHint).IsOk())
			++stats.links;
		else
			++stats.skippedLinks;
	}
	return Ok(stats);
}

/// Archive ouverte : lecteur, arborescence virtuelle (dossiers implicites
/// reconstitués, contenus extraits à la demande) et navigateur.
class Archive {
public:
	[[nodiscard]] static Result<Archive, ArchiveError> Open(const String& path,
															const ReadOptions& options = {}) {
		auto reader = OpenArchiveFile(path, options);
		if (reader.IsError())
			return Err(reader.Error());
		return Ok(Archive(std::move(reader).Unwrap()));
	}

	[[nodiscard]] static Result<Archive, ArchiveError>
	FromBytes(Bytes bytes, const ReadOptions& options = {}, const String& nameHint = "") {
		auto reader = OpenArchiveBytes(std::move(bytes), options, nameHint);
		if (reader.IsError())
			return Err(reader.Error());
		return Ok(Archive(std::move(reader).Unwrap()));
	}

	/// Enveloppe un lecteur déjà ouvert.
	explicit Archive(std::unique_ptr<ArchiveReader> reader) : m_reader(std::move(reader)) {
		BuildTree();
	}

	[[nodiscard]] Format GetFormat() const noexcept { return m_reader->GetFormat(); }
	[[nodiscard]] const std::vector<EntryInfo>& Entries() const noexcept {
		return m_reader->Entries();
	}
	[[nodiscard]] ArchiveReader& Reader() noexcept { return *m_reader; }
	[[nodiscard]] const VirtualFs& Fs() const noexcept { return *m_fs; }
	[[nodiscard]] Navigator& Nav() noexcept { return *m_navigator; }
	/// Entrées écartées de l'arborescence (chemins dangereux, conflits).
	[[nodiscard]] size_t SkippedEntries() const noexcept { return m_skipped; }

	[[nodiscard]] bool HasEncryptedEntries() const noexcept {
		return m_reader->HasEncryptedEntries();
	}
	/// Nouveau mot de passe ; l'arborescence est reconstruite (cibles de liens
	/// chiffrés désormais lisibles), le répertoire courant est conservé.
	void SetPassword(const String& password) {
		m_reader->SetPassword(password);
		const String cwd = m_navigator->Cwd();
		BuildTree();
		(void)m_navigator->ChangeDirectory(cwd);
	}

	[[nodiscard]] Result<Bytes, ArchiveError> Extract(size_t index) {
		return m_reader->Extract(index);
	}

	/// Contenu d'un fichier par chemin (absolu ou relatif au répertoire courant).
	[[nodiscard]] Result<Bytes, ArchiveError> Read(const String& path) {
		Option<VirtualFs::NodeId> id = m_navigator->Resolve(path);
		if (id.IsNone())
			return Err(MakeError(ErrorKind::INVALID_ARGUMENT,
								 String::Format("%s : introuvable dans l'archive", path.CStr())));
		return m_fs->ReadContent(id.Unwrap());
	}

	/// Contenu d'un fichier par chemin, en flux.
	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenFile(const String& path) {
		Option<VirtualFs::NodeId> id = m_navigator->Resolve(path);
		if (id.IsNone())
			return Err(MakeError(ErrorKind::INVALID_ARGUMENT,
								 String::Format("%s : introuvable dans l'archive", path.CStr())));
		return m_fs->OpenContent(id.Unwrap());
	}

	[[nodiscard]] Result<ExtractStats, ArchiveError>
	ExtractAll(const String& directory, const ExtractOptions& options = {}) {
		return data::archive::ExtractAll(*m_reader, directory, options);
	}

	/// Réécrit le contenu dans un autre format (conversion zip → 7z, iso → tar…).
	[[nodiscard]] Result<bool, ArchiveError> ConvertTo(const String& path, Format format,
													   const WriteOptions& options = {}) {
		return WriteArchiveFile(path, format, *m_fs, options);
	}

private:
	void BuildTree() {
		m_fs = std::make_unique<VirtualFs>();
		m_skipped = 0;
		ArchiveReader* raw = m_reader.get();
		const std::vector<EntryInfo>& entries = m_reader->Entries();
		for (size_t i = 0; i < entries.size(); ++i) {
			VirtualFs::Opener opener;
			if (entries[i].type == EntryType::FILE)
				opener = [raw, i]() { return raw->OpenEntry(i); };
			if (m_fs->AddEntry(entries[i], i, std::move(opener)).IsError())
				++m_skipped; // chemin dangereux ou conflit fichier/dossier : absent de
							 // l'arbre
		}
		m_navigator = std::make_unique<Navigator>(*m_fs);
	}

	std::unique_ptr<ArchiveReader> m_reader;
	std::unique_ptr<VirtualFs> m_fs;
	std::unique_ptr<Navigator> m_navigator;
	size_t m_skipped = 0;
};

} // namespace data::archive
