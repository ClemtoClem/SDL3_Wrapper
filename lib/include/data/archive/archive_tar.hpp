#pragma once
/**
 * data::archive — tar (POSIX ustar / pax, extensions GNU), compressé ou non
 * (tar.gz, tar.xz, tar.bz2, tar.zst).
 *
 * Lecture :
 *  - en-têtes de 512 octets, somme de contrôle (octets non signés ou signés,
 *    comme les vieux tar), nombres octaux ou base 256 (GNU, bit de poids fort)
 * ;
 *  - ustar : `prefix` + `name` (chemins jusqu'à 256 caractères) ;
 *  - GNU : noms et cibles longs (`L`, `K`), dossiers `D` ;
 *  - pax : en-têtes locaux `x` et globaux `g` — `path`, `linkpath`, `size`,
 *    `mtime` (décimal, fraction ignorée) ;
 *  - types : fichier (`0`, `\0`, `7`), lien physique (`1`, extrait via sa
 *    cible), lien symbolique (`2`), dossier (`5`) ; périphériques et FIFO
 *    ignorés ;
 *  - fichiers creux (`tar -S`) : ancien format GNU (`S`, carte dans l'en-tête
 *    et blocs d'extension) et pax GNU.sparse 0.0 (paires répétées), 0.1
 *    (`GNU.sparse.map`) et 1.0 (carte en tête des données) ; les trous sont
 *    reconstitués en zéros.
 * Le tar est parcouru directement dans son flux : les données ne sont lues
 * qu'à l'ouverture d'une entrée, en flux (les fichiers creux sont
 * reconstitués au fil de la lecture). Tar compressé : le flux décompressé est
 * gardé en mémoire s'il tient dans `ReadOptions::memoryCacheLimit` ; sinon
 * les entrées sont lues dans le flux de décompression lui-même (avancer =
 * décompresser, reculer = recommencer depuis le début) : mémoire bornée quelle
 * que soit la taille de l'archive.
 *
 * Écriture en flux (compression comprise) : ustar ; un en-tête pax `x`
 * précède toute entrée dont le chemin ne tient pas dans `prefix`/`name`, dont
 * la cible dépasse 100 octets, dont la taille dépasse 8 Gio - 1 ou dont la
 * date sort du champ octal. Fin d'archive : deux blocs nuls, total arrondi à
 * 10 240 octets (enregistrement de 20 blocs).
 *
 * Les champs d'en-tête sont du texte ASCII à largeur fixe (sans boutisme) :
 * chaque bloc de 512 octets est lu d'une traite par `ReadExact` puis analysé,
 * et composé en mémoire avant d'être écrit par `BinaryWriter`.
 */
#include "../../core/core.hpp"
#include "archive_compressed.hpp"
#include "archive_fs.hpp"
#include "archive_io.hpp"
#include "archive_stream.hpp"
#include "archive_types.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace data::archive {

namespace detail::tar {

inline constexpr size_t BLOCK = 512;
inline constexpr size_t RECORD = 10240;

/// Position et largeur des champs d'un en-tête ustar.
inline constexpr size_t NAME = 0, NAME_SIZE = 100;
inline constexpr size_t MODE = 100, UID = 108, GID = 116, ID_SIZE = 8;
inline constexpr size_t SIZE = 124, MTIME = 136, NUMBER12_SIZE = 12;
inline constexpr size_t CHECKSUM = 148, CHECKSUM_SIZE = 8;
inline constexpr size_t TYPE = 156;
inline constexpr size_t LINK = 157, LINK_SIZE = 100;
inline constexpr size_t MAGIC = 257;
inline constexpr size_t UNAME = 265, GNAME = 297, OWNER_SIZE = 32;
inline constexpr size_t PREFIX = 345, PREFIX_SIZE = 155;
/// Ancien format GNU des fichiers creux (type `S`).
inline constexpr size_t GNU_SPARSE = 386, GNU_SPARSE_ENTRIES = 4, GNU_IS_EXTENDED = 482,
						GNU_REAL_SIZE = 483;
inline constexpr size_t GNU_EXTENSION_ENTRIES = 21, GNU_EXTENSION_IS_EXTENDED = 504;
inline constexpr size_t MAX_SPARSE_SEGMENTS = 1u << 20;

struct SparseSegment {
	uint64_t offset = 0;
	uint64_t size = 0;
};

using Header = std::array<uint8_t, BLOCK>;

[[nodiscard]] String Field(const Header& header, size_t offset, size_t size);

/// Nombre octal (espaces/NUL de bourrage tolérés) ou base 256 (GNU).
[[nodiscard]] Option<uint64_t> Number(const Header& header, size_t offset, size_t size);

[[nodiscard]] bool IsZeroBlock(const Header& header) noexcept;

[[nodiscard]] bool ChecksumMatches(const Header& header);

/// Heuristique de détection : premier en-tête à somme de contrôle valide.
[[nodiscard]] bool LooksLikeTar(std::span<const uint8_t> bytes);

/// Enregistrements pax « longueur clé=valeur\n ».
[[nodiscard]] std::vector<std::pair<String, String>> ParsePax(const Bytes& data);

[[nodiscard]] Option<uint64_t> ParseDecimal(const String& text);

/// Fichier creux reconstitué : segments de données (lus dans `data`, à la
/// suite) posés sur un fond de zéros. Segments triés et disjoints.
class SparseStreamImpl final : public DecoderImpl {
public:
	SparseStreamImpl(ArchiveStream data, std::vector<SparseSegment> segments, uint64_t size,
					 StreamStatePtr state)
		: DecoderImpl(std::move(state), Some(size)), m_data(std::move(data)),
		  m_segments(std::move(segments)), m_size(size) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_data;
	std::vector<SparseSegment> m_segments;
	uint64_t m_size, m_offset = 0;
	size_t m_segment = 0;
};

/// Nom de la « méthode » d'une entrée d'un tar compressé.
[[nodiscard]] const char* ContainerMethod(Format format) noexcept;

} // namespace detail::tar

