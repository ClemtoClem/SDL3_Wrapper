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

[[nodiscard]] inline const char* MethodName(uint64_t id) noexcept {
	switch (id) {
	case METHOD_COPY:
		return "copy";
	case METHOD_DELTA:
		return "delta";
	case METHOD_ARM64:
		return "arm64";
	case METHOD_LZMA2:
		return "lzma2";
	case METHOD_LZMA:
		return "lzma";
	case METHOD_BCJ:
		return "bcj";
	case METHOD_BCJ2:
		return "bcj2";
	case METHOD_PPC:
		return "ppc";
	case METHOD_IA64:
		return "ia64";
	case METHOD_ARM:
		return "arm";
	case METHOD_ARMT:
		return "armt";
	case METHOD_SPARC:
		return "sparc";
	case METHOD_ZSTD:
		return "zstd";
	case METHOD_DEFLATE:
		return "deflate";
	case METHOD_DEFLATE64:
		return "deflate64";
	case METHOD_BZIP2:
		return "bzip2";
	case METHOD_PPMD:
		return "ppmd";
	case METHOD_AES:
		return "aes-256";
	default:
		return "inconnue";
	}
}

[[nodiscard]] inline BranchKind BranchKindOf(uint64_t method) noexcept {
	switch (method) {
	case METHOD_PPC:
		return BranchKind::POWERPC;
	case METHOD_IA64:
		return BranchKind::IA64;
	case METHOD_ARM:
		return BranchKind::ARM;
	case METHOD_ARMT:
		return BranchKind::ARM_THUMB;
	case METHOD_SPARC:
		return BranchKind::SPARC;
	case METHOD_ARM64:
		return BranchKind::ARM64;
	default:
		return BranchKind::X86;
	}
}

/// Nombre 7z : les bits de tête à 1 du premier octet donnent le nombre
/// d'octets supplémentaires (petit-boutiste), le reste en est la partie haute.
[[nodiscard]] inline uint64_t ReadNumber(BinaryReader& reader) {
	const uint8_t first = reader.U8();
	uint8_t mask = 0x80;
	uint64_t value = 0;
	for (int i = 0; i < 8; ++i) {
		if ((first & mask) == 0) {
			const uint64_t high = first & (mask - 1u);
			return value + (high << (8 * i));
		}
		value |= uint64_t(reader.U8()) << (8 * i);
		mask = uint8_t(mask >> 1);
	}
	return value;
}

inline void WriteNumber(BinaryWriter& writer, uint64_t value) {
	uint8_t first = 0;
	uint8_t mask = 0x80;
	int i = 0;
	for (; i < 8; ++i) {
		if (value < (uint64_t(1) << (7 * (i + 1)))) {
			first = uint8_t(first | (value >> (8 * i)));
			break;
		}
		first = uint8_t(first | mask);
		mask = uint8_t(mask >> 1);
	}
	writer.U8(first);
	for (; i > 0; --i) {
		writer.U8(uint8_t(value));
		value >>= 8;
	}
}

/// Vecteur de bits, bit de poids fort en premier.
[[nodiscard]] inline std::vector<bool> ReadBits(BinaryReader& reader, size_t count) {
	std::vector<bool> bits(count);
	uint8_t byte = 0;
	for (size_t i = 0; i < count; ++i) {
		if (i % 8 == 0)
			byte = reader.U8();
		bits[i] = (byte & (0x80 >> (i % 8))) != 0;
	}
	return bits;
}

inline void WriteBits(BinaryWriter& writer, const std::vector<bool>& bits) {
	uint8_t byte = 0;
	for (size_t i = 0; i < bits.size(); ++i) {
		if (bits[i])
			byte = uint8_t(byte | (0x80 >> (i % 8)));
		if (i % 8 == 7) {
			writer.U8(byte);
			byte = 0;
		}
	}
	if (bits.size() % 8 != 0)
		writer.U8(byte);
}

/// « AllAreDefined » + vecteur de bits éventuel.
[[nodiscard]] inline std::vector<bool> ReadDefined(BinaryReader& reader, size_t count) {
	if (reader.U8() != 0)
		return std::vector<bool>(count, true);
	return ReadBits(reader, count);
}

struct Digests {
	std::vector<bool> defined;
	std::vector<uint32_t> values;
};

[[nodiscard]] inline Digests ReadDigests(BinaryReader& reader, size_t count) {
	Digests digests;
	digests.defined = ReadDefined(reader, count);
	digests.values.assign(count, 0);
	for (size_t i = 0; i < count; ++i)
		if (digests.defined[i])
			digests.values[i] = reader.U32Le();
	return digests;
}

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

	[[nodiscard]] uint64_t TotalInStreams() const noexcept {
		uint64_t total = 0;
		for (const Coder& coder : coders)
			total += coder.inStreams;
		return total;
	}
	[[nodiscard]] uint64_t TotalOutStreams() const noexcept {
		uint64_t total = 0;
		for (const Coder& coder : coders)
			total += coder.outStreams;
		return total;
	}
	/// Sortie principale : celle qu'aucune liaison ne consomme.
	[[nodiscard]] Option<uint64_t> MainOutStream() const {
		for (uint64_t out = 0; out < TotalOutStreams(); ++out)
			if (std::none_of(bonds.begin(), bonds.end(),
							 [out](const Bond& bond) { return bond.outIndex == out; }))
				return Some(out);
		return NONE;
	}
	[[nodiscard]] uint64_t UnpackSize() const {
		Option<uint64_t> main = MainOutStream();
		return main.IsSome() && main.Unwrap() < unpackSizes.size()
				   ? unpackSizes[size_t(main.Unwrap())]
				   : 0;
	}
	[[nodiscard]] bool IsEncrypted() const {
		return std::any_of(coders.begin(), coders.end(),
						   [](const Coder& c) { return c.method == METHOD_AES; });
	}
	[[nodiscard]] String Methods() const {
		// Ordre de lecture humain : du flux décompressé vers le flux compressé.
		String text;
		for (size_t i = coders.size(); i-- > 0;) {
			if (!text.IsEmpty())
				text.Concat(" + ");
			text.Concat(MethodName(coders[i].method));
		}
		return text;
	}
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
[[nodiscard]] inline std::array<uint8_t, 32>
DeriveAesKey(const String& password, std::span<const uint8_t> salt, uint32_t power) {
	std::array<uint8_t, 32> key{};
	const Bytes utf16 = Utf8ToUtf16Le(password);
	if (power == 0x3F) {
		size_t at = 0;
		for (size_t i = 0; i < salt.size() && at < 32; ++i)
			key[at++] = salt[i];
		for (size_t i = 0; i < utf16.size() && at < 32; ++i)
			key[at++] = utf16[i];
		return key;
	}
	Sha256 sha;
	// Les tours sont empilés dans un tampon de 1 024 tours : un seul Update par
	// lot, compressé bloc par bloc sans copie (SHA-NI si disponible).
	Bytes one(salt.begin(), salt.end());
	one.insert(one.end(), utf16.begin(), utf16.end());
	const size_t roundSize = one.size() + 8;
	static constexpr uint64_t BATCH = 1024;
	const uint64_t rounds = uint64_t(1) << power;
	Bytes batch(size_t(std::min(rounds, BATCH)) * roundSize);
	for (uint64_t r = 0; r < rounds;) {
		const uint64_t count = std::min(BATCH, rounds - r);
		for (uint64_t i = 0; i < count; ++i, ++r) {
			uint8_t* round = batch.data() + i * roundSize;
			std::memcpy(round, one.data(), one.size());
			for (int k = 0; k < 8; ++k)
				round[one.size() + size_t(k)] = uint8_t(r >> (8 * k));
		}
		sha.Update(std::span<const uint8_t>(batch.data(), size_t(count) * roundSize));
	}
	return sha.Final();
}

