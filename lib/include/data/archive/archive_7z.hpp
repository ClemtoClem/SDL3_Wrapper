#pragma once
/**
 * data::archive — 7z (format 7-Zip 0.4).
 *
 * Structure : en-tête de signature de 32 octets (CRC, position et CRC de
 * l'en-tête final), flux compressés (« pack streams »), puis l'en-tête — le
 * plus souvent lui-même compressé (`kEncodedHeader`), voire chiffré.
 *
 * Lecture :
 *  - nombres 7z (1 à 9 octets, longueur dans les bits de tête du premier) ;
 *  - dossiers (« folders ») décrits comme un GRAPHE de codeurs : chaque
 *    codeur a des flux d'entrée (côté compressé) et de sortie, reliés par des
 *    liaisons ; le décodeur est générique et gère donc aussi BCJ2 (4 entrées) ;
 *  - codeurs : Copy, LZMA, LZMA2, Deflate, Deflate64, BZip2, PPMd (H), zstd
 *    (7-Zip ZS), AES-256 (7zAES : SHA-256 itéré 2^N fois, CBC), filtres BCJ
 *    x86/PPC/IA64/ARM/ARMT/SPARC/ARM64, BCJ2, Delta ;
 *  - chaque dossier est un GRAPHE DE FLUX (fenêtre → AES → décompresseur →
 *    filtre) lu à la demande ; un bloc solide qui tient dans
 *    `ReadOptions::memoryCacheLimit` est décodé une fois et gardé en mémoire,
 *    sinon ses fichiers sont des fenêtres sur un flux de décompression
 *    partagé (les lire dans l'ordre évite de recommencer) ;
 *  - noms UTF-16LE, dossiers et fichiers vides, date de modification
 *    (FILETIME), attributs Windows et extension Unix (bit 15 : mode dans les
 *    16 bits hauts), liens symboliques (mode S_IFLNK, contenu = cible) ;
 *  - mot de passe : sans lui, `PASSWORD_REQUIRED` (à l'ouverture si
 *    l'en-tête est chiffré, à l'extraction sinon) ; un mauvais mot de passe
 *    se manifeste par un flux indécodable ou un CRC faux → `WRONG_PASSWORD`.
 *
 * Écriture en flux : blocs solides de `WriteOptions::solidBlockSize` au plus
 * (ou un dossier par fichier) ; Copy, Deflate, LZMA, LZMA2, BZip2, PPMd ou
 * zstd ; chiffrement AES-256 optionnel des données et de l'en-tête
 * (`WriteOptions::encryptHeaders`, qui masque aussi les noms) ; en-tête
 * compressé en LZMA. Sortie non navigable (tube) : archive composée en
 * mémoire puis copiée. Archives vérifiées avec 7-Zip.
 *
 * Tous les champs (signature, nombres, CRC, FILETIME, attributs) passent par
 * `BinaryReader`/`BinaryWriter`, donc par les `ReadU32Le`/`WriteU64Le`… de
 * `sdl3::IOStream`.
 */
#include "../../core/core.hpp"
#include "archive_bzip2.hpp"
#include "archive_crc.hpp"
#include "archive_crypto.hpp"
#include "archive_deflate.hpp"
#include "archive_filters.hpp"
#include "archive_fs.hpp"
#include "archive_io.hpp"
#include "archive_lzma.hpp"
#include "archive_ppmd.hpp"
#include "archive_stream.hpp"
#include "archive_types.hpp"
#include "archive_zstd.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace data::archive {

