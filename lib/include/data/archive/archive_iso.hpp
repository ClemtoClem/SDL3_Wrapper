#pragma once
/**
 * data::archive — images disque ISO 9660 (ECMA-119), Joliet et Rock Ridge.
 *
 * Particularité du format : les nombres sont stockés « dans les deux sens »
 * (32 bits petit-boutiste immédiatement suivis du même nombre en gros-boutiste,
 * `U32LeBe`) ; les tables de chemins existent en deux exemplaires, L
 * (petit-boutiste, `U32Le`) et M (gros-boutiste, `U32Be`) ; les noms Joliet
 * sont en UCS-2 gros-boutiste.
 *
 * Lecture :
 *  - descripteurs de volume à partir du secteur 16 (primaire, supplémentaire
 *    Joliet, terminateur) ; les tailles du descripteur primaire sont lues en
 *    `U32LeBe` STRICT (les deux moitiés doivent concorder) ;
 *  - arborescence : Rock Ridge si présent (noms complets `NM`, permissions
 *    `PX`, liens symboliques `SL`, dates `TF`, zones de continuation `CE`,
 *    dossiers relogés `CL`/`RE`), sinon Joliet, sinon noms ISO (« ;1 » ôté) ;
 *  - fichiers multi-extents (> 4 Gio), parcours protégé contre les boucles.
 *
 * Écriture (contenus copiés en flux) : descripteur primaire avec Rock Ridge
 * (noms, modes, liens, dates) et noms ISO niveau 2 dédoublonnés, descripteur
 * Joliet (noms UCS-2 jusqu'à 103 caractères, sans liens), tables de chemins L
 * et M des deux arbres. Images vérifiées avec isoinfo, xorriso et 7-Zip.
 */
#include "../../core/core.hpp"
#include "archive_fs.hpp"
#include "archive_io.hpp"
#include "archive_stream.hpp"
#include "archive_types.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace data::archive {

namespace detail::iso {

inline constexpr uint64_t SECTOR = 2048;
inline constexpr uint64_t FIRST_DESCRIPTOR = 16;
inline constexpr uint8_t DESCRIPTOR_PRIMARY = 1;
inline constexpr uint8_t DESCRIPTOR_SUPPLEMENTARY = 2;
inline constexpr uint8_t DESCRIPTOR_TERMINATOR = 255;
inline constexpr uint8_t FLAG_DIRECTORY = 0x02;
inline constexpr uint8_t FLAG_MULTI_EXTENT = 0x80;
inline constexpr size_t RECORD_BASE = 33;
inline constexpr size_t MAX_DEPTH = 64;
inline constexpr size_t JOLIET_NAME_MAX = 103;

[[nodiscard]] bool IsJolietEscape(const uint8_t* escape) noexcept;

/// Date d'enregistrement de 7 octets → secondes Unix.
[[nodiscard]] int64_t UnixFromRecordDate(const uint8_t* date) noexcept;

void WriteRecordDate(BinaryWriter& writer, int64_t unix);

/// Date de descripteur de volume : 16 chiffres ASCII + décalage.
void WriteVolumeDate(BinaryWriter& writer, int64_t unix);

/// Nom de fichier ISO : « ;1 » et point final retirés.
[[nodiscard]] String CleanIsoName(String name);

} // namespace detail::iso

// ============================================================================
// Lecture
// ============================================================================

class IsoReader final : public ArchiveReader {
public:
	[[nodiscard]] static Result<std::unique_ptr<IsoReader>, ArchiveError>
	Open(ArchiveSource source, const ReadOptions& options);

	/// Vrai si l'image contient un descripteur ISO 9660 au secteur 16.
	[[nodiscard]] static bool LooksLikeIso(sdl3::IOStream& stream);

	[[nodiscard]] Format GetFormat() const noexcept override { return Format::ISO9660; }
	[[nodiscard]] const std::vector<EntryInfo>& Entries() const noexcept override;
	void SetPassword(const String&) override {}

	[[nodiscard]] const String& VolumeLabel() const noexcept { return m_volumeLabel; }
	/// « rock ridge », « joliet » ou « iso9660 ».
	[[nodiscard]] const char* NamingScheme() const noexcept;

	[[nodiscard]] uint64_t EntryLimit() const noexcept override { return m_options.maxEntrySize; }

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenEntry(size_t index) override;

private:
	struct Extent {
		uint64_t sector = 0;
		uint64_t size = 0;
	};