// ============================================================================
// Lecture
// ============================================================================

class TarReader final : public ArchiveReader {
public:
	/// Tar non compressé.
	[[nodiscard]] static Result<std::unique_ptr<TarReader>, ArchiveError>
	Open(ArchiveSource source, const ReadOptions& options);

	/// Tar compressé (gzip, xz, bzip2, zstd : détecté d'après la signature).
	[[nodiscard]] static Result<std::unique_ptr<TarReader>, ArchiveError>
	OpenCompressed(ArchiveSource source, const ReadOptions& options);

	[[nodiscard]] Format GetFormat() const noexcept override { return m_format; }
	[[nodiscard]] const std::vector<EntryInfo>& Entries() const noexcept override;
	void SetPassword(const String&) override {}
	[[nodiscard]] uint64_t EntryLimit() const noexcept override { return m_options.maxEntrySize; }
	/// Vrai si le tar décompressé est gardé en mémoire (accès aléatoire rapide).
	[[nodiscard]] bool IsCached() const noexcept { return m_cached; }

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenEntry(size_t index) override;

private:
	struct Record {
		uint64_t dataOffset = 0;
		uint64_t storedSize = 0; ///< octets présents dans l'archive (≠ taille réelle si creux)
		char type = '0';
		bool hardLink = false;
		bool sparse = false;
		std::vector<detail::tar::SparseSegment> segments;
	};

	TarReader(ArchiveSource source, ReadOptions options, Format format)
		: m_source(std::move(source)), m_options(std::move(options)), m_format(format) {}

	/// Flux du tar lui-même (décompressé le cas échéant).
	[[nodiscard]] sdl3::IOStream& TarStream() noexcept;

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenDecompressor();

	/// Tar compressé : en mémoire s'il tient dans `memoryCacheLimit`, sinon
	/// lu dans le flux de décompression.
	[[nodiscard]] Option<ArchiveError> OpenDecoded();

	/// Erreur du flux du tar s'il en a signalé une, sinon `fallback`.
	[[nodiscard]] ArchiveError StreamFailure(const char* fallback) const;

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenFollowing(size_t index, int depth);

	[[nodiscard]] Option<ArchiveError> Index();

	ArchiveSource m_source;
	ReadOptions m_options;
	Format m_format;
	Option<ArchiveStream> m_decoded = NONE; ///< tar décompressé (mémoire ou flux)
	bool m_cached = false;
	std::vector<EntryInfo> m_entries;
	std::vector<Record> m_records;
};

// ============================================================================
// Écriture
// ============================================================================

class TarWriter final : public ArchiveWriter {
public:
	explicit TarWriter(Format format = Format::TAR) noexcept : m_format(format) {}
	[[nodiscard]] Format GetFormat() const noexcept override { return m_format; }

	[[nodiscard]] Result<bool, ArchiveError> Write(const VirtualFs& fs, sdl3::IOStream& out,
												   const WriteOptions& options) override;

private:
	[[nodiscard]] static Result<bool, ArchiveError>
	WriteTar(const VirtualFs& fs, sdl3::IOStream& out, const WriteOptions& options);

	static void WriteHeader(BinaryWriter& writer, const String& name, const String& prefix,
							char type, uint32_t mode, uint64_t size, int64_t modified,
							const String& link);

	Format m_format;
};

} // namespace data::archive