namespace detail::sevenzip {

inline constexpr uint8_t SIGNATURE[6] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
inline constexpr size_t SIGNATURE_HEADER_SIZE = 32;

enum Property : uint8_t {
	END = 0x00,
	HEADER = 0x01,
	ARCHIVE_PROPERTIES = 0x02,
	ADDITIONAL_STREAMS_INFO = 0x03,
	MAIN_STREAMS_INFO = 0x04,
	FILES_INFO = 0x05,
	PACK_INFO = 0x06,
	UNPACK_INFO = 0x07,
	SUBSTREAMS_INFO = 0x08,
	SIZE = 0x09,
	CRC = 0x0A,
	FOLDER = 0x0B,
	CODERS_UNPACK_SIZE = 0x0C,
	NUM_UNPACK_STREAM = 0x0D,
	EMPTY_STREAM = 0x0E,
	EMPTY_FILE = 0x0F,
	ANTI = 0x10,
	NAME = 0x11,
	CTIME = 0x12,
	ATIME = 0x13,
	MTIME = 0x14,
	WIN_ATTRIBUTES = 0x15,
	COMMENT = 0x16,
	ENCODED_HEADER = 0x17,
	START_POS = 0x18,
	DUMMY = 0x19,
};

// Identifiants de méthodes (octets de l'identifiant, gros-boutiste).
inline constexpr uint64_t METHOD_COPY = 0x00;
inline constexpr uint64_t METHOD_DELTA = 0x03;
inline constexpr uint64_t METHOD_ARM64 = 0x0A;
inline constexpr uint64_t METHOD_LZMA2 = 0x21;
inline constexpr uint64_t METHOD_LZMA = 0x030101;
inline constexpr uint64_t METHOD_BCJ = 0x03030103;
inline constexpr uint64_t METHOD_BCJ2 = 0x0303011B;
inline constexpr uint64_t METHOD_PPC = 0x03030205;
inline constexpr uint64_t METHOD_IA64 = 0x03030401;
inline constexpr uint64_t METHOD_ARM = 0x03030501;
inline constexpr uint64_t METHOD_ARMT = 0x03030701;
inline constexpr uint64_t METHOD_SPARC = 0x03030805;
inline constexpr uint64_t METHOD_DEFLATE = 0x040108;
inline constexpr uint64_t METHOD_DEFLATE64 = 0x040109;
inline constexpr uint64_t METHOD_BZIP2 = 0x040202;
inline constexpr uint64_t METHOD_PPMD = 0x030401;
inline constexpr uint64_t METHOD_ZSTD = 0x04F71101; ///< 7-Zip ZS (zstd)
inline constexpr uint64_t METHOD_AES = 0x06F10701;

inline constexpr uint32_t ATTRIBUTE_DIRECTORY = 0x10;
inline constexpr uint32_t ATTRIBUTE_ARCHIVE = 0x20;
inline constexpr uint32_t ATTRIBUTE_UNIX_EXTENSION = 0x8000;

/// Garde-fous contre les en-têtes forgés (allocations démesurées).
inline constexpr uint64_t MAX_CODERS = 64;
inline constexpr uint64_t MAX_STREAMS = 64;
inline constexpr uint32_t AES_POWER_WRITE = 19;
inline constexpr uint32_t AES_POWER_MAX = 24;

[[nodiscard]] const char* MethodName(uint64_t id) noexcept;

[[nodiscard]] BranchKind BranchKindOf(uint64_t method) noexcept;

/// Nombre 7z : les bits de tête à 1 du premier octet donnent le nombre
/// d'octets supplémentaires (petit-boutiste), le reste en est la partie haute.
[[nodiscard]] uint64_t ReadNumber(BinaryReader& reader);

void WriteNumber(BinaryWriter& writer, uint64_t value);

/// Vecteur de bits, bit de poids fort en premier.
[[nodiscard]] std::vector<bool> ReadBits(BinaryReader& reader, size_t count);

void WriteBits(BinaryWriter& writer, const std::vector<bool>& bits);

/// « AllAreDefined » + vecteur de bits éventuel.
[[nodiscard]] std::vector<bool> ReadDefined(BinaryReader& reader, size_t count);

struct Digests {
	std::vector<bool> defined;
	std::vector<uint32_t> values;
};

[[nodiscard]] Digests ReadDigests(BinaryReader& reader, size_t count);

struct Coder {
	uint64_t method = 0;
	uint64_t inStreams = 1;	 ///< côté compressé
	uint64_t outStreams = 1; ///< côté décompressé
	Bytes properties;
};

struct Bond {
	uint64_t inIndex = 0;  ///< entrée (globale) d'un codeur…
	uint64_t outIndex = 0; ///< …alimentée par cette sortie (globale)
};

struct Folder {
	std::vector<Coder> coders;
	std::vector<Bond> bonds;
	std::vector<uint64_t> packedStreams; ///< entrées globales lues dans les flux compressés
	std::vector<uint64_t> unpackSizes;	 ///< une par sortie globale
	Option<uint32_t> crc = NONE;
	uint64_t firstPackStream = 0;
	uint64_t numSubstreams = 1;

