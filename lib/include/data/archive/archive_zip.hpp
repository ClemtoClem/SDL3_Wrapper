#pragma once
/**
 * data::archive — zip (PKWARE APPNOTE 6.3).
 *
 * Lecture :
 *  - fin du répertoire central et ZIP64 (localisateur + enregistrement 64 bits,
 *    champ extra 0x0001) ; archives précédées de données (auto-extractibles) :
 *    le décalage est déduit de la position réelle du répertoire central ;
 *  - noms UTF-8 (drapeau 11) ou CP437 ; permissions et liens symboliques Unix
 *    (attributs externes), date DOS ou horodatage étendu 0x5455 ;
 *  - méthodes : 0 stockée, 8 deflate, 9 deflate64, 12 bzip2, 14 LZMA,
 *    93 zstd, 95 XZ (LZMA2), 98 PPMd (variante I, révision 1) ;
 *  - chiffrement : WinZip AES (méthode 99, extra 0x9901, AE-1 et AE-2, clés de
 *    128 à 256 bits) avec vérificateur de mot de passe puis authentification
 *    HMAC-SHA1 des données ; chiffrement traditionnel PKWARE ;
 *  - les entrées sont lues EN FLUX : fenêtre sur l'archive → déchiffrement →
 *    décompression → contrôle de la taille et du CRC-32 (le code HMAC d'AES
 *    est vérifié quand la lecture atteint la fin des données).
 *
 * Écriture, en flux elle aussi : les petits fichiers (≤ 1 Mio) sont
 * compressés en mémoire, avec repli sur « stockée » si la compression
 * n'apporte rien ; les autres sont compressés au fil de la copie, puis
 * l'en-tête local est corrigé (sortie navigable) ou suivi d'un descripteur de
 * données (sortie séquentielle, chiffrement traditionnel). AES-256 AE-2 ou
 * traditionnel, ZIP64 automatique au-delà de 4 Gio ou 65 535 entrées,
 * horodatage étendu, permissions Unix.
 */
#include "../../core/core.hpp"
#include "archive_bzip2.hpp"
#include "archive_crc.hpp"
#include "archive_crypto.hpp"
#include "archive_deflate.hpp"
#include "archive_fs.hpp"
#include "archive_io.hpp"
#include "archive_lzma.hpp"
#include "archive_ppmd.hpp"
#include "archive_stream.hpp"
#include "archive_types.hpp"
#include "archive_zstd.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace data::archive {

// ============================================================================
// zip — constantes et outils
// ============================================================================