	/// Enregistrement de répertoire décodé.
	struct Record {
		uint64_t sector = 0;
		uint64_t size = 0;
		uint8_t flags = 0;
		int64_t modifiedTime = 0;
		String name;
		bool self = false, parent = false;
		// Rock Ridge
		bool hasRockRidge = false;
		String rrName;
		Option<uint32_t> mode = NONE;
		String linkTarget;
		bool symlink = false;
		Option<uint64_t> childLink = NONE; ///< CL : dossier relogé
		bool relocated = false;			   ///< RE : à masquer
		bool linkContinues = false;		   ///< SL : dernier composant coupé
	};

	IsoReader(ArchiveSource source, ReadOptions options)
		: m_source(std::move(source)), m_options(std::move(options)) {}

	[[nodiscard]] Option<ArchiveError> Index();

	[[nodiscard]] Option<ArchiveError> Walk(const Record& directory, const String& prefix,
											bool joliet, size_t depth);

	[[nodiscard]] Result<std::vector<Record>, ArchiveError> ReadDirectory(const Record& directory,
																		  bool joliet);

	/// Entrées SUSP / Rock Ridge d'une zone système (ou de continuation).
	void ParseSusp(const Bytes& area, Record& record, bool rootSelf, int hops);

	ArchiveSource m_source;
	ReadOptions m_options;
	std::vector<EntryInfo> m_entries;
	std::vector<std::vector<Extent>> m_extents;
	std::set<uint64_t> m_visited;
	String m_volumeLabel;
	uint8_t m_suspSkip = 0;
	bool m_rockRidge = false;
	bool m_joliet = false;
};

// ============================================================================
// Écriture
// ============================================================================

class IsoWriter final : public ArchiveWriter {
public:
	[[nodiscard]] Format GetFormat() const noexcept override { return Format::ISO9660; }

	[[nodiscard]] Result<bool, ArchiveError> Write(const VirtualFs& fs, sdl3::IOStream& out,
												   const WriteOptions& options) override;

private:
	struct Node {
		String name;	///< nom complet (Rock Ridge / Joliet)
		String isoName; ///< nom ISO niveau 2 dédoublonné
		String jolietName;
		size_t parent = 0;
		std::vector<size_t> children;
		bool directory = false;
		bool symlink = false;
		String linkTarget;
		uint32_t mode = 0;
		int64_t modifiedTime = 0;
		uint64_t size = 0;						 ///< taille du contenu d'un fichier
		Option<VirtualFs::NodeId> source = NONE; ///< contenu copié en flux à l'écriture…
		Bytes buffered; ///< …ou déjà en mémoire (taille inconnue d'avance)
		uint16_t directoryNumber = 0;
		uint64_t primarySector = 0, jolietSector = 0, dataSector = 0;
		uint64_t primarySize = 0, jolietSize = 0;
	};

	[[nodiscard]] static uint64_t SectorsFor(uint64_t bytes) noexcept;

	[[nodiscard]] Option<ArchiveError> Collect(VirtualFs::NodeId id, size_t parent, size_t depth);

	/// Noms ISO (d-caractères, 30 max, « NOM.EXT;1 ») et Joliet, uniques par
	/// dossier.
	void AssignIsoNames();

	[[nodiscard]] static String TruncateUtf8(const String& text, size_t bytes);

	[[nodiscard]] Bytes EncodedName(const Node& node, bool joliet) const;

	[[nodiscard]] Bytes PathTable(const std::vector<size_t>& directories, bool joliet,
								  bool bigEndian) const;

	/// Contenu d'un répertoire ; l'arbre primaire porte les entrées Rock Ridge
	/// (celles qui débordent vont dans la zone de continuation).
	[[nodiscard]] Bytes DirectoryBytes(size_t index, bool joliet);

	/// `kind` : 1 = « . » de la racine (SP + ER), 2 = « . » ou « .. », 0 = entrée
	/// nommée.
	[[nodiscard]] Bytes Record(size_t index, const Bytes& name, bool joliet, int kind);

	/// Entrées Rock Ridge : SP/ER (racine), PX, TF, NM, SL.
	[[nodiscard]] Bytes SystemUse(const Node& node, int kind) const;

	void WriteDescriptor(BinaryWriter& writer, const WriteOptions& options, bool joliet,
						 uint32_t totalSectors, uint32_t pathTableSize, uint32_t pathL,
						 uint32_t pathM);

	[[nodiscard]] Bytes RootRecord(bool joliet) const;

	const VirtualFs* m_fs = nullptr;
	std::vector<Node> m_nodes;
	Bytes m_continuation;
	uint64_t m_continuationSector = 0;
	int64_t m_now = 0;
};

} // namespace data::archive