	[[nodiscard]] uint64_t TotalInStreams() const noexcept;
	[[nodiscard]] uint64_t TotalOutStreams() const noexcept;
	/// Sortie principale : celle qu'aucune liaison ne consomme.
	[[nodiscard]] Option<uint64_t> MainOutStream() const;
	[[nodiscard]] uint64_t UnpackSize() const;
	[[nodiscard]] bool IsEncrypted() const;
	[[nodiscard]] String Methods() const;
};

struct StreamsInfo {
	uint64_t packPosition = 0;
	std::vector<uint64_t> packSizes;
	std::vector<Folder> folders;
	std::vector<uint64_t> substreamSizes; ///< tous dossiers confondus
	std::vector<Option<uint32_t>> substreamCrcs;
};

/// Clé AES du format 7z : SHA-256 d'un flux de 2^power répétitions de
/// (sel, mot de passe UTF-16LE, compteur 64 bits LE) ; power 0x3F = clé brute.
[[nodiscard]] std::array<uint8_t, 32>
DeriveAesKey(const String& password, std::span<const uint8_t> salt, uint32_t power);

/// Clés déjà dérivées (mot de passe, sel, puissance) : une archive non solide
/// chiffrée a un dossier par fichier, tous avec la même clé.
class KeyCache {
public:
	[[nodiscard]] std::array<uint8_t, 32> Get(const String& password, std::span<const uint8_t> salt,
											  uint32_t power);

private:
	struct Entry {
		String password;
		Bytes salt;
		uint32_t power = 0;
		std::array<uint8_t, 32> key{};
	};
	std::vector<Entry> m_entries;
};

} // namespace detail::sevenzip

// ============================================================================
// Lecture
// ============================================================================

class SevenZipReader final : public ArchiveReader {
public:
	[[nodiscard]] static Result<std::unique_ptr<SevenZipReader>, ArchiveError>
	Open(ArchiveSource source, const ReadOptions& options);

	[[nodiscard]] Format GetFormat() const noexcept override { return Format::SEVEN_ZIP; }
	[[nodiscard]] const std::vector<EntryInfo>& Entries() const noexcept override;
	void SetPassword(const String& password) override;

	[[nodiscard]] uint64_t EntryLimit() const noexcept override { return m_options.maxEntrySize; }

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenEntry(size_t index) override;

	/// Vrai si l'en-tête (la liste des fichiers) est chiffré.
	[[nodiscard]] bool HeaderEncrypted() const noexcept { return m_headerEncrypted; }

private:
	struct FileStream {
		Option<size_t> folder = NONE;
		uint64_t offset = 0; ///< dans les données décompressées du dossier
		Option<uint32_t> crc = NONE;
	};

	SevenZipReader(ArchiveSource source, ReadOptions options)
		: m_source(std::move(source)), m_options(std::move(options)) {}

	// ── Analyse des structures ───────────────────────────────────────────────

	[[nodiscard]] static Option<ArchiveError> Corrupt(const char* what);

	[[nodiscard]] Option<ArchiveError> ReadFolder(BinaryReader& reader,
												  detail::sevenzip::Folder& folder);

	[[nodiscard]] Option<ArchiveError> ReadStreamsInfo(BinaryReader& reader,
													   detail::sevenzip::StreamsInfo& info);

	// ── Décodage d'un dossier ────────────────────────────────────────────────

	/// Flux décompressé (sortie principale) d'un dossier : graphe de flux
	/// construit depuis les flux compressés.
	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenFolder(const detail::sevenzip::StreamsInfo& info, size_t folderIndex);

	/// Flux de la sortie globale `outIndex` (récursivement : ses entrées sont
	/// soit des flux compressés, soit les sorties d'autres codeurs).
	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenOut(const detail::sevenzip::Folder& folder, uint64_t outIndex,
			std::vector<Option<ArchiveStream>>& packed, int depth);

	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenCoder(const detail::sevenzip::Coder& coder, std::vector<ArchiveStream>& inputs,
			  uint64_t outSize);

	/// Dossier entier en mémoire (en-tête encodé, cache des petits blocs).
	[[nodiscard]] Result<Bytes, ArchiveError>
	DecodeFolder(const detail::sevenzip::StreamsInfo& info, size_t folderIndex, uint64_t limit);

	/// Flux décodé d'un bloc solide, partagé par ses entrées : en mémoire
	/// s'il tient dans `memoryCacheLimit`, sinon flux de décompression (lire
	/// les entrées dans l'ordre évite de recommencer le décodage).
	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenSolidEntry(size_t folderIndex, uint64_t offset, uint64_t size);