namespace detail::zip {

inline constexpr uint32_t LOCAL_SIGNATURE = 0x04034B50u;
inline constexpr uint32_t CENTRAL_SIGNATURE = 0x02014B50u;
inline constexpr uint32_t END_SIGNATURE = 0x06054B50u;
inline constexpr uint32_t END64_SIGNATURE = 0x06064B50u;
inline constexpr uint32_t END64_LOCATOR_SIGNATURE = 0x07064B50u;
inline constexpr uint32_t DESCRIPTOR_SIGNATURE = 0x08074B50u;

inline constexpr uint16_t METHOD_STORE = 0;
inline constexpr uint16_t METHOD_DEFLATE = 8;
inline constexpr uint16_t METHOD_DEFLATE64 = 9;
inline constexpr uint16_t METHOD_BZIP2 = 12;
inline constexpr uint16_t METHOD_LZMA = 14;
inline constexpr uint16_t METHOD_ZSTD = 93;
inline constexpr uint16_t METHOD_XZ = 95;
inline constexpr uint16_t METHOD_PPMD = 98;
inline constexpr uint16_t METHOD_AES = 99;

inline constexpr uint16_t FLAG_ENCRYPTED = 0x0001;
inline constexpr uint16_t FLAG_LZMA_END_MARKER = 0x0002;
inline constexpr uint16_t FLAG_DATA_DESCRIPTOR = 0x0008;
inline constexpr uint16_t FLAG_STRONG_ENCRYPTION = 0x0040;
inline constexpr uint16_t FLAG_UTF8 = 0x0800;

inline constexpr uint32_t UNIX_TYPE_MASK = 0170000;
inline constexpr uint32_t UNIX_DIRECTORY = 0040000;
inline constexpr uint32_t UNIX_REGULAR = 0100000;
inline constexpr uint32_t UNIX_SYMLINK = 0120000;

[[nodiscard]] const char* MethodName(uint16_t method) noexcept;

struct Record {
	uint16_t versionMadeBy = 0;
	uint16_t flags = 0;
	uint16_t method = 0; ///< méthode RÉELLE (celle cachée derrière AES)
	uint16_t dosTime = 0, dosDate = 0;
	uint32_t crc = 0;
	uint64_t compressedSize = 0;
	uint64_t uncompressedSize = 0;
	uint64_t localOffset = 0;
	uint32_t externalAttributes = 0;
	bool aes = false;
	uint16_t aesVersion = 0; ///< 1 = AE-1 (CRC vérifié), 2 = AE-2
	uint8_t aesStrength = 0; ///< 1, 2, 3 = 128, 192, 256 bits
};

[[nodiscard]] size_t AesKeyLength(uint8_t strength) noexcept;
[[nodiscard]] size_t AesSaltLength(uint8_t strength) noexcept;

/// Déchiffrement WinZip AES : `inner` couvre le chiffré suivi des 10 octets du
/// code d'authentification, vérifié quand la lecture atteint la fin.
class AesDecryptImpl final : public DecoderImpl {
public:
	AesDecryptImpl(ArchiveStream inner, uint64_t cipherLength, const Aes& aes,
				   const Hmac<Sha1>& hmac, String name, StreamStatePtr state)
		: DecoderImpl(std::move(state), Some(cipherLength)), m_inner(std::move(inner)),
		  m_cipherLength(cipherLength), m_aes(aes), m_hmac(hmac), m_ctr(aes), m_mac(hmac),
		  m_name(std::move(name)) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_inner;
	uint64_t m_cipherLength, m_read = 0;
	Aes m_aes;
	Hmac<Sha1> m_hmac;
	AesCtrWinZip m_ctr;
	Hmac<Sha1> m_mac;
	String m_name;
	bool m_verified = false;
};

/// Déchiffrement traditionnel des données qui suivent l'en-tête de 12 octets
/// (`keys` : état des clés après cet en-tête).
class ZipCryptoDecryptImpl final : public DecoderImpl {
public:
	ZipCryptoDecryptImpl(ArchiveStream inner, uint64_t length, const ZipCrypto& keys,
						 StreamStatePtr state)
		: DecoderImpl(std::move(state), Some(length)), m_inner(std::move(inner)), m_initial(keys),
		  m_keys(keys) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_inner;
	ZipCrypto m_initial, m_keys;
};

/// Chiffrement WinZip AES (AE-2) : sel et vérificateur, chiffré, puis code
/// d'authentification à la fermeture.
class AesEncryptImpl final : public EncoderImpl {
public:
	AesEncryptImpl(ArchiveStream sink, Bytes header, const Aes& aes, const Hmac<Sha1>& hmac,
				   StreamStatePtr state)
		: EncoderImpl(std::move(sink), std::move(state)), m_header(std::move(header)), m_ctr(aes),
		  m_mac(hmac) {}

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override;

private:
	Result<bool, ArchiveError> EmitHeader();

	Bytes m_header;
	AesCtrWinZip m_ctr;
	Hmac<Sha1> m_mac;
	uint8_t m_scratch[1 << 14];
};

/// Chiffrement traditionnel : en-tête de 12 octets (dont l'octet de contrôle)
/// puis les données.
class ZipCryptoEncryptImpl final : public EncoderImpl {
public:
	ZipCryptoEncryptImpl(ArchiveStream sink, Bytes header, const String& password,
						 StreamStatePtr state)
		: EncoderImpl(std::move(sink), std::move(state)), m_header(std::move(header)),
		  m_keys(password) {}

protected:
	Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) override;
	Result<bool, ArchiveError> Finish() override { return EmitHeader(); }

private:
	Result<bool, ArchiveError> EmitHeader();

	Bytes m_header;
	ZipCrypto m_keys;
	uint8_t m_scratch[1 << 14];
};

[[nodiscard]] std::span<const uint8_t> PasswordBytes(const String& password) noexcept;

} // namespace detail::zip

// ============================================================================
// zip — lecture
// ============================================================================

class ZipReader final : public ArchiveReader {
public:
	[[nodiscard]] static Result<std::unique_ptr<ZipReader>, ArchiveError>
	Open(ArchiveSource source, const ReadOptions& options);

	[[nodiscard]] Format GetFormat() const noexcept override { return Format::ZIP; }
	[[nodiscard]] const std::vector<EntryInfo>& Entries() const noexcept override;
	void SetPassword(const String& password) override;
	[[nodiscard]] uint64_t EntryLimit() const noexcept override { return m_options.maxEntrySize; }
	[[nodiscard]] const String& Comment() const noexcept { return m_comment; }

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenEntry(size_t index) override;

private:
	ZipReader(ArchiveSource source, ReadOptions options)
		: m_source(std::move(source)), m_options(std::move(options)) {}