/// Clés déjà dérivées (mot de passe, sel, puissance) : une archive non solide
/// chiffrée a un dossier par fichier, tous avec la même clé.
class KeyCache {
public:
	[[nodiscard]] std::array<uint8_t, 32> Get(const String& password, std::span<const uint8_t> salt,
											  uint32_t power) {
		for (const Entry& entry : m_entries)
			if (entry.power == power && entry.password == password &&
				entry.salt.size() == salt.size() &&
				std::equal(salt.begin(), salt.end(), entry.salt.begin()))
				return entry.key;
		Entry entry{password, Bytes(salt.begin(), salt.end()), power,
					DeriveAesKey(password, salt, power)};
		if (m_entries.size() >= 8)
			m_entries.erase(m_entries.begin());
		m_entries.push_back(entry);
		return entry.key;
	}

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
	Open(ArchiveSource source, const ReadOptions& options) {
		std::unique_ptr<SevenZipReader> reader(new SevenZipReader(std::move(source), options));
		if (auto error = reader->Index(); error.IsSome())
			return Err(error.Unwrap());
		return Ok(std::move(reader));
	}

	[[nodiscard]] Format GetFormat() const noexcept override { return Format::SEVEN_ZIP; }
	[[nodiscard]] const std::vector<EntryInfo>& Entries() const noexcept override {
		return m_entries;
	}
	void SetPassword(const String& password) override {
		m_options.password = password;
		m_cachedFolder = NONE;
		m_cachedBytes.reset();
		m_cachedStream.reset();
		ResolveLinkTargets();
	}