	// ── En-tête ──────────────────────────────────────────────────────────────

	[[nodiscard]] Option<ArchiveError> Index();

	[[nodiscard]] Option<ArchiveError> ReadHeader(BinaryReader& reader);

	/// Cible des liens symboliques (contenu de l'entrée) ; les entrées chiffrées
	/// attendent un mot de passe (`SetPassword` relance la résolution).
	void ResolveLinkTargets();

	ArchiveSource m_source;
	ReadOptions m_options;
	detail::sevenzip::StreamsInfo m_main;
	std::vector<EntryInfo> m_entries;
	std::vector<FileStream> m_fileStreams;
	detail::sevenzip::KeyCache m_keys;
	Option<size_t> m_cachedFolder = NONE;		   ///< bloc solide ouvert
	std::shared_ptr<const Bytes> m_cachedBytes;	   ///< …décodé en mémoire
	std::shared_ptr<ArchiveStream> m_cachedStream; ///< …ou en flux
	bool m_headerEncrypted = false;
};

// ============================================================================
// Écriture
// ============================================================================

class SevenZipWriter final : public ArchiveWriter {
public:
	[[nodiscard]] Format GetFormat() const noexcept override { return Format::SEVEN_ZIP; }

	[[nodiscard]] Result<bool, ArchiveError> Write(const VirtualFs& fs, sdl3::IOStream& out,
												   const WriteOptions& options) override;

private:
	struct FileRecord {
		String name;
		bool hasStream = false;
		bool directory = false;
		int64_t modifiedTime = 0;
		uint32_t attributes = 0;
		uint64_t size = 0;
		uint32_t crc = 0;
	};

	/// Dossier en cours d'écriture : on écrit dans `input` ; compression puis
	/// chiffrement éventuel jusqu'à la sortie.
	struct FolderEncoder {
		detail::sevenzip::Folder folder;
		std::unique_ptr<ArchiveStream> aes;
		std::unique_ptr<ArchiveStream> input;
		std::shared_ptr<uint64_t> packed =
			std::make_shared<uint64_t>(0); ///< octets écrits dans l'archive
		std::shared_ptr<uint64_t> compressed =
			std::make_shared<uint64_t>(0); ///< sortis du compresseur
		uint64_t unpacked = 0;
		uint64_t substreams = 0;
	};

	[[nodiscard]] Result<bool, ArchiveError> WriteSeekable(const VirtualFs& fs, sdl3::IOStream& out,
														   const WriteOptions& options);

	/// Codeur de compression pour les options (méthode et propriétés).
	[[nodiscard]] static detail::sevenzip::Coder CompressionCoder(const WriteOptions& options,
																  uint64_t sizeHint);

	/// Flux compressant selon `coder` vers `sink`.
	[[nodiscard]] static Result<ArchiveStream, ArchiveError>
	OpenCompression(ArchiveStream sink, const detail::sevenzip::Coder& coder,
					const WriteOptions& options, uint64_t sizeHint);

	/// Ouvre un dossier écrit dans `target` : compression, puis AES-256 si
	/// `encrypt` (codeur 0 = AES qui lit le flux compressé, codeur 1 =
	/// compression, liaison entrée 1 ← sortie 0 : l'ordre de 7-Zip).
	[[nodiscard]] Result<std::unique_ptr<FolderEncoder>, ArchiveError>
	OpenFolderEncoder(sdl3::IOStream& target, const WriteOptions& options, bool encrypt,
					  uint64_t sizeHint);

	/// Termine un dossier : vide compresseur et chiffreur, note les tailles.
	[[nodiscard]] static Result<bool, ArchiveError> FinishFolder(FolderEncoder& encoder);

	/// Compresse (et chiffre) un bloc en mémoire : rend le dossier et son flux
	/// compressé (en-tête encodé).
	[[nodiscard]] Result<std::pair<detail::sevenzip::Folder, Bytes>, ArchiveError>
	EncodeInMemory(const Bytes& data, const WriteOptions& options, bool encrypt);

	static void WriteStreamsInfo(BinaryWriter& writer, uint64_t packPosition,
								 const std::vector<uint64_t>& packSizes,
								 const std::vector<detail::sevenzip::Folder>& folders);

	detail::sevenzip::KeyCache m_keys;
};

} // namespace data::archive