	/// Début des données d'une entrée (après son en-tête local).
	[[nodiscard]] Result<uint64_t, ArchiveError> DataOffset(const EntryInfo& entry,
															const detail::zip::Record& record);

	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenDecoder(const EntryInfo& entry, const detail::zip::Record& record, ArchiveStream packed);

	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenAes(const EntryInfo& entry, const detail::zip::Record& record, uint64_t dataOffset);

	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenTraditional(const EntryInfo& entry, const detail::zip::Record& record,
					uint64_t dataOffset);

	[[nodiscard]] Option<ArchiveError> Index();

	/// Cible des liens symboliques (contenu de l'entrée) ; les entrées chiffrées
	/// attendent un mot de passe (`SetPassword` relance la résolution).
	void ResolveLinkTargets();

	[[nodiscard]] Option<ArchiveError> ParseExtra(const Bytes& extra, detail::zip::Record& record,
												  EntryInfo& entry);

	ArchiveSource m_source;
	ReadOptions m_options;
	std::vector<EntryInfo> m_entries;
	std::vector<detail::zip::Record> m_records;
	uint64_t m_offsetShift = 0;
	String m_comment;
};

// ============================================================================
// zip — écriture
// ============================================================================

class ZipWriter final : public ArchiveWriter {
public:
	/// Au-delà, un fichier est compressé au fil de la copie (sans repli sur
	/// « stockée ») plutôt qu'en mémoire.
	static constexpr uint64_t SMALL_ENTRY = uint64_t(1) << 20;

	[[nodiscard]] Format GetFormat() const noexcept override { return Format::ZIP; }

	[[nodiscard]] Result<bool, ArchiveError> Write(const VirtualFs& fs, sdl3::IOStream& out,
												   const WriteOptions& options) override;

private:
	/// Sortie : `stream` compte les octets écrits ; `target` (la sortie réelle)
	/// sert aux corrections d'en-tête quand `seekable`.
	struct Output {
		sdl3::IOStream& stream;
		sdl3::IOStream& target;
		uint64_t start;
		bool seekable;
		BinaryWriter writer{stream};
	};

	struct Central {
		String name;
		detail::zip::Record record;
		uint16_t versionNeeded = 20;
		int64_t modifiedTime = 0;
	};

	/// Contenu d'un fichier : en-tête local, données (compressées, chiffrées),
	/// puis correction de l'en-tête ou descripteur de données.
	[[nodiscard]] static Result<bool, ArchiveError> WriteEntry(const VirtualFs& fs,
															   VirtualFs::NodeId id, Output& output,
															   const WriteOptions& options,
															   Central& central);

	[[nodiscard]] static Result<bool, ArchiveError> WriteSmallEntry(const Bytes& data,
																	Output& output,
																	const WriteOptions& options,
																	Central& central);

	/// En-tête local ; `zip64` : tailles dans un champ ZIP64 (réservé, corrigé
	/// après coup) plutôt que dans les champs 32 bits.
	static void WriteLocalHeader(BinaryWriter& writer, const Central& central, bool zip64);

	[[nodiscard]] static uint16_t MethodFor(const WriteOptions& options, uint64_t size) noexcept;

	static void SetMethod(uint16_t method, Central& central);

	/// Version de la spécification requise pour extraire (APPNOTE 4.4.3.2).
	[[nodiscard]] static uint16_t VersionFor(uint16_t method) noexcept;

	[[nodiscard]] static Result<ArchiveStream, ArchiveError>
	OpenMethodEncoder(ArchiveStream sink, uint16_t method, const WriteOptions& options,
					  uint64_t sizeHint);

	static void MarkEncryption(const WriteOptions& options, Central& central);

	/// Flux chiffrant vers `sink` selon les options ; `checkByte` : octet de
	/// contrôle du chiffrement traditionnel.
	[[nodiscard]] static Result<ArchiveStream, ArchiveError>
	OpenEncryption(ArchiveStream sink, const WriteOptions& options, uint8_t checkByte);

	/// Extras : ZIP64 (si nécessaire), horodatage étendu, AES.
	[[nodiscard]] static Bytes BuildExtra(const detail::zip::Record& record, int64_t modifiedTime,
										  bool sizes64, bool offset64);
};

} // namespace data::archive