	[[nodiscard]] uint64_t EntryLimit() const noexcept override { return m_options.maxEntrySize; }

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenEntry(size_t index) override {
		if (index >= m_entries.size())
			return Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("entrée inexistante")));
		const EntryInfo& entry = m_entries[index];
		const FileStream& stream = m_fileStreams[index];
		if (stream.folder.IsNone())
			return OpenMemoryStream(
				entry.type == EntryType::SYMLINK
					? Bytes(entry.linkTarget.CStr(),
							entry.linkTarget.CStr() + entry.linkTarget.GetSize())
					: Bytes());
		const size_t folderIndex = stream.folder.Unwrap();
		const detail::sevenzip::Folder& folder = m_main.folders[folderIndex];
		// Dossier à un seul fichier : flux propre ; bloc solide : fenêtre sur
		// le flux partagé du bloc.
		auto data = folder.numSubstreams == 1 && stream.offset == 0
						? OpenFolder(m_main, folderIndex)
						: OpenSolidEntry(folderIndex, stream.offset, entry.size);
		if (data.IsError())
			return data;
		return OpenCheckedStream(
			std::move(data).Unwrap(), Some(entry.size),
			m_options.verifyChecksums ? stream.crc : Option<uint32_t>(NONE), entry.path,
			folder.IsEncrypted() ? ErrorKind::WRONG_PASSWORD : ErrorKind::CORRUPT);
	}

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

	[[nodiscard]] static Option<ArchiveError> Corrupt(const char* what) {
		return Some(MakeError(ErrorKind::CORRUPT, String::Format("7z : %s", what)));
	}

	[[nodiscard]] Option<ArchiveError> ReadFolder(BinaryReader& reader,
												  detail::sevenzip::Folder& folder) {
		using namespace detail::sevenzip;
		const uint64_t coderCount = ReadNumber(reader);
		if (!reader.Ok() || coderCount == 0 || coderCount > MAX_CODERS)
			return Corrupt("nombre de codeurs invalide");
		for (uint64_t c = 0; c < coderCount; ++c) {
			Coder coder;
			const uint8_t flags = reader.U8();
			const size_t idSize = flags & 0x0F;
			if (flags & 0x80)
				return Some(MakeError(ErrorKind::UNSUPPORTED,
									  String("7z : méthodes alternatives non gérées")));
			if (idSize > 8)
				return Corrupt("identifiant de méthode trop long");
			for (size_t i = 0; i < idSize; ++i)
				coder.method = (coder.method << 8) | reader.U8();
			if (flags & 0x10) {
				coder.inStreams = ReadNumber(reader);
				coder.outStreams = ReadNumber(reader);
				if (coder.inStreams == 0 || coder.inStreams > MAX_STREAMS ||
					coder.outStreams == 0 || coder.outStreams > MAX_STREAMS)
					return Corrupt("nombre de flux de codeur invalide");
			}
			if (flags & 0x20) {
				const uint64_t size = ReadNumber(reader);
				if (size > reader.Remaining())
					return Corrupt("propriétés de codeur tronquées");
				coder.properties = reader.ReadBytes(size);
			}
			folder.coders.push_back(std::move(coder));
		}
		const uint64_t totalOut = folder.TotalOutStreams();
		const uint64_t totalIn = folder.TotalInStreams();
		if (totalOut > MAX_STREAMS || totalIn > MAX_STREAMS || totalIn < totalOut - 1)
			return Corrupt("graphe de codeurs invalide");
		for (uint64_t b = 0; b + 1 < totalOut; ++b) {
			Bond bond;
			bond.inIndex = ReadNumber(reader);
			bond.outIndex = ReadNumber(reader);
			if (bond.inIndex >= totalIn || bond.outIndex >= totalOut)
				return Corrupt("liaison de codeurs invalide");
			folder.bonds.push_back(bond);
		}
		const uint64_t packedCount = totalIn - folder.bonds.size();
		if (packedCount == 1) {
			for (uint64_t in = 0; in < totalIn; ++in) {
				if (std::none_of(folder.bonds.begin(), folder.bonds.end(),
								 [in](const Bond& b) { return b.inIndex == in; })) {
					folder.packedStreams.push_back(in);
					break;
				}
			}
		} else {
			for (uint64_t p = 0; p < packedCount; ++p) {
				const uint64_t in = ReadNumber(reader);
				if (in >= totalIn)
					return Corrupt("flux compressé de dossier invalide");
				folder.packedStreams.push_back(in);
			}
		}
		if (folder.packedStreams.size() != packedCount || folder.MainOutStream().IsNone())
			return Corrupt("graphe de codeurs incohérent");
		return reader.Ok() ? Option<ArchiveError>(NONE) : Corrupt("dossier tronqué");
	}

	[[nodiscard]] Option<ArchiveError> ReadStreamsInfo(BinaryReader& reader,
													   detail::sevenzip::StreamsInfo& info) {
		using namespace detail::sevenzip;
		uint64_t type = ReadNumber(reader);
		if (type == PACK_INFO) {
			info.packPosition = ReadNumber(reader);
			const uint64_t count = ReadNumber(reader);
			if (count > reader.Remaining())
				return Corrupt("nombre de flux compressés invalide");
			info.packSizes.assign(size_t(count), 0);
			for (;;) {
				type = ReadNumber(reader);
				if (!reader.Ok())
					return Corrupt("informations de flux tronquées");
				if (type == END)
					break;
				if (type == SIZE) {
					for (uint64_t& size : info.packSizes)
						size = ReadNumber(reader);
				} else if (type == CRC) {
					(void)ReadDigests(reader, size_t(count));
				} else {
					return Corrupt("propriété inattendue dans PackInfo");
				}
			}
			type = ReadNumber(reader);
		}
		if (type == UNPACK_INFO) {
			if (ReadNumber(reader) != FOLDER)
				return Corrupt("UnpackInfo sans dossiers");
			const uint64_t folderCount = ReadNumber(reader);
			if (folderCount > reader.Remaining())
				return Corrupt("nombre de dossiers invalide");
			if (reader.U8() != 0)
				return Some(
					MakeError(ErrorKind::UNSUPPORTED, String("7z : dossiers externes non gérés")));
			info.folders.resize(size_t(folderCount));
			uint64_t packStream = 0;
			for (Folder& folder : info.folders) {
				if (auto error = ReadFolder(reader, folder); error.IsSome())
					return error;
				folder.firstPackStream = packStream;
				packStream += folder.packedStreams.size();
			}
			if (packStream > info.packSizes.size())
				return Corrupt("dossiers plus nombreux que les flux compressés");
			if (ReadNumber(reader) != CODERS_UNPACK_SIZE)
				return Corrupt("tailles décompressées absentes");
			for (Folder& folder : info.folders) {
				folder.unpackSizes.resize(size_t(folder.TotalOutStreams()));
				for (uint64_t& size : folder.unpackSizes)
					size = ReadNumber(reader);
			}
			for (;;) {
				type = ReadNumber(reader);
				if (!reader.Ok())
					return Corrupt("UnpackInfo tronqué");
				if (type == END)
					break;
				if (type != CRC)
					return Corrupt("propriété inattendue dans UnpackInfo");
				Digests digests = ReadDigests(reader, info.folders.size());
				for (size_t f = 0; f < info.folders.size(); ++f)
					if (digests.defined[f])
						info.folders[f].crc = Some(digests.values[f]);
			}
			type = ReadNumber(reader);
		}

		// Sous-flux : par défaut un par dossier, de la taille du dossier.
		bool substreamsRead = false;
		if (type == SUBSTREAMS_INFO) {
			substreamsRead = true;
			type = ReadNumber(reader);
			if (type == NUM_UNPACK_STREAM) {
				for (Folder& folder : info.folders) {
					folder.numSubstreams = ReadNumber(reader);
					if (folder.numSubstreams > reader.Size())
						return Corrupt("nombre de sous-flux invalide");
				}
				type = ReadNumber(reader);
			}
			const bool sizesPresent = type == SIZE;
			for (const Folder& folder : info.folders) {
				if (folder.numSubstreams == 0)
					continue;
				uint64_t sum = 0;
				for (uint64_t s = 1; s < folder.numSubstreams; ++s) {
					const uint64_t size = sizesPresent ? ReadNumber(reader) : 0;
					info.substreamSizes.push_back(size);
					sum += size;
				}
				if (sum > folder.UnpackSize())
					return Corrupt("sous-flux plus grands que leur dossier");
				info.substreamSizes.push_back(folder.UnpackSize() - sum);
			}
			if (sizesPresent)
				type = ReadNumber(reader);

			size_t missing = 0;
			for (const Folder& folder : info.folders)
				if (!(folder.numSubstreams == 1 && folder.crc.IsSome()))
					missing += size_t(folder.numSubstreams);
			info.substreamCrcs.assign(info.substreamSizes.size(), NONE);
			for (;;) {
				if (!reader.Ok())
					return Corrupt("SubStreamsInfo tronqué");
				if (type == END)
					break;
				if (type != CRC)
					return Corrupt("propriété inattendue dans SubStreamsInfo");
				Digests digests = ReadDigests(reader, missing);
				size_t next = 0, stream = 0;
				for (const Folder& folder : info.folders) {
					if (folder.numSubstreams == 1 && folder.crc.IsSome()) {
						info.substreamCrcs[stream++] = folder.crc;
						continue;
					}
					for (uint64_t s = 0; s < folder.numSubstreams; ++s, ++next, ++stream)
						if (digests.defined[next])
							info.substreamCrcs[stream] = Some(digests.values[next]);
				}
				type = ReadNumber(reader);
			}
			type = ReadNumber(reader); // kEnd de SubStreamsInfo lu : place au kEnd de StreamsInfo
		}
		if (!substreamsRead) {
			for (const Folder& folder : info.folders) {
				info.substreamSizes.push_back(folder.UnpackSize());
				info.substreamCrcs.push_back(folder.crc);
			}
		} else {
			// CRC de dossier à sous-flux unique, lorsqu'aucun kCRC n'a suivi.
			size_t stream = 0;
			for (const Folder& folder : info.folders) {
				if (folder.numSubstreams == 1 && folder.crc.IsSome() &&
					stream < info.substreamCrcs.size() && info.substreamCrcs[stream].IsNone())
					info.substreamCrcs[stream] = folder.crc;
				stream += size_t(folder.numSubstreams);
			}
		}
		if (type != END || !reader.Ok())
			return Corrupt("StreamsInfo mal terminé");
		return NONE;
	}

	// ── Décodage d'un dossier ────────────────────────────────────────────────

	/// Flux décompressé (sortie principale) d'un dossier : graphe de flux
	/// construit depuis les flux compressés.
	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenFolder(const detail::sevenzip::StreamsInfo& info, size_t folderIndex) {
		using namespace detail::sevenzip;
		const Folder& folder = info.folders[folderIndex];
		if (folder.IsEncrypted() && m_options.password.IsEmpty())
			return Err(MakeError(ErrorKind::PASSWORD_REQUIRED,
								 String("7z : données chiffrées, mot de passe requis")));
		uint64_t offset = SIGNATURE_HEADER_SIZE + info.packPosition;
		for (uint64_t p = 0; p < folder.firstPackStream; ++p)
			offset += info.packSizes[size_t(p)];
		std::vector<Option<ArchiveStream>> packed;
		for (size_t p = 0; p < folder.packedStreams.size(); ++p) {
			const uint64_t size = info.packSizes[size_t(folder.firstPackStream + p)];
			if (offset > m_source.Size() || size > m_source.Size() - offset)
				return Err(
					MakeError(ErrorKind::CORRUPT,
							  String("7z : flux compressé hors du fichier (archive tronquée ?)")));
			auto window = OpenSubStream(m_source.Stream(), offset, size);
			if (window.IsError())
				return Err(window.Error());
			packed.push_back(Some(std::move(window).Unwrap()));
			offset += size;
		}
		const Option<uint64_t> main = folder.MainOutStream();
		if (main.IsNone())
			return Err(
				MakeError(ErrorKind::CORRUPT, String("7z : dossier sans sortie principale")));
		auto stream = OpenOut(folder, main.Unwrap(), packed, 0);
		if (stream.IsError())
			return stream;
		// CRC du dossier (vérifié si on le lit jusqu'au bout) ; données
		// chiffrées indécodables → mauvais mot de passe.
		return OpenCheckedStream(
			std::move(stream).Unwrap(), Some(folder.UnpackSize()),
			m_options.verifyChecksums ? folder.crc : Option<uint32_t>(NONE), String("7z"),
			folder.IsEncrypted() ? ErrorKind::WRONG_PASSWORD : ErrorKind::CORRUPT);
	}

	/// Flux de la sortie globale `outIndex` (récursivement : ses entrées sont
	/// soit des flux compressés, soit les sorties d'autres codeurs).
	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenOut(const detail::sevenzip::Folder& folder, uint64_t outIndex,
			std::vector<Option<ArchiveStream>>& packed, int depth) {
		using namespace detail::sevenzip;
		if (depth > int(MAX_CODERS))
			return Err(MakeError(ErrorKind::CORRUPT, String("7z : graphe de codeurs cyclique")));
		size_t coderIndex = 0;
		uint64_t inBase = 0, outBase = 0;
		for (; coderIndex < folder.coders.size(); ++coderIndex) {
			const Coder& c = folder.coders[coderIndex];
			if (outIndex < outBase + c.outStreams)
				break;
			inBase += c.inStreams;
			outBase += c.outStreams;
		}
		if (coderIndex == folder.coders.size() || outIndex >= folder.unpackSizes.size())
			return Err(MakeError(ErrorKind::CORRUPT, String("7z : sortie de codeur inexistante")));
		const Coder& coder = folder.coders[coderIndex];
		if (coder.outStreams != 1)
			return Err(MakeError(ErrorKind::UNSUPPORTED,
								 String("7z : codeur à plusieurs sorties non géré")));

		std::vector<ArchiveStream> inputs;
		for (uint64_t j = 0; j < coder.inStreams; ++j) {
			const uint64_t in = inBase + j;
			auto bond = std::find_if(folder.bonds.begin(), folder.bonds.end(),
									 [in](const Bond& b) { return b.inIndex == in; });
			if (bond != folder.bonds.end()) {
				auto sub = OpenOut(folder, bond->outIndex, packed, depth + 1);
				if (sub.IsError())
					return sub;
				inputs.push_back(std::move(sub).Unwrap());
				continue;
			}
			auto packedIt = std::find(folder.packedStreams.begin(), folder.packedStreams.end(), in);
			const size_t slot = size_t(packedIt - folder.packedStreams.begin());
			if (packedIt == folder.packedStreams.end() || packed[slot].IsNone())
				return Err(
					MakeError(ErrorKind::CORRUPT, String("7z : entrée de codeur non reliée")));
			inputs.push_back(std::move(packed[slot].Value()));
			packed[slot] = NONE;
		}
		return OpenCoder(coder, inputs, folder.unpackSizes[size_t(outIndex)]);
	}

	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenCoder(const detail::sevenzip::Coder& coder, std::vector<ArchiveStream>& inputs,
			  uint64_t outSize) {
		using namespace detail::sevenzip;
		auto corrupt = [](const String& message) {
			return Result<ArchiveStream, ArchiveError>(
				Err(MakeError(ErrorKind::CORRUPT, String::Format("7z : %s", message.CStr()))));
		};
		if (coder.method != METHOD_BCJ2 && inputs.size() != 1)
			return Err(MakeError(
				ErrorKind::UNSUPPORTED,
				String::Format("7z : codeur %s à plusieurs entrées", MethodName(coder.method))));
		ArchiveStream& input = inputs[0];
		const Bytes& props = coder.properties;
		switch (coder.method) {
		case METHOD_COPY:
			return Ok(std::move(input));
		case METHOD_LZMA: {
			auto decoded = LzmaProperties::Decode(props);
			if (decoded.IsError())
				return corrupt(decoded.Error());
			return OpenLzmaStream(std::move(input), decoded.Value(), outSize, true);
		}
		case METHOD_LZMA2:
			if (props.size() != 1 || props[0] > 40)
				return corrupt(String("propriétés LZMA2 invalides"));
			return OpenLzma2Stream(std::move(input), Lzma2DictionarySize(props[0]), Some(outSize));
		case METHOD_DEFLATE:
		case METHOD_DEFLATE64:
			return OpenInflateStream(std::move(input), coder.method == METHOD_DEFLATE64,
									 Some(outSize));
		case METHOD_BZIP2:
			return OpenBzip2Stream(std::move(input), Some(outSize));
		case METHOD_ZSTD:
			return OpenZstdStream(std::move(input), Some(outSize));
		case METHOD_PPMD: {
			if (props.size() != 5)
				return corrupt(String("propriétés PPMd invalides"));
			const uint32_t memory = uint32_t(props[1]) | (uint32_t(props[2]) << 8) |
									(uint32_t(props[3]) << 16) | (uint32_t(props[4]) << 24);
			return OpenPpmd7Stream(std::move(input), props[0], memory, outSize);
		}
		case METHOD_BCJ:
		case METHOD_PPC:
		case METHOD_IA64:
		case METHOD_ARM:
		case METHOD_ARMT:
		case METHOD_SPARC:
		case METHOD_ARM64:
		case METHOD_DELTA: {
			FilterSpec spec;
			if (coder.method == METHOD_DELTA) {
				if (props.size() != 1)
					return corrupt(String("propriétés Delta invalides"));
				spec.type = FilterSpec::Type::DELTA;
				spec.deltaDistance = size_t(props[0]) + 1;
			} else {
				spec.branch = BranchKindOf(coder.method);
				// Adresse de départ facultative (4 octets LE).
				if (props.size() == 4)
					spec.startOffset = uint32_t(props[0]) | (uint32_t(props[1]) << 8) |
									   (uint32_t(props[2]) << 16) | (uint32_t(props[3]) << 24);
				else if (!props.empty())
					return corrupt(
						String::Format("propriétés %s invalides", MethodName(coder.method)));
			}
			return OpenFilterStream(std::move(input), spec, false, Some(outSize));
		}
		case METHOD_BCJ2:
			if (inputs.size() != 4)
				return corrupt(String("BCJ2 attend 4 flux"));
			return OpenBcj2Stream(std::move(inputs[0]), std::move(inputs[1]), std::move(inputs[2]),
								  std::move(inputs[3]), outSize);
		case METHOD_AES: {
			if (m_options.password.IsEmpty())
				return Err(MakeError(ErrorKind::PASSWORD_REQUIRED,
									 String("7z : données chiffrées, mot de passe requis")));
			if (props.empty())
				return corrupt(String("propriétés AES absentes"));
			const uint32_t power = props[0] & 0x3F;
			size_t saltSize = 0, ivSize = 0;
			if (props[0] & 0xC0) {
				if (props.size() < 2)
					return corrupt(String("propriétés AES tronquées"));
				saltSize = ((props[0] >> 7) & 1u) + (props[1] >> 4);
				ivSize = ((props[0] >> 6) & 1u) + (props[1] & 0x0F);
				if (props.size() != 2 + saltSize + ivSize)
					return corrupt(String("propriétés AES incohérentes"));
			}
			if (power > AES_POWER_MAX && power != 0x3F)
				return Err(MakeError(
					ErrorKind::LIMIT,
					String::Format("7z : 2^%u itérations de dérivation refusées", power)));
			std::span<const uint8_t> salt(props.data() + std::min<size_t>(props.size(), 2),
										  saltSize);
			std::array<uint8_t, 16> iv{};
			for (size_t i = 0; i < ivSize; ++i)
				iv[i] = props[2 + saltSize + i];
			auto aes = Aes::Create(m_keys.Get(m_options.password, salt, power));
			if (aes.IsError())
				return corrupt(aes.Error());
			auto state = std::make_shared<StreamState>();
			return MakeStream(std::make_unique<AesCbcDecryptImpl>(std::move(input), aes.Value(), iv,
																  outSize, state),
							  state);
		}
		default:
			return Err(MakeError(ErrorKind::UNSUPPORTED,
								 String::Format("7z : méthode %s (0x%llX) non gérée",
												MethodName(coder.method),
												static_cast<unsigned long long>(coder.method))));
		}
	}

	/// Dossier entier en mémoire (en-tête encodé, cache des petits blocs).
	[[nodiscard]] Result<Bytes, ArchiveError>
	DecodeFolder(const detail::sevenzip::StreamsInfo& info, size_t folderIndex, uint64_t limit) {
		const uint64_t size = info.folders[folderIndex].UnpackSize();
		if (size > limit)
			return Err(MakeError(ErrorKind::LIMIT,
								 String::Format("7z : bloc de %llu octets au-delà de la limite",
												static_cast<unsigned long long>(size))));
		auto stream = OpenFolder(info, folderIndex);
		if (stream.IsError())
			return Err(stream.Error());
		return ReadStreamToEnd(stream.Value(), limit, size);
	}

	/// Flux décodé d'un bloc solide, partagé par ses entrées : en mémoire
	/// s'il tient dans `memoryCacheLimit`, sinon flux de décompression (lire
	/// les entrées dans l'ordre évite de recommencer le décodage).
	[[nodiscard]] Result<ArchiveStream, ArchiveError>
	OpenSolidEntry(size_t folderIndex, uint64_t offset, uint64_t size) {
		const uint64_t folderSize = m_main.folders[folderIndex].UnpackSize();
		if (m_cachedFolder.IsNone() || m_cachedFolder.Unwrap() != folderIndex) {
			m_cachedBytes.reset();
			m_cachedStream.reset();
			m_cachedFolder = NONE;
			if (folderSize <= m_options.memoryCacheLimit) {
				auto bytes = DecodeFolder(m_main, folderIndex, m_options.memoryCacheLimit);
				if (bytes.IsError())
					return Err(bytes.Error());
				m_cachedBytes = std::make_shared<const Bytes>(std::move(bytes).Unwrap());
			} else {
				auto stream = OpenFolder(m_main, folderIndex);
				if (stream.IsError())
					return stream;
				m_cachedStream = std::make_shared<ArchiveStream>(std::move(stream).Unwrap());
			}
			m_cachedFolder = Some(folderIndex);
		}
		if (m_cachedBytes)
			return OpenSharedMemoryStream(m_cachedBytes, offset, size);
		if (offset > folderSize || size > folderSize - offset)
			return Err(MakeError(ErrorKind::CORRUPT, String("7z : sous-flux hors du bloc")));
		return OpenSharedSubStream(m_cachedStream, offset, size);
	}

	// ── En-tête ──────────────────────────────────────────────────────────────

	[[nodiscard]] Option<ArchiveError> Index() {
		using namespace detail::sevenzip;
		sdl3::IOStream& stream = m_source.Stream();
		BinaryReader reader(stream);
		(void)reader.Seek(0); // la source a pu être lue ailleurs (détection du format)
		uint8_t signature[6];
		if (!reader.Read(signature, 6) || std::memcmp(signature, SIGNATURE, 6) != 0)
			return Some(MakeError(ErrorKind::FORMAT, String("7z : signature absente")));
		const uint8_t major = reader.U8();
		(void)reader.U8();
		const uint32_t startCrc = reader.U32Le();
		const uint64_t nextOffset = reader.U64Le();
		const uint64_t nextSize = reader.U64Le();
		const uint32_t nextCrc = reader.U32Le();
		if (!reader.Ok())
			return Some(MakeError(ErrorKind::FORMAT, String("7z : en-tête de signature tronqué")));
		if (major != 0)
			return Some(
				MakeError(ErrorKind::UNSUPPORTED,
						  String::Format("7z : version majeure %u non gérée", unsigned(major))));
		auto startBytes = ReadRange(stream, 12, 20);
		if (startBytes.IsError() || Crc32(startBytes.Value()) != startCrc)
			return Corrupt("CRC de l'en-tête de signature incorrect");
		if (nextSize == 0)
			return NONE; // archive vide
		const uint64_t fileSize = m_source.Size();
		if (fileSize < SIGNATURE_HEADER_SIZE || nextOffset > fileSize - SIGNATURE_HEADER_SIZE ||
			nextSize > fileSize - SIGNATURE_HEADER_SIZE - nextOffset)
			return Corrupt("en-tête hors du fichier (archive tronquée ?)");
		auto headerRead = ReadRange(stream, SIGNATURE_HEADER_SIZE + nextOffset, nextSize);
		if (headerRead.IsError())
			return Corrupt("en-tête hors du fichier (archive tronquée ?)");
		Bytes header = std::move(headerRead).Unwrap();
		if (Crc32(header) != nextCrc)
			return Corrupt("CRC de l'en-tête incorrect");

		for (int level = 0;; ++level) {
			if (level > 4)
				return Corrupt("trop de niveaux d'en-tête encodé");
			auto memory = ViewStream(header);
			if (memory.IsError())
				return Some(MakeError(ErrorKind::IO, memory.Error()));
			BinaryReader headerReader(memory.Value());
			const uint64_t type = ReadNumber(headerReader);
			if (type == HEADER)
				return ReadHeader(headerReader);
			if (type != ENCODED_HEADER)
				return Corrupt("type d'en-tête inconnu");
			StreamsInfo info;
			if (auto error = ReadStreamsInfo(headerReader, info); error.IsSome())
				return error;
			if (info.folders.empty())
				return Corrupt("en-tête encodé sans dossier");
			if (info.folders[0].IsEncrypted())
				m_headerEncrypted = true;
			// En-tête : quelques Mio au plus, décodé en mémoire.
			auto decoded = DecodeFolder(info, 0, uint64_t(1) << 30);
			if (decoded.IsError()) {
				ArchiveError error = decoded.Error();
				if (m_headerEncrypted && error.kind == ErrorKind::PASSWORD_REQUIRED)
					error.message = String("7z : liste des fichiers chiffrée, mot de passe requis");
				return Some(error);
			}
			header = std::move(decoded).Unwrap();
		}
	}

	[[nodiscard]] Option<ArchiveError> ReadHeader(BinaryReader& reader) {
		using namespace detail::sevenzip;
		const auto wrongPassword = [this](Option<ArchiveError> error) {
			// En-tête chiffré déchiffré avec une mauvaise clé : structure
			// incohérente.
			if (error.IsSome() && m_headerEncrypted && error.Value().kind == ErrorKind::CORRUPT)
				return Option<ArchiveError>(Some(
					MakeError(ErrorKind::WRONG_PASSWORD, String("7z : mot de passe incorrect"))));
			return error;
		};
		uint64_t type = ReadNumber(reader);
		if (type == ARCHIVE_PROPERTIES) {
			for (;;) {
				const uint64_t property = ReadNumber(reader);
				if (!reader.Ok())
					return wrongPassword(Corrupt("propriétés d'archive tronquées"));
				if (property == END)
					break;
				(void)reader.Skip(ReadNumber(reader));
			}
			type = ReadNumber(reader);
		}
		if (type == ADDITIONAL_STREAMS_INFO) {
			StreamsInfo additional;
			if (auto error = ReadStreamsInfo(reader, additional); error.IsSome())
				return wrongPassword(error);
			type = ReadNumber(reader);
		}
		if (type == MAIN_STREAMS_INFO) {
			if (auto error = ReadStreamsInfo(reader, m_main); error.IsSome())
				return wrongPassword(error);
			type = ReadNumber(reader);
		}
		size_t fileCount = 0;
		std::vector<String> names;
		std::vector<bool> emptyStream, emptyFile;
		std::vector<Option<int64_t>> times;
		std::vector<Option<uint32_t>> attributes;
		if (type == FILES_INFO) {
			const uint64_t count = ReadNumber(reader);
			if (count > reader.Remaining() + 1)
				return wrongPassword(Corrupt("nombre de fichiers invalide"));
			fileCount = size_t(count);
			names.assign(fileCount, String());
			emptyStream.assign(fileCount, false);
			times.assign(fileCount, NONE);
			attributes.assign(fileCount, NONE);
			size_t emptyCount = 0;
			for (;;) {
				const uint64_t property = ReadNumber(reader);
				if (!reader.Ok())
					return wrongPassword(Corrupt("FilesInfo tronqué"));
				if (property == END)
					break;
				const uint64_t size = ReadNumber(reader);
				if (size > reader.Remaining())
					return wrongPassword(Corrupt("propriété de fichier tronquée"));
				const uint64_t end = reader.Tell() + size;
				switch (property) {
				case EMPTY_STREAM:
					emptyStream = ReadBits(reader, fileCount);
					emptyCount = size_t(std::count(emptyStream.begin(), emptyStream.end(), true));
					emptyFile.assign(emptyCount, false);
					break;
				case EMPTY_FILE:
					emptyFile = ReadBits(reader, emptyCount);
					break;
				case NAME: {
					if (reader.U8() != 0)
						return Some(MakeError(ErrorKind::UNSUPPORTED,
											  String("7z : noms externes non gérés")));
					Bytes raw = reader.ReadBytes(size - 1);
					size_t start = 0, file = 0;
					for (size_t i = 0; i + 1 < raw.size() && file < fileCount; i += 2) {
						if (raw[i] == 0 && raw[i + 1] == 0) {
							names[file++] = Utf16ToUtf8(
								std::span<const uint8_t>(raw).subspan(start, i - start), false);
							start = i + 2;
						}
					}
					if (file != fileCount)
						return wrongPassword(Corrupt("noms de fichiers incomplets"));
					break;
				}
				case MTIME: {
					std::vector<bool> defined = ReadDefined(reader, fileCount);
					if (reader.U8() != 0)
						return Some(MakeError(ErrorKind::UNSUPPORTED,
											  String("7z : dates externes non gérées")));
					for (size_t i = 0; i < fileCount; ++i)
						if (defined[i])
							times[i] = Some(UnixFromFileTime(reader.U64Le()));
					break;
				}
				case WIN_ATTRIBUTES: {
					std::vector<bool> defined = ReadDefined(reader, fileCount);
					if (reader.U8() != 0)
						return Some(MakeError(ErrorKind::UNSUPPORTED,
											  String("7z : attributs externes non gérés")));
					for (size_t i = 0; i < fileCount; ++i)
						if (defined[i])
							attributes[i] = Some(reader.U32Le());
					break;
				}
				default:
					break; // CTime, ATime, Anti, StartPos, Dummy… : ignorés
				}
				if (!reader.Ok() || reader.Tell() > end)
					return wrongPassword(Corrupt("propriété de fichier mal formée"));
				(void)reader.Seek(end);
			}
			type = ReadNumber(reader);
		}
		if (type != END || !reader.Ok())
			return wrongPassword(Corrupt("en-tête mal terminé"));

		// Association fichiers ↔ sous-flux.
		size_t folder = 0, substream = 0, globalSubstream = 0, emptyIndex = 0;
		uint64_t folderOffset = 0;
		for (size_t i = 0; i < fileCount; ++i) {
			EntryInfo entry;
			FileStream stream;
			const uint32_t attribute = attributes[i].UnwrapOr(0);
			const uint32_t unixMode =
				(attribute & ATTRIBUTE_UNIX_EXTENSION) ? (attribute >> 16) : 0;
			if (emptyStream[i]) {
				const bool isEmptyFile = emptyIndex < emptyFile.size() && emptyFile[emptyIndex];
				++emptyIndex;
				// Flux vide : fichier vide si marqué kEmptyFile, dossier sinon.
				entry.type = isEmptyFile ? EntryType::FILE : EntryType::DIRECTORY;
				entry.method = String("stockée");
			} else {
				while (folder < m_main.folders.size() &&
					   substream >= m_main.folders[folder].numSubstreams) {
					++folder;
					substream = 0;
					folderOffset = 0;
				}
				if (folder >= m_main.folders.size() ||
					globalSubstream >= m_main.substreamSizes.size())
					return wrongPassword(Corrupt("plus de fichiers que de sous-flux"));
				const Folder& f = m_main.folders[folder];
				entry.type = (unixMode & 0170000) == 0120000 ? EntryType::SYMLINK : EntryType::FILE;
				entry.size = m_main.substreamSizes[globalSubstream];
				stream.folder = Some(folder);
				stream.offset = folderOffset;
				stream.crc = m_main.substreamCrcs[globalSubstream];
				entry.crc32 = stream.crc;
				entry.encrypted = f.IsEncrypted();
				entry.method = f.Methods();
				if (f.numSubstreams == 1) {
					for (size_t p = 0; p < f.packedStreams.size(); ++p)
						entry.packedSize += m_main.packSizes[size_t(f.firstPackStream + p)];
				}
				folderOffset += entry.size;
				++substream;
				++globalSubstream;
			}
			auto normalized = NormalizePath(names[i]);
			entry.path = normalized.IsOk() ? normalized.Value() : names[i];
			entry.modifiedTime = times[i].UnwrapOr(0);
			entry.mode = unixMode & 07777;
			m_entries.push_back(std::move(entry));
			m_fileStreams.push_back(stream);
		}
		ResolveLinkTargets();
		return NONE;
	}

	/// Cible des liens symboliques (contenu de l'entrée) ; les entrées chiffrées
	/// attendent un mot de passe (`SetPassword` relance la résolution).
	void ResolveLinkTargets() {
		for (size_t i = 0; i < m_entries.size(); ++i) {
			if (m_entries[i].type != EntryType::SYMLINK || m_entries[i].size > 4096 ||
				!m_entries[i].linkTarget.IsEmpty() ||
				(m_entries[i].encrypted && m_options.password.IsEmpty()))
				continue;
			auto target = Extract(i);
			if (target.IsOk())
				m_entries[i].linkTarget = String(
					reinterpret_cast<const char*>(target.Value().data()), target.Value().size());
		}
	}

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
												   const WriteOptions& options) override {
		if (options.encryption == Encryption::ZIP_CRYPTO)
			return Err(MakeError(ErrorKind::UNSUPPORTED,
								 String("7z : seul le chiffrement AES-256 existe")));
		if ((options.encryption == Encryption::AES256 || options.encryptHeaders) &&
			options.password.IsEmpty())
			return Err(MakeError(ErrorKind::INVALID_ARGUMENT,
								 String("7z : chiffrement demandé sans mot de passe")));
		if (out.Tell() >= 0 && out.GetSize() >= 0)
			return WriteSeekable(fs, out, options);
		// Sortie séquentielle : l'en-tête de signature, en tête du fichier,
		// désigne l'en-tête final → archive composée en mémoire puis copiée.
		auto memory = CreateMemoryWriter();
		if (memory.IsError())
			return Err(MakeError(ErrorKind::IO, memory.Error()));
		auto written = WriteSeekable(fs, memory.Value(), options);
		if (written.IsError())
			return written;
		const Bytes bytes = memory.Value().DynamicMemoryBytes();
		if (!out.WriteExact(bytes.data(), bytes.size()))
			return Err(MakeError(ErrorKind::IO, String("7z : écriture impossible")));
		return Ok(true);
	}

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
														   const WriteOptions& options) {
		using namespace detail::sevenzip;
		const bool encrypt = options.encryption == Encryption::AES256;
		const int64_t now = options.defaultTime != 0 ? options.defaultTime : CurrentUnixTime();
		const Sint64 start = out.Tell();
		BinaryWriter writer(out);
		writer.Fill(SIGNATURE_HEADER_SIZE); // en-tête de signature, écrit à la fin

		std::vector<FileRecord> files;
		std::vector<Folder> folders;
		std::vector<uint64_t> packSizes;
		std::vector<uint64_t> substreamsPerFolder;
		std::unique_ptr<FolderEncoder> current;
		auto closeFolder = [&]() -> Result<bool, ArchiveError> {
			auto finished = FinishFolder(*current);
			if (finished.IsError())
				return finished;
			folders.push_back(current->folder);
			packSizes.push_back(*current->packed);
			substreamsPerFolder.push_back(current->substreams);
			current.reset();
			return Ok(true);
		};

		// Taille restante annoncée : dimensionne les dictionnaires.
		const std::vector<VirtualFs::NodeId> ids = fs.Flatten();
		uint64_t remaining = 0;
		for (VirtualFs::NodeId id : ids)
			if (!fs.Get(id).IsDirectory())
				remaining += fs.Get(id).info.size;

		std::vector<uint8_t> chunk(1 << 16);
		for (VirtualFs::NodeId id : ids) {
			const VirtualFs::Node& node = fs.Get(id);
			FileRecord record;
			record.name = node.info.path;
			record.directory = node.IsDirectory();
			record.modifiedTime = node.info.modifiedTime != 0 ? node.info.modifiedTime : now;
			const uint32_t type =
				record.directory ? 0040000u
								 : (node.info.type == EntryType::SYMLINK ? 0120000u : 0100000u);
			const uint32_t mode =
				node.info.mode != 0 ? node.info.mode : (record.directory ? 0755u : 0644u);
			record.attributes = ATTRIBUTE_UNIX_EXTENSION | ((type | (mode & 07777)) << 16) |
								(record.directory ? ATTRIBUTE_DIRECTORY : ATTRIBUTE_ARCHIVE);
			if (!record.directory) {
				auto content = fs.OpenContent(id);
				if (content.IsError())
					return Err(content.Error());
				for (;;) {
					auto done = StreamRead(content.Value(), chunk.data(), chunk.size());
					if (done.IsError())
						return Err(done.Error());
					if (done.Value() == 0)
						break;
					if (record.size == 0) {
						// Premier octet du fichier : nouveau dossier si non solide
						// ou si le bloc solide est plein.
						if (current &&
							(!options.solid || current->unpacked >= options.solidBlockSize)) {
							if (auto closed = closeFolder(); closed.IsError())
								return closed;
						}
						if (!current) {
							const uint64_t hint = options.solid
													  ? std::min(options.solidBlockSize, remaining)
													  : node.info.size;
							auto opened = OpenFolderEncoder(out, options, encrypt,
															std::max<uint64_t>(hint, done.Value()));
							if (opened.IsError())
								return Err(opened.Error());
							current = std::move(opened).Unwrap();
						}
					}
					const std::span<const uint8_t> view(chunk.data(), done.Value());
					if (auto written = StreamWrite(*current->input, view); written.IsError())
						return written;
					record.crc = Crc32(view, record.crc);
					record.size += view.size();
					current->unpacked += view.size();
				}
				record.hasStream = record.size > 0;
				if (record.hasStream)
					++current->substreams;
				remaining -= std::min(remaining, node.info.size);
				if (!options.solid && current) {
					if (auto closed = closeFolder(); closed.IsError())
						return closed;
				}
			}
			files.push_back(std::move(record));
		}
		if (current) {
			if (auto closed = closeFolder(); closed.IsError())
				return closed;
		}
		uint64_t packedTotal = 0;
		for (uint64_t size : packSizes)
			packedTotal += size;

		// En-tête principal.
		auto headerStream = CreateMemoryWriter();
		if (headerStream.IsError())
			return Err(MakeError(ErrorKind::IO, headerStream.Error()));
		BinaryWriter header(headerStream.Value());
		header.U8(HEADER);
		if (!folders.empty()) {
			header.U8(MAIN_STREAMS_INFO);
			WriteStreamsInfo(header, 0, packSizes, folders);
			header.U8(SUBSTREAMS_INFO);
			if (std::any_of(substreamsPerFolder.begin(), substreamsPerFolder.end(),
							[](uint64_t n) { return n != 1; })) {
				header.U8(NUM_UNPACK_STREAM);
				for (uint64_t count : substreamsPerFolder)
					WriteNumber(header, count);
				header.U8(SIZE);
				size_t file = 0;
				for (uint64_t count : substreamsPerFolder) {
					// Toutes les tailles sauf la dernière (déduite de celle du dossier).
					for (uint64_t s = 0; s < count; ++s) {
						while (!files[file].hasStream)
							++file;
						if (s + 1 < count)
							WriteNumber(header, files[file].size);
						++file;
					}
				}
			}
			header.U8(CRC);
			header.U8(1); // tous définis
			for (const FileRecord& record : files)
				if (record.hasStream)
					header.U32Le(record.crc);
			header.U8(END);
			header.U8(END);
		}
		if (!files.empty()) {
			header.U8(FILES_INFO);
			WriteNumber(header, files.size());
			std::vector<bool> emptyStream, emptyFile;
			for (const FileRecord& record : files) {
				emptyStream.push_back(!record.hasStream);
				if (!record.hasStream)
					emptyFile.push_back(!record.directory);
			}
			auto property = [&header](uint8_t id, const Bytes& body) {
				header.U8(id);
				WriteNumber(header, body.size());
				header.Write(body);
			};
			auto encode = [](const std::function<void(BinaryWriter&)>& fill) {
				auto stream = CreateMemoryWriter();
				if (stream.IsError())
					return Bytes();
				BinaryWriter writer(stream.Value());
				fill(writer);
				return stream.Value().DynamicMemoryBytes();
			};
			if (std::find(emptyStream.begin(), emptyStream.end(), true) != emptyStream.end()) {
				property(EMPTY_STREAM, encode([&](BinaryWriter& w) { WriteBits(w, emptyStream); }));
				if (std::find(emptyFile.begin(), emptyFile.end(), true) != emptyFile.end())
					property(EMPTY_FILE, encode([&](BinaryWriter& w) { WriteBits(w, emptyFile); }));
			}
			property(NAME, encode([&](BinaryWriter& w) {
						 w.U8(0);
						 for (const FileRecord& record : files)
							 w.Write(Utf8ToUtf16Le(record.name, true));
					 }));
			property(MTIME, encode([&](BinaryWriter& w) {
						 w.U8(1);
						 w.U8(0);
						 for (const FileRecord& record : files)
							 w.U64Le(FileTimeFromUnix(record.modifiedTime));
					 }));
			property(WIN_ATTRIBUTES, encode([&](BinaryWriter& w) {
						 w.U8(1);
						 w.U8(0);
						 for (const FileRecord& record : files)
							 w.U32Le(record.attributes);
					 }));
			header.U8(END);
		}
		header.U8(END);
		if (!header.Ok())
			return Err(
				MakeError(ErrorKind::IO, String("7z : construction de l'en-tête impossible")));
		Bytes rawHeader = headerStream.Value().DynamicMemoryBytes();

		// En-tête encodé : compressé en LZMA (et chiffré si demandé).
		Bytes finalHeader = rawHeader;
		Bytes headerPack;
		if (!rawHeader.empty()) {
			WriteOptions headerOptions = options;
			headerOptions.compression = Compression::LZMA;
			headerOptions.level = std::max(options.level, 5);
			headerOptions.dictionarySize = 1u << 20;
			auto folder = EncodeInMemory(rawHeader, headerOptions, options.encryptHeaders);
			if (folder.IsError())
				return Err(folder.Error());
			folder.Value().first.crc = Some(Crc32(rawHeader));
			auto encodedStream = CreateMemoryWriter();
			if (encodedStream.IsError())
				return Err(MakeError(ErrorKind::IO, encodedStream.Error()));
			BinaryWriter encoded(encodedStream.Value());
			encoded.U8(ENCODED_HEADER);
			WriteStreamsInfo(encoded, packedTotal, {uint64_t(folder.Value().second.size())},
							 {folder.Value().first});
			encoded.U8(END); // fin du StreamsInfo (pas de SubStreamsInfo ici)
			Bytes candidate = encodedStream.Value().DynamicMemoryBytes();
			if (options.encryptHeaders ||
				candidate.size() + folder.Value().second.size() < rawHeader.size()) {
				headerPack = std::move(folder.Value().second);
				finalHeader = std::move(candidate);
			}
		}

		// En-tête compressé puis en-tête final, après les flux compressés ;
		// l'en-tête de signature (au début) les désigne.
		const uint64_t nextOffset = packedTotal + headerPack.size();
		writer.Write(headerPack);
		writer.Write(finalHeader);
		const Sint64 end = out.Tell();
		auto startStream = CreateMemoryWriter();
		if (startStream.IsError())
			return Err(MakeError(ErrorKind::IO, startStream.Error()));
		BinaryWriter startHeader(startStream.Value());
		startHeader.U64Le(nextOffset);
		startHeader.U64Le(finalHeader.size());
		startHeader.U32Le(Crc32(finalHeader));
		const Bytes startBytes = startStream.Value().DynamicMemoryBytes();
		(void)writer.Seek(uint64_t(start));
		writer.Write(SIGNATURE, 6);
		writer.U8(0);
		writer.U8(4);
		writer.U32Le(Crc32(startBytes));
		writer.Write(startBytes);
		(void)writer.Seek(uint64_t(end));
		if (!writer.Ok() || end < 0)
			return Err(MakeError(ErrorKind::IO, String("7z : écriture impossible")));
		return Ok(true);
	}

	/// Codeur de compression pour les options (méthode et propriétés).
	[[nodiscard]] static detail::sevenzip::Coder CompressionCoder(const WriteOptions& options,
																  uint64_t sizeHint) {
		using namespace detail::sevenzip;
		Coder coder;
		switch (options.level == 0 ? Compression::STORE : options.compression) {
		case Compression::STORE:
			coder.method = METHOD_COPY;
			break;
		case Compression::DEFLATE:
			coder.method = METHOD_DEFLATE;
			break;
		case Compression::LZMA: {
			LzmaProperties props;
			// Inutile de dépasser la taille des données (le décodeur alloue le
			// dictionnaire).
			props.dictionarySize = uint32_t(
				std::min<uint64_t>(options.dictionarySize, std::max<uint64_t>(sizeHint, 1u << 16)));
			coder.method = METHOD_LZMA;
			const auto encoded = props.Encode();
			coder.properties.assign(encoded.begin(), encoded.end());
			break;
		}
		case Compression::LZMA2:
			coder.method = METHOD_LZMA2;
			coder.properties = {Lzma2DictionaryByte(uint32_t(std::min<uint64_t>(
				options.dictionarySize, std::max<uint64_t>(sizeHint, 1u << 16))))};
			break;
		case Compression::BZIP2:
			coder.method = METHOD_BZIP2;
			break;
		case Compression::ZSTD:
			// Propriétés de 7-Zip ZS : version de zstd (1.5) et niveau.
			coder.method = METHOD_ZSTD;
			coder.properties = {1, 5, uint8_t(std::clamp(options.level, 1, 22))};
			break;
		case Compression::PPMD: {
			coder.method = METHOD_PPMD;
			const uint32_t memory = std::clamp<uint32_t>(options.ppmdMemoryMb, 1, 2048) << 20;
			coder.properties = {uint8_t(std::clamp(options.ppmdOrder, 2u, 32u)), uint8_t(memory),
								uint8_t(memory >> 8), uint8_t(memory >> 16), uint8_t(memory >> 24)};
			break;
		}
		}
		return coder;
	}

	/// Flux compressant selon `coder` vers `sink`.
	[[nodiscard]] static Result<ArchiveStream, ArchiveError>
	OpenCompression(ArchiveStream sink, const detail::sevenzip::Coder& coder,
					const WriteOptions& options, uint64_t sizeHint) {
		using namespace detail::sevenzip;
		const int level = std::clamp(options.level, 1, 9);
		const Bytes& props = coder.properties;
		switch (coder.method) {
		case METHOD_DEFLATE:
			return OpenDeflateEncoder(std::move(sink), level);
		case METHOD_LZMA: {
			auto decoded = LzmaProperties::Decode(props);
			if (decoded.IsError())
				return Err(MakeError(ErrorKind::INVALID_ARGUMENT, decoded.Error()));
			return OpenLzmaEncoder(std::move(sink), decoded.Value(), level, false, sizeHint);
		}
		case METHOD_LZMA2:
			return OpenLzma2Encoder(std::move(sink), Lzma2DictionarySize(props[0]), level,
									sizeHint);
		case METHOD_BZIP2:
			return OpenBzip2Encoder(std::move(sink), level);
		case METHOD_ZSTD:
			return OpenZstdEncoder(std::move(sink), props[2]);
		case METHOD_PPMD:
			return OpenPpmd7Encoder(std::move(sink), props[0],
									uint32_t(props[1]) | (uint32_t(props[2]) << 8) |
										(uint32_t(props[3]) << 16) | (uint32_t(props[4]) << 24));
		default:
			return Ok(std::move(sink));
		}
	}

	/// Ouvre un dossier écrit dans `target` : compression, puis AES-256 si
	/// `encrypt` (codeur 0 = AES qui lit le flux compressé, codeur 1 =
	/// compression, liaison entrée 1 ← sortie 0 : l'ordre de 7-Zip).
	[[nodiscard]] Result<std::unique_ptr<FolderEncoder>, ArchiveError>
	OpenFolderEncoder(sdl3::IOStream& target, const WriteOptions& options, bool encrypt,
					  uint64_t sizeHint) {
		using namespace detail::sevenzip;
		auto encoder = std::make_unique<FolderEncoder>();
		auto sink = OpenCountingSink(target, encoder->packed);
		if (sink.IsError())
			return Err(sink.Error());
		Coder coder = CompressionCoder(options, sizeHint);
		Result<ArchiveStream, ArchiveError> compressorSink = std::move(sink);
		if (encrypt) {
			auto iv = SecureRandomBytes(16);
			if (iv.IsError())
				return Err(MakeError(ErrorKind::IO, iv.Error()));
			auto aes = Aes::Create(m_keys.Get(options.password, {}, AES_POWER_WRITE));
			if (aes.IsError())
				return Err(MakeError(ErrorKind::IO, aes.Error()));
			std::array<uint8_t, 16> ivArray{};
			std::memcpy(ivArray.data(), iv.Value().data(), 16);
			auto state = std::make_shared<StreamState>();
			auto aesStream =
				MakeStream(std::make_unique<AesCbcEncryptImpl>(std::move(compressorSink).Unwrap(),
															   aes.Value(), ivArray, state),
						   state);
			if (aesStream.IsError())
				return Err(aesStream.Error());
			encoder->aes = std::make_unique<ArchiveStream>(std::move(aesStream).Unwrap());
			compressorSink = OpenCountingSink(encoder->aes->io, encoder->compressed);
			if (compressorSink.IsError())
				return Err(compressorSink.Error());
			Coder aesCoder;
			aesCoder.method = METHOD_AES;
			aesCoder.properties = {uint8_t(0x40 | AES_POWER_WRITE), 0x0F};
			aesCoder.properties.insert(aesCoder.properties.end(), iv.Value().begin(),
									   iv.Value().end());
			encoder->folder.coders.push_back(std::move(aesCoder));
			encoder->folder.bonds = {Bond{1, 0}};
		}
		auto input = OpenCompression(std::move(compressorSink).Unwrap(), coder, options, sizeHint);
		if (input.IsError())
			return Err(input.Error());
		encoder->input = std::make_unique<ArchiveStream>(std::move(input).Unwrap());
		encoder->folder.coders.push_back(std::move(coder));
		encoder->folder.packedStreams = {0};
		return Ok(std::move(encoder));
	}

	/// Termine un dossier : vide compresseur et chiffreur, note les tailles.
	[[nodiscard]] static Result<bool, ArchiveError> FinishFolder(FolderEncoder& encoder) {
		if (auto finished = FinishStream(*encoder.input); finished.IsError())
			return finished;
		if (encoder.aes) {
			if (auto finished = FinishStream(*encoder.aes); finished.IsError())
				return finished;
			encoder.folder.unpackSizes = {*encoder.compressed, encoder.unpacked};
		} else {
			encoder.folder.unpackSizes = {encoder.unpacked};
		}
		return Ok(true);
	}

	/// Compresse (et chiffre) un bloc en mémoire : rend le dossier et son flux
	/// compressé (en-tête encodé).
	[[nodiscard]] Result<std::pair<detail::sevenzip::Folder, Bytes>, ArchiveError>
	EncodeInMemory(const Bytes& data, const WriteOptions& options, bool encrypt) {
		auto memory = CreateMemoryWriter();
		if (memory.IsError())
			return Err(MakeError(ErrorKind::IO, memory.Error()));
		auto encoder = OpenFolderEncoder(memory.Value(), options, encrypt, data.size());
		if (encoder.IsError())
			return Err(encoder.Error());
		if (auto written = StreamWrite(*encoder.Value()->input, data); written.IsError())
			return Err(written.Error());
		encoder.Value()->unpacked = data.size();
		if (auto finished = FinishFolder(*encoder.Value()); finished.IsError())
			return Err(finished.Error());
		return Ok(std::make_pair(encoder.Value()->folder, memory.Value().DynamicMemoryBytes()));
	}

	static void WriteStreamsInfo(BinaryWriter& writer, uint64_t packPosition,
								 const std::vector<uint64_t>& packSizes,
								 const std::vector<detail::sevenzip::Folder>& folders) {
		using namespace detail::sevenzip;
		writer.U8(PACK_INFO);
		WriteNumber(writer, packPosition);
		WriteNumber(writer, packSizes.size());
		writer.U8(SIZE);
		for (uint64_t size : packSizes)
			WriteNumber(writer, size);
		writer.U8(END);

		writer.U8(UNPACK_INFO);
		writer.U8(FOLDER);
		WriteNumber(writer, folders.size());
		writer.U8(0);
		for (const Folder& folder : folders) {
			WriteNumber(writer, folder.coders.size());
			for (const Coder& coder : folder.coders) {
				uint8_t id[8];
				size_t idSize = 0;
				for (uint64_t method = coder.method; method != 0; method >>= 8)
					++idSize;
				idSize = std::max<size_t>(idSize, 1);
				for (size_t i = 0; i < idSize; ++i)
					id[i] = uint8_t(coder.method >> (8 * (idSize - 1 - i)));
				const bool complex = coder.inStreams != 1 || coder.outStreams != 1;
				writer.U8(
					uint8_t(idSize | (complex ? 0x10 : 0) | (coder.properties.empty() ? 0 : 0x20)));
				writer.Write(id, idSize);
				if (complex) {
					WriteNumber(writer, coder.inStreams);
					WriteNumber(writer, coder.outStreams);
				}
				if (!coder.properties.empty()) {
					WriteNumber(writer, coder.properties.size());
					writer.Write(coder.properties);
				}
			}
			for (const Bond& bond : folder.bonds) {
				WriteNumber(writer, bond.inIndex);
				WriteNumber(writer, bond.outIndex);
			}
			if (folder.packedStreams.size() > 1)
				for (uint64_t in : folder.packedStreams)
					WriteNumber(writer, in);
		}
		writer.U8(CODERS_UNPACK_SIZE);
		for (const Folder& folder : folders)
			for (uint64_t size : folder.unpackSizes)
				WriteNumber(writer, size);
		const bool anyCrc = std::any_of(folders.begin(), folders.end(),
										[](const Folder& f) { return f.crc.IsSome(); });
		if (anyCrc) {
			writer.U8(CRC);
			std::vector<bool> defined;
			for (const Folder& folder : folders)
				defined.push_back(folder.crc.IsSome());
			const bool all = std::all_of(defined.begin(), defined.end(), [](bool d) { return d; });
			writer.U8(all ? 1 : 0);
			if (!all)
				WriteBits(writer, defined);
			for (const Folder& folder : folders)
				if (folder.crc.IsSome())
					writer.U32Le(folder.crc.Unwrap());
		}
		writer.U8(END);
	}

	detail::sevenzip::KeyCache m_keys;
};

} // namespace data::archive
