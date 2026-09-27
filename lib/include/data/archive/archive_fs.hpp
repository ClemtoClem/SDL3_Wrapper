#pragma once
/**
 * data::archive — système de fichiers virtuel d'une archive.
 *
 * `VirtualFs` est un arbre de répertoires, fichiers et liens symboliques,
 * utilisé dans les deux sens :
 *
 *  - LECTURE : construit depuis les entrées d'un `ArchiveReader`
 *    (`AddEntry`), il reconstitue la hiérarchie même quand l'archive ne liste
 *    pas ses dossiers (zip « a/b/c.txt » sans entrée « a/ ») ; chaque fichier
 *    garde l'index de son entrée et, si la façade l'a branché, un « ouvreur »
 *    qui ouvre son contenu en flux à la demande ;
 *  - ÉCRITURE : on y ajoute fichiers (octets en mémoire, ou flux ouverts à
 *    l'écriture depuis le disque ou une autre archive), dossiers et liens,
 *    puis un `ArchiveWriter` le sérialise dans n'importe quel format.
 *
 * `Navigator` offre une navigation de type shell : répertoire courant,
 * `ChangeDirectory("../images")`, `List`, `Resolve`, motifs `Glob`
 * (`*`, `?`, `[abc]` dans un segment, `**` sur plusieurs).
 *
 * Tous les chemins sont NORMALISÉS (séparateur '/', « . » et « // »
 * supprimés, « .. » résolu) ; un chemin qui sortirait de la racine est
 * refusé (`UNSAFE_PATH`) — c'est la protection contre le « zip slip » lors
 * d'une extraction sur disque.
 */
#include "../../core/core.hpp"
#include "../../sdl3/filesystem.hpp"
#include "archive_io.hpp"
#include "archive_platform.hpp"
#include "archive_stream.hpp"
#include "archive_types.hpp"

#include <algorithm>
#include <functional>
#include <vector>

namespace data::archive {

// ============================================================================
// Chemins
// ============================================================================

/// Normalise un chemin d'archive. Err si le chemin remonte au-dessus de la
/// racine, est vide après normalisation, ou contient un octet nul.
[[nodiscard]] Result<String, ArchiveError> NormalizePath(const String& raw);

[[nodiscard]] String JoinPath(const String& directory, const String& name);

[[nodiscard]] String ParentPath(const String& path);

[[nodiscard]] String BaseName(const String& path);

namespace detail::glob {

/// Motif d'UN segment : `*`, `?`, `[abc]`, `[a-z]`, `[!abc]`.
[[nodiscard]] bool MatchSegment(const char* pattern, size_t pLength, const char* text,
									   size_t tLength);

[[nodiscard]] bool MatchParts(const std::vector<String>& pattern, size_t p,
									 const std::vector<String>& path, size_t t);

} // namespace detail::glob

/// Correspondance de motif sur un chemin normalisé.
[[nodiscard]] bool MatchGlob(const String& pattern, const String& path);

// ============================================================================
// VirtualFs
// ============================================================================

class VirtualFs {
public:
	using NodeId = uint32_t;
	static constexpr NodeId ROOT = 0;
	/// Ouvre le contenu d'un fichier à la demande (disque, archive…).
	using Opener = std::function<Result<ArchiveStream, ArchiveError>()>;

	struct Node {
		String name;
		NodeId parent = ROOT;
		std::vector<NodeId> children; ///< triés par nom
		EntryInfo info;				  ///< `info.path` = chemin complet normalisé
		Option<size_t> entryIndex = NONE;
		bool implicit = false; ///< dossier déduit d'un chemin, absent de l'archive
		Bytes content;
		Opener opener;

		[[nodiscard]] bool IsDirectory() const noexcept;
		[[nodiscard]] bool IsFile() const noexcept { return info.type == EntryType::FILE; }
	};

	VirtualFs();

	// ── Construction ─────────────────────────────────────────────────────────

	/// Ajoute un fichier (remplace un fichier existant au même chemin).
	Result<NodeId, ArchiveError> AddFile(const String& path, Bytes content,
										 int64_t modifiedTime = 0, uint32_t mode = 0644);

	/// Fichier dont le contenu n'est lu qu'au moment de l'écriture.
	Result<NodeId, ArchiveError> AddLazyFile(const String& path, uint64_t size, Opener opener,
											 int64_t modifiedTime = 0, uint32_t mode = 0644);

	Result<NodeId, ArchiveError> AddDirectory(const String& path, int64_t modifiedTime = 0,
											  uint32_t mode = 0755);

