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
[[nodiscard]] Option<Format> DetectFormat(sdl3::IOStream& stream);

[[nodiscard]] Option<Format> FormatFromExtension(const String& path);

[[nodiscard]] const char* FormatExtension(Format format) noexcept;

[[nodiscard]] bool HasArchiveExtension(const String& path);

/// Vrai si le format peut être écrit (RAR : lecture seule).
[[nodiscard]] bool IsWritableFormat(Format format) noexcept;

// ============================================================================
// Lecture générique
// ============================================================================

/// Ouvre une archive de n'importe quel format. `nameHint` (nom du fichier)
/// nomme l'entrée d'un fichier compressé seul qui ne porte pas son nom
/// d'origine ; `volumes` ouvre les volumes suivants d'un RAR multi-volumes.
[[nodiscard]] Result<std::unique_ptr<ArchiveReader>, ArchiveError>
OpenArchive(ArchiveSource source, const ReadOptions& options = {}, const String& nameHint = "",
			RarVolumeOpener volumes = {});

/// Volumes suivants d'un RAR multi-volumes, cherchés à côté de `path`.
[[nodiscard]] RarVolumeOpener DiskVolumeOpener(const String& path);

[[nodiscard]] Result<std::unique_ptr<ArchiveReader>, ArchiveError>
OpenArchiveFile(const String& path, const ReadOptions& options = {});

[[nodiscard]] Result<std::unique_ptr<ArchiveReader>, ArchiveError>
OpenArchiveBytes(Bytes bytes, const ReadOptions& options = {}, const String& nameHint = "");

// ============================================================================
// Écriture générique
// ============================================================================

[[nodiscard]] std::unique_ptr<ArchiveWriter> CreateWriter(Format format);

[[nodiscard]] Result<bool, ArchiveError> WriteArchive(Format format, const VirtualFs& fs,
															 sdl3::IOStream& out,
															 const WriteOptions& options = {});

[[nodiscard]] Result<Bytes, ArchiveError>
WriteArchiveBytes(Format format, const VirtualFs& fs, const WriteOptions& options = {});

/// Écrit dans un fichier temporaire voisin puis le renomme : une archive
/// existante n'est jamais laissée à moitié écrite.
[[nodiscard]] Result<bool, ArchiveError> WriteArchiveFile(const String& path, Format format,
																 const VirtualFs& fs,
																 const WriteOptions& options = {});

/// Archive un fichier ou dossier du disque (format déduit de l'extension si
/// `format` est NONE).
[[nodiscard]] Result<size_t, ArchiveError> PackPath(const String& diskPath,
														   const String& archivePath,
														   Option<Format> format = NONE,
														   const WriteOptions& options = {});

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
[[nodiscard]] bool LinkStaysInside(const String& linkPath, const String& target);

} // namespace detail

/// Extrait toutes les entrées sous `directory`, en flux (chaque fichier est
/// copié par morceaux ; un fichier dont l'extraction échoue est supprimé).
/// Tous les chemins sont vérifiés AVANT d'écrire quoi que ce soit : un seul
/// chemin sortant de la racine (« ../ », absolu, lecteur Windows) →
/// `UNSAFE_PATH`, rien n'est écrit. Les liens symboliques sont créés en
/// dernier : aucun fichier n'est écrit à travers un lien de l'archive.
[[nodiscard]] Result<ExtractStats, ArchiveError>
ExtractAll(ArchiveReader& reader, const String& directory, const ExtractOptions& options = {});

/// Archive ouverte : lecteur, arborescence virtuelle (dossiers implicites
/// reconstitués, contenus extraits à la demande) et navigateur.
class Archive {
public:
	[[nodiscard]] static Result<Archive, ArchiveError> Open(const String& path,
															const ReadOptions& options = {});

	[[nodiscard]] static Result<Archive, ArchiveError>
	FromBytes(Bytes bytes, const ReadOptions& options = {}, const String& nameHint = "");

	/// Enveloppe un lecteur déjà ouvert.
	explicit Archive(std::unique_ptr<ArchiveReader> reader);

	[[nodiscard]] Format GetFormat() const noexcept { return m_reader->GetFormat(); }
	[[nodiscard]] const std::vector<EntryInfo>& Entries() const noexcept;
	[[nodiscard]] ArchiveReader& Reader() noexcept { return *m_reader; }
	[[nodiscard]] const VirtualFs& Fs() const noexcept { return *m_fs; }
	[[nodiscard]] Navigator& Nav() noexcept { return *m_navigator; }
	/// Entrées écartées de l'arborescence (chemins dangereux, conflits).
	[[nodiscard]] size_t SkippedEntries() const noexcept { return m_skipped; }

	[[nodiscard]] bool HasEncryptedEntries() const noexcept;
	/// Nouveau mot de passe ; l'arborescence est reconstruite (cibles de liens
	/// chiffrés désormais lisibles), le répertoire courant est conservé.
	void SetPassword(const String& password);

	[[nodiscard]] Result<Bytes, ArchiveError> Extract(size_t index);

	/// Contenu d'un fichier par chemin (absolu ou relatif au répertoire courant).
	[[nodiscard]] Result<Bytes, ArchiveError> Read(const String& path);

	/// Contenu d'un fichier par chemin, en flux.
	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenFile(const String& path);

	[[nodiscard]] Result<ExtractStats, ArchiveError>
	ExtractAll(const String& directory, const ExtractOptions& options = {});

	/// Réécrit le contenu dans un autre format (conversion zip → 7z, iso → tar…).
	[[nodiscard]] Result<bool, ArchiveError> ConvertTo(const String& path, Format format,
													   const WriteOptions& options = {});

private:
	void BuildTree();

	std::unique_ptr<ArchiveReader> m_reader;
	std::unique_ptr<VirtualFs> m_fs;
	std::unique_ptr<Navigator> m_navigator;
	size_t m_skipped = 0;
};

} // namespace data::archive