	Result<NodeId, ArchiveError> AddSymlink(const String& path, const String& target,
											int64_t modifiedTime = 0);

	/// Entrée d'un lecteur (garde son index ; `opener` optionnel).
	Result<NodeId, ArchiveError> AddEntry(const EntryInfo& entry, size_t entryIndex,
										  Opener opener = {});

	/// Ajoute un fichier ou un dossier du disque (récursivement) sous
	/// `archivePath` (défaut : son nom). Les contenus sont lus en flux à
	/// l'écriture. Les liens symboliques sont archivés comme liens (sauf
	/// `followSymlinks`). Rend le nombre d'entrées ajoutées.
	Result<size_t, ArchiveError> AddDiskPath(const String& diskPath, const String& archivePath = "",
											 bool recursive = true, bool followSymlinks = false);

	/// Retire un chemin (et tout son contenu pour un dossier).
	bool Remove(const String& path);

	// ── Consultation ─────────────────────────────────────────────────────────

	[[nodiscard]] const Node& Get(NodeId id) const noexcept { return m_nodes[id]; }
	[[nodiscard]] String PathOf(NodeId id) const { return m_nodes[id].info.path; }

	[[nodiscard]] Option<NodeId> Find(const String& path) const;

	[[nodiscard]] const std::vector<NodeId>& Children(NodeId id) const noexcept;

	/// Parcours en profondeur, enfants dans l'ordre alphabétique ; `visit`
	/// rend faux pour ne pas descendre dans un dossier.
	void Walk(const std::function<bool(NodeId, int)>& visit, NodeId from = ROOT) const;

	/// Tous les nœuds (hors racine) en profondeur, parents avant enfants.
	[[nodiscard]] std::vector<NodeId> Flatten(NodeId from = ROOT) const;

	[[nodiscard]] std::vector<NodeId> Glob(const String& pattern) const;

	[[nodiscard]] size_t FileCount() const;
	[[nodiscard]] uint64_t TotalSize() const;

	/// Contenu d'un fichier en FLUX : octets en mémoire (lus sur place, sans
	/// copie — le `VirtualFs` doit survivre au flux) ou ouverture à la demande.
	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenContent(NodeId id) const;

	/// Contenu d'un fichier en mémoire (au plus `limit` octets).
	[[nodiscard]] Result<Bytes, ArchiveError> ReadContent(NodeId id, uint64_t limit = uint64_t(1)
																					  << 32) const;

	/// Arborescence lisible (façon `tree`).
	[[nodiscard]] String Tree(NodeId from = ROOT, int maxDepth = 64) const;

private:
	[[nodiscard]] Option<NodeId> ChildNamed(NodeId parent, const String& name) const;

	Result<NodeId, ArchiveError> Insert(EntryInfo info);

	NodeId CreateChild(NodeId parent, const String& name, EntryInfo info);

	void DetachSubtree(NodeId id);

	bool WalkFrom(NodeId id, int depth, const std::function<bool(NodeId, int)>& visit) const;

	void AppendTree(String& out, NodeId id, const String& indent, int depth) const;

	std::vector<Node> m_nodes;
};

// ============================================================================
// Navigator
// ============================================================================

/// Navigation de type shell dans un `VirtualFs` (non possédant).
class Navigator {
public:
	explicit Navigator(const VirtualFs& fs) noexcept : m_fs(&fs) {}

	/// Chemin du répertoire courant, « / » à la racine.
	[[nodiscard]] String Cwd() const { return m_current.IsEmpty() ? String("/") : "/" + m_current; }
	[[nodiscard]] VirtualFs::NodeId Current() const;

	/// Résout un chemin absolu (« /a/b ») ou relatif au répertoire courant.
	[[nodiscard]] Option<VirtualFs::NodeId> Resolve(const String& path) const;

	/// Chemin normalisé (sans « / » initial) d'un chemin absolu ou relatif.
	[[nodiscard]] Result<String, ArchiveError> Absolute(const String& path) const;

	Result<bool, ArchiveError> ChangeDirectory(const String& path);

	/// Contenu d'un dossier (courant par défaut) ; un fichier se liste lui-même.
	[[nodiscard]] std::vector<VirtualFs::NodeId> List(const String& path = "") const;

	/// Motif relatif au répertoire courant (ou absolu).
	[[nodiscard]] std::vector<VirtualFs::NodeId> Glob(const String& pattern) const;

private:
	const VirtualFs* m_fs;
	String m_current;
};

} // namespace data::archive
